// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_DESKTOP_ANDROID_DESKTOP_ANDROID_EXTENSION_HOST_DELEGATE_H_
#define CHROME_BROWSER_EXTENSIONS_DESKTOP_ANDROID_DESKTOP_ANDROID_EXTENSION_HOST_DELEGATE_H_

#include "extensions/browser/extension_host_delegate.h"

namespace extensions {

// An ExtensionHostDelegate for the desktop Android platform.
class DesktopAndroidExtensionHostDelegate : public ExtensionHostDelegate {
 public:
  DesktopAndroidExtensionHostDelegate();
  DesktopAndroidExtensionHostDelegate(
      const DesktopAndroidExtensionHostDelegate&) = delete;
  DesktopAndroidExtensionHostDelegate& operator=(
      const DesktopAndroidExtensionHostDelegate&) = delete;
  ~DesktopAndroidExtensionHostDelegate() override;

  // ExtensionHostDelegate:
  void OnExtensionHostCreated(content::WebContents* web_contents) override;
  void CreateTab(std::unique_ptr<content::WebContents> web_contents,
                 const ExtensionId& extension_id,
                 WindowOpenDisposition disposition,
                 const blink::mojom::WindowFeatures& window_features,
                 bool user_gesture) override;
  void ProcessMediaAccessRequest(content::WebContents* web_contents,
                                 const content::MediaStreamRequest& request,
                                 content::MediaResponseCallback callback,
                                 const Extension* extension) override;
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type,
                                  const Extension* extension) override;
  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* web_contents) override;
  void ExitPictureInPicture() override;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_DESKTOP_ANDROID_DESKTOP_ANDROID_EXTENSION_HOST_DELEGATE_H_
