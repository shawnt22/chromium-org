// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/features.h"

#include "base/feature_list.h"
#include "chrome/browser/buildflags.h"
#include "chrome/browser/ui/ui_features.h"

namespace tabs {

// Enables the debug UI used to visualize the tab strip model.
// chrome://tab-strip-internals
BASE_FEATURE(kDebugUITabStrip,
             "DebugUITabStrip",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Splits pinned and unpinned tabs into separate TabStrips.
// https://crbug.com/1346019
BASE_FEATURE(kSplitTabStrip,
             "SplitTabStrip",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables tabs to scroll in the tabstrip. https://crbug.com/951078
BASE_FEATURE(kScrollableTabStrip,
             "ScrollableTabStrip",
             base::FEATURE_DISABLED_BY_DEFAULT);
const char kMinimumTabWidthFeatureParameterName[] = "minTabWidth";

// Enables tab scrolling while dragging tabs in tabstrip
// https://crbug.com/1145747
BASE_FEATURE(kScrollableTabStripWithDragging,
             "kScrollableTabStripWithDragging",
             base::FEATURE_DISABLED_BY_DEFAULT);
const char kTabScrollingWithDraggingModeName[] = "tabScrollWithDragMode";

// Enables different methods of overflow when scrolling tabs in tabstrip
// https://crbug.com/951078
BASE_FEATURE(kScrollableTabStripOverflow,
             "kScrollableTabStripOverflow",
             base::FEATURE_DISABLED_BY_DEFAULT);
const char kScrollableTabStripOverflowModeName[] = "tabScrollOverflow";

BASE_FEATURE(kTabGroupHome, "TabGroupHome", base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabSearchPositionSetting,
             "TabSearchPositionSetting",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabGroupShortcuts,
             "TabGroupShortcuts",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool CanShowTabSearchPositionSetting() {
  // Alternate tab search locations cannot be repositioned.
  if (features::IsTabSearchMoving()) {
    return false;
  }
// Mac and other platforms will always have the tab search position in the
// correct location, cros/linux/win git the user the option to change.
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
  return base::FeatureList::IsEnabled(kTabSearchPositionSetting);
#else
  return false;
#endif
}

bool AreTabGroupShortcutsEnabled() {
  return base::FeatureList::IsEnabled(kTabGroupShortcuts);
}

}  // namespace tabs
