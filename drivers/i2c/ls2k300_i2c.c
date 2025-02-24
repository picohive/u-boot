#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <clk.h>
#include <linux/delay.h>
#include "ls2k300_i2c.h"

#define LS2K300_I2C_TIMEOUT_US  10000

// #define I2C_DEBUG_ENABLE

#ifdef I2C_DEBUG_ENABLE
#define i2c_debug(fmt, args...) printf("I2C-%llx: " fmt, (unsigned long long)priv->base, ##args)
#else
#define i2c_debug(fmt, args...)
#endif

/*
 * @brief: 等待指定事件信号
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: reg_offset: 寄存器偏移
 * @param[in]: bit_offset: 位偏移
 * @param[in]: target: 目标值
 * @return: 成功返回0，超时返回-ETIMEDOUT
 */
static inline int ls2k300_i2c_wait_signal(struct ls2k300_i2c_priv *priv, unsigned char reg_offset, unsigned char bit_offset, unsigned char target)
{
	unsigned int count = 0;
	unsigned int status;

	while (1) {
		status = readl(priv->base + reg_offset);
		status &= (1 << bit_offset);

		if (!!status == !!target)
			return 0;

		udelay(10);
		if (++count >= LS2K300_I2C_TIMEOUT_US / 10) {
			i2c_debug("Wait timeout: reg=0x%x, bit=%d, target=%d\n", reg_offset, bit_offset, target);
			return -ETIMEDOUT;
		}
	}
}

/*
 * @brief: I2C总线错误恢复
 * @param[in]: priv: I2C私有数据结构指针
 * @return: 无
 */
static void ls2k300_i2c_bus_reset(struct ls2k300_i2c_priv *priv)
{
	unsigned int temp;
	unsigned int cr1_mask = 0;

	i2c_debug("Bus reset due to error\n");

	if (readl(priv->base + I2C_SR1) & (1 << I2C_SR1_BERR))
		cr1_mask = (1 << I2C_CR1_RECOVER);

	// 状态机复位
	temp = readl(priv->base + I2C_CR1);
	temp |= ((1 << I2C_CR1_SWRST) | cr1_mask);
	writel(temp, priv->base + I2C_CR1);

	// 错误复位
	temp = readl(priv->base + I2C_SR1);
	temp |= (0xF << 8);
	temp ^= (0xF << 8);
	writel(temp, priv->base + I2C_SR1);

	temp = readl(priv->base + I2C_CR1);
	temp |= ((1 << I2C_CR1_SWRST) | cr1_mask);
	temp ^= ((1 << I2C_CR1_SWRST) | cr1_mask);
	writel(temp, priv->base + I2C_CR1);
}

/*
 * @brief: 计算CCR寄存器值
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: bus_speed: 总线速度(Hz)
 * @return: CCR寄存器值
 */
static unsigned int ls2k300_i2c_calc_ccr(struct ls2k300_i2c_priv *priv, unsigned long bus_speed)
{
	unsigned int ccr;
	unsigned int duty_param;

	/*
	* 100KHz: 1:1占空比, 参数=2
	* 400KHz: 1:2占空比, 参数=3
	*/
	if (bus_speed == 400000) {
		duty_param = 3;
	} else {
		duty_param = 2;
	}

	/*
	* CCR = 输入时钟 / (总线速度 * 占空比参数)
	* 例如: 100MHz / (100KHz * 2) = 500
	*/
	ccr = priv->input_clk / (bus_speed * duty_param);

	/* 确保CCR在有效范围内 */
	if (ccr > I2C_CCR_CCR_MASK)
		ccr = I2C_CCR_CCR_MASK;
	if (ccr < 1)
		ccr = 1;

	i2c_debug("CCR calculation: input_clk=%lu, bus_speed=%lu, param=%d, ccr=%u\n",
			priv->input_clk, bus_speed, duty_param, ccr);

	return ccr;
}

/*
 * @brief: 读取1字节数据
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: msg: I2C消息结构体指针
 * @return: 成功返回0，超时返回-ETIMEDOUT
 */
static int ls2k300_i2c_read_1byte(struct ls2k300_i2c_priv *priv, struct i2c_msg *msg)
{
	unsigned int temp;

	temp = readl(priv->base + I2C_CR1);
	temp &= ~((1 << I2C_CR1_ACK) | (1 << I2C_CR1_POS));
	if (msg->flags & I2C_M_STOP)
		temp |= (1 << I2C_CR1_STOP);
	writel(temp, priv->base + I2C_CR1);

	if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_RXNE, 1))
		return -ETIMEDOUT;

	msg->buf[0] = readl(priv->base + I2C_DR) & 0xFF;
	return 0;
}

