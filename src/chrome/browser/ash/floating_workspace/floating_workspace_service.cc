// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/floating_workspace/floating_workspace_service.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/desk_template.h"
#include "ash/public/cpp/session/session_controller.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/system/tray/system_tray_notifier.h"
#include "ash/webui/settings/public/constants/routes.mojom-forward.h"
#include "ash/wm/desks/desk.h"
#include "ash/wm/desks/templates/saved_desk_metrics_util.h"
#include "ash/wm/desks/templates/saved_desk_util.h"
#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ash/floating_sso/floating_sso_service.h"
#include "chrome/browser/ash/floating_sso/floating_sso_service_factory.h"
#include "chrome/browser/ash/floating_workspace/floating_workspace_metrics_util.h"
#include "chrome/browser/ash/floating_workspace/floating_workspace_util.h"
#include "chrome/browser/ash/login/session/user_session_manager.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sync/desk_sync_service_factory.h"
#include "chrome/browser/sync/device_info_sync_service_factory.h"
#include "chrome/browser/sync/session_sync_service_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/ash/desks/desks_client.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/browser/ui/webui/ash/floating_workspace/floating_workspace_dialog.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/ash/components/network/network_handler.h"
#include "chromeos/ash/components/network/network_state_handler.h"
#include "components/app_constants/constants.h"
#include "components/desks_storage/core/desk_model.h"
#include "components/desks_storage/core/desk_sync_bridge.h"
#include "components/desks_storage/core/desk_sync_service.h"
#include "components/session_manager/core/session_manager.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/service/sync_service_utils.h"
#include "components/sync/service/sync_user_settings.h"
#include "components/sync_device_info/device_info.h"
#include "components/sync_device_info/device_info_sync_service.h"
#include "components/sync_device_info/local_device_info_provider.h"
#include "components/sync_sessions/open_tabs_ui_delegate.h"
#include "components/sync_sessions/session_sync_service.h"
#include "components/sync_sessions/synced_session.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/user_activity/user_activity_detector.h"
#include "ui/chromeos/devicetype_utils.h"

namespace {

bool IsFloatingSsoEnabled(Profile* profile) {
  if (!ash::features::IsFloatingSsoAllowed()) {
    return false;
  }
  ash::floating_sso::FloatingSsoService* floating_sso_service =
      ash::floating_sso::FloatingSsoServiceFactory::GetForProfile(profile);
  if (!floating_sso_service) {
    return false;
  }
  return floating_sso_service->IsFloatingSsoEnabled();
}
}  // namespace

namespace ash {

// Default time without activity after which a floating workspace template is
// considered stale and becomes a candidate for garbage collection.
constexpr base::TimeDelta kStaleFWSThreshold = base::Days(30);
// Minimum time to wait before we decide to show the progress status if no
// floating workspace templates have been downloaded yet.
constexpr base::TimeDelta kMinTimeToWait = base::Seconds(2);

FloatingWorkspaceService::FloatingWorkspaceService(
    Profile* profile,
    floating_workspace_util::FloatingWorkspaceVersion version)
    : profile_(profile),
      version_(version),
      initialization_timeticks_(base::TimeTicks::Now()),
      initialization_time_(base::Time::Now()) {}

FloatingWorkspaceService::~FloatingWorkspaceService() {
  StopCaptureAndUploadActiveDesk();
  ShutDownServicesAndObservers();
  if (ash::SessionController::Get()) {
    ash::SessionController::Get()->RemoveObserver(this);
  }
}

void FloatingWorkspaceService::OnSyncShutdown(syncer::SyncService* sync) {
  if (sync_service_ && sync_service_->HasObserver(this)) {
    sync_service_->RemoveObserver(this);
  }
  sync_service_ = nullptr;
}

void FloatingWorkspaceService::OnShuttingDown() {
  if (ash::NetworkHandler::IsInitialized()) {
    auto* network_handler = NetworkHandler::Get();
    if (network_handler->network_state_handler()->HasObserver(this)) {
      network_handler->network_state_handler()->RemoveObserver(this);
    }
  }
}

void FloatingWorkspaceService::OnDeviceInfoShutdown() {
  if (device_info_sync_service_ &&
      device_info_sync_service_->GetDeviceInfoTracker()) {
    device_info_sync_service_->GetDeviceInfoTracker()->RemoveObserver(this);
  }
  device_info_sync_service_ = nullptr;
}

void FloatingWorkspaceService::MaybeShowNetworkScreen() {
  if (!should_run_restore_) {
    return;
  }
  if (floating_workspace_util::IsInternetConnected()) {
    return;
  }
  FloatingWorkspaceDialog::ShowNetworkScreen();
}

void FloatingWorkspaceService::ScheduleShowingNetworkScreen() {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&FloatingWorkspaceService::MaybeShowNetworkScreen,
                     weak_pointer_factory_.GetWeakPtr()),
      kFwsNetworkScreenDelay);
}

void FloatingWorkspaceService::InitiateSigninTask() {
  syncer::LocalDeviceInfoProvider* local_device_info_provider =
      (device_info_sync_service_->GetLocalDeviceInfoProvider());
  if (!local_device_info_provider->GetLocalDeviceInfo()) {
    local_device_info_ready_subscription_ =
        local_device_info_provider->RegisterOnInitializedCallback(
            base::BindRepeating(
                &FloatingWorkspaceService::OnLocalDeviceInfoProviderReady,
                weak_pointer_factory_.GetWeakPtr()));
  } else {
    UpdateLocalDeviceInfo();
  }

  if (should_run_restore_) {
    // It is possible that all relevant Sync state changes happened before
    // this method was called (e.g. it often happens in the wake-up flow while
    // we are on the lock screen), so we trigger `OnStateChanged` here manually
    // to make sure that we process current Sync states at least once and update
    // the UI if needed. Otherwise we would wait for the next Sync update even
    // though all needed data is already available.
    OnStateChanged(sync_service_);
  }
}

// TODO(b/309137462): Clean up params to not need to be passed in.
void FloatingWorkspaceService::Init(
    syncer::SyncService* sync_service,
    desks_storage::DeskSyncService* desk_sync_service,
    syncer::DeviceInfoSyncService* device_info_sync_service) {
  if (ash::SessionController::Get()) {
    ash::SessionController::Get()->AddObserver(this);
  }

  if (version_ == floating_workspace_util::FloatingWorkspaceVersion::
                      kFloatingWorkspaceV1Enabled) {
    InitForV1();
    return;
  }

  if (version_ == floating_workspace_util::FloatingWorkspaceVersion::
                      kFloatingWorkspaceV2Enabled &&
      floating_workspace_util::IsFloatingWorkspaceV2Enabled()) {
    InitForV2(sync_service, desk_sync_service, device_info_sync_service);
    return;
  }

  if (version_ ==
      floating_workspace_util::FloatingWorkspaceVersion::kAutoSignoutOnly) {
    should_run_restore_ = false;
    // TODO(crbug.com/419508619): fix naming (now we call `InitForV2` in the
    // code path where the `version_` is not `kFloatingWorkspaceV2Enabled`).
    InitForV2(sync_service, desk_sync_service, device_info_sync_service);
  }
}

