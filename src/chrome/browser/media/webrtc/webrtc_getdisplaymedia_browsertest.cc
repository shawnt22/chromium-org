// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/to_string.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/config/chromebox_for_meetings/buildflags.h"  // PLATFORM_CFM
#include "chrome/browser/apps/platform_apps/app_browsertest_util.h"
#include "chrome/browser/media/webrtc/webrtc_browsertest_base.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tab_sharing/tab_sharing_infobar_delegate.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/infobar.h"
#include "components/permissions/permission_request_manager.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/elide_url.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/mock_captured_surface_controller.h"
#include "media/base/media_switches.h"
#include "net/base/filename_util.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#include "chrome/browser/media/webrtc/system_media_capture_permissions_mac.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/chromeos/policy/dlp/dlp_content_restriction_set.h"
#include "chrome/browser/chromeos/policy/dlp/test/dlp_content_manager_test_helper.h"
#endif

namespace {

using ::base::test::FeatureRef;
using ::content::BrowserThread;
using ::content::CapturedSurfaceController;
using ::content::GlobalRenderFrameHostId;
using ::content::MockCapturedSurfaceController;
using ::content::WebContents;
using ::content::WebContentsMediaCaptureId;
using ::testing::_;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::HasSubstr;
using ::testing::Mock;
using ::testing::Values;
using TabSharingInfoBarButton =
    ::TabSharingInfoBarDelegate::TabSharingInfoBarButton;

using CapturedSurfaceControllerFactoryCallback =
    ::base::RepeatingCallback<std::unique_ptr<MockCapturedSurfaceController>(
        GlobalRenderFrameHostId,
        WebContentsMediaCaptureId)>;

static const char kMainHtmlPage[] = "/webrtc/webrtc_getdisplaymedia_test.html";
static const char kMainHtmlFileName[] = "webrtc_getdisplaymedia_test.html";
static const char kSameOriginRenamedTitle[] = "Renamed Same Origin Tab";
static const char kMainHtmlTitle[] = "WebRTC Automated Test";
// The captured tab is identified by its title.
static const char kCapturedTabTitle[] = "totally-unique-captured-page-title";
static const char kCapturedPageMain[] = "/webrtc/captured_page_main.html";
static const std::u16string kShareThisTabInsteadMessage =
    u"Share this tab instead";

constexpr TabSharingInfoBarButton kCscIndicator =
    TabSharingInfoBarButton::kCapturedSurfaceControlIndicator;

enum class DisplaySurfaceType { kTab, kWindow, kScreen };

enum class GetDisplayMediaVariant : int {
  kStandard = 0,
  kPreferCurrentTab = 1
};

struct TestConfigForPicker {
  TestConfigForPicker(bool should_prefer_current_tab,
                      bool accept_this_tab_capture)
      : should_prefer_current_tab(should_prefer_current_tab),
        accept_this_tab_capture(accept_this_tab_capture) {}

  explicit TestConfigForPicker(std::tuple<bool, bool> input_tuple)
      : TestConfigForPicker(std::get<0>(input_tuple),
                            std::get<1>(input_tuple)) {}

  // If true, specify {preferCurrentTab: true}.
  // Otherwise, either don't specify it, or set it to false.
  bool should_prefer_current_tab;

  // |accept_this_tab_capture| is only applicable if
  // |should_prefer_current_tab| is set to true. Then, setting
  // |accept_this_tab_capture| to true accepts the current tab, and
  // |accept_this_tab_capture| set to false implies dismissing the media picker.
  bool accept_this_tab_capture;
};

struct TestConfigForFakeUI {
  bool should_prefer_current_tab;
  const char* display_surface;
};

struct TestConfigForMediaResolution {
  int constraint_width;
  int constraint_height;
};

constexpr char kAppWindowTitle[] = "AppWindow Display Capture Test";

constexpr char kEmbeddedTestServerOrigin[] = "http://127.0.0.1";
constexpr char kOtherOrigin[] = "https://other-origin.com";

std::string DisplaySurfaceTypeAsString(
    DisplaySurfaceType display_surface_type) {
  switch (display_surface_type) {
    case DisplaySurfaceType::kTab:
      return "browser";
    case DisplaySurfaceType::kWindow:
      return "window";
    case DisplaySurfaceType::kScreen:
      return "screen";
  }
  NOTREACHED();
}

void RunGetDisplayMedia(content::WebContents* tab,
                        const std::string& constraints,
                        bool is_fake_ui,
                        bool expect_success,
                        bool is_tab_capture,
                        const std::string& expected_error = "",
                        bool with_user_gesture = true) {
  DCHECK(!expect_success || expected_error.empty());

  const content::ToRenderFrameHost& adapter = tab->GetPrimaryMainFrame();
  const std::string script = base::StringPrintf(
      "runGetDisplayMedia(%s, \"top-level-document\", \"%s\");",
      constraints.c_str(), expected_error.c_str());
  std::string result =
      content::EvalJs(adapter, script,
                      with_user_gesture
                          ? content::EXECUTE_SCRIPT_DEFAULT_OPTIONS
                          : content::EXECUTE_SCRIPT_NO_USER_GESTURE)
          .ExtractString();

#if BUILDFLAG(IS_MAC)
  if (!is_fake_ui && !is_tab_capture &&
      system_media_permissions::CheckSystemScreenCapturePermission() !=
          system_permission_settings::SystemPermission::kAllowed) {
    expect_success = false;
  }
#endif

  EXPECT_EQ(result, expect_success           ? "capture-success"
                    : expected_error.empty() ? "capture-failure"
                                             : "expected-error");
}

void RunGetUserMedia(content::WebContents* tab,
                     const std::string& constraints) {
  const std::string script =
      base::StrCat({"runGetUserMedia(", constraints, ");"});
  std::string result = content::EvalJs(tab->GetPrimaryMainFrame(), script,
                                       content::EXECUTE_SCRIPT_NO_USER_GESTURE)
                           .ExtractString();
  EXPECT_EQ(result, "gum-success");
}

void StopAllTracks(content::WebContents* tab) {
  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(), "stopAllTracks();"),
            "stopped");
}

void UpdateWebContentsTitle(content::WebContents* contents,
                            const std::u16string& title) {
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(entry);
  contents->UpdateTitleForEntry(entry, title);
}

GURL GetFileURL(const char* filename) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath path;
  base::PathService::Get(chrome::DIR_TEST_DATA, &path);
  path = path.AppendASCII("webrtc").AppendASCII(filename);
  CHECK(base::PathExists(path));
  return net::FilePathToFileURL(path);
}

infobars::ContentInfoBarManager* GetInfoBarManager(
    content::WebContents* web_contents) {
  return infobars::ContentInfoBarManager::FromWebContents(web_contents);
}

TabSharingInfoBarDelegate* GetDelegate(content::WebContents* web_contents,
                                       size_t infobar_index = 0) {
  return static_cast<TabSharingInfoBarDelegate*>(
      GetInfoBarManager(web_contents)->infobars()[infobar_index]->delegate());
}

bool HasCscIndicator(content::WebContents* web_contents) {
  return GetDelegate(web_contents)->GetButtons() & kCscIndicator;
}

bool HasShareThisTabInsteadButton(content::WebContents* web_contents) {
  return GetDelegate(web_contents)->GetButtons() &
         TabSharingInfoBarButton::kShareThisTabInstead;
}

std::u16string GetShareThisTabInsteadButtonLabel(
    content::WebContents* web_contents) {
  DCHECK(HasShareThisTabInsteadButton(web_contents));  // Test error otherwise.
  return GetDelegate(web_contents)
      ->GetButtonLabel(TabSharingInfoBarButton::kShareThisTabInstead);
}

void AdjustCommandLineForZeroCopyCapture(base::CommandLine* command_line) {
  CHECK(command_line);

  // MSan and GL do not get along so avoid using the GPU with MSan.
  // TODO(crbug.com/40260482): Remove this after fixing feature
  // detection in 0c tab capture path as it'll no longer be needed.
#if !BUILDFLAG(IS_CHROMEOS) && !defined(MEMORY_SANITIZER)
  command_line->AppendSwitch(switches::kUseGpuInTests);
#endif
}

// The concept of "zoom level" is overloaded. For clarity, when we mean the
// "factor times 100," we'll just name it "zoom level percentage," at least
// in tests.
int GetZoomLevelPercentage(WebContents* wc) {
  return std::round(100 * blink::ZoomLevelToZoomFactor(
                              content::HostZoomMap::GetZoomLevel(wc)));
}

void SetZoomFactor(WebContents* wc, double zoom_factor) {
  content::HostZoomMap* const host_zoom_map =
      content::HostZoomMap::GetForWebContents(wc);
  CHECK(host_zoom_map);

  host_zoom_map->SetTemporaryZoomLevel(
      wc->GetPrimaryMainFrame()->GetGlobalId(),
      blink::ZoomFactorToZoomLevel(zoom_factor));

  if (!blink::ZoomValuesEqual(GetZoomLevelPercentage(wc), 100 * zoom_factor)) {
    FAIL();  // Abort test, not just the helper method.
  }
}

}  // namespace

// Base class for top level tests for getDisplayMedia().
class WebRtcScreenCaptureBrowserTest : public WebRtcTestBase {
 public:
  ~WebRtcScreenCaptureBrowserTest() override = default;

  void SetUpInProcessBrowserTestFixture() override {
    DetectErrorsInJavaScript();
  }

  virtual bool PreferCurrentTab() const = 0;

  std::string GetConstraints(bool video,
                             bool audio,
                             bool prefer_current_tab) const {
    return base::StringPrintf("{video: %s, audio: %s, preferCurrentTab: %s}",
                              base::ToString(video), base::ToString(audio),
                              base::ToString(prefer_current_tab));
  }

  std::string GetConstraints(bool video,
                             bool audio,
                             GetDisplayMediaVariant variant) const {
    return GetConstraints(video, audio,
                          variant == GetDisplayMediaVariant::kPreferCurrentTab);
  }

  std::string GetConstraints(bool video, bool audio) const {
    return GetConstraints(video, audio, PreferCurrentTab());
  }
};

// Top level test for getDisplayMedia().
// Pops picker UI and shares by default.
class WebRtcScreenCaptureBrowserTestWithPicker
    : public WebRtcScreenCaptureBrowserTest,
      public testing::WithParamInterface<std::tuple<bool, bool>> {
 public:
  WebRtcScreenCaptureBrowserTestWithPicker() : test_config_(GetParam()) {}

  void SetUpOnMainThread() override {
    WebRtcScreenCaptureBrowserTest::SetUpOnMainThread();
#if BUILDFLAG(PLATFORM_CFM)
    if (test_config_.should_prefer_current_tab &&
        !test_config_.accept_this_tab_capture) {
      GTEST_SKIP();  // CFMs always automatically accept current-tab captures.
    }
#endif
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    if (test_config_.should_prefer_current_tab) {
      command_line->AppendSwitch(test_config_.accept_this_tab_capture
                                     ? switches::kThisTabCaptureAutoAccept
                                     : switches::kThisTabCaptureAutoReject);
    } else {
#if BUILDFLAG(IS_CHROMEOS)
      command_line->AppendSwitchASCII(switches::kAutoSelectDesktopCaptureSource,
                                      "Display");
#else
      command_line->AppendSwitchASCII(switches::kAutoSelectDesktopCaptureSource,
                                      "Entire screen");
#endif  // BUILDFLAG(IS_CHROMEOS)
    }
  }

  bool PreferCurrentTab() const override {
    return test_config_.should_prefer_current_tab;
  }

  const TestConfigForPicker test_config_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         WebRtcScreenCaptureBrowserTestWithPicker,
                         Combine(
                             /*should_prefer_current_tab=*/Bool(),
                             /*accept_this_tab_capture=*/Bool()));

