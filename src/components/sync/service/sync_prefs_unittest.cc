// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/service/sync_prefs.h"

#include <memory>
#include <vector>

#include "base/base64.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_value_map.h"
#include "components/prefs/testing_pref_service.h"
#include "components/saved_tab_groups/public/pref_names.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/signin_prefs.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/sync/base/features.h"
#include "components/sync/base/pref_names.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/protocol/nigori_specifics.pb.h"
#include "components/sync/service/glue/sync_transport_data_prefs.h"
#include "google_apis/gaia/gaia_id.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using ::testing::_;
using ::testing::AtMost;
using ::testing::ContainerEq;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::StrictMock;

// Copy of the same constant in sync_prefs.cc, for testing purposes.
constexpr char kObsoleteAutofillWalletImportEnabled[] =
    "autofill.wallet_import_enabled";

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
constexpr GaiaId::Literal kGaiaId("gaia-id");
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
class SyncPrefsTest : public testing::Test {
 protected:
  SyncPrefsTest() {
    SyncPrefs::RegisterProfilePrefs(pref_service_.registry());
    signin::IdentityManager::RegisterProfilePrefs(pref_service_.registry());
    SigninPrefs::RegisterProfilePrefs(pref_service_.registry());
    // TODO(crbug.com/368409110): These prefs are required due to a workaround
    // in KeepAccountSettingsPrefsOnlyForUsers(); see TODOs there.
    SyncTransportDataPrefs::RegisterProfilePrefs(pref_service_.registry());
    pref_service_.registry()->RegisterDictionaryPref(
        tab_groups::prefs::kLocallyClosedRemoteTabGroupIds,
        base::Value::Dict());

    sync_prefs_ = std::make_unique<SyncPrefs>(&pref_service_);
    gaia_id_ = GaiaId("account_gaia");

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_CHROMEOS)
    pref_service_.SetBoolean(::prefs::kExplicitBrowserSignin, true);
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) &&
        // !BUILDFLAG(IS_CHROMEOS)
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  TestingPrefServiceSimple pref_service_;
  std::unique_ptr<SyncPrefs> sync_prefs_;
  GaiaId gaia_id_;
};

TEST_F(SyncPrefsTest, EncryptionBootstrapTokenPerAccountSignedOut) {
  EXPECT_TRUE(
      sync_prefs_->GetEncryptionBootstrapTokenForAccount(GaiaId()).empty());
}

TEST_F(SyncPrefsTest, EncryptionBootstrapTokenPerAccount) {
  ASSERT_TRUE(
      sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_).empty());
  sync_prefs_->SetEncryptionBootstrapTokenForAccount("token", gaia_id_);
  EXPECT_EQ("token",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_));
  GaiaId gaia_id_2("account_gaia_2");
  EXPECT_TRUE(
      sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_2).empty());
  sync_prefs_->SetEncryptionBootstrapTokenForAccount("token2", gaia_id_2);
  EXPECT_EQ("token",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_));
  EXPECT_EQ("token2",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_2));
}

TEST_F(SyncPrefsTest, ClearEncryptionBootstrapTokenPerAccount) {
  ASSERT_TRUE(
      sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_).empty());
  sync_prefs_->SetEncryptionBootstrapTokenForAccount("token", gaia_id_);
  EXPECT_EQ("token",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_));
  GaiaId gaia_id_2("account_gaia_2");
  EXPECT_TRUE(
      sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_2).empty());
  sync_prefs_->SetEncryptionBootstrapTokenForAccount("token2", gaia_id_2);
  EXPECT_EQ("token",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_));
  EXPECT_EQ("token2",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_2));
  // Remove account 2 from device by setting the available_gaia_ids to have the
  // gaia id of account 1 only.
  sync_prefs_->KeepAccountSettingsPrefsOnlyForUsers(
      /*available_gaia_ids=*/{gaia_id_});
  EXPECT_EQ("token",
            sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_));
  EXPECT_TRUE(
      sync_prefs_->GetEncryptionBootstrapTokenForAccount(gaia_id_2).empty());
}

TEST_F(SyncPrefsTest, CachedPassphraseType) {
  EXPECT_FALSE(sync_prefs_->GetCachedPassphraseType().has_value());

  sync_prefs_->SetCachedPassphraseType(PassphraseType::kKeystorePassphrase);
  EXPECT_EQ(PassphraseType::kKeystorePassphrase,
            sync_prefs_->GetCachedPassphraseType());

  sync_prefs_->SetCachedPassphraseType(PassphraseType::kCustomPassphrase);
  EXPECT_EQ(PassphraseType::kCustomPassphrase,
            sync_prefs_->GetCachedPassphraseType());

  sync_prefs_->ClearCachedPassphraseType();
  EXPECT_FALSE(sync_prefs_->GetCachedPassphraseType().has_value());
}

TEST_F(SyncPrefsTest, CachedTrustedVaultAutoUpgradeExperimentGroup) {
  const int kTestCohort = 123;
  const sync_pb::TrustedVaultAutoUpgradeExperimentGroup::Type kTestType =
      sync_pb::TrustedVaultAutoUpgradeExperimentGroup::VALIDATION;
  const int kTestTypeIndex = 5;

  EXPECT_FALSE(sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                   .has_value());

  {
    sync_pb::TrustedVaultAutoUpgradeExperimentGroup proto;
    proto.set_cohort(kTestCohort);
    proto.set_type(kTestType);
    proto.set_type_index(kTestTypeIndex);
    sync_prefs_->SetCachedTrustedVaultAutoUpgradeExperimentGroup(proto);
  }

  EXPECT_TRUE(sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                  .has_value());

  const sync_pb::TrustedVaultAutoUpgradeExperimentGroup group_from_prefs =
      sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup().value_or(
          sync_pb::TrustedVaultAutoUpgradeExperimentGroup());

  EXPECT_EQ(kTestCohort, group_from_prefs.cohort());
  EXPECT_EQ(kTestType, group_from_prefs.type());
  EXPECT_EQ(kTestTypeIndex, group_from_prefs.type_index());

  sync_prefs_->ClearCachedTrustedVaultAutoUpgradeExperimentGroup();
  EXPECT_FALSE(sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                   .has_value());
}

TEST_F(SyncPrefsTest, CachedTrustedVaultAutoUpgradeExperimentGroupCorrupt) {
  // Populate with a corrupt, non-base64 value.
  pref_service_.SetString(
      prefs::internal::kSyncCachedTrustedVaultAutoUpgradeExperimentGroup,
      "corrupt");
  EXPECT_TRUE(sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                  .has_value());
  EXPECT_EQ(0, sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                   .value_or(sync_pb::TrustedVaultAutoUpgradeExperimentGroup())
                   .cohort());
  EXPECT_EQ(sync_pb::TrustedVaultAutoUpgradeExperimentGroup::TYPE_UNSPECIFIED,
            sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                .value_or(sync_pb::TrustedVaultAutoUpgradeExperimentGroup())
                .type());
  EXPECT_EQ(0, sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                   .value_or(sync_pb::TrustedVaultAutoUpgradeExperimentGroup())
                   .type_index());

  // Populate with a corrupt, unparsable value after base64-decoding.
  pref_service_.SetString(
      prefs::internal::kSyncCachedTrustedVaultAutoUpgradeExperimentGroup,
      base::Base64Encode("corrupt"));
  EXPECT_TRUE(sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                  .has_value());
  EXPECT_EQ(0, sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                   .value_or(sync_pb::TrustedVaultAutoUpgradeExperimentGroup())
                   .cohort());
  EXPECT_EQ(sync_pb::TrustedVaultAutoUpgradeExperimentGroup::TYPE_UNSPECIFIED,
            sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                .value_or(sync_pb::TrustedVaultAutoUpgradeExperimentGroup())
                .type());
  EXPECT_EQ(0, sync_prefs_->GetCachedTrustedVaultAutoUpgradeExperimentGroup()
                   .value_or(sync_pb::TrustedVaultAutoUpgradeExperimentGroup())
                   .type_index());
}

class MockSyncPrefObserver : public SyncPrefObserver {
 public:
  MOCK_METHOD(void, OnSyncManagedPrefChange, (bool), (override));
  MOCK_METHOD(void, OnSelectedTypesPrefChange, (), (override));
};

