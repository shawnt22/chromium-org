// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/supervised_user/classify_url_navigation_throttle.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "build/build_config.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_test_util.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/safe_search_api/fake_url_checker_client.h"
#include "components/supervised_user/core/browser/supervised_user_preferences.h"
#include "components/supervised_user/core/browser/supervised_user_service.h"
#include "components/supervised_user/core/browser/supervised_user_url_filter.h"
#include "components/supervised_user/core/browser/supervised_user_utils.h"
#include "components/supervised_user/core/common/supervised_user_constants.h"
#include "components/supervised_user/test_support/kids_management_api_server_mock.h"
#include "components/supervised_user/test_support/supervised_user_url_filter_test_utils.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/mock_navigation_throttle_registry.h"
#include "content/public/test/navigation_simulator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace supervised_user {

namespace {

static const char* kExampleURL = "https://example.com/";
static const char* kExample1URL = "https://example1.com/";
static const char* kExample2URL = "https://example2.com/";

void ExpectThrottleStatus(base::HistogramTester* tester,
                          std::map<ClassifyUrlThrottleStatus, int> buckets) {
  int total = 0;
  for (const auto& [bucket, count] : buckets) {
    total += count;
    tester->ExpectBucketCount(kClassifyUrlThrottleStatusHistogramName, bucket,
                              count);
  }
  tester->ExpectTotalCount(kClassifyUrlThrottleStatusHistogramName, total);
}

void ExpectNoLatencyRecorded(base::HistogramTester* tester) {
  tester->ExpectTotalCount(kClassifiedEarlierThanContentResponseHistogramName,
                           /*expected_count=*/0);
  tester->ExpectTotalCount(kClassifiedLaterThanContentResponseHistogramName,
                           /*expected_count=*/0);
}

class MockSupervisedUserURLFilter : public SupervisedUserURLFilter {
 public:
  explicit MockSupervisedUserURLFilter(
      PrefService& prefs,
      std::unique_ptr<SupervisedUserURLFilter::Delegate> delegate,
      std::unique_ptr<safe_search_api::URLCheckerClient> checker_client)
      : SupervisedUserURLFilter(prefs,
                                std::move(delegate),
                                std::move(checker_client)) {}
  MOCK_METHOD(bool,
              RunAsyncChecker,
              (const GURL& url, ResultCallback callback));
};

class ClassifyUrlNavigationThrottleTest
    : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    EnableParentalControls(*profile()->GetPrefs());
  }

  TestingProfile::TestingFactories GetTestingFactories() const override {
    return {TestingProfile::TestingFactory{
        SupervisedUserServiceFactory::GetInstance(),
        base::BindRepeating(
            &supervised_user_test_util::BuildSupervisedUserService<
                MockSupervisedUserURLFilter>)}};
  }

  std::unique_ptr<content::MockNavigationThrottleRegistry>
  CreateNavigationThrottle(const std::vector<GURL> redirects) {
    CHECK_GT(redirects.size(), 0U) << "At least one url is required";

    redirects_ = redirects;
    current_url_it_ = redirects_.begin();

    navigation_handle_ =
        std::make_unique<::testing::NiceMock<content::MockNavigationHandle>>(
            *current_url_it_, main_rfh());

    // Note: this creates the throttle regardless the supervision status of the
    // user.
    auto registry = std::make_unique<content::MockNavigationThrottleRegistry>(
        navigation_handle_.get(),
        content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
    ClassifyUrlNavigationThrottle::MaybeCreateAndAdd(*registry.get());

    if (!registry->throttles().empty()) {
      // Add mock handlers for resume & cancel deferred.
      registry->throttles().back()->set_resume_callback_for_testing(
          base::BindLambdaForTesting([&]() { resume_called_ = true; }));
    }
    return registry;
  }

  std::unique_ptr<content::MockNavigationThrottleRegistry>
  CreateNavigationThrottle(const GURL& url) {
    return CreateNavigationThrottle(std::vector<GURL>({url}));
  }

  // Advances the pointer of the current url internally and synchronizes the
  // navigation_handle_ accordingly: updating both the url and the redirect
  // chain that led to it.
  void AdvanceRedirect() {
    current_url_it_++;

    // CHECK_NE doesn't support std::vector::iterator comparison.
    CHECK_NE(redirects_.end() - current_url_it_, 0)
        << "Can't advance past last redirect";

    std::vector<GURL> redirect_chain;
    for (auto it = redirects_.begin(); it != current_url_it_; ++it) {
      redirect_chain.push_back(*it);
    }

    navigation_handle_->set_url(*current_url_it_);
    navigation_handle_->set_redirect_chain(redirect_chain);
  }

  MockSupervisedUserURLFilter* GetSupervisedUserURLFilter() {
    // Cast is safe, see this::GetTestingFactories() to see how the object was
    // created.
    return static_cast<MockSupervisedUserURLFilter*>(
        SupervisedUserServiceFactory::GetForProfile(profile())->GetURLFilter());
  }

  base::HistogramTester* histogram_tester() { return &histogram_tester_; }
  bool resume_called() const { return resume_called_; }

 private:
  std::unique_ptr<content::MockNavigationHandle> navigation_handle_;
  base::HistogramTester histogram_tester_;
  bool resume_called_ = false;

  std::vector<GURL> redirects_;
  std::vector<GURL>::iterator current_url_it_;
};

