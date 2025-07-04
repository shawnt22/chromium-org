// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/webauthn/authenticator_request_dialog_model.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/containers/flat_set.h"
#include "base/containers/to_vector.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/types/strong_alias.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/password_manager/chrome_webauthn_credentials_delegate.h"
#include "chrome/browser/password_manager/chrome_webauthn_credentials_delegate_factory.h"
#include "chrome/browser/webauthn/authenticator_reference.h"
#include "chrome/browser/webauthn/authenticator_request_dialog_controller.h"
#include "chrome/browser/webauthn/authenticator_transport.h"
#include "chrome/browser/webauthn/gpm_user_verification_policy.h"
#include "chrome/browser/webauthn/webauthn_pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/password_manager/core/browser/passkey_credential.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "components/webauthn/core/browser/passkey_model.h"
#include "content/public/browser/authenticator_request_client_delegate.h"
#include "device/fido/cable/cable_discovery_data.h"
#include "device/fido/cable/v2_constants.h"
#include "device/fido/discoverable_credential_metadata.h"
#include "device/fido/fido_constants.h"
#include "device/fido/fido_request_handler_base.h"
#include "device/fido/fido_transport_protocol.h"
#include "device/fido/fido_types.h"
#include "device/fido/public_key_credential_user_entity.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "device/fido/win/fake_webauthn_api.h"
#include "device/fido/win/webauthn_api.h"
#endif

namespace {

using testing::ElementsAre;

using BleStatus = device::FidoRequestHandlerBase::BleStatus;
using RequestType = device::FidoRequestType;
using Step = AuthenticatorRequestDialogModel::Step;
using UIPresentation =
    content::AuthenticatorRequestClientDelegate::UIPresentation;

const base::flat_set<AuthenticatorTransport> kAllTransports = {
    AuthenticatorTransport::kUsbHumanInterfaceDevice,
    AuthenticatorTransport::kNearFieldCommunication,
    AuthenticatorTransport::kInternal,
    AuthenticatorTransport::kHybrid,
};

const base::flat_set<AuthenticatorTransport> kAllTransportsWithoutCable = {
    AuthenticatorTransport::kUsbHumanInterfaceDevice,
    AuthenticatorTransport::kNearFieldCommunication,
    AuthenticatorTransport::kInternal,
};

using TransportAvailabilityInfo =
    device::FidoRequestHandlerBase::TransportAvailabilityInfo;

class RequestCallbackReceiver {
 public:
  base::RepeatingCallback<void(const std::string&)> Callback() {
    return base::BindRepeating(&RequestCallbackReceiver::OnRequest,
                               weak_factory_.GetWeakPtr());
  }

  std::string WaitForResult() {
    if (!authenticator_id_) {
      run_loop_->Run();
    }
    std::string ret = std::move(*authenticator_id_);
    authenticator_id_.reset();
    run_loop_ = std::make_unique<base::RunLoop>();
    return ret;
  }

 private:
  void OnRequest(const std::string& authenticator_id) {
    authenticator_id_ = authenticator_id;
    run_loop_->Quit();
  }
  std::optional<std::string> authenticator_id_;
  std::unique_ptr<base::RunLoop> run_loop_ = std::make_unique<base::RunLoop>();
  base::WeakPtrFactory<RequestCallbackReceiver> weak_factory_{this};
};

class MockDialogModelObserver
    : public AuthenticatorRequestDialogModel::Observer {
 public:
  MockDialogModelObserver() = default;

  MockDialogModelObserver(const MockDialogModelObserver&) = delete;
  MockDialogModelObserver& operator=(const MockDialogModelObserver&) = delete;

  MOCK_METHOD0(OnStartOver, void());
  MOCK_METHOD1(OnModelDestroyed, void(AuthenticatorRequestDialogModel*));
  MOCK_METHOD0(OnStepTransition, void());
  MOCK_METHOD0(OnCancelRequest, void());
  MOCK_METHOD0(OnBluetoothPoweredStateChanged, void());
};

class BluetoothAdapterPowerOnCallbackReceiver {
 public:
  BluetoothAdapterPowerOnCallbackReceiver() = default;

  BluetoothAdapterPowerOnCallbackReceiver(
      const BluetoothAdapterPowerOnCallbackReceiver&) = delete;
  BluetoothAdapterPowerOnCallbackReceiver& operator=(
      const BluetoothAdapterPowerOnCallbackReceiver&) = delete;

  base::RepeatingClosure GetCallback() {
    return base::BindRepeating(
        &BluetoothAdapterPowerOnCallbackReceiver::CallbackTarget,
        base::Unretained(this));
  }

  bool was_called() const { return was_called_; }

 private:
  void CallbackTarget() {
    ASSERT_FALSE(was_called_);
    was_called_ = true;
  }

