// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices.ui.trustedwebactivity;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import org.chromium.base.Log;
import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.browserservices.BrowserServicesStore;
import org.chromium.chrome.browser.browserservices.ui.view.DisclosureNotification;
import org.chromium.components.browser_ui.notifications.BaseNotificationManagerProxy;
import org.chromium.components.browser_ui.notifications.BaseNotificationManagerProxyFactory;
import org.chromium.components.browser_ui.notifications.PendingIntentProvider;

/**
 * Deals with the result of user interaction with the {@link DisclosureNotification}.
 *
 * When a user clicks on such a notification, we record their acceptance of the disclosure and
 * dismiss the notification.
 *
 * Lifecycle: This BroadcastReceiver is started when an Intent arrives, performs a small amount of
 * work (dismissing a notification and saving a SharedPreference) and is destroyed.
 *
 * Thread safety: {@link #onReceive} is called on the main thread by the Android framework.
 */
@NullMarked
public class DisclosureAcceptanceBroadcastReceiver extends BroadcastReceiver {
    private static final String TAG = "TWADisclosureRec";

    private static final String TAG_EXTRA = "TWADisclosureResp.tag_extra";
    private static final String ID_EXTRA = "TWADisclosureResp.id_extra";
    private static final String PACKAGE_EXTRA = "TWADisclosureResp.package_extra";

    private final BaseNotificationManagerProxy mNotificationManager;

    /** Constructor used by the Android framework. */
    public DisclosureAcceptanceBroadcastReceiver() {
        this(BaseNotificationManagerProxyFactory.create());
    }

    /** Constructor that allows dependency injection for use in tests. */
    public DisclosureAcceptanceBroadcastReceiver(BaseNotificationManagerProxy notificationManager) {
        mNotificationManager = notificationManager;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null
                || !intent.hasExtra(TAG_EXTRA)
                || !intent.hasExtra(ID_EXTRA)
                || !intent.hasExtra(PACKAGE_EXTRA)) {
            Log.w(TAG, "Started with null or incomplete Intent.");
            return;
        }

        String tag = intent.getStringExtra(TAG_EXTRA);
        int id = intent.getIntExtra(ID_EXTRA, -1);
        String packageName = intent.getStringExtra(PACKAGE_EXTRA);
        assumeNonNull(tag);
        assumeNonNull(packageName);

        mNotificationManager.cancel(tag, id);
        BrowserServicesStore.setUserAcceptedTwaDisclosureForPackage(packageName);
    }

    public static PendingIntentProvider createPendingIntent(
            Context context, String tag, int id, String packageName) {
        Intent intent = new Intent();
        intent.setClass(context, DisclosureAcceptanceBroadcastReceiver.class);
        intent.putExtra(TAG_EXTRA, tag);
        intent.putExtra(ID_EXTRA, id);
        intent.putExtra(PACKAGE_EXTRA, packageName);

        int requestCode = 0;
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        return PendingIntentProvider.getBroadcast(context, requestCode, intent, flags);
    }
}
