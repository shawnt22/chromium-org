// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_BASE_ASH_JAVASCRIPT_BROWSER_TEST_H_
#define CHROME_TEST_BASE_ASH_JAVASCRIPT_BROWSER_TEST_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "chrome/test/base/in_process_browser_test.h"

// A base class providing construction of javascript testing assets.
class JavaScriptBrowserTest : public InProcessBrowserTest {
 public:
  JavaScriptBrowserTest(const JavaScriptBrowserTest&) = delete;
  JavaScriptBrowserTest& operator=(const JavaScriptBrowserTest&) = delete;
  // Add a custom helper JS library for your test.
  // If a relative path is specified, it'll be read
  // as relative to the test data dir.
  void AddLibrary(const base::FilePath& library_path);

 protected:
  JavaScriptBrowserTest();

  ~JavaScriptBrowserTest() override;

  // InProcessBrowserTest overrides.
  void SetUpOnMainThread() override;

  // Builds a vector of javascript library contents for execution. Returns true
  // on success, false if any library fails to load or if `libraries` is null.
  bool BuildJavascriptLibraries(std::vector<std::u16string>* libraries);

  // Builds a string with a call to the runTest JS function, passing the
  // given |is_async|, |test_name| and its |args|.
  // This is then suitable for execution by subclasses in a
  // |RunJavaScriptBrowserTestF| call.
  std::u16string BuildRunTestJSCall(bool is_async,
                                    const std::string& test_name,
                                    base::Value::List args);

  Profile* GetProfile() const;

 private:
  // User added libraries.
  std::vector<base::FilePath> user_libraries_;

  // User library search paths.
  std::vector<base::FilePath> library_search_paths_;
};

#endif  // CHROME_TEST_BASE_ASH_JAVASCRIPT_BROWSER_TEST_H_
