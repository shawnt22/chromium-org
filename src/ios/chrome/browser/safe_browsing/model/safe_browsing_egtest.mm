// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <memory>
#import <string>
#import <vector>

#import "base/check.h"
#import "base/strings/strcat.h"
#import "base/strings/string_util.h"
#import "base/strings/sys_string_conversions.h"
#import "base/test/ios/wait_util.h"
#import "base/time/time.h"
#import "components/enterprise/common/proto/synced/browser_events.pb.h"
#import "components/enterprise/common/proto/synced_from_google3/chrome_reporting_entity.pb.h"
#import "components/enterprise/common/proto/upload_request_response.pb.h"
#import "components/enterprise/connectors/core/common.h"
#import "components/enterprise/connectors/core/connectors_prefs.h"
#import "components/enterprise/connectors/core/realtime_reporting_test_environment.h"
#import "components/policy/core/common/policy_loader_ios_constants.h"
#import "components/policy/core/common/policy_types.h"
#import "components/safe_browsing/core/common/features.h"
#import "components/safe_browsing/core/common/safe_browsing_prefs.h"
#import "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/bookmarks/model/bookmark_storage_type.h"
#import "ios/chrome/browser/bookmarks/ui_bundled/bookmark_earl_grey.h"
#import "ios/chrome/browser/bookmarks/ui_bundled/bookmark_earl_grey_ui.h"
#import "ios/chrome/browser/infobars/ui_bundled/banners/infobar_banner_constants.h"
#import "ios/chrome/browser/metrics/model/metrics_app_interface.h"
#import "ios/chrome/browser/settings/ui_bundled/privacy/privacy_constants.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/chrome_test_case.h"
#import "ios/testing/earl_grey/app_launch_configuration.h"
#import "ios/testing/earl_grey/earl_grey_test.h"
#import "ios/web/common/features.h"
#import "ios/web/public/test/element_selector.h"
#import "net/test/embedded_test_server/embedded_test_server.h"
#import "net/test/embedded_test_server/http_request.h"
#import "net/test/embedded_test_server/http_response.h"
#import "ui/base/l10n/l10n_util.h"

using ::chrome::cros::reporting::proto::Event;
using ::chrome::cros::reporting::proto::EventResult;
using ::chrome::cros::reporting::proto::SafeBrowsingInterstitialEvent;
using InterstitialReason = ::chrome::cros::reporting::proto::
    SafeBrowsingInterstitialEvent::InterstitialReason;
using ::chrome::cros::reporting::proto::UploadEventsRequest;
using ::chrome::cros::reporting::proto::UrlFilteringInterstitialEvent;
using chrome_test_util::BackButton;
using chrome_test_util::ForwardButton;
using chrome_test_util::GREYAssertErrorNil;
using chrome_test_util::SettingsDoneButton;
using chrome_test_util::TappableBookmarkNodeWithLabel;
using enterprise_connectors::test::RealtimeReportingTestEnvironment;
using InterstitialThreatType = ::chrome::cros::reporting::proto::
    UrlFilteringInterstitialEvent_InterstitialThreatType;

namespace {

// Text that is found when expanding details on the phishing warning page.
const char kPhishingWarningDetails[] =
    "Google Safe Browsing, which recently found phishing";

// Text that is found when expanding details on the malware warning page.
const char kMalwareWarningDetails[] =
    "Google Safe Browsing, which recently found malware";

// Policy name and value for setting a fake enterprise enrollment token.
constexpr char kEnrollmentTokenPolicyName[] = "CloudManagementEnrollmentToken";
constexpr char kEnrollmentToken[] = "fake-enrollment-token";

// Text that is found on the enterprise warning page.
const char kEnterpriseWarningPage[] =
    "The site ahead is flagged by your organization";

// Text that is found on the enterprise block page.
const char kEnterpriseBlockPage[] =
    "The site ahead is blocked by your organization";

// Error message logged when the wrong number of Enterprise Reports were
// received.
NSString* kWrongNumberOfReportsErrorMessage = @"Wrong number of reports.";

// Id of the primary button in the security interstitial pages.
NSString* kPrimaryButtonID = @"primary-button";

// Duration to wait for an enterprise security event report.
constexpr base::TimeDelta kReportUploadTimeout = base::Seconds(15);

// Request handler for net::EmbeddedTestServer that returns the request URL's
// path as the body of the response if the request URL's path starts with
// "/echo". Otherwise, returns nulltpr to allow other handlers to handle the
// request.
std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
    const net::test_server::HttpRequest& request) {
  if (!base::StartsWith(request.relative_url, "/echo",
                        base::CompareCase::SENSITIVE)) {
    return nullptr;
  }
  auto http_response = std::make_unique<net::test_server::BasicHttpResponse>();
  http_response->set_content(request.relative_url);
  http_response->set_content_type("text/html");
  return http_response;
}

// Earl Grey matcher for the Enhanced Safe Browsing Infobar.
id<GREYMatcher> EnhancedSafeBrowsingInfobarButtonMatcher() {
  NSString* buttonLabel = l10n_util::GetNSString(
      IDS_IOS_SAFE_BROWSING_ENHANCED_PROTECTION_INFOBAR_BUTTON_TEXT);
  return grey_allOf(grey_accessibilityID(kInfobarBannerAcceptButtonIdentifier),
                    grey_accessibilityLabel(buttonLabel), nil);
}

