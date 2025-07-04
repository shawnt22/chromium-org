// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/offscreencanvas/offscreen_canvas.h"

#include <memory>
#include <utility>

#include "base/metrics/histogram_functions.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_metric_builder.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_metrics.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_study_settings.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/renderer/core/canvas_interventions/canvas_interventions_helper.h"
#include "third_party/blink/renderer/core/css/css_font_selector.h"
#include "third_party/blink/renderer/core/css/offscreen_font_selector.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/fileapi/blob.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_async_blob_creator.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_context_creation_attributes_core.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context_factory.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_resource_tracker.h"
#include "third_party/blink/renderer/core/html/canvas/image_data.h"
#include "third_party/blink/renderer/core/html/canvas/ukm_parameters.h"
#include "third_party/blink/renderer/core/html/canvas/unique_font_selector.h"
#include "third_party/blink/renderer/core/imagebitmap/image_bitmap.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/workers/dedicated_worker_global_scope.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/fonts/plain_text_painter.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_dispatcher.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/blink/renderer/platform/graphics/image.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image_transform.h"
#include "third_party/blink/renderer/platform/graphics/unaccelerated_static_bitmap_image.h"
#include "third_party/blink/renderer/platform/heap/thread_state.h"
#include "third_party/blink/renderer/platform/image-encoders/image_encoder_utils.h"
#include "third_party/blink/renderer/platform/instrumentation/histogram.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/wtf/math_extras.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace blink {

OffscreenCanvas::OffscreenCanvas(ExecutionContext* context, gfx::Size size)
    : CanvasRenderingContextHost(
          CanvasRenderingContextHost::HostType::kOffscreenCanvasHost,
          size),
      execution_context_(context) {
  // Other code in Blink watches for destruction of the context; be
  // robust here as well.
  if (!context->IsContextDestroyed()) {
    if (auto* window = DynamicTo<LocalDOMWindow>(context)) {
      // Snapshot the text direction. For a offscreen transferred from
      // an element this will be over-written by the value from the element.
      if (window->document()->documentElement()) {
        text_direction_ =
            window->document()->documentElement()->CachedDirectionality();
      }
      // If this OffscreenCanvas is being created in the context of a
      // cross-origin iframe, it should prefer to use the low-power GPU.
      LocalFrame* frame = window->GetFrame();
      if (!(frame && frame->IsCrossOriginToOutermostMainFrame())) {
        AllowHighPerformancePowerPreference();
      }
    } else if (context->IsDedicatedWorkerGlobalScope()) {
      // Per spec, dedicated workers can only load same-origin top-level
      // scripts, so grant them access to the high-performance GPU.
      //
      // TODO(crbug.com/1050739): refine this logic. If the worker was
      // spawned from an iframe, keep track of whether that iframe was
      // itself cross-origin.
      AllowHighPerformancePowerPreference();
    }
  }

  CanvasResourceTracker::For(context->GetIsolate())->Add(this, context);
  UpdateMemoryUsage();
}

OffscreenCanvas* OffscreenCanvas::Create(ScriptState* script_state,
                                         unsigned width,
                                         unsigned height) {
  UMA_HISTOGRAM_BOOLEAN("Blink.OffscreenCanvas.NewOffscreenCanvas", true);
  return MakeGarbageCollected<OffscreenCanvas>(
      ExecutionContext::From(script_state),
      gfx::Size(ClampTo<int>(width), ClampTo<int>(height)));
}

OffscreenCanvas::~OffscreenCanvas() {
  external_memory_accounter_.Decrease(v8::Isolate::GetCurrent(), memory_usage_);
}

void OffscreenCanvas::Commit(scoped_refptr<CanvasResource>&& canvas_resource,
                             const SkIRect& damage_rect) {
  if (!HasPlaceholderCanvas() || !canvas_resource)
    return;
  RecordCanvasSizeToUMA();

  current_frame_damage_rect_.join(damage_rect);
  GetOrCreateResourceDispatcher()->DispatchFrameSync(
      std::move(canvas_resource), current_frame_damage_rect_, IsOpaque());
  current_frame_damage_rect_ = SkIRect::MakeEmpty();
}

