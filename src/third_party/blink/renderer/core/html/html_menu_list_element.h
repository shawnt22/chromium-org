// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_HTML_MENU_LIST_ELEMENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_HTML_MENU_LIST_ELEMENT_H_

#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/menu_item_list.h"

namespace blink {

class HTMLMenuListElement final : public HTMLElement {
  DEFINE_WRAPPERTYPEINFO();

 public:
  explicit HTMLMenuListElement(Document&);

  bool IsValidBuiltinCommand(HTMLElement& invoker,
                             CommandEventType command) override;
  // This returns an iterable list of menuitems whose owner is this.
  MenuItemList GetItemList() const { return MenuItemList(*this); }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_HTML_MENU_LIST_ELEMENT_H_
