// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/authentication/ui_bundled/signin_earl_grey_ui_test_util.h"

#import "base/apple/foundation_util.h"
#import "base/test/ios/wait_util.h"
#import "base/time/time.h"
#import "ios/chrome/browser/authentication/ui_bundled/cells/signin_promo_view_constants.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin/signin_constants.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin_earl_grey.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin_matchers.h"
#import "ios/chrome/browser/authentication/ui_bundled/views/views_constants.h"
#import "ios/chrome/browser/recent_tabs/ui_bundled/recent_tabs_constants.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/google_services_settings_constants.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/manage_accounts/manage_accounts_table_view_controller_constants.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/manage_sync_settings_constants.h"
#import "ios/chrome/browser/shared/ui/table_view/table_view_navigation_controller_constants.h"
#import "ios/chrome/browser/signin/model/fake_system_identity.h"
#import "ios/chrome/browser/signin/model/test_constants.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey_ui.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/chrome_matchers_app_interface.h"
#import "ios/chrome/test/scoped_eg_synchronization_disabler.h"
#import "ios/testing/earl_grey/earl_grey_test.h"
#import "ui/base/l10n/l10n_util_mac.h"

using chrome_test_util::ButtonWithAccessibilityLabel;
using chrome_test_util::IdentityCellMatcherForEmail;
using chrome_test_util::PrimarySignInButton;
using chrome_test_util::SecondarySignInButton;
using chrome_test_util::SettingsAccountButton;
using chrome_test_util::SettingsDoneButton;
using chrome_test_util::SignOutAccountsButton;

namespace {

BOOL IsIdentityPossiblyManaged(id<SystemIdentity> identity) {
  return ![identity.userEmail hasSuffix:@"@gmail.com"];
}

void CloseHistorySyncSheet(BOOL enableHistorySync) {
  id<GREYMatcher> history_sync_matcher =
      grey_accessibilityID(kHistorySyncViewAccessibilityIdentifier);

  [ChromeEarlGrey waitForMatcher:history_sync_matcher];
  if (enableHistorySync) {
    [[EarlGrey selectElementWithMatcher:chrome_test_util::
                                            PromoScreenPrimaryButtonMatcher()]
        performAction:grey_tap()];
  } else {
    [[EarlGrey selectElementWithMatcher:chrome_test_util::
                                            PromoScreenSecondaryButtonMatcher()]
        performAction:grey_tap()];
  }
}

// Closes the "Sign out and delete data" dialog. That dialog may be shown when a
// managed account signs out.
void CloseManagedAccountSignOutAndDeleteDataDialog() {
  // Verify whether there is a confirmation dialog and interact with it to
  // complete the sign-in flow if present.
  id<GREYMatcher> acceptButton = [ChromeMatchersAppInterface
      buttonWithAccessibilityLabelID:
          IDS_IOS_SIGNOUT_AND_DELETE_DIALOG_SIGN_OUT_BUTTON];
  [ChromeEarlGrey waitForUIElementToAppearWithMatcher:acceptButton];
  [[EarlGrey selectElementWithMatcher:acceptButton] performAction:grey_tap()];
}

// Taps the sign-in sheet confirmation if the user is not signed-in yet, and
// the history opt-in confirmation if the user is not opted-in yet.
void MaybeTapSigninBottomSheetAndHistoryConfirmationDialog(
    FakeSystemIdentity* fakeIdentity) {
  if ([SigninEarlGrey isSignedOut]) {
    // First tap the "Continue as ..." button in the signin bottom sheet.
    [ChromeEarlGreyUI waitForAppToIdle];
    [[EarlGrey selectElementWithMatcher:chrome_test_util::
                                            WebSigninPrimaryButtonMatcher()]
        performAction:grey_tap()];
  }

  [ChromeEarlGreyUI waitForAppToIdle];
  [SigninEarlGrey closeManagedAccountSignInDialogIfAny:fakeIdentity];
  // If the history type isn't enabled yet, the history opt-in dialog should
  // show up now. Tap the "Yes, I'm In" button.
  if (![ChromeEarlGrey isSyncHistoryDataTypeSelected]) {
    [[EarlGrey selectElementWithMatcher:chrome_test_util::
                                            PromoScreenPrimaryButtonMatcher()]
        performAction:grey_tap()];
  }
}

// Returns a matcher for the sign out snackbar label.
id<GREYMatcher> SignOutSnackbarLabelMatcher() {
  NSString* snackbarLabel = l10n_util::GetNSString(
      IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_SIGN_OUT_SNACKBAR_MESSAGE);
  return grey_accessibilityLabel(snackbarLabel);
}

}  // namespace

