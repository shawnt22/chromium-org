// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_PLUS_ADDRESSES_MOCK_AUTOFILL_PLUS_ADDRESS_DELEGATE_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_PLUS_ADDRESSES_MOCK_AUTOFILL_PLUS_ADDRESS_DELEGATE_H_

#include "components/autofill/core/browser/integrators/password_form_classification.h"
#include "components/autofill/core/browser/integrators/plus_addresses/autofill_plus_address_delegate.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/autofill/core/common/form_field_data.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill {

class MockAutofillPlusAddressDelegate : public AutofillPlusAddressDelegate {
 public:
  MockAutofillPlusAddressDelegate();
  ~MockAutofillPlusAddressDelegate() override;

  MOCK_METHOD(bool, IsPlusAddress, (const std::string&), (const override));
  MOCK_METHOD(bool,
              MatchesPlusAddressFormat,
              (const std::u16string&),
              (const override));
  MOCK_METHOD(bool,
              IsPlusAddressFillingEnabled,
              (const url::Origin& origin),
              (const override));
  MOCK_METHOD(bool,
              IsFieldEligibleForPlusAddress,
              (const AutofillField& field),
              (const override));
  MOCK_METHOD(void,
              GetAffiliatedPlusAddresses,
              (const url::Origin& origin,
               base::OnceCallback<void(std::vector<std::string>)> callback),
              (override));
  MOCK_METHOD(std::vector<Suggestion>,
              GetSuggestionsFromPlusAddresses,
              (const std::vector<std::string>&,
               const url::Origin&,
               bool,
               const FormData&,
               const FormFieldData&,
               (const base::flat_map<FieldGlobalId, FieldTypeGroup>&),
               const PasswordFormClassification&,
               AutofillSuggestionTriggerSource),
              (override));
  MOCK_METHOD(Suggestion, GetManagePlusAddressSuggestion, (), (const override));
  MOCK_METHOD(void,
              RecordAutofillSuggestionEvent,
              (SuggestionEvent),
              (override));
  MOCK_METHOD(void,
              OnPlusAddressSuggestionShown,
              (AutofillManager&,
               FormGlobalId,
               FieldGlobalId,
               SuggestionContext,
               PasswordFormClassification::Type,
               SuggestionType),
              (override));
  MOCK_METHOD(void, DidFillPlusAddress, (), (override));
  MOCK_METHOD(size_t, GetPlusAddressesCount, (), (override));
  MOCK_METHOD(void,
              OnClickedRefreshInlineSuggestion,
              (const url::Origin&,
               base::span<const Suggestion>,
               size_t,
               base::OnceCallback<void(std::vector<Suggestion>,
                                       AutofillSuggestionTriggerSource)>),
              (override));
  MOCK_METHOD(void,
              OnShowedInlineSuggestion,
              (const url::Origin&,
               base::span<const Suggestion>,
               UpdateSuggestionsCallback),
              (override));
  MOCK_METHOD(void,
              OnAcceptedInlineSuggestion,
              (const url::Origin&,
               base::span<const Suggestion>,
               size_t,
               UpdateSuggestionsCallback,
               HideSuggestionsCallback,
               PlusAddressCallback,
               ShowAffiliationErrorDialogCallback,
               ShowErrorDialogCallback,
               base::OnceClosure),
              (override));
  MOCK_METHOD((std::map<std::string, std::string>),
              GetPlusAddressHatsData,
              (),
              (const, override));
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_PLUS_ADDRESSES_MOCK_AUTOFILL_PLUS_ADDRESS_DELEGATE_H_