// TODO(crbug.com/40744542): Real desktop capture is flaky on below platforms.
// TODO(crbug.com/41493366): enable this flaky test.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
#define MAYBE_ScreenCaptureVideo DISABLED_ScreenCaptureVideo
#else
#define MAYBE_ScreenCaptureVideo ScreenCaptureVideo
#endif  // BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestWithPicker,
                       MAYBE_ScreenCaptureVideo) {
  if (!test_config_.should_prefer_current_tab &&
      !test_config_.accept_this_tab_capture) {
    GTEST_SKIP();
  }

  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);
  RunGetDisplayMedia(tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/false),
                     /*is_fake_ui=*/false,
                     /*expect_success=*/test_config_.accept_this_tab_capture,
                     /*is_tab_capture=*/PreferCurrentTab());
}

#if BUILDFLAG(IS_CHROMEOS)
IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestWithPicker,
                       ScreenCaptureVideoWithDlp) {
  if (!test_config_.should_prefer_current_tab &&
      !test_config_.accept_this_tab_capture) {
    GTEST_SKIP();
  }

  ASSERT_TRUE(embedded_test_server()->Start());

  policy::DlpContentManagerTestHelper helper;
  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);
  RunGetDisplayMedia(tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/false),
                     /*is_fake_ui=*/false,
                     /*expect_success=*/test_config_.accept_this_tab_capture,
                     /*is_tab_capture=*/PreferCurrentTab());

  if (!test_config_.accept_this_tab_capture) {
    // This test is not relevant for this parameterized test case because it
    // does not capture the tab/display surface.
    return;
  }

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(), "waitVideoUnmuted();"),
            "unmuted");

  const policy::DlpContentRestrictionSet kScreenShareRestricted(
      policy::DlpContentRestriction::kScreenShare,
      policy::DlpRulesManager::Level::kBlock);

  helper.ChangeConfidentiality(tab, kScreenShareRestricted);
  content::WaitForLoadStop(tab);

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(), "waitVideoMuted();"),
            "muted");

  const policy::DlpContentRestrictionSet kEmptyRestrictionSet;
  helper.ChangeConfidentiality(tab, kEmptyRestrictionSet);

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(), "waitVideoUnmuted();"),
            "unmuted");
}
#endif  // BUILDFLAG(IS_CHROMEOS)

// TODO(crbug.com/40744542): Real desktop capture is flaky on below platforms.
// TODO(crbug.com/41493366): enable this flaky test.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
#define MAYBE_ScreenCaptureVideoAndAudio DISABLED_ScreenCaptureVideoAndAudio
// On linux debug bots, it's flaky as well.
#elif BUILDFLAG(IS_LINUX) && !defined(NDEBUG)
#define MAYBE_ScreenCaptureVideoAndAudio DISABLED_ScreenCaptureVideoAndAudio
// On linux asan bots, it's flaky as well - msan and other rel bot are fine.
#elif BUILDFLAG(IS_LINUX) && defined(ADDRESS_SANITIZER)
#define MAYBE_ScreenCaptureVideoAndAudio DISABLED_ScreenCaptureVideoAndAudio
#else
#define MAYBE_ScreenCaptureVideoAndAudio ScreenCaptureVideoAndAudio
#endif  // BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestWithPicker,
                       MAYBE_ScreenCaptureVideoAndAudio) {
  if (!test_config_.should_prefer_current_tab &&
      !test_config_.accept_this_tab_capture) {
    GTEST_SKIP();
  }

  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);
  RunGetDisplayMedia(tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/true),
                     /*is_fake_ui=*/false,
                     /*expect_success=*/test_config_.accept_this_tab_capture,
                     /*is_tab_capture=*/PreferCurrentTab());
}

// Top level test for getDisplayMedia().
// Skips picker UI and uses fake device with specified type.
class WebRtcScreenCaptureBrowserTestWithFakeUI
    : public WebRtcScreenCaptureBrowserTest,
      public testing::WithParamInterface<TestConfigForFakeUI> {
 public:
  WebRtcScreenCaptureBrowserTestWithFakeUI() : test_config_(GetParam()) {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
    command_line->RemoveSwitch(switches::kUseFakeDeviceForMediaStream);
    command_line->AppendSwitchASCII(
        switches::kUseFakeDeviceForMediaStream,
        base::StringPrintf("display-media-type=%s",
                           test_config_.display_surface));

    AdjustCommandLineForZeroCopyCapture(command_line);
  }

  bool PreferCurrentTab() const override {
    return test_config_.should_prefer_current_tab;
  }

 protected:
  const TestConfigForFakeUI test_config_;
};

IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestWithFakeUI,
                       ScreenCaptureVideo) {
  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);
  RunGetDisplayMedia(tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/false),
                     /*is_fake_ui=*/true, /*expect_success=*/true,
                     /*is_tab_capture=*/PreferCurrentTab());

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(),
                            "getDisplaySurfaceSetting();"),
            test_config_.display_surface);

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(),
                            "getLogicalSurfaceSetting();"),
            "true");

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(), "getCursorSetting();"),
            "never");
}

IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestWithFakeUI,
                       ScreenCaptureVideoAndAudio) {
  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);
  RunGetDisplayMedia(tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/true),
                     /*is_fake_ui=*/true, /*expect_success=*/true,
                     /*is_tab_capture=*/PreferCurrentTab());

  EXPECT_EQ(content::EvalJs(tab->GetPrimaryMainFrame(), "hasAudioTrack();"),
            "true");
}

IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestWithFakeUI,
                       ScreenCaptureWithConstraints) {
  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);
  const int kMaxWidth = 200;
  const int kMaxFrameRate = 6;
  const std::string constraints = base::StringPrintf(
      "{video: {width: {max: %d}, frameRate: {max: %d}}, "
      "should_prefer_current_tab: "
      "%s}",
      kMaxWidth, kMaxFrameRate,
      base::ToString(test_config_.should_prefer_current_tab));
  RunGetDisplayMedia(tab, constraints,
                     /*is_fake_ui=*/true, /*expect_success=*/true,
                     /*is_tab_capture=*/PreferCurrentTab());
  auto result =
      content::EvalJs(tab->GetPrimaryMainFrame(), "getWidthSetting();")
          .ExtractString();
  int value;
  EXPECT_TRUE(base::StringToInt(result, &value));
  EXPECT_LE(value, kMaxWidth);

  EXPECT_EQ(
      content::EvalJs(tab->GetPrimaryMainFrame(), "getFrameRateSetting();"),
      base::StringPrintf("%d", kMaxFrameRate));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    WebRtcScreenCaptureBrowserTestWithFakeUI,
    Values(TestConfigForFakeUI{/*should_prefer_current_tab=*/false,
                               /*display_surface=*/"monitor"},
           TestConfigForFakeUI{/*should_prefer_current_tab=*/false,
                               /*display_surface=*/"window"},
           TestConfigForFakeUI{/*should_prefer_current_tab=*/false,
                               /*display_surface=*/"browser"},
           TestConfigForFakeUI{/*should_prefer_current_tab=*/true,
                               /*display_surface=*/"browser"}));

class WebRtcScreenCapturePermissionPolicyBrowserTest
    : public WebRtcScreenCaptureBrowserTest,
      public testing::WithParamInterface<
          std::tuple<GetDisplayMediaVariant, bool>> {
 public:
  WebRtcScreenCapturePermissionPolicyBrowserTest()
      : tested_variant_(std::get<0>(GetParam())),
        allowlisted_by_policy_(std::get<1>(GetParam())) {}

  ~WebRtcScreenCapturePermissionPolicyBrowserTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    command_line->AppendSwitchASCII(
        switches::kAutoSelectTabCaptureSourceByTitle, kMainHtmlTitle);
  }

  bool PreferCurrentTab() const override {
    return tested_variant_ == GetDisplayMediaVariant::kPreferCurrentTab;
  }

 protected:
  const GetDisplayMediaVariant tested_variant_;
  const bool allowlisted_by_policy_;
};

INSTANTIATE_TEST_SUITE_P(
    All,
    WebRtcScreenCapturePermissionPolicyBrowserTest,
    Combine(Values(GetDisplayMediaVariant::kStandard,
                   GetDisplayMediaVariant::kPreferCurrentTab),
            /*allowlisted_by_policy=*/Bool()));

// Flaky on Win bots http://crbug.com/1264805
#if BUILDFLAG(IS_WIN)
#define MAYBE_ScreenShareFromEmbedded DISABLED_ScreenShareFromEmbedded
#else
#define MAYBE_ScreenShareFromEmbedded ScreenShareFromEmbedded
#endif
IN_PROC_BROWSER_TEST_P(WebRtcScreenCapturePermissionPolicyBrowserTest,
                       MAYBE_ScreenShareFromEmbedded) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // The use of selfBrowserSurface is in order to simplify the test by
  // using just one tab. It is orthogonal to the test's purpose.
  const std::string constraints = base::StringPrintf(
      "{video: true, selfBrowserSurface: 'include', preferCurrentTab: %s}",
      base::ToString(PreferCurrentTab()));

  EXPECT_EQ(
      content::EvalJs(
          OpenTestPageInNewTab(kMainHtmlPage)->GetPrimaryMainFrame(),
          base::StringPrintf(
              "runGetDisplayMedia(%s, \"%s\");", constraints.c_str(),
              allowlisted_by_policy_ ? "allowedFrame" : "disallowedFrame")),
      allowlisted_by_policy_ ? "embedded-capture-success"
                             : "embedded-capture-failure");
}

// Test class used to test WebRTC with App Windows. Unfortunately, due to
// creating a diamond pattern of inheritance, we can only inherit from one of
// the PlatformAppBrowserTest and WebRtcBrowserTestBase (or it's children).
// We need a lot more heavy lifting on creating the AppWindow than we would get
// from WebRtcBrowserTestBase; so we inherit from PlatformAppBrowserTest to
// minimize the code duplication.
class WebRtcAppWindowCaptureBrowserTestWithPicker
    : public extensions::PlatformAppBrowserTest {
 public:
  WebRtcAppWindowCaptureBrowserTestWithPicker() = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    PlatformAppBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    command_line->AppendSwitchASCII(
        switches::kAutoSelectTabCaptureSourceByTitle, kAppWindowTitle);

    AdjustCommandLineForZeroCopyCapture(command_line);
  }

  void SetUpOnMainThread() override {
    extensions::PlatformAppBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(StartEmbeddedTestServer());

    // We will restrict all pages to "Tab Capture" only. This should force App
    // Windows to show up in the tabs list, and thus make it selectable.
    base::Value::List matchlist;
    matchlist.Append("*");
    browser()->profile()->GetPrefs()->SetList(
        prefs::kTabCaptureAllowedByOrigins, std::move(matchlist));
  }

  void TearDownOnMainThread() override {
    extensions::PlatformAppBrowserTest::TearDownOnMainThread();
    browser()->profile()->GetPrefs()->SetList(
        prefs::kTabCaptureAllowedByOrigins, base::Value::List());
  }

  extensions::AppWindow* CreateAppWindowWithTitle(const std::u16string& title) {
    extensions::AppWindow* app_window = CreateTestAppWindow("{}");
    EXPECT_TRUE(app_window);
    UpdateWebContentsTitle(app_window->web_contents(), title);

    return app_window;
  }

  // This is mostly lifted from WebRtcBrowserTestBase, with the exception that
  // because we know we're setting the auto-accept switches, we don't need to
  // set the PermissionsManager auto accept.
  content::WebContents* OpenTestPageInNewTab(const std::string& test_url) {
    chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, true);
    GURL url = embedded_test_server()->GetURL(test_url);
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
};

IN_PROC_BROWSER_TEST_F(WebRtcAppWindowCaptureBrowserTestWithPicker,
                       CaptureAppWindow) {
  extensions::AppWindow* app_window =
      CreateAppWindowWithTitle(base::UTF8ToUTF16(std::string(kAppWindowTitle)));
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  RunGetDisplayMedia(capturing_tab, "{video: true}", /*is_fake_ui=*/false,
                     /*expect_success=*/true,
                     /*is_tab_capture=*/true);
  CloseAppWindow(app_window);
}

