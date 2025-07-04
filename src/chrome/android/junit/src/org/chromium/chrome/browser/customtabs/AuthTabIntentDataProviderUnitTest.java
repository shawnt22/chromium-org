// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs;

import static androidx.browser.customtabs.CustomTabsIntent.COLOR_SCHEME_LIGHT;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import static org.chromium.chrome.browser.customtabs.CustomTabsFeatureUsage.CUSTOM_TABS_FEATURE_USAGE_HISTOGRAM;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.BitmapDrawable;
import android.net.Uri;
import android.os.Bundle;

import androidx.browser.auth.AuthTabIntent;
import androidx.browser.customtabs.CustomTabsIntent;
import androidx.test.ext.junit.rules.ActivityScenarioRule;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.Features;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.base.test.util.HistogramWatcher;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider.CustomTabProfileType;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider.CustomTabsUiType;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider.TitleVisibility;
import org.chromium.chrome.browser.flags.ActivityType;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.ui.base.TestActivity;

@RunWith(BaseRobolectricTestRunner.class)
@Batch(Batch.UNIT_TESTS)
@Config(manifest = Config.NONE)
public class AuthTabIntentDataProviderUnitTest {
    @Rule
    public ActivityScenarioRule<TestActivity> mActivityScenario =
            new ActivityScenarioRule<>(TestActivity.class);

    private static final String PACKAGE = "com.example.package.app";
    private static final String URL = "https://www.google.com";
    private static final String SCHEME = "myscheme";
    private static final String HOST = "www.host.com";
    private static final String PATH = "/path/auth";

    private Activity mActivity;
    private Intent mIntent;
    private AuthTabIntentDataProvider mIntentDataProvider;

    @Before
    public void setUp() {
        mActivityScenario.getScenario().onActivity(activity -> mActivity = activity);
        mIntent = new Intent(Intent.ACTION_VIEW);
        mIntent.setData(Uri.parse(URL));
        mIntent.putExtra(AuthTabIntent.EXTRA_LAUNCH_AUTH_TAB, true);
        mIntent.putExtra(IntentHandler.EXTRA_CALLING_ACTIVITY_PACKAGE, PACKAGE);
        Bundle bundle = new Bundle();
        bundle.putBinder(CustomTabsIntent.EXTRA_SESSION, null);
        mIntent.putExtras(bundle);
    }

