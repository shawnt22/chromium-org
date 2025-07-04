// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/crowdsourcing/determine_possible_field_types.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/containers/span.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/zip.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/crowdsourcing/disambiguate_possible_field_types.h"
#include "components/autofill/core/browser/data_model/addresses/address.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile_comparator.h"
#include "components/autofill/core/browser/data_model/autofill_ai/entity_instance.h"
#include "components/autofill/core/browser/data_model/data_model_utils.h"
#include "components/autofill/core/browser/data_model/payments/credit_card.h"
#include "components/autofill/core/browser/data_model/valuables/loyalty_card.h"
#include "components/autofill/core/browser/data_quality/validation.h"
#include "components/autofill/core/browser/field_type_utils.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_regex_constants.h"
#include "components/autofill/core/common/autofill_regexes.h"
#include "components/autofill/core/common/unique_ids.h"

namespace autofill {

namespace {

struct DateAndFormat {
  data_util::Date date;
  std::u16string format;
};

// Matches a date consisting of year, month, and day in a the given string.
std::vector<DateAndFormat> GetMatchingCompleteDateAndFormats(
    std::u16string_view date) {
  std::vector<DateAndFormat> dafs;
  for (std::u16string_view format :
       {// Ordering: year month day.
        u"YYYY*MM*DD", u"YY*MM*DD", u"YYYY+M+D", u"YY+M+D",
        // Ordering: month day year.
        u"MM*DD*YYYY", u"MM*DD*YY", u"M+D+YYYY", u"M+D+YY",
        // Ordering: day month year.
        u"DD*MM*YYYY", u"DD*MM*YY", u"D+M+YYYY", u"D+M+YY"}) {
    data_util::Date result;
    const char16_t* separator = nullptr;
    if (data_util::ParseDate(date, format, result, separator) &&
        data_util::IsValidDateForFormat(result, format)) {
      std::u16string instantiated_format;
      base::ReplaceChars(format, u"*+", separator, &instantiated_format);
      if (data_util::ParseDate(date, instantiated_format, result)) {
        dafs.emplace_back(result, std::move(instantiated_format));
      }
    }
  }
  return dafs;
}

// Finds the first field in |form_structure| with |field.value|=|value|.
AutofillField* FindFirstFieldWithValue(const FormStructure& form_structure,
                                       std::u16string_view value) {
  for (const auto& field : form_structure) {
    std::u16string trimmed_value;
    base::TrimWhitespace(field->value_for_import(), base::TRIM_ALL,
                         &trimmed_value);
    if (trimmed_value == value) {
      return field.get();
    }
  }
  return nullptr;
}

// Heuristically identifies all possible credit card verification fields.
AutofillField* HeuristicallyFindCVCFieldForUpload(
    const FormStructure& form_structure) {
  // Stores a pointer to the explicitly found expiration year.
  bool found_explicit_expiration_year_field = false;

  // The first pass checks the existence of an explicitly marked field for the
  // credit card expiration year.
  for (const auto& field : form_structure) {
    const FieldTypeSet& type_set = field->possible_types();
    if (type_set.find(CREDIT_CARD_EXP_2_DIGIT_YEAR) != type_set.end() ||
        type_set.find(CREDIT_CARD_EXP_4_DIGIT_YEAR) != type_set.end()) {
      found_explicit_expiration_year_field = true;
      break;
    }
  }

  // Keeps track if a credit card number field was found.
  bool credit_card_number_found = false;

  // In the second pass, the CVC field is heuristically searched for.
  // A field is considered a CVC field, iff:
  // * it appears after the credit card number field;
  // * it has no prediction yet;
  // * it does not look like an expiration year or an expiration year was
  //   already found;
  // * it is filled with a 3-4 digit number;
  for (const auto& field : form_structure) {
    const FieldTypeSet& type_set = field->possible_types();

    // Checks if the field is of |CREDIT_CARD_NUMBER| type.
    if (type_set.find(CREDIT_CARD_NUMBER) != type_set.end()) {
      credit_card_number_found = true;
      continue;
    }
    // Skip the field if no credit card number was found yet.
    if (!credit_card_number_found) {
      continue;
    }

    // Don't consider fields that already have any prediction.
    if (!type_set.empty()) {
      continue;
    }

    std::u16string trimmed_value;
    base::TrimWhitespace(field->value_for_import(), base::TRIM_ALL,
                         &trimmed_value);

    // Skip the field if it can be confused with a expiration year.
    if (!found_explicit_expiration_year_field &&
        IsPlausible4DigitExpirationYear(trimmed_value)) {
      continue;
    }

    // Skip the field if its value does not like a CVC value.
    if (!IsPlausibleCreditCardCVCNumber(trimmed_value)) {
      continue;
    }

    return field.get();
  }
  return nullptr;
}

// Iff the CVC of the credit card is known, find the first field with this
// value (also set |properties_mask| to |kKnownValue|). Otherwise, heuristically
// search for the CVC field if any.
AutofillField* GetBestPossibleCVCFieldForUpload(
    const FormStructure& form_structure,
    std::u16string_view last_unlocked_credit_card_cvc) {
  if (!last_unlocked_credit_card_cvc.empty()) {
    AutofillField* result =
        FindFirstFieldWithValue(form_structure, last_unlocked_credit_card_cvc);
    if (result) {
      result->set_properties_mask(FieldPropertiesFlags::kKnownValue);
    }
    return result;
  }
  return HeuristicallyFindCVCFieldForUpload(form_structure);
}

// Returns the FieldTypes for some given EntityInstance defines a non-empty
// value.
//
// If kAutofillAiNoTagTypes is disabled:
// This may not just include Autofill AI types like PASSPORT_NUMBER but
// also tag types like PASSPORT_NAME_TAG together with the refined type like
// NAME_FIRST.
// TODO(crbug.com/422563282): Remove comment when cleaning up
// kAutofillAiNoTagTypes.
FieldTypeSet GetAvailableAutofillAiFieldTypes(
    base::span<const EntityInstance> entities,
    const std::string& app_locale) {
  CHECK(base::FeatureList::IsEnabled(features::kAutofillAiWithDataSchema));
  AutofillProfileComparator comparator(app_locale);
  FieldTypeSet types;
  for (const EntityInstance& entity : entities) {
    for (const AttributeInstance& attribute : entity.attributes()) {
      for (FieldType field_type : attribute.type().field_subtypes()) {
        bool is_empty = comparator.HasOnlySkippableCharacters(attribute.GetInfo(
            field_type, comparator.app_locale(), std::nullopt));
        if (!is_empty) {
          if (!base::FeatureList::IsEnabled(features::kAutofillAiNoTagTypes)) {
            types.insert(attribute.type().field_type());
          }
          types.insert(field_type);
        }
      }
    }
  }
  return types;
}

// Returns the FieldTypes for some given EntityInstance has an attribute whose
// value matches `value_u16`.
//
// If kAutofillAiNoTagTypes is disabled:
// This may not just include Autofill AI types like PASSPORT_NUMBER but
// also tag types like PASSPORT_NAME_TAG together with the refined type like
// NAME_FIRST.
// TODO(crbug.com/422563282): Remove comment when cleaning up
// kAutofillAiNoTagTypes.
FieldTypeSet GetPossibleAutofillAiFieldTypes(
    base::span<const EntityInstance> entities,
    std::u16string_view value_u16,
    const std::string& app_locale) {
  CHECK(base::FeatureList::IsEnabled(features::kAutofillAiWithDataSchema));

  AutofillProfileComparator comparator(app_locale);
  if (comparator.HasOnlySkippableCharacters(value_u16)) {
    return {};
  }

  std::u16string value =
      AutofillProfileComparator::NormalizeForComparison(value_u16);
  FieldTypeSet types;
  for (const EntityInstance& entity : entities) {
    for (const AttributeInstance& attribute : entity.attributes()) {
      for (FieldType field_type : attribute.type().field_subtypes()) {
        bool matches = comparator.Compare(
            value,
            attribute.GetInfo(field_type, comparator.app_locale(),
                              std::nullopt),
            AutofillProfileComparator::DISCARD_WHITESPACE);
        if (matches) {
          if (!base::FeatureList::IsEnabled(features::kAutofillAiNoTagTypes)) {
            types.insert(attribute.type().field_type());
          }
          types.insert(field_type);
        }
      }
    }
  }
  return types;
}

void FindAndSetPossibleDateFieldTypes(
    base::span<const EntityInstance> entities,
    const std::map<FieldGlobalId, DatesAndFormats>& dates_and_formats,
    const std::string& app_locale,
    FormStructure& form) {
  std::map<data_util::Date, std::vector<AutofillField*>> date_to_field;
  for (const auto& [field_id, dafs] : dates_and_formats) {
    for (const data_util::Date& date : dafs.dates) {
      if (AutofillField* field = form.GetFieldById(field_id)) {
        date_to_field[date].push_back(field);
      }
    }
  }

  for (const EntityInstance& entity : entities) {
    for (const AttributeInstance& attribute : entity.attributes()) {
      for (const FieldType field_type : attribute.type().field_subtypes()) {
        if (!IsDateFieldType(field_type)) {
          continue;
        }
        data_util::Date date;
        if (data_util::ParseDate(attribute.GetCompleteInfo(app_locale),
                                 u"YYYY-MM-DD", date)) {
          if (auto it = date_to_field.find(date); it != date_to_field.end()) {
            for (AutofillField* field : it->second) {
              FieldTypeSet field_types = field->possible_types();
              field_types.insert(field_type);
              field->set_possible_types(field_types);
            }
          }
        }
      }
    }
  }
}

// Matches the value from `field` against the values stored in the given
// profiles etc.
FieldTypeSet GetPossibleFieldTypes(
    const AutofillField& field,
    base::span<const AutofillProfile> profiles,
    base::span<const CreditCard> credit_cards,
    base::span<const EntityInstance> entities,
    base::span<const LoyaltyCard> loyalty_cards,
    const std::set<FieldGlobalId> fields_that_match_state,
    const std::string& app_locale) {
  std::u16string value_u16 = field.value_for_import();
  base::TrimWhitespace(value_u16, base::TRIM_ALL, &value_u16);

  FieldTypeSet matching_types;

  for (const AutofillProfile& profile : profiles) {
    profile.GetMatchingTypes(value_u16, app_locale, &matching_types);
  }
  if (fields_that_match_state.contains(field.global_id())) {
    matching_types.insert(ADDRESS_HOME_STATE);
  }

  for (const CreditCard& card : credit_cards) {
    card.GetMatchingTypes(value_u16, app_locale, &matching_types);
  }

  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableLoyaltyCardsFilling)) {
    const std::string value_u8 = base::UTF16ToUTF8(value_u16);
    for (const LoyaltyCard& card : loyalty_cards) {
      if (value_u8 == card.loyalty_card_number()) {
        matching_types.insert(LOYALTY_MEMBERSHIP_ID);
      }
    }
  }

