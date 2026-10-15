#include "book/page_index_file.h"

#include <cstdio>

#include "core/perf_trace.h"

namespace reader {
namespace book {
namespace {

constexpr char kMagic[4] = {'R', 'I', 'D', 'X'};
// v3 adds margin, paragraph indent and orientation; older files are rebuilt.
constexpr uint16_t kVersion = 3;

// Header layout (little-endian, as written by the ESP32):
//   char[4] magic, u16 version, u16 reserved,
//   u32 file_size, u32 data_start, u32 font_hash,
//   u16 font_size, u16 line_height, u16 text_w, u16 text_h,
//   u8 encoding, u8 paragraph_indent, u8 orientation, u8 reserved,
//   u16 margin, u16 reserved, u32 page_count, then u32 offsets[].
struct __attribute__((packed)) Header {
  char magic[4];
  uint16_t version;
  uint16_t reserved0;
  uint32_t file_size;
  uint32_t data_start;
  uint32_t font_hash;
  uint16_t font_size;
  uint16_t line_height;
  uint16_t text_w;
  uint16_t text_h;
  uint8_t encoding;
  uint8_t paragraph_indent;
  uint8_t orientation;
  uint8_t reserved1;
  uint16_t margin;
  uint16_t reserved2;
  uint32_t page_count;
};

std::string SdPath(const std::string& path) { return "/sd" + path; }

}  // namespace

bool IndexKey::operator==(const IndexKey& o) const {
  return file_size == o.file_size && data_start == o.data_start &&
         font_hash == o.font_hash && font_size == o.font_size &&
         line_height == o.line_height && text_w == o.text_w &&
         text_h == o.text_h && encoding == o.encoding &&
         margin == o.margin && paragraph_indent == o.paragraph_indent &&
         orientation == o.orientation;
}

uint32_t HashString(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

std::string IndexPathForBook(const std::string& book_path) {
  size_t last_slash = book_path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos)
                             ? book_path
                             : book_path.substr(last_slash + 1);
  return "/configs/" + filename + ".idx";
}

bool LoadPageIndex(const std::string& idx_path, const IndexKey& key,
                   std::vector<uint32_t>* pages) {
  PERF_SCOPE("book::LoadPageIndex");
  pages->clear();
  FILE* f = fopen(SdPath(idx_path).c_str(), "rb");
  if (!f) return false;

  Header h;
  bool ok = fread(&h, sizeof(h), 1, f) == 1;
  ok = ok && h.magic[0] == kMagic[0] && h.magic[1] == kMagic[1] &&
       h.magic[2] == kMagic[2] && h.magic[3] == kMagic[3] &&
       h.version == kVersion && h.page_count > 0;
  if (ok) {
    IndexKey stored;
    stored.file_size = h.file_size;
    stored.data_start = h.data_start;
    stored.font_hash = h.font_hash;
    stored.font_size = h.font_size;
    stored.line_height = h.line_height;
    stored.text_w = h.text_w;
    stored.text_h = h.text_h;
    stored.encoding = h.encoding;
    stored.margin = h.margin;
    stored.paragraph_indent = h.paragraph_indent;
    stored.orientation = h.orientation;
    ok = stored == key;
  }
  if (ok) {
    pages->resize(h.page_count);
    ok = fread(pages->data(), sizeof(uint32_t), h.page_count, f) ==
         h.page_count;
  }
  fclose(f);
  if (!ok) pages->clear();
  PERF_VALUE("index.loaded_pages", pages->size());
  return ok;
}

bool SavePageIndex(const std::string& idx_path, const IndexKey& key,
                   const std::vector<uint32_t>& pages) {
  PERF_SCOPE("book::SavePageIndex");
  if (pages.empty()) return false;
  FILE* f = fopen(SdPath(idx_path).c_str(), "wb");
  if (!f) return false;

  Header h = {};
  for (int i = 0; i < 4; ++i) h.magic[i] = kMagic[i];
  h.version = kVersion;
  h.file_size = key.file_size;
  h.data_start = key.data_start;
  h.font_hash = key.font_hash;
  h.font_size = key.font_size;
  h.line_height = key.line_height;
  h.text_w = key.text_w;
  h.text_h = key.text_h;
  h.encoding = key.encoding;
  h.margin = key.margin;
  h.paragraph_indent = key.paragraph_indent;
  h.orientation = key.orientation;
  h.page_count = pages.size();

  bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
            fwrite(pages.data(), sizeof(uint32_t), pages.size(), f) ==
                pages.size();
  ok = (fclose(f) == 0) && ok;
  if (!ok) remove(SdPath(idx_path).c_str());
  return ok;
}

}  // namespace book
}  // namespace reader
