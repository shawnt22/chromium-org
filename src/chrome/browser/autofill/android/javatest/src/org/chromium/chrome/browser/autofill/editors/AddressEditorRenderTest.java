// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill.editors;

import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.when;

import static org.chromium.base.ThreadUtils.runOnUiThreadBlocking;
import static org.chromium.base.test.util.Restriction.RESTRICTION_TYPE_NON_LOW_END_DEVICE;
import static org.chromium.chrome.browser.autofill.editors.AddressEditorCoordinator.UserFlow.MIGRATE_EXISTING_ADDRESS_PROFILE;
import static org.chromium.chrome.browser.autofill.editors.AddressEditorCoordinator.UserFlow.SAVE_NEW_ADDRESS_PROFILE;
import static org.chromium.chrome.browser.autofill.editors.AddressEditorCoordinator.UserFlow.UPDATE_EXISTING_ADDRESS_PROFILE;

import android.view.View;

import androidx.test.filters.MediumTest;

import org.junit.AfterClass;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.params.ParameterAnnotations;
import org.chromium.base.test.params.ParameterSet;
import org.chromium.base.test.params.ParameterizedRunner;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.DisableIf;
import org.chromium.base.test.util.DoNotBatch;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Restriction;
import org.chromium.chrome.browser.autofill.AutofillAddress;
import org.chromium.chrome.browser.autofill.AutofillProfileBridge;
import org.chromium.chrome.browser.autofill.AutofillProfileBridgeJni;
import org.chromium.chrome.browser.autofill.PersonalDataManager;
import org.chromium.chrome.browser.autofill.PersonalDataManagerFactory;
import org.chromium.chrome.browser.autofill.PhoneNumberUtil;
import org.chromium.chrome.browser.autofill.PhoneNumberUtilJni;
import org.chromium.chrome.browser.autofill.editors.AddressEditorCoordinator.Delegate;
import org.chromium.chrome.browser.feedback.HelpAndFeedbackLauncher;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.night_mode.ChromeNightModeTestUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.sync.SyncServiceFactory;
import org.chromium.chrome.test.ChromeJUnit4RunnerDelegate;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.components.autofill.AutofillAddressEditorUiInfo;
import org.chromium.components.autofill.AutofillAddressUiComponent;
import org.chromium.components.autofill.AutofillProfile;
import org.chromium.components.autofill.FieldType;
import org.chromium.components.autofill.RecordType;
import org.chromium.components.signin.base.CoreAccountInfo;
import org.chromium.components.signin.identitymanager.IdentityManager;
import org.chromium.components.sync.SyncService;
import org.chromium.google_apis.gaia.GaiaId;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.test.util.NightModeTestUtils;
import org.chromium.ui.test.util.RenderTestRule;
import org.chromium.ui.test.util.RenderTestRule.Component;

import java.util.HashSet;
import java.util.List;

/**
 * These tests render screenshots of the autofill address editor and compare them to a gold
 * standard.
 */
@DoNotBatch(reason = "The tests can't be batched because they run for different set-ups.")
@RunWith(ParameterizedRunner.class)
@ParameterAnnotations.UseRunnerDelegate(ChromeJUnit4RunnerDelegate.class)
@Restriction({DeviceFormFactor.PHONE, RESTRICTION_TYPE_NON_LOW_END_DEVICE})
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@DisableIf.Build(supported_abis_includes = "x86", message = "https://crbug.com/378544621")
@DisableIf.Build(supported_abis_includes = "x86_64", message = "https://crbug.com/378544621")
public class AddressEditorRenderTest {
    private static final String USER_EMAIL = "example@gmail.com";
    private static final List<AutofillAddressUiComponent> SUPPORTED_ADDRESS_FIELDS =
            List.of(
                    new AutofillAddressUiComponent(FieldType.NAME_FULL, "Name", true, true),
                    new AutofillAddressUiComponent(FieldType.COMPANY_NAME, "Company", false, true),
                    new AutofillAddressUiComponent(
                            FieldType.ADDRESS_HOME_STREET_ADDRESS, "Street address", true, true),
                    new AutofillAddressUiComponent(FieldType.ADDRESS_HOME_CITY, "City", true, true),
                    new AutofillAddressUiComponent(
                            FieldType.ADDRESS_HOME_STATE, "State", true, false),
                    new AutofillAddressUiComponent(FieldType.ADDRESS_HOME_ZIP, "ZIP", true, false));