// Base class for running tests with a SameOrigin policy applied.
class WebRtcSameOriginPolicyBrowserTest
    : public WebRtcScreenCaptureBrowserTest {
 public:
  ~WebRtcSameOriginPolicyBrowserTest() override = default;

  bool PreferCurrentTab() const override { return false; }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcScreenCaptureBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    command_line->AppendSwitchASCII(
        switches::kAutoSelectTabCaptureSourceByTitle, kSameOriginRenamedTitle);

    AdjustCommandLineForZeroCopyCapture(command_line);
  }

  void SetUpOnMainThread() override {
    WebRtcScreenCaptureBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());

    // Restrict all origins to SameOrigin tab capture only.
    base::Value::List matchlist;
    matchlist.Append("*");
    browser()->profile()->GetPrefs()->SetList(
        prefs::kSameOriginTabCaptureAllowedByOrigins, std::move(matchlist));
  }

  void TearDownOnMainThread() override {
    WebRtcScreenCaptureBrowserTest::TearDownOnMainThread();
    browser()->profile()->GetPrefs()->SetList(
        prefs::kSameOriginTabCaptureAllowedByOrigins, base::Value::List());
  }
};

IN_PROC_BROWSER_TEST_F(WebRtcSameOriginPolicyBrowserTest,
                       TerminateOnNavigationAwayFromSameOrigin) {
  // Open two pages, one to be captured, and one to do the capturing. Note that
  // we open the capturing page second so that is focused to allow the
  // getDisplayMedia request to succeed.
  content::WebContents* target_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  // Update the target tab to a unique title, so that we can ensure that it is
  // the one that gets captured via the autoselection.
  UpdateWebContentsTitle(
      target_tab, base::UTF8ToUTF16(std::string(kSameOriginRenamedTitle)));
  RunGetDisplayMedia(capturing_tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/true),
                     /*is_fake_ui=*/false, /*expect_success=*/true,
                     /*is_tab_capture=*/true);

  // Though the target tab should've been focused as a result of starting the
  // capture, we don't want to take a dependency on that behavior. Ensure that
  // the target tab is focused, so that we can navigate it easily. If it is
  // already focused, this will just no-op.
  int target_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(target_tab);
  browser()->tab_strip_model()->ActivateTabAt(
      target_index, TabStripUserGestureDetails(
                        TabStripUserGestureDetails::GestureType::kOther));
  ASSERT_EQ(target_tab, browser()->tab_strip_model()->GetActiveWebContents());

  // We navigate to a FileURL so that the origin will change, which should
  // trigger the capture to end.
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GetFileURL(kMainHtmlFileName)));

  // Verify that the video stream has ended.
  EXPECT_EQ(content::EvalJs(capturing_tab->GetPrimaryMainFrame(),
                            "waitVideoEnded();"),
            "ended");
}

IN_PROC_BROWSER_TEST_F(WebRtcSameOriginPolicyBrowserTest,
                       ContinueCapturingForSameOriginNavigation) {
  // Open two pages, one to be captured, and one to do the capturing. Note that
  // we open the capturing page second so that is focused to allow the
  // getDisplayMedia request to succeed.
  content::WebContents* target_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  // Update the target tab to a unique title, so that we can ensure that it is
  // the one that gets captured via the autoselection.
  UpdateWebContentsTitle(
      target_tab, base::UTF8ToUTF16(std::string(kSameOriginRenamedTitle)));
  RunGetDisplayMedia(capturing_tab,
                     GetConstraints(
                         /*video=*/true, /*audio=*/true),
                     /*is_fake_ui=*/false, /*expect_success=*/true,
                     /*is_tab_capture=*/true);

  // Though the target tab should've been focused as a result of starting the
  // capture, we don't want to take a dependency on that behavior. Ensure that
  // the target tab is focused, so that we can navigate it easily. If it is
  // already focused, this will just no-op.
  int target_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(target_tab);
  browser()->tab_strip_model()->ActivateTabAt(
      target_index, TabStripUserGestureDetails(
                        TabStripUserGestureDetails::GestureType::kOther));
  ASSERT_EQ(target_tab, browser()->tab_strip_model()->GetActiveWebContents());

  // We navigate using the test server so that the origin doesn't change.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/webrtc/captured_page_main.html")));

  // Verify that the video hasn't been ended.
  EXPECT_EQ(content::EvalJs(capturing_tab->GetPrimaryMainFrame(),
                            "video_track.readyState;"),
            "live");
}

