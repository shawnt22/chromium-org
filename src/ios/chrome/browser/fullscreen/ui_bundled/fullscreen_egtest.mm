// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "base/functional/bind.h"
#import "base/ios/ios_util.h"
#import "base/strings/stringprintf.h"
#import "base/strings/sys_string_conversions.h"
#import "base/strings/utf_string_conversions.h"
#import "base/test/ios/wait_util.h"
#import "components/translate/core/browser/translate_pref_names.h"
#import "ios/chrome/browser/fullscreen/ui_bundled/test/fullscreen_app_interface.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey_ui.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/scoped_block_popups_pref.h"
#import "ios/chrome/test/earl_grey/web_http_server_chrome_test_case.h"
#import "ios/chrome/test/scoped_eg_synchronization_disabler.h"
#import "ios/testing/earl_grey/app_launch_manager.h"
#import "ios/testing/earl_grey/earl_grey_test.h"
#import "ios/web/common/features.h"
#import "ios/web/public/test/http_server/error_page_response_provider.h"
#import "ios/web/public/test/http_server/http_server.h"
#import "ios/web/public/test/http_server/http_server_util.h"
#import "net/test/embedded_test_server/embedded_test_server.h"
#import "url/gurl.h"

using base::test::ios::kWaitForJSCompletionTimeout;
using base::test::ios::kWaitForPageLoadTimeout;
using base::test::ios::WaitUntilConditionOrTimeout;

using chrome_test_util::WebStateScrollViewMatcher;

namespace {

// The page height of test pages. This must be big enough to triger fullscreen.
const int kPageHeightEM = 400;

// Hides the toolbar by scrolling down.
void HideToolbarUsingUI() {
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_swipeSlowInDirection(kGREYDirectionUp)];
}

// Asserts that the current URL is the `expectedURL` one.
void AssertURLIs(const GURL& expectedURL) {
  NSString* description = [NSString
      stringWithFormat:@"Timeout waiting for the url to be %@",
                       base::SysUTF8ToNSString(expectedURL.GetContent())];

  ConditionBlock condition = ^{
    NSError* error = nil;
    [[EarlGrey selectElementWithMatcher:chrome_test_util::OmniboxText(
                                            expectedURL.GetContent())]
        assertWithMatcher:grey_notNil()
                    error:&error];
    return (error == nil);
  };
  GREYAssert(WaitUntilConditionOrTimeout(kWaitForPageLoadTimeout, condition),
             description);
}

// A PDF itself can take a little longer to appear even after the page is
// loaded. Instead, do an additional wait for the internal PDF class to appear
// in the view hierarchy.
void WaitforPDFExtensionView() {
  // TODO(crbug.com/424744794): Code below was added as a speculative fix. That
  // code is now skipped on iOS26, consider adding a long term fix in case
  // tests become flaky without that logic.
  if (@available(iOS 26, *)) {
    [ChromeEarlGrey waitForPageToFinishLoading];
    return;
  }
  ConditionBlock condition = ^{
    NSError* error = nil;
    [[EarlGrey selectElementWithMatcher:grey_kindOfClass(NSClassFromString(
                                            @"PDFExtensionTopView"))]
        assertWithMatcher:grey_notNil()
                    error:&error];
    return error == nil;
  };

  NSString* errorMessage = @"PDFExtensionTopView was not visible";
  GREYAssert(WaitUntilConditionOrTimeout(kWaitForPageLoadTimeout, condition),
             errorMessage);
}

// Helper function to create HTML responses.
std::unique_ptr<net::test_server::HttpResponse> CreateHttpResponse(
    const std::string& content) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(content);
  return response;
}

}  // namespace

#pragma mark - Tests

// Fullscreens tests for Chrome.
// TODO(crbug.com/40849153): Remove the "ZZZ" when the bug is fixed.
@interface ZZZFullscreenTestCase : WebHttpServerChromeTestCase
@end

@implementation ZZZFullscreenTestCase

- (AppLaunchConfiguration)appConfigurationForTestCase {
  AppLaunchConfiguration config;
  config.features_disabled.push_back(web::features::kSmoothScrollingDefault);
  return config;
}

- (void)setUp {
  [super setUp];

  // Disable translate to avoid the info bar that block the top toolbar.
  [ChromeEarlGrey setBoolValue:NO
                   forUserPref:translate::prefs::kOfferTranslateEnabled];

  [ChromeEarlGrey setBoolValue:NO forLocalStatePref:prefs::kBottomOmnibox];
}

- (void)tearDownHelper {
  // Reactivate translation.
  [ChromeEarlGrey setBoolValue:YES
                   forUserPref:translate::prefs::kOfferTranslateEnabled];
  [super tearDownHelper];
}

