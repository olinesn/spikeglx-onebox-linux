#!/bin/bash
# Start SpikeGLX under Wine with the shim.
#   SGL_WINE=/path/to/wine scripts/run_spikeglx.sh /path/to/SpikeGLX [SpikeGLX_NISIM.exe]
# Use SpikeGLX_NISIM.exe (imec probes and OneBox, no National Instruments driver needed);
# SpikeGLX.exe needs NI-DAQmx. SpikeGLX keeps its settings in _Configs/ beside the exe and
# its probe calibration in _Calibration/, so the folder must be writable.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SGL_DIR=${1:?usage: run_spikeglx.sh /path/to/SpikeGLX [exe]}
cd "$SGL_DIR"
sgl_prefix
exec $SGL_WRAP "$SGL_WINE/bin/wine" "${2:-SpikeGLX_NISIM.exe}"
