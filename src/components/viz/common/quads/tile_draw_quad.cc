// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/quads/tile_draw_quad.h"

#include "base/check.h"
#include "base/trace_event/traced_value.h"
#include "base/values.h"

namespace viz {

TileDrawQuad::TileDrawQuad() = default;

TileDrawQuad::~TileDrawQuad() = default;

void TileDrawQuad::SetNew(const SharedQuadState* shared_quad_state,
                          const gfx::Rect& rect,
                          const gfx::Rect& visible_rect,
                          bool needs_blending,
                          ResourceId resource,
                          const gfx::RectF& tex_coord_rect,
                          bool nearest_neighbor,
                          bool force_anti_aliasing_off) {
  CHECK_NE(resource, kInvalidResourceId);
  ContentDrawQuadBase::SetNew(shared_quad_state,
                              DrawQuad::Material::kTiledContent, rect,
                              visible_rect, needs_blending, tex_coord_rect,
                              nearest_neighbor, force_anti_aliasing_off);
  resource_id = resource;
}

void TileDrawQuad::SetAll(const SharedQuadState* shared_quad_state,
                          const gfx::Rect& rect,
                          const gfx::Rect& visible_rect,
                          bool needs_blending,
                          ResourceId resource,
                          const gfx::RectF& tex_coord_rect,
                          bool nearest_neighbor,
                          bool force_anti_aliasing_off) {
  CHECK_NE(resource, kInvalidResourceId);
  ContentDrawQuadBase::SetAll(shared_quad_state,
                              DrawQuad::Material::kTiledContent, rect,
                              visible_rect, needs_blending, tex_coord_rect,
                              nearest_neighbor, force_anti_aliasing_off);
  resource_id = resource;
}

const TileDrawQuad* TileDrawQuad::MaterialCast(const DrawQuad* quad) {
  CHECK_EQ(quad->material, DrawQuad::Material::kTiledContent);
  return static_cast<const TileDrawQuad*>(quad);
}

void TileDrawQuad::ExtendValue(base::trace_event::TracedValue* value) const {
  ContentDrawQuadBase::ExtendValue(value);
  value->SetInteger("resource_id", resource_id.GetUnsafeValue());
}

}  // namespace viz
