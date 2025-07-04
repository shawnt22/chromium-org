// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/public/feature_list.h"

#include <vector>

#include "build/build_config.h"
#include "components/feature_engagement/public/feature_constants.h"

namespace feature_engagement {

namespace {
// Whenever a feature is added to |kAllFeatures|, it should also be added as
// DEFINE_VARIATION_PARAM in the header, and also added to the
// |kIPHDemoModeChoiceVariations| array.
const base::Feature* const kAllFeatures[] = {
    &kIPHDummyFeature,  // Ensures non-empty array for all platforms.
#if BUILDFLAG(IS_ANDROID)
    &kIPHAccountSettingsHistorySync,
    &kIPHAndroidTabDeclutter,
    &kIPHAdaptiveButtonInTopToolbarCustomizationNewTabFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationOpenInBrowserFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationShareFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationVoiceSearchFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationAddToBookmarksFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationTranslateFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationReadAloudFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationPageSummaryWebFeature,
    &kIPHAdaptiveButtonInTopToolbarCustomizationPageSummaryPdfFeature,
    &kIPHPageSummaryWebMenuFeature,
    &kIPHPageSummaryPdfMenuFeature,
    &kIPHAutoDarkOptOutFeature,
    &kIPHAutoDarkUserEducationMessageFeature,
    &kIPHAutoDarkUserEducationMessageOptInFeature,
    &kIPHAppSpecificHistory,
    &kIPHCCTHistory,
    &kIPHCCTMinimized,
    &kIPHDataSaverDetailFeature,
    &kIPHDataSaverMilestonePromoFeature,
    &kIPHDataSaverPreviewFeature,
    &kIPHDefaultBrowserPromoMagicStackFeature,
    &kIPHDefaultBrowserPromoMessagesFeature,
    &kIPHDefaultBrowserPromoSettingCardFeature,
    &kIPHDownloadHomeFeature,
    &kIPHDownloadIndicatorFeature,
    &kIPHDownloadPageFeature,
    &kIPHDownloadPageScreenshotFeature,
    &kIPHChromeHomeExpandFeature,
    &kIPHChromeHomePullToRefreshFeature,
    &kIPHChromeReengagementNotification1Feature,
    &kIPHChromeReengagementNotification2Feature,
    &kIPHChromeReengagementNotification3Feature,
    &kIPHContextualPageActionsQuietVariantFeature,
    &kIPHContextualPageActionsActionChipFeature,
    &kIPHDownloadSettingsFeature,
    &kIPHDownloadInfoBarDownloadContinuingFeature,
    &kIPHDownloadInfoBarDownloadsAreFasterFeature,
    &kIPHEphemeralTabFeature,
    &kIPHFeedCardMenuFeature,
    &kIPHGenericAlwaysTriggerHelpUiFeature,
    &kIPHIdentityDiscFeature,
    &kIPHInstanceSwitcherFeature,
    &kIPHKeyboardAccessoryAddressFillingFeature,
    &kIPHKeyboardAccessoryBarSwipingFeature,
    &kIPHKeyboardAccessoryPasswordFillingFeature,
    &kIPHKeyboardAccessoryPaymentFillingFeature,
    &kIPHKeyboardAccessoryPaymentOfferFeature,
    &kIPHLowUserEngagementDetectorFeature,
    &kIPHMicToolbarFeature,
    &kIPHMenuAddToGroup,
    &kIPHPageInfoFeature,
    &kIPHPageInfoStoreInfoFeature,
    &kIPHPageZoomFeature,
    &kIPHPdfPageDownloadFeature,
    &kIPHPreviewsOmniboxUIFeature,
    &kIPHReadAloudAppMenuFeature,
    &kIPHReadAloudExpandedPlayerFeature,
    &kIPHReadAloudPlaybackModeFeature,
    &kIPHReadLaterContextMenuFeature,
    &kIPHReadLaterAppMenuBookmarkThisPageFeature,
    &kIPHReadLaterAppMenuBookmarksFeature,
    &kIPHReadLaterBottomSheetFeature,
    &kIPHRequestDesktopSiteDefaultOnFeature,
    &kIPHRequestDesktopSiteExceptionsGenericFeature,
    &kIPHRequestDesktopSiteWindowSettingFeature,
    &kIPHShoppingListMenuItemFeature,
    &kIPHShoppingListSaveFlowFeature,
    &kIPHTabGroupCreationDialogSyncTextFeature,
    &kIPHTabGroupsDragAndDropFeature,
    &kIPHTabGroupShareNoticeFeature,
    &kIPHTabGroupShareNotificationBubbleOnStripFeature,
    &kIPHTabGroupShareUpdateFeature,
    &kIPHTabGroupsRemoteGroupFeature,
    &kIPHTabGroupsSurfaceFeature,
    &kIPHTabGroupsSurfaceOnHideFeature,
    &kIPHTabGroupSyncOnStripFeature,
    &kIPHTabSwitcherAddToGroup,
    &kIPHTabSwitcherButtonFeature,
    &kIPHTabSwitcherButtonSwitchIncognitoFeature,
    &kIPHTranslateMenuButtonFeature,
    &kIPHVideoTutorialNTPChromeIntroFeature,
    &kIPHVideoTutorialNTPDownloadFeature,
    &kIPHVideoTutorialNTPSearchFeature,
    &kIPHVideoTutorialNTPVoiceSearchFeature,
    &kIPHVideoTutorialNTPSummaryFeature,
    &kIPHVideoTutorialTryNowFeature,
    &kIPHExploreSitesTileFeature,
    &kIPHFeedHeaderMenuFeature,
    &kIPHWebFeedAwarenessFeature,
    &kIPHFeedSwipeRefresh,
    &kIPHShareScreenshotFeature,
    &kIPHSharingHubLinkToggleFeature,
    &kIPHWebFeedFollowFeature,
    &kIPHWebFeedPostFollowDialogFeature,
    &kIPHWebFeedPostFollowDialogFeatureWithUIUpdate,
    &kIPHSharedHighlightingBuilder,
    &kIPHSharedHighlightingReceiverFeature,
    &kIPHSharingHubWebnotesStylizeFeature,
    &kIPHRestoreTabsOnFREFeature,
    &kIPHRtlGestureNavigationFeature,
    &kIPHTabSwitcherXR,
    &kIPHTabTearingXR,
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
    &kIPHBottomToolbarTipFeature,
#endif  // BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_IOS)
    &kIPHLongPressToolbarTipFeature,
    &kIPHBadgedReadingListFeature,
    &kIPHWhatsNewFeature,
    &kIPHWhatsNewUpdatedFeature,
    &kIPHReadingListMessagesFeature,
    &kIPHBadgedTranslateManualTriggerFeature,
    &kIPHDiscoverFeedHeaderFeature,
    &kIPHDefaultSiteViewFeature,
    &kIPHFollowWhileBrowsingFeature,
    &kIPHPriceNotificationsWhileBrowsingFeature,
    &kIPHiOSDefaultBrowserBadgeEligibilityFeature,
    &kIPHiOSDefaultBrowserOverflowMenuBadgeFeature,
    &kIPHiOSDownloadAutoDeletionFeature,
    &kIPHiOSFeedSwipeAnimatedFeature,
    &kIPHiOSFeedSwipeStaticFeature,
    &kIPHiOSLensKeyboardFeature,
    &kIPHiOSPromoAppStoreFeature,
    &kIPHiOSPromoWhatsNewFeature,
    &kIPHiOSPromoSigninFullscreenFeature,
    &kIPHiOSPromoPostRestoreFeature,
    &kIPHiOSPromoCredentialProviderExtensionFeature,
    &kIPHiOSPromoDefaultBrowserReminderFeature,
    &kIPHiOSHistoryOnOverflowMenuFeature,
    &kIPHiOSPromoPostRestoreDefaultBrowserFeature,
    &kIPHiOSPromoNonModalUrlPasteDefaultBrowserFeature,
    &kIPHiOSPromoNonModalAppSwitcherDefaultBrowserFeature,
    &kIPHiOSPromoNonModalShareDefaultBrowserFeature,
    &kIPHiOSPromoNonModalSigninPasswordFeature,
    &kIPHiOSPromoNonModalSigninBookmarkFeature,
    &kIPHiOSPromoPasswordManagerWidgetFeature,
    &kIPHiOSPullToRefreshFeature,
    &kIPHiOSReplaceSyncPromosWithSignInPromos,
    &kIPHiOSTabGridSwipeRightForIncognito,
    &kIPHiOSDockingPromoFeature,
    &kIPHiOSDockingPromoRemindMeLaterFeature,
    &kIPHiOSPromoAllTabsFeature,
    &kIPHiOSPromoMadeForIOSFeature,
    &kIPHiOSPromoStaySafeFeature,
    &kIPHiOSSwipeBackForwardFeature,
    &kIPHiOSSwipeToolbarToChangeTabFeature,
    &kIPHiOSPostDefaultAbandonmentPromoFeature,
    &kIPHiOSPromoGenericDefaultBrowserFeature,
    &kIPHiOSOverflowMenuCustomizationFeature,
    &kIPHiOSPageInfoRevampFeature,
    &kIPHiOSInlineEnhancedSafeBrowsingPromoFeature,
    &kIPHiOSSavedTabGroupClosed,
    &kIPHiOSContextualPanelSampleModelFeature,
    &kIPHiOSContextualPanelPriceInsightsFeature,
    &kIPHHomeCustomizationMenuFeature,
    &kIPHiOSLensOverlayEntrypointTipFeature,
    &kIPHiOSLensOverlayEscapeHatchTipFeature,
    &kIPHiOSSharedTabGroupForeground,
    &kIPHiOSDefaultBrowserBannerPromoFeature,
    &kIPHiOSReminderNotificationsOverflowMenuBubbleFeature,
    &kIPHiOSReminderNotificationsOverflowMenuNewBadgeFeature,
    &kIPHiOSSettingsInOverflowMenuBubbleFeature,
    &kIPHiOSSwitchAccountsWithNTPAccountParticleDiscFeature,
    &kIPHiOSWelcomeBackFeature,
    &kIPHIOSBWGPromoFeature,
    &kIPHiOSSafariImportFeature,
    &kIPHIOSPageActionMenu,
    &kIPHiOSHomepageLensNewBadge,
    &kIPHiOSHomepageCustomizationNewBadge,
#endif  // BUILDFLAG(IS_IOS)
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
    &kEsbDownloadRowPromoFeature,
#endif
    &kIPHBatterySaverModeFeature,
    &kIPHCompanionSidePanelFeature,
    &kIPHCompanionSidePanelRegionSearchFeature,
    &kIPHComposeMSBBSettingsFeature,
    &kIPHComposeNewBadgeFeature,
    &kIPHDesktopCustomizeChromeFeature,
    &kIPHDesktopCustomizeChromeRefreshFeature,
    &kIPHDesktopNewTabPageModulesCustomizeFeature,
    &kIPHDiscardRingFeature,
    &kIPHDownloadEsbPromoFeature,
    &kIPHExplicitBrowserSigninPreferenceRememberedFeature,
    &kIPHGlicPromoFeature,
    &kIPHHistorySearchFeature,
#if BUILDFLAG(ENABLE_EXTENSIONS)
    &kIPHExtensionsMenuFeature,
    &kIPHExtensionsRequestAccessButtonFeature,
    &kIPHExtensionsZeroStatePromoFeature,
#endif
    &kIPHFocusHelpBubbleScreenReaderPromoFeature,
    &kIPHGMCCastStartStopFeature,
    &kIPHGMCLocalMediaCastingFeature,
    &kIPHMemorySaverModeFeature,
    &kIPHLensOverlayFeature,
    &kIPHLensOverlayTranslateButtonFeature,
    &kIPHLiveCaptionFeature,
    &kIPHMerchantTrustFeature,
    &kIPHTabAudioMutingFeature,
    &kIPHPasswordsSavePrimingPromoFeature,
    &kIPHPasswordsSaveRecoveryPromoFeature,
    &kIPHPasswordsManagementBubbleAfterSaveFeature,
    &kIPHPasswordsManagementBubbleDuringSigninFeature,
    &kIPHPasswordsWebAppProfileSwitchFeature,
    &kIPHPasswordManagerShortcutFeature,
    &kIPHPasswordSharingFeature,
    &kIPHPdfSearchifyFeature,
    &kIPHPerformanceInterventionDialogFeature,
    &kIPHPlusAddressFirstSaveFeature,
    &kIPHPowerBookmarksSidePanelFeature,
    &kIPHPriceInsightsPageActionIconLabelFeature,
    &kIPHPriceTrackingEmailConsentFeature,
    &kIPHPriceTrackingPageActionIconLabelFeature,
    &kIPHReadingListDiscoveryFeature,
    &kIPHReadingListEntryPointFeature,
    &kIPHReadingListInSidePanelFeature,
    &kIPHReadingModeSidePanelFeature,
    &kIPHShoppingCollectionFeature,
    &kIPHSidePanelGenericPinnableFeature,
    &kIPHSidePanelLensOverlayPinnableFeature,
    &kIPHSidePanelLensOverlayPinnableFollowupFeature,
    &kIPHSideSearchAutoTriggeringFeature,
    &kIPHSideSearchPageActionLabelFeature,
    &kIPHSignoutWebInterceptFeature,
    &kIPHTabGroupsSaveV2IntroFeature,
    &kIPHTabGroupsSaveV2CloseGroupFeature,
    &kIPHTabGroupsSharedTabChangedFeature,
    &kIPHTabGroupsSharedTabFeedbackFeature,
    &kIPHTabOrganizationSuccessFeature,
    &kIPHTabSearchFeature,
    &kIPHTabSearchToolbarButtonFeature,
    &kIPHDesktopPwaInstallFeature,
    &kIPHProfileSwitchFeature,
    &kIPHDesktopSharedHighlightingFeature,
    &kIPHWebUiHelpBubbleTestFeature,
    &kIPHPriceTrackingInSidePanelFeature,
    &kIPHBackNavigationMenuFeature,
    &kIPHPwaQuietNotificationFeature,
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)
    &kIPHAutofillAiOptInFeature,
    &kIPHAutofillBnplAffirmOrZipSuggestionFeature,
    &kIPHAutofillBnplAffirmZipOrKlarnaSuggestionFeature,
    &kIPHAutofillCardInfoRetrievalSuggestionFeature,
    &kIPHAutofillCreditCardBenefitFeature,
    &kIPHAutofillDisabledVirtualCardSuggestionFeature,
    &kIPHAutofillEnableLoyaltyCardsFeature,
    &kIPHAutofillExternalAccountProfileSuggestionFeature,
    &kIPHAutofillHomeWorkProfileSuggestionFeature,
    &kIPHAutofillVirtualCardCVCSuggestionFeature,
    &kIPHAutofillVirtualCardSuggestionFeature,
    &kIPHCookieControlsFeature,
    &kIPHPlusAddressCreateSuggestionFeature,
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) ||
        // BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(IS_CHROMEOS)
    &kIPHGrowthFramework,
    &kIPHGoogleOneOfferNotificationFeature,
    &kIPHLauncherSearchHelpUiFeature,
    &kIPHScalableIphTimerBasedOneFeature,
    &kIPHScalableIphTimerBasedTwoFeature,
    &kIPHScalableIphTimerBasedThreeFeature,
    &kIPHScalableIphTimerBasedFourFeature,
    &kIPHScalableIphTimerBasedFiveFeature,
    &kIPHScalableIphTimerBasedSixFeature,
    &kIPHScalableIphTimerBasedSevenFeature,
    &kIPHScalableIphTimerBasedEightFeature,
    &kIPHScalableIphTimerBasedNineFeature,
    &kIPHScalableIphTimerBasedTenFeature,
    &kIPHScalableIphUnlockedBasedOneFeature,
    &kIPHScalableIphUnlockedBasedTwoFeature,
    &kIPHScalableIphUnlockedBasedThreeFeature,
    &kIPHScalableIphUnlockedBasedFourFeature,
    &kIPHScalableIphUnlockedBasedFiveFeature,
    &kIPHScalableIphUnlockedBasedSixFeature,
    &kIPHScalableIphUnlockedBasedSevenFeature,
    &kIPHScalableIphUnlockedBasedEightFeature,
    &kIPHScalableIphUnlockedBasedNineFeature,
    &kIPHScalableIphUnlockedBasedTenFeature,
    &kIPHScalableIphHelpAppBasedNudgeFeature,
    &kIPHScalableIphHelpAppBasedOneFeature,
    &kIPHScalableIphHelpAppBasedTwoFeature,
    &kIPHScalableIphHelpAppBasedThreeFeature,
    &kIPHScalableIphHelpAppBasedFourFeature,
    &kIPHScalableIphHelpAppBasedFiveFeature,
    &kIPHScalableIphHelpAppBasedSixFeature,
    &kIPHScalableIphHelpAppBasedSevenFeature,
    &kIPHScalableIphHelpAppBasedEightFeature,
    &kIPHScalableIphHelpAppBasedNineFeature,
    &kIPHScalableIphHelpAppBasedTenFeature,
    &kIPHScalableIphGamingFeature,
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    &kIPHDesktopPWAsLinkCapturingLaunch,
    &kIPHDesktopPWAsLinkCapturingLaunchAppInTab,
    &kIPHSupervisedUserProfileSigninFeature,
#endif  // BUILDFLAG(IS_WIN) ||  BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if !BUILDFLAG(IS_ANDROID)
    &kIPHiOSPasswordPromoDesktopFeature,
    &kIPHiOSAddressPromoDesktopFeature,
    &kIPHiOSPaymentPromoDesktopFeature
#endif  // !BUILDFLAG(IS_ANDROID)
};
}  // namespace

std::vector<const base::Feature*> GetAllFeatures() {
  return std::vector<const base::Feature*>(std::begin(kAllFeatures),
                                           std::end(kAllFeatures));
}

}  // namespace feature_engagement
