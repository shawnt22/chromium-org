// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/notimplemented.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_model_delegate.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_delegate.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_delegate_factory.h"
#include "chrome/browser/device_api/device_attribute_api.h"
#include "chrome/browser/device_api/device_service_impl.h"
#include "chrome/browser/ui/web_applications/test/isolated_web_app_test_utils.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_url_info.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_builder.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_test.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/iwa_test_server_configurator.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/policy_generator.h"
#include "chrome/browser/web_applications/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/test/fake_web_contents_manager.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_test.h"
#include "chrome/browser/web_applications/test/web_app_test_observers.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/account_id/account_id.h"
#include "components/permissions/features.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/profile_metrics/browser_profile_type.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/web_contents_tester.h"
#include "net/base/features.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "base/test/scoped_command_line.h"
#include "chrome/browser/ash/app_mode/web_app/kiosk_web_app_manager.h"
#include "chrome/browser/ash/login/users/fake_chrome_user_manager.h"
#include "chrome/common/chrome_switches.h"
#include "components/user_manager/scoped_user_manager.h"
#include "components/user_manager/test_helper.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace {

constexpr char kDefaultAppInstallUrl[] = "https://example.com/install";
constexpr char kTrustedUrl[] = "https://example.com/sample";
constexpr char kUntrustedUrl[] = "https://non-example.com/sample";
constexpr char kKioskAppInstallUrl[] = "https://kiosk.com/install";
constexpr char kUserEmail[] = "user-email@example.com";
constexpr char kNotAffiliatedErrorMessage[] =
    "This web API is not allowed if the current profile is not affiliated.";
constexpr char kUntrustedIwaAppOrigin[] =
    "isolated-app://abc2sheak3vpmm7vmjqnjwuzx3xwot3vdayrlgnvbkq2mp5lg4daaaic";

#if BUILDFLAG(IS_CHROMEOS)
constexpr char kKioskAppUrl[] = "https://kiosk.com/sample";
constexpr char kInvalidKioskAppUrl[] = "https://invalid-kiosk.com/sample";
constexpr char kNotAllowedOriginErrorMessage[] =
    "The current origin cannot use this web API because it is not allowed by "
    "the DeviceAttributesAllowedForOrigins policy.";
#endif

}  // namespace

namespace {

using Result = blink::mojom::DeviceAttributeResult;

constexpr char kAnnotatedAssetId[] = "annotated_asset_id";
constexpr char kAnnotatedLocation[] = "annotated_location";
constexpr char kDirectoryApiId[] = "directory_api_id";
constexpr char kHostname[] = "hostname";
constexpr char kSerialNumber[] = "serial_number";

class FakeDeviceAttributeApi : public DeviceAttributeApi {
 public:
  FakeDeviceAttributeApi() = default;
  ~FakeDeviceAttributeApi() override = default;

  // This method forwards calls to DeviceAttributesApiImpl to the test the
  // actual error reported by the service.
  void ReportNotAllowedError(
      base::OnceCallback<void(blink::mojom::DeviceAttributeResultPtr)> callback)
      override {
    device_attributes_api_.ReportNotAllowedError(std::move(callback));
  }

  // This method forwards calls to DeviceAttributesApiImpl to the test the
  // actual error reported by the service.
  void ReportNotAffiliatedError(
      base::OnceCallback<void(blink::mojom::DeviceAttributeResultPtr)> callback)
      override {
    device_attributes_api_.ReportNotAffiliatedError(std::move(callback));
  }

  void GetDirectoryId(blink::mojom::DeviceAPIService::GetDirectoryIdCallback
                          callback) override {
    std::move(callback).Run(Result::NewAttribute(kDirectoryApiId));
  }

  void GetHostname(
      blink::mojom::DeviceAPIService::GetHostnameCallback callback) override {
    std::move(callback).Run(Result::NewAttribute(kHostname));
  }

  void GetSerialNumber(blink::mojom::DeviceAPIService::GetSerialNumberCallback
                           callback) override {
    std::move(callback).Run(Result::NewAttribute(kSerialNumber));
  }

