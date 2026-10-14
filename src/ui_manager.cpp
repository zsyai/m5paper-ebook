#include "ui_manager.h"
#include "state_machine.h"
#include "utf8_utils.h"
#include "core/perf_trace.h"
#include "book/page_layout.h"
#include "fonts/font_cache_store.h"
#include "hal/display.h"
#include "hal/input.h"
#include "ui/text_renderer.h"
#include "ui/widgets/bars.h"

#include <M5Unified.h>
#include <SD.h>
#include <esp_sleep.h>
#include <cstring>

void UIManager::init() {
  PERF_SCOPE("UIManager::init");
  settings_.Load();
  applyReadingStyle();
  painter_.set_font_path(getUiFontPath());
  uiCache.setBuiltinFallbackOnly(true);
  uiCache.setTtfPixelSize(kUiFontPixelSize);
  pageCache.setTtfPixelSize(settings_.body_font_size);
  
  canvas.setPsram(true);
  canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  {
    PERF_SCOPE("canvas.createSprite");
    canvas.createSprite(M5.Display.width(), M5.Display.height());
  }
  updateCanvasRotation();

  M5.Display.setEpdMode(lgfx::epd_mode_t::epd_text);
}

void UIManager::openBook(const std::string& path, bool show_loading) {
  PERF_SCOPE("UIManager::openBook");
  size_t last_slash = path.find_last_of('/');
  std::string filename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
  
  if (show_loading) {
      const int max_text_w = canvas.width() - 80; // Box padding 40, Screen padding 40
      const std::string prefix = "正在打开 ";
      buildFontCache(prefix + filename + "...");
      showPromptBox(prefix + reader::ui::Ellipsize(
                                 uiCache, filename,
                                 max_text_w - getStringWidth(prefix)));
  }
  // Without a prompt (boot restore) the page itself is the first frame; it
  // is drawn with a full GC16 refresh (reading_refresh_ restarted), so no separate
  // blank frame (~0.7 s of push + refresh) is needed.

  if (bookReader.open(path)) {
    reading_refresh_.Restart();
    layout_dirty_ = false;
    resolvedBodyFontPath();  // persist a replacement if the font is gone
    
    // Load config first to get old position
    bookReader.loadConfig(path);
    saveGlobalConfig(path);
    
    // Lay out the saved page. A stored page index is loaded here (a few
    // ms); a missing or stale one is built in the background
    // (backgroundStep()). The reader page draws the page when it is shown.
    const reader::book::TextArea area = textArea();
    bookReader.readCurrentPage(pageCache, area.w, area.h);
    bookReader.prepareIndex(pageCache, area.w, area.h);
    PERF_VALUE("openBook.index_ready", bookReader.isIndexComplete());
  }
}

void UIManager::refreshBookLayout() {
  if (!layout_dirty_) return;
  layout_dirty_ = false;
  if (bookReader.getBookPath().empty()) return;
  resolvedBodyFontPath();
  const reader::book::TextArea area = textArea();
  bookReader.readCurrentPage(pageCache, area.w, area.h);
  bookReader.prepareIndex(pageCache, area.w, area.h);
}

bool UIManager::hasBackgroundWork() const {
  if (!background_enabled_) return false;
  return bookReader.hasPendingIndex() || pageCache.needsFontCacheBuild() ||
         uiCache.needsFontCacheBuild();
}

void UIManager::backgroundStep() {
  // Page index first (page numbers), then the body and UI TTF glyph caches.
  if (!bookReader.hasPendingIndex()) {
    if (!pageCache.stepFontCacheBuild(kBackgroundStepMs)) {
      uiCache.stepFontCacheBuild(kBackgroundStepMs);
    }
    return;
  }
  if (!bookReader.stepIndex(pageCache, kBackgroundStepMs)) return;
  // Pages that show page numbers switch from "…" to the final numbers.
  if (currentPage) currentPage->onIndexReady();
}

void UIManager::nextPage() {
  PERF_SCOPE("UIManager::nextPage");
  reading_refresh_.Count();
  const reader::book::TextArea area = textArea();
  bookReader.nextPage(pageCache, area.w, area.h);
  drawReadingView();
}

void UIManager::prevPage() {
  PERF_SCOPE("UIManager::prevPage");
  reading_refresh_.Count();
  if (bookReader.needsIndexForPrevPage()) {
    showPromptBox("加载中...");
    bookReader.indexUntilCurrentPage(pageCache);
  }
  const reader::book::TextArea area = textArea();
  bookReader.prevPage(pageCache, area.w, area.h);
  drawReadingView();
}

