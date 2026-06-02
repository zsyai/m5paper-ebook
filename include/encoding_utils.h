#pragma once

#include <cstdint>
#include <string>

enum class TextEncoding {
    UTF8,
    GBK,
    AUTO_DETECT
};

TextEncoding detect_text_encoding(const uint8_t *buffer, size_t size);
std::string convert_to_utf8(const std::string &input, TextEncoding from_encoding);
