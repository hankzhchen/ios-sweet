/* Host test for loader.c's payload search and unaligned copy (v3 ramdisk layout). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
void con_hex2(const char *l, const char *a, uint32_t va, const char *b, uint32_t vb) { (void)l; (void)a; (void)va; (void)b; (void)vb; }
void con_hex(const char *l, uint64_t v) { (void)l; (void)v; }
void con_str(const char *l, const char *s) { (void)l; (void)s; }
static uint64_t t_rd_lo, t_rd_hi;
int fdt_chosen_u64(uintptr_t d, const char *n, uint64_t *v)
{
	(void)d;
	*v = n[13] == 's' ? t_rd_lo : t_rd_hi;	/* linux,initrd-start / -end */
	return 1;
}
#include "loader.c"

int main(void)
{
	int fails = 0;
	for (int off = 0; off < 64; off++) {
		size_t vend = 4096 * 3 + off, plen = 4096;
		uint8_t *rd = aligned_alloc(4096, (vend + plen + 64 + 4095) / 4096 * 4096);
		for (size_t i = 0; i < vend; i++) rd[i] = (uint8_t)(rand() | 0x01) == 'S' ? 'S' : (uint8_t)rand();
		memcpy(rd + 100, "SWEETXN", 7);		/* near-miss decoy */
		uint8_t *p = rd + vend;
		for (size_t i = 0; i < plen; i++) p[i] = (uint8_t)(i * 7 + 3);
		memcpy(p, "SWEETXNU", 8);
		uintptr_t lo = (uintptr_t)rd, hi = (uintptr_t)(rd + vend + plen);
		uintptr_t got = find_payload(lo, hi);
		uint64_t dst[600];
		copy_any((uintptr_t)dst, (uintptr_t)p + 8, 4000);
		int ok = got == (uintptr_t)p && memcmp(dst, p + 8, 4000) == 0;
		/* v2 layout: payload at the very start */
		ok &= find_payload((uintptr_t)p, hi) == (uintptr_t)p;
		if (!ok) { printf("FAIL off=%d got=%+ld\n", off, (long)(got - (uintptr_t)p)); fails++; }
		free(rd);
	}
	/* unaligned copies of every length 1..40 from exact-size heap buffers (ASan: no over-read) */
	for (int off = 1; off < 8; off++)
		for (uint64_t n = 1; n <= 40; n++) {
			/* exactly up to the end of the aligned word holding the last byte */
			uint8_t *src = aligned_alloc(8, (off + n + 7) / 8 * 8);
			for (uint64_t i = 0; i < off + n; i++) src[i] = (uint8_t)(i * 13 + 1);
			uint64_t dst[6] = {0};
			copy_any((uintptr_t)dst, (uintptr_t)src + off, n);
			if (memcmp(dst, src + off, n)) { printf("FAIL copy off=%d n=%llu\n", off, (unsigned long long)n); fails++; }
			free(src);
		}

	/* header that points past rd_end must be rejected before any copy */
	static const struct { const char *want; int seg_oob, adt_oob, truncate; } cases[] = {
		{ "payload segment out of bounds", 1, 0, 0 },
		{ "payload ADT out of bounds", 0, 1, 0 },
		{ "payload truncated", 0, 0, 1 },
	};
	for (unsigned c = 0; c < 3; c++) {
		size_t len = sizeof(struct payload) + sizeof(struct payload_seg) + 64;
		uint8_t *buf = calloc(1, len + 3), *pl = buf + 3;	/* misaligned, like v3 */
		struct payload h = { .version = 1, .nsegs = 1, .adt_off = 0x2c8, .adt_size = cases[c].adt_oob ? 4096 : 8 };
		memcpy(h.magic, "SWEETXNU", 8);
		struct payload_seg s = { .vmaddr = 0, .vmsize = 8, .file_off = 0x2c8, .file_size = cases[c].seg_oob ? 1u << 20 : 8 };
		memcpy(pl, &h, sizeof h);
		memcpy(pl + sizeof h, &s, sizeof s);
		t_rd_lo = (uintptr_t)buf;
		t_rd_hi = (uintptr_t)pl + (cases[c].truncate ? 100 : len);
		const char *why = loader_boot(0);
		if (strcmp(why, cases[c].want)) { printf("FAIL bounds case %u: %s\n", c, why); fails++; }
		free(buf);
	}

	uint8_t none[256] = {0};
	fails += find_payload((uintptr_t)none, (uintptr_t)none + sizeof none) != 0;
	printf("%s (64 offsets, unaligned copy lengths, bounds checks, no-payload case)\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
