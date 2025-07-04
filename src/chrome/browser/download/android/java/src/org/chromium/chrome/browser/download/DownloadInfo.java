// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import android.graphics.Bitmap;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.OtrProfileId;
import org.chromium.components.download.DownloadDangerType;
import org.chromium.components.download.DownloadState;
import org.chromium.components.offline_items_collection.ContentId;
import org.chromium.components.offline_items_collection.FailState;
import org.chromium.components.offline_items_collection.LegacyHelpers;
import org.chromium.components.offline_items_collection.OfflineItem;
import org.chromium.components.offline_items_collection.OfflineItem.Progress;
import org.chromium.components.offline_items_collection.OfflineItemProgressUnit;
import org.chromium.components.offline_items_collection.OfflineItemState;
import org.chromium.components.offline_items_collection.OfflineItemVisuals;
import org.chromium.components.offline_items_collection.PendingState;
import org.chromium.url.GURL;

/** Class representing the state of a single download. */
@NullMarked
public final class DownloadInfo {
    private final GURL mUrl;
    private final @Nullable String mUserAgent;
    private final @Nullable String mMimeType;
    private final @Nullable String mCookie;
    private final @Nullable String mFileName;
    private final @Nullable String mDescription;
    private final @Nullable String mFilePath;
    private final GURL mReferrer;
    private final GURL mOriginalUrl;
    private final long mBytesReceived;
    private final long mBytesTotalSize;
    private final @Nullable String mDownloadGuid;
    private final boolean mHasUserGesture;
    private final @Nullable String mContentDisposition;
    private final boolean mIsGETRequest;
    private final @Nullable Progress mProgress;
    private final long mTimeRemainingInMillis;
    private final boolean mIsResumable;
    private final boolean mIsPaused;
    private final boolean mIsOffTheRecord;
    private final @Nullable OtrProfileId mOtrProfileId;
    private final boolean mIsOfflinePage;
    private final int mState;
    private final long mLastAccessTime;
    private final @DownloadDangerType int mDangerType;
    private final boolean mIsDangerous;

    // New variables to assist with the migration to OfflineItems.
    private final ContentId mContentId;
    private final boolean mIsOpenable;
    private final boolean mIsTransient;
    private final boolean mIsParallelDownload;
    private final @Nullable Bitmap mIcon;
    @PendingState private final int mPendingState;
    @FailState private final int mFailState;
    private final boolean mShouldPromoteOrigin;

    private DownloadInfo(Builder builder) {
        mUrl = builder.mUrl == null ? GURL.emptyGURL() : builder.mUrl;
        mUserAgent = builder.mUserAgent;
        mMimeType = builder.mMimeType;
        mCookie = builder.mCookie;
        mFileName = builder.mFileName;
        mDescription = builder.mDescription;
        mFilePath = builder.mFilePath;
        mReferrer = builder.mReferrer == null ? GURL.emptyGURL() : builder.mReferrer;
        mOriginalUrl = builder.mOriginalUrl == null ? GURL.emptyGURL() : builder.mOriginalUrl;
        mBytesReceived = builder.mBytesReceived;
        mBytesTotalSize = builder.mBytesTotalSize;
        mDownloadGuid = builder.mDownloadGuid;
        mHasUserGesture = builder.mHasUserGesture;
        mIsGETRequest = builder.mIsGETRequest;
        mContentDisposition = builder.mContentDisposition;
        mProgress = builder.mProgress;
        mTimeRemainingInMillis = builder.mTimeRemainingInMillis;
        mIsResumable = builder.mIsResumable;
        mIsPaused = builder.mIsPaused;
        mIsOffTheRecord = builder.mIsOffTheRecord;
        mOtrProfileId = builder.mOtrProfileId;
        mIsOfflinePage = builder.mIsOfflinePage;
        mState = builder.mState;
        mLastAccessTime = builder.mLastAccessTime;
        mDangerType = builder.mDangerType;
        mIsDangerous = builder.mIsDangerous;

        if (builder.mContentId != null) {
            mContentId = builder.mContentId;
        } else {
            mContentId = LegacyHelpers.buildLegacyContentId(mIsOfflinePage, mDownloadGuid);
        }
        mIsOpenable = builder.mIsOpenable;
        mIsTransient = builder.mIsTransient;
        mIsParallelDownload = builder.mIsParallelDownload;
        mIcon = builder.mIcon;
        mPendingState = builder.mPendingState;
        mFailState = builder.mFailState;
        mShouldPromoteOrigin = builder.mShouldPromoteOrigin;
    }

