// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/manifest_handlers/background_info.h"

#include <stddef.h>

#include <memory>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/file_util.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/permissions/api_permission_set.h"
#include "extensions/common/switches.h"
#include "extensions/strings/grit/extensions_strings.h"
#include "net/base/mime_util.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"
#include "ui/base/l10n/l10n_util.h"

using extensions::mojom::APIPermissionID;

namespace extensions {

namespace keys = manifest_keys;
namespace values = manifest_values;
namespace errors = manifest_errors;

namespace {

BASE_FEATURE(kValidateBackgroundScriptMimeType,
             "ValidateBackgroundScriptMimeType",
             base::FEATURE_ENABLED_BY_DEFAULT);

const char kBackground[] = "background";

static base::LazyInstance<BackgroundInfo>::DestructorAtExit
    g_empty_background_info = LAZY_INSTANCE_INITIALIZER;

const BackgroundInfo& GetBackgroundInfo(const Extension* extension) {
  BackgroundInfo* info = static_cast<BackgroundInfo*>(
      extension->GetManifestData(kBackground));
  if (!info) {
    return g_empty_background_info.Get();
  }
  return *info;
}

}  // namespace

BackgroundInfo::BackgroundInfo()
    : is_persistent_(true),
      allow_js_access_(true) {
}

BackgroundInfo::~BackgroundInfo() = default;

// static
GURL BackgroundInfo::GetBackgroundURL(const Extension* extension) {
  const BackgroundInfo& info = GetBackgroundInfo(extension);
  if (info.background_scripts_.empty()) {
    return info.background_url_;
  }
  return extension->ResolveExtensionURL(kGeneratedBackgroundPageFilename);
}

// static
const GURL& BackgroundInfo::GetBackgroundServiceWorkerScriptURL(
    const Extension* extension) {
  const BackgroundInfo& info = GetBackgroundInfo(extension);
  DCHECK(info.background_service_worker_script_url_.has_value());
  return *info.background_service_worker_script_url_;
}

// static
BackgroundServiceWorkerType BackgroundInfo::GetBackgroundServiceWorkerType(
    const Extension* extension) {
  const BackgroundInfo& info = GetBackgroundInfo(extension);
  return *info.background_service_worker_type_;
}

// static
const std::vector<std::string>& BackgroundInfo::GetBackgroundScripts(
    const Extension* extension) {
  return GetBackgroundInfo(extension).background_scripts_;
}

// static
bool BackgroundInfo::HasBackgroundPage(const Extension* extension) {
  return GetBackgroundInfo(extension).has_background_page();
}

// static
bool BackgroundInfo::HasPersistentBackgroundPage(const Extension* extension)  {
  return GetBackgroundInfo(extension).has_persistent_background_page();
}

// static
bool BackgroundInfo::HasLazyBackgroundPage(const Extension* extension) {
  return GetBackgroundInfo(extension).has_lazy_background_page();
}

// static
bool BackgroundInfo::HasGeneratedBackgroundPage(const Extension* extension) {
  const BackgroundInfo& info = GetBackgroundInfo(extension);
  return !info.background_scripts_.empty();
}

// static
bool BackgroundInfo::AllowJSAccess(const Extension* extension) {
  return GetBackgroundInfo(extension).allow_js_access_;
}

// static
bool BackgroundInfo::IsServiceWorkerBased(const Extension* extension) {
  return GetBackgroundInfo(extension)
      .background_service_worker_script_url_.has_value();
}

bool BackgroundInfo::Parse(Extension* extension, std::u16string* error) {
  const std::string& bg_scripts_key = extension->is_platform_app() ?
      keys::kPlatformAppBackgroundScripts : keys::kBackgroundScripts;
  if (!LoadBackgroundScripts(extension, bg_scripts_key, error) ||
      !LoadBackgroundPage(extension, error) ||
      !LoadBackgroundServiceWorkerScript(extension, error) ||
      !LoadBackgroundPersistent(extension, error) ||
      !LoadAllowJSAccess(extension, error)) {
    return false;
  }

  int background_solution_sum =
      (background_url_.is_valid() ? 1 : 0) +
      (!background_scripts_.empty() ? 1 : 0) +
      (background_service_worker_script_url_.has_value() ? 1 : 0);
  if (background_solution_sum > 1) {
    *error = errors::kInvalidBackgroundCombination;
    return false;
  }

  return true;
}

bool BackgroundInfo::LoadBackgroundScripts(Extension* extension,
                                           const std::string& key,
                                           std::u16string* error) {
  const base::Value* background_scripts_value =
      extension->manifest()->FindPath(key);
  if (background_scripts_value == nullptr) {
    return true;
  }

  CHECK(background_scripts_value);
  if (!background_scripts_value->is_list()) {
    *error = errors::kInvalidBackgroundScripts;
    return false;
  }

  const base::Value::List& background_scripts =
      background_scripts_value->GetList();
  for (size_t i = 0; i < background_scripts.size(); ++i) {
    if (!background_scripts[i].is_string()) {
      *error = ErrorUtils::FormatErrorMessageUTF16(
          errors::kInvalidBackgroundScript, base::NumberToString(i));
      return false;
    }

    const std::string& background_script = background_scripts[i].GetString();

    std::string mime_type;
    // TODO(https://crbug.com/40059598): Remove this if-check and always
    // validate the mime type in M139.
    if (base::FeatureList::IsEnabled(kValidateBackgroundScriptMimeType) &&
        (!net::GetWellKnownMimeTypeFromFile(
             base::FilePath::FromUTF8Unsafe(background_script), &mime_type) ||
         !blink::IsSupportedJavascriptMimeType(mime_type))) {
      // Issue a warning and ignore this file. This is a warning and not a
      // hard-error to preserve both backwards compatibility and potential
      // future-compatibility if mime types change.
      extension->AddInstallWarning(
          InstallWarning(ErrorUtils::FormatErrorMessage(
                             errors::kInvalidBackgroundScriptMimeType,
                             base::NumberToString(i)),
                         key));
      continue;
    }

    background_scripts_.push_back(background_script);
  }

  return true;
}

bool BackgroundInfo::LoadBackgroundPage(const Extension* extension,
                                        const std::string& key,
                                        std::u16string* error) {
  const base::Value* background_page_value =
      extension->manifest()->FindPath(key);
  if (background_page_value == nullptr) {
    return true;
  }

  if (!background_page_value->is_string()) {
    *error = errors::kInvalidBackground;
    return false;
  }
  const std::string& background_str = background_page_value->GetString();

  if (extension->is_hosted_app()) {
    background_url_ = GURL(background_str);

    if (!PermissionsParser::HasAPIPermission(extension,
                                             APIPermissionID::kBackground)) {
      *error = errors::kBackgroundPermissionNeeded;
      return false;
    }
    // Hosted apps require an absolute URL.
    if (!background_url_.is_valid()) {
      *error = errors::kInvalidBackgroundInHostedApp;
      return false;
    }

    if (!(background_url_.SchemeIs("https") ||
          (base::CommandLine::ForCurrentProcess()->HasSwitch(
               switches::kAllowHTTPBackgroundPage) &&
           background_url_.SchemeIs("http")))) {
      *error = errors::kInvalidBackgroundInHostedApp;
      return false;
    }
  } else {
    background_url_ = extension->ResolveExtensionURL(background_str);
    if (!background_url_.is_valid()) {
      *error = errors::kInvalidBackground;
      return false;
    }
  }

  return true;
}

bool BackgroundInfo::LoadBackgroundServiceWorkerScript(
    const Extension* extension,
    std::u16string* error) {
  const base::Value* scripts_value =
      extension->manifest()->FindPath(keys::kBackgroundServiceWorkerScript);
  if (scripts_value == nullptr) {
    return true;
  }

  DCHECK(scripts_value);
  if (!scripts_value->is_string()) {
    *error = errors::kInvalidBackgroundServiceWorkerScript;
    return false;
  }

  background_service_worker_script_url_ =
      extension->ResolveExtensionURL(scripts_value->GetString());
  if (!background_service_worker_script_url_->is_valid()) {
    *error = errors::kInvalidBackgroundServiceWorkerScript;
    return false;
  }

  const base::Value* scripts_type =
      extension->manifest()->FindPath(keys::kBackgroundServiceWorkerType);
  if (scripts_type == nullptr) {
    background_service_worker_type_ = BackgroundServiceWorkerType::kClassic;
    return true;
  }

  DCHECK(scripts_type);
  if (!scripts_type->is_string()) {
    *error = errors::kInvalidBackgroundServiceWorkerType;
    return false;
  }

  const std::string& type = scripts_type->GetString();
  if (type == "classic") {
    background_service_worker_type_ = BackgroundServiceWorkerType::kClassic;
    return true;
  }

  if (type == "module") {
    background_service_worker_type_ = BackgroundServiceWorkerType::kModule;
    return true;
  }

  *error = errors::kInvalidBackgroundServiceWorkerType;
  return false;
}

bool BackgroundInfo::LoadBackgroundPage(const Extension* extension,
                                        std::u16string* error) {
  const char* key = extension->is_platform_app()
                        ? keys::kPlatformAppBackgroundPage
                        : keys::kBackgroundPage;
  return LoadBackgroundPage(extension, key, error);
}

bool BackgroundInfo::LoadBackgroundPersistent(const Extension* extension,
                                              std::u16string* error) {
  if (extension->is_platform_app()) {
    is_persistent_ = false;
    return true;
  }

  const base::Value* background_persistent =
      extension->manifest()->FindPath(keys::kBackgroundPersistent);
  if (background_persistent == nullptr) {
    return true;
  }

  if (!background_persistent->is_bool()) {
    *error = errors::kInvalidBackgroundPersistent;
    return false;
  }
  is_persistent_ = background_persistent->GetBool();

  if (!has_background_page()) {
    *error = errors::kInvalidBackgroundPersistentNoPage;
    return false;
  }

  return true;
}

bool BackgroundInfo::LoadAllowJSAccess(const Extension* extension,
                                       std::u16string* error) {
  const base::Value* allow_js_access =
      extension->manifest()->FindPath(keys::kBackgroundAllowJsAccess);
  if (allow_js_access == nullptr) {
    return true;
  }

  if (!allow_js_access->is_bool()) {
    *error = errors::kInvalidBackgroundAllowJsAccess;
    return false;
  }
  allow_js_access_ = allow_js_access->GetBool();
  return true;
}

BackgroundManifestHandler::BackgroundManifestHandler() = default;
BackgroundManifestHandler::~BackgroundManifestHandler() = default;

bool BackgroundManifestHandler::Parse(Extension* extension,
                                      std::u16string* error) {
  std::unique_ptr<BackgroundInfo> info(new BackgroundInfo);
  if (!info->Parse(extension, error)) {
    return false;
  }

  // Platform apps must have background pages.
  if (extension->is_platform_app() && !info->has_background_page()) {
    *error = errors::kBackgroundRequiredForPlatformApps;
    return false;
  }
  // Lazy background pages are incompatible with the webRequest API.
  if (info->has_lazy_background_page() &&
      PermissionsParser::HasAPIPermission(extension,
                                          APIPermissionID::kWebRequest)) {
    *error = errors::kWebRequestConflictsWithLazyBackground;
    return false;
  }

  if (!info->has_lazy_background_page() &&
      PermissionsParser::HasAPIPermission(
          extension, APIPermissionID::kTransientBackground)) {
    *error = errors::kTransientBackgroundConflictsWithPersistentBackground;
    return false;
  }

  extension->SetManifestData(kBackground, std::move(info));
  return true;
}

bool BackgroundManifestHandler::Validate(
    const Extension& extension,
    std::string* error,
    std::vector<InstallWarning>* warnings) const {
  // Validate that background scripts exist.
  const std::vector<std::string>& background_scripts =
      BackgroundInfo::GetBackgroundScripts(&extension);
  for (const auto& background_script : background_scripts) {
    if (!base::PathExists(
            extension.GetResource(background_script).GetFilePath())) {
      *error =
          l10n_util::GetStringFUTF8(IDS_EXTENSION_LOAD_BACKGROUND_SCRIPT_FAILED,
                                    base::UTF8ToUTF16(background_script));
      return false;
    }
  }

  if (BackgroundInfo::IsServiceWorkerBased(&extension)) {
    DCHECK(extension.is_extension() ||
           extension.is_chromeos_system_extension() ||
           extension.is_login_screen_extension());
    base::FilePath path = file_util::ExtensionURLToAbsoluteFilePath(
        extension,
        BackgroundInfo::GetBackgroundServiceWorkerScriptURL(&extension));
    if (path.empty() || !base::PathExists(path)) {
      *error = l10n_util::GetStringFUTF8(
          IDS_EXTENSION_LOAD_BACKGROUND_SCRIPT_FAILED, path.LossyDisplayName());
      return false;
    }
  }

  // Validate background page location, except for hosted apps, which should use
  // an external URL. Background page for hosted apps are verified when the
  // extension is created (in Extension::InitFromValue)
  if (BackgroundInfo::HasBackgroundPage(&extension) &&
      !extension.is_hosted_app() && background_scripts.empty()) {
    base::FilePath path = file_util::ExtensionURLToAbsoluteFilePath(
        extension, BackgroundInfo::GetBackgroundURL(&extension));
    if (path.empty() || !base::PathExists(path)) {
      *error = l10n_util::GetStringFUTF8(
          IDS_EXTENSION_LOAD_BACKGROUND_PAGE_FAILED, path.LossyDisplayName());
      return false;
    }
  }

  if (extension.is_platform_app()) {
    const std::string manifest_key =
        std::string(keys::kPlatformAppBackground) + ".persistent";
    // Validate that packaged apps do not use a persistent background page.
    if (extension.manifest()->FindBoolPath(manifest_key).value_or(false)) {
      warnings->emplace_back(errors::kInvalidBackgroundPersistentInPlatformApp);
    }
  }

  return true;
}

bool BackgroundManifestHandler::AlwaysParseForType(Manifest::Type type) const {
  return type == Manifest::TYPE_PLATFORM_APP;
}

base::span<const char* const> BackgroundManifestHandler::Keys() const {
  static constexpr const char* kKeys[] = {keys::kBackgroundAllowJsAccess,
                                          keys::kBackgroundPage,
                                          keys::kBackgroundPersistent,
                                          keys::kBackgroundScripts,
                                          keys::kBackgroundServiceWorkerScript,
                                          keys::kBackgroundServiceWorkerType,
                                          keys::kPlatformAppBackgroundPage,
                                          keys::kPlatformAppBackgroundScripts};
  return kKeys;
}

}  // namespace extensions