void OffscreenCanvas::Dispose() {
  // We need to drop frame dispatcher, to prevent mojo calls from completing.
  disposing_ = true;
  frame_dispatcher_ = nullptr;
  DiscardResources();

  if (context_) {
    context_->DetachHost();
    context_ = nullptr;
  }
}

void OffscreenCanvas::DeregisterFromAnimationFrameProvider() {
  if (HasPlaceholderCanvas() && GetTopExecutionContext() &&
      GetTopExecutionContext()->IsDedicatedWorkerGlobalScope()) {
    WorkerAnimationFrameProvider* animation_frame_provider =
        To<DedicatedWorkerGlobalScope>(GetTopExecutionContext())
            ->GetAnimationFrameProvider();
    if (animation_frame_provider)
      animation_frame_provider->DeregisterOffscreenCanvas(this);
  }
}

void OffscreenCanvas::SetPlaceholderCanvasId(DOMNodeId canvas_id) {
  placeholder_canvas_id_ = canvas_id;
  if (GetTopExecutionContext() &&
      GetTopExecutionContext()->IsDedicatedWorkerGlobalScope()) {
    WorkerAnimationFrameProvider* animation_frame_provider =
        To<DedicatedWorkerGlobalScope>(GetTopExecutionContext())
            ->GetAnimationFrameProvider();
    DCHECK(animation_frame_provider);
    if (animation_frame_provider)
      animation_frame_provider->RegisterOffscreenCanvas(this);
  }
  if (frame_dispatcher_) {
    frame_dispatcher_->SetPlaceholderCanvasDispatcher(placeholder_canvas_id_);
  }
}

void OffscreenCanvas::setWidth(unsigned width) {
  gfx::Size new_size = Size();
  new_size.set_width(ClampTo<int>(width));
  SetSize(new_size);
}

void OffscreenCanvas::setHeight(unsigned height) {
  gfx::Size new_size = Size();
  new_size.set_height(ClampTo<int>(height));
  SetSize(new_size);
}

void OffscreenCanvas::SetSize(gfx::Size size) {
  // Setting size of a canvas also resets it.
  if (size == Size()) {
    if (context_ && context_->IsRenderingContext2D()) {
      context_->Reset();
      origin_clean_ = true;
    }
    return;
  }

  CanvasResourceHost::SetSize(size);
  UpdateMemoryUsage();
  current_frame_damage_rect_ = SkIRect::MakeWH(Size().width(), Size().height());

  if (context_ && context_->isContextLost()) {
    context_->RestoreFromInvalidSizeIfNeeded();
  }

  if (frame_dispatcher_)
    frame_dispatcher_->Reshape(Size());
  if (context_) {
    if (context_->IsWebGL() || IsWebGPU()) {
      context_->Reshape(Size().width(), Size().height());
    } else if (context_->IsRenderingContext2D() ||
               context_->IsImageBitmapRenderingContext()) {
      context_->Reset();
      origin_clean_ = true;
    }
    context_->DidDraw(CanvasPerformanceMonitor::DrawType::kOther);
  }
}

void OffscreenCanvas::RecordTransfer() {
  UMA_HISTOGRAM_BOOLEAN("Blink.OffscreenCanvas.Transferred", true);
}

void OffscreenCanvas::SetNeutered() {
  DCHECK(!context_);
  is_neutered_ = true;
  SetSize(gfx::Size(0, 0));
  DeregisterFromAnimationFrameProvider();
}

