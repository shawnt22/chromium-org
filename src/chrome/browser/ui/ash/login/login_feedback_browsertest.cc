// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/login/login_feedback.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "ash/webui/os_feedback_ui/url_constants.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/ash/login/login_manager_test.h"
#include "chrome/browser/ash/login/test/login_manager_mixin.h"
#include "chrome/browser/ash/login/test/oobe_base_test.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/webui/ash/os_feedback_dialog/os_feedback_dialog.h"
#include "chrome/browser/ui/webui/ash/system_web_dialog/system_web_dialog_delegate.h"
#include "chrome/browser/ui/webui/feedback/feedback_dialog.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {
namespace {

GURL GetFeedbackURL() {
  return GURL(kChromeUIOSFeedbackUrl);
}

bool HasInstanceOfOsFeedbackDialog() {
  return SystemWebDialogDelegate::HasInstance(GetFeedbackURL());
}

void TestOpenOsFeedbackDialog() {
  base::HistogramTester histogram_tester;
  Profile* const profile = ash::ProfileHelper::GetSigninProfile();
  auto login_feedback = std::make_unique<ash::LoginFeedback>(profile);
  // There should be none instance.
  EXPECT_FALSE(HasInstanceOfOsFeedbackDialog());

  base::test::TestFuture<void> test_future;
  // Open the feedback dialog.
  login_feedback->Request("Test feedback", test_future.GetCallback());
  EXPECT_TRUE(test_future.Wait());

  // Verify an instance exists now.
  EXPECT_TRUE(HasInstanceOfOsFeedbackDialog());
  histogram_tester.ExpectBucketCount("Feedback.RequestSource",
                                     feedback::kFeedbackSourceLogin, 1);
  histogram_tester.ExpectTotalCount("Feedback.RequestSource", 1);
}

class LoginFeedbackTest : public LoginManagerTest {
 public:
  LoginFeedbackTest() : LoginManagerTest() {
    login_mixin_.AppendRegularUsers(2);
  }

  LoginFeedbackTest(const LoginFeedbackTest&) = delete;
  LoginFeedbackTest& operator=(const LoginFeedbackTest&) = delete;

  ~LoginFeedbackTest() override = default;

 private:
  LoginManagerMixin login_mixin_{&mixin_host_};
};

void EnsureFeedbackAppUIShown(FeedbackDialog* feedback_dialog,
                              base::OnceClosure callback) {
  auto* widget = feedback_dialog->GetWidget();
  ASSERT_NE(nullptr, widget);
  if (widget->IsActive()) {
    std::move(callback).Run();
  } else {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&EnsureFeedbackAppUIShown, feedback_dialog,
                       std::move(callback)),
        base::Seconds(1));
  }
}

}  // namespace

// Test feedback UI shows up and is active on the Login Screen.
IN_PROC_BROWSER_TEST_F(LoginFeedbackTest, Basic) {
  TestOpenOsFeedbackDialog();
}

// Test feedback UI shows up and is active in OOBE.
IN_PROC_BROWSER_TEST_F(OobeBaseTest, Basic) {
  TestOpenOsFeedbackDialog();
}

}  // namespace ash
