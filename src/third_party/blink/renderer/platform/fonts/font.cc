/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2006, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (c) 2007, 2008, 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "third_party/blink/renderer/platform/fonts/font.h"

#include "cc/paint/paint_flags.h"
#include "third_party/blink/renderer/platform/fonts/character_range.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_fallback_list.h"
#include "third_party/blink/renderer/platform/fonts/font_fallback_map.h"
#include "third_party/blink/renderer/platform/fonts/shaping/caching_word_shaper.h"
#include "third_party/blink/renderer/platform/fonts/shaping/harfbuzz_shaper.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_bloberizer.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_spacing.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_view.h"
#include "third_party/blink/renderer/platform/fonts/simple_font_data.h"
#include "third_party/blink/renderer/platform/fonts/text_fragment_paint_info.h"
#include "third_party/blink/renderer/platform/fonts/text_run_paint_info.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"
#include "third_party/blink/renderer/platform/text/bidi_paragraph.h"
#include "third_party/blink/renderer/platform/text/character.h"
#include "third_party/blink/renderer/platform/text/text_run.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/character_names.h"
#include "third_party/blink/renderer/platform/wtf/text/unicode.h"
#include "ui/gfx/geometry/rect_f.h"

namespace blink {

namespace {

FontFallbackList* GetOrCreateFontFallbackList(
    const FontDescription& font_description,
    FontSelector* font_selector) {
  FontFallbackMap& fallback_map = font_selector
                                      ? font_selector->GetFontFallbackMap()
                                      : FontCache::Get().GetFontFallbackMap();
  return fallback_map.Get(font_description);
}

}  // namespace

Font::Font() = default;

Font::Font(const FontDescription& fd) : font_description_(fd) {}

Font::Font(const FontDescription& font_description, FontSelector* font_selector)
    : font_description_(font_description),
      font_fallback_list_(
          font_selector
              ? GetOrCreateFontFallbackList(font_description, font_selector)
              : nullptr) {}

FontFallbackList* Font::EnsureFontFallbackList() const {
  if (!font_fallback_list_ || !font_fallback_list_->IsValid()) {
    font_fallback_list_ =
        GetOrCreateFontFallbackList(font_description_, GetFontSelector());
  }
  return font_fallback_list_.Get();
}

bool Font::operator==(const Font& other) const {
  // Font objects with the same FontDescription and FontSelector should always
  // hold reference to the same FontFallbackList object, unless invalidated.
  if (font_fallback_list_ && font_fallback_list_->IsValid() &&
      other.font_fallback_list_ && other.font_fallback_list_->IsValid()) {
    return font_fallback_list_ == other.font_fallback_list_;
  }

  return GetFontSelector() == other.GetFontSelector() &&
         font_description_ == other.font_description_;
}

void Font::DrawText(cc::PaintCanvas* canvas,
                    const TextFragmentPaintInfo& text_info,
                    const gfx::PointF& point,
                    cc::NodeId node_id,
                    const cc::PaintFlags& flags,
                    DrawType draw_type) const {
  // Don't draw anything while we are using custom fonts that are in the process
  // of loading.
  if (ShouldSkipDrawing())
    return;

  ShapeResultBloberizer::FillGlyphsNG bloberizer(
      GetFontDescription(), text_info.text, text_info.from, text_info.to,
      text_info.shape_result,
      draw_type == Font::DrawType::kGlyphsOnly
          ? ShapeResultBloberizer::Type::kNormal
          : ShapeResultBloberizer::Type::kEmitText);
  DrawTextBlobs(bloberizer.Blobs(), *canvas, point, flags, node_id);
}

bool Font::DeprecatedDrawBidiText(
    cc::PaintCanvas* canvas,
    const TextRunPaintInfo& run_info,
    const gfx::PointF& point,
    CustomFontNotReadyAction custom_font_not_ready_action,
    const cc::PaintFlags& flags,
    DrawType draw_type) const {
  // Don't draw anything while we are using custom fonts that are in the process
  // of loading, except if the 'force' argument is set to true (in which case it
  // will use a fallback font).
  if (ShouldSkipDrawing() &&
      custom_font_not_ready_action == kDoNotPaintIfFontNotReady)
    return false;

  const TextRun& run = run_info.run;
  if (!run.length()) {
    return true;
  }
  bool is_sub_run = (run_info.from != 0 || run_info.to != run.length());

  if (run.DirectionalOverride()) [[unlikely]] {
    // If directional override, create a new string with Unicode directional
    // override characters.
    const String text_with_override =
        BidiParagraph::StringWithDirectionalOverride(run.ToStringView(),
                                                     run.Direction());
    TextRun run_with_override(text_with_override, run.Direction(),
                              /* directional_override */ false,
                              run.NormalizeSpace());
    return DeprecatedDrawBidiText(canvas, TextRunPaintInfo(run_with_override),
                                  point, custom_font_not_ready_action, flags,
                                  draw_type);
  }

  BidiParagraph::Runs bidi_runs;
  if (run.Is8Bit() && IsLtr(run.Direction())) {
    // U+0000-00FF are L or neutral, it's unidirectional if 8 bits and LTR.
    bidi_runs.emplace_back(0, run.length(), 0);
  } else {
    String text = run.ToStringView().ToString();
    text.Ensure16Bit();
    BidiParagraph bidi(text, run.Direction());
    bidi.GetVisualRuns(text, &bidi_runs);
  }

  gfx::PointF curr_point = point;
  CachingWordShaper word_shaper(*this);
  for (const BidiParagraph::Run& bidi_run : bidi_runs) {
    if (bidi_run.end <= run_info.from || run_info.to <= bidi_run.start) {
      continue;
    }

    TextRun subrun =
        run.SubRun(bidi_run.start, bidi_run.Length(), bidi_run.Direction());
    TextRunPaintInfo subrun_info(subrun);
    CharacterRange range(0, 0, 0, 0);
    if (is_sub_run) [[unlikely]] {
      // Calculate the required indexes for this specific run.
      subrun_info.from =
          run_info.from < bidi_run.start ? 0 : run_info.from - bidi_run.start;
      subrun_info.to = run_info.to > bidi_run.end
                           ? bidi_run.Length()
                           : run_info.to - bidi_run.start;
      // The range provides information required for positioning the subrun.
      range = word_shaper.GetCharacterRange(subrun, subrun_info.from,
                                            subrun_info.to);
    }

    ShapeResultBuffer buffer;
    word_shaper.FillResultBuffer(subrun, &buffer);

    // Fix regression with -ftrivial-auto-var-init=pattern. See
    // crbug.com/1055652.
    STACK_UNINITIALIZED ShapeResultBloberizer::FillGlyphs bloberizer(
        GetFontDescription(), subrun_info, buffer,
        draw_type == Font::DrawType::kGlyphsOnly
            ? ShapeResultBloberizer::Type::kNormal
            : ShapeResultBloberizer::Type::kEmitText);
    if (is_sub_run) [[unlikely]] {
      // Align the subrun with the point given.
      curr_point.Offset(-range.start, 0);
    }
    DrawTextBlobs(bloberizer.Blobs(), *canvas, curr_point, flags);

    if (is_sub_run) [[unlikely]] {
      curr_point.Offset(range.Width(), 0);
    } else {
      curr_point.Offset(bloberizer.Advance(), 0);
    }
  }
  return true;
}

void Font::DrawEmphasisMarks(cc::PaintCanvas* canvas,
                             const TextFragmentPaintInfo& text_info,
                             const AtomicString& mark,
                             const gfx::PointF& point,
                             const cc::PaintFlags& flags) const {
  if (ShouldSkipDrawing())
    return;

  FontCachePurgePreventer purge_preventer;
  const auto emphasis_glyph_data = GetEmphasisMarkGlyphData(mark);
  if (!emphasis_glyph_data.font_data)
    return;

  ShapeResultBloberizer::FillTextEmphasisGlyphsNG bloberizer(
      GetFontDescription(), text_info.text, text_info.from, text_info.to,
      text_info.shape_result, emphasis_glyph_data);
  DrawTextBlobs(bloberizer.Blobs(), *canvas, point, flags);
}

gfx::RectF Font::TextInkBounds(const TextFragmentPaintInfo& text_info) const {
  // No need to compute bounds if using custom fonts that are in the process
  // of loading as it won't be painted.
  if (ShouldSkipDrawing())
    return gfx::RectF();

  // NOTE(eae): We could use the SkTextBlob::bounds API [1] however by default
  // it returns conservative bounds (rather than tight bounds) which are
  // unsuitable for our needs. If we could get the tight bounds from Skia that
  // would be quite a bit faster than the two-stage approach employed by the
  // ShapeResultView::ComputeInkBounds method.
  // 1: https://skia.org/user/api/SkTextBlob_Reference#SkTextBlob_bounds
  return text_info.shape_result->ComputeInkBounds();
}

float Font::DeprecatedWidth(const TextRun& run,
                            gfx::RectF* glyph_bounds) const {
  FontCachePurgePreventer purge_preventer;
  CachingWordShaper shaper(*this);
  return shaper.Width(run, glyph_bounds);
}

float Font::DeprecatedSubRunWidth(const TextRun& run,
                                  unsigned from,
                                  unsigned to,
                                  gfx::RectF* glyph_bounds) const {
  if (run.length() == 0) {
    return 0;
  }

  FontCachePurgePreventer purge_preventer;
  CachingWordShaper shaper(*this);

  // Run bidi algorithm on the given text. Step 5 of:
  // https://html.spec.whatwg.org/multipage/canvas.html#text-preparation-algorithm
  String text16 = run.ToStringView().ToString();
  text16.Ensure16Bit();
  BidiParagraph bidi;
  bidi.SetParagraph(text16, run.Direction());
  BidiParagraph::Runs runs;
  bidi.GetVisualRuns(text16, &runs);

  float x_pos = 0;
  for (const BidiParagraph::Run& visual_run : runs) {
    if (visual_run.end <= from || to <= visual_run.start) {
      continue;
    }
    // Calculate the required indexes for this specific run.
    unsigned run_from = from < visual_run.start ? 0 : from - visual_run.start;
    unsigned run_to =
        to > visual_run.end ? visual_run.Length() : to - visual_run.start;

    // Measure the subrun.
    TextRun text_run(
        StringView(run.ToStringView(), visual_run.start, visual_run.Length()),
        visual_run.Direction(), /* directional_override */ false,
        /* normalize_space */ true);
    CharacterRange character_range =
        shaper.GetCharacterRange(text_run, run_from, run_to);

    // Accumulate the position and the glyph bounding box.
    if (glyph_bounds) {
      gfx::RectF range_bounds(character_range.start, -character_range.ascent,
                              character_range.Width(),
                              character_range.Height());
      // GetCharacterRange() returns bounds positioned as if the whole run was
      // there, so the rect has to be moved to align with the current position.
      range_bounds.Offset(-range_bounds.x() + x_pos, 0);
      glyph_bounds->Union(range_bounds);
    }
    x_pos += character_range.Width();
  }
  if (glyph_bounds != nullptr) {
    glyph_bounds->Offset(-glyph_bounds->x(), 0);
  }
  return x_pos;
}

namespace {  // anonymous namespace

unsigned InterceptsFromBlobs(const ShapeResultBloberizer::BlobBuffer& blobs,
                             const SkPaint& paint,
                             const std::tuple<float, float>& bounds,
                             SkScalar* intercepts_buffer) {
  SkScalar bounds_array[2] = {std::get<0>(bounds), std::get<1>(bounds)};

  unsigned num_intervals = 0;
  for (const auto& blob_info : blobs) {
    DCHECK(blob_info.blob);

    // ShapeResultBloberizer splits for a new blob rotation, but does not split
    // for a change in font. A TextBlob can contain runs with differing fonts
    // and the getTextBlobIntercepts method handles multiple fonts for us. For
    // upright in vertical blobs we currently have to bail, see crbug.com/655154
    if (IsCanvasRotationInVerticalUpright(blob_info.rotation))
      continue;

    SkScalar* offset_intercepts_buffer = nullptr;
    if (intercepts_buffer)
      offset_intercepts_buffer = UNSAFE_TODO(&intercepts_buffer[num_intervals]);
    num_intervals += blob_info.blob->getIntercepts(
        bounds_array, offset_intercepts_buffer, &paint);
  }
  return num_intervals;
}

void GetTextInterceptsInternal(const ShapeResultBloberizer::BlobBuffer& blobs,
                               const cc::PaintFlags& flags,
                               const std::tuple<float, float>& bounds,
                               Vector<Font::TextIntercept>& intercepts) {
  // Get the number of intervals, without copying the actual values by
  // specifying nullptr for the buffer, following the Skia allocation model for
  // retrieving text intercepts.
  SkPaint paint = flags.ToSkPaint();
  unsigned num_intervals = InterceptsFromBlobs(blobs, paint, bounds, nullptr);
  if (!num_intervals)
    return;
  DCHECK_EQ(num_intervals % 2, 0u);
  intercepts.resize(num_intervals / 2u);

  InterceptsFromBlobs(blobs, paint, bounds,
                      reinterpret_cast<SkScalar*>(intercepts.data()));
}

}  // anonymous namespace

void Font::GetTextIntercepts(const TextFragmentPaintInfo& text_info,
                             const cc::PaintFlags& flags,
                             const std::tuple<float, float>& bounds,
                             Vector<TextIntercept>& intercepts) const {
  if (ShouldSkipDrawing())
    return;

  ShapeResultBloberizer::FillGlyphsNG bloberizer(
      GetFontDescription(), text_info.text, text_info.from, text_info.to,
      text_info.shape_result, ShapeResultBloberizer::Type::kTextIntercepts);

  GetTextInterceptsInternal(bloberizer.Blobs(), flags, bounds, intercepts);
}

base::span<const FontFeatureRange> Font::GetFontFeatures() const {
  return EnsureFontFallbackList()->GetFontFeatures(font_description_);
}

bool Font::HasNonInitialFontFeatures() const {
  return EnsureFontFallbackList()->HasNonInitialFontFeatures(font_description_);
}

ShapeCache* Font::GetShapeCache() const {
  return EnsureFontFallbackList()->GetShapeCache(font_description_);
}

bool Font::CanShapeWordByWord() const {
  return EnsureFontFallbackList()->CanShapeWordByWord(GetFontDescription());
}

void Font::ReportNotDefGlyph() const {
  FontSelector* fontSelector = EnsureFontFallbackList()->GetFontSelector();
  // We have a few non-DOM usages of Font code, for example in DragImage::Create
  // and in EmbeddedObjectPainter::paintReplaced. In those cases, we can't
  // retrieve a font selector as our connection to a Document object to report
  // UseCounter metrics, and thus we cannot report notdef glyphs.
  if (fontSelector)
    fontSelector->ReportNotDefGlyph();
}

void Font::ReportEmojiSegmentGlyphCoverage(unsigned num_clusters,
                                           unsigned num_broken_clusters) const {
  FontSelector* fontSelector = EnsureFontFallbackList()->GetFontSelector();
  // See ReportNotDefGlyph(), sometimes no fontSelector is available in non-DOM
  // usages of Font.
  if (fontSelector) {
    fontSelector->ReportEmojiSegmentGlyphCoverage(num_clusters,
                                                  num_broken_clusters);
  }
}

void Font::WillUseFontData(const String& text) const {
  const FontDescription& font_description = GetFontDescription();
  const FontFamily& family = font_description.Family();
  if (family.FamilyName().empty()) [[unlikely]] {
    return;
  }
  if (FontSelector* font_selector = GetFontSelector()) {
    font_selector->WillUseFontData(font_description, family, text);
    return;
  }
  // Non-DOM usages can't resolve generic family.
  if (family.IsPrewarmed() || family.FamilyIsGeneric())
    return;
  family.SetIsPrewarmed();
  FontCache::PrewarmFamily(family.FamilyName());
}

GlyphData Font::GetEmphasisMarkGlyphData(const AtomicString& mark) const {
  if (mark.empty())
    return GlyphData();
  return CachingWordShaper(*this).EmphasisMarkGlyphData(TextRun(mark));
}

int Font::EmphasisMarkAscent(const AtomicString& mark) const {
  FontCachePurgePreventer purge_preventer;

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data;
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Ascent();
}

int Font::EmphasisMarkDescent(const AtomicString& mark) const {
  FontCachePurgePreventer purge_preventer;

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data;
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Descent();
}

int Font::EmphasisMarkHeight(const AtomicString& mark) const {
  FontCachePurgePreventer purge_preventer;

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data;
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Height();
}

float Font::TextAutoSpaceInlineSize() const {
  if (const SimpleFontData* font_data = PrimaryFont()) {
    return font_data->TextAutoSpaceInlineSize();
  }
  NOTREACHED();
}

float Font::TabWidth(const SimpleFontData* font_data,
                     const TabSize& tab_size,
                     float position) const {
  float base_tab_width = TabWidth(font_data, tab_size);
  if (!base_tab_width)
    return GetFontDescription().LetterSpacing();

  float distance_to_tab_stop = base_tab_width - fmodf(position, base_tab_width);

  // Let the minimum width be the half of the space width so that it's always
  // recognizable.  if the distance to the next tab stop is less than that,
  // advance an additional tab stop.
  if (distance_to_tab_stop < font_data->SpaceWidth() / 2)
    distance_to_tab_stop += base_tab_width;

  return distance_to_tab_stop;
}

LayoutUnit Font::TabWidth(const TabSize& tab_size, LayoutUnit position) const {
  const SimpleFontData* font_data = PrimaryFont();
  if (!font_data)
    return LayoutUnit::FromFloatCeil(GetFontDescription().LetterSpacing());
  float base_tab_width = tab_size.GetPixelSize(font_data->SpaceWidth());
  if (!base_tab_width)
    return LayoutUnit::FromFloatCeil(GetFontDescription().LetterSpacing());

  LayoutUnit distance_to_tab_stop = LayoutUnit::FromFloatFloor(
      base_tab_width - fmodf(position, base_tab_width));

  // Let the minimum width be the half of the space width so that it's always
  // recognizable.  if the distance to the next tab stop is less than that,
  // advance an additional tab stop.
  if (distance_to_tab_stop < font_data->SpaceWidth() / 2)
    distance_to_tab_stop += base_tab_width;

  return distance_to_tab_stop;
}

bool Font::IsFallbackValid() const {
  return !font_fallback_list_ || font_fallback_list_->IsValid();
}

}  // namespace blink
