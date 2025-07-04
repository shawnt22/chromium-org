// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/incognito_reauth/ui_bundled/incognito_reauth_scene_agent.h"

#import "base/feature_list.h"
#import "base/test/scoped_feature_list.h"
#import "base/test/scoped_mock_clock_override.h"
#import "base/time/time.h"
#import "components/prefs/testing_pref_service.h"
#import "ios/chrome/browser/incognito_reauth/ui_bundled/features.h"
#import "ios/chrome/browser/incognito_reauth/ui_bundled/incognito_reauth_constants.h"
#import "ios/chrome/browser/shared/coordinator/scene/scene_activation_level.h"
#import "ios/chrome/browser/shared/coordinator/scene/scene_controller.h"
#import "ios/chrome/browser/shared/coordinator/scene/test/stub_browser_provider_interface.h"
#import "ios/chrome/browser/shared/model/browser/browser_provider_interface.h"
#import "ios/chrome/browser/shared/model/browser/test/test_browser.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/model/profile/test/test_profile_ios.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_opener.h"
#import "ios/chrome/browser/shared/public/commands/application_commands.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/chrome/browser/shared/public/commands/tab_grid_commands.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/common/ui/reauthentication/reauthentication_protocol.h"
#import "ios/web/public/test/fakes/fake_web_state.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"

#pragma mark - StubReauthenticationModule

@interface StubReauthenticationModule : NSObject <ReauthenticationProtocol>

@property(nonatomic, assign) BOOL canAttemptReauthWithBiometrics;
@property(nonatomic, assign) BOOL canAttemptReauth;
@property(nonatomic, assign) ReauthenticationResult returnedResult;

@end

@implementation StubReauthenticationModule

- (void)attemptReauthWithLocalizedReason:(NSString*)localizedReason
                    canReusePreviousAuth:(BOOL)canReusePreviousAuth
                                 handler:
                                     (void (^)(ReauthenticationResult success))
                                         handler {
  handler(self.returnedResult);
}

@end

namespace {

#pragma mark - IncognitoReauthSceneAgentTest

class IncognitoReauthSceneAgentTest : public PlatformTest {
 public:
  IncognitoReauthSceneAgentTest()
      : profile_(TestProfileIOS::Builder().Build()),
        scene_state_([[SceneState alloc] initWithAppState:nil]),
        scene_state_mock_(OCMPartialMock(scene_state_)),
        scene_controller_(
            [[SceneController alloc] initWithSceneState:scene_state_]),
        scene_controller_mock_(OCMPartialMock(scene_controller_)),
        stub_reauth_module_([[StubReauthenticationModule alloc] init]),
        application_commands_handler_mock_(
            OCMProtocolMock(@protocol(ApplicationCommands))),
        tab_grid_commands_handler_mock_(
            OCMProtocolMock(@protocol(TabGridCommands))),
        agent_([[IncognitoReauthSceneAgent alloc]
                  initWithReauthModule:stub_reauth_module_
            applicationCommandsHandler:application_commands_handler_mock_]) {
    scene_state_.controller = scene_controller_;
    // Set UIEnabled here as this would trigger a callback in the agent, and we
    // usually test the behavior when foregrounding. When testing the UIEnabled
    // callback, we first set it to NO.
    scene_state_.UIEnabled = YES;
    scene_state_.activationLevel = SceneActivationLevelForegroundInactive;
    [scene_state_ addAgent:agent_];
  }

  ~IncognitoReauthSceneAgentTest() override {
    EXPECT_OCMOCK_VERIFY(scene_state_mock_);
    EXPECT_OCMOCK_VERIFY(scene_controller_mock_);
    EXPECT_OCMOCK_VERIFY(application_commands_handler_mock_);
    EXPECT_OCMOCK_VERIFY(tab_grid_commands_handler_mock_);
  }

