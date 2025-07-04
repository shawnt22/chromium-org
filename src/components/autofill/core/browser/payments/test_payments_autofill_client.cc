// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/payments/test_payments_autofill_client.h"

#include <memory>

#include "base/check_deref.h"
#include "base/functional/callback.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/foundations/autofill_client.h"
#include "components/autofill/core/browser/integrators/touch_to_fill/touch_to_fill_delegate.h"
#include "components/autofill/core/browser/payments/autofill_offer_manager.h"
#include "components/autofill/core/browser/payments/credit_card_cvc_authenticator.h"
#include "components/autofill/core/browser/payments/credit_card_otp_authenticator.h"
#include "components/autofill/core/browser/payments/mandatory_reauth_manager.h"
#include "components/autofill/core/browser/payments/test/mock_payments_window_manager.h"
#include "components/autofill/core/browser/payments/virtual_card_enrollment_manager.h"
#include "components/autofill/core/browser/single_field_fillers/payments/merchant_promo_code_manager.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/autofill/core/browser/test_utils/autofill_test_utils.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/build_info.h"
#include "base/test/gmock_callback_support.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_IOS)
#include "components/autofill/core/browser/payments/test_internal_authenticator.h"
#include "components/webauthn/core/browser/internal_authenticator.h"
#endif  // !BUILDFLAG(IS_IOS)

