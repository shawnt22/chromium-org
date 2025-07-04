// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/browser/ui/tabs/tab_strip_api/tab_strip_service_impl.h"

#include <algorithm>
#include <optional>

#include "base/strings/string_number_conversions.h"
#include "base/types/expected.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_api/adapters/browser_adapter_impl.h"
#include "chrome/browser/ui/tabs/tab_strip_api/adapters/tab_strip_model_adapter_impl.h"
#include "chrome/browser/ui/tabs/tab_strip_api/converters/tab_converters.h"
#include "chrome/browser/ui/tabs/tab_strip_api/event_broadcaster.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "mojo/public/mojom/base/error.mojom.h"
#include "url/gurl.h"

// Starts a mutation session that suppresses incoming messages to prevent
// re-entrancy and replays all recorded mutations on session destruction.
class MutationSession {
 public:
   explicit MutationSession(tabs_api::events::TabStripEventRecorder* recorder)
       : recorder_(recorder) {
     recorder_->StopNotificationAndStartRecording();
   }

   ~MutationSession() { recorder_->PlayRecordingsAndStartNotification(); }

   // Disallow copy and assign.
   MutationSession(const MutationSession&) = delete;
   MutationSession& operator=(const MutationSession&) = delete;

  private:
   raw_ptr<tabs_api::events::TabStripEventRecorder> recorder_;
 };

TabStripServiceImpl::TabStripServiceImpl(BrowserWindowInterface* browser,
                                         TabStripModel* tab_strip_model)
    : TabStripServiceImpl(
          std::make_unique<tabs_api::BrowserAdapterImpl>(browser),
          std::make_unique<tabs_api::TabStripModelAdapterImpl>(
              tab_strip_model)) {}

TabStripServiceImpl::TabStripServiceImpl(
    std::unique_ptr<tabs_api::BrowserAdapter> browser_adapter,
    std::unique_ptr<tabs_api::TabStripModelAdapter> tab_strip_model_adapter)
    : browser_adapter_(std::move(browser_adapter)),
      tab_strip_model_adapter_(std::move(tab_strip_model_adapter)) {
  recorder_ = std::make_unique<tabs_api::events::TabStripEventRecorder>(
      tab_strip_model_adapter_.get(),
      base::BindRepeating(&TabStripServiceImpl::BroadcastEvent,
                          base::Unretained(this)));
  tab_strip_model_adapter_->AddObserver(recorder_.get());
}

void TabStripServiceImpl::BroadcastEvent(
    const tabs_api::events::Event& event) const {
  tabs_api::EventBroadcaster broadcaster;
  broadcaster.Broadcast(observers_, event);
}

TabStripServiceImpl::~TabStripServiceImpl() {
  tab_strip_model_adapter_->RemoveObserver(recorder_.get());

  // Clear all observers
  // TODO (crbug.com/412955607): Implement a removal mechanism similar to
  // TabStripModelObserver where on shutdown of the TabStripService, it notifies
  // to all clients that service is shutting down.
  observers_.Clear();
}

void TabStripServiceImpl::GetTabs(GetTabsCallback callback) {
  tabs_api::mojom::TabCollectionContainerPtr topology =
      tab_strip_model_adapter_->GetTabStripTopology();
  auto snapshot = tabs_api::mojom::TabsSnapshot::New();
  snapshot->tab_strip = std::move(topology);

  // Now that we have a snapshot, create a event stream that will capture all
  // subsequent updates.
  mojo::AssociatedRemote<tabs_api::mojom::TabsObserver> stream;
  auto pending_receiver = stream.BindNewEndpointAndPassReceiver();
  observers_.Add(std::move(stream));
  snapshot->stream = std::move(pending_receiver);

  std::move(callback).Run(std::move(snapshot));
}

void TabStripServiceImpl::GetTab(const tabs_api::NodeId& tab_mojom_id,
                                 GetTabCallback callback) {
  if (tab_mojom_id.Type() != tabs_api::NodeId::Type::kContent) {
    std::move(callback).Run(base::unexpected(
        mojo_base::mojom::Error::New(mojo_base::mojom::Code::kInvalidArgument,
                                     "only tab content ids accepted")));
    return;
  }

  int32_t tab_id;
  if (!base::StringToInt(tab_mojom_id.Id(), &tab_id)) {
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kInvalidArgument, "invalid tab id provided")));
    return;
  }

  tabs_api::mojom::TabPtr tab_result;
  // TODO (crbug.com/412709270) TabStripModel or TabCollections should have an
  // api that can fetch id without of relying on indexes.
  auto tabs = tab_strip_model_adapter_->GetTabs();
  for (unsigned int i = 0; i < tabs.size(); ++i) {
    auto& handle = tabs.at(i);
    if (tab_id == handle.raw_value()) {
      auto renderer_data = tab_strip_model_adapter_->GetTabRendererData(i);
      tab_result = tabs_api::converters::BuildMojoTab(handle, renderer_data);
    }
  }

  if (tab_result) {
    std::move(callback).Run(std::move(tab_result));
  } else {
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kNotFound, "Tab not found")));
  }
}