  if (base::FeatureList::IsEnabled(features::kAutofillAiWithDataSchema)) {
    matching_types.insert_all(
        GetPossibleAutofillAiFieldTypes(entities, value_u16, app_locale));
  }

  return matching_types;
}

}  // namespace

DatesAndFormats::DatesAndFormats() = default;
DatesAndFormats::DatesAndFormats(base::flat_set<data_util::Date> dates,
                                 base::flat_set<std::u16string> formats)
    : dates(std::move(dates)), formats(std::move(formats)) {}
DatesAndFormats::DatesAndFormats(DatesAndFormats&&) = default;
DatesAndFormats& DatesAndFormats::operator=(DatesAndFormats&&) = default;
DatesAndFormats::~DatesAndFormats() = default;

std::set<FieldGlobalId> PreProcessStateMatchingTypes(
    base::span<const AutofillProfile*> profiles,
    const FormStructure& form_structure,
    const std::string& app_locale) {
  std::set<FieldGlobalId> fields_that_match_state;
  for (const auto* profile : profiles) {
    std::optional<AlternativeStateNameMap::CanonicalStateName>
        canonical_state_name_from_profile =
            profile->GetAddress().GetCanonicalizedStateName();

    if (!canonical_state_name_from_profile) {
      continue;
    }

    const std::u16string& country_code =
        profile->GetInfo(AutofillType(HtmlFieldType::kCountryCode), app_locale);

    for (auto& field : form_structure) {
      if (fields_that_match_state.contains(field->global_id())) {
        continue;
      }

      std::optional<AlternativeStateNameMap::CanonicalStateName>
          canonical_state_name_from_text =
              AlternativeStateNameMap::GetCanonicalStateName(
                  base::UTF16ToUTF8(country_code), field->value_for_import());

      if (canonical_state_name_from_text &&
          canonical_state_name_from_text.value() ==
              canonical_state_name_from_profile.value()) {
        fields_that_match_state.insert(field->global_id());
      }
    }
  }
  return fields_that_match_state;
}

