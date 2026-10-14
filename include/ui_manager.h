#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_MANAGER_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_MANAGER_H_

#include "bookshelf.h"
#include "transfer_page.h"
#include "user_guide_page.h"
#include "font_buffer.h"
#include <M5GFX.h>
#include <atomic>
#include <SD.h>
#include "book_reader.h"
#include "book/reading_style.h"
#include "core/settings.h"
#include "font_manager.h"
#include "hal/display.h"
#include "ui/painter.h"
#include "ui/pages/reader_pages.h"
#include "ui/pages/reading_settings_page.h"
#include "ui/pages/system_settings_page.h"
#include "ui/rect.h"

class MyFileDataWrapper : public lgfx::v1::DataWrapper {
    File _file;
 public:
    MyFileDataWrapper(File f) : _file(f) {}
    int read(uint8_t *buf, uint32_t len) override { return _file.read(buf, len); }
    void skip(int32_t offset) override { _file.seek(_file.position() + offset); }
    bool seek(uint32_t offset) override { return _file.seek(offset); }
    void close(void) override { _file.close(); }
    int32_t tell(void) override { return _file.position(); }
};
#include "page.h"
#include "bookshelf_page.h"
#include "image_picker_page.h"

class UIManager {
 public:
  void init();
  void handleInput();
  void openBook(const std::string& path, bool show_loading = true);
  void nextPage();
  void prevPage();

  // Background page indexing. Called from the state machine task between
  // input events (it shares the SD card and the glyph cache with page turns).
  static constexpr uint32_t kBackgroundStepMs = 40;
  bool hasBackgroundWork() const;
  void backgroundStep();
  // setup() runs in loopTask while the state machine task is already alive
  // and both use the SD card and glyph caches without locks, so background
  // work only starts once setup() is done drawing.
  void enableBackgroundWork() { background_enabled_ = true; }
  // Progress line for the reader menu, e.g. "12/340  3.5%".
  std::string progressText() const;

  // Getters and Setters for state machine integration
  int getSelection() const { return currentSelection; }
  void setSelection(int sel) { currentSelection = sel; }
  
  std::string getLastReadBook();
  void drawCachedString(const std::string& text, int x, int y) {
    painter_.Text(text, x, y);
  }
  void showPromptBox(const std::string& text);
  int getStringWidth(const std::string& text) { return painter_.Width(text); }
  // ---- Fonts -----------------------------------------------------------
  // The UI (menus, bookshelf, settings, prompts) and the body text use
  // separate glyph caches: the UI font has a fixed size (the built-in
  // font or any font from /fonts, TTFs at kUiFontPixelSize), while the body
  // font size is adjustable for TTF fonts.
  static constexpr uint8_t kUiFontPixelSize = 30;

  // Makes sure the glyphs of UI `text` are cached.
  void buildFontCache(const std::string& text) { painter_.Prepare(text); }
  // Body font path after fallback (missing file -> another /fonts font or
  // "builtin"); a changed result is saved back to the settings.
  std::string resolvedBodyFontPath();

  std::string getFontPath() const { return settings_.body_font_path; }
  void setFontPath(const std::string& path);
  // Effective UI font: a .ttf from /fonts or "builtin". Fixed-size .bin
  // fonts are not allowed for the UI (older settings may still name one).
  std::string getUiFontPath() const {
    const std::string& path = settings_.ui_font_path;
    return PageFontCache::IsTtfPath(path) ? path : "builtin";
  }
  void setUiFontPath(const std::string& path);
  int getBodyFontSize() const { return settings_.body_font_size; }
  void setBodyFontSize(int size);
  // True if the body font is a TTF, i.e. its size can be changed.
  bool isBodyFontScalable() const {
    return PageFontCache::IsTtfPath(settings_.body_font_path);
  }
  // True if the configured body font file is missing and the built-in font
  // is used instead (choosing "builtin" on purpose does not count).
  bool isUsingBuiltinFont() {
    return settings_.body_font_path != "builtin" &&
           resolvedBodyFontPath() == "builtin";
  }

  // TTF glyph cache files (fonts::FontCachePath) of the current UI and body
  // fonts and of the fonts open right now; cache cleaning keeps these.
  std::vector<std::string> fontCachesInUse() const;

  // UI metrics.
  int getFontSize() const { return uiCache.getFontSize(); }
  int getHeaderHeight() const { return getFontSize() * 2.5; }
  int getFooterHeight() const { return getFontSize() * 3.0; }
  float getLineSpacingFactor() const { return settings_.line_spacing; }
  void setLineSpacingFactor(float factor);
  int getMargin() const { return settings_.margin; }
  void setMargin(int margin);
  bool getParagraphIndent() const { return settings_.paragraph_indent; }
  void setParagraphIndent(bool on);
  bool getLineUnderline() const { return settings_.line_underline; }
  void setLineUnderline(bool on);
  reader::core::Orientation getOrientation() const {
    return settings_.orientation;
  }
  void setOrientation(reader::core::Orientation orientation);
  int getFullRefreshPages() const { return settings_.full_refresh_pages; }
  void setFullRefreshPages(int pages);
  bool saveGlobalConfig() { return settings_.Save(); }
  void saveCurrentBookConfig();
  bool isLockScreenEnabled() const { return settings_.lock_screen_enabled; }
  void setLockScreenEnabled(bool enable) {
    settings_.lock_screen_enabled = enable;
  }
  std::string getLockScreenPath() const { return settings_.lock_screen_path; }
  void setLockScreenPath(const std::string& path) {
    settings_.lock_screen_path = path;
  }
  void setBootToReaderFlag(bool enable);
  bool checkBootToReaderFlag();
  void enterDeepSleep(bool from_reader);
  bool isReadingUserGuide() const { return bookReader.getBookPath() == "/user_guide.txt"; }

