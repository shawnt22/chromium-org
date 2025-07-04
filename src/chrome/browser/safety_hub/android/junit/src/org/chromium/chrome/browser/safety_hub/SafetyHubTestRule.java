// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.safety_hub;

import static org.mockito.Mockito.when;

import android.app.PendingIntent;

import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.chrome.browser.password_manager.FakePasswordCheckupClientHelper;
import org.chromium.chrome.browser.password_manager.FakePasswordCheckupClientHelperFactoryImpl;
import org.chromium.chrome.browser.password_manager.FakePasswordManagerBackendSupportHelper;
import org.chromium.chrome.browser.password_manager.PasswordCheckupClientHelperFactory;
import org.chromium.chrome.browser.password_manager.PasswordManagerBackendSupportHelper;
import org.chromium.chrome.browser.password_manager.PasswordManagerHelper;
import org.chromium.chrome.browser.password_manager.PasswordManagerHelperJni;
import org.chromium.chrome.browser.password_manager.PasswordManagerUtilBridge;
import org.chromium.chrome.browser.password_manager.PasswordManagerUtilBridgeJni;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.signin.services.SigninManager;
import org.chromium.chrome.browser.sync.SyncServiceFactory;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.signin.base.CoreAccountInfo;
import org.chromium.components.signin.identitymanager.ConsentLevel;
import org.chromium.components.signin.identitymanager.IdentityManager;
import org.chromium.components.sync.SyncService;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.components.user_prefs.UserPrefsJni;
import org.chromium.google_apis.gaia.GaiaId;

/**
 * A TestRule that sets up the necessary mocks and helper methods that are needed to run Safety Hub
 * tests.
 */
public class SafetyHubTestRule implements TestRule {
    private static final String TEST_EMAIL_ADDRESS = "test@email.com";

    @Mock private Profile mProfile;
    @Mock private PrefService mPrefService;
    @Mock private UserPrefs.Natives mUserPrefsNatives;
    @Mock private PasswordManagerUtilBridge.Natives mPasswordManagerUtilBridgeNatives;
    @Mock private PasswordManagerHelper.Natives mPasswordManagerHelperNativeMock;
    @Mock private IdentityServicesProvider mIdentityServicesProvider;
    @Mock private IdentityManager mIdentityManager;
    @Mock private SigninManager mSigninManager;
    @Mock private SyncService mSyncService;
    @Mock private PendingIntent mPasswordCheckIntentForAccountCheckup;
    @Mock private PendingIntent mPasswordCheckIntentForLocalCheckup;

    private FakePasswordCheckupClientHelper mFakePasswordCheckupClientHelper;

    private void setUp() {
        MockitoAnnotations.initMocks(this);
        UserPrefsJni.setInstanceForTesting(mUserPrefsNatives);
        PasswordManagerUtilBridgeJni.setInstanceForTesting(mPasswordManagerUtilBridgeNatives);
        PasswordManagerHelperJni.setInstanceForTesting(mPasswordManagerHelperNativeMock);

        ProfileManager.setLastUsedProfileForTesting(mProfile);
        when(mProfile.getOriginalProfile()).thenReturn(mProfile);
        when(mUserPrefsNatives.get(mProfile)).thenReturn(mPrefService);
        when(mIdentityServicesProvider.getSigninManager(mProfile)).thenReturn(mSigninManager);
        when(mSigninManager.getIdentityManager()).thenReturn(mIdentityManager);
        when(mIdentityServicesProvider.getIdentityManager(mProfile)).thenReturn(mIdentityManager);

        IdentityServicesProvider.setInstanceForTests(mIdentityServicesProvider);
        SyncServiceFactory.setInstanceForTesting(mSyncService);
        setUpPasswordManagerBackendForTesting();
        setSignedInState(true);
    }

    private void setUpPasswordManagerBackendForTesting() {
        FakePasswordManagerBackendSupportHelper helper =
                new FakePasswordManagerBackendSupportHelper();
        helper.setBackendPresent(true);
        PasswordManagerBackendSupportHelper.setInstanceForTesting(helper);

        setUpFakePasswordCheckupClientHelper();
    }

    private void setUpFakePasswordCheckupClientHelper() {
        FakePasswordCheckupClientHelperFactoryImpl passwordCheckupClientHelperFactory =
                new FakePasswordCheckupClientHelperFactoryImpl();
        PasswordCheckupClientHelperFactory.setFactoryForTesting(passwordCheckupClientHelperFactory);
        mFakePasswordCheckupClientHelper =
                (FakePasswordCheckupClientHelper) passwordCheckupClientHelperFactory.createHelper();
        mFakePasswordCheckupClientHelper.setIntentForAccountCheckup(
                mPasswordCheckIntentForAccountCheckup);
        mFakePasswordCheckupClientHelper.setIntentForLocalCheckup(
                mPasswordCheckIntentForLocalCheckup);
    }

    public void setSignedInState(boolean isSignedIn) {
        when(mIdentityManager.hasPrimaryAccount(ConsentLevel.SIGNIN)).thenReturn(isSignedIn);
        when(mIdentityManager.getPrimaryAccountInfo(ConsentLevel.SIGNIN))
                .thenReturn(
                        isSignedIn
                                ? CoreAccountInfo.createFromEmailAndGaiaId(
                                        TEST_EMAIL_ADDRESS, new GaiaId("0"))
                                : null);
    }

    public void setPasswordManagerAvailable(
            boolean isPasswordManagerAvailable, boolean isLoginDbDeprecationEnabled) {
        if (isLoginDbDeprecationEnabled) {
            when(mPasswordManagerUtilBridgeNatives.isPasswordManagerAvailable(mPrefService, true))
                    .thenReturn(isPasswordManagerAvailable);
        } else {
            when(mPasswordManagerUtilBridgeNatives.shouldUseUpmWiring(mSyncService, mPrefService))
                    .thenReturn(isPasswordManagerAvailable);
            when(mPasswordManagerUtilBridgeNatives.areMinUpmRequirementsMet())
                    .thenReturn(isPasswordManagerAvailable);
        }
    }

    public PendingIntent getIntentForAccountPasswordCheckup() {
        return mPasswordCheckIntentForAccountCheckup;
    }

    public PendingIntent getIntentForLocalPasswordCheckup() {
        return mPasswordCheckIntentForLocalCheckup;
    }

    public FakePasswordCheckupClientHelper getPasswordCheckupClientHelper() {
        return mFakePasswordCheckupClientHelper;
    }

    public Profile getProfile() {
        return mProfile;
    }

    public PrefService getPrefService() {
        return mPrefService;
    }

    @Override
    public Statement apply(Statement base, Description description) {
        return new Statement() {
            @Override
            public void evaluate() throws Throwable {
                setUp();
                base.evaluate();
            }
        };
    }
}
