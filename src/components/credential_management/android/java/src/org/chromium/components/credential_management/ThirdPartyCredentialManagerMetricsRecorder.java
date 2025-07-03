// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package org.chromium.components.credential_management;

import androidx.annotation.Nullable;
import androidx.credentials.exceptions.CreateCredentialCancellationException;
import androidx.credentials.exceptions.CreateCredentialException;
import androidx.credentials.exceptions.CreateCredentialInterruptedException;
import androidx.credentials.exceptions.CreateCredentialNoCreateOptionException;
import androidx.credentials.exceptions.CreateCredentialUnknownException;
import androidx.credentials.exceptions.GetCredentialCancellationException;
import androidx.credentials.exceptions.GetCredentialCustomException;
import androidx.credentials.exceptions.GetCredentialException;
import androidx.credentials.exceptions.GetCredentialInterruptedException;
import androidx.credentials.exceptions.GetCredentialProviderConfigurationException;
import androidx.credentials.exceptions.GetCredentialUnknownException;
import androidx.credentials.exceptions.GetCredentialUnsupportedException;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.build.annotations.NullMarked;

/** Helper class for recording third party credential manager metrics. */
@NullMarked
public final class ThirdPartyCredentialManagerMetricsRecorder {
    public static final String STORE_RESULT_HISTOGRAM_NAME =
            "PasswordManager.CredentialRequest.ThirdParty.Store";
    public static final String GET_RESULT_HISTOGRAM_NAME =
            "PasswordManager.CredentialRequest.ThirdParty.Get";

    private ThirdPartyCredentialManagerMetricsRecorder() {}

    public static void recordCredentialManagerStoreResult(
            boolean success, @Nullable CreateCredentialException error) {
        int result = CredentialManagerStoreResult.SUCCESS;
        if (!success) {
            if (error instanceof CreateCredentialCancellationException) {
                result = CredentialManagerStoreResult.USER_CANCELED;
            } else if (error instanceof CreateCredentialNoCreateOptionException) {
                result = CredentialManagerStoreResult.NO_CREATE_OPTIONS;
            } else if (error instanceof CreateCredentialInterruptedException) {
                result = CredentialManagerStoreResult.INTERRUPTED;
            } else if (error instanceof CreateCredentialUnknownException) {
                result = CredentialManagerStoreResult.UNKNOWN;
            } else {
                result = CredentialManagerStoreResult.UNEXPECTED_ERROR;
            }
        }
        RecordHistogram.recordEnumeratedHistogram(
                STORE_RESULT_HISTOGRAM_NAME, result, CredentialManagerStoreResult.COUNT);
    }

    public static void recordCredentialManagerGetResult(
            boolean success, @Nullable GetCredentialException error) {
        int result = CredentialManagerAndroidGetResult.SUCCESS;
        if (!success) {
            if (error instanceof GetCredentialCancellationException) {
                result = CredentialManagerAndroidGetResult.USER_CANCELED;
            } else if (error instanceof GetCredentialCustomException) {
                result = CredentialManagerAndroidGetResult.CUSTOM_ERROR;
            } else if (error instanceof GetCredentialInterruptedException) {
                result = CredentialManagerAndroidGetResult.INTERRUPTED;
            } else if (error instanceof GetCredentialProviderConfigurationException) {
                result = CredentialManagerAndroidGetResult.PROVIDER_CONFIGURATION_ERROR;
            } else if (error instanceof GetCredentialUnknownException) {
                result = CredentialManagerAndroidGetResult.UNKNOWN;
            } else if (error instanceof GetCredentialUnsupportedException) {
                result = CredentialManagerAndroidGetResult.UNSUPPORTED;
            } else {
                result = CredentialManagerAndroidGetResult.UNEXPECTED_ERROR;
            }
        }
        RecordHistogram.recordEnumeratedHistogram(
                GET_RESULT_HISTOGRAM_NAME, result, CredentialManagerAndroidGetResult.COUNT);
    }
}
