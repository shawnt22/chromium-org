// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "headless/test/headless_protocol_browsertest.h"

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "content/public/common/content_switches.h"
#include "headless/lib/browser/headless_web_contents_impl.h"
#include "headless/public/switches.h"
#include "headless/test/headless_browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/network_switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "third_party/blink/public/common/switches.h"

namespace headless {

namespace switches {
static const char kResetResults[] = "reset-results";
static const char kDumpConsoleMessages[] = "dump-console-messages";
static const char kDumpDevToolsProtocol[] = "dump-devtools-protocol";
static const char kDumpTestResult[] = "dump-test-result";
}  // namespace switches

namespace {

static const base::FilePath kTestsDirectory(
    FILE_PATH_LITERAL("headless/test/data/protocol"));

// This is a very simple command line switches parser intended to process '--'
// separated switches with or without values. It will not process nested command
// line switches specifications like --js-flags=--expose-gc. Use with caution!
void AppendCommandLineExtras(base::CommandLine* command_line,
                             std::string_view extras) {
  std::vector<std::string> switches = base::SplitStringUsingSubstr(
      extras, "--", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (const auto& a_switch : switches) {
    if (size_t pos = a_switch.find('=', 1); pos != std::string::npos) {
      command_line->AppendSwitchASCII(a_switch.substr(0, pos),
                                      a_switch.substr(pos + 1));
    } else {
      command_line->AppendSwitch(a_switch);
    }
  }
}

}  // namespace

HeadlessProtocolBrowserTest::HeadlessProtocolBrowserTest() {
  embedded_test_server()->ServeFilesFromSourceDirectory(
      "third_party/blink/web_tests/http/tests/inspector-protocol");
  EXPECT_TRUE(embedded_test_server()->Start());
}

HeadlessProtocolBrowserTest::~HeadlessProtocolBrowserTest() = default;

void HeadlessProtocolBrowserTest::SetUpCommandLine(
    base::CommandLine* command_line) {
  command_line->AppendSwitchASCII(::network::switches::kHostResolverRules,
                                  "MAP *.test 127.0.0.1");
  HeadlessDevTooledBrowserTest::SetUpCommandLine(command_line);
}

base::Value::Dict HeadlessProtocolBrowserTest::GetPageUrlExtraParams() {
  return base::Value::Dict();
}

void HeadlessProtocolBrowserTest::RunDevTooledTest() {
  scoped_refptr<content::DevToolsAgentHost> agent_host =
      content::DevToolsAgentHost::GetOrCreateFor(
          HeadlessWebContentsImpl::From(web_contents_)->web_contents());

  // Set up Page domain.
  devtools_client_.AddEventHandler(
      "Page.loadEventFired",
      base::BindRepeating(&HeadlessProtocolBrowserTest::OnLoadEventFired,
                          base::Unretained(this)));
  devtools_client_.SendCommand("Page.enable");

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDumpConsoleMessages)) {
    // Set up Runtime domain to intercept console messages.
    devtools_client_.AddEventHandler(
        "Runtime.consoleAPICalled",
        base::BindRepeating(&HeadlessProtocolBrowserTest::OnConsoleAPICalled,
                            base::Unretained(this)));
    devtools_client_.SendCommand("Runtime.enable");
  }

  // Expose DevTools protocol to the target.
  browser_devtools_client_.SendCommand(
      "Target.exposeDevToolsProtocol", Param("targetId", agent_host->GetId()),
      base::BindOnce(&HeadlessProtocolBrowserTest::OnceSetUp,
                     base::Unretained(this)));
}

void HeadlessProtocolBrowserTest::OnceSetUp(base::Value::Dict) {
  // Navigate to test harness page
  GURL page_url = embedded_test_server()->GetURL(
      "harness.test", "/protocol/inspector-protocol-test.html");
  devtools_client_.SendCommand("Page.navigate", Param("url", page_url.spec()));
}

