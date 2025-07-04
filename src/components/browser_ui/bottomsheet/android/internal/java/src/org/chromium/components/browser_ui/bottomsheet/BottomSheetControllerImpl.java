// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.bottomsheet;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.build.annotations.MonotonicNonNull;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.desktop_windowing.AppHeaderState;
import org.chromium.components.browser_ui.desktop_windowing.DesktopWindowStateManager;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.components.browser_ui.widget.scrim.ScrimManager;
import org.chromium.components.browser_ui.widget.scrim.ScrimProperties;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.util.TokenHolder;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.PriorityQueue;

/**
 * This class is responsible for managing the content shown by the {@link BottomSheet}. Features
 * wishing to show content in the {@link BottomSheet} UI must implement {@link BottomSheetContent}
 * and call {@link #requestShowContent(BottomSheetContent, boolean)} which will return true if the
 * content was actually shown (see full doc on method).
 */
@NullMarked
class BottomSheetControllerImpl implements ManagedBottomSheetController, ScrimCoordinator.Observer {
    /** The initial capacity for the priority queue handling pending content show requests. */
    private static final int INITIAL_QUEUE_CAPACITY = 1;

    /**
     * A list of observers maintained by this controller until the bottom sheet is created, at which
     * point they will be added to the bottom sheet.
     */
    private final List<BottomSheetObserver> mPendingSheetObservers;

    /** A means of accessing the ScrimManager. */
    private final Supplier<ScrimManager> mScrimManagerSupplier;

    /**
     * A set of tokens for features suppressing the bottom sheet. If this holder has tokens, the
     * sheet is suppressed.
     */
    private final TokenHolder mSuppressionTokens;

    /** A supplier indicating whether back press should be handled by the bottom sheet. */
    private final ObservableSupplierImpl<Boolean> mBackPressStateChangedSupplier =
            new ObservableSupplierImpl<>();

    /**
     * A {@link BackPressHandler} to handle back press when the bottom sheet is open and/or has
     * sheet content.
     */
    private final BackPressHandler mBackPressHandler;

    /** Whether or not always use the full width of the container. */
    private final boolean mAlwaysFullWidth;

    private final Supplier<Integer> mEdgeToEdgeBottomInsetSupplier;
    private final KeyboardVisibilityDelegate mKeyboardVisibilityDelegate;
    private final @Nullable DesktopWindowStateManager mDesktopWindowStateManager;

    /**
     * An observer that observes changes to the bottom sheet content {@code
     * BottomSheetContent#mBackPressStateChangedSupplier} and updates the {@code
     * BottomSheetControllerImpl#mBackPressStateChangedSupplier}.
     */
    private final Callback<Boolean> mContentBackPressStateChangedObserver =
            contentWillHandleBackPress -> updateBackPressStateChangedSupplier();

    /** A handle to the {@link BottomSheet} that this class controls. */
    private @MonotonicNonNull BottomSheet mBottomSheet;

    /**
     * The container that the sheet exists in. This is one layer inside of the root coordinator view
     * to support the view's shadow.
     */
    private @MonotonicNonNull ViewGroup mBottomSheetContainer;

    /** A queue for content that is waiting to be shown in the {@link BottomSheet}. */
    private @MonotonicNonNull PriorityQueue<BottomSheetContent> mContentQueue;

    /** Whether the controller is already processing a hide request for the tab. */
    private boolean mIsProcessingHideRequest;

    /** Whether the currently processing show request is suppressing existing content. */
    private boolean mIsSuppressingCurrentContent;

    /** A runnable that initializes the bottom sheet when necessary. */
    private @Nullable Runnable mSheetInitializer;

    /** The state of the sheet so it can be returned to what it was prior to suppression. */
    private @SheetState int mSheetStateBeforeSuppress;

    /** The content being shown prior to the sheet being suppressed. */
    private @Nullable BottomSheetContent mContentWhenSuppressed;

    private int mAppHeaderHeight;
    private int mBottomControlsHeight;
    private boolean mIsAnchoredToBottomControls;

