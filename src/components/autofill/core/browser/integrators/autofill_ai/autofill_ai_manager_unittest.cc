// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/integrators/autofill_ai/autofill_ai_manager.h"

#include "base/strings/utf_string_conversions.h"
#include "base/task/current_thread.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/gmock_move_support.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "components/autofill/core/browser/data_manager/autofill_ai/entity_data_manager.h"
#include "components/autofill/core/browser/data_model/autofill_ai/entity_instance.h"
#include "components/autofill/core/browser/data_model/autofill_ai/entity_type.h"
#include "components/autofill/core/browser/data_model/autofill_ai/entity_type_names.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/form_structure_test_api.h"
#include "components/autofill/core/browser/foundations/test_autofill_client.h"
#include "components/autofill/core/browser/integrators/autofill_ai/autofill_ai_manager_test_api.h"
#include "components/autofill/core/browser/integrators/autofill_ai/autofill_ai_suggestions.h"
#include "components/autofill/core/browser/strike_databases/payments/test_strike_database.h"
#include "components/autofill/core/browser/suggestions/suggestion_type.h"
#include "components/autofill/core/browser/test_utils/autofill_form_test_utils.h"
#include "components/autofill/core/browser/test_utils/autofill_test_utils.h"
#include "components/autofill/core/browser/webdata/autofill_ai/entity_table.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service_test_helper.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_test_utils.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_data_test_api.h"
#include "components/autofill/core/common/form_field_data.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

// TODO(crbug.com/389629573): Refactor this test to handle only the
// implementation under `features::kAutofillAiWithDataSchema`
// flag.
namespace autofill {
namespace {

using enum SuggestionType;
using AutofillAiPayload = Suggestion::AutofillAiPayload;
using ::base::test::RunOnceCallback;
using ::base::test::RunOnceCallbackRepeatedly;
using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Pointer;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SizeIs;
using ::testing::Truly;
using ::testing::VariantWith;

auto FirstElementIs(auto&& matcher) {
  return ResultOf(
      "first element", [](const auto& container) { return *container.begin(); },
      std::move(matcher));
}

auto HasType(SuggestionType expected_type) {
  return Field("Suggestion::type", &Suggestion::type, Eq(expected_type));
}

auto HasAutofillAiPayload(auto expected_payload) {
  return Field("Suggestion::payload", &Suggestion::payload,
               VariantWith<AutofillAiPayload>(expected_payload));
}

auto HasAttributeWithValue(AttributeType attribute_type,
                           std::u16string value,
                           const std::string& app_locale) {
  return Truly([=](const EntityInstance& entity) {
    if (entity.type() != attribute_type.entity_type()) {
      return false;
    }
    base::optional_ref<const AttributeInstance> attribute =
        entity.attribute(attribute_type);
    return attribute && attribute->GetCompleteInfo(app_locale) == value;
  });
}

auto PassportWithNumber(std::u16string number) {
  return HasAttributeWithValue(
      AttributeType(AttributeTypeName::kPassportNumber), std::move(number),
      /*app_locale=*/"");
}

auto VehicleWithLicensePlate(std::u16string license_plate) {
  return HasAttributeWithValue(
      AttributeType(AttributeTypeName::kVehiclePlateNumber),
      std::move(license_plate), /*app_locale=*/"");
}

class MockAutofillClient : public TestAutofillClient {
 public:
  MockAutofillClient() = default;
  MockAutofillClient(const MockAutofillClient&) = delete;
  MockAutofillClient& operator=(const MockAutofillClient&) = delete;
  ~MockAutofillClient() override = default;

