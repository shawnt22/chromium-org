// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/canvas/offscreencanvas2d/offscreen_canvas_rendering_context_2d.h"

#include "base/metrics/histogram_functions.h"
#include "base/trace_event/trace_event.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_canvas_font_stretch.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_canvas_text_rendering.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_offscreen_rendering_context.h"
#include "third_party/blink/renderer/core/css/offscreen_font_selector.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/resolver/font_style_resolver.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/canvas/text_metrics.h"
#include "third_party/blink/renderer/core/html/canvas/unique_font_selector.h"
#include "third_party/blink/renderer/core/imagebitmap/image_bitmap.h"
#include "third_party/blink/renderer/core/workers/worker_global_scope.h"
#include "third_party/blink/renderer/core/workers/worker_settings.h"
#include "third_party/blink/renderer/modules/canvas/htmlcanvas/canvas_context_creation_attributes_helpers.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/fonts/text_run_paint_info.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_canvas.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/linked_hash_set.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace {
const size_t kHardMaxCachedFonts = 250;
const size_t kMaxCachedFonts = 25;
// Max delay to fire context lost for context in iframes.
static const unsigned kMaxIframeContextLoseDelay = 100;

class OffscreenFontCache {
 public:
  void PruneLocalFontCache(size_t target_size) {
    while (font_lru_list_.size() > target_size) {
      fonts_resolved_.erase(font_lru_list_.back());
      font_lru_list_.pop_back();
    }
  }

  void AddFont(String name, blink::FontDescription& font) {
    fonts_resolved_.insert(name, font);
    auto add_result = font_lru_list_.PrependOrMoveToFirst(name);
    DCHECK(add_result.is_new_entry);
    PruneLocalFontCache(kHardMaxCachedFonts);
  }

  blink::FontDescription* GetFont(String name) {
    auto i = fonts_resolved_.find(name);
    if (i != fonts_resolved_.end()) {
      auto add_result = font_lru_list_.PrependOrMoveToFirst(name);
      DCHECK(!add_result.is_new_entry);
      return &(i->value);
    }
    return nullptr;
  }

 private:
  HashMap<String, blink::FontDescription> fonts_resolved_;
  LinkedHashSet<String> font_lru_list_;
};

OffscreenFontCache& GetOffscreenFontCache() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(ThreadSpecific<OffscreenFontCache>,
                                  thread_specific_pool, ());
  return *thread_specific_pool;
}

}  // namespace

namespace blink {

CanvasRenderingContext* OffscreenCanvasRenderingContext2D::Factory::Create(
    CanvasRenderingContextHost* host,
    const CanvasContextCreationAttributesCore& attrs) {
  DCHECK(host->IsOffscreenCanvas());
  CanvasRenderingContext* rendering_context =
      MakeGarbageCollected<OffscreenCanvasRenderingContext2D>(
          static_cast<OffscreenCanvas*>(host), attrs);
  DCHECK(rendering_context);
  return rendering_context;
}

OffscreenCanvasRenderingContext2D::~OffscreenCanvasRenderingContext2D() =
    default;

OffscreenCanvasRenderingContext2D::OffscreenCanvasRenderingContext2D(
    OffscreenCanvas* canvas,
    const CanvasContextCreationAttributesCore& attrs)
    : BaseRenderingContext2D(canvas,
                             attrs,
                             canvas->GetTopExecutionContext()->GetTaskRunner(
                                 TaskType::kInternalDefault)) {
  identifiability_study_helper_.SetExecutionContext(
      canvas->GetTopExecutionContext());
  is_valid_size_ = Host()->IsValidImageSize();

  ExecutionContext* execution_context = canvas->GetTopExecutionContext();
  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    if (window->GetFrame() && window->GetFrame()->GetSettings() &&
        window->GetFrame()->GetSettings()->GetDisableReadingFromCanvas())
      canvas->SetDisableReadingFromCanvasTrue();
    return;
  }
  dirty_rect_for_commit_.setEmpty();
  WorkerSettings* worker_settings =
      To<WorkerGlobalScope>(execution_context)->GetWorkerSettings();
  if (worker_settings && worker_settings->DisableReadingFromCanvas())
    canvas->SetDisableReadingFromCanvasTrue();
}