  bool was_called_ = false;
};

std::string_view RequestTypeToString(RequestType req_type) {
  switch (req_type) {
    case RequestType::kGetAssertion:
      return "GetAssertion";
    case RequestType::kMakeCredential:
      return "MakeCredential";
  }
}

enum class TransportAvailabilityParam {
  kMaybeHasPlatformCredential,
  kHasPlatformCredential,
  kOneRecognizedCred,
  kTwoRecognizedCreds,
  kOneTouchIDRecognizedCred,
  kEmptyAllowList,
  kOnlyInternal,
  kOnlyHybridOrInternal,
  kHasWinNativeAuthenticator,
  kWindowsHandlesHybrid,
  kHasCableV1Extension,
  kHasCableV2Extension,
  kRequireResidentKey,
  kIsConditionalUI,
  kAttachmentAny,
  kAttachmentCrossPlatform,
  kBleDisabled,
  kBleAccessDenied,
  kHasICloudKeychain,
  kHasICloudKeychainCreds,
  kCreateInICloudKeychain,
  kNoTouchId,
  kUVRequired,
  kUVPreferred,
  kHintSecurityKeys,
  kHintHybrid,
  kHintClientDevice,
  kEnclaveCred,
  kEnclaveNeedsSignIn,
};

std::string_view TransportAvailabilityParamToString(
    TransportAvailabilityParam param) {
  switch (param) {
    case TransportAvailabilityParam::kMaybeHasPlatformCredential:
      return "kMaybeHasPlatformCredential";
    case TransportAvailabilityParam::kHasPlatformCredential:
      return "kHasPlatformCredential";
    case TransportAvailabilityParam::kOneRecognizedCred:
      return "kOneRecognizedCred";
    case TransportAvailabilityParam::kTwoRecognizedCreds:
      return "kTwoRecognizedCreds";
    case TransportAvailabilityParam::kOneTouchIDRecognizedCred:
      return "kOneTouchIDRecognizedCred";
    case TransportAvailabilityParam::kEmptyAllowList:
      return "kEmptyAllowList";
    case TransportAvailabilityParam::kOnlyInternal:
      return "kOnlyInternal";
    case TransportAvailabilityParam::kOnlyHybridOrInternal:
      return "kOnlyHybridOrInternal";
    case TransportAvailabilityParam::kHasWinNativeAuthenticator:
      return "kHasWinNativeAuthenticator";
    case TransportAvailabilityParam::kWindowsHandlesHybrid:
      return "kWindowsHandlesHybrid";
    case TransportAvailabilityParam::kHasCableV1Extension:
      return "kHasCableV1Extension";
    case TransportAvailabilityParam::kHasCableV2Extension:
      return "kHasCableV2Extension";
    case TransportAvailabilityParam::kRequireResidentKey:
      return "kRequireResidentKey";
    case TransportAvailabilityParam::kIsConditionalUI:
      return "kIsConditionalUI";
    case TransportAvailabilityParam::kAttachmentAny:
      return "kAttachmentAny";
    case TransportAvailabilityParam::kAttachmentCrossPlatform:
      return "kAttachmentCrossPlatform";
    case TransportAvailabilityParam::kBleDisabled:
      return "kBleDisabled";
    case TransportAvailabilityParam::kBleAccessDenied:
      return "kBleAccessDenied";
    case TransportAvailabilityParam::kHasICloudKeychain:
      return "kHasICloudKeychain";
    case TransportAvailabilityParam::kHasICloudKeychainCreds:
      return "kHasICloudKeychainCreds";
    case TransportAvailabilityParam::kCreateInICloudKeychain:
      return "kCreateInICloudKeychain";
    case TransportAvailabilityParam::kNoTouchId:
      return "kNoTouchId";
    case TransportAvailabilityParam::kUVRequired:
      return "kUVRequired";
    case TransportAvailabilityParam::kUVPreferred:
      return "kUVPreferred";
    case TransportAvailabilityParam::kHintSecurityKeys:
      return "kHintSecurityKeys";
    case TransportAvailabilityParam::kHintHybrid:
      return "kHintHybrid";
    case TransportAvailabilityParam::kHintClientDevice:
      return "kHintClientDevice";
    case TransportAvailabilityParam::kEnclaveCred:
      return "kEnclaveCred";
    case TransportAvailabilityParam::kEnclaveNeedsSignIn:
      return "kEnclaveNeedsSignIn";
  }
}

template <typename T, std::string_view (*F)(T)>
std::string SetToString(base::flat_set<T> s) {
  return base::JoinString(base::ToVector(s, F), ", ");
}

const device::PublicKeyCredentialUserEntity kUser1({1, 2, 3, 4},
                                                   "A",
                                                   std::nullopt);
const device::PublicKeyCredentialUserEntity kUser2({5, 6, 7, 8},
                                                   "B",
                                                   std::nullopt);
const device::PublicKeyCredentialUserEntity kPhoneUser1({9, 0, 1, 2},
                                                        "C",
                                                        std::nullopt);
const device::PublicKeyCredentialUserEntity kPhoneUser2({3, 4, 5, 6},
                                                        "D",
                                                        std::nullopt);

const device::DiscoverableCredentialMetadata kCred1(
    device::AuthenticatorType::kOther,
    "rp.com",
    {0},
    kUser1,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kCred1FromICloudKeychain(
    device::AuthenticatorType::kICloudKeychain,
    "rp.com",
    {4},
    kUser1,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kCred1From3p(
    device::AuthenticatorType::kICloudKeychain,
    "rp.com",
    {4},
    kUser1,
    "Bitwarden");
const device::DiscoverableCredentialMetadata kCred1FromChromeOS(
    device::AuthenticatorType::kChromeOS,
    "rp.com",
    {4},
    kUser1,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kCred2(
    device::AuthenticatorType::kOther,
    "rp.com",
    {1},
    kUser2,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kWinCred1(
    device::AuthenticatorType::kWinNative,
    "rp.com",
    {0},
    kUser1,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kWinCred2(
    device::AuthenticatorType::kWinNative,
    "rp.com",
    {1},
    kUser2,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kTouchIDCred1(
    device::AuthenticatorType::kTouchID,
    "rp.com",
    {4},
    kUser1,
    /*provider_name=*/std::nullopt);
const device::DiscoverableCredentialMetadata kEnclaveCred1(
    device::AuthenticatorType::kEnclave,
    "rp.com",
    {1},
    kUser1,
    /*provider_name=*/std::nullopt);

AuthenticatorRequestDialogModel::Mechanism::CredentialInfo CredentialInfoFrom(
    const device::DiscoverableCredentialMetadata& metadata) {
  return AuthenticatorRequestDialogModel::Mechanism::CredentialInfo(
      metadata.source, metadata.user.id, metadata.last_used_time);
}

template <class Value>
class RepeatingValueCallbackReceiver {
 public:
  base::RepeatingCallback<void(Value)> Callback() {
    return base::BindRepeating(&RepeatingValueCallbackReceiver::OnCallback,
                               base::Unretained(this));
  }

  Value WaitForResult() {
    if (!value_) {
      run_loop_->Run();
    }
    Value ret = std::move(*value_);
    value_.reset();
    run_loop_ = std::make_unique<base::RunLoop>();
    return ret;
  }

 private:
  void OnCallback(Value value) {
    value_ = std::move(value);
    run_loop_->Quit();
  }
  std::optional<Value> value_;
  std::unique_ptr<base::RunLoop> run_loop_ = std::make_unique<base::RunLoop>();
};

void UpdateModelBeforeStartFlow(AuthenticatorRequestDialogModel* model,
                                TransportAvailabilityInfo tai) {
  model->request_type = tai.request_type;
  model->resident_key_requirement = tai.resident_key_requirement;
  model->attestation_conveyance_preference =
      tai.attestation_conveyance_preference;
  model->ble_adapter_is_powered =
      tai.ble_status == device::FidoRequestHandlerBase::BleStatus::kOn;
  model->show_security_key_on_qr_sheet =
      base::Contains(tai.available_transports,
                     device::FidoTransportProtocol::kUsbHumanInterfaceDevice);
  model->is_off_the_record = tai.is_off_the_record_context;
  model->platform_has_biometrics = tai.platform_has_biometrics;
}

}  // namespace

class AuthenticatorRequestDialogControllerTest
    : public ChromeRenderViewHostTestHarness {
 public:
  AuthenticatorRequestDialogControllerTest()
      : ChromeRenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  AuthenticatorRequestDialogControllerTest(
      const AuthenticatorRequestDialogControllerTest&) = delete;
  AuthenticatorRequestDialogControllerTest& operator=(
      const AuthenticatorRequestDialogControllerTest&) = delete;
};

constexpr bool kIsMac = BUILDFLAG(IS_MAC);

class FakeEnclaveController : public AuthenticatorRequestDialogModel::Observer {
 public:
  explicit FakeEnclaveController(AuthenticatorRequestDialogModel* model)
      : model_(model) {
    model_observer_.Observe(model_);
  }

  void OnGPMPasskeySelected(std::vector<uint8_t> credential_id) override {
    if (GpmWillDoUserVerification(
            device::UserVerificationRequirement::kPreferred,
            *model_->platform_has_biometrics)) {
      if (kIsMac) {
        model_->SetStep(Step::kGPMTouchID);
      } else {
        model_->SetStep(Step::kGPMEnterPin);
      }
    } else {
      model_->SetStep(Step::kSelectAccount);
    }
  }

  void OnModelDestroyed(AuthenticatorRequestDialogModel*) override {
    model_observer_.Reset();
    model_ = nullptr;
  }

 private:
  raw_ptr<AuthenticatorRequestDialogModel> model_;
  base::ScopedObservation<AuthenticatorRequestDialogModel,
                          AuthenticatorRequestDialogModel::Observer>
      model_observer_{this};
};

TEST_F(AuthenticatorRequestDialogControllerTest, Mechanisms) {
  const auto mc = RequestType::kMakeCredential;
  const auto ga = RequestType::kGetAssertion;
  const auto usb = AuthenticatorTransport::kUsbHumanInterfaceDevice;
  const auto internal = AuthenticatorTransport::kInternal;
  const auto cable = AuthenticatorTransport::kHybrid;
  const auto cred1 = CredentialInfoFrom(kCred1);
  const auto cred2 = CredentialInfoFrom(kCred2);
  const auto ickc_cred1 = CredentialInfoFrom(kCred1FromICloudKeychain);
  const auto wincred1 = CredentialInfoFrom(kWinCred1);
  const auto wincred2 = CredentialInfoFrom(kWinCred2);
  [[maybe_unused]] const auto touchid_cred1 = CredentialInfoFrom(kTouchIDCred1);
  [[maybe_unused]] const auto enclave_cred1 = CredentialInfoFrom(kEnclaveCred1);
  const auto v1 = TransportAvailabilityParam::kHasCableV1Extension;
  const auto has_winapi =
      TransportAvailabilityParam::kHasWinNativeAuthenticator;
  [[maybe_unused]] const auto win_hybrid =
      TransportAvailabilityParam::kWindowsHandlesHybrid;
  const auto has_plat = TransportAvailabilityParam::kHasPlatformCredential;
  const auto maybe_plat =
      TransportAvailabilityParam::kMaybeHasPlatformCredential;
  const auto one_cred = TransportAvailabilityParam::kOneRecognizedCred;
  [[maybe_unused]] const auto one_touchid_cred =
      TransportAvailabilityParam::kOneTouchIDRecognizedCred;
  const auto two_cred = TransportAvailabilityParam::kTwoRecognizedCreds;
  const auto empty_al = TransportAvailabilityParam::kEmptyAllowList;
  [[maybe_unused]] const auto enclave_cred =
      TransportAvailabilityParam::kEnclaveCred;
  [[maybe_unused]] const auto only_internal =
      TransportAvailabilityParam::kOnlyInternal;
  const auto only_hybrid_or_internal =
      TransportAvailabilityParam::kOnlyHybridOrInternal;
  const auto rk = TransportAvailabilityParam::kRequireResidentKey;
  const auto c_ui = TransportAvailabilityParam::kIsConditionalUI;
  const auto att_any = TransportAvailabilityParam::kAttachmentAny;
  const auto att_xplat = TransportAvailabilityParam::kAttachmentCrossPlatform;
  const auto ble_off = TransportAvailabilityParam::kBleDisabled;
  const auto ble_denied = TransportAvailabilityParam::kBleAccessDenied;
  const auto hint_sk = TransportAvailabilityParam::kHintSecurityKeys;
  const auto hint_hybrid = TransportAvailabilityParam::kHintHybrid;
  const auto hint_plat = TransportAvailabilityParam::kHintClientDevice;
  [[maybe_unused]] const auto has_ickc =
      TransportAvailabilityParam::kHasICloudKeychain;
  [[maybe_unused]] const auto create_ickc =
      TransportAvailabilityParam::kCreateInICloudKeychain;
  [[maybe_unused]] const auto no_touchid =
      TransportAvailabilityParam::kNoTouchId;
  [[maybe_unused]] const auto ickc_creds =
      TransportAvailabilityParam::kHasICloudKeychainCreds;
  [[maybe_unused]] const auto uv_pref =
      TransportAvailabilityParam::kUVPreferred;
  [[maybe_unused]] const auto uv_req = TransportAvailabilityParam::kUVRequired;
  const auto enclave_needs_sign_in =
      TransportAvailabilityParam::kEnclaveNeedsSignIn;
  using c = AuthenticatorRequestDialogModel::Mechanism::Credential;
  using t = AuthenticatorRequestDialogModel::Mechanism::Transport;
  const auto winapi = AuthenticatorRequestDialogModel::Mechanism::WindowsAPI();
  const auto add = AuthenticatorRequestDialogModel::Mechanism::AddPhone();
  const auto sign_in_again =
      AuthenticatorRequestDialogModel::Mechanism::SignInAgain();
  [[maybe_unused]] const auto ickc =
      AuthenticatorRequestDialogModel::Mechanism::ICloudKeychain();
  const auto usb_ui = Step::kUsbInsertAndActivate;
  const auto mss = Step::kMechanismSelection;
  const auto plat_ui = Step::kNotStarted;
  const auto cable_ui = Step::kCableActivate;
  [[maybe_unused]] const auto create_pk = Step::kCreatePasskey;
  const auto create_pk_or_mss =
#if BUILDFLAG(IS_MAC)
      Step::kCreatePasskey;
#else
      Step::kMechanismSelection;
#endif
  const auto create_pk_or_plat_ui =
#if BUILDFLAG(IS_MAC)
      Step::kCreatePasskey;
#else
      Step::kNotStarted;
#endif
  const auto create_pk_or_qr =
#if BUILDFLAG(IS_MAC)
      Step::kCreatePasskey;
#else
      Step::kCableV2QRCode;
#endif
  [[maybe_unused]] const auto use_pk = Step::kPreSelectAccount;
  const auto qr = Step::kCableV2QRCode;
  const auto hero = Step::kSelectPriorityMechanism;
  [[maybe_unused]] const auto enclave_touchid = Step::kGPMTouchID;
  [[maybe_unused]] const auto enclave_pin = Step::kGPMEnterPin;

  struct Test {
    int line_num;
    RequestType request_type;
    base::flat_set<AuthenticatorTransport> transports;
    base::flat_set<TransportAvailabilityParam> params;
    std::vector<AuthenticatorRequestDialogModel::Mechanism::Type>
        expected_mechanisms;
    Step expected_first_step;
  };

#define L __LINE__
  // clang-format off
  Test kTests[]{
      // If there's only a single mechanism, it should activate.
      {L, mc, {usb}, {}, {t(usb)}, usb_ui},
      {L, ga, {usb}, {}, {t(usb)}, usb_ui},
      {L, ga, {usb, cable}, {}, {add}, qr},
      {L, ga, {usb, cable}, {}, {add}, qr},
      // If the platform authenticator has a credential it should activate.
      {L,
       ga,
       {},
       {has_plat, one_cred},
       {c(cred1)},
       plat_ui,
     },
      // If the platform authenticator has a credential it should activate.
      {L,
       ga,
       {usb, internal},
       {has_plat, one_cred},
       {c(cred1), t(usb)},
#if BUILDFLAG(IS_MAC)
       plat_ui
#else
       use_pk
#endif
      },
#if BUILDFLAG(IS_MAC)
       // Without Touch ID, the profile authenticator will show a confirmation
       // prompt.
      {L, ga, {usb, internal}, {has_plat, one_cred, no_touchid},
       {c(cred1), t(usb)}, use_pk},
      // When a single profile credential is available with uv!=required and no
      // Touch ID, the UI must show the confirmation because, otherwise,
      // there'll be no UI at all.
      {L, ga, {internal}, {has_plat, one_touchid_cred, no_touchid},
       {c(touchid_cred1)}, hero},
      // When TouchID is present, we can jump directly to the platform UI, which
      // will be a Touch ID prompt.
      {L, ga, {internal}, {has_plat, one_touchid_cred, uv_pref},
       {c(touchid_cred1)}, plat_ui},
      // Or if uv=required, plat_ui is also ok because it'll be a password
      // prompt.
      {L, ga, {internal}, {has_plat, one_touchid_cred, uv_req, no_touchid},
       {c(touchid_cred1)}, plat_ui},
      // The profile authenticator does UV even for uv=discouraged.
      {L, ga, {internal}, {has_plat, one_touchid_cred}, {c(touchid_cred1)},
       plat_ui},
#endif
      // Even with an empty allow list.
      {L,
       ga,
       {usb, internal},
       {has_plat, one_cred, empty_al},
       {c(cred1), t(usb)},
       hero},
      // Two credentials shows mechanism selection.
      {L,
       ga,
       {usb, internal},
       {has_plat, two_cred, empty_al},
       {c(cred1), c(cred2), t(usb)},
       mss},

      // MakeCredential with attachment=platform shows the 'Create a passkey'
      // step, but only on macOS. On other OSes, we defer to the platform.
      {L,
       mc,
       {internal},
       {},
       {t(internal)},
       create_pk_or_plat_ui,
      },
      // MakeCredential with attachment=undefined also shows the 'Create a
      // passkey' step on macOS. On other OSes, we show mechanism selection.
      {L,
       mc,
       {usb, internal},
       {},
       {t(internal), t(usb)},
       create_pk_or_mss,
      },

      // If the Windows API is available without caBLE, it should activate.
      {L, mc, {}, {has_winapi}, {winapi}, plat_ui},
      {L, ga, {}, {has_winapi}, {winapi}, plat_ui},
      // ...even if there are discovered Windows credentials.
      {L, ga, {}, {has_winapi, one_cred}, {c(wincred1), winapi}, plat_ui},

      // A caBLEv1 extension should cause us to go directly to caBLE.
      {L, ga, {usb, cable}, {v1}, {t(cable), t(usb)}, cable_ui},

      // If this is a Conditional UI request, don't offer the platform
      // authenticator.
      {L, ga, {usb, internal}, {c_ui}, {t(usb)}, usb_ui},
      {L,
       ga,
       {usb, internal, cable},
       {c_ui},
       {add},
       qr},

      // On Windows, mc with rk=required jumps to the platform UI when caBLE
      // isn't an option. The case where caBLE is possible is tested below.
      {L, mc, {}, {has_winapi, rk}, {winapi}, plat_ui},
      // For rk=discouraged, always jump to Windows UI.
      {L, mc, {cable}, {has_winapi}, {winapi, add}, plat_ui},
      {L, mc, {}, {has_winapi}, {winapi}, plat_ui},

      // On Windows, ga with an empty allow list goes to the platform UI unless
      // caBLE is an option and resident-key is required, which is tested below.
      {L, ga, {}, {has_winapi, empty_al}, {winapi}, plat_ui},
      // With a non-empty allow list containing non phone credentials, always
      // jump to Windows UI.
      // TODO(NEWUI): we should maintain this behaviour on Windows.
      {L, ga, {cable}, {has_winapi}, {add, winapi}, mss},
      {L, ga, {}, {has_winapi}, {winapi}, plat_ui},

       // With attachment=undefined, the UI should still default to a platform
       // authenticator.
       {L, mc, {usb, internal, cable}, {att_any}, {add, t(internal)},
        create_pk_or_mss},
       {L, mc, {usb, internal}, {att_any, rk}, {t(internal), t(usb)},
        create_pk_or_mss},

      // QR code first: Make credential should jump to the QR code with
      // RK=true.
      {L,
       mc,
       {usb, internal, cable},
       {rk, att_xplat},
       {add, t(internal)},
       qr},
      // Or if attachment=any
      {L,
       mc,
       {usb, internal, cable},
       {rk, att_any},
       {add, t(internal)},
       create_pk_or_qr},
      // If RK=false, go to the default for the platform instead.
      {
          L,
          mc,
          {usb, internal, cable},
          {},
          {add, t(internal)},
          create_pk_or_mss,
      },
      // Windows should also jump to the QR code first.
      {L, mc, {cable}, {att_xplat, rk, has_winapi}, {add, winapi}, qr},
      // ... but not for attachment=undefined.
      {L, mc, {cable}, {rk, has_winapi}, {winapi, add}, plat_ui},

      // QR code first: Get assertion should jump to the QR code with empty
      // allow-list.
      {L,
       ga,
       {usb, internal, cable},
       {empty_al},
       {add},
       qr},
      // And if the allow list only contains phones.
      {L,
       ga,
       {internal, cable},
       {only_hybrid_or_internal},
       {add},
       qr},
      // Or a recognized platform credential.
      {L,
       ga,
       {usb, internal, cable},
       {empty_al, has_plat, one_cred},
       {c(cred1), add},
       hero},
      // Ignore the platform credential for conditional ui requests
      {L,
       ga,
       {usb, internal, cable},
       {c_ui, empty_al, has_plat, one_cred},
       {add},
       qr},
      // If there is an allow-list containing USB, go to QR code as well.
      {L, ga, {usb, internal, cable}, {}, {add}, qr},
      // Windows should also jump to the QR code first.
      // TODO: the expectation here (mss) doesn't match the comment.
      {L, ga, {cable}, {empty_al, has_winapi}, {add, winapi}, mss},
      // Unless there is a recognized platform credential.
      {L,
       ga,
       {cable},
       {empty_al, has_winapi, has_plat, one_cred},
       {c(wincred1), add, winapi},
       hero},
      // For <=Win 10, we can't tell if there is a credential or not. Show the
      // mechanism selection screen instead.
      {L,
       ga,
       {cable},
       {empty_al, has_winapi, maybe_plat},
       {winapi, add},
       mss},
    #if BUILDFLAG(IS_MAC)
      // If there's a single enclave passkey, we should jump directly to
      // the enclave Touch ID sheet.
      {L,
       ga,
       {cable, internal},
       {only_hybrid_or_internal, empty_al, enclave_cred, uv_pref},
       {c(enclave_cred1), add},
       enclave_touchid},
      // But not if Touch ID isn't available.
      {L,
       ga,
       {cable, internal},
       {only_hybrid_or_internal, empty_al, enclave_cred, no_touchid, uv_pref},
       {c(enclave_cred1), add},
       hero},
      // And not if uv=discouraged
      {L,
       ga,
       {cable, internal},
       {only_hybrid_or_internal, empty_al, enclave_cred},
       {c(enclave_cred1), add},
       hero},
    #endif
    #if !BUILDFLAG(IS_CHROMEOS)
      // If an enclave credential is in an allowlist, we should jump to UV
      // immediately.
      {L,
       ga,
       {cable, internal},
       {only_hybrid_or_internal, enclave_cred, uv_pref},
       {c(enclave_cred1), add},
       kIsMac ? enclave_touchid : hero},
     #endif
      // But, again, not for uv=discouraged.
      {L,
       ga,
       {cable, internal},
       {only_hybrid_or_internal, enclave_cred},
       {c(enclave_cred1), add},
       hero},
      // When the enclave needs to sign-in again, that should appear as a
      // mechanism and the MSS should be shown.
      {L,
       ga,
       {cable, usb},
       {enclave_cred, enclave_needs_sign_in},
       {sign_in_again, add},
       mss},
      // Hinting "client-device" should not jump to the sign-in-again option.
      {L,
       mc,
       {cable, usb},
       {enclave_needs_sign_in, hint_plat},
       {sign_in_again, add},
       mss},
      // Hinting "client-device" should not just to any other options, like
      // the profile authenticator, if GPM needs to sign in again.
      {L,
       mc,
       {cable, usb, internal},
       {enclave_needs_sign_in, hint_plat},
       {sign_in_again, add, t(internal)},
       mss},

      // Tests for the mechanism selection screen mixing credential types.
      // Mac & Linux:
      // Internal credentials + qr code.
      {L,
       ga,
       {usb, cable, internal},
       {two_cred, has_plat, empty_al},
       {c(cred1), c(cred2), add},
       mss},
      // qr code with ble disabled shows usb option.
      {L, ga, {usb, cable}, {ble_off}, {add, t(usb)}, mss},
      // qr code with ble access denied shows usb option.
      {L, ga, {usb, cable}, {ble_denied}, {add, t(usb)}, mss},
      // Internal credentials, no qr code.
      {L,
       ga,
       {usb, internal},
       {two_cred, has_plat, empty_al},
       {c(cred1), c(cred2), t(usb)},
       mss},
      // Single internal credential with empty allow list.
      {L,
       ga,
       {usb, cable, internal},
       {one_cred, has_plat, empty_al},
       {c(cred1), add},
       hero,
     },
      // Single internal credential with non-empty allow list.
      {L,
       ga,
       {usb, cable, internal},
       {one_cred, has_plat},
       {c(cred1), add},
#if BUILDFLAG(IS_MAC)
       plat_ui,
#else
       use_pk,
#endif
      },
      // Regression test for crbug.com/1484660.
      // A platform authenticator that reports the availability of credentials
      // but does not enumerate them should be listed.
      {L,
       ga,
       {usb, cable, internal},
       {has_plat},
       {add, t(internal)},
       plat_ui},

#if BUILDFLAG(IS_MAC)
      // Even with iCloud Keychain present, we shouldn't jump to it without
      // additional flags set.
      {L, mc, {internal}, {rk, has_ickc}, {ickc, t(internal)}, create_pk},
      // iCloud Keychain should be the default if the request delegate
      // configured that.
      {L,
       mc,
       {internal},
       {rk, has_ickc, create_ickc},
       {ickc, t(internal)},
       plat_ui},
      // ... and also for attachment=any
      {L,
       mc,
       {internal},
       {rk, att_any, has_ickc, create_ickc},
       {ickc, t(internal)},
       plat_ui},
#endif

       // Tests for RP hints.
       //
       // create(): Security key hint should show security key UI.
       {L, mc, {usb, internal, cable}, {rk, hint_sk},
        {add, t(internal), t(usb)}, usb_ui},
       // But not if USB isn't a valid transport.
       {L, mc, {internal, cable}, {rk, hint_sk}, {add, t(internal)},
#if BUILDFLAG(IS_MAC)
         create_pk,
#else
         qr,
#endif
       },
       // If webauthn.dll is present, jump to it.
       {L, mc, {cable}, {has_winapi, rk, hint_sk}, {winapi, add}, plat_ui},

       // create(): Hybrid hint should show QR.
       {L, mc, {usb, internal, cable}, {rk, hint_hybrid},
        {add, t(internal), t(usb)}, qr},
       // But not if Hybrid isn't a valid transport.
       {L, mc, {usb, internal}, {rk, hint_hybrid}, {t(internal), t(usb)},
#if BUILDFLAG(IS_MAC)
         create_pk,
#else
         mss,
#endif
       },
       // If older webauthn.dll is present, don't jump to it since it doesn't do
       // hybrid.
       {L, mc, {cable}, {has_winapi, rk, hint_hybrid}, {winapi, add}, qr},
#if BUILDFLAG(IS_WIN)
       // ... but do if it supports hybrid.
       {L, mc, {cable}, {has_winapi, win_hybrid, rk, hint_hybrid}, {winapi},
        plat_ui},
#endif

       // create(): Client device hint should jump to the platform
       // authenticator.
       {L, mc, {usb, internal, cable}, {rk, hint_plat}, {add, t(internal)},
#if BUILDFLAG(IS_MAC)
         create_pk,
#else
         plat_ui,
#endif
       },
       // But not if there isn't a platform authenticator.
       {L, mc, {usb, cable}, {rk, hint_plat}, {add}, qr},
       // If webauthn.dll is present, jump to it.
       {L, mc, {cable}, {has_winapi, rk, hint_plat}, {winapi, add},
        plat_ui},
       // Or if there's iCloud Keychain.
       {L, mc, {cable}, {has_ickc, create_ickc, rk, hint_plat}, {ickc, add},
        plat_ui},

       // get(): Security key hint should show security key UI.
       {L, ga, {usb, internal, cable}, {rk, hint_sk}, {add, t(usb)},
        usb_ui},
       // But not if USB isn't a valid transport.
       {L, ga, {internal, cable}, {rk, hint_sk}, {add}, qr},
       // If credentials are found on a platform authenticator, they are still
       // shown.
       {L, ga, {usb, internal, cable}, {one_cred, rk, hint_sk},
        {c(cred1), add, t(usb)}, mss},
       // If webauthn.dll is present, jump to it.
       {L, ga, {cable}, {has_winapi, rk, hint_sk}, {add, winapi}, plat_ui},

       // get(): Hybrid hint should show QR.
       {L, ga, {usb, internal, cable}, {rk, hint_hybrid}, {add, t(usb)}, qr},
       // But not if hybrid isn't available.
       {L, ga, {usb, internal}, {rk, hint_hybrid}, {t(usb)}, usb_ui},
       // If older webauthn.dll is present, don't jump to it since it doesn't do
       // hybrid.
       {L, ga, {cable}, {has_winapi, rk, hint_hybrid}, {add, winapi}, qr},
#if BUILDFLAG(IS_WIN)
       // ... but do if it supports hybrid.
       {L, ga, {cable}, {has_winapi, win_hybrid, rk, hint_hybrid}, {winapi},
        plat_ui},
#endif
       // If credentials are found on a platform authenticator, they are still
       // shown.
       {L, ga, {usb, internal, cable}, {one_cred, rk, hint_hybrid},
        {c(cred1), add, t(usb)}, mss},

       // get(): Client device hint should trigger webauthn.dll, if it exists.
       {L, ga, {cable}, {rk, has_winapi, hint_plat}, {add, winapi},
        plat_ui},
       // But not if there's a credential match.
       {L, ga, {usb, cable, internal}, {one_cred, has_winapi, rk, hint_plat},
        {c(wincred1), add, winapi}, mss},
       // And otherwise it doesn't do anything because we generally assume that
       // we can enumerate platform authenticators and do a good job.
       {L, ga, {usb, cable, internal}, {rk, hint_plat}, {add}, qr},

#if BUILDFLAG(IS_WIN)
      // Windows tests.
      // Mix of internal credentials, but no USB/NFC.
      // This should jump to Windows, as there is a match with the local
      // authenticator.
      {L,
       ga,
       {cable},
       {two_cred, has_winapi, only_hybrid_or_internal,
        has_plat},
       {c(wincred1), c(wincred2), add},
       plat_ui},
      // Mix of internal credentials, and USB/NFC (empty allow list).
      // This should offer dispatching to the Windows API for USB/NFC.
      {L,
       ga,
       {cable},
       {two_cred, has_winapi, empty_al, has_plat},
       {c(wincred1), c(wincred2), add, winapi},
       mss},

      // Tests where Windows handles hybrid with internal credentials only.
      // This should dispatch directly to the Windows API.
      {L,
       ga,
       {},
       {two_cred, has_winapi, win_hybrid, only_internal, has_plat},
       {c(wincred1), c(wincred2)},
       plat_ui},
#endif  // BUILDFLAG(IS_WIN)
  };
  // clang-format on
#undef L

#if BUILDFLAG(IS_WIN)
  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);
#endif

  auto RunTest = [&](const Test& test) {
    SCOPED_TRACE(static_cast<int>(test.expected_first_step));
    SCOPED_TRACE(
        (SetToString<TransportAvailabilityParam,
                     TransportAvailabilityParamToString>(test.params)));
    SCOPED_TRACE((SetToString<device::FidoTransportProtocol, device::ToString>(
        test.transports)));
    SCOPED_TRACE(RequestTypeToString(test.request_type));
    SCOPED_TRACE(testing::Message() << "At line number: " << test.line_num);

#if BUILDFLAG(IS_WIN)
    bool has_win_hybrid = base::Contains(
        test.params, TransportAvailabilityParam::kWindowsHandlesHybrid);
    fake_win_webauthn_api.set_version(has_win_hybrid ? 7 : 4);
#endif

    TransportAvailabilityInfo transports_info;
    if (base::Contains(test.params, TransportAvailabilityParam::kBleDisabled)) {
      transports_info.ble_status = BleStatus::kOff;
    } else if (base::Contains(test.params,
                              TransportAvailabilityParam::kBleAccessDenied)) {
      transports_info.ble_status = BleStatus::kPermissionDenied;
    } else {
      transports_info.ble_status = BleStatus::kOn;
    }
    transports_info.request_type = test.request_type;
    if (test.request_type == device::FidoRequestType::kMakeCredential) {
      transports_info.attestation_conveyance_preference =
          device::AttestationConveyancePreference::kNone;
    }
    transports_info.available_transports = test.transports;
    transports_info.user_verification_requirement =
        base::Contains(test.params, TransportAvailabilityParam::kUVRequired)
            ? device::UserVerificationRequirement::kRequired
            : (base::Contains(test.params,
                              TransportAvailabilityParam::kUVPreferred)
                   ? device::UserVerificationRequirement::kPreferred
                   : device::UserVerificationRequirement::kDiscouraged);

    if (base::Contains(test.params,
                       TransportAvailabilityParam::kHasPlatformCredential)) {
      transports_info.has_platform_authenticator_credential =
          device::FidoRequestHandlerBase::RecognizedCredential::
              kHasRecognizedCredential;
    } else if (base::Contains(
                   test.params,
                   TransportAvailabilityParam::kMaybeHasPlatformCredential)) {
      transports_info.has_platform_authenticator_credential =
          device::FidoRequestHandlerBase::RecognizedCredential::kUnknown;
    } else {
      transports_info.has_platform_authenticator_credential = device::
          FidoRequestHandlerBase::RecognizedCredential::kNoRecognizedCredential;
    }

    device::DiscoverableCredentialMetadata cred1;
    device::DiscoverableCredentialMetadata cred2;
    if (base::Contains(
            test.params,
            TransportAvailabilityParam::kHasWinNativeAuthenticator)) {
      cred1 = kWinCred1;
      cred2 = kWinCred2;
    } else {
      cred1 = kCred1;
      cred2 = kCred2;
    }
    device::DiscoverableCredentialMetadata touchid_cred1 = kTouchIDCred1;
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kHasICloudKeychainCreds)) {
      transports_info.has_icloud_keychain_credential =
          device::FidoRequestHandlerBase::RecognizedCredential::
              kHasRecognizedCredential;
      transports_info.recognized_credentials.emplace_back(
          kCred1FromICloudKeychain);
    } else {
      transports_info.has_icloud_keychain_credential = device::
          FidoRequestHandlerBase::RecognizedCredential::kNoRecognizedCredential;
    }

    if (base::Contains(test.params, TransportAvailabilityParam::kEnclaveCred)) {
      transports_info.recognized_credentials.emplace_back(kEnclaveCred1);
    }

    if (base::Contains(test.params,
                       TransportAvailabilityParam::kOneRecognizedCred)) {
      transports_info.recognized_credentials = {std::move(cred1)};
    } else if (base::Contains(
                   test.params,
                   TransportAvailabilityParam::kTwoRecognizedCreds)) {
      transports_info.recognized_credentials = {std::move(cred1),
                                                std::move(cred2)};
    } else if (base::Contains(
                   test.params,
                   TransportAvailabilityParam::kOneTouchIDRecognizedCred)) {
      transports_info.recognized_credentials = {std::move(touchid_cred1)};
    }

    transports_info.has_icloud_keychain = base::Contains(
        test.params, TransportAvailabilityParam::kHasICloudKeychain);
    transports_info.has_empty_allow_list = base::Contains(
        test.params, TransportAvailabilityParam::kEmptyAllowList);
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kOnlyInternal)) {
      transports_info.request_is_internal_only = true;
      transports_info.transport_list_did_include_hybrid = false;
      transports_info.transport_list_did_include_security_key = false;
    } else if (base::Contains(
                   test.params,
                   TransportAvailabilityParam::kOnlyHybridOrInternal)) {
      transports_info.is_only_hybrid_or_internal = true;
      transports_info.transport_list_did_include_hybrid = true;
      transports_info.transport_list_did_include_security_key = false;
    } else {
      transports_info.transport_list_did_include_hybrid = true;
      transports_info.transport_list_did_include_security_key = true;
    }
    transports_info.transport_list_did_include_internal = true;

    if (base::Contains(
            test.params,
            TransportAvailabilityParam::kHasWinNativeAuthenticator)) {
      transports_info.has_win_native_api_authenticator = true;
      transports_info.win_native_ui_shows_resident_credential_notice = true;
      transports_info.win_is_uvpaa = true;
    }
    transports_info.resident_key_requirement =
        base::Contains(test.params,
                       TransportAvailabilityParam::kRequireResidentKey)
            ? device::ResidentKeyRequirement::kRequired
            : device::ResidentKeyRequirement::kDiscouraged;
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kAttachmentAny)) {
      CHECK(transports_info.request_type == RequestType::kMakeCredential);
      transports_info.make_credential_attachment =
          device::AuthenticatorAttachment::kAny;
    }
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kAttachmentCrossPlatform)) {
      CHECK(transports_info.request_type == RequestType::kMakeCredential);
      CHECK(!transports_info.make_credential_attachment.has_value());
      transports_info.make_credential_attachment =
          device::AuthenticatorAttachment::kCrossPlatform;
    }
    if (!transports_info.make_credential_attachment.has_value() &&
        transports_info.request_type == RequestType::kMakeCredential) {
      transports_info.make_credential_attachment =
          device::AuthenticatorAttachment::kPlatform;
    }

    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    FakeEnclaveController enclave_controller(model.get());

    std::optional<bool> has_v2_cable_extension;
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kHasCableV1Extension)) {
      has_v2_cable_extension = false;
    }
    if (base::Contains(test.params, TransportAvailabilityParam::kEnclaveCred)) {
      model->EnclaveEnabledStatusChanged(EnclaveEnabledStatus::kEnabled);
    }

    if (base::Contains(test.params,
                       TransportAvailabilityParam::kHasCableV2Extension)) {
      CHECK(!has_v2_cable_extension.has_value());
      has_v2_cable_extension = true;
    }

    controller.set_allow_icloud_keychain(transports_info.has_icloud_keychain);
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kCreateInICloudKeychain)) {
      controller.set_should_create_in_icloud_keychain(true);
    }
