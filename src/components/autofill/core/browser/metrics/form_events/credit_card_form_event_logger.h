// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_METRICS_FORM_EVENTS_CREDIT_CARD_FORM_EVENT_LOGGER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_METRICS_FORM_EVENTS_CREDIT_CARD_FORM_EVENT_LOGGER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/autofill_trigger_source.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/payments/credit_card.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/foundations/autofill_client.h"
#include "components/autofill/core/browser/metrics/autofill_metrics.h"
#include "components/autofill/core/browser/metrics/form_events/form_event_logger_base.h"
#include "components/autofill/core/browser/metrics/form_events/form_events.h"
#include "components/autofill/core/browser/metrics/payments/card_metadata_metrics.h"
#include "components/autofill/core/common/signatures.h"

namespace autofill {

enum class UnmaskAuthFlowType;

namespace autofill_metrics {

class CreditCardFormEventLogger : public FormEventLoggerBase {
 public:
  enum class UnmaskAuthFlowEvent {
    // Authentication prompt is shown.
    kPromptShown = 0,
    // Authentication prompt successfully completed.
    kPromptCompleted = 1,
    // Form was submitted.
    kFormSubmitted = 2,
    kMaxValue = kFormSubmitted,
  };

  explicit CreditCardFormEventLogger(BrowserAutofillManager* owner);

  ~CreditCardFormEventLogger() override;

  void set_server_record_type_count(size_t server_record_type_count) {
    server_record_type_count_ = server_record_type_count;
  }

  void set_local_record_type_count(size_t local_record_type_count) {
    local_record_type_count_ = local_record_type_count;
  }

  // Called by BnplManager after its suggestion update barrier callback is
  // triggered and a BNPL suggestion is shown.
  virtual void OnBnplSuggestionShown();

  // Invoked when `suggestions` are successfully fetched.
  // `with_offer` indicates whether an offer is attached to any of the
  // suggestion in the list.
  // `with_cvc` indicates whether CVC is saved in any of the suggestion in
  // the list.
  // `with_card_info_retrieval_enrolled` indicates whether at least one of the
  // suggestions contains card info retrieval enrolled card.
  // `is_virtual_card_standalone_cvc_field` indicates whether the `suggestions`
  // are fetched for a virtual card standalone CVC field.
  // `metadata_logging_context` contains information about whether any card has
  // a non-empty product description or art image, and whether they are shown.
  void OnDidFetchSuggestion(
      const std::vector<Suggestion>& suggestions,
      bool with_offer,
      bool with_cvc,
      bool with_card_info_retrieval_enrolled,
      bool is_virtual_card_standalone_cvc_field,
      CardMetadataLoggingContext metadata_logging_context);

  // TODO(crbug.com/40937936): Remove redundant parameters.
  // form_parsed_timestamp and off_the_record value can be removed, as their
  // values can be retrieved from `form` or `owner_`.
  void OnDidShowSuggestions(const FormStructure& form,
                            const AutofillField& field,
                            base::TimeTicks form_parsed_timestamp,
                            bool off_the_record,
                            base::span<const Suggestion> suggestions) override;

  void OnDidSelectCardSuggestion(
      const CreditCard& credit_card,
      const FormStructure& form,
      AutofillMetrics::PaymentsSigninState signin_state_for_metrics);

  // To be called whenever (by BrowserAutofillManager) whenever a form is filled
  // (but not on preview).
  //
  // In case of masked cards, the caller must make sure this gets called before
  // the card is upgraded to a full card.
  //
  // The `newly_filled_fields` are all fields of `form` that are newly
  // filled by BrowserAutofillManager. They are still subject to the security
  // policy for cross-frame filling.
  //
  // The `safe_filled_fields` are all fields of `newly_filled_fields` that
  // adhere to the security policy for cross-frame filling, and therefore, the
  // actually filled fields.
  void OnDidFillFormFillingSuggestion(
      const CreditCard& credit_card,
      const FormStructure& form,
      const AutofillField& field,
      const base::flat_set<FieldGlobalId>& newly_filled_fields,
      const base::flat_set<FieldGlobalId>& safe_filled_fields,
      AutofillMetrics::PaymentsSigninState signin_state_for_metrics,
      const AutofillTriggerSource trigger_source);

  void OnDidUndoAutofill();

  virtual void OnMetadataLoggingContextReceived(
      autofill_metrics::CardMetadataLoggingContext metadata_logging_context);

  void Log(FormEvent event, const FormStructure& form) override;

  // Logging what type of authentication flow was prompted.
  void LogCardUnmaskAuthenticationPromptShown(UnmaskAuthFlowType flow);

  // Logging when an authentication prompt is completed.
  void LogCardUnmaskAuthenticationPromptCompleted(UnmaskAuthFlowType flow);

  // Allows mocking that a virtual card was selected, for unit tests that don't
  // run the actual Autofill suggestions dropdown UI.
  void set_latest_selected_card_was_virtual_card_for_testing(
      bool latest_selected_card_was_virtual_card) {
    latest_selected_card_was_virtual_card_ =
        latest_selected_card_was_virtual_card;
  }

  void set_signin_state_for_metrics(
      AutofillMetrics::PaymentsSigninState state) {
    signin_state_for_metrics_ = state;
  }

  // Logging when a BNPL suggestion was accepted.
  void OnDidAcceptBnplSuggestion();

  std::optional<CreditCard> GetFilledCreditCardForTesting();

 protected:
  // FormEventLoggerBase pure-virtual overrides.
  void RecordPollSuggestions() override;
  void RecordParseForm() override;
  void RecordShowSuggestions() override;

