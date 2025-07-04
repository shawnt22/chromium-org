// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/data_protection/data_protection_navigation_controller.h"

#include "base/feature_list.h"
#include "chrome/browser/enterprise/data_protection/data_protection_features.h"
#include "chrome/browser/enterprise/data_protection/data_protection_navigation_observer.h"
#include "chrome/browser/enterprise/watermark/settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/enterprise/watermarking/content/watermark_text_container.h"
#include "components/enterprise/watermarking/watermark.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace {

// The preferred way to fetch the browser pointer is using `FindBrowserWithTab`.
// However, there are some code paths where `TabHelpers` is constructed before
// the `WebContents` instance is attached to the tab. In the implementation
// below, we prioritize using the tab to obtain the `Browser` ptr, but fallback
// to using the profile to do so if that fails. This is a workaround that is
// required as long as the `DataProtectionNavigationController` is constructed
// by `TabHelpers`.
Browser* GetBrowser(content::WebContents* web_contents) {
  DCHECK(web_contents);
  Browser* browser = chrome::FindBrowserWithTab(web_contents);
  if (browser) {
    return browser;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile) {
    return nullptr;
  }
  return chrome::FindBrowserWithProfile(profile);
}
}  // namespace

namespace enterprise_data_protection {

DataProtectionNavigationController::DataProtectionNavigationController(
    tabs::TabInterface* tab_interface)
    : content::WebContentsObserver(nullptr), tab_interface_(tab_interface) {
  Observe(tab_interface->GetContents());
  tab_subscriptions_.push_back(tab_interface_->RegisterDidActivate(
      base::BindRepeating(&DataProtectionNavigationController::TabForegrounded,
                          weak_ptr_factory_.GetWeakPtr())));
  tab_subscriptions_.push_back(
      tab_interface_->RegisterWillDiscardContents(base::BindRepeating(
          &DataProtectionNavigationController::WillDiscardContents,
          weak_ptr_factory_.GetWeakPtr())));

  // Fetch the protection settings for the current page.
  enterprise_data_protection::DataProtectionNavigationObserver::
      ApplyDataProtectionSettings(
          Profile::FromBrowserContext(
              tab_interface_->GetContents()->GetBrowserContext()),
          tab_interface_->GetContents(),
          base::BindOnce(
              &DataProtectionNavigationController::ApplyDataProtectionSettings,
              weak_ptr_factory_.GetWeakPtr(),
              tab_interface_->GetContents()->GetWeakPtr()));

  // If there happens to be a navigation in process then that will be missed,
  // since DidStartNavigation does not trigger.
}

void DataProtectionNavigationController::SetCallbackForTesting(
    base::OnceClosure closure) {
  on_delay_apply_data_protection_settings_if_empty_called_for_testing_ =
      std::move(closure);
}

void DataProtectionNavigationController::TabForegrounded(
    tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  enterprise_data_protection::DataProtectionNavigationObserver::
      ApplyDataProtectionSettings(
          profile, contents,
          base::BindOnce(
              &DataProtectionNavigationController::ApplyDataProtectionSettings,
              weak_ptr_factory_.GetWeakPtr(), web_contents()->GetWeakPtr()));
}

void DataProtectionNavigationController::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  Browser* browser = GetBrowser(web_contents());
  if (!browser) {
    return;
  }

  auto navigation_observer = enterprise_data_protection::
      DataProtectionNavigationObserver::CreateForNavigationIfNeeded(
          this, browser->profile(), navigation_handle,
          base::BindOnce(&DataProtectionNavigationController::
                             ApplyDataProtectionSettingsOrDelayIfEmpty,
                         weak_ptr_factory_.GetWeakPtr(),
                         web_contents()->GetWeakPtr()));

  if (navigation_observer) {
    navigation_observers_.emplace(navigation_handle->GetNavigationId(),
                                  std::move(navigation_observer));
  }
}

void DataProtectionNavigationController::
    ApplyDataProtectionSettingsOrDelayIfEmpty(
        base::WeakPtr<content::WebContents> expected_web_contents,
        const enterprise_data_protection::UrlSettings& settings) {
  // If discarded, do nothing.
  if (!expected_web_contents || expected_web_contents.get() != web_contents()) {
    return;
  }

  // If the tab is in the background, do nothing.
  if (!tab_interface_->IsActivated()) {
    return;
  }

  Browser* browser = GetBrowser(web_contents());
  if (!browser) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }

#if BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)
  if (!settings.allow_screenshots) {
    browser_view->ApplyScreenshotSettings(settings.allow_screenshots);
  } else {
    // Screenshot protection should be cleared.  Delay that until the page
    // finishes loading.
    clear_screenshot_protection_on_page_load_ = true;
  }
