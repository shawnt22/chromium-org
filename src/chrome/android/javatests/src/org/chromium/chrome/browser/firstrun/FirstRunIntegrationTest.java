// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.firstrun;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;

import static org.mockito.Mockito.when;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.Instrumentation.ActivityMonitor;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;

import androidx.annotation.ColorInt;
import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;
import androidx.test.platform.app.InstrumentationRegistry;

import org.hamcrest.Matcher;
import org.hamcrest.Matchers;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.ActivityState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.BuildInfo;
import org.chromium.base.ThreadUtils;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.base.test.util.Criteria;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.DoNotBatch;
import org.chromium.base.test.util.Features;
import org.chromium.base.test.util.HistogramWatcher;
import org.chromium.base.test.util.MinAndroidSdkLevel;
import org.chromium.base.test.util.Restriction;
import org.chromium.base.test.util.ScalableTimeout;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.DeferredStartupHandler;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.customtabs.CustomTabActivity;
import org.chromium.chrome.browser.customtabs.CustomTabsIntentTestUtils;
import org.chromium.chrome.browser.document.ChromeLauncherActivity;
import org.chromium.chrome.browser.enterprise.util.EnterpriseInfo;
import org.chromium.chrome.browser.enterprise.util.FakeEnterpriseInfo;
import org.chromium.chrome.browser.firstrun.FirstRunActivityTestObserver.ScopedObserverData;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.locale.LocaleManager;
import org.chromium.chrome.browser.locale.LocaleManagerDelegate;
import org.chromium.chrome.browser.partnercustomizations.BasePartnerBrowserCustomizationIntegrationTestRule;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.privacy.settings.PrivacyPreferencesManagerImpl;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.search_engines.DefaultSearchEngineDialogHelperUtils;
import org.chromium.chrome.browser.search_engines.SearchEnginePromoType;
import org.chromium.chrome.browser.search_engines.TemplateUrlServiceFactory;
import org.chromium.chrome.browser.signin.SigninFirstRunFragment;
import org.chromium.chrome.browser.ui.signin.DialogWhenLargeContentLayout;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.util.browser.signin.SigninTestRule;
import org.chromium.components.browser_ui.edge_to_edge.EdgeToEdgeSystemBarColorHelper;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.components.externalauth.ExternalAuthUtils;
import org.chromium.components.policy.test.annotations.Policies;
import org.chromium.components.search_engines.TemplateUrl;
import org.chromium.components.signin.AccountManagerFacadeProvider;
import org.chromium.components.signin.test.util.FakeAccountManagerFacade;
import org.chromium.components.signin.test.util.TestAccounts;
import org.chromium.content_public.common.ContentUrlConstants;
import org.chromium.ui.test.util.DeviceRestriction;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/** Integration test suite for the first run experience. */
@RunWith(ChromeJUnit4ClassRunner.class)
@DoNotBatch(reason = "This test interacts with startup, native initialization, and first run.")
public class FirstRunIntegrationTest {
    private static final String TEST_URL = "https://test.com";
    private static final String FOO_URL = "https://foo.com";
    private static final long ACTIVITY_WAIT_LONG_MS = TimeUnit.SECONDS.toMillis(20);
    private static final String TEST_ENROLLMENT_TOKEN = "enrollment-token";

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Rule
    public BasePartnerBrowserCustomizationIntegrationTestRule mCustomizationRule =
            new BasePartnerBrowserCustomizationIntegrationTestRule();

    @Rule public SigninTestRule mSigninTestRule = new SigninTestRule();

    @Mock private ExternalAuthUtils mExternalAuthUtilsMock;

    private final Set<Class> mSupportedActivities =
            Set.of(
                    ChromeLauncherActivity.class,
                    FirstRunActivity.class,
                    ChromeTabbedActivity.class,
                    CustomTabActivity.class);
    private final Map<Class, ActivityMonitor> mMonitorMap = new HashMap<>();
    // The following is only used for tests which call {@code blockOnFlowIsKnown}. Otherwise, the
    // real implementation
    // of {@code AccountManagerFacade} is used with a {@code FakeAccountManagerDelegate}.
    private final FakeAccountManagerFacade mFakeAccountManagerFacade =
            new FakeAccountManagerFacade();

    private Instrumentation mInstrumentation;
    private Context mContext;

    private final FirstRunActivityTestObserver mTestObserver = new FirstRunActivityTestObserver();
    private Activity mLastActivity;

    @Before
    public void setUp() {
        when(mExternalAuthUtilsMock.canUseGooglePlayServices()).thenReturn(true);
        ExternalAuthUtils.setInstanceForTesting(mExternalAuthUtilsMock);
        FirstRunStatus.setFirstRunSkippedByPolicy(false);
        FirstRunUtils.setDisableDelayOnExitFreForTest(true);
        FirstRunActivity.setObserverForTest(mTestObserver);
        FirstRunActivity.disableAnimationForTesting(true);

        mInstrumentation = InstrumentationRegistry.getInstrumentation();
        mContext = mInstrumentation.getTargetContext();
        for (Class clazz : mSupportedActivities) {
            ActivityMonitor monitor = new ActivityMonitor(clazz.getName(), null, false);
            mMonitorMap.put(clazz, monitor);
            mInstrumentation.addMonitor(monitor);
        }

        mSigninTestRule.addAccount(TestAccounts.AADC_ADULT_ACCOUNT);
    }