    @Test
    public void testOverriddenDefaults() {
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);
        assertEquals(
                "ActivityType should be AUTH_TAB.",
                ActivityType.AUTH_TAB,
                mIntentDataProvider.getActivityType());
        assertFalse(
                "UrlBar hiding should be disabled.",
                mIntentDataProvider.shouldEnableUrlBarHiding());
        assertEquals(
                "Page title should be visible.",
                TitleVisibility.VISIBLE,
                mIntentDataProvider.getTitleVisibilityState());
        assertEquals(
                "Ui type should be AUTH_TAB.",
                CustomTabsUiType.AUTH_TAB,
                mIntentDataProvider.getUiType());
        assertFalse("Star button shouldn't be shown.", mIntentDataProvider.shouldShowStarButton());
        assertFalse(
                "Download button shouldn't be shown.",
                mIntentDataProvider.shouldShowDownloadButton());
        assertTrue("Should be an Auth Tab.", mIntentDataProvider.isAuthTab());
        assertNotNull(
                "Should display default close button icon.",
                mIntentDataProvider.getCloseButtonDrawable());
    }

    @Test
    public void testIntentData() {
        var histogramWatcher =
                HistogramWatcher.newBuilder()
                        .expectIntRecord(
                                CUSTOM_TABS_FEATURE_USAGE_HISTOGRAM,
                                CustomTabsFeatureUsage.CustomTabsFeature.EXTRA_LAUNCH_AUTH_TAB)
                        .allowExtraRecordsForHistogramsAbove()
                        .build();
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);

        assertEquals("Intent doesn't match expectation.", mIntent, mIntentDataProvider.getIntent());
        assertEquals("Wrong package name", PACKAGE, mIntentDataProvider.getClientPackageName());
        assertEquals("Wrong URL", URL, mIntentDataProvider.getUrlToLoad());
        assertEquals(
                "CustomTabMode should be regular.",
                CustomTabProfileType.REGULAR,
                mIntentDataProvider.getCustomTabMode());
        histogramWatcher.assertExpected();
    }

    @Test
    public void testIntentData_customScheme() {
        var histogramWatcher =
                HistogramWatcher.newBuilder()
                        .expectIntRecord(
                                CUSTOM_TABS_FEATURE_USAGE_HISTOGRAM,
                                CustomTabsFeatureUsage.CustomTabsFeature.EXTRA_REDIRECT_SCHEME)
                        .allowExtraRecordsForHistogramsAbove()
                        .build();
        mIntent.putExtra(AuthTabIntent.EXTRA_REDIRECT_SCHEME, SCHEME);
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);

        assertEquals("Wrong redirect scheme.", SCHEME, mIntentDataProvider.getAuthRedirectScheme());
        histogramWatcher.assertExpected();
    }

    @Test
    public void testIntentData_https() {
        var histogramWatcher =
                HistogramWatcher.newBuilder()
                        .expectIntRecords(
                                CUSTOM_TABS_FEATURE_USAGE_HISTOGRAM,
                                CustomTabsFeatureUsage.CustomTabsFeature.EXTRA_HTTPS_REDIRECT_HOST,
                                CustomTabsFeatureUsage.CustomTabsFeature.EXTRA_HTTPS_REDIRECT_PATH)
                        .allowExtraRecordsForHistogramsAbove()
                        .build();
        mIntent.putExtra(AuthTabIntent.EXTRA_HTTPS_REDIRECT_HOST, HOST);
        mIntent.putExtra(AuthTabIntent.EXTRA_HTTPS_REDIRECT_PATH, PATH);
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);

        assertEquals("Wrong https redirect host.", HOST, mIntentDataProvider.getAuthRedirectHost());
        assertEquals("Wrong https redirect path.", PATH, mIntentDataProvider.getAuthRedirectPath());
        histogramWatcher.assertExpected();
    }

    @Test
    @Features.EnableFeatures(ChromeFeatureList.CCT_EPHEMERAL_MODE)
    public void testIntentData_ephemeralBrowsing() {
        var histogramWatcher =
                HistogramWatcher.newBuilder()
                        .expectIntRecord(
                                CUSTOM_TABS_FEATURE_USAGE_HISTOGRAM,
                                CustomTabsFeatureUsage.CustomTabsFeature
                                        .EXTRA_ENABLE_EPHEMERAL_BROWSING)
                        .allowExtraRecordsForHistogramsAbove()
                        .build();
        mIntent.putExtra(CustomTabsIntent.EXTRA_ENABLE_EPHEMERAL_BROWSING, true);
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);

        assertEquals(
                "CustomTabMode should be ephemeral.",
                CustomTabProfileType.EPHEMERAL,
                mIntentDataProvider.getCustomTabMode());
        histogramWatcher.assertExpected();
    }

    @Test
    public void testIntentData_closeButtonIcon() {
        var histogramWatcher =
                HistogramWatcher.newBuilder()
                        .expectIntRecord(
                                CUSTOM_TABS_FEATURE_USAGE_HISTOGRAM,
                                CustomTabsFeatureUsage.CustomTabsFeature.EXTRA_CLOSE_BUTTON_ICON)
                        .allowExtraRecordsForHistogramsAbove()
                        .build();
        Bitmap icon =
                BitmapFactory.decodeResource(
                        mActivity.getResources(), R.drawable.ic_arrow_back_white_24dp);
        mIntent.putExtra(CustomTabsIntent.EXTRA_CLOSE_BUTTON_ICON, icon);
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);

        assertEquals(
                "Close button icon should be the one provided.",
                icon,
                ((BitmapDrawable) mIntentDataProvider.getCloseButtonDrawable()).getBitmap());
        histogramWatcher.assertExpected();
    }

    @Test
    @EnableFeatures(ChromeFeatureList.CCT_ADAPTIVE_BUTTON)
    public void testIsOptionalButtonSupported_normalAuthTab() {
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);
        assertFalse(
                "AuthTab should not support optional button",
                mIntentDataProvider.isOptionalButtonSupported());
    }

    @Test
    @EnableFeatures(ChromeFeatureList.CCT_ADAPTIVE_BUTTON)
    public void testIsOptionalButtonSupported_ephemeralAuthTab() {
        mIntent.putExtra(CustomTabsIntent.EXTRA_ENABLE_EPHEMERAL_BROWSING, true);
        mIntentDataProvider = new AuthTabIntentDataProvider(mIntent, mActivity, COLOR_SCHEME_LIGHT);
        assertTrue(
                "IntentDataProvider should be for ephemeral AuthTab",
                AuthTabIntentDataProvider.isEphemeralTab(mIntent));
        assertFalse(
                "Ephemeral AuthTab should not support optional button",
                mIntentDataProvider.isOptionalButtonSupported());
    }
}
