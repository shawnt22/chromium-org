// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_AUTOFILL_AI_METRICS_AUTOFILL_AI_LOGGER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_AUTOFILL_AI_METRICS_AUTOFILL_AI_LOGGER_H_

#include <map>

#include "base/memory/raw_ref.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/integrators/autofill_ai/metrics/autofill_ai_ukm_logger.h"
#include "components/autofill/core/common/unique_ids.h"
#include "services/metrics/public/cpp/ukm_source_id.h"

namespace autofill {

// A class that takes care of keeping track of metric-related states and user
// interactions with forms.
class AutofillAiLogger {
 public:
  explicit AutofillAiLogger(AutofillClient* client);
  AutofillAiLogger(const AutofillAiLogger&) = delete;
  AutofillAiLogger& operator=(const AutofillAiLogger&) = delete;
  ~AutofillAiLogger();

  void OnFormEligibilityAvailable(FormGlobalId form_id, bool is_eligible);
  void OnFormHasDataToFill(FormGlobalId form_id);
  void OnSuggestionsShown(const FormStructure& form,
                          const AutofillField& field,
                          ukm::SourceId ukm_source_id);
  void OnDidFillSuggestion(const FormStructure& form,
                           const AutofillField& field,
                           ukm::SourceId ukm_source_id);
  void OnEditedAutofilledField(const FormStructure& form,
                               const AutofillField& field,
                               ukm::SourceId ukm_source_id);
  void OnDidFillField(const FormStructure& form,
                      const AutofillField& field,
                      ukm::SourceId ukm_source_id);

  // Function that records the contents of `form_states` for `form` into
  // appropriate metrics. `submission_state` denotes whether the form was
  // submitted or abandoned. Also logs form-related UKM metrics.
  void RecordFormMetrics(const FormStructure& form,
                         ukm::SourceId ukm_source_id,
                         bool submission_state,
                         bool opt_in_status);

 private:
  // Helper struct that contains relevant information about the state of a form
  // regarding the AutofillAi system.
  // TODO(crbug.com/372170223): Investigate whether this can be represented as
  // an enum.
  struct FunnelState {
    // Given a form, records whether it is supported for filling by prediction
    // improvements.
    bool is_eligible = false;
    // Given a form, records whether there's data available to fill this form.
    // Whether or not this data is used for filling is irrelevant.
    bool has_data_to_fill = false;
    // Given a form, records whether filling suggestions were actually shown
    // to the user.
    bool suggestions_shown = false;
    // Given a form, records whether the user chose to fill the form with a
    // filling suggestion.
    bool did_fill_suggestions = false;
    // Given a form, records whether the user corrected fields filled using
    // AutofillAi filling suggestions.
    bool edited_autofilled_field = false;
  };

  void RecordFunnelMetrics(const FunnelState& funnel_state,
                           bool submission_state) const;
  void RecordKeyMetrics(const FormStructure& form,
                        const FunnelState& funnel_state) const;
  void RecordNumberOfFieldsFilled(const FormStructure& form,
                                  const FunnelState& state,
                                  bool opt_in_status) const;

  // Records the funnel state of each form. See the documentation of
  // `FunnelState` for more information about what is recorded.
  std::map<FormGlobalId, FunnelState> form_states_;

  AutofillAiUkmLogger ukm_logger_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_AUTOFILL_AI_METRICS_AUTOFILL_AI_LOGGER_H_
