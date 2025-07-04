// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices.ui.view;

import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.DISCLOSURE_EVENTS_CALLBACK;
import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.DISCLOSURE_FIRST_TIME;
import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.DISCLOSURE_SCOPE;
import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.DISCLOSURE_STATE;
import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.DISCLOSURE_STATE_NOT_SHOWN;
import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.DISCLOSURE_STATE_SHOWN;
import static org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel.PACKAGE_NAME;
import static org.chromium.chrome.browser.notifications.NotificationConstants.NOTIFICATION_ID_TWA_DISCLOSURE_INITIAL;
import static org.chromium.chrome.browser.notifications.NotificationConstants.NOTIFICATION_ID_TWA_DISCLOSURE_SUBSEQUENT;

import android.content.res.Resources;

import org.chromium.base.ContextUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.browserservices.ui.TrustedWebActivityModel;
import org.chromium.chrome.browser.browserservices.ui.trustedwebactivity.DisclosureAcceptanceBroadcastReceiver;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.StartStopWithNativeObserver;
import org.chromium.chrome.browser.notifications.NotificationUmaTracker;
import org.chromium.chrome.browser.notifications.NotificationWrapperBuilderFactory;
import org.chromium.chrome.browser.notifications.channels.ChromeChannelDefinitions;
import org.chromium.components.browser_ui.notifications.BaseNotificationManagerProxy;
import org.chromium.components.browser_ui.notifications.BaseNotificationManagerProxyFactory;
import org.chromium.components.browser_ui.notifications.NotificationMetadata;
import org.chromium.components.browser_ui.notifications.NotificationWrapper;
import org.chromium.components.browser_ui.notifications.PendingIntentProvider;
import org.chromium.components.url_formatter.UrlFormatter;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyObservable;

/**
 * Displays a notification when the user is on the verified domain. The first such notification (per
 * TWA) is urgent priority, subsequent ones are low priority.
 */
@NullMarked
public class DisclosureNotification
        implements PropertyObservable.PropertyObserver<PropertyKey>, StartStopWithNativeObserver {
    private final Resources mResources;
    private final TrustedWebActivityModel mModel;
    private final BaseNotificationManagerProxy mNotificationManagerProxy =
            BaseNotificationManagerProxyFactory.create();

    private @Nullable String mCurrentScope;

    public DisclosureNotification(
            Resources resources,
            TrustedWebActivityModel model,
            ActivityLifecycleDispatcher lifecycleDispatcher) {
        mResources = resources;
        mModel = model;

        mModel.addObserver(this);
        lifecycleDispatcher.register(this);
    }

    private void show() {
        mCurrentScope = mModel.get(DISCLOSURE_SCOPE);
        boolean firstTime = mModel.get(DISCLOSURE_FIRST_TIME);
        String packageName = mModel.get(PACKAGE_NAME);

        NotificationWrapper notification =
                createNotification(firstTime, mCurrentScope, packageName);

        mNotificationManagerProxy.notify(notification);
        NotificationUmaTracker.getInstance()
                .onNotificationShown(
                        firstTime
                                ? NotificationUmaTracker.SystemNotificationType
                                        .TWA_DISCLOSURE_INITIAL
                                : NotificationUmaTracker.SystemNotificationType
                                        .TWA_DISCLOSURE_SUBSEQUENT,
                        notification.getNotification());

        mModel.get(DISCLOSURE_EVENTS_CALLBACK).onDisclosureShown();
    }

    private void dismiss() {
        mNotificationManagerProxy.cancel(mCurrentScope, NOTIFICATION_ID_TWA_DISCLOSURE_INITIAL);
        mNotificationManagerProxy.cancel(mCurrentScope, NOTIFICATION_ID_TWA_DISCLOSURE_SUBSEQUENT);
        mCurrentScope = null;
    }

    private NotificationWrapper createNotification(
            boolean firstTime, String scope, String packageName) {
        int umaType;
        int notificationId;
        String channelId;
        if (firstTime) {
            umaType = NotificationUmaTracker.SystemNotificationType.TWA_DISCLOSURE_INITIAL;
            channelId = ChromeChannelDefinitions.ChannelId.WEBAPPS;
            notificationId = NOTIFICATION_ID_TWA_DISCLOSURE_INITIAL;
        } else {
            umaType = NotificationUmaTracker.SystemNotificationType.TWA_DISCLOSURE_SUBSEQUENT;
            channelId = ChromeChannelDefinitions.ChannelId.WEBAPPS_QUIET;
            notificationId = NOTIFICATION_ID_TWA_DISCLOSURE_SUBSEQUENT;
        }

        // We use the TWA's package name as the notification tag so that multiple different TWAs
        // don't interfere with each other.
        NotificationMetadata metadata = new NotificationMetadata(umaType, scope, notificationId);

        String title = mResources.getString(R.string.twa_running_in_chrome);
        String scopeForDisplay =
                UrlFormatter.formatUrlForDisplayOmitSchemeOmitTrivialSubdomains(scope);
        String text = mResources.getString(R.string.twa_running_in_chrome_v2, scopeForDisplay);

        PendingIntentProvider intent =
                DisclosureAcceptanceBroadcastReceiver.createPendingIntent(
                        ContextUtils.getApplicationContext(), scope, notificationId, packageName);

        // We don't have an icon to display.
        int icon = 0;

        return NotificationWrapperBuilderFactory.createNotificationWrapperBuilder(
                        channelId, metadata)
                .setSmallIcon(R.drawable.ic_chrome)
                .setContentTitle(title)
                .setContentText(text)
                .setContentIntent(intent)
                .addAction(
                        icon,
                        mResources.getString(R.string.got_it),
                        intent,
                        NotificationUmaTracker.ActionType.TWA_NOTIFICATION_ACCEPTANCE)
                .setShowWhen(false)
                .setAutoCancel(false)
                .setSound(null)
                .setBigTextStyle(text)
                .setOngoing(!firstTime)
                .buildNotificationWrapper();
    }

    @Override
    public void onPropertyChanged(
            PropertyObservable<PropertyKey> source, @Nullable PropertyKey propertyKey) {
        if (propertyKey != DISCLOSURE_STATE) return;

        switch (mModel.get(DISCLOSURE_STATE)) {
            case DISCLOSURE_STATE_SHOWN:
                show();
                break;
            case DISCLOSURE_STATE_NOT_SHOWN:
                dismiss();
                break;
        }
    }

    @Override
    public void onStartWithNative() {
        if (mModel.get(DISCLOSURE_STATE) == DISCLOSURE_STATE_SHOWN) show();
    }

    @Override
    public void onStopWithNative() {
        dismiss();
    }
}