    public GURL getUrl() {
        return mUrl;
    }

    public @Nullable String getUserAgent() {
        return mUserAgent;
    }

    public @Nullable String getMimeType() {
        return mMimeType;
    }

    public @Nullable String getCookie() {
        return mCookie;
    }

    public @Nullable String getFileName() {
        return mFileName;
    }

    public @Nullable String getDescription() {
        return mDescription;
    }

    public @Nullable String getFilePath() {
        return mFilePath;
    }

    public GURL getReferrer() {
        return mReferrer;
    }

    public GURL getOriginalUrl() {
        return mOriginalUrl;
    }

    public long getBytesReceived() {
        return mBytesReceived;
    }

    public long getBytesTotalSize() {
        return mBytesTotalSize;
    }

    public boolean isGETRequest() {
        return mIsGETRequest;
    }

    public @Nullable String getDownloadGuid() {
        return mDownloadGuid;
    }

    public boolean hasUserGesture() {
        return mHasUserGesture;
    }

    public @Nullable String getContentDisposition() {
        return mContentDisposition;
    }

    public @Nullable Progress getProgress() {
        return mProgress;
    }

    /**
     * @return Remaining download time in milliseconds or -1 if it is unknown.
     */
    public long getTimeRemainingInMillis() {
        return mTimeRemainingInMillis;
    }

    public boolean isResumable() {
        return mIsResumable;
    }

    public boolean isPaused() {
        return mIsPaused;
    }

    public boolean isOffTheRecord() {
        return mIsOffTheRecord;
    }

    public @Nullable OtrProfileId getOtrProfileId() {
        return mOtrProfileId;
    }

    public boolean isOfflinePage() {
        return mIsOfflinePage;
    }

    public int state() {
        return mState;
    }

    public long getLastAccessTime() {
        return mLastAccessTime;
    }

    public @DownloadDangerType int getDangerType() {
        return mDangerType;
    }

    public boolean getIsDangerous() {
        return mIsDangerous;
    }

    public ContentId getContentId() {
        return mContentId;
    }

    public boolean getIsOpenable() {
        return mIsOpenable;
    }

    public boolean getIsTransient() {
        return mIsTransient;
    }

    public boolean getIsParallelDownload() {
        return mIsParallelDownload;
    }

    public @Nullable Bitmap getIcon() {
        return mIcon;
    }

    public @PendingState int getPendingState() {
        return mPendingState;
    }

    public @FailState int getFailState() {
        return mFailState;
    }

    public boolean getShouldPromoteOrigin() {
        return mShouldPromoteOrigin;
    }

    /**
     * Helper method to build a {@link DownloadInfo} from an {@link OfflineItem}.
     *
     * @param item The {@link OfflineItem} to mimic.
     * @param visuals The {@link OfflineItemVisuals} to mimic.
     * @return A {@link DownloadInfo} containing the relevant fields from {@code item}.
     */
    public static DownloadInfo fromOfflineItem(
            OfflineItem item, @Nullable OfflineItemVisuals visuals) {
        return builderFromOfflineItem(item, visuals).build();
    }

