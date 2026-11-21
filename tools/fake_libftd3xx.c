/* fake_libftd3xx.c: stand-in for FTDI's Linux libftd3xx.so with no device
 * attached, to test the Wine plumbing before FTDI's library is downloaded.
 * Every function returns what the real one would with nothing plugged in.
 * Build: gcc -shared -fPIC -Wl,-soname,libftd3xx.so -o libftd3xx.so fake_libftd3xx.c -I<ftdi header dir> */
#include "ftd3xx.h"

#define NO_DEVICE FT_DEVICE_NOT_FOUND

FT_STATUS FT_Create(PVOID a, DWORD f, FT_HANDLE *h) { (void)a; (void)f; if (h) *h = NULL; return NO_DEVICE; }
FT_STATUS FT_Close(FT_HANDLE h) { (void)h; return FT_INVALID_HANDLE; }
FT_STATUS FT_CreateDeviceInfoList(LPDWORD n) { if (n) *n = 0; return FT_OK; }
FT_STATUS FT_GetDeviceInfoList(FT_DEVICE_LIST_INFO_NODE *l, LPDWORD n) { (void)l; if (n) *n = 0; return FT_OK; }
FT_STATUS FT_GetDriverVersion(FT_HANDLE h, LPDWORD v) { (void)h; if (v) *v = 0x01010000; return FT_OK; }
FT_STATUS FT_GetLibraryVersion(LPDWORD v) { if (v) *v = 0x01010000; return FT_OK; }
FT_STATUS FT_SetPipeTimeout(FT_HANDLE h, UCHAR p, DWORD ms) { (void)h; (void)p; (void)ms; return FT_INVALID_HANDLE; }
FT_STATUS FT_WritePipe(FT_HANDLE h, UCHAR p, PUCHAR b, ULONG l, PULONG d, DWORD t) { (void)h;(void)p;(void)b;(void)l;(void)t; if (d) *d = 0; return FT_INVALID_HANDLE; }
FT_STATUS FT_ReadPipe(FT_HANDLE h, UCHAR p, PUCHAR b, ULONG l, PULONG d, DWORD t) { (void)h;(void)p;(void)b;(void)l;(void)t; if (d) *d = 0; return FT_INVALID_HANDLE; }
FT_STATUS FT_WritePipeAsync(FT_HANDLE h, UCHAR p, PUCHAR b, ULONG l, PULONG d, LPOVERLAPPED o) { (void)h;(void)p;(void)b;(void)l;(void)o; if (d) *d = 0; return FT_INVALID_HANDLE; }
FT_STATUS FT_ReadPipeAsync(FT_HANDLE h, UCHAR p, PUCHAR b, ULONG l, PULONG d, LPOVERLAPPED o) { (void)h;(void)p;(void)b;(void)l;(void)o; if (d) *d = 0; return FT_INVALID_HANDLE; }
FT_STATUS FT_GetOverlappedResult(FT_HANDLE h, LPOVERLAPPED o, PULONG d, BOOL w) { (void)h;(void)o;(void)w; if (d) *d = 0; return FT_INVALID_HANDLE; }
FT_STATUS FT_InitializeOverlapped(FT_HANDLE h, LPOVERLAPPED o) { (void)h; (void)o; return FT_INVALID_HANDLE; }
FT_STATUS FT_ReleaseOverlapped(FT_HANDLE h, LPOVERLAPPED o) { (void)h; (void)o; return FT_INVALID_HANDLE; }
FT_STATUS FT_SetStreamPipe(FT_HANDLE h, BOOL w, BOOL r, UCHAR p, ULONG s) { (void)h;(void)w;(void)r;(void)p;(void)s; return FT_INVALID_HANDLE; }
FT_STATUS FT_ClearStreamPipe(FT_HANDLE h, BOOL w, BOOL r, UCHAR p) { (void)h;(void)w;(void)r;(void)p; return FT_INVALID_HANDLE; }
FT_STATUS FT_FlushPipe(FT_HANDLE h, UCHAR p) { (void)h; (void)p; return FT_INVALID_HANDLE; }
FT_STATUS FT_AbortPipe(FT_HANDLE h, UCHAR p) { (void)h; (void)p; return FT_INVALID_HANDLE; }
