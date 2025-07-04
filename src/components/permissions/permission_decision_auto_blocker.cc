// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/permissions/permission_decision_auto_blocker.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/permissions/features.h"
#include "components/permissions/permission_util.h"
#include "url/gurl.h"

namespace permissions {
namespace {

using PermissionStatus = blink::mojom::PermissionStatus;

// The number of times that users may explicitly dismiss a permission prompt
// from an origin before it is automatically blocked.
constexpr int kDefaultDismissalsBeforeBlock = 3;

// The number of times that users may ignore a permission prompt from an origin
// before it is automatically blocked.
constexpr int kDefaultIgnoresBeforeBlock = 4;

// The number of times that users may dismiss a permission prompt that uses the
// quiet UI from an origin before it is automatically blocked.
constexpr int kDefaultDismissalsBeforeBlockWithQuietUi = 1;

// The number of times that users may ignore a permission prompt that uses the
// quiet UI from an origin before it is automatically blocked.
constexpr int kDefaultIgnoresBeforeBlockWithQuietUi = 2;

// The number of days that an origin will stay under embargo for a requested
// permission due to repeated ignores or dismissals.
constexpr int kDefaultEmbargoDays = 7;

// The number of times that users may explicitly dismiss a
// FEDERATED_IDENTITY_API permission prompt from an origin before it is
// automatically blocked.
constexpr int kFederatedIdentityApiDismissalsBeforeBlock = 1;

// The number of times that users may ignore a FEDERATED_IDENTITY_API permission
// prompt from an origin before it is automatically blocked.
constexpr int kFederatedIdentityApiIgnoresBeforeBlock = 1;

// The number of hours that an origin will stay under embargo for a requested
// permission due to users ignoring the prompt.
constexpr base::TimeDelta kFederatedIdentityApiIgnoresEmbargo = base::Hours(4);

// The duration that an origin will stay under embargo for the
// FEDERATED_IDENTITY_AUTO_REAUTHN_PERMISSION permission due to an auto re-authn
// prompt being displayed recently.
constexpr base::TimeDelta kFederatedIdentityAutoReauthnEmbargoDuration =
    base::Minutes(10);

// The duration that an origin will stay under embargo for the
// SUB_APP_INSTALLATION_PROMPTS permission when the embargo is applied
// for the first time. After another dismissal, the default kDefaultEmbargoDays
// is applied.
constexpr base::TimeDelta kSubAppInstallationPromptsFirstTimeEmbargoDuration =
    base::Minutes(10);

std::string GetStringForContentType(ContentSettingsType content_type) {
  switch (content_type) {
    case ContentSettingsType::AUTO_PICTURE_IN_PICTURE:
      return "AutoPictureInPicture";
    case ContentSettingsType::FEDERATED_IDENTITY_API:
      return "FederatedIdentityApi";
    case ContentSettingsType::FEDERATED_IDENTITY_AUTO_REAUTHN_PERMISSION:
      return "FederatedIdentityAutoReauthn";
    case ContentSettingsType::FILE_SYSTEM_ACCESS_RESTORE_PERMISSION:
      return "FileSystemAccessRestorePermission";
    case ContentSettingsType::FILE_SYSTEM_WRITE_GUARD:
      return "FileSystemWriteGuard";
    case ContentSettingsType::SUB_APP_INSTALLATION_PROMPTS:
      return "SubAppInstallationPrompts";
#if BUILDFLAG(IS_CHROMEOS)
    case ContentSettingsType::SMART_CARD_GUARD:
      return "SmartCard";
#endif  // BUILDFLAG(IS_CHROMEOS)
    // If you add a new Content Setting here, also add it to
    // IsEnabledForContentSetting.
    default:
      return PermissionUtil::GetPermissionString(content_type);
  }
}

base::Value::Dict GetOriginAutoBlockerData(HostContentSettingsMap* settings,
                                           const GURL& origin_url) {
  base::Value website_setting = settings->GetWebsiteSetting(
      origin_url, GURL(), ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA);
  if (!website_setting.is_dict()) {
    return base::Value::Dict();
  }

  return std::move(website_setting.GetDict());
}

base::Value::Dict* GetOrCreatePermissionDict(base::Value::Dict& origin_dict,
                                             const std::string& permission) {
  return origin_dict.EnsureDict(permission);
}

int RecordActionInWebsiteSettings(const GURL& url,
                                  ContentSettingsType permission,
                                  const char* key,
                                  HostContentSettingsMap* settings_map) {
  base::Value::Dict dict = GetOriginAutoBlockerData(settings_map, url);

  base::Value::Dict* permission_dict =
      GetOrCreatePermissionDict(dict, GetStringForContentType(permission));

  std::optional<int> value = permission_dict->FindInt(key);
  int current_count = value.value_or(0);
  permission_dict->Set(key, base::Value(++current_count));

  settings_map->SetWebsiteSettingDefaultScope(
      url, GURL(), ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA,
      base::Value(std::move(dict)));

  return current_count;
}

int GetActionCount(const GURL& url,
                   ContentSettingsType permission,
                   const char* key,
                   HostContentSettingsMap* settings_map) {
  base::Value::Dict dict = GetOriginAutoBlockerData(settings_map, url);
  base::Value::Dict* permission_dict =
      GetOrCreatePermissionDict(dict, GetStringForContentType(permission));

  std::optional<int> value = permission_dict->FindInt(key);
  return value.value_or(0);
}

// Returns the number of times that users may explicitly dismiss a permission
// prompt for an origin for the passed-in |permission| before it is
// automatically blocked.
int GetDismissalsBeforeBlockForContentSettingsType(
    ContentSettingsType permission) {
  return (permission == ContentSettingsType::FEDERATED_IDENTITY_API)
             ? kFederatedIdentityApiDismissalsBeforeBlock
             : kDefaultDismissalsBeforeBlock;
}

// Returns the number of times that users may ignore a permission prompt for an
// origin for the passed-in |permission| before it is automatically blocked.
int GetIgnoresBeforeBlockForContentSettingsType(
    ContentSettingsType permission) {
  return (permission == ContentSettingsType::FEDERATED_IDENTITY_API)
             ? kFederatedIdentityApiIgnoresBeforeBlock
             : kDefaultIgnoresBeforeBlock;
}

// The duration that an origin will stay under embargo for the passed-in
// |permission| due to the user explicitly dismissing the permission prompt.
base::TimeDelta GetEmbargoDurationForContentSettingsType(
    ContentSettingsType permission,
    int dismiss_count) {
  // The durations that an origin will stay under embargo for the
  // FEDERATED_IDENTITY_API permission due to the user explicitly dismissing the
  // permission prompt.
  auto FederatedIdentityApiEmbargoDurationDismiss =
      std::to_array<base::TimeDelta>(
          {base::Hours(base::GetFieldTrialParamByFeatureAsInt(
               features::kFedCmUpdatedCooldownPeriod, "FirstDismissal", 2)),
           base::Days(base::GetFieldTrialParamByFeatureAsInt(
               features::kFedCmUpdatedCooldownPeriod, "SecondDismissal", 1)),
           base::Days(base::GetFieldTrialParamByFeatureAsInt(
               features::kFedCmUpdatedCooldownPeriod, "ThirdDismissal", 7)),
           base::Days(base::GetFieldTrialParamByFeatureAsInt(
               features::kFedCmUpdatedCooldownPeriod, "FourthDismissal", 28))});

  if (permission == ContentSettingsType::FEDERATED_IDENTITY_API) {
    int duration_index =
        std::clamp(dismiss_count - 1, 0,
                   static_cast<int>(
                       FederatedIdentityApiEmbargoDurationDismiss.size() - 1));
    return FederatedIdentityApiEmbargoDurationDismiss[duration_index];
  }

  if (permission ==
      ContentSettingsType::FEDERATED_IDENTITY_AUTO_REAUTHN_PERMISSION) {
    return kFederatedIdentityAutoReauthnEmbargoDuration;
  }

  if (permission == ContentSettingsType::SUB_APP_INSTALLATION_PROMPTS) {
    // If this is the first time this embargo is applied, be more forgiving.
    if (dismiss_count == kDefaultDismissalsBeforeBlock) {
      return kSubAppInstallationPromptsFirstTimeEmbargoDuration;
    }
  }

  return base::Days(kDefaultEmbargoDays);
}

// The duration that an origin will stay under embargo for the passed-in
// |permission| due to the user ignoring the permission prompt.
base::TimeDelta GetIgnoreEmbargoDurationForContentSettingsType(
    ContentSettingsType permission) {
  return (permission == ContentSettingsType::FEDERATED_IDENTITY_API)
             ? kFederatedIdentityApiIgnoresEmbargo
             : base::Days(kDefaultEmbargoDays);
}

base::Time GetEmbargoStartTime(base::Value::Dict* permission_dict,
                               const char* key) {
  std::optional<double> found = permission_dict->FindDouble(key);
  if (found) {
    return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(*found));
  }
  return base::Time();
}

bool IsUnderEmbargo(base::Value::Dict* permission_dict,
                    const char* key,
                    base::Time current_time,
                    base::TimeDelta offset) {
  std::optional<double> found = permission_dict->FindDouble(key);
  if (found && current_time < base::Time::FromInternalValue(*found) + offset) {
    return true;
  }

  return false;
}
}  // namespace

