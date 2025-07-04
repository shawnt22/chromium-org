// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/keep_alive_url_loader_service.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "components/attribution_reporting/constants.h"
#include "content/browser/attribution_reporting/attribution_data_host_manager_impl.h"
#include "content/browser/attribution_reporting/test/mock_attribution_data_host_manager.h"
#include "content/browser/attribution_reporting/test/mock_attribution_manager.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/test/test_utils.h"
#include "content/test/navigation_simulator_impl.h"
#include "content/test/test_render_view_host.h"
#include "content/test/test_web_contents.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/functions.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.h"
#include "services/network/public/cpp/parsed_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/attribution.mojom.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/ip_address_space.mojom.h"
#include "services/network/public/mojom/referrer_policy.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "third_party/blink/public/mojom/loader/fetch_later.mojom.h"
#include "url/gurl.h"

namespace content {
namespace {

using attribution_reporting::kAttributionReportingRegisterSourceHeader;
using attribution_reporting::kAttributionReportingRegisterTriggerHeader;

using testing::_;
using testing::Eq;
using testing::IsEmpty;
using testing::SizeIs;
using testing::WithArg;

constexpr char kTestRequestUrl[] = "https://example.test";
constexpr char kTestResponseHeaderName[] = "My-Test-Header";
constexpr char kTestResponseHeaderValue[] = "my-test-value";
constexpr char kTestRedirectRequestUrl[] = "https://redirect.test";
constexpr char kTestUnSafeRedirectRequestUrl[] = "about:blank";
constexpr char kTestViolatingCSPRedirectRequestUrl[] =
    "https://violate-csp.test";

// Mock a receiver URLLoaderClient that may exist in renderer.
class MockReceiverURLLoaderClient : public network::mojom::URLLoaderClient {
 public:
  MockReceiverURLLoaderClient() = default;
  MockReceiverURLLoaderClient(const MockReceiverURLLoaderClient&) = delete;
  MockReceiverURLLoaderClient& operator=(const MockReceiverURLLoaderClient&) =
      delete;
  ~MockReceiverURLLoaderClient() override {
    if (receiver_.is_bound()) {
      // Flush the pipe to make sure there aren't any lingering events.
      receiver_.FlushForTesting();
    }
  }

  mojo::PendingRemote<network::mojom::URLLoaderClient>
  BindNewPipeAndPassRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  // Note that this also unbinds the receiver.
  void ResetReceiver() { receiver_.reset(); }

  // `network::mojom::URLLoaderClient` overrides:
  MOCK_METHOD1(OnReceiveEarlyHints, void(network::mojom::EarlyHintsPtr));
  MOCK_METHOD3(OnReceiveResponse,
               void(network::mojom::URLResponseHeadPtr,
                    mojo::ScopedDataPipeConsumerHandle,
                    std::optional<mojo_base::BigBuffer>));
  MOCK_METHOD2(OnReceiveRedirect,
               void(const net::RedirectInfo&,
                    network::mojom::URLResponseHeadPtr));
  MOCK_METHOD3(OnUploadProgress,
               void(int64_t, int64_t, base::OnceCallback<void()>));
  MOCK_METHOD1(OnTransferSizeUpdated, void(int32_t));
  MOCK_METHOD1(OnComplete, void(const network::URLLoaderCompletionStatus&));

 private:
  mojo::Receiver<network::mojom::URLLoaderClient> receiver_{this};
};

// Fakes a URLLoaderFactory that may exist in renderer, which only delegates to
// `remote_url_loader_factory`.
class FakeRemoteURLLoaderFactory {
 public:
  static constexpr int kRequestId = 1;

  FakeRemoteURLLoaderFactory() = default;
  FakeRemoteURLLoaderFactory(const FakeRemoteURLLoaderFactory&) = delete;
  FakeRemoteURLLoaderFactory& operator=(const FakeRemoteURLLoaderFactory&) =
      delete;
  ~FakeRemoteURLLoaderFactory() = default;

  mojo::PendingReceiver<network::mojom::URLLoaderFactory>
  BindNewPipeAndPassReceiver() {
    return remote_url_loader_factory.BindNewPipeAndPassReceiver();
  }

  // Binds `remote_url_loader` to a new URLLoader.
  void CreateLoaderAndStart(
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      bool expect_success = true) {
    remote_url_loader_factory->CreateLoaderAndStart(
        remote_url_loader.BindNewPipeAndPassReceiver(),
        /*request_id=*/kRequestId, /*options=*/0, request, std::move(client),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
    remote_url_loader_factory.FlushForTesting();
    ASSERT_EQ(remote_url_loader.is_connected(), expect_success);
  }

  bool is_remote_url_loader_connected() {
    return remote_url_loader.is_connected();
  }
  void reset_remote_url_loader() { remote_url_loader.reset(); }

 private:
  mojo::Remote<network::mojom::URLLoaderFactory> remote_url_loader_factory;
  mojo::Remote<network::mojom::URLLoader> remote_url_loader;
};

// Fakes a FetchLaterLoaderFactory that may exist in renderer, which only
// delegates to `remote_fetch_later_loader_factory`.
class FakeRemoteFetchLaterLoaderFactory {
 public:
  FakeRemoteFetchLaterLoaderFactory() = default;
  FakeRemoteFetchLaterLoaderFactory(const FakeRemoteFetchLaterLoaderFactory&) =
      delete;
  FakeRemoteFetchLaterLoaderFactory& operator=(
      const FakeRemoteFetchLaterLoaderFactory&) = delete;
  ~FakeRemoteFetchLaterLoaderFactory() = default;

  mojo::PendingAssociatedReceiver<blink::mojom::FetchLaterLoaderFactory>
  BindNewEndpointAndPassDedicatedReceiver() {
    return remote_fetch_later_loader_factory_
        .BindNewEndpointAndPassDedicatedReceiver();
  }

  // Binds `remote_fetch_later_loader_` to a new URLLoader.
  void CreateLoader(const network::ResourceRequest& request,
                    bool expect_success = true) {
    remote_fetch_later_loader_factory_->CreateLoader(
        remote_fetch_later_loader_.BindNewEndpointAndPassReceiver(),
        /*request_id=*/1, /*options=*/0, request,
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
    remote_fetch_later_loader_factory_.FlushForTesting();
    ASSERT_EQ(remote_fetch_later_loader_.is_connected(), expect_success);
  }

  bool is_remote_fetch_later_loader_connected() {
    return remote_fetch_later_loader_.is_connected();
  }
  void reset_remote_fetch_later_loader() { remote_fetch_later_loader_.reset(); }

 private:
  mojo::AssociatedRemote<blink::mojom::FetchLaterLoaderFactory>
      remote_fetch_later_loader_factory_;
  mojo::AssociatedRemote<blink::mojom::FetchLaterLoader>
      remote_fetch_later_loader_;
};

class ConfigurableURLLoaderThrottle final : public blink::URLLoaderThrottle {
 public:
  explicit ConfigurableURLLoaderThrottle(bool deferring = false,
                                         bool canceling_before_start = false,
                                         bool canceling_before_redirect = false)
      : deferring_(deferring),
        canceling_before_start_(canceling_before_start),
        canceling_before_redirect_(canceling_before_redirect) {}

  ~ConfigurableURLLoaderThrottle() override = default;
  // Not copyable.
  ConfigurableURLLoaderThrottle(const ConfigurableURLLoaderThrottle&) = delete;
  ConfigurableURLLoaderThrottle& operator=(
      const ConfigurableURLLoaderThrottle&) = delete;

