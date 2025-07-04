// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/metrics/field_filling_stats_and_score_metrics.h"

#include <string_view>

#include "base/test/metrics/histogram_tester.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/metrics/autofill_metrics_test_base.h"
#include "components/autofill/core/browser/test_utils/autofill_form_test_utils.h"
#include "components/autofill/core/browser/test_utils/autofill_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill::autofill_metrics {
namespace {

std::vector<test::FieldDescription> GetTestFormDataFields() {
  return {
      {.role = NAME_FULL, .value = u"First Middle Last", .is_autofilled = true},
      // Those two fields are going to be changed to a value of the
      // same type.
      {.role = NAME_FIRST, .value = u"First", .is_autofilled = true},
      {.role = NAME_LAST, .value = u"Last", .is_autofilled = true},
      // This field is going to be changed to a value of a different
      // type.
      {.role = NAME_FIRST, .value = u"First", .is_autofilled = true},
      // This field is going to be changed to another value of unknown
      // type.
      {.role = NAME_FIRST, .value = u"First", .is_autofilled = true},
      // This field is going to be changed to the empty value.
      {.role = NAME_MIDDLE, .value = u"Middle", .is_autofilled = true},
      // This field remains.
      {.role = NAME_LAST, .value = u"Last", .is_autofilled = true},
      // This following two fields are manually filled to a value of
      // type NAME_FIRST.
      {.role = NAME_FIRST, .value = u"Elvis", .is_autofilled = false},
      {.role = NAME_FIRST, .value = u"Elvis", .is_autofilled = false},
      // This one is manually filled to a value of type NAME_LAST.
      {.role = NAME_FIRST, .value = u"Presley", .is_autofilled = false},
      // This next three are manually filled to a value of
      // UNKNOWN_TYPE.
      {.role = NAME_FIRST, .value = u"Random Value", .is_autofilled = false},
      {.role = NAME_MIDDLE, .value = u"Random Value", .is_autofilled = false},
      {.role = NAME_LAST, .value = u"Random Value", .is_autofilled = false},
      {.role = ADDRESS_HOME_LINE1,
       .value = u"Erika-mann",
       .is_autofilled = true},
      {.role = ADDRESS_HOME_ZIP, .value = u"89173", .is_autofilled = true},
      {.role = ADDRESS_HOME_APT_NUM, .value = u"33", .is_autofilled = true},
      // The last field is not autofilled and empty.
      {.role = ADDRESS_HOME_CITY, .value = u"", .is_autofilled = false},
      // We add two credit cards field to make sure those are counted
      // in separate statistics.
      {.role = CREDIT_CARD_NAME_FULL,
       .value = u"Test Name",
       .is_autofilled = true},
      {.role = CREDIT_CARD_NUMBER, .value = u"", .is_autofilled = false}};
}

class AutofillFieldFillingStatsAndScoreMetricsTest
    : public AutofillMetricsBaseTest,
      public testing::Test {
 public:
  void SetUp() override { SetUpHelper(); }
  void TearDown() override { TearDownHelper(); }

  // Simulates user changes to the fields [1, 5] of `form_data_`. Used
  // to cover user correction metrics.
  void SimulationOfDefaultUserChangesOnAddedFormTextFields() {
    ASSERT_GT(form_data_.fields().size(), 6u);
    // Elvis is of type NAME_FIRST in the test profile.
    SimulateUserChangedFieldTo(form_data_, form_data_.fields()[1], u"Elvis");
    // Presley is of type NAME_LAST in the test profile
    SimulateUserChangedFieldTo(form_data_, form_data_.fields()[2], u"Presley");
    // Presley is of type NAME_LAST in the test profile
    SimulateUserChangedFieldTo(form_data_, form_data_.fields()[3], u"Presley");
    // This is a random string of UNKNOWN_TYPE.
    SimulateUserChangedFieldTo(form_data_, form_data_.fields()[4],
                               u"something random");
    SimulateUserChangedFieldTo(form_data_, form_data_.fields()[5], u"");
  }

  // Creates, adds and "sees" a form that contains `fields`.
  const FormData& GetAndAddSeenFormWithFields(
      const std::vector<test::FieldDescription>& fields) {
    form_data_ = GetAndAddSeenForm(
        {.description_for_logging = "FieldFillingStats",
         .fields = fields,
         .renderer_id = test::MakeFormRendererId(),
         .main_frame_origin = url::Origin::Create(autofill_driver_->url())});
    return form_data_;
  }

 private:
  // `FormData` initialized on `GetAndAddSeenFormWithFields()`. Used to simulate
  // a form submission.
  FormData form_data_;
};

// Test form-wise filling score for the different form types.
TEST_F(AutofillFieldFillingStatsAndScoreMetricsTest, FillingScores) {
  const FormData& form = GetAndAddSeenFormWithFields(GetTestFormDataFields());
  base::HistogramTester histogram_tester;
  SimulationOfDefaultUserChangesOnAddedFormTextFields();

  SubmitForm(form);

  // Testing of the FormFillingScore expectations.

  // The form contains a total of 7 autofilled address fields. Two fields are
  // accepted while 5 are corrected.
  const int accepted_address_fields = 5;
  const int corrected_address_fields = 5;

  const int expected_address_score =
      2 * accepted_address_fields - 3 * corrected_address_fields + 100;
  const int expected_address_complex_score =
      accepted_address_fields * 10 + corrected_address_fields;

  histogram_tester.ExpectUniqueSample("Autofill.FormFillingScore.Address",
                                      expected_address_score, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.FormFillingComplexScore.Address",
      expected_address_complex_score, 1);

  // Also test for credit cards where there is exactly one accepted field and
  // no corrected fields.
  histogram_tester.ExpectUniqueSample("Autofill.FormFillingScore.CreditCard",
                                      102, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.FormFillingComplexScore.CreditCard", 10, 1);
}

class AutocompleteUnrecognizedFieldFillingStatsTest
    : public AutofillFieldFillingStatsAndScoreMetricsTest {};

TEST_F(AutocompleteUnrecognizedFieldFillingStatsTest, FieldFillingStats) {
  FormData form = GetAndAddSeenForm(
      {.fields = {
           {.role = NAME_FIRST,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = NAME_MIDDLE,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = NAME_LAST,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = ADDRESS_HOME_COUNTRY,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = ADDRESS_HOME_STREET_NAME,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = ADDRESS_HOME_HOUSE_NUMBER,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = ADDRESS_HOME_CITY,
            .autocomplete_attribute = "unrecognized",
            .is_autofilled = true},
           {.role = ADDRESS_HOME_ZIP, .autocomplete_attribute = "unrecognized"},
           {.role = PHONE_HOME_WHOLE_NUMBER,
            .autocomplete_attribute = "unrecognized"},
           {.role = EMAIL_ADDRESS, .autocomplete_attribute = "unrecognized"}}});

  SimulateUserChangedFieldTo(form, form.fields()[0], u"Corrected First Name");
  SimulateUserChangedFieldTo(form, form.fields()[1], u"Corrected Middle Name");
  SimulateUserChangedFieldTo(form, form.fields()[2], u"Corrected Last Name");
  SimulateUserChangedFieldTo(form, form.fields()[8], u"Manually Filled Phone");
  SimulateUserChangedFieldTo(form, form.fields()[9], u"Manually Filled Email");

  base::HistogramTester histogram_tester;
  SubmitForm(form);
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.AutocompleteUnrecognized.FieldFillingStats2"),
      base::BucketsAre(
          base::Bucket(FieldFillingStatus::kAccepted, 4),
          base::Bucket(FieldFillingStatus::kCorrectedToUnknownType, 3),
          base::Bucket(FieldFillingStatus::kManuallyFilledToUnknownType, 2),
          base::Bucket(FieldFillingStatus::kLeftEmpty, 1)));
}

}  // namespace
}  // namespace autofill::autofill_metrics
