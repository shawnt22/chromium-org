// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/filling/autofill_ai/field_filling_entity_util.h"

#include "base/containers/flat_set.h"
#include "base/containers/to_vector.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/data_manager/autofill_ai/entity_data_manager.h"
#include "components/autofill/core/browser/data_model/autofill_ai/entity_instance.h"
#include "components/autofill/core/browser/data_quality/addresses/address_normalizer.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/filling/autofill_ai/select_date_matching.h"
#include "components/autofill/core/browser/filling/field_filling_util.h"
#include "components/autofill/core/browser/form_processing/autofill_ai/determine_attribute_types.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/foundations/autofill_client.h"
#include "components/autofill/core/browser/permissions/autofill_ai/autofill_ai_permission_utils.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom-shared.h"

namespace autofill {

namespace {

// Looks for the day, month, or year from `attribute` to fill into `field`.
std::optional<std::u16string> GetValueForDateSelectControl(
    const AttributeInstance& attribute,
    const AutofillField& field,
    const std::string& app_locale) {
  FieldType type = field.Type().GetStorableType();
  if (!IsDateFieldType(type)) {
    return std::nullopt;
  }

  auto get_part = [&](std::u16string format_string, uint32_t min = 0,
                      uint32_t max =
                          std::numeric_limits<uint32_t>::max()) -> uint32_t {
    std::u16string s = attribute.GetInfo(field.Type().GetStorableType(),
                                         app_locale, format_string);
    unsigned int i = 0;
    return base::StringToUint(s, &i) && min <= i && i <= max
               ? i
               : std::numeric_limits<uint32_t>::max();
  };

  if (base::optional_ref<const SelectOption> match =
          GetDayRange(field.options()).get_by_value(get_part(u"D", 1, 31))) {
    return match->value;
  }
  if (base::optional_ref<const SelectOption> match =
          GetMonthRange(field.options()).get_by_value(get_part(u"M", 1, 12))) {
    return match->value;
  }
  if (base::optional_ref<const SelectOption> match =
          GetYearRange(field.options()).get_by_value(get_part(u"YYYY"))) {
    return match->value;
  }
  return std::nullopt;
}

std::u16string GetValueForInput(const AttributeInstance& attribute,
                                const AutofillField& field,
                                const std::string& app_locale) {
  FieldType type = field.Type().GetStorableType();
  // TODO(crbug.com/389625753): Investigate whether only passing the
  // field type is the right choice here. This would for example
  // fail the fill a PASSPORT_NUMBER field that gets a
  // PHONE_HOME_WHOLE_NUMBER classification from regular autofill
  // prediction logic.
  std::u16string value =
      attribute.GetInfo(type, app_locale, field.format_string());
  switch (field.Type().GetStorableType()) {
    case ADDRESS_HOME_STATE:
      // TODO(crbug.com/389625753): Support countries other than the US.
      return GetStateTextForInput(value, /*country_code=*/"US",
                                  field.max_length(),
                                  /*failure_to_fill=*/nullptr);
    default:
      return value;
  }
}

std::u16string GetValueForSelectControl(const AttributeInstance& attribute,
                                        const AutofillField& field,
                                        const std::string& app_locale,
                                        AddressNormalizer* address_normalizer) {
  FieldType type = field.Type().GetStorableType();
  if (IsDateFieldType(type)) {
    return GetValueForDateSelectControl(attribute, field, app_locale)
        .value_or(u"");
  }
  std::u16string fill_value = GetValueForInput(attribute, field, app_locale);
  if (fill_value.empty()) {
    return u"";
  }

  switch (type) {
    case ADDRESS_HOME_COUNTRY:
      return GetCountrySelectControlValue(fill_value, field.options(),
                                          /*failure_to_fill=*/nullptr);
    case ADDRESS_HOME_STATE:
      // TODO(crbug.com/389625753): Support countries other than the US.
      return GetStateSelectControlValue(fill_value, field.options(),
                                        /*country_code=*/"US",
                                        address_normalizer,
                                        /*failure_to_fill=*/nullptr);
    default:
      return GetSelectControlValue(fill_value, field.options(),
                                   /*failure_to_fill=*/nullptr)
          .value_or(u"");
  }
}

}  // namespace

base::flat_set<FieldGlobalId> GetFieldsFillableByAutofillAi(
    const FormStructure& form,
    const AutofillClient& client) {
  const EntityDataManager* const edm = client.GetEntityDataManager();
  if (!MayPerformAutofillAiAction(client, AutofillAiAction::kFilling) || !edm) {
    return {};
  }

  std::optional<base::flat_map<
      Section,
      base::flat_map<EntityType, std::vector<AutofillFieldWithAttributeType>>>>
      fields_and_types;

  // Returns true if there is data present that could fill the `field`.
  auto is_fillable = [&](const AutofillField& field) {
    return std::ranges::any_of(
        edm->GetEntityInstances(), [&](const EntityInstance& entity) {
          if (!fields_and_types) {
            fields_and_types = DetermineAttributeTypes(form.fields());
          }
          auto it = fields_and_types->find(field.section());
          if (it == fields_and_types->end()) {
            return false;
          }
          auto jt = it->second.find(entity.type());
          if (jt == it->second.end()) {
            return false;
          }
          auto kt =
              std::ranges::find(jt->second, field.global_id(),
                                [](const AutofillFieldWithAttributeType& f) {
                                  return f.field->global_id();
                                });
          if (kt == jt->second.end()) {
            return false;
          }
          AttributeType type = kt->type;
          return entity.attribute(type).has_value();
        });
  };

  std::vector<FieldGlobalId> fillable_fields;
  for (const auto& field : form.fields()) {
    if (is_fillable(*field)) {
      fillable_fields.push_back(field->global_id());
    }
  }
  return fillable_fields;
}

std::u16string GetFillValueForEntity(
    const EntityInstance& entity,
    base::span<const AutofillFieldWithAttributeType> fields_and_types,
    const AutofillField& field,
    mojom::ActionPersistence action_persistence,
    const std::string& app_locale,
    AddressNormalizer* address_normalizer) {
  auto attribute = [&]() -> base::optional_ref<const AttributeInstance> {
    auto it = std::ranges::find(fields_and_types, field.global_id(),
                                [](const AutofillFieldWithAttributeType& f) {
                                  return f.field->global_id();
                                });
    if (it == fields_and_types.end()) {
      return std::nullopt;
    }
    AttributeType type = it->type;
    if (type.entity_type() != entity.type()) {
      return std::nullopt;
    }
    return entity.attribute(it->type);
  }();

  if (!attribute) {
    return u"";
  }

  std::u16string fill_value =
      field.IsSelectElement()
          ? GetValueForSelectControl(*attribute, field, app_locale,
                                     address_normalizer)
          : GetValueForInput(*attribute, field, app_locale);

  const bool should_obfuscate =
      action_persistence != mojom::ActionPersistence::kFill &&
      !field.IsSelectElement() && attribute->type().is_obfuscated();

  // TODO(crbug.com/394011769): Investigate whether the obfuscation should
  // should include some of the attribute's value, e.g. the last x characters.
  return should_obfuscate ? GetObfuscatedValue(fill_value) : fill_value;
}

}  // namespace autofill
