// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.dragdrop;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.SystemClock;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.IntentUtils;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.TimeUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.document.ChromeLauncherActivity;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabGroupMetadata;
import org.chromium.chrome.browser.tabwindow.TabWindowManager;
import org.chromium.ui.dragdrop.DragDropMetricUtils;
import org.chromium.ui.dragdrop.DragDropMetricUtils.DragDropType;
import org.chromium.ui.dragdrop.DragDropMetricUtils.UrlIntentSource;
import org.chromium.ui.util.XrUtils;

/** A helper activity for routing Chrome tab, tab group and link drag & drop launcher intents. */
// TODO (crbug/331865433): Consider removing use of this trampoline activity.
public class DragAndDropLauncherActivity extends Activity {
    static final String ACTION_DRAG_DROP_VIEW = "org.chromium.chrome.browser.dragdrop.action.VIEW";
    static final String LAUNCHED_FROM_LINK_USER_ACTION = "MobileNewInstanceLaunchedFromDraggedLink";
    static final String LAUNCHED_FROM_TAB_USER_ACTION = "MobileNewInstanceLaunchedFromDraggedTab";
    static final String LAUNCHED_FROM_TAB_GROUP_USER_ACTION =
            "MobileNewInstanceLaunchedFromDraggedTabGroup";

    // Hiding the overview takes some time and we need to delay starting new ChromeTabbedActivity to
    // align it with the View animation.
    private static final long XR_EXIT_OVERVIEW_DELAY_MS = 250L;
    private static final long DROP_TIMEOUT_MS = 5 * TimeUtils.MILLISECONDS_PER_MINUTE;
    private static Long sIntentCreationTimestampMs;
    private static Long sDropTimeoutForTesting;

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final Intent intent = getIntent();
        if (!isIntentValid(intent)) {
            finish();
            return;
        }

        // Launch the intent in a new or existing ChromeTabbedActivity.
        intent.setClass(this, ChromeTabbedActivity.class);
        IntentUtils.addTrustedIntentExtras(intent);

        // Do not propagate FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS that is set for this trampoline
        // DragAndDropLauncherActivity.
        intent.removeFlags(Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);

        recordLaunchMetrics(intent);

        // Launch the intent in an existing Chrome window, referenced by the EXTRA_WINDOW_ID intent
        // extra, if required.
        if (intent.hasExtra(IntentHandler.EXTRA_WINDOW_ID)) {
            int windowId =
                    IntentUtils.safeGetIntExtra(
                            intent,
                            IntentHandler.EXTRA_WINDOW_ID,
                            TabWindowManager.INVALID_WINDOW_ID);
            MultiWindowUtils.launchIntentInInstance(intent, windowId);
        } else {
            if (maybeExitOverview(intent)) {
                startActivityDelayed(intent, XR_EXIT_OVERVIEW_DELAY_MS);
            } else {
                startActivity(intent);
            }
        }

