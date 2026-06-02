#include "font_buffer.h"
#include "utf8_utils.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include "NotoSansSC_30.h"
#include "font_manager.h"

PageFontCache::PageFontCache()
    : file_index_(nullptr), file_char_count_(0), font_size_(24), font_version_(1), current_font_path_(""), loaded_font_path_(""), is_using_builtin_(false)
{
}

PageFontCache::~PageFontCache()
{
    clear();
    if (file_index_) {
        free(file_index_);
    }
}

void PageFontCache::clear()
{
    for (auto& pair : glyph_map_) {
        free(pair.second.first.bitmap);
    }
    glyph_map_.clear();
    lru_list_.clear();
    
    if (file_index_) {
        free(file_index_);
        file_index_ = nullptr;
    }
    file_char_count_ = 0;
    font_name_.clear();
}

bool PageFontCache::build(const std::string& text, const std::string& font_path)
{
    if (current_font_path_ != font_path) {
        clear();
        current_font_path_ = font_path;
        is_using_builtin_ = false;
        loaded_font_path_ = "";
    }

    if (!file_index_) {
        loaded_font_path_ = FontManager::resolveFontPath(font_path);
        is_using_builtin_ = (loaded_font_path_ == "builtin");

        File file;
        const uint8_t* data = nullptr;
        size_t data_offset = 0;

        if (is_using_builtin_) {
            data = NotoSansSC_30_bin;
        } else {
            file = SD.open(loaded_font_path_.c_str());
            if (!file) {
                Serial.println("Failed to open resolved font file for index loading");
                return false;
            }
        }

        auto read_data = [&](void* dest, size_t size) {
            if (is_using_builtin_) {
                memcpy(dest, data + data_offset, size);
                data_offset += size;
                return size;
            } else {
                return file.read(reinterpret_cast<uint8_t*>(dest), size);
            }
        };

        auto seek_data = [&](size_t pos) {
            if (is_using_builtin_) {
                data_offset = pos;
                return true;
            } else {
                return file.seek(pos);
            }
        };

        if (is_using_builtin_) {
            data_offset = 0;
        } else {
            file.seek(0);
        }

        read_data(&file_char_count_, 4);
        read_data(&font_size_, 1);
        read_data(&font_version_, 1);
        Serial.printf("Loaded font size: %d, version: %d\n", font_size_, font_version_);
        
        char name_buf[65];
        read_data(name_buf, 64);
        name_buf[64] = '\0';
        font_name_ = name_buf;
        Serial.printf("Loaded font name: %s\n", font_name_.c_str());
        
        seek_data(134);
        size_t index_size = file_char_count_ * sizeof(CharGlyphInfo);
        file_index_ = static_cast<CharGlyphInfo*>(heap_caps_malloc(index_size, MALLOC_CAP_SPIRAM));
        if (!file_index_) {
            Serial.println("Failed to allocate PSRAM for file index");
            if (!is_using_builtin_) file.close();
            return false;
        }
        if (read_data(file_index_, index_size) != index_size) {
            Serial.println("Failed to read complete file index");
            free(file_index_);
            file_index_ = nullptr;
            if (!is_using_builtin_) file.close();
            return false;
        }
        if (!is_using_builtin_) file.close();
    }

    std::vector<uint16_t> unique_chars;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
    const uint8_t* end = p + text.size();

    while (p < end)
    {
        int bytes = 0;
        uint32_t unicode = Utf8Utils::getNextCharUnicode(p, end, bytes);
        
        if (bytes > 0)
        {
            p += bytes;
            if (unicode <= 0xFFFF && unicode > 0)
            {
                unique_chars.push_back(static_cast<uint16_t>(unicode));
            }
        }
        else
        {
            p++;
        }
    }

    std::sort(unique_chars.begin(), unique_chars.end());
    unique_chars.erase(std::unique(unique_chars.begin(), unique_chars.end()), unique_chars.end());

    if (unique_chars.empty()) return false;

    std::vector<uint16_t> chars_to_load;
    for (uint16_t u : unique_chars) {
        if (glyph_map_.find(u) == glyph_map_.end()) {
            chars_to_load.push_back(u);
        } else {
            auto& pair = glyph_map_[u];
            lru_list_.erase(pair.second);
            lru_list_.push_front(u);
            pair.second = lru_list_.begin();
        }
    }

    if (chars_to_load.empty()) return true;

    bool use_builtin = is_using_builtin_;
    File file;
    const uint8_t* data = nullptr;
    size_t data_offset = 0;

    if (use_builtin) {
        data = NotoSansSC_30_bin;
    } else {
        file = SD.open(loaded_font_path_.c_str());
        if (!file) {
            Serial.printf("Failed to open font file for glyph reading: %s. Free Heap: %u, Free PSRAM: %u\n", 
                          loaded_font_path_.c_str(), ESP.getFreeHeap(), ESP.getFreePsram());
            return false;
        }
    }
    
    auto read_data = [&](void* dest, size_t size) {
        if (use_builtin) {
            memcpy(dest, data + data_offset, size);
            data_offset += size;
            return size;
        } else {
            return file.read(reinterpret_cast<uint8_t*>(dest), size);
        }
    };

    auto seek_data = [&](size_t pos) {
        if (use_builtin) {
            data_offset = pos;
            return true;
        } else {
            return file.seek(pos);
        }
    };

    size_t index_size = file_char_count_ * sizeof(CharGlyphInfo);
    uint32_t bitmap_area_start = 134 + index_size;

    for (uint16_t u : chars_to_load) {
        if (glyph_map_.size() >= CACHE_LIMIT) {
            uint16_t lru_char = lru_list_.back();
            lru_list_.pop_back();
            
            auto it = glyph_map_.find(lru_char);
            if (it != glyph_map_.end()) {
                free(it->second.first.bitmap);
                glyph_map_.erase(it);
            }
        }

        const CharGlyphInfo* info = findInIndex(u);
            
        if (info)
        {
            Serial.printf("Loading char: U+%04X, offset: %u, size: %u\n", u, info->bitmap_offset, info->bitmap_size);
            if (info->bitmapW == 0 || info->bitmapH == 0) {
                CachedGlyph cached;
                cached.info = *info;
                cached.bitmap = nullptr;
                lru_list_.push_front(u);
                glyph_map_[u] = {cached, lru_list_.begin()};
                continue;
            }

            uint32_t cache_bitmap_size = info->bitmapW * info->bitmapH;
            
            uint8_t* dest_bitmap = static_cast<uint8_t*>(heap_caps_malloc(cache_bitmap_size, MALLOC_CAP_SPIRAM));
            if (!dest_bitmap) {
                Serial.printf("Failed to allocate PSRAM for glyph. Size: %u, Free PSRAM: %u, Free Heap: %u, Cache Size: %u\n", 
                              cache_bitmap_size, ESP.getFreePsram(), ESP.getFreeHeap(), glyph_map_.size());
                continue; 
            }
            memset(dest_bitmap, 255, cache_bitmap_size); // Fill with white background

            if (!seek_data(bitmap_area_start + info->bitmap_offset)) {
                Serial.println("Failed to seek to glyph bitmap");
                free(dest_bitmap);
                continue;
            }
            
            uint8_t* packed_bitmap = static_cast<uint8_t*>(malloc(info->bitmap_size));
            if (!packed_bitmap) {
                Serial.printf("Failed to allocate memory for packed bitmap. Size: %u, Free Heap: %u, Free PSRAM: %u\n", 
                              info->bitmap_size, ESP.getFreeHeap(), ESP.getFreePsram());
                free(dest_bitmap);
                continue;
            }
            
            if (read_data(packed_bitmap, info->bitmap_size) != info->bitmap_size) {
                Serial.printf("Failed to read complete glyph bitmap. Expected: %u, U+%04X\n", info->bitmap_size, u);
                free(packed_bitmap);
                free(dest_bitmap);
                continue;
            }

            if (font_version_ == 2) {
                FontDecoder::decode_bitmap_4bit(packed_bitmap, info->bitmap_size, dest_bitmap, 
                                                info->bitmapW, info->bitmapH);
            } else if (font_version_ == 3) {
                FontDecoder::decode_bitmap_2bit(packed_bitmap, info->bitmap_size, dest_bitmap, 
                                                info->bitmapW, info->bitmapH);
            } else {
                FontDecoder::decode_bitmap_1bit(packed_bitmap, info->bitmap_size, dest_bitmap, 
                                                info->bitmapW, info->bitmapH);
            }
                                            
            free(packed_bitmap);

            CachedGlyph cached;
            cached.info = *info;
            cached.bitmap = dest_bitmap;

            lru_list_.push_front(u);
            glyph_map_[u] = {cached, lru_list_.begin()};
        }
    }
    if (!use_builtin) file.close();
    return true;
}

