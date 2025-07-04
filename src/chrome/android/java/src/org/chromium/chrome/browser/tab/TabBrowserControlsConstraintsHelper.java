// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import static org.chromium.build.NullUtil.assumeNonNull;

import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.base.ObserverList.RewindableIterator;
import org.chromium.base.UserData;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.cc.input.BrowserControlsOffsetTagModifications;
import org.chromium.cc.input.BrowserControlsState;
import org.chromium.cc.input.OffsetTag;
import org.chromium.chrome.browser.browser_controls.BrowserControlsOffsetTagsInfo;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.components.browser_ui.util.BrowserControlsVisibilityDelegate;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;

/** Manages the state of tab browser controls. */
@NullMarked
public class TabBrowserControlsConstraintsHelper implements UserData {
    private static final Class<TabBrowserControlsConstraintsHelper> USER_DATA_KEY =
            TabBrowserControlsConstraintsHelper.class;

    private final TabImpl mTab;
    private final Callback<@BrowserControlsState Integer> mConstraintsChangedCallback;

    private long mNativeTabBrowserControlsConstraintsHelper; // Lazily initialized in |update|
    private @Nullable BrowserControlsVisibilityDelegate mVisibilityDelegate;

    // These OffsetTags are used in:
    //   - Browser, to tag the layers that move with top controls to be moved by viz.
    //   - Renderer, to tag the corresponding scroll offset in the compositor frame's metadata.
    // When visibility of the browser controls are forced by the browser, this token will be null.
    private BrowserControlsOffsetTagsInfo mOffsetTagsInfo;

    public static void createForTab(Tab tab) {
        tab.getUserDataHost()
                .setUserData(USER_DATA_KEY, new TabBrowserControlsConstraintsHelper(tab));
    }

    private static @Nullable TabBrowserControlsConstraintsHelper safeGet(@Nullable Tab tab) {
        return tab == null ? null : get(tab);
    }

    public static @Nullable TabBrowserControlsConstraintsHelper get(Tab tab) {
        return tab.getUserDataHost().getUserData(USER_DATA_KEY);
    }

    /**
     * Returns the current visibility constraints for the display of browser controls. {@link
     * BrowserControlsState} defines the valid return options.
     *
     * @param tab Tab whose browser controls state is looked into.
     * @return The current visibility constraints.
     */
    public static @BrowserControlsState int getConstraints(Tab tab) {
        TabBrowserControlsConstraintsHelper helper = safeGet(tab);
        if (helper == null) return BrowserControlsState.BOTH;
        return helper.getConstraints();
    }

    /**
     * Returns the constraints delegate for a particular tab. The returned supplier will always be
     * associated with that tab, even if it stops being the active tab.
     *
     * @param tab Tab whose browser controls state is looked into.
     * @return Observable supplier for the current visibility constraints.
     */
    public static @Nullable
            ObservableSupplier<@BrowserControlsState Integer> getObservableConstraints(Tab tab) {
        TabBrowserControlsConstraintsHelper helper = safeGet(tab);
        if (helper == null) return null;
        return helper.mVisibilityDelegate;
    }

    /**
     * Push state about whether or not the browser controls can show or hide to the renderer.
     *
     * @param tab Tab object.
     */
    public static void updateEnabledState(Tab tab) {
        TabBrowserControlsConstraintsHelper helper = safeGet(tab);
        if (helper == null) return;
        helper.updateEnabledState();
    }

    /**
     * Updates the browser controls state for this tab. As these values are set at the renderer
     * level, there is potential for this impacting other tabs that might share the same process.
     *
     * @param tab Tab whose browser constrol state is looked into.
     * @param current The desired current state for the controls. Pass {@link
     *     BrowserControlsState#BOTH} to preserve the current position.
     * @param animate Whether the controls should animate to the specified ending condition or
     *     should jump immediately.
     */
    public static void update(Tab tab, int current, boolean animate) {
        TabBrowserControlsConstraintsHelper helper = safeGet(tab);
        if (helper == null) return;
        helper.update(current, animate);
    }

