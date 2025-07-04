// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/browser_with_test_window_test.h"

#include <memory>
#include <vector>

#include "base/command_line.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile_destroyer.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/test_utils.h"
#include "ui/base/page_transition_types.h"

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/ui/views/chrome_constrained_window_views_client.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/views/test/views_test_utils.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/crosapi/crosapi_manager.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/browser_process.h"
#include "chromeos/ash/components/browser_context_helper/annotated_account_id.h"
#include "chromeos/ash/components/dbus/cros_disks/cros_disks_client.h"
#include "chromeos/ash/components/disks/disk_mount_manager.h"
#include "chromeos/ash/components/disks/fake_disk_mount_manager.h"
#include "components/user_manager/fake_user_manager.h"
#include "components/user_manager/scoped_user_manager.h"
#include "components/user_manager/test_helper.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/context_factory.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_id.h"
#endif

using content::NavigationController;
using content::RenderFrameHost;
using content::RenderFrameHostTester;
using content::WebContents;

BrowserWithTestWindowTest::~BrowserWithTestWindowTest() = default;

void BrowserWithTestWindowTest::SetUp() {
  testing::Test::SetUp();

  base::CommandLine::ForCurrentProcess()->AppendSwitch(switches::kNoFirstRun);
  if (!profile_manager_) {
    SetUpProfileManager();
  }

#if BUILDFLAG(IS_CHROMEOS)
  ash::CrosDisksClient::InitializeFake();
  if (!ash::disks::DiskMountManager::GetInstance()) {
    ash::disks::DiskMountManager::InitializeForTesting(
        new ash::disks::FakeDiskMountManager());
  }
  if (!user_manager::UserManager::IsInitialized()) {
    user_manager_.Reset(std::make_unique<user_manager::FakeUserManager>(
        g_browser_process->local_state()));
  }
  {
    ash::AshTestHelper::InitParams ash_init;
    ash_init.local_state = g_browser_process->local_state();
    ash_init.start_session = false;

    // Do not auto create user pref service. PrefService will be created by
    // TestingProfile.
    ash_init.auto_create_prefs_services = false;

    ash_test_helper_.SetUp(std::move(ash_init));
  }
#endif

  // This must be created after |ash_test_helper_| is set up so that it doesn't
  // create a DeviceDataManager.
  rvh_test_enabler_ = std::make_unique<content::RenderViewHostTestEnabler>();

#if defined(TOOLKIT_VIEWS)
  SetConstrainedWindowViewsClient(CreateChromeConstrainedWindowViewsClient());
#endif

  user_performance_tuning_manager_environment_.SetUp(
      TestingBrowserProcess::GetGlobal()->local_state());

#if BUILDFLAG(IS_CHROMEOS)
  manager_ = std::make_unique<crosapi::CrosapiManager>();
  kiosk_chrome_app_manager_ = std::make_unique<ash::KioskChromeAppManager>();
#endif

  // Subclasses can provide their own Profile name.
  std::optional<std::string> profile_name = GetDefaultProfileName();
  if (profile_name) {
#if BUILDFLAG(IS_CHROMEOS)
    LogIn(*profile_name, GaiaId("fakegaia"));
#endif
    profile_ = CreateProfile(*profile_name)->GetWeakPtr();
#if BUILDFLAG(IS_CHROMEOS)
    SwitchActiveUser(*profile_name);
#endif

    window_ = CreateBrowserWindow();

    browser_ =
        CreateBrowser(profile(), browser_type_, hosted_app_, window_.get());
  }
}

void BrowserWithTestWindowTest::TearDown() {
  // Some tests end up posting tasks to the DB thread that must be completed
  // before the profile can be destroyed and the test safely shut down.
  base::RunLoop().RunUntilIdle();

  // Close the browser tabs and destroy the browser and window instances.
  if (browser_) {
    browser_->tab_strip_model()->CloseAllTabs();
    browser_->GetFeatures().TearDownPreBrowserWindowDestruction();
    browser_.reset();
  }
  window_.reset();

#if defined(TOOLKIT_VIEWS)
  constrained_window::SetConstrainedWindowViewsClient(nullptr);
#endif

  // Depends on LocalState owned by |profile_manager_|.
  if (SystemNetworkContextManager::GetInstance()) {
    SystemNetworkContextManager::DeleteInstance();
  }

#if BUILDFLAG(IS_CHROMEOS)
  manager_.reset();
  kiosk_chrome_app_manager_.reset();
#endif

  user_performance_tuning_manager_environment_.TearDown();

#if BUILDFLAG(IS_CHROMEOS)
  ash_test_helper_.TearDown();
#endif

  // Calling DeleteAllTestingProfiles() first can cause issues in some tests, if
  // they're still holding a ScopedProfileKeepAlive.
  profile_ = nullptr;
  profile_manager_.reset();

#if BUILDFLAG(IS_CHROMEOS)
  test_views_delegate_.reset();
  user_manager_.Reset();
  ash::disks::DiskMountManager::Shutdown();
  ash::CrosDisksClient::Shutdown();
#elif defined(TOOLKIT_VIEWS)
  views_test_helper_.reset();
#endif

  testing::Test::TearDown();

  // A Task is leaked if we don't destroy everything, then run all pending
  // tasks. This includes backend tasks which could otherwise be affected by the
  // deletion of the temp dir.
  task_environment_->RunUntilIdle();
}

