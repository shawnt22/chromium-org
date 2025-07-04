// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import androidx.annotation.NonNull;

import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tasks.tab_management.ArchivedTabsDialogCoordinator.ArchiveDelegate;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.util.motion.MotionEventInfo;

import java.util.List;

/** Restore all archived tabs action for the {@link TabListEditorMenu}. */
public class TabListEditorCloseArchivedTabsAction extends TabListEditorAction {
    private final @NonNull ArchivedTabsDialogCoordinator.ArchiveDelegate mArchiveDelegate;

    /**
     * Create an action for restoring archived tabs.
     *
     * @param archiveDelegate delegate which supports archive operations.
     */
    public static TabListEditorAction createAction(@NonNull ArchiveDelegate archiveDelegate) {
        return new TabListEditorCloseArchivedTabsAction(archiveDelegate);
    }

    private TabListEditorCloseArchivedTabsAction(@NonNull ArchiveDelegate archiveDelegate) {
        super(
                R.id.tab_list_editor_close_archived_tabs_menu_item,
                ShowMode.MENU_ONLY,
                ButtonType.TEXT,
                IconPosition.START,
                R.plurals.archived_tabs_dialog_close_action,
                R.plurals.accessibility_archived_tabs_dialog_close_action,
                null);

        mArchiveDelegate = archiveDelegate;
    }

    @Override
    public boolean shouldNotifyObserversOfAction() {
        return false;
    }

    @Override
    public void onSelectionStateChange(List<TabListEditorItemSelectionId> itemIds) {
        setEnabledAndItemCount(itemIds.size() > 0, itemIds.size());
    }

    @Override
    public boolean performAction(
            List<Tab> tabs,
            List<String> tabGroupSyncIds,
            @Nullable MotionEventInfo triggeringMotion) {
        mArchiveDelegate.closeArchivedTabs(tabs, tabGroupSyncIds);
        return true;
    }

    @Override
    public boolean shouldHideEditorAfterAction() {
        return false;
    }
}
