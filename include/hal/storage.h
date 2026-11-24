#pragma once

// SD card mount and failure detection.
//
// There is no card-detect pin and no polling: code that reads or writes the
// card reports unexpected failures with ReportSdError(), which becomes an
// app::EventType::kSdError. `StorageService` / `App` then probes the card and
// remounts it,
// or tells the user to insert it.

namespace reader {
namespace hal {

// Mounts the card at /sd (SPI, 25 MHz) and creates /configs. Expects
// SPI.begin() to have been called with the SD pins.
bool MountSd();
void UnmountSd();

// True if the card answers a raw sector read through the current mount.
// After the card was removed, or removed and inserted again, it does not:
// a reinserted card needs MountSd() to be initialized again.
bool SdResponds();

// Called by SD readers and writers when an operation fails in a way that
// can mean the card is gone. Safe from any task. The first report after a
// TakeSdError() also calls the listener (the app posts an event from it);
// repeats until then are coalesced.
void ReportSdError();
// Returns whether an error was reported since the last call, and clears it.
bool TakeSdError();
// Whether an error is reported and not yet taken (does not clear it).
bool SdErrorPending();
void SetSdErrorListener(void (*listener)());

#if READER_PERF && !READER_HOST_TEST
// Enables post-boot single-task SD access verification (`state_machine` task,
// plus `webserver_task` while on `State::TRANSFER`).
void ArmSdTaskInvariant(bool armed);
void CheckSdTaskInvariant(const char* op);
#else
inline void ArmSdTaskInvariant(bool) {}
inline void CheckSdTaskInvariant(const char*) {}
#endif

}  // namespace hal
}  // namespace reader