void BrowserWithTestWindowTest::SetUpProfileManager(
    const base::FilePath& profiles_path,
    std::unique_ptr<ProfileManager> profile_manager) {
  profile_manager_ = std::make_unique<TestingProfileManager>(
      TestingBrowserProcess::GetGlobal());
#if BUILDFLAG(IS_CHROMEOS)
  profile_manager_->set_on_profile_created_callback(
      base::BindRepeating(&BrowserWithTestWindowTest::PostUserProfileCreation,
                          base::Unretained(this)));
#endif
  ASSERT_TRUE(
      profile_manager_->SetUp(profiles_path, std::move(profile_manager)));
}

gfx::NativeWindow BrowserWithTestWindowTest::GetContext() {
#if BUILDFLAG(IS_CHROMEOS)
  return ash_test_helper_.GetContext();
#elif defined(TOOLKIT_VIEWS)
  return views_test_helper_->GetContext();
#else
  return nullptr;
#endif
}

void BrowserWithTestWindowTest::AddTab(Browser* browser, const GURL& url) {
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
  params.tabstrip_index = 0;
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  CommitPendingLoad(&params.navigated_or_inserted_contents->GetController());
#if defined(TOOLKIT_VIEWS)
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (browser_view) {
    views::test::RunScheduledLayout(browser_view);
  }
#endif
}

void BrowserWithTestWindowTest::CommitPendingLoad(
    NavigationController* controller) {
  if (!controller->GetPendingEntry()) {
    return;  // Nothing to commit.
  }

  RenderFrameHostTester::CommitPendingLoad(controller);
}

void BrowserWithTestWindowTest::NavigateAndCommit(WebContents* web_contents,
                                                  const GURL& url) {
  content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents, url);
}

void BrowserWithTestWindowTest::NavigateAndCommitActiveTab(const GURL& url) {
  NavigateAndCommit(browser()->tab_strip_model()->GetActiveWebContents(), url);
}

void BrowserWithTestWindowTest::NavigateAndCommitActiveTabWithTitle(
    Browser* navigating_browser,
    const GURL& url,
    const std::u16string& title) {
  WebContents* contents =
      navigating_browser->tab_strip_model()->GetActiveWebContents();
  NavigateAndCommit(contents, url);
  contents->UpdateTitleForEntry(contents->GetController().GetActiveEntry(),
                                title);
}

void BrowserWithTestWindowTest::FocusMainFrameOfActiveWebContents() {
  WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  content::FocusWebContentsOnFrame(contents, contents->GetPrimaryMainFrame());
}

std::optional<std::string> BrowserWithTestWindowTest::GetDefaultProfileName() {
  return TestingProfile::kDefaultProfileUserName;
}

TestingProfile* BrowserWithTestWindowTest::CreateProfile(
    const std::string& profile_name) {
  auto* profile = profile_manager_->CreateTestingProfile(
      profile_name, /*prefs=*/nullptr, /*user_name=*/std::u16string(),
      /*avatar_id=*/0, GetTestingFactories());
  return profile;
}

void BrowserWithTestWindowTest::DeleteProfile(const std::string& profile_name) {
  if (profile_name == GetDefaultProfileName()) {
    if (browser_) {
      browser_->tab_strip_model()->CloseAllTabs();
      browser_.reset();
    }
    profile_ = nullptr;
  }
  profile_manager_->DeleteTestingProfile(profile_name);
}

TestingProfile::TestingFactories
BrowserWithTestWindowTest::GetTestingFactories() {
  return {};
}

std::unique_ptr<BrowserWindow>
BrowserWithTestWindowTest::CreateBrowserWindow() {
  return std::make_unique<TestBrowserWindow>();
}