ImageBitmap* OffscreenCanvas::transferToImageBitmap(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  if (is_neutered_) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Cannot transfer an ImageBitmap from a detached OffscreenCanvas");
    return nullptr;
  }
  if (!context_) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "Cannot transfer an ImageBitmap from an "
                                      "OffscreenCanvas with no context");
    return nullptr;
  }
  if (ContextHasOpenLayers(context_)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "`transferToImageBitmap()` cannot be called with open layers.");
    return nullptr;
  }

  ImageBitmap* image =
      context_->TransferToImageBitmap(script_state, exception_state);
  if (exception_state.HadException()) [[unlikely]] {
    return nullptr;
  }

  if (!image) {
    // Undocumented exception (not in spec).
    exception_state.ThrowDOMException(DOMExceptionCode::kUnknownError,
                                      "ImageBitmap construction failed");
  }

  if (plain_text_painter_ != nullptr) {
    plain_text_painter_->DidSwitchFrame();
  }
  if (unique_font_selector_) {
    unique_font_selector_->DidSwitchFrame();
  }
  return image;
}

void OffscreenCanvas::RecordIdentifiabilityMetric(
    const blink::IdentifiableSurface& surface,
    const IdentifiableToken& token) const {
  if (!IdentifiabilityStudySettings::Get()->ShouldSampleSurface(surface))
    return;
  blink::IdentifiabilityMetricBuilder(GetExecutionContext()->UkmSourceID())
      .Add(surface, token)
      .Record(GetExecutionContext()->UkmRecorder());
}

scoped_refptr<Image> OffscreenCanvas::GetSourceImageForCanvas(
    FlushReason reason,
    SourceImageStatus* status,
    const gfx::SizeF& size) {
  if (!context_) {
    *status = kInvalidSourceImageStatus;
    sk_sp<SkSurface> surface = SkSurfaces::Raster(
        SkImageInfo::MakeN32Premul(Size().width(), Size().height()));
    return surface ? UnacceleratedStaticBitmapImage::Create(
                         surface->makeImageSnapshot())
                   : nullptr;
  }
  if (ContextHasOpenLayers(context_)) {
    *status = kLayersOpenInCanvasSource;
    return nullptr;
  }
  if (!size.width() || !size.height()) {
    *status = kZeroSizeCanvasSourceImageStatus;
    return nullptr;
  }
  scoped_refptr<StaticBitmapImage> image = context_->GetImage(reason);
  if (!image) {
    image = CreateTransparentImage();
  }
  *status = image ? kNormalSourceImageStatus : kInvalidSourceImageStatus;
  return image;
}

ScriptPromise<ImageBitmap> OffscreenCanvas::CreateImageBitmap(
    ScriptState* script_state,
    std::optional<gfx::Rect> crop_rect,
    const ImageBitmapOptions* options,
    ExceptionState& exception_state) {
  if (ContextHasOpenLayers(context_)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "`createImageBitmap()` cannot be called with open layers.");
    return EmptyPromise();
  }
  if (context_) {
    context_->FinalizeFrame(FlushReason::kCreateImageBitmap);
  }
  return ImageBitmapSource::FulfillImageBitmap(
      script_state,
      IsPaintable()
          ? MakeGarbageCollected<ImageBitmap>(this, crop_rect, options)
          : nullptr,
      options, exception_state);
}

