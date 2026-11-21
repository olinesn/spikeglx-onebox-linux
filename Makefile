# spikeglx-onebox-linux: a Wine builtin FTD3XX.dll backed by FTDI's Linux libftd3xx,
# so imec's Windows NeuropixAPI (and SpikeGLX) can talk to a Neuropixels OneBox on Linux.
#
# Build on x86-64 Linux. Both halves are x86-64 code: on an Arm host the same
# files run under Box64 together with Wine.
#
#   make WINE=/path/to/wine FTDI=/path/to/libftd3xx-linux-x86_64
#   make install WINE=... FTDI=...     # copies into that Wine tree
#
# WINE : a Wine 11 install tree (has bin/winebuild, lib/wine/x86_64-windows, lib/wine/x86_64-unix)
# FTDI : the unpacked FTDI "D3XX for Linux" x86-64 package (has ftd3xx.h, libftd3xx.so)

WINE  ?=
FTDI  ?=
MINGW ?= x86_64-w64-mingw32-gcc
CC    ?= gcc

ifeq ($(strip $(WINE)),)
$(error set WINE=<Wine 11 install tree>, e.g. WINE=$$HOME/wine11/opt/wine-stable)
endif
ifeq ($(strip $(FTDI)),)
$(error set FTDI=<unpacked FTDI D3XX for Linux, x86-64>, e.g. FTDI=$$HOME/ftdi/linux-x86_64)
endif

BUILD := build

all: $(BUILD)/ftd3xx.dll $(BUILD)/ftd3xx.so $(BUILD)/np_probe.exe

$(BUILD):
	mkdir -p $@

# Windows half: a PE DLL with FTDI's export ordinals, marked as a Wine builtin so
# Wine's ntdll loads ftd3xx.so (the Linux half) beside it.
$(BUILD)/ftd3xx.dll: src/ftd3xx_pe.c src/ftd3xx.def src/ftd3xx_shim.h | $(BUILD)
	$(MINGW) -O2 -Wall -Wno-missing-prototypes -shared -static-libgcc -o $@ src/ftd3xx_pe.c src/ftd3xx.def
	$(WINE)/bin/winebuild --builtin $@

# Linux half: finds libftd3xx.so in its own directory ($ORIGIN).
$(BUILD)/ftd3xx.so: src/ftd3xx_unix.c src/ftd3xx_shim.h | $(BUILD)
	$(CC) -O2 -Wall -fPIC -shared -o $@ src/ftd3xx_unix.c -I$(FTDI) -L$(FTDI) -lftd3xx -lpthread '-Wl,-rpath,$$ORIGIN'

# Test tool: loads imec's API and asks it what SpikeGLX asks (see README).
$(BUILD)/np_probe.exe: tools/np_probe.c | $(BUILD)
	$(MINGW) -O2 -Wall -o $@ tools/np_probe.c

# Optional: a stand-in libftd3xx.so that reports no devices, to test the Wine
# plumbing without FTDI's library. Not installed by `make install`.
$(BUILD)/fake/libftd3xx.so: tools/fake_libftd3xx.c | $(BUILD)
	mkdir -p $(BUILD)/fake
	$(CC) -O2 -shared -fPIC -Wl,-soname,libftd3xx.so -o $@ tools/fake_libftd3xx.c -I$(FTDI)

fake: $(BUILD)/fake/libftd3xx.so

# Into the given Wine tree. Use a private Wine tree for this, not a distribution's Wine.
install: $(BUILD)/ftd3xx.dll $(BUILD)/ftd3xx.so
	install -m 644 $(BUILD)/ftd3xx.dll $(WINE)/lib/wine/x86_64-windows/ftd3xx.dll
	install -m 755 $(BUILD)/ftd3xx.so $(WINE)/lib/wine/x86_64-unix/ftd3xx.so
	cp -P $(FTDI)/libftd3xx.so* $(WINE)/lib/wine/x86_64-unix/

clean:
	rm -rf $(BUILD)

.PHONY: all fake install clean
