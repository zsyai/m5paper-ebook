#include "book/zip_reader.h"

#include <esp_heap_caps.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if READER_HOST_TEST
#include <zlib.h>
#else
#include <esp32/rom/miniz.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/sd_path.h"
#include "hal/storage.h"

namespace reader {
namespace book {
namespace {

constexpr uint32_t kEocdSig = 0x06054b50u;
constexpr uint32_t kCdHeaderSig = 0x02014b50u;
constexpr uint32_t kLocalHeaderSig = 0x04034b50u;
constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflate = 8;

constexpr size_t kInBufSize = 16 * 1024;
constexpr size_t kDictBufSize = 32 * 1024;
constexpr size_t kMaxCdBytes = 2 * 1024 * 1024;
constexpr size_t kMaxEntries = 8192;

uint16_t ReadLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void* AllocPsramOrHeap(size_t bytes) {
  void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ptr) ptr = std::malloc(bytes);
  return ptr;
}

int HexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string UrlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      const int hi = HexNibble(s[i + 1]);
      const int lo = HexNibble(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string AsciiLower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

}  // namespace

std::string NormalizeZipPath(const std::string& base_dir,
                             const std::string& href) {
  std::string clean_href = href;
  const size_t hash = clean_href.find('#');
  if (hash != std::string::npos) clean_href.resize(hash);
  const size_t query = clean_href.find('?');
  if (query != std::string::npos) clean_href.resize(query);
  clean_href = UrlDecode(clean_href);

  std::string combined;
  if (!clean_href.empty() &&
      (clean_href[0] == '/' || clean_href[0] == '\\' || base_dir.empty())) {
    combined = clean_href;
  } else if (base_dir.back() == '/') {
    combined = base_dir + clean_href;
  } else {
    combined = base_dir + "/" + clean_href;
  }

  for (char& c : combined) {
    if (c == '\\') c = '/';
  }

  std::vector<std::string> parts;
  size_t pos = 0;
  while (pos <= combined.size()) {
    const size_t slash = combined.find('/', pos);
    const size_t end = (slash == std::string::npos) ? combined.size() : slash;
    const std::string seg = combined.substr(pos, end - pos);
    if (!seg.empty() && seg != ".") {
      if (seg == "..") {
        if (!parts.empty()) parts.pop_back();
      } else {
        parts.push_back(seg);
      }
    }
    if (slash == std::string::npos) break;
    pos = slash + 1;
  }

  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) result.push_back('/');
    result.append(parts[i]);
  }
  return result;
}

ZipReader::~ZipReader() { Close(); }

bool ZipReader::Open(const std::string& sd_path) {
  hal::CheckSdTaskInvariant("ZipReader::Open");
  Close();

  const std::string full = core::SdPath(sd_path);
  struct stat st;
  if (::stat(full.c_str(), &st) != 0 || st.st_size < 22) return false;

  const int fd = ::open(full.c_str(), O_RDONLY);
  if (fd < 0) {
    hal::ReportSdError();
    return false;
  }
  fd_ = fd;
  archive_size_ = static_cast<uint32_t>(st.st_size);
  archive_mtime_ = static_cast<uint32_t>(st.st_mtime);

  if (!ParseCentralDirectory()) {
    Close();
    return false;
  }
  return true;
}

void ZipReader::ReleaseScratchBuffers() {
  std::free(in_buf_);
  in_buf_ = nullptr;
  std::free(dict_buf_);
  dict_buf_ = nullptr;
  std::free(decomp_state_);
  decomp_state_ = nullptr;
}

void ZipReader::Close() {
  ReleaseScratchBuffers();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  archive_size_ = 0;
  archive_mtime_ = 0;
  entries_.clear();
  exact_index_.clear();
  lower_index_.clear();
}

bool ZipReader::ReadAt(uint32_t offset, uint8_t* dst, size_t len) {
  hal::CheckSdTaskInvariant("ZipReader::ReadAt");
  if (fd_ < 0 || offset > archive_size_ || len > archive_size_ - offset) {
    return false;
  }
  if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) {
    hal::ReportSdError();
    return false;
  }
  size_t done = 0;
  while (done < len) {
    const ssize_t n = ::read(fd_, dst + done, len - done);
    if (n <= 0) {
      hal::ReportSdError();
      return false;
    }
    done += static_cast<size_t>(n);
  }
  return true;
}

