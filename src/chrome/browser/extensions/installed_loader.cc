// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/installed_loader.h"

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/notimplemented.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/corrupted_extension_reinstaller.h"
#include "chrome/browser/extensions/extension_allowlist.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/load_error_reporter.h"
#include "chrome/browser/extensions/profile_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/chrome_manifest_url_handlers.h"
#include "chrome/common/extensions/manifest_handlers/settings_overrides_handler.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/allowlist_state.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/install_prefs_helper.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/pref_types.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_l10n_util.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/features/feature_developer_mode_only.h"
#include "extensions/common/file_util.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permission_message_provider.h"
#include "extensions/common/permissions/permissions_data.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "components/user_manager/user.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

using content::BrowserThread;

namespace extensions {

namespace {

// DO NOT REORDER. This enum is used in histograms.
enum class ManifestVersionPopulationSplit {
  kNoExtensions = 0,
  kMv2ExtensionsOnly,
  kMv2AndMv3Extensions,
  kMv3ExtensionsOnly,

  kMaxValue = kMv3ExtensionsOnly,
};

// Used in histogram Extensions.BackgroundPageType.
enum class BackgroundPageType {
  kNone = 0,
  kPersistent,
  kEventPage,
  kServiceWorker,

  // New enum values must go above here.
  kMaxValue = kServiceWorker
};

// Used in histogram Extensions.ExternalItemState.
enum class ExternalItemState {
  kDeprecated_Disabled = 0,
  kDeprecated_Enabled,
  kWebstoreDisabled,
  kWebstoreEnabled,
  kNonwebstoreDisabled,
  kNonwebstoreEnabled,
  kWebstoreUninstalled,
  kNonwebstoreUninstalled,

