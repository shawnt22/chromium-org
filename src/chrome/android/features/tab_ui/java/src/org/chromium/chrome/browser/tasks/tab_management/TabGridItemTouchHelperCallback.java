// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.tasks.tab_management.MessageCardViewProperties.MESSAGE_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.MESSAGE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.TAB;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.ViewConfiguration;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.recyclerview.widget.ItemTouchHelper;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.base.ResettersForTesting;
import org.chromium.base.Token;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabId;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.browser.tabmodel.TabGroupUtils;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tasks.tab_management.MessageService.MessageType;
import org.chromium.chrome.browser.tasks.tab_management.TabGridItemLongPressOrchestrator.OnLongPressTabItemEventListener;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.browser.tasks.tab_management.TabListMediator.TabActionListener;
import org.chromium.chrome.browser.tasks.tab_management.TabListMediator.TabGridDialogHandler;
import org.chromium.chrome.browser.tasks.tab_management.TabProperties.UiType;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.SimpleRecyclerViewAdapter;
import org.chromium.ui.recyclerview.widget.ItemTouchHelper2;

import java.util.List;

/**
 * A {@link ItemTouchHelper2.SimpleCallback} implementation to host the logic for swipe and drag
 * related actions in grid related layouts.
 */
public class TabGridItemTouchHelperCallback extends ItemTouchHelper2.SimpleCallback {
    /** An interface to observe drop tab events on top of an archival message card. */
    @FunctionalInterface
    public interface OnDropOnArchivalMessageCardEventListener {
        /**
         * Notify the observers that the drop event on the archival message card has triggered.
         *
         * @param tabId The ID of the tab dropped on the archival message card.
         */
        void onDropTab(@TabId int tabId);
    }

    private static final long LONGPRESS_DURATION_MS = ViewConfiguration.getLongPressTimeout();
    private final TabListModel mModel;
    private final Supplier<TabGroupModelFilter> mCurrentTabGroupModelFilterSupplier;
    private final ObservableSupplierImpl<Integer> mRecentlySwipedTabIdSupplier =
            new ObservableSupplierImpl<>(Tab.INVALID_TAB_ID);
    private final TabListMediator.TabActionListener mTabClosedListener;
    private final String mComponentName;
    private final TabListMediator.TabGridDialogHandler mTabGridDialogHandler;
    private final int mLongPressDpThresholdSquared;
    private final TabGroupCreationDialogManager mTabGroupCreationDialogManager;
    private final ObservableSupplierImpl<RecyclerView> mRecyclerViewSupplier =
            new ObservableSupplierImpl<>();
    private float mSwipeToDismissThreshold;
    private final float mLongPressDpCancelThreshold;
    private float mMergeThreshold;
    private float mUngroupThreshold;
    // A bool to track whether an action such as swiping, group/ungroup and drag past a certain
    // threshold was attempted. This can determine if a longpress on the tab is the objective.
    private boolean mActionAttempted;
    // A bool to track whether any action that is not a pure longpress hold-no-drag, was started.
    // This can determine if an unwanted following click from a pure longpress must be blocked.
    private boolean mActionStarted;
    private boolean mActionsOnAllRelatedTabs;
    private boolean mIsSwipingToDismiss;
    private boolean mShouldBlockAction;
    private int mDragFlags;
    private int mSelectedTabIndex = TabModel.INVALID_TAB_INDEX;
    private int mHoveredTabIndex = TabModel.INVALID_TAB_INDEX;
    private int mUnGroupTabIndex = TabModel.INVALID_TAB_INDEX;
    private int mCurrentActionState = ItemTouchHelper.ACTION_STATE_IDLE;
    private @Nullable TabGridItemLongPressOrchestrator mTabGridItemLongPressOrchestrator;
    private @Nullable OnDropOnArchivalMessageCardEventListener
            mOnDropOnArchivalMessageCardEventListener;
    private int mPreviousArchivedMessageCardIndex = TabModel.INVALID_TAB_INDEX;