    /**
     * Helper method to build a {@link DownloadInfo.Builder} from an {@link OfflineItem}.
     *
     * @param item The {@link OfflineItem} to mimic.
     * @param visuals The {@link OfflineItemVisuals} to mimic.
     * @return A {@link DownloadInfo.Builder} containing the relevant fields from {@code item}.
     */
    public static DownloadInfo.Builder builderFromOfflineItem(
            OfflineItem item, @Nullable OfflineItemVisuals visuals) {
        int state;
        switch (item.state) {
            case OfflineItemState.COMPLETE:
                state = DownloadState.COMPLETE;
                break;
            case OfflineItemState.CANCELLED:
                state = DownloadState.CANCELLED;
                break;
            case OfflineItemState.INTERRUPTED:
                state = DownloadState.INTERRUPTED;
                break;
            case OfflineItemState.FAILED:
                state = DownloadState.INTERRUPTED; // TODO(dtrainor): Validate what this state is.
                break;
            case OfflineItemState.PENDING: // TODO(dtrainor): Validate what this state is.
            case OfflineItemState.IN_PROGRESS:
            case OfflineItemState.PAUSED: // TODO(dtrainor): Validate what this state is.
            default:
                state = DownloadState.IN_PROGRESS;
                break;
        }

        return new DownloadInfo.Builder()
                .setContentId(item.id)
                .setDownloadGuid(item.id == null ? null : item.id.id)
                .setFileName(item.title)
                .setFilePath(item.filePath)
                .setDescription(item.description)
                .setIsTransient(item.isTransient)
                .setLastAccessTime(item.lastAccessedTimeMs)
                .setIsOpenable(item.isOpenable)
                .setMimeType(item.mimeType)
                .setUrl(item.url)
                .setOriginalUrl(item.originalUrl)
                .setOtrProfileId(
                        OtrProfileId.deserialize(
                                item.otrProfileId == null ? "" : item.otrProfileId))
                .setState(state)
                .setIsPaused(item.state == OfflineItemState.PAUSED)
                .setIsResumable(item.isResumable)
                .setBytesReceived(item.receivedBytes)
                .setBytesTotalSize(item.totalSizeBytes)
                .setProgress(item.progress)
                .setTimeRemainingInMillis(item.timeRemainingMs)
                .setDangerType(item.dangerType)
                .setIsDangerous(item.isDangerous)
                .setIsParallelDownload(item.isAccelerated)
                .setIcon(visuals == null ? null : visuals.icon)
                .setPendingState(item.pendingState)
                .setFailState(item.failState)
                .setShouldPromoteOrigin(item.promoteOrigin);
    }

    /** Helper class for building the DownloadInfo object. */
    public static class Builder {
        private @Nullable GURL mUrl;
        private @Nullable String mUserAgent;
        private @Nullable String mMimeType;
        private @Nullable String mCookie;
        private @Nullable String mFileName;
        private @Nullable String mDescription;
        private @Nullable String mFilePath;
        private @Nullable GURL mReferrer;
        private @Nullable GURL mOriginalUrl;
        private long mBytesReceived;
        private long mBytesTotalSize;
        private boolean mIsGETRequest;
        private @Nullable String mDownloadGuid;
        private boolean mHasUserGesture;
        private @Nullable String mContentDisposition;
        private @Nullable Progress mProgress = Progress.createIndeterminateProgress();
        private long mTimeRemainingInMillis;
        private boolean mIsResumable = true;
        private boolean mIsPaused;
        private boolean mIsOffTheRecord;
        private @Nullable OtrProfileId mOtrProfileId;
        private boolean mIsOfflinePage;
        private int mState = DownloadState.IN_PROGRESS;
        private long mLastAccessTime;
        private @DownloadDangerType int mDangerType;
        private boolean mIsDangerous;
        private @Nullable ContentId mContentId;
        private boolean mIsOpenable = true;
        private boolean mIsTransient;
        private boolean mIsParallelDownload;
        private @Nullable Bitmap mIcon;
        @PendingState private int mPendingState;
        @FailState private int mFailState;
        private boolean mShouldPromoteOrigin;

        public Builder setUrl(@Nullable GURL url) {
            mUrl = url;
            return this;
        }

        public Builder setUserAgent(@Nullable String userAgent) {
            mUserAgent = userAgent;
            return this;
        }

        public Builder setMimeType(@Nullable String mimeType) {
            mMimeType = mimeType;
            return this;
        }

        public Builder setCookie(@Nullable String cookie) {
            mCookie = cookie;
            return this;
        }

        public Builder setFileName(@Nullable String fileName) {
            mFileName = fileName;
            return this;
        }

