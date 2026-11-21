/* ftd3xx-wine-shim, Linux half: ftd3xx.so, loaded by Wine's ntdll beside the
 * builtin FTD3XX.dll (ftd3xx_pe.c). Each entry of __wine_unix_call_funcs runs
 * one D3XX call through FTDI's Linux libftd3xx.so.
 *
 * Differences from Windows D3XX that this file bridges:
 * - Windows FT_ReadPipe/FT_WritePipe take an OVERLAPPED*; Linux takes a timeout
 *   for blocking calls and has FT_ReadPipeAsync/FT_WritePipeAsync, which take a
 *   FIFO number (0-3) instead of a pipe ID (0x02-0x05 out, 0x82-0x85 in).
 * - Linux OVERLAPPED is a different struct (24 vs 32 bytes), so every Windows
 *   OVERLAPPED gets its own Linux one, found through a table.
 * - Linux has no FT_SetSuspendTimeout (USB selective suspend): accepted, no-op.
 * - FT_SetPipeTimeout(…, 0): Windows waits forever, Linux returns at once from
 *   the library buffer. 0 is passed on as 0xFFFFFFFF ms.
 * - Status codes are Windows' numbers except 11/12: from libftd3xx 1.1.0 they
 *   mean "memory insufficient"/"overflow" (Windows: EEPROM errors), so they
 *   go back as FT_INSUFFICIENT_RESOURCES (5) / FT_IO_ERROR (4).
 * - libftd3xx 1.1.5+ (tested: 1.1.8): a blocking FT_ReadPipe that gets a short
 *   packet waits for the full length and returns FT_TIMEOUT; Windows returns the
 *   short packet at once, which imec's register reads rely on. With 1.1.5+ the
 *   shim serves blocking reads with FT_ReadPipeAsync + FT_GetOverlappedResult,
 *   which return short packets at once, and sets fStopReadingOnURBUnderrun.
 * Use libftd3xx 1.1.8 or newer: 1.1.0 leaks ~260 B per asynchronous read and
 * after 5-18 min returns -1 from FT_GetOverlappedResult, which stops the stream.
 *
 * Environment:
 *   FTD3XX_SHIM_LOG=stderr|<path>   log every call (the busy read/write path:
 *                                   the first 50, then each failure)
 *   FTD3XX_SHIM_DRIVER_VERSION=0x.. value FT_GetDriverVersion reports
 *   FTD3XX_SHIM_LIBRARY_VERSION=0x.. value FT_GetLibraryVersion reports
 *   FTD3XX_SHIM_STATS=<seconds>     (with FTD3XX_SHIM_LOG) a line of transfer
 *                                   counters, heap and RSS every <seconds>
 *   FTD3XX_SHIM_SYNC_VIA_ASYNC=0|1  blocking reads via asynchronous reads
 *                                   (default: 1 with libftd3xx 1.1.5+)
 *   FTD3XX_SHIM_STOP_ON_UNDERRUN=0|1 fStopReadingOnURBUnderrun (same default)
 */
#include <stdint.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <malloc.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>

#include "ftd3xx.h"          /* FTDI's Linux header (with its Windows-style types) */
#include "ftd3xx_shim.h"

typedef int32_t shim_ntstatus;
typedef shim_ntstatus (*unixlib_entry_t)(void *args);

/* ---------------------------------------------------------------- logging */

static FILE *log_file;
static int log_state;   /* 0 = not checked, 1 = on, 2 = off */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint busy_calls;

static int log_on(void)
{
    if (log_state == 0) {
        const char *v = getenv("FTD3XX_SHIM_LOG");
        pthread_mutex_lock(&log_lock);
        if (log_state == 0) {
            if (v && *v) {
                log_file = strcmp(v, "stderr") ? fopen(v, "a") : stderr;
                log_state = log_file ? 1 : 2;
            } else log_state = 2;
        }
        pthread_mutex_unlock(&log_lock);
    }
    return log_state == 1;
}

#define LOG(...) do { if (log_on()) { pthread_mutex_lock(&log_lock); \
    fprintf(log_file, "ftd3xx-shim: " __VA_ARGS__); fputc('\n', log_file); fflush(log_file); \
    pthread_mutex_unlock(&log_lock); } } while (0)