// Enables the prefs needed for testing Enterprise Url Filtering.
void EnableEnterpriseUrlFilteringPrefs() {
  [ChromeEarlGrey
      setIntegerValue:enterprise_connectors::
                          REAL_TIME_CHECK_FOR_MAINFRAME_ENABLED
          forUserPref:enterprise_connectors::kEnterpriseRealTimeUrlCheckMode];
  [ChromeEarlGrey
      setIntegerValue:policy::POLICY_SCOPE_MACHINE
          forUserPref:enterprise_connectors::kEnterpriseRealTimeUrlCheckScope];
}

}  // namespace

// Tests Safe Browsing URL blocking.
@interface SafeBrowsingTestCase : ChromeTestCase {
  // A URL that is treated as an unsafe phishing page.
  GURL _phishingURL;
  // Text that is found on the phishing page.
  std::string _phishingContent;
  // A URL that is treated as an unsafe phishing page by real-time lookups.
  GURL _realTimePhishingURL;
  // Text that is found on the real-time phishing page.
  std::string _realTimePhishingContent;
  // A URL that is flagged by Enterprise real-time lookups.
  GURL _enterpriseWarnURL;
  // Text that is found in the page flagged by Enterprise lookups.
  std::string _enterpriseWarnContent;
  // A URL that is blocked by Enterprise real-time lookups.
  GURL _enterpriseBlockURL;
  // Text that is found in the page blocked by Enterprise lookups.
  std::string _enterpriseBlockContent;

  // A URL that is treated as an unsafe malware page.
  GURL _malwareURL;
  // Text that is found on the malware page.
  std::string _malwareContent;
  // A URL of a page with an iframe that is treated as a phishing page.
  GURL _iframeWithPhishingURL;
  // Text that is found on the iframe that is treated as a phishing page.
  std::string _iframeWithPhishingContent;
  // A URL that is treated as a safe page.
  GURL _safeURL1;
  // Text that is found on the safe page.
  std::string _safeContent1;
  // Another URL that is treated as a safe page.
  GURL _safeURL2;
  // Text that is found on the safe page.
  std::string _safeContent2;
  // The default value for SafeBrowsingEnabled pref.
  BOOL _safeBrowsingEnabledPrefDefault;
  // The default value for SafeBrowsingEnhanced pref.
  BOOL _safeBrowsingEnhancedPrefDefault;
  // The default value for SafeBrowsingProceedAnywayDisabled pref.
  BOOL _proceedAnywayDisabledPrefDefault;
  // Fake servers for testing enterprise security event reporting.
  std::unique_ptr<RealtimeReportingTestEnvironment> _reportingEnvironment;
}
@end

@implementation SafeBrowsingTestCase

- (AppLaunchConfiguration)appConfigurationForTestCase {
  AppLaunchConfiguration config;

  // Use commandline args to insert fake unsafe URLs into the Safe Browsing
  // database.
  config.additional_args.push_back(std::string("--mark_as_phishing=") +
                                   _phishingURL.spec());
  config.additional_args.push_back(std::string("--mark_as_malware=") +
                                   _malwareURL.spec());
  config.additional_args.push_back(
      std::string("--mark_as_hash_prefix_real_time_phishing=") +
      _phishingURL.spec());

  // Disable HPRT for malware URL related tests since artificial verdict caching
  // for HPRT marks a URL for phishing which breaks tests. Additionally, this
  // promotes continued test coverage for the non-HPRT logic path.
  if ([self
          isRunningTest:@selector(testRestoreToWarningPagePreservesHistory)] ||
      [self isRunningTest:@selector(testMalwarePage)] ||
      [self isRunningTest:@selector(testProceedingPastMalwareWarning)] ||
      [self
          isRunningTest:@selector(testProceedingPastMalwareWarningReported)]) {
    config.additional_args.push_back(std::string(
        "--disable-features=SafeBrowsingHashPrefixRealTimeLookups"));
  } else {
    config.additional_args.push_back(
        std::string("--enable-features=SafeBrowsingHashPrefixRealTimeLookups"));
  }

  if ([self isRunningEnterpriseReportingTest]) {
    CHECK(_reportingEnvironment);
    std::vector<std::string> reporting_args =
        _reportingEnvironment->GetArguments();
    config.additional_args.insert(config.additional_args.end(),
                                  reporting_args.begin(), reporting_args.end());
    config.additional_args.push_back(base::StrCat(
        {"-", base::SysNSStringToUTF8(kPolicyLoaderIOSConfigurationKey)}));
    config.additional_args.push_back(
        base::StrCat({"<dict><key>", kEnrollmentTokenPolicyName,
                      "</key><string>", kEnrollmentToken, "</string></dict>"}));
  }

  if ([self isRunningEntepriseUrlFilteringTest]) {
    // Enable url filtering feature flag.
    config.additional_args.push_back(
        std::string("--enable-features=IOSEnterpriseRealtimeUrlFiltering"));
  }

  if ([self isRunningTest:@selector(testEnterpriseBlockingPage)]) {
    config.additional_args.push_back(
        std::string("--mark_as_enterprise_blocked=") +
        _enterpriseBlockURL.spec());
  } else if ([self isRunningTest:@selector(testEnterpriseWarningPage)] ||
             [self isRunningTest:@selector(testEnterpriseWarningPageBypass)]) {
    config.additional_args.push_back(
        std::string("--mark_as_enterprise_warned=") +
        _enterpriseWarnURL.spec());

  } else {
    config.additional_args.push_back(
        std::string("--mark_as_real_time_phishing=") +
        _realTimePhishingURL.spec());
  }

  config.additional_args.push_back(
      std::string("--mark_as_allowlisted_for_real_time=") + _safeURL1.spec());
  config.relaunch_policy = NoForceRelaunchAndResetState;
  return config;
}

