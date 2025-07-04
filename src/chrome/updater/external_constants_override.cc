// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/external_constants_override.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/updater/constants.h"
#include "chrome/updater/external_constants.h"
#include "chrome/updater/external_constants_default.h"
#include "chrome/updater/updater_branding.h"
#include "chrome/updater/updater_scope.h"
#include "chrome/updater/updater_version.h"
#include "chrome/updater/util/util.h"
#include "components/crx_file/crx_verifier.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#elif BUILDFLAG(IS_WIN)
#include "base/path_service.h"
#endif

namespace {

// Developer override file name, relative to app data directory.
const char kDevOverrideFileName[] = "overrides.json";

std::vector<GURL> GURLVectorFromStringList(
    const base::Value::List& update_url_list) {
  std::vector<GURL> ret;
  ret.reserve(update_url_list.size());
  for (const base::Value& url : update_url_list) {
    CHECK(url.is_string()) << "Non-string Value in update URL list";
    ret.push_back(GURL(url.GetString()));
  }
  return ret;
}

// The test binary only ever needs to contact localhost during integration
// tests. To reduce the program's utility as a mule, crash if there is a
// non-localhost override.
GURL CheckURL(const GURL& url) {
  CHECK(url.is_empty() || url.host() == "localhost" ||
        url.host() == "127.0.0.1" || url.host() == "not_exist")
      << "Illegal URL override: " << url;
  return url;
}

std::vector<GURL> CheckURLs(const std::vector<GURL>& urls) {
  for (const auto& url : urls) {
    CheckURL(url);
  }
  return urls;
}

}  // anonymous namespace

