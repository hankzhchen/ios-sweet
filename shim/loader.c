/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 *
 * Boot image v2 (sweet): the payload is the whole ramdisk. Boot image v3
 * (yupik): ABL loads the device's vendor_boot ramdisk first and ours right
 * behind it, so the payload sits at an arbitrary, possibly unaligned, offset
 * and has to be searched for and read bytewise.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL
#define MAX_SEGS		64

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static uint8_t rd8(uintptr_t a)
{
	return *(volatile const uint8_t *)a;
}

/*
 * copy64 for a source of any alignment (dst stays 8-byte aligned): aligned
 * word loads merged with shifts, never reading past the word holding the
 * last source byte. Device memory, so no unaligned loads.
 */
static void copy_any(uintptr_t dst, uintptr_t src, uint64_t n)
{
	if (!(src & 7)) {
		copy64(dst, src, n);
		return;
	}
	if (!n)
		return;
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)(src & ~7UL);
	unsigned sh = (src & 7) * 8;
	uint64_t last = (src + n - 1) & ~7UL;
	uint64_t lo = s[0];
	for (uint64_t i = 0; i < (n + 7) / 8; i++) {
		uint64_t hi = (uintptr_t)&s[i + 1] <= last ? s[i + 1] : 0;
		d[i] = (lo >> sh) | (hi << (64 - sh));
		lo = hi;
	}
}

static int magic_ok(uintptr_t m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (rd8(m + i) != (uint8_t)want[i])
			return 0;
	return 1;
}

/* First "SWEETXNU" in [lo, hi), any alignment; 0 if none. Reads aligned words. */
static uintptr_t find_payload(uintptr_t lo, uintptr_t hi)
{
	if (hi - lo < 8)
		return 0;
	if (magic_ok(lo))
		return lo;
	for (uintptr_t w = lo & ~7UL; w + 8 <= hi; w += 8) {
		uint64_t v = *(volatile const uint64_t *)w;
		for (int b = 0; b < 8; b++)
			if (((v >> (8 * b)) & 0xff) == 'S' && w + b >= lo && w + b + 8 <= hi &&
			    magic_ok(w + b))
				return w + b;
	}
	return 0;
}

/* Aligned copy of the payload header and segment table. */
static uint64_t hdr_buf[(sizeof(struct payload) + MAX_SEGS * sizeof(struct payload_seg) + 7) / 8];

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);

	uintptr_t base = find_payload(rd_start, rd_end);
	if (!base)
		return "no SWEETXNU payload";
	con_hex("PAYLOAD", base);
	con_hex("PAYLOFF", base - rd_start);

	/* Everything the header points at must lie inside [base, rd_end). */
	uint64_t avail = rd_end - base;
	const struct payload *p = (const struct payload *)hdr_buf;
	if (avail < sizeof(struct payload))
		return "payload truncated";
	copy_any((uintptr_t)hdr_buf, base, sizeof(struct payload));
	if (p->version != 1 || (p->adt_off & 7) || p->nsegs > MAX_SEGS)
		return "bad payload header";
	if (avail - sizeof(struct payload) < p->nsegs * sizeof(struct payload_seg))
		return "payload truncated";
	copy_any((uintptr_t)p->segs, base + sizeof(struct payload),
		 p->nsegs * sizeof(struct payload_seg));
	for (uint32_t i = 0; i < p->nsegs; i++)
		if (p->segs[i].file_off > avail || p->segs[i].file_size > avail - p->segs[i].file_off)
			return "payload segment out of bounds";
	if (p->adt_off > avail || p->adt_size > avail - p->adt_off)
		return "payload ADT out of bounds";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", p->phys_base, "size=", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes (the vendor ramdisk before it may). */
	if (rd_end > p->phys_base && base < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy_any(dst, base + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy_any(adt_pa, base + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