void FloatingWorkspaceService::SubscribeToForeignSessionUpdates() {
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_);
  // If sync is disabled no need to observe anything.
  if (!sync_service || !sync_service->IsSyncFeatureEnabled()) {
    return;
  }
  foreign_session_updated_subscription_ =
      session_sync_service_->SubscribeToForeignSessionsChanged(
          base::BindRepeating(
              &FloatingWorkspaceService::
                  RestoreBrowserWindowsFromMostRecentlyUsedDevice,
              weak_pointer_factory_.GetWeakPtr()));
}

void FloatingWorkspaceService::
    RestoreBrowserWindowsFromMostRecentlyUsedDevice() {
  if (!should_run_restore_)
    return;
  if (base::TimeTicks::Now() >
      initialization_timeticks_ +
          ash::features::kFloatingWorkspaceMaxTimeAvailableForRestoreAfterLogin
              .Get()) {
    // No need to restore any remote session 3 seconds (TBD) after login.
    StopRestoringSession();
    return;
  }
  const sync_sessions::SyncedSession* most_recently_used_remote_session =
      GetMostRecentlyUsedRemoteSession();
  const sync_sessions::SyncedSession* local_session = GetLocalSession();
  if (!most_recently_used_remote_session ||
      (local_session &&
       local_session->GetModifiedTime() >
           most_recently_used_remote_session->GetModifiedTime())) {
    // If local session is the most recently modified or no remote session,
    // dispatch a delayed task to check whether any foreign session got updated.
    // If remote session is not updated after the delay, launch local session.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            &FloatingWorkspaceService::TryRestoreMostRecentlyUsedSession,
            weak_pointer_factory_.GetWeakPtr()),
        ash::features::kFloatingWorkspaceMaxTimeAvailableForRestoreAfterLogin
            .Get());
    StopRestoringSession();
    return;
  }

  // Restore most recently used remote session.
  RestoreForeignSessionWindows(most_recently_used_remote_session);
  StopRestoringSession();
}

void FloatingWorkspaceService::TryRestoreMostRecentlyUsedSession() {
  // A task generated by RestoreBrowserWindowsFromMostRecentlyUsedDevice
  // will call this method with a delay, at this time if local session is
  // still more recent, restore the local session.
  const sync_sessions::SyncedSession* local_session = GetLocalSession();
  const sync_sessions::SyncedSession* most_recently_used_remote_session =
      GetMostRecentlyUsedRemoteSession();
  if (local_session) {
    if (!most_recently_used_remote_session ||
        local_session->GetModifiedTime() >
            most_recently_used_remote_session->GetModifiedTime()) {
      // This is a delayed task, if at this time local session is still
      // most recent, restore local session.
      RestoreLocalSessionWindows();
    } else {
      RestoreForeignSessionWindows(most_recently_used_remote_session);
    }
  } else if (most_recently_used_remote_session) {
    RestoreForeignSessionWindows(most_recently_used_remote_session);
  }
}

void FloatingWorkspaceService::OnStateChanged(syncer::SyncService* sync) {
  MaybeStartOrStopCaptureBasedOnTabSyncSetting();
  UpdateUiStateIfNeeded();
  // Prematurely return when sync feature is not active.
  if (!sync_service_->IsSyncFeatureActive()) {
    return;
  }
  if (!should_run_restore_) {
    MaybeSignOutOfCurrentSession();
    return;
  }
  syncer::UploadState workspace_upload_state = syncer::GetUploadToGoogleState(
      sync_service_, syncer::DataType::WORKSPACE_DESK);
  if (workspace_upload_state == syncer::UploadState::NOT_ACTIVE) {
    // This state indicates that we are not permitted to upload user's workspace
    // data (see the comment above syncer::UploadState::NOT_ACTIVE for details).
    // We should treat this as if the feature is fully disabled.
    StopRestoringSession();
    return;
  }
  syncer::SyncService::DataTypeDownloadStatus workspace_download_status =
      sync_service_->GetDownloadStatusFor(syncer::DataType::WORKSPACE_DESK);
  switch (workspace_download_status) {
    case syncer::SyncService::DataTypeDownloadStatus::kWaitingForUpdates: {
      // Floating Workspace Service needs to wait until workspace desks are
      // up to date.
      break;
    }
    case syncer::SyncService::DataTypeDownloadStatus::kUpToDate: {
      if (workspace_upload_state != syncer::UploadState::ACTIVE) {
        // Download state can be kUpToDate when offline, but upload status will
        // only be active once we are connected to server and completed a sync
        // cycle. We shouldn't restore anything until then.
        break;
      }
      if (!first_sync_data_downloaded_timeticks_.has_value()) {
        first_sync_data_downloaded_timeticks_ = base::TimeTicks::Now();
      }
      if (ShouldWaitForCookies()) {
        // We can hit this code path repeatedly while waiting for cookies to be
        // up to date. `ShouldWaitForCookies()` call is expected to schedule a
        // call to `LaunchWhenAppCacheIsReady` which should be run once cookies
        // are ready. This will result in `should_run_restore_` being set to
        // `false`, which will enable an early return from `OnStateChanged`.
        // In practice, cookies and desks usually become up to date at the same
        // time.
        break;
      }
      LaunchWhenAppCacheIsReady();
      break;
    }
    case syncer::SyncService::DataTypeDownloadStatus::kError: {
      // Nothing to do here: error UI is shown from `UpdateUiStateIfNeeded()`.
      break;
    }
  }
}

bool FloatingWorkspaceService::ShouldWaitForCookies() {
  if (!IsFloatingSsoEnabled(profile_)) {
    return false;
  }
  syncer::SyncService::DataTypeDownloadStatus cookies_download_status =
      sync_service_->GetDownloadStatusFor(syncer::DataType::COOKIES);
  switch (cookies_download_status) {
    case syncer::SyncService::DataTypeDownloadStatus::kWaitingForUpdates: {
      return true;
    }
    case syncer::SyncService::DataTypeDownloadStatus::kUpToDate: {
      syncer::UploadState cookies_upload_state = syncer::GetUploadToGoogleState(
          sync_service_, syncer::DataType::COOKIES);
      if (cookies_upload_state != syncer::UploadState::ACTIVE) {
        // Download state can be kUpToDate when offline, but upload status will
        // only be active once we are connected to server and completed a sync
        // cycle. We shouldn't restore anything until then.
        return true;
      }
      ash::floating_sso::FloatingSsoService* floating_sso_service =
          ash::floating_sso::FloatingSsoServiceFactory::GetForProfile(profile_);
      // Even when Sync status is "up to date", cookies might still be in the
      // process of being applied to the cookie jar in the browser. Schedule a
      // callback to restore the workspace once it's done. This call is cheap
      // and it's ok to execute it multiple times.
      floating_sso_service->RunWhenCookiesAreReady(
          base::BindOnce(&FloatingWorkspaceService::LaunchWhenAppCacheIsReady,
                         weak_pointer_factory_.GetWeakPtr()));
      return true;
    }
    case syncer::SyncService::DataTypeDownloadStatus::kError: {
      // TODO(crbug.com/377327839): add error handling for cookies.
      return false;
    }
  }
}