    /**
     * @param context The activity context.
     * @param tabGroupCreationDialogManager The manager for showing a dialog on group creation.
     * @param tabListModel The property model of tab data to act on.
     * @param currentTabGroupModelFilterSupplier The supplier of the current {@link
     *     TabGroupModelFilter}. It should never return null.
     * @param tabClosedListener The listener to invoke when a tab is closed.
     * @param tabGridDialogHandler The interface for sending updates when using a tab grid dialog.
     * @param componentName The name of the component for metrics logging.
     * @param actionsOnAllRelatedTabs Whether to operate on related tabs.
     * @param mode The mode of the tab list.
     */
    public TabGridItemTouchHelperCallback(
            Context context,
            TabGroupCreationDialogManager tabGroupCreationDialogManager,
            TabListModel tabListModel,
            Supplier<TabGroupModelFilter> currentTabGroupModelFilterSupplier,
            TabActionListener tabClosedListener,
            TabGridDialogHandler tabGridDialogHandler,
            String componentName,
            boolean actionsOnAllRelatedTabs,
            @TabListMode int mode) {
        super(0, 0);
        mModel = tabListModel;
        mCurrentTabGroupModelFilterSupplier = currentTabGroupModelFilterSupplier;
        mTabClosedListener = tabClosedListener;
        mComponentName = componentName;
        mActionsOnAllRelatedTabs = actionsOnAllRelatedTabs;
        mTabGridDialogHandler = tabGridDialogHandler;
        mTabGroupCreationDialogManager = tabGroupCreationDialogManager;

        Resources resources = context.getResources();
        mLongPressDpCancelThreshold =
                resources.getDimensionPixelSize(R.dimen.long_press_cancel_threshold);

        // Square, since the threshold will be compared against a squared vector magnitude.
        int longPressDpThreshold =
                resources.getDimensionPixelSize(R.dimen.tab_list_editor_longpress_entry_threshold);
        mLongPressDpThresholdSquared = longPressDpThreshold * longPressDpThreshold;
    }

    /**
     * @param listener the handler for longpress actions.
     */
    void setOnLongPressTabItemEventListener(OnLongPressTabItemEventListener listener) {
        assert mTabGridItemLongPressOrchestrator == null;
        if (ChromeFeatureList.sTabGroupParityBottomSheetAndroid.isEnabled()) {
            setTabGridItemLongPressOrchestrator(
                    new TabGridItemLongPressOrchestrator(
                            mRecyclerViewSupplier,
                            mModel,
                            listener,
                            mLongPressDpCancelThreshold,
                            LONGPRESS_DURATION_MS));
        }
    }

    /**
     * @param listener the handler for dropping tabs on top of an archival message card.
     */
    void setOnDropOnArchivalMessageCardEventListener(
            OnDropOnArchivalMessageCardEventListener listener) {
        mOnDropOnArchivalMessageCardEventListener = listener;
    }

    /**
     * This method sets up parameters that are used by the {@link ItemTouchHelper} to make decisions
     * about user actions.
     *
     * @param swipeToDismissThreshold Defines the threshold that user needs to swipe in order to be
     *     considered as a remove operation.
     * @param mergeThreshold Defines the percentage threshold as a decimal of how much area of the
     *     two items need to be overlapped in order to be considered as a merge operation.
     */
    void setupCallback(
            float swipeToDismissThreshold, float mergeThreshold, float ungroupThreshold) {
        mSwipeToDismissThreshold = swipeToDismissThreshold;
        mMergeThreshold = mergeThreshold;
        mUngroupThreshold = ungroupThreshold;
        mDragFlags =
                ItemTouchHelper.START
                        | ItemTouchHelper.END
                        | ItemTouchHelper.UP
                        | ItemTouchHelper.DOWN;
    }

    boolean isMessageType(@Nullable RecyclerView.ViewHolder viewHolder) {
        if (viewHolder == null) return false;

        @UiType int type = viewHolder.getItemViewType();
        return type == UiType.MESSAGE
                || type == UiType.LARGE_MESSAGE
                || type == UiType.CUSTOM_MESSAGE;
    }

    boolean hasCollaboration(@Nullable RecyclerView.ViewHolder viewHolder) {
        if (viewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder simpleViewHolder) {
            PropertyModel model = simpleViewHolder.model;
            if (model.get(CARD_TYPE) == TAB) {
                @Nullable
                TabGroupColorViewProvider provider =
                        model.get(TabProperties.TAB_GROUP_COLOR_VIEW_PROVIDER);
                return provider != null && provider.hasCollaborationId();
            }
        }
        return false;
    }

