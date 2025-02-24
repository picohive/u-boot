/*
 * Loongson spi driver
 *
 * based on bfin_spi.c
 * Copyright (c) 2005-2008 Analog Devices Inc.
 * Copyright (C) 2014 Tang Haifeng <tanghaifeng-gz@loongson.cn>
 * Copyright (C) 2022 Jianhui Wang <wangjianhui@loongson.cn>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <asm/io.h>
#include <malloc.h>
#include <spi.h>
#include <spi-mem.h>
#include <clk.h>
#include <asm/gpio.h>

#define LS_SPI_IDLE_VAL 0xff

#define LS_SPSR_SPIF		(1 << 7)
#define LS_SPSR_WCOL		(1 << 6)
#define LS_SPSR_WFFULL		(1 << 3)
#define LS_SPSR_RFEMPTY		(1 << 0)

#ifdef CONFIG_DM_SPI

struct ls_spi_regs {
	unsigned char spcr;
	unsigned char spsr;
	unsigned char fifo;	/* TX/Rx data reg */
	unsigned char sper;
	unsigned char param;
	unsigned char softcs;
	unsigned char timing;
};

struct ls_spi_platdata {
	struct ls_spi_regs *regs;
	struct clk sclk;
	uint mode;
	uint div;
	uint flg;
	/* Optional GPIO chip-selects, indexed by CS number. A GPIO CS is
	 * used instead of the controller's soft-CS when valid (see cs-gpios). */
	struct gpio_desc cs_gpio[4];
};

static __always_inline void ls_spi_wait_rx_ready(struct ls_spi_regs *regs)
{
	int timeout = 20000;
	u8 stat;

	stat = readb(&regs->spsr);
	if (stat & LS_SPSR_SPIF)
		stat |= LS_SPSR_SPIF;	/* Int Clear */
	if (stat & LS_SPSR_WCOL)
		stat |= LS_SPSR_WCOL;	/* Write-Collision Clear */
	writeb(stat, &regs->spsr);

	while (timeout) {
		if (!(readb(&regs->spsr) & LS_SPSR_RFEMPTY))
			break;
		timeout--;
	}
}

static __always_inline void ls_spi_wait_tx_ready(struct ls_spi_regs *regs)
{
	int timeout = 20000;
	u8 stat;

	stat = readb(&regs->spsr);
	if (stat & LS_SPSR_SPIF)
		stat |= LS_SPSR_SPIF;	/* Int Clear */
	if (stat & LS_SPSR_WCOL)
		stat |= LS_SPSR_WCOL;	/* Write-Collision Clear */
	writeb(stat, &regs->spsr);

	while (timeout) {
		if (!(readb(&regs->spsr) & LS_SPSR_WFFULL))
			break;
		timeout--;
	}
}

static void ls_spi_cs_activate(struct udevice *dev)
{
	struct udevice *bus = dev_get_parent(dev);
	struct ls_spi_platdata *plat = dev_get_plat(bus);
	struct dm_spi_slave_plat *slave_plat = dev_get_parent_plat(dev);
	struct ls_spi_regs *const regs = plat->regs;
	u32 cs = slave_plat->cs;
	u8 ret;

	// disable spi-flash memory
	writeb(readb(&regs->param) & 0xfe, &regs->param);

	/* GPIO-based chip select (software simulated, e.g. SPI NAND).
	 * Only available when DM_GPIO is enabled (SPL has no GPIO CS).
	 * We call the GPIO driver's set_value() directly with raw physical
	 * values because dm_gpio_set_value() relies on the ACTIVE_LOW flag
	 * being correctly set in gpio_desc->flags, which is not guaranteed
	 * across all gpio_request_by_name() paths.  CS is active-LOW:
	 *   raw 0 = pin LOW  = asserted   raw 1 = pin HIGH = deasserted
	 */
#if CONFIG_IS_ENABLED(DM_GPIO)
	if (cs < ARRAY_SIZE(plat->cs_gpio) &&
	    dm_gpio_is_valid(&plat->cs_gpio[cs])) {
		struct gpio_desc *gdesc = &plat->cs_gpio[cs];

		gpio_get_ops(gdesc->dev)->set_value(gdesc->dev,
						     gdesc->offset, 0);
		return;
	}
#endif

	if (plat->flg) {
		writeb(0x0f, &regs->softcs);
		ret = readb(&regs->softcs);
		ret |= 0x10 << cs;
		writeb(ret, &regs->softcs);
	} else {
		writeb(0xff, &regs->softcs);
		ret = readb(&regs->softcs);
		ret &= ~(0x10 << cs);
		writeb(ret, &regs->softcs);
	}

}

