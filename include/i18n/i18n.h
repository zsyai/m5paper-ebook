#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_I18N_I18N_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_I18N_I18N_H_

// Interface language.
//
// The source keeps the Simplified Chinese strings as keys: wrap a UI string in
// TR("中文") and it is returned as is in Chinese, or looked up in the current
// language's table (src/i18n/strings_<lang>.cpp). A string missing from a
// table falls back to Chinese, so a partial translation still works.
//
// To add a language: add it to Language and kLanguageNames, add a table file
// next to strings_fr.cpp and return it from Table() in i18n.cpp.

#include <cstdint>

namespace reader {
namespace i18n {

// Stored in settings.cfg (`language=`), so keep the values stable.
enum class Language : uint8_t {
  kZhHans = 0,
  kFr = 1,
};
constexpr int kLanguageCount = 2;

void SetLanguage(Language language);
Language CurrentLanguage();

// Name of `language` written in that language ("简体中文", "Français").
const char* LanguageName(Language language);

// Translation of the Chinese UI string `zh` in the current language.
const char* Translate(const char* zh);
// Same, for a string whose translation depends on where it is used (e.g.
// "删除" is "Delete" in a dialog but "Backspace" on the keypad).
const char* Translate(const char* context, const char* zh);

// Glyphs warmed into the UI font cache before a page is drawn: `zh` in
// Chinese, else the Latin letters, digits and punctuation of the language.
const char* GlyphPreload(const char* zh);

// The built-in user guide (include/ui/pages/user_guide_text.h).
const char* UserGuideText();

// Sample paragraph of the reading settings preview.
const char* PreviewSampleText();

}  // namespace i18n
}  // namespace reader

#define TR(zh) ::reader::i18n::Translate(zh)
#define TRC(context, zh) ::reader::i18n::Translate(context, zh)

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_I18N_I18N_H_