        public Builder setDescription(@Nullable String description) {
            mDescription = description;
            return this;
        }

        public Builder setFilePath(@Nullable String filePath) {
            mFilePath = filePath;
            return this;
        }

        public Builder setReferrer(GURL referer) {
            mReferrer = referer;
            return this;
        }

        public Builder setOriginalUrl(@Nullable GURL originalUrl) {
            mOriginalUrl = originalUrl;
            return this;
        }

        public Builder setBytesReceived(long bytesReceived) {
            mBytesReceived = bytesReceived;
            return this;
        }

        public Builder setBytesTotalSize(long bytesTotalSize) {
            mBytesTotalSize = bytesTotalSize;
            return this;
        }

        public Builder setIsGETRequest(boolean isGETRequest) {
            mIsGETRequest = isGETRequest;
            return this;
        }

        public Builder setDownloadGuid(@Nullable String downloadGuid) {
            mDownloadGuid = downloadGuid;
            return this;
        }

        public Builder setHasUserGesture(boolean hasUserGesture) {
            mHasUserGesture = hasUserGesture;
            return this;
        }

        public Builder setContentDisposition(@Nullable String contentDisposition) {
            mContentDisposition = contentDisposition;
            return this;
        }

        public Builder setProgress(OfflineItem.@Nullable Progress progress) {
            mProgress = progress;
            return this;
        }

        public Builder setTimeRemainingInMillis(long timeRemainingInMillis) {
            mTimeRemainingInMillis = timeRemainingInMillis;
            return this;
        }

        public Builder setIsResumable(boolean isResumable) {
            mIsResumable = isResumable;
            return this;
        }

        public Builder setIsPaused(boolean isPaused) {
            mIsPaused = isPaused;
            return this;
        }

        public Builder setOtrProfileId(@Nullable OtrProfileId otrProfileId) {
            mOtrProfileId = otrProfileId;
            mIsOffTheRecord = OtrProfileId.isOffTheRecord(otrProfileId);
            return this;
        }

        public Builder setIsOfflinePage(boolean isOfflinePage) {
            mIsOfflinePage = isOfflinePage;
            return this;
        }

        public Builder setState(int downloadState) {
            mState = downloadState;
            return this;
        }

        public Builder setLastAccessTime(long lastAccessTime) {
            mLastAccessTime = lastAccessTime;
            return this;
        }

        public Builder setDangerType(@DownloadDangerType int dangerType) {
            mDangerType = dangerType;
            return this;
        }

        public Builder setIsDangerous(boolean isDangerous) {
            mIsDangerous = isDangerous;
            return this;
        }

        public Builder setContentId(@Nullable ContentId contentId) {
            mContentId = contentId;
            return this;
        }

        public Builder setIsOpenable(boolean isOpenable) {
            mIsOpenable = isOpenable;
            return this;
        }

        public Builder setIsTransient(boolean isTransient) {
            mIsTransient = isTransient;
            return this;
        }

        public Builder setIsParallelDownload(boolean isParallelDownload) {
            mIsParallelDownload = isParallelDownload;
            return this;
        }

        public Builder setIcon(@Nullable Bitmap icon) {
            mIcon = icon;
            return this;
        }

        public Builder setPendingState(@PendingState int pendingState) {
            mPendingState = pendingState;
            return this;
        }

        public Builder setFailState(@FailState int failState) {
            mFailState = failState;
            return this;
        }

        public Builder setShouldPromoteOrigin(boolean shouldPromoteOrigin) {
            mShouldPromoteOrigin = shouldPromoteOrigin;
            return this;
        }

        public DownloadInfo build() {
            return new DownloadInfo(this);
        }

