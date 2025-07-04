// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/geolocation_permission_context_delegate.h"

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/permissions/permission_request_id.h"
#include "components/permissions/permission_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/origin.h"

GeolocationPermissionContextDelegate::GeolocationPermissionContextDelegate(
    content::BrowserContext* browser_context)
    : extensions_context_(Profile::FromBrowserContext(browser_context)) {}

GeolocationPermissionContextDelegate::~GeolocationPermissionContextDelegate() =
    default;

bool GeolocationPermissionContextDelegate::DecidePermission(
    const permissions::PermissionRequestData& request_data,
    permissions::BrowserPermissionCallback* callback,
    permissions::GeolocationPermissionContext* context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  bool permission_set;
  bool new_permission;
  if (extensions_context_.DecidePermission(request_data.id,
                                           request_data.requesting_origin,
                                           request_data.user_gesture, callback,
                                           &permission_set, &new_permission)) {
    DCHECK_EQ(!!*callback, permission_set);
    if (permission_set) {
      PermissionDecision decision = new_permission ? PermissionDecision::kAllow
                                                   : PermissionDecision::kDeny;
      context->NotifyPermissionSet(request_data, std::move(*callback),
                                   /*persist=*/false, decision,
                                   /*is_final_decision=*/true);
    }
    return true;
  }
  return false;
}