  // FormEventLoggerBase virtual overrides.
  void LogWillSubmitForm(const FormStructure& form) override;
  void LogFormSubmitted(const FormStructure& form) override;
  void LogUkmInteractedWithForm(FormSignature form_signature) override;
  void OnSuggestionsShownOnce(const FormStructure& form) override;
  void OnSuggestionsShownSubmittedOnce(const FormStructure& form) override;
  void OnLog(const std::string& name,
             FormEvent event,
             const FormStructure& form) const override;
  bool HasLoggedDataToFillAvailable() const override;
  DenseSet<FormTypeNameForLogging> GetSupportedFormTypeNamesForLogging()
      const override;
  DenseSet<FormTypeNameForLogging> GetFormTypesForLogging(
      const FormStructure& form) const override;

  // Bringing base class' Log function into scope to allow overloading.
  using FormEventLoggerBase::Log;

 private:
  FormEvent GetCardNumberStatusFormEvent(const CreditCard& credit_card);
  void RecordCardUnmaskFlowEvent(UnmaskAuthFlowType flow,
                                 UnmaskAuthFlowEvent event);
  bool DoesCardHaveOffer(const CreditCard& credit_card);
  // Returns whether the shown suggestions included a virtual credit card.
  bool DoSuggestionsIncludeVirtualCard();
  // Checks whether the current website is relevant for BNPL for any known BNPL
  // provider, according to the optimization guide.
  bool IsEligibleForBnpl();

  size_t server_record_type_count_ = 0;
  size_t local_record_type_count_ = 0;
  UnmaskAuthFlowType current_authentication_flow_;
  bool has_logged_suggestion_with_metadata_shown_ = false;
  bool has_logged_suggestion_with_metadata_selected_ = false;
  bool has_logged_local_card_suggestion_selected_ = false;
  bool has_logged_masked_server_card_suggestion_selected_ = false;
  bool has_logged_masked_server_card_suggestion_filled_ = false;
  bool has_logged_virtual_card_suggestion_selected_ = false;
  bool has_logged_suggestion_for_virtual_card_standalone_cvc_shown_ = false;
  bool has_logged_suggestion_for_virtual_card_standalone_cvc_selected_ = false;
  bool has_logged_suggestion_for_virtual_card_standalone_cvc_filled_ = false;
  bool has_logged_suggestion_for_card_info_retrieval_enrolled_shown_ = false;
  bool has_logged_suggestion_for_card_info_retrieval_enrolled_selected_ = false;
  bool has_logged_suggestion_for_card_info_retrieval_enrolled_filled_ = false;
  bool has_logged_suggestion_for_card_with_cvc_shown_ = false;
  bool has_logged_suggestion_for_card_with_cvc_selected_ = false;
  bool has_logged_suggestion_for_card_with_cvc_filled_ = false;
  bool has_logged_suggestion_shown_for_benefits_ = false;
  bool logged_suggestion_filled_was_masked_server_card_ = false;
  bool logged_suggestion_filled_was_virtual_card_ = false;
  // If true, the most recent card to be selected as an Autofill suggestion was
  // a virtual card. False for all other card types.
  bool latest_selected_card_was_virtual_card_ = false;
  // If true, the most recent card that was filled as an Autofill suggestion was
  // a card enrolled in runtime retrieval, i.e. a card that had information such
  // as CVC or card number retrieved from the server. (False for all other card
  // types.)
  bool latest_filled_card_was_card_info_retrieval_enrolled_ = false;
  // If true, the most recent card that was filled as an Autofill suggestion
  // was a masked server card. False for all other card types.
  bool latest_filled_card_was_masked_server_card_ = false;
  std::vector<Suggestion> suggestions_;
  bool has_eligible_offer_ = false;
  bool card_selected_has_offer_ = false;
  // If true, the selected server card was filled and it had an equivalent local
  // version on file.
  bool server_card_with_local_duplicate_filled_ = false;
  // If true, the form contains a standalone CVC field that is associated with a
  // virtual card.
  bool is_virtual_card_standalone_cvc_field_ = false;
  // If true, one of the cards in the suggestions fetched has cvc info saved.
  bool suggestion_contains_card_with_cvc_ = false;
  // If true, one of the cards in the suggestions fetched card info retrieval
  // enrolled.
  bool suggestion_contains_card_info_retrieval_enrolled_card_ = false;
  // If true, the suggestions shown on BNPL eligible merchant is logged and
  // should not be logged again.
  bool has_logged_suggestions_shown_on_bnpl_eligible_merchant_ = false;
  // If true, the BNPL suggestion being shown was already logged and should not
  // be logged again.
  bool has_logged_bnpl_suggestion_shown_ = false;
  // If true, the metrics for a BNPL suggestion being accepted were already
  // logged and should not log again.
  bool has_logged_bnpl_suggestion_accepted_ = false;
  // If true, the metrics for a form filled with a BNPL issuer VCN were already
  // logged and should not log again.
  bool has_logged_form_filled_with_bnpl_vcn_ = false;
  // If true, the metrics for a form submitted with a BNPL issuer VCN were
  // already logged and should not log again.
  bool has_logged_form_submitted_with_bnpl_vcn_ = false;

  CardMetadataLoggingContext metadata_logging_context_;

  // Set when a list of suggestion is shown.
  base::TimeTicks suggestion_shown_timestamp_;

  AutofillMetrics::PaymentsSigninState signin_state_for_metrics_ =
      AutofillMetrics::PaymentsSigninState::kUnknown;

  // Weak references.
  raw_ptr<PersonalDataManager> personal_data_manager_;

  // Present only if a form was filled with a card.
  std::optional<CreditCard> filled_credit_card_;
};

}  // namespace autofill_metrics

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_METRICS_FORM_EVENTS_CREDIT_CARD_FORM_EVENT_LOGGER_H_
