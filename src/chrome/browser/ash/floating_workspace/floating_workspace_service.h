// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_FLOATING_WORKSPACE_FLOATING_WORKSPACE_SERVICE_H_
#define CHROME_BROWSER_ASH_FLOATING_WORKSPACE_FLOATING_WORKSPACE_SERVICE_H_

#include <memory>
#include <string>

#include "ash/public/cpp/desk_template.h"
#include "ash/public/cpp/session/session_observer.h"
#include "ash/system/session/logout_confirmation_controller.h"
#include "ash/system/tray/system_tray_observer.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "chrome/browser/ash/floating_workspace/floating_workspace_util.h"
#include "chrome/browser/ui/ash/desks/desks_client.h"
#include "chromeos/ash/components/network/network_state_handler_observer.h"
#include "chromeos/ash/services/device_sync/public/cpp/device_sync_client.h"
#include "chromeos/dbus/power/power_manager_client.h"
#include "components/desks_storage/core/desk_model.h"
#include "components/desks_storage/core/desk_sync_bridge.h"
#include "components/desks_storage/core/desk_sync_service.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"
#include "components/services/app_service/public/cpp/app_registry_cache_wrapper.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/service/sync_service_observer.h"
#include "components/sync_device_info/device_info_sync_service.h"
#include "components/sync_device_info/device_info_tracker.h"

class Profile;

namespace sync_sessions {
class OpenTabsUIDelegate;
class SessionSyncService;
struct SyncedSession;
}  // namespace sync_sessions

