// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_RANDOM_H_
#define CRYPTO_RANDOM_H_

#include <stddef.h>

#include <array>
#include <vector>

#include "base/containers/span.h"
#include "crypto/crypto_export.h"

namespace crypto {

// Fills `bytes` with cryptographically-secure random bits.
CRYPTO_EXPORT void RandBytes(base::span<uint8_t> bytes);

// Returns a vector of `length` bytes filled with cryptographically-secure
// random bits.
CRYPTO_EXPORT std::vector<uint8_t> RandBytesAsVector(size_t length);

// Returns an array of `N` bytes filled with cryptographically-secure random
// bits. This is useful for analyzing static nonces and similar.
template <size_t N>
std::array<uint8_t, N> RandBytesAsArray() {
  std::array<uint8_t, N> result;
  RandBytes(result);
  return result;
}

}  // namespace crypto

#endif  // CRYPTO_RANDOM_H_
