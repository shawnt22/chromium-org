// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webauthn/sheet_models.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/ui/webauthn/user_actions.h"
#include "chrome/browser/ui/webauthn/webauthn_ui_helpers.h"
#include "chrome/browser/webauthn/authenticator_request_dialog_model.h"
#include "chrome/browser/webauthn/local_authentication_token.h"
#include "chrome/browser/webauthn/webauthn_metrics_util.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/strings/grit/components_strings.h"
#include "device/fido/features.h"
#include "device/fido/fido_constants.h"
#include "device/fido/fido_types.h"
#include "device/fido/pin.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if BUILDFLAG(IS_MAC)
#include "device/fido/mac/util.h"
#endif

namespace {

using CredentialMech = AuthenticatorRequestDialogModel::Mechanism::Credential;
using EnclaveMech = AuthenticatorRequestDialogModel::Mechanism::Enclave;
using PasswordMech = AuthenticatorRequestDialogModel::Mechanism::Password;
using ICloudKeychainMech =
    AuthenticatorRequestDialogModel::Mechanism::ICloudKeychain;
using Step = AuthenticatorRequestDialogModel::Step;

constexpr int kGpmArbitraryPinMinLength = 4;

bool IsLocalPasskeyOrEnclaveAuthenticatorOrPassword(
    const AuthenticatorRequestDialogModel::Mechanism& mech) {
  return (std::holds_alternative<CredentialMech>(mech.type) &&
          std::get<CredentialMech>(mech.type).value().source !=
              device::AuthenticatorType::kPhone) ||
         std::holds_alternative<EnclaveMech>(mech.type) ||
         std::holds_alternative<PasswordMech>(mech.type);
}

// Possibly returns a resident key warning if the model indicates that it's
// needed.
std::u16string PossibleResidentKeyWarning(
    AuthenticatorRequestDialogModel* dialog_model) {
  switch (dialog_model->resident_key_requirement) {
    case device::ResidentKeyRequirement::kDiscouraged:
      return std::u16string();
    case device::ResidentKeyRequirement::kPreferred:
      return l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_RESIDENT_KEY_PREFERRED_PRIVACY);
    case device::ResidentKeyRequirement::kRequired:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_RESIDENT_KEY_PRIVACY);
  }
  NOTREACHED();
}

// Return a warning about attestation if attestation was requested, otherwise
// return an empty string.
std::u16string PossibleAttestationWarning(
    AuthenticatorRequestDialogModel* dialog_model) {
  if (!dialog_model->attestation_conveyance_preference) {
    return std::u16string();
  }

  switch (*dialog_model->attestation_conveyance_preference) {
    case device::AttestationConveyancePreference::kNone:
      return std::u16string();
    case device::AttestationConveyancePreference::kIndirect:
    case device::AttestationConveyancePreference::kDirect:
      return l10n_util::GetStringFUTF16(
          IDS_WEBAUTHN_ATTESTATION_WARNING,
          AuthenticatorSheetModelBase::GetRelyingPartyIdString(dialog_model));
    case device::AttestationConveyancePreference::
        kEnterpriseIfRPListedOnAuthenticator:
    case device::AttestationConveyancePreference::kEnterpriseApprovedByBrowser:
      return l10n_util::GetStringFUTF16(
          IDS_WEBAUTHN_ENTERPRISE_ATTESTATION_WARNING,
          AuthenticatorSheetModelBase::GetRelyingPartyIdString(dialog_model));
  }
}

}  // namespace

// AuthenticatorSheetModelBase ------------------------------------------------

AuthenticatorSheetModelBase::AuthenticatorSheetModelBase(
    AuthenticatorRequestDialogModel* dialog_model)
    : dialog_model_(dialog_model) {
  DCHECK(dialog_model);
  dialog_model_->observers.AddObserver(this);
}

AuthenticatorSheetModelBase::AuthenticatorSheetModelBase(
    AuthenticatorRequestDialogModel* dialog_model,
    OtherMechanismButtonVisibility other_mechanism_button_visibility)
    : AuthenticatorSheetModelBase(dialog_model) {
  other_mechanism_button_visibility_ = other_mechanism_button_visibility;
}

AuthenticatorSheetModelBase::~AuthenticatorSheetModelBase() {
  if (dialog_model_) {
    dialog_model_->observers.RemoveObserver(this);
    dialog_model_ = nullptr;
  }
}

// static
std::u16string AuthenticatorSheetModelBase::GetRelyingPartyIdString(
    const AuthenticatorRequestDialogModel* dialog_model) {
  // The preferred width of medium snap point modal dialog view is 448 dp, but
  // we leave some room for padding between the text and the modal views.
  static constexpr int kDialogWidth = 300;
  return webauthn_ui_helpers::RpIdToElidedHost(dialog_model->relying_party_id,
                                               kDialogWidth);
}

bool AuthenticatorSheetModelBase::IsActivityIndicatorVisible() const {
  return dialog_model_->ui_disabled_;
}

bool AuthenticatorSheetModelBase::IsCancelButtonVisible() const {
  return true;
}

bool AuthenticatorSheetModelBase::IsOtherMechanismButtonVisible() const {
  return other_mechanism_button_visibility_ ==
             OtherMechanismButtonVisibility::kVisible &&
         dialog_model_ && dialog_model_->mechanisms.size() > 1;
}

std::u16string AuthenticatorSheetModelBase::GetOtherMechanismButtonLabel()
    const {
  switch (dialog_model()->request_type) {
    case device::FidoRequestType::kMakeCredential:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_SAVE_ANOTHER_WAY);
    case device::FidoRequestType::kGetAssertion:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_USE_A_DIFFERENT_PASSKEY);
  }
}

std::u16string AuthenticatorSheetModelBase::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

AuthenticatorSheetModelBase::AcceptButtonState
AuthenticatorSheetModelBase::GetAcceptButtonState() const {
  return AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorSheetModelBase::GetAcceptButtonLabel() const {
  return std::u16string();
}

void AuthenticatorSheetModelBase::OnBack() {
  if (dialog_model()) {
    dialog_model()->StartOver();
  }
}

void AuthenticatorSheetModelBase::OnAccept() {
  NOTREACHED();
}

void AuthenticatorSheetModelBase::OnCancel() {
  if (dialog_model()) {
    webauthn::user_actions::RecordCancelClick();
    dialog_model()->CancelAuthenticatorRequest();
  }
}

void AuthenticatorSheetModelBase::OnModelDestroyed(
    AuthenticatorRequestDialogModel* model) {
  DCHECK(model == dialog_model_);
  dialog_model_ = nullptr;
}

// AuthenticatorMechanismSelectorSheetModel -----------------------------------

AuthenticatorMechanismSelectorSheetModel::
    AuthenticatorMechanismSelectorSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_PASSKEY_LIGHT,
                                IDR_WEBAUTHN_PASSKEY_DARK);
  webauthn::user_actions::RecordMultipleOptionsShown(
      dialog_model->mechanisms, dialog_model->request_type);
}

std::u16string AuthenticatorMechanismSelectorSheetModel::GetStepTitle() const {
  CHECK_EQ(dialog_model()->request_type,
           device::FidoRequestType::kMakeCredential);
  return l10n_util::GetStringFUTF16(
      IDS_WEBAUTHN_CREATE_PASSKEY_CHOOSE_DEVICE_TITLE,
      GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorMechanismSelectorSheetModel::GetStepDescription()
    const {
  return u"";
}

// AuthenticatorInsertAndActivateUsbSheetModel ----------------------

AuthenticatorInsertAndActivateUsbSheetModel::
    AuthenticatorInsertAndActivateUsbSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  webauthn::user_actions::RecordSecurityKeyDialogShown(
      dialog_model->request_type);
  vector_illustrations_.emplace(kPasskeyUsbIcon, kPasskeyUsbDarkIcon);
}

bool AuthenticatorInsertAndActivateUsbSheetModel::IsActivityIndicatorVisible()
    const {
  return true;
}

std::u16string AuthenticatorInsertAndActivateUsbSheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_GENERIC_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorInsertAndActivateUsbSheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_USB_ACTIVATE_DESCRIPTION);
}

std::vector<std::u16string>
AuthenticatorInsertAndActivateUsbSheetModel::GetAdditionalDescriptions() const {
  return {PossibleAttestationWarning(dialog_model()),
          PossibleResidentKeyWarning(dialog_model())};
}

// AuthenticatorTimeoutErrorModel ---------------------------------------------

AuthenticatorTimeoutErrorModel::AuthenticatorTimeoutErrorModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorTimeoutErrorModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}

std::u16string AuthenticatorTimeoutErrorModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_GENERIC_TITLE);
}

std::u16string AuthenticatorTimeoutErrorModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_TIMEOUT_DESCRIPTION);
}

// AuthenticatorNoAvailableTransportsErrorModel -------------------------------

AuthenticatorNoAvailableTransportsErrorModel::
    AuthenticatorNoAvailableTransportsErrorModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string
AuthenticatorNoAvailableTransportsErrorModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}

std::u16string AuthenticatorNoAvailableTransportsErrorModel::GetStepTitle()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_MISSING_CAPABILITY_TITLE);
}

std::u16string
AuthenticatorNoAvailableTransportsErrorModel::GetStepDescription() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_ERROR_MISSING_CAPABILITY_DESC,
                                    GetRelyingPartyIdString(dialog_model()));
}

// AuthenticatorNoPasskeysErrorModel ------------------------------------------

AuthenticatorNoPasskeysErrorModel::AuthenticatorNoPasskeysErrorModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorNoPasskeysErrorModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}
std::u16string AuthenticatorNoPasskeysErrorModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_NO_PASSKEYS_TITLE);
}

std::u16string AuthenticatorNoPasskeysErrorModel::GetStepDescription() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_ERROR_NO_PASSKEYS_DESCRIPTION,
                                    GetRelyingPartyIdString(dialog_model()));
}

// AuthenticatorNotRegisteredErrorModel ---------------------------------------

AuthenticatorNotRegisteredErrorModel::AuthenticatorNotRegisteredErrorModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorNotRegisteredErrorModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorNotRegisteredErrorModel::GetAcceptButtonState() const {
  return dialog_model()->offer_try_again_in_ui ? AcceptButtonState::kEnabled
                                               : AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorNotRegisteredErrorModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_RETRY);
}

std::u16string AuthenticatorNotRegisteredErrorModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_WRONG_KEY_TITLE);
}

std::u16string AuthenticatorNotRegisteredErrorModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_ERROR_WRONG_KEY_SIGN_DESCRIPTION);
}

void AuthenticatorNotRegisteredErrorModel::OnAccept() {
  dialog_model()->StartOver();
}

// AuthenticatorAlreadyRegisteredErrorModel -----------------------------------

AuthenticatorAlreadyRegisteredErrorModel::
    AuthenticatorAlreadyRegisteredErrorModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorAlreadyRegisteredErrorModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorAlreadyRegisteredErrorModel::GetAcceptButtonState() const {
  return dialog_model()->offer_try_again_in_ui ? AcceptButtonState::kEnabled
                                               : AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorAlreadyRegisteredErrorModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_RETRY);
}

std::u16string AuthenticatorAlreadyRegisteredErrorModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_WRONG_DEVICE_TITLE);
}

std::u16string AuthenticatorAlreadyRegisteredErrorModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_ERROR_WRONG_DEVICE_REGISTER_DESCRIPTION);
}

void AuthenticatorAlreadyRegisteredErrorModel::OnAccept() {
  dialog_model()->StartOver();
}

// AuthenticatorInternalUnrecognizedErrorSheetModel ---------------------------

AuthenticatorInternalUnrecognizedErrorSheetModel::
    AuthenticatorInternalUnrecognizedErrorSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorInternalUnrecognizedErrorSheetModel::GetAcceptButtonState() const {
  return dialog_model()->offer_try_again_in_ui ? AcceptButtonState::kEnabled
                                               : AcceptButtonState::kNotVisible;
}

std::u16string
AuthenticatorInternalUnrecognizedErrorSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_RETRY);
}

std::u16string AuthenticatorInternalUnrecognizedErrorSheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_ERROR_INTERNAL_UNRECOGNIZED_TITLE);
}

std::u16string
AuthenticatorInternalUnrecognizedErrorSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_ERROR_INTERNAL_UNRECOGNIZED_DESCRIPTION);
}

void AuthenticatorInternalUnrecognizedErrorSheetModel::OnAccept() {
  dialog_model()->StartOver();
}

// AuthenticatorChallengeFetchErrorModel
// ---------------------------------------------

AuthenticatorChallengeFetchErrorModel::AuthenticatorChallengeFetchErrorModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorChallengeFetchErrorModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}

std::u16string AuthenticatorChallengeFetchErrorModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_GENERIC_TITLE);
}

std::u16string AuthenticatorChallengeFetchErrorModel::GetStepDescription()
    const {
  // TODO(https://crbug.com/381219428): Get an approved string for this dialog.
  return u"An error occurred trying to process this request. (UT)";
}

// AuthenticatorBlePowerOnManualSheetModel ------------------------------------

AuthenticatorBlePowerOnManualSheetModel::
    AuthenticatorBlePowerOnManualSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  vector_illustrations_.emplace(kPasskeyErrorBluetoothIcon,
                                kPasskeyErrorBluetoothDarkIcon);
}

std::u16string AuthenticatorBlePowerOnManualSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_BLUETOOTH_POWER_ON_MANUAL_TITLE);
}

std::u16string AuthenticatorBlePowerOnManualSheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_BLUETOOTH_POWER_ON_MANUAL_DESCRIPTION);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorBlePowerOnManualSheetModel::GetAcceptButtonState() const {
  return dialog_model()->ble_adapter_is_powered ? AcceptButtonState::kEnabled
                                                : AcceptButtonState::kDisabled;
}

std::u16string AuthenticatorBlePowerOnManualSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_BLUETOOTH_POWER_ON_MANUAL_NEXT);
}

void AuthenticatorBlePowerOnManualSheetModel::OnBluetoothPoweredStateChanged() {
  dialog_model()->OnSheetModelChanged();
}

void AuthenticatorBlePowerOnManualSheetModel::OnAccept() {
  dialog_model()->ContinueWithFlowAfterBleAdapterPowered();
}

// AuthenticatorBlePowerOnAutomaticSheetModel
// ------------------------------------

AuthenticatorBlePowerOnAutomaticSheetModel::
    AuthenticatorBlePowerOnAutomaticSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  vector_illustrations_.emplace(kPasskeyErrorBluetoothIcon,
                                kPasskeyErrorBluetoothDarkIcon);
}

bool AuthenticatorBlePowerOnAutomaticSheetModel::IsActivityIndicatorVisible()
    const {
  return busy_powering_on_ble_;
}

std::u16string AuthenticatorBlePowerOnAutomaticSheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_BLUETOOTH_POWER_ON_AUTO_TITLE);
}

std::u16string AuthenticatorBlePowerOnAutomaticSheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_BLUETOOTH_POWER_ON_AUTO_DESCRIPTION);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorBlePowerOnAutomaticSheetModel::GetAcceptButtonState() const {
  return busy_powering_on_ble_ ? AcceptButtonState::kDisabled
                               : AcceptButtonState::kEnabled;
}

std::u16string
AuthenticatorBlePowerOnAutomaticSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_BLUETOOTH_POWER_ON_AUTO_NEXT);
}

void AuthenticatorBlePowerOnAutomaticSheetModel::OnAccept() {
  busy_powering_on_ble_ = true;
  dialog_model()->OnSheetModelChanged();
  dialog_model()->PowerOnBleAdapter();
}

#if BUILDFLAG(IS_MAC)

// AuthenticatorBlePermissionMacSheetModel
// ------------------------------------

AuthenticatorBlePermissionMacSheetModel::
    AuthenticatorBlePermissionMacSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  vector_illustrations_.emplace(kPasskeyErrorBluetoothIcon,
                                kPasskeyErrorBluetoothDarkIcon);
}

std::u16string AuthenticatorBlePermissionMacSheetModel::GetStepTitle() const {
  // An empty title causes the title View to be omitted.
  return u"";
}

std::u16string AuthenticatorBlePermissionMacSheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_BLUETOOTH_PERMISSION);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorBlePermissionMacSheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

bool AuthenticatorBlePermissionMacSheetModel::IsCancelButtonVisible() const {
  return true;
}

std::u16string AuthenticatorBlePermissionMacSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_OPEN_SETTINGS_LINK);
}

void AuthenticatorBlePermissionMacSheetModel::OnAccept() {
  dialog_model()->OpenBlePreferences();
}

// AuthenticatorTouchIdSheetModel
// ------------------------------------

AuthenticatorTouchIdSheetModel::AuthenticatorTouchIdSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  webauthn::user_actions::RecordGpmTouchIdDialogShown(
      dialog_model->request_type);
}