void FloatingWorkspaceService::LaunchWhenAppCacheIsReady() {
  if (!is_cache_ready_) {
    should_launch_on_ready_ = true;
    VLOG(1) << "App cache is not ready. Don't restore floating "
               "workspace yet.";
    return;
  }
  StopProgressBarAndRestoreFloatingWorkspace();
}

void FloatingWorkspaceService::DefaultNetworkChanged(
    const NetworkState* network) {
  UpdateUiStateIfNeeded();
}

void FloatingWorkspaceService::NetworkConnectionStateChanged(
    const NetworkState* network) {
  UpdateUiStateIfNeeded();
}

void FloatingWorkspaceService::UpdateUiStateIfNeeded() {
  if (!should_run_restore_) {
    // If the restore should not run, then there's no need to show any UI and it
    // is expected to be closed elsewhere.
    return;
  }

  if (!session_manager::SessionManager::Get()
           ->IsUserSessionStartUpTaskCompleted()) {
    // Our UI should only be shown once user sessions has properly started. The
    // service might still be active on the lock screen or during transition
    // from the login screen to user session, so we must check for it
    // explicitly.
    return;
  }

  if (!floating_workspace_util::IsInternetConnected()) {
    // When the user just signed in there might be no internet access, because
    // the device didn't have enough time to connect. In this case we show
    // Default screen before maybe showing the network screen.
    if (!FloatingWorkspaceDialog::IsShown()) {
      FloatingWorkspaceDialog::ShowDefaultScreen();
    }
    // If the dialog already exists showing it again will focus on it. This
    // behaviour is undesirable for captive portal, since it shows a dialog on
    // top.
    if (FloatingWorkspaceDialog::IsShown() !=
        FloatingWorkspaceDialog::State::kNetwork) {
      ScheduleShowingNetworkScreen();
    }
    return;
  }
  if (!sync_service_) {
    FloatingWorkspaceDialog::ShowErrorScreen();
    return;
  }
  if (!sync_service_->IsSyncFeatureActive()) {
    FloatingWorkspaceDialog::ShowErrorScreen();
    return;
  }
  syncer::SyncService::DataTypeDownloadStatus workspace_download_status =
      sync_service_->GetDownloadStatusFor(syncer::DataType::WORKSPACE_DESK);
  if (workspace_download_status ==
      syncer::SyncService::DataTypeDownloadStatus::kError) {
    FloatingWorkspaceDialog::ShowErrorScreen();
    return;
  }

  // We are online and Sync is active: show the default UI state.
  FloatingWorkspaceDialog::ShowDefaultScreen();
}

void FloatingWorkspaceService::SuspendImminent(
    power_manager::SuspendImminent::Reason reason) {
  timestamp_before_suspend_ = base::Time::Now();
}

void FloatingWorkspaceService::SuspendDone(base::TimeDelta sleep_duration) {
  restore_upon_wake_ = true;
  // Setting initialization time here is important to avoid unintended
  // automatic sign-out when device wakes up on the lock screen.
  initialization_time_ = base::Time::Now();
  initialization_timeticks_ = base::TimeTicks::Now();
}

void FloatingWorkspaceService::OnDeviceInfoChange() {}

void FloatingWorkspaceService::InitForV1() {
  session_sync_service_ =
      SessionSyncServiceFactory::GetInstance()->GetForProfile(profile_);
}

void FloatingWorkspaceService::InitForV2(
    syncer::SyncService* sync_service,
    desks_storage::DeskSyncService* desk_sync_service,
    syncer::DeviceInfoSyncService* device_info_sync_service) {
  // Disable floating workspace action in safe mode.
  if (floating_workspace_util::IsSafeMode()) {
    LOG(WARNING) << "Floating workspace disabled in safe mode.";
    // TODO(crbug.com/411121762): decide if we want to display something to the
    // user in this case with our new startup UI.
    return;
  }
  floating_workspace_metrics_util::
      RecordFloatingWorkspaceV2InitializedHistogram();
  SetUpServiceAndObservers(sync_service, desk_sync_service,
                           device_info_sync_service);
  InitiateSigninTask();
}

const sync_sessions::SyncedSession*
FloatingWorkspaceService::GetMostRecentlyUsedRemoteSession() {
  sync_sessions::OpenTabsUIDelegate* open_tabs = GetOpenTabsUIDelegate();
  std::vector<raw_ptr<const sync_sessions::SyncedSession, VectorExperimental>>
      remote_sessions;
  if (!open_tabs || !open_tabs->GetAllForeignSessions(&remote_sessions)) {
    return nullptr;
  }
  // GetAllForeignSessions returns remote sessions in sorted way
  // with most recent at first.
  return remote_sessions.front();
}

const sync_sessions::SyncedSession*
FloatingWorkspaceService::GetLocalSession() {
  sync_sessions::OpenTabsUIDelegate* open_tabs = GetOpenTabsUIDelegate();
  const sync_sessions::SyncedSession* local_session = nullptr;
  if (!open_tabs || !open_tabs->GetLocalSession(&local_session))
    return nullptr;
  return local_session;
}

void FloatingWorkspaceService::RestoreForeignSessionWindows(
    const sync_sessions::SyncedSession* session) {
  sync_sessions::OpenTabsUIDelegate* open_tabs = GetOpenTabsUIDelegate();
  if (!open_tabs) {
    return;
  }
  std::vector<const sessions::SessionWindow*> session_windows =
      open_tabs->GetForeignSession(session->GetSessionTag());
  if (session_windows.empty()) {
    return;
  }
  SessionRestore::RestoreForeignSessionWindows(
      profile_, session_windows.begin(), session_windows.end());
}

void FloatingWorkspaceService::RestoreLocalSessionWindows() {
  // Restore local session based on user settings in
  // chrome://settings/onStartup.
  UserSessionManager::GetInstance()->LaunchBrowser(profile_);
}

