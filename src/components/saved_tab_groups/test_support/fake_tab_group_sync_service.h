// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAVED_TAB_GROUPS_TEST_SUPPORT_FAKE_TAB_GROUP_SYNC_SERVICE_H_
#define COMPONENTS_SAVED_TAB_GROUPS_TEST_SUPPORT_FAKE_TAB_GROUP_SYNC_SERVICE_H_

#include <vector>

#include "base/observer_list.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/sync/base/collaboration_id.h"

namespace tab_groups {

// Fake implementation for the TabGroupSyncService.
class FakeTabGroupSyncService : public TabGroupSyncService {
 public:
  FakeTabGroupSyncService();
  ~FakeTabGroupSyncService() override;

  // TabGroupSyncService override.
  void SetTabGroupSyncDelegate(
      std::unique_ptr<TabGroupSyncDelegate> delegate) override;
  void AddGroup(SavedTabGroup group) override;
  void RemoveGroup(const LocalTabGroupID& local_id) override;
  void RemoveGroup(const base::Uuid& sync_id) override;
  void UpdateVisualData(
      const LocalTabGroupID local_group_id,
      const tab_groups::TabGroupVisualData* visual_data) override;
  void UpdateGroupPosition(const base::Uuid& sync_id,
                           std::optional<bool> is_pinned,
                           std::optional<int> new_index) override;
  void AddTab(const LocalTabGroupID& group_id,
              const LocalTabID& tab_id,
              const std::u16string& title,
              const GURL& url,
              std::optional<size_t> position) override;
  void NavigateTab(const LocalTabGroupID& group_id,
                   const LocalTabID& tab_id,
                   const GURL& url,
                   const std::u16string& title) override;
  void UpdateTabProperties(const LocalTabGroupID& group_id,
                           const LocalTabID& tab_id,
                           const SavedTabGroupTabBuilder& tab_builder) override;
  void RemoveTab(const LocalTabGroupID& group_id,
                 const LocalTabID& tab_id) override;
  void MoveTab(const LocalTabGroupID& group_id,
               const LocalTabID& tab_id,
               int new_group_index) override;
  void OnTabSelected(const std::optional<LocalTabGroupID>& group_id,
                     const LocalTabID& tab_id,
                     const std::u16string& tab_title) override;
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
  void SaveGroup(SavedTabGroup group) override;
  void UnsaveGroup(const LocalTabGroupID& local_id) override;
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
  void MakeTabGroupShared(const LocalTabGroupID& local_group_id,
                          const syncer::CollaborationId& collaboration_id,
                          TabGroupSharingCallback callback) override;
  void MakeTabGroupSharedForTesting(
      const LocalTabGroupID& local_group_id,
      const syncer::CollaborationId& collaboration_id) override;
  void AboutToUnShareTabGroup(const LocalTabGroupID& local_group_id,
                              base::OnceClosure on_complete_callback) override;
  void OnTabGroupUnShareComplete(const LocalTabGroupID& local_group_id,
                                 bool success) override;
  void OnCollaborationRemoved(
      const syncer::CollaborationId& collaboration_id) override;
  std::vector<const SavedTabGroup*> ReadAllGroups() const override;
  std::vector<SavedTabGroup> GetAllGroups() const override;
  std::optional<SavedTabGroup> GetGroup(const base::Uuid& guid) const override;
  std::optional<SavedTabGroup> GetGroup(
      const LocalTabGroupID& local_id) const override;
  std::optional<SavedTabGroup> GetGroup(
      const EitherGroupID& either_id) const override;
  std::vector<LocalTabGroupID> GetDeletedGroupIds() const override;
  std::optional<std::u16string> GetTitleForPreviouslyExistingSharedTabGroup(
      const CollaborationId& collaboration_id) const override;
  std::optional<LocalTabGroupID> OpenTabGroup(
      const base::Uuid& sync_group_id,
      std::unique_ptr<TabGroupActionContext> context) override;
  void UpdateLocalTabGroupMapping(const base::Uuid& sync_id,
                                  const LocalTabGroupID& local_id,
                                  OpeningSource opening_source) override;
  void RemoveLocalTabGroupMapping(const LocalTabGroupID& local_id,
                                  ClosingSource closing_source) override;
  void UpdateLocalTabId(const LocalTabGroupID& local_group_id,
                        const base::Uuid& sync_tab_id,
                        const LocalTabID& local_tab_id) override;
  void ConnectLocalTabGroup(const base::Uuid& sync_id,
                            const LocalTabGroupID& local_id,
                            OpeningSource opening_source) override;
  bool IsRemoteDevice(
      const std::optional<std::string>& cache_guid) const override;
  bool WasTabGroupClosedLocally(const base::Uuid& sync_id) const override;
  void RecordTabGroupEvent(const EventDetails& event_details) override;
  void UpdateArchivalStatus(const base::Uuid& sync_id,
                            bool archival_status) override;
  void UpdateTabLastSeenTime(const base::Uuid& group_id,
                             const base::Uuid& tab_id,
                             TriggerSource source) override;
  TabGroupSyncMetricsLogger* GetTabGroupSyncMetricsLogger() override;
  base::WeakPtr<syncer::DataTypeControllerDelegate>
  GetSavedTabGroupControllerDelegate() override;
  base::WeakPtr<syncer::DataTypeControllerDelegate>
  GetSharedTabGroupControllerDelegate() override;
  base::WeakPtr<syncer::DataTypeControllerDelegate>
  GetSharedTabGroupAccountControllerDelegate() override;
  std::unique_ptr<ScopedLocalObservationPauser>
  CreateScopedLocalObserverPauser() override;
  void GetURLRestriction(
      const GURL& url,
      TabGroupSyncService::UrlRestrictionCallback callback) override;
  std::unique_ptr<std::vector<SavedTabGroup>>
  TakeSharedTabGroupsAvailableAtStartupForMessaging() override;
  bool HadSharedTabGroupsLastSession(bool open_shared_tab_groups) override;
  VersioningMessageController* GetVersioningMessageController() override;
  void OnLastTabClosed(const SavedTabGroup& saved_tab_group) override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  // Helpers.
  std::optional<int> GetIndexOf(const base::Uuid& guid) const;
  std::optional<int> GetIndexOf(const LocalTabGroupID& local_id) const;

  // Notifies observers when `group` is updated.
  void NotifyObserversOfTabGroupUpdated(SavedTabGroup& group);
  // Notifies observers when `group` is shared.
  void NotifyObserversOfTabGroupShared(SavedTabGroup& group);

  std::unique_ptr<TabGroupSyncDelegate> delegate_;

  base::ObserverList<TabGroupSyncService::Observer> observers_;
  std::vector<SavedTabGroup> groups_;
};

}  // namespace tab_groups

#endif  // COMPONENTS_SAVED_TAB_GROUPS_TEST_SUPPORT_FAKE_TAB_GROUP_SYNC_SERVICE_H_
