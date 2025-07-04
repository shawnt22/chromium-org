// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import androidx.test.annotation.UiThreadTest;
import androidx.test.filters.SmallTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.profiles.OtrProfileId;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.components.browser_ui.notifications.NotificationProxyUtils;
import org.chromium.components.download.DownloadDangerType;
import org.chromium.components.offline_items_collection.ContentId;
import org.chromium.components.offline_items_collection.LegacyHelpers;
import org.chromium.components.offline_items_collection.OfflineItem.Progress;
import org.chromium.components.offline_items_collection.OfflineItemProgressUnit;
import org.chromium.components.offline_items_collection.PendingState;
import org.chromium.url.GURL;

import java.util.UUID;

/** Tests of {@link DownloadNotificationService}. */
@RunWith(ChromeJUnit4ClassRunner.class)
@Batch(Batch.UNIT_TESTS)
public class DownloadNotificationServiceTest {
    private static final ContentId ID1 =
            LegacyHelpers.buildLegacyContentId(false, UUID.randomUUID().toString());

    private MockDownloadNotificationService mDownloadNotificationService;
    private DownloadForegroundServiceManagerTest.MockDownloadForegroundServiceManager
            mDownloadForegroundServiceManager;
    private final OtrProfileId mPrimaryOtrProfileId = OtrProfileId.getPrimaryOtrProfileId();

    @Before
    public void setUp() {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mDownloadNotificationService = new MockDownloadNotificationService();
                    mDownloadForegroundServiceManager =
                            new DownloadForegroundServiceManagerTest
                                    .MockDownloadForegroundServiceManager();
                    mDownloadNotificationService.setDownloadForegroundServiceManager(
                            mDownloadForegroundServiceManager);
                });
        NotificationProxyUtils.setNotificationEnabledForTest(true);
    }

    @After
    public void tearDown() {
        ChromeSharedPreferences.getInstance()
                .removeKey(ChromePreferenceKeys.DOWNLOAD_PENDING_DOWNLOAD_NOTIFICATIONS);
        NotificationProxyUtils.setNotificationEnabledForTest(null);
    }

    @Test
    @SmallTest
    @UiThreadTest
    @Feature({"Download"})
    public void testBasicDownloadFlow() {
        // Download is in-progress.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "test",
                new Progress(1, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        mDownloadForegroundServiceManager.onServiceConnected();

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        int notificationId1 = mDownloadNotificationService.getLastNotificationId();
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is paused.
        mDownloadNotificationService.notifyDownloadPaused(
                ID1,
                "test",
                /* isResumable= */ true,
                /* isAutoResumable= */ false,
                mPrimaryOtrProfileId,
                false,
                null,
                null,
                false,
                false,
                false,
                PendingState.NOT_PENDING);

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is again in-progress.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "test",
                new Progress(20, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        mDownloadForegroundServiceManager.onServiceConnected();

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is successful.
        mDownloadNotificationService.notifyDownloadSuccessful(
                ID1,
                "",
                "test",
                1L,
                mPrimaryOtrProfileId,
                true,
                true,
                null,
                GURL.emptyGURL(),
                false,
                GURL.emptyGURL(),
                0);
        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));
    }

    @Test
    @SmallTest
    @UiThreadTest
    @Feature({"Download"})
    public void testDownloadPendingAndCancelled() {
        // Download is in-progress.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "test",
                new Progress(1, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        mDownloadForegroundServiceManager.onServiceConnected();

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        int notificationId1 = mDownloadNotificationService.getLastNotificationId();
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is interrupted and now is pending.
        mDownloadNotificationService.notifyDownloadPaused(
                ID1,
                "test",
                /* isResumable= */ true,
                /* isAutoResumable= */ true,
                mPrimaryOtrProfileId,
                false,
                null,
                null,
                false,
                false,
                false,
                PendingState.PENDING_NETWORK);
        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is cancelled.
        mDownloadNotificationService.notifyDownloadCanceled(ID1, false);

        assertEquals(0, mDownloadNotificationService.getNotificationIds().size());
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));
    }

    @Test
    @SmallTest
    @UiThreadTest
    @Feature({"Download"})
    public void testDownloadInterruptedAndFailed() {
        // Download is in-progress.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "test",
                new Progress(1, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        mDownloadForegroundServiceManager.onServiceConnected();

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        int notificationId1 = mDownloadNotificationService.getLastNotificationId();
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is interrupted but because it is not resumable, fails.
        mDownloadNotificationService.notifyDownloadPaused(
                ID1,
                "test",
                /* isResumable= */ false,
                /* isAutoResumable= */ true,
                mPrimaryOtrProfileId,
                false,
                null,
                null,
                false,
                false,
                false,
                PendingState.PENDING_NETWORK);
        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));
    }

    @Test
    @SmallTest
    @UiThreadTest
    @Feature({"Download"})
    public void testDownloadDangerousAndValidated() {
        // Download is in-progress.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "dangerous.apk",
                new Progress(1, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        mDownloadForegroundServiceManager.onServiceConnected();

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        int notificationId1 = mDownloadNotificationService.getLastNotificationId();
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download receives an update that it is dangerous.
        mDownloadNotificationService.notifyDownloadDangerous(
                ID1,
                "dangerous.apk",
                null,
                false,
                mPrimaryOtrProfileId,
                true,
                false,
                DownloadDangerType.DANGEROUS_CONTENT);

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        // The dangerous notification is not in the queue because it is not active.
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is validated and gets a progress update.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "dangerous.apk",
                new Progress(1, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));
    }

    @Test
    @SmallTest
    @UiThreadTest
    @Feature({"Download"})
    public void testDownloadDangerousAndRemoved() {
        // Download is in-progress.
        mDownloadNotificationService.notifyDownloadProgress(
                ID1,
                "dangerous.apk",
                new Progress(1, 100L, OfflineItemProgressUnit.PERCENTAGE),
                100L,
                1L,
                1L,
                mPrimaryOtrProfileId,
                true,
                false,
                null,
                null,
                false);
        mDownloadForegroundServiceManager.onServiceConnected();

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        int notificationId1 = mDownloadNotificationService.getLastNotificationId();
        assertTrue(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download receives an update that it is dangerous.
        mDownloadNotificationService.notifyDownloadDangerous(
                ID1,
                "dangerous.apk",
                null,
                false,
                mPrimaryOtrProfileId,
                true,
                false,
                DownloadDangerType.DANGEROUS_CONTENT);

        assertEquals(1, mDownloadNotificationService.getNotificationIds().size());
        // The dangerous notification is not in the queue because it is not active.
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));

        // Download is cancelled.
        mDownloadNotificationService.notifyDownloadCanceled(ID1, false);

        assertEquals(0, mDownloadNotificationService.getNotificationIds().size());
        assertFalse(
                mDownloadForegroundServiceManager.mDownloadUpdateQueue.containsKey(
                        notificationId1));
    }
}
