#include "book_reader.h"
#include "utf8_utils.h"
#include "gbk_unicode_table.h"
#include <M5Unified.h>
#include <algorithm>

static TextEncoding detect_text_encoding(const uint8_t *buffer, size_t size)
{
    if (size < 3)
        return TextEncoding::UTF8;

    if (size >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF)
    {
        return TextEncoding::UTF8;
    }

    size_t valid_utf8_chars = 0;
    size_t total_chars = 0;
    size_t gbk_chars = 0;

    for (size_t i = 0; i < size && i < 1024; i++)
    {
        uint8_t byte = buffer[i];
        total_chars++;

        if (byte < 0x80)
        {
            valid_utf8_chars++;
            continue;
        }

        if ((byte & 0xE0) == 0xC0 && i + 1 < size)
        {
            uint8_t next = buffer[i + 1];
            if ((next & 0xC0) == 0x80)
            {
                valid_utf8_chars += 2;
                i++;
                continue;
            }
        }
        else if ((byte & 0xF0) == 0xE0 && i + 2 < size)
        {
            uint8_t next1 = buffer[i + 1];
            uint8_t next2 = buffer[i + 2];
            if ((next1 & 0xC0) == 0x80 && (next2 & 0xC0) == 0x80)
            {
                valid_utf8_chars += 3;
                i += 2;
                continue;
            }
        }

        if (byte >= 0xA1 && byte <= 0xFE && i + 1 < size)
        {
            uint8_t next = buffer[i + 1];
            if (next >= 0xA1 && next <= 0xFE)
            {
                gbk_chars += 2;
                i++;
                continue;
            }
        }
    }

    float gbk_ratio = 0.0f;
    if (total_chars == 0)
    {
        return TextEncoding::UTF8;
    }
    else
    {
        gbk_ratio = (float)gbk_chars / (float)total_chars;
    }

    if (gbk_ratio > 0.3)
    {
        return TextEncoding::GBK;
    }
    else
    {
        return TextEncoding::UTF8;
    }
}

static size_t map_converted_pos_to_raw_consumed(const std::string &raw, TextEncoding enc, size_t converted_pos)
{
    const uint8_t *buf = (const uint8_t *)raw.c_str();
    size_t raw_len = raw.length();
    size_t acc_converted_bytes = 0;
    size_t i = 0;

    if (enc == TextEncoding::UTF8)
    {
        while (i < raw_len)
        {
            uint8_t b = buf[i];
            if (b < 0x80)
            {
                acc_converted_bytes += 1;
                i += 1;
            }
            else if ((b & 0xE0) == 0xC0 && i + 1 < raw_len && (buf[i + 1] & 0xC0) == 0x80)
            {
                acc_converted_bytes += 2;
                i += 2;
            }
            else if ((b & 0xF0) == 0xE0 && i + 2 < raw_len && (buf[i + 1] & 0xC0) == 0x80 && (buf[i + 2] & 0xC0) == 0x80)
            {
                acc_converted_bytes += 3;
                i += 3;
            }
            else
            {
                acc_converted_bytes += 3; // placeholder U+25A1
                i += 1;
            }

            if (acc_converted_bytes >= converted_pos)
                return i;
        }
        return raw_len;
    }

    if (enc == TextEncoding::GBK)
    {
        while (i < raw_len)
        {
            uint8_t b = buf[i];
            if (b < 0x80)
            {
                acc_converted_bytes += 1;
                i += 1;
            }
            else if (i + 1 < raw_len && b >= 0xA1 && b <= 0xFE && buf[i + 1] >= 0xA1 && buf[i + 1] <= 0xFE)
            {
                uint16_t gbk_code = (uint16_t(b) << 8) | uint16_t(buf[i + 1]);
                uint16_t uni = gbk_to_unicode_lookup(gbk_code);
                if (uni != 0)
                {
                    uint8_t tmp[4];
                    acc_converted_bytes += utf8_encode(uni, tmp);
                }
                else
                {
                    acc_converted_bytes += 3; // placeholder
                }
                i += 2;
            }
            else if ((b & 0xE0) == 0xC0 && i + 1 < raw_len && (buf[i + 1] & 0xC0) == 0x80)
            {
                acc_converted_bytes += 2;
                i += 2;
            }
            else if ((b & 0xF0) == 0xE0 && i + 2 < raw_len && (buf[i + 1] & 0xC0) == 0x80 && (buf[i + 2] & 0xC0) == 0x80)
            {
                acc_converted_bytes += 3;
                i += 3;
            }
            else
            {
                acc_converted_bytes += 3; // placeholder
                i += 1;
            }

            if (acc_converted_bytes >= converted_pos)
                return i;
        }
        return raw_len;
    }
    return raw_len;
}

