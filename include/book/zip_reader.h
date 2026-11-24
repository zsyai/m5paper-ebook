#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_ZIP_READER_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_ZIP_READER_H_

// Minimal, zero-dependency ZIP archive reader tailored for EPUB files on SD.
// Parses the Central Directory once and streams Stored (0) or Deflate (8)
// entries using ESP32 mask-ROM `tinfl_decompress` on device (0 B flash) and
// zlib raw inflate in host unit tests.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace reader {
namespace book {

struct ZipEntry {
  std::string name;
  uint16_t method = 0;
  uint32_t comp_size = 0;
  uint32_t uncomp_size = 0;
  uint32_t local_header_offset = 0;
};

class ZipReader {
 public:
  using SinkFn = std::function<bool(const uint8_t* data, size_t len)>;

  ZipReader() = default;
  ~ZipReader();
  ZipReader(const ZipReader&) = delete;
  ZipReader& operator=(const ZipReader&) = delete;

  // Opens an SD-relative path (e.g. "/books/foo.epub") and parses its Central
  // Directory.
  bool Open(const std::string& sd_path);
  void Close();

  bool is_open() const { return fd_ >= 0; }
  uint32_t archive_size() const { return archive_size_; }
  uint32_t archive_mtime() const { return archive_mtime_; }
  const std::vector<ZipEntry>& entries() const { return entries_; }

  // Finds an entry by normalized archive path (exact match first, then ASCII
  // case-insensitive fallback for Windows-authored EPUBs). Returns -1 if not
  // found.
  int FindEntry(const std::string& path) const;

  // Streams decompressed bytes of `entry_index` into `sink`. Fails if the
  // uncompressed output exceeds `max_uncomp_bytes` or `sink` returns false.
  bool ExtractEntry(size_t entry_index, size_t max_uncomp_bytes,
                    const SinkFn& sink);

  // Extracts `entry_index` up to `max_uncomp_bytes` into `out` (when
  // `truncate_at_max` is true, stops cleanly once `max_uncomp_bytes` have been
  // collected, useful for reading the `<metadata>` prefix of a large `.opf`).
  bool ExtractEntryToString(size_t entry_index, size_t max_uncomp_bytes,
                            std::string* out, bool truncate_at_max = false);

  // Extracts `entry_index` into a newly allocated buffer (PSRAM preferred on
  // device, caller frees with `free()`).
  bool ExtractEntryToBuffer(size_t entry_index, size_t max_uncomp_bytes,
                            uint8_t** out_buf, size_t* out_len);

  // Releases reusable decompression scratch buffers while keeping the directory
  // table and file descriptor open, or closes everything via `Close()`.
  void ReleaseScratchBuffers();

 private:
  bool ReadAt(uint32_t offset, uint8_t* dst, size_t len);
  bool ParseCentralDirectory();
  bool EnsureScratchBuffers();

  int fd_ = -1;
  uint32_t archive_size_ = 0;
  uint32_t archive_mtime_ = 0;
  std::vector<ZipEntry> entries_;
  std::unordered_map<std::string, size_t> exact_index_;
  std::unordered_map<std::string, size_t> lower_index_;

  uint8_t* in_buf_ = nullptr;
  uint8_t* dict_buf_ = nullptr;
  void* decomp_state_ = nullptr;
};

// Normalizes an archive relative path: resolves `.` and `..`, converts `\` to
// `/`, strips leading `/`, and URL-decodes `%XX` escapes.
std::string NormalizeZipPath(const std::string& base_dir,
                             const std::string& href);

}  // namespace book
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_ZIP_READER_H_
