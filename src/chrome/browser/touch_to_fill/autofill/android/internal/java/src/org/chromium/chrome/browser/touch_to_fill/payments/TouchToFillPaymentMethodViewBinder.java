// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill.payments;

import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.BACK_PRESS_HANDLER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ButtonProperties.ON_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.ButtonProperties.TEXT_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CURRENT_SCREEN;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.APPLY_DEACTIVATED_STYLE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.CARD_IMAGE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.FIRST_LINE_LABEL;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.ITEM_COLLECTION_INFO;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.MAIN_TEXT;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.MAIN_TEXT_CONTENT_DESCRIPTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.MINOR_TEXT;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.ON_CREDIT_CARD_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.CreditCardSuggestionProperties.SECOND_LINE_LABEL;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.DISMISS_HANDLER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.FooterProperties.OPEN_MANAGEMENT_UI_CALLBACK;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.FooterProperties.OPEN_MANAGEMENT_UI_TITLE_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.FooterProperties.SCAN_CREDIT_CARD_CALLBACK;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.FooterProperties.SHOULD_SHOW_SCAN_CREDIT_CARD;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.HeaderProperties.IMAGE_DRAWABLE_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.HeaderProperties.SUBTITLE_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.HeaderProperties.TITLE_ID;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.IBAN_NICKNAME;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.IBAN_VALUE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.IbanProperties.ON_IBAN_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.LOYALTY_CARD_ICON;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.LOYALTY_CARD_NUMBER;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.MERCHANT_NAME;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.LoyaltyCardProperties.ON_LOYALTY_CARD_CLICK_ACTION;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.SHEET_ITEMS;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.TermsLabelProperties.CARD_BENEFITS_TERMS_AVAILABLE;
import static org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.VISIBLE;

import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.chrome.browser.autofill.AutofillUiUtils;
import org.chromium.chrome.browser.touch_to_fill.common.FillableItemCollectionInfo;
import org.chromium.chrome.browser.touch_to_fill.payments.TouchToFillPaymentMethodProperties.AllLoyaltyCardsItemProperties;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * Provides functions that map {@link TouchToFillPaymentMethodProperties} changes in a {@link
 * PropertyModel} to the suitable method in {@link TouchToFillPaymentMethodView}.
 */
class TouchToFillPaymentMethodViewBinder {
    private static final float GRAYED_OUT_OPACITY_ALPHA = 0.38f;
    private static final float COMPLETE_OPACITY_ALPHA = 1.0f;

    /**
     * The collection info is added by setting an instance of this delegate on the last text view
     * (it is important to sound naturally and mimic the default message), so that the message which
     * is built from item children gets a suffix like ", 1 of 3.". This delegate also assumes that
     * its host sets text on {@link AccessibilityNodeInfo}, which is true for {@link TextView}.
     */
    private static class TextViewCollectionInfoAccessibilityDelegate
            extends View.AccessibilityDelegate {
        private final FillableItemCollectionInfo mCollectionInfo;

        public TextViewCollectionInfoAccessibilityDelegate(
                @NonNull FillableItemCollectionInfo collectionInfo) {
            mCollectionInfo = collectionInfo;
        }

        @Override
        public void onInitializeAccessibilityNodeInfo(
                @NonNull View host, @NonNull AccessibilityNodeInfo info) {
            super.onInitializeAccessibilityNodeInfo(host, info);

            assert info.getText() != null;
            info.setContentDescription(
                    host.getContext()
                            .getString(
                                    R.string.autofill_payment_method_a11y_item_collection_info,
                                    info.getText(),
                                    mCollectionInfo.getPosition(),
                                    mCollectionInfo.getTotal()));
        }
    }