bool PageFontCache::hasChar(uint16_t unicode) const
{
    return glyph_map_.find(unicode) != glyph_map_.end();
}

const CharGlyphInfo* PageFontCache::getCharGlyphInfo(uint16_t unicode) const
{
    auto it = glyph_map_.find(unicode);
    if (it != glyph_map_.end()) {
        lru_list_.erase(it->second.second);
        lru_list_.push_front(unicode);
        it->second.second = lru_list_.begin();
        
        return &it->second.first.info;
    }
    return nullptr;
}

const CharGlyphInfo* PageFontCache::getCharGlyphInfoDirect(uint16_t unicode) const {
    return findInIndex(unicode);
}

const uint8_t* PageFontCache::getCharBitmap(uint16_t unicode) const {
    auto it = glyph_map_.find(unicode);
    if (it != glyph_map_.end()) {
        lru_list_.erase(it->second.second);
        lru_list_.push_front(unicode);
        it->second.second = lru_list_.begin();
        
        return it->second.first.bitmap;
    }
    return nullptr;
}

const CharGlyphInfo* PageFontCache::findInIndex(uint16_t unicode) const {
    if (!file_index_) return nullptr;
    auto it = std::lower_bound(file_index_, file_index_ + file_char_count_, unicode, 
        [](const CharGlyphInfo& a, uint16_t b) { return a.unicode < b; });
        
    if (it != file_index_ + file_char_count_ && it->unicode == unicode) {
        return it;
    }
    return nullptr;
}