static void ls_spi_cs_deactivate(struct udevice *dev)
{
	struct udevice *bus = dev_get_parent(dev);
	struct ls_spi_platdata *plat = dev_get_plat(bus);
	struct ls_spi_regs *const regs = plat->regs;

	/* GPIO-based chip select (raw values, see cs_activate) */
#if CONFIG_IS_ENABLED(DM_GPIO)
	{
		struct dm_spi_slave_plat *slave_plat = dev_get_parent_plat(dev);
		u32 cs = slave_plat->cs;

		if (cs < ARRAY_SIZE(plat->cs_gpio) &&
		    dm_gpio_is_valid(&plat->cs_gpio[cs])) {
			struct gpio_desc *gdesc = &plat->cs_gpio[cs];

			/* deassert: raw 1 = pin HIGH */
			gpio_get_ops(gdesc->dev)->set_value(gdesc->dev,
							     gdesc->offset, 1);
			// restore spi-flash memory
			writeb(readb(&regs->param) | 0x1, &regs->param);
			return;
		}
	}
#endif

	if (plat->flg) {
		writeb(0x0f, &regs->softcs);
	} else {
		writeb(0xff, &regs->softcs);
	}

	// restore spi-flash memory
	writeb(readb(&regs->param) | 0x1, &regs->param);
}

static int ls_spi_claim_bus(struct udevice *dev)
{
	struct udevice *bus = dev->parent;
	struct ls_spi_platdata *plat = dev_get_plat(bus);
	struct ls_spi_regs *const regs = plat->regs;
	u8 ret;

	ret = readb(&regs->spcr);
	ret = ret & 0xf0;
	ret = ret | (plat->mode << 2) | (plat->div & 0x03);
	writeb(ret, &regs->spcr);

	ret = readb(&regs->sper);
	ret = ret & 0xfc;
	ret = ret | (plat->div >> 2);
	writeb(ret, &regs->sper);

	return 0;
}

static int ls_spi_release_bus(struct udevice *dev)
{
	return 0;
}

static int ls_spi_set_speed(struct udevice *bus, uint hz)
{
	struct ls_spi_platdata *plat = dev_get_plat(bus);

	unsigned int div, div_tmp, bit;
	unsigned long clk;

	clk = plat->sclk.rate;
	div = DIV_ROUND_UP(clk, hz);

	if (div < 2)
		div = 2;

	if (div > 4096)
		div = 4096;

	bit = fls(div) - 1;
	switch(1 << bit) {
		case 16:
			div_tmp = 2;
			if (div > (1<<bit)) {
				div_tmp++;
			}
			break;
		case 32:
			div_tmp = 3;
			if (div > (1<<bit)) {
				div_tmp += 2;
			}
			break;
		case 8:
			div_tmp = 4;
			if (div > (1<<bit)) {
				div_tmp -= 2;
			}
			break;
		default:
			div_tmp = bit - 1;
			if (div > (1<<bit)) {
				div_tmp++;
			}
			break;
	}
	debug("clk = %ld hz = %d div_tmp = %d bit = %d\n", clk, hz, div_tmp, bit);

	plat->div = div_tmp;

	return 0;
}

