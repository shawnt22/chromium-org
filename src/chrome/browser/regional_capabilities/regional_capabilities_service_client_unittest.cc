// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/regional_capabilities/regional_capabilities_service_client.h"

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "chrome/browser/regional_capabilities/regional_capabilities_test_environment.h"
#include "components/country_codes/country_codes.h"
#include "components/variations/pref_names.h"
#include "components/variations/service/test_variations_service.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::country_codes::CountryId;

namespace regional_capabilities {
namespace {

class RegionalCapabilitiesServiceClientTest : public testing::Test {
 public:
  RegionalCapabilitiesServiceClientTest() = default;

  ~RegionalCapabilitiesServiceClientTest() override = default;

 protected:
  base::test::TaskEnvironment task_environment_;

  RegionalCapabilitiesTestEnvironment rcaps_env_;
};

TEST_F(RegionalCapabilitiesServiceClientTest, GetVariationsLatestCountryId) {
  // Set up variations_service::GetLatestCountry().
  rcaps_env_.pref_service().SetString(variations::prefs::kVariationsCountry,
                                      "fr");

  RegionalCapabilitiesServiceClient client(&rcaps_env_.variations_service());

  EXPECT_EQ(client.GetVariationsLatestCountryId(), CountryId("FR"));
}

TEST_F(RegionalCapabilitiesServiceClientTest,
       GetVariationsLatestCountryIdWithoutVariationsService) {
  RegionalCapabilitiesServiceClient client(/* variations_service */ nullptr);

  EXPECT_EQ(client.GetVariationsLatestCountryId(), CountryId());
}

TEST_F(RegionalCapabilitiesServiceClientTest, GetFallbackCountryId) {
  RegionalCapabilitiesServiceClient client(&rcaps_env_.variations_service());

  EXPECT_EQ(client.GetFallbackCountryId(),
            country_codes::GetCurrentCountryID());
}

TEST_F(RegionalCapabilitiesServiceClientTest,
       GetFallbackCountryIdWithoutVariationsService) {
  RegionalCapabilitiesServiceClient client(/* variations_service */ nullptr);

  EXPECT_EQ(client.GetFallbackCountryId(),
            country_codes::GetCurrentCountryID());
}

TEST_F(RegionalCapabilitiesServiceClientTest, FetchCountryId) {
  RegionalCapabilitiesServiceClient client(&rcaps_env_.variations_service());

  base::test::TestFuture<CountryId> future;
  client.FetchCountryId(future.GetCallback());
  EXPECT_EQ(future.Get(), country_codes::GetCurrentCountryID());
}

TEST_F(RegionalCapabilitiesServiceClientTest,
       FetchCountryIdWithoutVariationsService) {
  RegionalCapabilitiesServiceClient client(/* variations_service */ nullptr);

  base::test::TestFuture<CountryId> future;
  client.FetchCountryId(future.GetCallback());
  EXPECT_EQ(future.Get(), country_codes::GetCurrentCountryID());
}

}  // namespace
}  // namespace regional_capabilities
