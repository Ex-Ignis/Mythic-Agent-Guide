#ifndef SPHINX_HMAC_SHA256_H
#define SPHINX_HMAC_SHA256_H

#include <stddef.h>

size_t hmac_sha256(const void* key, const size_t keylen,
                   const void* data, const size_t datalen,
                   void* out, const size_t outlen);

#endif //SPHINX_HMAC_SHA256_H
