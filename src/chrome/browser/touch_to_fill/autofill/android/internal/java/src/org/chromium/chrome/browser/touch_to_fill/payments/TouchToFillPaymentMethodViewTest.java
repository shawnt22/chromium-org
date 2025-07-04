// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill.payments;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.matcher.ViewMatchers.assertThat;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.timeout;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import static org.chromium.base.ThreadUtils.runOnUiThreadBlocking;
import static org.chromium.base.test.util.CriteriaHelper.pollUiThread;
import static org.chromium.chrome.browser.autofill.AutofillTestHelper.createClickActionWithFlags;
import static org.chromium.chrome.browser.autofill.AutofillTestHelper.createCreditCard;
import static org.chromium.chrome.browser.autofill.AutofillTestHelper.createCreditCardSuggestion;
import static org.chromium.chrome.browser.autofill.AutofillTestHelper.createLocalCreditCard;
import static org.chromium.chrome.browser.autofill.AutofillTestHelper.createVirtualCreditCard;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.BACK_PRESS_HANDLER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ButtonProperties.ON_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ButtonProperties.TEXT_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CURRENT_SCREEN;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.APPLY_DEACTIVATED_STYLE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.FIRST_LINE_LABEL;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.ITEM_COLLECTION_INFO;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.MAIN_TEXT;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.MAIN_TEXT_CONTENT_DESCRIPTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.MINOR_TEXT;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.NON_TRANSFORMING_CREDIT_CARD_SUGGESTION_KEYS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.ON_CREDIT_CARD_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.SECOND_LINE_LABEL;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.DISMISS_HANDLER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.HeaderProperties.IMAGE_DRAWABLE_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.HeaderProperties.TITLE_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.IBAN_NICKNAME;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.IBAN_VALUE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.NON_TRANSFORMING_IBAN_KEYS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.ON_IBAN_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.ALL_LOYALTY_CARDS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.CREDIT_CARD;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.FILL_BUTTON;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.HEADER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.IBAN;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.LOYALTY_CARD;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ItemType.TERMS_LABEL;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.LOYALTY_CARD_NUMBER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.MERCHANT_NAME;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.NON_TRANSFORMING_LOYALTY_CARD_KEYS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.ON_LOYALTY_CARD_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.SHEET_ITEMS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ScreenId.ALL_LOYALTY_CARDS_SCREEN;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ScreenId.HOME_SCREEN;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.TermsLabelProperties.ALL_TERMS_LABEL_KEYS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.TermsLabelProperties.CARD_BENEFITS_TERMS_AVAILABLE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.VISIBLE;

import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.IdRes;
import androidx.annotation.StringRes;
import androidx.recyclerview.widget.RecyclerView;
import androidx.test.filters.MediumTest;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.Callback;
import org.chromium.base.CallbackUtils;
import org.chromium.base.ContextUtils;
import org.chromium.base.ServiceLoaderUtil;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.DoNotBatch;
import org.chromium.base.test.util.Features.DisableFeatures;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.base.test.util.ScalableTimeout;
import org.chromium.chrome.browser.autofill.AutofillUiUtils;
import org.chromium.chrome.browser.autofill.PersonalDataManager.CreditCard;
import org.chromium.chrome.browser.autofill.PersonalDataManager.Iban;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.touch_to_fill.common.FillableItemCollectionInfo;
import org.chromium.chrome.browser.touch_to_fill.common.TouchToFillResourceProvider;
import org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.AllLoyaltyCardsItemProperties;
import org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ButtonProperties;
import org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.HeaderProperties;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.components.autofill.AutofillSuggestion;
import org.chromium.components.autofill.LoyaltyCard;
import org.chromium.components.autofill.PaymentsPayload;
import org.chromium.components.autofill.SuggestionType;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController.SheetState;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetTestSupport;
import org.chromium.ui.accessibility.AccessibilityState;
import org.chromium.ui.modelutil.MVCListAdapter.ListItem;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;
import org.chromium.url.GURL;

import java.util.Collections;

