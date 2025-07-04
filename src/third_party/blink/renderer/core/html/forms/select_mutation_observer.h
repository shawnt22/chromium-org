// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_FORMS_SELECT_MUTATION_OBSERVER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_FORMS_SELECT_MUTATION_OBSERVER_H_

#include "third_party/blink/renderer/core/dom/mutation_observer.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/inspector/inspector_audits_issue.h"

namespace blink {

class SelectMutationObserver : public MutationObserver::Delegate {
 public:
  explicit SelectMutationObserver(HTMLSelectElement& select);

  ExecutionContext* GetExecutionContext() const override;
  void Deliver(const MutationRecordVector& records, MutationObserver&) override;
  void Trace(Visitor* visitor) const override;

  void Disconnect();

 private:
  void CheckAddedNodes(MutationRecord* record);
  void CheckRemovedNodes(MutationRecord* record);
  void TraverseNodeDescendants(const Node* node);
  void MaybeAddDescendantTextInput(Node* node);
  void MaybeRemoveDescendantTextInput(Node* node);
  void AddDescendantDisallowedErrorToNode(Node& node);
  String GetMessageForReason(ElementAccessibilityIssueReason issue_reason);
  String FormatElementMessage(const String& element,
                              const String& article,
                              const String& example);
  String FormatInteractiveElementMessage(const String& element,
                                         const String& article,
                                         const String& context);
  bool IsAllowedInteractiveElement(Node& node);
  bool IsInteractiveElement(const Node& node);
  void RecordIssueByType(ElementAccessibilityIssueReason issue_reason);
  ElementAccessibilityIssueReason CheckForIssue(const Node& descendant);
  bool IsAllowedDescendantOfSelect(const Node& descendant, const Node& parent);
  bool IsAllowedDescendantOfOptgroup(const Node& descendant,
                                     const Node& parent);
  bool IsAllowedDescendantOfButton(const Node& descendant);
  ElementAccessibilityIssueReason CheckDescedantOfOption(
      const Node& descendant);
  bool HasTabIndexAttribute(const Node& node);
  bool IsContenteditable(const Node& node);
  ElementAccessibilityIssueReason TraverseAncestorsAndCheckDescendant(
      const Node& descendant);
  bool IsWhitespaceOrEmpty(const Node& node);
  bool IsAllowedPhrasingContent(const Node& node);
  bool IsAutonomousCustomElement(const Node& node);

  Member<HTMLSelectElement> select_;
  Member<MutationObserver> observer_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_FORMS_SELECT_MUTATION_OBSERVER_H_
