// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/settings/ui_bundled/google_services/manage_sync_settings_mediator.h"

#import <optional>
#import <string>

#import "base/apple/foundation_util.h"
#import "base/auto_reset.h"
#import "base/check_op.h"
#import "base/containers/contains.h"
#import "base/containers/enum_set.h"
#import "base/containers/fixed_flat_map.h"
#import "base/i18n/message_formatter.h"
#import "base/memory/raw_ptr.h"
#import "base/notreached.h"
#import "base/strings/sys_string_conversions.h"
#import "components/prefs/pref_service.h"
#import "components/signin/public/base/consent_level.h"
#import "components/signin/public/base/signin_metrics.h"
#import "components/signin/public/identity_manager/account_info.h"
#import "components/signin/public/identity_manager/objc/identity_manager_observer_bridge.h"
#import "components/strings/grit/components_strings.h"
#import "components/sync/base/data_type.h"
#import "components/sync/base/user_selectable_type.h"
#import "components/sync/service/local_data_description.h"
#import "components/sync/service/sync_service.h"
#import "components/sync/service/sync_user_settings.h"
#import "ios/chrome/browser/authentication/ui_bundled/cells/central_account_view.h"
#import "ios/chrome/browser/authentication/ui_bundled/history_sync/history_sync_utils.h"
#import "ios/chrome/browser/net/model/crurl.h"
#import "ios/chrome/browser/policy/ui_bundled/management_util.h"
#import "ios/chrome/browser/settings/model/sync/utils/account_error_ui_info.h"
#import "ios/chrome/browser/settings/model/sync/utils/identity_error_util.h"
#import "ios/chrome/browser/settings/model/sync/utils/sync_util.h"
#import "ios/chrome/browser/settings/ui_bundled/cells/settings_image_detail_text_item.h"
#import "ios/chrome/browser/settings/ui_bundled/cells/sync_switch_item.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/features.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/manage_sync_settings_command_handler.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/manage_sync_settings_constants.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/manage_sync_settings_consumer.h"
#import "ios/chrome/browser/settings/ui_bundled/google_services/sync_error_settings_command_handler.h"
#import "ios/chrome/browser/shared/model/profile/features.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/list_model/list_model.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_image_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_info_button_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_link_header_footer_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_text_header_footer_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_text_item.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/browser/signin/model/authentication_service.h"
#import "ios/chrome/browser/signin/model/chrome_account_manager_service.h"
#import "ios/chrome/browser/signin/model/constants.h"
#import "ios/chrome/browser/sync/model/enterprise_utils.h"
#import "ios/chrome/browser/sync/model/sync_observer_bridge.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/common/ui/table_view/table_view_cells_constants.h"
#import "ios/chrome/grit/ios_branded_strings.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util.h"
#import "url/gurl.h"

using l10n_util::GetNSString;

namespace {

// Ordered list of all account data type switches.
// This is the list of available datatypes for account state `kSignedIn`.
static const syncer::UserSelectableType kAccountSwitchItems[] = {
    syncer::UserSelectableType::kHistory,
    syncer::UserSelectableType::kBookmarks,
    syncer::UserSelectableType::kReadingList,
    syncer::UserSelectableType::kAutofill,
    syncer::UserSelectableType::kPasswords,
    syncer::UserSelectableType::kPayments,
    syncer::UserSelectableType::kPreferences};

constexpr CGFloat kErrorSymbolPointSize = 22.;
constexpr CGFloat kBatchUploadSymbolPointSize = 22.;

}  // namespace

@interface ManageSyncSettingsMediator () <IdentityManagerObserverBridgeDelegate>

// Model item for each data types.
@property(nonatomic, strong) NSArray<TableViewItem*>* syncSwitchItems;
// Encryption item.
@property(nonatomic, strong) TableViewImageItem* encryptionItem;
// Sync error item.
@property(nonatomic, strong) TableViewItem* syncErrorItem;
// Batch upload item.
@property(nonatomic, strong) SettingsImageDetailTextItem* batchUploadItem;
// Returns YES if the sync data items should be enabled.
@property(nonatomic, assign, readonly) BOOL shouldSyncDataItemEnabled;
// Returns whether the Sync settings should be disabled because of a Sync error.
@property(nonatomic, assign, readonly) BOOL disabledBecauseOfSyncError;
// Returns YES if the user cannot turn on sync for enterprise policy reasons.
@property(nonatomic, assign, readonly) BOOL isSyncDisabledByAdministrator;

@end

@implementation ManageSyncSettingsMediator {
  // Sync observer.
  std::unique_ptr<SyncObserverBridge> _syncObserver;
  // Whether Sync State changes should be currently ignored.
  BOOL _ignoreSyncStateChanges;
  // Sync service.
  raw_ptr<syncer::SyncService> _syncService;
  // Identity manager.
  raw_ptr<signin::IdentityManager> _identityManager;
  // Observer for `IdentityManager`.
  std::unique_ptr<signin::IdentityManagerObserverBridge>
      _identityManagerObserver;
  // Authentication service.
  raw_ptr<AuthenticationService> _authenticationService;
  // Account manager service to retrieve Chrome identities.
  raw_ptr<ChromeAccountManagerService> _chromeAccountManagerService;
  // The pref service.
  raw_ptr<PrefService> _prefService;
  // Signed-in identity. Note: may be nil while signing out.
  id<SystemIdentity> _signedInIdentity;
}

- (instancetype)
      initWithSyncService:(syncer::SyncService*)syncService
          identityManager:(signin::IdentityManager*)identityManager
    authenticationService:(AuthenticationService*)authenticationService
    accountManagerService:(ChromeAccountManagerService*)accountManagerService
              prefService:(PrefService*)prefService {
  self = [super init];
  if (self) {
    DCHECK(syncService);
    CHECK(authenticationService);
    _syncService = syncService;
    _syncObserver = std::make_unique<SyncObserverBridge>(self, syncService);
    _identityManager = identityManager;
    _identityManagerObserver =
        std::make_unique<signin::IdentityManagerObserverBridge>(identityManager,
                                                                self);
    _authenticationService = authenticationService;
    _chromeAccountManagerService = accountManagerService;
    _signedInIdentity = _authenticationService->GetPrimaryIdentity(
        signin::ConsentLevel::kSignin);
    _prefService = prefService;
    // Register for font size change notifications
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(preferredContentSizeChanged:)
               name:UIContentSizeCategoryDidChangeNotification
             object:nil];
  }
  return self;
}

- (void)disconnect {
  _syncObserver.reset();
  _syncService = nullptr;
  _identityManager = nullptr;
  _identityManagerObserver.reset();
  _authenticationService = nullptr;
  self.commandHandler = nullptr;
  self.syncErrorHandler = nullptr;
  _chromeAccountManagerService = nullptr;
  _prefService = nullptr;
  _signedInIdentity = nil;
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)autofillAlertConfirmed:(BOOL)value {
  _syncService->GetUserSettings()->SetSelectedType(
      syncer::UserSelectableType::kAutofill, value);
}

#pragma mark - Loads sync data type section