    /**
     * Build a new controller of the bottom sheet.
     *
     * @param scrimManagerSupplier A supplier of the scrimManagerSupplier that shows when the bottom
     *     sheet is opened.
     * @param initializedCallback A callback for the sheet being created (as the sheet is not
     *     initialized until first use.
     * @param window A means of accessing the screen size.
     * @param keyboardDelegate A means of hiding the keyboard.
     * @param root The view that should contain the sheet.
     * @param alwaysFullWidth Whether bottom sheet is full-width.
     * @param edgeToEdgeBottomInsetSupplier The supplier of bottom inset when e2e is on.
     * @param desktopWindowStateManager The {@link DesktopWindowStateManager} for the app header.
     */
    public BottomSheetControllerImpl(
            final Supplier<ScrimManager> scrimManagerSupplier,
            Callback<View> initializedCallback,
            Window window,
            KeyboardVisibilityDelegate keyboardDelegate,
            Supplier<ViewGroup> root,
            boolean alwaysFullWidth,
            Supplier<Integer> edgeToEdgeBottomInsetSupplier,
            @Nullable DesktopWindowStateManager desktopWindowStateManager) {
        mScrimManagerSupplier = scrimManagerSupplier;
        mPendingSheetObservers = new ArrayList<>();
        mSuppressionTokens = new TokenHolder(this::onSuppressionTokensChanged);
        mAlwaysFullWidth = alwaysFullWidth;
        mEdgeToEdgeBottomInsetSupplier = edgeToEdgeBottomInsetSupplier;
        mKeyboardVisibilityDelegate = keyboardDelegate;
        mDesktopWindowStateManager = desktopWindowStateManager;
        if (mDesktopWindowStateManager != null) {
            mDesktopWindowStateManager.addObserver(this);
        }

        mSheetInitializer =
                () -> {
                    initializeSheet(initializedCallback, window, keyboardDelegate, root);
                };

        mBackPressHandler =
                new BackPressHandler() {
                    @Override
                    public @BackPressResult int handleBackPress() {
                        assert mBottomSheet != null
                                && !mSuppressionTokens.hasTokens()
                                && mBottomSheet.getCurrentSheetContent() != null;
                        if (Boolean.TRUE.equals(
                                mBottomSheet
                                        .getCurrentSheetContent()
                                        .getBackPressStateChangedSupplier()
                                        .get())) {
                            mBottomSheet.getCurrentSheetContent().onBackPressed();
                            return BackPressResult.SUCCESS;
                        }
                        int sheetState = mBottomSheet.getMinSwipableSheetState();
                        mBottomSheet.setSheetState(sheetState, true, StateChangeReason.BACK_PRESS);
                        return BackPressResult.SUCCESS;
                    }

                    @Override
                    public ObservableSupplier<Boolean> getHandleBackPressChangedSupplier() {
                        return mBackPressStateChangedSupplier;
                    }
                };
    }

    // AppHeaderObserver implementation
    @Override
    public void onAppHeaderStateChanged(AppHeaderState newState) {
        int appHeaderHeight = newState.getAppHeaderHeight();
        if (appHeaderHeight == mAppHeaderHeight) return;
        mAppHeaderHeight = appHeaderHeight;
        if (mBottomSheet != null) {
            mBottomSheet.onAppHeaderHeightChanged(mAppHeaderHeight);
        }
    }

    @Override
    public BackPressHandler getBottomSheetBackPressHandler() {
        return mBackPressHandler;
    }