// static
const char PermissionDecisionAutoBlocker::kPromptDismissCountKey[] =
    "dismiss_count";

// static
const char PermissionDecisionAutoBlocker::kPromptIgnoreCountKey[] =
    "ignore_count";

// static
const char PermissionDecisionAutoBlocker::kPromptDismissCountWithQuietUiKey[] =
    "dismiss_count_quiet_ui";

// static
const char PermissionDecisionAutoBlocker::kPromptIgnoreCountWithQuietUiKey[] =
    "ignore_count_quiet_ui";

// static
const char PermissionDecisionAutoBlocker::kPermissionDismissalEmbargoKey[] =
    "dismissal_embargo_days";

// static
const char PermissionDecisionAutoBlocker::kPermissionIgnoreEmbargoKey[] =
    "ignore_embargo_days";

// static
const char PermissionDecisionAutoBlocker::kPermissionDisplayEmbargoKey[] =
    "display_embargo_minutes";

// static
bool PermissionDecisionAutoBlocker::IsEnabledForContentSetting(
    ContentSettingsType content_setting) {
  return PermissionUtil::IsPermission(content_setting) ||
         content_setting == ContentSettingsType::AUTO_PICTURE_IN_PICTURE ||
         content_setting == ContentSettingsType::FEDERATED_IDENTITY_API ||
         content_setting ==
             ContentSettingsType::FEDERATED_IDENTITY_AUTO_REAUTHN_PERMISSION ||
         content_setting ==
             ContentSettingsType::FILE_SYSTEM_ACCESS_RESTORE_PERMISSION ||
         content_setting == ContentSettingsType::FILE_SYSTEM_WRITE_GUARD ||
         content_setting == ContentSettingsType::SUB_APP_INSTALLATION_PROMPTS
#if BUILDFLAG(IS_CHROMEOS)
         || content_setting == ContentSettingsType::SMART_CARD_GUARD
#endif  // BUILDFLAG(IS_CHROMEOS)
      ;
  // If you add a new content setting here, also add it to
  // GetStringForContentType.
}

