// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import android.widget.Button;

import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.CallbackUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.fullscreen.FullscreenManagerTestUtils;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.transit.AutoResetCtaTransitTestRule;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.page.WebPageStation;
import org.chromium.chrome.test.util.ChromeTabUtils;
import org.chromium.net.test.util.TestWebServer;

/** Tests related to the sad tab logic. */
@RunWith(ChromeJUnit4ClassRunner.class)
@Batch(Batch.PER_CLASS)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class SadTabTest {
    private static final String LONG_HTML_TEST_PAGE =
            UrlUtils.encodeHtmlDataUri("<html><body style='height:100000px;'></body></html>");

    @Rule
    public AutoResetCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.fastAutoResetCtaActivityRule();

    private WebPageStation mInitialPage;

    private static boolean isShowingSadTab(Tab tab) {
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        return ThreadUtils.runOnUiThreadBlocking(() -> SadTab.isShowing(tab));
    }

    @Before
    public void setUp() {
        mInitialPage = mActivityTestRule.startOnBlankPage();
    }

    @After
    public void tearDown() {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    Tab tab = mActivityTestRule.getActivity().getActivityTab();
                    tab.show(TabSelectionType.FROM_USER, TabLoadIfNeededCaller.OTHER);
                    SadTab sadTab = SadTab.from(tab);
                    sadTab.removeIfPresent();
                });
    }

    /** Verify that the sad tab is shown when the renderer crashes. */
    @Test
    @SmallTest
    @Feature({"SadTab"})
    public void testSadTabShownWhenRendererProcessKilled() {
        final Tab tab = mInitialPage.loadedTabElement.get();

        Assert.assertFalse(isShowingSadTab(tab));
        simulateRendererKilled(tab, true);
        Assert.assertTrue(isShowingSadTab(tab));
    }

    /**
     * Verify that the sad tab is not shown when the renderer crashes in the background or the
     * renderer was killed by the OS out-of-memory killer.
     */
    @Test
    @SmallTest
    @Feature({"SadTab"})
    public void testSadTabNotShownWhenRendererProcessKilledInBackround() {
        final Tab tab = mInitialPage.loadedTabElement.get();

        Assert.assertFalse(isShowingSadTab(tab));
        simulateRendererKilled(tab, false);
        Assert.assertFalse(isShowingSadTab(tab));
    }

    /** Verify that a tab navigating to a page that is killed in the background is reloaded. */
    @Test
    @SmallTest
    @Feature({"SadTab"})
    public void testSadTabReloadAfterKill() throws Throwable {
        final Tab tab = mInitialPage.loadedTabElement.get();

        TestWebServer webServer = TestWebServer.start();
        try {
            final String url1 = webServer.setEmptyResponse("/page1.html");
            mActivityTestRule.loadUrl(url1);
            Assert.assertFalse(tab.needsReload());
            simulateRendererKilled(tab, false);
            Assert.assertTrue(tab.needsReload());
        } finally {
            webServer.shutdown();
        }
    }

    /** Verify that a tab killed in the background is not reloaded if another load has started. */
    @Test
    @SmallTest
    @Feature({"SadTab"})
    public void testSadTabNoReloadAfterLoad() throws Throwable {
        final Tab tab = mInitialPage.loadedTabElement.get();

        TestWebServer webServer = TestWebServer.start();
        try {
            final String url1 = webServer.setEmptyResponse("/page1.html");
            final String url2 = webServer.setEmptyResponse("/page2.html");
            mActivityTestRule.loadUrl(url1);
            Assert.assertFalse(tab.needsReload());
            simulateRendererKilled(tab, false);
            mActivityTestRule.loadUrl(url2);
            Assert.assertFalse(tab.needsReload());
        } finally {
            webServer.shutdown();
        }
    }

    /**
     * Confirm that after a successive refresh of a failed tab that failed to load, change the
     * button from "Reload" to "Send Feedback". If reloaded a third time and it is successful it
     * reverts from "Send Feedback" to "Reload".
     */
    @Test
    @SmallTest
    @Feature({"SadTab"})
    public void testSadTabPageButtonText() throws IllegalArgumentException {
        final Tab tab = mInitialPage.loadedTabElement.get();

        Assert.assertFalse(isShowingSadTab(tab));
        simulateRendererKilled(tab, true);
        Assert.assertTrue(isShowingSadTab(tab));
        String actualText = getSadTabButton(tab).getText().toString();
        Assert.assertEquals(
                "Expected the sad tab button to have the reload label",
                mActivityTestRule.getActivity().getString(R.string.sad_tab_reload_label),
                actualText);

        reloadSadTab(tab);
        Assert.assertTrue(isShowingSadTab(tab));
        actualText = getSadTabButton(tab).getText().toString();
        Assert.assertTrue(showSendFeedbackView(tab));
        Assert.assertEquals(
                "Expected the sad tab button to have the feedback label after the tab button "
                        + "crashes twice in a row.",
                mActivityTestRule.getActivity().getString(R.string.sad_tab_send_feedback_label),
                actualText);
        mActivityTestRule.loadUrl("about:blank");
        Assert.assertFalse(
                "Expected about:blank to destroy the sad tab however the sad tab is still in "
                        + "view",
                isShowingSadTab(tab));
        simulateRendererKilled(tab, true);
        actualText = getSadTabButton(tab).getText().toString();
        Assert.assertEquals(
                "Expected the sad tab button to have the reload label after a successful load",
                mActivityTestRule.getActivity().getString(R.string.sad_tab_reload_label),
                actualText);
    }

    @Test
    @MediumTest
    @Feature({"SadTab"})
    @DisabledTest(message = "https://crbug.com/1447840")
    public void testSadTabBrowserControlsVisibility() {
        ThreadUtils.runOnUiThreadBlocking(
                TabStateBrowserControlsVisibilityDelegate::disablePageLoadDelayForTests);
        FullscreenManagerTestUtils.disableBrowserOverrides();
        mActivityTestRule.loadUrl(LONG_HTML_TEST_PAGE);
        FullscreenManagerTestUtils.waitForBrowserControlsToBeMoveable(
                mActivityTestRule.getActivityTestRule(),
                mActivityTestRule.getActivity().getActivityTab());
        FullscreenManagerTestUtils.scrollBrowserControls(
                mActivityTestRule.getActivityTestRule(), false);
        simulateRendererKilled(mActivityTestRule.getActivity().getActivityTab(), true);
        FullscreenManagerTestUtils.waitForBrowserControlsPosition(
                mActivityTestRule.getActivityTestRule(), 0);
    }

    /** Helper method that kills the renderer on a UI thread. */
    private static void simulateRendererKilled(final Tab tab, final boolean visible) {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    if (!visible) tab.hide(TabHidingType.CHANGED_TABS);
                    ChromeTabUtils.simulateRendererKilledForTesting(tab);
                });
    }

    /** Helper method that reloads a tab with a SadTabView currently displayed. */
    private static void reloadSadTab(final Tab tab) {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    SadTab sadTab = SadTab.from(tab);
                    sadTab.removeIfPresent();
                    sadTab.show(
                            tab.getContext(),
                            CallbackUtils.emptyRunnable(),
                            CallbackUtils.emptyRunnable());
                });
    }

    private static boolean showSendFeedbackView(final Tab tab) {
        return ThreadUtils.runOnUiThreadBlocking(() -> SadTab.from(tab).showSendFeedbackView());
    }

    /**
     * If there is a SadTabView, this method will get the button for the sad tab.
     *
     * @param tab The tab that needs to contain a SadTabView.
     * @return Returns the button that is on the SadTabView, null if SadTabView. doesn't exist.
     */
    private static Button getSadTabButton(Tab tab) {
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        return ThreadUtils.runOnUiThreadBlocking(
                () -> tab.getView().findViewById(R.id.sad_tab_button));
    }
}
