#include "book/jpeg_gray_decoder.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace reader {
namespace book {
namespace {

constexpr int kMaxImageDim = 4096;
constexpr int kMaxDestDim = 2048;
constexpr uint64_t kMaxImagePixels = 12000000ULL;
// Up to ~1.05 MP (`<= 2.1 MB` of `int16_t` coefficients) uses full 8x8 IDCT;
// larger images fall back to 2x2 per-block frequency storage (`8 B`/block).
constexpr size_t kMaxFullIdctBlocks = 16500;

constexpr int kFastBits = 9;
constexpr int kFastMask = (1 << kFastBits) - 1;

constexpr uint8_t kZigZag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

void* AllocPsramPreferred(size_t bytes, bool allow_internal_fallback) {
  if (bytes == 0) return nullptr;
#if !defined(READER_HOST_TEST) && defined(BOARD_HAS_PSRAM)
  void* ptr = ps_malloc(bytes);
  if (ptr != nullptr || !allow_internal_fallback) return ptr;
#else
  (void)allow_internal_fallback;
#endif
  return std::malloc(bytes);
}

int16_t ClampInt16(int32_t value) {
  if (value < -32768) return -32768;
  if (value > 32767) return 32767;
  return static_cast<int16_t>(value);
}

uint8_t ClampUint8(int32_t value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

struct HuffTable {
  bool valid = false;
  uint8_t values[256] = {};
  int32_t maxcode[18] = {};
  int32_t delta[17] = {};
  // Packed `(code_len << 8) | symbol`, or `0xFFFF` when `code_len > kFastBits`.
  uint16_t fast[1 << kFastBits] = {};
};

bool BuildHuffTable(const uint8_t counts[16], const uint8_t* syms,
                    int total_syms, HuffTable* out) {
  out->valid = false;
  for (int i = 0; i < (1 << kFastBits); ++i) out->fast[i] = 0xFFFFu;
  std::memcpy(out->values, syms, static_cast<size_t>(total_syms));

  int code = 0;
  int k = 0;
  for (int bits = 1; bits <= 16; ++bits) {
    out->delta[bits] = k - code;
    const int cnt = counts[bits - 1];
    for (int i = 0; i < cnt; ++i) {
      if (code >= (1 << bits)) return false;
      if (bits <= kFastBits) {
        const int prefix = code << (kFastBits - bits);
        const int span = 1 << (kFastBits - bits);
        const uint16_t entry =
            static_cast<uint16_t>((bits << 8) | out->values[k]);
        for (int j = 0; j < span; ++j) {
          out->fast[prefix + j] = entry;
        }
      }
      ++code;
      ++k;
    }
    out->maxcode[bits] = (cnt > 0) ? (code - 1) : -1;
    code <<= 1;
  }
  out->maxcode[17] = 0x7FFFFFFF;
  out->valid = true;
  return true;
}

struct ComponentInfo {
  uint8_t id = 0;
  uint8_t h = 1;
  uint8_t v = 1;
  uint8_t tq = 0;
  uint8_t hd = 0;
  uint8_t ha = 0;
  int non_interleaved_w_px = 0;
  int non_interleaved_h_px = 0;
};

constexpr int32_t FixConst(double v) {
  return static_cast<int32_t>(v * 4096.0 + (v >= 0.0 ? 0.5 : -0.5));
}

void Idct1D(int32_t s0, int32_t s1, int32_t s2, int32_t s3, int32_t s4,
            int32_t s5, int32_t s6, int32_t s7, int32_t* out_x0,
            int32_t* out_x1, int32_t* out_x2, int32_t* out_x3, int32_t* out_t0,
            int32_t* out_t1, int32_t* out_t2, int32_t* out_t3) {
  int32_t p2 = s2;
  int32_t p3 = s6;
  int32_t p1 = (p2 + p3) * FixConst(0.5411961);
  int32_t t2 = p1 + p3 * FixConst(-1.847759065);
  int32_t t3 = p1 + p2 * FixConst(0.765366865);
  p2 = s0;
  p3 = s4;
  int32_t t0 = (p2 + p3) * 4096;
  int32_t t1 = (p2 - p3) * 4096;
  *out_x0 = t0 + t3;
  *out_x3 = t0 - t3;
  *out_x1 = t1 + t2;
  *out_x2 = t1 - t2;

  t0 = s7;
  t1 = s5;
  t2 = s3;
  t3 = s1;
  p3 = t0 + t2;
  int32_t p4 = t1 + t3;
  p1 = t0 + t3;
  p2 = t1 + t2;
  const int32_t p5 = (p3 + p4) * FixConst(1.175875602);
  t0 = t0 * FixConst(0.298631336);
  t1 = t1 * FixConst(2.053119869);
  t2 = t2 * FixConst(3.072711026);
  t3 = t3 * FixConst(1.501321110);
  p1 = p5 + p1 * FixConst(-0.899976223);
  p2 = p5 + p2 * FixConst(-2.562915447);
  p3 = p3 * FixConst(-1.961570560);
  p4 = p4 * FixConst(-0.390180644);
  *out_t3 = t3 + p1 + p4;
  *out_t2 = t2 + p2 + p3;
  *out_t1 = t1 + p2 + p4;
  *out_t0 = t0 + p1 + p3;
}

void DequantizeAndIdct8x8(const int16_t block[64], const uint16_t qt[64],
                          uint8_t* out, int out_stride) {
  int32_t inter[64];
  for (int col = 0; col < 8; ++col) {
    if (block[8 + col] == 0 && block[16 + col] == 0 && block[24 + col] == 0 &&
        block[32 + col] == 0 && block[40 + col] == 0 && block[48 + col] == 0 &&
        block[56 + col] == 0) {
      const int32_t dc = static_cast<int32_t>(block[col]) *
                         static_cast<int32_t>(qt[col]) * 4;
      for (int row = 0; row < 8; ++row) {
        inter[row * 8 + col] = dc;
      }
      continue;
    }
    int32_t x0, x1, x2, x3, t0, t1, t2, t3;
    Idct1D(static_cast<int32_t>(block[col]) * qt[col],
           static_cast<int32_t>(block[8 + col]) * qt[8 + col],
           static_cast<int32_t>(block[16 + col]) * qt[16 + col],
           static_cast<int32_t>(block[24 + col]) * qt[24 + col],
           static_cast<int32_t>(block[32 + col]) * qt[32 + col],
           static_cast<int32_t>(block[40 + col]) * qt[40 + col],
           static_cast<int32_t>(block[48 + col]) * qt[48 + col],
           static_cast<int32_t>(block[56 + col]) * qt[56 + col], &x0, &x1, &x2,
           &x3, &t0, &t1, &t2, &t3);
    x0 += 512;
    x1 += 512;
    x2 += 512;
    x3 += 512;
    inter[0 * 8 + col] = (x0 + t3) >> 10;
    inter[7 * 8 + col] = (x0 - t3) >> 10;
    inter[1 * 8 + col] = (x1 + t2) >> 10;
    inter[6 * 8 + col] = (x1 - t2) >> 10;
    inter[2 * 8 + col] = (x2 + t1) >> 10;
    inter[5 * 8 + col] = (x2 - t1) >> 10;
    inter[3 * 8 + col] = (x3 + t0) >> 10;
    inter[4 * 8 + col] = (x3 - t0) >> 10;
  }

  constexpr int32_t kRowBias = 65536 + (128 << 17);
  for (int row = 0; row < 8; ++row) {
    const int32_t* v = &inter[row * 8];
    int32_t x0, x1, x2, x3, t0, t1, t2, t3;
    Idct1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], &x0, &x1, &x2, &x3,
           &t0, &t1, &t2, &t3);
    x0 += kRowBias;
    x1 += kRowBias;
    x2 += kRowBias;
    x3 += kRowBias;
    uint8_t* dst = out + row * out_stride;
    dst[0] = ClampUint8((x0 + t3) >> 17);
    dst[7] = ClampUint8((x0 - t3) >> 17);
    dst[1] = ClampUint8((x1 + t2) >> 17);
    dst[6] = ClampUint8((x1 - t2) >> 17);
    dst[2] = ClampUint8((x2 + t1) >> 17);
    dst[5] = ClampUint8((x2 - t1) >> 17);
    dst[3] = ClampUint8((x3 + t0) >> 17);
    dst[4] = ClampUint8((x3 - t0) >> 17);
  }
}

void DequantizeAndIdct2x2(const int16_t block[4], const uint16_t qt[64],
                          uint8_t* out, int out_stride) {
  // Exact 2x2 low-frequency IDCT over `(0,0), (0,1), (1,0), (1,1)` (`alpha =
  // sqrt(2) * cos(pi / 16) = 1.387039845`, `alpha^2 = 1.923879533`).
  const int32_t f00 =
      (static_cast<int32_t>(block[0]) * static_cast<int32_t>(qt[0])) << 10;
  const int32_t f01 = static_cast<int32_t>(block[1]) *
                      static_cast<int32_t>(qt[1]) * 1420;
  const int32_t f10 = static_cast<int32_t>(block[2]) *
                      static_cast<int32_t>(qt[8]) * 1420;
  const int32_t f11 = static_cast<int32_t>(block[3]) *
                      static_cast<int32_t>(qt[9]) * 1970;
  constexpr int32_t kRound = 4096;
  out[0] = ClampUint8(128 + ((f00 + f01 + f10 + f11 + kRound) >> 13));
  out[1] = ClampUint8(128 + ((f00 - f01 + f10 - f11 + kRound) >> 13));
  out[out_stride + 0] =
      ClampUint8(128 + ((f00 + f01 - f10 - f11 + kRound) >> 13));
  out[out_stride + 1] =
      ClampUint8(128 + ((f00 - f01 - f10 + f11 + kRound) >> 13));
}

struct JpegDecoder {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t pos = 0;

