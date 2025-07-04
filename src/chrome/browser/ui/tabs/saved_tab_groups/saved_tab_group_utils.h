// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TABS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_UTILS_H_
#define CHROME_BROWSER_UI_TABS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_UTILS_H_

#include <unordered_set>
#include <vector>

#include "base/containers/span.h"
#include "base/uuid.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_keyed_service.h"
#include "chrome/browser/ui/tabs/tab_group_deletion_dialog_controller.h"
#include "chrome/browser/ui/views/tabs/recent_activity_bubble_dialog_view.h"
#include "components/data_sharing/public/group_data.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/tabs/public/tab_group.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

class Browser;
class Profile;

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace collaboration::messaging {
struct ActivityLogItem;
}  // namespace collaboration::messaging

namespace tab_groups {

class SavedTabGroupTab;
class TabGroupSyncService;

enum class GroupDeletionReason {
  ClosedLastTab,
  UngroupedLastTab,
};

class SavedTabGroupUtils {
 public:
  SavedTabGroupUtils() = delete;
  SavedTabGroupUtils(const SavedTabGroupUtils&) = delete;
  SavedTabGroupUtils& operator=(const SavedTabGroupUtils&) = delete;

  // Helper method for checking whether the feature can be used.
  static bool IsEnabledForProfile(Profile* profile);

  // TODO(crbug.com/350514491): Default to using the TabGroupSyncService when
  // crbug.com/350514491 is complete.
  // When IsTabGroupSyncServiceDesktopMigrationEnabled() is true use the
  // TabGroupSyncService. Otherwise, use SavedTabGroupKeyedService::proxy. This
  // function will only return nullptr when the services cannot be created, or
  // the profile is non-regular (Ex: incognito or guest mode).
  static TabGroupSyncService* GetServiceForProfile(Profile* profile);

  static void RemoveGroupFromTabstrip(
      const Browser* browser,
      const tab_groups::TabGroupId& local_group);
  static void UngroupSavedGroup(Browser* browser,
                                const base::Uuid& saved_group_guid);
  static void DeleteSavedGroup(Browser* browser,
                               const base::Uuid& saved_group_guid);
  static void LeaveSharedGroup(const Browser* browser,
                               const base::Uuid& saved_group_guid);

  // Open the `url` to the end of `browser` tab strip as a new ungrouped tab.
  static void OpenUrlInNewUngroupedTab(Browser* browser, const GURL& url);

  static void OpenOrMoveSavedGroupToNewWindow(
      Browser* browser,
      const base::Uuid& saved_group_guid);

  // Pin the saved tab group if it's unpinned, or unpin the saved tab group if
  // it's pinned.
  static void ToggleGroupPinState(Browser* browser,
                                  const base::Uuid& saved_group_guid);

  // Helper method to show the deletion dialog, if its needed. It either
  // runs the callback if the dialog is not shown or it shows the dialog
  // and the callback is run asynchronously through the dialog.
  static void MaybeShowSavedTabGroupDeletionDialog(
      Browser* browser,
      GroupDeletionReason reason,
      base::span<const TabGroupId> group_ids,
      base::OnceCallback<void(DeletionDialogController::DeletionDialogTiming)>
          callback);

  // Converts a webcontents into a SavedTabGroupTab.
  static SavedTabGroupTab CreateSavedTabGroupTabFromWebContents(
      content::WebContents* contents,
      base::Uuid saved_tab_group_id);

  // Creates a SavedTabGroup group for the provided local tab group.
  static SavedTabGroup CreateSavedTabGroupFromLocalId(
      const tab_groups::LocalTabGroupID& local_id);

  static content::NavigationHandle* OpenTabInBrowser(
      const GURL& url,
      Browser* browser,
      Profile* profile,
      WindowOpenDisposition disposition,
      std::optional<int> tabstrip_index = std::nullopt,
      std::optional<tab_groups::TabGroupId> local_group_id = std::nullopt);

  // Returns whether a navigation was initiated from sync.
  static bool WasNavigationInitiatedFromSync(
      content::NavigationHandle* navigation_handle);

  // Returns the Browser that contains a local group with id `group_id`.
  static Browser* GetBrowserWithTabGroupId(tab_groups::TabGroupId group_id);

  // Finds the TabGroup with id `group_id` across all Browsers.
  static TabGroup* GetTabGroupWithId(tab_groups::TabGroupId group_id);

  // Returns the list of Tabs in the local group `group_id` in order.
  static std::vector<tabs::TabInterface*> GetTabsInGroup(
      tab_groups::TabGroupId group_id);

  // TODO(crbug.com/350514491) remove this once all cases are handled by
  // GetTabsInGroup. Prefer GetTabsInGroup over this method.
  // Returns the list of WebContentses in the local group `group_id` in order.
  static std::vector<content::WebContents*> GetWebContentsesInGroup(
      tab_groups::TabGroupId group_id);

  // Returns the set of urls currently stored in the saved tab group.
  static std::unordered_set<std::string> GetURLsInSavedTabGroup(
      Profile* profile,
      const base::Uuid& saved_id);

  // Activates the first tab in the saved group. If a tab in the group is
  // already activated, then we focus the window the group belongs to instead.
  static void FocusFirstTabOrWindowInOpenGroup(
      tab_groups::TabGroupId local_group_id);

  // Returns the correct element for showing the IPH for Saved Groups V2. Either
  // the SavedTabGroupBar::EverythingMenuButton or the AppMenuButton.
  static ui::TrackedElement* GetAnchorElementForTabGroupsV2IPH(
      const ui::ElementTracker::ElementList& elements);

  // Returns true if new tab groups should be pinned.
  static bool ShouldAutoPinNewTabGroups(Profile* profile);

  // Returns true if the sync setting is on for saved tab groups.
  static bool AreSavedTabGroupsSyncedForProfile(Profile* profile);

  // Returns true if shared tab groups are supported.
  static bool SupportsSharedTabGroups();

  // Returns true if the user is the owner of the shared tab group.
  static bool IsOwnerOfSharedTabGroup(Profile* profile,
                                      const base::Uuid& sync_id);

  // Returns a list of the members of the group if the group data exists in the
  // collaboration service in that profile. returns empty in any case where data
  // is missing or not accessible.
  static std::vector<data_sharing::GroupMember> GetMembersOfSharedTabGroup(
      Profile* profile,
      const tab_groups::CollaborationId& collaboration_id);

  // Returns the GroupId for this tab group's collaboration.
  static std::optional<data_sharing::GroupId> GetDataSharingGroupId(
      Profile* profile,
      LocalTabGroupID group_id);

  // Returns the Recent Activity Log for this tab group. Optionally, filter for
  // the given tab.
  static std::vector<collaboration::messaging::ActivityLogItem>
  GetRecentActivity(Profile* profile,
                    LocalTabGroupID group_id,
                    std::optional<LocalTabID> tab_id = std::nullopt);

  // Returns the tab with this id if contained in this group. The group
  // must exist.
  static tabs::TabInterface* GetGroupedTab(LocalTabGroupID group_id,
                                           LocalTabID tab_id);
};

}  // namespace tab_groups

#endif  // CHROME_BROWSER_UI_TABS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_UTILS_H_
