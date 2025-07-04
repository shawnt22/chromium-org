// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import android.media.MediaScannerConnection;
import android.net.Uri;
import android.text.TextUtils;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.task.AsyncTask;
import org.chromium.build.annotations.NullMarked;

/** Includes helper methods to interact with Android media store. */
@NullMarked
public class MediaStoreHelper {
    private static final String TAG = "MediaStoreHelper";

    private MediaStoreHelper() {}

    /**
     * Adds an image file on external SD card to media store to show in the Android gallery app. The
     * images on primary storage will automatically scanned by media store. On external SD card,
     * usually .nomedia file will block the media store scan request. The media store will decode,
     * compress the image and maintain a copy on disk. Does nothing if the file is not an image or
     * the file is not on external SD card.
     *
     * @param filePath The file path of the image file.
     * @param mimeType The mime type of the image file.
     */
    public static void addImageToGalleryOnSDCard(String filePath, String mimeType) {
        // TODO(xingliu): Support Android Q when we have available device with SD card slot.
        if (TextUtils.isEmpty(filePath) || mimeType == null || !mimeType.startsWith("image/")) {
            return;
        }

        DownloadDirectoryProvider.getInstance()
                .getAllDirectoriesOptions(
                        (dirs) -> {
                            for (DirectoryOption dir : dirs) {
                                // Scan the media file if it is on SD card.
                                if (dir.type
                                                == DirectoryOption.DownloadLocationDirectoryType
                                                        .ADDITIONAL
                                        && filePath.contains(dir.location)) {
                                    addImageOnBlockingThread(filePath, mimeType);
                                    return;
                                }
                            }
                        });
    }

    /**
     * Adds the image to media store on a blocking thread.
     *
     * @param filePath The file path of the image file.
     * @param mimeType The MIME type of the image file.
     */
    private static void addImageOnBlockingThread(String filePath, String mimeType) {
        new AsyncTask<Void>() {
            @Override
            protected Void doInBackground() {
                MediaScannerConnection.scanFile(
                        ContextUtils.getApplicationContext(),
                        new String[] {filePath},
                        new String[] {mimeType},
                        new MediaScannerConnection.OnScanCompletedListener() {
                            @Override
                            public void onScanCompleted(final String path, final Uri uri) {
                                if (uri == null) {
                                    Log.v(TAG, "Media scan failed");
                                }
                            }
                        });
                return null;
            }

            @Override
            protected void onPostExecute(Void aVoid) {}
        }.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
    }
}
