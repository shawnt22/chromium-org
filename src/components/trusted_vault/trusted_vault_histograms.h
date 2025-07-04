// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TRUSTED_VAULT_TRUSTED_VAULT_HISTOGRAMS_H_
#define COMPONENTS_TRUSTED_VAULT_TRUSTED_VAULT_HISTOGRAMS_H_

#include <optional>

#include "components/trusted_vault/trusted_vault_server_constants.h"

namespace trusted_vault {

enum class LocalRecoveryFactorType;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultHintDegradedRecoverabilityChangedReason)
enum class TrustedVaultHintDegradedRecoverabilityChangedReasonForUMA {
  kRecoveryMethodAdded = 0,
  kPersistentAuthErrorResolved = 1,
  kMaxValue = kPersistentAuthErrorResolved,
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/sync/enums.xml:TrustedVaultHintDegradedRecoverabilityChangedReason)

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultRecoveryFactorRegistrationState)
enum class TrustedVaultRecoveryFactorRegistrationStateForUMA {
  kAlreadyRegisteredV0 = 0,  // Used only on iOS.
  kLocalKeysAreStale = 1,
  kThrottledClientSide = 2,
  kAttemptingRegistrationWithNewKeyPair = 3,
  kAttemptingRegistrationWithExistingKeyPair = 4,
  // Deprecated, replaced with more detailed
  // TrustedVaultRecoveryFactorRegistrationOutcomeForUMA.
  kDeprecatedAttemptingRegistrationWithPersistentAuthError = 5,
  kAlreadyRegisteredV1 = 6,
  kRegistrationWithConstantKeyNotSupported = 7,
  kMaxValue = kRegistrationWithConstantKeyNotSupported,
};
// TODO(crbug.com/369980730): this is used in internals, replace usages with the
// version above and delete this alias.
using TrustedVaultDeviceRegistrationStateForUMA =
    TrustedVaultRecoveryFactorRegistrationStateForUMA;
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:TrustedVaultRecoveryFactorRegistrationState)

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultRecoveryFactorRegistrationOutcome)
enum class TrustedVaultRecoveryFactorRegistrationOutcomeForUMA {
  kSuccess = 0,
  kAlreadyRegistered = 1,
  kLocalDataObsolete = 2,
  kTransientAccessTokenFetchError = 3,
  kPersistentAccessTokenFetchError = 4,
  kPrimaryAccountChangeAccessTokenFetchError = 5,
  kNetworkError = 6,
  kOtherError = 7,
  kMaxValue = kOtherError,
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:TrustedVaultRecoveryFactorRegistrationOutcome)

// Used to provide UMA metric breakdowns.
enum class TrustedVaultURLFetchReasonForUMA {
  kUnspecified,
  kRegisterDevice,
  kRegisterLockScreenKnowledgeFactor,
  kRegisterUnspecifiedAuthenticationFactor,
  kDownloadKeys,
  kDownloadIsRecoverabilityDegraded,
  kDownloadAuthenticationFactorsRegistrationState,
  kRegisterGpmPin,
  kRegisterICloudKeychain,
};

// Used to provide UMA metric breakdowns.
enum class RecoveryKeyStoreURLFetchReasonForUMA {
  kUpdateRecoveryKeyStore,
  kListRecoveryKeyStores,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(RecoveryKeyStoreCertificatesFetchStatus)
enum class RecoveryKeyStoreCertificatesFetchStatusForUMA {
  kSuccess = 0,
  kNetworkError = 1,
  kParseError = 2,
  kMaxValue = kParseError,
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:RecoveryKeyStoreCertificatesFetchStatus)

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultDownloadKeysStatus)
enum class TrustedVaultDownloadKeysStatusForUMA {
  kSuccess = 0,
  // Deprecated in favor of the more fine-grained buckets.
  kDeprecatedMembershipNotFoundOrCorrupted = 1,
  kNoNewKeys = 2,
  kKeyProofsVerificationFailed = 3,
  kAccessTokenFetchingFailure = 4,
  kOtherError = 5,
  kMemberNotFound = 6,
  kMembershipNotFound = 7,
  kMembershipCorrupted = 8,
  kMembershipEmpty = 9,
  kNoPrimaryAccount = 10,
  kDeviceNotRegistered = 11,
  kThrottledClientSide = 12,
  kCorruptedLocalDeviceRegistration = 13,
  kAborted = 14,
  kNetworkError = 15,
  kKeyProofVerificationNotSupported = 16,
  kMaxValue = kKeyProofVerificationNotSupported
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:TrustedVaultDownloadKeysStatus)

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultRecoverKeysOutcome)
enum class TrustedVaultRecoverKeysOutcomeForUMA {
  kSuccess = 0,
  kNoNewKeys = 1,
  kFailure = 2,
  kNoPrimaryAccount = 3,
  kAborted = 4,
  kMaxValue = kAborted
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:TrustedVaultRecoverKeysOutcome)

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultFileReadStatus)
enum class TrustedVaultFileReadStatusForUMA {
  kSuccess = 0,
  kNotFound = 1,
  kFileReadFailed = 2,
  kMD5DigestMismatch = 3,
  kFileProtoDeserializationFailed = 4,
  kDataProtoDeserializationFailed = 5,
  kMaxValue = kDataProtoDeserializationFailed
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:TrustedVaultFileReadStatus)

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(TrustedVaultListSecurityDomainMembersPinStatus)
enum class TrustedVaultListSecurityDomainMembersPinStatus {
  kPinPresentAndUsableForRecovery = 0,
  kPinPresentButUnusableForRecovery = 1,
  kNoPinPresent = 2,
  kMaxValue = kNoPinPresent
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/trusted_vault/enums.xml:TrustedVaultListSecurityDomainMembersPinStatus)

