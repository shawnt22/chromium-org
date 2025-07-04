// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/metrics/payments/card_metadata_metrics.h"

#include "base/test/gmock_callback_support.h"
#include "base/test/metrics/histogram_tester.h"
#include "components/autofill/core/browser/data_manager/payments/payments_data_manager.h"
#include "components/autofill/core/browser/data_model/payments/credit_card_benefit_test_api.h"
#include "components/autofill/core/browser/metrics/autofill_metrics_test_base.h"
#include "components/autofill/core/browser/payments/constants.h"
#include "components/autofill/core/browser/test_utils/autofill_test_utils.h"
#include "components/autofill/core/common/credit_card_network_identifiers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::base::Bucket;

namespace autofill::autofill_metrics {
namespace {

constexpr char kCardGuid[] = "10000000-0000-0000-0000-000000000001";

// Params:
// 1. Whether card metadata is available.
// 2. Whether card has a static card art image (instead of the rich card art
// from metadata).
// 3. Whether a larger-sized card art image is used.
// 4. Unique identifiers for the issuer of the card.
class CardMetadataFormEventMetricsTest
    : public AutofillMetricsBaseTest,
      public testing::Test,
      public testing::WithParamInterface<
          std::tuple<bool, bool, bool, std::string>> {
 public:
  CardMetadataFormEventMetricsTest() = default;
  ~CardMetadataFormEventMetricsTest() override = default;

  bool registered_card_issuer_available() { return issuer_id() != "Dummy"; }
  bool card_metadata_available() const { return std::get<0>(GetParam()); }
  bool card_has_static_art_image() const { return std::get<1>(GetParam()); }
  bool new_card_art_and_network_images_used() const {
    return std::get<2>(GetParam());
  }

  const std::string& issuer_id() const { return std::get<3>(GetParam()); }

  FormData form() { return form_; }
  const CreditCard& card() const { return card_; }

  void SetUp() override {
    SetUpHelper();
    // Set up the form data. Reset form action to skip the IsFormMixedContent
    // check.
    form_ =
        GetAndAddSeenForm({.description_for_logging = "CardMetadata",
                           .fields = {{.role = CREDIT_CARD_NAME_FULL},
                                      {.role = CREDIT_CARD_NUMBER},
                                      {.role = CREDIT_CARD_EXP_MONTH},
                                      {.role = CREDIT_CARD_EXP_2_DIGIT_YEAR}},
                           .action = ""});

    // Add a Mastercard masked server card.
    card_ = test::WithCvc(test::GetMaskedServerCard());
    card_.SetNetworkForMaskedCard(autofill::kMasterCard);
    card_.set_guid(kCardGuid);
    card_.set_issuer_id(issuer_id());
    if (issuer_id() == kCapitalOneCardIssuerId && card_has_static_art_image()) {
      if (new_card_art_and_network_images_used()) {
        card_.set_card_art_url(GURL(kCapitalOneLargeCardArtUrl));
      } else {
        card_.set_card_art_url(GURL(kCapitalOneCardArtUrl));
      }
    }

    // Set metadata to card. The `card_art_url` will be overridden with rich
    // card art url regardless of `card_has_static_art_image()` in the test
    // set-up, because rich card art, if available, is preferred by Payments
    // server and will be sent to the client.
    if (card_metadata_available()) {
      card_.set_product_description(u"card_description");
      card_.set_card_art_url(GURL("https://www.example.com/cardart.png"));
    }

    personal_data().test_payments_data_manager().AddServerCreditCard(card_);
  }

  void TearDown() override { TearDownHelper(); }

  std::string GetHistogramName(const std::string& issuer_or_network,
                               std::string_view event) {
    return base::StrCat({"Autofill.CreditCard.",
                         GetCardIssuerIdOrNetworkSuffix(issuer_or_network) != ""
                             ? GetCardIssuerIdOrNetworkSuffix(issuer_or_network)
                             : issuer_or_network,
                         event});
  }

 private:
  CreditCard card_;
  FormData form_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         CardMetadataFormEventMetricsTest,
                         testing::Combine(testing::Bool(),
                                          testing::Bool(),
                                          testing::Bool(),
                                          testing::Values("amex",
                                                          "anz",
                                                          "capitalone",
                                                          "chase",
                                                          "citi",
                                                          "discover",
                                                          "lloyds",
                                                          "marqeta",
                                                          "nab",
                                                          "natwest",
                                                          "Dummy")));

// Test metadata shown metrics are correctly logged.
TEST_P(CardMetadataFormEventMetricsTest, LogShownMetrics) {
  base::HistogramTester histogram_tester;

  // Simulate activating the autofill popup for the credit card field.
  autofill_manager().OnAskForValuesToFillTest(
      form(), form().fields().back().global_id());
  DidShowAutofillSuggestions(form(), /*field_index=*/form().fields().size() - 1,
                             SuggestionType::kCreditCardEntry);

  // Verify that:
  // 1. if the card suggestion shown had metadata,
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SHOWN` is logged as many times as
  // the suggestions are shown, and
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SHOWN_ONCE` is logged only once.
  // 2.  if the card suggestion shown did not have metadata,
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SHOWN` is logged as many times
  // as the suggestions are shown, and
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SHOWN_ONCE` is logged only
  // once.
  // 3. if the card suggestion shown had a registered issuer id, two histograms
  // are logged which tell if the card from the issuer had metadata.
  // 4. For cards with issuer ids that are not registered, no issuer-specific
  // metadata metrics are logged.
  EXPECT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_SUGGESTIONS_SHOWN, 1),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SHOWN,
                         card_metadata_available() ? 1 : 0),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SHOWN,
                         card_metadata_available() ? 0 : 1),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SHOWN_ONCE,
                         card_metadata_available() ? 1 : 0),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SHOWN_ONCE,
                         card_metadata_available() ? 0 : 1)));

  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".ShownWithMetadata"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".ShownWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".ShownWithMetadata"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".ShownWithMetadataOnce"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample("Autofill.CreditCard..ShownWithMetadata",
                                      card_metadata_available(), 0);

  // Show the popup again.
  autofill_manager().OnAskForValuesToFillTest(
      form(), form().fields().back().global_id());
  DidShowAutofillSuggestions(form(), /*field_index=*/form().fields().size() - 1,
                             SuggestionType::kCreditCardEntry);

  EXPECT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_SUGGESTIONS_SHOWN, 2),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SHOWN,
                         card_metadata_available() ? 2 : 0),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SHOWN,
                         card_metadata_available() ? 0 : 2),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SHOWN_ONCE,
                         card_metadata_available() ? 1 : 0),
                  Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SHOWN_ONCE,
                         card_metadata_available() ? 0 : 1)));
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".ShownWithMetadata"),
      card_metadata_available(), registered_card_issuer_available() ? 2 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".ShownWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".ShownWithMetadata"),
      card_metadata_available(), 2);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".ShownWithMetadataOnce"),
      card_metadata_available(), 1);
}

