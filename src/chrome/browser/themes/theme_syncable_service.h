// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_THEMES_THEME_SYNCABLE_SERVICE_H_
#define CHROME_BROWSER_THEMES_THEME_SYNCABLE_SERVICE_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/themes/theme_local_data_batch_uploader.h"
#include "chrome/browser/themes/theme_service_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/sync/model/sync_change.h"
#include "components/sync/model/sync_data.h"
#include "components/sync/model/syncable_service.h"

class PrefService;
class Profile;
class ThemeService;

namespace sync_pb {
class ThemeSpecifics;
}

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
//
// LINT.IfChange(ThemePrefInMigration)
enum class ThemePrefInMigration {
  kBrowserColorScheme,
  kUserColor,
  kBrowserColorVariant,
  kGrayscaleThemeEnabled,
  kNtpCustomBackgroundDict,
  kMaxValue = kNtpCustomBackgroundDict
};
// LINT.ThenChange(//tools/metrics/histograms/metadata/sync/enums.xml:ThemePrefInMigration)

void MigrateSyncingThemePrefsToNonSyncingIfNeeded(PrefService* prefs);

class ThemeSyncableService final : public syncer::SyncableService,
                                   public ThemeServiceObserver,
                                   public ThemeLocalDataBatchUploaderDelegate {
 public:
  // State of local theme after applying sync changes.
  enum class ThemeSyncState {
    // The remote theme has been applied locally or the other way around (or
    // there was no change to apply).
    kApplied,
    // Remote theme failed to apply locally.
    kFailed,
    // Remote theme is an extension theme that is not installed locally, yet.
    // Theme sync triggered the installation that may not be applied yet (as
    // extension installation is in nature async and also can fail).
    kWaitingForExtensionInstallation
  };

  class Observer : public base::CheckedObserver {
   public:
    // Called when theme sync gets started. Observers that register after theme
    // sync gets started are never called, they should check
    // GetThemeSyncStartState() before registering, instead.
    virtual void OnThemeSyncStarted(ThemeSyncState state) = 0;
  };

  // `profile` may be nullptr in tests (and is the one used by theme_service,
  // otherwise).
  ThemeSyncableService(Profile* profile, ThemeService* theme_service);

  ThemeSyncableService(const ThemeSyncableService&) = delete;
  ThemeSyncableService& operator=(const ThemeSyncableService&) = delete;

  ~ThemeSyncableService() override;

  static syncer::DataType data_type() { return syncer::THEMES; }

  // ThemeServiceObserver implementation.
  void OnThemeChanged() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);
  void NotifyOnSyncStartedForTesting(ThemeSyncState startup_state);

  // Returns the theme sync startup state or nullopt if it has not started yet.
  std::optional<ThemeSyncState> GetThemeSyncStartState();

  // syncer::SyncableService implementation.
  void WaitUntilReadyToSync(base::OnceClosure done) override;
  void WillStartInitialSync() override;
  std::optional<syncer::ModelError> MergeDataAndStartSyncing(
      syncer::DataType type,
      const syncer::SyncDataList& initial_sync_data,
      std::unique_ptr<syncer::SyncChangeProcessor> sync_processor) override;
  void StopSyncing(syncer::DataType type) override;
  void OnBrowserShutdown(syncer::DataType type) override;
  void StayStoppedAndMaybeClearData(syncer::DataType type) override;
  syncer::SyncDataList GetAllSyncDataForTesting(syncer::DataType type) const;
  std::optional<syncer::ModelError> ProcessSyncChanges(
      const base::Location& from_here,
      const syncer::SyncChangeList& change_list) override;
  base::WeakPtr<SyncableService> AsWeakPtr() override;

  // Returns a ThemeSpecifics based on the currently applied theme.
  sync_pb::ThemeSpecifics GetThemeSpecificsFromCurrentThemeForTesting() const;

  // Client tag and title of the single theme sync_pb::SyncEntity of an account.
  static const char kSyncEntityClientTag[];
  static const char kSyncEntityTitle[];

  static bool AreThemeSpecificsEquivalent(
      const sync_pb::ThemeSpecifics& a,
      const sync_pb::ThemeSpecifics& b,
      bool is_system_theme_distinct_from_default_theme);

  // Returns whether extensions or autogenerated themes are used.
  static bool HasNonDefaultTheme(
      const sync_pb::ThemeSpecifics& theme_specifics);

 private:
  class PrefServiceSyncableObserver;

  // ThemeLocalDataBatchUploaderDelegate implementation.
  std::optional<sync_pb::ThemeSpecifics> GetSavedLocalTheme() const override;
  bool ApplySavedLocalThemeIfExistsAndClear() override;

  // Set theme from `new_specs` if it's different from `current_specs`. Returns
  // the state of themes after the operation.
  ThemeSyncState MaybeSetTheme(const sync_pb::ThemeSpecifics& current_specs,
                               const sync_pb::ThemeSpecifics& new_specs);

  // Returns a ThemeSpecifics based on the currently applied theme.
  sync_pb::ThemeSpecifics GetThemeSpecificsFromCurrentTheme() const;

  // Returns if the current theme is syncable. A theme can be unsyncable if, for
  // example, it is set by an unsyncable extension or is set by policy.
  bool IsCurrentThemeSyncable() const;

  // Updates theme specifics in sync to |theme_specifics|.
  std::optional<syncer::ModelError> ProcessNewTheme(
      syncer::SyncChange::SyncChangeType change_type,
      const sync_pb::ThemeSpecifics& theme_specifics);

  void NotifyOnSyncStarted(ThemeSyncState startup_state);

  const raw_ptr<Profile> profile_;
  const raw_ptr<ThemeService> theme_service_;

  base::ObserverList<Observer> observer_list_;

  std::unique_ptr<syncer::SyncChangeProcessor> sync_processor_;

  // Persist use_system_theme_by_default for platforms that use it, even if
  // we're not on one.
  bool use_system_theme_by_default_;

  // Tracks whether changes from the syncer are being processed.
  bool processing_syncer_changes_ = false;

  // Captures the state of theme sync after initial data merge.
  std::optional<ThemeSyncState> startup_state_;

  // Holds the id of the remote extension theme, if any, pending installation.
  std::optional<std::string> remote_extension_theme_pending_install_;

  base::ThreadChecker thread_checker_;

  PrefChangeRegistrar pref_change_registrar_;

  std::unique_ptr<PrefServiceSyncableObserver> pref_service_syncable_observer_;

  base::WeakPtrFactory<ThemeSyncableService> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_THEMES_THEME_SYNCABLE_SERVICE_H_
