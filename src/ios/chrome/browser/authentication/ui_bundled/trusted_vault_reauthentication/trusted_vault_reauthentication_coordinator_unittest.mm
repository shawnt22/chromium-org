// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/authentication/ui_bundled/trusted_vault_reauthentication/trusted_vault_reauthentication_coordinator.h"

#import "base/test/ios/wait_util.h"
#import "components/sync/service/sync_service_utils.h"
#import "components/trusted_vault/trusted_vault_server_constants.h"
#import "ios/chrome/browser/authentication/ui_bundled/trusted_vault_reauthentication/trusted_vault_reauthentication_coordinator_delegate.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/browser/test/test_browser.h"
#import "ios/chrome/browser/shared/model/profile/test/test_profile_ios.h"
#import "ios/chrome/browser/signin/model/authentication_service.h"
#import "ios/chrome/browser/signin/model/authentication_service_factory.h"
#import "ios/chrome/browser/signin/model/fake_authentication_service_delegate.h"
#import "ios/chrome/browser/signin/model/fake_system_identity.h"
#import "ios/chrome/browser/signin/model/fake_system_identity_manager.h"
#import "ios/chrome/browser/signin/model/trusted_vault_client_backend_factory.h"
#import "ios/chrome/test/ios_chrome_scoped_testing_local_state.h"
#import "ios/chrome/test/providers/signin/fake_trusted_vault_client_backend.h"
#import "ios/chrome/test/scoped_key_window.h"
#import "ios/web/common/uikit_ui_util.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/gmock/include/gmock/gmock.h"
#import "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"
#import "third_party/ocmock/ocmock_extensions.h"

class TrustedVaultReauthenticationCoordinatorTest : public PlatformTest {
 public:
  TrustedVaultReauthenticationCoordinatorTest() {}

  void SetUp() override {
    PlatformTest::SetUp();

    base_view_controller_ = [[UIViewController alloc] init];
    base_view_controller_.view.backgroundColor = UIColor.blueColor;
    [scoped_key_window_.Get() setRootViewController:base_view_controller_];
    TestProfileIOS::Builder builder;
    builder.AddTestingFactory(
        AuthenticationServiceFactory::GetInstance(),
        AuthenticationServiceFactory::GetFactoryWithDelegate(
            std::make_unique<FakeAuthenticationServiceDelegate>()));
    profile_ = std::move(builder).Build();
    id<SystemIdentity> identity = [FakeSystemIdentity fakeIdentity1];
    FakeSystemIdentityManager* system_identity_manager =
        FakeSystemIdentityManager::FromSystemIdentityManager(
            GetApplicationContext()->GetSystemIdentityManager());
    system_identity_manager->AddIdentity(identity);
    AuthenticationService* authentication_service =
        AuthenticationServiceFactory::GetForProfile(profile_.get());
    authentication_service->SignIn(identity,
                                   signin_metrics::AccessPoint::kUnknown);

    browser_ = std::make_unique<TestBrowser>(profile_.get());
  }

  Browser* browser() { return browser_.get(); }

 protected:
  // Needed for test profile created by TestProfileIOS().
  web::WebTaskEnvironment task_environment_;
  IOSChromeScopedTestingLocalState scoped_testing_local_state_;

  std::unique_ptr<Browser> browser_;
  std::unique_ptr<TestProfileIOS> profile_;

  ScopedKeyWindow scoped_key_window_;
  UIViewController* base_view_controller_ = nil;
};

