// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "base/files/file.h"

#include <stdint.h>

#include <array>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/perfetto/include/perfetto/test/traced_value_test_support.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/environment.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/gtest_util.h"
#endif

namespace base {

TEST(FileTest, Create) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("create_file_1");

  {
    // Don't create a File at all.
    File file;
    EXPECT_FALSE(file.IsValid());
    EXPECT_EQ(File::FILE_ERROR_FAILED, file.error_details());

    File file2(File::FILE_ERROR_TOO_MANY_OPENED);
    EXPECT_FALSE(file2.IsValid());
    EXPECT_EQ(File::FILE_ERROR_TOO_MANY_OPENED, file2.error_details());
  }

  {
    // Open a file that doesn't exist.
    File file(file_path, File::FLAG_OPEN | File::FLAG_READ);
    EXPECT_FALSE(file.IsValid());
    EXPECT_EQ(File::FILE_ERROR_NOT_FOUND, file.error_details());
    EXPECT_EQ(File::FILE_ERROR_NOT_FOUND, File::GetLastFileError());
  }

  {
    // Open or create a file.
    File file(file_path, File::FLAG_OPEN_ALWAYS | File::FLAG_READ);
    EXPECT_TRUE(file.IsValid());
    EXPECT_TRUE(file.created());
    EXPECT_EQ(File::FILE_OK, file.error_details());
  }

  {
    // Open an existing file.
    File file(file_path, File::FLAG_OPEN | File::FLAG_READ);
    EXPECT_TRUE(file.IsValid());
    EXPECT_FALSE(file.created());
    EXPECT_EQ(File::FILE_OK, file.error_details());

    // This time verify closing the file.
    file.Close();
    EXPECT_FALSE(file.IsValid());
  }

  {
    // Open an existing file through Initialize
    File file;
    file.Initialize(file_path, File::FLAG_OPEN | File::FLAG_READ);
    EXPECT_TRUE(file.IsValid());
    EXPECT_FALSE(file.created());
    EXPECT_EQ(File::FILE_OK, file.error_details());

    // This time verify closing the file.
    file.Close();
    EXPECT_FALSE(file.IsValid());
  }

  {
    // Create a file that exists.
    File file(file_path, File::FLAG_CREATE | File::FLAG_READ);
    EXPECT_FALSE(file.IsValid());
    EXPECT_FALSE(file.created());
    EXPECT_EQ(File::FILE_ERROR_EXISTS, file.error_details());
    EXPECT_EQ(File::FILE_ERROR_EXISTS, File::GetLastFileError());
  }

  {
    // Create or overwrite a file.
    File file(file_path, File::FLAG_CREATE_ALWAYS | File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
    EXPECT_TRUE(file.created());
    EXPECT_EQ(File::FILE_OK, file.error_details());
  }

  {
    // Create a delete-on-close file.
    file_path = temp_dir.GetPath().AppendASCII("create_file_2");
    File file(file_path, File::FLAG_OPEN_ALWAYS | File::FLAG_READ |
                             File::FLAG_DELETE_ON_CLOSE);
    EXPECT_TRUE(file.IsValid());
    EXPECT_TRUE(file.created());
    EXPECT_EQ(File::FILE_OK, file.error_details());
  }

  EXPECT_FALSE(PathExists(file_path));
}

TEST(FileTest, SelfSwap) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("create_file_1");
  File file(file_path, File::FLAG_OPEN_ALWAYS | File::FLAG_DELETE_ON_CLOSE);
  std::swap(file, file);
  EXPECT_TRUE(file.IsValid());
}

TEST(FileTest, Async) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("create_file");

  {
    File file(file_path, File::FLAG_OPEN_ALWAYS | File::FLAG_ASYNC);
    EXPECT_TRUE(file.IsValid());
    EXPECT_TRUE(file.async());
  }

  {
    File file(file_path, File::FLAG_OPEN_ALWAYS);
    EXPECT_TRUE(file.IsValid());
    EXPECT_FALSE(file.async());
  }
}

