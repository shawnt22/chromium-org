// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/password_change_delegate_impl.h"

#include "base/test/metrics/histogram_tester.h"
#include "chrome/browser/optimization_guide/mock_optimization_guide_keyed_service.h"
#include "chrome/browser/optimization_guide/optimization_guide_keyed_service_factory.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/password_manager/password_change/change_password_form_finder.h"
#include "chrome/browser/ui/autofill/chrome_autofill_client.h"
#include "chrome/browser/ui/passwords/password_change_ui_controller.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/optimization_guide_prefs.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/mock_tab_interface.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

using ::testing::NiceMock;
using ::testing::Return;

constexpr char kChangePasswordURL[] = "https://example.com/password/";
const std::u16string kTestEmail = u"elisa.buckett@gmail.com";
const std::u16string kPassword = u"cE1L45Vgxyzlu8";

class MockPageNavigator : public content::PageNavigator {
 public:
  MOCK_METHOD(content::WebContents*,
              OpenURL,
              (const content::OpenURLParams&,
               base::OnceCallback<void(content::NavigationHandle&)>),
              (override));
};

class MockPasswordChangeUIController : public PasswordChangeUIController {
 public:
  MockPasswordChangeUIController(
      PasswordChangeDelegate* password_change_delegate)
      : PasswordChangeUIController(password_change_delegate, nullptr) {}
  ~MockPasswordChangeUIController() override = default;

  MOCK_METHOD(void, UpdateState, (PasswordChangeDelegate::State), (override));
};

}  // namespace

class PasswordChangeDelegateImplTest : public ChromeRenderViewHostTestHarness {
 public:
  PasswordChangeDelegateImplTest()
      : ChromeRenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  ~PasswordChangeDelegateImplTest() override = default;

  void FastForwardBy(base::TimeDelta delta) {
    task_environment()->FastForwardBy(delta);
  }

  PrefService* prefs() { return profile()->GetPrefs(); }
  MockPageNavigator& navigator() { return navigator_; }

  void SetOptimizationFeatureEnabled(bool enabled) {
    ON_CALL(*mock_optimization_guide_keyed_service_,
            ShouldFeatureBeCurrentlyEnabledForUser(
                optimization_guide::UserVisibleFeatureKey::
                    kPasswordChangeSubmission))
        .WillByDefault(Return(enabled));
  }

  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    mock_optimization_guide_keyed_service_ =
        static_cast<MockOptimizationGuideKeyedService*>(
            OptimizationGuideKeyedServiceFactory::GetInstance()
                ->SetTestingFactoryAndUse(
                    profile(),
                    base::BindRepeating([](content::BrowserContext* context)
                                            -> std::unique_ptr<KeyedService> {
                      return std::make_unique<
                          NiceMock<MockOptimizationGuideKeyedService>>();
                    })));
    tab_interface_ = std::make_unique<tabs::MockTabInterface>();
    ON_CALL(*tab_interface_, GetContents).WillByDefault(Return(web_contents()));
  }

  void TearDown() override {
    tab_interface_.reset();
    delegate_.reset();
    mock_optimization_guide_keyed_service_ = nullptr;
    ChromeRenderViewHostTestHarness::TearDown();
  }

  PasswordChangeDelegate* delegate() { return delegate_.get(); }

  void CreateDelegate() {
    delegate_ = std::make_unique<PasswordChangeDelegateImpl>(
        GURL(kChangePasswordURL), kTestEmail, kPassword, tab_interface_.get());
    delegate_->SetCustomUIController(
        std::make_unique<MockPasswordChangeUIController>(delegate_.get()));
  }

  void ResetDelegate() { delegate_.reset(); }

 private:
  raw_ptr<MockOptimizationGuideKeyedService>
      mock_optimization_guide_keyed_service_;
  MockPageNavigator navigator_;
  std::unique_ptr<tabs::MockTabInterface> tab_interface_;
  std::unique_ptr<PasswordChangeDelegateImpl> delegate_;
};