#if BUILDFLAG(IS_MAC)
    transports_info.platform_has_biometrics =
        !base::Contains(test.params, TransportAvailabilityParam::kNoTouchId);
#endif

    std::optional<device::FidoTransportProtocol> hint_transport;
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kHintSecurityKeys)) {
      CHECK(!hint_transport.has_value());
      hint_transport = device::FidoTransportProtocol::kUsbHumanInterfaceDevice;
    }
    if (base::Contains(test.params, TransportAvailabilityParam::kHintHybrid)) {
      CHECK(!hint_transport.has_value());
      hint_transport = device::FidoTransportProtocol::kHybrid;
    }
    if (base::Contains(test.params,
                       TransportAvailabilityParam::kHintClientDevice)) {
      CHECK(!hint_transport.has_value());
      hint_transport = device::FidoTransportProtocol::kInternal;
    }

    if (hint_transport.has_value()) {
      content::AuthenticatorRequestClientDelegate::Hints hints;
      hints.transport = hint_transport;
      controller.SetHints(hints);
    }

    if (base::Contains(test.params,
                       TransportAvailabilityParam::kEnclaveNeedsSignIn)) {
      controller.EnclaveEnabledStatusChanged(
          EnclaveEnabledStatus::kEnabledAndReauthNeeded);
    }

    controller.SetAccountPreselectedCallback(base::BindRepeating(
        [](device::DiscoverableCredentialMetadata cred) {}));

    if (has_v2_cable_extension.has_value() ||
        base::Contains(test.transports,
                       device::FidoTransportProtocol::kHybrid)) {
      std::vector<std::unique_ptr<device::cablev2::Pairing>> phones;
      controller.set_cable_transport_info(has_v2_cable_extension,
                                          std::nullopt);
    }

    const bool is_autofill = base::Contains(
        test.params, TransportAvailabilityParam::kIsConditionalUI);
    controller.SetUIPresentation(is_autofill ? UIPresentation::kAutofill
                                             : UIPresentation::kModal);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
    if (is_autofill) {
      EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
      controller.TransitionToModalWebAuthnRequest();
    }

    EXPECT_EQ(test.expected_first_step, model->step());

    std::vector<AuthenticatorRequestDialogModel::Mechanism::Type>
        mechanism_types;
    for (const auto& mech : model->mechanisms) {
      mechanism_types.push_back(mech.type);
    }
    EXPECT_EQ(test.expected_mechanisms, mechanism_types);

    if (!model->offer_try_again_in_ui) {
      return;
    }

    model->StartOver();
    EXPECT_EQ(Step::kMechanismSelection, model->step());
  };

  for (const auto& test : kTests) {
    RunTest(test);
  }
}

