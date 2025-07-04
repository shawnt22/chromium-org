// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_NTP_UI_BUNDLED_NEW_TAB_PAGE_UTILS_H_
#define IOS_CHROME_BROWSER_NTP_UI_BUNDLED_NEW_TAB_PAGE_UTILS_H_

#include "url/gurl.h"

// Whether the top of feed sync promo has met the criteria to be shown.
bool ShouldShowTopOfFeedSyncPromo();

// Retrieves the URL for the MIA web page.
GURL GetURLForMIA();

#endif  // IOS_CHROME_BROWSER_NTP_UI_BUNDLED_NEW_TAB_PAGE_UTILS_H_
