// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static androidx.test.espresso.matcher.ViewMatchers.assertThat;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isA;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import static org.chromium.components.data_sharing.SharedGroupTestHelper.COLLABORATION_ID1;
import static org.chromium.components.data_sharing.SharedGroupTestHelper.GROUP_MEMBER1;
import static org.chromium.components.data_sharing.SharedGroupTestHelper.GROUP_MEMBER2;

import android.content.res.ColorStateList;
import android.graphics.Color;
import android.view.View;

import androidx.annotation.Nullable;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.InOrder;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.LooperMode;

import org.chromium.base.Callback;
import org.chromium.base.Token;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Features.DisableFeatures;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.chrome.browser.collaboration.CollaborationServiceFactory;
import org.chromium.chrome.browser.data_sharing.DataSharingServiceFactory;
import org.chromium.chrome.browser.data_sharing.ui.shared_image_tiles.SharedImageTilesConfig;
import org.chromium.chrome.browser.data_sharing.ui.shared_image_tiles.SharedImageTilesCoordinator;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutStateProvider.LayoutStateObserver;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab_group_sync.TabGroupSyncFeatures;
import org.chromium.chrome.browser.tab_group_sync.TabGroupSyncFeaturesJni;
import org.chromium.chrome.browser.tab_group_sync.TabGroupSyncServiceFactory;
import org.chromium.chrome.browser.tab_ui.TabContentManager;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilterObserver;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilterProvider;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorImpl;
import org.chromium.chrome.browser.tabmodel.TabUiUnitTestUtils;
import org.chromium.chrome.browser.theme.ThemeColorProvider;
import org.chromium.chrome.browser.theme.ThemeColorProvider.ThemeColorObserver;
import org.chromium.chrome.browser.theme.ThemeColorProvider.TintObserver;
import org.chromium.chrome.browser.toolbar.bottom.BottomControlsCoordinator.BottomControlsVisibilityController;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.components.collaboration.CollaborationService;
import org.chromium.components.collaboration.ServiceStatus;
import org.chromium.components.data_sharing.DataSharingService;
import org.chromium.components.data_sharing.DataSharingUIDelegate;
import org.chromium.components.data_sharing.SharedGroupTestHelper;
import org.chromium.components.tab_group_sync.LocalTabGroupId;
import org.chromium.components.tab_group_sync.SavedTabGroup;
import org.chromium.components.tab_group_sync.TabGroupSyncService;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Tests for {@link TabGroupUiMediator}. */
@SuppressWarnings({"ResultOfMethodCallIgnored", "ArraysAsListWithZeroOrOneArgument", "unchecked"})
@RunWith(BaseRobolectricTestRunner.class)
@LooperMode(LooperMode.Mode.LEGACY)
@EnableFeatures(ChromeFeatureList.DATA_SHARING)
public class TabGroupUiMediatorUnitTest {
    private static final int TAB1_ID = 456;
    private static final int TAB2_ID = 789;
    private static final int TAB3_ID = 123;
    private static final int TAB4_ID = 357;
    private static final int POSITION1 = 0;
    private static final int POSITION2 = 1;
    private static final int POSITION3 = 2;
    private static final int TAB1_ROOT_ID = TAB1_ID;
    private static final int TAB2_ROOT_ID = TAB2_ID;
    private static final int TAB3_ROOT_ID = TAB2_ID;
    private static final String GROUP_TITLE = "My Group";
    private static final Token TAB2_GROUP_ID = new Token(1L, 2L);

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Profile mProfile;
    @Mock private TabGroupSyncFeatures.Natives mTabGroupSyncFeaturesJniMock;
    @Mock private TabGroupSyncService mTabGroupSyncService;
    @Mock private DataSharingService mDataSharingService;
    @Mock private DataSharingUIDelegate mDataSharingUiDelegate;
    @Mock private CollaborationService mCollaborationService;
    @Mock private ServiceStatus mServiceStatus;
    @Mock private BottomControlsVisibilityController mVisibilityController;
    @Mock private TabGroupUiMediator.ResetHandler mResetHandler;
    @Mock private TabModelSelectorImpl mTabModelSelector;
    @Mock private TabContentManager mTabContentManager;
    @Mock private TabCreatorManager mTabCreatorManager;
    @Mock private TabCreator mTabCreator;
    @Mock private LayoutStateProvider mLayoutManager;
    @Spy private TabModel mTabModel;
    @Mock private View mView;
    @Mock private TabGroupModelFilterProvider mTabGroupModelFilterProvider;
    @Mock private TabGroupModelFilter mTabGroupModelFilter;
    @Mock private TabGridDialogMediator.DialogController mTabGridDialogController;
    @Mock private SharedImageTilesCoordinator mSharedImageTilesCoordinator;
    @Mock private SharedImageTilesConfig.Builder mSharedImageTilesConfigBuilder;
    @Mock private ObservableSupplierImpl<TabModel> mTabModelSupplier;
    @Mock private ThemeColorProvider mThemeColorProvider;
    @Mock private ColorStateList mTintList1;
    @Mock private ColorStateList mTintList2;
    @Mock private Callback<Object> mOnSnapshotTokenChange;

    @Captor private ArgumentCaptor<Callback<TabModel>> mTabModelSupplierObserverCaptor;
    @Captor private ArgumentCaptor<TabModelObserver> mTabModelObserverArgumentCaptor;
    @Captor private ArgumentCaptor<LayoutStateObserver> mLayoutStateObserverCaptor;

    @Captor
    private ArgumentCaptor<TabGroupModelFilterObserver> mTabGroupModelFilterObserverArgumentCaptor;

    @Captor private ArgumentCaptor<TabObserver> mTabObserverCaptor;
    @Captor private ArgumentCaptor<DataSharingService.Observer> mSharingObserverCaptor;
    @Captor private ArgumentCaptor<Object> mTokenCaptor;
    @Captor private ArgumentCaptor<ThemeColorObserver> mThemeColorObserverCaptor;
    @Captor private ArgumentCaptor<TintObserver> mTintObserverCaptor;

