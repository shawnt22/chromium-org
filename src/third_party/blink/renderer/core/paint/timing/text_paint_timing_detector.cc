// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/timing/text_paint_timing_detector.h"

#include <memory>

#include "third_party/blink/renderer/core/css/properties/longhands.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/timing/largest_contentful_paint_calculator.h"
#include "third_party/blink/renderer/core/paint/timing/paint_timing.h"
#include "third_party/blink/renderer/core/paint/timing/paint_timing_callback_manager.h"
#include "third_party/blink/renderer/core/paint/timing/paint_timing_detector.h"
#include "third_party/blink/renderer/core/timing/soft_navigation_context.h"
#include "third_party/blink/renderer/core/timing/soft_navigation_heuristics.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/traced_value.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

void TextRecord::Trace(Visitor* visitor) const {
  visitor->Trace(node_);
  visitor->Trace(soft_navigation_context_);
}

TextPaintTimingDetector::TextPaintTimingDetector(
    LocalFrameView* frame_view,
    PaintTimingDetector* paint_timing_detector)
    : frame_view_(frame_view),
      ltp_manager_(MakeGarbageCollected<LargestTextPaintManager>(
          frame_view,
          paint_timing_detector)) {}

void LargestTextPaintManager::PopulateTraceValue(
    TracedValue& value,
    const TextRecord& first_text_paint) {
  value.SetString("nodeName", first_text_paint.node_->DebugName());
  value.SetInteger("DOMNodeId",
                   static_cast<int>(first_text_paint.node_->GetDomNodeId()));
  value.SetInteger("size", static_cast<int>(first_text_paint.recorded_size));
  value.SetInteger("candidateIndex", ++count_candidates_);
  value.SetBoolean("isMainFrame", frame_view_->GetFrame().IsMainFrame());
  value.SetBoolean("isOutermostMainFrame",
                   frame_view_->GetFrame().IsOutermostMainFrame());
  value.SetBoolean("isEmbeddedFrame",
                   !frame_view_->GetFrame().LocalFrameRoot().IsMainFrame() ||
                       frame_view_->GetFrame().IsInFencedFrameTree());
  if (first_text_paint.lcp_rect_info_) {
    first_text_paint.lcp_rect_info_->OutputToTraceValue(value);
  }
}

void LargestTextPaintManager::ReportCandidateToTrace(
    const TextRecord& largest_text_record) {
  if (!PaintTimingDetector::IsTracing() ||
      frame_view_->GetFrame().IsDetached()) {
    return;
  }
  auto value = std::make_unique<TracedValue>();
  PopulateTraceValue(*value, largest_text_record);
  TRACE_EVENT_MARK_WITH_TIMESTAMP2(
      "loading", "LargestTextPaint::Candidate", largest_text_record.paint_time,
      "data", std::move(value), "frame",
      GetFrameIdForTracing(&frame_view_->GetFrame()));
}

std::pair<TextRecord*, bool> LargestTextPaintManager::UpdateMetricsCandidate() {
  if (!largest_text_) {
    return {nullptr, false};
  }
  const base::TimeTicks time = largest_text_->paint_time;
  const uint64_t size = largest_text_->recorded_size;
  CHECK(paint_timing_detector_);
  CHECK(paint_timing_detector_->GetLargestContentfulPaintCalculator());

  bool changed = paint_timing_detector_->GetLargestContentfulPaintCalculator()
                     ->NotifyMetricsIfLargestTextPaintChanged(time, size);
  if (changed) {
    // It is not possible for an update to happen with a candidate that has no
    // paint time.
    DCHECK(!time.is_null());
    ReportCandidateToTrace(*largest_text_);
  }
  return {largest_text_.Get(), changed};
}

OptionalPaintTimingCallback TextPaintTimingDetector::TakePaintTimingCallback() {
  if (!added_entry_in_latest_frame_)
    return std::nullopt;

  added_entry_in_latest_frame_ = false;

  auto callback =
      BindOnce(&TextPaintTimingDetector::AssignPaintTimeToQueuedRecords,
               WrapWeakPersistent(this), frame_index_++);
  if (!callback_manager_) {
    return callback;
  }

  // This is for unit-tests only.
  callback_manager_->RegisterCallback(std::move(callback));
  return std::nullopt;
}

void TextPaintTimingDetector::LayoutObjectWillBeDestroyed(
    const LayoutObject& object) {
  recorded_set_.erase(&object);
  rewalkable_set_.erase(&object);
  texts_queued_for_paint_time_.erase(&object);
}

