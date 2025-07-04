// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_customization_bubble_sync_controller.h"

#include "base/memory/raw_ptr.h"
#include "base/test/mock_callback.h"
#include "base/time/time.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_syncable_service.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/test/test_sync_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

namespace {

constexpr SkColor kNewProfileColor = SK_ColorRED;
constexpr SkColor kSyncedProfileColor = SK_ColorBLUE;
const char kTestingProfileName[] = "testing_profile";

class FakeThemeService : public ThemeService {
 public:
  explicit FakeThemeService(const ThemeHelper& theme_helper)
      : ThemeService(nullptr, theme_helper) {}

  void SetThemeSyncableService(ThemeSyncableService* theme_syncable_service) {
    theme_syncable_service_ = theme_syncable_service;
    set_ready();
  }

  // ThemeService:
  void DoSetTheme(const extensions::Extension* extension,
                  bool suppress_infobar) override {
    using_default_theme_ = false;
    color_ = 0;
    NotifyThemeChanged();
  }

  void BuildAutogeneratedThemeFromColor(SkColor color) override {
    color_ = color;
    using_default_theme_ = false;
    NotifyThemeChanged();
  }

  void UseTheme(ui::SystemTheme system_theme) override {
    if (system_theme == ui::SystemTheme::kDefault) {
      using_default_theme_ = true;
      color_ = 0;
    }
    NotifyThemeChanged();
  }

  bool UsingDefaultTheme() const override { return using_default_theme_; }

  void ClearThemeData(bool clear_ntp_background) override {}

  SkColor GetAutogeneratedThemeColor() const override { return color_; }

  ThemeSyncableService* GetThemeSyncableService() const override {
    return theme_syncable_service_;
  }

  void SetUserColorAndBrowserColorVariant(
      SkColor user_color,
      ui::mojom::BrowserColorVariant color_variant) override {
    NotifyThemeChanged();
  }

 private:
  raw_ptr<ThemeSyncableService> theme_syncable_service_ = nullptr;
  bool using_default_theme_ = true;
  SkColor color_ = 0;
};

class ProfileCustomizationBubbleSyncControllerTest : public testing::Test {
 public:
  using Outcome = ProfileCustomizationBubbleSyncController::Outcome;
  ProfileCustomizationBubbleSyncControllerTest()
      : testing_profile_manager_(TestingBrowserProcess::GetGlobal()),
        fake_theme_service_(theme_helper_) {}

  void SetUp() override {
    ASSERT_TRUE(testing_profile_manager_.SetUp());
    testing_profile_ =
        testing_profile_manager_.CreateTestingProfile(kTestingProfileName);

    Browser::CreateParams params(testing_profile_, /*user_gesture=*/true);
    params.window = &test_browser_window_;
    browser_ = Browser::DeprecatedCreateOwnedForTesting(params);

    theme_syncable_service_ = std::make_unique<ThemeSyncableService>(
        testing_profile_, &fake_theme_service_);
    fake_theme_service_.SetThemeSyncableService(theme_syncable_service_.get());
  }

  void TearDown() override {
    // This is to avoid UAF in the FakeThemeService, since
    // `theme_syncable_service_` is destroyed before `fake_theme_service_`.
    fake_theme_service_.SetThemeSyncableService(nullptr);
  }

  void ApplyColorAndShowBubbleWhenNoValueSynced(
      ProfileCustomizationBubbleSyncController::ShowBubbleCallback
          show_bubble_callback) {
    ProfileCustomizationBubbleSyncController::
        ApplyColorAndShowBubbleWhenNoValueSyncedForTesting(
            browser_.get(), &test_sync_service_, &fake_theme_service_,
            std::move(show_bubble_callback), kNewProfileColor);
  }

  void SetSyncedProfileColor() {
    fake_theme_service_.BuildAutogeneratedThemeFromColor(kSyncedProfileColor);
  }

  void SetSyncedProfileTheme() {
    fake_theme_service_.DoSetTheme(nullptr, false);
  }

  void CloseBrowser() { browser_.reset(); }

  void NotifyOnSyncStarted(bool waiting_for_extension_installation = false) {
    theme_syncable_service_->NotifyOnSyncStartedForTesting(
        waiting_for_extension_installation
            ? ThemeSyncableService::ThemeSyncState::
                  kWaitingForExtensionInstallation
            : ThemeSyncableService::ThemeSyncState::kApplied);
  }

 protected:
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  syncer::TestSyncService test_sync_service_;

 private:
  TestingProfileManager testing_profile_manager_;
  raw_ptr<Profile> testing_profile_ = nullptr;

  TestBrowserWindow test_browser_window_;
  std::unique_ptr<Browser> browser_;

  FakeThemeService fake_theme_service_;
  std::unique_ptr<ThemeSyncableService> theme_syncable_service_;
  ThemeHelper theme_helper_;
};

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldShowWhenSyncGetsDefaultTheme) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kShowBubble));

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  NotifyOnSyncStarted();
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldShowWhenSyncDisabled) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kShowBubble));

  test_sync_service_.SetAllowedByEnterprisePolicy(false);
  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldNotShowWhenSyncGetsCustomColor) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kSkipBubble));

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  SetSyncedProfileColor();
  NotifyOnSyncStarted();
}

// Regression test for crbug.com/1213109.
TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldNotShowWhenSyncGetsCustomColorBeforeStarting) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kSkipBubble));

  // Set up theme sync before the bubble controller gets created.
  SetSyncedProfileColor();
  NotifyOnSyncStarted();

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldNotShowWhenSyncGetsCustomTheme) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kSkipBubble));

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  SetSyncedProfileTheme();
  NotifyOnSyncStarted();
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldNotShowWhenSyncGetsCustomThemeToInstall) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kSkipBubble));

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  NotifyOnSyncStarted(/*waiting_for_extension_installation=*/true);
  SetSyncedProfileTheme();
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldNotShowWhenSyncHasCustomPasshrase) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kSkipBubble));

  test_sync_service_.SetPassphraseRequired();
  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  test_sync_service_.FireStateChanged();
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest, ShouldNotShowOnTimeout) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kSkipBubble));

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  task_environment_.FastForwardBy(base::Seconds(4));
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest,
       ShouldNotShowWhenProfileGetsDeleted) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> show_bubble;
  EXPECT_CALL(show_bubble, Run(Outcome::kAbort));

  ApplyColorAndShowBubbleWhenNoValueSynced(show_bubble.Get());
  CloseBrowser();
}

TEST_F(ProfileCustomizationBubbleSyncControllerTest, ShouldAbortIfCalledAgain) {
  base::MockCallback<base::OnceCallback<void(Outcome)>> old_show_bubble;
  EXPECT_CALL(old_show_bubble, Run(Outcome::kAbort));
  base::MockCallback<base::OnceCallback<void(Outcome)>> new_show_bubble;
  EXPECT_CALL(new_show_bubble, Run(Outcome::kShowBubble));

  ApplyColorAndShowBubbleWhenNoValueSynced(old_show_bubble.Get());
  ApplyColorAndShowBubbleWhenNoValueSynced(new_show_bubble.Get());

  NotifyOnSyncStarted();
}

}  // namespace