// This test is used to test the behavior of the throttle when the user is not
// supervised - all navigations are allowed, but no metrics recorded.
class ClassifyUrlNavigationThrottleUnsupervisedUserTest
    : public ClassifyUrlNavigationThrottleTest {
 protected:
  void SetUp() override { ChromeRenderViewHostTestHarness::SetUp(); }
};

TEST_F(ClassifyUrlNavigationThrottleUnsupervisedUserTest,
       WillNotRegisterThrottle) {
  EXPECT_TRUE(CreateNavigationThrottle(GURL(kExampleURL))->throttles().empty());
}

TEST_F(ClassifyUrlNavigationThrottleTest, AllowedUrlsRecordedInAllowBucket) {
  GURL allowed_url(kExampleURL);
  supervised_user_test_util::SetManualFilterForHost(
      profile(), allowed_url.host(), /*allowlist=*/true);

  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(allowed_url);
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillProcessResponse());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 1);
  histogram_tester()->ExpectTotalCount(
      kClassifiedEarlierThanContentResponseHistogramName,
      /*expected_count(grew by)*/ 1);

  // This throttle continued on request, and proceeded on response.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 1},
                        {ClassifyUrlThrottleStatus::kProceed, 1}});
}

TEST_F(ClassifyUrlNavigationThrottleTest,
       BlocklistedUrlsRecordedInBlockManualBucket) {
  GURL blocked_url(kExampleURL);
  supervised_user_test_util::SetManualFilterForHost(
      profile(), blocked_url.host(), /*allowlist=*/false);
  ASSERT_TRUE(GetSupervisedUserURLFilter()
                  ->GetFilteringBehavior(blocked_url)
                  .IsBlocked());

  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(blocked_url);
  ASSERT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillStartRequest());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kBlockManual, 1);

  // Since this is not a success path, no latency metric is recorded.
  ExpectNoLatencyRecorded(histogram_tester());
  // This throttle immediately deferred and presented an interstitial.
  ExpectThrottleStatus(
      histogram_tester(),
      {{ClassifyUrlThrottleStatus::kDeferAndScheduleInterstitial, 1}});
}

TEST_F(ClassifyUrlNavigationThrottleTest,
       AllSitesBlockedRecordedInBlockNotInAllowlistBucket) {
  supervised_user_test_util::SetWebFilterType(profile(),
                                              WebFilterType::kCertainSites);

  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GURL(kExampleURL));
  ASSERT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillStartRequest());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kBlockNotInAllowlist, 1);

  // Since this is not a success path, no latency metric is recorded.
  ExpectNoLatencyRecorded(histogram_tester());
  // This throttle immediately deferred and presented an interstitial.
  ExpectThrottleStatus(
      histogram_tester(),
      {{ClassifyUrlThrottleStatus::kDeferAndScheduleInterstitial, 1}});
  // As a result, the navigation is not resumed
  EXPECT_FALSE(resume_called());
}

enum class SupervisionMode {
  kSupervisedByFamilyLink,
  kLocalSupervision,
};

struct AsyncCheckerTestCase {
  std::string name;
  SupervisionMode mode;
};

