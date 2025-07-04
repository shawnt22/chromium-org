// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/demo_mode/demo_session.h"

#include <string_view>

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/scoped_observation.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/ash/drive/drive_integration_service.h"
#include "chrome/browser/ash/drive/drive_integration_service_factory.h"
#include "chrome/browser/ash/drive/drivefs_test_support.h"
#include "chrome/browser/ash/file_manager/path_util.h"
#include "chrome/browser/ash/login/demo_mode/demo_components.h"
#include "chrome/browser/ash/login/demo_mode/demo_setup_controller.h"
#include "chrome/browser/ash/login/login_manager_test.h"
#include "chrome/browser/ash/login/test/device_state_mixin.h"
#include "chrome/browser/ash/login/test/login_manager_mixin.h"
#include "chrome/browser/ash/login/test/oobe_base_test.h"
#include "chrome/browser/ash/login/test/scoped_policy_update.h"
#include "chrome/browser/ash/policy/core/device_local_account.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/ash/system_web_apps/system_web_app_manager.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/login/login_display_host.h"
#include "chrome/browser/ui/ash/system_web_apps/system_web_app_ui_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/test/base/browser_process_platform_part_test_api_chromeos.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/ash/components/demo_mode/utils/demo_session_utils.h"
#include "chromeos/constants/chromeos_features.h"
#include "chromeos/dbus/power/fake_power_manager_client.h"
#include "components/component_updater/ash/fake_component_manager_ash.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/prefs/pref_service.h"
#include "components/user_manager/user_manager.h"
#include "components/variations/active_field_trials.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "net/base/url_util.h"

namespace ash {
namespace {

inline constexpr char kAccountIdEmail[] = "public-session@test.com";

inline constexpr char kDemoModeAppUrl[] =
    "chrome-untrusted://demo-mode-app/index.html";

inline constexpr char kGrowthCampaignsComponentName[] = "growth-campaigns";
inline constexpr char kDemoResourceComponentName[] = "demo-mode-resources";
inline constexpr char kCampaignsFileName[] = "campaigns.json";
inline constexpr char kDemoMediaDirName[] = "media/photos";
inline constexpr char kDemoPhotoName[] = "photo.jpg";

// inline constexpr base::TimeDelta kDemoIdleTimeout = base::Seconds(90);

void SetDemoConfigPref(DemoSession::DemoModeConfig demo_config) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetInteger(prefs::kDemoModeConfig, static_cast<int>(demo_config));
}

void CheckDemoMode() {
  EXPECT_TRUE(ash::demo_mode::IsDeviceInDemoMode());
  EXPECT_EQ(DemoSession::DemoModeConfig::kOnline, DemoSession::GetDemoConfig());
}

void CheckNoDemoMode() {
  EXPECT_FALSE(ash::demo_mode::IsDeviceInDemoMode());
  EXPECT_EQ(DemoSession::DemoModeConfig::kNone, DemoSession::GetDemoConfig());

  SetDemoConfigPref(DemoSession::DemoModeConfig::kOnline);
  EXPECT_FALSE(ash::demo_mode::IsDeviceInDemoMode());
  EXPECT_EQ(DemoSession::DemoModeConfig::kNone, DemoSession::GetDemoConfig());
}

}  // namespace

// Tests locking device to policy::DEVICE_MODE_DEMO mode. It is an equivalent to
// going through online demo mode setup or using offline setup.
class DemoSessionDemoDeviceModeTest : public OobeBaseTest {
 public:
  DemoSessionDemoDeviceModeTest(const DemoSessionDemoDeviceModeTest&) = delete;
  DemoSessionDemoDeviceModeTest& operator=(
      const DemoSessionDemoDeviceModeTest&) = delete;

 protected:
  DemoSessionDemoDeviceModeTest() = default;
  ~DemoSessionDemoDeviceModeTest() override = default;

