#include "book/text_decode.h"

#include "gbk_unicode_table.h"

namespace reader {
namespace book {
namespace {

// The helpers below moved verbatim from book_reader.cpp, so page layout and
// indexing keep producing the same offsets as before.

TextEncoding DetectEncodingImpl(const uint8_t *buffer, size_t size)
{
    if (size < 3)
        return TextEncoding::UTF8;

    if (size >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF)
    {
        return TextEncoding::UTF8;
    }
    if (size >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE)
    {
        return TextEncoding::UTF16LE;
    }
    if (size >= 2 && buffer[0] == 0xFE && buffer[1] == 0xFF)
    {
        return TextEncoding::UTF16BE;
    }

    size_t valid_utf8_chars = 0;
    size_t total_chars = 0;
    size_t gbk_chars = 0;

    for (size_t i = 0; i < size && i < 1024; i++)
    {
        uint8_t byte = buffer[i];
        total_chars++;

        if (byte < 0x80)
        {
            valid_utf8_chars++;
            continue;
        }

        if ((byte & 0xE0) == 0xC0 && i + 1 < size)
        {
            uint8_t next = buffer[i + 1];
            if ((next & 0xC0) == 0x80)
            {
                valid_utf8_chars += 2;
                i++;
                continue;
            }
        }
        else if ((byte & 0xF0) == 0xE0 && i + 2 < size)
        {
            uint8_t next1 = buffer[i + 1];
            uint8_t next2 = buffer[i + 2];
            if ((next1 & 0xC0) == 0x80 && (next2 & 0xC0) == 0x80)
            {
                valid_utf8_chars += 3;
                i += 2;
                continue;
            }
        }

        if (byte >= 0xA1 && byte <= 0xFE && i + 1 < size)
        {
            uint8_t next = buffer[i + 1];
            if (next >= 0xA1 && next <= 0xFE)
            {
                gbk_chars += 2;
                i++;
                continue;
            }
        }
    }

    float gbk_ratio = 0.0f;
    if (total_chars == 0)
    {
        return TextEncoding::UTF8;
    }
    else
    {
        gbk_ratio = (float)gbk_chars / (float)total_chars;
    }

    if (gbk_ratio > 0.3)
    {
        return TextEncoding::GBK;
    }
    else
    {
        return TextEncoding::UTF8;
    }
}

std::string convert_utf16le_to_utf8(const std::string& utf16_str) {
    std::string utf8_str;
    utf8_str.reserve(utf16_str.length() * 3 / 2);
    const uint8_t* src = (const uint8_t*)utf16_str.c_str();
    size_t len = utf16_str.length();
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint16_t w1 = src[i] | (src[i+1] << 8);
        if (i == 0 && w1 == 0xFEFF) continue;
        uint32_t codepoint = w1;
        if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 3 < len) {
            uint16_t w2 = src[i+2] | (src[i+3] << 8);
            if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                codepoint = 0x10000 + ((w1 - 0xD800) << 10) + (w2 - 0xDC00);
                i += 2;
            }
        }
        if (codepoint < 0x80) {
            utf8_str.push_back((char)codepoint);
        } else if (codepoint < 0x800) {
            utf8_str.push_back((char)(0xC0 | (codepoint >> 6)));
            utf8_str.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else if (codepoint < 0x10000) {
            utf8_str.push_back((char)(0xE0 | (codepoint >> 12)));
            utf8_str.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8_str.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else {
            utf8_str.push_back((char)(0xF0 | (codepoint >> 18)));
            utf8_str.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
            utf8_str.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8_str.push_back((char)(0x80 | (codepoint & 0x3F)));
        }
    }
    return utf8_str;
}

std::string convert_utf16be_to_utf8(const std::string& utf16_str) {
    std::string utf8_str;
    utf8_str.reserve(utf16_str.length() * 3 / 2);
    const uint8_t* src = (const uint8_t*)utf16_str.c_str();
    size_t len = utf16_str.length();
    for (size_t i = 0; i + 1 < len; i += 2) {
        uint16_t w1 = (src[i] << 8) | src[i+1];
        if (i == 0 && w1 == 0xFEFF) continue;
        uint32_t codepoint = w1;
        if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 3 < len) {
            uint16_t w2 = (src[i+2] << 8) | src[i+3];
            if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                codepoint = 0x10000 + ((w1 - 0xD800) << 10) + (w2 - 0xDC00);
                i += 2;
            }
        }
        if (codepoint < 0x80) {
            utf8_str.push_back((char)codepoint);
        } else if (codepoint < 0x800) {
            utf8_str.push_back((char)(0xC0 | (codepoint >> 6)));
            utf8_str.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else if (codepoint < 0x10000) {
            utf8_str.push_back((char)(0xE0 | (codepoint >> 12)));
            utf8_str.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8_str.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else {
            utf8_str.push_back((char)(0xF0 | (codepoint >> 18)));
            utf8_str.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
            utf8_str.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            utf8_str.push_back((char)(0x80 | (codepoint & 0x3F)));
        }
    }
    return utf8_str;
}

size_t MapConvertedPosToRawConsumed(const std::string &raw, TextEncoding enc, size_t converted_pos)
{
    const uint8_t *buf = (const uint8_t *)raw.c_str();
    size_t raw_len = raw.length();
    size_t acc_converted_bytes = 0;
    size_t i = 0;

    if (enc == TextEncoding::UTF8)
    {
        while (i < raw_len)
        {
            uint8_t b = buf[i];
            if (b < 0x80)
            {
                acc_converted_bytes += 1;
                i += 1;
            }
            else if ((b & 0xE0) == 0xC0 && i + 1 < raw_len && (buf[i + 1] & 0xC0) == 0x80)
            {
                acc_converted_bytes += 2;
                i += 2;
            }
            else if ((b & 0xF0) == 0xE0 && i + 2 < raw_len && (buf[i + 1] & 0xC0) == 0x80 && (buf[i + 2] & 0xC0) == 0x80)
            {
                acc_converted_bytes += 3;
                i += 3;
            }
            else
            {
                acc_converted_bytes += 3; // placeholder U+25A1
                i += 1;
            }

            if (acc_converted_bytes >= converted_pos)
                return i;
        }
        return raw_len;
    }

    if (enc == TextEncoding::GBK)
    {
        while (i < raw_len)
        {
            uint8_t b = buf[i];
            if (b < 0x80)
            {
                acc_converted_bytes += 1;
                i += 1;
            }
            else if (b >= 0x81 && b <= 0xFE && i + 3 < raw_len && buf[i+1] >= 0x30 && buf[i+1] <= 0x39 && buf[i+2] >= 0x81 && buf[i+2] <= 0xFE && buf[i+3] >= 0x30 && buf[i+3] <= 0x39)
            {
                if (b >= 0x90) {
                   uint32_t offset = (b - 0x90) * 12600 + (buf[i+1] - 0x30) * 1260 + (buf[i+2] - 0x81) * 10 + (buf[i+3] - 0x30);
                   uint32_t unicode = 0x10000 + offset;
                   if (unicode < 0x110000) acc_converted_bytes += 4;
                   else acc_converted_bytes += 3;
                } else {
                   acc_converted_bytes += 3; // placeholder
                }
                i += 4;
            }
            else if (i + 1 < raw_len && b >= 0x81 && b <= 0xFE && buf[i + 1] >= 0x40 && buf[i + 1] <= 0xFE && buf[i + 1] != 0x7F)
            {
                uint16_t gbk_code = (uint16_t(b) << 8) | uint16_t(buf[i + 1]);
                uint16_t uni = gbk_to_unicode_lookup(gbk_code);
                if (uni != 0)
                {
                    uint8_t tmp[4];
                    acc_converted_bytes += utf8_encode(uni, tmp);
                }
                else
                {
                    acc_converted_bytes += 3; // placeholder
                }
                i += 2;
            }
            else
            {
                acc_converted_bytes += 1; // placeholder or undefined
                i += 1;
            }

            if (acc_converted_bytes >= converted_pos)
                return i;
        }
        return raw_len;
    }

    if (enc == TextEncoding::UTF16LE)
    {
        while (i + 1 < raw_len)
        {
            uint16_t w1 = buf[i] | (buf[i+1] << 8);
            if (i == 0 && w1 == 0xFEFF) {
                i += 2;
                continue;
            }
            uint32_t codepoint = w1;
            int step = 2;
            if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 3 < raw_len) {
                uint16_t w2 = buf[i+2] | (buf[i+3] << 8);
                if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                    codepoint = 0x10000 + ((w1 - 0xD800) << 10) + (w2 - 0xDC00);
                    step = 4;
                }
            }
            
            if (codepoint < 0x80) acc_converted_bytes += 1;
            else if (codepoint < 0x800) acc_converted_bytes += 2;
            else if (codepoint < 0x10000) acc_converted_bytes += 3;
            else acc_converted_bytes += 4;
            
            i += step;
            
            if (acc_converted_bytes >= converted_pos)
                return i;
        }
        return raw_len;
    }