  bool progressive = false;
  bool frame_parsed = false;
  int width = 0;
  int height = 0;
  int num_comps = 0;
  int luma_comp = 0;
  int h_max = 1;
  int v_max = 1;
  int mcu_x = 0;
  int mcu_y = 0;
  int luma_blocks_x = 0;
  int luma_blocks_y = 0;
  bool reduced_2x2 = false;

  uint16_t qt[4][64] = {};
  bool qt_valid[4] = {};
  HuffTable huff_dc[4];
  HuffTable huff_ac[4];
  ComponentInfo comps[4];

  int restart_interval = 0;
  int scan_n = 0;
  int scan_order[4] = {};
  int spec_start = 0;
  int spec_end = 63;
  int succ_high = 0;
  int succ_low = 0;

  uint32_t bit_buf = 0;
  int bit_count = 0;
  int fake_bits = 0;
  uint8_t pending_marker = 0;
  int32_t dc_pred[4] = {};
  int eob_run = 0;

  int16_t* coeffs = nullptr;

  ~JpegDecoder() { std::free(coeffs); }

  bool ReadByte(uint8_t* out) {
    if (pos >= size) return false;
    *out = data[pos++];
    return true;
  }

  bool ReadU16BE(uint16_t* out) {
    if (pos + 2 > size) return false;
    *out = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
    pos += 2;
    return true;
  }