ScriptPromise<Blob> OffscreenCanvas::convertToBlob(
    ScriptState* script_state,
    const ImageEncodeOptions* options,
    ExceptionState& exception_state) {
  DCHECK(IsOffscreenCanvas());
  WTF::String object_name = "OffscreenCanvas";
  std::stringstream error_msg;

  if (is_neutered_) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "OffscreenCanvas object is detached.");
    return EmptyPromise();
  }

  if (ContextHasOpenLayers(context_)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "`convertToBlob()` cannot be called while layers are opened.");
    return EmptyPromise();
  }

  if (!OriginClean()) {
    error_msg << "Tainted " << object_name << " may not be exported.";
    exception_state.ThrowSecurityError(error_msg.str().c_str());
    return EmptyPromise();
  }

  // It's possible that there are recorded commands that have not been resolved
  // Finalize frame will be called in GetImage, but if there's no
  // resourceProvider yet then the IsPaintable check will fail
  if (context_) {
    context_->FinalizeFrame(FlushReason::kToBlob);
  }

  if (!IsPaintable() || Size().IsEmpty()) {
    error_msg << "The size of " << object_name << " is zero.";
    exception_state.ThrowDOMException(DOMExceptionCode::kIndexSizeError,
                                      error_msg.str().c_str());
    return EmptyPromise();
  }

  if (!context_) {
    error_msg << object_name << " has no rendering context.";
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      error_msg.str().c_str());
    return EmptyPromise();
  }

  base::TimeTicks start_time = base::TimeTicks::Now();
  scoped_refptr<StaticBitmapImage> image_bitmap =
      context_->GetImage(FlushReason::kToBlob);
  if (image_bitmap) {
    auto intervention_type =
        CanvasInterventionsHelper::CanvasInterventionType::kNone;
    if (CanvasInterventionsHelper::MaybeNoiseSnapshot(
            context_, GetExecutionContext(), image_bitmap)) {
      intervention_type =
          CanvasInterventionsHelper::CanvasInterventionType::kNoise;
    };

    auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<Blob>>(
        script_state, exception_state.GetContext());
    CanvasAsyncBlobCreator::ToBlobFunctionType function_type =
        CanvasAsyncBlobCreator::kOffscreenCanvasConvertToBlobPromise;
    auto* execution_context = ExecutionContext::From(script_state);
    auto* async_creator = MakeGarbageCollected<CanvasAsyncBlobCreator>(
        image_bitmap, options, function_type, start_time, execution_context,
        IdentifiabilityStudySettings::Get()->ShouldSampleType(
            IdentifiableSurface::Type::kCanvasReadback)
            ? IdentifiabilityInputDigest(context_)
            : 0,
        intervention_type, resolver);
    async_creator->ScheduleAsyncBlobCreation(options->quality());
    return resolver->Promise();
  }
  exception_state.ThrowDOMException(DOMExceptionCode::kNotReadableError,
                                    "Readback of the source image has failed.");
  return EmptyPromise();
}

bool OffscreenCanvas::IsOpaque() const {
  return context_ && !context_->CreationAttributes().alpha;
}

CanvasRenderingContext* OffscreenCanvas::GetCanvasRenderingContext(
    ExecutionContext* execution_context,
    CanvasRenderingContext::CanvasRenderingAPI rendering_api,
    const CanvasContextCreationAttributesCore& attributes) {
  DCHECK_EQ(execution_context, GetTopExecutionContext());

  if (execution_context->IsContextDestroyed())
    return nullptr;

  // Unknown type.
  if (rendering_api == CanvasRenderingContext::CanvasRenderingAPI::kUnknown)
    return nullptr;

  if (auto* window = DynamicTo<LocalDOMWindow>(GetExecutionContext())) {
    if (attributes.color_space != PredefinedColorSpace::kSRGB)
      UseCounter::Count(window->document(), WebFeature::kCanvasUseColorSpace);
  }

  CanvasRenderingContextFactory* factory =
      GetRenderingContextFactory(static_cast<int>(rendering_api));
  if (!factory)
    return nullptr;

  if (context_) {
    if (context_->GetRenderingAPI() != rendering_api) {
      factory->OnError(
          this, "OffscreenCanvas has an existing context of a different type");
      return nullptr;
    }
  } else {
    // Tell the debugger about the attempt to create an offscreen
    // canvas context even if it will fail, to ease debugging.
    probe::DidCreateOffscreenCanvasContext(this);

    CanvasContextCreationAttributesCore recomputed_attributes = attributes;
    if (!allow_high_performance_power_preference_) {
      recomputed_attributes.power_preference =
          CanvasContextCreationAttributesCore::PowerPreference::kLowPower;
    }

    context_ = factory->Create(this, recomputed_attributes);
    if (context_) {
      context_->RecordUKMCanvasRenderingAPI();
      context_->RecordUMACanvasRenderingAPI();
    }
  }

  return context_.Get();
}

