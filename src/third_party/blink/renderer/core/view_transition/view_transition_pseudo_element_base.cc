// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/view_transition/view_transition_pseudo_element_base.h"

#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/view_transition/view_transition_style_tracker.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

ViewTransitionPseudoElementBase::ViewTransitionPseudoElementBase(
    Element* parent,
    PseudoId pseudo_id,
    const AtomicString& view_transition_name,
    bool is_generated_name,
    const ViewTransitionStyleTracker* style_tracker)
    : PseudoElement(parent, pseudo_id, view_transition_name),
      style_tracker_(style_tracker) {
  DCHECK(IsTransitionPseudoElement(pseudo_id));
  DCHECK(pseudo_id == kPseudoIdViewTransition || view_transition_name);
  DCHECK(style_tracker_);
  SetIsGeneratedName(is_generated_name);
}

bool ViewTransitionPseudoElementBase::CanGeneratePseudoElement(
    PseudoId pseudo_id) const {
  switch (GetPseudoId()) {
    case kPseudoIdViewTransition:
      return pseudo_id == kPseudoIdViewTransitionGroup;
    case kPseudoIdViewTransitionGroup:
      return pseudo_id == kPseudoIdViewTransitionImagePair ||
             (pseudo_id == kPseudoIdViewTransitionGroupChildren &&
              RuntimeEnabledFeatures::NestedViewTransitionEnabled());
    case kPseudoIdViewTransitionGroupChildren:
      CHECK(RuntimeEnabledFeatures::NestedViewTransitionEnabled());
      return pseudo_id == kPseudoIdViewTransitionGroup;
    case kPseudoIdViewTransitionImagePair:
      return pseudo_id == kPseudoIdViewTransitionOld ||
             pseudo_id == kPseudoIdViewTransitionNew;
    case kPseudoIdViewTransitionOld:
    case kPseudoIdViewTransitionNew:
      return false;
    default:
      NOTREACHED();
  }
}

const Vector<AtomicString>&
ViewTransitionPseudoElementBase::ViewTransitionClassList() const {
  return style_tracker_->GetViewTransitionClassList(view_transition_name());
}

const ComputedStyle*
ViewTransitionPseudoElementBase::CustomStyleForLayoutObject(
    const StyleRecalcContext& style_recalc_context) {
  // Set the parent style to the style of our parent. There is no use
  // for an originating element for a view transition pseudo.
  StyleRequest style_request(
      GetPseudoId(), ParentOrShadowHostElement()->GetComputedStyle(),
      /* originating_element_style */ nullptr, view_transition_name());
  style_request.rules_to_include = style_tracker_->StyleRulesToInclude();
  if (GetPseudoId() != kPseudoIdViewTransition) {
    style_request.pseudo_ident_list =
        style_tracker_->GetViewTransitionClassList(view_transition_name());
  }
  if (RuntimeEnabledFeatures::CSSNestedPseudoElementsEnabled()) {
    style_request.pseudo_id = kPseudoIdNone;
    return StyleForPseudoElement(style_recalc_context, style_request);
  }
  // Use the originating element to get the style for the pseudo-element.
  return UltimateOriginatingElement().StyleForPseudoElement(
      style_recalc_context, style_request);
}

void ViewTransitionPseudoElementBase::Trace(Visitor* visitor) const {
  PseudoElement::Trace(visitor);
  visitor->Trace(style_tracker_);
}

bool ViewTransitionPseudoElementBase::IsBoundTo(
    const blink::ViewTransitionStyleTracker* tracker) const {
  return style_tracker_.Get() == tracker;
}

const Vector<AtomicString>&
ViewTransitionPseudoElementBase::GetViewTransitionNames() const {
  return style_tracker_->GetViewTransitionNames();
}

const Vector<AtomicString>
ViewTransitionPseudoElementBase::GetContainedViewTransitionNames() const {
  return style_tracker_->ComputeContainedGroupNames(view_transition_name());
}

}  // namespace blink
