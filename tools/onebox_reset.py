#!/usr/bin/env python3
"""Reset a Neuropixels OneBox's USB connection, as if it were unplugged and plugged in again.

Use it when FTDI's library lists the OneBox with an empty serial number and description
(FTD3XX_SHIM_LOG shows `serial "" desc ""`, SpikeGLX says "Can't find FTDI driver: Check
OneBox power"). That can happen after SpikeGLX or Wine was killed in the middle of a transfer.
Close SpikeGLX first.

    python3 tools/onebox_reset.py [serial]     # default: every FTDI FT600/FT601 (0403:601e/601f)

It needs write access to the device node, which the udev rule in udev/ gives.
"""
import fcntl
import os
import sys

USBDEVFS_RESET = 0x5514          # _IO('U', 20)
SYS = "/sys/bus/usb/devices"


def read(dev, name):
    try:
        with open(os.path.join(SYS, dev, name)) as f:
            return f.read().strip()
    except OSError:
        return None


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else None
    found = 0
    for dev in sorted(os.listdir(SYS)):
        if ":" in dev or read(dev, "idVendor") != "0403" or read(dev, "idProduct") not in ("601e", "601f"):
            continue
        serial = read(dev, "serial") or ""
        if want and serial != want:
            continue
        node = "/dev/bus/usb/%03d/%03d" % (int(read(dev, "busnum")), int(read(dev, "devnum")))
        fd = os.open(node, os.O_WRONLY)
        try:
            fcntl.ioctl(fd, USBDEVFS_RESET, 0)
        finally:
            os.close(fd)
        print("reset %s (%s, serial %s, %s Mb/s)" % (node, read(dev, "product"), serial, read(dev, "speed")))
        found += 1
    if not found:
        sys.exit("no matching OneBox (FTDI 0403:601e/601f) found")


if __name__ == "__main__":
    main()
