#pragma once
#include <stdint.h>
#include <cstddef>
#include <string>
#include <Arduino.h>

// GBK到Unicode映射表结构
struct GBKToUnicodeEntry {
    uint16_t gbk_code;
    uint16_t unicode;
};

// 常用汉字GBK到Unicode映射表（存储在Flash中）
// 这是一个简化版本，实际项目中需要包含完整的GBK字符集
// 完整的GBK表包含约21000个字符，需要约128KB存储空间
extern const GBKToUnicodeEntry gbk_to_unicode_table[];
extern const size_t GBK_TABLE_SIZE;

// 查表函数声明
uint16_t gbk_to_unicode_lookup(uint16_t gbk_code);
int utf8_encode(uint16_t unicode, uint8_t* outbuf);
std::string convert_gbk_to_utf8_lookup(const std::string& gbk_input);
// 反向查找：Unicode→GBK
uint16_t unicode_to_gbk_lookup(uint16_t unicode);
// UTF-8转GBK
std::string convert_utf8_to_gbk(const std::string& utf8_input);