void HeadlessProtocolBrowserTest::OnLoadEventFired(
    const base::Value::Dict& params) {
  ASSERT_THAT(params, DictHasValue("method", "Page.loadEventFired"));

  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath src_dir;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_dir));
  base::FilePath test_path =
      src_dir.Append(kTestsDirectory).AppendASCII(script_name_);
  std::string script;
  if (!base::ReadFileToString(test_path, &script)) {
    ADD_FAILURE() << "Unable to read test at " << test_path;
    FinishTest();
    return;
  }
  GURL test_url = embedded_test_server()->GetURL("harness.test",
                                                 "/protocol/" + script_name_);
  GURL target_url =
      embedded_test_server()->GetURL("127.0.0.1", "/protocol/" + script_name_);

  base::Value::Dict test_params;
  test_params.Set("test", test_url.spec());
  test_params.Set("target", target_url.spec());
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDumpDevToolsProtocol)) {
    test_params.Set("dumpDevToolsProtocol", true);
  }
  test_params.Merge(GetPageUrlExtraParams());

  std::string json_test_params;
  base::JSONWriter::Write(test_params, &json_test_params);
  std::string evaluate_script = "runTest(" + json_test_params + ")";

  base::Value::Dict evaluate_params;
  evaluate_params.Set("expression", evaluate_script);
  evaluate_params.Set("awaitPromise", true);
  evaluate_params.Set("returnByValue", true);
  devtools_client_.SendCommand(
      "Runtime.evaluate", std::move(evaluate_params),
      base::BindOnce(&HeadlessProtocolBrowserTest::OnEvaluateResult,
                     base::Unretained(this)));
}

void HeadlessProtocolBrowserTest::OnEvaluateResult(base::Value::Dict params) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDumpTestResult)) {
    LOG(ERROR) << "Test result:\n" << params.DebugString();
  }

  ProcessTestResult(DictString(params, "result.result.value"));

  FinishTest();
}

void HeadlessProtocolBrowserTest::ProcessTestResult(
    const std::string& test_result) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  base::FilePath src_dir;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_dir));
  base::FilePath expectation_path =
      src_dir.Append(kTestsDirectory)
          .AppendASCII(script_name_.substr(0, script_name_.length() - 3) +
                       "-expected.txt");

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kResetResults)) {
    LOG(INFO) << "Updating expectations at " << expectation_path;
    bool succcess = base::WriteFile(expectation_path, test_result);
    CHECK(succcess);
  }

  std::string expectation;
  if (!base::ReadFileToString(expectation_path, &expectation)) {
    ADD_FAILURE() << "Unable to read expectations at " << expectation_path;
  }

  EXPECT_EQ(expectation, test_result);
}

void HeadlessProtocolBrowserTest::OnConsoleAPICalled(
    const base::Value::Dict& params) {
  ASSERT_THAT(params, DictHasValue("method", "Runtime.consoleAPICalled"));

  const base::Value::List* args = params.FindListByDottedPath("params.args");
  if (!args || args->empty())
    return;

  const base::Value* value = args->front().GetDict().Find("value");
  switch (value->type()) {
    case base::Value::Type::NONE:
    case base::Value::Type::BOOLEAN:
    case base::Value::Type::INTEGER:
    case base::Value::Type::DOUBLE:
    case base::Value::Type::STRING:
      LOG(INFO) << value->DebugString();
      return;
    default:
      LOG(INFO) << "Unhandled value type: " << value->type();
      return;
  }
}

void HeadlessProtocolBrowserTest::FinishTest() {
  test_finished_ = true;
  FinishAsynchronousTest();
}

#define HEADLESS_PROTOCOL_TEST_CLASS(CLASS_NAME, TEST_NAME, SCRIPT_NAME) \
  IN_PROC_BROWSER_TEST_F(CLASS_NAME, TEST_NAME) {                        \
    test_folder_ = "/protocol/";                                         \
    script_name_ = SCRIPT_NAME;                                          \
    RunTest();                                                           \
  }

