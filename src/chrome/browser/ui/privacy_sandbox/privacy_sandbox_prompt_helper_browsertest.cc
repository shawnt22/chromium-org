// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/privacy_sandbox/privacy_sandbox_prompt_helper.h"

#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/privacy_sandbox/mock_privacy_sandbox_service.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service_factory.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service.h"
#include "chrome/browser/search_engine_choice/search_engine_choice_dialog_service_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/search/ntp_test_utils.h"
#include "chrome/browser/ui/signin/signin_view_controller.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/regional_capabilities/regional_capabilities_switches.h"
#include "components/search_engines/search_engines_switches.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/sync/test/test_sync_service.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/url_constants.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/webui/help_app_ui/url_constants.h"
#endif

namespace {

const char kPrivacySandboxDialogDisplayHostHistogram[] =
    "Settings.PrivacySandbox.DialogDisplayHost";
constexpr char kPrivacySandboxPromptHelperEventHistogram[] =
    "Settings.PrivacySandbox.PromptHelperEvent2";

std::unique_ptr<KeyedService> CreateTestSyncService(content::BrowserContext*) {
  return std::make_unique<syncer::TestSyncService>();
}
}  // namespace

class PrivacySandboxPromptHelperBrowserTest : public InProcessBrowserTest {
 public:
  PrivacySandboxPromptHelperBrowserTest()
      : https_test_server_(net::EmbeddedTestServer::TYPE_HTTPS) {}

  // This setup happens before the tests run as a second browser startup.
  // Mock service creation happens here so that we have a valid browser() object
  // to initialize the queue_manager_ with after the first browser startup in
  // SetUpInProcessBrowserTestFixture().
  void SetUpOnMainThread() override {
    mock_privacy_sandbox_service_ = static_cast<MockPrivacySandboxService*>(
        PrivacySandboxServiceFactory::GetInstance()->SetTestingFactoryAndUse(
            browser()->profile(),
            base::BindRepeating(&BuildMockPrivacySandboxService)));

    ON_CALL(*mock_privacy_sandbox_service_,
            GetRequiredPromptType(PrivacySandboxService::SurfaceType::kDesktop))
        .WillByDefault(testing::Return(TestPromptType()));
    ON_CALL(*mock_privacy_sandbox_service_, IsPromptOpenForBrowser(testing::_))
        .WillByDefault(testing::Return(false));

    queue_manager_ =
        std::make_unique<privacy_sandbox::PrivacySandboxQueueManager>(
            browser()->profile());
    ON_CALL(*mock_privacy_sandbox_service_,
            GetPrivacySandboxNoticeQueueManager())
        .WillByDefault(testing::ReturnRef(*queue_manager_.get()));
  }

  // Reset queue manager after all browser-related processes have completed,
  // including tear down.
  void PostRunTestOnMainThread() override {
    InProcessBrowserTest::PostRunTestOnMainThread();
    queue_manager_.reset();
  }

  // This setup happens before SetUpOnMainThread() as an initial startup.
  void SetUpInProcessBrowserTestFixture() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    https_test_server()->AddDefaultHandlers(
        base::FilePath(FILE_PATH_LITERAL("content/test/data")));
    ASSERT_TRUE(https_test_server()->Start());
    create_services_subscription_ =
        BrowserContextDependencyManager::GetInstance()
            ->RegisterCreateServicesCallbackForTesting(base::BindRepeating(
                &PrivacySandboxPromptHelperBrowserTest::SetupTestFactories,
                base::Unretained(this)));
  }

  void SetupTestFactories(content::BrowserContext* context) {
    SyncServiceFactory::GetInstance()->SetTestingFactory(
        context, base::BindRepeating(&CreateTestSyncService));
  }

  virtual PrivacySandboxService::PromptType TestPromptType() {
    return PrivacySandboxService::PromptType::kNone;
  }

  void ValidatePromptEventEntries(
      base::HistogramTester* histogram_tester,
      std::map<
          PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent,
          int> expected_event_count) {
    int total_expected_count = 0;
    for (const auto& event_to_count : expected_event_count) {
      histogram_tester->ExpectBucketCount(
          kPrivacySandboxPromptHelperEventHistogram, event_to_count.first,
          event_to_count.second);

      total_expected_count += event_to_count.second;
    }
    // Always ignore any entries for non-top frame and pending navigations,
    // these are recorded for completeness, but are not directly tested as they
    // are fragile.
    total_expected_count += histogram_tester->GetBucketCount(
        kPrivacySandboxPromptHelperEventHistogram,
        PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kNonTopFrameNavigation);
    histogram_tester->ExpectTotalCount(
        kPrivacySandboxPromptHelperEventHistogram, total_expected_count);
  }

  syncer::TestSyncService* test_sync_service() {
    return static_cast<syncer::TestSyncService*>(
        SyncServiceFactory::GetForProfile(browser()->profile()));
  }
  MockPrivacySandboxService* mock_privacy_sandbox_service() {
    return mock_privacy_sandbox_service_;
  }
  net::EmbeddedTestServer* https_test_server() { return &https_test_server_; }

 private:
  base::CallbackListSubscription create_services_subscription_;
  net::EmbeddedTestServer https_test_server_;
  std::unique_ptr<privacy_sandbox::PrivacySandboxQueueManager> queue_manager_;
  raw_ptr<MockPrivacySandboxService, DanglingUntriaged>
      mock_privacy_sandbox_service_;
};

