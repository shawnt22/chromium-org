// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_WEBAUTHN_ANDROID_WEBAUTHN_CRED_MAN_DELEGATE_H_
#define COMPONENTS_WEBAUTHN_ANDROID_WEBAUTHN_CRED_MAN_DELEGATE_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/types/strong_alias.h"

namespace content {
class WebContents;
}  // namespace content

namespace webauthn {

// This class is responsible for caching and serving CredMan calls. Android U+
// only.
class WebAuthnCredManDelegate {
 public:
  using RequestPasswords = base::StrongAlias<class RequestPasswordsTag, bool>;

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum State {
    kNotReady = 0,
    kNoPasskeys = 1,
    kHasPasskeys = 2,
    kMaxValue = kHasPasskeys
  };

  enum CredManEnabledMode {
    kNotEnabled,
    kAllCredMan,
    kNonGpmPasskeys,
  };

  explicit WebAuthnCredManDelegate(content::WebContents* web_contents);

  WebAuthnCredManDelegate(const WebAuthnCredManDelegate&) = delete;
  WebAuthnCredManDelegate& operator=(const WebAuthnCredManDelegate&) = delete;

  virtual ~WebAuthnCredManDelegate();

  // Called when a Web Authentication Conditional UI request is received. This
  // caches the callback that will complete the request after user
  // interaction.
  virtual void OnCredManConditionalRequestPending(
      bool has_results,
      base::RepeatingCallback<void(bool)> show_cred_man_ui_callback);

  // Called when the CredMan UI is closed.
  virtual void OnCredManUiClosed(bool success);

  // Called when the user focuses a webauthn login form. This will trigger
  // CredMan UI.
  // If |request_passwords|, the UI will also include passwords if there are
  // any.
  virtual void TriggerCredManUi(RequestPasswords request_passwords);

  // Returns whether there are passkeys in the Android Credential Manager UI.
  // Returns `kNotReady` if Credential Manager has not replied yet.
  virtual State HasPasskeys() const;

  // Clears the cached `show_cred_man_ui_callback_` and `has_results_`.
  virtual void CleanUpConditionalRequest();

  // The setter for `request_completion_callback_`. Classes can set
  // `request_completion_callback_` to be notified about when CredMan UI is
  // closed (i.e. to show / hide keyboard).
  virtual void SetRequestCompletionCallback(
      base::RepeatingCallback<void(bool)> callback);

  // The setter for `filling_callback_`.  Classes should use this method before
  // `FillUsernameAndPassword`.
  virtual void SetFillingCallback(
      base::OnceCallback<void(const std::u16string&, const std::u16string&)>
          filling_callback);

  // If a password credential is received from CredMan UI, this method will be
  // called. A password credential can be filled only once.
  virtual void FillUsernameAndPassword(const std::u16string& username,
                                       const std::u16string& password);

  // Callers of this method will be notified via `closure` when the credential
  // list from CredMan is available. `closure` can be invoked mmediately if the
  // passkey list has already been received. This CHECKs if called twice
  // without the first having resolved.
  virtual void RequestNotificationWhenCredentialsReady(
      base::OnceClosure closure);

  static CredManEnabledMode CredManMode();

  virtual base::WeakPtr<WebAuthnCredManDelegate> AsWeakPtr();

#if defined(UNIT_TEST)
  static void override_cred_man_support_for_testing(int support) {
    cred_man_support_ = support;
  }
#endif

 private:
  State has_passkeys_ = kNotReady;
  base::RepeatingCallback<void(bool)> show_cred_man_ui_callback_;
  base::RepeatingCallback<void(bool)> request_completion_callback_;
  base::OnceCallback<void(const std::u16string&, const std::u16string&)>
      filling_callback_;

  // Callback awaiting notification of credentials being available.
  base::OnceClosure credentials_available_closure_;

  // Trakcks whether the PasskeysArrivedAfterAutofillDisplay metric has been
  // recorded.
  bool passkeys_after_fill_recorded_ = false;

  static std::optional<int> cred_man_support_;

  base::WeakPtrFactory<WebAuthnCredManDelegate> weak_ptr_factory_{this};
};

}  // namespace webauthn

#endif  // COMPONENTS_WEBAUTHN_ANDROID_WEBAUTHN_CRED_MAN_DELEGATE_H_