/*
 * @brief: 读取2字节数据
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: msg: I2C消息结构体指针
 * @return: 成功返回0，超时返回-ETIMEDOUT
 */
static int ls2k300_i2c_read_2bytes(struct ls2k300_i2c_priv *priv, struct i2c_msg *msg)
{
	unsigned int temp;

	if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_RXNE, 1))
		return -ETIMEDOUT;
	msg->buf[0] = readl(priv->base + I2C_DR) & 0xFF;

	temp = readl(priv->base + I2C_CR1);
	temp &= ~((1 << I2C_CR1_ACK) | (1 << I2C_CR1_POS));
	if (msg->flags & I2C_M_STOP)
		temp |= (1 << I2C_CR1_STOP);
	writel(temp, priv->base + I2C_CR1);

	if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_RXNE, 1))
		return -ETIMEDOUT;
	msg->buf[1] = readl(priv->base + I2C_DR) & 0xFF;

	return 0;
}

/*
 * @brief: 读取多字节数据
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: msg: I2C消息结构体指针
 * @return: 成功返回0，超时返回-ETIMEDOUT
 */
static int ls2k300_i2c_read_multi(struct ls2k300_i2c_priv *priv, struct i2c_msg *msg)
{
	int i = 0;
	int remain = msg->len;
	unsigned int temp;

	while (remain > 3) {
		if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_RXNE, 1))
			return -ETIMEDOUT;
		msg->buf[i++] = readl(priv->base + I2C_DR) & 0xFF;
		remain--;
	}

	if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_BTF, 1))
		return -ETIMEDOUT;
	msg->buf[i++] = readl(priv->base + I2C_DR) & 0xFF;

	temp = readl(priv->base + I2C_CR1);
	temp &= ~((1 << I2C_CR1_ACK) | (1 << I2C_CR1_POS));
	if (msg->flags & I2C_M_STOP)
		temp |= (1 << I2C_CR1_STOP);
	writel(temp, priv->base + I2C_CR1);

	if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_BTF, 1))
		return -ETIMEDOUT;
	msg->buf[i++] = readl(priv->base + I2C_DR) & 0xFF;

	if (ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_RXNE, 1))
		return -ETIMEDOUT;
	msg->buf[i++] = readl(priv->base + I2C_DR) & 0xFF;

	return 0;
}

/*
 * @brief: I2C主设备读取数据
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: msg: I2C消息结构体指针
 * @return: 成功返回0，失败返回错误码
 */
static int ls2k300_i2c_master_read(struct ls2k300_i2c_priv *priv, struct i2c_msg *msg)
{
	int ret;
	unsigned int temp;
	unsigned char addr;

	temp = readl(priv->base + I2C_CR1);
	temp |= (1 << I2C_CR1_START) | (1 << I2C_CR1_ACK);
	writel(temp, priv->base + I2C_CR1);

	ret = ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_SB, 1);
	if (ret) {
		i2c_debug("Read: timeout waiting SB, addr=0x%02x\n", msg->addr);
		goto ls2k300_i2c_master_read_error;
	}

	addr = (msg->addr << 1) | 0x01;
	writeb(addr, priv->base + I2C_DR);

	ret = ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_ADDR, 1);
	if (ret) {
		i2c_debug("Read: timeout waiting ADDR, addr=0x%02x\n", msg->addr);
		goto ls2k300_i2c_master_read_error;
	}
	temp = readl(priv->base + I2C_SR2);

	if (msg->len == 1)
		ret = ls2k300_i2c_read_1byte(priv, msg);
	else if (msg->len == 2)
		ret = ls2k300_i2c_read_2bytes(priv, msg);
	else
		ret = ls2k300_i2c_read_multi(priv, msg);

	if (ret)
		i2c_debug("Read: transfer failed, addr=0x%02x, len=%d\n",
				msg->addr, msg->len);

ls2k300_i2c_master_read_error:
	if (ret)
		ls2k300_i2c_bus_reset(priv);
	else {
		// 如果需要关闭总线,等待busy信号为0,代表总线总体已经空闲
		if (msg->flags & I2C_M_STOP)
			ls2k300_i2c_wait_signal(priv, I2C_SR2, I2C_SR2_BUSY, 0);
	}
	return ret;
}

/*
 * @brief: I2C主设备写入数据
 * @param[in]: priv: I2C私有数据结构指针
 * @param[in]: msg: I2C消息结构体指针
 * @return: 成功返回0，失败返回错误码
 */