    private static final AutofillProfile sLocalProfile =
            AutofillProfile.builder()
                    .setFullName("Seb Doe")
                    .setCompanyName("Google")
                    .setStreetAddress("111 First St")
                    .setRegion("CA")
                    .setLocality("Los Angeles")
                    .setPostalCode("90291")
                    .setCountryCode("US")
                    .setPhoneNumber("650-253-0000")
                    .setEmailAddress("first@gmail.com")
                    .setLanguageCode("en-US")
                    .build();
    private static final AutofillProfile sAccountProfile =
            AutofillProfile.builder()
                    .setRecordType(RecordType.ACCOUNT)
                    .setFullName("Seb Doe")
                    .setCompanyName("Google")
                    .setStreetAddress("111 First St")
                    .setRegion("CA")
                    .setLocality("Los Angeles")
                    .setPostalCode("90291")
                    .setCountryCode("US")
                    .setPhoneNumber("650-253-0000")
                    .setEmailAddress("first@gmail.com")
                    .setLanguageCode("en-US")
                    .build();

    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    @ParameterAnnotations.ClassParameter
    private static final List<ParameterSet> sClassParams =
            new NightModeTestUtils.NightModeParams().getParameters();

    @Rule
    public final RenderTestRule mRenderTestRule =
            RenderTestRule.Builder.withPublicCorpus()
                    .setRevision(3)
                    .setBugComponent(Component.UI_BROWSER_AUTOFILL)
                    .build();

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private AutofillProfileBridge.Natives mAutofillProfileBridgeJni;
    @Mock private PhoneNumberUtil.Natives mPhoneNumberUtilJni;
    @Mock private IdentityServicesProvider mIdentityServicesProvider;
    @Mock private IdentityManager mIdentityManager;
    @Mock private SyncService mSyncService;
    @Mock private PersonalDataManager mPersonalDataManager;
    @Mock private Profile mProfile;
    @Mock private HelpAndFeedbackLauncher mLauncher;
    @Mock private Delegate mDelegate;

    private AddressEditorCoordinator mAddressEditor;

    private final CoreAccountInfo mAccountInfo =
            CoreAccountInfo.createFromEmailAndGaiaId(USER_EMAIL, new GaiaId("gaia_id"));

    public AddressEditorRenderTest(boolean nightModeEnabled) {
        ChromeNightModeTestUtils.setUpNightModeForChromeActivity(nightModeEnabled);
        mRenderTestRule.setNightModeEnabled(nightModeEnabled);
        mRenderTestRule.setNightModeEnabled(nightModeEnabled);
    }

    @Before
    public void setUp() throws Exception {
        mActivityTestRule.startMainActivityOnBlankPage();
        mActivityTestRule.waitForActivityCompletelyLoaded();

        AutofillProfileBridgeJni.setInstanceForTesting(mAutofillProfileBridgeJni);
        PhoneNumberUtilJni.setInstanceForTesting(mPhoneNumberUtilJni);
        when(mAutofillProfileBridgeJni.getRequiredFields(anyString()))
                .thenReturn(
                        new int[] {
                            FieldType.NAME_FULL,
                            FieldType.ADDRESS_HOME_CITY,
                            FieldType.ADDRESS_HOME_DEPENDENT_LOCALITY,
                            FieldType.ADDRESS_HOME_ZIP
                        });
        runOnUiThreadBlocking(
                () -> {
                    when(mSyncService.isSyncFeatureEnabled()).thenReturn(false);
                    when(mSyncService.getSelectedTypes()).thenReturn(new HashSet());
                    SyncServiceFactory.setInstanceForTesting(mSyncService);

                    when(mPersonalDataManager.getDefaultCountryCodeForNewAddress())
                            .thenReturn("US");
                    PersonalDataManagerFactory.setInstanceForTesting(mPersonalDataManager);

                    ProfileManager.setLastUsedProfileForTesting(mProfile);
                    IdentityServicesProvider.setInstanceForTests(mIdentityServicesProvider);
                    when(mIdentityServicesProvider.getIdentityManager(mProfile))
                            .thenReturn(mIdentityManager);
                    when(mIdentityManager.getPrimaryAccountInfo(anyInt())).thenReturn(mAccountInfo);
                });

        doAnswer(
                        invocation -> {
                            return (String) invocation.getArguments()[0];
                        })
                .when(mPhoneNumberUtilJni)
                .formatForDisplay(anyString(), anyString());
        doAnswer(
                        invocation -> {
                            return (String) invocation.getArguments()[0];
                        })
                .when(mPhoneNumberUtilJni)
                .formatForResponse(anyString());
        when(mPhoneNumberUtilJni.isPossibleNumber(anyString(), anyString())).thenReturn(true);

        setUpAddressUiComponents(SUPPORTED_ADDRESS_FIELDS);
    }

