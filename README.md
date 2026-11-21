# spikeglx-onebox-linux

Record from a **Neuropixels OneBox with SpikeGLX on Linux**, on x86-64 and on Arm.

SpikeGLX and imec's Neuropixels API are Windows-only. This project runs the unmodified
Windows SpikeGLX under [Wine](https://www.winehq.org/), and replaces one Windows DLL
(FTDI's `FTD3XX.dll`, the OneBox's USB library) with a small bridge to FTDI's own
**Linux** version of that library. imec's API and SpikeGLX don't notice the difference.
On Arm, [Box64](https://github.com/ptitSeb/box64) runs the x86-64 code.

> **Status: experimental, not supported by imec, HHMI/Janelia (SpikeGLX) or FTDI.**
> It recorded real data on two machines (below), but **a 2-hour test stopped streaming after
> 18 minutes** (see [Known problems](#known-problems)). Don't use it for real recordings yet.

## What was tested (2026-11-20)

| | x86-64 | Arm |
|---|---|---|
| Machine | GMKtec EVO-X2 (AMD Ryzen AI Max+ 395, Radeon 8060S) | NVIDIA Jetson AGX Orin (Cortex-A78AE), Box64 0.4.4 |
| OS | Ubuntu 24.04, kernel 7.0 | Ubuntu 24.04 (L4T R39.2), kernel 6.8-tegra |
| Wine | WineHQ 11.0 stable (unpacked, no root) | same, under Box64 |
| SpikeGLX | v20261020 (API4), imec API 4.1.3, `SpikeGLX_NISIM.exe` | same |
| FTDI library | D3XX for Linux 1.1.0, x86-64 | same x86-64 file (it runs inside the emulated process) |
| Hardware | OneBox (NPOB_ACQ_00-05-04, firmware 4.0.240), headstage NPM_HS_31, probe NP2013 (NP 2.0, four shanks) | same OneBox, behind a USB 3 hub |
| Stream | 30,001 samples/s over 10 s; 2 min 10 s run, no errors | 30,014 samples/s over 31 s |
| Recording | 30.7 s `.ap.bin`/`.meta`: size = samples × 385 channels × 2 bytes exactly, all 384 channels live, no zero rows | 30.8 s, same checks |
| SpikeGLX CPU (graphs on) | 10–20 % of one core | 50–70 % of one core |
| 2-hour test | not run | **failed:** streaming stopped after 18 min 20 s ([Known problems](#known-problems)) |

Not tested yet: two probes, the OneBox's own analog/digital channels, and the sync input.
PXI basestations are **not** covered (they need a Windows PCIe kernel driver).

## How it works

```
SpikeGLX_NISIM.exe  ─┐
imec NeuropixAPI DLL ├─ unmodified Windows x86-64 code, running under Wine (and Box64 on Arm)
                     │
build/ftd3xx.dll  ◄──┘  Windows half of the bridge: exports the FTD3XX functions imec uses,
       │                with FTDI's ordinals, and passes each call to the Linux half
       ▼                (through Wine's unix-call interface, as Wine's own DLLs do)
build/ftd3xx.so         Linux half: calls FTDI's libftd3xx.so, adapting what differs
       ▼
libftd3xx.so (FTDI, Linux) ─► libusb ─► OneBox (FT601, USB 0403:601f)
```

imec's DLL uses 17 FTD3XX functions. The Linux library has the same functions, but not always the
same behaviour, and imec's code depends on some of the differences. The bridge handles these:

- **Link speed:** FTDI's Linux 1.1.0 reports a OneBox on a 5 Gb/s port as "high speed"
  (`FT_FLAGS_HISPEED`). imec's API then refuses it ("OneBox ... not plugged into USB3 port!")
  and lists no OneBox. The bridge reads the real link speed from Linux (`/sys/bus/usb/devices`).
- **Driver version:** imec requires FTDI driver ≥ 1.3.10 and decodes the number unusually:
  `0x0103000A` reads as 1.3.4, `0x01030010` as 1.3.10. Linux has no Windows driver, so the
  bridge reports the value in `FTD3XX_SHIM_DRIVER_VERSION` (the scripts set `0x01030010`).
  *We don't know which Windows driver fix imec's minimum stands for, or whether FTDI's Linux
  library has the equivalent.*
- **Timeouts:** a pipe timeout of 0 means "wait forever" on Windows and "don't wait" on Linux.
- **Asynchronous reads:** Windows passes an `OVERLAPPED` structure and a pipe ID; Linux has
  separate async calls that take a FIFO number and a differently sized `OVERLAPPED`.
- **Error codes:** libftd3xx 1.1.0 reuses two Windows status numbers (11 and 12) for other errors.
- **USB suspend:** Linux has no `FT_SetSuspendTimeout`; the bridge accepts it and does nothing.

## Setup

You need an x86-64 Linux machine to **build** (on Arm, build on an x86-64 machine and copy
`build/` over, or use an x86-64 cross compiler as `CC`).

1. **Wine 11.** WineHQ's packages work. Without root you can unpack them into a folder:
   download `wine-stable` and `wine-stable-amd64` (11.0) from WineHQ's Ubuntu repository and run
   `dpkg -x <file>.deb ~/wine11` for each; the Wine tree is then `~/wine11/opt/wine-stable`.
   Use a private Wine tree for this, because `make install` copies files into it.
2. **Compilers:** `gcc` and mingw-w64 (`x86_64-w64-mingw32-gcc`, Ubuntu package
   `gcc-mingw-w64-x86-64`).
3. **FTDI D3XX for Linux, x86-64** (version 1.1.0 tested) from FTDI's D3XX drivers page.
   Unpack it; the folder with `ftd3xx.h` and `libftd3xx.so` is `FTDI` below. Use the x86-64
   package on Arm too.
4. **SpikeGLX API4 release** from the [SpikeGLX download page](https://billkarsh.github.io/SpikeGLX/).
   Unzip it to a writable folder. It brings imec's API DLLs.
5. **Build and install:**
   ```bash
   make WINE=$HOME/wine11/opt/wine-stable FTDI=$HOME/ftdi/linux-x86_64
   make install WINE=$HOME/wine11/opt/wine-stable FTDI=$HOME/ftdi/linux-x86_64
   ```
6. **USB permissions** (once per machine, needs sudo): see `udev/60-neuropixels-onebox.rules`.
   Then unplug and replug the OneBox. Use a USB 3 port.
7. **Check the chain** with the OneBox plugged in:
   ```bash
   SGL_WINE=$HOME/wine11/opt/wine-stable scripts/run_probe.sh /path/to/SpikeGLX open
   ```
   Expect `checkFtdiDriver: ... ok 1`, `getDeviceList: 1`, the OneBox's firmware, and for each
   headstage and probe their part and serial numbers.
8. **Run SpikeGLX:**
   ```bash
   SGL_WINE=$HOME/wine11/opt/wine-stable scripts/run_spikeglx.sh /path/to/SpikeGLX
   ```
   - **File ▸ New Acquisition** (Ctrl+N), Devices tab:
     **Configure Slots** (give your OneBox's serial a slot, e.g. 20), tick **Enable Imec**, click
     **Detect**, then **Run**. Detect can take 10–30 s; if the desktop says SpikeGLX is not
     responding, choose Wait.
   - **Calibration:** SpikeGLX stops Run with "Can't find calibration folder '\<probe SN\>'" until
     the probe's calibration files from imec are in `_Calibration/<probe SN>/` beside the exe
     (for an NP 2.0 probe, `<SN>_gainCalValues.csv`; NP 1.0 also needs `<SN>_ADCCalibration.csv`).
     For a quick test you can set **Cal policy** on the IM Setup tab to "Skip all calibration"
     instead (the data are then uncalibrated).
   - SpikeGLX's command server (Options ▸ Command Server Settings) works as on Windows;
     `tools/sglx_cmd.py` sends commands to it from Linux, e.g. `python3 tools/sglx_cmd.py
     ISRUNNING "GETSTREAMSAMPLECOUNT 2 0"`. Note that `STARTRUN` only works after the run
     settings have been checked once in the Configure dialog.

### Arm (Box64)

Build [Box64](https://github.com/ptitSeb/box64) for your board (we used 0.4.4 with
`-DTEGRA_T234=ON` on a Jetson AGX Orin), then set `BOX64=/path/to/box64` (and, if your build
needs it, `BOX64_X64LIB=/path/to/box64/x64lib`). Everything else is the same, with the same
x86-64 files. In our test SpikeGLX used 50–70 % of a Cortex-A78AE core, against 10–20 % of a
Zen 5 core on x86-64. Your kernel needs 4 KiB memory pages for Wine (the Jetson's default; a
Raspberry Pi 5's default 16 KiB pages are not).

## Known problems

**Streaming stops after about 18 minutes** (2-hour test on the Arm machine, 2026-11-20).
- The stream ran at exactly 30,000 samples/s for 18 min 20 s.
- Then FTDI's Linux library returned -1 (not a valid FTDI status) from `FT_GetOverlappedResult`,
  and imec's API shut the data pipe down.
- SpikeGLX kept reporting "running" and "saving", but the sample count and the file stopped growing.
- The kernel logged no USB error.

**Memory grows while streaming,** by about 128 MB a minute (2 MB/s), on both machines, so not
because of Box64. It stops growing when the stream stops, and is probably related.
- The bridge allocates nothing per transfer.
- The likely place is FTDI's asynchronous read path, which imec's API uses for the data stream
  (FTDI notes that asynchronous calls aren't needed on Linux). Not confirmed yet.

Until this is fixed, keep recordings to a few minutes, and watch the stream's sample count (for
example `python3 tools/sglx_cmd.py "GETSTREAMSAMPLECOUNT 2 0"`).

## Troubleshooting

- `FTD3XX_SHIM_LOG=stderr` (or a file path) logs every FTDI call: the first 50 reads and writes,
  then only failures.
- `getDeviceList: 0` with the OneBox plugged in: check `lsusb -t` shows it at 5000M, the udev
  rule, and that `WINEDLLOVERRIDES` contains `ftd3xx=b` (`scripts/env.sh` sets it).
- One `FT_GetOverlappedResult ... = 19` (timeout) during Detect is normal; it happens before the run.
- `make fake` builds a stand-in `libftd3xx.so` that reports no devices, to test the Wine side
  without FTDI's library.

## Licenses

This repository's code is under the BSD 3-Clause license (see `LICENSE`). It contains no files from
FTDI, imec, HHMI/Janelia or Wine; you download those yourself:

- **FTDI D3XX for Linux:** FTDI's terms (in its `ftd3xx.h`) allow use "only in conjunction with
  products based on FTDI parts" (the OneBox uses an FTDI FT601) and redistribution as long as the
  licence information is not modified. The library includes libusb (LGPL).
- **imec Neuropixels API** (distributed with SpikeGLX): we found no published licence terms.
  It is used here unmodified.
- **SpikeGLX:** Janelia Research Campus Software Copyright (BSD-style), see its repository.
- **Wine:** LGPL. **Box64:** MIT.

Not affiliated with imec, HHMI/Janelia, FTDI, WineHQ or the Box64 project.
