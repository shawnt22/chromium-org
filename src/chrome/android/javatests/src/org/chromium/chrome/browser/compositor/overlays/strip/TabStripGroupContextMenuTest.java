// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.compositor.overlays.strip;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.pressImeActionButton;
import static androidx.test.espresso.action.ViewActions.pressKey;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isRoot;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import static org.hamcrest.CoreMatchers.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import static org.chromium.ui.test.util.ViewUtils.onViewWaiting;

import android.view.KeyEvent;

import androidx.test.filters.SmallTest;
import androidx.test.platform.app.InstrumentationRegistry;

import org.hamcrest.Matchers;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.Token;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Criteria;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Features;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tasks.tab_management.TabUiTestHelper;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.transit.AutoResetCtaTransitTestRule;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.util.ChromeTabUtils;
import org.chromium.chrome.test.util.TabStripUtils;
import org.chromium.components.tab_groups.TabGroupColorId;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Instrumentation tests for tab strip group title long-press menu popup */
@RunWith(ChromeJUnit4ClassRunner.class)
@Batch(Batch.PER_CLASS)
// TODO(crbug.com/419289558): Re-enable color surface feature flags
@Features.DisableFeatures({
    ChromeFeatureList.ANDROID_SURFACE_COLOR_UPDATE,
    ChromeFeatureList.GRID_TAB_SWITCHER_SURFACE_COLOR_UPDATE,
    ChromeFeatureList.GRID_TAB_SWITCHER_UPDATE,
    ChromeFeatureList.ANDROID_THEME_MODULE
})
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@Restriction(DeviceFormFactor.TABLET_OR_DESKTOP)
public class TabStripGroupContextMenuTest {
    @Rule
    public AutoResetCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.fastAutoResetCtaActivityRule();

    private StripLayoutHelper mStripLayoutHelper;
    private int mRootId;
    private Token mTabGroupId;
    private ModalDialogManager mModalDialogManager;

    @Before
    public void setUp() throws Exception {
        mStripLayoutHelper =
                TabStripUtils.getActiveStripLayoutHelper(mActivityTestRule.getActivity());
        mModalDialogManager = mActivityTestRule.getActivity().getModalDialogManager();
    }

