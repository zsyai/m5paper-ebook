#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_DEBUG_CMD_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_DEBUG_CMD_H_

// Serial E2E command parser and 1-bit PBM screenshot encoding helpers used by
// `m5paper_perf` builds and host unit tests (`[env:native]`).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reader {
namespace core {

// Operations routed through the app queue as `EventType::kDebug`.
enum class DebugOp : uint8_t {
  kPing = 0,
  kState = 1,
  kMem = 2,
  kScreenshot = 3,
  kDropCaches = 4,
  kBooks = 5,
};

struct DebugCommand {
  enum class Kind : uint8_t {
    kEmpty,       // Blank line or '#' comment.
    kInvalid,     // Unrecognized command or out-of-range arguments.
    kTap,         // tap <x> <y> (panel coordinates)
    kLongPress,   // hold <x> <y> / longpress <x> <y>
    kSwipe,       // swipe l|r|u|d or swipe <dx> <dy>
    kWheel,       // wheel <delta>
    kWheelPress,  // press
    kDebugOp,     // ping / state? / status / mem? / screenshot / drop_caches
  };

  Kind kind = Kind::kEmpty;
  int16_t a = 0;
  int16_t b = 0;
  DebugOp op = DebugOp::kPing;
};

// Default pixel travel emitted for `swipe l|r|u|d` (exceeds kMinSwipeDistance).
constexpr int16_t kDefaultDebugSwipePx = 140;

// Parses one newline-stripped command line.
DebugCommand ParseDebugCommand(const char* line);

// Packs `width` 8-bit grayscale pixels (`< 128` -> black bit 1, `>= 128` ->
// white bit 0) into `(width + 7) / 8` netpbm P4 row bytes (MSB = leftmost px,
// trailing pad bits zeroed).
void PackGrayscaleRowTo1BitPbm(const uint8_t* gray, int width,
                               uint8_t* pbm_row);

// Compresses `src[0..len)` with Apple PackBits byte-run RLE:
// - Header `0..127`: copy the next `header + 1` literal bytes.
// - Header `129..255`: repeat the next single byte `257 - header` times.
void RleEncodePackBits(const uint8_t* src, size_t len,
                       std::vector<uint8_t>* out);

// Standard RFC 4648 Base64 encoding without line breaks.
std::string Base64Encode(const uint8_t* src, size_t len);

// Standard IEEE 802.3 CRC-32 checksum.
uint32_t Crc32(const uint8_t* data, size_t len);

}  // namespace core
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_DEBUG_CMD_H_