static int ls2k300_i2c_master_write(struct ls2k300_i2c_priv *priv, struct i2c_msg *msg)
{
	int ret;
	unsigned int temp;
	int i;
	unsigned char addr;

	// START信号发送开始命令,ACK则会在接收到数据后发送ACK命令
	temp = readl(priv->base + I2C_CR1);
	temp |= (1 << I2C_CR1_START);
	writel(temp, priv->base + I2C_CR1);

	ret = ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_SB, 1);
	if (ret) {
		i2c_debug("Write: timeout waiting SB, addr=0x%02x\n", msg->addr);
		goto ls2k300_i2c_master_write_error;
	}

	// 把7bit地址左移一位,然后最后1bit设置为1,代表此从地址用于读取
	addr = msg->addr << 1;
	writeb(addr, priv->base + I2C_DR);

	ret = ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_ADDR, 1);
	if (ret) {
		i2c_debug("Write: timeout waiting ADDR, addr=0x%02x\n", msg->addr);
		goto ls2k300_i2c_master_write_error;
	}
	temp = readl(priv->base + I2C_SR2);

	// 等待发送数据寄存器为空
	ret = ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_TXE, 1);
	if (ret) {
		i2c_debug("Write: timeout waiting TXE, addr=0x%02x\n", msg->addr);
		goto ls2k300_i2c_master_write_error;
	}

	for (i = 0; i < msg->len; i++) {
		writeb(msg->buf[i], priv->base + I2C_DR);

		ret = ls2k300_i2c_wait_signal(priv, I2C_SR1, I2C_SR1_BTF, 1);
		if (ret) {
			i2c_debug("Write: timeout waiting BTF at byte %d\n", i);
			goto ls2k300_i2c_master_write_error;
		}
	}

	if (msg->flags & I2C_M_STOP) {
		// 关闭总线
		temp = readl(priv->base + I2C_CR1);
		temp |= (1 << I2C_CR1_STOP);
		writel(temp, priv->base + I2C_CR1);
		// 等待busy信号为0,代表总线总体已经空闲
		if (ls2k300_i2c_wait_signal(priv, I2C_SR2, I2C_SR2_BUSY, 0))
			return -ETIMEDOUT;
	}

	return 0;

ls2k300_i2c_master_write_error:
	ls2k300_i2c_bus_reset(priv);
	return ret;
}

/*
 * @brief: I2C传输接口实现
 * @param[in]: bus: I2C总线设备
 * @param[in]: msgs: I2C消息数组
 * @param[in]: nmsgs: 消息数量
 * @return: 成功返回0，失败返回错误码
 */
static int ls2k300_i2c_xfer(struct udevice *bus, struct i2c_msg *msgs,
						int nmsgs)
{
	struct ls2k300_i2c_priv *priv = dev_get_priv(bus);
	int i, ret;

	for (i = 0; i < nmsgs; i++) {
		/*
		 * 这个本不应该是i2c总线驱动来
		 * 但是大家都不做,只能驱动做了
		 */
		if (i == (nmsgs - 1))
			msgs[i].flags |= I2C_M_STOP;

		if (msgs[i].flags & I2C_M_RD)
			ret = ls2k300_i2c_master_read(priv, &msgs[i]);
		else
			ret = ls2k300_i2c_master_write(priv, &msgs[i]);

		if (ret) {
			i2c_debug("Transfer failed at message %d\n", i);
			return ret;
		}
	}

	return 0;
}

/*
 * @brief: probe_chip接口实现，用于检测指定地址的设备是否存在
 * @param[in]: bus: I2C总线设备
 * @param[in]: chip_addr: 芯片地址
 * @param[in]: chip_flags: 芯片标志
 * @return: 成功返回0，设备不存在返回-EREMOTEIO，其他错误返回相应错误码
 */
static int ls2k300_i2c_probe_chip(struct udevice *bus, unsigned int chip_addr, unsigned int chip_flags)
{
	struct i2c_msg msg;
	int ret;

	printf("Probing chip at address 0x%02x\n", chip_addr);

	/* 默认使用写操作探测(更安全) */
	msg.addr = chip_addr;
	msg.flags = 0;  /* 写操作 */
	msg.len = 0;    /* 只发送地址,不发送数据 */
	msg.buf = NULL;

	/* 执行传输,发送STOP(快速探测) */
	msg.flags |= I2C_M_STOP;

	ret = ls2k300_i2c_xfer(bus, &msg, 1);
	if (ret) {
		if (ret == -EREMOTEIO) {
			printf("Chip 0x%02x not found (NACK)\n", chip_addr);
			return -EREMOTEIO;  /* 设备不存在 */
		}
		printf("Chip 0x%02x probe error: %d\n", chip_addr, ret);
		return ret;  /* 其他错误 */
	}

	printf("Chip 0x%02x found\n", chip_addr);
	return 0;  /* 设备存在 */
}

/*
 * @brief: 设置总线速度
 * @param[in]: bus: I2C总线设备
 * @param[in]: speed: 速度(Hz)
 * @return: 成功返回0
 */
