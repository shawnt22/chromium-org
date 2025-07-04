// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/notreached.h"
#include "base/test/gmock_expected_support.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/types/expected.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_browsertest.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/test/isolated_web_app_test_utils.h"
#include "chrome/browser/web_applications/isolated_web_apps/commands/install_isolated_web_app_command.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_install_source.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_trust_checker.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_url_info.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_builder.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/test_signed_web_bundle_builder.h"
#include "chrome/browser/web_applications/os_integration/os_integration_manager.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/custom_handlers/protocol_handler.h"
#include "components/web_package/signed_web_bundles/signed_web_bundle_id.h"
#include "components/web_package/test_support/signed_web_bundles/web_bundle_signer.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace {

GURL GetGoogleURL() {
  return GURL("http://www.google.com/");
}

class ExternalProtocolHandlerDelegate
    : public ExternalProtocolHandler::Delegate {
 public:
  ExternalProtocolHandlerDelegate() {
    ExternalProtocolHandler::SetDelegateForTesting(this);
  }
  ~ExternalProtocolHandlerDelegate() override {
    ExternalProtocolHandler::SetDelegateForTesting(nullptr);
  }

  // ExternalProtocolHandler::Delegate:
  ExternalProtocolHandler::BlockState GetBlockState(const std::string& scheme,
                                                    Profile* profile) override {
    return ExternalProtocolHandler::BLOCK;
  }
  void BlockRequest() override { future.SetValue(); }
  scoped_refptr<shell_integration::DefaultSchemeClientWorker> CreateShellWorker(
      const GURL& url) override {
    NOTREACHED();
  }
  void RunExternalProtocolDialog(
      const GURL& url,
      content::WebContents* web_contents,
      ui::PageTransition page_transition,
      bool has_user_gesture,
      const std::optional<url::Origin>& initiating_origin,
      const std::u16string& program_name) override {
    NOTREACHED();
  }
  void LaunchUrlWithoutSecurityCheck(
      const GURL& url,
      content::WebContents* web_contents) override {
    NOTREACHED();
  }
  void FinishedProcessingCheck() override { NOTREACHED(); }

  base::test::TestFuture<void> future;
};

class BrowserNavigatorIwaTest : public BrowserNavigatorTest {
 public:
  BrowserNavigatorIwaTest() {
    scoped_feature_list_.InitAndEnableFeature(features::kIsolatedWebApps);
  }

  void SetUpOnMainThread() override {
    BrowserNavigatorTest::SetUpOnMainThread();
    web_app::test::WaitUntilReady(
        web_app::WebAppProvider::GetForTest(profile()));
  }

  Profile* profile() { return browser()->profile(); }

 protected:
  void InstallBundles() {
    app1_ = web_app::IsolatedWebAppBuilder(
                web_app::ManifestBuilder()
                    .SetName("app-1.0.0")
                    .SetVersion("1.0.0")
                    .AddProtocolHandler("web+meow", "/index.html?params=%s"))
                .BuildBundle();

    url_info1_ = std::make_unique<web_app::IsolatedWebAppUrlInfo>(
        *app1_->Install(profile()));

    app2_ =
        web_app::IsolatedWebAppBuilder(
            web_app::ManifestBuilder().SetName("app-1.0.0").SetVersion("1.0.0"))
            .BuildBundle();

    url_info2_ = std::make_unique<web_app::IsolatedWebAppUrlInfo>(
        *app2_->Install(profile()));
  }

 protected:
  std::unique_ptr<web_app::IsolatedWebAppUrlInfo> url_info1_;
  std::unique_ptr<web_app::IsolatedWebAppUrlInfo> url_info2_;
  std::unique_ptr<web_app::ScopedBundledIsolatedWebApp> app1_;
  std::unique_ptr<web_app::ScopedBundledIsolatedWebApp> app2_;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  web_app::OsIntegrationManager::ScopedSuppressForTesting os_hooks_suppress_;
};