// Test metadata selected metrics are correctly logged.
TEST_P(CardMetadataFormEventMetricsTest, LogSelectedMetrics) {
  // Add a second card which won't be selected but will be logged in
  // Autofill.CreditCard.Amex.SelectedWithIssuerMetadataPresentOnce.
  CreditCard card2 = test::GetMaskedServerCard2();
  card2.set_guid(kTestMaskedCardId);
  card2.set_issuer_id("amex");
  if (card_metadata_available()) {
    card2.set_product_description(u"product description");
    card2.set_card_art_url(GURL("https://www.example.com/cardarturl.png"));
  }
  personal_data().test_payments_data_manager().AddServerCreditCard(card2);

  base::HistogramTester histogram_tester;

  // Simulate selecting the card.
  autofill_manager().OnAskForValuesToFillTest(
      form(), form().fields().back().global_id());
  DidShowAutofillSuggestions(form(), /*field_index=*/form().fields().size() - 1,
                             SuggestionType::kCreditCardEntry);
  autofill_manager().FillOrPreviewForm(
      mojom::ActionPersistence::kFill, form(),
      form().fields().back().global_id(),
      personal_data().payments_data_manager().GetCreditCardByGUID(kCardGuid),
      AutofillTriggerSource::kPopup);

  // Verify that:
  // 1. if the card suggestion selected had metadata,
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SELECTED` is logged as many times
  // as the suggestions are selected, and
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SELECTED_ONCE` is logged only
  // once.
  // 2. if the card suggestion selected did not have metadata,
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SELECTED` is logged as many
  // times as the suggestions are selected, and
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SELECTED_ONCE` is logged only
  // once.
  // 3. if the card suggestion selected had a registered issuer id, two
  // histograms are logged which tell if the card from the issuer had metadata.
  // 4. For cards with issuer ids that are not registered, no issuer-specific
  // metadata metrics are logged.
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 1),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SELECTED,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SELECTED,
                 card_metadata_available() ? 0 : 1),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SELECTED_ONCE,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SELECTED_ONCE,
                 card_metadata_available() ? 0 : 1)));
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".SelectedWithMetadata"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".SelectedWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".SelectedWithMetadata"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".SelectedWithMetadataOnce"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CreditCard..SelectedWithMetadata", card_metadata_available(),
      0);

  // Select the suggestion again.
  autofill_manager().FillOrPreviewForm(
      mojom::ActionPersistence::kFill, form(),
      form().fields().back().global_id(),
      personal_data().payments_data_manager().GetCreditCardByGUID(kCardGuid),
      AutofillTriggerSource::kPopup);

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 2),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SELECTED,
                 card_metadata_available() ? 2 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SELECTED,
                 card_metadata_available() ? 0 : 2),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SELECTED_ONCE,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SELECTED_ONCE,
                 card_metadata_available() ? 0 : 1)));
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".SelectedWithMetadata"),
      card_metadata_available(), registered_card_issuer_available() ? 2 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".SelectedWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".SelectedWithMetadata"),
      card_metadata_available(), 2);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".SelectedWithMetadataOnce"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(),
                       ".SelectedWithIssuerMetadataPresentOnce"),
      true,
      card_metadata_available() && registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".SelectedWithIssuerMetadataPresentOnce"),
      true, card_metadata_available() ? 1 : 0);

  // Only test non-Amex because for Amex case it will log true in
  // SelectedWithIssuerMetadataPresentOnce histogram.
  if (issuer_id() != "amex") {
    histogram_tester.ExpectUniqueSample(
        "Autofill.CreditCard.Amex.SelectedWithIssuerMetadataPresentOnce", false,
        card_metadata_available() ? 1 : 0);
  }
}

// Test metadata filled metrics are correctly logged.
TEST_P(CardMetadataFormEventMetricsTest, LogFilledMetrics) {
  base::HistogramTester histogram_tester;

  // Simulate filling the card.
  autofill_manager().OnAskForValuesToFillTest(
      form(), form().fields().back().global_id());
  DidShowAutofillSuggestions(form(), /*field_index=*/form().fields().size() - 1,
                             SuggestionType::kCreditCardEntry);
  EXPECT_CALL(credit_card_access_manager(), FetchCreditCard)
      .WillOnce(base::test::RunOnceCallback<1>(card()));
  autofill_manager().FillOrPreviewForm(
      mojom::ActionPersistence::kFill, form(),
      form().fields().back().global_id(),
      personal_data().payments_data_manager().GetCreditCardByGUID(kCardGuid),
      AutofillTriggerSource::kPopup);

  // Verify that:
  // 1. if the card suggestion filled had metadata,
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_FILLED` is logged as many times
  // as the suggestions are filled, and
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_FILLED_ONCE` is logged only
  // once.
  // 2. if the card suggestion filled did not have metadata,
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_FILLED` is logged as many
  // times as the suggestions are filled, and
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_FILLED_ONCE` is logged only
  // once.
  // 3. if the card suggestion filled had a registered issuer id, two histograms
  // are logged which tell if the card from the issuer had metadata.
  // 4. For cards with issuer ids that are not registered, no issuer-specific
  // metadata metrics are logged.
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 1),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_FILLED,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_FILLED,
                 card_metadata_available() ? 0 : 1),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_FILLED_ONCE,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_FILLED_ONCE,
                 card_metadata_available() ? 0 : 1)));
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".FilledWithMetadata"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".FilledWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".FilledWithMetadata"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".FilledWithMetadataOnce"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample("Autofill.CreditCard..FilledWithMetadata",
                                      card_metadata_available(), 0);

  // Fill the suggestion again.
  EXPECT_CALL(credit_card_access_manager(), FetchCreditCard)
      .WillOnce(base::test::RunOnceCallback<1>(card()));
  autofill_manager().FillOrPreviewForm(
      mojom::ActionPersistence::kFill, form(),
      form().fields().back().global_id(),
      personal_data().payments_data_manager().GetCreditCardByGUID(kCardGuid),
      AutofillTriggerSource::kPopup);

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 2),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_FILLED,
                 card_metadata_available() ? 2 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_FILLED,
                 card_metadata_available() ? 0 : 2),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_FILLED_ONCE,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_FILLED_ONCE,
                 card_metadata_available() ? 0 : 1)));
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".FilledWithMetadata"),
      card_metadata_available(), registered_card_issuer_available() ? 2 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".FilledWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".FilledWithMetadata"),
      card_metadata_available(), 2);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".FilledWithMetadataOnce"),
      card_metadata_available(), 1);
}