  bool NextMarker(uint8_t* out_marker) {
    if (pending_marker != 0) {
      *out_marker = pending_marker;
      pending_marker = 0;
      return true;
    }
    while (pos < size) {
      uint8_t b = data[pos++];
      if (b != 0xFF) continue;
      while (pos < size && (b = data[pos++]) == 0xFF) {
      }
      if (b == 0xFF) return false;
      if (b == 0x00 || (b >= 0xD0 && b <= 0xD7)) continue;
      *out_marker = b;
      return true;
    }
    return false;
  }

  void ResetBitStream() {
    bit_buf = 0;
    bit_count = 0;
    fake_bits = 0;
  }

  void FillBits() {
    while (bit_count <= 24) {
      if (pending_marker != 0) {
        bit_buf <<= 8;
        bit_count += 8;
        fake_bits += 8;
        continue;
      }
      if (pos >= size) {
        pending_marker = 0xD9;
        continue;
      }
      const uint8_t b = data[pos++];
      if (b != 0xFF) {
        bit_buf = (bit_buf << 8) | b;
        bit_count += 8;
        continue;
      }
      uint8_t m = 0xFF;
      while (pos < size && (m = data[pos++]) == 0xFF) {
      }
      if (m == 0xFF) {
        pending_marker = 0xD9;
      } else if (m == 0x00) {
        bit_buf = (bit_buf << 8) | 0xFFu;
        bit_count += 8;
      } else {
        pending_marker = m;
      }
    }
  }

  int GetBits(int n) {
    if (n == 0) return 0;
    if (bit_count < n) FillBits();
    if (bit_count - n < fake_bits) return -1;
    const int val =
        static_cast<int>((bit_buf >> (bit_count - n)) & ((1u << n) - 1u));
    bit_count -= n;
    return val;
  }

  int GetBit() { return GetBits(1); }

  bool ExtendReceive(int n, int* out_val) {
    const int bits = GetBits(n);
    if (bits < 0) return false;
    const int half = 1 << (n - 1);
    *out_val = (bits < half) ? (bits - ((1 << n) - 1)) : bits;
    return true;
  }

  int HuffDecode(const HuffTable& ht) {
    if (bit_count < 16) FillBits();
    const int look =
        static_cast<int>((bit_buf >> (bit_count - kFastBits)) & kFastMask);
    const uint16_t fast_entry = ht.fast[look];
    if (fast_entry != 0xFFFFu) {
      const int len = fast_entry >> 8;
      if (bit_count - len < fake_bits) return -1;
      bit_count -= len;
      return fast_entry & 0xFF;
    }
    int code =
        static_cast<int>((bit_buf >> (bit_count - kFastBits)) & kFastMask);
    for (int bits = kFastBits + 1; bits <= 16; ++bits) {
      code = (code << 1) |
             static_cast<int>((bit_buf >> (bit_count - bits)) & 1u);
      if (code <= ht.maxcode[bits]) {
        if (bit_count - bits < fake_bits) return -1;
        bit_count -= bits;
        const int idx = code + ht.delta[bits];
        if (idx < 0 || idx >= 256) return -1;
        return ht.values[idx];
      }
    }
    return -1;
  }

  bool ConsumeRestartMarker() {
    ResetBitStream();
    if (pending_marker == 0) {
      while (pos < size) {
        uint8_t b = data[pos++];
        if (b != 0xFF) continue;
        while (pos < size && (b = data[pos++]) == 0xFF) {
        }
        if (b == 0x00) continue;
        pending_marker = b;
        break;
      }
    }
    if (pending_marker < 0xD0 || pending_marker > 0xD7) return false;
    pending_marker = 0;
    std::memset(dc_pred, 0, sizeof(dc_pred));
    eob_run = 0;
    return true;
  }