  // blink::URLLoaderThrottle overrides:
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override {
    will_start_request_called_ = true;
    *defer = deferring_;
    if (canceling_before_start_) {
      delegate()->CancelWithError(net::ERR_ABORTED);
    }
  }
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& /* response_head */,
      bool* defer,
      std::vector<std::string>* /* to_be_removed_headers */,
      net::HttpRequestHeaders* /* modified_headers */,
      net::HttpRequestHeaders* /* modified_cors_exempt_headers */) override {
    will_redirect_request_called_ = true;
    *defer = deferring_;
    if (canceling_before_redirect_) {
      delegate()->CancelWithError(net::ERR_ABORTED);
    }
  }
  void WillProcessResponse(const GURL& response_url_,
                           network::mojom::URLResponseHead* response_head,
                           bool* defer) override {
    will_process_response_called_ = true;
    *defer = deferring_;
  }

  bool will_start_request_called() const { return will_start_request_called_; }
  bool will_redirect_request_called() const {
    return will_redirect_request_called_;
  }
  bool will_process_response_called() const {
    return will_process_response_called_;
  }

  Delegate* delegate() { return delegate_; }

 private:
  bool will_start_request_called_ = false;
  bool will_redirect_request_called_ = false;
  bool will_process_response_called_ = false;

  const bool deferring_;
  const bool canceling_before_start_;
  const bool canceling_before_redirect_;
};

// Returns true if `arg` has a header of the given `name` and `value`.
// `arg` is an `network::mojom::URLResponseHeadPtr`.
MATCHER_P2(ResponseHasHeader,
           name,
           value,
           base::StringPrintf("Response has %sheader[%s=%s]",
                              negation ? "no " : "",
                              name,
                              value)) {
  return arg->headers->HasHeaderValue(name, value);
}

network::ResourceRequest CreateFetchLaterResourceRequest(const GURL& url) {
  network::ResourceRequest request;
  request.url = url;
  request.keepalive = true;
  request.is_fetch_later_api = true;
  request.resource_type = static_cast<int>(blink::mojom::ResourceType::kXhr);
  return request;
}

network::ResourceRequest CreateResourceRequest(
    const GURL& url,
    bool keepalive = true,
    bool is_trusted = false,
    std::optional<network::mojom::RedirectMode> redirect_mode = std::nullopt) {
  network::ResourceRequest request;
  request.url = url;
  request.keepalive = keepalive;
  request.resource_type = static_cast<int>(blink::mojom::ResourceType::kXhr);
  if (is_trusted) {
    request.trusted_params = network::ResourceRequest::TrustedParams();
  }
  if (redirect_mode) {
    request.redirect_mode = *redirect_mode;
  }
  return request;
}

network::mojom::URLResponseHeadPtr CreateResponseHead(
    const std::vector<std::pair<std::string, std::string>>& extra_headers =
        {}) {
  auto response = network::mojom::URLResponseHead::New();
  net::HttpResponseHeaders::Builder builder({1, 1}, "200 OK");
  for (const auto& [name, value] : extra_headers) {
    builder.AddHeader(name, value);
  }
  response->headers = builder.Build();
  return response;
}

net::RedirectInfo CreateRedirectInfo(const GURL& new_url) {
  net::RedirectInfo redirect_info;
  redirect_info.new_method = "GET";
  redirect_info.new_url = new_url;
  redirect_info.status_code = 301;
  return redirect_info;
}

network::mojom::EarlyHintsPtr CreateEarlyHints(
    const GURL& url,
    const std::vector<std::pair<std::string, std::string>>& extra_headers =
        {}) {
  auto response_headers =
      base::MakeRefCounted<net::HttpResponseHeaders>("HTTP/1.1 200 OK\n");
  for (const auto& header : extra_headers) {
    response_headers->SetHeader(header.first, header.second);
  }
  return network::mojom::EarlyHints::New(
      network::PopulateParsedHeaders(response_headers.get(), url),
      network::mojom::ReferrerPolicy::kDefault,
      network::mojom::IPAddressSpace::kPublic);
}

}  // namespace

class KeepAliveURLLoaderServiceTestBase : public RenderViewHostTestHarness {
 public:
  KeepAliveURLLoaderServiceTestBase()
      : RenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

 protected:
  void SetUp() override {
    network_url_loader_factory_ =
        std::make_unique<network::TestURLLoaderFactory>(
            /*observe_loader_requests=*/true);
    // Intercepts Mojo bad-message error.
    mojo::SetDefaultProcessErrorHandler(
        base::BindLambdaForTesting([&](const std::string& error) {
          ASSERT_FALSE(mojo_bad_message_.has_value());
          mojo_bad_message_ = error;
        }));
    RenderViewHostTestHarness::SetUp();

    test_web_contents()->NavigateAndCommit(GURL("https://example.com"));

    pending_navigation_ = NavigationSimulator::CreateBrowserInitiated(
        GURL("https://example.com"), web_contents());
    pending_navigation_->ReadyToCommit();

    AddConnectSrcCSPToRFH(kTestRedirectRequestUrl);
  }

  void TearDown() override {
    pending_navigation_.reset();
    network_url_loader_factory_ = nullptr;
    loader_service_ = nullptr;
    mojo::SetDefaultProcessErrorHandler(base::NullCallback());
    mojo_bad_message_ = std::nullopt;
    RenderViewHostTestHarness::TearDown();
  }

  void ExpectMojoBadMessage(const std::string& message) {
    EXPECT_EQ(mojo_bad_message_, message);
  }

  NavigationHandle* GetNavigationHandle() {
    return pending_navigation_->GetNavigationHandle();
  }

  // Asks KeepAliveURLLoaderService to bind a KeepAliveURLLoaderFactory to the
  // given `remote_url_loader_factory`.
  // More than one factory can be bound to the same service.
  void BindKeepAliveURLLoaderFactory(
      FakeRemoteURLLoaderFactory& remote_url_loader_factory) {
    mojo::Remote<network::mojom::URLLoaderFactory> factory;
    network_url_loader_factory().Clone(factory.BindNewPipeAndPassReceiver());
    auto pending_factory =
        std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
            factory.Unbind());

    // Remote: `remote_url_loader_factory`
    // Receiver: Held in `loader_service_`.
    auto context = loader_service().BindFactory(
        remote_url_loader_factory.BindNewPipeAndPassReceiver(),
        network::SharedURLLoaderFactory::Create(std::move(pending_factory)),
        static_cast<RenderFrameHostImpl*>(main_rfh())
            ->policy_container_host()
            ->Clone());
    context->OnDidCommitNavigation(GetNavigationHandle());
  }

  network::TestURLLoaderFactory::PendingRequest* GetLastPendingRequest() {
    return &network_url_loader_factory_->pending_requests()->back();
  }

  const std::vector<network::TestURLLoaderFactory::PendingRequest>&
  GetPendingRequests() const {
    return *network_url_loader_factory_->pending_requests();
  }

  void AddConnectSrcCSPToRFH(const std::string& allowed_url) {
    static_cast<RenderFrameHostImpl*>(main_rfh())
        ->policy_container_host()
        ->AddContentSecurityPolicies(network::ParseContentSecurityPolicies(
            "connect-src " + allowed_url,
            network::mojom::ContentSecurityPolicyType::kEnforce,
            network::mojom::ContentSecurityPolicySource::kMeta,
            GURL(kTestRequestUrl)));
  }

  network::TestURLLoaderFactory& network_url_loader_factory() {
    return *network_url_loader_factory_;
  }
  KeepAliveURLLoaderService& loader_service() {
    if (!loader_service_) {
      loader_service_ = std::make_unique<KeepAliveURLLoaderService>(
          main_rfh()->GetBrowserContext());
    }
    return *loader_service_;
  }
  base::test::ScopedFeatureList& feature_list() { return scoped_feature_list_; }