IN_PROC_BROWSER_TEST_F(PrivacySandboxPromptHelperBrowserTest,
                       NoPromptRequired) {
  // Check when no prompt is required, it is not shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  ValidatePromptEventEntries(&histogram_tester, {});
}

class PrivacySandboxPromptHelperWithParamBrowserTest
    : public PrivacySandboxPromptHelperBrowserTest,
      public testing::WithParamInterface<PrivacySandboxService::PromptType> {
 private:
  PrivacySandboxService::PromptType TestPromptType() override {
    // Setup appropriate prompt type based on testing parameter. Helper
    // behavior should be "identical" regardless of which type of prompt is
    // required.
    return GetParam();
  }

  base::test::ScopedFeatureList feature_list_{
      privacy_sandbox::kPrivacySandboxSettings4};
};

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       PromptOpensOnNtp) {
  // Check when a navigation to the Chrome controlled NTP occurs, which is a
  // suitable location, a prompt is shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(1);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectUniqueSample(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("new-tab-page")), 1);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        1}});
}

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       PromptOpensAboutBlank) {
  // Check when a navigation to about:blank occurs, which is a suitable
  // location, a prompt is shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(1);
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              IsPromptOpenForBrowser(browser()))
      .Times(1)
      .WillOnce(testing::Return(false));

  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(url::kAboutBlankURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectUniqueSample(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("about:blank")), 1);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        1}});
}

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       PromptOpensOnSettings) {
  // Check when a navigation to the Chrome settings occurs, which is a
  // suitable location, a prompt is shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(1);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUISettingsURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectUniqueSample(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("settings")), 1);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        1}});
}

// TODO(crbug.com/40270789): Debug and re-enable the test.
#if BUILDFLAG(IS_CHROMEOS)
#define MAYBE_PromptOpensOnHistory DISABLED_PromptOpensOnHistory
#else
#define MAYBE_PromptOpensOnHistory PromptOpensOnHistory
#endif
IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       MAYBE_PromptOpensOnHistory) {
  // Check when a navigation to the Chrome history occurs, which is a
  // suitable location, a prompt is shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(1);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUIHistoryURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectUniqueSample(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("history")), 1);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        1}});
}