- (void)setUp {
  self.testServer->RegisterRequestHandler(base::BindRepeating(&HandleRequest));
  bool started = self.testServer->Start();
  _phishingURL = self.testServer->GetURL("/echo_phishing_page");
  _phishingContent = "phishing_page";

  _realTimePhishingURL = self.testServer->GetURL("/echo_realtime_page");
  _realTimePhishingContent = "realtime_page";

  _enterpriseWarnURL = self.testServer->GetURL("/echo_warn_page");
  _enterpriseWarnContent = "echo_warn_page";

  _enterpriseBlockURL = self.testServer->GetURL("/echo_block_page");
  _enterpriseBlockContent = "echo_block_page";

  _malwareURL = self.testServer->GetURL("/echo_malware_page");
  _malwareContent = "malware_page";

  _iframeWithPhishingURL =
      self.testServer->GetURL("/iframe?" + _phishingURL.spec());
  _iframeWithPhishingContent = _phishingContent;

  _safeURL1 = self.testServer->GetURL("/echo_safe_page");
  _safeContent1 = "safe_page";

  _safeURL2 = self.testServer->GetURL("/echo_also_safe");
  _safeContent2 = "also_safe";

  // Artificial verdict caching for hash prefix real time causes URLs with the
  // same host to be seen as unsafe. Replacing the host string with localhost
  // allows for proper testing between safe browsing v5 and iframe queries.
  GURL::Replacements replacements;
  replacements.SetHostStr("localhost");
  _safeURL1 = _safeURL1.ReplaceComponents(replacements);
  _safeURL2 = _safeURL2.ReplaceComponents(replacements);
  _iframeWithPhishingURL =
      _iframeWithPhishingURL.ReplaceComponents(replacements);

  if ([self isRunningEnterpriseReportingTest]) {
    // `GREYAssertTrue` can't be used before the superclass's `-setUp` call is
    // complete, so fall back to `CHECK()`.
    _reportingEnvironment = RealtimeReportingTestEnvironment::Create(
        {"interstitialEvent", "urlFilteringInterstitialEvent"},
        {{"interstitialEvent", {"*"}}});
    CHECK(_reportingEnvironment);
    CHECK(_reportingEnvironment->Start());
  }

  // `appConfigurationForTestCase` is called during [super setUp], and
  // depends on the URLs initialized above.
  [super setUp];

  // GREYAssertTrue cannot be called before [super setUp].
  GREYAssertTrue(started, @"Test server failed to start.");

  // Save the existing value of the pref to set it back in tearDown.
  _safeBrowsingEnabledPrefDefault =
      [ChromeEarlGrey userBooleanPref:prefs::kSafeBrowsingEnabled];
  // Ensure that Safe Browsing opt-out starts in its default (opted-out) state.
  [ChromeEarlGrey setBoolValue:YES forUserPref:prefs::kSafeBrowsingEnabled];

  // Save the existing value of the pref to set it back in tearDown.
  _safeBrowsingEnhancedPrefDefault =
      [ChromeEarlGrey userBooleanPref:prefs::kSafeBrowsingEnhanced];
  // Ensure that Enhanced Safe Browsing opt-out starts in its default (opted-in)
  // state.
  [ChromeEarlGrey setBoolValue:NO forUserPref:prefs::kSafeBrowsingEnhanced];

  // Save the existing value of the pref to set it back in tearDown.
  _proceedAnywayDisabledPrefDefault = [ChromeEarlGrey
      userBooleanPref:prefs::kSafeBrowsingProceedAnywayDisabled];
  // Ensure that Proceed link is shown by default in the safe browsing warning.
  [ChromeEarlGrey setBoolValue:NO
                   forUserPref:prefs::kSafeBrowsingProceedAnywayDisabled];

  // Ensure that the real-time Safe Browsing opt-in starts in the default
  // (opted-out) state.
  [ChromeEarlGrey setURLKeyedAnonymizedDataCollectionEnabled:NO];

  // Set up histograms for testing enterprise reporting.
  GREYAssertErrorNil([MetricsAppInterface setupHistogramTester]);
}

- (void)tearDownHelper {
  GREYAssertErrorNil([MetricsAppInterface releaseHistogramTester]);

  // Ensure that Safe Browsing is reset to its original value.
  [ChromeEarlGrey setBoolValue:_safeBrowsingEnabledPrefDefault
                   forUserPref:prefs::kSafeBrowsingEnabled];

  // Ensure that Enhanced Safe Browsing is reset to its original value.
  [ChromeEarlGrey setBoolValue:_safeBrowsingEnhancedPrefDefault
                   forUserPref:prefs::kSafeBrowsingEnhanced];

  // Ensure that Proceed link is reset to its original value.
  [ChromeEarlGrey setBoolValue:_proceedAnywayDisabledPrefDefault
                   forUserPref:prefs::kSafeBrowsingProceedAnywayDisabled];

  // Restore Enteprise URL Filtering prefs to their default values.
  [ChromeEarlGrey clearUserPrefWithName:enterprise_connectors::
                                            kEnterpriseRealTimeUrlCheckMode];
  [ChromeEarlGrey clearUserPrefWithName:enterprise_connectors::
                                            kEnterpriseRealTimeUrlCheckScope];

  // Ensure that the real-time Safe Browsing opt-in is reset to its original
  // value.
  [ChromeEarlGrey setURLKeyedAnonymizedDataCollectionEnabled:NO];

  [super tearDownHelper];
}

