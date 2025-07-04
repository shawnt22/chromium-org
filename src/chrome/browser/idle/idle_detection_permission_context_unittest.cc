// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/idle/idle_detection_permission_context.h"

#include "base/test/task_environment.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/permissions/permission_decision.h"
#include "components/permissions/permission_request_data.h"
#include "components/permissions/permission_request_id.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"

namespace {

class TestIdleDetectionPermissionContext
    : public IdleDetectionPermissionContext {
 public:
  explicit TestIdleDetectionPermissionContext(Profile* profile)
      : IdleDetectionPermissionContext(profile) {}

  int permission_set_count() const { return permission_set_count_; }
  bool last_permission_set_persisted() const {
    return last_permission_set_persisted_;
  }
  PermissionDecision last_set_decision() const { return last_set_decision_; }

  ContentSetting GetContentSettingFromMap(const GURL& url_a,
                                          const GURL& url_b) {
    return HostContentSettingsMapFactory::GetForProfile(browser_context())
        ->GetContentSetting(url_a.DeprecatedGetOriginAsURL(),
                            url_b.DeprecatedGetOriginAsURL(),
                            content_settings_type());
  }

 private:
  // IdleDetectionPermissionContext:
  void NotifyPermissionSet(
      const permissions::PermissionRequestData& request_data,
      permissions::BrowserPermissionCallback callback,
      bool persist,
      PermissionDecision decision,
      bool is_final_decision) override {
    permission_set_count_++;
    last_permission_set_persisted_ = persist;
    last_set_decision_ = decision;
    IdleDetectionPermissionContext::NotifyPermissionSet(
        request_data, std::move(callback), persist, decision,
        is_final_decision);
  }

  int permission_set_count_ = 0;
  bool last_permission_set_persisted_ = false;
  PermissionDecision last_set_decision_ = PermissionDecision::kNone;
};

}  // namespace

class IdleDetectionPermissionContextTest
    : public ChromeRenderViewHostTestHarness {
 public:
  IdleDetectionPermissionContextTest()
      : ChromeRenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
};

// Tests auto-denial after a time delay in incognito.
TEST_F(IdleDetectionPermissionContextTest, TestDenyInIncognitoAfterDelay) {
  TestIdleDetectionPermissionContext permission_context(
      profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true));
  GURL url("https://www.example.com");
  NavigateAndCommit(url);

  const permissions::PermissionRequestID id(
      web_contents()->GetPrimaryMainFrame()->GetGlobalId(),
      permissions::PermissionRequestID::RequestLocalId());

  ASSERT_EQ(0, permission_context.permission_set_count());
  ASSERT_FALSE(permission_context.last_permission_set_persisted());
  ASSERT_EQ(PermissionDecision::kNone, permission_context.last_set_decision());

  permission_context.RequestPermission(
      std::make_unique<permissions::PermissionRequestData>(
          &permission_context, id,
          /*user_gesture=*/true, url),
      base::DoNothing());

  // Should be blocked after 1-2 seconds, but the timer is reset whenever the
  // tab is not visible, so these 500ms never add up to >= 1 second.
  for (int n = 0; n < 10; n++) {
    web_contents()->WasShown();
    task_environment()->FastForwardBy(base::Milliseconds(500));
    web_contents()->WasHidden();
  }

  EXPECT_EQ(0, permission_context.permission_set_count());
  EXPECT_EQ(CONTENT_SETTING_ASK,
            permission_context.GetContentSettingFromMap(url, url));

  // Time elapsed whilst hidden is not counted.
  // n.b. This line also clears out any old scheduled timer tasks. This is
  // important, because otherwise Timer::Reset (triggered by
  // VisibilityTimerTabHelper::WasShown) may choose to re-use an existing
  // scheduled task, and when it fires Timer::RunScheduledTask will call
  // TimeTicks::Now() (which unlike task_environment()->NowTicks(), we can't
  // fake), and miscalculate the remaining delay at which to fire the timer.
  task_environment()->FastForwardBy(base::Days(1));

  EXPECT_EQ(0, permission_context.permission_set_count());
  EXPECT_EQ(CONTENT_SETTING_ASK,
            permission_context.GetContentSettingFromMap(url, url));

  // Should be blocked after 1-2 seconds. So 500ms is not enough.
  web_contents()->WasShown();
  task_environment()->FastForwardBy(base::Milliseconds(500));

  EXPECT_EQ(0, permission_context.permission_set_count());
  EXPECT_EQ(CONTENT_SETTING_ASK,
            permission_context.GetContentSettingFromMap(url, url));

  // But 5*500ms > 2 seconds, so it should now be blocked.
  for (int n = 0; n < 4; n++)
    task_environment()->FastForwardBy(base::Milliseconds(500));

  EXPECT_EQ(1, permission_context.permission_set_count());
  EXPECT_TRUE(permission_context.last_permission_set_persisted());
  EXPECT_EQ(PermissionDecision::kDeny, permission_context.last_set_decision());
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            permission_context.GetContentSettingFromMap(url, url));
}

// Tests how multiple parallel permission requests get auto-denied in incognito.
TEST_F(IdleDetectionPermissionContextTest, TestParallelDenyInIncognito) {
  TestIdleDetectionPermissionContext permission_context(
      profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true));
  GURL url("https://www.example.com");
  NavigateAndCommit(url);
  web_contents()->WasShown();

  const permissions::PermissionRequestID id1(
      web_contents()->GetPrimaryMainFrame()->GetGlobalId(),
      permissions::PermissionRequestID::RequestLocalId(1));
  const permissions::PermissionRequestID id2(
      web_contents()->GetPrimaryMainFrame()->GetGlobalId(),
      permissions::PermissionRequestID::RequestLocalId(2));

  ASSERT_EQ(0, permission_context.permission_set_count());
  ASSERT_FALSE(permission_context.last_permission_set_persisted());
  ASSERT_EQ(PermissionDecision::kNone, permission_context.last_set_decision());

  permission_context.RequestPermission(
      std::make_unique<permissions::PermissionRequestData>(
          &permission_context, id1,
          /*user_gesture=*/true, url),
      base::DoNothing());
  permission_context.RequestPermission(
      std::make_unique<permissions::PermissionRequestData>(
          &permission_context, id2,
          /*user_gesture=*/true, url),
      base::DoNothing());

  EXPECT_EQ(0, permission_context.permission_set_count());
  EXPECT_EQ(CONTENT_SETTING_ASK,
            permission_context.GetContentSettingFromMap(url, url));

  // Fast forward up to 2.5 seconds. Stop as soon as the first permission
  // request is auto-denied.
  for (int n = 0; n < 5; n++) {
    task_environment()->FastForwardBy(base::Milliseconds(500));
    if (permission_context.permission_set_count())
      break;
  }

  // Only the first permission request receives a response (crbug.com/577336).
  EXPECT_EQ(1, permission_context.permission_set_count());
  EXPECT_TRUE(permission_context.last_permission_set_persisted());
  EXPECT_EQ(PermissionDecision::kDeny, permission_context.last_set_decision());
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            permission_context.GetContentSettingFromMap(url, url));

  // After another 2.5 seconds, the second permission request should also have
  // received a response.
  task_environment()->FastForwardBy(base::Milliseconds(2500));
  EXPECT_EQ(2, permission_context.permission_set_count());
  EXPECT_TRUE(permission_context.last_permission_set_persisted());
  EXPECT_EQ(PermissionDecision::kDeny, permission_context.last_set_decision());
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            permission_context.GetContentSettingFromMap(url, url));
}
