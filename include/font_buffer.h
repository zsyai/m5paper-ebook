#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "font_decoder.h"
#include "fonts/bin_glyph_source.h"
#include "fonts/cached_ttf_glyph_source.h"
#include "fonts/font_cache_builder.h"

struct CachedGlyph {
    CharGlyphInfo info;
    uint8_t* bitmap;  // 8-bit grayscale in PSRAM, nullptr for empty glyphs
};

// LRU cache of decoded glyphs in front of a reader::fonts::IGlyphSource.
//
// Callers first build() the cache for the text they are about to measure or
// draw, then query metrics / bitmaps. File access lives in the glyph source;
// this class only owns the decoded-bitmap cache and font selection.
class PageFontCache {
public:
    // `budget_bytes` caps the decoded bitmap bytes kept in PSRAM. A byte
    // budget (not a glyph count) keeps memory flat across font sizes: a
    // 60px glyph takes about 4x the bytes of a 30px one.
    explicit PageFontCache(size_t budget_bytes = kDefaultBudgetBytes);
    ~PageFontCache();

    // Disable copy and assignment
    PageFontCache(const PageFontCache&) = delete;
    PageFontCache& operator=(const PageFontCache&) = delete;

    // Makes sure every glyph in UTF-8 `text` is cached for `font_path`
    // ("builtin" selects the flash font). Switching the path reloads the font.
    // Falls back to FontManager resolution, then the built-in font, if the
    // path cannot be opened.
    bool build(const std::string& text, const std::string& font_path);

    // Drops cached glyphs and closes the font; the next build() reopens it.
    void clear();

    // Query character
    bool hasChar(uint16_t unicode) const;

    // Cached glyph metrics (refreshes LRU position).
    const CharGlyphInfo* getCharGlyphInfo(uint16_t unicode) const;
    // Metrics straight from the font index, cached or not. No I/O.
    const CharGlyphInfo* getCharGlyphInfoDirect(uint16_t unicode) const;

    // Get character bitmap (unpacked 8-bit grayscale)
    const uint8_t* getCharBitmap(uint16_t unicode) const;

    // Pixel size of the open font (24 before any font is opened).
    uint8_t getFontSize() const {
        return source_->isOpen() ? source_->fontSize() : kDefaultFontSize;
    }

    const std::string& getFontName() const { return source_->fontName(); }

    // Pixel size used for .ttf fonts (ignored by fixed-size .bin fonts).
    // Changing it drops the cache if a TTF is open.
    void setTtfPixelSize(uint8_t pixel_size);
    uint8_t getTtfPixelSize() const { return ttf_pixel_size_; }

    static bool IsTtfPath(const std::string& path);

    // Path actually opened ("builtin" for the flash font).
    const std::string& getLoadedFontPath() const { return loaded_font_path_; }

    bool isUsingBuiltin() const { return is_using_builtin_; }

    // When set, a font that cannot be opened falls back straight to the
    // built-in font instead of trying other fonts in /fonts (UI font).
    void setBuiltinFallbackOnly(bool only) { builtin_fallback_only_ = only; }

    // TTF fonts get a pre-rendered glyph cache on the SD card, written in
    // the background by stepFontCacheBuild() (about 40 ms per call).
    bool needsFontCacheBuild() const {
        return source_ == &ttf_source_ && ttf_source_.needsCacheBuild();
    }
    // Returns false if no work was done.
    bool stepFontCacheBuild(uint32_t budget_ms);
    // Stops a running build and deletes its partial file.
    void cancelFontCacheBuild() { ttf_source_.cancelCacheBuild(); }

    // SD cache file used by the open TTF, or "" for other fonts.
    std::string fontCachePath() const {
        return source_ == &ttf_source_ ? ttf_source_.cachePath() : std::string();
    }

    // Characters of the built-in font; also the charset of TTF caches.
    static const reader::fonts::Charset& BuiltinCharset();

private:
    static constexpr uint8_t kDefaultFontSize = 24;
    static constexpr size_t kDefaultBudgetBytes = 1024 * 1024;

    bool openFont(const std::string& font_path);
    // Opens `path` with the source matching its extension.
    bool openFile(const std::string& path);
    void touch(uint16_t unicode) const;
    // Evicts least recently used glyphs until `incoming` more bitmap bytes
    // fit in the budget (or the cache is empty).
    void evictFor(size_t incoming);
    // Frees every cached bitmap and empties the LRU.
    void dropGlyphs();

    static constexpr uint8_t kDefaultTtfPixelSize = 30;

    reader::fonts::BinGlyphSource bin_source_;
    reader::fonts::CachedTtfGlyphSource ttf_source_;
    reader::fonts::IGlyphSource* source_ = &bin_source_;  // active source
    uint8_t ttf_pixel_size_ = kDefaultTtfPixelSize;

    // Persistent storage and LRU
    mutable std::unordered_map<uint16_t, std::pair<CachedGlyph, std::list<uint16_t>::iterator>> glyph_map_;
    mutable std::list<uint16_t> lru_list_;

    const size_t budget_bytes_;
    size_t cached_bytes_ = 0;  // Bitmap bytes held in glyph_map_.
    bool builtin_fallback_only_ = false;

    std::string current_font_path_;
    std::string loaded_font_path_;
    bool is_using_builtin_ = false;
};