  MOCK_METHOD(
      void,
      ShowEntitySaveOrUpdateBubble,
      (EntityInstance entity,
       std::optional<EntityInstance> old_entity,
       EntitySaveOrUpdatePromptResultCallback prompt_acceptance_callback),
      (override));
};

class AutofillAiManagerTest : public testing::Test {
 public:
  AutofillAiManagerTest() {
    scoped_feature_list_.InitWithFeatures(
        /*enabled_features=*/{features::kAutofillAiWithDataSchema,
                              features::kAutofillAiNoTagTypes,
                              features::kAutofillAiServerModel},
        /*disabled_features=*/{});
    autofill_client().GetPersonalDataManager().SetPrefService(
        autofill_client().GetPrefs());
    autofill_client().set_entity_data_manager(
        std::make_unique<EntityDataManager>(
            webdata_helper_.autofill_webdata_service(),
            /*history_service=*/nullptr,
            /*strike_database=*/nullptr));
    autofill_client().SetUpPrefsAndIdentityForAutofillAi();
  }

  // Given a `FormStructure` sets `field_types_predictions` for each field in
  // the form.
  void AddPredictionsToFormStructure(
      FormStructure& form_structure,
      const std::vector<std::vector<FieldType>>& field_types_predictions) {
    CHECK_EQ(form_structure.field_count(), field_types_predictions.size());
    for (size_t i = 0; i < form_structure.field_count(); i++) {
      std::vector<AutofillQueryResponse::FormSuggestion::FieldSuggestion::
                      FieldPrediction>
          predictions_for_field;
      for (FieldType type : field_types_predictions[i]) {
        AutofillQueryResponse::FormSuggestion::FieldSuggestion::FieldPrediction
            prediction;
        prediction.set_type(type);
        predictions_for_field.push_back(prediction);
      }

      form_structure.field(i)->set_server_predictions(predictions_for_field);
    }
  }

  void AddOrUpdateEntityInstance(EntityInstance entity) {
    edm().AddOrUpdateEntityInstance(std::move(entity));
    webdata_helper_.WaitUntilIdle();
  }

  void RemoveEntityInstance(base::Uuid guid) {
    edm().RemoveEntityInstance(guid);
    webdata_helper_.WaitUntilIdle();
  }

  void AddAutofillProfile() {
    autofill_client_.GetPersonalDataManager().address_data_manager().AddProfile(
        test::GetFullProfile());
  }

  base::span<const EntityInstance> GetEntityInstances() {
    webdata_helper_.WaitUntilIdle();
    return edm().GetEntityInstances();
  }