TEST(FileTest, DeleteOpenFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("create_file_1");

  // Create a file.
  File file(file_path, File::FLAG_OPEN_ALWAYS | File::FLAG_READ |
                           File::FLAG_WIN_SHARE_DELETE);
  EXPECT_TRUE(file.IsValid());
  EXPECT_TRUE(file.created());
  EXPECT_EQ(File::FILE_OK, file.error_details());

  // Open an existing file and mark it as delete on close.
  File same_file(file_path, File::FLAG_OPEN | File::FLAG_DELETE_ON_CLOSE |
                                File::FLAG_READ);
  EXPECT_TRUE(file.IsValid());
  EXPECT_FALSE(same_file.created());
  EXPECT_EQ(File::FILE_OK, same_file.error_details());

  // Close both handles and check that the file is gone.
  file.Close();
  same_file.Close();
  EXPECT_FALSE(PathExists(file_path));
}

TEST(FileTest, ReadWrite) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("read_write_file");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  ASSERT_TRUE(file.IsValid());

  std::array<char, 5> data_to_write{"test"};
  const int kTestDataSize = 4;

  // Write 0 bytes to the file.
  int bytes_written = file.Write(0, data_to_write.data(), 0);
  EXPECT_EQ(0, bytes_written);

  // Write 0 bytes, with buf=nullptr.
  bytes_written = file.Write(0, nullptr, 0);
  EXPECT_EQ(0, bytes_written);

  // Write "test" to the file.
  bytes_written = file.Write(0, data_to_write.data(), kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  // Read from EOF.
  char data_read_1[32];
  int bytes_read = file.Read(kTestDataSize, data_read_1, kTestDataSize);
  EXPECT_EQ(0, bytes_read);

  // Read from somewhere in the middle of the file.
  const int kPartialReadOffset = 1;
  bytes_read = file.Read(kPartialReadOffset, data_read_1, kTestDataSize);
  EXPECT_EQ(kTestDataSize - kPartialReadOffset, bytes_read);
  for (int i = 0; i < bytes_read; i++) {
    EXPECT_EQ(data_to_write[i + kPartialReadOffset], data_read_1[i]);
  }

  // Read 0 bytes.
  bytes_read = file.Read(0, data_read_1, 0);
  EXPECT_EQ(0, bytes_read);

  // Read the entire file.
  bytes_read = file.Read(0, data_read_1, kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_read);
  for (int i = 0; i < bytes_read; i++) {
    EXPECT_EQ(data_to_write[i], data_read_1[i]);
  }

  // Read again, but using the trivial native wrapper.
  std::optional<size_t> maybe_bytes_read =
      file.ReadNoBestEffort(0, as_writable_byte_span(data_read_1)
                                   .first(static_cast<size_t>(kTestDataSize)));
  ASSERT_TRUE(maybe_bytes_read.has_value());
  EXPECT_LE(maybe_bytes_read.value(), static_cast<size_t>(kTestDataSize));
  for (size_t i = 0; i < maybe_bytes_read.value(); i++) {
    EXPECT_EQ(data_to_write[i], data_read_1[i]);
  }

  // Write past the end of the file.
  const int kOffsetBeyondEndOfFile = 10;
  const int kPartialWriteLength = 2;
  bytes_written = file.Write(kOffsetBeyondEndOfFile, data_to_write.data(),
                             kPartialWriteLength);
  EXPECT_EQ(kPartialWriteLength, bytes_written);

  // Make sure the file was extended.
  std::optional<int64_t> file_size = GetFileSize(file_path);
  ASSERT_TRUE(file_size.has_value());
  EXPECT_EQ(kOffsetBeyondEndOfFile + kPartialWriteLength, file_size.value());

  // Make sure the file was zero-padded.
  char data_read_2[32];
  bytes_read = file.Read(0, data_read_2, static_cast<int>(file_size.value()));
  EXPECT_EQ(file_size, bytes_read);
  for (int i = 0; i < kTestDataSize; i++) {
    EXPECT_EQ(data_to_write[i], data_read_2[i]);
  }
  for (int i = kTestDataSize; i < kOffsetBeyondEndOfFile; i++) {
    EXPECT_EQ(0, data_read_2[i]);
  }
  for (int i = kOffsetBeyondEndOfFile; i < file_size; i++) {
    EXPECT_EQ(data_to_write[i - kOffsetBeyondEndOfFile], data_read_2[i]);
  }
}

