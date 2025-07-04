// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TOP_LEVEL_STORAGE_ACCESS_API_TOP_LEVEL_STORAGE_ACCESS_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_TOP_LEVEL_STORAGE_ACCESS_API_TOP_LEVEL_STORAGE_ACCESS_PERMISSION_CONTEXT_H_

#include "base/memory/weak_ptr.h"
#include "components/permissions/content_setting_permission_context_base.h"
#include "components/permissions/permission_context_base.h"
#include "components/permissions/permission_request_data.h"
#include "net/first_party_sets/first_party_set_metadata.h"

class GURL;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class TopLevelStorageAccessRequestOutcome {
  // The request was granted because the requesting site and the top level site
  // were in the same First-Party Set.
  kGrantedByFirstPartySet = 0,

  // The request was granted because the requesting site had not yet used up its
  // allowance of implicit grants (`kStorageAccessAPIImplicitGrantLimit`).
  // kGrantedByAllowance = 1,  // Unused

  // The request was granted by the user.
  // kGrantedByUser = 2,  // Unused

  // The request was denied because the requesting site and the top level site
  // were not in the same First-Party Set.
  kDeniedByFirstPartySet = 3,

  // The request was denied by the user.
  // kDeniedByUser = 4,  // Unused

  // The request was denied because it lacked user gesture, or one of the
  // domains was invalid, or the feature was disabled.
  kDeniedByPrerequisites = 5,

  // The request was dismissed by the user.
  // kDismissedByUser = 6,  // Unused
  // The user has already been asked and made a choice (and was not asked
  // again).
  // kReusedPreviousDecision = 7,  // Unused

  // The request was denied by cookie settings
  kDeniedByCookieSettings = 8,

  kMaxValue = kDeniedByCookieSettings,
};

class TopLevelStorageAccessPermissionContext
    : public permissions::ContentSettingPermissionContextBase {
 public:
  explicit TopLevelStorageAccessPermissionContext(
      content::BrowserContext* browser_context);

  TopLevelStorageAccessPermissionContext(
      const TopLevelStorageAccessPermissionContext&) = delete;
  TopLevelStorageAccessPermissionContext& operator=(
      const TopLevelStorageAccessPermissionContext&) = delete;

  ~TopLevelStorageAccessPermissionContext() override;

  // Exposes `DecidePermission` for tests.
  void DecidePermissionForTesting(
      std::unique_ptr<permissions::PermissionRequestData> request_data,
      permissions::BrowserPermissionCallback callback);

 private:
  // PermissionContextBase:
  void DecidePermission(
      std::unique_ptr<permissions::PermissionRequestData> request_data,
      permissions::BrowserPermissionCallback callback) override;
  void NotifyPermissionSet(
      const permissions::PermissionRequestData& request_data,
      permissions::BrowserPermissionCallback callback,
      bool persist,
      PermissionDecision decision,
      bool is_final_decision) override;

  // ContentSettingPermissionContextBase
  void UpdateContentSetting(
      const permissions::PermissionRequestData& request_data,
      ContentSetting content_setting,
      bool is_one_time) override;

  // ContentSettingPermissionContextBase
  ContentSetting GetContentSettingStatusInternal(
      content::RenderFrameHost* render_frame_host,
      const GURL& requesting_origin,
      const GURL& embedding_origin) const override;

  // Internal implementation for NotifyPermissionSet.
  void NotifyPermissionSetInternal(
      const permissions::PermissionRequestData& request_data,
      permissions::BrowserPermissionCallback callback,
      bool persist,
      PermissionDecision decision,
      TopLevelStorageAccessRequestOutcome outcome);

  // Checks First-Party Sets metadata to determine whether the request should be
  // auto-rejected or auto-denied.
  void CheckForAutoGrantOrAutoDenial(
      std::unique_ptr<permissions::PermissionRequestData> request_data,
      permissions::BrowserPermissionCallback callback,
      net::FirstPartySetMetadata metadata);

  base::WeakPtrFactory<TopLevelStorageAccessPermissionContext> weak_factory_{
      this};
};

#endif  // CHROME_BROWSER_TOP_LEVEL_STORAGE_ACCESS_API_TOP_LEVEL_STORAGE_ACCESS_PERMISSION_CONTEXT_H_