OffscreenCanvas::ContextFactoryVector&
OffscreenCanvas::RenderingContextFactories() {
  DEFINE_STATIC_LOCAL(
      ContextFactoryVector, context_factories,
      (static_cast<int>(CanvasRenderingContext::CanvasRenderingAPI::kMaxValue) +
       1));
  return context_factories;
}

CanvasRenderingContextFactory* OffscreenCanvas::GetRenderingContextFactory(
    int type) {
  DCHECK_LE(type, static_cast<int>(
                      CanvasRenderingContext::CanvasRenderingAPI::kMaxValue));
  return RenderingContextFactories()[type].get();
}

void OffscreenCanvas::RegisterRenderingContextFactory(
    std::unique_ptr<CanvasRenderingContextFactory> rendering_context_factory) {
  CanvasRenderingContext::CanvasRenderingAPI rendering_api =
      rendering_context_factory->GetRenderingAPI();
  DCHECK_LE(rendering_api,
            CanvasRenderingContext::CanvasRenderingAPI::kMaxValue);
  DCHECK(!RenderingContextFactories()[static_cast<int>(rendering_api)]);
  RenderingContextFactories()[static_cast<int>(rendering_api)] =
      std::move(rendering_context_factory);
}

bool OffscreenCanvas::OriginClean() const {
  return origin_clean_ && !disable_reading_from_canvas_;
}

bool OffscreenCanvas::EnableAccelerationForCanvas2D() {
  CHECK(IsRenderingContext2D());

  // Unlike HTML canvases, offscreen canvases don't automatically shift between
  // CPU and GPU. Instead, we just return true if the canvas exists on GPU, or
  // false if the canvas is CPU-bound. If the canvas' resource provider doesn't
  // exist yet, we create it here.
  // Note that `OffscreenCanvas::IsAccelerated` above is not equivalent! This
  // returns false if the canvas resource provider doesn't exist yet, even if it
  // will be an accelerated canvas once it has been created.
  CanvasResourceProvider* provider = GetOrCreateResourceProviderForCanvas2D();
  if (!provider) {
    return false;
  }
  return provider->IsAccelerated();
}

bool OffscreenCanvas::HasPlaceholderCanvas() const {
  return placeholder_canvas_id_ != kInvalidDOMNodeId;
}

CanvasResourceDispatcher* OffscreenCanvas::GetOrCreateResourceDispatcher() {
  DCHECK(HasPlaceholderCanvas());
  // If we don't have a valid placeholder_canvas_id_, then this is a standalone
  // OffscreenCanvas, and it should not have a placeholder.
  if (frame_dispatcher_ == nullptr) {
    scoped_refptr<base::SingleThreadTaskRunner>
        agent_group_scheduler_compositor_task_runner;
    scoped_refptr<base::SingleThreadTaskRunner> dispatcher_task_runner;
    if (auto* top_execution_context = GetTopExecutionContext()) {
      agent_group_scheduler_compositor_task_runner =
          top_execution_context->GetAgentGroupSchedulerCompositorTaskRunner();

      dispatcher_task_runner =
          top_execution_context->GetTaskRunner(TaskType::kInternalDefault);
    }

    // The frame dispatcher connects the current thread of OffscreenCanvas
    // (either main or worker) to the GPU process and will have to be recreated
    // if the GPU channel is lost.
    frame_dispatcher_ = std::make_unique<CanvasResourceDispatcher>(
        this, std::move(dispatcher_task_runner),
        std::move(agent_group_scheduler_compositor_task_runner), client_id_,
        sink_id_, placeholder_canvas_id_, Size());

    if (HasPlaceholderCanvas())
      frame_dispatcher_->SetPlaceholderCanvasDispatcher(placeholder_canvas_id_);
  }
  return frame_dispatcher_.get();
}