TEST(FileTest, ReadWriteSpans) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("read_write_file");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  ASSERT_TRUE(file.IsValid());

  // Write 0 bytes to the file.
  std::optional<size_t> bytes_written = file.Write(0, span<uint8_t>());
  ASSERT_TRUE(bytes_written.has_value());
  EXPECT_EQ(0u, bytes_written.value());

  // Write "test" to the file.
  std::string data_to_write("test");
  bytes_written = file.Write(0, as_byte_span(data_to_write));
  ASSERT_TRUE(bytes_written.has_value());
  EXPECT_EQ(data_to_write.size(), bytes_written.value());

  // Read from EOF.
  std::array<uint8_t, 32> data_read_1;
  std::optional<size_t> bytes_read =
      file.Read(bytes_written.value(), data_read_1);
  ASSERT_TRUE(bytes_read.has_value());
  EXPECT_EQ(0u, bytes_read.value());

  // Read from somewhere in the middle of the file.
  const int kPartialReadOffset = 1;
  bytes_read = file.Read(kPartialReadOffset, data_read_1);
  ASSERT_TRUE(bytes_read.has_value());
  EXPECT_EQ(bytes_written.value() - kPartialReadOffset, bytes_read.value());
  for (size_t i = 0; i < bytes_read.value(); i++) {
    EXPECT_EQ(data_to_write[i + kPartialReadOffset], data_read_1[i]);
  }

  // Read 0 bytes.
  bytes_read = file.Read(0, span<uint8_t>());
  ASSERT_TRUE(bytes_read.has_value());
  EXPECT_EQ(0u, bytes_read.value());

  // Read the entire file.
  bytes_read = file.Read(0, data_read_1);
  ASSERT_TRUE(bytes_read.has_value());
  EXPECT_EQ(data_to_write.size(), bytes_read.value());
  for (int i = 0; i < bytes_read; i++) {
    EXPECT_EQ(data_to_write[i], data_read_1[i]);
  }

  // Write past the end of the file.
  const size_t kOffsetBeyondEndOfFile = 10;
  const size_t kPartialWriteLength = 2;
  bytes_written =
      file.Write(kOffsetBeyondEndOfFile,
                 as_byte_span(data_to_write).first(kPartialWriteLength));
  ASSERT_TRUE(bytes_written.has_value());
  EXPECT_EQ(kPartialWriteLength, bytes_written.value());

  // Make sure the file was extended.
  std::optional<int64_t> file_size = GetFileSize(file_path);
  ASSERT_TRUE(file_size.has_value());
  EXPECT_EQ(static_cast<int64_t>(kOffsetBeyondEndOfFile + kPartialWriteLength),
            file_size.value());

  // Make sure the file was zero-padded.
  std::array<uint8_t, 32> data_read_2;
  bytes_read = file.Read(0, data_read_2);
  ASSERT_TRUE(bytes_read.has_value());
  EXPECT_EQ(file_size, static_cast<int64_t>(bytes_read.value()));
  for (size_t i = 0; i < data_to_write.size(); i++) {
    EXPECT_EQ(data_to_write[i], data_read_2[i]);
  }
  for (size_t i = data_to_write.size(); i < kOffsetBeyondEndOfFile; i++) {
    EXPECT_EQ(0, data_read_2[i]);
  }
  for (size_t i = 0; i < kPartialWriteLength; i++) {
    EXPECT_EQ(data_to_write[i], data_read_2[i + kOffsetBeyondEndOfFile]);
  }
}

TEST(FileTest, GetLastFileError) {
#if BUILDFLAG(IS_WIN)
  ::SetLastError(ERROR_ACCESS_DENIED);
#else
  errno = EACCES;
#endif
  EXPECT_EQ(File::FILE_ERROR_ACCESS_DENIED, File::GetLastFileError());

  ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  FilePath nonexistent_path(temp_dir.GetPath().AppendASCII("nonexistent"));
  File file(nonexistent_path, File::FLAG_OPEN | File::FLAG_READ);
  File::Error last_error = File::GetLastFileError();
  EXPECT_FALSE(file.IsValid());
  EXPECT_EQ(File::FILE_ERROR_NOT_FOUND, file.error_details());
  EXPECT_EQ(File::FILE_ERROR_NOT_FOUND, last_error);
}

