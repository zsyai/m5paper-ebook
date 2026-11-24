#include "book/page_layout.h"

#include <cstring>

#include "book/utf8_utils.h"

namespace reader {
namespace book {
namespace {

constexpr uint32_t kIdeographicSpace = 0x3000;
// Two U+3000 in UTF-8.
constexpr char kIndentText[] = "\xE3\x80\x80\xE3\x80\x80";

void Append(std::string* out, const uint8_t* p, int bytes) {
  if (out) out->append(reinterpret_cast<const char*>(p), bytes);
}

// Returns the byte length of a standalone `\x1E...\n` line starting at `p`
// (including its trailing `\n`, or up to `end` at EOF).
size_t ImageMarkerLineBytes(const uint8_t* p, const uint8_t* end) {
  const uint8_t* q = p + 1;
  while (q < end && *q != '\n') ++q;
  if (q < end && *q == '\n') ++q;
  return static_cast<size_t>(q - p);
}

}  // namespace

bool ParseImagePageMarker(const std::string& text, uint32_t* zip_entry_index) {
  constexpr size_t kPrefixLen = sizeof(kImagePagePrefix) - 1;
  if (text.size() <= kPrefixLen ||
      std::memcmp(text.data(), kImagePagePrefix, kPrefixLen) != 0) {
    return false;
  }
  uint64_t val = 0;
  size_t digits = 0;
  for (size_t i = kPrefixLen; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n' || c == '\r') break;
    if (c < '0' || c > '9') return false;
    val = val * 10 + static_cast<uint64_t>(c - '0');
    if (val > 0xFFFFFFFFull) return false;
    ++digits;
  }
  if (digits == 0) return false;
  if (zip_entry_index) *zip_entry_index = static_cast<uint32_t>(val);
  return true;
}

bool CannotStartLine(uint32_t unicode) {
  switch (unicode) {
    case 0x3002:  // 。
    case 0x3001:  // 、
    case 0xFF0C:  // ，
    case 0xFF1B:  // ；
    case 0xFF1A:  // ：
    case 0xFF1F:  // ？
    case 0xFF01:  // ！
    case 0xFF09:  // ）
    case 0x3011:  // 】
    case 0x300F:  // 』
    case 0x300D:  // 」
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

bool IsIndentSpace(uint32_t unicode) {
  return unicode == ' ' || unicode == '\t' || unicode == kIdeographicSpace ||
         unicode == 0x00A0;
}

size_t MeasurePage(const uint8_t* text, size_t len, const LayoutParams& layout,
                   const PageFontCache& cache, LayoutCursor* cursor,
                   bool* page_filled, std::string* out) {
  const uint8_t* p = text;
  const uint8_t* end = text + len;
  const uint8_t* page_end = p;
  const float overflow_w = layout.text_w * 1.15f;
  *page_filled = false;

  auto advance = [&](uint32_t u) {
    const CharGlyphInfo* info = cache.getCharGlyphInfoDirect(u);
    return info ? static_cast<int>(info->width)
                : (u == ' ' ? layout.font_size / 2 : layout.font_size);
  };

  while (p < end) {
    if (cursor->y > layout.text_h) {
      *page_filled = true;
      break;
    }
    if (*p == kImagePageSentinel && cursor->x == 0) {
      if (cursor->y > 0) {
        *page_filled = true;
        break;
      }
      const size_t marker_bytes = ImageMarkerLineBytes(p, end);
      Append(out, p, static_cast<int>(marker_bytes));
      p += marker_bytes;
      page_end = p;
      cursor->x = 0;
      cursor->y = layout.text_h + 1;
      cursor->paragraph_start = true;
      continue;
    }
    int bytes = 0;
    uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
    if (bytes == 0) {
      p++;
      page_end = p;
      cursor->paragraph_start = false;
      continue;
    }
    if (u == '\n') {
      if (out) out->push_back('\n');
      cursor->x = 0;
      cursor->y += layout.line_height;
      cursor->paragraph_start = true;
      p += bytes;
      page_end = p;
      continue;
    }
    if (u == '\r') {
      p += bytes;
      continue;
    }

    if (layout.paragraph_indent && cursor->paragraph_start) {
      // The paragraph's own leading whitespace is replaced by the indent.
      // page_end stays before it, so a page that breaks here starts right
      // after the '\n' and is indented too.
      if (IsIndentSpace(u)) {
        p += bytes;
        continue;
      }
      if (cursor->y + layout.font_size > layout.text_h) {
        *page_filled = true;
        break;
      }
      if (out) out->append(kIndentText);
      cursor->x += 2 * advance(kIdeographicSpace);
      cursor->paragraph_start = false;
    }

    int char_w = advance(u);

    if (cursor->x + char_w > layout.text_w) {
      if (cursor->x == 0) {
        Append(out, p, bytes);
        cursor->x += char_w;
        cursor->paragraph_start = false;
        p += bytes;
        page_end = p;
        continue;
      } else if (CannotStartLine(u) && cursor->x + char_w <= overflow_w) {
        Append(out, p, bytes);
        cursor->x += char_w;
        cursor->paragraph_start = false;
        p += bytes;
        page_end = p;
        continue;
      } else {
        if (out) out->push_back('\n');
        cursor->x = 0;
        cursor->y += layout.line_height;
        continue;
      }
    }

    if (cursor->y + layout.font_size > layout.text_h) {
      *page_filled = true;
      break;
    }

    Append(out, p, bytes);
    cursor->x += char_w;
    cursor->paragraph_start = false;
    p += bytes;
    page_end = p;
  }

  // A chunk holding only characters that never advance page_end ('\r', or
  // indent whitespace) would otherwise be offered again forever.
  if (!*page_filled && page_end == text) page_end = end;
  return page_end - text;
}

void MeasureLines(const uint8_t* text, size_t len, const LayoutParams& layout,
                  const PageFontCache& cache, bool start_is_paragraph,
                  std::vector<LineSpan>* lines) {
  lines->clear();
  lines->push_back(LineSpan{0, false, true, false});
  const uint8_t* p = text;
  const uint8_t* end = text + len;
  const float overflow_w = layout.text_w * 1.15f;
  int x = 0;
  bool paragraph_start = start_is_paragraph;
  size_t para_start_offset = start_is_paragraph ? 0 : len + 1;

  auto advance = [&](uint32_t u) {
    const CharGlyphInfo* info = cache.getCharGlyphInfoDirect(u);
    return info ? static_cast<int>(info->width)
                : (u == ' ' ? layout.font_size / 2 : layout.font_size);
  };

  while (p < end) {
    const size_t pos = static_cast<size_t>(p - text);
    if (*p == kImagePageSentinel && x == 0) {
      const size_t marker_bytes = ImageMarkerLineBytes(p, end);
      if (!lines->back().has_visible && lines->back().utf8_start == pos) {
        lines->back().has_visible = true;
        lines->back().can_start_page = true;
        lines->back().standalone_page = true;
      } else {
        lines->push_back(LineSpan{pos, true, true, true});
      }
      p += marker_bytes;
      x = 0;
      paragraph_start = true;
      para_start_offset = static_cast<size_t>(p - text);
      lines->push_back(LineSpan{para_start_offset, false, true, false});
      continue;
    }
    int bytes = 0;
    const uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
    if (bytes == 0) {
      p++;
      paragraph_start = false;
      continue;
    }
    if (u == '\n') {
      x = 0;
      paragraph_start = true;
      p += bytes;
      para_start_offset = static_cast<size_t>(p - text);
      lines->push_back(LineSpan{para_start_offset, false, true, false});
      continue;
    }
    if (u == '\r') {
      p += bytes;
      continue;
    }

    if (layout.paragraph_indent && paragraph_start) {
      if (IsIndentSpace(u)) {
        p += bytes;
        continue;
      }
      lines->back().has_visible = true;
      x += 2 * advance(kIdeographicSpace);
      paragraph_start = false;
    }

    const int char_w = advance(u);
    if (x + char_w > layout.text_w) {
      if (x == 0) {
        lines->back().has_visible = true;
        x += char_w;
        paragraph_start = false;
        p += bytes;
        continue;
      } else if (CannotStartLine(u) && x + char_w <= overflow_w) {
        x += char_w;
        paragraph_start = false;
        p += bytes;
        continue;
      } else {
        x = 0;
        lines->push_back(
            LineSpan{pos, false, pos != para_start_offset, false});
        continue;
      }
    }

    lines->back().has_visible = true;
    x += char_w;
    paragraph_start = false;
    p += bytes;
  }
}

}  // namespace book
}  // namespace reader
