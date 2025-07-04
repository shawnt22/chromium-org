// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/saved_tab_groups/test_support/fake_tab_group_sync_service.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "components/saved_tab_groups/delegate/tab_group_sync_delegate.h"
#include "components/saved_tab_groups/public/features.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/saved_tab_group_tab.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/saved_tab_groups/public/versioning_message_controller.h"
#include "components/sync/base/collaboration_id.h"

namespace tab_groups {

FakeTabGroupSyncService::FakeTabGroupSyncService() = default;

FakeTabGroupSyncService::~FakeTabGroupSyncService() = default;

void FakeTabGroupSyncService::SetTabGroupSyncDelegate(
    std::unique_ptr<TabGroupSyncDelegate> delegate) {
  delegate_ = std::move(delegate);
}

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
void FakeTabGroupSyncService::SaveGroup(SavedTabGroup group) {
  const base::Uuid sync_id = group.saved_guid();
  const LocalTabGroupID local_id = group.local_group_id().value();
  AddGroup(std::move(group));
  ConnectLocalTabGroup(sync_id, local_id, OpeningSource::kOpenedFromRevisitUi);
}

void FakeTabGroupSyncService::UnsaveGroup(const LocalTabGroupID& local_id) {
  RemoveGroup(local_id);
}
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

void FakeTabGroupSyncService::AddGroup(SavedTabGroup group) {
  groups_.push_back(group);

  for (auto& observer : observers_) {
    observer.OnTabGroupAdded(group, TriggerSource::LOCAL);
  }
}

void FakeTabGroupSyncService::RemoveGroup(const LocalTabGroupID& local_id) {
  std::optional<int> index = GetIndexOf(local_id);
  if (!index.has_value()) {
    return;
  }
  const base::Uuid& sync_id = groups_[index.value()].saved_guid();
  groups_.erase(groups_.begin() + index.value());

  // Call 2 types of observer methods with the saved group id and the local
  // id.
  for (auto& observer : observers_) {
    observer.OnTabGroupRemoved(sync_id, TriggerSource::LOCAL);
  }

  for (auto& observer : observers_) {
    observer.OnTabGroupRemoved(local_id, TriggerSource::LOCAL);
  }
}

void FakeTabGroupSyncService::RemoveGroup(const base::Uuid& sync_id) {
  int erased_group_count =
      std::erase_if(groups_, [sync_id](SavedTabGroup& group) {
        return group.saved_guid() == sync_id;
      });

  if (erased_group_count > 0) {
    for (auto& observer : observers_) {
      observer.OnTabGroupRemoved(sync_id, TriggerSource::LOCAL);
    }
  }
}

void FakeTabGroupSyncService::UpdateVisualData(
    const LocalTabGroupID local_group_id,
    const tab_groups::TabGroupVisualData* visual_data) {
  std::optional<int> index = GetIndexOf(local_group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  group.SetColor(visual_data->color());
  group.SetTitle(visual_data->title());

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::UpdateGroupPosition(
    const base::Uuid& sync_id,
    std::optional<bool> is_pinned,
    std::optional<int> new_index) {
  std::optional<int> index = GetIndexOf(sync_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  if (is_pinned) {
    group.SetPinned(is_pinned.value());
  }
  if (new_index) {
    group.SetPosition(new_index.value());
  }

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::AddTab(const LocalTabGroupID& group_id,
                                     const LocalTabID& tab_id,
                                     const std::u16string& title,
                                     const GURL& url,
                                     std::optional<size_t> position) {
  std::optional<int> index = GetIndexOf(group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  SavedTabGroupTab tab(url, title, group.saved_guid(), position);
  group.AddTabLocally(tab);

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::NavigateTab(const LocalTabGroupID& group_id,
                                          const LocalTabID& tab_id,
                                          const GURL& url,
                                          const std::u16string& title) {
  std::optional<int> index = GetIndexOf(group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  for (const auto& tab : group.saved_tabs()) {
    if (tab.local_tab_id() == tab_id) {
      SavedTabGroupTab updated_tab(tab);
      updated_tab.SetURL(url);
      updated_tab.SetTitle(title);
      group.UpdateTab(updated_tab);
      return;
    }
  }

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::UpdateTabProperties(
    const LocalTabGroupID& group_id,
    const LocalTabID& tab_id,
    const SavedTabGroupTabBuilder& tab_builder) {
  std::optional<int> index = GetIndexOf(group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  for (const auto& tab : group.saved_tabs()) {
    if (tab.local_tab_id() == tab_id) {
      SavedTabGroupTab updated_tab = tab_builder.Build(tab);
      group.UpdateTab(updated_tab);
      return;
    }
  }

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::RemoveTab(const LocalTabGroupID& group_id,
                                        const LocalTabID& tab_id) {
  std::optional<int> index = GetIndexOf(group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  auto tabs = group.saved_tabs();
  std::erase_if(tabs, [tab_id](const SavedTabGroupTab& tab) {
    return tab.local_tab_id() == tab_id;
  });

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::MoveTab(const LocalTabGroupID& group_id,
                                      const LocalTabID& tab_id,
                                      int new_group_index) {
  std::optional<int> index = GetIndexOf(group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  for (const auto& tab : group.saved_tabs()) {
    if (tab.local_tab_id() == tab_id) {
      group.MoveTabLocally(tab.saved_tab_guid(), new_group_index);
      return;
    }
  }

  NotifyObserversOfTabGroupUpdated(group);
}

void FakeTabGroupSyncService::OnTabSelected(
    const std::optional<LocalTabGroupID>& group_id,
    const LocalTabID& tab_id,
    const std::u16string& tab_title) {
  // No op.
}

void FakeTabGroupSyncService::MakeTabGroupShared(
    const LocalTabGroupID& local_group_id,
    const syncer::CollaborationId& collaboration_id,
    TabGroupSharingCallback callback) {
  std::optional<int> index = GetIndexOf(local_group_id);
  CHECK(index.has_value());
  SavedTabGroup& group = groups_[index.value()];
  group.SetCollaborationId(collaboration_id);
  NotifyObserversOfTabGroupShared(group);
  if (callback) {
    std::move(callback).Run(TabGroupSharingResult::kSuccess);
  }
}

void FakeTabGroupSyncService::MakeTabGroupSharedForTesting(
    const LocalTabGroupID& local_group_id,
    const syncer::CollaborationId& collaboration_id) {
  // No op.
}

void FakeTabGroupSyncService::AboutToUnShareTabGroup(
    const LocalTabGroupID& local_group_id,
    base::OnceClosure on_complete_callback) {
  std::optional<int> index = GetIndexOf(local_group_id);
  CHECK(index.has_value());
  SavedTabGroup& group = groups_[index.value()];
  group.SetIsTransitioningToSaved(true);
  NotifyObserversOfTabGroupUpdated(group);
  std::move(on_complete_callback).Run();
}

void FakeTabGroupSyncService::OnTabGroupUnShareComplete(
    const LocalTabGroupID& local_group_id,
    bool success) {
  std::optional<int> index = GetIndexOf(local_group_id);
  CHECK(index.has_value());
  SavedTabGroup& group = groups_[index.value()];
  if (!success) {
    group.SetIsTransitioningToSaved(false);
  } else {
    group.SetCollaborationId(std::nullopt);
  }
  NotifyObserversOfTabGroupShared(group);
}

void FakeTabGroupSyncService::OnCollaborationRemoved(
    const syncer::CollaborationId& collaboration_id) {
  // No op.
}

std::vector<const SavedTabGroup*> FakeTabGroupSyncService::ReadAllGroups()
    const {
  std::vector<const SavedTabGroup*> groups;
  for (const SavedTabGroup& group : groups_) {
    groups.push_back(&group);
  }
  return groups;
}

std::vector<SavedTabGroup> FakeTabGroupSyncService::GetAllGroups() const {
  std::vector<SavedTabGroup> groups;
  for (const SavedTabGroup& group : groups_) {
    groups.push_back(group);
  }
  return groups;
}

std::optional<SavedTabGroup> FakeTabGroupSyncService::GetGroup(
    const base::Uuid& guid) const {
  std::optional<int> index = GetIndexOf(guid);
  if (!index.has_value()) {
    return std::nullopt;
  }
  return std::make_optional(groups_[index.value()]);
}

std::optional<SavedTabGroup> FakeTabGroupSyncService::GetGroup(
    const LocalTabGroupID& local_id) const {
  std::optional<int> index = GetIndexOf(local_id);
  if (!index.has_value()) {
    return std::nullopt;
  }
  return std::make_optional(groups_[index.value()]);
}

std::optional<SavedTabGroup> FakeTabGroupSyncService::GetGroup(
    const EitherGroupID& either_id) const {
  std::optional<int> index;

  if (std::holds_alternative<LocalTabGroupID>(either_id)) {
    index = GetIndexOf(std::get<LocalTabGroupID>(either_id));
  } else {
    index = GetIndexOf(std::get<base::Uuid>(either_id));
  }

  if (!index.has_value()) {
    return std::nullopt;
  }
  return std::make_optional(groups_[index.value()]);
}

std::vector<LocalTabGroupID> FakeTabGroupSyncService::GetDeletedGroupIds()
    const {
  std::vector<LocalTabGroupID> deleted_group_ids;
  return deleted_group_ids;
}

std::optional<std::u16string>
FakeTabGroupSyncService::GetTitleForPreviouslyExistingSharedTabGroup(
    const CollaborationId& collaboration_id) const {
  return std::nullopt;
}

std::optional<LocalTabGroupID> FakeTabGroupSyncService::OpenTabGroup(
    const base::Uuid& sync_group_id,
    std::unique_ptr<TabGroupActionContext> context) {
  // No op.
  return std::nullopt;
}

void FakeTabGroupSyncService::UpdateLocalTabGroupMapping(
    const base::Uuid& sync_id,
    const LocalTabGroupID& local_id,
    OpeningSource opening_source) {
  std::optional<int> index = GetIndexOf(local_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  group.SetLocalGroupId(local_id);
}

void FakeTabGroupSyncService::RemoveLocalTabGroupMapping(
    const LocalTabGroupID& local_id,
    ClosingSource closing_source) {
  std::optional<int> index = GetIndexOf(local_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  group.SetLocalGroupId(std::nullopt);
  for (auto& tab : group.saved_tabs()) {
    tab.SetLocalTabID(std::nullopt);
  }
}

void FakeTabGroupSyncService::UpdateLocalTabId(
    const LocalTabGroupID& local_group_id,
    const base::Uuid& sync_tab_id,
    const LocalTabID& local_tab_id) {
  std::optional<int> index = GetIndexOf(local_group_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  auto tabs = group.saved_tabs();
  for (auto& tab : tabs) {
    if (tab.saved_tab_guid() == sync_tab_id) {
      tab.SetLocalTabID(local_tab_id);
    }
  }
}

void FakeTabGroupSyncService::ConnectLocalTabGroup(
    const base::Uuid& sync_id,
    const LocalTabGroupID& local_id,
    OpeningSource opening_source) {
  std::optional<int> index = GetIndexOf(sync_id);
  if (!index.has_value()) {
    return;
  }
  SavedTabGroup& group = groups_[index.value()];
  group.SetLocalGroupId(local_id);
}

bool FakeTabGroupSyncService::IsRemoteDevice(
    const std::optional<std::string>& cache_guid) const {
  return false;
}

bool FakeTabGroupSyncService::WasTabGroupClosedLocally(
    const base::Uuid& sync_id) const {
  return false;
}

void FakeTabGroupSyncService::RecordTabGroupEvent(
    const EventDetails& event_details) {
  // No op.
}

void FakeTabGroupSyncService::UpdateArchivalStatus(const base::Uuid& sync_id,
                                                   bool archival_status) {
  // No op.
}

void FakeTabGroupSyncService::UpdateTabLastSeenTime(const base::Uuid& group_id,
                                                    const base::Uuid& tab_id,
                                                    TriggerSource source) {
  // No op.
}

TabGroupSyncMetricsLogger*
FakeTabGroupSyncService::GetTabGroupSyncMetricsLogger() {
  return nullptr;
}

base::WeakPtr<syncer::DataTypeControllerDelegate>
FakeTabGroupSyncService::GetSavedTabGroupControllerDelegate() {
  return base::WeakPtr<syncer::DataTypeControllerDelegate>();
}

base::WeakPtr<syncer::DataTypeControllerDelegate>
FakeTabGroupSyncService::GetSharedTabGroupControllerDelegate() {
  return base::WeakPtr<syncer::DataTypeControllerDelegate>();
}

base::WeakPtr<syncer::DataTypeControllerDelegate>
FakeTabGroupSyncService::GetSharedTabGroupAccountControllerDelegate() {
  return base::WeakPtr<syncer::DataTypeControllerDelegate>();
}

std::unique_ptr<ScopedLocalObservationPauser>
FakeTabGroupSyncService::CreateScopedLocalObserverPauser() {
  return std::make_unique<ScopedLocalObservationPauser>();
}

void FakeTabGroupSyncService::GetURLRestriction(
    const GURL& url,
    TabGroupSyncService::UrlRestrictionCallback callback) {
  std::move(callback).Run(std::nullopt);
}

std::unique_ptr<std::vector<SavedTabGroup>>
FakeTabGroupSyncService::TakeSharedTabGroupsAvailableAtStartupForMessaging() {
  return std::make_unique<std::vector<SavedTabGroup>>();
}

bool FakeTabGroupSyncService::HadSharedTabGroupsLastSession(
    bool open_shared_tab_groups) {
  return false;
}

VersioningMessageController*
FakeTabGroupSyncService::GetVersioningMessageController() {
  return nullptr;
}

void FakeTabGroupSyncService::OnLastTabClosed(
    const SavedTabGroup& saved_tab_group) {}

void FakeTabGroupSyncService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);

  // Notify the observer here since there is no data loaded remotely in this
  // fake TabGroupSyncService.
  observer->OnInitialized();
}

void FakeTabGroupSyncService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::optional<int> FakeTabGroupSyncService::GetIndexOf(
    const base::Uuid& guid) const {
  for (size_t i = 0; i < groups_.size(); i++) {
    if (groups_[i].saved_guid() == guid) {
      return i;
    }
  }

  return std::nullopt;
}

std::optional<int> FakeTabGroupSyncService::GetIndexOf(
    const LocalTabGroupID& local_id) const {
  for (size_t i = 0; i < groups_.size(); i++) {
    if (groups_[i].local_group_id() == local_id) {
      return i;
    }
  }

  return std::nullopt;
}

void FakeTabGroupSyncService::NotifyObserversOfTabGroupUpdated(
    SavedTabGroup& group) {
  for (auto& observer : observers_) {
    observer.OnTabGroupUpdated(group, TriggerSource::LOCAL);
  }
}

void FakeTabGroupSyncService::NotifyObserversOfTabGroupShared(
    SavedTabGroup& group) {
  for (auto& observer : observers_) {
    observer.OnTabGroupMigrated(group, group.saved_guid(),
                                tab_groups::TriggerSource::LOCAL);
  }
}

}  // namespace tab_groups