  void GetAnnotatedAssetId(
      blink::mojom::DeviceAPIService::GetAnnotatedAssetIdCallback callback)
      override {
    std::move(callback).Run(Result::NewAttribute(kAnnotatedAssetId));
  }

  void GetAnnotatedLocation(
      blink::mojom::DeviceAPIService::GetAnnotatedLocationCallback callback)
      override {
    std::move(callback).Run(Result::NewAttribute(kAnnotatedLocation));
  }

 private:
  DeviceAttributeApiImpl device_attributes_api_;
};
}  // namespace

class DeviceAPIServiceTest {
 public:
  void InstallTrustedApps(Profile* profile) {
    app_id_ = web_app::GenerateAppIdFromManifestId(
        web_app::GenerateManifestIdFromStartUrlOnly(
            GURL(kDefaultAppInstallUrl)));

    web_app::WebAppTestInstallObserver observer(profile);
    observer.BeginListening({app_id()});

    {
      ScopedListPrefUpdate update(profile->GetPrefs(),
                                  prefs::kWebAppInstallForceList);
      base::Value::Dict app_policy;
      app_policy.Set(web_app::kUrlKey, kDefaultAppInstallUrl);
      update->Append(std::move(app_policy));
    }

    EXPECT_EQ(observer.Wait(), app_id());
  }

  void TryCreatingService(
      const GURL& url,
      std::unique_ptr<DeviceAttributeApi> device_attribute_api,
      content::WebContents* web_contents) {
    // Isolated Web Apps require Cross Origin Isolation headers to be included
    // in the response.
    if (url.SchemeIs(chrome::kIsolatedAppScheme)) {
      web_app::SimulateIsolatedWebAppNavigation(web_contents, url);
    } else {
      content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents,
                                                                 url);
    }

    DeviceServiceImpl::CreateForTest(web_contents->GetPrimaryMainFrame(),
                                     remote()->BindNewPipeAndPassReceiver(),
                                     std::move(device_attribute_api));
  }

  const webapps::AppId& app_id() const { return *app_id_; }

  mojo::Remote<blink::mojom::DeviceAPIService>* remote() { return &remote_; }

 private:
  std::optional<webapps::AppId> app_id_;
  mojo::Remote<blink::mojom::DeviceAPIService> remote_;
};

namespace {
void VerifyErrorMessageResultForAllDeviceAttributesAPIs(
    blink::mojom::DeviceAPIService* service,
    const std::string& expected_error_message) {
  base::test::TestFuture<blink::mojom::DeviceAttributeResultPtr> future;

  service->GetDirectoryId(future.GetCallback());
  EXPECT_EQ(future.Take()->get_error_message(), expected_error_message);

  service->GetHostname(future.GetCallback());
  EXPECT_EQ(future.Take()->get_error_message(), expected_error_message);

  service->GetSerialNumber(future.GetCallback());
  EXPECT_EQ(future.Take()->get_error_message(), expected_error_message);

  service->GetAnnotatedAssetId(future.GetCallback());
  EXPECT_EQ(future.Take()->get_error_message(), expected_error_message);

  service->GetAnnotatedLocation(future.GetCallback());
  EXPECT_EQ(future.Take()->get_error_message(), expected_error_message);
}
}  // namespace