    @AfterClass
    public static void tearDownClass() throws Exception {
        runOnUiThreadBlocking(NightModeTestUtils::tearDownNightModeForBlankUiTestActivity);
    }

    private void setUpAddressUiComponents(List<AutofillAddressUiComponent> addressUiComponents) {
        when(mAutofillProfileBridgeJni.getAddressEditorUiInfo(anyString(), anyString(), anyInt()))
                .thenReturn(new AutofillAddressEditorUiInfo("EN", addressUiComponents));
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    public void editNewAddressProfile() throws Exception {
        View editor =
                runOnUiThreadBlocking(
                        () -> {
                            mAddressEditor =
                                    new AddressEditorCoordinator(
                                            mActivityTestRule.getActivity(),
                                            mDelegate,
                                            mProfile,
                                            /* saveToDisk= */ false);
                            mAddressEditor.showEditorDialog();
                            return mAddressEditor
                                    .getEditorDialogForTesting()
                                    .getContentViewForTest();
                        });
        mRenderTestRule.render(editor, "edit_new_address_profile");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    public void editNewAccountAddressProfile() throws Exception {
        View editor =
                runOnUiThreadBlocking(
                        () -> {
                            when(mPersonalDataManager.isEligibleForAddressAccountStorage())
                                    .thenReturn(true);
                            mAddressEditor =
                                    new AddressEditorCoordinator(
                                            mActivityTestRule.getActivity(),
                                            mDelegate,
                                            mProfile,
                                            /* saveToDisk= */ false);
                            mAddressEditor.showEditorDialog();
                            return mAddressEditor
                                    .getEditorDialogForTesting()
                                    .getContentViewForTest();
                        });
        mRenderTestRule.render(editor, "edit_new_account_address_profile");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    public void editLocalOrSyncableAddressProfile() throws Exception {
        View editor =
                runOnUiThreadBlocking(
                        () -> {
                            when(mPersonalDataManager.isEligibleForAddressAccountStorage())
                                    .thenReturn(true);
                            mAddressEditor =
                                    new AddressEditorCoordinator(
                                            mActivityTestRule.getActivity(),
                                            mDelegate,
                                            mProfile,
                                            new AutofillAddress(
                                                    mActivityTestRule.getActivity(),
                                                    sLocalProfile,
                                                    mPersonalDataManager),
                                            UPDATE_EXISTING_ADDRESS_PROFILE,
                                            /* saveToDisk= */ false);
                            mAddressEditor.showEditorDialog();
                            return mAddressEditor
                                    .getEditorDialogForTesting()
                                    .getContentViewForTest();
                        });
        mRenderTestRule.render(editor, "edit_local_or_syncable_address_profile");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    public void editAccountAddressProfile() throws Exception {
        View editor =
                runOnUiThreadBlocking(
                        () -> {
                            when(mPersonalDataManager.isEligibleForAddressAccountStorage())
                                    .thenReturn(true);
                            mAddressEditor =
                                    new AddressEditorCoordinator(
                                            mActivityTestRule.getActivity(),
                                            mDelegate,
                                            mProfile,
                                            new AutofillAddress(
                                                    mActivityTestRule.getActivity(),
                                                    sAccountProfile,
                                                    mPersonalDataManager),
                                            SAVE_NEW_ADDRESS_PROFILE,
                                            /* saveToDisk= */ false);
                            mAddressEditor.showEditorDialog();
                            return mAddressEditor
                                    .getEditorDialogForTesting()
                                    .getContentViewForTest();
                        });
        mRenderTestRule.render(editor, "edit_account_address_profile");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    public void migrateLocalOrSyncableAddressProfile() throws Exception {
        View editor =
                runOnUiThreadBlocking(
                        () -> {
                            when(mPersonalDataManager.isEligibleForAddressAccountStorage())
                                    .thenReturn(true);
                            mAddressEditor =
                                    new AddressEditorCoordinator(
                                            mActivityTestRule.getActivity(),
                                            mDelegate,
                                            mProfile,
                                            new AutofillAddress(
                                                    mActivityTestRule.getActivity(),
                                                    sLocalProfile,
                                                    mPersonalDataManager),
                                            MIGRATE_EXISTING_ADDRESS_PROFILE,
                                            /* saveToDisk= */ false);
                            mAddressEditor.showEditorDialog();
                            return mAddressEditor
                                    .getEditorDialogForTesting()
                                    .getContentViewForTest();
                        });
        mRenderTestRule.render(editor, "migrate_local_or_syncable_address_profile");
    }
}