// Loads the sync data type section.
- (void)loadSyncDataTypeSection {
  if (!self.accountStateSignedIn) {
    return;
  }

  TableViewModel* model = self.consumer.tableViewModel;

  BOOL would_clear_data_on_signout =
      _authenticationService->ShouldClearDataForSignedInPeriodOnSignOut();
  [model addSectionWithIdentifier:SyncDataTypeSectionIdentifier];
  TableViewTextHeaderFooterItem* headerItem =
      [[TableViewTextHeaderFooterItem alloc]
          initWithType:TypesListHeaderOrFooterType];
  headerItem.text =
      l10n_util::GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_TYPES_LIST_HEADER);

  [model setHeader:headerItem
      forSectionWithIdentifier:SyncDataTypeSectionIdentifier];

  TableViewTextHeaderFooterItem* footerItem =
      [[TableViewTextHeaderFooterItem alloc]
          initWithType:TypesListHeaderOrFooterType];
  footerItem.subtitle =
      would_clear_data_on_signout
          ? l10n_util::GetNSString(
                IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_TYPES_LIST_DESCRIPTION_FOR_MANAGED_ACCOUNT)
          : l10n_util::GetNSString(
                IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_TYPES_LIST_DESCRIPTION);
  [model setFooter:footerItem
      forSectionWithIdentifier:SyncDataTypeSectionIdentifier];
  NSMutableArray* syncSwitchItems = [[NSMutableArray alloc] init];
  for (syncer::UserSelectableType dataType : kAccountSwitchItems) {
    TableViewItem* switchItem = [self tableViewItemWithDataType:dataType];
    [syncSwitchItems addObject:switchItem];
    [model addItem:switchItem
        toSectionWithIdentifier:SyncDataTypeSectionIdentifier];
  }
  self.syncSwitchItems = syncSwitchItems;

  [self updateSyncItemsNotifyConsumer:NO];
}

// Returns the management state for this browser and profile.
- (ManagementState)managementState {
  return GetManagementState(_identityManager, _authenticationService,
                            _prefService);
}

// Updates the consumer when the primary account is updated.
- (void)updatePrimaryAccountDetails {
  if (!self.accountStateSignedIn) {
    return;
  }
  UIImage* avatarImage =
      _chromeAccountManagerService->GetIdentityAvatarWithIdentity(
          _signedInIdentity, IdentityAvatarSize::Large);
  NSString* managementDescription =
      GetManagementDescription([self managementState]);
  [self.consumer
      updatePrimaryAccountWithAvatarImage:avatarImage
                                     name:_signedInIdentity.userFullName
                                    email:_signedInIdentity.userEmail
                    managementDescription:managementDescription];
}

// Updates all the sync data type items, and notify the consumer if
// `notifyConsumer` is set to YES.
- (void)updateSyncItemsNotifyConsumer:(BOOL)notifyConsumer {
  if (!self.accountStateSignedIn) {
    return;
  }
  for (TableViewItem* item in self.syncSwitchItems) {
    if ([item isKindOfClass:[TableViewInfoButtonItem class]]) {
      continue;
    }

    SyncSwitchItem* syncSwitchItem =
        base::apple::ObjCCast<SyncSwitchItem>(item);
    syncer::UserSelectableType dataType =
        static_cast<syncer::UserSelectableType>(syncSwitchItem.dataType);
    BOOL isDataTypeSynced =
        _syncService->GetUserSettings()->GetSelectedTypes().Has(dataType);
    BOOL isEnabled = self.shouldSyncDataItemEnabled &&
                     ![self isManagedSyncSettingsDataType:dataType];

    if (dataType == syncer::UserSelectableType::kHistory) {
      // kHistory toggle represents both kHistory and kTabs in this case.
      // kHistory and kTabs should usually have the same value, but in some
      // cases they may not, e.g. if one of them is disabled by policy. In that
      // case, show the toggle as on if at least one of them is enabled. The
      // toggle should reflect the value of the non-disabled type.
      isDataTypeSynced =
          _syncService->GetUserSettings()->GetSelectedTypes().Has(
              syncer::UserSelectableType::kHistory) ||
          _syncService->GetUserSettings()->GetSelectedTypes().Has(
              syncer::UserSelectableType::kTabs);
      isEnabled = self.shouldSyncDataItemEnabled &&
                  (![self isManagedSyncSettingsDataType:
                              syncer::UserSelectableType::kHistory] ||
                   ![self isManagedSyncSettingsDataType:
                              syncer::UserSelectableType::kTabs]);
    }
    BOOL needsUpdate = (syncSwitchItem.on != isDataTypeSynced) ||
                       (syncSwitchItem.isEnabled != isEnabled);
    syncSwitchItem.on = isDataTypeSynced;
    syncSwitchItem.enabled = isEnabled;
    if (needsUpdate && notifyConsumer) {
      [self.consumer reloadItem:syncSwitchItem];
    }
  }
}

#pragma mark - Loads the advanced settings section

