// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/internal/identity_manager/profile_oauth2_token_service_delegate_chromeos.h"

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/containers/contains.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/task_environment.h"
#include "components/account_manager_core/account.h"
#include "components/account_manager_core/account_manager_facade.h"
#include "components/account_manager_core/account_manager_facade_impl.h"
#include "components/account_manager_core/chromeos/account_manager.h"
#include "components/account_manager_core/chromeos/account_manager_mojo_service.h"
#include "components/account_manager_core/mock_account_manager_facade.h"
#include "components/signin/internal/identity_manager/account_tracker_service.h"
#include "components/signin/internal/identity_manager/mock_profile_oauth2_token_service_observer.h"
#include "components/signin/internal/identity_manager/profile_oauth2_token_service_observer.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/test_signin_client.h"
#include "components/signin/public/identity_manager/account_capabilities_test_mutator.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "google_apis/gaia/gaia_id.h"
#include "google_apis/gaia/gaia_urls.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "google_apis/gaia/oauth2_access_token_consumer.h"
#include "google_apis/gaia/oauth2_access_token_fetcher.h"
#include "google_apis/gaia/oauth2_access_token_manager_test_util.h"
#include "services/network/test/test_network_connection_tracker.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using ::account_manager::AccountManager;
using ::account_manager::AccountManagerFacade;

constexpr GaiaId::Literal kGaiaId("gaia-id");
constexpr char kGaiaToken[] = "gaia-token";
constexpr char kUserEmail[] = "user@gmail.com";
constexpr char kNoBindingChallenge[] = "";

class AccessTokenConsumer : public OAuth2AccessTokenConsumer {
 public:
  AccessTokenConsumer() = default;

  AccessTokenConsumer(const AccessTokenConsumer&) = delete;
  AccessTokenConsumer& operator=(const AccessTokenConsumer&) = delete;

  ~AccessTokenConsumer() override = default;

  void OnGetTokenSuccess(const TokenResponse& token_response) override {
    ++num_access_token_fetch_success_;
  }

  void OnGetTokenFailure(const GoogleServiceAuthError& error) override {
    ++num_access_token_fetch_failure_;
  }

  std::string GetConsumerName() const override {
    return "profile_oauth2_token_service_delegate_chromeos_unittest";
  }

  int num_access_token_fetch_success_ = 0;
  int num_access_token_fetch_failure_ = 0;
};

class TestOAuth2TokenServiceObserver
    : public ProfileOAuth2TokenServiceObserver {
 public:
  // |delegate| is a non-owning pointer to an
  // |ProfileOAuth2TokenServiceDelegate| that MUST outlive |this| instance.
  explicit TestOAuth2TokenServiceObserver(
      ProfileOAuth2TokenServiceDelegate* delegate)
      : delegate_(delegate) {
    token_service_observation_.Observe(delegate_);
  }

  ~TestOAuth2TokenServiceObserver() override = default;

  void StartBatchChanges() {
    EXPECT_FALSE(is_inside_batch_);
    is_inside_batch_ = true;

    // Start a new batch
    batch_change_records_.emplace_back(std::vector<CoreAccountId>());
  }

  void OnEndBatchChanges() override {
    EXPECT_TRUE(is_inside_batch_);
    is_inside_batch_ = false;
  }

  void OnRefreshTokenAvailable(const CoreAccountId& account_id) override {
    if (!is_inside_batch_) {
      StartBatchChanges();
    }

    // We should not be seeing any cached errors for a freshly updated account,
    // except when they have been generated by us (i.e.
    // CREDENTIALS_REJECTED_BY_CLIENT).
    const GoogleServiceAuthError error = delegate_->GetAuthError(account_id);
    EXPECT_TRUE((error == GoogleServiceAuthError::AuthErrorNone()) ||
                (error.state() ==
                     GoogleServiceAuthError::State::INVALID_GAIA_CREDENTIALS &&
                 error.GetInvalidGaiaCredentialsReason() ==
                     GoogleServiceAuthError::InvalidGaiaCredentialsReason::
                         CREDENTIALS_REJECTED_BY_CLIENT));

    account_ids_.insert(account_id);

    // Record the |account_id| in the last batch.
    batch_change_records_.rbegin()->emplace_back(account_id);
  }

  void OnRefreshTokensLoaded() override { refresh_tokens_loaded_ = true; }

  void OnRefreshTokenRevoked(const CoreAccountId& account_id) override {
    if (!is_inside_batch_) {
      StartBatchChanges();
    }

    account_ids_.erase(account_id);
    // Record the |account_id| in the last batch.
    batch_change_records_.rbegin()->emplace_back(account_id);
  }

  void OnAuthErrorChanged(
      const CoreAccountId& account_id,
      const GoogleServiceAuthError& auth_error,
      signin_metrics::SourceForRefreshTokenOperation source) override {
    last_err_account_id_ = account_id;
    last_err_ = auth_error;
    on_auth_error_changed_calls_++;
  }

  int on_auth_error_changed_calls_ = 0;

  CoreAccountId last_err_account_id_;
  GoogleServiceAuthError last_err_;
  std::set<CoreAccountId> account_ids_;
  bool is_inside_batch_ = false;
  bool refresh_tokens_loaded_ = false;

  // Records batch changes for later verification. Each index of this vector
  // represents a batch change. Each batch change is a vector of account ids for
  // which |OnRefreshTokenAvailable| is called.
  std::vector<std::vector<CoreAccountId>> batch_change_records_;

  // Non-owning pointer.
  const raw_ptr<ProfileOAuth2TokenServiceDelegate> delegate_;
  base::ScopedObservation<ProfileOAuth2TokenServiceDelegate,
                          ProfileOAuth2TokenServiceObserver>
      token_service_observation_{this};
};

}  // namespace

