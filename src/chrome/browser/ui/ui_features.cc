// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ui_features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "components/search/ntp_features.h"
#include "components/variations/service/variations_service.h"
#include "components/webui/flags/feature_entry.h"
#include "ui/base/ui_base_features.h"

namespace features {

// Enables the tab dragging fallback when full window dragging is not supported
// by the platform (e.g. Wayland). See https://crbug.com/896640
BASE_FEATURE(kAllowWindowDragUsingSystemDragDrop,
             "AllowWindowDragUsingSystemDragDrop",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables the use of WGC for the Eye Dropper screen capture.
BASE_FEATURE(kAllowEyeDropperWGCScreenCapture,
             "AllowEyeDropperWGCScreenCapture",
#if BUILDFLAG(IS_WIN)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif  // BUILDFLAG(IS_WIN)
);

// When enabled, clicks outside the omnibox and its popup will close an open
// omnibox popup.
BASE_FEATURE(kCloseOmniboxPopupOnInactiveAreaClick,
             "CloseOmniboxPopupOnInactiveAreaClick",
             base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// Enables the feature to remove the last confirmation dialog when relaunching
// to update Chrome.
BASE_FEATURE(kFewerUpdateConfirmations,
             "FewerUpdateConfirmations",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)

BASE_FEATURE(kExtensionsCollapseMainMenu,
             "ExtensionsCollapseMainMenu",
             base::FEATURE_DISABLED_BY_DEFAULT);

#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(IS_WIN)
BASE_FEATURE(kOfferPinToTaskbarWhenSettingToDefault,
             "OfferPinToTaskbarWhenSettingDefault",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kOfferPinToTaskbarInFirstRunExperience,
             "OfferPinToTaskbarInFirstRunExperience",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kOfferPinToTaskbarInfoBar,
             "OfferPinToTaskbarInfoBar",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
// Shows an infobar on PDFs offering to become the default PDF viewer if Chrome
// isn't the default already.
BASE_FEATURE(kPdfInfoBar, "PdfInfoBar", base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

// Preloads a WebContents with a Top Chrome WebUI on BrowserView initialization,
// so that it can be shown instantly at a later time when necessary.
BASE_FEATURE(kPreloadTopChromeWebUI,
             "PreloadTopChromeWebUI",
             base::FEATURE_ENABLED_BY_DEFAULT);

// An experiment to reduce the number of navigations when preloading WebUIs.
BASE_FEATURE(kPreloadTopChromeWebUILessNavigations,
             "PreloadTopChromeWebUILessNavigations",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables exiting browser fullscreen (users putting the browser itself into the
// fullscreen mode via the browser UI or shortcuts) with press-and-hold Esc.
BASE_FEATURE(kPressAndHoldEscToExitBrowserFullscreen,
             "PressAndHoldEscToExitBrowserFullscreen",
             base::FEATURE_ENABLED_BY_DEFAULT);

// When enabled, a scrim is shown behind window modal dialogs to cover the
// entire browser window. This gives user a visual cue that the browser window
// is not interactable.
BASE_FEATURE(kScrimForBrowserWindowModal,
             "ScrimForBrowserWindowModal",
             base::FEATURE_ENABLED_BY_DEFAULT);

// When enabled, a scrim is shown behind tab modal dialogs to cover the content
// area. This gives user a visual cue that the content area is not interactable.
BASE_FEATURE(KScrimForTabModal,
             "ScrimForTabModal",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kSideBySide, "SideBySide", base::FEATURE_DISABLED_BY_DEFAULT);

// The delay before showing the drop target for the side-by-side drag-and-drop
// entrypoint.
BASE_FEATURE_PARAM(base::TimeDelta,
                   kSideBySideShowDropTargetDelay,
                   &kSideBySide,
                   "drop_target_show_delay",
                   base::Seconds(1));

// The padding inside the drop target that determines the overall width.
BASE_FEATURE_PARAM(int,
                   kSideBySideDropTargetInnerPadding,
                   &kSideBySide,
                   "drop_target_inner_padding",
                   37);

constexpr base::FeatureParam<MiniToolbarActiveConfiguration>::Option
    kMiniToolbarActiveConfigurationOptions[] = {
        {MiniToolbarActiveConfiguration::Hide, "hide"},
        {MiniToolbarActiveConfiguration::ShowMenuOnly, "showmenuonly"},
        {MiniToolbarActiveConfiguration::ShowAll, "showall"}};

// The active configuration for the mini toolbar on active view of a split.
BASE_FEATURE_ENUM_PARAM(MiniToolbarActiveConfiguration,
                        kSideBySideMiniToolbarActiveConfiguration,
                        &kSideBySide,
                        "mini_toolbar_active_config",
                        MiniToolbarActiveConfiguration::Hide,
                        &kMiniToolbarActiveConfigurationOptions);

BASE_FEATURE(kSideBySideLinkMenuNewBadge,
             "SideBySideLinkMenuNewBadge",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsNtpFooterEnabledWithoutSideBySide() {
  return (base::FeatureList::IsEnabled(ntp_features::kNtpFooter) &&
          !base::FeatureList::IsEnabled(features::kSideBySide));
}

BASE_FEATURE(kSidePanelResizing,
             "SidePanelResizing",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabDuplicateMetrics,
             "TabDuplicateMetrics",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables buttons when scrolling the tabstrip https://crbug.com/951078
BASE_FEATURE(kTabScrollingButtonPosition,
             "TabScrollingButtonPosition",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables tabs to be frozen when collapsed.
// https://crbug.com/1110108
BASE_FEATURE(kTabGroupsCollapseFreezing,
             "TabGroupsCollapseFreezing",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables preview images in tab-hover cards.
// https://crbug.com/928954
BASE_FEATURE(kTabHoverCardImages,
             "TabHoverCardImages",
#if BUILDFLAG(IS_MAC)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);

BASE_FEATURE(kTabOrganization,
             "TabOrganization",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsTabOrganization() {
  return base::FeatureList::IsEnabled(features::kTabOrganization);
}

BASE_FEATURE(kTabstripDeclutter,
             "TabstripDeclutter",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsTabstripDeclutterEnabled() {
  return base::FeatureList::IsEnabled(features::kTabstripDeclutter);
}

BASE_FEATURE(kTabstripDedupe,
             "TabstripDedupe",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsTabstripDedupeEnabled() {
  return IsTabstripDeclutterEnabled() &&
         base::FeatureList::IsEnabled(features::kTabstripDedupe);
}

BASE_FEATURE(kTabOrganizationAppMenuItem,
             "TabOrganizationAppMenuItem",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTabOrganizationModelStrategy,
             "TabOrganizationModelStrategy",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabOrganizationUserInstruction,
             "TabOrganizationUserInstruction",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabOrganizationEnableNudgeForEnterprise,
             "TabOrganizationEnableNudgeForEnterprise",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables creating a web app window when tearing off a tab with a url
// controlled by a web app.
BASE_FEATURE(kTearOffWebAppTabOpensWebAppWindow,
             "TearOffWebAppTabOpensWebAppWindow",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if !BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kThreeButtonPasswordSaveDialog,
             "ThreeButtonPasswordSaveDialog",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

#if !defined(ANDROID)
BASE_FEATURE(kPinnedCastButton,
             "PinnedCastButton",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

// Enables enterprise profile badging for managed profiles on the toolbar
// avatar. On managed profiles, a "Work" or "School" label will be used in the
// toolbar.
BASE_FEATURE(kEnterpriseProfileBadgingForAvatar,
             "EnterpriseProfileBadgingForAvatar",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables enterprise profile badging for managed profiles on the toolbar avatar
// and in the profile menu. On managed profiles, a building icon will be used as
// a badge in the profile menu.
BASE_FEATURE(kEnterpriseProfileBadgingForMenu,
             "EnterpriseProfileBadgingForMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables enterprise profile badging for managed profiles on the toolbar avatar
// and in the profile menu when the policies are set. This acts as a kill
// switch. This has no effect if `kEnterpriseProfileBadging` is enabled.
BASE_FEATURE(kEnterpriseProfileBadgingPolicies,
             "EnterpriseProfileBadgingPolicies",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables enterprise badging for managed bnpwser on the new tab page footer.
// On managed browsers, a building icon and "Managed by <domain>" string will be
// shown in the footer, unless the icon and label are customized by the admin.
BASE_FEATURE(kEnterpriseBadgingForNtpFooter,
             "EnterpriseBadgingForNtpFooter",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables the management notice in the NTP footer if the custom policies are
// set. This acts as a kill switch for "EnterpriseCustomLabelForBrowser" and
// "EnterpriseLogoUrlForBrowser".
BASE_FEATURE(kNTPFooterBadgingPolicies,
             "NTPFooterBadgingPolicies",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables showing the EnterpriseCustomLabel` instead of the cloud policy
// manager in the managed disclaimer "Managed by..." in the profile and app
// menus.
BASE_FEATURE(kEnterpriseManagementDisclaimerUsesCustomLabel,
             "EnterpriseManagementDisclaimerUsesCustomLabel",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kEnterpriseUpdatedProfileCreationScreen,
             "EnterpriseUpdatedProfileCreationScreen",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kManagedProfileRequiredInterstitial,
             "ManagedProfileRequiredInterstitial",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kEnableAppMenuButtonColorsForDefaultAvatarButtonStates,
             "EnableAppMenuButtonColorsForDefaultAvatarButtonStates",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables a web-based tab strip. See https://crbug.com/989131. Note this
// feature only works when the ENABLE_WEBUI_TAB_STRIP buildflag is enabled.
BASE_FEATURE(kWebUITabStrip,
             "WebUITabStrip",
#if BUILDFLAG(IS_CHROMEOS)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// The default value of this flag is aligned with platform behavior to handle
// context menu with touch.
// TODO(crbug.com/40796475): Enable this flag for all platforms after launch.
BASE_FEATURE(kWebUITabStripContextMenuAfterTap,
             "WebUITabStripContextMenuAfterTap",
#if BUILDFLAG(IS_CHROMEOS)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);

#if BUILDFLAG(IS_MAC)
BASE_FEATURE(kViewsFirstRunDialog,
             "ViewsFirstRunDialog",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kViewsJSAppModalDialog,
             "ViewsJSAppModalDialog",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
BASE_FEATURE(kUsePortalAccentColor,
             "UsePortalAccentColor",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

BASE_FEATURE(kPageSpecificDataDialogRelatedInstalledAppsSection,
             "PageSpecificDataDialogRelatedInstalledAppsSection",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kEnableManagementPromotionBanner,
             "EnableManagementPromotionBanner",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kEnablePolicyPromotionBanner,
             "EnablePolicyPromotionBanner",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kInlineFullscreenPerfExperiment,
             "InlineFullscreenPerfExperiment",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPageActionsMigration,
             "PageActionsMigration",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSavePasswordsContextualUi,
             "SavePasswordsContextualUi",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCompositorLoadingAnimations,
             "CompositorLoadingAnimations",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kByDateHistoryInSidePanel,
             "ByDateHistoryInSidePanel",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabStripBrowserApi,
             "TabStripBrowserApi",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabstripComboButton,
             "TabstripComboButton",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kLaunchedTabSearchToolbarButton,
             "LaunchedTabSearchToolbarButton",
#if BUILDFLAG(IS_CHROMEOS)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);

static std::string GetCountryCode() {
  if (!g_browser_process || !g_browser_process->variations_service()) {
    return std::string();
  }
  std::string country_code =
      g_browser_process->variations_service()->GetStoredPermanentCountry();
  if (country_code.empty()) {
    country_code = g_browser_process->variations_service()->GetLatestCountry();
  }
  return country_code;
}

bool IsTabSearchMoving() {
  static const bool is_tab_search_moving = [] {
    if (GetCountryCode() == "us" &&
        base::FeatureList::IsEnabled(
            features::kLaunchedTabSearchToolbarButton)) {
      return true;
    }
    return base::FeatureList::IsEnabled(features::kTabstripComboButton);
  }();

  return is_tab_search_moving;
}

bool HasTabstripComboButtonWithBackground() {
  return IsTabSearchMoving() &&
         features::kTabstripComboButtonHasBackground.Get() &&
         !features::kTabSearchToolbarButton.Get();
}

bool HasTabstripComboButtonWithReverseButtonOrder() {
  return IsTabSearchMoving() &&
         features::kTabstripComboButtonHasReverseButtonOrder.Get() &&
         !features::kTabSearchToolbarButton.Get();
}

bool HasTabSearchToolbarButton() {
  static const bool has_tab_search_toolbar_button = [] {
    if (!IsTabSearchMoving()) {
      return false;
    }
    if (GetCountryCode() == "us" &&
        base::FeatureList::IsEnabled(
            features::kLaunchedTabSearchToolbarButton)) {
      return true;
    }
    // Gate on server-side Finch config for all other countries
    // as well as ChromeOS.
    return features::kTabSearchToolbarButton.Get();
  }();

  return has_tab_search_toolbar_button;
}

}  // namespace features
