// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/test/accessibility_notification_waiter.h"
#include "content/public/test/browser_test.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/accessibility/accessibility_manager.h"
#include "chrome/browser/ash/accessibility/chromevox_test_utils.h"
#include "chrome/browser/ash/accessibility/speech_monitor.h"
#include "extensions/browser/browsertest_util.h"
#include "extensions/common/constants.h"
#else
#include <optional>

#include "content/public/test/scoped_accessibility_mode_override.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/accessibility/accessibility_labels_service.h"
#include "chrome/browser/accessibility/accessibility_labels_service_factory.h"
#endif

class AccessibilityLabelsBrowserTest : public InProcessBrowserTest {
 public:
  AccessibilityLabelsBrowserTest() = default;

  AccessibilityLabelsBrowserTest(const AccessibilityLabelsBrowserTest&) =
      delete;
  AccessibilityLabelsBrowserTest& operator=(
      const AccessibilityLabelsBrowserTest&) = delete;

  // InProcessBrowserTest overrides:
  void TearDownOnMainThread() override { EnableScreenReader(false); }

  void EnableScreenReader(bool enabled) {
#if BUILDFLAG(IS_CHROMEOS)
    if (!enabled) {
      ash::AccessibilityManager::Get()->EnableSpokenFeedback(false);
      return;
    }

    chromevox_test_utils_ = std::make_unique<ash::ChromeVoxTestUtils>();
    chromevox_test_utils_->EnableChromeVox();
    // Note: we can safely call `Replay` here since none of these tests make
    // speech assertions.
    chromevox_test_utils_->sm()->Replay();
#else
    // Spoof a screen reader.
    if (!enabled) {
      screen_reader_override_.reset();
    } else if (!screen_reader_override_) {
      screen_reader_override_.emplace(ui::AXMode::kWebContents |
                                      ui::AXMode::kExtendedProperties);
    }
#endif  // BUILDFLAG(IS_CHROMEOS)
  }

 private:
#if BUILDFLAG(IS_CHROMEOS)
  std::unique_ptr<ash::ChromeVoxTestUtils> chromevox_test_utils_;
#else
  std::optional<content::ScopedAccessibilityModeOverride>
      screen_reader_override_;
#endif
};

// Changing the kAccessibilityImageLabelsEnabled pref should affect the
// accessibility mode of a new WebContents for this profile.
IN_PROC_BROWSER_TEST_F(AccessibilityLabelsBrowserTest, NewWebContents) {
  EnableScreenReader(true);
  ui::AXMode ax_mode =
      content::BrowserAccessibilityState::GetInstance()->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  chrome::NewTab(browser());
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, true);

  chrome::NewTab(browser());
  web_contents = browser()->tab_strip_model()->GetActiveWebContents();
  // Wait for ChromeVox to attach to the new tab if needed.
  if (!web_contents->GetAccessibilityMode().has_mode(
          ui::AXMode::kExtendedProperties)) {
    content::AccessibilityNotificationWaiter waiter(web_contents);
    ASSERT_TRUE(waiter.WaitForNotification());
  }
  ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_TRUE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, false);

  chrome::NewTab(browser());
  web_contents = browser()->tab_strip_model()->GetActiveWebContents();
  // Wait for ChromeVox to attach to the new tab if needed.
  if (!web_contents->GetAccessibilityMode().has_mode(
          ui::AXMode::kExtendedProperties)) {
    content::AccessibilityNotificationWaiter waiter(web_contents);
    ASSERT_TRUE(waiter.WaitForNotification());
  }
  ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));
}

// Changing the kAccessibilityImageLabelsEnabled pref should affect the
// accessibility mode of existing WebContents in this profile.
IN_PROC_BROWSER_TEST_F(AccessibilityLabelsBrowserTest, ExistingWebContents) {
  EnableScreenReader(true);
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui::AXMode ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, true);

  ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_TRUE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, false);

  ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));
}

#if !BUILDFLAG(IS_ANDROID)
IN_PROC_BROWSER_TEST_F(AccessibilityLabelsBrowserTest, EnableOnce) {
  EnableScreenReader(true);
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui::AXMode ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  Profile* const profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  auto* const labels_service =
      AccessibilityLabelsServiceFactory::GetForProfile(profile);
  labels_service->EnableLabelsServiceOnce(web_contents);

  // EnableOnce does not change the mode flags for the WebContents, so it's not
  // trivial to verify that the change took place.
}
#endif

IN_PROC_BROWSER_TEST_F(AccessibilityLabelsBrowserTest,
                       NotEnabledWithoutScreenReader) {
  EnableScreenReader(false);
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui::AXMode ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, true);

  ax_mode = web_contents->GetAccessibilityMode();
  EXPECT_FALSE(ax_mode.has_mode(ui::AXMode::kLabelImages));

  // Reset state.
  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, false);
}

// Turning on the preference while a screenreader is present should enable the
// feature for existing tabs.
IN_PROC_BROWSER_TEST_F(AccessibilityLabelsBrowserTest,
                       PRE_EnabledByPreference) {
  EnableScreenReader(true);

  // The preference is not yet set, so the feature is off.
  auto* const web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_FALSE(
      web_contents->GetAccessibilityMode().has_mode(ui::AXMode::kLabelImages));

  browser()->profile()->GetPrefs()->SetBoolean(
      prefs::kAccessibilityImageLabelsEnabled, true);

  // Now the feature is on.
  EXPECT_TRUE(
      web_contents->GetAccessibilityMode().has_mode(ui::AXMode::kLabelImages));
}

// When the preference is present at startup, the feature should become enabled
// when a screenreader is discovered.
IN_PROC_BROWSER_TEST_F(AccessibilityLabelsBrowserTest, EnabledByPreference) {
  // The preference was set for the profile by PRE_EnabledByPreference.
  ASSERT_TRUE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kAccessibilityImageLabelsEnabled));

  auto* const web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // If the test is run without --force-renderer-accessibility, then no screen
  // reader should have been detected yet, and the feature should be off.
  if (!content::BrowserAccessibilityState::GetInstance()
           ->GetAccessibilityMode()
           .has_mode(ui::AXMode::kExtendedProperties)) {
    EXPECT_FALSE(web_contents->GetAccessibilityMode().has_mode(
        ui::AXMode::kLabelImages));

    EnableScreenReader(true);
  }

  // Now the feature is on.
  EXPECT_TRUE(
      web_contents->GetAccessibilityMode().has_mode(ui::AXMode::kLabelImages));
}
