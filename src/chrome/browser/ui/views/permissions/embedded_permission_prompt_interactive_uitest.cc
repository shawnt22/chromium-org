// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <queue>
#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/system/system_permission_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_ask_view.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_base_view.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_content_scrim_view.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_previously_denied_view.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_previously_granted_view.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_show_system_prompt_view.h"
#include "chrome/browser/ui/views/permissions/embedded_permission_prompt_system_settings_view.h"
#include "chrome/browser/ui/views/permissions/permission_prompt_bubble_base_view.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/permissions/features.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/permission_uma_util.h"
#include "components/ukm/test_ukm_recorder.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "third_party/blink/public/common/features_generated.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/ozone_buildflags.h"
#include "ui/events/base_event_utils.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/views_switches.h"
#include "ui/views/widget/any_widget_observer.h"
#include "url/origin.h"

namespace {
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWebContentsElementId);
DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kPEPCVisibleEvent);
DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kDoneVisibleEvent);

using UkmEntry = ukm::builders::Permissions_EmbeddedPromptAction;

constexpr int kMinWindowHeight = 400;
}  // namespace

class EmbeddedPermissionPromptInteractiveTest
    : public InteractiveBrowserTest,
      public testing::WithParamInterface<float> {
 public:
  EmbeddedPermissionPromptInteractiveTest() {
    // Force the scale factor to test that the prompt is positioned correctly
    // for all device scale factors.
    display::Display::SetForceDeviceScaleFactor(GetParam());

    https_server_ = std::make_unique<net::EmbeddedTestServer>(
        net::EmbeddedTestServer::TYPE_HTTPS);
    feature_list_.InitWithFeatures(
        {blink::features::kPermissionElement,
         blink::features::kBypassPepcSecurityForTesting},
        {});
  }

  ~EmbeddedPermissionPromptInteractiveTest() override = default;
  EmbeddedPermissionPromptInteractiveTest(
      const EmbeddedPermissionPromptInteractiveTest&) = delete;
  void operator=(const EmbeddedPermissionPromptInteractiveTest&) = delete;

  void SetUp() override {
    https_server()->SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    https_server()->ServeFilesFromSourceDirectory(GetChromeTestDataDir());

    ASSERT_TRUE(https_server()->InitializeAndListen());
    InteractiveBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    InteractiveBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    content::SetupCrossSiteRedirector(https_server());
    https_server()->StartAcceptingConnections();
    ukm_recorder_ = std::make_unique<ukm::TestAutoSetUkmRecorder>();

    // Force the window to be large enough.
    BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->SetBounds(
        {10, 10, 800, 800});
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InteractiveBrowserTestT::SetUpCommandLine(command_line);
    // Disables the disregarding of potentially unintended input events.
    command_line->AppendSwitch(
        views::switches::kDisableInputEventActivationProtectionForTesting);
  }

  void TearDownOnMainThread() override {
    EXPECT_TRUE(https_server()->ShutdownAndWaitUntilComplete());
    InteractiveBrowserTest::TearDownOnMainThread();
  }

  net::EmbeddedTestServer* https_server() { return https_server_.get(); }

  ui::ElementContext context() const {
    return browser()->window()->GetElementContext();
  }

  GURL GetOrigin() { return url::Origin::Create(GetURL()).GetURL(); }

  GURL GetURL() {
    return https_server()->GetURL("a.test",
                                  "/permissions/permission_element.html");
  }

  auto ClickOnPEPCElement(const std::string& element_id) {
    StateChange pepc_visible;
    pepc_visible.where = DeepQuery{"#" + element_id};
    pepc_visible.type = StateChange::Type::kExists;
    pepc_visible.event = kPEPCVisibleEvent;
    return Steps(
        WaitForStateChange(kWebContentsElementId, pepc_visible),
        ExecuteJsAt(kWebContentsElementId, pepc_visible.where, "click"));
  }

  auto PushPEPCPromptButton(ui::ElementIdentifier button_identifier,
                            bool wait_for_prompt_resolution = true) {
    if (wait_for_prompt_resolution) {
      return InAnyContext(
          WaitForShow(button_identifier), PressButton(button_identifier),
          WaitForHide(EmbeddedPermissionPromptBaseView::kMainViewId));
    } else {
      return InAnyContext(WaitForShow(button_identifier),
                          PressButton(button_identifier));
    }
  }

  // Checks that the next value in the queue matches the text in the label
  // element. If the queue is empty or the popped value is empty, checks that
  // the label is not present instead. Pops the front of the queue if the queues
  // is not empty.
  auto CheckLabel(ui::ElementIdentifier label_identifier,
                  std::vector<std::u16string>& expected_labels,
                  size_t expected_label_index) {
    if (expected_label_index < expected_labels.size()) {
      return InAnyContext(
          CheckViewProperty(label_identifier, &views::Label::GetText,
                            expected_labels[expected_label_index]));
    } else {
      return InAnyContext(EnsureNotPresent(label_identifier));
    }
  }

  auto CheckContentSettingsValue(
      const std::vector<ContentSettingsType>& content_settings_types,
      const ContentSetting& expected_value) {
    return Steps(CheckResult(
        [&, this]() {
          return DoContentSettingsHaveValue(content_settings_types,
                                            expected_value);
        },
        true));
  }

  auto CheckHistogram(const base::HistogramTester& tester,
                      const std::string& view_name,
                      permissions::RequestTypeForUma request_type,
                      int count) {
    return Steps(Do([=, &tester]() {
      tester.ExpectBucketCount(
          view_name, static_cast<base::HistogramBase::Sample32>(request_type),
          count);
    }));
  }

  auto CheckNoUkmEntriesSinceLastCheck() {
    return Steps(Check([this]() {
      size_t entry_count =
          ukm_recorder_->GetEntriesByName(UkmEntry::kEntryName).size();
      ukm_recorder_ = std::make_unique<ukm::TestAutoSetUkmRecorder>();
      return entry_count == 0U;
    }));
  }

  auto CheckEntrySinceLastCheck(
      permissions::RequestTypeForUma permission,
      permissions::RequestTypeForUma screen_permission,
      permissions::ElementAnchoredBubbleAction action,
      permissions::ElementAnchoredBubbleVariant variant,
      int screen_counter) {
    return Steps(Do([=, this] {
      auto entries = ukm_recorder_->GetEntriesByName(UkmEntry::kEntryName);
      CHECK_EQ(entries.size(), 1U);

      ukm_recorder_->ExpectEntryMetric(entries[0],
                                       UkmEntry::kPermissionTypeName,
                                       static_cast<int64_t>(permission));
      ukm_recorder_->ExpectEntryMetric(entries[0],
                                       UkmEntry::kScreenPermissionTypeName,
                                       static_cast<int64_t>(screen_permission));
      ukm_recorder_->ExpectEntryMetric(entries[0], UkmEntry::kActionName,
                                       static_cast<int64_t>(action));
      ukm_recorder_->ExpectEntryMetric(entries[0], UkmEntry::kVariantName,
                                       static_cast<int64_t>(variant));
      ukm_recorder_->ExpectEntryMetric(entries[0],
                                       UkmEntry::kPreviousScreensName,
                                       static_cast<int64_t>(screen_counter));
      ukm_recorder_ = std::make_unique<ukm::TestAutoSetUkmRecorder>();
    }));
  }

  bool DoContentSettingsHaveValue(
      const std::vector<ContentSettingsType>& content_settings_types,
      ContentSetting expected_value) {
    HostContentSettingsMap* hcsm =
        HostContentSettingsMapFactory::GetForProfile(browser()->profile());
    for (const auto& type : content_settings_types) {
      if (expected_value !=
          hcsm->GetContentSetting(GetOrigin(), GetOrigin(), type)) {
        return false;
      }
    }

    return true;
  }

  void SetContentSetting(ContentSettingsType type, ContentSetting setting) {
    HostContentSettingsMap* hcsm =
        HostContentSettingsMapFactory::GetForProfile(browser()->profile());
    hcsm->SetContentSettingDefaultScope(GetOrigin(), GetOrigin(), type,
                                        setting);
  }

  // Tests
  void TestAskBlockAllowFlow(
      const std::string& element_id,
      const std::vector<ContentSettingsType>& content_settings_types,
      // Deliberately passing through value to make a locally modifiable copy.
      std::vector<std::u16string> expected_titles =
          std::vector<std::u16string>(),
      std::vector<std::u16string> expected_labels1 =
          std::vector<std::u16string>(),
      std::vector<std::u16string> expected_labels2 =
          std::vector<std::u16string>()) {
    RunTestSequence(
        InstrumentTab(kWebContentsElementId),
        NavigateWebContents(kWebContentsElementId, GetURL()),

        // Initially the Ask view is displayed.
        ClickOnPEPCElement(element_id),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
        CheckLabel(EmbeddedPermissionPromptBaseView::kTitleViewId,
                   expected_titles, /*expected_label_index=*/0),
        CheckLabel(EmbeddedPermissionPromptBaseView::kLabelViewId1,
                   expected_labels1, /*expected_label_index=*/0),
        CheckLabel(EmbeddedPermissionPromptBaseView::kLabelViewId2,
                   expected_labels2, /*expected_label_index=*/0),

        // After allowing, the content setting is updated accordingly.
        PushPEPCPromptButton(EmbeddedPermissionPromptAskView::kAllowId),
        CheckContentSettingsValue(content_settings_types,
                                  CONTENT_SETTING_ALLOW),

        // The PreviouslyGranted view is displayed since the permission is
        // granted.
        ClickOnPEPCElement(element_id),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
        CheckLabel(EmbeddedPermissionPromptBaseView::kTitleViewId,
                   expected_titles, /*expected_label_index=*/1),
        CheckLabel(EmbeddedPermissionPromptBaseView::kLabelViewId1,
                   expected_labels1, /*expected_label_index=*/1),
        CheckLabel(EmbeddedPermissionPromptBaseView::kLabelViewId2,
                   expected_labels2, /*expected_label_index=*/1),

        // Click on "Stop Allowing" and observe the content setting change.
        PushPEPCPromptButton(
            EmbeddedPermissionPromptPreviouslyGrantedView::kStopAllowingId),
        CheckContentSettingsValue(content_settings_types,
                                  CONTENT_SETTING_BLOCK),

        // The PreviouslyBlocked view is displayed since the permission is
        // blocked.
        ClickOnPEPCElement(element_id),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
        CheckLabel(EmbeddedPermissionPromptBaseView::kTitleViewId,
                   expected_titles, /*expected_label_index=*/2),
        CheckLabel(EmbeddedPermissionPromptBaseView::kLabelViewId1,
                   expected_labels1, /*expected_label_index=*/2),
        CheckLabel(EmbeddedPermissionPromptBaseView::kLabelViewId2,
                   expected_labels2, /*expected_label_index=*/2),

        // Click on "Allow this time" and observe the content setting change.
        PushPEPCPromptButton(
            EmbeddedPermissionPromptPreviouslyDeniedView::kAllowThisTimeId),
        CheckContentSettingsValue(content_settings_types,
                                  CONTENT_SETTING_ALLOW),

        // After the last tab is closed, since the last grant was one-time,
        // ensure the content setting is reset.
        Do([this]() {
          browser()->tab_strip_model()->GetActiveWebContents()->Close();
        }),
        // This has to be immediate, because otherwise closing the browser will
        // detach the profile.
        WithoutDelay(CheckContentSettingsValue(content_settings_types,
                                               CONTENT_SETTING_ASK)));
  }

  void TestPromptElementText(
      ContentSetting camera_setting,
      ContentSetting mic_setting,
      std::map<ui::ElementIdentifier, const std::u16string>& expected_labels,
      bool check_buttons) {
    auto steps = Steps(
        // Set the initial settings values.
        Do([&, this]() {
          SetContentSetting(ContentSettingsType::MEDIASTREAM_CAMERA,
                            camera_setting);
          SetContentSetting(ContentSettingsType::MEDIASTREAM_MIC, mic_setting);
        }),

        // Trigger a camera+mic prompt and check that the label has the expected
        // text.
        ClickOnPEPCElement("camera-microphone"),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)));

    for (const auto& expectation : expected_labels) {
      if (check_buttons) {
        AddStep(steps, Steps(InAnyContext(CheckViewProperty(
                           expectation.first, &views::MdTextButton::GetText,
                           expectation.second))));
      } else {
        AddStep(steps, Steps(InAnyContext(CheckViewProperty(
                           expectation.first, &views::Label::GetText,
                           expectation.second))));
      }
    }

    AddStep(steps,
            Steps(
                // Dismiss the prompt.
                Do([this]() {
                  auto* manager =
                      permissions::PermissionRequestManager::FromWebContents(
                          browser()->tab_strip_model()->GetActiveWebContents());
                  manager->Dismiss();
                  manager->FinalizeCurrentRequests();
                })));

    RunTestSequence(std::move(steps));
  }

  void TestPartialPermissionsLabel(ContentSetting camera_setting,
                                   ContentSetting mic_setting,
                                   ui::ElementIdentifier id,
                                   const std::u16string expected_label1) {
    std::map<ui::ElementIdentifier, const std::u16string> expected_labels = {
        {id, expected_label1}};
    TestPromptElementText(camera_setting, mic_setting, expected_labels,
                          /*check_buttons=*/false);
  }

  auto DoPromptAndCheckHistograms(const std::string& element_id,
                                  ui::ElementIdentifier prompt_button,
                                  const base::HistogramTester& tester,
                                  permissions::RequestTypeForUma type,
                                  int accepted_count,
                                  int accepted_once_count) {
    return Steps(
        ClickOnPEPCElement(element_id),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
        PushPEPCPromptButton(prompt_button),
        CheckHistogram(
            tester, permissions::PermissionUmaUtil::kPermissionsPromptAccepted,
            type, accepted_count),
        CheckHistogram(
            tester,
            permissions::PermissionUmaUtil::kPermissionsPromptAcceptedOnce,
            type, accepted_once_count),
        CheckHistogram(tester,
                       permissions::PermissionUmaUtil::kPermissionsPromptDenied,
                       type, 0),
        CheckHistogram(
            tester, permissions::PermissionUmaUtil::kPermissionsPromptDismissed,
            type, 0));
  }

  auto ShowTabModalUI() {
    return Do([this]() {
      scoped_tab_modal_ui_ = browser()->GetActiveTabInterface()->ShowModalUI();
    });
  }

  auto HideTabModalUI() {
    return Do([this]() { scoped_tab_modal_ui_.reset(); });
  }

 protected:
  base::test::ScopedFeatureList feature_list_;

 private:
  std::unique_ptr<net::EmbeddedTestServer> https_server_;

  // |ukm_recorder_| needs to be reset after every check so that further check
  // functions will only check the new data.
  std::unique_ptr<ukm::TestAutoSetUkmRecorder> ukm_recorder_;
  std::unique_ptr<tabs::ScopedTabModalUI> scoped_tab_modal_ui_;
};

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       BasicFlowMicrophone) {
  TestAskBlockAllowFlow(
      "microphone", {ContentSettingsType::MEDIASTREAM_MIC},
      std::vector<std::u16string>(
          {u"a.test:" + base::UTF8ToUTF16(GetOrigin().port()) + u" wants to",
           u"You have allowed microphone for this site",
           u"You previously didn't allow microphone for this site"}),
      std::vector<std::u16string>({u"Use your microphones"}));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       BasicFlowCamera) {
  TestAskBlockAllowFlow(
      "camera", {ContentSettingsType::MEDIASTREAM_CAMERA},
      std::vector<std::u16string>(
          {u"a.test:" + base::UTF8ToUTF16(GetOrigin().port()) + u" wants to",
           u"You have allowed camera for this site",
           u"You previously didn't allow camera for this site"}),
      std::vector<std::u16string>({u"Use your cameras"}));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       BasicFlowGeolocation) {
  TestAskBlockAllowFlow(
      "geolocation", {ContentSettingsType::GEOLOCATION},
      std::vector<std::u16string>(
          {u"a.test:" + base::UTF8ToUTF16(GetOrigin().port()) + u" wants to",
           u"You have allowed location for this site",
           u"You previously didn't allow location for this site"}),
      std::vector<std::u16string>({u"Know your location"}));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       BasicFlowCameraMicrophone) {
  TestAskBlockAllowFlow(
      "camera-microphone",
      {ContentSettingsType::MEDIASTREAM_CAMERA,
       ContentSettingsType::MEDIASTREAM_MIC},
      std::vector<std::u16string>(
          {u"a.test:" + base::UTF8ToUTF16(GetOrigin().port()) + u" wants to",
           u"You have allowed camera and microphone for this site",
           u"You previously didn't allow camera and microphone for this site"}),
      std::vector<std::u16string>({u"Use your cameras"}),
      std::vector<std::u16string>({u"Use your microphones"}));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestPartialPermissionsLabels) {
  RunTestSequence(InstrumentTab(kWebContentsElementId),
                  NavigateWebContents(kWebContentsElementId, GetURL()));

  TestPartialPermissionsLabel(CONTENT_SETTING_ALLOW, CONTENT_SETTING_ASK,
                              EmbeddedPermissionPromptBaseView::kLabelViewId1,
                              u"Use your microphones");
  TestPartialPermissionsLabel(CONTENT_SETTING_ASK, CONTENT_SETTING_ALLOW,
                              EmbeddedPermissionPromptBaseView::kLabelViewId1,
                              u"Use your cameras");

  TestPartialPermissionsLabel(
      CONTENT_SETTING_BLOCK, CONTENT_SETTING_ASK,
      EmbeddedPermissionPromptBaseView::kTitleViewId,
      u"You previously didn't allow camera and microphone for this site");
  TestPartialPermissionsLabel(
      CONTENT_SETTING_ASK, CONTENT_SETTING_BLOCK,
      EmbeddedPermissionPromptBaseView::kTitleViewId,
      u"You previously didn't allow camera and microphone for this site");

  TestPartialPermissionsLabel(
      CONTENT_SETTING_BLOCK, CONTENT_SETTING_ALLOW,
      EmbeddedPermissionPromptBaseView::kTitleViewId,
      u"You previously didn't allow camera for this site");
  TestPartialPermissionsLabel(
      CONTENT_SETTING_ALLOW, CONTENT_SETTING_BLOCK,
      EmbeddedPermissionPromptBaseView::kTitleViewId,
      u"You previously didn't allow microphone for this site");
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestButtonsLabel) {
  RunTestSequence(InstrumentTab(kWebContentsElementId),
                  NavigateWebContents(kWebContentsElementId, GetURL()));

  std::map<ui::ElementIdentifier, const std::u16string> expected_ask_labels = {
      {EmbeddedPermissionPromptAskView::kAllowId,
       u"Allow while visiting the site"},
      {EmbeddedPermissionPromptAskView::kAllowThisTimeId, u"Allow this time"}};

  TestPromptElementText(CONTENT_SETTING_ASK, CONTENT_SETTING_ASK,
                        expected_ask_labels, /*check_buttons=*/true);
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestOsPromptButtonsLabel) {
  base::AutoReset<bool> mock_system_settings =
      system_permission_settings::MockShowSystemSettingsForTesting();

  std::u16string open_settings_label;

#if BUILDFLAG(IS_MAC)
  open_settings_label = u"MacOS settings";
#elif BUILDFLAG(IS_WIN)
  open_settings_label = u"Windows settings";
#elif BUILDFLAG(IS_CHROMEOS)
  open_settings_label = u"ChromeOS settings";
#endif

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      ClickOnPEPCElement("camera"),
      InAnyContext(
          WaitForShow(EmbeddedPermissionPromptSystemSettingsView::kMainViewId)),
      InAnyContext(CheckViewProperty(
          EmbeddedPermissionPromptSystemSettingsView::kOpenSettingsId,
          &views::MdTextButton::GetText, open_settings_label)));
