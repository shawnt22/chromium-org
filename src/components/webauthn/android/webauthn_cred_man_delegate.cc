// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/webauthn/android/webauthn_cred_man_delegate.h"

#include <optional>
#include <string>
#include <utility>

#include "base/android/jni_android.h"
#include "base/functional/callback.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "components/webauthn/android/cred_man_support.h"
#include "content/public/browser/web_contents.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "components/webauthn/android/jni_headers/CredManSupportProvider_jni.h"

namespace content {
class WebContents;
}  // namespace content

namespace webauthn {

using webauthn::CredManSupport;

WebAuthnCredManDelegate::WebAuthnCredManDelegate(
    content::WebContents* web_contents) {}

WebAuthnCredManDelegate::~WebAuthnCredManDelegate() = default;

void WebAuthnCredManDelegate::OnCredManConditionalRequestPending(
    bool has_passkeys,
    base::RepeatingCallback<void(bool)> full_assertion_request) {
  has_passkeys_ = has_passkeys ? kHasPasskeys : kNoPasskeys;
  show_cred_man_ui_callback_ = std::move(full_assertion_request);

  std::vector<base::OnceClosure> notification_closures;
  if (credentials_available_closure_) {
    std::move(credentials_available_closure_).Run();
  }
}

void WebAuthnCredManDelegate::OnCredManUiClosed(bool success) {
  if (!request_completion_callback_.is_null()) {
    request_completion_callback_.Run(success);
  }
}

void WebAuthnCredManDelegate::TriggerCredManUi(
    RequestPasswords request_passwords) {
  if (!passkeys_after_fill_recorded_) {
    passkeys_after_fill_recorded_ = true;
    base::UmaHistogramBoolean(
        "PasswordManager.PasskeysArrivedAfterAutofillDisplay",
        has_passkeys_ == kNotReady);
  }

  if (show_cred_man_ui_callback_.is_null()) {
    return;
  }
  show_cred_man_ui_callback_.Run(request_passwords.value() &&
                                 !filling_callback_.is_null());
}

WebAuthnCredManDelegate::State WebAuthnCredManDelegate::HasPasskeys() const {
  return has_passkeys_;
}

void WebAuthnCredManDelegate::CleanUpConditionalRequest() {
  show_cred_man_ui_callback_.Reset();
  has_passkeys_ = kNotReady;
}

void WebAuthnCredManDelegate::SetRequestCompletionCallback(
    base::RepeatingCallback<void(bool)> callback) {
  request_completion_callback_ = std::move(callback);
}

void WebAuthnCredManDelegate::SetFillingCallback(
    base::OnceCallback<void(const std::u16string&, const std::u16string&)>
        filling_callback) {
  filling_callback_ = std::move(filling_callback);
}

void WebAuthnCredManDelegate::FillUsernameAndPassword(
    const std::u16string& username,
    const std::u16string& password) {
  std::move(filling_callback_).Run(username, password);
}

void WebAuthnCredManDelegate::RequestNotificationWhenCredentialsReady(
    base::OnceClosure closure) {
  if (has_passkeys_ != kNotReady) {
    std::move(closure).Run();
    return;
  }
  CHECK(!credentials_available_closure_);
  credentials_available_closure_ = std::move(closure);
}

base::WeakPtr<WebAuthnCredManDelegate> WebAuthnCredManDelegate::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

// static
WebAuthnCredManDelegate::CredManEnabledMode
WebAuthnCredManDelegate::CredManMode() {
  if (!cred_man_support_.has_value()) {
    cred_man_support_ = Java_CredManSupportProvider_getCredManSupport(
        base::android::AttachCurrentThread());
  }
  switch (cred_man_support_.value()) {
    case CredManSupport::NOT_EVALUATED:
      NOTREACHED();
    case CredManSupport::DISABLED:
    case CredManSupport::IF_REQUIRED:
      return CredManEnabledMode::kNotEnabled;
    case CredManSupport::FULL_UNLESS_INAPPLICABLE:
      return CredManEnabledMode::kAllCredMan;
    case CredManSupport::PARALLEL_WITH_FIDO_2:
      return CredManEnabledMode::kNonGpmPasskeys;
  }
  return CredManEnabledMode::kNotEnabled;
}

std::optional<int> WebAuthnCredManDelegate::cred_man_support_ = std::nullopt;

}  // namespace webauthn