    /** Constructor */
    private TabBrowserControlsConstraintsHelper(Tab tab) {
        mOffsetTagsInfo = new BrowserControlsOffsetTagsInfo(null, null, null);
        mTab = (TabImpl) tab;
        mConstraintsChangedCallback = unused_constraints -> updateEnabledState();
        mTab.addObserver(
                new EmptyTabObserver() {
                    @Override
                    public void onInitialized(Tab tab, @Nullable String appId) {
                        updateVisibilityDelegate();
                    }

                    @Override
                    public void onActivityAttachmentChanged(
                            Tab tab, @Nullable WindowAndroid window) {
                        if (window != null) updateVisibilityDelegate();
                    }

                    @Override
                    public void onDestroyed(Tab tab) {
                        tab.removeObserver(this);
                    }

                    private void updateAfterRendererProcessSwitch(Tab tab, boolean hasCommitted) {
                        int constraints = getConstraints();
                        if (constraints == BrowserControlsState.SHOWN
                                && hasCommitted
                                && TabBrowserControlsOffsetHelper.get(tab).topControlsOffset()
                                        == 0) {
                            // If the browser controls were already fully visible on the previous
                            // page, then avoid an animation to keep the controls from jumping
                            // around.
                            update(BrowserControlsState.SHOWN, false);
                        } else {
                            updateEnabledState();
                        }
                    }

                    @Override
                    public void onDidFinishNavigationInPrimaryMainFrame(
                            Tab tab, NavigationHandle navigationHandle) {
                        // At this point, we might have switched renderer processes, so push the
                        // existing constraints to the new renderer (has the potential to be
                        // slightly spammy, but the renderer has logic to suppress duplicate
                        // calls).
                        updateAfterRendererProcessSwitch(tab, navigationHandle.hasCommitted());
                    }

                    @Override
                    public void onHidden(Tab tab, @TabHidingType int type) {
                        if (ChromeFeatureList.sBrowserControlsInViz.isEnabled()) {
                            unregisterOffsetTags();
                        }
                    }

                    @Override
                    public void onShown(Tab tab, @TabHidingType int type) {
                        if (ChromeFeatureList.sBrowserControlsInViz.isEnabled()) {
                            updateEnabledState();
                        }
                    }

                    @Override
                    public void onWebContentsSwapped(
                            Tab tab, boolean didStartLoad, boolean didFinishLoad) {
                        updateAfterRendererProcessSwitch(tab, true);
                    }
                });
        if (mTab.isInitialized() && !mTab.isDetached()) updateVisibilityDelegate();
    }

    @Override
    public void destroy() {
        if (mVisibilityDelegate != null) {
            mVisibilityDelegate.removeObserver(mConstraintsChangedCallback);
            mVisibilityDelegate = null;
        }

        if (mNativeTabBrowserControlsConstraintsHelper != 0) {
            TabBrowserControlsConstraintsHelperJni.get()
                    .onDestroyed(
                            mNativeTabBrowserControlsConstraintsHelper,
                            TabBrowserControlsConstraintsHelper.this);
        }
    }

    private void updateVisibilityDelegate() {
        if (mVisibilityDelegate != null) {
            mVisibilityDelegate.removeObserver(mConstraintsChangedCallback);
        }
        mVisibilityDelegate =
                assumeNonNull(mTab.getDelegateFactory())
                        .createBrowserControlsVisibilityDelegate(mTab);
        if (mVisibilityDelegate != null) {
            mVisibilityDelegate.addObserver(mConstraintsChangedCallback);
        }
    }

    private boolean isStateForced(int state) {
        return state == BrowserControlsState.HIDDEN || state == BrowserControlsState.SHOWN;
    }

    private void updateEnabledState() {
        if (mTab.isFrozen()) return;
        update(BrowserControlsState.BOTH, getConstraints() != BrowserControlsState.HIDDEN);
    }

    /** Unregister all OffsetTags (for now, only the top controls have an OffsetTag.) */
    private void unregisterOffsetTags() {
        updateOffsetTags(new BrowserControlsOffsetTagsInfo(null, null, null), getConstraints());
    }