#endif
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestPepcHistograms) {
  base::HistogramTester tester;
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),

      // Initially the "ask" view is displayed.
      DoPromptAndCheckHistograms(
          "camera", EmbeddedPermissionPromptAskView::kAllowId, tester,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          /*accepted_count=*/1, /*accepted_once_count=*/0),

      // Now the "allow" view is displayed. Neither clicking "continue allowing"
      // or "stop allowing" records any additional histograms.
      DoPromptAndCheckHistograms(
          "camera",
          EmbeddedPermissionPromptPreviouslyGrantedView::kContinueAllowingId,
          tester, permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          /*accepted_count=*/1, /*accepted_once_count=*/0),

      DoPromptAndCheckHistograms(
          "camera",
          EmbeddedPermissionPromptPreviouslyGrantedView::kStopAllowingId,
          tester, permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          /*accepted_count=*/1, /*accepted_once_count=*/0),

      // Other permissions are not affected, check that the microphone
      // permission has no histograms.
      CheckHistogram(tester,
                     permissions::PermissionUmaUtil::kPermissionsPromptAccepted,
                     permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
                     /*count=*/0),
      CheckHistogram(
          tester,
          permissions::PermissionUmaUtil::kPermissionsPromptAcceptedOnce,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
          /*count=*/0),

      // Trigger and check a microphone "ask" prompt with allow-once.
      DoPromptAndCheckHistograms(
          "microphone", EmbeddedPermissionPromptAskView::kAllowThisTimeId,
          tester, permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
          /*accepted_count=*/0,
          /*accepted_once_count=*/1),

      // Showing a combined prompt at this point will result in a "previously
      // blocked" screen which won't record new histograms.
      DoPromptAndCheckHistograms(
          "camera-microphone",
          EmbeddedPermissionPromptPreviouslyDeniedView::kAllowThisTimeId,
          tester,
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          /*accepted_count=*/0,
          /*accepted_once_count=*/0),
      CheckHistogram(
          tester,
          permissions::PermissionUmaUtil::kPermissionsPromptAcceptedOnce,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          /*count=*/0),
      CheckHistogram(
          tester,
          permissions::PermissionUmaUtil::kPermissionsPromptAcceptedOnce,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
          /*count=*/1),

      // Reset permissions and show the combined prompt, now in "ask" mode.
      // First check the allow action, then the allow-once action.
      Do([&, this]() {
        SetContentSetting(ContentSettingsType::MEDIASTREAM_CAMERA,
                          CONTENT_SETTING_DEFAULT);
        SetContentSetting(ContentSettingsType::MEDIASTREAM_MIC,
                          CONTENT_SETTING_DEFAULT);
      }),

      DoPromptAndCheckHistograms(
          "camera-microphone", EmbeddedPermissionPromptAskView::kAllowId,
          tester,
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          /*accepted_count=*/1,
          /*accepted_once_count=*/0),

      Do([&, this]() {
        SetContentSetting(ContentSettingsType::MEDIASTREAM_CAMERA,
                          CONTENT_SETTING_DEFAULT);
        SetContentSetting(ContentSettingsType::MEDIASTREAM_MIC,
                          CONTENT_SETTING_DEFAULT);
      }),

      DoPromptAndCheckHistograms(
          "camera-microphone",
          EmbeddedPermissionPromptAskView::kAllowThisTimeId, tester,
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          /*accepted_count=*/1,
          /*accepted_once_count=*/1),

      // Check that all other histograms are unmodified.
      CheckHistogram(
          tester, permissions::PermissionUmaUtil::kPermissionsPromptAccepted,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          /*count=*/1),
      CheckHistogram(
          tester,
          permissions::PermissionUmaUtil::kPermissionsPromptAcceptedOnce,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          /*count=*/0),
      CheckHistogram(tester,
                     permissions::PermissionUmaUtil::kPermissionsPromptAccepted,
                     permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
                     /*count=*/0),
      CheckHistogram(
          tester,
          permissions::PermissionUmaUtil::kPermissionsPromptAcceptedOnce,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
          /*count=*/1));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       FocusableViaTabKey) {
  StateChange pepc_visible;
  pepc_visible.where = DeepQuery{"#geolocation"};
  pepc_visible.type = StateChange::Type::kExists;
  pepc_visible.event = kPEPCVisibleEvent;

  StateChange done_visible;
  done_visible.where = DeepQuery{"#done"};
  done_visible.type = StateChange::Type::kExists;
  done_visible.event = kDoneVisibleEvent;

  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      // Setup an event listener for focus changes. When all expected element
      // ids are matched. A 'done' element is appended to the body.
      ExecuteJs(kWebContentsElementId,
                R"JS(
        () => {
          var expected_focused_ids = [
            "geolocation",
            "microphone",
            "camera",
            "camera-microphone"
          ];

          document.addEventListener('focus', (event) => {
            if (event.target.id === expected_focused_ids[0]) {
              expected_focused_ids.shift();
              if (expected_focused_ids.length == 0) {
                const newElement = document.createElement('div');
                newElement.id = 'done';
                document.body.appendChild(newElement);
              }
            }
          }, true);
        })JS"),
      WaitForStateChange(kWebContentsElementId, pepc_visible), Do([this]() {
        // The exact number of "tab" presses needed to pass through all elements
        // differs by platform. Here we do it 10 times to be sure.
        for (int i = 0; i < 10; i++) {
          ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
              browser(), ui::VKEY_TAB, false, false, false, false));
        }
      }),
      // Make sure all elements have been focused as expected.
      WaitForStateChange(kWebContentsElementId, done_visible));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest, TestPepcUkm) {
  views::NamedWidgetShownWaiter waiter(
      views::test::AnyWidgetTestPasskey{},
      "EmbeddedPermissionPromptContentScrimWidget");
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      ClickOnPEPCElement("camera-microphone"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      CheckNoUkmEntriesSinceLastCheck(),
      PushPEPCPromptButton(EmbeddedPermissionPromptAskView::kAllowId),
      CheckEntrySinceLastCheck(
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          permissions::ElementAnchoredBubbleAction::kGranted,
          permissions::ElementAnchoredBubbleVariant::kAsk, 0),

      // Now mic+camera are granted.
      ClickOnPEPCElement("camera"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      PushPEPCPromptButton(
          EmbeddedPermissionPromptPreviouslyGrantedView::kStopAllowingId),
      CheckEntrySinceLastCheck(
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          permissions::ElementAnchoredBubbleAction::kDenied,
          permissions::ElementAnchoredBubbleVariant::kPreviouslyGranted, 0),

      ClickOnPEPCElement("microphone"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      PushPEPCPromptButton(
          EmbeddedPermissionPromptPreviouslyGrantedView::kContinueAllowingId),
      CheckEntrySinceLastCheck(
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_MIC,
          permissions::ElementAnchoredBubbleAction::kOk,
          permissions::ElementAnchoredBubbleVariant::kPreviouslyGranted, 0),

      // Mic is granted, camera is blocked. Triggering the double permission
      // prompt will show the screen that is only for camera, while the prompt
      // is for both.
      ClickOnPEPCElement("camera-microphone"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      PushPEPCPromptButton(
          EmbeddedPermissionPromptPreviouslyDeniedView::kAllowThisTimeId),
      CheckEntrySinceLastCheck(
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          permissions::RequestTypeForUma::PERMISSION_MEDIASTREAM_CAMERA,
          permissions::ElementAnchoredBubbleAction::kGrantedOnce,
          permissions::ElementAnchoredBubbleVariant::kPreviouslyDenied, 0),

      // Both permissions are granted. Dismiss the prompt via clicking on the
      // scrim.
      ClickOnPEPCElement("camera-microphone"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      Do([&]() {
        auto* scrim_view =
            static_cast<EmbeddedPermissionPromptContentScrimView*>(
                waiter.WaitIfNeededAndGet()->GetContentsView());
        scrim_view->OnMousePressed(ui::MouseEvent(
            ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
            ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
      }),
      CheckEntrySinceLastCheck(
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          permissions::RequestTypeForUma::MULTIPLE_AUDIO_AND_VIDEO_CAPTURE,
          permissions::ElementAnchoredBubbleAction::kDismissedScrim,
          permissions::ElementAnchoredBubbleVariant::kPreviouslyGranted, 0));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestOsSystemPromptTransition) {
  base::AutoReset<bool> mock_system_prompt =
      system_permission_settings::MockSystemPromptForTesting();

  views::NamedWidgetShownWaiter original_waiter(
      views::test::AnyWidgetTestPasskey{},
      "EmbeddedPermissionPromptContentScrimWidget");
  views::NamedWidgetShownWaiter waiter(
      views::test::AnyWidgetTestPasskey{},
      "EmbeddedPermissionPromptContentScrimWidget");

  EmbeddedPermissionPromptContentScrimView* original_scrim_view = nullptr;
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      ClickOnPEPCElement("camera"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      Do([&]() {
        // Save the reference to the scrim.
        original_scrim_view =
            static_cast<EmbeddedPermissionPromptContentScrimView*>(
                original_waiter.WaitIfNeededAndGet()->GetContentsView());
      }),
      PushPEPCPromptButton(EmbeddedPermissionPromptAskView::kAllowId),
      InAnyContext(WaitForShow(
          EmbeddedPermissionPromptShowSystemPromptView::kMainViewId)),
      Do([&]() {
        auto* scrim_view =
            static_cast<EmbeddedPermissionPromptContentScrimView*>(
                waiter.WaitIfNeededAndGet()->GetContentsView());
        // Verify that the scrim view is the same one that was opened at the
        // beginning, and wasn't closed and reopened during the transition.
        EXPECT_EQ(scrim_view, original_scrim_view);
        scrim_view->OnMousePressed(ui::MouseEvent(
            ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
            ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
      }));
}

// Linux wayland does not support window activation.
#if (BUILDFLAG(IS_LINUX) && BUILDFLAG(IS_OZONE_WAYLAND))
#define MAYBE_TestOsSystemAutoResolves DISABLED_TestOsSystemAutoResolves
#else
#define MAYBE_TestOsSystemAutoResolves TestOsSystemAutoResolves
#endif
IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       MAYBE_TestOsSystemAutoResolves) {
  std::unique_ptr<system_permission_settings::ScopedSettingsForTesting>
      scoped_system_permission_camera = std::make_unique<
          system_permission_settings::ScopedSettingsForTesting>(
          ContentSettingsType::MEDIASTREAM_CAMERA, /*blocked=*/true);
  std::unique_ptr<system_permission_settings::ScopedSettingsForTesting>
      scoped_system_permission_mic = std::make_unique<
          system_permission_settings::ScopedSettingsForTesting>(
          ContentSettingsType::MEDIASTREAM_MIC, /*blocked=*/true);

  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      ClickOnPEPCElement("camera-microphone"),
      InAnyContext(
          WaitForShow(EmbeddedPermissionPromptSystemSettingsView::kMainViewId)),
      Do([&]() {
        scoped_system_permission_camera.reset();
        scoped_system_permission_mic.reset();
        scoped_system_permission_camera = std::make_unique<
            system_permission_settings::ScopedSettingsForTesting>(
            ContentSettingsType::MEDIASTREAM_CAMERA, /*blocked=*/false);
        scoped_system_permission_mic = std::make_unique<
            system_permission_settings::ScopedSettingsForTesting>(
            ContentSettingsType::MEDIASTREAM_MIC, /*blocked=*/false);

        // Simulate another window becoming active, and then the current window
        // again.
        Browser* focused_window = CreateBrowser(browser()->profile());
        ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(focused_window));
        ASSERT_FALSE(browser()->window()->IsActive());

        ui_test_utils::BrowserActivationWaiter waiter(browser());
        browser()->window()->Activate();
        waiter.WaitForActivation();
      }),

      // Now that both system permissions changed to allowed, the PEPC prompt
      // advances to the next screen.
      InAnyContext(WaitForShow(EmbeddedPermissionPromptAskView::kAllowId)));
}

// This test relies on the presence of the "Go to [OS] setting" button which is
// not implemented for the linux version of the prompt.
#if BUILDFLAG(IS_LINUX)
#define MAYBE_TestOsSystemAutoResolvesOnButton \
  DISABLED_TestOsSystemAutoResolvesOnButton
#else
#define MAYBE_TestOsSystemAutoResolvesOnButton TestOsSystemAutoResolvesOnButton
#endif
IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       MAYBE_TestOsSystemAutoResolvesOnButton) {
  std::unique_ptr<system_permission_settings::ScopedSettingsForTesting>
      scoped_system_permission_camera = std::make_unique<
          system_permission_settings::ScopedSettingsForTesting>(
          ContentSettingsType::MEDIASTREAM_CAMERA, /*blocked=*/true);
  std::unique_ptr<system_permission_settings::ScopedSettingsForTesting>
      scoped_system_permission_mic = std::make_unique<
          system_permission_settings::ScopedSettingsForTesting>(
          ContentSettingsType::MEDIASTREAM_MIC, /*blocked=*/true);

  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      ClickOnPEPCElement("camera-microphone"),
      InAnyContext(
          WaitForShow(EmbeddedPermissionPromptSystemSettingsView::kMainViewId)),
      Do([&]() {
        scoped_system_permission_camera.reset();
        scoped_system_permission_mic.reset();
        scoped_system_permission_camera = std::make_unique<
            system_permission_settings::ScopedSettingsForTesting>(
            ContentSettingsType::MEDIASTREAM_CAMERA, /*blocked=*/false);
        scoped_system_permission_mic = std::make_unique<
            system_permission_settings::ScopedSettingsForTesting>(
            ContentSettingsType::MEDIASTREAM_MIC, /*blocked=*/false);
      }),

      PushPEPCPromptButton(
          EmbeddedPermissionPromptSystemSettingsView::kOpenSettingsId,
          /*wait_for_prompt_resolution=*/false),

      // Now that both system permissions changed to allowed, clicking the "open
      // settings" button means the prompt progresses to the next screen.
      InAnyContext(WaitForShow(EmbeddedPermissionPromptAskView::kAllowId)));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       CrossOriginZoomAffectsValidation) {
  StateChange done_visible;
  done_visible.where = DeepQuery{"#done"};
  done_visible.type = StateChange::Type::kExists;
  done_visible.event = kDoneVisibleEvent;

  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(
          kWebContentsElementId,
          https_server()->GetURL(
              "b.test", "/permissions/permission_element_embedder.html")));
  content::WebContentsConsoleObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents());

  RunTestSequence(
      ExecuteJs(kWebContentsElementId,
                content::JsReplace("() => { insertIframe($1, $2); }", GetURL(),
                                   "zoom5")));
  // Wait until getting the message that the permission element's font is
  // too large.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    for (const auto& message : observer.messages()) {
      if (message.message ==
          u"Font size of the permission element 'camera' is too large") {
        return true;
      }
    }

    return false;
  }));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       BrowserZoomDoesNotAffectValidation) {
  zoom::ZoomController* zoom_controller = zoom::ZoomController::FromWebContents(
      browser()->tab_strip_model()->GetActiveWebContents());
  zoom_controller->SetZoomLevel(10);
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()),
      ClickOnPEPCElement("camera-microphone"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)));
}

