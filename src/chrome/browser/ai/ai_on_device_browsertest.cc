// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/embedder_support/switches.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/default_handlers.h"
#include "third_party/blink/public/common/features_generated.h"

namespace {

// This is the public key of tools/origin_trials/eftest.key, used to validate
// origin trial tokens generated by tools/origin_trials/generate_token.py.
// https://chromium.googlesource.com/chromium/src/+/main/docs/origin_trials_integration.md
constexpr char kOriginTrialPublicKeyForTesting[] =
    "dRCs+TocuKkocNKa0AtZ4awrt9XKH2SQCI6o4FY6BNA=";

// Origin trial tokens (expire on 2033-08-06) generated by
// tools/origin_trials/generate_token.py https://a.test:32123 AIFooAPI \
//  --expire-days 3000
constexpr char kAIRewriterAPIOTToken[] =
    "A7gvtQAwPhmBOadB9rGCwqWwgmba7wU+zXqjfDR9cfTzR8Xi2Tkedxawd/"
    "PMg4SLjABtNGJZf3Iel4zqG/"
    "iqZQ8AAABUeyJvcmlnaW4iOiAiaHR0cHM6Ly9hLnRlc3Q6MzIxMjMiLCAiZmVhdHVyZSI6ICJB"
    "SVJld3JpdGVyQVBJIiwgImV4cGlyeSI6IDIwMDY5NzA3NDF9";
constexpr char kAIWriterAPIOTToken[] =
    "A0jJGgLmqGgNaHNH7my4hKMTvp7oBOvGoLvZhH3tzAGKY3SNkmSQCSTxFtgXNGxloQ7rFqxaut"
    "85MKQRKEug+"
    "Q4AAABSeyJvcmlnaW4iOiAiaHR0cHM6Ly9hLnRlc3Q6MzIxMjMiLCAiZmVhdHVyZSI6ICJBSVd"
    "yaXRlckFQSSIsICJleHBpcnkiOiAyMDA2OTcwNjU4fQ==";

// Execute script on the current Window and yield the posted message.
constexpr char kRunWindowCheck[] = R"JS(
    new Promise(r => { self.onmessage = e => { r(e.data); }; %s });
    )JS";

// Execute script on a new Worker and yield the posted message.
constexpr char kRunWorkerCheck[] = R"JS(
    const workerScript = `%s`;
    const blob = new Blob([workerScript], { type: 'text/javascript' });
    const worker = new Worker(URL.createObjectURL(blob));
    new Promise(r => { worker.onmessage = e => { r(e.data); }});
    )JS";

// Check if a global identifier is exposed and post an OK/error message.
constexpr char kCheckExposed[] = R"JS(
    try { %s; self.postMessage('OK');
    } catch (e) { self.postMessage(e.name); }
    )JS";

// Check if FooAPI.availability() yields a string and post an OK/error message.
constexpr char kCheckAvailability[] = R"JS(
    try { %s.availability().then(a => {
              self.postMessage(typeof(a) == 'string' ? 'OK' : 'NO'); });
    } catch (e) { self.postMessage(e.name); }
    )JS";

// The boolean tuple describing:
// 1. if the `kAIFooAPI` chrome://flag entries are explicitly enabled;
// 2. if the `kAIFooAPIForWorkers` are explicitly enabled;
// 3. if the `kAIFooAPI` kill switches are triggered;
// 4. if the `kAIFooAPI` OT tokens are supplied (for any APIs in OT).
using Variant = std::tuple<bool, bool, bool, bool>;
bool IsAPIFlagEnabled(Variant v) {
  return std::get<0>(v);
}
bool IsAPIWorkerFlagEnabled(Variant v) {
  return std::get<1>(v);
}
bool IsAPIKillSwitchTriggered(Variant v) {
  return std::get<2>(v);
}
bool IsOTTokenSupplied(Variant v) {
  return std::get<3>(v);
}

// Describes the test variants in a meaningful way in the parameterized tests.
std::string DescribeTestVariant(const testing::TestParamInfo<Variant> info) {
  std::string api_flag = IsAPIFlagEnabled(info.param) ? "FlagEnabledByUser"
                                                      : "FlagNotEnabledByUser";
  std::string worker_flag =
      IsAPIWorkerFlagEnabled(info.param) ? "WithWorkerFlag" : "NoWorkerFlag";
  std::string kill_switch = IsAPIKillSwitchTriggered(info.param)
                                ? "WithAPIKillswitch"
                                : "NoAPIKillswitch";
  std::string ot_token =
      IsOTTokenSupplied(info.param) ? "WithOTToken" : "NoOTToken";
  return base::JoinString({api_flag, worker_flag, kill_switch, ot_token}, "_");
}

// Get the names of all the APIs tested in this suite.
std::vector<std::string> GetAPINames() {
  return {"LanguageModel", "Rewriter", "Summarizer", "Writer"};
}

// Returns whether the API is enabled by default.
bool IsAPIEnabledByDefault(std::string_view name) {
  return name == "Summarizer";
}

// Returns whether the API name matches those currently in origin trial.
bool IsAPIInOT(std::string_view name) {
  return name == "Rewriter" || name == "Writer";
}

