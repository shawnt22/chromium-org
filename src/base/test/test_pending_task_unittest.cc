// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/test_pending_task.h"

#include "base/functional/bind.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/traced_value.h"
#include "base/tracing_buildflags.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest-spi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

TEST(TestPendingTaskTest, TraceSupport) {
  base::TestPendingTask task;

  // Check that TestPendingTask can be sent to the trace subsystem.
  TRACE_EVENT1("test", "TestPendingTask::TraceSupport", "task", task.AsValue());

  // Just a basic check that the trace output has *something* in it.
  base::trace_event::TracedValueJSON task_value;
  task.AsValueInto(&task_value);
  EXPECT_THAT(task_value.ToJSON(), ::testing::HasSubstr("post_time"));
}

TEST(TestPendingTaskTest, ToString) {
  base::TestPendingTask task;

  // Just a basic check that ToString has *something* in it.
  EXPECT_THAT(task.ToString(), ::testing::StartsWith("TestPendingTask("));
}

TEST(TestPendingTaskTest, GTestPrettyPrint) {
  base::TestPendingTask task;

  // Check that gtest is calling the TestPendingTask's PrintTo method.
  EXPECT_THAT(::testing::PrintToString(task),
              ::testing::StartsWith("TestPendingTask("));

  // Check that pretty printing works with the gtest iostreams operator.
  EXPECT_NONFATAL_FAILURE(EXPECT_TRUE(false) << task, "TestPendingTask(");
}

TEST(TestPendingTaskTest, ShouldRunBefore) {
  base::TestPendingTask task_first;
  task_first.delay = base::Milliseconds(1);
  base::TestPendingTask task_after;
  task_after.delay = base::Milliseconds(2);

  EXPECT_FALSE(task_after.ShouldRunBefore(task_first))
      << task_after << ".ShouldRunBefore(" << task_first << ")\n";
  EXPECT_TRUE(task_first.ShouldRunBefore(task_after))
      << task_first << ".ShouldRunBefore(" << task_after << ")\n";
}

}  // namespace base
