// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_TAB_STRIP_API_CONVERTERS_TAB_CONVERTERS_H_
#define CHROME_BROWSER_UI_TABS_TAB_STRIP_API_CONVERTERS_TAB_CONVERTERS_H_

#include "chrome/browser/ui/tabs/tab_renderer_data.h"
#include "chrome/browser/ui/tabs/tab_strip_api/tab_strip_api.mojom.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_collection.h"
#include "components/tabs/public/tab_interface.h"

namespace tabs_api::converters {

tabs_api::mojom::TabPtr BuildMojoTab(tabs::TabHandle handle,
                                     const TabRendererData& data);

tabs_api::mojom::TabCollectionPtr BuildMojoTabCollection(
    tabs::TabCollectionHandle handle,
    tabs::TabCollection::Type collection_type);

tabs_api::mojom::TabGroupVisualDataPtr BuildMojoTabGroupVisualData(
    const tab_groups::TabGroupVisualData& visual_data);

}  // namespace tabs_api::converters

#endif  // CHROME_BROWSER_UI_TABS_TAB_STRIP_API_CONVERTERS_TAB_CONVERTERS_H_
