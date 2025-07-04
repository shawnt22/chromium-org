// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/enterprise/browser_management/management_service_factory.h"
#include "chrome/browser/enterprise/connectors/test/deep_scanning_test_utils.h"
#include "chrome/browser/extensions/api/enterprise_reporting_private/enterprise_reporting_private_event_router.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/chrome_signin_client_test_util.h"
#include "chrome/browser/signin/identity_test_environment_profile_adaptor.h"
#include "chrome/common/extensions/api/enterprise_reporting_private.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/policy/core/common/management/management_service.h"
#include "components/safe_browsing/core/common/proto/realtimeapi.pb.h"
#include "components/signin/public/identity_manager/account_capabilities_test_mutator.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "components/version_info/version_info.h"
#include "content/public/test/browser_test.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_features.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "extensions/test/test_extension_dir.h"
#include "google_apis/gaia/gaia_id.h"
#include "services/network/test/test_url_loader_factory.h"

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "base/files/file_path.h"
#include "base/process/process.h"
#include "base/strings/string_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/device_signals/core/common/signals_features.h"
#include "components/device_signals/core/system_signals/platform_utils.h"  // nogncheck
#endif  //  BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
#include "components/device_signals/test/test_constants.h"
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN)
#include "base/strings/sys_string_conversions.h"
#include "base/test/test_reg_util_win.h"
#include "base/win/registry.h"
#include "components/device_signals/test/win/scoped_executable_files.h"
#endif  // BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_CHROMEOS)
#include "components/enterprise/browser/controller/fake_browser_dm_token_storage.h"
#include "components/policy/core/common/cloud/cloud_policy_core.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "components/policy/core/common/cloud/machine_level_user_cloud_policy_manager.h"
#include "components/policy/core/common/cloud/user_cloud_policy_manager.h"
#include "components/policy/proto/device_management_backend.pb.h"
#else
#include "base/strings/strcat.h"
#include "chrome/browser/ash/login/test/cryptohome_mixin.h"
#include "chrome/browser/ash/login/test/user_auth_config.h"
#include "chrome/browser/ash/policy/affiliation/affiliation_mixin.h"
#include "chrome/browser/ash/policy/affiliation/affiliation_test_helper.h"
#include "chrome/browser/ash/policy/core/device_policy_cros_browser_test.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/enterprise/util/affiliation.h"
#include "chrome/browser/extensions/api/enterprise_reporting_private/enterprise_reporting_private_api.h"
#endif  // !BUILDFLAG(IS_CHROMEOS)

namespace extensions {
namespace {

#if !BUILDFLAG(IS_CHROMEOS)
constexpr char kAffiliationId[] = "affiliation-id";
#endif  // !BUILDFLAG(IS_CHROMEOS)

// Manifest key for the Endpoint Verification extension found at
// chrome.google.com/webstore/detail/callobklhcbilhphinckomhgkigmfocg
// This extension is authorized to use the enterprise.reportingPrivate API.
constexpr char kAuthorizedManifestKey[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAjXwWSZq5RLuM5ZbmRWn4gXwpMOb52a"
    "oOhtzIsmbXUWPQeA6/D2p1uaPxIHh6EusxAhXMrBgNaJv1QFxCxiU1aGDlmCR9mOsA7rK5kmVC"
    "i0TYLbQa+C38UDmyhRACrvHO26Jt8qC8oM8yiSuzgb+16rgCCcek9dP7IaHaoJMsBMAEf3VEno"
    "4xt+kCAAsFsyFCB4plWid54avqpgg6+OsR3ZtUAMWooVziJHVmBTiyl82QR5ZURYr+TjkiljkP"
    "EBLaMTKC2g7tUl2h0Q1UmMTMc2qxLIVVREhr4q9iOegNxfNy78BaxZxI1Hjp0EVYMZunIEI9r1"
    "k0vyyaH13TvdeqNwIDAQAB";

// Manifest key for the Google Translate extension found at
// chrome.google.com/webstore/detail/aapbdbdomjkkjkaonfhkkikfgjllcleb
// This extension is unauthorized to use the enterprise.reportingPrivate API.
constexpr char kUnauthorizedManifestKey[] =
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCfHy1M+jghaHyaVAILzx/c/Dy+RXtcaP9/5p"
    "C7EY8JlNEI/G4DIIng9IzlrH8UWStpMWMyGUsdyusn2PkYFrqfVzhc2azVF3PX9D0KHG3FLN3m"
    "Noz1YTBHvO5QSXJf292qW0tTYuoGqeTfXtF9odLdg20Xd0YrLmtS4TQkpSYGDwIDAQAB";

constexpr char kManifestTemplate[] = R"(
    {
      "key": "%s",
      "name": "Enterprise Private Reporting API Test",
      "version": "0.1",
      "manifest_version": 3,
      "permissions": [
          "enterprise.reportingPrivate"
      ],
      "background": { "service_worker": "background.js" }
    })";

constexpr char kTestUrl[] = "https://foo.bar";

}  // namespace

// This test class is to validate that the API is correctly unavailable on
// unsupported extensions and unsupported platforms. It also does basic
// validation that fields are present in the values the API returns, but it
// doesn't make strong assumption on what those values are to minimize the kind
// of mocking that is already done in unit/browser tests covering this API.
class EnterpriseReportingPrivateApiTest : public extensions::ExtensionApiTest {
 public:
  EnterpriseReportingPrivateApiTest() {
#if !BUILDFLAG(IS_CHROMEOS)
    browser_dm_token_storage_.SetClientId("client_id");
    browser_dm_token_storage_.SetEnrollmentToken("enrollment_token");
    browser_dm_token_storage_.SetDMToken("dm_token");
    policy::BrowserDMTokenStorage::SetForTesting(&browser_dm_token_storage_);
#endif  // !BUILDFLAG(IS_CHROMEOS)
  }

