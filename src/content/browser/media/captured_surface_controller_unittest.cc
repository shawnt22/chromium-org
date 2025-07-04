// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/captured_surface_controller.h"

#include <limits>
#include <list>
#include <memory>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/run_loop.h"
#include "content/browser/host_zoom_map_impl.h"
#include "content/browser/media/captured_surface_control_permission_manager.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/test/browser_task_environment.h"
#include "content/test/test_web_contents.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "url/gurl.h"

namespace content {
namespace {

using ::blink::mojom::CapturedWheelAction;
using ::blink::mojom::CapturedWheelActionPtr;
using ::blink::mojom::ZoomLevelAction;
using CSCResult = ::blink::mojom::CapturedSurfaceControlResult;
using CSCPermissionResult =
    CapturedSurfaceControlPermissionManager::PermissionResult;

const char* const kUrlString = "http://www.example.com/";

enum class Boundary {
  kMin,
  kMax,
};

enum class CapturedSurfaceControlAPI {
  kSendWheel,
  kIncreaseZoomLevel,
  kDecreaseZoomLevel,
  kResetZoomLevel,
  kRequestPermission,
};

ZoomLevelAction ToZoomLevelAction(CapturedSurfaceControlAPI input) {
  switch (input) {
    case CapturedSurfaceControlAPI::kIncreaseZoomLevel:
      return ZoomLevelAction::kIncrease;
    case CapturedSurfaceControlAPI::kDecreaseZoomLevel:
      return ZoomLevelAction::kDecrease;
    case CapturedSurfaceControlAPI::kResetZoomLevel:
      return ZoomLevelAction::kReset;
    case CapturedSurfaceControlAPI::kSendWheel:
    case CapturedSurfaceControlAPI::kRequestPermission:
      break;
  }
  NOTREACHED() << "Not a ZoomLevelAction.";
}

// The concept of "zoom level" is overloaded. For clarity, when we mean the
// "factor times 100," we'll just name it "zoom level percentage," at least
// in tests.
double GetZoomLevelPercentageFor(WebContents* wc) {
  CHECK(wc);
  return 100 * blink::ZoomLevelToZoomFactor(HostZoomMap::GetZoomLevel(wc));
}

// Make an arbitrary valid CapturedWheelAction.
CapturedWheelActionPtr MakeCapturedWheelActionPtr() {
  return CapturedWheelAction::New(
      /*x=*/0,
      /*y=*/0,
      /*wheel_delta_x=*/0,
      /*wheel_delta_y=*/0);
}

class InputObserver : public RenderWidgetHost::InputEventObserver {
 public:
  struct ExpectedWheelEvent {
    double x = 0;
    double y = 0;
    double delta_x = 0;
    double delta_y = 0;
  };

  ~InputObserver() override { EXPECT_TRUE(expected_events_.empty()); }

  void OnInputEvent(const RenderWidgetHost& widget,
                    const blink::WebInputEvent& event) override {
    if (event.GetType() != blink::WebInputEvent::Type::kMouseWheel) {
      return;
    }

    const blink::WebMouseWheelEvent& wheel_event =
        static_cast<const blink::WebMouseWheelEvent&>(event);

    CHECK(!expected_events_.empty());
    ExpectedWheelEvent expected_event = expected_events_.front();
    expected_events_.pop_front();

    EXPECT_EQ(expected_event.x, wheel_event.PositionInWidget().x());
    EXPECT_EQ(expected_event.y, wheel_event.PositionInWidget().y());
    EXPECT_EQ(expected_event.delta_x, wheel_event.delta_x);
    EXPECT_EQ(expected_event.delta_y, wheel_event.delta_y);
  }

  void AddExpectation(ExpectedWheelEvent expected_event) {
    expected_events_.push_back(expected_event);

    // The wheel event chains are closed with a scroll of zero
    // magnitude in the same location.
    expected_events_.push_back(ExpectedWheelEvent{.x = expected_event.x,
                                                  .y = expected_event.y,
                                                  .delta_x = 0,
                                                  .delta_y = 0});
  }

 private:
  std::list<ExpectedWheelEvent> expected_events_;
};

class TestView : public TestRenderWidgetHostView {
 public:
  explicit TestView(RenderWidgetHostImpl* rwhi)
      : TestRenderWidgetHostView(rwhi) {}
  ~TestView() override = default;

  void SetSize(const gfx::Size& size) override { size_ = size; }

  gfx::Size GetVisibleViewportSize() override { return size_; }

 private:
  gfx::Size size_;
};

// Simulates a tab.
//
// Wraps a `WebContents`, which is the main object of interest, along with a
// `TestView`, which is essentially a `RenderWidgetHostView` that allows us to
// set a custom size, which is needed when testing SendWheel().
//
// This object records the original `RenderWidgetHostView` and injects it back
// from the destructor. This prevents LSAN/ASAN failures. This functionality is
// the main value proposition of this class.
class TestTab {
 public:
  static constexpr gfx::Size kDefaultViewportSize = gfx::Size(100, 400);

  explicit TestTab(BrowserContext* browser_context,
                   std::optional<GURL> gurl = std::nullopt)
      : web_contents_(MakeTestWebContents(browser_context)) {
    // Store the original RenderWidgetHost, allowing it to be injected back from
    // the destructor.
    original_rwhv_ = GetRenderWidgetHostImpl()->GetView();

    // Set a new RenderWidgetHost that allows us control over its size.
    rwhv_ = std::make_unique<TestView>(GetRenderWidgetHostImpl());
    SetView(rwhv_.get());
    SetSize(kDefaultViewportSize);

    if (gurl.has_value()) {
      web_contents_->NavigateAndCommit(gurl.value());
    }
  }

  virtual ~TestTab() {
    SetView(original_rwhv_);
    original_rwhv_ = nullptr;
  }

  TestWebContents* web_contents() const { return web_contents_.get(); }

  WebContentsMediaCaptureId GetWebContentsMediaCaptureId() const {
    RenderFrameHost* const rfh = web_contents_->GetPrimaryMainFrame();
    return WebContentsMediaCaptureId(rfh->GetProcess()->GetDeprecatedID(),
                                     rfh->GetRoutingID());
  }

  void SetSize(const gfx::Size& size) { rwhv_->SetSize(size); }

