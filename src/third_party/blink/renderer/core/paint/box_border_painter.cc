// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/box_border_painter.h"

#include <algorithm>

#include "third_party/blink/renderer/core/paint/box_painter.h"
#include "third_party/blink/renderer/core/paint/contoured_border_geometry.h"
#include "third_party/blink/renderer/core/paint/object_painter.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/style/border_edge.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/geometry/contoured_rect.h"
#include "third_party/blink/renderer/platform/geometry/float_rounded_rect.h"
#include "third_party/blink/renderer/platform/geometry/path_builder.h"
#include "third_party/blink/renderer/platform/geometry/stroke_data.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"
#include "third_party/blink/renderer/platform/graphics/styled_stroke_data.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/line_f.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace blink {

namespace {

enum BorderEdgeFlag {
  kTopBorderEdge = 1 << static_cast<unsigned>(BoxSide::kTop),
  kRightBorderEdge = 1 << static_cast<unsigned>(BoxSide::kRight),
  kBottomBorderEdge = 1 << static_cast<unsigned>(BoxSide::kBottom),
  kLeftBorderEdge = 1 << static_cast<unsigned>(BoxSide::kLeft),
  kAllBorderEdges =
      kTopBorderEdge | kBottomBorderEdge | kLeftBorderEdge | kRightBorderEdge
};

inline BorderEdgeFlag EdgeFlagForSide(BoxSide side) {
  return static_cast<BorderEdgeFlag>(1 << static_cast<unsigned>(side));
}

inline bool IncludesEdge(BorderEdgeFlags flags, BoxSide side) {
  return flags & EdgeFlagForSide(side);
}

inline bool IncludesAdjacentEdges(BorderEdgeFlags flags) {
  // The set includes adjacent edges iff it contains at least one horizontal and
  // one vertical edge.
  return (flags & (kTopBorderEdge | kBottomBorderEdge)) &&
         (flags & (kLeftBorderEdge | kRightBorderEdge));
}

inline bool StyleRequiresClipPolygon(EBorderStyle style) {
  // These are drawn with a stroke, so we have to clip to get corner miters.
  return style == EBorderStyle::kDotted || style == EBorderStyle::kDashed;
}

inline bool BorderStyleFillsBorderArea(EBorderStyle style) {
  return !(style == EBorderStyle::kDotted || style == EBorderStyle::kDashed ||
           style == EBorderStyle::kDouble);
}

inline bool BorderStyleHasInnerDetail(EBorderStyle style) {
  return style == EBorderStyle::kGroove || style == EBorderStyle::kRidge ||
         style == EBorderStyle::kDouble;
}

inline bool BorderStyleIsDottedOrDashed(EBorderStyle style) {
  return style == EBorderStyle::kDotted || style == EBorderStyle::kDashed;
}

// BorderStyleOutset darkens the bottom and right (and maybe lightens the top
// and left) BorderStyleInset darkens the top and left (and maybe lightens the
// bottom and right).
inline bool BorderStyleHasUnmatchedColorsAtCorner(EBorderStyle style,
                                                  BoxSide side,
                                                  BoxSide adjacent_side) {
  // These styles match at the top/left and bottom/right.
  if (style == EBorderStyle::kInset || style == EBorderStyle::kGroove ||
      style == EBorderStyle::kRidge || style == EBorderStyle::kOutset) {
    const BorderEdgeFlags top_right_flags =
        EdgeFlagForSide(BoxSide::kTop) | EdgeFlagForSide(BoxSide::kRight);
    const BorderEdgeFlags bottom_left_flags =
        EdgeFlagForSide(BoxSide::kBottom) | EdgeFlagForSide(BoxSide::kLeft);

    BorderEdgeFlags flags =
        EdgeFlagForSide(side) | EdgeFlagForSide(adjacent_side);
    return flags == top_right_flags || flags == bottom_left_flags;
  }
  return false;
}

inline bool BorderWillArcInnerEdge(const gfx::SizeF& first_radius,
                                   const gfx::SizeF& second_radius) {
  return !first_radius.IsZero() || !second_radius.IsZero();
}

inline bool WillOverdraw(BoxSide side,
                         EBorderStyle style,
                         BorderEdgeFlags completed_edges) {
  // If we're done with this side, it will obviously not overdraw any portion of
  // the current edge.
  if (IncludesEdge(completed_edges, side))
    return false;

  // The side is still to be drawn. It overdraws the current edge iff it has a
  // solid fill style.
  return BorderStyleFillsBorderArea(style);
}

inline bool BorderStylesRequireMiter(BoxSide side,
                                     BoxSide adjacent_side,
                                     EBorderStyle style,
                                     EBorderStyle adjacent_style) {
  if (style == EBorderStyle::kDouble ||
      adjacent_style == EBorderStyle::kDouble ||
      adjacent_style == EBorderStyle::kGroove ||
      adjacent_style == EBorderStyle::kRidge)
    return true;

  if (BorderStyleIsDottedOrDashed(style) !=
      BorderStyleIsDottedOrDashed(adjacent_style))
    return true;

  if (style != adjacent_style)
    return true;

  return BorderStyleHasUnmatchedColorsAtCorner(style, side, adjacent_side);
}

void SetToRightSideRect(gfx::Rect& rect, int edge_width) {
  rect.set_x(rect.right() - edge_width);
  rect.set_width(edge_width);
}

void SetToBottomSideRect(gfx::Rect& rect, int edge_width) {
  rect.set_y(rect.bottom() - edge_width);
  rect.set_height(edge_width);
}

gfx::Rect CalculateSideRect(const FloatRoundedRect& outer_border,
                            const BorderEdge& edge,
                            BoxSide side) {
  gfx::Rect side_rect = gfx::ToRoundedRect(outer_border.Rect());
  int width = edge.Width();

  switch (side) {
    case BoxSide::kTop:
      side_rect.set_height(width);
      break;
    case BoxSide::kBottom:
      SetToBottomSideRect(side_rect, width);
      break;
    case BoxSide::kLeft:
      side_rect.set_width(width);
      break;
    case BoxSide::kRight:
      SetToRightSideRect(side_rect, width);
      break;
  }
  return side_rect;
}

ContouredRect CalculateAdjustedInnerBorder(const ContouredRect& inner_border,
                                           BoxSide side) {
  if (!inner_border.GetCornerCurvature().IsHyperellipse()) {
    return inner_border;
  }

  // Expand the inner border as necessary to make it a rounded rect (i.e. radii
  // contained within each edge).  This function relies on the fact we only get
  // radii not contained within each edge if one of the radii for an edge is
  // zero, so we can shift the arc towards the zero radius corner.
  FloatRoundedRect::Radii new_radii = inner_border.GetRadii();
  gfx::RectF new_rect = inner_border.Rect();

  float overshoot;
  float max_radii;

  switch (side) {
    case BoxSide::kTop:
      overshoot = new_radii.TopLeft().width() + new_radii.TopRight().width() -
                  new_rect.width();
      // FIXME: once we start pixel-snapping rounded rects after this point, the
      // overshoot concept should disappear.
      if (overshoot > 0.1) {
        new_rect.set_width(new_rect.width() + overshoot);
        if (!new_radii.TopLeft().width())
          new_rect.Offset(-overshoot, 0);
      }
      new_radii.SetBottomLeft(gfx::SizeF(0, 0));
      new_radii.SetBottomRight(gfx::SizeF(0, 0));
      max_radii =
          std::max(new_radii.TopLeft().height(), new_radii.TopRight().height());
      if (max_radii > new_rect.height())
        new_rect.set_height(max_radii);
      break;

    case BoxSide::kBottom:
      overshoot = new_radii.BottomLeft().width() +
                  new_radii.BottomRight().width() - new_rect.width();
      if (overshoot > 0.1) {
        new_rect.set_width(new_rect.width() + overshoot);
        if (!new_radii.BottomLeft().width())
          new_rect.Offset(-overshoot, 0);
      }
      new_radii.SetTopLeft(gfx::SizeF(0, 0));
      new_radii.SetTopRight(gfx::SizeF(0, 0));
      max_radii = std::max(new_radii.BottomLeft().height(),
                           new_radii.BottomRight().height());
      if (max_radii > new_rect.height()) {
        new_rect.Offset(0, new_rect.height() - max_radii);
        new_rect.set_height(max_radii);
      }
      break;

    case BoxSide::kLeft:
      overshoot = new_radii.TopLeft().height() +
                  new_radii.BottomLeft().height() - new_rect.height();
      if (overshoot > 0.1) {
        new_rect.set_height(new_rect.height() + overshoot);
        if (!new_radii.TopLeft().height())
          new_rect.Offset(0, -overshoot);
      }
      new_radii.SetTopRight(gfx::SizeF(0, 0));
      new_radii.SetBottomRight(gfx::SizeF(0, 0));
      max_radii =
          std::max(new_radii.TopLeft().width(), new_radii.BottomLeft().width());
      if (max_radii > new_rect.width())
        new_rect.set_width(max_radii);
      break;

    case BoxSide::kRight:
      overshoot = new_radii.TopRight().height() +
                  new_radii.BottomRight().height() - new_rect.height();
      if (overshoot > 0.1) {
        new_rect.set_height(new_rect.height() + overshoot);
        if (!new_radii.TopRight().height())
          new_rect.Offset(0, -overshoot);
      }
      new_radii.SetTopLeft(gfx::SizeF(0, 0));
      new_radii.SetBottomLeft(gfx::SizeF(0, 0));
      max_radii = std::max(new_radii.TopRight().width(),
                           new_radii.BottomRight().width());
      if (max_radii > new_rect.width()) {
        new_rect.Offset(new_rect.width() - max_radii, 0);
        new_rect.set_width(max_radii);
      }
      break;
  }

  return ContouredRect(FloatRoundedRect(new_rect, new_radii),
                       inner_border.GetCornerCurvature());
}

void DrawSolidBorderRect(GraphicsContext& context,
                         const gfx::Rect& border_rect,
                         int border_width,
                         const Color& color,
                         const AutoDarkMode& auto_dark_mode) {
  gfx::RectF stroke_rect(border_rect);
  stroke_rect.Outset(-border_width / 2.f);

  bool was_antialias = context.ShouldAntialias();
  if (!was_antialias)
    context.SetShouldAntialias(true);

  context.SetStrokeColor(color);
  context.SetStrokeThickness(border_width);
  context.StrokeRect(stroke_rect, auto_dark_mode);

  if (!was_antialias)
    context.SetShouldAntialias(false);
}

void DrawBleedAdjustedDRRect(GraphicsContext& context,
                             BackgroundBleedAvoidance bleed_avoidance,
                             const FloatRoundedRect& outer,
                             const FloatRoundedRect& inner,
                             Color color,
                             const AutoDarkMode& auto_dark_mode) {
  switch (bleed_avoidance) {
    case kBackgroundBleedClipLayer: {
      // BackgroundBleedClipLayer clips the outer rrect for the whole layer.
      // Based on this, we can avoid background bleeding by filling the
      // *outside* of inner rrect, all the way to the layer bounds (enclosing
      // int rect for the clip, in device space).
      SkPath path;
      path.addRRect(SkRRect(inner));
      path.setFillType(SkPathFillType::kInverseWinding);

      cc::PaintFlags flags;
      flags.setColor(color.toSkColor4f());
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setAntiAlias(true);
      context.DrawPath(path, flags, auto_dark_mode);

      break;
    }
    case kBackgroundBleedClipOnly:
      if (outer.IsRounded()) {
        // BackgroundBleedClipOnly clips the outer rrect corners for us.
        FloatRoundedRect adjusted_outer = outer;
        adjusted_outer.SetRadii(FloatRoundedRect::Radii());
        context.FillDRRect(adjusted_outer, inner, color, auto_dark_mode);
        break;
      }
      [[fallthrough]];
    default:
      context.FillDRRect(outer, inner, color, auto_dark_mode);
      break;
  }
}

// The LUTs below assume specific enum values.
static_assert(EBorderStyle::kNone == static_cast<EBorderStyle>(0),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kHidden == static_cast<EBorderStyle>(1),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kInset == static_cast<EBorderStyle>(2),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kGroove == static_cast<EBorderStyle>(3),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kOutset == static_cast<EBorderStyle>(4),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kRidge == static_cast<EBorderStyle>(5),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kDotted == static_cast<EBorderStyle>(6),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kDashed == static_cast<EBorderStyle>(7),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kSolid == static_cast<EBorderStyle>(8),
              "unexpected EBorderStyle value");
static_assert(EBorderStyle::kDouble == static_cast<EBorderStyle>(9),
              "unexpected EBorderStyle value");

static_assert(static_cast<unsigned>(BoxSide::kTop) == 0,
              "unexpected BoxSide value");
static_assert(static_cast<unsigned>(BoxSide::kRight) == 1,
              "unexpected BoxSide value");
static_assert(static_cast<unsigned>(BoxSide::kBottom) == 2,
              "unexpected BoxSide value");
static_assert(static_cast<unsigned>(BoxSide::kLeft) == 3,
              "unexpected BoxSide value");

// Style-based paint order: non-solid edges (dashed/dotted/double) are painted
// before solid edges (inset/outset/groove/ridge/solid) to maximize overdraw
// opportunities.
constexpr auto kStylePriority = std::to_array<unsigned>({
    0,  // EBorderStyle::kNone
    0,  // EBorderStyle::kHidden
    2,  // EBorderStyle::kInset
    2,  // EBorderStyle::kGroove
    2,  // EBorderStyle::kOutset
    2,  // EBorderStyle::kRidge,
    1,  // EBorderStyle::kDotted
    1,  // EBorderStyle::kDashed
    3,  // EBorderStyle::kSolid
    1,  // EBorderStyle::kDouble
});

// Given the same style, prefer drawing in non-adjacent order to minimize the
// number of sides which require miters.
constexpr auto kSidePriority = std::to_array<unsigned>({
    0,  // BoxSide::kTop
    2,  // BoxSide::kRight
    1,  // BoxSide::kBottom
    3,  // BoxSide::kLeft
});

// Edges sharing the same opacity. Stores both a side list and an edge bitfield
// to support constant time iteration + membership tests.
struct OpacityGroup {
  DISALLOW_NEW();

