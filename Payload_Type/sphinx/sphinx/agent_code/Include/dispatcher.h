#ifndef SPHINX_DISPATCHER_H
#define SPHINX_DISPATCHER_H

#include <stdint.h>
#include "parser.h"

#define STATUS_FAILED 0x99
#define STATUS_COMPLETE 0x95

//  DEPRECATED
// typedef struct {
//     uint8_t  status;       // 0x95 = complete, 0x99 = failed
//     uint8_t* output;
//     uint32_t output_len;
// } CmdResult;

//  DEPRECATED
// typedef struct {
//     char     task_uuid[37];
//     uint8_t*  data;
//     size_t   len;
//     uint8_t  status;
// } PendingResponse;

void handle_builtin(const Task* task);

void dispatch(_In_ Task* tasks, _In_ uint32_t task_count);

/* Exit flag: the exit built-in sets this to 1; main returns after flushing all responses. */
extern volatile int g_exit_flag;

#endif //SPHINX_DISPATCHER_H