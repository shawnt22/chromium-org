// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "components/crx_file/crx_creator.h"
#include "crypto/keypair.h"

// This program, invoked via `crx_build_action out zip key` constructs a CRX3 at
// `out` from the input zip archive at `zip` and signed with the key at `key`.
// The file at `key` should be a DER-formatted PKCS #8 PrivateKeyInfo block.
//
// Consult crx3.gni for more information.
int main(int argc, char** raw_argv) {
  UNSAFE_BUFFERS(
      // SAFETY: The OS guarantees that there are at least argc argument values
      // starting at raw_argv.
      base::span<char*> argv(raw_argv, base::checked_cast<size_t>(argc));)

  std::string key_file;
  if (!base::ReadFileToString(
          base::MakeAbsoluteFilePath(base::FilePath::FromASCII(argv[3])),
          &key_file)) {
    VLOG(0) << "Failed to read key material from " << argv[3];
    return -1;
  }
  return static_cast<int>(crx_file::Create(
      base::FilePath::FromASCII(argv[1]), base::FilePath::FromASCII(argv[2]),
      *crypto::keypair::PrivateKey::FromPrivateKeyInfo(
          base::as_byte_span(key_file))));
}