#pragma mark - Helper methods

// Instantiates an ElementSelector to detect the enhanced protection message on
// interstitial page.
- (ElementSelector*)enhancedProtectionMessage {
  NSString* selector =
      @"(function() {"
       "  var element = document.getElementById('enhanced-protection-message');"
       "  if (element == null) return false;"
       "  if (element.classList.contains('hidden')) return false;"
       "  return true;"
       "})()";
  NSString* description = @"Enhanced Safe Browsing message.";
  return [ElementSelector selectorWithScript:selector
                         selectorDescription:description];
}

- (BOOL)isRunningEnterpriseReportingTest {
  return [self isRunningTest:@selector
               (testProceedingPastPhishingWarningReported)] ||
         [self isRunningTest:@selector
               (testProceedingPastMalwareWarningReported)] ||
         [self isRunningTest:@selector(testEnterpriseBlockingPage)] ||
         [self isRunningTest:@selector(testEnterpriseWarningPage)] ||
         [self isRunningTest:@selector(testEnterpriseWarningPageBypass)];
}

- (BOOL)isRunningEntepriseUrlFilteringTest {
  return [self isRunningTest:@selector(testEnterpriseBlockingPage)] ||
         [self isRunningTest:@selector(testEnterpriseWarningPage)] ||
         [self isRunningTest:@selector(testEnterpriseWarningPageBypass)];
}
- (void)waitForEnterpriseReports:(int)count {
  // Use metrics to detect that the report upload completed. This is the best
  // known way to wait because a task environment isn't available here for the
  // server's request handler to post to.
  GREYAssertTrue(
      base::test::ios::WaitUntilConditionOrTimeout(
          kReportUploadTimeout,
          ^{
            NSError* error = [MetricsAppInterface
                expectTotalCount:count
                    forHistogram:@"Enterprise.ReportingEventUploadSuccess"];
            return error == nil;
          }),
      @"Timed out uploading security event.");
  GREYAssertErrorNil([MetricsAppInterface
      expectTotalCount:0
          forHistogram:@"Enterprise.ReportingEventUploadFailure"]);
}

- (void)assertInterstitialEvent:(const UploadEventsRequest&)request
                            url:(const GURL&)url
                 clickedThrough:(BOOL)clickedThrough
                    eventResult:(EventResult)eventResult
                         reason:(InterstitialReason)reason {
  GREYAssertEqual(std::string("iOS"), request.device().os_platform(),
                  @"Wrong OS platform in report.");
  GREYAssertEqual(1, request.events_size(), @"Wrong number of events.");

  GREYAssertTrue(request.events(0).has_interstitial_event(),
                 @"Wrong event type.");
  const SafeBrowsingInterstitialEvent& event =
      request.events(0).interstitial_event();
  GREYAssertEqual(url, GURL(event.url()), @"Wrong interstitial event URL.");
  if (clickedThrough) {
    GREYAssertTrue(
        event.clicked_through(),
        @"Interstitial event unexpectedly not marked as clicked through.");
  } else {
    GREYAssertFalse(
        event.clicked_through(),
        @"Interstitial event unexpectedly marked as clicked through.");
  }
  GREYAssertEqual(eventResult, event.event_result(),
                  @"Wrong interstitial event result.");
  GREYAssertEqual(reason, event.reason(), @"Wrong interstitial event reason.");
}

// Asserts that the given `request` contains a UrlFilteringInterstitialEvent
// with the given `url`, `clickedThrough`, `eventResult` and `threatType`.
- (void)assertUrlFilteringInterstitialEvent:(const UploadEventsRequest&)request
                                        url:(const GURL&)url
                             clickedThrough:(BOOL)clickedThrough
                                eventResult:(EventResult)eventResult
                                 threatType:(InterstitialThreatType)threatType {
  GREYAssertEqual(std::string("iOS"), request.device().os_platform(),
                  @"Wrong OS platform in report.");
  GREYAssertEqual(1, request.events_size(), @"Wrong number of events.");

  GREYAssertTrue(request.events(0).has_url_filtering_interstitial_event(),
                 @"Wrong event type.");
  const UrlFilteringInterstitialEvent& event =
      request.events(0).url_filtering_interstitial_event();
  GREYAssertEqual(url, GURL(event.url()), @"Wrong interstitial event URL.");
  if (clickedThrough) {
    GREYAssertTrue(event.clicked_through(),
                   @"Url filtering interstitial event unexpectedly not marked "
                   @"as clicked through.");
  } else {
    GREYAssertFalse(event.clicked_through(),
                    @"Url filtering interstitial event unexpectedly marked as "
                    @"clicked through.");
  }
  GREYAssertEqual(eventResult, event.event_result(),
                  @"Wrong url filtering interstitial event result.");
  GREYAssertEqual(threatType, event.threat_type(),
                  @"Wrong interstitial event reason.");
}

#pragma mark - Tests
// Tests that safe pages are not blocked.
- (void)testSafePage {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
}

