// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/timing/soft_navigation_context.h"

#include "base/trace_event/trace_event.h"
#include "third_party/blink/renderer/core/dom/container_node.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/paint/timing/image_paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/timing/largest_contentful_paint_calculator.h"
#include "third_party/blink/renderer/core/paint/timing/text_paint_timing_detector.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"

namespace blink {

uint64_t SoftNavigationContext::last_context_id_ = 0;

SoftNavigationContext::SoftNavigationContext(
    LocalDOMWindow& window,
    features::SoftNavigationHeuristicsMode mode)
    : paint_attribution_mode_(mode),
      lcp_calculator_(MakeGarbageCollected<LargestContentfulPaintCalculator>(
          DOMWindowPerformance::performance(window))) {}

void SoftNavigationContext::AddModifiedNode(Node* node) {
  if (paint_attribution_mode_ !=
      features::SoftNavigationHeuristicsMode::kPrePaintBasedAttribution) {
    auto add_result = modified_nodes_.insert(node);
    if (!add_result.is_new_entry) {
      return;
    }
  }
  ++num_modified_dom_nodes_;
  TRACE_EVENT_INSTANT(
      "loading", "SoftNavigationContext::AddedModifiedNodeInAnimationFrame",
      perfetto::Track::FromPointer(this), "context", this, "nodeId",
      node->GetDomNodeId(), "nodeDebugName", node->DebugName(),
      "domModificationsThisAnimationFrame",
      num_modified_dom_nodes_ - num_modified_dom_nodes_last_animation_frame_);
}

bool SoftNavigationContext::IsNeededForTiming(Node* node) {
  CHECK_NE(paint_attribution_mode_,
           features::SoftNavigationHeuristicsMode::kPrePaintBasedAttribution);
  if (!node) {
    return false;
  }
  for (Node* current_node = node; current_node;
       current_node = current_node->parentNode()) {
    if (current_node == known_not_related_parent_) {
      return false;
    }
    // If the current_node is known modified, it is a container root.
    if (modified_nodes_.Contains(current_node)) {
      return true;
    }
    // For now, do not "tree walk" when in basic mode.
    if (paint_attribution_mode_ ==
        features::SoftNavigationHeuristicsMode::kBasic) {
      break;
    }
  }
  // This node was not part of a container root for this context.
  // Let's cache this node's parent node, so if any of this node's siblings
  // paint next, we can finish this check quicker for them.
  if (Node* parent = node->parentNode()) {
    known_not_related_parent_ = parent;
  }
  return false;
}

bool SoftNavigationContext::AddPaintedArea(TextRecord* text_record) {
  Node* node = text_record->node_;
  const gfx::RectF& rect = text_record->root_visual_rect_;
  bool is_attributable = AddPaintedAreaInternal(node, rect);
  if (is_attributable) {
    if (!largest_text_ ||
        largest_text_->recorded_size < text_record->recorded_size) {
      largest_text_ = text_record;
    }
  }
  return is_attributable;
}

bool SoftNavigationContext::AddPaintedArea(ImageRecord* image_record) {
  Node* node = Node::FromDomNodeId(image_record->node_id);
  const gfx::RectF& rect = image_record->root_visual_rect;
  bool is_attributable = AddPaintedAreaInternal(node, rect);
  if (is_attributable) {
    if (!largest_image_ ||
        largest_image_->recorded_size < image_record->recorded_size) {
      largest_image_ = image_record;
    }
  }
  return is_attributable;
}

bool SoftNavigationContext::AddPaintedAreaInternal(Node* node,
                                                   const gfx::RectF& rect) {
  // Stop recording paints once we have next input/scroll.
  if (!first_input_or_scroll_time_.is_null()) {
    return false;
  }

  uint64_t painted_area = rect.size().GetArea();

  if (paint_attribution_mode_ !=
      features::SoftNavigationHeuristicsMode::kPrePaintBasedAttribution) {
    DCHECK(IsNeededForTiming(node));
    if (already_painted_modified_nodes_.Contains(node)) {
      // We are sometimes observing paints for the same node.
      // Until we fix first-contentful-paint-only observation, let's ignore
      // these.
      repainted_area_ += painted_area;
      return false;
    }
    already_painted_modified_nodes_.insert(node);
  }

  painted_area_ += painted_area;
  TRACE_EVENT_INSTANT(
      "loading", "SoftNavigationContext::AttributablePaintInAnimationFrame",
      perfetto::Track::FromPointer(this), "context", this, "nodeId",
      node->GetDomNodeId(), "nodeDebugName", node->DebugName(), "rect_x",
      rect.x(), "rect_y", rect.y(), "rect_width", rect.width(), "rect_height",
      rect.height(), "paintedAreaThisAnimationFrame",
      painted_area_ - painted_area_last_animation_frame_);
  return true;
}

bool SoftNavigationContext::SatisfiesSoftNavNonPaintCriteria() const {
  return HasDomModification() && HasUrl() &&
         !user_interaction_timestamp_.is_null();
}

bool SoftNavigationContext::SatisfiesSoftNavPaintCriteria(
    uint64_t required_paint_area) const {
  return painted_area_ >= required_paint_area;
}

bool SoftNavigationContext::OnPaintFinished() {
  // Reset this with each paint, since the conditions might change.
  known_not_related_parent_ = nullptr;

  auto num_modded_new_nodes =
      num_modified_dom_nodes_ - num_modified_dom_nodes_last_animation_frame_;
  auto num_gced_old_nodes = num_live_nodes_last_animation_frame_ +
                            num_modded_new_nodes - modified_nodes_.size();
  auto new_painted_area = painted_area_ - painted_area_last_animation_frame_;
  auto new_repainted_area =
      repainted_area_ - repainted_area_last_animation_frame_;

  // TODO(crbug.com/353218760): Consider reporting if any of the values change
  // if we have an extra loud tracing debug mode.
  if (num_modded_new_nodes || new_painted_area) {
    TRACE_EVENT_INSTANT("loading", "SoftNavigationContext::OnPaintFinished",
                        perfetto::Track::FromPointer(this), "context", this,
                        "numModdenNewNodes", num_modded_new_nodes,
                        "numGcedOldNodes", num_gced_old_nodes, "newPaintedArea",
                        new_painted_area, "newRepaintedArea",
                        new_repainted_area);
  }

  num_modified_dom_nodes_last_animation_frame_ = num_modified_dom_nodes_;
  num_live_nodes_last_animation_frame_ = modified_nodes_.size();
  painted_area_last_animation_frame_ = painted_area_;
  repainted_area_last_animation_frame_ = repainted_area_;

  return new_painted_area > 0;
}

void SoftNavigationContext::OnInputOrScroll() {
  if (!first_input_or_scroll_time_.is_null()) {
    return;
  }
  // Between interaction and first painted area, we allow other inputs or
  // scrolling to happen.  Once we observe the first paint, we have to constrain
  // to that initial viewport, or else the viewport area and set of candidates
  // gets messy.
  if (!painted_area_) {
    return;
  }
  first_input_or_scroll_time_ = base::TimeTicks::Now();
}

// TODO(crbug.com/419386429): This gets called after each new presentation time
// update, but this might have a range of deficiencies:
//
// 1. Candidate records might get replaced between paint and presentation.
//
// `largest_text_` and `largest_image_` are updated in `AddPaintedArea` from
// Paint stage of rendering. But `UpdateSoftLcpCandidate` is called after we
// receive frame presentation time feedback (via `PaintTimingMixin`). It is
// possible that we replace the current largest* paint record with a "pending"
// candidate, but unrelated to the presentation feedback of this
// `UpdateSoftLcpCandidate`. We should only report fully recorded paint records.
// One option is to manage a largest pending/painted recortd (like LCP
// calculator), or, just skip this next step if the candidates aren't done.
//
// 2. We might not be ready to Emit LCP candidates yet, and we might not get
// another chance later.
//
// Right now we will skip emitting LCP candidates until after soft-navigation
// entry and NavigationID are incremented.  But, this might happen after a few
// frames/paints.  Potentially unlikely given the low paint area requirement
// right now, but increasingly likely as we bump that up.
// We might want to also call `UpdateSoftLcpCandidate()` as soon as we emit
// Soft-nav entry if we already have candidates to report.  Similar to above,
// there are concerns with reporting Candidates after Paint but before
// Presentation.
void SoftNavigationContext::UpdateWebExposedLargestContentfulPaintIfNeeded() {
  lcp_calculator_->UpdateWebExposedLargestContentfulPaintIfNeeded(
      largest_text_, largest_image_, true);
}

bool SoftNavigationContext::TryUpdateLcpCandidate() {
  // After we are ready to start measuring LCP (`HasNavigationId()`) and
  // before we want to stop (input or scroll), we update LCP candidate.
  if (!HasNavigationId() || !first_input_or_scroll_time_.is_null()) {
    return false;
  }

  // TODO(crbug.com/425398556): Consider updating `lcp_calculator_` to accept
  // ImageRecord and TextRecord and to extract its own timings/sizes rather than
  // passing them manually here-- similar to how
  // `UpdateWebExposedLargestContentfulPaintIfNeeded` does it.
  bool latest_lcp_details_for_ukm_changed = false;
  // TODO(crbug.com/425989954): Guard on paint_time, because although this
  // TryUpdateLcpCandidate gets called after presentation feedback, it might not
  // be the right presentation time for this specific text/image record.
  if (largest_text_ && !largest_text_->paint_time.is_null()) {
    latest_lcp_details_for_ukm_changed =
        latest_lcp_details_for_ukm_changed ||
        lcp_calculator_->NotifyMetricsIfLargestTextPaintChanged(
            largest_text_->paint_time, largest_text_->recorded_size);
  }
  if (largest_image_ && !largest_image_->paint_time.is_null()) {
    latest_lcp_details_for_ukm_changed =
        latest_lcp_details_for_ukm_changed ||
        lcp_calculator_->NotifyMetricsIfLargestImagePaintChanged(
            largest_image_->paint_time, largest_image_->recorded_size,
            largest_image_, largest_image_->EntropyForLCP(),
            largest_image_->RequestPriority());
  }
  return latest_lcp_details_for_ukm_changed;
}

const LargestContentfulPaintDetails&
SoftNavigationContext::LatestLcpDetailsForUkm() {
  return lcp_calculator_->LatestLcpDetails();
}

void SoftNavigationContext::WriteIntoTrace(
    perfetto::TracedValue context) const {
  perfetto::TracedDictionary dict = std::move(context).WriteDictionary();

  dict.Add("softNavContextId", context_id_);
  dict.Add("navigationId", navigation_id_);
  dict.Add("initialURL", initial_url_);
  dict.Add("mostRecentURL", most_recent_url_);

  dict.Add("interactionTimestamp", user_interaction_timestamp_);
  dict.Add("firstContentfulPaint", first_contentful_paint_);

  dict.Add("domModifications", num_modified_dom_nodes_);
  dict.Add("paintedArea", painted_area_);
  dict.Add("repaintedArea", repainted_area_);
}

void SoftNavigationContext::Trace(Visitor* visitor) const {
  visitor->Trace(modified_nodes_);
  visitor->Trace(already_painted_modified_nodes_);
  visitor->Trace(known_not_related_parent_);
  visitor->Trace(lcp_calculator_);
  visitor->Trace(largest_text_);
  visitor->Trace(largest_image_);
}

}  // namespace blink
