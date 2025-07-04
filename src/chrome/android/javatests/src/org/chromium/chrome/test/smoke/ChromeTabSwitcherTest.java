// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.smoke;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import androidx.test.InstrumentationRegistry;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.LargeTest;
import androidx.test.internal.runner.junit4.AndroidJUnit4ClassRunner;

import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.RuleChain;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;

import org.chromium.base.Log;
import org.chromium.chrome.R;
import org.chromium.chrome.test.pagecontroller.rules.ChromeUiApplicationTestRule;
import org.chromium.chrome.test.pagecontroller.rules.ChromeUiAutomatorTestRule;
import org.chromium.chrome.test.pagecontroller.utils.IUi2Locator;
import org.chromium.chrome.test.pagecontroller.utils.Ui2Locators;
import org.chromium.chrome.test.pagecontroller.utils.UiAutomatorUtils;
import org.chromium.chrome.test.smoke.utilities.FirstRunNavigator;
import org.chromium.net.test.util.TestWebServer;

/** Basic Test for Chrome Android to switch Tabs. */
@LargeTest
// Not using BaseJUnit4ClassRunner in order to avoid initializing command-line flags prematurely.
@RunWith(AndroidJUnit4ClassRunner.class)
// Do not disable this test unless you are confident you can re-enable it quickly.
// This is the main test that prevents crash-on-launch bugs.
public class ChromeTabSwitcherTest {
    private static final String TAG = "SmokeTest";
    private static final String ACTIVITY_NAME = "com.google.android.apps.chrome.IntentDispatcher";
    private static final String TEST_PAGE_HTML =
            """
            <!DOCTYPE html>
            <html>
            Hello Smoke Test
            </html>
            """;

    private final IUi2Locator mTabSwitcherButton =
            Ui2Locators.withAnyResEntry(R.id.tab_switcher_button);

    private final IUi2Locator mHubToolbar = Ui2Locators.withAnyResEntry(R.id.hub_toolbar);

    private final IUi2Locator mTabList = Ui2Locators.withAnyResEntry(R.id.tab_list_recycler_view);

    private final IUi2Locator mNewTabButton = Ui2Locators.withContentDesc("New tab");
    private final IUi2Locator mNtpOmnibox = Ui2Locators.withAnyResEntry(R.id.search_box_text);

    private final FirstRunNavigator mFirstRunNavigator = new FirstRunNavigator();

    public static final long TIMEOUT_MS = 20000L;
    private String mPackageName;
    public ChromeUiAutomatorTestRule mRule = new ChromeUiAutomatorTestRule();
    public ChromeUiApplicationTestRule mChromeUiRule = new ChromeUiApplicationTestRule();
    @Rule public final TestRule mChain = RuleChain.outerRule(mChromeUiRule).around(mRule);
    private static TestWebServer sWebServer;

    @BeforeClass
    public static void setUpClass() throws Exception {
        sWebServer = TestWebServer.start();
    }

    @AfterClass
    public static void tearDownClass() {
        sWebServer.close();
    }

    @Before
    public void setUp() throws Exception {
        mPackageName =
                InstrumentationRegistry.getArguments()
                        .getString(
                                ChromeUiApplicationTestRule.PACKAGE_NAME_ARG,
                                "org.chromium.chrome");
    }

    @Test
    // Do not disable this test unless you are confident you can re-enable it quickly.
    // This is the main test that prevents crash-on-launch bugs.
    public void testTabSwitcher() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        String url = sWebServer.setResponse("/test.html", TEST_PAGE_HTML, null);
        final Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        intent.addCategory(Intent.CATEGORY_BROWSABLE);
        intent.addCategory(Intent.CATEGORY_DEFAULT);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.setComponent(new ComponentName(mPackageName, ACTIVITY_NAME));
        context.startActivity(intent);

        // Looks for the any view/layout with the chrome package name.
        IUi2Locator locatorChrome = Ui2Locators.withPackageName(mPackageName);
        // Wait until chrome shows up
        Log.i(TAG, "Attempting to navigate through FRE");
        UiAutomatorUtils.getInstance().waitUntilAnyVisible(locatorChrome);

        // Go through the FRE until you see ChromeTabbedActivity urlbar.
        Log.i(TAG, "Waiting for omnibox to show URL");
        mFirstRunNavigator.navigateThroughFRE();

        Log.i(TAG, "Waiting for omnibox to show URL");
        assert url.startsWith("http://");
        String urlWithoutScheme = url.substring(7);
        IUi2Locator dataUrlText = Ui2Locators.withText(urlWithoutScheme);
        UiAutomatorUtils.getInstance().getLocatorHelper().verifyOnScreen(dataUrlText);

        Log.i(TAG, "Waiting 5 seconds to ensure background logic does not crash");
        Thread.sleep(5000);

        Log.i(TAG, "Activating tab switcher.");
        UiAutomatorUtils.getInstance().click(mTabSwitcherButton);
        UiAutomatorUtils.getInstance().waitUntilAnyVisible(mHubToolbar);
        UiAutomatorUtils.getInstance().getLocatorHelper().verifyOnScreen(mTabList);

        Log.i(TAG, "Test complete.");
    }
}