// static
std::optional<content::PermissionResult>
PermissionDecisionAutoBlocker::GetEmbargoResult(
    HostContentSettingsMap* settings_map,
    const GURL& request_origin,
    ContentSettingsType permission,
    base::Time current_time) {
  DCHECK(settings_map);
  DCHECK(IsEnabledForContentSetting(permission));

  base::Value::Dict dict =
      GetOriginAutoBlockerData(settings_map, request_origin);
  base::Value::Dict* permission_dict =
      GetOrCreatePermissionDict(dict, GetStringForContentType(permission));

  int dismiss_count = GetActionCount(request_origin, permission,
                                     kPromptDismissCountKey, settings_map);
  if (IsUnderEmbargo(permission_dict, kPermissionDismissalEmbargoKey,
                     current_time,
                     GetEmbargoDurationForContentSettingsType(permission,
                                                              dismiss_count))) {
    return content::PermissionResult(
        PermissionStatus::DENIED,
        content::PermissionStatusSource::MULTIPLE_DISMISSALS);
  }

  if (IsUnderEmbargo(
          permission_dict, kPermissionIgnoreEmbargoKey, current_time,
          GetIgnoreEmbargoDurationForContentSettingsType(permission))) {
    return content::PermissionResult(
        PermissionStatus::DENIED,
        content::PermissionStatusSource::MULTIPLE_IGNORES);
  }

  if (IsUnderEmbargo(permission_dict, kPermissionDisplayEmbargoKey,
                     current_time,
                     GetEmbargoDurationForContentSettingsType(
                         permission, /*dismiss_count=*/0))) {
    return content::PermissionResult(
        PermissionStatus::DENIED,
        content::PermissionStatusSource::RECENT_DISPLAY);
  }

  return std::nullopt;
}

bool PermissionDecisionAutoBlocker::IsEmbargoed(
    const GURL& request_origin,
    ContentSettingsType permission) {
  return GetEmbargoResult(request_origin, permission).has_value();
}