BookReader::BookReader() : current_page_start_offset(0), next_page_start_offset(0), line_spacing_factor(2.0f), font_path("/font.bin"), lock_screen_enabled(false), lock_screen_path("") {}

BookReader::~BookReader() {
  if (file) {
    file.close();
  }
}

bool BookReader::open(const std::string& path) {
  if (file) file.close();
  
  file = SD.open(path.c_str());
  if (!file) {
    Serial.println("Failed to open book file");
    return false;
  }
  
  current_book_path = path;
  current_page_start_offset = 0;
  next_page_start_offset = 0;
  page_offsets.clear();
  bookmarks.clear();
  
  // Detect encoding
  uint8_t detect_buffer[1024];
  size_t detect_size = file.read(detect_buffer, sizeof(detect_buffer));
  current_encoding = detect_text_encoding(detect_buffer, detect_size);
  file.seek(0); // Reset file pointer
  
  loadIndex(); // Try to load index
  
  return true;
}

bool BookReader::nextPage(PageFontCache& cache, int screen_w, int screen_h) {
  if (!file || page_index.empty()) return false;
  
  if (current_page_index + 1 < page_index.size()) {
      current_page_index++;
      current_page_start_offset = page_index[current_page_index];
      file.seek(current_page_start_offset);
      paginate(cache, screen_w, screen_h);
      saveConfig();
      return true;
  }
  return false; // No more pages
}

bool BookReader::prevPage(PageFontCache& cache, int screen_w, int screen_h) {
  if (!file || page_index.empty()) return false;
  
  if (current_page_index > 0) {
      current_page_index--;
      current_page_start_offset = page_index[current_page_index];
      file.seek(current_page_start_offset);
      paginate(cache, screen_w, screen_h);
      saveConfig();
      return true;
  }
  return false; // At beginning
}

bool BookReader::readCurrentPage(PageFontCache& cache, int screen_w, int screen_h) {
    if (!file) return false;
    file.seek(current_page_start_offset);
    paginate(cache, screen_w, screen_h);
    return !current_page_text.empty();
}

static bool cannotStartLine(uint32_t unicode) {
    switch (unicode) {
        case 0x3002: // 。
        case 0x3001: // 、
        case 0xFF0C: // ，
        case 0xFF1B: // ；
        case 0xFF1A: // ：
        case 0xFF1F: // ？
        case 0xFF01: // ！
        case 0xFF09: // ）
        case 0x3011: // 】
        case 0x300F: // 』
        case 0x300D: // 」
        case '!':
        case '?':
        case ',':
        case '.':
        case ';':
        case ':':
        case ')':
        case ']':
        case '}':
            return true;
        default:
            return false;
    }
}