sync_sessions::OpenTabsUIDelegate*
FloatingWorkspaceService::GetOpenTabsUIDelegate() {
  DCHECK(session_sync_service_);
  return session_sync_service_->GetOpenTabsUIDelegate();
}

void FloatingWorkspaceService::StartCaptureAndUploadActiveDesk() {
  if (!tab_sync_enabled_) {
    return;
  }
  CaptureAndUploadActiveDesk();
  if (!timer_.IsRunning()) {
    timer_.Start(
        FROM_HERE,
        ash::features::kFloatingWorkspaceV2PeriodicJobIntervalInSeconds.Get(),
        this, &FloatingWorkspaceService::CaptureAndUploadActiveDesk);
  }
}

void FloatingWorkspaceService::StopCaptureAndUploadActiveDesk() {
  if (timer_.IsRunning()) {
    timer_.Stop();
  }
}

bool FloatingWorkspaceService::ShouldExcludeTemplate(
    const DeskTemplate* floating_workspace_template) {
  if (!floating_workspace_template) {
    return true;
  }
  // We only consider remote entries if the device has woken up from suspend and
  // we are going through a restore flow. In this case, we only want to consider
  // restoring floating workspaces from other devices and if those templates
  // were after the device went to suspend mode. `timestamp_before_suspend_` is
  // only set if the user has gone to suspend, otherwise it is a nullopt.
  // Therefore, if it is set and we're getting entries for restore, we only want
  // to consider remote entries.
  bool should_only_consider_remote_entries =
      timestamp_before_suspend_.has_value();
  if (!should_only_consider_remote_entries) {
    return false;
  }
  bool is_remote_entry = floating_workspace_template->client_cache_guid() !=
                         desk_sync_service_->GetDeskModel()->GetCacheGuid();
  bool is_uploaded_after_suspend =
      floating_workspace_template->GetLastUpdatedTime() >
      timestamp_before_suspend_.value();
  return !(is_remote_entry && is_uploaded_after_suspend);
}

const DeskTemplate*
FloatingWorkspaceService::GetLatestFloatingWorkspaceTemplate() {
  const DeskTemplate* floating_workspace_template = nullptr;
  std::vector<const ash::DeskTemplate*> fws_entries =
      GetFloatingWorkspaceTemplateEntries();
  VLOG(1) << "Found " << fws_entries.size() << " floating workspace entries";
  for (const DeskTemplate* entry : fws_entries) {
    if (ShouldExcludeTemplate(entry)) {
      continue;
    }
    if (!floating_workspace_template ||
        floating_workspace_template->GetLastUpdatedTime() <
            entry->GetLastUpdatedTime()) {
      floating_workspace_template = entry;
    }
  }
  DoGarbageCollection(/*exclude template=*/floating_workspace_template);
  return floating_workspace_template;
}

std::vector<const ash::DeskTemplate*>
FloatingWorkspaceService::GetFloatingWorkspaceTemplateEntries() {
  std::vector<const ash::DeskTemplate*> entries;
  if (!desk_sync_service_ || !desk_sync_service_->GetDeskModel()) {
    return entries;
  }
  desks_storage::DeskModel::GetAllEntriesResult result =
      desk_sync_service_->GetDeskModel()->GetAllEntries();
  if (result.status != desks_storage::DeskModel::GetAllEntriesStatus::kOk) {
    return entries;
  }
  for (const DeskTemplate* desk_template : result.entries) {
    if (desk_template &&
        desk_template->type() == DeskTemplateType::kFloatingWorkspace) {
      entries.push_back(desk_template);
    }
  }
  return entries;
}

void FloatingWorkspaceService::CaptureAndUploadActiveDesk() {
  if (!tab_sync_enabled_) {
    return;
  }
  if (!desk_sync_service_->GetDeskModel()->IsSyncing()) {
    // Even when tab sync is enabled, Sync might be not running or not syncing
    // WORKSPACE_DESK data for some other reasons.
    return;
  }
  if (should_run_restore_) {
    // A safeguard in case the capture was triggered while we are waiting to
    // restore the session.
    return;
  }
  if (version_ ==
      floating_workspace_util::FloatingWorkspaceVersion::kAutoSignoutOnly) {
    return;
  }
  GetDesksClient()->CaptureActiveDesk(
      base::BindOnce(&FloatingWorkspaceService::OnTemplateCaptured,
                     weak_pointer_factory_.GetWeakPtr()),
      DeskTemplateType::kFloatingWorkspace);
}

void FloatingWorkspaceService::CaptureAndUploadActiveDeskForTest(
    std::unique_ptr<DeskTemplate> desk_template) {
  OnTemplateCaptured(std::nullopt, std::move(desk_template));
}

void FloatingWorkspaceService::StopProgressBarAndRestoreFloatingWorkspace() {
  FloatingWorkspaceDialog::Close();
  if (tab_sync_enabled_) {
    RestoreFloatingWorkspaceTemplate(GetLatestFloatingWorkspaceTemplate());
    StartCaptureAndUploadActiveDesk();
  }
}

void FloatingWorkspaceService::RestoreFloatingWorkspaceTemplate(
    const DeskTemplate* desk_template) {
  if (desk_template == nullptr) {
    LOG(WARNING)
        << "No floating workspace entry found. Won't "
           "restore. This is only possible if this is the first time "
           "a user is using Floating Workspace or we are attempting to restore "
           "from a suspend mode and there are no remote entries to restore.";
    StopRestoringSession();
    floating_workspace_metrics_util::
        RecordFloatingWorkspaceV2TemplateNotFound();
    return;
  }
  // Record metrics for window and tab count and also the time it took to
  // download the floating workspace template.
  floating_workspace_metrics_util::RecordFloatingWorkspaceV2TemplateLoadTime(
      base::TimeTicks::Now() - initialization_timeticks_);
  RecordWindowAndTabCountHistogram(*desk_template);
  LaunchFloatingWorkspaceTemplate(desk_template);
}

void FloatingWorkspaceService::LaunchFloatingWorkspaceTemplate(
    const DeskTemplate* desk_template) {
  StopRestoringSession();
  if (desk_template == nullptr) {
    return;
  }
  base::Uuid active_desk_uuid = GetDesksClient()->GetActiveDesk();
  VLOG(1) << "Launching Floating Workspace template with timestamp of "
          << desk_template->GetLastUpdatedTime();
  RemoveAllPreviousDesksExceptActiveDesk(
      /*exclude_desk_uuid=*/active_desk_uuid);

  // Close all windows between waking up from sleep and restore operation.
  // TODO: b/331420684 - Remove apps and windows in place without having to
  // launch a new desk.
  if (launch_on_new_desk_) {
    GetDesksClient()->LaunchDeskTemplate(
        desk_template->uuid(),
        base::BindOnce(&FloatingWorkspaceService::OnTemplateLaunched,
                       weak_pointer_factory_.GetWeakPtr()),
        desk_template->template_name());
    return;
  }
  VLOG(1) << "Combining Floating Workspace apps to current desk.";
  std::unique_ptr<DeskTemplate> template_copy = desk_template->Clone();
  // Open the apps from the floating workspace on top of existing windows.
  saved_desk_util::UpdateTemplateActivationIndicesRelativeOrder(*template_copy);
  GetDesksClient()->LaunchAppsFromTemplate(std::move(template_copy));
  RecordLaunchSavedDeskHistogram(DeskTemplateType::kFloatingWorkspace);
}

