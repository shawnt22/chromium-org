// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/policy/model/policy_watcher_browser_agent.h"

#import <Foundation/Foundation.h>

#import "base/apple/backup_util.h"
#import "base/apple/foundation_util.h"
#import "base/barrier_closure.h"
#import "base/functional/callback_helpers.h"
#import "base/metrics/histogram_functions.h"
#import "base/path_service.h"
#import "base/run_loop.h"
#import "base/task/sequenced_task_runner.h"
#import "base/task/thread_pool.h"
#import "components/policy/core/common/policy_pref_names.h"
#import "components/prefs/pref_change_registrar.h"
#import "components/prefs/pref_service.h"
#import "components/signin/public/base/signin_pref_names.h"
#import "components/sync/base/pref_names.h"
#import "ios/chrome/app/application_delegate/app_state.h"
#import "ios/chrome/app/profile/profile_state.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin/signin_utils.h"
#import "ios/chrome/browser/policy/model/policy_watcher_browser_agent_observer.h"
#import "ios/chrome/browser/shared/coordinator/scene/scene_state.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/public/commands/policy_change_commands.h"
#import "ios/chrome/browser/signin/model/authentication_service_factory.h"
#import "ios/web/public/thread/web_task_traits.h"

PolicyWatcherBrowserAgent::PolicyWatcherBrowserAgent(Browser* browser)
    : BrowserUserData(browser) {
  DCHECK(!browser->GetProfile()->IsOffTheRecord());
  prefs_change_observer_.Init(GetApplicationContext()->GetLocalState());
  profile_prefs_change_observer_.Init(browser->GetProfile()->GetPrefs());
}

PolicyWatcherBrowserAgent::~PolicyWatcherBrowserAgent() = default;

void PolicyWatcherBrowserAgent::SignInUIDismissed() {
  // Do nothing if the sign out is still in progress.
  if (sign_out_in_progress_) {
    return;
  }

  [handler_ showForceSignedOutPrompt];
}

void PolicyWatcherBrowserAgent::Initialize(id<PolicyChangeCommands> handler) {
  DCHECK(!handler_);
  DCHECK(handler);
  handler_ = handler;

  auth_service_ =
      AuthenticationServiceFactory::GetForProfile(browser_->GetProfile());
  DCHECK(auth_service_);
  auth_service_observation_.Observe(auth_service_.get());

  // BrowserSignin policy: start observing the kSigninAllowed pref. When the
  // pref becomes false, send a UI command to sign the user out. This requires
  // the given command dispatcher to be fully configured.
  prefs_change_observer_.Add(
      prefs::kBrowserSigninPolicy,
      base::BindRepeating(
          &PolicyWatcherBrowserAgent::ForceSignOutIfSigninDisabled,
          base::Unretained(this)));

  // Try to sign out in case the policy changed since last time. This should be
  // done after the handler is set to make sure the UI can be displayed.
  ForceSignOutIfSigninDisabled();

  // TODO(crbug.com/40265119): Instead of directly accessing internal sync
  // prefs, go through proper APIs (SyncService/SyncUserSettings).
  profile_prefs_change_observer_.Add(
      syncer::prefs::internal::kSyncManaged,
      base::BindRepeating(
          &PolicyWatcherBrowserAgent::ShowSyncDisabledPromptIfNeeded,
          base::Unretained(this)));

  // Try to show the alert in case the policy changed since last time.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PolicyWatcherBrowserAgent::ShowSyncDisabledPromptIfNeeded,
                     weak_factory_.GetWeakPtr()));

  profile_prefs_change_observer_.Add(
      prefs::kAllowChromeDataInBackups,
      base::BindRepeating(
          &PolicyWatcherBrowserAgent::UpdateAppContainerBackupExclusion,
          base::Unretained(this)));

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &PolicyWatcherBrowserAgent::UpdateAppContainerBackupExclusion,
          weak_factory_.GetWeakPtr()));
}