#endif  // BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)

  // Regardless of whether watermark text is empty, attach it as web contents
  // user data so that other browser process code can draw watermarks outside
  // of the context of a navigation (ex. when printing).
  Profile* profile =
      Profile::FromBrowserContext(expected_web_contents->GetBrowserContext());
  enterprise_watermark::WatermarkBlock block =
      enterprise_watermark::DrawWatermarkToPaintRecord(
          settings.watermark_text,
          enterprise_watermark::GetFillColor(profile->GetPrefs()),
          enterprise_watermark::GetOutlineColor(profile->GetPrefs()));
  enterprise_watermark::WatermarkTextContainer::CreateForWebContents(
      expected_web_contents.get());
  enterprise_watermark::WatermarkTextContainer::FromWebContents(
      expected_web_contents.get())
      ->SetWatermarkText(
          block.record.ToSkPicture(SkRect::MakeWH(block.width, block.height)),
          block.width, block.height);

  if (!settings.watermark_text.empty()) {
    browser_view->ApplyWatermarkSettings(settings.watermark_text);
  } else {
    // The watermark string should be cleared.  Delay that until the page
    // finishes loading.
    clear_watermark_text_on_page_load_ = true;
  }

  if (!on_delay_apply_data_protection_settings_if_empty_called_for_testing_
           .is_null()) {
    std::move(
        on_delay_apply_data_protection_settings_if_empty_called_for_testing_)
        .Run();
  }
}

void DataProtectionNavigationController::ApplyDataProtectionSettings(
    base::WeakPtr<content::WebContents> expected_web_contents,
    const enterprise_data_protection::UrlSettings& settings) {
  // If the tab was discarded, do nothing.
  if (!expected_web_contents || web_contents() != expected_web_contents.get()) {
    return;
  }

  // If the tab is in the background, do nothing.
  if (!tab_interface_->IsActivated()) {
    return;
  }

  Browser* browser = GetBrowser(web_contents());
  if (!browser) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }

  browser_view->ApplyWatermarkSettings(settings.watermark_text);

#if BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)
  browser_view->ApplyScreenshotSettings(settings.allow_screenshots);
#endif  // BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)
}

void DataProtectionNavigationController::
    DocumentOnLoadCompletedInPrimaryMainFrame() {
  // It is possible for `clear_watermark_text_on_page_load_` to be set to false
  // even when the watermark should be cleared.  However, in this case there
  // is a queued call to `ApplyDataProtectionSettings()` which will correctly
  // reset the watermark.  The scenario is as followed:
  //
  // 1/ User is viewing a page in Tab A that is watermarked.
  // 2/ User loads a page that should not be watermarked into Tab A.
  // 3/ `DelayApplyDataProtectionSettingsIfEmpty()` is called at navigation
  //     finish time which sets clear_watermark_text_on_page_load_=true.
  //    `DocumentOnLoadCompletedInPrimaryMainFrame()` will be called later.
  // 4/ User switches to Tab B, which may or may not be watermarked.
  //    This calls `ApplyDataProtectionSettings()` setting the watermark
  //    appropriate to Tab B and sets clear_watermark_text_on_page_load_=false.
  // 5/ User switches back to Tab A (which shows a page that should not be
  //    watermarked, as described in step 2 above). This also calls
  //    `ApplyDataProtectionSettings()` setting the watermark
  //    appropriate to Tab A (i.e. clears the watermark) and sets
  //    clear_watermark_text_on_page_load_=false.
  // 6/ `DocumentOnLoadCompletedInPrimaryMainFrame()` is eventually called
  //    which does nothing because clear_watermark_text_on_page_load_==false.
  //    However, the watermark is already cleared in step #5.
  //
  // Note that steps #5 and #6 are racy but the final outcome is correct
  // regardless of the order in which they execute.

  if (!tab_interface_->IsActivated()) {
    return;
  }

  Browser* browser = GetBrowser(web_contents());
  if (!browser) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }
  if (clear_watermark_text_on_page_load_) {
    browser_view->ApplyWatermarkSettings(std::string());
    clear_watermark_text_on_page_load_ = false;
  }

#if BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)
  if (clear_screenshot_protection_on_page_load_) {
    browser_view->ApplyScreenshotSettings(true);
    clear_screenshot_protection_on_page_load_ = false;
  }
#endif
}

void DataProtectionNavigationController::Cleanup(int64_t navigation_id) {
  // Not all navigation IDs passed to this cleanup will have been added to the
  // map, DataProtectionNavigationObserver tracks all navigations that happen
  // during its lifetime.
  navigation_observers_.erase(navigation_id);
}

// Called when the associated tab will enter the background.
void DataProtectionNavigationController::WillDiscardContents(
    tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  WebContentsObserver::Observe(new_contents);
}

DataProtectionNavigationController::~DataProtectionNavigationController() =
    default;

}  // namespace enterprise_data_protection
