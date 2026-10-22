#pragma once
/*
 * Per-board constants. build.sh passes -DBOARD_YUPIK for the Realme GT Master
 * (RMX3360, SM7325 "yupik"); the default is sweet (Redmi Note 10 Pro, SM7150).
 * Values come from each phone's live FDT.
 */
#if defined(BOARD_YUPIK)
#define BOARD_NAME		"yupik"
#define FB_PHYS			0xe1000000UL	/* reserved-memory cont_splash_region */
#define FB_SIZE			0x2300000UL
#define DRAM_TOP		0x280000000UL	/* 8 GB model: last bank 0x1_80000000 + 4 GiB */
#define VTIMER_INTID		27		/* DT timer PPIs 13/14/11/12: virtual = PPI 11 */
#define PTIMER_INTID		30		/* non-secure physical = PPI 14 */
/* No PS_HOLD node in the live FDT; PSCI SYSTEM_RESET was tested and works. */
#else
#define BOARD_NAME		"sweet"
#define FB_PHYS			0x9c000000UL	/* reserved-memory cont_splash_region */
#define FB_SIZE			0x1700000UL
#define DRAM_TOP		0x200000000UL
#define VTIMER_INTID		19		/* DT: <GIC_PPI 3 ...>, not the usual 27 */
#define PTIMER_INTID		18		/* DT: <GIC_PPI 2 ...> */
#define PSHOLD			0x0c264000UL
#endif

#define IMEM_RESTART_REASON	0x146aa65cUL	/* qcom,msm-imem restart_reason@65c (both) */
#define UFS_BASE		0x01d84000UL	/* ufshc@1d84000 "ufs_mem" (both) */
#define GICD_BASE		0x17a00000UL	/* both */
#define GICR_BASE		0x17a60000UL
#define GICR_SIZE		0x100000UL