    private final ObservableSupplierImpl<Boolean> mOmniboxFocusStateSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mHandleBackPressChangedSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mTabGridDialogBackPressSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mTabGridDialogShowingOrAnimationSupplier =
            new ObservableSupplierImpl<>();
    private final OneshotSupplierImpl<LayoutStateProvider> mLayoutStateProviderSupplier =
            new OneshotSupplierImpl<>();
    private final ObservableSupplierImpl<Object> mChildTokenSupplier =
            new ObservableSupplierImpl<>();

    private Tab mTab1;
    private Tab mTab2;
    private Tab mTab3;
    private List<Tab> mTabGroup1;
    private List<Tab> mTabGroup2;
    private PropertyModel mModel;
    private TabGroupUiMediator mTabGroupUiMediator;
    private InOrder mResetHandlerInOrder;
    private InOrder mVisibilityControllerInOrder;
    private LazyOneshotSupplier<TabGridDialogMediator.DialogController> mDialogControllerSupplier;

    private Tab prepareTab(int tabId, int rootId) {
        Tab tab = TabUiUnitTestUtils.prepareTab(tabId, rootId);
        doReturn(tab).when(mTabModelSelector).getTabById(tabId);
        return tab;
    }

    private void prepareIncognitoTabModel() {
        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        List<Tab> tabs = new ArrayList<>(Arrays.asList(newTab));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);
        TabModel incognitoTabModel = spy(TabModel.class);
        doReturn(newTab).when(incognitoTabModel).getTabAt(POSITION1);
        doReturn(true).when(incognitoTabModel).isIncognito();
        doReturn(1).when(incognitoTabModel).getCount();
    }

    private void verifyNeverReset() {
        mResetHandlerInOrder.verify(mResetHandler, never()).resetStripWithListOfTabs(any());
        mVisibilityControllerInOrder
                .verify(mVisibilityController, never())
                .setBottomControlsVisible(anyBoolean());
    }

    private void verifyResetStrip(boolean isVisible, @Nullable List<Tab> tabs) {
        mResetHandlerInOrder.verify(mResetHandler).resetStripWithListOfTabs(tabs);
        if (mDialogControllerSupplier.hasValue()) {
            verify(mTabGridDialogController, atLeastOnce()).hideDialog(false);
        }
        mVisibilityControllerInOrder
                .verify(mVisibilityController)
                .setBottomControlsVisible(isVisible);
    }

    private void initAndAssertProperties(@Nullable Tab currentTab) {
        doReturn(true).when(mTabModelSelector).isTabStateInitialized();
        if (currentTab == null) {
            doReturn(TabModel.INVALID_TAB_INDEX).when(mTabModel).index();
            doReturn(0).when(mTabModel).getCount();
            doReturn(0).when(mTabGroupModelFilter).getIndividualTabAndGroupCount();
            doReturn(null).when(mTabModelSelector).getCurrentTab();
        } else {
            doReturn(mTabModel.indexOf(currentTab)).when(mTabModel).index();
            doReturn(currentTab).when(mTabModelSelector).getCurrentTab();
        }

        // Fake a similar behavior to the supplier in TabGroupUiCoordinator.
        mDialogControllerSupplier = LazyOneshotSupplier.fromValue(mTabGridDialogController);
        doReturn(mTabGridDialogBackPressSupplier)
                .when(mTabGridDialogController)
                .getHandleBackPressChangedSupplier();
        doReturn(mTabGridDialogShowingOrAnimationSupplier)
                .when(mTabGridDialogController)
                .getShowingOrAnimationSupplier();
        mTabGroupUiMediator =
                new TabGroupUiMediator(
                        mVisibilityController,
                        mHandleBackPressChangedSupplier,
                        mResetHandler,
                        mModel,
                        mTabModelSelector,
                        mTabContentManager,
                        mTabCreatorManager,
                        mLayoutStateProviderSupplier,
                        mDialogControllerSupplier,
                        mOmniboxFocusStateSupplier,
                        mSharedImageTilesCoordinator,
                        mSharedImageTilesConfigBuilder,
                        mThemeColorProvider,
                        mOnSnapshotTokenChange,
                        mChildTokenSupplier);

        if (currentTab == null) {
            verifyNeverReset();
            return;
        }

        verify(mTabModelSupplier).addObserver(mTabModelSupplierObserverCaptor.capture());

        // Verify strip initial reset.
        List<Tab> tabs = mTabGroupModelFilter.getRelatedTabList(currentTab.getId());
        if (mTabGroupModelFilter.isTabInTabGroup(currentTab)) {
            verifyResetStrip(true, tabs);
        } else {
            verifyNeverReset();
        }
    }

    private void setupSyncedGroup(boolean isShared) {
        SavedTabGroup savedTabGroup = new SavedTabGroup();
        savedTabGroup.title = GROUP_TITLE;
        savedTabGroup.collaborationId = isShared ? COLLABORATION_ID1 : null;
        when(mTabGroupSyncService.getGroup(any(LocalTabGroupId.class))).thenReturn(savedTabGroup);
    }

    /**
     * After setUp(), tabModel has 3 tabs in the following order: mTab1, mTab2 and mTab3. If
     * TabGroup is enabled, mTab2 and mTab3 are in a group. Each test must call
     * initAndAssertProperties(selectedTab) first, with selectedTab being the currently selected tab
     * when the TabGroupUiMediator is created.
     */
    @Before
    public void setUp() {
        TabGroupSyncFeaturesJni.setInstanceForTesting(mTabGroupSyncFeaturesJniMock);
        doReturn(true).when(mTabGroupSyncFeaturesJniMock).isTabGroupSyncEnabled(mProfile);
        TabGroupSyncServiceFactory.setForTesting(mTabGroupSyncService);
        DataSharingServiceFactory.setForTesting(mDataSharingService);

        when(mServiceStatus.isAllowedToJoin()).thenReturn(true);
        when(mCollaborationService.getServiceStatus()).thenReturn(mServiceStatus);
        CollaborationServiceFactory.setForTesting(mCollaborationService);
        when(mDataSharingService.getUiDelegate()).thenReturn(mDataSharingUiDelegate);

        // Set up Tabs.
        mTab1 = prepareTab(TAB1_ID, TAB1_ROOT_ID);
        mTab2 = prepareTab(TAB2_ID, TAB2_ROOT_ID);
        when(mTab2.getTabGroupId()).thenReturn(TAB2_GROUP_ID);
        mTab3 = prepareTab(TAB3_ID, TAB3_ROOT_ID);
        when(mTab3.getTabGroupId()).thenReturn(TAB2_GROUP_ID);
        mTabGroup1 = new ArrayList<>(Arrays.asList(mTab1));
        mTabGroup2 = new ArrayList<>(Arrays.asList(mTab2, mTab3));

        // Setup TabModel.
        when(mTabModel.getProfile()).thenReturn(mProfile);
        when(mTabModelSelector.isIncognitoSelected()).thenReturn(false);
        doReturn(mTabModel).when(mTabModel).getComprehensiveModel();
        doReturn(mTabModel).when(mTabModelSelector).getModel(false);
        doReturn(false).when(mTabModel).isIncognito();
        doReturn(mTabModel).when(mTabModelSelector).getModel(false);
        doReturn(3).when(mTabModel).getCount();
        doReturn(0).when(mTabModel).index();
        doReturn(mTab1).when(mTabModel).getTabAt(0);
        doReturn(mTab2).when(mTabModel).getTabAt(1);
        doReturn(mTab3).when(mTabModel).getTabAt(2);
        when(mTabModel.getTabById(TAB1_ID)).thenReturn(mTab1);
        when(mTabModel.getTabById(TAB2_ID)).thenReturn(mTab2);
        when(mTabModel.getTabById(TAB3_ID)).thenReturn(mTab3);
        doReturn(POSITION1).when(mTabModel).indexOf(mTab1);
        doReturn(POSITION2).when(mTabModel).indexOf(mTab2);
        doReturn(POSITION3).when(mTabModel).indexOf(mTab3);
        doNothing().when(mTab1).addObserver(mTabObserverCaptor.capture());
        doNothing().when(mTab2).addObserver(mTabObserverCaptor.capture());
        doNothing().when(mTab3).addObserver(mTabObserverCaptor.capture());

        // Setup TabGroupModelFilter.
        when(mTabGroupModelFilter.getTabModel()).thenReturn(mTabModel);
        doReturn(2).when(mTabGroupModelFilter).getIndividualTabAndGroupCount();
        doReturn(mTabGroup1).when(mTabGroupModelFilter).getRelatedTabList(TAB1_ID);
        doReturn(mTabGroup2).when(mTabGroupModelFilter).getRelatedTabList(TAB2_ID);
        doReturn(mTabGroup2).when(mTabGroupModelFilter).getRelatedTabList(TAB3_ID);
        doReturn(false).when(mTabGroupModelFilter).isTabInTabGroup(mTab1);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab2);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab3);

        doReturn(mTabGroupModelFilter)
                .when(mTabGroupModelFilterProvider)
                .getCurrentTabGroupModelFilter();
        doReturn(mTabGroupModelFilter)
                .when(mTabGroupModelFilterProvider)
                .getTabGroupModelFilter(true);
        doReturn(mTabGroupModelFilter)
                .when(mTabGroupModelFilterProvider)
                .getTabGroupModelFilter(false);
        doNothing()
                .when(mTabGroupModelFilter)
                .addTabGroupObserver(mTabGroupModelFilterObserverArgumentCaptor.capture());

        // Set up TabModelSelector and TabGroupModelFilterProvider.
        List<TabModel> tabModelList = new ArrayList<>();
        tabModelList.add(mTabModel);
        doReturn(mTabModel).when(mTabModelSelector).getCurrentModel();
        doReturn(mTab1).when(mTabModelSelector).getCurrentTab();
        doReturn(TAB1_ID).when(mTabModelSelector).getCurrentTabId();
        doReturn(tabModelList).when(mTabModelSelector).getModels();
        when(mTabModelSelector.getCurrentTabModelSupplier()).thenReturn(mTabModelSupplier);

        doReturn(mTabGroupModelFilterProvider)
                .when(mTabModelSelector)
                .getTabGroupModelFilterProvider();
        doNothing()
                .when(mTabGroupModelFilterProvider)
                .addTabGroupModelFilterObserver(mTabModelObserverArgumentCaptor.capture());

        // Set up OverviewModeBehavior.
        doNothing().when(mLayoutManager).addObserver(mLayoutStateObserverCaptor.capture());
        mLayoutStateProviderSupplier.set(mLayoutManager);

        // Set up ResetHandler.
        doNothing().when(mResetHandler).resetStripWithListOfTabs(any());
        doNothing().when(mResetHandler).resetGridWithListOfTabs(any());

        // Set up TabCreatorManager.
        doReturn(mTabCreator).when(mTabCreatorManager).getTabCreator(anyBoolean());
        doReturn(null).when(mTabCreator).createNewTab(any(), anyInt(), any());

        when(mSharedImageTilesConfigBuilder.setBorderColor(anyInt()))
                .thenReturn(mSharedImageTilesConfigBuilder);
        when(mSharedImageTilesConfigBuilder.setBackgroundColor(anyInt()))
                .thenReturn(mSharedImageTilesConfigBuilder);

        mResetHandlerInOrder = inOrder(mResetHandler);
        mVisibilityControllerInOrder = inOrder(mVisibilityController);
        mModel = new PropertyModel(TabGroupUiProperties.ALL_KEYS);
    }

    // *********************** Tab group related tests *************************

    @Test
    public void verifyInitialization_NoTab_TabGroup() {
        initAndAssertProperties(null);
    }

    @Test
    public void verifyInitialization_SingleTab() {
        initAndAssertProperties(mTab1);
    }

    @Test
    public void verifyInitialization_TabGroup() {
        // Tab 2 is in a tab group.
        initAndAssertProperties(mTab2);
    }

    @Test
    public void onClickShowGroupDialogButton_TabGroup() {
        initAndAssertProperties(mTab2);

        View.OnClickListener listener =
                mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_ON_CLICK_LISTENER);
        assertThat(listener, instanceOf(View.OnClickListener.class));

        listener.onClick(mView);

        verify(mTabContentManager).cacheTabThumbnail(mTab2);
        verify(mResetHandler).resetGridWithListOfTabs(mTabGroup2);
    }

    @Test
    public void onClickShowGroupDialogButton_TabGroup_ShowingOrAnimation() {
        initAndAssertProperties(mTab2);
        mDialogControllerSupplier.get();
        mTabGridDialogShowingOrAnimationSupplier.set(true);

        View.OnClickListener listener =
                mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_ON_CLICK_LISTENER);
        assertThat(listener, instanceOf(View.OnClickListener.class));

        listener.onClick(mView);
        verify(mResetHandler, never()).resetGridWithListOfTabs(any());

        mTabGridDialogShowingOrAnimationSupplier.set(false);

        listener.onClick(mView);
        verify(mResetHandler).resetGridWithListOfTabs(mTabGroup2);
    }

    @Test
    public void onClickNewTabButton_TabGroup() {
        initAndAssertProperties(mTab1);

        View.OnClickListener listener =
                mModel.get(TabGroupUiProperties.NEW_TAB_BUTTON_ON_CLICK_LISTENER);
        assertThat(listener, instanceOf(View.OnClickListener.class));

        listener.onClick(mView);

        verify(mTabCreator)
                .createNewTab(
                        isA(LoadUrlParams.class), eq(TabLaunchType.FROM_TAB_GROUP_UI), eq(mTab1));
    }

    @Test
    public void tabSelection_NotSameGroup_GroupToSingleTab() {
        initAndAssertProperties(mTab2);

        // Mock selecting tab 1, and the last selected tab is tab 2 which is in different group.
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab1);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab1, TabSelectionType.FROM_USER, TAB2_ID);

        // Strip should not be showing since tab 1 is a single tab.
        verifyResetStrip(false, null);
    }

    @Test
    public void tabSelection_NotSameGroup_GroupToGroup() {
        initAndAssertProperties(mTab2);

        // Mock that tab 1 is not a single tab.
        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        List<Tab> tabs = new ArrayList<>(Arrays.asList(mTab1, newTab));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB1_ID);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab1);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(newTab);
        Token tab1GroupId = new Token(1L, 32L);
        when(mTab1.getTabGroupId()).thenReturn(tab1GroupId);
        when(newTab.getTabGroupId()).thenReturn(tab1GroupId);

        // Mock selecting tab 1, and the last selected tab is tab 2 which is in different group.
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab1);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab1, TabSelectionType.FROM_USER, TAB2_ID);

        // Strip should be showing since we are selecting a group.
        verifyResetStrip(true, tabs);
    }

    @Test
    public void tabSelection_NotSameGroup_SingleTabToGroup() {
        initAndAssertProperties(mTab1);

        // Mock that tab 2 is not a single tab.
        List<Tab> tabGroup = mTabGroupModelFilter.getRelatedTabList(TAB2_ID);
        assertThat(tabGroup.size(), equalTo(2));
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab2);

        // Mock selecting tab 2, and the last selected tab is tab 1 which is a single tab.
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab2, TabSelectionType.FROM_USER, TAB1_ID);

        // Strip should be showing since we are selecting a group.
        verifyResetStrip(true, tabGroup);
    }

    @Test
    public void tabSelection_NotSameGroup_SingleTabToSingleTab() {
        initAndAssertProperties(mTab1);

        // Mock that new tab is a single tab.
        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        List<Tab> tabs = new ArrayList<>(Arrays.asList(newTab));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);

        // Mock selecting new tab, and the last selected tab is tab 1 which is also a single tab.
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(newTab, TabSelectionType.FROM_USER, TAB1_ID);

        // Strip should not be showing since new tab is a single tab.
        verifyNeverReset();
    }

    @Test
    public void tabSelection_SameGroup_TabGroup() {
        initAndAssertProperties(mTab2);

        // Mock selecting tab 3, and the last selected tab is tab 2 which is in the same group.
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab3, TabSelectionType.FROM_USER, TAB2_ID);

        // Strip should not be reset since we are selecting in one group.
        verifyNeverReset();
    }

    @Test
    public void tabSelection_ScrollToSelectedIndex() {
        initAndAssertProperties(mTab1);
        assertThat(mModel.get(TabGroupUiProperties.INITIAL_SCROLL_INDEX), equalTo(null));

        // Mock that {tab2, tab3} are in the same tab group.
        List<Tab> tabGroup = mTabGroupModelFilter.getRelatedTabList(TAB2_ID);
        assertThat(tabGroup.size(), equalTo(2));

        // Mock selecting tab 3, and the last selected tab is tab 1 which is a single tab.
        doReturn(mTab3).when(mTabModelSelector).getCurrentTab();
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab3, TabSelectionType.FROM_USER, TAB1_ID);

        // Strip should be showing since we are selecting a group, and it should scroll to the index
        // of currently selected tab.
        verifyResetStrip(true, tabGroup);
        assertThat(mModel.get(TabGroupUiProperties.INITIAL_SCROLL_INDEX), equalTo(1));
    }

    @Test
    public void tabClosure_NotLastTabInGroup_Selection_SingleTabGroupsEnabled() {
        initAndAssertProperties(mTab2);

        doReturn(false).when(mTabGroupModelFilter).isTabInTabGroup(mTab2);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab3);
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab3);

        // Mock closing tab 2, and tab 3 then gets selected. They are in the same group assume that
        // that Tab 3 is the last tab in the group, but tab groups of size 1 are supported.
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab3, TabSelectionType.FROM_CLOSE, TAB2_ID);

        // Strip should not be reset since we are still in this group.
        verifyNeverReset();
    }

    @Test
    public void tabClosure_LastTabInGroup_GroupUiNotVisible() {
        initAndAssertProperties(mTab1);

        // Mock closing tab 1, and tab 2 then gets selected. They are in different group.
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab2);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab2, TabSelectionType.FROM_CLOSE, TAB1_ID);

        // Strip should be reset since we are switching to a different group.
        verifyResetStrip(true, mTabGroup2);
    }

    // TODO(crbug.com/40637854): Ignore this test until we have a conclusion from the attached bug.
    @Ignore
    @Test
    public void tabClosure_LastTabInGroup_GroupUiVisible() {
        initAndAssertProperties(mTab2);

        // Mock closing tab 2 and tab, then tab 1 gets selected. They are in different group. Right
        // now tab group UI is visible.
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab3, TabSelectionType.FROM_CLOSE, TAB2_ID);
        doReturn(new ArrayList<>()).when(mTabGroupModelFilter).getRelatedTabList(TAB3_ID);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didSelectTab(mTab1, TabSelectionType.FROM_CLOSE, TAB3_ID);

        // Strip should be reset since tab group UI was visible and now we are switching to a
        // different group.
        verifyResetStrip(false, null);
    }

    @Test
    public void tabAddition_SingleTab() {
        initAndAssertProperties(mTab1);

        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        List<Tab> tabs = new ArrayList<>(Arrays.asList(newTab));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);

        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_CHROME_UI,
                        TabCreationState.LIVE_IN_FOREGROUND,
                        false);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_RESTORE,
                        TabCreationState.FROZEN_ON_RESTORE,
                        false);

        // Strip should be not be reset when adding a single new tab.
        verifyNeverReset();
    }

    @Test
    public void tabAddition_SingleTab_Refresh_WithoutAutoGroupCreation() {
        initAndAssertProperties(mTab1);

        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        List<Tab> tabs = new ArrayList<>(Arrays.asList(mTab1, newTab));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB1_ID);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab1);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(newTab);
        Token tab1GroupId = new Token(3478L, 348L);
        when(mTab1.getTabGroupId()).thenReturn(tab1GroupId);
        when(newTab.getTabGroupId()).thenReturn(tab1GroupId);

        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP,
                        TabCreationState.LIVE_IN_FOREGROUND,
                        false);

        // Strip should be be reset when long pressing a link and add a tab into group.
        verifyResetStrip(true, tabs);
    }

    @Test
    public void tabAddition_TabGroup_NoRefresh() {
        initAndAssertProperties(mTab2);

        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        mTabGroup2.add(newTab);
        doReturn(mTabGroup1).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);

        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_CHROME_UI,
                        TabCreationState.LIVE_IN_FOREGROUND,
                        false);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_RESTORE,
                        TabCreationState.FROZEN_ON_RESTORE,
                        false);
        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_LONGPRESS_BACKGROUND,
                        TabCreationState.LIVE_IN_FOREGROUND,
                        false);

        // Strip should be not be reset through these two types of launching.
        verifyNeverReset();
    }

    @Test
    public void tabAddition_TabGroup_ScrollToTheLast() {
        initAndAssertProperties(mTab2);
        assertThat(mModel.get(TabGroupUiProperties.INITIAL_SCROLL_INDEX), equalTo(0));

        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        mTabGroup2.add(newTab);
        when(newTab.getTabGroupId()).thenReturn(TAB2_GROUP_ID);
        doReturn(mTabGroup2).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);

        mTabModelObserverArgumentCaptor
                .getValue()
                .didAddTab(
                        newTab,
                        TabLaunchType.FROM_TAB_GROUP_UI,
                        TabCreationState.LIVE_IN_FOREGROUND,
                        false);

        // Strip should be not be reset through adding tab from UI.
        verifyNeverReset();
        assertThat(mTabGroupModelFilter.getRelatedTabList(TAB4_ID).size(), equalTo(3));
        assertThat(mModel.get(TabGroupUiProperties.INITIAL_SCROLL_INDEX), equalTo(2));
    }

    @Test
    public void restoreCompleted_TabModelNoTab() {
        // Simulate no tab in current TabModel.
        initAndAssertProperties(null);

        // Simulate restore finished.
        mTabModelObserverArgumentCaptor.getValue().restoreCompleted();

        mVisibilityControllerInOrder
                .verify(mVisibilityController, never())
                .setBottomControlsVisible(anyBoolean());
    }

    @Test
    public void restoreCompleted_UiNotVisible() {
        // Assume mTab1 is selected, and it does not have related tabs.
        initAndAssertProperties(mTab1);
        doReturn(POSITION1).when(mTabModel).index();
        doReturn(mTab1).when(mTabModelSelector).getCurrentTab();
        // Simulate restore finished.
        mTabModelObserverArgumentCaptor.getValue().restoreCompleted();

        mVisibilityControllerInOrder
                .verify(mVisibilityController, never())
                .setBottomControlsVisible(anyBoolean());
    }

    @Test
    public void restoreCompleted_UiVisible() {
        initAndAssertProperties(mTab1);
        // Assume mTab2 is selected, and it has related tabs mTab2 and mTab3.
        doReturn(POSITION2).when(mTabModel).index();
        doReturn(mTab2).when(mTabModelSelector).getCurrentTab();
        // Simulate restore finished.
        mTabModelObserverArgumentCaptor.getValue().restoreCompleted();

        mVisibilityControllerInOrder.verify(mVisibilityController).setBottomControlsVisible(true);
    }

    @Test
    public void restoreCompleted_OverviewModeVisible() {
        // Assume mTab2 is selected, and it has related tabs mTab2 and mTab3. Also, the overview
        // mode is visible when restoring completed.
        initAndAssertProperties(mTab2);
        doReturn(POSITION2).when(mTabModel).index();
        doReturn(mTab2).when(mTabModelSelector).getCurrentTab();
        doReturn(true).when(mLayoutManager).isLayoutVisible(LayoutType.TAB_SWITCHER);
        // Simulate restore finished.
        mTabModelObserverArgumentCaptor.getValue().restoreCompleted();

        mVisibilityControllerInOrder
                .verify(mVisibilityController, never())
                .setBottomControlsVisible(anyBoolean());
    }

    @Test
    public void tabClosureUndone_UiVisible_NotShowingOverviewMode() {
        // Assume mTab2 is selected, and it has related tabs mTab2 and mTab3.
        initAndAssertProperties(mTab2);

        // Simulate that another member of this group, newTab, is being undone from closure.
        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        doReturn(new ArrayList<>(Arrays.asList(mTab2, mTab3, newTab)))
                .when(mTabGroupModelFilter)
                .getRelatedTabList(TAB4_ID);

        mTabModelObserverArgumentCaptor.getValue().tabClosureUndone(newTab);

        // Since the strip is already visible, no resetting.
        mVisibilityControllerInOrder
                .verify(mVisibilityController, never())
                .setBottomControlsVisible(anyBoolean());
    }

    @Test
    public void tabClosureUndone_UiNotVisible_NotShowingOverviewMode_TabNotInGroup() {
        // Assume mTab1 is selected. Since mTab1 is now a single tab, the strip is invisible.
        initAndAssertProperties(mTab1);

        // Simulate mTab2 and mTab3 being undone from closure with mTab1 still selected.
        doReturn(new ArrayList<>(Arrays.asList(mTab2, mTab3)))
                .when(mTabGroupModelFilter)
                .getRelatedTabList(TAB2_ID);
        doReturn(new ArrayList<>(Arrays.asList(mTab2, mTab3)))
                .when(mTabGroupModelFilter)
                .getRelatedTabList(TAB3_ID);
        doReturn(new ArrayList<>(Arrays.asList(mTab1)))
                .when(mTabGroupModelFilter)
                .getRelatedTabList(TAB1_ID);
        doReturn(mTab1).when(mTabModelSelector).getCurrentTab();

        mTabModelObserverArgumentCaptor.getValue().tabClosureUndone(mTab2);
        mTabModelObserverArgumentCaptor.getValue().tabClosureUndone(mTab3);

        // Strip should remain invisible.
        verify(mVisibilityController, never()).setBottomControlsVisible(true);
    }

    @Test
    public void tabClosureUndone_UiNotVisible_NotShowingOverviewMode_TabInGroup() {
        // Assume mTab1 is selected. Since mTab1 is now a single tab, the strip is invisible.
        initAndAssertProperties(mTab1);

        // Simulate that newTab which was a tab in the same group as mTab1 is being undone from
        // closure.
        Tab newTab = prepareTab(TAB4_ID, TAB4_ID);
        List<Tab> tabs = new ArrayList<>(Arrays.asList(mTab1, newTab));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB1_ID);
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB4_ID);
        Token tab1GroupId = new Token(1L, 3789L);
        when(mTab1.getTabGroupId()).thenReturn(tab1GroupId);
        when(newTab.getTabGroupId()).thenReturn(tab1GroupId);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab1);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(newTab);
        doReturn(mTab1).when(mTabModelSelector).getCurrentTab();

        mTabModelObserverArgumentCaptor.getValue().tabClosureUndone(newTab);

        // Strip should reset to be visible.
        mVisibilityControllerInOrder
                .verify(mVisibilityController)
                .setBottomControlsVisible(eq(true));
    }

    @Test
    public void tabClosureUndone_UiNotVisible_ShowingTabSwitcherMode() {
        // Assume mTab1 is selected the strip is hidden.
        initAndAssertProperties(mTab1);

        // Simulate the overview mode is showing, which suppresses showing the strip.
        mLayoutStateObserverCaptor.getValue().onStartedShowing(LayoutType.TAB_SWITCHER);
        verifyNeverReset();

        // Simulate that we undo a group closure of {mTab2, mTab3}.
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab3);
        mTabModelObserverArgumentCaptor.getValue().tabClosureUndone(mTab3);
        mTabModelObserverArgumentCaptor.getValue().tabClosureUndone(mTab2);

        // Since overview mode is showing, we should not show strip.
        verifyNeverReset();
    }

    @Test
    public void layoutStateChange_SingleTab() {
        initAndAssertProperties(mTab1);

        mLayoutStateObserverCaptor.getValue().onStartedShowing(LayoutType.TAB_SWITCHER);
        verifyNeverReset();

        mLayoutStateObserverCaptor.getValue().onFinishedHiding(LayoutType.TAB_SWITCHER);
        verifyNeverReset();
    }

    @Test
    public void layoutStateChange_NoCurrentTab() {
        initAndAssertProperties(null);

        mLayoutStateObserverCaptor.getValue().onStartedShowing(LayoutType.TAB_SWITCHER);
        verifyNeverReset();

        mLayoutStateObserverCaptor.getValue().onFinishedHiding(LayoutType.TAB_SWITCHER);
        verifyNeverReset();
    }

    @Test
    public void layoutStateChange_TabGroup() {
        initAndAssertProperties(mTab2);
        mDialogControllerSupplier.get();

        mLayoutStateObserverCaptor.getValue().onStartedShowing(LayoutType.TAB_SWITCHER);
        verify(mTabGridDialogController, atLeastOnce()).hideDialog(false);
        verifyResetStrip(false, null);

        mLayoutStateObserverCaptor.getValue().onFinishedHiding(LayoutType.TAB_SWITCHER);
        verifyResetStrip(true, mTabGroup2);
    }

    @Test
    public void destroy_TabGroup() {
        initAndAssertProperties(mTab1);
        verify(mThemeColorProvider).addThemeColorObserver(mThemeColorObserverCaptor.capture());
        verify(mThemeColorProvider).addTintObserver(mTintObserverCaptor.capture());

        mTabGroupUiMediator.destroy();

        verify(mTabGroupModelFilterProvider)
                .removeTabGroupModelFilterObserver(mTabModelObserverArgumentCaptor.capture());
        verify(mLayoutManager).removeObserver(mLayoutStateObserverCaptor.capture());
        verify(mTabModelSupplier).removeObserver(mTabModelSupplierObserverCaptor.capture());
        verify(mTabGroupModelFilter, times(2))
                .removeTabGroupObserver(mTabGroupModelFilterObserverArgumentCaptor.capture());
        verify(mThemeColorProvider).removeThemeColorObserver(mThemeColorObserverCaptor.getValue());
        verify(mThemeColorProvider).removeTintObserver(mTintObserverCaptor.getValue());
    }

    @Test
    public void uiNotVisibleAfterDragCurrentTabOutOfGroup() {
        initAndAssertProperties(mTab3);

        List<Tab> tabs = new ArrayList<>(Arrays.asList(mTab3));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB3_ID);
        doReturn(false).when(mTabGroupModelFilter).isTabInTabGroup(mTab3);
        mTabGroupModelFilterObserverArgumentCaptor.getValue().didMoveTabOutOfGroup(mTab3, 1);

        verifyResetStrip(false, null);
    }

    @Test
    public void uiVisibleAfterDragCurrentTabOutOfGroup_GroupSize1() {
        initAndAssertProperties(mTab3);

        List<Tab> tabs = new ArrayList<>(Arrays.asList(mTab3));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB3_ID);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab3);
        doReturn(new Token(1L, TAB3_ROOT_ID)).when(mTab3).getTabGroupId();
        mTabGroupModelFilterObserverArgumentCaptor.getValue().didMoveTabOutOfGroup(mTab3, 1);

        verifyResetStrip(true, tabs);
    }

    @Test
    public void uiVisibleAfterMergeCurrentTabToGroup() {
        initAndAssertProperties(mTab1);

        List<Tab> tabs = new ArrayList<>(Arrays.asList(mTab1, mTab2));
        doReturn(tabs).when(mTabGroupModelFilter).getRelatedTabList(TAB1_ID);
        doReturn(true).when(mTabGroupModelFilter).isTabInTabGroup(mTab1);
        doReturn(new Token(1L, TAB2_ROOT_ID)).when(mTab1).getTabGroupId();
        mTabGroupModelFilterObserverArgumentCaptor.getValue().didMergeTabToGroup(mTab1);

        verifyResetStrip(true, tabs);
    }

    @Test
    public void uiNotVisibleAfterMergeNonCurrentTabToGroup() {
        initAndAssertProperties(mTab1);

        mTabGroupModelFilterObserverArgumentCaptor.getValue().didMergeTabToGroup(mTab3);

        verify(mResetHandler, never()).resetGridWithListOfTabs(any());
    }

    @Test
    public void backButtonPress_ShouldHandle() {
        initAndAssertProperties(mTab1);
        mDialogControllerSupplier.get();
        doReturn(true).when(mTabGridDialogController).handleBackPressed();
        mTabGridDialogBackPressSupplier.set(true);

        var groupUiBackPressSupplier = mTabGroupUiMediator.getHandleBackPressChangedSupplier();
        Assert.assertEquals(Boolean.TRUE, groupUiBackPressSupplier.get());

        assertThat(mTabGroupUiMediator.onBackPressed(), equalTo(true));
        verify(mTabGridDialogController).handleBackPressed();
    }

    @Test
    public void backButtonPress_ShouldNotHandle() {
        initAndAssertProperties(mTab1);
        mDialogControllerSupplier.get();
        doReturn(false).when(mTabGridDialogController).handleBackPressed();
        mTabGridDialogBackPressSupplier.set(false);
        var groupUiBackPressSupplier = mTabGroupUiMediator.getHandleBackPressChangedSupplier();

        assertNotEquals(Boolean.TRUE, groupUiBackPressSupplier.get());
        assertThat(mTabGroupUiMediator.onBackPressed(), equalTo(false));
        verify(mTabGridDialogController).handleBackPressed();
    }

    @Test
    public void backButtonPress_LateInitController() {
        initAndAssertProperties(mTab1);

        var groupUiBackPressSupplier = mTabGroupUiMediator.getHandleBackPressChangedSupplier();

        // Not initialized yet.
        assertNotEquals(Boolean.TRUE, groupUiBackPressSupplier.get());

        // Late init.
        mDialogControllerSupplier.get();
        doReturn(false).when(mTabGridDialogController).handleBackPressed();
        mTabGridDialogBackPressSupplier.set(false);

        Assert.assertFalse(groupUiBackPressSupplier.get());

        mTabGridDialogBackPressSupplier.set(true);
        doReturn(true).when(mTabGridDialogController).handleBackPressed();
        Assert.assertTrue(groupUiBackPressSupplier.get());
    }

    @Test
    public void switchTabModel_UiVisible_TabGroup() {
        initAndAssertProperties(mTab1);
        prepareIncognitoTabModel();

        // Mock that tab2 is selected after tab model switch, and tab2 is in a group.
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab2);
        mTabModelSupplierObserverCaptor.getValue().onResult(mTabModel);

        verifyResetStrip(true, mTabGroup2);
    }

    @Test
    public void switchTabModel_UiNotVisible_TabGroup() {
        initAndAssertProperties(mTab2);
        prepareIncognitoTabModel();

        // Mock that tab1 is selected after tab model switch, and tab1 is a single tab.
        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab1);
        mTabModelSupplierObserverCaptor.getValue().onResult(mTabModel);

        verifyResetStrip(false, null);
    }

    @Test
    public void testSetShowGroupDialogButtonOnClickListener() {
        initAndAssertProperties(mTab3);
        View.OnClickListener listener = v -> {};

        mModel.set(TabGroupUiProperties.SHOW_GROUP_DIALOG_ON_CLICK_LISTENER, null);

        mModel.set(TabGroupUiProperties.SHOW_GROUP_DIALOG_ON_CLICK_LISTENER, listener);

        assertThat(
                mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_ON_CLICK_LISTENER),
                equalTo(listener));
    }

    @Test
    public void testTabModelSelectorTabObserverDestroyWhenDetach() {
        InOrder tabObserverDestroyInOrder = inOrder(mTab1);
        initAndAssertProperties(mTab1);

        mTabObserverCaptor.getValue().onActivityAttachmentChanged(mTab1, null);

        tabObserverDestroyInOrder.verify(mTab1).removeObserver(mTabObserverCaptor.capture());

        mTabGroupUiMediator.destroy();

        tabObserverDestroyInOrder
                .verify(mTab1, never())
                .removeObserver(mTabObserverCaptor.capture());
    }

    @Test
    public void testOmniboxFocusChange() {
        initAndAssertProperties(mTab2);

        mOmniboxFocusStateSupplier.set(true);
        verifyResetStrip(false, null);

        when(mTabModelSelector.getCurrentTab()).thenReturn(mTab2);
        mOmniboxFocusStateSupplier.set(false);
        verifyResetStrip(true, mTabGroup2);
    }

    @Test
    @DisableFeatures(ChromeFeatureList.DATA_SHARING)
    public void testImageTiles_NoDataSharing() {
        setupSyncedGroup(/* isShared= */ true);

        initAndAssertProperties(mTab2);

        assertTrue(mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_BUTTON_VISIBLE));
        assertFalse(mModel.get(TabGroupUiProperties.IMAGE_TILES_CONTAINER_VISIBLE));
    }

    @Test
    public void testImageTiles_NoGroup() {
        setupSyncedGroup(/* isShared= */ true);

        initAndAssertProperties(mTab1);

        assertTrue(mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_BUTTON_VISIBLE));
        assertFalse(mModel.get(TabGroupUiProperties.IMAGE_TILES_CONTAINER_VISIBLE));
    }

    @Test
    public void testImageTiles_2Members() {
        setupSyncedGroup(/* isShared= */ true);
        when(mCollaborationService.getGroupData(COLLABORATION_ID1))
                .thenReturn(
                        SharedGroupTestHelper.newGroupData(
                                COLLABORATION_ID1, GROUP_MEMBER1, GROUP_MEMBER2));
        initAndAssertProperties(mTab2);

        assertFalse(mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_BUTTON_VISIBLE));
        assertTrue(mModel.get(TabGroupUiProperties.IMAGE_TILES_CONTAINER_VISIBLE));
        verify(mSharedImageTilesCoordinator)
                .onGroupMembersChanged(COLLABORATION_ID1, List.of(GROUP_MEMBER1, GROUP_MEMBER2));

        // Remove the collaboration data.
        verify(mDataSharingService).addObserver(mSharingObserverCaptor.capture());
        mSharingObserverCaptor.getValue().onGroupRemoved(COLLABORATION_ID1);

        assertTrue(mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_BUTTON_VISIBLE));
        assertFalse(mModel.get(TabGroupUiProperties.IMAGE_TILES_CONTAINER_VISIBLE));
        verify(mSharedImageTilesCoordinator).onGroupMembersChanged(null, null);
    }

    @Test
    public void testImageTiles_NoCollaborationId() {
        setupSyncedGroup(/* isShared= */ false);

        initAndAssertProperties(mTab2);

        assertTrue(mModel.get(TabGroupUiProperties.SHOW_GROUP_DIALOG_BUTTON_VISIBLE));
        assertFalse(mModel.get(TabGroupUiProperties.IMAGE_TILES_CONTAINER_VISIBLE));
    }

    @Test
    public void testSnapshotToken() {
        doReturn(Color.RED).when(mThemeColorProvider).getThemeColor();
        initAndAssertProperties(mTab1);

        reset(mOnSnapshotTokenChange);
        mChildTokenSupplier.set(new Object());
        verify(mOnSnapshotTokenChange).onResult(mTokenCaptor.capture());
        Object composite1 = mTokenCaptor.getValue();
        assertNotNull(composite1);

        reset(mOnSnapshotTokenChange);
        doReturn(Color.BLUE).when(mThemeColorProvider).getThemeColor();
        verify(mThemeColorProvider).addThemeColorObserver(mThemeColorObserverCaptor.capture());
        mThemeColorObserverCaptor
                .getValue()
                .onThemeColorChanged(Color.BLUE, /* shouldAnimate= */ false);
        verify(mOnSnapshotTokenChange).onResult(mTokenCaptor.capture());
        Object composite2 = mTokenCaptor.getValue();
        assertNotNull(composite2);
        assertNotEquals(composite1, composite2);

        reset(mOnSnapshotTokenChange);
        mChildTokenSupplier.set(new Object());
        verify(mOnSnapshotTokenChange).onResult(mTokenCaptor.capture());
        Object composite3 = mTokenCaptor.getValue();
        assertNotNull(composite3);
        assertNotEquals(composite1, composite3);
        assertNotEquals(composite2, composite3);

        reset(mOnSnapshotTokenChange);
        mModel.get(TabGroupUiProperties.WIDTH_PX_CALLBACK).onResult(123);
        verify(mOnSnapshotTokenChange).onResult(mTokenCaptor.capture());
        Object composite4 = mTokenCaptor.getValue();
        assertNotNull(composite4);
        assertNotEquals(composite1, composite4);
        assertNotEquals(composite2, composite4);
        assertNotEquals(composite3, composite4);
    }

    @Test
    public void testThemeColorChanged() {
        doReturn(Color.RED).when(mThemeColorProvider).getThemeColor();
        initAndAssertProperties(mTab1);
        verify(mSharedImageTilesConfigBuilder).setBorderColor(Color.RED);
        verify(mSharedImageTilesCoordinator).updateConfig(any());

        doReturn(Color.BLUE).when(mThemeColorProvider).getThemeColor();
        verify(mThemeColorProvider).addThemeColorObserver(mThemeColorObserverCaptor.capture());
        mThemeColorObserverCaptor
                .getValue()
                .onThemeColorChanged(Color.BLUE, /* shouldAnimate= */ false);
        verify(mSharedImageTilesConfigBuilder).setBorderColor(Color.BLUE);
        verify(mSharedImageTilesCoordinator, times(2)).updateConfig(any());
    }

    @Test
    public void testTintChanged() {
        doReturn(mTintList1).when(mThemeColorProvider).getTint();
        initAndAssertProperties(mTab2);
        assertEquals(mTintList1, mModel.get(TabGroupUiProperties.TINT));

        doReturn(mTintList2).when(mThemeColorProvider).getTint();
        verify(mThemeColorProvider).addTintObserver(mTintObserverCaptor.capture());
        mTintObserverCaptor
                .getValue()
                .onTintChanged(mTintList2, mTintList2, BrandedColorScheme.APP_DEFAULT);
        assertEquals(mTintList2, mModel.get(TabGroupUiProperties.TINT));
    }
}
