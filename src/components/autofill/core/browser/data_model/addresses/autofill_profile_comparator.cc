// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/data_model/addresses/autofill_profile_comparator.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "base/i18n/char_iterator.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/country_type.h"
#include "components/autofill/core/browser/data_model/addresses/address.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_structured_address_component.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_structured_address_name.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_structured_address_utils.h"
#include "components/autofill/core/browser/data_model/addresses/contact_info.h"
#include "components/autofill/core/browser/data_model/transliterator.h"
#include "components/autofill/core/browser/data_quality/autofill_data_util.h"
#include "components/autofill/core/browser/field_type_utils.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/geo/autofill_country.h"
#include "components/autofill/core/browser/metrics/autofill_metrics.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_l10n_util.h"
#include "third_party/libphonenumber/phonenumber_api.h"

using i18n::phonenumbers::PhoneNumberUtil;

namespace autofill {
namespace {

constexpr char16_t kSpace[] = u" ";

std::ostream& operator<<(std::ostream& os,
                         const ::i18n::phonenumbers::PhoneNumber& n) {
  os << "country_code: " << n.country_code() << " "
     << "national_number: " << n.national_number();
  if (n.has_italian_leading_zero()) {
    os << " italian_leading_zero: " << n.italian_leading_zero();
  }
  if (n.has_number_of_leading_zeros()) {
    os << " number_of_leading_zeros: " << n.number_of_leading_zeros();
  }
  if (n.has_raw_input()) {
    os << " raw_input: \"" << n.raw_input() << "\"";
  }
  return os;
}

bool IsPunctuationOrWhitespace(const int8_t character) {
  switch (character) {
    // Punctuation
    case U_DASH_PUNCTUATION:
    case U_START_PUNCTUATION:
    case U_END_PUNCTUATION:
    case U_CONNECTOR_PUNCTUATION:
    case U_OTHER_PUNCTUATION:
    // Whitespace
    case U_CONTROL_CHAR:  // To escape the '\n' character.
    case U_SPACE_SEPARATOR:
    case U_LINE_SEPARATOR:
    case U_PARAGRAPH_SEPARATOR:
      return true;

    default:
      return false;
  }
}

// Iterator for a string that processes punctuation and white space according to
// `collapse_skippable_`.
class NormalizingIterator {
 public:
  NormalizingIterator(
      std::u16string_view text,
      AutofillProfileComparator::WhitespaceSpec whitespace_spec);
  ~NormalizingIterator();

  // Advances to the next non-skippable character in the string. Whether a
  // punctuation or white space character is skippable depends on
  // `collapse_skippable_`. Returns false if the end of the string has been
  // reached.
  void Advance();

  // Returns true if the iterator has reached the end of the string.
  bool End();

  // Returns true if the iterator ends in skippable characters or if the
  // iterator has reached the end of the string. Has the side effect of
  // advancing the iterator to either the first skippable character or to the
  // end of the string.
  bool EndsInSkippableCharacters();

  // Returns the next character that should be considered.
  int32_t GetNextChar();

 private:
  // When `collapse_skippable_` is false, this member is initialized to false
  // and is not updated.
  //
  // When `collapse_skippable_` is true, this member indicates whether the
  // previous character was punctuation or white space so that one or more
  // consecutive embedded punctuation and white space characters can be
  // collapsed to a single white space.
  bool previous_was_skippable_ = false;

  // True if punctuation and white space within the string should be collapsed
  // to a single white space.
  bool collapse_skippable_;

