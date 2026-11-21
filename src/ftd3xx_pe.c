/* ftd3xx-wine-shim, Windows half: a PE DLL named FTD3XX.dll that exports the
 * D3XX functions imec's NeuropixAPI uses (same export ordinals as FTDI's
 * FTD3XX.dll 1.3.0.10, see ftd3xx.def) and forwards each call to the Linux
 * half through Wine's unix-call interface.
 *
 * Built with mingw-w64, then marked as a Wine builtin with `winebuild --builtin`
 * so Wine's ntdll loads ftd3xx.so (the Linux half) beside it. */
#include <windows.h>
#include <stdint.h>
#include "ftd3xx_shim.h"

typedef PVOID FT_HANDLE;
typedef ULONG FT_STATUS;

/* Wine's unix-call interface (include/wine/unixlib.h, winternl.h of Wine 11),
 * written out here so this file builds with mingw's own headers. */
typedef UINT64 unixlib_handle_t;
typedef LONG (WINAPI *unix_dispatcher_t)(unixlib_handle_t, unsigned int, void *);
typedef LONG (WINAPI *nt_query_vm_t)(HANDLE, const void *, int, void *, SIZE_T, SIZE_T *);
#define MEMORY_WINE_UNIX_FUNCS 1000

static unixlib_handle_t unix_handle;
static unix_dispatcher_t unix_dispatcher;
static HMODULE self;

static BOOL init_unix_call(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    nt_query_vm_t query;
    unix_dispatcher_t *disp;

    if (!ntdll) return FALSE;
    query = (nt_query_vm_t)GetProcAddress(ntdll, "NtQueryVirtualMemory");
    disp = (unix_dispatcher_t *)GetProcAddress(ntdll, "__wine_unix_call_dispatcher");
    if (!query || !disp || !*disp) return FALSE;   /* not running under Wine */
    if (query(GetCurrentProcess(), self, MEMORY_WINE_UNIX_FUNCS,
              &unix_handle, sizeof(unix_handle), NULL))
        return FALSE;                                  /* ftd3xx.so not found or failed to load */
    unix_dispatcher = *disp;
    return TRUE;
}

static FT_STATUS call(enum shim_call c, struct shim_args *a)
{
    if (!unix_dispatcher || unix_dispatcher(unix_handle, c, a)) return SHIM_FT_OTHER_ERROR;
    return a->status;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = inst;
        DisableThreadLibraryCalls(inst);
        if (!init_unix_call()) {
            OutputDebugStringA("ftd3xx-wine-shim: no Linux half (ftd3xx.so); FTDI calls will fail\n");
        }
    }
    return TRUE;
}

#define ARGS struct shim_args a; ZeroMemory(&a, sizeof(a))

FT_STATUS WINAPI FT_Create(PVOID pvArg, DWORD dwFlags, FT_HANDLE *pftHandle)
{ ARGS; a.p1 = pvArg; a.u1 = dwFlags; a.p2 = pftHandle; return call(c_create, &a); }

FT_STATUS WINAPI FT_Close(FT_HANDLE h)
{ ARGS; a.h = h; return call(c_close, &a); }

FT_STATUS WINAPI FT_WritePipe(FT_HANDLE h, UCHAR pipe, PUCHAR buf, ULONG len, PULONG done, LPOVERLAPPED ov)
{ ARGS; a.h = h; a.u1 = pipe; a.p1 = buf; a.u2 = len; a.p2 = done; a.p3 = ov; return call(c_write_pipe, &a); }

FT_STATUS WINAPI FT_ReadPipe(FT_HANDLE h, UCHAR pipe, PUCHAR buf, ULONG len, PULONG done, LPOVERLAPPED ov)
{ ARGS; a.h = h; a.u1 = pipe; a.p1 = buf; a.u2 = len; a.p2 = done; a.p3 = ov; return call(c_read_pipe, &a); }

FT_STATUS WINAPI FT_FlushPipe(FT_HANDLE h, UCHAR pipe)
{ ARGS; a.h = h; a.u1 = pipe; return call(c_flush_pipe, &a); }

FT_STATUS WINAPI FT_CreateDeviceInfoList(LPDWORD count)
{ ARGS; a.p1 = count; return call(c_create_list, &a); }

FT_STATUS WINAPI FT_GetDeviceInfoList(PVOID list, LPDWORD count)
{ ARGS; a.p1 = list; a.p2 = count; return call(c_get_list, &a); }