    @Override
    public int getMovementFlags(RecyclerView recyclerView, RecyclerView.ViewHolder viewHolder) {
        final int dragFlags = isMessageType(viewHolder) ? 0 : mDragFlags;
        int swipeFlags = ItemTouchHelper.START | ItemTouchHelper.END;
        // The archived tabs message can't be dismissed.
        if (viewHolder.getItemViewType() == UiType.CUSTOM_MESSAGE) {
            SimpleRecyclerViewAdapter.ViewHolder simpleViewHolder =
                    (SimpleRecyclerViewAdapter.ViewHolder) viewHolder;
            if (simpleViewHolder.model.get(MESSAGE_TYPE) == MessageType.ARCHIVED_TABS_MESSAGE) {
                swipeFlags = 0;
            }
        }

        mRecyclerViewSupplier.set(recyclerView);
        return makeMovementFlags(dragFlags, swipeFlags);
    }

    @Override
    public boolean canDropOver(
            @NonNull RecyclerView recyclerView,
            @NonNull RecyclerView.ViewHolder current,
            @NonNull RecyclerView.ViewHolder target) {
        if (isArchivedMessageCard(current)) {
            return canDropOnArchivalMessage((SimpleRecyclerViewAdapter.ViewHolder) target);
        }
        if (target.getItemViewType() == TabProperties.UiType.MESSAGE
                || target.getItemViewType() == TabProperties.UiType.LARGE_MESSAGE
                || target.getItemViewType() == TabProperties.UiType.CUSTOM_MESSAGE) {
            return false;
        }
        return super.canDropOver(recyclerView, current, target);
    }

    @Override
    public boolean onMove(
            RecyclerView recyclerView,
            RecyclerView.ViewHolder fromViewHolder,
            RecyclerView.ViewHolder toViewHolder) {
        assert !(fromViewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder)
                || hasTabPropertiesModel(fromViewHolder);

        mSelectedTabIndex = toViewHolder.getAdapterPosition();
        if (mHoveredTabIndex != TabModel.INVALID_TAB_INDEX) {
            mModel.updateHoveredCardForHover(mHoveredTabIndex, false);
            mHoveredTabIndex = TabModel.INVALID_TAB_INDEX;
        }

        int currentTabId =
                ((SimpleRecyclerViewAdapter.ViewHolder) fromViewHolder)
                        .model.get(TabProperties.TAB_ID);

        if (isArchivedMessageCard(toViewHolder)) {
            return true;
        }

        int destinationTabId =
                ((SimpleRecyclerViewAdapter.ViewHolder) toViewHolder)
                        .model.get(TabProperties.TAB_ID);
        int distance = toViewHolder.getAdapterPosition() - fromViewHolder.getAdapterPosition();
        TabGroupModelFilter filter = mCurrentTabGroupModelFilterSupplier.get();
        TabModel tabModel = filter.getTabModel();
        if (!mActionsOnAllRelatedTabs) {
            int destinationIndex = tabModel.indexOf(tabModel.getTabById(destinationTabId));
            tabModel.moveTab(currentTabId, destinationIndex);
        } else {
            List<Tab> destinationTabGroup = getRelatedTabsForId(destinationTabId);
            int newIndex =
                    distance >= 0
                            ? TabGroupUtils.getLastTabModelIndexForList(
                                            tabModel, destinationTabGroup)
                            : TabGroupUtils.getFirstTabModelIndexForList(
                                    tabModel, destinationTabGroup);
            filter.moveRelatedTabs(currentTabId, newIndex);
        }
        RecordUserAction.record("TabGrid.Drag.Reordered." + mComponentName);
        mActionAttempted = true;
        return true;
    }

    @Override
    public void onSwiped(RecyclerView.ViewHolder viewHolder, int i) {
        assert viewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder;

        SimpleRecyclerViewAdapter.ViewHolder simpleViewHolder =
                (SimpleRecyclerViewAdapter.ViewHolder) viewHolder;

        if (simpleViewHolder.model.containsKey(TabProperties.TAB_ID)) {
            int tabId = simpleViewHolder.model.get(TabProperties.TAB_ID);
            mRecentlySwipedTabIdSupplier.set(tabId);
        }

        if (simpleViewHolder.model.get(CARD_TYPE) == TAB) {
            mTabClosedListener.run(
                    viewHolder.itemView,
                    simpleViewHolder.model.get(TabProperties.TAB_ID),
                    /* triggeringMotion= */ null);

            RecordUserAction.record("MobileStackViewSwipeCloseTab." + mComponentName);
        } else if (simpleViewHolder.model.get(CARD_TYPE) == MESSAGE) {
            // TODO(crbug.com/40099080): Have a caller instead of simulating the close click. And
            // write unit test to verify the caller is called.
            viewHolder.itemView.findViewById(R.id.close_button).performClick();
            // TODO(crbug.com/40099080): UserAction swipe to dismiss.
        }
        mActionAttempted = true;
    }