class DeviceAPIServiceWebAppTest : public DeviceAPIServiceTest,
                                   public WebAppTest {
 public:
  DeviceAPIServiceWebAppTest()
      : WebAppTest(WebAppTest::WithTestUrlLoaderFactory()) {
    account_id_ = AccountId::FromUserEmail(kUserEmail);
  }

  void SetUp() override {
    WebAppTest::SetUp();
    web_app::test::AwaitStartWebAppProviderAndSubsystems(profile());
    InstallTrustedApps();
    SetAllowedOrigin();
  }

  void InstallTrustedApps() {
    DeviceAPIServiceTest::InstallTrustedApps(profile());
  }

  void RemoveTrustedApps() {
    web_app::WebAppTestUninstallObserver observer(profile());
    observer.BeginListening({app_id()});

    {
      ScopedListPrefUpdate update(profile()->GetPrefs(),
                                  prefs::kWebAppInstallForceList);
      base::Value::Dict app_policy;
      update->clear();
    }

    task_environment()->RunUntilIdle();
  }

  webapps::AppId UserInstallWebApp() {
    auto app_info = web_app::WebAppInstallInfo::CreateWithStartUrlForTesting(
        GURL(kDefaultAppInstallUrl));

    return web_app::test::InstallWebApp(
        profile(), std::move(app_info),
        /*overwrite_existing_manifest_fields=*/false,
        webapps::WebappInstallSource::EXTERNAL_DEFAULT);
  }

  void SetAllowedOrigin() {
    base::Value::List allowed_origins;
    allowed_origins.Append(kTrustedUrl);
    allowed_origins.Append(kKioskAppInstallUrl);
    profile()->GetPrefs()->SetList(prefs::kDeviceAttributesAllowedForOrigins,
                                   std::move(allowed_origins));
  }

  void TryCreatingService(
      const GURL& url,
      std::unique_ptr<DeviceAttributeApi> device_attribute_api) {
    DeviceAPIServiceTest::TryCreatingService(
        url, std::move(device_attribute_api), web_contents());
  }

  void VerifyErrorMessageResultForAllDeviceAttributesAPIs(
      const std::string& expected_error_message) {
    ::VerifyErrorMessageResultForAllDeviceAttributesAPIs(
        remote()->get(), expected_error_message);
  }

  const AccountId& account_id() const { return account_id_; }

  web_app::WebAppProvider* provider() {
    return web_app::WebAppProvider::GetForTest(profile());
  }

 private:
  AccountId account_id_;
};

TEST_F(DeviceAPIServiceWebAppTest, ConnectsForTrustedApps) {
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_TRUE(remote()->is_connected());
}