  MockAutofillClient& autofill_client() { return autofill_client_; }
  EntityDataManager& edm() { return *autofill_client().GetEntityDataManager(); }
  AutofillAiManager& manager() { return manager_; }
  TestStrikeDatabase& strike_database() { return strike_database_; }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  base::test::SingleThreadTaskEnvironment task_environment_;
  test::AutofillUnitTestEnvironment autofill_test_env_;
  AutofillWebDataServiceTestHelper webdata_helper_{
      std::make_unique<EntityTable>()};
  NiceMock<MockAutofillClient> autofill_client_;
  TestStrikeDatabase strike_database_;
  AutofillAiManager manager_{&autofill_client(), &strike_database_};
};

// Tests that the user receives a filling suggestion when interacting with
// a field that has AutofillAi predictions.
TEST_F(AutofillAiManagerTest,
       GetSuggestionsTriggeringFieldIsAutofillAi_ReturnFillingSuggestion) {
  test::FormDescription form_description = {
      .fields = {{.role = PASSPORT_NUMBER}}};
  FormData form = test::GetFormData(form_description);
  FormStructure form_structure = FormStructure(form);
  AddPredictionsToFormStructure(form_structure, {{PASSPORT_NUMBER}});

  AddOrUpdateEntityInstance(test::GetPassportEntityInstance());
  EXPECT_THAT(manager().GetSuggestions(form_structure, form.fields().front()),
              ElementsAre(HasType(kFillAutofillAi), HasType(kSeparator),
                          HasType(kManageAutofillAi)));
}

// Tests that IPH should be displayed if the user is opted out of the feature,
// has an address, and form submission with filled out fields would lead to
// entity import.
TEST_F(AutofillAiManagerTest, ShouldDisplayIph) {
  test::FormDescription form_description = {.fields = {{}}};
  FormData form = test::GetFormData(form_description);
  FormStructure form_structure = FormStructure(form);
  AddPredictionsToFormStructure(form_structure, {{PASSPORT_NUMBER}});
  AddAutofillProfile();
  SetAutofillAiOptInStatus(autofill_client(), false);

  EXPECT_TRUE(
      manager().ShouldDisplayIph(form_structure, form.fields()[0].global_id()));
}

// Tests that IPH should not be displayed if the user is opted into AutofillAI
// already.
TEST_F(AutofillAiManagerTest, ShouldNotDisplayIphWhenOptedIn) {
  test::FormDescription form_description = {.fields = {{}}};
  FormData form = test::GetFormData(form_description);
  FormStructure form_structure = FormStructure(form);
  AddPredictionsToFormStructure(form_structure, {{PASSPORT_NUMBER}});
  AddAutofillProfile();
  SetAutofillAiOptInStatus(autofill_client(), true);

  EXPECT_FALSE(
      manager().ShouldDisplayIph(form_structure, form.fields()[0].global_id()));
}

// Tests that IPH should not be displayed if the page does not contain enough
// information for an import.
TEST_F(AutofillAiManagerTest,
       ShouldNotDisplayIphWhenInsufficientDataForImport) {
  test::FormDescription form_description = {.fields = {{}}};
  FormData form = test::GetFormData(form_description);
  FormStructure form_structure = FormStructure(form);
  AddPredictionsToFormStructure(form_structure, {{PASSPORT_ISSUE_DATE}});
  AddAutofillProfile();
  SetAutofillAiOptInStatus(autofill_client(), false);

  EXPECT_FALSE(
      manager().ShouldDisplayIph(form_structure, form.fields()[0].global_id()));
}

// Tests that IPH is not displayed on a field without AutofillAI predictions.
TEST_F(AutofillAiManagerTest, ShouldNotDisplayIphOnUnrelatedField) {
  test::FormDescription form_description = {.fields = {{}, {}}};
  FormData form = test::GetFormData(form_description);
  FormStructure form_structure = FormStructure(form);
  AddPredictionsToFormStructure(
      form_structure, {{PASSPORT_NUMBER}, {PHONE_HOME_CITY_AND_NUMBER}});
  AddAutofillProfile();
  SetAutofillAiOptInStatus(autofill_client(), false);

  EXPECT_FALSE(
      manager().ShouldDisplayIph(form_structure, form.fields()[1].global_id()));
}

class AutofillAiManagerImportFormTest : public AutofillAiManagerTest {
 public:
  AutofillAiManagerImportFormTest() = default;

  static constexpr char kDefaultUrl[] = "https://example.com";
  static constexpr char16_t kDefaultPassportNumber[] = u"123";
  static constexpr char16_t kDefaultLicensePlate[] = u"XC-12-34";

  [[nodiscard]] std::unique_ptr<FormStructure> CreateFormStructure(
      const std::vector<FieldType>& field_types_predictions,
      std::string url = std::string(kDefaultUrl)) {
    test::FormDescription form_description{.url = std::move(url)};
    for (FieldType field_type : field_types_predictions) {
      form_description.fields.emplace_back(
          test::FieldDescription({.role = field_type}));
    }
    auto form_structure =
        std::make_unique<FormStructure>(test::GetFormData(form_description));
    for (size_t i = 0; i < form_structure->field_count(); i++) {
      AutofillQueryResponse::FormSuggestion::FieldSuggestion::FieldPrediction
          prediction;
      prediction.set_type(form_description.fields[i].role);
      form_structure->field(i)->set_server_predictions({prediction});
    }
    return form_structure;
  }

  [[nodiscard]] std::unique_ptr<FormStructure> CreatePassportForm(
      std::u16string passport_number = std::u16string(kDefaultPassportNumber),
      std::string url = std::string(kDefaultUrl)) {
    std::unique_ptr<FormStructure> form = CreateFormStructure(
        {NAME_FULL, PASSPORT_NUMBER, PHONE_HOME_WHOLE_NUMBER}, std::move(url));
    form->field(0)->set_value(u"Jon Doe");
    form->field(1)->set_value(std::move(passport_number));
    return form;
  }