    /**
     * Called whenever a property in the given model changes. It updates the given view accordingly.
     *
     * @param model The observed {@link PropertyModel}. Its data need to be reflected in the view.
     * @param view The {@link TouchToFillPaymentMethodView} to update.
     * @param propertyKey The {@link PropertyKey} which changed.
     */
    static void bindTouchToFillPaymentMethodView(
            PropertyModel model, TouchToFillPaymentMethodView view, PropertyKey propertyKey) {
        if (propertyKey == DISMISS_HANDLER) {
            view.setDismissHandler(model.get(DISMISS_HANDLER));
        } else if (propertyKey == BACK_PRESS_HANDLER) {
            view.setBackPressHandler(model.get(BACK_PRESS_HANDLER));
        } else if (propertyKey == VISIBLE) {
            boolean visibilityChangeSuccessful = view.setVisible(model.get(VISIBLE));
            if (!visibilityChangeSuccessful && model.get(VISIBLE)) {
                assert (model.get(DISMISS_HANDLER) != null);
                model.get(DISMISS_HANDLER).onResult(BottomSheetController.StateChangeReason.NONE);
                view.destroy();
            }
        } else if (propertyKey == SHEET_ITEMS) {
            // SHEET_ITEMS and CURRENT_SCREEN properties are always updated together.
            view.setCurrentScreen(model.get(CURRENT_SCREEN));
            TouchToFillPaymentMethodCoordinator.setUpCardItems(model, view);
        } else if (propertyKey == CURRENT_SCREEN) {
            // Intentionally ignored.
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    private TouchToFillPaymentMethodViewBinder() {}

    /**
     * Factory used to create a card item inside the ListView inside the
     * TouchToFillPaymentMethodView.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static View createCardItemView(ViewGroup parent) {
        View cardItem =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.touch_to_fill_credit_card_sheet_item, parent, false);
        AutofillUiUtils.setFilterTouchForSecurity(cardItem);
        return cardItem;
    }

    /**
     * Factory used to create an IBAN item inside the ListView inside the
     * TouchToFillPaymentMethodView.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static View createIbanItemView(ViewGroup parent) {
        View ibanItem =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.touch_to_fill_iban_sheet_item, parent, false);
        AutofillUiUtils.setFilterTouchForSecurity(ibanItem);
        return ibanItem;
    }

    static View createLoyaltyCardItemView(ViewGroup parent) {
        View loyaltyCardItem =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.touch_to_fill_loyalty_card_sheet_item, parent, false);
        AutofillUiUtils.setFilterTouchForSecurity(loyaltyCardItem);
        return loyaltyCardItem;
    }

    /** Binds the item view to the model properties. */
    static void bindCardItemView(PropertyModel model, View view, PropertyKey propertyKey) {
        TextView mainText = view.findViewById(R.id.main_text);
        TextView minorText = view.findViewById(R.id.minor_text);
        ImageView icon = view.findViewById(R.id.favicon);
        TextView firstLineLabel = view.findViewById(R.id.first_line_label);
        TextView secondLineLabel = view.findViewById(R.id.second_line_label);
        // If card benefits are displayed on the first line, the second line will show
        // primary label with the expiration date or the virtual card status.
        TextView primaryLabel = firstLineLabel;
        if (!TextUtils.isEmpty(model.get(SECOND_LINE_LABEL))) {
            secondLineLabel.setVisibility(View.VISIBLE);
            primaryLabel = secondLineLabel;
        } else if (secondLineLabel != null) {
            secondLineLabel.setVisibility(View.GONE);
        }
        if (propertyKey == CARD_IMAGE) {
            icon.setImageDrawable(model.get(CARD_IMAGE));
        } else if (propertyKey == MAIN_TEXT) {
            mainText.setText(model.get(MAIN_TEXT));
        } else if (propertyKey == MAIN_TEXT_CONTENT_DESCRIPTION) {
            mainText.setContentDescription(model.get(MAIN_TEXT_CONTENT_DESCRIPTION));
        } else if (propertyKey == MINOR_TEXT) {
            minorText.setText(model.get(MINOR_TEXT));
        } else if (propertyKey == FIRST_LINE_LABEL) {
            firstLineLabel.setText(model.get(FIRST_LINE_LABEL));
        } else if (propertyKey == SECOND_LINE_LABEL) {
            secondLineLabel.setText(model.get(SECOND_LINE_LABEL));
        } else if (propertyKey == ON_CREDIT_CARD_CLICK_ACTION) {
            view.setOnClickListener(unusedView -> model.get(ON_CREDIT_CARD_CLICK_ACTION).run());
        } else if (propertyKey == ITEM_COLLECTION_INFO) {
            FillableItemCollectionInfo collectionInfo = model.get(ITEM_COLLECTION_INFO);
            if (collectionInfo != null) {
                primaryLabel.setAccessibilityDelegate(
                        new TextViewCollectionInfoAccessibilityDelegate(collectionInfo));
            }
        } else if (propertyKey == APPLY_DEACTIVATED_STYLE) {
            if (model.get(APPLY_DEACTIVATED_STYLE)) {
                view.setEnabled(false);
                // When merchants have opted out of virtual cards, we convey it
                // via a message in primary label. Since this message is
                // important, we remove the max lines limit to avoid truncation.
                primaryLabel.setMaxLines(Integer.MAX_VALUE);
                mainText.setTextAppearance(R.style.TextAppearance_TextMedium_Disabled);
                minorText.setTextAppearance(R.style.TextAppearance_TextMedium_Disabled);
                icon.setAlpha(GRAYED_OUT_OPACITY_ALPHA);
            } else {
                view.setEnabled(true);
                primaryLabel.setMaxLines(1);
                mainText.setTextAppearance(R.style.TextAppearance_TextMedium_Primary);
                minorText.setTextAppearance(R.style.TextAppearance_TextMedium_Primary);
                icon.setAlpha(COMPLETE_OPACITY_ALPHA);
            }

        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    static void bindIbanItemView(PropertyModel model, View view, PropertyKey propertyKey) {
        if (propertyKey == IBAN_VALUE) {
            if (model.get(IBAN_NICKNAME).isEmpty()) {
                TextView ibanPrimaryText = view.findViewById(R.id.iban_primary);
                ibanPrimaryText.setText(model.get(IBAN_VALUE));
                ibanPrimaryText.setTextAppearance(R.style.TextAppearance_TextLarge_Primary);
            } else {
                TextView ibanSecondaryText = view.findViewById(R.id.iban_secondary);
                ibanSecondaryText.setText(model.get(IBAN_VALUE));
                ibanSecondaryText.setVisibility(View.VISIBLE);
            }
        } else if (propertyKey == IBAN_NICKNAME) {
            if (!model.get(IBAN_NICKNAME).isEmpty()) {
                TextView ibanPrimaryText = view.findViewById(R.id.iban_primary);
                ibanPrimaryText.setText(model.get(IBAN_NICKNAME));
                ibanPrimaryText.setVisibility(View.VISIBLE);
            }
        } else if (propertyKey == ON_IBAN_CLICK_ACTION) {
            view.setOnClickListener(unusedView -> model.get(ON_IBAN_CLICK_ACTION).run());
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    static void bindLoyaltyCardItemView(PropertyModel model, View view, PropertyKey propertyKey) {
        if (propertyKey == LOYALTY_CARD_NUMBER) {
            TextView loyaltyCardNumber = view.findViewById(R.id.loyalty_card_number);
            loyaltyCardNumber.setText(model.get(LOYALTY_CARD_NUMBER));
            loyaltyCardNumber.setTextAppearance(R.style.TextAppearance_TextLarge_Primary);
        } else if (propertyKey == MERCHANT_NAME) {
            TextView merchantName = view.findViewById(R.id.merchant_name);
            merchantName.setText(model.get(MERCHANT_NAME));
            merchantName.setVisibility(View.VISIBLE);
        } else if (propertyKey == LOYALTY_CARD_ICON) {
            ImageView loyaltyCardIcon = view.findViewById(R.id.loyalty_card_icon);
            loyaltyCardIcon.setImageDrawable(model.get(LOYALTY_CARD_ICON));
        } else if (propertyKey == ON_LOYALTY_CARD_CLICK_ACTION) {
            view.setOnClickListener(unusedView -> model.get(ON_LOYALTY_CARD_CLICK_ACTION).run());
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    static View createAllLoyaltyCardsItemView(ViewGroup parent) {
        View view =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.touch_to_fill_all_loyalty_cards_item, parent, false);
        AutofillUiUtils.setFilterTouchForSecurity(view);
        return view;
    }

    static void bindAllLoyaltyCardsItemView(
            PropertyModel model, View view, PropertyKey propertyKey) {
        if (propertyKey == AllLoyaltyCardsItemProperties.ON_CLICK_ACTION) {
            view.setOnClickListener(
                    unusedView -> model.get(AllLoyaltyCardsItemProperties.ON_CLICK_ACTION).run());
        } else {
            assert false : "Unhandled update to property: " + propertyKey;
        }
    }

    /**
     * Factory used to create a new header inside the ListView inside the {@link
     * TouchToFillPaymentMethodView}.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static View createHeaderItemView(ViewGroup parent) {
        return LayoutInflater.from(parent.getContext())
                .inflate(R.layout.touch_to_fill_payment_method_header_item, parent, false);
    }

    /**
     * Called whenever a property in the given model changes. It updates the given view accordingly.
     * @param model The observed {@link PropertyModel}. Its data need to be reflected in the view.
     * @param view The {@link View} of the header to update.
     * @param key The {@link PropertyKey} which changed.
     */
    static void bindHeaderView(PropertyModel model, View view, PropertyKey propertyKey) {
        if (propertyKey == IMAGE_DRAWABLE_ID) {
            ImageView sheetHeaderImage = view.findViewById(R.id.branding_icon);
            sheetHeaderImage.setImageDrawable(
                    AppCompatResources.getDrawable(
                            view.getContext(), model.get(IMAGE_DRAWABLE_ID)));
        } else if (propertyKey == TITLE_ID) {
            TextView sheetHeaderTitle = view.findViewById(R.id.touch_to_fill_sheet_title);
            sheetHeaderTitle.setText(view.getContext().getString(model.get(TITLE_ID)));
        } else if (propertyKey == SUBTITLE_ID) {
            TextView sheetHeaderTitle = view.findViewById(R.id.touch_to_fill_sheet_subtitle);
            sheetHeaderTitle.setVisibility(View.VISIBLE);
            sheetHeaderTitle.setText(view.getContext().getString(model.get(SUBTITLE_ID)));
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    /**
     * Factory used to create a new "Continue" or "Autofill" button that fills in data into the
     * focused field.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static Button createFillButtonView(ViewGroup parent) {
        Button buttonView =
                (Button)
                        LayoutInflater.from(parent.getContext())
                                .inflate(R.layout.touch_to_fill_fill_button, parent, false);
        AutofillUiUtils.setFilterTouchForSecurity(buttonView);
        return buttonView;
    }

    /**
     * Factory used to create a new "Wallet settings" button that redirects the user to the
     * corresponding Chrome settings page.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static Button createWalletSettingsButtonView(ViewGroup parent) {
        Button buttonView =
                (Button)
                        LayoutInflater.from(parent.getContext())
                                .inflate(
                                        R.layout.touch_to_fill_wallet_settings_button,
                                        parent,
                                        false);
        AutofillUiUtils.setFilterTouchForSecurity(buttonView);
        return buttonView;
    }

    /**
     * Called whenever a property in the given model changes. It updates the given view accordingly.
     *
     * @param model The observed {@link PropertyModel}. Its data need to be reflected in the view.
     * @param button The {@link Button} from the bottom sheet to update.
     * @param key The {@link PropertyKey} which changed.
     */
    static void bindButtonView(PropertyModel model, Button button, PropertyKey propertyKey) {
        if (propertyKey == TEXT_ID) {
            button.setText(model.get(TEXT_ID));
        } else if (propertyKey == ON_CLICK_ACTION) {
            button.setOnClickListener(unusedView -> model.get(ON_CLICK_ACTION).run());
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    /**
     * Factory used to create a new label inside the TouchToFillPaymentMethodView. This label shows
     * the `Terms apply for card benefits` message when at least one of the cards has benefits.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static View createTermsLabelView(ViewGroup parent) {
        return LayoutInflater.from(parent.getContext())
                .inflate(R.layout.touch_to_fill_terms_label_sheet_item, parent, false);
    }

    /**
     * Called whenever a property in the given model changes. It updates the given view accordingly.
     *
     * @param model The observed {@link PropertyModel}. Its data need to be reflected in the view.
     * @param view The {@link View} of the header to update.
     * @param propertyKey The {@link PropertyKey} which changed.
     */
    static void bindTermsLabelView(PropertyModel model, View view, PropertyKey propertyKey) {
        if (propertyKey == CARD_BENEFITS_TERMS_AVAILABLE) {
            if (model.get(CARD_BENEFITS_TERMS_AVAILABLE)) {
                TextView termsLabelTextView = view.findViewById(R.id.touch_to_fill_terms_label);
                termsLabelTextView.setText(
                        R.string.autofill_payment_method_bottom_sheet_benefits_terms_label);
            }
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    /**
     * Factory used to create a new footer inside the ListView inside the
     * TouchToFillPaymentMethodView.
     *
     * @param parent The parent {@link ViewGroup} of the new item.
     */
    static View createFooterItemView(ViewGroup parent) {
        return LayoutInflater.from(parent.getContext())
                .inflate(R.layout.touch_to_fill_payment_method_footer_item, parent, false);
    }

    /**
     * Called whenever a property in the given model changes. It updates the given view accordingly.
     * @param model The observed {@link PropertyModel}. Its data need to be reflected in the view.
     * @param view The {@link View} of the header to update.
     * @param key The {@link PropertyKey} which changed.
     */
    static void bindFooterView(PropertyModel model, View view, PropertyKey propertyKey) {
        if (propertyKey == SHOULD_SHOW_SCAN_CREDIT_CARD) {
            setScanCreditCardButton(view, model.get(SHOULD_SHOW_SCAN_CREDIT_CARD));
        } else if (propertyKey == SCAN_CREDIT_CARD_CALLBACK) {
            setScanCreditCardCallback(view, model.get(SCAN_CREDIT_CARD_CALLBACK));
        } else if (propertyKey == OPEN_MANAGEMENT_UI_TITLE_ID) {
            setShowPaymentMethodsSettingsTitle(
                    view, view.getContext().getString(model.get(OPEN_MANAGEMENT_UI_TITLE_ID)));
        } else if (propertyKey == OPEN_MANAGEMENT_UI_CALLBACK) {
            setShowPaymentMethodsSettingsCallback(view, model.get(OPEN_MANAGEMENT_UI_CALLBACK));
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    private static void setScanCreditCardButton(View view, boolean shouldShowScanCreditCard) {
        View scanCreditCard = view.findViewById(R.id.scan_new_card);
        if (shouldShowScanCreditCard) {
            scanCreditCard.setVisibility(View.VISIBLE);
        } else {
            scanCreditCard.setVisibility(View.GONE);
            scanCreditCard.setOnClickListener(null);
        }
    }

    private static void setScanCreditCardCallback(View view, Runnable callback) {
        View scanCreditCard = view.findViewById(R.id.scan_new_card);
        scanCreditCard.setOnClickListener(unused -> callback.run());
    }

    private static void setShowPaymentMethodsSettingsTitle(View view, String title) {
        TextView managePaymentMethodsButton = view.findViewById(R.id.open_management_ui);
        managePaymentMethodsButton.setText(title);
    }

    private static void setShowPaymentMethodsSettingsCallback(View view, Runnable callback) {
        View managePaymentMethodsButton = view.findViewById(R.id.open_management_ui);
        managePaymentMethodsButton.setOnClickListener(unused -> callback.run());
    }
}
