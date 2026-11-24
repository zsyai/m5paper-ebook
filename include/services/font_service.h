#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_FONT_SERVICE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_FONT_SERVICE_H_

// Owns the UI and body glyph LRU caches, fallback font resolution, and
// background TTF->.bin font-cache building.

#include <cstdint>
#include <string>
#include <vector>

#include "core/settings.h"
#include "fonts/font_buffer.h"

namespace reader {
namespace services {

class FontService {
 public:
  static constexpr uint8_t kUiFontPixelSize = 30;

  void Init(const core::Settings& settings);

  PageFontCache& ui_cache() { return ui_cache_; }
  const PageFontCache& ui_cache() const { return ui_cache_; }
  PageFontCache& body_cache() { return body_cache_; }
  const PageFontCache& body_cache() const { return body_cache_; }

  // Effective UI font: a .ttf from /fonts or "builtin" (fixed-size .bin fonts
  // are rejected for UI layout).
  std::string EffectiveUiFontPath(const core::Settings& settings) const;

  bool IsBodyFontScalable(const core::Settings& settings) const {
    return PageFontCache::IsTtfPath(settings.body_font_path);
  }

  // Body font path after fallback; saves a replacement back to `settings` if
  // the configured font file vanished. Returns true in `*settings_updated` when
  // `settings->body_font_path` changed.
  std::string ResolveBodyFontPath(core::Settings* settings,
                                  bool* settings_updated = nullptr);

  // True when a non-builtin body font was configured but resolved to "builtin".
  bool IsUsingFallbackBuiltin(core::Settings* settings);

  void OnUiFontChanged() { ui_cache_.clear(); }
  void OnBodyFontChanged();
  void OnBodyFontSizeChanged(int pixel_size) {
    body_cache_.setTtfPixelSize(pixel_size);
  }

  // TTF glyph cache files currently open or selected by `settings`.
  std::vector<std::string> CachesInUse(const core::Settings& settings) const;

  bool NeedsBackgroundBuild() const;
  void StepBackgroundBuild(uint32_t budget_ms);
  void CancelBackgroundBuilds();

  // Closes all font descriptors and clears both glyph caches (for SD remount).
  void ReleaseSdHandles();

  int ui_font_size() const { return ui_cache_.getFontSize(); }
  int header_height() const { return static_cast<int>(ui_font_size() * 2.5f); }
  int footer_height() const { return static_cast<int>(ui_font_size() * 3.0f); }

 private:
  PageFontCache ui_cache_{/*budget_bytes=*/512 * 1024};
  PageFontCache body_cache_{/*budget_bytes=*/1536 * 1024};
  std::string body_font_requested_;
  std::string body_font_resolved_;
};

}  // namespace services
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_FONT_SERVICE_H_
