// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/privacy_sandbox/privacy_sandbox_prompt_helper.h"

#include "base/hash/hash.h"
#include "base/metrics/histogram_functions.h"
#include "chrome/browser/privacy_sandbox/notice/desktop_entrypoint_handlers_helper.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_queue_manager.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/privacy_sandbox/privacy_sandbox_prompt.h"
#include "chrome/browser/ui/profiles/profile_customization_bubble_sync_controller.h"
#include "chrome/browser/ui/signin/signin_view_controller.h"
#include "chrome/common/extensions/chrome_manifest_url_handlers.h"
#include "chrome/common/webui_url_constants.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/sync/service/sync_service.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/extension_registry.h"

namespace {
constexpr char kPrivacySandboxPromptHelperEventHistogram[] =
    "Settings.PrivacySandbox.PromptHelperEvent2";
constexpr int kMinRequiredDialogHeight = 100;

// Gets the type of prompt that should be displayed for |profile|, this includes
// the possibility of no prompt being required.
PrivacySandboxService::PromptType GetRequiredPromptType(Profile* profile) {
  if (!profile || !profile->IsRegularProfile()) {
    return PrivacySandboxService::PromptType::kNone;
  }

  auto* privacy_sandbox_service =
      PrivacySandboxServiceFactory::GetForProfile(profile);
  if (!privacy_sandbox_service) {
    return PrivacySandboxService::PromptType::kNone;
  }

  return privacy_sandbox_service->GetRequiredPromptType(
      PrivacySandboxService::SurfaceType::kDesktop);
}

#if BUILDFLAG(IS_CHROMEOS)
bool HasExtensionNtpOverride(
    extensions::ExtensionRegistry* extension_registry) {
  for (const auto& extension : extension_registry->enabled_extensions()) {
    const auto& overrides =
        extensions::URLOverrides::GetChromeURLOverrides(extension.get());
    if (overrides.find(chrome::kChromeUINewTabHost) != overrides.end()) {
      return true;
    }
  }
  return false;
}

// Returns whether |url| is an NTP controlled entirely by Chrome.
bool IsChromeControlledNtpUrl(const GURL& url) {
  // Convert to origins for comparison, as any appended paths are irrelevant.
  const auto ntp_origin = url::Origin::Create(url);

  return ntp_origin ==
             url::Origin::Create(GURL(chrome::kChromeUINewTabPageURL)) ||
         ntp_origin == url::Origin::Create(
                           GURL(chrome::kChromeUINewTabPageThirdPartyURL));
}
#endif  // BUILDFLAG(IS_CHROMEOS)

}  // namespace

PrivacySandboxPromptHelper::~PrivacySandboxPromptHelper() = default;

PrivacySandboxPromptHelper::PrivacySandboxPromptHelper(
    content::WebContents* web_contents)
    : WebContentsObserver(web_contents),
      content::WebContentsUserData<PrivacySandboxPromptHelper>(*web_contents) {
  base::UmaHistogramEnumeration(
      kPrivacySandboxPromptHelperEventHistogram,
      SettingsPrivacySandboxPromptHelperEvent::kCreated);
}

void PrivacySandboxPromptHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!ProfileRequiresPrompt(profile())) {
    base::UmaHistogramEnumeration(
        kPrivacySandboxPromptHelperEventHistogram,
        SettingsPrivacySandboxPromptHelperEvent::kPromptNotRequired);
    return;
  }

  // Only valid top frame navigations are considered.
  if (!navigation_handle || !navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame() ||
      navigation_handle->IsSameDocument()) {
    base::UmaHistogramEnumeration(
        kPrivacySandboxPromptHelperEventHistogram,
        SettingsPrivacySandboxPromptHelperEvent::kNonTopFrameNavigation);
    return;
  }

