// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill.vcn;

import static org.chromium.build.NullUtil.assertNonNull;
import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;

import androidx.annotation.DrawableRes;
import androidx.annotation.VisibleForTesting;
import androidx.browser.customtabs.CustomTabsIntent;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.ChromeStringConstants;
import org.chromium.chrome.browser.autofill.AutofillUiUtils.IconSpecs;
import org.chromium.chrome.browser.autofill.vcn.AutofillVcnEnrollBottomSheetProperties.Description;
import org.chromium.chrome.browser.autofill.vcn.AutofillVcnEnrollBottomSheetProperties.IssuerIcon;
import org.chromium.chrome.browser.autofill.vcn.AutofillVcnEnrollBottomSheetProperties.LegalMessages;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.layouts.LayoutManagerProvider;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorSupplier;
import org.chromium.components.autofill.AutofillFeatures;
import org.chromium.components.autofill.ImageSize;
import org.chromium.components.autofill.ImageType;
import org.chromium.components.autofill.VirtualCardEnrollmentLinkType;
import org.chromium.components.autofill.payments.LegalMessageLine;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.url.GURL;

import java.util.List;

/** Bridge for the virtual card enrollment bottom sheet. */
@JNINamespace("autofill")
@NullMarked
/*package*/ class AutofillVcnEnrollBottomSheetBridge
        implements AutofillVcnEnrollBottomSheetCoordinator.Delegate,
                AutofillVcnEnrollBottomSheetProperties.LinkOpener {
    private long mNativeAutofillVcnEnrollBottomSheetBridge;
    private @Nullable Context mContext;
    private @Nullable AutofillVcnEnrollBottomSheetCoordinator mCoordinator;

    private @Nullable LayoutStateProvider mLayoutStateProviderForTesting;
    private @Nullable ObservableSupplier<TabModelSelector> mTabModelSelectorSupplierForTesting;

    @CalledByNative
    @VisibleForTesting
    AutofillVcnEnrollBottomSheetBridge() {}

    /**
     * Requests to show the bottom sheet. Called via JNI from C++.
     *
     * @param nativeAutofillVcnEnrollBottomSheetBridge The native pointer to the C++ bridge that
     *     receives callbacks.
     * @param webContents The web contents where the bottom sheet should show.
     * @param messageText The prompt message for the bottom sheet, e.g., "Make it more secure with a
     *     virtual card next time?"
     * @param descriptionText A text that describes what a virtual card does, e.g., "A virtual card
     *     hides your actual card..." and so on. This text includes a "learn more" link text.
     * @param learnMoreLinkText The text of the "learn more" link in descriptionText.
     * @param issuerIconBitmap The icon for the card. For example, could be an American Express
     *     logo. Not used when
     *     AutofillFeatures.AUTOFILL_ENABLE_VIRTUAL_CARD_JAVA_PAYMENTS_DATA_MANAGER is enabled.
     * @param networkIconResource The resource-id icon for the card. For example, could be an
     *     American Express logo.
     * @param issuerIconUrl The url for the card icon. For example, could be a custom card art for a
     *     credit card. This takes precedence over the issuerIconResource.
     * @param cardLabel The label for the card, e.g., "Amex ****1234".
     * @param googleLegalMessages Legal messages from Google Pay.
     * @param issuerLegalMessages Legal messages from the issuer bank.
     * @param acceptButtonLabel The label for the button that enrolls a virtual card.
     * @param cancelButtonLabel The label for the button that cancels enrollment.
     * @return True if shown.
     */
    @CalledByNative
    @VisibleForTesting
    boolean requestShowContent(
            long nativeAutofillVcnEnrollBottomSheetBridge,
            WebContents webContents,
            @JniType("std::u16string") String messageText,
            @JniType("std::u16string") String descriptionText,
            @JniType("std::u16string") String learnMoreLinkText,
            Bitmap issuerIconBitmap,
            @DrawableRes int networkIconResource,
            @JniType("GURL") GURL issuerIconUrl,
            @JniType("std::u16string") String cardLabel,
            @JniType("std::vector") List<LegalMessageLine> googleLegalMessages,
            @JniType("std::vector") List<LegalMessageLine> issuerLegalMessages,
            @JniType("std::u16string") String acceptButtonLabel,
            @JniType("std::u16string") String cancelButtonLabel) {
        if (webContents == null || webContents.isDestroyed()) return false;

        WindowAndroid window = webContents.getTopLevelNativeWindow();
        if (window == null) return false;

        mContext = window.getContext().get();
        if (mContext == null) return false;

        if (mNativeAutofillVcnEnrollBottomSheetBridge != 0) return false;
        mNativeAutofillVcnEnrollBottomSheetBridge = nativeAutofillVcnEnrollBottomSheetBridge;

        IconSpecs iconSpecs =
                IconSpecs.create(mContext, ImageType.CREDIT_CARD_ART_IMAGE, ImageSize.LARGE);

        PropertyModel.Builder modelBuilder =
                new PropertyModel.Builder(AutofillVcnEnrollBottomSheetProperties.ALL_KEYS)
                        .with(AutofillVcnEnrollBottomSheetProperties.MESSAGE_TEXT, messageText)
                        .with(
                                AutofillVcnEnrollBottomSheetProperties.DESCRIPTION,
                                new Description(
                                        descriptionText,
                                        learnMoreLinkText,
                                        ChromeStringConstants
                                                .AUTOFILL_VIRTUAL_CARD_ENROLLMENT_SUPPORT_URL,
                                        VirtualCardEnrollmentLinkType
                                                .VIRTUAL_CARD_ENROLLMENT_LEARN_MORE_LINK,
                                        /* linkOpener= */ this))
                        .with(
                                AutofillVcnEnrollBottomSheetProperties.ISSUER_ICON,
                                ChromeFeatureList.isEnabled(
                                                AutofillFeatures
                                                        .AUTOFILL_ENABLE_VIRTUAL_CARD_JAVA_PAYMENTS_DATA_MANAGER)
                                        ? new IssuerIcon(networkIconResource, issuerIconUrl)
                                        : new IssuerIcon(
                                                issuerIconBitmap,
                                                iconSpecs.getWidth(),
                                                iconSpecs.getHeight()))
                        .with(AutofillVcnEnrollBottomSheetProperties.CARD_LABEL, cardLabel)
                        .with(
                                AutofillVcnEnrollBottomSheetProperties.GOOGLE_LEGAL_MESSAGES,
                                new LegalMessages(
                                        googleLegalMessages,
                                        VirtualCardEnrollmentLinkType
                                                .VIRTUAL_CARD_ENROLLMENT_GOOGLE_PAYMENTS_TOS_LINK,
                                        /* linkOpener= */ this))
                        .with(
                                AutofillVcnEnrollBottomSheetProperties.ISSUER_LEGAL_MESSAGES,
                                new LegalMessages(
                                        issuerLegalMessages,
                                        VirtualCardEnrollmentLinkType
                                                .VIRTUAL_CARD_ENROLLMENT_ISSUER_TOS_LINK,
                                        /* linkOpener= */ this))
                        .with(
                                AutofillVcnEnrollBottomSheetProperties.ACCEPT_BUTTON_LABEL,
                                acceptButtonLabel)
                        .with(
                                AutofillVcnEnrollBottomSheetProperties.CANCEL_BUTTON_LABEL,
                                cancelButtonLabel)
                        .with(AutofillVcnEnrollBottomSheetProperties.SHOW_LOADING_STATE, false);

        LayoutStateProvider layoutStateProvider =
                mLayoutStateProviderForTesting != null
                        ? mLayoutStateProviderForTesting
                        : LayoutManagerProvider.from(window);
        ObservableSupplier<TabModelSelector> selectorSupplier =
                mTabModelSelectorSupplierForTesting != null
                        ? mTabModelSelectorSupplierForTesting
                        : TabModelSelectorSupplier.from(window);
        Profile profile = Profile.fromWebContents(webContents);
        assertNonNull(profile);
        mCoordinator =
                new AutofillVcnEnrollBottomSheetCoordinator(
                        mContext,
                        profile,
                        modelBuilder,
                        assumeNonNull(layoutStateProvider),
                        assumeNonNull(selectorSupplier),
                        /* delegate= */ this);

        return mCoordinator.requestShowContent(window);
    }

    void setLayoutStateProviderForTesting(LayoutStateProvider layoutStateProvider) {
        mLayoutStateProviderForTesting = layoutStateProvider;
    }

    void setTabModelSelectorSupplierForTesting(
            ObservableSupplier<TabModelSelector> tabModelSelectorSupplier) {
        mTabModelSelectorSupplierForTesting = tabModelSelectorSupplier;
    }

    // AutofillVcnEnrollBottomSheetProperties.LinkOpener:
    @Override
    public void openLink(String url, @VirtualCardEnrollmentLinkType int linkType) {
        assumeNonNull(mContext);
        new CustomTabsIntent.Builder()
                .setShowTitle(true)
                .build()
                .launchUrl(mContext, Uri.parse(url));

        if (mNativeAutofillVcnEnrollBottomSheetBridge == 0) return;
        AutofillVcnEnrollBottomSheetBridgeJni.get()
                .recordLinkClickMetric(mNativeAutofillVcnEnrollBottomSheetBridge, linkType);
    }

    // AutofillVcnEnrollBottomSheetCoordinator.Delegate:
    @Override
    public void onAccept() {
        if (mNativeAutofillVcnEnrollBottomSheetBridge == 0) return;
        AutofillVcnEnrollBottomSheetBridgeJni.get()
                .onAccept(mNativeAutofillVcnEnrollBottomSheetBridge);
        mNativeAutofillVcnEnrollBottomSheetBridge = 0;
    }

    // AutofillVcnEnrollBottomSheetCoordinator.Delegate:
    @Override
    public void onCancel() {
        if (mNativeAutofillVcnEnrollBottomSheetBridge == 0) return;
        AutofillVcnEnrollBottomSheetBridgeJni.get()
                .onCancel(mNativeAutofillVcnEnrollBottomSheetBridge);
        mNativeAutofillVcnEnrollBottomSheetBridge = 0;
    }

    // AutofillVcnEnrollBottomSheetCoordinator.Delegate:
    @Override
    public void onDismiss() {
        if (mNativeAutofillVcnEnrollBottomSheetBridge == 0) return;
        AutofillVcnEnrollBottomSheetBridgeJni.get()
                .onDismiss(mNativeAutofillVcnEnrollBottomSheetBridge);
        mNativeAutofillVcnEnrollBottomSheetBridge = 0;
    }

    @CalledByNative
    @VisibleForTesting
    void hide() {
        mNativeAutofillVcnEnrollBottomSheetBridge = 0;
        if (mCoordinator == null) return;
        mCoordinator.hide();
        mCoordinator = null;
    }

    @Nullable AutofillVcnEnrollBottomSheetCoordinator getCoordinatorForTesting() {
        return mCoordinator;
    }

    @NativeMethods
    interface Natives {
        void onAccept(long nativeAutofillVCNEnrollBottomSheetBridge);

        void onCancel(long nativeAutofillVCNEnrollBottomSheetBridge);

        void onDismiss(long nativeAutofillVCNEnrollBottomSheetBridge);

        void recordLinkClickMetric(long nativeAutofillVCNEnrollBottomSheetBridge, int linkType);
    }
}