  // OobeBaseTest:
  void SetUpOnMainThread() override {
    OobeBaseTest::SetUpOnMainThread();
    SetDemoConfigPref(DemoSession::DemoModeConfig::kOnline);
  }

 private:
  DeviceStateMixin device_state_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_DEMO_MODE};
};

IN_PROC_BROWSER_TEST_F(DemoSessionDemoDeviceModeTest, IsDemoMode) {
  CheckDemoMode();
}

// Tests locking device to demo mode domain without policy::DEVICE_MODE_DEMO
// mode. It is an equivalent to enrolling device directly by using enterprise
// enrollment flow.
class DemoSessionDemoEnrolledDeviceTest : public OobeBaseTest {
 public:
  DemoSessionDemoEnrolledDeviceTest(const DemoSessionDemoEnrolledDeviceTest&) =
      delete;
  DemoSessionDemoEnrolledDeviceTest& operator=(
      const DemoSessionDemoEnrolledDeviceTest&) = delete;

 protected:
  DemoSessionDemoEnrolledDeviceTest() : OobeBaseTest() {
    device_state_.set_domain(policy::kDemoModeDomain);
  }

  ~DemoSessionDemoEnrolledDeviceTest() override = default;

  // OobeBaseTest:
  void SetUpOnMainThread() override {
    OobeBaseTest::SetUpOnMainThread();
    SetDemoConfigPref(DemoSession::DemoModeConfig::kOnline);
  }

 private:
  DeviceStateMixin device_state_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_CLOUD_ENROLLED};
};

IN_PROC_BROWSER_TEST_F(DemoSessionDemoEnrolledDeviceTest, IsDemoMode) {
  CheckDemoMode();
}

class DemoSessionNonDemoEnrolledDeviceTest : public OobeBaseTest {
 public:
  DemoSessionNonDemoEnrolledDeviceTest() = default;

  DemoSessionNonDemoEnrolledDeviceTest(
      const DemoSessionNonDemoEnrolledDeviceTest&) = delete;
  DemoSessionNonDemoEnrolledDeviceTest& operator=(
      const DemoSessionNonDemoEnrolledDeviceTest&) = delete;

  ~DemoSessionNonDemoEnrolledDeviceTest() override = default;

 private:
  DeviceStateMixin device_state_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_CLOUD_ENROLLED};
};

IN_PROC_BROWSER_TEST_F(DemoSessionNonDemoEnrolledDeviceTest, NotDemoMode) {
  CheckNoDemoMode();
}

class DemoSessionConsumerDeviceTest : public OobeBaseTest {
 public:
  DemoSessionConsumerDeviceTest() = default;

  DemoSessionConsumerDeviceTest(const DemoSessionConsumerDeviceTest&) = delete;
  DemoSessionConsumerDeviceTest& operator=(
      const DemoSessionConsumerDeviceTest&) = delete;

  ~DemoSessionConsumerDeviceTest() override = default;

 private:
  DeviceStateMixin device_state_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_CONSUMER_OWNED};
};

IN_PROC_BROWSER_TEST_F(DemoSessionConsumerDeviceTest, NotDemoMode) {
  CheckNoDemoMode();
}

class DemoSessionUnownedDeviceTest : public OobeBaseTest {
 public:
  DemoSessionUnownedDeviceTest() = default;

  DemoSessionUnownedDeviceTest(const DemoSessionUnownedDeviceTest&) = delete;
  DemoSessionUnownedDeviceTest& operator=(const DemoSessionUnownedDeviceTest&) =
      delete;

  ~DemoSessionUnownedDeviceTest() override = default;

 private:
  DeviceStateMixin device_state_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_UNOWNED};
};

IN_PROC_BROWSER_TEST_F(DemoSessionUnownedDeviceTest, NotDemoMode) {
  CheckNoDemoMode();
}

class DemoSessionActiveDirectoryDeviceTest : public OobeBaseTest {
 public:
  DemoSessionActiveDirectoryDeviceTest() = default;

