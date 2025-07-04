// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/collaboration/model/messaging/messaging_backend_service_factory.h"

#import "base/test/scoped_feature_list.h"
#import "components/data_sharing/public/features.h"
#import "ios/chrome/browser/saved_tab_groups/model/tab_group_sync_service_factory.h"
#import "ios/chrome/browser/shared/model/profile/test/test_profile_ios.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/test/ios_chrome_scoped_testing_local_state.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/platform_test.h"

namespace collaboration::messaging {

class MessagingBackendServiceFactoryTest : public PlatformTest {
 public:
  MessagingBackendServiceFactoryTest() {
    scoped_feature_list_.InitWithFeatures(
        /*enabled_features=*/
        {
            kTabGroupSync,
            data_sharing::features::kDataSharingFeature,
        },
        /*disable_features=*/{});
    TestProfileIOS::Builder builder;
    builder.AddTestingFactory(
        tab_groups::TabGroupSyncServiceFactory::GetInstance(),
        tab_groups::TabGroupSyncServiceFactory::GetDefaultFactory());
    profile_ = std::move(builder).Build();
  }

  ~MessagingBackendServiceFactoryTest() override = default;

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;
  web::WebTaskEnvironment task_environment_;
  IOSChromeScopedTestingLocalState scoped_testing_local_state_;
  std::unique_ptr<TestProfileIOS> profile_;
};

// Tests the creation of the service in regular.
TEST_F(MessagingBackendServiceFactoryTest, ServiceCreatedInRegularProfile) {
  MessagingBackendService* service =
      MessagingBackendServiceFactory::GetForProfile(profile_.get());
  EXPECT_TRUE(service);
}

// Tests that the factory is returning a nil pointer for incognito.
TEST_F(MessagingBackendServiceFactoryTest, ServiceNotCreatedInIncognito) {
  MessagingBackendService* service =
      MessagingBackendServiceFactory::GetForProfile(
          profile_->GetOffTheRecordProfile());
  EXPECT_FALSE(service);
}

}  // namespace collaboration::messaging
