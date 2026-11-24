#include "hal/display.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/Bus.hpp>
#include <lgfx/v1/panel/Panel_Device.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "core/perf_trace.h"
#include "hal/it8951_pack.h"

namespace reader {
namespace hal {
namespace {

constexpr uint16_t kCmdLoadImgArea = 0x0021;
constexpr uint16_t kCmdLoadImgEnd = 0x0022;
constexpr uint16_t kCmdDisplayBufArea = 0x0037;
constexpr uint16_t kCmdGetDevInfo = 0x0302;

constexpr uint16_t kLdImgBigEndian = 1;
constexpr uint16_t kLdImg4Bpp = 2;

constexpr uint16_t kUpdateModeGc16 = 2;
constexpr uint16_t kUpdateModeGl16 = 3;

constexpr uint32_t kDefaultM5PaperMemAddr = 0x001236E0u;
constexpr uint32_t kBusyTimeoutMs = 4096;
// 15 rows * 270 bytes/row + 2-byte I80 data preamble = 4052 bytes: exact
// multiple of 4 bytes (32-bit SPI DMA burst) and <= 4092 (single lldesc_t).
constexpr int kChunkRows = 15;

// Set when `PushReadingFrame` started an asynchronous IT8951 LUT update via
// `kCmdDisplayBufArea` without updating M5GFX's private `_range_old`. Cleared
// by waiting for `IT8951_LUTAFSR == 0` before the next panel write.
bool s_direct_update_in_flight = false;
uint32_t s_target_mem_addr = 0;

void SyncDirectUpdate() {
  if (!s_direct_update_in_flight) return;
  PERF_SCOPE("epd.waitPrev");
  M5.Display.waitDisplay();
  s_direct_update_in_flight = false;
}

bool WaitBusyPin(int16_t pin_busy) {
  if (pin_busy >= 0 && !lgfx::gpio_in(pin_busy)) {
    const uint32_t start_ms = millis();
    do {
      const uint32_t elapsed = millis() - start_ms;
      if (elapsed >= 16) {
        if (elapsed > kBusyTimeoutMs) {
          log_w("[epd] IT8951 busy pin timeout");
          return false;
        }
        delay(elapsed >> 4);
      }
    } while (!lgfx::gpio_in(pin_busy));
  }
  return true;
}

bool WaitBusy(lgfx::IBus* bus, int16_t pin_cs, int16_t pin_busy) {
  bus->wait();
  lgfx::gpio_hi(pin_cs);
  if (!WaitBusyPin(pin_busy)) return false;
  lgfx::gpio_lo(pin_cs);
  return true;
}

bool WriteCommand(lgfx::IBus* bus, int16_t pin_cs, int16_t pin_busy,
                  uint16_t cmd) {
  const uint32_t word =
      lgfx::getSwap16(0x6000) |
      (static_cast<uint32_t>(lgfx::getSwap16(cmd)) << 16);
  if (!WaitBusy(bus, pin_cs, pin_busy)) return false;
  bus->writeData(word, 32);
  return true;
}

bool WriteArgs(lgfx::IBus* bus, int16_t pin_cs, int16_t pin_busy, uint16_t cmd,
               const uint16_t* args, int count) {
  if (!WriteCommand(bus, pin_cs, pin_busy, cmd) ||
      !WaitBusy(bus, pin_cs, pin_busy)) {
    return false;
  }
  bus->writeData(0, 16);  // I80 data-write preamble (0x0000).
  for (int i = 0; i < count; ++i) {
    const uint32_t swapped = lgfx::getSwap16(args[i]);
    bus->wait();
    if (!WaitBusyPin(pin_busy)) {
      lgfx::gpio_hi(pin_cs);
      return false;
    }
    bus->writeData(swapped, 16);
  }
  return true;
}

bool ReadWords(lgfx::IBus* bus, int16_t pin_cs, int16_t pin_busy, uint16_t* out,
               size_t count) {
  if (!WaitBusy(bus, pin_cs, pin_busy)) return false;
  bus->writeData(lgfx::getSwap16(0x1000), 16);  // I80 data-read preamble.
  bus->beginRead(16);                           // 16-bit dummy word.
  bus->readBytes(reinterpret_cast<uint8_t*>(out), count * sizeof(uint16_t),
                 /*use_dma=*/false);
  bus->endRead();
  lgfx::gpio_hi(pin_cs);
  for (size_t i = 0; i < count; ++i) {
    out[i] = lgfx::getSwap16(out[i]);
  }
  return true;
}

uint32_t EnsureTargetMemAddr(lgfx::Panel_Device* panel) {
  if (s_target_mem_addr != 0) return s_target_mem_addr;
  lgfx::IBus* bus = panel->getBus();
  const auto& cfg = panel->config();
  const uint32_t saved_write_clock = bus->getClock();
  const uint32_t saved_read_clock = bus->getReadClock();
  if (saved_write_clock > 12000000) bus->setClock(12000000);
  if (saved_read_clock > 2000000) bus->setReadClock(2000000);

  M5.Display.startWrite();
  uint16_t dev_info[20] = {};
  bool ok = WriteCommand(bus, cfg.pin_cs, cfg.pin_busy, kCmdGetDevInfo);
  if (ok) {
    delay(1);
    ok = ReadWords(bus, cfg.pin_cs, cfg.pin_busy, dev_info, 20);
  }
  bus->wait();
  lgfx::gpio_hi(cfg.pin_cs);
  M5.Display.endWrite();

  bus->setClock(saved_write_clock);
  bus->setReadClock(saved_read_clock);

  const uint32_t addr =
      (static_cast<uint32_t>(dev_info[3]) << 16) | dev_info[2];
  s_target_mem_addr =
      (ok && addr != 0 && addr != ~0u) ? addr : kDefaultM5PaperMemAddr;
  return s_target_mem_addr;
}

size_t PackChunk(const uint8_t* src_base, int raw_w, int start_row, int rows,
                 int src_row0, int src_row_step, const uint8_t lut[256],
                 uint8_t* dst) {
  dst[0] = 0;  // 16-bit I80 data-write preamble (0x0000).
  dst[1] = 0;
  uint8_t* out = dst + 2;
  const size_t row_out_bytes = static_cast<size_t>(raw_w >> 1);
  int src_y = src_row0 + start_row * src_row_step;
  for (int r = 0; r < rows; ++r) {
    PackGray8To4bppBe(src_base + static_cast<size_t>(src_y) * raw_w,
                      static_cast<size_t>(raw_w), lut, out);
    out += row_out_bytes;
    src_y += src_row_step;
  }
  return 2 + static_cast<size_t>(rows) * row_out_bytes;
}

}  // namespace

void PushFrame(LGFX_Sprite& canvas, lgfx::epd_mode_t mode) {
  SyncDirectUpdate();
  M5.Display.setEpdMode(mode);
  {
    PERF_SCOPE("pushSprite");
    canvas.pushSprite(&M5.Display, 0, 0);
  }
#if READER_PERF
  {
    PERF_SCOPE("epd.waitDisplay");
    M5.Display.waitDisplay();
  }
#endif
}

void PushReadingFrame(LGFX_Sprite& canvas, bool full_refresh) {
  const lgfx::epd_mode_t fallback_mode =
      full_refresh ? lgfx::epd_mode_t::epd_quality : lgfx::epd_mode_t::epd_text;
  const int raw_w =
      (canvas.getRotation() & 1) ? canvas.height() : canvas.width();
  const int raw_h =
      (canvas.getRotation() & 1) ? canvas.width() : canvas.height();
  const auto* src = static_cast<const uint8_t*>(canvas.getBuffer());
  lgfx::Panel_Device* panel = M5.Display.getPanel();

  if (M5.Display.getBoard() != lgfx::board_t::board_M5Paper ||
      canvas.getColorDepth() != lgfx::color_depth_t::grayscale_8bit ||
      src == nullptr || panel == nullptr || panel->getBus() == nullptr ||
      panel->config().pin_cs < 0 || panel->config().pin_busy < 0 ||
      raw_w <= 0 || raw_h <= 0 || (raw_w & 3) != 0 ||
      raw_w != M5.Display.width() || raw_h != M5.Display.height()) {
    PushFrame(canvas, fallback_mode);
    return;
  }

  const size_t max_chunk_bytes =
      2 + static_cast<size_t>(kChunkRows) * static_cast<size_t>(raw_w >> 1);
  const size_t chunk_stride = (max_chunk_bytes + 3u) & ~size_t{3u};
  auto* dma_buf = static_cast<uint8_t*>(
      heap_caps_malloc(chunk_stride * 2, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (dma_buf == nullptr) {
    PushFrame(canvas, fallback_mode);
    return;
  }

  SyncDirectUpdate();
  // Also wait if an earlier M5GFX-driven update (e.g. closing the reader menu)
  // is still scanning out, since our direct write bypasses `_range_old`.
  M5.Display.waitDisplay();

  const uint32_t tar_addr = EnsureTargetMemAddr(panel);
  uint8_t lut[256];
  BuildReadingGrayLut(M5.Display.getInvert(), lut);

  lgfx::IBus* bus = panel->getBus();
  const auto& cfg = panel->config();
  const uint8_t r = M5.Display.getRotation() & 7;
  const uint8_t internal_rot =
      ((r + cfg.offset_rotation) & 3) | ((r & 4) ^ (cfg.offset_rotation & 4));
  const uint8_t it8951_rot = ((-internal_rot) & 3) | (internal_rot & 4);
  const int src_row0 = (it8951_rot & 4) ? (raw_h - 1) : 0;
  const int src_row_step = (it8951_rot & 4) ? -1 : 1;

  uint16_t rx =
      ((it8951_rot + 1) & 2) ? static_cast<uint16_t>(M5.Display.width() - raw_w)
                             : 0;
  uint16_t ry =
      (it8951_rot & 2) ? static_cast<uint16_t>(M5.Display.height() - raw_h) : 0;
  uint16_t rw = static_cast<uint16_t>(raw_w);
  uint16_t rh = static_cast<uint16_t>(raw_h);
  if (it8951_rot & 1) {
    std::swap(rx, ry);
    std::swap(rw, rh);
  }

  bool ok = false;
  {
    PERF_SCOPE("pushFast4bpp");
    M5.Display.startWrite();

    const uint16_t ld_args[5] = {
        static_cast<uint16_t>((kLdImgBigEndian << 8) | (kLdImg4Bpp << 4) |
                              (it8951_rot & 3)),
        0,
        0,
        static_cast<uint16_t>(raw_w),
        static_cast<uint16_t>(raw_h),
    };
    ok = WriteArgs(bus, cfg.pin_cs, cfg.pin_busy, kCmdLoadImgArea, ld_args, 5);

    if (ok) {
      uint8_t* bufs[2] = {dma_buf, dma_buf + chunk_stride};
      int row = 0;
      int cur_rows = std::min(kChunkRows, raw_h);
      size_t cur_bytes = PackChunk(src, raw_w, 0, cur_rows, src_row0,
                                   src_row_step, lut, bufs[0]);
      int slot = 0;
      while (row < raw_h) {
        if (!WaitBusy(bus, cfg.pin_cs, cfg.pin_busy)) {
          ok = false;
          break;
        }
        bus->writeBytes(bufs[slot], static_cast<uint32_t>(cur_bytes),
                        /*dc=*/true, /*use_dma=*/true);
        const int next_row = row + cur_rows;
        if (next_row < raw_h) {
          // Overlap CPU PSRAM->4bpp packing of the next chunk with VSPI DMA.
          const int next_rows = std::min(kChunkRows, raw_h - next_row);
          cur_bytes = PackChunk(src, raw_w, next_row, next_rows, src_row0,
                                src_row_step, lut, bufs[slot ^ 1]);
          cur_rows = next_rows;
          slot ^= 1;
        }
        row = next_row;
      }
    }

    if (ok) {
      ok = WriteCommand(bus, cfg.pin_cs, cfg.pin_busy, kCmdLoadImgEnd);
    }
    if (ok) {
      const uint16_t dpy_args[7] = {
          rx,
          ry,
          rw,
          rh,
          full_refresh ? kUpdateModeGc16 : kUpdateModeGl16,
          static_cast<uint16_t>(tar_addr),
          static_cast<uint16_t>(tar_addr >> 16),
      };
      ok = WriteArgs(bus, cfg.pin_cs, cfg.pin_busy, kCmdDisplayBufArea,
                     dpy_args, 7);
    }

    // Drain any in-flight SPI DMA before freeing `dma_buf` or releasing CS.
    bus->wait();
    lgfx::gpio_hi(cfg.pin_cs);
    M5.Display.endWrite();
  }

  heap_caps_free(dma_buf);
  if (ok) {
    s_direct_update_in_flight = true;
  }

#if READER_PERF
  if (s_direct_update_in_flight) {
    PERF_SCOPE("epd.waitDisplay");
    M5.Display.waitDisplay();
    s_direct_update_in_flight = false;
  }
#endif
}

void PushRegion(LGFX_Sprite& canvas, int x, int y, int w, int h,
                lgfx::epd_mode_t mode) {
  SyncDirectUpdate();
  // The sprite is pushed as its raw (unrotated) buffer, so convert the
  // rectangle to panel coordinates; see ui/canvas_pixels.h for the mapping.
  const int pw = M5.Display.width();
  const int ph = M5.Display.height();
  int px = x;
  int py = y;
  int rw = w;
  int rh = h;
  switch (canvas.getRotation() & 3) {
    case 1:
      px = pw - (y + h);
      py = x;
      rw = h;
      rh = w;
      break;
    case 2:
      px = pw - (x + w);
      py = ph - (y + h);
      break;
    case 3:
      px = y;
      py = ph - (x + w);
      rw = h;
      rh = w;
      break;
    default:
      break;
  }
  // Clamp and 4-pixel-align horizontal panel bounds (width >= 8) so IT8951
  // 4bpp packing is word-aligned and avoids `_update_raw_area`'s narrow-strip
  // `(l & ~3) == (r & ~3)` unsigned underflow at x = 0.
  const int px0 = std::max(0, px & ~3);
  int px1 = std::min(pw, (std::max(px, 0) + rw + 3) & ~3);
  if (px1 - px0 < 8) {
    px1 = std::min(pw, px0 + 8);
  }
  const int py0 = std::max(0, py);
  const int py1 = std::min(ph, std::max(py, 0) + rh);
  if (px1 <= px0 || py1 <= py0) return;

  M5.Display.setEpdMode(mode);
  M5.Display.setClipRect(px0, py0, px1 - px0, py1 - py0);
  {
    PERF_SCOPE("pushRegion");
    canvas.pushSprite(&M5.Display, 0, 0);
  }
  M5.Display.clearClipRect();
#if READER_PERF
  {
    PERF_SCOPE("epd.waitDisplay");
    M5.Display.waitDisplay();
  }
#endif
}

bool RefreshCounter::CountFrame(int interval) {
  Count();
  if (!Due(interval)) return false;
  Restart();
  return true;
}

}  // namespace hal
}  // namespace reader