  DemoSessionActiveDirectoryDeviceTest(
      const DemoSessionActiveDirectoryDeviceTest&) = delete;
  DemoSessionActiveDirectoryDeviceTest& operator=(
      const DemoSessionActiveDirectoryDeviceTest&) = delete;

  ~DemoSessionActiveDirectoryDeviceTest() override = default;

 private:
  DeviceStateMixin device_state_{
      &mixin_host_,
      DeviceStateMixin::State::OOBE_COMPLETED_ACTIVE_DIRECTORY_ENROLLED};
};

IN_PROC_BROWSER_TEST_F(DemoSessionActiveDirectoryDeviceTest, NotDemoMode) {
  CheckNoDemoMode();
}

/* ============================ Demo Login Tests =============================*/

// Extra parts for setting up the FakeComponentManagerAsh before the real one
// has been initialized on the browser
class DemoLoginTestMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  DemoLoginTestMainExtraParts() {
    CHECK(components_temp_dir_.CreateUniqueTempDir());
  }
  DemoLoginTestMainExtraParts(const DemoLoginTestMainExtraParts&) = delete;
  DemoLoginTestMainExtraParts& operator=(const DemoLoginTestMainExtraParts&) =
      delete;

  base::FilePath GetGrowthCampaignsPath() {
    return components_temp_dir_.GetPath()
        .AppendASCII("cros-components")
        .AppendASCII(kGrowthCampaignsComponentName);
  }

  base::FilePath GetDemoResourceComponentPath() {
    return components_temp_dir_.GetPath()
        .AppendASCII("cros-components")
        .AppendASCII(kDemoResourceComponentName);
  }

  void PostEarlyInitialization() override {
    auto component_manager_ash =
        base::MakeRefCounted<component_updater::FakeComponentManagerAsh>();
    component_manager_ash->set_supported_components(
        {"demo-mode-app", kGrowthCampaignsComponentName,
         kDemoResourceComponentName});
    component_manager_ash->ResetComponentState(
        "demo-mode-app",
        component_updater::FakeComponentManagerAsh::ComponentInfo(
            component_updater::ComponentManagerAsh::Error::NONE,
            base::FilePath("/dev/null"),
            base::FilePath("/run/imageloader/demo-mode-app")));
    component_manager_ash->ResetComponentState(
        "demo-mode-resources",
        component_updater::FakeComponentManagerAsh::ComponentInfo(
            component_updater::ComponentManagerAsh::Error::NONE,
            base::FilePath("/dev/null"), GetDemoResourceComponentPath()));
    component_manager_ash->ResetComponentState(
        "growth-campaigns",
        component_updater::FakeComponentManagerAsh::ComponentInfo(
            component_updater::ComponentManagerAsh::Error::NONE,
            base::FilePath("/dev/null"), GetGrowthCampaignsPath()));

    platform_part_test_api_ =
        std::make_unique<BrowserProcessPlatformPartTestApi>(
            g_browser_process->platform_part());
    platform_part_test_api_->InitializeComponentManager(
        std::move(component_manager_ash));
  }

  void PostMainMessageLoopRun() override {
    platform_part_test_api_->ShutdownComponentManager();
    platform_part_test_api_.reset();
  }

 private:
  std::unique_ptr<BrowserProcessPlatformPartTestApi> platform_part_test_api_;
  base::ScopedTempDir components_temp_dir_;
};