/* read/write/overlapped-result run thousands of times a second */
#define LOG_BUSY(st, ...) do { unsigned n_ = atomic_fetch_add(&busy_calls, 1); \
    if (n_ < 50 || ((st) != FT_OK && (st) != FT_IO_PENDING)) LOG(__VA_ARGS__); } while (0)

/* ------------------------------------------------------------ statistics */

/* FTD3XX_SHIM_STATS=<seconds> (with FTD3XX_SHIM_LOG set): every <seconds>, one log line with
 * the transfer counters, glibc's heap in use (libftd3xx, libusb and this file allocate there;
 * Wine's Windows heaps, used by imec's API and SpikeGLX, don't) and the process RSS. A leak in
 * libftd3xx shows in heap_used; one on the Windows side only in rss. */
static atomic_ulong st_async_rd, st_async_wr, st_sync_rd, st_sync_wr;
static atomic_ulong st_results, st_result_bytes, st_result_fail, st_ov_init, st_ov_release, st_ticks;
static double st_every = -1, st_t0, st_last;
static pthread_mutex_t st_lock = PTHREAD_MUTEX_INITIALIZER;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static long rss_kb(void)
{
    long size = 0, rss = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) { if (fscanf(f, "%ld %ld", &size, &rss) != 2) rss = 0; fclose(f); }
    return rss * (sysconf(_SC_PAGESIZE) / 1024);
}

static void stats_tick(void)
{
    if (st_every == 0) return;
    if (atomic_fetch_add(&st_ticks, 1) & 255) return;          /* look at the clock every 256 calls */
    pthread_mutex_lock(&st_lock);
    if (st_every < 0) {
        const char *v = getenv("FTD3XX_SHIM_STATS");
        st_every = (v && *v && log_on()) ? atof(v) : 0;
        if (st_every < 0) st_every = 0;
        st_t0 = st_last = now_s();
    }
    double t = now_s();
    if (st_every > 0 && t - st_last >= st_every) {
        st_last = t;
        struct mallinfo2 m = mallinfo2();
        LOG("stats t=%.1f async_rd=%lu async_wr=%lu sync_rd=%lu sync_wr=%lu results=%lu result_MB=%.1f "
            "result_fail=%lu ov_init=%lu ov_release=%lu heap_used_kB=%zu heap_mmap_kB=%zu rss_kB=%ld",
            t - st_t0, (unsigned long)st_async_rd, (unsigned long)st_async_wr, (unsigned long)st_sync_rd,
            (unsigned long)st_sync_wr, (unsigned long)st_results, st_result_bytes / 1e6,
            (unsigned long)st_result_fail, (unsigned long)st_ov_init, (unsigned long)st_ov_release,
            m.uordblks / 1024, m.hblkhd / 1024, rss_kb());
    }
    pthread_mutex_unlock(&st_lock);
}

/* ------------------------------------------------------ OVERLAPPED table */

#define MAX_OV 512
struct ov_entry { void *win; void *h; OVERLAPPED *lin; };
static struct ov_entry ovs[MAX_OV];
static pthread_mutex_t ov_lock = PTHREAD_MUTEX_INITIALIZER;

static OVERLAPPED *ov_find(void *win)
{
    OVERLAPPED *lin = NULL;
    pthread_mutex_lock(&ov_lock);
    for (int i = 0; i < MAX_OV; i++) if (ovs[i].win == win) { lin = ovs[i].lin; break; }
    pthread_mutex_unlock(&ov_lock);
    return lin;
}

static int ov_add(void *win, void *h, OVERLAPPED *lin)
{
    int ok = 0;
    pthread_mutex_lock(&ov_lock);
    for (int i = 0; i < MAX_OV; i++) if (!ovs[i].win) { ovs[i] = (struct ov_entry){ win, h, lin }; ok = 1; break; }
    pthread_mutex_unlock(&ov_lock);
    return ok;
}

static OVERLAPPED *ov_remove(void *win)
{
    OVERLAPPED *lin = NULL;
    pthread_mutex_lock(&ov_lock);
    for (int i = 0; i < MAX_OV; i++) if (ovs[i].win == win) { lin = ovs[i].lin; ovs[i] = (struct ov_entry){0}; break; }
    pthread_mutex_unlock(&ov_lock);
    return lin;
}

