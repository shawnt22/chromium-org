// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.hub;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.widget.FrameLayout;

import androidx.test.ext.junit.rules.ActivityScenarioRule;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.ParameterizedRobolectricTestRunner;
import org.robolectric.ParameterizedRobolectricTestRunner.Parameter;
import org.robolectric.ParameterizedRobolectricTestRunner.Parameters;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.base.test.BaseRobolectricTestRule;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButtonCoordinator;
import org.chromium.chrome.browser.ui.edge_to_edge.EdgeToEdgeController;
import org.chromium.chrome.browser.ui.searchactivityutils.SearchActivityClient;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler.BackPressResult;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.ui.base.TestActivity;
import org.chromium.ui.util.XrUtils;

import java.util.Arrays;
import java.util.Collection;

/** Tests for {@link HubCoordinator}. */
@RunWith(ParameterizedRobolectricTestRunner.class)
public class HubCoordinatorUnitTest {
    // All the tests in this file will run twice, once for isXrDevice=true and once for
    // isXrDevice=false. Expect all the tests with the same results on XR devices too.
    // The setup ensures the correct environment is configured for each run.
    @Parameters
    public static Collection<Object[]> data() {
        return Arrays.asList(new Object[][] {{true}, {false}});
    }

    @Parameter(0)
    public boolean mIsXrDevice;

    private static final int TAB_ID = 7;
    private static final int INCOGNITO_TAB_ID = 9;

    @Rule
    public ActivityScenarioRule<TestActivity> mActivityScenarioRule =
            new ActivityScenarioRule<>(TestActivity.class);

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Rule public BaseRobolectricTestRule mBaseRule = new BaseRobolectricTestRule();

    @Mock private Tab mTab;
    @Mock private Tab mIncognitoTab;
    @Mock private HubLayoutController mHubLayoutController;
    @Mock private Pane mTabSwitcherPane;
    @Mock private Pane mIncognitoTabSwitcherPane;
    @Mock private MenuButtonCoordinator mMenuButtonCoordinator;
    @Mock private DisplayButtonData mReferenceButtonData;
    @Mock private ProfileProvider mProfileProvider;
    @Mock private Profile mProfile;
    @Mock private Tracker mTracker;
    @Mock private SearchActivityClient mSearchActivityClient;
    @Mock private HubColorMixer mHubColorMixer;
    private final ObservableSupplierImpl<Boolean> mHubVisibilitySupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mTabSwitcherBackPressSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIncognitoTabSwitcherBackPressSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Tab> mTabSupplier = new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Integer> mPreviousLayoutTypeSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<DisplayButtonData> mReferenceButtonDataSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mRegularHubSearchEnabledStateSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIncognitoHubSearchEnabledStateSupplier =
            new ObservableSupplierImpl<>();
    private final OneshotSupplierImpl<ProfileProvider> mProfileProviderSupplier =
            new OneshotSupplierImpl<>();
    private final ObservableSupplierImpl<EdgeToEdgeController> mEdgeToEdgeSupplier =
            new ObservableSupplierImpl<>();
    private PaneManager mPaneManager;
    private FrameLayout mRootView;
    private HubCoordinator mHubCoordinator;