  [[nodiscard]] std::unique_ptr<FormStructure> CreateVehicleForm(
      std::u16string license_plate = std::u16string(kDefaultLicensePlate),
      std::string url = std::string(kDefaultUrl)) {
    std::unique_ptr<FormStructure> form =
        CreateFormStructure({NAME_FULL, VEHICLE_LICENSE_PLATE}, std::move(url));
    form->field(0)->set_value(u"Jane Doe");
    form->field(1)->set_value(std::move(license_plate));
    return form;
  }

  std::u16string GetValueFromEntity(const EntityInstance entity,
                                    AttributeType attribute,
                                    const std::string& app_locale = "") {
    return entity.attribute(attribute)->GetCompleteInfo(app_locale);
  }

  std::u16string GetValueFromEntityForAttributeTypeName(
      const EntityInstance entity,
      AttributeTypeName type,
      const std::string& app_locale) {
    AttributeType attribute_type = AttributeType(type);
    base::optional_ref<const AttributeInstance> instance =
        entity.attribute(attribute_type);
    CHECK(instance);
    return instance->GetInfo(attribute_type.field_type(), app_locale,
                             /*format_string=*/std::nullopt);
  }
};

// Tests that save prompts are only shown three times per url and entity type.
TEST_F(AutofillAiManagerImportFormTest, StrikesForSavePromptsPerUrl) {
  constexpr char16_t kOtherPassportNumber[] = u"67867";
  AutofillClient::EntitySaveOrUpdatePromptResult decline = {
      /*did_user_decline=*/true, std::nullopt};

  MockFunction<void()> check;
  {
    InSequence s;
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(decline));
    EXPECT_CALL(check, Call);
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(decline));
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    VehicleWithLicensePlate(kDefaultLicensePlate), _, _))
        .WillOnce(RunOnceCallback<2>(decline));
  }

  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber), /*ukm_source_id=*/{}));
  // The fourth attempt is ignored.
  EXPECT_FALSE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));

  // But submitting on a different page leads to a prompt.
  check.Call();
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kDefaultPassportNumber, "https://other.com"),
      /*ukm_source_id=*/{}));
  // And so does submitting a different entity type on the original page.
  EXPECT_TRUE(
      manager().OnFormSubmitted(*CreateVehicleForm(), /*ukm_source_id=*/{}));
}

// Tests that save prompts are only shown three times per strike attribute (in
// this case, passport number).
TEST_F(AutofillAiManagerImportFormTest, StrikesForSavePromptsPerAttribute) {
  constexpr char16_t kOtherPassportNumber[] = u"567435";
  AutofillClient::EntitySaveOrUpdatePromptResult decline = {
      /*did_user_decline=*/true, std::nullopt};
  AutofillClient::EntitySaveOrUpdatePromptResult ignore = {
      /*did_user_decline=*/false, std::nullopt};

  MockFunction<void()> check;
  {
    InSequence s;
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(ignore));
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .Times(3)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
    EXPECT_CALL(check, Call);
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(decline));
  }

  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  // The next attempt is ignored, even though it is a different domain.
  EXPECT_FALSE(manager().OnFormSubmitted(
      *CreatePassportForm(kDefaultPassportNumber, "https://other.com"),
      /*ukm_source_id*/ {}));

  // But submitting a different passport number leads to a prompt.
  check.Call();
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber, "https://other.com"),
      /*ukm_source_id*/ {}));
}

