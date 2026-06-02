#include "utf8_utils.h"

uint32_t Utf8Utils::getNextCharUnicode(const uint8_t* p, const uint8_t* end, int& bytes) {
    uint32_t unicode = 0;
    bytes = 0;

    if (p >= end) return 0;

    if (*p < 0x80) {
        unicode = *p;
        bytes = 1;
    } else if ((*p & 0xE0) == 0xC0) {
        if (p + 1 < end) {
            unicode = ((*p & 0x1F) << 6) | (*(p + 1) & 0x3F);
            bytes = 2;
        }
    } else if ((*p & 0xF0) == 0xE0) {
        if (p + 2 < end) {
            unicode = ((*p & 0x0F) << 12) | ((*(p + 1) & 0x3F) << 6) | (*(p + 2) & 0x3F);
            bytes = 3;
        }
    } else if ((*p & 0xF8) == 0xF0) {
        if (p + 3 < end) {
            unicode = ((*p & 0x07) << 18) | ((*(p + 1) & 0x3F) << 12) | ((*(p + 2) & 0x3F) << 6) | (*(p + 3) & 0x3F);
            bytes = 4;
        }
    }

    return unicode;
}
