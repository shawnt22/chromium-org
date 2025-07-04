// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.util.SparseArray;
import android.view.ViewGroup;
import android.view.ViewStructure;
import android.view.autofill.AutofillValue;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.dragdrop.ChromeDragAndDropBrowserDelegate;
import org.chromium.components.embedder_support.view.ContentView;
import org.chromium.content_public.browser.ContentFeatureMap;
import org.chromium.content_public.common.ContentFeatures;
import org.chromium.ui.base.ApplicationViewportInsetSupplier;
import org.chromium.ui.base.ViewAndroidDelegate;
import org.chromium.ui.base.ViewportInsets;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.dragdrop.DragAndDropBrowserDelegate;
import org.chromium.ui.dragdrop.DragStateTracker;

/** Implementation of the abstract class {@link ViewAndroidDelegate} for Chrome. */
@NullMarked
public class TabViewAndroidDelegate extends ViewAndroidDelegate {
    private final TabImpl mTab;

    private @Nullable DragAndDropBrowserDelegate mDragAndDropBrowserDelegate;

    /**
     * The inset for the bottom of the Visual Viewport in pixels, or 0 for no insetting. This is the
     * source of truth for the application viewport inset for this embedder.
     */
    private int mVisualViewportInsetBottomPx;

    /** The inset supplier the observer is currently attached to. */
    private @Nullable ApplicationViewportInsetSupplier mCurrentInsetSupplier;

    private final Callback<ViewportInsets> mInsetObserver =
            (unused) -> updateVisualViewportBottomInset();

    TabViewAndroidDelegate(Tab tab, ContentView containerView) {
        super(containerView);
        mTab = (TabImpl) tab;
        containerView.addOnDragListener(getDragStateTracker());

        if (ContentFeatureMap.isEnabled(ContentFeatures.TOUCH_DRAG_AND_CONTEXT_MENU)) {
            mDragAndDropBrowserDelegate =
                    new ChromeDragAndDropBrowserDelegate(
                            () -> {
                                if (mTab == null || mTab.getWindowAndroid() == null) return null;
                                return mTab.getWindowAndroid().getActivity().get();
                            });
            getDragAndDropDelegate().setDragAndDropBrowserDelegate(mDragAndDropBrowserDelegate);
        }

        mCurrentInsetSupplier = tab.getWindowAndroidChecked().getApplicationBottomInsetSupplier();
        mCurrentInsetSupplier.addObserver(mInsetObserver);

        mTab.addObserver(
                new EmptyTabObserver() {
                    @Override
                    public void onActivityAttachmentChanged(
                            Tab tab, @Nullable WindowAndroid window) {
                        if (mCurrentInsetSupplier != null) {
                            mCurrentInsetSupplier.removeObserver(mInsetObserver);
                            mCurrentInsetSupplier = null;
                        }
                        if (window != null) {
                            mCurrentInsetSupplier =
                                    tab.getWindowAndroidChecked()
                                            .getApplicationBottomInsetSupplier();
                            mCurrentInsetSupplier.addObserver(mInsetObserver);
                        }
                        updateVisualViewportBottomInset();
                    }

                    @Override
                    public void onShown(Tab tab, int type) {
                        updateVisualViewportBottomInset();
                    }

                    @Override
                    public void onHidden(Tab tab, int reason) {
                        updateVisualViewportBottomInset();
                    }
                });
    }

    @Override
    public void onBackgroundColorChanged(int color) {
        mTab.changeWebContentBackgroundColor(color);
    }

    @Override
    public void onControlsChanged(
            int topControlsOffsetY,
            int contentOffsetY,
            int topControlsMinHeightOffsetY,
            int bottomControlsOffsetY,
            int bottomControlsMinHeightOffsetY) {
        TabBrowserControlsOffsetHelper.get(mTab)
                .setOffsets(
                        topControlsOffsetY,
                        contentOffsetY,
                        topControlsMinHeightOffsetY,
                        bottomControlsOffsetY,
                        bottomControlsMinHeightOffsetY);
    }

    @Override
    public @Nullable DragStateTracker getDragStateTracker() {
        return getDragStateTrackerInternal();
    }

    /** Sets the Visual Viewport bottom inset. */
    private void updateVisualViewportBottomInset() {
        int inset =
                mTab.isHidden()
                                || mCurrentInsetSupplier == null
                                || mCurrentInsetSupplier.get() == null
                        ? 0
                        : mCurrentInsetSupplier.get().visualViewportBottomInset;

        if (inset == mVisualViewportInsetBottomPx) return;

        mVisualViewportInsetBottomPx = inset;

        if (mTab.getWebContents() == null
                || mTab.getWebContents().getRenderWidgetHostView() == null) {
            return;
        }

        mTab.getWebContents().getRenderWidgetHostView().onViewportInsetBottomChanged();
    }

    @Override
    // TODO(bokan): "Viewport Inset" is overloaded. Rename to make it clearer this is a "visual
    // viewport" inset. Also the RenderWidgetHostView call above.
    protected int getViewportInsetBottom() {
        return mVisualViewportInsetBottomPx;
    }

    @Override
    public void updateAnchorViews(@Nullable ViewGroup oldContainerView) {
        super.updateAnchorViews(oldContainerView);
        assumeNonNull(oldContainerView);

        assert oldContainerView instanceof ContentView
                : "TabViewAndroidDelegate does not host container views other than ContentView.";

        // Transfer the drag state tracker to the new container view.
        ((ContentView) oldContainerView).removeOnDragListener(getDragStateTracker());
        getContentView().addOnDragListener(getDragStateTracker());
    }

    private ContentView getContentView() {
        assert getContainerView() instanceof ContentView
                : "TabViewAndroidDelegate does not host container views other than ContentView.";

        return (ContentView) getContainerView();
    }

    /* Destroy and clean up {@link DragStateTracker} to the content view. */
    @Override
    public void destroy() {
        super.destroy();
        if (getContentView() != null) {
            getContentView().removeOnDragListener(getDragStateTracker());
        }
        if (mDragAndDropBrowserDelegate != null) {
            getDragAndDropDelegate().setDragAndDropBrowserDelegate(null);
            mDragAndDropBrowserDelegate = null;
        }
        if (mCurrentInsetSupplier != null) {
            mCurrentInsetSupplier.removeObserver(mInsetObserver);
            mCurrentInsetSupplier = null;
        }
    }

    @Override
    public void onProvideAutofillVirtualStructure(ViewStructure structure, int flags) {
        mTab.onProvideAutofillVirtualStructure(structure, flags);
    }

    @Override
    public void autofill(final SparseArray<AutofillValue> values) {
        mTab.autofill(values);
    }

    @Override
    public boolean providesAutofillStructure() {
        return mTab.providesAutofillStructure();
    }

    @Nullable DragAndDropBrowserDelegate getDragAndDropBrowserDelegateForTesting() {
        return mDragAndDropBrowserDelegate;
    }
}
