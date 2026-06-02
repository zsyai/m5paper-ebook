#pragma once

#include <stdint.h>
#include <cstddef>

class Utf8Utils {
public:
    // Get the next character's Unicode codepoint and its byte length.
    // Returns 0 on EOF or error.
    static uint32_t getNextCharUnicode(const uint8_t* p, const uint8_t* end, int& bytes);
};
