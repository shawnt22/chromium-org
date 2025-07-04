// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/android/auto_signin_first_run_dialog_android.h"

#include "base/android/jni_android.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/browser/password_manager/password_manager_settings_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/trusted_vault/trusted_vault_service_factory.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/password_manager/core/browser/mock_password_manager_settings_service.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

class AutoSigninFirstRunDialogAndroidTest
    : public ChromeRenderViewHostTestHarness {
 public:
  AutoSigninFirstRunDialogAndroidTest() = default;

  AutoSigninFirstRunDialogAndroidTest(
      const AutoSigninFirstRunDialogAndroidTest&) = delete;
  AutoSigninFirstRunDialogAndroidTest& operator=(
      const AutoSigninFirstRunDialogAndroidTest&) = delete;

  ~AutoSigninFirstRunDialogAndroidTest() override = default;

  PrefService* prefs();

  password_manager::MockPasswordManagerSettingsService*
  password_settings_service() {
    Profile* profile =
        Profile::FromBrowserContext(web_contents()->GetBrowserContext());
    return static_cast<password_manager::MockPasswordManagerSettingsService*>(
        PasswordManagerSettingsServiceFactory::GetForProfile(profile));
  }

 protected:
  AutoSigninFirstRunDialogAndroid* CreateDialog();

  TestingProfile::TestingFactories GetTestingFactories() const override {
    return {
        TestingProfile::TestingFactory{
            TrustedVaultServiceFactory::GetInstance(),
            TrustedVaultServiceFactory::GetDefaultFactory()},
        TestingProfile::TestingFactory{SyncServiceFactory::GetInstance(),
                                       SyncServiceFactory::GetDefaultFactory()},
        TestingProfile::TestingFactory{
            PasswordManagerSettingsServiceFactory::GetInstance(),
            base::BindRepeating(
                [](content::BrowserContext*) -> std::unique_ptr<KeyedService> {
                  return std::make_unique<
                      password_manager::MockPasswordManagerSettingsService>();
                })},
    };
  }
};

AutoSigninFirstRunDialogAndroid*
AutoSigninFirstRunDialogAndroidTest::CreateDialog() {
  return new AutoSigninFirstRunDialogAndroid(web_contents());
}

PrefService* AutoSigninFirstRunDialogAndroidTest::prefs() {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  return profile->GetPrefs();
}

TEST_F(AutoSigninFirstRunDialogAndroidTest,
       CheckPrefValueAfterFirstRunMessageWasShown) {
  prefs()->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false);
  AutoSigninFirstRunDialogAndroid* dialog = CreateDialog();
  dialog->Destroy(base::android::AttachCurrentThread(), nullptr);
  EXPECT_FALSE(prefs()->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown));
}

TEST_F(AutoSigninFirstRunDialogAndroidTest,
       CheckResetOfPrefAfterFirstRunMessageWasShownOnTurnOkClicked) {
  base::HistogramTester histogram_tester;
  prefs()->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false);
  EXPECT_CALL(*password_settings_service(), TurnOffAutoSignIn).Times(0);
  AutoSigninFirstRunDialogAndroid* dialog = CreateDialog();
  dialog->OnOkClicked(base::android::AttachCurrentThread(), nullptr);
  dialog->Destroy(base::android::AttachCurrentThread(), nullptr);
  EXPECT_TRUE(prefs()->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown));
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.AutoSigninFirstRunDialog",
      password_manager::metrics_util::AUTO_SIGNIN_OK_GOT_IT, 1);
}

TEST_F(AutoSigninFirstRunDialogAndroidTest,
       CheckResetOfPrefAfterFirstRunMessageWasShownOnTurnOffClicked) {
  base::HistogramTester histogram_tester;
  prefs()->SetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown, false);
  EXPECT_CALL(*password_settings_service(), TurnOffAutoSignIn);
  AutoSigninFirstRunDialogAndroid* dialog = CreateDialog();
  dialog->OnTurnOffClicked(base::android::AttachCurrentThread(), nullptr);
  dialog->Destroy(base::android::AttachCurrentThread(), nullptr);
  EXPECT_TRUE(prefs()->GetBoolean(
      password_manager::prefs::kWasAutoSignInFirstRunExperienceShown));
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.AutoSigninFirstRunDialog",
      password_manager::metrics_util::AUTO_SIGNIN_TURN_OFF, 1);
}
