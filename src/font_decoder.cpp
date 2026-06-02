#include "font_decoder.h"

void FontDecoder::decode_bitmap_1bit(const uint8_t* raw_data, uint32_t bitmap_size, 
                                     uint8_t* output_bitmap, int16_t w, int16_t h) {
    int pixel_idx = 0;
    int bit_idx = 0;
    int byte_idx = 0;
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t byte_val = raw_data[byte_idx];
            
            // Extract the bit (MSB first)
            bool bit = (byte_val >> (7 - bit_idx)) & 1;
            
            // In our Python tool, we inverted the bits:
            // 1 (text) -> 0
            // 0 (bg) -> 1
            // So here:
            // bit 0 -> text (we output 0 for black)
            // bit 1 -> bg (we output 255 for white)
            output_bitmap[pixel_idx++] = bit ? 255 : 0;
            
            bit_idx++;
            if (bit_idx == 8) {
                bit_idx = 0;
                byte_idx++;
            }
        }
        // Align to next byte at the end of each row
        if (bit_idx > 0) {
            bit_idx = 0;
            byte_idx++;
        }
    }
}

void FontDecoder::decode_bitmap_4bit(const uint8_t* raw_data, uint32_t bitmap_size, 
                                     uint8_t* output_bitmap, int16_t w, int16_t h) {
    int total_pixels = w * h;
    for (int i = 0; i < total_pixels; ++i) {
        uint8_t byte_val = raw_data[i / 2];
        uint8_t nibble = (i % 2 == 0) ? (byte_val >> 4) : (byte_val & 0x0F);
        // Map 0-15 to 0-255
        output_bitmap[i] = nibble * 17; // 15 * 17 = 255
    }
}

void FontDecoder::decode_bitmap_2bit(const uint8_t* raw_data, uint32_t bitmap_size, 
                                     uint8_t* output_bitmap, int16_t w, int16_t h) {
    int total_pixels = w * h;
    for (int i = 0; i < total_pixels; ++i) {
        uint8_t byte_val = raw_data[i / 4];
        int shift = 6 - (i % 4) * 2;
        uint8_t val = (byte_val >> shift) & 0x03;
        output_bitmap[i] = val * 85;
    }
}
