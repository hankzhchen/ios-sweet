#!/usr/bin/env python3
"""Pack an Android boot image, header version 3 (no dtb, no load addresses).

ABL on v3 devices (yupik) takes the dtb, load addresses and the first part of
the ramdisk from the device's own vendor_boot partition; this image only
carries the kernel, our ramdisk and a cmdline that ABL appends to vendor_boot's.

  mkbootimg_v3.py --kernel Image --ramdisk ramdisk.bin -o boot.img
"""
import argparse
import struct

PAGE = 4096
HDR_SIZE = 1580  # boot_img_hdr_v3


def os_version(release, patch):
    a, b, c = (list(map(int, release.split('.'))) + [0, 0])[:3]
    y, m = map(int, patch.split('-'))
    return (((a << 14) | (b << 7) | c) << 11) | (((y - 2000) << 4) | m)


def pad(b):
    return b + b'\0' * (-len(b) % PAGE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kernel', required=True)
    ap.add_argument('--ramdisk', required=True)
    ap.add_argument('--cmdline', default='')
    ap.add_argument('--os_version', default='12.0.0')
    ap.add_argument('--os_patch_level', default='2022-01')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()

    kernel = open(a.kernel, 'rb').read()
    ramdisk = open(a.ramdisk, 'rb').read()
    cmdline = a.cmdline.encode()
    if len(cmdline) >= 1536:
        raise SystemExit('cmdline too long')
    hdr = struct.pack('<8sIIII4II1536s', b'ANDROID!', len(kernel), len(ramdisk),
                      os_version(a.os_version, a.os_patch_level), HDR_SIZE,
                      0, 0, 0, 0, 3, cmdline)
    assert len(hdr) == HDR_SIZE
    with open(a.output, 'wb') as f:
        f.write(pad(hdr) + pad(kernel) + pad(ramdisk))


if __name__ == '__main__':
    main()
