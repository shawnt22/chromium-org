// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/tab_strip_api/adapters/tab_strip_model_adapter_impl.h"

#include "chrome/browser/ui/tabs/tab_strip_api/adapters/tree_builder/mojo_tree_builder.h"
#include "components/tabs/public/tab_collection.h"
#include "components/tabs/public/tab_interface.h"

namespace tabs_api {

void TabStripModelAdapterImpl::AddObserver(TabStripModelObserver* observer) {
  tab_strip_model_->AddObserver(observer);
}

void TabStripModelAdapterImpl::RemoveObserver(TabStripModelObserver* observer) {
  tab_strip_model_->RemoveObserver(observer);
}

std::vector<tabs::TabHandle> TabStripModelAdapterImpl::GetTabs() const {
  std::vector<tabs::TabHandle> tabs;
  for (auto* tab : *tab_strip_model_) {
    tabs.push_back(tab->GetHandle());
  }
  return tabs;
}

TabRendererData TabStripModelAdapterImpl::GetTabRendererData(int index) const {
  return TabRendererData::FromTabInModel(tab_strip_model_, index);
}

void TabStripModelAdapterImpl::CloseTab(size_t tab_index) {
  tab_strip_model_->CloseWebContentsAt(tab_index, TabCloseTypes::CLOSE_NONE);
}

std::optional<int> TabStripModelAdapterImpl::GetIndexForHandle(
    tabs::TabHandle tab_handle) {
  auto idx = tab_strip_model_->GetIndexOfTab(tab_handle.Get());
  return idx != TabStripModel::kNoTab ? std::make_optional(idx) : std::nullopt;
}

void TabStripModelAdapterImpl::ActivateTab(size_t index) {
  tab_strip_model_->ActivateTabAt(index);
}

void TabStripModelAdapterImpl::MoveTab(tabs::TabHandle tab, Position position) {
  auto maybe_index = GetIndexForHandle(tab);
  CHECK(maybe_index.has_value());
  auto index = maybe_index.value();
  tab_strip_model_->MoveWebContentsAt(index, position.index,
                                      /*select_after_move=*/false);
}

tabs_api::mojom::TabCollectionContainerPtr
TabStripModelAdapterImpl::GetTabStripTopology() {
  return MojoTreeBuilder(tab_strip_model_).Build();
}

}  // namespace tabs_api