  RenderWidgetHostImpl* GetRenderWidgetHostImpl() const {
    return RenderWidgetHostImpl::From(
        web_contents_->GetPrimaryMainFrame()->GetRenderWidgetHost());
  }

  void Focus() {
    web_contents_->GetPrimaryMainFrame()->GetRenderWidgetHost()->Focus();
    FrameTree& frame_tree = web_contents_->GetPrimaryFrameTree();
    FrameTreeNode* const root = frame_tree.root();
    frame_tree.SetFocusedFrame(
        root, root->current_frame_host()->GetSiteInstance()->group());
  }

  double GetZoomLevelPercentage() {
    return GetZoomLevelPercentageFor(web_contents_.get());
  }

 protected:
  static std::unique_ptr<TestWebContents> MakeTestWebContents(
      BrowserContext* browser_context) {
    scoped_refptr<SiteInstance> instance =
        SiteInstance::Create(browser_context);
    instance->GetOrCreateProcessForTesting()->Init();
    return TestWebContents::Create(browser_context, std::move(instance));
  }

  void SetView(RenderWidgetHostViewBase* rwhv) {
    GetRenderWidgetHostImpl()->SetView(rwhv);
  }

 private:
  const std::unique_ptr<TestWebContents> web_contents_;
  std::unique_ptr<TestView> rwhv_;
  raw_ptr<RenderWidgetHostViewBase> original_rwhv_ = nullptr;
};

class MockCapturedSurfaceControlPermissionManager
    : public CapturedSurfaceControlPermissionManager {
 public:
  MockCapturedSurfaceControlPermissionManager(
      GlobalRenderFrameHostId capturer_rfh_id)
      : CapturedSurfaceControlPermissionManager(capturer_rfh_id) {}
  ~MockCapturedSurfaceControlPermissionManager() override = default;

  void SetPermissionResult(CSCPermissionResult result) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    result_ = result;
  }

  void CheckPermission(
      base::OnceCallback<void(CSCPermissionResult)> callback) override {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    CHECK(result_.has_value());
    std::move(callback).Run(result_.value());
  }

 private:
  std::optional<CSCPermissionResult> result_;
};

using MockPermissionManager = MockCapturedSurfaceControlPermissionManager;

class MockObserver : public content::WebContentsObserver {
 public:
  explicit MockObserver(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents) {}
  ~MockObserver() override = default;

  // content::WebContentsObserver:
  MOCK_METHOD(void, OnCapturedSurfaceControl, (), (override));
};

// Make a callback that expects `result` and then unblock `run_loop`.
base::OnceCallback<void(CSCResult)> MakeCallbackExpectingResult(
    base::RunLoop* run_loop,
    CSCResult expected_result,
    MockWidgetInputHandler* mock_widget_input_handler) {
  return base::BindOnce(
      [](base::RunLoop* run_loop, CSCResult expected_result,
         MockWidgetInputHandler* mock_widget_input_handler, CSCResult result) {
        EXPECT_EQ(result, expected_result);

        // Run callbacks corresponding to `DispatchEvent` method in
        // `WidgetInputHandler` to allow processing of inputs in
        // `MouseWheelEventQueue`.
        if (mock_widget_input_handler) {
          MockWidgetInputHandler::MessageVector messages =
              mock_widget_input_handler->GetAndResetDispatchedMessages();
          if (!messages.empty()) {
            messages.clear();
            mock_widget_input_handler->FlushReceiverForTesting();
          }
        }

        run_loop->Quit();
      },
      run_loop, expected_result, mock_widget_input_handler);
}

class CapturedSurfaceControllerTestBase : public RenderViewHostTestHarness {
 public:
  ~CapturedSurfaceControllerTestBase() override = default;

  void SetUp() override {
    RenderViewHostTestHarness::SetUp();

    SetUpTestTabs();
    StartCaptureOf(*capturee_);
    AwaitWebContentsResolution();

    auto* test_host = static_cast<TestRenderWidgetHost*>(
        capturee_->GetRenderWidgetHostImpl());
    mojo::Remote<blink::mojom::WidgetInputHandler> remote;
    mock_widget_input_handler_ = std::make_unique<MockWidgetInputHandler>(
        remote.BindNewPipeAndPassReceiver(), mojo::NullRemote());
    test_host->GetRenderInputRouter()->SetWidgetInputHandlerForTesting(
        std::move(remote));
  }

  void SetUpTestTabs(bool focus_capturer = true) {
    capturer_ = std::make_unique<TestTab>(GetBrowserContext());
    capturee_ =
        std::make_unique<TestTab>(GetBrowserContext(), GURL(kUrlString));
    if (focus_capturer) {
      capturer_->Focus();
    }
  }

  void StartCaptureOf(const TestTab& tab) {
    auto permission_manager = std::make_unique<MockPermissionManager>(
        capturer_->web_contents()->GetPrimaryMainFrame()->GetGlobalId());
    permission_manager_ = permission_manager.get();

    // `base::Unretained(this)` is safe because `this` owns `controller_` and
    // therefore has a longer lifetime.
    controller_ = CapturedSurfaceController::CreateForTesting(
        capturer_->web_contents()->GetPrimaryMainFrame()->GetGlobalId(),
        tab.GetWebContentsMediaCaptureId(), std::move(permission_manager),
        base::BindRepeating(
            &CapturedSurfaceControllerTestBase::OnZoomLevelChange,
            base::Unretained(this)),
        base::BindRepeating(
            &CapturedSurfaceControllerTestBase::OnWebContentsResolved,
            base::Unretained(this)));
  }

  void TearDown() override {
    mock_widget_input_handler_.reset();
    permission_manager_ = nullptr;
    controller_.reset();
    capturer_.reset();
    capturee_.reset();

    RenderViewHostTestHarness::TearDown();
  }

  void SetZoomFactor(std::unique_ptr<TestTab>& tab, double zoom_factor) {
    WebContents* const wc = tab->web_contents();

    content::HostZoomMap* const host_zoom_map =
        content::HostZoomMap::GetForWebContents(wc);
    CHECK(host_zoom_map);

    host_zoom_map->SetTemporaryZoomLevel(
        wc->GetPrimaryMainFrame()->GetGlobalId(),
        blink::ZoomFactorToZoomLevel(zoom_factor));

    if (!blink::ZoomValuesEqual(GetZoomLevelPercentageFor(wc),
                                100 * zoom_factor)) {
      FAIL();  // Abort test, not just the helper method.
    }
  }