// Loads the advanced settings section.
- (void)loadAdvancedSettingsSection {
  if (!self.accountStateSignedIn) {
    return;
  }
  if (self.isSyncDisabledByAdministrator) {
    return;
  }
  TableViewModel* model = self.consumer.tableViewModel;
  [model addSectionWithIdentifier:AdvancedSettingsSectionIdentifier];
  // EncryptionItemType.
  self.encryptionItem =
      [[TableViewImageItem alloc] initWithType:EncryptionItemType];
  self.encryptionItem.accessibilityIdentifier =
      kEncryptionAccessibilityIdentifier;
  self.encryptionItem.title = GetNSString(IDS_IOS_MANAGE_SYNC_ENCRYPTION);
  // The detail text (if any) is an error message, so color it in red.
  self.encryptionItem.detailTextColor = [UIColor colorNamed:kRedColor];
  // For kNeedsTrustedVaultKey, the disclosure indicator should not
  // be shown since the reauth dialog for the trusted vault is presented from
  // the bottom, and is not part of navigation controller.
  const syncer::SyncService::UserActionableError error =
      _syncService->GetUserActionableError();
  BOOL hasDisclosureIndicator =
      error != syncer::SyncService::UserActionableError::
                   kNeedsTrustedVaultKeyForPasswords &&
      error != syncer::SyncService::UserActionableError::
                   kNeedsTrustedVaultKeyForEverything;
  if (hasDisclosureIndicator) {
    self.encryptionItem.accessoryView = [[UIImageView alloc]
        initWithImage:DefaultAccessorySymbolConfigurationWithRegularWeight(
                          kChevronForwardSymbol)];
    self.encryptionItem.accessoryView.tintColor =
        [UIColor colorNamed:kTextQuaternaryColor];
  } else {
    self.encryptionItem.accessoryView = nil;
  }
  self.encryptionItem.accessibilityTraits |= UIAccessibilityTraitButton;
  [self updateEncryptionItem:NO];
  [model addItem:self.encryptionItem
      toSectionWithIdentifier:AdvancedSettingsSectionIdentifier];

  if (IsLinkedServicesSettingIosEnabled()) {
    // PersonalizeGoogleServicesItemType.
    TableViewImageItem* personalizeGoogleServicesItem =
        [[TableViewImageItem alloc]
            initWithType:PersonalizeGoogleServicesItemType];
    if (self.isEEAAccount) {
      personalizeGoogleServicesItem.title = GetNSString(
          IDS_IOS_MANAGE_SYNC_PERSONALIZE_GOOGLE_SERVICES_TITLE_EEA);
      personalizeGoogleServicesItem.accessoryView = [[UIImageView alloc]
          initWithImage:DefaultAccessorySymbolConfigurationWithRegularWeight(
                            kChevronForwardSymbol)];
    } else {
      personalizeGoogleServicesItem.title =
          GetNSString(IDS_IOS_MANAGE_SYNC_PERSONALIZE_GOOGLE_SERVICES_TITLE);
      personalizeGoogleServicesItem.accessoryView = [[UIImageView alloc]
          initWithImage:DefaultAccessorySymbolConfigurationWithRegularWeight(
                            kExternalLinkSymbol)];
    }
    personalizeGoogleServicesItem.accessoryView.tintColor =
        [UIColor colorNamed:kTextQuaternaryColor];
    personalizeGoogleServicesItem.detailText = GetNSString(
        IDS_IOS_MANAGE_SYNC_PERSONALIZE_GOOGLE_SERVICES_DESCRIPTION);
    personalizeGoogleServicesItem.accessibilityIdentifier =
        kPersonalizeGoogleServicesIdentifier;
    personalizeGoogleServicesItem.accessibilityTraits |=
        UIAccessibilityTraitButton;
    [model addItem:personalizeGoogleServicesItem
        toSectionWithIdentifier:AdvancedSettingsSectionIdentifier];
  } else {
    // GoogleActivityControlsItemType.
    TableViewImageItem* googleActivityControlsItem = [[TableViewImageItem alloc]
        initWithType:GoogleActivityControlsItemType];
    googleActivityControlsItem.accessoryView = [[UIImageView alloc]
        initWithImage:DefaultAccessorySymbolConfigurationWithRegularWeight(
                          kExternalLinkSymbol)];
    googleActivityControlsItem.accessoryView.tintColor =
        [UIColor colorNamed:kTextQuaternaryColor];
    googleActivityControlsItem.title =
        GetNSString(IDS_IOS_MANAGE_SYNC_GOOGLE_ACTIVITY_CONTROLS_TITLE);
    googleActivityControlsItem.detailText =
        GetNSString(IDS_IOS_MANAGE_SYNC_GOOGLE_ACTIVITY_CONTROLS_DESCRIPTION);
    googleActivityControlsItem.accessibilityTraits |=
        UIAccessibilityTraitButton;
    [model addItem:googleActivityControlsItem
        toSectionWithIdentifier:AdvancedSettingsSectionIdentifier];
  }

  // AdvancedSettingsSectionIdentifier.
  TableViewImageItem* dataFromChromeSyncItem =
      [[TableViewImageItem alloc] initWithType:DataFromChromeSync];
  dataFromChromeSyncItem.accessoryView = [[UIImageView alloc]
      initWithImage:DefaultAccessorySymbolConfigurationWithRegularWeight(
                        kExternalLinkSymbol)];
  dataFromChromeSyncItem.accessoryView.tintColor =
      [UIColor colorNamed:kTextQuaternaryColor];
  dataFromChromeSyncItem.accessibilityIdentifier =
      kDataFromChromeSyncAccessibilityIdentifier;
  dataFromChromeSyncItem.accessibilityTraits |= UIAccessibilityTraitButton;

  dataFromChromeSyncItem.title =
      GetNSString(IDS_IOS_MANAGE_DATA_IN_YOUR_ACCOUNT_TITLE);
  dataFromChromeSyncItem.detailText =
      GetNSString(IDS_IOS_MANAGE_DATA_IN_YOUR_ACCOUNT_DESCRIPTION);
  [model addItem:dataFromChromeSyncItem
      toSectionWithIdentifier:AdvancedSettingsSectionIdentifier];
}

// Updates encryption item, and notifies the consumer if `notifyConsumer` is set
// to YES.
- (void)updateEncryptionItem:(BOOL)notifyConsumer {
  if (!self.accountStateSignedIn) {
    return;
  }
  if (![self.consumer.tableViewModel
          hasSectionForSectionIdentifier:AdvancedSettingsSectionIdentifier]) {
    return;
  }
  if (self.isSyncDisabledByAdministrator) {
    [self.consumer.tableViewModel
        removeSectionWithIdentifier:AdvancedSettingsSectionIdentifier];
    return;
  }
  BOOL needsUpdate =
      self.shouldEncryptionItemBeEnabled &&
      (self.encryptionItem.enabled != self.shouldEncryptionItemBeEnabled);
  self.encryptionItem.enabled = self.shouldEncryptionItemBeEnabled;
  if (self.shouldEncryptionItemBeEnabled) {
    self.encryptionItem.textColor = nil;
  } else {
    self.encryptionItem.textColor = [UIColor colorNamed:kTextSecondaryColor];
  }
  if (needsUpdate && notifyConsumer) {
    [self.consumer reloadItem:self.encryptionItem];
  }
}

#pragma mark - Loads sign out section

// Creates a footer item to display below the sign out button when forced
// sign-in is enabled.
- (TableViewItem*)createForcedSigninFooterItem {
  // Add information about the forced sign-in policy below the sign-out
  // button when forced sign-in is enabled.
  TableViewLinkHeaderFooterItem* footerItem =
      [[TableViewLinkHeaderFooterItem alloc]
          initWithType:SignOutItemFooterType];
  footerItem.text = l10n_util::GetNSString(
      IDS_IOS_ENTERPRISE_FORCED_SIGNIN_MESSAGE_WITH_LEARN_MORE);
  footerItem.urls =
      @[ [[CrURL alloc] initWithGURL:GURL("chrome://management/")] ];
  return footerItem;
}

- (void)updateSignOutSection {
  TableViewModel* model = self.consumer.tableViewModel;
  BOOL hasSignOutSection =
      [model hasSectionForSectionIdentifier:ManageAndSignOutSectionIdentifier];

  if (!self.accountStateSignedIn) {
    return;
  }
  // There should be a sign-out section. Load it if it's not there yet.
  if (!hasSignOutSection) {
    [self loadSignOutAndManageAccountsSection];
    [self loadSwitchAccountAndSignOutSection];
    NSUInteger sectionIndex =
        [model sectionForSectionIdentifier:ManageAndSignOutSectionIdentifier];
    [self.consumer insertSections:[NSIndexSet indexSetWithIndex:sectionIndex]
                     rowAnimation:NO];
  }
}

