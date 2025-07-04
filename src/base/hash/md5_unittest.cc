// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/hash/md5.h"

#include <string.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "base/containers/heap_array.h"
#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

TEST(MD5, DigestToBase16) {
  MD5Digest digest({0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,  //
                    0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e});
  std::string actual = MD5DigestToBase16(digest);
  std::string expected = "d41d8cd98f00b204e9800998ecf8427e";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5SumEmptyData) {
  MD5Digest digest;
  MD5Sum(base::span<uint8_t>(), &digest);
  const uint8_t expected[] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
                              0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
  EXPECT_EQ(base::span(expected), digest.a);
}

TEST(MD5, MD5SumOneByteData) {
  MD5Digest digest;
  MD5Sum(base::as_byte_span(std::string_view("a")), &digest);
  const uint8_t expected[] = {0x0c, 0xc1, 0x75, 0xb9, 0xc0, 0xf1, 0xb6, 0xa8,
                              0x31, 0xc3, 0x99, 0xe2, 0x69, 0x77, 0x26, 0x61};
  EXPECT_EQ(base::span(expected), digest.a);
}

TEST(MD5, MD5SumLongData) {
  const size_t length = 10 * 1024 * 1024 + 1;
  auto data = base::HeapArray<uint8_t>::Uninit(length);

  size_t i = 0;
  for (auto& datum : data) {
    datum = i++ & 0xff;
  }

  MD5Digest digest;
  MD5Sum(data, &digest);

  const uint8_t expected[] = {0x90, 0xbd, 0x6a, 0xd9, 0x0a, 0xce, 0xf5, 0xad,
                              0xaa, 0x92, 0x20, 0x3e, 0x21, 0xc7, 0xa1, 0x3e};
  EXPECT_EQ(base::span(expected), digest.a);
}

TEST(MD5, ContextWithEmptyData) {
  MD5Context ctx;
  MD5Init(&ctx);

  MD5Digest digest;
  MD5Final(&digest, &ctx);

  const uint8_t expected[] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
                              0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
  EXPECT_EQ(base::span(expected), digest.a);
}

TEST(MD5, ContextWithLongData) {
  MD5Context ctx;
  MD5Init(&ctx);

  const size_t length = 10 * 1024 * 1024 + 1;
  auto data = base::HeapArray<uint8_t>::Uninit(length);

  size_t i = 0;
  for (auto& datum : data) {
    datum = i++ & 0xff;
  }

  size_t total = 0;
  while (total < data.size()) {
    size_t len = 4097;  // intentionally not 2^k.
    if (len > length - total) {
      len = length - total;
    }

    MD5Update(&ctx, data.subspan(total, len));
    total += len;
  }

  EXPECT_EQ(length, total);

  MD5Digest digest;
  MD5Final(&digest, &ctx);

  const uint8_t expected[] = {0x90, 0xbd, 0x6a, 0xd9, 0x0a, 0xce, 0xf5, 0xad,
                              0xaa, 0x92, 0x20, 0x3e, 0x21, 0xc7, 0xa1, 0x3e};
  EXPECT_EQ(base::span(expected), digest.a);
}

// Example data from http://www.ietf.org/rfc/rfc1321.txt A.5 Test Suite
TEST(MD5, MD5StringTestSuite1) {
  std::string actual = MD5String("");
  std::string expected = "d41d8cd98f00b204e9800998ecf8427e";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5StringTestSuite2) {
  std::string actual = MD5String("a");
  std::string expected = "0cc175b9c0f1b6a831c399e269772661";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5StringTestSuite3) {
  std::string actual = MD5String("abc");
  std::string expected = "900150983cd24fb0d6963f7d28e17f72";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5StringTestSuite4) {
  std::string actual = MD5String("message digest");
  std::string expected = "f96b697d7cb7938d525a2f31aaf161d0";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5StringTestSuite5) {
  std::string actual = MD5String("abcdefghijklmnopqrstuvwxyz");
  std::string expected = "c3fcd3d76192e4007dfb496cca67e13b";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5StringTestSuite6) {
  std::string actual = MD5String(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789");
  std::string expected = "d174ab98d277d9f5a5611c2c9f419d9f";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, MD5StringTestSuite7) {
  std::string actual = MD5String(
      "12345678901234567890"
      "12345678901234567890"
      "12345678901234567890"
      "12345678901234567890");
  std::string expected = "57edf4a22be3c955ac49da2e2107b67a";
  EXPECT_EQ(expected, actual);
}

TEST(MD5, ContextWithStringData) {
  MD5Context ctx;
  MD5Init(&ctx);

  MD5Update(&ctx, "abc");

  MD5Digest digest;
  MD5Final(&digest, &ctx);

  std::string actual = MD5DigestToBase16(digest);
  std::string expected = "900150983cd24fb0d6963f7d28e17f72";

  EXPECT_EQ(expected, actual);
}

}  // namespace base
