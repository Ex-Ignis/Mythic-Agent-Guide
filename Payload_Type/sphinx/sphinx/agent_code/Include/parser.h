#ifndef SPHINX_PARSER_H
#define SPHINX_PARSER_H

#include <stdint.h>
#include "protocol.h"

#define MAX_PARAMS 16
#define MAX_TASKS 32

typedef struct {
    uint32_t size;
    uint8_t* data;    // puntero dentro del buffer original, no malloc
} Param;

typedef struct {
    uint8_t  command_id;
    char     task_uuid[37];
    uint32_t param_count;
    Param    params[MAX_PARAMS];
} Task;

uint8_t read_byte(const uint8_t* buf, size_t* offset);
void read_bytes(const uint8_t* buf, size_t* offset, uint8_t* data, size_t size);
void read_string(const uint8_t* buf, size_t* offset, char* data, size_t size);
int parse_checkin_response(const uint8_t* buf, size_t len, char* uuid_out);
int parse_tasks(const uint8_t* buf, size_t len, Task* tasks, uint32_t* task_count);

#endif //SPHINX_PARSER_H