#pragma once

#include <stdint.h>
#include <cstddef>

struct CharGlyphInfo {
    uint16_t unicode;
    uint16_t width;
    uint8_t bitmapW;
    uint8_t bitmapH;
    int8_t x_offset;
    int8_t y_offset;
    uint32_t bitmap_offset;
    uint32_t bitmap_size;
} __attribute__((packed));

struct PageFontCacheHeader {
    uint32_t total_size;
    uint32_t char_count;
    uint32_t index_offset;
    uint32_t bitmap_offset;
} __attribute__((packed));

class FontDecoder {
public:
    // Decode 1-bit packed bitmap to 8-bit grayscale (0 or 255)
    static void decode_bitmap_1bit(const uint8_t* raw_data, uint32_t bitmap_size, 
                                   uint8_t* output_bitmap, int16_t w, int16_t h);

    // Decode 4-bit packed bitmap to 8-bit grayscale (0-255)
    static void decode_bitmap_4bit(const uint8_t* raw_data, uint32_t bitmap_size, 
                                   uint8_t* output_bitmap, int16_t w, int16_t h);

    // Decode 2-bit packed bitmap to 8-bit grayscale (0-255)
    static void decode_bitmap_2bit(const uint8_t* raw_data, uint32_t bitmap_size, 
                                   uint8_t* output_bitmap, int16_t w, int16_t h);
};