- (void)loadSignOutAndManageAccountsSection {
  if (!self.accountStateSignedIn) {
    return;
  }

  // Creates the manage accounts and sign-out section.
  TableViewModel* model = self.consumer.tableViewModel;
  // The AdvancedSettingsSectionIdentifier does not exist when sync is disabled
  // by administrator for a signed-in account.
  NSInteger previousSection =
      [model hasSectionForSectionIdentifier:AdvancedSettingsSectionIdentifier]
          ? [model
                sectionForSectionIdentifier:AdvancedSettingsSectionIdentifier]
          : [model sectionForSectionIdentifier:SyncDataTypeSectionIdentifier];
  CHECK_NE(NSNotFound, previousSection);
  [model insertSectionWithIdentifier:ManageAndSignOutSectionIdentifier
                             atIndex:previousSection + 1];

  // Creates items in the manage accounts and sign-out section.
  // Manage Google Account item.
  TableViewTextItem* item =
      [[TableViewTextItem alloc] initWithType:ManageGoogleAccountItemType];
  item.text =
      GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_MANAGE_GOOGLE_ACCOUNT_ITEM);
  item.textColor = [UIColor colorNamed:kBlueColor];
  item.accessibilityTraits |= UIAccessibilityTraitButton;
  [model addItem:item
      toSectionWithIdentifier:ManageAndSignOutSectionIdentifier];

  if (base::FeatureList::IsEnabled(kIOSManageAccountStorage)) {
    // Manage account storage item.
    item = [[TableViewTextItem alloc] initWithType:ManageAccountStorageType];
    item.text =
        GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_MANAGE_STORAGE_ITEM);
    item.textColor = [UIColor colorNamed:kBlueColor];
    item.accessibilityTraits |= UIAccessibilityTraitButton;
    [model addItem:item
        toSectionWithIdentifier:ManageAndSignOutSectionIdentifier];
  }

  // Manage accounts on this device item.
  item = [[TableViewTextItem alloc] initWithType:ManageAccountsItemType];
  item.text = GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_MANAGE_ACCOUNTS_ITEM);
  item.textColor = [UIColor colorNamed:kBlueColor];
  item.accessibilityTraits |= UIAccessibilityTraitButton;
  [model addItem:item
      toSectionWithIdentifier:ManageAndSignOutSectionIdentifier];

  // If kSeparateProfilesForManagedAccounts is disabled, the signout button
  // exists in the ManageAndSignOutSection.
  if (!AreSeparateProfilesForManagedAccountsEnabled()) {
    // Sign out item.
    item = [[TableViewTextItem alloc] initWithType:SignOutItemType];
    item.text = GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_SIGN_OUT_ITEM);
    item.textColor = [UIColor colorNamed:kBlueColor];
    item.accessibilityTraits |= UIAccessibilityTraitButton;
    [model addItem:item
        toSectionWithIdentifier:ManageAndSignOutSectionIdentifier];

    if (self.forcedSigninEnabled) {
      [model setFooter:[self createForcedSigninFooterItem]
          forSectionWithIdentifier:ManageAndSignOutSectionIdentifier];
    }
  }
}

- (void)loadSwitchAccountAndSignOutSection {
  if (!self.accountStateSignedIn ||
      !AreSeparateProfilesForManagedAccountsEnabled()) {
    return;
  }

  TableViewModel* model = self.consumer.tableViewModel;
  NSInteger previousSection =
      [model sectionForSectionIdentifier:ManageAndSignOutSectionIdentifier];
  CHECK_NE(NSNotFound, previousSection);
  [model insertSectionWithIdentifier:SwitchAccountAndSignOutSectionIdentifier
                             atIndex:previousSection + 1];

  // If kSeparateProfilesForManagedAccounts is enabled, the signout button
  // exists in its own section along with the switch profile item.

  // Creates items in the switch account and sign-out section.
  // Switch Account item.
  TableViewTextItem* item =
      [[TableViewTextItem alloc] initWithType:SwitchAccountItemType];
  item.text = l10n_util::GetNSString(
      IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_SWITCH_ACCOUNT_ITEM);
  item.textColor = [UIColor colorNamed:kBlueColor];
  [model addItem:item
      toSectionWithIdentifier:SwitchAccountAndSignOutSectionIdentifier];

  // Sign out item.
  item = [[TableViewTextItem alloc] initWithType:SignOutItemType];
  item.text = GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_SIGN_OUT_ITEM);
  item.textColor = [UIColor colorNamed:kBlueColor];
  [model addItem:item
      toSectionWithIdentifier:SwitchAccountAndSignOutSectionIdentifier];
  if (self.forcedSigninEnabled) {
    [model setFooter:[self createForcedSigninFooterItem]
        forSectionWithIdentifier:SwitchAccountAndSignOutSectionIdentifier];
  }
}

#pragma mark - Loads batch upload section

- (TableViewTextItem*)batchUploadButtonItem {
  TableViewTextItem* item =
      [[TableViewTextItem alloc] initWithType:BatchUploadButtonItemType];
  item.text = l10n_util::GetNSString(
      IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_BATCH_UPLOAD_BUTTON_ITEM);
  item.textColor = [UIColor colorNamed:kBlueColor];
  item.accessibilityIdentifier = kBatchUploadAccessibilityIdentifier;
  return item;
}

- (NSString*)itemsToUploadRecommendationString {
  // _localPasswordsToUpload and _localItemsToUpload should be updated by
  // updateBatchUploadSectionWithNotifyConsumer before calling this method,
  // which also checks for the case of having no items to upload, thus this case
  // is not reached here.
  if (!_localPasswordsToUpload && !_localItemsToUpload) {
    NOTREACHED();
  }

  std::u16string userEmail =
      base::SysNSStringToUTF16(_signedInIdentity.userEmail);

  std::u16string itemsToUploadString;
  if (_localPasswordsToUpload == 0) {
    // No passwords, but there are other items to upload.
    itemsToUploadString = base::i18n::MessageFormatter::FormatWithNamedArgs(
        l10n_util::GetStringUTF16(
            IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_BATCH_UPLOAD_ITEMS_ITEM),
        "count", static_cast<int>(_localItemsToUpload), "email", userEmail);
  } else if (_localItemsToUpload > 0) {
    // Multiple passwords and items to upload.
    itemsToUploadString = base::i18n::MessageFormatter::FormatWithNamedArgs(
        l10n_util::GetStringUTF16(
            IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_BATCH_UPLOAD_PASSWORDS_AND_ITEMS_ITEM),
        "count", static_cast<int>(_localPasswordsToUpload), "email", userEmail);
  } else {
    // No items, but there are passwords to upload.
    itemsToUploadString = base::i18n::MessageFormatter::FormatWithNamedArgs(
        l10n_util::GetStringUTF16(
            IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_BATCH_UPLOAD_PASSWORDS_ITEM),
        "count", static_cast<int>(_localPasswordsToUpload), "email", userEmail);
  }
  return base::SysUTF16ToNSString(itemsToUploadString);
}

- (SettingsImageDetailTextItem*)batchUploadRecommendationItem {
  SettingsImageDetailTextItem* item = [[SettingsImageDetailTextItem alloc]
      initWithType:BatchUploadRecommendationItemType];
  item.detailText = [self itemsToUploadRecommendationString];
  item.image = CustomSymbolWithPointSize(kCloudAndArrowUpSymbol,
                                         kBatchUploadSymbolPointSize);
  item.imageViewTintColor = [UIColor colorNamed:kBlueColor];
  item.accessibilityIdentifier =
      kBatchUploadRecommendationItemAccessibilityIdentifier;
  return item;
}

// Loads the batch upload section.
- (void)loadBatchUploadSection {
  // Drop the batch upload item from a previous loading.
  self.batchUploadItem = nil;
  // Create the batch upload section and item if needed.
  [self updateBatchUploadSectionWithNotifyConsumer:NO firstLoad:YES];
}