    @After
    public void tearDown() {
        // Tear down the last activity first, otherwise the other cleanup, in particular skipped by
        // policy pref, might trigger an assert in activity initialization because of the statics
        // we reset below. Run it on UI so there are no threading issues.
        if (mLastActivity != null) {
            ThreadUtils.runOnUiThreadBlocking(() -> mLastActivity.finish());
        }
        // Finish the rest of the running activities.
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    for (Activity runningActivity : ApplicationStatus.getRunningActivities()) {
                        runningActivity.finish();
                    }
                });

        FirstRunActivity.disableAnimationForTesting(false);
        FirstRunStatus.setFirstRunSkippedByPolicy(false);
    }

    private ActivityMonitor getMonitor(Class activityClass) {
        Assert.assertTrue(mSupportedActivities.contains(activityClass));
        return mMonitorMap.get(activityClass);
    }

    private FirstRunActivity launchFirstRunActivity() {
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(TEST_URL));
        intent.setPackage(mContext.getPackageName());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mContext.startActivity(intent);

        // Because the AsyncInitializationActivity notices that the FRE hasn't been run yet, it
        // redirects to it.  Once the user closes the FRE, the user should be kicked back into the
        // startup flow where they were interrupted.
        return waitForFirstRunActivity();
    }

    private <T extends Activity> T waitForActivity(Class<T> activityClass) {
        Assert.assertTrue(mSupportedActivities.contains(activityClass));
        ActivityMonitor monitor = getMonitor(activityClass);
        mLastActivity = mInstrumentation.waitForMonitorWithTimeout(monitor, ACTIVITY_WAIT_LONG_MS);
        Assert.assertNotNull("Could not find " + activityClass.getName(), mLastActivity);
        return (T) mLastActivity;
    }

    private void skipTosDialogViaPolicy() {

        FakeEnterpriseInfo fakeEnterpriseInfo = new FakeEnterpriseInfo();
        fakeEnterpriseInfo.initialize(new EnterpriseInfo.OwnedState(true, false));
        EnterpriseInfo.setInstanceForTest(fakeEnterpriseInfo);
    }

    private void launchCustomTabs(String url) {
        mContext.startActivity(
                CustomTabsIntentTestUtils.createMinimalCustomTabIntent(mContext, url));
    }

    private void launchViewIntent(String url) {
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        intent.setPackage(mContext.getPackageName());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mContext.startActivity(intent);
    }

    private void launchMainIntent() {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.setPackage(mContext.getPackageName());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mContext.startActivity(intent);
    }

    private void clickThroughFirstRun(
            FirstRunActivity firstRunActivity, FirstRunPagesTestCase testCase) throws Exception {
        // Start FRE.
        FirstRunNavigationHelper navigationHelper = new FirstRunNavigationHelper(firstRunActivity);
        navigationHelper.ensurePagesCreationSucceeded();
        if (testCase.shouldSignIn()) {
            navigationHelper.continueAndSignIn();
        } else {
            navigationHelper.dismissSigninPromo();
        }

        if (testCase.searchPromoType() == SearchEnginePromoType.DONT_SHOW) {
            navigationHelper.ensureDefaultSearchEnginePromoNotCurrentPage();
        } else {
            navigationHelper.selectDefaultSearchEngine();
        }

        if (testCase.shouldShowHistorySyncPromo()) {
            navigationHelper.dismissHistorySync();
        } else {
            navigationHelper.ensureHistorySyncNotCurrentPage();
        }
    }

    private void verifyUrlEquals(String expected, Uri actual) {
        Assert.assertEquals(
                "Expected " + expected + " did not match actual " + actual,
                Uri.parse(expected),
                actual);
    }

    private FirstRunActivity waitForFirstRunActivity() {
        return (FirstRunActivity) waitForActivity(FirstRunActivity.class);
    }

    private CustomTabActivity waitForCustomTabActivity() {
        return (CustomTabActivity) waitForActivity(CustomTabActivity.class);
    }

    /**
     * When launching a second Chrome, the new FRE should replace the old FRE. In order to know when
     * the second FirstRunActivity is ready, use object inequality with old one.
     *
     * @param previousFreActivity The previous activity.
     */
    private FirstRunActivity waitForDifferentFirstRunActivity(
            FirstRunActivity previousFreActivity) {
        CriteriaHelper.pollInstrumentationThread(
                () -> {
                    for (Activity runningActivity : ApplicationStatus.getRunningActivities()) {
                        @ActivityState
                        int state = ApplicationStatus.getStateForActivity(runningActivity);
                        if (runningActivity.getClass() == FirstRunActivity.class
                                && runningActivity != previousFreActivity
                                && (state == ActivityState.STARTED
                                        || state == ActivityState.RESUMED)) {
                            mLastActivity = runningActivity;
                            return true;
                        }
                    }
                    return false;
                },
                "Did not find a different FirstRunActivity from " + previousFreActivity,
                /* maxTimeoutMs= */ ACTIVITY_WAIT_LONG_MS,
                /* checkIntervalMs= */ CriteriaHelper.DEFAULT_POLLING_INTERVAL);

        CriteriaHelper.pollInstrumentationThread(
                previousFreActivity::isFinishing,
                "The original FirstRunActivity should be finished, instead "
                        + ApplicationStatus.getStateForActivity(previousFreActivity));
        return (FirstRunActivity) mLastActivity;
    }

    private <T extends ChromeActivity> Uri waitAndGetUriFromChromeActivity(Class<T> activityClass) {
        ChromeActivity chromeActivity = waitForActivity(activityClass);
        return chromeActivity.getIntent().getData();
    }

    private ScopedObserverData getObserverData(FirstRunActivity freActivity) {
        return mTestObserver.getScopedObserverData(freActivity);
    }

    private FakeAccountManagerFacade.UpdateBlocker blockOnFlowIsKnown() {
        AccountManagerFacadeProvider.setInstanceForTests(mFakeAccountManagerFacade);
        return mFakeAccountManagerFacade.blockGetAccounts(/* populateCache= */ false);
    }

    @Test
    @MediumTest
    public void startPartnerCustomizationDuringFre() {
        launchFirstRunActivity();
        CriteriaHelper.pollInstrumentationThread(
                () -> PartnerBrowserCustomizations.getInstance().isInitialized());
    }

    @Test
    @MediumTest
    public void startPartnerCustomizationFromMainIntent() {
        launchMainIntent();
        CriteriaHelper.pollInstrumentationThread(
                () -> PartnerBrowserCustomizations.getInstance().isInitialized());
    }

    @Test
    @SmallTest
    public void testHelpPageSkipsFirstRun() {
        // Fire an Intent to load a generic URL.
        CustomTabActivity.showInfoPage(mContext, TEST_URL);

        // The original activity should be started because it's a "help page".
        waitForActivity(CustomTabActivity.class);
        Assert.assertFalse(mLastActivity.isFinishing());

        // First run should be skipped for this Activity.
        Assert.assertEquals(0, getMonitor(FirstRunActivity.class).getHits());
    }

    @Test
    @SmallTest
    public void testAbortFirstRun() throws Exception {
        launchViewIntent(TEST_URL);
        Activity chromeLauncherActivity = waitForActivity(ChromeLauncherActivity.class);

        // Because the ChromeLauncherActivity notices that the FRE hasn't been run yet, it
        // redirects to it.
        FirstRunActivity firstRunActivity = waitForFirstRunActivity();

        // Once the user closes the FRE, the user should be kicked back into the
        // startup flow where they were interrupted.
        ScopedObserverData scopedObserverData = getObserverData(firstRunActivity);
        Assert.assertEquals(0, scopedObserverData.abortFirstRunExperienceCallback.getCallCount());
        ThreadUtils.runOnUiThreadBlocking(mLastActivity::onBackPressed);
        scopedObserverData.abortFirstRunExperienceCallback.waitForCallback(
                "FirstRunActivity didn't abort", 0);

        CriteriaHelper.pollInstrumentationThread(() -> mLastActivity.isFinishing());

        // ChromeLauncherActivity should finish if FRE was aborted.
        CriteriaHelper.pollInstrumentationThread(chromeLauncherActivity::isFinishing);
    }

    // TODO(crbug.com/40785454): Add test cases for the new Welcome screen that includes the
    // Sign-in promo once the sign-in components can be disabled by policy.

    // TODO(crbug.com/40794359): Add test cases for ToS page disabled by policy after the
    // user accepted ToS and aborted first run.

    @Test
    @MediumTest
    public void testFirstRunPages_NoCctPolicy_AbsenceOfPromos() throws Exception {
        runFirstRunPagesTest(new FirstRunPagesTestCase());
    }

    @Test
    @MediumTest
    public void testFirstRunPages_NoCctPolicy_SearchPromo() throws Exception {
        runFirstRunPagesTest(new FirstRunPagesTestCase().withSearchPromo());
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testFirstRunPages_NoCctPolicy_SearchPromo_HistorySyncPromo() throws Exception {
        runFirstRunPagesTest(new FirstRunPagesTestCase().withSearchPromo().withHistorySyncPromo());
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testFirstRunPages_NoCctPolicy_HistorySyncPromo() throws Exception {
        runFirstRunPagesTest(new FirstRunPagesTestCase().withHistorySyncPromo());
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testFirstRunPages_NoCctPolicy_OnBackPressed() throws Exception {
        initializePreferences(FirstRunPagesTestCase.createWithShowAllPromos());

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go until the last page without skipping the last one, go back until initial page, and
        // then complete first run.
        new FirstRunNavigationHelper(firstRunActivity)
                .ensurePagesCreationSucceeded()
                .continueAndSignIn()
                .selectDefaultSearchEngine()
                .ensureHistorySyncIsCurrentPage()
                .goBackToPreviousPage()
                .ensureDefaultSearchEnginePromoIsCurrentPage()
                .goBackToPreviousPage()
                .ensureWelcomePageIsCurrentPage()
                .continueAndSignIn()
                .selectDefaultSearchEngine()
                .dismissHistorySync();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testFirstRunPages_WithCctPolicy_OnBackPressed() throws Exception {
        initializePreferences(FirstRunPagesTestCase.createWithShowAllPromos().withCctTosDisabled());

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go until the last page without skipping the last one, go back until initial page, and
        // then complete first run.
        new FirstRunNavigationHelper(firstRunActivity)
                .ensurePagesCreationSucceeded()
                .continueAndSignIn()
                .selectDefaultSearchEngine()
                .ensureHistorySyncIsCurrentPage()
                .goBackToPreviousPage()
                .ensureDefaultSearchEnginePromoIsCurrentPage()
                .goBackToPreviousPage()
                .ensureWelcomePageIsCurrentPage()
                .continueAndSignIn()
                .selectDefaultSearchEngine()
                .dismissHistorySync();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @MediumTest
    public void testSigninFirstRunPages_WithCctPolicy_AbsenceOfPromos() throws Exception {
        runFirstRunPagesTest(new FirstRunPagesTestCase().withCctTosDisabled());
    }

    @Test
    @MediumTest
    public void testSigninFirstRunPages_WithCctPolicy_SearchPromo() throws Exception {
        runFirstRunPagesTest(new FirstRunPagesTestCase().withCctTosDisabled().withSearchPromo());
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testSigninFirstRunPages_WithCctPolicy_SearchPromo_HistorySyncPromo()
            throws Exception {
        runFirstRunPagesTest(
                new FirstRunPagesTestCase()
                        .withCctTosDisabled()
                        .withSearchPromo()
                        .withHistorySyncPromo());
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testSigninFirstRunPages_WithCctPolicy_SigninPromo() throws Exception {
        runFirstRunPagesTest(
                new FirstRunPagesTestCase().withCctTosDisabled().withHistorySyncPromo());
    }

    private void runFirstRunPagesTest(FirstRunPagesTestCase testCase) throws Exception {
        initializePreferences(testCase);

        FirstRunActivity firstRunActivity = launchFirstRunActivity();
        clickThroughFirstRun(firstRunActivity, testCase);

        // FRE should be completed now, which will kick the user back into the interrupted flow.
        // In this case, the user gets sent to the ChromeTabbedActivity after a View Intent is
        // processed by ChromeLauncherActivity.
        getObserverData(firstRunActivity)
                .updateCachedEngineCallback
                .waitForCallback("Failed to alert search widgets that an update is necessary", 0);
        waitForActivity(ChromeTabbedActivity.class);
    }

    private void initializePreferences(FirstRunPagesTestCase testCase) {
        if (testCase.cctTosDisabled()) skipTosDialogViaPolicy();

        FirstRunFlowSequencer.setDelegateFactoryForTesting(
                (profileProvider) ->
                        new TestFirstRunFlowSequencerDelegate(testCase, profileProvider));

        setUpLocaleManagerDelegate(testCase.searchPromoType());
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testFirstRunPages_ProgressHistogramRecordedOnlyOnce() throws Exception {
        HistogramWatcher histograms =
                HistogramWatcher.newBuilder()
                        .expectIntRecords(
                                "MobileFre.Progress.ViewIntent",
                                MobileFreProgress.STARTED,
                                MobileFreProgress.WELCOME_SHOWN,
                                MobileFreProgress.WELCOME_SIGNIN_WITH_DEFAULT_ACCOUNT,
                                MobileFreProgress.HISTORY_SYNC_OPT_IN_SHOWN,
                                MobileFreProgress.HISTORY_SYNC_DISMISSED,
                                MobileFreProgress.DEFAULT_SEARCH_ENGINE_SHOWN)
                        .build();
        initializePreferences(FirstRunPagesTestCase.createWithShowAllPromos());

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go until the last page without skipping the last one, go back until initial page, and
        // then complete first run.
        new FirstRunNavigationHelper(firstRunActivity)
                .ensurePagesCreationSucceeded()
                .continueAndSignIn()
                .selectDefaultSearchEngine()
                .ensureHistorySyncIsCurrentPage()
                .goBackToPreviousPage()
                .ensureDefaultSearchEnginePromoIsCurrentPage()
                .goBackToPreviousPage()
                .ensureWelcomePageIsCurrentPage()
                .continueAndSignIn()
                .selectDefaultSearchEngine()
                .dismissHistorySync();

        waitForActivity(ChromeTabbedActivity.class);

        histograms.assertExpected();
    }

    @Test
    @MediumTest
    public void testFirstRunPages_ProgressHistogramRecording_NoPromos() throws Exception {
        HistogramWatcher.Builder histogramBuilder =
                HistogramWatcher.newBuilder()
                        .expectIntRecords(
                                "MobileFre.Progress.ViewIntent",
                                MobileFreProgress.STARTED,
                                MobileFreProgress.WELCOME_SHOWN);
        // There is no dismiss button on automotive devices.
        if (!BuildInfo.getInstance().isAutomotive) {
            histogramBuilder.expectIntRecord(
                    "MobileFre.Progress.ViewIntent", MobileFreProgress.WELCOME_DISMISS);
        }
        HistogramWatcher histograms = histogramBuilder.build();

        initializePreferences(new FirstRunPagesTestCase());

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        new FirstRunNavigationHelper(firstRunActivity)
                .ensurePagesCreationSucceeded()
                .dismissSigninPromo();

        waitForActivity(ChromeTabbedActivity.class);

        histograms.assertExpected();
    }

    @Test
    @MediumTest
    @DisabledTest(message = "https://crbug.com/1221647")
    public void testExitFirstRunWithPolicy() throws Exception {
        initializePreferences(new FirstRunPagesTestCase().withCctTosDisabled());

        Intent intent = CustomTabsIntentTestUtils.createMinimalCustomTabIntent(mContext, TEST_URL);
        mContext.startActivity(intent);

        FirstRunActivity freActivity = waitForFirstRunActivity();
        CriteriaHelper.pollUiThread(
                () -> freActivity.getSupportFragmentManager().getFragments().size() > 0);
        // Make sure native is initialized so that the subsequent transition is not blocked.
        CriteriaHelper.pollUiThread(
                () -> freActivity.getNativeInitializationPromise().isFulfilled(),
                "native never initialized.");

        waitForActivity(CustomTabActivity.class);
        Assert.assertFalse(
                "Usage and crash reporting pref was set to true after skip",
                PrivacyPreferencesManagerImpl.getInstance().isUsageAndCrashReportingPermitted());
        Assert.assertTrue(
                "FRE should be skipped for CCT.", FirstRunStatus.isFirstRunSkippedByPolicy());
    }

    @Test
    @MediumTest
    @DisabledTest(message = "issuetracker.google.com/360931705")
    public void testFirstRunSkippedSharedPreferenceRefresh() throws Exception {
        // Set that the first run was previous skipped by policy in shared preference, then
        // refreshing shared preference should cause its value to become false, since there's no
        // policy set in this test case.
        FirstRunStatus.setFirstRunSkippedByPolicy(true);

        Intent intent =
                CustomTabsIntentTestUtils.createMinimalCustomTabIntent(
                        mContext, ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);
        mContext.startActivity(intent);
        CustomTabActivity activity = waitForActivity(CustomTabActivity.class);
        CriteriaHelper.pollUiThreadLongTimeout(
                "Native init never completed", activity::didFinishNativeInitialization);

        // DeferredStartupHandler could not finish with CriteriaHelper#DEFAULT_MAX_TIME_TO_POLL.
        // Use longer timeout here to avoid flakiness. See https://crbug.com/1157611.
        CriteriaHelper.pollUiThread(activity::deferredStartupPostedForTesting);
        Assert.assertTrue(
                "Deferred startup never completed",
                DeferredStartupHandler.waitForDeferredStartupCompleteForTesting(
                        ScalableTimeout.scaleTimeout(ACTIVITY_WAIT_LONG_MS)));

        // FirstRun status should be refreshed by TosDialogBehaviorSharedPrefInvalidator in deferred
        // start up task.
        CriteriaHelper.pollUiThread(() -> !FirstRunStatus.isFirstRunSkippedByPolicy());
    }

    @Test
    @MediumTest
    public void testSkipTosPage() throws TimeoutException {
        // Test case that verifies when the ToS Page was previously accepted, launching the FRE
        // should transition to the next page.
        FirstRunStatus.setSkipWelcomePage(true);

        FirstRunActivity freActivity = launchFirstRunActivity();
        CriteriaHelper.pollUiThread(
                () -> freActivity.getSupportFragmentManager().getFragments().size() > 0);

        getObserverData(freActivity)
                .jumpToPageCallback
                .waitForCallback("Welcome page should be skipped.", 0);
    }

    @Test
    @MediumTest
    // A fake AppRestriction is injected in order to trigger the corresponding code in
    // AppRestrictionsProvider.
    @Policies.Add(@Policies.Item(key = "NoncePolicy", string = "true"))
    // TODO(crbug.com/40142602): Change this test case when policy can handle cases when ToS
    // is accepted in Browser App.
    public void testSkipTosPage_WithCctPolicy() throws Exception {
        skipTosDialogViaPolicy();
        FirstRunStatus.setSkipWelcomePage(true);

        Intent intent = CustomTabsIntentTestUtils.createMinimalCustomTabIntent(mContext, TEST_URL);
        mContext.startActivity(intent);

        FirstRunActivity freActivity = waitForFirstRunActivity();
        CriteriaHelper.pollUiThread(
                () -> freActivity.getSupportFragmentManager().getFragments().size() > 0);

        // A page skip should happen, while we are still staying at FRE.
        getObserverData(freActivity)
                .jumpToPageCallback
                .waitForCallback("Welcome page should be skipped.", 0);
        Assert.assertFalse(
                "FRE should not be skipped for CCT.", FirstRunStatus.isFirstRunSkippedByPolicy());
        Assert.assertFalse(
                "FreActivity should still be alive.", freActivity.isActivityFinishingOrDestroyed());
    }

    @Test
    @MediumTest
    public void testFastDestroy() {
        // Inspired by crbug.com/1119548, where onDestroy() before triggerLayoutInflation() caused
        // a crash.
        Intent intent = CustomTabsIntentTestUtils.createMinimalCustomTabIntent(mContext, TEST_URL);
        mContext.startActivity(intent);
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testMultipleFresCustomIntoView() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        launchCustomTabs(TEST_URL);
        FirstRunActivity firstFreActivity = waitForFirstRunActivity();

        launchViewIntent(FOO_URL);
        FirstRunActivity secondFreActivity = waitForDifferentFirstRunActivity(firstFreActivity);

        clickThroughFirstRun(secondFreActivity, testCase);
        verifyUrlEquals(FOO_URL, waitAndGetUriFromChromeActivity(ChromeTabbedActivity.class));
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testMultipleFresViewIntoCustom() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        launchViewIntent(TEST_URL);
        FirstRunActivity firstFreActivity = waitForFirstRunActivity();

        launchCustomTabs(FOO_URL);
        FirstRunActivity secondFreActivity = waitForDifferentFirstRunActivity(firstFreActivity);

        clickThroughFirstRun(secondFreActivity, testCase);
        verifyUrlEquals(FOO_URL, waitAndGetUriFromChromeActivity(CustomTabActivity.class));
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testMultipleFresBothView() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        launchViewIntent(TEST_URL);
        FirstRunActivity firstFreActivity = waitForFirstRunActivity();

        launchViewIntent(FOO_URL);
        FirstRunActivity secondFreActivity = waitForDifferentFirstRunActivity(firstFreActivity);

        clickThroughFirstRun(secondFreActivity, testCase);
        verifyUrlEquals(FOO_URL, waitAndGetUriFromChromeActivity(ChromeTabbedActivity.class));
    }

    @Test
    @MediumTest
    public void testMultipleFresBackButton() throws Exception {
        launchViewIntent(TEST_URL);
        FirstRunActivity firstFreActivity = waitForFirstRunActivity();

        launchViewIntent(TEST_URL);
        FirstRunActivity secondFreActivity = waitForDifferentFirstRunActivity(firstFreActivity);

        ScopedObserverData secondFreData = getObserverData(secondFreActivity);
        Assert.assertEquals(
                "Second FRE should not have aborted before back button is pressed.",
                0,
                secondFreData.abortFirstRunExperienceCallback.getCallCount());

        Assert.assertTrue(
                "FirstRunActivity should intercept back press",
                secondFreActivity.getOnBackPressedDispatcher().hasEnabledCallbacks());
        ThreadUtils.runOnUiThreadBlocking(
                secondFreActivity.getOnBackPressedDispatcher()::onBackPressed);
        secondFreData.abortFirstRunExperienceCallback.waitForCallback(
                "Second FirstRunActivity didn't abort", 0);
        CriteriaHelper.pollInstrumentationThread(
                secondFreActivity::isFinishing, "Second FRE should be finishing now.");
    }

    @Test
    @MediumTest
    public void testNativeInitBeforeFragment() throws Exception {
        FirstRunPagesTestCase testCase = new FirstRunPagesTestCase().withoutSignIn();
        initializePreferences(testCase);

        // Inspired by https://crbug.com/1207683 where a notification was dropped because native
        // initialized before the first fragment was attached to the activity.
        FirstRunActivity firstRunActivity;
        try (var ignored = blockOnFlowIsKnown()) {
            launchViewIntent(TEST_URL);
            firstRunActivity = waitForFirstRunActivity();
            CriteriaHelper.pollUiThread(
                    () -> firstRunActivity.getNativeInitializationPromise().isFulfilled(),
                    "native never initialized.");
        }

        clickThroughFirstRun(firstRunActivity, testCase);
        verifyUrlEquals(TEST_URL, waitAndGetUriFromChromeActivity(ChromeTabbedActivity.class));
    }

    @Test
    @MediumTest
    @Features.EnableFeatures({ChromeFeatureList.CCT_FRE_IN_SAME_TASK})
    public void testLaunchFirstRunInSameTask() throws Exception {
        launchCustomTabs(TEST_URL);
        FirstRunActivity firstRunActivity = waitForFirstRunActivity();
        CriteriaHelper.pollUiThread(
                () -> firstRunActivity.getNativeInitializationPromise().isFulfilled(),
                "native never initialized.");

        clickThroughFirstRun(firstRunActivity, new FirstRunPagesTestCase().withoutSignIn());
        CustomTabActivity customTabActivity = waitForCustomTabActivity();
        Assert.assertEquals(
                "FirstRun and CustomTab should be opened in the same task",
                firstRunActivity.getTaskId(),
                customTabActivity.getTaskId());
    }

    @Test
    @MediumTest
    @Policies.Add(@Policies.Item(key = "ForceSafeSearch", string = "true"))
    // Child accounts are not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testSigninFirstRunPageShownBeforeChildStatusFetch() throws Exception {
        // ChildAccountStatusSupplier uses AppRestrictions to quickly detect non-supervised cases,
        // so pretend there are AppRestrictions set by FamilyLink.
        try (var ignored = blockOnFlowIsKnown()) {
            initializePreferences(new FirstRunPagesTestCase());

            FirstRunActivity firstRunActivity = launchFirstRunActivity();
            new FirstRunNavigationHelper(firstRunActivity).ensureWelcomePageIsCurrentPage();
            ThreadUtils.runOnUiThreadBlocking(
                    () -> {
                        ProgressBar progressBar =
                                ((SigninFirstRunFragment)
                                                firstRunActivity.getCurrentFragmentForTesting())
                                        .getView()
                                        .findViewById(
                                                R.id.fre_native_and_policy_load_progress_spinner);
                        // Replace the progress bar with a placeholder to allow other checks.
                        // Currently
                        // the progress bar cannot be stopped otherwise due to some espresso issues
                        // (crbug.com/1115067).
                        progressBar.setIndeterminateDrawable(
                                new ColorDrawable(
                                        SemanticColorUtils.getDefaultBgColor(firstRunActivity)));
                    });

            onView(withId(R.id.fre_logo)).check(matches(isDisplayed()));
            onView(withId(R.id.fre_native_and_policy_load_progress_spinner))
                    .check(matches(isDisplayed()));
        }
    }

    @Test
    @MediumTest
    public void testSigninFirstRunLoadPointHistograms() throws Exception {
        var histograms =
                HistogramWatcher.newBuilder()
                        .expectAnyRecord("MobileFre.FromLaunch.ChildStatusAvailable")
                        .expectAnyRecord("MobileFre.FromLaunch.PoliciesLoaded")
                        .build();
        initializePreferences(new FirstRunPagesTestCase());

        FirstRunActivity firstRunActivity = launchFirstRunActivity();
        new FirstRunNavigationHelper(firstRunActivity)
                .ensurePagesCreationSucceeded()
                .ensureWelcomePageIsCurrentPage();

        histograms.pollInstrumentationThreadUntilSatisfied();
    }

    @Test
    @MediumTest
    // A fake AppRestriction is injected in order to trigger the corresponding code in
    // AppRestrictionsProvider.
    @Policies.Add(@Policies.Item(key = "NoncePolicy", string = "true"))
    public void testNativeInitBeforeFragmentSkip() throws Exception {
        FirstRunPagesTestCase testCase = new FirstRunPagesTestCase().withoutSignIn();
        initializePreferences(testCase);
        skipTosDialogViaPolicy();

        FirstRunActivity firstRunActivity;
        try (var ignored = blockOnFlowIsKnown()) {
            launchCustomTabs(TEST_URL);
            firstRunActivity = waitForFirstRunActivity();
            CriteriaHelper.pollUiThread(
                    () -> firstRunActivity.getNativeInitializationPromise().isFulfilled(),
                    "native never initialized.");
        }

        clickThroughFirstRun(firstRunActivity, testCase);
        verifyUrlEquals(TEST_URL, waitAndGetUriFromChromeActivity(CustomTabActivity.class));
    }

    @Test
    @MediumTest
    @Policies.Add(
            @Policies.Item(
                    key = "CloudManagementEnrollmentToken",
                    string = TEST_ENROLLMENT_TOKEN))
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testCloudManagementDoesNotBlockFirstRun() throws Exception {
        // Ensures FRE is not blocked if cloud management is enabled.
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        launchViewIntent(TEST_URL);
        FirstRunActivity firstRunActivity = waitForFirstRunActivity();
        clickThroughFirstRun(firstRunActivity, testCase);
        verifyUrlEquals(TEST_URL, waitAndGetUriFromChromeActivity(ChromeTabbedActivity.class));
    }

    private void setUpLocaleManagerDelegate(@SearchEnginePromoType final int searchPromoType) {
        // Force the LocaleManager into a specific state.
        LocaleManagerDelegate mockDelegate =
                new LocaleManagerDelegate() {
                    @Override
                    public int getSearchEnginePromoShowType() {
                        return searchPromoType;
                    }

                    @Override
                    public List<TemplateUrl> getSearchEnginesForPromoDialog(int promoType) {
                        return TemplateUrlServiceFactory.getForProfile(
                                        ProfileManager.getLastUsedRegularProfile())
                                .getTemplateUrls();
                    }
                };
        ThreadUtils.runOnUiThreadBlocking(
                () -> LocaleManager.getInstance().setDelegateForTest(mockDelegate));
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testPrefsUpdated_allPagesAlreadyShown() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go until the last page without skipping the last one.
        FirstRunNavigationHelper navigationHelper =
                new FirstRunNavigationHelper(firstRunActivity)
                        .ensurePagesCreationSucceeded()
                        .continueAndSignIn()
                        .selectDefaultSearchEngine()
                        .ensureHistorySyncIsCurrentPage();

        // Change preferences to disable all promos.
        testCase.setSearchPromoType(SearchEnginePromoType.DONT_SHOW);
        testCase.setShouldShowHistorySyncPromo(false);

        // Go back should skip all the promo pages and reach the terms of service page. Accepting
        // sign-in completes first run.
        navigationHelper
                .goBackToPreviousPage()
                .ensureWelcomePageIsCurrentPage()
                .continueAndSignIn();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testPrefsUpdated_noPagesShown() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Show welcome page.
        FirstRunNavigationHelper navigationHelper =
                new FirstRunNavigationHelper(firstRunActivity)
                        .ensurePagesCreationSucceeded()
                        .ensureWelcomePageIsCurrentPage();

        // Change preferences before any promo page is shown.
        testCase.setSearchPromoType(SearchEnginePromoType.DONT_SHOW);
        testCase.setShouldShowHistorySyncPromo(false);

        // Accepting sign-in should complete first run, since all the promos are disabled.
        navigationHelper
                .continueAndSignIn()
                .ensureDefaultSearchEnginePromoNotCurrentPage()
                .ensureHistorySyncNotCurrentPage();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testPrefsUpdated_searchEnginePromoDisableAfterPromoShown() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go until the last page without skipping the last one.
        FirstRunNavigationHelper navigationHelper =
                new FirstRunNavigationHelper(firstRunActivity)
                        .ensurePagesCreationSucceeded()
                        .continueAndSignIn()
                        .selectDefaultSearchEngine()
                        .ensureHistorySyncIsCurrentPage();

        // Disable search engine prompt after the next page is shown.
        testCase.setSearchPromoType(SearchEnginePromoType.DONT_SHOW);
        setUpLocaleManagerDelegate(SearchEnginePromoType.DONT_SHOW);

        // Go back until initial page, and then complete first run. The search engine prompt
        // shouldn't be shown again in either direction.
        navigationHelper
                .goBackToPreviousPage()
                .ensureDefaultSearchEnginePromoNotCurrentPage()
                .continueAndSignIn()
                .ensureDefaultSearchEnginePromoNotCurrentPage()
                .dismissHistorySync();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testPrefsUpdated_searchEnginePromoDisabledWhilePromoShown() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go over first run prompts and stop at the search engine page.
        FirstRunNavigationHelper navigationHelper =
                new FirstRunNavigationHelper(firstRunActivity)
                        .ensurePagesCreationSucceeded()
                        .continueAndSignIn()
                        .ensureDefaultSearchEnginePromoIsCurrentPage();

        // Disable search engine prompt while it's shown. This will not hide the page.
        testCase.setSearchPromoType(SearchEnginePromoType.DONT_SHOW);
        setUpLocaleManagerDelegate(SearchEnginePromoType.DONT_SHOW);

        // Pass the search engine prompt, and move to the last page without skipping it.
        // Go back until initial page, and then complete first run. The search engine prompt
        // shouldn't be shown again in either direction.
        navigationHelper
                .selectDefaultSearchEngine()
                .ensureHistorySyncIsCurrentPage()
                .goBackToPreviousPage()
                .ensureDefaultSearchEnginePromoNotCurrentPage()
                .dismissSigninPromo()
                .ensureDefaultSearchEnginePromoNotCurrentPage();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @MediumTest
    // Sign-in is not supported on automotive devices.
    @Restriction({DeviceRestriction.RESTRICTION_TYPE_NON_AUTO})
    public void testPrefsUpdated_historySyncPromoPromoDisabledWhilePromoShown() throws Exception {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);

        FirstRunActivity firstRunActivity = launchFirstRunActivity();

        // Go until the last page without skipping the last one.
        FirstRunNavigationHelper navigationHelper =
                new FirstRunNavigationHelper(firstRunActivity)
                        .ensurePagesCreationSucceeded()
                        .continueAndSignIn()
                        .selectDefaultSearchEngine()
                        .ensureHistorySyncIsCurrentPage();

        // Disable history sync promo while it's shown. This will not hide the page.
        testCase.setShouldShowHistorySyncPromo(false);

        // Go back until initial page, and then complete first run. The history sync promo shouldn't
        // be shown again.
        navigationHelper
                .goBackToPreviousPage()
                .ensureDefaultSearchEnginePromoIsCurrentPage()
                .goBackToPreviousPage()
                .continueAndSignIn()
                .selectDefaultSearchEngine();

        waitForActivity(ChromeTabbedActivity.class);
    }

    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.R)
    // Automotive devices do not support coloring the system bars.
    @Restriction(DeviceRestriction.RESTRICTION_TYPE_NON_AUTO)
    @Features.EnableFeatures({ChromeFeatureList.EDGE_TO_EDGE_EVERYWHERE})
    public void testEdgeToEdgeEverywhere() {
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);
        FirstRunActivity activity = launchFirstRunActivity();

        EdgeToEdgeSystemBarColorHelper edgeToEdgeSystemBarColorHelper =
                activity.getEdgeToEdgeManager().getEdgeToEdgeSystemBarColorHelper();
        @ColorInt int backgroundColor;
        if (DialogWhenLargeContentLayout.shouldShowAsDialog(activity)) {
            backgroundColor = DialogWhenLargeContentLayout.getDialogBackgroundColor(activity);
        } else {
            backgroundColor = SemanticColorUtils.getDefaultBgColor(activity);
        }
        Assert.assertEquals(backgroundColor, edgeToEdgeSystemBarColorHelper.getStatusBarColor());
        Assert.assertEquals(
                backgroundColor, edgeToEdgeSystemBarColorHelper.getNavigationBarColor());
    }

    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.R)
    // Automotive devices do not support coloring the system bars.
    @Restriction(DeviceRestriction.RESTRICTION_TYPE_NON_AUTO)
    @Features.EnableFeatures({ChromeFeatureList.EDGE_TO_EDGE_EVERYWHERE})
    public void testEdgeToEdgeEverywhere_testLargeContentLayout() {
        DialogWhenLargeContentLayout.enableShouldShowAsDialogForTesting(
                /* shouldShowAsDialog= */ true);
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);
        FirstRunActivity activity = launchFirstRunActivity();

        EdgeToEdgeSystemBarColorHelper edgeToEdgeSystemBarColorHelper =
                activity.getEdgeToEdgeManager().getEdgeToEdgeSystemBarColorHelper();

        @ColorInt
        int backgroundColor = DialogWhenLargeContentLayout.getDialogBackgroundColor(activity);
        Assert.assertEquals(backgroundColor, edgeToEdgeSystemBarColorHelper.getStatusBarColor());
        Assert.assertEquals(
                backgroundColor, edgeToEdgeSystemBarColorHelper.getNavigationBarColor());
    }

    @Test
    @SmallTest
    @Features.DisableFeatures({ChromeFeatureList.EDGE_TO_EDGE_EVERYWHERE})
    public void testLargeContentLayout() {
        DialogWhenLargeContentLayout.enableShouldShowAsDialogForTesting(
                /* shouldShowAsDialog= */ true);
        FirstRunPagesTestCase testCase = FirstRunPagesTestCase.createWithShowAllPromos();
        initializePreferences(testCase);
        FirstRunActivity activity = launchFirstRunActivity();
        Assert.assertEquals(
                Color.BLACK, activity.getWindowAndroid().getWindow().getStatusBarColor());
    }

    private void clickButton(final Activity activity, final int id, final String message) {
        CriteriaHelper.pollUiThread(
                () -> {
                    View view = activity.findViewById(id);
                    Criteria.checkThat(view, Matchers.notNullValue());
                    Criteria.checkThat(view.getVisibility(), Matchers.is(View.VISIBLE));
                    Criteria.checkThat(view.isEnabled(), Matchers.is(true));
                });

        PostTask.runOrPostTask(
                TaskTraits.UI_DEFAULT,
                () -> {
                    Button button = activity.findViewById(id);
                    Assert.assertNotNull(message, button);
                    button.performClick();
                });
    }

    /** Configuration for tests that depend on showing First Run pages. */
    static class FirstRunPagesTestCase {
        private boolean mCctTosDisabled;
        private @SearchEnginePromoType int mSearchPromoType = SearchEnginePromoType.DONT_SHOW;
        private boolean mShowHistorySyncPromo;
        private boolean mShouldSignIn;

        boolean cctTosDisabled() {
            return mCctTosDisabled;
        }

        @SearchEnginePromoType
        int searchPromoType() {
            return mSearchPromoType;
        }

        boolean showSearchPromo() {
            return mSearchPromoType == SearchEnginePromoType.SHOW_NEW
                    || mSearchPromoType == SearchEnginePromoType.SHOW_EXISTING;
        }

        boolean shouldShowHistorySyncPromo() {
            return mShowHistorySyncPromo;
        }

        boolean shouldSignIn() {
            return mShouldSignIn;
        }

        FirstRunPagesTestCase setCctTosDisabled() {
            mCctTosDisabled = true;
            return this;
        }

        FirstRunPagesTestCase setSearchPromoType(@SearchEnginePromoType int searchPromoType) {
            mSearchPromoType = searchPromoType;
            return this;
        }

        FirstRunPagesTestCase setShouldShowHistorySyncPromo(boolean showHistorySyncPromo) {
            mShowHistorySyncPromo = showHistorySyncPromo;
            // The history sync screen can only appear if the user is signed in.
            mShouldSignIn = true;
            return this;
        }

        FirstRunPagesTestCase setShouldSignIn(boolean shouldSignIn) {
            mShouldSignIn = shouldSignIn;
            // The history sync screen can only appear if the user is signed in.
            assert mShouldSignIn || !mShowHistorySyncPromo;
            return this;
        }

        FirstRunPagesTestCase withCctTosDisabled() {
            return setCctTosDisabled();
        }

        FirstRunPagesTestCase withSearchPromo() {
            return setSearchPromoType(SearchEnginePromoType.SHOW_EXISTING);
        }

        FirstRunPagesTestCase withHistorySyncPromo() {
            return setShouldShowHistorySyncPromo(true);
        }

        FirstRunPagesTestCase withoutSignIn() {
            return setShouldSignIn(false);
        }

        static FirstRunPagesTestCase createWithShowAllPromos() {
            return new FirstRunPagesTestCase().withHistorySyncPromo().withSearchPromo();
        }
    }

    /**
     * Performs basic navigation operations on First Run pages, such as checking if a given promo is
     * current shown, moving to the next page, or going back to the previous page.
     */
    class FirstRunNavigationHelper {
        private final FirstRunActivity mFirstRunActivity;
        private final ScopedObserverData mScopedObserverData;

        protected FirstRunNavigationHelper(FirstRunActivity firstRunActivity) {
            mFirstRunActivity = firstRunActivity;
            mScopedObserverData = getObserverData(mFirstRunActivity);
        }

        protected FirstRunNavigationHelper ensurePagesCreationSucceeded() throws Exception {
            mScopedObserverData.createPostNativeAndPoliciesPageSequenceCallback.waitForCallback(
                    "Failed to finalize the flow and create subsequent pages", 0);
            Assert.assertEquals(
                    "Search engine name should not have been set yet",
                    0,
                    mScopedObserverData.updateCachedEngineCallback.getCallCount());

            return this;
        }

        protected FirstRunNavigationHelper ensureWelcomePageIsCurrentPage() {
            return waitForCurrentFragmentToMatch(
                    "FRE welcome should be the current page",
                    Matchers.instanceOf(SigninFirstRunFragment.class));
        }

        protected FirstRunNavigationHelper ensureDefaultSearchEnginePromoIsCurrentPage() {
            return waitForCurrentFragmentToMatch(
                    "Search engine promo should be the current page",
                    Matchers.instanceOf(DefaultSearchEngineFirstRunFragment.class));
        }

        protected FirstRunNavigationHelper ensureDefaultSearchEnginePromoNotCurrentPage() {
            return waitForCurrentFragmentToMatch(
                    "Search engine promo shouldn't be the current page",
                    Matchers.not(Matchers.instanceOf(DefaultSearchEngineFirstRunFragment.class)));
        }

        protected FirstRunNavigationHelper ensureHistorySyncIsCurrentPage() {
            return waitForCurrentFragmentToMatch(
                    "History sync should be the current page",
                    Matchers.instanceOf(HistorySyncFirstRunFragment.class));
        }

        protected FirstRunNavigationHelper ensureHistorySyncNotCurrentPage() {
            return waitForCurrentFragmentToMatch(
                    "History sync shouldn't be the current page",
                    Matchers.not(Matchers.instanceOf(HistorySyncFirstRunFragment.class)));
        }

        protected FirstRunNavigationHelper continueAndSignIn() throws Exception {
            ensureWelcomePageIsCurrentPage();

            int jumpCallCount = mScopedObserverData.jumpToPageCallback.getCallCount();
            int acceptCallCount = mScopedObserverData.acceptTermsOfServiceCallback.getCallCount();

            clickButton(mFirstRunActivity, R.id.signin_fre_continue_button, "Failed to sign in");
            mScopedObserverData.jumpToPageCallback.waitForCallback(
                    "Failed to try moving to the next screen", jumpCallCount);
            mScopedObserverData.acceptTermsOfServiceCallback.waitForCallback(
                    "Failed to sign in", acceptCallCount);
            mSigninTestRule.waitForSignin(TestAccounts.AADC_ADULT_ACCOUNT);

            return this;
        }

        protected FirstRunNavigationHelper selectDefaultSearchEngine() throws Exception {
            ensureDefaultSearchEnginePromoIsCurrentPage();

            int jumpCallCount = mScopedObserverData.jumpToPageCallback.getCallCount();
            DefaultSearchEngineDialogHelperUtils.clickOnFirstEngine(
                    mFirstRunActivity.findViewById(android.R.id.content));
            mScopedObserverData.jumpToPageCallback.waitForCallback(
                    "Failed trying to move past the search engine fragment", jumpCallCount);

            return this;
        }

        protected FirstRunNavigationHelper dismissHistorySync() throws Exception {
            ensureHistorySyncIsCurrentPage();

            int jumpCallCount = mScopedObserverData.jumpToPageCallback.getCallCount();
            clickButton(
                    mFirstRunActivity, R.id.button_secondary, "Failed to skip history sync opt-in");
            mScopedObserverData.jumpToPageCallback.waitForCallback(
                    "Failed trying to move past the history sync fragment", jumpCallCount);

            return this;
        }

        protected FirstRunNavigationHelper dismissSigninPromo() throws Exception {
            ensureWelcomePageIsCurrentPage();
            int dismissButtonId =
                    BuildInfo.getInstance().isAutomotive
                            ? R.id.signin_fre_continue_button
                            : R.id.signin_fre_dismiss_button;
            clickButton(mFirstRunActivity, dismissButtonId, "Failed to skip signing-in");

            int jumpCallCount = mScopedObserverData.jumpToPageCallback.getCallCount();
            mScopedObserverData.jumpToPageCallback.waitForCallback(
                    "Failed trying to move past the sign in fragment", jumpCallCount);

            return this;
        }

        protected FirstRunNavigationHelper goBackToPreviousPage() throws Exception {
            int jumpCallCount = mScopedObserverData.jumpToPageCallback.getCallCount();
            ThreadUtils.runOnUiThreadBlocking(
                    mFirstRunActivity.getOnBackPressedDispatcher()::onBackPressed);
            mScopedObserverData.jumpToPageCallback.waitForCallback(
                    "Failed go back to previous page", jumpCallCount);

            return this;
        }

        protected FirstRunNavigationHelper waitForCurrentFragmentToMatch(
                String failureReason, Matcher<Object> matcher) {
            CriteriaHelper.pollUiThread(
                    () -> matcher.matches(mFirstRunActivity.getCurrentFragmentForTesting()),
                    failureReason);
            return this;
        }
    }

    /**
     * Overrides the default {@link FirstRunFlowSequencer}'s delegate to make decisions on
     * showing/skipping promo pages based on the current {@link FirstRunPagesTestCase}.
     */
    private static class TestFirstRunFlowSequencerDelegate
            extends FirstRunFlowSequencer.FirstRunFlowSequencerDelegate {
        private final FirstRunPagesTestCase mTestCase;

        public TestFirstRunFlowSequencerDelegate(
                FirstRunPagesTestCase testCase, OneshotSupplier<ProfileProvider> profileProvider) {
            super(profileProvider);
            mTestCase = testCase;
        }

        @Override
        public boolean shouldShowHistorySyncOptIn(boolean isChild) {
            return mTestCase.shouldShowHistorySyncPromo();
        }

        @Override
        public boolean shouldShowSearchEnginePage() {
            return mTestCase.showSearchPromo();
        }
    }
}