  TestWebContents* test_web_contents() {
    return static_cast<TestWebContents*>(web_contents());
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  // Intercepts network facotry requests instead of using production factory.
  std::unique_ptr<network::TestURLLoaderFactory> network_url_loader_factory_ =
      nullptr;
  // The test target.
  std::unique_ptr<KeepAliveURLLoaderService> loader_service_ = nullptr;
  std::optional<std::string> mojo_bad_message_;
  std::unique_ptr<NavigationSimulator> pending_navigation_;
};

class KeepAliveURLLoaderServiceTest : public KeepAliveURLLoaderServiceTestBase {
 protected:
  void SetUp() override {
    feature_list().InitWithFeatures(
        {blink::features::kKeepAliveInBrowserMigration,
         blink::features::kAttributionReportingInBrowserMigration},
        {});
    KeepAliveURLLoaderServiceTestBase::SetUp();
  }
};

TEST_F(KeepAliveURLLoaderServiceTest,
       LoadKeepAliveRequestWithInvalidFeatureAndTerminate) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads a keepalive request with invalid feature config:
  base::test::ScopedFeatureList overwritten_feature_list;
  overwritten_feature_list.InitAndDisableFeature(
      blink::features::kKeepAliveInBrowserMigration);
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote(),
      /*expect_success=*/false);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(renderer_loader_factory.is_remote_url_loader_connected());
  ExpectMojoBadMessage(
      "Unexpected call to "
      "KeepAliveURLLoaderFactories::CreateLoaderAndStart()");
}

TEST_F(KeepAliveURLLoaderServiceTest, LoadFetchLaterRequestAndTerminate) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads FetchLater request (which is also keepalive request), but is not
  // allowed with URLLoaderFactory.
  renderer_loader_factory.CreateLoaderAndStart(
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote(),
      /*expect_success=*/false);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(renderer_loader_factory.is_remote_url_loader_connected());
  ExpectMojoBadMessage(
      "Unexpected `resource_request.is_fetch_later_api` in "
      "KeepAliveURLLoaderFactories::CreateLoaderAndStart(): must not be set");
}

TEST_F(KeepAliveURLLoaderServiceTest, LoadNonKeepaliveRequestAndTerminate) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads non-keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl), /*keepalive=*/false),
      renderer_loader_client.BindNewPipeAndPassRemote(),
      /*expect_success=*/false);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(renderer_loader_factory.is_remote_url_loader_connected());
  ExpectMojoBadMessage(
      "Unexpected `resource_request` in "
      "KeepAliveURLLoaderFactoriesBase::CreateLoaderAndStart(): "
      "resource_request.keepalive must be true");
}

TEST_F(KeepAliveURLLoaderServiceTest, LoadTrustedRequestAndTerminate) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads trusted keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl), /*keepalive=*/true,
                            /*is_trusted=*/true),
      renderer_loader_client.BindNewPipeAndPassRemote(),
      /*expect_success=*/false);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(renderer_loader_factory.is_remote_url_loader_connected());
  ExpectMojoBadMessage(
      "Unexpected `resource_request` in "
      "KeepAliveURLLoaderFactoriesBase::CreateLoaderAndStart(): "
      "resource_request.trusted_params must not be set");
}

TEST_F(KeepAliveURLLoaderServiceTest, LoadRequestAfterPageIsUnloaded) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Deletes the current RenderFrameHost and then loads a keepalive request.
  DeleteContents();
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote(),
      /*expect_success=*/true);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 1);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
}

// This test initially provides an unbind factory to KeepAliveURLLoaderService.
// After that, provides a bound factory via UpdateFactory.
TEST_F(KeepAliveURLLoaderServiceTest, LoadRequestAfterUpdateFactory) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;

  // First, bind the service with a PendingSharedURLLoaderFactory that connects
  // to nothing.
  auto unbound_factory =
      std::make_unique<network::WrapperPendingSharedURLLoaderFactory>();
  auto context = loader_service().BindFactory(
      renderer_loader_factory.BindNewPipeAndPassReceiver(),
      network::SharedURLLoaderFactory::Create(std::move(unbound_factory)),
      static_cast<RenderFrameHostImpl*>(main_rfh())
          ->policy_container_host()
          ->Clone());
  context->OnDidCommitNavigation(GetNavigationHandle());
  {
    // Load a keepalive request. There should be no network loader created.
    MockReceiverURLLoaderClient renderer_loader_client;
    renderer_loader_factory.CreateLoaderAndStart(
        CreateResourceRequest(GURL(kTestRequestUrl)),
        renderer_loader_client.BindNewPipeAndPassRemote(),
        /*expect_success=*/true);
    EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  }

  // Second, update the service with a PendingSharedURLLoaderFactory that
  // connects to network loader factory as usual.
  renderer_loader_factory.reset_remote_url_loader();
  mojo::Remote<network::mojom::URLLoaderFactory> factory;
  network_url_loader_factory().Clone(factory.BindNewPipeAndPassReceiver());
  auto pending_factory = std::make_unique<blink::PendingURLLoaderFactoryBundle>(
      factory.Unbind(), blink::PendingURLLoaderFactoryBundle::SchemeMap(),
      blink::PendingURLLoaderFactoryBundle::OriginMap(),
      /*local_resource_loader_config=*/nullptr,
      /*bypass_redirect_checks=*/false);
  context->UpdateFactory(
      network::SharedURLLoaderFactory::Create(std::move(pending_factory)));
  {
    MockReceiverURLLoaderClient renderer_loader_client;
    renderer_loader_factory.CreateLoaderAndStart(
        CreateResourceRequest(GURL(kTestRequestUrl)),
        renderer_loader_client.BindNewPipeAndPassRemote(),
        /*expect_success=*/true);
    EXPECT_EQ(network_url_loader_factory().NumPending(), 1);
  }
}

TEST_F(KeepAliveURLLoaderServiceTest, ForwardOnReceiveResponse) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnReceiveResponse:
  // Expects underlying KeepAliveURLLoader forwards to `renderer_loader_client`.
  EXPECT_CALL(renderer_loader_client,
              OnReceiveResponse(ResponseHasHeader(kTestResponseHeaderName,
                                                  kTestResponseHeaderValue),
                                _, Eq(std::nullopt)))
      .Times(1);
  // Simluates receiving response in the network service.
  GetLastPendingRequest()->client->OnReceiveResponse(
      CreateResponseHead({{kTestResponseHeaderName, kTestResponseHeaderValue}}),
      /*body=*/{}, std::nullopt);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
}