bool ZipReader::ParseCentralDirectory() {
  auto find_eocd = [&](size_t tail_want, uint32_t* cd_offset,
                       uint32_t* cd_size, uint16_t* entry_count) -> bool {
    const size_t tail_len = std::min<size_t>(archive_size_, tail_want);
    uint8_t* tail = static_cast<uint8_t*>(AllocPsramOrHeap(tail_len));
    if (!tail) return false;
    const uint32_t tail_off = archive_size_ - static_cast<uint32_t>(tail_len);
    if (!ReadAt(tail_off, tail, tail_len)) {
      std::free(tail);
      return false;
    }
    bool found = false;
    for (size_t i = tail_len - 22; ; --i) {
      if (ReadLe32(tail + i) == kEocdSig) {
        const uint16_t comment_len = ReadLe16(tail + i + 20);
        if (i + 22 + comment_len <= tail_len) {
          *entry_count = ReadLe16(tail + i + 10);
          *cd_size = ReadLe32(tail + i + 12);
          *cd_offset = ReadLe32(tail + i + 16);
          found = true;
          break;
        }
      }
      if (i == 0) break;
    }
    std::free(tail);
    return found;
  };

  uint32_t cd_offset = 0;
  uint32_t cd_size = 0;
  uint16_t expected_entries = 0;
  if (!find_eocd(1024, &cd_offset, &cd_size, &expected_entries) &&
      !find_eocd(65557, &cd_offset, &cd_size, &expected_entries)) {
    return false;
  }
  if (cd_size == 0 || cd_size > kMaxCdBytes || cd_offset > archive_size_ ||
      cd_size > archive_size_ - cd_offset) {
    return false;
  }

  uint8_t* cd_buf = static_cast<uint8_t*>(AllocPsramOrHeap(cd_size));
  if (!cd_buf) return false;
  if (!ReadAt(cd_offset, cd_buf, cd_size)) {
    std::free(cd_buf);
    return false;
  }

  entries_.reserve(std::min<size_t>(expected_entries, kMaxEntries));
  size_t pos = 0;
  while (pos + 46 <= cd_size && entries_.size() < kMaxEntries) {
    if (ReadLe32(cd_buf + pos) != kCdHeaderSig) break;
    const uint16_t flags = ReadLe16(cd_buf + pos + 8);
    const uint16_t method = ReadLe16(cd_buf + pos + 10);
    const uint32_t comp_size = ReadLe32(cd_buf + pos + 20);
    const uint32_t uncomp_size = ReadLe32(cd_buf + pos + 24);
    const uint16_t name_len = ReadLe16(cd_buf + pos + 28);
    const uint16_t extra_len = ReadLe16(cd_buf + pos + 30);
    const uint16_t comment_len = ReadLe16(cd_buf + pos + 32);
    const uint32_t local_off = ReadLe32(cd_buf + pos + 42);

    const size_t next_pos = pos + 46 + name_len + extra_len + comment_len;
    if (next_pos > cd_size) break;

    // Skip encrypted entries (bit 0) and directory entries (trailing '/').
    if ((flags & 1u) == 0 && name_len > 0) {
      const std::string raw_name(
          reinterpret_cast<const char*>(cd_buf + pos + 46), name_len);
      if (raw_name.back() != '/' && raw_name.back() != '\\') {
        ZipEntry e;
        e.name = NormalizeZipPath("", raw_name);
        e.method = method;
        e.comp_size = comp_size;
        e.uncomp_size = uncomp_size;
        e.local_header_offset = local_off;
        if (!e.name.empty()) {
          const size_t idx = entries_.size();
          entries_.push_back(e);
          exact_index_.emplace(entries_[idx].name, idx);
          lower_index_.emplace(AsciiLower(entries_[idx].name), idx);
        }
      }
    }
    pos = next_pos;
  }

  std::free(cd_buf);
  return !entries_.empty();
}