IN_PROC_BROWSER_TEST_F(BrowserNavigatorIwaTest, NavigateCurrentTab) {
  ASSERT_NO_FATAL_FAILURE(InstallBundles());

  // 1. When navigating a tab to an isolated-app: origin, and that tab is not
  //    part of an app browser for that origin, a new window and tab should be
  //    created.

  NavigateParams params1 = MakeNavigateParams(browser());
  params1.url = url_info1_->origin().GetURL().Resolve("/first-page.html");
  params1.disposition = WindowOpenDisposition::CURRENT_TAB;
  ui_test_utils::NavigateToURL(&params1);

  Browser* iwa_browser = params1.browser;
  EXPECT_NE(iwa_browser, browser());
  EXPECT_EQ(2u, chrome::GetTotalBrowserCount());

  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()
                  ->tab_strip_model()
                  ->GetActiveWebContents()
                  ->GetURL()
                  .IsAboutBlank());

  ASSERT_EQ(1, iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(url_info1_->origin().GetURL().Resolve("/first-page.html"),
            iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());

  // 2. When navigating a tab to an isolated-app: origin, and that tab is
  //    already part of an app browser for that origin, then the same window and
  //    tab should be re-used.

  NavigateParams params2 = MakeNavigateParams(iwa_browser);
  params2.url = url_info1_->origin().GetURL().Resolve("/other-page.html");
  params2.disposition = WindowOpenDisposition::CURRENT_TAB;
  ui_test_utils::NavigateToURL(&params2);

  // Navigating a tab in the app's scope should not create a new browser.
  EXPECT_EQ(iwa_browser, params2.browser);
  ASSERT_EQ(1, iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(url_info1_->origin().GetURL().Resolve("/other-page.html"),
            iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());

  // 3. When navigating a tab to an isolated-app: origin, and that tab is
  //    already part of an app browser for a different isolated-app: origin,
  //    then a new window and tab should be created.
  NavigateParams params3 = MakeNavigateParams(iwa_browser);
  params3.url =
      url_info2_->origin().GetURL().Resolve("/page-in-another-iwa.html");
  params3.disposition = WindowOpenDisposition::CURRENT_TAB;
  ui_test_utils::NavigateToURL(&params3);

  // Navigating a tab outside of the app's scope should create a new browser.
  Browser* new_iwa_browser = params3.browser;
  EXPECT_NE(iwa_browser, new_iwa_browser);
  EXPECT_NE(browser(), new_iwa_browser);
  EXPECT_EQ(3u, chrome::GetTotalBrowserCount());

  ASSERT_EQ(1, iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(url_info1_->origin().GetURL().Resolve("/other-page.html"),
            iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());

  ASSERT_EQ(1, new_iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(
      url_info2_->origin().GetURL().Resolve("/page-in-another-iwa.html"),
      new_iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());

  // 4. When navigating a tab from an isolated-app: to an http: origin, then,
  //    - On ChromeOS, the navigation should be intercepted and instead be
  //      opened in a new tab in a non-IWA browser window.
  //    - On other platforms, the navigation should be intercepted and instead
  //      be opened in the default browser (as in, e.g., Firefox).

  auto protocol_handler_delegate =
      std::make_unique<ExternalProtocolHandlerDelegate>();

  NavigateParams params4 = MakeNavigateParams(iwa_browser);
  params4.url = GetGoogleURL();
  params4.disposition = WindowOpenDisposition::CURRENT_TAB;
  ui_test_utils::NavigateToURL(&params4);

  // It might seem counterintuitive that `params4.browser` is expected to equal
  // to `iwa_browser`, but this is because the request is not intercepted by the
  // browser navigation code, but by the `IsolatedWebAppThrottle`, which runs
  // afterwards.
  EXPECT_EQ(iwa_browser, params4.browser);
  EXPECT_EQ(3u, chrome::GetTotalBrowserCount());

  // The page should not have navigated.
  ASSERT_EQ(1, iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(
      url_info1_->origin().GetURL().Resolve("/other-page.html"),
      iwa_browser->tab_strip_model()->GetActiveWebContents()->GetVisibleURL());

#if BUILDFLAG(IS_CHROMEOS)
  // A new tab should have been opened in the non-app browser.
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()
                  ->tab_strip_model()
                  ->GetWebContentsAt(0)
                  ->GetURL()
                  .IsAboutBlank());
  EXPECT_EQ(GetGoogleURL(),
            browser()->tab_strip_model()->GetWebContentsAt(1)->GetURL());
#else
  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()
                  ->tab_strip_model()
                  ->GetWebContentsAt(0)
                  ->GetURL()
                  .IsAboutBlank());
  EXPECT_TRUE(protocol_handler_delegate->future.Wait());
#endif
}

#if BUILDFLAG(IS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(BrowserNavigatorIwaTest, WindowOpenProtocol) {
  ASSERT_NO_FATAL_FAILURE(InstallBundles());

  {
    // Eliminate all prompts/guards along the way.
    ExternalProtocolHandler::PermitLaunchUrl();
    ExternalProtocolHandler::SetBlockState("web+meow", url_info2_->origin(),
                                           ExternalProtocolHandler::DONT_BLOCK,
                                           profile());
    base::test::TestFuture<void> future;
    web_app::WebAppProvider::GetForWebApps(profile())
        ->scheduler()
        .UpdateProtocolHandlerUserApproval(url_info1_->app_id(), "web+meow",
                                           web_app::ApiApprovalState::kAllowed,
                                           future.GetCallback());
    ASSERT_TRUE(future.Wait());
  }

  // Open a protocol url from an app frame of IWA2.
  auto* rfh = web_app::OpenIsolatedWebApp(profile(), url_info2_->app_id());

  GURL remapped_url =
      custom_handlers::ProtocolHandler::CreateProtocolHandler(
          "web+meow",
          url_info1_->origin().GetURL().Resolve("/index.html?params=%s"))
          .TranslateUrl(GURL("web+meow://hru"));

  ui_test_utils::UrlLoadObserver observer(remapped_url);
  ASSERT_THAT(content::EvalJs(rfh, "window.open('web+meow://hru')"),
              content::EvalJsResult::IsOk());
  observer.Wait();

  ASSERT_TRUE(web_app::AppBrowserController::IsForWebApp(
      chrome::FindBrowserWithTab(observer.web_contents()),
      url_info1_->app_id()));
}
#endif

class BrowserNavigatorIwaNewTabTest
    : public BrowserNavigatorIwaTest,
      public ::testing::WithParamInterface<WindowOpenDisposition> {};

IN_PROC_BROWSER_TEST_P(BrowserNavigatorIwaNewTabTest, NavigateNewTab) {
  EXPECT_NO_FATAL_FAILURE(InstallBundles());

  Browser* iwa_browser = Browser::Create(Browser::CreateParams::CreateForApp(
      web_app::GenerateApplicationNameFromAppId(url_info1_->app_id()),
      /*trusted_source=*/false, gfx::Rect(), profile(), /*user_gesture=*/true));

  // 1. Navigate a new tab in the in an empty IWA browser to an http: origin.
  //    This should be aborted and instead be opened in the default browser (as
  //    in, e.g., Firefox). This test does not check whether the external
  //    default browser is actually opened, since this functionality is handled
  //    by the `IsolatedWebAppThrottle` and its unit tests.
  //
  // TODO(b/320288977): This is not yet working.

  // 2. Navigate a new tab in an empty IWA browser for an app matching the
  //    navigation's `params.url` origin. This should add the new tab to the
  //    existing browser window.

  NavigateParams params2 = MakeNavigateParams(iwa_browser);
  params2.url = url_info1_->origin().GetURL();
  params2.disposition = GetParam();
  ui_test_utils::NavigateToURL(&params2);

  EXPECT_EQ(params2.browser, iwa_browser);
  EXPECT_EQ(2u, chrome::GetTotalBrowserCount());

  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()
                  ->tab_strip_model()
                  ->GetActiveWebContents()
                  ->GetURL()
                  .IsAboutBlank());

  ASSERT_EQ(1, iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(url_info1_->origin().GetURL(),
            iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());

  // 3. Navigate a new tab in the same IWA browser that already has a tab for an
  //    app matching the navigation's `params.url` origin. This should create a
  //    new browser window.

  NavigateParams params3 = MakeNavigateParams(iwa_browser);
  params3.url = url_info1_->origin().GetURL();
  params3.disposition = GetParam();
  ui_test_utils::NavigateToURL(&params3);

  Browser* new_iwa_browser = params3.browser;
  EXPECT_NE(new_iwa_browser, iwa_browser);
  EXPECT_NE(new_iwa_browser, browser());
  EXPECT_EQ(3u, chrome::GetTotalBrowserCount());

  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(browser()
                  ->tab_strip_model()
                  ->GetActiveWebContents()
                  ->GetURL()
                  .IsAboutBlank());

  ASSERT_EQ(1, iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(url_info1_->origin().GetURL(),
            iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());

  ASSERT_EQ(1, new_iwa_browser->tab_strip_model()->count());
  EXPECT_EQ(
      url_info1_->origin().GetURL(),
      new_iwa_browser->tab_strip_model()->GetActiveWebContents()->GetURL());
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    BrowserNavigatorIwaNewTabTest,
    ::testing::Values(WindowOpenDisposition::NEW_FOREGROUND_TAB,
                      WindowOpenDisposition::NEW_BACKGROUND_TAB),
    [](const testing::TestParamInfo<BrowserNavigatorIwaNewTabTest::ParamType>&
           info) {
      switch (info.param) {
        case WindowOpenDisposition::NEW_FOREGROUND_TAB:
          return "NEW_FOREGROUND_TAB";
        case WindowOpenDisposition::NEW_BACKGROUND_TAB:
          return "NEW_BACKGROUND_TAB";
        default:
          NOTREACHED();
      }
    });

}  // namespace