  ~EnterpriseReportingPrivateApiTest() override = default;

#if !BUILDFLAG(IS_CHROMEOS)
  // Signs in and returns the account ID of the primary account.
  AccountInfo SignIn(const std::string& email, bool as_managed = true) {
    auto account_info = identity_test_env()->MakePrimaryAccountAvailable(
        email, signin::ConsentLevel::kSignin);
    EXPECT_TRUE(identity_test_env()->identity_manager()->HasPrimaryAccount(
        signin::ConsentLevel::kSignin));

    if (as_managed) {
      account_info.hosted_domain = "example.com";
      identity_test_env()->UpdateAccountInfoForAccount(account_info);

      enterprise_connectors::test::SetProfileDMToken(profile(),
                                                     "fake_user_dmtoken");
      auto profile_policy_data =
          std::make_unique<enterprise_management::PolicyData>();
      profile_policy_data->add_user_affiliation_ids(kAffiliationId);
      profile()
          ->GetUserCloudPolicyManager()
          ->core()
          ->store()
          ->set_policy_data_for_testing(std::move(profile_policy_data));
    }
    AccountCapabilitiesTestMutator(&account_info.capabilities)
        .set_is_subject_to_enterprise_policies(as_managed);

    return account_info;
  }
#endif  // !BUILDFLAG(IS_CHROMEOS)

  void RunTest(const std::string& background_js,
               bool authorized_manifest_key = true) {
    ResultCatcher result_catcher;
    TestExtensionDir test_dir;
    test_dir.WriteManifest(base::StringPrintf(
        kManifestTemplate, authorized_manifest_key ? kAuthorizedManifestKey
                                                   : kUnauthorizedManifestKey));

    // Since the API functions use async callbacks, this wrapper code is
    // necessary for assertions to work properly.
    constexpr char kTestWrapper[] = R"(
        chrome.test.runTests([
          async function asyncAssertions() {
            %s
          }
        ]);)";
    test_dir.WriteFile(FILE_PATH_LITERAL("background.js"),
                       base::StringPrintf(kTestWrapper, background_js.c_str()));

    const Extension* extension = LoadExtension(
        test_dir.UnpackedPath(), {.ignore_manifest_warnings = true});
    ASSERT_TRUE(extension);
    ASSERT_TRUE(result_catcher.GetNextResult()) << result_catcher.message();
  }

 protected:
  void SetUpBrowserContextKeyedServices(
      content::BrowserContext* context) override {
    extensions::ExtensionApiTest::SetUpBrowserContextKeyedServices(context);
    IdentityTestEnvironmentProfileAdaptor::
        SetIdentityTestEnvironmentFactoriesOnBrowserContext(context);
    ChromeSigninClientFactory::GetInstance()->SetTestingFactory(
        context, base::BindRepeating(&BuildChromeSigninClientWithURLLoader,
                                     &test_url_loader_factory_));
  }

  void SetUpOnMainThread() override {
    extensions::ExtensionApiTest::SetUpOnMainThread();
    identity_test_env_profile_adaptor_ =
        std::make_unique<IdentityTestEnvironmentProfileAdaptor>(profile());

    identity_test_env()->SetTestURLLoaderFactory(&test_url_loader_factory_);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    // Set device org's affiliated IDs.
    auto* browser_policy_manager =
        g_browser_process->browser_policy_connector()
            ->machine_level_user_cloud_policy_manager();
    auto browser_policy_data =
        std::make_unique<enterprise_management::PolicyData>();
    browser_policy_data->add_device_affiliation_ids(kAffiliationId);
    browser_policy_manager->core()->store()->set_policy_data_for_testing(
        std::move(browser_policy_data));
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  }

  void TearDownOnMainThread() override {
    extensions::ExtensionApiTest::TearDownOnMainThread();
    // Must be destroyed before the Profile.
    identity_test_env_profile_adaptor_.reset();
  }

  policy::ProfilePolicyConnector* profile_policy_connector() {
    return profile()->GetProfilePolicyConnector();
  }

  signin::IdentityTestEnvironment* identity_test_env() {
    return identity_test_env_profile_adaptor_->identity_test_env();
  }

  std::unique_ptr<IdentityTestEnvironmentProfileAdaptor>
      identity_test_env_profile_adaptor_;

