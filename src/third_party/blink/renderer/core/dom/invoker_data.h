// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_INVOKER_DATA_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_INVOKER_DATA_H_

#include "base/check_op.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/element_rare_data_field.h"
#include "third_party/blink/renderer/core/html/closewatcher/close_watcher.h"
#include "third_party/blink/renderer/core/html/forms/html_form_control_element.h"
#include "third_party/blink/renderer/core/html_element_type_helpers.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"

namespace blink {

// The InvokerData class stores info for invokers, which includes:
//   - elements with the commandfor attribute
//   - elements with the popovertarget attribute
//   - elements with the interestfor attribute
class InvokerData final : public GarbageCollected<InvokerData>,
                          public ElementRareDataField {
 public:
  InvokerData() = default;
  InvokerData(const InvokerData&) = delete;
  InvokerData& operator=(const InvokerData&) = delete;

  HTMLElement* GetInvokedPopover() const { return invoked_popover_; }
  void SetInvokedPopover(HTMLElement* popover) {
    CHECK(!popover || popover->popoverOpen());
    invoked_popover_ = popover;
  }

  Element::InterestState GetInterestState() const { return interest_state_; }
  void SetInterestState(Element::InterestState new_state) {
    interest_state_ = new_state;
  }

  Element* ActiveInterestTarget() const { return active_interest_target_; }
  void SetActiveInterestTarget(Element* new_target) {
    CHECK(new_target == active_interest_target_ ||
          !new_target != !active_interest_target_)
        << "Active target must be cleared before being changed";
    active_interest_target_ = new_target;
  }

  bool HasInterestGainedTask() const {
    return interest_gained_task_.IsActive();
  }
  void CancelInterestGainedTask() { interest_gained_task_.Cancel(); }
  void SetInterestGainedTask(TaskHandle&& task) {
    DCHECK(
        RuntimeEnabledFeatures::HTMLInterestForAttributeEnabledByRuntimeFlag());
    DCHECK(!interest_gained_task_.IsActive());
    interest_gained_task_ = std::move(task);
  }

  bool HasInterestLostTask() const { return interest_lost_task_.IsActive(); }
  void CancelInterestLostTask() { interest_lost_task_.Cancel(); }
  void SetInterestLostTask(TaskHandle&& task) {
    DCHECK(
        RuntimeEnabledFeatures::HTMLInterestForAttributeEnabledByRuntimeFlag());
    DCHECK(!interest_lost_task_.IsActive());
    interest_lost_task_ = std::move(task);
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(invoked_popover_);
    visitor->Trace(active_interest_target_);
    ElementRareDataField::Trace(visitor);
  }

 private:
  TaskHandle interest_gained_task_;
  TaskHandle interest_lost_task_;
  Element::InterestState interest_state_{Element::InterestState::kNoInterest};
  Member<HTMLElement> invoked_popover_;
  Member<Element> active_interest_target_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_INVOKER_DATA_H_