std::optional<content::PermissionResult>
PermissionDecisionAutoBlocker::GetEmbargoResult(
    const GURL& request_origin,
    ContentSettingsType permission) {
  return GetEmbargoResult(settings_map_, request_origin, permission,
                          clock_->Now());
}

base::Time PermissionDecisionAutoBlocker::GetEmbargoStartTime(
    const GURL& request_origin,
    ContentSettingsType permission) {
  DCHECK(settings_map_);
  base::Value::Dict dict =
      GetOriginAutoBlockerData(settings_map_, request_origin);
  base::Value::Dict* permission_dict =
      GetOrCreatePermissionDict(dict, GetStringForContentType(permission));

  // A permission may have a record for both dismisal and ignore, return the
  // most recent. A permission will only actually be under one embargo, but
  // the record of embargo start will persist until explicitly deleted
  base::Time dismissal_start_time = permissions::GetEmbargoStartTime(
      permission_dict, kPermissionDismissalEmbargoKey);
  base::Time ignore_start_time = permissions::GetEmbargoStartTime(
      permission_dict, kPermissionIgnoreEmbargoKey);

  return dismissal_start_time > ignore_start_time ? dismissal_start_time
                                                  : ignore_start_time;
}

std::set<GURL> PermissionDecisionAutoBlocker::GetEmbargoedOrigins(
    ContentSettingsType content_type) {
  return GetEmbargoedOrigins(std::vector<ContentSettingsType>{content_type});
}

std::set<GURL> PermissionDecisionAutoBlocker::GetEmbargoedOrigins(
    std::vector<ContentSettingsType> content_types) {
  DCHECK(settings_map_);

  std::vector<ContentSettingsType> filtered_content_types;
  for (ContentSettingsType content_type : content_types) {
    if (IsEnabledForContentSetting(content_type))
      filtered_content_types.emplace_back(content_type);
  }
  if (filtered_content_types.empty())
    return std::set<GURL>();

  std::set<GURL> origins;
  for (const auto& e : settings_map_->GetSettingsForOneType(
           ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA)) {
    for (auto content_type : filtered_content_types) {
      const GURL url(e.primary_pattern.ToString());
      if (IsEmbargoed(url, content_type)) {
        origins.insert(url);
        break;
      }
    }
  }
  return origins;
}

int PermissionDecisionAutoBlocker::GetDismissCount(
    const GURL& url,
    ContentSettingsType permission) {
  return GetActionCount(url, permission, kPromptDismissCountKey, settings_map_);
}

int PermissionDecisionAutoBlocker::GetIgnoreCount(
    const GURL& url,
    ContentSettingsType permission) {
  return GetActionCount(url, permission, kPromptIgnoreCountKey, settings_map_);
}

bool PermissionDecisionAutoBlocker::RecordDismissAndEmbargo(
    const GURL& url,
    ContentSettingsType permission,
    bool dismissed_prompt_was_quiet) {
  int current_dismissal_count = RecordActionInWebsiteSettings(
      url, permission, kPromptDismissCountKey, settings_map_);

  int current_dismissal_count_with_quiet_ui =
      dismissed_prompt_was_quiet
          ? RecordActionInWebsiteSettings(url, permission,
                                          kPromptDismissCountWithQuietUiKey,
                                          settings_map_)
          : -1;

  // TODO(dominickn): ideally we would have a method
  // ContentSettingPermissionContextBase::ShouldEmbargoAfterRepeatedDismissals()
  // to specify if a permission is opted in. This is difficult right now
  // because:
  // 1. PermissionQueueController needs to call this method at a point where it
  //    does not have a ContentSettingPermissionContextBase available
  // 2. Not calling RecordDismissAndEmbargo means no repeated dismissal metrics
  //    are recorded
  if (current_dismissal_count >=
      GetDismissalsBeforeBlockForContentSettingsType(permission)) {
    PlaceUnderEmbargo(url, permission, kPermissionDismissalEmbargoKey);
    return true;
  }

  if (current_dismissal_count_with_quiet_ui >=
      kDefaultDismissalsBeforeBlockWithQuietUi) {
    DCHECK(permission == ContentSettingsType::NOTIFICATIONS ||
           permission == ContentSettingsType::GEOLOCATION);
    PlaceUnderEmbargo(url, permission, kPermissionDismissalEmbargoKey);
    return true;
  }
  return false;
}