  network::TestURLLoaderFactory test_url_loader_factory_;

#if !BUILDFLAG(IS_CHROMEOS)
  policy::FakeBrowserDMTokenStorage browser_dm_token_storage_;
#endif
};

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest,
                       ExtensionAvailability) {
  constexpr char kBackgroundJs[] = R"(
    chrome.test.assertEq(undefined, chrome.enterprise);
    chrome.test.notifyPass();
  )";
  RunTest(kBackgroundJs, /*authorized_manifest_key*/ false);
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetDeviceId) {
  constexpr char kAssertions[] =
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
      "chrome.test.assertNoLastError();"
      "chrome.test.assertEq(id, 'client_id');";
#else
      "chrome.test.assertLastError('Access to extension API denied.');";
#endif
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getDeviceId);
      chrome.enterprise.reportingPrivate.getDeviceId((id) => {
        %s
        chrome.test.notifyPass();
      });
  )";
  RunTest(base::StringPrintf(kTest, kAssertions));
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetPersistentSecret) {
  constexpr char kAssertions[] =
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
      "chrome.test.assertNoLastError();"
      "chrome.test.assertTrue(secret instanceof ArrayBuffer);";
#else
      "chrome.test.assertLastError('Access to extension API denied.');";
#endif
  // Pass `true` as recreate on error to ensure that any keychain ACLs are fixed
  // by this call instead of failing the test (makes the test more robust).
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getPersistentSecret);
      chrome.enterprise.reportingPrivate.getPersistentSecret(true, (secret) => {
        %s
        chrome.test.notifyPass();
      });
  )";
  RunTest(base::StringPrintf(kTest, kAssertions));
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetDeviceData) {
  constexpr char kAssertions[] =
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
      "chrome.test.assertNoLastError();"
      "chrome.test.assertTrue(data instanceof ArrayBuffer);";
#else
      "chrome.test.assertLastError('Access to extension API denied.');";
#endif
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getDeviceData);
      chrome.enterprise.reportingPrivate.getDeviceData('id', (data) => {
        %s
        chrome.test.notifyPass();
      });
  )";
  RunTest(base::StringPrintf(kTest, kAssertions));
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, SetDeviceData) {
  constexpr char kAssertions[] =
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
      "chrome.test.assertNoLastError();"
      "chrome.enterprise.reportingPrivate.getDeviceData('id', (data) => {"
      "  let view = new Int8Array(data);"
      "  chrome.test.assertEq(3, view.length);"
      "  chrome.test.assertEq(2, view[0]);"
      "  chrome.test.assertEq(1, view[1]);"
      "  chrome.test.assertEq(0, view[2]);"
      "  chrome.test.notifyPass();"
      "});";
#else
      "chrome.test.assertLastError('Access to extension API denied.');"
      "chrome.test.notifyPass();";
#endif
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.setDeviceData);
      let array = new Int8Array(3);
      array[0] = 2;
      array[1] = 1;
      array[2] = 0;
      chrome.enterprise.reportingPrivate.setDeviceData('id', array, () => {
        %s
      });
  )";
  RunTest(base::StringPrintf(kTest, kAssertions));
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetDeviceInfo) {
#if BUILDFLAG(IS_WIN)
  constexpr char kOSName[] = "windows";
#elif BUILDFLAG(IS_MAC)
  constexpr char kOSName[] = "macOS";
#elif BUILDFLAG(IS_LINUX)
  constexpr char kOSName[] = "linux";
#endif

#if BUILDFLAG(IS_WIN)
  // The added conditions for windows are related to the fact that we don't know
  // if the machine running the test is managed or not
  constexpr char kTest[] = R"(
    chrome.test.assertEq(
      'function',
      typeof chrome.enterprise.reportingPrivate.getDeviceInfo);

    chrome.enterprise.reportingPrivate.getDeviceInfo((deviceInfo) => {
      chrome.test.assertNoLastError();
      let count = 10;
      if(deviceInfo.windowsUserDomain){
        count++;
        chrome.test.assertEq(typeof deviceInfo.windowsUserDomain, "string");
      } else {
        chrome.test.assertEq(typeof deviceInfo.windowsUserDomain, "undefined");
      }

      if(deviceInfo.windowsMachineDomain){
        count++;
        chrome.test.assertEq(typeof deviceInfo.windowsMachineDomain, "string");
      } else {
        chrome.test.assertEq(
          typeof deviceInfo.windowsMachineDomain,
          "undefined");
      }
      chrome.test.assertEq(count, Object.keys(deviceInfo).length);
      chrome.test.assertEq('%s', deviceInfo.osName);
      chrome.test.assertEq(typeof deviceInfo.osVersion, 'string');
      chrome.test.assertEq(typeof deviceInfo.securityPatchLevel, 'string');
      chrome.test.assertEq(typeof deviceInfo.deviceHostName, 'string');
      chrome.test.assertEq(typeof deviceInfo.deviceModel, 'string');
      chrome.test.assertEq(typeof deviceInfo.serialNumber, 'string');
      chrome.test.assertEq(typeof deviceInfo.screenLockSecured, 'string');
      chrome.test.assertEq(typeof deviceInfo.diskEncrypted, 'string');
      chrome.test.assertTrue(deviceInfo.macAddresses instanceof Array);
      chrome.test.assertEq(typeof deviceInfo.secureBootEnabled, 'string');

      chrome.test.notifyPass();
    });)";
  RunTest(base::StringPrintf(kTest, kOSName));
#elif BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  constexpr char kTest[] = R"(
    chrome.test.assertEq(
      'function',
      typeof chrome.enterprise.reportingPrivate.getDeviceInfo);

    chrome.enterprise.reportingPrivate.getDeviceInfo((deviceInfo) => {
      chrome.test.assertNoLastError();

      chrome.test.assertEq(9, Object.keys(deviceInfo).length);
      chrome.test.assertEq('%s', deviceInfo.osName);
      chrome.test.assertEq(typeof deviceInfo.osVersion, 'string');
      chrome.test.assertEq(typeof deviceInfo.securityPatchLevel, 'string');
      chrome.test.assertEq(typeof deviceInfo.deviceHostName, 'string');
      chrome.test.assertEq(typeof deviceInfo.deviceModel, 'string');
      chrome.test.assertEq(typeof deviceInfo.serialNumber, 'string');
      chrome.test.assertEq(typeof deviceInfo.screenLockSecured, 'string');
      chrome.test.assertEq(typeof deviceInfo.diskEncrypted, 'string');
      chrome.test.assertTrue(deviceInfo.macAddresses instanceof Array);
      chrome.test.assertEq(typeof deviceInfo.windowsMachineDomain, "undefined");
      chrome.test.assertEq(typeof deviceInfo.windowsUserDomain, "undefined");
      chrome.test.assertEq(typeof deviceInfo.secureBootEnabled, "undefined");

      chrome.test.notifyPass();
    });)";
  RunTest(base::StringPrintf(kTest, kOSName));
#else
  RunTest(R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getDeviceInfo);

      chrome.enterprise.reportingPrivate.getDeviceInfo((deviceInfo) => {
        chrome.test.assertLastError('Access to extension API denied.');
        chrome.test.notifyPass();
      });
  )");
