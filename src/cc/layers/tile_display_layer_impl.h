// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_TILE_DISPLAY_LAYER_IMPL_H_
#define CC_LAYERS_TILE_DISPLAY_LAYER_IMPL_H_

#include <map>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "base/memory/raw_ref.h"
#include "cc/base/tiling_data.h"
#include "cc/cc_export.h"
#include "cc/layers/layer_impl.h"
#include "cc/mojom/missing_tile_reason.mojom.h"
#include "cc/tiles/tile_index.h"
#include "cc/tiles/tile_priority.h"
#include "cc/tiles/tiling_coverage_iterator.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "components/viz/common/resources/transferable_resource.h"
#include "ui/gfx/geometry/axis_transform2d.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace cc {

// Viz-side counterpart to a client-side PictureLayerImpl when TreesInViz is
// enabled. Clients push tiling information and tile contents from a picture
// layer down to Viz, and this layer uses that information to draw tile quads.
class CC_EXPORT TileDisplayLayerImpl : public LayerImpl {
 public:
  struct NoContents {
    mojom::MissingTileReason reason =
        mojom::MissingTileReason::kResourceNotReady;

    NoContents() = default;
    explicit NoContents(mojom::MissingTileReason r) : reason(r) {}
  };

  struct CC_EXPORT TileResource {
    TileResource(viz::ResourceId resource_id,
                 gfx::Size resource_size,
                 bool is_checkered);
    TileResource(const TileResource&);
    TileResource& operator=(const TileResource&);
    ~TileResource();

    viz::ResourceId resource_id;
    gfx::Size resource_size;
    bool is_checkered;
  };

  using TileContents = std::variant<NoContents, SkColor4f, TileResource>;

  class CC_EXPORT Tile {
   public:
    explicit Tile(TileDisplayLayerImpl& layer, const TileContents& contents);
    ~Tile();
    Tile(Tile&&);

    const TileContents& contents() const { return contents_; }

    std::optional<SkColor4f> solid_color() const {
      if (std::holds_alternative<SkColor4f>(contents_)) {
        return std::get<SkColor4f>(contents_);
      }
      return std::nullopt;
    }

    std::optional<TileResource> resource() const {
      if (std::holds_alternative<TileResource>(contents_)) {
        return std::get<TileResource>(contents_);
      }
      return std::nullopt;
    }

    // We only construct Tile objects that are ready to draw.
    bool IsReadyToDraw() const { return true; }

   private:
    const raw_ref<TileDisplayLayerImpl> layer_;
    TileContents contents_;
  };

  class DisplayTilingCoverageIterator;

  class CC_EXPORT Tiling {
   public:
    using Tile = Tile;
    using TileMap = std::map<TileIndex, std::unique_ptr<Tile>>;
    using CoverageIterator = DisplayTilingCoverageIterator;

    explicit Tiling(TileDisplayLayerImpl& layer, float scale_key);
    ~Tiling();

    Tile* TileAt(const TileIndex& index) const;

    float contents_scale_key() const { return scale_key_; }
    TileResolution resolution() const { return HIGH_RESOLUTION; }
    const TilingData* tiling_data() const { return &tiling_data_; }
    gfx::Size raster_size() const { return layer_->bounds(); }
    const gfx::AxisTransform2d& raster_transform() const {
      return raster_transform_;
    }
    const gfx::Size tile_size() const {
      return tiling_data_.max_texture_size();
    }
    const gfx::Rect tiling_rect() const { return tiling_data_.tiling_rect(); }
    const TileMap& tiles() const { return tiles_; }

    void SetRasterTransform(const gfx::AxisTransform2d& transform);
    void SetTileSize(const gfx::Size& size);
    void SetTilingRect(const gfx::Rect& rect);
    void SetTileContents(const TileIndex& key,
                         const TileContents& contents,
                         bool update_damage);

    CoverageIterator Cover(const gfx::Rect& coverage_rect,
                           float coverage_scale) const;

   private:
    const raw_ref<TileDisplayLayerImpl> layer_;
    const float scale_key_;
    gfx::AxisTransform2d raster_transform_;
    TilingData tiling_data_{gfx::Size(), gfx::Rect(), /*border_texels=*/1};
    TileMap tiles_;
  };

  class CC_EXPORT DisplayTilingCoverageIterator
      : public TilingCoverageIterator<Tiling> {
   public:
    using TilingCoverageIterator<Tiling>::TilingCoverageIterator;
  };

  TileDisplayLayerImpl(LayerTreeImpl& tree, int id);
  ~TileDisplayLayerImpl() override;

  Tiling& GetOrCreateTilingFromScaleKey(float scale_key);
  void RemoveTiling(float scale_key);

  void SetSolidColor(std::optional<SkColor4f> color) { solid_color_ = color; }
  void SetIsBackdropFilterMask(bool is_backdrop_filter_mask) {
    is_backdrop_filter_mask_ = is_backdrop_filter_mask;
  }
  void SetIsDirectlyCompositedImage(bool is_directly_composited_image) {
    is_directly_composited_image_ = is_directly_composited_image;
  }
  void SetNearestNeighbor(bool nearest_neighbor) {
    nearest_neighbor_ = nearest_neighbor;
  }
  bool is_directly_composited_image() const {
    return is_directly_composited_image_;
  }
  bool nearest_neighbor() const { return nearest_neighbor_; }

  // LayerImpl overrides:
  mojom::LayerType GetLayerType() const override;
  std::unique_ptr<LayerImpl> CreateLayerImpl(
      LayerTreeImpl* tree_impl) const override;
  void PushPropertiesTo(LayerImpl* layer) override;
  void AppendQuads(const AppendQuadsContext& context,
                   viz::CompositorRenderPass* render_pass,
                   AppendQuadsData* append_quads_data) override;
  void GetContentsResourceId(viz::ResourceId* resource_id,
                             gfx::Size* resource_size,
                             gfx::SizeF* resource_uv_size) const override;
  gfx::Rect GetDamageRect() const override;
  void ResetChangeTracking() override;

  void RecordDamage(const gfx::Rect& damage_rect);

  const Tiling* GetTilingForTesting(float scale_key) const;
  void DiscardResource(viz::ResourceId resource);

  // For testing
  std::optional<SkColor4f> solid_color_for_testing() const {
    return solid_color_;
  }
  bool is_backdrop_filter_mask_for_testing() const {
    return is_backdrop_filter_mask_;
  }

 private:
  std::optional<SkColor4f> solid_color_;
  bool is_backdrop_filter_mask_ = false;
  bool is_directly_composited_image_ = false;
  bool nearest_neighbor_ = false;

  // Denotes an area that is damaged and needs redraw. This is in the layer's
  // space.
  gfx::Rect damage_rect_;
  std::vector<std::unique_ptr<Tiling>> tilings_;
};

}  // namespace cc

#endif  // CC_LAYERS_TILE_DISPLAY_LAYER_IMPL_H_
