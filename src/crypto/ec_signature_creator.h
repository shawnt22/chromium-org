// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This interface is deprecated and being removed: https://crbug.com/406190025.
// New users should use crypto/sign instead.

#ifndef CRYPTO_EC_SIGNATURE_CREATOR_H_
#define CRYPTO_EC_SIGNATURE_CREATOR_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "crypto/crypto_export.h"

namespace crypto {

class ECPrivateKey;
class ECSignatureCreator;

// Signs data using a bare private key (as opposed to a full certificate).
// We need this class because SignatureCreator is hardcoded to use
// RSAPrivateKey.
// TODO(https://crbug.com/406190025): Delete this.
class CRYPTO_EXPORT ECSignatureCreator {
 public:
  virtual ~ECSignatureCreator() {}

  // Create an instance. The caller must ensure that the provided PrivateKey
  // instance outlives the created ECSignatureCreator.
  // TODO(rch):  This is currently hard coded to use SHA256. Ideally, we should
  // pass in the hash algorithm identifier.
  static std::unique_ptr<ECSignatureCreator> Create(ECPrivateKey* key);

  // Signs |data| and writes the results into |signature| as a DER encoded
  // ECDSA-Sig-Value from RFC 3279.
  //
  //  ECDSA-Sig-Value ::= SEQUENCE {
  //    r     INTEGER,
  //    s     INTEGER }
  virtual bool Sign(base::span<const uint8_t> data,
                    std::vector<uint8_t>* signature) = 0;
};

}  // namespace crypto

#endif  // CRYPTO_EC_SIGNATURE_CREATOR_H_