#endif
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetContextInfo) {
  constexpr char kTest[] = R"(
    chrome.test.assertEq(
      'function',
      typeof chrome.enterprise.reportingPrivate.getContextInfo);
    chrome.enterprise.reportingPrivate.getContextInfo((info) => {
      chrome.test.assertNoLastError();

      chrome.test.assertEq(17, Object.keys(info).length);
      chrome.test.assertTrue(info.browserAffiliationIds instanceof Array);
      chrome.test.assertTrue(info.profileAffiliationIds instanceof Array);
      chrome.test.assertTrue(info.onFileAttachedProviders instanceof Array);
      chrome.test.assertTrue(info.onFileDownloadedProviders instanceof Array);
      chrome.test.assertTrue(info.onBulkDataEntryProviders instanceof Array);
      chrome.test.assertTrue(info.onPrintProviders instanceof Array);
      chrome.test.assertEq(typeof info.realtimeUrlCheckMode, 'string');
      chrome.test.assertTrue(info.onSecurityEventProviders instanceof Array);
      chrome.test.assertEq(typeof info.browserVersion, 'string');
      chrome.test.assertEq(typeof info.safeBrowsingProtectionLevel, 'string');
      chrome.test.assertEq(typeof info.siteIsolationEnabled, 'boolean');
      chrome.test.assertEq(typeof info.builtInDnsClientEnabled, 'boolean');
      chrome.test.assertEq
        (typeof info.passwordProtectionWarningTrigger, 'string');
      chrome.test.assertEq
        (typeof info.chromeRemoteDesktopAppBlocked, 'boolean');
      chrome.test.assertEq(typeof info.osFirewall, 'string');
      chrome.test.assertTrue(info.systemDnsServers instanceof Array);
      chrome.test.assertEq(typeof info.enterpriseProfileId, 'string');

      chrome.test.notifyPass();
    });)";
  RunTest(kTest);
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetCertificate) {
  // The encodedCertificate attribute should always be empty when the
  // AutoSelectCertificateForUrls policy is unset.
  RunTest(R"(
    chrome.test.assertEq(
      'function',
      typeof chrome.enterprise.reportingPrivate.getCertificate);
    chrome.enterprise.reportingPrivate.getCertificate(
      'https://foo.com', (certificate) => {
        chrome.test.assertNoLastError();

        chrome.test.assertEq(1, Object.keys(certificate).length);
        chrome.test.assertEq(typeof certificate.status, 'string');
        chrome.test.assertEq(certificate.encodedCertificate, undefined);

        chrome.test.notifyPass();
    });)");
}

#if BUILDFLAG(IS_WIN)

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetAvInfo_Success) {
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getAvInfo);
      const userContext = {userId: '%s'};

   chrome.enterprise.reportingPrivate.getAvInfo(userContext, (avProducts) => {
        chrome.test.assertNoLastError();
        chrome.test.assertTrue(avProducts instanceof Array);
        chrome.test.notifyPass();
      });
  )";

  AccountInfo account_info = SignIn("some-email@example.com");
  RunTest(base::StringPrintf(kTest, account_info.gaia.ToString().c_str()));
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest, GetHotfixes_Success) {
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getHotfixes);
      const userContext = {userId: '%s'};

   chrome.enterprise.reportingPrivate.getHotfixes(userContext, (hotfixes) => {
        chrome.test.assertNoLastError();
        chrome.test.assertTrue(hotfixes instanceof Array);
        chrome.test.notifyPass();
      });
  )";

  AccountInfo account_info = SignIn("some-email@example.com");
  RunTest(base::StringPrintf(kTest, account_info.gaia.ToString().c_str()));
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest,
                       GetRegistrySettings_Success) {
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getSettings);
      const userContext = {userId: '%s'};
      const options = [];

      %s

      const request = {userContext, options};

   chrome.enterprise.reportingPrivate.getSettings(
    request,
    (settingsItems) => {
        %s
    });
  )";

  std::string kOptions = "";

  std::string registry_path = "SOFTWARE\\\\Chromium\\\\DeviceTrust\\\\Test";
  std::string valid_key = "test_key";

  kOptions = base::StringPrintf(
      R"(
    const test_hive = 'HKEY_CURRENT_USER';
    const registry_path = '%s';
    const invalid_path = 'SOFTWARE\\Chromium\\DeviceTrust\\Invalid';
    const valid_key = '%s';
    const invalid_key = 'invalid_key';

    options.push({
      hive: test_hive,
      path: registry_path,
      key: valid_key,
      getValue: false
    });
    options.push({
      hive: test_hive,
      path: registry_path,
      key: valid_key,
      getValue: true
    });
    options.push({
      hive: test_hive,
      path: registry_path,
      key: invalid_key,
      getValue: true
    });
    options.push({
      hive: test_hive,
      path: invalid_path,
      key: valid_key,
      getValue: true
    });
  )",
      registry_path.c_str(), valid_key.c_str());

  registry_util::RegistryOverrideManager registry_override_manager_;
  registry_override_manager_.OverrideRegistry(HKEY_CURRENT_USER);

  base::win::RegKey key(HKEY_CURRENT_USER,
                        base::SysUTF8ToWide(registry_path).c_str(),
                        KEY_ALL_ACCESS);
  ASSERT_TRUE(key.WriteValue(base::SysUTF8ToWide(valid_key).c_str(), 37) ==
              ERROR_SUCCESS);

  constexpr char kAssertions[] = R"(
      chrome.test.assertNoLastError();
      chrome.test.assertTrue(settingsItems instanceof Array);
      chrome.test.assertEq(4, settingsItems.length);

      const expectedItems = [];

      expectedItems.push({
        hive: test_hive,
        path: registry_path,
        key: valid_key,
        presence: 'FOUND',
      });
      expectedItems.push({
        hive: test_hive,
        path: registry_path,
        key: valid_key,
        presence: 'FOUND',
        value: '37',
      });
      expectedItems.push({
        hive: test_hive,
        path: registry_path,
        key: invalid_key,
        presence: 'NOT_FOUND',
      });
      expectedItems.push({
        hive: test_hive,
        path: invalid_path,
        key: valid_key,
        presence: 'NOT_FOUND',
      });
      for (let i = 0; i < settingsItems.length; ++i) {
        chrome.test.assertEq(settingsItems[i], expectedItems[i]);
      }
      chrome.test.notifyPass();
  )";

  AccountInfo account_info = SignIn("some-email@example.com");
  RunTest(base::StringPrintf(kTest, account_info.gaia.ToString().c_str(),
                             kOptions.c_str(), kAssertions));
}

#endif  // BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_WIN) && !BUILDFLAG(IS_MAC)

IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest,
                       GetRegistrySettings_UnsupportedPlatform) {
  constexpr char kTest[] = R"(
    chrome.test.assertFalse(
      'function' == typeof chrome.enterprise.reportingPrivate.getSettings);

    chrome.test.notifyPass();
  )";

  RunTest(base::StringPrintf("%s", kTest));
}

