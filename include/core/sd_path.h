#pragma once

// Where the SD card is mounted in the POSIX VFS.
//
// Code addresses card files by their SD path ("/books/x.txt"); SdPath()
// prepends the mount point. On the device that is always "/sd". Host tests
// set READER_SD_ROOT to a scratch directory so the same code reads and
// writes there.

#include <string>

namespace reader {
namespace core {

const std::string& SdRoot();
inline std::string SdPath(const std::string& path) { return SdRoot() + path; }

}  // namespace core
}  // namespace reader