// Test metadata will submit and submitted metrics are correctly logged.
TEST_P(CardMetadataFormEventMetricsTest, LogSubmitMetrics) {
  base::HistogramTester histogram_tester;

  // Simulate filling and then submitting the card.
  autofill_manager().OnAskForValuesToFillTest(
      form(), form().fields().back().global_id());
  EXPECT_CALL(credit_card_access_manager(), FetchCreditCard)
      .WillOnce(base::test::RunOnceCallback<1>(card()));
  autofill_manager().FillOrPreviewForm(
      mojom::ActionPersistence::kFill, form(),
      form().fields().back().global_id(),
      personal_data().payments_data_manager().GetCreditCardByGUID(kCardGuid),
      AutofillTriggerSource::kPopup);
  SubmitForm(form());

  // Verify that:
  // 1. if the form was submitted after a card suggestion with metadata was
  // filled, `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_WILL_SUBMIT_ONCE` and
  // `FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SUBMITTED_ONCE` are logged.
  // 2. if the form was submitted after a card suggestion without metadata was
  // filled, `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_WILL_SUBMIT_ONCE` and
  // `FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SUBMITTED_ONCE` are logged.
  // 3. if the form was submitted after a card suggestion with a registered
  // issuer id was filled, two histograms are logged which tell if the card from
  // the issuer had metadata.
  // 4. For cards with issuer ids that are not registered, no issuer-specific
  // metadata metrics are logged.
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_WILL_SUBMIT_ONCE, 1),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_WILL_SUBMIT_ONCE,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_WILL_SUBMIT_ONCE,
                 card_metadata_available() ? 0 : 1),
          Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SUBMITTED_ONCE, 1),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITH_METADATA_SUBMITTED_ONCE,
                 card_metadata_available() ? 1 : 0),
          Bucket(FORM_EVENT_CARD_SUGGESTION_WITHOUT_METADATA_SUBMITTED_ONCE,
                 card_metadata_available() ? 0 : 1)));
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".WillSubmitWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(card().issuer_id(), ".SubmittedWithMetadataOnce"),
      card_metadata_available(), registered_card_issuer_available() ? 1 : 0);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".WillSubmitWithMetadataOnce"),
      card_metadata_available(), 1);
  histogram_tester.ExpectUniqueSample(
      GetHistogramName(kMasterCard, ".SubmittedWithMetadataOnce"),
      card_metadata_available(), 1);
}

// Params:
// 1) Whether card metadata (both product name and card art image) are provided.
// 2) Whether the card has linked virtual card (only card art is provided).
class CardMetadataLatencyMetricsTest
    : public AutofillMetricsBaseTest,
      public testing::Test,
      public testing::WithParamInterface<std::tuple<bool, bool>> {
 public:
  CardMetadataLatencyMetricsTest() = default;
  ~CardMetadataLatencyMetricsTest() override = default;

  bool card_metadata_available() { return std::get<0>(GetParam()); }
  bool card_has_static_art_image() { return std::get<1>(GetParam()); }

  FormData form() { return form_; }

  void SetUp() override {
    SetUpHelper();
    // Set up the form data. Reset form action to skip the IsFormMixedContent
    // check.
    form_ =
        GetAndAddSeenForm({.description_for_logging = "CardMetadata",
                           .fields = {{.role = CREDIT_CARD_NAME_FULL},
                                      {.role = CREDIT_CARD_NUMBER},
                                      {.role = CREDIT_CARD_EXP_MONTH},
                                      {.role = CREDIT_CARD_EXP_2_DIGIT_YEAR}},
                           .action = ""});

    CreditCard masked_server_card = test::GetMaskedServerCard();
    masked_server_card.SetNetworkForMaskedCard(kMasterCard);
    masked_server_card.set_guid(kTestMaskedCardId);
    masked_server_card.set_issuer_id(kCapitalOneCardIssuerId);
    if (card_has_static_art_image()) {
      masked_server_card.set_card_art_url(GURL(kCapitalOneCardArtUrl));
    }
    // If metadata is available, the `card_art_url` will be overridden with rich
    // card art url regardless of `card_has_static_art_image()` in the test
    // set-up, because rich card art, if available, is preferred by Payments
    // server and will be sent to the client.
    if (card_metadata_available()) {
      masked_server_card.set_product_description(u"card_description");
      masked_server_card.set_card_art_url(
          GURL("https://www.example.com/cardart.png"));
    }
    personal_data().test_payments_data_manager().AddServerCreditCard(
        masked_server_card);
  }

  void TearDown() override { TearDownHelper(); }

 private:
  base::test::ScopedFeatureList feature_list_card_product_name_;
  FormData form_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         CardMetadataLatencyMetricsTest,
                         testing::Combine(testing::Bool(),
                                          testing::Bool()));