#define HEADLESS_PROTOCOL_TEST(TEST_NAME, SCRIPT_NAME)                 \
  HEADLESS_PROTOCOL_TEST_CLASS(HeadlessProtocolBrowserTest, TEST_NAME, \
                               SCRIPT_NAME)

#define HEADLESS_PROTOCOL_TEST_P(CLASS_NAME, TEST_NAME, SCRIPT_NAME) \
  IN_PROC_BROWSER_TEST_P(CLASS_NAME, TEST_NAME) {                    \
    test_folder_ = "/protocol/";                                     \
    script_name_ = SCRIPT_NAME;                                      \
    RunTest();                                                       \
  }

// Headless-specific tests
HEADLESS_PROTOCOL_TEST(VirtualTimeBasics, "emulation/virtual-time-basics.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeInterrupt,
                       "emulation/virtual-time-interrupt.js")

// Flaky on Linux, Mac & Win. TODO(crbug.com/41440558): Re-enable.
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN) || BUILDFLAG(IS_FUCHSIA)
#define MAYBE_VirtualTimeCrossProcessNavigation \
  DISABLED_VirtualTimeCrossProcessNavigation
#else
#define MAYBE_VirtualTimeCrossProcessNavigation \
  VirtualTimeCrossProcessNavigation
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_VirtualTimeCrossProcessNavigation,
                       "emulation/virtual-time-cross-process-navigation.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeDetachFrame,
                       "emulation/virtual-time-detach-frame.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeNoBlock404, "emulation/virtual-time-404.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeLocalStorage,
                       "emulation/virtual-time-local-storage.js")
HEADLESS_PROTOCOL_TEST(VirtualTimePendingScript,
                       "emulation/virtual-time-pending-script.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeRedirect,
                       "emulation/virtual-time-redirect.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeSessionStorage,
                       "emulation/virtual-time-session-storage.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeStarvation,
                       "emulation/virtual-time-starvation.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeVideo, "emulation/virtual-time-video.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeErrorLoop,
                       "emulation/virtual-time-error-loop.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeFetchStream,
                       "emulation/virtual-time-fetch-stream.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeFetchReadBody,
                       "emulation/virtual-time-fetch-read-body.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeFetchBlobReadBodyBlob,
                       "emulation/virtual-time-fetch-read-body-blob.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeDialogWhileLoading,
                       "emulation/virtual-time-dialog-while-loading.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeHistoryNavigation,
                       "emulation/virtual-time-history-navigation.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeHistoryNavigationSameDoc,
                       "emulation/virtual-time-history-navigation-same-doc.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeSVG, "emulation/virtual-time-svg.js")

// Flaky on Mac. TODO(crbug.com/352304682): Re-enable.
#if BUILDFLAG(IS_MAC)
#define MAYBE_VirtualTimeWorkerBasic DISABLED_VirtualTimeWorkerBasic
#else
#define MAYBE_VirtualTimeWorkerBasic VirtualTimeWorkerBasic
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_VirtualTimeWorkerBasic,
                       "emulation/virtual-time-worker-basic.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeWorkerLockstep,
                       "emulation/virtual-time-worker-lockstep.js")

// Flaky on Mac. TODO(crbug.com/352304682): Re-enable.
#if BUILDFLAG(IS_MAC)
#define MAYBE_VirtualTimeWorkerFetch DISABLED_VirtualTimeWorkerFetch
#else
#define MAYBE_VirtualTimeWorkerFetch VirtualTimeWorkerFetch
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_VirtualTimeWorkerFetch,
                       "emulation/virtual-time-worker-fetch.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeWorkerTerminate,
                       "emulation/virtual-time-worker-terminate.js")

HEADLESS_PROTOCOL_TEST(VirtualTimeFetchKeepalive,
                       "emulation/virtual-time-fetch-keepalive.js")