  // New enum values must go above here.
  kMaxValue = kNonwebstoreUninstalled
};

bool IsManifestCorrupt(const base::Value::Dict& manifest) {
  // Because of bug #272524 sometimes manifests got mangled in the preferences
  // file, one particularly bad case resulting in having both a background page
  // and background scripts values. In those situations we want to reload the
  // manifest from the extension to fix this.
  return manifest.contains(manifest_keys::kBackgroundPage) &&
         manifest.contains(manifest_keys::kBackgroundScripts);
}

bool ShouldReloadExtensionManifest(const ExtensionInfo& info) {
  // Always reload manifests of unpacked extensions, because they can change
  // on disk independent of the manifest in our prefs.
  if (Manifest::IsUnpackedLocation(info.extension_location)) {
    return true;
  }

  if (!info.extension_manifest) {
    return false;
  }

  // Reload the manifest if it needs to be relocalized.
  if (extension_l10n_util::ShouldRelocalizeManifest(*info.extension_manifest)) {
    return true;
  }

  // Reload if the copy of the manifest in the preferences is corrupt.
  if (IsManifestCorrupt(*info.extension_manifest)) {
    return true;
  }

  return false;
}

BackgroundPageType GetBackgroundPageType(const Extension* extension) {
  if (!BackgroundInfo::HasBackgroundPage(extension)) {
    return BackgroundPageType::kNone;
  }
  if (BackgroundInfo::HasPersistentBackgroundPage(extension)) {
    return BackgroundPageType::kPersistent;
  }
  if (BackgroundInfo::IsServiceWorkerBased(extension)) {
    return BackgroundPageType::kServiceWorker;
  }
  return BackgroundPageType::kEventPage;
}

// Helper to record a single disable reason histogram value (see
// RecordDisableReasons below).
void RecordDisbleReasonHistogram(int reason) {
  base::UmaHistogramSparse("Extensions.DisableReason2", reason);
}

// Records the disable reasons for a single extension grouped by
// disable_reason::DisableReason.
void RecordDisableReasons(const DisableReasonSet& reasons) {
  // |reasons| is a bitmask with values from ExtensionDisabledReason
  // which are increasing powers of 2.
  if (reasons.empty()) {
    RecordDisbleReasonHistogram(disable_reason::DISABLE_NONE);
    return;
  }
  for (int reason : reasons) {
    RecordDisbleReasonHistogram(reason);
  }
}

// Returns the current access level for the given `extension`.
HostPermissionsAccess GetHostPermissionAccessLevelForExtension(
    const Extension& extension) {
  if (!util::CanWithholdPermissionsFromExtension(extension)) {
    return HostPermissionsAccess::kCannotAffect;
  }

  bool has_active_hosts = !extension.permissions_data()
                               ->active_permissions()
                               .effective_hosts()
                               .is_empty();
  size_t active_hosts_size = extension.permissions_data()
                                 ->active_permissions()
                                 .effective_hosts()
                                 .size();
  bool has_withheld_hosts = !extension.permissions_data()
                                 ->withheld_permissions()
                                 .effective_hosts()
                                 .is_empty();

  if (!has_active_hosts && !has_withheld_hosts) {
    // No hosts are granted or withheld, so none were requested.
    // Check if the extension is using activeTab.
    return extension.permissions_data()->HasAPIPermission(
               mojom::APIPermissionID::kActiveTab)
               ? HostPermissionsAccess::kOnActiveTabOnly
               : HostPermissionsAccess::kNotRequested;
  }

  if (!has_withheld_hosts) {
    // No hosts were withheld; the extension is running all requested sites.
    return HostPermissionsAccess::kOnAllRequestedSites;
  }

  // The extension is running automatically on some of the requested sites.
  // <all_urls> (strangely) includes the chrome://favicon/ permission. Thus,
  // we avoid counting the favicon pattern in the active hosts.
  if (active_hosts_size > 1) {
    return HostPermissionsAccess::kOnSpecificSites;
  }
  if (active_hosts_size == 1) {
    const URLPattern& single_pattern = *extension.permissions_data()
                                            ->active_permissions()
                                            .effective_hosts()
                                            .begin();
    if (single_pattern.scheme() != content::kChromeUIScheme ||
        single_pattern.host() != chrome::kChromeUIFaviconHost) {
      return HostPermissionsAccess::kOnSpecificSites;
    }
  }

  // The extension is not running automatically anywhere. All its hosts were
  // withheld.
  return HostPermissionsAccess::kOnClick;
}

void LogHostPermissionsAccess(const Extension& extension,
                              bool should_record_incremented_metrics) {
  HostPermissionsAccess access_level =
      GetHostPermissionAccessLevelForExtension(extension);
  // Extensions.HostPermissions.GrantedAccess is emitted for every
  // extension.
  base::UmaHistogramEnumeration("Extensions.HostPermissions.GrantedAccess",
                                access_level);
  if (should_record_incremented_metrics) {
    base::UmaHistogramEnumeration("Extensions.HostPermissions.GrantedAccess2",
                                  access_level);
  }

  const PermissionSet& active_permissions =
      extension.permissions_data()->active_permissions();
  const PermissionSet& withheld_permissions =
      extension.permissions_data()->withheld_permissions();

  // Since we only care about host permissions here, we don't want to
  // look at API permissions that might cause Chrome to warn about all hosts
  // (like debugger or devtools).
  static constexpr bool kIncludeApiPermissions = false;
  if (active_permissions.ShouldWarnAllHosts(kIncludeApiPermissions) ||
      withheld_permissions.ShouldWarnAllHosts(kIncludeApiPermissions)) {
    // Extension requests access to at least one eTLD.
    base::UmaHistogramEnumeration(
        "Extensions.HostPermissions.GrantedAccessForBroadRequests",
        access_level);
    if (should_record_incremented_metrics) {
      base::UmaHistogramEnumeration(
          "Extensions.HostPermissions.GrantedAccessForBroadRequests2",
          access_level);
    }
  } else if (!active_permissions.effective_hosts().is_empty() ||
             !withheld_permissions.effective_hosts().is_empty()) {
    // Extension requests access to hosts, but not eTLD.
    base::UmaHistogramEnumeration(
        "Extensions.HostPermissions.GrantedAccessForTargetedRequests",
        access_level);
    if (should_record_incremented_metrics) {
      base::UmaHistogramEnumeration(
          "Extensions.HostPermissions.GrantedAccessForTargetedRequests2",
          access_level);
    }
  }
}

}  // namespace

InstalledLoader::InstalledLoader(Profile* profile)
    : profile_(profile),
      extension_registry_(ExtensionRegistry::Get(profile_)),
      extension_prefs_(ExtensionPrefs::Get(profile_)),
      extension_management_(
          ExtensionManagementFactory::GetForBrowserContext(profile_)) {}

InstalledLoader::~InstalledLoader() = default;

void InstalledLoader::Load(const ExtensionInfo& info, bool write_to_prefs) {
  // TODO(asargent): add a test to confirm that we can't load extensions if
  // their ID in preferences does not match the extension's actual ID.
  if (invalid_extensions_.find(info.extension_path) !=
      invalid_extensions_.end())
    return;

  std::string error;
  scoped_refptr<const Extension> extension;
  if (info.extension_manifest) {
    extension = Extension::Create(info.extension_path, info.extension_location,
                                  *info.extension_manifest,
                                  GetCreationFlags(&info), &error);
  } else {
    error = manifest_errors::kManifestUnreadable;
  }

  // Once installed, non-unpacked extensions cannot change their IDs (e.g., by
  // updating the 'key' field in their manifest).
  // TODO(jstritar): migrate preferences when unpacked extensions change IDs.
  if (extension.get() && !Manifest::IsUnpackedLocation(extension->location()) &&
      info.extension_id != extension->id()) {
    error = manifest_errors::kCannotChangeExtensionID;
    extension = nullptr;
  }

  if (!extension.get()) {
    LoadErrorReporter::GetInstance()->ReportLoadError(info.extension_path,
                                                      error, profile_,
                                                      false);  // Be quiet.
    return;
  }

  const ManagementPolicy* policy =
      ExtensionSystem::Get(profile_)->management_policy();

  if (extension_prefs_->IsExtensionDisabled(extension->id())) {
    DisableReasonSet disable_reasons =
        extension_prefs_->GetDisableReasons(extension->id());

    // Update the extension prefs to reflect if the extension is no longer
    // blocked due to admin policy.
    if (disable_reasons.contains(disable_reason::DISABLE_BLOCKED_BY_POLICY) &&
        !policy->MustRemainDisabled(extension.get(), nullptr)) {
      disable_reasons.erase(disable_reason::DISABLE_BLOCKED_BY_POLICY);
      extension_prefs_->RemoveDisableReason(
          extension->id(), disable_reason::DISABLE_BLOCKED_BY_POLICY);
    }

    if ((disable_reasons.contains(disable_reason::DISABLE_CORRUPTED))) {
      HandleCorruptExtension(*extension, *policy);
    }
  } else {
    // Extension is enabled. Check management policy to verify if it should
    // remain so.
    disable_reason::DisableReason disable_reason = disable_reason::DISABLE_NONE;
    if (policy->MustRemainDisabled(extension.get(), &disable_reason)) {
      DCHECK_NE(disable_reason, disable_reason::DISABLE_NONE);
      extension_prefs_->AddDisableReason(extension->id(), disable_reason);
    }
  }

  if (write_to_prefs) {
    extension_prefs_->UpdateManifest(extension.get());
  }

  ExtensionRegistrar::Get(profile_)->AddExtension(extension.get());
}

void InstalledLoader::LoadAllExtensions() {
  LoadAllExtensions(profile_);
}

void InstalledLoader::LoadAllExtensions(Profile* profile) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  TRACE_EVENT0("browser,startup", "InstalledLoader::LoadAllExtensions");

  bool is_user_profile =
      profile_util::ProfileCanUseNonComponentExtensions(profile);
  const base::TimeTicks load_start_time = base::TimeTicks::Now();