 public:
  explicit OpacityGroup(float alpha) : edge_flags(0), alpha(alpha) {}

  Vector<BoxSide, 4> sides;
  BorderEdgeFlags edge_flags;
  float alpha;
};

void ClipPolygon(GraphicsContext& context,
                 base::span<const gfx::PointF> vertices,
                 bool antialiased) {
  SkPath path;
  path.moveTo(gfx::PointFToSkPoint(vertices[0]));
  for (size_t i = 1; i < vertices.size(); ++i) {
    path.lineTo(gfx::PointFToSkPoint(vertices[i]));
  }

  context.ClipPath(path, antialiased ? kAntiAliased : kNotAntiAliased);
}

void DrawDashedOrDottedBoxSide(GraphicsContext& context,
                               int x1,
                               int y1,
                               int x2,
                               int y2,
                               BoxSide side,
                               Color color,
                               int thickness,
                               EBorderStyle style,
                               const AutoDarkMode& auto_dark_mode) {
  DCHECK_GT(thickness, 0);

  GraphicsContextStateSaver state_saver(context);
  context.SetShouldAntialias(true);
  context.SetStrokeColor(color);
  StyledStrokeData styled_stroke;
  styled_stroke.SetThickness(thickness);
  styled_stroke.SetStyle(style == EBorderStyle::kDashed ? kDashedStroke
                                                        : kDottedStroke);

  switch (side) {
    case BoxSide::kBottom:
    case BoxSide::kTop: {
      int mid_y = y1 + thickness / 2;
      context.DrawLine(gfx::Point(x1, mid_y), gfx::Point(x2, mid_y),
                       styled_stroke, auto_dark_mode);
      break;
    }
    case BoxSide::kRight:
    case BoxSide::kLeft: {
      int mid_x = x1 + thickness / 2;
      context.DrawLine(gfx::Point(mid_x, y1), gfx::Point(mid_x, y2),
                       styled_stroke, auto_dark_mode);
      break;
    }
  }
}

void DrawLineForBoxSide(GraphicsContext& context,
                        int x1,
                        int y1,
                        int x2,
                        int y2,
                        BoxSide side,
                        Color color,
                        EBorderStyle style,
                        int adjacent_width1,
                        int adjacent_width2,
                        const AutoDarkMode& auto_dark_mode);

bool DarkenBoxSide(BoxSide side, EBorderStyle style) {
  return (side == BoxSide::kTop || side == BoxSide::kLeft) ==
         (style == EBorderStyle::kInset);
}

Color CalculateInsetOutsetColor(bool is_darken, const Color& color) {
  const Color dark_color = color.Dark();
  // Inset, outset, ridge, and groove paint a darkened or "shadow" edge:
  // https://w3c.github.io/csswg-drafts/css-backgrounds/#border-style. By
  // default, darken |color| for the darker edge and use |color| for the lighter
  // edge.
  if (is_darken) {
    return dark_color;
  }

  const SkColor4f sk_color = color.toSkColor4f();
  auto should_lighten_color = [sk_color, dark_color]() -> bool {
    // This constant is used to determine if there is enough contrast between
    // the darkened edge and |color|. If not, also lighten |color| for the
    // lighter edge.
    constexpr float kMinimumBorderEdgeContrastRatio = 1.75f;
    return color_utils::GetContrastRatio(sk_color, dark_color.toSkColor4f()) <
           kMinimumBorderEdgeContrastRatio;
  };
  // The following condition skips should_lighten_color() when the result is
  // know to be false. The values came from a brute force search of r, g, b
  // values, see https://crrev.com/c/4200827/3.
  if (sk_color.fR >= (150 / 255.0f) || sk_color.fG >= (92 / 255.0f)) {
    DCHECK(!should_lighten_color());
    return color;
  }
  return should_lighten_color() ? color.Light() : color;
}

void DrawDoubleBoxSide(GraphicsContext& context,
                       int x1,
                       int y1,
                       int x2,
                       int y2,
                       int length,
                       BoxSide side,
                       Color color,
                       int thickness,
                       int adjacent_width1,
                       int adjacent_width2,
                       const AutoDarkMode& auto_dark_mode) {
  int third_of_thickness = (thickness + 1) / 3;
  DCHECK_GT(third_of_thickness, 0);

  if (!adjacent_width1 && !adjacent_width2) {
    context.SetFillColor(color);

    const bool was_antialiased = context.ShouldAntialias();
    context.SetShouldAntialias(true);

    switch (side) {
      case BoxSide::kTop:
      case BoxSide::kBottom:
        context.FillRect(gfx::Rect(x1, y1, length, third_of_thickness),
                         auto_dark_mode);
        context.FillRect(
            gfx::Rect(x1, y2 - third_of_thickness, length, third_of_thickness),
            auto_dark_mode);
        break;
      case BoxSide::kLeft:
      case BoxSide::kRight:
        context.FillRect(gfx::Rect(x1, y1, third_of_thickness, length),
                         auto_dark_mode);
        context.FillRect(
            gfx::Rect(x2 - third_of_thickness, y1, third_of_thickness, length),
            auto_dark_mode);
        break;
    }

    context.SetShouldAntialias(was_antialiased);
    return;
  }

  int adjacent1_big_third =
      ((adjacent_width1 > 0) ? adjacent_width1 + 1 : adjacent_width1 - 1) / 3;
  int adjacent2_big_third =
      ((adjacent_width2 > 0) ? adjacent_width2 + 1 : adjacent_width2 - 1) / 3;

  switch (side) {
    case BoxSide::kTop:
      DrawLineForBoxSide(
          context, x1 + std::max((-adjacent_width1 * 2 + 1) / 3, 0), y1,
          x2 - std::max((-adjacent_width2 * 2 + 1) / 3, 0),
          y1 + third_of_thickness, side, color, EBorderStyle::kSolid,
          adjacent1_big_third, adjacent2_big_third, auto_dark_mode);
      DrawLineForBoxSide(context,
                         x1 + std::max((adjacent_width1 * 2 + 1) / 3, 0),
                         y2 - third_of_thickness,
                         x2 - std::max((adjacent_width2 * 2 + 1) / 3, 0), y2,
                         side, color, EBorderStyle::kSolid, adjacent1_big_third,
                         adjacent2_big_third, auto_dark_mode);
      break;
    case BoxSide::kLeft:
      DrawLineForBoxSide(context, x1,
                         y1 + std::max((-adjacent_width1 * 2 + 1) / 3, 0),
                         x1 + third_of_thickness,
                         y2 - std::max((-adjacent_width2 * 2 + 1) / 3, 0), side,
                         color, EBorderStyle::kSolid, adjacent1_big_third,
                         adjacent2_big_third, auto_dark_mode);
      DrawLineForBoxSide(context, x2 - third_of_thickness,
                         y1 + std::max((adjacent_width1 * 2 + 1) / 3, 0), x2,
                         y2 - std::max((adjacent_width2 * 2 + 1) / 3, 0), side,
                         color, EBorderStyle::kSolid, adjacent1_big_third,
                         adjacent2_big_third, auto_dark_mode);
      break;
    case BoxSide::kBottom:
      DrawLineForBoxSide(
          context, x1 + std::max((adjacent_width1 * 2 + 1) / 3, 0), y1,
          x2 - std::max((adjacent_width2 * 2 + 1) / 3, 0),
          y1 + third_of_thickness, side, color, EBorderStyle::kSolid,
          adjacent1_big_third, adjacent2_big_third, auto_dark_mode);
      DrawLineForBoxSide(context,
                         x1 + std::max((-adjacent_width1 * 2 + 1) / 3, 0),
                         y2 - third_of_thickness,
                         x2 - std::max((-adjacent_width2 * 2 + 1) / 3, 0), y2,
                         side, color, EBorderStyle::kSolid, adjacent1_big_third,
                         adjacent2_big_third, auto_dark_mode);
      break;
    case BoxSide::kRight:
      DrawLineForBoxSide(context, x1,
                         y1 + std::max((adjacent_width1 * 2 + 1) / 3, 0),
                         x1 + third_of_thickness,
                         y2 - std::max((adjacent_width2 * 2 + 1) / 3, 0), side,
                         color, EBorderStyle::kSolid, adjacent1_big_third,
                         adjacent2_big_third, auto_dark_mode);
      DrawLineForBoxSide(context, x2 - third_of_thickness,
                         y1 + std::max((-adjacent_width1 * 2 + 1) / 3, 0), x2,
                         y2 - std::max((-adjacent_width2 * 2 + 1) / 3, 0), side,
                         color, EBorderStyle::kSolid, adjacent1_big_third,
                         adjacent2_big_third, auto_dark_mode);
      break;
    default:
      break;
  }
}

void DrawRidgeOrGrooveBoxSide(GraphicsContext& context,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              BoxSide side,
                              Color color,
                              EBorderStyle style,
                              int adjacent_width1,
                              int adjacent_width2,
                              const AutoDarkMode& auto_dark_mode) {
  EBorderStyle s1;
  EBorderStyle s2;
  if (style == EBorderStyle::kGroove) {
    s1 = EBorderStyle::kInset;
    s2 = EBorderStyle::kOutset;
  } else {
    s1 = EBorderStyle::kOutset;
    s2 = EBorderStyle::kInset;
  }

  int adjacent1_big_half =
      ((adjacent_width1 > 0) ? adjacent_width1 + 1 : adjacent_width1 - 1) / 2;
  int adjacent2_big_half =
      ((adjacent_width2 > 0) ? adjacent_width2 + 1 : adjacent_width2 - 1) / 2;

  switch (side) {
    case BoxSide::kTop:
      DrawLineForBoxSide(context, x1 + std::max(-adjacent_width1, 0) / 2, y1,
                         x2 - std::max(-adjacent_width2, 0) / 2,
                         (y1 + y2 + 1) / 2, side, color, s1, adjacent1_big_half,
                         adjacent2_big_half, auto_dark_mode);
      DrawLineForBoxSide(
          context, x1 + std::max(adjacent_width1 + 1, 0) / 2, (y1 + y2 + 1) / 2,
          x2 - std::max(adjacent_width2 + 1, 0) / 2, y2, side, color, s2,
          adjacent_width1 / 2, adjacent_width2 / 2, auto_dark_mode);
      break;
    case BoxSide::kLeft:
      DrawLineForBoxSide(
          context, x1, y1 + std::max(-adjacent_width1, 0) / 2,
          (x1 + x2 + 1) / 2, y2 - std::max(-adjacent_width2, 0) / 2, side,
          color, s1, adjacent1_big_half, adjacent2_big_half, auto_dark_mode);
      DrawLineForBoxSide(
          context, (x1 + x2 + 1) / 2, y1 + std::max(adjacent_width1 + 1, 0) / 2,
          x2, y2 - std::max(adjacent_width2 + 1, 0) / 2, side, color, s2,
          adjacent_width1 / 2, adjacent_width2 / 2, auto_dark_mode);
      break;
    case BoxSide::kBottom:
      DrawLineForBoxSide(context, x1 + std::max(adjacent_width1, 0) / 2, y1,
                         x2 - std::max(adjacent_width2, 0) / 2,
                         (y1 + y2 + 1) / 2, side, color, s2, adjacent1_big_half,
                         adjacent2_big_half, auto_dark_mode);
      DrawLineForBoxSide(context, x1 + std::max(-adjacent_width1 + 1, 0) / 2,
                         (y1 + y2 + 1) / 2,
                         x2 - std::max(-adjacent_width2 + 1, 0) / 2, y2, side,
                         color, s1, adjacent_width1 / 2, adjacent_width2 / 2,
                         auto_dark_mode);
      break;
    case BoxSide::kRight:
      DrawLineForBoxSide(
          context, x1, y1 + std::max(adjacent_width1, 0) / 2, (x1 + x2 + 1) / 2,
          y2 - std::max(adjacent_width2, 0) / 2, side, color, s2,
          adjacent1_big_half, adjacent2_big_half, auto_dark_mode);
      DrawLineForBoxSide(context, (x1 + x2 + 1) / 2,
                         y1 + std::max(-adjacent_width1 + 1, 0) / 2, x2,
                         y2 - std::max(-adjacent_width2 + 1, 0) / 2, side,
                         color, s1, adjacent_width1 / 2, adjacent_width2 / 2,
                         auto_dark_mode);
      break;
  }
}

void FillQuad(GraphicsContext& context,
              const gfx::QuadF& quad,
              const Color& color,
              const AutoDarkMode& auto_dark_mode) {
  SkPath path;
  path.moveTo(gfx::PointFToSkPoint(quad.p1()));
  path.lineTo(gfx::PointFToSkPoint(quad.p2()));
  path.lineTo(gfx::PointFToSkPoint(quad.p3()));
  path.lineTo(gfx::PointFToSkPoint(quad.p4()));
  cc::PaintFlags flags(context.FillFlags());
  flags.setAntiAlias(true);
  flags.setColor(color.toSkColor4f());

  context.DrawPath(path, flags, auto_dark_mode);
}

void DrawSolidBoxSide(GraphicsContext& context,
                      int x1,
                      int y1,
                      int x2,
                      int y2,
                      BoxSide side,
                      Color color,
                      int adjacent_width1,
                      int adjacent_width2,
                      const AutoDarkMode& auto_dark_mode) {
  DCHECK_GE(x2, x1);
  DCHECK_GE(y2, y1);

  if (!adjacent_width1 && !adjacent_width2) {
    // Tweak antialiasing to match the behavior of fillQuad();
    // this matters for rects in transformed contexts.
    const bool was_antialiased = context.ShouldAntialias();
    if (!was_antialiased) {
      context.SetShouldAntialias(true);
    }
    context.FillRect(gfx::Rect(x1, y1, x2 - x1, y2 - y1), color,
                     auto_dark_mode);
    if (!was_antialiased) {
      context.SetShouldAntialias(was_antialiased);
    }
    return;
  }

  gfx::QuadF quad;
  switch (side) {
    case BoxSide::kTop:
      quad.set_p1(gfx::PointF(x1 + std::max(-adjacent_width1, 0), y1));
      quad.set_p2(gfx::PointF(x1 + std::max(adjacent_width1, 0), y2));
      quad.set_p3(gfx::PointF(x2 - std::max(adjacent_width2, 0), y2));
      quad.set_p4(gfx::PointF(x2 - std::max(-adjacent_width2, 0), y1));
      break;
    case BoxSide::kBottom:
      quad.set_p1(gfx::PointF(x1 + std::max(adjacent_width1, 0), y1));
      quad.set_p2(gfx::PointF(x1 + std::max(-adjacent_width1, 0), y2));
      quad.set_p3(gfx::PointF(x2 - std::max(-adjacent_width2, 0), y2));
      quad.set_p4(gfx::PointF(x2 - std::max(adjacent_width2, 0), y1));
      break;
    case BoxSide::kLeft:
      quad.set_p1(gfx::PointF(x1, y1 + std::max(-adjacent_width1, 0)));
      quad.set_p2(gfx::PointF(x1, y2 - std::max(-adjacent_width2, 0)));
      quad.set_p3(gfx::PointF(x2, y2 - std::max(adjacent_width2, 0)));
      quad.set_p4(gfx::PointF(x2, y1 + std::max(adjacent_width1, 0)));
      break;
    case BoxSide::kRight:
      quad.set_p1(gfx::PointF(x1, y1 + std::max(adjacent_width1, 0)));
      quad.set_p2(gfx::PointF(x1, y2 - std::max(adjacent_width2, 0)));
      quad.set_p3(gfx::PointF(x2, y2 - std::max(-adjacent_width2, 0)));
      quad.set_p4(gfx::PointF(x2, y1 + std::max(-adjacent_width1, 0)));
      break;
  }

  FillQuad(context, quad, color, auto_dark_mode);
}

void DrawLineForBoxSide(GraphicsContext& context,
                        int x1,
                        int y1,
                        int x2,
                        int y2,
                        BoxSide side,
                        Color color,
                        EBorderStyle style,
                        int adjacent_width1,
                        int adjacent_width2,
                        const AutoDarkMode& auto_dark_mode) {
  int thickness;
  int length;
  if (side == BoxSide::kTop || side == BoxSide::kBottom) {
    thickness = y2 - y1;
    length = x2 - x1;
  } else {
    thickness = x2 - x1;
    length = y2 - y1;
  }

  // We would like this check to be an ASSERT as we don't want to draw empty
  // borders. However nothing guarantees that the following recursive calls to
  // DrawLineForBoxSide() will have positive thickness and length.
  if (length <= 0 || thickness <= 0) {
    return;
  }

  style = BorderEdge::EffectiveStyle(style, thickness);

  switch (style) {
    case EBorderStyle::kDotted:
    case EBorderStyle::kDashed:
      DrawDashedOrDottedBoxSide(context, x1, y1, x2, y2, side, color, thickness,
                                style, auto_dark_mode);
      break;
    case EBorderStyle::kDouble:
      DrawDoubleBoxSide(context, x1, y1, x2, y2, length, side, color, thickness,
                        adjacent_width1, adjacent_width2, auto_dark_mode);
      break;
    case EBorderStyle::kRidge:
    case EBorderStyle::kGroove:
      DrawRidgeOrGrooveBoxSide(context, x1, y1, x2, y2, side, color, style,
                               adjacent_width1, adjacent_width2,
                               auto_dark_mode);
      break;
    case EBorderStyle::kInset:
    case EBorderStyle::kOutset:
      color = CalculateInsetOutsetColor(DarkenBoxSide(side, style), color);
      [[fallthrough]];
    case EBorderStyle::kSolid:
      DrawSolidBoxSide(context, x1, y1, x2, y2, side, color, adjacent_width1,
                       adjacent_width2, auto_dark_mode);
      break;
    case EBorderStyle::kNone:
    case EBorderStyle::kHidden:
      NOTREACHED();
  }
}

using Corner = ContouredRect::Corner;

struct CornerInfo {
  Corner outer;
  Corner inner;
  // The outer corner of the inner border, if it was not adjusted for curvature.
  gfx::PointF unadjusted_inner_edge;
};

void FindIntersection(const gfx::PointF& p1,
                      const gfx::PointF& p2,
                      const gfx::PointF& d1,
                      const gfx::PointF& d2,
                      gfx::PointF& intersection) {
  intersection =
      gfx::LineF(p1, p2).IntersectionWith({d1, d2}).value_or(intersection);
}

void ClipOutHalfCornerWithMiter(GraphicsContext& context,
                                const std::array<CornerInfo, 4>& corners,
                                AntiAliasingMode antialias_mode) {
  const CornerInfo& corner_to_slice = corners[0];
  const CornerInfo& other_corner_of_same_side = corners[1];
  const gfx::PointF& opposite_corner = corners[2].outer.Outer();
  const gfx::PointF& adjacent_corner = corners[3].outer.Outer();
  const gfx::LineF miter_line(corner_to_slice.outer.Outer(),
                              corner_to_slice.unadjusted_inner_edge);

  // When the corners intersect, we check if the intersection of the
  // nearest tangent line of the superellipse intersects with the miter line,
  // and whether that intersection is inside the other corner's bounding box.
  // If so, that overlap might be visible, so we clip out a hexagon that
  // starts from the miter incision and continues back at the tangent.
  const gfx::LineF inner_tangent_of_other_corner(
      other_corner_of_same_side.inner.End(),
      other_corner_of_same_side.inner.IsConcave()
          ? other_corner_of_same_side.inner.QuadraticControlPoint()
          : other_corner_of_same_side.inner.Start());
  const std::optional<gfx::PointF> intersection_between_tangent_and_miter =
      inner_tangent_of_other_corner.IntersectionWith(miter_line);
  if (intersection_between_tangent_and_miter.has_value() &&
      other_corner_of_same_side.inner.BoundingBox().InclusiveContains(
          *intersection_between_tangent_and_miter)) {
    const std::optional<gfx::PointF>
        intersection_between_tangent_and_opposite_edge =
            inner_tangent_of_other_corner.IntersectionWith(
                {other_corner_of_same_side.outer.Outer(),
                 other_corner_of_same_side.outer.Start()});
    // Clip out a hexagon that cuts out the part of the corner that should
    // not be rendered with the current side's color. The hexagon cuts this
    // corner at the miter, meets the other corner at the tangent, and
    // continues to the opposite corners to make sure all necessary parts of
    // this corner are cut. By meeting the other corner at the hull we ensure
    // that no visible part of that curner are cut.
    context.ClipPath(
        PathBuilder()
            .MoveTo(corner_to_slice.outer.Outer())
            .LineTo(*intersection_between_tangent_and_miter)
            .LineTo(intersection_between_tangent_and_opposite_edge.value_or(
                other_corner_of_same_side.inner.Center()))
            .LineTo(opposite_corner)
            .LineTo(adjacent_corner)
            .LineTo(corner_to_slice.outer.Start())
            .Close()
            .Finalize()
            .GetSkPath(),
        antialias_mode, SkClipOp::kDifference);
    return;
  }

  // When the corners of this side don't intersect, clip a triangle that goes
  // through the miter and the opposite side.
  const gfx::Vector2dF unadjusted_offset =
      corner_to_slice.unadjusted_inner_edge - corner_to_slice.outer.Outer();
  const gfx::PointF miter_hypot =
      miter_line
          .IntersectionWith({opposite_corner + unadjusted_offset,
                             adjacent_corner + unadjusted_offset})
          .value_or(opposite_corner);
  context.ClipPath(
      PathBuilder()
          .MoveTo(corner_to_slice.outer.Outer() - unadjusted_offset)
          .LineTo(miter_hypot)
          .LineTo(adjacent_corner + unadjusted_offset)
          .LineTo(adjacent_corner - unadjusted_offset)
          .Close()
          .Finalize()
          .GetSkPath(),
      antialias_mode, SkClipOp::kDifference);
}

// Make sure corners where the border-width > border-radius take the whole
// corner into account. We do that by extending the inner corner inwards to
// include the padding edge.
void ExtendInnerCornerToIncludePaddingEdgeIfNeeded(CornerInfo& corner) {
  if (corner.outer.IsZero() || corner.outer.IsStraight()) {
    return;
  }
  const gfx::Vector2dF side_direction =
      gfx::NormalizeVector2d(corner.inner.v2());
  const gfx::Vector2dF adjusted_vector = gfx::ScaleVector2d(
      side_direction,
      std::max(gfx::ScaleVector2d(
                   corner.unadjusted_inner_edge - corner.outer.Outer(),
                   side_direction.x(), side_direction.y())
                   .Length(),
               corner.inner.v2().Length()));
  corner.inner = Corner({corner.inner.Start(), corner.inner.Outer(),
                         corner.inner.Outer() + adjusted_vector,
                         corner.inner.Start() + adjusted_vector},
                        corner.inner.Curvature());
}

gfx::RectF UnionInnerCornersAndEdge(const CornerInfo& corner1,
                                    const CornerInfo& corner2) {
  return gfx::UnionRectsEvenIfEmpty(
      gfx::UnionRects(corner1.inner.BoundingBox(), corner2.inner.BoundingBox()),
      gfx::BoundingRect(corner1.unadjusted_inner_edge,
                        corner2.unadjusted_inner_edge));
}

void ClipBorderSidePolygonFromCorners(GraphicsContext& context,
                                      std::array<CornerInfo, 4> corners,
                                      AntiAliasingMode first_antialias,
                                      AntiAliasingMode second_antialias,
                                      const gfx::Vector2dF& width_vector,
                                      bool needs_miters) {
  const gfx::RectF edge_bounding_box =
      UnionInnerCornersAndEdge(corners[0], corners[1]);
  const gfx::RectF opposite_edge_bounding_box =
      UnionInnerCornersAndEdge(corners[2], corners[3]);
  if (edge_bounding_box.Intersects(opposite_edge_bounding_box) ||
      edge_bounding_box.Contains(corners[2].unadjusted_inner_edge) ||
      edge_bounding_box.Contains(corners[3].unadjusted_inner_edge) ||
      opposite_edge_bounding_box.Contains(corners[0].unadjusted_inner_edge) ||
      opposite_edge_bounding_box.Contains(corners[1].unadjusted_inner_edge)) {
    // Clip the full side, including the two full corners, to avoid overlapping
    // with the other sides.
    context.ClipPath(PathBuilder()
                         .MoveTo(corners[0].outer.Outer())
                         .LineTo(corners[0].outer.Start())
                         .AddCorner(corners[0].inner)
                         .LineTo(corners[0].outer.End() + width_vector)
                         .LineTo(corners[1].outer.Start() + width_vector)
                         .AddCorner(corners[1].inner)
                         .LineTo(corners[1].outer.End())
                         .LineTo(corners[1].outer.Outer())
                         .Close()
                         .MoveTo(corners[1].outer.Outer())
                         .LineTo(corners[0].outer.Outer())
                         .LineTo(corners[0].outer.Outer() + width_vector)
                         .LineTo(corners[1].outer.Outer() + width_vector)
                         .Close()
                         .Finalize()
                         .GetSkPath(),
                     kAntiAliased);
  } else {
    context.ClipOut(opposite_edge_bounding_box);
  }

  if (!needs_miters) {
    return;
  }

  ExtendInnerCornerToIncludePaddingEdgeIfNeeded(corners[0]);
  ExtendInnerCornerToIncludePaddingEdgeIfNeeded(corners[1]);
  // Clip two paths, one with the first full corner and the second corner
  // clipped at the miter, and the opposite one.
  CornerInfo second_corner_reversed{corners[1].outer.Reverse(),
                                    corners[1].inner.Reverse(),
                                    corners[1].unadjusted_inner_edge};

  ClipOutHalfCornerWithMiter(
      context, {corners[0], second_corner_reversed, corners[2], corners[3]},
      first_antialias);
  ClipOutHalfCornerWithMiter(
      context, {second_corner_reversed, corners[0], corners[3], corners[2]},
      second_antialias);
}

}  // anonymous namespace

