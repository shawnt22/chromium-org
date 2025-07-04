// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/permissions/permission_manager.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/permissions/content_setting_permission_context_base.h"
#include "components/permissions/features.h"
#include "components/permissions/permission_decision.h"
#include "components/permissions/permission_request_id.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/permission_uma_util.h"
#include "components/permissions/permission_util.h"
#include "components/permissions/permissions_client.h"
#include "components/permissions/request_type.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/permission_controller.h"
#include "content/public/browser/permission_descriptor_util.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "third_party/blink/public/mojom/permissions/permission_status.mojom.h"
#include "url/origin.h"

using blink::PermissionType;

namespace permissions {
namespace {

GURL GetEmbeddingOrigin(content::RenderFrameHost* const render_frame_host,
                        const GURL& requesting_origin) {
  content::WebContents* const web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  DCHECK(web_contents);

  if (PermissionsClient::Get()->DoURLsMatchNewTabPage(
          requesting_origin,
          web_contents->GetLastCommittedURL().DeprecatedGetOriginAsURL())) {
    return web_contents->GetLastCommittedURL().DeprecatedGetOriginAsURL();
  } else {
    return PermissionUtil::GetLastCommittedOriginAsURL(
        render_frame_host->GetMainFrame());
  }
}
}  // anonymous namespace

class PermissionManager::PendingRequest {
 public:
  PendingRequest(
      content::RenderFrameHost* render_frame_host,
      std::vector<blink::mojom::PermissionDescriptorPtr> permissions,
      base::OnceCallback<void(const std::vector<PermissionStatus>&)> callback)
      : render_process_id_(render_frame_host->GetProcess()->GetDeprecatedID()),
        render_frame_id_(render_frame_host->GetRoutingID()),
        callback_(std::move(callback)),
        remaining_results_(permissions.size()),
        results_(permissions.size(), PermissionStatus::DENIED),
        permissions_(std::move(permissions)) {}

  void SetPermissionStatus(int permission_id, PermissionStatus decision) {
    DCHECK(!IsComplete());

    results_[permission_id] = decision;
    --remaining_results_;
  }

  bool IsComplete() const { return remaining_results_ == 0; }

  int render_process_id() const { return render_process_id_; }
  int render_frame_id() const { return render_frame_id_; }

  base::OnceCallback<void(const std::vector<PermissionStatus>&)>
  TakeCallback() {
    return std::move(callback_);
  }

  const std::vector<blink::mojom::PermissionDescriptorPtr>& permissions()
      const {
    return permissions_;
  }

  std::vector<PermissionStatus> results() const { return results_; }

 private:
  int render_process_id_;
  int render_frame_id_;
  base::OnceCallback<void(const std::vector<PermissionStatus>&)> callback_;
  size_t remaining_results_;
  std::vector<PermissionStatus> results_;
  std::vector<blink::mojom::PermissionDescriptorPtr> permissions_;
};

// Object to track the callback passed to
// ContentSettingPermissionContextBase::RequestPermission. The callback passed
// in will never be run when a permission prompt has been ignored, but it's
// important that we know when a prompt is ignored to clean up
// |pending_requests_| correctly. If the callback is destroyed without being
// run, the destructor here will cancel the request to clean up.
// |permission_manager| must outlive this object.
class PermissionManager::PermissionResponseCallback {
 public:
  PermissionResponseCallback(
      const base::WeakPtr<PermissionManager>& permission_manager,
      PendingRequestLocalId request_local_id,
      int permission_id)
      : permission_manager_(permission_manager),
        request_local_id_(request_local_id),
        permission_id_(permission_id),
        request_answered_(false) {}

  PermissionResponseCallback(const PermissionResponseCallback&) = delete;
  PermissionResponseCallback& operator=(const PermissionResponseCallback&) =
      delete;

  ~PermissionResponseCallback() {
    if (!request_answered_ && permission_manager_ &&
        permission_manager_->pending_requests_.Lookup(request_local_id_)) {
      permission_manager_->pending_requests_.Remove(request_local_id_);
    }
  }

  void OnPermissionsRequestResponseStatus(PermissionStatus status) {
    if (!permission_manager_) {
      return;
    }
    request_answered_ = true;
    permission_manager_->OnPermissionsRequestResponseStatus(
        request_local_id_, permission_id_, status);
  }

