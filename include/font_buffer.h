#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>
#include <list>
#include "font_decoder.h"

struct CachedGlyph {
    CharGlyphInfo info;
    uint8_t* bitmap;
};

// Simplified PageFontCache
class PageFontCache {
public:
    PageFontCache();
    ~PageFontCache();

    // Disable copy and assignment
    PageFontCache(const PageFontCache&) = delete;
    PageFontCache& operator=(const PageFontCache&) = delete;

    // Build cache from UTF-8 text
    bool build(const std::string& text, const std::string& font_path);
    
    // Clear cache
    void clear();
    
    // Query character
    bool hasChar(uint16_t unicode) const;
    
    // Get character glyph info
    const CharGlyphInfo* getCharGlyphInfo(uint16_t unicode) const;
    const CharGlyphInfo* getCharGlyphInfoDirect(uint16_t unicode) const;
    
    // Get character bitmap (unpacked 8-bit grayscale)
    const uint8_t* getCharBitmap(uint16_t unicode) const;

    // Get font size
    uint8_t getFontSize() const { return font_size_; }
    
    // Get font name
    const std::string& getFontName() const { return font_name_; }

    // Get loaded font path
    const std::string& getLoadedFontPath() const { return loaded_font_path_; }

    // Check if using builtin font
    bool isUsingBuiltin() const { return is_using_builtin_; }

private:
    // Persistent storage and LRU
    mutable std::unordered_map<uint16_t, std::pair<CachedGlyph, std::list<uint16_t>::iterator>> glyph_map_;
    mutable std::list<uint16_t> lru_list_;
    
    // Font file index in PSRAM
    CharGlyphInfo* file_index_;
    uint32_t file_char_count_;
    uint8_t font_size_;
    uint8_t font_version_;
    std::string font_name_;
    std::string current_font_path_;
    std::string loaded_font_path_;
    bool is_using_builtin_;

    static const size_t CACHE_LIMIT = 1500;

    // Helper to find glyph in index
    const CharGlyphInfo* findInIndex(uint16_t unicode) const;
};