void FloatingWorkspaceService::OnTemplateLaunched(
    std::optional<DesksClient::DeskActionError> error,
    const base::Uuid& desk_uuid) {
  if (error) {
    HandleTemplateLaunchErrors(error.value());
    return;
  }
  RecordLaunchSavedDeskHistogram(DeskTemplateType::kFloatingWorkspace);
  RemoveAllPreviousDesksExceptActiveDesk(/*exclude_desk_uuid=*/desk_uuid);
}

DesksClient* FloatingWorkspaceService::GetDesksClient() {
  return DesksClient::Get();
}

bool FloatingWorkspaceService::IsCurrentDeskSameAsPrevious(
    DeskTemplate* current_desk_template) const {
  if (!previously_captured_desk_template_) {
    return false;
  }

  // If the last user activity was before the last uploaded template, then it is
  // very likely that the current captured desk is done due to changing urls for
  // the same window (caused by things like auth protection on gmail app when
  // certs aren't installed).
  if (ui::UserActivityDetector::Get()->last_activity_time() <=
      last_uploaded_timeticks_) {
    return true;
  }

  const auto& previous_app_id_to_app_launch_list =
      previously_captured_desk_template_->desk_restore_data()
          ->app_id_to_launch_list();
  const auto& current_app_id_to_app_launch_list =
      current_desk_template->desk_restore_data()->app_id_to_launch_list();

  // If previous and current template have different number of apps they are
  // different.
  if (previous_app_id_to_app_launch_list.size() !=
      current_app_id_to_app_launch_list.size()) {
    return false;
  }

  for (const auto& it : previous_app_id_to_app_launch_list) {
    const std::string app_id = it.first;
    // Cannot find app id in currently captured desk.
    if (current_app_id_to_app_launch_list.find(app_id) ==
        current_app_id_to_app_launch_list.end()) {
      return false;
    }
    for (const auto& [restore_window_id, previous_app_restore_data] :
         it.second) {
      auto& current_app_restore_data_launch_list =
          current_app_id_to_app_launch_list.at(app_id);
      // Cannot find window id in currently captured template.
      if (current_app_restore_data_launch_list.find(restore_window_id) ==
          current_app_restore_data_launch_list.end()) {
        return false;
      }
      // For the same window the data inside are different.
      if (*current_app_restore_data_launch_list.at(restore_window_id) !=
          *previous_app_restore_data) {
        return false;
      }
    }
  }
  return true;
}

void FloatingWorkspaceService::HandleTemplateCaptureErrors(
    DesksClient::DeskActionError error) {
  switch (error) {
    case DesksClient::DeskActionError::kUnknownError:
      LOG(WARNING) << "Failed to capture template: unknown error.";
      return;
    case DesksClient::DeskActionError::kStorageError:
      LOG(WARNING) << "Failed to capture template: storage error.";
      return;
    case DesksClient::DeskActionError::kDesksCountCheckFailedError:
      LOG(WARNING) << "Failed to capture template: max number of desks open.";
      return;
    case DesksClient::DeskActionError::kNoCurrentUserError:
      LOG(WARNING) << "Failed to capture template: no active user.";
      return;
    case DesksClient::DeskActionError::kBadProfileError:
      LOG(WARNING) << "Failed to capture template: bad profile.";
      return;
    case DesksClient::DeskActionError::kResourceNotFoundError:
      LOG(WARNING) << "Failed to capture template: resource not found.";
      return;
    case DesksClient::DeskActionError::kInvalidIdError:
      LOG(WARNING) << "Failed to capture template: desk id is invalid.";
      return;
    case DesksClient::DeskActionError::kDesksBeingModifiedError:
      LOG(WARNING)
          << "Failed to capture template: desk is currently being modified.";
      return;
  }
}

void FloatingWorkspaceService::HandleTemplateLaunchErrors(
    DesksClient::DeskActionError error) {
  switch (error) {
    case DesksClient::DeskActionError::kUnknownError:
      floating_workspace_metrics_util::
          RecordFloatingWorkspaceV2TemplateLaunchFailureType(
              floating_workspace_metrics_util::LaunchTemplateFailureType::
                  kUnknownError);
      LOG(WARNING) << "Failed to launch template: unknown error.";
      return;
    case DesksClient::DeskActionError::kStorageError:
      floating_workspace_metrics_util::
          RecordFloatingWorkspaceV2TemplateLaunchFailureType(
              floating_workspace_metrics_util::LaunchTemplateFailureType::
                  kStorageError);
      LOG(WARNING) << "Failed to launch template: storage error.";
      return;
    case DesksClient::DeskActionError::kDesksCountCheckFailedError:
      floating_workspace_metrics_util::
          RecordFloatingWorkspaceV2TemplateLaunchFailureType(
              floating_workspace_metrics_util::LaunchTemplateFailureType::
                  kDesksCountCheckFailedError);
      LOG(WARNING) << "Failed to launch template: max number of desks open.";
      return;
    // No need to record metrics for the below desk action errors since they
    // do not relate to template launch.
    case DesksClient::DeskActionError::kNoCurrentUserError:
      LOG(WARNING) << "Failed to launch template: no active user.";
      return;
    case DesksClient::DeskActionError::kBadProfileError:
      LOG(WARNING) << "Failed to launch template: bad profile.";
      return;
    case DesksClient::DeskActionError::kResourceNotFoundError:
      LOG(WARNING) << "Failed to launch template: resource not found.";
      return;
    case DesksClient::DeskActionError::kInvalidIdError:
      LOG(WARNING) << "Failed to launch template: desk id is invalid.";
      return;
    case DesksClient::DeskActionError::kDesksBeingModifiedError:
      LOG(WARNING)
          << "Failed to launch template: desk is currently being modified.";
      return;
  }
}

