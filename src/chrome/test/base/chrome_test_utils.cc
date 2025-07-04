// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/chrome_test_utils.h"

#include "base/path_service.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"

#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"
#else
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#endif

namespace chrome_test_utils {

content::WebContents* GetActiveWebContents(
    const PlatformBrowserTest* browser_test) {
#if BUILDFLAG(IS_ANDROID)
  for (const TabModel* model : TabModelList::models()) {
    if (model->IsActiveModel())
      return model->GetActiveWebContents();
  }
  NOTREACHED() << "No active TabModel??";
#else
  return browser_test->browser()->tab_strip_model()->GetActiveWebContents();
#endif
}

Profile* GetProfile(const PlatformBrowserTest* browser_test) {
#if BUILDFLAG(IS_ANDROID)
  for (const TabModel* model : TabModelList::models()) {
    if (model->IsActiveModel())
      return model->GetProfile();
  }
  NOTREACHED() << "No active TabModel??";
#else
  return browser_test->browser()->profile();
#endif
}

base::FilePath GetChromeTestDataDir() {
  return base::FilePath(FILE_PATH_LITERAL("chrome/test/data"));
}

void OverrideChromeTestDataDir() {
  base::FilePath src_dir;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_dir));
  CHECK(base::PathService::Override(chrome::DIR_TEST_DATA,
                                    src_dir.Append(GetChromeTestDataDir())));
}

}  // namespace chrome_test_utils
