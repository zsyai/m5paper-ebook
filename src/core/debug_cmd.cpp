#include "core/debug_cmd.h"

#if READER_PERF || READER_HOST_TEST

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace reader {
namespace core {
namespace {

const char* SkipSpaces(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  return p;
}

std::string NextTokenLower(const char** cursor) {
  const char* p = SkipSpaces(*cursor);
  const char* start = p;
  while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
    ++p;
  }
  *cursor = p;
  std::string out(start, static_cast<size_t>(p - start));
  for (char& ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
}

bool ParseInt16Token(const char** cursor, int lo, int hi, int16_t* out) {
  const char* p = SkipSpaces(*cursor);
  if (*p == '\0') return false;
  bool neg = false;
  if (*p == '+' || *p == '-') {
    neg = (*p == '-');
    ++p;
  }
  if (*p < '0' || *p > '9') return false;
  int32_t acc = 0;
  while (*p >= '0' && *p <= '9') {
    acc = acc * 10 + (*p - '0');
    if (acc > 32768) return false;
    ++p;
  }
  if (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
    return false;
  }
  const int32_t v = neg ? -acc : acc;
  if (v < lo || v > hi) return false;
  *out = static_cast<int16_t>(v);
  *cursor = p;
  return true;
}

bool AtEnd(const char* cursor) {
  const char* p = SkipSpaces(cursor);
  return *p == '\0' || *p == '#';
}

DebugCommand InvalidCmd() {
  DebugCommand cmd;
  cmd.kind = DebugCommand::Kind::kInvalid;
  return cmd;
}

}  // namespace

DebugCommand ParseDebugCommand(const char* line) {
  DebugCommand cmd;
  if (line == nullptr) return cmd;
  const char* p = SkipSpaces(line);
  if (*p == '\0' || *p == '#') return cmd;

  const std::string verb = NextTokenLower(&p);
  if (verb == "tap" || verb == "hold" || verb == "longpress") {
    int16_t x = 0;
    int16_t y = 0;
    if (!ParseInt16Token(&p, 0, 4095, &x) ||
        !ParseInt16Token(&p, 0, 4095, &y) || !AtEnd(p)) {
      return InvalidCmd();
    }
    cmd.kind = (verb == "tap") ? DebugCommand::Kind::kTap
                               : DebugCommand::Kind::kLongPress;
    cmd.a = x;
    cmd.b = y;
    return cmd;
  }

  if (verb == "swipe") {
    const char* saved = p;
    int16_t dx = 0;
    int16_t dy = 0;
    if (ParseInt16Token(&p, -4095, 4095, &dx) &&
        ParseInt16Token(&p, -4095, 4095, &dy) && AtEnd(p)) {
      cmd.kind = DebugCommand::Kind::kSwipe;
      cmd.a = dx;
      cmd.b = dy;
      return cmd;
    }
    p = saved;
    const std::string dir = NextTokenLower(&p);
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kSwipe;
    if (dir == "l" || dir == "left") {
      cmd.a = -kDefaultDebugSwipePx;
    } else if (dir == "r" || dir == "right") {
      cmd.a = kDefaultDebugSwipePx;
    } else if (dir == "u" || dir == "up") {
      cmd.b = -kDefaultDebugSwipePx;
    } else if (dir == "d" || dir == "down") {
      cmd.b = kDefaultDebugSwipePx;
    } else {
      return InvalidCmd();
    }
    return cmd;
  }

  if (verb == "wheel") {
    int16_t delta = 0;
    if (!ParseInt16Token(&p, -64, 64, &delta) || delta == 0 || !AtEnd(p)) {
      return InvalidCmd();
    }
    cmd.kind = DebugCommand::Kind::kWheel;
    cmd.a = delta;
    return cmd;
  }

  if (verb == "press") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kWheelPress;
    return cmd;
  }

  if (verb == "ping") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kDebugOp;
    cmd.op = DebugOp::kPing;
    return cmd;
  }

  if (verb == "state?" || verb == "status") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kDebugOp;
    cmd.op = DebugOp::kState;
    return cmd;
  }

  if (verb == "mem?") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kDebugOp;
    cmd.op = DebugOp::kMem;
    return cmd;
  }

  if (verb == "screenshot") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kDebugOp;
    cmd.op = DebugOp::kScreenshot;
    return cmd;
  }

  if (verb == "drop_caches") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kDebugOp;
    cmd.op = DebugOp::kDropCaches;
    return cmd;
  }

  if (verb == "books?") {
    if (!AtEnd(p)) return InvalidCmd();
    cmd.kind = DebugCommand::Kind::kDebugOp;
    cmd.op = DebugOp::kBooks;
    return cmd;
  }

  return InvalidCmd();
}

void PackGrayscaleRowTo1BitPbm(const uint8_t* gray, int width,
                               uint8_t* pbm_row) {
  if (width <= 0) return;
  const int row_bytes = (width + 7) / 8;
  std::memset(pbm_row, 0, static_cast<size_t>(row_bytes));
  for (int x = 0; x < width; ++x) {
    if (gray[x] < 128) {
      pbm_row[x >> 3] |= static_cast<uint8_t>(0x80u >> (x & 7));
    }
  }
}

void RleEncodePackBits(const uint8_t* src, size_t len,
                       std::vector<uint8_t>* out) {
  out->clear();
  size_t i = 0;
  while (i < len) {
    size_t run = 1;
    while (i + run < len && run < 128 && src[i + run] == src[i]) {
      ++run;
    }
    if (run >= 3) {
      out->push_back(static_cast<uint8_t>(257 - run));
      out->push_back(src[i]);
      i += run;
      continue;
    }
    const size_t lit_start = i;
    size_t lit_len = 0;
    while (i < len && lit_len < 128) {
      size_t ahead = 1;
      while (i + ahead < len && ahead < 3 && src[i + ahead] == src[i]) {
        ++ahead;
      }
      if (ahead >= 3) break;
      ++i;
      ++lit_len;
    }
    out->push_back(static_cast<uint8_t>(lit_len - 1));
    out->insert(out->end(), src + lit_start, src + lit_start + lit_len);
  }
}

std::string Base64Encode(const uint8_t* src, size_t len) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t v = (static_cast<uint32_t>(src[i]) << 16) |
                       (static_cast<uint32_t>(src[i + 1]) << 8) | src[i + 2];
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out.push_back(kTable[(v >> 6) & 0x3F]);
    out.push_back(kTable[v & 0x3F]);
    i += 3;
  }
  const size_t rem = len - i;
  if (rem == 1) {
    const uint32_t v = static_cast<uint32_t>(src[i]) << 16;
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    const uint32_t v = (static_cast<uint32_t>(src[i]) << 16) |
                       (static_cast<uint32_t>(src[i + 1]) << 8);
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out.push_back(kTable[(v >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

uint32_t Crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

}  // namespace core
}  // namespace reader

#endif  // READER_PERF || READER_HOST_TEST
