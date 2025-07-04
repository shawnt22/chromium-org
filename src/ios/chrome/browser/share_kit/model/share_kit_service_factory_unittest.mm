// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/share_kit/model/share_kit_service_factory.h"

#import "base/test/scoped_feature_list.h"
#import "components/data_sharing/public/features.h"
#import "components/saved_tab_groups/test_support/mock_tab_group_sync_service.h"
#import "ios/chrome/browser/saved_tab_groups/model/tab_group_sync_service_factory.h"
#import "ios/chrome/browser/shared/model/profile/test/test_profile_ios.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/test/ios_chrome_scoped_testing_local_state.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/platform_test.h"

namespace {

// Creates a MockTabGroupSyncService.
std::unique_ptr<KeyedService> CreateMockSyncService(
    web::BrowserState* context) {
  return std::make_unique<
      ::testing::NiceMock<tab_groups::MockTabGroupSyncService>>();
}

}  // namespace

// Test fixture for the share kit service factory.
class ShareKitServiceFactoryTest : public PlatformTest {
 protected:
  ShareKitServiceFactoryTest() {
    scoped_feature_list_.InitWithFeatures(
        /*enabled_features=*/
        {
            kTabGroupSync,
            data_sharing::features::kDataSharingJoinOnly,
        },
        /*disable_features=*/{});

    TestProfileIOS::Builder builder;
    builder.AddTestingFactory(
        tab_groups::TabGroupSyncServiceFactory::GetInstance(),
        base::BindRepeating(&CreateMockSyncService));
    profile_ = std::move(builder).Build();
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  web::WebTaskEnvironment task_environment_;
  IOSChromeScopedTestingLocalState scoped_testing_local_state_;
  std::unique_ptr<TestProfileIOS> profile_;
};

// Tests that the factory isn't returning a service in incognito.
TEST_F(ShareKitServiceFactoryTest, NoProfileInIncognito) {
  ShareKitService* regular_service =
      ShareKitServiceFactory::GetForProfile(profile_.get());
  ShareKitService* off_the_record_service =
      ShareKitServiceFactory::GetForProfile(profile_->GetOffTheRecordProfile());
  EXPECT_NE(nullptr, regular_service);
  EXPECT_EQ(nullptr, off_the_record_service);
}