    /**
     * Do the actual initialization of the bottom sheet.
     *
     * @param initializedCallback A callback for the creation of the sheet.
     * @param window A means of accessing the screen size.
     * @param keyboardDelegate A means of hiding the keyboard.
     * @param root The view that should contain the sheet.
     */
    private void initializeSheet(
            Callback<View> initializedCallback,
            Window window,
            KeyboardVisibilityDelegate keyboardDelegate,
            Supplier<ViewGroup> root) {
        mBottomSheetContainer = root.get();
        if (mBottomSheetContainer == null) {
            return;
        }
        mBottomSheetContainer.setVisibility(View.VISIBLE);

        LayoutInflater.from(root.get().getContext())
                .inflate(R.layout.bottom_sheet, mBottomSheetContainer);
        mBottomSheet = root.get().findViewById(R.id.bottom_sheet);
        initializedCallback.onResult(mBottomSheet);

        mBottomSheet.init(
                window,
                keyboardDelegate,
                mAlwaysFullWidth,
                mEdgeToEdgeBottomInsetSupplier,
                mAppHeaderHeight,
                mBottomControlsHeight);

        // Initialize the queue with a comparator that checks content priority.
        mContentQueue =
                new PriorityQueue<>(
                        INITIAL_QUEUE_CAPACITY,
                        Comparator.comparingInt(BottomSheetContent::getPriority));

        PropertyModel scrimProperties =
                new PropertyModel.Builder(ScrimProperties.ALL_KEYS)
                        .with(ScrimProperties.AFFECTS_STATUS_BAR, true)
                        .with(ScrimProperties.ANCHOR_VIEW, mBottomSheet)
                        .with(ScrimProperties.CLICK_DELEGATE, this::onScrimClicked)
                        .build();

        mBottomSheet.addObserver(
                new EmptyBottomSheetObserver() {
                    /**
                     * Whether the scrim was shown for the last content. TODO(mdjones): We should
                     * try to make sure the content in the sheet is not nulled prior to the close
                     * event occurring; sheets that don't have a peek state make this difficult
                     * since the sheet needs to be hidden before it is closed.
                     */
                    private boolean mScrimShown;

                    @Override
                    public void onSheetOpened(@StateChangeReason int reason) {
                        // The scrim may start visible, meaning we won't get an update. Manually
                        // trigger an update to account for this possibility.
                        scrimVisibilityChanged(mScrimManagerSupplier.get().isShowingScrim());
                        if (mBottomSheet.getCurrentSheetContent() != null
                                && mBottomSheet
                                        .getCurrentSheetContent()
                                        .hasCustomScrimLifecycle()) {
                            updateBackPressStateChangedSupplier();
                            return;
                        }

                        mScrimManagerSupplier.get().showScrim(scrimProperties);
                        mScrimShown = true;
                        updateBackPressStateChangedSupplier();
                    }

                    @Override
                    public void onSheetClosed(@StateChangeReason int reason) {
                        // Hide the scrim if the current content doesn't have a custom scrim
                        // lifecycle.
                        if (mScrimShown) {
                            mScrimManagerSupplier
                                    .get()
                                    .hideScrim(scrimProperties, /* animate= */ true);
                            mScrimShown = false;
                        }

                        // Try to swap contents unless the sheet's content has a custom lifecycle.
                        if (mBottomSheet.getCurrentSheetContent() != null
                                && !mBottomSheet.getCurrentSheetContent().hasCustomLifecycle()) {
                            // If the sheet is closed, it is an opportunity for another content to
                            // try to take its place if it is a higher priority.
                            BottomSheetContent content = mBottomSheet.getCurrentSheetContent();
                            BottomSheetContent nextContent = mContentQueue.peek();
                            if (content != null
                                    && nextContent != null
                                    && nextContent.getPriority() < content.getPriority()) {
                                mContentQueue.add(content);
                                mBottomSheet.setSheetState(SheetState.HIDDEN, true);
                            }
                        }
                        updateBackPressStateChangedSupplier();
                    }

                    @Override
                    public void onSheetStateChanged(@SheetState int state, int reason) {
                        // If hiding request is in progress, destroy the current sheet content being
                        // hidden even when it is in suppressed state. See
                        // https://crbug.com/1057966.
                        if (state != SheetState.HIDDEN
                                || (!mIsProcessingHideRequest && mSuppressionTokens.hasTokens())) {
                            return;
                        }
                        if (mBottomSheet.getCurrentSheetContent() != null
                                && !mIsSuppressingCurrentContent) {
                            mBottomSheet.getCurrentSheetContent().destroy();
                        }
                        mIsSuppressingCurrentContent = false;
                        mIsProcessingHideRequest = false;
                        showNextContent(true);
                        updateBackPressStateChangedSupplier();
                    }

                    @Override
                    public void onSheetContentChanged(@Nullable BottomSheetContent newContent) {
                        updateBackPressStateChangedSupplier();

                        if (newContent != null) return;

                        // If there are no more things to be shown, the container can avoid layouts.
                        mBottomSheetContainer.setVisibility(View.GONE);
                    }
                });

        mScrimManagerSupplier.get().addObserver(this);
        // Add any of the pending observers that were added prior to the sheet being created.
        for (int i = 0; i < mPendingSheetObservers.size(); i++) {
            mBottomSheet.addObserver(mPendingSheetObservers.get(i));
        }
        mPendingSheetObservers.clear();
        mSheetInitializer = null;
    }