// Tests that update prompts are only shown three times per entity that is to
// be updated. Tests that accepting a prompt resets the counter.
TEST_F(AutofillAiManagerImportFormTest, StrikesForUpdates) {
  constexpr char16_t kOtherPassportNumber[] = u"67867";
  constexpr char16_t kOtherPassportNumber2[] = u"6785634567";

  AutofillClient::EntitySaveOrUpdatePromptResult decline = {
      /*did_user_decline=*/true, std::nullopt};
  AutofillClient::EntitySaveOrUpdatePromptResult ignore = {
      /*did_user_decline=*/false, std::nullopt};
  AutofillClient::EntitySaveOrUpdatePromptResult accept = {
      /*did_user_decline=*/false,
      test::GetPassportEntityInstance({.number = kDefaultPassportNumber})};

  {
    InSequence s;
    // Accept the first prompt.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(accept));

    // Accept the third prompt.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(accept));

    // If the user just ignores the prompt, no strikes are recorded.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber2), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(ignore));

    // Only three more prompts will be shown for the next update because the
    // user declines explicitly.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber2), _, _))
        .Times(3)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
  }

  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  // The first dialog was accepted.
  EXPECT_THAT(GetEntityInstances(), SizeIs(1));

  // Simulate three submissions - the last prompt is accepted.
  ASSERT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber), /*ukm_source_id=*/{}));
  ASSERT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber), /*ukm_source_id=*/{}));
  ASSERT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber), /*ukm_source_id=*/{}));

  // Simulate four more submissions - only three prompts are shown.
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber2), /*ukm_source_id=*/{}));
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber2), /*ukm_source_id=*/{}));
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber2), /*ukm_source_id=*/{}));
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber2), /*ukm_source_id=*/{}));
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber2), /*ukm_source_id=*/{}));
  EXPECT_FALSE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber2), /*ukm_source_id=*/{}));
}

// Tests that accepting a save prompt for an entity resets the strike counter
// for that entity type.
TEST_F(AutofillAiManagerImportFormTest, AcceptingResetsStrikesPerUrl) {
  constexpr char16_t kOtherPassportNumber[] = u"56745";
  constexpr char16_t kOtherLicensePlate[] = u"MU-LJ-4500";

  AutofillClient::EntitySaveOrUpdatePromptResult decline{
      /*did_user_interact=*/true, std::nullopt};
  AutofillClient::EntitySaveOrUpdatePromptResult accept = {
      /*did_user_decline=*/false,
      test::GetPassportEntityInstance({.number = kDefaultPassportNumber})};
  MockFunction<void()> check;
  {
    InSequence s;
    // First, we expect to see two save attempts for a passport and two save
    // attempts for a vehicle.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    VehicleWithLicensePlate(kDefaultLicensePlate), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
    EXPECT_CALL(check, Call);

    // We accept the next save prompt for a passport form.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(accept));

    // We now only get one more vehicle save prompt (despite submitting a form
    // twice), but two more passport prompts because passport strikes were
    // reset.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    VehicleWithLicensePlate(kOtherLicensePlate), _, _))
        .WillOnce(RunOnceCallback<2>(decline));
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kOtherPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
  }

  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreateVehicleForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreateVehicleForm(), /*ukm_source_id=*/{}));
  check.Call();

  // This one will be accepted.
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));

  // Now attempt to show more dialogs.
  EXPECT_TRUE(manager().OnFormSubmitted(*CreateVehicleForm(kOtherLicensePlate),
                                        /*ukm_source_id=*/{}));
  // This one will be ignored because strikes were only reset for passports.
  EXPECT_FALSE(manager().OnFormSubmitted(*CreateVehicleForm(kOtherLicensePlate),
                                         /*ukm_source_id=*/{}));
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber), /*ukm_source_id=*/{}));
  EXPECT_TRUE(manager().OnFormSubmitted(
      *CreatePassportForm(kOtherPassportNumber), /*ukm_source_id=*/{}));
}