void BookReader::paginate(PageFontCache& cache, int screen_w, int screen_h) {
  current_page_text = "";
  if (!file) return;

  size_t chunk_size = 4096;
  uint8_t* buffer = static_cast<uint8_t*>(malloc(chunk_size));
  if (!buffer) return;

  int read_bytes = file.read(buffer, chunk_size);
  if (read_bytes <= 0) {
    free(buffer);
    return;
  }

  int end_pos = read_bytes;
  while (end_pos > 0 && (buffer[end_pos - 1] & 0xC0) == 0x80) {
    end_pos--;
  }
  if (end_pos > 0 && (buffer[end_pos - 1] & 0x80) != 0) {
    end_pos--; 
  }

  std::string text(reinterpret_cast<char*>(buffer), end_pos);
  free(buffer);

  if (current_encoding == TextEncoding::GBK) {
      text = convert_gbk_to_utf8_lookup(text);
  }

  cache.build(text, font_path);

  int cursor_x = 0;
  int cursor_y = 0;
  int line_height = cache.getFontSize() * line_spacing_factor; 
  
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint8_t* end = p + text.size();
  const uint8_t* page_end = p;
  std::string paginated_text = "";
  const uint8_t* last_p = p;

  while (p < end) {
    int bytes = 0;
    uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
    
    if (bytes == 0) {
        p++;
        page_end = p;
        continue;
    }
    
    if (u == '\n') {
      paginated_text.append(reinterpret_cast<const char*>(last_p), p - last_p + bytes);
      cursor_x = 0;
      cursor_y += line_height;
      p += bytes;
      last_p = p;
      page_end = p;
      continue;
    }
    if (u == '\r') {
        p += bytes;
        continue;
    }

    const CharGlyphInfo* info = cache.getCharGlyphInfo(u);
    int char_w = info ? info->width : (u == ' ' ? cache.getFontSize() / 2 : cache.getFontSize()); 

    if (cursor_x + char_w > screen_w) {
      if (cursor_x == 0) {
          cursor_x += char_w;
          p += bytes;
          page_end = p;
          continue;
      } else if (cannotStartLine(u) && cursor_x + char_w <= screen_w * 1.15f) {
          cursor_x += char_w;
          p += bytes;
          page_end = p;
          continue;
      } else {
          paginated_text.append(reinterpret_cast<const char*>(last_p), p - last_p);
          paginated_text.append("\n");
          cursor_x = 0;
          cursor_y += line_height;
          last_p = p;
          continue;
      }
    }

    if (cursor_y + cache.getFontSize() > screen_h) {
      break;
    }

    cursor_x += char_w;
    p += bytes;
    page_end = p; 
  }

  paginated_text.append(reinterpret_cast<const char*>(last_p), page_end - last_p);

  current_page_text = paginated_text;
  next_page_start_offset = current_page_start_offset + (page_end - reinterpret_cast<const uint8_t*>(text.c_str()));
}



void BookReader::seekTo(uint32_t offset) {
  page_offsets.clear();
  
  if (page_index.empty()) {
    current_page_index = 0;
    current_page_start_offset = offset;
  } else {
    auto it = std::upper_bound(page_index.begin(), page_index.end(), offset);
    if (it == page_index.begin()) {
      current_page_index = 0;
    } else {
      current_page_index = std::distance(page_index.begin(), it) - 1;
    }
    current_page_start_offset = page_index[current_page_index];
  }
  next_page_start_offset = current_page_start_offset;
}

bool BookReader::jumpToPage(int index, PageFontCache& cache, int screen_w,
                            int screen_h) {
    if (index >= 0 && index < page_index.size()) {
        current_page_index = index;
        current_page_start_offset = page_index[current_page_index];
        file.seek(current_page_start_offset);
        paginate(cache, screen_w, screen_h);
        saveConfig();
        return true;
    }
    return false;
}

bool BookReader::jumpToPercentage(float percentage, PageFontCache& cache,
                                  int screen_w, int screen_h) {
    if (page_index.empty()) return false;
    int target_index = (page_index.size() - 1) * (percentage / 100.0f);
    if (target_index < 0) target_index = 0;
    if (target_index >= page_index.size()) target_index = page_index.size() - 1;
    return jumpToPage(target_index, cache, screen_w, screen_h);
}

bool BookReader::saveConfig() {
  if (current_book_path.empty()) return false;
  
  size_t last_slash = current_book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? current_book_path : current_book_path.substr(last_slash + 1);
  
  std::string config_path = "/configs/" + filename + ".cfg";
  
  if (SD.exists(config_path.c_str())) {
    SD.remove(config_path.c_str());
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_WRITE);
  if (!cfg_file) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  
  cfg_file.printf("pos=%u\n", current_page_start_offset);
  bool completed = (!page_index.empty() && current_page_index + 1 == page_index.size());
  cfg_file.printf("completed=%d\n", completed ? 1 : 0);
  for (uint32_t bm : bookmarks) {
    cfg_file.printf("bookmark=%u\n", bm);
  }
  
  cfg_file.close();
  return true;
}