std::u16string AuthenticatorTouchIdSheetModel::GetStepTitle() const {
  const std::u16string rp_id = GetRelyingPartyIdString(dialog_model());
  std::optional<int> id = std::nullopt;
  switch (dialog_model()->request_type) {
    case device::FidoRequestType::kMakeCredential:
      id = IDS_WEBAUTHN_GPM_CREATE_PASSKEY_TITLE;
      break;
    case device::FidoRequestType::kGetAssertion:
      id = dialog_model()->ui_presentation == UIPresentation::kModalImmediate
               ? IDS_WEBAUTHN_SIGN_IN_TO_WEBSITE_DIALOG_TITLE
               : IDS_WEBAUTHN_CHOOSE_PASSKEY_FOR_RP_TITLE;
      break;
  }
  CHECK(id.has_value());
  return l10n_util::GetStringFUTF16(id.value(), rp_id);
}

std::u16string AuthenticatorTouchIdSheetModel::GetStepDescription() const {
  switch (dialog_model()->request_type) {
    case device::FidoRequestType::kMakeCredential:
      return l10n_util::GetStringFUTF16(
          IDS_WEBAUTHN_GPM_CREATE_PASSKEY_DESC,
          base::UTF8ToUTF16(dialog_model()->GetGpmAccountEmail()));

    case device::FidoRequestType::kGetAssertion:
      return dialog_model()->ui_presentation == UIPresentation::kModalImmediate
                 ? std::u16string()
                 : l10n_util::GetStringFUTF16(
                       IDS_WEBAUTHN_TOUCH_ID_ASSERTION_DESC,
                       GetRelyingPartyIdString(dialog_model()));
  }
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorTouchIdSheetModel::GetAcceptButtonState() const {
  // Visible only if biometrics aren't available (fallback to password)
  return !device::fido::mac::DeviceHasBiometricsAvailable()
             ? AcceptButtonState::kEnabled
             : AcceptButtonState::kNotVisible;
}

bool AuthenticatorTouchIdSheetModel::IsCancelButtonVisible() const {
  return true;
}

std::u16string AuthenticatorTouchIdSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_TOUCH_ID_ENTER_PASSWORD);
}

std::u16string AuthenticatorTouchIdSheetModel::GetCancelButtonLabel() const {
  return dialog_model()->ui_presentation == UIPresentation::kModalImmediate
             ? l10n_util::GetStringUTF16(IDS_SIGNIN_ACCESSIBLE_CLOSE_BUTTON)
             : l10n_util::GetStringUTF16(IDS_CANCEL);
}

void AuthenticatorTouchIdSheetModel::OnAccept() {
  if (touch_id_completed_) {
    return;
  }
  webauthn::user_actions::RecordAcceptClick();
  touch_id_completed_ = true;
  dialog_model()->OnTouchIDComplete(false);
}

void AuthenticatorTouchIdSheetModel::OnTouchIDSensorTapped(
    std::optional<webauthn::LocalAuthenticationToken> local_auth_token) {
  // Ignore Touch ID ceremony status after the user has completed the ceremony.
  if (touch_id_completed_) {
    return;
  }
  if (!local_auth_token) {
    // Authentication failed. Update the button status and rebuild the sheet,
    // which will restart the Touch ID request if the sensor is not softlocked
    // or display a padlock icon if it is.
    dialog_model()->OnSheetModelChanged();
    return;
  }
  touch_id_completed_ = true;
  dialog_model()->local_auth_token = std::move(local_auth_token);
  dialog_model()->OnTouchIDComplete(true);
}

#endif  // IS_MAC

// AuthenticatorOffTheRecordInterstitialSheetModel
// -----------------------------------------

AuthenticatorOffTheRecordInterstitialSheetModel::
    AuthenticatorOffTheRecordInterstitialSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  // TODO(crbug.com/40237082): Add more specific illustration once available.
  // The "error" graphic is a large question mark, so it looks visually very
  // similar.
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorOffTheRecordInterstitialSheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringFUTF16(
      IDS_WEBAUTHN_PLATFORM_AUTHENTICATOR_OFF_THE_RECORD_INTERSTITIAL_TITLE,
      GetRelyingPartyIdString(dialog_model()));
}

std::u16string
AuthenticatorOffTheRecordInterstitialSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_PLATFORM_AUTHENTICATOR_OFF_THE_RECORD_INTERSTITIAL_DESCRIPTION);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorOffTheRecordInterstitialSheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string
AuthenticatorOffTheRecordInterstitialSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CONTINUE);
}

void AuthenticatorOffTheRecordInterstitialSheetModel::OnAccept() {
  dialog_model()->OnOffTheRecordInterstitialAccepted();
}

std::u16string
AuthenticatorOffTheRecordInterstitialSheetModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_PLATFORM_AUTHENTICATOR_OFF_THE_RECORD_INTERSTITIAL_DENY);
}

// AuthenticatorPaaskSheetModel -----------------------------------------

AuthenticatorPaaskSheetModel::AuthenticatorPaaskSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  vector_illustrations_.emplace(kPasskeyPhoneIcon, kPasskeyPhoneDarkIcon);
}

AuthenticatorPaaskSheetModel::~AuthenticatorPaaskSheetModel() = default;

bool AuthenticatorPaaskSheetModel::IsActivityIndicatorVisible() const {
  return true;
}

std::u16string AuthenticatorPaaskSheetModel::GetStepTitle() const {
  switch (*dialog_model()->cable_ui_type) {
    case AuthenticatorRequestDialogModel::CableUIType::CABLE_V1:
    case AuthenticatorRequestDialogModel::CableUIType::CABLE_V2_SERVER_LINK:
      // caBLEv1 and v2 server-link don't include device names.
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CABLE_ACTIVATE_TITLE);
    case AuthenticatorRequestDialogModel::CableUIType::CABLE_V2_2ND_FACTOR:
      return l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_CABLE_ACTIVATE_TITLE_DEVICE);
  }
}

std::u16string AuthenticatorPaaskSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CABLE_ACTIVATE_DESCRIPTION);
}

// AuthenticatorClientPinEntrySheetModel
// -----------------------------------------

AuthenticatorClientPinEntrySheetModel::AuthenticatorClientPinEntrySheetModel(
    AuthenticatorRequestDialogModel* dialog_model,
    Mode mode,
    device::pin::PINEntryError error)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible),
      mode_(mode) {
  vector_illustrations_.emplace(kPasskeyUsbIcon, kPasskeyUsbDarkIcon);
  switch (error) {
    case device::pin::PINEntryError::kNoError:
      break;
    case device::pin::PINEntryError::kInternalUvLocked:
      error_ = l10n_util::GetStringUTF16(IDS_WEBAUTHN_UV_ERROR_LOCKED);
      break;
    case device::pin::PINEntryError::kInvalidCharacters:
      error_ = l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_PIN_ENTRY_ERROR_INVALID_CHARACTERS);
      break;
    case device::pin::PINEntryError::kSameAsCurrentPIN:
      error_ = l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_PIN_ENTRY_ERROR_SAME_AS_CURRENT);
      break;
    case device::pin::PINEntryError::kTooShort:
      error_ = l10n_util::GetPluralStringFUTF16(
          IDS_WEBAUTHN_PIN_ENTRY_ERROR_TOO_SHORT, dialog_model->min_pin_length);
      break;
    case device::pin::PINEntryError::kWrongPIN:
      std::optional<int> attempts = dialog_model->pin_attempts;
      error_ =
          attempts && *attempts <= 3
              ? l10n_util::GetPluralStringFUTF16(
                    IDS_WEBAUTHN_PIN_ENTRY_ERROR_FAILED_RETRIES, *attempts)
              : l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_ERROR_FAILED);
      break;
  }
}

AuthenticatorClientPinEntrySheetModel::
    ~AuthenticatorClientPinEntrySheetModel() = default;

void AuthenticatorClientPinEntrySheetModel::SetPinCode(
    std::u16string pin_code) {
  pin_code_ = std::move(pin_code);
}

void AuthenticatorClientPinEntrySheetModel::SetPinConfirmation(
    std::u16string pin_confirmation) {
  DCHECK(mode_ == Mode::kPinSetup || mode_ == Mode::kPinChange);
  pin_confirmation_ = std::move(pin_confirmation);
}

std::u16string AuthenticatorClientPinEntrySheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_TITLE);
}

std::u16string AuthenticatorClientPinEntrySheetModel::GetStepDescription()
    const {
  switch (mode_) {
    case Mode::kPinChange:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_FORCE_PIN_CHANGE);
    case Mode::kPinEntry:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_DESCRIPTION);
    case Mode::kPinSetup:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_SETUP_DESCRIPTION);
  }
}

std::u16string AuthenticatorClientPinEntrySheetModel::GetError() const {
  return error_;
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorClientPinEntrySheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string AuthenticatorClientPinEntrySheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_NEXT);
}