// Tests that accepting a save prompt for an entity resets the strike counter
// for the strike key attributes of that entity.
TEST_F(AutofillAiManagerImportFormTest, AcceptingResetsStrikesPerAttribute) {
  AutofillClient::EntitySaveOrUpdatePromptResult decline = {
      /*did_user_decline=*/true, std::nullopt};
  AutofillClient::EntitySaveOrUpdatePromptResult accept = {
      /*did_user_decline=*/false,
      test::GetPassportEntityInstance({.number = kDefaultPassportNumber})};
  {
    InSequence s;
    // First, we expect to see two save attempts for a passport.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
    // We accept the next save prompt for a passport form.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .WillOnce(RunOnceCallback<2>(accept));

    // (User now deletes the passport.)

    // We now get more prompts for the same passport number again.
    EXPECT_CALL(autofill_client(),
                ShowEntitySaveOrUpdateBubble(
                    PassportWithNumber(kDefaultPassportNumber), _, _))
        .Times(2)
        .WillRepeatedly(RunOnceCallbackRepeatedly<2>(decline));
  }

  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  // This one will be accepted.
  ASSERT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));

  // User deletes the passport.
  ASSERT_THAT(GetEntityInstances(), SizeIs(1));
  RemoveEntityInstance(GetEntityInstances()[0].guid());

  EXPECT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
  EXPECT_TRUE(
      manager().OnFormSubmitted(*CreatePassportForm(), /*ukm_source_id=*/{}));
}

TEST_F(AutofillAiManagerImportFormTest,
       EntityContainsRequiredAttributes_ShowPromptAndAccept) {
  std::unique_ptr<FormStructure> form = CreateFormStructure(
      {NAME_FULL, PASSPORT_NUMBER, PHONE_HOME_WHOLE_NUMBER});
  form->field(0)->set_value(u"Jon Doe");
  form->field(1)->set_value(u"1234321");

  std::optional<EntityInstance> new_entity;
  std::optional<EntityInstance> old_entity;
  AutofillClient::EntitySaveOrUpdatePromptResultCallback save_callback;
  EXPECT_CALL(autofill_client(), ShowEntitySaveOrUpdateBubble)
      .WillOnce(DoAll(SaveArg<0>(&new_entity), SaveArg<1>(&old_entity),
                      MoveArg<2>(&save_callback)));
  EXPECT_TRUE(manager().OnFormSubmitted(*form, /*ukm_source_id=*/{}));
  // This is a save bubble, `old_entity` should not exist.
  EXPECT_FALSE(old_entity.has_value());

  // Accept the bubble.
  std::move(save_callback)
      .Run(AutofillClient::EntitySaveOrUpdatePromptResult(
          /*did_user_decline=*/false, new_entity));
  // Tests that the expected entity was saved.
  base::span<const EntityInstance> saved_entities = GetEntityInstances();
  ASSERT_EQ(saved_entities.size(), 1u);
  const EntityInstance& saved_entity = *saved_entities.begin();
  EXPECT_EQ(saved_entity, *new_entity);
  EXPECT_EQ(
      GetValueFromEntityForAttributeTypeName(
          saved_entity, AttributeTypeName::kPassportName, /*app_locale=*/""),
      u"Jon Doe");
  EXPECT_EQ(
      GetValueFromEntityForAttributeTypeName(
          saved_entity, AttributeTypeName::kPassportNumber, /*app_locale=*/""),
      u"1234321");
}

TEST_F(AutofillAiManagerImportFormTest,
       EntityContainsRequiredAttributes_ShowPromptAndDecline) {
  std::unique_ptr<FormStructure> form = CreateFormStructure(
      {NAME_FULL, PASSPORT_NUMBER, PHONE_HOME_WHOLE_NUMBER});
  // Set the filled values to be the same as the ones already stored.
  form->field(0)->set_value(u"Jon Doe");
  form->field(1)->set_value(u"1234321");

  AutofillClient::EntitySaveOrUpdatePromptResultCallback save_callback;
  EXPECT_CALL(autofill_client(), ShowEntitySaveOrUpdateBubble)
      .WillOnce(MoveArg<2>(&save_callback));
  EXPECT_TRUE(manager().OnFormSubmitted(*form, /*ukm_source_id=*/{}));

  // Decline the bubble.
  std::move(save_callback)
      .Run(AutofillClient::EntitySaveOrUpdatePromptResult());
  // Tests that the no entity was saved.
  base::span<const EntityInstance> saved_entities = GetEntityInstances();
  EXPECT_EQ(saved_entities.size(), 0u);
}

