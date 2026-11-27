#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_I18N_TRANSLATION_TABLE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_I18N_TRANSLATION_TABLE_H_

// One table per language, defined in src/i18n/strings_<lang>.cpp.

#include <cstddef>

namespace reader {
namespace i18n {

struct TranslationEntry {
  const char* context;  // nullptr for a string used the same way everywhere.
  const char* zh;
  const char* text;
};

struct LanguageTable {
  const TranslationEntry* entries;
  size_t count;
  const char* glyph_preload;
  const char* user_guide;
  const char* preview_sample;
};

extern const LanguageTable kFrTable;

}  // namespace i18n
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_I18N_TRANSLATION_TABLE_H_