@implementation SigninEarlGreyUI

+ (void)signinWithFakeIdentity:(FakeSystemIdentity*)fakeIdentity {
  [self signinWithFakeIdentity:fakeIdentity enableHistorySync:NO];
}

+ (void)signinWithFakeIdentity:(FakeSystemIdentity*)fakeIdentity
             enableHistorySync:(BOOL)enableHistorySync {
  GREYAssert([SigninEarlGrey isSignedOut],
             @"Can't sign in when already signed in");

  if (![SigninEarlGrey isIdentityAdded:fakeIdentity]) {
    // For convenience, add the identity, if it was not added yet.
    [SigninEarlGrey addFakeIdentity:fakeIdentity];
  }
  if ([SigninEarlGrey areSeparateProfilesForManagedAccountsEnabled] &&
      IsIdentityPossiblyManaged(fakeIdentity)) {
    [SigninEarlGrey signinWithFakeIdentity:fakeIdentity];
    [ChromeEarlGreyUI waitForAppToIdle];
    CloseHistorySyncSheet(enableHistorySync);
    return;
  }
  // TODO(crbug.com/335592853): There's no good reason why the with-history vs
  // without-history flows should be completely different, unify them.
  if (!enableHistorySync) {
    [SigninEarlGrey signinWithFakeIdentity:fakeIdentity];
    ConditionBlock condition = ^bool {
      return [[SigninEarlGrey primaryAccountGaiaID]
          isEqualToString:fakeIdentity.gaiaID];
    };
    BOOL isSigned = base::test::ios::WaitUntilConditionOrTimeout(
        base::test::ios::kWaitForActionTimeout, condition);
    GREYAssert(isSigned,
               @"Signed in failed. Expected: %@, Currently signed: %@",
               fakeIdentity.gaiaID, [SigninEarlGrey primaryAccountGaiaID]);
    return;
  }

  [SigninEarlGreyUI tapPrimarySignInButtonInRecentTabs];
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kIdentityButtonControlIdentifier)]
      performAction:grey_tap()];
  [[EarlGrey selectElementWithMatcher:IdentityCellMatcherForEmail(
                                          fakeIdentity.userEmail)]
      performAction:grey_tap()];

  MaybeTapSigninBottomSheetAndHistoryConfirmationDialog(fakeIdentity);

  [[[EarlGrey
      selectElementWithMatcher:grey_allOf(
                                   grey_accessibilityID(
                                       kTableViewNavigationDismissButtonId),
                                   grey_sufficientlyVisible(), nil)]
         usingSearchAction:grey_swipeSlowInDirection(kGREYDirectionUp)
      onElementWithMatcher:
          grey_allOf(grey_accessibilityID(
                         kRecentTabsTableViewControllerAccessibilityIdentifier),
                     grey_sufficientlyVisible(), nil)]
      performAction:grey_tap()];

  // Sync utilities require sync to be active in order to perform
  // operations on the Sync server.
  [ChromeEarlGrey waitForSyncTransportStateActiveWithTimeout:base::Seconds(10)];
}

+ (void)signOut {
  [self signOutWithClearDataConfirmation:NO];
}

+ (void)signOutWithClearDataConfirmation:(BOOL)expectClearDataConfirmation {
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];
  // Scroll to the signout button is at the very bottom.
  id<GREYMatcher> scrollViewMatcher =
      grey_accessibilityID(kManageSyncTableViewAccessibilityIdentifier);
  [[EarlGrey selectElementWithMatcher:scrollViewMatcher]
      performAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)];

  // Tap the "Sign out" button.
  [[EarlGrey selectElementWithMatcher:
                 grey_text(l10n_util::GetNSString(
                     IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_SIGN_OUT_ITEM))]
      performAction:grey_tap()];

  if (expectClearDataConfirmation) {
    CloseManagedAccountSignOutAndDeleteDataDialog();
  }

  // Close the snackbar, so that it can't obstruct other UI items.
  [self dismissSignoutSnackbar];

  // Wait until the user is signed out. Use a longer timeout for cases where
  // sign out also triggers a clear browsing data.
  [ChromeEarlGrey
      waitForUIElementToAppearWithMatcher:SettingsDoneButton()
                                  timeout:base::test::ios::
                                              kWaitForClearBrowsingDataTimeout];

  [[EarlGrey selectElementWithMatcher:SettingsDoneButton()]
      performAction:grey_tap()];
  [SigninEarlGrey verifySignedOut];
}