HEADLESS_PROTOCOL_TEST(VirtualTimeDisposeWhileRunning,
                       "emulation/virtual-time-dispose-while-running.js")
HEADLESS_PROTOCOL_TEST(VirtualTimePausesDocumentLoading,
                       "emulation/virtual-time-pauses-document-loading.js")

HEADLESS_PROTOCOL_TEST(PageBeforeUnload, "page/page-before-unload.js")

// http://crbug.com/633321
#if BUILDFLAG(IS_ANDROID)
#define MAYBE_VirtualTimeTimerOrder DISABLED_VirtualTimeTimerOrder
#define MAYBE_VirtualTimeTimerSuspend DISABLED_VirtualTimeTimerSuspend
#else
#define MAYBE_VirtualTimeTimerOrder VirtualTimeTimerOrder
#define MAYBE_VirtualTimeTimerSuspend VirtualTimeTimerSuspend
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_VirtualTimeTimerOrder,
                       "emulation/virtual-time-timer-order.js")
HEADLESS_PROTOCOL_TEST(MAYBE_VirtualTimeTimerSuspend,
                       "emulation/virtual-time-timer-suspended.js")
#undef MAYBE_VirtualTimeTimerOrder
#undef MAYBE_VirtualTimeTimerSuspend

HEADLESS_PROTOCOL_TEST(Geolocation, "emulation/geolocation-crash.js")

HEADLESS_PROTOCOL_TEST(DragStarted, "input/dragIntercepted.js")

// https://crbug.com/1414190
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
#define MAYBE_InputClipboardOps DISABLED_InputClipboardOps
#else
#define MAYBE_InputClipboardOps InputClipboardOps
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_InputClipboardOps, "input/input-clipboard-ops.js")

HEADLESS_PROTOCOL_TEST(ClipboardApiCopyPaste,
                       "input/clipboard-api-copy-paste.js")

HEADLESS_PROTOCOL_TEST(FocusBlurNotifications,
                       "input/focus-blur-notifications.js")

HEADLESS_PROTOCOL_TEST(HeadlessSessionBasicsTest,
                       "sessions/headless-session-basics.js")

HEADLESS_PROTOCOL_TEST(HeadlessSessionCreateContextDisposeOnDetach,
                       "sessions/headless-createContext-disposeOnDetach.js")

HEADLESS_PROTOCOL_TEST(BrowserSetInitialProxyConfig,
                       "sanity/browser-set-initial-proxy-config.js")

HEADLESS_PROTOCOL_TEST(BrowserUniversalNetworkAccess,
                       "sanity/universal-network-access.js")

HEADLESS_PROTOCOL_TEST(ShowDirectoryPickerNoCrash,
                       "sanity/show-directory-picker-no-crash.js")

HEADLESS_PROTOCOL_TEST(ShowFilePickerInterception,
                       "sanity/show-file-picker-interception.js")

// The `change-window-*.js` tests cover DevTools methods, while `window-*.js`
// cover `window.*` JS APIs.
HEADLESS_PROTOCOL_TEST(ChangeWindowSize, "sanity/change-window-size.js")
HEADLESS_PROTOCOL_TEST(ChangeWindowState, "sanity/change-window-state.js")
HEADLESS_PROTOCOL_TEST(HiddenTargetCreate, "sanity/hidden-target-create.js")
HEADLESS_PROTOCOL_TEST(HiddenTargetClose, "sanity/hidden-target-close.js")
HEADLESS_PROTOCOL_TEST(HiddenTargetCreateInvalidParams,
                       "sanity/hidden-target-create-invalid-params.js")
HEADLESS_PROTOCOL_TEST(HiddenTargetPageEnable,
                       "sanity/hidden-target-page-enable.js")
