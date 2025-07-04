// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/public/identity_manager/account_info.h"

#include "build/build_config.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/identity_manager/signin_constants.h"
#include "components/signin/public/identity_manager/tribool.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_id.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/jni_string.h"
#include "components/signin/public/android/jni_headers/AccountInfo_jni.h"
#include "components/signin/public/android/jni_headers/CoreAccountInfo_jni.h"
#include "google_apis/gaia/android/jni_headers/CoreAccountId_jni.h"
#include "google_apis/gaia/android/jni_headers/GaiaId_jni.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/image/image_skia.h"
#endif

using signin::constants::kNoHostedDomainFound;

namespace {

// Updates |field| with |new_value| if non-empty and different; if |new_value|
// is equal to |default_value| then it won't override |field| unless it is not
// set. Returns whether |field| was changed.
bool UpdateField(std::string* field,
                 const std::string& new_value,
                 const char* default_value) {
  if (*field == new_value || new_value.empty()) {
    return false;
  }

  if (!field->empty() && default_value && new_value == default_value) {
    return false;
  }

  *field = new_value;
  return true;
}

// Updates |field| with |new_value| if different from the default value.
// Returns whether |field| was changed.
template <typename T>
bool UpdateField(T* field, T new_value, T default_value) {
  if (*field == new_value || new_value == default_value) {
    return false;
  }

  *field = new_value;
  return true;
}

// Updates |field| with |new_value| if non-empty. Returns whether |field| was
// changed.
bool UpdateField(GaiaId* field, const GaiaId& new_value) {
  if (*field == new_value || new_value.empty()) {
    return false;
  }

  *field = new_value;
  return true;
}

// Updates |field| with |new_value| if true. Returns whether |field| was
// changed.
bool UpdateField(bool* field, bool new_value) {
  return UpdateField<bool>(field, new_value, false);
}

// Updates |field| with |new_value| if true. Returns whether |field| was
// changed.
bool UpdateField(signin::Tribool* field, signin::Tribool new_value) {
  return UpdateField<signin::Tribool>(field, new_value,
                                      signin::Tribool::kUnknown);
}

}  // namespace

// This must be a string which can never be a valid picture URL.
const char kNoPictureURLFound[] = "NO_PICTURE_URL";

CoreAccountInfo::CoreAccountInfo() = default;

CoreAccountInfo::~CoreAccountInfo() = default;

CoreAccountInfo::CoreAccountInfo(const CoreAccountInfo& other) = default;

CoreAccountInfo::CoreAccountInfo(CoreAccountInfo&& other) noexcept = default;

CoreAccountInfo& CoreAccountInfo::operator=(const CoreAccountInfo& other) =
    default;

CoreAccountInfo& CoreAccountInfo::operator=(CoreAccountInfo&& other) noexcept =
    default;

bool CoreAccountInfo::IsEmpty() const {
  return account_id.empty() && email.empty() && gaia.empty();
}

AccountInfo::AccountInfo() = default;

AccountInfo::~AccountInfo() = default;

AccountInfo::AccountInfo(const AccountInfo& other) = default;

AccountInfo::AccountInfo(AccountInfo&& other) noexcept = default;

AccountInfo& AccountInfo::operator=(const AccountInfo& other) = default;

AccountInfo& AccountInfo::operator=(AccountInfo&& other) noexcept = default;

bool AccountInfo::IsEmpty() const {
  return CoreAccountInfo::IsEmpty() && hosted_domain.empty() &&
         full_name.empty() && given_name.empty() && locale.empty() &&
         picture_url.empty();
}

bool AccountInfo::IsValid() const {
  return !account_id.empty() && !email.empty() && !gaia.empty() &&
         !hosted_domain.empty() && !full_name.empty() && !given_name.empty() &&
         !picture_url.empty();
}

bool AccountInfo::UpdateWith(const AccountInfo& other) {
  if (account_id != other.account_id) {
    // Only updates with a compatible AccountInfo.
    return false;
  }

  bool modified = false;
  modified |= UpdateField(&gaia, other.gaia);
  modified |= UpdateField(&email, other.email, nullptr);
  modified |= UpdateField(&full_name, other.full_name, nullptr);
  modified |= UpdateField(&given_name, other.given_name, nullptr);
  modified |=
      UpdateField(&hosted_domain, other.hosted_domain, kNoHostedDomainFound);
  modified |= UpdateField(&locale, other.locale, nullptr);
  modified |= UpdateField(&picture_url, other.picture_url, kNoPictureURLFound);
  modified |= UpdateField(&is_child_account, other.is_child_account);
  modified |= UpdateField(&access_point, other.access_point,
                          signin_metrics::AccessPoint::kUnknown);
  modified |= UpdateField(&is_under_advanced_protection,
                          other.is_under_advanced_protection);
  modified |= capabilities.UpdateWith(other.capabilities);

  return modified;
}

// static
signin::Tribool AccountInfo::IsManaged(const std::string& hosted_domain) {
  return hosted_domain.empty()
             ? signin::Tribool::kUnknown
             : signin::TriboolFromBool(hosted_domain != kNoHostedDomainFound);
}

bool AccountInfo::IsMemberOfFlexOrg() const {
  return capabilities.is_subject_to_enterprise_policies() ==
             signin::Tribool::kTrue &&
         IsManaged(hosted_domain) != signin::Tribool::kTrue;
}

signin::Tribool AccountInfo::IsManaged() const {
  if (base::FeatureList::IsEnabled(
          kUseAccountCapabilityToDetermineAccountManagement)) {
    return capabilities.is_subject_to_enterprise_policies();
  }
  return IsManaged(hosted_domain);
}

bool AccountInfo::IsEduAccount() const {
  return capabilities.can_use_edu_features() == signin::Tribool::kTrue &&
         IsManaged(hosted_domain) == signin::Tribool::kTrue;
}

