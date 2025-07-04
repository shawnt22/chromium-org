// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browsing_data;

import android.os.Parcel;
import android.os.Parcelable;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;

/** Requests information about important sites and other forms of browsing data. */
@NullMarked
public class ClearBrowsingDataFetcher
        implements BrowsingDataBridge.ImportantSitesCallback,
                BrowsingDataBridge.OtherFormsOfBrowsingHistoryListener,
                Parcelable {
    // This is a constant on the C++ side.
    private final int mMaxImportantSites;
    // This is the sorted list of important registerable domains. If null, then we haven't finished
    // fetching them yet.
    private String @Nullable [] mSortedImportantDomains;
    // These are the reasons the above domains were chosen as important.
    private int @Nullable [] mSortedImportantDomainReasons;
    // These are full url examples of the domains above. We use them for favicons.
    private String @Nullable [] mSortedExampleOrigins;

    // Whether the dialog about other forms of browsing history should be shown.
    private boolean mIsDialogAboutOtherFormsOfBrowsingHistoryEnabled;

    ClearBrowsingDataFetcher() {
        mMaxImportantSites = BrowsingDataBridge.getMaxImportantSites();
    }

    protected ClearBrowsingDataFetcher(Parcel in) {
        mMaxImportantSites = in.readInt();
        mSortedImportantDomains = in.createStringArray();
        mSortedImportantDomainReasons = in.createIntArray();
        mSortedExampleOrigins = in.createStringArray();
        mIsDialogAboutOtherFormsOfBrowsingHistoryEnabled = in.readByte() != 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(mMaxImportantSites);
        dest.writeStringArray(mSortedImportantDomains);
        dest.writeIntArray(mSortedImportantDomainReasons);
        dest.writeStringArray(mSortedExampleOrigins);
        dest.writeByte((byte) (mIsDialogAboutOtherFormsOfBrowsingHistoryEnabled ? 1 : 0));
    }

    @Override
    public int describeContents() {
        return 0;
    }

    public static final Creator<ClearBrowsingDataFetcher> CREATOR =
            new Creator<>() {
                @Override
                public ClearBrowsingDataFetcher createFromParcel(Parcel in) {
                    return new ClearBrowsingDataFetcher(in);
                }

                @Override
                public ClearBrowsingDataFetcher[] newArray(int size) {
                    return new ClearBrowsingDataFetcher[size];
                }
            };

    /** Fetch important sites if the feature is enabled. */
    public void fetchImportantSites(Profile profile) {
        BrowsingDataBridge.getForProfile(profile).fetchImportantSites(this);
    }

    /**
     * Request information about other forms of browsing history if the history dialog hasn't been
     * shown yet.
     */
    public void requestInfoAboutOtherFormsOfBrowsingHistory(Profile profile) {
        if (!OtherFormsOfHistoryDialogFragment.wasDialogShown()) {
            BrowsingDataBridge.getForProfile(profile)
                    .requestInfoAboutOtherFormsOfBrowsingHistory(this);
        }
    }

    /**
     * @return The maximum number of important sites to show.
     */
    public int getMaxImportantSites() {
        return mMaxImportantSites;
    }

    /**
     * @return Get a sorted list of important registerable domains. If null, then we haven't
     * finished fetching them yet.
     */
    public String @Nullable [] getSortedImportantDomains() {
        return mSortedImportantDomains;
    }

    /**
     * @return The reasons the above domains were chosen as important.
     */
    public int @Nullable [] getSortedImportantDomainReasons() {
        return mSortedImportantDomainReasons;
    }

    /**
     * @return Full url examples of the domains above. We use them for favicons.
     */
    public String @Nullable [] getSortedExampleOrigins() {
        return mSortedExampleOrigins;
    }

    /**
     * @return Whether the dialog about other forms of browsing history should be shown.
     */
    public boolean isDialogAboutOtherFormsOfBrowsingHistoryEnabled() {
        return mIsDialogAboutOtherFormsOfBrowsingHistoryEnabled;
    }

    @Override
    public void onImportantRegisterableDomainsReady(
            String[] domains,
            String[] exampleOrigins,
            int[] importantReasons,
            boolean dialogDisabled) {
        if (dialogDisabled) return;
        // mMaxImportantSites is a constant on the C++ side. While 0 is valid, use 1 as the minimum
        // because histogram code assumes a min >= 1; the underflow bucket will record the 0s.
        RecordHistogram.recordLinearCountHistogram(
                "History.ClearBrowsingData.NumImportant",
                domains.length,
                1,
                mMaxImportantSites + 1,
                mMaxImportantSites + 1);
        mSortedImportantDomains = domains;
        mSortedImportantDomainReasons = importantReasons;
        mSortedExampleOrigins = exampleOrigins;
    }

    @Override
    public void enableDialogAboutOtherFormsOfBrowsingHistory() {
        mIsDialogAboutOtherFormsOfBrowsingHistoryEnabled = true;
    }
}
