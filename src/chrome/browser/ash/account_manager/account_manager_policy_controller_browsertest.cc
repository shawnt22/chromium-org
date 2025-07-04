// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/account_manager/account_manager_policy_controller.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ash/constants/ash_switches.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/ash/account_manager/account_manager_policy_controller_factory.h"
#include "chrome/browser/ash/account_manager/child_account_type_changed_user_data.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/signin/identity_test_environment_profile_adaptor.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/ash/components/account_manager/account_manager_facade_factory.h"
#include "chromeos/ash/components/account_manager/account_manager_factory.h"
#include "chromeos/ash/components/browser_context_helper/browser_context_helper.h"
#include "components/account_id/account_id.h"
#include "components/account_manager_core/account.h"
#include "components/account_manager_core/account_manager_facade.h"
#include "components/account_manager_core/chromeos/account_manager.h"
#include "components/account_manager_core/pref_names.h"
#include "components/session_manager/core/session_manager.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/user_manager/test_helper.h"
#include "components/user_manager/user_manager.h"
#include "content/public/test/browser_test.h"
#include "google_apis/gaia/gaia_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

namespace {

constexpr char kFakePrimaryUsername[] = "test-primary@example.com";
constexpr char kFakeSecondaryUsername[] = "test-secondary@example.com";
constexpr GaiaId::Literal kFakeSecondaryGaiaId("fake-secondary-gaia-id");

}  // namespace

class AccountManagerPolicyControllerTest : public InProcessBrowserTest {
 public:
  AccountManagerPolicyControllerTest() = default;

  AccountManagerPolicyControllerTest(
      const AccountManagerPolicyControllerTest&) = delete;
  AccountManagerPolicyControllerTest& operator=(
      const AccountManagerPolicyControllerTest&) = delete;

  ~AccountManagerPolicyControllerTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Disable automatic login.
    command_line->AppendSwitch(ash::switches::kLoginManager);
  }

  void SetUpOnMainThread() override {
    const AccountId account_id = AccountId::FromUserEmailGaiaId(
        kFakePrimaryUsername,
        signin::GetTestGaiaIdForEmail(kFakePrimaryUsername));
    ASSERT_TRUE(user_manager::TestHelper(user_manager::UserManager::Get())
                    .AddRegularUser(account_id));
    session_manager::SessionManager::Get()->CreateSession(
        account_id, user_manager::TestHelper::GetFakeUsernameHash(account_id),
        /*new_user=*/false,
        /*has_active_session=*/false);

    // Prep private fields.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    TestingProfile::Builder profile_builder;
    profile_builder.SetPath(temp_dir_.GetPath().AppendASCII(
        BrowserContextHelper::GetUserBrowserContextDirName(
            user_manager::TestHelper::GetFakeUsernameHash(account_id))));
    profile_builder.SetProfileName(kFakePrimaryUsername);
    profile_ = IdentityTestEnvironmentProfileAdaptor::
        CreateProfileForIdentityTestEnvironment(profile_builder);
    auto* factory =
        g_browser_process->platform_part()->GetAccountManagerFactory();
    account_manager_ = factory->GetAccountManager(profile()->GetPath().value());
    account_manager_facade_ =
        GetAccountManagerFacade(profile()->GetPath().value());
    identity_test_environment_adaptor_ =
        std::make_unique<IdentityTestEnvironmentProfileAdaptor>(profile_.get());

    // Prep the Primary account.
    auto* identity_test_env =
        identity_test_environment_adaptor_->identity_test_env();
    const AccountInfo primary_account_info =
        identity_test_env->MakePrimaryAccountAvailable(
            kFakePrimaryUsername, signin::ConsentLevel::kSignin);
    ASSERT_EQ(account_id,
              AccountId::FromUserEmailGaiaId(primary_account_info.email,
                                             primary_account_info.gaia));

    // Add accounts in Account Manager.
    account_manager_->UpsertAccount(
        ::account_manager::AccountKey::FromGaiaId(primary_account_info.gaia),
        primary_account_info.email,
        account_manager::AccountManager::kInvalidToken);
    account_manager_->UpsertAccount(
        ::account_manager::AccountKey::FromGaiaId(kFakeSecondaryGaiaId),
        kFakeSecondaryUsername, account_manager::AccountManager::kInvalidToken);

    AccountManagerPolicyControllerFactory::GetForBrowserContext(profile());
  }

  void TearDownOnMainThread() override {
    identity_test_environment_adaptor_.reset();
    profile_.reset();
    base::RunLoop().RunUntilIdle();
  }

  std::vector<::account_manager::Account> GetAccountManagerAccounts() {
    CHECK(account_manager_facade_);

    base::test::TestFuture<const std::vector<::account_manager::Account>&>
        future;
    account_manager_facade_->GetAccounts(future.GetCallback());
    return future.Get();
  }

  Profile* profile() { return profile_.get(); }

  signin::IdentityManager* identity_manager() {
    return identity_test_environment_adaptor_->identity_test_env()
        ->identity_manager();
  }

 private:
  base::ScopedTempDir temp_dir_;
  raw_ptr<account_manager::AccountManager, DanglingUntriaged> account_manager_ =
      nullptr;
  raw_ptr<account_manager::AccountManagerFacade> account_manager_facade_ =
      nullptr;
  std::unique_ptr<Profile> profile_;
  std::unique_ptr<IdentityTestEnvironmentProfileAdaptor>
      identity_test_environment_adaptor_;
};