bool AccountInfo::CanHaveEmailAddressDisplayed() const {
  return capabilities.can_have_email_address_displayed() ==
             signin::Tribool::kTrue ||
         capabilities.can_have_email_address_displayed() ==
             signin::Tribool::kUnknown;
}

bool operator==(const CoreAccountInfo& l, const CoreAccountInfo& r) {
  return l.account_id == r.account_id && l.gaia == r.gaia &&
         gaia::AreEmailsSame(l.email, r.email) &&
         l.is_under_advanced_protection == r.is_under_advanced_protection;
}

std::ostream& operator<<(std::ostream& os, const CoreAccountInfo& account) {
  os << "account_id: " << account.account_id << ", gaia: " << account.gaia
     << ", email: " << account.email << ", adv_prot: " << std::boolalpha
     << account.is_under_advanced_protection;
  return os;
}

#if BUILDFLAG(IS_ANDROID)
base::android::ScopedJavaLocalRef<jobject> ConvertToJavaCoreAccountInfo(
    JNIEnv* env,
    const CoreAccountInfo& account_info) {
  CHECK(!account_info.IsEmpty());
  return signin::Java_CoreAccountInfo_Constructor(
      env, ConvertToJavaCoreAccountId(env, account_info.account_id),
      base::android::ConvertUTF8ToJavaString(env, account_info.email),
      Java_GaiaId_Constructor(env, account_info.gaia.ToString()));
}

base::android::ScopedJavaLocalRef<jobject> ConvertToJavaAccountInfo(
    JNIEnv* env,
    const AccountInfo& account_info) {
  CHECK(!account_info.IsEmpty());
  // Empty domain means that the management status is unknown, which is
  // represented by `null` hostedDomain on the Java side.
  base::android::ScopedJavaLocalRef<jstring> hosted_domain =
      account_info.hosted_domain.empty()
          ? nullptr
          : base::android::ConvertUTF8ToJavaString(env,
                                                   account_info.hosted_domain);
  base::android::ScopedJavaLocalRef<jobject> account_image =
      account_info.account_image.IsEmpty()
          ? nullptr
          : gfx::ConvertToJavaBitmap(
                *account_info.account_image.AsImageSkia().bitmap());
  return signin::Java_AccountInfo_Constructor(
      env, ConvertToJavaCoreAccountId(env, account_info.account_id),
      base::android::ConvertUTF8ToJavaString(env, account_info.email),
      Java_GaiaId_Constructor(env, account_info.gaia.ToString()),
      base::android::ConvertUTF8ToJavaString(env, account_info.full_name),
      base::android::ConvertUTF8ToJavaString(env, account_info.given_name),
      hosted_domain, account_image,
      account_info.capabilities.ConvertToJavaAccountCapabilities(env));
}

base::android::ScopedJavaLocalRef<jobject> ConvertToJavaCoreAccountId(
    JNIEnv* env,
    const CoreAccountId& account_id) {
  CHECK(!account_id.empty());
  return Java_CoreAccountId_Constructor(
      env, Java_GaiaId_Constructor(env, account_id.ToString()));
}

base::android::ScopedJavaLocalRef<jobject> ConvertToJavaGaiaId(
    JNIEnv* env,
    const GaiaId& gaia_id) {
  CHECK(!gaia_id.empty());
  return Java_GaiaId_Constructor(env, gaia_id.ToString());
}

CoreAccountInfo ConvertFromJavaCoreAccountInfo(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& j_core_account_info) {
  CHECK(j_core_account_info);
  CoreAccountInfo account;
  account.account_id = ConvertFromJavaCoreAccountId(
      env, signin::Java_CoreAccountInfo_getId(env, j_core_account_info));
  account.gaia = GaiaId(Java_GaiaId_toString(
      env, signin::Java_CoreAccountInfo_getGaiaId(env, j_core_account_info)));
  account.email = base::android::ConvertJavaStringToUTF8(
      signin::Java_CoreAccountInfo_getEmail(env, j_core_account_info));
  return account;
}

AccountInfo ConvertFromJavaAccountInfo(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& j_account_info) {
  CHECK(j_account_info);
  AccountInfo account;
  account.account_id = ConvertFromJavaCoreAccountId(
      env, signin::Java_CoreAccountInfo_getId(env, j_account_info));
  account.gaia = ConvertFromJavaGaiaId(
      env, signin::Java_CoreAccountInfo_getGaiaId(env, j_account_info));
  account.email = base::android::ConvertJavaStringToUTF8(
      signin::Java_CoreAccountInfo_getEmail(env, j_account_info));
  account.full_name = base::android::ConvertJavaStringToUTF8(
      signin::Java_AccountInfo_getFullName(env, j_account_info));
  account.given_name = base::android::ConvertJavaStringToUTF8(
      signin::Java_AccountInfo_getGivenName(env, j_account_info));
  account.hosted_domain = base::android::ConvertJavaStringToUTF8(
      signin::Java_AccountInfo_getRawHostedDomain(env, j_account_info));
  // TODO(crbug.com/348373729): Marshal account image & capabilities from Java.
  return account;
}

CoreAccountId ConvertFromJavaCoreAccountId(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& j_core_account_id) {
  CHECK(j_core_account_id);
  CoreAccountId id =
      CoreAccountId::FromString(base::android::ConvertJavaStringToUTF8(
          Java_CoreAccountId_toString(env, j_core_account_id)));
  return id;
}

GaiaId ConvertFromJavaGaiaId(JNIEnv* env,
                             const base::android::JavaRef<jobject>& j_gaia_id) {
  CHECK(j_gaia_id);
  return GaiaId(Java_GaiaId_toString(env, j_gaia_id));
}

#endif