/* ------------------------------------------------- per-pipe timeouts */

/* Windows D3XX: 5 s by default; FT_SetPipeTimeout(…, 0) means wait forever. */
#define DEFAULT_TIMEOUT_MS 5000u
#define MAX_TO 64
struct to_entry { void *h; unsigned pipe; unsigned ms; };
static struct to_entry tos[MAX_TO];
static pthread_mutex_t to_lock = PTHREAD_MUTEX_INITIALIZER;

static void timeout_set(void *h, unsigned pipe, unsigned ms)
{
    pthread_mutex_lock(&to_lock);
    int free_slot = -1, i;
    for (i = 0; i < MAX_TO; i++) {
        if (tos[i].h == h && tos[i].pipe == pipe) break;
        if (!tos[i].h && free_slot < 0) free_slot = i;
    }
    if (i == MAX_TO) i = free_slot;
    if (i >= 0) tos[i] = (struct to_entry){ h, pipe, ms };
    pthread_mutex_unlock(&to_lock);
}

static unsigned timeout_get(void *h, unsigned pipe)
{
    unsigned ms = DEFAULT_TIMEOUT_MS;
    pthread_mutex_lock(&to_lock);
    for (int i = 0; i < MAX_TO; i++) if (tos[i].h == h && tos[i].pipe == pipe) { ms = tos[i].ms; break; }
    pthread_mutex_unlock(&to_lock);
    return ms ? ms : 0xFFFFFFFFu;
}

static void timeouts_forget(void *h)
{
    pthread_mutex_lock(&to_lock);
    for (int i = 0; i < MAX_TO; i++) if (tos[i].h == h) tos[i] = (struct to_entry){0};
    pthread_mutex_unlock(&to_lock);
}

/* pipe 0x02-0x05 (out) or 0x82-0x85 (in) -> FIFO 0-3 */
static UCHAR fifo_of(unsigned pipe) { return (UCHAR)((pipe & 0x7f) - 2); }

static unsigned env_hex(const char *name, int *set)
{
    const char *v = getenv(name);
    *set = v && *v;
    return *set ? (unsigned)strtoul(v, NULL, 0) : 0;
}

/* ------------------------------------------------- transfer parameters */

/* libftd3xx 1.1.5+ (seen with 1.1.8): a blocking FT_ReadPipe of 2,048 B that gets a 48 B short
 * packet waits for the rest and returns FT_TIMEOUT; Windows (and libftd3xx 1.1.0) return the 48 B
 * at once, which imec's register reads rely on. So with 1.1.5+ blocking reads go through an
 * asynchronous read, which returns short packets at once (sync_via_async), and
 * fStopReadingOnURBUnderrun is set for FIFO 0-3. Set once, before the first FT_Create (it is
 * refused while a device is open); zeros elsewhere keep the library's defaults. 1.1.5+ is
 * recognised by FT_GetTransferParams; FTD3XX_SHIM_SYNC_VIA_ASYNC / _STOP_ON_UNDERRUN=0|1 override. */
static pthread_once_t params_once = PTHREAD_ONCE_INIT;
static int sync_via_async;

static void set_transfer_params(void)
{
    typedef FT_STATUS (*get_params_t)(FT_TRANSFER_CONF *, DWORD);
    /* libftd3xx is loaded privately under Wine: ask it, not the global scope */
    void *lib = dlopen("libftd3xx.so", RTLD_LAZY | RTLD_NOLOAD);
    get_params_t get = (get_params_t)dlsym(lib ? lib : RTLD_DEFAULT, "FT_GetTransferParams");
    const char *sv = getenv("FTD3XX_SHIM_SYNC_VIA_ASYNC");
    sync_via_async = sv && *sv ? atoi(sv) != 0 : get != NULL;
    LOG("synchronous reads: %s", sync_via_async ? "asynchronous read + FT_GetOverlappedResult" : "FT_ReadPipe");
    const char *v = getenv("FTD3XX_SHIM_STOP_ON_UNDERRUN");
    int on = v && *v ? atoi(v) != 0 : get != NULL;
    if (!on) LOG("transfer parameters: library defaults (stop on URB underrun off)");
    else for (DWORD fifo = 0; fifo < 4; fifo++) {
        FT_TRANSFER_CONF c;
        memset(&c, 0, sizeof(c));
        c.wStructSize = sizeof(c);
        if (get) get(&c, fifo);               /* start from what is in effect */
        c.wStructSize = sizeof(c);
        c.fStopReadingOnURBUnderrun = TRUE;
        FT_STATUS st = FT_SetTransferParams(&c, fifo);
        LOG("FT_SetTransferParams(FIFO %u, stop on URB underrun) = %u", fifo, st);
    }
    if (lib) dlclose(lib);
}

