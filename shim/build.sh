#!/bin/bash
# Build the shim and wrap it in a boot image.
#   BOARD=sweet (default): boot.img v2 that mirrors the stock PixelOS header
#   BOARD=yupik: boot.img v3 (Realme GT Master); dtb and load addresses come
#                from the phone's vendor_boot
# CROSS overrides the toolchain prefix (aarch64-linux-gnu- or Homebrew's aarch64-elf-).
set -euo pipefail
cd "$(dirname "$0")"
BOARD=${BOARD:-sweet}
if [ -z "${CROSS:-}" ]; then
	if command -v aarch64-linux-gnu-gcc >/dev/null; then CROSS=aarch64-linux-gnu-; else CROSS=aarch64-elf-; fi
fi
CC=${CROSS}gcc
OBJCOPY=${CROSS}objcopy
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
case "$BOARD" in
sweet) ;;
yupik) CFLAGS="$CFLAGS -DBOARD_YUPIK" ;;
*) echo "error: unknown BOARD=$BOARD" >&2; exit 1 ;;
esac
mkdir -p out
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$(${CROSS}readelf -r out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	out/start.o out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o -o out/shim.elf
$OBJCOPY -O binary out/shim.elf out/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$(( 0x$(${CROSS}nm out/shim.elf | awk '$3=="_end"{print $1}') ))
python3 -c 'import os, sys; os.truncate(sys.argv[1], int(sys.argv[2]))' out/Image "$size"
# Ramdisk: an xnu payload (tools/mkpayload.py) if given as $1, else an empty
# newc cpio (ABL dislikes a zero-size ramdisk on some builds) -> diagnostics.
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)
fi
if [ "$BOARD" = yupik ]; then
	python3 ../tools/mkbootimg_v3.py --kernel out/Image --ramdisk out/ramdisk.bin \
		--cmdline "console=null" -o out/shim-boot.img
else
	mkbootimg --header_version 2 --kernel out/Image --ramdisk out/ramdisk.bin \
		--dtb ../dtb/boot_unpacked/dtb --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
		--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
		--os_version 17.0.0 --os_patch_level 2035-06 \
		--cmdline "console=null" -o out/shim-boot.img
fi
# Host render of the console layout.
cc -O1 -Wall -o out/host_test console.c host_test.c && ./out/host_test
ls -l out/Image out/shim-boot.img
