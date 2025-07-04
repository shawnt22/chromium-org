// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tabmodel;

import org.chromium.base.Callback;
import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;

import java.util.Iterator;

/**
 * A TabModel implementation that handles off the record tabs.
 *
 * <p>This is not thread safe and must only be operated on the UI thread.
 *
 * <p>The lifetime of this object is not tied to that of the native TabModel. This ensures the
 * native TabModel is present when at least one incognito Tab has been created and added. When no
 * Tabs remain, the native model will be destroyed and only rebuilt when a new incognito Tab is
 * created.
 */
@NullMarked
class IncognitoTabModelImpl implements IncognitoTabModelInternal {
    /** Creates TabModels for use in IncognitoModel. */
    public interface IncognitoTabModelDelegate {
        /** Creates a fully working TabModel to delegate calls to. */
        TabModelInternal createTabModel();

        /** Returns the tab creator for incognito tabs. */
        TabCreator getIncognitoTabCreator();
    }

    private final IncognitoTabModelDelegate mDelegate;
    private final ObserverList<TabModelObserver> mObservers = new ObserverList<>();
    private final ObserverList<IncognitoTabModelObserver> mIncognitoObservers =
            new ObserverList<>();
    private final ObserverList<Callback<TabModelInternal>> mDelegateModelObservers =
            new ObserverList<>();
    private final Callback<@Nullable Tab> mDelegateModelCurrentTabSupplierObserver;
    private final ObservableSupplierImpl<@Nullable Tab> mCurrentTabSupplier =
            new ObservableSupplierImpl<>();
    private final Callback<Integer> mDelegateModelTabCountSupplierObserver;
    private final ObservableSupplierImpl<Integer> mTabCountSupplier =
            new ObservableSupplierImpl<>();
    private final TabRemover mTabRemoverProxy =
            new TabRemover() {
                @Override
                public void closeTabs(
                        TabClosureParams tabClosureParams,
                        boolean allowDialog,
                        @Nullable TabModelActionListener listener) {
                    mDelegateModel
                            .getTabRemover()
                            .closeTabs(tabClosureParams, allowDialog, listener);
                }

                @Override
                public void prepareCloseTabs(
                        TabClosureParams tabClosureParams,
                        boolean allowDialog,
                        @Nullable TabModelActionListener listener,
                        Callback<TabClosureParams> onPreparedCallback) {
                    mDelegateModel
                            .getTabRemover()
                            .prepareCloseTabs(
                                    tabClosureParams, allowDialog, listener, onPreparedCallback);
                }

                @Override
                public void forceCloseTabs(TabClosureParams tabClosureParams) {
                    mDelegateModel.getTabRemover().forceCloseTabs(tabClosureParams);
                }

                @Override
                public void removeTab(
                        Tab tab, boolean allowDialog, @Nullable TabModelActionListener listener) {
                    mDelegateModel.getTabRemover().removeTab(tab, allowDialog, listener);
                }
            };

    private TabModelInternal mDelegateModel;
    private int mCountOfAddingOrClosingTabs;
    private boolean mActive;

    /** Constructor for IncognitoTabModel. */
    // https://github.com/uber/NullAway/issues/1128
    @SuppressWarnings("NullAway")
    IncognitoTabModelImpl(IncognitoTabModelDelegate tabModelCreator) {
        mDelegate = tabModelCreator;
        mDelegateModel = EmptyTabModel.getInstance(true);
        mDelegateModelCurrentTabSupplierObserver = mCurrentTabSupplier::set;
        mDelegateModelTabCountSupplierObserver = mTabCountSupplier::set;
        mTabCountSupplier.set(0);
    }