class ProfileOAuth2TokenServiceDelegateChromeOSTest : public testing::Test {
 public:
  ProfileOAuth2TokenServiceDelegateChromeOSTest() = default;

  ProfileOAuth2TokenServiceDelegateChromeOSTest(
      const ProfileOAuth2TokenServiceDelegateChromeOSTest&) = delete;
  ProfileOAuth2TokenServiceDelegateChromeOSTest& operator=(
      const ProfileOAuth2TokenServiceDelegateChromeOSTest&) = delete;

  ~ProfileOAuth2TokenServiceDelegateChromeOSTest() override = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
    AccountTrackerService::RegisterPrefs(pref_service_.registry());
    AccountManager::RegisterPrefs(pref_service_.registry());

    client_ = std::make_unique<TestSigninClient>(&pref_service_);

    account_manager_.Initialize(tmp_dir_.GetPath(),
                                client_->GetURLLoaderFactory(),
                                immediate_callback_runner_);
    account_manager_.SetPrefService(&pref_service_);
    task_environment_.RunUntilIdle();

    account_manager_mojo_service_ =
        std::make_unique<crosapi::AccountManagerMojoService>(&account_manager_);
    account_manager_facade_ =
        CreateAccountManagerFacade(account_manager_mojo_service_.get());

    account_tracker_service_.Initialize(&pref_service_, base::FilePath());