// Fetches the local data descriptions from the sync server, and calls
// `-[ManageSyncSettingsMediator localDataDescriptionsFetchedWithDescription:]`
// to process those description.
- (void)fetchLocalDataDescriptionsForBatchUploadWithFirstLoad:(BOOL)firstLoad {
  if (!self.accountStateSignedIn) {
    return;
  }

  // Types that are disabled by policy will be ignored.
  syncer::DataTypeSet requestedTypes;
  for (syncer::UserSelectableType userSelectableType : kAccountSwitchItems) {
    if (![self isManagedSyncSettingsDataType:userSelectableType]) {
      requestedTypes.Put(
          syncer::UserSelectableTypeToCanonicalDataType(userSelectableType));
    }
  }

  __weak __typeof__(self) weakSelf = self;
  _syncService->GetLocalDataDescriptions(
      requestedTypes,
      base::BindOnce(^(std::map<syncer::DataType, syncer::LocalDataDescription>
                           description) {
        [weakSelf localDataDescriptionsFetchedWithDescription:description
                                                    firstLoad:firstLoad];
      }));
}

// Saves the local data description, and update the batch upload section.
- (void)localDataDescriptionsFetchedWithDescription:
            (std::map<syncer::DataType, syncer::LocalDataDescription>)
                description
                                          firstLoad:(BOOL)firstLoad {
  self.localPasswordsToUpload = 0;
  self.localItemsToUpload = 0;

  for (auto& type : description) {
    if (type.first == syncer::PASSWORDS) {
      self.localPasswordsToUpload = type.second.item_count;
      continue;
    } else {
      self.localItemsToUpload += type.second.item_count;
    }
  }
  [self updateBatchUploadSectionWithNotifyConsumer:YES firstLoad:firstLoad];
}

// Deletes the batch upload section and notifies the consumer about model
// changes.
- (void)removeBatchUploadSection {
  TableViewModel* model = self.consumer.tableViewModel;
  if (![model hasSectionForSectionIdentifier:BatchUploadSectionIdentifier]) {
    return;
  }
  NSInteger sectionIndex =
      [model sectionForSectionIdentifier:BatchUploadSectionIdentifier];
  [model removeSectionWithIdentifier:BatchUploadSectionIdentifier];
  self.batchUploadItem = nil;

  // Remove the batch upload section from the table view model.
  NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:sectionIndex];
  [self.consumer deleteSections:indexSet rowAnimation:YES];
}

// Updates the batch upload section according to data already fetched.
// `notifyConsummer` if YES, call the consumer to update the table view.
// `firstLoad` if YES, load the section without animations.
- (void)updateBatchUploadSectionWithNotifyConsumer:(BOOL)notifyConsummer
                                         firstLoad:(BOOL)firstLoad {
  // Batch upload option is not shown if sync is disabled by policy, if the
  // account is in a persistent error state that requires a user action, or if
  // there is no local data to offer the batch upload.
  if (self.syncErrorItem || self.isSyncDisabledByAdministrator ||
      (!_localPasswordsToUpload && !_localItemsToUpload)) {
    [self removeBatchUploadSection];
    return;
  }

  TableViewModel* model = self.consumer.tableViewModel;
  DCHECK(![model hasSectionForSectionIdentifier:SyncErrorsSectionIdentifier]);

  if (!self.accountStateSignedIn) {
    return;
  }

  NSInteger batchUploadSectionIndex = 0;

  BOOL batchUploadSectionAlreadyExists = self.batchUploadItem;
  if (!batchUploadSectionAlreadyExists) {
    // Creates the batch upload section.
    [model insertSectionWithIdentifier:BatchUploadSectionIdentifier
                               atIndex:batchUploadSectionIndex];
    self.batchUploadItem = [self batchUploadRecommendationItem];
    [model addItem:self.batchUploadItem
        toSectionWithIdentifier:BatchUploadSectionIdentifier];
    [model addItem:[self batchUploadButtonItem]
        toSectionWithIdentifier:BatchUploadSectionIdentifier];
  } else {
    // The section already exists, update it.
    self.batchUploadItem.detailText = [self itemsToUploadRecommendationString];
    [self.consumer reloadItem:self.batchUploadItem];
  }

  if (!notifyConsummer) {
    return;
  }
  NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:batchUploadSectionIndex];
  if (batchUploadSectionAlreadyExists) {
    // The section should be updated if it already exists.
    [self.consumer reloadSections:indexSet];
  } else {
    // The animation is not needed if this is a first time load of the card.
    [self.consumer insertSections:indexSet rowAnimation:!firstLoad];
  }
}

#pragma mark - Private

// Creates a SyncSwitchItem or TableViewInfoButtonItem instance if the item is
// managed.
- (TableViewItem*)tableViewItemWithDataType:
    (syncer::UserSelectableType)dataType {
  NSInteger itemType = 0;
  int textStringID = 0;
  NSString* accessibilityIdentifier = nil;
  switch (dataType) {
    case syncer::UserSelectableType::kBookmarks:
      itemType = BookmarksDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_BOOKMARKS;
      accessibilityIdentifier = kSyncBookmarksIdentifier;
      break;
    case syncer::UserSelectableType::kHistory:
      itemType = HistoryDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_HISTORY_AND_TABS;
      accessibilityIdentifier = kSyncHistoryAndTabsIdentifier;
      break;
    case syncer::UserSelectableType::kPasswords:
      itemType = PasswordsDataTypeItemType;
      textStringID = IOSPasskeysM2Enabled()
                         ? IDS_SYNC_DATATYPE_PASSWORDS_AND_PASSKEYS
                         : IDS_SYNC_DATATYPE_PASSWORDS;
      accessibilityIdentifier = kSyncPasswordsIdentifier;
      break;
    case syncer::UserSelectableType::kTabs:
      itemType = OpenTabsDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_TABS;
      accessibilityIdentifier = kSyncOpenTabsIdentifier;
      break;
    case syncer::UserSelectableType::kAutofill:
      itemType = AutofillDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_AUTOFILL;
      accessibilityIdentifier = kSyncAutofillIdentifier;
      break;
    case syncer::UserSelectableType::kPayments:
      itemType = PaymentsDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_PAYMENTS;
      accessibilityIdentifier = kSyncPaymentsIdentifier;
      break;
    case syncer::UserSelectableType::kPreferences:
      itemType = SettingsDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_PREFERENCES;
      accessibilityIdentifier = kSyncPreferencesIdentifier;
      break;
    case syncer::UserSelectableType::kReadingList:
      itemType = ReadingListDataTypeItemType;
      textStringID = IDS_SYNC_DATATYPE_READING_LIST;
      accessibilityIdentifier = kSyncReadingListIdentifier;
      break;
    case syncer::UserSelectableType::kThemes:
    case syncer::UserSelectableType::kExtensions:
    case syncer::UserSelectableType::kApps:
    case syncer::UserSelectableType::kSavedTabGroups:
    case syncer::UserSelectableType::kProductComparison:
    case syncer::UserSelectableType::kCookies:
      NOTREACHED();
  }
  DCHECK_NE(itemType, 0);
  DCHECK_NE(textStringID, 0);
  DCHECK(accessibilityIdentifier);

  BOOL isToggleEnabled = ![self isManagedSyncSettingsDataType:dataType];
  if (dataType == syncer::UserSelectableType::kHistory) {
    // kHistory toggle represents both kHistory and kTabs.
    // kHistory and kTabs should usually have the same value, but in some
    // cases they may not, e.g. if one of them is disabled by policy. In that
    // case, show the toggle as on if at least one of them is enabled. The
    // toggle should reflect the value of the non-disabled type.
    isToggleEnabled =
        ![self isManagedSyncSettingsDataType:syncer::UserSelectableType::
                                                 kHistory] ||
        ![self isManagedSyncSettingsDataType:syncer::UserSelectableType::kTabs];
  }
  if (isToggleEnabled) {
    SyncSwitchItem* switchItem = [[SyncSwitchItem alloc] initWithType:itemType];
    switchItem.text = GetNSString(textStringID);
    switchItem.dataType = static_cast<NSInteger>(dataType);
    switchItem.accessibilityIdentifier = accessibilityIdentifier;
    return switchItem;
  } else {
    TableViewInfoButtonItem* button =
        [[TableViewInfoButtonItem alloc] initWithType:itemType];
    button.text = GetNSString(textStringID);
    button.statusText = GetNSString(IDS_IOS_SETTING_OFF);
    button.accessibilityIdentifier = accessibilityIdentifier;
    return button;
  }
}