    /** Ensures that the real TabModel has been created. */
    protected void ensureTabModelImpl() {
        ThreadUtils.assertOnUiThread();
        if (!(mDelegateModel instanceof EmptyTabModel)) return;

        mDelegateModel = mDelegate.createTabModel();
        mDelegateModel
                .getCurrentTabSupplier()
                .addObserver(mDelegateModelCurrentTabSupplierObserver);
        mDelegateModel.getTabCountSupplier().addObserver(mDelegateModelTabCountSupplierObserver);
        for (TabModelObserver observer : mObservers) {
            mDelegateModel.addObserver(observer);
        }
        for (Callback<TabModelInternal> delegateModelObserver : mDelegateModelObservers) {
            delegateModelObserver.onResult(mDelegateModel);
        }
    }

    /**
     * Resets the delegate TabModel to be a stub EmptyTabModel and notifies
     * {@link IncognitoTabModelObserver}s.
     */
    protected void destroyIncognitoIfNecessary() {
        ThreadUtils.assertOnUiThread();
        if (!isEmpty()
                || mDelegateModel instanceof EmptyTabModel
                || mCountOfAddingOrClosingTabs != 0) {
            return;
        }

        for (IncognitoTabModelObserver observer : mIncognitoObservers) {
            observer.didBecomeEmpty();
        }

        mDelegateModel
                .getCurrentTabSupplier()
                .removeObserver(mDelegateModelCurrentTabSupplierObserver);
        mDelegateModel.getTabCountSupplier().removeObserver(mDelegateModelTabCountSupplierObserver);
        mDelegateModel.destroy();
        mCurrentTabSupplier.set(null);
        mTabCountSupplier.set(0);

        mDelegateModel = EmptyTabModel.getInstance(true);
        for (Callback<TabModelInternal> delegateModelObserver : mDelegateModelObservers) {
            delegateModelObserver.onResult(mDelegateModel);
        }
    }

    private boolean isEmpty() {
        return getComprehensiveModel().getCount() == 0;
    }

    // Triggers IncognitoTabModelObserver.wasFirstTabCreated function. This function should only be
    // called just after the first tab is created.
    private void notifyIncognitoObserverFirstTabCreated(boolean shouldTrigger) {
        if (!shouldTrigger) return;
        assert getCount() == 1;

        for (IncognitoTabModelObserver observer : mIncognitoObservers) {
            observer.wasFirstTabCreated();
        }
    }

    @Override
    public @Nullable Profile getProfile() {
        return mDelegateModel.getProfile();
    }

    @Override
    public boolean isIncognito() {
        return true;
    }

    @Override
    public boolean isOffTheRecord() {
        return mDelegateModel.isOffTheRecord();
    }

    @Override
    public boolean isIncognitoBranded() {
        return mDelegateModel.isIncognitoBranded();
    }

    @Override
    public TabRemover getTabRemover() {
        return mTabRemoverProxy;
    }

    @Override
    public boolean closeTabs(TabClosureParams tabClosureParams) {
        mCountOfAddingOrClosingTabs++;
        boolean retVal = mDelegateModel.closeTabs(tabClosureParams);
        mCountOfAddingOrClosingTabs--;
        destroyIncognitoIfNecessary();
        return retVal;
    }

    @Override
    public @Nullable Tab getNextTabIfClosed(int id, boolean uponExit) {
        return mDelegateModel.getNextTabIfClosed(id, uponExit);
    }

    @Override
    public int getCount() {
        return mDelegateModel.getCount();
    }

    @Override
    public @Nullable Tab getTabAt(int index) {
        return mDelegateModel.getTabAt(index);
    }

    @Override
    public @Nullable Tab getTabById(int tabId) {
        return mDelegateModel.getTabById(tabId);
    }

    @Override
    public int indexOf(@Nullable Tab tab) {
        return mDelegateModel.indexOf(tab);
    }

    @Override
    public Iterator<Tab> iterator() {
        // The underlying model already returns a read-only iterator.
        return mDelegateModel.iterator();
    }

    @Override
    public int index() {
        return mDelegateModel.index();
    }

    @Override
    public ObservableSupplier<@Nullable Tab> getCurrentTabSupplier() {
        return mCurrentTabSupplier;
    }