// Test to ensure that we log card metadata related metrics only when card
// metadata is available.
TEST_P(CardMetadataLatencyMetricsTest, LogMetrics) {
  base::HistogramTester histogram_tester;

  // Simulate activating the autofill popup for the credit card field.
  autofill_manager().OnAskForValuesToFillTest(
      form(), form().fields().back().global_id());
  DidShowAutofillSuggestions(form(), /*field_index=*/form().fields().size() - 1,
                             SuggestionType::kCreditCardEntry);
  task_environment_.FastForwardBy(base::Seconds(2));
  autofill_manager().FillOrPreviewForm(
      mojom::ActionPersistence::kFill, form(),
      form().fields().front().global_id(),
      personal_data().payments_data_manager().GetCreditCardByGUID(
          kTestMaskedCardId),
      AutofillTriggerSource::kPopup);

  std::string latency_histogram_prefix =
      "Autofill.CreditCard.SelectionLatencySinceShown.";

  std::string latency_histogram_suffix;
  // Card product name is shown when card_metadata_available() return true.
  // Card art image is shown when 1. card_has_linked_virtual_card() or
  // 2. card_metadata_available() returns true.
  // TODO(crbug.com/387391138): Determine appropriate cases and modify test
  // coverage accordingly.
  if (card_metadata_available()) {
    latency_histogram_suffix = kProductNameAndArtImageBothShownSuffix;
  } else {
    latency_histogram_suffix = kProductNameAndArtImageNotShownSuffix;
  }
  histogram_tester.ExpectUniqueSample(
      latency_histogram_prefix + latency_histogram_suffix, 2000, 1);
  histogram_tester.ExpectUniqueSample(
      base::StrCat(
          {latency_histogram_prefix,
           "CardWithIssuerId." + latency_histogram_suffix + ".CapitalOne"}),
      2000, 1);
  histogram_tester.ExpectUniqueSample(
      base::StrCat(
          {latency_histogram_prefix,
           "CardWithIssuerId." + latency_histogram_suffix + ".Mastercard"}),
      2000, 1);
}

// Skip metrics test for card benefits on Android and iOS, since currently
// benefit is only suppoerted on desktop.
// TODO(crbug.com/332559112): Remove the platform check after Android and iOS
// are supported.
// TODO(crbug.com/346399130): Reduce the amount of '_ONCE' metric tests.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
// Params:
// 1. Whether card benefit feature flag is enabled.
// 2. Whether card benefit source sync feature flag is enabled.
// 3. Issuer id of the card with a benefit available.
// 4. Benefit source of the card with a benefit available.
class CardBenefitFormEventMetricsTest
    : public AutofillMetricsBaseTest,
      public testing::TestWithParam<
          std::tuple<bool, bool, std::string_view, std::string_view>> {
 public:
  CardBenefitFormEventMetricsTest() = default;
  ~CardBenefitFormEventMetricsTest() override = default;

  // Adding a benefit for the card on client.
  void AddBenefitToCard(CreditCard& card) {
    card.set_product_terms_url(GURL("https://www.example.com/term"));
    CreditCardBenefit benefit = test::GetActiveCreditCardFlatRateBenefit();
    test_api(benefit).SetLinkedCardInstrumentId(
        CreditCardBenefitBase::LinkedCardInstrumentId(card.instrument_id()));
    personal_data().payments_data_manager().AddCreditCardBenefitForTest(
        benefit);
  }

  // Adding a local card to the client.
  void AddLocalCard() {
    CreditCard local_card = test::GetCreditCard();
    local_card_guid_ = local_card.guid();
    personal_data().payments_data_manager().AddCreditCard(local_card);
  }

  // Adding an additional card from the same benefit source or issuer with
  // benefit available.
  void AddAdditionalCardWithBenefit() {
    CreditCard card = test::GetMaskedServerCard2();
    if (is_card_benefits_source_sync_enabled()) {
      card_.set_benefit_source(benefit_source());
    } else {
      card_.set_issuer_id(issuer_id());
    }
    personal_data().test_payments_data_manager().AddServerCreditCard(card);

    AddBenefitToCard(card);
  }

  // Simulate showing card suggestinos.
  void ShowCardSuggestions() {
    autofill_manager().OnAskForValuesToFillTest(
        form(), form().fields()[credit_card_number_field_index()].global_id());
    DidShowAutofillSuggestions(form(), credit_card_number_field_index(),
                               SuggestionType::kCreditCardEntry);
  }

  // Simulate selecting a card from a list of suggestions.
  void ShowSuggestionsAndSelectCard(const CreditCard* card) {
    ShowCardSuggestions();
    autofill_manager().FillOrPreviewForm(
        mojom::ActionPersistence::kFill, form(),
        form().fields()[credit_card_number_field_index()].global_id(), card,
        AutofillTriggerSource::kPopup);
  }

  // Simulating selecting and filling the given `card` from a list of
  // suggestions.
  void ShowSuggestionsThenSelectAndFillCard(const CreditCard* card) {
    EXPECT_CALL(credit_card_access_manager(), FetchCreditCard)
        .WillOnce(base::test::RunOnceCallback<1>(*card));
    ShowCardSuggestions();
    autofill_manager().FillOrPreviewForm(
        mojom::ActionPersistence::kFill, form(),
        form().fields()[credit_card_number_field_index()].global_id(), card,
        AutofillTriggerSource::kPopup);
  }

  const CreditCard* GetCreditCard() {
    return personal_data().payments_data_manager().GetCreditCardByInstrumentId(
        card_.instrument_id());
  }

  void SetUp() override {
    SetUpHelper();
    // Set up the form data. Reset form action to skip the IsFormMixedContent
    // check.
    form_ =
        GetAndAddSeenForm({.description_for_logging = "CardBenefit",
                           .fields = {{.role = CREDIT_CARD_NAME_FULL},
                                      {.role = CREDIT_CARD_NUMBER},
                                      {.role = CREDIT_CARD_EXP_MONTH},
                                      {.role = CREDIT_CARD_EXP_2_DIGIT_YEAR}},
                           .action = ""});
    credit_card_number_field_index_ = 1;

    // Add a masked server card.
    card_ = test::GetMaskedServerCard();
    if (is_card_benefits_source_sync_enabled()) {
      card_.set_benefit_source(benefit_source());
    } else {
      card_.set_issuer_id(issuer_id());
    }
    personal_data().test_payments_data_manager().AddServerCreditCard(card_);

    // Initialize features based on test params.
    scoped_feature_list_.InitWithFeatureStates(
        /*feature_states=*/
        {{features::kAutofillEnableCardBenefitsSync, true},
         {features::kAutofillEnableCardBenefitsSourceSync,
          is_card_benefits_source_sync_enabled()},
         {features::kAutofillEnableCardBenefitsForAmericanExpress,
          card_benefits_are_enabled()},
         {features::kAutofillEnableCardBenefitsForBmo,
          card_benefits_are_enabled()},
         {features::kAutofillEnableFlatRateCardBenefitsFromCurinos,
          card_benefits_are_enabled()}});
  }

  void TearDown() override { TearDownHelper(); }

  // Return whether the benefit feature flag is enabled.
  bool card_benefits_are_enabled() const { return std::get<0>(GetParam()); }
  // Returns whether the benefit source sync feature flag is enabled.
  bool is_card_benefits_source_sync_enabled() const {
    return std::get<1>(GetParam());
  }
  // Return the issuer id of the card saved on the client.
  std::string_view issuer_id() const { return std::get<2>(GetParam()); }
  // Return the benefit source of the card saved on the client.
  std::string_view benefit_source() const { return std::get<3>(GetParam()); }

  const FormData& form() const { return form_; }
  CreditCard& card() { return card_; }
  const std::string& local_card_guid() const { return local_card_guid_; }
  int credit_card_number_field_index() const {
    return credit_card_number_field_index_;
  }

  // Returns the histogram name for benefit source or issuer specific form
  // events.
  const std::string GetCardBenefitFormEventHistogram() const {
    if (is_card_benefits_source_sync_enabled()) {
      return base::StrCat({"Autofill.FormEvents.CreditCard.WithBenefits.",
                           GetCardBenefitSourceSuffix(card_.benefit_source())});
    } else {
      return base::StrCat({"Autofill.FormEvents.CreditCard.WithBenefits.",
                           GetCardIssuerIdOrNetworkSuffix(card_.issuer_id())});
    }
  }

  // Returns the benefit source, issuer id, or network suffix for benefit
  // source, issuer id, or network specific form events.
  const std::string_view GetSuffix() const {
    if (is_card_benefits_source_sync_enabled()) {
      return GetCardBenefitSourceSuffix(card_.benefit_source());
    }
    return GetCardIssuerIdOrNetworkSuffix(card_.issuer_id());
  }

 private:
  int credit_card_number_field_index_;
  CreditCard card_;
  std::string local_card_guid_;
  FormData form_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/,
    CardBenefitFormEventMetricsTest,
    testing::Combine(testing::Bool(),
                     testing::Bool(),
                     testing::Values(kAmexCardIssuerId, kBmoCardIssuerId),
                     testing::Values(kAmexCardBenefitSource,
                                     kBmoCardBenefitSource,
                                     kCurinosCardBenefitSource)),
    [](auto& info) {
      return base::StrCat({std::get<0>(info.param) ? "BenefitFeatureEnabled_"
                                                   : "BenefitFeatureDisabled_",
                           std::get<1>(info.param)
                               ? "BenefitSourceSyncFeatureEnabled_"
                               : "BenefitSourceSyncFeatureDisabled_",
                           std::get<2>(info.param), std::get<3>(info.param)});
    });

