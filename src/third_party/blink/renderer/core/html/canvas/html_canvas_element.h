/*
 * Copyright (C) 2004, 2006, 2009, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_CANVAS_HTML_CANVAS_ELEMENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_CANVAS_HTML_CANVAS_ELEMENT_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "cc/layers/texture_layer_client.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_surface.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_token.h"
#include "third_party/blink/renderer/bindings/core/v8/script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_blob_callback.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context_host.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/imagebitmap/image_bitmap_source.h"
#include "third_party/blink/renderer/core/page/page_visibility_observer.h"
#include "third_party/blink/renderer/platform/bindings/v8_external_memory_accounter.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_host.h"
#include "third_party/blink/renderer/platform/graphics/graphics_types_3d.h"
#include "third_party/blink/renderer/platform/graphics/offscreen_canvas_placeholder.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/surface_layer_bridge.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/prefinalizer.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"

#define CanvasDefaultInterpolationQuality kInterpolationLow

namespace cc {
class TextureLayer;
}

namespace blink {

class CanvasHibernationHandler;
class CanvasContextCreationAttributesCore;
class CanvasDrawListener;
class CanvasHighDynamicRangeOptions;
class CanvasRenderingContextFactory;
class CanvasResourceProvider;
class Element;
class GraphicsContext;
class HTMLCanvasElement;
class ImageBitmapOptions;
class StaticBitmapImageToVideoFrameCopier;
class SharedContextRateLimiter;

class
    CanvasRenderingContext2DOrWebGLRenderingContextOrWebGL2RenderingContextOrImageBitmapRenderingContextOrGPUCanvasContext;
typedef CanvasRenderingContext2DOrWebGLRenderingContextOrWebGL2RenderingContextOrImageBitmapRenderingContextOrGPUCanvasContext
    RenderingContext;

// This contains the information of HTML Canvas Element,
// There are four different types of rendering context this HTML Canvas can own.
// It can be a 3D Context (WebGL or WebGL2), 2D Context,
// BitmapRenderingContext or it can have no context (Offscreen placeholder).
// To check the no context case is good to check if there is a placeholder.
class CORE_EXPORT HTMLCanvasElement final
    : public HTMLElement,
      public ExecutionContextLifecycleObserver,
      public PageVisibilityObserver,
      public CanvasRenderingContextHost,
      public cc::TextureLayerClient,
      public WebSurfaceLayerBridgeObserver,
      public OffscreenCanvasPlaceholder {
  DEFINE_WRAPPERTYPEINFO();
  USING_PRE_FINALIZER(HTMLCanvasElement, Dispose);

 public:
  using ElementHitTestRegion = CanvasRenderingContext::ElementHitTestRegion;

  using Node::GetExecutionContext;

  explicit HTMLCanvasElement(Document&);
  ~HTMLCanvasElement() override;

  // cc::TextureLayerClient implementation.
  bool PrepareTransferableResource(
      viz::TransferableResource* out_resource,
      viz::ReleaseCallback* out_release_callback) override;
  bool IsCanvas2DResourceValid();

  // Attributes and functions exposed to script
  unsigned width() const { return Size().width(); }
  unsigned height() const { return Size().height(); }

  void setWidth(unsigned, ExceptionState&);
  void setHeight(unsigned, ExceptionState&);

  void setLayoutSubtree(bool);
  bool layoutSubtree() const;

  void SetSize(gfx::Size new_size) final;

  // Called by Document::getCSSCanvasContext as well as above getContext().
  CanvasRenderingContext* GetCanvasRenderingContext(
      const String&,
      const CanvasContextCreationAttributesCore&);

  String toDataURL(const String& mime_type,
                   const ScriptValue& quality_argument,
                   ExceptionState&) const;
  String toDataURL(const String& mime_type,
                   ExceptionState& exception_state) const {
    return toDataURL(mime_type, ScriptValue(), exception_state);
  }

  void toBlob(V8BlobCallback*,
              const String& mime_type,
              const ScriptValue& quality_argument,
              ExceptionState&);
  void toBlob(V8BlobCallback* callback,
              const String& mime_type,
              ExceptionState& exception_state) {
    return toBlob(callback, mime_type, ScriptValue(), exception_state);
  }
  void configureHighDynamicRange(const CanvasHighDynamicRangeOptions*,
                                 ExceptionState&);

  bool IsPresentationAttribute(const QualifiedName&) const final;
  void CollectStyleForPresentationAttribute(
      const QualifiedName&,
      const AtomicString&,
      HeapVector<CSSPropertyValue, 8>&) final;

  bool IsCompositedForCanvas2D() const;

  // Used for canvas capture.
  void AddListener(CanvasDrawListener*);
  void RemoveListener(CanvasDrawListener*);
  // Derived from OffscreenCanvasPlaceholder.
  bool HasCanvasCapture() const final { return !listeners_.empty(); }

  // Used for rendering
  void DidDraw(const SkIRect&) override;
  using CanvasRenderingContextHost::DidDraw;

  void Paint(GraphicsContext&,
             const PhysicalRect&,
             bool flatten_composited_layers);

  CanvasRenderingContext* RenderingContext() const override {
    return context_.Get();
  }

  bool OriginClean() const override;
  void SetOriginTainted() override { origin_clean_ = false; }

  CanvasHibernationHandler* GetHibernationHandler() const;

  unsigned IncrementFramesSinceLastCommit() {
    return ++frames_since_last_commit_;
  }

  SharedContextRateLimiter* RateLimiter() const;
  void CreateRateLimiter();

  void SetIsDisplayed(bool);
  bool IsDisplayed() const { return is_displayed_; }

  cc::TextureLayer* GetOrCreateCcLayerForCanvas2DIfNeeded();
  cc::TextureLayer* GetCanvas2DCcLayerForTesting() { return cc_layer_.get(); }
  void ClearCanvas2DLayerTexture() override;

  void SetNeedsPushProperties();

  void DiscardResources() override;

  TextDirection GetTextDirection(const ComputedStyle*) override;
  const LayoutLocale* GetLocale() const override;

  UniqueFontSelector* GetFontSelector() override;

  bool ShouldBeDirectComposited() const;

  const AtomicString ImageSourceURL() const override;

  InsertionNotificationRequest InsertedInto(ContainerNode&) override;

  bool IsDirty() { return !dirty_rect_.IsEmpty(); }

  void DoDeferredPaintInvalidation();

  void InitializeLayerWithCSSProperties(cc::Layer* layer) override;
  void PreFinalizeFrame() override;
  void PostFinalizeFrame(FlushReason) override;

  CanvasResourceDispatcher* GetOrCreateResourceDispatcher() override;
  void DiscardResourceDispatcher() override { frame_dispatcher_ = nullptr; }

  bool PushFrame(scoped_refptr<CanvasResource>&& image,
                 const SkIRect& damage_rect) override;

  // ExecutionContextLifecycleObserver and PageVisibilityObserver implementation
  void ContextDestroyed() override;

  // PageVisibilityObserver implementation
  void PageVisibilityChanged() override;

  // CanvasImageSource implementation
  scoped_refptr<Image> GetSourceImageForCanvas(FlushReason,
                                               SourceImageStatus*,
                                               const gfx::SizeF&) override;
  bool WouldTaintOrigin() const override;
  gfx::SizeF ElementSize(const gfx::SizeF&,
                         const RespectImageOrientationEnum) const override;
  bool IsCanvasElement() const override { return true; }
  bool IsOpaque() const override;

  // SurfaceLayerBridgeObserver implementation
  void OnWebLayerUpdated() override;
  void RegisterContentsLayer(cc::Layer*) override;
  void UnregisterContentsLayer(cc::Layer*) override;

  // CanvasResourceHost implementation
  bool IsPageVisible() const override;
  void NotifyGpuContextLost() override;
  void SetNeedsCompositingUpdate() override;
  void UpdateMemoryUsage() override;
  size_t GetMemoryUsage() const override;
  bool ShouldAccelerate2dContext() const override;
  bool LowLatencyEnabled() const override;
  CanvasResourceProvider* GetOrCreateCanvasResourceProviderForCanvas2D();
  bool IsPrinting() const override;
  bool IsHibernating() const override;
  void SetTransferToGPUTextureWasInvoked() override;
  bool TransferToGPUTextureWasInvoked() override;

  // CanvasRenderingContextHost implementation.
  UkmParameters GetUkmParameters() override;

  void DisableAccelerationForCanvas2D();
  bool EnableAccelerationForCanvas2D() final;

  // ImageBitmapSource implementation
  ScriptPromise<ImageBitmap> CreateImageBitmap(
      ScriptState*,
      std::optional<gfx::Rect> crop_rect,
      const ImageBitmapOptions*,
      ExceptionState&) override;

  // OffscreenCanvasPlaceholder implementation.
  void SetOffscreenCanvasResource(scoped_refptr<CanvasResource>&&,
                                  viz::ResourceId resource_id) override;
  void Trace(Visitor*) const override;

  void SetCanvas2DResourceProviderForTesting(
      std::unique_ptr<CanvasResourceProvider> provider,
      const gfx::Size& size);

  static void RegisterRenderingContextFactory(
      std::unique_ptr<CanvasRenderingContextFactory>);

  bool StyleChangeNeedsDidDraw(const ComputedStyle* old_style,
                               const ComputedStyle& new_style);
  void StyleDidChange(const ComputedStyle* old_style,
                      const ComputedStyle& new_style);
  void LayoutObjectDestroyed();
  void LangAttributeChanged() override;

  void NotifyListenersCanvasChanged();

  // For OffscreenCanvas that controls this html canvas element
  ::blink::SurfaceLayerBridge* SurfaceLayerBridge() const {
    return surface_layer_bridge_.get();
  }
  bool CreateLayer();

  void DetachContext() override { context_ = nullptr; }

  void WillDrawImageInCanvas2D(CanvasImageSource*);

  ExecutionContext* GetTopExecutionContext() const override {
    return GetDocument().GetExecutionContext();
  }

  const KURL& GetExecutionContextUrl() const override {
    return GetDocument().Url();
  }

  DispatchEventResult HostDispatchEvent(Event* event) override {
    return DispatchEvent(*event);
  }

  void UpdateSuspendOffscreenCanvasAnimation();

  // Gets the settings of this Html Canvas Element. If there is a frame, it will
  // return the settings from the frame. If it is a frameless element it will
  // try to fetch the global dom window and get the settings from there.
  Settings* GetSettings() const;
  bool IsWebGL1Enabled() const override;
  bool IsWebGL2Enabled() const override;
  bool IsWebGLBlocked() const override;
  void SetContextCreationWasBlocked() override;

  bool NeedsUnbufferedInputEvents() const { return needs_unbuffered_input_; }

  void SetNeedsUnbufferedInputEvents(bool value) {
    needs_unbuffered_input_ = value;
  }

  scoped_refptr<StaticBitmapImage> Snapshot(FlushReason,
                                            SourceDrawingBuffer) const;

  // Returns the cc layer containing the contents. It's the cc layer of
  // SurfaceLayerBridge() or RenderingContext(), or nullptr if the canvas is not
  // composited.
  cc::Layer* ContentsCcLayer() const;

  // Return the image orientation setting from the layout object, if available.
  // In the absence of a layout object, kRespectImageOrientation will be
  // returned.
  RespectImageOrientationEnum RespectImageOrientation() const;

  bool IsCanvasClear() { return canvas_is_clear_; }

  bool IsPlaceholder() const override { return IsOffscreenCanvasRegistered(); }

  bool CanStartSelection() const override;

  bool ShouldDisableAccelerationBecauseOfReadback() const;

  void SetHitTestRegions(VectorOf<ElementHitTestRegion> hit_test_regions);
  const VectorOf<ElementHitTestRegion>& GetHitTestRegions() const;

 protected:
  void DidMoveToNewDocument(Document& old_document) override;
  void DidRecalcStyle(const StyleRecalcChange change) override;
  void RemovedFrom(ContainerNode& insertion_point) override;

 private:
  enum class ReadbackType {
    kWebExposed,
    kNotWebExposed,
  };

  void Dispose();

  // Updates the preferred 2D raster mode based on the state of the context and
  // GPU acceleration.
  void UpdatePreferred2DRasterMode();

  // Recreates the resource provider.
  // TODO(crbug.com/40280152): Remove parameter once the hibernation handler is
  // an instance variable of this class.
  CanvasResourceProvider* RecreateCanvasResourceProviderForCanvas2D(
      CanvasHibernationHandler& hibernation_handler);
  void CreateCanvasResourceProviderForCanvas2D();

  void ColorSchemeMayHaveChanged();

  void RecordIdentifiabilityMetric(IdentifiableSurface surface,
                                   IdentifiableToken value) const;

  // If the user is enrolled in the identifiability study, report the canvas
  // type, and if applicable, canvas digest, taint bits, and
  // |canvas_contents_token|, which represents the current bitmap displayed by
  // this canvas.
  void IdentifiabilityReportWithDigest(
      IdentifiableToken canvas_contents_token) const;

  void PaintInternal(GraphicsContext&, const PhysicalRect&);

  using ContextFactoryVector =
      Vector<std::unique_ptr<CanvasRenderingContextFactory>>;
  static ContextFactoryVector& RenderingContextFactories();
  static CanvasRenderingContextFactory* GetRenderingContextFactory(int);

  bool ShouldAccelerate() const;
  void ParseAttribute(const AttributeModificationParams&) override;
  void AttributeChanged(const AttributeModificationParams&) override;
  LayoutObject* CreateLayoutObject(const ComputedStyle&) override;
  bool AreAuthorShadowsAllowed() const override { return false; }

  void Reset();
  void ResetLayer();

  void SetSurfaceSize(gfx::Size);

  bool SizeChangesAreAllowed(ExceptionState& exception_state);

  bool PaintsIntoCanvasBuffer() const;

  String ToDataURLInternal(
      const String& mime_type,
      const double& quality,
      SourceDrawingBuffer,
      ReadbackType readback_type = ReadbackType::kWebExposed) const;

  // Returns the transparent image resource for this canvas.
  scoped_refptr<StaticBitmapImage> GetTransparentImage();

  CanvasRenderingContext* GetCanvasRenderingContextInternal(
      const String&,
      const CanvasContextCreationAttributesCore&);

  scoped_refptr<StaticBitmapImage> GetSourceImageForCanvasInternal(
      FlushReason,
      SourceImageStatus*);

  static std::pair<blink::Image*, float> BrokenCanvas(
      float device_scale_factor);

  bool RecreateCanvasInGPURasterModeForCanvas2D();

  FRIEND_TEST_ALL_PREFIXES(HTMLCanvasElementTest, BrokenCanvasHighRes);

  HeapHashSet<WeakMember<CanvasDrawListener>> listeners_;

  Member<CanvasRenderingContext> context_;
  // Used only for WebGL currently.
  bool context_creation_was_blocked_;

  bool disposing_ = false;
  bool canvas_is_clear_ = true;

  bool ignore_reset_ = false;
  gfx::Rect dirty_rect_;

  scoped_refptr<cc::TextureLayer> cc_layer_;

  bool is_opaque_ = false;
  bool is_displayed_ = false;
  unsigned frames_since_last_commit_ = 0;
  std::unique_ptr<SharedContextRateLimiter> rate_limiter_;
  gfx::HDRMetadata hdr_metadata_;
  bool origin_clean_;
  bool needs_unbuffered_input_ = false;
  bool style_is_visible_ = false;

  // CanvasHibernationHandler is used when canvas has 2d rendering context
  std::unique_ptr<CanvasHibernationHandler> hibernation_handler_;

  // If the ResourceProvider currently exists, replaces it with a
  // CanvasResourceProvider that was newly created for usage with a 2D context.
  void DropAndRecreateExistingCanvas2DResourceProvider();

  // Used for OffscreenCanvas that controls this HTML canvas element
  // and for low latency mode.
  std::unique_ptr<::blink::SurfaceLayerBridge> surface_layer_bridge_;

  // Used for low latency mode.
  // TODO: rename to CanvasFrameDispatcher.
  std::unique_ptr<CanvasResourceDispatcher> frame_dispatcher_;

  std::unique_ptr<StaticBitmapImageToVideoFrameCopier> copier_;

  bool did_notify_listeners_for_current_frame_ = false;

  // GPU Memory Management
  mutable intptr_t externally_allocated_memory_;

  scoped_refptr<StaticBitmapImage> transparent_image_;

  // Paint flags set based on CSS properties, which must be propagated to the
  // cc::Layer.
  cc::PaintFlags::FilterQuality filter_quality_ =
      cc::PaintFlags::FilterQuality::kLow;
  cc::PaintFlags::DynamicRangeLimitMixture dynamic_range_limit_;

  VectorOf<ElementHitTestRegion> hit_test_regions_;

  NO_UNIQUE_ADDRESS V8ExternalMemoryAccounterBase external_memory_accounter_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_CANVAS_HTML_CANVAS_ELEMENT_H_
