// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/external_constants_override.h"

#include <utility>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/updater/constants.h"
#include "chrome/updater/external_constants.h"
#include "chrome/updater/external_constants_default.h"
#include "chrome/updater/updater_branding.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace updater {

class ExternalConstantsOverriderTest : public ::testing::Test {};

TEST_F(ExternalConstantsOverriderTest, TestEmptyDictValue) {
  auto overrider = base::MakeRefCounted<ExternalConstantsOverrider>(
      base::Value::Dict(), CreateDefaultExternalConstants());

  EXPECT_TRUE(overrider->UseCUP());

  std::vector<GURL> urls = overrider->UpdateURL();
  ASSERT_EQ(urls.size(), 1ul);
  EXPECT_EQ(urls[0], GURL(UPDATE_CHECK_URL));
  EXPECT_TRUE(urls[0].is_valid());

  EXPECT_EQ(overrider->CrashUploadURL(), GURL(CRASH_UPLOAD_URL));
  EXPECT_TRUE(overrider->CrashUploadURL().is_valid());
  EXPECT_EQ(overrider->AppLogoURL(), GURL(APP_LOGO_URL));
  EXPECT_TRUE(overrider->AppLogoURL().is_valid());

  EXPECT_EQ(overrider->InitialDelay(), kInitialDelay);
  EXPECT_EQ(overrider->ServerKeepAliveTime(), kServerKeepAliveTime);
  EXPECT_EQ(overrider->DictPolicies().size(), 0U);
  EXPECT_EQ(overrider->CecaConnectionTimeout(), kCecaConnectionTimeout);
}

TEST_F(ExternalConstantsOverriderTest, TestFullOverrides) {
  base::Value::Dict overrides;
  base::Value::List url_list;
  url_list.Append("https://localhost/1/www");
  url_list.Append("https://localhost/2/www");
  base::Value::Dict dict_policies;
  dict_policies.Set("a", 1);
  dict_policies.Set("b", 2);

  overrides.Set(kDevOverrideKeyUseCUP, false);
  overrides.Set(kDevOverrideKeyUrl, std::move(url_list));
  overrides.Set(kDevOverrideKeyCrashUploadUrl,
                "https://localhost/2/crash_test");
  overrides.Set(kDevOverrideKeyAppLogoUrl, "https://localhost/2/applogo/");
  overrides.Set(kDevOverrideKeyInitialDelay, 137.1);
  overrides.Set(kDevOverrideKeyServerKeepAliveSeconds, 1);
  overrides.Set(kDevOverrideKeyDictPolicies, std::move(dict_policies));
  overrides.Set(kDevOverrideKeyOverinstallTimeout, 3);
  overrides.Set(kDevOverrideKeyIdleCheckPeriodSeconds, 4);
  overrides.Set(kDevOverrideKeyCecaConnectionTimeout, 27);
  auto overrider = base::MakeRefCounted<ExternalConstantsOverrider>(
      std::move(overrides), CreateDefaultExternalConstants());

  EXPECT_FALSE(overrider->UseCUP());

  std::vector<GURL> urls = overrider->UpdateURL();
  ASSERT_EQ(urls.size(), 2ul);
  EXPECT_EQ(urls[0], GURL("https://localhost/1/www"));
  EXPECT_TRUE(urls[0].is_valid());
  EXPECT_EQ(urls[1], GURL("https://localhost/2/www"));
  EXPECT_TRUE(urls[1].is_valid());

  EXPECT_EQ(overrider->CrashUploadURL(),
            GURL("https://localhost/2/crash_test"));
  EXPECT_TRUE(overrider->CrashUploadURL().is_valid());
  EXPECT_EQ(overrider->AppLogoURL(), GURL("https://localhost/2/applogo/"));
  EXPECT_TRUE(overrider->AppLogoURL().is_valid());

  EXPECT_EQ(overrider->InitialDelay(), base::Seconds(137.1));
  EXPECT_EQ(overrider->ServerKeepAliveTime(), base::Seconds(1));
  EXPECT_EQ(overrider->DictPolicies().size(), 2U);
  EXPECT_EQ(overrider->OverinstallTimeout(), base::Seconds(3));
  EXPECT_EQ(overrider->IdleCheckPeriod(), base::Seconds(4));
  EXPECT_EQ(overrider->CecaConnectionTimeout(), base::Seconds(27));
}

TEST_F(ExternalConstantsOverriderTest, TestOverrideUnwrappedURL) {
  base::Value::Dict overrides;
  overrides.Set(kDevOverrideKeyUrl, "https://localhost/1/www");
  auto overrider = base::MakeRefCounted<ExternalConstantsOverrider>(
      std::move(overrides), CreateDefaultExternalConstants());

  std::vector<GURL> urls = overrider->UpdateURL();
  ASSERT_EQ(urls.size(), 1ul);
  EXPECT_EQ(urls[0], GURL("https://localhost/1/www"));
  EXPECT_TRUE(urls[0].is_valid());

  // Non-overridden items should fall back to defaults
  EXPECT_TRUE(overrider->UseCUP());
  EXPECT_EQ(overrider->InitialDelay(), kInitialDelay);
  EXPECT_EQ(overrider->ServerKeepAliveTime(), kServerKeepAliveTime);
  EXPECT_EQ(overrider->DictPolicies().size(), 0U);
  EXPECT_EQ(overrider->CecaConnectionTimeout(), kCecaConnectionTimeout);
}

}  // namespace updater