    @After
    public void tearDown() {
        // Click anywhere to dismiss menu if has not already been dismissed.
        onView(isRoot()).perform(click());

        // Dismiss any visible dialogs(crbug.com/394606261). Clicking anywhere to dismiss the popup
        // menu may unintentionally trigger a menu item (e.g. "Ungroup"), which can show a dialog.
        // Attempts to redirect the click to views e.g.(R.id.compositor_view_holder) didn't work, as
        // no views outside the popup menu were accessible while it was showing. Dismissing the
        // popup menu directly via StripLayoutHelper was also ineffective, so explicitly dismissing
        // all dialogs.
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mModalDialogManager.dismissAllDialogs(DialogDismissalCause.UNKNOWN);
                });
    }

    @Test
    @SmallTest
    public void testOpenNewTabInGroup() {
        // Prepare standard state and show menu.
        prepareStandardState();
        showMenu();

        // Assert there are 2 grouped tabs.
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(false);
        int tabCount = tabGroupModelFilter.getTabCountForGroup(mTabGroupId);
        assertEquals("There should be 2 tabs in group", 2, tabCount);

        // Verify and click "New tab in group".
        onView(withText(R.string.open_new_tab_in_group_context_menu_item))
                .check(matches(isDisplayed()));
        onView(withText(R.string.open_new_tab_in_group_context_menu_item)).perform(click());

        // Verify the grouped tab count is incremented.
        assertEquals(
                "There should be 3 tabs in group",
                tabCount + 1,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));
    }

    @Test
    @SmallTest
    public void testUngroup() {
        // Prepare standard state and show menu.
        prepareStandardState();
        showMenu();

        // Assert there are 2 grouped tabs.
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(false);
        assertEquals(
                "There should be 2 tabs in group",
                2,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Verify and click "Ungroup".
        onView(withText(R.string.ungroup_tab_group_menu_item)).check(matches(isDisplayed()));
        onView(withText(R.string.ungroup_tab_group_menu_item)).perform(click());

        // Verify confirmation dialog is showing and tab group is ungrouped after confirming the
        // action.
        verifyModalDialog(/* shouldShow= */ true);
        onView(withText(R.string.ungroup_tab_group_action)).perform(click());
        assertEquals(
                "Tab group should be ungrouped",
                0,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Verify no tab group exists.
        TabModel tabModel = mActivityTestRule.getActivity().getCurrentTabModel();
        for (int i = 0; i < tabModel.getCount(); i++) {
            assertFalse(
                    "Tab should not be grouped",
                    tabGroupModelFilter.isTabInTabGroup(tabModel.getTabAt(i)));
        }
    }

    @Test
    @SmallTest
    public void testUngroup_Incognito() {
        // Prepare incognito state and show menu.
        prepareIncognitoState();
        showMenu();

        // Verify "Delete group" option should not be showing for incognito.
        onView(withText(R.string.tab_grid_dialog_toolbar_delete_group)).check(doesNotExist());

        // Assert there are 2 grouped tabs.
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(true);
        assertEquals(
                "There should be 2 tabs in group",
                2,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Verify and click "Ungroup".
        onView(withText(R.string.ungroup_tab_group_menu_item)).check(matches(isDisplayed()));
        onView(withText(R.string.ungroup_tab_group_menu_item)).perform(click());

        // Verify confirmation dialog is not showing for incognito and tab group is immediately
        // ungrouped.
        verifyModalDialog(/* shouldShow= */ false);
        assertEquals(
                "Tab group should be ungrouped",
                0,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Verify no tab group exists.
        TabModel tabModel = mActivityTestRule.getActivity().getCurrentTabModel();
        for (int i = 0; i < tabModel.getCount(); i++) {
            assertFalse(
                    "Tab should not be grouped",
                    tabGroupModelFilter.isTabInTabGroup(tabModel.getTabAt(i)));
        }
    }

    @Test
    @SmallTest
    public void testCloseGroup() {
        // Prepare standard state and show menu.
        prepareStandardState();
        showMenu();

        // Assert there are 2 grouped tabs.
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(false);
        assertEquals(
                "There should be 2 tabs in group",
                2,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Assert last tab is an ungrouped tab.
        TabModel tabModel = mActivityTestRule.getActivity().getCurrentTabModel();
        Tab ungroupedTab = tabModel.getTabAt(2);
        assertFalse(
                "Last tab should not be grouped",
                tabGroupModelFilter.isTabInTabGroup(ungroupedTab));

        // Verify and click "Close group".
        onView(withText(R.string.tab_grid_dialog_toolbar_close_group))
                .check(matches(isDisplayed()));
        onView(withText(R.string.tab_grid_dialog_toolbar_close_group)).perform(click());

        // Assert tab group is closed and undo option showed.
        assertFalse("Tab group should be closed", tabGroupModelFilter.tabGroupExists(mTabGroupId));
        assertEquals("Expected only one tab to be present", 1, tabModel.getCount());
        assertEquals(
                "Expected the only tab remain is the ungrouped tab",
                ungroupedTab.getId(),
                tabModel.getTabAt(0).getId());
        onView(withText("Undo")).check(matches(isDisplayed()));
    }

    @Test
    @SmallTest
    public void testCloseGroup_Incognito() {
        // Prepare incognito state and show menu.
        prepareIncognitoState();
        showMenu();

        // Verify "Delete group" option should not be showing for incognito.
        onView(withText(R.string.tab_grid_dialog_toolbar_delete_group)).check(doesNotExist());

        // Assert there are 2 grouped tabs.
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(true);
        assertEquals(
                "There should be 2 tabs in group",
                2,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Assert last tab is an ungrouped tab.
        TabModel tabModel = mActivityTestRule.getActivity().getCurrentTabModel();
        Tab ungroupedTab = tabModel.getTabAt(2);
        assertFalse(
                "Last tab should not be grouped",
                tabGroupModelFilter.isTabInTabGroup(ungroupedTab));

        // Verify and click "Close group".
        onView(withText(R.string.tab_grid_dialog_toolbar_close_group))
                .check(matches(isDisplayed()));
        onView(withText(R.string.tab_grid_dialog_toolbar_close_group)).perform(click());

        // Assert tab group is closed and undo option not showed.
        assertFalse("Tab group should be closed", tabGroupModelFilter.tabGroupExists(mTabGroupId));
        assertEquals("Expected only one tab to be present", 1, tabModel.getCount());
        assertEquals(
                "Expected the only tab remain is the ungrouped tab",
                ungroupedTab.getId(),
                tabModel.getTabAt(0).getId());
        onView(withText("Undo")).check(doesNotExist());
    }

    @Test
    @SmallTest
    public void testDeleteGroup() {
        // Prepare standard state and show menu.
        prepareStandardState();
        showMenu();

        // Assert there are 2 grouped tabs.
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(false);
        assertEquals(
                "There should be 2 tabs in group",
                2,
                tabGroupModelFilter.getTabCountForGroup(mTabGroupId));

        // Assert last tab is an ungrouped tab.
        TabModel tabModel = mActivityTestRule.getActivity().getCurrentTabModel();
        Tab ungroupedTab = tabModel.getTabAt(2);
        assertFalse(
                "Last tab should not be grouped",
                tabGroupModelFilter.isTabInTabGroup(ungroupedTab));

        // Verify and click "Delete group".
        onView(withText(R.string.tab_grid_dialog_toolbar_delete_group))
                .check(matches(isDisplayed()));
        onView(withText(R.string.tab_grid_dialog_toolbar_delete_group)).perform(click());

        // Verify confirmation dialog is showing and tab group is deleted after confirming the
        // action.
        verifyModalDialog(/* shouldShow= */ true);
        onView(withText(R.string.delete_tab_group_action)).perform(click());
        assertFalse("Tab group should be deleted", tabGroupModelFilter.tabGroupExists(mTabGroupId));
        assertEquals("Expected only one tab to be present", 1, tabModel.getCount());
        assertEquals(
                "Expected the only tab remain is the ungrouped tab",
                ungroupedTab.getId(),
                tabModel.getTabAt(0).getId());
    }

    @Test
    @SmallTest
    public void testUpdateAndDeleteGroupTitle() {
        // Prepare standard state and show menu.
        prepareStandardState();
        showMenu();

        // Verify default group title.
        String title = "2 tabs";
        onView(withText(title)).check(matches(isDisplayed()));

        // Update tab group title and verify.
        title = "newTitle";
        updateGroupTitle(title);
        onView(withText(title)).check(matches(isDisplayed()));

        // Delete the group title by clearing the edit box and verify its default to "N tabs".
        title = "";
        updateGroupTitle(title);
        onView(withText("2 tabs")).check(matches(isDisplayed()));
    }

    @Test
    @SmallTest
    public void testUpdateGroupColor() {
        // Prepare standard state and show menu.
        prepareStandardState();
        showMenu();
        TabGroupModelFilter tabGroupModelFilter = getTabGroupModelFilter(false);

        // Verify the default grey color is selected.
        assertEquals(
                "The default grey color should be selected",
                TabGroupColorId.GREY,
                tabGroupModelFilter.getTabGroupColor(mRootId));

        // Select the blue color.
        String blueColor =
                mActivityTestRule
                        .getActivity()
                        .getString(R.string.accessibility_tab_group_color_picker_color_item_blue);
        String notSelectedStringBlue =
                mActivityTestRule
                        .getActivity()
                        .getString(
                                R.string
                                        .accessibility_tab_group_color_picker_color_item_not_selected_description,
                                blueColor);
        onView(withContentDescription(notSelectedStringBlue)).perform(click());

        // Verify the blue color is selected.
        assertEquals(
                "The blue color should be selected",
                TabGroupColorId.BLUE,
                tabGroupModelFilter.getTabGroupColor(mRootId));
    }

    @Test
    @SmallTest
    @Feature("KeyboardA11y")
    public void testKeyboardFocusAndActivation() {
        // Prepare standard state and show menu.
        prepareStandardState();
        int numTabsBeforeClick = mActivityTestRule.getActivity().getCurrentTabModel().getCount();
        showMenu();

        // Hit down arrow 3 times. The first time should highlight the text field; the next should
        // highlight the color chooser row; the last should skip the divider and go to "add new tab
        // in group".
        for (int i = 0; i < 3; i++) {
            // We need to use espresso to perform the key events.
            // InstrumentationRegistry.getInstrumentation().sendKeySync() and
            // activity.dispatchKeyEvent don't work.
            onView(withId(R.id.tab_group_action_menu_list))
                    .perform(pressKey(KeyEvent.KEYCODE_DPAD_DOWN));
        }
        onView(withId(R.id.tab_group_action_menu_list)).perform(pressKey(KeyEvent.KEYCODE_SPACE));
        assertEquals(
                numTabsBeforeClick + 1,
                mActivityTestRule.getActivity().getCurrentTabModel().getCount());
    }

    private void prepareStandardState() {
        // 1. Create 2 more normal new tabs, for a total 3 normal tabs.
        ChromeTabUtils.newTabFromMenu(
                InstrumentationRegistry.getInstrumentation(), mActivityTestRule.getActivity());
        ChromeTabUtils.newTabFromMenu(
                InstrumentationRegistry.getInstrumentation(), mActivityTestRule.getActivity());

        // 2. Assert the normal tab strip is selected and there are 3 normal tabs in total.
        assertFalse(
                "Expected normal strip to be selected",
                mActivityTestRule.getActivity().getTabModelSelector().isIncognitoSelected());
        assertEquals(
                "There should be three tabs present",
                3,
                mActivityTestRule.getActivity().getCurrentTabModel().getCount());

        // 3. Create tab group with 2 tabs.
        groupFirstTwoTabs(/* isIncognito= */ false);
    }

    private void prepareIncognitoState() {
        // 1. create 3 incognito tabs
        ChromeTabUtils.newTabFromMenu(
                InstrumentationRegistry.getInstrumentation(),
                mActivityTestRule.getActivity(),
                /* incognito= */ true,
                /* waitForNtpLoad= */ true);
        ChromeTabUtils.newTabFromMenu(
                InstrumentationRegistry.getInstrumentation(),
                mActivityTestRule.getActivity(),
                /* incognito= */ true,
                /* waitForNtpLoad= */ true);
        ChromeTabUtils.newTabFromMenu(
                InstrumentationRegistry.getInstrumentation(),
                mActivityTestRule.getActivity(),
                /* incognito= */ true,
                /* waitForNtpLoad= */ true);

        // 2. Assert the incognito tab strip is selected and there are 3 incognito tabs in total.
        Assert.assertTrue(
                "Expected incognito strip to be selected",
                mActivityTestRule.getActivity().getTabModelSelector().isIncognitoSelected());
        assertEquals(
                "There are 3 incognito tabs present",
                3,
                mActivityTestRule.getActivity().getCurrentTabModel().getCount());

        // 3. Create an incognito tab group with 2 tabs.
        groupFirstTwoTabs(/* isIncognito= */ true);
    }

    private TabGroupModelFilter getTabGroupModelFilter(boolean isIncognito) {
        return mActivityTestRule
                .getActivity()
                .getTabModelSelector()
                .getTabGroupModelFilterProvider()
                .getTabGroupModelFilter(isIncognito);
    }

    private void verifyModalDialog(boolean shouldShow) {
        CriteriaHelper.pollUiThread(
                () -> {
                    Criteria.checkThat(mModalDialogManager.isShowing(), Matchers.is(shouldShow));
                });
    }

    private void groupFirstTwoTabs(boolean isIncognito) {
        // Assert the correct strip is selected.
        Assert.assertEquals(
                "The wrong strip is selected",
                isIncognito,
                mActivityTestRule.getActivity().getTabModelSelector().isIncognitoSelected());

        // Group the first two tabs.
        List<Tab> tabGroup =
                new ArrayList<>(
                        Arrays.asList(
                                mActivityTestRule.getActivity().getCurrentTabModel().getTabAt(0),
                                mActivityTestRule.getActivity().getCurrentTabModel().getTabAt(1)));
        TabUiTestHelper.createTabGroup(mActivityTestRule.getActivity(), isIncognito, tabGroup);
        mStripLayoutHelper =
                TabStripUtils.getActiveStripLayoutHelper(mActivityTestRule.getActivity());
        StripLayoutView[] views = mStripLayoutHelper.getStripLayoutViewsForTesting();
        assertTrue(
                "First view should be a group title.", views[0] instanceof StripLayoutGroupTitle);
    }

    private void showMenu() {
        StripLayoutView[] views = mStripLayoutHelper.getStripLayoutViewsForTesting();
        assertTrue(
                "First view should be a group title.", views[0] instanceof StripLayoutGroupTitle);
        StripLayoutGroupTitle stripLayoutGroupTitle = ((StripLayoutGroupTitle) views[0]);
        float x = stripLayoutGroupTitle.getPaddedX();
        float y = stripLayoutGroupTitle.getPaddedY();
        mRootId = stripLayoutGroupTitle.getRootId();
        mTabGroupId = stripLayoutGroupTitle.getTabGroupId();

        final StripLayoutHelperManager manager =
                mActivityTestRule.getActivity().getLayoutManager().getStripLayoutHelperManager();
        InstrumentationRegistry.getInstrumentation()
                .runOnMainSync(
                        new Runnable() {
                            @Override
                            public void run() {
                                manager.simulateLongPress(x, y);
                            }
                        });
        onViewWaiting(allOf(withId(R.id.tab_group_action_menu_list), isDisplayed()));
    }

    private void updateGroupTitle(String title) {
        KeyboardVisibilityDelegate delegate =
                mActivityTestRule.getActivity().getWindowAndroid().getKeyboardDelegate();

        // Click group title text box to display keyboard for editing.
        onView(withId(R.id.tab_group_title)).perform(click());

        // Verify keyboard is displayed.
        CriteriaHelper.pollUiThread(
                () ->
                        delegate.isKeyboardShowing(
                                mActivityTestRule.getActivity(),
                                mActivityTestRule
                                        .getActivity()
                                        .getCompositorViewHolderForTesting()));

        // Enter new title in text box and press "enter" to dismiss keyboard to update group title.
        onView(withId(R.id.tab_group_title))
                .perform(replaceText(title))
                .perform(pressImeActionButton());

        // Verify keyboard is dismissed.
        CriteriaHelper.pollUiThread(
                () ->
                        !delegate.isKeyboardShowing(
                                mActivityTestRule.getActivity(),
                                mActivityTestRule
                                        .getActivity()
                                        .getCompositorViewHolderForTesting()));
    }
}
