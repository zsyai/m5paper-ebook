#include "core/sd_path.h"

#if READER_HOST_TEST
#include <cstdlib>
#endif

namespace reader {
namespace core {

const std::string& SdRoot() {
#if READER_HOST_TEST
  // Re-read every time so a test can point at its own scratch directory.
  static std::string root;
  const char* env = std::getenv("READER_SD_ROOT");
  root = env && *env ? env : "/sd";
#else
  static const std::string root = "/sd";
#endif
  return root;
}

}  // namespace core
}  // namespace reader
