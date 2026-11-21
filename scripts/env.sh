# Source this before running anything under Wine with the shim:
#   SGL_WINE=/path/to/wine . scripts/env.sh
#
# SGL_WINE   (required) the Wine 11 install tree the shim was installed into
# WINEPREFIX (optional) default ~/.wine-spikeglx
# BOX64      (Arm only) path to the box64 binary, default `box64` on PATH
# BOX64_X64LIB (Arm only, optional) Box64's x64lib folder, added to its library path
: "${SGL_WINE:?set SGL_WINE to the Wine install tree (the one you ran make install into)}"
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine-spikeglx}
export WINEDEBUG=${WINEDEBUG:-err+all,fixme-all}
# ftd3xx=b: use the shim (Wine builtin) instead of the FTD3XX.dll shipped beside SpikeGLX.
# mscoree,mshtml=: don't offer to install Mono / Gecko when the prefix is created.
export WINEDLLOVERRIDES="ftd3xx=b;mscoree,mshtml="
# imec's API requires FTDI driver >= 1.3.10 and reads this number in its own way:
# 0x01030010 reads as 1.3.10 (0x0103000A would read as 1.3.4). See README.
export FTD3XX_SHIM_DRIVER_VERSION=${FTD3XX_SHIM_DRIVER_VERSION:-0x01030010}
SGL_WRAP=""
if [ "$(uname -m)" = aarch64 ]; then
    SGL_WRAP=${BOX64:-box64}
    export BOX64_LD_LIBRARY_PATH="${BOX64_X64LIB:+$BOX64_X64LIB:}$SGL_WINE/lib/wine/x86_64-unix"
    export BOX64_LOG=${BOX64_LOG:-0}
fi
sgl_wine()       { $SGL_WRAP "$SGL_WINE/bin/wine" "$@"; }
sgl_wineserver() { $SGL_WRAP "$SGL_WINE/bin/wineserver" "$@"; }
sgl_wineboot()   { $SGL_WRAP "$SGL_WINE/bin/wineboot" "$@"; }
sgl_prefix() {   # create the prefix once
    [ -f "$WINEPREFIX/system.reg" ] && return 0
    echo "creating Wine prefix $WINEPREFIX"
    sgl_wineboot -i >/dev/null 2>&1; sgl_wineserver -w
}