    @Before
    public void setUp() {
        XrUtils.setXrDeviceForTesting(mIsXrDevice);

        TrackerFactory.setTrackerForTests(mTracker);
        mReferenceButtonDataSupplier.set(mReferenceButtonData);
        when(mProfileProvider.getOriginalProfile()).thenReturn(mProfile);
        mProfileProviderSupplier.set(mProfileProvider);
        when(mTabSwitcherPane.getPaneId()).thenReturn(PaneId.TAB_SWITCHER);
        when(mTabSwitcherPane.getColorScheme()).thenReturn(HubColorScheme.DEFAULT);
        when(mTabSwitcherPane.getHandleBackPressChangedSupplier())
                .thenReturn(mTabSwitcherBackPressSupplier);
        when(mTabSwitcherPane.getActionButtonDataSupplier())
                .thenReturn(new ObservableSupplierImpl<>());
        when(mTabSwitcherPane.getReferenceButtonDataSupplier())
                .thenReturn(mReferenceButtonDataSupplier);
        when(mTabSwitcherPane.getHubSearchEnabledStateSupplier())
                .thenReturn(mRegularHubSearchEnabledStateSupplier);
        when(mIncognitoTabSwitcherPane.getPaneId()).thenReturn(PaneId.INCOGNITO_TAB_SWITCHER);
        when(mIncognitoTabSwitcherPane.getColorScheme()).thenReturn(HubColorScheme.INCOGNITO);
        when(mIncognitoTabSwitcherPane.getHandleBackPressChangedSupplier())
                .thenReturn(mIncognitoTabSwitcherBackPressSupplier);
        when(mIncognitoTabSwitcherPane.getActionButtonDataSupplier())
                .thenReturn(new ObservableSupplierImpl<>());
        when(mIncognitoTabSwitcherPane.getReferenceButtonDataSupplier())
                .thenReturn(mReferenceButtonDataSupplier);
        when(mIncognitoTabSwitcherPane.getHubSearchEnabledStateSupplier())
                .thenReturn(mIncognitoHubSearchEnabledStateSupplier);
        when(mTab.getId()).thenReturn(TAB_ID);
        when(mTab.isIncognito()).thenReturn(false);
        when(mIncognitoTab.getId()).thenReturn(INCOGNITO_TAB_ID);
        when(mIncognitoTab.isIncognito()).thenReturn(true);
        when(mHubLayoutController.getPreviousLayoutTypeSupplier())
                .thenReturn(mPreviousLayoutTypeSupplier);
        when(mHubLayoutController.getIsAnimatingSupplier())
                .thenReturn(new ObservableSupplierImpl<>());

        PaneListBuilder builder =
                new PaneListBuilder(new DefaultPaneOrderController())
                        .registerPane(
                                PaneId.TAB_SWITCHER,
                                LazyOneshotSupplier.fromValue(mTabSwitcherPane))
                        .registerPane(
                                PaneId.INCOGNITO_TAB_SWITCHER,
                                LazyOneshotSupplier.fromValue(mIncognitoTabSwitcherPane));
        mPaneManager = spy(new PaneManagerImpl(builder, mHubVisibilitySupplier));

        assertTrue(mPaneManager.focusPane(PaneId.TAB_SWITCHER));
        assertEquals(mTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());
        mActivityScenarioRule.getScenario().onActivity(this::onActivity);
    }

    private void onActivity(TestActivity activity) {
        mRootView = new FrameLayout(activity);
        activity.setContentView(mRootView);

        mHubCoordinator =
                new HubCoordinator(
                        activity,
                        mProfileProviderSupplier,
                        mRootView,
                        mPaneManager,
                        mHubLayoutController,
                        mTabSupplier,
                        mMenuButtonCoordinator,
                        mSearchActivityClient,
                        mEdgeToEdgeSupplier,
                        mHubColorMixer,
                        /* xrSpaceModeObservableSupplier= */ null);
        ShadowLooper.runUiThreadTasks();
        mRootView.getChildCount();
        assertNotEquals(0, mRootView.getChildCount());
    }

    @After
    public void tearDown() {
        mHubCoordinator.destroy();
        assertEquals(0, mRootView.getChildCount());
        assertFalse(mTabSwitcherBackPressSupplier.hasObservers());
        assertFalse(mPreviousLayoutTypeSupplier.hasObservers());
        assertFalse(mIncognitoTabSwitcherBackPressSupplier.hasObservers());
        assertFalse(mTabSupplier.hasObservers());
    }