bool TextPaintTimingDetector::ShouldWalkObject(
    const LayoutBoxModelObject& aggregator) {
  Node* node = aggregator.GetNode();
  if (!node)
    return false;

  // Do not walk the object if it has already been recorded, unless it has
  // specifically been marked for "re-walking".
  if (recorded_set_.Contains(&aggregator)) {
    // TODO(crbug.com/40220033): rewalkable_set_ should be empty most of the
    // time, until we ship the feature for custom fonts.
    // HashSet::Contains() appears to hash key even when container is empty.
    return !rewalkable_set_.empty() && rewalkable_set_.Contains(&aggregator);
  }

  // Check if we know for certain that we need to measure this node, first.
  if (IsRecordingLargestTextPaint() ||
      TextElementTiming::NeededForTiming(*node)) {
    return true;
  }

  // If we haven't seen this node before, an we aren't recording LCP nor is this
  // node needed for element timing, the only remaining reason to measure text
  // timing is for soft navs paints.  We leave this check for last, just because
  // it might be more expensive.
  // TODO(crbug.com/423670827): If we cache this value during pre-paint, then we
  // might not need to worry about it.
  if (LocalDOMWindow* window = frame_view_->GetFrame().DomWindow()) {
    if (SoftNavigationHeuristics* heuristics =
            window->GetSoftNavigationHeuristics();
        heuristics && heuristics->MaybeGetSoftNavigationContextForTiming(
                          aggregator.GetNode())) {
      return true;
    }
  }

  // If we've decided not to visit this node for any reason, then let's add it
  // to the set of recorded nodes, even without measuring its paint, so we never
  // bother to check it again.
  // TODO(crbug.com/423670827): Part of the motivation for doing this is so we
  // don't try to look up context more than once per node.  But then this
  // content becomes un-recorded for any future observers, and that isn't always
  // correct (i.e. late application of elementtiming or an Interaction which
  // toggles content within the node, i.e. adding textContent for the first time
  // to a previously empty node.)
  recorded_set_.insert(&aggregator);
  return false;
}

void TextPaintTimingDetector::RecordAggregatedText(
    const LayoutBoxModelObject& aggregator,
    const gfx::Rect& aggregated_visual_rect,
    const PropertyTreeStateOrAlias& property_tree_state) {
  bool is_color_transparent = aggregator.StyleRef()
                                  .VisitedDependentColor(GetCSSPropertyColor())
                                  .IsFullyTransparent();
  bool has_shadow = !!aggregator.StyleRef().TextShadow();
  bool has_text_stroke = aggregator.StyleRef().TextStrokeWidth();

  if (is_color_transparent && !has_shadow && !has_text_stroke) {
    return;
  }

  DCHECK(ShouldWalkObject(aggregator));

  // The caller should check this.
  DCHECK(!aggregated_visual_rect.IsEmpty());

  gfx::RectF mapped_visual_rect =
      frame_view_->GetPaintTimingDetector().CalculateVisualRect(
          aggregated_visual_rect, property_tree_state);
  uint64_t aggregated_size = mapped_visual_rect.size().GetArea();

  DCHECK_LE(IgnorePaintTimingScope::IgnoreDepth(), 1);
  // Record the largest aggregated text that is hidden due to documentElement
  // being invisible but by no other reason (i.e. IgnoreDepth() needs to be 1).
  if (IgnorePaintTimingScope::IgnoreDepth() == 1) {
    if (IgnorePaintTimingScope::IsDocumentElementInvisible() &&
        IsRecordingLargestTextPaint()) {
      ltp_manager_->MaybeUpdateLargestIgnoredText(aggregator, aggregated_size,
                                                  aggregated_visual_rect,
                                                  mapped_visual_rect);
    }
    return;
  }

  // Web font styled node should be rewalkable so that resizing during swap
  // would make the node eligible to be LCP candidate again.
  if (RuntimeEnabledFeatures::WebFontResizeLCPEnabled()) {
    if (aggregator.StyleRef().GetFont()->HasCustomFont()) {
      rewalkable_set_.insert(&aggregator);
    }
  }

  SoftNavigationContext* context = nullptr;
  if (LocalDOMWindow* window = frame_view_->GetFrame().DomWindow()) {
    if (SoftNavigationHeuristics* heuristics =
            window->GetSoftNavigationHeuristics()) {
      context = heuristics->MaybeGetSoftNavigationContextForTiming(
          aggregator.GetNode());
    }
  }

  recorded_set_.insert(&aggregator);
  TextRecord* record = MaybeRecordTextRecord(
      aggregator, aggregated_size, property_tree_state, aggregated_visual_rect,
      mapped_visual_rect, context);
  if (context && record) {
    context->AddPaintedArea(record);
  }
  if (std::optional<PaintTimingVisualizer>& visualizer =
          frame_view_->GetPaintTimingDetector().Visualizer()) {
    visualizer->DumpTextDebuggingRect(aggregator, mapped_visual_rect);
  }
}

void TextPaintTimingDetector::StopRecordingLargestTextPaint() {
  recording_largest_text_paint_ = false;
}

void TextPaintTimingDetector::ReportLargestIgnoredText() {
  if (!ltp_manager_)
    return;
  TextRecord* record = ltp_manager_->PopLargestIgnoredText();
  // If the content has been removed, abort. It was never visible.
  if (!record || !record->node_ || !record->node_->GetLayoutObject())
    return;

  // Trigger FCP if it's not already set.
  Document* document = frame_view_->GetFrame().GetDocument();
  DCHECK(document);
  PaintTiming::From(*document).MarkFirstContentfulPaint();

  record->frame_index_ = frame_index_;
  QueueToMeasurePaintTime(*record->node_->GetLayoutObject(), record);
}