IN_PROC_BROWSER_TEST_F(AccountManagerPolicyControllerTest,
                       ExistingSecondaryAccountsAreNotRemovedIfPolicyIsNotSet) {
  std::vector<::account_manager::Account> accounts =
      GetAccountManagerAccounts();
  // We should have at least 1 Secondary Account.
  const std::vector<::account_manager::Account>::size_type
      initial_num_accounts = accounts.size();
  ASSERT_GT(initial_num_accounts, 1UL);

  // Use default policy value for |kSecondaryGoogleAccountSigninAllowed|
  // (|true|).
  profile()->GetPrefs()->SetBoolean(
      ::account_manager::prefs::kSecondaryGoogleAccountSigninAllowed, true);
  ChildAccountTypeChangedUserData::GetForProfile(profile())->SetValue(false);

  base::RunLoop().RunUntilIdle();

  // All accounts must be intact.
  accounts = GetAccountManagerAccounts();
  EXPECT_EQ(initial_num_accounts, accounts.size());
}

IN_PROC_BROWSER_TEST_F(
    AccountManagerPolicyControllerTest,
    ExistingSecondaryAccountsAreRemovedAfterPolicyApplication) {
  std::vector<::account_manager::Account> accounts =
      GetAccountManagerAccounts();
  // We should have at least 1 Secondary Account.
  ASSERT_GT(accounts.size(), 1UL);

  // Disallow secondary account sign-ins.
  profile()->GetPrefs()->SetBoolean(
      ::account_manager::prefs::kSecondaryGoogleAccountSigninAllowed, false);

  base::RunLoop().RunUntilIdle();

  // Secondary Accounts must be removed.
  const GaiaId gaia_id = BrowserContextHelper::Get()
                             ->GetUserByBrowserContext(profile())
                             ->GetAccountId()
                             .GetGaiaId();
  accounts = GetAccountManagerAccounts();
  ASSERT_EQ(accounts.size(), 1UL);
  EXPECT_EQ(gaia_id, GaiaId(accounts[0].key.id()));
  EXPECT_EQ(gaia_id, identity_manager()
                         ->GetPrimaryAccountInfo(signin::ConsentLevel::kSignin)
                         .gaia);
}

IN_PROC_BROWSER_TEST_F(
    AccountManagerPolicyControllerTest,
    SecondaryAccountsAreRemovedAfterAccountTypeChangedWithCoexistenceEnabled) {
  std::vector<::account_manager::Account> accounts =
      GetAccountManagerAccounts();
  const std::vector<::account_manager::Account>::size_type
      initial_num_accounts = accounts.size();
  // We should have at least 1 Secondary Account.
  ASSERT_GT(initial_num_accounts, 1UL);

  // Disallow secondary account sign-ins.
  ChildAccountTypeChangedUserData::GetForProfile(profile())->SetValue(true);

  base::RunLoop().RunUntilIdle();

  // Secondary Accounts must be removed.
  const GaiaId gaia_id = BrowserContextHelper::Get()
                             ->GetUserByBrowserContext(profile())
                             ->GetAccountId()
                             .GetGaiaId();
  accounts = GetAccountManagerAccounts();
  ASSERT_EQ(accounts.size(), 1UL);
  EXPECT_EQ(gaia_id, GaiaId(accounts[0].key.id()));
  EXPECT_EQ(gaia_id, identity_manager()
                         ->GetPrimaryAccountInfo(signin::ConsentLevel::kSignin)
                         .gaia);
}

}  // namespace ash
