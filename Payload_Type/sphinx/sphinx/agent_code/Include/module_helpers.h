#ifndef SPHINX_MODULE_HELPERS_H
#define SPHINX_MODULE_HELPERS_H
#include <stddef.h>
#include <stdint.h>

#define OUT_BUF_SIZE 65536
#define PATTERN_LEN  1024

/* Longitud de un string wide (wcslen manual) */
static inline size_t wlen(const wchar_t* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Longitud de un string ASCII (strlen manual, solo para literales nuestros) */
static inline size_t slen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Concatenar wide */
static inline void wcat(wchar_t* dst, const wchar_t* src) {
    size_t i = wlen(dst);
    size_t j = 0;
    while (src[j]) dst[i++] = src[j++];
    dst[i] = L'\0';
}

/* Append al buffer de salida. Devuelve 1 si cabia, 0 si lleno. */
static int buf_append(char* buf, size_t* used, const char* data, size_t len) {
    if (*used + len >= OUT_BUF_SIZE) return 0;
    for (size_t i = 0; i < len; i++)
        buf[(*used)++] = data[i];
    return 1;
}

/* Append de un string ASCII literal */
static int buf_append_str(char* buf, size_t* used, const char* s) {
    return buf_append(buf, used, s, slen(s));
}

/* uint64 → decimal, null-terminated. Devuelve longitud. */
static inline size_t u64_to_dec(uint64_t n, char* out) {
    char tmp[24];
    size_t i = 0;

    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }

    while (n > 0) {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    for (size_t j = 0; j < i; j++)
        out[j] = tmp[i - 1 - j];
    out[i] = '\0';
    return i;
}

/* Zero manual de un buffer wide (sustituye a memset: no genera simbolo externo) */
static inline void wzero(wchar_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++)
        buf[i] = L'\0';
}

/* SOCKADDR* → "a.b.c.d" (IPv4) o "xxxx:xxxx:..." (IPv6, sin compresion ::) */
static void sockaddr_to_str(ApiTable* api, LPSOCKADDR sa, char* out, size_t cap) {
    if (sa->sa_family == AF_INET) {
        SOCKADDR_IN* s4 = (SOCKADDR_IN*)sa;
        /* S_addr esta en network byte order en memoria(big endian) */
        unsigned char* b = (unsigned char*)&s4->sin_addr.S_un.S_addr;
        api->sprintf(out, "%u.%u.%u.%u", (unsigned)b[0], (unsigned)b[1],(unsigned)b[2], (unsigned)b[3]);
    } else if (sa->sa_family == AF_INET6) {
        SOCKADDR_IN6* s6 = (SOCKADDR_IN6*)sa;
        unsigned char* b = s6->sin6_addr.u.Byte;
        int off = 0;
        for (int i = 0; i < 16; i += 2) {
            off += api->sprintf(out + off, "%s%02x%02x",(i == 0) ? "" : ":", b[i], b[i + 1]);
        }
    } else {
        api->sprintf(out, "Unknown");
    }
}

/* DWORD (network byte order) → "a.b.c.d". dwLocalAddr/dwRemoteAddr vienen asi
   en MIB_TCPROW_OWNER_PID/MIB_UDPROW_OWNER_PID. */
static void ip4_from_dword(ApiTable* api, DWORD addr, char* out) {
    unsigned char* b = (unsigned char*)&addr;
    api->sprintf(out, "%u.%u.%u.%u", (unsigned)b[0], (unsigned)b[1],
                 (unsigned)b[2], (unsigned)b[3]);
}

/* Puerto en network byte order → decimal. dwLocalPort/dwRemotePort guardan el
   puerto en los 16 bits bajos pero en orden de red (big-endian) → invertir. */
static void port_from_dword(ApiTable* api, DWORD port, char* out) {
    unsigned int p = ((port >> 8) & 0xFF) | ((port & 0xFF) << 8);
    api->sprintf(out, "%u", p);
}

#endif //SPHINX_MODULE_HELPERS_H