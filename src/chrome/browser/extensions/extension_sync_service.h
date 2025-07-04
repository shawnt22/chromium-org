// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_SERVICE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/version.h"
#include "chrome/browser/extensions/sync_bundle.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/sync/model/model_error.h"
#include "components/sync/model/syncable_service.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_prefs_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/browser/extension_system.h"
#include "extensions/buildflags/buildflags.h"

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

class Profile;

namespace extensions {
class Extension;
class ExtensionSet;
class ExtensionSyncData;
}  // namespace extensions

// SyncableService implementation responsible for the APPS and EXTENSIONS data
// types, i.e. "proper" apps/extensions (not themes).
class ExtensionSyncService : public syncer::SyncableService,
                             public KeyedService,
                             public extensions::ExtensionRegistryObserver,
                             public extensions::ExtensionPrefsObserver {
 public:
  explicit ExtensionSyncService(Profile* profile);

  ExtensionSyncService(const ExtensionSyncService&) = delete;
  ExtensionSyncService& operator=(const ExtensionSyncService&) = delete;

  ~ExtensionSyncService() override;

  // Convenience function to get the ExtensionSyncService for a BrowserContext.
  static ExtensionSyncService* Get(content::BrowserContext* context);

  // Notifies Sync (if needed) of a newly-installed extension or a change to
  // an existing extension. Call this when you change an extension setting that
  // is synced as part of ExtensionSyncData (e.g. incognito_enabled).
  void SyncExtensionChangeIfNeeded(const extensions::Extension& extension);

  // syncer::SyncableService implementation.
  void WaitUntilReadyToSync(base::OnceClosure done) override;
  std::optional<syncer::ModelError> MergeDataAndStartSyncing(
      syncer::DataType type,
      const syncer::SyncDataList& initial_sync_data,
      std::unique_ptr<syncer::SyncChangeProcessor> sync_processor) override;
  void StopSyncing(syncer::DataType type) override;
  syncer::SyncDataList GetAllSyncDataForTesting(syncer::DataType type) const;
  std::optional<syncer::ModelError> ProcessSyncChanges(
      const base::Location& from_here,
      const syncer::SyncChangeList& change_list) override;
  base::WeakPtr<SyncableService> AsWeakPtr() override;

  void SetSyncStartFlareForTesting(
      const syncer::SyncableService::StartSyncFlare& flare);

  // Special hack: There was a bug where themes incorrectly ended up in the
  // syncer::EXTENSIONS type. This is for cleaning up the data. crbug.com/558299
  // DO NOT USE FOR ANYTHING ELSE!
  // TODO(crbug.com/41401013): This *should* be safe to remove now, but it's
  // not.
  void DeleteThemeDoNotUse(const extensions::Extension& theme);

 private:
  FRIEND_TEST_ALL_PREFIXES(TwoClientExtensionAppsSyncTest,
                           UnexpectedLaunchType);
  FRIEND_TEST_ALL_PREFIXES(ExtensionDisabledGlobalErrorTest,
                           HigherPermissionsFromSync);

  // extensions::ExtensionRegistryObserver:
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;

  // extensions::ExtensionPrefsObserver:
  void OnExtensionDisableReasonsChanged(
      const std::string& extension_id,
      extensions::DisableReasonSet disabled_reasons) override;
  void OnExtensionPrefsWillBeDestroyed(
      extensions::ExtensionPrefs* prefs) override;

  // Gets the SyncBundle for the given `type`.
  extensions::SyncBundle* GetSyncBundle(syncer::DataType type);
  const extensions::SyncBundle* GetSyncBundle(syncer::DataType type) const;

  // Creates the ExtensionSyncData for the given app/extension.
  extensions::ExtensionSyncData CreateSyncData(
      const extensions::Extension& extension) const;

  // Applies the given change coming in from the server to the local state.
  void ApplySyncData(const extensions::ExtensionSyncData& extension_sync_data);

  // Collects the ExtensionSyncData for all installed apps or extensions.
  std::vector<extensions::ExtensionSyncData> GetLocalSyncDataList(
      syncer::DataType type) const;

  // Helper for GetLocalSyncDataList.
  void FillSyncDataList(
      const extensions::ExtensionSet& extensions,
      syncer::DataType type,
      std::vector<extensions::ExtensionSyncData>* sync_data_list) const;

  // Returns if the extension corresponding to the given `extension_sync_data`
  // should be promoted to an account extension, or false if there is no
  // corresponding extension.
  // Note that this is used if only the account extension state needs to be set.
  bool ShouldPromoteToAccountExtension(
      const extensions::ExtensionSyncData& extension_sync_data) const;

  // Returns if the given `extension` should receive and apply updates from
  // incoming sync data. This does not necessarily mean the extension can be
  // uploaded to sync (ShouldSync returns false).
  bool ShouldReceiveSyncData(const extensions::Extension& extension) const;

  // Returns if the given `extension` should be synced by this class (i.e. it
  // can be uploaded to the sync server).
  bool ShouldSync(const extensions::Extension& extension) const;

  // Returns true if the given `extension_id` corresponds to an item that has
  // migrated to a pre-installed web app.
  bool IsMigratingPreinstalledWebApp(
      const extensions::ExtensionId& extension_id);

  // The normal profile associated with this ExtensionSyncService.
  raw_ptr<Profile> profile_;

  raw_ptr<extensions::ExtensionSystem> system_;

  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::ScopedObservation<extensions::ExtensionPrefs,
                          extensions::ExtensionPrefsObserver>
      prefs_observation_{this};

  // When this is set to true, any incoming updates (from the observers as well
  // as from explicit SyncExtensionChangeIfNeeded calls) are ignored. This is
  // set during ApplySyncData, so that ExtensionSyncService doesn't end up
  // notifying itself while applying sync changes.
  bool ignore_updates_;

  extensions::SyncBundle app_sync_bundle_;
  extensions::SyncBundle extension_sync_bundle_;

  // Map from extension id to pending update data. Used for two things:
  // - To send the new version back to the sync server while we're waiting for
  //   an extension to update.
  // - For re-enables, to defer granting permissions until the version matches.
  struct PendingUpdate;
  std::map<std::string, PendingUpdate> pending_updates_;

  // Run()ning tells sync to try and start soon, because syncable changes
  // have started happening. It will cause sync to call us back
  // asynchronously via MergeDataAndStartSyncing as soon as possible.
  SyncableService::StartSyncFlare flare_;

#if !BUILDFLAG(IS_ANDROID)
  // Caches the set of Chrome app IDs undergoing migration to web apps because
  // it is expensive to generate every time (multiple SkBitmap copies).
  // Android does not support Chrome apps.
  std::optional<base::flat_set<std::string>>
      migrating_default_chrome_app_ids_cache_;
#endif  // !BUILDFLAG(IS_ANDROID)

  base::WeakPtrFactory<ExtensionSyncService> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_SERVICE_H_