    @Override
    public void setIndex(int i, @TabSelectionType int type) {
        mDelegateModel.setIndex(i, type);
    }

    @Override
    public boolean isActiveModel() {
        return mActive;
    }

    @Override
    public void moveTab(int id, int newIndex) {
        mDelegateModel.moveTab(id, newIndex);
    }

    @Override
    public void pinTab(int tabId) {
        mDelegateModel.pinTab(tabId);
    }

    @Override
    public void unpinTab(int tabId) {
        mDelegateModel.unpinTab(tabId);
    }

    @Override
    public void destroy() {
        mDelegateModel.destroy();
    }

    @Override
    public boolean isClosurePending(int tabId) {
        return mDelegateModel.isClosurePending(tabId);
    }

    @Override
    public boolean supportsPendingClosures() {
        return mDelegateModel.supportsPendingClosures();
    }

    @Override
    public TabList getComprehensiveModel() {
        return mDelegateModel.getComprehensiveModel();
    }

    @Override
    public void commitAllTabClosures() {
        // Return early if no tabs are open. In particular, we don't want to destroy the incognito
        // tab model, in case we are about to add a tab to it.
        if (isEmpty()) return;
        mDelegateModel.commitAllTabClosures();
        destroyIncognitoIfNecessary();
    }

    @Override
    public void commitTabClosure(int tabId) {
        mDelegateModel.commitTabClosure(tabId);
        destroyIncognitoIfNecessary();
    }

    @Override
    public void cancelTabClosure(int tabId) {
        mDelegateModel.cancelTabClosure(tabId);
    }

    @Override
    public ObservableSupplier<Integer> getTabCountSupplier() {
        return mTabCountSupplier;
    }

    @Override
    public TabCreator getTabCreator() {
        return mDelegate.getIncognitoTabCreator();
    }

    @Override
    public void addTab(
            Tab tab, int index, @TabLaunchType int type, @TabCreationState int creationState) {
        mCountOfAddingOrClosingTabs++;
        ensureTabModelImpl();
        boolean shouldTriggerFirstTabCreated = getCount() == 0;
        mDelegateModel.addTab(tab, index, type, creationState);
        notifyIncognitoObserverFirstTabCreated(shouldTriggerFirstTabCreated);
        mCountOfAddingOrClosingTabs--;
    }

    @Override
    public void addObserver(TabModelObserver observer) {
        mObservers.addObserver(observer);
        mDelegateModel.addObserver(observer);
    }

    @Override
    public void removeObserver(TabModelObserver observer) {
        mObservers.removeObserver(observer);
        mDelegateModel.removeObserver(observer);
    }

    @Override
    public void addDelegateModelObserver(Callback<TabModelInternal> delegateModelObserver) {
        mDelegateModelObservers.addObserver(delegateModelObserver);
        if (mDelegateModel != null) {
            delegateModelObserver.onResult(mDelegateModel);
        }
    }

    @Override
    public void addIncognitoObserver(IncognitoTabModelObserver observer) {
        mIncognitoObservers.addObserver(observer);
    }

    @Override
    public void removeIncognitoObserver(IncognitoTabModelObserver observer) {
        mIncognitoObservers.removeObserver(observer);
    }

    @Override
    public void removeTab(Tab tab) {
        mCountOfAddingOrClosingTabs++;
        mDelegateModel.removeTab(tab);
        mCountOfAddingOrClosingTabs--;
        // Call destroyIncognitoIfNecessary() in case the last incognito tab in this model is
        // reparented to a different activity. See crbug.com/611806.
        destroyIncognitoIfNecessary();
    }

    @Override
    public void openMostRecentlyClosedEntry() {}

    @Override
    public void setActive(boolean active) {
        mActive = active;
        if (active) ensureTabModelImpl();
        mDelegateModel.setActive(active);
        if (!active) destroyIncognitoIfNecessary();
    }

    @Override
    public void broadcastSessionRestoreComplete() {}
}
