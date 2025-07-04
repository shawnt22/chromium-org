// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sharing/sharing_device_registration_impl.h"

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/notimplemented.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "components/gcm_driver/instance_id/instance_id_driver.h"
#include "components/prefs/pref_registry.h"
#include "components/prefs/pref_service_factory.h"
#include "components/sharing_message/features.h"
#include "components/sharing_message/pref_names.h"
#include "components/sharing_message/sharing_constants.h"
#include "components/sharing_message/sharing_device_registration_result.h"
#include "components/sharing_message/sharing_sync_preference.h"
#include "components/sync/test/test_sync_service.h"
#include "components/sync_device_info/device_info.h"
#include "components/sync_device_info/fake_device_info_sync_service.h"
#include "components/sync_preferences/pref_service_mock_factory.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "crypto/ec_private_key.h"
#include "google_apis/gcm/engine/account_mapping.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
const char kAppID[] = "test_app_id";
const char kSenderIdFCMToken[] = "sharing_fcm_token";
const char kSenderIdP256dh[] = "sharing_p256dh";
const char kSenderIdAuthSecret[] = "sharing_auth_secret";

class MockInstanceIDDriver : public instance_id::InstanceIDDriver {
 public:
  MockInstanceIDDriver() : InstanceIDDriver(/*gcm_driver=*/nullptr) {}

  MockInstanceIDDriver(const MockInstanceIDDriver&) = delete;
  MockInstanceIDDriver& operator=(const MockInstanceIDDriver&) = delete;

  ~MockInstanceIDDriver() override = default;

  MOCK_METHOD1(GetInstanceID,
               instance_id::InstanceID*(const std::string& app_id));
};

class FakeInstanceID : public instance_id::InstanceID {
 public:
  FakeInstanceID() : InstanceID(kAppID, /*gcm_driver = */ nullptr) {}
  ~FakeInstanceID() override = default;

  void GetID(GetIDCallback callback) override { NOTIMPLEMENTED(); }

  void GetCreationTime(GetCreationTimeCallback callback) override {
    NOTIMPLEMENTED();
  }

  void GetToken(const std::string& authorized_entity,
                const std::string& scope,
                base::TimeDelta time_to_live,
                std::set<Flags> flags,
                GetTokenCallback callback) override {
    ASSERT_EQ(authorized_entity, kSharingSenderID)
        << "Unexpected authorized_entity: " << authorized_entity;
    std::move(callback).Run(kSenderIdFCMToken, result_);
  }

  void ValidateToken(const std::string& authorized_entity,
                     const std::string& scope,
                     const std::string& token,
                     ValidateTokenCallback callback) override {
    NOTIMPLEMENTED();
  }

  void DeleteToken(const std::string& authorized_entity,
                   const std::string& scope,
                   DeleteTokenCallback callback) override {
    std::move(callback).Run(result_);
  }

  void DeleteTokenImpl(const std::string& authorized_entity,
                       const std::string& scope,
                       DeleteTokenCallback callback) override {
    NOTIMPLEMENTED();
  }

  void DeleteIDImpl(DeleteIDCallback callback) override { NOTIMPLEMENTED(); }

  void SetFCMResult(InstanceID::Result result) { result_ = result; }

  void GetEncryptionInfo(const std::string& authorized_entity,
                         GetEncryptionInfoCallback callback) override {
    if (authorized_entity != kSharingSenderID) {
      ADD_FAILURE() << "Unexpected authorized_entity: " << authorized_entity;
      return;
    }

    std::move(callback).Run(kSenderIdP256dh, kSenderIdAuthSecret);
  }

 private:
  InstanceID::Result result_;
};

class SharingDeviceRegistrationImplTest : public testing::Test {
 public:
  SharingDeviceRegistrationImplTest()
      : sync_prefs_(&prefs_, &fake_device_info_sync_service_),
        sharing_device_registration_(pref_service_.get(),
                                     &sync_prefs_,
                                     &mock_instance_id_driver_,
                                     &test_sync_service_) {
    SharingSyncPreference::RegisterProfilePrefs(prefs_.registry());
  }

  static std::unique_ptr<PrefService> CreatePrefServiceAndRegisterPrefs() {
    scoped_refptr<user_prefs::PrefRegistrySyncable> registry(
        new user_prefs::PrefRegistrySyncable());
    registry->RegisterBooleanPref(prefs::kSharedClipboardEnabled, true);
    PrefServiceFactory factory;
    factory.set_user_prefs(base::MakeRefCounted<TestingPrefStore>());
    return factory.Create(registry);
  }

  void SetUp() override {
    ON_CALL(mock_instance_id_driver_, GetInstanceID(testing::_))
        .WillByDefault(testing::Return(&fake_instance_id_));
  }

  void SetSharedClipboardPolicy(bool val) {
    pref_service_->SetBoolean(prefs::kSharedClipboardEnabled, val);
  }