// Updates the consumer when the content size is updated.
- (void)preferredContentSizeChanged:(NSNotification*)notification {
  [self updatePrimaryAccountDetails];
}

#pragma mark - Properties

- (BOOL)disabledBecauseOfSyncError {
  return _syncService->GetDisableReasons().Has(
      syncer::SyncService::DISABLE_REASON_UNRECOVERABLE_ERROR);
}

- (BOOL)shouldSyncDataItemEnabled {
  if (!self.accountStateSignedIn) {
    return NO;
  }
  return !self.disabledBecauseOfSyncError;
}

- (BOOL)shouldEncryptionItemBeEnabled {
  // Note, that it is not enough to check whether UserActionableError is
  // kNeedsTrustedVaultKeyForPasswords or kNeedsTrustedVaultKeyForEverything
  // because sync might currently attempt to silently fetch the trusted vault
  // keys.
  return !self.disabledBecauseOfSyncError &&
         !_syncService->GetUserSettings()->IsTrustedVaultKeyRequired() &&
         _syncService->GetUserSettings()->IsCustomPassphraseAllowed();
}

- (NSString*)overrideViewControllerTitle {
  if (self.accountStateSignedIn) {
    return l10n_util::GetNSString(IDS_IOS_GOOGLE_ACCOUNT_SETTINGS_TITLE);
  }
  return nil;
}

- (BOOL)accountStateSignedIn {
  // As the manage sync settings mediator is running, the account state
  // does not change except only when the user signs out of their account.
  if (_syncService->GetAccountInfo().IsEmpty()) {
    return NO;
  }
  return YES;
}

#pragma mark - ManageSyncSettingsTableViewControllerModelDelegate

- (void)manageSyncSettingsTableViewControllerLoadModel:
    (id<ManageSyncSettingsConsumer>)controller {
  DCHECK_EQ(self.consumer, controller);
  if (!_authenticationService->GetPrimaryIdentity(
          signin::ConsentLevel::kSignin)) {
    // If the user signed out from this view or a child controller the view is
    // closing and should not re-load the model.
    return;
  }
  [self loadSyncErrorsSection];
  [self loadBatchUploadSection];
  [self loadSyncDataTypeSection];
  [self loadAdvancedSettingsSection];
  [self loadSignOutAndManageAccountsSection];
  [self loadSwitchAccountAndSignOutSection];
  [self fetchLocalDataDescriptionsForBatchUploadWithFirstLoad:YES];
  // Loading the header asks the consumer to reload the data, so it should be
  // done after all sections are initially loaded.
  [self updatePrimaryAccountDetails];
}

#pragma mark - SyncObserverModelBridge

- (void)onSyncStateChanged {
  if (_ignoreSyncStateChanges || self.signOutFlowInProgress) {
    // The UI should not updated so the switch animations can run smoothly.
    return;
  }
  if (!_syncService->GetDisableReasons().empty()) {
    [self.commandHandler closeManageSyncSettings];
    return;
  }
  [self updateSyncErrorsSection:YES];
  [self updateBatchUploadSectionWithNotifyConsumer:YES firstLoad:NO];
  [self updateSyncItemsNotifyConsumer:YES];
  [self updateEncryptionItem:YES];
  [self updateSignOutSection];
  [self fetchLocalDataDescriptionsForBatchUploadWithFirstLoad:NO];
}

#pragma mark - IdentityManagerObserverBridgeDelegate

- (void)onExtendedAccountInfoUpdated:(const AccountInfo&)info {
  id<SystemIdentity> identity =
      _chromeAccountManagerService->GetIdentityOnDeviceWithGaiaID(info.gaia);
  if ([_signedInIdentity isEqual:identity]) {
    [self updatePrimaryAccountDetails];
    [self updateSyncItemsNotifyConsumer:YES];
    [self updateSyncErrorsSection:YES];
    [self updateBatchUploadSectionWithNotifyConsumer:YES firstLoad:NO];
    [self updateEncryptionItem:YES];
    [self fetchLocalDataDescriptionsForBatchUploadWithFirstLoad:NO];
  }
}

- (void)onPrimaryAccountChanged:
    (const signin::PrimaryAccountChangeEvent&)event {
  switch (event.GetEventTypeFor(signin::ConsentLevel::kSignin)) {
    case signin::PrimaryAccountChangeEvent::Type::kSet:
      _signedInIdentity = _authenticationService->GetPrimaryIdentity(
          signin::ConsentLevel::kSignin);
      [self updatePrimaryAccountDetails];
      break;
    case signin::PrimaryAccountChangeEvent::Type::kCleared:
      // Temporary state, we can ignore this event, until the UI is signed out.
    case signin::PrimaryAccountChangeEvent::Type::kNone:
      break;
  }
}

#pragma mark - ManageSyncSettingsServiceDelegate

