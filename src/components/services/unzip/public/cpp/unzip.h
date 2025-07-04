// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SERVICES_UNZIP_PUBLIC_CPP_UNZIP_H_
#define COMPONENTS_SERVICES_UNZIP_PUBLIC_CPP_UNZIP_H_

#include <cstdint>

#include "base/functional/callback_forward.h"
#include "components/services/unzip/public/mojom/unzipper.mojom-forward.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "third_party/ced/src/util/encodings/encodings.h"

namespace base {
class FilePath;
}

namespace unzip {

// A type storing how to create Unzipper remotes, for dependency injection.
// This will typically be unzip::LaunchUnzipper, except in tests / iOS.
using UnzipperFactory =
    base::RepeatingCallback<mojo::PendingRemote<mojom::Unzipper>()>;

// Unzips files and directories in `zip_file` that match `filter_callback` into
// `output_dir`. Returns a closure that cancels the unzip operation when called.
// Unzip must be called on a sequenced task runner. The cancellation closure may
// be called on any sequence (or none at all). Unzip does not block.
// `result_callback` and `listener_callback` will run on the same sequence Unzip
// is called on. `filter_callback` may run on any sequence. If no filtration is
// needed, pass `unzip::AllContents` as `filter_callback`. If no progress
// tracking is needed, pass `base::DoNothing()` as `listener_callback`.
using UnzipCallback = base::OnceCallback<void(bool result)>;
using UnzipFilterCallback =
    base::RepeatingCallback<bool(const base::FilePath& path)>;
using UnzipListenerCallback = base::RepeatingCallback<void(uint64_t bytes)>;
base::OnceClosure Unzip(mojo::PendingRemote<mojom::Unzipper> unzipper,
                        const base::FilePath& zip_file,
                        const base::FilePath& output_dir,
                        mojom::UnzipOptionsPtr options,
                        UnzipFilterCallback filter_callback,
                        UnzipListenerCallback listener_callback,
                        UnzipCallback result_callback);

// Must be called on a sequenced task runner. `result_callback` will run on the
// same sequence.
using DetectEncodingCallback = base::OnceCallback<void(Encoding)>;
void DetectEncoding(mojo::PendingRemote<mojom::Unzipper> unzipper,
                    const base::FilePath& zip_file,
                    DetectEncodingCallback result_callback);

// Must be called on a sequenced task runner. `result_callback` will run on the
// same sequence.
using GetExtractedInfoCallback = base::OnceCallback<void(mojom::InfoPtr)>;
void GetExtractedInfo(mojo::PendingRemote<mojom::Unzipper> unzipper,
                      const base::FilePath& zip_file,
                      GetExtractedInfoCallback result_callback);

UnzipFilterCallback AllContents();

// Decodes an xz file at `in_file` and writes the results to `out_file`.
// Must be called on a sequenced task runner. `callback` will run on the
// same sequence, with `true` if and only if decoding is successful. `out_file`
// must not already exist. The `out_file` dirname must already exist. Returns a
// cancellation closure. The cancellation closure may be invoked on any sequence
// and will cancel the decoding operation, triggering `callback`.
base::OnceClosure DecodeXz(mojo::PendingRemote<mojom::Unzipper> unzipper,
                           const base::FilePath& in_file,
                           const base::FilePath& out_file,
                           base::OnceCallback<void(bool)> callback);

}  // namespace unzip

#endif  // COMPONENTS_SERVICES_UNZIP_PUBLIC_CPP_UNZIP_H_