void TextPaintTimingDetector::Trace(Visitor* visitor) const {
  visitor->Trace(callback_manager_);
  visitor->Trace(frame_view_);
  visitor->Trace(text_element_timing_);
  visitor->Trace(rewalkable_set_);
  visitor->Trace(recorded_set_);
  visitor->Trace(texts_queued_for_paint_time_);
  visitor->Trace(ltp_manager_);
}

LargestTextPaintManager::LargestTextPaintManager(
    LocalFrameView* frame_view,
    PaintTimingDetector* paint_timing_detector)
    : frame_view_(frame_view), paint_timing_detector_(paint_timing_detector) {}

void LargestTextPaintManager::MaybeUpdateLargestText(TextRecord* record) {
  if (!largest_text_ || largest_text_->recorded_size < record->recorded_size) {
    largest_text_ = record;
  }
}

void LargestTextPaintManager::MaybeUpdateLargestIgnoredText(
    const LayoutObject& object,
    const uint64_t& size,
    const gfx::Rect& frame_visual_rect,
    const gfx::RectF& root_visual_rect) {
  if (size &&
      (!largest_ignored_text_ || size > largest_ignored_text_->recorded_size)) {
    // Create the largest ignored text with a |frame_index_| of 0. When it is
    // queued for paint, we'll set the appropriate |frame_index_|.
    largest_ignored_text_ = MakeGarbageCollected<TextRecord>(
        *object.GetNode(), size, gfx::RectF(), frame_visual_rect,
        root_visual_rect, 0u, /*is_needed_for_timing=*/false,
        /*soft_navigation_context=*/nullptr);
  }
}

void LargestTextPaintManager::Trace(Visitor* visitor) const {
  visitor->Trace(largest_text_);
  visitor->Trace(largest_ignored_text_);
  visitor->Trace(frame_view_);
  visitor->Trace(paint_timing_detector_);
}

void TextPaintTimingDetector::AssignPaintTimeToQueuedRecords(
    uint32_t frame_index,
    const base::TimeTicks& timestamp,
    const DOMPaintTimingInfo& paint_timing_info) {
  if (!text_element_timing_) {
    if (Document* document = frame_view_->GetFrame().GetDocument()) {
      if (LocalDOMWindow* window = document->domWindow()) {
        text_element_timing_ = TextElementTiming::From(*window);
      }
    }
  }

  bool is_needed_for_lcp = IsRecordingLargestTextPaint();
  bool can_report_timing =
      text_element_timing_ ? text_element_timing_->CanReportElements() : false;
  HeapVector<Member<const LayoutObject>> keys_to_be_removed;
  for (const auto& [key, record] : texts_queued_for_paint_time_) {
    if (!record->paint_time.is_null() || record->frame_index_ > frame_index) {
      continue;
    }
    record->paint_time = timestamp;
    record->paint_timing_info = paint_timing_info;
    if (can_report_timing && record->is_needed_for_timing_) {
      text_element_timing_->OnTextObjectPainted(*record, paint_timing_info);
    }

    if (is_needed_for_lcp && ltp_manager_ && (record->recorded_size > 0u)) {
      ltp_manager_->MaybeUpdateLargestText(record);
    }
    keys_to_be_removed.push_back(key);
  }
  texts_queued_for_paint_time_.RemoveAll(keys_to_be_removed);
}

TextRecord* TextPaintTimingDetector::MaybeRecordTextRecord(
    const LayoutObject& object,
    const uint64_t& visual_size,
    const PropertyTreeStateOrAlias& property_tree_state,
    const gfx::Rect& frame_visual_rect,
    const gfx::RectF& root_visual_rect,
    SoftNavigationContext* context) {
  Node* node = object.GetNode();
  DCHECK(node);

  bool is_needed_for_lcp = IsRecordingLargestTextPaint() && visual_size > 0u;
  bool is_needed_for_element_timing = TextElementTiming::NeededForTiming(*node);
  bool is_needed_for_soft_navs = context != nullptr;

  // If the node is not required by LCP and not required by ElementTiming,
  // we can bail out early.
  if (!is_needed_for_lcp && !is_needed_for_element_timing &&
      !is_needed_for_soft_navs) {
    return nullptr;
  }

  TextRecord* record;
  if (visual_size == 0u) {
    record = MakeGarbageCollected<TextRecord>(
        *node, visual_size, gfx::RectF(), gfx::Rect(), gfx::RectF(),
        frame_index_, is_needed_for_element_timing, context);
  } else {
    record = MakeGarbageCollected<TextRecord>(
        *node, visual_size,
        TextElementTiming::ComputeIntersectionRect(
            object, frame_visual_rect, property_tree_state, frame_view_),
        frame_visual_rect, root_visual_rect, frame_index_,
        is_needed_for_element_timing, context);
  }
  QueueToMeasurePaintTime(object, record);
  return record;
}

}  // namespace blink