// The service should be disabled in the Incognito mode.
TEST_F(DeviceAPIServiceWebAppTest, DoesNotConnectForIncognitoProfile) {
  profile_metrics::SetBrowserProfileType(
      profile(), profile_metrics::BrowserProfileType::kIncognito);
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());

  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceWebAppTest, DoesNotConnectForUntrustedApps) {
  TryCreatingService(GURL(kUntrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceWebAppTest, DisconnectWhenTrustRevoked) {
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  RemoveTrustedApps();
  remote()->FlushForTesting();

  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceWebAppTest, MultiOriginDisconnectWhenTrustRevoked) {
  webapps::AppId app_id = UserInstallWebApp();

  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  RemoveTrustedApps();
  remote()->FlushForTesting();

  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceWebAppTest, ReportErrorForDefaultUser) {
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  VerifyErrorMessageResultForAllDeviceAttributesAPIs(
      kNotAffiliatedErrorMessage);
  ASSERT_TRUE(remote()->is_connected());
}

class DeviceAPIServiceIwaTest : public DeviceAPIServiceTest,
                                public web_app::IsolatedWebAppTest {
 public:
  void SetUp() override {
    web_app::IsolatedWebAppTest::SetUp();
    web_app::test::AwaitStartWebAppProviderAndSubsystems(profile());
    InstallTrustedIWA();

    rvh_test_enabler_ = std::make_unique<content::RenderViewHostTestEnabler>();
    web_contents_ = content::WebContentsTester::CreateTestWebContents(
        profile(), /*instance=*/nullptr);
  }

  void TearDown() override {
    web_contents_.reset();
    rvh_test_enabler_.reset();
    web_app::IsolatedWebAppTest::TearDown();
  }

  void InstallTrustedIWA() {
    auto app = web_app::IsolatedWebAppBuilder(
                   web_app::ManifestBuilder().SetVersion("1.0.0"))
                   .BuildBundle();
    app->FakeInstallPageState(profile());

    url_info_ = web_app::IsolatedWebAppUrlInfo::CreateFromSignedWebBundleId(
        app->web_bundle_id());

    web_app::WebAppTestInstallObserver install_observer(profile());
    install_observer.BeginListening({app_id()});

    test_update_server().AddBundle(std::move(app));

    profile()->GetPrefs()->SetList(
        prefs::kIsolatedWebAppInstallForceList,
        base::Value::List().Append(
            web_app::IwaTestServerConfigurator::CreateForceInstallPolicyEntry(
                get_url_info().web_bundle_id())));

    EXPECT_EQ(install_observer.Wait(), app_id());
  }

  void RemoveTrustedIWA() {
    web_app::WebAppTestUninstallObserver uninstall_observer(profile());

    uninstall_observer.BeginListening({app_id()});

    profile()->GetPrefs()->SetList(prefs::kIsolatedWebAppInstallForceList,
                                   base::Value::List());

    EXPECT_EQ(uninstall_observer.Wait(), app_id());
  }

  void TryCreatingService(
      const GURL& url,
      std::unique_ptr<DeviceAttributeApi> device_attribute_api) {
    DeviceAPIServiceTest::TryCreatingService(
        url, std::move(device_attribute_api), web_contents_.get());
  }

  void InitWebContents() {}

  const web_app::IsolatedWebAppUrlInfo& get_url_info() const {
    return *url_info_;
  }

  const webapps::AppId& app_id() const { return get_url_info().app_id(); }

 private:
  std::unique_ptr<content::RenderViewHostTestEnabler> rvh_test_enabler_;
  std::unique_ptr<content::WebContents> web_contents_;
  std::optional<web_app::IsolatedWebAppUrlInfo> url_info_;
};

TEST_F(DeviceAPIServiceIwaTest, ConnectsForTrustedApps) {
  TryCreatingService(get_url_info().origin().GetURL(),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_TRUE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceIwaTest, DoesNotConnectForUntrustedApps) {
  TryCreatingService(GURL(kUntrustedIwaAppOrigin),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceIwaTest, DisconnectWhenTrustRevoked) {
  TryCreatingService(get_url_info().origin().GetURL(),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  RemoveTrustedIWA();
  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceIwaTest, ReportErrorForDefaultUser) {
  TryCreatingService(get_url_info().origin().GetURL(),
                     std::make_unique<DeviceAttributeApiImpl>());
  VerifyErrorMessageResultForAllDeviceAttributesAPIs(
      remote()->get(), kNotAffiliatedErrorMessage);
  ASSERT_TRUE(remote()->is_connected());
}

#if BUILDFLAG(IS_CHROMEOS)

class DeviceAPIServiceParamTest
    : public DeviceAPIServiceWebAppTest,
      public testing::WithParamInterface<std::pair<std::string, bool>> {
 public:
  void SetAllowedOriginFromParam() {
    profile()->GetPrefs()->SetList(
        prefs::kDeviceAttributesAllowedForOrigins,
        base::Value::List().Append(GetParamOrigin()));
  }

  void SetAllowedOrigin(const std::string& origin) {
    profile()->GetPrefs()->SetList(prefs::kDeviceAttributesAllowedForOrigins,
                                   base::Value::List().Append(origin));
  }

  void EnableFeatureAndAllowlistOrigin(const base::Feature& param,
                                       const std::string& origin) {
    base::FieldTrialParams feature_params;
    feature_params[permissions::feature_params::
                       kWebKioskBrowserPermissionsAllowlist.name] = origin;
    feature_list_.InitAndEnableFeatureWithParameters(param, feature_params);
  }

  void EnableFeature(const base::Feature& param) {
    feature_list_.InitAndEnableFeature(param);
  }

  void DisableFeature(const base::Feature& feature) {
    feature_list_.InitAndDisableFeature(feature);
  }

  void SetKioskBrowserPermissionsAllowedForOrigins(const std::string& origin) {
    profile()->GetPrefs()->SetList(
        prefs::kKioskBrowserPermissionsAllowedForOrigins,
        base::Value::List().Append(std::move(origin)));
  }

  void VerifyCanAccessForAllDeviceAttributesAPIs() {
    base::test::TestFuture<blink::mojom::DeviceAttributeResultPtr> future;

    remote()->get()->GetDirectoryId(future.GetCallback());
    EXPECT_EQ(future.Take()->get_attribute(), kDirectoryApiId);

    remote()->get()->GetHostname(future.GetCallback());
    EXPECT_EQ(future.Take()->get_attribute(), kHostname);

    remote()->get()->GetSerialNumber(future.GetCallback());
    EXPECT_EQ(future.Take()->get_attribute(), kSerialNumber);

    remote()->get()->GetAnnotatedAssetId(future.GetCallback());
    EXPECT_EQ(future.Take()->get_attribute(), kAnnotatedAssetId);

    remote()->get()->GetAnnotatedLocation(future.GetCallback());
    EXPECT_EQ(future.Take()->get_attribute(), kAnnotatedLocation);
  }

  const std::string& GetParamOrigin() { return GetParam().first; }

  bool ExpectApiAvailable() { return GetParam().second; }

 private:
  base::test::ScopedFeatureList feature_list_;
};

class DeviceAPIServiceRegularUserTest : public DeviceAPIServiceParamTest {
 public:
  void LoginRegularUser(bool is_affiliated) {
    fake_user_manager_ = static_cast<ash::FakeChromeUserManager*>(
        user_manager::UserManager::Get());
    const user_manager::User* user =
        fake_user_manager()->AddUserWithAffiliation(account_id(),
                                                    is_affiliated);
    fake_user_manager()->UserLoggedIn(
        user->GetAccountId(),
        user_manager::TestHelper::GetFakeUsernameHash(user->GetAccountId()));
  }

  ash::FakeChromeUserManager* fake_user_manager() const {
    return fake_user_manager_;
  }

  void RemoveAllowedOrigin() {
    profile()->GetPrefs()->SetList(prefs::kDeviceAttributesAllowedForOrigins,
                                   base::Value::List());
  }

  void TearDown() override {
    provider()->Shutdown();
    DeviceAPIServiceParamTest::TearDown();
  }

 private:
  raw_ptr<ash::FakeChromeUserManager, DanglingUntriaged> fake_user_manager_;
};

TEST_F(DeviceAPIServiceRegularUserTest, ReportErrorForUnaffiliatedUser) {
  LoginRegularUser(false);
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  VerifyErrorMessageResultForAllDeviceAttributesAPIs(
      kNotAffiliatedErrorMessage);
  ASSERT_TRUE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceRegularUserTest, ReportErrorForDisallowedOrigin) {
  LoginRegularUser(true);
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  RemoveAllowedOrigin();

  VerifyErrorMessageResultForAllDeviceAttributesAPIs(
      kNotAllowedOriginErrorMessage);
  ASSERT_TRUE(remote()->is_connected());
}

TEST_P(DeviceAPIServiceRegularUserTest, TestPolicyOriginPatterns) {
  SetAllowedOriginFromParam();
  LoginRegularUser(true);
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());

  if (ExpectApiAvailable()) {
    VerifyCanAccessForAllDeviceAttributesAPIs();
  } else {
    VerifyErrorMessageResultForAllDeviceAttributesAPIs(
        kNotAllowedOriginErrorMessage);
  }
  ASSERT_TRUE(remote()->is_connected());
}

INSTANTIATE_TEST_SUITE_P(
    All,
    DeviceAPIServiceRegularUserTest,
    testing::ValuesIn(
        {std::pair<std::string, bool>("*", false),
         std::pair<std::string, bool>(".example.com", false),
         std::pair<std::string, bool>("example.", false),
         std::pair<std::string, bool>("file://example*", false),
         std::pair<std::string, bool>("invalid-example.com", false),
         std::pair<std::string, bool>(kTrustedUrl, true),
         std::pair<std::string, bool>("https://example.com", true),
         std::pair<std::string, bool>("https://example.com/sample", true),
         std::pair<std::string, bool>("example.com", true),
         std::pair<std::string, bool>("*://example.com:*/", true),
         std::pair<std::string, bool>("[*.]example.com", true)}));

class DeviceAPIServiceWithKioskUserTest : public DeviceAPIServiceParamTest {
 public:
  DeviceAPIServiceWithKioskUserTest() {
    fake_user_manager_.Reset(std::make_unique<ash::FakeChromeUserManager>());
  }

  void SetUp() override {
    DeviceAPIServiceParamTest::SetUp();
    command_line_.GetProcessCommandLine()->AppendSwitch(
        switches::kForceAppMode);
    app_manager_ = std::make_unique<ash::KioskWebAppManager>();
  }

  void TearDown() override {
    app_manager_.reset();
    DeviceAPIServiceParamTest::TearDown();
  }

  void LoginKioskUser() {
    app_manager()->AddAppForTesting(account_id(), GURL(kKioskAppInstallUrl));
    fake_user_manager()->AddKioskWebAppUser(account_id());
    fake_user_manager()->LoginUser(account_id());
  }

  ash::FakeChromeUserManager* fake_user_manager() const {
    return fake_user_manager_.Get();
  }

  ash::KioskWebAppManager* app_manager() const { return app_manager_.get(); }

 private:
  user_manager::TypedScopedUserManager<ash::FakeChromeUserManager>
      fake_user_manager_;
  std::unique_ptr<ash::KioskWebAppManager> app_manager_;
  base::test::ScopedCommandLine command_line_;
};

// The service should be enabled if the current origin is same as the origin of
// Kiosk app.
TEST_F(DeviceAPIServiceWithKioskUserTest, ConnectsForKioskOrigin) {
  LoginKioskUser();
  TryCreatingService(GURL(kKioskAppUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_TRUE(remote()->is_connected());
}

// The service should be disabled if the current origin is different from the
// origin of Kiosk app.
TEST_F(DeviceAPIServiceWithKioskUserTest, DoesNotConnectForInvalidOrigin) {
  LoginKioskUser();
  TryCreatingService(GURL(kInvalidKioskAppUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

// The service should be disabled if the current origin is different from the
// origin of Kiosk app, even if it is trusted (force-installed).
TEST_F(DeviceAPIServiceWithKioskUserTest,
       DoesNotConnectForNonKioskTrustedOrigin) {
  LoginKioskUser();
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

class DeviceAPIServiceWithChromeAppKioskUserTest
    : public DeviceAPIServiceTest,
      public ChromeRenderViewHostTestHarness {
 public:
  DeviceAPIServiceWithChromeAppKioskUserTest()
      : account_id_(AccountId::FromUserEmail(kUserEmail)) {
    fake_user_manager_.Reset(std::make_unique<ash::FakeChromeUserManager>());
  }

  void LoginChromeAppKioskUser() {
    fake_user_manager()->AddKioskChromeAppUser(account_id());
    fake_user_manager()->LoginUser(account_id());
  }

  const AccountId& account_id() const { return account_id_; }

  ash::FakeChromeUserManager* fake_user_manager() const {
    return fake_user_manager_.Get();
  }

  void TryCreatingService(
      const GURL& url,
      std::unique_ptr<DeviceAttributeApi> device_attribute_api) {
    DeviceAPIServiceTest::TryCreatingService(
        url, std::move(device_attribute_api), web_contents());
  }

 private:
  AccountId account_id_;
  std::unique_ptr<user_manager::ScopedUserManager> scoped_user_manager_;
  user_manager::TypedScopedUserManager<ash::FakeChromeUserManager>
      fake_user_manager_;
};

// The service should be disabled if a non-PWA kiosk user is logged in.
TEST_F(DeviceAPIServiceWithChromeAppKioskUserTest,
       DoesNotConnectForChromeAppKioskSession) {
  LoginChromeAppKioskUser();

  TryCreatingService(GURL(kKioskAppUrl),
                     std::make_unique<DeviceAttributeApiImpl>());
  remote()->FlushForTesting();
  ASSERT_FALSE(remote()->is_connected());
}

class DeviceAPIServiceWithKioskUserTestForOrigins
    : public DeviceAPIServiceWithKioskUserTest {};

TEST_F(DeviceAPIServiceWithKioskUserTestForOrigins,
       TestTrustedKioskOriginsWhenEnabledByFeature) {
  EnableFeatureAndAllowlistOrigin(
      permissions::features::kAllowMultipleOriginsForWebKioskPermissions,
      kTrustedUrl);
  SetAllowedOrigin(kTrustedUrl);

  LoginKioskUser();
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  remote()->FlushForTesting();

  // Check whether the service connects for a different allowed origin.
  ASSERT_TRUE(remote()->is_connected());
  VerifyCanAccessForAllDeviceAttributesAPIs();
}

TEST_F(DeviceAPIServiceWithKioskUserTestForOrigins,
       TestUntrustedKioskOriginsWhenEnabledByFeature) {
  EnableFeatureAndAllowlistOrigin(
      permissions::features::kAllowMultipleOriginsForWebKioskPermissions,
      kTrustedUrl);
  SetAllowedOrigin(kUntrustedUrl);

  LoginKioskUser();
  TryCreatingService(GURL(kUntrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  remote()->FlushForTesting();

  // Check whether the service connects for a different allowed origin.
  ASSERT_FALSE(remote()->is_connected());
}

TEST_F(DeviceAPIServiceWithKioskUserTestForOrigins,
       TestTrustedKioskOriginWhenMultipleOriginPrefIsSet) {
  EnableFeature(
      permissions::features::kAllowMultipleOriginsForWebKioskPermissions);
  SetKioskBrowserPermissionsAllowedForOrigins(kTrustedUrl);
  SetAllowedOrigin(kTrustedUrl);

  LoginKioskUser();
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  remote()->FlushForTesting();

  // Check whether the service connects for a different allowed origin.
  ASSERT_TRUE(remote()->is_connected());
  VerifyCanAccessForAllDeviceAttributesAPIs();
}

TEST_F(DeviceAPIServiceWithKioskUserTestForOrigins,
       TestKioskInstallOriginWhenMultipleOriginPrefIsNotSet) {
  EnableFeature(
      permissions::features::kAllowMultipleOriginsForWebKioskPermissions);
  SetAllowedOrigin(kKioskAppInstallUrl);

  LoginKioskUser();
  TryCreatingService(GURL(kKioskAppInstallUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  remote()->FlushForTesting();

  // Check whether the service connects for install origin.
  ASSERT_TRUE(remote()->is_connected());
  VerifyCanAccessForAllDeviceAttributesAPIs();
}

TEST_F(DeviceAPIServiceWithKioskUserTestForOrigins,
       TestMultipleOriginPolicyWhenFeatureIsDisabled) {
  DisableFeature(
      permissions::features::kAllowMultipleOriginsForWebKioskPermissions);
  SetKioskBrowserPermissionsAllowedForOrigins(kTrustedUrl);
  SetAllowedOrigin(kTrustedUrl);

  LoginKioskUser();
  TryCreatingService(GURL(kTrustedUrl),
                     std::make_unique<FakeDeviceAttributeApi>());
  remote()->FlushForTesting();

  // Check that the service is not able to connect when the feature is disabled.
  ASSERT_FALSE(remote()->is_connected());
}

TEST_P(DeviceAPIServiceWithKioskUserTestForOrigins, TestPolicyOriginPatterns) {
  SetAllowedOriginFromParam();
  LoginKioskUser();
  TryCreatingService(GURL(kKioskAppUrl),
                     std::make_unique<FakeDeviceAttributeApi>());

  remote()->FlushForTesting();

  ASSERT_TRUE(remote()->is_connected());

  if (ExpectApiAvailable()) {
    VerifyCanAccessForAllDeviceAttributesAPIs();
  } else {
    VerifyErrorMessageResultForAllDeviceAttributesAPIs(
        kNotAllowedOriginErrorMessage);
  }
}

INSTANTIATE_TEST_SUITE_P(
    All,
    DeviceAPIServiceWithKioskUserTestForOrigins,
    testing::ValuesIn({std::pair<std::string, bool>("*", false),
                       std::pair<std::string, bool>("*.kiosk.com", false),
                       std::pair<std::string, bool>("*kiosk.com", false),
                       std::pair<std::string, bool>("kiosk.", false),
                       std::pair<std::string, bool>(kInvalidKioskAppUrl, false),
                       std::pair<std::string, bool>(kKioskAppUrl, true),
                       std::pair<std::string, bool>("https://kiosk.com", true),
                       std::pair<std::string, bool>("https://kiosk.com/sample",
                                                    true),
                       std::pair<std::string, bool>("kiosk.com", true),
                       std::pair<std::string, bool>("*://kiosk.com:*/", true),
                       std::pair<std::string, bool>("[*.]kiosk.com", true)}));
#endif  // BUILDFLAG(IS_CHROMEOS)
