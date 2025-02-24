#ifndef __NVMEM_EEPROM_H__
#define __NVMEM_EEPROM_H__

int nvmem_get_mac(int mac_num, unsigned char* buf);
int nvmem_set_mac(int mac_num, unsigned char* buf);

// tool

/*
 * @brief: 将MAC地址字符串转换为字节数组
 * @param[in]: mac_str: MAC地址字符串（支持"xx:xx:xx:xx:xx:xx"或"xxxxxx"格式）
 * @param[out]: buf: 存储转换结果的缓冲区（至少6字节）
 * @return: 成功返回0，失败返回1
 */
int nvmem_mac_str_to_mac_data(char* mac_str, unsigned char* buf);
int nvmem_mac_data_to_mac_str(unsigned char* buf, char* mac_str);

#endif

