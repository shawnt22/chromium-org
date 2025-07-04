// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/public/group_list.h"

#include <vector>

#include "build/build_config.h"
#include "components/feature_engagement/public/group_constants.h"

namespace feature_engagement {

namespace {
const base::Feature* const kAllGroups[] = {
    &kIPHDummyGroup,  // Ensures non-empty array for all platforms.
#if BUILDFLAG(IS_IOS)
    &kiOSFullscreenPromosGroup,
    &kiOSDefaultBrowserPromosGroup,
    &kiOSTailoredDefaultBrowserPromosGroup,
    &kiOSTailoredNonModalDefaultBrowserPromosGroup,
    &kiOSNonModalSigninPromosGroup,
    &kiOSHomepageNewBadgesGroup,
#endif  // BUILDFLAG(IS_IOS)
#if BUILDFLAG(IS_ANDROID)
    &kClankDefaultBrowserPromosGroup,
#endif  // BUILDFLAG(IS_ANDROID)
};
}  // namespace

std::vector<const base::Feature*> GetAllGroups() {
  return std::vector<const base::Feature*>(std::begin(kAllGroups),
                                           std::end(kAllGroups));
}

}  // namespace feature_engagement