    @Override
    public void onSelectedChanged(RecyclerView.ViewHolder viewHolder, int actionState) {
        super.onSelectedChanged(viewHolder, actionState);
        @Nullable RecyclerView recyclerView = mRecyclerViewSupplier.get();
        if (isMessageType(viewHolder) || recyclerView == null) {
            return;
        }

        if (mTabGridItemLongPressOrchestrator != null && viewHolder != null) {
            mTabGridItemLongPressOrchestrator.onSelectedChanged(
                    viewHolder.getBindingAdapterPosition(), actionState);
        }

        if (actionState == ItemTouchHelper.ACTION_STATE_DRAG) {
            mSelectedTabIndex = viewHolder.getBindingAdapterPosition();
            mModel.updateSelectedCardForSelection(mSelectedTabIndex, true);
            RecordUserAction.record("TabGrid.Drag.Start." + mComponentName);
        } else if (actionState == ItemTouchHelper.ACTION_STATE_IDLE) {
            mIsSwipingToDismiss = false;

            RecyclerView.ViewHolder hoveredViewHolder =
                    recyclerView.findViewHolderForAdapterPosition(mHoveredTabIndex);
            RecyclerView.ViewHolder selectedViewHolder =
                    recyclerView.findViewHolderForAdapterPosition(mSelectedTabIndex);

            boolean shouldUpdate =
                    !(hoveredViewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder)
                            || hasTabPropertiesModel(hoveredViewHolder);

            if (wasHoveredOnArchivedMessageCard()
                    && mSelectedTabIndex != TabModel.INVALID_TAB_INDEX) {
                onDropOnArchivalMessageCard();
            } else if (mHoveredTabIndex != TabModel.INVALID_TAB_INDEX
                    && mActionsOnAllRelatedTabs
                    && !hasCollaboration(viewHolder)) {
                if (selectedViewHolder != null
                        && !recyclerView.isComputingLayout()
                        && shouldUpdate) {
                    View selectedItemView = selectedViewHolder.itemView;
                    onTabMergeToGroup(
                            mModel.getTabCardCountsBefore(mSelectedTabIndex),
                            mModel.getTabCardCountsBefore(mHoveredTabIndex));
                    recyclerView.getLayoutManager().removeView(selectedItemView);
                }
                mActionAttempted = true;
            } else {
                mModel.updateSelectedCardForSelection(mSelectedTabIndex, false);
            }
            if (mHoveredTabIndex != TabModel.INVALID_TAB_INDEX && shouldUpdate) {
                mModel.updateHoveredCardForHover(
                        mSelectedTabIndex > mHoveredTabIndex
                                ? mHoveredTabIndex
                                : mModel.getTabIndexBefore(mHoveredTabIndex),
                        false);
                mActionAttempted = true;
            }
            if (mUnGroupTabIndex != TabModel.INVALID_TAB_INDEX) {
                TabGroupModelFilter filter = mCurrentTabGroupModelFilterSupplier.get();
                RecyclerView.ViewHolder ungroupViewHolder =
                        recyclerView.findViewHolderForAdapterPosition(mUnGroupTabIndex);
                if (ungroupViewHolder != null && !recyclerView.isComputingLayout()) {
                    View ungroupItemView = ungroupViewHolder.itemView;
                    int tabId = mModel.get(mUnGroupTabIndex).model.get(TabProperties.TAB_ID);
                    @Nullable Tab tab = filter.getTabModel().getTabById(tabId);
                    if (tab != null) {
                        filter.getTabUngrouper()
                                .ungroupTabs(
                                        List.of(tab),
                                        /* trailing= */ true,
                                        /* allowDialog= */ true);
                    }
                    // Handle the case where the recyclerView is cleared out after ungrouping the
                    // last tab in group.
                    if (recyclerView.getAdapter().getItemCount() != 0) {
                        recyclerView.getLayoutManager().removeView(ungroupItemView);
                    }
                    RecordUserAction.record("TabGrid.Drag.RemoveFromGroup." + mComponentName);
                }
                mActionAttempted = true;
            }

            // There is a bug with ItemTouchHelper where on longpress, if the held tab is not
            // dragged (no movement occurs), then the gesture will not actually be consumed by the
            // ItemTouchHelper. This manifests as a MOTION_UP event being propagated to child view
            // click handlers and resulting in a real "click" occurring despite the action having
            // technically been consumed as a longpress by this class. The downstream click
            // handlers running can result in a tab being selected or closed in an unexpected manner
            // and due to a race condition between animations a phantom tab can even remain in the
            // UI (see crbug.com/1425336).
            //
            // To avoid this it is necessary for TabListMediator to attach an additional
            // OnItemTouchListener that resolves after the OnItemTouchListener attached by the
            // ItemTouchHelper that TabGridItemTouchHelperCallback is bound to. This additional
            // OnItemTouchListener will block the MOTION_UP event preventing the unintended action
            // from resolving.
            //
            // This block will not trigger if:
            //      a swipe was started but unfinished as mSelectedTabIndex may not be set.
            //      a swipe, move or group/ungroup happens.
            //      a tab is moved beyond a minimum distance from its original location.
            //
            // Otherwise, the unwanted click behaviour will be blocked.
            if (mSelectedTabIndex != TabModel.INVALID_TAB_INDEX
                    && mSelectedTabIndex < mModel.size()
                    && !mActionAttempted
                    && mModel.get(mSelectedTabIndex).model.get(CARD_TYPE) == TAB) {
                // If the child was ever dragged or swiped do not consume the next action, as the
                // longpress will resolve safely due to the listener intercepting the DRAG event
                // and negating any further action. However, if we just release the tab without
                // starting a swipe or drag then it is possible the longpress instead resolves as a
                // MOTION_UP click event leading to the problems described above.
                if (!mActionStarted) {
                    mShouldBlockAction = true;
                }
            }
            mHoveredTabIndex = TabModel.INVALID_TAB_INDEX;
            mSelectedTabIndex = TabModel.INVALID_TAB_INDEX;
            mUnGroupTabIndex = TabModel.INVALID_TAB_INDEX;
            if (mTabGridDialogHandler != null) {
                mTabGridDialogHandler.updateUngroupBarStatus(
                        TabGridDialogView.UngroupBarStatus.HIDE);
            }
        }
        mActionStarted = false;
        mActionAttempted = false;
    }