TEST_F(KeepAliveURLLoaderServiceTest,
       ForwardRedirectsAndResponseToAttributionRequestHelper) {
  // The Attribution Manager uses the DataDecoder service, which, when an
  // InProcessDataDecoer object exists, will route to an internal in-process
  // instance.
  data_decoder::test::InProcessDataDecoder in_process_data_decoder;

  // Set up the Attribution Manager.
  auto mock_manager = std::make_unique<MockAttributionManager>();
  mock_manager->SetDataHostManager(
      std::make_unique<AttributionDataHostManagerImpl>(mock_manager.get()));
  MockAttributionManager* mock_attribution_manager = mock_manager.get();
  static_cast<StoragePartitionImpl*>(
      browser_context()->GetDefaultStoragePartition())
      ->OverrideAttributionManagerForTesting(std::move(mock_manager));

  // Loads keepalive request.
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);
  network::ResourceRequest request =
      CreateResourceRequest(GURL(kTestRequestUrl));
  request.attribution_reporting_eligibility =
      network::mojom::AttributionReportingEligibility::kEventSourceOrTrigger;
  renderer_loader_factory.CreateLoaderAndStart(
      std::move(request), renderer_loader_client.BindNewPipeAndPassRemote());

  // Simluates receiving a redirect in the network service.
  EXPECT_CALL(*mock_attribution_manager, HandleTrigger).Times(1);
  constexpr char kRegisterTriggerJson[] = R"json({ })json";
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead({{kAttributionReportingRegisterTriggerHeader,
                           kRegisterTriggerJson}}));

  // Simluates receiving response in the network service.
  EXPECT_CALL(*mock_attribution_manager, HandleSource).Times(1);
  constexpr char kRegisterSourceJson[] =
      R"json({"destination":"https://destination.example"})json";
  GetLastPendingRequest()->client->OnReceiveResponse(
      CreateResponseHead({{kAttributionReportingRegisterSourceHeader,
                           kRegisterSourceJson}}),
      /*body=*/{}, /*cached_metadata=*/std::nullopt);

  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest, ForwardErrorToAttributionRequestHelper) {
  // Set up the Attribution Manager.
  auto mock_manager = std::make_unique<MockAttributionManager>();
  auto mock_data_host_manager =
      std::make_unique<MockAttributionDataHostManager>();
  MockAttributionDataHostManager* mock_data_host_manager_ptr =
      mock_data_host_manager.get();
  mock_manager->SetDataHostManager(std::move(mock_data_host_manager));
  static_cast<StoragePartitionImpl*>(
      browser_context()->GetDefaultStoragePartition())
      ->OverrideAttributionManagerForTesting(std::move(mock_manager));

  // Loads keepalive request.
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);
  network::ResourceRequest request =
      CreateResourceRequest(GURL(kTestRequestUrl));
  request.attribution_reporting_eligibility =
      network::mojom::AttributionReportingEligibility::kEventSourceOrTrigger;
  renderer_loader_factory.CreateLoaderAndStart(
      std::move(request), renderer_loader_client.BindNewPipeAndPassRemote());

  // Simluates receiving error in the network service.
  EXPECT_CALL(*mock_data_host_manager_ptr,
              NotifyBackgroundRegistrationCompleted)
      .Times(1);
  GetLastPendingRequest()->client->OnComplete(
      network::URLLoaderCompletionStatus(-1));
  base::RunLoop().RunUntilIdle();
}

TEST_F(
    KeepAliveURLLoaderServiceTest,
    OnReceiveRedirectWithErrorRedirectMode_NotForwardedToAttributionRequestHelper) {
  // Set up the Attribution Manager.
  auto mock_manager = std::make_unique<MockAttributionManager>();
  auto mock_data_host_manager =
      std::make_unique<MockAttributionDataHostManager>();
  MockAttributionDataHostManager* mock_data_host_manager_ptr =
      mock_data_host_manager.get();
  mock_manager->SetDataHostManager(std::move(mock_data_host_manager));
  static_cast<StoragePartitionImpl*>(
      browser_context()->GetDefaultStoragePartition())
      ->OverrideAttributionManagerForTesting(std::move(mock_manager));

  // Loads keepalive request that redirects first, with error redirect_mode:
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);
  network::ResourceRequest request = CreateResourceRequest(
      GURL(kTestRequestUrl), /*keepalive=*/true,
      /*is_trusted=*/false, network::mojom::RedirectMode::kError);
  request.attribution_reporting_eligibility =
      network::mojom::AttributionReportingEligibility::kEventSourceOrTrigger;
  renderer_loader_factory.CreateLoaderAndStart(
      std::move(request), renderer_loader_client.BindNewPipeAndPassRemote());

  // Simluates receiving redirect in the network service.
  EXPECT_CALL(*mock_data_host_manager_ptr, NotifyBackgroundRegistrationData)
      .Times(0);
  EXPECT_CALL(*mock_data_host_manager_ptr,
              NotifyBackgroundRegistrationCompleted)
      .Times(1);
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnReceiveResponseAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  base::RunLoop().RunUntilIdle();

  // OnReceiveResponse:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveResponse(_, _, _)).Times(0);
  // Simluates receiving response in the network service.
  GetLastPendingRequest()->client->OnReceiveResponse(
      CreateResponseHead({{kTestResponseHeaderName, kTestResponseHeaderValue}}),
      /*body=*/{}, std::nullopt);
  base::RunLoop().RunUntilIdle();
  // The loader should have been deleted by the service.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest, DoNotForwardOnReceiveRedirect) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnReceiveRedirect:
  // Expects underlying KeepAliveURLLoader NOT forwards to
  // `renderer_loader_client`: all redirects are processed in browser, and will
  // only be forwarded after request completes/fails.
  EXPECT_CALL(renderer_loader_client,
              OnReceiveRedirect(_, ResponseHasHeader(kTestResponseHeaderName,
                                                     kTestResponseHeaderValue)))
      .Times(0);
  // Simluates receiving redirect in the network service.
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnReceiveRedirectAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request that redirects first:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl), /*keepalive=*/true,
                            /*is_trusted=*/false,
                            network::mojom::RedirectMode::kFollow),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  DeleteContents();
  base::RunLoop().RunUntilIdle();

  // OnReceiveRedirect:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveRedirect(_, _)).Times(0);
  // Simluates receiving redirect in the network service.
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();

  // Verifies URLLoader::FollowRedirect() is sent to network service.
  const auto& params =
      GetLastPendingRequest()->test_url_loader->follow_redirect_params();
  EXPECT_THAT(params, SizeIs(1));
  EXPECT_EQ(params[0].new_url, std::nullopt);
  EXPECT_THAT(params[0].removed_headers, IsEmpty());
  EXPECT_TRUE(params[0].modified_headers.IsEmpty());
  EXPECT_TRUE(params[0].modified_cors_exempt_headers.IsEmpty());
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnReceiveRedirectToUnSafeTargetAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request that redirects first:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl), /*keepalive=*/true,
                            /*is_trusted=*/false,
                            network::mojom::RedirectMode::kFollow),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  DeleteContents();
  base::RunLoop().RunUntilIdle();

  // OnReceiveRedirect:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveRedirect(_, _)).Times(0);
  // Simluates receiving unsafe redirect in the network service.
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestUnSafeRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();

  // Verifies URLLoader::FollowRedirect() is NOT sent to network service.
  const auto& params =
      GetLastPendingRequest()->test_url_loader->follow_redirect_params();
  EXPECT_THAT(params, IsEmpty());
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnReceiveRedirectWithErrorRedirectModeAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request that redirects first, with error redirect_mode:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl), /*keepalive=*/true,
                            /*is_trusted=*/false,
                            network::mojom::RedirectMode::kError),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  DeleteContents();
  base::RunLoop().RunUntilIdle();

  // OnReceiveRedirect:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveRedirect(_, _)).Times(0);
  // Simluates receiving redirect in the network service.
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();

  // Verifies URLLoader::FollowRedirect() is NOT sent to network service.
  const auto& params =
      GetLastPendingRequest()->test_url_loader->follow_redirect_params();
  EXPECT_THAT(params, IsEmpty());
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnReceiveRedirectViolatingCSPAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request that redirects first:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl), /*keepalive=*/true,
                            /*is_trusted=*/false,
                            network::mojom::RedirectMode::kFollow),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  DeleteContents();
  base::RunLoop().RunUntilIdle();

  // OnReceiveRedirect:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveRedirect(_, _)).Times(0);
  // Simluates receiving redirect in the network service.
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestViolatingCSPRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();

  // Verifies URLLoader::FollowRedirect() is NOT sent to network service.
  const auto& params =
      GetLastPendingRequest()->test_url_loader->follow_redirect_params();
  EXPECT_THAT(params, IsEmpty());
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest, ForwardOnReceiveEarlyHints) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnReceiveEarlyHints:
  // Expects underlying KeepAliveURLLoader forwards to `renderer_loader_client`.
  EXPECT_CALL(renderer_loader_client, OnReceiveEarlyHints(_)).Times(1);
  // Simluates receiving early hints in the network service.
  GetLastPendingRequest()->client->OnReceiveEarlyHints(
      CreateEarlyHints(GURL(kTestRequestUrl)));
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnReceiveEarlyHintsAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  base::RunLoop().RunUntilIdle();

  // OnReceiveEarlyHints:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveEarlyHints(_)).Times(0);
  // Simluates receiving early hints in the network service.
  GetLastPendingRequest()->client->OnReceiveEarlyHints(
      CreateEarlyHints(GURL(kTestRequestUrl)));
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest, ForwardOnUploadProgress) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnUploadProgress:
  const int64_t current_position = 5;
  const int64_t total_size = 100;
  base::OnceCallback<void()> callback;
  // Expects underlying KeepAliveURLLoader forwards to `renderer_loader_client`.
  EXPECT_CALL(renderer_loader_client,
              OnUploadProgress(Eq(current_position), Eq(total_size), _))
      .Times(1)
      .WillOnce(WithArg<2>([](base::OnceCallback<void()> callback) {
        // must be consumed.
        std::move(callback).Run();
      }));
  // Simluates receiving upload progress in the network service.
  GetLastPendingRequest()->client->OnUploadProgress(
      current_position, total_size, std::move(callback));
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest, ForwardOnTransferSizeUpdated) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnTransferSizeUpdated:
  const int32_t size_diff = 5;
  // Expects underlying KeepAliveURLLoader forwards to `renderer_loader_client`.
  EXPECT_CALL(renderer_loader_client, OnTransferSizeUpdated(Eq(size_diff)))
      .Times(1);
  // Simluates receiving transfer size update in the network service.
  GetLastPendingRequest()->client->OnTransferSizeUpdated(size_diff);
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest,
       OnTransferSizeUpdatedAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  base::RunLoop().RunUntilIdle();

  // OnTransferSizeUpdated:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnTransferSizeUpdated(_)).Times(0);
  // Simluates receiving transfer size update in the network service.
  const int32_t size_diff = 5;
  GetLastPendingRequest()->client->OnTransferSizeUpdated(size_diff);
  base::RunLoop().RunUntilIdle();
}

