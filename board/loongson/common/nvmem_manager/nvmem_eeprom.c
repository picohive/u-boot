#include <common.h>
#include <malloc.h>
#include <dm.h>
#include <u-boot/crc.h>
#include <i2c_eeprom.h>
#include <search.h>
#include <watchdog.h>
#include <linux/ctype.h>
#include "nvmem_eeprom.h"

#define NVMEM_MAC_INFO_FRONT "LSGD"
#define NVMEM_MAC_INFO_BACK NVMEM_MAC_INFO_FRONT

#define nvmem_manager_debug(fmt, args...) printf("[%s][%s]: DEBUG: " fmt, __FILE__, __func__, ##args)
#define nvmem_manager_error(fmt, args...) printf("[%s][%s]: ERROR: " fmt, __FILE__, __func__, ##args)

static struct udevice* find_nvmem_store_dev(void)
{
	int find;
	struct udevice* dev;

	find = 0;
	dev = NULL;
	for (uclass_first_device(UCLASS_I2C_EEPROM, &dev); dev; uclass_next_device(&dev)) {
		if (ofnode_read_bool(dev->node_, "loongson,nvmem")) {
			find = 1;
			break;
		}
	}
	return find ? dev : NULL;
}

static void nvmem_mac_info_printf(unsigned char* buf)
{
	int i;
	printf("nvmem mac info:\n");
	for (i = 0; i < 16; ++i)
		printf("%.2x ", buf[i]);
	printf("\n");
}

static int nvmem_mac_info_invaild(unsigned char* buf)
{
	int ret;
	if (!buf)
		return 1;

	ret = 0;
	ret = strncmp(buf, NVMEM_MAC_INFO_FRONT, 4);
	ret = strncmp(buf + 12, NVMEM_MAC_INFO_BACK, 4);
	return ret ? 0 : 1;
}

int nvmem_get_mac(int mac_num, unsigned char* buf)
{
	// LSGDxxxxxxzzLSGD zz not care
	struct udevice *dev;
	unsigned char nvmem_buf[16];
	int nvmem_offset;
	int nvmem_size;
	if (mac_num < 0 || mac_num > 3) {
		nvmem_manager_error("param mac is invaild(%d)\n", mac_num);
		return 1;
	}
	if (!buf) {
		nvmem_manager_error("param buf is NULL");
		return 1;
	}

	nvmem_offset = mac_num * 0x10;
	nvmem_size = 16;

	dev = find_nvmem_store_dev();
	if (!dev)
		return 1;

	i2c_eeprom_read(dev, nvmem_offset, nvmem_buf, nvmem_size);
	if (!nvmem_mac_info_invaild(nvmem_buf)) {
		nvmem_manager_debug("mac-%d nvmem mac info is invaild\n", mac_num);
		nvmem_mac_info_printf(nvmem_buf);
		return 1;
	}

	memmove(buf, nvmem_buf + 4, 6);
	return 0;
}

int nvmem_set_mac(int mac_num, unsigned char* buf)
{
	int ret;
	struct udevice *dev;
	unsigned char mac_info_buf[16];
	int nvmem_offset;
	int nvmem_size;

	if (mac_num < 0 || mac_num > 3) {
		nvmem_manager_error("param mac is invaild(%d)\n", mac_num);
		return 1;
	}
	if (!buf) {
		nvmem_manager_error("param buf is NULL");
		return 1;
	}

	nvmem_offset = mac_num * 0x10;
	nvmem_size = 16;

	dev = find_nvmem_store_dev();
	if (!dev)
		return 1;

	ret = nvmem_get_mac(mac_num, mac_info_buf);
	if (!ret) {
		ret = memcmp(mac_info_buf, buf, 6);
		// 相同无需重复刷写
		if (!ret)
			return 0;
	}

	memmove(mac_info_buf, NVMEM_MAC_INFO_FRONT, 4);
	memmove(mac_info_buf + 4, buf, 6);
	mac_info_buf[10] = 0;
	mac_info_buf[11] = 0;
	memmove(mac_info_buf + 12, NVMEM_MAC_INFO_BACK, 4);
	ret = i2c_eeprom_write(dev, nvmem_offset, mac_info_buf, nvmem_size);
	if (ret) {
		nvmem_manager_error("nvmem update mac-%d mac info failed!(err: %d)\n", mac_num, ret);
		nvmem_mac_info_printf(mac_info_buf);
		return 1;
	}
	nvmem_manager_debug("nvmem update mac-%d mac info success!\n", mac_num);
	nvmem_mac_info_printf(mac_info_buf);
	return 0;
}