void TabStripServiceImpl::CreateTabAt(tabs_api::mojom::PositionPtr pos,
                                      const std::optional<GURL>& url,
                                      CreateTabAtCallback callback) {
  MutationSession recorder_session(recorder_.get());

  GURL target_url;
  if (url.has_value()) {
    target_url = url.value();
  }
  std::optional<int> index;
  if (pos) {
    index = pos->index;
  }

  auto tab_handle = browser_adapter_->AddTabAt(target_url, index);
  if (tab_handle == tabs::TabHandle::Null()) {
    // Missing content can happen for a number of reasons. i.e. If the profile
    // is shutting down or if navigation requests are blocked due to some
    // internal state. This is usually because the browser is not in the
    // required state to perform the action.
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kInternal, "Failed to create WebContents")));
    return;
  }

  auto tab_index = tab_strip_model_adapter_->GetIndexForHandle(tab_handle);
  if (!tab_index.has_value()) {
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kInternal,
        "Could not find the index of the newly created tab")));
    return;
  }

  auto renderer_data =
      tab_strip_model_adapter_->GetTabRendererData(tab_index.value());
  auto mojo_tab = tabs_api::converters::BuildMojoTab(tab_handle, renderer_data);
  std::move(callback).Run(base::ok(std::move(mojo_tab)));
}

void TabStripServiceImpl::CloseTabs(const std::vector<tabs_api::NodeId>& ids,
                                    CloseTabsCallback callback) {
  MutationSession recorder_session(recorder_.get());

  std::vector<int32_t> tab_content_targets;
  for (const auto& id : ids) {
    if (id.Type() != tabs_api::NodeId::Type::kContent) {
      std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
          mojo_base::mojom::Code::kUnimplemented,
          "only content tab closing has been implemented right now")));
      return;
    }
    int32_t numeric_id;
    if (!base::StringToInt(id.Id(), &numeric_id)) {
      std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
          mojo_base::mojom::Code::kInvalidArgument, "invalid tab content id")));
      return;
    }
    tab_content_targets.push_back(numeric_id);
  }

  std::vector<size_t> tab_strip_indices;
  // Transform targets from ids to indices in the tabstrip.
  for (auto target : tab_content_targets) {
    auto target_idx =
        tab_strip_model_adapter_->GetIndexForHandle(tabs::TabHandle(target));
    if (!target_idx.has_value()) {
      std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
          mojo_base::mojom::Code::kNotFound, "could not find the a tab")));
      return;
    }
    tab_strip_indices.push_back(target_idx.value());
  }

  // Close from last to first, that way the removals won't change the index of
  // the next target.
  std::sort(tab_strip_indices.begin(), tab_strip_indices.end());
  std::reverse(tab_strip_indices.begin(), tab_strip_indices.end());
  for (auto idx : tab_strip_indices) {
    tab_strip_model_adapter_->CloseTab(idx);
  }

  std::move(callback).Run(mojo_base::mojom::Empty::New());
}

void TabStripServiceImpl::ActivateTab(const tabs_api::NodeId& id,
                                      ActivateTabCallback callback) {
  MutationSession recorder_session(recorder_.get());

  if (id.Type() != tabs_api::NodeId::Type::kContent) {
    std::move(callback).Run(base::unexpected(
        mojo_base::mojom::Error::New(mojo_base::mojom::Code::kInvalidArgument,
                                     "only a content tab id can be provided")));
    return;
  }

  int32_t handle_id;
  if (!base::StringToInt(id.Id(), &handle_id)) {
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kInvalidArgument, "id is malformed")));
    return;
  }

  auto maybe_idx =
      tab_strip_model_adapter_->GetIndexForHandle(tabs::TabHandle(handle_id));
  if (!maybe_idx.has_value()) {
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kNotFound, "tab not found")));
    return;
  }

  tab_strip_model_adapter_->ActivateTab(maybe_idx.value());
  std::move(callback).Run(mojo_base::mojom::Empty::New());
}

void TabStripServiceImpl::MoveTab(const tabs_api::NodeId& id,
                                  tabs_api::mojom::PositionPtr position,
                                  MoveTabCallback callback) {
  MutationSession recorder_session(recorder_.get());

  // TODO(crbug.com/409086859): this implementation is not complete, because
  // it will only move the tabs within the unpinned section. We need additional
  // API support for the tab strip model, which is currently in discussion.
  if (id.Type() != tabs_api::NodeId::Type::kContent) {
    std::move(callback).Run(base::unexpected(
        mojo_base::mojom::Error::New(mojo_base::mojom::Code::kUnimplemented,
                                     "only tab moves have been implemetned")));
    return;
  }

  int32_t handle_id;
  if (!base::StringToInt(id.Id(), &handle_id)) {
    std::move(callback).Run(base::unexpected(mojo_base::mojom::Error::New(
        mojo_base::mojom::Code::kInvalidArgument, "id is malformed")));
    return;
  }

  auto tab_handle = tabs::TabHandle(handle_id);
  if (position->index >= tab_strip_model_adapter_->GetTabs().size()) {
    std::move(callback).Run(base::unexpected(
        mojo_base::mojom::Error::New(mojo_base::mojom::Code::kInvalidArgument,
                                     "position cannot exceed tab strip")));
    return;
  }

  tab_strip_model_adapter_->MoveTab(tab_handle, {position->index});

  std::move(callback).Run(mojo_base::mojom::Empty::New());
}

void TabStripServiceImpl::Accept(
    mojo::PendingReceiver<tabs_api::mojom::TabStripService> client) {
  clients_.Add(this, std::move(client));
}