    account_info_ = CreateAccountInfoTestFixture(kGaiaId, kUserEmail);
    account_tracker_service_.SeedAccountInfo(account_info_);
    ResetProfileOAuth2TokenServiceDelegateChromeOS();
  }

  void ResetProfileOAuth2TokenServiceDelegateChromeOS() {
    delegate_.reset();
    delegate_ =
        std::make_unique<signin::ProfileOAuth2TokenServiceDelegateChromeOS>(
            client_.get(), &account_tracker_service_,
            network::TestNetworkConnectionTracker::GetInstance(),
            account_manager_facade_.get(),
            /*is_regular_profile=*/true);
    delegate_->SetOnRefreshTokenRevokedNotified(base::DoNothing());

    LoadCredentialsAndWaitForCompletion(
        /*primary_account_id=*/account_info_.account_id);
  }

  account_manager::AccountKey gaia_account_key() const {
    return account_manager::AccountKey::FromGaiaId(account_info_.gaia);
  }

  AccountInfo CreateAccountInfoTestFixture(const GaiaId& gaia_id,
                                           const std::string& email) {
    AccountInfo account_info;

    account_info.gaia = gaia_id;
    account_info.email = email;
    account_info.full_name = "name";
    account_info.given_name = "name";
    account_info.hosted_domain = "example.com";
    account_info.locale = "en";
    account_info.picture_url = "https://example.com";
    account_info.account_id = account_tracker_service_.PickAccountIdForAccount(
        account_info.gaia, account_info.email);
    AccountCapabilitiesTestMutator(&account_info.capabilities)
        .set_is_subject_to_enterprise_policies(true);

    // Cannot use |ASSERT_TRUE| due to a |void| return type in an |ASSERT_TRUE|
    // branch.
    EXPECT_TRUE(account_info.IsValid());

    return account_info;
  }

  void AddSuccessfulOAuthTokenResponse() {
    client_->GetTestURLLoaderFactory()->AddResponse(
        GaiaUrls::GetInstance()->oauth2_token_url().spec(),
        GetValidTokenResponse("token", 3600));
  }

  void LoadCredentialsAndWaitForCompletion(
      const CoreAccountId& primary_account_id) {
    signin::MockProfileOAuth2TokenServiceObserver observer(delegate_.get());
    base::RunLoop run_loop;
    EXPECT_CALL(observer, OnRefreshTokensLoaded())
        .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
    delegate_->LoadCredentials(primary_account_id);
    run_loop.Run();
  }

  void UpsertAccountAndWaitForCompletion(
      const ::account_manager::AccountKey& account_key,
      const std::string& raw_email,
      const std::string& token) {
    ASSERT_EQ(account_key.account_type(), account_manager::AccountType::kGaia);

    // `ProfileOAuth2TokenServiceDelegateChromeOS` asynchronously obtains error
    // statuses for Gaia accounts, so we have to wait for a notification from
    // the delegate itself here.
    signin::MockProfileOAuth2TokenServiceObserver observer(delegate_.get());
    base::RunLoop run_loop;
    EXPECT_CALL(observer, OnRefreshTokenAvailable(testing::_))
        .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
    account_manager_.UpsertAccount(account_key, raw_email, token);
    run_loop.Run();
  }

  void RemoveAccountAndWaitForCompletion(
      const ::account_manager::AccountKey& account_key) {
    ASSERT_EQ(account_key.account_type(), account_manager::AccountType::kGaia);
    signin::MockProfileOAuth2TokenServiceObserver observer(delegate_.get());
    base::RunLoop run_loop;
    EXPECT_CALL(observer, OnRefreshTokenRevoked(testing::_))
        .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
    account_manager_.RemoveAccount(account_key);
    run_loop.Run();
  }

  std::unique_ptr<AccountManagerFacade> CreateAccountManagerFacade(
      crosapi::AccountManagerMojoService* account_manager_mojo_service) {
    DCHECK(account_manager_mojo_service);
    mojo::Remote<crosapi::mojom::AccountManager> remote;
    account_manager_mojo_service->BindReceiver(
        remote.BindNewPipeAndPassReceiver());
    return std::make_unique<account_manager::AccountManagerFacadeImpl>(
        std::move(remote),
        /*remote_version=*/std::numeric_limits<uint32_t>::max(),
        /*account_manager_for_tests=*/nullptr);
  }

  base::test::TaskEnvironment task_environment_;

  base::ScopedTempDir tmp_dir_;
  AccountInfo account_info_;
  AccountTrackerService account_tracker_service_;
  AccountManager account_manager_;
  std::unique_ptr<crosapi::AccountManagerMojoService>
      account_manager_mojo_service_;
  std::unique_ptr<account_manager::AccountManagerFacade>
      account_manager_facade_;
  std::unique_ptr<signin::ProfileOAuth2TokenServiceDelegateChromeOS> delegate_;
  AccountManager::DelayNetworkCallRunner immediate_callback_runner_ =
      base::BindRepeating(
          [](base::OnceClosure closure) -> void { std::move(closure).Run(); });
  sync_preferences::TestingPrefServiceSyncable pref_service_;
  std::unique_ptr<TestSigninClient> client_;
};