    if (enc == TextEncoding::UTF16BE)
    {
        while (i + 1 < raw_len)
        {
            uint16_t w1 = (buf[i] << 8) | buf[i+1];
            if (i == 0 && w1 == 0xFEFF) {
                i += 2;
                continue;
            }
            uint32_t codepoint = w1;
            int step = 2;
            if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 3 < raw_len) {
                uint16_t w2 = (buf[i+2] << 8) | buf[i+3];
                if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                    codepoint = 0x10000 + ((w1 - 0xD800) << 10) + (w2 - 0xDC00);
                    step = 4;
                }
            }
            
            if (codepoint < 0x80) acc_converted_bytes += 1;
            else if (codepoint < 0x800) acc_converted_bytes += 2;
            else if (codepoint < 0x10000) acc_converted_bytes += 3;
            else acc_converted_bytes += 4;
            
            i += step;
            
            if (acc_converted_bytes >= converted_pos)
                return i;
        }
        return raw_len;
    }

    return raw_len;
}

}  // namespace

TextEncoding DetectEncoding(const uint8_t* buffer, size_t size) {
  return DetectEncodingImpl(buffer, size);
}

size_t SafeChunkEnd(const uint8_t* buffer, size_t len, TextEncoding enc) {
  size_t end_pos = len;
  if (enc == TextEncoding::UTF16LE || enc == TextEncoding::UTF16BE) {
    if (end_pos % 2 != 0) end_pos--;
    if (end_pos >= 2) {
      uint16_t last_word = (enc == TextEncoding::UTF16LE)
                               ? (buffer[end_pos - 2] | (buffer[end_pos - 1] << 8))
                               : ((buffer[end_pos - 2] << 8) | buffer[end_pos - 1]);
      if (last_word >= 0xD800 && last_word <= 0xDBFF) {
        end_pos -= 2;
      }
    }
  } else if (enc == TextEncoding::GBK) {
    size_t safe_end = 0;
    while (safe_end < end_pos) {
      uint8_t c = buffer[safe_end];
      if (c >= 0x81 && c <= 0xFE && safe_end + 3 < end_pos &&
          buffer[safe_end + 1] >= 0x30 && buffer[safe_end + 1] <= 0x39 &&
          buffer[safe_end + 2] >= 0x81 && buffer[safe_end + 2] <= 0xFE &&
          buffer[safe_end + 3] >= 0x30 && buffer[safe_end + 3] <= 0x39) {
        if (safe_end + 4 > end_pos) break;
        safe_end += 4;
      } else if (c >= 0x81 && c <= 0xFE) {
        if (safe_end + 2 > end_pos) break;
        safe_end += 2;
      } else {
        safe_end++;
      }
    }
    end_pos = safe_end;
  } else {
    while (end_pos > 0 && (buffer[end_pos - 1] & 0xC0) == 0x80) {
      end_pos--;
    }
    if (end_pos > 0 && (buffer[end_pos - 1] & 0x80) != 0) {
      end_pos--;
    }
  }
  return end_pos;
}

std::string ToUtf8(const uint8_t* raw, size_t len, TextEncoding enc) {
  std::string text(reinterpret_cast<const char*>(raw), len);
  switch (enc) {
    case TextEncoding::GBK:
      return convert_gbk_to_utf8_lookup(text);
    case TextEncoding::UTF16LE:
      return convert_utf16le_to_utf8(text);
    case TextEncoding::UTF16BE:
      return convert_utf16be_to_utf8(text);
    default:
      return text;
  }
}

size_t RawBytesForUtf8Prefix(const uint8_t* raw, size_t len, TextEncoding enc,
                             size_t utf8_bytes) {
  if (enc != TextEncoding::GBK && enc != TextEncoding::UTF16LE &&
      enc != TextEncoding::UTF16BE) {
    return utf8_bytes;  // UTF-8 text is laid out as-is.
  }
  std::string text(reinterpret_cast<const char*>(raw), len);
  return MapConvertedPosToRawConsumed(text, enc, utf8_bytes);
}

}  // namespace book
}  // namespace reader