  ExtensionPrefs::ExtensionsInfo extensions_info =
      extension_prefs_->GetInstalledExtensionsInfo();

  bool should_write_prefs = false;

  for (auto& info : extensions_info) {
    // Skip extensions that were loaded from the command-line because we don't
    // want those to persist across browser restart.
    if (info.extension_location == mojom::ManifestLocation::kCommandLine) {
      continue;
    }

    if (ShouldReloadExtensionManifest(info)) {
      // Reloading an extension reads files from disk.  We do this on the
      // UI thread because reloads should be very rare, and the complexity
      // added by delaying the time when the extensions service knows about
      // all extensions is significant.  See crbug.com/37548 for details.
      // |allow_blocking| disables tests that file operations run on the file
      // thread.
      base::ScopedAllowBlocking allow_blocking;

      std::string error;
      scoped_refptr<const Extension> extension(
          file_util::LoadExtension(info.extension_path, info.extension_location,
                                   GetCreationFlags(&info), &error));

      if (!extension.get() || extension->id() != info.extension_id) {
        invalid_extensions_.insert(info.extension_path);
        LoadErrorReporter::GetInstance()->ReportLoadError(info.extension_path,
                                                          error, profile,
                                                          false);  // Be quiet.
        continue;
      }

      info.extension_manifest = std::make_unique<base::Value::Dict>(
          extension->manifest()->value()->Clone());
      should_write_prefs = true;
    }
  }

  for (const auto& info : extensions_info) {
    if (info.extension_location != mojom::ManifestLocation::kCommandLine) {
      Load(info, should_write_prefs);
    }
  }

  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadAll",
                           extension_registry_->enabled_extensions().size());
  UMA_HISTOGRAM_COUNTS_100("Extensions.Disabled",
                           extension_registry_->disabled_extensions().size());
  if (is_user_profile) {
    UMA_HISTOGRAM_COUNTS_100("Extensions.LoadAll2",
                             extension_registry_->enabled_extensions().size());
    UMA_HISTOGRAM_COUNTS_100("Extensions.Disabled2",
                             extension_registry_->disabled_extensions().size());
  }

  RecordExtensionsMetrics(profile, is_user_profile);

  const base::TimeDelta load_all_time =
      base::TimeTicks::Now() - load_start_time;
  UMA_HISTOGRAM_TIMES("Extensions.LoadAllTime2", load_all_time);
  if (is_user_profile) {
    UMA_HISTOGRAM_TIMES("Extensions.LoadAllTime2.User", load_all_time);
  } else {
    UMA_HISTOGRAM_TIMES("Extensions.LoadAllTime2.NonUser", load_all_time);
  }
}

// static
void InstalledLoader::RecordPermissionMessagesHistogram(
    const Extension* extension,
    const char* histogram_basename,
    bool log_user_profile_histograms) {
  PermissionIDSet permissions =
      PermissionMessageProvider::Get()->GetAllPermissionIDs(
          extension->permissions_data()->active_permissions(),
          extension->GetType());
  base::UmaHistogramBoolean(
      base::StringPrintf("Extensions.HasPermissions_%s3", histogram_basename),
      !permissions.empty());

  std::string permissions_histogram_name =
      base::StringPrintf("Extensions.Permissions_%s3", histogram_basename);
  for (const PermissionID& id : permissions) {
    base::UmaHistogramEnumeration(permissions_histogram_name, id.id());
  }

  if (log_user_profile_histograms) {
    base::UmaHistogramBoolean(
        base::StringPrintf("Extensions.HasPermissions_%s4", histogram_basename),
        !permissions.empty());

    std::string permissions_histogram_name_incremented =
        base::StringPrintf("Extensions.Permissions_%s4", histogram_basename);
    for (const PermissionID& id : permissions) {
      base::UmaHistogramEnumeration(permissions_histogram_name_incremented,
                                    id.id());
    }
  }
}

void InstalledLoader::RecordExtensionsMetricsForTesting() {
  RecordExtensionsMetrics(profile_,
                          /*is_user_profile=*/false);
}

void InstalledLoader::RecordExtensionsIncrementedMetricsForTesting(
    Profile* profile) {
  LoadAllExtensions(profile);
}

