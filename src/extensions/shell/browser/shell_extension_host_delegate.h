// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_SHELL_BROWSER_SHELL_EXTENSION_HOST_DELEGATE_H_
#define EXTENSIONS_SHELL_BROWSER_SHELL_EXTENSION_HOST_DELEGATE_H_

#include "extensions/browser/extension_host_delegate.h"
#include "extensions/common/extension_id.h"

namespace extensions {

// A minimal ExtensionHostDelegate.
class ShellExtensionHostDelegate : public ExtensionHostDelegate {
 public:
  ShellExtensionHostDelegate();

  ShellExtensionHostDelegate(const ShellExtensionHostDelegate&) = delete;
  ShellExtensionHostDelegate& operator=(const ShellExtensionHostDelegate&) =
      delete;

  ~ShellExtensionHostDelegate() override;

  // ExtensionHostDelegate implementation.
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

#endif  // EXTENSIONS_SHELL_BROWSER_SHELL_EXTENSION_HOST_DELEGATE_H_
