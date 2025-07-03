// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/actor/select_tool.h"

#include <cstdint>
#include <optional>

#include "base/check.h"
#include "base/notimplemented.h"
#include "chrome/common/actor/action_result.h"
#include "chrome/common/actor/actor_logging.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/actor/tool_utils.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/public/web/web_option_element.h"
#include "third_party/blink/public/web/web_select_element.h"

namespace actor {

using blink::WebElement;
using blink::WebNode;
using blink::WebOptionElement;
using blink::WebSelectElement;
using blink::WebString;

SelectTool::SelectTool(content::RenderFrame& frame,
                       Journal::TaskId task_id,
                       Journal& journal,
                       mojom::SelectActionPtr action)
    : ToolBase(frame, task_id, journal), action_(std::move(action)) {}

SelectTool::~SelectTool() = default;

mojom::ActionResultPtr SelectTool::Execute() {
  ValidatedResult validated_result = Validate();
  if (!validated_result.has_value()) {
    return std::move(validated_result.error());
  }

  WebSelectElement select = validated_result.value().select;
  WebString value = validated_result.value().option_value;
  select.SetValue(value, /*send_events=*/true);

  // Check if the set value is now the current value in the <select>
  if (select.Value() != value) {
    return MakeResult(
        mojom::ActionResultCode::kSelectUnexpectedValue,
        absl::StrFormat("ValueAfter [%s]", select.Value().Utf8()));
  }

  return MakeOkResult();
}

std::string SelectTool::DebugString() const {
  return absl::StrFormat("SelectTool[%s;value(%s)]",
                         ToDebugString(action_->target), action_->value);
}

SelectTool::ValidatedResult SelectTool::Validate() const {
  CHECK(frame_->GetWebFrame());
  CHECK(frame_->GetWebFrame()->FrameWidget());

  mojom::ToolTargetPtr& target = action_->target;

  if (target->is_coordinate()) {
    NOTIMPLEMENTED() << "Coordinate-based target is not yet supported.";
    return base::unexpected(MakeErrorResult());
  }

  int32_t dom_node_id = target->get_dom_node_id();

  WebNode node = GetNodeFromId(frame_.get(), dom_node_id);
  if (node.IsNull()) {
    return base::unexpected(
        MakeResult(mojom::ActionResultCode::kInvalidDomNodeId));
  }

  WebSelectElement select = node.DynamicTo<WebSelectElement>();
  if (!select) {
    return base::unexpected(
        MakeResult(mojom::ActionResultCode::kSelectInvalidElement,
                   absl::StrFormat("Element [%s]", base::ToString(node))));
  }

  if (!select.IsEnabled()) {
    return base::unexpected(
        MakeResult(mojom::ActionResultCode::kElementDisabled,
                   absl::StrFormat("Element [%s]", base::ToString(select))));
  }

  WebString value(WebString::FromUTF8(action_->value));
  for (const auto& e : select.GetListItems()) {
    auto option = e.DynamicTo<WebOptionElement>();
    if (option && option.Value() == value) {
      if (!option.IsEnabled()) {
        return base::unexpected(MakeResult(
            mojom::ActionResultCode::kSelectOptionDisabled,
            absl::StrFormat("SelectElement[%s] OptionElement [%s]",
                            base::ToString(select), base::ToString(option))));
      }
      return TargetAndValue{select, value};
    }
  }

  return base::unexpected(
      MakeResult(mojom::ActionResultCode::kSelectNoSuchOption,
                 absl::StrFormat("SelectElement[%s]", base::ToString(select))));
}

}  // namespace actor