        finish();
    }

    private void startActivityDelayed(Intent intent, long delay) {
        new Handler().postDelayed(() -> startActivity(intent), delay);
    }

    private boolean maybeExitOverview(Intent intent) {
        int sourceWindowId =
                IntentUtils.safeGetIntExtra(
                        intent,
                        IntentHandler.EXTRA_DRAGDROP_TAB_WINDOW_ID,
                        TabWindowManager.INVALID_WINDOW_ID);
        if (sourceWindowId != TabWindowManager.INVALID_WINDOW_ID && XrUtils.isXrDevice()) {
            Intent exitOverviewIntent = new Intent(Intent.ACTION_MAIN);
            exitOverviewIntent.setClass(this, ChromeLauncherActivity.class);
            exitOverviewIntent.putExtra(IntentHandler.EXTRA_EXIT_XR_OVERVIEW_MODE, true);
            IntentUtils.addTrustedIntentExtras(exitOverviewIntent);
            MultiWindowUtils.launchIntentInInstance(exitOverviewIntent, sourceWindowId);
            return true;
        }

        return false;
    }

    /**
     * Creates an intent from a tab or tab group dragged out of Chrome to move it to a new Chrome
     * window.
     *
     * @param chromeDropDataAndroid The drop data containing either a single tab or tab group
     *     metadata.
     * @param context The context used to retrieve the package name.
     * @param sourceWindowId The window ID of the Chrome window where the tab drag starts.
     * @param destWindowId The window ID of the Chrome window in which the tab or group will be
     *     moved, |TabWindowManager.INVALID_WINDOW_ID| if the tab should be moved to a new window.
     * @return An {@link Intent} configured to launch the provided tab or tab group; or null if the
     *     data type is unsupported.
     */
    public static @Nullable Intent buildTabOrGroupIntent(
            ChromeDropDataAndroid chromeDropDataAndroid,
            Context context,
            int sourceWindowId,
            int destWindowId) {
        if (!MultiWindowUtils.isMultiInstanceApi31Enabled()) return null;
        Intent intent = setupIntent(context, destWindowId);
        if (chromeDropDataAndroid instanceof ChromeTabDropDataAndroid tabDropData) {
            intent = getTabIntent(intent, tabDropData.tab);
        } else if (chromeDropDataAndroid instanceof ChromeTabGroupDropDataAndroid groupDropData) {
            intent = getTabGroupIntent(intent, groupDropData.tabGroupMetadata);
        }
        intent.putExtra(IntentHandler.EXTRA_DRAGDROP_TAB_WINDOW_ID, sourceWindowId);
        DragAndDropLauncherActivity.setIntentCreationTimestampMs(SystemClock.elapsedRealtime());
        return intent;
    }

    /**
     * Creates an intent from a link dragged out of Chrome to open a new Chrome window.
     *
     * @param context The context used to retrieve the package name.
     * @param urlString The link URL string.
     * @param windowId The window ID of the Chrome window in which the link will be opened,
     *     |TabWindowManager.INVALID_WINDOW_ID| if there is no preference.
     * @param intentSrc An enum indicating whether the intent is created by link or tab.
     * @return The intent that will be used to create a new Chrome instance from a dragged link.
     */
    public static Intent getLinkLauncherIntent(
            Context context, String urlString, int windowId, @UrlIntentSource int intentSrc) {
        Intent intent = setupIntent(context, windowId);
        intent.setData(Uri.parse(urlString));
        intent.putExtra(IntentHandler.EXTRA_URL_DRAG_SOURCE, intentSrc);
        DragAndDropLauncherActivity.setIntentCreationTimestampMs(SystemClock.elapsedRealtime());
        return intent;
    }

    /**
     * Creates an intent from a tab dragged out of Chrome to move it to a new Chrome window.
     *
     * @param intent The intent to be configured for moving a tab to a new window.
     * @param tab The dragged tab.
     * @return The intent that will be used to move a dragged tab to a new Chrome instance.
     */
    @VisibleForTesting
    static Intent getTabIntent(Intent intent, Tab tab) {
        intent.putExtra(IntentHandler.EXTRA_URL_DRAG_SOURCE, UrlIntentSource.TAB_IN_STRIP);
        intent.putExtra(IntentHandler.EXTRA_DRAGGED_TAB_ID, tab.getId());
        intent.setData(Uri.parse(tab.getUrl().getSpec()));
        return intent;
    }

    /**
     * Creates an intent from a tab group dragged out of Chrome to move it to a new Chrome window.
     *
     * @param intent The intent to be configured for moving a tab group to a new window.
     * @param tabGroupMetadata The tabGroupMetadata of the dragged tab group.
     * @return The intent that will be used to move a dragged tab group to a new Chrome instance.
     */
    @VisibleForTesting
    static Intent getTabGroupIntent(Intent intent, TabGroupMetadata tabGroupMetadata) {
        intent.putExtra(IntentHandler.EXTRA_URL_DRAG_SOURCE, UrlIntentSource.TAB_GROUP_IN_STRIP);
        IntentHandler.setTabGroupMetadata(intent, tabGroupMetadata);
        return intent;
    }

    private static Intent setupIntent(Context context, int destWindowId) {
        Intent intent =
                MultiWindowUtils.createNewWindowIntent(
                        context.getApplicationContext(),
                        destWindowId,
                        /* preferNew= */ true,
                        /* openAdjacently= */ false,
                        /* addTrustedIntentExtras= */ false);
        intent.setClass(context, DragAndDropLauncherActivity.class);
        intent.setAction(DragAndDropLauncherActivity.ACTION_DRAG_DROP_VIEW);
        intent.addCategory(Intent.CATEGORY_BROWSABLE);
        return intent;
    }

    /**
     * Validates the intent before processing it.
     *
     * @param intent The incoming intent.
     * @return {@code true} if the intent is valid for processing, {@code false} otherwise.
     */
    @VisibleForTesting
    static boolean isIntentValid(Intent intent) {
        // Exit early if the original intent action isn't for viewing a dragged link/tab.
        assert ACTION_DRAG_DROP_VIEW.equals(intent.getAction()) : "The intent action is invalid.";

        // Exit early if the duration between the original intent creation and drop to launch the
        // activity exceeds the timeout.
        return getIntentCreationTimestampMs() != null
                && (SystemClock.elapsedRealtime() - getIntentCreationTimestampMs()
                        <= getDropTimeoutMs());
    }

    /**
     * Sets the ClipData intent creation timestamp when a Chrome link/tab drag starts.
     *
     * @param timestamp The intent creation timestamp in milliseconds.
     */
    static void setIntentCreationTimestampMs(Long timestamp) {
        sIntentCreationTimestampMs = timestamp;
    }

    /**
     * @return The dragged link/tab intent creation timestamp in milliseconds.
     */
    static Long getIntentCreationTimestampMs() {
        return sIntentCreationTimestampMs;
    }

    @VisibleForTesting
    static Long getDropTimeoutMs() {
        return sDropTimeoutForTesting == null ? DROP_TIMEOUT_MS : sDropTimeoutForTesting;
    }

    static void setDropTimeoutMsForTesting(Long timeout) {
        sDropTimeoutForTesting = timeout;
        ResettersForTesting.register(() -> sDropTimeoutForTesting = null);
    }

    private static void recordLaunchMetrics(Intent intent) {
        @UrlIntentSource
        int intentSource =
                IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_URL_DRAG_SOURCE, UrlIntentSource.UNKNOWN);
        if (intentSource == UrlIntentSource.LINK) {
            RecordUserAction.record(LAUNCHED_FROM_LINK_USER_ACTION);
            RecordHistogram.recordEnumeratedHistogram(
                    DragDropMetricUtils.HISTOGRAM_DRAG_DROP_TAB_TYPE,
                    DragDropType.LINK_TO_NEW_INSTANCE,
                    DragDropType.NUM_ENTRIES);
        } else if (intentSource == UrlIntentSource.TAB_IN_STRIP) {
            RecordUserAction.record(LAUNCHED_FROM_TAB_USER_ACTION);
        } else if (intentSource == UrlIntentSource.TAB_GROUP_IN_STRIP) {
            RecordUserAction.record(LAUNCHED_FROM_TAB_GROUP_USER_ACTION);
        }
    }
}