// =============================
//    Benefits metrics: Shown
// =============================

// Tests that when the card suggestion shown had a benefit available,
// `FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN` is logged as
// many times as the suggestions are shown, and
// `FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE` is
// logged only once.
TEST_P(CardBenefitFormEventMetricsTest, LogShownMetrics_SuggestionHasBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();
  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(FORM_EVENT_SUGGESTIONS_SHOWN, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN,
                 1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));

  // Show the popup again.
  ShowCardSuggestions();

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(FORM_EVENT_SUGGESTIONS_SHOWN, 2)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN,
                 2),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when we have multiple cards with benefits that share the same
// issuer or benefit source, we only log
// FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE once for all
// cards in that benefit source's benefits histogram.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogShownMetrics_BenefitHistogram_MultipleSuggestionsWithSameBenefitSourceHaveBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  AddAdditionalCardWithBenefit();

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();
  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsAre(Bucket(
          FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
          1)));
}

// Tests that when the card suggestion shown did not have any benefit available,
// shown metrics for card benefit won't be logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogShownMetrics_NoSuggestionsWithBenefits) {
  base::HistogramTester histogram_tester;

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(FORM_EVENT_SUGGESTIONS_SHOWN, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN,
                 0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              0)));
}

// Tests that when we have one server card with a benefit available and a local
// card, when we select the server card with a benefit available, we don't log
// `kSuggestionWithBenefitShownWithMultipleServerCards`.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_OneServerCardWithBenefitAndOneLocalCard_DoesNotLogSuggestionWithBenefitShownWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with a benefit available.
  AddBenefitToCard(card());

  // Add local card.
  AddLocalCard();

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      0);
}

// Tests that when we have multiple server cards with one having benefits
// available, we only log `kSuggestionWithBenefitShownWithMultipleServerCards`
// once when the suggestions are shown twice.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithOneBenefitAvailable_LogSuggestionWithBenefitShownWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with benefit available.
  AddBenefitToCard(card());

  // Add a server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      1);

  // Show the popup again.
  ShowCardSuggestions();

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      1);
}

// Tests that when we have multiple server cards with benefits available that
// share the same benefit source or issuer id, we only log
// `kSuggestionWithBenefitShownWithMultipleServerCards` once to the benefit
// subhistogram.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithSameBenefitSourceOrIssuerId_LogSuggestionWithBenefitShownWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with benefit available.
  AddBenefitToCard(card());

  // Add another server card with the same benefit source or issuer id as the
  // first server card.
  AddAdditionalCardWithBenefit();

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();

  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      1);
}

// Tests that when we have multiple server cards without any benefits available,
// shown metrics for card benefits will not be logged.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithoutBenefitsAvailable_DoesNotLogAnyShownMetrics) {
  base::HistogramTester histogram_tester;

  // Add a server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard());

  // Add another server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate activating the autofill popup for the credit card field.
  ShowCardSuggestions();

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitShownWithMultipleServerCards,
      0);
}

// =============================
//    Benefits metrics: Selected
// =============================

// Tests that when a masked server card with a benefit is selected after card
// suggestions containing a benefit were shown,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED` is
// logged as many times as the suggestions are selected, and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE`
// and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// are logged only once.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSelectedMetrics_SuggestionHasBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  // Simulate selecting the card.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));

  // Select the suggestion again.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 2)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED,
              2),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when a masked server card with a benefit is selected from a list
// of card suggestions containing a benefit from the same issuer or benefit
// source,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE`
// and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// are logged only once in the issuer or benefit source specific histogram for
// card benefits.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogSelectedMetrics_BenefitHistogram_MultipleSuggestionsWithSameBenefitSourceHaveBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());
  AddAdditionalCardWithBenefit();

  // Simulate selecting the card.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));

  // Select the suggestion again.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when a masked server card with no benefit is selected from a