  void AwaitWebContentsResolution() {
    CHECK(!wc_resolution_run_loop_);
    wc_resolution_run_loop_ = std::make_unique<base::RunLoop>();
    wc_resolution_run_loop_->Run();
    wc_resolution_run_loop_.reset();
  }

  void AwaitOnZoomLevelChange() {
    CHECK(!on_zoom_level_change_run_loop_);
    on_zoom_level_change_run_loop_ = std::make_unique<base::RunLoop>();
    on_zoom_level_change_run_loop_->Run();
    on_zoom_level_change_run_loop_.reset();
  }

  void OnZoomLevelChange(int zoom_level) {
    if (on_zoom_level_change_run_loop_) {
      on_zoom_level_change_run_loop_->Quit();
    }
    zoom_level_ = zoom_level;
  }

  void OnWebContentsResolved(base::WeakPtr<WebContents> wc) {
    if (wc_resolution_run_loop_) {
      wc_resolution_run_loop_->Quit();
    }
    last_resolved_web_contents_ = wc;
  }

 protected:
  std::unique_ptr<CapturedSurfaceController> controller_;
  raw_ptr<MockPermissionManager> permission_manager_ = nullptr;
  std::unique_ptr<TestTab> capturer_;
  std::unique_ptr<TestTab> capturee_;
  std::unique_ptr<base::RunLoop> wc_resolution_run_loop_;
  std::unique_ptr<base::RunLoop> on_zoom_level_change_run_loop_;
  std::optional<base::WeakPtr<WebContents>> last_resolved_web_contents_;
  std::optional<int> zoom_level_;
  std::unique_ptr<MockWidgetInputHandler> mock_widget_input_handler_ = nullptr;
};

class CapturedSurfaceControllerSendWheelTest
    : public CapturedSurfaceControllerTestBase {
 public:
  ~CapturedSurfaceControllerSendWheelTest() override = default;

  void SetUp() override {
    CapturedSurfaceControllerTestBase::SetUp();

    input_observer_ = std::make_unique<InputObserver>();
    capturee_->GetRenderWidgetHostImpl()->AddInputEventObserver(
        input_observer_.get());
  }

  void TearDown() override {
    capturee_->GetRenderWidgetHostImpl()->RemoveInputEventObserver(
        input_observer_.get());

    CapturedSurfaceControllerTestBase::TearDown();
  }

 protected:
  std::unique_ptr<InputObserver> input_observer_;
};

TEST_F(CapturedSurfaceControllerSendWheelTest, CorrectScaling) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturee_->SetSize(gfx::Size(256, 4096));
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 256 * 0.25, .y = 4096 * 0.5, .delta_x = 300, .delta_y = 400});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_F(CapturedSurfaceControllerSendWheelTest,
       GracefullyHandleZeroWidthCapturedSurface) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturee_->SetSize(gfx::Size(0, 4096));
  base::RunLoop run_loop;
  // Note absence of call to input_observer_->AddExpectation().
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kUnknownError,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_F(CapturedSurfaceControllerSendWheelTest,
       GracefullyHandleZeroHeightCapturedSurface) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturee_->SetSize(gfx::Size(256, 0));
  base::RunLoop run_loop;
  // Note absence of call to input_observer_->AddExpectation().
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kUnknownError,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_F(CapturedSurfaceControllerSendWheelTest,
       GracefullyHandleExtremelyNarrowCapturedSurface) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturee_->SetSize(gfx::Size(1, 4096));
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 0, .y = 4096 * 0.5, .delta_x = 300, .delta_y = 400});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_F(CapturedSurfaceControllerSendWheelTest,
       GracefullyHandleExtremelyShortCapturedSurface) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturee_->SetSize(gfx::Size(256, 1));
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 256 * 0.25, .y = 0, .delta_x = 300, .delta_y = 400});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

class CapturedSurfaceControllerZoomEventTest
    : public CapturedSurfaceControllerTestBase {
 public:
  ~CapturedSurfaceControllerZoomEventTest() override = default;

  void SetUp() override {
    CapturedSurfaceControllerTestBase::SetUp();
    new_capturee_ = std::make_unique<TestTab>(GetBrowserContext());
  }

  void TearDown() override {
    new_capturee_.reset();
    CapturedSurfaceControllerTestBase::TearDown();
  }

 protected:
  std::unique_ptr<TestTab> new_capturee_;
};

TEST_F(CapturedSurfaceControllerZoomEventTest, ZoomEventProducedByZoomChange) {
  HostZoomMap::SetZoomLevel(capturee_->web_contents(),
                            blink::ZoomFactorToZoomLevel(0.9));
  AwaitOnZoomLevelChange();
  ASSERT_TRUE(zoom_level_);
  EXPECT_EQ(zoom_level_, 90);
}

TEST_F(CapturedSurfaceControllerZoomEventTest,
       ZoomEventProducedByTemporaryZoomChange) {
  content::HostZoomMap* const host_zoom_map =
      content::HostZoomMap::GetForWebContents(capturee_->web_contents());
  CHECK(host_zoom_map);

  host_zoom_map->SetTemporaryZoomLevel(
      capturee_->web_contents()->GetPrimaryMainFrame()->GetGlobalId(),
      blink::ZoomFactorToZoomLevel(0.9));

  AwaitOnZoomLevelChange();
  ASSERT_TRUE(zoom_level_);
  EXPECT_EQ(zoom_level_, 90);
}

TEST_F(CapturedSurfaceControllerZoomEventTest, ZoomEventUpdateTarget) {
  const RenderFrameHost* const new_main_rfh =
      new_capturee_->web_contents()->GetPrimaryMainFrame();
  const WebContentsMediaCaptureId new_wc_id(
      new_main_rfh->GetProcess()->GetDeprecatedID(),
      new_main_rfh->GetRoutingID());
  controller_->UpdateCaptureTarget(new_wc_id);

  AwaitWebContentsResolution();

  // Set a temporary zoom level so only the second WebContents is affected.
  HostZoomMapImpl* mock_widget_input_handler_zoom_map =
      static_cast<HostZoomMapImpl*>(
          HostZoomMap::GetForWebContents(new_capturee_->web_contents()));
  mock_widget_input_handler_zoom_map->SetTemporaryZoomLevel(
      new_main_rfh->GetGlobalId(), blink::ZoomFactorToZoomLevel(1.1));

  AwaitOnZoomLevelChange();
  ASSERT_TRUE(zoom_level_);
  EXPECT_EQ(zoom_level_, 110);
}

