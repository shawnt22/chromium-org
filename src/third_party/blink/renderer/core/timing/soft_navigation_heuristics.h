// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_TIMING_SOFT_NAVIGATION_HEURISTICS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_TIMING_SOFT_NAVIGATION_HEURISTICS_H_

#include <optional>

#include "base/gtest_prod_util.h"
#include "base/memory/stack_allocated.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/paint/timing/lcp_objects.h"
#include "third_party/blink/renderer/core/paint/timing/paint_timing.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/scheduler/public/task_attribution_tracker.h"
#include "third_party/blink/renderer/platform/wtf/hash_set.h"

namespace blink {
namespace scheduler {
class TaskAttributionInfo;
}  // namespace scheduler

class ScriptState;
class SoftNavigationContext;
class SoftNavigationPaintAttributionTracker;

// This class contains the logic for calculating Single-Page-App soft navigation
// heuristics. See https://github.com/WICG/soft-navigations
class CORE_EXPORT SoftNavigationHeuristics
    : public GarbageCollected<SoftNavigationHeuristics>,
      public scheduler::TaskAttributionTracker::Observer {
 public:
  FRIEND_TEST_ALL_PREFIXES(SoftNavigationHeuristicsTest,
                           EarlyReturnOnInvalidPendingInteractionTimestamp);

  // This class defines a scope that would cover click or navigation related
  // events, in order for the SoftNavigationHeuristics class to be able to keep
  // track of them and their descendant tasks.
  class CORE_EXPORT EventScope {
    STACK_ALLOCATED();

   public:
    enum class Type {
      kKeydown,
      kKeypress,
      kKeyup,
      kClick,
      kNavigate,
      kLast = kNavigate
    };

    ~EventScope();

    EventScope(EventScope&&);
    EventScope& operator=(EventScope&&);

   private:
    using ObserverScope = scheduler::TaskAttributionTracker::ObserverScope;
    using TaskScope = scheduler::TaskAttributionTracker::TaskScope;

    friend class SoftNavigationHeuristics;

    EventScope(SoftNavigationHeuristics*,
               std::optional<ObserverScope>,
               std::optional<TaskScope>,
               Type,
               bool is_nested);

    SoftNavigationHeuristics* heuristics_;
    std::optional<ObserverScope> observer_scope_;
    std::optional<TaskScope> task_scope_;
    Type type_;
    bool is_nested_;
  };

  explicit SoftNavigationHeuristics(LocalDOMWindow* window);
  virtual ~SoftNavigationHeuristics() = default;

  static SoftNavigationHeuristics* CreateIfNeeded(LocalDOMWindow*);

  // Inform `SoftNavigationHeuristics` that `inserted_node` was inserted into
  // `container_node`. Sets up paint tracking if the modification is
  // attributable to a `SoftNavigationContext` and connected to the DOM.
  static void InsertedNode(Node* inserted_node, Node* container_node);

  // Inform `SoftNavigationHeuristics` that `node` was modified in some way.
  // Sets up paint tracking if the modification is attributable to a
  // `SoftNavigationContext` and connected to the DOM.
  static void ModifiedNode(Node* node);

  // GarbageCollected boilerplate.
  void Trace(Visitor*) const override;

  void Shutdown();

  // The class's API.

  // Returns an id to be used for retrieving the associated task state during
  // commit, or nullopt if no `SoftNavigationContext` is associated with the
  // navigation.
  std::optional<scheduler::TaskAttributionId>
  AsyncSameDocumentNavigationStarted();

  void SameDocumentNavigationCommitted(const String& url,
                                       SoftNavigationContext*);
  bool ModifiedDOM(Node* node);
  uint32_t SoftNavigationCount() { return soft_navigation_count_; }

  // TaskAttributionTracker::Observer's implementation.
  void OnCreateTaskScope(scheduler::TaskAttributionInfo&) override;

  SoftNavigationContext* MaybeGetSoftNavigationContextForTiming(Node* node);
  void OnPaintFinished();
  void OnInputOrScroll();
  OptionalPaintTimingCallback TakePaintTimingCallback();
  void UpdateSoftLcpCandidate();

  const LargestContentfulPaintDetails&
  SoftNavigationLargestContentfulPaintDetailsForMetrics() const {
    return soft_navigation_lcp_details_for_metrics_;
  }

  // Returns an `EventScope` suitable for navigation. Used for navigations not
  // yet associated with an event.
  EventScope CreateNavigationEventScope(ScriptState* script_state) {
    return CreateEventScope(EventScope::Type::kNavigate, script_state);
  }

  // Returns an `EventScope` for the given `Event` if the event is relevant to
  // soft navigation tracking, otherwise it returns nullopt.
  std::optional<EventScope> MaybeCreateEventScopeForEvent(const Event&);

  SoftNavigationPaintAttributionTracker* GetPaintAttributionTracker() {
    CHECK_EQ(IsPrePaintBasedAttributionEnabled(), !!paint_attribution_tracker_);
    return paint_attribution_tracker_.Get();
  }

  // This method is called during the weakness processing stage of garbage
  // collection to remove items from `potential_soft_navigations_`.
  void ProcessCustomWeakness(const LivenessBroker& info);

 private:
  void ReportSoftNavigationToMetrics(SoftNavigationContext*) const;
  void SetIsTrackingSoftNavigationHeuristicsOnDocument(bool value) const;

  // We can grab a context from the "running task", or sometimes from other
  // scheduling sources-- but these can leak across windows.
  // Any time we retrieve a context, we should check to ensure that these were
  // created for this window (i.e. by this SNH instance).
  SoftNavigationContext* EnsureContextForCurrentWindow(
      SoftNavigationContext*) const;
  SoftNavigationContext* GetSoftNavigationContextForCurrentTask() const;

  void EmitSoftNavigationEntryIfAllConditionsMet(SoftNavigationContext*);
  LocalFrame* GetLocalFrameIfOutermostAndNotDetached() const;
  void OnSoftNavigationEventScopeDestroyed(const EventScope&);
  EventScope CreateEventScope(EventScope::Type type, ScriptState*);
  uint64_t CalculateRequiredPaintArea() const;
  uint64_t CalculateViewportArea() const;

  bool IsPrePaintBasedAttributionEnabled() const {
    return paint_attribution_mode_ ==
           features::SoftNavigationHeuristicsMode::kPrePaintBasedAttribution;
  }

  Member<LocalDOMWindow> window_;

  // The set of ongoing potential soft navigations. `SoftNavigationContext`
  // objects are added when they are the active context during an event handler
  // running in an `EventScope`. Entries are stored as untraced members to do
  // custom weak processing (see `ProcessCustomWeakness()`).
  HashSet<UntracedMember<SoftNavigationContext>> potential_soft_navigations_;

  // The `SoftNavigationContext` of the "active interaction", if any.
  //
  // This is set to a new `SoftNavigationContext` when
  //   1. an `EventScope` is created for a new interaction (click, navigation,
  //      and keydown) and there isn't already an active `EventScope` on the
  //      stack for this `SoftNavigationHeuristics`. Note that the latter
  //      restriction causes the same context to be reused for nested
  //      `EventScope`s, which occur when the navigate event occurs within the
  //      scope of the input event.
  //
  //   2. an `EventScope` is created for a non-new interaction (keypress, keyup)
  //      and `active_interaction_context_` isn't set. These events typically
  //      follow a keydown, in which case the context created for that will be
  //      reused, but the context can be cleared if, for example, a click
  //      happens while a key is held.
  //
  // This is cleared when the outermost `EventScope` is destroyed if the scope
  // type is click or navigate. For keyboard events, which have multiple related
  // events, this remains alive until the next interaction.
  Member<SoftNavigationContext> active_interaction_context_;

  // Save a strong reference to the most recent context that changed URL.  This
  // context could still be pending (not emitted) as we wait to observe more
  // paints, or it might have already been emitted, but we still want to
  // continue measuring paints for a while.
  Member<SoftNavigationContext> context_for_current_url_;

  // Save a strong reference to the most recent context that painted for the
  // first time, and needs an FCP presentation callback.  This will be picked
  // up by PaintTimingMixin, cleared, but held strongly until presententation
  // feedback.  Soft-navigation entries are not reported to the performance
  // timeline until after FCP is measured.
  // TODO(crbug.com/424448145): Needs some changes:
  // - measure first paint update, not the update after criteria met.
  // - measure first paint of first contentful candidate, not fully loaded
  // paint.
  // - support multiple context in a single animation frame, rather than
  // single value here.  Will become more important when all interactions
  // measure paint.
  Member<SoftNavigationContext> context_for_first_contentful_paint_;

  // Used to map DOM modifications to `SoftNavigationContext`s for paint
  // attribution. Only set when `IsPrePaintBasedAttributionEnabled()` is true.
  Member<SoftNavigationPaintAttributionTracker> paint_attribution_tracker_;

  uint32_t soft_navigation_count_ = 0;
  bool has_active_event_scope_ = false;

  const features::SoftNavigationHeuristicsMode paint_attribution_mode_;
  // `task_attribution_tracker_` is cleared during `Shutdown()` (frame detach),
  // which should happen before the tracker is destroyed, since its lifetime is
  // tied to the lifetime of the isolate/main thread.
  scheduler::TaskAttributionTracker* task_attribution_tracker_;

  // The soft navigation LCP details reported to metrics (UKM).
  LargestContentfulPaintDetails soft_navigation_lcp_details_for_metrics_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_TIMING_SOFT_NAVIGATION_HEURISTICS_H_