TEST_F(KeepAliveURLLoaderServiceTest, ForwardOnComplete) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnComplete:
  const network::URLLoaderCompletionStatus status{net::OK};
  // Expects underlying KeepAliveURLLoader forwards to `renderer_loader_client`.
  EXPECT_CALL(renderer_loader_client, OnComplete(Eq(status))).Times(1);
  // Simluates receiving completion status in the network service.
  GetLastPendingRequest()->client->OnComplete(status);
  base::RunLoop().RunUntilIdle();
  // The KeepAliveURLLoader should have been deleted.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest, OnCompleteAfterRendererIsDisconnected) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Disconnects and unbinds the receiver client & remote loader.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  base::RunLoop().RunUntilIdle();

  // OnComplete:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);
  // Simluates receiving completion status in the network service.
  const network::URLLoaderCompletionStatus status{net::OK};
  GetLastPendingRequest()->client->OnComplete(status);
  base::RunLoop().RunUntilIdle();
  // The KeepAliveURLLoader should have been deleted.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest, RendererDisconnectedBeforeOnComplete) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // OnReceiveResponse
  // Simluates receiving response in the network service.
  GetLastPendingRequest()->client->OnReceiveResponse(
      CreateResponseHead({{kTestResponseHeaderName, kTestResponseHeaderValue}}),
      /*body=*/{}, std::nullopt);

  // Disconnects and unbinds the receiver client & remote loader before
  // OnComplete is triggered.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  base::RunLoop().RunUntilIdle();

  // The KeepAliveURLLoader should have been deleted, even if OnComplete is not
  // triggered.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest,
       RendererConnectedAndThrottleCancelLoaderBeforeStartRequest) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  loader_service().SetURLLoaderThrottlesGetterForTesting(
      base::BindRepeating([]() {
        std::vector<std::unique_ptr<blink::URLLoaderThrottle>> ret;
        ret.emplace_back(std::make_unique<ConfigurableURLLoaderThrottle>(
            /*deferring=*/false, /*canceling_before_start=*/true,
            /*canceling_before_redirect=*/false));
        return ret;
      }));

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  base::RunLoop().RunUntilIdle();

  // The KeepAliveURLLoader should NOT be cancelled by the in-browser throttle,
  // as the loader is still connected to the renderer and thus should respect
  // in-renderer throttles.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);
}

TEST_F(KeepAliveURLLoaderServiceTest,
       RendererDisconnectedAndThrottleCancelLoaderBeforeStartRedirect) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  loader_service().SetURLLoaderThrottlesGetterForTesting(
      base::BindRepeating([]() {
        std::vector<std::unique_ptr<blink::URLLoaderThrottle>> ret;
        ret.emplace_back(std::make_unique<ConfigurableURLLoaderThrottle>(
            /*deferring=*/false, /*canceling_before_start=*/false,
            /*canceling_before_redirect=*/true));
        return ret;
      }));

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  // Disconnects and unbinds the receiver client & remote loader to simulate
  // the renderer gets disconnected before redirect.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  DeleteContents();
  base::RunLoop().RunUntilIdle();

  // OnReceiveRedirect:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveRedirect(_, _)).Times(0);
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);
  // Simluates receiving redirect in the network service.
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead(
          {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  base::RunLoop().RunUntilIdle();

  // The KeepAliveURLLoader should be cancelled by the in-browser throttle.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
}

TEST_F(KeepAliveURLLoaderServiceTest,
       RendererDisconnectedAndThrottleDeferLoaderBeforeStartRedirect) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  loader_service().SetURLLoaderThrottlesGetterForTesting(
      base::BindRepeating([]() {
        std::vector<std::unique_ptr<blink::URLLoaderThrottle>> ret;
        ret.emplace_back(std::make_unique<ConfigurableURLLoaderThrottle>(
            /*deferring=*/true, /*canceling_before_start=*/false,
            /*canceling_before_redirect=*/false));
        return ret;
      }));

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      CreateResourceRequest(GURL(kTestRequestUrl)),
      renderer_loader_client.BindNewPipeAndPassRemote());
  // Disconnects and unbinds the receiver client & remote loader to simulate
  // the renderer gets disconnected before redirect.
  renderer_loader_client.ResetReceiver();
  renderer_loader_factory.reset_remote_url_loader();
  DeleteContents();
  base::RunLoop().RunUntilIdle();

  // OnReceiveRedirect:
  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // Expects no forwarding.
  EXPECT_CALL(renderer_loader_client, OnReceiveRedirect(_, _)).Times(0);
  EXPECT_CALL(renderer_loader_client, OnComplete(_)).Times(0);

  // As the request loading is deferred by `ConfigurableURLLoaderThrottle` from
  // the beginning, there should be no requests to the network service.
  EXPECT_THAT(GetPendingRequests(), IsEmpty());
}