TEST(FileTest, Append) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("append_file");
  File file(file_path, File::FLAG_CREATE | File::FLAG_APPEND);
  ASSERT_TRUE(file.IsValid());

  std::array<char, 5> data_to_write{"test"};
  const int kTestDataSize = 4;

  // Write 0 bytes to the file.
  int bytes_written = file.Write(0, data_to_write.data(), 0);
  EXPECT_EQ(0, bytes_written);

  // Write 0 bytes, with buf=nullptr.
  bytes_written = file.Write(0, nullptr, 0);
  EXPECT_EQ(0, bytes_written);

  // Write "test" to the file.
  bytes_written = file.Write(0, data_to_write.data(), kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  file.Close();
  File file2(file_path, File::FLAG_OPEN | File::FLAG_READ | File::FLAG_APPEND);
  ASSERT_TRUE(file2.IsValid());

  // Test passing the file around.
  file = std::move(file2);
  EXPECT_FALSE(file2.IsValid());  // NOLINT(bugprone-use-after-move)
  ASSERT_TRUE(file.IsValid());

  std::array<char, 3> append_data_to_write{"78"};
  const int kAppendDataSize = 2;

  // Append "78" to the file.
  bytes_written = file.Write(0, append_data_to_write.data(), kAppendDataSize);
  EXPECT_EQ(kAppendDataSize, bytes_written);

  // Read the entire file.
  char data_read_1[32];
  int bytes_read = file.Read(0, data_read_1, kTestDataSize + kAppendDataSize);
  EXPECT_EQ(kTestDataSize + kAppendDataSize, bytes_read);
  for (int i = 0; i < kTestDataSize; i++) {
    EXPECT_EQ(data_to_write[i], data_read_1[i]);
  }
  for (int i = 0; i < kAppendDataSize; i++) {
    EXPECT_EQ(append_data_to_write[i], data_read_1[kTestDataSize + i]);
  }
}

TEST(FileTest, Length) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("truncate_file");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  ASSERT_TRUE(file.IsValid());
  EXPECT_EQ(0, file.GetLength());

  // Write "test" to the file.
  std::array<char, 5> data_to_write{"test"};
  int kTestDataSize = 4;
  int bytes_written = file.Write(0, data_to_write.data(), kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  // Extend the file.
  const int kExtendedFileLength = 10;
  EXPECT_TRUE(file.SetLength(kExtendedFileLength));
  EXPECT_EQ(kExtendedFileLength, file.GetLength());
  std::optional<int64_t> file_size = GetFileSize(file_path);
  ASSERT_TRUE(file_size.has_value());
  EXPECT_EQ(kExtendedFileLength, file_size.value());

  // Make sure the file was zero-padded.
  char data_read[32];
  int bytes_read = file.Read(0, data_read, static_cast<int>(file_size.value()));
  EXPECT_EQ(file_size, bytes_read);
  for (int i = 0; i < kTestDataSize; i++) {
    EXPECT_EQ(data_to_write[i], data_read[i]);
  }
  for (int i = kTestDataSize; i < file_size; i++) {
    EXPECT_EQ(0, data_read[i]);
  }

  // Truncate the file.
  const int kTruncatedFileLength = 2;
  EXPECT_TRUE(file.SetLength(kTruncatedFileLength));
  EXPECT_EQ(kTruncatedFileLength, file.GetLength());

  file_size = GetFileSize(file_path);
  ASSERT_TRUE(file_size.has_value());
  EXPECT_EQ(kTruncatedFileLength, file_size.value());

  // Make sure the file was truncated.
  bytes_read = file.Read(0, data_read, kTestDataSize);
  EXPECT_EQ(file_size.value(), bytes_read);
  for (int i = 0; i < file_size.value(); i++) {
    EXPECT_EQ(data_to_write[i], data_read[i]);
  }

#if !BUILDFLAG(IS_FUCHSIA)  // Fuchsia doesn't seem to support big files.
  // Expand the file past the 4 GB limit.
  const int64_t kBigFileLength = 5'000'000'000;
  EXPECT_TRUE(file.SetLength(kBigFileLength));
  EXPECT_EQ(kBigFileLength, file.GetLength());
  file_size = GetFileSize(file_path);
  ASSERT_TRUE(file_size.has_value());
  EXPECT_EQ(kBigFileLength, file_size.value());
#endif

  // Close the file and reopen with File::FLAG_CREATE_ALWAYS, and make
  // sure the file is empty (old file was overridden).
  file.Close();
  file.Initialize(file_path, File::FLAG_CREATE_ALWAYS | File::FLAG_WRITE);
  EXPECT_EQ(0, file.GetLength());
}