void DeterminePossibleFieldTypesForUpload(
    base::span<const AutofillProfile> profiles,
    base::span<const CreditCard> credit_cards,
    base::span<const EntityInstance> entities,
    base::span<const LoyaltyCard> loyalty_cards,
    const std::set<FieldGlobalId>& fields_that_match_state,
    std::u16string_view last_unlocked_credit_card_cvc,
    const std::map<FieldGlobalId, DatesAndFormats>& dates_and_formats,
    const std::string& app_locale,
    FormStructure& form) {
  // Most type detection happens in this loop.
  for (const std::unique_ptr<AutofillField>& field : form.fields()) {
    field->set_possible_types(GetPossibleFieldTypes(
        *field, profiles, credit_cards, entities, loyalty_cards,
        fields_that_match_state, app_locale));
  }

  // Date detection is not part of the above loop because dates can span
  // multiple fields.
  FindAndSetPossibleDateFieldTypes(entities, dates_and_formats, app_locale,
                                   form);

  // As CVCs are not stored, run special heuristics to detect CVC-like values.
  if (AutofillField* cvc_field = GetBestPossibleCVCFieldForUpload(
          form, last_unlocked_credit_card_cvc)) {
    FieldTypeSet possible_types = cvc_field->possible_types();
    possible_types.insert(CREDIT_CARD_VERIFICATION_CODE);
    cvc_field->set_possible_types(possible_types);
  }

  for (const std::unique_ptr<AutofillField>& field : form.fields()) {
    if (field->possible_types().empty()) {
      field->set_possible_types({UNKNOWN_TYPE});
    }
  }

  DisambiguatePossibleFieldTypes(form);
}