  void RegisterDeviceSync() {
    base::RunLoop run_loop;
    sharing_device_registration_.RegisterDevice(
        base::BindLambdaForTesting([&](SharingDeviceRegistrationResult r) {
          result_ = r;
          local_sharing_info_ =
              SharingSyncPreference::GetLocalSharingInfoForSync(&prefs_);
          fcm_registration_ = sync_prefs_.GetFCMRegistration();
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void UnregisterDeviceSync() {
    base::RunLoop run_loop;
    sharing_device_registration_.UnregisterDevice(
        base::BindLambdaForTesting([&](SharingDeviceRegistrationResult r) {
          result_ = r;
          local_sharing_info_ =
              SharingSyncPreference::GetLocalSharingInfoForSync(&prefs_);
          fcm_registration_ = sync_prefs_.GetFCMRegistration();
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void SetInstanceIDFCMResult(instance_id::InstanceID::Result result) {
    fake_instance_id_.SetFCMResult(result);
  }

  std::set<sync_pb::SharingSpecificFields::EnabledFeatures>
  GetExpectedEnabledFeatures() {
    std::set<sync_pb::SharingSpecificFields::EnabledFeatures> features;

    // IsClickToCallSupported() involves JNI call which is hard to test.
    if (sharing_device_registration_.IsClickToCallSupported()) {
      features.insert(sync_pb::SharingSpecificFields::CLICK_TO_CALL_V2);
    }

    // Shared clipboard should always be supported.
    features.insert(sync_pb::SharingSpecificFields::SHARED_CLIPBOARD_V2);

    if (sharing_device_registration_.IsRemoteCopySupported()) {
      features.insert(sync_pb::SharingSpecificFields::REMOTE_COPY);
    }

    if (sharing_device_registration_.IsSmsFetcherSupported()) {
      features.insert(sync_pb::SharingSpecificFields::SMS_FETCHER);
    }

    if (supports_opt_guide()) {
      features.insert(
          sync_pb::SharingSpecificFields::OPTIMIZATION_GUIDE_PUSH_NOTIFICATION);
    }

    return features;
  }

  bool supports_opt_guide() const {
#if BUILDFLAG(IS_ANDROID)
    return true;
#else
    return false;
#endif
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  sync_preferences::TestingPrefServiceSyncable prefs_;
  testing::NiceMock<MockInstanceIDDriver> mock_instance_id_driver_;
  syncer::FakeDeviceInfoSyncService fake_device_info_sync_service_;
  FakeInstanceID fake_instance_id_;

  std::unique_ptr<PrefService> pref_service_ =
      CreatePrefServiceAndRegisterPrefs();
  SharingSyncPreference sync_prefs_;
  syncer::TestSyncService test_sync_service_;
  SharingDeviceRegistrationImpl sharing_device_registration_;

  // callback results
  std::optional<syncer::DeviceInfo::SharingInfo> local_sharing_info_;
  std::optional<SharingSyncPreference::FCMRegistration> fcm_registration_;
  SharingDeviceRegistrationResult result_;
};

}  // namespace

TEST_F(SharingDeviceRegistrationImplTest, IsSharedClipboardSupported_True) {
  SetSharedClipboardPolicy(true);

  EXPECT_TRUE(sharing_device_registration_.IsSharedClipboardSupported());
}

TEST_F(SharingDeviceRegistrationImplTest, IsSharedClipboardSupported_False) {
  SetSharedClipboardPolicy(false);

  EXPECT_FALSE(sharing_device_registration_.IsSharedClipboardSupported());
}

TEST_F(SharingDeviceRegistrationImplTest, RegisterDeviceTest_Success) {
  test_sync_service_.GetUserSettings()->SetSelectedTypes(
      /*sync_everything=*/false,
      /*types=*/{syncer::UserSelectableType::kPreferences});
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::SUCCESS);
  fake_device_info_sync_service_.GetDeviceInfoTracker()->Add(
      fake_device_info_sync_service_.GetLocalDeviceInfoProvider()
          ->GetLocalDeviceInfo());

  RegisterDeviceSync();

  std::set<sync_pb::SharingSpecificFields::EnabledFeatures> enabled_features =
      GetExpectedEnabledFeatures();
  syncer::DeviceInfo::SharingInfo expected_sharing_info(
      {kSenderIdFCMToken, kSenderIdP256dh, kSenderIdAuthSecret},
      /*chime_representative_target_id=*/std::string(), enabled_features);

  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_EQ(expected_sharing_info, local_sharing_info_);
  EXPECT_TRUE(fcm_registration_);
}

TEST_F(SharingDeviceRegistrationImplTest, RegisterDeviceTest_SenderIDOnly) {
  test_sync_service_.GetUserSettings()->SetSelectedTypes(
      /*sync_everything=*/false,
      /*types=*/syncer::UserSelectableTypeSet());
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::SUCCESS);
  fake_device_info_sync_service_.GetDeviceInfoTracker()->Add(
      fake_device_info_sync_service_.GetLocalDeviceInfoProvider()
          ->GetLocalDeviceInfo());

  RegisterDeviceSync();

  std::set<sync_pb::SharingSpecificFields::EnabledFeatures> enabled_features =
      GetExpectedEnabledFeatures();
  syncer::DeviceInfo::SharingInfo expected_sharing_info(
      {kSenderIdFCMToken, kSenderIdP256dh, kSenderIdAuthSecret},
      /*chime_representative_target_id=*/std::string(), enabled_features);

  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_EQ(expected_sharing_info, local_sharing_info_);
  EXPECT_TRUE(fcm_registration_);
}

TEST_F(SharingDeviceRegistrationImplTest, RegisterDeviceTest_InternalError) {
  // Make sync unavailable to force using vapid.
  test_sync_service_.SetFailedDataTypes({syncer::SHARING_MESSAGE});
  test_sync_service_.GetUserSettings()->SetSelectedTypes(
      /*sync_everything=*/false,
      /*types=*/syncer::UserSelectableTypeSet());
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::SUCCESS);
  fake_device_info_sync_service_.GetDeviceInfoTracker()->Add(
      fake_device_info_sync_service_.GetLocalDeviceInfoProvider()
          ->GetLocalDeviceInfo());

  RegisterDeviceSync();

  EXPECT_EQ(SharingDeviceRegistrationResult::kInternalError, result_);
  EXPECT_FALSE(local_sharing_info_);
  EXPECT_FALSE(fcm_registration_);
}

TEST_F(SharingDeviceRegistrationImplTest, RegisterDeviceTest_NetworkError) {
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::NETWORK_ERROR);

  RegisterDeviceSync();

  EXPECT_EQ(SharingDeviceRegistrationResult::kFcmTransientError, result_);
  EXPECT_FALSE(local_sharing_info_);
  EXPECT_FALSE(fcm_registration_);
}

TEST_F(SharingDeviceRegistrationImplTest, RegisterDeviceTest_FatalError) {
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::DISABLED);

  RegisterDeviceSync();

  EXPECT_EQ(SharingDeviceRegistrationResult::kFcmFatalError, result_);
  EXPECT_FALSE(local_sharing_info_);
  EXPECT_FALSE(fcm_registration_);
}

TEST_F(SharingDeviceRegistrationImplTest, UnregisterDeviceTest_Success) {
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::SUCCESS);
  fake_device_info_sync_service_.GetDeviceInfoTracker()->Add(
      fake_device_info_sync_service_.GetLocalDeviceInfoProvider()
          ->GetLocalDeviceInfo());

  // First register the device.
  RegisterDeviceSync();
  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_TRUE(local_sharing_info_);
  EXPECT_TRUE(fcm_registration_);

  // Then unregister the device.
  UnregisterDeviceSync();
  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_FALSE(local_sharing_info_);
  EXPECT_FALSE(fcm_registration_);

  // Further unregister does nothing and returns kDeviceNotRegistered.
  UnregisterDeviceSync();
  EXPECT_EQ(SharingDeviceRegistrationResult::kDeviceNotRegistered, result_);

  // Register the device again.
  RegisterDeviceSync();

  // Device should be registered with the new FCM token.
  std::set<sync_pb::SharingSpecificFields::EnabledFeatures> enabled_features =
      GetExpectedEnabledFeatures();
  syncer::DeviceInfo::SharingInfo expected_sharing_info(
      {kSenderIdFCMToken, kSenderIdP256dh, kSenderIdAuthSecret},
      /*chime_representative_target_id=*/std::string(), enabled_features);

  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_EQ(expected_sharing_info, local_sharing_info_);
  EXPECT_TRUE(fcm_registration_);
}

TEST_F(SharingDeviceRegistrationImplTest, UnregisterDeviceTest_SenderIDonly) {
  test_sync_service_.GetUserSettings()->SetSelectedTypes(
      /*sync_everything=*/false,
      /*types=*/syncer::UserSelectableTypeSet());
  SetInstanceIDFCMResult(instance_id::InstanceID::Result::SUCCESS);
  fake_device_info_sync_service_.GetDeviceInfoTracker()->Add(
      fake_device_info_sync_service_.GetLocalDeviceInfoProvider()
          ->GetLocalDeviceInfo());

  // First register the device.
  RegisterDeviceSync();
  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_TRUE(local_sharing_info_);
  EXPECT_TRUE(fcm_registration_);

  // Then unregister the device.
  UnregisterDeviceSync();
  EXPECT_EQ(SharingDeviceRegistrationResult::kSuccess, result_);
  EXPECT_FALSE(local_sharing_info_);
  EXPECT_FALSE(fcm_registration_);
}
