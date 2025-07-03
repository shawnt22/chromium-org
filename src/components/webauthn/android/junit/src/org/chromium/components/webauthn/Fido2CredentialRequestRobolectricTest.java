// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.webauthn;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.notNull;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.robolectric.Shadows.shadowOf;

import android.app.Activity;
import android.app.Application;
import android.app.PendingIntent;
import android.content.Context;
import android.credentials.CredentialManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ResultReceiver;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.SmallTest;

import com.google.android.gms.tasks.OnFailureListener;
import com.google.android.gms.tasks.OnSuccessListener;
import com.google.common.collect.ImmutableList;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import org.robolectric.annotation.Config;
import org.robolectric.shadow.api.Shadow;
import org.robolectric.shadows.ShadowApplication;

import org.chromium.base.Callback;
import org.chromium.base.FeatureOverrides;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.blink.mojom.AuthenticatorStatus;
import org.chromium.blink.mojom.Mediation;
import org.chromium.blink.mojom.PublicKeyCredentialCreationOptions;
import org.chromium.blink.mojom.PublicKeyCredentialDescriptor;
import org.chromium.blink.mojom.PublicKeyCredentialRequestOptions;
import org.chromium.blink.mojom.ResidentKeyRequirement;
import org.chromium.blink_public.common.BlinkFeatures;
import org.chromium.components.webauthn.cred_man.CredManHelper;
import org.chromium.components.webauthn.cred_man.CredManSupportProvider;
import org.chromium.components.webauthn.cred_man.ShadowCredentialManager;
import org.chromium.content_public.browser.RenderFrameHost;
import org.chromium.content_public.browser.RenderFrameHost.WebAuthSecurityChecksResults;
import org.chromium.net.GURLUtils;
import org.chromium.net.GURLUtilsJni;
import org.chromium.url.GURL;
import org.chromium.url.Origin;

import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@RunWith(BaseRobolectricTestRunner.class)
@Config(
        manifest = Config.NONE,
        shadows = {
            ShadowCredentialManager.class,
        })
public class Fido2CredentialRequestRobolectricTest {
    private static final String TEST_CHANNEL_EXTRA = "stable";
    private static final Boolean TEST_INCOGNITO_EXTRA = true;
    private static final String TEST_CLIENT_DATA_JSON = "{ClientDataJSON}";

    private Fido2CredentialRequest mRequest;
    private PublicKeyCredentialCreationOptions mCreationOptions;
    private PublicKeyCredentialRequestOptions mRequestOptions;
    private Fido2ApiTestHelper.AuthenticatorCallback mCallback;
    private Origin mOrigin;
    private Bundle mBrowserOptions;
    private FakeFido2ApiCallHelper mFido2ApiCallHelper;

    @Mock private RenderFrameHost mFrameHost;
    @Mock GURLUtils.Natives mGURLUtilsJniMock;
    @Mock Activity mActivity;
    @Mock WebauthnBrowserBridge mBrowserBridgeMock;
    @Mock CredManHelper mCredManHelperMock;
    @Mock Barrier mBarrierMock;
    @Mock WebauthnModeProvider mModeProviderMock;
    @Mock AuthenticationContextProvider mAuthenticationContextProviderMock;
    @Mock GmsCoreGetCredentialsHelper mGmsCoreGetCredentialsHelperMock;