void AuthenticatorClientPinEntrySheetModel::OnAccept() {
  if ((mode_ == Mode::kPinChange || mode_ == Mode::kPinSetup) &&
      pin_code_ != pin_confirmation_) {
    error_ = l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_ERROR_MISMATCH);
    dialog_model()->OnSheetModelChanged();
    return;
  }

  if (dialog_model()) {
    dialog_model()->OnHavePIN(pin_code_);
  }
}

bool AuthenticatorClientPinEntrySheetModel::IsOtherMechanismButtonVisible()
    const {
  // Always allow restarting the request to select a different security key or
  // hybrid authenticator.
  return true;
}

// AuthenticatorClientPinTapAgainSheetModel ----------------------

AuthenticatorClientPinTapAgainSheetModel::
    AuthenticatorClientPinTapAgainSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyUsbIcon, kPasskeyUsbDarkIcon);
}

AuthenticatorClientPinTapAgainSheetModel::
    ~AuthenticatorClientPinTapAgainSheetModel() = default;

bool AuthenticatorClientPinTapAgainSheetModel::IsActivityIndicatorVisible()
    const {
  return true;
}

std::u16string AuthenticatorClientPinTapAgainSheetModel::GetStepTitle() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_GENERIC_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorClientPinTapAgainSheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_TAP_AGAIN_DESCRIPTION);
}

std::vector<std::u16string>
AuthenticatorClientPinTapAgainSheetModel::GetAdditionalDescriptions() const {
  return {PossibleAttestationWarning(dialog_model()),
          PossibleResidentKeyWarning(dialog_model())};
}

// AuthenticatorBioEnrollmentSheetModel ----------------------------------

// No illustration since the content already has a large animated
// fingerprint icon.
AuthenticatorBioEnrollmentSheetModel::AuthenticatorBioEnrollmentSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {}

AuthenticatorBioEnrollmentSheetModel::~AuthenticatorBioEnrollmentSheetModel() =
    default;

bool AuthenticatorBioEnrollmentSheetModel::IsActivityIndicatorVisible() const {
  return !HasBioSamplesRemaining();
}

std::u16string AuthenticatorBioEnrollmentSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(
      IDS_SETTINGS_SECURITY_KEYS_BIO_ENROLLMENT_ADD_TITLE);
}

std::u16string AuthenticatorBioEnrollmentSheetModel::GetStepDescription()
    const {
  return HasBioSamplesRemaining()
             ? l10n_util::GetStringUTF16(
                   IDS_SETTINGS_SECURITY_KEYS_BIO_ENROLLMENT_ENROLLING_COMPLETE_LABEL)
             : l10n_util::GetStringUTF16(
                   IDS_SETTINGS_SECURITY_KEYS_BIO_ENROLLMENT_ENROLLING_LABEL);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorBioEnrollmentSheetModel::GetAcceptButtonState() const {
  return HasBioSamplesRemaining() ? AcceptButtonState::kEnabled
                                  : AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorBioEnrollmentSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_NEXT);
}

bool AuthenticatorBioEnrollmentSheetModel::IsCancelButtonVisible() const {
  return !HasBioSamplesRemaining();
}

std::u16string AuthenticatorBioEnrollmentSheetModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_INLINE_ENROLLMENT_CANCEL_LABEL);
}

void AuthenticatorBioEnrollmentSheetModel::OnAccept() {
  dialog_model()->OnBioEnrollmentDone();
}

void AuthenticatorBioEnrollmentSheetModel::OnCancel() {
  OnAccept();
}

bool AuthenticatorBioEnrollmentSheetModel::HasBioSamplesRemaining() const {
  return dialog_model()->bio_samples_remaining &&
         dialog_model()->bio_samples_remaining <= 0;
}

// AuthenticatorRetryUvSheetModel -------------------------------------

AuthenticatorRetryUvSheetModel::AuthenticatorRetryUvSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  vector_illustrations_.emplace(kPasskeyFingerprintIcon,
                                kPasskeyFingerprintDarkIcon);
}

AuthenticatorRetryUvSheetModel::~AuthenticatorRetryUvSheetModel() = default;

bool AuthenticatorRetryUvSheetModel::IsActivityIndicatorVisible() const {
  return true;
}

std::u16string AuthenticatorRetryUvSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_UV_RETRY_TITLE);
}

std::u16string AuthenticatorRetryUvSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_UV_RETRY_DESCRIPTION);
}

std::u16string AuthenticatorRetryUvSheetModel::GetError() const {
  int attempts = *dialog_model()->uv_attempts;
  if (attempts > 3) {
    return std::u16string();
  }
  return l10n_util::GetPluralStringFUTF16(
      IDS_WEBAUTHN_UV_RETRY_ERROR_FAILED_RETRIES, attempts);
}

// AuthenticatorGenericErrorSheetModel -----------------------------------

// static
std::unique_ptr<AuthenticatorGenericErrorSheetModel>
AuthenticatorGenericErrorSheetModel::ForClientPinErrorSoftBlock(
    AuthenticatorRequestDialogModel* dialog_model) {
  return base::WrapUnique(new AuthenticatorGenericErrorSheetModel(
      dialog_model, l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_GENERIC_TITLE),
      l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_CLIENT_PIN_SOFT_BLOCK_DESCRIPTION)));
}

// static
std::unique_ptr<AuthenticatorGenericErrorSheetModel>
AuthenticatorGenericErrorSheetModel::ForClientPinErrorHardBlock(
    AuthenticatorRequestDialogModel* dialog_model) {
  return base::WrapUnique(new AuthenticatorGenericErrorSheetModel(
      dialog_model, l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_GENERIC_TITLE),
      l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_CLIENT_PIN_HARD_BLOCK_DESCRIPTION)));
}

// static
std::unique_ptr<AuthenticatorGenericErrorSheetModel>
AuthenticatorGenericErrorSheetModel::ForClientPinErrorAuthenticatorRemoved(
    AuthenticatorRequestDialogModel* dialog_model) {
  return base::WrapUnique(new AuthenticatorGenericErrorSheetModel(
      dialog_model, l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_GENERIC_TITLE),
      l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_CLIENT_PIN_AUTHENTICATOR_REMOVED_DESCRIPTION)));
}

// static
std::unique_ptr<AuthenticatorGenericErrorSheetModel>
AuthenticatorGenericErrorSheetModel::ForMissingCapability(
    AuthenticatorRequestDialogModel* dialog_model) {
  return base::WrapUnique(new AuthenticatorGenericErrorSheetModel(
      dialog_model,
      l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_MISSING_CAPABILITY_TITLE),
      l10n_util::GetStringFUTF16(IDS_WEBAUTHN_ERROR_MISSING_CAPABILITY_DESC,
                                 GetRelyingPartyIdString(dialog_model))));
}

// static
std::unique_ptr<AuthenticatorGenericErrorSheetModel>
AuthenticatorGenericErrorSheetModel::ForStorageFull(
    AuthenticatorRequestDialogModel* dialog_model) {
  return base::WrapUnique(new AuthenticatorGenericErrorSheetModel(
      dialog_model,
      l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_MISSING_CAPABILITY_TITLE),
      l10n_util::GetStringUTF16(IDS_WEBAUTHN_STORAGE_FULL_DESC)));
}

std::unique_ptr<AuthenticatorGenericErrorSheetModel>
AuthenticatorGenericErrorSheetModel::ForWindowsHelloNotEnabled(
    AuthenticatorRequestDialogModel* dialog_model) {
  return base::WrapUnique(new AuthenticatorGenericErrorSheetModel(
      dialog_model,
      l10n_util::GetStringUTF16(IDS_WEBAUTHN_WINDOWS_HELLO_NOT_ENABLED_TITLE),
      l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_WINDOWS_HELLO_NOT_ENABLED_DESCRIPTION)));
}

AuthenticatorGenericErrorSheetModel::AuthenticatorGenericErrorSheetModel(
    AuthenticatorRequestDialogModel* dialog_model,
    std::u16string title,
    std::u16string description)
    : AuthenticatorSheetModelBase(dialog_model),
      title_(std::move(title)),
      description_(std::move(description)) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

std::u16string AuthenticatorGenericErrorSheetModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CLOSE);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorGenericErrorSheetModel::GetAcceptButtonState() const {
  return dialog_model()->offer_try_again_in_ui ? AcceptButtonState::kEnabled
                                               : AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorGenericErrorSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_RETRY);
}

std::u16string AuthenticatorGenericErrorSheetModel::GetStepTitle() const {
  return title_;
}

std::u16string AuthenticatorGenericErrorSheetModel::GetStepDescription() const {
  return description_;
}

void AuthenticatorGenericErrorSheetModel::OnAccept() {
  dialog_model()->StartOver();
}

