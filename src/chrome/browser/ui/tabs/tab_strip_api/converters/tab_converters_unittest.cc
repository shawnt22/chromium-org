// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/tab_strip_api/converters/tab_converters.h"

#include "chrome/browser/ui/tabs/tab_renderer_data.h"
#include "chrome/browser/ui/tabs/tab_strip_api/node_id.h"
#include "components/tabs/public/tab_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace tabs_api::converters {
namespace {

TEST(TabStripServiceConverters, ConvertTab) {
  tabs::TabHandle handle(888);
  TabRendererData data;
  data.visible_url = GURL("http://nowhere");
  data.title = std::u16string(u"title");

  auto mojo = BuildMojoTab(handle, data);

  ASSERT_EQ("888", mojo->id.Id());
  ASSERT_EQ(NodeId::Type::kContent, mojo->id.Type());
  ASSERT_EQ(GURL("http://nowhere"), mojo->url);
  ASSERT_EQ("title", mojo->title);
}

TEST(TabStripServiceConverters, ConvertTabCollection) {
  tabs::TabCollectionHandle handle(888);
  tabs::TabCollection::Type collection_type =
      tabs::TabCollection::Type::TABSTRIP;

  auto mojo = BuildMojoTabCollection(handle, collection_type);
  ASSERT_EQ("888", mojo->id.Id());
  ASSERT_EQ(NodeId::Type::kCollection, mojo->id.Type());
  ASSERT_EQ(tabs_api::mojom::TabCollection::CollectionType::kTabStrip,
            mojo->collection_type);
}

}  // namespace
}  // namespace tabs_api::converters