namespace autofill::payments {

TestPaymentsAutofillClient::TestPaymentsAutofillClient(AutofillClient* client)
    : client_(CHECK_DEREF(client)),
      mock_merchant_promo_code_manager_(
          &client_->GetPersonalDataManager().payments_data_manager()) {}

TestPaymentsAutofillClient::~TestPaymentsAutofillClient() = default;

void TestPaymentsAutofillClient::LoadRiskData(
    base::OnceCallback<void(const std::string&)> callback) {
  risk_data_loaded_ = true;
  std::move(callback).Run("some risk data");
}

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
void TestPaymentsAutofillClient::ConfirmSaveIbanLocally(
    const Iban& iban,
    bool should_show_prompt,
    payments::PaymentsAutofillClient::SaveIbanPromptCallback callback) {
  confirm_save_iban_locally_called_ = true;
  offer_to_save_iban_bubble_was_shown_ = should_show_prompt;
}

void TestPaymentsAutofillClient::ConfirmUploadIbanToCloud(
    const Iban& iban,
    LegalMessageLines legal_message_lines,
    bool should_show_prompt,
    payments::PaymentsAutofillClient::SaveIbanPromptCallback callback) {
  confirm_upload_iban_to_cloud_called_ = true;
  legal_message_lines_ = std::move(legal_message_lines);
  offer_to_save_iban_bubble_was_shown_ = should_show_prompt;
}

bool TestPaymentsAutofillClient::CloseWebauthnDialog() {
  return true;
}
#else   // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
void TestPaymentsAutofillClient::ConfirmAccountNameFixFlow(
    base::OnceCallback<void(const std::u16string&)> callback) {
  credit_card_name_fix_flow_bubble_was_shown_ = true;
  std::move(callback).Run(std::u16string(u"Gaia Name"));
}

void TestPaymentsAutofillClient::ConfirmExpirationDateFixFlow(
    const CreditCard& card,
    base::OnceCallback<void(const std::u16string&, const std::u16string&)>
        callback) {
  credit_card_name_fix_flow_bubble_was_shown_ = true;
  std::move(callback).Run(
      std::u16string(u"03"),
      std::u16string(base::ASCIIToUTF16(test::NextYear().c_str())));
}
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

PaymentsNetworkInterface*
TestPaymentsAutofillClient::GetPaymentsNetworkInterface() {
  return payments_network_interface_.get();
}

void TestPaymentsAutofillClient::ShowAutofillProgressDialog(
    AutofillProgressDialogType autofill_progress_dialog_type,
    base::OnceClosure cancel_callback) {
  autofill_progress_dialog_shown_ = true;
  autofill_progress_dialog_type_ = autofill_progress_dialog_type;
}

void TestPaymentsAutofillClient::CloseAutofillProgressDialog(
    bool show_confirmation_before_closing,
    base::OnceClosure no_user_perceived_authentication_callback) {
  if (no_user_perceived_authentication_callback) {
    std::move(no_user_perceived_authentication_callback).Run();
  }
}

void TestPaymentsAutofillClient::ShowAutofillErrorDialog(
    AutofillErrorDialogContext context) {
  autofill_error_dialog_shown_ = true;
  autofill_error_dialog_context_ = std::move(context);
}

void TestPaymentsAutofillClient::ShowCardUnmaskOtpInputDialog(
    CreditCard::RecordType card_type,
    const CardUnmaskChallengeOption& challenge_option,
    base::WeakPtr<OtpUnmaskDelegate> delegate) {
  show_otp_input_dialog_ = true;
}

PaymentsWindowManager* TestPaymentsAutofillClient::GetPaymentsWindowManager() {
  if (!payments_window_manager_) {
    payments_window_manager_ =
        std::make_unique<testing::NiceMock<MockPaymentsWindowManager>>();
  }
  return payments_window_manager_.get();
}

VirtualCardEnrollmentManager*
TestPaymentsAutofillClient::GetVirtualCardEnrollmentManager() {
  if (!virtual_card_enrollment_manager_) {
    virtual_card_enrollment_manager_ =
        std::make_unique<VirtualCardEnrollmentManager>(
            &client_->GetPersonalDataManager().payments_data_manager(),
            GetPaymentsNetworkInterface(), &client_.get());
  }

  return virtual_card_enrollment_manager_.get();
}

CreditCardCvcAuthenticator& TestPaymentsAutofillClient::GetCvcAuthenticator() {
  if (!cvc_authenticator_) {
    cvc_authenticator_ =
        std::make_unique<CreditCardCvcAuthenticator>(&client_.get());
  }
  return *cvc_authenticator_;
}

CreditCardOtpAuthenticator* TestPaymentsAutofillClient::GetOtpAuthenticator() {
  if (!otp_authenticator_) {
    otp_authenticator_ =
        std::make_unique<CreditCardOtpAuthenticator>(&client_.get());
  }
  return otp_authenticator_.get();
}

TestCreditCardRiskBasedAuthenticator*
TestPaymentsAutofillClient::GetRiskBasedAuthenticator() {
  if (!risk_based_authenticator_) {
    risk_based_authenticator_ =
        std::make_unique<TestCreditCardRiskBasedAuthenticator>(&client_.get());
  }
  return risk_based_authenticator_.get();
}

void TestPaymentsAutofillClient::ShowMandatoryReauthOptInPrompt(
    base::OnceClosure accept_mandatory_reauth_callback,
    base::OnceClosure cancel_mandatory_reauth_callback,
    base::RepeatingClosure close_mandatory_reauth_callback) {
  mandatory_reauth_opt_in_prompt_was_shown_ = true;
}

MockIbanManager* TestPaymentsAutofillClient::GetIbanManager() {
  if (!mock_iban_manager_) {
    mock_iban_manager_ = std::make_unique<testing::NiceMock<MockIbanManager>>(
        &client_->GetPersonalDataManager().payments_data_manager());
  }
  return mock_iban_manager_.get();
}

MockIbanAccessManager* TestPaymentsAutofillClient::GetIbanAccessManager() {
  if (!mock_iban_access_manager_) {
    mock_iban_access_manager_ =
        std::make_unique<testing::NiceMock<MockIbanAccessManager>>(
            &client_.get());
  }
  return mock_iban_access_manager_.get();
}

MockSaveAndFillManager* TestPaymentsAutofillClient::GetSaveAndFillManager() {
  if (!mock_save_and_fill_manager_) {
    mock_save_and_fill_manager_ =
        std::make_unique<testing::NiceMock<MockSaveAndFillManager>>();
  }
  return mock_save_and_fill_manager_.get();
}

void TestPaymentsAutofillClient::ShowMandatoryReauthOptInConfirmation() {
  mandatory_reauth_opt_in_prompt_was_reshown_ = true;
}

MockMerchantPromoCodeManager*
TestPaymentsAutofillClient::GetMerchantPromoCodeManager() {
  return &mock_merchant_promo_code_manager_;
}

AutofillOfferManager* TestPaymentsAutofillClient::GetAutofillOfferManager() {
  return autofill_offer_manager_.get();
}

bool TestPaymentsAutofillClient::ShowTouchToFillCreditCard(
    base::WeakPtr<TouchToFillDelegate> delegate,
    base::span<const Suggestion> suggestions) {
  return false;
}

bool TestPaymentsAutofillClient::IsTabModalPopupDeprecated() const {
  return is_tab_model_popup_;
}

#if !BUILDFLAG(IS_IOS)
std::unique_ptr<webauthn::InternalAuthenticator>
TestPaymentsAutofillClient::CreateCreditCardInternalAuthenticator(
    AutofillDriver* driver) {
  return std::make_unique<TestInternalAuthenticator>();
}
#endif

MockMandatoryReauthManager*
TestPaymentsAutofillClient::GetOrCreatePaymentsMandatoryReauthManager() {
  if (!mock_payments_mandatory_reauth_manager_) {
    mock_payments_mandatory_reauth_manager_ = std::make_unique<
        testing::NiceMock<payments::MockMandatoryReauthManager>>();
  }
  return mock_payments_mandatory_reauth_manager_.get();
}

PaymentsDataManager& TestPaymentsAutofillClient::GetPaymentsDataManager() {
  return client_->GetPersonalDataManager().payments_data_manager();
}

bool TestPaymentsAutofillClient::GetMandatoryReauthOptInPromptWasShown() {
  return mandatory_reauth_opt_in_prompt_was_shown_;
}

bool TestPaymentsAutofillClient::GetMandatoryReauthOptInPromptWasReshown() {
  return mandatory_reauth_opt_in_prompt_was_reshown_;
}

bool TestPaymentsAutofillClient::IsRiskBasedAuthEffectivelyAvailable() const {
  return true;
}

void TestPaymentsAutofillClient::set_virtual_card_enrollment_manager(
    std::unique_ptr<VirtualCardEnrollmentManager> vcem) {
  virtual_card_enrollment_manager_ = std::move(vcem);
}

void TestPaymentsAutofillClient::set_otp_authenticator(
    std::unique_ptr<CreditCardOtpAuthenticator> authenticator) {
  otp_authenticator_ = std::move(authenticator);
}

void TestPaymentsAutofillClient::ShowUnmaskAuthenticatorSelectionDialog(
    const std::vector<CardUnmaskChallengeOption>& challenge_options,
    base::OnceCallback<void(const std::string&)>
        confirm_unmask_challenge_option_callback,
    base::OnceClosure cancel_unmasking_closure) {
  unmask_authenticator_selection_dialog_shown_ = true;
}

#if BUILDFLAG(IS_ANDROID)
void TestPaymentsAutofillClient::
    SetUpDeviceBiometricAuthenticatorSuccessOnAutomotive() {
  if (!base::android::BuildInfo::GetInstance()->is_automotive()) {
    return;
  }

  payments::MockMandatoryReauthManager& mandatory_reauth_manager =
      *GetOrCreatePaymentsMandatoryReauthManager();

  ON_CALL(mandatory_reauth_manager, GetAuthenticationMethod)
      .WillByDefault(testing::Return(
          payments::MandatoryReauthAuthenticationMethod::kBiometric));

  ON_CALL(mandatory_reauth_manager, Authenticate)
      .WillByDefault(testing::WithArg<0>(
          testing::Invoke([](base::OnceCallback<void(bool)> callback) {
            std::move(callback).Run(true);
          })));
}
#endif

}  // namespace autofill::payments
