// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/features/password_features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "components/password_manager/core/browser/password_manager_buildflags.h"

namespace password_manager::features {

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kAndroidSmsOtpFilling,
             "AndroidSmsOtpFilling",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

BASE_FEATURE(kAutoApproveSharedPasswordUpdatesFromSameSender,
             "AutoApproveSharedPasswordUpdatesFromSameSender",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)  // Desktop
BASE_FEATURE(kAutofillPasswordUserPerceptionSurvey,
             "AutofillPasswordUserPerceptionSurvey",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enabled by default in M138. Remove in or after M141.
BASE_FEATURE(kWebAuthnUsePasskeyFromAnotherDeviceInContextMenu,
             "WebAuthnUsePasskeyFromAnotherDeviceInContextMenu",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

BASE_FEATURE(kBiometricTouchToFill,
             "BiometricTouchToFill",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kClearUndecryptablePasswords,
             "ClearUndecryptablePasswords",
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_IOS)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

BASE_FEATURE(kClearUndecryptablePasswordsOnSync,
             "ClearUndecryptablePasswordsInSync",
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_IOS) || \
    BUILDFLAG(IS_WIN)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

BASE_FEATURE(kFailedLoginDetectionBasedOnResourceLoadingErrors,
             "FailedLoginDetectionBasedOnResourceLoadingErrors",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kFailedLoginDetectionBasedOnFormClearEvent,
             "FailedLoginDetectionBasedOnFormClearEvent",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kFetchGaiaHashOnSignIn,
             "FetchGaiaHashOnSignIn",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

BASE_FEATURE(kFillOnAccountSelect,
             "fill-on-account-select",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kFillRecoveryPassword,
             "FillRecoveryPassword",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(IS_IOS)
BASE_FEATURE(kIosCleanupHangingPasswordFormExtractionRequests,
             "IosCleanupHangingPasswordFormExtractionRequests",
             base::FEATURE_ENABLED_BY_DEFAULT);
const base::FeatureParam<int> kIosPasswordFormExtractionRequestsTimeoutMs = {
    &kIosCleanupHangingPasswordFormExtractionRequests,
    /*name=*/"period-ms", /*default_value=*/250};

BASE_FEATURE(kIOSPasswordBottomSheetV2,
             "IOSPasswordBottomSheetV2",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIOSProactivePasswordGenerationBottomSheet,
             "kIOSProactivePasswordGenerationBottomSheet",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kIOSFillRecoveryPassword,
             "IOSFillRecoveryPassword",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // IS_IOS

BASE_FEATURE(kPasswordFormGroupedAffiliations,
             "PasswordFormGroupedAffiliations",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPasswordFormClientsideClassifier,
             "PasswordFormClientsideClassifier",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)  // Desktop
BASE_FEATURE(kPasswordGenerationChunking,
             "PasswordGenerationChunkPassword",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kPasswordManualFallbackAvailable,
             "PasswordManualFallbackAvailable",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

BASE_FEATURE(kPasswordManagerLogToTerminal,
             "PasswordManagerLogToTerminal",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kReuseDetectionBasedOnPasswordHashes,
             "ReuseDetectionBasedOnPasswordHashes",
             base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
BASE_FEATURE(kRestartToGainAccessToKeychain,
             "RestartToGainAccessToKeychain",
#if BUILDFLAG(IS_MAC)
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_CHROMEOS)
BASE_FEATURE(kBiometricsAuthForPwdFill,
             "BiometricsAuthForPwdFill",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_CHROMEOS)

BASE_FEATURE(kSetLeakCheckRequestCriticality,
             "SetLeakCheckRequestCriticality",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kShowRecoveryPassword,
             "ShowRecoveryPassword",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kSkipUndecryptablePasswords,
             "SkipUndecryptablePasswords",
#if BUILDFLAG(IS_WIN)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

BASE_FEATURE(kTriggerPasswordResyncAfterDeletingUndecryptablePasswords,
             "TriggerPasswordResyncAfterDeletingUndecryptablePasswords",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTriggerPasswordResyncWhenUndecryptablePasswordsDetected,
             "TriggerPasswordResyncWhenUndecryptablePasswordsDetected",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kBiometricAuthIdentityCheck,
             "BiometricAuthIdentityCheck",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kLoginDbDeprecationAndroid,
             "LoginDbDeprecationAndroid",
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

BASE_FEATURE(kUseNewEncryptionMethod,
             "UseNewEncryptionMethod",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kEncryptAllPasswordsWithOSCryptAsync,
             "EncryptAllPasswordsWithOSCryptAsync",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMarkAllCredentialsAsLeaked,
             "MarkAllCredentialsAsLeaked",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kImprovedPasswordChangeService,
             "ImprovedPasswordChangeService",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace password_manager::features
