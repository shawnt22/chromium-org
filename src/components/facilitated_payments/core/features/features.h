// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FACILITATED_PAYMENTS_CORE_FEATURES_FEATURES_H_
#define COMPONENTS_FACILITATED_PAYMENTS_CORE_FEATURES_FEATURES_H_

#include "base/feature_list.h"
#include "build/build_config.h"

namespace payments::facilitated {

BASE_DECLARE_FEATURE(kEnablePixPaymentsInLandscapeMode);
#if BUILDFLAG(IS_ANDROID)
BASE_DECLARE_FEATURE(kDisableFacilitatedPaymentsMerchantAllowlist);
BASE_DECLARE_FEATURE(kEnablePixAccountLinking);
BASE_DECLARE_FEATURE(kEwalletPayments);
#endif  // BUILDFLAG(IS_ANDROID)
BASE_DECLARE_FEATURE(kSupportMultipleServerRequestsForPixPayments);
#if BUILDFLAG(IS_ANDROID)
BASE_DECLARE_FEATURE(kFacilitatedPaymentsEnableA2APayment);
#endif  // BUILDFLAG(IS_ANDROID)

}  // namespace payments::facilitated

#endif  // COMPONENTS_FACILITATED_PAYMENTS_CORE_FEATURES_FEATURES_H_
