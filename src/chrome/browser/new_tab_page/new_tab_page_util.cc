// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/new_tab_page/new_tab_page_util.h"

#include "base/strings/strcat.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/new_tab_page/modules/modules_switches.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/optimization_guide/core/optimization_guide_logger.h"
#include "components/page_content_annotations/core/page_content_annotations_features.h"
#include "components/prefs/pref_service.h"
#include "components/search/ntp_features.h"
#include "components/signin/public/identity_manager/accounts_in_cookie_jar_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/sync/service/sync_service.h"
#include "components/variations/service/variations_service.h"

namespace {

bool IsOsSupportedForCart() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
  return true;
#else
  return false;
#endif
}

bool IsOsSupportedForDrive() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  return true;
#else
  return false;
#endif
}

bool IsInUS() {
  return g_browser_process->GetApplicationLocale() == "en-US" &&
         GetVariationsServiceCountryCode(
             g_browser_process->variations_service()) == "us";
}

}  // namespace

// If feature is overridden manually or by finch, read the feature flag value.
// Otherwise filter by os, locale and country code.
bool IsCartModuleEnabled() {
  if (base::FeatureList::GetInstance()->IsFeatureOverridden(
          ntp_features::kNtpChromeCartModule.name)) {
    return base::FeatureList::IsEnabled(ntp_features::kNtpChromeCartModule);
  }
  return IsOsSupportedForCart() && IsInUS();
}

bool IsDriveModuleEnabled() {
  if (base::FeatureList::GetInstance()->IsFeatureOverridden(
          ntp_features::kNtpDriveModule.name)) {
    return IsFeatureForceEnabled(ntp_features::kNtpDriveModule);
  }
  const bool default_enabled = IsOsSupportedForDrive();
  LogModuleEnablement(ntp_features::kNtpDriveModule, default_enabled,
                      "default feature flag value");
  return default_enabled;
}

bool IsDriveModuleEnabledForProfile(bool is_managed_profile, Profile* profile) {
  if (!IsDriveModuleEnabled()) {
    return false;
  }

  if (!IsProfileSignedIn(profile)) {
    LogModuleEnablement(ntp_features::kNtpDriveModule, false, "not signed in");
    return false;
  }

  if (!base::FeatureList::IsEnabled(
          ntp_features::kNtpDriveModuleNoSyncRequirement)) {
    auto* sync_service = SyncServiceFactory::GetForProfile(profile);
    if (!sync_service || !sync_service->IsSyncFeatureEnabled()) {
      LogModuleEnablement(ntp_features::kNtpDriveModule, false, "no sync");
      return false;
    }
  }

  if (!is_managed_profile) {
    LogModuleEnablement(ntp_features::kNtpDriveModule, false,
                        "account not managed");
    return false;
  }
  return true;
}

bool IsEnUSLocaleOnlyFeatureEnabled(const base::Feature& ntp_feature) {
  if (base::FeatureList::GetInstance()->IsFeatureOverridden(ntp_feature.name)) {
    return base::FeatureList::IsEnabled(ntp_feature);
  }
  return IsInUS();
}

bool IsFeatureEnabled(const base::Feature& feature) {
  if (base::FeatureList::GetInstance()->IsFeatureOverridden(feature.name)) {
    return IsFeatureForceEnabled(feature);
  }

  bool is_default_enabled =
      feature.default_state == base::FeatureState::FEATURE_ENABLED_BY_DEFAULT;
  LogModuleEnablement(feature, is_default_enabled,
                      "default feature flag value");
  return is_default_enabled;
}

bool IsFeatureForceEnabled(const base::Feature& feature) {
  const bool force_enabled = base::FeatureList::IsEnabled(feature);
  LogModuleEnablement(
      feature, force_enabled,
      force_enabled ? "feature flag forced on" : "feature flag forced off");
  return force_enabled;
}

bool IsGoogleCalendarModuleEnabled(bool is_managed_profile, Profile* profile) {
  if (!IsProfileSignedIn(profile)) {
    LogModuleEnablement(ntp_features::kNtpCalendarModule, false,
                        "not signed in");
    return false;
  }

  if (!is_managed_profile) {
    LogModuleEnablement(ntp_features::kNtpCalendarModule, false,
                        "account not managed");

    // Override if in test, which must be using a command line override and
    // fake data.                           }
    return !base::GetFieldTrialParamValueByFeature(
                ntp_features::kNtpCalendarModule,
                ntp_features::kNtpCalendarModuleDataParam)
                .empty() &&
           base::CommandLine::ForCurrentProcess()->HasSwitch(
               switches::kSignedOutNtpModulesSwitch);
  }

  return IsFeatureEnabled(ntp_features::kNtpCalendarModule);
}