// Holds edges grouped by opacity and sorted in paint order.
struct BoxBorderPainter::ComplexBorderInfo {
  STACK_ALLOCATED();

 public:
  explicit ComplexBorderInfo(const BoxBorderPainter& border_painter) {
    Vector<BoxSide, 4> sorted_sides;

    // First, collect all visible sides.
    for (unsigned i = border_painter.first_visible_edge_; i < 4; ++i) {
      BoxSide side = static_cast<BoxSide>(i);

      if (IncludesEdge(border_painter.visible_edge_set_, side))
        sorted_sides.push_back(side);
    }
    DCHECK(!sorted_sides.empty());

    // Then sort them in paint order, based on three (prioritized) criteria:
    // alpha, style, side.
    std::sort(sorted_sides.begin(), sorted_sides.end(),
              [&border_painter](BoxSide a, BoxSide b) -> bool {
                const BorderEdge& edge_a = border_painter.Edge(a);
                const BorderEdge& edge_b = border_painter.Edge(b);

                const float alpha_a = edge_a.GetColor().Alpha();
                const float alpha_b = edge_b.GetColor().Alpha();
                if (alpha_a != alpha_b)
                  return alpha_a < alpha_b;

                const unsigned style_priority_a =
                    kStylePriority[static_cast<unsigned>(edge_a.BorderStyle())];
                const unsigned style_priority_b =
                    kStylePriority[static_cast<unsigned>(edge_b.BorderStyle())];
                if (style_priority_a != style_priority_b)
                  return style_priority_a < style_priority_b;

                return kSidePriority[static_cast<unsigned>(a)] <
                       kSidePriority[static_cast<unsigned>(b)];
              });

    // Finally, build the opacity group structures.
    BuildOpacityGroups(border_painter, sorted_sides);
  }