static int ls2k300_i2c_set_speed(struct udevice *bus, unsigned int speed)
{
	struct ls2k300_i2c_priv *priv = dev_get_priv(bus);
	unsigned int temp;
	unsigned int ccr_value;

	/* 配置CR2 */
	temp = readl(priv->base + I2C_CR2);
	temp |= I2C_CR2_FREQ_MASK;
	writel(temp, priv->base + I2C_CR2);

	/* 配置TRISE */
	temp = readl(priv->base + I2C_TRISE);
	temp |= I2C_TRISE_TRISE_MASK;
	writel(temp, priv->base + I2C_TRISE);

	/* 配置CCR */
	temp = readl(priv->base + I2C_CCR);
	temp &= ~(I2C_CCR_CCR_MASK | (1 << I2C_CCR_F_S) | (1 << I2C_CCR_DUTY));

	/* 根据速度模式设置 */
	if (speed == 400000) {
		temp |= (1 << I2C_CCR_F_S);      /* 快速模式 */
		temp &= ~(1 << I2C_CCR_DUTY);    /* 1:2占空比 */
	} else {
		temp &= ~(1 << I2C_CCR_F_S);     /* 标准模式 */
	}

	/* 动态计算CCR值 */
	ccr_value = ls2k300_i2c_calc_ccr(priv, speed);
	temp |= ccr_value & I2C_CCR_CCR_MASK;
	writel(temp, priv->base + I2C_CCR);

	priv->speed = speed;
	i2c_debug("Bus speed set to %u Hz (CCR=0x%x)\n", speed, ccr_value);

	return 0;
}

/*
 * @brief: 获取当前总线速度
 * @param[in]: bus: I2C总线设备
 * @return: 当前总线速度(Hz)
 */
static int ls2k300_i2c_get_speed(struct udevice *bus)
{
	struct ls2k300_i2c_priv *priv = dev_get_priv(bus);

	return priv->speed;
}

/*
 * @brief: 设备探测和初始化
 * @param[in]: bus: I2C总线设备
 * @return: 成功返回0，失败返回错误码
 */
static int ls2k300_i2c_probe(struct udevice *bus)
{
	struct ls2k300_i2c_priv *priv = dev_get_priv(bus);
	unsigned int temp;
	int ret;
	unsigned long bus_speed;

	/* 映射寄存器 */
	priv->base = map_physmem(dev_read_addr(bus), sizeof(void *), MAP_NOCACHE);
	if (!priv->base) {
		printf("Failed to map I2C registers\n");
		return -ENOMEM;
	}

	/* 获取并启用时钟 */
	ret = clk_get_by_index(bus, 0, &priv->clk);
	if (ret) {
		printf("Failed to get I2C clock: %d\n", ret);
		/* 使用默认时钟频率 */
		priv->input_clk = 100000000; /* 默认100MHz */
		printf("Using default input clock: %lu Hz\n", priv->input_clk);
	} else {
		ret = clk_enable(&priv->clk);
		if (ret && ret != -ENOSYS) {
			printf("Failed to enable I2C clock: %d\n", ret);
			return ret;
		}

		priv->input_clk = clk_get_rate(&priv->clk);
		if (priv->input_clk == 0) {
			printf("Clock rate is 0, using default 100MHz\n");
			priv->input_clk = 100000000;
		} else {
			i2c_debug("Input clock rate: %lu Hz\n", priv->input_clk);
		}
	}

	/* 使能I2C控制器 */
	temp = readl(priv->base + I2C_CR1);
	temp |= (1 << I2C_CR1_PE);
	writel(temp, priv->base + I2C_CR1);

	/* 从设备树获取总线速度 */
	bus_speed = dev_read_u32_default(bus, "clock-frequency", 100000);
	ret = ls2k300_i2c_set_speed(bus, bus_speed);
	if (ret) {
		printf("Failed to set I2C speed\n");
		return ret;
	}

	return 0;
}

static const struct dm_i2c_ops ls2k300_i2c_ops = {
	.xfer           = ls2k300_i2c_xfer,
	.probe_chip     = ls2k300_i2c_probe_chip,
	.set_bus_speed  = ls2k300_i2c_set_speed,
	.get_bus_speed  = ls2k300_i2c_get_speed,
};

static const struct udevice_id ls2k300_i2c_ids[] = {
	{ .compatible = "loongson,ls2k300-i2c" },
	{ }
};

U_BOOT_DRIVER(ls2k300_i2c) = {
	.name           = "ls2k300_i2c",
	.id             = UCLASS_I2C,
	.of_match       = ls2k300_i2c_ids,
	.probe          = ls2k300_i2c_probe,
	.priv_auto      = sizeof(struct ls2k300_i2c_priv),
	.ops            = &ls2k300_i2c_ops,
};

