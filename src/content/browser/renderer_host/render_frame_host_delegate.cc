// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_frame_host_delegate.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "build/build_config.h"
#include "content/public/browser/cookie_access_details.h"
#include "content/public/browser/trust_token_access_details.h"
#include "ipc/ipc_message.h"
#include "services/network/public/cpp/permissions_policy/permissions_policy_declaration.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "third_party/blink/public/mojom/frame/text_autosizer_page_info.mojom.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/gfx/native_widget_types.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

blink::mojom::PartitionedPopinParamsPtr
PartitionedPopinOpenerProperties::AsMojom() const {
  return blink::mojom::PartitionedPopinParams::New(top_frame_origin,
                                                   site_for_cookies);
}

bool RenderFrameHostDelegate::OnMessageReceived(
    RenderFrameHostImpl* render_frame_host,
    const IPC::Message& message) {
  return false;
}

bool RenderFrameHostDelegate::DidAddMessageToConsole(
    RenderFrameHostImpl* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id,
    const std::optional<std::u16string>& untrusted_stack_trace) {
  return false;
}

void RenderFrameHostDelegate::RequestMediaAccessPermission(
    RenderFrameHostImpl* render_frame_host,
    const MediaStreamRequest& request,
    MediaResponseCallback callback) {
  LOG(ERROR) << "RenderFrameHostDelegate::RequestMediaAccessPermission: "
             << "Not supported.";
  std::move(callback).Run(blink::mojom::StreamDevicesSet(),
                          blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED,
                          std::unique_ptr<MediaStreamUI>());
}

void RenderFrameHostDelegate::ProcessSelectAudioOutput(
    const SelectAudioOutputRequest& request,
    SelectAudioOutputCallback callback) {
  LOG(ERROR) << "RenderFrameHostDelegate::ProcessSelectAudioOutput: "
             << "Not supported.";
  std::move(callback).Run(
      base::unexpected(content::SelectAudioOutputError::kNotSupported));
}

bool RenderFrameHostDelegate::CheckMediaAccessPermission(
    RenderFrameHostImpl* render_frame_host,
    const url::Origin& security_origin,
    blink::mojom::MediaStreamType type) {
  LOG(ERROR) << "RenderFrameHostDelegate::CheckMediaAccessPermission: "
             << "Not supported.";
  return false;
}

ui::AXMode RenderFrameHostDelegate::GetAccessibilityMode() {
  return ui::AXMode();
}

device::mojom::GeolocationContext*
RenderFrameHostDelegate::GetGeolocationContext() {
  return nullptr;
}

#if BUILDFLAG(IS_ANDROID) || (BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_IOS_TVOS))
void RenderFrameHostDelegate::GetNFC(
    RenderFrameHostImpl* render_frame_host,
    mojo::PendingReceiver<device::mojom::NFC> receiver) {}
#endif

bool RenderFrameHostDelegate::CanEnterFullscreenMode(
    RenderFrameHostImpl* requesting_frame) {
  return true;
}

void RenderFrameHostDelegate::FullscreenStateChanged(
    RenderFrameHostImpl* rfh,
    bool is_fullscreen,
    blink::mojom::FullscreenOptionsPtr options) {}

bool RenderFrameHostDelegate::CanUseWindowingControls(RenderFrameHostImpl*) {
  return false;
}

bool RenderFrameHostDelegate::IsInnerWebContentsForGuest() {
  return false;
}

RenderFrameHostImpl* RenderFrameHostDelegate::GetFocusedFrame() {
  return nullptr;
}

FrameTree* RenderFrameHostDelegate::CreateNewWindow(
    RenderFrameHostImpl* opener,
    const mojom::CreateNewWindowParams& params,
    bool is_new_browsing_instance,
    bool has_user_gesture,
    SessionStorageNamespace* session_storage_namespace) {
  return nullptr;
}

WebContents* RenderFrameHostDelegate::ShowCreatedWindow(
    RenderFrameHostImpl* opener,
    int main_frame_widget_route_id,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture) {
  return nullptr;
}

