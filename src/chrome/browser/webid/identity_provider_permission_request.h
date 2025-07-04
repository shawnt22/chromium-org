// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEBID_IDENTITY_PROVIDER_PERMISSION_REQUEST_H_
#define CHROME_BROWSER_WEBID_IDENTITY_PROVIDER_PERMISSION_REQUEST_H_

#include "components/content_settings/core/common/content_settings.h"
#include "components/permissions/permission_request.h"
#include "url/origin.h"

class IdentityProviderPermissionRequest
    : public permissions::PermissionRequest {
 public:
  explicit IdentityProviderPermissionRequest(
      const url::Origin& origin,
      base::OnceCallback<void(bool accepted)> callback);
  ~IdentityProviderPermissionRequest() override;

  IdentityProviderPermissionRequest(const IdentityProviderPermissionRequest&) =
      delete;
  IdentityProviderPermissionRequest& operator=(
      const IdentityProviderPermissionRequest&) = delete;

 private:
  void PermissionDecided(
      PermissionDecision decision,
      bool is_final_decision,
      const permissions::PermissionRequestData& request_data);

  base::OnceCallback<void(bool accepted)> callback_;
};

#endif  // CHROME_BROWSER_WEBID_IDENTITY_PROVIDER_PERMISSION_REQUEST_H_