TEST_F(SyncPrefsTest, ObservedPrefs) {
  StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
  InSequence in_sequence;
  EXPECT_CALL(mock_sync_pref_observer, OnSyncManagedPrefChange(true));
  EXPECT_CALL(mock_sync_pref_observer, OnSyncManagedPrefChange(false));

  ASSERT_FALSE(sync_prefs_->IsSyncClientDisabledByPolicy());

  sync_prefs_->AddObserver(&mock_sync_pref_observer);

  pref_service_.SetBoolean(prefs::internal::kSyncManaged, true);
  EXPECT_TRUE(sync_prefs_->IsSyncClientDisabledByPolicy());
  pref_service_.SetBoolean(prefs::internal::kSyncManaged, false);
  EXPECT_FALSE(sync_prefs_->IsSyncClientDisabledByPolicy());

  sync_prefs_->RemoveObserver(&mock_sync_pref_observer);
}

#if !BUILDFLAG(IS_CHROMEOS)
TEST_F(SyncPrefsTest, FirstSetupCompletePrefChange) {
  ASSERT_FALSE(sync_prefs_->IsInitialSyncFeatureSetupComplete());

  sync_prefs_->SetInitialSyncFeatureSetupComplete();
  EXPECT_TRUE(sync_prefs_->IsInitialSyncFeatureSetupComplete());

  sync_prefs_->ClearInitialSyncFeatureSetupComplete();
  EXPECT_FALSE(sync_prefs_->IsInitialSyncFeatureSetupComplete());
}
#endif  // !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS)
TEST_F(SyncPrefsTest, SyncFeatureDisabledViaDashboard) {
  EXPECT_FALSE(sync_prefs_->IsSyncFeatureDisabledViaDashboard());

  sync_prefs_->SetSyncFeatureDisabledViaDashboard();
  EXPECT_TRUE(sync_prefs_->IsSyncFeatureDisabledViaDashboard());

  sync_prefs_->ClearSyncFeatureDisabledViaDashboard();
  EXPECT_FALSE(sync_prefs_->IsSyncFeatureDisabledViaDashboard());
}

TEST_F(SyncPrefsTest, SetSelectedOsTypesTriggersPreferredDataTypesPrefChange) {
  StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
  EXPECT_CALL(mock_sync_pref_observer, OnSelectedTypesPrefChange);

  sync_prefs_->AddObserver(&mock_sync_pref_observer);
  sync_prefs_->SetSelectedOsTypes(/*sync_all_os_types=*/false,
                                  UserSelectableOsTypeSet(),
                                  UserSelectableOsTypeSet());
  sync_prefs_->RemoveObserver(&mock_sync_pref_observer);
}
#endif  // BUILDFLAG(IS_CHROMEOS)

TEST_F(SyncPrefsTest, Basic) {
#if !BUILDFLAG(IS_CHROMEOS)
  EXPECT_FALSE(sync_prefs_->IsInitialSyncFeatureSetupComplete());
  sync_prefs_->SetInitialSyncFeatureSetupComplete();
#endif  // !BUILDFLAG(IS_CHROMEOS)

  EXPECT_TRUE(sync_prefs_->IsInitialSyncFeatureSetupComplete());

  EXPECT_TRUE(sync_prefs_->HasKeepEverythingSynced());
  sync_prefs_->SetSelectedTypesForSyncingUser(
      /*keep_everything_synced=*/false,
      /*registered_types=*/UserSelectableTypeSet::All(),
      /*selected_types=*/UserSelectableTypeSet::All());
  EXPECT_FALSE(sync_prefs_->HasKeepEverythingSynced());
  sync_prefs_->SetSelectedTypesForSyncingUser(
      /*keep_everything_synced=*/true,
      /*registered_types=*/UserSelectableTypeSet::All(),
      /*selected_types=*/UserSelectableTypeSet());
  EXPECT_TRUE(sync_prefs_->HasKeepEverythingSynced());
}

TEST_F(SyncPrefsTest, SelectedTypesKeepEverythingSynced) {
  ASSERT_TRUE(sync_prefs_->HasKeepEverythingSynced());

  EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
              ContainerEq(UserSelectableTypeSet::All()));
  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
    sync_prefs_->AddObserver(&mock_sync_pref_observer);

    // SetSelectedTypesForSyncingUser() should result in at most one observer
    // notification: Never more than one, and in this case, since nothing
    // actually changes, zero calls would also be okay.
    EXPECT_CALL(mock_sync_pref_observer, OnSelectedTypesPrefChange)
        .Times(AtMost(1));

    sync_prefs_->SetSelectedTypesForSyncingUser(
        /*keep_everything_synced=*/true,
        /*registered_types=*/UserSelectableTypeSet::All(),
        /*selected_types=*/{type});
    EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
                ContainerEq(UserSelectableTypeSet::All()));

    sync_prefs_->RemoveObserver(&mock_sync_pref_observer);
  }
}

TEST_F(SyncPrefsTest, SelectedTypesKeepEverythingSyncedButPolicyRestricted) {
  ASSERT_TRUE(sync_prefs_->HasKeepEverythingSynced());

  StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
  sync_prefs_->AddObserver(&mock_sync_pref_observer);

  // Setting a managed pref value should trigger an
  // OnSelectedTypesPrefChange() notification.
  EXPECT_CALL(mock_sync_pref_observer, OnSelectedTypesPrefChange);
  pref_service_.SetManagedPref(prefs::internal::kSyncPreferences,
                               base::Value(false));

  sync_prefs_->RemoveObserver(&mock_sync_pref_observer);

  UserSelectableTypeSet expected_type_set = UserSelectableTypeSet::All();
  expected_type_set.Remove(UserSelectableType::kPreferences);
  EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
              ContainerEq(expected_type_set));
}

TEST_F(SyncPrefsTest, SelectedTypesNotKeepEverythingSynced) {
  sync_prefs_->SetSelectedTypesForSyncingUser(
      /*keep_everything_synced=*/false,
      /*registered_types=*/UserSelectableTypeSet::All(),
      /*selected_types=*/UserSelectableTypeSet());

  ASSERT_NE(UserSelectableTypeSet::All(),
            sync_prefs_->GetSelectedTypesForSyncingUser());
  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
    sync_prefs_->AddObserver(&mock_sync_pref_observer);

    // SetSelectedTypesForSyncingUser() should result in exactly one call to
    // OnSelectedTypesPrefChange(), even when multiple data types change
    // state (here, usually one gets enabled and one gets disabled).
    EXPECT_CALL(mock_sync_pref_observer, OnSelectedTypesPrefChange);

    sync_prefs_->SetSelectedTypesForSyncingUser(
        /*keep_everything_synced=*/false,
        /*registered_types=*/UserSelectableTypeSet::All(),
        /*selected_types=*/{type});
    EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
                ContainerEq(UserSelectableTypeSet({type})));

    sync_prefs_->RemoveObserver(&mock_sync_pref_observer);
  }
}

TEST_F(SyncPrefsTest, SelectedTypesNotKeepEverythingSyncedAndPolicyRestricted) {
  pref_service_.SetManagedPref(prefs::internal::kSyncPreferences,
                               base::Value(false));
  sync_prefs_->SetSelectedTypesForSyncingUser(
      /*keep_everything_synced=*/false,
      /*registered_types=*/UserSelectableTypeSet::All(),
      /*selected_types=*/UserSelectableTypeSet());

  ASSERT_FALSE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kPreferences));
  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    sync_prefs_->SetSelectedTypesForSyncingUser(
        /*keep_everything_synced=*/false,
        /*registered_types=*/UserSelectableTypeSet::All(),
        /*selected_types=*/{type});
    UserSelectableTypeSet expected_type_set = {type};
    expected_type_set.Remove(UserSelectableType::kPreferences);
    EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
                ContainerEq(expected_type_set));
  }
}