class FetchLaterKeepAliveURLLoaderServiceTest
    : public KeepAliveURLLoaderServiceTestBase {
 protected:
  void SetUp() override {
    feature_list().InitWithFeatures(
        {blink::features::kFetchLaterAPI,
         blink::features::kAttributionReportingInBrowserMigration},
        {});
    KeepAliveURLLoaderServiceTestBase::SetUp();
  }

  // Asks KeepAliveURLLoaderService to bind a FetchLaterLoaderFactory to the
  // given `remote_fetch_later_loader_factory`.
  // More than one factory can be bound to the same service.
  void BindFetchLaterLoaderFactory(
      FakeRemoteFetchLaterLoaderFactory& remote_fetch_later_loader_factory) {
    mojo::Remote<network::mojom::URLLoaderFactory> factory;
    network_url_loader_factory().Clone(factory.BindNewPipeAndPassReceiver());
    auto pending_factory =
        std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
            factory.Unbind());

    // Remote: `remote_fetch_later_loader_factory`
    // Receiver: Held in `loader_service_`.
    auto context = loader_service().BindFetchLaterLoaderFactory(
        remote_fetch_later_loader_factory
            .BindNewEndpointAndPassDedicatedReceiver(),
        network::SharedURLLoaderFactory::Create(std::move(pending_factory)),
        static_cast<RenderFrameHostImpl*>(main_rfh())
            ->policy_container_host()
            ->Clone());
    context->OnDidCommitNavigation(GetNavigationHandle());
  }
};

TEST_F(FetchLaterKeepAliveURLLoaderServiceTest,
       LoadFetchLaterRequestWithInvalidFeatureAndTerminate) {
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);

  // Loads FetchLater request (which is also keepalive request) under invalid
  // configuration:
  feature_list().Reset();
  feature_list().InitAndDisableFeature(blink::features::kFetchLaterAPI);
  renderer_loader_factory.CreateLoader(
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl)),
      /*expect_success=*/false);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(
      renderer_loader_factory.is_remote_fetch_later_loader_connected());
  ExpectMojoBadMessage(
      "Unexpected call to FetchLaterLoaderFactories::CreateLoader()");
}

TEST_F(FetchLaterKeepAliveURLLoaderServiceTest,
       LoadNonFetchLaterRequestAndTerminate) {
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);

  // Loads non-FetchLater keepalive request.
  renderer_loader_factory.CreateLoader(
      CreateResourceRequest(GURL(kTestRequestUrl)), /*expect_success=*/false);

  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(
      renderer_loader_factory.is_remote_fetch_later_loader_connected());
  ExpectMojoBadMessage(
      "Unexpected `resource_request.is_fetch_later_api` in "
      "FetchLaterLoaderFactories::CreateLoader(): must be set");
}

TEST_F(FetchLaterKeepAliveURLLoaderServiceTest,
       LoadFetchLaterRequestAndDeferred) {
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);

  // Loads FetchLater request (which is also keepalive request):
  renderer_loader_factory.CreateLoader(
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl)));

  // The KeepAliveURLLoaderService holds a deferred KeepAliveURLLoader.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // As the request is deferred, the pending URLoader in network is 0.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
}

// Creates a fetchLater request which is deferred by default. The mojo endpoints
// in renderer then gets disconnected, which should start the fetchLater
// request.
TEST_F(FetchLaterKeepAliveURLLoaderServiceTest,
       LoadFetchLaterRequestAndLoaderStayAliveAfterRendererIsDisconnected) {
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);

  // Loads FetchLater request (which is also keepalive request):
  renderer_loader_factory.CreateLoader(
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl)));
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // As the request is deferred, the pending URLoader in network is 0.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);

  // Simulates a renderer disconnection:
  // Disconnects and unbinds the remote loader, which should start all deferred
  // KeepAliveURLLoader.
  renderer_loader_factory.reset_remote_fetch_later_loader();
  base::RunLoop().RunUntilIdle();

  // Disconnected KeepAliveURLLoader is still alive.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 1u);
  // The network should now have created pending URLLoader.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 1);
}

// Creates a fetchLater request which is deferred by default. The mojo endpoints
// in renderer then gets disconnected, and then the loader gets dropped by
// browser due to exceeding internal timeout.
TEST_F(FetchLaterKeepAliveURLLoaderServiceTest,
       LoadFetchLaterRequestAndLoaderKilledAfterRendererIsDisconnected) {
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);

  // Loads FetchLater request (which is also keepalive request):
  renderer_loader_factory.CreateLoader(
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl)));
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // As the request is deferred, the pending URLoader in network is 0.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);

  // Simulates a renderer disconnection:
  // Disconnects and unbinds the remote loader, which should start all deferred
  // KeepAliveURLLoader.
  renderer_loader_factory.reset_remote_fetch_later_loader();
  base::RunLoop().RunUntilIdle();
  // Fast forwards `kDefaultDisconnectedKeepAliveURLLoaderTimeout` (30s).
  task_environment()->FastForwardBy(base::Seconds(30));

  // Disconnected KeepAliveURLLoader should be killed.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 0u);
  // The network should not create pending URLLoader.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
}

// Notifying KeepAliveURLLoaderService about shutdown should start any pending
// loaders.
TEST_F(FetchLaterKeepAliveURLLoaderServiceTest, Shutdown) {
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);

  // Loads FetchLater request (which is also keepalive request):
  renderer_loader_factory.CreateLoader(
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl)));
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // As the request is deferred, the pending URLoader in network is 0.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);

  loader_service().Shutdown();

  // The pending loader should still exist.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // There should be no disconnected loader.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 0u);
  // The network should now have created pending URLLoader.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 1);
}

TEST_F(FetchLaterKeepAliveURLLoaderServiceTest,
       ForwardRedirectsAndResponseToAttributionRequestHelper) {
  // The Attribution Manager uses the DataDecoder service, which, when an
  // InProcessDataDecoer object exists, will route to an internal in-process
  // instance.
  data_decoder::test::InProcessDataDecoder in_process_data_decoder;

  // Set up the Attribution Manager.
  auto mock_manager = std::make_unique<MockAttributionManager>();
  mock_manager->SetDataHostManager(
      std::make_unique<AttributionDataHostManagerImpl>(mock_manager.get()));
  MockAttributionManager* mock_attribution_manager = mock_manager.get();
  static_cast<StoragePartitionImpl*>(
      browser_context()->GetDefaultStoragePartition())
      ->OverrideAttributionManagerForTesting(std::move(mock_manager));

  // Loads FetchLater request (which is also keepalive request):
  FakeRemoteFetchLaterLoaderFactory renderer_loader_factory;
  BindFetchLaterLoaderFactory(renderer_loader_factory);
  network::ResourceRequest request =
      CreateFetchLaterResourceRequest(GURL(kTestRequestUrl));
  request.attribution_reporting_eligibility =
      network::mojom::AttributionReportingEligibility::kEventSourceOrTrigger;
  renderer_loader_factory.CreateLoader(std::move(request));
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // As the request is deferred, the pending URLoader in network is 0.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 0);
  // Simulate a shutdown to start the pending request.
  loader_service().Shutdown();
  // The pending loader should still exist.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  // There should be no disconnected loader.
  EXPECT_EQ(loader_service().NumDisconnectedLoadersForTesting(), 0u);
  // The network should now have created pending URLLoader.
  EXPECT_EQ(network_url_loader_factory().NumPending(), 1);

  base::RunLoop run_loop_1;

  // Simluates receiving a redirect in the network service.
  EXPECT_CALL(*mock_attribution_manager, HandleTrigger)
      .WillOnce([&](AttributionTrigger, GlobalRenderFrameHostId) {
        run_loop_1.Quit();
      });
  constexpr char kRegisterTriggerJson[] = R"json({ })json";
  GetLastPendingRequest()->client->OnReceiveRedirect(
      CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
      CreateResponseHead({{kAttributionReportingRegisterTriggerHeader,
                           kRegisterTriggerJson}}));
  run_loop_1.Run();

  base::RunLoop run_loop_2;

  // Simluates receiving response in the network service.
  EXPECT_CALL(*mock_attribution_manager, HandleSource)
      .WillOnce(
          [&](StorableSource, GlobalRenderFrameHostId) { run_loop_2.Quit(); });
  constexpr char kRegisterSourceJson[] =
      R"json({"destination":"https://destination.example"})json";
  GetLastPendingRequest()->client->OnReceiveResponse(
      CreateResponseHead(
          {{kAttributionReportingRegisterSourceHeader, kRegisterSourceJson}}),
      /*body=*/{}, /*cached_metadata=*/std::nullopt);
  run_loop_2.Run();
}