    @Override
    public void setBrowserControlsHiddenRatio(float ratio) {
        if (mBottomSheet != null) mBottomSheet.setBrowserControlsHiddenRatio(ratio);
    }

    @Override
    public void setBottomControlsHeight(int bottomControlsHeight) {
        if (mBottomControlsHeight == bottomControlsHeight) return;
        mBottomControlsHeight = bottomControlsHeight;
        if (mScrimManagerSupplier.hasValue()) {
            // Set the appropriate offset for the current scrim state.
            scrimVisibilityChanged(mScrimManagerSupplier.get().isShowingScrim());
        }
    }

    @Override
    public ScrimManager getScrimManager() {
        return mScrimManagerSupplier.get();
    }

    @Override
    public PropertyModel createScrimParams() {
        return new PropertyModel.Builder(ScrimProperties.ALL_KEYS)
                .with(ScrimProperties.AFFECTS_STATUS_BAR, true)
                .with(ScrimProperties.ANCHOR_VIEW, mBottomSheet)
                .with(ScrimProperties.CLICK_DELEGATE, this::onScrimClicked)
                .build();
    }

    // Destroyable implementation.
    @Override
    public void destroy() {
        if (mBottomSheet != null) mBottomSheet.destroy();
        if (mDesktopWindowStateManager != null) {
            mDesktopWindowStateManager.removeObserver(this);
        }
    }

    @Override
    public boolean handleBackPress() {
        // If suppressed (therefore invisible), users are likely to expect for Chrome
        // browser, not the bottom sheet, to react. Do not consume the event.
        if (mBottomSheet == null || mSuppressionTokens.hasTokens()) return false;

        // Give the sheet the opportunity to handle the back press itself before falling to the
        // default "close" behavior.
        if (getCurrentSheetContent() != null && getCurrentSheetContent().handleBackPress()) {
            return true;
        }

        if (!mBottomSheet.isSheetOpen()) return false;
        int sheetState = mBottomSheet.getMinSwipableSheetState();
        mBottomSheet.setSheetState(sheetState, true, StateChangeReason.BACK_PRESS);
        return true;
    }

    @Override
    public @Nullable BottomSheetContent getCurrentSheetContent() {
        return mBottomSheet == null ? null : mBottomSheet.getCurrentSheetContent();
    }

    @Override
    @SheetState
    public int getSheetState() {
        return mBottomSheet == null ? SheetState.HIDDEN : mBottomSheet.getSheetState();
    }

    @Override
    @SheetState
    public int getTargetSheetState() {
        return mBottomSheet == null ? SheetState.NONE : mBottomSheet.getTargetSheetState();
    }

    @Override
    public boolean isSheetOpen() {
        return mBottomSheet != null && mBottomSheet.isSheetOpen();
    }

    @Override
    public boolean isSheetHiding() {
        return mBottomSheet == null ? false : mBottomSheet.isHiding();
    }

    @Override
    public int getCurrentOffset() {
        return mBottomSheet == null ? 0 : (int) mBottomSheet.getCurrentOffsetPx();
    }

    @Override
    public int getContainerHeight() {
        return mBottomSheet != null ? (int) mBottomSheet.getSheetContainerHeight() : 0;
    }

    @Override
    public int getContainerWidth() {
        return mBottomSheet != null ? (int) mBottomSheet.getSheetContainerWidth() : 0;
    }

    @Override
    public int getMaxSheetWidth() {
        return mBottomSheet != null ? mBottomSheet.getMaxSheetWidth() : 0;
    }

