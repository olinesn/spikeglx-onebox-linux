/* ftd3xx-wine-shim: shared between the Windows half (ftd3xx_pe.c, a PE DLL
 * that replaces FTDI's FTD3XX.dll under Wine) and the Linux half
 * (ftd3xx_unix.c, which calls FTDI's Linux libftd3xx.so).
 *
 * Both halves run in the same 64-bit process, so pointers pass through as is.
 * Plain C types only: the Linux half includes FTDI's WinTypes.h, which clashes
 * with Wine's and mingw's Windows headers. */
#ifndef FTD3XX_SHIM_H
#define FTD3XX_SHIM_H

#include <stdint.h>

enum shim_call {
    c_create,               /* p1 = pvArg, u1 = dwFlags, p2 = FT_HANDLE*            */
    c_close,                /* h                                                    */
    c_write_pipe,           /* h, u1 = pipe, p1 = buf, u2 = len, p2 = ULONG* done, p3 = OVERLAPPED* (Windows) */
    c_read_pipe,            /* same as write                                        */
    c_flush_pipe,           /* h, u1 = pipe                                         */
    c_create_list,          /* p1 = DWORD* count                                    */
    c_get_list,             /* p1 = FT_DEVICE_LIST_INFO_NODE*, p2 = DWORD* count    */
    c_driver_version,       /* h, p1 = DWORD*                                       */
    c_library_version,      /* p1 = DWORD*                                          */
    c_set_pipe_timeout,     /* h, u1 = pipe, u2 = ms                                */
    c_get_overlapped_result,/* h, p1 = OVERLAPPED* (Windows), p2 = ULONG*, u1 = bWait */
    c_init_overlapped,      /* h, p1 = OVERLAPPED* (Windows)                        */
    c_release_overlapped,   /* h, p1 = OVERLAPPED* (Windows)                        */
    c_set_stream_pipe,      /* h, u1 = allWrite, u2 = allRead, u3 = pipe, u4 = size */
    c_clear_stream_pipe,    /* h, u1 = allWrite, u2 = allRead, u3 = pipe            */
    c_abort_pipe,           /* h, u1 = pipe                                         */
    c_set_suspend_timeout,  /* h, u1 = timeout                                      */
    c_count
};

struct shim_args {
    uint32_t status;            /* the FT_STATUS the caller gets */
    uint32_t u1, u2, u3, u4;
    void *h;                    /* FT_HANDLE */
    void *p1, *p2, *p3;
};

/* FT_STATUS values the Windows half needs (same numbers on Windows and Linux) */
#define SHIM_FT_OK              0u
#define SHIM_FT_OTHER_ERROR     32u

#endif
