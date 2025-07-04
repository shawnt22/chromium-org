// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/signin_error_handler.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/signin/signin_error_ui.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_web_ui.h"

namespace {
const char kSigninErrorLearnMoreUrl[] =
    "https://support.google.com/chrome/answer/1181420?";

class TestingSigninErrorHandler : public SigninErrorHandler {
 public:
  TestingSigninErrorHandler(Browser* browser,
                            bool is_system_profile,
                            content::WebUI* web_ui)
      : SigninErrorHandler(browser, is_system_profile) {
    set_web_ui(web_ui);
  }

  TestingSigninErrorHandler(const TestingSigninErrorHandler&) = delete;
  TestingSigninErrorHandler& operator=(const TestingSigninErrorHandler&) =
      delete;

  void CloseBrowserModalSigninDialog() override {
    browser_modal_dialog_did_close_ = true;
    SigninErrorHandler::CloseBrowserModalSigninDialog();
  }

  using SigninErrorHandler::HandleConfirm;
  using SigninErrorHandler::HandleInitializedWithSize;
  using SigninErrorHandler::HandleLearnMore;
  using SigninErrorHandler::HandleSwitchToExistingProfile;

  bool browser_modal_dialog_did_close() {
    return browser_modal_dialog_did_close_;
  }

 private:
  bool browser_modal_dialog_did_close_ = false;
};

class SigninErrorHandlerTest : public InProcessBrowserTest {
 public:
  SigninErrorHandlerTest()
      : web_ui_(new content::TestWebUI), handler_(nullptr) {}

  SigninErrorHandlerTest(const SigninErrorHandlerTest&) = delete;
  SigninErrorHandlerTest& operator=(const SigninErrorHandlerTest&) = delete;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL(chrome::kChromeUINewTabURL)));
    web_ui()->set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    signin_error_ui_ = std::make_unique<SigninErrorUI>(web_ui());
  }

  void TearDownOnMainThread() override {
    signin_error_ui_.reset();
    web_ui_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void CreateHandlerInBrowser() {
    DCHECK(!handler_);
    auto handler = std::make_unique<TestingSigninErrorHandler>(
        browser(), false /* is_system_profile */, web_ui());
    handler_ = handler.get();
    signin_error_ui_ = std::make_unique<SigninErrorUI>(web_ui());
    web_ui()->AddMessageHandler(std::move(handler));
  }

  void CreateHandlerInProfilePicker() {
    DCHECK(!handler_);
    auto handler = std::make_unique<TestingSigninErrorHandler>(
        nullptr /* browser */, true /* is_system_profile */, web_ui());
    handler_ = handler.get();
    web_ui()->AddMessageHandler(std::move(handler));
  }

  TestingSigninErrorHandler* handler() { return handler_; }

  content::TestWebUI* web_ui() { return web_ui_.get(); }

 private:
  std::unique_ptr<content::TestWebUI> web_ui_;
  std::unique_ptr<SigninErrorUI> signin_error_ui_;
  raw_ptr<TestingSigninErrorHandler, DanglingUntriaged> handler_;  // Not owned.
};

IN_PROC_BROWSER_TEST_F(SigninErrorHandlerTest, InBrowserHandleLearnMore) {
  // Before the test, there is only one new tab opened.
  TabStripModel* tab_strip_model = browser()->tab_strip_model();
  EXPECT_EQ(1, tab_strip_model->count());
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL),
            tab_strip_model->GetActiveWebContents()->GetVisibleURL());

  // Open learn more.
  CreateHandlerInBrowser();
  base::Value::List args;
  handler()->HandleLearnMore(args);

  // Dialog should be closed now.
  EXPECT_TRUE(handler()->browser_modal_dialog_did_close());

  // Verify that the learn more URL was opened.
  EXPECT_EQ(2, tab_strip_model->count());
  EXPECT_EQ(GURL(kSigninErrorLearnMoreUrl),
            tab_strip_model->GetActiveWebContents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(SigninErrorHandlerTest,
                       InBrowserHandleLearnMoreAfterBrowserRemoved) {
  // Before the test, there is only one new tab opened.
  TabStripModel* tab_strip_model = browser()->tab_strip_model();
  EXPECT_EQ(1, tab_strip_model->count());
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL),
            tab_strip_model->GetActiveWebContents()->GetVisibleURL());

  // Inform the handler that the browser was removed.
  CreateHandlerInBrowser();
  handler()->OnBrowserRemoved(browser());

  // Open learn more.
  base::Value::List args;
  handler()->HandleLearnMore(args);

  // Dialog is not closed if the browser was removed.
  EXPECT_FALSE(handler()->browser_modal_dialog_did_close());

  // Verify that the learn more URL was not opened as the browser was removed.
  EXPECT_EQ(1, tab_strip_model->count());
  EXPECT_EQ(GURL(chrome::kChromeUINewTabURL),
            tab_strip_model->GetActiveWebContents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(SigninErrorHandlerTest, InBrowserTestConfirm) {
  CreateHandlerInBrowser();
  base::Value::List args;
  handler()->HandleConfirm(args);

  // Confirm simply closes the dialog.
  EXPECT_TRUE(handler()->browser_modal_dialog_did_close());
}

}  // namespace
