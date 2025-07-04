// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.ALL_KEYS;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.BLOCK_TOUCH_INPUT;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.BROWSER_CONTROLS_STATE_PROVIDER;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.FOCUS_TAB_INDEX_FOR_ACCESSIBILITY;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.INITIAL_SCROLL_INDEX;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.MODE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.SUPPRESS_ACCESSIBILITY;

import android.view.View;
import android.view.ViewGroup;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.Callback;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.MockTab;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabClosingSource;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tasks.tab_management.TabGridDialogMediator.DialogController;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorCoordinator.TabListEditorController;
import org.chromium.chrome.browser.tasks.tab_management.TabListMediator.TabActionListener;
import org.chromium.chrome.browser.tasks.tab_management.TabSwitcherPaneMediator.TabIndexLookup;
import org.chromium.chrome.test.util.browser.tabmodel.MockTabModel;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController.StateChangeReason;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetObserver;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler.BackPressResult;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.List;

/** Unit tests for {@link TabSwitcherPaneMediator}. */
@RunWith(BaseRobolectricTestRunner.class)
public class TabSwitcherPaneMediatorUnitTest {
    private static final int UNGROUPED_TAB_ID = 1;
    private static final int GROUPED_TAB_1_ID = 2;
    private static final int GROUPED_TAB_2_ID = 3;

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TabSwitcherResetHandler mResetHandler;
    @Mock private DialogController mTabGridDialogController;
    @Mock private Runnable mOnTabSwitcherShownRunnable;
    @Mock private Profile mProfile;
    @Mock private TabGroupModelFilter mTabGroupModelFilter;
    @Mock private TabListEditorController mTabListEditorController;
    @Mock private ViewGroup mContainerView;
    @Mock private View mCustomView;
    @Mock private Runnable mCustomViewBackPressRunnable;
    @Mock private Callback<Integer> mOnTabClickedCallback;
    @Mock private TabIndexLookup mTabIndexLookup;
    @Mock private BottomSheetController mBottomSheetController;

    @Captor private ArgumentCaptor<TabModelObserver> mTabModelObserverCaptor;
    @Captor private ArgumentCaptor<BottomSheetObserver> mBottomSheetObserverCaptor;

    private final ObservableSupplierImpl<TabGroupModelFilter> mTabGroupModelFilterSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mDialogBackPressChangedSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mShowingOrAnimationSupplier =
            new ObservableSupplierImpl<>(false);
    private final ObservableSupplierImpl<Boolean> mIsVisibleSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIsAnimatingSupplier =
            new ObservableSupplierImpl<>(false);
    private final ObservableSupplierImpl<TabListEditorController> mTabListEditorControllerSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mTabListEditorBackPressChangedSupplier =
            new ObservableSupplierImpl<>();

    private LazyOneshotSupplier<DialogController> mTabGridDialogControllerSupplier;
    private PropertyModel mModel;
    private MockTabModel mTabModel;
    private Tab mUngroupedTab;
    private Tab mGroupedTab1;
    private Tab mGroupedTab2;
    private List<Tab> mTabList;
    private TabSwitcherPaneMediator mMediator;

