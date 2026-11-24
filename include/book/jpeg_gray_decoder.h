#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_JPEG_GRAY_DECODER_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_JPEG_GRAY_DECODER_H_

// Self-contained grayscale JPEG decoder supporting Baseline (`SOF0`), Extended
// Sequential (`SOF1`), and Progressive (`SOF2`) Huffman-coded JPEGs.
//
// M5GFX's built-in `TJpgDec` is a single-pass streaming decoder that rejects
// progressive (`SOF2`) and extended-sequential (`SOF1`) JPEGs (common in
// Calibre-exported EPUB covers). Because M5Paper is a 16-grayscale e-ink panel,
// this fallback decodes only the luminance (`Y`) channel in PSRAM, skipping
// non-interleaved chroma scans to cut memory and CPU cost to ~1/3.

#include <cstddef>
#include <cstdint>

namespace reader {
namespace book {

// Probes JPEG frame header (`SOF0`, `SOF1`, or `SOF2`) in `data[0..size)`.
// Returns true and writes positive `out_width`, `out_height`, and `out_sof`
// (`0xC0`, `0xC1`, or `0xC2`) when `data` is a supported 8-bit JPEG header.
bool ProbeJpegHeader(const uint8_t* data, size_t size, int* out_width,
                     int* out_height, uint8_t* out_sof = nullptr);

// Decodes an 8-bit Huffman JPEG (`SOF0` / `SOF1` / `SOF2`, 1-channel grayscale
// or 3-channel YCbCr) from `[data, data + size)` and scales the luminance plane
// to `dst_width * dst_height` 8-bit grayscale pixels (`0 = black .. 255 =
// white`). On success, allocates `*out_gray` (PSRAM preferred; caller frees
// with `std::free()`) and returns true.
bool DecodeJpegGrayscaleScaled(const uint8_t* data, size_t size, int dst_width,
                               int dst_height, uint8_t** out_gray);

}  // namespace book
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_JPEG_GRAY_DECODER_H_