class CapturedSurfaceControllerUpdateZoomLevelTest
    : public CapturedSurfaceControllerTestBase {
 public:
  CapturedSurfaceControllerUpdateZoomLevelTest()
      : min_zoom_factor_(blink::kPresetBrowserZoomFactors.front()),
        max_zoom_factor_(blink::kPresetBrowserZoomFactors.back()) {}

  ~CapturedSurfaceControllerUpdateZoomLevelTest() override = default;

  void SetUp() override {
    CapturedSurfaceControllerTestBase::SetUp();
    permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  }

 protected:
  const double min_zoom_factor_;
  const double max_zoom_factor_;
};

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       IncreaseZoomLevelSucceedsUntilMaxLevel) {
  SetZoomFactor(capturee_, min_zoom_factor_);

  for (int i = 1; i < blink::kPresetBrowserZoomFactors.size(); ++i) {
    base::RunLoop run_loop;
    controller_->UpdateZoomLevel(
        ZoomLevelAction::kIncrease,
        MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                    mock_widget_input_handler_.get()));
    run_loop.Run();

    EXPECT_TRUE(
        blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                               100 * blink::kPresetBrowserZoomFactors[i]));
  }
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       IncreaseZoomLevelSucceedsBetweenCanonicalValues) {
  for (int i = 0; i < blink::kPresetBrowserZoomFactors.size() - 1; ++i) {
    // Average the two factors and set the zoom level to that,
    // thereby getting a non-canonical zoom level.
    const double mid_zoom_factor = (blink::kPresetBrowserZoomFactors[i] +
                                    blink::kPresetBrowserZoomFactors[i + 1]) /
                                   2;
    SetZoomFactor(capturee_, mid_zoom_factor);

    base::RunLoop run_loop;
    controller_->UpdateZoomLevel(
        ZoomLevelAction::kIncrease,
        MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                    mock_widget_input_handler_.get()));
    run_loop.Run();

    EXPECT_TRUE(
        blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                               100 * blink::kPresetBrowserZoomFactors[i + 1]));
  }
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       DecreaseZoomLevelSucceedsUntilMinLevel) {
  SetZoomFactor(capturee_, max_zoom_factor_);

  for (int i = blink::kPresetBrowserZoomFactors.size() - 2; i >= 0; --i) {
    base::RunLoop run_loop;
    controller_->UpdateZoomLevel(
        ZoomLevelAction::kDecrease,
        MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                    mock_widget_input_handler_.get()));
    run_loop.Run();

    EXPECT_TRUE(
        blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                               100 * blink::kPresetBrowserZoomFactors[i]));
  }
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       DecreaseZoomLevelSucceedsBetweenCanonicalValues) {
  for (int i = 0; i < blink::kPresetBrowserZoomFactors.size() - 1; ++i) {
    // Average the two factors and set the zoom level to that,
    // thereby getting a non-canonical zoom level.
    const double mid_zoom_factor = (blink::kPresetBrowserZoomFactors[i] +
                                    blink::kPresetBrowserZoomFactors[i + 1]) /
                                   2;
    SetZoomFactor(capturee_, mid_zoom_factor);

    base::RunLoop run_loop;
    controller_->UpdateZoomLevel(
        ZoomLevelAction::kDecrease,
        MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                    mock_widget_input_handler_.get()));
    run_loop.Run();

    EXPECT_TRUE(
        blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                               100 * blink::kPresetBrowserZoomFactors[i]));
  }
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       IncreaseZoomLevelFailsWhenAtMaxLevel) {
  SetZoomFactor(capturee_, max_zoom_factor_);

  // Main expectation - the call to UpdateZoomLevel() fails.
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kIncrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kMaxZoomLevel,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  // Secondary expectation - zoom level remains unchanged.
  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                                     100 * max_zoom_factor_));
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       DecreaseZoomLevelFailsWhenAtMinLevel) {
  SetZoomFactor(capturee_, min_zoom_factor_);

  // Main expectation - the call to UpdateZoomLevel() fails.
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kDecrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kMinZoomLevel,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  // Secondary expectation - zoom level remains unchanged.
  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                                     100 * min_zoom_factor_));
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       IncreaseZoomLevelFailsWhenWithinEpsilonOfMaxLevel) {
  // Start out within epsilon of the maximum zoom level.
  // (Note that this has to be even smaller than the value within
  // blink::ZoomValuesEqual()).
  constexpr double kEpsilon = 0.000001;
  SetZoomFactor(capturee_, max_zoom_factor_ - kEpsilon);

  // Secondary expectation - zoom level remains unchanged.
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kIncrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kMaxZoomLevel,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  // Secondary expectation - zoom level updated to actual maximum.
  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                                     100 * max_zoom_factor_));
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       DecreaseZoomLevelFailsWhenWithinEpsilonOfMinLevel) {
  // Start out within epsilon of the minimum zoom level.
  // (Note that this has to be even smaller than the value within
  // blink::ZoomValuesEqual()).
  constexpr double kEpsilon = 0.000001;
  SetZoomFactor(capturee_, min_zoom_factor_ + kEpsilon);

  // Secondary expectation - zoom level remains unchanged.
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kDecrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kMinZoomLevel,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  // Secondary expectation - zoom level updated to actual maximum.
  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                                     100 * min_zoom_factor_));
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       IncreaseZoomLevelSucceedsWhenWithinEpsilonOfMaxLevel) {
  // Set the captured tab to a zoom level that would appear to the user as
  // roughly 1% less than the maximum.
  SetZoomFactor(capturee_, max_zoom_factor_ - 0.01);

  // Main expectation - the call to UpdateZoomLevel() succeeds.
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kIncrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  // Secondary expectation - zoom level updated to maximum.
  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                                     100 * max_zoom_factor_));
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       DecreaseZoomLevelSucceedsWhenWithinEpsilonOfMinLevel) {
  // Set the captured tab to a zoom level that would appear to the user as
  // roughly 1% more than the minimum.
  SetZoomFactor(capturee_, min_zoom_factor_ + 0.01);

  // Main expectation - the call to UpdateZoomLevel() succeeds.
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kDecrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  // Secondary expectation - zoom level updated to minimum.
  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(),
                                     100 * min_zoom_factor_));
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       ResetZoomLevelSucceedsAtAllCanonicalLevels) {
  for (double zoom_factor : blink::kPresetBrowserZoomFactors) {
    SetZoomFactor(capturee_, zoom_factor);

    base::RunLoop run_loop;
    controller_->UpdateZoomLevel(
        ZoomLevelAction::kReset,
        MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                    mock_widget_input_handler_.get()));
    run_loop.Run();

    EXPECT_TRUE(
        blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(), 100));
  }
}

TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       ResetZoomLevelSucceedsBetweenCanonicalLevels) {
  for (int i = 1; i < blink::kPresetBrowserZoomFactors.size() - 1; ++i) {
    // Average the two factors and set the zoom level to that,
    // thereby getting a non-canonical zoom level.
    const double mid_zoom_factor = (blink::kPresetBrowserZoomFactors[i] +
                                    blink::kPresetBrowserZoomFactors[i + 1]) /
                                   2;
    SetZoomFactor(capturee_, mid_zoom_factor);

    base::RunLoop run_loop;
    controller_->UpdateZoomLevel(
        ZoomLevelAction::kReset,
        MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                    mock_widget_input_handler_.get()));
    run_loop.Run();

    EXPECT_TRUE(
        blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(), 100));
  }
}

// This is vicariously tested by ResetZoomLevelSucceedsAtAllCanonicalLevels,
// but it is important enough a use case to merit its own explicit test.
TEST_F(CapturedSurfaceControllerUpdateZoomLevelTest,
       ResetZoomLevelSucceedsEvenWhenAlreadyAtDefaultZoom) {
  SetZoomFactor(capturee_, 1.0);
  ASSERT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(), 100));

  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kReset,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();

  EXPECT_TRUE(blink::ZoomValuesEqual(capturee_->GetZoomLevelPercentage(), 100));
}

class CapturedSurfaceControllerUpdateZoomLevelImpermanenceTest
    : public CapturedSurfaceControllerTestBase {
 public:
  ~CapturedSurfaceControllerUpdateZoomLevelImpermanenceTest() override =
      default;
};

// Ensure the effect does not extend to other tabs, even if they are dialed
// to the same origin.
TEST_F(CapturedSurfaceControllerUpdateZoomLevelImpermanenceTest,
       UpdateZoomLevelOnlyAffectsCapturedTab) {
  ASSERT_EQ(capturee_->GetZoomLevelPercentage(), 100);

  // Create another tab and navigate it to the same URL as the captured tab.
  auto duplicate_tab =
      std::make_unique<TestTab>(GetBrowserContext(), GURL(kUrlString));
  ASSERT_EQ(duplicate_tab->GetZoomLevelPercentage(), 100);

  // Change the zoom-level on the captured tab.
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kIncrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
  ASSERT_GT(capturee_->GetZoomLevelPercentage(), 100);

  // Setting the zoom level only affected the captured tab, not the
  // Chrome-level settings for the origin.
  EXPECT_EQ(duplicate_tab->GetZoomLevelPercentage(), 100);
}

// Ensure the effect does not get persisted and does not affect newly
// opened tabs later, even if they are navigated to the same URL.
TEST_F(CapturedSurfaceControllerUpdateZoomLevelImpermanenceTest,
       UpdateZoomLevelEffectsDoNotPersistAfterClosed) {
  ASSERT_EQ(capturee_->GetZoomLevelPercentage(), 100);

  // Change the zoom-level on the captured tab.
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ZoomLevelAction::kIncrease,
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
  ASSERT_GT(capturee_->GetZoomLevelPercentage(), 100);

  // Close the tab.
  capturee_.reset();

  // Create another tab and navigate it to the same URL as the captured tab.
  auto new_tab =
      std::make_unique<TestTab>(GetBrowserContext(), GURL(kUrlString));

  // Setting the zoom level only affected the captured tab, not the
  // Chrome-level settings for the origin.
  EXPECT_EQ(new_tab->GetZoomLevelPercentage(), 100);
}

class CapturedSurfaceControllerInterfaceTestBase
    : public CapturedSurfaceControllerTestBase {
 public:
  explicit CapturedSurfaceControllerInterfaceTestBase(
      CapturedSurfaceControlAPI tested_interface)
      : tested_interface_(tested_interface) {}
  ~CapturedSurfaceControllerInterfaceTestBase() override = default;

  void RunTestedActionAndExpect(base::RunLoop* run_loop,
                                CSCResult expected_result) {
    switch (tested_interface_) {
      case CapturedSurfaceControlAPI::kSendWheel:
        controller_->SendWheel(
            MakeCapturedWheelActionPtr(),
            MakeCallbackExpectingResult(run_loop, expected_result,
                                        mock_widget_input_handler_.get()));
        return;
      case CapturedSurfaceControlAPI::kIncreaseZoomLevel:
      case CapturedSurfaceControlAPI::kDecreaseZoomLevel:
      case CapturedSurfaceControlAPI::kResetZoomLevel:
        controller_->UpdateZoomLevel(
            ToZoomLevelAction(tested_interface_),
            MakeCallbackExpectingResult(run_loop, expected_result,
                                        /*mock_widget_input_handler=*/nullptr));
        return;
      case CapturedSurfaceControlAPI::kRequestPermission:
        controller_->RequestPermission(MakeCallbackExpectingResult(
            run_loop, expected_result, /*mock_widget_input_handler=*/nullptr));
        return;
    }
    NOTREACHED();
  }

 protected:
  const CapturedSurfaceControlAPI tested_interface_;
};