// Checks that the prompt is not shown if the tab is displaying another modal
// UI and that it is shown when the other modal UI is closed.
IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestSamePromptInteractionsWithModalUILock) {
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()), ShowTabModalUI(),
      ClickOnPEPCElement("camera"), HideTabModalUI(),
      ClickOnPEPCElement("camera"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      InAnyContext(
          CheckViewProperty(EmbeddedPermissionPromptBaseView::kLabelViewId1,
                            &views::Label::GetText, u"Use your cameras")),
      Do([&]() {
        auto* manager = permissions::PermissionRequestManager::FromWebContents(
            browser()->tab_strip_model()->GetActiveWebContents());
        ASSERT_FALSE(manager->has_pending_requests());

        // Need to close the permission prompt before the test shuts down.
        manager->Dismiss();
        manager->FinalizeCurrentRequests();
      }));
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptInteractiveTest,
                       TestDifferentPromptInteractionsWithModalUILock) {
  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(kWebContentsElementId, GetURL()), ShowTabModalUI(),
      ClickOnPEPCElement("camera"), HideTabModalUI(),
      ClickOnPEPCElement("geolocation"),
      InAnyContext(WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
      InAnyContext(
          CheckViewProperty(EmbeddedPermissionPromptBaseView::kLabelViewId1,
                            &views::Label::GetText, u"Know your location")),
      Do([&]() {
        auto* manager = permissions::PermissionRequestManager::FromWebContents(
            browser()->tab_strip_model()->GetActiveWebContents());
        ASSERT_FALSE(manager->has_pending_requests());

        // Need to close the permission prompt before the test shuts down.
        manager->Dismiss();
        manager->FinalizeCurrentRequests();
      }));
}

