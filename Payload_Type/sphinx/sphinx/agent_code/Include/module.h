#ifndef SPHINX_MODULE_H
#define SPHINX_MODULE_H

#include <stdint.h>
#include "api_table.h"
#include "parser.h"
#include <windows.h>

#define MAX_MODULES      32
#define MODULE_NAME_LEN  32

/* ── Representa un módulo cargado ── */

typedef struct {
    uint8_t  command_id;
    char     name[MODULE_NAME_LEN];

    /* Puntero opaco: el módulo guarda aquí su estado privado */
    void*    context;

    /* Estado del modulo*/
    uint8_t  loaded;

    /* Entry point: llamado en cada invocación desde dispatch() */
    void (*run)(ApiTable* api, const char* task_uuid,
                Param* params, uint32_t param_count);

    /* Llamados al cargar/descargar */
    void (*init)(ApiTable* api);
    void (*cleanup)(ApiTable* api);

    /* Dirección base de la región VirtualAlloc (para liberar) */
    void*    code_base;
    size_t   code_size;

} Module;

/* ── Tabla global ── */

extern Module module_table[MAX_MODULES];
extern uint32_t module_count;

/* ── Operaciones sobre la tabla ── */

Module* find_module(uint8_t command_id);
int     module_register(_In_ const Module* mod);
int     module_unregister(_In_ uint8_t command_id);

#endif //SPHINX_MODULE_H
