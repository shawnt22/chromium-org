// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/android/personal_data_manager_android.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/command_line.h"
#include "base/format_macros.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/android/resource_mapper.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/payments/payments_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_structured_address_constants.h"
#include "components/autofill/core/browser/data_model/payments/bank_account.h"
#include "components/autofill/core/browser/data_model/payments/ewallet.h"
#include "components/autofill/core/browser/data_model/payments/payment_instrument.h"
#include "components/autofill/core/browser/data_quality/autofill_data_util.h"
#include "components/autofill/core/browser/data_quality/validation.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/geo/address_i18n.h"
#include "components/autofill/core/browser/geo/autofill_country.h"
#include "components/autofill/core/browser/geo/country_names.h"
#include "components/autofill/core/browser/studies/autofill_experiments.h"
#include "components/autofill/core/browser/suggestions/payments/payments_suggestion_generator.h"
#include "components/autofill/core/browser/ui/autofill_resource_utils.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/credit_card_number_validation.h"
#include "components/autofill/core/common/dense_set.h"
#include "components/prefs/pref_service.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/android/gurl_android.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/browser/autofill/android/jni_headers/PersonalDataManager_jni.h"
#include "components/autofill/android/payments_jni_headers/BankAccount_jni.h"
#include "components/autofill/android/payments_jni_headers/Ewallet_jni.h"
#include "components/autofill/android/payments_jni_headers/PaymentInstrument_jni.h"

namespace autofill {
namespace {

using ::base::android::ConvertJavaStringToUTF16;
using ::base::android::ConvertJavaStringToUTF8;
using ::base::android::ConvertUTF16ToJavaString;
using ::base::android::ConvertUTF8ToJavaString;
using ::base::android::JavaParamRef;
using ::base::android::JavaRef;
using ::base::android::ScopedJavaGlobalRef;
using ::base::android::ScopedJavaLocalRef;
using ::base::android::ToJavaIntArray;

// Logs whether the alternative name in a new profile contains a separator.
void RecordAlternativeNameSeparatorUsage(
    const autofill::AutofillProfile& profile,
    const autofill::AutofillProfile* existing_profile) {
  const std::u16string existing_alternative_name =
      existing_profile
          ? existing_profile->GetInfo(autofill::ALTERNATIVE_FULL_NAME,
                                      g_browser_process->GetApplicationLocale())
          : std::u16string();

  const std::u16string saved_alternative_name =
      profile.GetInfo(autofill::ALTERNATIVE_FULL_NAME,
                      g_browser_process->GetApplicationLocale());

  if (!saved_alternative_name.empty() &&
      saved_alternative_name != existing_alternative_name) {
    const bool has_name_separator =
        re2::RE2::PartialMatch(base::UTF16ToUTF8(saved_alternative_name),
                               autofill::kCjkNameSeparatorsRe);
    base::UmaHistogramBoolean(
        "Autofill.Settings.EditedAlternativeNameContainsASeparator",
        has_name_separator);
  }
}

}  // namespace

PersonalDataManagerAndroid::PersonalDataManagerAndroid(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& obj,
    PersonalDataManager* personal_data_manager,
    PrefService* prefs)
    : weak_java_obj_(env, obj),
      prefs_(prefs) {
  pdm_observation_.Observe(personal_data_manager);
}

PersonalDataManagerAndroid::~PersonalDataManagerAndroid() = default;

void PersonalDataManagerAndroid::Destroy(JNIEnv* env) {
  delete this;
}

// static
ScopedJavaLocalRef<jobject>
PersonalDataManagerAndroid::CreateJavaCreditCardFromNative(
    JNIEnv* env,
    const CreditCard& card) {
  // Full server cards are only a temporary state for a credit card used when
  // re-filling a cached masked server card on a page. They are never offered as
  // suggestions, and are not expected to be used/created on the Java side.
  CHECK_NE(card.record_type(), CreditCard::RecordType::kFullServerCard);

  const data_util::PaymentRequestData& payment_request_data =
      data_util::GetPaymentRequestData(card.network());
  return Java_CreditCard_create(
      env, card.guid(), card.origin(),
      card.record_type() == CreditCard::RecordType::kLocalCard,
      card.record_type() == CreditCard::RecordType::kVirtualCard,
      card.GetRawInfo(CREDIT_CARD_NAME_FULL),
      card.GetRawInfo(CREDIT_CARD_NUMBER), card.NetworkAndLastFourDigits(),
      card.GetRawInfo(CREDIT_CARD_EXP_MONTH),
      card.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR),
      payment_request_data.basic_card_issuer_network,
      ResourceMapper::MapToJavaDrawableId(
          GetIconResourceID(card.CardIconForAutofillSuggestion())),
      card.billing_address_id(), card.server_id(), card.instrument_id(),
      card.CardNameAndLastFourDigits(), card.nickname(),
      url::GURLAndroid::FromNativeGURL(env, card.card_art_url()),
      static_cast<jint>(card.virtual_card_enrollment_state()),
      card.product_description(), card.CardNameForAutofillDisplay(),
      card.ObfuscatedNumberWithVisibleLastFourDigits(), card.cvc(),
      card.issuer_id(),
      url::GURLAndroid::FromNativeGURL(env, card.product_terms_url()));
}