#if BUILDFLAG(IS_WIN)
TEST_F(AuthenticatorRequestDialogControllerTest, WinCancel) {
  // Simulate the user canceling the Windows native UI, both with and without
  // that UI being immediately triggered. If it was immediately triggered then
  // canceling it should show the mechanism selection UI.

  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);

  for (const int win_webauthn_api_version : {4, 7}) {
    fake_win_webauthn_api.set_version(win_webauthn_api_version);
    for (const bool is_passkey_request : {false, true}) {
      SCOPED_TRACE(testing::Message() << "passkey req? " << is_passkey_request);
      SCOPED_TRACE(testing::Message() << "win v" << win_webauthn_api_version);

      TransportAvailabilityInfo tai;
      tai.make_credential_attachment =
          device::AuthenticatorAttachment::kCrossPlatform;
      tai.request_type = device::FidoRequestType::kMakeCredential;
      tai.attestation_conveyance_preference =
          device::AttestationConveyancePreference::kNone;
      tai.has_win_native_api_authenticator = true;
      tai.win_native_ui_shows_resident_credential_notice = true;
      tai.available_transports.insert(device::FidoTransportProtocol::kHybrid);
      tai.resident_key_requirement =
          is_passkey_request ? device::ResidentKeyRequirement::kRequired
                             : device::ResidentKeyRequirement::kDiscouraged;
      tai.ble_status = BleStatus::kOn;

      auto model =
          base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
      AuthenticatorRequestDialogController controller(model.get(), main_rfh());
      controller.saved_authenticators().AddAuthenticator(
          AuthenticatorReference("ID", AuthenticatorTransport::kInternal,
                                 device::AuthenticatorType::kWinNative));
      controller.set_cable_transport_info(std::nullopt, "fido:/1234");

      UpdateModelBeforeStartFlow(model.get(), tai);
      controller.StartFlow(std::move(tai), {});

      const bool win_ui_was_immediately_triggered =
          !is_passkey_request || win_webauthn_api_version == 7;
      if (!win_ui_was_immediately_triggered) {
        EXPECT_NE(model->step(), Step::kNotStarted);
        // Canceling the Windows UI ends the request because the user must have
        // selected the Windows option first.
        EXPECT_FALSE(controller.OnWinUserCancelled());
        continue;
      }

      EXPECT_EQ(model->step(), Step::kNotStarted);

      if (win_webauthn_api_version >= 7) {
        // Windows handles hybrid itself starting with this version, so
        // canceling shouldn't try to show Chrome UI.
        EXPECT_FALSE(controller.OnWinUserCancelled());
        continue;
      }

      // Canceling the Windows native UI should be handled.
      EXPECT_TRUE(controller.OnWinUserCancelled());
      // The mechanism selection sheet should now be showing.
      EXPECT_EQ(model->step(), Step::kMechanismSelection);
      // Canceling the Windows UI ends the request because the user must have
      // selected the Windows option first.
      EXPECT_FALSE(controller.OnWinUserCancelled());
    }
  }
}

// Simulate the user cancelling the Windows native UI after it was automatically
// dispatched to because a matching credential for Windows Hello was found for
// an allow-list request.
// Regression test for crbug.com/1479142.
TEST_F(AuthenticatorRequestDialogControllerTest,
       WinCancel_AfterMatchingLocalCred) {
  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);

  fake_win_webauthn_api.set_version(4);

  TransportAvailabilityInfo tai;
  tai.request_type = device::FidoRequestType::kGetAssertion;
  tai.has_win_native_api_authenticator = true;
  tai.has_empty_allow_list = false;
  tai.available_transports.insert(device::FidoTransportProtocol::kHybrid);
  tai.ble_status = BleStatus::kOn;
  tai.recognized_credentials = {kWinCred1};
  tai.has_platform_authenticator_credential = device::FidoRequestHandlerBase::
      RecognizedCredential::kHasRecognizedCredential;

  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  controller.saved_authenticators().AddAuthenticator(
      AuthenticatorReference("ID", AuthenticatorTransport::kInternal,
                             device::AuthenticatorType::kWinNative));
  controller.set_cable_transport_info(std::nullopt, "fido:/1234");
  UpdateModelBeforeStartFlow(model.get(), tai);
  controller.StartFlow(std::move(tai), {});

  // The Windows native UI should have been triggered.
  EXPECT_EQ(model->step(), Step::kNotStarted);

  // Canceling the Windows native UI should be handled.
  EXPECT_TRUE(controller.OnWinUserCancelled());

  // The mechanism selection sheet should now be showing.
  EXPECT_EQ(model->step(), Step::kMechanismSelection);

  // Canceling the Windows UI ends the request because the user must have
  // selected the Windows option first.
  EXPECT_FALSE(controller.OnWinUserCancelled());
}

TEST_F(AuthenticatorRequestDialogControllerTest, WinNoPlatformAuthenticator) {
  TransportAvailabilityInfo tai;
  tai.request_type = device::FidoRequestType::kMakeCredential;
  tai.attestation_conveyance_preference =
      device::AttestationConveyancePreference::kNone;
  tai.make_credential_attachment = device::AuthenticatorAttachment::kAny;
  tai.request_is_internal_only = true;
  tai.win_is_uvpaa = false;
  tai.has_win_native_api_authenticator = true;
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  UpdateModelBeforeStartFlow(model.get(), tai);
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  controller.StartFlow(std::move(tai), {});
  EXPECT_EQ(model->step(), Step::kErrorWindowsHelloNotEnabled);
  EXPECT_FALSE(model->offer_try_again_in_ui);
}
#endif

