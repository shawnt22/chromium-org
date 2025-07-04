// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "components/input/render_input_router.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "base/tracing/protos/chrome_track_event.pbzero.h"
#include "cc/input/browser_controls_offset_tag_modifications.h"
#include "components/input/input_constants.h"
#include "components/input/input_router_config_helper.h"
#include "components/input/input_router_impl.h"
#include "components/input/render_input_router_client.h"
#include "components/input/render_widget_host_input_event_router.h"
#include "components/input/render_widget_host_view_input.h"
#include "components/input/switches.h"
#include "components/input/touch_emulator.h"
#include "components/input/utils.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/latency/latency_info.h"

using blink::WebGestureEvent;
using blink::WebInputEvent;

namespace input {
namespace {

using ::perfetto::protos::pbzero::ChromeLatencyInfo2;

class UnboundWidgetInputHandler : public blink::mojom::WidgetInputHandler {
 public:
  void SetFocus(blink::mojom::FocusState focus_state) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void MouseCaptureLost() override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void SetEditCommandsForNextKeyEvent(
      std::vector<blink::mojom::EditCommandPtr> commands) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void CursorVisibilityChanged(bool visible) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void ImeSetComposition(const std::u16string& text,
                         const std::vector<ui::ImeTextSpan>& ime_text_spans,
                         const gfx::Range& range,
                         int32_t start,
                         int32_t end,
                         ImeSetCompositionCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void ImeCommitText(const std::u16string& text,
                     const std::vector<ui::ImeTextSpan>& ime_text_spans,
                     const gfx::Range& range,
                     int32_t relative_cursor_position,
                     ImeCommitTextCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void ImeFinishComposingText(bool keep_selection) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void RequestTextInputStateUpdate() override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void RequestCompositionUpdates(bool immediate_request,
                                 bool monitor_request) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void DispatchEvent(std::unique_ptr<blink::WebCoalescedInputEvent> event,
                     DispatchEventCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void DispatchNonBlockingEvent(
      std::unique_ptr<blink::WebCoalescedInputEvent> event) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
  void WaitForInputProcessed(WaitForInputProcessedCallback callback) override {
    DLOG(WARNING) << "Input request on unbound interface";
  }
#if BUILDFLAG(IS_ANDROID)
  void AttachSynchronousCompositor(
      mojo::PendingRemote<blink::mojom::SynchronousCompositorControlHost>
          control_host,
      mojo::PendingAssociatedRemote<blink::mojom::SynchronousCompositorHost>
          host,
      mojo::PendingAssociatedReceiver<blink::mojom::SynchronousCompositor>
          compositor_request) override {
    NOTREACHED() << "Input request on unbound interface";
  }
#endif
  void GetFrameWidgetInputHandler(
      mojo::PendingAssociatedReceiver<blink::mojom::FrameWidgetInputHandler>
          request) override {
    NOTREACHED() << "Input request on unbound interface";
  }
  void UpdateBrowserControlsState(
      cc::BrowserControlsState constraints,
      cc::BrowserControlsState current,
      bool animate,
      const std::optional<cc::BrowserControlsOffsetTagModifications>&
          offset_tag_modifications) override {
    NOTREACHED() << "Input request on unbound interface";
  }
};

base::LazyInstance<UnboundWidgetInputHandler>::Leaky g_unbound_input_handler =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

RenderInputRouter::~RenderInputRouter() {
  TRACE_EVENT("input", "RenderInputRouter::~RenderInputRouter");
}

RenderInputRouter::RenderInputRouter(
    RenderInputRouterClient* host,
    std::unique_ptr<FlingSchedulerBase> fling_scheduler,
    RenderInputRouterDelegate* delegate,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : should_disable_hang_monitor_(
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kDisableHangMonitor)),
      hung_renderer_delay_(kHungRendererDelay),
      fling_scheduler_(std::move(fling_scheduler)),
      latency_tracker_(
          std::make_unique<RenderInputRouterLatencyTracker>(delegate)),
      render_input_router_client_(host),
      delegate_(delegate),
      task_runner_(std::move(task_runner)) {
  TRACE_EVENT("input", "RenderInputRouter::RenderInputRouter");
  input_event_ack_timeout_.SetTaskRunner(task_runner_);
}

void RenderInputRouter::SetupInputRouter(float device_scale_factor) {
  TRACE_EVENT("input", "RenderInputRouter::SetupInputRouter");

  in_flight_event_count_ = 0;
  StopInputEventAckTimeout();

  bool was_active = input_router_ && input_router_->IsActive();

  input_router_ = std::make_unique<InputRouterImpl>(
      this, this, fling_scheduler_.get(),
      GetInputRouterConfigForPlatform(task_runner_));

  // Restore states in the newly recreated `input_router_`.
  input_router_->SetForceEnableZoom(force_enable_zoom_);
  input_router_->SetDeviceScaleFactor(device_scale_factor);
  if (was_active) {
    input_router_->MakeActive();
  }
}

void RenderInputRouter::SetFlingScheduler(
    std::unique_ptr<FlingSchedulerBase> fling_scheduler) {
  fling_scheduler_ = std::move(fling_scheduler);
}

void RenderInputRouter::BindRenderInputRouterInterfaces(
    mojo::PendingRemote<blink::mojom::RenderInputRouterClient> remote) {
  client_remote_.reset();

  client_remote_.Bind(std::move(remote), task_runner_);
}

void RenderInputRouter::RendererWidgetCreated(bool for_frame_widget,
                                              bool is_in_viz) {
  TRACE_EVENT("input", "RenderInputRouter::RendererWidgetCreated");

  if (is_in_viz) {
    client_remote_->GetWidgetInputHandlerForInputOnViz(
        widget_input_handler_.BindNewPipeAndPassReceiver(task_runner_));
  } else {
    client_remote_->GetWidgetInputHandler(
        widget_input_handler_.BindNewPipeAndPassReceiver(task_runner_),
        input_router_->BindNewHost(task_runner_));
  }

  if (for_frame_widget) {
    // `for_frame_widget` is always true for RenderInputRouters created on Viz,
    // but Viz side RIR do no need to establish FrameWidgetInputHandler
    // connection.
    if (!is_in_viz) {
      widget_input_handler_->GetFrameWidgetInputHandler(
          frame_widget_input_handler_.BindNewEndpointAndPassReceiver(
              task_runner_));
    }
    client_remote_->BindInputTargetClient(
        input_target_client_.BindNewPipeAndPassReceiver(task_runner_));
  }
}

void RenderInputRouter::SetForceEnableZoom(bool enabled) {
  force_enable_zoom_ = enabled;
  input_router_->SetForceEnableZoom(enabled);
}

void RenderInputRouter::SetDeviceScaleFactor(float device_scale_factor) {
  input_router_->SetDeviceScaleFactor(device_scale_factor);
}

void RenderInputRouter::ProgressFlingIfNeeded(base::TimeTicks current_time) {
  TRACE_EVENT("input", "RenderInputRouter::ProgressFlingIfNeeded");
  CHECK(fling_scheduler_);
  fling_scheduler_->ProgressFlingOnBeginFrameIfneeded(current_time);
}

void RenderInputRouter::StopFling() {
  input_router()->StopFling();
}

bool RenderInputRouter::IsAnyScrollGestureInProgress() const {
  for (size_t i = 0; i < is_in_gesture_scroll_.size(); i++) {
    if (is_in_gesture_scroll_[i]) {
      return true;
    }
  }
  return false;
}

blink::mojom::WidgetInputHandler* RenderInputRouter::GetWidgetInputHandler() {
  TRACE_EVENT("input", "RenderInputRouter::GetWidgetInputHandler");

  if (widget_input_handler_) {
    return widget_input_handler_.get();
  }
  // TODO(dtapuska): Remove the need for the unbound interface. It is
  // possible that a RVHI may make calls to a WidgetInputHandler when
  // the main frame is remote. This is because of ordering issues during
  // widget shutdown, so we present an UnboundWidgetInputHandler had
  // DLOGS the message calls.
  return g_unbound_input_handler.Pointer();
}

void RenderInputRouter::OnImeCompositionRangeChanged(
    const gfx::Range& range,
    const std::optional<std::vector<gfx::Rect>>& character_bounds) {
  render_input_router_client_->OnImeCompositionRangeChanged(range,
                                                            character_bounds);
}
void RenderInputRouter::OnImeCancelComposition() {
  render_input_router_client_->OnImeCancelComposition();
}

StylusInterface* RenderInputRouter::GetStylusInterface() {
  return delegate_->GetStylusInterface();
}

void RenderInputRouter::OnStartStylusWriting() {
  render_input_router_client_->OnStartStylusWriting();
}

bool RenderInputRouter::IsWheelScrollInProgress() {
  return is_in_gesture_scroll_[static_cast<int>(
      blink::WebGestureDevice::kTouchpad)];
}

bool RenderInputRouter::IsAutoscrollInProgress() {
  return render_input_router_client_->IsAutoscrollInProgress();
}

void RenderInputRouter::SetMouseCapture(bool capture) {
  render_input_router_client_->SetMouseCapture(capture);
}

void RenderInputRouter::SetAutoscrollSelectionActiveInMainFrame(
    bool autoscroll_selection) {
  render_input_router_client_->SetAutoscrollSelectionActiveInMainFrame(
      autoscroll_selection);
}

void RenderInputRouter::RequestMouseLock(
    bool from_user_gesture,
    bool unadjusted_movement,
    InputRouterImpl::RequestMouseLockCallback response) {
  render_input_router_client_->RequestMouseLock(
      from_user_gesture, unadjusted_movement, std::move(response));
}

gfx::Size RenderInputRouter::GetRootWidgetViewportSize() {
  if (!view_input_) {
    return gfx::Size();
  }

  // if |view_| is RWHVCF and |frame_connector_| is destroyed, then call to
  // GetRootView will return null pointer.
  auto* root_view = view_input_->GetRootView();
  if (!root_view) {
    return gfx::Size();
  }

  return root_view->GetVisibleViewportSize();
}

blink::mojom::InputEventResultState RenderInputRouter::FilterInputEvent(
    const blink::WebInputEvent& event,
    const ui::LatencyInfo& latency_info) {
  // Don't ignore touch cancel events, since they may be sent while input
  // events are being ignored in order to keep the renderer from getting
  // confused about how many touches are active.
  if ((is_blocked_ || delegate_->IsIgnoringWebInputEvents(event)) &&
      event.GetType() != WebInputEvent::Type::kTouchCancel) {
    delegate_->OnInputIgnored(event);
    return blink::mojom::InputEventResultState::kNoConsumerExists;
  }

  if (!delegate_->IsInitializedAndNotDead()) {
    return blink::mojom::InputEventResultState::kUnknown;
  }

  delegate_->OnInputEventPreDispatch(event);

  return view_input_ ? view_input_->FilterInputEvent(event)
                     : blink::mojom::InputEventResultState::kNotConsumed;
}

void RenderInputRouter::StartInputEventAckTimeout() {
  if (should_disable_hang_monitor_) {
    return;
  }

  if (!input_event_ack_timeout_.IsRunning()) {
    input_event_ack_timeout_.Start(
        FROM_HERE, hung_renderer_delay_,
        base::BindOnce(&RenderInputRouter::OnInputEventAckTimeout,
                       weak_factory_.GetWeakPtr()));
  }
}

void RenderInputRouter::StopInputEventAckTimeout() {
  input_event_ack_timeout_.Stop();
  delegate_->RendererIsResponsive();
}

void RenderInputRouter::RestartInputEventAckTimeoutIfNecessary() {
  if (!is_blocked_ && !should_disable_hang_monitor_ &&
      in_flight_event_count_ > 0) {
    input_event_ack_timeout_.Start(
        FROM_HERE, hung_renderer_delay_,
        base::BindOnce(&RenderInputRouter::OnInputEventAckTimeout,
                       weak_factory_.GetWeakPtr()));
  }
}

void RenderInputRouter::OnInputEventAckTimeout() {
  delegate_->OnInputEventAckTimeout(
      /* ack_timeout_ts= */ base::TimeTicks::Now());
  // Do not add code after this since the Delegate may delete this
  // RenderInputRouter in RendererUnresponsive.
}

void RenderInputRouter::IncrementInFlightEventCount() {
  ++in_flight_event_count_;

  if (!delegate_->IsHidden()) {
    StartInputEventAckTimeout();
  }
}

void RenderInputRouter::DecrementInFlightEventCount(
    blink::mojom::InputEventResultSource ack_source) {
  --in_flight_event_count_;
  if (in_flight_event_count_ <= 0) {
    // Cancel pending hung renderer checks since the renderer is
    // responsive.
    StopInputEventAckTimeout();
  } else {
    // Only restart the hang monitor timer if we got a response from the
    // main thread.
    if (ack_source == blink::mojom::InputEventResultSource::kMainThread) {
      RestartInputEventAckTimeoutIfNecessary();
    }
  }
}

void RenderInputRouter::OnInputDispatchedToRendererResult(
    const blink::WebInputEvent& event,
    DispatchToRendererResult result) {
  delegate_->NotifyObserversOfInputEvent(
      event, result == DispatchToRendererResult::kDispatched);
}

void RenderInputRouter::DidOverscroll(
    blink::mojom::DidOverscrollParamsPtr params) {
  delegate_->DidOverscroll(std::move(params));
}

void RenderInputRouter::DidStartScrollingViewport() {
  set_is_currently_scrolling_viewport(true);
}

void RenderInputRouter::OnInvalidInputEventSource() {
  delegate_->OnInvalidInputEventSource();
}

void RenderInputRouter::ForwardGestureEvent(
    const blink::WebGestureEvent& gesture_event) {
  TRACE_EVENT("input", "RenderInputRouter::ForwardGestureEvent", "type",
              WebInputEvent::GetName(gesture_event.GetType()));

  ForwardGestureEventWithLatencyInfo(gesture_event, ui::LatencyInfo());
}

void RenderInputRouter::ForwardGestureEventWithLatencyInfo(
    const blink::WebGestureEvent& gesture_event,
    const ui::LatencyInfo& latency_info) {
  TRACE_EVENT1("input", "RenderInputRouter::ForwardGestureEvent", "type",
               WebInputEvent::GetName(gesture_event.GetType()));

  input::GestureEventWithLatencyInfo gesture_with_latency(gesture_event,
                                                          latency_info);

  // Assigns a `trace_id` to the latency object.
  latency_tracker_->OnEventStart(&gesture_with_latency.latency);

  int64_t trace_id = gesture_with_latency.latency.trace_id();
  TRACE_EVENT(
      "input,benchmark,latencyInfo", "LatencyInfo.Flow",
      [&](perfetto::EventContext ctx) {
        ui::LatencyInfo::FillTraceEvent(
            ctx, trace_id, ChromeLatencyInfo2::Step::STEP_SEND_INPUT_EVENT_UI,
            InputEventTypeToProto(gesture_with_latency.event.GetType()));
      });

  // Early out if necessary, prior to performing latency logic.
  if (is_blocked_ || delegate_->IsIgnoringWebInputEvents(gesture_event)) {
    // IgnoreWebInputEvents is primarily concerned with suppressing event
    // dispatch to the renderer. However, the embedder may be filtering gesture
    // events to drive its own UI so we still give it an opportunity to see
    // these events.
    if (view_input_) {
      view_input_->FilterInputEvent(gesture_event);
    }
    return;
  }

  // The gesture events must have a known source.
  CHECK_NE(gesture_event.SourceDevice(),
           blink::WebGestureDevice::kUninitialized);

  if (gesture_event.GetType() == WebInputEvent::Type::kGestureScrollBegin) {
    scroll_peak_gpu_mem_tracker_ = delegate_->MakePeakGpuMemoryTracker(
        viz::PeakGpuMemoryTracker::Usage::SCROLL);
  } else if (gesture_event.GetType() ==
             WebInputEvent::Type::kGestureScrollEnd) {
    if (scroll_peak_gpu_mem_tracker_ && !is_currently_scrolling_viewport()) {
      // We start tracking peak gpu-memory usage when the initial scroll-begin
      // is dispatched. However, it is possible that the scroll-begin did not
      // trigger any scrolls (e.g. the page is not scrollable). In such cases,
      // we do not want to report the peak-memory usage metric. So it is
      // canceled here.
      scroll_peak_gpu_mem_tracker_->Cancel();
    }

    set_is_currently_scrolling_viewport(false);

    scroll_peak_gpu_mem_tracker_ = nullptr;
  }

  // Delegate must be non-null, due to `IsIgnoringWebInputEvents()` test.
  if (delegate_->PreHandleGestureEvent(gesture_event)) {
    return;
  }

  DispatchInputEventWithLatencyInfo(
      gesture_with_latency.event, &gesture_with_latency.latency,
      &gesture_with_latency.event.GetModifiableEventLatencyMetadata());
  {
    ScopedDispatchToRendererCallback dispatch_callback(
        GetDispatchToRendererCallback());
    SendGestureEventWithLatencyInfo(gesture_with_latency,
                                    dispatch_callback.callback);
  }
}

void RenderInputRouter::ForwardWheelEventWithLatencyInfo(
    const blink::WebMouseWheelEvent& wheel_event,
    const ui::LatencyInfo& latency_info) {
  render_input_router_client_->ForwardWheelEventWithLatencyInfo(wheel_event,
                                                                latency_info);
}

DispatchToRendererCallback RenderInputRouter::GetDispatchToRendererCallback() {
  return base::BindOnce(&RenderInputRouter::OnInputDispatchedToRendererResult,
                        base::Unretained(this));
}

void RenderInputRouter::OnWheelEventAck(
    const input::MouseWheelEventWithLatencyInfo& wheel_event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  latency_tracker_->OnInputEventAck(wheel_event.event, &wheel_event.latency,
                                    ack_result);
  delegate_->NotifyObserversOfInputEventAcks(ack_source, ack_result,
                                             wheel_event.event);

  delegate_->OnWheelEventAck(wheel_event, ack_source, ack_result);
}

void RenderInputRouter::OnTouchEventAck(
    const input::TouchEventWithLatencyInfo& event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  latency_tracker_->OnInputEventAck(event.event, &event.latency, ack_result);
  delegate_->NotifyObserversOfInputEventAcks(ack_source, ack_result,
                                             event.event);

  auto* input_event_router = delegate_->GetInputEventRouter();

  // At present interstitial pages might not have an input event router, so we
  // just have the view process the ack directly in that case; the view is
  // guaranteed to be a top-level view with an appropriate implementation of
  // ProcessAckedTouchEvent().
  if (input_event_router) {
    input_event_router->ProcessAckedTouchEvent(event, ack_result,
                                               view_input_.get());
  } else if (view_input_) {
    // Check if |view_input_| is a root view.
    CHECK(!view_input_->GetParentViewInput());
    view_input_->ProcessAckedTouchEvent(event, ack_result);
  }
}

void RenderInputRouter::OnGestureEventAck(
    const input::GestureEventWithLatencyInfo& event,
    blink::mojom::InputEventResultSource ack_source,
    blink::mojom::InputEventResultState ack_result) {
  TRACE_EVENT1("input", "RenderInputRouter::OnGestureEventAck", "type",
               blink::WebInputEvent::GetName(event.event.GetType()));
  latency_tracker_->OnInputEventAck(event.event, &event.latency, ack_result);
  delegate_->NotifyObserversOfInputEventAcks(ack_source, ack_result,
                                             event.event);

  // If the TouchEmulator didn't exist when this GestureEvent was sent, we
  // shouldn't create it here.
  if (auto* touch_emulator =
          delegate_->GetTouchEmulator(/*create_if_necessary=*/false)) {
    touch_emulator->OnGestureEventAck(event.event, view_input_.get());
  }

  if (view_input_) {
    view_input_->GestureEventAck(event.event, ack_source, ack_result);
  }
}

void RenderInputRouter::DispatchInputEventWithLatencyInfo(
    const WebInputEvent& event,
    ui::LatencyInfo* latency,
    ui::EventLatencyMetadata* event_latency_metadata) {
  latency_tracker_->OnInputEvent(event, latency, event_latency_metadata);
}

void RenderInputRouter::ForwardTouchEventWithLatencyInfo(
    const blink::WebTouchEvent& touch_event,
    const ui::LatencyInfo& latency) {
  TRACE_EVENT0("input,input.scrolling", "RenderInputRouter::ForwardTouchEvent");

  // Always forward TouchEvents for touch stream consistency. They will be
  // ignored if appropriate in FilterInputEvent().

  input::TouchEventWithLatencyInfo touch_with_latency(touch_event, latency);

  // Assigns a `trace_id` to the latency object.
  latency_tracker_->OnEventStart(&touch_with_latency.latency);

  int64_t trace_id = touch_with_latency.latency.trace_id();
  TRACE_EVENT("input,benchmark,latencyInfo", "LatencyInfo.Flow",
              [&](perfetto::EventContext ctx) {
                ui::LatencyInfo::FillTraceEvent(
                    ctx, trace_id,
                    ChromeLatencyInfo2::Step::STEP_SEND_INPUT_EVENT_UI,
                    InputEventTypeToProto(touch_with_latency.event.GetType()));
              });

  DispatchInputEventWithLatencyInfo(
      touch_with_latency.event, &touch_with_latency.latency,
      &touch_with_latency.event.GetModifiableEventLatencyMetadata());

  {
    ScopedDispatchToRendererCallback dispatch_callback(
        GetDispatchToRendererCallback());
    input_router_->SendTouchEvent(touch_with_latency,
                                  dispatch_callback.callback);
  }
}

std::unique_ptr<RenderInputRouterIterator>
RenderInputRouter::GetEmbeddedRenderInputRouters() {
  return delegate_->GetEmbeddedRenderInputRouters();
}

void RenderInputRouter::ShowContextMenuAtPoint(
    const gfx::Point& point,
    const ui::mojom::MenuSourceType source_type) {
  if (client_remote_) {
    client_remote_->ShowContextMenu(source_type, point);
  }
}

void RenderInputRouter::SendGestureEventWithLatencyInfo(
    const GestureEventWithLatencyInfo& gesture_with_latency,
    DispatchToRendererCallback& dispatch_callback) {
  const blink::WebGestureEvent& gesture_event = gesture_with_latency.event;
  if (gesture_event.GetType() == WebInputEvent::Type::kGestureScrollBegin) {
    DCHECK(
        !is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())]);
    is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())] =
        true;
  } else if (gesture_event.GetType() ==
             WebInputEvent::Type::kGestureScrollEnd) {
    DCHECK(
        is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())]);
    is_in_gesture_scroll_[static_cast<int>(gesture_event.SourceDevice())] =
        false;
    is_in_touchpad_gesture_fling_ = false;
  } else if (gesture_event.GetType() ==
             WebInputEvent::Type::kGestureFlingStart) {
    if (gesture_event.SourceDevice() == blink::WebGestureDevice::kTouchpad) {
      // a GSB event is generated from the first wheel event in a sequence after
      // the event is acked as not consumed by the renderer. Sometimes when the
      // main thread is busy/slow (e.g ChromeOS debug builds) a GFS arrives
      // before the first wheel is acked. In these cases no GSB will arrive
      // before the GFS. With browser side fling the out of order GFS arrival
      // does not need a DCHECK since the fling controller will process the GFS
      // and start queuing wheel events which will follow the one currently
      // awaiting ACK and the renderer receives the events in order.

      is_in_touchpad_gesture_fling_ = true;
    } else {
      DCHECK(is_in_gesture_scroll_[static_cast<int>(
          gesture_event.SourceDevice())]);

      // The FlingController handles GFS with touchscreen source and sends GSU
      // events with inertial state to the renderer to progress the fling.
      // is_in_gesture_scroll must stay true till the fling progress is
      // finished. Then the FlingController will generate and send a GSE which
      // shows the end of a scroll sequence and resets is_in_gesture_scroll_.
    }
  }
  input_router()->SendGestureEvent(gesture_with_latency, dispatch_callback);
}

