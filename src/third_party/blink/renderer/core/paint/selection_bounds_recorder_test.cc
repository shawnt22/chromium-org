// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/editing/selection_template.h"
#include "third_party/blink/renderer/core/editing/testing/selection_sample.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/paint/paint_controller_paint_test.h"

using testing::ElementsAre;

namespace blink {

class SelectionBoundsRecorderTest : public PaintControllerPaintTestBase {};

TEST_F(SelectionBoundsRecorderTest, SelectAll) {
  SetBodyInnerHTML("<span>A<br>B<br>C</span>");

  LocalFrame* local_frame = GetDocument().GetFrame();
  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  local_frame->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(8, 8));
  EXPECT_EQ(start.edge_end, gfx::Point(8, 9));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(9, 10));
  EXPECT_EQ(end.edge_end, gfx::Point(9, 11));
}

TEST_F(SelectionBoundsRecorderTest, SelectAllInVerticalRl) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);
  SetBodyInnerHTML(R"HTML(
      <style>body {
        writing-mode: vertical-rl;
        font: 20px Ahem;
      }</style><span>AB<br>C</span>)HTML");

  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  local_frame->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(772, 8));
  EXPECT_EQ(start.edge_end, gfx::Point(792, 8));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(772, 28));
  EXPECT_EQ(end.edge_end, gfx::Point(752, 28));
}

TEST_F(SelectionBoundsRecorderTest, SelectAllInVerticalLr) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);
  SetBodyInnerHTML(R"HTML(
      <style>body {
        writing-mode: vertical-lr;
        font: 20px Ahem;
      }</style><span>AB<br>C</span>)HTML");

  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  local_frame->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(28, 8));
  EXPECT_EQ(start.edge_end, gfx::Point(8, 8));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(28, 28));
  EXPECT_EQ(end.edge_end, gfx::Point(48, 28));
}

TEST_F(SelectionBoundsRecorderTest, SelectAllInSidewaysRl) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);
  SetBodyInnerHTML(R"HTML(
      <style>body {
        writing-mode: sideways-rl;
        font: 20px Ahem;
      }</style><span>AB<br>C</span>)HTML");

  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  local_frame->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(772, 8));
  EXPECT_EQ(start.edge_end, gfx::Point(792, 8));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(772, 28));
  EXPECT_EQ(end.edge_end, gfx::Point(752, 28));
}

TEST_F(SelectionBoundsRecorderTest, SelectAllInSidewaysLr) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);
  SetBodyInnerHTML(R"HTML(
      <style>body {
        writing-mode: sideways-lr;
        font: 20px Ahem;
      }</style><span>AB<br>C</span>)HTML");

  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  local_frame->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(8, 592));
  EXPECT_EQ(start.edge_end, gfx::Point(28, 592));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(28, 572));
  EXPECT_EQ(end.edge_end, gfx::Point(48, 572));
}

TEST_F(SelectionBoundsRecorderTest, SelectMultiline) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);

  local_frame->Selection().SetSelection(
      SelectionSample::SetSelectionText(GetDocument().body(),
                                        R"HTML(
          <style>
            div { white-space:pre; font-family: Ahem; }
          </style>
          <div>f^oo\nbar\nb|az</div>
      )HTML"),
      SetSelectionOptions());

  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(9, 8));
  EXPECT_EQ(start.edge_end, gfx::Point(9, 9));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(19, 8));
  EXPECT_EQ(end.edge_end, gfx::Point(19, 9));
}

TEST_F(SelectionBoundsRecorderTest, SelectMultilineEmptyStartEnd) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);
  local_frame->Selection().SetSelection(
      SelectionSample::SetSelectionText(GetDocument().body(),
                                        R"HTML(
          <style>
            body { margin: 0; }
            * { font: 10px/1 Ahem; }
          </style>
          <div>foo^<br>bar<br>|baz</div>
      )HTML"),
      SetSelectionOptions());
  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_TRUE(chunks[0].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[0].layer_selection_data->end.has_value());
  PaintedSelectionBound start = chunks[0].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(30, 0));
  EXPECT_EQ(start.edge_end, gfx::Point(30, 10));

  PaintedSelectionBound end = chunks[0].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(0, 20));
  EXPECT_EQ(end.edge_end, gfx::Point(0, 30));
}