// Injects an Origin Trial `token` into the page.
void InjectOTToken(content::WebContents* tab, std::string_view token) {
  static constexpr char kScript[] =
      R"JS(
        const meta = document.createElement('meta');
        meta.httpEquiv = 'origin-trial';
        meta.content = '%s';
        document.head.appendChild(meta);
      )JS";
  EXPECT_TRUE(ExecJs(tab, base::StringPrintf(kScript, token)));
}

// TODO(crbug.com/419321441): Support Built-In AI APIs on ChromeOS.
#if BUILDFLAG(IS_CHROMEOS)
#define MAYBE_AIOnDeviceBrowserTest DISABLED_AIOnDeviceBrowserTest
#else
#define MAYBE_AIOnDeviceBrowserTest AIOnDeviceBrowserTest
#endif  // BUILDFLAG(IS_CHROMEOS)
class MAYBE_AIOnDeviceBrowserTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<Variant> {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    if (IsAPIFlagEnabled(GetParam())) {
      command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                      "AIPromptAPI,AIRewriterAPI,"
                                      "AISummarizationAPI,AIWriterAPI");
    }
    if (IsAPIWorkerFlagEnabled(GetParam())) {
      command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                      "AIPromptAPIForWorkers,"
                                      "AIRewriterAPIForWorkers,"
                                      "AISummarizationAPIForWorkers,"
                                      "AIWriterAPIForWorkers");
    }
    // Specify the OT test public key to make the test token effective.
    command_line->AppendSwitchASCII(embedder_support::kOriginTrialPublicKey,
                                    kOriginTrialPublicKeyForTesting);
    if (IsAPIKillSwitchTriggered(GetParam())) {
      base::flat_map<base::test::FeatureRef, bool> feature_states;
      feature_states[blink::features::kAIPromptAPI] = false;
      feature_states[blink::features::kAIRewriterAPI] = false;
      feature_states[blink::features::kAISummarizationAPI] = false;
      feature_states[blink::features::kAIWriterAPI] = false;
      feature_list_.InitWithFeatureStates(feature_states);
    }
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_https_test_server().SetSSLConfig(
        net::EmbeddedTestServer::CERT_TEST_NAMES);
    net::test_server::RegisterDefaultHandlers(&embedded_https_test_server());
    // Specify a port to match the generated test OT tokens.
    // TODO(421053094): Remove port and move to browser_tests target after OTs.
    ASSERT_TRUE(embedded_https_test_server().Start(/*port=*/32123));

    auto* tab = browser()->tab_strip_model()->GetActiveWebContents();
    GURL url(embedded_https_test_server().GetURL("a.test", "/empty.html"));
    ASSERT_TRUE(NavigateToURL(tab, url));

    if (IsOTTokenSupplied(GetParam())) {
      InjectOTToken(tab, kAIRewriterAPIOTToken);
      InjectOTToken(tab, kAIWriterAPIOTToken);
    }
  }

  bool ExpectExposedToWindow(std::string_view name) const {
    return IsAPIFlagEnabled(GetParam()) ||
           ((IsAPIEnabledByDefault(name) ||
             (IsAPIInOT(name) && IsOTTokenSupplied(GetParam()))) &&
            !IsAPIKillSwitchTriggered(GetParam()));
  }

  bool ExpectExposedToWorker(std::string_view name) const {
    // Worker access requires an additional flag, even with a valid OT.
    return ExpectExposedToWindow(name) && IsAPIWorkerFlagEnabled(GetParam());
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    MAYBE_AIOnDeviceBrowserTest,
    testing::Combine(testing::Bool(),
                     testing::Bool(),
                     testing::Bool(),
                     testing::Bool()),
    &DescribeTestVariant);

// Check whether the APIs are exposed to the window or worker when expected.
IN_PROC_BROWSER_TEST_P(MAYBE_AIOnDeviceBrowserTest, ExposedToWindowOrWorker) {
  auto* tab = browser()->tab_strip_model()->GetActiveWebContents();
  for (const auto& name : GetAPINames()) {
    auto check = absl::StrFormat(kCheckExposed, name);
    SCOPED_TRACE(testing::Message() << "Checking " << name);
    EXPECT_EQ(ExpectExposedToWindow(name) ? "OK" : "ReferenceError",
              content::EvalJs(tab, absl::StrFormat(kRunWindowCheck, check)));
    EXPECT_EQ(ExpectExposedToWorker(name) ? "OK" : "ReferenceError",
              content::EvalJs(tab, absl::StrFormat(kRunWorkerCheck, check)));
  }
}

// Invoke availability() for basic API functionality coverage beyond WPTs.
IN_PROC_BROWSER_TEST_P(MAYBE_AIOnDeviceBrowserTest, AvailableInWindowOrWorker) {
  auto* tab = browser()->tab_strip_model()->GetActiveWebContents();
  for (const auto& name : GetAPINames()) {
    auto check = absl::StrFormat(kCheckAvailability, name);
    SCOPED_TRACE(testing::Message() << "Checking " << name);
    EXPECT_EQ(ExpectExposedToWindow(name) ? "OK" : "ReferenceError",
              content::EvalJs(tab, absl::StrFormat(kRunWindowCheck, check)));
    EXPECT_EQ(ExpectExposedToWorker(name) ? "OK" : "ReferenceError",
              content::EvalJs(tab, absl::StrFormat(kRunWorkerCheck, check)));
  }
}

}  // namespace