  Vector<OpacityGroup, 4> opacity_groups;

 private:
  void BuildOpacityGroups(const BoxBorderPainter& border_painter,
                          const Vector<BoxSide, 4>& sorted_sides) {
    float current_alpha = 0.0f;
    for (BoxSide side : sorted_sides) {
      const BorderEdge& edge = border_painter.Edge(side);
      const float edge_alpha = edge.GetColor().Alpha();

      DCHECK_GT(edge_alpha, 0.0f);
      DCHECK_GE(edge_alpha, current_alpha);
      // TODO(crbug.com/1434423): This float comparison looks very brittle. We
      // need to deduce the original intention of the code here. Also, this path
      // is clearly un-tested and caused some serious regressions when touched.
      // See crbug.com/1445288
      if (edge_alpha != current_alpha) {
        opacity_groups.push_back(OpacityGroup(edge_alpha));
        current_alpha = edge_alpha;
      }

      DCHECK(!opacity_groups.empty());
      OpacityGroup& current_group = opacity_groups.back();
      current_group.sides.push_back(side);
      current_group.edge_flags |= EdgeFlagForSide(side);
    }

    DCHECK(!opacity_groups.empty());
  }
};

void BoxBorderPainter::DrawDoubleBorder() const {
  DCHECK(is_uniform_color_);
  DCHECK(is_uniform_style_);
  DCHECK(FirstEdge().BorderStyle() == EBorderStyle::kDouble);
  DCHECK(visible_edge_set_ == kAllBorderEdges);

  const Color& color = FirstEdge().GetColor();

  // When painting outlines, we ignore outer/inner radii.
  const auto force_rectangular = !outer_.IsRounded() && !inner_.IsRounded();

  AutoDarkMode auto_dark_mode(PaintAutoDarkMode(style_, element_role_));

  // outer stripe
  const PhysicalBoxStrut outer_third_outsets =
      DoubleStripeOutsets(BorderEdge::kDoubleBorderStripeOuter);
  FloatRoundedRect outer_third_rect =
      ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
          style_, border_rect_, outer_third_outsets, sides_to_include_)
          .AsRoundedRect();
  if (force_rectangular)
    outer_third_rect.SetRadii(FloatRoundedRect::Radii());
  DrawBleedAdjustedDRRect(context_, bleed_avoidance_, outer_.AsRoundedRect(),
                          outer_third_rect, color, auto_dark_mode);

  // inner stripe
  const PhysicalBoxStrut inner_third_outsets =
      DoubleStripeOutsets(BorderEdge::kDoubleBorderStripeInner);
  FloatRoundedRect inner_third_rect =
      ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
          style_, border_rect_, inner_third_outsets, sides_to_include_)
          .AsRoundedRect();
  if (force_rectangular)
    inner_third_rect.SetRadii(FloatRoundedRect::Radii());
  context_.FillDRRect(inner_third_rect, inner_.AsRoundedRect(), color,
                      auto_dark_mode);
}

