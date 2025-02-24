#include <common.h>
#include <asm/global_data.h>
#include <spl.h>
#include <init.h>
#include <hang.h>
#include <mapmem.h>

DECLARE_GLOBAL_DATA_PTR;

extern void ddr_init(void);
extern ulong spl_relocate_stack_gd(void);

extern void printstr(const char* str);

__weak void spl_mach_init(void)
{
	arch_cpu_init();
}

__weak void spl_mach_init_late(void)
{
}

__weak void spl_board_init_late(void)
{
}

u32 spl_boot_device(void)
{
#if defined(CONFIG_SPL_SPI)
	return BOOT_DEVICE_SPI;
#elif defined(CONFIG_SPL_MMC)
	return BOOT_DEVICE_MMC1;
#elif defined(CONFIG_SPL_NAND_SUPPORT)
	return BOOT_DEVICE_NAND;
#else
	return BOOT_DEVICE_NONE;
#endif
}

__weak int spl_sdram_init(void)
{
	return dram_init();
}

#if defined(CONFIG_SPL_MULTI_DTB_FIT)
__weak int spl_dtb_select(void)
{
	printf("Note: Please implement %s for special machine/board!\n", __func__);
	return 0;
}
#endif

void __noreturn board_init_f(ulong dummy)
{
	int ret;
	ulong new_sp;

#ifdef DBG_ASM
	printstr("Enter board_init_f...\r\n");
#endif
	printstr("[Glog] spl_mach_init init\r\n");
	spl_mach_init();
	printstr("[Glog] spl_early_init init\r\n");
	ret = spl_early_init();
	if (ret) {
		debug("spl_early_init() failed: %d\n", ret);
#ifdef DBG_ASM
		printstr("spl_early_init() failed.\r\n");
#endif
		hang();
	}

	printstr("[Glog] preloader_console_init init\r\n");
	preloader_console_init();

#if defined(CONFIG_SPL_MULTI_DTB_FIT)
	printstr("[Glog] spl_dtb_select init\r\n");
	if (spl_dtb_select()) {
		printf("switch dtb failed\n");
		hang();
	}
#endif

	printstr("[Glog] spl_board_init_late init\r\n");
	spl_board_init_late();

	printstr("[Glog] spl sdram init\r\n");
	if (spl_sdram_init()) {
		printf("spl sdram init failed\n");
		hang();
	}

	printstr("[Glog] spl relocate stack gd\r\n");
	// change the sp, gd and malloc to sdram space
	new_sp = spl_relocate_stack_gd();
	if (new_sp > 0) {
		// set new sp addr.
		asm volatile (
			"ori	$sp, %0, 0\n"
			: /* no outputs */
			: "r"(new_sp));
	}

	printstr("[Glog] spl mach init late\r\n");
	spl_mach_init_late();
	printf("Jump to board_init_r.\n");
	board_init_r(NULL, 0);
}