class CapturedSurfaceControllerInterfaceTest
    : public CapturedSurfaceControllerInterfaceTestBase,
      public ::testing::WithParamInterface<CapturedSurfaceControlAPI> {
 public:
  CapturedSurfaceControllerInterfaceTest()
      : CapturedSurfaceControllerInterfaceTestBase(GetParam()) {}

  ~CapturedSurfaceControllerInterfaceTest() override = default;
};

INSTANTIATE_TEST_SUITE_P(
    ,
    CapturedSurfaceControllerInterfaceTest,
    ::testing::Values(CapturedSurfaceControlAPI::kSendWheel,
                      CapturedSurfaceControlAPI::kIncreaseZoomLevel,
                      CapturedSurfaceControlAPI::kDecreaseZoomLevel,
                      CapturedSurfaceControlAPI::kResetZoomLevel,
                      CapturedSurfaceControlAPI::kRequestPermission));

TEST_P(CapturedSurfaceControllerInterfaceTest, SuccessReportedIfPermitted) {
  base::RunLoop run_loop;
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  RunTestedActionAndExpect(&run_loop, CSCResult::kSuccess);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerInterfaceTest, NoPermissionReportedIfDenied) {
  base::RunLoop run_loop;
  permission_manager_->SetPermissionResult(CSCPermissionResult::kDenied);
  RunTestedActionAndExpect(&run_loop, CSCResult::kNoPermissionError);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerInterfaceTest,
       UnknownErrorReportedIfPermissionError) {
  base::RunLoop run_loop;
  permission_manager_->SetPermissionResult(CSCPermissionResult::kError);
  RunTestedActionAndExpect(&run_loop, CSCResult::kUnknownError);
  run_loop.Run();
}

// Simulate the captured tab being closed after permission is granted but before
// the controller has time to process the response from the permission manager.
TEST_P(CapturedSurfaceControllerInterfaceTest,
       SurfaceNotFoundReportedIfTabClosedBeforePromptResponseHandled) {
  base::RunLoop run_loop;
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturee_.reset();
  RunTestedActionAndExpect(&run_loop, CSCResult::kCapturedSurfaceNotFoundError);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerInterfaceTest,
       SurfaceNotFoundReportedIfCaptureTargetUpdatedToNonTabSurface) {
  base::RunLoop run_loop;
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  controller_->UpdateCaptureTarget(WebContentsMediaCaptureId());
  RunTestedActionAndExpect(&run_loop, CSCResult::kCapturedSurfaceNotFoundError);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerInterfaceTest,
       CapturerNotFoundErrorReportedIfCapturerClosed) {
  base::RunLoop run_loop;
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  capturer_.reset();
  RunTestedActionAndExpect(&run_loop, CSCResult::kCapturerNotFoundError);
  run_loop.Run();
}

// Test suite ensuring that API calls before/after the WebContents ID is
// resolved to a base::WeakPtr<WebContents> behave as expected.
class CapturedSurfaceControllerWebContentsResolutionTest
    : public CapturedSurfaceControllerInterfaceTestBase,
      public ::testing::WithParamInterface<CapturedSurfaceControlAPI> {
 public:
  CapturedSurfaceControllerWebContentsResolutionTest()
      : CapturedSurfaceControllerInterfaceTestBase(GetParam()) {}

  ~CapturedSurfaceControllerWebContentsResolutionTest() override = default;

  void SetUp() override {
    // Intentionally skip CapturedSurfaceControllerInterfaceTestBase's SetUp(),
    // and therefore also CapturedSurfaceControllerTestBase's SetUp().
    RenderViewHostTestHarness::SetUp();

    // Prepare a new tab to capture instead of the original one.
    new_capturee_ = std::make_unique<TestTab>(GetBrowserContext());
  }

  void TearDown() override {
    new_capturee_.reset();

    CapturedSurfaceControllerInterfaceTestBase::TearDown();
  }

 protected:
  std::unique_ptr<TestTab> new_capturee_;
};

INSTANTIATE_TEST_SUITE_P(
    ,
    CapturedSurfaceControllerWebContentsResolutionTest,
    ::testing::Values(CapturedSurfaceControlAPI::kSendWheel,
                      CapturedSurfaceControlAPI::kIncreaseZoomLevel,
                      CapturedSurfaceControlAPI::kDecreaseZoomLevel,
                      CapturedSurfaceControlAPI::kResetZoomLevel));

TEST_P(CapturedSurfaceControllerWebContentsResolutionTest,
       ApiInvocationAfterWebContentsResolutionSucceeds) {
  SetUpTestTabs();  // Triggers resolution but does not await it.
  StartCaptureOf(*capturee_);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  AwaitWebContentsResolution();

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop, CSCResult::kSuccess);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerWebContentsResolutionTest,
       ApiInvocationPriorToWebContentsResolutionFails) {
  SetUpTestTabs();  // Triggers resolution but does not await it.
  StartCaptureOf(*capturee_);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop, CSCResult::kCapturedSurfaceNotFoundError);
  run_loop.Run();

  AwaitWebContentsResolution();
}

TEST_P(
    CapturedSurfaceControllerWebContentsResolutionTest,
    ApiInvocationPriorToWebContentsResolutionFailsButSubsequentCallsAreNotBlocked) {
  // Setup - repeat ApiInvocationPriorToWebContentsResolutionFails.
  {
    SetUpTestTabs();  // Triggers resolution but does not await it.
    StartCaptureOf(*capturee_);
    permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

    base::RunLoop run_loop;
    RunTestedActionAndExpect(&run_loop,
                             CSCResult::kCapturedSurfaceNotFoundError);
    run_loop.Run();

    AwaitWebContentsResolution();
  }

  // After AwaitWebContentsResolution() is called, subsequent API calls succeed.
  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop, CSCResult::kSuccess);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerWebContentsResolutionTest,
       MultiplePendingResolutions) {
  SetUpTestTabs();  // Triggers resolution but does not await it.
  StartCaptureOf(*capturee_);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  // The original resolution has not yet resolved.
  ASSERT_FALSE(last_resolved_web_contents_.has_value());

  // Updating to capture another tab schedules a new task to resolve.
  controller_->UpdateCaptureTarget(
      new_capturee_->GetWebContentsMediaCaptureId());

  // Neither resolutions has completed at this point.
  ASSERT_FALSE(last_resolved_web_contents_.has_value());

  // We await the resolution to be considered complete.
  // This should only happen after the last pending task resolves.
  // In our cases, that is for the new tab. The first response
  // should be ignored.
  AwaitWebContentsResolution();
  ASSERT_TRUE(last_resolved_web_contents_.has_value());
  EXPECT_EQ(last_resolved_web_contents_.value().get(),
            new_capturee_->web_contents());
}