void RenderInputRouter::DidStopFlinging() {
  is_in_touchpad_gesture_fling_ = false;
  if (view_input_) {
    view_input_->DidStopFlinging();
  }
}

blink::mojom::FrameWidgetInputHandler*
RenderInputRouter::GetFrameWidgetInputHandler() {
  if (!frame_widget_input_handler_) {
    return nullptr;
  }
  return frame_widget_input_handler_.get();
}

void RenderInputRouter::SetView(RenderWidgetHostViewInput* view) {
  if (!view) {
    view_input_.reset();
    return;
  }
  view_input_ = view->GetInputWeakPtr();
}

void RenderInputRouter::SetBeginFrameSourceForFlingScheduler(
    viz::BeginFrameSource* begin_frame_source) {
  CHECK(fling_scheduler_);
  fling_scheduler_->SetBeginFrameSource(begin_frame_source);
}

void RenderInputRouter::ResetFrameWidgetInputInterfaces() {
  frame_widget_input_handler_.reset();
  input_target_client_.reset();
}

void RenderInputRouter::ResetWidgetInputInterfaces() {
  widget_input_handler_.reset();
}

void RenderInputRouter::RenderProcessBlockedStateChanged(bool blocked) {
  // Early out if the blocked state hasn't actually changed.
  if (blocked == is_blocked_) {
    return;
  }

  is_blocked_ = blocked;
  is_blocked_ ? StopInputEventAckTimeout()
              : RestartInputEventAckTimeoutIfNecessary();
}

void RenderInputRouter::SetInputTargetClientForTesting(
    mojo::Remote<viz::mojom::InputTargetClient> input_target_client) {
  input_target_client_ = std::move(input_target_client);
}

void RenderInputRouter::SetWidgetInputHandlerForTesting(
    mojo::Remote<blink::mojom::WidgetInputHandler> widget_input_handler) {
  widget_input_handler_ = std::move(widget_input_handler);
}

}  // namespace input