TEST_F(AuthenticatorRequestDialogControllerTest, NoAvailableTransports) {
  testing::StrictMock<MockDialogModelObserver> mock_observer;
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  model->observers.AddObserver(&mock_observer);

  EXPECT_CALL(mock_observer, OnStepTransition());
  TransportAvailabilityInfo transports_info;
  transports_info.attestation_conveyance_preference =
      device::AttestationConveyancePreference::kNone;
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  EXPECT_EQ(Step::kErrorNoAvailableTransports, model->step());
  testing::Mock::VerifyAndClearExpectations(&mock_observer);

  EXPECT_CALL(mock_observer, OnCancelRequest());
  model->CancelAuthenticatorRequest();
  testing::Mock::VerifyAndClearExpectations(&mock_observer);

  EXPECT_CALL(mock_observer, OnStepTransition());
  model->OnRequestComplete();
  EXPECT_EQ(Step::kClosed, model->step());
  testing::Mock::VerifyAndClearExpectations(&mock_observer);

  EXPECT_CALL(mock_observer, OnModelDestroyed(model.get()));
}

TEST_F(AuthenticatorRequestDialogControllerTest, Cable2ndFactorFlows) {
#if BUILDFLAG(IS_WIN)
  // TODO(crbug.com/41490900): Get test to pass in the webauthn supports
  // hybrid case.
  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);
  fake_win_webauthn_api.set_version(4);
#endif  // BUILDFLAG(IS_WIN)

  enum class Profile {
    NORMAL,
    INCOGNITO,
  };

  const auto mc = RequestType::kMakeCredential;
  const auto ga = RequestType::kGetAssertion;
  const auto on_ = BleStatus::kOn;
  const auto off = BleStatus::kOff;
  const auto normal = Profile::NORMAL;
  const auto otr___ = Profile::INCOGNITO;
  const auto mss = Step::kMechanismSelection;
  const auto qr = Step::kCableV2QRCode;
  [[maybe_unused]] const auto interstitial = Step::kOffTheRecordInterstitial;
  const auto power = Step::kBlePowerOnAutomatic;

  const struct {
    RequestType request_type;
    BleStatus ble_power;
    Profile profile;
    std::vector<Step> steps;
  } kTests[] = {
      //               | Expected UI steps in order.
      {mc, on_, normal, {qr}},
      // TODO(crbug.com/424448497): this should show the interstitial.
      {mc, on_, otr___, {qr}},
      {mc, off, normal, {mss, power, qr}},
      // TODO(crbug.com/424448497): this should show the interstitial.
      {mc, off, otr___, {mss, power, qr}},
      {ga, on_, normal, {qr}},
      {ga, on_, otr___, {qr}},
      {ga, off, normal, {mss, power, qr}},
      {ga, off, otr___, {mss, power, qr}},
  };

  unsigned test_num = 0;
  for (const auto& test : kTests) {
    SCOPED_TRACE(test_num++);

    TransportAvailabilityInfo transports_info;
    transports_info.ble_status = test.ble_power;
    transports_info.can_power_on_ble_adapter = true;
    transports_info.request_type = test.request_type;
    if (transports_info.request_type == RequestType::kMakeCredential) {
      transports_info.make_credential_attachment =
          device::AuthenticatorAttachment::kAny;
      transports_info.attestation_conveyance_preference =
          device::AttestationConveyancePreference::kNone;
    }
    transports_info.available_transports = {
        AuthenticatorTransport::kHybrid,
        device::FidoTransportProtocol::kUsbHumanInterfaceDevice};
    transports_info.is_off_the_record_context =
        test.profile == Profile::INCOGNITO;

    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());

    controller.set_cable_transport_info(
        /*extension_is_v2=*/std::nullopt, std::nullopt);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});

    for (const auto step : test.steps) {
      ASSERT_EQ(step, model->step())
          << static_cast<int>(step)
          << " != " << static_cast<int>(model->step());

      switch (step) {
        case Step::kBlePowerOnAutomatic:
          controller.BluetoothAdapterStatusChanged(BleStatus::kOn);
          break;

        case Step::kOffTheRecordInterstitial:
          model->OnOffTheRecordInterstitialAccepted();
          break;

        case Step::kCableV2QRCode:
          break;

        case Step::kMechanismSelection:
          std::ranges::find_if(model->mechanisms, [](const auto& m) -> bool {
            return std::holds_alternative<
                AuthenticatorRequestDialogModel::Mechanism::AddPhone>(m.type);
          })->callback.Run();
          break;

        default:
          NOTREACHED();
      }
    }
  }
}

TEST_F(AuthenticatorRequestDialogControllerTest, AwaitingAcknowledgement) {
  const struct {
    void (AuthenticatorRequestDialogController::*event)();
    Step expected_sheet;
  } kTestCases[] = {
      {&AuthenticatorRequestDialogController::OnRequestTimeout,
       Step::kTimedOut},
      {&AuthenticatorRequestDialogController::OnActivatedKeyNotRegistered,
       Step::kKeyNotRegistered},
      {&AuthenticatorRequestDialogController::OnActivatedKeyAlreadyRegistered,
       Step::kKeyAlreadyRegistered},
  };

  for (const auto& test_case : kTestCases) {
    testing::StrictMock<MockDialogModelObserver> mock_observer;
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    model->observers.AddObserver(&mock_observer);

    TransportAvailabilityInfo transports_info;
    transports_info.request_type = RequestType::kMakeCredential;
    transports_info.attestation_conveyance_preference =
        device::AttestationConveyancePreference::kNone;
    transports_info.make_credential_attachment =
        device::AuthenticatorAttachment::kAny;
    transports_info.available_transports = kAllTransportsWithoutCable;

    EXPECT_CALL(mock_observer, OnStepTransition());
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
#if BUILDFLAG(IS_MAC)
    EXPECT_EQ(Step::kCreatePasskey, model->step());
#else
    EXPECT_EQ(Step::kMechanismSelection, model->step());
#endif
    testing::Mock::VerifyAndClearExpectations(&mock_observer);

    EXPECT_CALL(mock_observer, OnStepTransition());
    (controller.*test_case.event)();
    EXPECT_EQ(test_case.expected_sheet, model->step());
    testing::Mock::VerifyAndClearExpectations(&mock_observer);

    EXPECT_CALL(mock_observer, OnStepTransition());
    EXPECT_CALL(mock_observer, OnCancelRequest());
    controller.CancelAuthenticatorRequest();
    EXPECT_EQ(Step::kClosed, model->step());
    testing::Mock::VerifyAndClearExpectations(&mock_observer);

    EXPECT_CALL(mock_observer, OnModelDestroyed(model.get()));
  }
}

TEST_F(AuthenticatorRequestDialogControllerTest, BleAdapterAlreadyPowered) {
  const struct {
    AuthenticatorTransport transport;
    Step expected_final_step;
  } kTestCases[] = {
      {AuthenticatorTransport::kHybrid, Step::kCableActivate},
  };

  for (const auto test_case : kTestCases) {
    TransportAvailabilityInfo transports_info;
    transports_info.request_type = RequestType::kGetAssertion;
    transports_info.available_transports = {test_case.transport};
    transports_info.can_power_on_ble_adapter = true;
    transports_info.ble_status = BleStatus::kOn;

    BluetoothAdapterPowerOnCallbackReceiver power_receiver;
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    controller.SetBluetoothAdapterPowerOnCallback(power_receiver.GetCallback());
    controller.set_cable_transport_info(true, std::nullopt);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
    EXPECT_EQ(test_case.expected_final_step, model->step());
    EXPECT_TRUE(model->ble_adapter_is_powered);
    EXPECT_FALSE(power_receiver.was_called());
  }
}

TEST_F(AuthenticatorRequestDialogControllerTest,
       BleAdapterNeedToBeManuallyPowered) {
  const struct {
    AuthenticatorTransport transport;
    Step expected_final_step;
  } kTestCases[] = {
      {AuthenticatorTransport::kHybrid, Step::kCableActivate},
  };

  for (const auto test_case : kTestCases) {
    TransportAvailabilityInfo transports_info;
    transports_info.request_type = RequestType::kGetAssertion;
    transports_info.available_transports = {test_case.transport};
    transports_info.can_power_on_ble_adapter = false;
    transports_info.ble_status = BleStatus::kOff;

    testing::NiceMock<MockDialogModelObserver> mock_observer;
    BluetoothAdapterPowerOnCallbackReceiver power_receiver;
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    model->observers.AddObserver(&mock_observer);
    controller.SetBluetoothAdapterPowerOnCallback(power_receiver.GetCallback());
    controller.set_cable_transport_info(true, std::nullopt);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});

    EXPECT_EQ(Step::kBlePowerOnManual, model->step());
    EXPECT_FALSE(model->ble_adapter_is_powered);

    EXPECT_CALL(mock_observer, OnBluetoothPoweredStateChanged());
    controller.BluetoothAdapterStatusChanged(BleStatus::kOn);

    EXPECT_EQ(Step::kBlePowerOnManual, model->step());
    EXPECT_TRUE(model->ble_adapter_is_powered);
    testing::Mock::VerifyAndClearExpectations(&mock_observer);

    controller.ContinueWithFlowAfterBleAdapterPowered();

    EXPECT_EQ(test_case.expected_final_step, model->step());
    EXPECT_FALSE(power_receiver.was_called());
  }
}

TEST_F(AuthenticatorRequestDialogControllerTest,
       BleAdapterCanBeAutomaticallyPowered) {
  const struct {
    AuthenticatorTransport transport;
    Step expected_final_step;
  } kTestCases[] = {
      {AuthenticatorTransport::kHybrid, Step::kCableActivate},
  };

  for (const auto test_case : kTestCases) {
    TransportAvailabilityInfo transports_info;
    transports_info.request_type = RequestType::kGetAssertion;
    transports_info.available_transports = {test_case.transport};
    transports_info.can_power_on_ble_adapter = true;
    transports_info.ble_status = BleStatus::kOff;

    BluetoothAdapterPowerOnCallbackReceiver power_receiver;
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    controller.SetBluetoothAdapterPowerOnCallback(power_receiver.GetCallback());
    controller.set_cable_transport_info(true, std::nullopt);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});

    EXPECT_EQ(Step::kBlePowerOnAutomatic, model->step());

    controller.PowerOnBleAdapter();

    EXPECT_EQ(Step::kBlePowerOnAutomatic, model->step());
    EXPECT_TRUE(power_receiver.was_called());
    EXPECT_FALSE(model->ble_adapter_is_powered);

    controller.BluetoothAdapterStatusChanged(BleStatus::kOn);

    EXPECT_EQ(test_case.expected_final_step, model->step());
    EXPECT_TRUE(model->ble_adapter_is_powered);
  }
}

// Tests that Chrome will request Bluetooth permissions before attempting to
// power the adapter on if the adapter reports the status as pending permission.
TEST_F(AuthenticatorRequestDialogControllerTest, BleAdapterPendingPermission) {
  for (BleStatus ble_status :
       {BleStatus::kOn, BleStatus::kOff, BleStatus::kPermissionDenied}) {
    SCOPED_TRACE(testing::Message() << static_cast<int>(ble_status));
    TransportAvailabilityInfo transports_info;
    transports_info.request_type = RequestType::kGetAssertion;
    transports_info.available_transports = {
        device::FidoTransportProtocol::kHybrid};
    transports_info.can_power_on_ble_adapter = true;
    transports_info.ble_status = BleStatus::kPendingPermissionRequest;

    RepeatingValueCallbackReceiver<
        device::FidoRequestHandlerBase::BlePermissionCallback>
        request_ble_permission_callback_receiver;
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    controller.SetRequestBlePermissionCallback(
        request_ble_permission_callback_receiver.Callback());
    controller.set_cable_transport_info(true, std::nullopt);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});

    device::FidoRequestHandlerBase::BlePermissionCallback
        ble_permission_callback =
            request_ble_permission_callback_receiver.WaitForResult();
    ASSERT_TRUE(ble_permission_callback);
    std::move(ble_permission_callback).Run(ble_status);

    if (ble_status == BleStatus::kOn) {
      EXPECT_TRUE(model->ble_adapter_is_powered);
      EXPECT_EQ(model->step(), Step::kCableActivate);
    } else if (ble_status == BleStatus::kOff) {
      EXPECT_FALSE(model->ble_adapter_is_powered);
      EXPECT_EQ(model->step(), Step::kBlePowerOnAutomatic);
    } else {
      EXPECT_FALSE(model->ble_adapter_is_powered);
      EXPECT_EQ(model->step(), Step::kBlePermissionMac);
    }
  }
}