HEADLESS_PROTOCOL_TEST(WindowOuterSize, "sanity/window-outer-size.js")
HEADLESS_PROTOCOL_TEST(WindowResizeTo, "sanity/window-resize-to.js")

// https://crbug.com/378531862
#if BUILDFLAG(IS_MAC)
#define MAYBE_CreateTargetPosition DISABLED_CreateTargetPosition
#else
#define MAYBE_CreateTargetPosition CreateTargetPosition
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_CreateTargetPosition,
                       "sanity/create-target-position.js")

HEADLESS_PROTOCOL_TEST(WindowSizeOnStart, "sanity/window-size-on-start.js")

HEADLESS_PROTOCOL_TEST(LargeBrowserWindowSize,
                       "sanity/large-browser-window-size.js")

HEADLESS_PROTOCOL_TEST(ScreencastBasics, "sanity/screencast-basics.js")
HEADLESS_PROTOCOL_TEST(ScreencastViewport, "sanity/screencast-viewport.js")

HEADLESS_PROTOCOL_TEST(GrantPermissions, "sanity/grant_permissions.js")

#if !defined(HEADLESS_USE_EMBEDDED_RESOURCES)
HEADLESS_PROTOCOL_TEST(AutoHyphenation, "sanity/auto-hyphenation.js")
#endif

// Web Bluetooth is still experimental on Linux.
#if !BUILDFLAG(IS_LINUX)
HEADLESS_PROTOCOL_TEST(Bluetooth, "emulation/bluetooth.js")
#endif

class HeadlessProtocolBrowserTestWithKnownPermission
    : public HeadlessProtocolBrowserTest {
 public:
  HeadlessProtocolBrowserTestWithKnownPermission() = default;

 protected:
  base::Value::Dict GetPageUrlExtraParams() override {
    base::Value::List permissions;
    const std::vector<blink::PermissionType>& types =
        blink::GetAllPermissionTypes();
    for (blink::PermissionType type : types) {
      std::string permission = blink::GetPermissionString(type);
      NormalizePermissionName(permission);
      permissions.Append(permission);
    }

    base::Value::Dict dict;
    dict.Set("permissions", std::move(permissions));
    return dict;
  }

  static void NormalizePermissionName(std::string& permission) {
    if (IsAllAsciiUpper(permission)) {
      permission = base::ToLowerASCII(permission);
    } else {
      permission[0] = base::ToLowerASCII(permission[0]);
    }

    // Handle known exceptions.
    if (permission == "midiSysEx") {
      permission = "midiSysex";
    }
  }

  static bool IsAllAsciiUpper(const std::string& permission) {
    for (char ch : permission) {
      if (!base::IsAsciiUpper(ch)) {
        return false;
      }
    }
    return true;
  }
};

HEADLESS_PROTOCOL_TEST_CLASS(HeadlessProtocolBrowserTestWithKnownPermission,
                             KnownPermissionTypes,
                             "sanity/known-permission-types.js")

class HeadlessProtocolBrowserTestWithProxy
    : public HeadlessProtocolBrowserTest {
 public:
  HeadlessProtocolBrowserTestWithProxy()
      : proxy_server_(net::EmbeddedTestServer::TYPE_HTTP) {
    proxy_server_.AddDefaultHandlers(
        base::FilePath(FILE_PATH_LITERAL("headless/test/data")));
  }

  void SetUp() override {
    ASSERT_TRUE(proxy_server_.Start());
    HeadlessProtocolBrowserTest::SetUp();
  }

  void TearDown() override {
    EXPECT_TRUE(proxy_server_.ShutdownAndWaitUntilComplete());
    HeadlessProtocolBrowserTest::TearDown();
  }

  net::EmbeddedTestServer* proxy_server() { return &proxy_server_; }

 protected:
  base::Value::Dict GetPageUrlExtraParams() override {
    std::string proxy = proxy_server()->host_port_pair().ToString();
    base::Value::Dict dict;
    dict.Set("proxy", proxy);
    return dict;
  }

 private:
  net::EmbeddedTestServer proxy_server_;
};

