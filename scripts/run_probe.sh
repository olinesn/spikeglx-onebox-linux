#!/bin/bash
# Check the whole chain without SpikeGLX: imec's API -> shim -> FTDI's Linux library -> OneBox.
#   SGL_WINE=/path/to/wine scripts/run_probe.sh /path/to/SpikeGLX [open]
# /path/to/SpikeGLX is the folder of a SpikeGLX API4 release (it holds NeuropixAPI_x64_4_1_3.dll).
# "open" also opens each OneBox, its headstages and probes, and prints their IDs.
# Set FTD3XX_SHIM_LOG=stderr (or a file path) to log every FTDI call.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SGL_DIR=${1:?usage: run_probe.sh /path/to/SpikeGLX [open]}
SGL_DIR=$(cd "$SGL_DIR" && pwd)
PROBE=${PROBE_EXE:-$HERE/../build/np_probe.exe}
sgl_prefix
t=$(date +%s.%N)
sgl_wine "$PROBE" "Z:$(echo "$SGL_DIR" | tr / '\\')" ${2:-}
echo "--- exit $?, $(echo "$(date +%s.%N) - $t" | bc) s"
