// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/bluetooth/bluetooth_log_controller.h"

#include "base/test/task_environment.h"
#include "chromeos/ash/components/dbus/upstart/fake_upstart_client.h"
#include "chromeos/ash/components/dbus/upstart/upstart_client.h"
#include "components/account_id/account_id.h"
#include "components/prefs/testing_pref_service.h"
#include "components/user_manager/fake_user_manager.h"
#include "components/user_manager/test_helper.h"
#include "google_apis/gaia/gaia_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

class BluetoothLogControllerTest : public testing::Test {
 public:
  BluetoothLogControllerTest() = default;
  ~BluetoothLogControllerTest() override = default;

  void SetUp() override {
    UpstartClient::InitializeFake();
    user_manager::UserManagerImpl::RegisterPrefs(local_state_.registry());
  }

  void TearDown() override { UpstartClient::Shutdown(); }

  user_manager::FakeUserManager& user_manager() { return user_manager_; }
  BluetoothLogController& controller() { return controller_; }

 private:
  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple local_state_;
  user_manager::FakeUserManager user_manager_{&local_state_};
  BluetoothLogController controller_{&user_manager_};
};

TEST_F(BluetoothLogControllerTest, GoogleInternalUser) {
  auto* upstart_client = FakeUpstartClient::Get();
  upstart_client->StartRecordingUpstartOperations();

  auto* user = user_manager().AddGaiaUser(
      AccountId::FromUserEmailGaiaId("test@google.com", GaiaId("fakegaia")),
      user_manager::UserType::kRegular);
  // TODO(b/278643115): use UserManager::UserLoggedIn() to notify observer.
  controller().OnUserLoggedIn(*user);

  auto upstart_operations =
      upstart_client->GetRecordedUpstartOperationsForJob("bluetoothlog");
  ASSERT_EQ(1u, upstart_operations.size());
  EXPECT_EQ(FakeUpstartClient::UpstartOperationType::START,
            upstart_operations[0].type);
}

TEST_F(BluetoothLogControllerTest, NonGoolgeInternalUser) {
  auto* upstart_client = FakeUpstartClient::Get();
  upstart_client->StartRecordingUpstartOperations();

  auto* user = user_manager().AddGaiaUser(
      AccountId::FromUserEmailGaiaId("test@test.org", GaiaId("fakegaia")),
      user_manager::UserType::kRegular);
  // TODO(b/278643115): use UserManager::UserLoggedIn() to notify observer.
  controller().OnUserLoggedIn(*user);

  auto upstart_operations =
      upstart_client->GetRecordedUpstartOperationsForJob("bluetoothlog");
  EXPECT_EQ(0u, upstart_operations.size());
}

TEST_F(BluetoothLogControllerTest, NonRegularUser) {
  auto* upstart_client = FakeUpstartClient::Get();
  upstart_client->StartRecordingUpstartOperations();

  auto* user =
      user_manager::TestHelper(&user_manager())
          .AddKioskChromeAppUser("test@kiosk-apps.device-local.localhost");
  // TODO(b/278643115): use UserManager::UserLoggedIn() to notify observer.
  controller().OnUserLoggedIn(*user);

  auto upstart_operations =
      upstart_client->GetRecordedUpstartOperationsForJob("bluetoothlog");
  EXPECT_EQ(0u, upstart_operations.size());
}

}  // namespace ash