HEADLESS_PROTOCOL_TEST_CLASS(HeadlessProtocolBrowserTestWithProxy,
                             BrowserSetProxyConfig,
                             "sanity/browser-set-proxy-config.js")

class HeadlessAllowedVideoCodecsTest
    : public HeadlessDevTooledBrowserTest,
      public testing::WithParamInterface<
          std::tuple<std::string, std::string, bool>> {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII("allow-video-codecs", allowlist());
  }

  void RunDevTooledTest() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    SendCommandSync(devtools_client_, "Page.enable");
    devtools_client_.AddEventHandler(
        "Page.loadEventFired",
        base::BindRepeating(&HeadlessAllowedVideoCodecsTest::OnLoadEventFired,
                            base::Unretained(this)));
    devtools_client_.SendCommand(
        "Page.navigate",
        Param("url", embedded_test_server()->GetURL("/hello.html").spec()));
  }

  void OnLoadEventFired(const base::Value::Dict& params) {
    base::Value::Dict eval_params;
    eval_params.Set("returnByValue", true);
    eval_params.Set("awaitPromise", true);
    eval_params.Set("expression", base::StringPrintf(R"(
      VideoDecoder.isConfigSupported({codec: "%s"})
          .then(result => result.supported)
    )",
                                                     codec_name().c_str()));
    base::Value::Dict result = SendCommandSync(
        devtools_client_, "Runtime.evaluate", std::move(eval_params));
    EXPECT_THAT(result.FindBoolByDottedPath("result.result.value"),
                testing::Optional(is_codec_enabled()));
    FinishAsynchronousTest();
  }

  const std::string& allowlist() const { return std::get<0>(GetParam()); }
  const std::string& codec_name() const { return std::get<1>(GetParam()); }
  bool is_codec_enabled() const { return std::get<2>(GetParam()); }
};

constexpr bool have_proprietary_codecs =
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
    true;
#else
    false;
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)

INSTANTIATE_TEST_SUITE_P(
    All,
    HeadlessAllowedVideoCodecsTest,
    testing::Values(
        std::make_tuple("av1,-*", "av01.0.04M.08", true),
        std::make_tuple("-av1,*", "av01.0.04M.08", false),
        std::make_tuple("*", "avc1.64000b", have_proprietary_codecs)));

HEADLESS_DEVTOOLED_TEST_P(HeadlessAllowedVideoCodecsTest);

class PopupWindowOpenTest : public HeadlessProtocolBrowserTest,
                            public testing::WithParamInterface<bool> {
 protected:
  PopupWindowOpenTest() = default;

  void CustomizeHeadlessBrowserContext(
      HeadlessBrowserContext::Builder& builder) override {
    builder.SetBlockNewWebContents(ShouldBlockNewWebContents());
  }

  base::Value::Dict GetPageUrlExtraParams() override {
    base::Value::Dict params;
    params.Set("blockingNewWebContents", ShouldBlockNewWebContents());
    return params;
  }

  bool ShouldBlockNewWebContents() const { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(/* no prefix */,
                         PopupWindowOpenTest,
                         ::testing::Bool());

HEADLESS_PROTOCOL_TEST_P(PopupWindowOpenTest,
                         Open,
                         "sanity/popup-window-open.js")

class HeadlessProtocolBrowserTestWithoutSiteIsolation
    : public HeadlessProtocolBrowserTest {
 public:
  HeadlessProtocolBrowserTestWithoutSiteIsolation() = default;

 protected:
  bool ShouldEnableSitePerProcess() override { return false; }
};

HEADLESS_PROTOCOL_TEST_CLASS(
    HeadlessProtocolBrowserTestWithoutSiteIsolation,
    VirtualTimeLocalStorageDetachedFrame,
    "emulation/virtual-time-local-storage-detached-frame.js")

class HeadlessProtocolBrowserTestWithDataPath
    : public HeadlessProtocolBrowserTest {
 protected:
  base::Value::Dict GetPageUrlExtraParams() override {
    base::FilePath src_dir;
    CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_dir));
    base::FilePath path =
        src_dir.Append(kTestsDirectory).AppendASCII(data_path_);
    base::Value::Dict dict;
    dict.Set("data_path", path.AsUTF8Unsafe());
    return dict;
  }

  std::string data_path_;
};

