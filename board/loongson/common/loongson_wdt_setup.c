#include <common.h>
#include <command.h>
#include <dm.h>
#include <wdt.h>
#include <stdio.h>
#include "loongson_wdt_setup.h"

#ifdef CONFIG_WDT
static struct udevice *currdev = NULL;

static int init_wdt_dev(void)
{
	int ret;

	if (currdev)
		return 0;

	ret = uclass_get_device_by_name(UCLASS_WDT, LS_WDT_NODE_NAME, &currdev);
	if (ret) {
		printf("%s: Can't get the watchdog timer: %s\n", __func__, LS_WDT_NODE_NAME);
		currdev = NULL;
		return -1;
	}

	return 0;
}

int ls_wdt_start(int ms)
{
	int ret;
	ret = init_wdt_dev();
	if (ret)
		return -1;

	ret = wdt_start(currdev, ms, 0);
	if (ret == -ENOSYS) {
		printf("%s Starting watchdog timer not supported.\n", __func__);
		return -1;
	} else if (ret) {
		printf("%s Starting watchdog timer failed (%d)\n", __func__, ret);
		return -1;
	}

	return 0;
}

int ls_wdt_stop(void)
{
	int ret;
	ret = init_wdt_dev();
	if (ret)
		return -1;

	ret = wdt_stop(currdev);
	if (ret == -ENOSYS) {
		printf("%s Stopping watchdog timer not supported.\n", __func__);
		return -1;
	} else if (ret) {
		printf("%s Stopping watchdog timer failed (%d)\n", __func__, ret);
		return -1;
	}

	return 0;
}

int ls_wdt_restart(void)
{
	int ret;
	ret = init_wdt_dev();
	if (ret)
		return -1;

	ret = wdt_reset(currdev);
	if (ret == -ENOSYS) {
		printf("%s Resetting watchdog timer not supported.\n", __func__);
		return -1;
	} else if (ret) {
		printf("%s Resetting watchdog timer failed (%d)\n", __func__, ret);
		return -1;
	}

	return 0;
}
#else
int ls_wdt_start(int ms) {return 0;}
int ls_wdt_stop(void) {return 0;}
int ls_wdt_restart(void) {return 0;}
#endif

static int do_loongson_watchdog_daemon(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
	int num;
	int restart;
	if(argc != 2)
		return -1;

	restart = 0;
	if (argv[1][0] == 'p' && !argv[1][1]) {
		restart = 1;
		printf("restart watchdog daemon\n");
	} else {
		num = simple_strtoul(argv[1], NULL, 10);
		if (argv[1][0] == '0' && !argv[1][1] && !num) {
			printf("stop watchdog daemon\n");
			ls_wdt_stop();
			return 0;
		} else if (num > 0) {
			printf("start watchdog daemon [maybe timeout: %d ms]\n", num);
		} else {
			printf("ls_wdt_d Error: param invaild [param: %s]\n", argv[1]);
			return -1;
		}
	}

	if (restart) {
		ls_wdt_restart();
	} else {
		ls_wdt_start(num);
	}

	return 0;
}

U_BOOT_CMD(
	ls_wdt_d,    2,    0,     do_loongson_watchdog_daemon,
	"init uboot env after reboot. [param: timeout ms]\r\n"
	"ls_wdt_d 0 [mean stop watchdog]\r\n"
	"ls_wdt_d p [mean restart watchdog]",
	""
);