bool IsMostRelevantTabResumeModuleEnabled(Profile* profile) {
  if (!IsProfileSignedIn(profile)) {
    LogModuleEnablement(ntp_features::kNtpMostRelevantTabResumptionModule,
                        false, "not signed in");
    return false;
  }

  return g_browser_process &&
         page_content_annotations::features::
             ShouldExecutePageVisibilityModelOnPageContent(
                 g_browser_process->GetApplicationLocale()) &&
         base::FeatureList::IsEnabled(
             ntp_features::kNtpMostRelevantTabResumptionModule);
}

bool IsMicrosoftFilesModuleEnabledForProfile(Profile* profile) {
  if (IsFeatureEnabled(ntp_features::kNtpSharepointModule) &&
      IsFeatureEnabled(ntp_features::kNtpMicrosoftAuthenticationModule) &&
      profile->GetPrefs()->IsManagedPreference(
          prefs::kNtpSharepointModuleVisible) &&
      profile->GetPrefs()->GetBoolean(prefs::kNtpSharepointModuleVisible)) {
    return true;
  }
  LogModuleEnablement(ntp_features::kNtpSharepointModule, false,
                      "disabled by policy");
  return false;
}

bool IsOutlookCalendarModuleEnabledForProfile(Profile* profile) {
  if (IsFeatureEnabled(ntp_features::kNtpOutlookCalendarModule) &&
      IsFeatureEnabled(ntp_features::kNtpMicrosoftAuthenticationModule) &&
      profile->GetPrefs()->IsManagedPreference(
          prefs::kNtpOutlookModuleVisible) &&
      profile->GetPrefs()->GetBoolean(prefs::kNtpOutlookModuleVisible)) {
    return true;
  }
  LogModuleEnablement(ntp_features::kNtpOutlookCalendarModule, false,
                      "disabled by policy");
  return false;
}

bool IsMicrosoftModuleEnabledForProfile(Profile* profile) {
  return IsMicrosoftFilesModuleEnabledForProfile(profile) ||
         IsOutlookCalendarModuleEnabledForProfile(profile);
}

bool IsProfileSignedIn(Profile* profile) {
  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile);
  return !base::FeatureList::IsEnabled(
             ntp_features::kNtpModuleSignInRequirement) ||
         (identity_manager && identity_manager->GetAccountsInCookieJar()
                                      .GetPotentiallyInvalidSignedInAccounts()
                                      .size() > 0);
}

std::string GetVariationsServiceCountryCode(
    variations::VariationsService* variations_service) {
  std::string country_code;
  if (!variations_service) {
    return country_code;
  }
  country_code = variations_service->GetStoredPermanentCountry();
  return country_code.empty() ? variations_service->GetLatestCountry()
                              : country_code;
}

void LogModuleEnablement(const base::Feature& feature,
                         bool enabled,
                         const std::string& reason) {
  OPTIMIZATION_GUIDE_LOGGER(
      optimization_guide_common::mojom::LogSource::NTP_MODULE,
      OptimizationGuideLogger::GetInstance())
      << feature.name << (enabled ? " enabled: " : " disabled: ") << reason;
}

void LogModuleDismissed(const base::Feature& feature,
                        bool dismissed,
                        const std::string& remaining_hours) {
  std::string log = base::StrCat({feature.name, " dismissal: "});
  if (dismissed) {
    base::StrAppend(&log, {remaining_hours, " hours remaining"});
  } else {
    base::StrAppend(&log, {" not dismissed"});
  }
  OPTIMIZATION_GUIDE_LOGGER(
      optimization_guide_common::mojom::LogSource::NTP_MODULE,
      OptimizationGuideLogger::GetInstance())
      << log;
}

void LogModuleError(const base::Feature& feature,
                    const std::string& error_message) {
  OPTIMIZATION_GUIDE_LOGGER(
      optimization_guide_common::mojom::LogSource::NTP_MODULE,
      OptimizationGuideLogger::GetInstance())
      << feature.name << " error: " << error_message;
}
