// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_TIMING_SOFT_NAVIGATION_CONTEXT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_TIMING_SOFT_NAVIGATION_CONTEXT_H_

#include <cstdint>

#include "base/time/time.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/timing/performance_entry.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "ui/gfx/geometry/rect_f.h"

namespace blink {

class Node;
class TextRecord;
class ImageRecord;
class LargestContentfulPaintCalculator;
struct LargestContentfulPaintDetails;

class CORE_EXPORT SoftNavigationContext
    : public GarbageCollected<SoftNavigationContext> {
  static uint64_t last_context_id_;

 public:
  explicit SoftNavigationContext(LocalDOMWindow& window,
                                 features::SoftNavigationHeuristicsMode);

  bool IsMostRecentlyCreatedContext() const {
    return context_id_ == last_context_id_;
  }

  bool HasNavigationId() const { return !navigation_id_.empty(); }
  String GetNavigationId() const { return navigation_id_; }
  void SetNavigationId(String navigation_id) { navigation_id_ = navigation_id; }

  base::TimeTicks UserInteractionTimestamp() const {
    return user_interaction_timestamp_;
  }
  void SetUserInteractionTimestamp(base::TimeTicks value) {
    user_interaction_timestamp_ = value;
  }
  bool HasFirstContentfulPaint() const {
    return !first_contentful_paint_.is_null();
  }
  base::TimeTicks FirstContentfulPaint() const {
    return first_contentful_paint_;
  }
  void SetFirstContentfulPaint(
      const base::TimeTicks& raw_presentation_timestamp,
      const DOMPaintTimingInfo& paint_timing_info) {
    CHECK(first_contentful_paint_.is_null());
    first_contentful_paint_ = raw_presentation_timestamp;
    first_contentful_paint_timing_info_ = paint_timing_info;
  }

  // First Url and Last Url help for cases with multiple client-side redirects.
  const String& InitialUrl() const { return initial_url_; }
  void AddUrl(const String& url) {
    if (initial_url_.empty()) {
      initial_url_ = url;
    }
    most_recent_url_ = url;
  }
  bool HasUrl() const { return !initial_url_.empty(); }

  void AddModifiedNode(Node* node);
  // Returns true if this paint updated the attributed area, and so we should
  // check for sufficient paints to emit a soft-nav entry.
  bool HasDomModification() const { return num_modified_dom_nodes_ > 0; }

  uint64_t PaintedArea() const { return painted_area_; }
  uint64_t ContextId() const { return context_id_; }

  // Returns true if this Context is involved in modifying the container root
  // for this Node*.
  bool IsNeededForTiming(Node* node);
  // Reports a new contentful paint area to this context, and the Node painted.
  bool AddPaintedArea(TextRecord*);
  bool AddPaintedArea(ImageRecord*);
  // Returns true if we update the total attributed area this animation frame.
  // Used to check if it is worthwhile to call `SatisfiesSoftNavPaintCriteria`.
  bool OnPaintFinished();
  void OnInputOrScroll();
  bool TryUpdateLcpCandidate();
  void UpdateWebExposedLargestContentfulPaintIfNeeded();
  const LargestContentfulPaintDetails& LatestLcpDetailsForUkm();

  bool SatisfiesSoftNavNonPaintCriteria() const;
  bool SatisfiesSoftNavPaintCriteria(uint64_t required_paint_area) const;

  bool IsRecordingLargestContentfulPaint() const {
    return first_input_or_scroll_time_.is_null();
  }

  void WriteIntoTrace(perfetto::TracedValue context) const;

  void Trace(Visitor* visitor) const;

 private:
  bool AddPaintedAreaInternal(Node* node, const gfx::RectF& rect);

  // Pre-Increment `last_context_id_` such that the newest context uses the
  // largest value and can be used to identify the most recent context.
  const uint64_t context_id_ = ++last_context_id_;

  String navigation_id_;
  const features::SoftNavigationHeuristicsMode paint_attribution_mode_;

  base::TimeTicks user_interaction_timestamp_;
  base::TimeTicks first_input_or_scroll_time_;
  base::TimeTicks first_contentful_paint_;
  DOMPaintTimingInfo first_contentful_paint_timing_info_;

  String initial_url_;
  String most_recent_url_;

  blink::HeapHashSet<WeakMember<Node>> modified_nodes_;
  blink::HeapHashSet<WeakMember<Node>> already_painted_modified_nodes_;

  Member<LargestContentfulPaintCalculator> lcp_calculator_;
  Member<TextRecord> largest_text_;
  Member<ImageRecord> largest_image_;

  // Elements of `modified_nodes_` can get GC-ed, so we need to keep a count of
  // the total nodes modified.
  size_t num_modified_dom_nodes_ = 0;
  uint64_t painted_area_ = 0;
  uint64_t repainted_area_ = 0;

  size_t num_modified_dom_nodes_last_animation_frame_ = 0;
  size_t num_live_nodes_last_animation_frame_ = 0;
  uint64_t painted_area_last_animation_frame_ = 0;
  uint64_t repainted_area_last_animation_frame_ = 0;

  WeakMember<Node> known_not_related_parent_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_TIMING_SOFT_NAVIGATION_CONTEXT_H_