    private void updateOffsetTags(
            BrowserControlsOffsetTagsInfo newOffsetTags, @BrowserControlsState int constraints) {
        if (newOffsetTags == mOffsetTagsInfo) {
            return;
        }

        // Relies on BrowserControlsManager and BottomControlsMediator to set the heights of the
        // top/bottom controls and their shadows.
        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) {
            observers
                    .next()
                    .onBrowserControlsConstraintsChanged(
                            mTab, mOffsetTagsInfo, newOffsetTags, constraints);
        }

        mOffsetTagsInfo = newOffsetTags;
    }

    private void generateOffsetTags(@BrowserControlsState int constraints) {
        if (mTab.isHidden()) {
            return;
        }

        boolean isNewStateForced = isStateForced(constraints);
        if (!mOffsetTagsInfo.hasTags() && !isNewStateForced) {
            OffsetTag bottomControlsOffsetTag = null;
            if (ChromeFeatureList.sBcivBottomControls.isEnabled()) {
                bottomControlsOffsetTag = OffsetTag.createRandom();
            }

            updateOffsetTags(
                    new BrowserControlsOffsetTagsInfo(
                            OffsetTag.createRandom(),
                            OffsetTag.createRandom(),
                            bottomControlsOffsetTag),
                    constraints);
        } else if (mOffsetTagsInfo.hasTags() && isNewStateForced) {
            updateOffsetTags(new BrowserControlsOffsetTagsInfo(null, null, null), constraints);
        }
    }

    /**
     * Updates the browser controls state for this tab. As these values are set at the renderer
     * level, there is potential for this impacting other tabs that might share the same process.
     *
     * @param current The desired current state for the controls. Pass {@link
     *     BrowserControlsState#BOTH} to preserve the current position.
     * @param animate Whether the controls should animate to the specified ending condition or
     *     should jump immediately.
     */
    public void update(int current, boolean animate) {
        assert mTab.getWebContents() != null : "Shouldn't update a Tab with a null WebContents.";

        int constraints = getConstraints();

        // Do nothing if current and constraints conflict to avoid error in renderer.
        if ((constraints == BrowserControlsState.HIDDEN && current == BrowserControlsState.SHOWN)
                || (constraints == BrowserControlsState.SHOWN
                        && current == BrowserControlsState.HIDDEN)) {
            return;
        }

        if (ChromeFeatureList.sBrowserControlsInViz.isEnabled()) {
            generateOffsetTags(constraints);
        }

        if (current == BrowserControlsState.SHOWN || constraints == BrowserControlsState.SHOWN) {
            mTab.willShowBrowserControls();
        }

        if (mNativeTabBrowserControlsConstraintsHelper == 0) {
            mNativeTabBrowserControlsConstraintsHelper =
                    TabBrowserControlsConstraintsHelperJni.get()
                            .init(TabBrowserControlsConstraintsHelper.this);
        }

        BrowserControlsOffsetTagModifications offsetTagModifications =
                new BrowserControlsOffsetTagModifications(
                        mOffsetTagsInfo.getTags(),
                        mOffsetTagsInfo.getTopControlsAdditionalHeight(),
                        mOffsetTagsInfo.getBottomControlsAdditionalHeight());
        TabBrowserControlsConstraintsHelperJni.get()
                .updateState(
                        mNativeTabBrowserControlsConstraintsHelper,
                        TabBrowserControlsConstraintsHelper.this,
                        mTab.getWebContents(),
                        constraints,
                        current,
                        animate,
                        offsetTagModifications);
    }

    private @BrowserControlsState int getConstraints() {
        return mVisibilityDelegate == null ? BrowserControlsState.BOTH : mVisibilityDelegate.get();
    }

    public static void setForTesting(Tab tab, TabBrowserControlsConstraintsHelper helper) {
        tab.getUserDataHost().setUserData(USER_DATA_KEY, helper);
    }

    @NativeMethods
    interface Natives {
        long init(TabBrowserControlsConstraintsHelper caller);

        void onDestroyed(
                long nativeTabBrowserControlsConstraintsHelper,
                TabBrowserControlsConstraintsHelper caller);

        void updateState(
                long nativeTabBrowserControlsConstraintsHelper,
                TabBrowserControlsConstraintsHelper caller,
                WebContents webContents,
                int contraints,
                int current,
                boolean animate,
                BrowserControlsOffsetTagModifications offsetTagsInfo);
    }
}
