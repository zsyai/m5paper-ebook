#include "fonts/font_buffer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "core/perf_trace.h"
#include "fonts/font_manager.h"
#include "book/utf8_utils.h"

// Built-in UI font: GB2312 + ASCII at 30px (assets/ui_font_30.bin, embedded
// through board_build.embed_files in platformio.ini).
extern const uint8_t ui_font_30_start[] asm("_binary_assets_ui_font_30_bin_start");
extern const uint8_t ui_font_30_end[] asm("_binary_assets_ui_font_30_bin_end");

PageFontCache::PageFontCache(size_t budget_bytes) : budget_bytes_(budget_bytes) {}

const reader::fonts::Charset& PageFontCache::BuiltinCharset()
{
    static const reader::fonts::Charset charset(ui_font_30_start,
                                                ui_font_30_end - ui_font_30_start);
    return charset;
}

bool PageFontCache::stepFontCacheBuild(uint32_t budget_ms)
{
    if (!needsFontCacheBuild()) return false;
    const bool was_live = !ttf_source_.usingCache();
    const bool worked = ttf_source_.stepCacheBuild(budget_ms);
    if (was_live && ttf_source_.usingCache()) {
        // Metrics are identical, but bitmaps now come from the 4 bpp cache;
        // drop the live renders so pages look the same before and after.
        dropGlyphs();
    }
    return worked;
}

PageFontCache::~PageFontCache() { clear(); }

void PageFontCache::dropGlyphs()
{
    for (auto& pair : glyph_map_) {
        free(pair.second.first.bitmap);
    }
    glyph_map_.clear();
    lru_list_.clear();
    cached_bytes_ = 0;
}

void PageFontCache::clear()
{
    dropGlyphs();
    bin_source_.close();
    ttf_source_.close();
    source_ = &bin_source_;
    current_font_path_.clear();
    loaded_font_path_.clear();
    is_using_builtin_ = false;
}

bool PageFontCache::IsTtfPath(const std::string& path)
{
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".ttf";
}

void PageFontCache::setTtfPixelSize(uint8_t pixel_size)
{
    if (pixel_size == ttf_pixel_size_) return;
    ttf_pixel_size_ = pixel_size;
    // Force the next build() to reopen a TTF at the new size.
    if (source_ == &ttf_source_) clear();
}

bool PageFontCache::openFile(const std::string& path)
{
    if (IsTtfPath(path)) {
        if (!ttf_source_.openFile(path, ttf_pixel_size_, &BuiltinCharset())) {
            return false;
        }
        source_ = &ttf_source_;
    } else {
        if (!bin_source_.openFile(path)) return false;
        source_ = &bin_source_;
    }
    loaded_font_path_ = path;
    return true;
}

bool PageFontCache::openFont(const std::string& font_path)
{
    PERF_SCOPE("PageFontCache::openFont");
    is_using_builtin_ = false;
    if (font_path != "builtin" && openFile(font_path)) {
        return true;
    }

    // The requested font is gone or unreadable: fall back like before.
    if (font_path != "builtin" && !builtin_fallback_only_) {
        const std::string resolved = FontManager::resolveFontPath(font_path);
        if (resolved != "builtin" && resolved != font_path && openFile(resolved)) {
            return true;
        }
    }

    source_ = &bin_source_;
    if (bin_source_.openMemory(ui_font_30_start,
                               ui_font_30_end - ui_font_30_start)) {
        loaded_font_path_ = "builtin";
        is_using_builtin_ = true;
        return true;
    }
    loaded_font_path_.clear();
    return false;
}

void PageFontCache::touch(uint16_t unicode) const
{
    auto it = glyph_map_.find(unicode);
    if (it == glyph_map_.end()) return;
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second.second);
}

