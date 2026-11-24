#include "book/epub_book.h"

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
#include <unordered_map>
#include <utility>
#include <vector>

#include "book/page_layout.h"
#include "core/sd_path.h"
#include "hal/storage.h"

namespace reader {
namespace book {
namespace {

constexpr size_t kWriteBufCap = 64 * 1024;
constexpr size_t kInitialReadyBytes = 16 * 1024;
constexpr size_t kMaxXmlMetaBytes = 512 * 1024;
constexpr size_t kFastOpfPrefixBytes = 32 * 1024;
constexpr size_t kMaxChapterUncompBytes = 4 * 1024 * 1024;
constexpr size_t kMaxTocItems = 4096;
constexpr int kMinIllustrationDimPx = 48;

void* AllocPsramOrHeap(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = std::malloc(bytes);
  return p;
}

std::string AsciiLower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool EndsWithIgnoreCase(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    const char a = s[s.size() - n + i];
    const char b = suffix[i];
    if (std::tolower(static_cast<unsigned char>(a)) !=
        std::tolower(static_cast<unsigned char>(b))) {
      return false;
    }
  }
  return true;
}

std::string DirOfPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return (slash == std::string::npos) ? "" : path.substr(0, slash);
}

std::string LocalXmlName(const std::string& raw_name) {
  const size_t colon = raw_name.find(':');
  return AsciiLower(
      colon == std::string::npos ? raw_name : raw_name.substr(colon + 1));
}

void AppendCodepointUtf8(uint32_t cp, std::string* out) {
  if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return;
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void DecodeHtmlEntity(const std::string& ent, std::string* out) {
  if (ent.empty()) return;
  if (ent[0] == '#') {
    uint32_t cp = 0;
    if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
      for (size_t i = 2; i < ent.size(); ++i) {
        const char c = ent[i];
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) return;
        cp = (cp << 4) | static_cast<uint32_t>(v);
        if (cp > 0x10FFFF) return;
      }
    } else {
      for (size_t i = 1; i < ent.size(); ++i) {
        if (ent[i] < '0' || ent[i] > '9') return;
        cp = cp * 10 + static_cast<uint32_t>(ent[i] - '0');
        if (cp > 0x10FFFF) return;
      }
    }
    if (cp == 0xA0) cp = ' ';
    AppendCodepointUtf8(cp, out);
    return;
  }

  const std::string low = AsciiLower(ent);
  if (low == "nbsp" || low == "ensp" || low == "emsp" || low == "thinsp") {
    out->push_back(' ');
  } else if (low == "amp") {
    out->push_back('&');
  } else if (low == "lt") {
    out->push_back('<');
  } else if (low == "gt") {
    out->push_back('>');
  } else if (low == "quot") {
    out->push_back('"');
  } else if (low == "apos") {
    out->push_back('\'');
  } else if (low == "ldquo") {
    out->append("\xE2\x80\x9C");  // “
  } else if (low == "rdquo") {
    out->append("\xE2\x80\x9D");  // ”
  } else if (low == "lsquo") {
    out->append("\xE2\x80\x98");  // ‘
  } else if (low == "rsquo") {
    out->append("\xE2\x80\x99");  // ’
  } else if (low == "mdash") {
    out->append("\xE2\x80\x94");  // —
  } else if (low == "ndash") {
    out->append("\xE2\x80\x93");  // –
  } else if (low == "hellip") {
    out->append("\xE2\x80\xA6");  // …
  } else if (low == "middot" || low == "bull") {
    out->append("\xC2\xB7");      // ·
  } else if (low == "copy") {
    out->append("\xC2\xA9");
  } else {
    // Unknown entity: preserve literally so raw '&foo;' text is never lost.
    out->push_back('&');
    out->append(ent);
    out->push_back(';');
  }
}