#define HEADLESS_PROTOCOL_TEST_WITH_DATA_PATH(TEST_NAME, SCRIPT_NAME, PATH)    \
  IN_PROC_BROWSER_TEST_F(HeadlessProtocolBrowserTestWithDataPath, TEST_NAME) { \
    test_folder_ = "/protocol/";                                               \
    script_name_ = SCRIPT_NAME;                                                \
    data_path_ = PATH;                                                         \
    RunTest();                                                                 \
  }

// TODO(crbug.com/40883155)  Re-enable after resolving flaky failures.
HEADLESS_PROTOCOL_TEST_WITH_DATA_PATH(
    FileInputDirectoryUpload,
    "sanity/file-input-directory-upload.js",
    "sanity/resources/file-input-directory-upload")

class HeadlessProtocolBrowserTestWithExposeGC
    : public HeadlessProtocolBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    HeadlessProtocolBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(blink::switches::kJavaScriptFlags,
                                    "--expose-gc");
  }
};

HEADLESS_PROTOCOL_TEST_CLASS(HeadlessProtocolBrowserTestWithExposeGC,
                             GetDOMCountersForLeakDetection,
                             "sanity/get-dom-counters-for-leak-detection.js")

class HeadlessProtocolBrowserTestSitePerProcess
    : public HeadlessProtocolBrowserTest,
      public testing::WithParamInterface<bool> {
 public:
  bool ShouldEnableSitePerProcess() override { return GetParam(); }

  base::Value::Dict GetPageUrlExtraParams() override {
    base::Value::Dict params;
    params.Set("sitePerProcessEnabled", ShouldEnableSitePerProcess());
    return params;
  }
};

INSTANTIATE_TEST_SUITE_P(/* no prefix */,
                         HeadlessProtocolBrowserTestSitePerProcess,
                         ::testing::Bool());

HEADLESS_PROTOCOL_TEST_P(HeadlessProtocolBrowserTestSitePerProcess,
                         SitePerProcess,
                         "sanity/site-per-process.js")

HEADLESS_PROTOCOL_TEST(DataURIIframe, "sanity/data-uri-iframe.js")

// The test brlow requires beginFrameControl which is currently not supported
// on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_IOCommandAfterInput DISABLED_IOCommandAfterInput
#else
#define MAYBE_IOCommandAfterInput IOCommandAfterInput
#endif
HEADLESS_PROTOCOL_TEST(MAYBE_IOCommandAfterInput,
                       "input/io-command-after-input.js")