class GetDisplayMediaVideoTrackBrowserTest
    : public WebRtcTestBase,
      public testing::WithParamInterface<std::tuple<bool, DisplaySurfaceType>> {
 public:
  GetDisplayMediaVideoTrackBrowserTest()
      : region_capture_enabled_(std::get<0>(GetParam())),
        display_surface_type_(std::get<1>(GetParam())) {}

  ~GetDisplayMediaVideoTrackBrowserTest() override = default;

  void SetUpInProcessBrowserTestFixture() override {
    DetectErrorsInJavaScript();
  }

  void SetUpOnMainThread() override {
    WebRtcTestBase::SetUpOnMainThread();

    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // Unlike SetUp(), this is called from the test body. This allows skipping
  // this test for (platform, test-case) combinations which are not supported.
  void SetupTest() {
    // Fire up the page.
    tab_ = OpenTestPageInNewTab(kMainHtmlPage);

    // Initiate the capture.
    ASSERT_EQ("capture-success",
              content::EvalJs(tab_->GetPrimaryMainFrame(),
                              "runGetDisplayMedia({video: true, audio: true}, "
                              "\"top-level-document\");"));
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcTestBase::SetUpCommandLine(command_line);

    std::vector<std::string> enabled_blink_features;
    std::vector<std::string> disabled_blink_features;

    if (region_capture_enabled_) {
      enabled_blink_features.push_back("RegionCapture");
    } else {
      disabled_blink_features.push_back("RegionCapture");
    }

    if (!enabled_blink_features.empty()) {
      command_line->AppendSwitchASCII(
          switches::kEnableBlinkFeatures,
          base::JoinString(enabled_blink_features, ","));
    }

    if (!disabled_blink_features.empty()) {
      command_line->AppendSwitchASCII(
          switches::kDisableBlinkFeatures,
          base::JoinString(disabled_blink_features, ","));
    }

    command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
    command_line->AppendSwitchASCII(
        switches::kUseFakeDeviceForMediaStream,
        base::StrCat({"display-media-type=",
                      DisplaySurfaceTypeAsString(display_surface_type_)}));

    AdjustCommandLineForZeroCopyCapture(command_line);
  }

  std::string GetVideoTrackType() {
    return content::EvalJs(tab_->GetPrimaryMainFrame(), "getVideoTrackType();")
        .ExtractString();
  }

  std::string GetVideoCloneTrackType() {
    return content::EvalJs(tab_->GetPrimaryMainFrame(),
                           "getVideoCloneTrackType();")
        .ExtractString();
  }

  bool HasAudioTrack() {
    std::string result =
        content::EvalJs(tab_->GetPrimaryMainFrame(), "hasAudioTrack();")
            .ExtractString();
    EXPECT_TRUE(result == "true" || result == "false");
    return result == "true";
  }

  std::string GetAudioTrackType() {
    return content::EvalJs(tab_->GetPrimaryMainFrame(), "getAudioTrackType();")
        .ExtractString();
  }

  std::string ExpectedVideoTrackType() const {
    switch (display_surface_type_) {
      case DisplaySurfaceType::kTab:
        return region_capture_enabled_ ? "BrowserCaptureMediaStreamTrack"
                                       : "MediaStreamTrack";
      case DisplaySurfaceType::kWindow:
      case DisplaySurfaceType::kScreen:
        return "MediaStreamTrack";
    }
    NOTREACHED();
  }

 protected:
  const bool region_capture_enabled_;
  const DisplaySurfaceType display_surface_type_;

 private:
  raw_ptr<content::WebContents, AcrossTasksDanglingUntriaged> tab_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(
    _,
    GetDisplayMediaVideoTrackBrowserTest,
    Combine(/*region_capture_enabled=*/Bool(),
            /*display_surface_type=*/
            Values(DisplaySurfaceType::kTab,
                   DisplaySurfaceType::kWindow,
                   DisplaySurfaceType::kScreen)),
    [](const testing::TestParamInfo<
        GetDisplayMediaVideoTrackBrowserTest::ParamType>& info) {
      return base::StrCat(
          {std::get<0>(info.param) ? "RegionCapture" : "",
           std::get<1>(info.param) == DisplaySurfaceType::kTab ? "Tab"
           : std::get<1>(info.param) == DisplaySurfaceType::kWindow
               ? "Window"
               : "Screen"});
    });

// Normally, each of these these would have its own test, but the number of
// combinations and the setup time for browser-tests make this undesirable,
// especially given the simplicity of each of these tests.
// After both (a) Conditional Focus and (b) Region Capture ship, this can
// simpplified to three non-parameterized tests (tab/window/screen).
IN_PROC_BROWSER_TEST_P(GetDisplayMediaVideoTrackBrowserTest, RunCombinedTest) {
  SetupTest();

  // Test #1: The video track is of the expected type.
  EXPECT_EQ(GetVideoTrackType(), ExpectedVideoTrackType());

  // Test #2: Video clones are of the same type as the original.
  EXPECT_EQ(GetVideoTrackType(), GetVideoCloneTrackType());

  // Test #3: Audio tracks are all simply MediaStreamTrack.
  if (HasAudioTrack()) {
    EXPECT_EQ(GetAudioTrackType(), "MediaStreamTrack");
  }
}

// Flaky on Mac, Windows, and ChromeOS bots, https://crbug.com/1371309
// Also some flakes on Linux ASAN/MSAN builds.
#if BUILDFLAG(IS_LINUX) && \
    !(defined(MEMORY_SANITIZER) || defined(ADDRESS_SANITIZER))
class GetDisplayMediaHiDpiBrowserTest
    : public WebRtcTestBase,
      public testing::WithParamInterface<TestConfigForMediaResolution> {
 public:
  GetDisplayMediaHiDpiBrowserTest() : test_config_(GetParam()) {}

  // The browser window size must be consistent with the
  // INSTANTIATE_TEST_SUITE_P TestConfigForMediaResolution configurations below.
  // See the comments there for more details.
  static constexpr int kBrowserWindowWidth = 800;
  static constexpr int kBrowserWindowHeight = 600;

  int constraint_width() const { return test_config_.constraint_width; }
  int constraint_height() const { return test_config_.constraint_height; }

  void SetUpInProcessBrowserTestFixture() override {
    WebRtcTestBase::SetUpInProcessBrowserTestFixture();
    DetectErrorsInJavaScript();
  }

  void SetUpOnMainThread() override {
    WebRtcTestBase::SetUpOnMainThread();

    ASSERT_TRUE(embedded_test_server()->Start());

    // Fire up the page.
    tab_ = OpenTestPageInNewTab(kMainHtmlPage);
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcTestBase::SetUpCommandLine(command_line);

    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);

    command_line->AppendSwitch(switches::kThisTabCaptureAutoAccept);

    command_line->AppendSwitchASCII(
        switches::kWindowSize,
        base::StringPrintf("%d,%d", kBrowserWindowWidth, kBrowserWindowHeight));

    // Optionally, in case the test isn't working correctly, you can turn on
    // debug logging for the feature to help track down problems. For example:
    // command_line->AppendSwitchASCII(switches::kVModule,
    //                                "*host_view*=1,*frame_tracker*=3");
  }

  std::string ResizeVideoForHiDpiCapture(int width, int height) {
    return RunJs(base::StringPrintf("resizeVideoForHiDpiCapture(%d, %d);",
                                    width, height));
  }

  double GetDevicePixelRatio() {
    std::string result = RunJs("getDevicePixelRatio();");
    double device_pixel_ratio;
    EXPECT_TRUE(base::StringToDouble(result, &device_pixel_ratio));
    return device_pixel_ratio;
  }

  std::string GetDisplaySurfaceSetting() {
    return RunJs("getDisplaySurfaceSetting();");
  }

  std::string GetLogicalSurfaceSetting() {
    return RunJs("getLogicalSurfaceSetting();");
  }

  content::WebContents* Tab() const { return tab_; }

 private:
  std::string RunJs(const std::string& command) {
    return content::EvalJs(tab_->GetPrimaryMainFrame(), command)
        .ExtractString();
  }

  const TestConfigForMediaResolution test_config_;
  raw_ptr<content::WebContents, AcrossTasksDanglingUntriaged> tab_ = nullptr;
};

IN_PROC_BROWSER_TEST_P(GetDisplayMediaHiDpiBrowserTest, Capture) {
  ASSERT_EQ(GetDevicePixelRatio(), 1.0);

  // Initiate the capture.
  RunGetDisplayMedia(
      Tab(),
      base::StringPrintf("{video: {width: {max: %d}, height: {max: %d}}, "
                         "preferCurrentTab: true}",
                         constraint_width(), constraint_height()),
      /*is_fake_ui=*/false, /*expect_success=*/true,
      /*is_tab_capture=*/true);

  // Ensure that the video is larger than the source tab to encourage use of a
  // higher-resolution video stream. The size is arbitrary, but it should be
  // significantly bigger than the switches::kWindowSize configured in this
  // test's setup.
  EXPECT_EQ(ResizeVideoForHiDpiCapture(kBrowserWindowWidth * 2,
                                       kBrowserWindowHeight * 2),
            "success");

  EXPECT_EQ(GetDisplaySurfaceSetting(), "browser");

  EXPECT_EQ(GetLogicalSurfaceSetting(), "true");

  // The HiDPI scale change only occurs once the capture has actually started
  // and the size information was propagated back to the browser process.
  // Waiting for the video to start playing helps ensure that this is the case.
  StartDetectingVideo(Tab(), "video");
  WaitForVideoToPlay(Tab());

  // If the video size is higher resolution than the browser window
  // size, expect that HiDPI mode should be active.
  bool expect_hidpi = constraint_width() > kBrowserWindowWidth &&
                      constraint_height() > kBrowserWindowHeight;

  double device_pixel_ratio = GetDevicePixelRatio();
  if (expect_hidpi) {
    EXPECT_GT(device_pixel_ratio, 1.0);
    EXPECT_LE(device_pixel_ratio, 2.0);
  } else {
    EXPECT_EQ(device_pixel_ratio, 1.0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    All,
    GetDisplayMediaHiDpiBrowserTest,
    // The test configurations use both large and small constraint sizes. The
    // small constraint sizes must be smaller than the configured window size
    // (cf. kBrowserWindowWidth and kBrowserWindowHeight in
    // GetDisplayMediaHiDpiBrowserTest above), and the large sizes must be
    // significantly larger than the browser window size.
    Values(TestConfigForMediaResolution{/*constraint_width=*/640,
                                        /*constraint_height=*/480},
           TestConfigForMediaResolution{/*constraint_width=*/3840,
                                        /*constraint_height=*/2160}));
#endif

class GetDisplayMediaChangeSourceBrowserTest
    : public WebRtcTestBase,
      public testing::WithParamInterface<std::tuple<bool, bool, bool>> {
 public:
  GetDisplayMediaChangeSourceBrowserTest()
      : dynamic_surface_switching_requested_(std::get<0>(GetParam())),
        feature_enabled_(std::get<1>(GetParam())),
        user_shared_audio_(std::get<2>(GetParam())) {}
  ~GetDisplayMediaChangeSourceBrowserTest() override = default;

  void SetUp() override {
    // TODO(crbug.com/40245399): Fix GetDisplayMediaChangeSourceBrowserTest with
    // audio requested on ChromeOS
#if BUILDFLAG(IS_CHROMEOS)
    if (dynamic_surface_switching_requested_ && feature_enabled_ &&
        user_shared_audio_) {
      GTEST_SKIP();
    }
#endif
    WebRtcTestBase::SetUp();
  }

  void SetUpInProcessBrowserTestFixture() override {
    feature_list_.InitWithFeatureState(
        media::kShareThisTabInsteadButtonGetDisplayMedia, feature_enabled_);

    WebRtcTestBase::SetUpInProcessBrowserTestFixture();

    DetectErrorsInJavaScript();

    base::FilePath test_dir;
    ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &test_dir));
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    command_line->AppendSwitchASCII(
        switches::kAutoSelectTabCaptureSourceByTitle, kCapturedTabTitle);

    AdjustCommandLineForZeroCopyCapture(command_line);

    if (!user_shared_audio_) {
      command_line->AppendSwitch(switches::kScreenCaptureAudioDefaultUnchecked);
    }
  }

  std::string GetConstraints() const {
    return base::StringPrintf(
        "{video: true, audio: true, surfaceSwitching: \"%s\"}",
        dynamic_surface_switching_requested_ ? "include" : "exclude");
  }

  bool ShouldShowShareThisTabInsteadButton() const {
    return dynamic_surface_switching_requested_ && feature_enabled_;
  }

 private:
  base::test::ScopedFeatureList feature_list_;
  const bool dynamic_surface_switching_requested_;
  const bool feature_enabled_;
  const bool user_shared_audio_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         GetDisplayMediaChangeSourceBrowserTest,
                         Combine(Bool(), Bool(), Bool()));

// TODO(crbug.com/40900706) Re-enable flaky test.
IN_PROC_BROWSER_TEST_P(GetDisplayMediaChangeSourceBrowserTest,
                       DISABLED_ChangeSource) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* captured_tab = OpenTestPageInNewTab(kCapturedPageMain);
  content::WebContents* other_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  RunGetDisplayMedia(capturing_tab, GetConstraints(), /*is_fake_ui=*/false,
                     /*expect_success=*/true,
                     /*is_tab_capture=*/true);

  EXPECT_TRUE(captured_tab->IsBeingCaptured());
  EXPECT_FALSE(other_tab->IsBeingCaptured());
  EXPECT_FALSE(capturing_tab->IsBeingCaptured());
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(captured_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              captured_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(capturing_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              capturing_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
  if (!ShouldShowShareThisTabInsteadButton()) {
    EXPECT_FALSE(HasShareThisTabInsteadButton(other_tab));
    return;
  }
  EXPECT_EQ(GetShareThisTabInsteadButtonLabel(other_tab),
            kShareThisTabInsteadMessage);

  // Click the share-this-tab-instead secondary button.
  GetDelegate(other_tab)->ShareThisTabInstead();

  // Wait until the capture of the other tab has started.
  while (!other_tab->IsBeingCaptured()) {
    base::RunLoop().RunUntilIdle();
  }

  EXPECT_FALSE(captured_tab->IsBeingCaptured());
  EXPECT_TRUE(other_tab->IsBeingCaptured());
  EXPECT_FALSE(capturing_tab->IsBeingCaptured());
  EXPECT_EQ(GetShareThisTabInsteadButtonLabel(captured_tab),
            kShareThisTabInsteadMessage);
  EXPECT_EQ(GetShareThisTabInsteadButtonLabel(other_tab),
            l10n_util::GetStringFUTF16(
                IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
                url_formatter::FormatOriginForSecurityDisplay(
                    other_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
                    url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(capturing_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              capturing_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
}

// TODO(crbug.com/40900706) Re-enable flaky test.
IN_PROC_BROWSER_TEST_P(GetDisplayMediaChangeSourceBrowserTest,
                       DISABLED_ChangeSourceThenStopTracksRemovesIndicators) {
  if (!ShouldShowShareThisTabInsteadButton()) {
    GTEST_SKIP();
  }

  ASSERT_TRUE(embedded_test_server()->Start());
  OpenTestPageInNewTab(kCapturedPageMain);
  content::WebContents* other_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  RunGetDisplayMedia(capturing_tab, GetConstraints(), /*is_fake_ui=*/false,
                     /*expect_success=*/true,
                     /*is_tab_capture=*/true);

  // Click the share-this-tab-instead secondary button.
  GetDelegate(other_tab)->ShareThisTabInstead();

  // Wait until the capture of the other tab has started.
  while (!other_tab->IsBeingCaptured()) {
    base::RunLoop().RunUntilIdle();
  }

  ASSERT_EQ(GetInfoBarManager(capturing_tab)->infobars().size(), 1u);
  StopAllTracks(capturing_tab);
  do {
    base::RunLoop().RunUntilIdle();
  } while (GetInfoBarManager(capturing_tab)->infobars().size() > 0u);
}

// TODO(crbug.com/40900706) Re-enable flaky test.
IN_PROC_BROWSER_TEST_P(GetDisplayMediaChangeSourceBrowserTest,
                       DISABLED_ChangeSourceReject) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* captured_tab = OpenTestPageInNewTab(kCapturedPageMain);
  content::WebContents* other_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  RunGetDisplayMedia(capturing_tab, GetConstraints(), /*is_fake_ui=*/false,
                     /*expect_success=*/true,
                     /*is_tab_capture=*/true);

  EXPECT_TRUE(captured_tab->IsBeingCaptured());
  EXPECT_FALSE(other_tab->IsBeingCaptured());
  EXPECT_FALSE(capturing_tab->IsBeingCaptured());
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(captured_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              captured_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(capturing_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              capturing_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
  if (!ShouldShowShareThisTabInsteadButton()) {
    EXPECT_FALSE(HasShareThisTabInsteadButton(other_tab));
    return;
  }
  EXPECT_EQ(GetShareThisTabInsteadButtonLabel(other_tab),
            kShareThisTabInsteadMessage);

  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(other_tab));
  while (browser()->tab_strip_model()->GetActiveWebContents() != other_tab) {
    base::RunLoop().RunUntilIdle();
  }

  browser()->profile()->GetPrefs()->SetBoolean(prefs::kScreenCaptureAllowed,
                                               false);

  // Click the share-this-tab-instead secondary button. This is rejected since
  // screen capture is not allowed by the above policy.
  GetDelegate(other_tab)->ShareThisTabInstead();

  // When "Share this tab instead" fails for other_tab, the focus goes back to
  // the captured tab. Wait until that happens:
  while (browser()->tab_strip_model()->GetActiveWebContents() != captured_tab) {
    base::RunLoop().RunUntilIdle();
  }

  EXPECT_TRUE(captured_tab->IsBeingCaptured());
  EXPECT_FALSE(other_tab->IsBeingCaptured());
  EXPECT_FALSE(capturing_tab->IsBeingCaptured());
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(captured_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              captured_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
  EXPECT_EQ(GetShareThisTabInsteadButtonLabel(other_tab),
            kShareThisTabInsteadMessage);
  EXPECT_EQ(
      GetShareThisTabInsteadButtonLabel(capturing_tab),
      l10n_util::GetStringFUTF16(
          IDS_TAB_SHARING_INFOBAR_SWITCH_TO_BUTTON,
          url_formatter::FormatOriginForSecurityDisplay(
              capturing_tab->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
              url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)));
}

class GetDisplayMediaSelfBrowserSurfaceBrowserTest
    : public WebRtcTestBase,
      public testing::WithParamInterface<std::string> {
 public:
  GetDisplayMediaSelfBrowserSurfaceBrowserTest()
      : self_browser_surface_(GetParam()) {}

  void SetUpInProcessBrowserTestFixture() override {
    WebRtcTestBase::SetUpInProcessBrowserTestFixture();

    DetectErrorsInJavaScript();

    base::FilePath test_dir;
    ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &test_dir));
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);

    command_line->AppendSwitchASCII(
        switches::kAutoSelectTabCaptureSourceByTitle, kMainHtmlTitle);

    AdjustCommandLineForZeroCopyCapture(command_line);
  }

  std::string GetConstraints(bool prefer_current_tab = false) {
    std::vector<std::string> constraints = {"video: true"};
    if (!self_browser_surface_.empty()) {
      constraints.push_back(base::StringPrintf("selfBrowserSurface: \"%s\"",
                                               self_browser_surface_.c_str()));
    }
    if (prefer_current_tab) {
      constraints.push_back("preferCurrentTab: true");
    }
    prefer_current_tab_ = prefer_current_tab;
    return "{" + base::JoinString(constraints, ",") + "}";
  }

  bool IsSelfBrowserSurfaceExclude() const {
    if (self_browser_surface_ == "" && !prefer_current_tab_) {
      // Special case - when using the new order, selfBrowserSurface
      // defaults to "exclude", unless {preferCurrentTab: true} is specified.
      return true;
    }
    return self_browser_surface_ == "exclude";
  }

 protected:
  // If empty, the constraint is unused. Otherwise, the value is either
  // "include" or "exclude"
  const std::string self_browser_surface_;

  // Whether {preferCurrentTab: true} will be specified by the test.
  bool prefer_current_tab_ = false;
};

INSTANTIATE_TEST_SUITE_P(All,
                         GetDisplayMediaSelfBrowserSurfaceBrowserTest,
                         Values("", "include", "exclude"));

IN_PROC_BROWSER_TEST_P(GetDisplayMediaSelfBrowserSurfaceBrowserTest,
                       SelfBrowserSurfaceChangesCapturedTab) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // This test relies on |capturing_tab| appearing earlier in the media picker,
  // and being auto-selected earlier if it is offered.
  content::WebContents* other_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  // Success expected either way, with the *other* tab being captured
  // when selfBrowserCapture is set to "exclude".
  RunGetDisplayMedia(capturing_tab, GetConstraints(), /*is_fake_ui=*/false,
                     /*expect_success=*/true, /*is_tab_capture=*/true);

  EXPECT_EQ(!IsSelfBrowserSurfaceExclude(), capturing_tab->IsBeingCaptured());
  EXPECT_EQ(IsSelfBrowserSurfaceExclude(), other_tab->IsBeingCaptured());
}

IN_PROC_BROWSER_TEST_P(GetDisplayMediaSelfBrowserSurfaceBrowserTest,
                       SelfBrowserSurfaceInteractionWithPreferCurrentTab) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // This test relies on |capturing_tab| appearing earlier in the media picker,
  // and being auto-selected earlier if it is offered.
  content::WebContents* other_tab = OpenTestPageInNewTab(kMainHtmlPage);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  // Test focal point - getDisplayMedia() rejects if preferCurrentTab
  // and exclude-current-tab are simultaneously specified.
  // Note that preferCurrentTab is hard-coded in this test while
  // exclude-current-tab is parameterized.
  const bool expect_success = (self_browser_surface_ != "exclude");
  const std::string expected_error =
      expect_success ? ""
                     : "TypeError: Failed to execute 'getDisplayMedia' on "
                       "'MediaDevices': Self-contradictory configuration "
                       "(preferCurrentTab and selfBrowserSurface=exclude).";
  RunGetDisplayMedia(capturing_tab, GetConstraints(/*prefer_current_tab=*/true),
                     /*is_fake_ui=*/false, expect_success,
                     /*is_tab_capture=*/true, expected_error);

  EXPECT_EQ(!IsSelfBrowserSurfaceExclude(), capturing_tab->IsBeingCaptured());
  EXPECT_FALSE(other_tab->IsBeingCaptured());
}

// Covers whether transient activation is required to call getDisplayMedia.
class GetDisplayMediaTransientActivationRequiredTest
    : public WebRtcScreenCaptureBrowserTest,
      public testing::WithParamInterface<
          std::tuple<bool, bool, bool, std::optional<std::string>>> {
 public:
  GetDisplayMediaTransientActivationRequiredTest()
      : with_user_gesture_(std::get<0>(GetParam())),
        require_gesture_feature_enabled_(std::get<1>(GetParam())),
        prefer_current_tab_(std::get<2>(GetParam())),
        policy_allowlist_value_(std::get<3>(GetParam())) {}
  ~GetDisplayMediaTransientActivationRequiredTest() override = default;

  static std::string GetDescription(
      const testing::TestParamInfo<
          GetDisplayMediaTransientActivationRequiredTest::ParamType>& info) {
    std::string name = base::StrCat(
        {std::get<0>(info.param) ? "WithUserGesture_" : "WithoutUserGesture_",
         std::get<1>(info.param) ? "RequireGestureFeatureEnabled_"
                                 : "_RequireGestureFeatureDisabled_",
         std::get<2>(info.param) ? "PreferCurrentTab_"
                                 : "DontPreferCurrentTab_",
         std::get<3>(info.param).has_value()
             ? (*std::get<3>(info.param) == kEmbeddedTestServerOrigin)
                   ? "Allowlisted"
                   : "OtherAllowlisted"
             : "NoPolicySet"});
    return name;
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
  }

  void SetUpInProcessBrowserTestFixture() override {
    WebRtcScreenCaptureBrowserTest::SetUpInProcessBrowserTestFixture();

    if (require_gesture_feature_enabled_) {
      feature_list_.InitAndEnableFeature(
          blink::features::kGetDisplayMediaRequiresUserActivation);
    } else {
      feature_list_.InitAndDisableFeature(
          blink::features::kGetDisplayMediaRequiresUserActivation);
    }

    policy_provider_.SetDefaultReturns(
        /*is_initialization_complete_return=*/true,
        /*is_first_policy_load_complete_return=*/true);
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(
        &policy_provider_);

    DetectErrorsInJavaScript();
  }

  bool PreferCurrentTab() const override { return prefer_current_tab_; }

 protected:
  const bool with_user_gesture_;
  const bool require_gesture_feature_enabled_;
  const bool prefer_current_tab_;
  const std::optional<std::string> policy_allowlist_value_;
  base::test::ScopedFeatureList feature_list_;
  testing::NiceMock<policy::MockConfigurationPolicyProvider> policy_provider_;
};

IN_PROC_BROWSER_TEST_P(GetDisplayMediaTransientActivationRequiredTest, Check) {
  ASSERT_TRUE(embedded_test_server()->Start());

  if (policy_allowlist_value_.has_value()) {
    policy::PolicyMap policy_map;
    base::Value::List allowed_origins;
    allowed_origins.Append(base::Value(*policy_allowlist_value_));
    policy_map.Set(policy::key::kScreenCaptureWithoutGestureAllowedForOrigins,
                   policy::POLICY_LEVEL_MANDATORY, policy::POLICY_SCOPE_USER,
                   policy::POLICY_SOURCE_PLATFORM,
                   base::Value(std::move(allowed_origins)), nullptr);
    policy_provider_.UpdateChromePolicy(policy_map);
  }

  content::WebContents* tab = OpenTestPageInNewTab(kMainHtmlPage);

  const bool expect_success =
      with_user_gesture_ || !require_gesture_feature_enabled_ ||
      (policy_allowlist_value_ &&
       *policy_allowlist_value_ == kEmbeddedTestServerOrigin);
  const std::string expected_error =
      expect_success
          ? ""
          : "InvalidStateError: Failed to execute 'getDisplayMedia' on "
            "'MediaDevices': getDisplayMedia() requires transient activation "
            "(user gesture).";

  RunGetDisplayMedia(tab, GetConstraints(/*video=*/true, /*audio=*/true),
                     /*is_fake_ui=*/true, expect_success,
                     /*is_tab_capture=*/false, expected_error,
                     with_user_gesture_);
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    GetDisplayMediaTransientActivationRequiredTest,
    Combine(
        /*with_user_gesture=*/Bool(),
        /*require_gesture_feature_enabled=*/Bool(),
        /*prefer_current_tab=*/Bool(),
        /*policy_allowlist_value=*/
        Values(std::nullopt, kEmbeddedTestServerOrigin, kOtherOrigin)),
    &GetDisplayMediaTransientActivationRequiredTest::GetDescription);

// Covers whether transient activation is conferred by the user's interaction
// with the prompt shown by getDisplayMedia.
class GetDisplayMediaConfersTransientActivationTest
    : public WebRtcScreenCaptureBrowserTest,
      public testing::WithParamInterface<std::tuple<bool, bool, bool>> {
 public:
  static std::string GetDescription(
      const testing::TestParamInfo<
          GetDisplayMediaConfersTransientActivationTest::ParamType>& info) {
    const bool feature_enabled = std::get<0>(info.param);
    const bool prefer_current_tab = std::get<1>(info.param);
    const bool user_accepts = std::get<2>(info.param);
    return base::StrCat(
        {"WithFeature", feature_enabled ? "Enabled" : "Disabled",
         prefer_current_tab ? "PreferCurrentTabVariant" : "StandardVariant",
         "User", user_accepts ? "Accepts" : "Rejects", "Prompt"});
  }

  GetDisplayMediaConfersTransientActivationTest()
      : feature_enabled_(std::get<0>(GetParam())),
        prefer_current_tab_(std::get<1>(GetParam())),
        user_accepts_(std::get<2>(GetParam())) {}
  ~GetDisplayMediaConfersTransientActivationTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    if (prefer_current_tab_) {
      command_line->AppendSwitch(user_accepts_
                                     ? switches::kThisTabCaptureAutoAccept
                                     : switches::kThisTabCaptureAutoReject);
    } else {
      if (user_accepts_) {
        command_line->AppendSwitchASCII(
            switches::kAutoSelectTabCaptureSourceByTitle, kCapturedTabTitle);
      } else {
        command_line->AppendSwitch(switches::kCaptureAutoReject);
      }
    }
  }

  void SetUpInProcessBrowserTestFixture() override {
    WebRtcScreenCaptureBrowserTest::SetUpInProcessBrowserTestFixture();
    feature_list_.InitWithFeatureState(media::kGetDisplayMediaConfersActivation,
                                       feature_enabled_);
    DetectErrorsInJavaScript();
  }

  bool PreferCurrentTab() const override { return prefer_current_tab_; }

 protected:
  const bool feature_enabled_;
  const bool prefer_current_tab_;
  const bool user_accepts_;

 private:
  base::test::ScopedFeatureList feature_list_;
};

INSTANTIATE_TEST_SUITE_P(
    ,
    GetDisplayMediaConfersTransientActivationTest,
    Combine(
        /*feature_enabled=*/Bool(),
        /*prefer_current_tab=*/Bool(),
        /*user_accepts=*/Bool()),
    &GetDisplayMediaConfersTransientActivationTest::GetDescription);

// TODO(crbug.com/420406085): Re-enable the tests.
#if defined(MEMORY_SANITIZER) || defined(ADDRESS_SANITIZER)
#define MAYBE_GdmActivation_RunTest DISABLED_RunTest
#else
#define MAYBE_GdmActivation_RunTest RunTest
#endif
IN_PROC_BROWSER_TEST_P(GetDisplayMediaConfersTransientActivationTest,
                       MAYBE_GdmActivation_RunTest) {
  // Setup
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenTestPageInNewTab(kCapturedPageMain);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  ASSERT_FALSE(
      capturing_tab->GetPrimaryMainFrame()->HasTransientUserActivation());

  // `with_user_gesture` is set to `false` because `getDisplayMedia()` does not
  // currently consume the activation (nor requires it).
  RunGetDisplayMedia(
      capturing_tab,
      GetConstraints(/*video=*/true, /*audio=*/true, prefer_current_tab_),
      /*is_fake_ui=*/false, /*expect_success=*/user_accepts_,
      /*is_tab_capture=*/true, /*expected_error=*/"",
      /*with_user_gesture=*/false);

  EXPECT_EQ(capturing_tab->GetPrimaryMainFrame()->HasTransientUserActivation(),
            feature_enabled_ && user_accepts_);
}

// This test suite ensures that, no matter the combination of inputs,
// an interaction with getUserMedia() does not confer transient activation.
// That is, the code authored for gDM does not mistrigger and run for gUM.
class GetUserMediaDoesNotConferTransientActivationTest
    : public WebRtcTestBase,
      public testing::WithParamInterface<std::tuple<bool, bool, bool>> {
 public:
  static std::string GetDescription(
      const testing::TestParamInfo<
          GetUserMediaDoesNotConferTransientActivationTest::ParamType>& info) {
    const bool video = std::get<0>(info.param);
    const bool audio = std::get<1>(info.param);
    const bool user_accepts = std::get<2>(info.param);
    return base::StrCat({"Video", video ? "On" : "Off", "Audio",
                         audio ? "On" : "Off", "User",
                         user_accepts ? "Accepts" : "Rejects", "Prompt"});
  }

  GetUserMediaDoesNotConferTransientActivationTest()
      : video_(std::get<0>(GetParam())),
        audio_(std::get<1>(GetParam())),
        user_accepts_(std::get<2>(GetParam())) {}
  ~GetUserMediaDoesNotConferTransientActivationTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
  }

  void SetUpInProcessBrowserTestFixture() override {
    WebRtcTestBase::SetUpInProcessBrowserTestFixture();
    DetectErrorsInJavaScript();
  }

 protected:
  const bool video_;
  const bool audio_;
  const bool user_accepts_;
};