// static
void PersonalDataManagerAndroid::PopulateNativeCreditCardFromJava(
    const JavaRef<jobject>& jcard,
    JNIEnv* env,
    CreditCard* card) {
  card->set_origin(Java_CreditCard_getOrigin(env, jcard));
  card->SetRawInfo(CREDIT_CARD_NAME_FULL, Java_CreditCard_getName(env, jcard));
  card->SetRawInfo(CREDIT_CARD_NUMBER, Java_CreditCard_getNumber(env, jcard));
  card->SetRawInfo(CREDIT_CARD_EXP_MONTH, Java_CreditCard_getMonth(env, jcard));
  card->SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR,
                   Java_CreditCard_getYear(env, jcard));
  card->set_billing_address_id(Java_CreditCard_getBillingAddressId(env, jcard));
  card->set_server_id(Java_CreditCard_getServerId(env, jcard));
  card->set_instrument_id(Java_CreditCard_getInstrumentId(env, jcard));
  card->SetNickname(Java_CreditCard_getNickname(env, jcard));
  ScopedJavaLocalRef<jobject> java_card_art_url =
      Java_CreditCard_getCardArtUrl(env, jcard);
  if (!java_card_art_url.is_null()) {
    card->set_card_art_url(
        url::GURLAndroid::ToNativeGURL(env, java_card_art_url));
  }
  // Only set the guid if it is an existing card (java guid not empty).
  // Otherwise, keep the generated one.
  std::string guid = Java_CreditCard_getGUID(env, jcard);
  if (!guid.empty()) {
    card->set_guid(guid);
  }
  if (Java_CreditCard_getIsLocal(env, jcard)) {
    card->set_record_type(CreditCard::RecordType::kLocalCard);
  } else {
    // Native copies of virtual credit card objects should not be created.
    DCHECK(!Java_CreditCard_getIsVirtual(env, jcard));
    card->set_record_type(CreditCard::RecordType::kMaskedServerCard);
    card->SetNetworkForMaskedCard(
        data_util::GetIssuerNetworkForBasicCardIssuerNetwork(
            Java_CreditCard_getBasicCardIssuerNetwork(env, jcard)));
  }
  card->set_virtual_card_enrollment_state(
      static_cast<CreditCard::VirtualCardEnrollmentState>(
          Java_CreditCard_getVirtualCardEnrollmentState(env, jcard)));
  card->set_product_description(
      Java_CreditCard_getProductDescription(env, jcard));
  card->set_cvc(Java_CreditCard_getCvc(env, jcard));
  ScopedJavaLocalRef<jstring> issuer_id =
      Java_CreditCard_getIssuerId(env, jcard);
  if (issuer_id) {
    card->set_issuer_id(ConvertJavaStringToUTF8(env, issuer_id));
  }
  ScopedJavaLocalRef<jobject> java_product_terms_url =
      Java_CreditCard_getProductTermsUrl(env, jcard);
  if (!java_product_terms_url.is_null()) {
    card->set_product_terms_url(
        url::GURLAndroid::ToNativeGURL(env, java_product_terms_url));
  }
}

