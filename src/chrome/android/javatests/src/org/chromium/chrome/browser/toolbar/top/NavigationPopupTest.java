// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.top;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.Matchers.lessThan;

import static org.chromium.base.test.util.Batch.PER_CLASS;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.util.DisplayMetrics;
import android.view.View;
import android.widget.ListPopupWindow;
import android.widget.ListView;
import android.widget.TextView;

import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;

import org.hamcrest.Matchers;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Criteria;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Restriction;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.history.HistoryManagerUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.FreshCtaTransitTestRule;
import org.chromium.chrome.test.transit.page.WebPageStation;
import org.chromium.content_public.browser.NavigationController;
import org.chromium.content_public.browser.NavigationEntry;
import org.chromium.content_public.browser.NavigationHistory;
import org.chromium.content_public.browser.test.mock.MockNavigationController;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.url.GURL;

import java.util.List;
import java.util.concurrent.ExecutionException;

/** Tests for the navigation popup. */
@Batch(PER_CLASS)
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class NavigationPopupTest {
    @Rule
    public FreshCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.freshChromeTabbedActivityRule();

    private static final int INVALID_NAVIGATION_INDEX = -1;
    private int mMinWidth;
    private int mMaxWidth;
    private int mMargin;
    private WebPageStation mPage;

    @Before
    public void setUp() throws Exception {
        mPage = mActivityTestRule.startOnBlankPage();
        Resources resources = mActivityTestRule.getActivity().getResources();
        mMinWidth = resources.getDimensionPixelSize(R.dimen.navigation_popup_tablet_min_width);
        mMaxWidth = resources.getDimensionPixelSize(R.dimen.navigation_popup_tablet_max_width);
        mMargin = resources.getDimensionPixelSize(R.dimen.navigation_popup_tablet_width_margin);
    }

    // Exists solely to expose protected methods to this test.
    private static class TestNavigationEntry extends NavigationEntry {
        public TestNavigationEntry(
                int index,
                GURL url,
                GURL virtualUrl,
                GURL originalUrl,
                String title,
                Bitmap favicon,
                int transition,
                long timestamp) {
            super(
                    index,
                    url,
                    virtualUrl,
                    originalUrl,
                    title,
                    favicon,
                    transition,
                    timestamp,
                    /* isInitialEntry= */ false);
        }
    }

    private static class TestNavigationController extends MockNavigationController {
        private final NavigationHistory mHistory;
        private int mNavigatedIndex = INVALID_NAVIGATION_INDEX;

        public TestNavigationController() {
            mHistory = new NavigationHistory();
            mHistory.addEntry(
                    new TestNavigationEntry(
                            1,
                            new GURL("about:blank"),
                            GURL.emptyGURL(),
                            null,
                            "About Blank",
                            null,
                            0,
                            0));
            mHistory.addEntry(
                    new TestNavigationEntry(
                            5,
                            new GURL(UrlUtils.encodeHtmlDataUri("<html>1</html>")),
                            GURL.emptyGURL(),
                            GURL.emptyGURL(),
                            null,
                            null,
                            0,
                            0));
        }

        public TestNavigationController(List<NavigationEntry> navigationEntries) {
            mHistory = new NavigationHistory();
            for (NavigationEntry navigationEntry : navigationEntries) {
                mHistory.addEntry(navigationEntry);
            }
        }

        @Override
        public NavigationHistory getDirectedNavigationHistory(boolean isForward, int itemLimit) {
            return mHistory;
        }

        @Override
        public void goToNavigationIndex(int index) {
            mNavigatedIndex = index;
        }

        public int getEntryCount() {
            return mHistory.getEntryCount();
        }
    }

    @Test
    @MediumTest
    @Feature({"Navigation"})
    public void testFaviconFetching() throws ExecutionException {
        final TestNavigationController controller = new TestNavigationController();
        final ListPopupWindow popup = showPopup(controller, false);

        CriteriaHelper.pollUiThread(
                () -> {
                    NavigationHistory history = controller.mHistory;
                    for (int i = 0; i < history.getEntryCount(); i++) {
                        Criteria.checkThat(
                                "Favicon[" + i + "] not updated",
                                history.getEntryAtIndex(i).getFavicon(),
                                Matchers.notNullValue());
                    }
                });

        ThreadUtils.runOnUiThreadBlocking(() -> popup.dismiss());
    }

    @Test
    @SmallTest
    @Feature({"Navigation"})
    public void testItemSelection() throws ExecutionException {
        final TestNavigationController controller = new TestNavigationController();
        final ListPopupWindow popup = showPopup(controller, false);

        ThreadUtils.runOnUiThreadBlocking((Runnable) () -> popup.performItemClick(1));

        Assert.assertFalse("Popup did not hide as expected.", popup.isShowing());
        Assert.assertEquals(
                "Popup attempted to navigate to the wrong index", 5, controller.mNavigatedIndex);
    }

    @Test
    @SmallTest
    @Feature({"Navigation"})
    public void testShowAllHistory() throws ExecutionException {
        final TestNavigationController controller = new TestNavigationController();
        final ListPopupWindow popup = showPopup(controller, false);

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    ListView list = popup.getListView();
                    View view =
                            list.getAdapter().getView(list.getAdapter().getCount() - 1, null, list);
                    TextView text = view.findViewById(R.id.entry_title);
                    Assert.assertNotNull(text);
                    Assert.assertEquals(
                            text.getResources().getString(R.string.show_full_history),
                            text.getText().toString());
                });
    }

    @Test
    @SmallTest
    @Feature({"Navigation"})
    public void testPopupForIncognito() throws ExecutionException {
        final TestNavigationController controller = new TestNavigationController();
        final ListPopupWindow popup = showPopup(controller, true);

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    ListView list = popup.getListView();
                    View view =
                            list.getAdapter().getView(list.getAdapter().getCount() - 1, null, list);
                    TextView text = view.findViewById(R.id.entry_title);
                    Assert.assertNotNull(text);
                    Assert.assertNotEquals(
                            text.getResources().getString(R.string.show_full_history),
                            text.getText().toString());
                    Assert.assertEquals(controller.getEntryCount(), list.getAdapter().getCount());
                });
    }

    @Test
    @SmallTest
    @Restriction(DeviceFormFactor.TABLET_OR_DESKTOP)
    @Feature("Navigation")
    public void testWidth_smallestWidth() throws ExecutionException {
        final TestNavigationController controller =
                new TestNavigationController(
                        List.of(
                                new TestNavigationEntry(
                                        1,
                                        GURL.emptyGURL(),
                                        GURL.emptyGURL(),
                                        null,
                                        "l",
                                        null,
                                        0,
                                        0)));
        // Use incognito so that the menu won't contain the "Show all history" option.
        final ListPopupWindow popup = showPopup(controller, /* isOffTheRecord= */ true);
        Assert.assertEquals(mMinWidth, popup.getWidth());
    }

    @Test
    @SmallTest
    @Restriction(DeviceFormFactor.TABLET_OR_DESKTOP)
    @Feature("Navigation")
    public void testWidth_mediumWidth() throws ExecutionException {
        // The default controller has a moderately long title, so the width is between min & max.
        final TestNavigationController controller = new TestNavigationController();
        final ListPopupWindow popup = showPopup(controller, /* isOffTheRecord= */ false);
        assertThat(popup.getWidth(), greaterThan(mMinWidth));
        assertThat(popup.getWidth(), lessThan(mMaxWidth));
    }

    @Test
    @SmallTest
    @Restriction(DeviceFormFactor.TABLET_OR_DESKTOP)
    @Feature("Navigation")
    public void testWidth_maxWidth() throws ExecutionException {
        // Add an entry with a very long title, so the width is limited by the max.
        final TestNavigationController controller =
                new TestNavigationController(
                        List.of(
                                new TestNavigationEntry(
                                        1,
                                        new GURL("about:blank"),
                                        GURL.emptyGURL(),
                                        null,
                                        "A".repeat(100),
                                        null,
                                        0,
                                        0)));
        final ListPopupWindow popup = showPopup(controller, /* isOffTheRecord= */ false);
        Assert.assertEquals(mMaxWidth, popup.getWidth());
    }

    @Test
    @SmallTest
    @Restriction(DeviceFormFactor.TABLET_OR_DESKTOP)
    @Feature("Navigation")
    public void testWidth_limitedByScreenWidth() throws ExecutionException {
        final DisplayMetrics displayMetrics =
                mActivityTestRule.getActivity().getResources().getDisplayMetrics();
        final int savedScreenWidth = displayMetrics.widthPixels;
        final int screenWidthForTest = mMaxWidth - 5;
        displayMetrics.widthPixels = screenWidthForTest;

        // Add an entry with a very long title, so the width is limited by the screen width.
        final TestNavigationController controller =
                new TestNavigationController(
                        List.of(
                                new TestNavigationEntry(
                                        1,
                                        new GURL("about:blank"),
                                        GURL.emptyGURL(),
                                        null,
                                        "A".repeat(100),
                                        null,
                                        0,
                                        0)));
        final ListPopupWindow popup = showPopup(controller, /* isOffTheRecord= */ false);
        Assert.assertEquals(screenWidthForTest - mMargin, popup.getWidth());

        // Clean up.
        displayMetrics.widthPixels = savedScreenWidth;
    }

    private ListPopupWindow showPopup(NavigationController controller, boolean isOffTheRecord)
            throws ExecutionException {
        return ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    Profile profile =
                            isOffTheRecord
                                    ? ProfileManager.getLastUsedRegularProfile()
                                            .getPrimaryOtrProfile(true)
                                    : ProfileManager.getLastUsedRegularProfile();
                    NavigationPopup popup =
                            new NavigationPopup(
                                    profile,
                                    mActivityTestRule.getActivity(),
                                    controller,
                                    NavigationPopup.Type.TABLET_FORWARD,
                                    mActivityTestRule.getActivity().getActivityTabProvider(),
                                    (tab) ->
                                            HistoryManagerUtils.showHistoryManager(
                                                    mActivityTestRule.getActivity(), tab, profile));
                    popup.show(
                            mActivityTestRule
                                    .getActivity()
                                    .getToolbarManager()
                                    .getToolbarLayoutForTesting());
                    return popup.getPopupForTesting();
                });
    }
}