  bool ParseDqt() {
    uint16_t seg_len = 0;
    if (!ReadU16BE(&seg_len) || seg_len < 2) return false;
    int rem = static_cast<int>(seg_len) - 2;
    if (pos + static_cast<size_t>(rem) > size) return false;
    while (rem > 0) {
      const uint8_t info = data[pos++];
      --rem;
      const int pq = info >> 4;
      const int tq = info & 0x0F;
      if ((pq != 0 && pq != 1) || tq > 3) return false;
      const int entry_bytes = (pq == 0) ? 64 : 128;
      if (rem < entry_bytes) return false;
      for (int i = 0; i < 64; ++i) {
        uint16_t val = 0;
        if (pq == 0) {
          val = data[pos++];
        } else {
          val = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
          pos += 2;
        }
        if (val == 0) return false;
        qt[tq][kZigZag[i]] = val;
      }
      qt_valid[tq] = true;
      rem -= entry_bytes;
    }
    return rem == 0;
  }

  bool ParseDht() {
    uint16_t seg_len = 0;
    if (!ReadU16BE(&seg_len) || seg_len < 2) return false;
    int rem = static_cast<int>(seg_len) - 2;
    if (pos + static_cast<size_t>(rem) > size) return false;
    while (rem > 0) {
      if (rem < 17) return false;
      const uint8_t info = data[pos++];
      const int tc = info >> 4;
      const int th = info & 0x0F;
      if (tc > 1 || th > 3) return false;
      const uint8_t* counts = &data[pos];
      pos += 16;
      rem -= 17;
      int total = 0;
      for (int i = 0; i < 16; ++i) total += counts[i];
      if (total > 256 || rem < total) return false;
      HuffTable* dst = (tc == 0) ? &huff_dc[th] : &huff_ac[th];
      if (!BuildHuffTable(counts, &data[pos], total, dst)) return false;
      pos += static_cast<size_t>(total);
      rem -= total;
    }
    return rem == 0;
  }

  bool ParseDri() {
    uint16_t seg_len = 0;
    if (!ReadU16BE(&seg_len) || seg_len != 4) return false;
    uint16_t ri = 0;
    if (!ReadU16BE(&ri)) return false;
    restart_interval = ri;
    return true;
  }

  bool ParseSof(uint8_t marker) {
    if (frame_parsed) return false;
    progressive = (marker == 0xC2);
    uint16_t seg_len = 0;
    if (!ReadU16BE(&seg_len) || seg_len < 11) return false;
    if (pos + seg_len - 2 > size) return false;
    const uint8_t precision = data[pos++];
    uint16_t h16 = 0;
    uint16_t w16 = 0;
    if (!ReadU16BE(&h16) || !ReadU16BE(&w16)) return false;
    const uint8_t nf = data[pos++];
    if (precision != 8 || w16 == 0 || h16 == 0 || w16 > kMaxImageDim ||
        h16 > kMaxImageDim || (nf != 1 && nf != 3)) {
      return false;
    }
    if (seg_len != static_cast<uint16_t>(8 + 3 * nf)) return false;
    if (static_cast<uint64_t>(w16) * h16 > kMaxImagePixels) return false;

    width = w16;
    height = h16;
    num_comps = nf;
    h_max = 1;
    v_max = 1;
    for (int i = 0; i < num_comps; ++i) {
      comps[i].id = data[pos++];
      const uint8_t hv = data[pos++];
      comps[i].h = hv >> 4;
      comps[i].v = hv & 0x0F;
      comps[i].tq = data[pos++];
      if (comps[i].h == 0 || comps[i].h > 4 || comps[i].v == 0 ||
          comps[i].v > 4 || comps[i].tq > 3) {
        return false;
      }
      h_max = std::max<int>(h_max, comps[i].h);
      v_max = std::max<int>(v_max, comps[i].v);
    }
    for (int i = 0; i < num_comps; ++i) {
      if (h_max % comps[i].h != 0 || v_max % comps[i].v != 0) return false;
      comps[i].non_interleaved_w_px =
          (width * comps[i].h + h_max - 1) / h_max;
      comps[i].non_interleaved_h_px =
          (height * comps[i].v + v_max - 1) / v_max;
    }

    // For rare RGB-tagged 3-component JPEGs, component 1 ('G') carries ~59% of
    // luminance; for standard JFIF/Exif YCbCr or 1-channel JPEGs, component 0
    // is `Y`.
    if (num_comps == 3 && comps[0].id == 'R' && comps[1].id == 'G' &&
        comps[2].id == 'B') {
      luma_comp = 1;
    } else {
      luma_comp = 0;
    }

    mcu_x = (width + h_max * 8 - 1) / (h_max * 8);
    mcu_y = (height + v_max * 8 - 1) / (v_max * 8);
    luma_blocks_x = mcu_x * comps[luma_comp].h;
    luma_blocks_y = mcu_y * comps[luma_comp].v;
    const size_t total_blocks =
        static_cast<size_t>(luma_blocks_x) * static_cast<size_t>(luma_blocks_y);

    if (total_blocks <= kMaxFullIdctBlocks) {
      const size_t full_bytes = total_blocks * 64 * sizeof(int16_t);
      coeffs = static_cast<int16_t*>(
          AllocPsramPreferred(full_bytes, /*allow_internal_fallback=*/false));
      if (coeffs != nullptr) {
        std::memset(coeffs, 0, full_bytes);
        reduced_2x2 = false;
        frame_parsed = true;
        return true;
      }
    }

    const size_t reduced_bytes = total_blocks * 4 * sizeof(int16_t);
    coeffs = static_cast<int16_t*>(
        AllocPsramPreferred(reduced_bytes, /*allow_internal_fallback=*/false));
    if (coeffs == nullptr) return false;
    std::memset(coeffs, 0, reduced_bytes);
    reduced_2x2 = true;
    frame_parsed = true;
    return true;
  }