TEST_F(AuthenticatorRequestDialogControllerTest,
       ConditionalUINoRecognizedCredential) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());

  int preselect_num_called = 0;
  controller.SetAccountPreselectedCallback(base::BindRepeating(
      [](int* i, device::DiscoverableCredentialMetadata cred) {
        EXPECT_EQ(cred.cred_id, std::vector<uint8_t>({1, 2, 3, 4}));
        ++(*i);
      },
      &preselect_num_called));
  int request_num_called = 0;
  controller.SetRequestCallback(base::BindRepeating(
      [](int* i, const std::string& authenticator_id) { ++(*i); },
      &request_num_called));
  controller.saved_authenticators().AddAuthenticator(
      AuthenticatorReference(/*device_id=*/"authenticator",
                             AuthenticatorTransport::kUsbHumanInterfaceDevice,
                             device::AuthenticatorType::kOther));
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"authenticator", AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));

  TransportAvailabilityInfo transports_info;
  transports_info.attestation_conveyance_preference =
      device::AttestationConveyancePreference::kNone;
  transports_info.available_transports = kAllTransports;
  transports_info.has_platform_authenticator_credential = device::
      FidoRequestHandlerBase::RecognizedCredential::kHasRecognizedCredential;
  controller.SetUIPresentation(UIPresentation::kAutofill);
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  task_environment()->RunUntilIdle();
  EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
  EXPECT_TRUE(model->should_dialog_be_closed());
  EXPECT_EQ(preselect_num_called, 0);
  EXPECT_EQ(request_num_called, 0);
}

TEST_F(AuthenticatorRequestDialogControllerTest,
       ConditionalUIRecognizedCredential) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  int preselect_num_called = 0;
  controller.SetAccountPreselectedCallback(base::BindRepeating(
      [](int* i, device::DiscoverableCredentialMetadata cred) {
        EXPECT_EQ(cred.cred_id, std::vector<uint8_t>({0}));
        ++(*i);
      },
      &preselect_num_called));
  int request_num_called = 0;
  controller.SetRequestCallback(base::BindRepeating(
      [](int* i, const std::string& authenticator_id) {
        EXPECT_EQ(authenticator_id, "internal");
        ++(*i);
      },
      &request_num_called));
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"usb", AuthenticatorTransport::kUsbHumanInterfaceDevice,
      device::AuthenticatorType::kOther));
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"internal", AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));

  TransportAvailabilityInfo transports_info;
  transports_info.attestation_conveyance_preference =
      device::AttestationConveyancePreference::kNone;
  transports_info.available_transports = kAllTransports;
  transports_info.has_platform_authenticator_credential = device::
      FidoRequestHandlerBase::RecognizedCredential::kHasRecognizedCredential;
  transports_info.recognized_credentials = {kCred1, kCred2};
  controller.SetUIPresentation(UIPresentation::kAutofill);
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
  EXPECT_TRUE(model->should_dialog_be_closed());
  EXPECT_EQ(request_num_called, 0);

  // After preselecting an account, the request should be dispatched to the
  // platform authenticator.
  controller.OnAccountPreselected(kCred1.cred_id);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(preselect_num_called, 1);
  EXPECT_EQ(request_num_called, 1);
}

// Tests that cancelling a Conditional UI request that has completed restarts
// it.
TEST_F(AuthenticatorRequestDialogControllerTest, ConditionalUICancelRequest) {
  testing::StrictMock<MockDialogModelObserver> mock_observer;
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  model->observers.AddObserver(&mock_observer);
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"internal", AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));

  EXPECT_CALL(mock_observer, OnStepTransition());
  TransportAvailabilityInfo transports_info;
  transports_info.attestation_conveyance_preference =
      device::AttestationConveyancePreference::kNone;
  controller.SetUIPresentation(UIPresentation::kAutofill);
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
  testing::Mock::VerifyAndClearExpectations(&mock_observer);

  // Cancel an ongoing request (as if e.g. the user clicked the accept button).
  // The request should be restarted.
  EXPECT_CALL(mock_observer, OnStartOver());
  EXPECT_CALL(mock_observer, OnStepTransition()).Times(2);
  controller.SetCurrentStepForTesting(Step::kKeyAlreadyRegistered);
  controller.CancelAuthenticatorRequest();
  EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
  testing::Mock::VerifyAndClearExpectations(&mock_observer);
  model->observers.RemoveObserver(&mock_observer);
}

#if BUILDFLAG(IS_WIN)
// Tests that cancelling the Windows Platform authenticator during a Conditional
// UI request restarts it.
TEST_F(AuthenticatorRequestDialogControllerTest, ConditionalUIWindowsCancel) {
  testing::StrictMock<MockDialogModelObserver> mock_observer;
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  model->observers.AddObserver(&mock_observer);
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"internal", AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));

  EXPECT_CALL(mock_observer, OnStepTransition());
  TransportAvailabilityInfo transports_info;
  transports_info.attestation_conveyance_preference =
      device::AttestationConveyancePreference::kNone;
  controller.SetUIPresentation(UIPresentation::kAutofill);
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
  testing::Mock::VerifyAndClearExpectations(&mock_observer);

  // Simulate the Windows authenticator cancelling.
  EXPECT_CALL(mock_observer, OnStepTransition());
  EXPECT_CALL(mock_observer, OnStartOver());
  controller.OnWinUserCancelled();
  EXPECT_EQ(model->step(), Step::kPasskeyAutofill);
  testing::Mock::VerifyAndClearExpectations(&mock_observer);
  model->observers.RemoveObserver(&mock_observer);
}
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_MAC)
// Tests that a transport = internal virtual authenticator can be dispatched to
// on Mac.
// Regression test for crbug.com/1520898.
TEST_F(AuthenticatorRequestDialogControllerTest, PlatformVirtualAuthenticator) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"virtual-authenticator", AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));
  controller.SetAccountPreselectedCallback(base::DoNothing());
  base::RunLoop run_loop;
  controller.SetRequestCallback(
      base::BindLambdaForTesting([&](const std::string& authenticator_id) {
        EXPECT_EQ(authenticator_id, "virtual-authenticator");
        run_loop.Quit();
      }));
  TransportAvailabilityInfo transports_info;
  transports_info.user_verification_requirement =
      device::UserVerificationRequirement::kRequired;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.has_empty_allow_list = false;
  transports_info.recognized_credentials = {kCred2};
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  run_loop.Run();
}
#endif  // BUILDFLAG(IS_MAC)

TEST_F(AuthenticatorRequestDialogControllerTest, PreSelect) {
  for (const bool has_empty_allow_list : {false, true}) {
    SCOPED_TRACE(::testing::Message()
                 << "has_empty_allow_list=" << has_empty_allow_list);

    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    int preselect_num_called = 0;
    controller.SetAccountPreselectedCallback(base::BindLambdaForTesting(
        [&preselect_num_called](device::DiscoverableCredentialMetadata cred) {
          EXPECT_EQ(cred.cred_id, std::vector<uint8_t>({1}));
          ++preselect_num_called;
        }));
    int request_num_called = 0;
    controller.SetRequestCallback(base::BindLambdaForTesting(
        [&request_num_called](const std::string& authenticator_id) {
          EXPECT_EQ(authenticator_id, "internal-authenticator");
          ++request_num_called;
        }));

    controller.saved_authenticators().AddAuthenticator(
        AuthenticatorReference(/*device_id=*/"usb-authenticator",
                               AuthenticatorTransport::kUsbHumanInterfaceDevice,
                               device::AuthenticatorType::kOther));
    controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
        /*device_id=*/"internal-authenticator",
        AuthenticatorTransport::kInternal, device::AuthenticatorType::kOther));

    TransportAvailabilityInfo transports_info;
    transports_info.request_type = device::FidoRequestType::kGetAssertion;
    transports_info.available_transports = kAllTransports;
    transports_info.has_empty_allow_list = has_empty_allow_list;
    transports_info.user_verification_requirement =
        device::UserVerificationRequirement::kPreferred;
    transports_info.has_platform_authenticator_credential = device::
        FidoRequestHandlerBase::RecognizedCredential::kHasRecognizedCredential;
    transports_info.recognized_credentials = {kCred1FromICloudKeychain, kCred2};
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});

    if (has_empty_allow_list) {
      EXPECT_EQ(model->step(), Step::kSelectPriorityMechanism);
    } else {
      EXPECT_EQ(model->step(), Step::kPreSelectAccount);
    }
    task_environment()->RunUntilIdle();

    if (has_empty_allow_list) {
      EXPECT_EQ(preselect_num_called, 0);
      EXPECT_EQ(request_num_called, 0);
      // After preselecting an account, the request should be dispatched to the
      // platform authenticator.
      controller.OnAccountPreselected(kCred2.cred_id);
      task_environment()->RunUntilIdle();
      EXPECT_EQ(preselect_num_called, 1);
      EXPECT_EQ(request_num_called, 1);
    } else {
      EXPECT_EQ(request_num_called, 0);
      ASSERT_EQ(model->creds.size(), 1u);
      // `kCred1FromICloudKeychain` is an iCloud Keychain credential so,
      // even though it's in `recognized_credentials`, it shouldn't have been
      // used by the standard platform authenticator code.
      EXPECT_EQ(model->creds[0].cred_id, std::vector<uint8_t>({1}));
    }
  }
}

#if BUILDFLAG(IS_WIN)
// Regression test for crbug.com/1476884.
TEST_F(AuthenticatorRequestDialogControllerTest, JumpToWindowsWithNewUI) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());

  TransportAvailabilityInfo transports_info;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.available_transports = kAllTransports;
  transports_info.has_win_native_api_authenticator = true;
  transports_info.has_empty_allow_list = false;
  transports_info.has_platform_authenticator_credential = device::
      FidoRequestHandlerBase::RecognizedCredential::kHasRecognizedCredential;
  transports_info.recognized_credentials = {kWinCred1, kWinCred2};

  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      /*device_id=*/"win", AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kWinNative));

  RequestCallbackReceiver request_callback;
  controller.SetRequestCallback(request_callback.Callback());
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
  EXPECT_EQ(request_callback.WaitForResult(), "win");
}
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_MAC)
TEST_F(AuthenticatorRequestDialogControllerTest, BluetoothPermissionPrompt) {
  // When BLE permission is denied on macOS, we should jump to the sheet that
  // explains that if the user tries to use a linked phone or tries to show the
  // QR code.
  for (const BleStatus ble_status :
       {BleStatus::kOn, BleStatus::kPermissionDenied}) {
      SCOPED_TRACE(::testing::Message()
                   << "ble_status=" << static_cast<int>(ble_status));

      auto model =
          base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
      AuthenticatorRequestDialogController controller(model.get(), main_rfh());
      controller.set_cable_transport_info(/*extension_is_v2=*/std::nullopt,
                                          std::nullopt);
      TransportAvailabilityInfo transports_info;
      transports_info.ble_status = ble_status;
      transports_info.request_type = device::FidoRequestType::kGetAssertion;
      transports_info.available_transports = {
          AuthenticatorTransport::kHybrid,
          AuthenticatorTransport::kUsbHumanInterfaceDevice};
      UpdateModelBeforeStartFlow(model.get(), transports_info);
      controller.StartFlow(std::move(transports_info), {});

      std::ranges::find_if(model->mechanisms, [](const auto& m) -> bool {
        return std::holds_alternative<
            AuthenticatorRequestDialogModel::Mechanism::AddPhone>(m.type);
      })->callback.Run();

      if (ble_status == BleStatus::kPermissionDenied) {
        EXPECT_EQ(model->step(), Step::kBlePermissionMac);
      } else {
        EXPECT_EQ(model->step(), Step::kCableV2QRCode);
      }
  }
}
#endif

TEST_F(AuthenticatorRequestDialogControllerTest, AdvanceThroughCableV2States) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  controller.set_cable_transport_info(/*extension_is_v2=*/std::nullopt,
                                      std::nullopt);
  TransportAvailabilityInfo transports_info;
  transports_info.ble_status = BleStatus::kOn;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.available_transports = {AuthenticatorTransport::kHybrid};
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});

  controller.OnCableEvent(device::cablev2::Event::kPhoneConnected);
  EXPECT_EQ(model->step(), Step::kCableV2Connecting);
  controller.OnCableEvent(device::cablev2::Event::kBLEAdvertReceived);
  EXPECT_EQ(model->step(), Step::kCableV2Connecting);
  controller.OnCableEvent(device::cablev2::Event::kReady);
  // kCableV2Connecting won't flash by too quickly, so it'll still be showing.
  EXPECT_EQ(model->step(), Step::kCableV2Connecting);

  task_environment()->FastForwardBy(base::Seconds(2));
  EXPECT_EQ(model->step(), Step::kCableV2Connected);
}