// Tests that involve asserting state about actual logged-in Demo sessions
//
// Currently this fixture enables the Demo SWA by default - consider extracting
// this feature enablement into a subclass if non-SWA tests are needed
class DemoSessionLoginTest : public LoginManagerTest,
                             public LocalStateMixin::Delegate,
                             public BrowserListObserver,
                             public user_manager::UserManager::Observer,
                             public chromeos::FakePowerManagerClient::Observer {
 public:
  DemoSessionLoginTest() {
    login_manager_mixin_.SetShouldLaunchBrowser(true);
    BrowserList::AddObserver(this);
  }

  ~DemoSessionLoginTest() override { BrowserList::RemoveObserver(this); }

  void CreatedBrowserMainParts(
      content::BrowserMainParts* browser_main_parts) override {
    auto extra_parts = std::make_unique<DemoLoginTestMainExtraParts>();
    growth_campaigns_mounted_path_ = extra_parts->GetGrowthCampaignsPath();
    demo_resource_mounted_path_ = extra_parts->GetDemoResourceComponentPath();
    static_cast<ChromeBrowserMainParts*>(browser_main_parts)
        ->AddParts(std::move(extra_parts));
    LoginManagerTest::CreatedBrowserMainParts(browser_main_parts);
  }

  void SetUpOnMainThread() override {
    std::unique_ptr<ScopedDevicePolicyUpdate> device_policy_update =
        device_state_mixin_.RequestDevicePolicyUpdate();

    enterprise_management::DeviceLocalAccountsProto* const
        device_local_accounts = device_policy_update->policy_payload()
                                    ->mutable_device_local_accounts();
    enterprise_management::DeviceLocalAccountInfoProto* const account =
        device_local_accounts->add_account();
    account->set_account_id(kAccountIdEmail);
    account->set_type(enterprise_management::DeviceLocalAccountInfoProto::
                          ACCOUNT_TYPE_PUBLIC_SESSION);
    device_local_accounts->set_auto_login_id(kAccountIdEmail);
    device_policy_update.reset();

    // Populate device_local_account policy cache with empty proto so policy
    // isn't marked as missing for the user, which causes
    // ExistingUserController::LoginAsPublicSession to wait endlessly on the
    // policy to be available. In browsertests, the device_local_account_policy
    // is never loaded again after initial device policy storage, likely because
    // policy fetches fail.
    std::unique_ptr<ScopedUserPolicyUpdate> device_local_account_policy_update =
        device_state_mixin_.RequestDeviceLocalAccountPolicyUpdate(
            kAccountIdEmail);
    device_local_account_policy_update.reset();

    // chromeos::PowerManagerClient::InitializeFake();
    chromeos::FakePowerManagerClient::Get()->set_keyboard_brightness_percent(
        kInitialBrightness);

    LoginManagerTest::SetUpOnMainThread();
  }

  void WaitForBrowserAdded() {
    base::RunLoop run_loop;
    on_browser_added_callback_ = run_loop.QuitClosure();
    run_loop.Run();
  }

 protected:
  // LocalStateMixin::Delegate
  void SetUpLocalState() override {
    SetDemoConfigPref(DemoSession::DemoModeConfig::kOnline);
  }

  // BrowserListObserver:
  void OnBrowserAdded(Browser* browser) override {
    if (on_browser_added_callback_) {
      std::move(on_browser_added_callback_).Run();
    }
  }

  void OpenBrowserAndInstallSystemAppForActiveProfile() {
    login_manager_mixin_.WaitForActiveSession();
    SystemWebAppManager::GetForTest(ProfileManager::GetActiveUserProfile())
        ->InstallSystemAppsForTesting();
    ui_test_utils::BrowserChangeObserver browser_opened(
        nullptr, ui_test_utils::BrowserChangeObserver::ChangeType::kAdded);
    browser_opened.Wait();
  }

  base::FilePath growth_campaigns_mounted_path() {
    return growth_campaigns_mounted_path_;
  }

  base::FilePath& demo_resource_mounted_path() {
    return demo_resource_mounted_path_;
  }

  LoginManagerMixin login_manager_mixin_{&mixin_host_};
  DeviceStateMixin device_state_mixin_{
      &mixin_host_, DeviceStateMixin::State::OOBE_COMPLETED_DEMO_MODE};
  LocalStateMixin local_state_mixin_{&mixin_host_, this};
  base::OnceClosure on_browser_added_callback_;
  static constexpr double kInitialBrightness = 20.0;
  base::FilePath growth_campaigns_mounted_path_;
  base::FilePath demo_resource_mounted_path_;
  base::WeakPtrFactory<DemoSessionLoginTest> weak_ptr_factory_{this};
};