 protected:
  void SetUpTestObjects(int tab_count,
                        bool reauth_enabled,
                        bool soft_lock_feature_enabled,
                        bool soft_lock_pref_enabled) {
    // Stub all calls to be able to mock the following:
    // 1. sceneState.browserProviderInterface.incognitoBrowserProvider
    //            .browser->GetWebStateList()->count()
    // 2. sceneState.browserProviderInterface.hasIncognitoBrowserProvider
    test_browser_ = std::make_unique<TestBrowser>(profile_.get());
    for (int i = 0; i < tab_count; ++i) {
      test_browser_->GetWebStateList()->InsertWebState(
          std::make_unique<web::FakeWebState>(),
          WebStateList::InsertionParams::AtIndex(i));
    }

    stub_browser_interface_provider_ =
        [[StubBrowserProviderInterface alloc] init];
    stub_browser_interface_provider_.incognitoBrowserProvider.browser =
        test_browser_.get();

    OCMStub([scene_state_mock_ browserProviderInterface])
        .andReturn(stub_browser_interface_provider_);

    CommandDispatcher* dispatcher = test_browser_->GetCommandDispatcher();
    [dispatcher startDispatchingToTarget:tab_grid_commands_handler_mock_
                             forProtocol:@protocol(TabGridCommands)];

    [IncognitoReauthSceneAgent registerLocalState:pref_service_.registry()];
    agent_.localState = &pref_service_;
    pref_service_.SetBoolean(prefs::kIncognitoAuthenticationSetting,
                             reauth_enabled);
    feature_list_.InitWithFeatureState(kIOSSoftLock, soft_lock_feature_enabled);
    pref_service_.SetBoolean(prefs::kIncognitoSoftLockSetting,
                             soft_lock_pref_enabled);
  }

  void SetUpTestObjects(int tab_count, bool enable_pref) {
    SetUpTestObjects(tab_count, enable_pref,
                     /*soft_lock_feature_enabled=*/false,
                     /*soft_lock_pref_enabled=*/false);
  }

  void SetUp() override {
    // Set up default stub reauth module behavior.
    stub_reauth_module_.canAttemptReauthWithBiometrics = YES;
    stub_reauth_module_.canAttemptReauth = YES;
    stub_reauth_module_.returnedResult = ReauthenticationResult::kSuccess;
  }

  void TearDown() override { scene_state_.UIEnabled = NO; }

  void AdvanceClock(const base::TimeDelta& delay) {
    scoped_clock_.Advance(delay);
  }

  void RecordCurrentTimeInPref() {
    pref_service_.SetTime(prefs::kLastBackgroundedTime, scoped_clock_.Now());
  }

  web::WebTaskEnvironment task_environment_;
  std::unique_ptr<TestProfileIOS> profile_;

  // The scene state that the agent works with.
  SceneState* scene_state_;
  // Partial mock for stubbing scene_state_'s methods
  id scene_state_mock_;
  SceneController* scene_controller_;
  id scene_controller_mock_;
  StubReauthenticationModule* stub_reauth_module_;
  id application_commands_handler_mock_;
  id tab_grid_commands_handler_mock_;
  // The tested agent
  IncognitoReauthSceneAgent* agent_;
  StubBrowserProviderInterface* stub_browser_interface_provider_;
  std::unique_ptr<TestBrowser> test_browser_;
  TestingPrefServiceSimple pref_service_;
  base::test::ScopedFeatureList feature_list_;
  base::ScopedMockClockOverride scoped_clock_;
};

// Test that when the feature pref is disabled, auth isn't required.
TEST_F(IncognitoReauthSceneAgentTest, PrefDisabled) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*enable_pref=*/false);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_FALSE(agent_.authenticationRequired);
}

// Test that when the feature is enabled, we're foregrounded with some incognito
// content already present, auth is required
TEST_F(IncognitoReauthSceneAgentTest, NeedsAuth) {
  SetUpTestObjects(/*tab_count=*/1, /*enable_pref=*/true);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that when auth is required and is successfully performed, it's not
// required anymore.
TEST_F(IncognitoReauthSceneAgentTest, SuccessfulAuth) {
  SetUpTestObjects(/*tab_count=*/1, /*enable_pref=*/true);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);

  [agent_ authenticateIncognitoContent];

  // Auth not required
  EXPECT_FALSE(agent_.authenticationRequired);

  // Auth required after backgrounding.
  scene_state_.activationLevel = SceneActivationLevelBackground;
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
  EXPECT_TRUE(agent_.authenticationRequired);
}

// Tests that authentication is still required if authentication fails.
TEST_F(IncognitoReauthSceneAgentTest, FailedSkippedAuth) {
  SetUpTestObjects(/*tab_count=*/1, /*enable_pref=*/true);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);

  stub_reauth_module_.returnedResult = ReauthenticationResult::kFailure;

  [agent_ authenticateIncognitoContent];
  // Auth still required
  EXPECT_TRUE(agent_.authenticationRequired);

  stub_reauth_module_.returnedResult = ReauthenticationResult::kSkipped;
  [agent_ authenticateIncognitoContent];
  // Auth still required
  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that when the feature is enabled, auth is required if we foreground
