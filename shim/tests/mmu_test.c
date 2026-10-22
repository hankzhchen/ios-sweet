#include <stdio.h>
#include <stdint.h>
static uint64_t rsr_stub(void) { return 0; }
#define rsr(r) rsr_stub()
#define wsr(r, v) ((void)(v))
static uintptr_t fake_head, fake_end;
#define _head ((char *)fake_head)
#define _end ((char *)fake_end)
#include "out/mmu_host.c"	/* mmu.c minus sysreg/asm, made by run.sh */

static uint64_t walk(uint64_t pa, int *lvl)
{
	uint64_t *l2 = (uint64_t *)((uintptr_t)pt_pool + PG_SIZE - 1 & ~(PG_SIZE - 1));
	uint64_t e = l2[pa >> BLK_SHIFT];
	*lvl = 2;
	if ((e & 3) != D_TABLE)
		return e;
	*lvl = 3;
	return ((uint64_t *)(e & D_ADDR))[(pa >> PG_SHIFT) & (NENT - 1)];
}

static int check(const char *what, uint64_t pa, int attr, int valid)
{
	int lvl; uint64_t e = walk(pa, &lvl);
	int ok = valid ? ((e & 1) && ((e >> 2) & 7) == (uint64_t)attr && (e & 0x0000ffffffffc000UL) == (pa & ~((lvl == 2 ? BLK_SIZE : PG_SIZE) - 1))) : !(e & 1);
	printf("%-4s %-30s pa=%#011llx L%d attr=%llu\n", ok ? "ok" : "FAIL", what, (unsigned long long)pa, lvl, (unsigned long long)((e >> 2) & 7));
	return !ok;
}

int main(void)
{
	struct mmu_info mi;
	fake_head = 0xa8000000; fake_end = 0xa8000000 + 217088;	/* ABL-style load address */
	int rc = mmu_enable(&mi), f = rc != 0;
	printf("rc=%d n_l3=%d img=%#llx-%#llx\n", rc, mi.n_l3, (unsigned long long)mi.img_lo, (unsigned long long)mi.img_hi);
	f += check("image first page (WB)", 0xa8000000, ATTR_WB, 1);
	f += check("image last page (WB)", mi.img_hi - PG_SIZE, ATTR_WB, 1);
	f += check("after image (Device)", mi.img_hi, ATTR_DEV, 1);
	f += check("splash first page (NC)", FB_PHYS, ATTR_NC, 1);
	f += check("splash across 32MB edge (NC)", 0xe2000000, ATTR_NC, 1);
	f += check("splash last page (NC)", FB_PHYS + FB_SIZE - PG_SIZE, ATTR_NC, 1);
	f += check("dfps_data_memory (Device)", 0xe3300000, ATTR_DEV, 1);
	f += check("below splash (Device)", FB_PHYS - PG_SIZE, ATTR_DEV, 1);
	f += check("GICD (Device)", GICD_BASE, ATTR_DEV, 1);
	f += check("UFS (Device)", UFS_BASE, ATTR_DEV, 1);
	f += check("high bank start (Device)", 0x180000000UL, ATTR_DEV, 1);
	f += check("high bank end (Device)", DRAM_TOP - BLK_SIZE, ATTR_DEV, 1);
	f += check("past DRAM_TOP (unmapped)", DRAM_TOP, 0, 0);
	/* image straddling a block edge too -> 4 L3 tables */
	fake_head = 0xa9ff0000; fake_end = fake_head + 217088;
	rc = mmu_enable(&mi);
	printf("straddling image: rc=%d n_l3=%d\n", rc, mi.n_l3);
	f += rc != 0 || mi.n_l3 != 4;
	f += check("straddling image tail (WB)", 0xaa000000, ATTR_WB, 1);
	printf("%s\n", f ? "FAILED" : "all passed");
	return f != 0;
}