#endif  // !BUILDFLAG(IS_WIN) && !BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
// TODO(crbug.com/40888560): Failing consistently on Mac.
// TODO(crbug.com/40863616): Flaky on Linux.
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#define MAYBE_GetFileSystemInfo_Success DISABLED_GetFileSystemInfo_Success
#else
#define MAYBE_GetFileSystemInfo_Success GetFileSystemInfo_Success
#endif
IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest,
                       MAYBE_GetFileSystemInfo_Success) {
  // Use the test runner process and binary as test parameters, as it will
  // always be running.
  auto test_runner_file_path =
      device_signals::GetProcessExePath(base::Process::Current().Pid());

  ASSERT_TRUE(test_runner_file_path.has_value());
  ASSERT_FALSE(test_runner_file_path->empty());

  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getFileSystemInfo);
      const userContext = {userId: '%s'};

      const executablePath = '%s';
      const fileItem = {
        path: executablePath,
        computeSha256: true,
        computeExecutableMetadata: true
      };

      const options = [fileItem];

      %s

      const request = { userContext, options };

   chrome.enterprise.reportingPrivate.getFileSystemInfo(
    request,
    (fileItems) => {
        chrome.test.assertNoLastError();

        %s

        chrome.test.notifyPass();
      });
  )";

  std::string extra_items = "";
#if BUILDFLAG(IS_WIN)
  device_signals::test::ScopedExecutableFiles scoped_executable_files;
  std::string signed_exe_path =
      scoped_executable_files.GetSignedExePath().AsUTF8Unsafe();
  base::ReplaceSubstringsAfterOffset(&signed_exe_path, 0U, "\\", "\\\\");

  std::string metadata_exe_path =
      scoped_executable_files.GetMetadataExePath().AsUTF8Unsafe();
  base::ReplaceSubstringsAfterOffset(&metadata_exe_path, 0U, "\\", "\\\\");

  extra_items = base::StringPrintf(
      R"(
    const signedExePath = '%s';
    options.push({
      path: signedExePath,
      computeSha256: true,
      computeExecutableMetadata: true
    });

    const metadataExePath = '%s';
    const metadataName = '%s';
    const metadataVersion = '%s';
    options.push({
      path: metadataExePath,
      computeSha256: true,
      computeExecutableMetadata: true
    });
  )",
      signed_exe_path.c_str(), metadata_exe_path.c_str(),
      scoped_executable_files.GetMetadataProductName().c_str(),
      scoped_executable_files.GetMetadataProductVersion().c_str());

  constexpr char kAssertions[] = R"(
        chrome.test.assertTrue(fileItems instanceof Array);
        chrome.test.assertEq(3, fileItems.length);

        let expectedFilesCounter = 0;
        for (const response of fileItems) {
          if (response.path === executablePath) {
            chrome.test.assertEq('FOUND', response.presence);
            chrome.test.assertTrue(!!response.sha256Hash);
            chrome.test.assertTrue(response.isRunning);
            chrome.test.assertEq([], response.publicKeysHashes);
            ++expectedFilesCounter;
          } else if (response.path === signedExePath) {
            chrome.test.assertEq('FOUND', response.presence);
            chrome.test.assertEq(
              '4R_6DJ8lI0RTqe3RyyUdRhB_NLU2rXRkKoWErKjBqM4',
              response.sha256Hash);
            chrome.test.assertEq(
              ['Rsw3wqh8gUxnMU8j2jGvvBMZqpe6OhIxn_WeEVg-pYQ'],
              response.publicKeysHashes);
            chrome.test.assertFalse(response.isRunning);
            chrome.test.assertFalse(!!response.productName);
            chrome.test.assertFalse(!!response.version);
            ++expectedFilesCounter;
          } else if (response.path === metadataExePath) {
            chrome.test.assertEq('FOUND', response.presence);
            chrome.test.assertEq(
              'bLHEy9cl0WbDjNsdsSCGp1wRGT0tdp8ML56xyrh0W48',
              response.sha256Hash);
            chrome.test.assertEq(metadataName, response.productName);
            chrome.test.assertEq(metadataVersion, response.version);
            chrome.test.assertEq([], response.publicKeysHashes);
            chrome.test.assertFalse(response.isRunning);
            ++expectedFilesCounter;
          }
        }
        chrome.test.assertEq(fileItems.length, expectedFilesCounter);
  )";
#elif BUILDFLAG(IS_MAC)
  std::string test_bundle_path =
      device_signals::test::GetTestBundlePath().AsUTF8Unsafe();

  extra_items = base::StringPrintf(
      R"(
    const testBundlePath = '%s';
    const testBundleProductName = '%s';
    const testBundleProductVersion = '%s';
    options.push({
      path: testBundlePath,
      computeSha256: true,
      computeExecutableMetadata: true
    });
  )",
      test_bundle_path.c_str(),
      device_signals::test::GetTestBundleProductName().c_str(),
      device_signals::test::GetTestBundleProductVersion().c_str());

  constexpr char kAssertions[] = R"(
        chrome.test.assertTrue(fileItems instanceof Array);
        chrome.test.assertEq(2, fileItems.length);

        let expectedFilesCounter = 0;
        for (const response of fileItems) {
          if (response.path === executablePath) {
            chrome.test.assertEq(executablePath, response.path);
            chrome.test.assertEq('FOUND', response.presence);
            chrome.test.assertTrue(!!response.sha256Hash);
            chrome.test.assertTrue(response.isRunning);
            ++expectedFilesCounter;
          } else if (response.path === testBundlePath) {
            chrome.test.assertEq('FOUND', response.presence);
            chrome.test.assertEq(
              't9gFsLVjhXKMPAz9KfxMX6lSDT_EFLtQXX4DJLrvxB8',
              response.sha256Hash);
            chrome.test.assertEq(testBundleProductName, response.productName);
            chrome.test.assertEq(testBundleProductVersion, response.version);
            chrome.test.assertEq(
              ['E7ahL43DGT2VrGvGpnlI9ONkEqdni9ddf4fCTN26uFc'],
              response.publicKeysHashes);
            chrome.test.assertFalse(response.isRunning);
            ++expectedFilesCounter;
          }
        }
        chrome.test.assertEq(fileItems.length, expectedFilesCounter);
  )";
