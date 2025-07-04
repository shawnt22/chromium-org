// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/device_bound_sessions/session_binding_utils.h"

#include <optional>
#include <string_view>

#include "base/base64url.h"
#include "base/containers/span.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_view_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "crypto/signature_verifier.h"
#include "net/base/url_util.h"
#include "net/device_bound_sessions/jwk_utils.h"
#include "third_party/boringssl/src/include/openssl/bn.h"
#include "third_party/boringssl/src/include/openssl/ecdsa.h"
#include "url/gurl.h"

namespace net::device_bound_sessions {

namespace {

// Source: JSON Web Signature and Encryption Algorithms
// https://www.iana.org/assignments/jose/jose.xhtml
std::string SignatureAlgorithmToString(
    crypto::SignatureVerifier::SignatureAlgorithm algorithm) {
  switch (algorithm) {
    case crypto::SignatureVerifier::ECDSA_SHA256:
      return "ES256";
    case crypto::SignatureVerifier::RSA_PKCS1_SHA256:
      return "RS256";
    case crypto::SignatureVerifier::RSA_PSS_SHA256:
      return "PS256";
    case crypto::SignatureVerifier::RSA_PKCS1_SHA1:
      return "RS1";
  }
}

std::string Base64UrlEncode(std::string_view data) {
  std::string output;
  base::Base64UrlEncode(data, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &output);
  return output;
}

std::optional<std::string> CreateHeaderAndPayloadWithCustomPayload(
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    const base::Value::Dict& payload) {
  auto header = base::Value::Dict()
                    .Set("alg", SignatureAlgorithmToString(algorithm))
                    .Set("typ", "dbsc+jwt");
  std::optional<std::string> header_serialized = base::WriteJson(header);
  if (!header_serialized) {
    DVLOG(1) << "Unexpected JSONWriter error while serializing a registration "
                "token header";
    return std::nullopt;
  }

  std::optional<std::string> payload_serialized = base::WriteJsonWithOptions(
      payload, base::JSONWriter::OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION);
  if (!payload_serialized) {
    DVLOG(1) << "Unexpected JSONWriter error while serializing a registration "
                "token payload";
    return std::nullopt;
  }

  return base::StrCat({Base64UrlEncode(*header_serialized), ".",
                       Base64UrlEncode(*payload_serialized)});
}

std::optional<std::vector<uint8_t>> ConvertDERSignatureToRaw(
    base::span<const uint8_t> der_signature) {
  bssl::UniquePtr<ECDSA_SIG> ecdsa_sig(
      ECDSA_SIG_from_bytes(der_signature.data(), der_signature.size()));
  if (!ecdsa_sig) {
    DVLOG(1) << "Failed to create ECDSA_SIG";
    return std::nullopt;
  }

  // TODO(b/301888680): this implicitly depends on a curve used by
  // `crypto::UnexportableKey`. Make this dependency more explicit.
  const size_t kMaxBytesPerBN = 32;
  std::vector<uint8_t> jwt_signature(2 * kMaxBytesPerBN);

  if (!BN_bn2bin_padded(&jwt_signature[0], kMaxBytesPerBN, ecdsa_sig->r) ||
      !BN_bn2bin_padded(&jwt_signature[kMaxBytesPerBN], kMaxBytesPerBN,
                        ecdsa_sig->s)) {
    DVLOG(1) << "Failed to serialize R and S to " << kMaxBytesPerBN << " bytes";
    return std::nullopt;
  }

  return jwt_signature;
}

}  // namespace

std::optional<std::string> CreateKeyRegistrationHeaderAndPayload(
    std::string_view challenge,
    const GURL& registration_url,
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    base::span<const uint8_t> pubkey_spki,
    base::Time timestamp,
    std::optional<std::string> authorization,
    std::optional<std::string> session_id) {
  base::Value::Dict jwk = ConvertPkeySpkiToJwk(algorithm, pubkey_spki);
  if (jwk.empty()) {
    DVLOG(1) << "Unexpected error when converting the SPKI to a JWK";
    return std::nullopt;
  }

  auto payload =
      base::Value::Dict()
          .Set("aud", registration_url.spec())
          .Set("jti", challenge)
          // Write out int64_t variable as a double.
          // Note: this may discard some precision, but for `base::Value`
          // there's no other option.
          .Set("iat", static_cast<double>(
                          (timestamp - base::Time::UnixEpoch()).InSeconds()))
          .Set("key", std::move(jwk));

  if (authorization.has_value()) {
    payload.Set("authorization", authorization.value());
  }
  if (session_id.has_value()) {
    payload.Set("sub", session_id.value());
  }
  return CreateHeaderAndPayloadWithCustomPayload(algorithm, payload);
}

std::optional<std::string> AppendSignatureToHeaderAndPayload(
    std::string_view header_and_payload,
    crypto::SignatureVerifier::SignatureAlgorithm algorithm,
    base::span<const uint8_t> signature) {
  std::optional<std::vector<uint8_t>> signature_holder;
  if (algorithm == crypto::SignatureVerifier::ECDSA_SHA256) {
    signature_holder = ConvertDERSignatureToRaw(signature);
    if (!signature_holder.has_value()) {
      return std::nullopt;
    }
    signature = base::span(*signature_holder);
  }

  return base::StrCat(
      {header_and_payload, ".", Base64UrlEncode(as_string_view(signature))});
}

bool IsSecure(const GURL& url) {
  return url.SchemeIsCryptographic() || IsLocalhost(url);
}

}  // namespace net::device_bound_sessions
