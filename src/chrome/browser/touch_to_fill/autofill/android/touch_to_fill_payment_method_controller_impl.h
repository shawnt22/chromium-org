// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TOUCH_TO_FILL_AUTOFILL_ANDROID_TOUCH_TO_FILL_PAYMENT_METHOD_CONTROLLER_IMPL_H_
#define CHROME_BROWSER_TOUCH_TO_FILL_AUTOFILL_ANDROID_TOUCH_TO_FILL_PAYMENT_METHOD_CONTROLLER_IMPL_H_

#include <memory>
#include <string>

#include "base/android/scoped_java_ref.h"
#include "base/containers/span.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/touch_to_fill/autofill/android/touch_to_fill_payment_method_controller.h"
#include "components/autofill/android/touch_to_fill_keyboard_suppressor.h"
#include "components/autofill/content/browser/content_autofill_driver_factory.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"

namespace autofill {

class ContentAutofillClient;
class Iban;
class LoyaltyCard;
class TouchToFillDelegate;
class TouchToFillPaymentMethodView;

// Controller of the bottom sheet surface for filling credit card IBAN or
// loyalty cards on Android. It is responsible for showing the view and handling
// user interactions. While the surface is shown, stores its Java counterpart in
// `java_object_`.
class TouchToFillPaymentMethodControllerImpl
    : public TouchToFillPaymentMethodController,
      public ContentAutofillDriverFactory::Observer,
      public content::WebContentsObserver {
 public:
  explicit TouchToFillPaymentMethodControllerImpl(
      ContentAutofillClient* autofill_client);
  TouchToFillPaymentMethodControllerImpl(
      const TouchToFillPaymentMethodControllerImpl&) = delete;
  TouchToFillPaymentMethodControllerImpl& operator=(
      const TouchToFillPaymentMethodControllerImpl&) = delete;
  ~TouchToFillPaymentMethodControllerImpl() override;

  // TouchToFillPaymentMethodController:
  bool ShowCreditCards(std::unique_ptr<TouchToFillPaymentMethodView> view,
                       base::WeakPtr<TouchToFillDelegate> delegate,
                       base::span<const Suggestion> suggestions) override;
  bool ShowIbans(std::unique_ptr<TouchToFillPaymentMethodView> view,
                 base::WeakPtr<TouchToFillDelegate> delegate,
                 base::span<const Iban> ibans_to_suggest) override;
  bool ShowLoyaltyCards(std::unique_ptr<TouchToFillPaymentMethodView> view,
                        base::WeakPtr<TouchToFillDelegate> delegate,
                        base::span<const LoyaltyCard> affiliated_loyalty_cards,
                        base::span<const LoyaltyCard> all_loyalty_cards,
                        bool first_time_usage) override;
  void Hide() override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // ContentAutofillDriverFactory::Observer:
  void OnContentAutofillDriverFactoryDestroyed(
      ContentAutofillDriverFactory& factory) override;
  void OnContentAutofillDriverCreated(ContentAutofillDriverFactory& factory,
                                      ContentAutofillDriver& driver) override;

  // TouchToFillPaymentMethodViewController:
  void OnDismissed(JNIEnv* env, bool dismissed_by_user) override;
  void ScanCreditCard(JNIEnv* env) override;
  void ShowPaymentMethodSettings(JNIEnv* env) override;
  void CreditCardSuggestionSelected(
      JNIEnv* env,
      const base::android::JavaParamRef<jstring>& unique_id,
      bool is_virtual) override;
  void LocalIbanSuggestionSelected(
      JNIEnv* env,
      const base::android::JavaParamRef<jstring>& guid) override;
  void ServerIbanSuggestionSelected(JNIEnv* env, long instrument_id) override;
  void LoyaltyCardSuggestionSelected(
      JNIEnv* env,
      const std::string& loyalty_card_number) override;
  int GetJavaResourceId(int native_resource_id) override;
  base::android::ScopedJavaLocalRef<jobject> GetJavaObject() override;
  void ResetJavaObject();

  TouchToFillKeyboardSuppressor& keyboard_suppressor_for_test() {
    return keyboard_suppressor_;
  }

 private:
  // Observes creation of ContentAutofillDrivers to inject a
  // TouchToFillDelegateAndroidImpl into the BrowserAutofillManager.
  base::ScopedObservation<ContentAutofillDriverFactory,
                          ContentAutofillDriverFactory::Observer>
      driver_factory_observation_{this};
  // Delegate for the surface being shown.
  base::WeakPtr<TouchToFillDelegate> delegate_;
  // View that displays the surface, owned by `this`.
  std::unique_ptr<TouchToFillPaymentMethodView> view_;
  // The corresponding Java TouchToFillPaymentMethodControllerBridge.
  base::android::ScopedJavaGlobalRef<jobject> java_object_;
  // Suppresses the keyboard between
  // AutofillManager::Observer::On{Before,After}AskForValuesToFill() events if
  // TTF may be shown.
  TouchToFillKeyboardSuppressor keyboard_suppressor_;
};

}  // namespace autofill

#endif  // CHROME_BROWSER_TOUCH_TO_FILL_AUTOFILL_ANDROID_TOUCH_TO_FILL_PAYMENT_METHOD_CONTROLLER_IMPL_H_