    @Override
    public void addObserver(BottomSheetObserver observer) {
        if (mBottomSheet == null) {
            mPendingSheetObservers.add(observer);
            return;
        }
        mBottomSheet.addObserver(observer);
    }

    @Override
    public void removeObserver(BottomSheetObserver observer) {
        if (mBottomSheet != null) {
            mBottomSheet.removeObserver(observer);
        } else {
            mPendingSheetObservers.remove(observer);
        }
    }

    /** Handle a change in the state of the token holder responsible for the suppression tokens. */
    private void onSuppressionTokensChanged() {
        if (!mSuppressionTokens.hasTokens()) undoSuppression();
        updateBackPressStateChangedSupplier();
    }

    @Override
    public int suppressSheet(@StateChangeReason int reason) {
        boolean hadTokens = mSuppressionTokens.hasTokens();
        int token = mSuppressionTokens.acquireToken();
        if (!hadTokens && mBottomSheet != null) {
            // Make sure we don't save an invalid final state (particularly "scrolling").
            mSheetStateBeforeSuppress = getTargetSheetState();
            if (mSheetStateBeforeSuppress == SheetState.NONE) {
                mSheetStateBeforeSuppress = getSheetState();
            }

            mContentWhenSuppressed = getCurrentSheetContent();
            mBottomSheet.setSheetState(SheetState.HIDDEN, false, reason);
        }

        return token;
    }

    @Override
    public void unsuppressSheet(int token) {
        mSuppressionTokens.releaseToken(token);
    }

    private void undoSuppression() {
        if (mBottomSheet == null) return;

        if (mBottomSheet.getCurrentSheetContent() != null) {
            @SheetState
            int openState =
                    mContentWhenSuppressed == getCurrentSheetContent()
                            ? mSheetStateBeforeSuppress
                            : mBottomSheet.getOpeningState();
            mBottomSheet.setSheetState(openState, true);
        } else {
            // In the event the previous content was hidden, try to show the next one.
            showNextContent(true);
        }
        mContentWhenSuppressed = null;
        mSheetStateBeforeSuppress = SheetState.NONE;
    }

    void setSheetStateForTesting(@SheetState int state, boolean animate) {
        assumeNonNull(mBottomSheet).setSheetState(state, animate);
    }

    View getBottomSheetViewForTesting() {
        return assumeNonNull(mBottomSheet);
    }

    ViewGroup getBottomSheetContainerForTesting() {
        return assumeNonNull(mBottomSheetContainer);
    }

    public void endAnimationsForTesting() {
        assumeNonNull(mBottomSheet).endAnimations();
    }

    @VisibleForTesting
    public void forceDismissAllContent() {
        clearRequestsAndHide();

        // Handle content that has a custom lifecycle.
        hideContent(assumeNonNull(mBottomSheet).getCurrentSheetContent(), /* animate= */ true);
    }

    @Override
    public boolean requestShowContent(BottomSheetContent content, boolean animate) {
        if (content == null) {
            throw new RuntimeException("Attempting to show null content in the sheet!");
        }
        if (mBottomSheet == null) assumeNonNull(mSheetInitializer).run();
        if (mBottomSheet == null) {
            return false;
        }
        assumeNonNull(mContentQueue);

        // If already showing (or queued to show) the requested content, do nothing.
        if (content == mBottomSheet.getCurrentSheetContent() || mContentQueue.contains(content)) {
            return content == mBottomSheet.getCurrentSheetContent();
        }

        boolean shouldSwapForPriorityContent =
                mBottomSheet.getCurrentSheetContent() != null
                        && content.getPriority()
                                < mBottomSheet.getCurrentSheetContent().getPriority()
                        && canBottomSheetSwitchContent();

        // Always add the content to the queue, it will be handled after the sheet closes if
        // necessary. If already hidden, |showNextContent| will handle the request.
        mContentQueue.add(content);

        if (mBottomSheet.getCurrentSheetContent() == null && !mSuppressionTokens.hasTokens()) {
            showNextContent(animate);
            return true;
        } else if (shouldSwapForPriorityContent) {
            mIsSuppressingCurrentContent = true;
            mContentQueue.add(mBottomSheet.getCurrentSheetContent());
            if (!mSuppressionTokens.hasTokens()) {
                mBottomSheet.setSheetState(SheetState.HIDDEN, animate);
                return true;
            } else {
                // Since the sheet is already suppressed and hidden, clear the sheet's content if
                // the requested content is higher priority. The undo suppression logic will figure
                // out which content to show next.
                mBottomSheet.showContent(null);
            }
        }
        return false;
    }

