#include "book/page_layout.h"

#include "utf8_utils.h"

namespace reader {
namespace book {
namespace {

constexpr uint32_t kIdeographicSpace = 0x3000;
// Two U+3000 in UTF-8.
constexpr char kIndentText[] = "\xE3\x80\x80\xE3\x80\x80";

void Append(std::string* out, const uint8_t* p, int bytes) {
  if (out) out->append(reinterpret_cast<const char*>(p), bytes);
}

}  // namespace

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

}  // namespace book
}  // namespace reader