void FloatingWorkspaceService::OnTemplateCaptured(
    std::optional<DesksClient::DeskActionError> error,
    std::unique_ptr<DeskTemplate> desk_template) {
  // Desk capture was not successful, nothing to upload.
  if (error) {
    HandleTemplateCaptureErrors(error.value());
  }
  if (!desk_template) {
    LOG(WARNING) << "Desk capture failed. Nothing to upload.";
    return;
  }
  // Check if there's an associated floating workspace uuid from the desk
  // sync bridge. If there is, use that one. The
  // `floating_workspace_uuid_ is populated once during the first capture
  // of the session if there is known information from the sync bridge
  // and the info may be outdated for the sync bridge. However, the sync
  // bridge does not need to know the new uuid since the current service
  // will handle it. Ignore for testing.
  if (!floating_workspace_uuid_.has_value()) {
    std::optional<base::Uuid> floating_workspace_uuid_from_desk_model =
        GetFloatingWorkspaceUuidForCurrentDevice();
    if (floating_workspace_uuid_from_desk_model.has_value()) {
      floating_workspace_uuid_ =
          floating_workspace_uuid_from_desk_model.value();
    }
  }
  if (floating_workspace_uuid_.has_value() &&
      floating_workspace_uuid_.value().is_valid()) {
    desk_template->set_uuid(floating_workspace_uuid_.value());
  } else {
    floating_workspace_uuid_ = desk_template->uuid();
  }
  // If it successfully captured desk, remove old entry and record new uuid only
  // if the user was active from when the sync cycle is finished to now.
  if (!IsCurrentDeskSameAsPrevious(desk_template.get()) &&
      (first_sync_data_downloaded_timeticks_.has_value() &&
       first_sync_data_downloaded_timeticks_.value() <=
           ui::UserActivityDetector::Get()->last_activity_time())) {
    UploadFloatingWorkspaceTemplateToDeskModel(std::move(desk_template));
  }
}

void FloatingWorkspaceService::UploadFloatingWorkspaceTemplateToDeskModel(
    std::unique_ptr<DeskTemplate> desk_template) {
  // Upload and save the template.
  auto* active_user = user_manager::UserManager::Get()->GetActiveUser();
  auto* user_profile = ProfileHelper::Get()->GetProfileByUser(active_user);
  // Do not upload if the active user profile doesn't match the logged in user
  // profile.
  if (user_profile != profile_) {
    return;
  }
  desk_sync_service_->GetDeskModel()->AddOrUpdateEntry(
      std::move(desk_template),
      base::BindOnce(&FloatingWorkspaceService::OnTemplateUploaded,
                     weak_pointer_factory_.GetWeakPtr()));
}

void FloatingWorkspaceService::OnTemplateUploaded(
    desks_storage::DeskModel::AddOrUpdateEntryStatus status,
    std::unique_ptr<DeskTemplate> new_entry) {
  previously_captured_desk_template_ = std::move(new_entry);
  last_uploaded_timeticks_ = base::TimeTicks::Now();
  floating_workspace_metrics_util::
      RecordFloatingWorkspaceV2TemplateUploadStatusHistogram(status);
  VLOG(1) << "Desk template uploaded successfully.";
}

std::optional<base::Uuid>
FloatingWorkspaceService::GetFloatingWorkspaceUuidForCurrentDevice() {
  std::string cache_guid = desk_sync_service_->GetDeskModel()->GetCacheGuid();
  std::vector<const ash::DeskTemplate*> fws_entries =
      GetFloatingWorkspaceTemplateEntries();
  for (const DeskTemplate* entry : fws_entries) {
    if (entry && entry->client_cache_guid() == cache_guid) {
      return entry->uuid();
    }
  }
  return std::nullopt;
}

void FloatingWorkspaceService::DoGarbageCollection(
    const DeskTemplate* exclude_template) {
  // Do not delete any floating workspace templates if we have less than 2
  // templates. We want to keep the latest template. If there's only one
  // floating workspace template then this is the latest one.
  std::vector<const DeskTemplate*> fws_entries =
      GetFloatingWorkspaceTemplateEntries();
  if (fws_entries.size() < 2) {
    return;
  }
  for (const DeskTemplate* entry : fws_entries) {
    const base::TimeDelta template_age =
        base::Time::Now() - entry->GetLastUpdatedTime();
    if (template_age < kStaleFWSThreshold ||
        (exclude_template != nullptr &&
         exclude_template->uuid() == entry->uuid())) {
      continue;
    }
    base::Uuid uuid = entry->uuid();
    desk_sync_service_->GetDeskModel()->DeleteEntry(uuid, base::DoNothing());
  }
}

// TODO(b/294456894): Migrate to desk controller logic.
void FloatingWorkspaceService::RemoveAllPreviousDesksExceptActiveDesk(
    const base::Uuid& exclude_desk_uuid) {
  auto all_desks = GetDesksClient()->GetAllDesks();
  if (all_desks.has_value() && all_desks.value().size() > 1) {
    for (const Desk* entry : all_desks.value()) {
      if (entry && entry->uuid() != exclude_desk_uuid) {
        base::Uuid uuid_to_remove = entry->uuid();
        GetDesksClient()->RemoveDesk(uuid_to_remove,
                                     ash::DeskCloseType::kCloseAllWindows);
      }
    }
  }
}

