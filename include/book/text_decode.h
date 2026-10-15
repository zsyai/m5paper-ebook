#pragma once

// Text decoding helpers shared by page rendering and background indexing.
//
// Book files are read in raw chunks. A chunk is trimmed so it never ends in
// the middle of a character, converted to UTF-8 for layout, and the number of
// UTF-8 bytes laid out is mapped back to raw bytes to advance the file offset.

#include <cstddef>
#include <cstdint>
#include <string>

enum class TextEncoding {
  UTF8,
  GBK,
  UTF16LE,
  UTF16BE,
  AUTO_DETECT
};

namespace reader {
namespace book {

// Guesses the encoding from the first bytes of a file (BOM, then a
// UTF-8 / GBK byte-pattern ratio).
TextEncoding DetectEncoding(const uint8_t* buffer, size_t size);

// Returns the length of the longest prefix of `buf` that ends on a character
// boundary for `enc`.
size_t SafeChunkEnd(const uint8_t* buf, size_t len, TextEncoding enc);

// Converts `len` raw bytes to UTF-8. UTF-8 input is returned unchanged.
std::string ToUtf8(const uint8_t* raw, size_t len, TextEncoding enc);

// Returns how many raw bytes of `raw` produce the first `utf8_bytes` bytes of
// ToUtf8(raw).
size_t RawBytesForUtf8Prefix(const uint8_t* raw, size_t len, TextEncoding enc,
                             size_t utf8_bytes);

}  // namespace book
}  // namespace reader