  void StoreCoeff(int16_t* block_ptr, int zig_pos, int16_t val) {
    const uint8_t nat = kZigZag[zig_pos];
    if (!reduced_2x2) {
      block_ptr[nat] = val;
      return;
    }
    if (nat == 0) {
      block_ptr[0] = val;
    } else if (nat == 1) {
      block_ptr[1] = val;
    } else if (nat == 8) {
      block_ptr[2] = val;
    } else if (nat == 9) {
      block_ptr[3] = val;
    }
  }

  bool DecodeSequentialBlock(int comp_idx, int16_t* block_ptr) {
    const HuffTable& hdc = huff_dc[comps[comp_idx].hd];
    const HuffTable& hac = huff_ac[comps[comp_idx].ha];
    if (!hdc.valid || !hac.valid) return false;

    const int t = HuffDecode(hdc);
    if (t < 0 || t > 15) return false;
    int diff = 0;
    if (t > 0 && !ExtendReceive(t, &diff)) return false;
    const int32_t dc = dc_pred[comp_idx] + diff;
    dc_pred[comp_idx] = dc;
    if (block_ptr != nullptr) {
      StoreCoeff(block_ptr, 0, ClampInt16(dc));
    }

    int k = 1;
    while (k < 64) {
      const int rs = HuffDecode(hac);
      if (rs < 0) return false;
      const int r = rs >> 4;
      const int s = rs & 0x0F;
      if (s == 0) {
        if (r != 15) break;
        k += 16;
      } else {
        k += r;
        if (k >= 64) return false;
        int ac = 0;
        if (!ExtendReceive(s, &ac)) return false;
        if (block_ptr != nullptr) {
          StoreCoeff(block_ptr, k, ClampInt16(ac));
        }
        ++k;
      }
    }
    return true;
  }

  bool DecodeProgDcBlock(int comp_idx, int16_t* block_ptr) {
    if (succ_high == 0) {
      const HuffTable& hdc = huff_dc[comps[comp_idx].hd];
      if (!hdc.valid) return false;
      const int t = HuffDecode(hdc);
      if (t < 0 || t > 15) return false;
      int diff = 0;
      if (t > 0 && !ExtendReceive(t, &diff)) return false;
      const int32_t dc = dc_pred[comp_idx] + diff;
      dc_pred[comp_idx] = dc;
      if (block_ptr != nullptr) {
        StoreCoeff(block_ptr, 0, ClampInt16(dc * (1 << succ_low)));
      }
      return true;
    }
    const int b = GetBit();
    if (b < 0) return false;
    if (b != 0 && block_ptr != nullptr) {
      block_ptr[0] = ClampInt16(static_cast<int32_t>(block_ptr[0]) +
                                (1 << succ_low));
    }
    return true;
  }

  bool RefineCoeff(int16_t* p, int16_t bit) {
    const int b = GetBit();
    if (b < 0) return false;
    if (b != 0 && (*p & bit) == 0) {
      *p = (*p > 0) ? ClampInt16(static_cast<int32_t>(*p) + bit)
                    : ClampInt16(static_cast<int32_t>(*p) - bit);
    }
    return true;
  }

