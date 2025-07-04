// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/blockfile/storage_block.h"

#include <algorithm>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "net/disk_cache/blockfile/disk_format.h"
#include "net/disk_cache/blockfile/storage_block-inl.h"
#include "net/disk_cache/disk_cache_test_base.h"
#include "net/disk_cache/disk_cache_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

typedef disk_cache::StorageBlock<disk_cache::EntryStore> CacheEntryBlock;

TEST_F(DiskCacheTest, StorageBlock_LoadStore) {
  base::FilePath filename = cache_path_.AppendASCII("a_test");
  auto file = base::MakeRefCounted<disk_cache::MappedFile>();
  ASSERT_TRUE(CreateCacheTestFile(filename));
  ASSERT_TRUE(file->Init(filename, 8192));

  CacheEntryBlock entry1(file.get(), disk_cache::Addr(0xa0010001));
  std::ranges::fill(base::byte_span_from_ref(*entry1.Data()), 0);
  entry1.Data()->hash = 0xaa5555aa;
  entry1.Data()->rankings_node = 0xa0010002;

  EXPECT_TRUE(entry1.Store());
  entry1.Data()->hash = 0x88118811;
  entry1.Data()->rankings_node = 0xa0040009;

  EXPECT_TRUE(entry1.Load());
  EXPECT_EQ(0xaa5555aa, entry1.Data()->hash);
  EXPECT_EQ(0xa0010002, entry1.Data()->rankings_node);
}

TEST_F(DiskCacheTest, StorageBlock_SetData) {
  base::FilePath filename = cache_path_.AppendASCII("a_test");
  auto file = base::MakeRefCounted<disk_cache::MappedFile>();
  ASSERT_TRUE(CreateCacheTestFile(filename));
  ASSERT_TRUE(file->Init(filename, 8192));

  CacheEntryBlock entry1(file.get(), disk_cache::Addr(0xa0010001));
  entry1.Data()->hash = 0xaa5555aa;

  CacheEntryBlock entry2(file.get(), disk_cache::Addr(0xa0010002));
  EXPECT_TRUE(entry2.Load());
  EXPECT_TRUE(entry2.Data() != nullptr);
  EXPECT_TRUE(0 == entry2.Data()->hash);

  EXPECT_TRUE(entry2.Data() != entry1.Data());
  entry2.SetData(entry1.AllData());
  EXPECT_EQ(0xaa5555aa, entry2.Data()->hash);
  EXPECT_TRUE(entry2.Data() == entry1.Data());
}

TEST_F(DiskCacheTest, StorageBlock_SetModified) {
  base::FilePath filename = cache_path_.AppendASCII("a_test");
  auto file = base::MakeRefCounted<disk_cache::MappedFile>();
  ASSERT_TRUE(CreateCacheTestFile(filename));
  ASSERT_TRUE(file->Init(filename, 8192));

  auto entry1 = std::make_unique<CacheEntryBlock>(file.get(),
                                                  disk_cache::Addr(0xa0010003));
  EXPECT_TRUE(entry1->Load());
  EXPECT_TRUE(0 == entry1->Data()->hash);
  entry1->Data()->hash = 0x45687912;
  entry1->set_modified();
  entry1.reset();

  CacheEntryBlock entry2(file.get(), disk_cache::Addr(0xa0010003));
  EXPECT_TRUE(entry2.Load());
  EXPECT_TRUE(0x45687912 == entry2.Data()->hash);
}

TEST_F(DiskCacheTest, StorageBlock_DifferentNumBuffers) {
  base::FilePath filename = cache_path_.AppendASCII("a_test");
  auto file = base::MakeRefCounted<disk_cache::MappedFile>();
  ASSERT_TRUE(CreateCacheTestFile(filename));
  ASSERT_TRUE(file->Init(filename, 8192));

  // 2 buffers at index 1.
  CacheEntryBlock entry1(file.get(), disk_cache::Addr(0xa1010001));
  EXPECT_TRUE(entry1.Load());

  // 1 buffer at index 3.
  CacheEntryBlock entry2(file.get(), disk_cache::Addr(0xa0010003));
  EXPECT_TRUE(entry2.Load());

  // Now specify 2 buffers at index 1.
  entry2.CopyFrom(&entry1);
  EXPECT_TRUE(entry2.Load());
}

TEST_F(DiskCacheTest, StorageBlock_CopyFrom) {
  base::FilePath filename = cache_path_.AppendASCII("a_test");
  auto file = base::MakeRefCounted<disk_cache::MappedFile>();
  ASSERT_TRUE(CreateCacheTestFile(filename));
  ASSERT_TRUE(file->Init(filename, 8192));

  // 1 buffer at index 1.
  CacheEntryBlock entry1(file.get(), disk_cache::Addr(0xa0010001));
  EXPECT_TRUE(entry1.Load());
  entry1.Data()->creation_time = 1;
  EXPECT_TRUE(entry1.Store());

  // 1 buffer at index 3.
  CacheEntryBlock entry2(file.get(), disk_cache::Addr(0xa0010003));
  EXPECT_TRUE(entry2.Load());
  entry2.Data()->creation_time = 3;
  EXPECT_TRUE(entry2.Store());

  // Now make sure `entry2` points to the same block as `entry1` after copy;
  // both with and w/o refetching from disk.
  entry2.CopyFrom(&entry1);
  EXPECT_EQ(entry1.address(), entry2.address());
  EXPECT_EQ(1, entry2.Data()->creation_time);
  EXPECT_EQ(entry1.Data()->self_hash, entry2.Data()->self_hash);
  EXPECT_TRUE(entry2.Load());
  EXPECT_EQ(1, entry2.Data()->creation_time);
  EXPECT_EQ(entry1.Data()->self_hash, entry2.Data()->self_hash);
}
