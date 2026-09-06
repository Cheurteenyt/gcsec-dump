# gcsec-dump — GameCardInitialData recovery (zero eShop, zero Lockpick)

Homebrew Switch qui retrouve le **GameCardInitialData** (0x200 octets) de la
cartouche insérée en scannant la mémoire du processus FS, puis l'écrit sur
la SD. Ce bloc contient la titlekey chiffrée dont la clé de décryptage CCM
est **publique** (`package_id || zeros`) — la récupération se fait ensuite
100% sur PC avec `t2_titlekey_recover.py`.

## Pourquoi ça marche (discipline réponse-connue)

Le header XCI du jeu porte:
- `initial_data_hash` = SHA-256 du bloc InitialData (0x200 B) — `@0x160`
- `package_id` — `@0x110`

Le bloc InitialData contient `key_source = package_id || zeros` (16 B
connus) + 44 octets inconnus (titlekey chiffrée + MAC + nonce) + 452 zéros.
Le scan mémoire cherche la signature `[package_id || zeros]` et valide
chaque candidat par `SHA-256(fenêtre) == initial_data_hash` — **zéro faux
positif possible** (double contrainte publique). Fonctionne pour T1 ET T2
(pas de branche par génération: le hash tranche).

## Usage

1. `config.txt` à la racine SD dans `sdmc:/gcsec-dump/`:
   ```
   <initial_data_hash — 64 hex, header XCI @0x160>
   <package_id — 16 hex, header XCI @0x110>
   ```
   (générer depuis le XCI: `python3 tools/extract/t2_nca_parse.py --xci jeu.xci --offset 0`
   ou extraire directement: `dd if=jeu.xci bs=1 skip=352 count=16 | xxd -p`)
2. Insérer la cartouche, attendre ~10 s sur le HOME.
3. Lancer gcsec-dump (hbmenu).
4. Récupérer `sdmc:/gcsec-dump/initial_data.bin` → PC.
5. `t2_titlekey_recover.py --initial-data initial_data.bin --xci jeu.xci`
   → titlekey (MAC CCM auto-validé) → `ingest --titlekey-dec` → usine.

## Build

devkitPro (devkitA64 + libnx + mbedtls), puis `make`. Le .nro sort du
build. CI GitHub Actions: workflow fourni (`build-gcsec-dump.yml`) utilisant
`devkitPro/install-toolchain-action` — le runner GH n'est pas bloqué par
Cloudflare, contrairement au réseau local.

## Fichiers produits sur la SD

- `initial_data.bin` (0x200) — le bloc authentifié (hash vérifié)
- `context.bin` — ±0x1000 octets autour du match (forensique: security
  information, card ids, certificat si présents en mémoire)
