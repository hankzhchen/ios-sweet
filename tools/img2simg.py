#!/usr/bin/env python3
"""Convert a raw image to an Android sparse image (for fastboot flash).

  img2simg.py RAW.img OUT.simg

All-zero blocks become FILL chunks with value 0 (not DONT_CARE), so whatever
the partition held before reads back as zeros, exactly like the raw image.
Holes in a sparse source file are skipped with SEEK_DATA/SEEK_HOLE.
"""
import os
import struct
import sys

BLK = 4096
MAX_RAW = 16384  # blocks per RAW chunk (64 MiB)
RAW, FILL = 0xCAC1, 0xCAC2


def main(src, dst):
    size = os.path.getsize(src)
    if size % BLK:
        raise SystemExit(f'{src}: size {size} is not a multiple of {BLK}')
    total = size // BLK
    zero = bytes(BLK)
    chunks = 0
    with open(src, 'rb') as f, open(dst, 'wb') as o:
        o.write(bytes(28))  # header, rewritten at the end
        run_raw, run_zero = [], 0

        def flush():
            nonlocal run_raw, run_zero, chunks
            if run_raw:
                data = b''.join(run_raw)
                o.write(struct.pack('<HHII', RAW, 0, len(run_raw), 12 + len(data)))
                o.write(data)
                chunks += 1
                run_raw = []
            if run_zero:
                o.write(struct.pack('<HHIII', FILL, 0, run_zero, 16, 0))
                chunks += 1
                run_zero = 0

        fd = f.fileno()
        blk = 0
        while blk < total:
            off = blk * BLK
            try:
                data_off = os.lseek(fd, off, os.SEEK_DATA)
            except OSError:  # no more data: the rest is a hole
                data_off = size
            if data_off > off:  # hole -> zero blocks
                n = (min(data_off, size) - off) // BLK
                if run_raw:
                    flush()
                run_zero += n
                blk += n
                continue
            f.seek(off)
            b = f.read(BLK)
            if b == zero:
                if run_raw:
                    flush()
                run_zero += 1
            else:
                if run_zero:
                    flush()
                run_raw.append(b)
                if len(run_raw) == MAX_RAW:
                    flush()
            blk += 1
            if blk % 262144 == 0:
                print(f'{blk * BLK >> 30} GiB / {size >> 30} GiB', file=sys.stderr)
        flush()
        o.seek(0)
        o.write(struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, total, chunks, 0))
    print(f'{dst}: {total} blocks, {chunks} chunks, {os.path.getsize(dst)} bytes', file=sys.stderr)


if __name__ == '__main__':
    main(*sys.argv[1:3])