// Opens the trusted vault reauth dialog, and simulate a user cancel.
TEST_F(TrustedVaultReauthenticationCoordinatorTest, TestCancel) {
  // Create sign-in coordinator.
  trusted_vault::SecurityDomainId securityDomainID =
      trusted_vault::SecurityDomainId::kChromeSync;
  syncer::TrustedVaultUserActionTriggerForUMA trigger =
      syncer::TrustedVaultUserActionTriggerForUMA::kSettings;
  TrustedVaultReauthenticationCoordinator*
      trustedVaultReauthenticationCoordinator =
          [[TrustedVaultReauthenticationCoordinator alloc]
              initWithBaseViewController:base_view_controller_
                                 browser:browser()
                                  intent:SigninTrustedVaultDialogIntentFetchKeys
                        securityDomainID:securityDomainID
                                 trigger:trigger];
  id<TrustedVaultReauthenticationCoordinatorDelegate> delegate =
      OCMStrictProtocolMock(
          @protocol(TrustedVaultReauthenticationCoordinatorDelegate));
  trustedVaultReauthenticationCoordinator.delegate = delegate;

  // Open and cancel the web sign-in dialog.
  OCMExpect([delegate trustedVaultReauthenticationCoordinatorWantsToBeStopped:
                          trustedVaultReauthenticationCoordinator])
      .andDo(^(NSInvocation*) {
        [trustedVaultReauthenticationCoordinator stop];
      });

  [trustedVaultReauthenticationCoordinator start];
  // Wait until the view controller is presented.
  EXPECT_NE(nil, base_view_controller_.presentedViewController);
  EXPECT_TRUE(base::test::ios::WaitUntilConditionOrTimeout(
      base::test::ios::kWaitForUIElementTimeout, ^bool() {
        return !base_view_controller_.presentedViewController.beingPresented;
      }));

  // The TrustedVaultClientBackend instance is created by the provider API.
  // The test implementation returns a `FakeTrustedVaultClientBackend`. The
  // provider API implementation is selected at link time and since it is a
  // function, if multiple implementation are linked at the same time, the
  // linker will fail.
  //
  // The class `FakeTrustedVaultClientBackend` is defined in the same target
  // as the test implementation of the trusted_vault API. This means that if
  // the current executable succeeded at link time, it is guaranteed to use
  // the test implementation of the trusted_vault API (as the current target
  // depends on it, and a binary cannot depend on two version of the API).
  //
  // This means that it is safe to cast the `TrustedVaultClientBackend` to
  // `FakeTrustedVaultClientBackend` at runtime.
  static_cast<FakeTrustedVaultClientBackend*>(
      TrustedVaultClientBackendFactory::GetForProfile(profile_.get()))
      ->SimulateUserCancel();
  // Stop the coordinator while being opened.
  EXPECT_OCMOCK_VERIFY(delegate);
}

// Opens the trusted vault reauth dialog, and simulate a user cancel.
TEST_F(TrustedVaultReauthenticationCoordinatorTest, TestInterruptWithDismiss) {
  // Create sign-in coordinator.
  trusted_vault::SecurityDomainId securityDomainID =
      trusted_vault::SecurityDomainId::kChromeSync;
  syncer::TrustedVaultUserActionTriggerForUMA trigger =
      syncer::TrustedVaultUserActionTriggerForUMA::kSettings;
  TrustedVaultReauthenticationCoordinator*
      trustedVaultReauthenticationCoordinator =
          [[TrustedVaultReauthenticationCoordinator alloc]
              initWithBaseViewController:base_view_controller_
                                 browser:browser()
                                  intent:SigninTrustedVaultDialogIntentFetchKeys
                        securityDomainID:securityDomainID
                                 trigger:trigger];
  id<TrustedVaultReauthenticationCoordinatorDelegate> delegate =
      OCMStrictProtocolMock(
          @protocol(TrustedVaultReauthenticationCoordinatorDelegate));
  trustedVaultReauthenticationCoordinator.delegate = delegate;
  // Open and cancel the web sign-in dialog.
  [trustedVaultReauthenticationCoordinator start];
  // Wait until the view controller is presented.
  EXPECT_NE(nil, base_view_controller_.presentedViewController);
  EXPECT_TRUE(base::test::ios::WaitUntilConditionOrTimeout(
      base::test::ios::kWaitForUIElementTimeout, ^bool() {
        return !base_view_controller_.presentedViewController.beingPresented;
      }));
  // Stop the coordinator while being opened.
  [trustedVaultReauthenticationCoordinator stop];
  EXPECT_OCMOCK_VERIFY(delegate);
}