CanvasResourceProvider*
OffscreenCanvas::GetOrCreateResourceProviderForCanvas2D() {
  CHECK(IsRenderingContext2D());
  if (!context_ ||
      (context_->isContextLost() && !context_->IsContextBeingRestored())) {
    return nullptr;
  }

  if (CanvasResourceProvider* provider = GetResourceProviderForCanvas2D()) {
    if (!provider->IsValid()) {
      // The canvas context is not lost but the provider is invalid. This
      // happens if the GPU process dies in the middle of a render task. The
      // canvas is notified of GPU context losses via the `NotifyGpuContextLost`
      // callback and restoration happens in `TryRestoreContextEvent`. Both
      // callbacks are executed in their own separate task. If the GPU context
      // goes invalid in the middle of a render task, the canvas won't
      // immediately know about it and canvas APIs will continue using the
      // provider that is now invalid. We can early return here, trying to
      // re-create the provider right away would just fail. We need to let
      // `TryRestoreContextEvent` wait for the GPU process to up again.
      return nullptr;
    }
    return provider;
  }

  if (!IsValidImageSize() && !Size().IsEmpty()) {
    context_->LoseContext(CanvasRenderingContext::kInvalidCanvasSize);
    return nullptr;
  }

  std::unique_ptr<CanvasResourceProvider> provider;
  gfx::Size surface_size(width(), height());
  const bool can_use_gpu =
      SharedGpuContext::IsGpuCompositingEnabled() &&
      RuntimeEnabledFeatures::Accelerated2dCanvasEnabled() &&
      !(context_->CreationAttributes().will_read_frequently ==
        CanvasContextCreationAttributesCore::WillReadFrequently::kTrue);
  const bool use_shared_image =
      can_use_gpu ||
      (HasPlaceholderCanvas() && SharedGpuContext::IsGpuCompositingEnabled());
  const bool use_scanout =
      use_shared_image && HasPlaceholderCanvas() &&
      SharedGpuContext::MaySupportImageChromium() &&
      RuntimeEnabledFeatures::Canvas2dImageChromiumEnabled();

  gpu::SharedImageUsageSet shared_image_usage_flags =
      gpu::SHARED_IMAGE_USAGE_DISPLAY_READ;
  if (use_scanout) {
    shared_image_usage_flags |= gpu::SHARED_IMAGE_USAGE_SCANOUT;
  }

  const SkAlphaType alpha_type = GetRenderingContextAlphaType();
  const viz::SharedImageFormat format = GetRenderingContextFormat();
  const gfx::ColorSpace color_space = GetRenderingContextColorSpace();
  if (use_shared_image) {
    provider = CanvasResourceProvider::CreateSharedImageProvider(
        Size(), format, alpha_type, color_space,
        CanvasResourceProvider::ShouldInitialize::kCallClear,
        SharedGpuContext::ContextProviderWrapper(),
        can_use_gpu ? RasterMode::kGPU : RasterMode::kCPU,
        shared_image_usage_flags, this);
  } else if (HasPlaceholderCanvas()) {
    // using the software compositor
    base::WeakPtr<CanvasResourceDispatcher> dispatcher_weakptr =
        GetOrCreateResourceDispatcher()->GetWeakPtr();
    provider =
        CanvasResourceProvider::CreateSharedImageProviderForSoftwareCompositor(
            Size(), format, alpha_type, color_space,
            CanvasResourceProvider::ShouldInitialize::kCallClear,
            SharedGpuContext::SharedImageInterfaceProvider(), this);
  }

  if (!provider) {
    // Last resort fallback is to use the bitmap provider. Using this
    // path is normal for software-rendered OffscreenCanvases that have no
    // placeholder canvas. If there is a placeholder, its content will not be
    // visible on screen, but at least readbacks will work. Failure to create
    // another type of resource prover above is a sign that the graphics
    // pipeline is in a bad state (e.g. gpu process crashed, out of memory)
    provider = CanvasResourceProvider::CreateBitmapProvider(
        Size(), format, alpha_type, color_space,
        CanvasResourceProvider::ShouldInitialize::kCallClear, this);
  }

  SetResourceProviderForCanvas2D(std::move(provider));

  if (GetResourceProviderForCanvas2D() &&
      GetResourceProviderForCanvas2D()->IsValid()) {
    // todo(crbug/1064363)  Add a separate UMA for Offscreen Canvas usage and
    // understand if the if (ResourceProvider() &&
    // ResourceProvider()->IsValid()) is really needed.
    base::UmaHistogramBoolean(
        "Blink.Canvas.ResourceProviderIsAccelerated",
        GetResourceProviderForCanvas2D()->IsAccelerated());
    base::UmaHistogramEnumeration("Blink.Canvas.ResourceProviderType",
                                  GetResourceProviderForCanvas2D()->GetType());
    DidDraw();
  }
  return GetResourceProviderForCanvas2D();
}