INSTANTIATE_TEST_SUITE_P(
    ,
    GetUserMediaDoesNotConferTransientActivationTest,
    Combine(
        /*video=*/Bool(),
        /*audio=*/Bool(),
        /*user_accepts=*/Bool()),
    &GetUserMediaDoesNotConferTransientActivationTest::GetDescription);

// TODO(crbug.com/420406085): Re-enable the tests.
#if defined(MEMORY_SANITIZER) || defined(ADDRESS_SANITIZER)
#define MAYBE_GumActivation_RunTest DISABLED_RunTest
#else
#define MAYBE_GumActivation_RunTest RunTest
#endif
IN_PROC_BROWSER_TEST_P(GetUserMediaDoesNotConferTransientActivationTest,
                       MAYBE_GumActivation_RunTest) {
  if (!video_ && !audio_) {
    GTEST_SKIP();
  }

  // Setup
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* const wc = OpenTestPageInNewTab(kMainHtmlPage);
  permissions::PermissionRequestManager::FromWebContents(wc)
      ->set_auto_response_for_test(
          permissions::PermissionRequestManager::ACCEPT_ALL);

  ASSERT_FALSE(wc->GetPrimaryMainFrame()->HasTransientUserActivation());

  const std::string constraints =
      base::StringPrintf("{video: %s, audio: %s}", video_ ? "true" : "false",
                         audio_ ? "true" : "false");
  RunGetUserMedia(wc, constraints);

  EXPECT_FALSE(wc->GetPrimaryMainFrame()->HasTransientUserActivation());
}

