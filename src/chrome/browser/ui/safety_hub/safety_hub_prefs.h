// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SAFETY_HUB_SAFETY_HUB_PREFS_H_
#define CHROME_BROWSER_UI_SAFETY_HUB_SAFETY_HUB_PREFS_H_

#include "build/build_config.h"

class PrefRegistrySimple;

namespace safety_hub_prefs {

#if !BUILDFLAG(IS_ANDROID)
// Dictionary that determines the next time SafetyHub will trigger a background
// password check.
inline constexpr char kBackgroundPasswordCheckTimeAndInterval[] =
    "profile.background_password_check";

// Keys used inside the `kBackgroundPasswordCheckTimeAndInterval` pref dict.
inline constexpr char kNextPasswordCheckTimeKey[] = "next_check_time";
inline constexpr char kPasswordCheckIntervalKey[] = "check_interval";
inline constexpr char kPasswordCheckMonWeight[] = "check_mon_weight";
inline constexpr char kPasswordCheckTueWeight[] = "check_tue_weight";
inline constexpr char kPasswordCheckWedWeight[] = "check_wed_weight";
inline constexpr char kPasswordCheckThuWeight[] = "check_thu_weight";
inline constexpr char kPasswordCheckFriWeight[] = "check_fri_weight";
inline constexpr char kPasswordCheckSatWeight[] = "check_sat_weight";
inline constexpr char kPasswordCheckSunWeight[] = "check_sun_weight";

#else   // BUILDFLAG(IS_ANDROID)

// An integer count of how many account-level weak credentials were detected by
// GMSCore.
inline constexpr char kWeakCredentialsCount[] =
    "profile.safety_hub_weak_credentials_count";

// An integer count of how many account-level reused credentials were detected
// by GMSCore.
inline constexpr char kReusedCredentialsCount[] =
    "profile.safety_hub_reused_credentials_count";

// An integer count of how many local-level breached credentials were detected
// by GMSCore.
inline constexpr char kLocalBreachedCredentialsCount[] =
    "profile.safety_hub_local_breached_credentials_count";

// An integer count of how many local-level weak credentials were detected by
// GMSCore.
inline constexpr char kLocalWeakCredentialsCount[] =
    "profile.safety_hub_local_weak_credentials_count";

// An integer count of how many local-level reused credentials were detected
// by GMSCore.
inline constexpr char kLocalReusedCredentialsCount[] =
    "profile.safety_hub_reused_local_credentials_count";

// A long that represents the last time in milliseconds that a check for
// account-level credentials was triggered in GMSCore by Chrome.
inline constexpr char kLastTimeInMsAccountPasswordCheckCompleted[] =
    "profile.safety_hub_last_time_in_ms_account_password_check_completed";

// A long that represents the last time in milliseconds that a check for
// local-level credentials was triggered in GMSCore by Chrome.
inline constexpr char kLastTimeInMsLocalPasswordCheckCompleted[] =
    "profile.safety_hub_last_time_in_ms_local_password_check_completed";
#endif  // !BUILDFLAG(IS_ANDROID)

// A long that represents the last time in milliseconds that the blocklist used
// for abusive notification revocation was checked.
inline constexpr char
    kLastTimeInMsAbusiveNotificationBlocklistCheckCompleted[] =
        "profile.safety_hub_last_time_in_ms_abusive_notification_blocklist_"
        "check_completed";

// Dictionary that holds the notifications in the three-dot menu and their
// associated results.
inline const char kMenuNotificationsPrefsKey[] =
    "profile.safety_hub_menu_notifications";

// Boolean that specifies whether or not unused site permissions should be
// revoked by Safety Hub. It is used only when kSafetyHub flag is on.
// Conditioned because currently Safety Hub is available only on desktop and
// Android.
inline const char kUnusedSitePermissionsRevocationEnabled[] =
    "safety_hub.unused_site_permissions_revocation.enabled";

// Boolean that indicates whether the revoked permissions have successfully
// migrated to use string key values instead of integer key values.
inline const char kUnusedSitePermissionsRevocationMigrationCompleted[] =
    "safety_hub.unused_site_permissions_revocation.migration_completed";

}  // namespace safety_hub_prefs

void RegisterSafetyHubProfilePrefs(PrefRegistrySimple* registry);

#endif  // CHROME_BROWSER_UI_SAFETY_HUB_SAFETY_HUB_PREFS_H_
