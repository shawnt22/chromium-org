// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/ios/account_select_fill_data.h"

#include <algorithm>
#include <variant>

#include "base/debug/crash_logging.h"
#include "base/feature_list.h"
#include "base/strings/string_util.h"
#include "base/types/expected.h"
#include "base/types/expected_macros.h"
#include "components/autofill/core/common/password_form_fill_data.h"
#include "components/autofill/core/common/unique_ids.h"
#include "components/password_manager/core/browser/features/password_features.h"
#include "components/password_manager/ios/features.h"

using autofill::FieldRendererId;
using autofill::FormRendererId;

namespace password_manager {

namespace {

// Returns true if credentials are eligible. For example, credentials are
// ineglible when there are only credentials with an empty username available
// for a single username form.
bool AreCredentialsEligibleForFilling(
    const FormInfo* form_info,
    const std::vector<Credential>& credentials) {
  // Check that this is only called when `form_info` is available.
  CHECK(form_info);

  const bool is_single_username = form_info && form_info->username_element_id &&
                                  !form_info->password_element_id;

  const auto has_empty_username = [](const Credential& c) {
    return c.username.empty();
  };
  return !(is_single_username &&
           std::ranges::all_of(credentials, has_empty_username));
}

}  // namespace

FillDataRetrievalStatus GetFillDataRetrievalStatus(
    FormInfoRetrievalError error) {
  switch (error) {
    case FormInfoRetrievalError::kNoFormMatch:
      return FillDataRetrievalStatus::kNoFormMatch;
    case FormInfoRetrievalError::kNoFieldMatch:
      return FillDataRetrievalStatus::kNoFieldMatch;
  }
}

FillData::FillData() = default;
FillData::~FillData() = default;
FillData::FillData(const FillData& other) = default;

FormInfo::FormInfo() = default;
FormInfo::~FormInfo() = default;
FormInfo::FormInfo(const FormInfo&) = default;

Credential::Credential(const std::u16string& username,
                       const std::u16string& password,
                       const std::optional<std::u16string>& backup_password,
                       const std::string& realm)
    : username(username),
      password(password),
      backup_password(backup_password),
      realm(realm) {}
Credential::~Credential() = default;
Credential::Credential(const Credential&) = default;

AccountSelectFillData::AccountSelectFillData() = default;
AccountSelectFillData::~AccountSelectFillData() = default;

void AccountSelectFillData::Add(const autofill::PasswordFormFillData& form_data,
                                bool always_populate_realm) {
  auto iter_ok = forms_.insert(
      std::make_pair(form_data.form_renderer_id.value(), FormInfo()));
  FormInfo& form_info = iter_ok.first->second;
  form_info.origin = form_data.url;
  form_info.form_id = form_data.form_renderer_id;
  form_info.username_element_id = form_data.username_element_renderer_id;
  form_info.password_element_id = form_data.password_element_renderer_id;

  // Suggested credentials don't depend on a clicked form. It's better to use
  // the latest known credentials, since credentials can be updated between
  // loading of different forms.
  credentials_.clear();

  credentials_.push_back(
      {form_data.preferred_login.username_value,
       form_data.preferred_login.password_value,
       form_data.preferred_login.backup_password_value,
       always_populate_realm && form_data.preferred_login.realm.empty()
           ? form_data.url.spec()
           : form_data.preferred_login.realm});

  for (const auto& login : form_data.additional_logins) {
    const std::u16string& username = login.username_value;
    const std::u16string& password = login.password_value;
    const std::optional<std::u16string>& backup_password =
        login.backup_password_value;
    const std::string& realm = login.realm;
    if (always_populate_realm && realm.empty()) {
      credentials_.push_back(
          {username, password, backup_password, form_data.url.spec()});
    } else {
      credentials_.push_back({username, password, backup_password, realm});
    }
  }
}

void AccountSelectFillData::Reset() {
  forms_.clear();
  credentials_.clear();
  last_requested_form_ = nullptr;
}

void AccountSelectFillData::ResetCache() {
  credentials_.clear();
}

bool AccountSelectFillData::Empty() const {
  return credentials_.empty();
}

bool AccountSelectFillData::IsSuggestionsAvailable(
    FormRendererId form_identifier,
    FieldRendererId field_identifier,
    bool is_password_field) const {
  ASSIGN_OR_RETURN(
      const FormInfo* form_info,
      GetFormInfo(form_identifier, field_identifier, is_password_field),
      [](auto) { return false; });
  return AreCredentialsEligibleForFilling(form_info, credentials_);
}

std::vector<UsernameAndRealm> AccountSelectFillData::RetrieveSuggestions(
    FormRendererId form_identifier,
    FieldRendererId field_identifier,
    bool is_password_field) {
  FormInfoRetrievalResult form_info_result =
      GetFormInfo(form_identifier, field_identifier, is_password_field);
  CHECK(form_info_result.has_value());
  last_requested_form_ = form_info_result.value();
  CHECK(last_requested_form_);

  if (!AreCredentialsEligibleForFilling(last_requested_form_, credentials_)) {
    return {};
  }

  last_requested_password_field_id_ =
      is_password_field ? field_identifier : FieldRendererId();
  std::vector<UsernameAndRealm> usernames;
  for (const Credential& credential : credentials_) {
    usernames.push_back({credential.username, credential.realm});
    // If `credential` has a backup password, create a separate UsernameAndRealm
    // entry for it.
    if (credential.backup_password &&
        base::FeatureList::IsEnabled(
            password_manager::features::kIOSFillRecoveryPassword)) {
      usernames.push_back({credential.username, credential.realm,
                           /*is_backup_credential=*/true});
    }
  }

  return usernames;
}

FillDataRetrievalResult AccountSelectFillData::GetFillData(
    const std::u16string& username,
    autofill::FormRendererId form_renderer_id,
    autofill::FieldRendererId field_renderer_id,
    bool is_likely_real_password_field) const {
  ASSIGN_OR_RETURN(const FormInfo* form_info,
                   GetFormInfo(form_renderer_id, field_renderer_id,
                               is_likely_real_password_field),
                   [](auto e) { return GetFillDataRetrievalStatus(e); });
  autofill::FieldRendererId password_field_id =
      is_likely_real_password_field ? field_renderer_id
                                    : autofill::FieldRendererId();

  return GetFillData(username, form_info, password_field_id);
}

FillDataRetrievalResult AccountSelectFillData::GetFillData(
    const std::u16string& username) const {
  if (!last_requested_form_) {
    SCOPED_CRASH_KEY_NUMBER(
        "Bug6401794", "fill_data_status",
        static_cast<int>(FillDataRetrievalStatus::kNoCachedLastRequestForm));
    DUMP_WILL_BE_NOTREACHED();
    return base::unexpected(FillDataRetrievalStatus::kNoCachedLastRequestForm);
  }
  return GetFillData(username, last_requested_form_,
                     last_requested_password_field_id_);
}

FillDataRetrievalResult AccountSelectFillData::GetFillData(
    const std::u16string& username,
    const FormInfo* requested_form,
    autofill::FieldRendererId password_field_id) const {
  // There must be a `requested_form` at this point. It is the responsibility of
  // the caller to ensure that.
  CHECK(requested_form);

  auto it = std::ranges::find(credentials_, username, &Credential::username);
  if (it == credentials_.end()) {
    return base::unexpected(FillDataRetrievalStatus::kNoCredentials);
  }
  const Credential& credential = *it;
  auto result = std::make_unique<FillData>();
  result->origin = requested_form->origin;
  result->form_id = requested_form->form_id;
  result->username_element_id = requested_form->username_element_id;
  result->username_value = credential.username;
  result->password_element_id =
      password_field_id ?: requested_form->password_element_id;
  result->password_value = credential.password;
  return std::move(result);
}

FormInfoRetrievalResult AccountSelectFillData::GetFormInfo(
    FormRendererId form_identifier,
    FieldRendererId field_identifier,
    bool is_password_field) const {
  auto it = forms_.find(form_identifier);
  if (it == forms_.end()) {
    return base::unexpected(FormInfoRetrievalError::kNoFormMatch);
  }
  const FormInfo* form_info =
      is_password_field || it->second.username_element_id == field_identifier
          ? &it->second
          : nullptr;
  if (!form_info) {
    return base::unexpected(FormInfoRetrievalError::kNoFieldMatch);
  }
  return form_info;
}

}  // namespace  password_manager
