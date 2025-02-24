#ifndef __LS2K300_I2C_H__
#define __LS2K300_I2C_H__

#include <linux/types.h>
#include <clk.h>

/* 寄存器偏移 */
#define I2C_CR1 (0x00)
#define I2C_CR2 (0x04)
#define I2C_OAR (0x08)
#define I2C_DR (0x10)
#define I2C_SR1 (0x14)
#define I2C_SR2 (0x18)
#define I2C_CCR (0x1c)
#define I2C_TRISE (0x20)

#define I2C_CR1_SWRST (15)
#define I2C_CR1_RECOVER (14)
#define I2C_CR1_POS (11)
#define I2C_CR1_ACK (10)
#define I2C_CR1_STOP (9)
#define I2C_CR1_START (8)
#define I2C_CR1_PE (0)

#define I2C_CR2_FREQ_MASK (0x3f)
#define I2C_CCR_CCR_MASK (0xfff)
#define I2C_CCR_F_S (15)
#define I2C_CCR_DUTY (14)
#define I2C_SR1_AF (10)
#define I2C_SR1_ARLO (9)
#define I2C_SR1_BERR (8)
#define I2C_SR1_TXE (7)
#define I2C_SR1_RXNE (6)
#define I2C_SR1_BTF (2)
#define I2C_SR1_ADDR (1)
#define I2C_SR1_SB (0)
#define I2C_TRISE_TRISE_MASK (0x3f)

#define I2C_SR1_ITEVTEN_MASK ((1 << I2C_SR1_BTF) | (1 << I2C_SR1_ADDR) | (1 << I2C_SR1_SB))
#define I2C_SR1_ITBUFEN_MASK ((1 << I2C_SR1_TXE) | (1 << I2C_SR1_RXNE))
#define I2C_SR1_ITERREN_MASK ((1 << I2C_SR1_AF)  | (1 << I2C_SR1_ARLO) | (1 << I2C_SR1_BERR))

/* Loongson I2C Status 2 */
#define I2C_SR2_BUSY (1)

/* 驱动私有数据结构 - 添加时钟支持 */
struct ls2k300_i2c_priv {
    void __iomem *base;
    int speed;          /* 总线速度(Hz) */
    unsigned long input_clk; /* 输入时钟频率(Hz) */
    struct clk clk;     /* 时钟句柄 */
};

#endif