static int ls_spi_set_mode(struct udevice *bus, uint mode)
{
	struct ls_spi_platdata *plat = dev_get_plat(bus);

	plat->mode = mode;
	plat->flg = mode & SPI_CS_HIGH ? 1 : 0;

	return 0;
}

static int ls_spi_xfer(struct udevice *dev, unsigned int bitlen, const void *dout,
	     void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct ls_spi_platdata *plat = dev_get_plat(bus);
	struct ls_spi_regs *const regs = plat->regs;

	u8 *txp = (u8 *)dout;
	u8 *rxp = din;
	uint bytes = bitlen / 8;
	uint i;

	//	debug("%s: bus:%i cs:%i bitlen:%i bytes:%i flags:%lx\n", __func__,
	//		slave->bus, slave->cs, bitlen, bytes, flags);
	if (bitlen == 0)
		goto done;

	/* assume to do 8 bits transfers */
	if (bitlen % 8) {
		flags |= SPI_XFER_END;
		goto done;
	}

	if (flags & SPI_XFER_BEGIN)
		ls_spi_cs_activate(dev);

	for (i = 0; i < bytes; ++i) {
		ls_spi_wait_tx_ready(regs);
		if (txp) 
			writeb(*txp++, &regs->fifo);
		else 
			writeb(LS_SPI_IDLE_VAL, &regs->fifo);
		ls_spi_wait_rx_ready(regs);
		if (rxp)
			*rxp++ = readb(&regs->fifo);
		else
			readb(&regs->fifo);
	}

 done:
	if (flags & SPI_XFER_END)
		ls_spi_cs_deactivate(dev);

	return 0;
}

static int ls_spi_probe(struct udevice *bus)
{
	struct ls_spi_platdata *plat = dev_get_plat(bus);
	struct ls_spi_regs *regs;

	regs = plat->regs;

	/* 使能SPI控制器，master模式，关闭中断 */
	writeb(0x53, &regs->spcr);
	/* 清空状态寄存器 */
	writeb(0xc0, &regs->spsr);
	/* 1字节产生中断，采样(读)与发送(写)时机同时 */
	writeb(0x03, &regs->sper);
	writeb(0xff, &regs->softcs);

	/* SPI flash时序控制寄存器 */
	writeb(0x05, &regs->timing);

	return 0;
}

static int ls_spi_ofdata_to_platdata(struct udevice *bus)
{
	struct ls_spi_platdata *plat = dev_get_plat(bus);
	int res;

	plat->regs = ioremap(dev_read_addr(bus),
					sizeof(struct ls_spi_regs));

	res = clk_get_by_name(bus, "sclk", &plat->sclk);
	if (res)
		plat->sclk.rate = 100000000;
	else
		plat->sclk.rate = clk_get_rate(&plat->sclk);

	/* Optional GPIO chip selects (software simulated CS lines).
	 * Guarded by DM_GPIO: SPL has no GPIO support and only drives the
	 * controller's soft-CS for SPI NOR. */
#if CONFIG_IS_ENABLED(DM_GPIO)
	{
		int i;

		for (i = 0; i < ARRAY_SIZE(plat->cs_gpio); i++) {
			res = gpio_request_by_name(bus, "cs-gpios", i,
						    &plat->cs_gpio[i], GPIOD_IS_OUT);
			if (res == -ENOENT)
				break;
			if (res)
				break;
		}
	}
#endif

	return 0;
}

static int ls_spi_mem_xfer(struct udevice *dev, const u8 *tx, u8 *rx,
			    uint nbytes, bool first, bool last)
{
	unsigned long flags = 0;

	if (first)
		flags |= SPI_XFER_BEGIN;
	if (last)
		flags |= SPI_XFER_END;

	return ls_spi_xfer(dev, nbytes * 8, tx, rx, flags);
}

/*
 * The Loongson SPI controller is a simple single-bit FIFO master, so we only
 * support 1-bit SPI memory operations. A buswidth of 0 means "default /
 * single bit" (this is what spi-nor uses for its register ops such as RDID),
 * so we accept both 0 and 1. The SPI NAND/core framework will pick the 1-bit
 * variant of each op via spi_mem_supports_op().
 */