    @Override
    public void hideContent(
            @Nullable BottomSheetContent content,
            boolean animate,
            @StateChangeReason int hideReason) {
        if (mBottomSheet == null) return;

        if (content != mBottomSheet.getCurrentSheetContent()) {
            assumeNonNull(mContentQueue).remove(content);
            return;
        }

        if (mIsProcessingHideRequest) return;

        // Handle showing the next content if it exists.
        if (mBottomSheet.getSheetState() == SheetState.HIDDEN) {
            // If the sheet is already hidden, destroy it and simply show the next content.
            // TODO(mdjones): Add tests to make sure the content is being destroyed as expected.
            if (mBottomSheet.getCurrentSheetContent() != null) {
                mBottomSheet.getCurrentSheetContent().destroy();
            }
            showNextContent(animate);
        } else {
            mIsProcessingHideRequest = true;
            mBottomSheet.setSheetState(SheetState.HIDDEN, animate, hideReason);
        }
    }

    @Override
    public void hideContent(@Nullable BottomSheetContent content, boolean animate) {
        hideContent(content, animate, StateChangeReason.NONE);
    }

    @Override
    public void expandSheet() {
        if (mBottomSheet == null || mSuppressionTokens.hasTokens() || mBottomSheet.isHiding()) {
            return;
        }

        if (mBottomSheet.getCurrentSheetContent() == null) return;
        mBottomSheet.setSheetState(SheetState.HALF, true);
    }

    @Override
    public boolean collapseSheet(boolean animate) {
        if (mBottomSheet == null || mSuppressionTokens.hasTokens() || mBottomSheet.isHiding()) {
            return false;
        }
        if (mBottomSheet.isSheetOpen() && mBottomSheet.isPeekStateEnabled()) {
            mBottomSheet.setSheetState(SheetState.PEEK, animate);
            return true;
        }
        return false;
    }

    /**
     * Show the next {@link BottomSheetContent} if it is available and peek the sheet. If no content
     * is available the sheet's content is set to null.
     * @param animate Whether the sheet should animate opened.
     */
    private void showNextContent(boolean animate) {
        if (assumeNonNull(mBottomSheet).getSheetState() != SheetState.HIDDEN) {
            throw new RuntimeException("Showing next content before sheet is hidden!");
        }

        // Make sure the container is visible as it is set to "gone" when there is no content.
        assumeNonNull(mBottomSheetContainer).setVisibility(View.VISIBLE);

        if (assumeNonNull(mContentQueue).isEmpty()) {
            mBottomSheet.showContent(null);
            return;
        }

        BottomSheetContent nextContent = mContentQueue.poll();
        if (mBottomSheet.getCurrentSheetContent() != null) {
            mBottomSheet
                    .getCurrentSheetContent()
                    .getBackPressStateChangedSupplier()
                    .removeObserver(mContentBackPressStateChangedObserver);
        }
        if (nextContent != null) {
            mKeyboardVisibilityDelegate.hideKeyboard(mBottomSheetContainer);
            nextContent
                    .getBackPressStateChangedSupplier()
                    .addObserver(mContentBackPressStateChangedObserver);
        }
        mBottomSheet.showContent(nextContent);
        mBottomSheet.setSheetState(mBottomSheet.getOpeningState(), animate);
    }

    @Override
    public void clearRequestsAndHide() {
        if (mBottomSheet == null) return;

        clearRequests(assumeNonNull(mContentQueue).iterator());

        BottomSheetContent currentContent = mBottomSheet.getCurrentSheetContent();
        if (currentContent == null || !currentContent.hasCustomLifecycle()) {
            hideContent(currentContent, /* animate= */ true);
        }
        mContentWhenSuppressed = null;
        mSheetStateBeforeSuppress = SheetState.NONE;
    }