+ (void)dismissSignoutSnackbar {
  [ChromeEarlGrey
      waitForUIElementToAppearWithMatcher:SignOutSnackbarLabelMatcher()
                                  timeout:base::test::ios::
                                              kWaitForUIElementTimeout];
  // The tap closes the snackbar.
  [[EarlGrey selectElementWithMatcher:SignOutSnackbarLabelMatcher()]
      performAction:grey_tap()];
}

+ (void)verifySigninPromoVisibleWithMode:(SigninPromoViewMode)mode {
  [self verifySigninPromoVisibleWithMode:mode closeButton:YES];
}

+ (void)verifySigninPromoVisibleWithMode:(SigninPromoViewMode)mode
                             closeButton:(BOOL)closeButton {
  [ChromeEarlGreyUI waitForAppToIdle];

  [[EarlGrey
      selectElementWithMatcher:grey_allOf(PrimarySignInButton(),
                                          grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_notNil()];

  switch (mode) {
    case SigninPromoViewModeNoAccounts:
    case SigninPromoViewModeSignedInWithPrimaryAccount:
      [[EarlGrey
          selectElementWithMatcher:grey_allOf(SecondarySignInButton(),
                                              grey_sufficientlyVisible(), nil)]
          assertWithMatcher:grey_nil()];
      break;
    case SigninPromoViewModeSigninWithAccount:
      // TODO(crbug.com/40182627): Determine when the SecondarySignInButton
      // should be present and assert that.
      break;
  }

  if (closeButton) {
    [[EarlGrey
        selectElementWithMatcher:grey_allOf(grey_accessibilityID(
                                                kSigninPromoCloseButtonId),
                                            grey_sufficientlyVisible(), nil)]
        assertWithMatcher:grey_notNil()];
  }
}

+ (void)verifySigninPromoNotVisible {
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(
                                   grey_accessibilityID(kSigninPromoViewId),
                                   grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_nil()];
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(PrimarySignInButton(),
                                          grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_nil()];
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(SecondarySignInButton(),
                                          grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_nil()];
}

+ (void)openRemoveAccountConfirmationDialogWithFakeIdentity:
    (FakeSystemIdentity*)fakeIdentity {
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(ButtonWithAccessibilityLabel(
                                              fakeIdentity.userEmail),
                                          grey_sufficientlyVisible(), nil)]
      performAction:grey_tap()];

  [[EarlGrey selectElementWithMatcher:
                 chrome_test_util::ActionSheetItemWithAccessibilityLabelId(
                     IDS_IOS_REMOVE_GOOGLE_ACCOUNT_TITLE)]
      performAction:grey_tap()];
}

+ (void)tapRemoveAccountFromDeviceWithFakeIdentity:
    (FakeSystemIdentity*)fakeIdentity {
  [self openRemoveAccountConfirmationDialogWithFakeIdentity:fakeIdentity];
  [[EarlGrey selectElementWithMatcher:
                 chrome_test_util::ActionSheetItemWithAccessibilityLabelId(
                     IDS_IOS_REMOVE_ACCOUNT_LABEL)] performAction:grey_tap()];
  // Wait until the account is removed.
  [ChromeEarlGreyUI waitForAppToIdle];
}

+ (void)tapPrimarySignInButtonInRecentTabs {
  [SigninEarlGreyUI openRecentTabsAndTapButton:PrimarySignInButton()];
}