// Verifies that the content offset of the web view is set up at the correct
// initial value when initially displaying a PDF.
- (void)testLongPDFInitialState {
  GURL URL = web::test::HttpServer::MakeUrl(
      "http://ios/testing/data/http_server_files/two_pages.pdf");
  [ChromeEarlGrey loadURL:URL];
  WaitforPDFExtensionView();
  [ChromeEarlGreyUI waitForToolbarVisible:YES];

  // Initial y scroll positions are set to make room for the toolbar.
  CGFloat yOffset = -[FullscreenAppInterface currentViewportInsets].top;
  DCHECK_LT(yOffset, 0);
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      assertWithMatcher:grey_scrollViewContentOffset(CGPointMake(0, yOffset))];
}

// Verifies that the toolbar is not hidden when scrolling a short pdf, as the
// entire document is visible without hiding the toolbar.
- (void)testSmallWidePDFScroll {
  GURL URL = web::test::HttpServer::MakeUrl(
      "http://ios/testing/data/http_server_files/single_page_wide.pdf");
  [ChromeEarlGrey loadURL:URL];
  WaitforPDFExtensionView();

  // Test that the toolbar is still visible after a user swipes down.
  // Use a slow swipe here because in this combination of conditions (one
  // page PDF, overscroll actions enabled, fast swipe), the
  // `UIScrollViewDelegate scrollViewDidEndDecelerating:` is not called leading
  // to an EarlGrey infinite wait.
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_swipeSlowInDirection(kGREYDirectionDown)];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Verifies that the toolbar properly appears/disappears when scrolling up/down
// on a PDF that is long in length and wide in width.
- (void)testLongPDFScroll {
  GURL URL = web::test::HttpServer::MakeUrl(
      "http://ios/testing/data/http_server_files/two_pages.pdf");
  [ChromeEarlGrey loadURL:URL];
  WaitforPDFExtensionView();

  // Test that the toolbar is hidden after a user swipes up.
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_scrollInDirection(kGREYDirectionDown, 150)];
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Test that the toolbar is visible after a user swipes down.
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_swipeFastInDirection(kGREYDirectionDown)];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];

  // Test that the toolbar is hidden after a user swipes up.
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_scrollInDirection(kGREYDirectionDown, 150)];
  [ChromeEarlGreyUI waitForToolbarVisible:NO];
}

// Tests that link clicks from a chrome:// to chrome:// link result in the
// header being shown even if was not previously shown.
- (void)testChromeToChromeURLKeepsHeaderOnScreen {
  const GURL kChromeAboutURL("chrome://chrome-urls");
  [ChromeEarlGrey loadURL:kChromeAboutURL];
  [ChromeEarlGrey waitForWebStateContainingText:"chrome://version"];

  // Hide the toolbar. The page is not long enough to dismiss the toolbar using
  // the UI so we have to zoom in.
  NSString* script = @"(function(){"
                      "var metas = document.getElementsByTagName('meta');"
                      "for (var i=0; i<metas.length; i++) {"
                      "  if (metas[i].getAttribute('name') == 'viewport') {"
                      "    metas[i].setAttribute('content', 'width=10');"
                      "    return;"
                      "  }"
                      "}"
                      "document.body.innerHTML += \"<meta name='viewport' "
                      "content='width=10'>\""
                      "})()";
  [ChromeEarlGrey evaluateJavaScriptForSideEffect:script];

  // Scroll up to be sure the toolbar can be dismissed by scrolling down.
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_swipeFastInDirection(kGREYDirectionDown)];

  // Scroll to hide the UI.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Test that the toolbar is visible when moving from one chrome:// link to
  // another chrome:// link. The script below queries for the
  // "chrome://version" link on the chrome://chrome-urls page, and clicks on
  // it. The link is in the shadow DOM of the chrome-urls-app custom element
  // that contains the page's UI.
  NSString* clickLinkScript =
      @"document.body.querySelector('chrome-urls-app')"
       ".shadowRoot.querySelector('a[href=\"chrome://version\"]').click()";
  [ChromeEarlGrey evaluateJavaScriptForSideEffect:clickLinkScript];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Tests hiding and showing of the header with a user scroll on a long page.
- (void)testHideHeaderUserScrollLongPage {
  self.testServer->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/tallpage") {
          return CreateHttpResponse(base::StringPrintf(
              "<p style='height:%dem'>a</p><p>b</p>", kPageHeightEM));
        }
        return nullptr;
      }));

  GREYAssertTrue(self.testServer->Start(), @"The server has not started");
  GURL URL = self.testServer->GetURL("/tallpage");
  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
  // Simulate a user scroll down.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];
  // Simulate a user scroll up.
  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_swipeFastInDirection(kGREYDirectionDown)];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Tests that reloading of a page shows the header even if it was not shown