namespace ash {

// How long do we wait before showing the network screen in case there is no
// connection.
inline constexpr base::TimeDelta kFwsNetworkScreenDelay = base::Seconds(2);

// A keyed service to support floating workspace. Note that a periodical
// task `CaptureAndUploadActiveDesk` will be dispatched during service
// initialization.
class FloatingWorkspaceService
    : public KeyedService,
      public syncer::SyncServiceObserver,
      public apps::AppRegistryCache::Observer,
      public apps::AppRegistryCacheWrapper::Observer,
      public ash::SessionObserver,
      public ash::LogoutConfirmationController::Observer,
      public NetworkStateHandlerObserver,
      public ash::SystemTrayObserver,
      public chromeos::PowerManagerClient::Observer,
      public syncer::DeviceInfoTracker::Observer {
 public:
  explicit FloatingWorkspaceService(
      Profile* profile,
      floating_workspace_util::FloatingWorkspaceVersion version);

  ~FloatingWorkspaceService() override;

  // Used in constructor for initializations
  void Init(syncer::SyncService* sync_service,
            desks_storage::DeskSyncService* desk_sync_service,
            syncer::DeviceInfoSyncService* device_info_sync_service);

  // Add subscription to foreign session changes.
  void SubscribeToForeignSessionUpdates();

  // Get and restore most recently used device browser session
  // remote or local.
  void RestoreBrowserWindowsFromMostRecentlyUsedDevice();

  void TryRestoreMostRecentlyUsedSession();

  void CaptureAndUploadActiveDeskForTest(
      std::unique_ptr<DeskTemplate> desk_template);

  // Get latest Floating Workspace Template from DeskSyncBridge.
  const DeskTemplate* GetLatestFloatingWorkspaceTemplate();

  // syncer::SyncServiceObserver overrides:
  void OnStateChanged(syncer::SyncService* sync) override;
  void OnSyncShutdown(syncer::SyncService* sync) override;

  // ash::SessionObserver overrides:
  void OnActiveUserSessionChanged(const AccountId& account_id) override;
  void OnFirstSessionReady() override;
  void OnLockStateChanged(bool locked) override;

  // ash::LogoutConfirmationController::Observer:
  void OnLogoutConfirmationStarted() override;

  // NetworkStateHandlerObserver:
  void OnShuttingDown() override;
  void NetworkConnectionStateChanged(const NetworkState* network) override;
  void DefaultNetworkChanged(const NetworkState* network) override;

  // ash::SystemTrayObserver overrides:
  void OnFocusLeavingSystemTray(bool reverse) override;
  void OnSystemTrayBubbleShown() override;

  // chromeos::PowerManagerClient::Observer overrides.
  void SuspendImminent(power_manager::SuspendImminent::Reason reason) override;
  void SuspendDone(base::TimeDelta sleep_duration) override;

  // syncer::DeviceInfoTracker::Observer:
  void OnDeviceInfoChange() override;
  void OnDeviceInfoShutdown() override;

  std::vector<const ash::DeskTemplate*> GetFloatingWorkspaceTemplateEntries();

  // Setups the convenience pointers to the dependent services and observers.
  // This will be called when the service is first initialized and when the
  // active user session is changed back to the first logged in user.
  void SetUpServiceAndObservers(
      syncer::SyncService* sync_service,
      desks_storage::DeskSyncService* desk_sync_service,
      syncer::DeviceInfoSyncService* device_info_sync_service);

  // Shuts down the observers and dependent services.
  // This will be called when the user session changes to a different user or
  // on service shutdown.
  void ShutDownServicesAndObservers();

  // Capture the current active desk task, running every ~30(TBD) seconds.
  // Upload captured desk to chrome sync and record the randomly generated
  // UUID key to `floating_workspace_template_uuid_`.
  void CaptureAndUploadActiveDesk();

  // Prevents floating workspace service from restoring the session.
  void StopRestoringSession();

 protected:
  std::unique_ptr<DeskTemplate> previously_captured_desk_template_;

 private:
  // AppRegistryCache::Observer
  void OnAppRegistryCacheWillBeDestroyed(
      apps::AppRegistryCache* cache) override;
  void OnAppTypeInitialized(apps::AppType app_type) override;

  // AppRegistryCacheWrapper::Observer
  void OnAppRegistryCacheAdded(const AccountId& account_id) override;

  void InitForV1();
  void InitForV2(syncer::SyncService* sync_service,
                 desks_storage::DeskSyncService* desk_sync_service,
                 syncer::DeviceInfoSyncService* device_info_sync_service);

  const sync_sessions::SyncedSession* GetMostRecentlyUsedRemoteSession();

  const sync_sessions::SyncedSession* GetLocalSession();

  // Virtual for testing.
  virtual void RestoreForeignSessionWindows(
      const sync_sessions::SyncedSession* session);

  // Virtual for testing.
  virtual void RestoreLocalSessionWindows();

  // Virtual for testing.
  virtual sync_sessions::OpenTabsUIDelegate* GetOpenTabsUIDelegate();

  // Start and Stop capturing and uploading the active desks.
  void StartCaptureAndUploadActiveDesk();
  void StopCaptureAndUploadActiveDesk();

  // Stops the progress bar and resumes the latest floating workspace. This is
  // called when the app cache is ready and sync data is available.
  void StopProgressBarAndRestoreFloatingWorkspace();

  // Restore last saved floating workspace desk for current user with
  // floating_workspace_template_uuid_.
  void RestoreFloatingWorkspaceTemplate(const DeskTemplate* desk_template);

  // Launch downloaded floating workspace desk when all conditions are met.
  // Virtual for testing.
  virtual void LaunchFloatingWorkspaceTemplate(
      const DeskTemplate* desk_template);

  // Handles the recording of the error for template launch.
  void HandleTemplateLaunchErrors(DesksClient::DeskActionError error);

  // Callback function that is run after a floating workspace template
  // is downloaded and launched.
  void OnTemplateLaunched(std::optional<DesksClient::DeskActionError> error,
                          const base::Uuid& desk_uuid);

  // Return the desk client to be used, in test it will return a mocked one.
  virtual DesksClient* GetDesksClient();

  // Compare currently captured and previous floating workspace desk.
  // Called by CaptureAndUploadActiveDesk before upload.
  // If no difference is recorded no upload job will be triggered.
  bool IsCurrentDeskSameAsPrevious(DeskTemplate* current_desk_template) const;

  // Handles the recording of the error for template capture.
  void HandleTemplateCaptureErrors(DesksClient::DeskActionError error);

  // Callback function that is run after a floating workspace template is
  // captured by `desks_storage::DeskSyncBridge`.
  void OnTemplateCaptured(std::optional<DesksClient::DeskActionError> error,
                          std::unique_ptr<DeskTemplate> desk_template);

  // Upload floating workspace desk template after detecting that it's a
  // different template. Virtual for testing.
  virtual void UploadFloatingWorkspaceTemplateToDeskModel(
      std::unique_ptr<DeskTemplate> desk_template);

  void OnTemplateUploaded(
      desks_storage::DeskModel::AddOrUpdateEntryStatus status,
      std::unique_ptr<DeskTemplate> new_entry);

  // Get the associated floating workspace uuid for the current device. Return
  // an std::nullopt if there is no floating workspace uuid that is associated
  // with the current device.
  std::optional<base::Uuid> GetFloatingWorkspaceUuidForCurrentDevice();

  // Performs garbage collection of stale floating workspace templates. A
  // floating workspace template is considered stale if it's older than 30
  // days. The only exception is if it's the only floating workspace
  // template associated with the current user, which we want to keep.
  void DoGarbageCollection(const DeskTemplate* exclude_template);

  // Close desks that were already open on current device.
  void RemoveAllPreviousDesksExceptActiveDesk(
      const base::Uuid& exclude_desk_uuid);

  // Sign out of the current user session when we detect another active
  // session after this service was started.
  void MaybeSignOutOfCurrentSession();

  // Updates the `is_cache_ready_` status if all the required app types are
  // initialized.
  bool AreRequiredAppTypesInitialized();

  void UpdateUiStateIfNeeded();

  // Initial task start. This includes checking the network connectivity upon
  // login and setting the appropriate state for startup UI.
  void InitiateSigninTask();

  // Returns true if we should exclude the `floating_workspace_template` from
  // consideration for either sign out or restore.
  bool ShouldExcludeTemplate(const DeskTemplate* floating_workspace_template);

  // Called by local_device_info_provider when it is ready.
  void OnLocalDeviceInfoProviderReady();

  // Updates the local device info with the new floating workspace recent signin
  // time.
  void UpdateLocalDeviceInfo();

  // Check if we should wait for cookies to be synced before restoring the
  // workspace. If yes, it will set the callback for Floating SSO code to
  // restore the workspace once cookies are ready.
  bool ShouldWaitForCookies();

  // Schedule restoration of floating workspace on app cache being ready. Will
  // restore immediately if cache is ready at the moment of the call.
  void LaunchWhenAppCacheIsReady();

  void LaunchWhenDeskTemplatesAreReadyOnFirstSync();

  // When there is no connection, we slightly delay showing the network screen,
  // because on startup ChromeOS might be connecting to a new network, and we
  // want to avoid showing the network screen for a few seconds only for it to
  // immediately go away.
  void ScheduleShowingNetworkScreen();
  void MaybeShowNetworkScreen();

  // When syncing for the very first time, Chrome can assume that all Chrome
  // Sync data for a given Sync type is downloaded once corresponding Sync
  // bridge executes `MergeFullSyncData` method.
  // `SetCallbacksToLaunchOnFirstSync` sets callbacks to bridges responsible for
  // desk templates and cookies (if enabled) to launch as soon as data is
  // downloaded. This only works on the very first sync, in other cases we
  // should wait for `UpToDate` signal from the sync service before launching,
  // see `OnStateChanged` method. On the first sync `UpToDate` signal comes with
  // a delay, so tracking `MergeFullSyncData` can be seen as an optimization.
  void SetCallbacksToLaunchOnFirstSync();

  void MaybeStartOrStopCaptureBasedOnTabSyncSetting();

  const raw_ptr<Profile> profile_;

  const floating_workspace_util::FloatingWorkspaceVersion version_;

  raw_ptr<sync_sessions::SessionSyncService> session_sync_service_;

  base::CallbackListSubscription foreign_session_updated_subscription_;

  // Flag to determine if we should run the restore.
  bool should_run_restore_ = true;

  // Tells us whether or not the apps cache is ready.
  bool is_cache_ready_ = false;

  // Flag to tell us if we should launch on cache is ready.
  bool should_launch_on_ready_ = false;

  // Flag to tell us if we should restore when we wake up from sleep.
  bool restore_upon_wake_ = false;

  // Flag to tell us if we should launch the floating workspace template onto a
  // new desk.
  bool launch_on_new_desk_ = false;

  // When Sync state changes we check if the user has disabled tab sync. This
  // flag caches the result of the last check.
  bool tab_sync_enabled_ = true;

  // Time when the service is initialized.
  base::TimeTicks initialization_timeticks_;

  // Time when service is initialized in base::Time format for comparison with
  // desk template time.
  base::Time initialization_time_;

  // Time when sync data becomes available for the first time.
  std::optional<base::TimeTicks> first_sync_data_downloaded_timeticks_;

  // Time when the last template was uploaded.
  base::TimeTicks last_uploaded_timeticks_;

  // The in memory cache of the latest floating workspace template. This is
  // populated when we first capture a floating workspace template and every
  // time we receive a new floating workspace template from sync. This is used
  // to detect stale entries when we rerun floating workspace flow from sleep
  // mode.
  std::optional<base::Time> timestamp_before_suspend_;

  // Timer used for periodic capturing and uploading.
  base::RepeatingTimer timer_;

  // Timer used to wait for internet connection after service initialization.
  base::OneShotTimer connection_timer_;

  // Timer used to periodically update the progress status bar based on time
  // from the 2 seconds after login to 15 seconds max wait time.
  base::RepeatingTimer progress_timer_;

  // Convenience pointer to desks_storage::DeskSyncService. Guaranteed to be
  // not null for the duration of `this`.
  raw_ptr<desks_storage::DeskSyncService> desk_sync_service_ = nullptr;

  raw_ptr<syncer::SyncService> sync_service_ = nullptr;

  raw_ptr<syncer::DeviceInfoSyncService> device_info_sync_service_ = nullptr;

  base::CallbackListSubscription local_device_info_ready_subscription_;

  // The uuid associated with this device's floating workspace template. This is
  // populated when we first capture a floating workspace template.
  std::optional<base::Uuid> floating_workspace_uuid_;


  // The in memory cache of the floating workspace that should be restored
  // after downloading latest updates. Saved in case the user delays resuming
  // the session and a captured template was uploaded.
  std::unique_ptr<DeskTemplate> floating_workspace_template_to_restore_ =
      nullptr;

  // scoped Observations
  base::ScopedObservation<apps::AppRegistryCache,
                          apps::AppRegistryCache::Observer>
      app_cache_obs_{this};
  base::ScopedObservation<apps::AppRegistryCacheWrapper,
                          apps::AppRegistryCacheWrapper::Observer>
      app_cache_wrapper_obs_{this};
  // Weak pointer factory used to provide references to this service.
  base::WeakPtrFactory<FloatingWorkspaceService> weak_pointer_factory_{this};
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_FLOATING_WORKSPACE_FLOATING_WORKSPACE_SERVICE_H_