void OffscreenCanvas::DidDraw(const SkIRect& rect) {
  if (rect.isEmpty())
    return;

  if (HasPlaceholderCanvas()) {
    needs_push_frame_ = true;
    if (!inside_worker_raf_)
      GetOrCreateResourceDispatcher()->SetNeedsBeginFrame(true);
  }
}

bool OffscreenCanvas::BeginFrame() {
  DCHECK(HasPlaceholderCanvas());
  GetOrCreateResourceDispatcher()->SetNeedsBeginFrame(false);
  return PushFrameIfNeeded();
}

bool OffscreenCanvas::PushFrameIfNeeded() {
  if (needs_push_frame_ && context_) {
    return context_->PushFrame();
  }
  return false;
}

bool OffscreenCanvas::PushFrame(scoped_refptr<CanvasResource>&& canvas_resource,
                                const SkIRect& damage_rect) {
  TRACE_EVENT0("blink", "OffscreenCanvas::PushFrame");
  DCHECK(needs_push_frame_);
  needs_push_frame_ = false;
  current_frame_damage_rect_.join(damage_rect);
  if (current_frame_damage_rect_.isEmpty() || !canvas_resource)
    return false;
  GetOrCreateResourceDispatcher()->DispatchFrame(
      std::move(canvas_resource), current_frame_damage_rect_, IsOpaque());
  current_frame_damage_rect_ = SkIRect::MakeEmpty();

  if (plain_text_painter_ != nullptr) {
    plain_text_painter_->DidSwitchFrame();
  }
  if (unique_font_selector_) {
    unique_font_selector_->DidSwitchFrame();
  }
  return true;
}

bool OffscreenCanvas::ShouldAccelerate2dContext() const {
  base::WeakPtr<WebGraphicsContext3DProviderWrapper> context_provider_wrapper =
      SharedGpuContext::ContextProviderWrapper();
  return context_provider_wrapper &&
         context_provider_wrapper->Utils()->Accelerated2DCanvasFeatureEnabled();
}

UkmParameters OffscreenCanvas::GetUkmParameters() {
  auto* context = GetExecutionContext();
  return {context->UkmRecorder(), context->UkmSourceID()};
}

void OffscreenCanvas::NotifyGpuContextLost() {
  if (context_ && !context_->isContextLost()) {
    // This code path is used only by 2D canvas, because NotifyGpuContextLost
    // is called by Canvas2DLayerBridge and OffscreenCanvas itself, rather
    // than the rendering context.
    DCHECK(context_->IsRenderingContext2D());
    context_->LoseContext(CanvasRenderingContext::kRealLostContext);
  }
  if (context_->IsWebGL() && frame_dispatcher_ != nullptr) {
    // We'll need to recreate a new frame dispatcher once the context is
    // restored in order to reestablish the compositor frame sink mojo
    // channel.
    frame_dispatcher_ = nullptr;
  }
}

