// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.payments;

import static java.util.Collections.unmodifiableList;

import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.os.Handler;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.payments.mojom.PaymentDetailsModifier;
import org.chromium.payments.mojom.PaymentItem;
import org.chromium.payments.mojom.PaymentMethodData;
import org.chromium.payments.mojom.PaymentOptions;
import org.chromium.payments.mojom.PaymentRequestDetailsUpdate;
import org.chromium.payments.mojom.PaymentResponse;
import org.chromium.payments.mojom.PaymentShippingOption;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Wrapper around a C++ payment app. */
@JNINamespace("payments")
@NullMarked
public class JniPaymentApp extends PaymentApp {
    private final Handler mHandler = new Handler();
    private final @PaymentAppType int mPaymentAppType;

    // The Java object owns the C++ payment app and frees it in dismissInstrument().
    private long mNativeObject;

    private @Nullable AbortCallback mAbortCallback;
    private @Nullable InstrumentDetailsCallback mInvokeCallback;
    private @Nullable final Bitmap mIssuerIcon;
    private @Nullable final Bitmap mNetworkIcon;
    private final List<PaymentEntityLogoImpl> mPaymentEntitiesLogos;

    @CalledByNative
    private JniPaymentApp(
            String id,
            String label,
            String sublabel,
            @JniType("const SkBitmap*") @Nullable final Bitmap icon,
            @PaymentAppType int paymentAppType,
            long nativeObject,
            // TODO(https://crbug.com/416516287): Remove issuer and network bitmap once all callers
            // use getPaymentEntityLogos() instead.
            @JniType("const SkBitmap*") @Nullable final Bitmap issuerIcon,
            @JniType("const SkBitmap*") @Nullable final Bitmap networkIcon,
            @JniType("std::vector<PaymentApp::PaymentEntityLogo*>")
                    List<PaymentEntityLogoImpl> paymentEntitiesLogos) {
        super(id, label, sublabel, new BitmapDrawable(icon));
        mPaymentAppType = paymentAppType;
        mNativeObject = nativeObject;
        mIssuerIcon = issuerIcon;
        mNetworkIcon = networkIcon;
        mPaymentEntitiesLogos = paymentEntitiesLogos;
    }

    public static class PaymentEntityLogoImpl implements PaymentApp.PaymentEntityLogo {
        private final String mLabel;
        private final Bitmap mIcon;

        @CalledByNative("PaymentEntityLogoImpl")
        PaymentEntityLogoImpl(
                @JniType("std::u16string") String label, @JniType("const SkBitmap*") Bitmap icon) {
            mLabel = label;
            mIcon = icon;
        }

        @Override
        public String getLabel() {
            return mLabel;
        }

        @Override
        public Bitmap getIcon() {
            return mIcon;
        }
    }

    @CalledByNative
    public void onAbortResult(boolean aborted) {
        mHandler.post(
                () -> {
                    if (mAbortCallback == null) return;
                    mAbortCallback.onInstrumentAbortResult(aborted);
                    mAbortCallback = null;
                });
    }

    @CalledByNative
    public void onInvokeResult(String methodName, String stringifiedDetails, PayerData payerData) {
        mHandler.post(
                () -> {
                    if (mInvokeCallback == null) return;
                    mInvokeCallback.onInstrumentDetailsReady(
                            methodName, stringifiedDetails, payerData);
                    mInvokeCallback = null;
                });
    }

    @CalledByNative
    public void onInvokeError(String errorMessage) {
        mHandler.post(
                () -> {
                    if (mInvokeCallback == null) return;
                    mInvokeCallback.onInstrumentDetailsError(errorMessage);
                    mInvokeCallback = null;
                });
    }

    @CalledByNative
    private static PayerData createPayerData(
            String payerName,
            String payerPhone,
            String payerEmail,
            Address shippingAddress,
            String selectedShippingOptionId) {
        return new PayerData(
                payerName, payerPhone, payerEmail, shippingAddress, selectedShippingOptionId);
    }

    @CalledByNative
    private static Address createShippingAddress(
            String country,
            String[] addressLine,
            String region,
            String city,
            String dependentLocality,
            String postalCode,
            String sortingCode,
            String organization,
            String recipient,
            String phone) {
        return new Address(
                country,
                addressLine,
                region,
                city,
                dependentLocality,
                postalCode,
                sortingCode,
                organization,
                recipient,
                phone);
    }

    @Override
    public Set<String> getInstrumentMethodNames() {
        return new HashSet<>(
                Arrays.asList(JniPaymentAppJni.get().getInstrumentMethodNames(mNativeObject)));
    }

    @Override
    public boolean isValidForPaymentMethodData(String method, @Nullable PaymentMethodData data) {
        return JniPaymentAppJni.get()
                .isValidForPaymentMethodData(
                        mNativeObject, method, data != null ? data.serialize() : null);
    }

    @Override
    public boolean handlesShippingAddress() {
        return JniPaymentAppJni.get().handlesShippingAddress(mNativeObject);
    }

    @Override
    public boolean handlesPayerName() {
        return JniPaymentAppJni.get().handlesPayerName(mNativeObject);
    }

    @Override
    public boolean handlesPayerEmail() {
        return JniPaymentAppJni.get().handlesPayerEmail(mNativeObject);
    }

    @Override
    public boolean handlesPayerPhone() {
        return JniPaymentAppJni.get().handlesPayerPhone(mNativeObject);
    }