// Refresh tokens should load successfully for non-regular (Signin and Lock
// Screen) Profiles.
TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokensAreLoadedForNonRegularProfiles) {
  // Create an instance of Account Manager but do not
  // |AccountManager::Initialize| it. This mimics Signin and Lock Screen Profile
  // behaviour.
  AccountManager account_manager;

  auto delegate =
      std::make_unique<signin::ProfileOAuth2TokenServiceDelegateChromeOS>(
          client_.get(), &account_tracker_service_,
          network::TestNetworkConnectionTracker::GetInstance(),
          account_manager_facade_.get(),
          /*is_regular_profile=*/false);
  TestOAuth2TokenServiceObserver observer(delegate.get());

  // Test that LoadCredentials works as expected.
  EXPECT_FALSE(observer.refresh_tokens_loaded_);
  delegate->LoadCredentials(CoreAccountId() /* primary_account_id */);
  EXPECT_TRUE(observer.refresh_tokens_loaded_);
  EXPECT_EQ(
      signin::LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
      delegate->load_credentials_state());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokenIsAvailableReturnsTrueForValidGaiaTokens) {
  EXPECT_EQ(
      signin::LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
      delegate_->load_credentials_state());

  EXPECT_FALSE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_FALSE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));

  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);

  EXPECT_TRUE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_TRUE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokenIsAvailableReturnsTrueForInvalidGaiaTokens) {
  EXPECT_EQ(
      signin::LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
      delegate_->load_credentials_state());

  EXPECT_FALSE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_FALSE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));

  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail,
                                    AccountManager::kInvalidToken);

  EXPECT_TRUE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_TRUE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnAuthErrorChange) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);

  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(error, observer.last_err_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       OnAuthErrorChangedAfterUpdatingCredentials) {
  testing::StrictMock<signin::MockProfileOAuth2TokenServiceObserver> observer(
      delegate_.get());

  {
    testing::InSequence in_sequence;
    base::RunLoop upsert_run_loop;
    EXPECT_CALL(observer, OnRefreshTokenAvailable)
        .WillOnce(base::test::RunClosure(upsert_run_loop.QuitClosure()));
    EXPECT_CALL(observer, OnEndBatchChanges);
    // `OnAuthErrorChanged()` is called *after* `OnRefreshTokenAvailable()`
    // *and* `OnEndBatchChanges()` after adding a new account on ChromeOS.
    EXPECT_CALL(observer, OnAuthErrorChanged);
    account_manager_.UpsertAccount(gaia_account_key(), kUserEmail, kGaiaToken);
    upsert_run_loop.Run();
    testing::Mock::VerifyAndClearExpectations(&observer);
  }

  {
    testing::InSequence in_sequence;
    base::RunLoop update_run_loop;
    EXPECT_CALL(observer, OnRefreshTokenAvailable)
        .WillOnce(base::test::RunClosure(update_run_loop.QuitClosure()));
    EXPECT_CALL(observer, OnEndBatchChanges);
    // `OnAuthErrorChanged()` is also called when a token is updated without
    // changing its error state.
    EXPECT_CALL(observer, OnAuthErrorChanged);
    account_manager_.UpdateToken(gaia_account_key(), "new-gaia-token");
    update_run_loop.Run();
    testing::Mock::VerifyAndClearExpectations(&observer);
  }
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotNotifiedIfErrorDidntChange) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);

  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(1, observer.on_auth_error_changed_calls_);
  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(1, observer.on_auth_error_changed_calls_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedIfErrorDidChange) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  delegate_->UpdateAuthError(
      account_info_.account_id,
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR));
  EXPECT_EQ(1, observer.on_auth_error_changed_calls_);

  delegate_->UpdateAuthError(
      account_info_.account_id,
      GoogleServiceAuthError(
          GoogleServiceAuthError::State::INVALID_GAIA_CREDENTIALS));
  EXPECT_EQ(2, observer.on_auth_error_changed_calls_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnCredentialsInsertion) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);

  EXPECT_EQ(1UL, observer.account_ids_.size());
  EXPECT_EQ(account_info_.account_id, *observer.account_ids_.begin());
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(), observer.last_err_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversDoNotSeeCachedErrorsOnCredentialsUpdate) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);

  // Deliberately add an error.
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);
  delegate_->UpdateAuthError(account_info_.account_id, error);

  // Update credentials. The delegate will check if see cached errors.
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    "new-token");
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversDoNotSeeCachedErrorsOnAccountRemoval) {
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);
  // Deliberately add an error.
  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
  RemoveAccountAndWaitForCompletion(gaia_account_key());
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            delegate_->GetAuthError(account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       DummyTokensArePreEmptivelyRejected) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    AccountManager::kInvalidToken);

  const GoogleServiceAuthError error =
      delegate_->GetAuthError(account_info_.account_id);
  EXPECT_EQ(GoogleServiceAuthError::State::INVALID_GAIA_CREDENTIALS,
            error.state());
  EXPECT_EQ(GoogleServiceAuthError::InvalidGaiaCredentialsReason::
                CREDENTIALS_REJECTED_BY_CLIENT,
            error.GetInvalidGaiaCredentialsReason());

  // Observer notification should also have notified about the same error.
  EXPECT_EQ(error, observer.last_err_);
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnCredentialsUpdate) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);

  EXPECT_EQ(1UL, observer.account_ids_.size());
  EXPECT_EQ(account_info_.account_id, *observer.account_ids_.begin());
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(), observer.last_err_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotNotifiedIfCredentialsAreNotUpdated) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());

  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);
  observer.account_ids_.clear();
  observer.last_err_account_id_ = CoreAccountId();
  // UpsertAccountAndWaitForCompletion can't be used here, as it uses an
  // observer to wait for completion. Observers aren't called in this flow, so
  // UpsertAccountAndWaitForCompletion would hang here.
  account_manager_.UpsertAccount(gaia_account_key(), account_info_.email,
                                 kGaiaToken);
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(observer.account_ids_.empty());
  EXPECT_TRUE(observer.last_err_account_id_.empty());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BatchChangeObserversAreNotifiedOnCredentialsUpdate) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);

  EXPECT_EQ(1UL, observer.batch_change_records_.size());
  EXPECT_EQ(1UL, observer.batch_change_records_[0].size());
  EXPECT_EQ(account_info_.account_id, observer.batch_change_records_[0][0]);
}