TEST_F(AuthenticatorRequestDialogControllerTest,
       AdvanceThroughCableV2StatesStopTimer) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  controller.set_cable_transport_info(/*extension_is_v2=*/std::nullopt,
                                      std::nullopt);
  TransportAvailabilityInfo transports_info;
  transports_info.ble_status = BleStatus::kOn;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.available_transports = {AuthenticatorTransport::kHybrid};
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});

  controller.OnCableEvent(device::cablev2::Event::kPhoneConnected);
  EXPECT_EQ(model->step(), Step::kCableV2Connecting);
  controller.OnCableEvent(device::cablev2::Event::kBLEAdvertReceived);
  EXPECT_EQ(model->step(), Step::kCableV2Connecting);
  controller.OnCableEvent(device::cablev2::Event::kReady);
  // kCableV2Connecting won't flash by too quickly, so it'll still be showing.
  EXPECT_EQ(model->step(), Step::kCableV2Connecting);

  // Moving to a different step should stop the timer so that kCableV2Connected
  // never shows.
  controller.SetCurrentStepForTesting(Step::kCableActivate);

  task_environment()->FastForwardBy(base::Seconds(10));
  EXPECT_EQ(model->step(), Step::kCableActivate);
}

TEST_F(AuthenticatorRequestDialogControllerTest, Crbug1503187) {
  // This test reproduces the crash from crbug.com/1503187.
  TransportAvailabilityInfo transports_info;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.available_transports = {
      AuthenticatorTransport::kInternal,
      AuthenticatorTransport::kUsbHumanInterfaceDevice};
  transports_info.recognized_credentials = {kCred1FromChromeOS};
  transports_info.has_empty_allow_list = false;
  transports_info.has_platform_authenticator_credential = device::
      FidoRequestHandlerBase::RecognizedCredential::kHasRecognizedCredential;

  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  RepeatingValueCallbackReceiver<device::DiscoverableCredentialMetadata>
      account_preselected_callback;
  controller.SetAccountPreselectedCallback(
      account_preselected_callback.Callback());
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});
}

TEST_F(AuthenticatorRequestDialogControllerTest, DeduplicateAccounts) {
  using Mechanism = AuthenticatorRequestDialogModel::Mechanism;
  const struct {
    std::vector<device::DiscoverableCredentialMetadata> recognized_credentials;
    std::optional<Mechanism::Type> type_of_priority_mechanism;
  } kTests[] = {
      {{kCred1, kCred2}, std::nullopt},
      {{kCred1, kCred1FromICloudKeychain},
       Mechanism::Credential(CredentialInfoFrom(kCred1FromICloudKeychain))},
      {{kCred1FromICloudKeychain, kCred1},
       Mechanism::Credential(CredentialInfoFrom(kCred1FromICloudKeychain))},
  };

  for (const auto& test : kTests) {
    TransportAvailabilityInfo transports_info;
    transports_info.request_type = device::FidoRequestType::kGetAssertion;
    transports_info.available_transports = {AuthenticatorTransport::kInternal};
    transports_info.recognized_credentials = test.recognized_credentials;
    transports_info.has_empty_allow_list = true;

    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    controller.set_allow_icloud_keychain(true);
    RepeatingValueCallbackReceiver<device::DiscoverableCredentialMetadata>
        account_preselected_callback;
    controller.SetAccountPreselectedCallback(
        account_preselected_callback.Callback());
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
    ASSERT_EQ(model->priority_mechanism_index.has_value(),
              test.type_of_priority_mechanism.has_value());
    if (!test.type_of_priority_mechanism.has_value()) {
      continue;
    }

    EXPECT_EQ(*test.type_of_priority_mechanism,
              model->mechanisms[*model->priority_mechanism_index].type);
  }
}

// Tests the text on the hybrid button label.
// Regression test for crbug.com/328698086.
TEST_F(AuthenticatorRequestDialogControllerTest, HybridButtonLabel) {
#if BUILDFLAG(IS_WIN)
  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);
#endif
  constexpr int kPhoneOrSk =
      IDS_WEBAUTHN_PASSKEY_PHONE_TABLET_OR_SECURITY_KEY_LABEL;
  constexpr int kPhone = IDS_WEBAUTHN_PASSKEY_PHONE_OR_TABLET_LABEL;
  const auto usb = AuthenticatorTransport::kUsbHumanInterfaceDevice;
  const auto hybrid = AuthenticatorTransport::kHybrid;
  enum CanDoUSB : bool {
    kNoUSB = false,
    kUSB = true,
  };
  struct TestCase {
    CanDoUSB chrome_can_do_usb;
    BleStatus ble_status;
    std::optional<AuthenticatorTransport> transport_hint;
    int expected;
  } kTestCases[] = {
      {kUSB, BleStatus::kOn, std::nullopt, kPhoneOrSk},
      {kUSB, BleStatus::kOn, usb, kPhone},
      {kUSB, BleStatus::kOn, hybrid, kPhone},
      {kUSB, BleStatus::kOff, std::nullopt, kPhone},
      {kNoUSB, BleStatus::kOn, std::nullopt, kPhone},
  };
  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.transport_hint
                     ? static_cast<int>(*test_case.transport_hint)
                     : -1);
    SCOPED_TRACE(static_cast<int>(test_case.ble_status));
    SCOPED_TRACE(static_cast<int>(test_case.chrome_can_do_usb));
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    TransportAvailabilityInfo transports_info;
    transports_info.request_type = device::FidoRequestType::kMakeCredential;
    transports_info.attestation_conveyance_preference =
        device::AttestationConveyancePreference::kNone;
    controller.set_cable_transport_info(
        /*extension_is_v2=*/std::nullopt, std::nullopt);
    if (test_case.chrome_can_do_usb) {
      transports_info.available_transports = {
          device::FidoTransportProtocol::kHybrid,
          device::FidoTransportProtocol::kUsbHumanInterfaceDevice};
    } else {
      transports_info.available_transports = {
          device::FidoTransportProtocol::kHybrid};
    }
    transports_info.ble_status = test_case.ble_status;
    content::AuthenticatorRequestClientDelegate::Hints hints;
    hints.transport = test_case.transport_hint;
    controller.SetHints(std::move(hints));
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
    auto hybrid_button_it =
        std::ranges::find_if(model->mechanisms, [](const auto& m) {
          return std::holds_alternative<
              AuthenticatorRequestDialogModel::Mechanism::AddPhone>(m.type);
        });
    ASSERT_NE(hybrid_button_it, model->mechanisms.end());
    EXPECT_EQ(hybrid_button_it->name,
              l10n_util::GetStringUTF16(test_case.expected));
  }
}

#if BUILDFLAG(IS_MAC)

TEST_F(AuthenticatorRequestDialogControllerTest, Dispatch) {
  for (const bool should_create_in_icloud_keychain : {false, true}) {
    for (const bool platform_attachment : {false, true}) {
      if (!platform_attachment && should_create_in_icloud_keychain) {
        // Without `platform_attachment`, `should_create_in_icloud_keychain` is
        // moot.
        continue;
      }

      SCOPED_TRACE(testing::Message() << "should_create_in_icloud_keychain: "
                                      << should_create_in_icloud_keychain);
      SCOPED_TRACE(testing::Message()
                   << "platform_attachment: " << platform_attachment);

      TransportAvailabilityInfo transports_info;
      transports_info.attestation_conveyance_preference =
          device::AttestationConveyancePreference::kNone;
      transports_info.has_icloud_keychain = true;
      transports_info.available_transports = {
          AuthenticatorTransport::kInternal,
          AuthenticatorTransport::kUsbHumanInterfaceDevice};
      transports_info.request_type = device::FidoRequestType::kMakeCredential;
      transports_info.resident_key_requirement =
          device::ResidentKeyRequirement::kRequired;
      transports_info.make_credential_attachment =
          platform_attachment ? device::AuthenticatorAttachment::kPlatform
                              : device::AuthenticatorAttachment::kAny;

      auto model =
          base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
      AuthenticatorRequestDialogController controller(model.get(), main_rfh());
      controller.set_allow_icloud_keychain(true);
      controller.set_should_create_in_icloud_keychain(
          should_create_in_icloud_keychain);

      RequestCallbackReceiver request_callback;
      controller.SetRequestCallback(request_callback.Callback());

      const std::string kProfileAuthenticatorId = "platauth";
      controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
          kProfileAuthenticatorId, AuthenticatorTransport::kInternal,
          device::AuthenticatorType::kTouchID));
      const std::string kICloudKeychainId = "ickc";
      controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
          kICloudKeychainId, AuthenticatorTransport::kInternal,
          device::AuthenticatorType::kICloudKeychain));
      UpdateModelBeforeStartFlow(model.get(), transports_info);
      controller.StartFlow(std::move(transports_info), {});
      if (should_create_in_icloud_keychain) {
        EXPECT_EQ(request_callback.WaitForResult(), kICloudKeychainId);
      } else {
        EXPECT_EQ(model->step(), Step::kCreatePasskey);
        controller.HideDialogAndDispatchToPlatformAuthenticator();
        EXPECT_EQ(request_callback.WaitForResult(), kProfileAuthenticatorId);
      }

      controller.OnUserConsentDenied();

      EXPECT_EQ(model->step(), should_create_in_icloud_keychain
                                   ? Step::kMechanismSelection
                                   : Step::kErrorInternalUnrecognized);

      controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
          kProfileAuthenticatorId, AuthenticatorTransport::kInternal,
          device::AuthenticatorType::kTouchID));
      controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
          kICloudKeychainId, AuthenticatorTransport::kInternal,
          device::AuthenticatorType::kICloudKeychain));

      // Dispatch and cancel again to confirm that canceling the non-automatic
      // dispatch cancels the whole request.
      controller.HideDialogAndDispatchToPlatformAuthenticator(
          device::AuthenticatorType::kICloudKeychain);
      controller.OnUserConsentDenied();

      EXPECT_EQ(model->step(), Step::kNotStarted);
    }
  }
}

TEST_F(AuthenticatorRequestDialogControllerTest,
       OnlyShowConfirmationSheetForProfileAuthenticator) {
  for (const auto credential_source :
       {device::AuthenticatorType::kTouchID,
        device::AuthenticatorType::kICloudKeychain}) {
    SCOPED_TRACE(static_cast<int>(credential_source));

    TransportAvailabilityInfo transports_info;
    transports_info.has_icloud_keychain = true;
    transports_info.available_transports = {AuthenticatorTransport::kInternal};
    transports_info.request_type = device::FidoRequestType::kGetAssertion;
    transports_info.has_empty_allow_list = false;

    if (credential_source == device::AuthenticatorType::kTouchID) {
      transports_info.recognized_credentials = {kCred2};
      transports_info.has_platform_authenticator_credential =
          device::FidoRequestHandlerBase::RecognizedCredential::
              kHasRecognizedCredential;
    } else {
      transports_info.recognized_credentials = {kCred1FromICloudKeychain};
      transports_info.has_icloud_keychain_credential =
          device::FidoRequestHandlerBase::RecognizedCredential::
              kHasRecognizedCredential;
    }

    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    controller.set_allow_icloud_keychain(true);
    RepeatingValueCallbackReceiver<device::DiscoverableCredentialMetadata>
        account_preselected_callback;
    controller.SetAccountPreselectedCallback(
        account_preselected_callback.Callback());
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});

    EXPECT_EQ(model->step(), Step::kNotStarted);
    device::DiscoverableCredentialMetadata descriptor =
        account_preselected_callback.WaitForResult();
    if (credential_source == device::AuthenticatorType::kTouchID) {
      EXPECT_EQ(descriptor.cred_id, kCred2.cred_id);
    } else {
      EXPECT_EQ(descriptor.cred_id, kCred1FromICloudKeychain.cred_id);
    }
  }
}

#endif

// Tests that iCloud Keychain passkeys are listed in conditional UI with the
// correct label.
// Regression test for crbug.com/409806800.
TEST_F(AuthenticatorRequestDialogControllerTest,
       ListICloudKeychainPasskeysInConditionalUI) {
  NavigateAndCommit(GURL("rp.com"));

  ChromeWebAuthnCredentialsDelegate* delegate =
      ChromeWebAuthnCredentialsDelegateFactory::GetFactory(web_contents())
          ->GetDelegateForFrame(web_contents()->GetPrimaryMainFrame());
  ASSERT_TRUE(delegate);

  TransportAvailabilityInfo transports_info;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.recognized_credentials = {kCred1From3p};
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  controller.SetUIPresentation(UIPresentation::kAutofill);
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(transports_info, {});

  const std::vector<password_manager::PasskeyCredential>* autofill_passkeys =
      delegate->GetPasskeys().value();
  ASSERT_TRUE(autofill_passkeys);
  EXPECT_EQ(autofill_passkeys->size(), 1u);
  EXPECT_THAT(
      base::UTF16ToUTF8(autofill_passkeys->at(0).GetAuthenticatorLabel()),
      testing::HasSubstr("Bitwarden"));
}

