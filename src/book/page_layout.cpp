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

// ---------------------------------------------------------------------------
// Word-based line breaking (Latin scripts) with French hyphenation.
// CJK characters keep the original per-character breaking.

// Breakable space between words. No-break spaces (U+00A0, U+202F) glue.
bool IsBreakSpace(uint32_t u) { return u == ' ' || u == '\t'; }

// Characters laid out one by one, breakable anywhere (CJK and full-width).
bool IsCjk(uint32_t u) { return u >= 0x2E80 && !(u >= 0xFB00 && u <= 0xFB06); }

// A line may break right after these, inside a word ("peut-être").
bool IsBreakAfter(uint32_t u) {
  return u == '-' || u == 0x2010 || u == 0x2013 || u == 0x2014 || u == '/';
}

bool EndsWord(uint32_t u) {
  return u == '\n' || u == '\r' || IsBreakSpace(u) || IsCjk(u);
}

uint32_t ToLowerLatin(uint32_t u) {
  if (u >= 'A' && u <= 'Z') return u + 32;
  if (u >= 0xC0 && u <= 0xDE && u != 0xD7) return u + 32;
  if (u == 0x152) return 0x153;  // Œ
  if (u == 0x178) return 0xFF;   // Ÿ
  return u;
}

bool IsLetter(uint32_t u) {
  u = ToLowerLatin(u);
  return (u >= 'a' && u <= 'z') || (u >= 0xDF && u <= 0xFF && u != 0xF7) ||
         u == 0x153 || u == 0xE6;
}

bool IsVowel(uint32_t u) {
  switch (ToLowerLatin(u)) {
    case 'a': case 'e': case 'i': case 'o': case 'u': case 'y':
    case 0xE0: case 0xE2: case 0xE4: case 0xE6: case 0xE9: case 0xE8:
    case 0xEA: case 0xEB: case 0xEE: case 0xEF: case 0xF4: case 0xF6:
    case 0xF9: case 0xFB: case 0xFC: case 0xFF: case 0x153:
      return true;
    default:
      return false;
  }
}

// Consonant pairs French never splits across lines.
bool Inseparable(uint32_t a, uint32_t b) {
  a = ToLowerLatin(a);
  b = ToLowerLatin(b);
  if (b == 'l') return a == 'b' || a == 'c' || a == 'f' || a == 'g' || a == 'p';
  if (b == 'r') {
    return a == 'b' || a == 'c' || a == 'd' || a == 'f' || a == 'g' ||
           a == 'p' || a == 't' || a == 'v';
  }
  if (b == 'h') return a == 'c' || a == 'p' || a == 't';
  return a == 'g' && b == 'n';
}

bool CannotStartLineMark(uint32_t u);

// True if every character of [p, p + len) is a mark that cannot start a line.
bool IsPunctuationOnly(const uint8_t* p, size_t len) {
  const uint8_t* end = p + len;
  while (p < end) {
    int bytes = 0;
    const uint32_t u = Utf8Utils::getNextCharUnicode(p, end, bytes);
    if (bytes == 0 || !CannotStartLineMark(u)) return false;
    p += bytes;
  }
  return true;
}

constexpr int kMaxWordChars = 48;
constexpr int kMinBefore = 2;  // letters kept before a hyphen
constexpr int kMinAfter = 3;   // letters carried to the next line

struct Breaker {
  const LayoutParams& layout;
  const PageFontCache& cache;
  // Inside a word already placed: no more decisions until `word_end`.
  const uint8_t* word_end = nullptr;
  // Hyphenate (emit '-' + line break) on reaching this position.
  const uint8_t* hyphen_at = nullptr;
  // Word wider than a whole line: wrap it character by character.
  bool char_wrap = false;

  Breaker(const LayoutParams& l, const PageFontCache& c) : layout(l), cache(c) {}

  int Advance(uint32_t u) const {
    const CharGlyphInfo* info = cache.getCharGlyphInfoDirect(u);
    return info ? static_cast<int>(info->width)
                : (u == ' ' ? layout.font_size / 2 : layout.font_size);
  }

  enum class Act { kEmit, kSkip, kBreak, kBreakHyphen, kNeedMore };

