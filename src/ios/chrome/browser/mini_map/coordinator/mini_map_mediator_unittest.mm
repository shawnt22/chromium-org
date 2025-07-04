// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/mini_map/coordinator/mini_map_mediator.h"

#import "base/ios/ios_util.h"
#import "base/test/scoped_feature_list.h"
#import "components/sync_preferences/testing_pref_service_syncable.h"
#import "ios/chrome/browser/mini_map/coordinator/mini_map_mediator_delegate.h"
#import "ios/chrome/browser/shared/model/prefs/browser_prefs.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/model/profile/test/test_profile_ios.h"
#import "ios/web/common/features.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/gmock/include/gmock/gmock.h"
#import "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#import "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"

class MiniMapMediatorTest : public PlatformTest {
 protected:
  MiniMapMediatorTest() {
    TestProfileIOS::Builder builder;
    builder.SetPrefService(CreatePrefService());
    profile_ = std::move(builder).Build();

    delegate_ = OCMStrictProtocolMock(@protocol(MiniMapMediatorDelegate));

    mediator_ = [[MiniMapMediator alloc] initWithPrefs:profile_->GetPrefs()
                                              webState:nullptr];
    mediator_.delegate = delegate_;
  }

  std::unique_ptr<sync_preferences::PrefServiceSyncable> CreatePrefService() {
    auto prefs =
        std::make_unique<sync_preferences::TestingPrefServiceSyncable>();
    user_prefs::PrefRegistrySyncable* registry = prefs->registry();
    RegisterProfilePrefs(registry);
    return prefs;
  }

  void TearDown() override {
    EXPECT_OCMOCK_VERIFY(delegate_);
    PlatformTest::TearDown();
  }

 protected:
  web::WebTaskEnvironment environment_;
  std::unique_ptr<TestProfileIOS> profile_;
  id delegate_;
  MiniMapMediator* mediator_;
};

// Tests that consent screen is not triggered if not needed.
TEST_F(MiniMapMediatorTest, TestNoConsentNeeded) {
  if (!base::ios::IsRunningOnOrLater(16, 4, 0)) {
    GTEST_SKIP() << "Feature only available on iOS16.4+";
  }

  profile_->GetPrefs()->SetBoolean(prefs::kDetectAddressesAccepted, false);
  profile_->GetPrefs()->SetBoolean(prefs::kDetectAddressesEnabled, true);
  OCMExpect([delegate_ showMapWithIPH:NO]);
  [mediator_ userInitiatedMiniMapWithIPH:NO];
}

// Tests that consent screen is not triggered but IPH is displayed.
TEST_F(MiniMapMediatorTest, TestConsentIPH) {
  if (!base::ios::IsRunningOnOrLater(16, 4, 0)) {
    GTEST_SKIP() << "Feature only available on iOS16.4+";
  }

  profile_->GetPrefs()->SetBoolean(prefs::kDetectAddressesAccepted, false);
  profile_->GetPrefs()->SetBoolean(prefs::kDetectAddressesEnabled, true);
  OCMExpect([delegate_ showMapWithIPH:YES]);
  [mediator_ userInitiatedMiniMapWithIPH:YES];

  environment_.RunUntilIdle();
  EXPECT_TRUE(
      profile_->GetPrefs()->GetBoolean(prefs::kDetectAddressesAccepted));
  EXPECT_TRUE(profile_->GetPrefs()->GetBoolean(prefs::kDetectAddressesEnabled));
}