void OffscreenCanvasRenderingContext2D::Trace(Visitor* visitor) const {
  ScriptWrappable::Trace(visitor);
  BaseRenderingContext2D::Trace(visitor);
}

void OffscreenCanvasRenderingContext2D::FinalizeFrame(FlushReason reason) {
  TRACE_EVENT0("blink", "OffscreenCanvasRenderingContext2D::FinalizeFrame");

  // Make sure surface is ready for painting: fix the rendering mode now
  // because it will be too late during the paint invalidation phase.
  if (!GetOrCreateCanvasResourceProvider())
    return;
  Host()->FlushRecordingForCanvas2D(reason);
}

// BaseRenderingContext2D implementation
bool OffscreenCanvasRenderingContext2D::OriginClean() const {
  return Host()->OriginClean();
}

void OffscreenCanvasRenderingContext2D::SetOriginTainted() {
  Host()->SetOriginTainted();
}

int OffscreenCanvasRenderingContext2D::Width() const {
  return Host()->Size().width();
}

int OffscreenCanvasRenderingContext2D::Height() const {
  return Host()->Size().height();
}

bool OffscreenCanvasRenderingContext2D::CanCreateCanvas2dResourceProvider()
    const {
  const CanvasRenderingContextHost* const host = Host();
  if (host == nullptr || host->Size().IsEmpty()) [[unlikely]] {
    return false;
  }
  return !!GetOrCreateCanvasResourceProvider();
}

CanvasResourceProvider*
OffscreenCanvasRenderingContext2D::GetOrCreateCanvasResourceProvider() const {
  DCHECK(Host() && Host()->IsOffscreenCanvas());
  OffscreenCanvas* host = HostAsOffscreenCanvas();
  if (host == nullptr) [[unlikely]] {
    return nullptr;
  }
  return host->GetOrCreateResourceProviderForCanvas2D();
}

CanvasResourceProvider*
OffscreenCanvasRenderingContext2D::GetCanvasResourceProvider() const {
  return Host()->GetResourceProviderForCanvas2D();
}

void OffscreenCanvasRenderingContext2D::Reset() {
  Host()->DiscardResources();
  BaseRenderingContext2D::ResetInternal();
  // Because the host may have changed to a zero size
  is_valid_size_ = Host()->IsValidImageSize();
  // We must resize the damage rect to avoid a potentially larger damage than
  // actual canvas size. See: crbug.com/1227165
  dirty_rect_for_commit_ = SkIRect::MakeWH(Width(), Height());
}

scoped_refptr<CanvasResource>
OffscreenCanvasRenderingContext2D::ProduceCanvasResource(FlushReason reason) {
  CanvasResourceProvider* provider = GetOrCreateCanvasResourceProvider();
  if (!provider) {
    return nullptr;
  }
  scoped_refptr<CanvasResource> frame = provider->ProduceCanvasResource(reason);
  if (!frame)
    return nullptr;

  frame->SetOriginClean(OriginClean());
  return frame;
}

bool OffscreenCanvasRenderingContext2D::PushFrame() {
  if (dirty_rect_for_commit_.isEmpty())
    return false;

  SkIRect damage_rect(dirty_rect_for_commit_);
  FinalizeFrame(FlushReason::kOffscreenCanvasPushFrame);
  bool ret = Host()->PushFrame(
      ProduceCanvasResource(FlushReason::kOffscreenCanvasPushFrame),
      damage_rect);
  dirty_rect_for_commit_.setEmpty();
  GetOffscreenFontCache().PruneLocalFontCache(kMaxCachedFonts);
  return ret;
}

