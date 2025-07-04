// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/accounts_policy_manager.h"

#include "base/auto_reset.h"
#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/signin/chrome_signin_client.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_utils.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/startup/startup_types.h"
#include "chrome/browser/ui/webui/profile_helper.h"
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "components/policy/core/common/features.h"
#include "components/signin/public/identity_manager/accounts_mutator.h"
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_CHROMEOS)
// Manager that presents the profile will be deleted dialog on the first active
// browser window.
class AccountsPolicyManager::DeleteProfileDialogManager
    : public BrowserListObserver {
 public:
  DeleteProfileDialogManager(std::string primary_account_email,
                             AccountsPolicyManager* delegate)
      : primary_account_email_(primary_account_email), delegate_(delegate) {}
  ~DeleteProfileDialogManager() override { BrowserList::RemoveObserver(this); }

  DeleteProfileDialogManager(const DeleteProfileDialogManager&) = delete;
  DeleteProfileDialogManager& operator=(const DeleteProfileDialogManager&) =
      delete;

  void PresentDialogOnAllBrowserWindows(
      Profile* profile,
      bool auto_confirm_profile_deletion_for_testing) {
    DCHECK(profile);
    DCHECK(profile_path_.empty());
    profile_path_ = profile->GetPath();

    if (auto_confirm_profile_deletion_for_testing) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&DeleteProfileDialogManager::
                             HandleUserConfirmedProfileDeletionAndDie,
                         weak_factory_.GetWeakPtr()));

      return;
    }

    BrowserList::AddObserver(this);
    Browser* active_browser = chrome::FindLastActiveWithProfile(profile);
    if (active_browser) {
      OnBrowserSetLastActive(active_browser);
    }
  }

  void OnBrowserSetLastActive(Browser* browser) override {
    DCHECK(!profile_path_.empty());

    if (profile_path_ != browser->profile()->GetPath()) {
      return;
    }

    active_browser_ = browser;

    // Display the dialog on the next run loop as otherwise the dialog can block
    // browser from displaying because the dialog creates a nested run loop.
    //
    // This happens because the browser window is not fully created yet when
    // OnBrowserSetLastActive() is called. To finish the creation, the code
    // needs to return from OnBrowserSetLastActive().
    //
    // However, if we open a warning dialog from OnBrowserSetLastActive()
    // synchronously, it will create a nested run loop that will not return
    // from OnBrowserSetLastActive() until the dialog is dismissed. But the user
    // cannot dismiss the dialog because the browser is not even shown!
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&DeleteProfileDialogManager::ShowDeleteProfileDialog,
                       weak_factory_.GetWeakPtr(), browser->AsWeakPtr()));
  }

  // Called immediately after a browser becomes not active.
  void OnBrowserNoLongerActive(Browser* browser) override {
    if (active_browser_ == browser) {
      active_browser_ = nullptr;
    }
  }

  void OnBrowserRemoved(Browser* browser) override {
    if (active_browser_ == browser) {
      active_browser_ = nullptr;
    }
  }

 private:
  void ShowDeleteProfileDialog(base::WeakPtr<Browser> active_browser) {
    // Block opening dialog from nested task.
    static bool is_dialog_shown = false;
    if (is_dialog_shown) {
      return;
    }
    base::AutoReset<bool> auto_reset(&is_dialog_shown, true);

    // Check the |active_browser_| hasn't changed while waiting for the task to
    // be executed.
    if (!active_browser_ || active_browser_ != active_browser.get()) {
      return;
    }

    // Show the dialog.
    DCHECK(active_browser_->window()->GetNativeWindow());
    chrome::MessageBoxResult result = chrome::ShowWarningMessageBoxSync(
        active_browser_->window()->GetNativeWindow(),
        l10n_util::GetStringUTF16(IDS_PROFILE_WILL_BE_DELETED_DIALOG_TITLE),
        l10n_util::GetStringFUTF16(
            IDS_PROFILE_WILL_BE_DELETED_DIALOG_DESCRIPTION,
            base::ASCIIToUTF16(primary_account_email_),
            base::ASCIIToUTF16(
                gaia::ExtractDomainName(primary_account_email_))));

    switch (result) {
      case chrome::MessageBoxResult::MESSAGE_BOX_RESULT_NO: {
        // If the warning dialog is automatically dismissed or the user closed
        // the dialog by clicking on the close "X" button, then re-present the
        // dialog (the user should not be able to interact with the
        // `active_browser_` window as the profile must be deleted).
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(&DeleteProfileDialogManager::ShowDeleteProfileDialog,
                           weak_factory_.GetWeakPtr(),
                           active_browser_->AsWeakPtr()));
        break;
      }
      case chrome::MessageBoxResult::MESSAGE_BOX_RESULT_YES:
        HandleUserConfirmedProfileDeletionAndDie();
        break;
      case chrome::MessageBoxResult::MESSAGE_BOX_RESULT_DEFERRED:
        NOTREACHED() << "Message box must not return deferred result when run "
                        "synchronously";
    }
  }

  void HandleUserConfirmedProfileDeletionAndDie() {
    delegate_->OnUserConfirmedProfileDeletion(this, profile_path_);
    // |this| may be destroyed at this point. Avoid using it.
  }

  std::string primary_account_email_;
  raw_ptr<AccountsPolicyManager> delegate_;
  base::FilePath profile_path_;
  raw_ptr<Browser> active_browser_;
  base::WeakPtrFactory<DeleteProfileDialogManager> weak_factory_{this};
};
#endif  // defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_CHROMEOS)

