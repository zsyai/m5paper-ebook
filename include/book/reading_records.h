#pragma once

// Per-book files in /configs/: "<book file>.cfg" (position, bookmarks) and
// "<book file>.idx" (page index), named after a file in /books/.

namespace reader {
namespace book {

// Deletes the .cfg / .idx files of books no longer in /books/. Returns the
// number of files deleted.
int RemoveExpiredReadingRecords();

}  // namespace book
}  // namespace reader
