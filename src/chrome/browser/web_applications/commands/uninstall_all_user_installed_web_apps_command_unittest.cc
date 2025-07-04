// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/commands/uninstall_all_user_installed_web_apps_command.h"

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_delegate.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_delegate_factory.h"
#include "chrome/browser/ui/web_applications/test/isolated_web_app_test_utils.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_url_info.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_builder.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_test.h"
#include "chrome/browser/web_applications/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/test/fake_web_app_provider.h"
#include "chrome/browser/web_applications/test/mock_file_utils_wrapper.h"
#include "chrome/browser/web_applications/test/test_file_utils.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_test.h"
#include "chrome/browser/web_applications/test/web_app_test_observers.h"
#include "chrome/browser/web_applications/test/web_app_test_utils.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_command_manager.h"
#include "chrome/browser/web_applications/web_app_icon_manager.h"
#include "chrome/browser/web_applications/web_app_management_type.h"
#include "chrome/browser/web_applications/web_app_registry_update.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/web_package/signed_web_bundles/signed_web_bundle_id.h"
#include "content/public/browser/browsing_data_remover.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace web_app {

namespace {

void WaitForPendingDataClearingTasks(Profile* profile) {
  content::BrowsingDataRemover* browsing_data_remover =
      profile->GetBrowsingDataRemover();
  if (browsing_data_remover->GetPendingTaskCountForTesting() == 0) {
    return;
  }

  base::test::TestFuture<void> future;
  browsing_data_remover->SetWouldCompleteCallbackForTesting(
      base::BindLambdaForTesting([&](base::OnceClosure callback) {
        if (browsing_data_remover->GetPendingTaskCountForTesting() == 1) {
          future.SetValue();
        }
        std::move(callback).Run();
      }));
  CHECK(future.Wait());
}
}  // namespace

class UninstallAllUserInstalledWebAppsCommandTest : public IsolatedWebAppTest {
 public:
  UninstallAllUserInstalledWebAppsCommandTest()
      : IsolatedWebAppTest(WithDevMode{}) {}

  void SetUp() override {
    IsolatedWebAppTest::SetUp();
    test::AwaitStartWebAppProviderAndSubsystems(profile());
  }

  void TearDown() override {
    // IWAs will start a data clearing job when uninstalled, which needs to
    // complete before we delete the Profile.
    WaitForPendingDataClearingTasks(profile());
    IsolatedWebAppTest::TearDown();
  }

  WebAppProvider* web_app_provider() {
    return WebAppProvider::GetForTest(profile());
  }

  WebAppRegistrar& registrar_unsafe() {
    return web_app_provider()->registrar_unsafe();
  }
};

TEST_F(UninstallAllUserInstalledWebAppsCommandTest, NoUserInstalledWebApps) {
  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  {
    base::Value::Dict app_policy;
    app_policy.Set(web_app::kUrlKey, "https://example.com/install");
    ScopedListPrefUpdate update(profile()->GetPrefs(),
                                prefs::kWebAppInstallForceList);
    update->Append(std::move(app_policy));
  }
  webapps::AppId app_id = observer.Wait();

  base::test::TestFuture<const std::optional<std::string>&> future;
  web_app_provider()->command_manager().ScheduleCommand(
      std::make_unique<UninstallAllUserInstalledWebAppsCommand>(
          webapps::WebappUninstallSource::kHealthcareUserInstallCleanup,
          *profile(), future.GetCallback()));
  EXPECT_EQ(future.Get(), std::nullopt);

  EXPECT_EQ(proto::InstallState::INSTALLED_WITH_OS_INTEGRATION,
            registrar_unsafe().GetInstallState(app_id));
}

TEST_F(UninstallAllUserInstalledWebAppsCommandTest, RemovesUserInstallSources) {
  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  {
    base::Value::Dict app_policy;
    app_policy.Set(web_app::kUrlKey, "https://example.com/install");
    ScopedListPrefUpdate update(profile()->GetPrefs(),
                                prefs::kWebAppInstallForceList);
    update->Append(std::move(app_policy));
  }
  webapps::AppId app_id = observer.Wait();

  webapps::AppId sync_app_id = test::InstallDummyWebApp(
      profile(), "app from sync", GURL("https://example.com/install"),
      webapps::WebappInstallSource::SYNC);
  EXPECT_EQ(app_id, sync_app_id);

  const WebApp* web_app = registrar_unsafe().GetAppById(app_id);
  EXPECT_TRUE(web_app->GetSources().Has(WebAppManagement::kPolicy));
  EXPECT_TRUE(web_app->GetSources().Has(WebAppManagement::kSync));

  base::test::TestFuture<const std::optional<std::string>&> future;
  web_app_provider()->command_manager().ScheduleCommand(
      std::make_unique<UninstallAllUserInstalledWebAppsCommand>(
          webapps::WebappUninstallSource::kHealthcareUserInstallCleanup,
          *profile(), future.GetCallback()));
  EXPECT_EQ(future.Get(), std::nullopt);

  EXPECT_EQ(proto::InstallState::INSTALLED_WITH_OS_INTEGRATION,
            registrar_unsafe().GetInstallState(app_id));
  EXPECT_TRUE(web_app->GetSources().Has(WebAppManagement::kPolicy));
  EXPECT_FALSE(web_app->GetSources().Has(WebAppManagement::kSync));
}