  // Scans the word starting at `p`. Returns its byte length and width; sets
  // `*complete` to false if it runs into `end` (more text may follow).
  size_t ScanWord(const uint8_t* p, const uint8_t* end, int* width,
                  bool* complete) const {
    const uint8_t* q = p;
    *width = 0;
    *complete = true;
    while (q < end) {
      if (*q == kImagePageSentinel) return q - p;
      int bytes = 0;
      const uint32_t u = Utf8Utils::getNextCharUnicode(q, end, bytes);
      if (bytes == 0 || EndsWord(u)) return q - p;
      *width += Advance(u);
      q += bytes;
      if (IsBreakAfter(u)) return q - p;
    }
    *complete = false;
    return q - p;
  }

  // Byte length of the longest prefix of word [p, p + len) that may end a
  // line with a hyphen and fits in `avail` pixels hyphen included, or 0.
  size_t HyphenPrefix(const uint8_t* p, size_t len, int avail) const {
    uint32_t cps[kMaxWordChars];
    size_t offs[kMaxWordChars + 1];
    int widths[kMaxWordChars];
    int n = 0;
    const uint8_t* q = p;
    const uint8_t* end = p + len;
    while (q < end) {
      if (n == kMaxWordChars) return 0;
      int bytes = 0;
      const uint32_t u = Utf8Utils::getNextCharUnicode(q, end, bytes);
      if (bytes == 0) return 0;
      cps[n] = u;
      offs[n] = q - p;
      widths[n] = Advance(u);
      ++n;
      q += bytes;
    }
    offs[n] = len;
    // Letters run [a, b): after an elision (l', qu') and before trailing
    // punctuation; anything else in the word disables hyphenation.
    int a = 0;
    for (int i = 0; i < n; ++i) {
      if (cps[i] == '\'' || cps[i] == 0x2019) a = i + 1;
    }
    while (a < n && !IsLetter(cps[a])) ++a;
    int b = a;
    while (b < n && IsLetter(cps[b])) ++b;
    for (int i = b; i < n; ++i) {
      if (IsLetter(cps[i]) || (cps[i] >= '0' && cps[i] <= '9')) return 0;
    }
    if (b - a < kMinBefore + kMinAfter) return 0;
    for (int i = 0; i < a; ++i) {
      if (cps[i] >= '0' && cps[i] <= '9') return 0;
    }

    // "qu" and "gu" before a vowel act as one consonant.
    auto vowel_at = [&](int i) {
      if (!IsVowel(cps[i])) return false;
      if (ToLowerLatin(cps[i]) == 'u' && i > a && i + 1 < b &&
          IsVowel(cps[i + 1])) {
        const uint32_t prev = ToLowerLatin(cps[i - 1]);
        if (prev == 'q' || prev == 'g') return false;
      }
      return true;
    };

    const int hyphen_w = Advance('-');
    int best = 0;
    int prefix_w = 0;
    for (int i = 0; i < a; ++i) prefix_w += widths[i];
    // Walk consonant clusters between two vowels and keep the last allowed
    // break that still fits.
    int i = a;
    int w = prefix_w;
    while (i < b) {
      if (!vowel_at(i)) {
        w += widths[i];
        ++i;
        continue;
      }
      // i: vowel. Find the following consonant cluster [c0, c1).
      int c0 = i + 1;
      int c1 = c0;
      while (c1 < b && !vowel_at(c1)) ++c1;
      if (c1 >= b) break;  // no vowel after: word end
      const int k = c1 - c0;
      int cut = -1;  // break before index `cut`
      if (k == 1) {
        cut = (ToLowerLatin(cps[c0]) == 'x') ? -1 : c0;
      } else if (k == 2) {
        cut = Inseparable(cps[c0], cps[c0 + 1]) ? c0 : c0 + 1;
      } else if (k >= 3) {
        cut = Inseparable(cps[c1 - 2], cps[c1 - 1]) ? c1 - 2 : c1 - 1;
      }
      // Width up to the cut.
      int cut_w = w + widths[i];
      for (int j = c0; j < cut && j < c1; ++j) cut_w += widths[j];
      if (cut > 0 && cut - a >= kMinBefore && b - cut >= kMinAfter &&
          cut_w + hyphen_w <= avail) {
        best = cut;
      }
      if (cut_w + hyphen_w > avail) break;
      // Advance past the vowel and the cluster.
      w += widths[i];
      for (int j = c0; j < c1; ++j) w += widths[j];
      i = c1;
    }
    return best > 0 ? offs[best] : 0;
  }