#if !BUILDFLAG(IS_CHROMEOS)
IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       NoPromptNonDefaultNtp) {
  // Check that navigations to the generic chrome://newtab, when a non default
  // NTP is used, do not show a prompt. On ChromeOS, it opens an about blank
  // tab to display the prompt because it cannot be handled during startup
  // there.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);

  // Sets up explicit redirect to invalid URL. Otherwise, redirects to
  // chrome://new-tab-page/, which is considered a valid URL.
  GURL ntp_url = https_test_server()->GetURL("/title1.html");
  ntp_test_utils::SetUserSelectedDefaultSearchProvider(
      browser()->profile(), https_test_server()->base_url().spec(),
      ntp_url.spec());

  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectTotalCount(kPrivacySandboxDialogDisplayHostHistogram,
                                    0);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kUrlNotSuitable,
        1}});
}
#endif

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       NoPromptSync) {
  // Check when sync setup is in progress, that no prompt is shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);
  test_sync_service()->SetSetupInProgress();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectTotalCount(kPrivacySandboxDialogDisplayHostHistogram,
                                    0);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kSyncSetupInProgress,
        1}});
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       NoPromptProfileSetup) {
  // Check when profile setup is in progress, that no prompt is shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);
  // Show the profile customization dialog.
  browser()
      ->GetFeatures()
      .signin_view_controller()
      ->ShowModalProfileCustomizationDialog(
          /*is_local_profile_creation=*/true);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectTotalCount(kPrivacySandboxDialogDisplayHostHistogram,
                                    0);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kSigninDialogShown,
        1}});
}
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       UnsuitableUrl) {
  // Check that no prompt is shown for navigations to unsuitable URLs.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);

  std::vector<GURL> urls_to_open = {
      https_test_server()->GetURL("a.test", "/title1.html"),
      GURL(chrome::kChromeUISettingsURL).Resolve(chrome::kAutofillSubPage),
#if BUILDFLAG(IS_CHROMEOS)
      GURL(ash::kChromeUIHelpAppURL),
      GURL(chrome::kChromeUIOSSettingsURL),
#endif
  };

  for (size_t i = 0; i < urls_to_open.size(); ++i) {
    if (i == 0) {
      // Open the first URL in a new tab to create a fresh new tab helper.
      ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), urls_to_open[i], WindowOpenDisposition::NEW_FOREGROUND_TAB,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    } else {
      ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), urls_to_open[i]));
    }
  }

  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectTotalCount(kPrivacySandboxDialogDisplayHostHistogram,
                                    0);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kUrlNotSuitable,
        urls_to_open.size()}});
}

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       SinglePromptPerBrowser) {
  // Check that only a single prompt is opened per browser window at a time.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(1);
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              IsPromptOpenForBrowser(browser()))
      .WillOnce(testing::Return(false))
      .WillRepeatedly(testing::Return(true));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL(chrome::kChromeUINewTabPageURL)));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL(chrome::kChromeUINewTabPageURL)));
  base::RunLoop().RunUntilIdle();
  histogram_tester.ExpectUniqueSample(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("new-tab-page")), 1);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptAlreadyExistsForBrowser,
        2},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        1}});
}

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptHelperWithParamBrowserTest,
                       MultipleBrowserWindows) {
  // Check that if multiple browser windows are opened, and navigated to
  // appropriate tabs, two prompts are opened.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(testing::_, testing::_))
      .Times(2);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(url::kAboutBlankURL), WindowOpenDisposition::NEW_WINDOW,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabPageURL),
      WindowOpenDisposition::NEW_WINDOW,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  histogram_tester.ExpectBucketCount(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("new-tab-page")), 1);
  histogram_tester.ExpectBucketCount(
      kPrivacySandboxDialogDisplayHostHistogram,
      static_cast<base::HistogramBase::Sample32>(base::Hash("about:blank")), 1);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        2},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        2}});
}

INSTANTIATE_TEST_SUITE_P(
    PrivacySandboxPromptHelperWithParamInstanceBrowserTest,
    PrivacySandboxPromptHelperWithParamBrowserTest,
    testing::Values(PrivacySandboxService::PromptType::kM1Consent,
                    PrivacySandboxService::PromptType::kM1NoticeEEA,
                    PrivacySandboxService::PromptType::kM1NoticeROW));

class PrivacySandboxPromptNonNormalBrowserTest
    : public PrivacySandboxPromptHelperBrowserTest,
      public testing::WithParamInterface<PrivacySandboxService::PromptType> {
 public:
  PrivacySandboxService::PromptType TestPromptType() override {
    return GetParam();
  }
};

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptNonNormalBrowserTest,
                       NoPromptInLargeBrowser) {
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(testing::_, testing::_))
      .Times(0);

  NavigateParams params(browser(), GURL(chrome::kChromeUINewTabPageURL),
                        ui::PAGE_TRANSITION_FIRST);
  params.window_action = NavigateParams::SHOW_WINDOW;
  params.disposition = WindowOpenDisposition::NEW_POPUP;
  params.window_features.bounds = gfx::Rect(0, 0, 500, 500);
  ui_test_utils::NavigateToURL(&params);

  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kNonNormalBrowser,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        0}});
}