namespace updater {

std::optional<base::FilePath> GetOverrideFilePath(UpdaterScope scope) {
  std::optional<base::FilePath> base = GetInstallDirectory(scope);
  if (!base) {
    return std::nullopt;
  }
  return base->DirName().AppendUTF8(kDevOverrideFileName);
}

ExternalConstantsOverrider::ExternalConstantsOverrider(
    base::Value::Dict override_values,
    scoped_refptr<ExternalConstants> next_provider)
    : ExternalConstants(std::move(next_provider)),
      override_values_(std::move(override_values)) {}

ExternalConstantsOverrider::~ExternalConstantsOverrider() = default;

std::vector<GURL> ExternalConstantsOverrider::UpdateURL() const {
  if (!override_values_.contains(kDevOverrideKeyUrl)) {
    return next_provider_->UpdateURL();
  }
  const base::Value* update_url_value =
      override_values_.Find(kDevOverrideKeyUrl);
  switch (update_url_value->type()) {
    case base::Value::Type::STRING:
      return CheckURLs({GURL(update_url_value->GetString())});
    case base::Value::Type::LIST:
      return CheckURLs(GURLVectorFromStringList(update_url_value->GetList()));
    default:
      LOG(FATAL) << "Unexpected type of override[" << kDevOverrideKeyUrl
                 << "]: " << base::Value::GetTypeName(update_url_value->type());
  }
}

GURL ExternalConstantsOverrider::CrashUploadURL() const {
  if (!override_values_.contains(kDevOverrideKeyCrashUploadUrl)) {
    return next_provider_->CrashUploadURL();
  }
  const base::Value* crash_upload_url_value =
      override_values_.Find(kDevOverrideKeyCrashUploadUrl);
  CHECK(crash_upload_url_value->is_string())
      << "Unexpected type of override[" << kDevOverrideKeyCrashUploadUrl
      << "]: " << base::Value::GetTypeName(crash_upload_url_value->type());
  return CheckURL({GURL(crash_upload_url_value->GetString())});
}

GURL ExternalConstantsOverrider::AppLogoURL() const {
  if (!override_values_.contains(kDevOverrideKeyAppLogoUrl)) {
    return next_provider_->AppLogoURL();
  }
  const base::Value* app_logo_url_value =
      override_values_.Find(kDevOverrideKeyAppLogoUrl);
  CHECK(app_logo_url_value->is_string())
      << "Unexpected type of override[" << kDevOverrideKeyAppLogoUrl
      << "]: " << base::Value::GetTypeName(app_logo_url_value->type());
  return CheckURL({GURL(app_logo_url_value->GetString())});
}

GURL ExternalConstantsOverrider::EventLoggingURL() const {
  if (!override_values_.contains(kDevOverrideKeyEventLoggingUrl)) {
    return next_provider_->EventLoggingURL();
  }
  const base::Value* event_logging_url_value =
      override_values_.Find(kDevOverrideKeyEventLoggingUrl);
  CHECK(event_logging_url_value->is_string())
      << "Unexpected type of override[" << kDevOverrideKeyEventLoggingUrl
      << "]: " << base::Value::GetTypeName(event_logging_url_value->type());
  return CheckURL({GURL(event_logging_url_value->GetString())});
}

bool ExternalConstantsOverrider::UseCUP() const {
  if (!override_values_.contains(kDevOverrideKeyUseCUP)) {
    return next_provider_->UseCUP();
  }
  const base::Value* use_cup_value =
      override_values_.Find(kDevOverrideKeyUseCUP);
  CHECK(use_cup_value->is_bool())
      << "Unexpected type of override[" << kDevOverrideKeyUseCUP
      << "]: " << base::Value::GetTypeName(use_cup_value->type());

  return use_cup_value->GetBool();
}

base::TimeDelta ExternalConstantsOverrider::InitialDelay() const {
  if (!override_values_.contains(kDevOverrideKeyInitialDelay)) {
    return next_provider_->InitialDelay();
  }

  const base::Value* initial_delay_value =
      override_values_.Find(kDevOverrideKeyInitialDelay);
  CHECK(initial_delay_value->is_double())
      << "Unexpected type of override[" << kDevOverrideKeyInitialDelay
      << "]: " << base::Value::GetTypeName(initial_delay_value->type());
  return base::Seconds(initial_delay_value->GetDouble());
}

base::TimeDelta ExternalConstantsOverrider::ServerKeepAliveTime() const {
  if (!override_values_.contains(kDevOverrideKeyServerKeepAliveSeconds)) {
    return next_provider_->ServerKeepAliveTime();
  }

  const base::Value* server_keep_alive_seconds_value =
      override_values_.Find(kDevOverrideKeyServerKeepAliveSeconds);
  CHECK(server_keep_alive_seconds_value->is_int())
      << "Unexpected type of override[" << kDevOverrideKeyServerKeepAliveSeconds
      << "]: "
      << base::Value::GetTypeName(server_keep_alive_seconds_value->type());
  return base::Seconds(server_keep_alive_seconds_value->GetInt());
}

crx_file::VerifierFormat ExternalConstantsOverrider::CrxVerifierFormat() const {
  if (!override_values_.contains(kDevOverrideKeyCrxVerifierFormat)) {
    return next_provider_->CrxVerifierFormat();
  }

  const base::Value* crx_format_verifier_value =
      override_values_.Find(kDevOverrideKeyCrxVerifierFormat);
  CHECK(crx_format_verifier_value->is_int())
      << "Unexpected type of override[" << kDevOverrideKeyCrxVerifierFormat
      << "]: " << base::Value::GetTypeName(crx_format_verifier_value->type());
  return static_cast<crx_file::VerifierFormat>(
      crx_format_verifier_value->GetInt());
}

base::TimeDelta ExternalConstantsOverrider::MinimumEventLoggingCooldown()
    const {
  if (!override_values_.contains(
          kDevOverrideKeyMinumumEventLoggingCooldownSeconds)) {
    return next_provider_->MinimumEventLoggingCooldown();
  }

  const base::Value* minimum_event_logging_cooldown_seconds =
      override_values_.Find(kDevOverrideKeyMinumumEventLoggingCooldownSeconds);
  CHECK(minimum_event_logging_cooldown_seconds->is_int())
      << "Unexpected type of override["
      << kDevOverrideKeyMinumumEventLoggingCooldownSeconds << "]: "
      << base::Value::GetTypeName(
             minimum_event_logging_cooldown_seconds->type());
  return base::Seconds(minimum_event_logging_cooldown_seconds->GetInt());
}

std::optional<EventLoggingPermissionProvider>
ExternalConstantsOverrider::GetEventLoggingPermissionProvider() const {
  if (!override_values_.contains(
          kDevOverrideKeyEventLoggingPermissionProviderAppId)) {
    return next_provider_->GetEventLoggingPermissionProvider();
  }

  EventLoggingPermissionProvider provider;

  const base::Value* app_id =
      override_values_.Find(kDevOverrideKeyEventLoggingPermissionProviderAppId);
  CHECK(app_id->is_string())
      << "Unexpected type of override["
      << kDevOverrideKeyEventLoggingPermissionProviderAppId
      << "]: " << base::Value::GetTypeName(app_id->type());
  provider.app_id = app_id->GetString();

#if BUILDFLAG(IS_MAC)
  const base::Value* directory_name = override_values_.Find(
      kDevOverrideKeyEventLoggingPermissionProviderDirectoryName);
  CHECK(directory_name->is_string())
      << "Unexpected type of override["
      << kDevOverrideKeyEventLoggingPermissionProviderDirectoryName
      << "]: " << base::Value::GetTypeName(directory_name->type());
  provider.directory_name = directory_name->GetString();
#endif

  return provider;
}

base::Value::Dict ExternalConstantsOverrider::DictPolicies() const {
  if (!override_values_.contains(kDevOverrideKeyDictPolicies)) {
    return next_provider_->DictPolicies();
  }

  const base::Value* dict_policies_value =
      override_values_.Find(kDevOverrideKeyDictPolicies);
  CHECK(dict_policies_value->is_dict())
      << "Unexpected type of override[" << kDevOverrideKeyDictPolicies
      << "]: " << base::Value::GetTypeName(dict_policies_value->type());
  return dict_policies_value->GetDict().Clone();
}

base::TimeDelta ExternalConstantsOverrider::OverinstallTimeout() const {
  if (!override_values_.contains(kDevOverrideKeyOverinstallTimeout)) {
    return next_provider_->OverinstallTimeout();
  }

  const base::Value* value =
      override_values_.Find(kDevOverrideKeyOverinstallTimeout);
  CHECK(value->is_int()) << "Unexpected type of override["
                         << kDevOverrideKeyOverinstallTimeout
                         << "]: " << base::Value::GetTypeName(value->type());
  return base::Seconds(value->GetInt());
}

base::TimeDelta ExternalConstantsOverrider::IdleCheckPeriod() const {
  if (!override_values_.contains(kDevOverrideKeyIdleCheckPeriodSeconds)) {
    return next_provider_->IdleCheckPeriod();
  }

  const base::Value* value =
      override_values_.Find(kDevOverrideKeyIdleCheckPeriodSeconds);
  CHECK(value->is_int()) << "Unexpected type of override["
                         << kDevOverrideKeyIdleCheckPeriodSeconds
                         << "]: " << base::Value::GetTypeName(value->type());
  return base::Seconds(value->GetInt());
}

std::optional<bool> ExternalConstantsOverrider::IsMachineManaged() const {
  if (!override_values_.contains(kDevOverrideKeyManagedDevice)) {
    return next_provider_->IsMachineManaged();
  }
  const base::Value* is_managed =
      override_values_.Find(kDevOverrideKeyManagedDevice);
  CHECK(is_managed->is_bool())
      << "Unexpected type of override[" << kDevOverrideKeyManagedDevice
      << "]: " << base::Value::GetTypeName(is_managed->type());

  return std::make_optional(is_managed->GetBool());
}

base::TimeDelta ExternalConstantsOverrider::CecaConnectionTimeout() const {
  if (!override_values_.contains(kDevOverrideKeyCecaConnectionTimeout)) {
    return next_provider_->CecaConnectionTimeout();
  }

  const base::Value* value =
      override_values_.Find(kDevOverrideKeyCecaConnectionTimeout);
  CHECK(value->is_int()) << "Unexpected type of override["
                         << kDevOverrideKeyCecaConnectionTimeout
                         << "]: " << base::Value::GetTypeName(value->type());
  return base::Seconds(value->GetInt());
}

// static
scoped_refptr<ExternalConstantsOverrider>
ExternalConstantsOverrider::FromDefaultJSONFile(
    scoped_refptr<ExternalConstants> next_provider) {
  const std::optional<base::FilePath> override_file_path =
      GetOverrideFilePath(GetUpdaterScope());
  if (!override_file_path) {
    LOG(ERROR) << "Cannot find override file path.";
    return nullptr;
  }

  JSONFileValueDeserializer parser(*override_file_path,
                                   base::JSON_ALLOW_TRAILING_COMMAS);
  int error_code = 0;
  std::string error_message;
  std::unique_ptr<base::Value> parsed_value(
      parser.Deserialize(&error_code, &error_message));
  if (error_code || !parsed_value) {
    VLOG(2) << "Could not parse " << override_file_path << ": error "
            << error_code << ": " << error_message;
    return nullptr;
  }

  if (!parsed_value->is_dict()) {
    LOG(ERROR) << "Invalid data in " << override_file_path << ": not a dict";
    return nullptr;
  }

  return base::MakeRefCounted<ExternalConstantsOverrider>(
      std::move(*parsed_value).TakeDict(), next_provider);
}

// Declared in external_constants.h. This implementation of the function is
// used only if external_constants_override is linked into the binary.
scoped_refptr<ExternalConstants> CreateExternalConstants() {
  scoped_refptr<ExternalConstants> overrider =
      ExternalConstantsOverrider::FromDefaultJSONFile(
          CreateDefaultExternalConstants());
  return overrider ? overrider : CreateDefaultExternalConstants();
}

}  // namespace updater