// previously.
- (void)testShowHeaderOnReload {
  self.testServer->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/origin") {
          return CreateHttpResponse(base::StringPrintf(
              "<p style='height:%dem'>Tall page</p>"
              "<a onclick='window.location.reload();' id='link'>link</a>",
              kPageHeightEM));
        }
        return nullptr;
      }));

  GREYAssertTrue(self.testServer->Start(), @"The server has not started");
  GURL URL = self.testServer->GetURL("/origin");
  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGrey waitForWebStateContainingText:"Tall page"];

  // Hide the toolbar.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];
  [ChromeEarlGrey tapWebStateElementWithID:@"link"];
  // Main test is here: Make sure the header is still visible!
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Test to make sure the header is shown when a Tab opened by the current Tab is
// closed even if the toolbar was not present previously.
- (void)testShowHeaderWhenChildTabCloses {
  std::map<GURL, std::string> responses;
  const GURL URL = web::test::HttpServer::MakeUrl("http://origin");
  const GURL destinationURL =
      web::test::HttpServer::MakeUrl("http://destination");
  // JavaScript to open a window using window.open.
  std::string javaScript =
      base::StringPrintf("window.open(\"%s\");", destinationURL.spec().c_str());

  // A long page with a link to execute JavaScript.
  responses[URL] = base::StringPrintf("<p style='height:%dem'>whatever</p>"
                                      "<a onclick='%s' id='link1'>link1</a>",
                                      kPageHeightEM, javaScript.c_str());
  // A long page with some simple text and link to close itself using
  // window.close.
  javaScript = "window.close()";
  responses[destinationURL] =
      base::StringPrintf("<p style='height:%dem'>whatever</p><a onclick='%s' "
                         "id='link2'>link2</a>",
                         kPageHeightEM, javaScript.c_str());

  web::test::SetUpSimpleHttpServer(responses);
  ScopedBlockPopupsPref prefSetter(CONTENT_SETTING_ALLOW);

  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGrey waitForWebStateContainingText:"link1"];
  [ChromeEarlGrey waitForMainTabCount:1];

  // Hide the toolbar.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Open new window.
  [ChromeEarlGrey tapWebStateElementWithID:@"link1"];

  // Check that a new Tab was created.
  [ChromeEarlGrey waitForWebStateContainingText:"link2"];
  [ChromeEarlGrey waitForMainTabCount:2];

  AssertURLIs(destinationURL);

  // Hide the toolbar.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Close the tab by tapping link2.
  [ChromeEarlGrey tapWebStateElementWithID:@"link2"];

  [ChromeEarlGrey waitForWebStateContainingText:"link1"];

  // Make sure the toolbar is on the screen.
  [ChromeEarlGrey waitForMainTabCount:1];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Tests that the header is shown when a regular page (non-native page) is
// loaded from a page where the header was not see before.
// Also tests that auto-hide works correctly on new page loads.
- (void)testShowHeaderOnRegularPageLoad {
  self.testServer->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        const std::string manyLines = base::StringPrintf(
            "<p style='height:%dem'>a</p><p>End of lines</p>", kPageHeightEM);

        if (request.relative_url == "/origin") {
          return CreateHttpResponse(
              manyLines + "<a href='/destination' id='link1'>link1</a>");
        } else if (request.relative_url == "/destination") {
          return CreateHttpResponse(manyLines +
                                    "<a href='javascript:void(0)' "
                                    "onclick='window.history.back()' "
                                    "id='link2'>link2</a>");
        }
        return nullptr;
      }));

  GREYAssertTrue(self.testServer->Start(), @"The server has not started");
  GURL originURL = self.testServer->GetURL("/origin");
  [ChromeEarlGrey loadURL:originURL];

  [ChromeEarlGrey waitForWebStateContainingText:"link1"];
  // Dismiss the toolbar.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Navigate to the other page.
  [ChromeEarlGrey tapWebStateElementWithID:@"link1"];
  [ChromeEarlGrey waitForWebStateContainingText:"link2"];

  // Make sure toolbar is shown since a new load has started.
  [ChromeEarlGreyUI waitForToolbarVisible:YES];

  // Dismiss the toolbar.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Go back.
  [ChromeEarlGrey tapWebStateElementWithID:@"link2"];

  // Make sure the toolbar has loaded now that a new page has loaded.
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Tests that the header is shown when a native page is loaded from a page where
// the header was not seen before.
- (void)testShowHeaderOnNativePageLoad {
  self.testServer->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/origin") {
          return CreateHttpResponse(base::StringPrintf(
              "<p style='height:%dem'>a</p>"
              "<a onclick='window.history.back()' id='link'>link</a>",
              kPageHeightEM));
        }
        return nullptr;
      }));

  GREYAssertTrue(self.testServer->Start(), @"The server has not started");
  GURL URL = self.testServer->GetURL("/origin");

  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGrey waitForWebStateContainingText:"link"];

  // Dismiss the toolbar.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Go back to NTP, which is a native view.
  [ChromeEarlGrey tapWebStateElementWithID:@"link"];

  // Make sure the toolbar is visible now that a new page has loaded.
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Tests that the header is shown when loading an error page in a native view
// even if fullscreen was enabled previously.
- (void)testShowHeaderOnErrorPage {
  GURL errorURL = ErrorPageResponseProvider::GetDnsFailureUrl();

  self.testServer->RegisterRequestHandler(base::BindRepeating(
      [](const std::string& errorURLSpec,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/origin") {
          return CreateHttpResponse(
              base::StringPrintf("<p style='height:%dem'>a</p>"
                                 "<a href=\"%s\" id=\"link\">bad link</a>",
                                 kPageHeightEM, errorURLSpec.c_str()));
        }
        return nullptr;
      },
      errorURL.spec()));

  GREYAssertTrue(self.testServer->Start(), @"The server has not started");
  GURL URL = self.testServer->GetURL("/origin");
  [ChromeEarlGrey loadURL:URL];
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  [ChromeEarlGrey tapWebStateElementWithID:@"link"];
  AssertURLIs(ErrorPageResponseProvider::GetDnsFailureUrl());
  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

