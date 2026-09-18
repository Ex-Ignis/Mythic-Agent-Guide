#ifndef SPHINX_SHA256_H
#define SPHINX_SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_HASH_SIZE 32

typedef struct {
    uint8_t bytes[SHA256_HASH_SIZE];
} SHA256_HASH;

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint32_t curlen;
    uint8_t  buf[64];
} Sha256Context;

void Sha256Initialise(Sha256Context* Context);
void Sha256Update(Sha256Context* Context, void const* Buffer, uint32_t BufferSize);
void Sha256Finalise(Sha256Context* Context, SHA256_HASH* Digest);
void Sha256Calculate(void const* Buffer, uint32_t BufferSize, SHA256_HASH* Digest);

#endif //SPHINX_SHA256_H