 private:
  base::WeakPtr<PermissionManager> permission_manager_;
  PendingRequestLocalId request_local_id_;
  int permission_id_;
  bool request_answered_;
};

PermissionManager::PermissionManager(content::BrowserContext* browser_context,
                                     PermissionContextMap permission_contexts)
    : browser_context_(browser_context),
      permission_contexts_(std::move(permission_contexts)) {
  auto* autoblocker =
      permissions::PermissionsClient::Get()->GetPermissionDecisionAutoBlocker(
          browser_context_);
  if (autoblocker) {
    autoblocker->AddObserver(this);
  }
}

PermissionManager::~PermissionManager() {
  DCHECK(pending_requests_.IsEmpty());
}

void PermissionManager::Shutdown() {
  is_shutting_down_ = true;

  if (subscriptions() && !subscriptions()->IsEmpty()) {
    SetSubscriptions(nullptr);
    for (const auto& type_to_count : subscription_type_counts_) {
      if (type_to_count.second > 0) {
        ContentSettingPermissionContextBase* context =
            GetPermissionContext(type_to_count.first);
        if (context != nullptr) {
          context->RemoveObserver(this);
        }
      }
    }
    subscription_type_counts_.clear();
  }
  permission_contexts_.clear();

  auto* autoblocker =
      permissions::PermissionsClient::Get()->GetPermissionDecisionAutoBlocker(
          browser_context_);
  if (autoblocker) {
    autoblocker->RemoveObserver(this);
  }
}

void PermissionManager::OnEmbargoStarted(const GURL& origin,
                                         ContentSettingsType content_setting) {
  auto primary_pattern = ContentSettingsPattern::FromURLNoWildcard(origin);
  OnPermissionChanged(primary_pattern, ContentSettingsPattern::Wildcard(),
                      ContentSettingsTypeSet(content_setting));
}

ContentSettingPermissionContextBase*
PermissionManager::GetPermissionContextForTesting(ContentSettingsType type) {
  return GetPermissionContext(type);
}

ContentSettingPermissionContextBase* PermissionManager::GetPermissionContext(
    ContentSettingsType type) {
  const auto& it = permission_contexts_.find(type);
  return it == permission_contexts_.end() ? nullptr : it->second.get();
}

void PermissionManager::RequestPermissions(
    content::RenderFrameHost* render_frame_host,
    const content::PermissionRequestDescription& request_description,
    base::OnceCallback<void(const std::vector<PermissionStatus>&)>
        permission_status_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RequestPermissionsInternal(render_frame_host, request_description,
                             std::move(permission_status_callback));
}

void PermissionManager::RequestPermissionsInternal(
    content::RenderFrameHost* render_frame_host,
    const content::PermissionRequestDescription& request_description,
    base::OnceCallback<void(const std::vector<PermissionStatus>&)>
        permission_status_callback) {
  std::vector<blink::mojom::PermissionDescriptorPtr> permissions;
  permissions.reserve(request_description.permissions.size());
  for (const auto& permission : request_description.permissions) {
    permissions.push_back(permission.Clone());
  }

  if (request_description.permissions.empty()) {
    std::move(permission_status_callback).Run(std::vector<PermissionStatus>());
    return;
  }

  auto request_local_id = request_local_id_generator_.GenerateNextId();
  pending_requests_.AddWithID(std::make_unique<PendingRequest>(
                                  render_frame_host, std::move(permissions),
                                  std::move(permission_status_callback)),
                              request_local_id);

  const PermissionRequestID request_id(render_frame_host, request_local_id);
  const GURL embedding_origin = GetEmbeddingOrigin(
      render_frame_host, request_description.requesting_origin);
  for (size_t i = 0; i < request_description.permissions.size(); ++i) {
    const ContentSettingsType permission =
        PermissionUtil::PermissionTypeToContentSettingsType(
            blink::PermissionDescriptorToPermissionType(
                request_description.permissions[i]));
    const GURL canonical_requesting_origin = PermissionUtil::GetCanonicalOrigin(
        permission, request_description.requesting_origin, embedding_origin);

    auto response_callback = std::make_unique<PermissionResponseCallback>(
        weak_factory_.GetWeakPtr(), request_local_id, i);
    ContentSettingPermissionContextBase* context =
        GetPermissionContext(permission);
    if (!context || PermissionUtil::IsPermissionBlockedInPartition(
                        permission, request_description.requesting_origin,
                        render_frame_host->GetProcess())) {
      response_callback->OnPermissionsRequestResponseStatus(
          PermissionStatus::DENIED);
      continue;
    }

    context->RequestPermission(
        std::make_unique<PermissionRequestData>(
            context, request_id, request_description,
            canonical_requesting_origin.DeprecatedGetOriginAsURL(), GURL(), i),
        base::BindOnce(
            &PermissionResponseCallback::OnPermissionsRequestResponseStatus,
            std::move(response_callback)));
  }
}

void PermissionManager::ResetPermission(PermissionType permission,
                                        const GURL& requesting_origin,
                                        const GURL& embedding_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ContentSettingsType type =
      PermissionUtil::PermissionTypeToContentSettingsType(permission);
  ContentSettingPermissionContextBase* context = GetPermissionContext(type);
  if (!context)
    return;
  context->ResetPermission(PermissionUtil::GetCanonicalOrigin(
                               type, requesting_origin, embedding_origin),
                           embedding_origin.DeprecatedGetOriginAsURL());
}

void PermissionManager::RequestPermissionsFromCurrentDocument(
    content::RenderFrameHost* render_frame_host,
    const content::PermissionRequestDescription& request_description,
    base::OnceCallback<void(const std::vector<PermissionStatus>&)>
        permission_status_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  RequestPermissionsInternal(render_frame_host, request_description,
                             std::move(permission_status_callback));
}

PermissionStatus PermissionManager::GetPermissionStatus(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  // TODO(benwells): split this into two functions, GetPermissionStatus and
  // GetPermissionStatusForPermissionsAPI.
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  return GetPermissionStatusInternal(permission_descriptor,
                                     /*render_process_host=*/nullptr,
                                     /*render_frame_host=*/nullptr,
                                     requesting_origin, embedding_origin,
                                     /*should_include_device_status=*/false)
      .status;
}

content::PermissionResult
PermissionManager::GetPermissionResultForOriginWithoutContext(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    const url::Origin& requesting_origin,
    const url::Origin& embedding_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  return GetPermissionStatusInternal(
      permission_descriptor,
      /*render_process_host=*/nullptr,
      /*render_frame_host=*/nullptr, requesting_origin.GetURL(),
      embedding_origin.GetURL(), /*should_include_device_status=*/false);
}

PermissionStatus PermissionManager::GetPermissionStatusForCurrentDocument(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderFrameHost* render_frame_host,
    bool should_include_device_status) {
  return GetPermissionResultForCurrentDocument(permission_descriptor,
                                               render_frame_host,
                                               should_include_device_status)
      .status;
}

content::PermissionResult
PermissionManager::GetPermissionResultForCurrentDocument(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderFrameHost* render_frame_host,
    bool should_include_device_status) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const GURL requesting_origin =
      PermissionUtil::GetLastCommittedOriginAsURL(render_frame_host);
  const GURL embedding_origin =
      GetEmbeddingOrigin(render_frame_host, requesting_origin);

