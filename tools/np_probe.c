/* np_probe.exe: loads imec's NeuropixAPI DLL (Windows, run under Wine) and asks
 * it what SpikeGLX asks at startup: API version, FTDI driver check, scanBS,
 * getDeviceList. Shows which FTD3XX.dll got loaded.
 *
 *   wine np_probe.exe 'Z:\path\to\SpikeGLX'          (folder with the imec DLLs)
 */
#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

typedef struct { int platformid; int ID; } basestationID;   /* NPPlatform_PXI = 1, USB = 2 */
typedef struct { int vmajor, vminor, vbuild; } ftdi_driver_version_t;

typedef void (*fn_ver_t)(int *, int *, int *);
typedef size_t (*fn_str_t)(char *, size_t);
typedef void (*fn_ftdi_t)(ftdi_driver_version_t *, ftdi_driver_version_t *, bool *, bool *);
typedef int (*fn_scan_t)(void);
typedef int (*fn_list_t)(basestationID *, int);
typedef const char *(*fn_err_t)(int);

typedef struct { int major, minor, build; char name[64]; } firmware_Info;
#pragma pack(push, 1)
typedef struct { unsigned long long SerialNumber; char ProductNumber[64]; char version_Major[16]; char version_Minor[16]; } HardwareID;
#pragma pack(pop)
typedef int (*fn_map_t)(int, int);
typedef int (*fn_slot_t)(int);
typedef int (*fn_fw_t)(int, firmware_Info *);
typedef void (*fn_supp_t)(int, firmware_Info *, firmware_Info *, bool *, bool *);
typedef int (*fn_drv_t)(int, char *, size_t);
typedef int (*fn_hwid_t)(int, HardwareID *);
typedef int (*fn_count_t)(int, int *);
typedef int (*fn_detect_t)(int, int, bool *);
typedef int (*fn_hwid_port_t)(int, int, HardwareID *);
typedef int (*fn_hwid_dock_t)(int, int, int, HardwareID *);
typedef int (*fn_dock_t)(int, int, int);
#define GET(type, name) type name = (type)(void (*)(void))GetProcAddress(m, #name); \
    if (!name) { printf("missing export %s\n", #name); return 2; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[MAX_PATH];

    SetDllDirectoryA(dir);
    snprintf(path, sizeof(path), "%s\\NeuropixAPI_x64_4_1_3.dll", dir);
    HMODULE m = LoadLibraryA(path);
    if (!m) { printf("LoadLibrary(%s) failed, error %lu\n", path, GetLastError()); return 1; }

    HMODULE ft = GetModuleHandleA("FTD3XX.dll");
    if (ft && GetModuleFileNameA(ft, path, sizeof(path))) printf("FTD3XX.dll loaded from: %s\n", path);
    HMODULE fm = GetModuleHandleA("FpgaManager.dll");
    if (fm && GetModuleFileNameA(fm, path, sizeof(path))) printf("FpgaManager.dll loaded from: %s\n", path);

    GET(fn_ver_t, np_getAPIVersion);
    GET(fn_str_t, np_getAPIVersionFull);
    GET(fn_ver_t, np_getMinimumFtdiDriverVersion);
    GET(fn_ftdi_t, np_checkFtdiDriver);
    GET(fn_scan_t, np_scanBS);
    GET(fn_list_t, np_getDeviceList);
    GET(fn_str_t, np_getLastErrorMessage);
    GET(fn_err_t, np_getErrorMessage);

    int a = 0, b = 0, c = 0;
    char buf[512] = "";
    np_getAPIVersion(&a, &b, &c);
    np_getAPIVersionFull(buf, sizeof(buf));
    printf("API version: %d.%d.%d (%s)\n", a, b, c, buf);

    np_getMinimumFtdiDriverVersion(&a, &b, &c);
    printf("minimum FTDI driver: %d.%d.%d\n", a, b, c);

    ftdi_driver_version_t req = {0}, cur = {0};
    bool present = false, ok = false;
    np_checkFtdiDriver(&req, &cur, &present, &ok);
    printf("checkFtdiDriver: required %d.%d.%d, current %d.%d.%d, present %d, ok %d\n",
           req.vmajor, req.vminor, req.vbuild, cur.vmajor, cur.vminor, cur.vbuild, present, ok);

    DWORD t0 = GetTickCount();
    int rc = np_scanBS();
    printf("scanBS: %d (%s), %lu ms\n", rc, np_getErrorMessage(rc), GetTickCount() - t0);

    basestationID list[16];
    int n = np_getDeviceList(list, 16);
    printf("getDeviceList: %d\n", n);
    for (int i = 0; i < n && i < 16; i++)
        printf("  [%d] platform %s, ID %d\n", i,
               list[i].platformid == 2 ? "USB (OneBox)" : list[i].platformid == 1 ? "PXI" : "?", list[i].ID);

    buf[0] = 0;
    np_getLastErrorMessage(buf, sizeof(buf));
    printf("last error message: %s\n", buf[0] ? buf : "(none)");

    /* "open": map each OneBox to a slot (SpikeGLX uses 20..31) and open it,
     * the headstages and probes the way SpikeGLX's Detect does. */
    if (argc > 2 && !strcmp(argv[2], "open")) {
        GET(fn_map_t, np_mapBS);
        GET(fn_slot_t, np_openBS);
        GET(fn_slot_t, np_closeBS);
        GET(fn_slot_t, np_unmapBS);
        GET(fn_fw_t, np_bs_getFirmwareInfo);
        GET(fn_fw_t, np_bsc_getFirmwareInfo);
        GET(fn_supp_t, np_checkBasestationSupported);
        GET(fn_drv_t, np_getBasestationDriverID);
        GET(fn_hwid_t, np_getBSCHardwareID);
        GET(fn_count_t, np_getBSCSupportedPortCount);
        GET(fn_detect_t, np_detectHeadStage);
        GET(fn_hwid_port_t, np_getHeadstageHardwareID);
        GET(fn_hwid_dock_t, np_getFlexHardwareID);
        GET(fn_hwid_dock_t, np_getProbeHardwareID);
        GET(fn_dock_t, np_openProbe);
        GET(fn_dock_t, np_closeProbe);
#define CHECK(what, expr) do { DWORD t_ = GetTickCount(); int r_ = (expr); \
        printf("%-34s %d (%s), %lu ms\n", what, r_, np_getErrorMessage(r_), GetTickCount() - t_); \
        if (r_) { buf[0] = 0; np_getLastErrorMessage(buf, sizeof(buf)); if (buf[0]) printf("    last error: %s\n", buf); } } while (0)
        int slot = 20;
        for (int i = 0; i < n && i < 16; i++) {
            if (list[i].platformid != 2) continue;
            char what[64];
            snprintf(what, sizeof(what), "mapBS(%d, %d)", list[i].ID, slot);
            CHECK(what, np_mapBS(list[i].ID, slot));
            CHECK("openBS", np_openBS(slot));
            firmware_Info fw;
            memset(&fw, 0, sizeof(fw));
            CHECK("bs_getFirmwareInfo", np_bs_getFirmwareInfo(slot, &fw));
            printf("    OneBox firmware %d.%d.%d %s\n", fw.major, fw.minor, fw.build, fw.name);
            memset(&fw, 0, sizeof(fw));
            CHECK("bsc_getFirmwareInfo", np_bsc_getFirmwareInfo(slot, &fw));
            printf("    BSC firmware %d.%d.%d %s\n", fw.major, fw.minor, fw.build, fw.name);
            firmware_Info cf, bf;
            bool cok = false, bok = false;
            memset(&cf, 0, sizeof(cf)); memset(&bf, 0, sizeof(bf));
            np_checkBasestationSupported(slot, &cf, &bf, &cok, &bok);
            printf("checkBasestationSupported\n    minimum: carrier %d.%d.%d ok %d, BSC %d.%d.%d ok %d\n",
                   cf.major, cf.minor, cf.build, cok, bf.major, bf.minor, bf.build, bok);
            buf[0] = 0;
            CHECK("getBasestationDriverID", np_getBasestationDriverID(slot, buf, sizeof(buf)));
            printf("    driver: %s\n", buf);
            HardwareID hw;
            memset(&hw, 0, sizeof(hw));
            CHECK("getBSCHardwareID", np_getBSCHardwareID(slot, &hw));
            printf("    BSC: SN %llu, PN %s, v%s.%s\n", (unsigned long long)hw.SerialNumber, hw.ProductNumber, hw.version_Major, hw.version_Minor);
            int ports = 0;
            CHECK("getBSCSupportedPortCount", np_getBSCSupportedPortCount(slot, &ports));
            printf("    ports: %d\n", ports);
            for (int port = 1; port <= ports && port <= 8; port++) {
                bool hs = false;
                snprintf(what, sizeof(what), "detectHeadStage(port %d)", port);
                CHECK(what, np_detectHeadStage(slot, port, &hs));
                printf("    headstage on port %d: %s\n", port, hs ? "yes" : "no");
                if (!hs) continue;
                memset(&hw, 0, sizeof(hw));
                CHECK("  getHeadstageHardwareID", np_getHeadstageHardwareID(slot, port, &hw));
                printf("      headstage: SN %llu, PN %s, v%s.%s\n", (unsigned long long)hw.SerialNumber, hw.ProductNumber, hw.version_Major, hw.version_Minor);
                memset(&hw, 0, sizeof(hw));
                CHECK("  getFlexHardwareID(dock 1)", np_getFlexHardwareID(slot, port, 1, &hw));
                printf("      flex: SN %llu, PN %s, v%s.%s\n", (unsigned long long)hw.SerialNumber, hw.ProductNumber, hw.version_Major, hw.version_Minor);
                CHECK("  openProbe(dock 1)", np_openProbe(slot, port, 1));
                memset(&hw, 0, sizeof(hw));
                CHECK("  getProbeHardwareID(dock 1)", np_getProbeHardwareID(slot, port, 1, &hw));
                printf("      probe: SN %llu, PN %s, v%s.%s\n", (unsigned long long)hw.SerialNumber, hw.ProductNumber, hw.version_Major, hw.version_Minor);
                CHECK("  closeProbe(dock 1)", np_closeProbe(slot, port, 1));
            }
            CHECK("closeBS", np_closeBS(slot));
            CHECK("unmapBS", np_unmapBS(slot));
            slot++;
        }
    }
    return 0;
}