  // Decides what to do with character `u` at `p`. `text`/`end` bound the
  // buffer; `more_follows` tells whether text continues after `end`.
  Act Decide(const uint8_t* text, const uint8_t* p, const uint8_t* end,
             bool more_follows, uint32_t u, int x, bool paragraph_start,
             int* char_w) {
    if (hyphen_at && p >= hyphen_at) {
      hyphen_at = nullptr;
      word_end = nullptr;
      char_wrap = false;
      return Act::kBreakHyphen;
    }
    if (word_end && p >= word_end) {
      word_end = nullptr;
      char_wrap = false;
    }
    *char_w = Advance(u);
    const float overflow_w = layout.text_w * 1.15f;

    if (word_end) {
      if (char_wrap && x > 0 && x + *char_w > layout.text_w) return Act::kBreak;
      return Act::kEmit;
    }
    if (IsBreakSpace(u)) {
      return (x == 0 && !paragraph_start) ? Act::kSkip : Act::kEmit;
    }
    if (IsCjk(u)) {
      if (x + *char_w > layout.text_w && x > 0 &&
          !(CannotStartLine(u) && x + *char_w <= overflow_w)) {
        return Act::kBreak;
      }
      return Act::kEmit;
    }

    // Start of a word.
    int word_w = 0;
    bool complete = true;
    const size_t word_len = ScanWord(p, end, &word_w, &complete);
    if (!complete && more_follows && p != text) return Act::kNeedMore;
    if (word_len == 0) return Act::kEmit;
    if (x + word_w <= layout.text_w) {
      word_end = p + word_len;
      return Act::kEmit;
    }
    // A lone mark that must not start a line (" ?", " !", " »") may hang
    // into the margin instead.
    if (x > 0 && IsPunctuationOnly(p, word_len) &&
        x + word_w <= overflow_w) {
      word_end = p + word_len;
      return Act::kEmit;
    }
    const size_t cut = HyphenPrefix(p, word_len, layout.text_w - x);
    if (cut > 0) {
      word_end = p + word_len;
      hyphen_at = p + cut;
      return Act::kEmit;
    }
    if (x > 0) return Act::kBreak;
    word_end = p + word_len;
    char_wrap = true;
    return Act::kEmit;
  }
};

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
    case 0x00BB:  // »
    case 0x2026:  // …
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

namespace {
bool CannotStartLineMark(uint32_t u) {
  return CannotStartLine(u) || u == 0x00A0 || u == 0x202F;
}
}  // namespace

bool IsIndentSpace(uint32_t unicode) {
  return unicode == ' ' || unicode == '\t' || unicode == kIdeographicSpace ||
         unicode == 0x00A0;
}

size_t MeasurePage(const uint8_t* text, size_t len, const LayoutParams& layout,
                   const PageFontCache& cache, LayoutCursor* cursor,
                   bool* page_filled, std::string* out, bool more_follows) {
  const uint8_t* p = text;
  const uint8_t* end = text + len;
  const uint8_t* page_end = p;
  *page_filled = false;
  Breaker breaker(layout, cache);

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
      cursor->x += 2 * breaker.Advance(kIdeographicSpace);
      cursor->paragraph_start = false;
    }

    int char_w = 0;
    const Breaker::Act act =
        breaker.Decide(text, p, end, more_follows, u, cursor->x,
                       cursor->paragraph_start, &char_w);
    if (act == Breaker::Act::kNeedMore) {
      // The word continues in the next chunk: resume from its start.
      return p - text;
    }
    if (act == Breaker::Act::kSkip) {
      p += bytes;
      page_end = p;
      continue;
    }
    if (act == Breaker::Act::kBreak || act == Breaker::Act::kBreakHyphen) {
      if (out) {
        if (act == Breaker::Act::kBreakHyphen) out->push_back('-');
        out->push_back('\n');
      }
      cursor->x = 0;
      cursor->y += layout.line_height;
      continue;
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
  int x = 0;
  bool paragraph_start = start_is_paragraph;
  size_t para_start_offset = start_is_paragraph ? 0 : len + 1;
  Breaker breaker(layout, cache);

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
      x += 2 * breaker.Advance(kIdeographicSpace);
      paragraph_start = false;
    }

    int char_w = 0;
    const Breaker::Act act = breaker.Decide(text, p, end, /*more_follows=*/false,
                                            u, x, paragraph_start, &char_w);
    if (act == Breaker::Act::kSkip) {
      p += bytes;
      continue;
    }
    if (act == Breaker::Act::kBreak || act == Breaker::Act::kBreakHyphen) {
      x = 0;
      lines->push_back(LineSpan{pos, false, pos != para_start_offset, false});
      continue;
    }

    lines->back().has_visible = true;
    x += char_w;
    paragraph_start = false;
    p += bytes;
  }
}

}  // namespace book
}  // namespace reader
