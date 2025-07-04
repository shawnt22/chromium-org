// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.appmenu;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * Responsible for handling the creation, showing, hiding of the AppMenu and notifying the
 * AppMenuObservers about these actions. This interface may be used by classes outside of app_menu
 * to interact with the app menu.
 */
@NullMarked
public interface AppMenuHandler {
    @IntDef({
        AppMenuItemType.STANDARD,
        AppMenuItemType.TITLE_BUTTON,
        AppMenuItemType.BUTTON_ROW,
        AppMenuItemType.DIVIDER
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface AppMenuItemType {
        /** Regular Android menu item that contains a title and an icon if icon is specified. */
        int STANDARD = 0;

        /**
         * Menu item that has two buttons, the first one is a title and the second one is an icon.
         * It is different from the regular menu item because it contains two separate buttons.
         */
        int TITLE_BUTTON = 1;

        /**
         * Menu item that has multiple buttons (no more than 5). Every one of these buttons is
         * displayed as an icon.
         */
        int BUTTON_ROW = 2;

        /** A divider item to distinguish between menu item groupings. */
        int DIVIDER = 3;

        /**
         * The number of menu item types specified above. If you add a menu item type you MUST
         * increment this.
         */
        int NUM_ENTRIES = 4;
    }

    /**
     * Adds the observer to App Menu.
     * @param observer Observer that should be notified about App Menu changes.
     */
    void addObserver(AppMenuObserver observer);

    /**
     * Removes the observer from the App Menu.
     *
     * @param observer Observer that should no longer be notified about App Menu changes.
     */
    void removeObserver(AppMenuObserver observer);

    /**
     * Calls attention to this menu and a particular item in it. The menu will only stay highlighted
     * for one menu usage. After that the highlight will be cleared.
     *
     * @param highlightItemId The id of a menu item to highlight or {@code null} to turn off the
     *     highlight.
     */
    void setMenuHighlight(Integer highlightItemId);

    /**
     * Overloaded setMenuHighlight method to control whether the menu button itself is highlighted
     * or not.
     *
     * @param highlightItemId The id of a menu item to highlight or {@code null} to turn off the
     *     highlight.
     * @param shouldHighlightMenuButton whether the triple dot app menu button should be highlighted
     */
    void setMenuHighlight(Integer highlightItemId, boolean shouldHighlightMenuButton);

    /** Clears the menu highlight. */
    void clearMenuHighlight();

    /**
     * @return Whether the App Menu is currently showing.
     */
    boolean isAppMenuShowing();

    /** Requests to hide the App Menu. */
    void hideAppMenu();

    /**
     * @return A new {@link AppMenuButtonHelper} to hook up to a view containing a menu button.
     */
    AppMenuButtonHelper createAppMenuButtonHelper();

    /**
     * @return {@link AppMenuPropertiesDelegate} that builds the menu list.
     */
    AppMenuPropertiesDelegate getMenuPropertiesDelegate();
}