CanvasRenderingContextHost*
OffscreenCanvasRenderingContext2D::GetCanvasRenderingContextHost() const {
  return Host();
}
ExecutionContext* OffscreenCanvasRenderingContext2D::GetTopExecutionContext()
    const {
  return Host()->GetTopExecutionContext();
}

ImageBitmap* OffscreenCanvasRenderingContext2D::TransferToImageBitmap(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  WebFeature feature = WebFeature::kOffscreenCanvasTransferToImageBitmap2D;
  UseCounter::Count(ExecutionContext::From(script_state), feature);

  if (layer_count_ != 0) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "`transferToImageBitmap()` cannot be called while layers are opened.");
    return nullptr;
  }

  if (!GetOrCreateCanvasResourceProvider())
    return nullptr;
  scoped_refptr<StaticBitmapImage> image = GetImage(FlushReason::kTransfer);
  if (!image)
    return nullptr;
  image->SetOriginClean(OriginClean());
  // Before discarding the image resource, we need to flush pending render ops
  // to fully resolve the snapshot.
  image->PaintImageForCurrentFrame().FlushPendingSkiaOps();

  Host()->DiscardResources();

  return MakeGarbageCollected<ImageBitmap>(std::move(image));
}

scoped_refptr<StaticBitmapImage> OffscreenCanvasRenderingContext2D::GetImage(
    FlushReason reason) {
  FinalizeFrame(reason);
  if (!IsPaintable())
    return nullptr;
  scoped_refptr<StaticBitmapImage> image =
      GetCanvasResourceProvider()->Snapshot(reason);

  return image;
}

V8RenderingContext* OffscreenCanvasRenderingContext2D::AsV8RenderingContext() {
  return nullptr;
}

V8OffscreenRenderingContext*
OffscreenCanvasRenderingContext2D::AsV8OffscreenRenderingContext() {
  return MakeGarbageCollected<V8OffscreenRenderingContext>(this);
}

Color OffscreenCanvasRenderingContext2D::GetCurrentColor() const {
  return Color::kBlack;
}

cc::PaintCanvas* OffscreenCanvasRenderingContext2D::GetOrCreatePaintCanvas() {
  if (!is_valid_size_ || isContextLost() ||
      !GetOrCreateCanvasResourceProvider()) [[unlikely]] {
    return nullptr;
  }
  return GetPaintCanvas();
}

const cc::PaintCanvas* OffscreenCanvasRenderingContext2D::GetPaintCanvas()
    const {
  if (!is_valid_size_ || isContextLost()) [[unlikely]] {
    return nullptr;
  }
  CanvasResourceProvider* const provider = GetCanvasResourceProvider();
  if (provider == nullptr) [[unlikely]] {
    return nullptr;
  }
  return &provider->Canvas();
}

const MemoryManagedPaintRecorder* OffscreenCanvasRenderingContext2D::Recorder()
    const {
  const CanvasResourceProvider* provider = GetCanvasResourceProvider();
  if (provider == nullptr) [[unlikely]] {
    return nullptr;
  }
  return &provider->Recorder();
}

void OffscreenCanvasRenderingContext2D::WillDraw(
    const SkIRect& dirty_rect,
    CanvasPerformanceMonitor::DrawType draw_type) {
  dirty_rect_for_commit_.join(dirty_rect);
  GetCanvasPerformanceMonitor().DidDraw(draw_type);
  if (GetState().ShouldAntialias()) {
    SkIRect inflated_dirty_rect = dirty_rect_for_commit_.makeOutset(1, 1);
    Host()->DidDraw(inflated_dirty_rect);
  } else {
    Host()->DidDraw(dirty_rect_for_commit_);
  }
  if (CanvasResourceProvider* provider =
          Host()->GetResourceProviderForCanvas2D();
      layer_count_ == 0 && provider != nullptr) [[likely]] {
    // TODO(crbug.com/1246486): Make auto-flushing layer friendly.
    provider->FlushIfRecordingLimitExceeded();
  }
}

