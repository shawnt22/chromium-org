// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_CHROME_PAYMENTS_AUTOFILL_CLIENT_H_
#define CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_CHROME_PAYMENTS_AUTOFILL_CLIENT_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ref.h"
#include "components/autofill/core/browser/data_model/payments/credit_card.h"
#include "components/autofill/core/browser/payments/autofill_error_dialog_context.h"
#include "components/autofill/core/browser/payments/payments_autofill_client.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/autofill/core/browser/ui/payments/autofill_error_dialog_controller_impl.h"
#include "components/autofill/core/browser/ui/payments/autofill_progress_dialog_controller_impl.h"
#include "components/autofill/core/browser/ui/payments/card_unmask_prompt_controller_impl.h"
#include "components/autofill/core/browser/ui/payments/save_and_fill_dialog_controller_impl.h"
#include "content/public/browser/web_contents_observer.h"

#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/touch_to_fill/autofill/android/touch_to_fill_payment_method_controller_impl.h"
#include "components/autofill/core/browser/ui/payments/card_expiration_date_fix_flow_controller_impl.h"
#include "components/autofill/core/browser/ui/payments/card_name_fix_flow_controller_impl.h"
#endif  // BUILDFLAG(IS_ANDROID)

class GURL;

namespace webauthn {
class InternalAuthenticator;
}