bool BoxBorderPainter::PaintBorderFastPath() const {
  if (!is_uniform_color_ || !is_uniform_style_ || !inner_.IsRenderable() ||
      !inner_.HasRoundCurvature()) {
    return false;
  }

  if (FirstEdge().BorderStyle() != EBorderStyle::kSolid &&
      FirstEdge().BorderStyle() != EBorderStyle::kDouble)
    return false;

  if (visible_edge_set_ == kAllBorderEdges) {
    if (FirstEdge().BorderStyle() == EBorderStyle::kSolid) {
      if (is_uniform_width_ && !outer_.IsRounded()) {
        // 4-side, solid, uniform-width, rectangular border => one drawRect()
        DrawSolidBorderRect(context_, gfx::ToRoundedRect(outer_.Rect()),
                            FirstEdge().Width(), FirstEdge().GetColor(),
                            PaintAutoDarkMode(style_, element_role_));
      } else {
        // 4-side, solid border => one drawDRRect()
        DrawBleedAdjustedDRRect(context_, bleed_avoidance_,
                                outer_.AsRoundedRect(), inner_.AsRoundedRect(),
                                FirstEdge().GetColor(),
                                PaintAutoDarkMode(style_, element_role_));
      }
    } else {
      // 4-side, double border => 2x drawDRRect()
      DCHECK(FirstEdge().BorderStyle() == EBorderStyle::kDouble);
      DrawDoubleBorder();
    }

    return true;
  }

  // This is faster than the normal complex border path only if it avoids
  // creating transparency layers (when the border is translucent).
  if (FirstEdge().BorderStyle() == EBorderStyle::kSolid &&
      !outer_.IsRounded() && has_transparency_) {
    DCHECK(visible_edge_set_ != kAllBorderEdges);
    // solid, rectangular border => one drawPath()
    PathBuilder builder;
    builder.SetWindRule(RULE_NONZERO);

    for (auto side :
         {BoxSide::kTop, BoxSide::kRight, BoxSide::kBottom, BoxSide::kLeft}) {
      const BorderEdge& curr_edge = Edge(side);
      if (curr_edge.ShouldRender()) {
        builder.AddRect(gfx::RectF(
            CalculateSideRect(outer_.AsRoundedRect(), curr_edge, side)));
      }
    }

    context_.SetFillColor(FirstEdge().GetColor());
    context_.FillPath(builder.Finalize(),
                      PaintAutoDarkMode(style_, element_role_));
    return true;
  }

  return false;
}

BoxBorderPainter::BoxBorderPainter(GraphicsContext& context,
                                   const PhysicalRect& border_rect,
                                   const ComputedStyle& style,
                                   BackgroundBleedAvoidance bleed_avoidance,
                                   PhysicalBoxSides sides_to_include)
    : context_(context),
      border_rect_(border_rect),
      style_(style),
      bleed_avoidance_(bleed_avoidance),
      sides_to_include_(sides_to_include),
      visible_edge_count_(0),
      first_visible_edge_(0),
      visible_edge_set_(0),
      is_uniform_style_(true),
      is_uniform_width_(true),
      is_uniform_color_(true),
      is_rounded_(false),
      has_transparency_(false) {
  style.GetBorderEdgeInfo(edges_, sides_to_include);
  ComputeBorderProperties();

  // No need to compute the rrects if we don't have any borders to draw.
  if (!visible_edge_set_)
    return;

  outer_ = ContouredBorderGeometry::PixelSnappedContouredBorder(
      style_, border_rect, sides_to_include);
  inner_ = ContouredBorderGeometry::PixelSnappedContouredInnerBorder(
      style_, border_rect, sides_to_include);

  // Make sure that the border width isn't larger than the border box, which
  // can pixel snap smaller.
  float max_width = outer_.Rect().width();
  float max_height = outer_.Rect().height();
  Edge(BoxSide::kTop).ClampWidth(max_height);
  Edge(BoxSide::kRight).ClampWidth(max_width);
  Edge(BoxSide::kBottom).ClampWidth(max_height);
  Edge(BoxSide::kLeft).ClampWidth(max_width);

  is_rounded_ = outer_.IsRounded();

  element_role_ = DarkModeFilter::ElementRole::kBorder;
}

BoxBorderPainter::BoxBorderPainter(GraphicsContext& context,
                                   const ComputedStyle& style,
                                   const PhysicalRect& border_rect,
                                   int width,
                                   const PhysicalBoxStrut& inner_outsets)
    : context_(context),
      border_rect_(border_rect),
      outer_outsets_(inner_outsets + PhysicalBoxStrut(LayoutUnit(width))),
      style_(style),
      bleed_avoidance_(kBackgroundBleedNone),
      sides_to_include_(PhysicalBoxSides()),
      visible_edge_count_(0),
      first_visible_edge_(0),
      visible_edge_set_(0),
      is_uniform_style_(true),
      is_uniform_width_(true),
      is_uniform_color_(true),
      is_rounded_(false),
      has_transparency_(false) {
  DCHECK(style.HasOutline());

  BorderEdge edge(width,
                  style.VisitedDependentColor(GetCSSPropertyOutlineColor()),
                  style.OutlineStyle());
  for (auto& e : edges_)
    e = edge;
  ComputeBorderProperties();

  outer_ = ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
      style, border_rect, outer_outsets_);
  is_rounded_ = outer_.IsRounded();

  inner_ = ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
      style, border_rect, inner_outsets);

  element_role_ = DarkModeFilter::ElementRole::kBackground;
}

void BoxBorderPainter::ComputeBorderProperties() {
  for (unsigned i = 0; i < std::size(edges_); ++i) {
    const BorderEdge& edge = edges_[i];

    if (!edge.ShouldRender()) {
      if (edge.PresentButInvisible()) {
        is_uniform_width_ = false;
        is_uniform_color_ = false;
      }

      continue;
    }

    DCHECK(!edge.GetColor().IsFullyTransparent());

    visible_edge_count_++;
    visible_edge_set_ |= EdgeFlagForSide(static_cast<BoxSide>(i));

    if (!edge.GetColor().IsOpaque()) {
      has_transparency_ = true;
    }

    if (visible_edge_count_ == 1) {
      first_visible_edge_ = i;
      continue;
    }

    is_uniform_style_ &=
        edge.BorderStyle() == edges_[first_visible_edge_].BorderStyle();
    is_uniform_width_ &= edge.Width() == edges_[first_visible_edge_].Width();
    is_uniform_color_ &= edge.SharesColorWith(edges_[first_visible_edge_]);
  }
}

void BoxBorderPainter::ClipContouredRect(const ContouredRect& rect) const {
  context_.ClipContouredRect(rect);
}

void BoxBorderPainter::ClipOutContouredRect(const ContouredRect& rect) const {
    context_.ClipOutContouredRect(rect);
}

void BoxBorderPainter::Paint() const {
  if (!visible_edge_count_ || outer_.Rect().IsEmpty())
    return;

  if (PaintBorderFastPath())
    return;

  bool clip_to_outer_border = outer_.IsRounded();
  GraphicsContextStateSaver state_saver(context_, clip_to_outer_border);

  if (clip_to_outer_border) {
    // For BackgroundBleedClip{Only,Layer}, the outer rrect clip is already
    // applied.
    if (!BleedAvoidanceIsClipping(bleed_avoidance_)) {
      ClipContouredRect(outer_);
    }

    if (inner_.IsRenderable() && !inner_.IsEmpty()) {
      ClipOutContouredRect(inner_);
    }
  }

  const ComplexBorderInfo border_info(*this);
  PaintOpacityGroup(border_info, 0, 1);
}

// In order to maximize the use of overdraw as a corner seam avoidance
// technique, we draw translucent border sides using the following algorithm:
//
//   1) cluster sides sharing the same opacity into "opacity groups"
//      [ComplexBorderInfo]
//   2) sort groups in increasing opacity order [ComplexBorderInfo]
//   3) reverse-iterate over groups (decreasing opacity order), pushing nested
//      transparency layers with adjusted/relative opacity [paintOpacityGroup]
//   4) iterate over groups (increasing opacity order), painting actual group
//      contents and then ending their corresponding transparency layer
//      [PaintOpacityGroup]
//
// Layers are created in decreasing opacity order (top -> bottom), while actual
// border sides are drawn in increasing opacity order (bottom -> top). At each
// level, opacity is adjusted to account for accumulated/ancestor layer alpha.
// Because opacity is applied via layers, the actual draw paint is opaque.
//
// As an example, let's consider a border with the following sides/opacities:
//
//   top:    1.0
//   right:  0.25
//   bottom: 0.5
//   left:   0.25
//
// These are grouped and sorted in ComplexBorderInfo as follows:
//
//   group[0]: { alpha: 1.0,  sides: top }
//   group[1]: { alpha: 0.5,  sides: bottom }
//   group[2]: { alpha: 0.25, sides: right, left }
//
// Applying the algorithm yields the following paint sequence:
//
//                                // no layer needed for group 0 (alpha = 1)
//   beginLayer(0.5)              // layer for group 1
//     beginLayer(0.5)            // layer for group 2 (alpha: 0.5 * 0.5 = 0.25)
//       paintSides(right, left)  // paint group 2
//     endLayer
//     paintSides(bottom)         // paint group 1
//   endLayer
//   paintSides(top)              // paint group 0
//
// Note that we're always drawing using opaque paints on top of less-opaque
// content - hence we can use overdraw to mask portions of the previous sides.
//
BorderEdgeFlags BoxBorderPainter::PaintOpacityGroup(
    const ComplexBorderInfo& border_info,
    unsigned index,
    float effective_opacity) const {
  DCHECK(effective_opacity > 0 && effective_opacity <= 1);

  const wtf_size_t opacity_group_count = border_info.opacity_groups.size();

  // For overdraw logic purposes, treat missing/transparent edges as completed.
  if (index >= opacity_group_count)
    return ~visible_edge_set_;

  // Groups are sorted in increasing opacity order, but we need to create layers
  // in decreasing opacity order - hence the reverse iteration.
  const OpacityGroup& group =
      border_info.opacity_groups[opacity_group_count - index - 1];

  // Adjust this group's paint opacity to account for ancestor transparency
  // layers (needed in case we avoid creating a layer below).
  float paint_alpha = group.alpha / effective_opacity;
  DCHECK_LE(paint_alpha, 1.0f);

  // For the last (bottom) group, we can skip the layer even in the presence of
  // opacity iff it contains no adjecent edges (no in-group overdraw
  // possibility).
  bool needs_layer =
      group.alpha != 1.0f && (IncludesAdjacentEdges(group.edge_flags) ||
                              (index + 1 < border_info.opacity_groups.size()));

  if (needs_layer) {
    DCHECK_LT(group.alpha, effective_opacity);

    context_.BeginLayer(group.alpha / effective_opacity);
    effective_opacity = group.alpha;

    // Group opacity is applied via a layer => we draw the members using opaque
    // paint.
    paint_alpha = 1.0f;
  }

  // Recursion may seem unpalatable here, but
  //   a) it has an upper bound of 4
  //   b) only triggers at all when mixing border sides with different opacities
  //   c) it allows us to express the layer nesting algorithm more naturally
  BorderEdgeFlags completed_edges =
      PaintOpacityGroup(border_info, index + 1, effective_opacity);

  // Paint the actual group edges with an alpha adjusted to account for
  // ancenstor layers opacity.
  for (BoxSide side : group.sides) {
    PaintSide(border_info, side, paint_alpha, completed_edges);
    completed_edges |= EdgeFlagForSide(side);
  }

  if (needs_layer)
    context_.EndLayer();

  return completed_edges;
}