// AuthenticatorResidentCredentialConfirmationSheetView -----------------------

// TODO(crbug.com/40237082): Add more specific illustration once available. The
// "error" graphic is a large question mark, so it looks visually very similar.
AuthenticatorResidentCredentialConfirmationSheetView::
    AuthenticatorResidentCredentialConfirmationSheetView(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

AuthenticatorResidentCredentialConfirmationSheetView::
    ~AuthenticatorResidentCredentialConfirmationSheetView() = default;

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorResidentCredentialConfirmationSheetView::GetAcceptButtonState()
    const {
  return AcceptButtonState::kEnabled;
}

std::u16string
AuthenticatorResidentCredentialConfirmationSheetView::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CONTINUE);
}

std::u16string
AuthenticatorResidentCredentialConfirmationSheetView::GetStepTitle() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_GENERIC_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string
AuthenticatorResidentCredentialConfirmationSheetView::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_RESIDENT_KEY_PRIVACY);
}

void AuthenticatorResidentCredentialConfirmationSheetView::OnAccept() {
  dialog_model()->OnResidentCredentialConfirmed();
}

// AuthenticatorSelectAccountSheetModel ---------------------------------------

AuthenticatorSelectAccountSheetModel::AuthenticatorSelectAccountSheetModel(
    AuthenticatorRequestDialogModel* dialog_model,
    UserVerificationMode mode)
    : AuthenticatorSheetModelBase(
          dialog_model,
          mode == kPreUserVerification
              ? OtherMechanismButtonVisibility::kVisible
              : OtherMechanismButtonVisibility::kHidden),
      user_verification_mode_(mode) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_PASSKEY_LIGHT,
                                IDR_WEBAUTHN_PASSKEY_DARK);
}

AuthenticatorSelectAccountSheetModel::~AuthenticatorSelectAccountSheetModel() =
    default;

void AuthenticatorSelectAccountSheetModel::SetCurrentSelection(int selected) {
  DCHECK_LE(0, selected);
  DCHECK_LT(static_cast<size_t>(selected), dialog_model()->creds.size());
  selected_ = selected;
}

void AuthenticatorSelectAccountSheetModel::OnAccept() {
  switch (user_verification_mode_) {
    case kPreUserVerification:
      dialog_model()->OnAccountPreselectedIndex(selected_);
      break;
    case kPostUserVerification:
      dialog_model()->OnAccountSelected(selected_);
      break;
  }
}

std::u16string AuthenticatorSelectAccountSheetModel::GetStepTitle() const {
  if (dialog_model()->creds.size() > 1) {
    return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CHOOSE_PASSKEY_TITLE);
  }
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_USE_PASSKEY_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorSelectAccountSheetModel::GetStepDescription()
    const {
  if (dialog_model()->creds.size() > 1) {
    return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_CHOOSE_PASSKEY_BODY,
                                      GetRelyingPartyIdString(dialog_model()));
  }
  return u"";
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorSelectAccountSheetModel::GetAcceptButtonState() const {
  return dialog_model()->creds.size() == 1 ? AcceptButtonState::kEnabled
                                           : AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorSelectAccountSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CONTINUE);
}

// AuthenticatorHybridAndSecurityKeySheetModel --------------------------------

AuthenticatorHybridAndSecurityKeySheetModel::
    AuthenticatorHybridAndSecurityKeySheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  webauthn::user_actions::RecordHybridAndSecurityKeyDialogShown(
      dialog_model->request_type);
}

AuthenticatorHybridAndSecurityKeySheetModel::
    ~AuthenticatorHybridAndSecurityKeySheetModel() = default;

std::u16string AuthenticatorHybridAndSecurityKeySheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringUTF16(dialog_model()->show_security_key_on_qr_sheet
                                       ? IDS_WEBAUTHN_PASSKEYS_AND_SECURITY_KEYS
                                       : IDS_WEBAUTHN_PASSKEYS);
}

std::u16string AuthenticatorHybridAndSecurityKeySheetModel::GetStepDescription()
    const {
  return u"";
}

std::u16string
AuthenticatorHybridAndSecurityKeySheetModel::GetOtherMechanismButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_ACCOUNT_SELECTION_BACK);
}

std::optional<std::u16string>
AuthenticatorHybridAndSecurityKeySheetModel::GetAttestationWarning() const {
  if (dialog_model()->show_security_key_on_qr_sheet &&
      (dialog_model()->request_type ==
       device::FidoRequestType::kMakeCredential)) {
    std::u16string warning = PossibleAttestationWarning(dialog_model());
    if (!warning.empty()) {
      return warning;
    }
  }
  return std::nullopt;
}

// AuthenticatorConnectingSheetModel ------------------------------------------

AuthenticatorConnectingSheetModel::AuthenticatorConnectingSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_HYBRID_CONNECTING_LIGHT,
                                IDR_WEBAUTHN_HYBRID_CONNECTING_DARK);
}

AuthenticatorConnectingSheetModel::~AuthenticatorConnectingSheetModel() =
    default;

std::u16string AuthenticatorConnectingSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CABLEV2_CONNECTING_TITLE);
}

std::u16string AuthenticatorConnectingSheetModel::GetStepDescription() const {
  return u"";
}

// AuthenticatorConnectedSheetModel ------------------------------------------

AuthenticatorConnectedSheetModel::AuthenticatorConnectedSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  vector_illustrations_.emplace(kPasskeyPhoneIcon, kPasskeyPhoneDarkIcon);
}

AuthenticatorConnectedSheetModel::~AuthenticatorConnectedSheetModel() = default;

bool AuthenticatorConnectedSheetModel::IsActivityIndicatorVisible() const {
  return false;
}

std::u16string AuthenticatorConnectedSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CABLEV2_CONNECTED_DESCRIPTION);
}

std::u16string AuthenticatorConnectedSheetModel::GetStepDescription() const {
  return u"";
}

// AuthenticatorCableErrorSheetModel ------------------------------------------

AuthenticatorCableErrorSheetModel::AuthenticatorCableErrorSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
}

AuthenticatorCableErrorSheetModel::~AuthenticatorCableErrorSheetModel() =
    default;

std::u16string AuthenticatorCableErrorSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_ERROR_GENERIC_TITLE);
}

std::u16string AuthenticatorCableErrorSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CABLEV2_ERROR_DESCRIPTION);
}

std::u16string AuthenticatorCableErrorSheetModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CABLEV2_ERROR_CLOSE);
}

// AuthenticatorCreatePasskeySheetModel
// --------------------------------------------------

AuthenticatorCreatePasskeySheetModel::AuthenticatorCreatePasskeySheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_PASSKEY_LIGHT,
                                IDR_WEBAUTHN_PASSKEY_DARK);
}

AuthenticatorCreatePasskeySheetModel::~AuthenticatorCreatePasskeySheetModel() =
    default;

std::u16string AuthenticatorCreatePasskeySheetModel::GetStepTitle() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_CREATE_PASSKEY_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorCreatePasskeySheetModel::GetStepDescription()
    const {
  return u"";
}

std::u16string
AuthenticatorCreatePasskeySheetModel::passkey_storage_description() const {
  return l10n_util::GetStringUTF16(
      dialog_model()->is_off_the_record
          ? IDS_WEBAUTHN_CREATE_PASSKEY_EXTRA_INCOGNITO
          : IDS_WEBAUTHN_CREATE_PASSKEY_EXTRA);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorCreatePasskeySheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string AuthenticatorCreatePasskeySheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CONTINUE);
}

void AuthenticatorCreatePasskeySheetModel::OnAccept() {
  dialog_model()->OnCreatePasskeyAccepted();
}

// AuthenticatorGPMErrorSheetModel -------------------------------------------

AuthenticatorGPMErrorSheetModel::AuthenticatorGPMErrorSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(
          dialog_model,
          base::FeatureList::IsEnabled(device::kWebAuthnNoAccountTimeout)
              ? OtherMechanismButtonVisibility::kVisible
              : OtherMechanismButtonVisibility::kHidden) {
  vector_illustrations_.emplace(kPasskeyErrorIcon, kPasskeyErrorDarkIcon);
  if (dialog_model->in_onboarding_flow) {
    RecordOnboardingEvent(webauthn::metrics::OnboardingEvents::kFailure);
  }
  webauthn::user_actions::RecordGpmFailureShown();
  switch (dialog_model->request_type) {
    case device::FidoRequestType::kGetAssertion:
      RecordGPMGetAssertionEvent(
          webauthn::metrics::GPMGetAssertionEvents::kFailure);
      break;
    case device::FidoRequestType::kMakeCredential:
      RecordGPMMakeCredentialEvent(
          webauthn::metrics::GPMMakeCredentialEvents::kFailure);
      break;
  }
}