// Tests collapsing of toolbar when a user scroll on a long page and rotate.
- (void)testCollapseToolbarOnScrollAndRotate {
  self.testServer->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/tallpage") {
          return CreateHttpResponse(base::StringPrintf(
              "<p style='height:%dem'>a</p><p>b</p>", kPageHeightEM));
        }
        return nullptr;
      }));

  GREYAssertTrue(self.testServer->Start(), @"The server has not started");
  GURL URL = self.testServer->GetURL("/tallpage");

  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];

  // Scroll and check that toolbar is collapsed.
  HideToolbarUsingUI();
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Rotate and check that toolbar is still collapsed.
  [EarlGrey rotateDeviceToOrientation:UIDeviceOrientationLandscapeLeft
                                error:nil];
  [ChromeEarlGreyUI waitForToolbarVisible:NO];

  // Cancel the rotation.
  [EarlGrey rotateDeviceToOrientation:UIDeviceOrientationPortrait error:nil];
}

// Tests that the toolbar reappears after backgrounding and foregrounding the
// app during or after a fast scroll.
- (void)testShowFullToolbarAfterBackgroundDuringFastScroll {
  std::map<GURL, std::string> responses;
  const GURL URL = web::test::HttpServer::MakeUrl("http://tallpage");
  responses[URL] =
      base::StringPrintf("<p style='height:%dem'>a</p><p>b</p>", kPageHeightEM);
  web::test::SetUpSimpleHttpServer(responses);

  [ChromeEarlGrey loadURL:URL];
  [ChromeEarlGreyUI waitForToolbarVisible:YES];

  [[EarlGrey selectElementWithMatcher:WebStateScrollViewMatcher()]
      performAction:grey_swipeFastInDirection(kGREYDirectionUp)];

  [[AppLaunchManager sharedManager] backgroundAndForegroundApp];

  [ChromeEarlGreyUI waitForToolbarVisible:YES];
}

@end

#pragma mark - Smooth scrolling enabled Tests

// Fullscreens tests for Chrome.
@interface FullscreenSmoothScrollingTestCase : ZZZFullscreenTestCase
@end

@implementation FullscreenSmoothScrollingTestCase

- (AppLaunchConfiguration)appConfigurationForTestCase {
  AppLaunchConfiguration config;
  config.features_enabled.push_back(web::features::kSmoothScrollingDefault);
  return config;
}

// This is currently needed to prevent this test case from being ignored.
- (void)testEmpty {
}

@end

#pragma mark - Bottom omnibox Tests

// Fullscreens tests for Chrome with bottom omnibox enabled by default.
@interface FullscreenBottomOmniboxTestCase : ZZZFullscreenTestCase
@end

@implementation FullscreenBottomOmniboxTestCase

- (void)setUp {
  [super setUp];
  [ChromeEarlGrey setBoolValue:YES forLocalStatePref:prefs::kBottomOmnibox];
}

// This is currently needed to prevent this test case from being ignored.
- (void)testEmpty {
}

- (void)testLongPDFScroll {
  [super testLongPDFScroll];
}

@end
