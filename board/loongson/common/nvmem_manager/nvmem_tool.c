#include <string.h>
#include <stddef.h>

/*
 * @brief: 将单个十六进制字符转换为数值
 * @param[in]: ch: 十六进制字符（0-9, A-F, a-f）
 * @return: 成功返回0-15，失败返回255
 */
static unsigned char hex_char_to_value(unsigned char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	return 0xff;
}

/*
 * @brief: 将MAC地址字符串转换为字节数组
 * @param[in]: mac_str: MAC地址字符串(支持"xx:xx:xx:xx:xx:xx"或"xxxxxx"格式)
 * @param[out]: buf: 存储转换结果的缓冲区 (6字节)
 * @return: 成功返回0，失败返回1
 */
int nvmem_mac_str_to_mac_data(char* mac_str, unsigned char* buf)
{
	int i;
	int len;
	unsigned char* mac_str_p;
	unsigned char mac_data[6];
	int has_colon;

	if (!mac_str|| !buf)
		return 1;

	len = strlen(mac_str);
	mac_str_p = mac_str;

	// 长度验证
	if (len != 17 && len != 12)
		return 1;

	has_colon = (len == 17);

	// 解析6个字节
	for (i = 0; i < 6; i++) {
		// 检查是否还有足够的字符（至少需要2个字符）
		if (!mac_str_p[0] || !mac_str_p[1])
			return 1;

		// 转换两个十六进制字符
		unsigned char high = hex_char_to_value(mac_str_p[0]);
		unsigned char low = hex_char_to_value(mac_str_p[1]);

		if (high == 0xff || low == 0xff)
			return 1;

		mac_data[i] = (high << 4) | low;
		mac_str_p += 2;  // 移动到下一位

		// 处理分隔符或结束符
		if (i < 5) {
			// 前5个字节后
			if (has_colon) {
				// 带冒号格式：必须是冒号
				if (mac_str_p[0] != ':')
					return 1;
				mac_str_p++;  // 跳过冒号
			}
			// 无冒号格式：继续解析下一个字节
		} else {
			// 第6个字节后：必须是字符串结束符
			if (mac_str_p[0] != '\0')
				return 1;
		}
	}

	memmove(buf, mac_data, 6);
	return 0;
}

/*
 * @brief: 将6字节数组转换为小写MAC地址字符串
 * @param[in]: buf: MAC地址字节数组（6字节）
 * @param[out]: out_str: 输出字符串缓冲区（至少18字节）
 * @return: 成功返回0，失败返回 1
 */
int nvmem_mac_data_to_mac_str(unsigned char* buf, char* mac_str)
{
	int i;
	char *p;
	unsigned char high;
	unsigned char low;

	if (!buf || !mac_str)
		return 1;

	p = mac_str;

	for (i = 0; i < 6; i++) {
		// 分离高4位和低4位
		high = (buf[i] >> 4) & 0x0F;
		low = buf[i] & 0x0F;

		// 高4位转小写十六进制字符
		if (high < 10)
			*p++ = '0' + high;
		else
			*p++ = 'a' + (high - 10);

		// 低4位转小写十六进制字符
		if (low < 10)
			*p++ = '0' + low;
		else
			*p++ = 'a' + (low - 10);

		// 添加冒号（除了最后一个字节）
		if (i < 5)
			*p++ = ':';
	}

	// 添加字符串结束符
	*p = '\0';

	return 0;
}