FieldTypeSet DetermineAvailableFieldTypes(
    base::span<const AutofillProfile> profiles,
    base::span<const CreditCard> credit_cards,
    base::span<const EntityInstance> entities,
    base::span<const LoyaltyCard> loyalty_cards,
    std::u16string_view last_unlocked_credit_card_cvc,
    const std::string& app_locale) {
  FieldTypeSet types;
  for (const AutofillProfile& profile : profiles) {
    profile.GetNonEmptyTypes(app_locale, &types);
  }

  for (const CreditCard& card : credit_cards) {
    card.GetNonEmptyTypes(app_locale, &types);
  }
  // As CVC is not stored, treat it separately.
  if (!last_unlocked_credit_card_cvc.empty() ||
      types.contains(CREDIT_CARD_NUMBER)) {
    types.insert(CREDIT_CARD_VERIFICATION_CODE);
  }

  if (base::FeatureList::IsEnabled(features::kAutofillAiWithDataSchema)) {
    types.insert_all(GetAvailableAutofillAiFieldTypes(entities, app_locale));
  }

  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableLoyaltyCardsFilling) &&
      !loyalty_cards.empty()) {
    types.insert(LOYALTY_MEMBERSHIP_ID);
  }
  return types;
}

std::map<FieldGlobalId, DatesAndFormats> ExtractDatesInFields(
    base::span<const std::unique_ptr<AutofillField>> fields) {
  // Cheap plausibility checks if the field is relevant for date matching.
  auto may_be_interesting = [](const std::unique_ptr<AutofillField>& field) {
    return field->form_control_type() == FormControlType::kInputText &&
           (field->is_user_edited() || field->is_autofilled() ||
            field->initial_value() != field->value());
  };

  // Cheap check if the field's value might contain a year, month, and day.
  auto may_be_complete_date = [&](const std::unique_ptr<AutofillField>& field) {
    static constexpr size_t kMinDateLength =
        std::u16string_view(u"1.1.25").size();
    static constexpr size_t kMaxDateLength =
        std::u16string_view(u"2025 / 12 / 31").size();
    const std::u16string& value = field->value();
    return kMinDateLength <= value.size() && value.size() <= kMaxDateLength &&
           std::ranges::all_of(value, [&](char16_t c) {
             return base::IsAsciiDigit(c) || data_util::IsDateSeparatorChar(c);
           });
  };

  // Cheap check if the field's value might contain a year, month, or day.
  auto may_be_part_of_date = [](const std::unique_ptr<AutofillField>& field) {
    const std::u16string& value = field->value();
    return 1 <= value.size() && value.size() <= 4 &&
           std::ranges::all_of(value, base::IsAsciiDigit<char16_t>);
  };

  // Cheap check if the three fields' values might together contain a year,
  // month and day.
  auto may_be_split_date =
      [&](base::span<const std::unique_ptr<AutofillField>, 3> group) {
        return std::ranges::all_of(group, may_be_part_of_date) &&
               (group[0]->label() == group[1]->label() ||
                group[1]->label().empty()) &&
               group[1]->label() == group[2]->label();
      };

  std::map<FieldGlobalId, DatesAndFormats> dates_and_formats_by_field;

  // Match formats against individual fields.
  if (base::FeatureList::IsEnabled(
          features::kAutofillAiVoteForFormatStringsFromSingleFields)) {
    for (const std::unique_ptr<AutofillField>& field : fields) {
      if (!may_be_interesting(field) || !may_be_complete_date(field)) {
        continue;
      }
      std::vector<DateAndFormat> dafs =
          GetMatchingCompleteDateAndFormats(field->value());
      if (!dafs.empty()) {
        dates_and_formats_by_field[field->global_id()] = DatesAndFormats(
            base::MakeFlatSet<data_util::Date>(dafs, {}, &DateAndFormat::date),
            base::MakeFlatSet<std::u16string>(dafs, {},
                                              &DateAndFormat::format));
      }
    }
  }

  // Match formats against groups of three consecutive fields.
  if (base::FeatureList::IsEnabled(
          features::kAutofillAiVoteForFormatStringsFromMultipleFields)) {
    for (size_t i = 0; i + 2 < fields.size(); ++i) {
      const base::span<const std::unique_ptr<AutofillField>, 3> group =
          fields.subspan(i).first<3>();
      if (!std::ranges::all_of(group, may_be_interesting) ||
          !may_be_split_date(group)) {
        continue;
      }
      static constexpr std::u16string_view kSeparator = u"-";
      static_assert(
          std::ranges::all_of(kSeparator, data_util::IsDateSeparatorChar));
      const std::u16string date = base::JoinString(
          {group[0]->value(), group[1]->value(), group[2]->value()},
          kSeparator);
      for (const DateAndFormat& daf : GetMatchingCompleteDateAndFormats(date)) {
        std::vector<std::u16string> partial_formats =
            base::SplitString(daf.format, kSeparator, base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_ALL);
        if (partial_formats.size() == 3) {
          for (auto [field, partial_format] :
               base::zip(group, partial_formats)) {
            DatesAndFormats& dates_and_formats =
                dates_and_formats_by_field[field->global_id()];
            dates_and_formats.dates.insert(daf.date);
            dates_and_formats.formats.insert(std::move(partial_format));
          }
        }
      }
    }
  }
  return dates_and_formats_by_field;
}

}  // namespace autofill