TEST_F(AutofillAiManagerImportFormTest,
       EntityDoesNotContainRequiredAttributes_DoNotShowPrompt) {
  std::unique_ptr<FormStructure> form =
      CreateFormStructure({PASSPORT_ISSUING_COUNTRY, NAME_FULL});
  // Set the filled values to be the same as the ones already stored.
  form->field(0)->set_value(u"Germany");
  form->field(1)->set_value(u"1234321");

  EXPECT_CALL(autofill_client(), ShowEntitySaveOrUpdateBubble).Times(0);
  EXPECT_FALSE(manager().OnFormSubmitted(*form, /*ukm_source_id=*/{}));

  // Tests that no entity was saved.
  base::span<const EntityInstance> saved_entities = GetEntityInstances();
  EXPECT_EQ(saved_entities.size(), 0u);
}

// In this test, we simulate the user submitting a form with data that is
// already contained in one of the entities.
TEST_F(AutofillAiManagerImportFormTest, EntityAlreadyStored_DoNotShowPrompt) {
  using enum AttributeTypeName;
  std::unique_ptr<FormStructure> form =
      CreateFormStructure({NAME_FULL, DRIVERS_LICENSE_NUMBER});
  EntityInstance entity = test::GetDriversLicenseEntityInstance();
  // Set the filled values to be the same as the ones already stored.
  form->field(0)->set_value(
      GetValueFromEntity(entity, AttributeType(kDriversLicenseName)));
  form->field(1)->set_value(
      GetValueFromEntity(entity, AttributeType(kDriversLicenseNumber)));
  AddOrUpdateEntityInstance(entity);

  EXPECT_CALL(autofill_client(), ShowEntitySaveOrUpdateBubble).Times(0);
  EXPECT_FALSE(manager().OnFormSubmitted(*form, /*ukm_source_id=*/{}));

  // Tests that no entity was saved.
  base::span<const EntityInstance> saved_entities = GetEntityInstances();
  EXPECT_EQ(saved_entities.size(), 1u);
}

TEST_F(AutofillAiManagerImportFormTest, NewEntity_ShowPromptAndAccept) {
  std::unique_ptr<FormStructure> form = CreateFormStructure(
      {NAME_FULL, PASSPORT_NUMBER, PHONE_HOME_WHOLE_NUMBER});
  EntityInstance existing_entity = test::GetPassportEntityInstance();
  AddOrUpdateEntityInstance(existing_entity);
  // Set the filled values to be different to the ones already stored.
  form->field(0)->set_value(u"Jon Doe");
  form->field(1)->set_value(u"1234321");

  std::optional<EntityInstance> entity;
  std::optional<EntityInstance> old_entity;
  AutofillClient::EntitySaveOrUpdatePromptResultCallback save_callback;
  EXPECT_CALL(autofill_client(), ShowEntitySaveOrUpdateBubble)
      .WillOnce(DoAll(SaveArg<0>(&entity), SaveArg<1>(&old_entity),
                      MoveArg<2>(&save_callback)));

  EXPECT_TRUE(manager().OnFormSubmitted(*form, /*ukm_source_id=*/{}));
  // This is a save bubble, `old_entity` should not exist.
  EXPECT_FALSE(old_entity.has_value());

  // Accept the bubble.
  std::move(save_callback)
      .Run(AutofillClient::EntitySaveOrUpdatePromptResult(
          /*did_user_decline=*/false, entity));
  // Tests that the expected entity was saved.
  base::span<const EntityInstance> saved_entities = GetEntityInstances();
  ASSERT_EQ(saved_entities.size(), 2u);
  EntityInstance saved_entity = *saved_entities.begin();
  if (saved_entity == existing_entity) {
    saved_entity = *(saved_entities.begin() + 1);
  }
  EXPECT_EQ(saved_entity, *entity);
  EXPECT_EQ(
      GetValueFromEntityForAttributeTypeName(
          saved_entity, AttributeTypeName::kPassportName, /*app_locale=*/""),
      u"Jon Doe");
  EXPECT_EQ(
      GetValueFromEntityForAttributeTypeName(
          saved_entity, AttributeTypeName::kPassportNumber, /*app_locale=*/""),
      u"1234321");
}

