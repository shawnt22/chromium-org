// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_util.h"

#include <string_view>
#include <vector>

#include "base/check_is_test.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/extension_sync_service.h"
#include "chrome/browser/extensions/permissions/permissions_updater.h"
#include "chrome/browser/extensions/shared_module_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/common/extensions/api/url_handlers/url_handlers_parser.h"
#include "chrome/common/extensions/sync_helper.h"
#include "chrome/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/site_instance.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/pref_names.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/browser/updater/scoped_extension_updater_keep_alive.h"
#include "extensions/browser/user_script_manager.h"
#include "extensions/common/extension.h"
#include "extensions/common/features/feature_developer_mode_only.h"
#include "extensions/common/icons/extension_icon_set.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/switches.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_elider.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chromeos/ash/components/file_manager/app_id.h"
#endif

#if !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/browser.h"
#endif

namespace extensions::util {

namespace {

bool ExtensionsDisabledViaCommandLine(const base::CommandLine& command_line) {
  return command_line.HasSwitch(switches::kDisableExtensions) ||
         command_line.HasSwitch(switches::kDisableExtensionsExcept);
}

// Returns |extension_id|. See note below.
std::string ReloadExtension(const std::string& extension_id,
                            content::BrowserContext* context) {
  // When we reload the extension the ID may be invalidated if we've passed it
  // by const ref everywhere. Make a copy to be safe. http://crbug.com/103762
  std::string id = extension_id;
  ExtensionRegistrar::Get(context)->ReloadExtension(extension_id);
  return id;
}

std::string ReloadExtensionIfEnabled(const std::string& extension_id,
                                     content::BrowserContext* context) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  bool extension_is_enabled =
      registry->enabled_extensions().Contains(extension_id);

  if (!extension_is_enabled) {
    return extension_id;
  }
  return ReloadExtension(extension_id, context);
}

#if BUILDFLAG(IS_CHROMEOS)
// Returns true if the extension ID is found in the InstallForceList policy. Is
// checked by HasIsolatedStorage() when the extension is not found in the
// registry.
bool IsForceInstalledExtension(const ExtensionId& extension_id,
                               content::BrowserContext* context) {
  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(context);
  const PrefService::Preference* const pref =
      extension_prefs->pref_service()->FindPreference(
          pref_names::kInstallForceList);
  if (!pref || !pref->IsManaged() ||
      pref->GetType() != base::Value::Type::DICT) {
    return false;
  }
  for (const auto item : pref->GetValue()->GetDict()) {
    if (extension_id == item.first) {
      return true;
    }
  }
  return false;
}
#endif  // BUILDFLAG(IS_CHROMEOS)

// Returns true if the profile is a sign-in profile and the extension is policy
// installed. `is_policy_installed` can be passed to the method if its value is
// known (i.e. the extension was found in the registry and the extension
// location was checked). If no value is passed for `is_policy_installed`, the
// force-installed list will be queried for the extension ID.
bool IsLoginScreenExtension(
    ExtensionId extension_id,
    content::BrowserContext* context,
    std::optional<bool> is_policy_installed = std::nullopt) {
#if BUILDFLAG(IS_CHROMEOS)
  // Verify the force-installed extension list if no value for
  // `is_policy_installed` was passed.
  if (is_policy_installed == std::nullopt) {
    is_policy_installed = IsForceInstalledExtension(extension_id, context);
  }
  Profile* profile = Profile::FromBrowserContext(context);
  return profile && ash::ProfileHelper::IsSigninProfile(profile) &&
         is_policy_installed.value();
#else
  return false;
#endif
}

#if BUILDFLAG(IS_CHROMEOS)
bool IsPolicyInstalled(const ExtensionId& extension_id,
                       content::BrowserContext* context) {
  const Extension* extension =
      ExtensionRegistry::Get(context)->GetInstalledExtension(extension_id);
  if (!extension) {
    return false;
  }

  return Manifest::IsPolicyLocation(extension->location());
}
#endif
}  // namespace

