// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/field_types.h"

#include <string_view>

#include "base/containers/fixed_flat_map.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"

namespace autofill {

std::ostream& operator<<(std::ostream& o, FieldTypeSet field_type_set) {
  o << "[";
  bool first = true;
  for (const auto type : field_type_set) {
    if (!first) {
      o << ", ";
    } else {
      first = false;
    }
    o << FieldTypeToStringView(type);
  }
  o << "]";
  return o;
}

// This map should be extended for every added FieldType.
// You are free to add or remove the String representation of FieldType,
// but don't change any existing values, Android WebView presents them to
// Autofill Service as part of APIs.
static constexpr auto kTypeNameToFieldType =
    base::MakeFixedFlatMap<std::string_view, FieldType>(
        {{"NO_SERVER_DATA", NO_SERVER_DATA},
         {"UNKNOWN_TYPE", UNKNOWN_TYPE},
         {"EMPTY_TYPE", EMPTY_TYPE},
         {"NAME_FIRST", NAME_FIRST},
         {"NAME_MIDDLE", NAME_MIDDLE},
         {"NAME_LAST", NAME_LAST},
         {"NAME_MIDDLE_INITIAL", NAME_MIDDLE_INITIAL},
         {"NAME_FULL", NAME_FULL},
         {"NAME_SUFFIX", NAME_SUFFIX},
         {"ALTERNATIVE_FULL_NAME", ALTERNATIVE_FULL_NAME},
         {"ALTERNATIVE_GIVEN_NAME", ALTERNATIVE_GIVEN_NAME},
         {"ALTERNATIVE_FAMILY_NAME", ALTERNATIVE_FAMILY_NAME},
         {"EMAIL_ADDRESS", EMAIL_ADDRESS},
         {"PHONE_HOME_NUMBER", PHONE_HOME_NUMBER},
         {"PHONE_HOME_CITY_CODE", PHONE_HOME_CITY_CODE},
         {"PHONE_HOME_COUNTRY_CODE", PHONE_HOME_COUNTRY_CODE},
         {"PHONE_HOME_CITY_AND_NUMBER", PHONE_HOME_CITY_AND_NUMBER},
         {"PHONE_HOME_WHOLE_NUMBER", PHONE_HOME_WHOLE_NUMBER},
         {"ADDRESS_HOME_LINE1", ADDRESS_HOME_LINE1},
         {"ADDRESS_HOME_LINE2", ADDRESS_HOME_LINE2},
         {"ADDRESS_HOME_APT", ADDRESS_HOME_APT},
         {"ADDRESS_HOME_APT_NUM", ADDRESS_HOME_APT_NUM},
         {"ADDRESS_HOME_APT_TYPE", ADDRESS_HOME_APT_TYPE},
         {"ADDRESS_HOME_HOUSE_NUMBER_AND_APT",
          ADDRESS_HOME_HOUSE_NUMBER_AND_APT},
         {"ADDRESS_HOME_CITY", ADDRESS_HOME_CITY},
         {"ADDRESS_HOME_STATE", ADDRESS_HOME_STATE},
         {"ADDRESS_HOME_ZIP", ADDRESS_HOME_ZIP},
         {"ADDRESS_HOME_COUNTRY", ADDRESS_HOME_COUNTRY},
         {"CREDIT_CARD_NAME_FULL", CREDIT_CARD_NAME_FULL},
         {"CREDIT_CARD_NUMBER", CREDIT_CARD_NUMBER},
         {"CREDIT_CARD_EXP_MONTH", CREDIT_CARD_EXP_MONTH},
         {"CREDIT_CARD_EXP_2_DIGIT_YEAR", CREDIT_CARD_EXP_2_DIGIT_YEAR},
         {"CREDIT_CARD_EXP_4_DIGIT_YEAR", CREDIT_CARD_EXP_4_DIGIT_YEAR},
         {"CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR",
          CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR},
         {"CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR",
          CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR},
         {"CREDIT_CARD_TYPE", CREDIT_CARD_TYPE},
         {"CREDIT_CARD_VERIFICATION_CODE", CREDIT_CARD_VERIFICATION_CODE},
         {"COMPANY_NAME", COMPANY_NAME},
         {"FIELD_WITH_DEFAULT_VALUE", FIELD_WITH_DEFAULT_VALUE},
         {"MERCHANT_EMAIL_SIGNUP", MERCHANT_EMAIL_SIGNUP},
         {"MERCHANT_PROMO_CODE", MERCHANT_PROMO_CODE},
         {"PASSWORD", PASSWORD},
         {"ACCOUNT_CREATION_PASSWORD", ACCOUNT_CREATION_PASSWORD},
         {"ADDRESS_HOME_STREET_ADDRESS", ADDRESS_HOME_STREET_ADDRESS},
         {"ADDRESS_HOME_SORTING_CODE", ADDRESS_HOME_SORTING_CODE},
         {"ADDRESS_HOME_DEPENDENT_LOCALITY", ADDRESS_HOME_DEPENDENT_LOCALITY},
         {"ADDRESS_HOME_LINE3", ADDRESS_HOME_LINE3},
         {"NOT_ACCOUNT_CREATION_PASSWORD", NOT_ACCOUNT_CREATION_PASSWORD},
         {"USERNAME", USERNAME},
         {"USERNAME_AND_EMAIL_ADDRESS", USERNAME_AND_EMAIL_ADDRESS},
         {"NEW_PASSWORD", NEW_PASSWORD},
         {"PROBABLY_NEW_PASSWORD", PROBABLY_NEW_PASSWORD},
         {"NOT_NEW_PASSWORD", NOT_NEW_PASSWORD},
         {"CREDIT_CARD_NAME_FIRST", CREDIT_CARD_NAME_FIRST},
         {"CREDIT_CARD_NAME_LAST", CREDIT_CARD_NAME_LAST},
         {"PHONE_HOME_EXTENSION", PHONE_HOME_EXTENSION},
         {"CONFIRMATION_PASSWORD", CONFIRMATION_PASSWORD},
         {"AMBIGUOUS_TYPE", AMBIGUOUS_TYPE},
         {"SEARCH_TERM", SEARCH_TERM},
         {"PRICE", PRICE},
         {"NOT_PASSWORD", NOT_PASSWORD},
         {"SINGLE_USERNAME", SINGLE_USERNAME},
         {"NOT_USERNAME", NOT_USERNAME},
         {"ADDRESS_HOME_STREET_NAME", ADDRESS_HOME_STREET_NAME},
         {"ADDRESS_HOME_HOUSE_NUMBER", ADDRESS_HOME_HOUSE_NUMBER},
         {"ADDRESS_HOME_SUBPREMISE", ADDRESS_HOME_SUBPREMISE},
         {"ADDRESS_HOME_OTHER_SUBUNIT", ADDRESS_HOME_OTHER_SUBUNIT},
         {"NAME_LAST_PREFIX", NAME_LAST_PREFIX},
         {"NAME_LAST_CORE", NAME_LAST_CORE},
         {"NAME_LAST_FIRST", NAME_LAST_FIRST},
         {"NAME_LAST_CONJUNCTION", NAME_LAST_CONJUNCTION},
         {"NAME_LAST_SECOND", NAME_LAST_SECOND},
         {"NAME_HONORIFIC_PREFIX", NAME_HONORIFIC_PREFIX},
         {"ADDRESS_HOME_ADDRESS", ADDRESS_HOME_ADDRESS},
         {"ADDRESS_HOME_ADDRESS_WITH_NAME", ADDRESS_HOME_ADDRESS_WITH_NAME},
         {"ADDRESS_HOME_FLOOR", ADDRESS_HOME_FLOOR},
         {"PHONE_HOME_CITY_CODE_WITH_TRUNK_PREFIX",
          PHONE_HOME_CITY_CODE_WITH_TRUNK_PREFIX},
         {"PHONE_HOME_CITY_AND_NUMBER_WITHOUT_TRUNK_PREFIX",
          PHONE_HOME_CITY_AND_NUMBER_WITHOUT_TRUNK_PREFIX},
         {"PHONE_HOME_NUMBER_PREFIX", PHONE_HOME_NUMBER_PREFIX},
         {"PHONE_HOME_NUMBER_SUFFIX", PHONE_HOME_NUMBER_SUFFIX},
         {"IBAN_VALUE", IBAN_VALUE},
         {"CREDIT_CARD_STANDALONE_VERIFICATION_CODE",
          CREDIT_CARD_STANDALONE_VERIFICATION_CODE},
         {"NUMERIC_QUANTITY", NUMERIC_QUANTITY},
         {"ONE_TIME_CODE", ONE_TIME_CODE},
         {"ADDRESS_HOME_LANDMARK", ADDRESS_HOME_LANDMARK},
         {"ADDRESS_HOME_BETWEEN_STREETS", ADDRESS_HOME_BETWEEN_STREETS},
         {"ADDRESS_HOME_ADMIN_LEVEL2", ADDRESS_HOME_ADMIN_LEVEL2},
         {"DELIVERY_INSTRUCTIONS", DELIVERY_INSTRUCTIONS},
         {"ADDRESS_HOME_OVERFLOW", ADDRESS_HOME_OVERFLOW},
         {"ADDRESS_HOME_STREET_LOCATION", ADDRESS_HOME_STREET_LOCATION},
         {"ADDRESS_HOME_BETWEEN_STREETS_1", ADDRESS_HOME_BETWEEN_STREETS_1},
         {"ADDRESS_HOME_BETWEEN_STREETS_2", ADDRESS_HOME_BETWEEN_STREETS_2},
         {"ADDRESS_HOME_OVERFLOW_AND_LANDMARK",
          ADDRESS_HOME_OVERFLOW_AND_LANDMARK},
         {"ADDRESS_HOME_BETWEEN_STREETS_OR_LANDMARK",
          ADDRESS_HOME_BETWEEN_STREETS_OR_LANDMARK},
         {"SINGLE_USERNAME_FORGOT_PASSWORD", SINGLE_USERNAME_FORGOT_PASSWORD},
         {"SINGLE_USERNAME_WITH_INTERMEDIATE_VALUES",
          SINGLE_USERNAME_WITH_INTERMEDIATE_VALUES},
         {"ADDRESS_HOME_STREET_LOCATION_AND_LOCALITY",
          ADDRESS_HOME_STREET_LOCATION_AND_LOCALITY},
         {"ADDRESS_HOME_STREET_LOCATION_AND_LANDMARK",
          ADDRESS_HOME_STREET_LOCATION_AND_LANDMARK},
         {"ADDRESS_HOME_DEPENDENT_LOCALITY_AND_LANDMARK",
          ADDRESS_HOME_DEPENDENT_LOCALITY_AND_LANDMARK},
         {"PASSPORT_NAME_TAG", PASSPORT_NAME_TAG},
         {"PASSPORT_NUMBER", PASSPORT_NUMBER},
         {"PASSPORT_ISSUING_COUNTRY", PASSPORT_ISSUING_COUNTRY},
         {"PASSPORT_EXPIRATION_DATE", PASSPORT_EXPIRATION_DATE},
         {"PASSPORT_ISSUE_DATE", PASSPORT_ISSUE_DATE},
         {"LOYALTY_MEMBERSHIP_PROGRAM", LOYALTY_MEMBERSHIP_PROGRAM},
         {"LOYALTY_MEMBERSHIP_PROVIDER", LOYALTY_MEMBERSHIP_PROVIDER},
         {"LOYALTY_MEMBERSHIP_ID", LOYALTY_MEMBERSHIP_ID},
         {"VEHICLE_OWNER_TAG", VEHICLE_OWNER_TAG},
         {"VEHICLE_LICENSE_PLATE", VEHICLE_LICENSE_PLATE},
         {"VEHICLE_VIN", VEHICLE_VIN},
         {"VEHICLE_MAKE", VEHICLE_MAKE},
         {"VEHICLE_MODEL", VEHICLE_MODEL},
         {"VEHICLE_YEAR", VEHICLE_YEAR},
         {"VEHICLE_PLATE_STATE", VEHICLE_PLATE_STATE},
         {"DRIVERS_LICENSE_NAME_TAG", DRIVERS_LICENSE_NAME_TAG},
         {"DRIVERS_LICENSE_REGION", DRIVERS_LICENSE_REGION},
         {"DRIVERS_LICENSE_NUMBER", DRIVERS_LICENSE_NUMBER},
         {"DRIVERS_LICENSE_EXPIRATION_DATE", DRIVERS_LICENSE_EXPIRATION_DATE},
         {"DRIVERS_LICENSE_ISSUE_DATE", DRIVERS_LICENSE_ISSUE_DATE},
         {"EMAIL_OR_LOYALTY_MEMBERSHIP_ID", EMAIL_OR_LOYALTY_MEMBERSHIP_ID}});

bool IsFillableFieldType(FieldType field_type) {
  switch (field_type) {
    case NAME_HONORIFIC_PREFIX:
    case NAME_FIRST:
    case NAME_MIDDLE:
    case NAME_LAST:
    case NAME_LAST_CORE:
    case NAME_LAST_PREFIX:
    case NAME_LAST_FIRST:
    case NAME_LAST_CONJUNCTION:
    case NAME_LAST_SECOND:
    case NAME_MIDDLE_INITIAL:
    case NAME_FULL:
    case NAME_SUFFIX:
    case ALTERNATIVE_FULL_NAME:
    case ALTERNATIVE_FAMILY_NAME:
    case ALTERNATIVE_GIVEN_NAME:
    case EMAIL_ADDRESS:
    case USERNAME_AND_EMAIL_ADDRESS:
    case PHONE_HOME_NUMBER:
    case PHONE_HOME_NUMBER_PREFIX:
    case PHONE_HOME_NUMBER_SUFFIX:
    case PHONE_HOME_CITY_CODE:
    case PHONE_HOME_CITY_CODE_WITH_TRUNK_PREFIX:
    case PHONE_HOME_COUNTRY_CODE:
    case PHONE_HOME_CITY_AND_NUMBER:
    case PHONE_HOME_CITY_AND_NUMBER_WITHOUT_TRUNK_PREFIX:
    case PHONE_HOME_WHOLE_NUMBER:
    case PHONE_HOME_EXTENSION:
    case ADDRESS_HOME_LINE1:
    case ADDRESS_HOME_LINE2:
    case ADDRESS_HOME_LINE3:
    case ADDRESS_HOME_APT:
    case ADDRESS_HOME_APT_NUM:
    case ADDRESS_HOME_APT_TYPE:
    case ADDRESS_HOME_HOUSE_NUMBER_AND_APT:
    case ADDRESS_HOME_CITY:
    case ADDRESS_HOME_STATE:
    case ADDRESS_HOME_ZIP:
    case ADDRESS_HOME_COUNTRY:
    case ADDRESS_HOME_STREET_ADDRESS:
    case ADDRESS_HOME_SORTING_CODE:
    case ADDRESS_HOME_DEPENDENT_LOCALITY:
    case ADDRESS_HOME_STREET_NAME:
    case ADDRESS_HOME_HOUSE_NUMBER:
    case ADDRESS_HOME_STREET_LOCATION:
    case ADDRESS_HOME_SUBPREMISE:
    case ADDRESS_HOME_OTHER_SUBUNIT:
    case ADDRESS_HOME_ADDRESS:
    case ADDRESS_HOME_ADDRESS_WITH_NAME:
    case ADDRESS_HOME_FLOOR:
    case ADDRESS_HOME_LANDMARK:
    case ADDRESS_HOME_BETWEEN_STREETS:
    case ADDRESS_HOME_BETWEEN_STREETS_1:
    case ADDRESS_HOME_BETWEEN_STREETS_2:
    case ADDRESS_HOME_ADMIN_LEVEL2:
    case ADDRESS_HOME_OVERFLOW:
    case ADDRESS_HOME_BETWEEN_STREETS_OR_LANDMARK:
    case ADDRESS_HOME_OVERFLOW_AND_LANDMARK:
    case ADDRESS_HOME_STREET_LOCATION_AND_LOCALITY:
    case ADDRESS_HOME_STREET_LOCATION_AND_LANDMARK:
    case ADDRESS_HOME_DEPENDENT_LOCALITY_AND_LANDMARK:
    case DELIVERY_INSTRUCTIONS:
    case LOYALTY_MEMBERSHIP_PROGRAM:
    case LOYALTY_MEMBERSHIP_PROVIDER:
    case LOYALTY_MEMBERSHIP_ID:
    case EMAIL_OR_LOYALTY_MEMBERSHIP_ID:
      return true;

    case CREDIT_CARD_NAME_FULL:
    case CREDIT_CARD_NAME_FIRST:
    case CREDIT_CARD_NAME_LAST:
    case CREDIT_CARD_NUMBER:
    case CREDIT_CARD_EXP_MONTH:
    case CREDIT_CARD_EXP_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_4_DIGIT_YEAR:
    case CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR:
    case CREDIT_CARD_TYPE:
    case CREDIT_CARD_VERIFICATION_CODE:
    case CREDIT_CARD_STANDALONE_VERIFICATION_CODE:
      return true;

    case IBAN_VALUE:
      return true;

    case COMPANY_NAME:
      return true;

    case MERCHANT_PROMO_CODE:
      return true;

    // Fillable credential fields.
    case USERNAME:
    case PASSWORD:
    case ACCOUNT_CREATION_PASSWORD:
    case CONFIRMATION_PASSWORD:
    case SINGLE_USERNAME:
    case SINGLE_USERNAME_FORGOT_PASSWORD:
    case SINGLE_USERNAME_WITH_INTERMEDIATE_VALUES:
      return true;

    // Autofill AI types.
    case DRIVERS_LICENSE_EXPIRATION_DATE:
    case DRIVERS_LICENSE_ISSUE_DATE:
    case DRIVERS_LICENSE_NAME_TAG:
    case DRIVERS_LICENSE_NUMBER:
    case DRIVERS_LICENSE_REGION:
    case PASSPORT_EXPIRATION_DATE:
    case PASSPORT_ISSUE_DATE:
    case PASSPORT_ISSUING_COUNTRY:
    case PASSPORT_NAME_TAG:
    case PASSPORT_NUMBER:
    case VEHICLE_LICENSE_PLATE:
    case VEHICLE_MAKE:
    case VEHICLE_MODEL:
    case VEHICLE_OWNER_TAG:
    case VEHICLE_PLATE_STATE:
    case VEHICLE_VIN:
    case VEHICLE_YEAR:
      return true;

    // Not fillable credential fields.
    case NOT_PASSWORD:
    case NOT_USERNAME:
      return false;

    // Credential field types that the server should never return as
    // classifications.
    case NOT_ACCOUNT_CREATION_PASSWORD:
    case NEW_PASSWORD:
    case PROBABLY_NEW_PASSWORD:
    case NOT_NEW_PASSWORD:
    case ONE_TIME_CODE:
      return false;

    case NO_SERVER_DATA:
    case EMPTY_TYPE:
    case AMBIGUOUS_TYPE:
    case FIELD_WITH_DEFAULT_VALUE:
    case MERCHANT_EMAIL_SIGNUP:
    case PRICE:
    case NUMERIC_QUANTITY:
    case SEARCH_TERM:
    case UNKNOWN_TYPE:
    case MAX_VALID_FIELD_TYPE:
      return false;
  }
  NOTREACHED();
}

std::string_view FieldTypeToStringView(FieldType type) {
  static const base::NoDestructor<base::flat_map<FieldType, std::string_view>>
      kFieldTypeToTypeName(base::MakeFlatMap<FieldType, std::string_view>(
          kTypeNameToFieldType, {}, [](const auto& item) {
            return std::make_pair(item.second, item.first);
          }));

  auto it = kFieldTypeToTypeName->find(type);
  if (it != kFieldTypeToTypeName->end()) {
    return it->second;
  }
  NOTREACHED();
}

std::string FieldTypeToString(FieldType type) {
  return std::string(FieldTypeToStringView(type));
}

FieldType TypeNameToFieldType(std::string_view type_name) {
  auto it = kTypeNameToFieldType.find(type_name);
  return it != kTypeNameToFieldType.end() ? it->second : UNKNOWN_TYPE;
}

std::string_view FieldTypeToDeveloperRepresentationString(FieldType type) {
  switch (type) {
    case NO_SERVER_DATA:
    case UNKNOWN_TYPE:
    case FIELD_WITH_DEFAULT_VALUE:
    case EMPTY_TYPE:
    case NOT_ACCOUNT_CREATION_PASSWORD:
    case NOT_NEW_PASSWORD:
    case NOT_PASSWORD:
    case NOT_USERNAME:
    case AMBIGUOUS_TYPE:
    case NAME_SUFFIX:
    case ADDRESS_HOME_ADDRESS:
    case ADDRESS_HOME_ADDRESS_WITH_NAME:
    case PASSPORT_NAME_TAG:
    case PASSPORT_NUMBER:
    case PASSPORT_ISSUING_COUNTRY:
    case PASSPORT_EXPIRATION_DATE:
    case PASSPORT_ISSUE_DATE:
    case LOYALTY_MEMBERSHIP_PROGRAM:
    case LOYALTY_MEMBERSHIP_PROVIDER:
    case LOYALTY_MEMBERSHIP_ID:
    case EMAIL_OR_LOYALTY_MEMBERSHIP_ID:
    case VEHICLE_OWNER_TAG:
    case VEHICLE_LICENSE_PLATE:
    case VEHICLE_VIN:
    case VEHICLE_MAKE:
    case VEHICLE_MODEL:
    case VEHICLE_YEAR:
    case VEHICLE_PLATE_STATE:
    case DRIVERS_LICENSE_NAME_TAG:
    case DRIVERS_LICENSE_REGION:
    case DRIVERS_LICENSE_NUMBER:
    case DRIVERS_LICENSE_EXPIRATION_DATE:
    case DRIVERS_LICENSE_ISSUE_DATE:
      return "";
    case NUMERIC_QUANTITY:
      return "Numeric quantity";
    case MERCHANT_EMAIL_SIGNUP:
      return "Merchant email signup";
    case MERCHANT_PROMO_CODE:
      return "Merchant promo code";
    case PASSWORD:
      return "Password";
    case ACCOUNT_CREATION_PASSWORD:
      return "Account creation password";
    case USERNAME:
    case SINGLE_USERNAME:
    case SINGLE_USERNAME_FORGOT_PASSWORD:
    case SINGLE_USERNAME_WITH_INTERMEDIATE_VALUES:
      return "Username";
    case USERNAME_AND_EMAIL_ADDRESS:
      return "Username and email";
    case PROBABLY_NEW_PASSWORD:
    case NEW_PASSWORD:
      return "New password";
    case CONFIRMATION_PASSWORD:
      return "Confirmation password";
    case SEARCH_TERM:
      return "Search term";
    case PRICE:
      return "Price";
    case NAME_HONORIFIC_PREFIX:
      return "Honorific prefix";
    case NAME_FIRST:
      return "First name";
    case NAME_MIDDLE:
      return "Middle name";
    case NAME_LAST:
      return "Last name";
    case NAME_LAST_PREFIX:
      return "Last name prefix";
    case NAME_LAST_CORE:
      return "Last name core";
    case NAME_LAST_FIRST:
      return "First last name";
    case NAME_LAST_CONJUNCTION:
      return "Last name conjunction";
    case NAME_LAST_SECOND:
      return "Second last name";
    case NAME_MIDDLE_INITIAL:
      return "Middle name initial";
    case NAME_FULL:
      return "Full name";
    case ALTERNATIVE_FULL_NAME:
      return "Alternative full name";
    case ALTERNATIVE_FAMILY_NAME:
      return "Alternative family name";
    case ALTERNATIVE_GIVEN_NAME:
      return "Alternative given name";
    case EMAIL_ADDRESS:
      return "Email address";
    case PHONE_HOME_NUMBER:
    case PHONE_HOME_WHOLE_NUMBER:
    case PHONE_HOME_CITY_AND_NUMBER:
    case PHONE_HOME_CITY_AND_NUMBER_WITHOUT_TRUNK_PREFIX:
      return "Phone number";
    case PHONE_HOME_NUMBER_PREFIX:
      return "Phone number prefix";
    case PHONE_HOME_NUMBER_SUFFIX:
      return "Phone number suffix";
    case PHONE_HOME_CITY_CODE:
    case PHONE_HOME_CITY_CODE_WITH_TRUNK_PREFIX:
      return "Phone number city code";
    case PHONE_HOME_COUNTRY_CODE:
      return "Phone number country code";
    case PHONE_HOME_EXTENSION:
      return "Phone number extension";
    case ADDRESS_HOME_FLOOR:
      return "Floor";
    case ADDRESS_HOME_LANDMARK:
      return "Landmark";
    case ADDRESS_HOME_STREET_NAME:
      return "Street name";
    case ADDRESS_HOME_HOUSE_NUMBER:
      return "House number";
    case ADDRESS_HOME_BETWEEN_STREETS:
      return "Address between-streets";
    case ADDRESS_HOME_BETWEEN_STREETS_1:
      return "Address between-streets 1";
    case ADDRESS_HOME_BETWEEN_STREETS_2:
      return "Address between-streets 2";
    case ADDRESS_HOME_LINE1:
      return "Address line 1";
    case ADDRESS_HOME_LINE2:
      return "Address line 2";
    case ADDRESS_HOME_LINE3:
      return "Address line 3";
    case ADDRESS_HOME_SUBPREMISE:
      return "Address subpremise";
    case ADDRESS_HOME_OTHER_SUBUNIT:
      return "Address subunit";
    case ADDRESS_HOME_ADMIN_LEVEL2:
      return "Administrative area level 2";
    case ADDRESS_HOME_STREET_LOCATION:
      return "Street location";
    case ADDRESS_HOME_STREET_ADDRESS:
      return "Street address";
    case ADDRESS_HOME_SORTING_CODE:
      return "Sorting code";
    case ADDRESS_HOME_DEPENDENT_LOCALITY:
      return "Dependent locality";
    case ADDRESS_HOME_APT:
      return "Apt";
    case ADDRESS_HOME_APT_NUM:
      return "Apt num";
    case ADDRESS_HOME_APT_TYPE:
      return "Apt type";
    case ADDRESS_HOME_CITY:
      return "City";
    case ADDRESS_HOME_STATE:
      return "State";
    case ADDRESS_HOME_ZIP:
      return "ZIP code";
    case ADDRESS_HOME_COUNTRY:
      return "Country";
    case ADDRESS_HOME_OVERFLOW:
      return "Address overflow";
    case ADDRESS_HOME_OVERFLOW_AND_LANDMARK:
      return "Address overflow and landmark";
    case ADDRESS_HOME_BETWEEN_STREETS_OR_LANDMARK:
      return "Address between-streets and landmark";
    case ADDRESS_HOME_STREET_LOCATION_AND_LOCALITY:
      return "Address street location and locality";
    case ADDRESS_HOME_STREET_LOCATION_AND_LANDMARK:
      return "Address street location and landmark";
    case ADDRESS_HOME_DEPENDENT_LOCALITY_AND_LANDMARK:
      return "Address locality and landmark";
    case ADDRESS_HOME_HOUSE_NUMBER_AND_APT:
      return "House number and apartment number";
    case DELIVERY_INSTRUCTIONS:
      return "Delivery instructions";
    case CREDIT_CARD_NAME_FULL:
      return "Credit card full name";
    case CREDIT_CARD_NAME_FIRST:
      return "Credit card first name";
    case CREDIT_CARD_NAME_LAST:
      return "Credit card last name";
    case CREDIT_CARD_NUMBER:
      return "Credit card number";
    case CREDIT_CARD_EXP_MONTH:
      return "Credit card exp month";
    case CREDIT_CARD_EXP_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_4_DIGIT_YEAR:
    case CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR:
      return "Credit card exp year";
    case CREDIT_CARD_TYPE:
      return "Credit card type";
    case CREDIT_CARD_VERIFICATION_CODE:
      return "Credit card verification code";
    case COMPANY_NAME:
      return "Company name";
    case IBAN_VALUE:
      return "IBAN";
    case CREDIT_CARD_STANDALONE_VERIFICATION_CODE:
    case ONE_TIME_CODE:
      return "One time code";
    case MAX_VALID_FIELD_TYPE:
      return "";
  }
  NOTREACHED();
}

FieldTypeGroup GroupTypeOfHtmlFieldType(HtmlFieldType field_type) {
  switch (field_type) {
    case HtmlFieldType::kName:
    case HtmlFieldType::kHonorificPrefix:
    case HtmlFieldType::kGivenName:
    case HtmlFieldType::kAdditionalName:
    case HtmlFieldType::kAdditionalNameInitial:
    case HtmlFieldType::kFamilyName:
      return FieldTypeGroup::kName;

    case HtmlFieldType::kOrganization:
      return FieldTypeGroup::kCompany;

    case HtmlFieldType::kStreetAddress:
    case HtmlFieldType::kAddressLine1:
    case HtmlFieldType::kAddressLine2:
    case HtmlFieldType::kAddressLine3:
    case HtmlFieldType::kAddressLevel1:
    case HtmlFieldType::kAddressLevel2:
    case HtmlFieldType::kAddressLevel3:
    case HtmlFieldType::kCountryCode:
    case HtmlFieldType::kCountryName:
    case HtmlFieldType::kPostalCode:
      return FieldTypeGroup::kAddress;

    case HtmlFieldType::kCreditCardNameFull:
    case HtmlFieldType::kCreditCardNameFirst:
    case HtmlFieldType::kCreditCardNameLast:
    case HtmlFieldType::kCreditCardNumber:
    case HtmlFieldType::kCreditCardExp:
    case HtmlFieldType::kCreditCardExpDate2DigitYear:
    case HtmlFieldType::kCreditCardExpDate4DigitYear:
    case HtmlFieldType::kCreditCardExpMonth:
    case HtmlFieldType::kCreditCardExpYear:
    case HtmlFieldType::kCreditCardExp2DigitYear:
    case HtmlFieldType::kCreditCardExp4DigitYear:
    case HtmlFieldType::kCreditCardVerificationCode:
    case HtmlFieldType::kCreditCardType:
      return FieldTypeGroup::kCreditCard;

    case HtmlFieldType::kTransactionAmount:
    case HtmlFieldType::kTransactionCurrency:
      return FieldTypeGroup::kTransaction;

    case HtmlFieldType::kTel:
    case HtmlFieldType::kTelCountryCode:
    case HtmlFieldType::kTelNational:
    case HtmlFieldType::kTelAreaCode:
    case HtmlFieldType::kTelLocal:
    case HtmlFieldType::kTelLocalPrefix:
    case HtmlFieldType::kTelLocalSuffix:
    case HtmlFieldType::kTelExtension:
      return FieldTypeGroup::kPhone;

    case HtmlFieldType::kEmail:
      return FieldTypeGroup::kEmail;

    case HtmlFieldType::kBirthdateDay:
    case HtmlFieldType::kBirthdateMonth:
    case HtmlFieldType::kBirthdateYear:
      return FieldTypeGroup::kNoGroup;

    case HtmlFieldType::kOneTimeCode:
      return FieldTypeGroup::kNoGroup;

    case HtmlFieldType::kMerchantPromoCode:
      return FieldTypeGroup::kNoGroup;

    case HtmlFieldType::kIban:
      return FieldTypeGroup::kIban;

    case HtmlFieldType::kUnspecified:
    case HtmlFieldType::kUnrecognized:
      return FieldTypeGroup::kNoGroup;
  }
  NOTREACHED();
}

FieldType HtmlFieldTypeToBestCorrespondingFieldType(HtmlFieldType field_type) {
  switch (field_type) {
    case HtmlFieldType::kUnspecified:
      return UNKNOWN_TYPE;

    case HtmlFieldType::kName:
      return NAME_FULL;

    case HtmlFieldType::kHonorificPrefix:
      return NAME_HONORIFIC_PREFIX;

    case HtmlFieldType::kGivenName:
      return NAME_FIRST;

    case HtmlFieldType::kAdditionalName:
      return NAME_MIDDLE;

    case HtmlFieldType::kFamilyName:
      return NAME_LAST;

    case HtmlFieldType::kOrganization:
      return COMPANY_NAME;

    case HtmlFieldType::kStreetAddress:
      return ADDRESS_HOME_STREET_ADDRESS;

    case HtmlFieldType::kAddressLine1:
      return ADDRESS_HOME_LINE1;

    case HtmlFieldType::kAddressLine2:
      return ADDRESS_HOME_LINE2;

    case HtmlFieldType::kAddressLine3:
      return ADDRESS_HOME_LINE3;

    case HtmlFieldType::kAddressLevel1:
      return ADDRESS_HOME_STATE;

    case HtmlFieldType::kAddressLevel2:
      return ADDRESS_HOME_CITY;

    case HtmlFieldType::kAddressLevel3:
      return ADDRESS_HOME_DEPENDENT_LOCALITY;

    case HtmlFieldType::kCountryCode:
    case HtmlFieldType::kCountryName:
      return ADDRESS_HOME_COUNTRY;

    case HtmlFieldType::kPostalCode:
      return ADDRESS_HOME_ZIP;

    case HtmlFieldType::kCreditCardNameFull:
      return CREDIT_CARD_NAME_FULL;

    case HtmlFieldType::kCreditCardNameFirst:
      return CREDIT_CARD_NAME_FIRST;

    case HtmlFieldType::kCreditCardNameLast:
      return CREDIT_CARD_NAME_LAST;

    case HtmlFieldType::kCreditCardNumber:
      return CREDIT_CARD_NUMBER;

    case HtmlFieldType::kCreditCardExp:
      return CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR;

    case HtmlFieldType::kCreditCardExpMonth:
      return CREDIT_CARD_EXP_MONTH;

    case HtmlFieldType::kCreditCardExpYear:
      return CREDIT_CARD_EXP_4_DIGIT_YEAR;

    case HtmlFieldType::kCreditCardVerificationCode:
      return CREDIT_CARD_VERIFICATION_CODE;

    case HtmlFieldType::kCreditCardType:
      return CREDIT_CARD_TYPE;

    case HtmlFieldType::kTel:
      return PHONE_HOME_WHOLE_NUMBER;

    case HtmlFieldType::kTelCountryCode:
      return PHONE_HOME_COUNTRY_CODE;

    case HtmlFieldType::kTelNational:
      return PHONE_HOME_CITY_AND_NUMBER;

    case HtmlFieldType::kTelAreaCode:
      return PHONE_HOME_CITY_CODE;

    case HtmlFieldType::kTelLocal:
      return PHONE_HOME_NUMBER;

    case HtmlFieldType::kTelLocalPrefix:
      return PHONE_HOME_NUMBER_PREFIX;

    case HtmlFieldType::kTelLocalSuffix:
      return PHONE_HOME_NUMBER_SUFFIX;

    case HtmlFieldType::kTelExtension:
      return PHONE_HOME_EXTENSION;

    case HtmlFieldType::kEmail:
      return EMAIL_ADDRESS;

    case HtmlFieldType::kAdditionalNameInitial:
      return NAME_MIDDLE_INITIAL;

    case HtmlFieldType::kCreditCardExpDate2DigitYear:
      return CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR;

    case HtmlFieldType::kCreditCardExpDate4DigitYear:
      return CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR;

    case HtmlFieldType::kCreditCardExp2DigitYear:
      return CREDIT_CARD_EXP_2_DIGIT_YEAR;

    case HtmlFieldType::kCreditCardExp4DigitYear:
      return CREDIT_CARD_EXP_4_DIGIT_YEAR;

    case HtmlFieldType::kOneTimeCode:
      return ONE_TIME_CODE;

    case HtmlFieldType::kIban:
      return IBAN_VALUE;

    // These types aren't stored; they're transient.
    case HtmlFieldType::kBirthdateDay:
    case HtmlFieldType::kBirthdateMonth:
    case HtmlFieldType::kBirthdateYear:
    case HtmlFieldType::kTransactionAmount:
    case HtmlFieldType::kTransactionCurrency:
    case HtmlFieldType::kMerchantPromoCode:
      return UNKNOWN_TYPE;

    case HtmlFieldType::kUnrecognized:
      return UNKNOWN_TYPE;
  }
  NOTREACHED();
}

bool IsDateFieldType(FieldType field_type) {
  switch (field_type) {
    case NO_SERVER_DATA:
    case UNKNOWN_TYPE:
    case EMPTY_TYPE:
    case NAME_FIRST:
    case NAME_MIDDLE:
    case NAME_LAST:
    case NAME_MIDDLE_INITIAL:
    case NAME_FULL:
    case NAME_SUFFIX:
    case EMAIL_ADDRESS:
    case PHONE_HOME_NUMBER:
    case PHONE_HOME_CITY_CODE:
    case PHONE_HOME_COUNTRY_CODE:
    case PHONE_HOME_CITY_AND_NUMBER:
    case PHONE_HOME_WHOLE_NUMBER:
    case ADDRESS_HOME_LINE1:
    case ADDRESS_HOME_LINE2:
    case ADDRESS_HOME_APT_NUM:
    case ADDRESS_HOME_CITY:
    case ADDRESS_HOME_STATE:
    case ADDRESS_HOME_ZIP:
    case ADDRESS_HOME_COUNTRY:
    case CREDIT_CARD_NAME_FULL:
    case CREDIT_CARD_NUMBER:
    case CREDIT_CARD_EXP_MONTH:
    case CREDIT_CARD_EXP_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_4_DIGIT_YEAR:
    case CREDIT_CARD_TYPE:
    case CREDIT_CARD_VERIFICATION_CODE:
    case COMPANY_NAME:
    case FIELD_WITH_DEFAULT_VALUE:
    case MERCHANT_EMAIL_SIGNUP:
    case MERCHANT_PROMO_CODE:
    case PASSWORD:
    case ACCOUNT_CREATION_PASSWORD:
    case ADDRESS_HOME_STREET_ADDRESS:
    case ADDRESS_HOME_SORTING_CODE:
    case ADDRESS_HOME_DEPENDENT_LOCALITY:
    case ADDRESS_HOME_LINE3:
    case NOT_ACCOUNT_CREATION_PASSWORD:
    case USERNAME:
    case USERNAME_AND_EMAIL_ADDRESS:
    case NEW_PASSWORD:
    case PROBABLY_NEW_PASSWORD:
    case NOT_NEW_PASSWORD:
    case CREDIT_CARD_NAME_FIRST:
    case CREDIT_CARD_NAME_LAST:
    case PHONE_HOME_EXTENSION:
    case CONFIRMATION_PASSWORD:
    case AMBIGUOUS_TYPE:
    case SEARCH_TERM:
    case PRICE:
    case NOT_PASSWORD:
    case SINGLE_USERNAME:
    case NOT_USERNAME:
    case ADDRESS_HOME_STREET_NAME:
    case ADDRESS_HOME_HOUSE_NUMBER:
    case ADDRESS_HOME_SUBPREMISE:
    case ADDRESS_HOME_OTHER_SUBUNIT:
    case NAME_LAST_FIRST:
    case NAME_LAST_CONJUNCTION:
    case NAME_LAST_SECOND:
    case NAME_HONORIFIC_PREFIX:
    case ADDRESS_HOME_ADDRESS:
    case ADDRESS_HOME_ADDRESS_WITH_NAME:
    case ADDRESS_HOME_FLOOR:
    case PHONE_HOME_CITY_CODE_WITH_TRUNK_PREFIX:
    case PHONE_HOME_CITY_AND_NUMBER_WITHOUT_TRUNK_PREFIX:
    case PHONE_HOME_NUMBER_PREFIX:
    case PHONE_HOME_NUMBER_SUFFIX:
    case IBAN_VALUE:
    case CREDIT_CARD_STANDALONE_VERIFICATION_CODE:
    case NUMERIC_QUANTITY:
    case ONE_TIME_CODE:
    case DELIVERY_INSTRUCTIONS:
    case ADDRESS_HOME_OVERFLOW:
    case ADDRESS_HOME_LANDMARK:
    case ADDRESS_HOME_OVERFLOW_AND_LANDMARK:
    case ADDRESS_HOME_ADMIN_LEVEL2:
    case ADDRESS_HOME_STREET_LOCATION:
    case ADDRESS_HOME_BETWEEN_STREETS:
    case ADDRESS_HOME_BETWEEN_STREETS_OR_LANDMARK:
    case ADDRESS_HOME_STREET_LOCATION_AND_LOCALITY:
    case ADDRESS_HOME_STREET_LOCATION_AND_LANDMARK:
    case ADDRESS_HOME_DEPENDENT_LOCALITY_AND_LANDMARK:
    case ADDRESS_HOME_BETWEEN_STREETS_1:
    case ADDRESS_HOME_BETWEEN_STREETS_2:
    case ADDRESS_HOME_HOUSE_NUMBER_AND_APT:
    case SINGLE_USERNAME_FORGOT_PASSWORD:
    case ADDRESS_HOME_APT:
    case ADDRESS_HOME_APT_TYPE:
    case LOYALTY_MEMBERSHIP_ID:
    case EMAIL_OR_LOYALTY_MEMBERSHIP_ID:
    case SINGLE_USERNAME_WITH_INTERMEDIATE_VALUES:
    case ALTERNATIVE_FULL_NAME:
    case ALTERNATIVE_GIVEN_NAME:
    case ALTERNATIVE_FAMILY_NAME:
    case NAME_LAST_PREFIX:
    case NAME_LAST_CORE:
    case PASSPORT_NAME_TAG:
    case PASSPORT_NUMBER:
    case PASSPORT_ISSUING_COUNTRY:
    case LOYALTY_MEMBERSHIP_PROGRAM:
    case LOYALTY_MEMBERSHIP_PROVIDER:
    case VEHICLE_OWNER_TAG:
    case VEHICLE_LICENSE_PLATE:
    case VEHICLE_VIN:
    case VEHICLE_MAKE:
    case VEHICLE_MODEL:
    case VEHICLE_YEAR:
    case VEHICLE_PLATE_STATE:
    case DRIVERS_LICENSE_NAME_TAG:
    case DRIVERS_LICENSE_REGION:
    case DRIVERS_LICENSE_NUMBER:
    case MAX_VALID_FIELD_TYPE:
      return false;
    case CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR:
    case CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR:
    case PASSPORT_EXPIRATION_DATE:
    case PASSPORT_ISSUE_DATE:
    case DRIVERS_LICENSE_EXPIRATION_DATE:
    case DRIVERS_LICENSE_ISSUE_DATE:
      return true;
  }
  NOTREACHED();
}

}  // namespace autofill