static bool ls_spi_mem_supports_op(struct spi_slave *slave,
				    const struct spi_mem_op *op)
{
	if (op->cmd.buswidth != 1 && op->cmd.buswidth != 0)
		return false;
	if (op->addr.buswidth != 1 && op->addr.buswidth != 0)
		return false;
	if (op->dummy.buswidth != 1 && op->dummy.buswidth != 0)
		return false;
	if (op->data.buswidth != 1 && op->data.buswidth != 0)
		return false;

	if (op->cmd.nbytes > 2 || op->addr.nbytes > 8 || op->dummy.nbytes > 31)
		return false;

	return true;
}

static int ls_spi_exec_op(struct spi_slave *slave, const struct spi_mem_op *op)
{
	struct udevice *dev = slave->dev;
	u8 cmd_buf[2], addr_buf[8];
	int i, ret = 0;
	bool first = true;

	if (op->cmd.nbytes) {
		for (i = 0; i < op->cmd.nbytes; i++)
			cmd_buf[i] = (op->cmd.opcode >>
				      (8 * (op->cmd.nbytes - 1 - i))) & 0xff;
		ret = ls_spi_mem_xfer(dev, cmd_buf, NULL, op->cmd.nbytes,
				      first, !op->addr.nbytes &&
				      !op->dummy.nbytes && !op->data.nbytes);
		if (ret)
			return ret;
		first = false;
	}

	if (op->addr.nbytes) {
		for (i = 0; i < op->addr.nbytes; i++)
			addr_buf[i] = (op->addr.val >>
				       (8 * (op->addr.nbytes - 1 - i))) & 0xff;
		ret = ls_spi_mem_xfer(dev, addr_buf, NULL, op->addr.nbytes,
				      first, !op->dummy.nbytes &&
				      !op->data.nbytes);
		if (ret)
			return ret;
		first = false;
	}

	if (op->dummy.nbytes) {
		ret = ls_spi_mem_xfer(dev, NULL, NULL, op->dummy.nbytes,
				      first, !op->data.nbytes);
		if (ret)
			return ret;
		first = false;
	}

	if (op->data.nbytes) {
		if (op->data.dir == SPI_MEM_DATA_IN)
			ret = ls_spi_mem_xfer(dev, NULL,
					      (u8 *)op->data.buf.in,
					      op->data.nbytes, first, true);
		else
			ret = ls_spi_mem_xfer(dev,
					      (const u8 *)op->data.buf.out,
					      NULL, op->data.nbytes, first, true);
		if (ret)
			return ret;
		first = false;
	}

	return 0;
}

static const struct spi_controller_mem_ops ls_spi_mem_ops = {
	.supports_op	= ls_spi_mem_supports_op,
	.exec_op	= ls_spi_exec_op,
};

static const struct dm_spi_ops ls_spi_ops = {
	.claim_bus	= ls_spi_claim_bus,
	.release_bus	= ls_spi_release_bus,
	.xfer		= ls_spi_xfer,
	.set_speed	= ls_spi_set_speed,
	.set_mode	= ls_spi_set_mode,
	.mem_ops	= &ls_spi_mem_ops,
	/*
	 * cs_info is not needed, since we require all chip selects to be
	 * in the device tree explicitly
	 */
};

static const struct udevice_id ls_spi_ids[] = {
	{ .compatible = "loongson,ls-spi" },
	{}
};

U_BOOT_DRIVER(ls_spi) = {
	.name	= "ls_spi",
	.id		= UCLASS_SPI,
	.ops	= &ls_spi_ops,
	.of_match = ls_spi_ids,
	.of_to_plat = ls_spi_ofdata_to_platdata,
	.plat_auto = sizeof(struct ls_spi_platdata),
	.probe	= ls_spi_probe,
};
#endif