// Similar to CapturedSurfaceControllerWebContentsResolutionTest,
// but focuses on calls to UpdateCaptureTarget(), which also trigger resolution.
class CapturedSurfaceControllerWebContentsResolutionOfUpdatesTest
    : public CapturedSurfaceControllerInterfaceTestBase,
      public ::testing::WithParamInterface<CapturedSurfaceControlAPI> {
 public:
  CapturedSurfaceControllerWebContentsResolutionOfUpdatesTest()
      : CapturedSurfaceControllerInterfaceTestBase(GetParam()) {}

  ~CapturedSurfaceControllerWebContentsResolutionOfUpdatesTest() override =
      default;

  void SetUp() override {
    // Unlike CapturedSurfaceControllerWebContentsResolutionTest, the current
    // test works well with the parent's SetUp(), which awaits the resolution
    // of the *first* ID. This is due to the current test's focus on what
    // happens before/after the call to UpdateCaptureTarget().
    CapturedSurfaceControllerInterfaceTestBase::SetUp();

    permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

    // Prepare a new tab to capture instead of the original one.
    new_capturee_ = std::make_unique<TestTab>(GetBrowserContext());
  }

  void TearDown() override {
    new_capturee_.reset();

    CapturedSurfaceControllerInterfaceTestBase::TearDown();
  }

 protected:
  std::unique_ptr<TestTab> new_capturee_;
};

INSTANTIATE_TEST_SUITE_P(
    ,
    CapturedSurfaceControllerWebContentsResolutionOfUpdatesTest,
    ::testing::Values(CapturedSurfaceControlAPI::kSendWheel,
                      CapturedSurfaceControlAPI::kIncreaseZoomLevel,
                      CapturedSurfaceControlAPI::kDecreaseZoomLevel,
                      CapturedSurfaceControlAPI::kResetZoomLevel));

TEST_P(
    CapturedSurfaceControllerWebContentsResolutionOfUpdatesTest,
    AfterUpdateCaptureTargetApiInvocationAfterToWebContentsResolutionSucceeds) {
  // Call UpdateCaptureTarget() - capturing a new tab.
  controller_->UpdateCaptureTarget(
      new_capturee_->GetWebContentsMediaCaptureId());
  AwaitWebContentsResolution();

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop, CSCResult::kSuccess);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerWebContentsResolutionOfUpdatesTest,
       AfterUpdateCaptureTargetApiInvocationPriorToWebContentsResolutionFails) {
  // Call UpdateCaptureTarget() - capturing a new tab.
  controller_->UpdateCaptureTarget(
      new_capturee_->GetWebContentsMediaCaptureId());
  // Note absence of call to AwaitWebContentsResolution().

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop, CSCResult::kCapturedSurfaceNotFoundError);
  run_loop.Run();

  AwaitWebContentsResolution();
}

// Test suite ensuring that API calls before/after the WebContents ID is
// resolved to a base::WeakPtr<WebContents> behave as expected.
class CapturedSurfaceControllerSelfCaptureTest
    : public CapturedSurfaceControllerInterfaceTestBase,
      public ::testing::WithParamInterface<CapturedSurfaceControlAPI> {
 public:
  CapturedSurfaceControllerSelfCaptureTest()
      : CapturedSurfaceControllerInterfaceTestBase(GetParam()) {}

  ~CapturedSurfaceControllerSelfCaptureTest() override = default;

  void SetUp() override {
    // Intentionally skip CapturedSurfaceControllerInterfaceTestBase's SetUp(),
    // and therefore also CapturedSurfaceControllerTestBase's SetUp().
    RenderViewHostTestHarness::SetUp();
    SetUpTestTabs();
  }
};

INSTANTIATE_TEST_SUITE_P(
    ,
    CapturedSurfaceControllerSelfCaptureTest,
    ::testing::Values(CapturedSurfaceControlAPI::kSendWheel,
                      CapturedSurfaceControlAPI::kIncreaseZoomLevel,
                      CapturedSurfaceControlAPI::kDecreaseZoomLevel,
                      CapturedSurfaceControlAPI::kResetZoomLevel));

TEST_P(CapturedSurfaceControllerSelfCaptureTest, SelfCaptureDisallowed) {
  StartCaptureOf(*capturer_);
  AwaitWebContentsResolution();
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop,
                           CSCResult::kDisallowedForSelfCaptureError);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerSelfCaptureTest,
       UpdateCaptureTargetToOtherTabEnablesCapturedSurfaceControl) {
  StartCaptureOf(*capturer_);
  AwaitWebContentsResolution();
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  controller_->UpdateCaptureTarget(capturee_->GetWebContentsMediaCaptureId());
  AwaitWebContentsResolution();

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop, CSCResult::kSuccess);
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerSelfCaptureTest,
       UpdateCaptureTargetToCapturingTabDisablesCapturedSurfaceControl) {
  StartCaptureOf(*capturee_);
  AwaitWebContentsResolution();
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  {
    base::RunLoop run_loop;
    RunTestedActionAndExpect(&run_loop, CSCResult::kSuccess);
    run_loop.Run();
  }

  controller_->UpdateCaptureTarget(capturer_->GetWebContentsMediaCaptureId());
  AwaitWebContentsResolution();

  base::RunLoop run_loop;
  RunTestedActionAndExpect(&run_loop,
                           CSCResult::kDisallowedForSelfCaptureError);
  run_loop.Run();
}