TEST_F(AuthenticatorRequestDialogControllerTest, MechanismsFromUserAccounts) {
  // Set up a model with two local passkeys and a GPM passkey.
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());
  TransportAvailabilityInfo transports_info;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.available_transports = {AuthenticatorTransport::kInternal};
  transports_info.recognized_credentials = {kCred1, kCred2};
  transports_info.ble_status = BleStatus::kOn;

  controller.set_cable_transport_info(
      /*extension_is_v2=*/std::nullopt, std::nullopt);
  RepeatingValueCallbackReceiver<device::DiscoverableCredentialMetadata>
      account_preselected_callback;
  controller.SetAccountPreselectedCallback(
      account_preselected_callback.Callback());

  RequestCallbackReceiver request_callback;
  controller.SetRequestCallback(request_callback.Callback());
  const std::string kLocalAuthenticatorId = "local-authenticator";
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      kLocalAuthenticatorId, AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));
  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});

  // Entries will be sorted by username. So the first entry should correspond to
  // the first local passkey.
  const AuthenticatorRequestDialogModel::Mechanism& mech1 =
      model->mechanisms[0];
  EXPECT_EQ(mech1.name, base::UTF8ToUTF16(*kUser1.name));
  EXPECT_EQ(mech1.short_name, base::UTF8ToUTF16(*kUser1.name));
  EXPECT_EQ(mech1.description,
            l10n_util::GetStringUTF16(IDS_WEBAUTHN_SOURCE_USB_SECURITY_KEY));
  EXPECT_EQ(mech1.icon, vector_icons::kPasskeyIcon);
  mech1.callback.Run();
  device::DiscoverableCredentialMetadata result =
      account_preselected_callback.WaitForResult();
  EXPECT_EQ(result.cred_id, kCred1.cred_id);
  EXPECT_EQ(result.source, device::AuthenticatorType::kOther);
  EXPECT_EQ(request_callback.WaitForResult(), kLocalAuthenticatorId);

  // Reset the model as if the user had cancelled out of the operation.
  controller.StartOver();
  controller.saved_authenticators().AddAuthenticator(AuthenticatorReference(
      kLocalAuthenticatorId, AuthenticatorTransport::kInternal,
      device::AuthenticatorType::kOther));

  // The second entry will be `kCred2`.
  const AuthenticatorRequestDialogModel::Mechanism& mech2 =
      model->mechanisms[1];
  EXPECT_EQ(mech2.name, base::UTF8ToUTF16(*kUser2.name));
  EXPECT_EQ(mech2.short_name, base::UTF8ToUTF16(*kUser2.name));
  EXPECT_EQ(mech2.description,
            l10n_util::GetStringUTF16(IDS_WEBAUTHN_SOURCE_USB_SECURITY_KEY));
  EXPECT_EQ(mech2.icon, vector_icons::kPasskeyIcon);
  mech2.callback.Run();
  result = account_preselected_callback.WaitForResult();
  EXPECT_EQ(result.cred_id, kCred2.cred_id);
  EXPECT_EQ(result.source, device::AuthenticatorType::kOther);
  EXPECT_EQ(request_callback.WaitForResult(), kLocalAuthenticatorId);
}

#if BUILDFLAG(IS_WIN)

using HasCreds = device::FidoRequestHandlerBase::RecognizedCredential;
constexpr int kNoWinButton = -1;
constexpr int kNoChromeUI = -2;
constexpr int kHelloOrSk = IDS_WEBAUTHN_TRANSPORT_WINDOWS_HELLO_OR_SECURITY_KEY;
constexpr int kHello = IDS_WEBAUTHN_TRANSPORT_WINDOWS_HELLO;
constexpr int kSk = IDS_WEBAUTHN_TRANSPORT_EXTERNAL_SECURITY_KEY;
constexpr int kPhoneOrSk =
    IDS_WEBAUTHN_PASSKEY_PHONE_TABLET_OR_SECURITY_KEY_LABEL;
constexpr int kPhone = IDS_WEBAUTHN_PASSKEY_PHONE_OR_TABLET_LABEL;
#define L __LINE__
struct {
  int line_num;
  bool has_sk;
  bool has_hybrid;
  bool has_internal;
  bool supports_hybrid;
  HasCreds has_creds;
  int expected_button;
} kWinHelloButtonGetAssertionTestCases[] = {
    // Windows v7+ with all transports.
    {L, true, true, true, true, HasCreds::kHasRecognizedCredential, kPhoneOrSk},

    // Windows v7+ with only security keys.
    {L, true, false, false, true, HasCreds::kNoRecognizedCredential, kSk},

    // Windows v7+ with only phones.
    {L, false, true, false, true, HasCreds::kNoRecognizedCredential, kPhone},

    // Windows v7+ with only internal creds.
    {L, false, false, true, true, HasCreds::kHasRecognizedCredential,
     kNoChromeUI},

    // Windows v7+ with empty allow-list.
    {L, false, false, false, true, HasCreds::kHasRecognizedCredential,
     kPhoneOrSk},

    // Windows v5+ with all transports.
    {L, true, true, true, false, HasCreds::kHasRecognizedCredential, kSk},

    // Windows v5+ with only security keys
    {L, true, false, false, false, HasCreds::kNoRecognizedCredential, kSk},

    // Windows v5+ with only phones.
    {L, false, true, false, false, HasCreds::kNoRecognizedCredential,
     kNoWinButton},

    // Windows v5+ with only internal creds.
    {L, false, false, true, false, HasCreds::kHasRecognizedCredential,
     kNoChromeUI},

    // Windows v5+ with empty allow-list.
    {L, false, false, false, false, HasCreds::kHasRecognizedCredential, kSk},

    // Windows <v4 with all transports.
    {L, true, true, true, false, HasCreds::kUnknown, kHelloOrSk},

    // Windows <v4 with only security keys.
    {L, true, false, false, false, HasCreds::kUnknown, kSk},

    // Windows <v4 with only phones.
    {L, false, true, false, false, HasCreds::kUnknown, kNoWinButton},

    // Windows <v4 with only internal creds.
    {L, false, false, true, false, HasCreds::kUnknown, kHello},

    // Windows <v4 with empty allow-list.
    {L, false, false, false, false, HasCreds::kUnknown, kHelloOrSk},
};
#undef L

TEST_F(AuthenticatorRequestDialogControllerTest,
       WindowsHelloButtonLabel_GetAssertion) {
  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);
  for (const auto& test_case : kWinHelloButtonGetAssertionTestCases) {
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    controller.SetAccountPreselectedCallback(base::DoNothing());

    TransportAvailabilityInfo transports_info;
    transports_info.has_win_native_api_authenticator = true;
    transports_info.request_type = device::FidoRequestType::kGetAssertion;
    transports_info.transport_list_did_include_security_key = test_case.has_sk;
    transports_info.transport_list_did_include_hybrid = test_case.has_hybrid;
    transports_info.transport_list_did_include_internal =
        test_case.has_internal;
    transports_info.has_platform_authenticator_credential = test_case.has_creds;
    if (test_case.has_creds == HasCreds::kHasRecognizedCredential) {
      transports_info.recognized_credentials = {kCred1};
    }
    if (!test_case.has_sk && !test_case.has_hybrid && !test_case.has_internal) {
      transports_info.has_empty_allow_list = true;
    }
    fake_win_webauthn_api.set_version(test_case.supports_hybrid ? 7 : 4);
    SCOPED_TRACE(testing::Message() << "Line number: " << test_case.line_num);
    SCOPED_TRACE(testing::Message() << "SK: " << test_case.has_sk);
    SCOPED_TRACE(testing::Message() << "Hybrid: " << test_case.has_hybrid);
    SCOPED_TRACE(testing::Message() << "Internal: " << test_case.has_internal);
    SCOPED_TRACE(testing::Message()
                 << "Has creds: " << static_cast<int>(test_case.has_creds));
    SCOPED_TRACE(testing::Message()
                 << "Handles hybrid: " << test_case.supports_hybrid);
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
    auto win_button_it =
        std::ranges::find_if(model->mechanisms, [](const auto& m) {
          return std::holds_alternative<
              AuthenticatorRequestDialogModel::Mechanism::WindowsAPI>(m.type);
        });
    if (test_case.expected_button == kNoWinButton) {
      EXPECT_EQ(win_button_it, model->mechanisms.end());
    } else if (test_case.expected_button == kNoChromeUI) {
      // In these cases, Chrome should have invoked the Windows UI immediately.
      EXPECT_EQ(model->step(), Step::kNotStarted);
    } else {
      ASSERT_NE(win_button_it, model->mechanisms.end());
      EXPECT_EQ(win_button_it->name,
                l10n_util::GetStringUTF16(test_case.expected_button));
      EXPECT_EQ(win_button_it->short_name,
                l10n_util::GetStringUTF16(test_case.expected_button));
      switch (test_case.expected_button) {
        case kHelloOrSk:
        case kHello:
          EXPECT_EQ(win_button_it->icon, kLaptopIcon);
          break;
        case kSk:
          EXPECT_EQ(win_button_it->icon, kUsbSecurityKeyIcon);
          break;
        case kPhoneOrSk:
        case kPhone:
          EXPECT_EQ(win_button_it->icon, kSmartphoneIcon);
          break;
        default:
          NOTREACHED();
      }
    }
  }
}

struct {
  device::AuthenticatorAttachment attachment;
  int expected_button;
} kWinHelloButtonMakeCredentialTestCases[] = {
    // For make credential, we will only show the authenticator picker when
    // Windows does not do hybrid. Therefore, there is no option for "Hello,
    // Security Key, or Phone".
    {device::AuthenticatorAttachment::kAny, kHelloOrSk},
    {device::AuthenticatorAttachment::kCrossPlatform, kSk},
    {device::AuthenticatorAttachment::kPlatform, kHello},
};

TEST_F(AuthenticatorRequestDialogControllerTest,
       WindowsHelloButtonLabel_MakeCredential) {
  device::FakeWinWebAuthnApi fake_win_webauthn_api;
  device::WinWebAuthnApi::ScopedOverride win_webauthn_api_override(
      &fake_win_webauthn_api);
  for (const auto& test_case : kWinHelloButtonMakeCredentialTestCases) {
    auto model =
        base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
    AuthenticatorRequestDialogController controller(model.get(), main_rfh());
    TransportAvailabilityInfo transports_info;
    transports_info.has_win_native_api_authenticator = true;
    transports_info.request_type = device::FidoRequestType::kMakeCredential;
    transports_info.attestation_conveyance_preference =
        device::AttestationConveyancePreference::kNone;
    transports_info.make_credential_attachment = test_case.attachment;
    fake_win_webauthn_api.set_version(4);
    SCOPED_TRACE(testing::Message()
                 << "Attachment: " << static_cast<int>(test_case.attachment));
    UpdateModelBeforeStartFlow(model.get(), transports_info);
    controller.StartFlow(std::move(transports_info), {});
    auto win_button_it =
        std::ranges::find_if(model->mechanisms, [](const auto& m) {
          return std::holds_alternative<
              AuthenticatorRequestDialogModel::Mechanism::WindowsAPI>(m.type);
        });
    ASSERT_NE(win_button_it, model->mechanisms.end());
    EXPECT_EQ(win_button_it->name,
              l10n_util::GetStringUTF16(test_case.expected_button));
    EXPECT_EQ(win_button_it->short_name,
              l10n_util::GetStringUTF16(test_case.expected_button));
    switch (test_case.expected_button) {
      case kHelloOrSk:
      case kHello:
        EXPECT_EQ(win_button_it->icon, kLaptopIcon);
        break;
      case kSk:
        EXPECT_EQ(win_button_it->icon, kUsbSecurityKeyIcon);
        break;
      default:
        NOTREACHED();
    }
  }
}

#endif  // BUILDFLAG(IS_WIN)

TEST_F(AuthenticatorRequestDialogControllerTest,
       NoICloudKeychainMechanism_ModalImmediate_UnknownCredStatus) {
  auto model =
      base::MakeRefCounted<AuthenticatorRequestDialogModel>(main_rfh());
  AuthenticatorRequestDialogController controller(model.get(), main_rfh());

  TransportAvailabilityInfo transports_info;
  transports_info.request_type = device::FidoRequestType::kGetAssertion;
  transports_info.has_icloud_keychain = true;
  transports_info.has_icloud_keychain_credential =
      device::FidoRequestHandlerBase::RecognizedCredential::kUnknown;
  transports_info.available_transports = {AuthenticatorTransport::kInternal};
  transports_info.transport_list_did_include_internal = true;

  // Ensure allow_icloud_keychain_ is true in the controller.
  // This would typically be set based on transports_info.has_icloud_keychain
  // during the full flow, but we set it explicitly here for clarity and
  // directness.
  controller.set_allow_icloud_keychain(true);

  // Set the UI presentation to kModalImmediate.
  controller.SetUIPresentation(UIPresentation::kModalImmediate);

  UpdateModelBeforeStartFlow(model.get(), transports_info);
  controller.StartFlow(std::move(transports_info), {});

  // Verify that no iCloud Keychain mechanism was added.
  bool icloud_mechanism_found = false;
  for (const auto& mech : model->mechanisms) {
    if (std::holds_alternative<
            AuthenticatorRequestDialogModel::Mechanism::ICloudKeychain>(
            mech.type)) {
      icloud_mechanism_found = true;
      break;
    }
  }
  EXPECT_FALSE(icloud_mechanism_found);
}