// Flakily fails: http://crbug.com/86494
#if BUILDFLAG(IS_ANDROID)
TEST(FileTest, TouchGetInfo) {
#else
TEST(FileTest, DISABLED_TouchGetInfo) {
#endif
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  File file(temp_dir.GetPath().AppendASCII("touch_get_info_file"),
            File::FLAG_CREATE | File::FLAG_WRITE | File::FLAG_WRITE_ATTRIBUTES);
  ASSERT_TRUE(file.IsValid());

  // Get info for a newly created file.
  File::Info info;
  EXPECT_TRUE(file.GetInfo(&info));

  // Add 2 seconds to account for possible rounding errors on
  // filesystems that use a 1s or 2s timestamp granularity.
  Time now = Time::Now() + Seconds(2);
  EXPECT_EQ(0, info.size);
  EXPECT_FALSE(info.is_directory);
  EXPECT_FALSE(info.is_symbolic_link);
  EXPECT_LE(info.last_accessed.ToInternalValue(), now.ToInternalValue());
  EXPECT_LE(info.last_modified.ToInternalValue(), now.ToInternalValue());
  EXPECT_LE(info.creation_time.ToInternalValue(), now.ToInternalValue());
  Time creation_time = info.creation_time;

  // Write "test" to the file.
  char data[] = "test";
  const int kTestDataSize = 4;
  int bytes_written = file.Write(0, data, kTestDataSize);
  EXPECT_EQ(kTestDataSize, bytes_written);

  // Change the last_accessed and last_modified dates.
  // It's best to add values that are multiples of 2 (in seconds)
  // to the current last_accessed and last_modified times, because
  // FATxx uses a 2s timestamp granularity.
  Time new_last_accessed = info.last_accessed + Seconds(234);
  Time new_last_modified = info.last_modified + Minutes(567);

  EXPECT_TRUE(file.SetTimes(new_last_accessed, new_last_modified));

  // Make sure the file info was updated accordingly.
  EXPECT_TRUE(file.GetInfo(&info));
  EXPECT_EQ(info.size, kTestDataSize);
  EXPECT_FALSE(info.is_directory);
  EXPECT_FALSE(info.is_symbolic_link);

  // ext2/ext3 and HPS/HPS+ seem to have a timestamp granularity of 1s.
#if BUILDFLAG(IS_POSIX)
  EXPECT_EQ(info.last_accessed.ToTimeVal().tv_sec,
            new_last_accessed.ToTimeVal().tv_sec);
  EXPECT_EQ(info.last_modified.ToTimeVal().tv_sec,
            new_last_modified.ToTimeVal().tv_sec);
#else
  EXPECT_EQ(info.last_accessed.ToInternalValue(),
            new_last_accessed.ToInternalValue());
  EXPECT_EQ(info.last_modified.ToInternalValue(),
            new_last_modified.ToInternalValue());
#endif

  EXPECT_EQ(info.creation_time.ToInternalValue(),
            creation_time.ToInternalValue());
}

// Test we can retrieve the file's creation time through File::GetInfo().
TEST(FileTest, GetInfoForCreationTime) {
  int64_t before_creation_time_s =
      Time::Now().ToDeltaSinceWindowsEpoch().InSeconds();

  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("test_file");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  EXPECT_TRUE(file.IsValid());

  int64_t after_creation_time_s =
      Time::Now().ToDeltaSinceWindowsEpoch().InSeconds();

  File::Info info;
  EXPECT_TRUE(file.GetInfo(&info));
  EXPECT_GE(info.creation_time.ToDeltaSinceWindowsEpoch().InSeconds(),
            before_creation_time_s);
  EXPECT_LE(info.creation_time.ToDeltaSinceWindowsEpoch().InSeconds(),
            after_creation_time_s);
}

TEST(FileTest, ReadAtCurrentPosition) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path =
      temp_dir.GetPath().AppendASCII("read_at_current_position");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  EXPECT_TRUE(file.IsValid());

  const char kData[] = "test";
  const int kDataSize = sizeof(kData) - 1;
  EXPECT_EQ(kDataSize, file.Write(0, kData, kDataSize));

  EXPECT_EQ(0, file.Seek(File::FROM_BEGIN, 0));

  char buffer[kDataSize];
  int first_chunk_size = kDataSize / 2;
  EXPECT_EQ(first_chunk_size, file.ReadAtCurrentPos(buffer, first_chunk_size));
  EXPECT_EQ(kDataSize - first_chunk_size,
            file.ReadAtCurrentPos(buffer + first_chunk_size,
                                  kDataSize - first_chunk_size));
  EXPECT_EQ(std::string(buffer, buffer + kDataSize), std::string(kData));
}