void BoxBorderPainter::PaintSide(const ComplexBorderInfo& border_info,
                                 BoxSide side,
                                 float alpha,
                                 BorderEdgeFlags completed_edges) const {
  const BorderEdge& edge = Edge(side);
  DCHECK(edge.ShouldRender());
  const Color color = Color::FromColorSpace(
      edge.GetColor().GetColorSpace(), edge.GetColor().Param0(),
      edge.GetColor().Param1(), edge.GetColor().Param2(), alpha);

  gfx::Rect side_rect = gfx::ToRoundedRect(outer_.Rect());

  // TODO(fmalita): find a way to consolidate these without sacrificing
  // readability.
  switch (side) {
    case BoxSide::kTop: {
      const bool is_curved =
          is_rounded_ && (BorderStyleHasInnerDetail(edge.BorderStyle()) ||
                          !inner_.HasRoundCurvature() ||
                          BorderWillArcInnerEdge(inner_.GetRadii().TopLeft(),
                                                 inner_.GetRadii().TopRight()));
      if (!is_curved) {
        side_rect.set_height(edge.Width());
      }

      const SideType side_type = is_curved ? kCurved : kStraight;
      PaintOneBorderSide(side_rect, BoxSide::kTop, BoxSide::kLeft,
                         BoxSide::kRight, side_type, color, completed_edges);
      break;
    }
    case BoxSide::kBottom: {
      const bool is_curved =
          is_rounded_ &&
          (BorderStyleHasInnerDetail(edge.BorderStyle()) ||
           !inner_.HasRoundCurvature() ||
           BorderWillArcInnerEdge(inner_.GetRadii().BottomLeft(),
                                  inner_.GetRadii().BottomRight()));
      if (!is_curved) {
        SetToBottomSideRect(side_rect, edge.Width());
      }

      const SideType side_type = is_curved ? kCurved : kStraight;
      PaintOneBorderSide(side_rect, BoxSide::kBottom, BoxSide::kLeft,
                         BoxSide::kRight, side_type, color, completed_edges);
      break;
    }
    case BoxSide::kLeft: {
      const bool is_curved =
          is_rounded_ && (BorderStyleHasInnerDetail(edge.BorderStyle()) ||
                          !inner_.HasRoundCurvature() ||
                          BorderWillArcInnerEdge(inner_.GetRadii().BottomLeft(),
                                                 inner_.GetRadii().TopLeft()));
      if (!is_curved) {
        side_rect.set_width(edge.Width());
      }

      const SideType side_type = is_curved ? kCurved : kStraight;
      PaintOneBorderSide(side_rect, BoxSide::kLeft, BoxSide::kTop,
                         BoxSide::kBottom, side_type, color, completed_edges);
      break;
    }
    case BoxSide::kRight: {
      const bool is_curved =
          is_rounded_ &&
          (BorderStyleHasInnerDetail(edge.BorderStyle()) ||
           !inner_.HasRoundCurvature() ||
           BorderWillArcInnerEdge(inner_.GetRadii().BottomRight(),
                                  inner_.GetRadii().TopRight()));
      if (!is_curved) {
        SetToRightSideRect(side_rect, edge.Width());
      }

      const SideType side_type = is_curved ? kCurved : kStraight;
      PaintOneBorderSide(side_rect, BoxSide::kRight, BoxSide::kTop,
                         BoxSide::kBottom, side_type, color, completed_edges);
      break;
    }
    default:
      NOTREACHED();
  }
}

BoxBorderPainter::MiterType BoxBorderPainter::ComputeMiter(
    BoxSide side,
    BoxSide adjacent_side,
    BorderEdgeFlags completed_edges) const {
  const BorderEdge& adjacent_edge = Edge(adjacent_side);

  // No miters for missing edges.
  if (!adjacent_edge.UsedWidth()) {
    return kNoMiter;
  }

  // The adjacent edge will overdraw this corner, resulting in a correct miter.
  if (WillOverdraw(adjacent_side, adjacent_edge.BorderStyle(), completed_edges))
    return kNoMiter;

  // Color transitions require miters. Use miters compatible with the AA drawing
  // mode to avoid introducing extra clips.
  if (!ColorsMatchAtCorner(side, adjacent_side))
    return kSoftMiter;

  // Non-anti-aliased miters ensure correct same-color seaming when required by
  // style.
  if (BorderStylesRequireMiter(side, adjacent_side, Edge(side).BorderStyle(),
                               adjacent_edge.BorderStyle()))
    return kHardMiter;

  // Overdraw the adjacent edge when the colors match and we have no style
  // restrictions.
  return kNoMiter;
}

bool BoxBorderPainter::MitersRequireClipping(MiterType miter1,
                                             MiterType miter2,
                                             EBorderStyle style) {
  // Clipping is required if any of the present miters doesn't match the current
  // AA mode.
  bool should_clip = miter1 == kHardMiter || miter2 == kHardMiter;

  // Some styles require clipping for any type of miter.
  should_clip = should_clip || ((miter1 != kNoMiter || miter2 != kNoMiter) &&
                                StyleRequiresClipPolygon(style));

  return should_clip;
}

void BoxBorderPainter::PaintOneBorderSide(
    const gfx::Rect& side_rect,
    BoxSide side,
    BoxSide adjacent_side1,
    BoxSide adjacent_side2,
    SideType side_type,
    Color color,
    BorderEdgeFlags completed_edges) const {
  const BorderEdge& edge_to_render = Edge(side);
  DCHECK(edge_to_render.Width());
  const BorderEdge& adjacent_edge1 = Edge(adjacent_side1);
  const BorderEdge& adjacent_edge2 = Edge(adjacent_side2);

  if (side_type == kCurved) {
    MiterType miter1 =
        ColorsMatchAtCorner(side, adjacent_side1) ? kHardMiter : kSoftMiter;
    MiterType miter2 =
        ColorsMatchAtCorner(side, adjacent_side2) ? kHardMiter : kSoftMiter;

    GraphicsContextStateSaver state_saver(context_);

    ClipBorderSidePolygon(side, miter1, miter2);
    if (!inner_.IsRenderable()) {
      ContouredRect adjusted_inner_rect =
          CalculateAdjustedInnerBorder(inner_, side);
      if (!adjusted_inner_rect.IsEmpty()) {
        context_.ClipOutContouredRect(adjusted_inner_rect);
      }
    }

    int stroke_thickness =
        std::max(std::max(edge_to_render.Width(), adjacent_edge1.Width()),
                 adjacent_edge2.Width());
    DrawCurvedBoxSide(edge_to_render.Width(), stroke_thickness, side, color,
                      edge_to_render.BorderStyle());
  } else {
    MiterType miter1 = ComputeMiter(side, adjacent_side1, completed_edges);
    MiterType miter2 = ComputeMiter(side, adjacent_side2, completed_edges);
    bool should_clip =
        MitersRequireClipping(miter1, miter2, edge_to_render.BorderStyle());

    GraphicsContextStateSaver clip_state_saver(context_, should_clip);
    if (should_clip) {
      ClipBorderSidePolygon(side, miter1, miter2);
      // Miters are applied via clipping, no need to draw them.
      miter1 = miter2 = kNoMiter;
    }

    DrawLineForBoxSide(context_, side_rect.x(), side_rect.y(),
                       side_rect.right(), side_rect.bottom(), side, color,
                       edge_to_render.BorderStyle(),
                       miter1 != kNoMiter ? adjacent_edge1.Width() : 0,
                       miter2 != kNoMiter ? adjacent_edge2.Width() : 0,
                       PaintAutoDarkMode(style_, element_role_));
  }
}

void BoxBorderPainter::DrawCurvedBoxSide(int border_thickness,
                                         int stroke_thickness,
                                         BoxSide side,
                                         Color color,
                                         EBorderStyle border_style) const {
  if (border_thickness <= 0)
    return;

  // The caller should have adjusted border_style.
  DCHECK_EQ(border_style,
            BorderEdge::EffectiveStyle(border_style, border_thickness));

  switch (border_style) {
    case EBorderStyle::kDotted:
    case EBorderStyle::kDashed:
      DrawCurvedDashedDottedBoxSide(border_thickness, stroke_thickness, color,
                                    border_style);
      return;
    case EBorderStyle::kDouble:
      DrawCurvedDoubleBoxSide(color);
      return;
    case EBorderStyle::kRidge:
    case EBorderStyle::kGroove:
      DrawCurvedRidgeGrooveBoxSide(side, color, border_style);
      return;
    case EBorderStyle::kInset:
    case EBorderStyle::kOutset:
      color =
          CalculateInsetOutsetColor(DarkenBoxSide(side, border_style), color);
      [[fallthrough]];
    case EBorderStyle::kSolid:
      break;
    case EBorderStyle::kNone:
    case EBorderStyle::kHidden:
      NOTREACHED();
  }

  context_.SetFillColor(color);
  context_.FillRect(gfx::ToRoundedRect(outer_.Rect()),
                    PaintAutoDarkMode(style_, element_role_));
}

void BoxBorderPainter::DrawCurvedDashedDottedBoxSide(
    int border_thickness,
    int stroke_thickness,
    Color color,
    EBorderStyle border_style) const {
  // Convert the path to be down the middle of the dots or dashes.
  const Path centerline_path =
      ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
          style_, border_rect_, CenterOutsets(), sides_to_include_)
          .GetPath();
  StyledStrokeData styled_stroke;
  styled_stroke.SetStyle(border_style == EBorderStyle::kDashed ? kDashedStroke
                                                               : kDottedStroke);
  if (!StyledStrokeData::StrokeIsDashed(border_thickness,
                                        styled_stroke.Style())) {
    styled_stroke.SetThickness(border_thickness);
  } else {
    // The stroke is doubled here because the provided path is the outside edge
    // of the border so half the stroke is clipped off, with the extra
    // multiplier so that the clipping mask can antialias the edges to prevent
    // jaggies.
    static constexpr float kThicknessMultiplier = 2 * 1.1f;
    styled_stroke.SetThickness(stroke_thickness * kThicknessMultiplier);
  }

  // TODO: Stroking the border path causes issues with tight corners
  // (crbug.com/41089993).
  const StrokeData stroke_data = styled_stroke.ConvertToStrokeData(
      {static_cast<int>(centerline_path.length()), border_thickness,
       centerline_path.IsClosed()});
  context_.SetStroke(stroke_data);
  context_.SetStrokeColor(color);
  context_.StrokePath(centerline_path,
                      PaintAutoDarkMode(style_, element_role_));
}

void BoxBorderPainter::DrawCurvedDoubleBoxSide(Color color) const {
  const AutoDarkMode auto_dark_mode = PaintAutoDarkMode(style_, element_role_);
  const gfx::Rect rect = gfx::ToRoundedRect(outer_.Rect());

  // Draw inner border line
  {
    GraphicsContextStateSaver state_saver(context_);
    const PhysicalBoxStrut inner_outsets =
        DoubleStripeOutsets(BorderEdge::kDoubleBorderStripeInner);
    ContouredRect inner_clip =
        ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
            style_, border_rect_, inner_outsets, sides_to_include_);
    ClipContouredRect(inner_clip);
    context_.SetFillColor(color);
    context_.FillRect(rect, auto_dark_mode);
  }

  // Draw outer border line
  {
    GraphicsContextStateSaver state_saver(context_);
    PhysicalRect used_border_rect = border_rect_;
    PhysicalBoxStrut outer_outsets =
        DoubleStripeOutsets(BorderEdge::kDoubleBorderStripeOuter);

    if (BleedAvoidanceIsClipping(bleed_avoidance_)) {
      used_border_rect.Inflate(LayoutUnit(1));
      outer_outsets.Inflate(LayoutUnit(-1));
    }

    ContouredRect outer_clip =
        ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
            style_, used_border_rect, outer_outsets, sides_to_include_);
    ClipOutContouredRect(outer_clip);
    context_.SetFillColor(color);
    context_.FillRect(rect, auto_dark_mode);
  }
}

void BoxBorderPainter::DrawCurvedRidgeGrooveBoxSide(
    BoxSide side,
    Color color,
    EBorderStyle border_style) const {
  EBorderStyle s1;
  if (border_style == EBorderStyle::kGroove) {
    s1 = EBorderStyle::kInset;
  } else {
    s1 = EBorderStyle::kOutset;
  }

  const bool darken_s1 = DarkenBoxSide(side, s1);
  const AutoDarkMode auto_dark_mode = PaintAutoDarkMode(style_, element_role_);
  const gfx::Rect rect = gfx::ToRoundedRect(outer_.Rect());

  // Paint full border
  context_.SetFillColor(CalculateInsetOutsetColor(darken_s1, color));
  context_.FillRect(rect, auto_dark_mode);

  // Paint inner only
  GraphicsContextStateSaver state_saver(context_);
  ContouredRect clip_rect =
      ContouredBorderGeometry::PixelSnappedContouredBorderWithOutsets(
          style_, border_rect_, CenterOutsets(), sides_to_include_);

  ClipContouredRect(clip_rect);
  context_.SetFillColor(CalculateInsetOutsetColor(!darken_s1, color));
  context_.FillRect(rect, auto_dark_mode);
}