  bool DecodeProgAcBlock(int comp_idx, int16_t* block_ptr) {
    const HuffTable& hac = huff_ac[comps[comp_idx].ha];
    if (!hac.valid) return false;

    if (succ_high == 0) {
      if (eob_run > 0) {
        --eob_run;
        return true;
      }
      int k = spec_start;
      while (k <= spec_end) {
        const int rs = HuffDecode(hac);
        if (rs < 0) return false;
        const int r = rs >> 4;
        const int s = rs & 0x0F;
        if (s == 0) {
          if (r < 15) {
            const int extra = (r > 0) ? GetBits(r) : 0;
            if (extra < 0) return false;
            eob_run = ((1 << r) - 1) + extra;
            break;
          }
          k += 16;
        } else {
          k += r;
          if (k > spec_end) return false;
          int ac = 0;
          if (!ExtendReceive(s, &ac)) return false;
          if (block_ptr != nullptr) {
            StoreCoeff(block_ptr, k, ClampInt16(ac * (1 << succ_low)));
          }
          ++k;
        }
      }
      return true;
    }

    // AC refinement scan (`succ_high > 0`), executed in full `8x8` mode.
    const int16_t bit = static_cast<int16_t>(1 << succ_low);
    if (eob_run > 0) {
      --eob_run;
      for (int k = spec_start; k <= spec_end; ++k) {
        int16_t* p = &block_ptr[kZigZag[k]];
        if (*p != 0 && !RefineCoeff(p, bit)) return false;
      }
      return true;
    }

    int k = spec_start;
    while (k <= spec_end) {
      const int rs = HuffDecode(hac);
      if (rs < 0) return false;
      int r = rs >> 4;
      const int s = rs & 0x0F;
      int16_t new_val = 0;
      if (s == 0) {
        if (r < 15) {
          const int extra = (r > 0) ? GetBits(r) : 0;
          if (extra < 0) return false;
          eob_run = ((1 << r) - 1) + extra;
          for (; k <= spec_end; ++k) {
            int16_t* p = &block_ptr[kZigZag[k]];
            if (*p != 0 && !RefineCoeff(p, bit)) return false;
          }
          break;
        }
        // `r == 15, s == 0` (`ZRL`): skip 15 zeros and leave the 16th at 0.
      } else {
        if (s != 1) return false;
        const int sign = GetBit();
        if (sign < 0) return false;
        new_val = sign ? bit : static_cast<int16_t>(-bit);
      }

      while (k <= spec_end) {
        int16_t* p = &block_ptr[kZigZag[k++]];
        if (*p != 0) {
          if (!RefineCoeff(p, bit)) return false;
        } else {
          if (r == 0) {
            *p = new_val;
            break;
          }
          --r;
        }
      }
    }
    return true;
  }

  bool DecodeBlock(int comp_idx, int16_t* block_ptr) {
    if (!progressive) {
      return DecodeSequentialBlock(comp_idx, block_ptr);
    }
    if (spec_start == 0) {
      return DecodeProgDcBlock(comp_idx, block_ptr);
    }
    return DecodeProgAcBlock(comp_idx, block_ptr);
  }

  int16_t* LumaBlockPtr(int bx, int by) {
    const size_t stride = reduced_2x2 ? 4u : 64u;
    const size_t idx =
        (static_cast<size_t>(by) * static_cast<size_t>(luma_blocks_x) +
         static_cast<size_t>(bx)) *
        stride;
    return &coeffs[idx];
  }

  bool ParseAndDecodeSos() {
    if (!frame_parsed) return false;
    uint16_t seg_len = 0;
    if (!ReadU16BE(&seg_len) || seg_len < 8) return false;
    if (pos + seg_len - 2 > size) return false;
    scan_n = data[pos++];
    if (scan_n < 1 || scan_n > num_comps ||
        seg_len != static_cast<uint16_t>(6 + 2 * scan_n)) {
      return false;
    }
    for (int i = 0; i < scan_n; ++i) {
      const uint8_t cid = data[pos++];
      const uint8_t td_ta = data[pos++];
      int match = -1;
      for (int c = 0; c < num_comps; ++c) {
        if (comps[c].id == cid) {
          match = c;
          break;
        }
      }
      if (match < 0) return false;
      comps[match].hd = td_ta >> 4;
      comps[match].ha = td_ta & 0x0F;
      if (comps[match].hd > 3 || comps[match].ha > 3) return false;
      scan_order[i] = match;
    }
    spec_start = data[pos++];
    spec_end = data[pos++];
    const uint8_t ah_al = data[pos++];
    succ_high = ah_al >> 4;
    succ_low = ah_al & 0x0F;

    if (progressive) {
      if (spec_start > 63 || spec_end > 63 || spec_start > spec_end ||
          (spec_start == 0 && spec_end != 0) || succ_high > 13 ||
          succ_low > 13) {
        return false;
      }
      if (spec_start > 0 && scan_n != 1) return false;
    } else {
      if (spec_start != 0 || spec_end != 63 || succ_high != 0 ||
          succ_low != 0) {
        return false;
      }
    }

    // Fast skip: non-interleaved chroma scans and AC refinement scans in
    // reduced 2x2 mode do not affect the decoded luminance plane.
    if ((scan_n == 1 && scan_order[0] != luma_comp) ||
        (reduced_2x2 && spec_start > 0 && succ_high > 0)) {
      pending_marker = 0;
      ResetBitStream();
      return true;
    }

    ResetBitStream();
    std::memset(dc_pred, 0, sizeof(dc_pred));
    eob_run = 0;

    if (scan_n == 1) {
      const int c = scan_order[0];
      const int sb_x = (comps[c].non_interleaved_w_px + 7) / 8;
      const int sb_y = (comps[c].non_interleaved_h_px + 7) / 8;
      const int total_mcus = sb_x * sb_y;
      int todo = restart_interval;
      int done_mcus = 0;
      for (int by = 0; by < sb_y; ++by) {
        for (int bx = 0; bx < sb_x; ++bx) {
          int16_t* blk = (c == luma_comp && bx < luma_blocks_x &&
                          by < luma_blocks_y)
                             ? LumaBlockPtr(bx, by)
                             : nullptr;
          if (!DecodeBlock(c, blk)) return false;
          ++done_mcus;
          if (restart_interval > 0 && done_mcus < total_mcus && --todo == 0) {
            if (!ConsumeRestartMarker()) return false;
            todo = restart_interval;
          }
        }
      }
      return true;
    }

    const int total_mcus = mcu_x * mcu_y;
    int todo = restart_interval;
    int done_mcus = 0;
    for (int my = 0; my < mcu_y; ++my) {
      for (int mx = 0; mx < mcu_x; ++mx) {
        for (int s = 0; s < scan_n; ++s) {
          const int c = scan_order[s];
          for (int vy = 0; vy < comps[c].v; ++vy) {
            for (int hx = 0; hx < comps[c].h; ++hx) {
              int16_t* blk =
                  (c == luma_comp)
                      ? LumaBlockPtr(mx * comps[c].h + hx, my * comps[c].v + vy)
                      : nullptr;
              if (!DecodeBlock(c, blk)) return false;
            }
          }
        }
        ++done_mcus;
        if (restart_interval > 0 && done_mcus < total_mcus && --todo == 0) {
          if (!ConsumeRestartMarker()) return false;
          todo = restart_interval;
        }
      }
    }
    return true;
  }