TEST_F(SelectionBoundsRecorderTest, InvalidationForEmptyBounds) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);

  // Set a selection that has empty start and end in separate paint chunks.
  // We'll move these empty endpoints into the middle div and make sure
  // everything is invalidated/re-painted/recorded correctly.
  local_frame->Selection().SetSelection(
      SelectionSample::SetSelectionText(GetDocument().body(),
                                        R"HTML(
          <style>
            body { margin: 0; }
            div { will-change: transform; }
            * { font: 10px/1 Ahem; }
          </style>
          <div>foo^</div><div id=target>bar</div><div>|baz</div>
      )HTML"),
      SetSelectionOptions());
  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  UpdateAllLifecyclePhasesForTest();

  auto chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 4u);

  // Skip the root chunk to get to the first div.
  EXPECT_TRUE(chunks[1].layer_selection_data->start.has_value());
  PaintedSelectionBound start = chunks[1].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(30, 0));
  EXPECT_EQ(start.edge_end, gfx::Point(30, 10));

  // Skip the middle div as well to get to the third div where the end of the
  // selection is.
  EXPECT_TRUE(chunks[3].layer_selection_data->end.has_value());
  PaintedSelectionBound end = chunks[3].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  // Coordinates are chunk-relative, so they should start at 0 y coordinate.
  EXPECT_EQ(end.edge_start, gfx::Point(0, 0));
  EXPECT_EQ(end.edge_end, gfx::Point(0, 10));

  // Move the selection around the start and end of the second div.
  local_frame->Selection().SetSelection(
      SelectionInDOMTree::Builder()
          .Collapse(Position(GetElementById("target")->firstChild(), 0))
          .Extend(Position(GetElementById("target")->firstChild(), 3))
          .Build(),
      SetSelectionOptions());

  // Ensure the handle will be visible for the next paint (previous call to
  // SetSelection will clear the bit).
  local_frame->Selection().SetHandleVisibleForTesting();

  UpdateAllLifecyclePhasesForTest();

  chunks = ContentPaintChunks();
  ASSERT_EQ(chunks.size(), 4u);

  // Skip the root chunk to get to the first div, which should no longer have
  // a recorded value.
  EXPECT_FALSE(chunks[1].layer_selection_data);

  // Validate start/end in second div.
  EXPECT_TRUE(chunks[2].layer_selection_data->start.has_value());
  EXPECT_TRUE(chunks[2].layer_selection_data->end.has_value());
  start = chunks[2].layer_selection_data->start.value();
  EXPECT_EQ(start.type, gfx::SelectionBound::LEFT);
  EXPECT_EQ(start.edge_start, gfx::Point(0, 0));
  EXPECT_EQ(start.edge_end, gfx::Point(0, 10));

  end = chunks[2].layer_selection_data->end.value();
  EXPECT_EQ(end.type, gfx::SelectionBound::RIGHT);
  EXPECT_EQ(end.edge_start, gfx::Point(30, 0));
  EXPECT_EQ(end.edge_end, gfx::Point(30, 10));

  // Third div's chunk should no longer have an end value.
  EXPECT_FALSE(chunks[3].layer_selection_data);
}

TEST_F(SelectionBoundsRecorderTest, BoundsHidden) {
  LocalFrame* local_frame = GetDocument().GetFrame();
  LoadAhem(*local_frame);
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0; font: 80px Ahem; }</style>
    <div id="container" style="width: 100px; height: 100px; overflow: hidden">
      X<br>X
    </div>
  )HTML");

  local_frame->Selection().SetHandleVisibleForTesting();
  local_frame->GetPage()->GetFocusController().SetFocusedFrame(local_frame);
  local_frame->Selection().SelectAll();
  UpdateAllLifecyclePhasesForTest();

  auto* host = local_frame->View()->RootCcLayer()->layer_tree_host();
  EXPECT_FALSE(host->selection().start.hidden);
  EXPECT_EQ(gfx::SelectionBound::LEFT, host->selection().start.type);
  EXPECT_EQ(gfx::Point(), host->selection().start.edge_start);
  EXPECT_EQ(gfx::Point(0, 80), host->selection().start.edge_end);
  EXPECT_TRUE(host->selection().end.hidden);
  EXPECT_EQ(gfx::SelectionBound::RIGHT, host->selection().end.type);
  EXPECT_EQ(gfx::Point(80, 80), host->selection().end.edge_start);
  EXPECT_EQ(gfx::Point(80, 160), host->selection().end.edge_end);

  GetDocument()
      .getElementById(AtomicString("container"))
      ->scrollToForTesting(0, 60);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(host->selection().start.hidden);
  EXPECT_EQ(gfx::SelectionBound::LEFT, host->selection().start.type);
  EXPECT_EQ(gfx::Point(0, -60), host->selection().start.edge_start);
  EXPECT_EQ(gfx::Point(0, 20), host->selection().start.edge_end);
  EXPECT_FALSE(host->selection().end.hidden);
  EXPECT_EQ(gfx::SelectionBound::RIGHT, host->selection().end.type);
  EXPECT_EQ(gfx::Point(80, 20), host->selection().end.edge_start);
  EXPECT_EQ(gfx::Point(80, 100), host->selection().end.edge_end);
}

}  // namespace blink
