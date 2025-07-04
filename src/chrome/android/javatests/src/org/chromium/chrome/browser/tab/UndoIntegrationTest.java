// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import static org.chromium.base.test.util.Restriction.RESTRICTION_TYPE_NON_LOW_END_DEVICE;

import android.os.SystemClock;

import androidx.test.filters.LargeTest;

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
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Restriction;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.compositor.overlays.strip.StripLayoutTab;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.transit.AutoResetCtaTransitTestRule;
import org.chromium.chrome.test.transit.ChromeTransitTestRules;
import org.chromium.chrome.test.transit.page.WebPageStation;
import org.chromium.chrome.test.util.TabStripUtils;
import org.chromium.content_public.browser.test.util.DOMUtils;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.util.MotionEventUtils;

import java.util.concurrent.TimeoutException;

/** Tests undo and it's interactions with the UI. */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@Batch(Batch.PER_CLASS)
public class UndoIntegrationTest {
    @Rule
    public AutoResetCtaTransitTestRule mActivityTestRule =
            ChromeTransitTestRules.fastAutoResetCtaActivityRule();

    private static final String WINDOW_OPEN_BUTTON_URL =
            UrlUtils.encodeHtmlDataUri(
                    "<html>"
                            + "  <head>"
                            + "  <script>"
                            + "    function openWindow() {"
                            + "      window.open('about:blank');"
                            + "    }"
                            + "  </script>"
                            + "  </head>"
                            + "  <body>"
                            + "    <a id=\"link\" onclick=\"setTimeout(openWindow, 500);\">Open</a>"
                            + "  </body>"
                            + "</html>");

    @Before
    public void setUp() throws InterruptedException {
        SnackbarManager.setDurationForTesting(1500);
    }

    /** Test that a tab that is closing can't open other windows. */
    @Test
    @LargeTest
    @Restriction(RESTRICTION_TYPE_NON_LOW_END_DEVICE)
    @DisabledTest(message = "https://crbug.com/373950522")
    public void testAddNewContentsFromClosingTab() throws TimeoutException {
        // Load in a new tab as Chrome will close if the last tab is closed.
        mActivityTestRule.startOnWebPage(WINDOW_OPEN_BUTTON_URL);

        final TabModel model =
                mActivityTestRule.getActivity().getTabModelSelector().getCurrentModel();
        final Tab tab = TabModelUtils.getCurrentTab(model);

        // Click on the link that will trigger a delayed window popup. If this resolves it will open
        // a second about:blank tab.
        DOMUtils.clickNode(tab.getWebContents(), "link");

        // Attempt to close the tab, which will delay closing until the undo timeout goes away.
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    Assert.assertEquals("Model should have two tabs", 2, model.getCount());
                    model.getTabRemover()
                            .closeTabs(
                                    TabClosureParams.closeTab(tab).allowUndo(true).build(),
                                    /* allowDialog= */ false);
                    Assert.assertTrue("Tab was not marked as closing", tab.isClosing());
                    Assert.assertTrue(
                            "Tab is not actually closing", model.isClosurePending(tab.getId()));
                });

        // Give the model a chance to process the pending closure.
        CriteriaHelper.pollUiThread(
                () -> {
                    Criteria.checkThat(model.isClosurePending(tab.getId()), Matchers.is(false));
                    Criteria.checkThat(model.getCount(), Matchers.is(1));
                });

        // Validate that the model doesn't contain the original tab or any newly opened tabs.
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    Assert.assertFalse(
                            "Model is still waiting to close the tab",
                            model.isClosurePending(tab.getId()));
                    Assert.assertEquals(
                            "Model has more than the expected about:blank tab",
                            1,
                            model.getCount());
                });
    }

    // Regression test for crbug/1465745.
    @Test
    @LargeTest
    @Restriction(DeviceFormFactor.TABLET_OR_DESKTOP)
    public void testTabletCloseTabAndCommitDoesNotCrash() {
        WebPageStation secondPage =
                mActivityTestRule.startOnBlankPage().openFakeLinkToWebPage("about:blank");
        final ChromeTabbedActivity cta = secondPage.getActivity();
        TabStripUtils.settleDownCompositor(
                TabStripUtils.getStripLayoutHelperManager(cta).getStripLayoutHelper(false));

        TabModel model = cta.getTabModelSelector().getModel(/* incognito= */ false);
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    closeTabViaButton(cta, model.getTabAt(1).getId());
                    closeTabViaButton(cta, model.getTabAt(0).getId());

                    model.commitAllTabClosures();
                });
    }

    private void closeTabViaButton(ChromeTabbedActivity cta, int tabId) {
        final StripLayoutTab tab =
                TabStripUtils.findStripLayoutTab(cta, /* incognito= */ false, tabId);
        tab.getCloseButton()
                .handleClick(SystemClock.uptimeMillis(), MotionEventUtils.MOTION_EVENT_BUTTON_NONE);
    }
}
