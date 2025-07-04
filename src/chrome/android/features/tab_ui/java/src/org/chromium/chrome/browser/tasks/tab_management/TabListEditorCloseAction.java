// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.content.Context;
import android.graphics.drawable.Drawable;

import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tasks.tab_management.TabUiMetricsHelper.TabListEditorActionMetricGroups;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.util.motion.MotionEventInfo;

import java.util.List;

/** Close action for the {@link TabListEditorMenu}. */
@NullMarked
public class TabListEditorCloseAction extends TabListEditorAction {
    /**
     * Create an action for closing tabs.
     *
     * @param context for loading resources.
     * @param showMode whether to show an action view.
     * @param buttonType the type of the action view.
     * @param iconPosition the position of the icon in the action view.
     */
    public static TabListEditorAction createAction(
            Context context,
            @ShowMode int showMode,
            @ButtonType int buttonType,
            @IconPosition int iconPosition) {
        Drawable drawable = AppCompatResources.getDrawable(context, R.drawable.ic_close_tabs_24dp);
        return new TabListEditorCloseAction(showMode, buttonType, iconPosition, drawable);
    }

    private TabListEditorCloseAction(
            @ShowMode int showMode,
            @ButtonType int buttonType,
            @IconPosition int iconPosition,
            Drawable drawable) {
        super(
                R.id.tab_list_editor_close_menu_item,
                showMode,
                buttonType,
                iconPosition,
                R.plurals.tab_selection_editor_close_tabs,
                R.plurals.accessibility_tab_selection_editor_close_tabs,
                drawable);
    }

    @Override
    public void onSelectionStateChange(List<TabListEditorItemSelectionId> itemIds) {
        int size =
                editorSupportsActionOnRelatedTabs()
                        ? getTabCountIncludingRelatedTabs(getTabGroupModelFilter(), itemIds)
                        : itemIds.size();
        setEnabledAndItemCount(!itemIds.isEmpty(), size);
    }

    @Override
    public boolean performAction(
            List<Tab> tabs,
            List<String> tabGroupSyncIds,
            @Nullable MotionEventInfo triggeringMotion) {
        assert !tabs.isEmpty() : "Close action should not be enabled for no tabs.";

        getTabGroupModelFilter()
                .getTabModel()
                .getTabRemover()
                .closeTabs(
                        TabClosureParams.closeTabs(tabs)
                                .allowUndo(true)
                                .hideTabGroups(editorSupportsActionOnRelatedTabs())
                                .build(),
                        /* allowDialog= */ true);
        TabUiMetricsHelper.recordSelectionEditorActionMetrics(
                TabListEditorActionMetricGroups.CLOSE);

        return true;
    }

    @Override
    public boolean shouldHideEditorAfterAction() {
        return true;
    }
}
