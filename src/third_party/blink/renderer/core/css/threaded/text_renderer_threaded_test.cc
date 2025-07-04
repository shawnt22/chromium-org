// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/skottie_wrapper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/css/resolver/font_style_resolver.h"
#include "third_party/blink/renderer/core/css/threaded/multi_threaded_test_util.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/fonts/font_custom_platform_data.h"
#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/font_selector.h"
#include "third_party/blink/renderer/platform/fonts/plain_text_painter.h"
#include "third_party/blink/renderer/platform/fonts/shaping/caching_word_shape_iterator.h"
#include "third_party/blink/renderer/platform/fonts/shaping/harfbuzz_shaper.h"
#include "third_party/blink/renderer/platform/fonts/text_run_paint_info.h"
#include "third_party/blink/renderer/platform/graphics/test/mock_paint_canvas.h"
#include "third_party/blink/renderer/platform/language.h"
#include "third_party/blink/renderer/platform/testing/font_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "third_party/blink/renderer/platform/text/text_direction.h"

using testing::_;
using testing::Return;

using blink::test::CreateTestFont;

namespace blink {

TSAN_TEST(TextRendererThreadedTest, MeasureText) {
  ScopedNoFontAntialiasingForTest disable_no_font_antialiasing_for_test(false);

  RunOnThreads([]() {
    String text = "measure this";

    FontDescription font_description;
    font_description.SetComputedSize(12.0);
    font_description.SetLocale(LayoutLocale::Get(AtomicString("en")));
    ASSERT_EQ(USCRIPT_LATIN, font_description.GetScript());
    font_description.SetGenericFamily(FontDescription::kStandardFamily);

    Font* font = MakeGarbageCollected<Font>(font_description);

    const SimpleFontData* font_data = font->PrimaryFont();
    ASSERT_TRUE(font_data);

    TextRun text_run(text, TextDirection::kLtr,
                     /* directional_override */ false,
                     /* normalize_space */ true);
    gfx::RectF text_bounds =
        MakeGarbageCollected<PlainTextPainter>(PlainTextPainter::kCanvas)
            ->SelectionRectForTextWithoutBidi(
                text_run, 0, text_run.length(), *font, gfx::PointF(),
                font->GetFontDescription().ComputedSize());

    // X direction.
    if (RuntimeEnabledFeatures::CanvasTextNgEnabled(nullptr)) {
      EXPECT_EQ(
          78, MakeGarbageCollected<PlainTextPainter>(PlainTextPainter::kCanvas)
                  ->ComputeInlineSize(text_run, *font));
    } else {
      EXPECT_EQ(78, font->DeprecatedWidth(text_run));
    }
    EXPECT_EQ(0, text_bounds.x());
    EXPECT_EQ(78, text_bounds.right());

    // Y direction.
    const FontMetrics& font_metrics = font_data->GetFontMetrics();
    EXPECT_EQ(11, font_metrics.FloatAscent());
    EXPECT_EQ(3, font_metrics.FloatDescent());
    EXPECT_EQ(0, text_bounds.y());
    EXPECT_EQ(12, text_bounds.bottom());
  });
}

TSAN_TEST(TextRendererThreadedTest, DrawText) {
  callbacks_per_thread_ = 50;
  RunOnThreads([]() {
    String text = "draw this";

    FontDescription font_description;
    font_description.SetComputedSize(12.0);
    font_description.SetLocale(LayoutLocale::Get(AtomicString("en")));
    ASSERT_EQ(USCRIPT_LATIN, font_description.GetScript());
    font_description.SetGenericFamily(FontDescription::kStandardFamily);

    Font* font = MakeGarbageCollected<Font>(font_description);

    gfx::PointF location(0, 0);
    TextRun text_run(text, TextDirection::kLtr,
                     /* directional_override */ false,
                     /* normalize_space */ true);

    MockPaintCanvas mpc;
    cc::PaintFlags flags;

    EXPECT_CALL(mpc, getSaveCount()).WillOnce(Return(17));
    EXPECT_CALL(mpc, drawTextBlob(_, 0, 0, _)).Times(1);
    EXPECT_CALL(mpc, restoreToCount(17)).WillOnce(Return());

    if (RuntimeEnabledFeatures::CanvasTextNgEnabled(nullptr)) {
      MakeGarbageCollected<PlainTextPainter>(PlainTextPainter::kCanvas)
          ->DrawWithBidiReorder(text_run, 0, text_run.length(), *font,
                                Font::kUseFallbackIfFontNotReady, mpc, location,
                                flags, Font::DrawType::kGlyphsAndClusters);
    } else {
      TextRunPaintInfo text_run_paint_info(text_run);
      font->DeprecatedDrawBidiText(&mpc, text_run_paint_info, location,
                                   Font::kUseFallbackIfFontNotReady, flags,
                                   Font::DrawType::kGlyphsAndClusters);
    }
  });
}

}  // namespace blink