  // Also sets the canvas rotation for the page (see
  // Page::followsReadingDirection()).
  void setPage(Page* page);
  // Converts a touch or wheel message from panel coordinates to the current
  // page's canvas orientation. Called before the page handles the message.
  void mapInputToPage(SystemMessage_t* msg) const;
  // Draws and pushes the current page (Page::show()).
  void drawCurrentPage();
  Page* getCurrentPage() { return currentPage; }
  void setPageByState(State state);
  void setTFCardPresent(bool p) { tf_card_present_ = p; }
  bool hasTFCard() const { return tf_card_present_; }
  void drawHeader(const std::string& title);
  void drawBottomBar(const std::vector<std::string>& items);
  void drawBattery(int right_x, int y);

  // ---- Drawing helpers for pages ----------------------------------------
  LGFX_Sprite& getCanvas() { return canvas; }
  reader::ui::UiPainter& painter() { return painter_; }
  // Pushes the canvas with the UI refresh policy (a full refresh every few
  // frames, GL16 otherwise).
  void pushUiFrame();
  void pushCanvas(lgfx::epd_mode_t mode);
  // Pushes a reader overlay (menu, jump keypad). It counts toward the
  // reading full-refresh interval and uses `normal_mode` between full
  // refreshes; `quiet` redraws (background updates) use GL16 and are not
  // counted, so they never flash the panel.
  void pushReaderOverlay(bool quiet, lgfx::epd_mode_t normal_mode);
  // Draws a sample paragraph in the body font and reading style into `box`.
  void drawBodyPreview(const reader::ui::Rect& box);

  // ---- Open book ----------------------------------------------------------
  // File name of the open book.
  std::string bookTitle() const;
  bool isIndexComplete() const { return bookReader.isIndexComplete(); }
  int totalPages() const { return bookReader.getTotalPages(); }
  // Re-lays out the open book's current page (same position) after reading
  // settings changed, and reloads or rebuilds its page index.
  void refreshBookLayout();

  void drawReadingView(bool forceFullRefresh = false);
  // Draws the current page into the canvas without pushing it.
  void renderReadingView();
  bool jumpToPage(int page);
  bool jumpToPercentage(float percentage);

 private:
  bool tf_card_present_ = true;
  std::atomic<bool> background_enabled_{false};
  int currentSelection = 0;
  // Full refresh every kUiFullRefreshFrames UI frames.
  static constexpr int kUiFullRefreshFrames = 5;
  // Page turns and reader overlays; full refresh every
  // settings_.full_refresh_pages frames.
  reader::hal::RefreshCounter reading_refresh_;
  // Starts one short of the interval so the first UI frame after boot is a
  // GC16 refresh (M5.begin no longer clears the panel).
  reader::hal::RefreshCounter ui_refresh_{kUiFullRefreshFrames - 1};
  reader::core::Settings settings_;
  // Set when a reading setting that changes page breaks is modified; the
  // open book is re-laid out when the reader page is shown again.
  bool layout_dirty_ = false;
  reader::ui::SystemSettingsPage systemSettingsPage;
  reader::ui::ReadingSettingsPage readingSettingsPage;
  reader::ui::ReaderPage readerPage;
  reader::ui::ReaderMenuPage readerMenuPage;
  reader::ui::JumpPage jumpPage;
  BookshelfPage bookshelfPage;
  Page* currentPage = nullptr;
  TransferPage transferPage;
  UserGuidePage userGuidePage;
  ImagePickerPage imagePickerPage;
  // UI glyphs: the UI uses a few hundred distinct characters at 30px.
  PageFontCache uiCache{/*budget_bytes=*/512 * 1024};
  // Body glyphs: at least two pages of distinct characters, even at 60px.
  PageFontCache pageCache{/*budget_bytes=*/1536 * 1024};
  std::string body_font_requested_;
  std::string body_font_resolved_;
  LGFX_Sprite canvas;
  reader::ui::UiPainter painter_{canvas, uiCache};
  BookReader bookReader;

  void saveGlobalConfig(const std::string& book_path);
  // Pushes the reading settings to BookReader after a change.
  void applyReadingStyle();
  // Sets the canvas rotation for `currentPage` and `settings_.orientation`.
  void updateCanvasRotation();
  // Draws laid-out body `text` (glyphs already cached) at (x, y) in a
  // w x h text area, with the line guides if they are on.
  void drawBodyText(const std::string& text, int x, int y, int w, int h);
  // Body text rectangle on a canvas in the reading direction (canvas minus
  // the margins), whatever page is shown right now.
  reader::book::TextArea textArea() const;
};

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_MANAGER_H_
