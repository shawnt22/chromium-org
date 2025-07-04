// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.homepage.settings;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Contains Homepage related enums used for metrics. */
@NullMarked
public final class HomepageMetricsEnums {
    private HomepageMetricsEnums() {}

    /**
     * Possible location type for homepage. Used for Histogram "Settings.Homepage.LocationType"
     * recorded in {@link HomepageManager#recordHomepageLocationType()}.
     *
     * <p>These values are persisted to logs, and should therefore never be renumbered nor reused.
     *
     * <p>A single Homepage URL can be supplied by one of three sources (with precedence between
     * them). That Homepage is then categorized as being The Chrome NTP, or some "OTHER" URL. E.g. A
     * partner can provide their own custom Homepage URL, which will be PARTNER_PROVIDED_OTHER. Some
     * users may want to use something else, e.g. Chrome's NTP which will be USER_CUSTOMIZED_NTP or
     * their own custom URL which will be USER_CUSTOMIZED_NTP.
     */
    @IntDef({
        HomepageLocationType.POLICY_NTP,
        HomepageLocationType.POLICY_OTHER,
        HomepageLocationType.PARTNER_PROVIDED_NTP,
        HomepageLocationType.PARTNER_PROVIDED_OTHER,
        HomepageLocationType.USER_CUSTOMIZED_NTP,
        HomepageLocationType.USER_CUSTOMIZED_OTHER,
        HomepageLocationType.DEFAULT_NTP,
        HomepageLocationType.NUM_ENTRIES
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface HomepageLocationType {
        /** Enterprise policy provided New Tab Page. */
        int POLICY_NTP = 0;

        /** Enterprise policy provided Homepage. */
        int POLICY_OTHER = 1;

        /** Partner provided New Tab Page. */
        int PARTNER_PROVIDED_NTP = 2;

        /** Partner provided Homepage. */
        int PARTNER_PROVIDED_OTHER = 3;

        /** User provided New Tab Page. */
        int USER_CUSTOMIZED_NTP = 4;

        /** User specified Homepage. */
        int USER_CUSTOMIZED_OTHER = 5;

        /** Chrome's default NTP. */
        int DEFAULT_NTP = 6;

        int NUM_ENTRIES = 7;
    }

    /**
     * Used for Histogram "Settings.Homepage.HomeButtonStatus" recorded in {@link
     * org.chromium.chrome.browser.homepage.HomepageManager#recordHomepageStatus()}
     *
     * <p>Note: There is code {@link HomepageManager#setPrefHomepageEnabled()} that could allow
     * Partners to control this switch, however it is currently unused and there does not yet exist
     * a way to differentiate it from user selections.
     */
    @IntDef({
        HomeButtonStatus.POLICY_ON,
        HomeButtonStatus.POLICY_OFF,
        HomeButtonStatus.USER_ON,
        HomeButtonStatus.USER_OFF
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface HomeButtonStatus {

        /** Enterprise force enabled home button. */
        int POLICY_ON = 0;

        /** Enterprise disabled home button. */
        int POLICY_OFF = 1;

        /** User/default enabled home button. */
        int USER_ON = 2;

        /** User disabled home button. */
        int USER_OFF = 3;

        int NUM_ENTRIES = 4;
    }
}