std::string DecodeEntitiesInString(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '&') {
      const size_t semi = s.find(';', i + 1);
      if (semi != std::string::npos && semi - i <= 14) {
        DecodeHtmlEntity(s.substr(i + 1, semi - i - 1), &out);
        i = semi;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string NormalizeWhitespace(const std::string& s) {
  std::string decoded = DecodeEntitiesInString(s);
  std::string out;
  out.reserve(decoded.size());
  bool prev_ws = true;
  for (size_t i = 0; i < decoded.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(decoded[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!prev_ws) {
        out.push_back(' ');
        prev_ws = true;
      }
    } else {
      out.push_back(static_cast<char>(c));
      prev_ws = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

struct ParsedTag {
  std::string local_name;
  bool is_closing = false;
  bool self_closing = false;
  std::vector<std::pair<std::string, std::string>> attrs;

  std::string GetAttr(const char* want_local) const {
    for (const auto& kv : attrs) {
      if (kv.first == want_local) return kv.second;
    }
    return "";
  }
};

// Parses the interior of `< ... >` (without the outer `<` and `>`).
bool ParseTagInterior(const std::string& raw, ParsedTag* out) {
  *out = ParsedTag{};
  size_t i = 0;
  const size_t n = raw.size();
  while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
  if (i >= n || raw[i] == '!' || raw[i] == '?') return false;

  if (raw[i] == '/') {
    out->is_closing = true;
    ++i;
    while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
  }

  const size_t name_start = i;
  while (i < n && !std::isspace(static_cast<unsigned char>(raw[i])) &&
         raw[i] != '/' && raw[i] != '=') {
    ++i;
  }
  if (i == name_start) return false;
  out->local_name = LocalXmlName(raw.substr(name_start, i - name_start));

  while (i < n) {
    while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    if (i >= n) break;
    if (raw[i] == '/') {
      out->self_closing = true;
      ++i;
      continue;
    }
    const size_t attr_start = i;
    while (i < n && !std::isspace(static_cast<unsigned char>(raw[i])) &&
           raw[i] != '=' && raw[i] != '/') {
      ++i;
    }
    if (i == attr_start) {
      ++i;
      continue;
    }
    std::string attr_name =
        LocalXmlName(raw.substr(attr_start, i - attr_start));
    while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    std::string attr_val;
    if (i < n && raw[i] == '=') {
      ++i;
      while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
      if (i < n && (raw[i] == '"' || raw[i] == '\'')) {
        const char quote = raw[i++];
        const size_t val_start = i;
        while (i < n && raw[i] != quote) ++i;
        attr_val = DecodeEntitiesInString(raw.substr(val_start, i - val_start));
        if (i < n && raw[i] == quote) ++i;
      } else {
        const size_t val_start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(raw[i])) &&
               raw[i] != '/') {
          ++i;
        }
        attr_val = DecodeEntitiesInString(raw.substr(val_start, i - val_start));
      }
    }
    out->attrs.emplace_back(std::move(attr_name), std::move(attr_val));
  }
  return true;
}

// Walks tags and text segments in an in-memory XML/HTML document (for small
// package files: `container.xml`, `.opf`, `toc.ncx`, and EPUB 3 `nav`).
template <typename TagFn, typename TextFn>
void WalkXmlDocument(const std::string& xml, TagFn on_tag, TextFn on_text) {
  size_t pos = 0;
  const size_t n = xml.size();
  while (pos < n) {
    const size_t lt = xml.find('<', pos);
    if (lt == std::string::npos) {
      on_text(xml.substr(pos));
      break;
    }
    if (lt > pos) on_text(xml.substr(pos, lt - pos));

    if (xml.compare(lt, 4, "<!--") == 0) {
      const size_t end = xml.find("-->", lt + 4);
      pos = (end == std::string::npos) ? n : end + 3;
      continue;
    }
    if (xml.compare(lt, 9, "<![CDATA[") == 0) {
      const size_t end = xml.find("]]>", lt + 9);
      if (end == std::string::npos) {
        on_text(xml.substr(lt + 9));
        break;
      }
      on_text(xml.substr(lt + 9, end - (lt + 9)));
      pos = end + 3;
      continue;
    }
    const size_t gt = xml.find('>', lt + 1);
    if (gt == std::string::npos) break;
    ParsedTag tag;
    if (ParseTagInterior(xml.substr(lt + 1, gt - (lt + 1)), &tag)) {
      on_tag(tag);
    }
    pos = gt + 1;
  }
}

bool FindOpfPathInContainer(ZipReader& zip, std::string* opf_path) {
  const int c_idx = zip.FindEntry("META-INF/container.xml");
  if (c_idx < 0) return false;
  std::string xml;
  if (!zip.ExtractEntryToString(static_cast<size_t>(c_idx), 64 * 1024, &xml)) {
    return false;
  }
  opf_path->clear();
  WalkXmlDocument(
      xml,
      [&](const ParsedTag& t) {
        if (!t.is_closing && t.local_name == "rootfile" && opf_path->empty()) {
          const std::string fp = t.GetAttr("full-path");
          if (!fp.empty()) *opf_path = NormalizeZipPath("", fp);
        }
      },
      [](const std::string&) {});
  return !opf_path->empty();
}

bool HasToken(const std::string& prop_list, const char* token) {
  const std::string low = AsciiLower(prop_list);
  size_t i = 0;
  while (i < low.size()) {
    while (i < low.size() && std::isspace(static_cast<unsigned char>(low[i]))) {
      ++i;
    }
    const size_t start = i;
    while (i < low.size() &&
           !std::isspace(static_cast<unsigned char>(low[i]))) {
      ++i;
    }
    if (i > start && low.compare(start, i - start, token) == 0) return true;
  }
  return false;
}

bool IsSupportedIllustrationPath(const std::string& zip_path) {
  return EndsWithIgnoreCase(zip_path, ".jpg") ||
         EndsWithIgnoreCase(zip_path, ".jpeg") ||
         EndsWithIgnoreCase(zip_path, ".png");
}

int ParsePositiveDimensionPx(const std::string& attr) {
  if (attr.empty()) return 0;
  // Ignore percentage widths like "100%".
  if (attr.find('%') != std::string::npos) return 0;
  int val = 0;
  bool any_digit = false;
  for (char c : attr) {
    if (c >= '0' && c <= '9') {
      any_digit = true;
      val = val * 10 + (c - '0');
      if (val > 10000) return 10000;
    } else {
      break;
    }
  }
  return any_digit ? val : 0;
}

bool IsBlockBreakTag(const std::string& tag) {
  return tag == "p" || tag == "div" || tag == "h1" || tag == "h2" ||
         tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6" ||
         tag == "li" || tag == "blockquote" || tag == "section" ||
         tag == "article" || tag == "header" || tag == "footer" ||
         tag == "tr" || tag == "dt" || tag == "dd" || tag == "pre" ||
         tag == "hr" || tag == "br";
}

bool IsIgnoredSubtreeTag(const std::string& tag) {
  return tag == "head" || tag == "style" || tag == "script" ||
         tag == "noscript" || tag == "svg";
}

void SplitHrefFragment(const std::string& raw_href, std::string* path_part,
                       std::string* fragment) {
  const size_t hash = raw_href.find('#');
  if (hash == std::string::npos) {
    *path_part = raw_href;
    fragment->clear();
  } else {
    *path_part = raw_href.substr(0, hash);
    *fragment = NormalizeZipPath("", raw_href.substr(hash + 1));
    // Fragment IDs do not undergo path slash collapsing; keep raw decoded id.
    std::string raw_frag = raw_href.substr(hash + 1);
    *fragment = raw_frag;
  }
}

}  // namespace

bool IsEpubPath(const std::string& path) {
  return EndsWithIgnoreCase(path, ".epub");
}

bool ResolveEpubDisplayTitle(const std::string& epub_sd_path,
                             std::string* out_title) {
  out_title->clear();
  struct stat st;
  if (::stat(core::SdPath(epub_sd_path).c_str(), &st) != 0) return false;
  const uint32_t src_size = static_cast<uint32_t>(st.st_size);
  const uint32_t src_mtime = static_cast<uint32_t>(st.st_mtime);

  const std::string toc_path = TocPathForBook(epub_sd_path);
  if (LoadBookTitleFromToc(toc_path, src_size, src_mtime, out_title)) {
    return true;
  }

  ZipReader zip;
  if (!zip.Open(epub_sd_path)) return false;
  std::string opf_path;
  if (!FindOpfPathInContainer(zip, &opf_path)) return false;
  const int opf_idx = zip.FindEntry(opf_path);
  if (opf_idx < 0) return false;

  std::string opf_xml;
  if (!zip.ExtractEntryToString(static_cast<size_t>(opf_idx),
                                kFastOpfPrefixBytes, &opf_xml,
                                /*truncate_at_max=*/true)) {
    return false;
  }

  bool in_metadata = false;
  bool in_title = false;
  std::string title_buf;
  WalkXmlDocument(
      opf_xml,
      [&](const ParsedTag& t) {
        if (t.local_name == "metadata") {
          in_metadata = !t.is_closing;
        } else if (in_metadata && t.local_name == "title") {
          if (!t.is_closing && out_title->empty() && !t.self_closing) {
            in_title = true;
            title_buf.clear();
          } else if (t.is_closing && in_title) {
            in_title = false;
            *out_title = NormalizeWhitespace(title_buf);
          }
        }
      },
      [&](const std::string& text) {
        if (in_title) title_buf.append(text);
      });

  // Persist a tiny title-only `.toc` (`complete = false`) so future bookshelf
  // refreshes never need to reopen the `.epub` archive.
  TocData meta;
  meta.source_size = src_size;
  meta.source_mtime = src_mtime;
  meta.complete = false;
  meta.book_title = *out_title;
  SaveBookToc(toc_path, meta);
  return true;
}

bool TryLoadCompleteEpubCache(const std::string& epub_sd_path, TocData* out_toc,
                              std::string* out_epc_path) {
  struct stat epub_st;
  if (::stat(core::SdPath(epub_sd_path).c_str(), &epub_st) != 0) return false;

  const std::string toc_path = TocPathForBook(epub_sd_path);
  const std::string epc_path = EpcPathForBook(epub_sd_path);
  if (!LoadBookToc(toc_path, static_cast<uint32_t>(epub_st.st_size),
                   static_cast<uint32_t>(epub_st.st_mtime), out_toc)) {
    return false;
  }
  if (!out_toc->complete || out_toc->content_size == 0) return false;

  struct stat epc_st;
  if (::stat(core::SdPath(epc_path).c_str(), &epc_st) != 0 ||
      static_cast<uint32_t>(epc_st.st_size) != out_toc->content_size) {
    return false;
  }
  *out_epc_path = epc_path;
  return true;
}

bool ExtractEpubImageEntry(const std::string& epub_sd_path,
                           uint32_t zip_entry_index, size_t max_bytes,
                           uint8_t** out_buf, size_t* out_len) {
  ZipReader zip;
  if (!zip.Open(epub_sd_path)) return false;
  return zip.ExtractEntryToBuffer(zip_entry_index, max_bytes, out_buf, out_len);
}

// ---- EpubIncrementalBuilder ------------------------------------------------

EpubIncrementalBuilder::~EpubIncrementalBuilder() { Cancel(); }

void EpubIncrementalBuilder::ReleaseRuntimeMemory() {
  zip_.Close();
  std::free(write_buf_);
  write_buf_ = nullptr;
  write_len_ = 0;
}

void EpubIncrementalBuilder::Cancel() {
  ReleaseRuntimeMemory();
  if (active_ && !done_ && !epc_tmp_path_.empty()) {
    ::unlink(core::SdPath(epc_tmp_path_).c_str());
  }
  active_ = false;
  done_ = true;
  spine_.clear();
  raw_toc_.clear();
}

bool EpubIncrementalBuilder::ParsePackageAndToc() {
  std::string opf_path;
  if (!FindOpfPathInContainer(zip_, &opf_path)) return false;
  const int opf_idx = zip_.FindEntry(opf_path);
  if (opf_idx < 0) return false;

  std::string opf_xml;
  if (!zip_.ExtractEntryToString(static_cast<size_t>(opf_idx), kMaxXmlMetaBytes,
                                 &opf_xml)) {
    return false;
  }
  const std::string opf_dir = DirOfPath(opf_path);

  struct ManifestItem {
    std::string zip_path;
    std::string media_type;
    std::string properties;
  };
  std::unordered_map<std::string, ManifestItem> manifest;
  std::vector<std::string> spine_idrefs;
  std::string spine_toc_id;
  std::string nav_zip_path;
  std::string ncx_zip_path;

  bool in_metadata = false;
  bool in_title = false;
  std::string title_buf;

  WalkXmlDocument(
      opf_xml,
      [&](const ParsedTag& t) {
        if (t.local_name == "metadata") {
          in_metadata = !t.is_closing;
        } else if (in_metadata && t.local_name == "title") {
          if (!t.is_closing && book_title_.empty() && !t.self_closing) {
            in_title = true;
            title_buf.clear();
          } else if (t.is_closing && in_title) {
            in_title = false;
            book_title_ = NormalizeWhitespace(title_buf);
          }
        } else if (!t.is_closing && t.local_name == "item") {
          const std::string id = t.GetAttr("id");
          const std::string href = t.GetAttr("href");
          if (!id.empty() && !href.empty()) {
            ManifestItem item;
            item.zip_path = NormalizeZipPath(opf_dir, href);
            item.media_type = AsciiLower(t.GetAttr("media-type"));
            item.properties = t.GetAttr("properties");
            if (HasToken(item.properties, "nav") && nav_zip_path.empty()) {
              nav_zip_path = item.zip_path;
            }
            if ((item.media_type == "application/x-dtbncx+xml" ||
                 EndsWithIgnoreCase(item.zip_path, ".ncx")) &&
                ncx_zip_path.empty()) {
              ncx_zip_path = item.zip_path;
            }
            manifest.emplace(id, std::move(item));
          }
        } else if (!t.is_closing && t.local_name == "spine") {
          spine_toc_id = t.GetAttr("toc");
        } else if (!t.is_closing && t.local_name == "itemref") {
          const std::string idref = t.GetAttr("idref");
          if (!idref.empty()) spine_idrefs.push_back(idref);
        }
      },
      [&](const std::string& text) {
        if (in_title) title_buf.append(text);
      });

  if (!spine_toc_id.empty()) {
    auto it = manifest.find(spine_toc_id);
    if (it != manifest.end()) ncx_zip_path = it->second.zip_path;
  }

  std::unordered_map<std::string, int> path_to_spine;
  std::unordered_map<std::string, int> lower_path_to_spine;
  spine_.reserve(spine_idrefs.size());
  for (const std::string& idref : spine_idrefs) {
    auto it = manifest.find(idref);
    if (it == manifest.end()) continue;
    const ManifestItem& m = it->second;
    const int zidx = zip_.FindEntry(m.zip_path);
    if (zidx < 0) continue;
    // Accept XHTML/HTML/XML content documents (or direct cover images in spine).
    const int sidx = static_cast<int>(spine_.size());
    SpineItem s;
    s.zip_index = static_cast<size_t>(zidx);
    s.zip_path = zip_.entries()[static_cast<size_t>(zidx)].name;
    s.base_dir = DirOfPath(s.zip_path);
    path_to_spine.emplace(s.zip_path, sidx);
    lower_path_to_spine.emplace(AsciiLower(s.zip_path), sidx);
    spine_.push_back(std::move(s));
  }
  if (spine_.empty()) return false;

  auto resolve_href_spine = [&](const std::string& toc_dir,
                                const std::string& raw_href,
                                std::string* out_frag) -> int {
    std::string path_part;
    SplitHrefFragment(raw_href, &path_part, out_frag);
    const std::string norm = NormalizeZipPath(toc_dir, path_part);
    auto it = path_to_spine.find(norm);
    if (it != path_to_spine.end()) return it->second;
    auto lit = lower_path_to_spine.find(AsciiLower(norm));
    if (lit != lower_path_to_spine.end()) return lit->second;
    return -1;
  };

  // 1. Try EPUB 3 `<nav epub:type="toc">`.
  if (!nav_zip_path.empty()) {
    const int nav_idx = zip_.FindEntry(nav_zip_path);
    std::string nav_xml;
    if (nav_idx >= 0 &&
        zip_.ExtractEntryToString(static_cast<size_t>(nav_idx),
                                  kMaxXmlMetaBytes, &nav_xml)) {
      const std::string nav_dir = DirOfPath(nav_zip_path);
      bool in_toc_nav = false;
      int nav_depth = 0;
      int ol_depth = 0;
      bool in_anchor = false;
      std::string anchor_href;
      std::string anchor_text;

      auto parse_nav = [&](bool require_toc_attr) {
        in_toc_nav = false;
        nav_depth = 0;
        ol_depth = 0;
        in_anchor = false;
        WalkXmlDocument(
            nav_xml,
            [&](const ParsedTag& t) {
              if (t.local_name == "nav") {
                if (!t.is_closing && !t.self_closing) {
                  if (!in_toc_nav) {
                    const std::string et = AsciiLower(t.GetAttr("type"));
                    const std::string role = AsciiLower(t.GetAttr("role"));
                    if (!require_toc_attr || et == "toc" ||
                        role == "doc-toc") {
                      in_toc_nav = true;
                      nav_depth = 1;
                      ol_depth = 0;
                    }
                  } else {
                    ++nav_depth;
                  }
                } else if (t.is_closing && in_toc_nav) {
                  if (--nav_depth <= 0) in_toc_nav = false;
                }
              } else if (in_toc_nav && t.local_name == "ol") {
                if (!t.is_closing && !t.self_closing) {
                  ++ol_depth;
                } else if (t.is_closing && ol_depth > 0) {
                  --ol_depth;
                }
              } else if (in_toc_nav && t.local_name == "a") {
                if (!t.is_closing && !t.self_closing) {
                  in_anchor = true;
                  anchor_href = t.GetAttr("href");
                  anchor_text.clear();
                } else if (t.is_closing && in_anchor) {
                  in_anchor = false;
                  const std::string label = NormalizeWhitespace(anchor_text);
                  if (!label.empty() && !anchor_href.empty() &&
                      raw_toc_.size() < kMaxTocItems) {
                    std::string frag;
                    const int sidx =
                        resolve_href_spine(nav_dir, anchor_href, &frag);
                    if (sidx >= 0) {
                      RawTocTarget tgt;
                      tgt.spine_index = sidx;
                      tgt.fragment = std::move(frag);
                      tgt.level = static_cast<uint8_t>(
                          std::max(0, std::min(ol_depth - 1, 2)));
                      tgt.title = label;
                      raw_toc_.push_back(std::move(tgt));
                    }
                  }
                }
              }
            },
            [&](const std::string& text) {
              if (in_anchor) anchor_text.append(text);
            });
      };

      parse_nav(/*require_toc_attr=*/true);
      if (raw_toc_.empty()) parse_nav(/*require_toc_attr=*/false);
    }
  }

  // 2. Fall back to EPUB 2 `toc.ncx`.
  if (raw_toc_.empty() && !ncx_zip_path.empty()) {
    const int ncx_idx = zip_.FindEntry(ncx_zip_path);
    std::string ncx_xml;
    if (ncx_idx >= 0 &&
        zip_.ExtractEntryToString(static_cast<size_t>(ncx_idx),
                                  kMaxXmlMetaBytes, &ncx_xml)) {
      const std::string ncx_dir = DirOfPath(ncx_zip_path);
      struct NavFrame {
        std::string label;
        std::string src;
        bool emitted = false;
      };
      std::vector<NavFrame> stack;
      bool in_nav_label = false;
      bool in_text = false;
      std::string text_buf;

      auto emit_frame = [&](NavFrame* f, size_t depth) {
        if (f->emitted || f->label.empty() || f->src.empty() ||
            raw_toc_.size() >= kMaxTocItems) {
          return;
        }
        std::string frag;
        const int sidx = resolve_href_spine(ncx_dir, f->src, &frag);
        if (sidx >= 0) {
          RawTocTarget tgt;
          tgt.spine_index = sidx;
          tgt.fragment = std::move(frag);
          tgt.level = static_cast<uint8_t>(std::min<size_t>(depth, 2));
          tgt.title = f->label;
          raw_toc_.push_back(std::move(tgt));
          f->emitted = true;
        }
      };

      WalkXmlDocument(
          ncx_xml,
          [&](const ParsedTag& t) {
            if (t.local_name == "navpoint") {
              if (!t.is_closing && !t.self_closing) {
                if (!stack.empty()) {
                  emit_frame(&stack.back(), stack.size() - 1);
                }
                stack.push_back(NavFrame{});
              } else if (t.is_closing && !stack.empty()) {
                emit_frame(&stack.back(), stack.size() - 1);
                stack.pop_back();
              }
            } else if (!stack.empty() && t.local_name == "navlabel") {
              in_nav_label = !t.is_closing;
            } else if (in_nav_label && t.local_name == "text") {
              if (!t.is_closing && !t.self_closing) {
                in_text = true;
                text_buf.clear();
              } else if (t.is_closing && in_text) {
                in_text = false;
                const std::string lbl = NormalizeWhitespace(text_buf);
                if (!lbl.empty() && stack.back().label.empty()) {
                  stack.back().label = lbl;
                }
              }
            } else if (!stack.empty() && !t.is_closing &&
                       t.local_name == "content") {
              const std::string src = t.GetAttr("src");
              if (!src.empty() && stack.back().src.empty()) {
                stack.back().src = src;
                emit_frame(&stack.back(), stack.size() - 1);
              }
            }
          },
          [&](const std::string& text) {
            if (in_text) text_buf.append(text);
          });
    }
  }

  auto_toc_from_headings_ = raw_toc_.empty();
  return true;
}

bool EpubIncrementalBuilder::AppendBytes(const char* data, size_t len,
                                         const FlushHook& before_flush,
                                         const AfterFlushHook& after_flush) {
  size_t pos = 0;
  while (pos < len) {
    const size_t room = kWriteBufCap - write_len_;
    const size_t take = std::min(room, len - pos);
    std::memcpy(write_buf_ + write_len_, data + pos, take);
    write_len_ += take;
    pos += take;
    if (write_len_ == kWriteBufCap) {
      if (!FlushWriteBuffer(/*finalize=*/false, before_flush, after_flush)) {
        return false;
      }
    }
  }
  return true;
}

bool EpubIncrementalBuilder::FlushWriteBuffer(
    bool finalize, const FlushHook& before_flush,
    const AfterFlushHook& after_flush) {
  hal::CheckSdTaskInvariant("EpubIncrementalBuilder::FlushWriteBuffer");
  if (before_flush) before_flush();

  bool ok = true;
  if (write_len_ > 0 || (flushed_bytes_ == 0 && finalize)) {
    const int fd = ::open(core::SdPath(epc_tmp_path_).c_str(),
                          O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
      ok = false;
    } else {
      size_t done = 0;
      while (done < write_len_) {
        const ssize_t n = ::write(fd, write_buf_ + done, write_len_ - done);
        if (n <= 0) {
          ok = false;
          break;
        }
        done += static_cast<size_t>(n);
      }
      if (::close(fd) != 0) ok = false;
      if (ok) flushed_bytes_ += static_cast<uint32_t>(write_len_);
      write_len_ = 0;
    }
  }

  if (ok && finalize) {
    // Ensure `.epc.tmp` is never 0 bytes so `BookReader` can open empty EPUBs.
    if (flushed_bytes_ == 0) {
      const int fd = ::open(core::SdPath(epc_tmp_path_).c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) {
        (void)::write(fd, "\n", 1);
        ::close(fd);
        flushed_bytes_ = 1;
      }
    }
    const std::string tmp_host = core::SdPath(epc_tmp_path_);
    const std::string fin_host = core::SdPath(epc_final_path_);
    ::unlink(fin_host.c_str());
    if (::rename(tmp_host.c_str(), fin_host.c_str()) != 0) {
      ok = false;
    } else {
      TocData toc;
      toc.source_size = zip_.archive_size();
      toc.source_mtime = zip_.archive_mtime();
      toc.content_size = flushed_bytes_;
      toc.complete = true;
      toc.book_title = book_title_;
      toc.entries = CurrentTocEntries();
      SaveBookToc(toc_path_, toc);
      done_ = true;
      ReleaseRuntimeMemory();
    }
  }

  if (!ok) {
    error_ = true;
    hal::ReportSdError();
  }
  if (after_flush) {
    after_flush(done_ ? epc_final_path_ : epc_tmp_path_, flushed_bytes_);
  }
  return ok;
}

bool EpubIncrementalBuilder::ConvertOneSpineChapter(size_t spine_idx) {
  const SpineItem& item = spine_[spine_idx];
  const uint32_t chapter_start_off =
      flushed_bytes_ + static_cast<uint32_t>(write_len_);

  // Resolve any TOC entries pointing to the start of this spine item.
  for (auto& tgt : raw_toc_) {
    if (tgt.spine_index == static_cast<int>(spine_idx) && !tgt.resolved &&
        tgt.fragment.empty()) {
      tgt.text_offset = chapter_start_off;
      tgt.resolved = true;
    }
  }

  // Direct image item in `<spine>`: emit standalone illustration marker.
  if (IsSupportedIllustrationPath(item.zip_path)) {
    std::string marker =
        std::string(kImagePagePrefix) + std::to_string(item.zip_index) + "\n";
    return AppendBytes(marker.data(), marker.size(), *active_before_flush_,
                       *active_after_flush_);
  }

  // Streaming XHTML/HTML tokenizer state (zero full-chapter buffer!).
  enum class State {
    kText,
    kTag,
    kCommentDash1,
    kCommentDash2,
    kComment,
    kCommentEnd1,
    kCommentEnd2,
    kEntity,
  };
  State st = State::kText;
  std::string tag_buf;
  std::string ent_buf;
  tag_buf.reserve(256);
  ent_buf.reserve(16);

  int ignore_depth = 0;
  bool at_line_start = true;
  bool pending_ascii_space = false;
  bool in_heading_capture = false;
  uint8_t heading_level = 0;
  uint32_t heading_offset = chapter_start_off;
  std::string heading_text;

  std::string out_chunk;
  out_chunk.reserve(4096);

  auto cur_offset = [&]() -> uint32_t {
    return flushed_bytes_ + static_cast<uint32_t>(write_len_ + out_chunk.size());
  };

  auto flush_out_chunk = [&]() -> bool {
    if (out_chunk.empty()) return true;
    const bool ok = AppendBytes(out_chunk.data(), out_chunk.size(),
                                *active_before_flush_, *active_after_flush_);
    out_chunk.clear();
    return ok;
  };

  auto emit_newline = [&]() {
    pending_ascii_space = false;
    if (!at_line_start) {
      out_chunk.push_back('\n');
      at_line_start = true;
    }
  };

  auto emit_char = [&](char ch) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!at_line_start) pending_ascii_space = true;
    } else {
      if (pending_ascii_space) {
        if (!at_line_start) out_chunk.push_back(' ');
        pending_ascii_space = false;
      }
      out_chunk.push_back(ch);
      at_line_start = false;
    }
    if (in_heading_capture && heading_text.size() < 240) {
      heading_text.push_back(ch);
    }
  };

  auto emit_text_bytes = [&](const std::string& utf8) {
    for (char ch : utf8) emit_char(ch);
  };

  auto check_anchor_id = [&](const std::string& id_val) {
    if (id_val.empty()) return;
    const uint32_t off = cur_offset();
    for (auto& tgt : raw_toc_) {
      if (tgt.spine_index == static_cast<int>(spine_idx) && !tgt.resolved &&
          tgt.fragment == id_val) {
        tgt.text_offset = off;
        tgt.resolved = true;
      }
    }
  };

  auto handle_tag = [&](const std::string& raw_tag) {
    ParsedTag t;
    if (!ParseTagInterior(raw_tag, &t)) return;

    if (!t.is_closing) {
      check_anchor_id(t.GetAttr("id"));
      check_anchor_id(t.GetAttr("name"));
    }

    // Even inside `<svg>...</svg>` cover wrappers, capture `<image href=.../>`.
    if (!t.is_closing && (t.local_name == "img" || t.local_name == "image")) {
      std::string src = t.GetAttr("src");
      if (src.empty()) src = t.GetAttr("href");
      if (!src.empty()) {
        const int w = ParsePositiveDimensionPx(t.GetAttr("width"));
        const int h = ParsePositiveDimensionPx(t.GetAttr("height"));
        const bool tiny_icon =
            (w > 0 && w < kMinIllustrationDimPx) ||
            (h > 0 && h < kMinIllustrationDimPx);
        if (!tiny_icon) {
          const std::string img_path = NormalizeZipPath(item.base_dir, src);
          if (IsSupportedIllustrationPath(img_path)) {
            const int img_zidx = zip_.FindEntry(img_path);
            if (img_zidx >= 0) {
              emit_newline();
              out_chunk.append(kImagePagePrefix);
              out_chunk.append(std::to_string(img_zidx));
              out_chunk.push_back('\n');
              at_line_start = true;
              pending_ascii_space = false;
            }
          }
        }
      }
    }

    if (IsIgnoredSubtreeTag(t.local_name)) {
      if (!t.is_closing && !t.self_closing) {
        ++ignore_depth;
      } else if (t.is_closing && ignore_depth > 0) {
        --ignore_depth;
      }
      return;
    }
    if (ignore_depth > 0) return;

    if (IsBlockBreakTag(t.local_name)) {
      emit_newline();
    }

    if (auto_toc_from_headings_ &&
        (t.local_name == "h1" || t.local_name == "h2" ||
         t.local_name == "h3")) {
      if (!t.is_closing && !t.self_closing) {
        in_heading_capture = true;
        heading_level = static_cast<uint8_t>(t.local_name[1] - '1');
        heading_offset = cur_offset();
        heading_text.clear();
      } else if (t.is_closing && in_heading_capture) {
        in_heading_capture = false;
        const std::string clean = NormalizeWhitespace(heading_text);
        if (!clean.empty() && raw_toc_.size() < kMaxTocItems) {
          RawTocTarget tgt;
          tgt.spine_index = static_cast<int>(spine_idx);
          tgt.level = heading_level;
          tgt.title = clean;
          tgt.resolved = true;
          tgt.text_offset = heading_offset;
          raw_toc_.push_back(std::move(tgt));
        }
      }
    }
  };

  const bool extracted = zip_.ExtractEntry(
      item.zip_index, kMaxChapterUncompBytes,
      [&](const uint8_t* data, size_t len) -> bool {
        for (size_t i = 0; i < len; ++i) {
          const char ch = static_cast<char>(data[i]);
          switch (st) {
            case State::kText:
              if (ch == '<') {
                st = State::kTag;
                tag_buf.clear();
              } else if (ch == '&') {
                st = State::kEntity;
                ent_buf.clear();
              } else if (ignore_depth == 0) {
                emit_char(ch);
              }
              break;

            case State::kTag:
              if (tag_buf.empty() && ch == '!') {
                tag_buf.push_back(ch);
              } else if (tag_buf == "!" && ch == '-') {
                st = State::kCommentDash1;
                tag_buf.clear();
              } else if (ch == '>') {
                handle_tag(tag_buf);
                tag_buf.clear();
                st = State::kText;
              } else {
                if (tag_buf.size() < 2048) tag_buf.push_back(ch);
              }
              break;

            case State::kCommentDash1:
              if (ch == '-') {
                st = State::kComment;
              } else if (ch == '>') {
                st = State::kText;
              } else {
                tag_buf = "!-";
                tag_buf.push_back(ch);
                st = State::kTag;
              }
              break;

            case State::kCommentDash2:
              st = State::kComment;
              break;

            case State::kComment:
              if (ch == '-') st = State::kCommentEnd1;
              break;

            case State::kCommentEnd1:
              if (ch == '-') {
                st = State::kCommentEnd2;
              } else {
                st = State::kComment;
              }
              break;

            case State::kCommentEnd2:
              if (ch == '>') {
                st = State::kText;
              } else if (ch != '-') {
                st = State::kComment;
              }
              break;

            case State::kEntity:
              if (ch == ';') {
                if (ignore_depth == 0) {
                  std::string decoded;
                  DecodeHtmlEntity(ent_buf, &decoded);
                  emit_text_bytes(decoded);
                }
                st = State::kText;
              } else if (std::isspace(static_cast<unsigned char>(ch)) ||
                         ch == '<' || ent_buf.size() >= 14) {
                if (ignore_depth == 0) {
                  emit_text_bytes("&" + ent_buf);
                }
                if (ch == '<') {
                  st = State::kTag;
                  tag_buf.clear();
                } else {
                  if (ignore_depth == 0) emit_text_bytes(std::string(1, ch));
                  st = State::kText;
                }
              } else {
                ent_buf.push_back(ch);
              }
              break;
          }

          if (out_chunk.size() >= 4096 && !flush_out_chunk()) return false;
        }
        return true;
      });

  if (!extracted) return false;
  emit_newline();
  if (!flush_out_chunk()) return false;

  // Any `#fragment` in this spine item that was not matched falls back safely
  // to the start of the chapter.
  for (auto& tgt : raw_toc_) {
    if (tgt.spine_index == static_cast<int>(spine_idx) && !tgt.resolved) {
      tgt.text_offset = chapter_start_off;
      tgt.resolved = true;
    }
  }
  return true;
}