#define HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(              \
    TEST_NAME, SCRIPT_NAME, COMMAND_LINE_EXTRAS)                      \
                                                                      \
  class HeadlessProtocolBrowserTestWithCommandLineExtras_##TEST_NAME  \
      : public HeadlessProtocolBrowserTest {                          \
   public:                                                            \
    void SetUpCommandLine(base::CommandLine* command_line) override { \
      HeadlessProtocolBrowserTest::SetUpCommandLine(command_line);    \
      AppendCommandLineExtras(command_line, COMMAND_LINE_EXTRAS);     \
    }                                                                 \
  };                                                                  \
                                                                      \
  IN_PROC_BROWSER_TEST_F(                                             \
      HeadlessProtocolBrowserTestWithCommandLineExtras_##TEST_NAME,   \
      TEST_NAME) {                                                    \
    test_folder_ = "/protocol/";                                      \
    script_name_ = SCRIPT_NAME;                                       \
    RunTest();                                                        \
  }

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenScaleFactor,
    "sanity/screen-scale-factor.js",
    "--screen-info={devicePixelRatio=3.0}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenWorkArea,
    "sanity/screen-work-area.js",
    "--screen-info={ workAreaLeft=100 workAreaRight=100"
    " workAreaTop=100 workAreaBottom=100 }")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenSizeOrientation,
    "sanity/screen-size-orientation.js",
    "--screen-info={600x800}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenRotationAngle,
    "sanity/screen-rotation-angle.js",
    "--screen-info={rotation=180}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenOrientationLockNaturalLandscape,
    "sanity/screen-orientation-lock-natural-landscape.js",
    "--screen-info={800x600}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenOrientationLockNaturalPortrait,
    "sanity/screen-orientation-lock-natural-portrait.js",
    "--screen-info={600x800}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenDetailsMultipleScreens,
    "sanity/screen-details-multiple-screens.js",
    "--screen-info={ label='1st screen' }{ 600x800 label='2nd screen' }")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenDetailsPixelRatioAndColorDepth,
    "sanity/screen-details-pixel-ratio-and-color-depth.js",
    "--screen-info={ label='Screen' devicePixelRatio=3.0 colorDepth=32 }")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    RequestFullscreen,
    "sanity/request-fullscreen.js",
    "--screen-info={ 800x600 } --window-size=400,200")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowOpenOnSecondaryScreen,
    "sanity/window-open-on-secondary-screen.js",
    "--screen-info={ label='1st screen' }{ label='2nd screen' }")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    RequestFullscreenOnSecondaryScreen,
    "sanity/request-fullscreen-on-secondary-screen.js",
    "--screen-info={ label='1st screen' }{ 600x800 label='2nd screen' }")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    ScreenRotationSecondaryScreen,
    "sanity/screen-rotation-secondary-screen.js",
    "--screen-info={ label='1st screen' }{ 600x800 label='2nd screen' }")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    MoveWindowBetweenScreens,
    "sanity/move-window-between-screens.js",
    "--screen-info={label='#1'}{label='#2'}{0,600 label='#3'}{label='#4'}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    CreateTargetSecondaryScreen,
    "sanity/create-target-secondary-screen.js",
    "--screen-info={label='#1'}{label='#2'}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    CreateTargetWindowState,
    "sanity/create-target-window-state.js",
    "--screen-info={1600x1200}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    MultipleScreenDetails,
    "sanity/multiple-screen-details.js",
    "--screen-info={label='#1'}{600x800 label='#2'}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowOpenPopupPlacement,
    "sanity/window-open-popup-placement.js",
    "--screen-info={1600x1200}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowSizeSwitchHandling,
    "sanity/window-size-switch-handling.js",
    "--screen-info={1600x1200} --window-size=700,500")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowSizeSwitchLargerThanScreen,
    "sanity/window-size-switch-larger-than-screen.js",
    "--screen-info={800x600} --window-size=1600,1200")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowScreenAvail,
    "sanity/window-screen-avail.js",
    "--screen-info={800x600"
    " workAreaLeft=10 workAreaRight=90"
    " workAreaTop=20 workAreaBottom=80}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowStateTransitions,
    "sanity/window-state-transitions.js",
    "--screen-info={1600x1200}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowZoomOnSecondaryScreen,
    "sanity/window-zoom-on-secondary-screen.js",
    "--screen-info={1600x1200}{1200x1600}")

HEADLESS_PROTOCOL_TEST_WITH_COMMAND_LINE_EXTRAS(
    WindowZoomSizeMatchesWorkArea,
    "sanity/window-zoom-size-matches-work-area.js",
    "--screen-info={800x600 "
    " workAreaLeft=10 workAreaRight=90"
    " workAreaTop=20 workAreaBottom=80}")

}  // namespace headless
