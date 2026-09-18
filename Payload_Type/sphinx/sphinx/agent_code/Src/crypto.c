/*
 * crypto.c — formato Mythic (mythic_encrypts = True)
 *
 * Paquete cifrado (sin UUID, que gestiona comms.c):
 *
 *   [ IV: 16 bytes random ][ AES256CBC( PKCS7(plain) ) ][ HMAC-SHA256 ]
 *                                                   (32 bytes, key=32B, msg=IV||CT)
 *
 * Verificado contra Xenon/Crypto.c (referencia funcional de Mythic).
 */

#include "crypto.h"
#include "aes.h"
#include "hmac_sha256.h"
#include <windows.h>
#include <ntsecapi.h>
#include <string.h>

#define AES_BLOCK  16
#define HMAC_SIZE  32
#define KEY_SIZE   32

size_t crypto_encrypt(const uint8_t* key, const uint8_t* plain, size_t plain_len, uint8_t* out) {
    /* 1. IV aleatorio */
    uint8_t iv[AES_BLOCK];
    SystemFunction036(iv, AES_BLOCK);

    /* 2. PKCS7: SIEMPRE al menos 1 byte de padding (bloque completo si es multiplo) */
    size_t pad        = AES_BLOCK - (plain_len % AES_BLOCK);
    size_t padded_len = plain_len + pad;
    size_t total      = AES_BLOCK + padded_len + HMAC_SIZE; /* IV || CT || HMAC */

    /* 3. Copiar plaintext + padding tras el hueco del IV */
    memcpy(out + AES_BLOCK, plain, plain_len);
    memset(out + AES_BLOCK + plain_len, (int)pad, pad);

    /* 4. AES-256-CBC */
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_encrypt_buffer(&ctx, out + AES_BLOCK, padded_len);

    /* 5. IV delante del ciphertext */
    memcpy(out, iv, AES_BLOCK);

    /* 6. HMAC-SHA256(key_32, IV || CT) al final */
    hmac_sha256(key, KEY_SIZE, out, AES_BLOCK + padded_len,
                out + AES_BLOCK + padded_len, HMAC_SIZE);

    return total;
}

int crypto_decrypt(const uint8_t* key, uint8_t* blob, size_t blob_len) {
    if (blob_len < AES_BLOCK + AES_BLOCK + HMAC_SIZE) return -1;

    size_t ct_len = blob_len - AES_BLOCK - HMAC_SIZE;
    if (ct_len % AES_BLOCK != 0) return -1;

    /* 1. Verificar HMAC sobre IV || CT */
    uint8_t hmac_calc[HMAC_SIZE];
    hmac_sha256(key, KEY_SIZE, blob, AES_BLOCK + ct_len, hmac_calc, HMAC_SIZE);
    if (memcmp(hmac_calc, blob + AES_BLOCK + ct_len, HMAC_SIZE) != 0) return -1;

    /* 2. IV = primeros 16 bytes */
    uint8_t iv[AES_BLOCK];
    memcpy(iv, blob, AES_BLOCK);

    /* 3. Descifrar CT in-place */
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, blob + AES_BLOCK, ct_len);

    /* 4. Unpad PKCS7 con validacion */
    uint8_t pad = blob[AES_BLOCK + ct_len - 1];
    if (pad == 0 || pad > AES_BLOCK) return -1;
    for (uint8_t i = 1; i <= pad; i++) {
        if (blob[AES_BLOCK + ct_len - i] != pad) return -1;
    }
    size_t plain_len = ct_len - pad;

    /* 5. Plaintext al inicio del buffer */
    memmove(blob, blob + AES_BLOCK, plain_len);
    return (int)plain_len;
}