- (void)toggleSwitchItem:(TableViewItem*)item withValue:(BOOL)value {
  {
    SyncSwitchItem* syncSwitchItem =
        base::apple::ObjCCast<SyncSwitchItem>(item);
    syncSwitchItem.on = value;
    if (value &&
        static_cast<syncer::UserSelectableType>(syncSwitchItem.dataType) ==
            syncer::UserSelectableType::kAutofill &&
        _syncService->GetUserSettings()->IsUsingExplicitPassphrase()) {
      [self.commandHandler showAdressesNotEncryptedDialog];
      return;
    }

    // The notifications should be ignored to get smooth switch animations.
    // Notifications are sent by SyncObserverModelBridge while changing
    // settings.
    base::AutoReset<BOOL> autoReset(&_ignoreSyncStateChanges, YES);
    SyncSettingsItemType itemType =
        static_cast<SyncSettingsItemType>(item.type);
    switch (itemType) {
      case HistoryDataTypeItemType: {
        DCHECK(syncSwitchItem);
        // Update History Sync decline prefs.
        value ? history_sync::ResetDeclinePrefs(_prefService)
              : history_sync::RecordDeclinePrefs(_prefService);
        // Don't try to toggle the managed item.
        if (![self isManagedSyncSettingsDataType:syncer::UserSelectableType::
                                                     kHistory]) {
          _syncService->GetUserSettings()->SetSelectedType(
              syncer::UserSelectableType::kHistory, value);
        }
        // The kTabs toggle does not exist. Instead it's
        // controlled by the history toggle.
        if (![self isManagedSyncSettingsDataType:syncer::UserSelectableType::
                                                     kTabs]) {
          _syncService->GetUserSettings()->SetSelectedType(
              syncer::UserSelectableType::kTabs, value);
        }
        break;
      }
      case PaymentsDataTypeItemType:
      case AutofillDataTypeItemType:
      case BookmarksDataTypeItemType:
      case OpenTabsDataTypeItemType:
      case PasswordsDataTypeItemType:
      case ReadingListDataTypeItemType:
      case SettingsDataTypeItemType: {
        // Don't try to toggle if item is managed.
        DCHECK(syncSwitchItem);
        syncer::UserSelectableType dataType =
            static_cast<syncer::UserSelectableType>(syncSwitchItem.dataType);
        if ([self isManagedSyncSettingsDataType:dataType]) {
          break;
        }

        _syncService->GetUserSettings()->SetSelectedType(dataType, value);
        break;
      }
      case ManageGoogleAccountItemType:
      case ManageAccountsItemType:
      case SwitchAccountItemType:
      case SignOutItemType:
      case EncryptionItemType:
      case GoogleActivityControlsItemType:
      case DataFromChromeSync:
      case PersonalizeGoogleServicesItemType:
      case PrimaryAccountReauthErrorItemType:
      case ShowPassphraseDialogErrorItemType:
      case SyncNeedsTrustedVaultKeyErrorItemType:
      case SyncTrustedVaultRecoverabilityDegradedErrorItemType:
      case SyncDisabledByAdministratorErrorItemType:
      case SignOutItemFooterType:
      case TypesListHeaderOrFooterType:
      case AccountErrorMessageItemType:
      case BatchUploadButtonItemType:
      case BatchUploadRecommendationItemType:
      case ManageAccountStorageType:
        NOTREACHED();
    }
  }
  [self updateSyncItemsNotifyConsumer:YES];
  // Switching toggles might affect the batch upload recommendation.
  [self fetchLocalDataDescriptionsForBatchUploadWithFirstLoad:NO];
}

- (void)didSelectItem:(TableViewItem*)item cellRect:(CGRect)cellRect {
  SyncSettingsItemType itemType = static_cast<SyncSettingsItemType>(item.type);
  switch (itemType) {
    case EncryptionItemType: {
      const syncer::SyncService::UserActionableError error =
          _syncService->GetUserActionableError();
      if (error == syncer::SyncService::UserActionableError::
                       kNeedsTrustedVaultKeyForPasswords ||
          error == syncer::SyncService::UserActionableError::
                       kNeedsTrustedVaultKeyForEverything) {
        [self.syncErrorHandler openTrustedVaultReauthForFetchKeys];
        break;
      }
      [self.syncErrorHandler openPassphraseDialogWithModalPresentation:NO];
      break;
    }
    case GoogleActivityControlsItemType:
      [self.commandHandler openWebAppActivityDialog];
      break;
    case DataFromChromeSync:
      [self.commandHandler openDataFromChromeSyncWebPage];
      break;
    case PersonalizeGoogleServicesItemType:
      if (self.isEEAAccount) {
        [self.commandHandler openPersonalizeGoogleServices];
      } else {
        [self.commandHandler openWebAppActivityDialog];
      }
      break;
    case PrimaryAccountReauthErrorItemType: {
      id<SystemIdentity> identity = _authenticationService->GetPrimaryIdentity(
          signin::ConsentLevel::kSignin);
      if (_authenticationService->HasCachedMDMErrorForIdentity(identity)) {
        [self.syncErrorHandler openMDMErrodDialogWithSystemIdentity:identity];
      } else {
        [self.syncErrorHandler openPrimaryAccountReauthDialog];
      }
      break;
    }
    case ShowPassphraseDialogErrorItemType:
      [self.syncErrorHandler openPassphraseDialogWithModalPresentation:YES];
      break;
    case SyncNeedsTrustedVaultKeyErrorItemType:
      [self.syncErrorHandler openTrustedVaultReauthForFetchKeys];
      break;
    case SyncTrustedVaultRecoverabilityDegradedErrorItemType:
      [self.syncErrorHandler openTrustedVaultReauthForDegradedRecoverability];
      break;
    case SignOutItemType:
      [self.commandHandler signOutFromTargetRect:cellRect];
      break;
    case ManageGoogleAccountItemType:
      [self.commandHandler showManageYourGoogleAccount];
      break;
    case ManageAccountsItemType:
      [self.commandHandler showAccountsPage];
      break;
    case SwitchAccountItemType:
      [self.commandHandler openAccountMenu];
      break;
    case BatchUploadButtonItemType:
      [self.commandHandler openBulkUpload];
      break;
    case ManageAccountStorageType:
      [self.commandHandler openAccountStorage];
      break;
    case AutofillDataTypeItemType:
    case BookmarksDataTypeItemType:
    case HistoryDataTypeItemType:
    case OpenTabsDataTypeItemType:
    case PasswordsDataTypeItemType:
    case ReadingListDataTypeItemType:
    case SettingsDataTypeItemType:
    case PaymentsDataTypeItemType:
    case SyncDisabledByAdministratorErrorItemType:
    case SignOutItemFooterType:
    case TypesListHeaderOrFooterType:
    case AccountErrorMessageItemType:
    case BatchUploadRecommendationItemType:
      // Nothing to do.
      break;
  }
}

// Creates a message item to display the sync error description for signed in
// not syncing users.
- (TableViewItem*)createSyncErrorMessageItem:(int)messageID {
  CHECK(self.accountStateSignedIn);
  SettingsImageDetailTextItem* syncErrorItem =
      [[SettingsImageDetailTextItem alloc]
          initWithType:AccountErrorMessageItemType];
  syncErrorItem.detailText = l10n_util::GetNSString(messageID);
  syncErrorItem.image =
      DefaultSymbolWithPointSize(kErrorCircleFillSymbol, kErrorSymbolPointSize);
  syncErrorItem.imageViewTintColor = [UIColor colorNamed:kRed500Color];
  syncErrorItem.accessibilityElementsHidden = YES;
  return syncErrorItem;
}

// Creates an error action button item to handle the indicated sync error type
// for signed in users.
- (TableViewItem*)createSyncErrorButtonItemWithItemType:(NSInteger)itemType
                                          buttonLabelID:(int)buttonLabelID
                                              messageID:(int)messageID {
  CHECK((itemType == PrimaryAccountReauthErrorItemType) ||
        (itemType == ShowPassphraseDialogErrorItemType) ||
        (itemType == SyncNeedsTrustedVaultKeyErrorItemType) ||
        (itemType == SyncTrustedVaultRecoverabilityDegradedErrorItemType))
      << "itemType: " << itemType;
  CHECK(self.accountStateSignedIn);
  TableViewTextItem* item = [[TableViewTextItem alloc] initWithType:itemType];
  item.text = l10n_util::GetNSString(buttonLabelID);
  item.accessibilityLabel = l10n_util::GetNSString(messageID);
  item.textColor = [UIColor colorNamed:kBlueColor];
  item.accessibilityTraits = UIAccessibilityTraitButton;
  item.accessibilityIdentifier = kSyncErrorButtonIdentifier;
  return item;
}