int ZipReader::FindEntry(const std::string& path) const {
  const std::string norm = NormalizeZipPath("", path);
  auto it = exact_index_.find(norm);
  if (it != exact_index_.end()) return static_cast<int>(it->second);
  auto lit = lower_index_.find(AsciiLower(norm));
  if (lit != lower_index_.end()) return static_cast<int>(lit->second);
  return -1;
}

bool ZipReader::EnsureScratchBuffers() {
  if (!in_buf_) {
    in_buf_ = static_cast<uint8_t*>(AllocPsramOrHeap(kInBufSize));
  }
  if (!dict_buf_) {
    dict_buf_ = static_cast<uint8_t*>(AllocPsramOrHeap(kDictBufSize));
  }
#if !READER_HOST_TEST
  if (!decomp_state_) {
    decomp_state_ = AllocPsramOrHeap(sizeof(tinfl_decompressor));
  }
  return in_buf_ != nullptr && dict_buf_ != nullptr && decomp_state_ != nullptr;
#else
  return in_buf_ != nullptr && dict_buf_ != nullptr;
#endif
}

bool ZipReader::ExtractEntry(size_t entry_index, size_t max_uncomp_bytes,
                             const SinkFn& sink) {
  if (fd_ < 0 || entry_index >= entries_.size()) return false;
  const ZipEntry& e = entries_[entry_index];
  if (e.uncomp_size > max_uncomp_bytes) return false;
  if (e.method != kMethodStored && e.method != kMethodDeflate) return false;
  if (!EnsureScratchBuffers()) return false;

  uint8_t lhdr[30];
  if (!ReadAt(e.local_header_offset, lhdr, sizeof(lhdr)) ||
      ReadLe32(lhdr) != kLocalHeaderSig) {
    return false;
  }
  const uint16_t fname_len = ReadLe16(lhdr + 26);
  const uint16_t extra_len = ReadLe16(lhdr + 28);
  const uint64_t data_off64 = static_cast<uint64_t>(e.local_header_offset) +
                              30u + fname_len + extra_len;
  if (data_off64 + e.comp_size > archive_size_) return false;
  uint32_t cur_off = static_cast<uint32_t>(data_off64);
  uint32_t rem_comp = e.comp_size;

  if (e.uncomp_size == 0 || e.comp_size == 0) {
    return e.uncomp_size == 0;
  }

  if (e.method == kMethodStored) {
    if (e.comp_size != e.uncomp_size) return false;
    while (rem_comp > 0) {
      const size_t chunk = std::min<size_t>(kInBufSize, rem_comp);
      if (!ReadAt(cur_off, in_buf_, chunk)) return false;
      if (!sink(in_buf_, chunk)) return false;
      cur_off += static_cast<uint32_t>(chunk);
      rem_comp -= static_cast<uint32_t>(chunk);
    }
    return true;
  }

  size_t produced_total = 0;
#if READER_HOST_TEST
  z_stream strm = {};
  if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
  bool ok = true;
  bool stream_done = false;
  while (ok && !stream_done) {
    if (strm.avail_in == 0 && rem_comp > 0) {
      const size_t chunk = std::min<size_t>(kInBufSize, rem_comp);
      if (!ReadAt(cur_off, in_buf_, chunk)) {
        ok = false;
        break;
      }
      cur_off += static_cast<uint32_t>(chunk);
      rem_comp -= static_cast<uint32_t>(chunk);
      strm.next_in = in_buf_;
      strm.avail_in = static_cast<uInt>(chunk);
    }
    strm.next_out = dict_buf_;
    strm.avail_out = static_cast<uInt>(kDictBufSize);
    const int ret = inflate(&strm, Z_NO_FLUSH);
    const size_t have = kDictBufSize - strm.avail_out;
    if (have > 0) {
      produced_total += have;
      if (produced_total > max_uncomp_bytes || !sink(dict_buf_, have)) {
        ok = false;
        break;
      }
    }
    if (ret == Z_STREAM_END) {
      stream_done = true;
    } else if (ret != Z_OK) {
      ok = false;
    } else if (strm.avail_in == 0 && rem_comp == 0 && have == 0) {
      ok = false;
    }
  }
  inflateEnd(&strm);
  return ok && stream_done;
#else
  auto* decomp = static_cast<tinfl_decompressor*>(decomp_state_);
  tinfl_init(decomp);
  size_t avail_in = 0;
  const uint8_t* next_in = in_buf_;
  size_t dict_ofs = 0;

  while (true) {
    if (avail_in == 0 && rem_comp > 0) {
      const size_t chunk = std::min<size_t>(kInBufSize, rem_comp);
      if (!ReadAt(cur_off, in_buf_, chunk)) return false;
      cur_off += static_cast<uint32_t>(chunk);
      rem_comp -= static_cast<uint32_t>(chunk);
      next_in = in_buf_;
      avail_in = chunk;
    }
    size_t in_bytes = avail_in;
    size_t out_bytes = kDictBufSize - dict_ofs;
    const mz_uint32 flags = (rem_comp > 0) ? TINFL_FLAG_HAS_MORE_INPUT : 0;
    const tinfl_status status =
        tinfl_decompress(decomp, next_in, &in_bytes, dict_buf_,
                         dict_buf_ + dict_ofs, &out_bytes, flags);
    next_in += in_bytes;
    avail_in -= in_bytes;

    if (out_bytes > 0) {
      produced_total += out_bytes;
      if (produced_total > max_uncomp_bytes ||
          !sink(dict_buf_ + dict_ofs, out_bytes)) {
        return false;
      }
      dict_ofs = (dict_ofs + out_bytes) & (kDictBufSize - 1);
    }
    if (status == TINFL_STATUS_DONE) return true;
    if (status < TINFL_STATUS_DONE) return false;
    if (status == TINFL_STATUS_NEEDS_MORE_INPUT && avail_in == 0 &&
        rem_comp == 0) {
      return false;
    }
  }
#endif
}