AuthenticatorGPMErrorSheetModel::~AuthenticatorGPMErrorSheetModel() = default;

std::u16string AuthenticatorGPMErrorSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_ERROR_TITLE);
}

std::u16string AuthenticatorGPMErrorSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_ERROR_DESC);
}

// AuthenticatorGPMConnectingSheetModel --------------------------------------

AuthenticatorGPMConnectingSheetModel::AuthenticatorGPMConnectingSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_HYBRID_CONNECTING_LIGHT,
                                IDR_WEBAUTHN_HYBRID_CONNECTING_DARK);
}

AuthenticatorGPMConnectingSheetModel::~AuthenticatorGPMConnectingSheetModel() =
    default;

std::u16string AuthenticatorGPMConnectingSheetModel::GetStepTitle() const {
  return u"";
}

std::u16string AuthenticatorGPMConnectingSheetModel::GetStepDescription()
    const {
  return u"";
}

// AuthenticatorMultiSourcePickerSheetModel --------------------------------

AuthenticatorMultiSourcePickerSheetModel::
    AuthenticatorMultiSourcePickerSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_PASSKEY_LIGHT,
                                IDR_WEBAUTHN_PASSKEY_DARK);

  webauthn::user_actions::RecordMultipleOptionsShown(
      dialog_model->mechanisms, dialog_model->request_type);
  if (std::ranges::any_of(dialog_model->mechanisms,
                          &IsLocalPasskeyOrEnclaveAuthenticatorOrPassword)) {
    primary_passkeys_label_ =
        l10n_util::GetStringUTF16(IDS_WEBAUTHN_THIS_DEVICE_LABEL);
    for (size_t i = 0; i < dialog_model->mechanisms.size(); ++i) {
      const AuthenticatorRequestDialogModel::Mechanism& mech =
          dialog_model->mechanisms[i];
      if (IsLocalPasskeyOrEnclaveAuthenticatorOrPassword(mech) ||
          // iCloud Keychain appears in the primary list if present. This
          // happens when Chrome does not have permission to enumerate
          // credentials from iCloud Keychain. Thus this generic option is the
          // only way for the user to trigger it.
          std::holds_alternative<ICloudKeychainMech>(mech.type)) {
        primary_passkey_indices_.push_back(i);
      } else {
        secondary_passkey_indices_.push_back(i);
      }
      if (std::holds_alternative<PasswordMech>(mech.type)) {
        has_passwords_ = true;
      }
    }
    return;
  }

  for (size_t i = 0; i < dialog_model->mechanisms.size(); ++i) {
    secondary_passkey_indices_.push_back(i);
  }
}

AuthenticatorMultiSourcePickerSheetModel::
    ~AuthenticatorMultiSourcePickerSheetModel() = default;

std::u16string AuthenticatorMultiSourcePickerSheetModel::GetStepTitle() const {
  if (has_passwords_) {
    return u"Use a saved credential for " +
           GetRelyingPartyIdString(dialog_model()) + u" (UT)";
  }
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_CHOOSE_PASSKEY_FOR_RP_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorMultiSourcePickerSheetModel::GetStepDescription()
    const {
  return u"";
}

// AuthenticatorPriorityMechanismSheetModel --------------------------------

AuthenticatorPriorityMechanismSheetModel::
    AuthenticatorPriorityMechanismSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_PASSKEY_LIGHT,
                                IDR_WEBAUTHN_PASSKEY_DARK);

  webauthn::user_actions::RecordPriorityOptionShown(
      dialog_model->mechanisms[*dialog_model->priority_mechanism_index]);
}
AuthenticatorPriorityMechanismSheetModel::
    ~AuthenticatorPriorityMechanismSheetModel() = default;

std::u16string AuthenticatorPriorityMechanismSheetModel::GetStepTitle() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_USE_PASSKEY_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorPriorityMechanismSheetModel::GetStepDescription()
    const {
  return u"";
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorPriorityMechanismSheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string AuthenticatorPriorityMechanismSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CONTINUE);
}

void AuthenticatorPriorityMechanismSheetModel::OnAccept() {
  dialog_model()->OnUserConfirmedPriorityMechanism();
}

// AuthenticatorGpmPinSheetModelBase -------------------------------------------

AuthenticatorGpmPinSheetModelBase::AuthenticatorGpmPinSheetModelBase(
    AuthenticatorRequestDialogModel* dialog_model,
    Mode mode)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden),
      mode_(mode) {}

AuthenticatorGpmPinSheetModelBase::~AuthenticatorGpmPinSheetModelBase() =
    default;

std::u16string AuthenticatorGpmPinSheetModelBase::GetGpmAccountEmail() const {
  std::optional<AccountInfo> account_info = dialog_model()->GetGpmAccountInfo();
  if (!account_info) {
    return std::u16string();
  }
  return base::UTF8ToUTF16(account_info->email);
}

std::u16string AuthenticatorGpmPinSheetModelBase::GetGpmAccountName() const {
  std::optional<AccountInfo> account_info = dialog_model()->GetGpmAccountInfo();
  if (!account_info) {
    return std::u16string();
  }
  return base::UTF8ToUTF16(account_info->full_name);
}

gfx::Image AuthenticatorGpmPinSheetModelBase::GetGpmAccountImage() const {
  std::optional<AccountInfo> account_info = dialog_model()->GetGpmAccountInfo();
  if (!account_info) {
    return gfx::Image();
  }
  gfx::Image account_image = account_info->account_image;
  if (account_image.IsEmpty()) {
    account_image = ui::ResourceBundle::GetSharedInstance().GetImageNamed(
        profiles::GetPlaceholderAvatarIconResourceID());
  }
  constexpr int kAvatarIconSize = 32;
  return profiles::GetSizedAvatarIcon(account_image,
                                      /*width=*/kAvatarIconSize,
                                      /*height=*/kAvatarIconSize,
                                      profiles::SHAPE_CIRCLE);
}

std::u16string AuthenticatorGpmPinSheetModelBase::GetAccessibleDescription()
    const {
  std::u16string error = GetError();
  return error.empty() ? GetHint() : error;
}

bool AuthenticatorGpmPinSheetModelBase::ui_disabled() const {
  return dialog_model()->ui_disabled_;
}

std::u16string AuthenticatorGpmPinSheetModelBase::GetStepTitle() const {
  switch (mode_) {
    case Mode::kPinCreate:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_CREATE_PIN_TITLE);
    case Mode::kPinEntry:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_ENTER_PIN_TITLE);
  }
}

std::u16string AuthenticatorGpmPinSheetModelBase::GetStepDescription() const {
  switch (mode_) {
    case Mode::kPinCreate:
      return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_CREATE_PIN_DESC);
    case Mode::kPinEntry:
      return l10n_util::GetStringFUTF16(
          IDS_WEBAUTHN_GPM_ENTER_PIN_DESC,
          GetRelyingPartyIdString(dialog_model()));
  }
}

std::u16string AuthenticatorGpmPinSheetModelBase::GetError() const {
  std::optional<int> remaining_attempts =
      dialog_model()->gpm_pin_remaining_attempts_;
  return remaining_attempts && mode_ == Mode::kPinEntry
             ? l10n_util::GetPluralStringFUTF16(
                   IDS_WEBAUTHN_GPM_WRONG_PIN_ERROR, *remaining_attempts)
             : std::u16string();
}

bool AuthenticatorGpmPinSheetModelBase::IsForgotGPMPinButtonVisible() const {
  return mode_ == Mode::kPinEntry;
}

bool AuthenticatorGpmPinSheetModelBase::IsGPMPinOptionsButtonVisible() const {
  return mode_ == Mode::kPinCreate;
}

void AuthenticatorGpmPinSheetModelBase::OnAccept() {
  webauthn::user_actions::RecordAcceptClick();
  dialog_model()->OnGPMPinEntered(pin_);
}

void AuthenticatorGpmPinSheetModelBase::OnCancel() {
  if (dialog_model()->in_onboarding_flow) {
    RecordOnboardingEvent(webauthn::metrics::OnboardingEvents::
                              kAuthenticatorGpmPinSheetCancelled);
  }
  AuthenticatorSheetModelBase::OnCancel();
}

void AuthenticatorGpmPinSheetModelBase::OnForgotGPMPin() const {
  webauthn::user_actions::RecordGpmForgotPinClick();
  dialog_model()->OnForgotGPMPinPressed();
}

