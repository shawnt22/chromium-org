// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/platform/web_font.h"

#include "cc/paint/paint_flags.h"
#include "third_party/blink/public/platform/web_font_description.h"
#include "third_party/blink/public/platform/web_text_run.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/plain_text_painter.h"
#include "third_party/blink/renderer/platform/fonts/text_run_paint_info.h"
#include "third_party/blink/renderer/platform/text/text_run.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

WebFont* WebFont::Create(const WebFontDescription& description) {
  return new WebFont(description);
}

class WebFont::Impl final : public GarbageCollected<WebFont::Impl> {
 public:
  explicit Impl(const WebFontDescription& description)
      : font_(MakeGarbageCollected<Font>(description)) {}

  void Trace(Visitor* visitor) const { visitor->Trace(font_); }

  const Font* GetFont() const { return font_; }

 private:
  Member<const Font> font_;
};

WebFont::WebFont(const WebFontDescription& description)
    : private_(MakeGarbageCollected<Impl>(description)) {}

WebFont::~WebFont() = default;

WebFontDescription WebFont::GetFontDescription() const {
  return WebFontDescription(private_->GetFont()->GetFontDescription());
}

static inline const SimpleFontData* GetFontData(const Font* font) {
  const SimpleFontData* font_data = font->PrimaryFont();
  DCHECK(font_data);
  return font_data;
}

int WebFont::Ascent() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().Ascent() : 0;
}

int WebFont::Descent() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().Descent() : 0;
}

int WebFont::Height() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().Height() : 0;
}

int WebFont::LineSpacing() const {
  const SimpleFontData* font_data = GetFontData(private_->GetFont());
  return font_data ? font_data->GetFontMetrics().LineSpacing() : 0;
}

float WebFont::XHeight() const {
  const SimpleFontData* font_data = private_->GetFont()->PrimaryFont();
  DCHECK(font_data);
  return font_data ? font_data->GetFontMetrics().XHeight() : 0;
}

void WebFont::DrawText(cc::PaintCanvas* canvas,
                       const WebTextRun& run,
                       const gfx::PointF& left_baseline,
                       SkColor color) const {
  FontCachePurgePreventer font_cache_purge_preventer;
  TextRun text_run(run);

  cc::PaintFlags flags;
  flags.setColor(color);
  flags.setAntiAlias(true);
  PlainTextPainter::Shared().DrawWithoutBidi(text_run, *private_->GetFont(),
                                             *canvas, left_baseline, flags);
}

int WebFont::CalculateWidth(const WebTextRun& run) const {
  return PlainTextPainter::Shared().ComputeInlineSizeWithoutBidi(
      run, *private_->GetFont());
}

int WebFont::OffsetForPosition(const WebTextRun& run, float position) const {
  return PlainTextPainter::Shared().OffsetForPositionWithoutBidi(
      run, *private_->GetFont(), position, kIncludePartialGlyphs,
      BreakGlyphsOption(false));
}

gfx::RectF WebFont::SelectionRectForText(const WebTextRun& run,
                                         const gfx::PointF& left_baseline,
                                         int height,
                                         int from,
                                         int to) const {
  return PlainTextPainter::Shared().SelectionRectForTextWithoutBidi(
      run, from, to == -1 ? run.text.length() : to, *private_->GetFont(),
      left_baseline, height);
}

}  // namespace blink