IN_PROC_BROWSER_TEST_F(DemoSessionLoginTest, SessionStartup) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;
  login_manager_mixin_.WaitForActiveSession();
}

IN_PROC_BROWSER_TEST_F(DemoSessionLoginTest, DemoSWALaunchesOnSessionStartup) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;

  OpenBrowserAndInstallSystemAppForActiveProfile();

  // Verify that Demo Mode App is opened.
  Browser* app_browser = FindSystemWebAppBrowser(
      ProfileManager::GetActiveUserProfile(), SystemWebAppType::DEMO_MODE,
      Browser::TYPE_APP, GURL(kDemoModeAppUrl));
  ASSERT_TRUE(app_browser);
  content::WebContents* tab =
      app_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetController().GetVisibleEntry()->GetPageType(),
            content::PAGE_TYPE_NORMAL);
}

IN_PROC_BROWSER_TEST_F(
    DemoSessionLoginTest,
    DemoSessionKeyboardBrightnessIncreaseThreeTimesToOneHundredPercents) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;
  login_manager_mixin_.WaitForActiveSession();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(chromeos::FakePowerManagerClient::Get()
                ->num_increase_keyboard_brightness_calls(),
            3);
}

class DemoSessionLoginWithGrowthCampaignTest : public DemoSessionLoginTest {
 public:
  DemoSessionLoginWithGrowthCampaignTest() {
    scoped_feature_list_.InitWithFeatures(
        {features::kGrowthCampaignsInDemoMode, ash::features::kGrowthFramework},
        {});
  }

  void CreateTestCampaignsFile(std::string_view data) {
    auto campaigns_mounted_path = growth_campaigns_mounted_path();
    CHECK(base::CreateDirectory(campaigns_mounted_path));

    base::FilePath campaigns_file(
        campaigns_mounted_path.Append(kCampaignsFileName));
    CHECK(base::WriteFile(campaigns_file, data));
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(DemoSessionLoginWithGrowthCampaignTest,
                       DemoSWALaunchesOnSessionStartupWithPayload) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;

  CreateTestCampaignsFile(R"({
    "0": [
      {
        "id": 3,
        "studyId":1,
        "targetings": [],
        "payload": {
          "demoModeApp": {
            "attractionLoop": {
              "videoSrcLang1": "/asset/peripherals_lang1.mp4",
              "videoSrcLang2": "/asset/peripherals_lang2.mp4"
            }
          }
        }
      }
    ]
  })");

  OpenBrowserAndInstallSystemAppForActiveProfile();

  // Verify that Demo Mode App is opened with payload
  auto base_url = GURL(kDemoModeAppUrl);
  auto* param_value =
      R"({"attractionLoop":{"videoSrcLang1":"/asset/peripherals_lang1.mp4",)"
      R"("videoSrcLang2":"/asset/peripherals_lang2.mp4"}})";
  auto url = net::AppendQueryParameter(base_url, /*name=*/"model", param_value);
  Browser* app_browser = FindSystemWebAppBrowser(
      ProfileManager::GetActiveUserProfile(), SystemWebAppType::DEMO_MODE,
      Browser::TYPE_APP, url);
  ASSERT_TRUE(app_browser);

  content::WebContents* tab =
      app_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetController().GetVisibleEntry()->GetPageType(),
            content::PAGE_TYPE_NORMAL);
  EXPECT_TRUE(
      variations::IsInSyntheticTrialGroup("CrOSGrowthStudy1", "CampaignId3"));
}