    @Before
    public void setUp() {
        when(mProfile.isOffTheRecord()).thenReturn(false);
        when(mTabIndexLookup.getNthTabIndexInModel(anyInt())).thenAnswer(i -> i.getArguments()[0]);
        mTabModel = new MockTabModel(mProfile, null);
        mTabModel.addTab(
                new MockTab(UNGROUPED_TAB_ID, mProfile, TabLaunchType.FROM_CHROME_UI),
                /* index= */ 0,
                TabLaunchType.FROM_CHROME_UI,
                TabCreationState.LIVE_IN_FOREGROUND);
        mTabModel.addTab(GROUPED_TAB_1_ID);
        mTabModel.addTab(GROUPED_TAB_2_ID);
        mUngroupedTab = mTabModel.getTabAt(0);
        mGroupedTab1 = mTabModel.getTabAt(1);
        mGroupedTab2 = mTabModel.getTabAt(2);
        mUngroupedTab.setRootId(UNGROUPED_TAB_ID);
        mGroupedTab1.setRootId(GROUPED_TAB_1_ID);
        mGroupedTab2.setRootId(GROUPED_TAB_1_ID);
        mTabList = List.of(mUngroupedTab, mGroupedTab1);
        when(mTabGroupModelFilter.getRepresentativeTabList()).thenReturn(mTabList);
        when(mTabGroupModelFilter.getRelatedTabList(UNGROUPED_TAB_ID))
                .thenReturn(List.of(mUngroupedTab));
        when(mTabGroupModelFilter.getRelatedTabList(GROUPED_TAB_1_ID))
                .thenReturn(List.of(mGroupedTab1, mGroupedTab2));
        when(mTabGroupModelFilter.isTabInTabGroup(mUngroupedTab)).thenReturn(false);
        when(mTabGroupModelFilter.isTabInTabGroup(mGroupedTab1)).thenReturn(true);
        when(mTabGroupModelFilter.isTabInTabGroup(mGroupedTab2)).thenReturn(true);
        when(mTabGroupModelFilter.getTabModel()).thenReturn(mTabModel);
        when(mTabGroupModelFilter.representativeIndexOf(mUngroupedTab)).thenReturn(0);
        when(mTabGroupModelFilter.representativeIndexOf(mGroupedTab1)).thenReturn(1);
        when(mTabGroupModelFilter.representativeIndexOf(mGroupedTab2)).thenReturn(1);

        when(mTabGridDialogController.getHandleBackPressChangedSupplier())
                .thenReturn(mDialogBackPressChangedSupplier);
        when(mTabGridDialogController.getShowingOrAnimationSupplier())
                .thenReturn(mShowingOrAnimationSupplier);
        when(mTabListEditorController.getHandleBackPressChangedSupplier())
                .thenReturn(mTabListEditorBackPressChangedSupplier);

        when(mTabGridDialogController.isVisible()).thenReturn(false);
        when(mTabListEditorController.isVisible()).thenReturn(false);

        mModel =
                new PropertyModel.Builder(ALL_KEYS)
                        .with(BROWSER_CONTROLS_STATE_PROVIDER, null)
                        .with(MODE, TabListMode.GRID)
                        .build();
        mTabGridDialogControllerSupplier = LazyOneshotSupplier.fromValue(mTabGridDialogController);
        mMediator =
                new TabSwitcherPaneMediator(
                        mResetHandler,
                        mTabGroupModelFilterSupplier,
                        mTabGridDialogControllerSupplier,
                        mModel,
                        mContainerView,
                        mOnTabSwitcherShownRunnable,
                        mIsVisibleSupplier,
                        mIsAnimatingSupplier,
                        mOnTabClickedCallback,
                        mTabIndexLookup,
                        mBottomSheetController);

        assertTrue(mTabGroupModelFilterSupplier.hasObservers());
        assertTrue(mIsVisibleSupplier.hasObservers());
        assertTrue(mIsAnimatingSupplier.hasObservers());

        mTabGroupModelFilterSupplier.set(mTabGroupModelFilter);
        verify(mTabGroupModelFilter).addObserver(mTabModelObserverCaptor.capture());

        mMediator.setTabListEditorControllerSupplier(mTabListEditorControllerSupplier);
        assertTrue(mTabListEditorControllerSupplier.hasObservers());
        mTabListEditorControllerSupplier.set(mTabListEditorController);

        mTabGridDialogControllerSupplier.get();
        ShadowLooper.runUiThreadTasks();
        assertTrue(mDialogBackPressChangedSupplier.hasObservers());

        verify(mOnTabSwitcherShownRunnable, never()).run();
        mIsVisibleSupplier.set(true);
        mIsAnimatingSupplier.set(false);
        verify(mOnTabSwitcherShownRunnable).run();
    }

    @After
    public void tearDown() {
        mMediator.destroy();

        verify(mTabGroupModelFilter, atLeastOnce())
                .removeObserver(mTabModelObserverCaptor.getValue());
        verify(mTabGridDialogController, atLeastOnce()).hideDialog(false);

        assertFalse(mTabGroupModelFilterSupplier.hasObservers());
        assertFalse(mIsVisibleSupplier.hasObservers());
        assertFalse(mIsAnimatingSupplier.hasObservers());
        assertFalse(mDialogBackPressChangedSupplier.hasObservers());
        assertFalse(mTabListEditorControllerSupplier.hasObservers());
        assertFalse(mTabListEditorBackPressChangedSupplier.hasObservers());
    }

    @Test
    public void testTabModelObserver() {
        // This observer is only used to update the back press state.
        TabModelObserver observer = mTabModelObserverCaptor.getValue();

        // Dialog visibility supplier is updated when back press internal state changes. Use this as
        // a proxy to detect that back press state updated.
        ObservableSupplier<Boolean> dialogVisibilitySupplier =
                mMediator.getIsDialogVisibleSupplier();
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        observer.tabClosureUndone(null);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(false);
        observer.tabPendingClosure(null, TabClosingSource.UNKNOWN);
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        observer.onFinishingTabClosure(null, TabClosingSource.UNKNOWN);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(false);
        observer.tabRemoved(null);
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        observer.multipleTabsPendingClosure(null, false);
        assertTrue(dialogVisibilitySupplier.get());
    }

