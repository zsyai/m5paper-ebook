#include "book/book_toc.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "book/utf8_utils.h"
#include "core/sd_path.h"
#include "hal/storage.h"

namespace reader {
namespace book {
namespace {

constexpr char kTocMagic[4] = {'R', 'T', 'O', 'C'};
constexpr uint16_t kTocVersion = 1;
constexpr uint16_t kFlagComplete = 1u << 0;

constexpr size_t kScanWindowBytes = 32 * 1024;
constexpr size_t kMaxTocEntries = 4096;
constexpr size_t kMaxTitleBytes = 240;
constexpr size_t kMaxCarryBytes = 1024;

struct __attribute__((packed)) TocHeader {
  char magic[4];
  uint16_t version;
  uint16_t flags;
  uint32_t source_size;
  uint32_t source_mtime;
  uint32_t content_size;
  uint16_t book_title_len;
  uint16_t reserved;
  uint32_t entry_count;
};

struct __attribute__((packed)) DiskTocEntry {
  uint32_t text_offset;
  uint8_t level;
  uint8_t reserved;
  uint16_t title_len;
};

std::string FilenameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

bool IsWsCodepoint(uint32_t u) {
  return u == ' ' || u == '\t' || u == '\r' || u == '\n' || u == 0x3000 ||
         u == 0x00A0 || u == 0xFEFF;
}

bool IsCjkNumeralOrDigit(uint32_t u) {
  if (u >= '0' && u <= '9') return true;
  if (u >= 0xFF10 && u <= 0xFF19) return true;  // Fullwidth 0-9
  switch (u) {
    case 0x96F6:  // 零
    case 0x3007:  // 〇
    case 0x4E00:  // 一
    case 0x4E8C:  // 二
    case 0x4E24:  // 两
    case 0x4E09:  // 三
    case 0x56DB:  // 四
    case 0x4E94:  // 五
    case 0x516D:  // 六
    case 0x4E03:  // 七
    case 0x516B:  // 八
    case 0x4E5D:  // 九
    case 0x5341:  // 十
    case 0x767E:  // 百
    case 0x5343:  // 千
    case 0x4E07:  // 万
    case 0x58F9:  // 壹
    case 0x8D30:  // 贰
    case 0x53C1:  // 叁
    case 0x8086:  // 肆
    case 0x4F0D:  // 伍
    case 0x9646:  // 陆
    case 0x67D2:  // 柒
    case 0x634C:  // 捌
    case 0x7396:  // 玖
    case 0x62FE:  // 拾
    case 0x4F70:  // 佰
    case 0x4EDF:  // 仟
      return true;
    default:
      return false;
  }
}

// Returns 0 for volume-level units, 1 for chapter-level units, or -1 if not a
// chapter/volume unit.
int HeadingUnitLevel(uint32_t u) {
  switch (u) {
    case 0x5377:  // 卷
    case 0x90E8:  // 部
    case 0x96C6:  // 集
    case 0x7BC7:  // 篇
    case 0x518C:  // 册
    case 0x5B63:  // 季
      return 0;
    case 0x7AE0:  // 章
    case 0x56DE:  // 回
    case 0x8282:  // 节
    case 0x8BDD:  // 话
    case 0x5E55:  // 幕
    case 0x6298:  // 折
      return 1;
    default:
      return -1;
  }
}

// Rejects common narrative continuations like "第一章的伏笔..." or "第一节课...".
bool DisallowedAfterUnit(uint32_t u) {
  switch (u) {
    case 0x7684:  // 的
    case 0x4E86:  // 了
    case 0x7740:  // 着
    case 0x5728:  // 在
    case 0x662F:  // 是
    case 0x6709:  // 有
    case 0x548C:  // 和
    case 0x4E0E:  // 与
    case 0x91CC:  // 里
    case 0x4E2D:  // 中
    case 0x4E0A:  // 上
    case 0x4E0B:  // 下
    case 0x524D:  // 前
    case 0x540E:  // 后
    case 0x5185:  // 内
    case 0x5916:  // 外
    case 0x8BFE:  // 课
    case 0x8F66:  // 车
    case 0xFF0C:  // ，
    case ',':
      return true;
    default:
      return false;
  }
}

bool StartsWithAsciiIgnoreCase(const std::string& s, const char* prefix) {
  const size_t n = std::strlen(prefix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(s[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool MatchPrefixWord(const std::string& s, const char* word) {
  const size_t n = std::strlen(word);
  if (s.compare(0, n, word) != 0) return false;
  if (s.size() == n) return true;
  int bytes = 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data() + n);
  const uint8_t* end = reinterpret_cast<const uint8_t*>(s.data() + s.size());
  const uint32_t next = Utf8Utils::getNextCharUnicode(p, end, bytes);
  if (IsWsCodepoint(next) || next == ':' || next == 0xFF1A || next == '-' ||
      next == 0x3001 || next == 0x00B7 || next == 0x2014 ||
      IsCjkNumeralOrDigit(next)) {
    return true;
  }
  return false;
}

}  // namespace

std::string TocPathForBook(const std::string& book_path) {
  return "/configs/" + FilenameOf(book_path) + ".toc";
}

std::string EpcPathForBook(const std::string& book_path) {
  return "/configs/" + FilenameOf(book_path) + ".epc";
}

std::string StripBookExtension(const std::string& filename) {
  const std::string base = FilenameOf(filename);
  const size_t dot = base.find_last_of('.');
  if (dot == std::string::npos || dot == 0) return base;
  std::string ext = base.substr(dot + 1);
  for (char& ch : ext) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (ext == "txt" || ext == "epub") {
    return base.substr(0, dot);
  }
  return base;
}

bool LoadBookTitleFromToc(const std::string& toc_path,
                          uint32_t expected_source_size,
                          uint32_t expected_source_mtime, std::string* title,
                          bool* is_complete) {
  hal::CheckSdTaskInvariant("LoadBookTitleFromToc");
  title->clear();
  if (is_complete) *is_complete = false;

  FILE* f = std::fopen(core::SdPath(toc_path).c_str(), "rb");
  if (!f) return false;

  TocHeader hdr;
  bool ok = std::fread(&hdr, sizeof(hdr), 1, f) == 1 &&
            std::memcmp(hdr.magic, kTocMagic, 4) == 0 &&
            hdr.version == kTocVersion &&
            hdr.source_size == expected_source_size &&
            hdr.source_mtime == expected_source_mtime &&
            hdr.book_title_len <= kMaxTitleBytes;
  if (ok && hdr.book_title_len > 0) {
    title->resize(hdr.book_title_len);
    ok = std::fread(&(*title)[0], 1, hdr.book_title_len, f) ==
         hdr.book_title_len;
  }
  std::fclose(f);
  if (!ok) {
    title->clear();
    return false;
  }
  if (is_complete) *is_complete = (hdr.flags & kFlagComplete) != 0;
  return true;
}

bool LoadBookToc(const std::string& toc_path, uint32_t expected_source_size,
                 uint32_t expected_source_mtime, TocData* out) {
  hal::CheckSdTaskInvariant("LoadBookToc");
  *out = TocData{};
  FILE* f = std::fopen(core::SdPath(toc_path).c_str(), "rb");
  if (!f) return false;

  TocHeader hdr;
  bool ok = std::fread(&hdr, sizeof(hdr), 1, f) == 1 &&
            std::memcmp(hdr.magic, kTocMagic, 4) == 0 &&
            hdr.version == kTocVersion &&
            hdr.source_size == expected_source_size &&
            hdr.source_mtime == expected_source_mtime &&
            hdr.book_title_len <= kMaxTitleBytes &&
            hdr.entry_count <= kMaxTocEntries;
  if (ok && hdr.book_title_len > 0) {
    out->book_title.resize(hdr.book_title_len);
    ok = std::fread(&out->book_title[0], 1, hdr.book_title_len, f) ==
         hdr.book_title_len;
  }
  if (ok) {
    out->source_size = hdr.source_size;
    out->source_mtime = hdr.source_mtime;
    out->content_size = hdr.content_size;
    out->complete = (hdr.flags & kFlagComplete) != 0;
    out->entries.reserve(hdr.entry_count);
    for (uint32_t i = 0; i < hdr.entry_count; ++i) {
      DiskTocEntry de;
      if (std::fread(&de, sizeof(de), 1, f) != 1 ||
          de.title_len > kMaxTitleBytes) {
        ok = false;
        break;
      }
      TocEntry e;
      e.text_offset = de.text_offset;
      e.level = de.level;
      e.resolved = true;
      if (de.title_len > 0) {
        e.title.resize(de.title_len);
        if (std::fread(&e.title[0], 1, de.title_len, f) != de.title_len) {
          ok = false;
          break;
        }
      }
      out->entries.push_back(std::move(e));
    }
  }
  std::fclose(f);
  if (!ok) *out = TocData{};
  return ok;
}

bool SaveBookToc(const std::string& toc_path, const TocData& data) {
  hal::CheckSdTaskInvariant("SaveBookToc");
  const std::string final_host = core::SdPath(toc_path);
  const std::string tmp_host = final_host + ".tmp";

  FILE* f = std::fopen(tmp_host.c_str(), "wb");
  if (!f) {
    hal::ReportSdError();
    return false;
  }

  std::string book_title = data.book_title;
  if (book_title.size() > kMaxTitleBytes) book_title.resize(kMaxTitleBytes);
  const size_t count = std::min(data.entries.size(), kMaxTocEntries);

  TocHeader hdr = {};
  std::memcpy(hdr.magic, kTocMagic, 4);
  hdr.version = kTocVersion;
  hdr.flags = data.complete ? kFlagComplete : 0;
  hdr.source_size = data.source_size;
  hdr.source_mtime = data.source_mtime;
  hdr.content_size = data.content_size;
  hdr.book_title_len = static_cast<uint16_t>(book_title.size());
  hdr.entry_count = static_cast<uint32_t>(count);

  bool ok = std::fwrite(&hdr, sizeof(hdr), 1, f) == 1;
  if (ok && !book_title.empty()) {
    ok = std::fwrite(book_title.data(), 1, book_title.size(), f) ==
         book_title.size();
  }
  for (size_t i = 0; ok && i < count; ++i) {
    std::string t = data.entries[i].title;
    if (t.size() > kMaxTitleBytes) t.resize(kMaxTitleBytes);
    DiskTocEntry de = {};
    de.text_offset = data.entries[i].text_offset;
    de.level = data.entries[i].level;
    de.title_len = static_cast<uint16_t>(t.size());
    ok = std::fwrite(&de, sizeof(de), 1, f) == 1;
    if (ok && !t.empty()) {
      ok = std::fwrite(t.data(), 1, t.size(), f) == t.size();
    }
  }
  ok = (std::fclose(f) == 0) && ok;
  if (!ok) {
    ::unlink(tmp_host.c_str());
    hal::ReportSdError();
    return false;
  }
  ::unlink(final_host.c_str());
  if (::rename(tmp_host.c_str(), final_host.c_str()) != 0) {
    ::unlink(tmp_host.c_str());
    hal::ReportSdError();
    return false;
  }
  return true;
}

bool IsTxtChapterHeading(const std::string& utf8_line, uint8_t* out_level,
                         std::string* out_title) {
  const uint8_t* begin = reinterpret_cast<const uint8_t*>(utf8_line.data());
  const uint8_t* end = begin + utf8_line.size();

  // Trim leading whitespace.
  const uint8_t* p = begin;
  while (p < end) {
    int b = 0;
    const uint32_t u = Utf8Utils::getNextCharUnicode(p, end, b);
    if (b <= 0 || !IsWsCodepoint(u)) break;
    p += b;
  }
  if (p >= end) return false;

  // Collect codepoints & byte offsets up to a sensible limit.
  std::vector<uint32_t> cps;
  std::vector<size_t> byte_pos;
  cps.reserve(48);
  byte_pos.reserve(49);
  const uint8_t* cur = p;
  int last_non_ws = -1;
  while (cur < end) {
    int b = 0;
    const uint32_t u = Utf8Utils::getNextCharUnicode(cur, end, b);
    if (b <= 0) return false;
    byte_pos.push_back(static_cast<size_t>(cur - p));
    cps.push_back(u);
    if (!IsWsCodepoint(u)) last_non_ws = static_cast<int>(cps.size()) - 1;
    cur += b;
    if (last_non_ws >= 45) return false;
  }
  byte_pos.push_back(static_cast<size_t>(cur - p));
  if (last_non_ws < 0) return false;

  const size_t cp_count = static_cast<size_t>(last_non_ws + 1);
  if (cp_count > 42) return false;
  // Narrative sentences end with a full stop '。'.
  if (cps[cp_count - 1] == 0x3002) return false;

  const std::string trimmed(reinterpret_cast<const char*>(p),
                            byte_pos[cp_count]);

  // Optional "正文 " prefix before "第...".
  size_t idx = 0;
  if (cp_count >= 4 && cps[0] == 0x6B63 && cps[1] == 0x6587) {  // 正文
    idx = 2;
    while (idx < cp_count && IsWsCodepoint(cps[idx])) ++idx;
  }

  // Pattern 1: 第 + [numerals] + [卷/部/章/回/节...]
  if (idx + 2 < cp_count && cps[idx] == 0x7B2C) {  // 第
    size_t k = idx + 1;
    while (k < cp_count && k - (idx + 1) < 10 && IsCjkNumeralOrDigit(cps[k])) {
      ++k;
    }
    if (k > idx + 1 && k < cp_count) {
      const int lvl = HeadingUnitLevel(cps[k]);
      if (lvl >= 0) {
        if (k + 1 == cp_count || !DisallowedAfterUnit(cps[k + 1])) {
          *out_level = static_cast<uint8_t>(lvl);
          *out_title = trimmed;
          return true;
        }
      }
    }
  }

  // Pattern 2: Standalone / prefixed special headings (<= 28 codepoints).
  if (cp_count <= 28) {
    static const char* const kSpecials[] = {
        "序章", "序言", "楔子", "引子",   "前言", "内容简介",
        "尾声", "后记", "终章", "大结局", "番外"};
    for (const char* w : kSpecials) {
      if (MatchPrefixWord(trimmed, w)) {
        *out_level = 1;
        *out_title = trimmed;
        return true;
      }
    }
    if (trimmed == "序") {
      *out_level = 1;
      *out_title = trimmed;
      return true;
    }
  }

  // Pattern 3: English Chapter / Part / Prologue / Epilogue.
  if (StartsWithAsciiIgnoreCase(trimmed, "chapter ") ||
      StartsWithAsciiIgnoreCase(trimmed, "prologue") ||
      StartsWithAsciiIgnoreCase(trimmed, "epilogue")) {
    *out_level = 1;
    *out_title = trimmed;
    return true;
  }
  if (StartsWithAsciiIgnoreCase(trimmed, "part ")) {
    *out_level = 0;
    *out_title = trimmed;
    return true;
  }

  return false;
}

TxtTocScanner::~TxtTocScanner() { Stop(); }

bool TxtTocScanner::Start(const std::string& book_path, TextEncoding encoding,
                          uint32_t data_start, uint32_t file_size) {
  hal::CheckSdTaskInvariant("TxtTocScanner::Start");
  Stop();

  const std::string host = core::SdPath(book_path);
  struct stat st;
  if (::stat(host.c_str(), &st) != 0) return false;

  buf_ = static_cast<uint8_t*>(
      heap_caps_malloc(kScanWindowBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf_) buf_ = static_cast<uint8_t*>(std::malloc(kScanWindowBytes));
  if (!buf_) return false;

  const int fd = ::open(host.c_str(), O_RDONLY);
  if (fd < 0) {
    std::free(buf_);
    buf_ = nullptr;
    return false;
  }

  fd_ = fd;
  book_path_ = book_path;
  encoding_ = encoding;
  data_start_ = std::min(data_start, file_size);
  file_size_ = file_size;
  source_mtime_ = static_cast<uint32_t>(st.st_mtime);
  file_pos_ = data_start_;
  carry_raw_.clear();
  carry_offset_ = data_start_;
  entries_.clear();
  read_error_ = false;
  done_ = false;

  if (::lseek(fd_, static_cast<off_t>(data_start_), SEEK_SET) < 0) {
    read_error_ = true;
    Stop();
    return false;
  }
  if (data_start_ >= file_size_) {
    done_ = true;
    SaveCompletedToc();
    Stop();
  }
  return true;
}

void TxtTocScanner::Stop() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  std::free(buf_);
  buf_ = nullptr;
  carry_raw_.clear();
}

void TxtTocScanner::ScanCompleteLines(const uint8_t* raw, size_t raw_len,
                                      uint32_t base_offset, bool flush_tail) {
  const size_t safe_len =
      flush_tail ? raw_len : SafeChunkEnd(raw, raw_len, encoding_);
  if (safe_len == 0) return;

  // Find the last newline in `raw[0..safe_len)` unless `flush_tail` is true.
  size_t process_raw_len = safe_len;
  if (!flush_tail) {
    size_t last_nl_end = 0;
    if (encoding_ == TextEncoding::UTF16LE) {
      for (size_t i = 0; i + 1 < safe_len; i += 2) {
        if (raw[i] == '\n' && raw[i + 1] == 0) last_nl_end = i + 2;
      }
    } else if (encoding_ == TextEncoding::UTF16BE) {
      for (size_t i = 0; i + 1 < safe_len; i += 2) {
        if (raw[i] == 0 && raw[i + 1] == '\n') last_nl_end = i + 2;
      }
    } else {
      for (size_t i = 0; i < safe_len; ++i) {
        if (raw[i] == '\n') last_nl_end = i + 1;
      }
    }
    if (last_nl_end == 0) {
      // Single very long paragraph without '\n': a line > kMaxCarryBytes cannot
      // be a <= 42-char chapter heading; mark carry with a placeholder
      // non-heading prefix until the next '\n' arrives.
      if (raw_len > kMaxCarryBytes) {
        carry_raw_.assign("...", 3);
        carry_offset_ = base_offset;
      } else {
        carry_raw_.assign(reinterpret_cast<const char*>(raw), raw_len);
        carry_offset_ = base_offset;
      }
      return;
    }
    process_raw_len = last_nl_end;
  }

  if (entries_.size() < kMaxTocEntries) {
    std::string decoded;
    const char* utf8_data = nullptr;
    size_t utf8_size = 0;
    if (encoding_ == TextEncoding::UTF8) {
      utf8_data = reinterpret_cast<const char*>(raw);
      utf8_size = process_raw_len;
    } else {
      decoded = ToUtf8(raw, process_raw_len, encoding_);
      utf8_data = decoded.data();
      utf8_size = decoded.size();
    }

    std::vector<size_t> match_utf8_offsets;
    std::vector<TocEntry> pending;

    size_t line_start = 0;
    while (line_start < utf8_size &&
           entries_.size() + pending.size() < kMaxTocEntries) {
      const void* nl_ptr =
          std::memchr(utf8_data + line_start, '\n', utf8_size - line_start);
      const size_t line_end =
          nl_ptr ? static_cast<size_t>(static_cast<const char*>(nl_ptr) -
                                       utf8_data)
                 : utf8_size;
      const size_t line_len = line_end - line_start;
      if (line_len > 0 && line_len <= 200) {
        uint8_t lvl = 0;
        std::string title;
        if (IsTxtChapterHeading(std::string(utf8_data + line_start, line_len),
                                &lvl, &title)) {
          match_utf8_offsets.push_back(line_start);
          TocEntry e;
          e.level = lvl;
          e.resolved = true;
          e.title = std::move(title);
          pending.push_back(std::move(e));
        }
      }
      line_start = (line_end < utf8_size) ? (line_end + 1) : utf8_size;
    }

    if (!pending.empty()) {
      if (encoding_ == TextEncoding::UTF8) {
        for (size_t i = 0; i < pending.size(); ++i) {
          pending[i].text_offset =
              base_offset + static_cast<uint32_t>(match_utf8_offsets[i]);
          entries_.push_back(std::move(pending[i]));
        }
      } else {
        std::vector<size_t> raw_offsets;
        MapUtf8OffsetsToRaw(raw, process_raw_len, encoding_, match_utf8_offsets,
                            &raw_offsets);
        for (size_t i = 0; i < pending.size() && i < raw_offsets.size(); ++i) {
          pending[i].text_offset =
              base_offset + static_cast<uint32_t>(raw_offsets[i]);
          entries_.push_back(std::move(pending[i]));
        }
      }
    }
  }

  if (flush_tail) {
    carry_raw_.clear();
    carry_offset_ = base_offset + static_cast<uint32_t>(raw_len);
  } else {
    const size_t rem = raw_len - process_raw_len;
    carry_raw_.assign(reinterpret_cast<const char*>(raw + process_raw_len),
                      rem);
    carry_offset_ = base_offset + static_cast<uint32_t>(process_raw_len);
  }
}

void TxtTocScanner::NormalizeLevels() {
  // If the book has no level-0 volumes/parts, promote level-1 chapters to 0 so
  // flat chapter lists sit flush at the left margin without wasted indentation.
  bool has_level0 = false;
  for (const auto& e : entries_) {
    if (e.level == 0) {
      has_level0 = true;
      break;
    }
  }
  if (!has_level0) {
    for (auto& e : entries_) e.level = 0;
  }
}

void TxtTocScanner::SaveCompletedToc() {
  NormalizeLevels();
  TocData data;
  data.source_size = file_size_;
  data.source_mtime = source_mtime_;
  data.content_size = file_size_;
  data.complete = true;
  data.entries = entries_;
  SaveBookToc(TocPathForBook(book_path_), data);
}

bool TxtTocScanner::Step(uint32_t budget_ms) {
  if (!active()) return true;
  hal::CheckSdTaskInvariant("TxtTocScanner::Step");
  const uint32_t t0 = millis();

  while (file_pos_ < file_size_) {
    const size_t want =
        std::min<size_t>(kScanWindowBytes, file_size_ - file_pos_);
    const ssize_t n = ::read(fd_, buf_, want);
    if (n <= 0) {
      read_error_ = true;
      hal::ReportSdError();
      Stop();
      return true;
    }
    const size_t got = static_cast<size_t>(n);
    file_pos_ += static_cast<uint32_t>(got);

    if (carry_raw_.empty()) {
      ScanCompleteLines(buf_, got, file_pos_ - static_cast<uint32_t>(got),
                        file_pos_ >= file_size_);
    } else {
      std::string combined;
      combined.reserve(carry_raw_.size() + got);
      combined.append(carry_raw_);
      combined.append(reinterpret_cast<const char*>(buf_), got);
      const uint32_t base = carry_offset_;
      carry_raw_.clear();
      ScanCompleteLines(reinterpret_cast<const uint8_t*>(combined.data()),
                        combined.size(), base, file_pos_ >= file_size_);
    }

    if (millis() - t0 >= budget_ms) break;
  }

  if (file_pos_ >= file_size_) {
    done_ = true;
    SaveCompletedToc();
    Stop();
    return true;
  }
  return false;
}

void TxtTocScanner::Finish() {
  while (active()) {
    Step(200);
  }
}

}  // namespace book
}  // namespace reader
