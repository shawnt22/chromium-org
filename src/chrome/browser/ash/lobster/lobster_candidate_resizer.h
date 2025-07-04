// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOBSTER_LOBSTER_CANDIDATE_RESIZER_H_
#define CHROME_BROWSER_ASH_LOBSTER_LOBSTER_CANDIDATE_RESIZER_H_

#include <string>

#include "ash/public/cpp/lobster/lobster_image_candidate.h"
#include "ash/public/cpp/lobster/lobster_result.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ash/lobster/lobster_image_fetcher.h"

class LobsterCandidateResizer {
 public:
  explicit LobsterCandidateResizer(LobsterImageFetcher* image_fetcher);
  ~LobsterCandidateResizer();

  void InflateImage(uint32_t seed,
                    const std::string& query,
                    ash::InflateCandidateCallback callback);

 private:
  raw_ptr<LobsterImageFetcher> image_fetcher_;
};

#endif  // CHROME_BROWSER_ASH_LOBSTER_LOBSTER_CANDIDATE_RESIZER_H_