// Tests that a phishing page is blocked, and the "Back to safety" button on
// the warning page works as expected.
- (void)testPhishingPage {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the phishing page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the "Back to safety" button and verify that the previous page's
  // contents are loaded.
  [ChromeEarlGrey tapWebStateElementWithID:kPrimaryButtonID];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
}

// Tests expanding the details on a phishing warning, and proceeding past the
// warning. Also verifies that a warning is still shown when visiting the unsafe
// URL in a new tab.
- (void)testProceedingPastPhishingWarning {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the phishing page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kPhishingWarningDetails];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateContainingText:_phishingContent];

  // In a new tab, a warning should still be shown, even though the user
  // proceeded to the unsafe content in the other tab.
  [ChromeEarlGrey openNewTab];
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
}

// Tests expanding the details on a phishing warning, and proceeding past the
// warning in incognito mode. Also verifies that a warning is still shown when
// visiting the unsafe URL in a new incognito tab.
- (void)testProceedingPastPhishingWarningInIncognito {
  [ChromeEarlGrey openNewIncognitoTab];
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the phishing page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kPhishingWarningDetails];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateContainingText:_phishingContent];

  // In a new tab, a warning should still be shown, even though the user
  // proceeded to the unsafe content in the other tab.
  [ChromeEarlGrey openNewIncognitoTab];
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
}

// Tests expanding the details on a phishing warning, and proceeding past the
// warning is reported to an enterprise connector.
- (void)testProceedingPastPhishingWarningReported {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the phishing page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kPhishingWarningDetails];

  // Verify the server is notified the browser showed a warning.
  [self waitForEnterpriseReports:1];
  std::vector<UploadEventsRequest> requests =
      _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(1U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertInterstitialEvent:requests[0]
                            url:_phishingURL
                 clickedThrough:NO
                    eventResult:EventResult::EVENT_RESULT_WARNED
                         reason:SafeBrowsingInterstitialEvent::
                                    SOCIAL_ENGINEERING];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateContainingText:_phishingContent];

  // Verify the server is notified the end user bypassed the warning.
  [self waitForEnterpriseReports:2];
  requests = _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(2U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertInterstitialEvent:requests[1]
                            url:_phishingURL
                 clickedThrough:YES
                    eventResult:EventResult::EVENT_RESULT_BYPASSED
                         reason:SafeBrowsingInterstitialEvent::
                                    SOCIAL_ENGINEERING];
}

// Tests that a malware page is blocked, and the "Back to safety" button on the
// warning page works as expected.
- (void)testMalwarePage {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_malwareURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the "Back to safety" button and verify that the previous page's
  // contents are loaded.
  [ChromeEarlGrey tapWebStateElementWithID:kPrimaryButtonID];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
}

// Tests expanding the details on a malware warning, proceeding past the
// warning, and navigating back/forward to the unsafe page.
- (void)testProceedingPastMalwareWarning {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_malwareURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kMalwareWarningDetails];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateFrameContainingText:_malwareContent];

  // Verify that no warning is shown when navigating back and then forward to
  // the unsafe page.
  [ChromeEarlGrey goBack];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
  [ChromeEarlGrey goForward];
  [ChromeEarlGrey waitForWebStateContainingText:_malwareContent];

  // Visit another safe page, and then navigate back to the unsafe page and
  // verify that no warning is shown.
  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
  [ChromeEarlGrey goBack];
  [ChromeEarlGrey waitForWebStateContainingText:_malwareContent];
}

// Tests expanding the details on a malware warning, proceeding past the
// warning, and navigating back/forward to the unsafe page, in incognito mode.
- (void)testProceedingPastMalwareWarningInIncognito {
  [ChromeEarlGrey openNewIncognitoTab];
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_malwareURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kMalwareWarningDetails];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateFrameContainingText:_malwareContent];

  // Verify that no warning is shown when navigating back and then forward to
  // the unsafe page.
  [[EarlGrey selectElementWithMatcher:BackButton()] performAction:grey_tap()];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
  [[EarlGrey selectElementWithMatcher:ForwardButton()]
      performAction:grey_tap()];
  [ChromeEarlGrey waitForWebStateContainingText:_malwareContent];

  // Visit another safe page, and then navigate back to the unsafe page and
  // verify that no warning is shown.
  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
  [[EarlGrey selectElementWithMatcher:BackButton()] performAction:grey_tap()];
  [ChromeEarlGrey waitForWebStateContainingText:_malwareContent];
}

// Tests expanding the details on a malware warning, and proceeding past the
// warning is reported to an enterprise connector.
- (void)testProceedingPastMalwareWarningReported {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_malwareURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kMalwareWarningDetails];

  // Verify the server is notified the browser showed a warning.
  [self waitForEnterpriseReports:1];
  std::vector<UploadEventsRequest> requests =
      _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(1U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertInterstitialEvent:requests[0]
                            url:_malwareURL
                 clickedThrough:NO
                    eventResult:EventResult::EVENT_RESULT_WARNED
                         reason:SafeBrowsingInterstitialEvent::MALWARE];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateFrameContainingText:_malwareContent];

  // Verify the server is notified the end user bypassed the warning.
  [self waitForEnterpriseReports:2];
  requests = _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(2U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertInterstitialEvent:requests[1]
                            url:_malwareURL
                 clickedThrough:YES
                    eventResult:EventResult::EVENT_RESULT_BYPASSED
                         reason:SafeBrowsingInterstitialEvent::MALWARE];
}