IN_PROC_BROWSER_TEST_F(DemoSessionLoginWithGrowthCampaignTest,
                       DemoSWALaunchesOnSessionStartupWithoutPayload) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;

  CreateTestCampaignsFile(R"({
    "0": [
      {
        "id": 3,
        "targetings": [],
        "payload": {}
      }
    ]
  })");

  OpenBrowserAndInstallSystemAppForActiveProfile();

  // Verify that Demo Mode App is opened without payload.
  auto base_url = GURL(kDemoModeAppUrl);
  Browser* app_browser = FindSystemWebAppBrowser(
      ProfileManager::GetActiveUserProfile(), SystemWebAppType::DEMO_MODE,
      Browser::TYPE_APP, base_url);
  ASSERT_TRUE(app_browser);
  content::WebContents* tab =
      app_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetController().GetVisibleEntry()->GetPageType(),
            content::PAGE_TYPE_NORMAL);

  // Campaign is active with empty payload. Empty payload means the demo app
  // would be launched without params.
  EXPECT_TRUE(
      variations::IsInSyntheticTrialGroup("CrOSGrowthStudy", "CampaignId3"));
}

IN_PROC_BROWSER_TEST_F(DemoSessionLoginWithGrowthCampaignTest,
                       DemoSWALaunchesOnSessionStartupMismatch) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;

  CreateTestCampaignsFile(R"({
    "0": [
      {
        "id": 3,
        "studyId":1,
        "targetings": [
          {
            "demoMode": {
              "retailers": ["bby", "bestbuy", "bbt"],
              "storeIds": ["2", "4", "6"],
              "countries": ["US"],
              "capability": {
                "isCloudGamingDevice": true,
                "isFeatureAwareDevice": true
              }
            }
          }
        ],
        "payload": {
          "demoModeApp": {
            "attractionLoop": {
              "videoSrcLang1": "/asset/peripherals_lang1.mp4",
              "videoSrcLang2": "/asset/peripherals_lang2.mp4"
            }
          }
        }
      }
    ]
  })");

  OpenBrowserAndInstallSystemAppForActiveProfile();

  // Verify that Demo Mode App is opened without payload.
  auto base_url = GURL(kDemoModeAppUrl);
  Browser* app_browser = FindSystemWebAppBrowser(
      ProfileManager::GetActiveUserProfile(), SystemWebAppType::DEMO_MODE,
      Browser::TYPE_APP, base_url);
  ASSERT_TRUE(app_browser);
  content::WebContents* tab =
      app_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetController().GetVisibleEntry()->GetPageType(),
            content::PAGE_TYPE_NORMAL);
  EXPECT_FALSE(variations::HasSyntheticTrial("CrOSGrowthStudy1"));
}

IN_PROC_BROWSER_TEST_F(DemoSessionLoginWithGrowthCampaignTest,
                       DemoSWACampaignNoStudyId) {
  base::ScopedAllowBlockingForTesting scoped_allow_blocking;

  CreateTestCampaignsFile(R"({
    "0": [
      {
        "id": 3,
        "targetings": [],
        "payload": {
          "demoModeApp": {
            "attractionLoop": {
              "videoSrcLang1": "/asset/peripherals_lang1.mp4",
              "videoSrcLang2": "/asset/peripherals_lang2.mp4"
            }
          }
        }
      }
    ]
  })");

  OpenBrowserAndInstallSystemAppForActiveProfile();

  // Verify that Demo Mode App is opened with payload
  auto base_url = GURL(kDemoModeAppUrl);
  auto* param_value =
      R"({"attractionLoop":{"videoSrcLang1":"/asset/peripherals_lang1.mp4",)"
      R"("videoSrcLang2":"/asset/peripherals_lang2.mp4"}})";
  auto url = net::AppendQueryParameter(base_url, /*name=*/"model", param_value);
  Browser* app_browser = FindSystemWebAppBrowser(
      ProfileManager::GetActiveUserProfile(), SystemWebAppType::DEMO_MODE,
      Browser::TYPE_APP, url);
  ASSERT_TRUE(app_browser);

  content::WebContents* tab =
      app_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetController().GetVisibleEntry()->GetPageType(),
            content::PAGE_TYPE_NORMAL);
  EXPECT_TRUE(
      variations::IsInSyntheticTrialGroup("CrOSGrowthStudy", "CampaignId3"));
}