sk_sp<PaintFilter> OffscreenCanvasRenderingContext2D::StateGetFilter() {
  return GetState().GetFilterForOffscreenCanvas(Host()->Size(), this);
}

void OffscreenCanvasRenderingContext2D::LoseContext(LostContextMode lost_mode) {
  if (context_lost_mode_ != kNotLostContext)
    return;
  context_lost_mode_ = lost_mode;
  ResetInternal();
  if (CanvasRenderingContextHost* host = Host()) [[likely]] {
    host->DiscardResources();
    host->DiscardResourceDispatcher();
  }
  uint32_t delay = base::RandInt(1, kMaxIframeContextLoseDelay);
  dispatch_context_lost_event_timer_.StartOneShot(base::Milliseconds(delay),
                                                  FROM_HERE);
}

bool OffscreenCanvasRenderingContext2D::IsPaintable() const {
  return Host()->GetResourceProviderForCanvas2D();
}

bool OffscreenCanvasRenderingContext2D::WritePixels(
    const SkImageInfo& orig_info,
    const void* pixels,
    size_t row_bytes,
    int x,
    int y) {
  DCHECK(IsCanvas2DBufferValid());

  Host()->FlushRecordingForCanvas2D(FlushReason::kWritePixels);

  // Short-circuit out if an error occurred while flushing the recording.
  if (!Host()->GetResourceProviderForCanvas2D()->IsValid()) {
    return false;
  }

  return Host()->GetResourceProviderForCanvas2D()->WritePixels(
      orig_info, pixels, row_bytes, x, y);
}

bool OffscreenCanvasRenderingContext2D::ResolveFont(const String& new_font) {
  OffscreenFontCache& font_cache = GetOffscreenFontCache();
  FontDescription* cached_font = font_cache.GetFont(new_font);
  CanvasRenderingContextHost* const host = Host();
  bool use_locale = RuntimeEnabledFeatures::CanvasTextLangEnabled();
  const LayoutLocale* locale = use_locale ? LocaleFromLang() : nullptr;

  if (cached_font) {
    if (use_locale && locale != cached_font->Locale()) {
      cached_font->SetLocale(locale);
    }
    GetState().SetFont(*cached_font, host->GetFontSelector());
  } else {
    auto* style =
        CSSParser::ParseFont(new_font, host->GetTopExecutionContext());
    if (!style) {
      return false;
    }
    FontDescription desc = FontStyleResolver::ComputeFont(
        *style, host->GetFontSelector()->BaseFontSelector());
    if (use_locale) {
      desc.SetLocale(locale);
    }
    font_cache.AddFont(new_font, desc);
    GetState().SetFont(desc, host->GetFontSelector());
  }
  return true;
}

bool OffscreenCanvasRenderingContext2D::IsCanvas2DBufferValid() const {
  if (IsPaintable())
    return GetCanvasResourceProvider()->IsValid();
  return false;
}

std::optional<cc::PaintRecord> OffscreenCanvasRenderingContext2D::FlushCanvas(
    FlushReason reason) {
  if (CanvasResourceProvider* provider = GetCanvasResourceProvider())
      [[likely]] {
    return provider->FlushCanvas(reason);
  }
  return std::nullopt;
}

OffscreenCanvas* OffscreenCanvasRenderingContext2D::HostAsOffscreenCanvas()
    const {
  return static_cast<OffscreenCanvas*>(Host());
}

UniqueFontSelector* OffscreenCanvasRenderingContext2D::GetFontSelector() const {
  return Host()->GetFontSelector();
}

CanvasResourceProvider*
OffscreenCanvasRenderingContext2D::GetOrCreateCanvas2DResourceProvider() {
  return GetOrCreateCanvasResourceProvider();
}

}  // namespace blink