    private boolean hasTabPropertiesModel(RecyclerView.ViewHolder viewHolder) {
        return viewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder
                && ((SimpleRecyclerViewAdapter.ViewHolder) viewHolder).model.get(CARD_TYPE) == TAB;
    }

    @Override
    public void clearView(RecyclerView recyclerView, RecyclerView.ViewHolder viewHolder) {
        super.clearView(recyclerView, viewHolder);
        int prevActionState = mCurrentActionState;
        mCurrentActionState = ItemTouchHelper.ACTION_STATE_IDLE;
        if (prevActionState != ItemTouchHelper.ACTION_STATE_DRAG) return;
        // If this item view becomes stale after the dragging animation is finished, manually clean
        // it out. Post this call as otherwise there is an IllegalStateException. See:
        // crbug.com/361498419. When the post task is executed we need to ensure that the
        // state is still inconcistent i.e. the view holder is a child of the recycler view,
        // but the adapter doesn't contain the item. If so we should remove the view. Previous
        // attempts to fix this also checked for matching item positions in the adapter, but
        // this led to phantom items in the recycler view due to the position the item view
        // thought it had pre-post being inconsistent with the state after the post.
        // TODO(crbug.com/40641179): Figure out why the deleting signal is not properly sent when
        // item is being dragged.
        Runnable removeViewHolderRunnable =
                () -> {
                    if (viewHolder.itemView.getParent() == null
                            || recyclerView.getChildCount() == 0) {
                        return;
                    }

                    @Nullable var adapter = recyclerView.getAdapter();
                    if (adapter == null) return;

                    @Nullable var layoutManager = recyclerView.getLayoutManager();
                    if (layoutManager != null && adapter.getItemCount() == 0) {
                        layoutManager.removeView(viewHolder.itemView);
                    }
                };
        recyclerView.post(removeViewHolderRunnable);
        if (mTabGridItemLongPressOrchestrator != null) {
            mTabGridItemLongPressOrchestrator.cancel();
        }
    }

