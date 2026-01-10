// SPDX-License-Identifier: ISC
/**
 * @file
 * @brief Retevis RC2/RC4/MD2 composite cipher helpers.
 */
#ifndef RETEVIS_AP
#define RETEVIS_AP

#include <stddef.h>
#include <stdint.h>

#define n1 264

typedef struct {
    uint8_t array_rc4[256];
    int i_rc4, j_rc4;
    uint64_t x;
    uint8_t xyz, count;
    uint64_t bb;
} RC4State;

typedef struct {
    int x1, x2;
    unsigned char h2[n1];
    unsigned char h1[n1 * 3];
} MD2State;

typedef struct {
    uint16_t xkey[64];
    uint8_t plain[8];
    uint8_t cipher[8];
} RC2State;

typedef struct {
    RC4State rc4;
    MD2State md2;
    RC2State rc2;
    uint64_t internal_state;
    uint64_t internal_zero;
    uint8_t keys[16];
} CryptoContext;

/* Public API */
/** @brief Derive cipher keys from provided seed bytes. */
void create_keys_rc2(CryptoContext* ctx, unsigned char key1[], size_t size1);
/** @brief Encrypt a 49-bit frame using the composite cipher suite. */
void encryption_rc2(CryptoContext* ctx, uint8_t s1[49]);
/** @brief Decrypt a 49-bit frame using the composite cipher suite. */
void decrypt_rc2(CryptoContext* ctx, uint8_t bits[49]);

#endif