#if BUILDFLAG(IS_CHROMEOS)
  // TODO(crbug.com/1315580, crbug.com/1315579): When navigating to a NTP that
  // isn't Chrome-controlled on ChromeOS, open an about blank tab to display the
  // prompt. On other platforms, it's being handled during the startup.
  if (web_contents()->GetLastCommittedURL() == chrome::kChromeUINewTabURL) {
    const bool has_extention_override =
        HasExtensionNtpOverride(extensions::ExtensionRegistry::Get(profile()));

    const GURL new_tab_page = search::GetNewTabPageURL(profile());
    const bool is_non_chrome_controlled_ntp =
        navigation_handle->GetURL() == new_tab_page &&
        !IsChromeControlledNtpUrl(new_tab_page);

    if (has_extention_override || is_non_chrome_controlled_ntp) {
      web_contents()->OpenURL(
          content::OpenURLParams(GURL(url::kAboutBlankURL), content::Referrer(),
                                 WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                 ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                 /*is_renderer_initiated=*/false),
          /*navigation_handle_callback=*/{});
      base::UmaHistogramEnumeration(
          kPrivacySandboxPromptHelperEventHistogram,
          SettingsPrivacySandboxPromptHelperEvent::kAboutBlankOpened);
      return;
    }
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  // Check whether the navigation target is a suitable prompt location. The
  // navigation URL, rather than the visible or committed URL, is required to
  // distinguish between different types of NTPs.
  if (!privacy_sandbox::IsUrlSuitableForPrompt(navigation_handle->GetURL())) {
    base::UmaHistogramEnumeration(
        kPrivacySandboxPromptHelperEventHistogram,
        SettingsPrivacySandboxPromptHelperEvent::kUrlNotSuitable);
    return;
  }

  // If a Sync setup is in progress, the prompt should not be shown.
  if (auto* sync_service = SyncServiceFactory::GetForProfile(profile())) {
    if (sync_service->IsSetupInProgress()) {
      base::UmaHistogramEnumeration(
          kPrivacySandboxPromptHelperEventHistogram,
          SettingsPrivacySandboxPromptHelperEvent::kSyncSetupInProgress);
      return;
    }
  }

  auto* browser =
      chrome::FindBrowserWithTab(navigation_handle->GetWebContents());

  // If a sign-in dialog is being currently displayed or is about to be
  // displayed, the prompt should not be shown to avoid conflict.
  // TODO(crbug.com/370806609): When we add sign in notice to queue, put this
  // behind flag / remove.
  bool signin_dialog_showing =
      browser->GetFeatures().signin_view_controller()->ShowsModalDialog();
#if !BUILDFLAG(IS_CHROMEOS)
  signin_dialog_showing =
      signin_dialog_showing ||
      IsProfileCustomizationBubbleSyncControllerRunning(browser);
#endif  // !BUILDFLAG(IS_CHROMEOS)
  if (signin_dialog_showing) {
    base::UmaHistogramEnumeration(
        kPrivacySandboxPromptHelperEventHistogram,
        SettingsPrivacySandboxPromptHelperEvent::kSigninDialogShown);
    return;
  }

  // If a Privacy Sandbox prompt already exists for this browser, do not attempt
  // to open another one.
  if (auto* privacy_sandbox_service =
          PrivacySandboxServiceFactory::GetForProfile(profile())) {
    if (privacy_sandbox_service->IsPromptOpenForBrowser(browser)) {
      base::UmaHistogramEnumeration(kPrivacySandboxPromptHelperEventHistogram,
                                    SettingsPrivacySandboxPromptHelperEvent::
                                        kPromptAlreadyExistsForBrowser);
      return;
    }
  }

  // The PrivacySandbox prompt can always fit inside a normal tabbed window due
  // to its minimum width, so checking the height is enough here. Other non
  // normal tabbed browsers will be exlcuded in a later check.
  const bool is_window_height_too_small =
      browser->window()
          ->GetWebContentsModalDialogHost()
          ->GetMaximumDialogSize()
          .height() < kMinRequiredDialogHeight;
  // If the windows height is too small, it is difficult to read or interact
  // with the dialog. The dialog is blocking modal, that is why we want to
  // prevent it from showing if there isn't enough space.
  if (is_window_height_too_small) {
    base::UmaHistogramEnumeration(
        kPrivacySandboxPromptHelperEventHistogram,
        SettingsPrivacySandboxPromptHelperEvent::kWindowTooSmall);
    return;
  }

  // Avoid showing the prompt on popups, pip, anything that isn't a normal
  // browser.
  if (browser->type() != Browser::TYPE_NORMAL) {
    base::UmaHistogramEnumeration(
        kPrivacySandboxPromptHelperEventHistogram,
        SettingsPrivacySandboxPromptHelperEvent::kNonNormalBrowser);
    return;
  }

  // If the handle is not being held, do not attempt to show the prompt.
  // We want to check this constraint at the very end for histogram emitting
  // reasons.
  if (auto* privacy_sandbox_service =
          PrivacySandboxServiceFactory::GetForProfile(profile())) {
    privacy_sandbox::PrivacySandboxQueueManager& queue_manager =
        privacy_sandbox_service->GetPrivacySandboxNoticeQueueManager();
    if (base::FeatureList::IsEnabled(
            privacy_sandbox::kPrivacySandboxNoticeQueue) &&
        !queue_manager.IsHoldingHandle()) {
      queue_manager.MaybeEmitQueueStateMetrics();
      return;
    }
  }

  // Record the URL that the prompt was displayed over.
  uint32_t host_hash = base::Hash(navigation_handle->GetURL().IsAboutBlank()
                                      ? "about:blank"
                                      : navigation_handle->GetURL().host());
  base::UmaHistogramSparse(
      "Settings.PrivacySandbox.DialogDisplayHost",
      static_cast<base::HistogramBase::Sample32>(host_hash));

  browser->tab_strip_model()->ActivateTabAt(
      browser->tab_strip_model()->GetIndexOfWebContents(
          navigation_handle->GetWebContents()));

  PrivacySandboxDialog::Show(browser, GetRequiredPromptType(profile()));
  base::UmaHistogramEnumeration(
      kPrivacySandboxPromptHelperEventHistogram,
      SettingsPrivacySandboxPromptHelperEvent::kPromptShown);
}