class DemoSessionLoginIdleHandlerTest : public DemoSessionLoginTest {
 public:
  DemoSessionLoginIdleHandlerTest() : DemoSessionLoginTest() {
    scoped_feature_list_.InitWithFeatures(
        {features::kDemoModeSignInFileCleanup}, {});

    login_manager_mixin_.AppendRegularUsers(1);
    demo_account_id_ = login_manager_mixin_.users()[0].account_id;
  }

  void SetUpOnMainThread() override {
    CreateTestMediaFile();

    // Setup DriveIntegrationService:
    create_drive_integration_service_ = base::BindRepeating(
        &DemoSessionLoginIdleHandlerTest::CreateDriveIntegrationService,
        base::Unretained(this));
    service_factory_for_test_ = std::make_unique<
        drive::DriveIntegrationServiceFactory::ScopedFactoryForTest>(
        &create_drive_integration_service_);

    drive::SetUpUserDataDirectoryForDriveFsTest(demo_account_id_);

    DemoSessionLoginTest::SetUpOnMainThread();
  }

  void CreateTestMediaFile() {
    const base::FilePath media_dir =
        demo_resource_mounted_path_.AppendASCII(kDemoMediaDirName);
    CHECK(base::CreateDirectory(media_dir));

    const base::FilePath photo(media_dir.Append(kDemoPhotoName));
    CHECK(base::WriteFile(photo, "random text"));
  }

  void FlushIOTasks() {
    base::RunLoop run_loop;
    DemoSession::Get()->GetBlockingTaskRunnerForTest()->PostTask(
        FROM_HERE, run_loop.QuitClosure());
    run_loop.Run();
  }

  drive::DriveIntegrationService* CreateDriveIntegrationService(
      Profile* profile) {
    // Ignore non-user profile.
    if (!ProfileHelper::IsUserProfile(profile)) {
      return nullptr;
    }

    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath mount_path = profile->GetPath().Append("drivefs");

    fake_drivefs_helper_ =
        std::make_unique<drive::FakeDriveFsHelper>(profile, mount_path);
    auto* integration_service = new drive::DriveIntegrationService(
        g_browser_process->local_state(), profile, std::string(), mount_path,
        fake_drivefs_helper_->CreateFakeDriveFsListenerFactory());
    return integration_service;
  }

  base::FilePath GetDriveFsAbsolutePath(const std::string& relative_path) {
    base::ScopedAllowBlockingForTesting allow_blocking;

    drive::DriveIntegrationService* service =
        drive::DriveIntegrationServiceFactory::FindForProfile(
            ProfileManager::GetActiveUserProfile());
    EXPECT_TRUE(service->IsMounted());
    EXPECT_TRUE(base::PathExists(service->GetMountPointPath()));

    base::FilePath root("/");
    base::FilePath absolute_path(service->GetMountPointPath());
    root.AppendRelativePath(base::FilePath(relative_path), &absolute_path);
    return absolute_path;
  }

  drivefs::FakeDriveFs* GetFakeDriveFs() {
    return &fake_drivefs_helper_->fake_drivefs();
  }

  // Creates file under the Drive relative `file_path`. Returns the absolute
  // path.
  base::FilePath CreateFileInDriveFsFolder(const std::string& file_path) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath relative_file_path(file_path);
    base::FilePath folder_path =
        GetDriveFsAbsolutePath(relative_file_path.DirName().value());

