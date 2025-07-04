// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/shell/browser/shell_app_window_client.h"

#include <vector>

#include "base/notimplemented.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/shell/browser/shell_app_delegate.h"

namespace extensions {

ShellAppWindowClient::ShellAppWindowClient() = default;

ShellAppWindowClient::~ShellAppWindowClient() = default;

AppWindow* ShellAppWindowClient::CreateAppWindow(
    content::BrowserContext* context,
    const Extension* extension) {
  return new AppWindow(context, std::make_unique<ShellAppDelegate>(),
                       extension);
}

void ShellAppWindowClient::OpenDevToolsWindow(
    content::WebContents* web_contents,
    base::OnceClosure callback) {
  NOTIMPLEMENTED();
}

bool ShellAppWindowClient::IsCurrentChannelOlderThanDev() {
  return false;
}

}  // namespace extensions