    @Test
    public void testLateTabGroupModelFilterWhileVisible() {
        when(mTabListEditorController.isVisible()).thenReturn(true);
        // Reset to simulate the UI is shown with no tab model filter set.
        mIsVisibleSupplier.set(false);
        verify(mTabGridDialogController).hideDialog(false);
        verify(mTabListEditorController).hide();
        when(mTabListEditorController.isVisible()).thenReturn(false);
        mTabGroupModelFilterSupplier.set(null);
        verify(mTabGroupModelFilter, times(1)).addObserver(mTabModelObserverCaptor.capture());

        mMediator.destroy();

        mMediator =
                new TabSwitcherPaneMediator(
                        mResetHandler,
                        mTabGroupModelFilterSupplier,
                        mTabGridDialogControllerSupplier,
                        mModel,
                        mContainerView,
                        mOnTabSwitcherShownRunnable,
                        mIsVisibleSupplier,
                        mIsAnimatingSupplier,
                        mOnTabClickedCallback,
                        mTabIndexLookup,
                        mBottomSheetController);
        ShadowLooper.runUiThreadTasks();

        mIsVisibleSupplier.set(true);

        // When the filter is set we need to show tabs when visible if the restore already finished.
        mTabGroupModelFilterSupplier.set(mTabGroupModelFilter);
        verify(mTabGroupModelFilter, times(2)).addObserver(mTabModelObserverCaptor.capture());
        verify(mResetHandler).resetWithListOfTabs(mTabList);
    }

    @Test
    public void testTabModelObserverOnRestore() {
        TabModelObserver observer = mTabModelObserverCaptor.getValue();

        observer.restoreCompleted();
        verify(mResetHandler).resetWithListOfTabs(mTabList);
    }

    @Test
    public void testIsDialogVisibleSupplier() {
        ObservableSupplier<Boolean> dialogVisibilitySupplier =
                mMediator.getIsDialogVisibleSupplier();
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        mTabListEditorBackPressChangedSupplier.set(true);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabListEditorController.isVisible()).thenReturn(false);
        mTabListEditorBackPressChangedSupplier.set(false);
        assertFalse(dialogVisibilitySupplier.get());

        when(mTabGridDialogController.isVisible()).thenReturn(true);
        mDialogBackPressChangedSupplier.set(true);
        assertTrue(dialogVisibilitySupplier.get());