TEST(FileTest, ReadAtCurrentPositionSpans) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path =
      temp_dir.GetPath().AppendASCII("read_at_current_position");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  EXPECT_TRUE(file.IsValid());

  std::string data("test");
  std::optional<size_t> result = file.Write(0, as_byte_span(data));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(data.size(), result.value());

  EXPECT_EQ(0, file.Seek(File::FROM_BEGIN, 0));

  std::array<uint8_t, 4> buffer;
  size_t first_chunk_size = 2;
  const auto [chunk1, chunk2] = span(buffer).split_at(first_chunk_size);
  EXPECT_EQ(chunk1.size(), file.ReadAtCurrentPos(chunk1));
  EXPECT_EQ(chunk2.size(), file.ReadAtCurrentPos(chunk2));
  for (size_t i = 0; i < data.size(); i++) {
    EXPECT_EQ(data[i], static_cast<char>(buffer[i]));
  }
}

TEST(FileTest, WriteAtCurrentPosition) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path =
      temp_dir.GetPath().AppendASCII("write_at_current_position");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  EXPECT_TRUE(file.IsValid());

  const char kData[] = "test";
  const int kDataSize = sizeof(kData) - 1;

  int first_chunk_size = kDataSize / 2;
  EXPECT_EQ(first_chunk_size, file.WriteAtCurrentPos(kData, first_chunk_size));
  EXPECT_EQ(kDataSize - first_chunk_size,
            file.WriteAtCurrentPos(kData + first_chunk_size,
                                   kDataSize - first_chunk_size));

  char buffer[kDataSize];
  EXPECT_EQ(kDataSize, file.Read(0, buffer, kDataSize));
  EXPECT_EQ(std::string(buffer, buffer + kDataSize), std::string(kData));
}

TEST(FileTest, WriteAtCurrentPositionSpans) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path =
      temp_dir.GetPath().AppendASCII("write_at_current_position");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  EXPECT_TRUE(file.IsValid());

  std::string data("test");
  size_t first_chunk_size = data.size() / 2;
  const auto [first, second] = as_byte_span(data).split_at(first_chunk_size);
  std::optional<size_t> result = file.WriteAtCurrentPos(first);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(first_chunk_size, result.value());

  result = file.WriteAtCurrentPos(second);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(first_chunk_size, result.value());

  const int kDataSize = 4;
  char buffer[kDataSize];
  EXPECT_EQ(kDataSize, file.Read(0, buffer, kDataSize));
  EXPECT_EQ(std::string(buffer, buffer + kDataSize), data);
}

TEST(FileTest, Seek) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("seek_file");
  File file(file_path, File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
  ASSERT_TRUE(file.IsValid());

  const int64_t kOffset = 10;
  EXPECT_EQ(kOffset, file.Seek(File::FROM_BEGIN, kOffset));
  EXPECT_EQ(2 * kOffset, file.Seek(File::FROM_CURRENT, kOffset));
  EXPECT_EQ(kOffset, file.Seek(File::FROM_CURRENT, -kOffset));
  EXPECT_TRUE(file.SetLength(kOffset * 2));
  EXPECT_EQ(kOffset, file.Seek(File::FROM_END, -kOffset));
}

TEST(FileTest, Duplicate) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");
  File file(file_path,
            (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE));
  ASSERT_TRUE(file.IsValid());

  File file2(file.Duplicate());
  ASSERT_TRUE(file2.IsValid());

  // Write through one handle, close it, read through the other.
  static const char kData[] = "now is a good time.";
  static const int kDataLen = sizeof(kData) - 1;

  ASSERT_EQ(0, file.Seek(File::FROM_CURRENT, 0));
  ASSERT_EQ(0, file2.Seek(File::FROM_CURRENT, 0));
  ASSERT_EQ(kDataLen, file.WriteAtCurrentPos(kData, kDataLen));
  ASSERT_EQ(kDataLen, file.Seek(File::FROM_CURRENT, 0));
  ASSERT_EQ(kDataLen, file2.Seek(File::FROM_CURRENT, 0));
  file.Close();
  char buf[kDataLen];
  ASSERT_EQ(kDataLen, file2.Read(0, &buf[0], kDataLen));
  ASSERT_EQ(std::string(kData, kDataLen), std::string(&buf[0], kDataLen));
}