void PolicyWatcherBrowserAgent::ForceSignOutIfSigninDisabled() {
  DCHECK(handler_);
  DCHECK(auth_service_);
  if ((auth_service_->GetServiceStatus() ==
       AuthenticationService::ServiceStatus::SigninDisabledByPolicy)) {
    for (auto& observer : observers_) {
      observer.OnSignInDisallowed(this);
    }
    if (auth_service_->HasPrimaryIdentity(signin::ConsentLevel::kSignin)) {
      sign_out_in_progress_ = true;
      base::UmaHistogramBoolean("Enterprise.BrowserSigninIOS.SignedOutByPolicy",
                                true);
      signin::ProfileSignoutRequest(
          signin_metrics::ProfileSignout::kPrefChanged)
          .SetShouldRecordMetrics(false)
          .Run(browser_);
    }
  }
}

void PolicyWatcherBrowserAgent::ShowSyncDisabledPromptIfNeeded() {
  auto* profile_prefs = browser_->GetProfile()->GetPrefs();
  BOOL syncDisabledAlertShown =
      profile_prefs->GetBoolean(policy::policy_prefs::kSyncDisabledAlertShown);
  // TODO(crbug.com/40265119): Instead of directly accessing internal sync
  // prefs, go through proper APIs (SyncService/SyncUserSettings).
  BOOL isSyncDisabledByAdministrator =
      profile_prefs->GetBoolean(syncer::prefs::internal::kSyncManaged);

  if (!syncDisabledAlertShown && isSyncDisabledByAdministrator) {
    SceneState* scene_state = browser_->GetSceneState();
    BOOL scene_is_active =
        scene_state.activationLevel >= SceneActivationLevelForegroundActive;
    if (scene_is_active) {
      [handler_ showSyncDisabledPrompt];
      // Will never trigger again unless policy changes.
      profile_prefs->SetBoolean(policy::policy_prefs::kSyncDisabledAlertShown,
                                true);
    }
  } else if (syncDisabledAlertShown && !isSyncDisabledByAdministrator) {
    // Will trigger again, if policy is turned back on.
    profile_prefs->SetBoolean(policy::policy_prefs::kSyncDisabledAlertShown,
                              false);
  }
}

void PolicyWatcherBrowserAgent::UpdateAppContainerBackupExclusion() {
  bool backup_allowed = browser_->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kAllowChromeDataInBackups);
  // TODO(crbug.com/40826035): If multiple profiles are supported on iOS, update
  // this logic to work with multiple profiles having possibly-possibly
  // conflicting preference values.
  base::FilePath storage_dir = base::apple::GetUserLibraryPath();
  if (backup_allowed) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(base::IgnoreResult(&base::apple::ClearBackupExclusion),
                       std::move(storage_dir)));
  } else {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(base::IgnoreResult(&base::apple::SetBackupExclusion),
                       std::move(storage_dir)));
  }
}

void PolicyWatcherBrowserAgent::AddObserver(
    PolicyWatcherBrowserAgentObserver* observer) {
  observers_.AddObserver(observer);
}

void PolicyWatcherBrowserAgent::RemoveObserver(
    PolicyWatcherBrowserAgentObserver* observer) {
  observers_.RemoveObserver(observer);
}

void PolicyWatcherBrowserAgent::OnSignOutComplete() {
  SceneState* scene_state = browser_->GetSceneState();
  sign_out_in_progress_ = false;
  BOOL scene_is_active =
      scene_state.activationLevel >= SceneActivationLevelForegroundActive;
  if (scene_is_active) {
    // Try to show the signout prompt in all cases: if there is a sign
    // in in progress, the UI will prevent the prompt from showing.
    [handler_ showForceSignedOutPrompt];
  } else {
    scene_state.profileState.shouldShowForceSignOutPrompt = YES;
  }
}

void PolicyWatcherBrowserAgent::OnPrimaryAccountRestricted() {
  [handler_ showRestrictAccountSignedOutPrompt];
}