        when(mTabGridDialogController.isVisible()).thenReturn(false);
        mDialogBackPressChangedSupplier.set(false);
        assertFalse(dialogVisibilitySupplier.get());
    }

    @Test
    public void testRequestAccessibilityFocusOnCurrentTab() {
        int index = 5;
        when(mTabGroupModelFilter.getCurrentRepresentativeTabIndex()).thenReturn(index);
        mMediator.requestAccessibilityFocusOnCurrentTab();

        assertEquals(index, mModel.get(FOCUS_TAB_INDEX_FOR_ACCESSIBILITY).intValue());
    }

    @Test
    public void testBackPress() {
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.FAILURE, mMediator.handleBackPress());

        when(mTabListEditorController.isVisible()).thenReturn(true);
        when(mTabListEditorController.handleBackPressed()).thenReturn(true);
        mTabListEditorBackPressChangedSupplier.set(true);
        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mTabListEditorController, times(2)).handleBackPressed();
        when(mTabListEditorController.isVisible()).thenReturn(false);
        when(mTabListEditorController.handleBackPressed()).thenReturn(false);
        mTabListEditorBackPressChangedSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());

        mIsAnimatingSupplier.set(true);
        verify(mTabGridDialogController).hideDialog(true);
        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        mIsAnimatingSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());

        when(mTabGridDialogController.isVisible()).thenReturn(true);
        when(mTabGridDialogController.handleBackPressed()).thenReturn(true);
        mDialogBackPressChangedSupplier.set(true);
        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mTabGridDialogController, times(2)).handleBackPressed();
        when(mTabGridDialogController.isVisible()).thenReturn(false);
        when(mTabGridDialogController.handleBackPressed()).thenReturn(false);
        mDialogBackPressChangedSupplier.set(false);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testOpenTabGridDialog() {
        TabActionListener listener = mMediator.openTabGridDialog(mGroupedTab1);
        assertNotNull(listener);
        listener.run(mCustomView, mGroupedTab1.getId(), /* triggeringMotion= */ null);

        verify(mTabGridDialogController).resetWithListOfTabs(List.of(mGroupedTab1, mGroupedTab2));
    }

    @Test
    public void testOpenTabGridDialog_SingleTab() {
        assertNull(mMediator.openTabGridDialog(mUngroupedTab));
    }

    @Test
    public void testOpenTabGridDialog_SingleTabGroup() {
        when(mTabGroupModelFilter.isTabInTabGroup(mUngroupedTab)).thenReturn(true);

        TabActionListener listener = mMediator.openTabGridDialog(mUngroupedTab);
        assertNotNull(listener);
        listener.run(mCustomView, mUngroupedTab.getId(), /* triggeringMotion= */ null);
        verify(mTabGridDialogController).resetWithListOfTabs(List.of(mUngroupedTab));
    }

    @Test
    public void testOnTabSelecting() {
        mMediator.onTabSelecting(mUngroupedTab.getId(), /* fromActionButton= */ true);
        verify(mOnTabClickedCallback).onResult(UNGROUPED_TAB_ID);
    }

    @Test
    public void testScrollToTab() {
        int index = 5;
        when(mTabGroupModelFilter.getCurrentRepresentativeTabIndex()).thenReturn(index);

        mMediator.setInitialScrollIndexOffset();
        assertEquals(index, mModel.get(INITIAL_SCROLL_INDEX).intValue());

        index = 3;
        mMediator.scrollToTab(index);
        assertEquals(index, mModel.get(INITIAL_SCROLL_INDEX).intValue());

        mMediator.scrollToTabById(GROUPED_TAB_2_ID);
        assertEquals(1, mModel.get(INITIAL_SCROLL_INDEX).intValue());

        int overrideIndex = 7;
        when(mTabIndexLookup.getNthTabIndexInModel(anyInt())).thenReturn(overrideIndex);

        mMediator.setInitialScrollIndexOffset();
        assertEquals(overrideIndex, mModel.get(INITIAL_SCROLL_INDEX).intValue());

        mMediator.scrollToTab(index);
        assertEquals(index, mModel.get(INITIAL_SCROLL_INDEX).intValue());

        mMediator.scrollToTabById(GROUPED_TAB_2_ID);
        assertEquals(overrideIndex, mModel.get(INITIAL_SCROLL_INDEX).intValue());
    }

    @Test
    public void testCustomViewWithClearTabList() {
        when(mTabListEditorController.isVisible()).thenReturn(true);

        mMediator.addCustomView(
                mCustomView, mCustomViewBackPressRunnable, /* clearTabList= */ true);
        verify(mResetHandler).resetWithListOfTabs(null);
        verify(mContainerView).addView(mCustomView);
        verify(mTabListEditorController).hide();
        when(mTabListEditorController.isVisible()).thenReturn(false);

        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mCustomViewBackPressRunnable).run();

        mMediator.removeCustomView(mCustomView);
        verify(mContainerView).removeView(mCustomView);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testCustomViewWithoutClearTabList() {
        when(mTabGridDialogController.isVisible()).thenReturn(true);

        mMediator.addCustomView(
                mCustomView, mCustomViewBackPressRunnable, /* clearTabList= */ false);
        verify(mResetHandler, never()).resetWithListOfTabs(null);
        verify(mContainerView).addView(mCustomView);
        verify(mTabGridDialogController).hideDialog(false);
        when(mTabGridDialogController.isVisible()).thenReturn(false);

        assertTrue(mMediator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mMediator.handleBackPress());
        verify(mCustomViewBackPressRunnable).run();

        mMediator.removeCustomView(mCustomView);
        verify(mContainerView).removeView(mCustomView);
        assertFalse(mMediator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testBlockTouchInput() {
        assertFalse(mModel.get(BLOCK_TOUCH_INPUT));
        mShowingOrAnimationSupplier.set(true);
        assertTrue(mModel.get(BLOCK_TOUCH_INPUT));
        mShowingOrAnimationSupplier.set(false);
        assertFalse(mModel.get(BLOCK_TOUCH_INPUT));
    }

    @Test
    public void testSuppressAccessibility() {
        assertFalse(mModel.get(SUPPRESS_ACCESSIBILITY));

        verify(mBottomSheetController).addObserver(mBottomSheetObserverCaptor.capture());
        mBottomSheetObserverCaptor.getValue().onSheetOpened(StateChangeReason.NONE);
        assertTrue(mModel.get(SUPPRESS_ACCESSIBILITY));

        mBottomSheetObserverCaptor.getValue().onSheetClosed(StateChangeReason.NONE);
        assertFalse(mModel.get(SUPPRESS_ACCESSIBILITY));
    }
}
