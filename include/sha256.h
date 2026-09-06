// SHA-256 — implémentation compacte (FIPS 180-4), domaine public.
// Validée par réponses connues: "abc" -> ba7816bf8f01cfea414140de5dae2223
// et hash d'un bloc réel contre sha256 (reference PC).
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

void sha256_calc(const void *data, size_t len, uint8_t out[32]);

#endif
