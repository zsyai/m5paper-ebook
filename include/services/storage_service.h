#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_STORAGE_SERVICE_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_STORAGE_SERVICE_H_

// Owns SD mount state, persistent global boot/book pointers in /configs, and
// coordinated handle release across services before unmounting/remounting.

#include <functional>
#include <string>
#include <vector>

namespace reader {
namespace services {

class StorageService {
 public:
  bool MountInitial();

  bool card_present() const { return card_present_; }
  void set_card_present(bool present) { card_present_ = present; }

  // Registers a callback invoked before `UnmountSd()` during card recovery so
  // open font, book, or future EPUB cache descriptors close in one pass.
  void OnUnmount(std::function<void()> hook);

  // Consumes a pending SD error flag and returns true if the card stopped
  // answering and must be remounted.
  bool ShouldRemountOnError();

  // Runs all `OnUnmount` hooks, unmounts, and mounts the SD card again.
  bool Remount();

  std::string LoadLastReadBook() const;
  void SaveLastReadBook(const std::string& book_path);

  void SetBootToReader(bool enable);
  bool CheckBootToReader() const;

 private:
  bool card_present_ = true;
  std::vector<std::function<void()>> on_unmount_;
};

}  // namespace services
}  // namespace reader

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_SERVICES_STORAGE_SERVICE_H_
