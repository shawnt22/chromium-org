// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/public/feature_constants.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"

namespace feature_engagement {

// Features used by the In-Product Help system.
BASE_FEATURE(kIPHDemoMode, "IPH_DemoMode", base::FEATURE_DISABLED_BY_DEFAULT);

// Features used by various clients to show their In-Product Help messages.
BASE_FEATURE(kIPHDummyFeature, "IPH_Dummy", base::FEATURE_DISABLED_BY_DEFAULT);

// Feature used to add on-device storage for feature engagement.
BASE_FEATURE(kOnDeviceStorage,
             "OnDeviceStorage",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsOnDeviceStorageEnabled() {
  return base::FeatureList::IsEnabled(kOnDeviceStorage);
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
BASE_FEATURE(kEsbDownloadRowPromoFeature,
             "EsbDownloadRowPromo",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif
BASE_FEATURE(kIPHBatterySaverModeFeature,
             "IPH_BatterySaverMode",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHCompanionSidePanelFeature,
             "IPH_CompanionSidePanel",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHCompanionSidePanelRegionSearchFeature,
             "IPH_CompanionSidePanelRegionSearch",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHComposeMSBBSettingsFeature,
             "IPH_ComposeMSBBSettingsFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHComposeNewBadgeFeature,
             "IPH_ComposeNewBadgeFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDesktopSharedHighlightingFeature,
             "IPH_DesktopSharedHighlighting",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDesktopCustomizeChromeFeature,
             "IPH_DesktopCustomizeChrome",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDesktopCustomizeChromeRefreshFeature,
             "IPH_DesktopCustomizeChromeRefresh",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDesktopNewTabPageModulesCustomizeFeature,
             "IPH_DesktopNewTabPageModulesCustomize",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDiscardRingFeature,
             "IPH_DiscardRing",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadEsbPromoFeature,
             "IPH_DownloadEsbPromo",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHExplicitBrowserSigninPreferenceRememberedFeature,
             "IPH_ExplicitBrowserSigninPreferenceRemembered",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHHistorySearchFeature,
             "IPH_HistorySearch",
             base::FEATURE_ENABLED_BY_DEFAULT);
#if BUILDFLAG(ENABLE_EXTENSIONS)
BASE_FEATURE(kIPHExtensionsMenuFeature,
             "IPH_ExtensionsMenu",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHExtensionsRequestAccessButtonFeature,
             "IPH_ExtensionsRequestAccessButton",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHExtensionsZeroStatePromoFeature,
             "IPH_ExtensionsZeroStatePromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<IPHExtensionsZeroStatePromoVariant>::Option
    kIPHExtensionsZeroStatePromoVariantOptions[] = {
        {IPHExtensionsZeroStatePromoVariant::kCustomActionIph,
         "custom-action-iph"},
        {IPHExtensionsZeroStatePromoVariant::kCustomUiChipIph,
         "custom-ui-chip-iph"},
        {IPHExtensionsZeroStatePromoVariant::kCustomUIPlainLinkIph,
         "custom-ui-plain-link-iph"}};
BASE_FEATURE_ENUM_PARAM(
    IPHExtensionsZeroStatePromoVariant,
    kIPHExtensionsZeroStatePromoVariantParam,
    &feature_engagement::kIPHExtensionsZeroStatePromoFeature,
    "x_iph-variant",
    IPHExtensionsZeroStatePromoVariant::kCustomActionIph,
    &kIPHExtensionsZeroStatePromoVariantOptions);
#endif
BASE_FEATURE(kIPHFocusHelpBubbleScreenReaderPromoFeature,
             "IPH_FocusHelpBubbleScreenReaderPromo",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHGlicPromoFeature,
             "IPH_GlicPromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHGMCCastStartStopFeature,
             "IPH_GMCCastStartStop",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHGMCLocalMediaCastingFeature,
             "IPH_GMCLocalMediaCasting",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHMemorySaverModeFeature,
             "IPH_HighEfficiencyMode",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHLiveCaptionFeature,
             "IPH_LiveCaption",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHMerchantTrustFeature,
             "IPH_MerchantTrust",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHLensOverlayFeature,
             "IPH_LensOverlay",
             base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<std::string> kIPHLensOverlayUrlAllowFilters{
    &feature_engagement::kIPHLensOverlayFeature, "x_url_allow_filters", "[]"};
const base::FeatureParam<std::string> kIPHLensOverlayUrlBlockFilters{
    &feature_engagement::kIPHLensOverlayFeature, "x_url_block_filters", "[]"};
const base::FeatureParam<std::string> kIPHLensOverlayUrlPathMatchAllowPatterns{
    &feature_engagement::kIPHLensOverlayFeature,
    "x_url_path_match_allow_patterns", "[]"};
const base::FeatureParam<std::string>
    kIPHLensOverlayUrlForceAllowedUrlMatchPatterns{
        &feature_engagement::kIPHLensOverlayFeature,
        "x_url_forced_allowed_match_patterns", "[]"};
const base::FeatureParam<std::string> kIPHLensOverlayUrlPathMatchBlockPatterns{
    &feature_engagement::kIPHLensOverlayFeature,
    "x_url_path_match_block_patterns", "[]"};
const base::FeatureParam<base::TimeDelta> kIPHLensOverlayDelayTime{
    &feature_engagement::kIPHLensOverlayFeature, "x_wait_time",
    base::Seconds(7)};
BASE_FEATURE(kIPHLensOverlayTranslateButtonFeature,
             "IPH_LensOverlayTranslateButton",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabAudioMutingFeature,
             "IPH_TabAudioMuting",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordsSavePrimingPromoFeature,
             "IPH_PasswordsSavePrimingPromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordsSaveRecoveryPromoFeature,
             "IPH_PasswordsSaveRecoveryPromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordsManagementBubbleAfterSaveFeature,
             "IPH_PasswordsManagementBubbleAfterSave",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordsManagementBubbleDuringSigninFeature,
             "IPH_PasswordsManagementBubbleDuringSignin",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordsWebAppProfileSwitchFeature,
             "IPH_PasswordsWebAppProfileSwitch",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordManagerShortcutFeature,
             "IPH_PasswordManagerShortcut",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPasswordSharingFeature,
             "IPH_PasswordSharingFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPdfSearchifyFeature,
             "IPH_PdfSearchifyFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPerformanceInterventionDialogFeature,
             "IPH_PerformanceInterventionDialogFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPlusAddressFirstSaveFeature,
             "IPH_PlusAddressFirstSaveFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPowerBookmarksSidePanelFeature,
             "IPH_PowerBookmarksSidePanel",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPriceInsightsPageActionIconLabelFeature,
             "IPH_PriceInsightsPageActionIconLabelFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPriceTrackingEmailConsentFeature,
             "IPH_PriceTrackingEmailConsentFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPriceTrackingPageActionIconLabelFeature,
             "IPH_PriceTrackingPageActionIconLabelFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadingListDiscoveryFeature,
             "IPH_ReadingListDiscovery",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadingListEntryPointFeature,
             "IPH_ReadingListEntryPoint",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadingListInSidePanelFeature,
             "IPH_ReadingListInSidePanel",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadingModeSidePanelFeature,
             "IPH_ReadingModeSidePanel",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHShoppingCollectionFeature,
             "IPH_ShoppingCollectionFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSidePanelGenericPinnableFeature,
             "IPH_SidePanelGenericPinnableFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSidePanelLensOverlayPinnableFeature,
             "IPH_SidePanelLensOverlayPinnableFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSidePanelLensOverlayPinnableFollowupFeature,
             "IPH_SidePanelLensOverlayPinnableFollowupFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSideSearchAutoTriggeringFeature,
             "IPH_SideSearchAutoTriggering",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSideSearchPageActionLabelFeature,
             "IPH_SideSearchPageActionLabel",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSignoutWebInterceptFeature,
             "IPH_SignoutWebIntercept",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPwaQuietNotificationFeature,
             "IPH_PwaQuietNotification",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsSaveV2IntroFeature,
             "IPH_TabGroupsSaveV2Intro",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsSaveV2CloseGroupFeature,
             "IPH_TabGroupsSaveV2CloseGroup",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsSharedTabChangedFeature,
             "IPH_TabGroupsSharedTabChanged",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsSharedTabFeedbackFeature,
             "IPH_TabGroupsSharedTabFeedback",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabOrganizationSuccessFeature,
             "IPH_TabOrganizationSuccess",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabSearchFeature,
             "IPH_TabSearch",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabSearchToolbarButtonFeature,
             "IPH_TabSearchToolbarButton",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDesktopSnoozeFeature,
             "IPH_DesktopSnoozeFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDesktopPwaInstallFeature,
             "IPH_DesktopPwaInstall",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHProfileSwitchFeature,
             "IPH_ProfileSwitch",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWebUiHelpBubbleTestFeature,
             "IPH_WebUiHelpBubbleTest",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPriceTrackingInSidePanelFeature,
             "IPH_PriceTrackingInSidePanel",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHBackNavigationMenuFeature,
             "IPH_BackNavigationMenu",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kIPHAccountSettingsHistorySync,
             "IPH_AccountSettingsHistorySync",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAndroidTabDeclutter,
             "IPH_AndroidTabDeclutter",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationNewTabFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_NewTab",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationOpenInBrowserFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_OpenInBrowser",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationShareFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_Share",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationVoiceSearchFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_VoiceSearch",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationTranslateFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_Translate",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationAddToBookmarksFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_AddToBookmarks",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationReadAloudFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_ReadAloud",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationPageSummaryWebFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_PageSummary_Web",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAdaptiveButtonInTopToolbarCustomizationPageSummaryPdfFeature,
             "IPH_AdaptiveButtonInTopToolbarCustomization_PageSummary_Pdf",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPageSummaryWebMenuFeature,
             "IPH_PageSummaryWebMenu",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPageSummaryPdfMenuFeature,
             "IPH_PageSummaryPdfMenu",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAppSpecificHistory,
             "IPH_AppSpecificHistory",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutoDarkOptOutFeature,
             "IPH_AutoDarkOptOut",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutoDarkUserEducationMessageFeature,
             "IPH_AutoDarkUserEducationMessage",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutoDarkUserEducationMessageOptInFeature,
             "IPH_AutoDarkUserEducationMessageOptIn",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHCCTHistory,
             "IPH_CCTHistory",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHCCTMinimized,
             "IPH_CCTMinimized",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHContextualPageActionsQuietVariantFeature,
             "IPH_ContextualPageActions_QuietVariant",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHContextualPageActionsActionChipFeature,
             "IPH_ContextualPageActions_ActionChip",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDataSaverDetailFeature,
             "IPH_DataSaverDetail",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDataSaverMilestonePromoFeature,
             "IPH_DataSaverMilestonePromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDataSaverPreviewFeature,
             "IPH_DataSaverPreview",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDefaultBrowserPromoMagicStackFeature,
             "IPH_DefaultBrowserPromoMagicStack",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDefaultBrowserPromoMessagesFeature,
             "IPH_DefaultBrowserPromoMessages",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDefaultBrowserPromoSettingCardFeature,
             "IPH_DefaultBrowserPromoSettingCard",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadHomeFeature,
             "IPH_DownloadHome",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadIndicatorFeature,
             "IPH_DownloadIndicator",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadPageFeature,
             "IPH_DownloadPage",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadPageScreenshotFeature,
             "IPH_DownloadPageScreenshot",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHChromeHomeExpandFeature,
             "IPH_ChromeHomeExpand",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHChromeHomePullToRefreshFeature,
             "IPH_ChromeHomePullToRefresh",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadSettingsFeature,
             "IPH_DownloadSettings",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadInfoBarDownloadContinuingFeature,
             "IPH_DownloadInfoBarDownloadContinuing",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDownloadInfoBarDownloadsAreFasterFeature,
             "IPH_DownloadInfoBarDownloadsAreFaster",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadAloudAppMenuFeature,
             "IPH_ReadAloudAppMenuFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);
             BASE_FEATURE(kIPHReadAloudExpandedPlayerFeature,
              "IPH_ReadAloudExpandedPlayerFeature",
              base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadAloudPlaybackModeFeature,
              "IPH_ReadAloudPlaybackModeFeature",
              base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadLaterContextMenuFeature,
             "IPH_ReadLaterContextMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadLaterAppMenuBookmarkThisPageFeature,
             "IPH_ReadLaterAppMenuBookmarkThisPage",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadLaterAppMenuBookmarksFeature,
             "IPH_ReadLaterAppMenuBookmarks",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadLaterBottomSheetFeature,
             "IPH_ReadLaterBottomSheet",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHRequestDesktopSiteDefaultOnFeature,
             "IPH_RequestDesktopSiteDefaultOn",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHRequestDesktopSiteExceptionsGenericFeature,
             "IPH_RequestDesktopSiteExceptionsGeneric",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHRequestDesktopSiteWindowSettingFeature,
             "IPH_RequestDesktopSiteWindowSetting",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHRtlGestureNavigationFeature,
             "IPH_RtlGestureNavigation",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHShoppingListSaveFlowFeature,
             "IPH_ShoppingListSaveFlow",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHEphemeralTabFeature,
             "IPH_EphemeralTab",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHFeedCardMenuFeature,
             "IPH_FeedCardMenu",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHGenericAlwaysTriggerHelpUiFeature,
             "IPH_GenericAlwaysTriggerHelpUiFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHIdentityDiscFeature,
             "IPH_IdentityDisc",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHInstanceSwitcherFeature,
             "IPH_InstanceSwitcher",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHKeyboardAccessoryAddressFillingFeature,
             "IPH_KeyboardAccessoryAddressFilling",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHKeyboardAccessoryBarSwipingFeature,
             "IPH_KeyboardAccessoryBarSwiping",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHKeyboardAccessoryPasswordFillingFeature,
             "IPH_KeyboardAccessoryPasswordFilling",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHKeyboardAccessoryPaymentFillingFeature,
             "IPH_KeyboardAccessoryPaymentFilling",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHKeyboardAccessoryPaymentOfferFeature,
             "IPH_KeyboardAccessoryPaymentOffer",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHLowUserEngagementDetectorFeature,
             "IPH_LowUserEngagementDetector",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHMicToolbarFeature,
             "IPH_MicToolbar",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHMenuAddToGroup,
             "IPH_MenuAddToGroup",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPageInfoFeature,
             "IPH_PageInfo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPageInfoStoreInfoFeature,
             "IPH_PageInfoStoreInfo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPageZoomFeature,
             "IPH_PageZoom",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPdfPageDownloadFeature,
             "IPH_PdfPageDownload",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPreviewsOmniboxUIFeature,
             "IPH_PreviewsOmniboxUI",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHShoppingListMenuItemFeature,
             "IPH_ShoppingListMenuItem",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupCreationDialogSyncTextFeature,
             "IPH_TabGroupCreationDialogSyncText",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsDragAndDropFeature,
             "IPH_TabGroupsDragAndDrop",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupShareNoticeFeature,
             "IPH_TabGroupShareNotice",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupShareNotificationBubbleOnStripFeature,
             "IPH_TabGroupSharedNotificationBubbleOnStrip",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupShareUpdateFeature,
             "IPH_TabGroupShareUpdate",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsRemoteGroupFeature,
             "IPH_TabGroupsRemoteGroup",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsSurfaceFeature,
             "IPH_TabGroupsSurface",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupsSurfaceOnHideFeature,
             "IPH_TabGroupsSurfaceOnHide",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabGroupSyncOnStripFeature,
             "IPH_TabGroupSyncOnStrip",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabSwitcherAddToGroup,
             "IPH_TabSwitcherAddToGroup",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabSwitcherButtonFeature,
             "IPH_TabSwitcherButton",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabSwitcherButtonSwitchIncognitoFeature,
             "IPH_TabSwitcherButtonSwitchIncognito",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTranslateMenuButtonFeature,
             "IPH_TranslateMenuButton",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHVideoTutorialNTPChromeIntroFeature,
             "IPH_VideoTutorial_NTP_ChromeIntro",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHVideoTutorialNTPDownloadFeature,
             "IPH_VideoTutorial_NTP_Download",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHVideoTutorialNTPSearchFeature,
             "IPH_VideoTutorial_NTP_Search",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHVideoTutorialNTPVoiceSearchFeature,
             "IPH_VideoTutorial_NTP_VoiceSearch",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHVideoTutorialNTPSummaryFeature,
             "IPH_VideoTutorial_NTP_Summary",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHVideoTutorialTryNowFeature,
             "IPH_VideoTutorial_TryNow",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHExploreSitesTileFeature,
             "IPH_ExploreSitesTile",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHFeedHeaderMenuFeature,
             "IPH_FeedHeaderMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWebFeedAwarenessFeature,
             "IPH_WebFeedAwareness",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHFeedSwipeRefresh,
             "IPH_FeedSwipeRefresh",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHChromeReengagementNotification1Feature,
             "IPH_ChromeReengagementNotification1",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHChromeReengagementNotification2Feature,
             "IPH_ChromeReengagementNotification2",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHChromeReengagementNotification3Feature,
             "IPH_ChromeReengagementNotification3",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHShareScreenshotFeature,
             "IPH_ShareScreenshot",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSharingHubLinkToggleFeature,
             "IPH_SharingHubLinkToggle",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWebFeedFollowFeature,
             "IPH_WebFeedFollow",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWebFeedPostFollowDialogFeature,
             "IPH_WebFeedPostFollowDialog",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWebFeedPostFollowDialogFeatureWithUIUpdate,
             "IPH_WebFeedPostFollowDialogWithUIUpdate",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSharedHighlightingBuilder,
             "IPH_SharedHighlightingBuilder",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSharedHighlightingReceiverFeature,
             "IPH_SharedHighlightingReceiver",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHSharingHubWebnotesStylizeFeature,
             "IPH_SharingHubWebnotesStylize",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHRestoreTabsOnFREFeature,
             "IPH_RestoreTabsOnFRE",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabSwitcherXR,
             "IPH_TabSwitcherXR",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHTabTearingXR,
             "IPH_TabTearingXR",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kIPHBottomToolbarTipFeature,
             "IPH_BottomToolbarTip",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_IOS)
BASE_FEATURE(kIPHLongPressToolbarTipFeature,
             "IPH_LongPressToolbarTip",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHBadgedReadingListFeature,
             "IPH_BadgedReadingList",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWhatsNewFeature,
             "IPH_WhatsNew",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHWhatsNewUpdatedFeature,
             "IPH_WhatsNewUpdated",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHReadingListMessagesFeature,
             "IPH_ReadingListMessages",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHBadgedTranslateManualTriggerFeature,
             "IPH_BadgedTranslateManualTrigger",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDiscoverFeedHeaderFeature,
             "IPH_DiscoverFeedHeaderMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHDefaultSiteViewFeature,
             "IPH_DefaultSiteView",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHFollowWhileBrowsingFeature,
             "IPH_FollowWhileBrowsing",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPriceNotificationsWhileBrowsingFeature,
             "IPH_PriceNotificationsWhileBrowsing",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSDefaultBrowserBadgeEligibilityFeature,
             "IPH_iOSDefaultBrowserBadgeEligibility",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSDefaultBrowserOverflowMenuBadgeFeature,
             "IPH_iOSDefaultBrowserOverflowMenuBadge",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSDownloadAutoDeletionFeature,
             "IPH_iOSDownloadAutoDeletion",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSLensKeyboardFeature,
             "IPH_iOSLensKeyboard",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoAppStoreFeature,
             "IPH_iOSPromoAppStore",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoWhatsNewFeature,
             "IPH_iOSPromoWhatsNew",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoSigninFullscreenFeature,
             "IPH_iOSPromoSigninFullscreen",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoPostRestoreFeature,
             "IPH_iOSPromoPostRestore",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoCredentialProviderExtensionFeature,
             "IPH_iOSPromoCredentialProviderExtension",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoDefaultBrowserReminderFeature,
             "IPH_iOSPromoDefaultBrowserReminder",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSHistoryOnOverflowMenuFeature,
             "IPH_iOSHistoryOnOverflowMenuFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoPostRestoreDefaultBrowserFeature,
             "IPH_iOSPromoPostRestoreDefaultBrowser",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoNonModalUrlPasteDefaultBrowserFeature,
             "IPH_iOSPromoNonModalUrlPasteDefaultBrowser",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoNonModalAppSwitcherDefaultBrowserFeature,
             "IPH_iOSPromoNonModalAppSwitcherDefaultBrowser",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoNonModalShareDefaultBrowserFeature,
             "IPH_iOSPromoNonModalShareDefaultBrowser",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoNonModalSigninPasswordFeature,
             "IPH_iOSPromoNonModalSigninPassword",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoNonModalSigninBookmarkFeature,
             "IPH_iOSPromoNonModalSigninBookmark",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoPasswordManagerWidgetFeature,
             "IPH_iOSPromoPasswordManagerWidget",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPullToRefreshFeature,
             "IPH_iOSPullToRefreshFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSReplaceSyncPromosWithSignInPromos,
             "IPH_iOSReplaceSyncPromosWithSignInPromos",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSTabGridSwipeRightForIncognito,
             "IPH_iOSTabGridSwipeRightForIncognito",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSDockingPromoFeature,
             "IPH_iOSDockingPromo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSDockingPromoRemindMeLaterFeature,
             "IPH_iOSDockingPromoRemindMeLater",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoAllTabsFeature,
             "IPH_iOSPromoAllTabs",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoMadeForIOSFeature,
             "IPH_iOSPromoMadeForIOS",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoStaySafeFeature,
             "IPH_iOSPromoStaySafe",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSSwipeBackForwardFeature,
             "IPH_iOSSwipeBackForward",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSSwipeToolbarToChangeTabFeature,
             "IPH_iOSSwipeToolbarToChangeTab",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPostDefaultAbandonmentPromoFeature,
             "IPH_iOSPostDefaultAbandonmentPromo",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPromoGenericDefaultBrowserFeature,
             "IPH_iOSPromoGenericDefaultBrowser",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSOverflowMenuCustomizationFeature,
             "IPH_iOSOverflowMenuCustomization",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPageInfoRevampFeature,
             "IPH_iOSPageInfoRevamp",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSInlineEnhancedSafeBrowsingPromoFeature,
             "IPH_iOSInlineEnhancedSafeBrowsingPromo",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSSavedTabGroupClosed,
             "IPH_iOSSavedTabGroupClosed",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSContextualPanelSampleModelFeature,
             "IPH_iOSContextualPanelSampleModel",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSContextualPanelPriceInsightsFeature,
             "IPH_iOSContextualPanelPriceInsights",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHHomeCustomizationMenuFeature,
             "IPH_HomeCustomizationMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSLensOverlayEntrypointTipFeature,
             "IPH_iOSLensOverlayEntrypointTip",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSLensOverlayEscapeHatchTipFeature,
             "IPH_iOSLensOverlayEscapeHatchTip",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSSharedTabGroupForeground,
             "IPH_iOSSharedTabGroupForeground",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSDefaultBrowserBannerPromoFeature,
             "IPH_iOSDefaultBrowserBannerPromoFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSReminderNotificationsOverflowMenuBubbleFeature,
             "IPH_iOSReminderNotificationsOverflowMenuBubbleFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSReminderNotificationsOverflowMenuNewBadgeFeature,
             "IPH_iOSReminderNotificationsOverflowMenuNewBadgeFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
// Note: This IPH will only be triggered if `kImportPasswordsFromSafari` is
// enabled.
BASE_FEATURE(kIPHiOSSafariImportFeature,
             "IPH_iOSSafariImportFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
// Note: This IPH will only be triggered if `kIdentityDiscAccountMenu` is
// enabled.
BASE_FEATURE(kIPHiOSSettingsInOverflowMenuBubbleFeature,
             "IPH_iOSSettingsInOverflowMenuBubbleFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
// Note: This IPH will only be triggered if
// `kSeparateProfilesForManagedAccounts` is enabled.
BASE_FEATURE(kIPHiOSSwitchAccountsWithNTPAccountParticleDiscFeature,
             "IPH_iOSSwitchAccountsWithNTPAccountParticleDiscFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
// Note: Feed swipe IPHs will only be triggered if `kFeedSwipeInProductHelp` is
// enabled.
BASE_FEATURE(kIPHiOSFeedSwipeStaticFeature,
             "IPH_iOSFeedSwipeStaticFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSFeedSwipeAnimatedFeature,
             "IPH_iOSFeedSwipeAnimatedFeature",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIPHiOSWelcomeBackFeature,
             "IPH_iOSWelcomeBack",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIPHIOSBWGPromoFeature,
             "IPH_iOSBWGPromo",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIPHIOSPageActionMenu,
             "IPH_iOSPageActionMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIPHiOSHomepageLensNewBadge,
             "IPH_iOSHomepageLensNewBadge",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIPHiOSHomepageCustomizationNewBadge,
             "IPH_iOSHomepageCustomizationNewBadge",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Non-FET feature.
BASE_FEATURE(kDefaultBrowserEligibilitySlidingWindow,
             "DefaultBrowserEligibilitySlidingWindow",
             base::FEATURE_ENABLED_BY_DEFAULT);

constexpr base::FeatureParam<int> kDefaultBrowserEligibilitySlidingWindowParam{
    &kDefaultBrowserEligibilitySlidingWindow,
    /*name=*/"sliding-window-days",
    /*default_value=*/180};

BASE_FEATURE(kDefaultBrowserTriggerCriteriaExperiment,
             "DefaultBrowserTriggerCriteriaExperiment",
             base::FEATURE_DISABLED_BY_DEFAULT);

#endif  // BUILDFLAG(IS_IOS)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)
BASE_FEATURE(kIPHAutofillBnplAffirmOrZipSuggestionFeature,
             "IPH_AutofillBnplAffirmOrZipSuggestion",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillBnplAffirmZipOrKlarnaSuggestionFeature,
             "IPH_AutofillBnplAffirmZipOrKlarnaSuggestion",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillCardInfoRetrievalSuggestionFeature,
             "IPH_AutofillCardInfoRetrievalSuggestion",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillCreditCardBenefitFeature,
             "IPH_AutofillCreditCardBenefit",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillDisabledVirtualCardSuggestionFeature,
             "IPH_AutofillDisabledVirtualCardSuggestion",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillExternalAccountProfileSuggestionFeature,
             "IPH_AutofillExternalAccountProfileSuggestion",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillHomeWorkProfileSuggestionFeature,
             "IPH_AutofillHomeWorkProfileSuggestion",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillAiOptInFeature,
             "IPH_AutofillAiOptIn",
             base::FEATURE_ENABLED_BY_DEFAULT);
// This parameter enables updates the Autofill IPH CTA button string.
const base::FeatureParam<int> kAutofillIphCTAVariationsStringValue{
    &feature_engagement::kIPHAutofillAiOptInFeature, "x_autofill_ai_cta_string_value", 0};
BASE_FEATURE(kIPHAutofillVirtualCardCVCSuggestionFeature,
             "IPH_AutofillVirtualCardCVCSuggestion",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillVirtualCardSuggestionFeature,
             "IPH_AutofillVirtualCardSuggestion",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHCookieControlsFeature,
             "IPH_CookieControls",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHPlusAddressCreateSuggestionFeature,
             "IPH_PlusAddressCreateSuggestion",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHAutofillEnableLoyaltyCardsFeature,
             "IPH_AutofillEnableLoyaltyCards",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) ||
        // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(IS_CHROMEOS)
BASE_FEATURE(kIPHGrowthFramework,
             "IPH_GrowthFramework",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHGoogleOneOfferNotificationFeature,
             "IPH_GoogleOneOfferNotification",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHLauncherSearchHelpUiFeature,
             "IPH_LauncherSearchHelpUi",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedOneFeature,
             "IPH_ScalableIphTimerBasedOne",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedTwoFeature,
             "IPH_ScalableIphTimerBasedTwo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedThreeFeature,
             "IPH_ScalableIphTimerBasedThree",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedFourFeature,
             "IPH_ScalableIphTimerBasedFour",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedFiveFeature,
             "IPH_ScalableIphTimerBasedFive",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedSixFeature,
             "IPH_ScalableIphTimerBasedSix",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedSevenFeature,
             "IPH_ScalableIphTimerBasedSeven",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedEightFeature,
             "IPH_ScalableIphTimerBasedEight",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedNineFeature,
             "IPH_ScalableIphTimerBasedNine",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphTimerBasedTenFeature,
             "IPH_ScalableIphTimerBasedTen",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedOneFeature,
             "IPH_ScalableIphUnlockedBasedOne",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedTwoFeature,
             "IPH_ScalableIphUnlockedBasedTwo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedThreeFeature,
             "IPH_ScalableIphUnlockedBasedThree",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedFourFeature,
             "IPH_ScalableIphUnlockedBasedFour",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedFiveFeature,
             "IPH_ScalableIphUnlockedBasedFive",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedSixFeature,
             "IPH_ScalableIphUnlockedBasedSix",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedSevenFeature,
             "IPH_ScalableIphUnlockedBasedSeven",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedEightFeature,
             "IPH_ScalableIphUnlockedBasedEight",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedNineFeature,
             "IPH_ScalableIphUnlockedBasedNine",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphUnlockedBasedTenFeature,
             "IPH_ScalableIphUnlockedBasedTen",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedNudgeFeature,
             "IPH_ScalableIphHelpAppBasedNudge",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedOneFeature,
             "IPH_ScalableIphHelpAppBasedOne",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedTwoFeature,
             "IPH_ScalableIphHelpAppBasedTwo",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedThreeFeature,
             "IPH_ScalableIphHelpAppBasedThree",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedFourFeature,
             "IPH_ScalableIphHelpAppBasedFour",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedFiveFeature,
             "IPH_ScalableIphHelpAppBasedFive",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedSixFeature,
             "IPH_ScalableIphHelpAppBasedSix",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedSevenFeature,
             "IPH_ScalableIphHelpAppBasedSeven",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedEightFeature,
             "IPH_ScalableIphHelpAppBasedEight",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedNineFeature,
             "IPH_ScalableIphHelpAppBasedNine",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphHelpAppBasedTenFeature,
             "IPH_ScalableIphHelpAppBasedTen",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kIPHScalableIphGamingFeature,
             "IPH_ScalableIphGaming",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// This can be enabled by default, as the DesktopPWAsLinkCapturing
// flag is needed for the IPH linked to this feature to work, and
// use-cases to show the IPH are guarded by that flag.
BASE_FEATURE(kIPHDesktopPWAsLinkCapturingLaunch,
             "IPH_DesktopPWAsLinkCapturingLaunch",
             base::FEATURE_ENABLED_BY_DEFAULT);

// This can be enabled by default, as the DesktopPWAsLinkCapturing
// flag is needed for the IPH linked to this feature to work, and
// use-cases to show the IPH are guarded by that flag.
BASE_FEATURE(kIPHDesktopPWAsLinkCapturingLaunchAppInTab,
             "IPH_DesktopPWAsLinkCapturingLaunchAppInTab",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIPHSupervisedUserProfileSigninFeature,
             "IPH_SupervisedUserProfileSignin",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_LINUX)

#if !BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kIPHiOSPasswordPromoDesktopFeature,
             "IPH_iOSPasswordPromoDesktop",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSAddressPromoDesktopFeature,
             "IPH_iOSAddressPromoDesktop",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kIPHiOSPaymentPromoDesktopFeature,
             "IPH_iOSPaymentPromoDesktop",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // !BUILDFLAG(IS_ANDROID)

}  // namespace feature_engagement