gfx::Rect BoxBorderPainter::CalculateSideRectIncludingInner(
    BoxSide side) const {
  gfx::Rect side_rect = gfx::ToRoundedRect(outer_.Rect());
  int width;

  switch (side) {
    case BoxSide::kTop:
      width = side_rect.height() - Edge(BoxSide::kBottom).Width();
      side_rect.set_height(width);
      break;
    case BoxSide::kBottom:
      width = side_rect.height() - Edge(BoxSide::kTop).Width();
      SetToBottomSideRect(side_rect, width);
      break;
    case BoxSide::kLeft:
      width = side_rect.width() - Edge(BoxSide::kRight).Width();
      side_rect.set_width(width);
      break;
    case BoxSide::kRight:
      width = side_rect.width() - Edge(BoxSide::kLeft).Width();
      SetToRightSideRect(side_rect, width);
      break;
  }

  return side_rect;
}

// This algorithm clips as follows:
// The path of the side, including the full two corners, is clipped first, to
// avoid including overlapping opposite corners. Then, each of the half corners
// that should be excluded because of the miter is clipped out. If the corners
// overlap each other, this might leave an ambiguous area, not explicitly part
// of any side. By clipping out areas that are definitely part of the adjacent
// side, those ambiguous areas would be part of both sides.
void BoxBorderPainter::ClipBorderSidePolygonCloseToEdges(
    BoxSide side,
    MiterType first_miter,
    MiterType second_miter) const {
  const AntiAliasingMode antialias_top_or_left =
      first_miter == MiterType::kSoftMiter ? kAntiAliased : kNotAntiAliased;
  const AntiAliasingMode antialias_right_or_bottom =
      second_miter == MiterType::kSoftMiter ? kAntiAliased : kNotAntiAliased;
  const CornerInfo top_left_corner_info{
      .outer = outer_.TopLeftCorner(),
      .inner = inner_.TopLeftCorner(),
      .unadjusted_inner_edge = inner_.Rect().origin()};

  const CornerInfo top_right_corner_info{
      .outer = outer_.TopRightCorner(),
      .inner = inner_.TopRightCorner(),
      .unadjusted_inner_edge = inner_.Rect().top_right()};

  const CornerInfo bottom_right_corner_info{
      .outer = outer_.BottomRightCorner(),
      .inner = inner_.BottomRightCorner(),
      .unadjusted_inner_edge = inner_.Rect().bottom_right()};

  const CornerInfo bottom_left_corner_info{
      .outer = outer_.BottomLeftCorner(),
      .inner = inner_.BottomLeftCorner(),
      .unadjusted_inner_edge = inner_.Rect().bottom_left()};

  const EBorderStyle border_style = Edge(side).BorderStyle();
  const bool needs_miters = !is_uniform_color_ || !is_uniform_style_ ||
                            border_style == EBorderStyle::kGroove ||
                            border_style == EBorderStyle::kRidge;

  switch (side) {
    case BoxSide::kTop:
      ClipBorderSidePolygonFromCorners(
          context_,
          {top_left_corner_info, top_right_corner_info,
           bottom_right_corner_info, bottom_left_corner_info},
          antialias_top_or_left, antialias_right_or_bottom,
          gfx::Vector2dF(0, inner_.Rect().y() - outer_.Rect().y()),
          needs_miters);
      break;
    case BoxSide::kRight:
      ClipBorderSidePolygonFromCorners(
          context_,
          {top_right_corner_info, bottom_right_corner_info,
           bottom_left_corner_info, top_left_corner_info},
          antialias_top_or_left, antialias_right_or_bottom,
          gfx::Vector2dF(inner_.Rect().right() - outer_.Rect().right(), 0),
          needs_miters);
      break;
    case BoxSide::kBottom:
      ClipBorderSidePolygonFromCorners(
          context_,
          {bottom_right_corner_info, bottom_left_corner_info,
           top_left_corner_info, top_right_corner_info},
          antialias_right_or_bottom, antialias_top_or_left,
          gfx::Vector2dF(0, inner_.Rect().bottom() - outer_.Rect().bottom()),
          needs_miters);
      break;
    case BoxSide::kLeft:
      ClipBorderSidePolygonFromCorners(
          context_,
          {bottom_left_corner_info, top_left_corner_info, top_right_corner_info,
           bottom_right_corner_info},
          antialias_right_or_bottom, antialias_top_or_left,
          gfx::Vector2dF(inner_.Rect().x() - outer_.Rect().x(), 0),
          needs_miters);
      break;
  }
}