    @Override
    public void onChildDraw(
            Canvas c,
            RecyclerView recyclerView,
            RecyclerView.ViewHolder viewHolder,
            float dX,
            float dY,
            int actionState,
            boolean isCurrentlyActive) {
        super.onChildDraw(c, recyclerView, viewHolder, dX, dY, actionState, isCurrentlyActive);
        if (Math.abs(dX) > 0 || Math.abs(dY) > 0) {
            mActionStarted = true;
        }
        if (actionState == ItemTouchHelper.ACTION_STATE_SWIPE) {
            float alpha = Math.max(0.2f, 1f - 0.8f * Math.abs(dX) / mSwipeToDismissThreshold);

            assert viewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder;

            SimpleRecyclerViewAdapter.ViewHolder simpleViewHolder =
                    (SimpleRecyclerViewAdapter.ViewHolder) viewHolder;

            @Nullable PropertyModel viewHolderModel = simpleViewHolder.model;
            if (viewHolderModel == null) return;

            @Nullable PropertyModel cardModel = null;
            if (viewHolderModel.get(CARD_TYPE) == TAB) {
                cardModel =
                        mModel.getModelFromTabId(viewHolderModel.get(TabProperties.TAB_ID));
            } else if (viewHolderModel.get(CARD_TYPE) == MESSAGE) {
                int index =
                        mModel.lastIndexForMessageItemFromType(
                                viewHolderModel.get(MESSAGE_TYPE));
                if (index == TabModel.INVALID_TAB_INDEX) return;

                cardModel = mModel.get(index).model;
            }

            if (cardModel == null) return;

            cardModel.set(TabListModel.CardProperties.CARD_ALPHA, alpha);
            boolean isOverSwipeThreshold = Math.abs(dX) >= mSwipeToDismissThreshold;
            if (isOverSwipeThreshold && !mIsSwipingToDismiss) {
                viewHolder.itemView.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            }
            mIsSwipingToDismiss = isOverSwipeThreshold;
            return;
        }

        float magnitudeSquared = calcMagnitudeSquared(dX, dY);
        if (magnitudeSquared > mLongPressDpThresholdSquared) {
            mActionAttempted = true;
        }
        if (mTabGridItemLongPressOrchestrator != null) {
            mTabGridItemLongPressOrchestrator.processChildDisplacement(magnitudeSquared);
        }

        mCurrentActionState = actionState;
        if (actionState == ItemTouchHelper.ACTION_STATE_DRAG && mActionsOnAllRelatedTabs) {
            int prev_hovered = mHoveredTabIndex;
            mHoveredTabIndex =
                    TabListRecyclerView.getHoveredCardIndex(
                            recyclerView, viewHolder.itemView, dX, dY, mMergeThreshold);

            RecyclerView.ViewHolder hoveredViewHolder =
                    recyclerView.findViewHolderForAdapterPosition(mHoveredTabIndex);

            handleHoverForArchiveMessage(recyclerView);

            if (hasTabPropertiesModel(hoveredViewHolder) && !hasCollaboration(viewHolder)) {
                mModel.updateHoveredCardForHover(mHoveredTabIndex, true);
            } else {
                mHoveredTabIndex = TabModel.INVALID_TAB_INDEX;
            }
            if (prev_hovered != mHoveredTabIndex) {
                mModel.updateHoveredCardForHover(prev_hovered, false);
            }
        } else if (actionState == ItemTouchHelper.ACTION_STATE_DRAG
                && mTabGridDialogHandler != null) {
            int itemMiddle =
                    Math.floorDiv(
                            viewHolder.itemView.getBottom() + viewHolder.itemView.getTop(), 2);
            boolean isHoveredOnUngroupBar =
                    itemMiddle + dY > recyclerView.getBottom() - mUngroupThreshold;
            if (mSelectedTabIndex == TabModel.INVALID_TAB_INDEX) return;
            mUnGroupTabIndex =
                    isHoveredOnUngroupBar
                            ? viewHolder.getAdapterPosition()
                            : TabModel.INVALID_TAB_INDEX;
            mTabGridDialogHandler.updateUngroupBarStatus(
                    isHoveredOnUngroupBar
                            ? TabGridDialogView.UngroupBarStatus.HOVERED
                            : (mSelectedTabIndex == TabModel.INVALID_TAB_INDEX
                                    ? TabGridDialogView.UngroupBarStatus.HIDE
                                    : TabGridDialogView.UngroupBarStatus.SHOW));
        }
    }

