// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_ACTOR_SCROLL_TOOL_H_
#define CHROME_RENDERER_ACTOR_SCROLL_TOOL_H_

#include <cstdint>

#include "base/memory/raw_ref.h"
#include "base/types/expected.h"
#include "chrome/common/actor.mojom.h"
#include "chrome/renderer/actor/tool_base.h"
#include "third_party/blink/public/web/web_element.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace content {
class RenderFrame;
}  // namespace content

namespace actor {

// A tool that can be invoked to perform a scroll over a target.
class ScrollTool : public ToolBase {
 public:
  ScrollTool(content::RenderFrame& frame,
             Journal::TaskId task_id,
             Journal& journal,
             mojom::ScrollActionPtr action);

  ~ScrollTool() override;

  // actor::ToolBase
  mojom::ActionResultPtr Execute() override;
  std::string DebugString() const override;
  base::TimeDelta ExecutionObservationDelay() const override;

 private:
  struct ScrollerAndDistance {
    blink::WebElement scroller;
    gfx::Vector2dF scroll_by_offset;
  };
  using ValidatedResult =
      base::expected<ScrollerAndDistance, mojom::ActionResultPtr>;

  ValidatedResult Validate() const;

  bool targeting_smooth_scroller_ = false;

  mojom::ScrollActionPtr action_;
};

}  // namespace actor

#endif  // CHROME_RENDERER_ACTOR_SCROLL_TOOL_H_
