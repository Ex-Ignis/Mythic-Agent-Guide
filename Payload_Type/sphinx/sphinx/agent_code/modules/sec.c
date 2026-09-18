/**
Info seguridad
114 - 0x72
1. Estado de Defender (servicio WinDefend + registry)
2. Deteccion de EDRs por procesos conocidos
 */

#include <stdint.h>
#include "api_table.h"
#include "module_helpers.h"
#include "parser.h"

/* ── Helpers (static: no generan simbolo externo) ── */

/* Comparacion wide case-insensitive (solo dobla el caso ASCII). Sustituye a
   _wcsicmp (libc → UND prohibido en modulos COFF). */
static int wstr_ieq(const wchar_t* a, const wchar_t* b) {
    while (*a && *b) {
        wchar_t ca = (*a >= L'A' && *a <= L'Z') ? *a + 32 : *a;
        wchar_t cb = (*b >= L'A' && *b <= L'Z') ? *b + 32 : *b;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return (*a == L'\0' && *b == L'\0');
}

/* Estado del servicio a string. dwCurrentState es SERVICE_STATUS.*/
static const char* svc_state_str(DWORD state) {
    switch (state) {
        case SERVICE_STOPPED:          return "\tSTOPPED";
        case SERVICE_START_PENDING:    return "\tSTART_PENDING";
        case SERVICE_STOP_PENDING:     return "\tSTOP_PENDING";
        case SERVICE_RUNNING:          return "\tRUNNING";
        case SERVICE_CONTINUE_PENDING: return "\tCONTINUE_PENDING";
        case SERVICE_PAUSE_PENDING:    return "\tPAUSE_PENDING";
        case SERVICE_PAUSED:           return "\tPAUSED";
        default:                       return "\tUNKNOWN";
    }
}

/* Lee un DWORD del registro. Devuelve 1 si existe, 0 si no existe / error.
   api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", L"DisableBehaviorMonitoring", &v*/
static int reg_dword(const ApiTable* api, HKEY hive, const wchar_t* subkey, const wchar_t* value, DWORD* out_val) {
    HKEY hKey = NULL;
    if (api->RegOpenKeyExW(hive, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 0;
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    LSTATUS st = api->RegQueryValueExW(hKey, value, NULL, &type, (LPBYTE)out_val, &size);
    api->RegCloseKey(hKey);

    return (st == ERROR_SUCCESS && type == REG_DWORD);
}

/* Imprime "  [Defender] nombre = valor" o "= not set" */
static void print_reg_flag(const ApiTable* api, char* out, size_t* used, int found, DWORD val, const char* label) {
    char line[96];
    if (found)
        api->sprintf(line, "\t[Defender] %s = %lu (%s)\n", label, val,
                     val ? "DISABLED" : "ENABLED");
    else
        api->sprintf(line, "\t[Defender] %s = not set (default)\n", label);
    buf_append_str(out, used, line);
}

/* Enumera los valores de una subclave. Imprime nombre = value */
static int enum_exclusions(const ApiTable* api, char* out, size_t* used, HKEY hive, const wchar_t* subkey, const char* label) {
    HKEY hKey = NULL;
    if (api->RegOpenKeyExW(hive, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 0;

    int count = 0;
    for (DWORD idx = 0;; idx++) {
        WCHAR name_wide[520];
        DWORD name_chars = 520;
        DWORD type = 0;
        DWORD data = 0;
        DWORD data_len = sizeof(data);
        LSTATUS st = api->RegEnumValueW(hKey, idx, name_wide, &name_chars,
                                        NULL, &type, (LPBYTE)&data, &data_len);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS) continue;

        char name_utf8[520];
        api->WideCharToMultiByte(CP_UTF8, 0, name_wide, -1,
                                 name_utf8, sizeof(name_utf8), NULL, NULL);

        if (count == 0) {
            buf_append_str(out, used, label);
            buf_append_str(out, used, ": ");
        } else {
            buf_append_str(out, used, ", ");
        }
        buf_append_str(out, used, name_utf8);
        buf_append_str(out, used, "=");
        char dv[8];
        u64_to_dec((uint64_t)data, dv);
        buf_append_str(out, used, dv);
        count++;
    }
    if (count > 0)
        buf_append_str(out, used, "\n");

    api->RegCloseKey(hKey);
    return count;
}

void go(const ApiTable* api, const char* task_uuid, const Param* params, uint32_t param_count) {
    (void)params;
    (void)param_count;

    /* Buffer de salida */
    char* out = (char*)api->malloc(OUT_BUF_SIZE);
    if (!out) return;
    size_t used = 0;

    char err_str[12];

    /* ── Estado de Defender SCM ── */
    buf_append_str(out, &used, "[Defender Service]: \n");

    SC_HANDLE hSCM = api->OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        buf_append_str(out, &used, "\t[ERROR] OpenSCManagerW (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    } else {
        /* WinDefend: AV clasico. Sense: Windows Defender ATP (EDR). */
        static const wchar_t* svc_names[] = { L"WinDefend", L"Sense" };
        static const char* svc_labels[] = { "WinDefend", "Sense (EDR/ATP)" };
        for (int s = 0; s < 2; s++) {
            SC_HANDLE hSvc = api->OpenServiceW(hSCM, svc_names[s], SERVICE_QUERY_STATUS);
            buf_append_str(out, &used, "\t");
            buf_append_str(out, &used, svc_labels[s]);
            if (!hSvc) {
                buf_append_str(out, &used, "service not present\n");
            } else {
                SERVICE_STATUS_PROCESS ssp;
                DWORD needed = 0;
                if (!api->QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &needed)) {
                    buf_append_str(out, &used, "[ERROR] QueryServiceStatusEx\n");
                } else {
                    buf_append_str(out, &used, svc_state_str(ssp.dwCurrentState));
                    buf_append_str(out, &used, "\n");
                }
                api->CloseServiceHandle(hSvc);
            }
        }
        api->CloseServiceHandle(hSCM);
    }

    /* ── Estado de Defender: registry ── */
    buf_append_str(out, &used, "[Defender Registry]\n");

    DWORD v = 0;
    int found;

    /* Politica (GPO) - manda sobre las demas */
    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", L"DisableAntiSpyware", &v);
    print_reg_flag(api, out, &used, found, v, "Policy DisableAntiSpyware");

    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", L"DisableAntiVirus", &v);
    print_reg_flag(api, out, &used, found, v, "Policy DisableAntiVirus");

    /* Configuracion local */
    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender", L"DisableAntiSpyware", &v);
    print_reg_flag(api, out, &used, found, v, "DisableAntiSpyware");

    /* Real-Time Protection local */
    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", L"DisableRealtimeMonitoring", &v);
    print_reg_flag(api, out, &used, found, v, "DisableRealtimeMonitoring");

    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", L"DisableBehaviorMonitoring", &v);
    print_reg_flag(api, out, &used, found, v, "DisableBehaviorMonitoring");

    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", L"DisableOnAccessProtection", &v);
    print_reg_flag(api, out, &used, found, v, "DisableOnAccessProtection");

    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", L"DisableIOAVProtection", &v);
    print_reg_flag(api, out, &used, found, v, "DisableIOAVProtection");

    found = reg_dword(api, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection", L"DisableScriptScanning", &v);
    print_reg_flag(api, out, &used, found, v, "DisableScriptScanning");

    /* Exclusiones de Defender (GPO) */
    buf_append_str(out, &used, "[Defender Exclusions]\n");

    int ex_count = 0;
    ex_count += enum_exclusions(api, out, &used, HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Exclusions\\Paths", "\t[GPO Paths]");
    ex_count += enum_exclusions(api, out, &used, HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Exclusions\\Extensions", "\t[GPO Extensions]");
    ex_count += enum_exclusions(api, out, &used, HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Exclusions\\Processes", "\t[GPO Processes]");
    ex_count += enum_exclusions(api, out, &used, HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths", "\t[Local Paths]");
    ex_count += enum_exclusions(api, out, &used, HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Extensions", "\t[Local Extensions]");
    ex_count += enum_exclusions(api, out, &used, HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Processes", "\t[Local Processes]");

    if (ex_count == 0)
        buf_append_str(out, &used, "\tN/A\n");

    /* ── Deteccion de EDR/AV: procesos ── */
    buf_append_str(out, &used, "[EDR/AV Processes]\n");

    /* Blacklist de procesos EDR/AV conocidos */
    static const wchar_t* edr_procs[] = {
        L"msmpeng.exe",        /* Windows Defender */
        L"msseces.exe",        /* Defender / Security Essentials */
        L"csagent.exe",        /* CrowdStrike Falcon */
        L"crowdstrike.exe",
        L"sentinelagent.exe",  /* SentinelOne */
        L"sentinelstaticengine.exe",
        L"cylance.exe",        /* Cylance / BlackBerry */
        L"cb.exe",             /* Carbon Black */
        L"parity.exe",         /* Carbon Black */
        L"wrsa.exe",           /* WebRoot */
        L"sophosagent.exe",    /* Sophos */
        L"esensor.exe",        /* FireEye */
        L"xagt.exe",           /* Bitdefender */
        L"bdagent.exe",
        L"ccsvchst.exe",       /* Symantec / Norton */
        L"tprwcache.exe",
        L"mbamservice.exe",    /* Malwarebytes */
        L"ekrn.exe",           /* ESET */
        L"egui.exe",
        L"kavsvc.exe",         /* Kaspersky */
        L"avp.exe"
    };
    const int edr_count = (int)(sizeof(edr_procs) / sizeof(edr_procs[0]));

    HANDLE snap = api->CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        buf_append_str(out, &used, "\t[ERROR] CreateToolhelp32Snapshot (error ");
        u64_to_dec((uint64_t)api->GetLastError(), err_str);
        buf_append_str(out, &used, err_str);
        buf_append_str(out, &used, ")\n");
    } else {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);

        int detected = 0;
        if (api->Process32FirstW(snap, &pe)) {
            do {
                for (int i = 0; i < edr_count; i++) {
                    if (wstr_ieq(pe.szExeFile, edr_procs[i])) {
                        char line[128];
                        char pid[12];
                        u64_to_dec((uint64_t)pe.th32ProcessID, pid);
                        api->sprintf(line, "\t[!] %ls FOUND  pid %s\n",
                                     pe.szExeFile, pid);
                        buf_append_str(out, &used, line);
                        detected = 1;
                        break;
                    }
                }
            } while (api->Process32NextW(snap, &pe));
        }
        api->CloseHandle(snap);

        if (!detected)
            buf_append_str(out, &used, "\tNo EDR/AV processes detected\n");
    }

    /* Reportar al core */
    api->report_result(task_uuid, (const uint8_t*)out, used, 0x95);
    api->free(out);
}
