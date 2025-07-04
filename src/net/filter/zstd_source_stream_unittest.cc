// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/filter/zstd_source_stream.h"

#include <stdint.h>

#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_view_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "net/base/io_buffer.h"
#include "net/base/test_completion_callback.h"
#include "net/filter/mock_source_stream.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace net {

namespace {

const size_t kDefaultBufferSize = 4096;
const size_t kLargeBufferSize = 7168;

// Get the path of data directory.
base::FilePath GetTestDataDir() {
  base::FilePath data_dir;
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &data_dir);
  data_dir = data_dir.AppendASCII("net");
  data_dir = data_dir.AppendASCII("data");
  data_dir = data_dir.AppendASCII("filter_unittests");
  return data_dir;
}

}  // namespace

class ZstdSourceStreamTest : public PlatformTest {
 protected:
  void SetUp() override {
    PlatformTest::SetUp();

    // Get the path of data directory.
    base::FilePath data_dir = GetTestDataDir();

    // Read data from the original file into buffer.
    base::FilePath file_path;
    file_path = data_dir.AppendASCII("google.txt");
    ASSERT_TRUE(base::ReadFileToString(file_path, &source_data_));
    ASSERT_GE(kDefaultBufferSize, source_data_.size());

    // Read data from the encoded file into buffer.
    base::FilePath encoded_file_path;
    encoded_file_path = data_dir.AppendASCII("google.zst");
    ASSERT_TRUE(base::ReadFileToString(encoded_file_path, &encoded_buffer_));
    ASSERT_GE(kDefaultBufferSize, encoded_buffer_.size());

    auto source = std::make_unique<MockSourceStream>();
    source->set_expect_all_input_consumed(false);
    source_ = source.get();
    zstd_stream_ = CreateZstdSourceStream(std::move(source));

    out_buffer_ = base::MakeRefCounted<IOBufferWithSize>(kDefaultBufferSize);
  }

  int ReadStream(net::CompletionOnceCallback callback) {
    return zstd_stream_->Read(out_buffer_.get(), out_buffer_->size(),
                              std::move(callback));
  }

  std::string ReadStreamUntilDone() {
    std::string actual_output;
    while (true) {
      TestCompletionCallback callback;
      int bytes_read = ReadStream(callback.callback());
      if (bytes_read <= OK) {
        break;
      }
      actual_output.append(
          base::as_string_view(out_buffer_->first(bytes_read)));
    }
    return actual_output;
  }

  IOBuffer* out_buffer() { return out_buffer_.get(); }

  std::string source_data() { return source_data_; }
  size_t source_data_len() { return source_data_.length(); }

  base::span<const uint8_t> encoded_span() const {
    return base::as_byte_span(encoded_buffer_);
  }

  MockSourceStream* source() { return source_; }
  SourceStream* zstd_stream() { return zstd_stream_.get(); }

  void ResetStream() {
    source_ = nullptr;
    zstd_stream_ = nullptr;
  }

 private:
  std::unique_ptr<SourceStream> zstd_stream_;
  raw_ptr<MockSourceStream> source_;
  scoped_refptr<IOBufferWithSize> out_buffer_;

  std::string source_data_;
  std::string encoded_buffer_;
};

TEST_F(ZstdSourceStreamTest, EmptyStream) {
  source()->AddReadResult(base::span<uint8_t>(), OK, MockSourceStream::SYNC);
  TestCompletionCallback callback;
  int result = ReadStream(callback.callback());
  EXPECT_EQ(OK, result);
  EXPECT_EQ("ZSTD", zstd_stream()->Description());
}

// Basic scenario: decoding zstd data with big enough buffer
TEST_F(ZstdSourceStreamTest, DecodeZstdOneBlockSync) {
  base::HistogramTester histograms;

  source()->AddReadResult(encoded_span(), OK, MockSourceStream::SYNC);

  TestCompletionCallback callback;
  int bytes_read = ReadStream(callback.callback());
  EXPECT_EQ(static_cast<int>(source_data_len()), bytes_read);
  EXPECT_EQ(base::as_string_view(out_buffer()->first(source_data_len())),
            source_data());

  // Resetting streams is needed to call the destructor of ZstdSourceStream,
  // where the histograms are recorded.
  ResetStream();

  histograms.ExpectTotalCount("Net.ZstdFilter.Status", 1);
  histograms.ExpectUniqueSample(
      "Net.ZstdFilter.Status",
      static_cast<int>(ZstdDecodingStatus::kEndOfFrame), 1);
}

