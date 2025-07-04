// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DISPLAY_CAPTURE_CAPTURED_SURFACE_CONTROL_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_DISPLAY_CAPTURE_CAPTURED_SURFACE_CONTROL_PERMISSION_CONTEXT_H_

#include "components/permissions/content_setting_permission_context_base.h"
#include "components/permissions/permission_request_data.h"

namespace permissions {

class CapturedSurfaceControlPermissionContext
    : public permissions::ContentSettingPermissionContextBase {
 public:
  explicit CapturedSurfaceControlPermissionContext(
      content::BrowserContext* browser_context);
  ~CapturedSurfaceControlPermissionContext() override = default;

  CapturedSurfaceControlPermissionContext(
      const CapturedSurfaceControlPermissionContext&) = delete;
  CapturedSurfaceControlPermissionContext& operator=(
      const CapturedSurfaceControlPermissionContext&) = delete;

};

}  // namespace permissions

#endif  // CHROME_BROWSER_DISPLAY_CAPTURE_CAPTURED_SURFACE_CONTROL_PERMISSION_CONTEXT_H_