// without any incognito tabs.
TEST_F(IncognitoReauthSceneAgentTest, AuthRequiredWhenNoIncognitoTabs) {
  SetUpTestObjects(/*tab_count=*/0, /*enable_pref=*/true);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that when the feature is enabled, we're foregrounded with some incognito
// content already present, auth is required.
TEST_F(IncognitoReauthSceneAgentTest,
       AuthRequiredWhenNoIncognitoTabsOnForeground) {
  SetUpTestObjects(/*tab_count=*/0, /*enable_pref=*/true);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);

  // Open another tab.
  test_browser_->GetWebStateList()->InsertWebState(
      std::make_unique<web::FakeWebState>(),
      WebStateList::InsertionParams::AtIndex(0));

  EXPECT_TRUE(agent_.authenticationRequired);
}

#pragma mark - Soft Lock tests

// Test that when both reauth and soft lock are disabled, no overlay is
// displayed.
TEST_F(IncognitoReauthSceneAgentTest, AllFeaturesDisabled) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);

  // Satisfy soft lock conditions
  RecordCurrentTimeInPref();
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_EQ(agent_.incognitoLockState, IncognitoLockState::kNone);
}

// Test that the correct overlay is displayed when both reauth and soft lock are
// enabled.
TEST_F(IncognitoReauthSceneAgentTest, AllFeaturesEnabled) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Satisfy soft lock conditions
  RecordCurrentTimeInPref();
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_EQ(agent_.incognitoLockState, IncognitoLockState::kReauth);
}

// Test that when unlock is required and is successfully performed, it's
// not required anymore.
TEST_F(IncognitoReauthSceneAgentTest, SuccessfulSoftUnlock) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Satisfy soft lock conditions
  RecordCurrentTimeInPref();
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);

  [agent_ authenticateIncognitoContent];

  // Auth not required
  EXPECT_FALSE(agent_.authenticationRequired);

  // Auth required after backgrounding.
  scene_state_.activationLevel = SceneActivationLevelBackground;
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that when soft lock is enabled, unlock isn't required if we foreground
// without any incognito tabs.
TEST_F(IncognitoReauthSceneAgentTest,
       SoftUnlockNotRequiredWhenNoIncognitoTabs) {
  SetUpTestObjects(/*tab_count=*/0, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Satisfy soft lock conditions
  RecordCurrentTimeInPref();
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_FALSE(agent_.authenticationRequired);
}

// Test that when soft lock is enabled, we're foregrounded with some incognito
// content already present, unlock is not required.
TEST_F(IncognitoReauthSceneAgentTest,
       SoftUnlockNotRequiredWhenNoIncognitoTabsOnForeground) {
  SetUpTestObjects(/*tab_count=*/0, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Satisfy soft lock conditions
  RecordCurrentTimeInPref();
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_FALSE(agent_.authenticationRequired);

  // Open another tab.
  test_browser_->GetWebStateList()->InsertWebState(
      std::make_unique<web::FakeWebState>(),
      WebStateList::InsertionParams::AtIndex(0));

  EXPECT_FALSE(agent_.authenticationRequired);
}

// Test that unlock is not required when we have not cached a value for the
// pref.
TEST_F(IncognitoReauthSceneAgentTest, SoftLockNotRequiredWithoutCachedPref) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_FALSE(agent_.authenticationRequired);
}

// Test that unlock is not required when we have saved a value for the pref,
// that is less than the threshold.
TEST_F(IncognitoReauthSceneAgentTest,
       SoftLockNotRequiredWithPrefBeforeThreshold) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);
  RecordCurrentTimeInPref();

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_FALSE(agent_.authenticationRequired);
}

// Test that unlock is required when we have saved a value for the pref that is
// more than the threshold.
TEST_F(IncognitoReauthSceneAgentTest, SoftLockRequiredWithPrefAfterThreshold) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Satisfy soft lock conditions.
  RecordCurrentTimeInPref();
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that unlock is not required when we background the app and foreground
// before the time threshold elapses.
TEST_F(IncognitoReauthSceneAgentTest,
       SoftLockNotRequiredWhenForegroundingBeforeThreshold) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Go background.
  scene_state_.activationLevel = SceneActivationLevelBackground;

  EXPECT_FALSE(agent_.authenticationRequired);

  // Foreground the app.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_FALSE(agent_.authenticationRequired);
}

// Test that unlock is required when we background the app and foreground after
// the time threshold elapses.
TEST_F(IncognitoReauthSceneAgentTest,
       SoftLockRequiredWhenForegroundingAfterThreshold) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Go background.
  scene_state_.activationLevel = SceneActivationLevelBackground;

  EXPECT_FALSE(agent_.authenticationRequired);

  // Advance the clock and foreground the app.
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that when unlock is required, backgrounding and foregrounding the app
// does not unlock Incognito.
TEST_F(IncognitoReauthSceneAgentTest,
       SoftLockRequiredDoesNotResetOnBackground) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);

  // Go background.
  scene_state_.activationLevel = SceneActivationLevelBackground;

  EXPECT_FALSE(agent_.authenticationRequired);

  // Advance the clock and foreground the app.
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);

  // Re-background and foreground
  scene_state_.activationLevel = SceneActivationLevelBackground;
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_TRUE(agent_.authenticationRequired);
}

