// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import static com.google.common.truth.Truth.assertThat;

import com.google.common.truth.Correspondence;
import com.google.common.truth.Correspondence.BinaryPredicate;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

/**
 * Stores Reporting API reports received by a test collector, providing helper methods for checking
 * whether expected reports were actually received.
 */
class ReportingCollector {
    private final List<JSONObject> mReceivedReports = new ArrayList<>();
    private final Semaphore mReceivedReportsSemaphore = new Semaphore(0);

    /**
     * Stores a batch of uploaded reports.
     *
     * @param payload the POST payload from the upload
     * @return whether the payload was parsed successfully
     */
    public boolean addReports(String payload) {
        try {
            JSONArray reports = new JSONArray(payload);
            int elementCount = 0;
            synchronized (mReceivedReports) {
                for (int i = 0; i < reports.length(); i++) {
                    JSONObject element = reports.optJSONObject(i);
                    if (element != null) {
                        mReceivedReports.add(element);
                        elementCount++;
                    }
                }
            }
            mReceivedReportsSemaphore.release(elementCount);
            return true;
        } catch (JSONException e) {
            return false;
        }
    }

    public void assertContainsReport(String expectedString) {
        JSONObject expectedJson;

        try {
            expectedJson = new JSONObject(expectedString);
        } catch (JSONException e) {
            throw new IllegalArgumentException(e);
        }
        synchronized (mReceivedReports) {
            assertThat(mReceivedReports)
                    .comparingElementsUsing(
                            Correspondence.from(
                                    (BinaryPredicate<JSONObject, JSONObject>)
                                            (actual, expected) ->
                                                    isJSONObjectSubset(expected, actual),
                                    "is a subset of"))
                    .contains(expectedJson);
        }
    }

    /** Waits until the requested number of reports have been received, with a 5-second timeout. */
    public void waitForReports(int reportCount) {
        final int timeoutSeconds = 5;
        try {
            mReceivedReportsSemaphore.tryAcquire(reportCount, timeoutSeconds, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
        }
    }

    /**
     * Checks whether one {@link JSONObject} is a subset of another.  Any fields that appear in
     * {@code lhs} must also appear in {@code rhs}, with the same value.  There can be extra fields
     * in {@code rhs}; if so, they are ignored.
     */
    private boolean isJSONObjectSubset(JSONObject lhs, JSONObject rhs) {
        Iterator<String> keys = lhs.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            Object lhsElement = lhs.opt(key);
            Object rhsElement = rhs.opt(key);

            if (rhsElement == null) {
                // lhs has an element that doesn't appear in rhs
                return false;
            }

            if (lhsElement instanceof JSONObject) {
                if (!(rhsElement instanceof JSONObject)) {
                    return false;
                }
                return isJSONObjectSubset((JSONObject) lhsElement, (JSONObject) rhsElement);
            }

            if (!lhsElement.equals(rhsElement)) {
                return false;
            }
        }
        return true;
    }
}