// Tests that disabling and re-enabling Safe Browsing works as expected.
- (void)testDisableAndEnableSafeBrowsing {
  // Disable Safe Browsing and verify that unsafe content is shown.
  [ChromeEarlGrey setBoolValue:NO forUserPref:prefs::kSafeBrowsingEnabled];
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:_phishingContent];

  // Re-enable Safe Browsing and verify that a warning is shown for unsafe
  // content.
  [ChromeEarlGrey setBoolValue:YES forUserPref:prefs::kSafeBrowsingEnabled];
  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
}

// Tests enabling Enhanced Protection from a Standard Protection state (Default
// state) from the interstitial blocking page.
- (void)testDisableAndEnableEnhancedSafeBrowsing {
  BOOL isInfobarEnabled = [ChromeEarlGrey isEnhancedSafeBrowsingInfobarEnabled];
  // Disable Enhanced Safe Browsing and verify that a dark red box prompting to
  // turn on Enhanced Protection is visible.
  [ChromeEarlGrey setBoolValue:NO forUserPref:prefs::kSafeBrowsingEnhanced];
  ElementSelector* enhancedSafeBrowsingMessage =
      [self enhancedProtectionMessage];

  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
  [ChromeEarlGrey loadURL:_phishingURL];
  if (isInfobarEnabled) {
    [ChromeEarlGrey waitForMatcher:EnhancedSafeBrowsingInfobarButtonMatcher()];
  } else {
    [ChromeEarlGrey
        waitForWebStateContainingElement:enhancedSafeBrowsingMessage];
  }

  // Re-enable Enhanced Safe Browsing and verify that a dark red box prompting
  // to turn on Enhanced Protection is not visible.
  [ChromeEarlGrey setBoolValue:YES forUserPref:prefs::kSafeBrowsingEnhanced];
  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
  [ChromeEarlGrey loadURL:_realTimePhishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
  if (isInfobarEnabled) {
    [ChromeEarlGrey waitForUIElementToDisappearWithMatcher:
                        EnhancedSafeBrowsingInfobarButtonMatcher()];
  } else {
    [ChromeEarlGrey
        waitForWebStateNotContainingElement:enhancedSafeBrowsingMessage];
  }
}

- (void)testEnhancedSafeBrowsingLink {
  BOOL isInfobarEnabled = [ChromeEarlGrey isEnhancedSafeBrowsingInfobarEnabled];
  // Disable Enhanced Safe Browsing and verify that a dark red box prompting to
  // turn on Enhanced Protection is visible.
  [ChromeEarlGrey setBoolValue:NO forUserPref:prefs::kSafeBrowsingEnhanced];
  ElementSelector* enhancedSafeBrowsingMessage =
      [self enhancedProtectionMessage];

  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
  [ChromeEarlGrey loadURL:_phishingURL];
  if (isInfobarEnabled) {
    [[EarlGrey
        selectElementWithMatcher:EnhancedSafeBrowsingInfobarButtonMatcher()]
        performAction:grey_tap()];
  } else {
    [ChromeEarlGrey
        waitForWebStateContainingElement:enhancedSafeBrowsingMessage];
    [ChromeEarlGrey tapWebStateElementWithID:@"enhanced-protection-link"];
  }

  [[EarlGrey
      selectElementWithMatcher:
          grey_accessibilityID(kSettingsSafeBrowsingEnhancedProtectionCellId)]
      performAction:grey_tap()];
  GREYAssertTrue([ChromeEarlGrey userBooleanPref:prefs::kSafeBrowsingEnhanced],
                 @"Failed to toggle-on Enhanced Safe Browsing");
  [[EarlGrey selectElementWithMatcher:SettingsDoneButton()]
      performAction:grey_tap()];

  // Verify that a dark red box prompting to turn on Enhanced Protection is not
  // visible.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
  if (isInfobarEnabled) {
    [ChromeEarlGrey waitForUIElementToDisappearWithMatcher:
                        EnhancedSafeBrowsingInfobarButtonMatcher()];
  } else {
    [ChromeEarlGrey
        waitForWebStateNotContainingElement:enhancedSafeBrowsingMessage];
  }
}

// Tests displaying a warning for an unsafe page in incognito mode, and
// proceeding past the warning.
- (void)testWarningInIncognito {
  [ChromeEarlGrey openNewIncognitoTab];
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the phishing page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the Details button and verify that warning details are shown.
  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kPhishingWarningDetails];

  // Tap on the link to proceed to the unsafe page, and verify that this page is
  // loaded.
  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-link"];
  [ChromeEarlGrey waitForWebStateContainingText:_phishingContent];
}

// Tests that the proceed option is not shown when
// kSafeBrowsingProceedAnywayDisabled is enabled.
- (void)testProceedAlwaysDisabled {
  // Enable the pref.
  [ChromeEarlGrey setBoolValue:YES
                   forUserPref:prefs::kSafeBrowsingProceedAnywayDisabled];

  // Load the a malware safe browsing error page.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:"Dangerous site"];

  [ChromeEarlGrey tapWebStateElementWithID:@"details-button"];
  [ChromeEarlGrey waitForWebStateContainingText:kPhishingWarningDetails];

  // Verify that the proceed-link element is not found.  When the proceed link
  // is disabled, the entire second paragraph hidden.
  NSString* selector =
      @"(function() {"
       "  var element = document.getElementById('final-paragraph');"
       "  if (element.classList.contains('hidden')) return true;"
       "  return false;"
       "})()";
  NSString* description = @"Hidden proceed-anyway link.";
  ElementSelector* proceedLink =
      [ElementSelector selectorWithScript:selector
                      selectorDescription:description];
  GREYAssert(
      [ChromeEarlGrey webStateContainsElement:proceedLink],
      @"Proceed anyway link shown despite kSafeBrowsingProceedAnywayDisabled");
}