  bool SkipSegment() {
    uint16_t seg_len = 0;
    if (!ReadU16BE(&seg_len) || seg_len < 2) return false;
    const size_t skip = static_cast<size_t>(seg_len - 2);
    if (pos + skip > size) return false;
    pos += skip;
    return true;
  }

  bool DecodeStream() {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    pos = 2;
    bool decoded_any_scan = false;
    uint8_t marker = 0;
    while (NextMarker(&marker)) {
      if (marker == 0xD9) break;  // EOI
      if (marker == 0xD8 || marker == 0x01) continue;
      if (marker == 0xDB) {
        if (!ParseDqt()) return false;
      } else if (marker == 0xC4) {
        if (!ParseDht()) return false;
      } else if (marker == 0xDD) {
        if (!ParseDri()) return false;
      } else if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
        if (!ParseSof(marker)) return false;
      } else if (marker == 0xDA) {
        if (!ParseAndDecodeSos()) return false;
        decoded_any_scan = true;
      } else if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
        if (!SkipSegment()) return false;
      } else {
        // Unsupported SOF (lossless/arithmetic/12-bit) or corrupt marker.
        return false;
      }
    }
    return frame_parsed && decoded_any_scan &&
           qt_valid[comps[luma_comp].tq];
  }

  bool RenderScaled(int dst_w, int dst_h, uint8_t** out_gray) {
    const uint16_t* luma_qt = qt[comps[luma_comp].tq];
    const int comp_w = comps[luma_comp].non_interleaved_w_px;
    const int comp_h = comps[luma_comp].non_interleaved_h_px;
    const int eff_w = reduced_2x2 ? std::max(1, (comp_w + 3) / 4) : comp_w;
    const int eff_h = reduced_2x2 ? std::max(1, (comp_h + 3) / 4) : comp_h;
    const int block_px = reduced_2x2 ? 2 : 8;
    const int strip_stride = luma_blocks_x * block_px;

    std::unique_ptr<uint8_t, decltype(&std::free)> strip_buf(
        static_cast<uint8_t*>(AllocPsramPreferred(
            static_cast<size_t>(strip_stride) * block_px,
            /*allow_internal_fallback=*/true)),
        &std::free);
    if (!strip_buf) return false;

    const size_t out_bytes =
        static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h);
    uint8_t* dst = static_cast<uint8_t*>(
        AllocPsramPreferred(out_bytes, /*allow_internal_fallback=*/true));
    if (dst == nullptr) return false;

    int loaded_block_row = -1;
    auto ensure_block_row = [&](int by) {
      if (by == loaded_block_row) return;
      loaded_block_row = by;
      for (int bx = 0; bx < luma_blocks_x; ++bx) {
        const int16_t* blk = LumaBlockPtr(bx, by);
        uint8_t* out_cell = strip_buf.get() + bx * block_px;
        if (reduced_2x2) {
          DequantizeAndIdct2x2(blk, luma_qt, out_cell, strip_stride);
        } else {
          DequantizeAndIdct8x8(blk, luma_qt, out_cell, strip_stride);
        }
      }
    };

    std::unique_ptr<uint32_t, decltype(&std::free)> row_accum(
        static_cast<uint32_t*>(AllocPsramPreferred(
            static_cast<size_t>(dst_w) * sizeof(uint32_t),
            /*allow_internal_fallback=*/true)),
        &std::free);
    std::unique_ptr<uint32_t, decltype(&std::free)> row_weight(
        static_cast<uint32_t*>(AllocPsramPreferred(
            static_cast<size_t>(dst_w) * sizeof(uint32_t),
            /*allow_internal_fallback=*/true)),
        &std::free);
    if (!row_accum || !row_weight) {
      std::free(dst);
      return false;
    }

    for (int dy = 0; dy < dst_h; ++dy) {
      const int sy0 = static_cast<int>(
          (static_cast<int64_t>(dy) * eff_h) / dst_h);
      const int sy1 = std::min(
          eff_h,
          std::max(sy0 + 1,
                   static_cast<int>((static_cast<int64_t>(dy + 1) * eff_h) /
                                    dst_h)));
      std::memset(row_accum.get(), 0,
                  static_cast<size_t>(dst_w) * sizeof(uint32_t));
      std::memset(row_weight.get(), 0,
                  static_cast<size_t>(dst_w) * sizeof(uint32_t));

      for (int sy = sy0; sy < sy1; ++sy) {
        const int by = std::min(luma_blocks_y - 1, sy / block_px);
        ensure_block_row(by);
        const uint8_t* src_row =
            strip_buf.get() + (sy % block_px) * strip_stride;
        for (int dx = 0; dx < dst_w; ++dx) {
          const int sx0 = static_cast<int>(
              (static_cast<int64_t>(dx) * eff_w) / dst_w);
          const int sx1 = std::min(
              eff_w,
              std::max(sx0 + 1,
                       static_cast<int>((static_cast<int64_t>(dx + 1) * eff_w) /
                                        dst_w)));
          uint32_t sum = 0;
          for (int sx = sx0; sx < sx1; ++sx) {
            sum += src_row[sx];
          }
          row_accum.get()[dx] += sum;
          row_weight.get()[dx] += static_cast<uint32_t>(sx1 - sx0);
        }
      }

      uint8_t* dst_row = dst + static_cast<size_t>(dy) * dst_w;
      for (int dx = 0; dx < dst_w; ++dx) {
        const uint32_t w = std::max<uint32_t>(1u, row_weight.get()[dx]);
        dst_row[dx] = static_cast<uint8_t>((row_accum.get()[dx] + w / 2) / w);
      }
    }

    *out_gray = dst;
    return true;
  }
};

}  // namespace