TEST_F(SyncPrefsTest, SetTypeDisabledByPolicy) {
  // By default, data types are enabled, and not policy-controlled.
  ASSERT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kBookmarks));
  ASSERT_FALSE(
      sync_prefs_->IsTypeManagedByPolicy(UserSelectableType::kBookmarks));
  ASSERT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kAutofill));
  ASSERT_FALSE(
      sync_prefs_->IsTypeManagedByPolicy(UserSelectableType::kAutofill));

  // Set up a policy to disable bookmarks.
  PrefValueMap policy_prefs;
  SyncPrefs::SetTypeDisabledByPolicy(&policy_prefs,
                                     UserSelectableType::kBookmarks);
  // Copy the policy prefs map over into the PrefService.
  for (const auto& policy_pref : policy_prefs) {
    pref_service_.SetManagedPref(policy_pref.first, policy_pref.second.Clone());
  }

  // The policy should take effect and disable bookmarks.
  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kBookmarks));
  EXPECT_TRUE(
      sync_prefs_->IsTypeManagedByPolicy(UserSelectableType::kBookmarks));
  EXPECT_FALSE(
      sync_prefs_->IsTypeManagedByCustodian(UserSelectableType::kBookmarks));
  // Other types should be unaffected.
  EXPECT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kAutofill));
  EXPECT_FALSE(
      sync_prefs_->IsTypeManagedByPolicy(UserSelectableType::kAutofill));
}

TEST_F(SyncPrefsTest, SetTypeDisabledByCustodian) {
  // By default, data types are enabled, and not custodian-controlled.
  ASSERT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kBookmarks));
  ASSERT_FALSE(
      sync_prefs_->IsTypeManagedByCustodian(UserSelectableType::kBookmarks));
  ASSERT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kAutofill));
  ASSERT_FALSE(
      sync_prefs_->IsTypeManagedByCustodian(UserSelectableType::kAutofill));

  // Set up a custodian enforcement to disable bookmarks.
  PrefValueMap supervised_user_prefs;
  SyncPrefs::SetTypeDisabledByCustodian(&supervised_user_prefs,
                                        UserSelectableType::kBookmarks);
  // Copy the supervised user prefs map over into the PrefService.
  for (const auto& supervised_user_pref : supervised_user_prefs) {
    pref_service_.SetSupervisedUserPref(supervised_user_pref.first,
                                        supervised_user_pref.second.Clone());
  }

  // The restriction should take effect and disable bookmarks.
  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kBookmarks));
  EXPECT_TRUE(
      sync_prefs_->IsTypeManagedByCustodian(UserSelectableType::kBookmarks));
  EXPECT_FALSE(
      sync_prefs_->IsTypeManagedByPolicy(UserSelectableType::kBookmarks));
  // Other types should be unaffected.
  EXPECT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kAutofill));
  EXPECT_FALSE(
      sync_prefs_->IsTypeManagedByCustodian(UserSelectableType::kAutofill));
}

// kReplaceSyncPromosWithSignInPromos has been enabled by default on mobile
// platforms for a long time, so the feature-disabled case is not worth testing.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
TEST_F(SyncPrefsTest,
       DefaultSelectedTypesForAccountInTransportMode_SyncToSigninDisabled) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatures(
      /*enabled_features=*/{switches::kSyncEnableBookmarksInTransportMode,
                            kReadingListEnableSyncTransportModeUponSignIn,
                            switches::kEnablePreferencesAccountStorage},
      /*disabled_features=*/{kReplaceSyncPromosWithSignInPromos});

  EXPECT_THAT(
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
      ContainerEq(UserSelectableTypeSet({UserSelectableType::kPasswords,
                                         UserSelectableType::kAutofill,
                                         UserSelectableType::kPayments})));
}
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

TEST_F(SyncPrefsTest,
       DefaultSelectedTypesForAccountInTransportMode_SyncToSigninEnabled) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatures(
      /*enabled_features=*/{switches::kSyncEnableBookmarksInTransportMode,
                            kReplaceSyncPromosWithSignInPromos,
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
                            kReadingListEnableSyncTransportModeUponSignIn,
                            kSeparateLocalAndAccountSearchEngines,
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
                            syncer::kSeparateLocalAndAccountThemes,
                            switches::kEnableExtensionsExplicitBrowserSignin,
                            switches::kEnablePreferencesAccountStorage},
      /*disabled_features=*/{});

  // All except history-guarded types should be enabled.
  UserSelectableTypeSet expected_types{
      UserSelectableType::kBookmarks,
      UserSelectableType::kProductComparison,
      UserSelectableType::kReadingList,
      UserSelectableType::kPasswords,
      UserSelectableType::kAutofill,
      UserSelectableType::kPayments,
      UserSelectableType::kPreferences,
      UserSelectableType::kExtensions,
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
      // kThemes is not supported on mobile.
      UserSelectableType::kThemes,
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
  };

  EXPECT_THAT(sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
              ContainerEq(expected_types));
}

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_CHROMEOS)
TEST_F(SyncPrefsTest, DefaultWithImplicitBrowserSignin_SyncToSigninDisabled) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatures(
      /*enabled_features=*/{switches::kSyncEnableBookmarksInTransportMode,
                            kReadingListEnableSyncTransportModeUponSignIn,
                            switches::kEnablePreferencesAccountStorage},
      /*disabled_features=*/{kReplaceSyncPromosWithSignInPromos});

  pref_service_.ClearPref(::prefs::kExplicitBrowserSignin);
  ASSERT_FALSE(sync_prefs_->IsExplicitBrowserSignin());

  UserSelectableTypeSet expected_types{UserSelectableType::kPayments};
  EXPECT_THAT(
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
      ContainerEq(UserSelectableTypeSet{UserSelectableType::kPayments}));
}

TEST_F(SyncPrefsTest, DefaultWithImplicitBrowserSignin_SyncToSigninEnabled) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatures(
      /*enabled_features=*/{switches::kSyncEnableBookmarksInTransportMode,
                            kReplaceSyncPromosWithSignInPromos,
                            kReadingListEnableSyncTransportModeUponSignIn,
                            kSeparateLocalAndAccountSearchEngines,
                            syncer::kSeparateLocalAndAccountThemes,
                            switches::kEnablePreferencesAccountStorage},
      /*disabled_features=*/{});

  pref_service_.ClearPref(::prefs::kExplicitBrowserSignin);
  ASSERT_FALSE(sync_prefs_->IsExplicitBrowserSignin());

  EXPECT_THAT(
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
      ContainerEq(UserSelectableTypeSet{UserSelectableType::kPayments}));
}

#endif

TEST_F(SyncPrefsTest, SetSelectedTypesForAccountInTransportMode) {
  const UserSelectableTypeSet default_selected_types =
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_);
  ASSERT_TRUE(default_selected_types.Has(UserSelectableType::kPayments));

  StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
  sync_prefs_->AddObserver(&mock_sync_pref_observer);

  // Change one of the default values, for example kPayments. This should
  // result in an observer notification.
  EXPECT_CALL(mock_sync_pref_observer, OnSelectedTypesPrefChange);
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kPayments, false,
                                         gaia_id_);

  sync_prefs_->RemoveObserver(&mock_sync_pref_observer);

  // kPayments should be disabled, other default values should be unaffected.
  EXPECT_THAT(sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
              ContainerEq(Difference(default_selected_types,
                                     {UserSelectableType::kPayments})));
  // Other accounts should be unnafected.
  EXPECT_THAT(sync_prefs_->GetSelectedTypesForAccount(GaiaId("account_gaia_2")),
              ContainerEq(default_selected_types));
}

TEST_F(SyncPrefsTest,
       SetSelectedTypesForAccountInTransportModeWithPolicyRestrictedType) {
  StrictMock<MockSyncPrefObserver> mock_sync_pref_observer;
  sync_prefs_->AddObserver(&mock_sync_pref_observer);

  // Passwords gets disabled by policy. This should result in an observer
  // notification.
  EXPECT_CALL(mock_sync_pref_observer, OnSelectedTypesPrefChange);
  pref_service_.SetManagedPref(prefs::internal::kSyncPasswords,
                               base::Value(false));

  sync_prefs_->RemoveObserver(&mock_sync_pref_observer);

  // kPasswords should be disabled.
  UserSelectableTypeSet selected_types =
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_);
  ASSERT_FALSE(selected_types.empty());
  EXPECT_FALSE(selected_types.Has(UserSelectableType::kPasswords));

  // User tries to enable kPasswords.
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kPasswords, true,
                                         gaia_id_);

  // kPasswords should still be disabled.
  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPasswords));
}

