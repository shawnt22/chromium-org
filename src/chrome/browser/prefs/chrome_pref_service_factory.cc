// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/chrome_pref_service_factory.h"

#include <stddef.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/signin/enterprise_signin_prefs.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/prefs/chrome_command_line_pref_store.h"
#include "chrome/browser/prefs/chrome_pref_model_associator_client.h"
#include "chrome/browser/prefs/profile_pref_store_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/glue/sync_start_util.h"
#include "chrome/browser/sync/prefs/chrome_syncable_prefs_database.h"
#include "chrome/browser/ui/profiles/profile_error_dialog.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "components/component_updater/pref_names.h"
#include "components/policy/core/browser/configuration_policy_pref_store.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/default_pref_store.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_filter.h"
#include "components/prefs/pref_notifier_impl.h"
#include "components/prefs/pref_registry.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_store.h"
#include "components/prefs/pref_value_store.h"
#include "components/prefs/wrap_with_prefix_pref_store.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/search_engines/default_search_manager.h"
#include "components/search_engines/search_engines_pref_names.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/supervised_user/core/browser/supervised_user_pref_store.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/features.h"
#include "components/sync/base/pref_names.h"
#include "components/sync_preferences/pref_model_associator.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "components/sync_preferences/pref_service_syncable_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "rlz/buildflags/buildflags.h"
#include "services/preferences/public/cpp/tracked/configuration.h"
#include "services/preferences/public/cpp/tracked/pref_names.h"
#include "services/preferences/tracked/pref_hash_filter.h"
#include "sql/error_delegate_util.h"
#include "ui/base/resource/resource_bundle.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "base/files/file_util.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/pref_names.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "base/enterprise_util.h"
#endif  // BUILDFLAG(IS_WIN)

using content::BrowserContext;
using content::BrowserThread;

using EnforcementLevel =
    prefs::mojom::TrackedPreferenceMetadata::EnforcementLevel;
using PrefTrackingStrategy =
    prefs::mojom::TrackedPreferenceMetadata::PrefTrackingStrategy;
using ValueType = prefs::mojom::TrackedPreferenceMetadata::ValueType;

