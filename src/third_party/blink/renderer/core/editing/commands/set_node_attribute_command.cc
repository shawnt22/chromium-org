/*
 * Copyright (C) 2005, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/editing/commands/set_node_attribute_command.h"

#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/platform/wtf/text/strcat.h"

namespace blink {

SetNodeAttributeCommand::SetNodeAttributeCommand(Element* element,
                                                 const QualifiedName& attribute,
                                                 const AtomicString& value)
    : SimpleEditCommand(element->GetDocument()),
      element_(element),
      attribute_(attribute),
      value_(value) {
  DCHECK(element_);
}

void SetNodeAttributeCommand::DoApply(EditingState*) {
  old_value_ = element_->getAttribute(attribute_);
  element_->setAttribute(attribute_, value_);
}

void SetNodeAttributeCommand::DoUnapply() {
  element_->setAttribute(attribute_, old_value_);
  old_value_ = g_null_atom;
}

String SetNodeAttributeCommand::ToString() const {
  return StrCat(
      {"SetNodeAttributeCommand {attribute:", attribute_.ToString(), "}"});
}

void SetNodeAttributeCommand::Trace(Visitor* visitor) const {
  visitor->Trace(element_);
  SimpleEditCommand::Trace(visitor);
}

}  // namespace blink