// list of suggestions containing both cards with benefits and cards without
// benefits,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE`
// is not logged and
// 'FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE'
// is logged for the issuer or benefit source with benefits available.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogSelectedMetrics_BenefitHistogram_SelectedNoBenefits_OtherSuggestionHasBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  CreditCard second_card = test::GetMaskedServerCard2();
  personal_data().test_payments_data_manager().AddServerCreditCard(second_card);

  // Simulate selecting the card with no benefit.
  ShowSuggestionsAndSelectCard(&second_card);

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when the shown masked server card suggestions do not have any
// entries with benefits, that when a suggestion is selected, selected metrics
// for card benefit won't be logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSelectedMetrics_NoSuggestionsWithBenefits) {
  base::HistogramTester histogram_tester;

  // Simulate selecting the card.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SELECTED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              0)));
}

// Tests that when a masked server card with no benefit is selected from a
// list of suggestions containing both cards with benefits and cards without
// benefits,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// is logged only once.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSelectedMetrics_SelectedNoBenefits_OtherSuggestionHasBenefits) {
  AddBenefitToCard(card());

  // Add a second card which has no benefit available.
  CreditCard card2 = test::GetMaskedServerCard2();
  personal_data().test_payments_data_manager().AddServerCreditCard(card2);

  base::HistogramTester histogram_tester;

  // Simulate selecting the card with no benefit.
  ShowSuggestionsAndSelectCard(&card2);

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
          1)));

  // Select the card again.
  ShowSuggestionsAndSelectCard(&card2);

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SELECTED, 2)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SELECTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
          1)));
}

// Tests that when we have one server card with a benefit available and a local
// card, when we select the server card with a benefit available, we don't log
// `kSuggestionWithBenefitSelectedWithMultipleServerCards`.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_OneServerCardWithBenefitAndOneLocalCard_DoesNotLogSuggestionWithBenefitSelectedWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with a benefit available.
  AddBenefitToCard(card());

  // Add local card.
  CreditCard local_card = test::GetCreditCard();
  personal_data().payments_data_manager().AddCreditCard(local_card);

  // Simulate selecting the server card with a benefit available.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      0);
}

// Tests that when we have multiple server cards with one having benefits
// available, we log `kSuggestionWithBenefitSelectedWithMultipleServerCards`
// once when the server card with benefits available is selected twice.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithOneBenefitAvailable_LogSuggestionWithBenefitSelectedWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with benefit available.
  AddBenefitToCard(card());

  // Add a server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate selecting the server card with a benefit available.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      1);

  // Select the suggestion again.
  ShowSuggestionsAndSelectCard(GetCreditCard());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      1);
}

// Tests that when we have multiple server cards without any benefits available,
// after selecting any server card,
// `kSuggestionWithBenefitSelectedWithMultipleServerCards` will not be logged.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithoutBenefitsAvailable_DoesNotLogSuggestionWithBenefitSelectedWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add a server card without a benefit available.
  CreditCard server_card_1 = test::GetMaskedServerCard();
  personal_data().test_payments_data_manager().AddServerCreditCard(
      server_card_1);

  // Add another server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate selecting a server card without a benefit available.
  ShowSuggestionsAndSelectCard(
      personal_data().payments_data_manager().GetCreditCardByGUID(
          server_card_1.guid()));

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSelectedWithMultipleServerCards,
      0);
}

// =============================
//    Benefits metrics: Filled
// =============================

// Tests that when a masked server card with a benefit is filled after card
// suggestions containing a benefit were shown,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED` is
// logged as many times as the suggestions are filled, and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE`
// and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// are logged only once.
TEST_P(CardBenefitFormEventMetricsTest,
       LogFilledMetrics_SuggestionHasBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  // Simulate filling the card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));

  // Fill the card suggestion again.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 2)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED,
              2),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when a masked server card with a benefit is filled from a list of
// card suggestions containing a benefit from the same issuer or benefit source,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE`
// and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// are logged only once in the issuer or benefit source specific histogram for
// card benefits.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogFilledMetrics_BenefitHistogram_MultipleSuggestionsWithSameBenefitSourceHaveBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());
  AddAdditionalCardWithBenefit();

  // Simulate filling the card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));

  // Fill the card suggestion again.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when a masked server card with no benefit is filled from a
// list of suggestions containing both cards with benefits and cards without
// benefits,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE`
// is not logged and
// 'FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE'
// is logged in the issuer or benefit source specific histogram for card
// benefits.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogFilledMetrics_BenefitHistogram_FilledNoBenefits_OtherSuggestionHasBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  CreditCard second_card = test::GetMaskedServerCard2();
  personal_data().test_payments_data_manager().AddServerCreditCard(second_card);

  // Simulate filling the card with no benefit.
  ShowCardSuggestions();
  ShowSuggestionsThenSelectAndFillCard(&second_card);

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when the shown masked server card suggestions do not have any
// entries with benefits when a suggestion is filled, filled metrics for card
// benefit won't be logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogFilledMetrics_NoSuggestionsWithBenefits) {
  base::HistogramTester histogram_tester;

  // Simulate filling the card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              0)));
}

// Tests that when a masked server card with no benefit is filled from a list of
// suggestions containing both cards with benefits and cards without benefits,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// will be logged only once.
TEST_P(CardBenefitFormEventMetricsTest,
       LogFilledMetrics_FilledNoBenefits_OtherSuggestionHasBenefits) {
  AddBenefitToCard(card());

  // Add a second card which has no benefit available.
  CreditCard card2 = test::GetMaskedServerCard2();
  personal_data().test_payments_data_manager().AddServerCreditCard(card2);

  base::HistogramTester histogram_tester;

  // Simulate filling the card with no benefit.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByInstrumentId(
          card2.instrument_id()));

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
          1)));

  // Fill the card suggestion again.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByInstrumentId(
          card2.instrument_id()));

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(
                  Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 2)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_FILLED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
          1)));
}

// Test that when a local card is filled after a masked server card with a
// benefit is filled,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED`
// is only logged for the masked server card filling.
TEST_P(CardBenefitFormEventMetricsTest,
       LogFilledMetrics_FilledMaskedServerCardAndThenLocalCard) {
  AddBenefitToCard(card());
  AddLocalCard();

  base::HistogramTester histogram_tester;

  // Simulate filling with a masked server card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  ASSERT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 1),
                     Bucket(FORM_EVENT_LOCAL_SUGGESTION_FILLED, 0)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED,
          1)));

  // Simulate filling with a local card.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByGUID(
          local_card_guid()));

  ASSERT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_FILLED, 1),
                     Bucket(FORM_EVENT_LOCAL_SUGGESTION_FILLED, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_FILLED,
          1)));
}