TEST(FileTest, DuplicateDeleteOnClose) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");
  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());
  File file2(file.Duplicate());
  ASSERT_TRUE(file2.IsValid());
  file.Close();
  file2.Close();
  ASSERT_FALSE(PathExists(file_path));
}

TEST(FileTest, TracedValueSupport) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());

  EXPECT_EQ(perfetto::TracedValueToString(file),
            "{is_valid:true,created:true,async:false,error_details:FILE_OK}");
}

#if BUILDFLAG(IS_WIN)
// Flakily times out on Windows, see http://crbug.com/846276.
#define MAYBE_WriteDataToLargeOffset DISABLED_WriteDataToLargeOffset
#else
#define MAYBE_WriteDataToLargeOffset WriteDataToLargeOffset
#endif
TEST(FileTest, MAYBE_WriteDataToLargeOffset) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");
  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());

  const char kData[] = "this file is sparse.";
  const int kDataLen = sizeof(kData) - 1;
  const int64_t kLargeFileOffset = (1LL << 31);

  // If the file fails to write, it is probably we are running out of disk space
  // and the file system doesn't support sparse file.
  if (file.Write(kLargeFileOffset - kDataLen - 1, kData, kDataLen) < 0) {
    return;
  }

  ASSERT_EQ(kDataLen, file.Write(kLargeFileOffset + 1, kData, kDataLen));
}

TEST(FileTest, AddFlagsForPassingToUntrustedProcess) {
  {
    uint32_t flags = File::FLAG_OPEN | File::FLAG_READ;
    flags = File::AddFlagsForPassingToUntrustedProcess(flags);
    EXPECT_EQ(flags, File::FLAG_OPEN | File::FLAG_READ);
  }
  {
    uint32_t flags = File::FLAG_OPEN | File::FLAG_WRITE;
    flags = File::AddFlagsForPassingToUntrustedProcess(flags);
    EXPECT_EQ(flags,
              File::FLAG_OPEN | File::FLAG_WRITE | File::FLAG_WIN_NO_EXECUTE);
  }
}

#if BUILDFLAG(IS_WIN)
TEST(FileTest, GetInfoForDirectory) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath empty_dir =
      temp_dir.GetPath().Append(FILE_PATH_LITERAL("gpfi_test"));
  ASSERT_TRUE(CreateDirectory(empty_dir));

  File dir(
      ::CreateFile(empty_dir.value().c_str(), GENERIC_READ | GENERIC_WRITE,
                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                   OPEN_EXISTING,
                   FILE_FLAG_BACKUP_SEMANTICS,  // Needed to open a directory.
                   NULL));
  ASSERT_TRUE(dir.IsValid());

  File::Info info;
  EXPECT_TRUE(dir.GetInfo(&info));
  EXPECT_TRUE(info.is_directory);
  EXPECT_FALSE(info.is_symbolic_link);
  EXPECT_EQ(0, info.size);
}

TEST(FileTest, DeleteNoop) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // Creating and closing a file with DELETE perms should do nothing special.
  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());
  file.Close();
  ASSERT_TRUE(PathExists(file_path));
}

TEST(FileTest, Delete) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // Creating a file with DELETE and then marking for delete on close should
  // delete it.
  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());
  ASSERT_TRUE(file.DeleteOnClose(true));
  file.Close();
  ASSERT_FALSE(PathExists(file_path));
}

TEST(FileTest, DeleteThenRevoke) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // Creating a file with DELETE, marking it for delete, then clearing delete on
  // close should not delete it.
  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());
  ASSERT_TRUE(file.DeleteOnClose(true));
  ASSERT_TRUE(file.DeleteOnClose(false));
  file.Close();
  ASSERT_TRUE(PathExists(file_path));
}

TEST(FileTest, IrrevokableDeleteOnClose) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // DELETE_ON_CLOSE cannot be revoked by this opener.
  File file(file_path,
            (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
             File::FLAG_DELETE_ON_CLOSE | File::FLAG_WIN_SHARE_DELETE |
             File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());
  // https://msdn.microsoft.com/library/windows/desktop/aa364221.aspx says that
  // setting the dispositon has no effect if the handle was opened with
  // FLAG_DELETE_ON_CLOSE. Do not make the test's success dependent on whether
  // or not SetFileInformationByHandle indicates success or failure. (It happens
  // to indicate success on Windows 10.)
  file.DeleteOnClose(false);
  file.Close();
  ASSERT_FALSE(PathExists(file_path));
}