// Tests performing a back navigation to a warning page and a forward navigation
// from a warning page.
- (void)testBackForwardNavigationWithWarning {
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_phishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
  // TODO(crbug.com/40159013): Adding a delay to avoid never-ending load on the
  // last navigation forward. Should be fixed in newer iOS version.
  base::test::ios::SpinRunLoopWithMinDelay(base::Seconds(1));

  [ChromeEarlGrey goBack];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
  // TODO(crbug.com/40159013): Adding a delay to avoid never-ending load on the
  // last navigation forward. Should be fixed in newer iOS version.
  base::test::ios::SpinRunLoopWithMinDelay(base::Seconds(1));

  [ChromeEarlGrey goForward];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
}

// Tests performing a back navigation to a warning page and a forward navigation
// from a warning page, in incognito mode.
- (void)testBackForwardNavigationWithWarningInIncognito {
  [ChromeEarlGrey openNewIncognitoTab];
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_malwareURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];

  [[EarlGrey selectElementWithMatcher:BackButton()] performAction:grey_tap()];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  [[EarlGrey selectElementWithMatcher:ForwardButton()]
      performAction:grey_tap()];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
}

// Tests that performing session restoration to a Safe Browsing warning page
// preserves navigation history.
- (void)testRestoreToWarningPagePreservesHistory {
  // TODO(crbug.com/41489568): Test fails on iOS 18.4. Re-enable the test.
  if (@available(iOS 18.4, *)) {
    EARL_GREY_TEST_DISABLED(@"Fails on iOS 18.4.");
  }

  // Build up navigation history that consists of a safe URL, a warning page,
  // and another safe URL.
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the malware page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_malwareURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Tap on the "Back to safety" button and verify that the previous page's
  // contents are loaded.
  [ChromeEarlGrey tapWebStateElementWithID:kPrimaryButtonID];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  [ChromeEarlGrey goForward];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];

  // Navigate back so that both the back list and the forward list are
  // non-empty.
  [ChromeEarlGrey goBack];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Do a session restoration and verify that all navigation history is
  // preserved.
  [self triggerRestoreByRestartingApplication];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  [ChromeEarlGrey goBack];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  [ChromeEarlGrey goForward];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  [ChromeEarlGrey goForward];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
}

// Tests that a page with an unsafe ifame is not blocked.
- (void)testPageWithUnsafeIframeSkipSubresources {
  [ChromeEarlGrey loadURL:_safeURL2];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent2];
  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load a page that has an iframe with malware, and verify that a warning is
  // not shown.
  [ChromeEarlGrey loadURL:_iframeWithPhishingURL];
  [ChromeEarlGrey
      waitForWebStateFrameContainingText:_iframeWithPhishingContent];
}

// Tests that real-time lookups are not performed when opted-out of real-time
// lookups.
- (void)testRealTimeLookupsWhileOptedOut {
  // Load the real-time phishing page and verify that no warning is shown.
  [ChromeEarlGrey loadURL:_realTimePhishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:_realTimePhishingContent];
}

// Tests that real-time lookups are not performed when opted-out of Safe
// Browsing, regardless of the state of the real-time opt-in.
- (void)testRealTimeLookupsWhileOptedOutOfSafeBrowsing {
  // Opt out of Safe Browsing.
  [ChromeEarlGrey setBoolValue:NO forUserPref:prefs::kSafeBrowsingEnabled];

  // Load the real-time phishing page and verify that no warning is shown.
  [ChromeEarlGrey loadURL:_realTimePhishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:_realTimePhishingContent];

  // Opt-in to real-time checks and verify that it's still the case that no
  // warning is shown.
  [ChromeEarlGrey setURLKeyedAnonymizedDataCollectionEnabled:YES];
  [ChromeEarlGrey openNewTab];
  [ChromeEarlGrey loadURL:_realTimePhishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:_realTimePhishingContent];
}

// Tests that a page identified as unsafe by real-time Safe Browsing is blocked
// when opted-in to real-time lookups.
- (void)testRealTimeLookupsWhileOptedIn {
  // Opt-in to real-time checks.
  [ChromeEarlGrey setURLKeyedAnonymizedDataCollectionEnabled:YES];

  // Load the real-time phishing page and verify that a warning page is shown.
  [ChromeEarlGrey loadURL:_realTimePhishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];
}

// Tests that real-time lookups are not performed in incognito mode.
- (void)testRealTimeLookupsInIncognito {
  // Opt-in to real-time checks.
  [ChromeEarlGrey setURLKeyedAnonymizedDataCollectionEnabled:YES];

  // Load the real-time phishing page and verify that no warning is shown.
  [ChromeEarlGrey openNewIncognitoTab];
  [ChromeEarlGrey loadURL:_realTimePhishingURL];
  [ChromeEarlGrey waitForWebStateContainingText:_realTimePhishingContent];
}