AccountsPolicyManager::AccountsPolicyManager(Profile* profile)
    : profile_(profile) {
  DCHECK(profile_);
  DCHECK(!profile_->IsOffTheRecord());
}

AccountsPolicyManager::~AccountsPolicyManager() = default;

void AccountsPolicyManager::Initialize() {
  EnsurePrimaryAccountAllowedForProfile(
      profile_, signin_metrics::ProfileSignout::kSigninNotAllowedOnProfileInit);

  signin_allowed_.Init(
      prefs::kSigninAllowed, profile_->GetPrefs(),
      base::BindRepeating(&AccountsPolicyManager::OnSigninAllowedPrefChanged,
                          weak_pointer_factory_.GetWeakPtr()));

  local_state_pref_registrar_.Init(g_browser_process->local_state());
  local_state_pref_registrar_.Add(
      prefs::kGoogleServicesUsernamePattern,
      base::BindRepeating(
          &AccountsPolicyManager::OnGoogleServicesUsernamePatternChanged,
          weak_pointer_factory_.GetWeakPtr()));

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile_);
  identity_manager_observation_.Observe(identity_manager);
  profile_pref_change_registrar_.Init(profile_->GetPrefs());
  profile_pref_change_registrar_.Add(
      prefs::kProfileSeparationDomainExceptionList,
      base::BindRepeating(&AccountsPolicyManager::RemoveUnallowedAccounts,
                          weak_pointer_factory_.GetWeakPtr()));
  if (identity_manager->AreRefreshTokensLoaded()) {
    OnRefreshTokensLoaded();
  }
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
}

void AccountsPolicyManager::Shutdown() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  profile_pref_change_registrar_.RemoveAll();
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  local_state_pref_registrar_.RemoveAll();
  signin_allowed_.Destroy();
}

void AccountsPolicyManager::OnGoogleServicesUsernamePatternChanged() {
  EnsurePrimaryAccountAllowedForProfile(
      profile_,
      signin_metrics::ProfileSignout::kGoogleServiceNamePatternChanged);
}

void AccountsPolicyManager::OnSigninAllowedPrefChanged() {
  EnsurePrimaryAccountAllowedForProfile(
      profile_, signin_metrics::ProfileSignout::kPrefChanged);
}