void UIManager::handleInput() {
  // Handle Wheel
  if (M5.BtnA.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::WHEEL;
    msg.timestamp = millis();
    msg.data.wheel.delta = -1;
    send_message(msg);
  }
  if (M5.BtnC.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::WHEEL;
    msg.timestamp = millis();
    msg.data.wheel.delta = 1;
    send_message(msg);
  }
  if (M5.BtnB.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::SYSTEM;
    msg.timestamp = millis();
    msg.data.sys_code = 1; // Press
    send_message(msg);
  }

  // Handle Touch
  auto detail = M5.Touch.getDetail();
  if (detail.wasClicked()) {
    SystemMessage_t msg;
    msg.type = MessageType::TOUCH;
    msg.timestamp = millis();
    msg.data.touch.x = detail.x;
    msg.data.touch.y = detail.y;
    send_message(msg);
  }
}


void UIManager::drawReadingView(bool forceFullRefresh) {
  PERF_SCOPE("UIManager::drawReadingView");
  renderReadingView();
  if (forceFullRefresh || reading_refresh_.Due(settings_.full_refresh_pages)) {
    Serial.println("Full refresh triggered.");
    reading_refresh_.Restart();
    pushCanvas(lgfx::epd_mode_t::epd_quality);
  } else {
    pushCanvas(lgfx::epd_mode_t::epd_text);
  }
}

void UIManager::renderReadingView() {
  canvas.fillScreen(TFT_WHITE);
  
  const reader::book::TextArea area = textArea();
  std::string text = bookReader.getPageText();
  if (text.empty()) {
    buildFontCache("文件为空或读取失败");
    drawCachedString("文件为空或读取失败", area.x, area.y);
  } else {
    // Same path as BookReader::paginate, so the body font is not reopened.
    const reader::book::ReadingStyle& style = bookReader.style();
    pageCache.build(text, style.font_path);
    drawBodyText(text, area.x, area.y, area.w, area.h);
  }
}

void UIManager::drawBodyText(const std::string& text, int x, int y, int w,
                             int h) {
  const reader::book::ReadingStyle& style = bookReader.style();
  const int fs = pageCache.getFontSize();
  const int line_h = style.LineHeight(fs);
  reader::ui::DrawText(canvas, pageCache, text, x, y, line_h);
  if (style.line_underline) {
    reader::ui::DrawLineGuides(canvas, x, y, w, h, fs, line_h);
  }
}

void UIManager::pushReaderOverlay(bool quiet, lgfx::epd_mode_t normal_mode) {
  lgfx::epd_mode_t mode = lgfx::epd_mode_t::epd_text;
  if (!quiet) {
    mode = reading_refresh_.CountFrame(settings_.full_refresh_pages)
               ? lgfx::epd_mode_t::epd_quality
               : normal_mode;
  }
  pushCanvas(mode);
}

