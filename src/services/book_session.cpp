#include "services/book_session.h"

#include <M5GFX.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "book/page_layout.h"
#include "core/perf_trace.h"
#include "core/sd_path.h"
#include "hal/storage.h"
#include "i18n/i18n.h"
#include "ui/pages/image_picker_page.h"
#include "ui/text_renderer.h"

namespace reader {
namespace services {
namespace {

constexpr size_t kMaxIllustrationBytes = 2 * 1024 * 1024;

std::string BasenameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

}  // namespace

BookSession::BookSession(StorageService& storage, FontService& fonts,
                         FrameService& frame)
    : storage_(storage), fonts_(fonts), frame_(frame) {}

void BookSession::ApplyStyle(const core::Settings& settings) {
  reader_.setStyle(book::ReadingStyle::FromSettings(settings));
}

void BookSession::CancelBackgroundWork() {
  epub_builder_.Cancel();
  txt_toc_scanner_.Stop();
  txt_toc_pending_start_ = false;
}

bool BookSession::Open(const std::string& path, core::Settings* settings,
                       bool show_loading) {
  PERF_SCOPE("BookSession::Open");
  if (reader_.is_open()) reader_.flushConfig();
  CancelBackgroundWork();
  toc_entries_.clear();
  book_title_.clear();
  toc_loaded_ = false;

  const std::string filename = BasenameOf(path);
  const std::string display_stem = book::StripBookExtension(filename);
  const bool is_epub = book::IsEpubPath(path);

  auto show_open_prompt = [&](const std::string& prefix,
                              const std::string& label) {
    if (!show_loading) return;
    ui::UiPainter& p = frame_.painter();
    const int max_text_w = frame_.canvas().width() - 80;
    p.Prepare(prefix + label + "...");
    frame_.ShowPromptBox(prefix + ui::Ellipsize(fonts_.ui_cache(), label,
                                                max_text_w - p.Width(prefix)));
  };

  if (is_epub) {
    book::TocData cached_toc;
    std::string epc_path;
    if (book::TryLoadCompleteEpubCache(path, &cached_toc, &epc_path)) {
      book_title_ = cached_toc.book_title;
      const std::string shown =
          book_title_.empty() ? display_stem : book_title_;
      show_open_prompt(TR("正在打开 "), shown);

      if (!reader_.open(path, epc_path)) return false;
      reader_.setContentComplete(true);
      toc_entries_ = std::move(cached_toc.entries);
      toc_loaded_ = true;
    } else {
      std::string fast_title;
      book::ResolveEpubDisplayTitle(path, &fast_title);
      const std::string shown =
          fast_title.empty() ? display_stem : fast_title;
      show_open_prompt(TR("正在解析 "), shown);

      const uint32_t saved_pos = BookReader::peekSavedOffset(path);
      if (!epub_builder_.Start(path, saved_pos)) return false;
      book_title_ = epub_builder_.book_title();

      if (!reader_.open(path, epub_builder_.active_epc_path())) {
        epub_builder_.Cancel();
        return false;
      }
      reader_.setContentComplete(epub_builder_.done());
      toc_entries_ = epub_builder_.CurrentTocEntries();
      if (epub_builder_.done()) toc_loaded_ = true;
    }
  } else {
    show_open_prompt(TR("正在打开 "), display_stem);
    if (!reader_.open(path)) return false;
    reader_.setContentComplete(true);

    if (path != "/user_guide.txt") {
      struct stat st;
      if (::stat(core::SdPath(path).c_str(), &st) == 0) {
        book::TocData cached_toc;
        if (book::LoadBookToc(book::TocPathForBook(path),
                              static_cast<uint32_t>(st.st_size),
                              static_cast<uint32_t>(st.st_mtime),
                              &cached_toc) &&
            cached_toc.complete) {
          toc_entries_ = std::move(cached_toc.entries);
          toc_loaded_ = true;
        } else {
          txt_toc_pending_start_ = true;
        }
      }
    } else {
      toc_loaded_ = true;
    }
  }

  frame_.RestartReadingRefresh();
  layout_dirty_ = false;
  bool font_replaced = false;
  fonts_.ResolveBodyFontPath(settings, &font_replaced);
  if (font_replaced) ApplyStyle(*settings);

  reader_.loadConfig(path);
  if (path != "/user_guide.txt") {
    if (settings != nullptr) {
      if (reader_.lastReadSeq() > settings->read_seq) {
        settings->read_seq = reader_.lastReadSeq();
      }
      if (show_loading || reader_.lastReadSeq() == 0) {
        settings->read_seq += 1;
        settings->Save();
        reader_.setLastReadSeq(settings->read_seq);
      }
    }
    if (show_loading) {
      storage_.SaveLastReadBook(path);
    }
  }

  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  if (reader_.isContentComplete()) {
    reader_.prepareIndex(fonts_.body_cache(), area.w, area.h);
  }
  reader_.readCurrentPage(fonts_.body_cache(), area.w, area.h);
  PERF_VALUE("openBook.index_ready", reader_.isIndexComplete());
  return true;
}

void BookSession::ReleaseIfPath(const std::string& path) {
  if (reader_.getBookPath() == path) {
    CancelBackgroundWork();
    reader_.reset();
    toc_entries_.clear();
    book_title_.clear();
    toc_loaded_ = false;
  }
  if (book_to_reopen_ == path) {
    book_to_reopen_.clear();
  }
}

void BookSession::CloseForTransfer() {
  FlushConfig();
  CancelBackgroundWork();
  reader_.reset();
  toc_entries_.clear();
  book_title_.clear();
  toc_loaded_ = false;
}

void BookSession::RefreshLayoutIfNeeded(core::Settings* settings) {
  if (!layout_dirty_) return;
  layout_dirty_ = false;
  if (reader_.getBookPath().empty()) return;
  bool font_replaced = false;
  fonts_.ResolveBodyFontPath(settings, &font_replaced);
  if (font_replaced) ApplyStyle(*settings);
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  if (reader_.isContentComplete()) {
    reader_.prepareIndex(fonts_.body_cache(), area.w, area.h);
  }
  reader_.readCurrentPage(fonts_.body_cache(), area.w, area.h);
}

void BookSession::OnEpubConversionCompleted() {
  toc_entries_ = epub_builder_.CurrentTocEntries();
  toc_loaded_ = true;
  reader_.setContentComplete(true);
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  reader_.prepareIndex(fonts_.body_cache(), area.w, area.h);
}

bool BookSession::EnsureEpubOffset(uint32_t min_offset) {
  if (!epub_builder_.active()) return true;
  const bool ok = epub_builder_.EnsureOffsetAvailable(
      min_offset, [this] { reader_.suspendContentFile(); },
      [this](const std::string& epc_path, uint32_t) {
        reader_.resumeContentFile(epc_path);
      });
  toc_entries_ = epub_builder_.CurrentTocEntries();
  if (ok && epub_builder_.done()) OnEpubConversionCompleted();
  return ok;
}

bool BookSession::EnsureEpubSpine(size_t spine_idx) {
  if (!epub_builder_.active()) return true;
  const bool ok = epub_builder_.EnsureSpineConverted(
      spine_idx, [this] { reader_.suspendContentFile(); },
      [this](const std::string& epc_path, uint32_t) {
        reader_.resumeContentFile(epc_path);
      });
  toc_entries_ = epub_builder_.CurrentTocEntries();
  if (ok && epub_builder_.done()) OnEpubConversionCompleted();
  return ok;
}

bool BookSession::FinishEpubBuildWithPrompt() {
  if (!epub_builder_.active()) return true;
  frame_.ShowPromptBox(TR("加载中..."));
  const size_t total = epub_builder_.total_spine_count();
  if (total == 0) return false;
  return EnsureEpubSpine(total - 1);
}

bool BookSession::NextPage(const core::Settings& settings) {
  PERF_SCOPE("BookSession::NextPage");
  if (epub_builder_.active() &&
      reader_.getNextOffset() >= reader_.getFileSize()) {
    EnsureEpubOffset(reader_.getNextOffset() + 4096);
  }
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  if (!reader_.nextPage(fonts_.body_cache(), area.w, area.h)) return false;
  frame_.CountPageTurn();
  DrawReadingView(settings);
  return true;
}

void BookSession::PrevPage(const core::Settings& settings) {
  PERF_SCOPE("BookSession::PrevPage");
  bool showed_prompt = false;
  if (reader_.needsIndexForPrevPage()) {
    frame_.ShowPromptBox(TR("加载中..."));
    showed_prompt = true;
    reader_.indexUntilCurrentPage(fonts_.body_cache());
  }
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  if (!reader_.prevPage(fonts_.body_cache(), area.w, area.h)) {
    if (showed_prompt) DrawReadingView(settings);
    return;
  }
  frame_.CountPageTurn();
  DrawReadingView(settings);
}

bool BookSession::JumpToPage(int page) {
  if (epub_builder_.active() && !FinishEpubBuildWithPrompt()) return false;
  if (!reader_.isIndexComplete() && page >= reader_.getTotalPages()) {
    frame_.ShowPromptBox(TR("加载中..."));
    reader_.indexUntilPageCount(fonts_.body_cache(), page + 1);
  }
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  return reader_.jumpToPage(page, fonts_.body_cache(), area.w, area.h);
}

bool BookSession::JumpToPercentage(float percentage) {
  if (epub_builder_.active() && !FinishEpubBuildWithPrompt()) return false;
  if (!reader_.isIndexComplete()) {
    frame_.ShowPromptBox(TR("加载中..."));
    reader_.finishIndex(fonts_.body_cache());
  }
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  return reader_.jumpToPercentage(percentage, fonts_.body_cache(), area.w,
                                  area.h);
}

void BookSession::EnsureTocReady() {
  if (epub_builder_.active()) {
    toc_entries_ = epub_builder_.CurrentTocEntries();
    return;
  }
  if (toc_loaded_) return;
  if (txt_toc_pending_start_) {
    txt_toc_pending_start_ = false;
    if (!txt_toc_scanner_.Start(reader_.getBookPath(), reader_.getEncoding(),
                                reader_.getDataStart(),
                                reader_.getFileSize())) {
      toc_loaded_ = true;
      return;
    }
  }
  if (txt_toc_scanner_.active()) {
    frame_.ShowPromptBox(TR("加载中..."));
    txt_toc_scanner_.Finish();
    if (!txt_toc_scanner_.read_error()) {
      toc_entries_ = txt_toc_scanner_.entries();
      toc_loaded_ = true;
    }
  }
}

int BookSession::ActiveTocIndex() const {
  const uint32_t cur = reader_.getCurrentOffset();
  int active = -1;
  for (size_t i = 0; i < toc_entries_.size(); ++i) {
    if (!toc_entries_[i].resolved) continue;
    if (toc_entries_[i].text_offset <= cur) {
      active = static_cast<int>(i);
    } else {
      break;
    }
  }
  return active;
}

int BookSession::PageForTocEntry(size_t index) const {
  if (index >= toc_entries_.size() || !toc_entries_[index].resolved) return -1;
  return reader_.getPageIndexForOffset(toc_entries_[index].text_offset);
}

bool BookSession::JumpToTocEntry(size_t index) {
  if (epub_builder_.active()) {
    toc_entries_ = epub_builder_.CurrentTocEntries();
  }
  if (index >= toc_entries_.size()) return false;
  if (!toc_entries_[index].resolved) {
    frame_.ShowPromptBox(TR("加载中..."));
    if (!EnsureEpubSpine(toc_entries_[index].spine_index)) return false;
  }
  if (index >= toc_entries_.size() || !toc_entries_[index].resolved) {
    return false;
  }
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  return reader_.jumpToOffset(toc_entries_[index].text_offset,
                              fonts_.body_cache(), area.w, area.h,
                              /*start_index_if_incomplete=*/
                              reader_.isContentComplete());
}

void BookSession::RenderCurrentPage() {
  frame_.canvas().fillScreen(TFT_WHITE);
  const book::TextArea area = frame_.ReadingTextArea(reader_.style());
  const std::string text = reader_.getPageText();
  if (text.empty()) {
    ui::UiPainter& p = frame_.painter();
    p.Prepare(TR("文件为空或读取失败"));
    p.Text(TR("文件为空或读取失败"), area.x, area.y);
    return;
  }

  uint32_t zip_entry_idx = 0;
  if (book::ParseImagePageMarker(text, &zip_entry_idx) &&
      book::IsEpubPath(reader_.getBookPath())) {
    uint8_t* img_buf = nullptr;
    size_t img_len = 0;
    bool drawn = false;
    if (book::ExtractEpubImageEntry(reader_.getBookPath(), zip_entry_idx,
                                    kMaxIllustrationBytes, &img_buf,
                                    &img_len)) {
      drawn = ui::DrawCenteredImageFromMemory(
          frame_.canvas(), img_buf, img_len,
          ui::Rect{area.x, area.y, area.w, area.h});
      std::free(img_buf);
    }
    if (!drawn) {
      ui::UiPainter& p = frame_.painter();
      const std::string msg = TR("图片解析失败");
      p.Prepare(msg);
      p.TextIn(msg, ui::Rect{area.x, area.y, area.w, area.h});
    }
    return;
  }

  const book::ReadingStyle& style = reader_.style();
  fonts_.body_cache().build(text, style.font_path);
  frame_.DrawBodyText(fonts_.body_cache(), style, text, area.x, area.y, area.w,
                      area.h);
}

void BookSession::DrawReadingView(const core::Settings& settings,
                                  bool force_full_refresh) {
  PERF_SCOPE("BookSession::DrawReadingView");
  if (hal::SdErrorPending()) return;
  RenderCurrentPage();
  frame_.PushReadingPage(settings.full_refresh_pages, force_full_refresh);
}

void BookSession::DrawBodyPreview(const ui::Rect& box) {
  PERF_SCOPE("BookSession::DrawBodyPreview");
  const char* const sample = i18n::PreviewSampleText();
  const book::ReadingStyle& style = reader_.style();
  PageFontCache& cache = fonts_.body_cache();
  cache.build(std::string(), style.font_path);
  book::LayoutParams layout;
  layout.text_w = box.w;
  layout.text_h = box.h;
  layout.font_size = cache.getFontSize();
  layout.line_height = style.LineHeight(layout.font_size);
  layout.paragraph_indent = style.paragraph_indent;
  book::LayoutCursor cursor;
  cursor.paragraph_start = true;
  bool filled = false;
  std::string text;
  book::MeasurePage(reinterpret_cast<const uint8_t*>(sample),
                    std::strlen(sample), layout, cache, &cursor, &filled,
                    &text);
  cache.build(text, style.font_path);
  frame_.DrawBodyText(cache, style, text, box.x, box.y, box.w, box.h);
}

std::string BookSession::ProgressText() const {
  char buf[40];
  const int page = reader_.getCurrentPageIndex();
  const float progress = reader_.getProgress();
  if (reader_.isIndexComplete()) {
    std::snprintf(buf, sizeof(buf), "%d/%d  %.1f%%", page + 1,
                  reader_.getTotalPages(), progress);
  } else if (!reader_.isContentComplete()) {
    std::snprintf(buf, sizeof(buf), "…");
  } else if (page >= 0) {
    std::snprintf(buf, sizeof(buf), "%d/…  %.1f%%", page + 1, progress);
  } else {
    std::snprintf(buf, sizeof(buf), "…  %.1f%%", progress);
  }
  return buf;
}

std::string BookSession::Title() const {
  if (!book_title_.empty()) return book_title_;
  return book::StripBookExtension(BasenameOf(reader_.getBookPath()));
}

bool BookSession::HasBackgroundWork() const {
  return epub_builder_.active() || reader_.hasPendingIndex() ||
         txt_toc_pending_start_ || txt_toc_scanner_.active();
}

bool BookSession::StepBackgroundWork(uint32_t budget_ms) {
  // Stage 1: Finish incremental EPUB extraction first so `.epc` reaches its
  // final size before `PageIndexer` starts.
  if (epub_builder_.active()) {
    const bool finished = epub_builder_.Step(
        budget_ms, [this] { reader_.suspendContentFile(); },
        [this](const std::string& epc_path, uint32_t) {
          reader_.resumeContentFile(epc_path);
        });
    if (epub_builder_.error()) return false;
    if (finished && epub_builder_.done()) {
      OnEpubConversionCompleted();
      return true;
    }
    return false;
  }

  // Stage 2: Build or finish the `.idx` pagination index.
  if (reader_.hasPendingIndex()) {
    return reader_.stepIndex(fonts_.body_cache(), budget_ms);
  }

  // Stage 3: Scan plain-text chapter headings into `.toc` once `.idx` is idle.
  if (txt_toc_pending_start_) {
    txt_toc_pending_start_ = false;
    if (!reader_.is_open() ||
        !txt_toc_scanner_.Start(reader_.getBookPath(), reader_.getEncoding(),
                                reader_.getDataStart(),
                                reader_.getFileSize())) {
      toc_loaded_ = true;
      return false;
    }
  }
  if (txt_toc_scanner_.active()) {
    if (!txt_toc_scanner_.Step(budget_ms)) return false;
    if (!txt_toc_scanner_.read_error()) {
      toc_entries_ = txt_toc_scanner_.entries();
      toc_loaded_ = true;
      return true;
    }
  }
  return false;
}

void BookSession::FlushConfig() {
  if (!storage_.card_present() || hal::SdErrorPending()) return;
  reader_.flushConfig();
}

void BookSession::OnReadingIdle(uint32_t now_ms) {
  if (!storage_.card_present() || hal::SdErrorPending()) return;
  reader_.warmPrefetch(fonts_.body_cache());
  reader_.flushConfigIfIdle(now_ms);
}

void BookSession::PrepareForUnmount() {
  if (reader_.is_open()) {
    book_to_reopen_ = reader_.getBookPath();
  }
  CancelBackgroundWork();
  reader_.close();
}

bool BookSession::ReopenAfterRemount(core::Settings* settings) {
  if (book_to_reopen_.empty()) return true;
  const bool ok = Open(book_to_reopen_, settings, /*show_loading=*/false);
  book_to_reopen_.clear();
  return ok;
}

}  // namespace services
}  // namespace reader
