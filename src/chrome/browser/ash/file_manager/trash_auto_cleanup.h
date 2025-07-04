// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_FILE_MANAGER_TRASH_AUTO_CLEANUP_H_
#define CHROME_BROWSER_ASH_FILE_MANAGER_TRASH_AUTO_CLEANUP_H_

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ash/file_manager/trash_common_util.h"
#include "chrome/browser/ash/file_manager/trash_info_validator.h"
#include "chrome/browser/profiles/profile.h"

namespace file_manager::trash {

inline constexpr base::TimeDelta kCleanupInterval = base::Days(1);
inline constexpr base::TimeDelta kCleanupCheckInterval = base::Hours(1);
inline constexpr base::TimeDelta kMaxTrashAge = base::Days(30);
inline constexpr int kMaxBatchSize = 500;

// List of UMA enum values for the errors encountered during the auto cleanup
// process. The enum values must be kept in sync with TrashAutoCleanupError in
// tools/metrics/histograms/metadata/file/enums.xml.
enum class AutoCleanupError {
  kSuccessfullyDeleted = 0,
  kInvalidTrashInfoFile = 1,
  kFailedToGetTrashInfoFileModifiedTime = 2,
  kFailedToParseTrashInfoFile = 3,
  kFailedToDeleteTrashFile = 4,
  kMaxValue = kFailedToDeleteTrashFile,
};

// Used for tests to provide the outcome of a cleanup iteration.
enum class AutoCleanupResult {
  kWaitingForNextCleanupIteration = 0,
  kNoOldFilesToCleanup,
  kTrashInfoParsingError,
  kDeletionError,
  kCleanupSuccessful,
};

// Handles the 30-day Trash files autocleanup.
class TrashAutoCleanup {
 public:
  ~TrashAutoCleanup();

  TrashAutoCleanup(const TrashAutoCleanup&) = delete;
  TrashAutoCleanup& operator=(const TrashAutoCleanup&) = delete;

  static std::unique_ptr<TrashAutoCleanup> Create(Profile* profile);

 private:
  friend class TrashAutoCleanupTest;

  explicit TrashAutoCleanup(Profile* profile);

  void Init();
  void StartCleanup();
  void OnTrashInfoFilesToDeleteEnumerated(
      const std::vector<base::FilePath>& trash_info_paths_to_delete);
  void OnTrashInfoFilesParsed(
      std::vector<file_manager::trash::ParsedTrashInfoDataOrError>
          parsed_data_or_error);
  void OnCleanupDone(bool success);

  void SetCleanupDoneCallbackForTest(
      base::OnceCallback<void(AutoCleanupResult result)> cleanup_done_closure);

  raw_ptr<Profile> profile_;
  std::unique_ptr<file_manager::trash::TrashInfoValidator> validator_ = nullptr;
  std::vector<base::FilePath> trash_info_directories_;
  base::RepeatingTimer cleanup_repeating_timer_;
  base::Time last_cleanup_time_;
  base::TimeTicks cleanup_start_time_;
  base::OnceCallback<void(AutoCleanupResult result)>
      cleanup_done_closure_for_test_;

  base::WeakPtrFactory<TrashAutoCleanup> weak_ptr_factory_{this};
};

}  // namespace file_manager::trash

#endif  // CHROME_BROWSER_ASH_FILE_MANAGER_TRASH_AUTO_CLEANUP_H_
