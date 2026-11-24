#include "services/storage_service.h"

#include <SD.h>

#include <utility>

#include "core/perf_trace.h"
#include "hal/storage.h"

namespace reader {
namespace services {
namespace {

constexpr char kGlobalCfgPath[] = "/configs/global.cfg";
constexpr char kBootToReaderFlagPath[] = "/configs/boot_to_reader.flag";

}  // namespace

bool StorageService::MountInitial() {
  card_present_ = hal::MountSd();
  return card_present_;
}

void StorageService::OnUnmount(std::function<void()> hook) {
  on_unmount_.push_back(std::move(hook));
}

bool StorageService::ShouldRemountOnError() {
  hal::TakeSdError();
  if (!card_present_) return false;
  // Missing files or full cards also raise SD errors; only remount when the
  // card itself stopped answering sector reads.
  return !hal::SdResponds();
}

bool StorageService::Remount() {
  PERF_SCOPE("StorageService::Remount");
  for (const auto& hook : on_unmount_) {
    hook();
  }
  hal::UnmountSd();
  card_present_ = hal::MountSd();
  hal::TakeSdError();
  return card_present_;
}

std::string StorageService::LoadLastReadBook() const {
  if (!SD.exists(kGlobalCfgPath)) return "";
  File file = SD.open(kGlobalCfgPath, FILE_READ);
  if (!file) return "";
  String path = file.readStringUntil('\n');
  path.trim();
  file.close();
  return path.c_str();
}

void StorageService::SaveLastReadBook(const std::string& book_path) {
  PERF_SCOPE("StorageService::SaveLastReadBook");
  if (SD.exists(kGlobalCfgPath)) {
    SD.remove(kGlobalCfgPath);
  }
  File file = SD.open(kGlobalCfgPath, FILE_WRITE);
  if (!file) {
    hal::ReportSdError();
    return;
  }
  file.println(book_path.c_str());
  file.close();
}

void StorageService::SetBootToReader(bool enable) {
  if (enable) {
    File file = SD.open(kBootToReaderFlagPath, FILE_WRITE);
    if (file) {
      file.println("1");
      file.close();
    }
  } else if (SD.exists(kBootToReaderFlagPath)) {
    SD.remove(kBootToReaderFlagPath);
  }
}

bool StorageService::CheckBootToReader() const {
  return SD.exists(kBootToReaderFlagPath);
}

}  // namespace services
}  // namespace reader