void FloatingWorkspaceService::MaybeSignOutOfCurrentSession() {
  base::TimeDelta time_delta =
      ui::UserActivityDetector::Get()->last_activity_time() -
      initialization_timeticks_;
  if (sync_service_->GetDownloadStatusFor(syncer::DataType::DEVICE_INFO) ==
      syncer::SyncService::DataTypeDownloadStatus::kUpToDate) {
    std::vector<const syncer::DeviceInfo*> all_devices =
        device_info_sync_service_->GetDeviceInfoTracker()->GetAllDeviceInfo();

    // Sort the DeviceInfo vector so the most recently modified devices are
    // first.
    std::sort(
        all_devices.begin(), all_devices.end(),
        [](const syncer::DeviceInfo* device1,
           const syncer::DeviceInfo* device2) {
          return device1->floating_workspace_last_signin_timestamp().value_or(
                     base::Time()) >
                 device2->floating_workspace_last_signin_timestamp().value_or(
                     base::Time());
        });
    // Checks if the most recently modified devices are after this device's last
    // active timestamp.
    for (const syncer::DeviceInfo* device : all_devices) {
      // If the timestamp is older than the current timestamp or the entry is
      // nullopt, then any other devices afterwards are older, so we can stop
      // here.
      if (!device->floating_workspace_last_signin_timestamp().has_value() ||
          device->floating_workspace_last_signin_timestamp().value() <
              initialization_time_ +
                  (time_delta.is_positive() ? time_delta : base::Seconds(0)) +
                  kMinTimeToWait) {
        break;
      }
      // Skip current device info.
      if (device_info_sync_service_->GetDeviceInfoTracker()
              ->IsRecentLocalCacheGuid(device->guid())) {
        continue;
      }
      // We log out if we reach this part of the loop. We only reach here when:
      // 1) the device info is not for the current device and 2) the last active
      // timestamp is after the last user activity on this device.
      ash::Shell::Get()->session_controller()->RequestSignOut();
      return;
    }
  }

  if (version_ ==
      floating_workspace_util::FloatingWorkspaceVersion::kAutoSignoutOnly) {
    // In `kAutoSignoutOnly` mode, we can rely only on the device info
    // timestamp which was handled above.
    return;
  }

  // As a final resort, if we could not logout via the device info, or
  // floating workspace entries came first before the device info, use
  // floating workspace entries to determine if we should logout.
  if (sync_service_->GetDownloadStatusFor(syncer::DataType::WORKSPACE_DESK) !=
      syncer::SyncService::DataTypeDownloadStatus::kUpToDate) {
    return;
  }
  auto* latest_floating_workspace = GetLatestFloatingWorkspaceTemplate();

  if (latest_floating_workspace == nullptr) {
    return;
  }
  // Checks if the latest uploaded floating workspace template is a captured
  // template from this device and sign out of this session if it is not.
  // Note: we are comparing the last activity time for the user here with the
  // template that we just got. Since `last_activity_time` is in timeticks and
  // the template time is in time, we need to do some manually conversion with
  // Time. Note: this time_delta is strictly > 0 but can be smaller than wall
  // clock time difference. Some additional time buffer (using the 30s from
  // the periodic capture job) is added to account for clock drifts from
  // device to device.
  if (latest_floating_workspace->client_cache_guid() !=
          desk_sync_service_->GetDeskModel()->GetCacheGuid() &&
      latest_floating_workspace->GetLastUpdatedTime() >
          initialization_time_ +
              (time_delta.is_positive() ? time_delta : base::Seconds(0)) +
              ash::features::kFloatingWorkspaceV2PeriodicJobIntervalInSeconds
                  .Get()) {
    ash::Shell::Get()->session_controller()->RequestSignOut();
  }
}

void FloatingWorkspaceService::OnAppRegistryCacheWillBeDestroyed(
    apps::AppRegistryCache* cache) {
  // Set the cache readiness to false. If this is happening, then it's very
  // likely the service will be destroyed soon.
  is_cache_ready_ = false;
  app_cache_obs_.Reset();
}

bool FloatingWorkspaceService::AreRequiredAppTypesInitialized() {
  if (!app_cache_obs_.IsObserving()) {
    return false;
  }
  apps::AppRegistryCache* cache =
      apps::AppRegistryCacheWrapper::Get().GetAppRegistryCache(
          multi_user_util::GetAccountIdFromProfile(profile_));
  DCHECK(cache);
  const std::set<apps::AppType>& initialized_types =
      cache->InitializedAppTypes();
  if (!initialized_types.contains(apps::AppType::kWeb)) {
    return false;
  }
  return initialized_types.contains(apps::AppType::kChromeApp);
}

void FloatingWorkspaceService::OnAppTypeInitialized(apps::AppType app_type) {
  // If the cache is already ready we don't need to check for additional app
  // type initialization.
  if (is_cache_ready_) {
    return;
  }
  is_cache_ready_ = AreRequiredAppTypesInitialized();
  // If we're here it means that we have floating workspace template to be
  // launched, but until this point the AppRegistryCache wasn't ready.
  if (is_cache_ready_ && should_launch_on_ready_ && should_run_restore_) {
    StopProgressBarAndRestoreFloatingWorkspace();
  }
}

void FloatingWorkspaceService::OnAppRegistryCacheAdded(
    const AccountId& account_id) {
  if (account_id != multi_user_util::GetAccountIdFromProfile(profile_) ||
      app_cache_obs_.IsObserving()) {
    return;
  }
  auto* apps_cache =
      apps::AppRegistryCacheWrapper::Get().GetAppRegistryCache(account_id);
  app_cache_obs_.Observe(apps_cache);
  is_cache_ready_ = AreRequiredAppTypesInitialized();
}

void FloatingWorkspaceService::OnActiveUserSessionChanged(
    const AccountId& account_id) {
  VLOG(1) << "Active User session changed for fws";
  Profile* active_profile =
      ash::ProfileHelper::Get()->GetProfileByAccountId(account_id);
  // Stop the capture if the switched user is not the profile we logged in with.
  // Set up the observers again if we switched back to the profile we logged in
  // with.
  if (active_profile != profile_) {
    ShutDownServicesAndObservers();
  } else {
    SetUpServiceAndObservers(
        SyncServiceFactory::GetForProfile(profile_),
        DeskSyncServiceFactory::GetForProfile(profile_),
        DeviceInfoSyncServiceFactory::GetForProfile(profile_));
  }
}

void FloatingWorkspaceService::OnFirstSessionReady() {
  // It's important that we wait for "first session ready" and not just for the
  // session state to become `session_manager::SessionState::ACTIVE` - the
  // latter happens earlier and by that time we can't yet show our modal dialog.
  if (should_run_restore_) {
    OnStateChanged(sync_service_);
  }
}

void FloatingWorkspaceService::OnLockStateChanged(bool locked) {
  // The user has signed in the device via the lock screen and has woken up from
  // sleep mode. Reset initialization times and start the flow as if the user
  // has just logged in.
  if (!locked && restore_upon_wake_) {
    if (version_ == floating_workspace_util::FloatingWorkspaceVersion::
                        kFloatingWorkspaceV2Enabled) {
      should_run_restore_ = true;
      launch_on_new_desk_ = true;
    }
    restore_upon_wake_ = false;
    initialization_time_ = base::Time::Now();
    initialization_timeticks_ = base::TimeTicks::Now();
    InitiateSigninTask();
  }
}

void FloatingWorkspaceService::OnLogoutConfirmationStarted() {
  CaptureAndUploadActiveDesk();
}

void FloatingWorkspaceService::OnFocusLeavingSystemTray(bool reverse) {}

void FloatingWorkspaceService::OnSystemTrayBubbleShown() {
  CaptureAndUploadActiveDesk();
}

void FloatingWorkspaceService::OnLocalDeviceInfoProviderReady() {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&FloatingWorkspaceService::UpdateLocalDeviceInfo,
                     weak_pointer_factory_.GetWeakPtr()));
}

void FloatingWorkspaceService::UpdateLocalDeviceInfo() {
  if (!device_info_sync_service_ ||
      !device_info_sync_service_->GetLocalDeviceInfoProvider() ||
      !device_info_sync_service_->GetLocalDeviceInfoProvider()
           ->GetLocalDeviceInfo()) {
    return;
  }
  syncer::MutableLocalDeviceInfoProvider* local_device_info_provider =
      static_cast<syncer::MutableLocalDeviceInfoProvider*>(
          device_info_sync_service_->GetLocalDeviceInfoProvider());
  local_device_info_provider->UpdateRecentSignInTime(initialization_time_);
  device_info_sync_service_->RefreshLocalDeviceInfo();
}