/* ------------------------------------------------------------- the calls */

static shim_ntstatus u_create(void *p)
{
    struct shim_args *a = p;
    pthread_once(&params_once, set_transfer_params);
    a->status = FT_Create(a->p1, a->u1, (FT_HANDLE *)a->p2);
    if (a->u1 & FT_OPEN_BY_INDEX)
        LOG("FT_Create(index %lu, flags 0x%x) = %u, handle %p", (unsigned long)(uintptr_t)a->p1, a->u1, a->status,
            a->p2 ? *(void **)a->p2 : NULL);
    else
        LOG("FT_Create(\"%.32s\", flags 0x%x) = %u, handle %p", a->p1 ? (const char *)a->p1 : "", a->u1, a->status,
            a->p2 ? *(void **)a->p2 : NULL);
    return 0;
}

static shim_ntstatus u_close(void *p)
{
    struct shim_args *a = p;
    a->status = FT_Close(a->h);
    timeouts_forget(a->h);
    LOG("FT_Close(%p) = %u", a->h, a->status);
    return 0;
}

static shim_ntstatus rw(struct shim_args *a, int write)
{
    ULONG *done = a->p2;
    if (!a->p3) {
        unsigned ms = timeout_get(a->h, a->u1);
        if (!write && sync_via_async) {           /* 1.1.5+: a short packet must end the read */
            OVERLAPPED ov;
            ULONG n = 0;
            memset(&ov, 0, sizeof(ov));
            FT_STATUS st = FT_InitializeOverlapped(a->h, &ov);
            if (st == FT_OK) {
                FT_SetPipeTimeout(a->h, (UCHAR)a->u1, ms == 0xFFFFFFFFu ? 0 : ms);   /* 1.1.7+: 0 = no timeout */
                st = FT_ReadPipeAsync(a->h, fifo_of(a->u1), a->p1, a->u2, &n, &ov);
                if (st == FT_IO_PENDING || st == FT_OK) st = FT_GetOverlappedResult(a->h, &ov, &n, TRUE);
                FT_ReleaseOverlapped(a->h, &ov);
            }
            a->status = st;
            if (done) *done = n;
        } else
        a->status = write ? FT_WritePipe(a->h, (UCHAR)a->u1, a->p1, a->u2, done, ms)
                          : FT_ReadPipe(a->h, (UCHAR)a->u1, a->p1, a->u2, done, ms);
        atomic_fetch_add(write ? &st_sync_wr : &st_sync_rd, 1);
        stats_tick();
        LOG_BUSY(a->status, "FT_%sPipe(%p, 0x%02x, %u bytes, sync %u ms) = %u, done %u", write ? "Write" : "Read",
                 a->h, a->u1, a->u2, ms, a->status, done ? *done : 0);
        return 0;
    }
    OVERLAPPED *lin = ov_find(a->p3);
    if (!lin) {
        a->status = FT_INVALID_PARAMETER;
        LOG("FT_%sPipe(%p, 0x%02x): OVERLAPPED %p was never initialized", write ? "Write" : "Read", a->h, a->u1, a->p3);
        return 0;
    }
    a->status = write ? FT_WritePipeAsync(a->h, fifo_of(a->u1), a->p1, a->u2, done, lin)
                      : FT_ReadPipeAsync(a->h, fifo_of(a->u1), a->p1, a->u2, done, lin);
    atomic_fetch_add(write ? &st_async_wr : &st_async_rd, 1);
    stats_tick();
    LOG_BUSY(a->status, "FT_%sPipe(%p, 0x%02x -> FIFO %u, %u bytes, async ov %p) = %u", write ? "Write" : "Read",
             a->h, a->u1, fifo_of(a->u1), a->u2, a->p3, a->status);
    return 0;
}