bool HasIsolatedStorage(const ExtensionId& extension_id,
                        content::BrowserContext* context) {
  const Extension* extension =
      ExtensionRegistry::Get(context)->GetInstalledExtension(extension_id);
  // Extension is null when the extension is cleaned up after it's unloaded and
  // won't be present in the ExtensionRegistry.
  return extension ? HasIsolatedStorage(*extension, context)
                   : IsLoginScreenExtension(extension_id, context);
}

bool HasIsolatedStorage(const Extension& extension,
                        content::BrowserContext* context) {
#if BUILDFLAG(IS_CHROMEOS)
  const bool is_policy_extension =
      Manifest::IsPolicyLocation(extension.location());
  if (IsLoginScreenExtension(extension.id(), context, is_policy_extension)) {
    return true;
  }
#endif

  return extension.is_platform_app();
}

void SetIsIncognitoEnabled(const std::string& extension_id,
                           content::BrowserContext* context,
                           bool enabled) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  const Extension* extension =
      registry->GetExtensionById(extension_id, ExtensionRegistry::EVERYTHING);

  if (extension) {
    if (!util::CanBeIncognitoEnabled(extension)) {
      return;
    }

    // TODO(crbug.com/356905053): Enable handling component extensions on
    // desktop android.
#if !BUILDFLAG(IS_ANDROID)
    // TODO(treib,kalman): Should this be Manifest::IsComponentLocation(..)?
    // (which also checks for kExternalComponent).
    if (extension->location() == mojom::ManifestLocation::kComponent) {
      // This shouldn't be called for component extensions unless it is called
      // by sync, for syncable component extensions.
      // See http://crbug.com/112290 and associated CLs for the sordid history.
      bool syncable = sync_helper::IsSyncableComponentExtension(extension);
#if BUILDFLAG(IS_CHROMEOS)
      // For some users, the file manager app somehow ended up being synced even
      // though it's supposed to be unsyncable; see crbug.com/576964. If the bad
      // data ever gets cleaned up, this hack should be removed.
      syncable = syncable || extension->id() == file_manager::kFileManagerAppId;
#endif
      DCHECK(syncable);

      // If we are here, make sure the we aren't trying to change the value.
      DCHECK_EQ(enabled, IsIncognitoEnabled(extension_id, context));
      return;
    }
#endif  // !BUILDFLAG(IS_ANDROID)
  }

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(context);
#if BUILDFLAG(IS_CHROMEOS)
  // Admin installed extensions should not be restartable, so we will apply the
  // change when Chrome restarts.
  if (IsPolicyInstalled(extension_id, context)) {
    extension_prefs->SetIsIncognitoEnabledDelayed(extension_id, enabled);
    return;
  }
#endif
  // Broadcast unloaded and loaded events to update browser state. Only bother
  // if the value changed and the extension is actually enabled, since there is
  // no UI otherwise.
  bool old_enabled = extension_prefs->IsIncognitoEnabled(extension_id);
  if (enabled == old_enabled) {
    return;
  }

  extension_prefs->SetIsIncognitoEnabled(extension_id, enabled);

  std::string id = ReloadExtensionIfEnabled(extension_id, context);

  // Reloading the extension invalidates the |extension| pointer.
  extension = registry->GetExtensionById(id, ExtensionRegistry::EVERYTHING);
  if (extension) {
    Profile* profile = Profile::FromBrowserContext(context);
    ExtensionSyncService::Get(profile)->SyncExtensionChangeIfNeeded(*extension);
  }
}

void SetAllowFileAccess(const std::string& extension_id,
                        content::BrowserContext* context,
                        bool allow) {
#if BUILDFLAG(IS_CHROMEOS)
  // Admin installed extensions should not be restartable, so we will apply the
  // change when Chrome restarts.
  if (IsPolicyInstalled(extension_id, context)) {
    ExtensionPrefs::Get(context)->SetAllowFileAccessDelayed(extension_id,
                                                            allow);
    return;
  }
#endif
  // Reload to update browser state if the value changed. We need to reload even
  // if the extension is disabled, in order to make sure file access is
  // reinitialized correctly.
  if (allow == util::AllowFileAccess(extension_id, context)) {
    return;
  }

  ExtensionPrefs::Get(context)->SetAllowFileAccess(extension_id, allow);

  ReloadExtension(extension_id, context);
}