void FloatingWorkspaceService::ShutDownServicesAndObservers() {
  // Remove `this` service as an observer so we do not run into an issue where
  // chrome sync data is downloaded and the capture is kicked started after we
  // stopped the capture timer below.
  OnSyncShutdown(sync_service_);
  OnShuttingDown();
  OnDeviceInfoShutdown();
  // If we don't have an apps cache then we observe the wrapper to
  // wait for it to be ready.
  if (app_cache_obs_.IsObserving()) {
    app_cache_obs_.Reset();
  }
  if (app_cache_wrapper_obs_.IsObserving()) {
    app_cache_wrapper_obs_.Reset();
  }
  StopCaptureAndUploadActiveDesk();
  if (Shell::HasInstance()) {
    if (SystemTrayNotifier* system_tray_notifier =
            Shell::Get()->system_tray_notifier()) {
      system_tray_notifier->RemoveSystemTrayObserver(this);
    }
    if (LogoutConfirmationController* logout_confirmation_controller =
            Shell::Get()->logout_confirmation_controller()) {
      logout_confirmation_controller->RemoveObserver(this);
    }
  }
  if (chromeos::PowerManagerClient::Get()) {
    chromeos::PowerManagerClient::Get()->RemoveObserver(this);
  }
}

void FloatingWorkspaceService::SetUpServiceAndObservers(
    syncer::SyncService* sync_service,
    desks_storage::DeskSyncService* desk_sync_service,
    syncer::DeviceInfoSyncService* device_info_sync_service) {
  sync_service_ = sync_service;
  desk_sync_service_ = desk_sync_service;
  device_info_sync_service_ = device_info_sync_service;
  tab_sync_enabled_ = sync_service_->GetUserSettings()->GetSelectedTypes().Has(
      syncer::UserSelectableType::kTabs);
  if (!tab_sync_enabled_) {
    should_run_restore_ = false;
  }
  if (ash::NetworkHandler::IsInitialized()) {
    auto* network_handler = NetworkHandler::Get();
    if (!network_handler->network_state_handler()->HasObserver(this)) {
      network_handler->network_state_handler()->AddObserver(this);
    }
  }
  if (Shell::HasInstance()) {
    if (SystemTrayNotifier* system_tray_notifier =
            Shell::Get()->system_tray_notifier()) {
      system_tray_notifier->AddSystemTrayObserver(this);
    }
    if (LogoutConfirmationController* logout_confirmation_controller =
            Shell::Get()->logout_confirmation_controller()) {
      logout_confirmation_controller->AddObserver(this);
    }
  }
  if (sync_service_ && !sync_service_->HasObserver(this)) {
    sync_service_->AddObserver(this);
  }
  if (chromeos::PowerManagerClient::Get()) {
    chromeos::PowerManagerClient::Get()->AddObserver(this);
  }
  if (device_info_sync_service_ &&
      device_info_sync_service_->GetDeviceInfoTracker()) {
    device_info_sync_service_->GetDeviceInfoTracker()->AddObserver(this);
  }
  if (version_ ==
      floating_workspace_util::FloatingWorkspaceVersion::kAutoSignoutOnly) {
    // No need to observe apps and scheduling the capture task when we are only
    // interested in automatic sign-out, so we exit here.
    return;
  }

  // If we don't have an apps cache then we observe the wrapper to
  // wait for it to be ready.
  auto& apps_cache_wrapper = apps::AppRegistryCacheWrapper::Get();
  DCHECK(&apps_cache_wrapper);
  auto* apps_cache = apps_cache_wrapper.GetAppRegistryCache(
      multi_user_util::GetAccountIdFromProfile(profile_));
  if (apps_cache) {
    app_cache_obs_.Observe(apps_cache);
  } else {
    app_cache_wrapper_obs_.Observe(&apps_cache_wrapper);
  }
  is_cache_ready_ = AreRequiredAppTypesInitialized();
  // Explicitly start the capture if we do not need to run restore. This means
  // we had already gone through the restore logic before a profile switch and
  // won't go through the restore procedure to start the capture. So instead,
  // just start capturing.
  if (!should_run_restore_) {
    StartCaptureAndUploadActiveDesk();
    return;
  }
  SetCallbacksToLaunchOnFirstSync();
}

void FloatingWorkspaceService::SetCallbacksToLaunchOnFirstSync() {
  if (IsFloatingSsoEnabled(profile_)) {
    ash::floating_sso::FloatingSsoService* floating_sso_service =
        ash::floating_sso::FloatingSsoServiceFactory::GetForProfile(profile_);
    floating_sso_service->RunWhenCookiesAreReadyOnFirstSync(base::BindOnce(
        &FloatingWorkspaceService::LaunchWhenDeskTemplatesAreReadyOnFirstSync,
        weak_pointer_factory_.GetWeakPtr()));
  } else {
    LaunchWhenDeskTemplatesAreReadyOnFirstSync();
  }
}

void FloatingWorkspaceService::LaunchWhenDeskTemplatesAreReadyOnFirstSync() {
  if (!first_sync_data_downloaded_timeticks_.has_value()) {
    first_sync_data_downloaded_timeticks_ = base::TimeTicks::Now();
  }
  desk_sync_service_->RunWhenDesksTemplatesAreReadyOnFirstSync(
      base::BindOnce(&FloatingWorkspaceService::LaunchWhenAppCacheIsReady,
                     weak_pointer_factory_.GetWeakPtr()));
}

void FloatingWorkspaceService::MaybeStartOrStopCaptureBasedOnTabSyncSetting() {
  // Users don't have a direct toggle for workspace desks in Sync settings. But
  // if they disable tab sync there, we treat this as a signal to also disable
  // Floating Workspace functionality.
  // TODO(crbug.com/425368424): Sync data types might be disabled for a variety
  // of reasons. We should track the change of `DeskSyncBridge::IsSyncing()`
  // instead of only checking the state of `kTabs` here.
  bool tab_sync_enabled =
      sync_service_->GetUserSettings()->GetSelectedTypes().Has(
          syncer::UserSelectableType::kTabs);
  if (tab_sync_enabled_ == tab_sync_enabled) {
    return;
  }
  tab_sync_enabled_ = tab_sync_enabled;
  if (!tab_sync_enabled) {
    should_run_restore_ = false;
    StopCaptureAndUploadActiveDesk();
  } else {
    // Start capturing user's desk once they (re)-enable tab sync.
    StartCaptureAndUploadActiveDesk();
  }
}

void FloatingWorkspaceService::StopRestoringSession() {
  should_run_restore_ = false;
}
}  // namespace ash