        /**
         * Create a builder from the DownloadInfo object.
         * @param downloadInfo DownloadInfo object from which builder fields are populated.
         * @return A builder initialized with fields from downloadInfo object.
         */
        public static Builder fromDownloadInfo(final DownloadInfo downloadInfo) {
            Builder builder = new Builder();
            builder.setUrl(downloadInfo.getUrl())
                    .setUserAgent(downloadInfo.getUserAgent())
                    .setMimeType(downloadInfo.getMimeType())
                    .setCookie(downloadInfo.getCookie())
                    .setFileName(downloadInfo.getFileName())
                    .setDescription(downloadInfo.getDescription())
                    .setFilePath(downloadInfo.getFilePath())
                    .setReferrer(downloadInfo.getReferrer())
                    .setOriginalUrl(downloadInfo.getOriginalUrl())
                    .setBytesReceived(downloadInfo.getBytesReceived())
                    .setBytesTotalSize(downloadInfo.getBytesTotalSize())
                    .setDownloadGuid(downloadInfo.getDownloadGuid())
                    .setHasUserGesture(downloadInfo.hasUserGesture())
                    .setContentDisposition(downloadInfo.getContentDisposition())
                    .setIsGETRequest(downloadInfo.isGETRequest())
                    .setProgress(downloadInfo.getProgress())
                    .setTimeRemainingInMillis(downloadInfo.getTimeRemainingInMillis())
                    .setDangerType(downloadInfo.getDangerType())
                    .setIsDangerous(downloadInfo.getIsDangerous())
                    .setIsResumable(downloadInfo.isResumable())
                    .setIsPaused(downloadInfo.isPaused())
                    .setOtrProfileId(downloadInfo.getOtrProfileId())
                    .setIsOfflinePage(downloadInfo.isOfflinePage())
                    .setState(downloadInfo.state())
                    .setLastAccessTime(downloadInfo.getLastAccessTime())
                    .setIsTransient(downloadInfo.getIsTransient())
                    .setIsParallelDownload(downloadInfo.getIsParallelDownload())
                    .setIcon(downloadInfo.getIcon())
                    .setPendingState(downloadInfo.getPendingState())
                    .setFailState(downloadInfo.getFailState())
                    .setShouldPromoteOrigin(downloadInfo.getShouldPromoteOrigin());
            return builder;
        }
    }

    @CalledByNative
    private static DownloadInfo createDownloadInfo(
            @JniType("std::string") String downloadGuid,
            @JniType("std::string") String fileName,
            @JniType("std::string") String filePath,
            @JniType("GURL") GURL url,
            @JniType("std::string") String mimeType,
            long bytesReceived,
            long bytesTotalSize,
            OtrProfileId otrProfileId,
            int state,
            int percentCompleted,
            boolean isPaused,
            boolean hasUserGesture,
            boolean isResumable,
            boolean isParallelDownload,
            @JniType("GURL") GURL originalUrl,
            @JniType("GURL") GURL referrerUrl,
            long timeRemainingInMs,
            long lastAccessTime,
            @DownloadDangerType int dangerType,
            boolean isDangerous,
            @FailState int failState,
            boolean isTransient) {
        String remappedMimeType = MimeUtils.remapGenericMimeType(mimeType, url.getSpec(), fileName);

        Progress progress =
                new Progress(
                        bytesReceived,
                        percentCompleted == -1 ? null : bytesTotalSize,
                        OfflineItemProgressUnit.BYTES);
        return new DownloadInfo.Builder()
                .setBytesReceived(bytesReceived)
                .setBytesTotalSize(bytesTotalSize)
                .setDescription(fileName)
                .setDownloadGuid(downloadGuid)
                .setFileName(fileName)
                .setFilePath(filePath)
                .setHasUserGesture(hasUserGesture)
                .setOtrProfileId(otrProfileId)
                .setIsPaused(isPaused)
                .setIsResumable(isResumable)
                .setIsParallelDownload(isParallelDownload)
                .setMimeType(remappedMimeType)
                .setOriginalUrl(originalUrl)
                .setProgress(progress)
                .setReferrer(referrerUrl)
                .setState(state)
                .setTimeRemainingInMillis(timeRemainingInMs)
                .setLastAccessTime(lastAccessTime)
                .setDangerType(dangerType)
                .setIsDangerous(isDangerous)
                .setUrl(url)
                .setFailState(failState)
                .setIsTransient(isTransient)
                .build();
    }
}
