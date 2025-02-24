/*
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <asm/io.h>
#include <mach/loongson.h>

void _machine_restart(void)
{
#ifdef CONFIG_LS_ACPI_NOT_CONFIG
	writel(0x40, LS_WD_TIMER_REG);
	writel(0x1, LS_WD_SET_REG);
	writel(0x2, LS_RST_CNT_REG);
	writel(0x1, LS_WD_SET_REG);
	while (1) ;
#else
	writel(0x1, LS_RST_CNT_REG);
	while (1) /* NOP*/ ;
#endif
}

int do_poweroff(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
#ifdef CONFIG_LS_ACPI_NOT_CONFIG
	printf("current not config ACPI! cant poweroff!\n");
	printf("please shutdown poweroff\n");
	while (1);
#endif
#ifndef NOT_USE_ACPI
	writel(0xffffffff, LS_PM1_STS_REG);
	writel(0x1c00, LS_PM1_CNT_REG);
	writel(0x3c00, LS_PM1_CNT_REG);
#endif
	while (1) /* NOP*/ ;
	return 0;
}

void _machine_poweroff(void)
{
	do_poweroff(NULL, 0, 0, NULL);
}