// Encapsulates information about a capture-session in which one tab starts
// out capturing a specific other tab, and later possibly moves to capturing
// another tab. The encapsulation of this state allows for more succinct tests,
// especially when testing multiple concurrent capture-sessions.
class CaptureSessionDetails {
 public:
  enum class CapturedTab {
    kInitiallyCapturedTab,
    kOtherTab,
    kCapturingTab,  // Share-this-tab-instead can cause self-capture.
  };

  CaptureSessionDetails(std::string session_name,
                        WebContents* initially_captured_tab,
                        WebContents* other_tab,
                        WebContents* capturing_tab)
      : session_name_(std::move(session_name)),
        initially_captured_tab_(initially_captured_tab),
        other_tab_(other_tab),
        capturing_tab_(capturing_tab) {}

  std::unique_ptr<MockCapturedSurfaceController>
  MakeMockCapturedSurfaceController(
      blink::mojom::CapturedSurfaceControlResult permission_response,
      GlobalRenderFrameHostId gdm_rfhid,
      WebContentsMediaCaptureId captured_wc_id) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);

    EXPECT_FALSE(mock_captured_surface_controller_)
        << "Instantiated more CapturedSurfaceController than expected.";

    mock_captured_surface_controller_ =
        new MockCapturedSurfaceController(gdm_rfhid, captured_wc_id);
    mock_captured_surface_controller_->SetRequestPermissionResponse(
        permission_response);

    return base::WrapUnique(mock_captured_surface_controller_.get());
  }

  void RunGetDisplayMedia() {
    ::RunGetDisplayMedia(capturing_tab_,
                         "{video: true, surfaceSwitching: \"include\"}",
                         /*is_fake_ui=*/false,
                         /*expect_success=*/true,
                         /*is_tab_capture=*/true);
  }

  // Sets a factory that produces mock controllers for Captured Surface Control
  // and attaches them to `this` CaptureSessionDetails object. They will respond
  // to permission checks with the preconfigured `permission_response`.
  //
  // The factory is global. Tests that instantiate multiple capture sessions
  // should make sure to call this again from the new CaptureSessionDetails
  // object at the appropriate time, thereby replacing the factory after it's
  // used.
  //
  // This method is called on the UI thread. Hops to the IO thread and sets the
  // CSC-factory, then unblocks execution on the UI thread.
  void SetCapturedSurfaceControllerFactory(
      blink::mojom::CapturedSurfaceControlResult permission_response =
          blink::mojom::CapturedSurfaceControlResult::kSuccess) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);

    base::RunLoop run_loop;
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            &CaptureSessionDetails::SetCapturedSurfaceControllerFactoryOnIO,
            base::Unretained(this), permission_response,
            run_loop.QuitClosure()));
    run_loop.Run();
  }

  void SetExpectUpdateCaptureTarget() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);

    base::RunLoop run_loop;
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&CaptureSessionDetails::ExpectUpdateCaptureTargetOnIO,
                       base::Unretained(this), run_loop.QuitClosure()));
    run_loop.Run();
  }

  void VerifyAndClearExpectations() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    SCOPED_TRACE(session_name_);

    base::RunLoop run_loop;
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&CaptureSessionDetails::VerifyAndClearExpectationsOnIO,
                       base::Unretained(this), run_loop.QuitClosure()));
    run_loop.Run();
  }

  WebContents* GetTab(CapturedTab captured_tab) {
    switch (captured_tab) {
      case CapturedTab::kInitiallyCapturedTab:
        return initially_captured_tab_;
      case CapturedTab::kOtherTab:
        return other_tab_;
      case CapturedTab::kCapturingTab:
        return capturing_tab_;
    }
    NOTREACHED();
  }

  WebContents* GetCapturedTab() {
    CHECK_EQ(static_cast<int>(capturing_tab_->IsBeingCaptured()) +
                 static_cast<int>(initially_captured_tab_->IsBeingCaptured()) +
                 static_cast<int>(other_tab_->IsBeingCaptured()),
             1);
    if (capturing_tab_->IsBeingCaptured()) {
      return capturing_tab_;
    } else if (initially_captured_tab_->IsBeingCaptured()) {
      return initially_captured_tab_;
    } else if (other_tab_->IsBeingCaptured()) {
      return other_tab_;
    }
    NOTREACHED();
  }

  // Get the tab that's neither capturing nor being captured.
  WebContents* GetNonCapturedTab() {
    CHECK(!capturing_tab_->IsBeingCaptured());
    CHECK_EQ(static_cast<int>(initially_captured_tab_->IsBeingCaptured()) +
                 static_cast<int>(other_tab_->IsBeingCaptured()),
             1);

    return initially_captured_tab_->IsBeingCaptured() ? other_tab_
                                                      : initially_captured_tab_;
  }

  void WaitForCaptureOf(CapturedTab expected_tab) {
    while (!GetTab(expected_tab)->IsBeingCaptured()) {
      base::RunLoop().RunUntilIdle();
    }
    ExpectCapturedTab(expected_tab);
  }

  void ExpectCapturedTab(CapturedTab captured) {
    EXPECT_EQ(initially_captured_tab_->IsBeingCaptured(),
              captured == CapturedTab::kInitiallyCapturedTab);
    EXPECT_EQ(other_tab_->IsBeingCaptured(),
              captured == CapturedTab::kOtherTab);
    EXPECT_EQ(capturing_tab_->IsBeingCaptured(),
              captured == CapturedTab::kCapturingTab);

    EXPECT_EQ(GetShareThisTabInsteadButtonLabel(GetNonCapturedTab()),
              kShareThisTabInsteadMessage);
  }

  // Forwards from the target element, or stops forwarding if target is "null".
  std::string ForwardWheel(const std::string& target) {
    return content::EvalJs(
               capturing_tab_->GetPrimaryMainFrame(),
               base::StringPrintf("forwardWheel(%s);", target.c_str()))
        .ExtractString();
  }

  void UpdateZoomLevel(const std::string& action, bool expect_success = true) {
    const std::string command =
        base::StringPrintf("updateZoomLevel(\"%s\");", action);
    const std::string expected_result = base::StringPrintf(
        "%s-zoom-level-%s", action, expect_success ? "resolved" : "error");

    EXPECT_EQ(content::EvalJs(capturing_tab_->GetPrimaryMainFrame(), command),
              expected_result);
  }

  std::optional<int> GetZoomLevel() {
    const content::EvalJsResult result = content::EvalJs(
        capturing_tab_->GetPrimaryMainFrame(), "getZoomLevel();");
    return (result == nullptr) ? std::nullopt
                               : std::make_optional<int>(result.ExtractInt());
  }

  // Call `controller.getSupportedZoomLevels()`.
  // Returns the result if successful; the error otherwise.
  base::expected<std::vector<int>, std::string> GetSupportedZoomLevels() {
    content::EvalJsResult js_result = content::EvalJs(
        capturing_tab_->GetPrimaryMainFrame(), "getSupportedZoomLevels();");

    base::Value::List list = js_result.ExtractList();
    EXPECT_GE(list.size(), 1u);
    if (list.size() == 1u) {
      // Reserved for an error.
      return base::unexpected(list[0].GetString());
    }

    std::vector<int> result;
    result.reserve(list.size());
    for (const base::Value& val : list) {
      EXPECT_TRUE(val.is_int());
      result.push_back(val.GetInt());
    }
    return result;
  }

  int GetZoomLevelChangeEventsSinceLast() {
    // Note that ExtractInt() will implicitly ensure the script did not run into
    // an error.
    return content::EvalJs(capturing_tab_->GetPrimaryMainFrame(),
                           "zoomLevelChangeEventsSinceLast();")
        .ExtractInt();
  }

  WebContents* initially_captured_tab() const {
    return initially_captured_tab_;
  }
  WebContents* other_tab() const { return other_tab_; }
  WebContents* capturing_tab() const { return capturing_tab_; }

 private:
  void SetCapturedSurfaceControllerFactoryOnIO(
      blink::mojom::CapturedSurfaceControlResult permission_response,
      base::RepeatingClosure done_closure) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);

    CapturedSurfaceControllerFactoryCallback factory = base::BindRepeating(
        &CaptureSessionDetails::MakeMockCapturedSurfaceController,
        base::Unretained(this), permission_response);

    content::SetCapturedSurfaceControllerFactoryForTesting(factory);

    done_closure.Run();
  }

  void ExpectUpdateCaptureTargetOnIO(base::RepeatingClosure done_closure) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);

    CHECK(mock_captured_surface_controller_);
    EXPECT_CALL(*mock_captured_surface_controller_, UpdateCaptureTarget(_))
        .Times(1);

    done_closure.Run();
  }

  void VerifyAndClearExpectationsOnIO(base::RepeatingClosure done_closure) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);

    CHECK(mock_captured_surface_controller_);
    Mock::VerifyAndClearExpectations(mock_captured_surface_controller_);
    mock_captured_surface_controller_ = nullptr;

    done_closure.Run();
  }

  const std::string session_name_;

  // Handled on UI thread.
  const raw_ptr<WebContents> initially_captured_tab_;
  const raw_ptr<WebContents> other_tab_;
  const raw_ptr<WebContents> capturing_tab_;

  // Handled on the IO thread.
  raw_ptr<MockCapturedSurfaceController> mock_captured_surface_controller_;
};