// Tests that when a page identified as unsafe by real-time Safe Browsing is
// loaded using a bookmark, a warning is shown.
- (void)testRealTimeWarningForBookmark {
  NSString* phishingTitle = @"Real-time phishing";
  [BookmarkEarlGrey
      addBookmarkWithTitle:phishingTitle
                       URL:base::SysUTF8ToNSString(_realTimePhishingURL.spec())
                 inStorage:BookmarkStorageType::kLocalOrSyncable];
  // Opt-in to real-time checks.
  [ChromeEarlGrey setURLKeyedAnonymizedDataCollectionEnabled:YES];

  // Load the real-time phishing page using its bookmark, and verify that a
  // warning is shown.
  [BookmarkEarlGreyUI openBookmarks];
  [BookmarkEarlGreyUI openMobileBookmarks];
  [[EarlGrey
      selectElementWithMatcher:TappableBookmarkNodeWithLabel(phishingTitle)]
      performAction:grey_tap()];
  [ChromeEarlGrey waitForWebStateContainingText:l10n_util::GetStringUTF8(
                                                    IDS_SAFEBROWSING_HEADING)];

  // Remove bookmarked phishing site.
  [BookmarkEarlGrey clearBookmarks];
}

// Verifies that the Enteprise warning interstitial is displayed for urls
// flagged by Enterprise organizations.
- (void)testEnterpriseWarningPage {
  EnableEnterpriseUrlFilteringPrefs();

  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the enterprise flagged page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_enterpriseWarnURL];
  [ChromeEarlGrey waitForWebStateContainingText:kEnterpriseWarningPage];

  // Verify the server is notified the browser blocked a navigation.
  [self waitForEnterpriseReports:1];
  std::vector<UploadEventsRequest> requests =
      _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(1U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertUrlFilteringInterstitialEvent:requests[0]
                                        url:_enterpriseWarnURL
                             clickedThrough:NO
                                eventResult:EventResult::EVENT_RESULT_WARNED
                                 threatType:UrlFilteringInterstitialEvent::
                                                ENTERPRISE_WARNED_SEEN];

  // Tap on the "Go back" button and verify that the previous page's
  // contents are loaded.
  [ChromeEarlGrey tapWebStateElementWithID:kPrimaryButtonID];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
}

// Verifies that the Enteprise warning interstitial allows to bypass the warning
// and navigate to urls flagged by Enterprise organizations.
- (void)testEnterpriseWarningPageBypass {
  EnableEnterpriseUrlFilteringPrefs();

  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the enterprise flagged page and verify a warning is shown.
  [ChromeEarlGrey loadURL:_enterpriseWarnURL];
  [ChromeEarlGrey waitForWebStateContainingText:kEnterpriseWarningPage];

  // Verify the server is notified the browser flagged a navigation.
  [self waitForEnterpriseReports:1];
  std::vector<UploadEventsRequest> requests =
      _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(1U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertUrlFilteringInterstitialEvent:requests[0]
                                        url:_enterpriseWarnURL
                             clickedThrough:NO
                                eventResult:EventResult::EVENT_RESULT_WARNED
                                 threatType:UrlFilteringInterstitialEvent::
                                                ENTERPRISE_WARNED_SEEN];

  [ChromeEarlGrey tapWebStateElementWithID:@"proceed-button"];
  [ChromeEarlGrey waitForWebStateContainingText:_enterpriseWarnContent];

  // Verify the server is notified about the user bypassing a flagged a
  // navigation.
  [self waitForEnterpriseReports:2];
  requests = _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(2U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertUrlFilteringInterstitialEvent:requests[1]
                                        url:_enterpriseWarnURL
                             clickedThrough:YES
                                eventResult:EventResult::EVENT_RESULT_BYPASSED
                                 threatType:UrlFilteringInterstitialEvent::
                                                ENTERPRISE_WARNED_BYPASS];

  // Load the enterprise flagged page should go directly to the page after the
  // warning was bypassed.
  [ChromeEarlGrey loadURL:_enterpriseWarnURL];
  [ChromeEarlGrey waitForWebStateContainingText:_enterpriseWarnContent];
}

// Verifies that the Enteprise blocking interstitial is displayed for urls
// blocked by Enterprise organizations.
- (void)testEnterpriseBlockingPage {
  EnableEnterpriseUrlFilteringPrefs();

  [ChromeEarlGrey loadURL:_safeURL1];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];

  // Load the enterprise blocked page and verify a blocking interstitial is
  // shown.
  [ChromeEarlGrey loadURL:_enterpriseBlockURL];
  [ChromeEarlGrey waitForWebStateContainingText:kEnterpriseBlockPage];

  // Verify the server is notified the browser blocked a navigation.
  [self waitForEnterpriseReports:1];
  std::vector<UploadEventsRequest> requests =
      _reportingEnvironment->reporting_server()->GetUploadedReports();
  GREYAssertEqual(1U, requests.size(), kWrongNumberOfReportsErrorMessage);
  [self assertUrlFilteringInterstitialEvent:requests[0]
                                        url:_enterpriseBlockURL
                             clickedThrough:NO
                                eventResult:EventResult::EVENT_RESULT_BLOCKED
                                 threatType:UrlFilteringInterstitialEvent::
                                                ENTERPRISE_BLOCKED_SEEN];

  // Tap on the "Go back" button and verify that the previous page's
  // contents are loaded.
  [ChromeEarlGrey tapWebStateElementWithID:kPrimaryButtonID];
  [ChromeEarlGrey waitForWebStateContainingText:_safeContent1];
}

@end