bool ProbeJpegHeader(const uint8_t* data, size_t size, int* out_width,
                     int* out_height, uint8_t* out_sof) {
  if (data == nullptr || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    return false;
  }
  size_t pos = 2;
  while (pos < size) {
    if (data[pos++] != 0xFF) break;
    while (pos < size && data[pos] == 0xFF) ++pos;
    if (pos >= size) break;
    const uint8_t b = data[pos++];
    if (b == 0xD9 || b == 0xDA) break;
    if (b == 0x00 || b == 0x01 || (b >= 0xD0 && b <= 0xD8)) continue;
    if (pos + 2 > size) break;
    const uint16_t seg_len =
        static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
    pos += 2;
    if (seg_len < 2 || pos + seg_len - 2 > size) break;
    if (b == 0xC0 || b == 0xC1 || b == 0xC2) {
      if (seg_len < 8) return false;
      const uint8_t precision = data[pos];
      const int h = (data[pos + 1] << 8) | data[pos + 2];
      const int w = (data[pos + 3] << 8) | data[pos + 4];
      const uint8_t nf = data[pos + 5];
      if (precision != 8 || w <= 0 || h <= 0 || w > kMaxImageDim ||
          h > kMaxImageDim || (nf != 1 && nf != 3)) {
        return false;
      }
      *out_width = w;
      *out_height = h;
      if (out_sof != nullptr) *out_sof = b;
      return true;
    }
    pos += seg_len - 2;
  }
  return false;
}

bool DecodeJpegGrayscaleScaled(const uint8_t* data, size_t size, int dst_width,
                               int dst_height, uint8_t** out_gray) {
  if (out_gray == nullptr) return false;
  *out_gray = nullptr;
  if (data == nullptr || size < 4 || dst_width <= 0 || dst_height <= 0 ||
      dst_width > kMaxDestDim || dst_height > kMaxDestDim) {
    return false;
  }

  std::unique_ptr<JpegDecoder, decltype(&std::free)> dec_mem(
      static_cast<JpegDecoder*>(AllocPsramPreferred(
          sizeof(JpegDecoder), /*allow_internal_fallback=*/true)),
      &std::free);
  if (!dec_mem) return false;
  JpegDecoder* dec = new (dec_mem.get()) JpegDecoder();
  dec->data = data;
  dec->size = size;

  const bool ok =
      dec->DecodeStream() && dec->RenderScaled(dst_width, dst_height, out_gray);
  dec->~JpegDecoder();
  return ok;
}

}  // namespace book
}  // namespace reader
