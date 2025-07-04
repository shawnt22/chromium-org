// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/layers/ui_resource_layer_impl.h"

#include <memory>

#include "base/trace_event/traced_value.h"
#include "cc/base/math_util.h"
#include "cc/trees/layer_tree_impl.h"
#include "cc/trees/occlusion.h"
#include "components/viz/common/quads/texture_draw_quad.h"
#include "ui/gfx/geometry/rect_f.h"

namespace cc {

UIResourceLayerImpl::UIResourceLayerImpl(LayerTreeImpl* tree_impl, int id)
    : LayerImpl(tree_impl, id),
      ui_resource_id_(0),
      uv_top_left_(0.f, 0.f),
      uv_bottom_right_(1.f, 1.f) {
}

UIResourceLayerImpl::~UIResourceLayerImpl() = default;

mojom::LayerType UIResourceLayerImpl::GetLayerType() const {
  return mojom::LayerType::kUIResource;
}

std::unique_ptr<LayerImpl> UIResourceLayerImpl::CreateLayerImpl(
    LayerTreeImpl* tree_impl) const {
  return UIResourceLayerImpl::Create(tree_impl, id());
}

void UIResourceLayerImpl::PushPropertiesTo(LayerImpl* layer) {
  LayerImpl::PushPropertiesTo(layer);
  UIResourceLayerImpl* layer_impl = static_cast<UIResourceLayerImpl*>(layer);

  layer_impl->SetUIResourceId(ui_resource_id_);
  layer_impl->SetImageBounds(image_bounds_);
  layer_impl->SetUV(uv_top_left_, uv_bottom_right_);
}

void UIResourceLayerImpl::SetUIResourceId(UIResourceId uid) {
  if (uid == ui_resource_id_)
    return;
  ui_resource_id_ = uid;
  NoteLayerPropertyChanged();
}

void UIResourceLayerImpl::SetImageBounds(const gfx::Size& image_bounds) {
  // This check imposes an ordering on the call sequence.  An UIResource must
  // exist before SetImageBounds can be called.
  DCHECK(ui_resource_id_);

  if (image_bounds_ == image_bounds)
    return;

  image_bounds_ = image_bounds;

  NoteLayerPropertyChanged();
}

void UIResourceLayerImpl::SetUV(const gfx::PointF& top_left,
                                const gfx::PointF& bottom_right) {
  if (uv_top_left_ == top_left && uv_bottom_right_ == bottom_right)
    return;
  uv_top_left_ = top_left;
  uv_bottom_right_ = bottom_right;
  NoteLayerPropertyChanged();
}

bool UIResourceLayerImpl::WillDraw(
    DrawMode draw_mode,
    viz::ClientResourceProvider* resource_provider) {
  if (!ui_resource_id_ || draw_mode == DRAW_MODE_RESOURCELESS_SOFTWARE)
    return false;
  return LayerImpl::WillDraw(draw_mode, resource_provider);
}

void UIResourceLayerImpl::AppendQuads(const AppendQuadsContext& context,
                                      viz::CompositorRenderPass* render_pass,
                                      AppendQuadsData* append_quads_data) {
  DCHECK(!bounds().IsEmpty());

  viz::SharedQuadState* shared_quad_state =
      render_pass->CreateAndAppendSharedQuadState();

  viz::ResourceId resource =
      ui_resource_id_
          ? layer_tree_impl()->ResourceIdForUIResource(ui_resource_id_)
          : viz::kInvalidResourceId;
  bool are_contents_opaque =
      resource && (layer_tree_impl()->IsUIResourceOpaque(ui_resource_id_) ||
                   contents_opaque());
  PopulateSharedQuadState(shared_quad_state, are_contents_opaque);
  AppendDebugBorderQuad(render_pass, gfx::Rect(bounds()), shared_quad_state,
                        append_quads_data);

  if (!resource)
    return;

  static const bool nearest_neighbor = false;

  gfx::Rect quad_rect(bounds());
  bool needs_blending = !are_contents_opaque;
  gfx::Rect visible_quad_rect =
      draw_properties().occlusion_in_content_space.GetUnoccludedContentRect(
          quad_rect);
  if (visible_quad_rect.IsEmpty())
    return;

  auto* quad = render_pass->CreateAndAppendDrawQuad<viz::TextureDrawQuad>();
  quad->SetNew(shared_quad_state, quad_rect, visible_quad_rect, needs_blending,
               resource, uv_top_left_, uv_bottom_right_, SkColors::kTransparent,
               nearest_neighbor,
               /*secure_output_only=*/false, gfx::ProtectedVideoType::kClear);
  ValidateQuadResources(quad);
}

void UIResourceLayerImpl::AsValueInto(
    base::trace_event::TracedValue* state) const {
  LayerImpl::AsValueInto(state);

  MathUtil::AddToTracedValue("ImageBounds", image_bounds_, state);
  MathUtil::AddToTracedValue("UVTopLeft", uv_top_left_, state);
  MathUtil::AddToTracedValue("UVBottomRight", uv_bottom_right_, state);
}

}  // namespace cc
