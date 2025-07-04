// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_PERMISSION_CONTEXT_H_

#include "components/content_settings/core/common/content_settings.h"
#include "components/permissions/content_setting_permission_context_base.h"

class GURL;

// Manages user permissions for Background Fetch. Background Fetch permission
// is currently dynamic and relies on either the download status from
// DownloadRequestLimiter, or the Automatic Downloads content setting
// This is why it isn't persisted.
class BackgroundFetchPermissionContext
    : public permissions::ContentSettingPermissionContextBase {
 public:
  explicit BackgroundFetchPermissionContext(
      content::BrowserContext* browser_context);

  BackgroundFetchPermissionContext(const BackgroundFetchPermissionContext&) =
      delete;
  BackgroundFetchPermissionContext& operator=(
      const BackgroundFetchPermissionContext&) = delete;

  ~BackgroundFetchPermissionContext() override = default;

 private:
  // ContentSettingPermissionContextBase implementation.
  ContentSetting GetContentSettingStatusInternal(
      content::RenderFrameHost* render_frame_host,
      const GURL& requesting_origin,
      const GURL& embedding_origin) const override;

  // PermissionContextBase implementation.
  void DecidePermission(
      std::unique_ptr<permissions::PermissionRequestData> request_data,
      permissions::BrowserPermissionCallback callback) override;
  void NotifyPermissionSet(
      const permissions::PermissionRequestData& request_data,
      permissions::BrowserPermissionCallback callback,
      bool persist,
      PermissionDecision decision,
      bool is_final_decision) override;
};

#endif  // CHROME_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_PERMISSION_CONTEXT_H_
