// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SAFETY_HUB_REVOKED_PERMISSIONS_SERVICE_H_
#define CHROME_BROWSER_UI_SAFETY_HUB_REVOKED_PERMISSIONS_SERVICE_H_

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/ui/safety_hub/abusive_notification_permissions_manager.h"
#include "chrome/browser/ui/safety_hub/disruptive_notification_permissions_manager.h"
#include "chrome/browser/ui/safety_hub/revoked_permissions_result.h"
#include "chrome/browser/ui/safety_hub/safety_hub_service.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class PrefChangeRegistrar;
class PrefService;

namespace url {
class Origin;
}

namespace content {
class Page;
}  // namespace content

// This class keeps track of revoked permissions, including unused permissions,
// abusive and disruptive notifications. For unused permissions, it updates
// their last_visit date on navigations and clears them periodically.
class RevokedPermissionsService final : public SafetyHubService,
                                        public content_settings::Observer {
 public:
  class TabHelper : public content::WebContentsObserver,
                    public content::WebContentsUserData<TabHelper> {
   public:
    TabHelper(const TabHelper&) = delete;
    TabHelper& operator=(const TabHelper&) = delete;
    ~TabHelper() override;

    // WebContentsObserver:
    void PrimaryPageChanged(content::Page& page) override;

   private:
    explicit TabHelper(
        content::WebContents* web_contents,
        RevokedPermissionsService* unused_site_permission_service);

    base::WeakPtr<RevokedPermissionsService> unused_site_permission_service_;

    friend class content::WebContentsUserData<TabHelper>;
    WEB_CONTENTS_USER_DATA_KEY_DECL();
  };

  explicit RevokedPermissionsService(content::BrowserContext* browser_context,
                                     PrefService* prefs);

  RevokedPermissionsService(const RevokedPermissionsService&) = delete;
  RevokedPermissionsService& operator=(const RevokedPermissionsService&) =
      delete;

  ~RevokedPermissionsService() override;

  // content_settings::Observer implementation.
  void OnContentSettingChanged(
      const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern,
      ContentSettingsTypeSet content_type_set) override;

  // KeyedService implementation.
  void Shutdown() override;

  // If the user clicked "Allow again" for an auto-revoked origin, the
  // permissions for that site should not be auto-revoked again by the service.
  void IgnoreOriginForAutoRevocation(const url::Origin& origin);

  // Re-grants permissions that are auto-revoked ones and removes the origin
  // from revoked permissions list.
  void RegrantPermissionsForOrigin(const url::Origin& origin);

  // Reverse changes made by |RegrantPermissionsForOrigin|. Adds this origin to
  // the removed permissions list and resets its permissions.
  void UndoRegrantPermissionsForOrigin(const PermissionsData& permission);

  // Clear the list of revoked permissions so they will no longer be shown to
  // the user. Does not change permissions themselves.
  void ClearRevokedPermissionsList();

  // Restores the list of revoked permissions after it was deleted after user
  // has accepted the revocation (via `ClearRevokedPermissionsList()`).
  void RestoreDeletedRevokedPermissionsList(
      const std::vector<PermissionsData>& permissions_data_list);

  // Returns the list of all permissions that have been revoked.
  std::unique_ptr<RevokedPermissionsResult> GetRevokedPermissions();

  // Stops or restarts permissions autorevocation upon the pref change.
  void OnPermissionsAutorevocationControlChanged();

  // Does most of the heavy lifting of the update process: for each permission,
  // it determines whether it should be considered as recently unused (i.e. one
  // week). This list will be further filtered in the UI task to determine which
  // permissions should be revoked.
  static std::unique_ptr<Result> UpdateOnBackgroundThread(
      base::Clock* clock,
      const scoped_refptr<HostContentSettingsMap> hcsm);

  // Helpers to convert content settings between enum int and string name.
  static std::string ConvertContentSettingsTypeToKey(ContentSettingsType type);
  static ContentSettingsType ConvertKeyToContentSettingsType(
      const std::string& key);

  // Helper to convert single origin primary pattern to an origin.
  // Converting a primary pattern to an origin is normally an anti-pattern, and
  // this method should only be used for single origin primary patterns.
  // They have fully defined URL+scheme+port which makes converting
  // a primary pattern to an origin successful.
  static url::Origin ConvertPrimaryPatternToOrigin(
      const ContentSettingsPattern& primary_pattern);

  // SafetyHubService implementation
  // Returns a weak pointer to the service.
  base::WeakPtr<SafetyHubService> GetAsWeakRef() override;

  // TabHelper needs a weak pointer to the implementation type.
  base::WeakPtr<RevokedPermissionsService> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Test support:
  void SetClockForTesting(base::Clock* clock);
  std::vector<ContentSettingEntry> GetTrackedUnusedPermissionsForTesting();

  using UnusedPermissionMap = RevokedPermissionsResult::UnusedPermissionMap;

 private:
  FRIEND_TEST_ALL_PREFIXES(RevokedPermissionsServiceTest,
                           UpdateIntegerValuesToGroupName_AllContentSettings);
  FRIEND_TEST_ALL_PREFIXES(
      RevokedPermissionsServiceTest,
      UpdateIntegerValuesToGroupName_SubsetOfContentSettings);
  FRIEND_TEST_ALL_PREFIXES(
      RevokedPermissionsServiceTest,
      UpdateIntegerValuesToGroupName_UnknownContentSettings);
  FRIEND_TEST_ALL_PREFIXES(RevokedPermissionsServiceTest,
                           UpdateIntegerValuesToGroupName_OnStartUp);
  FRIEND_TEST_ALL_PREFIXES(RevokedPermissionsServiceTest,
                           UpdateIntegerValuesToGroupName_MixedKeys);
  // Called by TabHelper when a URL was visited.
  void OnPageVisited(const url::Origin& origin);

  // Removes a pattern from the list of revoked permissions so that the entry is
  // no longer shown to the user. Does not affect permissions themselves.
  void DeletePatternFromRevokedPermissionList(
      const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern);

  // Revokes permissions that belong to sites that were last visited over 60
  // days ago.
  void RevokeUnusedPermissions();

  // Stores revoked permissions data on HCSM.
  void StorePermissionInUnusedSitePermissionSetting(
      const std::set<ContentSettingsType>& permissions,
      const base::Value::Dict& chooser_permissions_data,
      const std::optional<content_settings::ContentSettingConstraints>
          constraint,
      const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern);

  HostContentSettingsMap* hcsm() {
    return HostContentSettingsMapFactory::GetForProfile(browser_context_.get());
  }

  void MaybeStartRepeatedUpdates();

  // SafetyHubService implementation

  std::unique_ptr<SafetyHubService::Result> InitializeLatestResultImpl()
      override;

  // Returns the interval at which the repeated updates will be run.
  base::TimeDelta GetRepeatedUpdateInterval() override;

  // Returns a reference to the static |UpdateOnBackgroundThread| function,
  // bound with a |Result| containing a reference to the clock and
  // host content settings map.
  base::OnceCallback<std::unique_ptr<Result>()> GetBackgroundTask() override;

  // Uses the |UnusedPermissionMap| from the background task to determine which
  // permissions should be revoked, revokes them and returns the list of revoked
  // permissions.
  std::unique_ptr<Result> UpdateOnUIThread(
      std::unique_ptr<Result> result) override;

  // Returns if the permissions auto-revocation is enabled for unused sites.
  bool IsUnusedSiteAutoRevocationEnabled();

  // Returns true if all features are enabled to automatically revoke abusive
  // notification permissions.
  bool IsAbusiveNotificationAutoRevocationEnabled();

  // Since the permissions are a const set, reconstruct a const set
  // of unused site content setting types by removing `NOTIFICATIONS`
  // from the set if it is in there.
  const std::set<ContentSettingsType> GetRevokedUnusedSitePermissionTypes(
      const std::set<ContentSettingsType> permissions);

  // Convert all integer permission values to string, if there is any permission
  // represented by integer.
  void UpdateIntegerValuesToGroupName();

  // Set of permissions that haven't been used for at least a week.
  UnusedPermissionMap recently_unused_permissions_;

  raw_ptr<content::BrowserContext> browser_context_;

  // Observer to watch for content settings changed.
  base::ScopedObservation<HostContentSettingsMap, content_settings::Observer>
      content_settings_observation_{this};

  // Observes user profile prefs.
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;

  raw_ptr<base::Clock> clock_;

  // Object for managing Safe Browsing blocklist checks and notification
  // revocation for abusive sites.
  std::unique_ptr<AbusiveNotificationPermissionsManager>
      abusive_notification_manager_;

  // Object for notification revocation for disruptive sites.
  std::unique_ptr<DisruptiveNotificationPermissionsManager>
      disruptive_notification_manager_;

  // Returns true if automatic check and revocation of unused site permissions
  // is occurring. This value is used in `OnContentSettingChanged` to help
  // decide whether to clean up revoked permission data.
  bool is_unused_site_revocation_running = false;

  base::WeakPtrFactory<RevokedPermissionsService> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_SAFETY_HUB_REVOKED_PERMISSIONS_SERVICE_H_