    @Test
    public void testFocusedPaneBackPress() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        when(mTabSwitcherPane.handleBackPress())
                .thenReturn(BackPressResult.SUCCESS)
                .thenReturn(BackPressResult.FAILURE);
        mTabSwitcherBackPressSupplier.set(true);

        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mHubCoordinator.handleBackPress());
        verify(mTabSwitcherPane).handleBackPress();

        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.FAILURE, mHubCoordinator.handleBackPress());
        verify(mTabSwitcherPane, times(2)).handleBackPress();

        mTabSwitcherBackPressSupplier.set(false);
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testChangePaneBackPress() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        // Tab switcher pane is focused.
        when(mTabSwitcherPane.handleBackPress()).thenReturn(BackPressResult.SUCCESS);
        mTabSwitcherBackPressSupplier.set(true);

        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mHubCoordinator.handleBackPress());
        verify(mTabSwitcherPane).handleBackPress();

        mTabSwitcherBackPressSupplier.set(false);
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        // Change to incognito tab switcher.
        when(mIncognitoTabSwitcherPane.handleBackPress()).thenReturn(BackPressResult.SUCCESS);
        mIncognitoTabSwitcherBackPressSupplier.set(true);

        assertTrue(mPaneManager.focusPane(PaneId.INCOGNITO_TAB_SWITCHER));
        assertEquals(mIncognitoTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mHubCoordinator.handleBackPress());
        verify(mIncognitoTabSwitcherPane).handleBackPress();

        mIncognitoTabSwitcherBackPressSupplier.set(false);

        // Back to tab switcher.
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.SUCCESS, mHubCoordinator.handleBackPress());
        assertEquals(mTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());

        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testBackNavigationBetweenPanes() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        assertTrue(mPaneManager.focusPane(PaneId.INCOGNITO_TAB_SWITCHER));
        assertEquals(mIncognitoTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        assertEquals(BackPressResult.SUCCESS, mHubCoordinator.handleBackPress());
        assertEquals(mTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testBackNavigationBetweenPanesOnEscapeKeyPressNotTriggered() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        assertTrue(mPaneManager.focusPane(PaneId.INCOGNITO_TAB_SWITCHER));
        assertEquals(mIncognitoTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        assertEquals(Boolean.FALSE, mHubCoordinator.handleEscPress());
        assertEquals(mIncognitoTabSwitcherPane, mPaneManager.getFocusedPaneSupplier().get());
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
    }

    @Test
    public void testBackNavigationWithNullTab() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.FAILURE, mHubCoordinator.handleBackPress());

        mTabSupplier.set(mTab);
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        mTabSupplier.set(null);

        assertEquals(BackPressResult.FAILURE, mHubCoordinator.handleBackPress());
        verify(mHubLayoutController, never()).selectTabAndHideHubLayout(anyInt());
    }

    @Test
    public void testBackNavigationWithNullTabOnEscapeKeyPress() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(Boolean.FALSE, mHubCoordinator.handleEscPress());

        mTabSupplier.set(mTab);
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        mTabSupplier.set(null);

        assertEquals(Boolean.FALSE, mHubCoordinator.handleEscPress());
        verify(mHubLayoutController, never()).selectTabAndHideHubLayout(anyInt());
    }

    @Test
    public void testBackNavigationWithTab() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(BackPressResult.FAILURE, mHubCoordinator.handleBackPress());

        mTabSupplier.set(mTab);
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        assertEquals(BackPressResult.SUCCESS, mHubCoordinator.handleBackPress());
        verify(mHubLayoutController).selectTabAndHideHubLayout(eq(TAB_ID));
    }

    @Test
    public void testBackNavigationWithTabOnEscapeKeyPress() {
        assertFalse(mHubCoordinator.getHandleBackPressChangedSupplier().get());
        assertEquals(Boolean.FALSE, mHubCoordinator.handleEscPress());

        mTabSupplier.set(mTab);
        assertTrue(mHubCoordinator.getHandleBackPressChangedSupplier().get());

        assertEquals(Boolean.TRUE, mHubCoordinator.handleEscPress());
        verify(mHubLayoutController).selectTabAndHideHubLayout(eq(TAB_ID));
    }

    @Test
    public void testFocusPane() {
        reset(mPaneManager);
        mHubCoordinator.focusPane(PaneId.TAB_SWITCHER);
        verify(mPaneManager).focusPane(PaneId.TAB_SWITCHER);
    }

    @Test
    public void testSelectTabAndHideHub() {
        int tabId = 5;
        mHubCoordinator.selectTabAndHideHub(tabId);
        verify(mHubLayoutController).selectTabAndHideHubLayout(tabId);
    }
}
