#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_BOOK_SESSION_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_BOOK_SESSION_H_

// Owns the active book session: opening (.txt and .epub), incremental EPUB
// background conversion (Option B), page turns, jumps, TOC navigation,
// standalone illustration rendering, background `.idx` + TXT `.toc` building,
// idle prefetch/config flush, and SD-remount / Wi-Fi-transfer handle safety.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "book/book_reader.h"
#include "book/book_toc.h"
#include "book/epub_book.h"
#include "core/settings.h"
#include "services/font_service.h"
#include "services/frame_service.h"
#include "services/storage_service.h"
#include "ui/rect.h"

namespace reader {
namespace services {

class BookSession {
 public:
  BookSession(StorageService& storage, FontService& fonts, FrameService& frame);

  void ApplyStyle(const core::Settings& settings);
  void MarkLayoutDirty() { layout_dirty_ = true; }

  bool Open(const std::string& path, core::Settings* settings,
            bool show_loading = true);
  void ReleaseIfPath(const std::string& path);
  // Flushes config, cancels all background book tasks, and closes all open book
  // handles so `TransferPage`'s `webserver_task` has exclusive SD access.
  void CloseForTransfer();
  void RefreshLayoutIfNeeded(core::Settings* settings);

  bool NextPage(const core::Settings& settings);
  void PrevPage(const core::Settings& settings);
  bool JumpToPage(int page);
  bool JumpToPercentage(float percentage);

  // Table-of-contents queries and navigation (`TocPage`).
  void EnsureTocReady();
  const std::vector<book::TocEntry>& TocEntries() const { return toc_entries_; }
  int ActiveTocIndex() const;
  // Returns 0-based page index for `toc_entries_[index]`, or -1 while pending.
  int PageForTocEntry(size_t index) const;
  bool JumpToTocEntry(size_t index);

  // Renders the current page into `frame_.canvas()` without pushing it.
  void RenderCurrentPage();
  void DrawReadingView(const core::Settings& settings,
                       bool force_full_refresh = false);
  void DrawBodyPreview(const ui::Rect& box);

  std::string ProgressText() const;
  std::string Title() const;

  bool IsOpen() const { return reader_.is_open(); }
  std::string BookPath() const { return reader_.getBookPath(); }
  bool IsUserGuide() const {
    return reader_.getBookPath() == "/user_guide.txt";
  }
  bool IsIndexComplete() const { return reader_.isIndexComplete(); }

  // Serialized background pipeline: Stage 1 (EPUB incremental conversion) ->
  // Stage 2 (`PageIndexer`) -> Stage 3 (`TxtTocScanner`).
  bool HasBackgroundWork() const;
  // Advances the active stage by `budget_ms`; returns true when a stage
  // finishes so open overlays (`ReaderMenuPage`, `JumpPage`, `TocPage`) can
  // refresh quietly.
  bool StepBackgroundWork(uint32_t budget_ms);
  void CancelBackgroundWork();

  int CurrentPageIndex() const { return reader_.getCurrentPageIndex(); }
  int TotalPages() const { return reader_.getTotalPages(); }
  uint32_t CurrentOffset() const { return reader_.getCurrentOffset(); }
  float Progress() const { return reader_.getProgress(); }

  void SaveConfig() { reader_.saveConfig(); }
  void FlushConfig();
  void OnReadingIdle(uint32_t now_ms);

  // Remembers the active book path and closes file handles before SD unmount.
  void PrepareForUnmount();
  // Reopens any book remembered before SD unmount; returns false if a book was
  // remembered and could not be reopened on the newly mounted card.
  bool ReopenAfterRemount(core::Settings* settings);

 private:
  void OnEpubConversionCompleted();
  bool FinishEpubBuildWithPrompt();
  bool EnsureEpubOffset(uint32_t min_offset);
  bool EnsureEpubSpine(size_t spine_idx);

  StorageService& storage_;
  FontService& fonts_;
  FrameService& frame_;
  BookReader reader_;
  book::EpubIncrementalBuilder epub_builder_;
  book::TxtTocScanner txt_toc_scanner_;
  std::vector<book::TocEntry> toc_entries_;
  std::string book_title_;
  std::string book_to_reopen_;
  bool toc_loaded_ = false;
  bool txt_toc_pending_start_ = false;
  bool layout_dirty_ = false;
};

}  // namespace services
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_BOOK_SESSION_H_
