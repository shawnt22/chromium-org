// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.hub;

import android.view.View;
import android.view.ViewGroup;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController.MenuOrKeyboardActionHandler;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;

/** A base interface representing a UI that will be displayed as a Pane in the Hub. */
@NullMarked
public interface Pane extends BackPressHandler {
    /** Returns the {@link PaneId} corresponding to this Pane. */
    @PaneId
    int getPaneId();

    /** Returns the {@link ViewGroup} containing the contents of the Pane. */
    ViewGroup getRootView();

    /** Returns the {@link MenuOrKeyboardActionHandler} for the Pane. */
    @Nullable
    MenuOrKeyboardActionHandler getMenuOrKeyboardActionHandler();

    /** Returns whether the menu button should be visible for the Pane. */
    boolean getMenuButtonVisible();

    /** Returns the desired color scheme. Should be constant for individual panes. */
    @HubColorScheme
    int getColorScheme();

    /** Destroys the pane. Called when the Hub is destroyed. */
    void destroy();

    /**
     * Sets an interface for controlling certain aspects of the Hub while focused.
     *
     * @param paneHubController An interface that can be used to control the hub, may be null when
     *     not focused. If null is set do not keep a reference to the old controller.
     */
    void setPaneHubController(@Nullable PaneHubController paneHubController);

    /**
     * Notifies of a change to the Hub's or the pane's lifecycle. See {@link LoadHint} for possible
     * values and what the pane could or should do in response to a notification.
     *
     * @param loadHint The {@link LoadHint} for the latest change.
     */
    void notifyLoadHint(@LoadHint int loadHint);

    /** Returns button data for the primary action on the page, such as adding a tab. */
    ObservableSupplier<FullButtonData> getActionButtonDataSupplier();

    /** Returns the visuals for creating a button to navigate to this pane. */
    ObservableSupplier<DisplayButtonData> getReferenceButtonDataSupplier();

    /** Returns whether to show the hairline for the pane. */
    ObservableSupplier<Boolean> getHairlineVisibilitySupplier();

    /** Returns a supplier for a view to overlay the hub with. */
    ObservableSupplier<@Nullable View> getHubOverlayViewSupplier();

    /** Returns an optional listener for animation progress. */
    @Nullable HubLayoutAnimationListener getHubLayoutAnimationListener();

    /**
     * Create a {@link HubLayoutAnimatorProvider} to use when showing the {@link HubLayout} if this
     * pane is focused.
     *
     * @param hubContainerView The {@link HubContainerView} that should show.
     */
    HubLayoutAnimatorProvider createShowHubLayoutAnimatorProvider(
            HubContainerView hubContainerView);

    /**
     * Create a {@link HubLayoutAnimatorProvider} to use when hiding the {@link HubLayout} if this
     * pane is focused.
     *
     * @param hubContainerView The {@link HubContainerView} that should hide.
     */
    HubLayoutAnimatorProvider createHideHubLayoutAnimatorProvider(
            HubContainerView hubContainerView);

    /** Returns whether to enable the state of hub search. */
    ObservableSupplier<Boolean> getHubSearchEnabledStateSupplier();
}
