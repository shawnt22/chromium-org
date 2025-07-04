// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/webrtc/media_stream_device_permission_context.h"

#include "base/command_line.h"
#include "chrome/browser/media/webrtc/media_stream_device_permissions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "components/permissions/permission_decision.h"
#include "components/permissions/permission_util.h"
#include "content/public/browser/permission_descriptor_util.h"
#include "content/public/browser/permission_request_description.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "extensions/common/constants.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/stl_util.h"
#include "components/permissions/android/android_permission_util.h"
#include "components/permissions/android/permissions_reprompt_controller_android.h"
#include "components/permissions/permission_request_id.h"
#include "components/permissions/permissions_client.h"
#include "content/public/browser/web_contents.h"
#endif

namespace {

network::mojom::PermissionsPolicyFeature GetPermissionsPolicyFeature(
    ContentSettingsType type) {
  if (type == ContentSettingsType::MEDIASTREAM_MIC) {
    return network::mojom::PermissionsPolicyFeature::kMicrophone;
  }

  DCHECK_EQ(ContentSettingsType::MEDIASTREAM_CAMERA, type);
  return network::mojom::PermissionsPolicyFeature::kCamera;
}

}  // namespace

MediaStreamDevicePermissionContext::MediaStreamDevicePermissionContext(
    content::BrowserContext* browser_context,
    const ContentSettingsType content_settings_type)
    : permissions::ContentSettingPermissionContextBase(
          browser_context,
          content_settings_type,
          GetPermissionsPolicyFeature(content_settings_type)),
      content_settings_type_(content_settings_type) {
  DCHECK(content_settings_type_ == ContentSettingsType::MEDIASTREAM_MIC ||
         content_settings_type_ == ContentSettingsType::MEDIASTREAM_CAMERA);
}

MediaStreamDevicePermissionContext::~MediaStreamDevicePermissionContext() =
    default;

ContentSetting
MediaStreamDevicePermissionContext::GetContentSettingStatusInternal(
    content::RenderFrameHost* render_frame_host,
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {
  // TODO(raymes): Merge this policy check into content settings
  // crbug.com/244389.
  const char* policy_name = nullptr;
  const char* urls_policy_name = nullptr;
  if (content_settings_type_ == ContentSettingsType::MEDIASTREAM_MIC) {
    policy_name = prefs::kAudioCaptureAllowed;
    urls_policy_name = prefs::kAudioCaptureAllowedUrls;
  } else {
    DCHECK(content_settings_type_ == ContentSettingsType::MEDIASTREAM_CAMERA);
    policy_name = prefs::kVideoCaptureAllowed;
    urls_policy_name = prefs::kVideoCaptureAllowedUrls;
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUseFakeUIForMediaStream)) {
    bool blocked = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                       switches::kUseFakeUIForMediaStream) == "deny";
    return blocked ? CONTENT_SETTING_BLOCK : CONTENT_SETTING_ALLOW;
  }

  MediaStreamDevicePolicy policy =
      GetDevicePolicy(Profile::FromBrowserContext(browser_context()),
                      requesting_origin, policy_name, urls_policy_name);

  switch (policy) {
    case ALWAYS_DENY:
      return CONTENT_SETTING_BLOCK;
    case ALWAYS_ALLOW:
      return CONTENT_SETTING_ALLOW;
    default:
      DCHECK_EQ(POLICY_NOT_SET, policy);
  }

  // Check the content setting. TODO(raymes): currently mic/camera permission
  // doesn't consider the embedder.
  ContentSetting setting = permissions::ContentSettingPermissionContextBase::
      GetContentSettingStatusInternal(render_frame_host, requesting_origin,
                                      requesting_origin);

  if (setting == CONTENT_SETTING_DEFAULT) {
    setting = CONTENT_SETTING_ASK;
  }

  return setting;
}