    @Override
    public boolean isFullWidth() {
        return assumeNonNull(mBottomSheet).isFullWidth();
    }

    @Override
    @VisibleForTesting
    public boolean isSmallScreen() {
        return assumeNonNull(mBottomSheet).isSmallScreen();
    }

    @Override
    public boolean isAnchoredToBottomControls() {
        return mIsAnchoredToBottomControls;
    }

    @Override
    public @Nullable Integer getSheetBackgroundColor() {
        if (mBottomSheet == null
                || getCurrentSheetContent() == null
                || !getCurrentSheetContent().hasSolidBackgroundColor()) {
            return null;
        }
        return mBottomSheet.getSheetBackgroundColor();
    }

    // ScrimCoordinator.Observer
    @Override
    public void scrimVisibilityChanged(boolean scrimVisible) {
        if (mBottomSheet == null) return;
        assumeNonNull(mBottomSheetContainer);
        if (scrimVisible && mBottomSheet.isSheetOpen()) {
            // Scrimmed bottom sheet. Draw the bottom sheet container on top of all sibling views,
            // originating from the bottom of the screen.
            mIsAnchoredToBottomControls = false;
            mBottomSheetContainer.setZ(1.0f);
            mBottomSheet.setBottomMargin(0);
        } else {
            // Unscrimmed bottom sheet. Draw the bottom sheet container in its "natural" order; i.e.
            // in the order specified in res_app/layout/main.xml. Draw originating from the top of
            // the bottom controls.
            mIsAnchoredToBottomControls = true;
            mBottomSheetContainer.setZ(0.0f);
            mBottomSheet.setBottomMargin(mBottomControlsHeight);
        }
    }

    /**
     * Remove all contents from {@code iterator} that don't have a custom lifecycle.
     *
     * @param iterator The iterator whose items must be removed.
     */
    private void clearRequests(Iterator<BottomSheetContent> iterator) {
        while (iterator.hasNext()) {
            if (!iterator.next().hasCustomLifecycle()) {
                iterator.remove();
            }
        }
    }

    /**
     * The bottom sheet cannot change content while it is open, unless the current content returns
     * true from canSuppressInAnyState(). If the user has the bottom sheet open with content that is
     * not suppressable, they are currently engaged in a task and shouldn't be interrupted.
     *
     * @return Whether the sheet currently supports switching its content.
     */
    private boolean canBottomSheetSwitchContent() {
        BottomSheetContent currentContent = assumeNonNull(mBottomSheet).getCurrentSheetContent();
        if (!mBottomSheet.isSheetOpen()) {
            return true;
        }

        if (currentContent != null && currentContent.canSuppressInAnyState()) {
            assert currentContent.getPriority() == BottomSheetContent.ContentPriority.LOW;
            return true;
        }

        return false;
    }

    boolean hasSuppressionTokensForTesting() {
        return mSuppressionTokens.hasTokens();
    }

    /**
     * Update the supplier to hold true when the sheet is in a valid state and holds sheet content,
     * and when there are no suppression tokens, false otherwise.
     */
    private void updateBackPressStateChangedSupplier() {
        mBackPressStateChangedSupplier.set(
                mBottomSheet != null
                        && !mSuppressionTokens.hasTokens()
                        && mBottomSheet.getCurrentSheetContent() != null
                        && (Boolean.TRUE.equals(
                                        mBottomSheet
                                                .getCurrentSheetContent()
                                                .getBackPressStateChangedSupplier()
                                                .get())
                                || mBottomSheet.isSheetOpen()));
    }

    private void onScrimClicked() {
        if (!assumeNonNull(mBottomSheet).isSheetOpen()) return;
        mBottomSheet.setSheetState(
                mBottomSheet.getMinSwipableSheetState(), true, StateChangeReason.TAP_SCRIM);
    }

    void runSheetInitializerForTesting() {
        assumeNonNull(mSheetInitializer).run();
    }
}
