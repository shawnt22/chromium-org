// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_IOS_ACCOUNT_SELECT_FILL_DATA_H_
#define COMPONENTS_PASSWORD_MANAGER_IOS_ACCOUNT_SELECT_FILL_DATA_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/types/expected.h"
#include "components/autofill/core/common/unique_ids.h"
#include "url/gurl.h"

namespace autofill {
struct PasswordFormFillData;
}

namespace password_manager {

struct UsernameAndRealm {
  std::u16string username;
  std::string realm;
  bool is_backup_credential = false;
};

// Keeps all required for filling information from Password Form.
struct FormInfo {
  FormInfo();
  ~FormInfo();
  FormInfo(const FormInfo&);
  GURL origin;
  autofill::FormRendererId form_id;
  autofill::FieldRendererId username_element_id;
  autofill::FieldRendererId password_element_id;
};

struct Credential {
  Credential(const std::u16string& username,
             const std::u16string& password,
             const std::optional<std::u16string>& backup_password,
             const std::string& realm);
  ~Credential();
  Credential(const Credential&);
  std::u16string username;
  std::u16string password;
  std::optional<std::u16string> backup_password;
  std::string realm;
};

// Contains all information whis is required for filling the password form.
// TODO(crbug.com/40128249): Remove form name and field identifiers once
// unique IDs are used in filling.
struct FillData {
  FillData();
  ~FillData();
  FillData(const FillData& other);

  GURL origin;
  autofill::FormRendererId form_id;
  autofill::FieldRendererId username_element_id;
  std::u16string username_value;
  autofill::FieldRendererId password_element_id;
  std::u16string password_value;
};

// Represents an error when retrieving FormInfo.
enum class FormInfoRetrievalError {
  kNoFormMatch,
  kNoFieldMatch,
};

// Represents the status of getting FillData.
enum class FillDataRetrievalStatus {
  // Success, fill data could be retrieved.
  kSuccess,
  // Error because the frame didn't have any AccountSelectFillData at the
  // moment of getting fill data where new AccountSelectFillData had to
  // be created.
  kNoFrame,
  // Error because no form with fill data matched the form provided in the
  // query.
  kNoFormMatch,
  // Error because no form with fill data matched the field provided in the
  // query.
  kNoFieldMatch,
  // Error because there were no credentials that matched the username in the
  // query at the time of filling.
  kNoCredentials,
  // There was no `last_requested_form_`. Only applies if not stateless.
  kNoCachedLastRequestForm,
  kMaxValue = kNoCachedLastRequestForm
};

// Contains the result of retrieving FillData.
using FillDataRetrievalResult =
    base::expected<std::unique_ptr<FillData>, FillDataRetrievalStatus>;

// Represents the result of retrieving FormInfo.
using FormInfoRetrievalResult =
    base::expected<const FormInfo*, FormInfoRetrievalError>;

// Gets the equivalent FillDataRetrievalStatus for the provided
// FormInfoRetrievalError.
FillDataRetrievalStatus GetFillDataRetrievalStatus(
    FormInfoRetrievalError error);

// Handles data and logic for filling on account select. This class stores 2
// types of independent data - forms on the page and credentials saved for the
// current page. Based on the user action (clicks, typing values, choosing
// suggestions) this class decides which suggestions should be shown and which
// credentials should be filled.
class AccountSelectFillData {
 public:
  AccountSelectFillData();

  AccountSelectFillData(const AccountSelectFillData&) = delete;
  AccountSelectFillData& operator=(const AccountSelectFillData&) = delete;

  ~AccountSelectFillData();

  // Adds form structure from |form_data| to internal lists of known forms and
  // overrides known credentials with credentials from |form_data|. So only the
  // credentials from the latest |form_data| will be shown to the user.
  void Add(const autofill::PasswordFormFillData& form_data,
           bool always_populate_realm);
  void Reset();
  bool Empty() const;

  // Returns whether suggestions are available for field with id
  // |field_identifier| which is in the form with id |form_identifier|.
  bool IsSuggestionsAvailable(autofill::FormRendererId form_identifier,
                              autofill::FieldRendererId field_identifier,
                              bool is_password_field) const;

  // Returns suggestions for field with id |field_identifier| which is in the
  // form with id |form_identifier|.
  std::vector<UsernameAndRealm> RetrieveSuggestions(
      autofill::FormRendererId form_identifier,
      autofill::FieldRendererId field_identifier,
      bool is_password_field);

  // Returns data for password form filling based on |username| chosen by the
  // user.
  // RetrieveSuggestions should be called before in order to specify on which
  // field the user clicked.
  FillDataRetrievalResult GetFillData(const std::u16string& username) const;

  // Returns data for password form filling based on the |username| chosen by
  // the user and contextual information. This interface is meant to be used
  // when in stateless mode.
  FillDataRetrievalResult GetFillData(
      const std::u16string& username,
      autofill::FormRendererId form_renderer_id,
      autofill::FieldRendererId field_renderer_id,
      bool is_password_field) const;

  // Returns form information from |forms_| that has id |form_identifier|.
  // If |is_password_field| == false and |field_identifier| is not equal to
  // form username_element null is returned. If |is_password_field| == true then
  // |field_identifier| is ignored. That corresponds to the logic, that
  // suggestions should be shown on any password fields.
  FormInfoRetrievalResult GetFormInfo(
      autofill::FormRendererId form_identifier,
      autofill::FieldRendererId field_identifier,
      bool is_password_field) const;

  // Clear credentials cache.
  void ResetCache();

 private:
  // Returns data for password form filling based on the |username| chosen by
  // the user and contextual information provided through |requested_form|.
  FillDataRetrievalResult GetFillData(
      const std::u16string& username,
      const FormInfo* requested_form,
      autofill::FieldRendererId password_field_id) const;

  // Keeps data about all known forms. The key is the pair (form_id, username
  // field_name).
  std::map<autofill::FormRendererId, FormInfo> forms_;

  // Keeps all known credentials.
  std::vector<Credential> credentials_;

  // Mutable because it's updated from RetrieveSuggestions, which is logically
  // should be const.
  // Keeps information about last form that was requested in
  // RetrieveSuggestions.
  mutable raw_ptr<const FormInfo> last_requested_form_ = nullptr;
  // Keeps id of the last requested field if it was password otherwise the empty
  // string.
  autofill::FieldRendererId last_requested_password_field_id_;
};

}  // namespace  password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_IOS_ACCOUNT_SELECT_FILL_DATA_H_
