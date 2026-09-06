// gcsec-dump — retrouve le GameCardInitialData dans la mémoire du processus
// FS par sa signature unique: [package_id(8) || zeros(8)] + SHA-256(fenêtre
// 0x200) == initial_data_hash du header XCI. Double contrainte = zéro faux
// positif possible. Aucune identification T1/T2, aucun branchement par jeu:
// la signature vient de deux fichiers de config sur la SD.
//
// Usage: insérer la cartouche, lancer le .nro, lire initial_data.bin sur la SD.
// Build: devkitPro + devkitA64 + libnx (make).

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <mbedtls/sha256.h>

#define HASH_LEN      32
#define BLOCK_LEN     0x200
#define SIG_LEN       16          // package_id(8) || zeros(8)
#define SCAN_CHUNK    (1 << 20)
#define CHUNK_OVERLAP 0x200
#define MAX_PIDS      300

static u8  g_expected_hash[HASH_LEN];
static u64 g_package_id;
static u8  g_sig[SIG_LEN];

static char g_out_name[128];
static char g_ctx_name[128];

static bool parse_hex(const char *hex, u8 *out, size_t len) {
    if (strlen(hex) < len * 2) return false;
    for (size_t i = 0; i < len; i++) {
        char c = hex[i * 2], c2 = hex[i * 2 + 1];
        u8 hi, lo;
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else return false;
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        else return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}

static bool load_config(void) {
    FILE *f = fopen("sdmc:/gcsec-dump/config.txt", "r");
    if (!f) return false;
    char hash_line[128] = {0}, pid_line[128] = {0};
    bool ok = fgets(hash_line, sizeof(hash_line), f) != NULL &&
              fgets(pid_line, sizeof(pid_line), f) != NULL;
    fclose(f);
    if (!ok) return false;
    // retirer les retours à la ligne
    hash_line[strcspn(hash_line, "\r\n")] = 0;
    pid_line[strcspn(pid_line, "\r\n")] = 0;
    if (!parse_hex(hash_line, g_expected_hash, HASH_LEN)) return false;
    u8 pid[8];
    if (!parse_hex(pid_line, pid, 8)) return false;
    memcpy(&g_package_id, pid, 8);
    // signature: package_id || zeros(8)
    memcpy(g_sig, pid, 8);
    memset(g_sig + 8, 0, 8);
    snprintf(g_out_name, sizeof(g_out_name), "sdmc:/gcsec-dump/initial_data.bin");
    snprintf(g_ctx_name, sizeof(g_ctx_name), "sdmc:/gcsec-dump/context.bin");
    return true;
}

// scanne un buffer à la recherche de la signature + vérifie le hash de la
// fenêtre 0x200. Retourne l'offset du match (ou -1).
static long scan_buffer(const u8 *buf, size_t len, const u8 *hash_out) {
    if (len < BLOCK_LEN) return -1;
    for (size_t i = 0; i + BLOCK_LEN <= len; i++) {
        if (memcmp(buf + i, g_sig, SIG_LEN) != 0) continue;
        u8 calc[HASH_LEN];
        mbedtls_sha256(buf + i, BLOCK_LEN, calc, 0);
        if (memcmp(calc, g_expected_hash, HASH_LEN) == 0) {
            if (hash_out) memcpy(hash_out, calc, HASH_LEN);
            return (long)i;
        }
    }
    return -1;
}

// vérifie (sans lecture) que la fenêtre est plausible: reserved = zéros
static bool window_reserved_zeros(const u8 *win) {
    // reserved[0x1C4] commence à 0x3C
    for (size_t i = 0x3C; i < BLOCK_LEN; i++)
        if (win[i] != 0) return false;
    return true;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    printf("gcsec-dump — GameCardInitialData recovery par signature\n");
    printf("========================================================\n");

    fsdevMountSdmc();

    if (!load_config()) {
        printf("\nERREUR: sdmc:/gcsec-dump/config.txt absent ou invalide\n");
        printf("  ligne 1: initial_data_hash (64 hex)\n");
        printf("  ligne 2: package_id (16 hex)\n");
        printf("\nAppuyez sur + pour quitter.\n");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 1;
    }
    printf("signature chargée: package_id=%016lx\n", g_package_id);

    // énumérer les processus et scanner la mémoire debuggable de chacun
    u64 pids[MAX_PIDS];
    s32 num = 0;
    Result rc = svcGetProcessList(&num, pids, MAX_PIDS);
    if (R_FAILED(rc)) {
        printf("\nERREUR: svcGetProcessList: 0x%x\n", rc);
        printf("\nAppuyez sur + pour quitter.\n");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 1;
    }
    printf("%d processus énumérés — scan en cours…\n", num);

    int matches = 0;
    int dbg_ok = 0;
    static u8 chunk[SCAN_CHUNK + CHUNK_OVERLAP];

    for (s32 i = 0; i < num && !matches; i++) {
        Handle dbg = 0;
        rc = svcDebugActiveProcess(&dbg, pids[i]);
        if (R_FAILED(rc)) continue;   // kernel/illustre: non debuggable, normal
        dbg_ok++;

        u64 prog_id = 0;
        svcGetInfo(&prog_id, InfoType_ProgramId, dbg, 0);

        // marche de la memory map du processus debuggé: seule la mémoire
        // mappée+lisible est lue (les trous unmapped sont sautés)
        u64 addr = 0x0;
        while (addr < 0x800000000ULL && !matches) {
            MemoryInfo mi;
            u32 pi = 0;
            if (R_FAILED(svcQueryDebugProcessMemory(&mi, &pi, dbg, addr))) break;
            if ((mi.perm & MemPerm_Read) && mi.size > 0 &&
                mi.size < 0x100000000ULL) {
                u64 r_pos = mi.addr;
                u64 r_end = mi.addr + mi.size;
                size_t buf_len = 0;
                u64 buf_base = r_pos;
                bool r_done = false;
                while (!r_done && !matches) {
                    if (buf_len > sizeof(chunk) - SCAN_CHUNK) {
                        // glisser en conservant les derniers BLOCK_LEN-1
                        // octets (fenêtres à cheval sur deux chunks)
                        size_t keep = BLOCK_LEN - 1;
                        memmove(chunk, chunk + buf_len - keep, keep);
                        buf_base += buf_len - keep;
                        buf_len = keep;
                    }
                    u64 left = r_end - (buf_base + buf_len);
                    size_t want = (size_t)(left > SCAN_CHUNK ? SCAN_CHUNK : left);
                    if (buf_len + want > sizeof(chunk))
                        want = sizeof(chunk) - buf_len;
                    if (want == 0) { r_done = true; break; }
                    if (R_FAILED(svcReadDebugProcessMemory(
                            chunk + buf_len, dbg, buf_base + buf_len, want))) {
                        r_done = true; break;   // région partiellement protégée
                    }
                    buf_len += want;
                    for (size_t j = 0; j + BLOCK_LEN <= buf_len; j++) {
                        if (memcmp(chunk + j, g_sig, SIG_LEN) != 0) continue;
                        u8 calc[HASH_LEN];
                        mbedtls_sha256_ret(chunk + j, BLOCK_LEN, calc, 0);
                        if (memcmp(calc, g_expected_hash, HASH_LEN) != 0) continue;
                        // ✓ signature + hash: l'artefact est trouvé
                        const u8 *win = chunk + j;
                        printf("\n✓ SIGNATURE TROUVÉE (pid %lu, prog %016lx, "
                               "addr %016llx)\n",
                               (unsigned long)pids[i],
                               (unsigned long long)prog_id,
                               (unsigned long long)(buf_base + j));
                        FILE *fo = fopen(g_out_name, "wb");
                        if (fo) {
                            fwrite(win, 1, BLOCK_LEN, fo);
                            fclose(fo);
                            printf("→ %s écrit (0x200 B)\n", g_out_name);
                        }
                        size_t cstart = j > 0x1000 ? j - 0x1000 : 0;
                        size_t cend = j + BLOCK_LEN + 0x1000;
                        if (cend > buf_len) cend = buf_len;
                        FILE *fc = fopen(g_ctx_name, "wb");
                        if (fc) {
                            fwrite(chunk + cstart, 1, cend - cstart, fc);
                            fclose(fc);
                            printf("→ %s écrit (0x%zx B)\n", g_ctx_name,
                                   cend - cstart);
                        }
                        matches++;
                        break;
                    }
                    size_t keep = buf_len > (BLOCK_LEN - 1) ? (BLOCK_LEN - 1)
                                                            : buf_len;
                    memmove(chunk, chunk + buf_len - keep, keep);
                    buf_base += buf_len - keep;
                    buf_len = keep;
                    if (buf_base + buf_len >= r_end) r_done = true;
                }
            }
            addr = mi.addr + mi.size;
            if (mi.size == 0) addr = mi.addr + 0x1000;   // garde anti-boucle
        }
        svcCloseHandle(dbg);
        printf(".");
        consoleUpdate(NULL);
    }

    printf("\n\n=== RÉSULTAT ===\n");
    printf("processus debuggés: %d\n", dbg_ok);
    if (matches) {
        printf("INITIAL DATA RÉCUPÉRÉ — voir %s\n", g_out_name);
        printf("Transférer ce fichier sur le PC et lancer:\n");
        printf("  t2_titlekey_recover.py --initial-data initial_data.bin\n");
    } else {
        printf("AUCUNE correspondance.\n");
        printf("  → la cartouche est-elle INSÉRÉE?\n");
        printf("  → attendre ~10 s après l'insertion puis relancer.\n");
        printf("  → le config.txt correspond-il au header du XCI?\n");
    }
    printf("\nAppuyez sur + pour quitter.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return matches ? 0 : 2;
}