TEST_F(AutofillAiManagerImportFormTest, UpdateEntity_ShowPromptAndAccept) {
  using enum AttributeTypeName;
  // The submitted form will have issue date info.
  std::unique_ptr<FormStructure> form =
      CreateFormStructure({NAME_FULL, PASSPORT_NUMBER, PASSPORT_ISSUE_DATE,
                           PASSPORT_EXPIRATION_DATE, PASSPORT_EXPIRATION_DATE,
                           PASSPORT_EXPIRATION_DATE});

  // The current entity however does not.
  EntityInstance existing_entity_without_issue_and_expiry_dates =
      test::GetPassportEntityInstance(
          {.expiry_date = nullptr, .issue_date = nullptr});
  AddOrUpdateEntityInstance(existing_entity_without_issue_and_expiry_dates);

  // Set the filled values to be the same as the ones already stored in the
  // existing entity, also fill the issue and expiry dates.
  form->field(0)->set_value(
      GetValueFromEntity(existing_entity_without_issue_and_expiry_dates,
                         AttributeType(kPassportName)));
  form->field(1)->set_value(
      GetValueFromEntity(existing_entity_without_issue_and_expiry_dates,
                         AttributeType(kPassportNumber)));
  // Issue date.
  form->field(2)->set_value(u"01/02/16");
  form->field(2)->set_format_string_unless_overruled(
      u"DD/MM/YY", AutofillField::FormatStringSource::kServer);
  // Expiry date.
  form->field(3)->set_value(u"01");
  form->field(3)->set_format_string_unless_overruled(
      u"DD", AutofillField::FormatStringSource::kServer);
  form->field(4)->set_value(u"02");
  form->field(4)->set_format_string_unless_overruled(
      u"MM", AutofillField::FormatStringSource::kServer);
  form->field(5)->set_value(u"2020");
  form->field(5)->set_format_string_unless_overruled(
      u"YYYY", AutofillField::FormatStringSource::kServer);

  std::optional<EntityInstance> entity;
  std::optional<EntityInstance> old_entity;
  AutofillClient::EntitySaveOrUpdatePromptResultCallback save_callback;
  EXPECT_CALL(autofill_client(), ShowEntitySaveOrUpdateBubble)
      .WillOnce(DoAll(SaveArg<0>(&entity), SaveArg<1>(&old_entity),
                      MoveArg<2>(&save_callback)));
  EXPECT_TRUE(manager().OnFormSubmitted(*form, /*ukm_source_id=*/{}));
  // This is an update bubble, `old_entity` should exist.
  ASSERT_TRUE(old_entity.has_value());
  EXPECT_EQ(*old_entity, existing_entity_without_issue_and_expiry_dates);

  // Accept the bubble.
  std::move(save_callback)
      .Run(AutofillClient::EntitySaveOrUpdatePromptResult(
          /*did_user_interact=*/true, entity));
  // Tests that the expected entity was updated.
  base::span<const EntityInstance> saved_entities = GetEntityInstances();

  // Only one entity should exist, as it was updated.
  ASSERT_EQ(saved_entities.size(), 1u);
  const EntityInstance& saved_entity = *saved_entities.begin();
  EXPECT_EQ(saved_entity, *entity);
  EXPECT_EQ(saved_entity.guid(),
            existing_entity_without_issue_and_expiry_dates.guid());
  EXPECT_EQ(GetValueFromEntityForAttributeTypeName(
                saved_entity, AttributeTypeName::kPassportIssueDate,
                /*app_locale=*/""),
            u"2016-02-01");
  EXPECT_EQ(GetValueFromEntityForAttributeTypeName(
                saved_entity, AttributeTypeName::kPassportExpirationDate,
                /*app_locale=*/""),
            u"2020-02-01");
}

}  // namespace
}  // namespace autofill