// Deletes the error section. If `notifyConsumer` is YES, the consumer is
// notified about model changes.
- (void)removeSyncErrorsSection:(BOOL)notifyConsumer {
  TableViewModel* model = self.consumer.tableViewModel;
  if (![model hasSectionForSectionIdentifier:SyncErrorsSectionIdentifier]) {
    return;
  }
  NSInteger sectionIndex =
      [model sectionForSectionIdentifier:SyncErrorsSectionIdentifier];
  [model removeSectionWithIdentifier:SyncErrorsSectionIdentifier];
  self.syncErrorItem = nil;

  // Remove the sync error section from the table view model.
  if (notifyConsumer) {
    NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:sectionIndex];
    [self.consumer deleteSections:indexSet rowAnimation:NO];
  }
}

// Loads the sync errors section.
- (void)loadSyncErrorsSection {
  // The `self.consumer.tableViewModel` will be reset prior to this method.
  // Ignore any previous value the `self.syncErrorItem` may have contained.
  self.syncErrorItem = nil;
  [self updateSyncErrorsSection:NO];
}

// Updates the sync errors section. If `notifyConsumer` is YES, the consumer is
// notified about model changes.
- (void)updateSyncErrorsSection:(BOOL)notifyConsumer {
  if (!self.accountStateSignedIn) {
    return;
  }
  // Checks if the sync setup service state has changed from the saved state in
  // the table view model.
  std::optional<SyncSettingsItemType> type = [self syncErrorItemType];
  if (![self needsErrorSectionUpdate:type]) {
    return;
  }

  TableViewModel* model = self.consumer.tableViewModel;
  // There is no sync error now, but there previously was an error.
  if (!type.has_value()) {
    [self removeSyncErrorsSection:notifyConsumer];
    return;
  }

  // There is an error now and there might be a previous error.
  BOOL errorSectionAlreadyExists = self.syncErrorItem;

  if (errorSectionAlreadyExists) {
    // As the previous error might not have a message item in case it is
    // SyncDisabledByAdministratorError, clear the whole section instead of
    // updating it's items.
    errorSectionAlreadyExists = NO;
    [self removeSyncErrorsSection:notifyConsumer];
  }

  if (GetAccountErrorUIInfo(_syncService) == nil) {
    // In some transient states like in SyncService::TransportState::PAUSED,
    // GetAccountErrorUIInfo returns nil and thus will not be able to fetch the
    // current error data. In this case, do not update/add the error item.
    return;
  }

  // Create the new sync error item.
  DCHECK(type.has_value());
  if (type.value() == SyncDisabledByAdministratorErrorItemType) {
    self.syncErrorItem = [self createSyncDisabledByAdministratorErrorItem];
  } else {
    // For signed in users, the sync error item will be displayed as a button.
    self.syncErrorItem =
        [self createSyncErrorButtonItemWithItemType:type.value()
                                      buttonLabelID:GetAccountErrorUIInfo(
                                                        _syncService)
                                                        .buttonLabelID
                                          messageID:GetAccountErrorUIInfo(
                                                        _syncService)
                                                        .messageID];
  }

  NSInteger syncErrorSectionIndex = 0;
  if (!errorSectionAlreadyExists) {
    if (type.value() != SyncDisabledByAdministratorErrorItemType) {
      [model insertSectionWithIdentifier:SyncErrorsSectionIdentifier
                                 atIndex:syncErrorSectionIndex];
      // For signed in users, the sync error item will be preceded by a
      // descriptive message item.
      [model addItem:[self createSyncErrorMessageItem:GetAccountErrorUIInfo(
                                                          _syncService)
                                                          .messageID]
          toSectionWithIdentifier:SyncErrorsSectionIdentifier];
      [model addItem:self.syncErrorItem
          toSectionWithIdentifier:SyncErrorsSectionIdentifier];
    }
  }

  if (notifyConsumer) {
    NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:syncErrorSectionIndex];
    if (errorSectionAlreadyExists) {
      [self.consumer reloadSections:indexSet];
    } else {
      [self.consumer insertSections:indexSet rowAnimation:NO];
    }
  }
}

// Returns the sync error item type or std::nullopt if the item
// is not an actionable error.
- (std::optional<SyncSettingsItemType>)syncErrorItemType {
  if (self.isSyncDisabledByAdministrator) {
    return SyncDisabledByAdministratorErrorItemType;
  }
  switch (_syncService->GetUserActionableError()) {
    case syncer::SyncService::UserActionableError::kSignInNeedsUpdate:
      return PrimaryAccountReauthErrorItemType;
    case syncer::SyncService::UserActionableError::kNeedsPassphrase:
      return ShowPassphraseDialogErrorItemType;
    case syncer::SyncService::UserActionableError::
        kNeedsTrustedVaultKeyForPasswords:
    case syncer::SyncService::UserActionableError::
        kNeedsTrustedVaultKeyForEverything:
      return SyncNeedsTrustedVaultKeyErrorItemType;
    case syncer::SyncService::UserActionableError::
        kTrustedVaultRecoverabilityDegradedForPasswords:
    case syncer::SyncService::UserActionableError::
        kTrustedVaultRecoverabilityDegradedForEverything:
      return SyncTrustedVaultRecoverabilityDegradedErrorItemType;
    case syncer::SyncService::UserActionableError::kNone:
      return std::nullopt;
  }
  NOTREACHED();
}

// Returns whether the error state has changed since the last update.
- (BOOL)needsErrorSectionUpdate:(std::optional<SyncSettingsItemType>)type {
  BOOL hasError = type.has_value();
  return (hasError && !self.syncErrorItem) ||
         (!hasError && self.syncErrorItem) ||
         (hasError && self.syncErrorItem &&
          type.value() != self.syncErrorItem.type);
}

// Returns an item to show to the user the sync cannot be turned on for an
// enterprise policy reason.
- (TableViewItem*)createSyncDisabledByAdministratorErrorItem {
  TableViewImageItem* item = [[TableViewImageItem alloc]
      initWithType:SyncDisabledByAdministratorErrorItemType];
  item.image = SymbolWithPalette(CustomSettingsRootSymbol(kEnterpriseSymbol),
                                 @[ [UIColor colorNamed:kStaticGrey600Color] ]);
  item.title = GetNSString(
      IDS_IOS_GOOGLE_SERVICES_SETTINGS_SYNC_DISABLBED_BY_ADMINISTRATOR_TITLE);
  item.enabled = NO;
  item.textColor = [UIColor colorNamed:kTextSecondaryColor];
  return item;
}

// Returns YES if the given type is managed by policies (i.e. is not syncable)
- (BOOL)isManagedSyncSettingsDataType:(syncer::UserSelectableType)type {
  return _syncService->GetUserSettings()->IsTypeManagedByPolicy(type) ||
         (self.accountStateSignedIn && self.isSyncDisabledByAdministrator);
}

#pragma mark - Properties

- (BOOL)isSyncDisabledByAdministrator {
  return _syncService->HasDisableReason(
      syncer::SyncService::DISABLE_REASON_ENTERPRISE_POLICY);
}


@end