// Tests that when we have one server card with a benefit available and a local
// card, when we fill the server card with a benefit available, we don't log
// `kSuggestionWithBenefitFilledWithMultipleServerCards`.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_OneServerCardWithBenefitAndOneLocalCard_DoesNotLogSuggestionWithBenefitFilledWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with a benefit available.
  AddBenefitToCard(card());

  // Add local card.
  CreditCard local_card = test::GetCreditCard();
  personal_data().payments_data_manager().AddCreditCard(local_card);

  // Simulate filling the server card with a benefit available.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      0);
}

// Tests that when we have multiple server cards with one having benefits
// available, we log `kSuggestionWithBenefitFilledWithMultipleServerCards` once
// when the server card with benefits available is filled twice.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithOneBenefitAvailable_LogSuggestionWithBenefitFilledWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with benefit available.
  AddBenefitToCard(card());

  // Add a server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate filling the server card with a benefit available.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      1);

  // Filling the suggestion again.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      1);
}

// Tests that when we have multiple server cards without any benefits available,
// after filling any server card,
// `kSuggestionWithBenefitFilledWithMultipleServerCards` will not be logged.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithoutBenefitsAvailable_DoesNotLogSuggestionWithBenefitFilledWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add a server card without a benefit available.
  CreditCard server_card_1 = test::GetMaskedServerCard();
  personal_data().test_payments_data_manager().AddServerCreditCard(
      server_card_1);

  // Add another server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate filling the server card without a benefit available.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByGUID(
          server_card_1.guid()));
  SubmitForm(form());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::kSuggestionWithBenefitFilledWithMultipleServerCards,
      0);
}

// Tests that when a form is submitted after a masked server card with a
// benefit is filled from a list of suggestions containing a masked server
// card with a benefit,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE`,
// and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// are logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSubmittedMetrics_SuggestionHasBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  // Simulate submitting the card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  SubmitForm(form());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SUBMITTED_ONCE, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when a form is submitted after a masked server card with a
// benefit is filled from a list of suggestions containing a list of masked
// server cards with a benefit from the same issuer or benefit source,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE`
// and
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// are logged only once in the issuer or benefit source specific histogram for
// card benefits.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogSubmittedMetrics_BenefitHistogram_MultipleSuggestionsWithSameBenefitSourceHaveBenefits) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());
  AddAdditionalCardWithBenefit();

  // Simulate submitting the card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  SubmitForm(form());
  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE,
              1),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when a form is submitted after a masked server card with no
// benefit is filled from a list of suggestions containing both cards with
// benefits and cards without benefits,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE`
// is not logged and
// 'FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE'
// is logged in the issuer or benefit source specific histogram for card
// benefits.
TEST_P(
    CardBenefitFormEventMetricsTest,
    LogSubmittedMetrics_BenefitHistogram_FilledNoBenefits_OtherSuggestionHasBenefits_SameBenefitSource) {
  base::HistogramTester histogram_tester;
  AddBenefitToCard(card());

  CreditCard second_card = test::GetMaskedServerCard2();
  personal_data().test_payments_data_manager().AddServerCreditCard(second_card);

  // Simulate filling the card with no benefit.
  ShowSuggestionsThenSelectAndFillCard(&second_card);
  SubmitForm(form());

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetCardBenefitFormEventHistogram()),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              1)));
}

// Tests that when the shown masked server card suggestions do not have any
// entries with benefits, when the form is submitted after a masked server
// card is filled, submitted metrics for card benefit won't be logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSubmittedMetrics_NoSuggestionsWithBenefits) {
  base::HistogramTester histogram_tester;

  // Simulate submitting the card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  SubmitForm(form());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SUBMITTED_ONCE, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              0)));
}

// Tests that when a form is submitted after a masked server card with no
// benefit is filled from a list of suggestions containing both cards with
// benefits and cards without benefits,
// `FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE`
// is logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSubmittedMetrics_FilledNoBenefits_OtherSuggestionHasBenefits) {
  AddBenefitToCard(card());

  // Add a second card which has no benefit available.
  CreditCard card2 = test::GetMaskedServerCard2();
  personal_data().test_payments_data_manager().AddServerCreditCard(card2);

  base::HistogramTester histogram_tester;

  // Simulate submitting the card.
  ShowSuggestionsThenSelectAndFillCard(&card2);
  SubmitForm(form());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SUBMITTED_ONCE, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(Bucket(
          FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
          1)));
}

// Test that when a form is submitted after a masked server card with no
// benefit is filled and then overwritten by a local card, submitted metrics
// for card benefit won't be logged.
TEST_P(CardBenefitFormEventMetricsTest,
       LogSubmittedMetrics_FilledMaskedServerCardAndThenLocalCard) {
  AddLocalCard();

  base::HistogramTester histogram_tester;

  // Filling with a masked server card.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());

  // Filling with a local card.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByGUID(
          local_card_guid()));
  SubmitForm(form());

  ASSERT_THAT(histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
              BucketsInclude(Bucket(
                  FORM_EVENT_MASKED_SERVER_CARD_SUGGESTION_SUBMITTED_ONCE, 1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Autofill.FormEvents.CreditCard"),
      BucketsInclude(
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_WITH_BENEFIT_AVAILABLE_SUBMITTED_ONCE,
              0),
          Bucket(
              FORM_EVENT_SUGGESTION_FOR_SERVER_CARD_SUBMITTED_AFTER_CARD_WITH_BENEFIT_AVAILABLE_SHOWN_ONCE,
              0)));
}

// Tests that when we have one server card with a benefit available and a local
// card, when we submit the server card with a benefit available, we don't log
// `kSuggestionWithBenefitSubmittedWithMultipleServerCards`.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_OneServerCardWithBenefitAndOneLocalCard_DoesNotLogSuggestionWithBenefitSubmittedWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with a benefit available.
  AddBenefitToCard(card());

  // Add local card.
  CreditCard local_card = test::GetCreditCard();
  personal_data().payments_data_manager().AddCreditCard(local_card);

  // Simulate submitting the server card with a benefit available.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  SubmitForm(form());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSubmittedWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSubmittedWithMultipleServerCards,
      0);
}