/** Tests for {@link TouchToFillPaymentMethodView} */
@RunWith(ChromeJUnit4ClassRunner.class)
@DoNotBatch(reason = "The methods of ChromeAccessibilityUtil don't seem to work with batching.")
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class TouchToFillPaymentMethodViewTest {
    private static final CreditCard VISA =
            createCreditCard(
                    "Visa",
                    "4111111111111111",
                    "5",
                    "2050",
                    true,
                    "Visa",
                    "• • • • 1111",
                    0,
                    "visa");
    private static final CreditCard NICKNAMED_VISA =
            createCreditCard(
                    "Visa",
                    "4111111111111111",
                    "5",
                    "2050",
                    true,
                    "Best Card",
                    "• • • • 1111",
                    0,
                    "visa");
    private static final CreditCard MASTERCARD =
            createLocalCreditCard("MasterCard", "5555555555554444", "8", "2050");
    private static final CreditCard VIRTUAL_CARD =
            createVirtualCreditCard(
                    /* name= */ "Mojo Jojo",
                    /* number= */ "4111111111111111",
                    /* month= */ "4",
                    /* year= */ "2090",
                    /* network= */ "Visa",
                    /* iconId= */ 0,
                    /* cardNameForAutofillDisplay= */ "Visa",
                    /* obfuscatedLastFourDigits= */ "1111");
    private static final CreditCard LONG_CARD_NAME_CARD =
            createCreditCard(
                    "MJ",
                    "4111111111111111",
                    "5",
                    "2050",
                    false,
                    "How much wood would a woodchuck chuck if a woodchuck could chuck wood",
                    "• • • • 1111",
                    0,
                    "visa");
    private static final AutofillSuggestion VISA_SUGGESTION =
            createCreditCardSuggestion(
                    VISA.getCardNameForAutofillDisplay(),
                    VISA.getObfuscatedLastFourDigits(),
                    VISA.getFormattedExpirationDate(ContextUtils.getApplicationContext()),
                    /* secondarySubLabel= */ "",
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL(""),
                    VISA.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ false,
                    VISA.getGUID(),
                    VISA.getIsLocal());
    private static final AutofillSuggestion VISA_SUGGESTION_WITH_CARD_BENEFITS =
            createCreditCardSuggestion(
                    VISA.getCardNameForAutofillDisplay(),
                    VISA.getObfuscatedLastFourDigits(),
                    /* subLabel= */ "2% cashback on travel",
                    VISA.getFormattedExpirationDate(ContextUtils.getApplicationContext()),
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL("http://www.example.com"),
                    VISA.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ true,
                    VISA.getGUID(),
                    VISA.getIsLocal());
    private static final AutofillSuggestion NICKNAMED_VISA_SUGGESTION =
            createCreditCardSuggestion(
                    NICKNAMED_VISA.getCardNameForAutofillDisplay(),
                    NICKNAMED_VISA.getObfuscatedLastFourDigits(),
                    NICKNAMED_VISA.getFormattedExpirationDate(ContextUtils.getApplicationContext()),
                    /* secondarySubLabel= */ "",
                    String.format(
                            "%s %s",
                            NICKNAMED_VISA.getCardNameForAutofillDisplay(),
                            NICKNAMED_VISA.getBasicCardIssuerNetwork()),
                    /* suggestionType= */ SuggestionType.CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL("http://www.example.com"),
                    NICKNAMED_VISA.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ false,
                    NICKNAMED_VISA.getGUID(),
                    NICKNAMED_VISA.getIsLocal());
    private static final AutofillSuggestion MASTERCARD_SUGGESTION =
            createCreditCardSuggestion(
                    MASTERCARD.getName(),
                    MASTERCARD.getNumber(),
                    MASTERCARD.getFormattedExpirationDate(ContextUtils.getApplicationContext()),
                    /* secondarySubLabel= */ "",
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL("http://www.example.com"),
                    MASTERCARD.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ false,
                    MASTERCARD.getGUID(),
                    MASTERCARD.getIsLocal());
    private static final AutofillSuggestion VIRTUAL_CARD_SUGGESTION =
            createCreditCardSuggestion(
                    VIRTUAL_CARD.getCardNameForAutofillDisplay(),
                    VIRTUAL_CARD.getObfuscatedLastFourDigits(),
                    /* subLabel= */ "Virtual card",
                    /* secondarySubLabel= */ "",
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.VIRTUAL_CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL(AutofillUiUtils.CAPITAL_ONE_ICON_URL),
                    VIRTUAL_CARD.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ false,
                    VIRTUAL_CARD.getGUID(),
                    VIRTUAL_CARD.getIsLocal());
    private static final AutofillSuggestion VIRTUAL_CARD_SUGGESTION_WITH_CARD_BENEFITS =
            createCreditCardSuggestion(
                    VIRTUAL_CARD.getCardNameForAutofillDisplay(),
                    VIRTUAL_CARD.getObfuscatedLastFourDigits(),
                    /* subLabel= */ "2% cashback on travel",
                    /* secondarySubLabel= */ "Virtual card",
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.VIRTUAL_CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL("http://www.example.com"),
                    VIRTUAL_CARD.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ true,
                    VIRTUAL_CARD.getGUID(),
                    VIRTUAL_CARD.getIsLocal());
    private static final AutofillSuggestion NON_ACCEPTABLE_VIRTUAL_CARD_SUGGESTION =
            createCreditCardSuggestion(
                    VIRTUAL_CARD.getCardNameForAutofillDisplay(),
                    VIRTUAL_CARD.getObfuscatedLastFourDigits(),
                    /* subLabel= */ "Merchant doesn't accept this virtual card",
                    /* secondarySubLabel= */ "",
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.VIRTUAL_CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL("http://www.example.com"),
                    VIRTUAL_CARD.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ true,
                    /* shouldDisplayTermsAvailable= */ false,
                    VIRTUAL_CARD.getGUID(),
                    VIRTUAL_CARD.getIsLocal());
    private static final AutofillSuggestion LONG_CARD_NAME_CARD_SUGGESTION =
            createCreditCardSuggestion(
                    LONG_CARD_NAME_CARD.getCardNameForAutofillDisplay(),
                    LONG_CARD_NAME_CARD.getObfuscatedLastFourDigits(),
                    LONG_CARD_NAME_CARD.getFormattedExpirationDate(
                            ContextUtils.getApplicationContext()),
                    /* secondarySubLabel= */ "",
                    /* labelContentDescription= */ "",
                    /* suggestionType= */ SuggestionType.CREDIT_CARD_ENTRY,
                    /* customIconUrl= */ new GURL("http://www.example.com"),
                    LONG_CARD_NAME_CARD.getIssuerIconDrawableId(),
                    /* applyDeactivatedStyle= */ false,
                    /* shouldDisplayTermsAvailable= */ false,
                    LONG_CARD_NAME_CARD.getGUID(),
                    LONG_CARD_NAME_CARD.getIsLocal());
    private static final Iban LOCAL_IBAN =
            Iban.createLocal(
                    /* guid= */ "000000111111",
                    /* label= */ "CH56 **** **** **** *800 9",
                    /* nickname= */ "My brother's IBAN",
                    /* value= */ "CH5604835012345678009");
    private static final Iban LOCAL_IBAN_NO_NICKNAME =
            Iban.createLocal(
                    /* guid= */ "000000111111",
                    /* label= */ "CH56 **** **** **** *800 9",
                    /* nickname= */ "",
                    /* value= */ "CH5604835012345678009");
    private static final LoyaltyCard CVS_LOYALTY_CARD =
            new LoyaltyCard(
                    /* loyaltyCardId= */ "cvs",
                    /* merchantName= */ "CVS Pharmacy",
                    /* programName= */ "Loyalty program",
                    /* programLogo= */ new GURL("https://site.com/icon.png"),
                    /* loyaltyCardNumber= */ "1234",
                    /* merchantDomains= */ Collections.emptyList());

    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    @Mock private Callback<Integer> mDismissCallback;
    @Mock private Runnable mBackPressHandler;
    @Mock private FillableItemCollectionInfo mItemCollectionInfo;
    @Mock private TouchToFillResourceProvider mResourceProvider;

    private BottomSheetController mBottomSheetController;
    private BottomSheetTestSupport mSheetTestSupport;
    private TouchToFillPaymentMethodView mTouchToFillPaymentMethodView;
    private PropertyModel mTouchToFillPaymentMethodModel;

    @Before
    public void setupTest() throws InterruptedException {
        ServiceLoaderUtil.setInstanceForTesting(
                TouchToFillResourceProvider.class, mResourceProvider);
        when(mResourceProvider.getLoyaltyCardHeaderDrawableId())
                .thenReturn(R.drawable.ic_globe_24dp);
        mActivityTestRule.startMainActivityOnBlankPage();
        mBottomSheetController =
                mActivityTestRule
                        .getActivity()
                        .getRootUiCoordinatorForTesting()
                        .getBottomSheetController();
        mSheetTestSupport = new BottomSheetTestSupport(mBottomSheetController);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel =
                            new PropertyModel.Builder(TouchToFillPaymentMethodProperties.ALL_KEYS)
                                    .with(VISIBLE, false)
                                    .with(CURRENT_SCREEN, HOME_SCREEN)
                                    .with(SHEET_ITEMS, new ModelList())
                                    .with(BACK_PRESS_HANDLER, mBackPressHandler)
                                    .with(DISMISS_HANDLER, mDismissCallback)
                                    .build();
                    mTouchToFillPaymentMethodView =
                            new TouchToFillPaymentMethodView(
                                    mActivityTestRule.getActivity(), mBottomSheetController);
                    PropertyModelChangeProcessor.create(
                            mTouchToFillPaymentMethodModel,
                            mTouchToFillPaymentMethodView,
                            TouchToFillPaymentMethodViewBinder::bindTouchToFillPaymentMethodView);
                });
    }

    @Test
    @MediumTest
    public void testInitializesHomeScreen() {
        assertNotNull(mTouchToFillPaymentMethodView.getSheetItemListView());
        assertNotNull(mTouchToFillPaymentMethodView.getSheetItemListView().getAdapter());
    }

    @Test
    @MediumTest
    public void testHeaderItem() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(HEADER, createHeaderModel()));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        ImageView brandingIcon =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.branding_icon);
        assertThat(brandingIcon.isShown(), is(true));
        TextView title =
                mTouchToFillPaymentMethodView
                        .getContentView()
                        .findViewById(R.id.touch_to_fill_sheet_title);
        assertThat(
                title.getText().toString(),
                is(getString(R.string.autofill_loyalty_card_bottom_sheet_title)));
    }

    @Test
    @MediumTest
    public void testVisibilityChangedByModel() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION, mItemCollectionInfo)));
                });
        // After setting the visibility to true, the view should exist and be visible.
        runOnUiThreadBlocking(() -> mTouchToFillPaymentMethodModel.set(VISIBLE, true));
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);
        assertThat(mTouchToFillPaymentMethodView.getContentView().isShown(), is(true));

        // After hiding the view, the view should still exist but be invisible.
        runOnUiThreadBlocking(() -> mTouchToFillPaymentMethodModel.set(VISIBLE, false));
        pollUiThread(() -> getBottomSheetState() == BottomSheetController.SheetState.HIDDEN);
        assertThat(mTouchToFillPaymentMethodView.getContentView().isShown(), is(false));
    }

    @Test
    @MediumTest
    public void testCredentialsChangedByModel() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION, mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    MASTERCARD_SUGGESTION, mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VIRTUAL_CARD_SUGGESTION, mItemCollectionInfo)));
                });

        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        assertThat(getCreditCardSuggestions().getChildCount(), is(3));

        assertThat(getSuggestionMainTextAt(0).getText(), is(VISA_SUGGESTION.getLabel()));
        assertThat(getSuggestionMinorTextAt(0).getText(), is(VISA_SUGGESTION.getSecondaryLabel()));
        assertThat(
                getSuggestionFirstLineLabelAt(0).getText(),
                is(VISA.getFormattedExpirationDate(ContextUtils.getApplicationContext())));

        assertThat(getSuggestionMainTextAt(1).getText(), is(MASTERCARD_SUGGESTION.getLabel()));
        assertThat(
                getSuggestionMinorTextAt(1).getText(),
                is(MASTERCARD_SUGGESTION.getSecondaryLabel()));
        assertThat(
                getSuggestionFirstLineLabelAt(1).getText(),
                is(MASTERCARD.getFormattedExpirationDate(ContextUtils.getApplicationContext())));

        assertThat(getSuggestionMainTextAt(2).getText(), is(VIRTUAL_CARD_SUGGESTION.getLabel()));
        assertThat(
                getSuggestionMinorTextAt(2).getText(),
                is(VIRTUAL_CARD_SUGGESTION.getSecondaryLabel()));
        assertThat(
                getSuggestionFirstLineLabelAt(2).getText(),
                is(VIRTUAL_CARD_SUGGESTION.getSublabel()));
    }

    @Test
    @MediumTest
    public void testSheetStartsInFullHeightForAccessibility() {
        // Enabling the accessibility settings.
        runOnUiThreadBlocking(
                () -> {
                    AccessibilityState.setIsTouchExplorationEnabledForTesting(true);
                });

        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION, mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        // The sheet should be expanded to full height.
        pollUiThread(() -> getBottomSheetState() == BottomSheetController.SheetState.FULL);

        // Disabling the accessibility settings.
        runOnUiThreadBlocking(
                () -> {
                    AccessibilityState.setIsTouchExplorationEnabledForTesting(false);
                });
    }

    @Test
    @MediumTest
    public void testSheetStartsInHalfHeightForAccessibilityDisabled() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION, mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        // The sheet should be expanded to half height if possible, the half height state is
        // disabled on small screens.
        @BottomSheetController.SheetState
        int desiredState =
                mBottomSheetController.isSmallScreen()
                        ? BottomSheetController.SheetState.FULL
                        : BottomSheetController.SheetState.HALF;
        pollUiThread(() -> getBottomSheetState() == desiredState);
    }

    @Test
    @MediumTest
    public void testSheetScrollabilityDependsOnState() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION, mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        // The sheet should be expanded to the half height and scrolling suppressed, unless
        // the half height state is disabled due to the device having too small a screen.
        RecyclerView recyclerView = mTouchToFillPaymentMethodView.getSheetItemListView();
        assertEquals(!mBottomSheetController.isSmallScreen(), recyclerView.isLayoutSuppressed());

        // Expand the sheet to the full height and scrolling .
        runOnUiThreadBlocking(
                () ->
                        mSheetTestSupport.setSheetState(
                                BottomSheetController.SheetState.FULL, false));
        BottomSheetTestSupport.waitForState(
                mBottomSheetController, BottomSheetController.SheetState.FULL);

        assertFalse(recyclerView.isLayoutSuppressed());
    }

    @Test
    @MediumTest
    @DisableFeatures({ChromeFeatureList.AUTOFILL_ENABLE_SECURITY_TOUCH_EVENT_FILTERING_ANDROID})
    public void testCreditCardSuggestionViewProcessesClicksThroughObscuredSurfaces() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    NICKNAMED_VISA_SUGGESTION,
                                                    mItemCollectionInfo,
                                                    actionCallback)));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(FILL_BUTTON, createFillButtonModel(actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        onView(withText(NICKNAMED_VISA_SUGGESTION.getLabel()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    @DisableFeatures({ChromeFeatureList.AUTOFILL_ENABLE_SECURITY_TOUCH_EVENT_FILTERING_ANDROID})
    public void testAcceptButtonProcessesClicksThroughObscuredSurfaces() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    NICKNAMED_VISA_SUGGESTION,
                                                    mItemCollectionInfo,
                                                    actionCallback)));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(FILL_BUTTON, createFillButtonModel(actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        onView(withText(getString(R.string.autofill_payment_method_continue_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    @EnableFeatures({ChromeFeatureList.AUTOFILL_ENABLE_SECURITY_TOUCH_EVENT_FILTERING_ANDROID})
    public void testCreditCardSuggestionViewFiltersClicks() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    NICKNAMED_VISA_SUGGESTION,
                                                    mItemCollectionInfo,
                                                    /* actionCallback= */ () -> fail())));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            FILL_BUTTON,
                                            createFillButtonModel(
                                                    /* actionCallback= */ () -> fail())));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        // Make sure touch events are ignored if something is drawn on top the the bottom sheet.
        onView(withText(NICKNAMED_VISA_SUGGESTION.getLabel()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        onView(withText(NICKNAMED_VISA_SUGGESTION.getLabel()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED));
        onView(withText(getString(R.string.autofill_payment_method_continue_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        onView(withText(getString(R.string.autofill_payment_method_continue_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED));
    }

    @Test
    @MediumTest
    public void testMainTextShowsNetworkForNicknamedCard() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    NICKNAMED_VISA_SUGGESTION,
                                                    mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        TextView mainText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.main_text);
        assertTrue(mainText.getContentDescription().toString().equals("Best Card visa"));
    }

    @Test
    @MediumTest
    public void testDescriptionLineContentDescriptionOfCreditCardSuggestion() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION,
                                                    new FillableItemCollectionInfo(
                                                            /* position= */ 1, /* total= */ 1))));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        assertContentDescriptionEquals(
                getSuggestionFirstLineLabelAt(0), /* position= */ 1, /* total= */ 1);
    }

    @Test
    @MediumTest
    public void testDescriptionLineContentDescriptionOfVirtualCardSuggestion() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VIRTUAL_CARD_SUGGESTION,
                                                    new FillableItemCollectionInfo(
                                                            /* position= */ 1, /* total= */ 1))));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        assertContentDescriptionEquals(
                getSuggestionFirstLineLabelAt(0), /* position= */ 1, /* total= */ 1);
    }

    @Test
    @MediumTest
    public void testAllCardLabelsOfCreditCardSuggestionAndTermsLabel_CardBenefitsPresent() {
        runOnUiThreadBlocking(
                () -> {
                    PropertyModel cardModel =
                            createCardSuggestionModel(
                                    VISA_SUGGESTION_WITH_CARD_BENEFITS,
                                    new FillableItemCollectionInfo(
                                            /* position= */ 1, /* total= */ 1));
                    PropertyModel termsLabelModel =
                            createTermsLabelModel(/* cardBenefitsTermsAvailable= */ true);
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(CREDIT_CARD, cardModel));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(TERMS_LABEL, termsLabelModel));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        assertThat(
                getSuggestionMainTextAt(0).getText(),
                is(VISA_SUGGESTION_WITH_CARD_BENEFITS.getLabel()));
        assertThat(
                getSuggestionMinorTextAt(0).getText(),
                is(VISA_SUGGESTION_WITH_CARD_BENEFITS.getSecondaryLabel()));
        assertThat(
                getSuggestionFirstLineLabelAt(0).getText(),
                is(VISA_SUGGESTION_WITH_CARD_BENEFITS.getSublabel()));
        TextView secondLineLabel = getSuggestionSecondLineLabelAt(0);
        assertThat(
                secondLineLabel.getText(),
                is(VISA_SUGGESTION_WITH_CARD_BENEFITS.getSecondarySublabel()));
        assertContentDescriptionEquals(secondLineLabel, /* position= */ 1, /* total= */ 1);
        TextView benefitsTermsLabel = getCreditCardBenefitsTermsLabel();
        String expectedBenefitsTermsLabel =
                getString(R.string.autofill_payment_method_bottom_sheet_benefits_terms_label);
        assertThat(benefitsTermsLabel.getText(), is(expectedBenefitsTermsLabel));
    }

    @Test
    @MediumTest
    public void testSecondLineLabelOfCreditCardSuggestionAndTermsLabel_HiddenWithoutCardBenefits() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    VISA_SUGGESTION, mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        assertEquals(View.GONE, getSuggestionSecondLineLabelAt(0).getVisibility());
        // The benefit terms label is omitted when there are no card benefits.
        assertNull(getCreditCardBenefitsTermsLabel());
    }

    @Test
    @MediumTest
    public void testAllCardLabelsOfVirtualCardSuggestionAndTermsLabel_CardBenefitsPresent() {
        runOnUiThreadBlocking(
                () -> {
                    PropertyModel cardModel =
                            createCardSuggestionModel(
                                    VIRTUAL_CARD_SUGGESTION_WITH_CARD_BENEFITS,
                                    new FillableItemCollectionInfo(
                                            /* position= */ 1, /* total= */ 1));
                    PropertyModel termsLabelModel =
                            createTermsLabelModel(/* cardBenefitsTermsAvailable= */ true);
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(CREDIT_CARD, cardModel));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(TERMS_LABEL, termsLabelModel));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        assertThat(
                getSuggestionMainTextAt(0).getText(),
                is(VIRTUAL_CARD_SUGGESTION_WITH_CARD_BENEFITS.getLabel()));
        assertThat(
                getSuggestionMinorTextAt(0).getText(),
                is(VIRTUAL_CARD_SUGGESTION_WITH_CARD_BENEFITS.getSecondaryLabel()));
        assertThat(
                getSuggestionFirstLineLabelAt(0).getText(),
                is(VIRTUAL_CARD_SUGGESTION_WITH_CARD_BENEFITS.getSublabel()));
        TextView secondLineLabel = getSuggestionSecondLineLabelAt(0);
        assertThat(
                secondLineLabel.getText(),
                is(VIRTUAL_CARD_SUGGESTION_WITH_CARD_BENEFITS.getSecondarySublabel()));
        assertContentDescriptionEquals(secondLineLabel, /* position= */ 1, /* total= */ 1);
        TextView benefitsTermsLabel = getCreditCardBenefitsTermsLabel();
        String expectedBenefitsTermsLabel =
                getString(R.string.autofill_payment_method_bottom_sheet_benefits_terms_label);
        assertThat(benefitsTermsLabel.getText(), is(expectedBenefitsTermsLabel));
    }

    @Test
    @MediumTest
    public void testNonAcceptableVirtualCardSuggestion() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    NON_ACCEPTABLE_VIRTUAL_CARD_SUGGESTION,
                                                    new FillableItemCollectionInfo(1, 1),
                                                    CallbackUtils.emptyRunnable())));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        ImageView icon = mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.favicon);
        assertThat(icon.getAlpha(), is(0.38f));
        assertThat(getCreditCardSuggestions().getChildAt(0).isEnabled(), is(false));
        assertThat(
                getSuggestionMainTextAt(0).getText(),
                is(NON_ACCEPTABLE_VIRTUAL_CARD_SUGGESTION.getLabel()));
        assertThat(
                getSuggestionMinorTextAt(0).getText(),
                is(NON_ACCEPTABLE_VIRTUAL_CARD_SUGGESTION.getSecondaryLabel()));
        assertThat(
                getSuggestionFirstLineLabelAt(0).getText(),
                is(NON_ACCEPTABLE_VIRTUAL_CARD_SUGGESTION.getSublabel()));
    }

    @Test
    @MediumTest
    @DisabledTest(message = "crbug.com/333128685")
    public void testMainTextTruncatesLongCardNameWithLastFourDigitsAlwaysShown() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            CREDIT_CARD,
                                            createCardSuggestionModel(
                                                    LONG_CARD_NAME_CARD_SUGGESTION,
                                                    mItemCollectionInfo)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        TextView mainText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.main_text);
        TextView minorText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.minor_text);
        assertTrue(
                mainText.getLayout().getEllipsisCount(mainText.getLayout().getLineCount() - 1) > 0);
        assertThat(
                minorText.getLayout().getText().toString(),
                is(LONG_CARD_NAME_CARD_SUGGESTION.getSecondaryLabel()));
    }

    @Test
    @MediumTest
    @DisableFeatures({ChromeFeatureList.AUTOFILL_ENABLE_SECURITY_TOUCH_EVENT_FILTERING_ANDROID})
    public void testIbanViewProcessesTouchEvents() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(IBAN, createIbanModel(LOCAL_IBAN, actionCallback)));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(FILL_BUTTON, createFillButtonModel(actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        onView(withText(LOCAL_IBAN.getLabel()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    @DisableFeatures({ChromeFeatureList.AUTOFILL_ENABLE_SECURITY_TOUCH_EVENT_FILTERING_ANDROID})
    public void testIbanAcceptButtonProcessesTouchEvents() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(IBAN, createIbanModel(LOCAL_IBAN, actionCallback)));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(FILL_BUTTON, createFillButtonModel(actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        onView(withText(getString(R.string.autofill_payment_method_continue_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    @EnableFeatures({ChromeFeatureList.AUTOFILL_ENABLE_SECURITY_TOUCH_EVENT_FILTERING_ANDROID})
    public void testIbanViewFiltersTouchEvents() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            IBAN,
                                            createIbanModel(
                                                    LOCAL_IBAN,
                                                    /* actionCallback= */ () -> fail())));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            FILL_BUTTON,
                                            createFillButtonModel(
                                                    /* actionCallback= */ () -> fail())));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        // Make sure touch events are ignored if something is drawn on top the the bottom sheet.
        onView(withText(LOCAL_IBAN.getLabel()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        onView(withText(LOCAL_IBAN.getLabel()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED));
        onView(withText(getString(R.string.autofill_payment_method_continue_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        onView(withText(getString(R.string.autofill_payment_method_continue_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED));
    }

    @Test
    @MediumTest
    public void testIbanTouchToFillItem_displayPrimaryAndSecondaryText() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(IBAN, createIbanModel(LOCAL_IBAN)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        TextView ibanPrimaryText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.iban_primary);
        assertThat(ibanPrimaryText.getLayout().getText().toString(), is(LOCAL_IBAN.getNickname()));
        TextView ibanSecondaryText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.iban_secondary);
        assertThat(ibanSecondaryText.getLayout().getText().toString(), is(LOCAL_IBAN.getLabel()));
    }

    @Test
    @MediumTest
    public void testIbanTouchToFillItem_displayPrimaryTextOnly() {
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(new ListItem(IBAN, createIbanModel(LOCAL_IBAN_NO_NICKNAME)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        TextView ibanPrimaryText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.iban_primary);
        assertThat(ibanPrimaryText.getLayout().getText().toString(), is(LOCAL_IBAN.getLabel()));
        TextView ibanSecondaryText =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.iban_secondary);
        assertThat(ibanSecondaryText.getVisibility(), is(View.GONE));
    }

    @Test
    @MediumTest
    public void testLoyaltyCardTouchToFillItem() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            LOYALTY_CARD,
                                            createLoyaltyCardModel(
                                                    CVS_LOYALTY_CARD, actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        TextView loyaltyCardNumber =
                mTouchToFillPaymentMethodView
                        .getContentView()
                        .findViewById(R.id.loyalty_card_number);
        assertThat(
                loyaltyCardNumber.getText().toString(),
                is(CVS_LOYALTY_CARD.getLoyaltyCardNumber()));
        TextView merchantName =
                mTouchToFillPaymentMethodView.getContentView().findViewById(R.id.merchant_name);
        assertThat(merchantName.getText().toString(), is(CVS_LOYALTY_CARD.getMerchantName()));

        onView(withText(CVS_LOYALTY_CARD.getLoyaltyCardNumber()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    public void testAutofillLoyaltyCardIsClickable() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            LOYALTY_CARD,
                                            createLoyaltyCardModel(
                                                    CVS_LOYALTY_CARD, actionCallback)));
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            FILL_BUTTON,
                                            createFillButtonModel(
                                                    R.string.autofill_loyalty_card_autofill_button,
                                                    actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        onView(withText(getString(R.string.autofill_loyalty_card_autofill_button)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    public void testAllLoyaltyCardsItem() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    mTouchToFillPaymentMethodModel
                            .get(SHEET_ITEMS)
                            .add(
                                    new ListItem(
                                            ALL_LOYALTY_CARDS,
                                            createAllLoyaltyCardsItemModel(actionCallback)));
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        TextView allLoyaltyCardsItemTitle =
                mTouchToFillPaymentMethodView
                        .getContentView()
                        .findViewById(R.id.all_loyalty_cards_item_title);
        assertThat(
                allLoyaltyCardsItemTitle.getText().toString(),
                is(getString(R.string.autofill_bottom_sheet_all_your_loyalty_cards)));

        onView(withText(getString(R.string.autofill_bottom_sheet_all_your_loyalty_cards)))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();
    }

    @Test
    @MediumTest
    public void testAllLoyaltyCardsScreen() {
        Runnable actionCallback = mock(Runnable.class);
        runOnUiThreadBlocking(
                () -> {
                    ModelList allLoyaltyCards = new ModelList();
                    allLoyaltyCards.add(
                            new ListItem(
                                    LOYALTY_CARD,
                                    createLoyaltyCardModel(CVS_LOYALTY_CARD, actionCallback)));
                    mTouchToFillPaymentMethodModel.set(CURRENT_SCREEN, ALL_LOYALTY_CARDS_SCREEN);
                    mTouchToFillPaymentMethodModel.set(SHEET_ITEMS, allLoyaltyCards);
                    mTouchToFillPaymentMethodModel.set(VISIBLE, true);
                });
        BottomSheetTestSupport.waitForOpen(mBottomSheetController);

        ViewGroup allLoyaltyCardsScreen =
                mTouchToFillPaymentMethodView
                        .getContentView()
                        .findViewById(R.id.touch_to_fill_payment_method_all_loyalty_cards_screen);
        assertNotNull(allLoyaltyCardsScreen);

        TextView allLoyaltyCardsScreenTitle =
                allLoyaltyCardsScreen.findViewById(R.id.all_loyalty_cards_screen_title);
        assertThat(
                allLoyaltyCardsScreenTitle.getText().toString(),
                is(getString(R.string.autofill_bottom_sheet_all_loyalty_cards_screen_title)));

        // Verify that 1 loyalty card is displayed.
        RecyclerView allLoyaltyCardsContainer =
                allLoyaltyCardsScreen.findViewById(R.id.touch_to_fill_all_loyalty_cards_list);
        assertThat(allLoyaltyCardsContainer.getAdapter().getItemCount(), is(1));

        // Verify that the correct data is displayed for the loyalty card.
        View loyaltyCardItem = allLoyaltyCardsContainer.getChildAt(0);
        TextView loyaltyCardNumber = loyaltyCardItem.findViewById(R.id.loyalty_card_number);
        assertThat(
                loyaltyCardNumber.getText().toString(),
                is(CVS_LOYALTY_CARD.getLoyaltyCardNumber()));
        TextView merchantName = loyaltyCardItem.findViewById(R.id.merchant_name);
        assertThat(merchantName.getText().toString(), is(CVS_LOYALTY_CARD.getMerchantName()));

        // Verify that the loyalty card is clickable.
        onView(withText(CVS_LOYALTY_CARD.getLoyaltyCardNumber()))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(actionCallback).run();

        onView(withId(R.id.all_loyalty_cards_back_image_button))
                .perform(createClickActionWithFlags(MotionEvent.FLAG_WINDOW_IS_OBSCURED));
        waitForEvent(mBackPressHandler).run();
    }

    private RecyclerView getCreditCardSuggestions() {
        return mTouchToFillPaymentMethodView
                .getContentView()
                .findViewById(R.id.touch_to_fill_payment_method_home_screen);
    }

    private TextView getSuggestionMainTextAt(int index) {
        return getCreditCardSuggestions().getChildAt(index).findViewById(R.id.main_text);
    }

    private TextView getSuggestionMinorTextAt(int index) {
        return getCreditCardSuggestions().getChildAt(index).findViewById(R.id.minor_text);
    }

    private TextView getSuggestionFirstLineLabelAt(int index) {
        return getCreditCardSuggestions().getChildAt(index).findViewById(R.id.first_line_label);
    }

    private TextView getSuggestionSecondLineLabelAt(int index) {
        return getCreditCardSuggestions().getChildAt(index).findViewById(R.id.second_line_label);
    }

    private TextView getCreditCardBenefitsTermsLabel() {
        return mTouchToFillPaymentMethodView
                .getContentView()
                .findViewById(R.id.touch_to_fill_terms_label);
    }

    private @SheetState int getBottomSheetState() {
        return mBottomSheetController.getSheetState();
    }

    private String getString(@IdRes int id) {
        return mActivityTestRule.getActivity().getString(id);
    }

    private static PropertyModel createHeaderModel() {
        return new PropertyModel.Builder(HeaderProperties.ALL_KEYS)
                .with(IMAGE_DRAWABLE_ID, R.drawable.ic_globe_24dp)
                .with(TITLE_ID, R.string.autofill_loyalty_card_bottom_sheet_title)
                .build();
    }

    private static PropertyModel createCardSuggestionModel(
            AutofillSuggestion suggestion, FillableItemCollectionInfo collectionInfo) {
        return createCardSuggestionModel(suggestion, collectionInfo, CallbackUtils.emptyRunnable());
    }

    private static PropertyModel createCardSuggestionModel(
            AutofillSuggestion suggestion,
            FillableItemCollectionInfo collectionInfo,
            Runnable actionCallback) {
        PaymentsPayload payload = (PaymentsPayload) suggestion.getPayload();
        PropertyModel.Builder creditCardSuggestionModelBuilder =
                new PropertyModel.Builder(NON_TRANSFORMING_CREDIT_CARD_SUGGESTION_KEYS)
                        .with(MAIN_TEXT, suggestion.getLabel())
                        .with(MAIN_TEXT_CONTENT_DESCRIPTION, payload.getLabelContentDescription())
                        .with(MINOR_TEXT, suggestion.getSecondaryLabel())
                        .with(FIRST_LINE_LABEL, suggestion.getSublabel())
                        .with(SECOND_LINE_LABEL, suggestion.getSecondarySublabel())
                        .with(ITEM_COLLECTION_INFO, collectionInfo)
                        .with(ON_CREDIT_CARD_CLICK_ACTION, actionCallback)
                        .with(APPLY_DEACTIVATED_STYLE, suggestion.applyDeactivatedStyle());
        return creditCardSuggestionModelBuilder.build();
    }

    private static PropertyModel createIbanModel(Iban iban) {
        return createIbanModel(iban, CallbackUtils.emptyRunnable());
    }

    private static PropertyModel createIbanModel(Iban iban, Runnable actionCallback) {
        PropertyModel.Builder ibanModelBuilder =
                new PropertyModel.Builder(NON_TRANSFORMING_IBAN_KEYS)
                        .with(IBAN_VALUE, iban.getLabel())
                        .with(IBAN_NICKNAME, iban.getNickname())
                        .with(ON_IBAN_CLICK_ACTION, actionCallback);
        return ibanModelBuilder.build();
    }

    private static PropertyModel createLoyaltyCardModel(
            LoyaltyCard loyaltyCard, Runnable runnable) {
        return new PropertyModel.Builder(NON_TRANSFORMING_LOYALTY_CARD_KEYS)
                .with(LOYALTY_CARD_NUMBER, loyaltyCard.getLoyaltyCardNumber())
                .with(MERCHANT_NAME, loyaltyCard.getMerchantName())
                .with(ON_LOYALTY_CARD_CLICK_ACTION, runnable)
                .build();
    }

    private static PropertyModel createAllLoyaltyCardsItemModel(Runnable runnable) {
        return new PropertyModel.Builder(AllLoyaltyCardsItemProperties.ALL_KEYS)
                .with(AllLoyaltyCardsItemProperties.ON_CLICK_ACTION, runnable)
                .build();
    }

    private static PropertyModel createFillButtonModel(Runnable actionCallback) {
        return createFillButtonModel(
                R.string.autofill_payment_method_continue_button, actionCallback);
    }

    private static PropertyModel createFillButtonModel(
            @StringRes int textId, Runnable actionCallback) {
        return new PropertyModel.Builder(ButtonProperties.ALL_KEYS)
                .with(TEXT_ID, textId)
                .with(ON_CLICK_ACTION, actionCallback)
                .build();
    }

    private static PropertyModel createTermsLabelModel(boolean cardBenefitsTermsAvailable) {
        return new PropertyModel.Builder(ALL_TERMS_LABEL_KEYS)
                .with(CARD_BENEFITS_TERMS_AVAILABLE, cardBenefitsTermsAvailable)
                .build();
    }

    private static <T> T waitForEvent(T mock) {
        return verify(
                mock,
                timeout(ScalableTimeout.scaleTimeout(CriteriaHelper.DEFAULT_MAX_TIME_TO_POLL)));
    }

    private static void assertContentDescriptionEquals(
            TextView descriptionLine, int position, int total) {
        AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain();
        descriptionLine.onInitializeAccessibilityNodeInfo(info);
        assertEquals(
                descriptionLine
                        .getContext()
                        .getString(
                                R.string.autofill_payment_method_a11y_item_collection_info,
                                descriptionLine.getText(),
                                position,
                                total),
                info.getContentDescription());
    }
}
