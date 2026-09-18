
#ifndef SPHINX_UTILS_H
#define SPHINX_UTILS_H
#include <stddef.h>
#include <stdint.h>

#define BASE64_ENCODE_LEN(n) (((n) + 2) / 3 * 4)
#define BASE64_DECODE_LEN(n) ((n) / 4 * 3)
#define SWAP32(x) (((x) >> 24) | ((x) >> 8 & 0xFF00) | ((x) << 8 & 0xFF0000) | ((x) << 24))

void base64_encode(const uint8_t* input_bytes, const size_t size, char* output_b64);
size_t base64_decode(const char* input_b64, const size_t size, uint8_t* output_bytes);

void generate_uuid(char* input_buf, size_t size);

uint32_t read_u32(const uint8_t* buffer, size_t* offset);
uint64_t read_u64(const uint8_t* buffer, size_t* offset);
#endif //SPHINX_UTILS_H