#if BUILDFLAG(IS_ANDROID)
// There are two other permissions that need to check corresponding OS-level
// permissions, and they take two different approaches to this. Geolocation only
// stores the permission ContentSetting if both requests are granted (or if the
// site permission is "Block"). WebXR permissions are following the approach
// found here.
void MediaStreamDevicePermissionContext::NotifyPermissionSet(
    const permissions::PermissionRequestData& request_data,
    permissions::BrowserPermissionCallback callback,
    bool persist,
    PermissionDecision decision,
    bool is_final_decision) {
  DCHECK(is_final_decision);

  // For Android, we need to customize the ContentSettingPermissionContextBase's
  // behavior if the permission was granted. We will:
  // 1. Check if the permission was granted by the user - if not, we'll fall
  //    back to the implementation in the base class.
  // 2. Handle persisting the permission if needed (this is the same as base
  //    class implementation).
  // 3. Check if the OS permissions need to be re-prompted.
  // a) If no, we'll call base class impl. & say that the permission was granted
  //    by the user, but skip persisting it (because we already did in step 2).
  // b) If yes but we cannot show the info bar, we will call base class impl. &
  //    say that the permission was rejected by the user, and we won't persist
  //    it (because we already did in step 2 and the user didn't actually reject
  //    the permission).
  // c) If yes and we can show the info bar, we show it and propagate the answer
  //    to the base class impl., skipping persisting it (because we already
  //    persisted it and the user didn't actually reject it).
  //
  // Note that base class implementation will call into `UpdateTabContext()`
  // virtual method when we invoke `NotifyPermissionSet()` from the base class.
  // This is fine, even in 3b) and 3c), where we call it with a parameter that
  // does not correspond to user's answer to Chrome-level permission, because
  // `MediaStreamDevicePermissionContext` does *not* have a custom
  // implementation for `UpdateTabContext()` - if it did, we'd need to stop
  // calling into base class with the parameter not matching user's answer.

  DCHECK(content_settings_type_ == ContentSettingsType::MEDIASTREAM_CAMERA ||
         content_settings_type_ == ContentSettingsType::MEDIASTREAM_MIC);

  // Camera and Microphone need to check for additional permissions, but only if
  // they were actually allowed:
  if (decision != PermissionDecision::kAllow) {
    ContentSettingPermissionContextBase::NotifyPermissionSet(
        request_data, std::move(callback), persist, decision,
        is_final_decision);
    return;
  }

  // Must exist since permission requests must be initiated from an RFH
  auto* rfh = content::RenderFrameHost::FromID(
      request_data.id.global_render_frame_host_id());

  // Whether or not the user will ultimately accept the OS permissions, we want
  // to save the content_setting here if we should. This is done here because we
  // won't set `persist=true` when calling
  // `ContentSettingPermissionContextBase::NotifyPermissionSet()` after this
  // point.
  if (persist) {
    // Need to reretrieve the persisted value, since the underlying permission
    // status may have changed in the meantime.
    auto previous_content_setting = GetContentSettingStatusInternal(
        rfh, request_data.requesting_origin, request_data.embedding_origin);
    auto new_content_setting = content_settings::ValueToContentSetting(
        request_data.resolver->ComputePermissionDecisionResult(
            base::Value(previous_content_setting), decision,
            request_data.prompt_options));

    UpdateContentSetting(request_data, new_content_setting,
                         decision == PermissionDecision::kAllowThisTime);
  }

  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(rfh);

  if (!web_contents) {
    // If we can't get the web contents, we don't know the state of the OS
    // permission, so assume we don't have it.
    OnAndroidPermissionDecided(request_data.id, request_data.requesting_origin,
                               request_data.embedding_origin,
                               std::move(callback),
                               false /*permission_granted*/);
    return;
  }

  // Otherwise, the user granted permission to use `content_settings_type_`, so
  // now we need to check if we need to prompt for Android system permissions.
  std::vector<ContentSettingsType> permission_type = {content_settings_type_};

  // For PEPC-initiated permission requests we never need to handle android
  // permissions, so we can shortcut to calling NotifyPermissionSet directly.
  const auto* request = FindPermissionRequest(request_data.id);
  if (request && request->IsEmbeddedPermissionElementInitiated()) {
    ContentSettingPermissionContextBase::NotifyPermissionSet(
        request_data, std::move(callback), persist, decision,
        is_final_decision);
    return;
  }

  permissions::PermissionRepromptState reprompt_state =
      permissions::ShouldRepromptUserForPermissions(web_contents,
                                                    permission_type);
  switch (reprompt_state) {
    case permissions::PermissionRepromptState::kNoNeed:
      // We would have already returned if permission was denied by the user,
      // and this result indicates that we have all the OS permissions we need.
      OnAndroidPermissionDecided(
          request_data.id, request_data.requesting_origin,
          request_data.embedding_origin, std::move(callback),
          true /*permission_granted*/);
      return;

    case permissions::PermissionRepromptState::kCannotShow:
      // If we cannot show the info bar, then we have to assume we don't have
      // the permissions we need.
      OnAndroidPermissionDecided(
          request_data.id, request_data.requesting_origin,
          request_data.embedding_origin, std::move(callback),
          false /*permission_granted*/);
      return;

    case permissions::PermissionRepromptState::kShow:
      // Otherwise, prompt the user that we need additional permissions.
      permissions::PermissionsRepromptControllerAndroid::CreateForWebContents(
          web_contents);
      permissions::PermissionsRepromptControllerAndroid::FromWebContents(
          web_contents)
          ->RepromptPermissionRequest(
              permission_type, content_settings_type_,
              base::BindOnce(&MediaStreamDevicePermissionContext::
                                 OnAndroidPermissionDecided,
                             weak_ptr_factory_.GetWeakPtr(), request_data.id,
                             request_data.requesting_origin,
                             request_data.embedding_origin,
                             std::move(callback)));
      return;
  }
}

void MediaStreamDevicePermissionContext::OnAndroidPermissionDecided(
    const permissions::PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    permissions::BrowserPermissionCallback callback,
    bool permission_granted) {
  // If we were supposed to persist the setting we've already done so in the
  // initial override of |NotifyPermissionSet|. At this point, if the user
  // has denied the OS level permission, we want to notify the requestor that
  // the permission has been blocked.
  PermissionDecision decision = permission_granted ? PermissionDecision::kAllow
                                                   : PermissionDecision::kDeny;
  // `persist=false` because the user's response to Chrome-level permission is
  // already persisted, and `is_one_time=false` because it is only relevant when
  // persisting permission.
  ContentSettingPermissionContextBase::NotifyPermissionSet(
      permissions::PermissionRequestData(
          this, id,
          content::PermissionRequestDescription(
              content::PermissionDescriptorUtil::
                  CreatePermissionDescriptorForPermissionType(
                      permissions::PermissionUtil::
                          ContentSettingsTypeToPermissionType(
                              content_settings_type_))),
          requesting_origin, embedding_origin),
      std::move(callback), false /*persist*/, decision,
      /*is_final_decision=*/true);
}

void MediaStreamDevicePermissionContext::UpdateTabContext(
    const permissions::PermissionRequestID& id,
    const GURL& requesting_origin,
    bool allowed) {
  // See the comment in `NotifyPermissionSet()` for context on why this method
  // should be empty.
}
#endif

void MediaStreamDevicePermissionContext::ResetPermission(
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  NOTREACHED() << "ResetPermission is not implemented";
}