TEST_F(ZstdSourceStreamTest, IgnoreExtraDataInOneRead) {
  std::string response_with_extra_data(base::as_string_view(encoded_span()));
  response_with_extra_data.append(100, 'x');
  source()->AddReadResult(base::as_byte_span(response_with_extra_data), OK,
                          MockSourceStream::SYNC);
  // Add an EOF.
  source()->AddReadResult(base::span<uint8_t>(), OK, MockSourceStream::SYNC);

  std::string actual_output = ReadStreamUntilDone();

  EXPECT_EQ(source_data_len(), actual_output.size());
  EXPECT_EQ(source_data(), actual_output);
}

TEST_F(ZstdSourceStreamTest, IgnoreExtraDataInDifferentRead) {
  std::vector<uint8_t> extra_data(100, 'x');
  source()->AddReadResult(encoded_span(), OK, MockSourceStream::SYNC);
  source()->AddReadResult(extra_data, OK, MockSourceStream::SYNC);
  // Add an EOF.
  source()->AddReadResult(base::span<uint8_t>(), OK, MockSourceStream::SYNC);

  std::string actual_output = ReadStreamUntilDone();

  EXPECT_EQ(source_data_len(), actual_output.size());
  EXPECT_EQ(source_data(), actual_output);
}

TEST_F(ZstdSourceStreamTest, DecodeZstdTwoBlockSync) {
  source()->AddReadResult(encoded_span().first(10u), OK,
                          MockSourceStream::SYNC);
  source()->AddReadResult(encoded_span().subspan(10u), OK,
                          MockSourceStream::SYNC);
  TestCompletionCallback callback;
  int bytes_read = ReadStream(callback.callback());
  EXPECT_EQ(static_cast<int>(source_data_len()), bytes_read);
  EXPECT_EQ(base::as_string_view(out_buffer()->first(source_data_len())),
            source_data());
}

TEST_F(ZstdSourceStreamTest, DecodeZstdOneBlockAsync) {
  source()->AddReadResult(encoded_span(), OK, MockSourceStream::ASYNC);
  // Add an EOF.
  source()->AddReadResult(base::span<uint8_t>(), OK, MockSourceStream::ASYNC);

  scoped_refptr<IOBuffer> buffer =
      base::MakeRefCounted<IOBufferWithSize>(source_data_len());

  std::string actual_output;
  int bytes_read = 0;
  do {
    TestCompletionCallback callback;
    bytes_read = ReadStream(callback.callback());
    if (bytes_read == ERR_IO_PENDING) {
      source()->CompleteNextRead();
      bytes_read = callback.WaitForResult();
    }
    EXPECT_GE(static_cast<int>(kDefaultBufferSize), bytes_read);
    EXPECT_GE(bytes_read, 0);
    if (bytes_read > 0) {
      actual_output.append(
          base::as_string_view(out_buffer()->first(bytes_read)));
    }
  } while (bytes_read > 0);
  EXPECT_EQ(source_data_len(), actual_output.size());
  EXPECT_EQ(source_data(), actual_output);
}

