// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import android.view.View;
import android.view.ViewGroup;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.omnibox.suggestions.OmniboxSuggestionsDropdownScrollListener;
import org.chromium.chrome.browser.omnibox.suggestions.OmniboxSuggestionsVisualState;
import org.chromium.chrome.browser.omnibox.voice.VoiceRecognitionHandler;
import org.chromium.chrome.browser.tab.Tab;

import java.util.Optional;

/** Container that holds the {@link UrlBar} and SSL state related with the current {@link Tab}. */
@NullMarked
public interface LocationBar {
    /** Handle all necessary tasks that can be delayed until initialization completes. */
    default void onDeferredStartup() {}

    /**
     * Call to force the UI to update the state of various buttons based on whether or not the
     * current tab is incognito.
     */
    void updateVisualsForState();

    /**
     * Sets whether the location bar should have a layout showing a title.
     *
     * @param showTitle Whether the title should be shown.
     */
    void setShowTitle(boolean showTitle);

    /**
     * Sends an accessibility event to the URL bar to request accessibility focus on it (e.g. for
     * TalkBack).
     */
    default void requestUrlBarAccessibilityFocus() {}

    /**
     * Triggers the cursor to be visible in the UrlBar without triggering any of the focus animation
     * logic.
     *
     * <p>Only applies to devices with a hardware keyboard attached.
     */
    void showUrlBarCursorWithoutFocusAnimations();

    /**
     * Notifies the LocationBar to take necessary action after exiting from the NTP, while a
     * hardware keyboard is connected. If the URL bar was previously focused on the NTP due to a
     * connected keyboard, a navigation away from the NTP should clear this focus before filling the
     * current tab's URL.
     */
    void clearUrlBarCursorWithoutFocusAnimations();

    /** Selects all of the editable text in the {@link UrlBar}. */
    void selectAll();

    /**
     * Reverts any pending edits of the location bar and reset to the page state. This does not
     * change the focus state of the location bar.
     */
    void revertChanges();

    /** Returns {@link ViewGroup} that this container holds. */
    View getContainerView();

    /**
     * TODO(twellington): Try to remove this method. It's only used to return an in-product help
     * bubble anchor view... which should be moved out of tab and perhaps into the status bar icon
     * component.
     *
     * @return The view containing the security icon.
     */
    View getSecurityIconView();

    /** Returns the {@link VoiceRecognitionHandler} associated with this LocationBar. */
    default @Nullable VoiceRecognitionHandler getVoiceRecognitionHandler() {
        return null;
    }

    /**
     * Returns a (@link OmniboxStub}.
     *
     * <p>TODO(crbug.com/40153747): Inject OmniboxStub where needed and remove this method.
     */
    @Nullable OmniboxStub getOmniboxStub();

    /** Returns the UrlBarData currently in use by the URL bar inside this location bar. */
    UrlBarData getUrlBarData();

    /** Adds an observer for suggestions scroll events. */
    default void addOmniboxSuggestionsDropdownScrollListener(
            OmniboxSuggestionsDropdownScrollListener listener) {}

    /** Removes an observer for suggestions scroll events. */
    default void removeOmniboxSuggestionsDropdownScrollListener(
            OmniboxSuggestionsDropdownScrollListener listener) {}

    Optional<OmniboxSuggestionsVisualState> getOmniboxSuggestionsVisualState();

    /**
     * Toggle showing only the origin portion of the URL (as opposed to the default behavior of
     * showing the max amount of the url, prioritizing the origin)
     */
    default void setShowOriginOnly(boolean showOriginOnly) {}

    /** Toggle the url bar's text size to be small or normal sized. */
    default void setUrlBarUsesSmallText(boolean useSmallText) {}

    /**
     * Toggle whether the status icon should be hidden for secure origins. This should only be used
     * in minimized/reduced presentations of the LocationBar since the status icon has affordances
     * for page-specific permissions, privacy, etc.
     */
    default void setHideStatusIconForSecureOrigins(boolean hideStatusIconForSecureOrigins) {}

    /** Gets the height of the url bar view contained by the location bar. */
    default float getUrlBarHeight() {
        return 0;
    }

    /** Destroys the LocationBar. */
    void destroy();
}