bool BookReader::loadConfig(const std::string& book_path) {
  size_t last_slash = book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? book_path : book_path.substr(last_slash + 1);
  
  std::string config_path = "/configs/" + filename + ".cfg";
  
  if (!SD.exists(config_path.c_str())) {
    return false;
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_READ);
  if (!cfg_file) {
    return false;
  }
  
  bookmarks.clear();
  
  while (cfg_file.available()) {
    String line = cfg_file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("pos=")) {
      uint32_t pos = line.substring(4).toInt();
      seekTo(pos);
    } else if (line.startsWith("bookmark=")) {
      uint32_t bm = line.substring(9).toInt();
      bookmarks.push_back(bm);
    }
  }
  
  cfg_file.close();
  return true;
}

bool BookReader::saveGlobalConfig() {
  std::string config_path = "/configs/settings.cfg";
  
  if (SD.exists(config_path.c_str())) {
    SD.remove(config_path.c_str());
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_WRITE);
  if (!cfg_file) {
    Serial.println("Failed to open global config file for writing");
    return false;
  }
  
  cfg_file.printf("line_spacing=%.1f\n", line_spacing_factor);
  cfg_file.printf("font_path=%s\n", font_path.c_str());
  cfg_file.printf("lock_screen_enabled=%d\n", lock_screen_enabled ? 1 : 0);
  cfg_file.printf("lock_screen_path=%s\n", lock_screen_path.c_str());
  cfg_file.close();
  return true;
}

bool BookReader::loadGlobalConfig() {
  std::string config_path = "/configs/settings.cfg";
  
  if (!SD.exists(config_path.c_str())) {
    return false;
  }
  
  File cfg_file = SD.open(config_path.c_str(), FILE_READ);
  if (!cfg_file) {
    return false;
  }
  
  while (cfg_file.available()) {
    String line = cfg_file.readStringUntil('\n');
    line.trim();
    if (line.startsWith("line_spacing=")) {
      line_spacing_factor = line.substring(13).toFloat();
    } else if (line.startsWith("font_path=")) {
      font_path = line.substring(10).c_str();
    } else if (line.startsWith("lock_screen_enabled=")) {
      lock_screen_enabled = line.substring(20).toInt() == 1;
    } else if (line.startsWith("lock_screen_path=")) {
      lock_screen_path = line.substring(17).c_str();
    }
  }
  
  cfg_file.close();
  return true;
}

