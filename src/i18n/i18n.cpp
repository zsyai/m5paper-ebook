#include "i18n/i18n.h"

#include <cstring>

#include "i18n/translation_table.h"
#include "ui/pages/user_guide_text.h"

namespace reader {
namespace i18n {

namespace {

Language g_language = Language::kZhHans;

constexpr const char* kLanguageNames[kLanguageCount] = {"简体中文", "Français"};

constexpr char kZhPreviewSample[] =
    "滚滚长江东逝水，浪花淘尽英雄。是非成败转头空。青山依旧在，几度夕阳红。"
    "白发渔樵江渚上，惯看秋月春风。一壶浊酒喜相逢。古今多少事，都付笑谈中。\n"
    "话说天下大势，分久必合，合久必分。周末七国分争，并入于秦。及秦灭之后，"
    "楚、汉分争，又并入于汉。汉朝自高祖斩白蛇而起义，一统天下，后来光武中兴，"
    "传至献帝，遂分为三国。";

// nullptr for Chinese, the source language.
const LanguageTable* Table() {
  switch (g_language) {
    case Language::kFr:
      return &kFrTable;
    case Language::kZhHans:
      break;
  }
  return nullptr;
}

const char* Lookup(const char* context, const char* zh) {
  const LanguageTable* table = Table();
  if (table == nullptr || zh == nullptr) return zh;
  const char* fallback = nullptr;
  for (size_t i = 0; i < table->count; ++i) {
    const TranslationEntry& e = table->entries[i];
    if (std::strcmp(e.zh, zh) != 0) continue;
    if (e.context == nullptr) {
      if (context == nullptr) return e.text;
      fallback = e.text;
    } else if (context != nullptr && std::strcmp(e.context, context) == 0) {
      return e.text;
    }
  }
  return fallback != nullptr ? fallback : zh;
}

}  // namespace

void SetLanguage(Language language) {
  const int value = static_cast<int>(language);
  g_language = value >= 0 && value < kLanguageCount ? language
                                                    : Language::kZhHans;
}

Language CurrentLanguage() { return g_language; }

const char* LanguageName(Language language) {
  const int value = static_cast<int>(language);
  return kLanguageNames[value >= 0 && value < kLanguageCount ? value : 0];
}

const char* Translate(const char* zh) { return Lookup(nullptr, zh); }

const char* Translate(const char* context, const char* zh) {
  return Lookup(context, zh);
}

const char* GlyphPreload(const char* zh) {
  const LanguageTable* table = Table();
  return table == nullptr ? zh : table->glyph_preload;
}

const char* UserGuideText() {
  const LanguageTable* table = Table();
  return table == nullptr ? kUserGuideTextZhHans : table->user_guide;
}

const char* PreviewSampleText() {
  const LanguageTable* table = Table();
  return table == nullptr ? kZhPreviewSample : table->preview_sample;
}

}  // namespace i18n
}  // namespace reader