// If observers register themselves with |ProfileOAuth2TokenServiceDelegate|
// before |AccountManager| has been initialized, they should receive all the
// accounts stored in |AccountManager| in a single batch.
TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BatchChangeObserversAreNotifiedOncePerBatch) {
  // Setup
  AccountInfo account1 = CreateAccountInfoTestFixture(
      GaiaId("1"), "user1@example.com" /* email */);
  AccountInfo account2 = CreateAccountInfoTestFixture(
      GaiaId("2"), "user2@example.com" /* email */);

  account_tracker_service_.SeedAccountInfo(account1);
  account_tracker_service_.SeedAccountInfo(account2);
  account_manager_.UpsertAccount(
      account_manager::AccountKey::FromGaiaId(account1.gaia),
      "user1@example.com", "token1");
  account_manager_.UpsertAccount(
      account_manager::AccountKey::FromGaiaId(account2.gaia),
      "user2@example.com", "token2");
  task_environment_.RunUntilIdle();

  AccountManager account_manager;
  // AccountManager will not be fully initialized until
  // |task_environment_.RunUntilIdle()| is called.
  account_manager.Initialize(tmp_dir_.GetPath(), client_->GetURLLoaderFactory(),
                             immediate_callback_runner_);
  account_manager.SetPrefService(&pref_service_);

  auto account_manager_mojo_service =
      std::make_unique<crosapi::AccountManagerMojoService>(&account_manager);
  auto account_manager_facade =
      CreateAccountManagerFacade(account_manager_mojo_service.get());

  // Register callbacks before AccountManager has been fully initialized.
  auto delegate =
      std::make_unique<signin::ProfileOAuth2TokenServiceDelegateChromeOS>(
          client_.get(), &account_tracker_service_,
          network::TestNetworkConnectionTracker::GetInstance(),
          account_manager_facade.get(),
          /*is_regular_profile=*/true);
  delegate->LoadCredentials(account1.account_id /* primary_account_id */);
  TestOAuth2TokenServiceObserver observer(delegate.get());
  // Wait until AccountManager is fully initialized.
  task_environment_.RunUntilIdle();

  // Tests

  // The observer should receive at least one batch change callback: batch of
  // all accounts stored in AccountManager: because of the delegate's
  // invocation of |AccountManagerFacade::GetAccounts| in |LoadCredentials|.
  EXPECT_FALSE(observer.batch_change_records_.empty());
  const std::vector<CoreAccountId>& first_batch =
      observer.batch_change_records_[0];
  EXPECT_EQ(2UL, first_batch.size());
  EXPECT_TRUE(base::Contains(first_batch, account1.account_id));
  EXPECT_TRUE(base::Contains(first_batch, account2.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       GetAccountsReturnsGaiaAccounts) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);

  std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ(account_info_.account_id, accounts[0]);
}