bool BookReader::buildIndex(PageFontCache& cache, int screen_w, int screen_h) {
    if (!file) return false;
    
    Serial.println("Building global index (optimized)...");
    page_index.clear();
    uint32_t scan_pos = 0;
    
    size_t chunk_size = 4096;
    uint8_t* buffer = static_cast<uint8_t*>(malloc(chunk_size));
    if (!buffer) return false;
    
    int font_size = cache.getFontSize();
    int line_height = font_size * line_spacing_factor;
    
    while (scan_pos < file.size()) {
        page_index.push_back(scan_pos);
        
        int cursor_x = 0;
        int cursor_y = 0;
        
        uint32_t current_page_offset = scan_pos;
        bool page_filled = false;
        
        while (!page_filled && current_page_offset < file.size()) {
            file.seek(current_page_offset);
            int read_bytes = file.read(buffer, chunk_size);
            if (read_bytes <= 0) break;
            
            // Handle UTF-8 truncation at chunk boundary
            int end_pos = read_bytes;
            while (end_pos > 0 && (buffer[end_pos - 1] & 0xC0) == 0x80) {
                end_pos--;
            }
            if (end_pos > 0 && (buffer[end_pos - 1] & 0x80) != 0) {
                end_pos--; 
            }
            
            if (end_pos == 0 && read_bytes > 0) {
                current_page_offset += read_bytes;
                continue;
            }
            
            std::string chunk_text(reinterpret_cast<char*>(buffer), end_pos);
            if (current_encoding == TextEncoding::GBK) {
                chunk_text = convert_gbk_to_utf8_lookup(chunk_text);
            }
            
            const uint8_t* p = reinterpret_cast<const uint8_t*>(chunk_text.c_str());
            const uint8_t* chunk_start = p;
            const uint8_t* end = p + chunk_text.size();
            const uint8_t* page_end = p;
            const uint8_t* last_p = p;
            
            while (p < end) {
                int bytes = 0;
                uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
                if (bytes == 0) {
                    p++;
                    page_end = p;
                    continue;
                }
                
                if (u == '\n') {
                    cursor_x = 0;
                    cursor_y += line_height;
                    p += bytes;
                    page_end = p;
                    last_p = p;
                    continue;
                }
                if (u == '\r') {
                    p += bytes;
                    continue;
                }
                
                const CharGlyphInfo* info = cache.getCharGlyphInfoDirect(u);
                int char_w = info ? info->width : (u == ' ' ? font_size / 2 : font_size);
                
                if (cursor_x + char_w > screen_w) {
                    if (cursor_x == 0) {
                        cursor_x += char_w;
                        p += bytes;
                        page_end = p;
                        continue;
                    } else if (cannotStartLine(u) && cursor_x + char_w <= screen_w * 1.15f) {
                        cursor_x += char_w;
                        p += bytes;
                        page_end = p;
                        continue;
                    } else {
                        cursor_x = 0;
                        cursor_y += line_height;
                        last_p = p;
                        continue;
                    }
                }
                
                if (cursor_y + font_size > screen_h) {
                    page_filled = true;
                    break;
                }
                
                cursor_x += char_w;
                p += bytes;
                page_end = p;
            }
            
            if (current_encoding == TextEncoding::GBK) {
                size_t converted_consumed = page_end - chunk_start;
                size_t raw_consumed = map_converted_pos_to_raw_consumed(std::string(reinterpret_cast<char*>(buffer), end_pos), current_encoding, converted_consumed);
                current_page_offset += raw_consumed;
            } else {
                current_page_offset += (page_end - chunk_start);
            }
            
            if (page_filled) {
                break; 
            }
        }
        
        scan_pos = current_page_offset;
        if (scan_pos == page_index.back()) {
            break;
        }
    }
    
    free(buffer);
    Serial.printf("Index built with %d pages\n", page_index.size());
    
    if (page_index.empty()) {
        current_page_index = 0;
    } else {
        auto it = std::upper_bound(page_index.begin(), page_index.end(), current_page_start_offset);
        if (it == page_index.begin()) {
            current_page_index = 0;
        } else {
            current_page_index = std::distance(page_index.begin(), it) - 1;
        }
        current_page_start_offset = page_index[current_page_index];
    }
    
    return true;
}

bool BookReader::saveIndex() {
    if (current_book_path.empty() || page_index.empty()) return false;
    
    size_t last_slash = current_book_path.find_last_of('/');
    std::string filename = (last_slash == std::string::npos) ? current_book_path : current_book_path.substr(last_slash + 1);
    
    std::string config_path = "/configs/" + filename + ".idx";
    
    if (SD.exists(config_path.c_str())) {
        SD.remove(config_path.c_str());
    }
    
    File idx_file = SD.open(config_path.c_str(), FILE_WRITE);
    if (!idx_file) {
        Serial.println("Failed to open idx file for writing");
        return false;
    }
    
    for (uint32_t offset : page_index) {
        idx_file.printf("%u\n", offset);
    }
    
    idx_file.close();
    return true;
}

bool BookReader::loadIndex() {
    if (current_book_path.empty()) return false;
    
    size_t last_slash = current_book_path.find_last_of('/');
    std::string filename = (last_slash == std::string::npos) ? current_book_path : current_book_path.substr(last_slash + 1);
    
    std::string config_path = "/configs/" + filename + ".idx";
    
    if (!SD.exists(config_path.c_str())) {
        return false;
    }
    
    File idx_file = SD.open(config_path.c_str(), FILE_READ);
    if (!idx_file) {
        return false;
    }
    
    page_index.clear();
    while (idx_file.available()) {
        String line = idx_file.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty()) {
            page_index.push_back(line.toInt());
        }
    }
    
    idx_file.close();
    Serial.printf("Loaded index with %d pages\n", page_index.size());
    return true;
}

void BookReader::addBookmark() {
  for (uint32_t bm : bookmarks) {
    if (bm == current_page_start_offset) return;
  }
  bookmarks.push_back(current_page_start_offset);
  saveConfig();
}

void BookReader::removeBookmark(uint32_t offset) {
  auto it = std::find(bookmarks.begin(), bookmarks.end(), offset);
  if (it != bookmarks.end()) {
    bookmarks.erase(it);
    saveConfig();
  }
}
