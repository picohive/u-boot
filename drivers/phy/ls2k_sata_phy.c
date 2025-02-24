// SPDX-License-Identifier: GPL-2.0+
/*
 * Loongson 2k sata PHY CONFIG driver
 *
 * Copyright (C) 2025 qujintao <qujintao@loongson.cn>
 * Copyright (C) 2025 LSGD
 *
 * Author: qujintao <qujintao@loongson.cn>
 */

#include <common.h>
#include <dm.h>
#include <generic-phy.h>
#include <reset.h>
#include <linux/io.h>
#include <linux/bitops.h>

/*
 * 本质上就是 对寄存器进行内容填充
 * 谨记 需要主动调用 phy_ls2k_sata_init(phy.h文件里面) 函数 才会自动加载这个驱动
 */

struct prop_info {
	const char* prop_name;
	unsigned int shift;
	unsigned int mask;
};

static struct prop_info sata_prop_info_set[] = {
	{.prop_name = "gen3_pe", .shift = 57, .mask = 0x3f},
	{.prop_name = "gen2_pe", .shift = 51, .mask = 0x3f},
	{.prop_name = "gen1_pe", .shift = 45, .mask = 0x3f},
	{.prop_name = "gen3_amp", .shift = 38, .mask = 0x7f},
	{.prop_name = "gen2_amp", .shift = 31, .mask = 0x7f},
	{.prop_name = "gen1_amp", .shift = 24, .mask = 0x7f},
	{.prop_name = "pre_rw", .shift = 23, .mask = 0x1},
	{.prop_name = "r_a_w", .shift = 22, .mask = 0x1},
	{.prop_name = "disable_WAW", .shift = 21, .mask = 0x1},
	{.prop_name = "disable_WAR", .shift = 20, .mask = 0x1},
	{.prop_name = "idle_w_period", .shift = 16, .mask = 0xf},
	{.prop_name = "rx_equalizer", .shift = 12, .mask = 0x7},
	{.prop_name = NULL, .shift = 0, .mask = 0},
};

#define SATA_PHY_CLOSE BIT(31)

struct ls2k_sata_phy_priv {
	unsigned long long sata_phy_addr;
	struct reset_ctl_bulk resets;
};

static int phy_ls2k_sata_power_on(struct phy *phy)
{
	struct udevice *dev = phy->dev;
	struct ls2k_sata_phy_priv *priv = dev_get_priv(dev);
	uint val;

	val = readl(priv->sata_phy_addr + 4);
	val |= SATA_PHY_CLOSE;
	val ^= SATA_PHY_CLOSE;
	writel(val, priv->sata_phy_addr + 4);

	return 0;
}

static int phy_ls2k_sata_power_off(struct phy *phy)
{
	struct udevice *dev = phy->dev;
	struct ls2k_sata_phy_priv *priv = dev_get_priv(dev);
	uint val;

	val = readl(priv->sata_phy_addr + 4);
	val |= SATA_PHY_CLOSE;
	writel(val, priv->sata_phy_addr + 4);

	return 0;
}

static struct phy_ops ls2k_sata_phy_ops = {
	.power_on = phy_ls2k_sata_power_on,
	.power_off = phy_ls2k_sata_power_off,
};

static int read_param_and_set(struct udevice *dev, struct ls2k_sata_phy_priv *priv, struct prop_info* info)
{
	int ret;
	unsigned long long addr;
	unsigned int reg_value;
	unsigned int new_value;
	unsigned int shift;
	unsigned int mask;
	if (unlikely(!dev || !priv || !info))
		return -1;

	ret = dev_read_u32u(dev, info->prop_name, &new_value);
	if (ret)
		return -2;

	mask = info->mask;
	new_value &= mask;

	shift = info->shift;
	addr = priv->sata_phy_addr;
	if (shift >= 32) {
		addr += 4;
		shift -= 32;
	}

	reg_value = readl(addr);
	reg_value |= (mask << shift);
	reg_value ^= (mask << shift);
	reg_value |= (new_value << shift);
	writel(reg_value, addr);

	return 0;
}

static int ls2k_sata_phy_probe(struct udevice *dev)
{
	struct ls2k_sata_phy_priv *priv = dev_get_priv(dev);
	struct prop_info* cur_prop_info;
	int i;

	priv->sata_phy_addr = (unsigned long long)dev_read_addr_ptr(dev);

	for (i = 0; ;++i) {
		cur_prop_info = sata_prop_info_set + i;
		if (!cur_prop_info->prop_name)
			break;
		read_param_and_set(dev, priv, cur_prop_info);
	}

	return 0;
}

static int ls2k_sata_phy_remove(struct udevice *dev)
{
	struct ls2k_sata_phy_priv *priv = dev_get_priv(dev);

	return reset_release_bulk(&priv->resets);
}

static const struct udevice_id ls2k_sata_phy_ids[] = {
	{ .compatible = "ls2k-sata-phy" },
	{ }
};

U_BOOT_DRIVER(ls2k_sata_phy) = {
	.name = "ls2k_sata_phy",
	.id = UCLASS_PHY,
	.of_match = ls2k_sata_phy_ids,
	.probe = ls2k_sata_phy_probe,
	.remove = ls2k_sata_phy_remove,
	.ops = &ls2k_sata_phy_ops,
	.priv_auto = sizeof(struct ls2k_sata_phy_priv),
};

int phy_ls2k_sata_init(void)
{
	struct udevice *dev;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_PHY, &uc);
	if (ret)
		return ret;
	for (uclass_first_device(UCLASS_PHY, &dev);
		dev;
		uclass_next_device(&dev)) {
	}

	return 0;
}