TEST_F(SyncPrefsTest, KeepAccountSettingsPrefsOnlyForUsers) {
  const UserSelectableTypeSet default_selected_types =
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_);

  auto gaia_id_2 = GaiaId("account_gaia_2");

  // Change one of the default values for example kPasswords for account 1.
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kPasswords, false,
                                         gaia_id_);
  // Change one of the default values for example kReadingList for account 2.
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kReadingList,
                                         false, gaia_id_2);
  ASSERT_EQ(
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
      Difference(default_selected_types, {UserSelectableType::kPasswords}));
  ASSERT_EQ(
      sync_prefs_->GetSelectedTypesForAccount(gaia_id_2),
      Difference(default_selected_types, {UserSelectableType::kReadingList}));

  // Remove account 2 from device by setting the available_gaia_ids to have the
  // gaia id of account 1 only.
  sync_prefs_->KeepAccountSettingsPrefsOnlyForUsers(
      /*available_gaia_ids=*/{gaia_id_});

  // Nothing should change on account 1.
  EXPECT_THAT(sync_prefs_->GetSelectedTypesForAccount(gaia_id_),
              ContainerEq(Difference(default_selected_types,
                                     {UserSelectableType::kPasswords})));
  // Account 2 should be cleared to default values.
  EXPECT_THAT(sync_prefs_->GetSelectedTypesForAccount(gaia_id_2),
              ContainerEq(default_selected_types));
}

#if BUILDFLAG(IS_CHROMEOS)
TEST_F(SyncPrefsTest, IsSyncAllOsTypesEnabled) {
  EXPECT_TRUE(sync_prefs_->IsSyncAllOsTypesEnabled());

  sync_prefs_->SetSelectedOsTypes(
      /*sync_all_os_types=*/false,
      /*registered_types=*/UserSelectableOsTypeSet::All(),
      /*selected_types=*/UserSelectableOsTypeSet::All());
  EXPECT_FALSE(sync_prefs_->IsSyncAllOsTypesEnabled());
  // Browser pref is not affected.
  EXPECT_TRUE(sync_prefs_->HasKeepEverythingSynced());

  sync_prefs_->SetSelectedOsTypes(
      /*sync_all_os_types=*/true,
      /*registered_types=*/UserSelectableOsTypeSet::All(),
      /*selected_types=*/UserSelectableOsTypeSet::All());
  EXPECT_TRUE(sync_prefs_->IsSyncAllOsTypesEnabled());
}

TEST_F(SyncPrefsTest, GetSelectedOsTypesWithAllOsTypesEnabled) {
  EXPECT_TRUE(sync_prefs_->IsSyncAllOsTypesEnabled());
  EXPECT_THAT(sync_prefs_->GetSelectedOsTypes(),
              ContainerEq(UserSelectableOsTypeSet::All()));
  for (UserSelectableOsType type : UserSelectableOsTypeSet::All()) {
    sync_prefs_->SetSelectedOsTypes(
        /*sync_all_os_types=*/true,
        /*registered_types=*/UserSelectableOsTypeSet::All(),
        /*selected_types=*/{type});
    EXPECT_EQ(UserSelectableOsTypeSet::All(),
              sync_prefs_->GetSelectedOsTypes());
  }
}

TEST_F(SyncPrefsTest, GetSelectedOsTypesNotAllOsTypesSelected) {
  const UserSelectableTypeSet browser_types =
      sync_prefs_->GetSelectedTypesForSyncingUser();

  sync_prefs_->SetSelectedOsTypes(
      /*sync_all_os_types=*/false,
      /*registered_types=*/UserSelectableOsTypeSet::All(),
      /*selected_types=*/UserSelectableOsTypeSet());
  EXPECT_THAT(sync_prefs_->GetSelectedOsTypes(), IsEmpty());
  // Browser types are not changed.
  EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
              ContainerEq(browser_types));

  for (UserSelectableOsType type : UserSelectableOsTypeSet::All()) {
    sync_prefs_->SetSelectedOsTypes(
        /*sync_all_os_types=*/false,
        /*registered_types=*/UserSelectableOsTypeSet::All(),
        /*selected_types=*/{type});
    EXPECT_THAT(sync_prefs_->GetSelectedOsTypes(),
                ContainerEq(UserSelectableOsTypeSet({type})));
    // Browser types are not changed.
    EXPECT_THAT(sync_prefs_->GetSelectedTypesForSyncingUser(),
                ContainerEq(browser_types));
  }
}

TEST_F(SyncPrefsTest, SelectedOsTypesKeepEverythingSyncedButPolicyRestricted) {
  ASSERT_TRUE(sync_prefs_->HasKeepEverythingSynced());
  pref_service_.SetManagedPref(prefs::internal::kSyncOsPreferences,
                               base::Value(false));

  UserSelectableOsTypeSet expected_type_set = UserSelectableOsTypeSet::All();
  expected_type_set.Remove(UserSelectableOsType::kOsPreferences);
  EXPECT_THAT(sync_prefs_->GetSelectedOsTypes(),
              ContainerEq(expected_type_set));
}

TEST_F(SyncPrefsTest,
       SelectedOsTypesNotKeepEverythingSyncedAndPolicyRestricted) {
  pref_service_.SetManagedPref(prefs::internal::kSyncOsPreferences,
                               base::Value(false));
  sync_prefs_->SetSelectedOsTypes(
      /*sync_all_os_types=*/false,
      /*registered_types=*/UserSelectableOsTypeSet::All(),
      /*selected_types=*/UserSelectableOsTypeSet());

  ASSERT_FALSE(sync_prefs_->GetSelectedOsTypes().Has(
      UserSelectableOsType::kOsPreferences));
  for (UserSelectableOsType type : UserSelectableOsTypeSet::All()) {
    sync_prefs_->SetSelectedOsTypes(
        /*sync_all_os_types=*/false,
        /*registered_types=*/UserSelectableOsTypeSet::All(),
        /*selected_types=*/{type});
    UserSelectableOsTypeSet expected_type_set = {type};
    expected_type_set.Remove(UserSelectableOsType::kOsPreferences);
    EXPECT_THAT(sync_prefs_->GetSelectedOsTypes(),
                ContainerEq(expected_type_set));
  }
}

TEST_F(SyncPrefsTest, SetOsTypeDisabledByPolicy) {
  // By default, data types are enabled, and not policy-controlled.
  ASSERT_TRUE(
      sync_prefs_->GetSelectedOsTypes().Has(UserSelectableOsType::kOsApps));
  ASSERT_FALSE(
      sync_prefs_->IsOsTypeManagedByPolicy(UserSelectableOsType::kOsApps));
  ASSERT_TRUE(sync_prefs_->GetSelectedOsTypes().Has(
      UserSelectableOsType::kOsPreferences));
  ASSERT_FALSE(sync_prefs_->IsOsTypeManagedByPolicy(
      UserSelectableOsType::kOsPreferences));

  // Set up a policy to disable apps.
  PrefValueMap policy_prefs;
  SyncPrefs::SetOsTypeDisabledByPolicy(&policy_prefs,
                                       UserSelectableOsType::kOsApps);
  // Copy the policy prefs map over into the PrefService.
  for (const auto& policy_pref : policy_prefs) {
    pref_service_.SetManagedPref(policy_pref.first, policy_pref.second.Clone());
  }

  // The policy should take effect and disable apps.
  EXPECT_FALSE(
      sync_prefs_->GetSelectedOsTypes().Has(UserSelectableOsType::kOsApps));
  EXPECT_TRUE(
      sync_prefs_->IsOsTypeManagedByPolicy(UserSelectableOsType::kOsApps));
  // Other types should be unaffected.
  EXPECT_TRUE(sync_prefs_->GetSelectedOsTypes().Has(
      UserSelectableOsType::kOsPreferences));
  EXPECT_FALSE(sync_prefs_->IsOsTypeManagedByPolicy(
      UserSelectableOsType::kOsPreferences));
}
#endif  // BUILDFLAG(IS_CHROMEOS)

