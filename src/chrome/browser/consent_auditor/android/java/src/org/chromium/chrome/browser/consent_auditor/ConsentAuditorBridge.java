// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.consent_auditor;

import androidx.annotation.StringRes;

import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.base.ThreadUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.google_apis.gaia.GaiaId;

import java.util.List;

/**
 * This class is used to pass consent records from Android Java UI to the C++
 * consent_auditor component.
 */
@NullMarked
public final class ConsentAuditorBridge {
    private static @Nullable ConsentAuditorBridge sInstance;

    /**
     * Records that the user consented to a feature.
     *
     * @param profile The {@link Profile} associated with this consent record.
     * @param gaiaId The Gaia Id for which to record the consent.
     * @param feature The {@link ConsentAuditorFeature} for which to record the consent.
     * @param consentDescription The resource IDs of the text the user read before consenting.
     * @param consentConfirmation The resource ID of the text the user clicked when consenting.
     */
    public void recordConsent(
            Profile profile,
            GaiaId gaiaId,
            @ConsentAuditorFeature int feature,
            List<Integer> consentDescription,
            @StringRes int consentConfirmation) {
        int[] consentDescriptionArray = new int[consentDescription.size()];
        for (int i = 0; i < consentDescription.size(); ++i) {
            consentDescriptionArray[i] = consentDescription.get(i);
        }
        ConsentAuditorBridgeJni.get()
                .recordConsent(
                        ConsentAuditorBridge.this,
                        profile,
                        gaiaId,
                        feature,
                        consentDescriptionArray,
                        consentConfirmation);
    }

    private ConsentAuditorBridge() {}

    /** Returns the singleton bridge object. */
    public static ConsentAuditorBridge getInstance() {
        ThreadUtils.assertOnUiThread();
        if (sInstance == null) sInstance = new ConsentAuditorBridge();
        return sInstance;
    }

    @NativeMethods
    interface Natives {
        void recordConsent(
                ConsentAuditorBridge caller,
                @JniType("Profile*") Profile profile,
                GaiaId gaiaId,
                int feature,
                int[] consentDescription,
                int consentConfirmation);
    }
}