class ClassifyUrlNavigationThrottleAsyncCheckerTest
    : public ClassifyUrlNavigationThrottleTest,
      public ::testing::WithParamInterface<AsyncCheckerTestCase> {
 protected:
  void SetUp() override {
    // Consciously bypasses direct superclass SetUp to avoid enabling parental
    // controls.
    ChromeRenderViewHostTestHarness::SetUp();
    if (GetParam().mode == SupervisionMode::kSupervisedByFamilyLink) {
      EnableParentalControls(*profile()->GetPrefs());
    } else {
      EnableBrowserContentFilters(*profile()->GetPrefs());
    }
  }
};

TEST_P(ClassifyUrlNavigationThrottleAsyncCheckerTest,
       BlockedMatureSitesRecordedInBlockSafeSitesBucket) {
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault([](const GURL& url,
                        MockSupervisedUserURLFilter::ResultCallback callback) {
        std::move(callback).Run({url, FilteringBehavior::kBlock,
                                 FilteringBehaviorReason::ASYNC_CHECKER});
        return true;
      });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(GURL(kExampleURL), testing::_))
      .Times(1);
  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GURL(kExampleURL));
  ASSERT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillStartRequest());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kBlockSafeSites, 1);

  // Since this is not a success path, no latency metric is recorded.
  ExpectNoLatencyRecorded(histogram_tester());
  // This throttle immediately deferred and presented an interstitial.
  ExpectThrottleStatus(
      histogram_tester(),
      {{ClassifyUrlThrottleStatus::kDeferAndScheduleInterstitial, 1}});
  // As a result, the navigation is not resumed
  EXPECT_FALSE(resume_called());
}

TEST_P(ClassifyUrlNavigationThrottleAsyncCheckerTest,
       ClassificationIsFasterThanHttp) {
  MockSupervisedUserURLFilter::ResultCallback check;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&check](const GURL& url,
                   MockSupervisedUserURLFilter::ResultCallback callback) {
            check = std::move(callback);
            return false;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(GURL(kExampleURL), testing::_))
      .Times(1);

  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GURL(kExampleURL));
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());

  // Check is not completed yet
  EXPECT_TRUE(check);
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 0);

  // Before the throttle will be notified that the content is ready, complete
  // the check
  std::move(check).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                        FilteringBehaviorReason::ASYNC_CHECKER});

  // Throttle is not blocked
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillProcessResponse());

  // As a result, the navigation hadn't had to be resumed
  EXPECT_FALSE(resume_called());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 1);
  // Since the throttle had to wait for checks to complete, it recorded a
  // corresponding metric.
  histogram_tester()->ExpectTotalCount(
      kClassifiedEarlierThanContentResponseHistogramName,
      /*expected_count=*/1);

  // This throttle continued on request, and proceeded on response because the
  // result was already there.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 1},
                        {ClassifyUrlThrottleStatus::kProceed, 1}});
}

TEST_P(ClassifyUrlNavigationThrottleAsyncCheckerTest,
       ClassificationIsSlowerThanHttp) {
  MockSupervisedUserURLFilter::ResultCallback check;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&check](const GURL& url,
                   MockSupervisedUserURLFilter::ResultCallback callback) {
            check = std::move(callback);
            return false;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(GURL(kExampleURL), testing::_))
      .Times(1);

  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GURL(kExampleURL));

  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());

  // At this point, check was not completed.
  EXPECT_TRUE(check);
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 0);

  // But will block at process response because the check is still
  // pending and no filtering was completed.
  EXPECT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillProcessResponse());

  // Now complete the outstanding check
  std::move(check).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                        FilteringBehaviorReason::ASYNC_CHECKER});

  // As a result, the navigation is resumed (and three checks registered)
  EXPECT_TRUE(resume_called());
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 1);
  // Since the throttle had to wait for checks to complete, it recorded a
  // corresponding metric.
  histogram_tester()->ExpectTotalCount(
      kClassifiedLaterThanContentResponseHistogramName,
      /*expected_count=*/1);

  // This throttle continued on request, and deferred on response because the
  // result wasn't there. Then it resumed.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 1},
                        {ClassifyUrlThrottleStatus::kDefer, 1},
                        {ClassifyUrlThrottleStatus::kResume, 1}});
}