bool ZipReader::ExtractEntryToString(size_t entry_index,
                                     size_t max_uncomp_bytes, std::string* out,
                                     bool truncate_at_max) {
  out->clear();
  if (entry_index >= entries_.size()) return false;
  const ZipEntry& e = entries_[entry_index];
  if (!truncate_at_max && e.uncomp_size > max_uncomp_bytes) return false;
  out->reserve(std::min<size_t>(e.uncomp_size, max_uncomp_bytes));

  bool stopped_at_cap = false;
  const size_t limit =
      truncate_at_max ? std::max<size_t>(e.uncomp_size, max_uncomp_bytes)
                      : max_uncomp_bytes;
  const bool ok = ExtractEntry(
      entry_index, limit, [&](const uint8_t* data, size_t len) -> bool {
        const size_t room = max_uncomp_bytes - out->size();
        const size_t take = std::min(len, room);
        if (take > 0) {
          out->append(reinterpret_cast<const char*>(data), take);
        }
        if (truncate_at_max && out->size() >= max_uncomp_bytes) {
          stopped_at_cap = true;
          return false;
        }
        return take == len;
      });
  return ok || stopped_at_cap;
}

bool ZipReader::ExtractEntryToBuffer(size_t entry_index,
                                     size_t max_uncomp_bytes, uint8_t** out_buf,
                                     size_t* out_len) {
  *out_buf = nullptr;
  *out_len = 0;
  if (entry_index >= entries_.size()) return false;
  const ZipEntry& e = entries_[entry_index];
  if (e.uncomp_size == 0 || e.uncomp_size > max_uncomp_bytes) return false;

  uint8_t* buf = static_cast<uint8_t*>(AllocPsramOrHeap(e.uncomp_size));
  if (!buf) return false;

  size_t written = 0;
  const bool ok = ExtractEntry(
      entry_index, e.uncomp_size, [&](const uint8_t* data, size_t len) -> bool {
        if (written + len > e.uncomp_size) return false;
        std::memcpy(buf + written, data, len);
        written += len;
        return true;
      });
  if (!ok || written == 0) {
    std::free(buf);
    return false;
  }
  *out_buf = buf;
  *out_len = written;
  return true;
}

}  // namespace book
}  // namespace reader