void AuthenticatorGpmPinSheetModelBase::OnGPMPinOptionChosen(
    bool is_arbitrary) const {
  if ((dialog_model()->step() == Step::kGPMChangeArbitraryPin ||
       dialog_model()->step() == Step::kGPMCreateArbitraryPin ||
       dialog_model()->step() == Step::kGPMEnterArbitraryPin) &&
      is_arbitrary) {
    // The sheet already facilitates entering arbitrary pin.
    return;
  }
  if ((dialog_model()->step() == Step::kGPMChangePin ||
       dialog_model()->step() == Step::kGPMCreatePin ||
       dialog_model()->step() == Step::kGPMEnterPin) &&
      !is_arbitrary) {
    // The sheet already facilitates entering six digit pin.
    return;
  }
  webauthn::user_actions::RecordGpmPinOptionChangeClick();
  dialog_model()->OnGPMPinOptionChanged(is_arbitrary);
}

// AuthenticatorGpmPinSheetModel -----------------------------------------------

AuthenticatorGpmPinSheetModel::AuthenticatorGpmPinSheetModel(
    AuthenticatorRequestDialogModel* dialog_model,
    int pin_digits_count,
    Mode mode)
    : AuthenticatorGpmPinSheetModelBase(dialog_model, mode),
      pin_digits_count_(pin_digits_count) {
  webauthn::user_actions::RecordGpmPinSheetShown(
      dialog_model->request_type,
      /*is_pin_creation=*/mode == Mode::kPinCreate,
      /*is_arbitrary=*/false);
}

AuthenticatorGpmPinSheetModel::~AuthenticatorGpmPinSheetModel() = default;

void AuthenticatorGpmPinSheetModel::PinCharTyped(bool is_digit) {
  if (mode_ != Mode::kPinCreate || show_digit_hint_ != is_digit) {
    return;
  }

  show_digit_hint_ = !is_digit;
  dialog_model()->OnSheetModelChanged();
}

int AuthenticatorGpmPinSheetModel::pin_digits_count() const {
  return pin_digits_count_;
}

void AuthenticatorGpmPinSheetModel::SetPin(std::u16string pin) {
  bool full_pin_typed_before = FullPinTyped();
  pin_ = std::move(pin);
  bool full_pin_typed = FullPinTyped();

  // When entering an existing PIN, the dialog completes as soon as all the
  // digits have been typed. When creating a new PIN, the user has to hit enter
  // to confirm.
  if (mode_ == Mode::kPinEntry && full_pin_typed) {
    dialog_model()->OnGPMPinEntered(pin_);
  } else if (mode_ == Mode::kPinCreate &&
             full_pin_typed_before != full_pin_typed) {
    dialog_model()->OnButtonsStateChanged();
  }
}

std::u16string AuthenticatorGpmPinSheetModel::GetAccessibleName() const {
  std::u16string pin_digits_typed_str = base::NumberToString16(
      std::min(static_cast<int>(pin_.length()) + 1, pin_digits_count_));
  return l10n_util::GetStringFUTF16(
      IDS_WEBAUTHN_GPM_SIX_DIGIT_PIN_ACCESSIBILITY_LABEL, pin_digits_typed_str);
}

bool AuthenticatorGpmPinSheetModel::FullPinTyped() const {
  return static_cast<int>(pin_.length()) == pin_digits_count_;
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorGpmPinSheetModel::GetAcceptButtonState() const {
  if (mode() == Mode::kPinCreate) {
    return FullPinTyped() && !ui_disabled() ? AcceptButtonState::kEnabled
                                            : AcceptButtonState::kDisabled;
  }
  return AcceptButtonState::kNotVisible;
}

std::u16string AuthenticatorGpmPinSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_CONFIRM);
}

std::u16string AuthenticatorGpmPinSheetModel::GetHint() const {
  return mode_ == Mode::kPinCreate && show_digit_hint_
             ? l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_PIN_DIGIT_HINT)
             : std::u16string();
}

// AuthenticatorGpmArbitraryPinSheetModel --------------------------------------

AuthenticatorGpmArbitraryPinSheetModel::AuthenticatorGpmArbitraryPinSheetModel(
    AuthenticatorRequestDialogModel* dialog_model,
    Mode mode)
    : AuthenticatorGpmPinSheetModelBase(dialog_model, mode) {
  webauthn::user_actions::RecordGpmPinSheetShown(
      dialog_model->request_type,
      /*is_pin_creation=*/mode == Mode::kPinCreate,
      /*is_arbitrary=*/true);
}

AuthenticatorGpmArbitraryPinSheetModel::
    ~AuthenticatorGpmArbitraryPinSheetModel() = default;

void AuthenticatorGpmArbitraryPinSheetModel::SetPin(std::u16string pin) {
  bool accept_button_enabled =
      GetAcceptButtonState() == AcceptButtonState::kEnabled;
  pin_ = std::move(pin);
  if (accept_button_enabled !=
      (GetAcceptButtonState() == AcceptButtonState::kEnabled)) {
    dialog_model()->OnButtonsStateChanged();
  }
}

std::u16string AuthenticatorGpmArbitraryPinSheetModel::GetAccessibleName()
    const {
  switch (mode_) {
    case Mode::kPinCreate:
      return l10n_util::GetStringUTF16(
          IDS_WEBAUTHN_GPM_CREATE_ALPHANUMERIC_PIN_ACCESSIBILITY);
    case Mode::kPinEntry:
      return l10n_util::GetStringFUTF16(
          IDS_WEBAUTHN_GPM_ENTER_ALPHANUMERIC_PIN_ACCESSIBILITY_WITH_WEBSITE,
          GetRelyingPartyIdString(dialog_model()));
  }
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorGpmArbitraryPinSheetModel::GetAcceptButtonState() const {
  return pin_.length() >= kGpmArbitraryPinMinLength && !ui_disabled()
             ? AcceptButtonState::kEnabled
             : AcceptButtonState::kDisabled;
}

std::u16string AuthenticatorGpmArbitraryPinSheetModel::GetAcceptButtonLabel()
    const {
  return mode_ == Mode::kPinEntry
             ? l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_NEXT)
             : l10n_util::GetStringUTF16(IDS_CONFIRM);
}

std::u16string AuthenticatorGpmArbitraryPinSheetModel::GetHint() const {
  return mode_ == Mode::kPinCreate
             ? l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_PIN_LENGTH_HINT)
             : std::u16string();
}

// AuthenticatorTrustThisComputerAssertionSheetModel -------------------------

AuthenticatorTrustThisComputerAssertionSheetModel::
    AuthenticatorTrustThisComputerAssertionSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_LAPTOP_LIGHT,
                                IDR_WEBAUTHN_LAPTOP_DARK);

  webauthn::user_actions::RecordTrustDialogShown(dialog_model->request_type);
}

AuthenticatorTrustThisComputerAssertionSheetModel::
    ~AuthenticatorTrustThisComputerAssertionSheetModel() = default;

std::u16string AuthenticatorTrustThisComputerAssertionSheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_GPM_TRUST_THIS_COMPUTER_ASSERTION_TITLE);
}

std::u16string
AuthenticatorTrustThisComputerAssertionSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_GPM_TRUST_THIS_COMPUTER_ASSERTION_DESC);
}

bool AuthenticatorTrustThisComputerAssertionSheetModel::IsCancelButtonVisible()
    const {
  return true;
}

std::u16string
AuthenticatorTrustThisComputerAssertionSheetModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorTrustThisComputerAssertionSheetModel::GetAcceptButtonState()
    const {
  return AcceptButtonState::kEnabled;
}

std::u16string
AuthenticatorTrustThisComputerAssertionSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_NEXT);
}

bool AuthenticatorTrustThisComputerAssertionSheetModel::
    IsOtherMechanismButtonVisible() const {
  return true;
}

std::u16string AuthenticatorTrustThisComputerAssertionSheetModel::
    GetOtherMechanismButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_USE_A_DIFFERENT_DEVICE);
}

void AuthenticatorTrustThisComputerAssertionSheetModel::OnAccept() {
  webauthn::user_actions::RecordAcceptClick();
  dialog_model()->OnTrustThisComputer();
}

// AuthenticatorCreateGpmPasskeySheetModel -------------------------------------

AuthenticatorCreateGpmPasskeySheetModel::
    AuthenticatorCreateGpmPasskeySheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_GPM_PASSKEY_LIGHT,
                                IDR_WEBAUTHN_GPM_PASSKEY_DARK);

  webauthn::user_actions::RecordCreateGpmDialogShown();
}

AuthenticatorCreateGpmPasskeySheetModel::
    ~AuthenticatorCreateGpmPasskeySheetModel() = default;

std::u16string AuthenticatorCreateGpmPasskeySheetModel::GetStepTitle() const {
  return l10n_util::GetStringFUTF16(IDS_WEBAUTHN_GPM_CREATE_PASSKEY_TITLE,
                                    GetRelyingPartyIdString(dialog_model()));
}

std::u16string AuthenticatorCreateGpmPasskeySheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringFUTF16(
      IDS_WEBAUTHN_GPM_CREATE_PASSKEY_DESC,
      base::UTF8ToUTF16(dialog_model()->GetGpmAccountEmail()));
}

bool AuthenticatorCreateGpmPasskeySheetModel::IsCancelButtonVisible() const {
  return true;
}

std::u16string AuthenticatorCreateGpmPasskeySheetModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorCreateGpmPasskeySheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string AuthenticatorCreateGpmPasskeySheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CREATE);
}

void AuthenticatorCreateGpmPasskeySheetModel::OnAccept() {
  webauthn::user_actions::RecordAcceptClick();
  dialog_model()->OnGPMCreatePasskey();
}

void AuthenticatorCreateGpmPasskeySheetModel::OnCancel() {
  if (dialog_model()->in_onboarding_flow) {
    RecordOnboardingEvent(
        webauthn::metrics::OnboardingEvents::kCreateGpmPasskeySheetCancelled);
  }
  AuthenticatorSheetModelBase::OnCancel();
}

void AuthenticatorCreateGpmPasskeySheetModel::OnBack() {
  if (dialog_model()->in_onboarding_flow) {
    RecordOnboardingEvent(webauthn::metrics::OnboardingEvents::
                              kCreateGpmPasskeySheetSaveAnotherWaySelected);
  }
  AuthenticatorSheetModelBase::OnBack();
}

// AuthenticatorGpmIncognitoCreateSheetModel ---------------------------------
AuthenticatorGpmIncognitoCreateSheetModel::
    AuthenticatorGpmIncognitoCreateSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  // Incognito always has a dark color scheme and so the two illustrations are
  // the same.
  lottie_illustrations_.emplace(IDR_WEBAUTHN_GPM_INCOGNITO,
                                IDR_WEBAUTHN_GPM_INCOGNITO);
}

AuthenticatorGpmIncognitoCreateSheetModel::
    ~AuthenticatorGpmIncognitoCreateSheetModel() = default;

std::u16string AuthenticatorGpmIncognitoCreateSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_INCOGNITO_CREATE_TITLE);
}

std::u16string AuthenticatorGpmIncognitoCreateSheetModel::GetStepDescription()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_GPM_INCOGNITO_CREATE_DESC);
}

bool AuthenticatorGpmIncognitoCreateSheetModel::IsCancelButtonVisible() const {
  return true;
}

std::u16string AuthenticatorGpmIncognitoCreateSheetModel::GetCancelButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorGpmIncognitoCreateSheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string AuthenticatorGpmIncognitoCreateSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CONTINUE);
}
void AuthenticatorGpmIncognitoCreateSheetModel::OnAccept() {
  dialog_model()->OnGPMConfirmOffTheRecordCreate();
}

// AuthenticatorTrustThisComputerCreationSheetModel ---------------------

AuthenticatorTrustThisComputerCreationSheetModel::
    AuthenticatorTrustThisComputerCreationSheetModel(
        AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kVisible) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_LAPTOP_LIGHT,
                                IDR_WEBAUTHN_LAPTOP_DARK);

  webauthn::user_actions::RecordTrustDialogShown(dialog_model->request_type);
}

AuthenticatorTrustThisComputerCreationSheetModel::
    ~AuthenticatorTrustThisComputerCreationSheetModel() = default;

std::u16string AuthenticatorTrustThisComputerCreationSheetModel::GetStepTitle()
    const {
  return l10n_util::GetStringUTF16(
      IDS_WEBAUTHN_GPM_TRUST_THIS_COMPUTER_CREATION_TITLE);
}

std::u16string
AuthenticatorTrustThisComputerCreationSheetModel::GetStepDescription() const {
  return l10n_util::GetStringFUTF16(
      IDS_WEBAUTHN_GPM_TRUST_THIS_COMPUTER_CREATION_DESC,
      base::UTF8ToUTF16(dialog_model()->GetGpmAccountEmail()));
}

bool AuthenticatorTrustThisComputerCreationSheetModel::IsCancelButtonVisible()
    const {
  return true;
}

std::u16string
AuthenticatorTrustThisComputerCreationSheetModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_CANCEL);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorTrustThisComputerCreationSheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string
AuthenticatorTrustThisComputerCreationSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_PIN_ENTRY_NEXT);
}

std::u16string
AuthenticatorTrustThisComputerCreationSheetModel::GetOtherMechanismButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_SAVE_ANOTHER_WAY);
}

void AuthenticatorTrustThisComputerCreationSheetModel::OnAccept() {
  webauthn::user_actions::RecordAcceptClick();
  dialog_model()->OnTrustThisComputer();
}

// AuthenticatorGPMLockedPinSheetModel ----------------------------------

AuthenticatorGPMLockedPinSheetModel::AuthenticatorGPMLockedPinSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  lottie_illustrations_.emplace(IDR_WEBAUTHN_GPM_PIN_LOCKED_LIGHT,
                                IDR_WEBAUTHN_GPM_PIN_LOCKED_DARK);
  webauthn::user_actions::RecordGpmLockedShown();
}

AuthenticatorGPMLockedPinSheetModel::~AuthenticatorGPMLockedPinSheetModel() =
    default;

std::u16string AuthenticatorGPMLockedPinSheetModel::GetStepTitle() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_LOCKED_GPM_PIN_TITLE);
}

std::u16string AuthenticatorGPMLockedPinSheetModel::GetStepDescription() const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_LOCKED_GPM_PIN_DESCRIPTION);
}

AuthenticatorRequestSheetModel::AcceptButtonState
AuthenticatorGPMLockedPinSheetModel::GetAcceptButtonState() const {
  return AcceptButtonState::kEnabled;
}

std::u16string AuthenticatorGPMLockedPinSheetModel::GetAcceptButtonLabel()
    const {
  return l10n_util::GetStringUTF16(IDS_WEBAUTHN_CHANGE_PIN);
}

void AuthenticatorGPMLockedPinSheetModel::OnAccept() {
  webauthn::user_actions::RecordAcceptClick();
  dialog_model()->OnForgotGPMPinPressed();
}

// CombinedSelectorSheetModel

CombinedSelectorSheetModel::CombinedSelectorSheetModel(
    AuthenticatorRequestDialogModel* dialog_model)
    : AuthenticatorSheetModelBase(dialog_model,
                                  OtherMechanismButtonVisibility::kHidden) {
  webauthn::user_actions::RecordCombinedSelectorShown();
}

CombinedSelectorSheetModel::SelectionStatus
CombinedSelectorSheetModel::GetSelectionStatus(size_t index) const {
  if (dialog_model()->mechanisms.size() == 1) {
    return SelectionStatus::kNone;
  }
  return selection_index_ == index ? SelectionStatus::kSelected
                                   : SelectionStatus::kNotSelected;
}

size_t CombinedSelectorSheetModel::GetSelectionIndex() const {
  return selection_index_;
}

void CombinedSelectorSheetModel::SetSelectionIndex(size_t index) {
  selection_index_ = index;
}

std::u16string CombinedSelectorSheetModel::GetStepTitle() const {
  return l10n_util::GetStringFUTF16(
      IDS_WEBAUTHN_SIGN_IN_TO_WEBSITE_DIALOG_TITLE,
      base::UTF8ToUTF16(dialog_model()->relying_party_id));
}

std::u16string CombinedSelectorSheetModel::GetStepDescription() const {
  return u"";
}

AuthenticatorRequestSheetModel::AcceptButtonState
CombinedSelectorSheetModel::GetAcceptButtonState() const {
  return dialog_model()->ui_disabled_ ? AcceptButtonState::kDisabledWithSpinner
                                      : AcceptButtonState::kEnabled;
}

bool CombinedSelectorSheetModel::IsCancelButtonVisible() const {
  return true;
}

bool CombinedSelectorSheetModel::IsActivityIndicatorVisible() const {
  return false;
}

std::u16string CombinedSelectorSheetModel::GetCancelButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_SIGNIN_ACCESSIBLE_CLOSE_BUTTON);
}

std::u16string CombinedSelectorSheetModel::GetAcceptButtonLabel() const {
  return l10n_util::GetStringUTF16(
      IDS_PASSWORD_MANAGER_ACCOUNT_CHOOSER_SIGN_IN);
}

void CombinedSelectorSheetModel::OnAccept() {
  const auto& mech = dialog_model()->mechanisms.at(selection_index_);
  webauthn::user_actions::RecordMechanismClick(mech);
  mech.callback.Run();
}
