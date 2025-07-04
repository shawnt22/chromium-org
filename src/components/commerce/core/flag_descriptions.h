// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_COMMERCE_CORE_FLAG_DESCRIPTIONS_H_
#define COMPONENTS_COMMERCE_CORE_FLAG_DESCRIPTIONS_H_

#include "build/build_config.h"

namespace commerce::flag_descriptions {

extern const char kCommerceLocalPDPDetectionName[];
extern const char kCommerceLocalPDPDetectionDescription[];

extern const char kProductSpecificationsName[];
extern const char kProductSpecificationsDescription[];

extern const char kCompareConfirmationToastName[];
extern const char kCompareConfirmationToastDescription[];

extern const char kShoppingListName[];
extern const char kShoppingListDescription[];

extern const char kChromeCartDomBasedHeuristicsName[];
extern const char kChromeCartDomBasedHeuristicsDescription[];

extern const char kPriceInsightsName[];
extern const char kPriceInsightsDescription[];

extern const char kDiscountOnNavigationName[];
extern const char kDiscountOnNavigationDescription[];

#if BUILDFLAG(IS_IOS)
extern const char kPriceInsightsIosName[];
extern const char kPriceInsightsIosDescription[];

extern const char kPriceInsightsHighPriceIosName[];
extern const char kPriceInsightsHighPriceIosDescription[];
#endif

extern const char kShoppingPageTypesName[];
extern const char kShoppingPageTypesDescription[];

extern const char kTrackByDefaultOnMobileName[];
extern const char kTrackByDefaultOnMobileDescription[];

extern const char kPriceTrackingSubscriptionServiceLocaleKeyName[];
extern const char kPriceTrackingSubscriptionServiceLocaleKeyDescription[];

extern const char kPriceTrackingSubscriptionServiceProductVersionName[];
extern const char kPriceTrackingSubscriptionServiceProductVersionDescription[];

extern const char kDiscountAutofillName[];
extern const char kDiscountAutofillDescription[];

extern const char kShoppingAlternateServerName[];
extern const char kShoppingAlternateServerDescription[];

}  // namespace commerce::flag_descriptions

#endif  // COMPONENTS_COMMERCE_CORE_FLAG_DESCRIPTIONS_H_