void AccountsPolicyManager::EnsurePrimaryAccountAllowedForProfile(
    Profile* profile,
    signin_metrics::ProfileSignout clear_primary_account_source) {
// All primary accounts are allowed on ChromeOS, so this method is a no-op on
// ChromeOS.
#if !BUILDFLAG(IS_CHROMEOS)
  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile);
  if (!identity_manager->HasPrimaryAccount(signin::ConsentLevel::kSync)) {
    return;
  }

  CoreAccountInfo primary_account =
      identity_manager->GetPrimaryAccountInfo(signin::ConsentLevel::kSync);
  if (profile->GetPrefs()->GetBoolean(prefs::kSigninAllowed) &&
      signin::IsUsernameAllowedByPatternFromPrefs(
          g_browser_process->local_state(), primary_account.email)) {
    return;
  }

  if (ChromeSigninClientFactory::GetForProfile(profile)
          ->IsClearPrimaryAccountAllowed(identity_manager->HasPrimaryAccount(
              signin::ConsentLevel::kSync))) {
    // Force clear the primary account if it is no longer allowed and if sign
    // out is allowed.
    auto* primary_account_mutator =
        identity_manager->GetPrimaryAccountMutator();
    primary_account_mutator->ClearPrimaryAccount(clear_primary_account_source);
  } else {
#if defined(TOOLKIT_VIEWS)
    // Force remove the profile if sign out is not allowed and if the
    // primary account is no longer allowed.
    // This may be called while the profile is initializing, so it must be
    // scheduled for later to allow the profile initialization to complete.
    CHECK(profiles::IsMultipleProfilesEnabled());
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&AccountsPolicyManager::ShowDeleteProfileDialog,
                       weak_pointer_factory_.GetWeakPtr(), profile,
                       primary_account.email));
#elif BUILDFLAG(IS_ANDROID)
    // The CHECK below was disabled on Android as test
    // HistoryActivityTest#testSupervisedUser signs out a supervised account.
    // We believe this state is not expected on Android as supervised users
    // are not allowed to sign out.
    // See https://crbug.com/1285271#c7 for more info.
    //
    // TODO(crbug.com/40220593): Understand if this test covers a valid usecase
    // and see how this should be handled on Android.
    LOG(WARNING) << "Unexpected state: User is signed in, signin is not "
                    "allowed, sign out is not allowed. Do nothing.";
#else
    NOTREACHED() << "Deleting profiles is not supported.";
#endif  // defined(TOOLKIT_VIEWS)
  }
#endif  // !BUILDFLAG(IS_CHROMEOS)
}

#if defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_CHROMEOS)
// Shows the delete profile dialog on the first browser active window.
void AccountsPolicyManager::ShowDeleteProfileDialog(Profile* profile,
                                                    const std::string& email) {
  if (delete_profile_dialog_manager_) {
    return;
  }

  delete_profile_dialog_manager_ =
      std::make_unique<DeleteProfileDialogManager>(email, this);
  delete_profile_dialog_manager_->PresentDialogOnAllBrowserWindows(
      profile, hide_ui_for_testing_);
}

void AccountsPolicyManager::OnUserConfirmedProfileDeletion(
    DeleteProfileDialogManager* dialog_manager,
    base::FilePath profile_path) {
  DCHECK_EQ(delete_profile_dialog_manager_.get(), dialog_manager);
  delete_profile_dialog_manager_.reset();

  DCHECK(profiles::IsMultipleProfilesEnabled());

  g_browser_process->profile_manager()
      ->GetDeleteProfileHelper()
      .MaybeScheduleProfileForDeletion(
          profile_path,
          hide_ui_for_testing_
              ? base::DoNothing()
              : base::BindOnce(&webui::OpenNewWindowForProfile),
          ProfileMetrics::DELETE_PROFILE_PRIMARY_ACCOUNT_NOT_ALLOWED);
}
#endif  // defined(TOOLKIT_VIEWS) && !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
void AccountsPolicyManager::OnRefreshTokensLoaded() {
  RemoveUnallowedAccounts();
  identity_manager_observation_.Reset();
}

void AccountsPolicyManager::RemoveUnallowedAccounts() {
  if (!base::FeatureList::IsEnabled(
          policy::features::kProfileSeparationDomainExceptionListRetroactive)) {
    return;
  }

  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile_);
  if (!identity_manager->AreRefreshTokensLoaded()) {
    return;
  }
  auto primary_account_id =
      identity_manager->GetPrimaryAccountId(signin::ConsentLevel::kSignin);
  std::vector<AccountInfo> accounts =
      identity_manager->GetExtendedAccountInfoForAccountsWithRefreshToken();
  auto* accounts_mutator = identity_manager->GetAccountsMutator();
  for (const auto& account : accounts) {
    if (!signin_util::IsAccountExemptedFromEnterpriseProfileSeparation(
            profile_, account.email) &&
        account.account_id != primary_account_id) {
      accounts_mutator->RemoveAccount(
          account.account_id,
          signin_metrics::SourceForRefreshTokenOperation::
              kEnterprisePolicy_AccountNotAllowedInContentArea);
    }
  }
}
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