bool EpubIncrementalBuilder::Start(const std::string& epub_sd_path,
                                   uint32_t min_initial_bytes) {
  hal::CheckSdTaskInvariant("EpubIncrementalBuilder::Start");
  Cancel();

  if (!zip_.Open(epub_sd_path)) return false;
  epub_path_ = epub_sd_path;
  epc_final_path_ = EpcPathForBook(epub_sd_path);
  epc_tmp_path_ = epc_final_path_ + ".tmp";
  toc_path_ = TocPathForBook(epub_sd_path);
  book_title_.clear();

  if (!ParsePackageAndToc()) {
    Cancel();
    return false;
  }

  write_buf_ = static_cast<uint8_t*>(AllocPsramOrHeap(kWriteBufCap));
  if (!write_buf_) {
    Cancel();
    return false;
  }

  // Start from a clean `.epc.tmp`.
  ::unlink(core::SdPath(epc_tmp_path_).c_str());

  write_len_ = 0;
  flushed_bytes_ = 0;
  next_spine_ = 0;
  active_ = true;
  done_ = false;
  error_ = false;

  FlushHook noop_before;
  AfterFlushHook noop_after;
  active_before_flush_ = &noop_before;
  active_after_flush_ = &noop_after;

  const size_t target_bytes =
      std::max<size_t>(kInitialReadyBytes, min_initial_bytes + 4096);
  while (next_spine_ < spine_.size()) {
    if (!ConvertOneSpineChapter(next_spine_)) {
      Cancel();
      return false;
    }
    ++next_spine_;
    if (flushed_bytes_ + write_len_ >= target_bytes) break;
  }

  const bool all_done = (next_spine_ >= spine_.size());
  if (!FlushWriteBuffer(/*finalize=*/all_done, noop_before, noop_after)) {
    Cancel();
    return false;
  }
  return true;
}