void RecordTrustedVaultHintDegradedRecoverabilityChangedReason(
    TrustedVaultHintDegradedRecoverabilityChangedReasonForUMA
        hint_degraded_recoverability_changed_reason);

// TODO(crbug.com/369980730): this is used in internals, replace usages with the
// version below and delete this one.
void RecordTrustedVaultDeviceRegistrationState(
    TrustedVaultDeviceRegistrationStateForUMA registration_state);

void RecordTrustedVaultRecoveryFactorRegistrationState(
    LocalRecoveryFactorType local_recovery_factor_type,
    SecurityDomainId security_domain_id,
    TrustedVaultRecoveryFactorRegistrationStateForUMA registration_state);

void RecordTrustedVaultRecoveryFactorRegistrationOutcome(
    LocalRecoveryFactorType local_recovery_factor_type,
    SecurityDomainId security_domain_id,
    TrustedVaultRecoveryFactorRegistrationOutcomeForUMA registration_outcome);

// Records url fetch response status (combined http and net error code) for
// requests to security domain service. If |http_response_code| is non-zero, it
// will be recorded, otherwise |net_error| will be recorded. Either
// |http_status| or |net_error| must be non zero.
void RecordTrustedVaultURLFetchResponse(SecurityDomainId security_domain_id,
                                        TrustedVaultURLFetchReasonForUMA reason,
                                        int http_response_code,
                                        int net_error);

// Records url fetch response status (combined http and net error code) for
// requests to vault service. If |http_response_code| is non-zero, it
// will be recorded, otherwise |net_error| will be recorded. Either
// |http_status| or |net_error| must be non zero.
void RecordRecoveryKeyStoreURLFetchResponse(
    RecoveryKeyStoreURLFetchReasonForUMA reason,
    int http_response_code,
    int net_error);

// Records the result of fetching recovery key store certificates.
void RecordRecoveryKeyStoreFetchCertificatesStatus(
    RecoveryKeyStoreCertificatesFetchStatusForUMA status);

// Records the outcome of trying to download keys from the server.
void RecordTrustedVaultDownloadKeysStatus(
    LocalRecoveryFactorType local_recovery_factor_type,
    SecurityDomainId security_domain_id,
    TrustedVaultDownloadKeysStatusForUMA status);

// TODO(crbug.com/369980730): replace usages with the version above (in
// downstream) and delete this one.
void RecordTrustedVaultDownloadKeysStatus(
    TrustedVaultDownloadKeysStatusForUMA status);

void RecordTrustedVaultRecoverKeysOutcome(
    SecurityDomainId security_domain_id,
    TrustedVaultRecoverKeysOutcomeForUMA status);

void RecordTrustedVaultFileReadStatus(SecurityDomainId security_domain_id,
                                      TrustedVaultFileReadStatusForUMA status);

enum class IsOffTheRecord { kNo, kYes };

// Records a call to set security domain encryption keys in the browser.
// `std::nullopt` indicates the caller attempted to set keys for a security
// domain with a name that was not understood by this client.
void RecordTrustedVaultSetEncryptionKeysForSecurityDomain(
    std::optional<SecurityDomainId> security_domain,
    IsOffTheRecord is_off_the_record);

// Records a call to chrome.setClientEncryptionKeys() for the given security
// domain in the renderer. `std::nullopt` indicates the caller attempted to set
// keys for a security domain with a name that was not understood by this
// client.
void RecordCallToJsSetClientEncryptionKeysWithSecurityDomainToUma(
    std::optional<SecurityDomainId> security_domain);

void RecordTrustedVaultListSecurityDomainMembersPinStatus(
    SecurityDomainId security_domain_id,
    TrustedVaultListSecurityDomainMembersPinStatus status);

// Returns a recovery factor name suitable for using in histograms. When
// including this in a histogram, its name in the XML should have
// "{LocalRecoveryFactorType}" where the returned string will be inserted (which
// will include a leading period). For example:
//   name="TrustedVault.Foo{LocalRecoveryFactorType}"
// Will match a histogram name like:
//   TrustedVault.Foo.PhysicalDevice
//
// Then there needs to be a <token> element in the XML entry like:
//   <token key="LocalRecoveryFactorType" variants="LocalRecoveryFactorType"/>
//
// See
// https://chromium.googlesource.com/chromium/src.git/+/HEAD/tools/metrics/histograms/README.md#patterned-histograms
std::string GetLocalRecoveryFactorNameForUma(
    LocalRecoveryFactorType local_recovery_factor_type);

// Returns a security domain name suitable for using in histograms. When
// including this in a histogram, its name in the XML should have
// "{SecurityDomainId}" where the returned string will be inserted (which
// will include a leading period). For example:
//   name="TrustedVault.Foo{SecurityDomainId}"
// Will match a histogram name like:
//   TrustedVault.Foo.ChromeSync
//
// Then there needs to be a <token> element in the XML entry like:
//   <token key="SecurityDomainId" variants="SecurityDomainId"/>
//
// See
// https://chromium.googlesource.com/chromium/src.git/+/HEAD/tools/metrics/histograms/README.md#patterned-histograms
std::string GetSecurityDomainNameForUma(SecurityDomainId domain);

}  // namespace trusted_vault

#endif  // COMPONENTS_TRUSTED_VAULT_TRUSTED_VAULT_HISTOGRAMS_H_