static shim_ntstatus u_write_pipe(void *p) { return rw(p, 1); }
static shim_ntstatus u_read_pipe(void *p)  { return rw(p, 0); }

static shim_ntstatus u_flush_pipe(void *p)
{
    struct shim_args *a = p;
    a->status = FT_FlushPipe(a->h, (UCHAR)a->u1);
    LOG("FT_FlushPipe(%p, 0x%02x) = %u", a->h, a->u1, a->status);
    return 0;
}

static shim_ntstatus u_create_list(void *p)
{
    struct shim_args *a = p;
    a->status = FT_CreateDeviceInfoList((DWORD *)a->p1);
    LOG("FT_CreateDeviceInfoList() = %u, %u devices", a->status, a->p1 ? *(DWORD *)a->p1 : 0);
    return 0;
}

/* Link speed in Mb/s of the FTDI device with this serial number, from sysfs
 * (/sys/bus/usb/devices/<port>/{idVendor,serial,speed}); 0 if not found. */
static int sysfs_usb_speed(const char *serial)
{
    DIR *d = opendir("/sys/bus/usb/devices");
    struct dirent *e;
    char path[512], buf[64];
    int speed = 0;
    if (!d) return 0;
    while (!speed && (e = readdir(d))) {
        if (e->d_name[0] == '.' || strchr(e->d_name, ':')) continue;   /* skip interfaces */
        FILE *f;
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        int ok = fgets(buf, sizeof(buf), f) && !strncmp(buf, "0403", 4);
        fclose(f);
        if (!ok) continue;
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/serial", e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        ok = fgets(buf, sizeof(buf), f) != NULL;
        fclose(f);
        if (!ok) continue;
        buf[strcspn(buf, "\r\n")] = 0;
        if (strncmp(buf, serial, 16)) continue;
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/speed", e->d_name);
        if ((f = fopen(path, "r"))) { if (fgets(buf, sizeof(buf), f)) speed = atoi(buf); fclose(f); }
    }
    closedir(d);
    return speed;
}

static shim_ntstatus u_get_list(void *p)
{
    struct shim_args *a = p;
    FT_DEVICE_LIST_INFO_NODE *list = a->p1;
    DWORD *count = a->p2;
    a->status = FT_GetDeviceInfoList(list, count);
    LOG("FT_GetDeviceInfoList() = %u, %u devices", a->status, count ? *count : 0);
    if (a->status == FT_OK && list && count)
        for (DWORD i = 0; i < *count; i++) {
            LOG("  [%u] flags 0x%x type %u id 0x%08x loc 0x%x serial \"%.16s\" desc \"%.32s\"", i, list[i].Flags,
                list[i].Type, list[i].ID, list[i].LocId, list[i].SerialNumber, list[i].Description);
            /* libftd3xx 1.1.0 reports a 5 Gb/s OneBox as FT_FLAGS_HISPEED; imec's API
             * then refuses it ("not plugged into USB3 port"). Take the speed from sysfs. */
            char sn[17];
            memcpy(sn, list[i].SerialNumber, 16); sn[16] = 0;
            int mbps = sysfs_usb_speed(sn);
            ULONG flags = list[i].Flags & FT_FLAGS_OPENED;
            if (mbps >= 5000) flags |= FT_FLAGS_SUPERSPEED;
            else if (mbps == 480) flags |= FT_FLAGS_HISPEED;
            else flags = list[i].Flags;                /* unknown: keep FTDI's value */
            /* Windows reports the chip as FT_DEVICE_600/601 (by PID 601e/601f). */
            ULONG type = list[i].Type;
            if (type != FT_DEVICE_600 && type != FT_DEVICE_601) {
                if ((list[i].ID & 0xffff) == 0x601e) type = FT_DEVICE_600;
                else if ((list[i].ID & 0xffff) == 0x601f) type = FT_DEVICE_601;
            }
            if (flags != list[i].Flags || type != list[i].Type)
                LOG("  [%u] sysfs speed %d Mb/s: flags 0x%x -> 0x%x, type %u -> %u", i, mbps,
                    list[i].Flags, flags, list[i].Type, type);
            list[i].Flags = flags;
            list[i].Type = type;
        }
    return 0;
}