    @Override
    public boolean hasEnrolledInstrument() {
        return JniPaymentAppJni.get().hasEnrolledInstrument(mNativeObject);
    }

    @Override
    public boolean canPreselect() {
        return JniPaymentAppJni.get().canPreselect(mNativeObject);
    }

    @Override
    public void invokePaymentApp(
            String id,
            String merchantName,
            String origin,
            String iframeOrigin,
            byte @Nullable [][] certificateChain,
            Map<String, PaymentMethodData> methodDataMap,
            PaymentItem total,
            List<PaymentItem> displayItems,
            Map<String, PaymentDetailsModifier> modifiers,
            PaymentOptions paymentOptions,
            List<PaymentShippingOption> shippingOptions,
            InstrumentDetailsCallback callback) {
        mInvokeCallback = callback;
        JniPaymentAppJni.get().invokePaymentApp(mNativeObject, /* callback= */ this);
    }

    @Override
    public void updateWith(PaymentRequestDetailsUpdate response) {
        JniPaymentAppJni.get().updateWith(mNativeObject, response.serialize());
    }

    @Override
    public void onPaymentDetailsNotUpdated() {
        JniPaymentAppJni.get().onPaymentDetailsNotUpdated(mNativeObject);
    }

    @Override
    public boolean isWaitingForPaymentDetailsUpdate() {
        return JniPaymentAppJni.get().isWaitingForPaymentDetailsUpdate(mNativeObject);
    }

    @Override
    public void abortPaymentApp(AbortCallback callback) {
        mAbortCallback = callback;
        JniPaymentAppJni.get().abortPaymentApp(mNativeObject, this);
    }

    @Override
    public @Nullable String getApplicationIdentifierToHide() {
        return JniPaymentAppJni.get().getApplicationIdentifierToHide(mNativeObject);
    }

    @Override
    public @Nullable Set<String> getApplicationIdentifiersThatHideThisApp() {
        return new HashSet<>(
                Arrays.asList(
                        JniPaymentAppJni.get()
                                .getApplicationIdentifiersThatHideThisApp(mNativeObject)));
    }

    @Override
    public long getUkmSourceId() {
        return JniPaymentAppJni.get().getUkmSourceId(mNativeObject);
    }

    @Override
    public void setPaymentHandlerHost(PaymentHandlerHost host) {
        JniPaymentAppJni.get().setPaymentHandlerHost(mNativeObject, host);
    }

    @Override
    public void dismissInstrument() {
        if (mNativeObject == 0) return;
        JniPaymentAppJni.get().freeNativeObject(mNativeObject);
        mNativeObject = 0;
    }

    // TODO(crbug.com/40286193): Use an explicit destroy() method.
    @SuppressWarnings("Finalize")
    @Override
    public void finalize() throws Throwable {
        dismissInstrument();
        super.finalize();
    }

    @Override
    public @PaymentAppType int getPaymentAppType() {
        return mPaymentAppType;
    }

    @Override
    public PaymentResponse setAppSpecificResponseFields(PaymentResponse response) {
        byte[] byteResult =
                JniPaymentAppJni.get()
                        .setAppSpecificResponseFields(mNativeObject, response.serialize());
        return PaymentResponse.deserialize(ByteBuffer.wrap(byteResult));
    }

    @Override
    public @Nullable Bitmap getIssuerIcon() {
        return mIssuerIcon;
    }

    @Override
    public @Nullable Bitmap getNetworkIcon() {
        return mNetworkIcon;
    }

    @Override
    public List<PaymentEntityLogo> getPaymentEntitiesLogos() {
        return unmodifiableList(mPaymentEntitiesLogos);
    }

    @NativeMethods
    interface Natives {
        String[] getInstrumentMethodNames(long nativeJniPaymentApp);

        boolean isValidForPaymentMethodData(
                long nativeJniPaymentApp, String method, @Nullable ByteBuffer dataByteBuffer);

        boolean handlesShippingAddress(long nativeJniPaymentApp);

        boolean handlesPayerName(long nativeJniPaymentApp);

        boolean handlesPayerEmail(long nativeJniPaymentApp);

        boolean handlesPayerPhone(long nativeJniPaymentApp);

        boolean hasEnrolledInstrument(long nativeJniPaymentApp);

        boolean canPreselect(long nativeJniPaymentApp);

        void invokePaymentApp(long nativeJniPaymentApp, JniPaymentApp callback);

        void updateWith(long nativeJniPaymentApp, ByteBuffer responseByteBuffer);

        void onPaymentDetailsNotUpdated(long nativeJniPaymentApp);

        boolean isWaitingForPaymentDetailsUpdate(long nativeJniPaymentApp);

        void abortPaymentApp(long nativeJniPaymentApp, JniPaymentApp callback);

        String getApplicationIdentifierToHide(long nativeJniPaymentApp);

        String[] getApplicationIdentifiersThatHideThisApp(long nativeJniPaymentApp);

        long getUkmSourceId(long nativeJniPaymentApp);

        void setPaymentHandlerHost(long nativeJniPaymentApp, PaymentHandlerHost paymentHandlerHost);

        void freeNativeObject(long nativeJniPaymentApp);

        byte[] setAppSpecificResponseFields(long nativeJniPaymentApp, ByteBuffer paymentResponse);
    }
}
