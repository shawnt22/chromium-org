// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEBAUTHN_WEBAUTHN_PREF_NAMES_H_
#define CHROME_BROWSER_WEBAUTHN_WEBAUTHN_PREF_NAMES_H_

namespace webauthn::pref_names {

// Maps to the AllowWebAuthnWithBrokenCerts enterprise policy.
extern const char kAllowWithBrokenCerts[];

// Tracks how many consecutive times a user has backed out of the GPM credential
// creation UI. This is reset when the user chooses to perform any enclave
// request.
extern const char kEnclaveDeclinedGPMCredentialCreationCount[];

// Tracks how many times a user has declined GPM bootstrapping on this device.
extern const char kEnclaveDeclinedGPMBootstrappingCount[];

// Tracks how many consecutive failed GPM PIN attempts have been made to the
// enclave service from this device and profile.
extern const char kEnclaveFailedPINAttemptsCount[];

// Maps to the WebAuthenticationRemoteProxiedRequestsAllowed enterprise
// policy.
extern const char kRemoteProxiedRequestsAllowed[];

// Maps to the WebAuthenticationRemoteDesktopAllowedOrigins enterprise
// policy.
extern const char kRemoteDesktopAllowedOrigins[];

extern const char kWebAuthnTouchIdMetadataSecretPrefName[];

}  // namespace webauthn::pref_names

#endif  // CHROME_BROWSER_WEBAUTHN_WEBAUTHN_PREF_NAMES_H_
