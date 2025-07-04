// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tabmodel;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabSelectionType;

/**
 * This class serves as a callback from TabModel to TabModelSelector. Avoid adding unnecessary
 * methods that expose too much access to TabModel. http://crbug.com/263579
 */
@NullMarked
public interface TabModelDelegate {
    /**
     * Requests the specified to be shown.
     *
     * @param tab The tab that is requested to be shown.
     * @param type The reason why this tab was requested to be shown.
     */
    void requestToShowTab(@Nullable Tab tab, @TabSelectionType int type);

    /**
     * @return Whether reparenting is currently in progress for this TabModel.
     */
    boolean isReparentingInProgress();

    /**
     * Request to the native TabRestoreService to restore the most recently closed tab.
     *
     * @param model The model requesting the restore.
     */
    default void openMostRecentlyClosedEntry(TabModel model) {}

    // TODO(aurimas): clean these methods up.
    TabModel getCurrentModel();

    /** Provides the top level tab manager object for the current scope. */
    TabModel getModel(boolean incognito);

    /** Provides the top level tab group manager object for the current scope. */
    TabGroupModelFilter getFilter(boolean incognito);

    /**
     * Whether all the tabs in the tab model have been restored from disk. If this is false session
     * restore is still ongoing.
     */
    boolean isTabModelRestored();

    void selectModel(boolean incognito);
}