TextDirection OffscreenCanvas::GetTextDirection(const ComputedStyle*) {
  return text_direction_.value_or(TextDirection::kLtr);
}

void OffscreenCanvas::SetLocale(scoped_refptr<const LayoutLocale> locale) {
  locale_ = std::move(locale);
}

const LayoutLocale* OffscreenCanvas::GetLocale() const {
  if (locale_) {
    return locale_.get();
  }
  if (const auto* window = DynamicTo<LocalDOMWindow>(GetExecutionContext())) {
    const Element* document_element = window->document()->documentElement();
    if (document_element) {
      return &LayoutLocale::ValueOrDefault(
          LayoutLocale::Get(document_element->ComputeInheritedLanguage()));
    }
  }
  return &LayoutLocale::GetDefault();
}

UniqueFontSelector* OffscreenCanvas::GetFontSelector() {
  if (UniqueFontSelector* unique_font_selector = unique_font_selector_) {
    return unique_font_selector;
  }
  FontSelector* base_selector = nullptr;
  if (auto* window = DynamicTo<LocalDOMWindow>(GetExecutionContext())) {
    base_selector = window->document()->GetStyleEngine().GetFontSelector();
  } else {
    // TODO(crbug.com/40059901): Temporary mitigation.  Remove the following
    // CHECK once a more comprehensive solution has been implemented.
    CHECK(GetExecutionContext()->IsWorkerGlobalScope());
    base_selector =
        To<WorkerGlobalScope>(GetExecutionContext())->GetFontSelector();
  }
  auto* unique_font_selector = MakeGarbageCollected<UniqueFontSelector>(
      base_selector,
      RuntimeEnabledFeatures::CanvasTextNgEnabled(GetExecutionContext()));
  unique_font_selector_ = unique_font_selector;
  return unique_font_selector;
}

void OffscreenCanvas::UpdateMemoryUsage() {
  // NOTE: All formats used by canvas are either 8-bit or 16-bit.
  int bytes_per_pixel = GetRenderingContextFormat().BitsPerPixel() / 8;

  base::CheckedNumeric<int32_t> memory_usage_checked = bytes_per_pixel;
  memory_usage_checked *= Size().width();
  memory_usage_checked *= Size().height();
  int32_t new_memory_usage =
      memory_usage_checked.ValueOrDefault(std::numeric_limits<int32_t>::max());

  // TODO(junov): We assume that it is impossible to be inside a FastAPICall
  // from a host interface other than the rendering context.  This assumption
  // may need to be revisited in the future depending on how the usage of
  // [NoAllocDirectCall] evolves.
  intptr_t delta_bytes = new_memory_usage - memory_usage_;
  if (delta_bytes) {
    // Here we check "IsAllocationAllowed", but it is actually garbage
    // collection that is not allowed, and allocations can trigger GC.
    // AdjustAmountOfExternalAllocatedMemory is not an allocation but it
    // can trigger GC, So we use "IsAllocationAllowed" as a proxy for
    // "is GC allowed". When garbage collection is already in progress,
    // allocations are not allowed, but calling
    // AdjustAmountOfExternalAllocatedMemory is safe, hence the
    // 'diposing_' condition in the DCHECK below.
    DCHECK(ThreadState::Current()->IsAllocationAllowed() || disposing_);
    external_memory_accounter_.Update(v8::Isolate::GetCurrent(), delta_bytes);
    memory_usage_ = new_memory_usage;
  }
}

size_t OffscreenCanvas::GetMemoryUsage() const {
  return base::saturated_cast<size_t>(memory_usage_);
}

void OffscreenCanvas::Trace(Visitor* visitor) const {
  visitor->Trace(context_);
  visitor->Trace(execution_context_);
  CanvasRenderingContextHost::Trace(visitor);
  EventTarget::Trace(visitor);
  CanvasRenderingContextHost::Trace(visitor);
}

}  // namespace blink