std::unique_ptr<Browser> BrowserWithTestWindowTest::CreateBrowser(
    Profile* profile,
    Browser::Type browser_type,
    bool hosted_app,
    BrowserWindow* browser_window) {
  Browser::CreateParams params(profile, true);
  if (hosted_app) {
    params = Browser::CreateParams::CreateForApp(
        "Test", /*trusted_source=*/true, /*window_bounds=*/gfx::Rect(), profile,
        /*user_gesture=*/true);
  } else if (browser_type == Browser::TYPE_DEVTOOLS) {
    params = Browser::CreateParams::CreateForDevTools(profile);
  } else {
    params.type = browser_type;
  }
  params.window = browser_window;
  return Browser::DeprecatedCreateOwnedForTesting(params);
}

#if BUILDFLAG(IS_CHROMEOS)
void BrowserWithTestWindowTest::LogIn(std::string_view email,
                                      const GaiaId& gaia_id) {
  const AccountId account_id = AccountId::FromUserEmailGaiaId(email, gaia_id);
  user_manager_->AddGaiaUser(account_id, user_manager::UserType::kRegular);
  user_manager_->UserLoggedIn(
      account_id, user_manager::TestHelper::GetFakeUsernameHash(account_id));
}

void BrowserWithTestWindowTest::OnUserProfileCreated(const std::string& email,
                                                     Profile* profile) {
  CHECK(profile);
  // TODO(b/40225390): Unset for_test explicit param after subclasses are
  // migrated.
  AccountId account_id = AccountId::FromUserEmail(email);
  ash::AnnotatedAccountId::Set(profile, account_id,
                               /*for_test=*/false);
  // Do not use the member directly, because another UserManager instance
  // may be injected.
  auto* user_manager = user_manager::UserManager::Get();
  user_manager->OnUserProfileCreated(account_id, profile->GetPrefs());
  GetSessionControllerClient()->SetUnownedUserPrefService(account_id,
                                                          profile->GetPrefs());
  auto observation =
      std::make_unique<base::ScopedObservation<Profile, ProfileObserver>>(this);
  observation->Observe(profile);
  profile_observations_.push_back(std::move(observation));
}

void BrowserWithTestWindowTest::SwitchActiveUser(const std::string& email) {
  GetSessionControllerClient()->SwitchActiveUser(
      AccountId::FromUserEmail(email));
  GetSessionControllerClient()->SetSessionState(
      session_manager::SessionState::ACTIVE);
}

void BrowserWithTestWindowTest::OnProfileWillBeDestroyed(Profile* profile) {
  CHECK(
      std::erase_if(profile_observations_, [profile](const auto& observation) {
        return observation->IsObservingSource(profile);
      }));
  const AccountId* account_id = ash::AnnotatedAccountId::Get(profile);
  CHECK(account_id);
  // Do not use the member directly, because another UserManager instance
  // may be injected.
  user_manager::UserManager::Get()->OnUserProfileWillBeDestroyed(*account_id);
}

ash::ScopedCrosSettingsTestHelper*
BrowserWithTestWindowTest::GetCrosSettingsHelper() {
  return &cros_settings_test_helper_;
}

ash::StubInstallAttributes* BrowserWithTestWindowTest::GetInstallAttributes() {
  return GetCrosSettingsHelper()->InstallAttributes();
}

ash::TestSessionControllerClient*
BrowserWithTestWindowTest::GetSessionControllerClient() {
  return ash_test_helper()->test_session_controller_client(
      base::PassKey<BrowserWithTestWindowTest>());
}

void BrowserWithTestWindowTest::PostUserProfileCreation(
    const std::string& email,
    Profile* profile) {
  // The test profile is not for gaia login.
  if (email != gaia::CanonicalizeEmail(email)) {
    return;
  }
  auto* user = user_manager::UserManager::Get()->FindUser(
      AccountId::FromUserEmail(email));
  if (user) {
    OnUserProfileCreated(email, profile);
    GetSessionControllerClient()->AddUserSession({email, user->GetType()});
  }
}

#endif  // BUILDFLAG(IS_CHROMEOS)

BrowserWithTestWindowTest::BrowserWithTestWindowTest(
    std::unique_ptr<content::BrowserTaskEnvironment> task_environment,
    Browser::Type browser_type,
    bool hosted_app)
    : task_environment_(std::move(task_environment)),
      browser_type_(browser_type),
      hosted_app_(hosted_app) {}
