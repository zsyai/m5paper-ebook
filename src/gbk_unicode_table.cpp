#include "gbk_unicode_table.h"
#include <Arduino.h>

// 查找表数据在 gbk_unicode_data.cpp 中定义
// 这样便于单独生成和管理

// 二分查找GBK码对应的Unicode
uint16_t gbk_to_unicode_lookup(uint16_t gbk_code)
{
    int left = 0;
    int right = GBK_TABLE_SIZE - 1;

    while (left <= right)
    {
        int mid = (left + right) / 2;
        uint16_t mid_gbk = pgm_read_word(&gbk_to_unicode_table[mid].gbk_code);

        if (mid_gbk == gbk_code)
        {
            return pgm_read_word(&gbk_to_unicode_table[mid].unicode);
        }
        else if (mid_gbk < gbk_code)
        {
            left = mid + 1;
        }
        else
        {
            right = mid - 1;
        }
    }

    return 0; // 未找到，返回0
}

// 反向查找：Unicode→GBK
uint16_t unicode_to_gbk_lookup(uint16_t unicode)
{
    for (size_t i = 0; i < GBK_TABLE_SIZE; ++i)
    {
        if (pgm_read_word(&gbk_to_unicode_table[i].unicode) == unicode)
        {
#if DBG_GBK_UNICODE_TABLE
            Serial.printf("[GBK] Unicode U+%04X -> GBK 0x%04X\n", unicode, pgm_read_word(&gbk_to_unicode_table[i].gbk_code));
#endif
            return pgm_read_word(&gbk_to_unicode_table[i].gbk_code);
        }
    }
#if DBG_GBK_UNICODE_TABLE
    Serial.printf("[GBK] Unicode U+%04X 未找到GBK映射\n", unicode);
#endif
    return 0; // 未找到
}

// UTF8编码函数：将Unicode码点编码为UTF8字节序列
int utf8_encode(uint16_t unicode, uint8_t *outbuf)
{
    if (unicode <= 0x7F)
    {
        // 1字节UTF8 (0xxxxxxx)
        outbuf[0] = (uint8_t)unicode;
        return 1;
    }
    else if (unicode <= 0x7FF)
    {
        // 2字节UTF8 (110xxxxx 10xxxxxx)
        outbuf[0] = 0xC0 | (unicode >> 6);
        outbuf[1] = 0x80 | (unicode & 0x3F);
        return 2;
    }
    else
    {
        // 3字节UTF8 (1110xxxx 10xxxxxx 10xxxxxx)
        outbuf[0] = 0xE0 | (unicode >> 12);
        outbuf[1] = 0x80 | ((unicode >> 6) & 0x3F);
        outbuf[2] = 0x80 | (unicode & 0x3F);
        return 3;
    }
}

// GBK字符串转换为UTF8字符串
std::string convert_gbk_to_utf8_lookup(const std::string &gbk_input)
{
    std::string result;
    result.reserve(gbk_input.length() * 2); // 预分配空间

    const uint8_t *buf = (const uint8_t *)gbk_input.c_str();
    size_t len = gbk_input.length();

    //    Serial.printf("[GBK_CONVERT] 开始转换，输入长度=%zu\n", len);

    for (size_t i = 0; i < len;)
    {
        uint8_t byte1 = buf[i];

        // ASCII字符直接复制
        if (byte1 < 0x80)
        {
            result += (char)byte1;
            i++;
            continue;
        }

        // GBK双字节字符
        if (i + 1 < len)
        {
            uint8_t byte2 = buf[i + 1];

            // 检查是否为合法GBK范围
            if (byte1 >= 0xA1 && byte1 <= 0xFE && byte2 >= 0xA1 && byte2 <= 0xFE)
            {
                uint16_t gbk_code = (byte1 << 8) | byte2;
                uint16_t unicode = gbk_to_unicode_lookup(gbk_code);

                //                Serial.printf("[GBK_CONVERT] GBK=0x%04X -> Unicode=0x%04X\n", gbk_code, unicode);

                if (unicode != 0)
                {
                    // 找到对应的Unicode，转换为UTF8
                    uint8_t utf8_bytes[4];
                    int utf8_len = utf8_encode(unicode, utf8_bytes);
                    for (int j = 0; j < utf8_len; j++)
                    {
                        result += (char)utf8_bytes[j];
                    }
                }
                else
                {
                    // 查表未找到，使用替代字符
                    result += "□"; // 使用方框替代未知字符

#if DBG_GBK_UNICODE_TABLE
                    Serial.printf("[GBK_CONVERT] 未找到映射: GBK=0x%04X\n", gbk_code);
#endif
                }

                i += 2;
            }
            else
            {
                // 不是合法GBK范围，单字节处理
                result += (char)byte1;
                i++;
            }
        }
        else
        {
            // 最后一个字节，直接添加
            result += (char)byte1;
            i++;
        }
    }

    //    Serial.printf("[GBK_CONVERT] 转换完成，输出长度=%zu\n", result.length());
    return result;
}

// UTF-8转GBK
std::string convert_utf8_to_gbk(const std::string &utf8_input)
{
    std::string result;
    size_t i = 0;
    while (i < utf8_input.length())
    {
        uint8_t c = (uint8_t)utf8_input[i];
        uint16_t unicode = 0;
        if (c < 0x80)
        {
            unicode = c;
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < utf8_input.length())
        {
            unicode = ((c & 0x1F) << 6) | (utf8_input[i + 1] & 0x3F);
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < utf8_input.length())
        {
            unicode = ((c & 0x0F) << 12) | ((utf8_input[i + 1] & 0x3F) << 6) | (utf8_input[i + 2] & 0x3F);
            i += 3;
        }
        else
        {
// 非法字节，跳过
#if DBG_GBK_UNICODE_TABLE
            Serial.printf("[GBK] 非法UTF8字节: 0x%02X\n", c);
#endif
            i += 1;
            result += '?';
            continue;
        }
        uint16_t gbk = unicode_to_gbk_lookup(unicode);
        if (gbk)
        {
#if DBG_GBK_UNICODE_TABLE
            Serial.printf("[GBK] 转换: U+%04X -> GBK 0x%04X\n", unicode, gbk);
#endif
            result += (char)(gbk >> 8);
            result += (char)(gbk & 0xFF);
        }
        else if (unicode < 0x80)
        {
            result += (char)unicode;
        }
        else
        {
#if DBG_GBK_UNICODE_TABLE
            Serial.printf("[GBK] 无法映射U+%04X，输出?\n", unicode);
#endif
            result += '?'; // 未找到映射
        }
    }
#if DBG_GBK_UNICODE_TABLE
    Serial.printf("[GBK] UTF8转GBK完成，输入长度=%zu，输出长度=%zu\n", utf8_input.length(), result.length());
#endif
    return result;
}