// Tests that when we have multiple server cards with one having benefits
// available, we log `kSuggestionWithBenefitSubmittedWithMultipleServerCards`
// when the server card with benefits available is submitted.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithOneBenefitAvailable_LogSuggestionWithBenefitSubmittedWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add server card with benefit available.
  AddBenefitToCard(card());

  // Add a server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate submitting the server card with a benefit available.
  ShowSuggestionsThenSelectAndFillCard(GetCreditCard());
  SubmitForm(form());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSubmittedWithMultipleServerCards,
      1);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSubmittedWithMultipleServerCards,
      1);
}

// Tests that when we have multiple server cards without any benefits available,
// after submitting any server card,
// `kSuggestionWithBenefitSubmittedWithMultipleServerCards` will not be logged.
TEST_P(
    CardBenefitFormEventMetricsTest,
    Metrics_MultipleServerCardsWithoutBenefitsAvailable_DoesNotLogSuggestionWithBenefitSubmittedWithMultipleServerCards) {
  base::HistogramTester histogram_tester;

  // Add a server card without a benefit available.
  CreditCard server_card_1 = test::GetMaskedServerCard();
  personal_data().test_payments_data_manager().AddServerCreditCard(
      server_card_1);

  // Add another server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate submitting the server card without a benefit available.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByGUID(
          server_card_1.guid()));
  SubmitForm(form());

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard.Benefits",
      CardBenefitFormEvent::
          kSuggestionWithBenefitSubmittedWithMultipleServerCards,
      0);
  histogram_tester.ExpectBucketCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.", GetSuffix()}),
      CardBenefitFormEvent::
          kSuggestionWithBenefitSubmittedWithMultipleServerCards,
      0);
}

class CardBenefitFormEventMetricsInvalidBenefitSourceTest
    : public AutofillMetricsBaseTest,
      public testing::Test {
 public:
  CardBenefitFormEventMetricsInvalidBenefitSourceTest() = default;
  ~CardBenefitFormEventMetricsInvalidBenefitSourceTest() override = default;

  // Adding a benefit for the card on client.
  void AddBenefitToCard(CreditCard& card) {
    card.set_product_terms_url(GURL("https://www.example.com/term"));
    CreditCardBenefit benefit = test::GetActiveCreditCardFlatRateBenefit();
    test_api(benefit).SetLinkedCardInstrumentId(
        CreditCardBenefitBase::LinkedCardInstrumentId(card.instrument_id()));
    personal_data().payments_data_manager().AddCreditCardBenefitForTest(
        benefit);
  }

  // Simulate showing card suggestinos.
  void ShowCardSuggestions() {
    autofill_manager().OnAskForValuesToFillTest(
        form(), form().fields()[credit_card_number_field_index()].global_id());
    DidShowAutofillSuggestions(form(), credit_card_number_field_index(),
                               SuggestionType::kCreditCardEntry);
  }

  // Simulate selecting a card from a list of suggestions.
  void ShowSuggestionsAndSelectCard(const CreditCard* card) {
    ShowCardSuggestions();
    autofill_manager().FillOrPreviewForm(
        mojom::ActionPersistence::kFill, form(),
        form().fields()[credit_card_number_field_index()].global_id(), card,
        AutofillTriggerSource::kPopup);
  }

  // Simulating selecting and filling the given `card` from a list of
  // suggestions.
  void ShowSuggestionsThenSelectAndFillCard(const CreditCard* card) {
    EXPECT_CALL(credit_card_access_manager(), FetchCreditCard)
        .WillOnce(base::test::RunOnceCallback<1>(*card));
    ShowCardSuggestions();
    autofill_manager().FillOrPreviewForm(
        mojom::ActionPersistence::kFill, form(),
        form().fields()[credit_card_number_field_index()].global_id(), card,
        AutofillTriggerSource::kPopup);
  }

  void SetUp() override {
    SetUpHelper();
    // Set up the form data. Reset form action to skip the IsFormMixedContent
    // check.
    form_ =
        GetAndAddSeenForm({.description_for_logging = "CardBenefit",
                           .fields = {{.role = CREDIT_CARD_NAME_FULL},
                                      {.role = CREDIT_CARD_NUMBER},
                                      {.role = CREDIT_CARD_EXP_MONTH},
                                      {.role = CREDIT_CARD_EXP_2_DIGIT_YEAR}},
                           .action = ""});
    credit_card_number_field_index_ = 1;

    // Initialize features.
    scoped_feature_list_.InitWithFeatures(
        /*enabled_features=*/
        {features::kAutofillEnableCardBenefitsSync,
         features::kAutofillEnableCardBenefitsSourceSync,
         features::kAutofillEnableCardBenefitsForAmericanExpress,
         features::kAutofillEnableCardBenefitsForBmo,
         features::kAutofillEnableFlatRateCardBenefitsFromCurinos},
        /*disabled_features=*/{});
  }

  void TearDown() override { TearDownHelper(); }

  const FormData& form() const { return form_; }
  int credit_card_number_field_index() const {
    return credit_card_number_field_index_;
  }

 private:
  int credit_card_number_field_index_;
  CreditCard card_;
  FormData form_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

// Tests that when we have multiple server cards with one having an invalid
// benefit source, when the server card with an invalid benefit source is
// selected, no multiple server card metrics will be logged.
TEST_F(
    CardBenefitFormEventMetricsInvalidBenefitSourceTest,
    Metrics_MultipleServerCardsWithOneInvalidBenefitSource_DoesNotLogAnyMultipleServerCardMetrics) {
  base::HistogramTester histogram_tester;

  // Add a server card with an invalid benefit source.
  CreditCard server_card_1 = test::GetMaskedServerCard();
  server_card_1.set_benefit_source("UnknownSource");
  personal_data().test_payments_data_manager().AddServerCreditCard(
      server_card_1);
  AddBenefitToCard(server_card_1);

  // Add a second server card without a benefit available.
  personal_data().test_payments_data_manager().AddServerCreditCard(
      test::GetMaskedServerCard2());

  // Simulate submitting the server card with an invalid benefit source.
  ShowSuggestionsThenSelectAndFillCard(
      personal_data().payments_data_manager().GetCreditCardByGUID(
          server_card_1.guid()));
  SubmitForm(form());

  histogram_tester.ExpectTotalCount("Autofill.FormEvents.CreditCard.Benefits",
                                    0);
  histogram_tester.ExpectTotalCount(
      base::StrCat({"Autofill.FormEvents.CreditCard.Benefits.",
                    GetCardBenefitSourceSuffix("UnknownSource")}),
      0);
}

#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

}  // namespace
}  // namespace autofill::autofill_metrics