#else
  constexpr char kAssertions[] = R"(
        chrome.test.assertTrue(fileItems instanceof Array);
        chrome.test.assertEq(1, fileItems.length);

        const fileItemResponse = fileItems[0];
        chrome.test.assertEq(executablePath, fileItemResponse.path);
        chrome.test.assertEq('FOUND', fileItemResponse.presence);
        chrome.test.assertTrue(!!fileItemResponse.sha256Hash);
        chrome.test.assertTrue(fileItemResponse.isRunning);
  )";
#endif  // BUILDFLAG(IS_WIN)

  // Escape all backslashes.
  std::string escaped_file_path = test_runner_file_path->AsUTF8Unsafe();
  base::ReplaceSubstringsAfterOffset(&escaped_file_path, 0U, "\\", "\\\\");

  AccountInfo account_info = SignIn("some-email@example.com");
  RunTest(base::StringPrintf(kTest, account_info.gaia.ToString().c_str(),
                             escaped_file_path.c_str(), extra_items.c_str(),
                             kAssertions));
}

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_MAC)
// TODO(http://crbug.com/1408618): Failing consistently on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_GetPlistSettings_Success DISABLED_GetPlistSettings_Success
#else
#define MAYBE_GetPlistSettings_Success GetPlistSettings_Success
#endif
IN_PROC_BROWSER_TEST_F(EnterpriseReportingPrivateApiTest,
                       MAYBE_GetPlistSettings_Success) {
  constexpr char kTest[] = R"(
      chrome.test.assertEq(
        'function',
        typeof chrome.enterprise.reportingPrivate.getSettings);
      const userContext = {userId: '%s'};

      const options = [];

      %s

      const request = {userContext, options};

   chrome.enterprise.reportingPrivate.getSettings(
    request,
    (settingItems) => {
        chrome.test.assertNoLastError();

        %s

        chrome.test.notifyPass();
      });
  )";

  std::string extra_items = base::StringPrintf(
      R"(
    const filePath = '%s';
    const validKeyPath = "Key1.SubKey1.SubSubKey1[0][10]";
    const invalidKeyPath = "Key1.SubKey1.SubSubKey1[0][0][3]";
    options.push({
      path: filePath,
      key: validKeyPath,
      getValue: true
    });
    options.push({
      path: filePath,
      key: invalidKeyPath,
      getValue: true
    });
  )",

      device_signals::test::GetMixArrayDictionaryPlistPath().value().c_str());

  constexpr char kAssertions[] = R"(
        chrome.test.assertTrue(settingItems instanceof Array);
        chrome.test.assertEq(2, settingItems.length);
        for (const response of settingItems) {
          chrome.test.assertEq(filePath, response.path);
          if (response.key == validKeyPath) {
            chrome.test.assertEq("FOUND", response.presence);
            chrome.test.assertEq(
              '\"string10\"', response.value);
          } else if (response.key == invalidKeyPath) {
            chrome.test.assertEq("NOT_FOUND", response.presence);
            chrome.test.assertEq(null, response.value);
          } else {
            chrome.test.fail();
          }
        }
  )";

  AccountInfo account_info = SignIn("some-email@example.com");
  RunTest(base::StringPrintf(kTest, account_info.gaia.ToString().c_str(),
                             extra_items.c_str(), kAssertions));
}
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_CHROMEOS)
static void RunTestUsingProfile(const std::string& background_js,
                                Profile* profile) {
  ResultCatcher result_catcher;
  TestExtensionDir test_dir;
  test_dir.WriteManifest(
      base::StringPrintf(kManifestTemplate, kAuthorizedManifestKey));

  // Since the API functions use async callbacks, this wrapper code is
  // necessary for assertions to work properly.
  constexpr char kTestWrapper[] = R"(
        chrome.test.runTests([
          async function asyncAssertions() {
            %s
          }
        ]);)";
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"),
                     base::StringPrintf(kTestWrapper, background_js.c_str()));

  ChromeTestExtensionLoader loader(profile);
  loader.set_ignore_manifest_warnings(true);

  const Extension* extension =
      loader.LoadExtension(test_dir.UnpackedPath()).get();
  ASSERT_TRUE(extension);
  ASSERT_TRUE(result_catcher.GetNextResult()) << result_catcher.message();
}

static std::string CreateValidRecord() {
  std::vector<uint8_t> serialized_record_data;
  std::string serialized_data = R"({"TEST_KEY":"TEST_VALUE"})";
  reporting::Record record;
  record.set_data(serialized_data);
  record.set_destination(reporting::Destination::TELEMETRY_METRIC);
  record.set_timestamp_us(base::Time::Now().InMillisecondsSinceUnixEpoch() *
                          base::Time::kMicrosecondsPerMillisecond);
  serialized_record_data.resize(record.SerializeAsString().size());
  record.SerializeToArray(serialized_record_data.data(),
                          serialized_record_data.size());

  // Print std::vector<uint8_t> into a form like "[1,2,3,4]"
  std::string serialized_record_data_str = "[";
  for (size_t i = 0; i < serialized_record_data.size(); i++) {
    if (i == serialized_record_data.size() - 1) {
      base::StrAppend(&serialized_record_data_str,
                      {base::NumberToString(serialized_record_data[i]), "]"});
    } else {
      base::StrAppend(&serialized_record_data_str,
                      {base::NumberToString(serialized_record_data[i]), ","});
    }
  }
  return serialized_record_data_str;
}

// Inheriting from DevicePolicyCrosBrowserTest enables use of AffiliationMixin
// for setting up profile/device affiliation. Only available in ChromeOS.
struct Params {
  explicit Params(bool affiliated) : affiliated(affiliated) {}
  // Whether the user is expected to be affiliated.
  bool affiliated;
};