// Checks a scenario where the classification responses arrive in reverse order:
// Last check is completed first but is blocking, and first check is completed
// after it and is not blocking. Both checks complete after the response was
// ready for processing.
TEST_P(ClassifyUrlNavigationThrottleAsyncCheckerTest,
       ReverseOrderOfResponsesAfterContentIsReady) {
  std::vector<MockSupervisedUserURLFilter::ResultCallback> checks;
  // Check for the first url that will complete last.
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&checks](const GURL& url,
                    MockSupervisedUserURLFilter::ResultCallback callback) {
            checks.push_back(std::move(callback));
            return false;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(testing::_, testing::_))
      .Times(2);

  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle({GURL(kExampleURL), GURL(kExample1URL)});

  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());
  // As expected, the process navigation is deferred.
  EXPECT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillProcessResponse());

  // Resolve pending checks in reverse order, so that block for 2nd request
  // comes first.
  std::move(checks[1]).Run({GURL(kExample1URL), FilteringBehavior::kBlock,
                            FilteringBehaviorReason::ASYNC_CHECKER});
  std::move(checks[0]).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                            FilteringBehaviorReason::ASYNC_CHECKER});

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 1);
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kBlockSafeSites, 1);

  // Since this is not a success path, no latency metric is recorded.
  ExpectNoLatencyRecorded(histogram_tester());
  // This throttle continued on request and redirect, and deferred on response
  // because the result wasn't there. It never recovered from defer state
  // (interstitial was presented).
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 2},
                        {ClassifyUrlThrottleStatus::kDefer, 1}});
  EXPECT_FALSE(resume_called());
}

const AsyncCheckerTestCase kAsyncCheckerTestCases[] = {
    {.name = "SupervisedByFamilyLink",
     .mode = SupervisionMode::kSupervisedByFamilyLink}
#if BUILDFLAG(IS_ANDROID)
    ,
    {.name = "LocalSupervision", .mode = SupervisionMode::kLocalSupervision}
#endif  // BUILDFLAG(IS_ANDROID)
};

INSTANTIATE_TEST_SUITE_P(
    ,
    ClassifyUrlNavigationThrottleAsyncCheckerTest,
    testing::ValuesIn(kAsyncCheckerTestCases),
    [](const testing::TestParamInfo<AsyncCheckerTestCase>& info) {
      return info.param.name;
    });

struct TestCase {
  std::string name;
  std::vector<std::string> redirect_chain;
};

class ClassifyUrlNavigationThrottleParallelizationTest
    : public ClassifyUrlNavigationThrottleTest,
      public testing::WithParamInterface<TestCase> {
 protected:
  static const std::vector<GURL> GetRedirectChain() {
    CHECK_EQ(GetParam().redirect_chain.size(), 3U)
        << "Tests assume one request and two redirects";
    std::vector<GURL> urls;
    for (const auto& redirect : GetParam().redirect_chain) {
      urls.push_back(GURL(redirect));
    }
    return urls;
  }
};

TEST_P(ClassifyUrlNavigationThrottleParallelizationTest,
       ClassificationIsFasterThanHttp) {
  std::vector<MockSupervisedUserURLFilter::ResultCallback> checks;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&checks](const GURL& url,
                    MockSupervisedUserURLFilter::ResultCallback callback) {
            checks.push_back(std::move(callback));
            // Asynchronous behavior all the time.
            return false;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(testing::_, testing::_))
      .Times(3);

  // This navigation is a 3-piece redirect chain on the same URL:
  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GetRedirectChain());

  // It will allow request and two redirects to pass...
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());

  // No checks are completed yet
  EXPECT_THAT(checks, testing::SizeIs(3));
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 0);

  // Before the throttle will be notified that the content is ready, complete
  // all checks
  for (auto& check : checks) {
    std::move(check).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                          FilteringBehaviorReason::ASYNC_CHECKER});
  }

  // Throttle is not blocked
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillProcessResponse());

  // As a result, the navigation hadn't had to be resumed
  EXPECT_FALSE(resume_called());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 3);
  // Since the throttle had to wait for checks to complete, it recorded a
  // corresponding metric.
  histogram_tester()->ExpectTotalCount(
      kClassifiedEarlierThanContentResponseHistogramName,
      /*expected_count=*/1);

  // This throttle continued on request and redirects and proceeded because
  // verdict was ready.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 3},
                        {ClassifyUrlThrottleStatus::kProceed, 1}});
}