    private void handleHoverForArchiveMessage(RecyclerView recyclerView) {
        if (!ChromeFeatureList.sTabArchivalDragDropAndroid.isEnabled()) return;

        SimpleRecyclerViewAdapter.ViewHolder hoveredViewHolder =
                (SimpleRecyclerViewAdapter.ViewHolder)
                        recyclerView.findViewHolderForAdapterPosition(mHoveredTabIndex);
        SimpleRecyclerViewAdapter.ViewHolder selectedViewHolder =
                (SimpleRecyclerViewAdapter.ViewHolder)
                        recyclerView.findViewHolderForAdapterPosition(mSelectedTabIndex);

        boolean isArchivedMessageCard = isArchivedMessageCard(hoveredViewHolder);
        if (isArchivedMessageCard && !canDropOnArchivalMessage(selectedViewHolder)) return;

        // Remove the hovered animation on the archived message card.
        boolean hoveredOnArchivedMessageCard = wasHoveredOnArchivedMessageCard();
        if (!isArchivedMessageCard && hoveredOnArchivedMessageCard) {
            mModel.updateHoveredCardForHover(mPreviousArchivedMessageCardIndex, false);
            mPreviousArchivedMessageCardIndex = TabModel.INVALID_TAB_INDEX;
        } else if (isArchivedMessageCard && !hoveredOnArchivedMessageCard) {
            mModel.updateHoveredCardForHover(mHoveredTabIndex, true);
            mPreviousArchivedMessageCardIndex = mHoveredTabIndex;
        }
    }

    private boolean canDropOnArchivalMessage(
            @Nullable SimpleRecyclerViewAdapter.ViewHolder tabToBeArchived) {
        if (tabToBeArchived == null) return false;

        PropertyModel model = tabToBeArchived.model;
        if (!model.containsKey(TabProperties.TAB_ID)) return false;

        @TabId int tabId = model.get(TabProperties.TAB_ID);
        TabGroupModelFilter tabGroupModelFilter = mCurrentTabGroupModelFilterSupplier.get();

        Tab tab = tabGroupModelFilter.getTabModel().getTabById(tabId);
        if (tab == null) return false;

        Token groupId = tab.getTabGroupId();

        // Tab groups can only be archived when this feature is enabled.
        if (groupId != null
                && !ChromeFeatureList.sAndroidTabDeclutterArchiveTabGroups.isEnabled()) {
            return false;
        }

        // Check if the tab is in a shared group.
        return groupId == null || !hasCollaboration(tabToBeArchived);
    }

    private void onDropOnArchivalMessageCard() {
        RecyclerView recyclerView = mRecyclerViewSupplier.get();
        SimpleRecyclerViewAdapter.ViewHolder selectedViewHolder =
                (SimpleRecyclerViewAdapter.ViewHolder)
                        recyclerView.findViewHolderForAdapterPosition(mSelectedTabIndex);
        if (selectedViewHolder == null) return;

        PropertyModel selectedModel = selectedViewHolder.model;
        if (selectedModel.containsKey(TabProperties.TAB_ID)
                && canDropOnArchivalMessage(selectedViewHolder)
                && mOnDropOnArchivalMessageCardEventListener != null) {
            RecyclerView.ViewHolder archivalMessageViewHolder =
                    recyclerView.findViewHolderForAdapterPosition(
                            mPreviousArchivedMessageCardIndex);
            if (isArchivedMessageCard(archivalMessageViewHolder)) {
                mModel.updateHoveredCardForHover(mPreviousArchivedMessageCardIndex, false);
            }
            mPreviousArchivedMessageCardIndex = TabModel.INVALID_TAB_INDEX;

            @TabId int tabId = selectedModel.get(TabProperties.TAB_ID);
            mOnDropOnArchivalMessageCardEventListener.onDropTab(tabId);

            View selectedItemView = selectedViewHolder.itemView;
            recyclerView.getLayoutManager().removeView(selectedItemView);
        }
    }

    private boolean wasHoveredOnArchivedMessageCard() {
        return mPreviousArchivedMessageCardIndex != TabModel.INVALID_TAB_INDEX
                && isArchivedMessageCard(mModel.get(mPreviousArchivedMessageCardIndex).model);
    }

    private static float calcMagnitudeSquared(float dX, float dY) {
        return dX * dX + dY * dY;
    }

    @Override
    public float getSwipeThreshold(RecyclerView.ViewHolder viewHolder) {
        @Nullable RecyclerView recyclerView = mRecyclerViewSupplier.get();
        if (recyclerView != null) {
            return mSwipeToDismissThreshold / recyclerView.getWidth();
        }
        return 0.f;
    }

    private List<Tab> getRelatedTabsForId(int id) {
        return mCurrentTabGroupModelFilterSupplier.get().getRelatedTabList(id);
    }