class EnterpriseReportingPrivateEnqueueRecordApiTest
    : public ::policy::DevicePolicyCrosBrowserTest,
      public ::testing::WithParamInterface<Params> {
 protected:
  EnterpriseReportingPrivateEnqueueRecordApiTest() {
    affiliation_mixin_.set_affiliated(GetParam().affiliated);
    crypto_home_mixin_.MarkUserAsExisting(affiliation_mixin_.account_id());
    crypto_home_mixin_.ApplyAuthConfig(
        affiliation_mixin_.account_id(),
        ash::test::UserAuthConfig::Create(ash::test::kDefaultAuthSetup));
  }

  ~EnterpriseReportingPrivateEnqueueRecordApiTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ::policy::AffiliationTestHelper::AppendCommandLineSwitchesForLoginManager(
        command_line);
    ::policy::DevicePolicyCrosBrowserTest::SetUpCommandLine(command_line);
  }

  ::policy::DevicePolicyCrosTestHelper test_helper_;
  ::policy::AffiliationMixin affiliation_mixin_{&mixin_host_, &test_helper_};
  ash::CryptohomeMixin crypto_home_mixin_{&mixin_host_};
};

IN_PROC_BROWSER_TEST_P(EnterpriseReportingPrivateEnqueueRecordApiTest,
                       PRE_EnqueueRecord) {
  policy::AffiliationTestHelper::PreLoginUser(affiliation_mixin_.account_id());
}

IN_PROC_BROWSER_TEST_P(EnterpriseReportingPrivateEnqueueRecordApiTest,
                       EnqueueRecord) {
  policy::AffiliationTestHelper::LoginUser(affiliation_mixin_.account_id());

  constexpr char kTest[] = R"(

        const request = {
          eventType: "USER",
          priority: 4,
          recordData: Uint8Array.from(%s),
        };

        chrome.enterprise.reportingPrivate.enqueueRecord(request, () =>{
          %s
          chrome.test.succeed();
        });

      )";

  std::string javascript_assertion =
      GetParam().affiliated
          ? "chrome.test.assertNoLastError();"
          : base::StrCat({"chrome.test.assertLastError(\'",
                          EnterpriseReportingPrivateEnqueueRecordFunction::
                              kErrorProfileNotAffiliated,
                          "\');"});

  ASSERT_EQ(GetParam().affiliated,
            enterprise_util::IsProfileAffiliated(
                ash::ProfileHelper::Get()->GetProfileByAccountId(
                    affiliation_mixin_.account_id())));

  RunTestUsingProfile(base::StringPrintf(kTest, CreateValidRecord().c_str(),
                                         javascript_assertion.c_str()),
                      ash::ProfileHelper::Get()->GetProfileByAccountId(
                          affiliation_mixin_.account_id()));
}
INSTANTIATE_TEST_SUITE_P(TestAffiliation,
                         EnterpriseReportingPrivateEnqueueRecordApiTest,
                         ::testing::Values(Params(/*affiliated=*/true),
                                           Params(/*affiliated=*/false)));
#endif  // BUILDFLAG(IS_CHROMEOS)

class EnterpriseReportDataMaskingEventTest
    : public EnterpriseReportingPrivateApiTest {
 public:
  static constexpr char kTestJS[] = R"(
    chrome.test.assertEq(
      'function',
      typeof chrome.enterprise.reportingPrivate.reportDataMaskingEvent);
    chrome.enterprise.reportingPrivate.reportDataMaskingEvent(
        {
          "url": "https://foo.com",
          "eventResult": "EVENT_RESULT_DATA_MASKED",
          "triggeredRuleInfo": [
            {
              "ruleId": "1234",
              "ruleName": "Data Masking rule",
              "matchedDetectors": [
                {
                  "detectorId": "5678",
                  "displayName": "Credit card matcher",
                  "detectorType": "PREDEFINED_DLP"
                }
              ]
            }
          ]
        }, () => {
        chrome.test.assertNoLastError();
        chrome.test.notifyPass();
    });)";

  void SetUpOnMainThread() override {
    EnterpriseReportingPrivateApiTest::SetUpOnMainThread();
    event_report_validator_helper_ = std::make_unique<
        enterprise_connectors::test::EventReportValidatorHelper>(
        profile(), /*browser_test=*/true);
  }

  void TearDownOnMainThread() override {
    event_report_validator_helper_.reset();
    EnterpriseReportingPrivateApiTest::TearDownOnMainThread();
  }

 protected:
  std::unique_ptr<enterprise_connectors::test::EventReportValidatorHelper>
      event_report_validator_helper_;
};

IN_PROC_BROWSER_TEST_F(EnterpriseReportDataMaskingEventTest,
                       ReportingPolicyDisabled) {
  auto event_validator = event_report_validator_helper_->CreateValidator();
  event_validator.ExpectNoReport();
  enterprise_connectors::test::SetOnSecurityEventReporting(
      profile()->GetPrefs(), false, {}, {});

  RunTest(kTestJS);
}

IN_PROC_BROWSER_TEST_F(EnterpriseReportDataMaskingEventTest,
                       ReportingPolicyEnabled) {
  auto event_validator = event_report_validator_helper_->CreateValidator();

  api::enterprise_reporting_private::TriggeredRuleInfo rule_info;
  rule_info.rule_id = "1234";
  rule_info.rule_name = "Data Masking rule";
  rule_info.matched_detectors.push_back({});
  rule_info.matched_detectors[0].detector_id = "5678";
  rule_info.matched_detectors[0].display_name = "Credit card matcher";
  rule_info.matched_detectors[0].detector_type =
      api::enterprise_reporting_private::DetectorType::kPredefinedDlp;

  api::enterprise_reporting_private::DataMaskingEvent event;
  event.event_result =
      api::enterprise_reporting_private::EventResult::kEventResultDataMasked;
  event.url = "https://foo.com";
  event.triggered_rule_info.push_back(std::move(rule_info));
  base::RunLoop run_loop;
  event_validator.SetDoneClosure(run_loop.QuitClosure());
  event_validator.ExpectDataMaskingEvent("test-user@chromium.org",
                                         profile()->GetPath().AsUTF8Unsafe(),
                                         std::move(event));

  // Explicitly only enable sensitive data events only to avoid having to handle
  // assertions for extension install events.
  enterprise_connectors::test::SetOnSecurityEventReporting(
      profile()->GetPrefs(), true, {"sensitiveDataEvent"}, {});

  RunTest(kTestJS);
  run_loop.Run();
}

