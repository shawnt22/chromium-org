// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DISPLAY_CAPTURE_DISPLAY_CAPTURE_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_DISPLAY_CAPTURE_DISPLAY_CAPTURE_PERMISSION_CONTEXT_H_

#include "components/content_settings/core/common/content_settings.h"
#include "components/permissions/content_setting_permission_context_base.h"

class DisplayCapturePermissionContext
    : public permissions::ContentSettingPermissionContextBase {
 public:
  explicit DisplayCapturePermissionContext(
      content::BrowserContext* browser_context);
  ~DisplayCapturePermissionContext() override = default;

  DisplayCapturePermissionContext(const DisplayCapturePermissionContext&) =
      delete;
  DisplayCapturePermissionContext& operator=(
      const DisplayCapturePermissionContext&) = delete;

 protected:
  // PermissionContextBase
  void DecidePermission(
      std::unique_ptr<permissions::PermissionRequestData> request_data,
      permissions::BrowserPermissionCallback callback) override;

  // ContentSettingPermissionContextBase
  ContentSetting GetContentSettingStatusInternal(
      content::RenderFrameHost* render_frame_host,
      const GURL& requesting_origin,
      const GURL& embedding_origin) const override;

  // ContentSettingPermissionContextBase
  void UpdateContentSetting(
      const permissions::PermissionRequestData& request_data,
      ContentSetting content_setting,
      bool is_one_time) override;
};

#endif  // CHROME_BROWSER_DISPLAY_CAPTURE_DISPLAY_CAPTURE_PERMISSION_CONTEXT_H_