// This test suite checks correct clamping of x/y wheel-deltas to min/max.
//
// The suite is parameterized on the *zoom* level because that affects
// the values that will ultimately be fed into the UI system, and checking
// at both the min/max zoom levels increases coverage somewhat.
//
// The suite is *not* parameterized on the wheel deltas themselves, as that
// would increase test complexity and reduce confidence in test correctness.
class CapturedSurfaceControllerSendWheelClampTest
    : public CapturedSurfaceControllerSendWheelTest,
      public ::testing::WithParamInterface<Boundary> {
 public:
  CapturedSurfaceControllerSendWheelClampTest()
      : zoom_level_boundary_(GetParam()) {}
  ~CapturedSurfaceControllerSendWheelClampTest() override = default;

 protected:
  int zoom_level() const {
    static const double kMin = 100 * blink::kMaximumBrowserZoomFactor;
    static const double kMax = 100 * blink::kMinimumBrowserZoomFactor;
    switch (zoom_level_boundary_) {
      case Boundary::kMin:
        return static_cast<int>(std::ceil(kMin));
      case Boundary::kMax:
        return static_cast<int>(std::floor(kMax));
    }
    NOTREACHED();
  }

 private:
  const Boundary zoom_level_boundary_;
};

INSTANTIATE_TEST_SUITE_P(,
                         CapturedSurfaceControllerSendWheelClampTest,
                         ::testing::Values(Boundary::kMin, Boundary::kMax));

TEST_P(CapturedSurfaceControllerSendWheelClampTest, ClampMinWheelDeltaX) {
  using WheelDeltaType = decltype(CapturedWheelAction::wheel_delta_x);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 0,
      .y = 0,
      .delta_x = -CapturedSurfaceController::kMaxWheelDeltaMagnitude,
      .delta_y = 0});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0,
          /*y=*/0,
          /*wheel_delta_x=*/std::numeric_limits<WheelDeltaType>::min(),
          /*wheel_delta_y=*/0),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerSendWheelClampTest, ClampMaxWheelDeltaX) {
  using WheelDeltaType = decltype(CapturedWheelAction::wheel_delta_x);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 0,
      .y = 0,
      .delta_x = CapturedSurfaceController::kMaxWheelDeltaMagnitude,
      .delta_y = 0});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0,
          /*y=*/0,
          /*wheel_delta_x=*/std::numeric_limits<WheelDeltaType>::max(),
          /*wheel_delta_y=*/0),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerSendWheelClampTest, ClampMinWheelDeltaY) {
  using WheelDeltaType = decltype(CapturedWheelAction::wheel_delta_y);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 0,
      .y = 0,
      .delta_x = 0,
      .delta_y = -CapturedSurfaceController::kMaxWheelDeltaMagnitude});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0,
          /*y=*/0,
          /*wheel_delta_x=*/0,
          /*wheel_delta_y=*/std::numeric_limits<WheelDeltaType>::min()),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_P(CapturedSurfaceControllerSendWheelClampTest, ClampMaxWheelDeltaY) {
  using WheelDeltaType = decltype(CapturedWheelAction::wheel_delta_y);
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  base::RunLoop run_loop;
  input_observer_->AddExpectation(InputObserver::ExpectedWheelEvent{
      .x = 0,
      .y = 0,
      .delta_x = 0,
      .delta_y = CapturedSurfaceController::kMaxWheelDeltaMagnitude});
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0,
          /*y=*/0,
          /*wheel_delta_x=*/0,
          /*wheel_delta_y=*/std::numeric_limits<WheelDeltaType>::max()),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

class WebContentsObserverCscNotifiedBySendWheelTest
    : public CapturedSurfaceControllerTestBase {
 public:
  ~WebContentsObserverCscNotifiedBySendWheelTest() override = default;
};

TEST_F(WebContentsObserverCscNotifiedBySendWheelTest,
       NotifiedBySendWheelIfSuccessful) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);
  testing::StrictMock<MockObserver> observer(capturer_->web_contents());
  EXPECT_CALL(observer, OnCapturedSurfaceControl()).Times(1);

  base::RunLoop run_loop;
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_F(WebContentsObserverCscNotifiedBySendWheelTest,
       NotNotifiedBySendWheelIfUnsuccessful) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kDenied);

  testing::StrictMock<MockObserver> observer(capturer_->web_contents());
  EXPECT_CALL(observer, OnCapturedSurfaceControl()).Times(0);

  base::RunLoop run_loop;
  controller_->SendWheel(
      CapturedWheelAction::New(
          /*x=*/0.25,
          /*y=*/0.5,
          /*wheel_delta_x=*/300,
          /*wheel_delta_y=*/400),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kNoPermissionError,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

class WebContentsObserverCscNotifiedByUpdateZoomLevelTest
    : public CapturedSurfaceControllerTestBase,
      public ::testing::WithParamInterface<CapturedSurfaceControlAPI> {
 public:
  WebContentsObserverCscNotifiedByUpdateZoomLevelTest()
      : tested_interface_(GetParam()) {}
  ~WebContentsObserverCscNotifiedByUpdateZoomLevelTest() override = default;

 protected:
  const CapturedSurfaceControlAPI tested_interface_;
};

INSTANTIATE_TEST_SUITE_P(
    ,
    WebContentsObserverCscNotifiedByUpdateZoomLevelTest,
    ::testing::Values(CapturedSurfaceControlAPI::kIncreaseZoomLevel,
                      CapturedSurfaceControlAPI::kDecreaseZoomLevel,
                      CapturedSurfaceControlAPI::kResetZoomLevel));

TEST_P(WebContentsObserverCscNotifiedByUpdateZoomLevelTest,
       NotifiedByUpdateZoomLevelIfSuccessful) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kGranted);

  testing::StrictMock<MockObserver> observer(capturer_->web_contents());
  EXPECT_CALL(observer, OnCapturedSurfaceControl()).Times(1);

  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ToZoomLevelAction(tested_interface_),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kSuccess,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

TEST_P(WebContentsObserverCscNotifiedByUpdateZoomLevelTest,
       NotNotifiedByUpdateZoomLevelIfUnsuccessful) {
  permission_manager_->SetPermissionResult(CSCPermissionResult::kDenied);

  testing::StrictMock<MockObserver> observer(capturer_->web_contents());
  EXPECT_CALL(observer, OnCapturedSurfaceControl()).Times(0);

  base::RunLoop run_loop;
  controller_->UpdateZoomLevel(
      ToZoomLevelAction(tested_interface_),
      MakeCallbackExpectingResult(&run_loop, CSCResult::kNoPermissionError,
                                  mock_widget_input_handler_.get()));
  run_loop.Run();
}

}  // namespace
}  // namespace content
