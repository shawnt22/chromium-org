// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_SYNC_FILE_SYSTEM_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_SYNC_FILE_SYSTEM_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/values.h"
#include "chrome/browser/sync_file_system/conflict_resolution_policy.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_callbacks.h"
#include "chrome/browser/sync_file_system/sync_process_runner.h"
#include "chrome/browser/sync_file_system/sync_service_state.h"
#include "chrome/browser/sync_file_system/task_logger.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/sync/service/sync_service_observer.h"
#include "extensions/browser/extension_registry_observer.h"
#include "url/gurl.h"

class Profile;

namespace storage {
class FileSystemContext;
}

namespace syncer {
class SyncService;
}

namespace sync_file_system {

class LocalFileSyncService;
class LocalSyncRunner;
class RemoteSyncRunner;

// Service implementing the chrome.syncFileSystem() API for the deprecated
// Chrome Apps platform.
// https://developer.chrome.com/docs/extensions/reference/syncFileSystem/
class SyncFileSystemService final
    : public KeyedService,
      public SyncProcessRunner::Client,
      public syncer::SyncServiceObserver,
      public extensions::ExtensionRegistryObserver {
 public:
  // Uses SyncFileSystemServiceFactory instead.
  explicit SyncFileSystemService(Profile* profile);
  ~SyncFileSystemService() override;
  SyncFileSystemService(const SyncFileSystemService&) = delete;
  SyncFileSystemService& operator=(const SyncFileSystemService&) = delete;

  // KeyedService implementation.
  void Shutdown() override;

  void InitializeForApp(storage::FileSystemContext* file_system_context,
                        const GURL& app_origin,
                        SyncStatusCallback callback);

  // Returns the file |url|'s sync status.
  void GetFileSyncStatus(const storage::FileSystemURL& url,
                         SyncFileStatusCallback callback);

  LocalChangeProcessor* GetLocalChangeProcessor(const GURL& origin);

  // SyncProcessRunner::Client implementations.
  void OnSyncIdle() override;
  SyncServiceState GetSyncServiceState() override;
  SyncFileSystemService* GetSyncService() override;

  void OnPromotionCompleted(int* num_running_jobs);
  void CheckIfIdle();

  TaskLogger* task_logger() { return &task_logger_; }

  void CallOnIdleForTesting(base::OnceClosure callback);

 private:
  friend class SyncFileSystemServiceFactory;
  friend class SyncFileSystemServiceTest;
  friend class SyncFileSystemTest;
  friend std::default_delete<SyncFileSystemService>;
  friend class LocalSyncRunner;
  friend class RemoteSyncRunner;

  void Initialize(std::unique_ptr<LocalFileSyncService> local_file_service,
                  std::unique_ptr<RemoteFileSyncService> remote_file_service);

  // Callbacks for InitializeForApp.
  void DidInitializeFileSystem(const GURL& app_origin,
                               SyncStatusCallback callback,
                               SyncStatusCode status);
  void DidRegisterOrigin(const GURL& app_origin,
                         SyncStatusCallback callback,
                         SyncStatusCode status);

  // Overrides sync_enabled_ setting. This should be called only by tests.
  void SetSyncEnabledForTesting(bool enabled);

  void DidGetLocalChangeStatus(SyncFileStatusCallback callback,
                               SyncStatusCode status,
                               bool has_pending_local_changes);

  void OnRemoteServiceStateUpdated(RemoteServiceState state,
                                   const std::string& description);

  // extensions::ExtensionRegistryObserver implementations.
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;

  // syncer::SyncServiceObserver implementation.
  void OnStateChanged(syncer::SyncService* sync) override;

  // Check the profile's sync preference settings and call
  // remote_file_service_->SetSyncEnabled() to update the status.
  // |sync_service| must be non-null.
  void UpdateSyncEnabledStatus(syncer::SyncService* sync_service);

  // Runs the SyncProcessRunner method of all sync runners (e.g. for Local sync
  // and Remote sync).
  void RunForEachSyncRunners(void(SyncProcessRunner::*method)());

  raw_ptr<Profile> profile_;

  std::unique_ptr<LocalFileSyncService> local_service_;
  std::unique_ptr<RemoteFileSyncService> remote_service_;

  // Holds all SyncProcessRunners.
  std::vector<std::unique_ptr<SyncProcessRunner>> local_sync_runners_;
  std::vector<std::unique_ptr<SyncProcessRunner>> remote_sync_runners_;

  // Indicates if sync is currently enabled or not.
  bool sync_enabled_;

  TaskLogger task_logger_;

  bool promoting_demoted_changes_;
  base::OnceClosure idle_callback_;
  base::WeakPtrFactory<SyncFileSystemService> weak_ptr_factory_{this};
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_SYNC_FILE_SYSTEM_SERVICE_H_