// Test that, if the conditions are met, the screen transitions on foreground.
TEST_F(IncognitoReauthSceneAgentTest, TestScreenTransitionOnForeground) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(NO);
  scene_state_.incognitoContentVisible = YES;

  OCMExpect([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}

// Test that, if the conditions are met, the screen transitions on UI enabled.
TEST_F(IncognitoReauthSceneAgentTest, TestScreenTransitionOnUIEnabled) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(NO);
  scene_state_.UIEnabled = NO;
  scene_state_.incognitoContentVisible = YES;
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  OCMExpect([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);

  // Enabled UI
  scene_state_.UIEnabled = YES;
}

// Test that no transition occurs when no lock surface is displayed.
TEST_F(IncognitoReauthSceneAgentTest, TestNoScreenTransitionOnNoLock) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  OCMReject([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);

  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(NO);
  scene_state_.incognitoContentVisible = YES;

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}

// Test that no transition occurs when UI is disabled.
TEST_F(IncognitoReauthSceneAgentTest, TestNoScreenTransitionOnUIDisabled) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  OCMReject([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);

  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(NO);
  scene_state_.UIEnabled = NO;
  scene_state_.incognitoContentVisible = YES;

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}

// Test that no transition occurs when the normal browser interface is
// displayed.
TEST_F(IncognitoReauthSceneAgentTest, TestNoScreenTransitionOnNormalInterface) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  OCMReject([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);

  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(NO);
  scene_state_.incognitoContentVisible = NO;

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}

// Test that no transition occurs if we area already on the tab grid.
TEST_F(IncognitoReauthSceneAgentTest, TestNoScreenTransitionOnTabGrid) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  OCMReject([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);

  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(YES);
  scene_state_.incognitoContentVisible = YES;

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;
}

// Test that, if the conditions are met, the screen transitions to the tab.
TEST_F(IncognitoReauthSceneAgentTest, TestScreenTransitionToTab) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  // Satisfy transition conditions.
  OCMExpect([scene_controller_mock_ isTabGridVisible]).andReturn(NO);
  OCMExpect([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);
  scene_state_.incognitoContentVisible = YES;

  // Go to foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_OCMOCK_VERIFY(application_commands_handler_mock_);

  // Test reverse transition, tab grid to tab.
  OCMExpect([scene_controller_mock_ isTabGridVisible]).andReturn(YES);
  OCMExpect([tab_grid_commands_handler_mock_ exitTabGrid]);

  [agent_ authenticateIncognitoContent];
}

// Test that, if the conditions are not met, the screen does not transition to
// the tab.
TEST_F(IncognitoReauthSceneAgentTest, TestNoScreenTransitionToTab) {
  SetUpTestObjects(/*tab_count=*/1,
                   /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  // Satisfy transition conditions.
  OCMStub([scene_controller_mock_ isTabGridVisible]).andReturn(YES);
  OCMReject([application_commands_handler_mock_
      displayTabGridInMode:TabGridOpeningMode::kIncognito]);
  scene_state_.incognitoContentVisible = YES;

  // Go to foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_OCMOCK_VERIFY(application_commands_handler_mock_);

  // Test reverse transition, tab grid to tab.
  OCMReject([tab_grid_commands_handler_mock_ exitTabGrid]);

  [agent_ authenticateIncognitoContent];
}

// Test that soft lock is not required when Chrome was launched via an external
// intent.
TEST_F(IncognitoReauthSceneAgentTest, NoSoftLockOnExternalIntents) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/false,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/true);
  scene_state_.startupHadExternalIntent = YES;

  // Advance the clock and foreground the app.
  AdvanceClock(kIOSSoftLockBackgroundThreshold.Get());
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_EQ(agent_.incognitoLockState, IncognitoLockState::kNone);
}

// Test that reauth is required when Chrome was launched via an external intent.
TEST_F(IncognitoReauthSceneAgentTest, ReauthOnExternalIntents) {
  SetUpTestObjects(/*tab_count=*/1, /*reauth_enabled=*/true,
                   /*soft_lock_feature_enabled=*/true,
                   /*soft_lock_pref_enabled=*/false);
  scene_state_.startupHadExternalIntent = YES;

  // Go foreground.
  scene_state_.activationLevel = SceneActivationLevelForegroundActive;

  EXPECT_EQ(agent_.incognitoLockState, IncognitoLockState::kReauth);
}

}  // namespace