TEST_F(SyncPrefsTest, PassphrasePromptMutedProductVersion) {
  EXPECT_EQ(0, sync_prefs_->GetPassphrasePromptMutedProductVersion());

  sync_prefs_->SetPassphrasePromptMutedProductVersion(83);
  EXPECT_EQ(83, sync_prefs_->GetPassphrasePromptMutedProductVersion());

  sync_prefs_->ClearPassphrasePromptMutedProductVersion();
  EXPECT_EQ(0, sync_prefs_->GetPassphrasePromptMutedProductVersion());
}

TEST_F(SyncPrefsTest, PasswordSyncAllowed_DefaultValue) {
  // Passwords is in its default state. For syncing users, it's enabled. For
  // non-syncing users, it depends on the platform.
  ASSERT_TRUE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kPasswords));
  StrictMock<MockSyncPrefObserver> observer;
  sync_prefs_->AddObserver(&observer);
  EXPECT_CALL(observer, OnSelectedTypesPrefChange);

  sync_prefs_->SetPasswordSyncAllowed(false);

  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kPasswords));
  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPasswords));
  sync_prefs_->RemoveObserver(&observer);
}

TEST_F(SyncPrefsTest, PasswordSyncAllowed_ExplicitValue) {
  // Make passwords explicitly enabled (no default value).
  sync_prefs_->SetSelectedTypesForSyncingUser(
      /*keep_everything_synced=*/false,
      /*registered_types=*/UserSelectableTypeSet::All(),
      /*selected_types=*/{UserSelectableType::kPasswords});
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kPasswords, true,
                                         gaia_id_);

  sync_prefs_->SetPasswordSyncAllowed(false);

  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForSyncingUser().Has(
      UserSelectableType::kPasswords));
  EXPECT_FALSE(sync_prefs_->GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPasswords));
}

enum BooleanPrefState { PREF_FALSE, PREF_TRUE, PREF_UNSET };

// Similar to SyncPrefsTest, but does not create a SyncPrefs instance. This lets
// individual tests set up the "before" state of the PrefService before
// SyncPrefs gets created.
class SyncPrefsMigrationTest : public testing::Test {
 protected:
  SyncPrefsMigrationTest() {
    // Enable various features that are required for data types to be supported
    // in transport mode.
    feature_list_.InitWithFeatures(
        /*enabled_features=*/
        {
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
            switches::kSyncEnableBookmarksInTransportMode,
            kReadingListEnableSyncTransportModeUponSignIn,
            kSeparateLocalAndAccountSearchEngines,
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
            switches::kEnablePreferencesAccountStorage},
        /*disabled_features=*/{});

    SyncPrefs::RegisterProfilePrefs(pref_service_.registry());
    SigninPrefs::RegisterProfilePrefs(pref_service_.registry());
    gaia_id_ = GaiaId("account_gaia");
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
    signin::IdentityManager::RegisterProfilePrefs(pref_service_.registry());
    pref_service_.SetBoolean(::prefs::kExplicitBrowserSignin, true);
    pref_service_.SetBoolean(
        ::prefs::kPrefsThemesSearchEnginesAccountStorageEnabled, true);
#endif
  }

  void SetBooleanUserPrefValue(const char* pref_name, BooleanPrefState state) {
    switch (state) {
      case PREF_FALSE:
        pref_service_.SetBoolean(pref_name, false);
        break;
      case PREF_TRUE:
        pref_service_.SetBoolean(pref_name, true);
        break;
      case PREF_UNSET:
        pref_service_.ClearPref(pref_name);
        break;
    }
  }

  BooleanPrefState GetBooleanUserPrefValue(const char* pref_name) const {
    const base::Value* pref_value = pref_service_.GetUserPrefValue(pref_name);
    if (!pref_value) {
      return PREF_UNSET;
    }
    return pref_value->GetBool() ? PREF_TRUE : PREF_FALSE;
  }

  // Global prefs for syncing users, affecting all accounts.
  const char* kGlobalBookmarksPref =
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kBookmarks);
  const char* kGlobalReadingListPref =
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kReadingList);
  const char* kGlobalPasswordsPref =
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPasswords);
  const char* kGlobalAutofillPref =
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kAutofill);
  const char* kGlobalPaymentsPref =
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPayments);
  const char* kGlobalPreferencesPref =
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPreferences);

  base::test::ScopedFeatureList feature_list_;
  base::test::SingleThreadTaskEnvironment task_environment_;

  TestingPrefServiceSimple pref_service_;
  GaiaId gaia_id_;
};

TEST_F(SyncPrefsMigrationTest, MigrateAutofillWalletImportEnabledPrefIfSet) {
  pref_service_.SetBoolean(kObsoleteAutofillWalletImportEnabled, false);
  ASSERT_TRUE(
      pref_service_.GetUserPrefValue(kObsoleteAutofillWalletImportEnabled));

  SyncPrefs::MigrateAutofillWalletImportEnabledPref(&pref_service_);

  SyncPrefs prefs(&pref_service_);

  EXPECT_TRUE(pref_service_.GetUserPrefValue(
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPayments)));
  EXPECT_FALSE(pref_service_.GetBoolean(
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPayments)));
}

TEST_F(SyncPrefsMigrationTest, MigrateAutofillWalletImportEnabledPrefIfUnset) {
  ASSERT_FALSE(
      pref_service_.GetUserPrefValue(kObsoleteAutofillWalletImportEnabled));

  SyncPrefs::MigrateAutofillWalletImportEnabledPref(&pref_service_);

  SyncPrefs prefs(&pref_service_);

  EXPECT_FALSE(pref_service_.GetUserPrefValue(
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPayments)));
}

// Regression test for crbug.com/1467307.
TEST_F(SyncPrefsMigrationTest,
       MigrateAutofillWalletImportEnabledPrefIfUnsetWithSyncEverythingOff) {
  // Mimic an old profile where sync-everything was turned off without
  // populating kObsoleteAutofillWalletImportEnabled (i.e. before the UI
  // included the payments toggle).
  pref_service_.SetBoolean(prefs::internal::kSyncKeepEverythingSynced, false);

  ASSERT_FALSE(
      pref_service_.GetUserPrefValue(kObsoleteAutofillWalletImportEnabled));

  SyncPrefs::MigrateAutofillWalletImportEnabledPref(&pref_service_);

  SyncPrefs prefs(&pref_service_);

  EXPECT_TRUE(pref_service_.GetUserPrefValue(
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPayments)));
  EXPECT_TRUE(pref_service_.GetBoolean(
      SyncPrefs::GetPrefNameForTypeForTesting(UserSelectableType::kPayments)));
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
TEST_F(SyncPrefsMigrationTest,
       DoNotMigratePasswordsToPerAccountPrefIfLastGaiaIdMissing) {
  ASSERT_EQ(pref_service_.GetString(::prefs::kGoogleServicesLastSyncingGaiaId),
            std::string());
  pref_service_.SetBoolean(prefs::internal::kSyncKeepEverythingSynced, false);
  ASSERT_FALSE(pref_service_.GetBoolean(kGlobalPasswordsPref));
  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));

  SyncPrefs::MaybeMigrateAutofillToPerAccountPref(&pref_service_);

  EXPECT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));
}

TEST_F(SyncPrefsMigrationTest,
       DoNotMigratePasswordsToPerAccountPrefIfSyncEverythingEnabled) {
  pref_service_.SetString(::prefs::kGoogleServicesLastSyncingGaiaId,
                          kGaiaId.ToString());
  ASSERT_TRUE(
      pref_service_.GetBoolean(prefs::internal::kSyncKeepEverythingSynced));
  ASSERT_FALSE(pref_service_.GetBoolean(kGlobalPasswordsPref));
  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));

  SyncPrefs::MaybeMigrateAutofillToPerAccountPref(&pref_service_);

  EXPECT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));
}

TEST_F(SyncPrefsMigrationTest,
       DoNotMigratePasswordsToPerAccountPrefIfPasswordsEnabled) {
  pref_service_.SetString(::prefs::kGoogleServicesLastSyncingGaiaId,
                          kGaiaId.ToString());
  pref_service_.SetBoolean(prefs::internal::kSyncKeepEverythingSynced, false);
  pref_service_.SetBoolean(kGlobalPasswordsPref, true);
  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));

  SyncPrefs::MaybeMigrateAutofillToPerAccountPref(&pref_service_);

  EXPECT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));
}