base::Value::Dict GetExtensionInfo(const Extension* extension) {
  DCHECK(extension);
  base::Value::Dict dict;

  dict.Set("id", extension->id());
  dict.Set("name", extension->name());

  GURL icon = extensions::ExtensionIconSource::GetIconURL(
      extension, extension_misc::EXTENSION_ICON_SMALLISH,
      ExtensionIconSet::Match::kBigger,
      /*grayscale=*/false);
  dict.Set("icon", icon.spec());

  return dict;
}

std::unique_ptr<const PermissionSet> GetInstallPromptPermissionSetForExtension(
    const Extension* extension,
    Profile* profile) {
  // Initialize permissions if they have not already been set so that
  // any transformations are correctly reflected in the install prompt.
  PermissionsUpdater(profile, PermissionsUpdater::INIT_FLAG_TRANSIENT)
      .InitializePermissions(extension);

  return extension->permissions_data()->active_permissions().Clone();
}

std::vector<content::BrowserContext*> GetAllRelatedProfiles(
    Profile* profile,
    const Extension& extension) {
  std::vector<content::BrowserContext*> related_contexts;
  related_contexts.push_back(profile->GetOriginalProfile());

  // The returned `related_contexts` should include all the related incognito
  // profiles if the extension is globally allowed in incognito (this is a
  // global, rather than per-profile toggle - this is why we it can be checked
  // globally here, rather than once for every incognito profile looped over
  // below).
  if (IsIncognitoEnabled(extension.id(), profile)) {
    std::vector<Profile*> off_the_record_profiles =
        profile->GetAllOffTheRecordProfiles();
    related_contexts.reserve(related_contexts.size() +
                             off_the_record_profiles.size());
    for (Profile* off_the_record_profile : off_the_record_profiles)
      related_contexts.push_back(off_the_record_profile);
  }

  return related_contexts;
}

void SetDeveloperModeForProfile(Profile* profile, bool in_developer_mode) {
  profile->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode,
                                  in_developer_mode);
  SetCurrentDeveloperMode(util::GetBrowserContextId(profile),
                          in_developer_mode);
  RendererStartupHelperFactory::GetForBrowserContext(profile)
      ->OnDeveloperModeChanged(in_developer_mode);

  // kDynamicUserScript scripts are allowed if and only if the user is in dev
  // mode (since they allow raw code execution). Notify the user script manager
  // to properly enable or disable any scripts.
  UserScriptManager* user_script_manager =
      ExtensionSystem::Get(profile)->user_script_manager();
  if (!user_script_manager) {
    CHECK_IS_TEST();  // `user_script_manager` can be null in unit tests.
    return;
  }

  user_script_manager->SetUserScriptSourceEnabledForExtensions(
      UserScript::Source::kDynamicUserScript, in_developer_mode);
}

std::u16string GetFixupExtensionNameForUIDisplay(
    const std::u16string& extension_name) {
  const size_t extension_name_char_limit =
      75;  // Extension name char limit on CWS
  gfx::BreakType break_type = gfx::BreakType::CHARACTER_BREAK;
  std::u16string fixup_extension_name = gfx::TruncateString(
      extension_name, extension_name_char_limit, break_type);
  return fixup_extension_name;
}

std::u16string GetFixupExtensionNameForUIDisplay(
    const std::string& extension_name) {
  return GetFixupExtensionNameForUIDisplay(base::UTF8ToUTF16(extension_name));
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(prefs::kShouldGarbageCollectStoragePartitions,
                                false);
}

bool AreExtensionsDisabled(const base::CommandLine& command_line,
                           content::BrowserContext* context) {
  Profile* profile = Profile::FromBrowserContext(context);
  return ExtensionsDisabledViaCommandLine(command_line) ||
         profile->GetPrefs()->GetBoolean(prefs::kDisableExtensions);
}

} // namespace extensions::util