static shim_ntstatus u_driver_version(void *p)
{
    struct shim_args *a = p;
    int set; unsigned v = env_hex("FTD3XX_SHIM_DRIVER_VERSION", &set);
    if (set) { if (a->p1) *(DWORD *)a->p1 = v; a->status = FT_OK; }
    else a->status = FT_GetDriverVersion(a->h, (DWORD *)a->p1);
    LOG("FT_GetDriverVersion(%p) = %u, 0x%08x%s", a->h, a->status, a->p1 ? *(DWORD *)a->p1 : 0, set ? " (from env)" : "");
    return 0;
}

static shim_ntstatus u_library_version(void *p)
{
    struct shim_args *a = p;
    int set; unsigned v = env_hex("FTD3XX_SHIM_LIBRARY_VERSION", &set);
    if (set) { if (a->p1) *(DWORD *)a->p1 = v; a->status = FT_OK; }
    else a->status = FT_GetLibraryVersion((DWORD *)a->p1);
    LOG("FT_GetLibraryVersion() = %u, 0x%08x%s", a->status, a->p1 ? *(DWORD *)a->p1 : 0, set ? " (from env)" : "");
    return 0;
}

static shim_ntstatus u_set_pipe_timeout(void *p)
{
    struct shim_args *a = p;
    unsigned ms = a->u2 ? a->u2 : 0xFFFFFFFFu;   /* Windows 0 = forever; Linux 0 = don't wait */
    timeout_set(a->h, a->u1, a->u2);
    a->status = FT_SetPipeTimeout(a->h, (UCHAR)a->u1, ms);
    LOG("FT_SetPipeTimeout(%p, 0x%02x, %u ms -> Linux %u) = %u", a->h, a->u1, a->u2, ms, a->status);
    return 0;
}

static shim_ntstatus u_get_overlapped_result(void *p)
{
    struct shim_args *a = p;
    OVERLAPPED *lin = ov_find(a->p1);
    if (!lin) { a->status = FT_INVALID_PARAMETER; LOG("FT_GetOverlappedResult: unknown OVERLAPPED %p", a->p1); return 0; }
    a->status = FT_GetOverlappedResult(a->h, lin, (ULONG *)a->p2, (BOOL)a->u1);
    atomic_fetch_add(&st_results, 1);
    if (a->status == FT_OK && a->p2) atomic_fetch_add(&st_result_bytes, *(ULONG *)a->p2);
    else if (a->status != FT_IO_PENDING && a->status != FT_IO_INCOMPLETE) atomic_fetch_add(&st_result_fail, 1);
    stats_tick();
    LOG_BUSY(a->status, "FT_GetOverlappedResult(%p, ov %p, wait %u) = %u, done %u", a->h, a->p1, a->u1, a->status,
             a->p2 ? *(ULONG *)a->p2 : 0);
    return 0;
}

static shim_ntstatus u_init_overlapped(void *p)
{
    struct shim_args *a = p;
    OVERLAPPED *lin = calloc(1, sizeof(*lin));
    if (!lin) { a->status = FT_INSUFFICIENT_RESOURCES; return 0; }
    a->status = FT_InitializeOverlapped(a->h, lin);
    atomic_fetch_add(&st_ov_init, 1);
    if (a->status == FT_OK && !ov_add(a->p1, a->h, lin)) {
        FT_ReleaseOverlapped(a->h, lin);
        a->status = FT_INSUFFICIENT_RESOURCES;
    }
    if (a->status != FT_OK) free(lin);
    LOG("FT_InitializeOverlapped(%p, ov %p) = %u", a->h, a->p1, a->status);
    return 0;
}

static shim_ntstatus u_release_overlapped(void *p)
{
    struct shim_args *a = p;
    OVERLAPPED *lin = ov_remove(a->p1);
    if (!lin) { a->status = FT_INVALID_PARAMETER; LOG("FT_ReleaseOverlapped: unknown OVERLAPPED %p", a->p1); return 0; }
    a->status = FT_ReleaseOverlapped(a->h, lin);
    atomic_fetch_add(&st_ov_release, 1);
    free(lin);
    LOG("FT_ReleaseOverlapped(%p, ov %p) = %u", a->h, a->p1, a->status);
    return 0;
}