class KeepAliveURLLoaderServiceRetryTest
    : public KeepAliveURLLoaderServiceTestBase {
 protected:
  static constexpr int kMaxRetryCountForTesting = 10;
  static constexpr base::TimeDelta kMinRetryDeltaForTesting = base::Seconds(10);
  static constexpr double kMinRetryBackoffFactorForTesting = 10.0;
  static constexpr base::TimeDelta kMaxRetryAgeForTesting = base::Days(1);

  void SetUp() override {
    feature_list().InitWithFeaturesAndParameters(
        {{blink::features::kKeepAliveInBrowserMigration, {}},
         {blink::features::kAttributionReportingInBrowserMigration, {}},
         {blink::features::kFetchRetry,
          {
              {"max_retry_count",
               base::NumberToString(kMaxRetryCountForTesting)},
              {"min_retry_delta",
               base::NumberToString(kMinRetryDeltaForTesting.InSeconds()) +
                   "s"},
              {"min_retry_backoff",
               base::NumberToString(kMinRetryBackoffFactorForTesting)},
              {"max_retry_age",
               base::NumberToString(kMaxRetryAgeForTesting.InDays()) + "d"},
          }}},
        {});
    KeepAliveURLLoaderServiceTestBase::SetUp();
  }
};

// Test that setting retry options above the feature-param controlled limits
// results in adjustment of some of the options.
TEST_F(KeepAliveURLLoaderServiceRetryTest, AboveRetryLimits) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = kMaxRetryCountForTesting + 10;
  options.initial_delay = kMinRetryDeltaForTesting + base::Seconds(10);
  options.backoff_factor = kMinRetryBackoffFactorForTesting + 10.0;
  options.max_age = kMaxRetryAgeForTesting + base::Seconds(10);
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  // Max attempt will adjust to the feature param-controlled max attempt,
  // instead of using the requested max attempt.
  EXPECT_NE(loader->GetMaxAttemptsForRetry(), options.max_attempts);
  EXPECT_EQ(loader->GetMaxAttemptsForRetry(), kMaxRetryCountForTesting);

  // Initial delay will follow the requested initial delay, since it's ok to
  // exceed the feature param-controlled minimum initial delay.
  EXPECT_EQ(loader->GetInitialTimeDeltaForRetry(), options.initial_delay);
  EXPECT_NE(loader->GetInitialTimeDeltaForRetry(), kMinRetryDeltaForTesting);

  // Backoff factor will follow the requested backoff factor, since it's ok to
  // exceed the feature param-controlled minimum backoff factor.
  EXPECT_EQ(loader->GetBackoffFactorForRetry(), options.backoff_factor);
  EXPECT_NE(loader->GetBackoffFactorForRetry(),
            kMinRetryBackoffFactorForTesting);

  // Max age will adjust to the feature param-controlled max age,
  // instead of using the requested max age.
  EXPECT_NE(loader->GetMaxAgeForRetry(), options.max_age);
  EXPECT_EQ(loader->GetMaxAgeForRetry(), kMaxRetryAgeForTesting);
}

// Test that setting retry options below the feature-param controlled limits
// results in adjustment of some of the options.
TEST_F(KeepAliveURLLoaderServiceRetryTest, BelowRetryLimits) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = kMaxRetryCountForTesting - 1;
  options.initial_delay = kMinRetryDeltaForTesting - base::Milliseconds(10);
  options.backoff_factor = kMinRetryBackoffFactorForTesting - 1.0;
  options.max_age = kMaxRetryAgeForTesting - base::Seconds(10);
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  // Max attempt will follow the requested options, since it's ok to go below
  // the feature param-controlled minimum max attempt.
  EXPECT_EQ(loader->GetMaxAttemptsForRetry(), options.max_attempts);
  EXPECT_NE(loader->GetMaxAttemptsForRetry(), kMaxRetryCountForTesting);

  // Initial delay will adjust to the feature param-controlled min initial
  // delay, instead of using the requested initial delay.
  EXPECT_NE(loader->GetInitialTimeDeltaForRetry(), options.initial_delay);
  EXPECT_EQ(loader->GetInitialTimeDeltaForRetry(), kMinRetryDeltaForTesting);

  // Backoff factor will adjust to the feature param-controlled min backoff
  // factor, instead of using the requested backoff factor.
  EXPECT_NE(loader->GetBackoffFactorForRetry(), options.backoff_factor);
  EXPECT_EQ(loader->GetBackoffFactorForRetry(),
            kMinRetryBackoffFactorForTesting);

  // Max age will follow the requested options, since it's ok to go below the
  // feature param-controlled minimum max age.
  EXPECT_EQ(loader->GetMaxAgeForRetry(), options.max_age);
  EXPECT_NE(loader->GetMaxAgeForRetry(), kMaxRetryAgeForTesting);
}

// Test that setting only the max attempt would cause the other options to use
// default values set by the feature params.
TEST_F(KeepAliveURLLoaderServiceRetryTest, RetryLimitsDefaults) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  // Max attempt will follow the requested options.
  EXPECT_EQ(loader->GetMaxAttemptsForRetry(), options.max_attempts);
  EXPECT_NE(loader->GetMaxAttemptsForRetry(), kMaxRetryCountForTesting);

  // All other options will use the feature param-controlled values.
  EXPECT_EQ(loader->GetInitialTimeDeltaForRetry(), kMinRetryDeltaForTesting);
  EXPECT_EQ(loader->GetBackoffFactorForRetry(),
            kMinRetryBackoffFactorForTesting);
  EXPECT_EQ(loader->GetMaxAgeForRetry(), kMaxRetryAgeForTesting);
}

// Test which errors are eligible for retry.
TEST_F(KeepAliveURLLoaderServiceRetryTest, ErrorCodeRetryEligibility) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  net::Error eligible_errors[] = {
      net::ERR_TIMED_OUT, net::ERR_CONNECTION_TIMED_OUT,
      net::ERR_CONNECTION_CLOSED, net::ERR_CONNECTION_REFUSED,
      net::ERR_CONNECTION_RESET, net::ERR_CONNECTION_FAILED,
      net::ERR_ADDRESS_UNREACHABLE, net::ERR_NETWORK_CHANGED,
      // Proxy/tunnel-specific connection issues.
      net::ERR_TUNNEL_CONNECTION_FAILED, net::ERR_PROXY_CONNECTION_FAILED,
      net::ERR_SOCKS_CONNECTION_FAILED, net::ERR_HTTP2_PING_FAILED,
      net::ERR_HTTP2_PROTOCOL_ERROR, net::ERR_QUIC_PROTOCOL_ERROR,
      // DNS failures.
      net::ERR_NAME_NOT_RESOLVED, net::ERR_INTERNET_DISCONNECTED,
      net::ERR_NAME_RESOLUTION_FAILED};
  for (net::Error error : eligible_errors) {
    ASSERT_TRUE(
        loader->IsEligibleForRetry(network::URLLoaderCompletionStatus(error)))
        << " Should be eligible for retry: " << error;
  }
  // Not passing an error code is possible for disconnect loader timeout
  // failure.
  ASSERT_TRUE(loader->IsEligibleForRetry(std::nullopt));
  // Other error codes are not eligible for retry. Testing a sample here.
  ASSERT_FALSE(
      loader->IsEligibleForRetry(network::URLLoaderCompletionStatus(net::OK)));
  ASSERT_FALSE(loader->IsEligibleForRetry(
      network::URLLoaderCompletionStatus(net::ERR_ABORTED)));
}