class CapturedSurfaceControlTest : public WebRtcTestBase {
 public:
  enum class Action {
    kForwardWheel,      // forwardWheel(validElement)
    kForwardWheelNull,  // forwardWheel(null)
    kIncreaseZoomLevel,
    kDecreaseZoomLevel,
    kResetZoomLevel,
    kGetZoomLevel,
    kGetSupportedZoomLevels,
  };

  static const char* ToZoomLevelAction(Action input) {
    switch (input) {
      case Action::kIncreaseZoomLevel:
        return "increase";
      case Action::kDecreaseZoomLevel:
        return "decrease";
      case Action::kResetZoomLevel:
        return "reset";
      case Action::kForwardWheel:
      case Action::kForwardWheelNull:
      case Action::kGetZoomLevel:
      case Action::kGetSupportedZoomLevels:
        break;
    }
    NOTREACHED() << "Not a ZoomLevelAction.";
  }

  static bool ShouldTriggerCscIndicator(Action action) {
    switch (action) {
      case Action::kForwardWheel:
      case Action::kIncreaseZoomLevel:
      case Action::kDecreaseZoomLevel:
      case Action::kResetZoomLevel:
        return true;
      case Action::kGetZoomLevel:
      case Action::kGetSupportedZoomLevels:
      case Action::kForwardWheelNull:
        return false;
    }
    NOTREACHED();
  }

  static void MakeValidApiCall(CaptureSessionDetails& capture_session,
                               Action action) {
    switch (action) {
      case Action::kForwardWheel:
        EXPECT_EQ(capture_session.ForwardWheel("video"),
                  "forward-wheel-resolved");
        return;
      case Action::kForwardWheelNull:
        EXPECT_EQ(capture_session.ForwardWheel("null"),
                  "forward-wheel-resolved");
        return;
      case Action::kIncreaseZoomLevel:
      case Action::kDecreaseZoomLevel:
      case Action::kResetZoomLevel:
        capture_session.UpdateZoomLevel(ToZoomLevelAction(action));
        return;
      case Action::kGetZoomLevel:
        capture_session.GetZoomLevel();
        return;
      case Action::kGetSupportedZoomLevels:
        (void)capture_session.GetSupportedZoomLevels();
        return;
    }
    NOTREACHED();
  }

  CapturedSurfaceControlTest() = default;
  ~CapturedSurfaceControlTest() override = default;

  void SetUpInProcessBrowserTestFixture() override {
    feature_list_.InitWithFeatures(
        /*enabled_features=*/{media::kShareThisTabInsteadButtonGetDisplayMedia,
                              blink::features::kCapturedSurfaceControl},
        /*disabled_features=*/{});

    WebRtcTestBase::SetUpInProcessBrowserTestFixture();

    DetectErrorsInJavaScript();

    base::FilePath test_dir;
    ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &test_dir));
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
    command_line->AppendSwitchASCII(
        switches::kAutoSelectTabCaptureSourceByTitle, kCapturedTabTitle);
    command_line->AppendSwitch(
        switches::kAutoGrantCapturedSurfaceControlPrompt);

    AdjustCommandLineForZeroCopyCapture(command_line);
  }

  // Runs the `ChangeSourceWorksOnCorrectCaptureSession` test.
  // This is defined as a method in order to test both the first/second
  // capture experiencing the share-this-tab-instead click, without having
  // to parameterize the entire test suite.
  void RunChangeSourceWorksOnCorrectCaptureSession(
      size_t session_experiencing_change);

  // Runs the `ChangingCapturedTabZoomChangeEventTest` test.
  // This is defined as a method in order to test both of the following tests
  // without the overhead and unclarity of parameterizing a test suite for it.
  // * ChangingCapturedTabIssuesEventIfDifferentZoomLevels
  // * ChangingCapturedTabDoesNotIssueEventIfSameZoomLevels
  void RunChangingCapturedTabZoomChangeEventTest(double zoom_level_first_tab,
                                                 double zoom_level_second_tab);

 protected:
  using CapturedTab = ::CaptureSessionDetails::CapturedTab;

  CaptureSessionDetails MakeCaptureSessionDetails(std::string session_name) {
    return CaptureSessionDetails(
        std::move(session_name),
        /*initially_captured_tab=*/OpenTestPageInNewTab(kCapturedPageMain),
        /*other_tab=*/(OpenTestPageInNewTab(kMainHtmlPage)),
        /*capturing_tab=*/(OpenTestPageInNewTab(kMainHtmlPage)));
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

using CscAction = CapturedSurfaceControlTest::Action;

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       UnboundCaptureControllerReportNullZoomLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Note absence of call to RunGetDisplayMedia().
  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  EXPECT_EQ(capture_session.GetZoomLevel(), std::nullopt);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       CorrectlyReportDefaultCapturedSurfaceZoomLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Note absence of call to SetZoomFactor().
  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  WebContents* const captured_tab = capture_session.GetCapturedTab();
  EXPECT_EQ(GetZoomLevelPercentage(captured_tab), 100);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       CorrectlyReportNonDefaultCapturedSurfaceZoomLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  // Set the zoom factor to something other than the default before
  // capture starts.
  WebContents* const captured_tab =
      capture_session.GetTab(CapturedTab::kInitiallyCapturedTab);
  SetZoomFactor(captured_tab, 0.5);

  // Start the capture.
  capture_session.RunGetDisplayMedia();
  ASSERT_EQ(capture_session.GetCapturedTab(), captured_tab);

  // The initially reported zoom level is as expected.
  EXPECT_EQ(GetZoomLevelPercentage(captured_tab), 50);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       GetSupportedZoomLevelsFailsOnUnboundCaptureController) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Note absence of call to RunGetDisplayMedia().
  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  const base::expected<std::vector<int>, std::string> result =
      capture_session.GetSupportedZoomLevels();
  EXPECT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("InvalidStateError"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       GetSupportedZoomLevelsSucceedsIfCapturingTab) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  EXPECT_TRUE(capture_session.GetSupportedZoomLevels().has_value());
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       GetSupportedZoomLevelsMonotonouslyIncreasing) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  const base::expected<std::vector<int>, std::string> result =
      capture_session.GetSupportedZoomLevels();
  EXPECT_TRUE(result.has_value());
  const std::vector<int>& values = result.value();
  ASSERT_GE(values.size(), 2u);
  for (size_t i = 0; i < values.size() - 1; ++i) {
    EXPECT_GT(values[i + 1], values[i]);
  }
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       GetSupportedZoomLevelsFailsIfTracksStopped) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  ASSERT_TRUE(capture_session.GetSupportedZoomLevels().has_value());

  StopAllTracks(capture_session.capturing_tab());

  const base::expected<std::vector<int>, std::string> result =
      capture_session.GetSupportedZoomLevels();
  EXPECT_FALSE(result.has_value());
  EXPECT_NE(result.error().find("InvalidStateError"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(
    CapturedSurfaceControlTest,
    NoZoomLevelChangeEventFiredWhenCaptureStartsWithDefaultZoomLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Note absence of call to SetZoomFactor().
  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 0);
}

IN_PROC_BROWSER_TEST_F(
    CapturedSurfaceControlTest,
    NoZoomLevelChangeEventFiredWhenCaptureStartsWithNonDefaultZoomLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  // Set the zoom factor to something other than the default before
  // capture starts.
  WebContents* const captured_tab =
      capture_session.GetTab(CapturedTab::kInitiallyCapturedTab);
  SetZoomFactor(captured_tab, 0.5);

  capture_session.RunGetDisplayMedia();
  ASSERT_EQ(capture_session.GetCapturedTab(), captured_tab);

  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 0);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       IncreaseZoomLevelSucceedsBelowMaxValue) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  WebContents* const captured_tab = capture_session.GetCapturedTab();
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), 100);

  capture_session.UpdateZoomLevel("increase");

  // Check both the actual zoom level as well as the one reported to JS.
  const int actual_zoom_level_percent = GetZoomLevelPercentage(captured_tab);
  EXPECT_GT(actual_zoom_level_percent, 100);
  EXPECT_EQ(actual_zoom_level_percent, capture_session.GetZoomLevel());
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       IncreaseZoomLevelFailsAtMaxValue) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  WebContents* const captured_tab = capture_session.GetCapturedTab();
  const double max_factor = blink::kPresetBrowserZoomFactors.back();
  SetZoomFactor(captured_tab, max_factor);
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), std::round(100 * max_factor));

  capture_session.UpdateZoomLevel("increase", /*expect_success=*/false);

  // Check both the actual zoom level as well as the one reported to JS.
  const int actual_zoom_level_percent = GetZoomLevelPercentage(captured_tab);
  EXPECT_EQ(actual_zoom_level_percent, std::round(100 * max_factor));
  EXPECT_EQ(actual_zoom_level_percent, capture_session.GetZoomLevel());
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       IncreaseZoomLevelIssuesEvent) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  capture_session.UpdateZoomLevel("increase");
  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 1);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       DecreaseZoomLevelSucceedsAboveMinValue) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  WebContents* const captured_tab = capture_session.GetCapturedTab();
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), 100);

  capture_session.UpdateZoomLevel("decrease");

  // Check both the actual zoom level as well as the one reported to JS.
  const int actual_zoom_level_percent = GetZoomLevelPercentage(captured_tab);
  EXPECT_LT(actual_zoom_level_percent, 100);
  EXPECT_EQ(actual_zoom_level_percent, capture_session.GetZoomLevel());
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       DecreaseZoomLevelFailsAtMinValue) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  WebContents* const captured_tab = capture_session.GetCapturedTab();
  const double min_factor = blink::kPresetBrowserZoomFactors.front();
  SetZoomFactor(captured_tab, min_factor);
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), std::round(100 * min_factor));

  capture_session.UpdateZoomLevel("decrease", /*expect_success=*/false);

  // Check both the actual zoom level as well as the one reported to JS.
  const int actual_zoom_level_percent = GetZoomLevelPercentage(captured_tab);
  EXPECT_EQ(actual_zoom_level_percent, std::round(100 * min_factor));
  EXPECT_EQ(actual_zoom_level_percent, capture_session.GetZoomLevel());
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       DecreaseZoomLevelIssuesEvent) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();

  capture_session.UpdateZoomLevel("decrease");
  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 1);
}

// The "expected" case of resetZoomLevel() - changing *back* to
// the default value.
IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ResetZoomLevelSucceedsIfNonDefaultLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  // Set the zoom factor to something other than the default before
  // capture starts.
  WebContents* const captured_tab =
      capture_session.GetTab(CapturedTab::kInitiallyCapturedTab);
  SetZoomFactor(captured_tab, 0.5);

  // Start the capture.
  capture_session.RunGetDisplayMedia();
  ASSERT_EQ(capture_session.GetCapturedTab(), captured_tab);
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), 50);

  // Reset works as expected.
  capture_session.UpdateZoomLevel("reset");

  // Check both the actual zoom level as well as the one reported to JS.
  EXPECT_EQ(GetZoomLevelPercentage(captured_tab), 100);
  EXPECT_EQ(capture_session.GetZoomLevel(), 100);
}

// The less "expected" case of resetZoomLevel() - calling reset...()
// when already at the default value. Should be no-op but succeed.
IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ResetZoomLevelSucceedsIfDefaultLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  capture_session.RunGetDisplayMedia();
  WebContents* const captured_tab = capture_session.GetCapturedTab();
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), 100);

  // Reset works as expected.
  capture_session.UpdateZoomLevel("reset");

  // Check both the actual zoom level as well as the one reported to JS.
  EXPECT_EQ(GetZoomLevelPercentage(captured_tab), 100);
  EXPECT_EQ(capture_session.GetZoomLevel(), 100);
}