static shim_ntstatus u_set_stream_pipe(void *p)
{
    struct shim_args *a = p;
    a->status = FT_SetStreamPipe(a->h, a->u1, a->u2, (UCHAR)a->u3, a->u4);
    LOG("FT_SetStreamPipe(%p, allW %u, allR %u, 0x%02x, %u) = %u", a->h, a->u1, a->u2, a->u3, a->u4, a->status);
    return 0;
}

static shim_ntstatus u_clear_stream_pipe(void *p)
{
    struct shim_args *a = p;
    a->status = FT_ClearStreamPipe(a->h, a->u1, a->u2, (UCHAR)a->u3);
    LOG("FT_ClearStreamPipe(%p, allW %u, allR %u, 0x%02x) = %u", a->h, a->u1, a->u2, a->u3, a->status);
    return 0;
}

static shim_ntstatus u_abort_pipe(void *p)
{
    struct shim_args *a = p;
    a->status = FT_AbortPipe(a->h, (UCHAR)a->u1);
    LOG("FT_AbortPipe(%p, 0x%02x) = %u", a->h, a->u1, a->status);
    return 0;
}

static shim_ntstatus u_set_suspend_timeout(void *p)
{
    struct shim_args *a = p;
    a->status = FT_OK;   /* not on Linux; USB autosuspend is the kernel's */
    LOG("FT_SetSuspendTimeout(%p, %u) = 0 (no-op on Linux)", a->h, a->u1);
    return 0;
}

/* Linux status -> the Windows status number imec's DLL expects */
static uint32_t win_status(uint32_t s)
{
    switch (s) {
    case 11: return FT_INSUFFICIENT_RESOURCES;  /* Linux FT_MEM_INSUFFICIENT_ERROR */
    case 12: return FT_IO_ERROR;                /* Linux FT_OVERFLOW_ERROR */
    default: return s;
    }
}

#define ENTRY(fn) static shim_ntstatus e_##fn(void *p) {                        \
    shim_ntstatus r = fn(p); struct shim_args *a = p;                         \
    uint32_t w = win_status(a->status);                                       \
    if (w != a->status) { LOG(#fn ": Linux status %u sent as Windows %u", a->status, w); a->status = w; } \
    return r; }
ENTRY(u_create) ENTRY(u_close) ENTRY(u_write_pipe) ENTRY(u_read_pipe) ENTRY(u_flush_pipe)
ENTRY(u_create_list) ENTRY(u_get_list) ENTRY(u_driver_version) ENTRY(u_library_version)
ENTRY(u_set_pipe_timeout) ENTRY(u_get_overlapped_result) ENTRY(u_init_overlapped)
ENTRY(u_release_overlapped) ENTRY(u_set_stream_pipe) ENTRY(u_clear_stream_pipe)
ENTRY(u_abort_pipe) ENTRY(u_set_suspend_timeout)

__attribute__((visibility("default")))
const unixlib_entry_t __wine_unix_call_funcs[c_count] = {
    [c_create] = e_u_create,
    [c_close] = e_u_close,
    [c_write_pipe] = e_u_write_pipe,
    [c_read_pipe] = e_u_read_pipe,
    [c_flush_pipe] = e_u_flush_pipe,
    [c_create_list] = e_u_create_list,
    [c_get_list] = e_u_get_list,
    [c_driver_version] = e_u_driver_version,
    [c_library_version] = e_u_library_version,
    [c_set_pipe_timeout] = e_u_set_pipe_timeout,
    [c_get_overlapped_result] = e_u_get_overlapped_result,
    [c_init_overlapped] = e_u_init_overlapped,
    [c_release_overlapped] = e_u_release_overlapped,
    [c_set_stream_pipe] = e_u_set_stream_pipe,
    [c_clear_stream_pipe] = e_u_clear_stream_pipe,
    [c_abort_pipe] = e_u_abort_pipe,
    [c_set_suspend_timeout] = e_u_set_suspend_timeout,
};