TEST_F(SyncPrefsMigrationTest, MigratePasswordsToPerAccountPrefRunsOnce) {
  pref_service_.SetString(::prefs::kGoogleServicesLastSyncingGaiaId,
                          kGaiaId.ToString());
  pref_service_.SetBoolean(prefs::internal::kSyncKeepEverythingSynced, false);
  ASSERT_FALSE(pref_service_.GetBoolean(kGlobalPasswordsPref));
  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));

  SyncPrefs::MaybeMigrateAutofillToPerAccountPref(&pref_service_);

  EXPECT_FALSE(SyncPrefs(&pref_service_)
                   .GetSelectedTypesForAccount(kGaiaId)
                   .Has(UserSelectableType::kPasswords));

  // Manually re-enable and attempt to run the migration again.
  SyncPrefs(&pref_service_)
      .SetSelectedTypeForAccount(UserSelectableType::kPasswords, true, kGaiaId);
  SyncPrefs::MaybeMigrateAutofillToPerAccountPref(&pref_service_);

  // This time the migration didn't run, because it was one-off.
  EXPECT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kPasswords));
}

TEST_F(SyncPrefsMigrationTest, MigrateAddressesToPerAccountPref) {
  pref_service_.SetString(::prefs::kGoogleServicesLastSyncingGaiaId,
                          kGaiaId.ToString());
  pref_service_.SetBoolean(prefs::internal::kSyncKeepEverythingSynced, false);
  ASSERT_FALSE(pref_service_.GetBoolean(kGlobalAutofillPref));
  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(kGaiaId)
                  .Has(UserSelectableType::kAutofill));

  SyncPrefs::MaybeMigrateAutofillToPerAccountPref(&pref_service_);

  EXPECT_FALSE(SyncPrefs(&pref_service_)
                   .GetSelectedTypesForAccount(kGaiaId)
                   .Has(UserSelectableType::kAutofill));
}
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

TEST_F(SyncPrefsMigrationTest, NoPassphraseMigrationForSignoutUsers) {
  SyncPrefs prefs(&pref_service_);
  // Passphrase is not set.
  ASSERT_TRUE(
      pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken)
          .empty());

  prefs.MaybeMigrateCustomPassphrasePref(GaiaId());
  EXPECT_TRUE(
      pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken)
          .empty());
  EXPECT_TRUE(prefs.GetEncryptionBootstrapTokenForAccount(GaiaId()).empty());
}

TEST_F(SyncPrefsMigrationTest, PassphraseMigrationDone) {
  SyncPrefs prefs(&pref_service_);
  pref_service_.SetString(prefs::internal::kSyncEncryptionBootstrapToken,
                          "token");
  prefs.MaybeMigrateCustomPassphrasePref(gaia_id_);
  EXPECT_EQ(
      pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken),
      "token");
  EXPECT_EQ(prefs.GetEncryptionBootstrapTokenForAccount(gaia_id_), "token");
  GaiaId gaia_id_2("account_gaia_2");
  EXPECT_TRUE(prefs.GetEncryptionBootstrapTokenForAccount(gaia_id_2).empty());
}

TEST_F(SyncPrefsMigrationTest, PassphraseMigrationOnlyOnce) {
  SyncPrefs prefs(&pref_service_);
  pref_service_.SetString(prefs::internal::kSyncEncryptionBootstrapToken,
                          "token");
  prefs.MaybeMigrateCustomPassphrasePref(gaia_id_);
  EXPECT_EQ(
      pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken),
      "token");
  EXPECT_EQ(prefs.GetEncryptionBootstrapTokenForAccount(gaia_id_), "token");

  // Force old pref to change for testing purposes.
  pref_service_.SetString(prefs::internal::kSyncEncryptionBootstrapToken,
                          "token2");
  prefs.MaybeMigrateCustomPassphrasePref(gaia_id_);
  // The migration should not run again.
  EXPECT_EQ(
      pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken),
      "token2");
  EXPECT_EQ(prefs.GetEncryptionBootstrapTokenForAccount(gaia_id_), "token");
}

TEST_F(SyncPrefsMigrationTest, PassphraseMigrationOnlyOnceWithBrowserRestart) {
  {
    SyncPrefs prefs(&pref_service_);
    pref_service_.SetString(prefs::internal::kSyncEncryptionBootstrapToken,
                            "token");
    prefs.MaybeMigrateCustomPassphrasePref(gaia_id_);
    EXPECT_EQ(
        pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken),
        "token");
    EXPECT_EQ(prefs.GetEncryptionBootstrapTokenForAccount(gaia_id_), "token");
    // Force old pref to change for testing purposes.
    pref_service_.SetString(prefs::internal::kSyncEncryptionBootstrapToken,
                            "token2");
  }

  // The browser is restarted.
  {
    SyncPrefs prefs(&pref_service_);
    prefs.MaybeMigrateCustomPassphrasePref(gaia_id_);
    // No migration should run.
    EXPECT_EQ(
        pref_service_.GetString(prefs::internal::kSyncEncryptionBootstrapToken),
        "token2");
    EXPECT_EQ(prefs.GetEncryptionBootstrapTokenForAccount(gaia_id_), "token");
  }
}

TEST_F(SyncPrefsMigrationTest, NoMigrationForSignedOutUser) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  EXPECT_FALSE(SyncPrefs(&pref_service_)
                   .MaybeMigratePrefsForSyncToSigninPart1(
                       SyncPrefs::SyncAccountState::kNotSignedIn, GaiaId()));
  // Part 2 isn't called because the engine isn't initialized.
}

TEST_F(SyncPrefsMigrationTest, NoMigrationForSyncingUser) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  SyncPrefs prefs(&pref_service_);
  EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
      SyncPrefs::SyncAccountState::kSyncing, gaia_id_));
  EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
      gaia_id_,
      /*is_using_explicit_passphrase=*/true));
}

TEST_F(SyncPrefsMigrationTest, RunsOnlyOnce) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  {
    SyncPrefs prefs(&pref_service_);

    // The user is signed-out, so the migration should not run and it should be
    // be marked as done. MaybeMigratePrefsForSyncToSigninPart2() isn't called
    // yet, because the sync engine wasn't initialized.
    ASSERT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kNotSignedIn, GaiaId()));

    // The user signs in, causing the engine to initialize and the call to part
    // 2. The migration should not run, because this wasn't an *existing*
    // signed-in user.
    EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
        gaia_id_,
        /*is_using_explicit_passphrase=*/true));
  }

  // The browser is restarted.
  {
    SyncPrefs prefs(&pref_service_);

    // Both methods are called. No migration should run.
    EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_));
    EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
        gaia_id_,
        /*is_using_explicit_passphrase=*/true));
  }
}

TEST_F(SyncPrefsMigrationTest, RunsAgainAfterFeatureReenabled) {
  // The feature gets enabled for the first time.
  {
    base::test::ScopedFeatureList enable_sync_to_signin(
        kReplaceSyncPromosWithSignInPromos);

    SyncPrefs prefs(&pref_service_);

    // The user is signed-in non-syncing, so part 1 runs. The user also has an
    // explicit passphrase, so part 2 runs too.
    EXPECT_TRUE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_));
    EXPECT_TRUE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
        gaia_id_,
        /*is_using_explicit_passphrase=*/true));
  }

  // On the next startup, the feature is disabled.
  {
    base::test::ScopedFeatureList disable_sync_to_signin;
    disable_sync_to_signin.InitAndDisableFeature(
        kReplaceSyncPromosWithSignInPromos);

    SyncPrefs prefs(&pref_service_);

    // Since the feature is disabled now, no migration runs.
    EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_));
    EXPECT_FALSE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
        gaia_id_,
        /*is_using_explicit_passphrase=*/true));
  }

  // On the next startup, the feature is enabled again.
  {
    base::test::ScopedFeatureList enable_sync_to_signin(
        kReplaceSyncPromosWithSignInPromos);

    SyncPrefs prefs(&pref_service_);

    // Since it was disabled in between, the migration should run again.
    EXPECT_TRUE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_));
    EXPECT_TRUE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
        gaia_id_,
        /*is_using_explicit_passphrase=*/true));
  }
}