// TODO(crbug.com/40739895): Separate out Webstore/Offstore metrics.
void InstalledLoader::RecordExtensionsMetrics(Profile* profile,
                                              bool is_user_profile) {
  int app_user_count = 0;
  int app_external_count = 0;
  int hosted_app_count = 0;
  int legacy_packaged_app_count = 0;
  int platform_app_count = 0;
  int user_script_count = 0;
  int extension_user_count = 0;
  int extension_external_count = 0;
  int theme_count = 0;
  int page_action_count = 0;
  int browser_action_count = 0;
  int no_action_count = 0;
  int disabled_for_permissions_count = 0;
  int non_webstore_ntp_override_count = 0;
  int ntp_override_count = 0;
  int homepage_override_count = 0;
  int search_engine_override_count = 0;
  int startup_pages_override_count = 0;
  int incognito_allowed_count = 0;
  int incognito_not_allowed_count = 0;
  int file_access_allowed_count = 0;
  int file_access_not_allowed_count = 0;
  int eventless_event_pages_count = 0;
  int off_store_item_count = 0;
  int web_request_blocking_count = 0;
  int web_request_count = 0;
  int enabled_not_allowlisted_count = 0;
  int disabled_not_allowlisted_count = 0;

  struct ManifestVersion2And3Counts {
    int version_2_count = 0;
    int version_3_count = 0;
  };

  ManifestVersion2And3Counts internal_manifest_version_counts;
  ManifestVersion2And3Counts external_manifest_version_counts;
  ManifestVersion2And3Counts policy_manifest_version_counts;
  ManifestVersion2And3Counts component_manifest_version_counts;
  ManifestVersion2And3Counts unpacked_manifest_version_counts;

  bool should_record_incremented_metrics = is_user_profile;
  bool dev_mode_enabled =
      GetCurrentDeveloperMode(util::GetBrowserContextId(profile));

  const ExtensionSet& extensions = extension_registry_->enabled_extensions();
  for (ExtensionSet::const_iterator iter = extensions.begin();
       iter != extensions.end();
       ++iter) {
    const Extension* extension = iter->get();
    mojom::ManifestLocation location = extension->location();
    Manifest::Type type = extension->GetType();

    // For the first few metrics, include all extensions and apps (component,
    // unpacked, etc). It's good to know these locations, and it doesn't
    // muck up any of the stats. Later, though, we want to omit component and
    // unpacked, as they are less interesting.

    if (extension->is_app() && should_record_incremented_metrics) {
      UMA_HISTOGRAM_ENUMERATION("Extensions.AppLocation2", location);
    } else if (extension->is_extension()) {
      UMA_HISTOGRAM_ENUMERATION("Extensions.ExtensionLocation", location);
      if (should_record_incremented_metrics) {
        UMA_HISTOGRAM_ENUMERATION("Extensions.ExtensionLocation2", location);
      }
    }
    if (!UpdatesFromWebstore(*extension)) {
      UMA_HISTOGRAM_ENUMERATION("Extensions.NonWebstoreLocation", location);
      if (should_record_incremented_metrics) {
        UMA_HISTOGRAM_ENUMERATION("Extensions.NonWebstoreLocation2", location);
      }

      // Check for inconsistencies if the extension was supposedly installed
      // from the webstore.
      enum {
        kBadUpdateUrl = 0,
        // This value was a mistake. Turns out sideloaded extensions can
        // have the from_webstore bit if they update from the webstore.
        kDeprecatedIsExternal = 1,
      };
      if (extension->from_webstore()) {
        UMA_HISTOGRAM_ENUMERATION("Extensions.FromWebstoreInconsistency",
                                  kBadUpdateUrl, 2);
        if (should_record_incremented_metrics) {
          UMA_HISTOGRAM_ENUMERATION("Extensions.FromWebstoreInconsistency2",
                                    kBadUpdateUrl, 2);
        }
      } else if (is_user_profile) {
        // Record enabled non-webstore extensions based on developer mode
        // status.
        if (dev_mode_enabled) {
          base::UmaHistogramEnumeration(
              "Extensions.NonWebstoreLocationWithDeveloperModeOn.Enabled3",
              location);
        } else {
          base::UmaHistogramEnumeration(
              "Extensions.NonWebstoreLocationWithDeveloperModeOff.Enabled3",
              location);
        }
      }
    }

    if (is_user_profile) {
      base::UmaHistogramBoolean("Extensions.DeveloperModeEnabled",
                                dev_mode_enabled);
    }

    if (Manifest::IsExternalLocation(location)) {
      // See loop below for DISABLED.
      if (UpdatesFromWebstore(*extension)) {
        base::UmaHistogramEnumeration("Extensions.ExternalItemState",
                                      ExternalItemState::kWebstoreEnabled);
        if (should_record_incremented_metrics) {
          base::UmaHistogramEnumeration("Extensions.ExternalItemState2",
                                        ExternalItemState::kWebstoreEnabled);
        }
      } else {
        base::UmaHistogramEnumeration("Extensions.ExternalItemState",
                                      ExternalItemState::kNonwebstoreEnabled);
        if (should_record_incremented_metrics) {
          base::UmaHistogramEnumeration("Extensions.ExternalItemState2",
                                        ExternalItemState::kNonwebstoreEnabled);
        }
      }
    }

    if (extension->permissions_data()->HasAPIPermission(
            mojom::APIPermissionID::kWebRequestBlocking)) {
      web_request_blocking_count++;
    }

    if (extension->permissions_data()->HasAPIPermission(
            mojom::APIPermissionID::kWebRequest)) {
      web_request_count++;
    }

    // 10 is arbitrarily chosen.
    static constexpr int kMaxManifestVersion = 10;
    // ManifestVersion split by location for items of type
    // Manifest::TYPE_EXTENSION. An ungrouped histogram is below, includes all
    // extension-y types (such as platform apps and hosted apps), and doesn't
    // include unpacked or component locations.
    if (extension->is_extension() && is_user_profile) {
      const char* location_histogram_name = nullptr;
      ManifestVersion2And3Counts* manifest_version_counts = nullptr;
      switch (extension->location()) {
        case mojom::ManifestLocation::kInternal:
          location_histogram_name =
              "Extensions.ManifestVersionByLocation.Internal";
          manifest_version_counts = &internal_manifest_version_counts;
          break;
        case mojom::ManifestLocation::kExternalPref:
        case mojom::ManifestLocation::kExternalPrefDownload:
        case mojom::ManifestLocation::kExternalRegistry:
          location_histogram_name =
              "Extensions.ManifestVersionByLocation.External";
          manifest_version_counts = &external_manifest_version_counts;
          break;
        case mojom::ManifestLocation::kComponent:
        case mojom::ManifestLocation::kExternalComponent:
          location_histogram_name =
              "Extensions.ManifestVersionByLocation.Component";
          manifest_version_counts = &component_manifest_version_counts;
          break;
        case mojom::ManifestLocation::kExternalPolicy:
        case mojom::ManifestLocation::kExternalPolicyDownload:
          location_histogram_name =
              "Extensions.ManifestVersionByLocation.Policy";
          manifest_version_counts = &policy_manifest_version_counts;
          break;
        case mojom::ManifestLocation::kCommandLine:
        case mojom::ManifestLocation::kUnpacked:
          location_histogram_name =
              "Extensions.ManifestVersionByLocation.Unpacked";
          manifest_version_counts = &unpacked_manifest_version_counts;
          break;
        case mojom::ManifestLocation::kInvalidLocation:
          NOTREACHED();
      }
      base::UmaHistogramExactLinear(location_histogram_name,
                                    extension->manifest_version(),
                                    kMaxManifestVersion);
      if (extension->manifest_version() == 2) {
        manifest_version_counts->version_2_count++;
      } else if (extension->manifest_version() == 3) {
        manifest_version_counts->version_3_count++;
      }
      // Report the days since the extension was installed.
      base::Time time_since_install =
          GetFirstInstallTime(extension_prefs_, extension->id());
      if (!time_since_install.is_null()) {
        int days_since_install =
            (base::Time::Now() - time_since_install).InDays();
        UMA_HISTOGRAM_CUSTOM_COUNTS("Extensions.DaysSinceInstall",
                                    days_since_install, 0, 5000, 91);
      }
      // Report the days since the extension was last updated.
      base::Time time_since_last_update =
          GetLastUpdateTime(extension_prefs_, extension->id());
      if (!time_since_last_update.is_null()) {
        int days_since_updated =
            (base::Time::Now() - time_since_last_update).InDays();
        UMA_HISTOGRAM_CUSTOM_COUNTS("Extensions.DaysSinceLastUpdate",
                                    days_since_updated, 0, 5000, 91);
      }
    }

    // From now on, don't count component extensions, since they are only
    // extensions as an implementation detail. Continue to count unpacked
    // extensions for a few metrics.
    if (Manifest::IsComponentLocation(location)) {
      continue;
    }

    // Histogram for extensions overriding the new tab page should include
    // unpacked extensions.
    if (URLOverrides::GetChromeURLOverrides(extension).count("newtab")) {
      ++ntp_override_count;
      if (!extension->from_webstore()) {
        ++non_webstore_ntp_override_count;
      }
    }

    // Histogram for extensions with settings overrides.
    const SettingsOverrides* settings = SettingsOverrides::Get(extension);
    if (settings) {
      if (settings->search_engine) {
        ++search_engine_override_count;
      }
      if (!settings->startup_pages.empty()) {
        ++startup_pages_override_count;
      }
      if (settings->homepage) {
        ++homepage_override_count;
      }
    }

    // Don't count unpacked extensions anymore, either.
    if (Manifest::IsUnpackedLocation(location)) {
      continue;
    }

    if (should_record_incremented_metrics) {
      UMA_HISTOGRAM_ENUMERATION("Extensions.ManifestVersion2",
                                extension->manifest_version(),
                                kMaxManifestVersion);
    }

    // We might have wanted to count legacy packaged apps here, too, since they
    // are effectively extensions. Unfortunately, it's too late, as we don't
    // want to mess up the existing stats.
    if (type == Manifest::TYPE_EXTENSION) {
      base::UmaHistogramEnumeration("Extensions.BackgroundPageType",
                                    GetBackgroundPageType(extension));
      if (should_record_incremented_metrics) {
        base::UmaHistogramEnumeration("Extensions.BackgroundPageType2",
                                      GetBackgroundPageType(extension));
      }

      if (GetBackgroundPageType(extension) == BackgroundPageType::kEventPage) {
        // Count extension event pages with no registered events. Either the
        // event page is badly designed, or there may be a bug where the event
        // page failed to start after an update (crbug.com/469361).
        if (!EventRouter::Get(profile_)->HasRegisteredEvents(extension->id())) {
          ++eventless_event_pages_count;
          VLOG(1) << "Event page without registered event listeners: "
                  << extension->id() << " " << extension->name();
        }
      }
    }

    // Using an enumeration shows us the total installed ratio across all users.
    // Using the totals per user at each startup tells us the distribution of
    // usage for each user (e.g. 40% of users have at least one app installed).
    UMA_HISTOGRAM_ENUMERATION(
        "Extensions.LoadType", type, Manifest::NUM_LOAD_TYPES);
    if (should_record_incremented_metrics) {
      UMA_HISTOGRAM_ENUMERATION("Extensions.LoadType2", type,
                                Manifest::NUM_LOAD_TYPES);
    }
    switch (type) {
      case Manifest::TYPE_THEME:
        ++theme_count;
        break;
      case Manifest::TYPE_USER_SCRIPT:
        ++user_script_count;
        break;
      case Manifest::TYPE_HOSTED_APP:
        ++hosted_app_count;
        if (Manifest::IsExternalLocation(location)) {
          ++app_external_count;
        } else {
          ++app_user_count;
        }
        break;
      case Manifest::TYPE_LEGACY_PACKAGED_APP:
        ++legacy_packaged_app_count;
        if (Manifest::IsExternalLocation(location)) {
          ++app_external_count;
        } else {
          ++app_user_count;
        }
        break;
      case Manifest::TYPE_PLATFORM_APP:
        ++platform_app_count;
        if (Manifest::IsExternalLocation(location)) {
          ++app_external_count;
        } else {
          ++app_user_count;
        }
        break;
      case Manifest::TYPE_EXTENSION:
      default:
        if (Manifest::IsExternalLocation(location)) {
          ++extension_external_count;
        } else {
          ++extension_user_count;
        }
        break;
    }

    // We check the manifest key (instead of the ExtensionActionManager) because
    // we want to know how many extensions have a given type of action as part
    // of their code, rather than as part of the extension action redesign
    // (which gives each extension an action).
    if (extension->manifest()->FindKey(manifest_keys::kPageAction)) {
      ++page_action_count;
    } else if (extension->manifest()->FindKey(manifest_keys::kBrowserAction)) {
      ++browser_action_count;
    } else {
      ++no_action_count;
    }

    RecordPermissionMessagesHistogram(extension, "Load",
                                      should_record_incremented_metrics);

    // For incognito and file access, skip anything that doesn't appear in
    // settings. Also, policy-installed (and unpacked of course, checked above)
    // extensions are boring.
    if (ui_util::ShouldDisplayInExtensionSettings(*extension) &&
        !Manifest::IsPolicyLocation(extension->location())) {
      if (util::CanBeIncognitoEnabled(extension)) {
        if (util::IsIncognitoEnabled(extension->id(), profile)) {
          ++incognito_allowed_count;
        } else {
          ++incognito_not_allowed_count;
        }
      }
      if (extension->wants_file_access()) {
        if (util::AllowFileAccess(extension->id(), profile)) {
          ++file_access_allowed_count;
        } else {
          ++file_access_not_allowed_count;
        }
      }
    }

    if (!UpdatesFromWebstore(*extension)) {
      ++off_store_item_count;
    }

    PermissionsManager* permissions_manager = PermissionsManager::Get(profile);
    // NOTE: CanAffectExtension() returns false in all cases when the
    // RuntimeHostPermissions feature is disabled.
    if (permissions_manager->CanAffectExtension(*extension)) {
      bool extension_has_withheld_hosts =
          permissions_manager->HasWithheldHostPermissions(*extension);
      UMA_HISTOGRAM_BOOLEAN(
          "Extensions.RuntimeHostPermissions.ExtensionHasWithheldHosts",
          extension_has_withheld_hosts);
      if (should_record_incremented_metrics) {
        UMA_HISTOGRAM_BOOLEAN(
            "Extensions.RuntimeHostPermissions.ExtensionHasWithheldHosts2",
            extension_has_withheld_hosts);
      }
      if (extension_has_withheld_hosts) {
        // Record the number of granted hosts if and only if the extension
        // has withheld host permissions. This lets us equate "0" granted
        // hosts to "on click only".
        size_t num_granted_hosts = 0;
        for (const auto& pattern : extension->permissions_data()
                                       ->active_permissions()
                                       .effective_hosts()) {
          // Ignore chrome:-scheme patterns (like chrome://favicon); these
          // aren't withheld, and thus shouldn't be considered "granted".
          if (pattern.scheme() != content::kChromeUIScheme) {
            ++num_granted_hosts;
          }
        }
        // TODO(devlin): This only takes into account the granted hosts that
        // were also requested by the extension (because it looks at the active
        // permissions). We could potentially also record the granted hosts that
        // were explicitly not requested.
        UMA_HISTOGRAM_COUNTS_100(
            "Extensions.RuntimeHostPermissions.GrantedHostCount",
            num_granted_hosts);
        if (should_record_incremented_metrics) {
          UMA_HISTOGRAM_COUNTS_100(
              "Extensions.RuntimeHostPermissions.GrantedHostCount2",
              num_granted_hosts);
        }
      }
    }

    LogHostPermissionsAccess(*extension, should_record_incremented_metrics);

    if (ExtensionAllowlist::Get(profile)->GetExtensionAllowlistState(
            extension->id()) == ALLOWLIST_NOT_ALLOWLISTED) {
      // Record the number of not allowlisted enabled extensions.
      ++enabled_not_allowlisted_count;
    }
  }

  const ExtensionSet& disabled_extensions =
      extension_registry_->disabled_extensions();

  for (const scoped_refptr<const Extension>& disabled_extension :
       disabled_extensions) {
    mojom::ManifestLocation location = disabled_extension->location();
    if (extension_prefs_->DidExtensionEscalatePermissions(
            disabled_extension->id())) {
      ++disabled_for_permissions_count;
    }
    if (should_record_incremented_metrics) {
      RecordDisableReasons(
          extension_prefs_->GetDisableReasons(disabled_extension->id()));
    }
    if (Manifest::IsExternalLocation(location)) {
      // See loop above for ENABLED.
      if (UpdatesFromWebstore(*disabled_extension)) {
        base::UmaHistogramEnumeration("Extensions.ExternalItemState",
                                      ExternalItemState::kWebstoreDisabled);
        if (should_record_incremented_metrics) {
          base::UmaHistogramEnumeration("Extensions.ExternalItemState2",
                                        ExternalItemState::kWebstoreDisabled);
        }
      } else {
        base::UmaHistogramEnumeration("Extensions.ExternalItemState",
                                      ExternalItemState::kNonwebstoreDisabled);
        if (should_record_incremented_metrics) {
          base::UmaHistogramEnumeration(
              "Extensions.ExternalItemState2",
              ExternalItemState::kNonwebstoreDisabled);
        }
      }
    }

    // Record disabled non-webstore extensions based on developer mode status.
    if (is_user_profile && !UpdatesFromWebstore(*disabled_extension) &&
        !disabled_extension->from_webstore()) {
      if (dev_mode_enabled) {
        base::UmaHistogramEnumeration(
            "Extensions.NonWebstoreLocationWithDeveloperModeOn.Disabled3",
            location);
      } else {
        base::UmaHistogramEnumeration(
            "Extensions.NonWebstoreLocationWithDeveloperModeOff.Disabled3",
            location);
      }
    }

    if (ExtensionAllowlist::Get(profile)->GetExtensionAllowlistState(
            disabled_extension->id()) == ALLOWLIST_NOT_ALLOWLISTED) {
      // Record the number of not allowlisted disabled extensions.
      ++disabled_not_allowlisted_count;
    }
  }

  if (is_user_profile) {
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion2Count.Internal",
        internal_manifest_version_counts.version_2_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion3Count.Internal",
        internal_manifest_version_counts.version_3_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion2Count.External",
        external_manifest_version_counts.version_2_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion3Count.External",
        external_manifest_version_counts.version_3_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion2Count.Component",
        component_manifest_version_counts.version_2_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion3Count.Component",
        component_manifest_version_counts.version_3_count);
    base::UmaHistogramCounts100("Extensions.ManifestVersion2Count.Policy",
                                policy_manifest_version_counts.version_2_count);
    base::UmaHistogramCounts100("Extensions.ManifestVersion3Count.Policy",
                                policy_manifest_version_counts.version_3_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion2Count.Unpacked",
        unpacked_manifest_version_counts.version_2_count);
    base::UmaHistogramCounts100(
        "Extensions.ManifestVersion3Count.Unpacked",
        unpacked_manifest_version_counts.version_3_count);

    auto get_manifest_version_population_split =
        [](const ManifestVersion2And3Counts& counts) {
          if (counts.version_2_count == 0 && counts.version_3_count == 0) {
            return ManifestVersionPopulationSplit::kNoExtensions;
          }
          if (counts.version_2_count > 0 && counts.version_3_count == 0) {
            return ManifestVersionPopulationSplit::kMv2ExtensionsOnly;
          }
          if (counts.version_3_count > 0 && counts.version_2_count == 0) {
            return ManifestVersionPopulationSplit::kMv3ExtensionsOnly;
          }
          return ManifestVersionPopulationSplit::kMv2AndMv3Extensions;
        };
    base::UmaHistogramEnumeration(
        "Extensions.ManifestVersionPopulationSplit.Internal",
        get_manifest_version_population_split(
            internal_manifest_version_counts));
    base::UmaHistogramEnumeration(
        "Extensions.ManifestVersionPopulationSplit.External",
        get_manifest_version_population_split(
            external_manifest_version_counts));
    base::UmaHistogramEnumeration(
        "Extensions.ManifestVersionPopulationSplit.Component",
        get_manifest_version_population_split(
            component_manifest_version_counts));
    base::UmaHistogramEnumeration(
        "Extensions.ManifestVersionPopulationSplit.Unpacked",
        get_manifest_version_population_split(
            unpacked_manifest_version_counts));
    ManifestVersion2And3Counts internal_and_external_counts;
    internal_and_external_counts.version_2_count =
        internal_manifest_version_counts.version_2_count +
        external_manifest_version_counts.version_2_count;
    internal_and_external_counts.version_3_count =
        internal_manifest_version_counts.version_3_count +
        external_manifest_version_counts.version_3_count;
    // We log an additional one for the combination of internal and external
    // since these are both "user controlled" and not unpacked.
    base::UmaHistogramEnumeration(
        "Extensions.ManifestVersionPopulationSplit.InternalAndExternal",
        get_manifest_version_population_split(
            internal_manifest_version_counts));
  }

  base::UmaHistogramCounts100("Extensions.LoadApp",
                              app_user_count + app_external_count);
  base::UmaHistogramCounts100("Extensions.LoadAppUser", app_user_count);
  base::UmaHistogramCounts100("Extensions.LoadAppExternal", app_external_count);
  base::UmaHistogramCounts100("Extensions.LoadHostedApp", hosted_app_count);
  base::UmaHistogramCounts100("Extensions.LoadPackagedApp",
                              legacy_packaged_app_count);
  base::UmaHistogramCounts100("Extensions.LoadPlatformApp", platform_app_count);
  base::UmaHistogramCounts100("Extensions.LoadExtension",
                              extension_user_count + extension_external_count);
  base::UmaHistogramCounts100("Extensions.LoadExtensionExternal",
                              extension_external_count);
  base::UmaHistogramCounts100("Extensions.LoadTheme", theme_count);
  // Histogram name different for legacy reasons.
  base::UmaHistogramCounts100("PageActionController.ExtensionsWithPageActions",
                              page_action_count);
  base::UmaHistogramCounts100("Extensions.LoadBrowserAction",
                              browser_action_count);
  base::UmaHistogramCounts100("Extensions.LoadNoExtensionAction",
                              no_action_count);
  base::UmaHistogramCounts100("Extensions.DisabledForPermissions",
                              disabled_for_permissions_count);
  base::UmaHistogramCounts100("Extensions.NonWebStoreNewTabPageOverrides",
                              non_webstore_ntp_override_count);
  base::UmaHistogramCounts100("Extensions.NewTabPageOverrides",
                              ntp_override_count);
  base::UmaHistogramCounts100("Extensions.SearchEngineOverrides",
                              search_engine_override_count);
  base::UmaHistogramCounts100("Extensions.StartupPagesOverrides",
                              startup_pages_override_count);
  base::UmaHistogramCounts100("Extensions.HomepageOverrides",
                              homepage_override_count);
  if (should_record_incremented_metrics) {
    base::UmaHistogramCounts100("Extensions.LoadApp2",
                                app_user_count + app_external_count);
    base::UmaHistogramCounts100("Extensions.LoadAppUser2", app_user_count);
    base::UmaHistogramCounts100("Extensions.LoadAppExternal2",
                                app_external_count);
    base::UmaHistogramCounts100("Extensions.LoadHostedApp2", hosted_app_count);
    base::UmaHistogramCounts100("Extensions.LoadPackagedApp2",
                                legacy_packaged_app_count);
    base::UmaHistogramCounts100("Extensions.LoadPlatformApp2",
                                platform_app_count);
    base::UmaHistogramCounts100(
        "Extensions.LoadExtension2",
        extension_user_count + extension_external_count);
    base::UmaHistogramCounts100("Extensions.LoadExtensionUser2",
                                extension_user_count);
    base::UmaHistogramCounts100("Extensions.LoadExtensionExternal2",
                                extension_external_count);
    base::UmaHistogramCounts100("Extensions.LoadUserScript2",
                                user_script_count);
    base::UmaHistogramCounts100("Extensions.LoadTheme2", theme_count);
    base::UmaHistogramCounts100("Extensions.ExtensionsWithPageActions",
                                page_action_count);
    base::UmaHistogramCounts100("Extensions.LoadBrowserAction2",
                                browser_action_count);
    base::UmaHistogramCounts100("Extensions.LoadNoExtensionAction2",
                                no_action_count);
    base::UmaHistogramCounts100("Extensions.DisabledForPermissions2",
                                disabled_for_permissions_count);
    base::UmaHistogramCounts100("Extensions.NonWebStoreNewTabPageOverrides2",
                                non_webstore_ntp_override_count);
    base::UmaHistogramCounts100("Extensions.NewTabPageOverrides2",
                                ntp_override_count);
    base::UmaHistogramCounts100("Extensions.SearchEngineOverrides2",
                                search_engine_override_count);
    base::UmaHistogramCounts100("Extensions.StartupPagesOverrides2",
                                startup_pages_override_count);
    base::UmaHistogramCounts100("Extensions.HomepageOverrides2",
                                homepage_override_count);
  }

  if (incognito_allowed_count + incognito_not_allowed_count > 0) {
    base::UmaHistogramCounts100("Extensions.IncognitoAllowed",
                                incognito_allowed_count);
    if (should_record_incremented_metrics) {
      base::UmaHistogramCounts100("Extensions.IncognitoAllowed2",
                                  incognito_allowed_count);
    }
  }
  if (file_access_allowed_count + file_access_not_allowed_count > 0 &&
      should_record_incremented_metrics) {
    base::UmaHistogramCounts100("Extensions.FileAccessAllowed2",
                                file_access_allowed_count);
    base::UmaHistogramCounts100("Extensions.FileAccessNotAllowed2",
                                file_access_not_allowed_count);
  }
  base::UmaHistogramCounts100(
      "Extensions.CorruptExtensionTotalDisables",
      extension_prefs_->GetPrefAsInteger(kCorruptedDisableCount));
  base::UmaHistogramCounts100("Extensions.LoadOffStoreItems",
                              off_store_item_count);
  base::UmaHistogramCounts100("Extensions.WebRequestBlockingCount",
                              web_request_blocking_count);
  base::UmaHistogramCounts100("Extensions.WebRequestCount", web_request_count);
  base::UmaHistogramCounts100("Extensions.NotAllowlistedEnabled",
                              enabled_not_allowlisted_count);
  base::UmaHistogramCounts100("Extensions.NotAllowlistedDisabled",
                              disabled_not_allowlisted_count);

  if (should_record_incremented_metrics) {
    base::UmaHistogramCounts100(
        "Extensions.CorruptExtensionTotalDisables2",
        extension_prefs_->GetPrefAsInteger(kCorruptedDisableCount));
    base::UmaHistogramCounts100("Extensions.EventlessEventPages2",
                                eventless_event_pages_count);
    base::UmaHistogramCounts100("Extensions.LoadOffStoreItems2",
                                off_store_item_count);
    base::UmaHistogramCounts100("Extensions.WebRequestBlockingCount2",
                                web_request_blocking_count);
    base::UmaHistogramCounts100("Extensions.WebRequestCount2",
                                web_request_count);
    base::UmaHistogramCounts100("Extensions.NotAllowlistedEnabled2",
                                enabled_not_allowlisted_count);
    base::UmaHistogramCounts100("Extensions.NotAllowlistedDisabled2",
                                disabled_not_allowlisted_count);
  }
  if (safe_browsing::IsEnhancedProtectionEnabled(*profile->GetPrefs())) {
    base::UmaHistogramCounts100("Extensions.NotAllowlistedEnabledAndEsbUser",
                                enabled_not_allowlisted_count);
    base::UmaHistogramCounts100("Extensions.NotAllowlistedDisabledAndEsbUser",
                                disabled_not_allowlisted_count);
    if (should_record_incremented_metrics) {
      base::UmaHistogramCounts100("Extensions.NotAllowlistedEnabledAndEsbUser2",
                                  enabled_not_allowlisted_count);
      base::UmaHistogramCounts100(
          "Extensions.NotAllowlistedDisabledAndEsbUser2",
          disabled_not_allowlisted_count);
    }
  }
}