// |GetAccounts| should return all known Gaia accounts, whether or not they have
// a "valid" refresh token stored against them.
TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       GetAccountsReturnsGaiaAccountsWithInvalidTokens) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail,
                                    AccountManager::kInvalidToken);

  std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ(account_info_.account_id, accounts[0]);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokenMustBeAvailableForAllAccountsReturnedByGetAccounts) {
  EXPECT_EQ(
      signin::LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
      delegate_->load_credentials_state());
  EXPECT_TRUE(delegate_->GetAccounts().empty());
  const std::string kUserEmail2 = "random-email2@example.com";

  // Insert 2 Gaia accounts: 1 with a valid refresh token and 1 with a dummy
  // token.
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);

  account_manager::AccountKey gaia_account_key2 =
      account_manager::AccountKey::FromGaiaId(GaiaId("random-gaia-id"));
  account_tracker_service_.SeedAccountInfo(CreateAccountInfoTestFixture(
      GaiaId(gaia_account_key2.id()), kUserEmail2));
  UpsertAccountAndWaitForCompletion(gaia_account_key2, kUserEmail2,
                                    AccountManager::kInvalidToken);

  // Verify.
  const std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  // 2 Gaia accounts should be returned.
  EXPECT_EQ(2UL, accounts.size());
  // And |RefreshTokenIsAvailable| should return true for these accounts.
  for (const CoreAccountId& account : accounts) {
    EXPECT_TRUE(delegate_->RefreshTokenIsAvailable(account));
  }
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       UpdateCredentialsSucceeds) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);

  std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ(account_info_.account_id, accounts[0]);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnAccountRemoval) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);

  TestOAuth2TokenServiceObserver observer(delegate_.get());
  RemoveAccountAndWaitForCompletion(gaia_account_key());

  EXPECT_EQ(1UL, observer.batch_change_records_.size());
  EXPECT_EQ(1UL, observer.batch_change_records_[0].size());
  EXPECT_EQ(account_info_.account_id, observer.batch_change_records_[0][0]);
  EXPECT_TRUE(observer.account_ids_.empty());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       AccountRemovedRightAfterAccountUpserted) {
  // Use StrictMock to verify that no observer methods are invoked.
  testing::StrictMock<signin::MockProfileOAuth2TokenServiceObserver> observer(
      delegate_.get());

  // `UpsertAccount` will asynchronously send a notification through
  // `AccountManagerFacade`, so `RemoveAccount` should remove the account before
  // `ProfileOAuth2TokenServiceDelegateChromeOS` can add this account.
  account_manager_.UpsertAccount(gaia_account_key(), account_info_.email,
                                 kGaiaToken);
  account_manager_.RemoveAccount(gaia_account_key());

  task_environment_.RunUntilIdle();

  EXPECT_EQ(0UL, delegate_->GetAccounts().size());
  // Destroying the mock will verify no observer methods were called.
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       PreexistingAccountRemovedRightAfterAccountTokenUpdate) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);
  EXPECT_EQ(1UL, delegate_->GetAccounts().size());

  base::RunLoop run_loop;
  signin::MockProfileOAuth2TokenServiceObserver observer(delegate_.get());

  // Since this account already existed, `RemoveAccount` should trigger
  // `OnRefreshTokenRevoked` call to observers.
  EXPECT_CALL(observer, OnRefreshTokenRevoked(account_info_.account_id))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));

  account_manager_.UpsertAccount(gaia_account_key(), account_info_.email,
                                 AccountManager::kInvalidToken);
  account_manager_.RemoveAccount(gaia_account_key());

  run_loop.Run();

  EXPECT_EQ(0UL, delegate_->GetAccounts().size());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       SigninErrorObserversAreNotifiedOnAuthErrorChange) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);

  delegate_->UpdateAuthError(account_info_.account_id, error);

  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       TransientErrorsAreNotShown) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);
  auto transient_error = GoogleServiceAuthError(
      GoogleServiceAuthError::State::SERVICE_UNAVAILABLE);
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            delegate_->GetAuthError(account_info_.account_id));

  delegate_->UpdateAuthError(account_info_.account_id, transient_error);

  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            delegate_->GetAuthError(account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BackOffIsTriggerredForTransientErrors) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);
  auto transient_error = GoogleServiceAuthError(
      GoogleServiceAuthError::State::SERVICE_UNAVAILABLE);
  delegate_->UpdateAuthError(account_info_.account_id, transient_error);
  // Add a dummy success response. The actual network call has not been made
  // yet.
  AddSuccessfulOAuthTokenResponse();

  // Transient error should repeat until backoff period expires.
  AccessTokenConsumer access_token_consumer;
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_failure_);
  std::vector<std::string> scopes{"scope"};
  std::unique_ptr<OAuth2AccessTokenFetcher> fetcher =
      delegate_->CreateAccessTokenFetcher(
          account_info_.account_id, delegate_->GetURLLoaderFactory(),
          &access_token_consumer, kNoBindingChallenge);
  task_environment_.RunUntilIdle();
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
  // Expect a positive backoff time.
  EXPECT_GT(delegate_->BackoffEntry()->GetTimeUntilRelease(),
            base::TimeDelta());

  // Pretend that backoff has expired and try again.
  delegate_->backoff_entry_->SetCustomReleaseTime(base::TimeTicks());
  fetcher = delegate_->CreateAccessTokenFetcher(
      account_info_.account_id, delegate_->GetURLLoaderFactory(),
      &access_token_consumer, kNoBindingChallenge);
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BackOffIsResetOnNetworkChange) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);
  auto transient_error = GoogleServiceAuthError(
      GoogleServiceAuthError::State::SERVICE_UNAVAILABLE);
  delegate_->UpdateAuthError(account_info_.account_id, transient_error);
  // Add a dummy success response. The actual network call has not been made
  // yet.
  AddSuccessfulOAuthTokenResponse();

  // Transient error should repeat until backoff period expires.
  AccessTokenConsumer access_token_consumer;
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_failure_);
  std::vector<std::string> scopes{"scope"};
  std::unique_ptr<OAuth2AccessTokenFetcher> fetcher =
      delegate_->CreateAccessTokenFetcher(
          account_info_.account_id, delegate_->GetURLLoaderFactory(),
          &access_token_consumer, kNoBindingChallenge);
  task_environment_.RunUntilIdle();
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
  // Expect a positive backoff time.
  EXPECT_GT(delegate_->BackoffEntry()->GetTimeUntilRelease(),
            base::TimeDelta());

  // Notify of network change and ensure that request now runs.
  delegate_->OnConnectionChanged(
      network::mojom::ConnectionType::CONNECTION_WIFI);
  fetcher = delegate_->CreateAccessTokenFetcher(
      account_info_.account_id, delegate_->GetURLLoaderFactory(),
      &access_token_consumer, kNoBindingChallenge);
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       AccountErrorsAreReportedToAccountManagerFacade) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), account_info_.email,
                                    kGaiaToken);
  account_manager::MockAccountManagerFacadeObserver observer;
  account_manager_facade_->AddObserver(&observer);
  // Flush all the pending Mojo messages before setting expectations.
  base::RunLoop().RunUntilIdle();

  GoogleServiceAuthError error =
      GoogleServiceAuthError::FromInvalidGaiaCredentialsReason(
          GoogleServiceAuthError::InvalidGaiaCredentialsReason::
              CREDENTIALS_REJECTED_BY_SERVER);
  base::RunLoop run_loop;
  EXPECT_CALL(observer, OnAuthErrorChanged(gaia_account_key(), error))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  delegate_->UpdateAuthError(account_info_.account_id, error);
  run_loop.Run();

  account_manager_facade_->RemoveObserver(&observer);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       AccountErrorNotificationsFromAccountManagerFacadeArePropagated) {
  UpsertAccountAndWaitForCompletion(gaia_account_key(), kUserEmail, kGaiaToken);
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  GoogleServiceAuthError error =
      GoogleServiceAuthError::FromInvalidGaiaCredentialsReason(
          GoogleServiceAuthError::InvalidGaiaCredentialsReason::
              CREDENTIALS_REJECTED_BY_SERVER);

  // Simulate an observer notification from AccountManagerFacade.
  delegate_->OnAuthErrorChanged(gaia_account_key(), error);
  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(error, observer.last_err_);
}