TEST_P(ClassifyUrlNavigationThrottleParallelizationTest,
       OutOfOrderClassification) {
  std::vector<MockSupervisedUserURLFilter::ResultCallback> checks;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&checks](const GURL& url,
                    MockSupervisedUserURLFilter::ResultCallback callback) {
            checks.push_back(std::move(callback));
            // Asynchronous behavior all the time.
            return false;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(testing::_, testing::_))
      .Times(3);

  // This navigation is a 3-piece redirect chain on the same URL:
  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GetRedirectChain());

  // It will allow request and two redirects to pass...
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());

  // No checks are completed yet
  EXPECT_THAT(checks, testing::SizeIs(3));
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 0);

  // Before the throttle will be notified that the content is ready, complete
  // all checks but from the back.
  for (auto it = checks.rbegin(); it != checks.rend(); ++it) {
    std::move(*it).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                        FilteringBehaviorReason::ASYNC_CHECKER});
    // Classification still not complete.
    histogram_tester()->ExpectTotalCount(
        kClassifiedEarlierThanContentResponseHistogramName,
        /*expected_count=*/0);
  }

  // Throttle is not blocked
  EXPECT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillProcessResponse());

  // As a result, the navigation hadn't had to be resumed
  EXPECT_FALSE(resume_called());

  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 3);
  // Since the throttle had to wait for checks to complete, it recorded a
  // corresponding metric.
  histogram_tester()->ExpectTotalCount(
      kClassifiedEarlierThanContentResponseHistogramName,
      /*expected_count=*/1);

  // This throttle continued on request and redirects and then proceeded because
  // verdict was ready.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 3},
                        {ClassifyUrlThrottleStatus::kProceed, 1}});
}

TEST_P(ClassifyUrlNavigationThrottleParallelizationTest,
       ClassificationIsSlowerThanHttp) {
  std::vector<MockSupervisedUserURLFilter::ResultCallback> checks;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&checks](const GURL& url,
                    MockSupervisedUserURLFilter::ResultCallback callback) {
            checks.push_back(std::move(callback));
            // Asynchronous behavior all the time.
            return false;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(testing::_, testing::_))
      .Times(3);

  // This navigation is a 3-piece redirect chain on the same URL:
  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GetRedirectChain());

  // It will allow request and two redirects to pass...
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());

  // At this point, no check was completed.
  EXPECT_THAT(checks, testing::SizeIs(3));
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 0);

  // Complete two last checks
  std::move(checks[1]).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                            FilteringBehaviorReason::ASYNC_CHECKER});
  std::move(checks[2]).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                            FilteringBehaviorReason::ASYNC_CHECKER});

  // Now two out of three checks are complete
  EXPECT_THAT(checks, testing::SizeIs(3));
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 2);

  // But will block at process response because one check is still
  // pending and no filtering was completed.
  EXPECT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillProcessResponse());

  // Now complete the outstanding check
  std::move(checks[0]).Run({GURL(kExampleURL), FilteringBehavior::kAllow,
                            FilteringBehaviorReason::ASYNC_CHECKER});

  // As a result, the navigation is resumed (and three checks registered)
  EXPECT_TRUE(resume_called());
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 3);
  // Since the throttle had to wait for checks to complete, it recorded a
  // corresponding metric.
  histogram_tester()->ExpectTotalCount(
      kClassifiedLaterThanContentResponseHistogramName,
      /*expected_count=*/1);

  // This throttle continued on request and redirects and then deferred because
  // one check was outstanding. After it was completed, the throttle resumed.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 3},
                        {ClassifyUrlThrottleStatus::kDefer, 1},
                        {ClassifyUrlThrottleStatus::kResume, 1}});
}