  base::i18n::UTF16CharIterator iter_;
};

NormalizingIterator::NormalizingIterator(
    std::u16string_view text,
    AutofillProfileComparator::WhitespaceSpec whitespace_spec)
    : collapse_skippable_(whitespace_spec ==
                          AutofillProfileComparator::RETAIN_WHITESPACE),
      iter_(text) {
  int32_t character = iter_.get();

  while (!iter_.end() && IsPunctuationOrWhitespace(u_charType(character))) {
    iter_.Advance();
    character = iter_.get();
  }
}

NormalizingIterator::~NormalizingIterator() = default;

void NormalizingIterator::Advance() {
  if (!iter_.Advance()) {
    return;
  }

  while (!End()) {
    int32_t character = iter_.get();
    bool is_punctuation_or_whitespace =
        IsPunctuationOrWhitespace(u_charType(character));

    if (!is_punctuation_or_whitespace) {
      previous_was_skippable_ = false;
      return;
    }

    if (is_punctuation_or_whitespace && !previous_was_skippable_ &&
        collapse_skippable_) {
      // Punctuation or white space within the string was found, e.g. the "," in
      // the string "Hotel Schmotel, 3 Old Rd", and is after a non-skippable
      // character.
      previous_was_skippable_ = true;
      return;
    }

    iter_.Advance();
  }
}

bool NormalizingIterator::End() {
  return iter_.end();
}

bool NormalizingIterator::EndsInSkippableCharacters() {
  while (!End()) {
    int32_t character = iter_.get();
    if (!IsPunctuationOrWhitespace(u_charType(character))) {
      return false;
    }
    iter_.Advance();
  }
  return true;
}

int32_t NormalizingIterator::GetNextChar() {
  if (End()) {
    return 0;
  }

  if (previous_was_skippable_) {
    return ' ';
  }

  return iter_.get();
}

// Helper function retrieving given name of `name_type` type from `profile`.
// Function leverages `AddressComponent::GetValueForComparisonForType()` which
// requires name from `other_profile` that the name is compared against.
const std::u16string GetNameForComparison(
    const AutofillProfile& profile,
    const AddressCountryCode& common_country_code,
    FieldType name_type) {
  switch (name_type) {
    case ALTERNATIVE_FULL_NAME:
      return profile.GetNameInfo()
          .GetStructuredAlternativeName()
          .GetValueForComparisonForType(name_type, common_country_code);
    case NAME_FULL:
      // Using GetValue() directly to prevent normalization that would remove
      // diactrics. Normalization happens in
      // `AutofillProfileComparator::Compare()`.
      return profile.GetNameInfo().GetStructuredName().GetValue();
    default:
      NOTREACHED();
  }
}

}  // namespace

AutofillProfileComparator::AutofillProfileComparator(
    std::string_view app_locale)
    : app_locale_(app_locale.data(), app_locale.size()) {}

AutofillProfileComparator::~AutofillProfileComparator() = default;

std::vector<ProfileValueDifference>
AutofillProfileComparator::GetProfileDifference(
    const AutofillProfile& first_profile,
    const AutofillProfile& second_profile,
    FieldTypeSet types,
    const std::string& app_locale) {
  std::vector<ProfileValueDifference> difference;
  difference.reserve(types.size());

  for (auto type : types) {
    const std::u16string& first_value = first_profile.GetInfo(type, app_locale);
    const std::u16string& second_value =
        second_profile.GetInfo(type, app_locale);
    if (first_value != second_value) {
      difference.emplace_back(
          ProfileValueDifference{type, first_value, second_value});
    }
  }
  return difference;
}

std::vector<ProfileValueDifference>
AutofillProfileComparator::GetSettingsVisibleProfileDifference(
    const AutofillProfile& first_profile,
    const AutofillProfile& second_profile,
    const std::string& app_locale) {
  FieldTypeSet types = first_profile.GetUserVisibleTypes();
  types.insert_all(second_profile.GetUserVisibleTypes());
  return GetProfileDifference(first_profile, second_profile, types, app_locale);
}

bool AutofillProfileComparator::Compare(
    std::u16string_view text1,
    std::u16string_view text2,
    WhitespaceSpec whitespace_spec,
    std::optional<FieldType> type,
    AddressCountryCode country_code_1,
    AddressCountryCode country_code_2) const {
  if (text1.empty() && text2.empty()) {
    return true;
  }
  // We transliterate the entire text as it's non-trivial to go character
  // by character (eg. a "ß" is transliterated to "ss").
  std::u16string normalized_text1 =
      RemoveDiacriticsAndConvertToLowerCase(text1, country_code_1);
  std::u16string normalized_text2 =
      RemoveDiacriticsAndConvertToLowerCase(text2, country_code_2);

  // TODO(crbug.com/359768803): Extract alternative name transliteration and
  // remove `type` parameter. Japanese alternative names are stored in Hiragana
  // only. We transliterate Katarana to ensure correct comparison.
  if (type.has_value() && IsAlternativeNameType(type.value()) &&
      base::FeatureList::IsEnabled(
          features::kAutofillSupportPhoneticNameForJP)) {
    normalized_text1 = TransliterateAlternativeName(normalized_text1);
    normalized_text2 = TransliterateAlternativeName(normalized_text2);
  }

  NormalizingIterator normalizing_iter1{normalized_text1, whitespace_spec};
  NormalizingIterator normalizing_iter2{normalized_text2, whitespace_spec};

  while (!normalizing_iter1.End() && !normalizing_iter2.End()) {
    if (normalizing_iter1.GetNextChar() != normalizing_iter2.GetNextChar()) {
      return false;
    }
    normalizing_iter1.Advance();
    normalizing_iter2.Advance();
  }

  if (normalizing_iter1.EndsInSkippableCharacters() &&
      normalizing_iter2.EndsInSkippableCharacters()) {
    return true;
  }

  return false;
}

bool AutofillProfileComparator::HasOnlySkippableCharacters(
    std::u16string_view text) const {
  if (text.empty()) {
    return true;
  }

  return NormalizingIterator(text,
                             AutofillProfileComparator::DISCARD_WHITESPACE)
      .End();
}

// static
std::u16string AutofillProfileComparator::NormalizeForComparison(
    std::u16string_view text,
    AutofillProfileComparator::WhitespaceSpec whitespace_spec,
    const AddressCountryCode& country_code) {
  // This algorithm is not designed to be perfect, we could get arbitrarily
  // fancy here trying to canonicalize address lines. Instead, this is designed
  // to handle common cases for all types of data (addresses and names) without
  // needing domain-specific logic.
  //
  // 1. Convert punctuation to spaces and normalize all whitespace to spaces if
  //    `whitespace_spec` is RETAIN_WHITESPACE.
  //    This will convert "Mid-Island Plz." -> "Mid Island Plz " (the trailing
  //    space will be trimmed off outside of the end of the loop).
  //
  // 2. Collapse consecutive punctuation/whitespace characters to a single
  //    space. We pretend the string has already started with whitespace in
  //    order to trim leading spaces.
  //    If DISCARD_WHITESPACE was picked, remove all the punctuation/whitespace
  //    characters altogether.
  //
  // 3. Remove diacritics (accents and other non-spacing marks) and perform
  //    case folding to lower-case.
  std::u16string result;
  result.reserve(text.length());
  const bool retain_whitespace = whitespace_spec == RETAIN_WHITESPACE;
  bool previous_was_whitespace = true;
  for (base::i18n::UTF16CharIterator iter(text); !iter.end(); iter.Advance()) {
    if (!IsPunctuationOrWhitespace(u_charType(iter.get()))) {
      previous_was_whitespace = false;
      base::WriteUnicodeCharacter(iter.get(), &result);
    } else if (retain_whitespace && !previous_was_whitespace) {
      result.push_back(' ');
      previous_was_whitespace = true;
    }
  }
  // Trim off trailing whitespace if we left one.
  if (previous_was_whitespace && !result.empty()) {
    result.resize(result.size() - 1);
  }

  return RemoveDiacriticsAndConvertToLowerCase(result, country_code);
}

bool AutofillProfileComparator::AreMergeable(const AutofillProfile& p1,
                                             const AutofillProfile& p2) const {
  // Sorted in order to relative expense of the tests to fail early and cheaply
  // if possible.
  // Emails go last, since their comparison logic triggers ICU code, which can
  // trigger the loading of locale-specific rules.
  DVLOG(1) << "Comparing profiles:\np1 = " << p1 << "\np2 = " << p2;

  if (!HaveMergeableCompanyNames(p1, p2)) {
    DVLOG(1) << "Different email company names.";
    return false;
  }

  if (!HaveMergeablePhoneNumbers(p1, p2)) {
    DVLOG(1) << "Different phone numbers.";
    return false;
  }

  if (!HaveMergeableNames(p1, p2)) {
    DVLOG(1) << "Different names.";
    return false;
  }

  if (!HaveMergeableAlternativeNames(p1, p2)) {
    DVLOG(1) << "Different alternative names.";
    return false;
  }

  if (!HaveMergeableAddresses(p1, p2)) {
    DVLOG(1) << "Different addresses.";
    return false;
  }

  if (!HaveMergeableEmailAddresses(p1, p2)) {
    DVLOG(1) << "Different email addresses.";
    return false;
  }

  DVLOG(1) << "Profiles are mergeable.";
  return true;
}

bool AutofillProfileComparator::MergeNames(const AutofillProfile& new_profile,
                                           const AutofillProfile& old_profile,
                                           NameInfo& name_info) const {
  DCHECK(HaveMergeableNames(new_profile, old_profile));
  DCHECK(HaveMergeableAlternativeNames(new_profile, old_profile));

  auto name_full = std::make_unique<NameFull>();
  auto alternative_full_name = std::make_unique<AlternativeFullName>();

  // TODO(crbug.com/375383124): Update `MergeNamesImpl` to provide meaningful
  // return values.
  MergeNamesImpl(new_profile, old_profile, NAME_FULL, *name_full);
  if (base::FeatureList::IsEnabled(
          features::kAutofillSupportPhoneticNameForJP)) {
    MergeNamesImpl(new_profile, old_profile, ALTERNATIVE_FULL_NAME,
                   *alternative_full_name);
  }
  name_info = NameInfo(std::move(name_full), std::move(alternative_full_name));
  return true;
}

bool AutofillProfileComparator::IsNameVariantOf(
    const std::u16string& full_name_1,
    const std::u16string& full_name_2) const {
  data_util::NameParts name_1_parts = data_util::SplitName(full_name_1);

  // Build the variants of full_name_1`s given, middle and family names.
  //
  // TODO(rogerm): Figure out whether or not we should break apart a compound
  // family name into variants (crbug/619051)
  const std::set<std::u16string> given_name_variants =
      GetNamePartVariants(name_1_parts.given);
  const std::set<std::u16string> middle_name_variants =
      GetNamePartVariants(name_1_parts.middle);
  std::u16string_view family_name = name_1_parts.family;

  // Iterate over all full name variants of profile 2 and see if any of them
  // match the full name from profile 1.
  for (const auto& given_name : given_name_variants) {
    for (const auto& middle_name : middle_name_variants) {
      std::u16string candidate = base::CollapseWhitespace(
          base::JoinString({given_name, middle_name, family_name}, kSpace),
          true);
      if (candidate == full_name_2) {
        return true;
      }
    }
  }

  // Also check if the name is just composed of the user's initials. For
  // example, "thomas jefferson miller" could be composed as "tj miller".
  if (!name_1_parts.given.empty() && !name_1_parts.middle.empty()) {
    std::u16string initials;
    initials.push_back(name_1_parts.given[0]);
    initials.push_back(name_1_parts.middle[0]);
    std::u16string candidate = base::CollapseWhitespace(
        base::JoinString({initials, family_name}, kSpace), true);
    if (candidate == full_name_2) {
      return true;
    }
  }

  // There was no match found.
  return false;
}

bool AutofillProfileComparator::MergeEmailAddresses(
    const AutofillProfile& new_profile,
    const AutofillProfile& old_profile,
    EmailInfo& email_info) const {
  DCHECK(HaveMergeableEmailAddresses(new_profile, old_profile));

  const std::u16string& e1 = new_profile.GetInfo(EMAIL_ADDRESS, app_locale_);
  const std::u16string& e2 = old_profile.GetInfo(EMAIL_ADDRESS, app_locale_);
  const std::u16string* best = nullptr;

  if (e1.empty()) {
    best = &e2;
  } else if (e2.empty()) {
    best = &e1;
  } else {
    best = old_profile.usage_history().use_date() >
                   new_profile.usage_history().use_date()
               ? &e2
               : &e1;
  }

  email_info.SetInfo(EMAIL_ADDRESS, *best, app_locale_);
  return true;
}

bool AutofillProfileComparator::MergeCompanyNames(
    const AutofillProfile& new_profile,
    const AutofillProfile& old_profile,
    CompanyInfo& company_info) const {
  const std::u16string& c1 = new_profile.GetInfo(COMPANY_NAME, app_locale_);
  const std::u16string& c2 = old_profile.GetInfo(COMPANY_NAME, app_locale_);
  const std::u16string* best = nullptr;

  DCHECK(HaveMergeableCompanyNames(new_profile, old_profile))
      << "Company names are not mergeable: '" << c1 << "' vs '" << c2 << "'";

  CompareTokensResult result =
      CompareTokens(NormalizeForComparison(c1), NormalizeForComparison(c2));
  switch (result) {
    case DIFFERENT_TOKENS:
    default:
      NOTREACHED() << "Unexpected mismatch: '" << c1 << "' vs '" << c2 << "'";
    case S1_CONTAINS_S2:
      best = &c1;
      break;
    case S2_CONTAINS_S1:
      best = &c2;
      break;
    case SAME_TOKENS:
      best = old_profile.usage_history().use_date() >
                     new_profile.usage_history().use_date()
                 ? &c2
                 : &c1;
      break;
  }
  company_info.SetInfo(COMPANY_NAME, *best, app_locale_);
  return true;
}

bool AutofillProfileComparator::MergePhoneNumbers(
    const AutofillProfile& new_profile,
    const AutofillProfile& old_profile,
    PhoneNumber& phone_number) const {
  const FieldType kWholePhoneNumber = PHONE_HOME_WHOLE_NUMBER;
  const std::u16string& s1 = new_profile.GetRawInfo(kWholePhoneNumber);
  const std::u16string& s2 = old_profile.GetRawInfo(kWholePhoneNumber);

  DCHECK(HaveMergeablePhoneNumbers(new_profile, old_profile))
      << "Phone numbers are not mergeable: '" << s1 << "' vs '" << s2 << "'";

  if (HasOnlySkippableCharacters(s1) && HasOnlySkippableCharacters(s2)) {
    phone_number.SetRawInfo(kWholePhoneNumber, std::u16string());
  }

  if (HasOnlySkippableCharacters(s1)) {
    phone_number.SetRawInfo(kWholePhoneNumber, s2);
    return true;
  }

  if (HasOnlySkippableCharacters(s2) || s1 == s2) {
    phone_number.SetRawInfo(kWholePhoneNumber, s1);
    return true;
  }

  // Figure out a country code hint.
  // TODO(crbug.com/40221178) `GetNonEmptyOf()` prefers `new_profile` in case
  // both are non empty.
  std::string region = base::UTF16ToUTF8(GetNonEmptyOf(
      new_profile, old_profile, AutofillType(HtmlFieldType::kCountryCode)));
  if (region.empty()) {
    region = AutofillCountry::CountryCodeForLocale(app_locale_);
  }

  // Parse the phone numbers.
  PhoneNumberUtil* phone_util = PhoneNumberUtil::GetInstance();

  ::i18n::phonenumbers::PhoneNumber n1;
  if (phone_util->ParseAndKeepRawInput(base::UTF16ToUTF8(s1), region, &n1) !=
      PhoneNumberUtil::NO_PARSING_ERROR) {
    return false;
  }

  ::i18n::phonenumbers::PhoneNumber n2;
  if (phone_util->ParseAndKeepRawInput(base::UTF16ToUTF8(s2), region, &n2) !=
      PhoneNumberUtil::NO_PARSING_ERROR) {
    return false;
  }

  // `country_code()` defaults to the provided `region`. But if one of the
  // numbers is in international format, we should prefer that country code.
  auto HasInternationalCountryCode =
      [](const ::i18n::phonenumbers::PhoneNumber& number) {
        return number.country_code_source() !=
               ::i18n::phonenumbers::PhoneNumber::FROM_DEFAULT_COUNTRY;
      };

  ::i18n::phonenumbers::PhoneNumber merged_number;
  // There are three cases for country codes:
  // - Both numbers are in international format, so because the numbers are
  //   mergeable, they are equal.
  // - Both are not in international format, so their country codes both default
  //   to `region`.
  // - One of them is in international format, so we prefer that country code.
  DCHECK(HasInternationalCountryCode(n1) != HasInternationalCountryCode(n2) ||
         n1.country_code() == n2.country_code());
  merged_number.set_country_code(
      HasInternationalCountryCode(n1) ? n1.country_code() : n2.country_code());
  merged_number.set_national_number(
      std::max(n1.national_number(), n2.national_number()));
  if (n1.has_italian_leading_zero() || n2.has_italian_leading_zero()) {
    merged_number.set_italian_leading_zero(n1.italian_leading_zero() ||
                                           n2.italian_leading_zero());
  }
  if (n1.has_number_of_leading_zeros() || n2.has_number_of_leading_zeros()) {
    merged_number.set_number_of_leading_zeros(
        std::max(n1.number_of_leading_zeros(), n2.number_of_leading_zeros()));
  }

  // Format the `merged_number` in international format only if at least one
  // of the country codes was derived from the number itself. This is done
  // consistently with `::autofill::i18n::FormatValidatedNumber()` and
  // `::autofill::i18n::ParsePhoneNumber()`, which backs the `PhoneNumber`
  // implementation.
  PhoneNumberUtil::PhoneNumberFormat format =
      HasInternationalCountryCode(n1) || HasInternationalCountryCode(n2)
          ? PhoneNumberUtil::INTERNATIONAL
          : PhoneNumberUtil::NATIONAL;

  std::string new_number;
  phone_util->Format(merged_number, format, &new_number);

  DVLOG(2) << "n1 = {" << n1 << "}";
  DVLOG(2) << "n2 = {" << n2 << "}";
  DVLOG(2) << "merged_number = {" << merged_number << "}";
  DVLOG(2) << "new_number = \"" << new_number << "\"";

  // Check if it's a North American number that's missing the area code.
  // Libphonenumber doesn't know how to format short numbers; it will still
  // include the country code prefix.
  if (merged_number.country_code() == 1 &&
      merged_number.national_number() <= 9999999 &&
      new_number.starts_with("+1")) {
    size_t offset = 2;  // The char just after "+1".
    while (offset < new_number.size() &&
           base::IsAsciiWhitespace(new_number[offset])) {
      ++offset;
    }
    new_number = new_number.substr(offset);
  }

  phone_number.SetRawInfo(kWholePhoneNumber, base::UTF8ToUTF16(new_number));
  return true;
}

bool AutofillProfileComparator::MergeAddresses(
    const AutofillProfile& new_profile,
    const AutofillProfile& old_profile,
    Address& address) const {
  DCHECK(HaveMergeableAddresses(new_profile, old_profile));

  address = old_profile.GetAddress();
  return address.MergeStructuredAddress(
      new_profile.GetAddress(), old_profile.usage_history().use_date() <
                                    new_profile.usage_history().use_date());
}

std::optional<FieldTypeSet>
AutofillProfileComparator::NonMergeableSettingVisibleTypes(
    const AutofillProfile& a,
    const AutofillProfile& b) const {
  if (a.GetAddressCountryCode() != b.GetAddressCountryCode()) {
    return std::nullopt;
  }
  FieldTypeSet setting_visible_types = a.GetUserVisibleTypes();
  FieldTypeSet non_mergeable_types;
  auto maybe_add_type = [&](FieldType type, bool is_mergeable) {
    // Ensure that `type` is actually a setting-visible type.
    CHECK_EQ(setting_visible_types.erase(type), 1u);
    if (!is_mergeable) {
      non_mergeable_types.insert(type);
    }
  };
  // For most setting-visible types, a HaveMergeable* function exists. If these
  // types ever become non-settings visible, the check in `maybe_add_type` will
  // fail in the unittest.
  maybe_add_type(NAME_FULL, HaveMergeableNames(a, b));
  if (setting_visible_types.contains(ALTERNATIVE_FULL_NAME)) {
    maybe_add_type(ALTERNATIVE_FULL_NAME, HaveMergeableAlternativeNames(a, b));
  }
  maybe_add_type(COMPANY_NAME, HaveMergeableCompanyNames(a, b));
  maybe_add_type(PHONE_HOME_WHOLE_NUMBER, HaveMergeablePhoneNumbers(a, b));
  maybe_add_type(EMAIL_ADDRESS, HaveMergeableEmailAddresses(a, b));
  // Now, only address-related types remain in `setting_visible_types`. Using
  // `HaveMergeableAddresses()` is not fine-grained enough, since multiple
  // address types are setting-visible (e.g. city, zip, etc). Verify differences
  // in the corresponding subtrees manually.
  for (FieldType address_type : setting_visible_types) {
    CHECK_EQ(GroupTypeOfFieldType(address_type), FieldTypeGroup::kAddress);
    if (!a.GetAddress().IsAddressFieldSettingAccessible(address_type)) {
      // `address_type` is not applicable to `a`'s country (= `b`'s country).
      continue;
    }
    if (!a.GetAddress().IsStructuredAddressMergeableForType(address_type,
                                                            b.GetAddress())) {
      non_mergeable_types.insert(address_type);
    }
  }
  return non_mergeable_types;
}

bool AutofillProfileComparator::ProfilesHaveDifferentSettingsVisibleValues(
    const AutofillProfile& p1,
    const AutofillProfile& p2,
    const std::string& app_locale) {
  if (p1.GetUserVisibleTypes() != p2.GetUserVisibleTypes()) {
    return false;
  }
  // Return true if at least one value corresponding to the settings visible
  // types is different between the two profiles.
  return std::ranges::any_of(p1.GetUserVisibleTypes(), [&](FieldType type) {
    if (IsAlternativeNameType(type) &&
        base::FeatureList::IsEnabled(
            features::kAutofillSupportPhoneticNameForJP)) {
      // Consider two alternative names that differ only in the character set
      // equal.
      const AddressCountryCode common_country_code =
          AddressComponent::GetCommonCountry(p1.GetAddressCountryCode(),
                                             p2.GetAddressCountryCode());
      return p1.GetNameInfo()
                 .GetStructuredAlternativeName()
                 .GetValueForComparisonForType(type, common_country_code) !=
             p2.GetNameInfo()
                 .GetStructuredAlternativeName()
                 .GetValueForComparisonForType(type, common_country_code);
    }
    return p1.GetInfo(type, app_locale) != p2.GetInfo(type, app_locale);
  });
}

// static
std::set<std::u16string_view> AutofillProfileComparator::UniqueTokens(
    std::u16string_view s) {
  std::vector<std::u16string_view> tokens = base::SplitStringPiece(
      s, kSpace, base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  return std::set<std::u16string_view>(tokens.begin(), tokens.end());
}

// static
AutofillProfileComparator::CompareTokensResult
AutofillProfileComparator::CompareTokens(std::u16string_view s1,
                                         std::u16string_view s2) {
  // Note: std::include() expects the items in each range to be in sorted order,
  // hence the use of std::set<> instead of std::unordered_set<>.
  std::set<std::u16string_view> t1 = UniqueTokens(s1);
  std::set<std::u16string_view> t2 = UniqueTokens(s2);

  // Does `s1` contain all of the tokens in `s2`? As a special case, return 0 if
  // the two sets are exactly the same.
  if (std::includes(t1.begin(), t1.end(), t2.begin(), t2.end())) {
    return t1.size() == t2.size() ? SAME_TOKENS : S1_CONTAINS_S2;
  }

  // Does `s2` contain all of the tokens in `s1`?
  if (std::includes(t2.begin(), t2.end(), t1.begin(), t1.end())) {
    return S2_CONTAINS_S1;
  }

  // Neither string contains all of the tokens from the other.
  return DIFFERENT_TOKENS;
}

std::u16string AutofillProfileComparator::GetNonEmptyOf(
    const AutofillProfile& p1,
    const AutofillProfile& p2,
    AutofillType t) const {
  const std::u16string& s1 = p1.GetInfo(t, app_locale_);
  if (!s1.empty()) {
    return s1;
  }
  return p2.GetInfo(t, app_locale_);
}

// static
std::set<std::u16string> AutofillProfileComparator::GetNamePartVariants(
    const std::u16string& name_part) {
  const size_t kMaxSupportedSubNames = 8;

  std::vector<std::u16string_view> sub_names = base::SplitStringPiece(
      name_part, kSpace, base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Limit the number of sub-names we support (to constrain memory usage);
  if (sub_names.size() > kMaxSupportedSubNames) {
    return {name_part};
  }

  // Start with the empty string as a variant.
  std::set<std::u16string> variants = {{}};

  // For each sub-name, add a variant of all the already existing variants that
  // appends this sub-name and one that appends the initial of this sub-name.
  // Duplicates will be discarded when they're added to the variants set.
  for (const auto& sub_name : sub_names) {
    if (sub_name.empty()) {
      continue;
    }
    std::vector<std::u16string> new_variants;
    for (const std::u16string& variant : variants) {
      new_variants.push_back(base::CollapseWhitespace(
          base::JoinString({variant, sub_name}, kSpace), true));
      new_variants.push_back(base::CollapseWhitespace(
          base::JoinString({variant, sub_name.substr(0, 1)}, kSpace), true));
    }
    variants.insert(new_variants.begin(), new_variants.end());
  }

  // As a common case, also add the variant that just concatenates all of the
  // initials.
  std::u16string initials;
  for (const auto& sub_name : sub_names) {
    if (sub_name.empty()) {
      continue;
    }
    initials.push_back(sub_name[0]);
  }
  variants.insert(initials);

  // And, we're done.
  return variants;
}

bool AutofillProfileComparator::HaveMergeableNames(
    const AutofillProfile& p1,
    const AutofillProfile& p2) const {
  return AreNamesMergeable(p1, p2, NAME_FULL);
}

bool AutofillProfileComparator::HaveMergeableAlternativeNames(
    const AutofillProfile& p1,
    const AutofillProfile& p2) const {
  if (!base::FeatureList::IsEnabled(
          features::kAutofillSupportPhoneticNameForJP)) {
    return true;
  }

  return AreNamesMergeable(p1, p2, ALTERNATIVE_FULL_NAME);
}

bool AutofillProfileComparator::HaveMergeableEmailAddresses(
    const AutofillProfile& p1,
    const AutofillProfile& p2) const {
  const std::u16string& email_1 = p1.GetInfo(EMAIL_ADDRESS, app_locale_);
  const std::u16string& email_2 = p2.GetInfo(EMAIL_ADDRESS, app_locale_);
  return email_1.empty() || email_2.empty() ||
         l10n::CaseInsensitiveCompare().StringsEqual(email_1, email_2);
}

bool AutofillProfileComparator::HaveMergeableCompanyNames(
    const AutofillProfile& p1,
    const AutofillProfile& p2) const {
  const std::u16string& company_name_1 = p1.GetInfo(COMPANY_NAME, app_locale_);
  const std::u16string& company_name_2 = p2.GetInfo(COMPANY_NAME, app_locale_);
  return HasOnlySkippableCharacters(company_name_1) ||
         HasOnlySkippableCharacters(company_name_2) ||
         CompareTokens(NormalizeForComparison(company_name_1),
                       NormalizeForComparison(company_name_2)) !=
             DIFFERENT_TOKENS;
}

bool AutofillProfileComparator::HaveMergeablePhoneNumbers(
    const AutofillProfile& p1,
    const AutofillProfile& p2) const {
  // We work with the raw phone numbers to avoid losing any helpful information
  // as we parse.
  const std::u16string& raw_phone_1 = p1.GetRawInfo(PHONE_HOME_WHOLE_NUMBER);
  const std::u16string& raw_phone_2 = p2.GetRawInfo(PHONE_HOME_WHOLE_NUMBER);

  // Are the two phone numbers trivially mergeable?
  if (HasOnlySkippableCharacters(raw_phone_1) ||
      HasOnlySkippableCharacters(raw_phone_2) || raw_phone_1 == raw_phone_2) {
    return true;
  }

  // TODO(rogerm): Modify ::autofill::i18n::PhoneNumbersMatch to support
  // SHORT_NSN_MATCH and just call that instead of accessing the underlying
  // utility library directly?

  // Parse and compare the phone numbers.
  // The phone number util library needs the numbers in utf8.
  PhoneNumberUtil* phone_util = PhoneNumberUtil::GetInstance();
  switch (phone_util->IsNumberMatchWithTwoStrings(
      base::UTF16ToUTF8(raw_phone_1), base::UTF16ToUTF8(raw_phone_2))) {
    case PhoneNumberUtil::SHORT_NSN_MATCH:
    case PhoneNumberUtil::NSN_MATCH:
    case PhoneNumberUtil::EXACT_MATCH:
      return true;
    case PhoneNumberUtil::INVALID_NUMBER:
    case PhoneNumberUtil::NO_MATCH:
      return false;
    default:
      NOTREACHED();
  }
}

bool AutofillProfileComparator::HaveMergeableAddresses(
    const AutofillProfile& p1,
    const AutofillProfile& p2) const {
  return p2.GetAddress().IsStructuredAddressMergeable(p1.GetAddress());
}

bool AutofillProfileComparator::AreNamesMergeable(const AutofillProfile& p1,
                                                  const AutofillProfile& p2,
                                                  FieldType name_type) const {
  DCHECK(name_type == NAME_FULL || name_type == ALTERNATIVE_FULL_NAME);
  const AddressCountryCode common_country_code =
      AddressComponent::GetCommonCountry(p1.GetAddressCountryCode(),
                                         p2.GetAddressCountryCode());
  const std::u16string name_1 =
      GetNameForComparison(p1, common_country_code, name_type);
  const std::u16string name_2 =
      GetNameForComparison(p2, common_country_code, name_type);

  if (HasOnlySkippableCharacters(name_1) ||
      HasOnlySkippableCharacters(name_2) ||
      Compare(name_1, name_2, DISCARD_WHITESPACE, name_type,
              p1.GetAddressCountryCode(), p2.GetAddressCountryCode())) {
    return true;
  }

  // If the two names are just a permutation of each other, they are mergeable
  // for structured names.
  if (AreStringTokenEquivalent(name_1, name_2)) {
    return true;
  }

  std::u16string canon_full_name_1 = NormalizeForComparison(
      name_1, RETAIN_WHITESPACE, p1.GetAddressCountryCode());
  std::u16string canon_full_name_2 = NormalizeForComparison(
      name_2, RETAIN_WHITESPACE, p2.GetAddressCountryCode());

  // Is it reasonable to merge the names from `p1` and `p2`?
  bool result = IsNameVariantOf(canon_full_name_1, canon_full_name_2) ||
                IsNameVariantOf(canon_full_name_2, canon_full_name_1);
  return result;
}

void AutofillProfileComparator::MergeNamesImpl(
    const AutofillProfile& new_profile,
    const AutofillProfile& old_profile,
    FieldType name_type,
    AddressComponent& name_component) const {
  DCHECK(name_type == NAME_FULL || name_type == ALTERNATIVE_FULL_NAME);

  const AddressCountryCode common_country_code =
      AddressComponent::GetCommonCountry(new_profile.GetAddressCountryCode(),
                                         old_profile.GetAddressCountryCode());
  const std::u16string name_1 = NormalizeForComparison(
      GetNameForComparison(new_profile, common_country_code, name_type),
      RETAIN_WHITESPACE, new_profile.GetAddressCountryCode());
  const std::u16string name_2 = NormalizeForComparison(
      GetNameForComparison(old_profile, common_country_code, name_type),
      RETAIN_WHITESPACE, old_profile.GetAddressCountryCode());

  // At this state it is already determined that the two names are mergeable.
  // This can mean of of the following things:
  // * One name is empty. In this scenario the non-empty name is used.
  // * The names are token equivalent: In this scenario a merge of the tree
  // structure should be possible.
  // * One name is a variant of the other. In this scenario, use the non-variant
  // name.
  // First, set info to the original profile.
  name_component.CopyFrom(*old_profile.GetNameInfo().GetRootForType(name_type));
  // If the name of the `new_profile` is empty, just keep the state of
  // `old_profile`.
  if (HasOnlySkippableCharacters(name_1)) {
    return;
  }
  // Vice versa set name to the one of `new_profile` if `old_profile` has an
  // empty name
  if (HasOnlySkippableCharacters(name_2)) {
    name_component.CopyFrom(
        *new_profile.GetNameInfo().GetRootForType(name_type));
    return;
  }
  // Try to apply a direct merging.
  if (name_component.MergeWithComponent(
          *new_profile.GetNameInfo().GetRootForType(name_type))) {
    return;
  }
  // If the name in `old_profile` is a variant of `new_profile` use the one in
  // `new_profile`.
  if (IsNameVariantOf(name_1, name_2)) {
    name_component.CopyFrom(
        *new_profile.GetNameInfo().GetRootForType(name_type));
  } else {
    name_component.CopyFrom(
        *old_profile.GetNameInfo().GetRootForType(name_type));
  }
}

}  // namespace autofill
