// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ACTOR_TOOLS_TOOL_H_
#define CHROME_BROWSER_ACTOR_TOOLS_TOOL_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/safe_ref.h"
#include "chrome/browser/actor/task_id.h"
#include "chrome/common/actor.mojom.h"
#include "url/gurl.h"

namespace optimization_guide::proto {
class AnnotatedPageContent;
}  // namespace optimization_guide::proto

namespace actor {

class AggregatedJournal;
class ObservationDelayController;

// Interface all actor tools implement. A tool is held by the ToolController and
// validated and invoked from there. The controller makes no guarantees about
// when the tool will be destroyed.
class Tool {
 public:
  // NOTE: Let's rename this to `ToolCallback`, move to a shared header, and
  // eliminate the other redundant definitions.
  using ValidateCallback = base::OnceCallback<void(mojom::ActionResultPtr)>;
  using InvokeCallback = base::OnceCallback<void(mojom::ActionResultPtr)>;
  Tool(TaskId task_id, AggregatedJournal& journal);
  virtual ~Tool();

  // Not copyable or movable.
  Tool(const Tool& other) = delete;
  Tool(Tool&& other) = delete;

  // Perform any browser-side validation on the tool. The given callback must be
  // invoked by the tool when validation is completed. If the result given to
  // the callback indicates success, the framework will call Invoke. Otherwise,
  // the tool will be destroyed.
  virtual void Validate(ValidateCallback callback) = 0;

  // Perform any synchronous time-of-use checks just before invoking the tool.
  // These are typically TOCTOU (time-of-check/time-of-use) validations that the
  // live state of the page/browser still matches what the client can see as of
  // the last observation snapshot. This is a synchronous check so there are no
  // further opportunities for changes to the live browser state before invoking
  // the tool.
  virtual mojom::ActionResultPtr TimeOfUseValidation(
      const optimization_guide::proto::AnnotatedPageContent* last_observation);

  // Perform the action of the tool. The given callback must be invoked when the
  // tool has finished its actions.
  virtual void Invoke(InvokeCallback callback) = 0;

  // Provides a human readable description of the tool useful for log and
  // debugging purposes.
  virtual std::string DebugString() const = 0;

  // Provides the URL to be recorded in journal entries for this tool. This can
  // be an empty URL for tool not associated with a tab/frame or if the
  // tab/frame is no longer available.
  virtual GURL JournalURL() const;

  // Provides a journal event name.
  virtual std::string JournalEvent() const = 0;

  // Returns an optional delay object that can be used to delay completion of
  // the tool until some external conditions are met, typically waiting on a
  // loading navigation to settle.
  virtual std::unique_ptr<ObservationDelayController> GetObservationDelayer()
      const = 0;

 protected:
  TaskId task_id() const { return task_id_; }
  AggregatedJournal& journal() { return *journal_; }

 private:
  TaskId task_id_;
  base::SafeRef<AggregatedJournal> journal_;
};

}  // namespace actor

#endif  // CHROME_BROWSER_ACTOR_TOOLS_TOOL_H_