  return GetPermissionStatusInternal(
      permission_descriptor,
      /*render_process_host=*/nullptr, render_frame_host, requesting_origin,
      embedding_origin, should_include_device_status);
}

PermissionStatus PermissionManager::GetPermissionStatusForWorker(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderProcessHost* render_process_host,
    const GURL& worker_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  return GetPermissionStatusInternal(permission_descriptor, render_process_host,
                                     /*render_frame_host=*/nullptr,
                                     worker_origin, worker_origin,
                                     /*should_include_device_status=*/false)
      .status;
}

PermissionStatus PermissionManager::GetPermissionStatusForEmbeddedRequester(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderFrameHost* render_frame_host,
    const url::Origin& requesting_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const GURL embedding_origin =
      GetEmbeddingOrigin(render_frame_host, requesting_origin.GetURL());

  return GetPermissionStatusInternal(
             permission_descriptor,
             /*render_process_host=*/nullptr, render_frame_host,
             requesting_origin.GetURL(), embedding_origin,
             /*should_include_device_status=*/false)
      .status;
}

bool PermissionManager::IsPermissionOverridable(
    PermissionType permission,
    const std::optional<url::Origin>& origin) {
  ContentSettingsType type =
      PermissionUtil::PermissionTypeToContentSettingsTypeSafe(permission);
  ContentSettingPermissionContextBase* context = GetPermissionContext(type);

  if (!context || context->IsPermissionKillSwitchOn())
    return false;

  return !origin || context->IsPermissionAvailableToOrigins(origin->GetURL(),
                                                            origin->GetURL());
}