class EmbeddedPermissionPromptPositioningInteractiveTest
    : public EmbeddedPermissionPromptInteractiveTest {
 public:
  EmbeddedPermissionPromptPositioningInteractiveTest() {
    feature_list_.Reset();
    feature_list_.InitWithFeaturesAndParameters(
        {
            {blink::features::kPermissionElement, {}},
            {permissions::features::kPermissionElementPromptPositioning,
             {{"PermissionElementPromptPositioningParam", "near_element"}}},
            {blink::features::kBypassPepcSecurityForTesting, {}},
        },
        {});
  }
};

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptPositioningInteractiveTest,
                       TestPermissionElementDialogPositioning) {
  RunTestSequence(InstrumentTab(kWebContentsElementId),
                  NavigateWebContents(kWebContentsElementId, GetURL()),
                  // Set the font size to 'small' to ensure all elements have
                  // enough room in a line as this test depends on it.
                  ExecuteJs(kWebContentsElementId, "setFontSizeSmall"));

  // Click on multiple elements in order from left to right, and ensure that
  // dialog moves with each click
  gfx::Point current_origin;
  struct ElementAction {
    std::string element_name;
    ui::ElementIdentifier button_identifier;
  };
  std::vector<ElementAction> element_actions = {
      {"microphone", EmbeddedPermissionPromptAskView::kAllowId},
      {"camera", EmbeddedPermissionPromptAskView::kAllowId},
      {"camera-microphone",
       EmbeddedPermissionPromptPreviouslyGrantedView::kStopAllowingId},
  };

  for (const auto& element_action : element_actions) {
    RunTestSequence(
        ClickOnPEPCElement(element_action.element_name),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),

        InAnyContext(CheckView(EmbeddedPermissionPromptBaseView::kMainViewId,
                               [&current_origin](views::View* view) {
                                 gfx::Point previous_origin = current_origin;
                                 current_origin =
                                     view->GetBoundsInScreen().origin();
                                 return previous_origin < current_origin;
                               })),
        PushPEPCPromptButton(element_action.button_identifier));
  }
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptPositioningInteractiveTest,
                       TestPositionUsingZoom) {
  auto* widget = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  if (widget->GetWindowBoundsInScreen().height() < kMinWindowHeight) {
    // Skip the test if the actual window size is too small to fit the prompt
    // even after forcing the window size in the test suite.
    return;
  }

  RunTestSequence(InstrumentTab(kWebContentsElementId),
                  NavigateWebContents(kWebContentsElementId, GetURL()),
                  ExecuteJs(kWebContentsElementId, "setFontSizeSmall"));

  double zoom_level = 0;
  int previous_x = 0;

  int loops = 5;
  while (loops--) {
    RunTestSequence(
        ClickOnPEPCElement("microphone"),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),

        InAnyContext(CheckView(
            EmbeddedPermissionPromptBaseView::kMainViewId,
            [&previous_x](views::View* view) {
              gfx::Rect bounds = view->GetBoundsInScreen();
              previous_x = bounds.x();
              return bounds.x();
            },
            testing::Gt(previous_x))),

        Do([this, &zoom_level]() {
          auto* manager =
              permissions::PermissionRequestManager::FromWebContents(
                  browser()->tab_strip_model()->GetActiveWebContents());
          manager->Dismiss();
          manager->FinalizeCurrentRequests();

          zoom::ZoomController* zoom_controller =
              zoom::ZoomController::FromWebContents(
                  browser()->tab_strip_model()->GetActiveWebContents());
          zoom_level += 0.2;
          zoom_controller->SetZoomLevel(zoom_level);
        }));
  }

  zoom::ZoomController* zoom_controller = zoom::ZoomController::FromWebContents(
      browser()->tab_strip_model()->GetActiveWebContents());
  zoom_controller->SetZoomLevel(zoom_controller->GetDefaultZoomLevel());
}