TEST_F(SyncPrefsMigrationTest, GlobalPrefsAreUnchanged) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    ASSERT_EQ(
        GetBooleanUserPrefValue(SyncPrefs::GetPrefNameForTypeForTesting(type)),
        BooleanPrefState::PREF_UNSET);
  }

  SyncPrefs prefs(&pref_service_);

  ASSERT_TRUE(prefs.MaybeMigratePrefsForSyncToSigninPart1(
      SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_));
  ASSERT_TRUE(prefs.MaybeMigratePrefsForSyncToSigninPart2(
      gaia_id_,
      /*is_using_explicit_passphrase=*/true));

  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    EXPECT_EQ(
        GetBooleanUserPrefValue(SyncPrefs::GetPrefNameForTypeForTesting(type)),
        BooleanPrefState::PREF_UNSET);
  }
}

TEST_F(SyncPrefsMigrationTest, TurnsPreferencesOff) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  SyncPrefs prefs(&pref_service_);

  // Pre-migration, preferences is enabled by default.
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPreferences));

  // Run the migration for a pre-existing signed-in non-syncing user.
  prefs.MaybeMigratePrefsForSyncToSigninPart1(
      SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

  // Preferences should've been turned off in the account-scoped settings.
  EXPECT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPreferences));
}

TEST_F(SyncPrefsMigrationTest, MigratesBookmarksOptedIn) {
  {
    // The SyncToSignin feature starts disabled.
    base::test::ScopedFeatureList disable_sync_to_signin;
    disable_sync_to_signin.InitAndDisableFeature(
        kReplaceSyncPromosWithSignInPromos);

    // The user enables Bookmarks and Reading List. On non-mobile platforms set
    // a special opt-in pref for bookmarks.
    SyncPrefs prefs(&pref_service_);

#if !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_ANDROID)
    SigninPrefs(pref_service_)
        .SetBookmarksExplicitBrowserSignin(gaia_id_, true);
#endif

    prefs.SetSelectedTypeForAccount(UserSelectableType::kBookmarks, true,
                                    gaia_id_);
    prefs.SetSelectedTypeForAccount(UserSelectableType::kReadingList, true,
                                    gaia_id_);

    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kBookmarks));
    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kReadingList));
  }

  {
    // Now (on the next browser restart) the SyncToSignin feature gets enabled,
    // and the migration runs.
    base::test::ScopedFeatureList enable_sync_to_signin(
        kReplaceSyncPromosWithSignInPromos);

    SyncPrefs prefs(&pref_service_);

    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kBookmarks));
    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kReadingList));

    prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

    // Bookmarks and ReadingList should still be enabled.
    EXPECT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kBookmarks));
    EXPECT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kReadingList));
  }
}

TEST_F(SyncPrefsMigrationTest, MigratesBookmarksNotOptedIn) {
  {
    // The SyncToSignin feature starts disabled.
    base::test::ScopedFeatureList disable_sync_to_signin;
    disable_sync_to_signin.InitAndDisableFeature(
        kReplaceSyncPromosWithSignInPromos);

    SyncPrefs prefs(&pref_service_);

    // With the feature disabled, Bookmarks and ReadingList are disabled by
    // default.
    ASSERT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kBookmarks));
    ASSERT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kReadingList));
  }

  {
    // Now (on the next browser restart) the SyncToSignin feature gets enabled,
    // and the migration runs.
    base::test::ScopedFeatureList enable_sync_to_signin(
        kReplaceSyncPromosWithSignInPromos);

    SyncPrefs prefs(&pref_service_);

    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kBookmarks));
    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kReadingList));

    // Run the migration!
    prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

    // After the migration, the types should be disabled.
    EXPECT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kBookmarks));
    EXPECT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kReadingList));
  }
}

TEST_F(SyncPrefsMigrationTest, TurnsAutofillOffForCustomPassphraseUser) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  SyncPrefs prefs(&pref_service_);

  // Autofill is enabled (by default).
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kAutofill));

  // Run the first phase of the migration.
  prefs.MaybeMigratePrefsForSyncToSigninPart1(
      SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

  // Autofill should still be unaffected for now, since the passphrase state
  // wasn't known yet.
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kAutofill));

  // Now run the second phase, once the passphrase state is known (and it's
  // a custom passphrase).
  prefs.MaybeMigratePrefsForSyncToSigninPart2(
      gaia_id_,
      /*is_using_explicit_passphrase=*/true);

  // Now Autofill should've been turned off in the account-scoped settings.
  EXPECT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kAutofill));
}

TEST_F(SyncPrefsMigrationTest,
       LeavesAutofillAloneForUserWithoutExplicitPassphrase) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  SyncPrefs prefs(&pref_service_);

  // Autofill and payments are enabled (by default).
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kAutofill));
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPayments));

  // Run the first phase of the migration.
  prefs.MaybeMigratePrefsForSyncToSigninPart1(
      SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

  // The types should still be unaffected for now, since the passphrase state
  // wasn't known yet.
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kAutofill));
  ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPayments));

  // Now run the second phase, once the passphrase state is known (and it's a
  // regular keystore passphrase, i.e. no custom passphrase).
  prefs.MaybeMigratePrefsForSyncToSigninPart2(
      gaia_id_,
      /*is_using_explicit_passphrase=*/false);

  // Since this is not a custom passphrase user, the types should still be
  // unaffected.
  EXPECT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kAutofill));
  EXPECT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
      UserSelectableType::kPayments));
}

TEST_F(SyncPrefsMigrationTest, Part2RunsOnSecondAttempt) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  {
    SyncPrefs prefs(&pref_service_);

    // Autofill is enabled (by default).
    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kAutofill));

    // Run the first phase of the migration.
    prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

    // The account-scoped settings should still be unaffected for now, since the
    // passphrase state wasn't known yet.
    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kAutofill));
  }

  // Before the second phase runs, Chrome gets restarted.
  {
    SyncPrefs prefs(&pref_service_);

    // The first phase runs again. This should effectively do nothing.
    prefs.MaybeMigratePrefsForSyncToSigninPart1(
        SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_);

    ASSERT_TRUE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kAutofill));

    // Now run the second phase.
    prefs.MaybeMigratePrefsForSyncToSigninPart2(
        gaia_id_,
        /*is_using_explicit_passphrase=*/true);

    // Now the type should've been turned off in the account-scoped settings.
    EXPECT_FALSE(prefs.GetSelectedTypesForAccount(gaia_id_).Has(
        UserSelectableType::kAutofill));
  }
}

TEST_F(SyncPrefsMigrationTest, GlobalToAccount_DefaultState) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  // Everything is in the default state. Notably, "Sync Everything" is true.

  // Pre-migration (without any explicit per-account settings), most supported
  // types are considered selected by default - except for kHistory and kTabs.
  // Note that this is not exhaustive - depending on feature flags, additional
  // types may be supported and default-enabled.
  UserSelectableTypeSet default_enabled_types{
      UserSelectableType::kAutofill, UserSelectableType::kPasswords,
      UserSelectableType::kPayments, UserSelectableType::kPreferences};

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
  // Bookmarks and Reading List are only selected by default on mobile.
  default_enabled_types.Put(UserSelectableType::kBookmarks);
  default_enabled_types.Put(UserSelectableType::kReadingList);
#endif

  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(gaia_id_)
                  .HasAll(default_enabled_types));
  ASSERT_FALSE(
      SyncPrefs(&pref_service_)
          .GetSelectedTypesForAccount(gaia_id_)
          .HasAny({UserSelectableType::kHistory, UserSelectableType::kTabs}));

  SyncPrefs::MigrateGlobalDataTypePrefsToAccount(&pref_service_, gaia_id_);

  // All supported types should be considered selected for this account now,
  // including kHistory and kTabs.
  SyncPrefs prefs(&pref_service_);
  UserSelectableTypeSet selected_types =
      prefs.GetSelectedTypesForAccount(gaia_id_);
  EXPECT_TRUE(selected_types.HasAll(default_enabled_types));
  EXPECT_TRUE(selected_types.Has(UserSelectableType::kHistory));
  EXPECT_TRUE(selected_types.Has(UserSelectableType::kTabs));
  EXPECT_TRUE(selected_types.Has(UserSelectableType::kPasswords));
}