+ (void)tapPrimarySignInButtonInTabSwitcher {
  GREYAssert(![ChromeEarlGrey isTabGroupSyncEnabled],
             @"Recent Tabs is not available in Tab Grid when Tab Group Sync is "
             @"enabled, so there is no way to sign-in from Tab Switcher.");

  [ChromeEarlGreyUI openTabGrid];
  [[EarlGrey selectElementWithMatcher:chrome_test_util::
                                          TabGridOtherDevicesPanelButton()]
      performAction:grey_tap()];
  // The start point needs to avoid the "Done" bar on iPhone, in order to catch
  // the table view and scroll.
  [[[EarlGrey
      selectElementWithMatcher:grey_allOf(PrimarySignInButton(),
                                          grey_sufficientlyVisible(), nil)]
         usingSearchAction:grey_scrollToContentEdgeWithStartPoint(
                               kGREYContentEdgeBottom, 0.5, 0.5)
      onElementWithMatcher:
          grey_allOf(grey_accessibilityID(
                         kRecentTabsTableViewControllerAccessibilityIdentifier),
                     grey_sufficientlyVisible(), nil)]
      performAction:grey_tap()];
}

+ (void)verifyWebSigninIsVisible:(BOOL)isVisible {
  NSString* conditionDescription = isVisible
                                       ? @"Web sign-in should be visible"
                                       : @"Web sign-in should not be visible";
  id<GREYMatcher> matcher =
      isVisible ? grey_sufficientlyVisible() : grey_notVisible();
  GREYCondition* condition = [GREYCondition
      conditionWithName:conditionDescription
                  block:^BOOL {
                    NSError* error;
                    [[EarlGrey selectElementWithMatcher:
                                   grey_accessibilityID(
                                       kWebSigninAccessibilityIdentifier)]
                        assertWithMatcher:matcher
                                    error:&error];
                    return error == nil;
                  }];
  GREYAssertTrue([condition waitWithTimeout:10 pollInterval:0.1],
                 conditionDescription);
}

+ (void)submitSyncPassphrase:(NSString*)passphrase {
  [[EarlGrey selectElementWithMatcher:
                 grey_accessibilityID(
                     kSyncEncryptionPassphraseTextFieldAccessibilityIdentifier)]
      performAction:grey_replaceText(passphrase)];
  // grey_replaceText triggers textFieldDidEndEditing, which the
  // SyncEncryptionPassphraseTableViewController will treat as a signInPressed,
  // so there's no reason to tap the 'enter' button.
}

#pragma mark - Private

+ (void)openRecentTabsAndTapButton:(id<GREYMatcher>)buttonMatcher {
  [ChromeEarlGreyUI openToolsMenu];
  [ChromeEarlGreyUI
      tapToolsMenuButton:chrome_test_util::RecentTabsDestinationButton()];
  [[[EarlGrey
      selectElementWithMatcher:grey_allOf(buttonMatcher,
                                          grey_sufficientlyVisible(), nil)]
         usingSearchAction:grey_scrollToContentEdge(kGREYContentEdgeBottom)
      onElementWithMatcher:
          grey_allOf(grey_accessibilityID(
                         kRecentTabsTableViewControllerAccessibilityIdentifier),
                     grey_sufficientlyVisible(), nil)]
      performAction:grey_tap()];
}

+ (void)assertFakeAddAccountMenuDisplayed {
  // The existence of the "add account" accessibility button on screen verifies
  // that the screen  was shown.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kFakeAuthAddAccountButtonIdentifier)]
      assertWithMatcher:grey_notNil()];
}

+ (void)addFakeAccountInFakeAddAccountMenu:(FakeSystemIdentity*)fakeIdentity {
  [self addFakeAccountInFakeAddAccountMenu:fakeIdentity
                   withUnknownCapabilities:NO];
}

+ (void)addFakeAccountInFakeAddAccountMenu:(FakeSystemIdentity*)fakeIdentity
                   withUnknownCapabilities:(BOOL)unknownCapabilities {
  [SigninEarlGreyUI assertFakeAddAccountMenuDisplayed];
  [SigninEarlGrey addFakeIdentityForSSOAuthAddAccountFlow:fakeIdentity
                                  withUnknownCapabilities:unknownCapabilities];
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(
                                   grey_accessibilityID(
                                       kFakeAuthAddAccountButtonIdentifier),
                                   grey_sufficientlyVisible(), nil)]
      performAction:grey_tap()];
  // Make sure the fake SSO view controller is fully removed.
  [ChromeEarlGreyUI waitForAppToIdle];
}
@end