IN_PROC_BROWSER_TEST_P(EmbeddedPermissionPromptPositioningInteractiveTest,
                       TestPositionInsideCrossOriginFrame) {
  auto* widget = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  if (widget->GetWindowBoundsInScreen().height() < kMinWindowHeight) {
    // Skip the test if the actual window size is too small to fit the prompt
    // even after forcing the window size in the test suite.
    return;
  }

  StateChange done_visible;
  done_visible.where = DeepQuery{"#done"};
  done_visible.type = StateChange::Type::kExists;
  done_visible.event = kDoneVisibleEvent;

  RunTestSequence(
      InstrumentTab(kWebContentsElementId),
      NavigateWebContents(
          kWebContentsElementId,
          https_server()->GetURL(
              "b.test", "/permissions/permission_element_embedder.html")),
      ExecuteJs(kWebContentsElementId,
                content::JsReplace("() => { insertIframe($1); }", GetURL())),
      WaitForStateChange(kWebContentsElementId, done_visible));

  int loops = 5;
  int previous_y = 0;
  while (loops--) {
    RunTestSequence(
        ExecuteJs(
            kWebContentsElementId,
            content::JsReplace("() => { clickInIframe($1); }", "microphone")),
        InAnyContext(
            WaitForShow(EmbeddedPermissionPromptBaseView::kMainViewId)),
        InAnyContext(CheckView(
            EmbeddedPermissionPromptBaseView::kMainViewId,
            [&previous_y](views::View* view) {
              gfx::Rect bounds = view->GetBoundsInScreen();
              previous_y = bounds.y();
              return bounds.y();
            },
            testing::Gt(previous_y))),
        ExecuteJs(kWebContentsElementId, "expandDiv"), Do([this]() {
          auto* manager =
              permissions::PermissionRequestManager::FromWebContents(
                  browser()->tab_strip_model()->GetActiveWebContents());
          manager->Dismiss();
          manager->FinalizeCurrentRequests();
        }));
  }
}

// Setting up to run all tests with two screen scale factors.
INSTANTIATE_TEST_SUITE_P(,
                         EmbeddedPermissionPromptInteractiveTest,
                         testing::Values(1.0, 2.0));
INSTANTIATE_TEST_SUITE_P(,
                         EmbeddedPermissionPromptPositioningInteractiveTest,
                         testing::Values(1.0, 2.0));
