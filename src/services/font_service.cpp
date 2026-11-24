#include "services/font_service.h"

#include <string>
#include <vector>

#include "fonts/font_cache_store.h"
#include "fonts/font_manager.h"

namespace reader {
namespace services {

void FontService::Init(const core::Settings& settings) {
  ui_cache_.setBuiltinFallbackOnly(true);
  ui_cache_.setTtfPixelSize(kUiFontPixelSize);
  body_cache_.setTtfPixelSize(settings.body_font_size);
}

std::string FontService::EffectiveUiFontPath(
    const core::Settings& settings) const {
  return PageFontCache::IsTtfPath(settings.ui_font_path) ? settings.ui_font_path
                                                         : "builtin";
}

std::string FontService::ResolveBodyFontPath(core::Settings* settings,
                                             bool* settings_updated) {
  if (settings_updated != nullptr) *settings_updated = false;
  const std::string requested = settings->body_font_path;
  if (requested == body_font_requested_) return body_font_resolved_;
  body_font_resolved_ = FontManager::resolveFontPath(requested);
  if (body_font_resolved_ != "builtin" && body_font_resolved_ != requested) {
    settings->body_font_path = body_font_resolved_;
    settings->Save();
    if (settings_updated != nullptr) *settings_updated = true;
  }
  body_font_requested_ = settings->body_font_path;
  return body_font_resolved_;
}

bool FontService::IsUsingFallbackBuiltin(core::Settings* settings) {
  return settings->body_font_path != "builtin" &&
         ResolveBodyFontPath(settings) == "builtin";
}

void FontService::OnBodyFontChanged() {
  body_cache_.clear();
  body_font_requested_.clear();
}

std::vector<std::string> FontService::CachesInUse(
    const core::Settings& settings) const {
  std::vector<std::string> paths = {ui_cache_.fontCachePath(),
                                    body_cache_.fontCachePath()};
  const std::string ui_path = EffectiveUiFontPath(settings);
  if (PageFontCache::IsTtfPath(ui_path)) {
    paths.push_back(fonts::FontCachePath(ui_path, kUiFontPixelSize));
  }
  if (IsBodyFontScalable(settings)) {
    paths.push_back(
        fonts::FontCachePath(settings.body_font_path, settings.body_font_size));
  }
  return paths;
}

bool FontService::NeedsBackgroundBuild() const {
  return body_cache_.needsFontCacheBuild() || ui_cache_.needsFontCacheBuild();
}

void FontService::StepBackgroundBuild(uint32_t budget_ms) {
  if (!body_cache_.stepFontCacheBuild(budget_ms)) {
    ui_cache_.stepFontCacheBuild(budget_ms);
  }
}

void FontService::CancelBackgroundBuilds() {
  body_cache_.cancelFontCacheBuild();
  ui_cache_.cancelFontCacheBuild();
}

void FontService::ReleaseSdHandles() {
  CancelBackgroundBuilds();
  body_cache_.clear();
  ui_cache_.clear();
  body_font_requested_.clear();
}

}  // namespace services
}  // namespace reader