TEST(FileTest, IrrevokableDeleteOnCloseOther) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // DELETE_ON_CLOSE cannot be revoked by another opener.
  File file(file_path,
            (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
             File::FLAG_DELETE_ON_CLOSE | File::FLAG_WIN_SHARE_DELETE |
             File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());

  File file2(file_path,
             (File::FLAG_OPEN | File::FLAG_READ | File::FLAG_WRITE |
              File::FLAG_WIN_SHARE_DELETE | File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file2.IsValid());

  file2.DeleteOnClose(false);
  file2.Close();
  ASSERT_TRUE(PathExists(file_path));
  file.Close();
  ASSERT_FALSE(PathExists(file_path));
}

TEST(FileTest, DeleteWithoutPermission) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // It should not be possible to mark a file for deletion when it was not
  // created/opened with DELETE.
  File file(file_path,
            (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE));
  ASSERT_TRUE(file.IsValid());
  ASSERT_FALSE(file.DeleteOnClose(true));
  file.Close();
  ASSERT_TRUE(PathExists(file_path));
}

TEST(FileTest, UnsharedDeleteOnClose) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // Opening with DELETE_ON_CLOSE when a previous opener hasn't enabled sharing
  // will fail.
  File file(file_path,
            (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE));
  ASSERT_TRUE(file.IsValid());
  File file2(file_path,
             (File::FLAG_OPEN | File::FLAG_READ | File::FLAG_WRITE |
              File::FLAG_DELETE_ON_CLOSE | File::FLAG_WIN_SHARE_DELETE));
  ASSERT_FALSE(file2.IsValid());

  file.Close();
  ASSERT_TRUE(PathExists(file_path));
}

TEST(FileTest, NoDeleteOnCloseWithMappedFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  // Mapping a file into memory blocks DeleteOnClose.
  File file(file_path, (File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE |
                        File::FLAG_CAN_DELETE_ON_CLOSE));
  ASSERT_TRUE(file.IsValid());
  ASSERT_EQ(5, file.WriteAtCurrentPos("12345", 5));

  {
    MemoryMappedFile mapping;
    ASSERT_TRUE(mapping.Initialize(file.Duplicate()));
    ASSERT_EQ(5U, mapping.length());

    EXPECT_FALSE(file.DeleteOnClose(true));
  }

  file.Close();
  ASSERT_TRUE(PathExists(file_path));
}

// Check that we handle the async bit being set incorrectly in a sane way.
TEST(FileTest, UseSyncApiWithAsyncFile) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_path = temp_dir.GetPath().AppendASCII("file");

  File file(file_path, File::FLAG_CREATE | File::FLAG_WRITE | File::FLAG_ASYNC);
  File lying_file(file.TakePlatformFile(), false /* async */);
  ASSERT_TRUE(lying_file.IsValid());

  ASSERT_EQ(lying_file.WriteAtCurrentPos("12345", 5), -1);
}

TEST(FileDeathTest, InvalidFlags) {
  EXPECT_CHECK_DEATH_WITH(
      {
        // When this test is running as Admin, TMP gets ignored and temporary
        // files/folders are created in %ProgramFiles%. This means that the
        // temporary folder created by the death test never gets deleted, as it
        // crashes before the `ScopedTempDir` goes out of scope and also
        // does not get automatically cleaned by by the test runner.
        //
        // To avoid this from happening, this death test explicitly creates the
        // temporary folder in TMP, which is set by the test runner parent
        // process to a temporary folder for the test. This means that the
        // folder created here is always deleted during test runner cleanup.
        std::optional<std::string> tmp_folder =
            Environment::Create()->GetVar("TMP");
        ASSERT_TRUE(tmp_folder.has_value());
        ScopedTempDir temp_dir;
        ASSERT_TRUE(temp_dir.CreateUniqueTempDirUnderPath(
            FilePath(UTF8ToWide(tmp_folder.value()))));
        FilePath file_path = temp_dir.GetPath().AppendASCII("file");

        File file(file_path, File::FLAG_CREATE | File::FLAG_WIN_EXECUTE |
                                 File::FLAG_READ | File::FLAG_WIN_NO_EXECUTE);
      },
      "FLAG_WIN_NO_EXECUTE");
}
#endif  // BUILDFLAG(IS_WIN)

}  // namespace base