namespace {

#if BUILDFLAG(IS_WIN)
// Whether we are in testing mode; can be enabled via
// DisableDomainCheckForTesting(). Forces startup checks to ignore the presence
// of a domain when determining the active SettingsEnforcement group.
bool g_disable_domain_check_for_testing = false;
#endif  // BUILDFLAG(IS_WIN)

// These preferences must be kept in sync with the TrackedPreference enum in
// tools/metrics/histograms/metadata/settings/enums.xml. To add a new
// preference, append it to the array and add a corresponding value to the
// histogram enum. Each tracked preference must be given a unique reporting ID.
// See CleanupDeprecatedTrackedPreferences() in pref_hash_filter.cc to remove a
// deprecated tracked preference.
const auto kTrackedPrefs = std::to_array<prefs::TrackedPreferenceMetadata>({
    {0, prefs::kShowHomeButton, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
    {1, prefs::kHomePageIsNewTabPage, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
    {2, prefs::kHomePage, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
    {3, prefs::kRestoreOnStartup, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
    {4, prefs::kURLsToRestoreOnStartup, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {5, extensions::pref_names::kExtensions, EnforcementLevel::NO_ENFORCEMENT,
     PrefTrackingStrategy::SPLIT, ValueType::IMPERSONAL},
#endif
    {6, prefs::kGoogleServicesLastSyncingUsername,
     EnforcementLevel::ENFORCE_ON_LOAD, PrefTrackingStrategy::ATOMIC,
     ValueType::PERSONAL},
    {7, prefs::kSearchProviderOverrides, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
#if !BUILDFLAG(IS_ANDROID)
    {11, prefs::kPinnedTabs, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
#endif
    {14, DefaultSearchManager::kDefaultSearchProviderDataPrefName,
     EnforcementLevel::NO_ENFORCEMENT, PrefTrackingStrategy::ATOMIC,
     ValueType::IMPERSONAL},
    {// Protecting kPreferenceResetTime does two things:
     //  1) It ensures this isn't accidently set by someone stomping the pref
     //     file.
     //  2) More importantly, it declares kPreferenceResetTime as a protected
     //     pref which is required for it to be visible when queried via the
     //     SegregatedPrefStore. This is because it's written directly in the
     //     protected JsonPrefStore by that store's PrefHashFilter if there was
     //     a reset in FilterOnLoad and SegregatedPrefStore will not look for it
     //     in the protected JsonPrefStore unless it's declared as a protected
     //     preference here.
     15, user_prefs::kPreferenceResetTime, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
    // kSyncRemainingRollbackTries is deprecated and will be removed a few
    // releases after M50.
    {18, prefs::kSafeBrowsingIncidentsSent, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
    {23, prefs::kGoogleServicesAccountId, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::PERSONAL},
    {29, prefs::kMediaStorageIdSalt, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
#if BUILDFLAG(IS_WIN)
    {32, prefs::kMediaCdmOriginData, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
#endif  // BUILDFLAG(IS_WIN)
    {33, prefs::kGoogleServicesLastSignedInUsername,
     EnforcementLevel::ENFORCE_ON_LOAD, PrefTrackingStrategy::ATOMIC,
     ValueType::PERSONAL},
    {34, enterprise_signin::prefs::kPolicyRecoveryToken,
     EnforcementLevel::ENFORCE_ON_LOAD, PrefTrackingStrategy::ATOMIC,
     ValueType::IMPERSONAL},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {35, prefs::kExtensionsUIDeveloperMode, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},
#endif
    // Allows it to trigger a write to the protected pref store.
    {36, user_prefs::kScheduleToFlushToDisk, EnforcementLevel::ENFORCE_ON_LOAD,
     PrefTrackingStrategy::ATOMIC, ValueType::IMPERSONAL},

    // See note at top, new items added here also need to be added to
    // histograms.xml's TrackedPreference enum.
});

// One more than the last tracked preferences ID above.
const size_t kTrackedPrefsReportingIDsCount =
    kTrackedPrefs[std::size(kTrackedPrefs) - 1].reporting_id + 1;

// Each group enforces a superset of the protection provided by the previous
// one.
enum SettingsEnforcementGroup {
  GROUP_NO_ENFORCEMENT,
  // Enforce protected settings on profile loads.
  GROUP_ENFORCE_ALWAYS,
  // Also enforce extension default search.
  GROUP_ENFORCE_ALWAYS_WITH_DSE,
  // Also enforce extension settings and default search.
  GROUP_ENFORCE_ALWAYS_WITH_EXTENSIONS_AND_DSE,
  // The default enforcement group contains all protection features.
  GROUP_ENFORCE_DEFAULT
};

SettingsEnforcementGroup GetSettingsEnforcementGroup() {
#if BUILDFLAG(IS_WIN)
  if (!g_disable_domain_check_for_testing) {
    static const bool is_domain_joined = base::IsEnterpriseDevice();
    if (is_domain_joined)
      return GROUP_NO_ENFORCEMENT;
  }
#endif

  // Use the strongest enforcement setting on Windows and MacOS. Remember to
  // update the OFFICIAL_BUILD section of extension_startup_browsertest.cc and
  // pref_hash_browsertest.cc when updating the default value below.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
  return GROUP_ENFORCE_DEFAULT;
#else
  return GROUP_NO_ENFORCEMENT;
#endif
}

// Returns the effective preference tracking configuration.
std::vector<prefs::mojom::TrackedPreferenceMetadataPtr>
GetTrackingConfiguration() {
  const SettingsEnforcementGroup enforcement_group =
      GetSettingsEnforcementGroup();

  browser_sync::ChromeSyncablePrefsDatabase syncable_prefs_db;
  std::vector<prefs::mojom::TrackedPreferenceMetadataPtr> result;
  for (size_t i = 0; i < std::size(kTrackedPrefs); ++i) {
    prefs::mojom::TrackedPreferenceMetadataPtr data =
        prefs::ConstructTrackedMetadata(kTrackedPrefs[i]);

    if (GROUP_NO_ENFORCEMENT == enforcement_group) {
      // Remove enforcement for all tracked preferences.
      data->enforcement_level = EnforcementLevel::NO_ENFORCEMENT;
    }

    if (enforcement_group >= GROUP_ENFORCE_ALWAYS_WITH_DSE &&
        data->name ==
            DefaultSearchManager::kDefaultSearchProviderDataPrefName) {
      // Specifically enable default search settings enforcement.
      data->enforcement_level = EnforcementLevel::ENFORCE_ON_LOAD;
    }

#if BUILDFLAG(ENABLE_EXTENSIONS)
    if (enforcement_group >= GROUP_ENFORCE_ALWAYS_WITH_EXTENSIONS_AND_DSE &&
        data->name == extensions::pref_names::kExtensions) {
      // Specifically enable extension settings enforcement.
      data->enforcement_level = EnforcementLevel::ENFORCE_ON_LOAD;
    }
#endif
    // Add the account value equivalent for syncable prefs for tracking, by
    // prefixing the pref name with `kAccountPreferencesPrefix`.
    if (base::FeatureList::IsEnabled(
            switches::kEnablePreferencesAccountStorage) &&
        syncable_prefs_db.IsPreferenceSyncable(data->name)) {
      auto account_data = data->Clone();
      account_data->name = base::StringPrintf(
          "%s.%s", chrome_prefs::kAccountPreferencesPrefix, data->name.c_str());
      result.push_back(std::move(account_data));
    }

    result.push_back(std::move(data));
  }
  return result;
}

std::unique_ptr<ProfilePrefStoreManager> CreateProfilePrefStoreManager(
    const base::FilePath& profile_path) {
  std::string seed;
  CHECK(ui::ResourceBundle::HasSharedInstance());
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  seed = std::string(ui::ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_PREF_HASH_SEED_BIN));
#endif
  return std::make_unique<ProfilePrefStoreManager>(profile_path, seed);
}

#if BUILDFLAG(IS_CHROMEOS)
// The standalone browser prefs store does not exist anymore but there may still
// be files left on disk. Delete them.
// TODO(crbug.com/380780352): Remove this code after the stepping stone.
void CleanupObsoleteStandaloneBrowserPrefsFile(
    const base::FilePath& profile_path) {
  base::FilePath file(FILE_PATH_LITERAL("standalone_browser_preferences.json"));
  base::FilePath user_data_dir;
  CHECK(base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  base::FilePath obsolete_paths[] = {user_data_dir.Append(file),
                                     profile_path.Append(file)};
  for (const auto& path : obsolete_paths) {
    if (base::PathExists(path)) {
      bool success = base::DeleteFile(path);
      LOG(WARNING) << "Removing obsolete " << path << " file: " << success;
    }
  }
}
#endif

void PrepareFactory(
    sync_preferences::PrefServiceSyncableFactory* factory,
    const base::FilePath& pref_filename,
    policy::PolicyService* policy_service,
    supervised_user::SupervisedUserSettingsService* supervised_user_settings,
    scoped_refptr<PersistentPrefStore> user_pref_store,
    scoped_refptr<PrefStore> extension_prefs,
    bool async,
    policy::BrowserPolicyConnector* policy_connector) {
  factory->SetManagedPolicies(policy_service, policy_connector);
  factory->SetRecommendedPolicies(policy_service, policy_connector);
  if (supervised_user_settings) {
    scoped_refptr<PrefStore> supervised_user_prefs =
        base::MakeRefCounted<SupervisedUserPrefStore>(supervised_user_settings);
    DCHECK(async || supervised_user_prefs->IsInitializationComplete());
    factory->set_supervised_user_prefs(supervised_user_prefs);
  }

  factory->set_async(async);
  factory->set_extension_prefs(std::move(extension_prefs));
  factory->set_command_line_prefs(
      base::MakeRefCounted<ChromeCommandLinePrefStore>(
          base::CommandLine::ForCurrentProcess()));
  factory->set_read_error_callback(base::BindRepeating(
      &chrome_prefs::HandlePersistentPrefStoreReadError, pref_filename));
  factory->set_user_prefs(std::move(user_pref_store));
  factory->SetPrefModelAssociatorClient(
      base::MakeRefCounted<ChromePrefModelAssociatorClient>());
}

class ResetOnLoadObserverImpl : public prefs::mojom::ResetOnLoadObserver {
 public:
  explicit ResetOnLoadObserverImpl(const base::FilePath& profile_path)
      : profile_path_(profile_path) {}

  ResetOnLoadObserverImpl(const ResetOnLoadObserverImpl&) = delete;
  ResetOnLoadObserverImpl& operator=(const ResetOnLoadObserverImpl&) = delete;

  void OnResetOnLoad() override {
    // A StartSyncFlare used to kick sync early in case of a reset event. This
    // is done since sync may bring back the user's server value post-reset
    // which could potentially cause a "settings flash" between the factory
    // default and the re-instantiated server value. Starting sync ASAP
    // minimizes the window before the server value is re-instantiated (this
    // window can otherwise be as long as 10 seconds by default).
    sync_start_util::GetFlareForSyncableService(profile_path_)
        .Run(syncer::PREFERENCES);
  }

 private:
  const base::FilePath profile_path_;
};

}  // namespace

namespace chrome_prefs {

const char kAccountPreferencesPrefix[] = "account_values";

std::unique_ptr<PrefService> CreateLocalState(
    const base::FilePath& pref_filename,
    scoped_refptr<PersistentPrefStore> pref_store,
    policy::PolicyService* policy_service,
    scoped_refptr<PrefRegistry> pref_registry,
    policy::BrowserPolicyConnector* policy_connector) {
  sync_preferences::PrefServiceSyncableFactory factory;
  PrepareFactory(&factory, pref_filename, policy_service,
                 nullptr,  // supervised_user_settings
                 pref_store,
                 nullptr,  // extension_prefs
                 /*async=*/false, policy_connector);

  return factory.Create(std::move(pref_registry));
}

std::unique_ptr<sync_preferences::PrefServiceSyncable> CreateProfilePrefs(
    const base::FilePath& profile_path,
    mojo::PendingRemote<prefs::mojom::TrackedPreferenceValidationDelegate>
        validation_delegate,
    policy::PolicyService* policy_service,
    supervised_user::SupervisedUserSettingsService* supervised_user_settings,
    scoped_refptr<PrefStore> extension_prefs,
    scoped_refptr<user_prefs::PrefRegistrySyncable> pref_registry,
    policy::BrowserPolicyConnector* connector,
    bool async,
    scoped_refptr<base::SequencedTaskRunner> io_task_runner,
    os_crypt_async::OSCryptAsync* os_crypt_async) {
  TRACE_EVENT0("browser", "chrome_prefs::CreateProfilePrefs");

  mojo::PendingRemote<prefs::mojom::ResetOnLoadObserver> reset_on_load_observer;
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<ResetOnLoadObserverImpl>(profile_path),
      reset_on_load_observer.InitWithNewPipeAndPassReceiver());
  sync_preferences::PrefServiceSyncableFactory factory;

  scoped_refptr<PersistentPrefStore> user_pref_store =
      CreateProfilePrefStoreManager(profile_path)
          ->CreateProfilePrefStore(
              GetTrackingConfiguration(), kTrackedPrefsReportingIDsCount,
              io_task_runner, std::move(reset_on_load_observer),
              std::move(validation_delegate), os_crypt_async);

#if BUILDFLAG(IS_CHROMEOS)
  io_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&CleanupObsoleteStandaloneBrowserPrefsFile, profile_path));
#endif

  PrepareFactory(&factory, profile_path, policy_service,
                 supervised_user_settings, user_pref_store,
                 std::move(extension_prefs), async, connector);

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  // Get raw pointers to the filters before moving user_pref_store.
  PrefFilter* default_filter = nullptr;
  PrefFilter* selected_filter = nullptr;
  if (user_pref_store) {
    // Get the underlying segregated pref store filters if possible, otherwise
    // the functions will return nullptr.
    default_filter = user_pref_store->GetDefaultStoreFilter();
    selected_filter = user_pref_store->GetSelectedStoreFilter();

    // JsonPrefStore will not have the two getters implemented, it will fall
    // back to this block below. The user_pref_store itself will have the
    // filter.
    if (!default_filter && !selected_filter) {
      default_filter = user_pref_store->GetFilter();
    }
  }
#endif  // !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)

  if (base::FeatureList::IsEnabled(
          switches::kEnablePreferencesAccountStorage)) {
    // Desktop and Mobile platforms have different implementation for account
    // preferences. Mobile platforms have a separate file to store account
    // preferences. Whereas, desktop platforms would store account preferences
    // as a dictionary in the main preference file.
#if BUILDFLAG(IS_ANDROID)
    if (!base::FeatureList::IsEnabled(syncer::kMigrateAccountPrefs)) {
      // Mobile platforms do not require preference protection. Hence pref
      // filters and ProfilePrefStoreManager::CreateProfilePrefStore() can be
      // avoided.
      factory.SetAccountPrefStore(base::MakeRefCounted<JsonPrefStore>(
          /*pref_filename=*/profile_path.Append(
              chrome::kAccountPreferencesFilename),
          /*pref_filter=*/nullptr,
          /*file_task_runner=*/io_task_runner));
    } else
#endif  // BUILDFLAG(IS_ANDROID)
    {
#if BUILDFLAG(IS_ANDROID)
      // Delete account preference file on Mobile platforms.
      // TODO(crbug.com/346508597): Remove this after an year, consistent with
      // the pref migration process.
      io_task_runner->PostTask(
          FROM_HERE, base::BindOnce(IgnoreResult(&base::DeleteFile),
                                    profile_path.Append(
                                        chrome::kAccountPreferencesFilename)));
#endif  // BUILDFLAG(IS_ANDROID)
      /**
       * Account values will live under `kAccountPreferencesPrefix` as a
       * dictionary in the main preference file and will be operated upon by a
       * WrapWithPrefixPrefStore.
       * {
       *   "A": ...
       *   "B": ...
       *   "C": ...
       *   "account_values": {
       *     "A": ...
       *     "B": ...
       *     "D": ...
       *   }
       * }
       *
       * To achieve the above, a WrapWithPrefixPrefStore is used to prefix the
       * prefs with `kAccountPreferencesPrefix` to allow easy access to the
       * account values. A DualLayerUserPrefStore then wraps this pref store
       * along with the main pref store. The callers of the
       * DualLayerUserPrefStore will be unaware of where a preference value is
       * coming from, the local store or the account store.
       *
       * +---------------------+   +------------------+   +-------------+
       * | DualLayerUserPref   |   | SegregatedPref   |   | Secure      |
       * | Store               |   | Store            |   | Preferences |
       * | +------------+      |   | +--------------+ |   | .json       |
       * | | Local Pref |      |   | |Protected Pref|-|-->|             |
       * | | Store      |---- -|-->| |Store         | |   |             |
       * | +------------+      |   | +--------------+ |   |             |
       * |                     |   |                  |   +-------------+
       * | +-----------------+ |   |                  |   +-------------+
       * | | WrapWithPrefix  | |   |                  |   | Preferences |
       * | | PrefStore       | |   | +-------------+  |   | .json       |
       * | | +-------------+ | |   | |Unprotected  |--|-->|             |
       * | | | Local Pref  | | |   | |Pref Store   |  |   |             |
       * | | | Store (same | | |
       * | | | as above)   | | |   | +-------------+  |   |             |
       * | | +-------------+ | |   +------------------+   +-------------+
       * | +-----------------+ |
       * +---------------------+
       *
       * NOTE: Mobile platforms do not require preference protection and hence,
       * the SegregatedPrefStore layer above does not actually get created,
       * thus keeping only a single preference file on Mobile platforms.
       */
      factory.SetAccountPrefStore(base::MakeRefCounted<WrapWithPrefixPrefStore>(
          std::move(user_pref_store), kAccountPreferencesPrefix));
      // Register `kAccountPreferencesPrefix` as dictionary pref. This prevents
      // others from using the prefix as a preference.
      pref_registry->RegisterDictionaryPref(kAccountPreferencesPrefix);
    }
  }

  std::unique_ptr<sync_preferences::PrefServiceSyncable> pref_service =
      factory.CreateSyncable(std::move(pref_registry));

// The PrefService is created, set the weakptr for the filters.
#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  if (default_filter) {
    default_filter->SetPrefService(pref_service.get());
  }
  if (selected_filter) {
    selected_filter->SetPrefService(pref_service.get());
  }
#endif  // !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)

  return pref_service;
}

void DisableDomainCheckForTesting() {
#if BUILDFLAG(IS_WIN)
  g_disable_domain_check_for_testing = true;
#endif  // BUILDFLAG(IS_WIN)
}

bool InitializePrefsFromMasterPrefs(
    const base::FilePath& profile_path,
    base::Value::Dict master_prefs,
    os_crypt_async::OSCryptAsync* os_crypt_async) {
  return CreateProfilePrefStoreManager(profile_path)
      ->InitializePrefsFromMasterPrefs(GetTrackingConfiguration(),
                                       kTrackedPrefsReportingIDsCount,
                                       std::move(master_prefs), os_crypt_async);
}

base::Time GetResetTime(Profile* profile) {
  return ProfilePrefStoreManager::GetResetTime(profile->GetPrefs());
}

void ClearResetTime(Profile* profile) {
  ProfilePrefStoreManager::ClearResetTime(profile->GetPrefs());
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  ProfilePrefStoreManager::RegisterProfilePrefs(registry);
}

void HandlePersistentPrefStoreReadError(
    const base::FilePath& pref_filename,
    PersistentPrefStore::PrefReadError error) {
  // The error callback is always invoked back on the main thread (which is
  // BrowserThread::UI unless called during early initialization before the main
  // thread is promoted to BrowserThread::UI).
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         !BrowserThread::IsThreadInitialized(BrowserThread::UI));

  if (error != PersistentPrefStore::PREF_READ_ERROR_NONE) {
#if !BUILDFLAG(IS_CHROMEOS)
    // Failing to load prefs on startup is a bad thing(TM). See bug 38352 for
    // an example problem that this can cause.
    // Do some diagnosis and try to avoid losing data.
    int message_id = 0;
    if (error <= PersistentPrefStore::PREF_READ_ERROR_JSON_TYPE) {
      message_id = IDS_PREFERENCES_CORRUPT_ERROR;
    } else if (error != PersistentPrefStore::PREF_READ_ERROR_NO_FILE) {
      message_id = IDS_PREFERENCES_UNREADABLE_ERROR;
    }

    if (message_id) {
      // Note: SingleThreadTaskRunner::CurrentDefaultHandle is usually
      // BrowserThread::UI but during early startup it can be
      // ChromeBrowserMainParts::DeferringTaskRunner which will forward to
      // BrowserThread::UI when it's initialized.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&ShowProfileErrorDialog, ProfileErrorType::PREFERENCES,
                         message_id,
                         sql::GetCorruptFileDiagnosticsInfo(pref_filename)));
    }
#else
    // On ChromeOS error screen with message about broken local state
    // will be displayed.

    // A supplementary error message about broken local state - is included
    // in logs and user feedbacks.
    if (error != PersistentPrefStore::PREF_READ_ERROR_NONE &&
        error != PersistentPrefStore::PREF_READ_ERROR_NO_FILE) {
      LOG(ERROR) << "An error happened during prefs loading: " << error;
    }
#endif
  }
}

}  // namespace chrome_prefs