void CapturedSurfaceControlTest::RunChangingCapturedTabZoomChangeEventTest(
    double zoom_level_first_tab,
    double zoom_level_second_tab) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  WebContents* const first_captured_tab =
      capture_session.GetTab(CapturedTab::kInitiallyCapturedTab);
  SetZoomFactor(first_captured_tab, zoom_level_first_tab);

  WebContents* const second_captured_tab =
      capture_session.GetTab(CapturedTab::kOtherTab);
  SetZoomFactor(second_captured_tab, zoom_level_second_tab);

  capture_session.RunGetDisplayMedia();
  ASSERT_EQ(GetZoomLevelPercentage(first_captured_tab),
            100 * zoom_level_first_tab);
  ASSERT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 0);

  GetDelegate(second_captured_tab)->ShareThisTabInstead();
  capture_session.WaitForCaptureOf(CapturedTab::kOtherTab);
  ASSERT_EQ(capture_session.GetCapturedTab(), second_captured_tab);
  ASSERT_EQ(GetZoomLevelPercentage(second_captured_tab),
            100 * zoom_level_second_tab);

  const int expected_event_count =
      (zoom_level_first_tab != zoom_level_second_tab) ? 1 : 0;
  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(),
            expected_event_count);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ChangingCapturedTabIssuesEventIfDifferentZoomLevels) {
  SCOPED_TRACE("ChangingCapturedTabIssuesEventIfDifferentZoomLevels");
  RunChangingCapturedTabZoomChangeEventTest(0.5, 0.75);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ChangingCapturedTabDoesNotIssueEventIfSameZoomLevels) {
  SCOPED_TRACE("ChangingCapturedTabDoesNotIssueEventIfSameZoomLevels");
  RunChangingCapturedTabZoomChangeEventTest(0.5, 0.5);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ResetZoomLevelOnlyIssuesEventsWhenZoomLevelChanges) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");

  // Set the zoom factor to something other than the default before
  // capture starts.
  WebContents* const captured_tab =
      capture_session.GetTab(CapturedTab::kInitiallyCapturedTab);
  SetZoomFactor(captured_tab, 0.5);

  // Start the capture.
  capture_session.RunGetDisplayMedia();
  ASSERT_EQ(capture_session.GetCapturedTab(), captured_tab);
  ASSERT_EQ(GetZoomLevelPercentage(captured_tab), 50);
  ASSERT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 0);

  // Expectation #1 - the initial reset issues an event.
  capture_session.UpdateZoomLevel("reset");
  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 1);

  // Expectation #2 - additional resets don't issue an event.
  capture_session.UpdateZoomLevel("reset");
  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 0);

  // Expectation #3 - events still generally issued, they just
  // require actual change of zoom level. (Test is sane.)
  capture_session.UpdateZoomLevel("increase");
  capture_session.UpdateZoomLevel("reset");
  EXPECT_EQ(capture_session.GetZoomLevelChangeEventsSinceLast(), 2);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ChangeSourceTriggersUpdateCaptureTarget) {
  SCOPED_TRACE("ChangeSourceTriggersUpdateCaptureTarget");

  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.SetCapturedSurfaceControllerFactory();
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  EXPECT_EQ(capture_session.ForwardWheel("video"), "forward-wheel-resolved");

  // Expect that clicking "share this tab instead" will pipe a notification of
  // the change to the captured surface controller.
  capture_session.SetExpectUpdateCaptureTarget();
  GetDelegate(capture_session.other_tab())->ShareThisTabInstead();
  capture_session.WaitForCaptureOf(CapturedTab::kOtherTab);

  capture_session.VerifyAndClearExpectations();
}

void CapturedSurfaceControlTest::RunChangeSourceWorksOnCorrectCaptureSession(
    size_t session_experiencing_change) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session_0 =
      MakeCaptureSessionDetails("capture_session_0");
  capture_session_0.SetCapturedSurfaceControllerFactory();
  capture_session_0.RunGetDisplayMedia();
  capture_session_0.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);
  EXPECT_EQ(capture_session_0.ForwardWheel("video"), "forward-wheel-resolved");

  CaptureSessionDetails capture_session_1 =
      MakeCaptureSessionDetails("capture_session_1");
  capture_session_1.SetCapturedSurfaceControllerFactory();
  capture_session_1.RunGetDisplayMedia();
  capture_session_1.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);
  EXPECT_EQ(capture_session_1.ForwardWheel("video"), "forward-wheel-resolved");

  // Expect that clicking "share this tab instead" will pipe a notification of
  // the change to the correct CapturedSurfaceController.
  CHECK(session_experiencing_change == 0 || session_experiencing_change == 1);
  CaptureSessionDetails& capture_session_experiencing_change =
      (session_experiencing_change == 0) ? capture_session_0
                                         : capture_session_1;
  capture_session_experiencing_change.SetExpectUpdateCaptureTarget();
  GetDelegate(capture_session_experiencing_change.other_tab(),
              /*infobar_index=*/session_experiencing_change)
      ->ShareThisTabInstead();
  capture_session_experiencing_change.WaitForCaptureOf(CapturedTab::kOtherTab);

  capture_session_0.VerifyAndClearExpectations();
  capture_session_1.VerifyAndClearExpectations();
}

// Test when the first of two capture sessions experiences the source-change.
IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ChangeSourceWorksOnCorrectCaptureSession0) {
  SCOPED_TRACE("ChangeSourceWorksOnCorrectCaptureSession0");
  RunChangeSourceWorksOnCorrectCaptureSession(0);
}

// Test when the second of two capture sessions experiences the source-change.
IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ChangeSourceWorksOnCorrectCaptureSession1) {
  SCOPED_TRACE("ChangeSourceWorksOnCorrectCaptureSession1");
  RunChangeSourceWorksOnCorrectCaptureSession(1);
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ForwardWheelElementFailsIfNoPermission) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.SetCapturedSurfaceControllerFactory(
      blink::mojom::CapturedSurfaceControlResult::kNoPermissionError);
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  EXPECT_THAT(capture_session.ForwardWheel("video"),
              HasSubstr("NotAllowedError"));
}

IN_PROC_BROWSER_TEST_F(CapturedSurfaceControlTest,
                       ForwardWheelNullSucceedsWithoutPermission) {
  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.SetCapturedSurfaceControllerFactory(
      blink::mojom::CapturedSurfaceControlResult::kNoPermissionError);
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  EXPECT_EQ(capture_session.ForwardWheel("null"), "forward-wheel-resolved");
}

class CapturedSurfaceControlIndicatorTest
    : public CapturedSurfaceControlTest,
      public testing::WithParamInterface<CscAction> {
 public:
  CapturedSurfaceControlIndicatorTest() : action_(GetParam()) {}
  ~CapturedSurfaceControlIndicatorTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    CapturedSurfaceControlTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        switches::kAutoGrantCapturedSurfaceControlPrompt);
  }

 protected:
  const CscAction action_;
};

INSTANTIATE_TEST_SUITE_P(,
                         CapturedSurfaceControlIndicatorTest,
                         Values(CscAction::kForwardWheel,
                                CscAction::kForwardWheelNull,
                                CscAction::kIncreaseZoomLevel,
                                CscAction::kDecreaseZoomLevel,
                                CscAction::kResetZoomLevel,
                                CscAction::kGetZoomLevel,
                                CscAction::kGetSupportedZoomLevels));

IN_PROC_BROWSER_TEST_P(CapturedSurfaceControlIndicatorTest,
                       IndicatorNotShownBeforeApiInvocation) {
  SCOPED_TRACE("IndicatorNotShownBeforeApiInvocation");

  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  // The CSC indicator is not shown anywhere.
  EXPECT_FALSE(HasCscIndicator(capture_session.capturing_tab()));
  EXPECT_FALSE(HasCscIndicator(capture_session.initially_captured_tab()));
  EXPECT_FALSE(HasCscIndicator(capture_session.other_tab()));
}

IN_PROC_BROWSER_TEST_P(CapturedSurfaceControlIndicatorTest,
                       IndicatorShownAfterWriteAccessApiInvocation) {
  SCOPED_TRACE("IndicatorShownAfterWriteAccessApiInvocation");

  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  MakeValidApiCall(capture_session, action_);

  // The capturing tab's infobar shows the CSC indicator, but only
  // if the action was a write-access action.
  EXPECT_EQ(HasCscIndicator(capture_session.capturing_tab()),
            ShouldTriggerCscIndicator(action_));

  // The CSC indicator is not shown on any other infobar.
  EXPECT_FALSE(HasCscIndicator(capture_session.initially_captured_tab()));
  EXPECT_FALSE(HasCscIndicator(capture_session.other_tab()));
}

IN_PROC_BROWSER_TEST_P(
    CapturedSurfaceControlIndicatorTest,
    IndicatorStateRetainedAfterShareThisTabInsteadNoCscBefore) {
  SCOPED_TRACE("IndicatorStateRetainedAfterShareThisTabInsteadNoCscBefore");

  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  // Note absence of call to MakeValidApiCall() before share-this-tab-instead.
  GetDelegate(capture_session.other_tab())->ShareThisTabInstead();

  // The capturing tab's infobar does not show the CSC indicator because
  // a write-access CSC action was not invoked.
  EXPECT_FALSE(HasCscIndicator(capture_session.capturing_tab()));

  // The CSC indicator is not shown on any other infobar.
  EXPECT_FALSE(HasCscIndicator(capture_session.initially_captured_tab()));
  EXPECT_FALSE(HasCscIndicator(capture_session.other_tab()));
}

IN_PROC_BROWSER_TEST_P(
    CapturedSurfaceControlIndicatorTest,
    IndicatorStateRetainedAfterShareThisTabInsteadAfterCscAction) {
  SCOPED_TRACE("IndicatorStateRetainedAfterShareThisTabInsteadAfterCscAction");

  ASSERT_TRUE(embedded_test_server()->Start());

  CaptureSessionDetails capture_session =
      MakeCaptureSessionDetails("capture_session");
  capture_session.RunGetDisplayMedia();
  capture_session.ExpectCapturedTab(CapturedTab::kInitiallyCapturedTab);

  MakeValidApiCall(capture_session, action_);
  GetDelegate(capture_session.other_tab())->ShareThisTabInstead();

  // The capturing tab's infobar show the CSC indicator if the action
  // was a write-access action.
  EXPECT_EQ(HasCscIndicator(capture_session.capturing_tab()),
            ShouldTriggerCscIndicator(action_));

  // The CSC indicator is not shown on any other infobar.
  EXPECT_FALSE(HasCscIndicator(capture_session.initially_captured_tab()));
  EXPECT_FALSE(HasCscIndicator(capture_session.other_tab()));
}

class WebRtcScreenCaptureBrowserTestUserRejection
    : public WebRtcScreenCaptureBrowserTest,
      public testing::WithParamInterface<bool> {
 public:
  WebRtcScreenCaptureBrowserTestUserRejection()
      : prefer_current_tab_(GetParam()) {}
  ~WebRtcScreenCaptureBrowserTestUserRejection() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcScreenCaptureBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(prefer_current_tab_
                                   ? switches::kThisTabCaptureAutoReject
                                   : switches::kCaptureAutoReject);
  }

  bool PreferCurrentTab() const override { return prefer_current_tab_; }

 private:
  const bool prefer_current_tab_;
};

INSTANTIATE_TEST_SUITE_P(, WebRtcScreenCaptureBrowserTestUserRejection, Bool());

IN_PROC_BROWSER_TEST_P(WebRtcScreenCaptureBrowserTestUserRejection,
                       CorrectErrorReported) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenTestPageInNewTab(kCapturedPageMain);
  content::WebContents* capturing_tab = OpenTestPageInNewTab(kMainHtmlPage);

  RunGetDisplayMedia(
      capturing_tab,
      GetConstraints(
          /*video=*/true, /*audio=*/false),
      /*is_fake_ui=*/false,
      /*expect_success=*/false,
      /*is_tab_capture=*/true,
      /*expected_error=*/"NotAllowedError: Permission denied by user");
}
