// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ACTOR_ACTOR_TEST_UTIL_H_
#define CHROME_BROWSER_ACTOR_ACTOR_TEST_UTIL_H_

#include "base/metrics/field_trial_params.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/common/actor.mojom-forward.h"
#include "components/optimization_guide/proto/features/actions_data.pb.h"
#include "ui/gfx/geometry/point.h"

namespace base {
class CommandLine;
}  // namespace base

namespace content {
class RenderFrameHost;
}  // namespace content

namespace actor {

optimization_guide::proto::BrowserAction MakeClick(
    content::RenderFrameHost& rfh,
    int content_node_id);
optimization_guide::proto::BrowserAction MakeClick(
    const gfx::Point& click_point);
optimization_guide::proto::BrowserAction MakeHistoryBack();
optimization_guide::proto::BrowserAction MakeHistoryForward();
optimization_guide::proto::BrowserAction MakeMouseMove(
    content::RenderFrameHost& rfh,
    int content_node_id);
optimization_guide::proto::BrowserAction MakeMouseMove(
    const gfx::Point& move_point);
optimization_guide::proto::BrowserAction MakeNavigate(
    std::string_view target_url);
optimization_guide::proto::BrowserAction MakeType(content::RenderFrameHost& rfh,
                                                  int content_node_id,
                                                  std::string_view text,
                                                  bool follow_by_enter);
optimization_guide::proto::BrowserAction MakeType(const gfx::Point& type_point,
                                                  std::string_view text,
                                                  bool follow_by_enter);
optimization_guide::proto::BrowserAction MakeSelect(
    content::RenderFrameHost& rfh,
    int content_node_id,
    std::string_view value);

optimization_guide::proto::BrowserAction MakeScroll(
    content::RenderFrameHost& rfh,
    std::optional<int> content_node_id,
    float scroll_offset_x,
    float scroll_offset_y);
optimization_guide::proto::BrowserAction MakeDragAndRelease(
    const gfx::Point& from_point,
    const gfx::Point& to_point);
optimization_guide::proto::BrowserAction MakeWait();

void ExpectOkResult(base::test::TestFuture<mojom::ActionResultPtr>& future);
void ExpectErrorResult(base::test::TestFuture<mojom::ActionResultPtr>& future,
                       mojom::ActionResultCode expected_code);

// Sets up GLIC_ACTION_PAGE_BLOCK to block the given host.
void SetUpBlocklist(base::CommandLine* command_line,
                    const std::string& blocked_host);

}  // namespace actor

#endif  // CHROME_BROWSER_ACTOR_ACTOR_TEST_UTIL_H_