    @Before
    public void setUp() throws Exception {
        FeatureOverrides.newBuilder()
                .disable(BlinkFeatures.SECURE_PAYMENT_CONFIRMATION_BROWSER_BOUND_KEYS)
                .apply();

        MockitoAnnotations.initMocks(this);

        ((ShadowApplication) shadowOf((Application) ApplicationProvider.getApplicationContext()))
                .setSystemService(
                        Context.CREDENTIAL_SERVICE, Shadow.newInstanceOf(CredentialManager.class));

        GURL gurl =
                new GURL(
                        "https://subdomain.example.test:443/content/test/data/android/authenticator.html");
        mOrigin = Origin.create(gurl);

        mBrowserOptions = new Bundle();
        mBrowserOptions.putString("com.android.chrome.CHANNEL", TEST_CHANNEL_EXTRA);
        mBrowserOptions.putBoolean("com.android.chrome.INCOGNITO", TEST_INCOGNITO_EXTRA);

        GURLUtilsJni.setInstanceForTesting(mGURLUtilsJniMock);
        Mockito.when(mGURLUtilsJniMock.getOrigin(any(String.class)))
                .thenReturn("https://subdomain.example.test:443");

        mFido2ApiCallHelper = new FakeFido2ApiCallHelper();
        mFido2ApiCallHelper.setArePlayServicesAvailable(true);
        Fido2ApiCallHelper.overrideInstanceForTesting(mFido2ApiCallHelper);
        GmsCoreGetCredentialsHelper.overrideInstanceForTesting(mGmsCoreGetCredentialsHelperMock);

        mCreationOptions = Fido2ApiTestHelper.createDefaultMakeCredentialOptions();
        // Set rk=required and empty allowlist on the assumption that most test cases care about
        // exercising the passkeys case.
        mCreationOptions.authenticatorSelection.residentKey = ResidentKeyRequirement.REQUIRED;
        mRequestOptions = Fido2ApiTestHelper.createDefaultGetAssertionOptions();
        mRequestOptions.allowCredentials = new PublicKeyCredentialDescriptor[0];
        WebauthnModeProvider.setInstanceForTesting(mModeProviderMock);
        Mockito.when(mModeProviderMock.getWebauthnMode(any())).thenReturn(WebauthnMode.CHROME);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode()).thenReturn(WebauthnMode.CHROME);
        Mockito.when(mAuthenticationContextProviderMock.getIntentSender()).thenReturn(null);
        Mockito.when(mAuthenticationContextProviderMock.getContext()).thenReturn(mActivity);
        Mockito.when(mAuthenticationContextProviderMock.getRenderFrameHost())
                .thenReturn(mFrameHost);
        mRequest = new Fido2CredentialRequest(mAuthenticationContextProviderMock);

        Fido2ApiTestHelper.mockFido2CredentialRequestJni();
        Fido2ApiTestHelper.mockClientDataJson(TEST_CLIENT_DATA_JSON);

        mCallback = Fido2ApiTestHelper.getAuthenticatorCallback();

