// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs.content;

import org.chromium.base.ObserverList;
import org.chromium.base.supplier.Supplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.customtabs.CustomTabsConnection;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;

/**
 * Holds the Tab currently shown in a Custom Tab activity. Unlike {@link ActivityTabProvider}, is
 * aware of early created tabs that are not yet attached. Is also aware of tab swapping when
 * navigating by links with target="_blank". Thus it is a single source of truth about the current
 * Tab of a Custom Tab activity.
 */
@NullMarked
public class CustomTabActivityTabProvider implements Supplier<@Nullable Tab> {
    private final ObserverList<Observer> mObservers = new ObserverList<>();

    private @Nullable Tab mTab;
    private @TabCreationMode int mTabCreationMode = TabCreationMode.NONE;
    private @Nullable String mSpeculatedUrl;

    public CustomTabActivityTabProvider(String speculatedUrl) {
        mSpeculatedUrl = speculatedUrl;
    }

    /** Adds an {@link Observer} */
    public void addObserver(Observer observer) {
        mObservers.addObserver(observer);
    }

    /** Removes an {@link Observer} */
    public void removeObserver(Observer observer) {
        mObservers.removeObserver(observer);
    }

    /**
     * Returns tab currently managed by the Custom Tab activity.
     *
     * The difference from {@link ActivityTabProvider#get()} is that we may have acquired
     * a hidden tab (see {@link CustomTabsConnection#takeHiddenTab}), which is not yet added to a
     * {@link TabModel}. In that case this method returns the hidden tab, and ActivityTabProvider
     * returns null.
     *
     * During reparenting, both this method and ActivityTabProvider return null.
     */
    public @Nullable Tab getTab() {
        return mTab;
    }

    @Override
    public @Nullable Tab get() {
        return getTab();
    }

    /**
     * Returns a {@link TabCreationMode} specifying how the initial tab was created. Returns {@link
     * TabCreationMode#NONE} if and only if the initial tab has not been yet created.
     */
    public @TabCreationMode int getInitialTabCreationMode() {
        return mTabCreationMode;
    }

    /** Returns speculated url, if there was one. */
    public @Nullable String getSpeculatedUrl() {
        return mSpeculatedUrl;
    }

    public void setInitialTab(Tab tab, @TabCreationMode int creationMode) {
        assert mTab == null;
        assert creationMode != TabCreationMode.NONE;
        mTab = tab;
        mTabCreationMode = creationMode;
        if (creationMode != TabCreationMode.HIDDEN) {
            mSpeculatedUrl = null;
        }
        for (Observer observer : mObservers) {
            observer.onInitialTabCreated(tab, creationMode);
        }
    }

    void removeTab() {
        if (mTab == null) return;
        mTab = null;
        mTabCreationMode = TabCreationMode.NONE;
        for (Observer observer : mObservers) {
            observer.onAllTabsClosed();
        }
    }

    public void swapTab(@Nullable Tab tab) {
        if (mTab == tab) return;
        assert mTab != null : "swapTab shouldn't be called before setInitialTab";
        mTab = tab;
        if (tab == null) {
            for (Observer observer : mObservers) {
                observer.onAllTabsClosed();
            }
        } else {
            for (Observer observer : mObservers) {
                observer.onTabSwapped(tab);
            }
        }
    }

    /**
     * Observer that gets notified about changes of the Tab currently managed by Custom Tab
     * activity.
     */
    public abstract static class Observer {
        /** Fired when the initial tab has been created. */
        public void onInitialTabCreated(Tab tab, @TabCreationMode int mode) {}

        /**
         * Fired when the currently visible tab has changed when navigating by a link with
         * target="_blank" or backwards.
         */
        public void onTabSwapped(Tab tab) {}

        /**
         * Fired when all the Tabs are closed (during shutdown or reparenting).
         *
         * Following a target="_blank" link creates a new tab; going back closes it and brings back
         * the previous tab. In that case onTabSwapped is called, and onAllTabsClosed isn't.
         * If the user unwinds the entire stack of tabs and closes the last one, then
         * onAllTabsClosed is called.
         */
        public void onAllTabsClosed() {}
    }
}
