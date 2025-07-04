// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PERMISSIONS_PERMISSION_REQUEST_DATA_H_
#define COMPONENTS_PERMISSIONS_PERMISSION_REQUEST_DATA_H_

#include <optional>

#include "base/values.h"
#include "components/permissions/permission_request_id.h"
#include "components/permissions/request_type.h"
#include "components/permissions/resolvers/permission_resolver.h"
#include "third_party/blink/public/mojom/permissions/permission_status.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace content {
struct PermissionRequestDescription;
}

namespace permissions {

class ContentSettingPermissionContextBase;

// Holds information about `permissions::PermissionRequest`
struct PermissionRequestData {
  PermissionRequestData(
      ContentSettingPermissionContextBase* context,
      const PermissionRequestID& id,
      const content::PermissionRequestDescription& request_description,
      const GURL& canonical_requesting_origin,
      const GURL& embedding_origin = GURL(),
      int request_description_permission_index = 0);

  PermissionRequestData(ContentSettingPermissionContextBase* context,
                        const PermissionRequestID& id,
                        bool user_gesture,
                        const GURL& requesting_origin,
                        const GURL& embedding_origin = GURL());

  PermissionRequestData(
      std::unique_ptr<permissions::PermissionResolver> resolver,
      bool user_gesture,
      const GURL& requesting_origin,
      const GURL& embedding_origin = GURL());

  PermissionRequestData& operator=(const PermissionRequestData&) = delete;
  PermissionRequestData(const PermissionRequestData&) = delete;

  PermissionRequestData& operator=(PermissionRequestData&&);
  PermissionRequestData(PermissionRequestData&&);

  ~PermissionRequestData();

  PermissionRequestData& WithRequestingOrigin(const GURL& origin) {
    requesting_origin = origin;
    return *this;
  }

  PermissionRequestData& WithEmbeddingOrigin(const GURL& origin) {
    embedding_origin = origin;
    return *this;
  }

  // The request type if it exists.
  std::optional<RequestType> request_type;

  // The permission resolver associated with the request.
  std::unique_ptr<permissions::PermissionResolver> resolver;

  //  Uniquely identifier of particular permission request.
  PermissionRequestID id;

  // Indicates the request is initiated by a user gesture.
  bool user_gesture;

  // Indicates the request is initiated from an embedded permission element.
  bool embedded_permission_element_initiated;

  // The origin on whose behalf this permission request is being made.
  GURL requesting_origin;

  // The origin of embedding frame (generally is the top level frame).
  GURL embedding_origin;

  // Anchor element position (in screen coordinates), gennerally when the
  // permission request is made from permission element. Used to calculate
  // position where the secondary prompt UI is expected to be shown.
  std::optional<gfx::Rect> anchor_element_position;

  std::vector<std::string> requested_audio_capture_device_ids;
  std::vector<std::string> requested_video_capture_device_ids;

  base::Value prompt_options;
};

}  // namespace permissions

#endif  // COMPONENTS_PERMISSIONS_PERMISSION_REQUEST_DATA_H_