        Mockito.when(mFrameHost.getLastCommittedURL()).thenReturn(gurl);
        Mockito.when(mFrameHost.getLastCommittedOrigin()).thenReturn(mOrigin);
        Mockito.doAnswer(
                        (invocation) -> {
                            ((Callback<WebAuthSecurityChecksResults>) invocation.getArguments()[4])
                                    .onResult(
                                            new WebAuthSecurityChecksResults(
                                                    AuthenticatorStatus.SUCCESS, false));
                            return null;
                        })
                .when(mFrameHost)
                .performMakeCredentialWebAuthSecurityChecks(
                        any(String.class),
                        any(Origin.class),
                        anyBoolean(),
                        Mockito.nullable(Origin.class),
                        any(Callback.class));
        Mockito.doAnswer(
                        (invocation) -> {
                            ((Callback<WebAuthSecurityChecksResults>) invocation.getArguments()[4])
                                    .onResult(
                                            new WebAuthSecurityChecksResults(
                                                    AuthenticatorStatus.SUCCESS, false));
                            return null;
                        })
                .when(mFrameHost)
                .performGetAssertionWebAuthSecurityChecks(
                        any(String.class),
                        any(Origin.class),
                        anyBoolean(),
                        Mockito.nullable(Origin.class),
                        any(Callback.class));

        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.UPSIDE_DOWN_CAKE,
                /* overrideForcesGpm= */ true);
        mRequest.overrideBrowserBridgeForTesting(mBrowserBridgeMock);
        mRequest.setCredManHelperForTesting(mCredManHelperMock);
        mRequest.setBarrierForTesting(mBarrierMock);
    }

    @After
    public void tearDown() {
        WebauthnModeProvider.setInstanceForTesting(null);
    }

    @Test
    @SmallTest
    public void testMakeCredential() {
        handleMakeCredentialRequest(/* browserOptions= */ null);

        verify(mCredManHelperMock, times(1))
                .startMakeRequest(any(), any(), any(), any(), any(), any());
    }

    @Test
    @SmallTest
    public void testMakeCredential_rkDiscouraged_goesToPlayServices() {
        mCreationOptions.authenticatorSelection.residentKey = ResidentKeyRequirement.DISCOURAGED;

        handleMakeCredentialRequest(mBrowserOptions);

        assertThat(mFido2ApiCallHelper.mMakeCredentialCalled).isTrue();
        assertThat(mFido2ApiCallHelper.getChannelExtraOrNull()).isEqualTo(TEST_CHANNEL_EXTRA);
        assertThat(mFido2ApiCallHelper.getIncognitoExtraOrNull()).isTrue();
        verify(mCredManHelperMock, times(0))
                .startMakeRequest(any(), any(), any(), any(), any(), any());
    }

    @Test
    @SmallTest
    public void testMakeCredential_paymentsEnabled_goesToPlayServices() {
        mCreationOptions.isPaymentCredentialCreation = true;

        handleMakeCredentialRequest(mBrowserOptions);

        assertThat(mFido2ApiCallHelper.mMakeCredentialCalled).isTrue();
        verifyNoInteractions(mCredManHelperMock);
    }

    @Test
    @SmallTest
    public void testMakeCredential_webauthnModeAppAndBelowAndroid14_goesToPlayServices() {
        Mockito.when(mModeProviderMock.getWebauthnMode(any())).thenReturn(WebauthnMode.APP);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode()).thenReturn(WebauthnMode.NONE);
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.TIRAMISU,
                /* overrideForcesGpm= */ false);

        handleMakeCredentialRequest(mBrowserOptions);

        assertThat(mFido2ApiCallHelper.mMakeCredentialCalled).isTrue();
        verifyNoInteractions(mCredManHelperMock);
    }

    @Test
    @SmallTest
    public void testMakeCredential_webauthnModeAppAndAboveAndroid14_goesToCredMan() {
        Mockito.when(mModeProviderMock.getWebauthnMode(any())).thenReturn(WebauthnMode.APP);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode()).thenReturn(WebauthnMode.NONE);

        handleMakeCredentialRequest(mBrowserOptions);

        assertThat(mFido2ApiCallHelper.mMakeCredentialCalled).isFalse();
        verify(mCredManHelperMock)
                .startMakeRequest(
                        any(),
                        any(),
                        /* clientDataJson= */ eq(null),
                        /* clientDataHash= */ eq(null),
                        any(),
                        any());
    }

    @Test
    @SmallTest
    public void testGetAssertion_credManEnabledGpmInCredMan_success() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        handleGetAssertionRequest();

        verify(mGmsCoreGetCredentialsHelperMock, never())
                .getCredentials(
                        any(),
                        any(),
                        eq(GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS),
                        any(),
                        any());

        String originString = Fido2CredentialRequest.convertOriginToString(mOrigin);
        verify(mCredManHelperMock)
                .startGetRequest(
                        eq(mRequestOptions),
                        eq(originString),
                        eq(TEST_CLIENT_DATA_JSON.getBytes()),
                        /* clientDataHash= */ notNull(),
                        /* getCallback= */ any(),
                        /* errorCallback= */ any(),
                        /* ignoreGpm= */ eq(false));

        assertThat(mFido2ApiCallHelper.mPasskeyCacheGetCredentialsCalled).isFalse();
    }

    @Test
    @SmallTest
    public void testGetAssertion_GpmNotInCredMan_callCredManGetCredentialsAndGmsCoreInParallel() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.UPSIDE_DOWN_CAKE,
                /* overrideForcesGpm= */ false);

        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.GET_ASSERTION_NON_GOOGLE,
                Collections.emptyList());

        runFido2ApiSuccessfulCallback();

        String originString = Fido2CredentialRequest.convertOriginToString(mOrigin);
        verify(mCredManHelperMock)
                .startPrefetchRequest(
                        eq(mRequestOptions),
                        eq(originString),
                        eq(TEST_CLIENT_DATA_JSON.getBytes()),
                        /* clientDataHash= */ any(),
                        /* getCallback= */ any(),
                        /* errorCallback= */ any(),
                        /* barrier= */ any(),
                        /* ignoreGpm= */ eq(true));
        verify(mBrowserBridgeMock)
                .onCredentialsDetailsListReceived(
                        eq(mFrameHost), eq(Collections.emptyList()), eq(false), any(), any());
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isFalse();
    }

    @Test
    @SmallTest
    public void testGetAssertion_allowListMatchWithExplicitHash_goesToGmsCore() {
        setGetAssertionRequestOptions(/* hasAllowList= */ true);

        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS,
                ImmutableList.of(createWebauthnCredential()));
        verify(mCredManHelperMock).setNoCredentialsFallback(any());
        verifyNoMoreInteractions(mCredManHelperMock);
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isTrue();
    }

    @Test
    @SmallTest
    public void testGetAssertion_NoCredentials_fallbackToPlayServices() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mFido2ApiCallHelper.mCredentialsError = new IllegalStateException("injected error");

        handleGetAssertionRequest();

        verify(mGmsCoreGetCredentialsHelperMock, never())
                .getCredentials(
                        any(),
                        any(),
                        eq(GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS),
                        any(),
                        any());

        ArgumentCaptor<Runnable> setNoCredentialsParamCaptor =
                ArgumentCaptor.forClass(Runnable.class);
        verify(mCredManHelperMock).setNoCredentialsFallback(setNoCredentialsParamCaptor.capture());
        verify(mCredManHelperMock)
                .startGetRequest(
                        any(), any(), any(), any(), any(), any(), /* ignoreGpm= */ eq(false));

        // Now run the no credentials fallback action:
        setNoCredentialsParamCaptor.getValue().run();

        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isTrue();
        assertThat(mCallback.getStatus())
                .isEqualTo(Integer.valueOf(AuthenticatorStatus.NOT_ALLOWED_ERROR));
        verify(mBrowserBridgeMock, never()).onCredManUiClosed(any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testGetAssertion_allowListNoMatch_goesToCredMan() {
        setGetAssertionRequestOptions(/* hasAllowList= */ true);
        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS,
                Collections.emptyList());
        verify(mCredManHelperMock)
                .startGetRequest(
                        any(), any(), any(), any(), any(), any(), /* ignoreGpm= */ eq(false));
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isFalse();
    }

    @Test
    @SmallTest
    public void testGetAssertion_allowListEnumerationFails_goesToCredMan() {
        setGetAssertionRequestOptions(/* hasAllowList= */ true);
        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerFailure(
                GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS,
                new IllegalStateException("injected error"));

        verify(mCredManHelperMock)
                .startGetRequest(
                        any(), any(), any(), any(), any(), any(), /* ignoreGpm= */ eq(false));
        verify(mCredManHelperMock).setNoCredentialsFallback(any());
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isFalse();
    }

    @Test
    @SmallTest
    public void testGetAssertion_allowListMatch_goesToPlayServices() {
        setGetAssertionRequestOptions(/* hasAllowList= */ true);

        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS,
                ImmutableList.of(createWebauthnCredential()));
        verify(mCredManHelperMock).setNoCredentialsFallback(any());
        verifyNoMoreInteractions(mCredManHelperMock);
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isTrue();
    }

    @Test
    @SmallTest
    public void testGetAssertion_allowListNoMatchAndGpmNotInCredMan_goesToCredMan() {
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.UPSIDE_DOWN_CAKE,
                /* overrideForcesGpm= */ false);
        setGetAssertionRequestOptions(/* hasAllowList= */ true);

        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.CHECK_FOR_MATCHING_CREDENTIALS,
                Collections.emptyList());
        verify(mCredManHelperMock)
                .startGetRequest(
                        any(), any(), any(), any(), any(), any(), /* ignoreGpm= */ eq(true));
        verify(mCredManHelperMock).setNoCredentialsFallback(any());
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isFalse();
    }

    @Test
    @SmallTest
    public void testGetAssertion_WebAuthnModeApp_GoesToPlayServices() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.TIRAMISU,
                /* overrideForcesGpm= */ false);

        Mockito.when(mModeProviderMock.getWebauthnMode(any())).thenReturn(WebauthnMode.APP);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode()).thenReturn(WebauthnMode.NONE);

        handleGetAssertionRequest();

        verifyNoInteractions(mCredManHelperMock);
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isTrue();
    }

    @Test
    @SmallTest
    public void testGetAssertion_WebAuthnModeApp_failsIfGmscoreNotAvailable() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.TIRAMISU,
                /* overrideForcesGpm= */ false);

        Mockito.when(mModeProviderMock.getWebauthnMode(any())).thenReturn(WebauthnMode.APP);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode()).thenReturn(WebauthnMode.NONE);
        mFido2ApiCallHelper.setArePlayServicesAvailable(false);
        Fido2CredentialRequest request =
                new Fido2CredentialRequest(mAuthenticationContextProviderMock);

        request.handleGetAssertionRequest(
                mRequestOptions,
                mOrigin,
                mOrigin,
                /* payment= */ null,
                mCallback::onSignResponse,
                mCallback::onError,
                mCallback::onRequestOutcome);

        verifyNoInteractions(mCredManHelperMock);
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isFalse();
        assertThat(mCallback.getStatus())
                .isEqualTo(Integer.valueOf(AuthenticatorStatus.UNKNOWN_ERROR));
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_success() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.CONDITIONAL;

        handleGetAssertionRequest();

        String originString = Fido2CredentialRequest.convertOriginToString(mOrigin);
        verify(mCredManHelperMock, times(1))
                .startPrefetchRequest(
                        eq(mRequestOptions),
                        eq(originString),
                        eq(TEST_CLIENT_DATA_JSON.getBytes()),
                        /* clientDataHash= */ notNull(),
                        /* getCallback= */ any(),
                        /* errorCallback= */ any(),
                        /* barrier= */ any(),
                        /* ignoreGpm= */ eq(false));
        verify(mBrowserBridgeMock, never()).onCredManUiClosed(any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_GpmNotInCredMan_success() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.CONDITIONAL;
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.UPSIDE_DOWN_CAKE,
                /* overrideForcesGpm= */ false);

        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.GET_ASSERTION_NON_GOOGLE,
                Collections.emptyList());
        runFido2ApiSuccessfulCallback();

        String originString = Fido2CredentialRequest.convertOriginToString(mOrigin);
        verify(mCredManHelperMock, times(1))
                .startPrefetchRequest(
                        eq(mRequestOptions),
                        eq(originString),
                        eq(TEST_CLIENT_DATA_JSON.getBytes()),
                        /* clientDataHash= */ notNull(),
                        /* getCallback= */ any(),
                        /* errorCallback= */ any(),
                        /* barrier= */ any(),
                        /* ignoreGpm= */ eq(true));
        verify(mBrowserBridgeMock, times(1))
                .onCredentialsDetailsListReceived(any(), any(), eq(true), any(), any());
        verify(mBrowserBridgeMock, never()).onCredManUiClosed(any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_webauthnModeNotChrome_notImplemented() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.CONDITIONAL;
        Mockito.when(mModeProviderMock.getWebauthnMode(any())).thenReturn(WebauthnMode.APP);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode()).thenReturn(WebauthnMode.NONE);

        handleGetAssertionRequest();

        assertThat(mCallback.getStatus()).isEqualTo(AuthenticatorStatus.NOT_IMPLEMENTED);
        verifyNoInteractions(mCredManHelperMock);
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_RpCancelWhileIdleWithGpmInCredMan_notAllowedError() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.CONDITIONAL;

        handleGetAssertionRequest();

        mRequest.cancelConditionalGetAssertion();

        // CredManHelper class is responsible to return the status.
        assertThat(mCallback.getStatus()).isEqualTo(null);
        verify(mCredManHelperMock).cancelConditionalGetAssertion();
        verify(mBrowserBridgeMock, never()).cleanupRequest(any());
        verify(mBrowserBridgeMock, never()).onCredManUiClosed(any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_RpCancelWhileIdleWithGpmNotInCredMan_notAllowedError() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.CONDITIONAL;
        CredManSupportProvider.setupForTesting(
                /* overrideAndroidVersion= */ Build.VERSION_CODES.UPSIDE_DOWN_CAKE,
                /* overrideForcesGpm= */ false);

        handleGetAssertionRequest();

        verifyGetCredentialsAndTriggerSuccess(
                GmsCoreGetCredentialsHelper.Reason.GET_ASSERTION_NON_GOOGLE,
                Collections.emptyList());

        runFido2ApiSuccessfulCallback();

        mRequest.cancelConditionalGetAssertion();

        verify(mBarrierMock).onFido2ApiCancelled();
        verify(mCredManHelperMock).cancelConditionalGetAssertion();
        verify(mBrowserBridgeMock).cleanupRequest(any());
        verify(mBrowserBridgeMock, never()).onCredManUiClosed(any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_abortedWhileWaitingForRpIdValidation_aborted() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        // Capture the RP ID validation callback and let the request sit
        // waiting for it.
        var rpIdValidationCallback = new Callback[1];
        Mockito.doAnswer(
                        (invocation) -> {
                            rpIdValidationCallback[0] = (Callback) invocation.getArguments()[4];
                            return null;
                        })
                .when(mFrameHost)
                .performGetAssertionWebAuthSecurityChecks(
                        any(String.class),
                        any(Origin.class),
                        anyBoolean(),
                        Mockito.nullable(Origin.class),
                        any(Callback.class));

        handleGetAssertionRequest();

        // The request should have requested RP ID validation.
        assertThat(rpIdValidationCallback[0]).isNotNull();
        // Aborting the request shouldn't do anything yet because it's waiting
        // for RP ID validation.
        mRequest.cancelConditionalGetAssertion();
        assertThat(mCallback.getStatus()).isEqualTo(null);
        // When the RP ID validation completes, the overall request should then
        // be canceled. Any RP ID validation error should be ignored in favour
        // of `ABORT_ERROR`.
        rpIdValidationCallback[0].onResult(
                new WebAuthSecurityChecksResults(AuthenticatorStatus.NOT_ALLOWED_ERROR, false));
        assertThat(mCallback.getStatus()).isEqualTo(AuthenticatorStatus.ABORT_ERROR);
    }

    @Test
    @SmallTest
    public void testConditionalGetAssertion_webauthnModeChrome3pp_goesToCredMan() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.CONDITIONAL;
        Mockito.when(mModeProviderMock.getWebauthnMode(any()))
                .thenReturn(WebauthnMode.CHROME_3PP_ENABLED);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode())
                .thenReturn(WebauthnMode.CHROME_3PP_ENABLED);

        handleGetAssertionRequest();

        assertThat(mCallback.getStatus()).isNull();
        verify(mCredManHelperMock, times(1))
                .startPrefetchRequest(
                        any(), any(), any(), any(), any(), any(), any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testGetAssertion_webauthnModeChrome3pp_goesToCredMan() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        Mockito.when(mModeProviderMock.getWebauthnMode(any()))
                .thenReturn(WebauthnMode.CHROME_3PP_ENABLED);
        Mockito.when(mModeProviderMock.getGlobalWebauthnMode())
                .thenReturn(WebauthnMode.CHROME_3PP_ENABLED);

        handleGetAssertionRequest();

        verify(mCredManHelperMock, times(1))
                .startGetRequest(any(), any(), any(), any(), any(), any(), anyBoolean());
        assertThat(mFido2ApiCallHelper.mGetAssertionCalled).isFalse();
    }

    @Test
    @SmallTest
    public void testImmediateGetAssertion_success() {
        setGetAssertionRequestOptions(/* hasAllowList= */ false);
        mRequestOptions.mediation = Mediation.IMMEDIATE;

        handleGetAssertionRequest();

        String originString = Fido2CredentialRequest.convertOriginToString(mOrigin);
        verify(mCredManHelperMock)
                .startGetRequest(
                        eq(mRequestOptions),
                        eq(originString),
                        eq(TEST_CLIENT_DATA_JSON.getBytes()),
                        /* clientDataHash= */ notNull(),
                        /* getCallback= */ any(),
                        /* errorCallback= */ any(),
                        /* ignoreGpm= */ eq(false));
        verify(mBrowserBridgeMock, never()).onCredManUiClosed(any(), anyBoolean());
    }

    @Test
    @SmallTest
    public void testImmediateGetAssertion_withAllowList_notAllowed() {
        setGetAssertionRequestOptions(/* hasAllowList= */ true);
        mRequestOptions.mediation = Mediation.IMMEDIATE;

        handleGetAssertionRequest();

        verify(mCredManHelperMock, never())
                .startGetRequest(any(), any(), any(), any(), any(), any(), anyBoolean());
        assertThat(mCallback.getStatus())
                .isEqualTo(Integer.valueOf(AuthenticatorStatus.NOT_ALLOWED_ERROR));
    }

    private void handleMakeCredentialRequest(Bundle browserOptions) {
        mRequest.handleMakeCredentialRequest(
                mCreationOptions,
                browserOptions,
                mOrigin,
                mOrigin,
                /* paymentOptions= */ null,
                mCallback::onRegisterResponse,
                mCallback::onError,
                mCallback::onRequestOutcome);
    }

    private void handleGetAssertionRequest() {
        mRequest.handleGetAssertionRequest(
                mRequestOptions,
                mOrigin,
                mOrigin,
                /* payment= */ null,
                mCallback::onSignResponse,
                mCallback::onError,
                mCallback::onRequestOutcome);
    }

    private void verifyGetCredentialsAndTriggerSuccess(
            GmsCoreGetCredentialsHelper.Reason reason,
            List<WebauthnCredentialDetails> credentials) {
        ArgumentCaptor<GmsCoreGetCredentialsHelper.GetCredentialsCallback> successCallbackCaptor =
                ArgumentCaptor.forClass(GmsCoreGetCredentialsHelper.GetCredentialsCallback.class);
        verify(mGmsCoreGetCredentialsHelperMock)
                .getCredentials(
                        eq(mAuthenticationContextProviderMock),
                        eq(mRequestOptions.relyingPartyId),
                        eq(reason),
                        successCallbackCaptor.capture(),
                        any());
        successCallbackCaptor.getValue().onCredentialsReceived(credentials);
    }

    private void verifyGetCredentialsAndTriggerFailure(
            GmsCoreGetCredentialsHelper.Reason reason, Exception exception) {
        ArgumentCaptor<OnFailureListener> failureCallbackCaptor =
                ArgumentCaptor.forClass(OnFailureListener.class);
        verify(mGmsCoreGetCredentialsHelperMock)
                .getCredentials(
                        eq(mAuthenticationContextProviderMock),
                        eq(mRequestOptions.relyingPartyId),
                        eq(reason),
                        any(),
                        failureCallbackCaptor.capture());
        failureCallbackCaptor.getValue().onFailure(exception);
    }

    private void runFido2ApiSuccessfulCallback() {
        ArgumentCaptor<Runnable> fido2ApiCallSuccessfulRunback =
                ArgumentCaptor.forClass(Runnable.class);
        verify(mBarrierMock).onFido2ApiSuccessful(fido2ApiCallSuccessfulRunback.capture());
        fido2ApiCallSuccessfulRunback.getValue().run();
    }

    private WebauthnCredentialDetails createWebauthnCredential() {
        PublicKeyCredentialDescriptor descriptor = new PublicKeyCredentialDescriptor();
        descriptor.type = 0;
        descriptor.id = new byte[] {1, 2, 3, 4};
        descriptor.transports = new int[] {0};
        mRequestOptions.allowCredentials = new PublicKeyCredentialDescriptor[] {descriptor};

        WebauthnCredentialDetails details = new WebauthnCredentialDetails();
        details.mCredentialId = descriptor.id;
        return details;
    }

    private void setGetAssertionRequestOptions(boolean hasAllowList) {
        if (hasAllowList) {
            PublicKeyCredentialDescriptor descriptor = new PublicKeyCredentialDescriptor();
            descriptor.type = 0;
            descriptor.id = new byte[] {1, 2, 3, 4};
            descriptor.transports = new int[] {0};
            mRequestOptions.allowCredentials = new PublicKeyCredentialDescriptor[] {descriptor};
        } else {
            mRequestOptions.allowCredentials = new PublicKeyCredentialDescriptor[0];
        }
    }

    static class FakeFido2ApiCallHelper extends Fido2ApiCallHelper {
        public boolean mMakeCredentialCalled;
        public boolean mGetAssertionCalled;
        public List<WebauthnCredentialDetails> mCredentials;
        public Exception mCredentialsError;
        public byte[] mClientDataHash;
        public Bundle mBrowserOptions;
        public boolean mFido2GetCredentialsCalled;
        public boolean mPasskeyCacheGetCredentialsCalled;

        private boolean mArePlayServicesAvailable = true;

        @Override
        public boolean arePlayServicesAvailable() {
            return mArePlayServicesAvailable;
        }

        public void setArePlayServicesAvailable(boolean arePlayServicesAvailable) {
            mArePlayServicesAvailable = arePlayServicesAvailable;
        }

        String getChannelExtraOrNull() {
            return mBrowserOptions == null
                    ? null
                    : mBrowserOptions.getString("com.android.chrome.CHANNEL");
        }

        Boolean getIncognitoExtraOrNull() {
            return mBrowserOptions == null
                    ? null
                    : mBrowserOptions.getBoolean("com.android.chrome.INCOGNITO");
        }

        @Override
        public void invokeFido2GetCredentials(
                AuthenticationContextProvider authenticationContextProvider,
                String relyingPartyId,
                OnSuccessListener<List<WebauthnCredentialDetails>> successCallback,
                OnFailureListener failureCallback) {
            mFido2GetCredentialsCalled = true;
            if (mCredentialsError != null) {
                failureCallback.onFailure(mCredentialsError);
                return;
            }

            List<WebauthnCredentialDetails> credentials;
            if (mCredentials == null) {
                credentials = new ArrayList();
            } else {
                credentials = mCredentials;
                mCredentials = null;
            }

            successCallback.onSuccess(credentials);
        }

        @Override
        public void invokePasskeyCacheGetCredentials(
                AuthenticationContextProvider authenticationContextProvider,
                String relyingPartyId,
                OnSuccessListener<List<WebauthnCredentialDetails>> successCallback,
                OnFailureListener failureCallback) {
            mPasskeyCacheGetCredentialsCalled = true;

            List<WebauthnCredentialDetails> credentials;
            if (mCredentials == null) {
                credentials = new ArrayList();
            } else {
                credentials = mCredentials;
                mCredentials = null;
            }

            successCallback.onSuccess(credentials);
        }

        @Override
        public void invokeFido2MakeCredential(
                AuthenticationContextProvider authenticationContextProvider,
                PublicKeyCredentialCreationOptions options,
                Uri uri,
                byte[] clientDataHash,
                Bundle browserOptions,
                ResultReceiver resultReceiver,
                OnSuccessListener<PendingIntent> successCallback,
                OnFailureListener failureCallback)
                throws NoSuchAlgorithmException {
            mMakeCredentialCalled = true;
            mClientDataHash = clientDataHash;
            mBrowserOptions = browserOptions;

            if (mCredentialsError != null) {
                failureCallback.onFailure(mCredentialsError);
                return;
            }
            // Don't make any actual calls to Play Services.
        }

        @Override
        public void invokeFido2GetAssertion(
                AuthenticationContextProvider authenticationContextProvider,
                PublicKeyCredentialRequestOptions options,
                Uri uri,
                byte[] clientDataHash,
                ResultReceiver resultReceiver,
                OnSuccessListener<PendingIntent> successCallback,
                OnFailureListener failureCallback) {
            mGetAssertionCalled = true;
            mClientDataHash = clientDataHash;

            if (mCredentialsError != null) {
                failureCallback.onFailure(mCredentialsError);
                return;
            }
            // Don't make any actual calls to Play Services.
        }
    }
}