IN_PROC_BROWSER_TEST_P(PrivacySandboxPromptNonNormalBrowserTest,
                       NoPromptInSmallBrowser) {
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(testing::_, testing::_))
      .Times(0);

  NavigateParams params(browser(), GURL(chrome::kChromeUINewTabPageURL),
                        ui::PAGE_TRANSITION_FIRST);
  params.window_action = NavigateParams::SHOW_WINDOW;
  params.disposition = WindowOpenDisposition::NEW_POPUP;
  params.window_features.bounds = gfx::Rect(0, 0, 200, 200);
  ui_test_utils::NavigateToURL(&params);

  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kCreated,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kNonNormalBrowser,
        1},
       {PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kPromptShown,
        0}});
}

INSTANTIATE_TEST_SUITE_P(
    PrivacySandboxPromptNonNormalBrowserTestInstance,
    PrivacySandboxPromptNonNormalBrowserTest,
    testing::Values(PrivacySandboxService::PromptType::kM1Consent,
                    PrivacySandboxService::PromptType::kM1NoticeEEA,
                    PrivacySandboxService::PromptType::kM1NoticeROW,
                    PrivacySandboxService::PromptType::kM1NoticeRestricted));

class PrivacySandboxPromptHelperWithSearchEngineChoiceEnabledBrowserTest
    : public PrivacySandboxPromptHelperWithParamBrowserTest {
 public:
  void SetUpOnMainThread() override {
    PrivacySandboxPromptHelperWithParamBrowserTest::SetUpOnMainThread();
    SearchEngineChoiceDialogService::SetDialogDisabledForTests(
        /*dialog_disabled=*/false);
  }

  // Override the country to simulate showing the search engine choice dialog.
  void SetUpCommandLine(base::CommandLine* command_line) override {
    PrivacySandboxPromptHelperWithParamBrowserTest::SetUpCommandLine(
        command_line);
    command_line->AppendSwitchASCII(switches::kSearchEngineChoiceCountry, "BE");
    command_line->AppendSwitch(
        switches::kIgnoreNoFirstRunForSearchEngineChoiceScreen);
  }

 private:
  base::AutoReset<bool> scoped_chrome_build_override_ =
      SearchEngineChoiceDialogServiceFactory::
          ScopedChromeBuildOverrideForTesting(
              /*force_chrome_build=*/true);
};

IN_PROC_BROWSER_TEST_P(
    PrivacySandboxPromptHelperWithSearchEngineChoiceEnabledBrowserTest,
    NoPromptWhenSearchEngineChoiceDialogIsDisplayed) {
  // Check that the Privacy Sandbox dialog is not shown.
  base::HistogramTester histogram_tester;
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);

  // Navigate to a url to show the search engine choice dialog.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(url::kAboutBlankURL),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  histogram_tester.ExpectTotalCount(kPrivacySandboxDialogDisplayHostHistogram,
                                    0);
  ValidatePromptEventEntries(
      &histogram_tester,
      {{PrivacySandboxPromptHelper::SettingsPrivacySandboxPromptHelperEvent::
            kSearchEngineChoiceDialogShown,
        1}});

  // Make a search engine choice to close the dialog.
  SearchEngineChoiceDialogService* search_engine_choice_dialog_service =
      SearchEngineChoiceDialogServiceFactory::GetForProfile(
          browser()->profile());
  search_engine_choice_dialog_service->NotifyChoiceMade(
      /*prepopulate_id=*/1, /*save_guest_mode_selection=*/false,
      SearchEngineChoiceDialogService::EntryPoint::kDialog);

  // Make sure that the Privacy Sandbox prompt doesn't get displayed on the next
  // navigation.
  EXPECT_CALL(*mock_privacy_sandbox_service(),
              PromptOpenedForBrowser(browser(), testing::_))
      .Times(0);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(url::kAboutBlankURL), WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
}

INSTANTIATE_TEST_SUITE_P(
    PrivacySandboxPromptHelperWithParamInstanceBrowserTest,
    PrivacySandboxPromptHelperWithSearchEngineChoiceEnabledBrowserTest,
    testing::Values(PrivacySandboxService::PromptType::kM1Consent,
                    PrivacySandboxService::PromptType::kM1NoticeEEA,
                    PrivacySandboxService::PromptType::kM1NoticeROW));