bool RenderFrameHostDelegate::ShouldAllowRunningInsecureContent(
    bool allowed_per_prefs,
    const url::Origin& origin,
    const GURL& resource_url) {
  return false;
}

#if BUILDFLAG(IS_ANDROID)
base::android::ScopedJavaLocalRef<jobject>
RenderFrameHostDelegate::GetJavaRenderFrameHostDelegate() {
  return nullptr;
}
#endif

Visibility RenderFrameHostDelegate::GetVisibility() {
  return Visibility::HIDDEN;
}

std::vector<FrameTreeNode*> RenderFrameHostDelegate::GetUnattachedOwnedNodes(
    RenderFrameHostImpl* owner) {
  return {};
}

void RenderFrameHostDelegate::IsClipboardPasteAllowedByPolicy(
    const ClipboardEndpoint& source,
    const ClipboardEndpoint& destination,
    const ClipboardMetadata& metadata,
    ClipboardPasteData clipboard_paste_data,
    IsClipboardPasteAllowedCallback callback) {
  std::move(callback).Run(std::move(clipboard_paste_data));
}

std::optional<std::vector<std::u16string>>
RenderFrameHostDelegate::GetClipboardTypesIfPolicyApplied(
    const ui::ClipboardSequenceNumberToken& seqno) {
  return std::nullopt;
}

bool RenderFrameHostDelegate::IsTransientActivationRequiredForHtmlFullscreen() {
  return true;
}

bool RenderFrameHostDelegate::IsBackForwardCacheSupported() {
  return false;
}

RenderWidgetHostImpl* RenderFrameHostDelegate::CreateNewPopupWidget(
    base::SafeRef<SiteInstanceGroup> site_instance_group,
    int32_t route_id,
    mojo::PendingAssociatedReceiver<blink::mojom::PopupWidgetHost>
        blink_popup_widget_host,
    mojo::PendingAssociatedReceiver<blink::mojom::WidgetHost> blink_widget_host,
    mojo::PendingAssociatedRemote<blink::mojom::Widget> blink_widget) {
  return nullptr;
}

std::vector<RenderFrameHostImpl*>
RenderFrameHostDelegate::GetActiveTopLevelDocumentsInBrowsingContextGroup(
    RenderFrameHostImpl* render_frame_host) {
  return std::vector<RenderFrameHostImpl*>();
}

PrerenderHostRegistry* RenderFrameHostDelegate::GetPrerenderHostRegistry() {
  return nullptr;
}

bool RenderFrameHostDelegate::IsAllowedToGoToEntryAtOffset(int32_t offset) {
  return true;
}

bool RenderFrameHostDelegate::IsJavaScriptDialogShowing() const {
  return false;
}

bool RenderFrameHostDelegate::ShouldIgnoreUnresponsiveRenderer() {
  return false;
}

std::optional<network::ParsedPermissionsPolicy>
RenderFrameHostDelegate::GetPermissionsPolicyForIsolatedWebApp(
    RenderFrameHostImpl* source) {
  return network::ParsedPermissionsPolicy();
}

bool RenderFrameHostDelegate::IsPopup() const {
  return false;
}

bool RenderFrameHostDelegate::IsPartitionedPopin() const {
  return false;
}

const PartitionedPopinOpenerProperties&
RenderFrameHostDelegate::GetPartitionedPopinOpenerProperties() const {
  NOTREACHED();
}

WebContents* RenderFrameHostDelegate::GetOpenedPartitionedPopin() const {
  return nullptr;
}

gfx::NativeWindow RenderFrameHostDelegate::GetOwnerNativeWindow() {
  return gfx::NativeWindow();
}

media::PictureInPictureEventsInfo::AutoPipInfo
RenderFrameHostDelegate::GetAutoPipInfo() const {
  return media::PictureInPictureEventsInfo::AutoPipInfo();
}

}  // namespace content