TEST_F(ZstdSourceStreamTest, DecodeTwoConcatenatedFrames) {
  std::string encoded_buffer;
  std::string source_data;

  base::FilePath data_dir = GetTestDataDir();

  // Read data from the original file into buffer.
  base::FilePath file_path;
  file_path = data_dir.AppendASCII("google.txt");
  ASSERT_TRUE(base::ReadFileToString(file_path, &source_data));
  source_data.append(source_data);
  ASSERT_GE(kLargeBufferSize, source_data.size());

  // Read data from the encoded file into buffer.
  base::FilePath encoded_file_path;
  encoded_file_path = data_dir.AppendASCII("google.zst");
  ASSERT_TRUE(base::ReadFileToString(encoded_file_path, &encoded_buffer));

  // Concatenate two encoded buffers.
  encoded_buffer.append(encoded_buffer);
  ASSERT_GE(kLargeBufferSize, encoded_buffer.size());

  scoped_refptr<IOBufferWithSize> out_buffer =
      base::MakeRefCounted<IOBufferWithSize>(kLargeBufferSize);

  // Decompress content.
  auto source = std::make_unique<MockSourceStream>();
  source->AddReadResult(base::as_byte_span(encoded_buffer), OK,
                        MockSourceStream::SYNC);
  source->AddReadResult(base::span<uint8_t>(), OK, MockSourceStream::SYNC);
  source->set_expect_all_input_consumed(false);

  std::unique_ptr<SourceStream> zstd_stream =
      CreateZstdSourceStream(std::move(source));

  std::string actual_output;
  while (true) {
    TestCompletionCallback callback;
    int bytes_read = zstd_stream->Read(out_buffer.get(), kLargeBufferSize,
                                       callback.callback());
    if (bytes_read <= OK) {
      break;
    }
    actual_output.append(base::as_string_view(out_buffer->first(bytes_read)));
  }

  EXPECT_EQ(source_data.length(), actual_output.size());
  EXPECT_EQ(source_data, actual_output);
}

TEST_F(ZstdSourceStreamTest, WithDictionary) {
  std::string encoded_buffer;
  std::string dictionary_data;

  base::FilePath data_dir = GetTestDataDir();
  // Read data from the encoded file into buffer.
  base::FilePath encoded_file_path;
  encoded_file_path = data_dir.AppendASCII("google.szst");
  ASSERT_TRUE(base::ReadFileToString(encoded_file_path, &encoded_buffer));

  // Read data from the dictionary file into buffer.
  base::FilePath dictionary_file_path;
  dictionary_file_path = data_dir.AppendASCII("test.dict");
  ASSERT_TRUE(base::ReadFileToString(dictionary_file_path, &dictionary_data));

  scoped_refptr<net::IOBuffer> dictionary_buffer =
      base::MakeRefCounted<net::StringIOBuffer>(dictionary_data);

  scoped_refptr<IOBufferWithSize> out_buffer =
      base::MakeRefCounted<IOBufferWithSize>(kDefaultBufferSize);

  auto source = std::make_unique<MockSourceStream>();
  source->AddReadResult(base::as_byte_span(encoded_buffer), OK,
                        MockSourceStream::SYNC);

  std::unique_ptr<SourceStream> zstd_stream =
      CreateZstdSourceStreamWithDictionary(std::move(source), dictionary_buffer,
                                           dictionary_data.size());

  TestCompletionCallback callback;
  int bytes_read = zstd_stream->Read(out_buffer.get(), kDefaultBufferSize,
                                     callback.callback());

  EXPECT_EQ(static_cast<int>(source_data_len()), bytes_read);
  EXPECT_EQ(base::as_string_view(out_buffer->span().first(source_data_len())),
            source_data());
}

TEST_F(ZstdSourceStreamTest, WindowSizeTooBig) {
  base::HistogramTester histograms;

  constexpr uint8_t kNineMegWindowZstd[] = {
      0x28, 0xb5, 0x2f, 0xfd, 0xa4, 0x00, 0x00, 0x90, 0x00, 0x4c, 0x00, 0x00,
      0x08, 0x00, 0x01, 0x00, 0xfc, 0xff, 0x39, 0x10, 0x02, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10, 0x00, 0x02, 0x00, 0x10,
      0x00, 0x03, 0x00, 0x10, 0x00, 0x6e, 0x70, 0x97, 0x34};
  out_buffer()->span()[0] = 'e';

  source()->AddReadResult(kNineMegWindowZstd, OK, MockSourceStream::SYNC);

  TestCompletionCallback callback;
  int bytes_read = ReadStream(callback.callback());
  EXPECT_EQ(net::ERR_ZSTD_WINDOW_SIZE_TOO_BIG, bytes_read);
  EXPECT_EQ(out_buffer()->span()[0], 'e');

  // Resetting streams is needed to call the destructor of ZstdSourceStream,
  // where the histograms are recorded.
  ResetStream();

  histograms.ExpectTotalCount("Net.ZstdFilter.Status", 1);
  histograms.ExpectUniqueSample(
      "Net.ZstdFilter.Status",
      static_cast<int>(ZstdDecodingStatus::kDecodingError), 1);
}

}  // namespace net