void BoxBorderPainter::ClipBorderSidePolygon(BoxSide side,
                                             MiterType first_miter,
                                             MiterType second_miter) const {
  DCHECK(first_miter != kNoMiter || second_miter != kNoMiter);
  if (is_rounded_ && !outer_.GetCornerCurvature().IsHyperellipse()) {
    ClipBorderSidePolygonCloseToEdges(side, first_miter, second_miter);
    return;
  }

  // The boundary of the edge for fill.
  gfx::PointF edge_quad[4];
  Vector<gfx::PointF, 5> edge_pentagon;

  // Point 1 of the rectilinear bounding box of edge_quad.
  gfx::PointF bound_quad1;
  // Point 2 of the rectilinear bounding box of edge_quad.
  gfx::PointF bound_quad2;

  // For each side, create a quad that encompasses all parts of that side that
  // may draw, including areas inside the innerBorder.
  //
  //         0----------------3
  //       3  \              /  0
  //       |\  1----------- 2  /|
  //       | 2                1 |
  //       | |                | |
  //       | |                | |
  //       | 1                2 |
  //       |/  2------------1  \|
  //       0  /              \  3
  //         3----------------0

  // Points 1 and 2 of each quad are initially the corresponding corners of the
  // inner rect. If an inner corner is rounded, the corresponding point will be
  // moved inside to ensure the quad contains the half corner.
  // However, if the inner border is not renderable, and line 1-2 would clip the
  // rounded corner near the miter, we need to insert a point between 1 and 2 to
  // create a pentagon.
  // 0-------------3       0-------------3       0-------------4
  // |\           /|       |\           /|       |\           /|
  // | 1---------2 |       | \---------2 |       | \---------3 |
  // | |         | |       | |\       /| |       | |\        | |
  // | |         | |       | | \     / | |       | | \       | |
  // | |         | |  -->  | |  \   /  | |  -->  | |  \      | |
  // | |         | |       | |    1    | |       | |    1----2 |
  // | |         | |       | |         | |       | |         | |
  // | /---------\ |       | /---------\ |       | /---------\ |
  //  -------------         -------------         -------------
  //
  // For concave corners, point 2 in the quad (or point 3 in the pentagon) is
  // adjusted to the hull of the corner superellipse. This ensures that the
  // entire concave border is within the clip, while not clipping in other
  // borders.

  const gfx::PointF inner_points[4] = {
      inner_.Rect().origin(),
      inner_.Rect().top_right(),
      inner_.Rect().bottom_right(),
      inner_.Rect().bottom_left(),
  };
  const gfx::PointF outer_points[4] = {
      outer_.Rect().origin(),
      outer_.Rect().top_right(),
      outer_.Rect().bottom_right(),
      outer_.Rect().bottom_left(),
  };

  // Offset size and direction to expand clipping quad
  const static float kExtensionLength = 1e-1f;
  gfx::Vector2dF extension_offset;

  switch (side) {
    case BoxSide::kTop:
      edge_quad[0] = outer_points[0];
      edge_quad[1] = inner_points[0];
      edge_quad[2] = inner_points[1];
      edge_quad[3] = outer_points[1];

      DCHECK(edge_quad[0].y() == edge_quad[3].y());
      DCHECK(edge_quad[1].y() == edge_quad[2].y());

      bound_quad1 = gfx::PointF(edge_quad[0].x(), edge_quad[1].y());
      bound_quad2 = gfx::PointF(edge_quad[3].x(), edge_quad[2].y());

      extension_offset.set_x(-kExtensionLength);
      extension_offset.set_y(0);

      if (!inner_.GetRadii().TopLeft().IsZero()) {
          FindIntersection(
              edge_quad[0], edge_quad[1],
              gfx::PointF(
                  edge_quad[1].x() + inner_.GetRadii().TopLeft().width(),
                  edge_quad[1].y()),
              gfx::PointF(
                  edge_quad[1].x(),
                  edge_quad[1].y() + inner_.GetRadii().TopLeft().height()),
              edge_quad[1]);
        DCHECK(bound_quad1.y() <= edge_quad[1].y());
        bound_quad1.set_y(edge_quad[1].y());
        bound_quad2.set_y(edge_quad[1].y());

        if (edge_quad[1].y() > inner_points[2].y()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[3],
                           inner_points[2], edge_quad[1]);
        }
        if (edge_quad[1].x() > inner_points[2].x()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[1],
                           inner_points[2], edge_quad[1]);
        }
        if (edge_quad[2].y() < edge_quad[1].y() &&
            edge_quad[2].x() > edge_quad[1].x()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[2].x(), edge_quad[1].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }

      if (!inner_.GetRadii().TopRight().IsZero()) {
        FindIntersection(
            edge_quad[3], edge_quad[2],
            gfx::PointF(edge_quad[2].x() - inner_.GetRadii().TopRight().width(),
                        edge_quad[2].y()),
            gfx::PointF(
                edge_quad[2].x(),
                edge_quad[2].y() + inner_.GetRadii().TopRight().height()),
            edge_quad[2]);
        if (bound_quad1.y() < edge_quad[2].y()) {
          bound_quad1.set_y(edge_quad[2].y());
          bound_quad2.set_y(edge_quad[2].y());
        }

        if (edge_quad[2].y() > inner_points[3].y()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[3],
                           inner_points[2], edge_quad[2]);
        }
        if (edge_quad[2].x() < inner_points[3].x()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[0],
                           inner_points[3], edge_quad[2]);
        }
        if (edge_quad[2].y() > edge_quad[1].y() &&
            edge_quad[2].x() > edge_quad[1].x()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[1].x(), edge_quad[2].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }
      break;

    case BoxSide::kLeft:
      // Swap the order of adjacent edges to allow common code
      std::swap(first_miter, second_miter);
      edge_quad[0] = outer_points[3];
      edge_quad[1] = inner_points[3];
      edge_quad[2] = inner_points[0];
      edge_quad[3] = outer_points[0];

      DCHECK(edge_quad[0].x() == edge_quad[3].x());
      DCHECK(edge_quad[1].x() == edge_quad[2].x());

      bound_quad1 = gfx::PointF(edge_quad[1].x(), edge_quad[0].y());
      bound_quad2 = gfx::PointF(edge_quad[2].x(), edge_quad[3].y());

      extension_offset.set_x(0);
      extension_offset.set_y(kExtensionLength);

      if (!inner_.GetRadii().TopLeft().IsZero()) {
        FindIntersection(
            edge_quad[3], edge_quad[2],
            gfx::PointF(edge_quad[2].x() + inner_.GetRadii().TopLeft().width(),
                        edge_quad[2].y()),
            gfx::PointF(
                edge_quad[2].x(),
                edge_quad[2].y() + inner_.GetRadii().TopLeft().height()),
            edge_quad[2]);

        DCHECK(bound_quad2.x() <= edge_quad[2].x());
        bound_quad1.set_x(edge_quad[2].x());
        bound_quad2.set_x(edge_quad[2].x());

        if (edge_quad[2].y() > inner_points[2].y()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[3],
                           inner_points[2], edge_quad[2]);
        }
        if (edge_quad[2].x() > inner_points[2].x()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[1],
                           inner_points[2], edge_quad[2]);
        }
        if (edge_quad[2].y() < edge_quad[1].y() &&
            edge_quad[2].x() > edge_quad[1].x()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[2].x(), edge_quad[1].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }

      if (!inner_.GetRadii().BottomLeft().IsZero()) {
        FindIntersection(
            edge_quad[0], edge_quad[1],
            gfx::PointF(
                edge_quad[1].x() + inner_.GetRadii().BottomLeft().width(),
                edge_quad[1].y()),
            gfx::PointF(
                edge_quad[1].x(),
                edge_quad[1].y() - inner_.GetRadii().BottomLeft().height()),
            edge_quad[1]);
        if (bound_quad1.x() < edge_quad[1].x()) {
          bound_quad1.set_x(edge_quad[1].x());
          bound_quad2.set_x(edge_quad[1].x());
        }

        if (edge_quad[1].y() < inner_points[1].y()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[0],
                           inner_points[1], edge_quad[1]);
        }
        if (edge_quad[1].x() > inner_points[1].x()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[1],
                           inner_points[2], edge_quad[1]);
        }
        if (edge_quad[2].y() < edge_quad[1].y() &&
            edge_quad[2].x() < edge_quad[1].x()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[1].x(), edge_quad[2].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }
      break;

    case BoxSide::kBottom:
      // Swap the order of adjacent edges to allow common code
      std::swap(first_miter, second_miter);
      edge_quad[0] = outer_points[2];
      edge_quad[1] = inner_points[2];
      edge_quad[2] = inner_points[3];
      edge_quad[3] = outer_points[3];

      DCHECK(edge_quad[0].y() == edge_quad[3].y());
      DCHECK(edge_quad[1].y() == edge_quad[2].y());

      bound_quad1 = gfx::PointF(edge_quad[0].x(), edge_quad[1].y());
      bound_quad2 = gfx::PointF(edge_quad[3].x(), edge_quad[2].y());

      extension_offset.set_x(kExtensionLength);
      extension_offset.set_y(0);

      if (!inner_.GetRadii().BottomLeft().IsZero()) {
          FindIntersection(
              edge_quad[3], edge_quad[2],
              gfx::PointF(
                  edge_quad[2].x() + inner_.GetRadii().BottomLeft().width(),
                  edge_quad[2].y()),
              gfx::PointF(
                  edge_quad[2].x(),
                  edge_quad[2].y() - inner_.GetRadii().BottomLeft().height()),
              edge_quad[2]);
        DCHECK(bound_quad2.y() >= edge_quad[2].y());
        bound_quad1.set_y(edge_quad[2].y());
        bound_quad2.set_y(edge_quad[2].y());

        if (edge_quad[2].y() < inner_points[1].y()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[0],
                           inner_points[1], edge_quad[2]);
        }
        if (edge_quad[2].x() > inner_points[1].x()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[1],
                           inner_points[2], edge_quad[2]);
        }
        if (edge_quad[2].y() < edge_quad[1].y() &&
            edge_quad[2].x() < edge_quad[1].x()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[1].x(), edge_quad[2].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }

      if (!inner_.GetRadii().BottomRight().IsZero()) {
        FindIntersection(
            edge_quad[0], edge_quad[1],
            gfx::PointF(
                edge_quad[1].x() - inner_.GetRadii().BottomRight().width(),
                edge_quad[1].y()),
            gfx::PointF(
                edge_quad[1].x(),
                edge_quad[1].y() - inner_.GetRadii().BottomRight().height()),
            edge_quad[1]);
        if (bound_quad1.y() > edge_quad[1].y()) {
          bound_quad1.set_y(edge_quad[1].y());
          bound_quad2.set_y(edge_quad[1].y());
        }

        if (edge_quad[1].y() < inner_points[0].y()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[0],
                           inner_points[1], edge_quad[1]);
        }
        if (edge_quad[1].x() < inner_points[0].x()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[0],
                           inner_points[3], edge_quad[1]);
        }
        if (edge_quad[2].x() < edge_quad[1].x() &&
            edge_quad[2].y() > edge_quad[1].y()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[2].x(), edge_quad[1].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }
      break;

    case BoxSide::kRight:
      edge_quad[0] = outer_points[1];
      edge_quad[1] = inner_points[1];
      edge_quad[2] = inner_points[2];
      edge_quad[3] = outer_points[2];

      DCHECK(edge_quad[0].x() == edge_quad[3].x());
      DCHECK(edge_quad[1].x() == edge_quad[2].x());

      bound_quad1 = gfx::PointF(edge_quad[1].x(), edge_quad[0].y());
      bound_quad2 = gfx::PointF(edge_quad[2].x(), edge_quad[3].y());

      extension_offset.set_x(0);
      extension_offset.set_y(-kExtensionLength);

      if (!inner_.GetRadii().TopRight().IsZero()) {
          FindIntersection(
              edge_quad[0], edge_quad[1],
              gfx::PointF(
                  edge_quad[1].x() - inner_.GetRadii().TopRight().width(),
                  edge_quad[1].y()),
              gfx::PointF(
                  edge_quad[1].x(),
                  edge_quad[1].y() + inner_.GetRadii().TopRight().height()),
              edge_quad[1]);
        DCHECK(bound_quad1.x() >= edge_quad[1].x());
        bound_quad1.set_x(edge_quad[1].x());
        bound_quad2.set_x(edge_quad[1].x());

        if (edge_quad[1].y() > inner_points[3].y()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[3],
                           inner_points[2], edge_quad[1]);
        }
        if (edge_quad[1].x() < inner_points[3].x()) {
          FindIntersection(edge_quad[0], edge_quad[1], inner_points[0],
                           inner_points[3], edge_quad[1]);
        }
        if (edge_quad[2].y() > edge_quad[1].y() &&
            edge_quad[2].x() > edge_quad[1].x()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[1].x(), edge_quad[2].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }

      if (!inner_.GetRadii().BottomRight().IsZero()) {
        FindIntersection(
            edge_quad[3], edge_quad[2],
            gfx::PointF(
                edge_quad[2].x() - inner_.GetRadii().BottomRight().width(),
                edge_quad[2].y()),
            gfx::PointF(
                edge_quad[2].x(),
                edge_quad[2].y() - inner_.GetRadii().BottomRight().height()),
            edge_quad[2]);
        if (bound_quad1.x() > edge_quad[2].x()) {
          bound_quad1.set_x(edge_quad[2].x());
          bound_quad2.set_x(edge_quad[2].x());
        }

        if (edge_quad[2].y() < inner_points[0].y()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[0],
                           inner_points[1], edge_quad[2]);
        }
        if (edge_quad[2].x() < inner_points[0].x()) {
          FindIntersection(edge_quad[3], edge_quad[2], inner_points[0],
                           inner_points[3], edge_quad[2]);
        }
        if (edge_quad[2].x() < edge_quad[1].x() &&
            edge_quad[2].y() > edge_quad[1].y()) {
          edge_pentagon = {edge_quad[0], edge_quad[1],
                           gfx::PointF(edge_quad[2].x(), edge_quad[1].y()),
                           edge_quad[2], edge_quad[3]};
        }
      }
      break;
  }

  if (first_miter == second_miter) {
    if (!edge_pentagon.empty() && !inner_.IsRenderable()) {
      DCHECK_EQ(edge_pentagon.size(), 5u);

      ClipPolygon(context_, edge_pentagon, first_miter == kSoftMiter);
      return;
    }

    ClipPolygon(context_, edge_quad, first_miter == kSoftMiter);
    return;
  }

  // If antialiasing settings for the first edge and second edge are different,
  // they have to be addressed separately. We do this by applying 2 clips, one
  // for each miter, with the appropriate anti-aliasing setting. Each clip uses
  // 3 sides of the quad rectilinear bounding box and a 4th side aligned with
  // the miter edge. We extend the clip in the miter direction to ensure overlap
  // as each edge is drawn.
  if (first_miter != kNoMiter) {
    gfx::PointF clipping_quad[4];

    clipping_quad[0] = edge_quad[0] + extension_offset;
    FindIntersection(edge_quad[0], edge_quad[1], bound_quad1, bound_quad2,
                     clipping_quad[1]);
    clipping_quad[1] += extension_offset;
    clipping_quad[2] = bound_quad2;
    clipping_quad[3] = edge_quad[3];

    ClipPolygon(context_, clipping_quad, first_miter == kSoftMiter);
  }

  if (second_miter != kNoMiter) {
    gfx::PointF clipping_quad[4];

    clipping_quad[0] = edge_quad[0];
    clipping_quad[1] = bound_quad1;
    FindIntersection(edge_quad[2], edge_quad[3], bound_quad1, bound_quad2,
                     clipping_quad[2]);
    clipping_quad[2] -= extension_offset;
    clipping_quad[3] = edge_quad[3] - extension_offset;

    ClipPolygon(context_, clipping_quad, second_miter == kSoftMiter);
  }
}

PhysicalBoxStrut BoxBorderPainter::DoubleStripeOutsets(
    BorderEdge::DoubleBorderStripe stripe) const {
  return outer_outsets_ -
         PhysicalBoxStrut(
             Edge(BoxSide::kTop).GetDoubleBorderStripeWidth(stripe),
             Edge(BoxSide::kRight).GetDoubleBorderStripeWidth(stripe),
             Edge(BoxSide::kBottom).GetDoubleBorderStripeWidth(stripe),
             Edge(BoxSide::kLeft).GetDoubleBorderStripeWidth(stripe));
}

PhysicalBoxStrut BoxBorderPainter::CenterOutsets() const {
  return outer_outsets_ -
         PhysicalBoxStrut(Edge(BoxSide::kTop).UsedWidth() * 0.5,
                          Edge(BoxSide::kRight).UsedWidth() * 0.5,
                          Edge(BoxSide::kBottom).UsedWidth() * 0.5,
                          Edge(BoxSide::kLeft).UsedWidth() * 0.5);
}

bool BoxBorderPainter::ColorsMatchAtCorner(BoxSide side,
                                           BoxSide adjacent_side) const {
  if (!Edge(adjacent_side).ShouldRender())
    return false;

  if (!Edge(side).SharesColorWith(Edge(adjacent_side)))
    return false;

  return !BorderStyleHasUnmatchedColorsAtCorner(Edge(side).BorderStyle(), side,
                                                adjacent_side);
}

void BoxBorderPainter::DrawBoxSide(GraphicsContext& context,
                                   const gfx::Rect& snapped_edge_rect,
                                   BoxSide side,
                                   Color color,
                                   EBorderStyle style,
                                   const AutoDarkMode& auto_dark_mode) {
  if (style == EBorderStyle::kNone || style == EBorderStyle::kHidden) {
    return;
  }
  DrawLineForBoxSide(context, snapped_edge_rect.x(), snapped_edge_rect.y(),
                     snapped_edge_rect.right(), snapped_edge_rect.bottom(),
                     side, color, style, 0, 0, auto_dark_mode);
}

}  // namespace blink