bool PermissionDecisionAutoBlocker::RecordIgnoreAndEmbargo(
    const GURL& url,
    ContentSettingsType permission,
    bool ignored_prompt_was_quiet) {
  int current_ignore_count = RecordActionInWebsiteSettings(
      url, permission, kPromptIgnoreCountKey, settings_map_);

  int current_ignore_count_with_quiet_ui =
      ignored_prompt_was_quiet
          ? RecordActionInWebsiteSettings(url, permission,
                                          kPromptIgnoreCountWithQuietUiKey,
                                          settings_map_)
          : -1;

  if (current_ignore_count >=
      GetIgnoresBeforeBlockForContentSettingsType(permission)) {
    PlaceUnderEmbargo(url, permission, kPermissionIgnoreEmbargoKey);
    return true;
  }

  if (current_ignore_count_with_quiet_ui >=
      kDefaultIgnoresBeforeBlockWithQuietUi) {
    DCHECK(permission == ContentSettingsType::NOTIFICATIONS ||
           permission == ContentSettingsType::GEOLOCATION);
    PlaceUnderEmbargo(url, permission, kPermissionIgnoreEmbargoKey);
    return true;
  }

  return false;
}

bool PermissionDecisionAutoBlocker::RecordDisplayAndEmbargo(
    const GURL& url,
    ContentSettingsType permission) {
  DCHECK_EQ(permission,
            ContentSettingsType::FEDERATED_IDENTITY_AUTO_REAUTHN_PERMISSION);
  PlaceUnderEmbargo(url, permission, kPermissionDisplayEmbargoKey);
  return true;
}

void PermissionDecisionAutoBlocker::RemoveEmbargoAndResetCounts(
    const GURL& url,
    ContentSettingsType permission) {
  if (!IsEnabledForContentSetting(permission))
    return;

  base::Value::Dict dict = GetOriginAutoBlockerData(settings_map_, url);

  dict.Remove(GetStringForContentType(permission));

  settings_map_->SetWebsiteSettingDefaultScope(
      url, GURL(), ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA,
      base::Value(std::move(dict)));
}

void PermissionDecisionAutoBlocker::RemoveEmbargoAndResetCounts(
    base::RepeatingCallback<bool(const GURL& url)> filter) {
  for (const auto& site : settings_map_->GetSettingsForOneType(
           ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA)) {
    GURL origin(site.primary_pattern.ToString());

    if (origin.is_valid() && filter.Run(origin)) {
      settings_map_->SetWebsiteSettingDefaultScope(
          origin, GURL(), ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA,
          base::Value());
    }
  }
}

void PermissionDecisionAutoBlocker::AddObserver(Observer* obs) {
  observers_.AddObserver(obs);
}

void PermissionDecisionAutoBlocker::RemoveObserver(Observer* obs) {
  observers_.RemoveObserver(obs);
}

// static
const char*
PermissionDecisionAutoBlocker::GetPromptDismissCountKeyForTesting() {
  return kPromptDismissCountKey;
}

PermissionDecisionAutoBlocker::PermissionDecisionAutoBlocker(
    HostContentSettingsMap* settings_map)
    : settings_map_(settings_map), clock_(base::DefaultClock::GetInstance()) {}

PermissionDecisionAutoBlocker::~PermissionDecisionAutoBlocker() = default;

void PermissionDecisionAutoBlocker::PlaceUnderEmbargo(
    const GURL& request_origin,
    ContentSettingsType permission,
    const char* key) {
  base::Value::Dict dict =
      GetOriginAutoBlockerData(settings_map_, request_origin);
  base::Value::Dict* permission_dict =
      GetOrCreatePermissionDict(dict, GetStringForContentType(permission));
  permission_dict->Set(
      key, base::Value(static_cast<double>(clock_->Now().ToInternalValue())));
  settings_map_->SetWebsiteSettingDefaultScope(
      request_origin, GURL(), ContentSettingsType::PERMISSION_AUTOBLOCKER_DATA,
      base::Value(std::move(dict)));
  NotifyEmbargoStarted(request_origin, permission);
}

void PermissionDecisionAutoBlocker::NotifyEmbargoStarted(
    const GURL& origin,
    ContentSettingsType content_setting) {
  for (Observer& obs : observers_)
    obs.OnEmbargoStarted(origin, content_setting);
}

void PermissionDecisionAutoBlocker::SetClockForTesting(base::Clock* clock) {
  clock_ = clock;
}

}  // namespace permissions