namespace autofill {

#if BUILDFLAG(IS_ANDROID)
class AutofillCvcSaveMessageDelegate;
#endif  // BUILDFLAG(IS_ANDROID)
class AutofillDriver;
class AutofillErrorDialogControllerImpl;
#if BUILDFLAG(IS_ANDROID)
class AutofillMessageController;
#endif
class AutofillOfferData;
class AutofillOfferManager;
class AutofillSaveCardBottomSheetBridge;
class AutofillSaveIbanBottomSheetBridge;
class BnplTosControllerImpl;
struct BnplTosModel;
class CardUnmaskAuthenticationSelectionDialogControllerImpl;
struct CardUnmaskChallengeOption;
class CardUnmaskOtpInputDialogControllerImpl;
class CreditCardCvcAuthenticator;
class CreditCardOtpAuthenticator;
class ContentAutofillClient;
class CreditCardRiskBasedAuthenticator;
struct FilledCardInformationBubbleOptions;
class IbanAccessManager;
class IbanManager;
class MerchantPromoCodeManager;
struct OfferNotificationOptions;
class OtpUnmaskDelegate;
enum class OtpUnmaskResult;
class PaymentsDataManager;
class SaveAndFillDialogControllerImpl;
class SaveAndFillManager;
class TouchToFillDelegate;
struct VirtualCardEnrollmentFields;
class VirtualCardEnrollmentManager;

namespace payments {

struct BnplIssuerContext;
class MandatoryReauthManager;
class MultipleRequestPaymentsNetworkInterface;
class PaymentsWindowManager;
class SelectBnplIssuerDialogControllerImpl;

// Chrome implementation of PaymentsAutofillClient. Used for Chrome Desktop
// and Clank. Owned by the ChromeAutofillClient. Created lazily in the
// ChromeAutofillClient when it is needed, and it observes the same
// WebContents as its owning ChromeAutofillClient.
class ChromePaymentsAutofillClient : public PaymentsAutofillClient,
                                     public content::WebContentsObserver {
 public:
  explicit ChromePaymentsAutofillClient(ContentAutofillClient* client);
  ChromePaymentsAutofillClient(const ChromePaymentsAutofillClient&) = delete;
  ChromePaymentsAutofillClient& operator=(const ChromePaymentsAutofillClient&) =
      delete;
  ~ChromePaymentsAutofillClient() override;

  static constexpr base::TimeDelta kSaveCardConfirmationSnackbarDuration =
      base::Seconds(3);

  // RiskDataLoader:
  void LoadRiskData(
      base::OnceCallback<void(const std::string&)> callback) override;

  // PaymentsAutofillClient:
#if BUILDFLAG(IS_ANDROID)
  AutofillSaveCardBottomSheetBridge*
  GetOrCreateAutofillSaveCardBottomSheetBridge() override;
  AutofillSaveIbanBottomSheetBridge*
  GetOrCreateAutofillSaveIbanBottomSheetBridge();
  void ConfirmAccountNameFixFlow(
      base::OnceCallback<void(const std::u16string&)> callback) override;
  void ConfirmExpirationDateFixFlow(
      const CreditCard& card,
      base::OnceCallback<void(const std::u16string&, const std::u16string&)>
          callback) override;
#else   // !BUILDFLAG(IS_ANDROID)
  void ShowWebauthnOfferDialog(
      WebauthnDialogCallback offer_dialog_callback) override;
  void ShowWebauthnVerifyPendingDialog(
      WebauthnDialogCallback verify_pending_dialog_callback) override;
  void UpdateWebauthnOfferDialogWithError() override;
  bool CloseWebauthnDialog() override;
  void HideVirtualCardEnrollBubbleAndIconIfVisible() override;
#endif  // BUILDFLAG(IS_ANDROID)
  bool HasCreditCardScanFeature() const override;
  void ScanCreditCard(CreditCardScanCallback callback) override;
  void ShowSaveCreditCardLocally(const CreditCard& card,
                                 SaveCreditCardOptions options,
                                 LocalSaveCardPromptCallback callback) override;
  void ShowSaveCreditCardToCloud(
      const CreditCard& card,
      const LegalMessageLines& legal_message_lines,
      SaveCreditCardOptions options,
      UploadSaveCardPromptCallback callback) override;
  void CreditCardUploadCompleted(PaymentsRpcResult result,
                                 std::optional<OnConfirmationClosedCallback>
                                     on_confirmation_closed_callback) override;
  void HideSaveCardPrompt() override;
  void ShowVirtualCardEnrollDialog(
      const VirtualCardEnrollmentFields& virtual_card_enrollment_fields,
      base::OnceClosure accept_virtual_card_callback,
      base::OnceClosure decline_virtual_card_callback) override;
  void VirtualCardEnrollCompleted(PaymentsRpcResult result) override;
  void OnCardDataAvailable(
      const FilledCardInformationBubbleOptions& options) override;
  void ConfirmSaveIbanLocally(const Iban& iban,
                              bool should_show_prompt,
                              SaveIbanPromptCallback callback) override;
  void ConfirmUploadIbanToCloud(const Iban& iban,
                                LegalMessageLines legal_message_lines,
                                bool should_show_prompt,
                                SaveIbanPromptCallback callback) override;
  void IbanUploadCompleted(bool iban_saved, bool hit_max_strikes) override;
  void ShowAutofillProgressDialog(
      AutofillProgressDialogType autofill_progress_dialog_type,
      base::OnceClosure cancel_callback) override;
  void CloseAutofillProgressDialog(
      bool show_confirmation_before_closing,
      base::OnceClosure no_interactive_authentication_callback) override;
  void ShowCardUnmaskOtpInputDialog(
      CreditCard::RecordType card_type,
      const CardUnmaskChallengeOption& challenge_option,
      base::WeakPtr<OtpUnmaskDelegate> delegate) override;
  void OnUnmaskOtpVerificationResult(OtpUnmaskResult unmask_result) override;
  PaymentsNetworkInterface* GetPaymentsNetworkInterface() override;
  MultipleRequestPaymentsNetworkInterface*
  GetMultipleRequestPaymentsNetworkInterface() override;
  void ShowAutofillErrorDialog(AutofillErrorDialogContext context) override;
  PaymentsWindowManager* GetPaymentsWindowManager() override;
  void ShowUnmaskPrompt(
      const CreditCard& card,
      const CardUnmaskPromptOptions& card_unmask_prompt_options,
      base::WeakPtr<CardUnmaskDelegate> delegate) override;
  void ShowUnmaskAuthenticatorSelectionDialog(
      const std::vector<CardUnmaskChallengeOption>& challenge_options,
      base::OnceCallback<void(const std::string&)>
          confirm_unmask_challenge_option_callback,
      base::OnceClosure cancel_unmasking_closure) override;
  void DismissUnmaskAuthenticatorSelectionDialog(bool server_success) override;
  void OnUnmaskVerificationResult(PaymentsRpcResult result) override;
  void ShowBnplTos(BnplTosModel bnpl_tos_model,
                   base::OnceClosure accept_callback,
                   base::OnceClosure cancel_callback) override;
  void CloseBnplTos() override;
  VirtualCardEnrollmentManager* GetVirtualCardEnrollmentManager() override;
  CreditCardCvcAuthenticator& GetCvcAuthenticator() override;
  CreditCardOtpAuthenticator* GetOtpAuthenticator() override;
  CreditCardRiskBasedAuthenticator* GetRiskBasedAuthenticator() override;
  void ShowMandatoryReauthOptInPrompt(
      base::OnceClosure accept_mandatory_reauth_callback,
      base::OnceClosure cancel_mandatory_reauth_callback,
      base::RepeatingClosure close_mandatory_reauth_callback) override;
  IbanManager* GetIbanManager() override;
  IbanAccessManager* GetIbanAccessManager() override;
  void ShowMandatoryReauthOptInConfirmation() override;
  void UpdateOfferNotification(
      const AutofillOfferData& offer,
      const OfferNotificationOptions& options) override;
  void DismissOfferNotification() override;
  void OpenPromoCodeOfferDetailsURL(const GURL& url) override;
  MerchantPromoCodeManager* GetMerchantPromoCodeManager() override;
  AutofillOfferManager* GetAutofillOfferManager() override;
  bool ShowTouchToFillCreditCard(
      base::WeakPtr<TouchToFillDelegate> delegate,
      base::span<const Suggestion> suggestions) override;
  bool ShowTouchToFillIban(
      base::WeakPtr<TouchToFillDelegate> delegate,
      base::span<const autofill::Iban> ibans_to_suggest) override;
  bool ShowTouchToFillLoyaltyCard(
      base::WeakPtr<TouchToFillDelegate> delegate,
      std::vector<autofill::LoyaltyCard> loyalty_cards_to_suggest) override;
  void HideTouchToFillPaymentMethod() override;
  std::unique_ptr<webauthn::InternalAuthenticator>
  CreateCreditCardInternalAuthenticator(AutofillDriver* driver) override;
  payments::MandatoryReauthManager* GetOrCreatePaymentsMandatoryReauthManager()
      override;
  PaymentsDataManager& GetPaymentsDataManager() final;
  void ShowCreditCardSaveAndFillDialog() override;
  payments::SaveAndFillManager* GetSaveAndFillManager() override;
  void ShowSelectBnplIssuerDialog(
      std::vector<BnplIssuerContext> bnpl_issuer_context,
      std::string app_locale,
      base::OnceCallback<void(BnplIssuer)> selected_issuer_callback,
      base::OnceClosure cancel_callback) override;
  void DismissSelectBnplIssuerDialog() override;
  bool IsTabModalPopupDeprecated() const override;
  bool IsRiskBasedAuthEffectivelyAvailable() const override;

#if BUILDFLAG(IS_ANDROID)
  // The AutofillMessageController is used to show a message notification
  // on Android.
  AutofillMessageController& GetAutofillMessageController();

  TouchToFillPaymentMethodController* GetTouchToFillPaymentMethodController();
#endif

  AutofillProgressDialogControllerImpl*
  AutofillProgressDialogControllerForTesting() {
    return autofill_progress_dialog_controller_.get();
  }

  std::unique_ptr<CardUnmaskPromptControllerImpl>
  ExtractCardUnmaskControllerForTesting() {
    return std::move(unmask_controller_);
  }
  void SetCardUnmaskControllerForTesting(
      std::unique_ptr<CardUnmaskPromptControllerImpl> test_controller) {
    unmask_controller_ = std::move(test_controller);
  }

#if BUILDFLAG(IS_ANDROID)
  void SetAutofillSaveCardBottomSheetBridgeForTesting(
      std::unique_ptr<AutofillSaveCardBottomSheetBridge>
          autofill_save_card_bottom_sheet_bridge);

  void SetAutofillSaveIbanBottomSheetBridgeForTesting(
      std::unique_ptr<AutofillSaveIbanBottomSheetBridge>
          autofill_save_iban_bottom_sheet_bridge);

  void SetAutofillMessageControllerForTesting(
      std::unique_ptr<AutofillMessageController> autofill_message_controller);

  void SetTouchToFillPaymentMethodControllerForTesting(
      std::unique_ptr<TouchToFillPaymentMethodController>
          touch_to_fill_payment_method_controller);

#endif
  void SetRiskDataForTesting(const std::string& risk_data);

  void SetCachedRiskDataLoadedCallbackForTesting(
      base::OnceCallback<void(const std::string&)>
          cached_risk_data_loaded_callback_for_testing);

 private:
  std::u16string GetAccountHolderName() const;

  const raw_ref<ContentAutofillClient> client_;

  // The method takes `risk_data` and caches it in `risk_data_`, logs the start
  // time and runs the callback with the risk_data.
  void OnRiskDataLoaded(base::OnceCallback<void(const std::string&)> callback,
                        base::TimeTicks start_time,
                        const std::string& risk_data);

#if BUILDFLAG(IS_ANDROID)
  std::unique_ptr<AutofillCvcSaveMessageDelegate>
      autofill_cvc_save_message_delegate_;

  std::unique_ptr<AutofillSaveCardBottomSheetBridge>
      autofill_save_card_bottom_sheet_bridge_;

  std::unique_ptr<AutofillSaveIbanBottomSheetBridge>
      autofill_save_iban_bottom_sheet_bridge_;

  std::unique_ptr<AutofillMessageController> autofill_message_controller_;

  CardNameFixFlowControllerImpl card_name_fix_flow_controller_;

  CardExpirationDateFixFlowControllerImpl
      card_expiration_date_fix_flow_controller_;

  std::unique_ptr<TouchToFillPaymentMethodController>
      touch_to_fill_payment_method_controller_ =
          std::make_unique<TouchToFillPaymentMethodControllerImpl>(
              &client_.get());
#endif

  std::unique_ptr<PaymentsNetworkInterface> payments_network_interface_;

  std::unique_ptr<MultipleRequestPaymentsNetworkInterface>
      multiple_request_payments_network_interface_;

  std::unique_ptr<AutofillProgressDialogControllerImpl>
      autofill_progress_dialog_controller_;

  std::unique_ptr<AutofillErrorDialogControllerImpl>
      autofill_error_dialog_controller_;

  std::unique_ptr<CardUnmaskOtpInputDialogControllerImpl>
      card_unmask_otp_input_dialog_controller_;

  std::unique_ptr<PaymentsWindowManager> payments_window_manager_;

  std::unique_ptr<CardUnmaskPromptControllerImpl> unmask_controller_;

  // `virtual_card_enrollment_manager_` must be destroyed before
  // `payments_network_interface_` because the former keeps a reference to the
  // latter.
  // TODO(crbug.com/41489024): Remove the reference to
  // `payments_network_interface_` in `virtual_card_enrollment_manager_`.
  std::unique_ptr<VirtualCardEnrollmentManager>
      virtual_card_enrollment_manager_;

  std::unique_ptr<CreditCardCvcAuthenticator> cvc_authenticator_;

  std::unique_ptr<CreditCardOtpAuthenticator> otp_authenticator_;

  std::unique_ptr<CreditCardRiskBasedAuthenticator> risk_based_authenticator_;

  std::unique_ptr<CardUnmaskAuthenticationSelectionDialogControllerImpl>
      card_unmask_authentication_selection_controller_;

  std::unique_ptr<BnplTosControllerImpl> bnpl_tos_controller_;

  std::unique_ptr<IbanAccessManager> iban_access_manager_;

  std::unique_ptr<payments::MandatoryReauthManager>
      payments_mandatory_reauth_manager_;

  std::unique_ptr<SaveAndFillDialogControllerImpl>
      save_and_fill_dialog_controller_;

  std::unique_ptr<SaveAndFillManager> save_and_fill_manager_;

  std::unique_ptr<SelectBnplIssuerDialogControllerImpl>
      select_bnpl_issuer_dialog_controller_;

  // Used to cache client side risk data. The cache is invalidated when the
  // chrome browser tab is closed.
  std::string risk_data_;

  base::OnceCallback<void(const std::string&)>
      cached_risk_data_loaded_callback_for_testing_;

  base::WeakPtrFactory<ChromePaymentsAutofillClient> weak_ptr_factory_{this};
};

}  // namespace payments

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_CHROME_PAYMENTS_AUTOFILL_CLIENT_H_