    private void onTabMergeToGroup(int selectedCardIndex, int hoveredCardIndex) {
        TabGroupModelFilter filter = mCurrentTabGroupModelFilterSupplier.get();
        Tab selectedCard = filter.getRepresentativeTabAt(selectedCardIndex);
        Tab hoveredCard = filter.getRepresentativeTabAt(hoveredCardIndex);
        if (selectedCard == null) return;
        if (hoveredCard == null) return;
        boolean willMergingCreateNewGroup =
                filter.willMergingCreateNewGroup(List.of(selectedCard, hoveredCard));
        filter.mergeTabsToGroup(selectedCard.getId(), hoveredCard.getId());

        if (willMergingCreateNewGroup) {
            mTabGroupCreationDialogManager.showDialog(hoveredCard.getTabGroupId(), filter);
        }

        // If user has used drop-to-merge, send a signal to disable
        // FeatureConstants.TAB_GROUPS_DRAG_AND_DROP_FEATURE.
        final Tracker tracker =
                TrackerFactory.getTrackerForProfile(
                        mCurrentTabGroupModelFilterSupplier.get().getTabModel().getProfile());
        tracker.notifyEvent(EventConstants.TAB_DRAG_AND_DROP_TO_GROUP);
    }

    /*
     * Returns whether or not a touch action should be blocked on an item accessed from
     * the TabListCoordinator. The bit is always defaulted to false and reset to that
     * value after shouldBlockAction() is called. It is used primarily to prevent a
     * secondary touch event from occurring on a longpress event on a tab grid item.
     */
    boolean shouldBlockAction() {
        boolean out = mShouldBlockAction;
        mShouldBlockAction = false;
        return out;
    }

    void setActionsOnAllRelatedTabsForTesting(boolean flag) {
        var oldValue = mActionsOnAllRelatedTabs;
        mActionsOnAllRelatedTabs = flag;
        ResettersForTesting.register(() -> mActionsOnAllRelatedTabs = oldValue);
    }

    void setHoveredTabIndexForTesting(int index) {
        var oldValue = mHoveredTabIndex;
        mHoveredTabIndex = index;
        ResettersForTesting.register(() -> mHoveredTabIndex = oldValue);
    }

    void setSelectedTabIndexForTesting(int index) {
        var oldValue = mSelectedTabIndex;
        mSelectedTabIndex = index;
        ResettersForTesting.register(() -> mSelectedTabIndex = oldValue);
    }

    void setUnGroupTabIndexForTesting(int index) {
        var oldValue = mUnGroupTabIndex;
        mUnGroupTabIndex = index;
        ResettersForTesting.register(() -> mUnGroupTabIndex = oldValue);
    }

    void setCurrentActionStateForTesting(int actionState) {
        var oldValue = mCurrentActionState;
        mCurrentActionState = actionState;
        ResettersForTesting.register(() -> mCurrentActionState = oldValue);
    }

    boolean hasDragFlagForTesting(RecyclerView recyclerView, RecyclerView.ViewHolder viewHolder) {
        int flags = getMovementFlags(recyclerView, viewHolder);
        return (flags >> 16) != 0;
    }

    /** Provides the tab ID for the most recently swiped tab. */
    ObservableSupplier<Integer> getRecentlySwipedTabIdSupplier() {
        return mRecentlySwipedTabIdSupplier;
    }

    @VisibleForTesting
    boolean hasSwipeFlag(RecyclerView recyclerView, RecyclerView.ViewHolder viewHolder) {
        int flags = getMovementFlags(recyclerView, viewHolder);
        return ((flags >> 8) & 0xFF) != 0;
    }

    @VisibleForTesting
    void setTabGridItemLongPressOrchestrator(TabGridItemLongPressOrchestrator orchestrator) {
        mTabGridItemLongPressOrchestrator = orchestrator;
    }

    private boolean isArchivedMessageCard(@Nullable RecyclerView.ViewHolder cardViewHolder) {
        if (cardViewHolder instanceof SimpleRecyclerViewAdapter.ViewHolder simpleViewHolder) {
            return isArchivedMessageCard(simpleViewHolder.model);
        }
        return false;
    }

    private boolean isArchivedMessageCard(PropertyModel model) {
        return model.get(CARD_TYPE) == MESSAGE
                && model.get(MESSAGE_TYPE) == MessageType.ARCHIVED_TABS_MESSAGE;
    }
}
