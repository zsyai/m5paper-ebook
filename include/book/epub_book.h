#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_EPUB_BOOK_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_EPUB_BOOK_H_

// EPUB 2 / EPUB 3 metadata, table-of-contents, standalone illustration marker,
// and incremental background chapter-to-UTF-8 cache builder (Option B).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "book/book_toc.h"
#include "book/zip_reader.h"

namespace reader {
namespace book {

// Returns true if `path` ends with `.epub` (case-insensitive).
bool IsEpubPath(const std::string& path);

// Fast `<dc:title>` lookup for `Bookshelf::refresh()`. Reads cached title from
// `/configs/<filename>.toc` when fresh, or extracts `<dc:title>` from the OPF
// header once and caches it (`complete = false`) without unpacking chapters.
bool ResolveEpubDisplayTitle(const std::string& epub_sd_path,
                             std::string* out_title);

// Checks whether a complete, up-to-date `.epc` + `.toc` cache already exists
// for `epub_sd_path`.
bool TryLoadCompleteEpubCache(const std::string& epub_sd_path, TocData* out_toc,
                              std::string* out_epc_path);

// Extracts a single image entry (`zip_entry_index` from `\x1EIMG:<idx>`) from
// `epub_sd_path` into a newly allocated buffer (PSRAM preferred; caller frees
// with `free()`).
bool ExtractEpubImageEntry(const std::string& epub_sd_path,
                           uint32_t zip_entry_index, size_t max_bytes,
                           uint8_t** out_buf, size_t* out_len);

// Incremental EPUB-to-`.epc` converter (Option B):
//  - `Start()` parses `container.xml`, `.opf`, and NCX/NAV TOC, then converts
//    only enough initial spine chapters to cover `min_initial_bytes` into
//    `<epc>.tmp` so the first reading page opens in `< 0.8 s`.
//  - `Step()` converts remaining spine chapters in bounded idle slices,
//    coalescing writes in a 64 KB PSRAM buffer and coordinating with the caller
//    around each append flush so FatFS `FIL.obj.objsize` never goes stale.
//  - Once the final spine chapter finishes, `<epc>.tmp` is atomically renamed
//    to `<epc>` and the completed `.toc` is persisted.
class EpubIncrementalBuilder {
 public:
  using FlushHook = std::function<void()>;
  using AfterFlushHook =
      std::function<void(const std::string& active_epc_path, uint32_t size)>;

  struct SpineItem {
    size_t zip_index = 0;
    std::string zip_path;
    std::string base_dir;
  };

  struct RawTocTarget {
    int spine_index = -1;
    std::string fragment;
    uint8_t level = 0;
    std::string title;
    bool resolved = false;
    uint32_t text_offset = 0;
  };

  EpubIncrementalBuilder() = default;
  ~EpubIncrementalBuilder();
  EpubIncrementalBuilder(const EpubIncrementalBuilder&) = delete;
  EpubIncrementalBuilder& operator=(const EpubIncrementalBuilder&) = delete;

  // Starts incremental conversion for `epub_sd_path`. On return (when true),
  // `active_epc_path()` already exists on SD with at least the initial chapter
  // (or `min_initial_bytes` if the book is resumed at a saved byte offset).
  bool Start(const std::string& epub_sd_path, uint32_t min_initial_bytes = 0);

  // Advances background conversion by ~`budget_ms`. Invokes `before_flush` and
  // `after_flush` around any SD append or finalization rename. Returns true
  // once all spine chapters are converted and finalized (or on fatal error).
  bool Step(uint32_t budget_ms, const FlushHook& before_flush,
            const AfterFlushHook& after_flush);

  // Synchronously converts spine chapters until `written_total() > min_offset`
  // (or `done()`), used when the reader turns past the currently flushed end.
  bool EnsureOffsetAvailable(uint32_t min_offset, const FlushHook& before_flush,
                             const AfterFlushHook& after_flush);

  // Synchronously converts spine chapters through `target_spine_index`
  // inclusive, used when jumping to an unconverted chapter from `TocPage`.
  bool EnsureSpineConverted(size_t target_spine_index,
                            const FlushHook& before_flush,
                            const AfterFlushHook& after_flush);

  // Aborts an incomplete build, closing file handles, freeing PSRAM buffers,
  // and unlinking `<epc>.tmp`.
  void Cancel();

  bool active() const { return active_ && !done_; }
  bool done() const { return done_; }
  bool error() const { return error_; }

  const std::string& epub_path() const { return epub_path_; }
  const std::string& active_epc_path() const {
    return done_ ? epc_final_path_ : epc_tmp_path_;
  }
  const std::string& book_title() const { return book_title_; }
  uint32_t flushed_bytes() const { return flushed_bytes_; }
  size_t next_spine_index() const { return next_spine_; }
  size_t total_spine_count() const { return spine_.size(); }

  // Snapshot of TOC entries (already-converted chapters carry `resolved = true`
  // and `text_offset`; queued chapters carry `resolved = false` and
  // `spine_index`).
  std::vector<TocEntry> CurrentTocEntries() const;

 private:
  bool ParsePackageAndToc();
  bool ConvertOneSpineChapter(size_t spine_idx);
  bool AppendBytes(const char* data, size_t len, const FlushHook& before_flush,
                   const AfterFlushHook& after_flush);
  bool FlushWriteBuffer(bool finalize, const FlushHook& before_flush,
                        const AfterFlushHook& after_flush);
  void ReleaseRuntimeMemory();

  ZipReader zip_;
  std::string epub_path_;
  std::string epc_tmp_path_;
  std::string epc_final_path_;
  std::string toc_path_;
  std::string book_title_;

  std::vector<SpineItem> spine_;
  std::vector<RawTocTarget> raw_toc_;
  bool auto_toc_from_headings_ = false;

  uint8_t* write_buf_ = nullptr;
  size_t write_len_ = 0;
  uint32_t flushed_bytes_ = 0;
  size_t next_spine_ = 0;

  bool active_ = false;
  bool done_ = true;
  bool error_ = false;
  const FlushHook* active_before_flush_ = nullptr;
  const AfterFlushHook* active_after_flush_ = nullptr;
};

}  // namespace book
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_BOOK_EPUB_BOOK_H_
