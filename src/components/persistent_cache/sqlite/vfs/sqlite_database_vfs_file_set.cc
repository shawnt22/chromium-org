// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/persistent_cache/sqlite/vfs/sqlite_database_vfs_file_set.h"

#include <atomic>

#include "base/files/file_path.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "components/persistent_cache/sqlite/vfs/sandboxed_file.h"

namespace {

std::atomic<uint64_t> g_file_set_id_generator(0);
constexpr const char kPathSeperator[] = "_";

}  // namespace

namespace persistent_cache {

SqliteVfsFileSet::SqliteVfsFileSet(std::unique_ptr<SandboxedFile> db_file,
                                   std::unique_ptr<SandboxedFile> journal_file)
    : db_file_(std::move(db_file)),
      journal_file_(std::move(journal_file)),
      virtual_fs_path_(base::NumberToString(
          g_file_set_id_generator.fetch_add(1, std::memory_order_relaxed))) {}

SqliteVfsFileSet::SqliteVfsFileSet(SqliteVfsFileSet&& other) = default;
SqliteVfsFileSet& SqliteVfsFileSet::operator=(SqliteVfsFileSet&& other) =
    default;
SqliteVfsFileSet::~SqliteVfsFileSet() = default;

base::FilePath SqliteVfsFileSet::GetDbVirtualFilePath() const {
  constexpr const char kDbFileName[] = "data.db";
  return base::FilePath::FromASCII(
      base::StrCat({virtual_fs_path_, kPathSeperator, kDbFileName}));
}

base::FilePath SqliteVfsFileSet::GetJournalVirtualFilePath() const {
  constexpr const char kJournalFileName[] = "data.db-journal";
  return base::FilePath::FromASCII(
      base::StrCat({virtual_fs_path_, kPathSeperator, kJournalFileName}));
}

std::array<std::pair<base::FilePath, raw_ptr<SandboxedFile>>, 2>
SqliteVfsFileSet::GetFiles() const {
  return {std::make_pair(GetDbVirtualFilePath(), db_file_.get()),
          std::make_pair(GetJournalVirtualFilePath(), journal_file_.get())};
}

}  // namespace persistent_cache