TEST_F(UninstallAllUserInstalledWebAppsCommandTest,
       UninstallsUserInstalledWebApps) {
  webapps::AppId app_id1 = test::InstallDummyWebApp(
      profile(), "app from browser", GURL("https://example1.com"),
      webapps::WebappInstallSource::AUTOMATIC_PROMPT_BROWSER_TAB);

  webapps::AppId app_id2 = test::InstallDummyWebApp(
      profile(), "app from sync", GURL("https://example2.com"),
      webapps::WebappInstallSource::SYNC);

  const std::unique_ptr<web_app::ScopedBundledIsolatedWebApp> app_bundle3 =
      web_app::IsolatedWebAppBuilder(
          web_app::ManifestBuilder().SetName("iwa from installer"))
          .BuildBundle();
  app_bundle3->FakeInstallPageState(profile());
  app_bundle3->TrustSigningKey();
  webapps::AppId app_id3 = app_bundle3->InstallChecked(profile()).app_id();

  const std::unique_ptr<web_app::ScopedBundledIsolatedWebApp> app_bundle4 =
      web_app::IsolatedWebAppBuilder(
          web_app::ManifestBuilder().SetName("iwa from dev ui"))
          .BuildBundle();
  webapps::AppId app_id4 =
      app_bundle4
          ->InstallWithSource(profile(),
                              &IsolatedWebAppInstallSource::FromDevUi)
          .value()
          .app_id();

  const std::unique_ptr<web_app::ScopedBundledIsolatedWebApp> app_bundle5 =
      web_app::IsolatedWebAppBuilder(
          web_app::ManifestBuilder().SetName("iwa from dev command line"))
          .BuildBundle();
  webapps::AppId app_id5 =
      app_bundle5
          ->InstallWithSource(profile(),
                              &IsolatedWebAppInstallSource::FromDevCommandLine)
          .value()
          .app_id();

  base::test::TestFuture<const std::optional<std::string>&> future;
  web_app_provider()->command_manager().ScheduleCommand(
      std::make_unique<UninstallAllUserInstalledWebAppsCommand>(
          webapps::WebappUninstallSource::kHealthcareUserInstallCleanup,
          *profile(), future.GetCallback()));
  EXPECT_EQ(future.Get(), std::nullopt);

  EXPECT_FALSE(registrar_unsafe().IsInRegistrar(app_id1));
  EXPECT_FALSE(registrar_unsafe().IsInRegistrar(app_id2));
  EXPECT_FALSE(registrar_unsafe().IsInRegistrar(app_id3));
  EXPECT_FALSE(registrar_unsafe().IsInRegistrar(app_id4));
  EXPECT_FALSE(registrar_unsafe().IsInRegistrar(app_id5));

  // TODO(crbug.com/40277668): As a temporary fix to avoid race conditions with
  // `ScopedProfileKeepAlive`s, manually shutdown `KeyedService`s holding them.
  provider().Shutdown();
  ChromeBrowsingDataRemoverDelegateFactory::GetForProfile(profile())
      ->Shutdown();
}

class UninstallAllUserInstalledWebAppsCommandWithIconManagerTest
    : public UninstallAllUserInstalledWebAppsCommandTest {
 public:
  void SetUp() override {
    IsolatedWebAppTest::SetUp();

    file_utils_wrapper_ =
        base::MakeRefCounted<testing::NiceMock<MockFileUtilsWrapper>>();
    provider().SetFileUtils(file_utils_wrapper_);

    test::AwaitStartWebAppProviderAndSubsystems(profile());
  }

  void TearDown() override {
    file_utils_wrapper_ = nullptr;
    UninstallAllUserInstalledWebAppsCommandTest::TearDown();
  }

  scoped_refptr<testing::NiceMock<MockFileUtilsWrapper>> file_utils_wrapper_;
};

TEST_F(UninstallAllUserInstalledWebAppsCommandWithIconManagerTest,
       ReturnUninstallErrors) {
  EXPECT_CALL(*file_utils_wrapper_, WriteFile)
      .WillRepeatedly(testing::Return(true));

  webapps::AppId app_id = test::InstallDummyWebApp(
      profile(), "app from sync", GURL("https://example.com"),
      webapps::WebappInstallSource::SYNC);

  EXPECT_CALL(*file_utils_wrapper_, DeleteFileRecursively)
      .WillOnce(testing::Return(false));

  base::test::TestFuture<const std::optional<std::string>&> future;
  web_app_provider()->command_manager().ScheduleCommand(
      std::make_unique<UninstallAllUserInstalledWebAppsCommand>(
          webapps::WebappUninstallSource::kHealthcareUserInstallCleanup,
          *profile(), future.GetCallback()));
  EXPECT_EQ(future.Get(), app_id + "[Sync]: kError");

  EXPECT_FALSE(registrar_unsafe().IsInRegistrar(app_id));
}

}  // namespace web_app
