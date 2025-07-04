// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/platform/ax_platform_tree_manager.h"

namespace ui {

AXPlatformTreeManager::AXPlatformTreeManager(std::unique_ptr<AXTree> tree)
    : AXTreeManager(std::move(tree), /*is_platform_tree_manager=*/true) {}

AXPlatformTreeManager::~AXPlatformTreeManager() = default;

void AXPlatformTreeManager::FireSentinelEventForTesting() {}

}  // namespace ui
