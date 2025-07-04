// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_android.h"
#include "components/payments/content/payment_request_web_contents_manager.h"
#include "components/payments/content/secure_payment_confirmation_transaction_mode.h"
#include "content/public/browser/web_contents.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "components/payments/content/android/jni_headers/PaymentRequestWebContentsData_jni.h"

namespace payments {
namespace android {

// static
jboolean JNI_PaymentRequestWebContentsData_HadActivationlessShow(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jweb_contents) {
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  CHECK(web_contents);

  return PaymentRequestWebContentsManager::GetOrCreateForWebContents(
             web_contents)
      ->HadActivationlessShow();
}

// static
void JNI_PaymentRequestWebContentsData_RecordActivationlessShow(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jweb_contents) {
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  CHECK(web_contents);

  PaymentRequestWebContentsManager::GetOrCreateForWebContents(web_contents)
      ->RecordActivationlessShow();
}

// static
jint JNI_PaymentRequestWebContentsData_GetSPCTransactionMode(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jweb_contents) {
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  CHECK(web_contents);

  return static_cast<jint>(
      PaymentRequestWebContentsManager::GetOrCreateForWebContents(web_contents)
          ->transaction_mode());
}

}  // namespace android
}  // namespace payments