void PermissionManager::OnPermissionStatusChangeSubscriptionAdded(
    content::PermissionController::SubscriptionId subscription_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (is_shutting_down_)
    return;
  if (!subscriptions() || subscriptions()->IsEmpty()) {
    return;
  }
  content::PermissionStatusSubscription* subscription =
      subscriptions()->Lookup(subscription_id);
  if (!subscription) {
    return;
  }
  ContentSettingsType content_type =
      PermissionUtil::PermissionTypeToContentSettingsType(
          subscription->permission);
  auto& type_count = subscription_type_counts_[content_type];
  if (type_count == 0) {
    ContentSettingPermissionContextBase* context =
        GetPermissionContext(content_type);
    if (context == nullptr) {
      return;
    }
    context->AddObserver(this);
  }
  ++type_count;

  if (subscription->render_frame_id != -1) {
    subscription->embedding_origin = GetEmbeddingOrigin(
        content::RenderFrameHost::FromID(subscription->render_process_id,
                                         subscription->render_frame_id),
        subscription->requesting_origin);
    // TODO(crbug.com/408965890): Add support for multi-state permissions. The
    // following won't work for detecting changes in permission options.
    subscription->permission_result = GetPermissionStatusInternal(
        content::PermissionDescriptorUtil::
            CreatePermissionDescriptorForPermissionType(
                permissions::PermissionUtil::
                    ContentSettingsTypeToPermissionType(content_type)),
        /*render_process_host=*/nullptr,
        content::RenderFrameHost::FromID(subscription->render_process_id,
                                         subscription->render_frame_id),
        subscription->requesting_origin, subscription->embedding_origin,
        subscription->should_include_device_status);
  } else {
    // TODO(crbug.com/408965890): Add support for multi-state permissions. The
    // following won't work for detecting changes in permission options.
    subscription->permission_result = GetPermissionStatusInternal(
        content::PermissionDescriptorUtil::
            CreatePermissionDescriptorForPermissionType(
                permissions::PermissionUtil::
                    ContentSettingsTypeToPermissionType(content_type)),
        content::RenderProcessHost::FromID(subscription->render_process_id),
        /*render_frame_host=*/nullptr, subscription->requesting_origin,
        subscription->embedding_origin,
        subscription->should_include_device_status);
  }
  subscription->requesting_origin_delegation =
      PermissionUtil::GetCanonicalOrigin(content_type,
                                         subscription->requesting_origin,
                                         subscription->embedding_origin);
}

void PermissionManager::UnsubscribeFromPermissionStatusChange(
    content::PermissionController::SubscriptionId subscription_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (is_shutting_down_)
    return;

  if (!subscriptions()) {
    return;
  }
  content::PermissionStatusSubscription* subscription =
      subscriptions()->Lookup(subscription_id);
  if (!subscription)
    return;

  ContentSettingsType type =
      PermissionUtil::PermissionTypeToContentSettingsType(
          subscription->permission);
  auto type_count = subscription_type_counts_.find(type);
  CHECK(type_count != subscription_type_counts_.end());
  // type_count is zero only in the tests that we are directly calling
  // subscribing functions but is not subscribing to any real permission
  // context.
  ContentSettingPermissionContextBase* context = GetPermissionContext(type);
  if (type_count->second == 0) {
    if (context == nullptr) {
      return;
    }
  }
  CHECK_GT(type_count->second, size_t(0));
  type_count->second--;
  if (type_count->second == 0) {
    if (context != nullptr) {
      context->RemoveObserver(this);
    }
  }
}

std::optional<gfx::Rect> PermissionManager::GetExclusionAreaBoundsInScreen(
    content::WebContents* web_contents) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto* manager = PermissionRequestManager::FromWebContents(web_contents);
  return manager ? manager->GetPromptBubbleViewBoundsInScreen() : std::nullopt;
}

void PermissionManager::OnPermissionsRequestResponseStatus(
    PendingRequestLocalId request_local_id,
    int permission_id,
    PermissionStatus status) {
  PendingRequest* pending_request = pending_requests_.Lookup(request_local_id);
  if (!pending_request)
    return;

  pending_request->SetPermissionStatus(permission_id, status);

  if (!pending_request->IsComplete())
    return;

  pending_request->TakeCallback().Run(pending_request->results());
  pending_requests_.Remove(request_local_id);
}

