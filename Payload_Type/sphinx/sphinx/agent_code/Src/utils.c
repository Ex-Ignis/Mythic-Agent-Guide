#include "../Include/utils.h"

#include <stdint.h>
#include <windows.h>
#include <ntsecapi.h>
#include <stdio.h>

static const char hex_chars[] = "0123456789abcdef";

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const uint8_t b64_reverse[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,
    ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
    ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
    ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
    ['y']=50,['z']=51,
    ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,
    ['8']=60,['9']=61,
    ['+']=62,['/']=63,
};

void base64_encode(const uint8_t* input_bytes, const size_t size, char* output_b64){
    size_t out_idx = 0;

    for (size_t i = 0; i < size; i += 3) {
        uint8_t b0 = input_bytes[i];
        uint8_t b1 = (i + 1 < size) ? input_bytes[i + 1] : 0;
        uint8_t b2 = (i + 2 < size) ? input_bytes[i + 2] : 0;

        uint32_t bits_24 = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | (uint32_t)b2;

        output_b64[out_idx++] = b64_table[(bits_24 >> 18) & 0x3F];
        output_b64[out_idx++] = b64_table[(bits_24 >> 12) & 0x3F];
        output_b64[out_idx++] = (i + 1 < size) ? b64_table[(bits_24 >> 6) & 0x3F] : '=';
        output_b64[out_idx++] = (i + 2 < size) ? b64_table[bits_24 & 0x3F] : '=';
    }

    output_b64[out_idx] = '\0';
}

size_t base64_decode(const char* input_b64, const size_t size, uint8_t* output_bytes) {
    size_t out_idx = 0;

    for (size_t i = 0; i < size; i += 4) {
        uint8_t val0 = b64_reverse[(uint8_t)input_b64[i]];
        uint8_t val1 = b64_reverse[(uint8_t)input_b64[i + 1]];

        output_bytes[out_idx++] = (val0 << 2) | (val1 >> 4);

        uint8_t val2;
        if (input_b64[i + 2] != '=') {
            val2 = b64_reverse[(uint8_t)input_b64[i + 2]];
            output_bytes[out_idx++] = (val1 << 4) | (val2 >> 2);
        }

        uint8_t val3;
        if (input_b64[i + 3] != '=') {
            val3 = b64_reverse[(uint8_t)input_b64[i + 3]];
            output_bytes[out_idx++] = (val2 << 6) | val3;
        }
    }

    return out_idx;
}

void generate_uuid(char* input_buf, size_t size) {
    if (size != 37) return;
    SystemFunction036(input_buf, 36);
    for (int i = 0; i < 36; i++) {
        input_buf[i] = hex_chars[(unsigned char)input_buf[i] % 16];
    }
    input_buf[14] = '4';
    input_buf[19] = 'a';
    input_buf[8] = '-'; input_buf[13] = '-'; input_buf[18] = '-'; input_buf[23] = '-';
    input_buf[36] = '\0';
}

uint32_t read_u32(const uint8_t* buffer, size_t* offset) {
    uint32_t val = ((uint32_t)buffer[*offset] << 24)
                     | ((uint32_t)buffer[*offset + 1] << 16)
                     | ((uint32_t)buffer[*offset + 2] << 8)
                     |  (uint32_t)buffer[*offset + 3];
    *offset += 4;
    return val;

}

uint64_t read_u64(const uint8_t* buffer, size_t* offset) {
    uint64_t val = ((uint64_t)buffer[*offset] << 56)
                        | ((uint64_t)buffer[*offset + 1] << 48)
                        | ((uint64_t)buffer[*offset + 2] << 40)
                        | ((uint64_t)buffer[*offset + 3] << 32)
                        | ((uint64_t)buffer[*offset + 4] << 24)
                        | ((uint64_t)buffer[*offset + 5] << 16)
                        | ((uint64_t)buffer[*offset + 6] << 8)
                        |  (uint64_t)buffer[*offset + 7];
    *offset += 8;
    return val;

}