TEST_F(SyncPrefsMigrationTest, GlobalToAccount_CustomState) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  // The user has chosen specific data types to sync. In this example, Bookmarks
  // and Preferences are disabled.
  const UserSelectableTypeSet old_selected_types{
      UserSelectableType::kAutofill,    UserSelectableType::kHistory,
      UserSelectableType::kPasswords,   UserSelectableType::kPayments,
      UserSelectableType::kReadingList, UserSelectableType::kTabs};
  {
    SyncPrefs old_prefs(&pref_service_);
    old_prefs.SetSelectedTypesForSyncingUser(
        /*keep_everything_synced=*/false,
        /*registered_types=*/UserSelectableTypeSet::All(), old_selected_types);
  }

  // Pre-migration (without any explicit per-account settings), most supported
  // types are considered selected by default, including Preferences - but not
  // History or Tabs.  Note that this is not exhaustive - depending on feature
  // flags, additional types may be supported and default-enabled.
  UserSelectableTypeSet pre_migration_selected_types{
      UserSelectableType::kAutofill, UserSelectableType::kPasswords,
      UserSelectableType::kPayments, UserSelectableType::kPreferences};

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
  // Bookmarks and Reading List are only selected by default on mobile.
  pre_migration_selected_types.Put(UserSelectableType::kBookmarks);
  pre_migration_selected_types.Put(UserSelectableType::kReadingList);
#endif

  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(gaia_id_)
                  .HasAll(pre_migration_selected_types));

  ASSERT_FALSE(
      SyncPrefs(&pref_service_)
          .GetSelectedTypesForAccount(gaia_id_)
          .HasAny({UserSelectableType::kHistory, UserSelectableType::kTabs}));

  SyncPrefs::MigrateGlobalDataTypePrefsToAccount(&pref_service_, gaia_id_);

  // After the migration, exactly the same types should be selected as before.
  SyncPrefs prefs(&pref_service_);
  EXPECT_THAT(prefs.GetSelectedTypesForAccount(gaia_id_),
              ContainerEq(old_selected_types));
}

TEST_F(SyncPrefsMigrationTest, GlobalToAccount_HistoryDisabled) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  // All types except for kHistory are selected in the global prefs.
  {
    SyncPrefs old_prefs(&pref_service_);
    UserSelectableTypeSet selected_types = UserSelectableTypeSet::All();
    selected_types.Remove(UserSelectableType::kHistory);
    old_prefs.SetSelectedTypesForSyncingUser(
        /*keep_everything_synced=*/false,
        /*registered_types=*/UserSelectableTypeSet::All(), selected_types);
  }

  SyncPrefs::MigrateGlobalDataTypePrefsToAccount(&pref_service_, gaia_id_);

  // After the migration, both kHistory and kTabs should be disabled, since
  // there is only a single toggle for both of them.
  SyncPrefs prefs(&pref_service_);
  UserSelectableTypeSet selected_types =
      prefs.GetSelectedTypesForAccount(gaia_id_);
  EXPECT_FALSE(selected_types.Has(UserSelectableType::kHistory));
  EXPECT_FALSE(selected_types.Has(UserSelectableType::kTabs));
}

TEST_F(SyncPrefsMigrationTest, GlobalToAccount_TabsDisabled) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  // All types except for kTabs are selected in the global prefs.
  {
    SyncPrefs old_prefs(&pref_service_);
    UserSelectableTypeSet selected_types = UserSelectableTypeSet::All();
    selected_types.Remove(UserSelectableType::kTabs);
    old_prefs.SetSelectedTypesForSyncingUser(
        /*keep_everything_synced=*/false,
        /*registered_types=*/UserSelectableTypeSet::All(), selected_types);
  }

  SyncPrefs::MigrateGlobalDataTypePrefsToAccount(&pref_service_, gaia_id_);

  // After the migration, both kHistory and kTabs should be disabled, since
  // there is only a single toggle for both of them.
  SyncPrefs prefs(&pref_service_);
  UserSelectableTypeSet selected_types =
      prefs.GetSelectedTypesForAccount(gaia_id_);
  EXPECT_FALSE(selected_types.Has(UserSelectableType::kHistory));
  EXPECT_FALSE(selected_types.Has(UserSelectableType::kTabs));
}

TEST_F(SyncPrefsMigrationTest, GlobalToAccount_CustomPassphrase) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  // All types are enabled ("Sync Everything" is true), but the user has a
  // custom passphrase.
  {
    SyncPrefs old_prefs(&pref_service_);
    old_prefs.SetCachedPassphraseType(PassphraseType::kCustomPassphrase);
  }

  // Pre-migration (without any explicit per-account settings), most supported
  // types are considered selected by default - except for kHistory and kTabs.
  // Note that this is not exhaustive - depending on feature flags, additional
  // types may be supported and default-enabled.
  UserSelectableTypeSet default_enabled_types{
      UserSelectableType::kAutofill, UserSelectableType::kPasswords,
      UserSelectableType::kPayments, UserSelectableType::kPreferences};

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
  // Bookmarks and Reading List are only selected by default on mobile.
  default_enabled_types.Put(UserSelectableType::kBookmarks);
  default_enabled_types.Put(UserSelectableType::kReadingList);
#endif

  ASSERT_TRUE(SyncPrefs(&pref_service_)
                  .GetSelectedTypesForAccount(gaia_id_)
                  .HasAll(default_enabled_types));

  SyncPrefs::MigrateGlobalDataTypePrefsToAccount(&pref_service_, gaia_id_);

  // All supported types should be considered selected for this account now,
  // except for kAutofill ("Addresses and more") which should've been disabled
  // for custom passphrase users.
  const UserSelectableTypeSet expected_types =
      base::Difference(default_enabled_types, {UserSelectableType::kAutofill});
  SyncPrefs prefs(&pref_service_);
  UserSelectableTypeSet selected_types =
      prefs.GetSelectedTypesForAccount(gaia_id_);
  EXPECT_TRUE(selected_types.HasAll(expected_types));
}

TEST_F(SyncPrefsMigrationTest,
       GlobalToAccount_SuppressesSyncToSigninMigration) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  SyncPrefs::MigrateGlobalDataTypePrefsToAccount(&pref_service_, gaia_id_);

  // After the GlobalToAccount migration has run, the SyncToSignin migration
  // should not have any effect anymore.
  EXPECT_FALSE(
      SyncPrefs(&pref_service_)
          .MaybeMigratePrefsForSyncToSigninPart1(
              SyncPrefs::SyncAccountState::kSignedInNotSyncing, gaia_id_));
}

TEST_F(SyncPrefsTest, IsTypeDisabledByUserForAccount) {
  base::test::ScopedFeatureList enable_sync_to_signin(
      kReplaceSyncPromosWithSignInPromos);

  ASSERT_FALSE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kBookmarks, gaia_id_));
  ASSERT_FALSE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kReadingList, gaia_id_));
  ASSERT_FALSE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kPasswords, gaia_id_));

  // Set up a policy to disable Bookmarks.
  PrefValueMap policy_prefs;
  SyncPrefs::SetTypeDisabledByPolicy(&policy_prefs,
                                     UserSelectableType::kBookmarks);
  // Copy the policy prefs map over into the PrefService.
  for (const auto& policy_pref : policy_prefs) {
    pref_service_.SetManagedPref(policy_pref.first, policy_pref.second.Clone());
  }

  // Disable Reading List.
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kReadingList,
                                         false, gaia_id_);

  // Enable Passwords.
  sync_prefs_->SetSelectedTypeForAccount(UserSelectableType::kPasswords, true,
                                         gaia_id_);

  // Check for a disabled type by policy.
  EXPECT_FALSE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kBookmarks, gaia_id_));
  // Check for a disabled type by user choice.
  EXPECT_TRUE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kReadingList, gaia_id_));
  // Check for an enabled type by user choice.
  EXPECT_FALSE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kPasswords, gaia_id_));
  // Check for a type with default value.
  EXPECT_FALSE(sync_prefs_->IsTypeDisabledByUserForAccount(
      UserSelectableType::kPreferences, gaia_id_));
}

}  // namespace

}  // namespace syncer