class EnterpriseOnDataMaskingRulesTriggeredTest
    : public EnterpriseReportingPrivateApiTest {
 public:
  EnterpriseOnDataMaskingRulesTriggeredTest() = default;

 private:
  base::test::ScopedFeatureList scoped_features_{
      extensions_features::
          kApiEnterpriseReportingPrivateOnDataMaskingRulesTriggered};
};

IN_PROC_BROWSER_TEST_F(EnterpriseOnDataMaskingRulesTriggeredTest,
                       WithoutRules) {
  static constexpr char kTestJS[] = R"(
    chrome.test.runTests([
      async function asyncAssertions() {
        chrome.enterprise.reportingPrivate.onDataMaskingRulesTriggered.addListener(
          rules => {
            if (rules.triggeredRuleInfo.length === 0) {
              chrome.test.fail(
                  'There should not be an event when no rules are triggered');
            } else {
              chrome.test.assertEq(rules, {
                  triggeredRuleInfo: [
                    {
                      matchedDetectors:[
                        {
                          detectorId: "12345",
                          displayName: "display_name",
                          maskType:'mask_type',
                          pattern:'pattern'
                        }
                      ],
                      ruleId:'rule_id',
                      ruleName:'rule_name'
                    }
                  ],
                  url:'https://foo.bar/'
                });
              chrome.test.succeed();
            }
          }
        );
      }
    ]);)";

  TestExtensionDir test_dir;
  test_dir.WriteManifest(
      base::StringPrintf(kManifestTemplate, kAuthorizedManifestKey));
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"), kTestJS);

  const Extension* extension = LoadExtension(test_dir.UnpackedPath());
  EXPECT_TRUE(extension);

  ResultCatcher result_catcher;

  auto* router = EnterpriseReportingPrivateEventRouterFactory::GetInstance()
                     ->GetForProfile(profile());

  // This first call should not produce any result as there are no triggered
  // rules in the response.
  router->OnUrlFilteringVerdict(GURL(kTestUrl),
                                safe_browsing::RTLookupResponse());

  safe_browsing::RTLookupResponse response;

  auto* rule =
      response.add_threat_info()->mutable_matched_url_navigation_rule();
  rule->set_rule_id("rule_id");
  rule->set_rule_name("rule_name");

  auto* data_masking = rule->add_data_masking_actions();
  data_masking->set_display_name("display_name");
  data_masking->set_mask_type("mask_type");
  data_masking->set_pattern("pattern");
  data_masking->set_detector_id("12345");

  router->OnUrlFilteringVerdict(GURL(kTestUrl), response);

  ASSERT_TRUE(result_catcher.GetNextResult()) << result_catcher.message();
}

IN_PROC_BROWSER_TEST_F(EnterpriseOnDataMaskingRulesTriggeredTest, WithRules) {
  static constexpr char kTestJS[] = R"(
    chrome.test.runTests([
      async function asyncAssertions() {
        chrome.enterprise.reportingPrivate.onDataMaskingRulesTriggered.addListener(
          rules => {
            chrome.test.assertEq(rules, {
              triggeredRuleInfo: [
                {
                  ruleId:'rule_id_1',
                  ruleName:'rule_name_1',
                  matchedDetectors:[
                    {
                      displayName: "display_name_1",
                      detectorId: "id_1",
                      maskType:'mask_type_1',
                      pattern:'pattern_1'
                    },
                    {
                      displayName: "display_name_2",
                      detectorId: "id_2",
                      maskType:'mask_type_2',
                      pattern:'pattern_2'
                    }
                  ],
                },
                {
                  ruleId:'rule_id_2',
                  ruleName:'rule_name_2',
                  matchedDetectors:[
                    {
                      displayName: "display_name_3",
                      detectorId: "id_3",
                      maskType:'mask_type_3',
                      pattern:'pattern_3'
                    }
                  ]
                }
              ],
              url:'https://foo.bar/'
            });
            chrome.test.succeed();
          }
        );
      }]);)";

  TestExtensionDir test_dir;
  test_dir.WriteManifest(
      base::StringPrintf(kManifestTemplate, kAuthorizedManifestKey));
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"), kTestJS);

  const Extension* extension = LoadExtension(test_dir.UnpackedPath());
  EXPECT_TRUE(extension);

  ResultCatcher result_catcher;

  safe_browsing::RTLookupResponse response;

  auto* rule_1 =
      response.add_threat_info()->mutable_matched_url_navigation_rule();
  rule_1->set_rule_id("rule_id_1");
  rule_1->set_rule_name("rule_name_1");

  auto* data_masking_1 = rule_1->add_data_masking_actions();
  data_masking_1->set_display_name("display_name_1");
  data_masking_1->set_mask_type("mask_type_1");
  data_masking_1->set_pattern("pattern_1");
  data_masking_1->set_detector_id("id_1");

  auto* data_masking_2 = rule_1->add_data_masking_actions();
  data_masking_2->set_display_name("display_name_2");
  data_masking_2->set_mask_type("mask_type_2");
  data_masking_2->set_pattern("pattern_2");
  data_masking_2->set_detector_id("id_2");

  auto* rule_2 =
      response.add_threat_info()->mutable_matched_url_navigation_rule();
  rule_2->set_rule_id("rule_id_2");
  rule_2->set_rule_name("rule_name_2");

  auto* data_masking_3 = rule_2->add_data_masking_actions();
  data_masking_3->set_display_name("display_name_3");
  data_masking_3->set_mask_type("mask_type_3");
  data_masking_3->set_pattern("pattern_3");
  data_masking_3->set_detector_id("id_3");

  EnterpriseReportingPrivateEventRouterFactory::GetInstance()
      ->GetForProfile(profile())
      ->OnUrlFilteringVerdict(GURL(kTestUrl), response);

  ASSERT_TRUE(result_catcher.GetNextResult()) << result_catcher.message();
}

}  // namespace extensions