TEST_P(ClassifyUrlNavigationThrottleParallelizationTest,
       ShortCircuitsSynchronousBlock) {
  bool first_check = false;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault(
          [&first_check](const GURL& url,
                         MockSupervisedUserURLFilter::ResultCallback callback) {
            if (!first_check) {
              std::move(callback).Run({url, FilteringBehavior::kAllow,
                                       FilteringBehaviorReason::ASYNC_CHECKER});
              first_check = true;
              return true;
            }

            // Subsequent checks are synchronous blocks.
            std::move(callback).Run({url, FilteringBehavior::kBlock,
                                     FilteringBehaviorReason::ASYNC_CHECKER});
            return true;
          });
  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(testing::_, testing::_))
      .Times(2);

  // This navigation is a 3-piece redirect chain on the same URL:
  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GetRedirectChain());

  // It will DEFER at 2nd request (1st redirect).
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  AdvanceRedirect();
  ASSERT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillRedirectRequest());

  // And one completed block from safe-sites (async checker)
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kBlockSafeSites, 1);

  // As a result, the navigation is not resumed
  EXPECT_FALSE(resume_called());
  // Since this is not a success path, no latency metric is recorded.
  ExpectNoLatencyRecorded(histogram_tester());
  // This throttle continued on first request deferred on second one.
  ExpectThrottleStatus(
      histogram_tester(),
      {{ClassifyUrlThrottleStatus::kContinue, 1},
       {ClassifyUrlThrottleStatus::kDeferAndScheduleInterstitial, 1}});
}

TEST_P(ClassifyUrlNavigationThrottleParallelizationTest,
       HandlesLateAsynchronousBlock) {
  std::vector<MockSupervisedUserURLFilter::ResultCallback> checks;
  bool first_check_completed = false;
  ON_CALL(*GetSupervisedUserURLFilter(),
          RunAsyncChecker(testing::_, testing::_))
      .WillByDefault([&checks, &first_check_completed](
                         const GURL& url,
                         MockSupervisedUserURLFilter::ResultCallback callback) {
        // First check is synchronous allow
        if (!first_check_completed) {
          first_check_completed = true;
          std::move(callback).Run({url, FilteringBehavior::kAllow,
                                   FilteringBehaviorReason::ASYNC_CHECKER});
          return true;
        }
        // Subsequent checks are asynchronous
        checks.push_back(std::move(callback));
        return false;
      });

  EXPECT_CALL(*GetSupervisedUserURLFilter(),
              RunAsyncChecker(testing::_, testing::_))
      .Times(3);

  // This navigation is a 3-piece redirect chain on the same URL:
  std::unique_ptr<content::MockNavigationThrottleRegistry> registry =
      CreateNavigationThrottle(GetRedirectChain());

  // It proceed all three request/redirects.
  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillStartRequest());
  AdvanceRedirect();

  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());
  AdvanceRedirect();

  ASSERT_EQ(content::NavigationThrottle::PROCEED,
            registry->throttles().back()->WillRedirectRequest());

  // There will be two pending checks (first was synchronous)
  EXPECT_THAT(checks, testing::SizeIs(2));
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kAllow, 1);

  // Http server completes first
  EXPECT_EQ(content::NavigationThrottle::DEFER,
            registry->throttles().back()->WillProcessResponse());

  // Complete first pending check
  std::move(checks.front())
      .Run({GURL(kExampleURL), FilteringBehavior::kBlock,
            FilteringBehaviorReason::ASYNC_CHECKER});

  // Now two out of three checks are complete
  EXPECT_THAT(checks, testing::SizeIs(2));
  histogram_tester()->ExpectBucketCount(
      kSupervisedUserTopLevelURLFilteringResultHistogramName,
      SupervisedUserFilterTopLevelResult::kBlockSafeSites, 1);

  // As a result, the navigation is not resumed
  EXPECT_FALSE(resume_called());
  // Since this is not a success path, no latency metric is recorded.
  ExpectNoLatencyRecorded(histogram_tester());
  // This throttle continued on request and redirects and deferred waiting for
  // last classification.
  ExpectThrottleStatus(histogram_tester(),
                       {{ClassifyUrlThrottleStatus::kContinue, 3},
                        {ClassifyUrlThrottleStatus::kDefer, 1}});
}

const TestCase kTestCases[] = {
    {.name = "TwoRedirects",
     .redirect_chain = {kExampleURL, kExample1URL, kExample2URL}},
    {.name = "TwoIdenticalRedirects",
     .redirect_chain = {kExampleURL, kExampleURL, kExampleURL}}};

INSTANTIATE_TEST_SUITE_P(,
                         ClassifyUrlNavigationThrottleParallelizationTest,
                         testing::ValuesIn(kTestCases),
                         [](const testing::TestParamInfo<TestCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace supervised_user