// Test failing with an eligible error with OnComplete causes the fetch to be
// retried.
TEST_F(KeepAliveURLLoaderServiceRetryTest, OnCompleteWillBeRetried) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  EXPECT_TRUE(loader->IsAttemptingRetry());
}
// Test which errors are eligible for retry when opting in to retry only if the
// server is not reached yet.
TEST_F(KeepAliveURLLoaderServiceRetryTest,
       ErrorCodeRetryEligibility_OnlyIfServerUnreached) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  options.retry_only_if_server_unreached = true;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  net::Error eligible_errors[] = {
      net::ERR_CONNECTION_REFUSED,       net::ERR_ADDRESS_UNREACHABLE,
      net::ERR_TUNNEL_CONNECTION_FAILED, net::ERR_PROXY_CONNECTION_FAILED,
      net::ERR_SOCKS_CONNECTION_FAILED,  net::ERR_NAME_NOT_RESOLVED,
      net::ERR_NAME_RESOLUTION_FAILED};
  for (net::Error error : eligible_errors) {
    ASSERT_TRUE(
        loader->IsEligibleForRetry(network::URLLoaderCompletionStatus(error)))
        << " Should be eligible for retry: " << error;
  }
  net::Error ineligible_errors[] = {
      net::ERR_TIMED_OUT, net::ERR_CONNECTION_TIMED_OUT,
      net::ERR_CONNECTION_CLOSED, net::ERR_CONNECTION_RESET,
      net::ERR_CONNECTION_FAILED, net::ERR_NETWORK_CHANGED,
      // Proxy/tunnel-specific connection issues.
      net::ERR_HTTP2_PING_FAILED, net::ERR_HTTP2_PROTOCOL_ERROR,
      net::ERR_QUIC_PROTOCOL_ERROR,
      // DNS failures.
      net::ERR_INTERNET_DISCONNECTED};
  for (net::Error error : ineligible_errors) {
    ASSERT_FALSE(
        loader->IsEligibleForRetry(network::URLLoaderCompletionStatus(error)))
        << " Should not be eligible for retry: " << error;
  }
  // Not passing an error code is possible for disconnect loader timeout
  // failure. We can't guarantee that the server has not been reached yet since
  // there's no error information.
  ASSERT_FALSE(loader->IsEligibleForRetry(std::nullopt));
  // Other error codes are also not eligible for retry. Testing a sample here.
  ASSERT_FALSE(
      loader->IsEligibleForRetry(network::URLLoaderCompletionStatus(net::OK)));
  ASSERT_FALSE(loader->IsEligibleForRetry(
      network::URLLoaderCompletionStatus(net::ERR_ABORTED)));
}

// Test failing with an eligible error with CancelWithStatus causes the fetch to
// be retreid.
TEST_F(KeepAliveURLLoaderServiceRetryTest, CancelWithStatusWillBeRetried) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->CancelWithStatus(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  EXPECT_TRUE(loader->IsAttemptingRetry());
}

// Test that failing a request with no retry options won't be retried.
TEST_F(KeepAliveURLLoaderServiceRetryTest, NoRetryOptionsWillNotBeRetried) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  // The loader is deleted as it can't be retried.
  EXPECT_FALSE(loader.get());
}

// Test that failing a request to non-HTTPs will not be retried.
TEST_F(KeepAliveURLLoaderServiceRetryTest, NonHTTPSWillNotBeRetried) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL("http://foo.com"));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  // The loader is deleted as it can't be retried.
  EXPECT_FALSE(loader.get());
}

// Test that failing a request using a POST method will not be retried if the
// retry options doesn't specify it wants to retry non-idempotent failures.
TEST_F(KeepAliveURLLoaderServiceRetryTest,
       POSTWillNotBeRetriedUnlessRequested) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;
  resource_request.method = "POST";

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  // The loader is deleted as it can't be retried.
  EXPECT_FALSE(loader.get());
}

// Test that failing a request after it has received a response will not be
// retried.
TEST_F(KeepAliveURLLoaderServiceRetryTest, ReceivedResponseWillNotBeRetried) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 1;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->OnReceiveResponse(
      CreateResponseHead({{kTestResponseHeaderName, kTestResponseHeaderValue}}),
      /*body=*/{}, std::nullopt);
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));

  // The loader can't be retried. Note that it won't be immediately deleted like
  // in other cases, because it will forward the response to the renderer.
  EXPECT_TRUE(loader->IsForwardURLLoadStarted());
}

// Test that hitting the redirect limit won't trigger a retry.
TEST_F(KeepAliveURLLoaderServiceRetryTest,
       ExceededRedirectLimitWillNotBeRetried) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 2;
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);

  // Simulate hitting kMaxRedirects - 1 redirects, then failing the request.
  for (int i = 1; i < net::URLRequest::kMaxRedirects; ++i) {
    loader->EndReceiveRedirect(
        CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
        CreateResponseHead(
            {{kTestResponseHeaderName, kTestResponseHeaderValue}}));
  }
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  // The load should be eligible for retry still.
  EXPECT_TRUE(loader->IsAttemptingRetry());
  EXPECT_FALSE(loader->IsForwardURLLoadStarted());

  // But if we hit another redirect, the loader will fail with
  // TOO_MANY_REDIRECTS, which is not retriable.
  loader->EndReceiveRedirect(CreateRedirectInfo(GURL(kTestRedirectRequestUrl)),
                             CreateResponseHead({{kTestResponseHeaderName,
                                                  kTestResponseHeaderValue}}));

  // The loader can't be retried. Note that it won't be immediately deleted like
  // in other cases, because it will forward the redirects to the renderer.
  EXPECT_TRUE(loader->IsForwardURLLoadStarted());
}

// Check that a retrying loader will be deleted when it reaches max age.
TEST_F(KeepAliveURLLoaderServiceRetryTest, SelfDeletionOnMaxAge) {
  FakeRemoteURLLoaderFactory renderer_loader_factory;
  MockReceiverURLLoaderClient renderer_loader_client;
  BindKeepAliveURLLoaderFactory(renderer_loader_factory);

  auto resource_request = CreateResourceRequest(GURL(kTestRequestUrl));
  network::FetchRetryOptions options;
  options.max_attempts = 10;
  options.max_age = base::Seconds(10);
  resource_request.fetch_retry_options = options;

  // Loads keepalive request:
  renderer_loader_factory.CreateLoaderAndStart(
      resource_request, renderer_loader_client.BindNewPipeAndPassRemote());
  ASSERT_EQ(network_url_loader_factory().NumPending(), 1);
  ASSERT_EQ(loader_service().NumLoadersForTesting(), 1u);

  // Simmulate failure that will cause the loader to attempt retry.
  base::WeakPtr<KeepAliveURLLoader> loader =
      loader_service().GetLoaderWithRequestIdForTesting(
          FakeRemoteURLLoaderFactory::kRequestId);
  loader->OnComplete(
      network::URLLoaderCompletionStatus(net::ERR_NETWORK_CHANGED));
  EXPECT_TRUE(loader->IsAttemptingRetry());

  // Fast forwards to just before the max age timeout fires.
  task_environment()->FastForwardBy(options.max_age.value() - base::Seconds(5));
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 1u);
  EXPECT_TRUE(loader->IsAttemptingRetry());

  // Fast forward to after the max age timeout fires.
  task_environment()->FastForwardBy(base::Seconds(10));
  base::RunLoop().RunUntilIdle();

  // The loader should be deleted after hitting max age.
  EXPECT_EQ(loader_service().NumLoadersForTesting(), 0u);
  EXPECT_FALSE(loader.get());
}

}  // namespace content
