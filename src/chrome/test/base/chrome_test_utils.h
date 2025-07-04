// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_BASE_CHROME_TEST_UTILS_H_
#define CHROME_TEST_BASE_CHROME_TEST_UTILS_H_

#include "build/build_config.h"
#include "chrome/test/base/platform_browser_test.h"

namespace content {
class WebContents;
}
class Profile;

// This namespace contains test utilities that function for both Android and
// desktop browser tests.
namespace chrome_test_utils {

// Returns the active WebContents. On desktop this is in the first browser
// window created by tests, more specific behaviour requires other means.
// Takes a const PlatformBrowserTest so it can be called from other const
// methods:
// void MyConstMemberFunction() const {
//   auto* tab = chrome_test_utils::GetActiveWebContents(this);
//   ...
content::WebContents* GetActiveWebContents(
    const PlatformBrowserTest* browser_test);

// Returns the active Profile. On desktop this is in the first browser
// window created by tests, more specific behaviour requires other means.
Profile* GetProfile(const PlatformBrowserTest* browser_test);

// Returns the test data path used by the embedded test server.
base::FilePath GetChromeTestDataDir();

// Overrides the path chrome::DIR_TEST_DATA. Used early in test startup so the
// value is available in constructors and SetUp methods.
void OverrideChromeTestDataDir();

}  // namespace chrome_test_utils

#endif  // CHROME_TEST_BASE_CHROME_TEST_UTILS_H_