std::string UIManager::bookTitle() const {
  const std::string path = bookReader.getBookPath();
  const size_t slash = path.find_last_of('/');
  return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

void UIManager::drawBodyPreview(const reader::ui::Rect& box) {
  PERF_SCOPE("UIManager::drawBodyPreview");
  // Two paragraphs without leading spaces, so turning the paragraph indent
  // on shows a visible change.
  static const char kSample[] =
      "滚滚长江东逝水，浪花淘尽英雄。是非成败转头空。青山依旧在，几度夕阳红。"
      "白发渔樵江渚上，惯看秋月春风。一壶浊酒喜相逢。古今多少事，都付笑谈中。\n"
      "话说天下大势，分久必合，合久必分。周末七国分争，并入于秦。及秦灭之后，"
      "楚、汉分争，又并入于汉。汉朝自高祖斩白蛇而起义，一统天下，后来光武中兴，"
      "传至献帝，遂分为三国。";
  const reader::book::ReadingStyle& style = bookReader.style();
  // Opens the body font; layout only needs its glyph index.
  pageCache.build(std::string(), style.font_path);
  reader::book::LayoutParams layout;
  layout.text_w = box.w;
  layout.text_h = box.h;
  layout.font_size = pageCache.getFontSize();
  layout.line_height = style.LineHeight(layout.font_size);
  layout.paragraph_indent = style.paragraph_indent;
  reader::book::LayoutCursor cursor;
  cursor.paragraph_start = true;
  bool filled = false;
  std::string text;
  reader::book::MeasurePage(reinterpret_cast<const uint8_t*>(kSample),
                            sizeof(kSample) - 1, layout, pageCache, &cursor,
                            &filled, &text);
  pageCache.build(text, style.font_path);
  drawBodyText(text, box.x, box.y, box.w, box.h);
}

std::string UIManager::progressText() const {
  // Page numbers are final only once the index is complete. While it is
  // being built: "N/…" when the current page is already indexed, "…"
  // otherwise, and the percentage is by byte offset.
  char buf[40];
  int page = bookReader.getCurrentPageIndex();
  float progress = bookReader.getProgress();
  if (bookReader.isIndexComplete()) {
    snprintf(buf, sizeof(buf), "%d/%d  %.1f%%", page + 1,
             bookReader.getTotalPages(), progress);
  } else if (page >= 0) {
    snprintf(buf, sizeof(buf), "%d/…  %.1f%%", page + 1, progress);
  } else {
    snprintf(buf, sizeof(buf), "…  %.1f%%", progress);
  }
  return buf;
}

bool UIManager::jumpToPage(int page) {
    if (!bookReader.isIndexComplete() && page >= bookReader.getTotalPages()) {
        showPromptBox("加载中...");
        bookReader.indexUntilPageCount(pageCache, page + 1);
    }
    const reader::book::TextArea area = textArea();
    return bookReader.jumpToPage(page, pageCache, area.w, area.h);
}

bool UIManager::jumpToPercentage(float percentage) {
    if (!bookReader.isIndexComplete()) {
        showPromptBox("加载中...");
        bookReader.finishIndex(pageCache);
    }
    const reader::book::TextArea area = textArea();
    return bookReader.jumpToPercentage(percentage, pageCache, area.w, area.h);
}

void UIManager::applyReadingStyle() {
  bookReader.setStyle(reader::book::ReadingStyle::FromSettings(settings_));
}

reader::book::TextArea UIManager::textArea() const {
  // The canvas may be rotated for another page; use the panel size turned
  // to the reading direction.
  const bool canvas_landscape = canvas.getRotation() & 1;
  const int panel_w = canvas_landscape ? canvas.height() : canvas.width();
  const int panel_h = canvas_landscape ? canvas.width() : canvas.height();
  const bool landscape = static_cast<int>(bookReader.style().orientation) & 1;
  return bookReader.style().Area(landscape ? panel_h : panel_w,
                                 landscape ? panel_w : panel_h);
}

std::string UIManager::resolvedBodyFontPath() {
  const std::string requested = settings_.body_font_path;
  if (requested == body_font_requested_) return body_font_resolved_;
  body_font_resolved_ = FontManager::resolveFontPath(requested);
  if (body_font_resolved_ != "builtin" && body_font_resolved_ != requested) {
    // The configured font is gone; remember the replacement.
    settings_.body_font_path = body_font_resolved_;
    settings_.Save();
    applyReadingStyle();
  }
  body_font_requested_ = settings_.body_font_path;
  return body_font_resolved_;
}

std::vector<std::string> UIManager::fontCachesInUse() const {
  // Open fonts may still differ from the settings until the next draw, and
  // their caches may be half written, so both count.
  std::vector<std::string> paths = {uiCache.fontCachePath(),
                                    pageCache.fontCachePath()};
  if (PageFontCache::IsTtfPath(getUiFontPath())) {
    paths.push_back(
        reader::fonts::FontCachePath(getUiFontPath(), kUiFontPixelSize));
  }
  if (isBodyFontScalable()) {
    paths.push_back(reader::fonts::FontCachePath(settings_.body_font_path,
                                                 settings_.body_font_size));
  }
  return paths;
}

void UIManager::setFontPath(const std::string& path) {
  settings_.body_font_path = path;
  applyReadingStyle();
  pageCache.clear();
  body_font_requested_.clear();
  layout_dirty_ = true;
}

void UIManager::setUiFontPath(const std::string& path) {
  settings_.ui_font_path = path;
  painter_.set_font_path(getUiFontPath());
  uiCache.clear();
}

void UIManager::setBodyFontSize(int size) {
  settings_.body_font_size = reader::core::ClampBodyFontSize(size);
  pageCache.setTtfPixelSize(settings_.body_font_size);
  layout_dirty_ = true;
}

void UIManager::setLineSpacingFactor(float factor) {
  settings_.line_spacing = reader::core::ClampLineSpacing(factor);
  applyReadingStyle();
  layout_dirty_ = true;
}

void UIManager::setMargin(int margin) {
  settings_.margin = reader::core::ClampMargin(margin);
  applyReadingStyle();
  layout_dirty_ = true;
}

void UIManager::setParagraphIndent(bool on) {
  settings_.paragraph_indent = on;
  applyReadingStyle();
  layout_dirty_ = true;
}

void UIManager::setLineUnderline(bool on) {
  // Drawing only: page breaks stay the same.
  settings_.line_underline = on;
  applyReadingStyle();
}

void UIManager::setOrientation(reader::core::Orientation orientation) {
  settings_.orientation = orientation;
  applyReadingStyle();
  updateCanvasRotation();
  layout_dirty_ = true;
}

void UIManager::setFullRefreshPages(int pages) {
  settings_.full_refresh_pages = reader::core::ClampFullRefreshPages(pages);
}

std::string UIManager::getLastReadBook() {
    if (!SD.exists("/configs/global.cfg")) {
        return "";
    }
    File file = SD.open("/configs/global.cfg", FILE_READ);
    if (!file) {
        return "";
    }
    String path = file.readStringUntil('\n');
    path.trim();
    file.close();
    return path.c_str();
}

void UIManager::saveGlobalConfig(const std::string& book_path) {
    PERF_SCOPE("UIManager::saveGlobalConfig");
    if (SD.exists("/configs/global.cfg")) {
        SD.remove("/configs/global.cfg");
    }
    File file = SD.open("/configs/global.cfg", FILE_WRITE);
    if (file) {
        file.println(book_path.c_str());
        file.close();
    }
}

void UIManager::showPromptBox(const std::string& text) {
  PERF_SCOPE("UIManager::showPromptBox");
  reader::ui::DrawPromptBox(painter_, text);
  pushCanvas(lgfx::epd_mode_t::epd_fast);
}

void UIManager::saveCurrentBookConfig() {
    bookReader.saveConfig();
}

void UIManager::setBootToReaderFlag(bool enable) {
    const char* flag_path = "/configs/boot_to_reader.flag";
    if (enable) {
        File file = SD.open(flag_path, FILE_WRITE);
        if (file) {
            file.println("1");
            file.close();
        }
    } else {
        if (SD.exists(flag_path)) {
            SD.remove(flag_path);
        }
    }
}

bool UIManager::checkBootToReaderFlag() {
    return SD.exists("/configs/boot_to_reader.flag");
}

void UIManager::enterDeepSleep(bool from_reader) {
  canvas.setRotation(reader::hal::SystemRotation(settings_.orientation));
  canvas.fillScreen(TFT_WHITE);
  
  bool lock_enabled = settings_.lock_screen_enabled;
  std::string lock_path = settings_.lock_screen_path;
  bool drawn = false;
  
  if (lock_enabled && !lock_path.empty() && SD.exists(lock_path.c_str())) {
      File f = SD.open(lock_path.c_str(), FILE_READ);
      if (f) {
          MyFileDataWrapper wrapper(f);
          if (lock_path.find(".png") != std::string::npos || lock_path.find(".PNG") != std::string::npos) {
              drawn = canvas.drawPng(&wrapper, 0, 0);
          } else {
              drawn = canvas.drawJpg(&wrapper, 0, 0);
          }
          f.close();
      }
  }
  
  if (!drawn) {
      // 1. Build Cache for Chinese sleep prompts
      std::string sleep_chars = "系统已进入休眠状态"
                                "向内长按滚轮键3秒即可唤醒";
      buildFontCache(sleep_chars);
      
      int font_size = getFontSize();
      int screen_w = canvas.width();
      int screen_h = canvas.height();
      
      // 2. Draw Prompts
      std::string prompt1 = "系统已进入休眠";
      int txt_w = getStringWidth(prompt1);
      int prompt1_y = screen_h / 2 - font_size;
      drawCachedString(prompt1, screen_w / 2 - txt_w / 2, prompt1_y);
      
      std::string prompt2 = "向内长按滚轮键3秒即可唤醒";
      txt_w = getStringWidth(prompt2);
      int prompt2_y = prompt1_y + 2 * font_size;
      drawCachedString(prompt2, screen_w / 2 - txt_w / 2, prompt2_y);
  }
  
  // 4. Push Sprite with high quality mode to clear ghosting
  pushCanvas(lgfx::epd_mode_t::epd_quality);
  
  // 5. Setup sleep hardware
  esp_sleep_enable_ext0_wakeup((gpio_num_t)38, 0);
  // Close half-written font cache files; the build restarts after wakeup.
  pageCache.cancelFontCacheBuild();
  uiCache.cancelFontCacheBuild();
  setBootToReaderFlag(from_reader);
  if (from_reader) {
      saveCurrentBookConfig();
  }
  
  delay(500);
  // Sleep through ESP-IDF directly instead of M5.Power.deepSleep(): that call
  // also sends the IT8951 its SLEEP command, so a wakeup would find the panel
  // in a different state than a reset-button reboot does. E-ink keeps the
  // image without it.
  M5.Display.waitDisplay();
  constexpr gpio_num_t kWheelPushPin = GPIO_NUM_38;
  while (digitalRead(kWheelPushPin) == LOW) {
    delay(10);  // A held wheel would retrigger the ext0 wakeup at once.
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  log_i("[sleep] deep sleep, wakeup on GPIO%d low", (int)kWheelPushPin);
  esp_deep_sleep_start();
}

void UIManager::updateCanvasRotation() {
  // Rotating the canvas only changes how later drawing is mapped; the
  // pixels already in it stay, so a portrait page can open over a
  // landscape one without redrawing it.
  const bool rotated = currentPage && currentPage->followsReadingDirection();
  canvas.setRotation(rotated
                         ? static_cast<uint8_t>(settings_.orientation)
                         : reader::hal::SystemRotation(settings_.orientation));
}

void UIManager::setPage(Page* page) {
  if (currentPage) {
    currentPage->exit();
  }
  currentPage = page;
  updateCanvasRotation();
  if (currentPage) {
    currentPage->enter();
  }
}

void UIManager::mapInputToPage(SystemMessage_t* msg) const {
  if (msg->type == MessageType::TOUCH) {
    int x = msg->data.touch.x;
    int y = msg->data.touch.y;
    reader::hal::PanelToCanvas(canvas.getRotation(), M5.Display.width(),
                               M5.Display.height(), &x, &y);
    msg->data.touch.x = x;
    msg->data.touch.y = y;
  } else if (msg->type == MessageType::WHEEL) {
    msg->data.wheel.delta = reader::hal::WheelToPageStep(
        msg->data.wheel.delta, canvas.getRotation());
  }
}

void UIManager::drawCurrentPage() {
  PERF_SCOPE("UIManager::drawCurrentPage");
  if (currentPage) currentPage->show();
}

void UIManager::pushUiFrame() {
  if (ui_refresh_.CountFrame(kUiFullRefreshFrames)) {
    Serial.println("UI full refresh triggered.");
    pushCanvas(lgfx::epd_mode_t::epd_quality);
  } else {
    pushCanvas(lgfx::epd_mode_t::epd_text);
  }
}

void UIManager::pushCanvas(lgfx::epd_mode_t mode) {
  reader::hal::PushFrame(canvas, mode);
}

void UIManager::setPageByState(State state) {
  PERF_SCOPE("UIManager::setPageByState");
  switch (state) {
    case State::BOOKSHELF:
      setPage(&bookshelfPage);
      break;
    case State::SETTING:
      setPage(&systemSettingsPage);
      break;
    case State::READING_SETTINGS:
      setPage(&readingSettingsPage);
      break;
    case State::READER:
      setPage(&readerPage);
      break;
    case State::READER_MENU:
      setPage(&readerMenuPage);
      break;
    case State::READER_JUMP_MENU:
      setPage(&jumpPage);
      break;
    case State::TRANSFER:
      setPage(&transferPage);
      break;
    case State::USER_GUIDE:
      setPage(&userGuidePage);
      break;
    case State::IMAGE_PICKER:
      setPage(&imagePickerPage);
      break;
    default:
      setPage(nullptr);
      break;
  }
}

void UIManager::drawHeader(const std::string& title) {
  reader::ui::DrawHeader(painter_, title, getHeaderHeight(),
                         M5.Power.getBatteryLevel());
}

void UIManager::drawBottomBar(const std::vector<std::string>& items) {
  reader::ui::DrawBottomBar(painter_, items, getFooterHeight());
}

void UIManager::drawBattery(int right_x, int y) {
  reader::ui::DrawBattery(painter_, right_x, y, M5.Power.getBatteryLevel());
}