// static
bool PrivacySandboxPromptHelper::ProfileRequiresPrompt(Profile* profile) {
  bool eligible = GetRequiredPromptType(profile) !=
                  PrivacySandboxService::PromptType::kNone;

  // TODO(crbug.com/370804492): When we add DMA notice to queue, put this behind
  // flag / remove.
  SearchEngineChoiceDialogService* search_engine_choice_dialog_service =
      SearchEngineChoiceDialogServiceFactory::GetForProfile(profile);
  if (search_engine_choice_dialog_service &&
      search_engine_choice_dialog_service->CanSuppressPrivacySandboxPromo()) {
    base::UmaHistogramEnumeration(kPrivacySandboxPromptHelperEventHistogram,
                                  SettingsPrivacySandboxPromptHelperEvent::
                                      kSearchEngineChoiceDialogShown);
    eligible = false;
  }

  if (auto* privacy_sandbox_service =
          PrivacySandboxServiceFactory::GetForProfile(profile)) {
    privacy_sandbox::PrivacySandboxQueueManager& queue_manager =
        privacy_sandbox_service->GetPrivacySandboxNoticeQueueManager();
    // When checking profile eligibility also update the queue.
    // Case 1: Profile is eligible, but not in the queue. Add to queue.
    // Case 2: Profile is ineligible, but we are queued, so we must unqueue. OR
    //         We are holding the handle, so we must release the handle and
    //         prevent showing.
    eligible ? queue_manager.MaybeQueueNotice()
             : queue_manager.MaybeUnqueueNotice();
  }

  return eligible;
}

Profile* PrivacySandboxPromptHelper::profile() {
  return Profile::FromBrowserContext(web_contents()->GetBrowserContext());
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PrivacySandboxPromptHelper);