FT_STATUS WINAPI FT_GetDriverVersion(FT_HANDLE h, LPDWORD version)
{ ARGS; a.h = h; a.p1 = version; return call(c_driver_version, &a); }

FT_STATUS WINAPI FT_GetLibraryVersion(LPDWORD version)
{ ARGS; a.p1 = version; return call(c_library_version, &a); }

FT_STATUS WINAPI FT_SetPipeTimeout(FT_HANDLE h, UCHAR pipe, ULONG ms)
{ ARGS; a.h = h; a.u1 = pipe; a.u2 = ms; return call(c_set_pipe_timeout, &a); }

FT_STATUS WINAPI FT_GetOverlappedResult(FT_HANDLE h, LPOVERLAPPED ov, PULONG done, BOOL wait)
{ ARGS; a.h = h; a.p1 = ov; a.p2 = done; a.u1 = (uint32_t)wait; return call(c_get_overlapped_result, &a); }

FT_STATUS WINAPI FT_InitializeOverlapped(FT_HANDLE h, LPOVERLAPPED ov)
{ ARGS; a.h = h; a.p1 = ov; return call(c_init_overlapped, &a); }

FT_STATUS WINAPI FT_ReleaseOverlapped(FT_HANDLE h, LPOVERLAPPED ov)
{ ARGS; a.h = h; a.p1 = ov; return call(c_release_overlapped, &a); }

FT_STATUS WINAPI FT_SetStreamPipe(FT_HANDLE h, BOOLEAN allWrite, BOOLEAN allRead, UCHAR pipe, ULONG size)
{ ARGS; a.h = h; a.u1 = allWrite; a.u2 = allRead; a.u3 = pipe; a.u4 = size; return call(c_set_stream_pipe, &a); }

FT_STATUS WINAPI FT_ClearStreamPipe(FT_HANDLE h, BOOLEAN allWrite, BOOLEAN allRead, UCHAR pipe)
{ ARGS; a.h = h; a.u1 = allWrite; a.u2 = allRead; a.u3 = pipe; return call(c_clear_stream_pipe, &a); }

FT_STATUS WINAPI FT_AbortPipe(FT_HANDLE h, UCHAR pipe)
{ ARGS; a.h = h; a.u1 = pipe; return call(c_abort_pipe, &a); }

FT_STATUS WINAPI FT_SetSuspendTimeout(FT_HANDLE h, ULONG timeout)
{ ARGS; a.h = h; a.u1 = timeout; return call(c_set_suspend_timeout, &a); }

/* The other 30 D3XX exports exist so a caller that looks them up by name gets
 * a clean error instead of a missing export. imec's DLL does not import them. */
#define NOT_SUPPORTED 17u
#define UNSUPPORTED(name) FT_STATUS WINAPI name(void) { return NOT_SUPPORTED; }
UNSUPPORTED(FT_ListDevices)            UNSUPPORTED(FT_GetDeviceInfo)
UNSUPPORTED(FT_GetDeviceInfoDetail)    UNSUPPORTED(FT_GetVIDPID)
UNSUPPORTED(FT_GetDeviceDescriptor)    UNSUPPORTED(FT_GetConfigurationDescriptor)
UNSUPPORTED(FT_GetInterfaceDescriptor) UNSUPPORTED(FT_GetPipeInformation)
UNSUPPORTED(FT_GetStringDescriptor)    UNSUPPORTED(FT_GetDescriptor)
UNSUPPORTED(FT_ControlTransfer)        UNSUPPORTED(FT_SetGPIO)
UNSUPPORTED(FT_GetGPIO)                UNSUPPORTED(FT_SetNotificationCallback)
UNSUPPORTED(FT_GetChipConfiguration)   UNSUPPORTED(FT_SetChipConfiguration)
UNSUPPORTED(FT_GetFirmwareVersion)     UNSUPPORTED(FT_ResetDevicePort)
UNSUPPORTED(FT_CycleDevicePort)        UNSUPPORTED(FT_IsDevicePath)
UNSUPPORTED(FT_GetPipeTimeout)         UNSUPPORTED(FT_GetSuspendTimeout)
UNSUPPORTED(FT_EnableGPIO)             UNSUPPORTED(FT_WriteGPIO)
UNSUPPORTED(FT_ReadGPIO)               UNSUPPORTED(FT_SetGPIOPull)
UNSUPPORTED(FT_SetGPIOLevel)           UNSUPPORTED(FT_WritePipeEx)
UNSUPPORTED(FT_ReadPipeEx)
void WINAPI FT_ClearNotificationCallback(void) { }