void PageFontCache::evictFor(size_t incoming)
{
    while (!lru_list_.empty() && cached_bytes_ + incoming > budget_bytes_) {
        const uint16_t victim = lru_list_.back();
        lru_list_.pop_back();
        auto it = glyph_map_.find(victim);
        if (it != glyph_map_.end()) {
            const CachedGlyph& glyph = it->second.first;
            if (glyph.bitmap != nullptr) {
                cached_bytes_ -= glyph.info.bitmapW * glyph.info.bitmapH;
                free(glyph.bitmap);
            }
            glyph_map_.erase(it);
        }
    }
}

bool PageFontCache::build(const std::string& text, const std::string& font_path)
{
    PERF_SCOPE("PageFontCache::build");
    if (current_font_path_ != font_path || !source_->isOpen()) {
        clear();
        current_font_path_ = font_path;
        if (!openFont(font_path)) return false;
    }

    std::vector<uint16_t> unique_chars;
    unique_chars.reserve(text.size() / 2);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
    const uint8_t* end = p + text.size();
    while (p < end) {
        int bytes = 0;
        uint32_t unicode = Utf8Utils::getNextCharUnicode(p, end, bytes);
        if (bytes > 0) {
            p += bytes;
            if (unicode <= 0xFFFF && unicode > 0) {
                unique_chars.push_back(static_cast<uint16_t>(unicode));
            }
        } else {
            p++;
        }
    }
    std::sort(unique_chars.begin(), unique_chars.end());
    unique_chars.erase(std::unique(unique_chars.begin(), unique_chars.end()), unique_chars.end());

    std::vector<uint16_t> chars_to_load;
    size_t incoming_bytes = 0;
    const size_t font_size = getFontSize();
    for (uint16_t u : unique_chars) {
        if (glyph_map_.count(u)) {
            touch(u);
        } else if (const CharGlyphInfo* info = source_->findGlyph(u)) {
            // Characters missing from the font are skipped without I/O.
            chars_to_load.push_back(u);
            // Live TTF metrics carry no bitmap size before rendering.
            const size_t bytes = info->bitmapW * info->bitmapH;
            incoming_bytes += bytes > 0 ? bytes : font_size * font_size;
        }
    }

    PERF_VALUE("glyph.unique", unique_chars.size());
    PERF_VALUE("glyph.miss", chars_to_load.size());
    if (chars_to_load.empty()) return true;

    evictFor(incoming_bytes);
    source_->loadGlyphs(chars_to_load, [this](const CharGlyphInfo& info, const uint8_t* bitmap) {
        CachedGlyph cached;
        cached.info = info;
        cached.bitmap = nullptr;
        if (bitmap != nullptr) {
            const size_t bytes = info.bitmapW * info.bitmapH;
            cached.bitmap = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
            if (cached.bitmap == nullptr) {
                log_e("PSRAM alloc failed for glyph U+%04X (%u B)", info.unicode, bytes);
                return;
            }
            memcpy(cached.bitmap, bitmap, bytes);
            cached_bytes_ += bytes;
        }
        lru_list_.push_front(info.unicode);
        glyph_map_[info.unicode] = {cached, lru_list_.begin()};
    });
    return true;
}

bool PageFontCache::hasChar(uint16_t unicode) const
{
    return glyph_map_.find(unicode) != glyph_map_.end();
}

const CharGlyphInfo* PageFontCache::getCharGlyphInfo(uint16_t unicode) const
{
    auto it = glyph_map_.find(unicode);
    if (it == glyph_map_.end()) return nullptr;
    touch(unicode);
    return &it->second.first.info;
}

const CharGlyphInfo* PageFontCache::getCharGlyphInfoDirect(uint16_t unicode) const
{
    return source_->findGlyph(unicode);
}

const uint8_t* PageFontCache::getCharBitmap(uint16_t unicode) const
{
    auto it = glyph_map_.find(unicode);
    if (it == glyph_map_.end()) return nullptr;
    touch(unicode);
    return it->second.first.bitmap;
}