jboolean PersonalDataManagerAndroid::IsDataLoaded(JNIEnv* env) const {
  return pdm_observation_.GetSource()->IsDataLoaded();
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileGUIDsForSettings(JNIEnv* env) {
  return GetProfileGUIDs(env, address_data_manager().GetProfilesForSettings());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileGUIDsToSuggest(JNIEnv* env) {
  return GetProfileGUIDs(env, address_data_manager().GetProfilesToSuggest());
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetProfileByGUID(
    JNIEnv* env,
    std::string& guid) {
  const AutofillProfile* profile =
      address_data_manager().GetProfileByGUID(guid);
  if (!profile) {
    return ScopedJavaLocalRef<jobject>();
  }
  return profile->CreateJavaObject(g_browser_process->GetApplicationLocale());
}

jboolean PersonalDataManagerAndroid::IsEligibleForAddressAccountStorage(
    JNIEnv* env) {
  return address_data_manager().IsEligibleForAddressAccountStorage();
}

std::string PersonalDataManagerAndroid::GetDefaultCountryCodeForNewAddress(
    JNIEnv* env) const {
  return address_data_manager().GetDefaultCountryCodeForNewAddress().value();
}

std::string PersonalDataManagerAndroid::SetProfile(
    JNIEnv* env,
    const JavaParamRef<jobject>& jprofile,
    std::string& guid) {
  AutofillProfile profile = AutofillProfile::CreateFromJavaObject(
      jprofile, address_data_manager().GetProfileByGUID(guid),
      g_browser_process->GetApplicationLocale());

  const bool use_existing_profile = !guid.empty();
  const autofill::AutofillProfile* existing_profile = nullptr;
  if (use_existing_profile) {
    existing_profile = address_data_manager().GetProfileByGUID(guid);
  }

  RecordAlternativeNameSeparatorUsage(profile, existing_profile);

  if (!use_existing_profile) {
    address_data_manager().AddProfile(profile);
  } else {
    address_data_manager().UpdateProfile(profile);
  }

  return profile.guid();
}

std::string PersonalDataManagerAndroid::SetProfileToLocal(
    JNIEnv* env,
    const JavaParamRef<jobject>& jprofile,
    std::string& guid) {
  const AutofillProfile* target_profile =
      address_data_manager().GetProfileByGUID(guid);
  AutofillProfile profile = AutofillProfile::CreateFromJavaObject(
      jprofile, target_profile, g_browser_process->GetApplicationLocale());

  if (target_profile != nullptr) {
    address_data_manager().UpdateProfile(profile);
  } else {
    address_data_manager().AddProfile(profile);
  }

  return profile.guid();
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileLabelsForSettings(JNIEnv* env) {
  return GetProfileLabels(env, false /* address_only */,
                          false /* include_name_in_label */,
                          true /* include_organization_in_label */,
                          true /* include_country_in_label */,
                          address_data_manager().GetProfilesForSettings());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileLabelsToSuggest(
    JNIEnv* env,
    jboolean include_name_in_label,
    jboolean include_organization_in_label,
    jboolean include_country_in_label) {
  return GetProfileLabels(env, true /* address_only */, include_name_in_label,
                          include_organization_in_label,
                          include_country_in_label,
                          address_data_manager().GetProfilesToSuggest());
}

std::u16string
PersonalDataManagerAndroid::GetShippingAddressLabelForPaymentRequest(
    JNIEnv* env,
    const JavaParamRef<jobject>& jprofile,
    std::string& guid,
    bool include_country_in_label) {
  // The full name is not included in the label for shipping address. It is
  // added separately instead.
  static constexpr auto kLabelFields = std::to_array<FieldType>(
      {COMPANY_NAME, ADDRESS_HOME_LINE1, ADDRESS_HOME_LINE2,
       ADDRESS_HOME_DEPENDENT_LOCALITY, ADDRESS_HOME_CITY, ADDRESS_HOME_STATE,
       ADDRESS_HOME_ZIP, ADDRESS_HOME_SORTING_CODE, ADDRESS_HOME_COUNTRY});
  base::span<const FieldType> label_fields = base::span(kLabelFields);
  if (!include_country_in_label) {
    label_fields = label_fields.first<kLabelFields.size() - 1>();
  }

  AutofillProfile profile = AutofillProfile::CreateFromJavaObject(
      jprofile, address_data_manager().GetProfileByGUID(guid),
      g_browser_process->GetApplicationLocale());

  return profile.ConstructInferredLabel(
      label_fields, /*num_fields_to_use=*/label_fields.size(),
      g_browser_process->GetApplicationLocale());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetCreditCardGUIDsForSettings(JNIEnv* env) {
  return GetCreditCardGUIDs(env, payments_data_manager().GetCreditCards());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetCreditCardGUIDsToSuggest(JNIEnv* env) {
  return GetCreditCardGUIDs(env,
                            GetCreditCardsToSuggest(payments_data_manager()));
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetCreditCardByGUID(
    JNIEnv* env,
    std::string& guid) {
  const CreditCard* card = payments_data_manager().GetCreditCardByGUID(guid);
  if (!card) {
    return ScopedJavaLocalRef<jobject>();
  }
  return PersonalDataManagerAndroid::CreateJavaCreditCardFromNative(env, *card);
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetCreditCardForNumber(
    JNIEnv* env,
    std::u16string& card_number) {
  // A local card with empty GUID.
  CreditCard card("", "");
  card.SetNumber(card_number);
  return PersonalDataManagerAndroid::CreateJavaCreditCardFromNative(env, card);
}

std::string PersonalDataManagerAndroid::SetCreditCard(
    JNIEnv* env,
    const JavaParamRef<jobject>& jcard) {
  std::string guid = Java_CreditCard_getGUID(env, jcard);

  CreditCard card;
  PopulateNativeCreditCardFromJava(jcard, env, &card);

  if (guid.empty()) {
    payments_data_manager().AddCreditCard(card);
  } else {
    card.set_guid(guid);
    payments_data_manager().UpdateCreditCard(card);
  }
  return card.guid();
}

void PersonalDataManagerAndroid::UpdateServerCardBillingAddress(
    JNIEnv* env,
    const JavaParamRef<jobject>& jcard) {
  CreditCard card;
  PopulateNativeCreditCardFromJava(jcard, env, &card);

  payments_data_manager().UpdateServerCardsMetadata({card});
}

void PersonalDataManagerAndroid::RemoveByGUID(JNIEnv* env,
                                              const std::string& guid) {
  payments_data_manager().RemoveByGUID(guid);
}

void PersonalDataManagerAndroid::RemoveProfile(JNIEnv* env,
                                               const std::string& guid) {
  address_data_manager().RemoveProfile(guid);
}

void PersonalDataManagerAndroid::DeleteAllLocalCreditCards(JNIEnv* env) {
  payments_data_manager().DeleteAllLocalCreditCards();
}

void PersonalDataManagerAndroid::OnPersonalDataChanged() {
  JNIEnv* env = base::android::AttachCurrentThread();
  auto java_obj = weak_java_obj_.get(env);
  if (java_obj.is_null()) {
    return;
  }
  Java_PersonalDataManager_personalDataChanged(env, java_obj);
}

void PersonalDataManagerAndroid::RecordAndLogProfileUse(JNIEnv* env,
                                                        std::string& guid) {
  const AutofillProfile* profile =
      address_data_manager().GetProfileByGUID(guid);
  if (profile) {
    address_data_manager().RecordUseOf(*profile);
  }
}

void PersonalDataManagerAndroid::RecordAndLogCreditCardUse(JNIEnv* env,
                                                           std::string& guid) {
  if (const CreditCard* card =
          payments_data_manager().GetCreditCardByGUID(guid)) {
    payments_data_manager().RecordUseOfCard(*card);
  }
}

jboolean PersonalDataManagerAndroid::HasProfiles(JNIEnv* env) {
  return !address_data_manager().GetProfiles().empty();
}

jboolean PersonalDataManagerAndroid::HasCreditCards(JNIEnv* env) {
  return !payments_data_manager().GetCreditCards().empty();
}

jboolean PersonalDataManagerAndroid::IsFidoAuthenticationAvailable(
    JNIEnv* env) {
  // Don't show toggle switch if user is unable to downstream cards.
  if (!payments_data_manager().IsPaymentsDownloadActive()) {
    return false;
  }
  // Show the toggle switch only if FIDO authentication is available.
  return IsCreditCardFidoAuthenticationEnabled();
}

// static
ScopedJavaLocalRef<jobject>
PersonalDataManagerAndroid::CreateJavaBankAccountFromNative(
    JNIEnv* env,
    const BankAccount& bank_account) {
  std::vector<int> supported_payment_rails_array =
      GetPaymentRailsFromPaymentInstrument(bank_account.payment_instrument());
  ScopedJavaLocalRef<jstring> jnickname = nullptr;
  if (!bank_account.payment_instrument().nickname().empty()) {
    jnickname = ConvertUTF16ToJavaString(
        env, bank_account.payment_instrument().nickname());
  }
  ScopedJavaLocalRef<jobject> jdisplay_icon_url = nullptr;
  if (!bank_account.payment_instrument().display_icon_url().is_empty()) {
    jdisplay_icon_url = url::GURLAndroid::FromNativeGURL(
        env, bank_account.payment_instrument().display_icon_url());
  }
  ScopedJavaLocalRef<jstring> jbank_name = nullptr;
  if (!bank_account.bank_name().empty()) {
    jbank_name = ConvertUTF16ToJavaString(env, bank_account.bank_name());
  }
  ScopedJavaLocalRef<jstring> jaccount_number_suffix = nullptr;
  if (!bank_account.account_number_suffix().empty()) {
    jaccount_number_suffix =
        ConvertUTF16ToJavaString(env, bank_account.account_number_suffix());
  }
  return Java_BankAccount_create(
      env,
      static_cast<jlong>(bank_account.payment_instrument().instrument_id()),
      jnickname, jdisplay_icon_url,
      ToJavaIntArray(env, supported_payment_rails_array),
      static_cast<jboolean>(
          bank_account.payment_instrument().is_fido_enrolled()),
      jbank_name, jaccount_number_suffix,
      static_cast<jint>(bank_account.account_type()));
}

// static
BankAccount PersonalDataManagerAndroid::CreateNativeBankAccountFromJava(
    JNIEnv* env,
    const JavaParamRef<jobject>& jbank_account) {
  int64_t instrument_id = static_cast<int64_t>(
      Java_PaymentInstrument_getInstrumentId(env, jbank_account));
  const ScopedJavaLocalRef<jstring>& jnickname =
      Java_PaymentInstrument_getNickname(env, jbank_account);
  std::u16string nickname;
  if (!jnickname.is_null()) {
    nickname = ConvertJavaStringToUTF16(jnickname);
  }
  const ScopedJavaLocalRef<jobject>& jdisplay_icon_url =
      Java_PaymentInstrument_getDisplayIconUrl(env, jbank_account);
  GURL display_icon_url = GURL();
  if (!jdisplay_icon_url.is_null()) {
    display_icon_url = url::GURLAndroid::ToNativeGURL(env, jdisplay_icon_url);
  }
  const ScopedJavaLocalRef<jstring>& jbank_name =
      Java_BankAccount_getBankName(env, jbank_account);
  std::u16string bank_name;
  if (!jbank_name.is_null()) {
    bank_name = ConvertJavaStringToUTF16(jbank_name);
  }
  const ScopedJavaLocalRef<jstring>& jaccount_number_suffix =
      Java_BankAccount_getAccountNumberSuffix(env, jbank_account);
  std::u16string account_number_suffix;
  if (!jaccount_number_suffix.is_null()) {
    account_number_suffix = ConvertJavaStringToUTF16(jaccount_number_suffix);
  }
  int jaccount_type = Java_BankAccount_getAccountType(env, jbank_account);
  BankAccount::AccountType bank_account_type =
      BankAccount::AccountType::kUnknown;
  if (jaccount_type > static_cast<int>(BankAccount::AccountType::kUnknown) &&
      jaccount_type <=
          static_cast<int>(BankAccount::AccountType::kTransactingAccount)) {
    bank_account_type = static_cast<BankAccount::AccountType>(jaccount_type);
  }
  return BankAccount(instrument_id, nickname, display_icon_url, bank_name,
                     account_number_suffix, bank_account_type);
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetEwallets(
    JNIEnv* env) {
  std::vector<base::android::ScopedJavaLocalRef<jobject>> jewallets_list;
  std::ranges::transform(payments_data_manager().GetEwalletAccounts(),
                         std::back_inserter(jewallets_list),
                         [env](const Ewallet& ewallet) {
                           return CreateJavaEwalletFromNative(env, ewallet);
                         });
  ScopedJavaLocalRef<jclass> type = base::android::GetClass(
      env, "org/chromium/components/autofill/payments/Ewallet");
  return base::android::ToTypedJavaArrayOfObjects(env, jewallets_list,
                                                  type.obj());
}

// static
ScopedJavaLocalRef<jobject>
PersonalDataManagerAndroid::CreateJavaEwalletFromNative(
    JNIEnv* env,
    const Ewallet& ewallet) {
  std::vector<int> supported_payment_rails_array =
      GetPaymentRailsFromPaymentInstrument(ewallet.payment_instrument());

  ScopedJavaLocalRef<jstring> jnickname = nullptr;
  if (!ewallet.payment_instrument().nickname().empty()) {
    jnickname =
        ConvertUTF16ToJavaString(env, ewallet.payment_instrument().nickname());
  }

  ScopedJavaLocalRef<jobject> jdisplay_icon_url = nullptr;
  if (!ewallet.payment_instrument().display_icon_url().is_empty()) {
    jdisplay_icon_url = url::GURLAndroid::FromNativeGURL(
        env, ewallet.payment_instrument().display_icon_url());
  }

  ScopedJavaLocalRef<jstring> jewallet_name = nullptr;
  if (!ewallet.ewallet_name().empty()) {
    jewallet_name = ConvertUTF16ToJavaString(env, ewallet.ewallet_name());
  }

  ScopedJavaLocalRef<jstring> jaccount_display_name = nullptr;
  if (!ewallet.account_display_name().empty()) {
    jaccount_display_name =
        ConvertUTF16ToJavaString(env, ewallet.account_display_name());
  }

  return Java_Ewallet_create(
      env, static_cast<jlong>(ewallet.payment_instrument().instrument_id()),
      jnickname, jdisplay_icon_url,
      ToJavaIntArray(env, supported_payment_rails_array),
      static_cast<jboolean>(ewallet.payment_instrument().is_fido_enrolled()),
      jewallet_name, jaccount_display_name);
}

// static
Ewallet PersonalDataManagerAndroid::CreateNativeEwalletFromJava(
    JNIEnv* env,
    const JavaParamRef<jobject>& jewallet) {
  int64_t instrument_id = static_cast<int64_t>(
      Java_PaymentInstrument_getInstrumentId(env, jewallet));

  const ScopedJavaLocalRef<jstring>& jnickname =
      Java_PaymentInstrument_getNickname(env, jewallet);
  std::u16string nickname;
  if (!jnickname.is_null()) {
    nickname = ConvertJavaStringToUTF16(jnickname);
  }

  const ScopedJavaLocalRef<jobject>& jdisplay_icon_url =
      Java_PaymentInstrument_getDisplayIconUrl(env, jewallet);
  GURL display_icon_url = GURL();
  if (!jdisplay_icon_url.is_null()) {
    display_icon_url = url::GURLAndroid::ToNativeGURL(env, jdisplay_icon_url);
  }

  bool is_fido_enrolled = static_cast<bool>(
      Java_PaymentInstrument_getIsFidoEnrolled(env, jewallet));

  const ScopedJavaLocalRef<jstring>& jewallet_name =
      Java_Ewallet_getEwalletName(env, jewallet);
  std::u16string ewallet_name;
  if (!jewallet_name.is_null()) {
    ewallet_name = ConvertJavaStringToUTF16(jewallet_name);
  }

  const ScopedJavaLocalRef<jstring>& jaccount_display_name =
      Java_Ewallet_getAccountDisplayName(env, jewallet);
  std::u16string account_display_name;
  if (!jaccount_display_name.is_null()) {
    account_display_name = ConvertJavaStringToUTF16(jaccount_display_name);
  }

  return Ewallet(instrument_id, nickname, display_icon_url, ewallet_name,
                 account_display_name, /*supported_payment_link_uris=*/{},
                 is_fido_enrolled);
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetProfileGUIDs(
    JNIEnv* env,
    const std::vector<const AutofillProfile*>& profiles) {
  std::vector<std::u16string> guids;
  for (const AutofillProfile* profile : profiles) {
    guids.push_back(base::UTF8ToUTF16(profile->guid()));
  }

  return base::android::ToJavaArrayOfStrings(env, guids);
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetCreditCardGUIDs(
    JNIEnv* env,
    const std::vector<const CreditCard*>& credit_cards) {
  std::vector<std::u16string> guids;
  guids.reserve(credit_cards.size());
  for (const CreditCard* credit_card : credit_cards) {
    guids.push_back(base::UTF8ToUTF16(credit_card->guid()));
  }
  return base::android::ToJavaArrayOfStrings(env, guids);
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetProfileLabels(
    JNIEnv* env,
    bool address_only,
    bool include_name_in_label,
    bool include_organization_in_label,
    bool include_country_in_label,
    std::vector<const AutofillProfile*> profiles) {
  FieldTypeSet suggested_fields;
  size_t minimal_fields_shown = 2;
  if (address_only) {
    suggested_fields = FieldTypeSet();
    if (include_name_in_label) {
      suggested_fields.insert(NAME_FULL);
    }
    if (include_organization_in_label) {
      suggested_fields.insert(COMPANY_NAME);
    }
    suggested_fields.insert(ADDRESS_HOME_LINE1);
    suggested_fields.insert(ADDRESS_HOME_LINE2);
    suggested_fields.insert(ADDRESS_HOME_DEPENDENT_LOCALITY);
    suggested_fields.insert(ADDRESS_HOME_CITY);
    suggested_fields.insert(ADDRESS_HOME_STATE);
    suggested_fields.insert(ADDRESS_HOME_ZIP);
    suggested_fields.insert(ADDRESS_HOME_SORTING_CODE);
    if (include_country_in_label) {
      suggested_fields.insert(ADDRESS_HOME_COUNTRY);
    }
    minimal_fields_shown = suggested_fields.size();
  }

  FieldType excluded_field = include_name_in_label ? UNKNOWN_TYPE : NAME_FULL;

  std::vector<std::u16string> labels = AutofillProfile::CreateInferredLabels(
      profiles,
      address_only ? std::make_optional(suggested_fields) : std::nullopt,
      /*triggering_field_type=*/std::nullopt, {excluded_field},
      minimal_fields_shown, g_browser_process->GetApplicationLocale());

  return base::android::ToJavaArrayOfStrings(env, labels);
}

// static
std::vector<int>
PersonalDataManagerAndroid::GetPaymentRailsFromPaymentInstrument(
    const PaymentInstrument& payment_instrument) {
  DenseSet<PaymentInstrument::PaymentRail> payment_instrument_supported_rails =
      payment_instrument.supported_rails();
  std::vector<int> supported_payment_rails_array(
      payment_instrument.supported_rails().size());
  std::transform(payment_instrument_supported_rails.begin(),
                 payment_instrument_supported_rails.end(),
                 supported_payment_rails_array.begin(),
                 [](PaymentInstrument::PaymentRail rail) {
                   return static_cast<int>(rail);
                 });
  return supported_payment_rails_array;
}

ScopedJavaLocalRef<jobject>
PersonalDataManagerAndroid::CreateJavaIbanFromNative(JNIEnv* env,
                                                     const Iban& iban) {
  switch (iban.record_type()) {
    case Iban::kLocalIban:
      return Java_Iban_createLocal(env, iban.guid(),
                                   iban.GetIdentifierStringForAutofillDisplay(),
                                   iban.nickname(), iban.value());
    case Iban::kServerIban:
      return Java_Iban_createServer(
          env, iban.instrument_id(),
          iban.GetIdentifierStringForAutofillDisplay(), iban.nickname(),
          iban.value());
    case Iban::kUnknown:
      return Java_Iban_createEphemeral(
          env, iban.GetIdentifierStringForAutofillDisplay(), iban.nickname(),
          iban.value());
  }
}

void PersonalDataManagerAndroid::PopulateNativeIbanFromJava(
    const JavaRef<jobject>& jiban,
    JNIEnv* env,
    Iban* iban) {
  iban->set_nickname(Java_Iban_getNickname(env, jiban));
  iban->set_value(Java_Iban_getValue(env, jiban));
  // Only set the GUID if it is an existing local IBAN.
  Iban::RecordType record_type =
      static_cast<Iban::RecordType>(Java_Iban_getRecordType(env, jiban));
  if (record_type == Iban::RecordType::kUnknown) {
    return;
  } else if (record_type == Iban::RecordType::kLocalIban) {
    iban->set_identifier(Iban::Guid(Java_Iban_getGuid(env, jiban)));
    iban->set_record_type(Iban::RecordType::kLocalIban);
  } else {
    // Server IBANs shouldn't use PopulateNativeIbanFromJava().
    NOTREACHED();
  }
}

// TODO(crbug.com/369626137): Move test functions to a new test helper file.
void PersonalDataManagerAndroid::AddServerIbanForTest(
    JNIEnv* env,
    const JavaParamRef<jobject>& jiban) {
  std::unique_ptr<Iban> iban = std::make_unique<Iban>();
  iban->set_nickname(Java_Iban_getNickname(env, jiban));
  iban->set_identifier(
      Iban::InstrumentId(Java_Iban_getInstrumentId(env, jiban)));
  iban->set_record_type(Iban::RecordType::kServerIban);
  payments_data_manager().AddServerIbanForTest(std::move(iban));  // IN-TEST
  pdm_observation_.GetSource()->NotifyPersonalDataObserver();
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetIbanByGuid(
    JNIEnv* env,
    std::string& guid) {
  const Iban* iban = payments_data_manager().GetIbanByGUID(guid);
  if (!iban) {
    return ScopedJavaLocalRef<jobject>();
  }

  return PersonalDataManagerAndroid::CreateJavaIbanFromNative(env, *iban);
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetIbansForSettings(JNIEnv* env) {
  std::vector<ScopedJavaLocalRef<jobject>> j_ibans_list;
  for (const Iban* iban : payments_data_manager().GetIbans()) {
    j_ibans_list.push_back(CreateJavaIbanFromNative(env, *iban));
  }
  ScopedJavaLocalRef<jclass> type = base::android::GetClass(
      env, "org/chromium/chrome/browser/autofill/PersonalDataManager$Iban");
  return base::android::ToTypedJavaArrayOfObjects(env, j_ibans_list,
                                                  type.obj());
}

std::string PersonalDataManagerAndroid::AddOrUpdateLocalIban(
    JNIEnv* env,
    const JavaParamRef<jobject>& jiban) {
  Iban iban;
  PopulateNativeIbanFromJava(jiban, env, &iban);

  std::string guid;
  if (iban.record_type() == Iban::RecordType::kUnknown) {
    guid = payments_data_manager().AddAsLocalIban(std::move(iban));
  } else {
    guid = payments_data_manager().UpdateIban(iban);
  }
  return guid;
}

jboolean PersonalDataManagerAndroid::IsValidIban(JNIEnv* env,
                                                 std::u16string& iban_value) {
  return Iban::IsValid(iban_value);
}

jboolean PersonalDataManagerAndroid::ShouldShowAddIbanButtonOnSettingsPage(
    JNIEnv* env) {
  return ShouldShowIbanOnSettingsPage(
      payments_data_manager().GetCountryCodeForExperimentGroup(), prefs_);
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetMaskedBankAccounts(JNIEnv* env) {
  std::vector<ScopedJavaLocalRef<jobject>> j_bank_accounts_list;
  std::ranges::transform(payments_data_manager().GetMaskedBankAccounts(),
                         std::back_inserter(j_bank_accounts_list),
                         [env](const BankAccount& bank_account) {
                           return CreateJavaBankAccountFromNative(env,
                                                                  bank_account);
                         });
  ScopedJavaLocalRef<jclass> type = base::android::GetClass(
      env, "org/chromium/components/autofill/payments/BankAccount");
  return base::android::ToTypedJavaArrayOfObjects(env, j_bank_accounts_list,
                                                  type.obj());
}

jboolean PersonalDataManagerAndroid::IsAutofillProfileManaged(JNIEnv* env) {
  return prefs::IsAutofillProfileManaged(prefs_);
}

jboolean PersonalDataManagerAndroid::IsAutofillCreditCardManaged(JNIEnv* env) {
  return prefs::IsAutofillCreditCardManaged(prefs_);
}

// Returns the issuer network string according to PaymentRequest spec, or an
// empty string if the given card number is not valid and |jempty_if_invalid|
// is true.
static std::string JNI_PersonalDataManager_GetBasicCardIssuerNetwork(
    JNIEnv* env,
    std::u16string& card_number,
    const jboolean jempty_if_invalid) {
  if (static_cast<bool>(jempty_if_invalid) &&
      !IsValidCreditCardNumber(card_number)) {
    return "";
  }
  return data_util::GetPaymentRequestData(GetCardNetwork(card_number))
      .basic_card_issuer_network;
}

// Returns an ISO 3166-1-alpha-2 country code for a `country_name` using
// the application locale, or an empty string.
static std::string JNI_PersonalDataManager_ToCountryCode(
    JNIEnv* env,
    std::u16string& country_name) {
  return CountryNames::GetInstance()->GetCountryCode(country_name);
}

static jlong JNI_PersonalDataManager_Init(JNIEnv* env,
                                          const JavaParamRef<jobject>& obj,
                                          Profile* profile) {
  CHECK(profile);
  PersonalDataManagerAndroid* personal_data_manager_android =
      new PersonalDataManagerAndroid(
          env, obj, PersonalDataManagerFactory::GetForBrowserContext(profile),
          profile->GetPrefs());
  return reinterpret_cast<intptr_t>(personal_data_manager_android);
}

}  // namespace autofill
