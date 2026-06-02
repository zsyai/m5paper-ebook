#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_MANAGER_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_MANAGER_H_

#include "bookshelf.h"
#include "continue_page.h"
#include "setting_page.h"
#include "transfer_page.h"
#include "user_guide_page.h"
#include "font_buffer.h"
#include <M5GFX.h>
#include <SD.h>
#include "book_reader.h"
#include "font_manager.h"

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

  // Getters and Setters for state machine integration
  int getSelection() const { return currentSelection; }
  void setSelection(int sel) { currentSelection = sel; }
  void resetMenuUpdateCount() { menuUpdateCount = 0; }
  
  std::string getLastReadBook();
  void drawCachedString(const std::string& text, int x, int y);
  void showPromptBox(const std::string& text);
  int getStringWidth(const std::string& text);
  void buildFontCache(const std::string& text) {
    static std::string last_requested = "";
    static std::string cached_resolved = "";
    
    std::string requested = bookReader.getFontPath();
    
    if (requested != last_requested) {
        last_requested = requested;
        cached_resolved = FontManager::resolveFontPath(requested);
        
        if (cached_resolved != "builtin" && cached_resolved != requested) {
            bookReader.setFontPath(cached_resolved);
            bookReader.saveGlobalConfig();
            last_requested = cached_resolved;
        }
    }
    
    bool success = pageCache.build(text, cached_resolved);
    if (!success && cached_resolved != "builtin") {
        Serial.println("Font load failed, retrying resolution...");
        cached_resolved = FontManager::resolveFontPath(requested);
        if (cached_resolved != "builtin" && cached_resolved != requested) {
            bookReader.setFontPath(cached_resolved);
            bookReader.saveGlobalConfig();
            last_requested = cached_resolved;
        }
        pageCache.build(text, cached_resolved);
    }
  }
  void clearFontCache() {
    pageCache.clear();
  }
  std::string getFontPath() const { return bookReader.getFontPath(); }
  void setFontPath(const std::string& path) { bookReader.setFontPath(path); }
  int getFontSize() const { return pageCache.getFontSize(); }
  std::string getFontName() const { return pageCache.getFontName(); }
  bool isUsingBuiltinFont() const { return pageCache.isUsingBuiltin(); }
  int getHeaderHeight() const { return getFontSize() * 2.5; }
  int getFooterHeight() const { return getFontSize() * 3.0; }
  float getLineSpacingFactor() const { return bookReader.getLineSpacingFactor(); }
  void setLineSpacingFactor(float factor) { bookReader.setLineSpacingFactor(factor); }
  bool loadGlobalConfig() { return bookReader.loadGlobalConfig(); }
  bool saveGlobalConfig() { return bookReader.saveGlobalConfig(); }
  void saveCurrentBookConfig();
  bool isLockScreenEnabled() const { return bookReader.isLockScreenEnabled(); }
  void setLockScreenEnabled(bool enable) { bookReader.setLockScreenEnabled(enable); }
  std::string getLockScreenPath() const { return bookReader.getLockScreenPath(); }
  void setLockScreenPath(const std::string& path) { bookReader.setLockScreenPath(path); }
  void setBootToReaderFlag(bool enable);
  bool checkBootToReaderFlag();
  void enterDeepSleep(bool from_reader);
  bool isReadingUserGuide() const { return bookReader.getBookPath() == "/user_guide.txt"; }

  void setPage(Page* page);
  void drawCurrentPage();
  Page* getCurrentPage() { return currentPage; }
  void setPageByState(State state);
  bool checkUiFullRefresh() {
    uiOpCount++;
    if (uiOpCount >= 5) {
      uiOpCount = 0;
      return true;
    }
    return false;
  }
  void drawBottomBar(const std::vector<std::string>& items,
                     int selected_idx = -1);
  void drawBattery(LGFX_Sprite& canvas, int right_x, int y);

  // Temp for pages that are not refactored yet
  void drawContinuePage() { continuePage.draw(); }

  void drawReadingView(bool forceFullRefresh = false);
  void drawReaderMenu();
  bool jumpToPage(int page);
  bool jumpToPercentage(float percentage);
  void drawReaderJumpMenu(const std::string& input);
  std::string jumpInputText;

 private:
  bool loadFont();
  bool fontLoaded = false;
  int currentSelection = 0;
  int pageTurnCount = 0;
  int menuUpdateCount = 0;
  int uiOpCount = 0;
  SettingPage settingPage;
  BookshelfPage bookshelfPage;
  Page* currentPage = nullptr;
  ContinuePage continuePage;
  TransferPage transferPage;
  UserGuidePage userGuidePage;
  ImagePickerPage imagePickerPage;
  PageFontCache pageCache;
  LGFX_Sprite canvas;
  BookReader bookReader;

  void saveGlobalConfig(const std::string& book_path);
};

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_MANAGER_H_