TEST_F(PasswordChangeDelegateImplTest, WaitingForAgreement) {
  CreateDelegate();
  EXPECT_EQ(
      prefs()->GetInteger(optimization_guide::prefs::GetSettingEnabledPrefName(
          optimization_guide::UserVisibleFeatureKey::
              kPasswordChangeSubmission)),
      static_cast<int>(
          optimization_guide::prefs::FeatureOptInState::kNotInitialized));

  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForAgreement);

  delegate()->OnPrivacyNoticeAccepted();
  SetOptimizationFeatureEnabled(true);
  // Both pref and state reflect acceptance.
  EXPECT_EQ(
      prefs()->GetInteger(optimization_guide::prefs::GetSettingEnabledPrefName(
          optimization_guide::UserVisibleFeatureKey::
              kPasswordChangeSubmission)),
      static_cast<int>(optimization_guide::prefs::FeatureOptInState::kEnabled));
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForChangePasswordForm);
}

TEST_F(PasswordChangeDelegateImplTest, PasswordChangeFormNotFound) {
  SetOptimizationFeatureEnabled(true);
  CreateDelegate();
  base::HistogramTester histogram_tester;

  delegate()->StartPasswordChangeFlow();

  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForChangePasswordForm);

  static_cast<PasswordChangeDelegateImpl*>(delegate())
      ->form_finder()
      ->RespondWithFormNotFound();

  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kChangePasswordFormNotFound);
  ResetDelegate();
  histogram_tester.ExpectUniqueSample(
      PasswordChangeDelegateImpl::kFinalPasswordChangeStatusHistogram,
      PasswordChangeDelegate::State::kChangePasswordFormNotFound, 1);
}

TEST_F(PasswordChangeDelegateImplTest, MetricsReportedFlowOffered) {
  SetOptimizationFeatureEnabled(true);
  CreateDelegate();
  base::HistogramTester histogram_tester;

  ResetDelegate();
  histogram_tester.ExpectUniqueSample(
      PasswordChangeDelegateImpl::kFinalPasswordChangeStatusHistogram,
      PasswordChangeDelegate::State::kOfferingPasswordChange, 1);
}

TEST_F(PasswordChangeDelegateImplTest,
       MetricsReportedFlowCanceledInPrivacyNotice) {
  SetOptimizationFeatureEnabled(false);
  CreateDelegate();
  base::HistogramTester histogram_tester;

  ResetDelegate();
  histogram_tester.ExpectUniqueSample(
      PasswordChangeDelegateImpl::kFinalPasswordChangeStatusHistogram,
      PasswordChangeDelegate::State::kWaitingForAgreement, 1);
}

TEST_F(PasswordChangeDelegateImplTest,
       MetricsReportedFlowCanceledDuringSignInCheck) {
  SetOptimizationFeatureEnabled(true);
  CreateDelegate();
  base::HistogramTester histogram_tester;
  delegate()->StartPasswordChangeFlow();

  ResetDelegate();
  histogram_tester.ExpectUniqueSample(
      PasswordChangeDelegateImpl::kFinalPasswordChangeStatusHistogram,
      PasswordChangeDelegate::State::kWaitingForChangePasswordForm, 1);
}

TEST_F(PasswordChangeDelegateImplTest,
       OtpDetectionIgnoredWhenPasswordChangeNotStarted) {
  SetOptimizationFeatureEnabled(true);
  CreateDelegate();
  ASSERT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kOfferingPasswordChange);

  delegate()->OnOtpFieldDetected(web_contents());
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kOfferingPasswordChange);
}

TEST_F(PasswordChangeDelegateImplTest,
       OtpDetectionIgnoredWhenWaitingForAgreement) {
  CreateDelegate();
  ASSERT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForAgreement);

  delegate()->OnOtpFieldDetected(
      static_cast<PasswordChangeDelegateImpl*>(delegate())->executor());
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForAgreement);
}

TEST_F(PasswordChangeDelegateImplTest, OtpDetectionIgnoredOnOriginalTab) {
  SetOptimizationFeatureEnabled(true);
  CreateDelegate();
  delegate()->StartPasswordChangeFlow();
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForChangePasswordForm);

  delegate()->OnOtpFieldDetected(web_contents());
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForChangePasswordForm);
}

TEST_F(PasswordChangeDelegateImplTest, OtpDetectionProcessed) {
  SetOptimizationFeatureEnabled(true);
  CreateDelegate();
  delegate()->StartPasswordChangeFlow();
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kWaitingForChangePasswordForm);

  delegate()->OnOtpFieldDetected(
      static_cast<PasswordChangeDelegateImpl*>(delegate())->executor());
  EXPECT_EQ(delegate()->GetCurrentState(),
            PasswordChangeDelegate::State::kOtpDetected);
}