int InstalledLoader::GetCreationFlags(const ExtensionInfo* info) {
  int flags = extension_prefs_->GetCreationFlags(info->extension_id);
  if (!Manifest::IsUnpackedLocation(info->extension_location)) {
    flags |= Extension::REQUIRE_KEY;
  }
  // Use the AllowFileAccess pref as the source of truth for file access,
  // rather than any previously stored creation flag.
  flags &= ~Extension::ALLOW_FILE_ACCESS;
  if (extension_prefs_->AllowFileAccess(info->extension_id)) {
    flags |= Extension::ALLOW_FILE_ACCESS;
  }
  return flags;
}

void InstalledLoader::HandleCorruptExtension(const Extension& extension,
                                             const ManagementPolicy& policy) {
  CorruptedExtensionReinstaller* corrupted_extension_reinstaller =
      CorruptedExtensionReinstaller::Get(profile_);
  if (policy.MustRemainEnabled(&extension, nullptr)) {
    // This extension must have been disabled due to corruption on a
    // previous run of chrome, and for some reason we weren't successful in
    // auto-reinstalling it. So we want to notify the reinstaller that we'd
    // still like to keep attempt to re-download and reinstall it whenever
    // the ExtensionService checks for external updates.
    LOG(ERROR) << "Expecting reinstall for extension id: " << extension.id()
               << " due to corruption detected in prior session.";
    corrupted_extension_reinstaller->ExpectReinstallForCorruption(
        extension.id(),
        CorruptedExtensionReinstaller::PolicyReinstallReason::
            CORRUPTION_DETECTED_IN_PRIOR_SESSION,
        extension.location());
  } else if (extension.from_webstore()) {
    // Non-policy extensions are repaired on startup. Add any corrupted
    // user-installed extensions to the reinstaller as well.
    corrupted_extension_reinstaller->ExpectReinstallForCorruption(
        extension.id(), std::nullopt, extension.location());
  }
}

bool InstalledLoader::UpdatesFromWebstore(const Extension& extension) {
  return extension_management_->UpdatesFromWebstore(extension);
}

}  // namespace extensions