void PermissionManager::OnPermissionChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsTypeSet content_type_set) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(primary_pattern.IsValid());
  DCHECK(secondary_pattern.IsValid());
  if (!subscriptions()) {
    return;
  }

  std::vector<base::OnceClosure> callbacks;
  callbacks.reserve(subscriptions()->size());
  for (content::PermissionController::SubscriptionsMap::iterator iter(
           subscriptions());
       !iter.IsAtEnd(); iter.Advance()) {
    content::PermissionStatusSubscription* subscription =
        iter.GetCurrentValue();
    if (!subscription) {
      continue;
    }
    if (!content_type_set.Contains(
            PermissionUtil::PermissionTypeToContentSettingsType(
                subscription->permission))) {
      continue;
    }

    // The RFH may be null if the request is for a worker.
    content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(
        subscription->render_process_id, subscription->render_frame_id);
    GURL embedding_origin;
    GURL requesting_origin_delegation =
        subscription->requesting_origin_delegation;
    if (rfh) {
      embedding_origin = GetEmbeddingOrigin(rfh, requesting_origin_delegation);
    } else {
      embedding_origin = requesting_origin_delegation;
    }

    if (!primary_pattern.Matches(requesting_origin_delegation) ||
        !secondary_pattern.Matches(embedding_origin)) {
      continue;
    }

    content::RenderProcessHost* rph =
        rfh ? nullptr
            : content::RenderProcessHost::FromID(
                  subscription->render_process_id);

    content::PermissionResult new_value = GetPermissionStatusInternal(
        content::PermissionDescriptorUtil::
            CreatePermissionDescriptorForPermissionType(
                subscription->permission),
        rph, rfh, subscription->requesting_origin_delegation, embedding_origin,
        subscription->should_include_device_status);

    if (subscription->permission_result &&
        subscription->permission_result->status == new_value.status) {
      continue;
    }

    subscription->permission_result = new_value;

    // Add the callback to |callbacks| which will be run after the loop to
    // prevent re-entrance issues.
    callbacks.push_back(base::BindOnce(subscription->callback, new_value.status,
                                       /*ignore_status_override=*/false));
  }

  for (auto& callback : callbacks)
    std::move(callback).Run();
}

content::PermissionResult PermissionManager::GetPermissionStatusInternal(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderProcessHost* render_process_host,
    content::RenderFrameHost* render_frame_host,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    bool should_include_device_status) {
  DCHECK(!render_process_host || !render_frame_host);

  // TODO(crbug.com/40218610): Move this to ContentSettingPermissionContextBase.
  content::RenderProcessHost* rph =
      render_frame_host ? render_frame_host->GetProcess() : render_process_host;

  auto content_settings_type =
      PermissionUtil::PermissionTypeToContentSettingsType(
          blink::PermissionDescriptorToPermissionType(permission_descriptor));
  ContentSettingPermissionContextBase* context =
      GetPermissionContext(content_settings_type);

  if (!context || (rph && PermissionUtil::IsPermissionBlockedInPartition(
                              content_settings_type, requesting_origin, rph))) {
    return content::PermissionResult(
        PermissionStatus::DENIED, content::PermissionStatusSource::UNSPECIFIED);
  }

  GURL canonical_requesting_origin = PermissionUtil::GetCanonicalOrigin(
      content_settings_type, requesting_origin, embedding_origin);
  content::PermissionResult result = context->GetPermissionStatus(
      permission_descriptor, render_frame_host,
      canonical_requesting_origin.DeprecatedGetOriginAsURL(),
      embedding_origin.DeprecatedGetOriginAsURL());
  content::WebContents* const web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  if (should_include_device_status || context->AlwaysIncludeDeviceStatus()) {
    result = context->UpdatePermissionStatusWithDeviceStatus(
        web_contents, result, requesting_origin, embedding_origin);
  } else {
    // Give the context an opportunity to still check the device status and
    // maybe notify observers.
    context->MaybeUpdateCachedHasDevicePermission(web_contents);
  }
  DCHECK(result.status == PermissionStatus::GRANTED ||
         result.status == PermissionStatus::ASK ||
         result.status == PermissionStatus::DENIED);

  return result;
}

}  // namespace permissions