    // base::CreateDirectory returns 'true' on successful creation, or if the
    // directory already exists.
    EXPECT_TRUE(base::CreateDirectory(folder_path));
    base::FilePath absolute_path =
        folder_path.Append(relative_file_path.BaseName());
    CHECK(base::WriteFile(absolute_path, "random text"));
    return absolute_path;
  }

  AccountId demo_account_id_;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  // DriveFS test dependencies:
  drive::DriveIntegrationServiceFactory::FactoryCallback
      create_drive_integration_service_;
  std::unique_ptr<drive::DriveIntegrationServiceFactory::ScopedFactoryForTest>
      service_factory_for_test_;
  std::unique_ptr<drive::FakeDriveFsHelper> fake_drivefs_helper_;
};

// TODO(crbugs.com/406823191): Investigate the flaky and and re-enabled it.
IN_PROC_BROWSER_TEST_F(DemoSessionLoginIdleHandlerTest,
                       DISABLED_CleanUpLocalFiles) {
  demo_mode::SetForceEnableDemoAccountSignIn(true);

  // Mock login with demo account, which is a regular user.
  LoginUser(demo_account_id_);
  login_manager_mixin_.WaitForActiveSession();

  // Ensure media of resource components gets installed.
  FlushIOTasks();
  // Wait for idle handler get created at
  // `DemoSession::OnDemoAppComponentLoaded`:
  EXPECT_TRUE(base::test::RunUntil(
    []() { return DemoSession::Get()->GetIdleHandlerForTest(); }));

  //  Verify the photo was copied to download folder.
  auto* profile = ProfileManager::GetActiveUserProfile();
  base::FilePath downloads_path =
      file_manager::util::GetDownloadsFolderForProfile(profile);
  base::FilePath photo_file = downloads_path.AppendASCII(kDemoPhotoName);
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    EXPECT_TRUE(base::PathExists(photo_file));
  }

  // Shorten the timeout for testing.
  base::TimeDelta idle_timeout = base::Seconds(2);
  DemoSession::Get()->GetIdleHandlerForTest()->SetIdleTimeoutForTest(
      idle_timeout);

  // Mock user activity:
  ui::UserActivityDetector::Get()->HandleExternalUserActivity();
  base::FilePath user_created_dir_path;
  base::FilePath drive_fs_file;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    // Mock user creates a new folder under "MyFiles" and deletes the photo
    // files.
    base::ScopedTempDir user_created_dir;
    base::FilePath my_files_path = profile->GetPath().AppendASCII("MyFiles");
    EXPECT_TRUE(user_created_dir.CreateUniqueTempDirUnderPath(my_files_path));
    EXPECT_TRUE(base::DirectoryExists(user_created_dir.GetPath()));
    EXPECT_TRUE(base::DeleteFile(photo_file));
    user_created_dir_path = user_created_dir.GetPath();

    // Mock user creates a file under DriveFS:
    drive_fs_file = CreateFileInDriveFsFolder("/root/test1.txt");
    EXPECT_TRUE(base::PathExists(drive_fs_file));
  }

  // Wait idle timeout + 1s buffer for invoking the file clean up task.
  base::RunLoop run_loop{base::RunLoop::Type::kNestableTasksAllowed};
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), idle_timeout + base::Seconds(1));
  run_loop.Run();

  // Wait file clean up tasks to be finished.
  FlushIOTasks();
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    // Verify `user_created_dir` was deleted and photo was reset.
    EXPECT_TRUE(base::test::RunUntil([&user_created_dir_path]() {
      return !base::DirectoryExists(user_created_dir_path);
    }));
    EXPECT_TRUE(base::test::RunUntil(
        [&photo_file]() { return base::PathExists(photo_file); }));
    // Verify DriveFS file is deleted.
    EXPECT_TRUE(base::test::RunUntil(
        [&drive_fs_file]() { return !base::PathExists(drive_fs_file); }));
  }
}

}  // namespace ash
