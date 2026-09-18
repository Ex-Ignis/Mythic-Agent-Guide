#ifndef SPHINX_CRYPTO_H
#define SPHINX_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/* Formato Mythic (mythic_encrypts=True), verificado contra Xenon/Crypto.c:
 *   encrypt:  IV(16) || AES256CBC(PKCS7(plain)) || HMAC-SHA256(key_32, IV||CT)
 *   el UUID (36 chars) y el base64 los gestiona comms.c, NO esta capa.
 */

/* Cifra plaintext. out debe tener capacidad plain_len + 64 (IV+pad max+HMAC).
 * Devuelve la longitud total escrita o 0 si fallo. */
size_t crypto_encrypt(const uint8_t* key, const uint8_t* plain, size_t plain_len, uint8_t* out);

/* Descifra in-place. blob = IV||CT||HMAC (sin UUID, sin base64).
 * Devuelve longitud del plaintext (movido al inicio de blob) o -1 si fallo. */
int crypto_decrypt(const uint8_t* key, uint8_t* blob, size_t blob_len);

#endif //SPHINX_CRYPTO_H