bool EpubIncrementalBuilder::Step(uint32_t budget_ms,
                                  const FlushHook& before_flush,
                                  const AfterFlushHook& after_flush) {
  if (!active_ || done_) return true;
  const uint32_t t0 = millis();
  active_before_flush_ = &before_flush;
  active_after_flush_ = &after_flush;

  while (next_spine_ < spine_.size()) {
    if (!ConvertOneSpineChapter(next_spine_)) {
      Cancel();
      error_ = true;
      return true;
    }
    ++next_spine_;
    if (millis() - t0 >= budget_ms) break;
  }

  if (next_spine_ >= spine_.size()) {
    FlushWriteBuffer(/*finalize=*/true, before_flush, after_flush);
    return true;
  }
  return false;
}

bool EpubIncrementalBuilder::EnsureOffsetAvailable(
    uint32_t min_offset, const FlushHook& before_flush,
    const AfterFlushHook& after_flush) {
  if (!active_ || done_) return flushed_bytes_ > min_offset || done_;
  active_before_flush_ = &before_flush;
  active_after_flush_ = &after_flush;

  while (next_spine_ < spine_.size() &&
         flushed_bytes_ + write_len_ <= min_offset) {
    if (!ConvertOneSpineChapter(next_spine_)) {
      Cancel();
      error_ = true;
      return false;
    }
    ++next_spine_;
  }
  const bool all_done = (next_spine_ >= spine_.size());
  return FlushWriteBuffer(/*finalize=*/all_done, before_flush, after_flush);
}

bool EpubIncrementalBuilder::EnsureSpineConverted(
    size_t target_spine_index, const FlushHook& before_flush,
    const AfterFlushHook& after_flush) {
  if (!active_ || done_) return true;
  active_before_flush_ = &before_flush;
  active_after_flush_ = &after_flush;

  while (next_spine_ <= target_spine_index && next_spine_ < spine_.size()) {
    if (!ConvertOneSpineChapter(next_spine_)) {
      Cancel();
      error_ = true;
      return false;
    }
    ++next_spine_;
  }
  const bool all_done = (next_spine_ >= spine_.size());
  return FlushWriteBuffer(/*finalize=*/all_done, before_flush, after_flush);
}

std::vector<TocEntry> EpubIncrementalBuilder::CurrentTocEntries() const {
  std::vector<TocEntry> out;
  out.reserve(raw_toc_.size());
  for (const auto& r : raw_toc_) {
    if (r.spine_index < 0) continue;
    TocEntry e;
    e.text_offset = r.text_offset;
    e.level = r.level;
    e.resolved = r.resolved;
    e.spine_index = static_cast<uint16_t>(r.spine_index);
    e.title = r.title;
    out.push_back(std::move(e));
  }
  return out;
}

}  // namespace book
}  // namespace reader
