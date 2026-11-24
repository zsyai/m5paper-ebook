#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_VERSION_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_VERSION_H_

// Firmware release version, shown in System Settings. tools/package_release.py
// reads this line to name the merged release image, so keep the format.

namespace reader {
namespace core {

constexpr char kFirmwareVersion[] = "1.2.0";

}  // namespace core
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_CORE_VERSION_H_
