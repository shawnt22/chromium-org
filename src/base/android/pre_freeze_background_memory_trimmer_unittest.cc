// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/pre_freeze_background_memory_trimmer.h"

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/utsname.h>

#include <optional>

#include "base/android/self_compaction_manager.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_file.h"
#include "base/memory/page_size.h"
#include "base/task/thread_pool.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base::android {

namespace {

static int s_counter = 0;

void ResetGlobalCounter() {
  s_counter = 0;
}

void IncGlobalCounter() {
  s_counter++;
}

void DecGlobalCounter() {
  s_counter--;
}

void PostDelayedIncGlobal() {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(10));
}

class MockMetric final
    : public PreFreezeBackgroundMemoryTrimmer::PreFreezeMetric {
 public:
  MockMetric() : PreFreezeBackgroundMemoryTrimmer::PreFreezeMetric("Mock") {
    count_++;
  }
  std::optional<uint64_t> Measure() const override { return 0; }
  static size_t count_;

  ~MockMetric() override { count_--; }
};

size_t MockMetric::count_ = 0;

std::optional<const debug::MappedMemoryRegion> GetMappedMemoryRegion(
    uintptr_t addr) {
  std::vector<debug::MappedMemoryRegion> regions;

  std::string proc_maps;
  if (!debug::ReadProcMaps(&proc_maps) || !ParseProcMaps(proc_maps, &regions)) {
    return std::nullopt;
  }

  for (const auto& region : regions) {
    if (region.start <= addr && addr < region.end) {
      return region;
    }
  }

  return std::nullopt;
}

std::optional<const debug::MappedMemoryRegion> GetMappedMemoryRegion(
    void* addr) {
  return GetMappedMemoryRegion(reinterpret_cast<uintptr_t>(addr));
}

size_t CountResidentPagesInRange(void* addr, size_t size) {
  DCHECK((reinterpret_cast<uintptr_t>(addr) & (base::GetPageSize() - 1)) == 0);
  DCHECK(size % base::GetPageSize() == 0);

  std::vector<unsigned char> pages(size / base::GetPageSize());

  mincore(addr, size, &pages[0]);
  size_t tmp = 0;
  for (const auto& v : pages) {
    tmp += v & 0x01;
  }

  return tmp;
}

}  // namespace

class PreFreezeBackgroundMemoryTrimmerTest : public testing::Test {
 public:
  void SetUp() override {
    PreFreezeBackgroundMemoryTrimmer::SetSupportsModernTrimForTesting(true);
    PreFreezeBackgroundMemoryTrimmer::ClearMetricsForTesting();
    ResetGlobalCounter();
  }

 protected:
  size_t pending_task_count() {
    return PreFreezeBackgroundMemoryTrimmer::Instance()
        .GetNumberOfPendingBackgroundTasksForTesting();
  }

  bool did_register_tasks() {
    return PreFreezeBackgroundMemoryTrimmer::Instance()
        .DidRegisterTasksForTesting();
  }

  size_t measurements_count() {
    return PreFreezeBackgroundMemoryTrimmer::Instance()
        .GetNumberOfKnownMetricsForTesting();
  }

  size_t values_before_count() {
    return PreFreezeBackgroundMemoryTrimmer::Instance()
        .GetNumberOfValuesBeforeForTesting();
  }

  test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

class PreFreezeSelfCompactionTest : public testing::Test {
 public:
  void SetUp() override { SelfCompactionManager::ResetCompactionForTesting(); }

  bool ShouldContinueCompaction(base::TimeTicks compaction_started_at) {
    return SelfCompactionManager::Instance().ShouldContinueCompaction(
        compaction_started_at);
  }

  bool CompactionIsSupported() {
    return SelfCompactionManager::CompactionIsSupported();
  }

  std::optional<int64_t> CompactRegion(debug::MappedMemoryRegion region) {
    return SelfCompactionManager::CompactRegion(region);
  }

  // |size| is in bytes.
  void* Map(size_t size) {
    DCHECK_EQ(size % base::GetPageSize(), 0u);
    void* addr = mmap(nullptr, size, PROT_WRITE | PROT_READ,
                      MAP_PRIVATE | MAP_ANON, -1, 0);
    debug::MappedMemoryRegion region;
    region.permissions = debug::MappedMemoryRegion::WRITE |
                         debug::MappedMemoryRegion::READ |
                         debug::MappedMemoryRegion::PRIVATE;
    region.inode = 0;
    region.dev_major = 0;
    region.dev_minor = 0;
    region.start = reinterpret_cast<uintptr_t>(addr);
    region.end = region.start + size;
    mapped_regions_.push_back(region);
    // We memset to guarantee that the memory we just allocated is resident.
    UNSAFE_TODO(memset(addr, 02, size));
    return addr;
  }

  virtual std::string GetMetricName(std::string_view name) {
    return StrCat({"Memory.SelfCompact2.Renderer.", name});
  }

  // |addr| must have been allocated using |Map|. |size| is in bytes.
  void Unmap(void* addr, size_t size) {
    munmap(addr, size);
    std::erase_if(mapped_regions_,
                  [&](const debug::MappedMemoryRegion& region) {
                    return region.start == reinterpret_cast<uintptr_t>(addr);
                  });
  }

  // Returns a copy of the regions that have been allocated via |Map|.
  void GetMappedMemoryRegions(
      std::vector<debug::MappedMemoryRegion>* regions) const {
    *regions = mapped_regions_;
  }

  test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

 private:
  std::vector<debug::MappedMemoryRegion> mapped_regions_;
};

class PreFreezeSelfCompactionTestWithParam
    : public PreFreezeSelfCompactionTest,
      public testing::WithParamInterface<int> {
 public:
  std::unique_ptr<SelfCompactionManager::CompactionState> GetState(
      const base::TimeTicks& triggered_at) {
    auto task_runner = task_environment_.GetMainThreadTaskRunner();
    if (UseRunningCompact()) {
      return SelfCompactionManager::GetRunningCompactionStateForTesting(
          task_runner, triggered_at);
    } else {
      return SelfCompactionManager::GetSelfCompactionStateForTesting(
          task_runner, triggered_at);
    }
  }

  void ExpectTotalCount(std::string_view name, size_t count) {
    histograms_.ExpectTotalCount(GetMetricName(name), count);
  }

  std::string GetMetricName(std::string_view name) override {
    if (UseRunningCompact()) {
      return StrCat({"Memory.RunningCompact.Renderer.", name});
    } else {
      return StrCat({"Memory.SelfCompact2.Renderer.", name});
    }
  }

  base::HistogramTester histograms_;

 private:
  bool UseRunningCompact() {
    switch (GetParam()) {
      case 0:
        return false;
      case 1:
        return true;
      default:
        NOTREACHED();
    }
  }
};

// We do not expect any tasks to be registered with
// PreFreezeBackgroundMemoryTrimmer on Android versions before U.
TEST_F(PreFreezeBackgroundMemoryTrimmerTest, PostTaskPreFreezeUnsupported) {
  PreFreezeBackgroundMemoryTrimmer::SetSupportsModernTrimForTesting(false);

  ASSERT_FALSE(did_register_tasks());

  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(did_register_tasks());

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);
  EXPECT_EQ(s_counter, 1);
}

// TODO(thiabaud): Test that the histograms are recorded too.
TEST_F(PreFreezeBackgroundMemoryTrimmerTest, RegisterMetric) {
  ASSERT_EQ(measurements_count(), 0u);
  ASSERT_EQ(MockMetric::count_, 0u);

  {
    MockMetric mock_metric;

    PreFreezeBackgroundMemoryTrimmer::RegisterMemoryMetric(&mock_metric);

    EXPECT_EQ(MockMetric::count_, 1u);
    EXPECT_EQ(measurements_count(), 1u);

    PreFreezeBackgroundMemoryTrimmer::UnregisterMemoryMetric(&mock_metric);

    // Unregistering does not destroy the metric.
    EXPECT_EQ(MockMetric::count_, 1u);
    EXPECT_EQ(measurements_count(), 0u);
  }

  EXPECT_EQ(MockMetric::count_, 0u);
  EXPECT_EQ(measurements_count(), 0u);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, UnregisterDuringPreFreeze) {
  ASSERT_EQ(measurements_count(), 0u);
  ASSERT_EQ(MockMetric::count_, 0u);

  {
    MockMetric mock_metric;

    PreFreezeBackgroundMemoryTrimmer::RegisterMemoryMetric(&mock_metric);

    EXPECT_EQ(MockMetric::count_, 1u);
    EXPECT_EQ(measurements_count(), 1u);

    // This posts a metrics task.
    PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

    EXPECT_EQ(measurements_count(), 1u);
    EXPECT_EQ(values_before_count(), 1u);

    PreFreezeBackgroundMemoryTrimmer::UnregisterMemoryMetric(&mock_metric);

    // Unregistering does not destroy the metric, but does remove its value
    // from |before_values_|.
    EXPECT_EQ(MockMetric::count_, 1u);
    EXPECT_EQ(measurements_count(), 0u);
    EXPECT_EQ(values_before_count(), 0u);
  }

  EXPECT_EQ(MockMetric::count_, 0u);
  EXPECT_EQ(measurements_count(), 0u);
  EXPECT_EQ(values_before_count(), 0u);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, PostDelayedTaskSimple) {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(30));

  ASSERT_TRUE(did_register_tasks());
  ASSERT_EQ(pending_task_count(), 1u);

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, PostDelayedTaskMultiple) {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(40));

  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 2u);

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 1u);

  EXPECT_EQ(s_counter, 1);

  task_environment_.FastForwardBy(base::Seconds(10));

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 2);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, PostDelayedTaskPreFreeze) {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(60));

  ASSERT_EQ(pending_task_count(), 1u);

  task_environment_.FastForwardBy(base::Seconds(30));

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, PostDelayedTaskMultiThreaded) {
  base::WaitableEvent event1(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
  base::WaitableEvent event2(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto task_runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
  ASSERT_FALSE(task_runner->RunsTasksInCurrentSequence());

  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<base::SequencedTaskRunner> task_runner,
             base::WaitableEvent* event1, base::WaitableEvent* event2) {
            PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
                task_runner, FROM_HERE,
                base::BindOnce(
                    [](base::WaitableEvent* event) {
                      IncGlobalCounter();
                      event->Signal();
                    },
                    base::Unretained(event2)),
                base::Seconds(30));
            event1->Signal();
          },
          task_runner, base::Unretained(&event1), base::Unretained(&event2)));

  task_environment_.FastForwardBy(base::Seconds(1));

  event1.Wait();

  ASSERT_EQ(pending_task_count(), 1u);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  event2.Wait();

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest,
       PostDelayedTaskBeforeAndAfterPreFreeze) {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(60));

  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&IncGlobalCounter), base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 2u);

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 1u);

  EXPECT_EQ(s_counter, 1);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 2);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, AddDuringPreFreeze) {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&PostDelayedIncGlobal), base::Seconds(10));

  ASSERT_EQ(pending_task_count(), 1u);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 1u);
  EXPECT_EQ(s_counter, 0);

  // Fast forward to run the metrics task.
  task_environment_.FastForwardBy(base::Seconds(2));

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, AddDuringPreFreezeRunNormally) {
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindRepeating(&PostDelayedIncGlobal), base::Seconds(10));

  ASSERT_EQ(pending_task_count(), 1u);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 1u);
  EXPECT_EQ(s_counter, 0);

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerNeverStarted) {
  OneShotDelayedBackgroundTimer timer;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  ASSERT_FALSE(did_register_tasks());
  EXPECT_EQ(s_counter, 0);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerFastForward) {
  OneShotDelayedBackgroundTimer timer;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());
  ASSERT_FALSE(did_register_tasks());

  timer.Start(FROM_HERE, base::Seconds(30), base::BindOnce(&IncGlobalCounter));

  ASSERT_EQ(pending_task_count(), 1u);
  ASSERT_TRUE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerOnPreFreeze) {
  OneShotDelayedBackgroundTimer timer;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());
  ASSERT_FALSE(did_register_tasks());

  timer.Start(FROM_HERE, base::Seconds(30), base::BindOnce(&IncGlobalCounter));

  ASSERT_EQ(pending_task_count(), 1u);
  ASSERT_TRUE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  EXPECT_EQ(s_counter, 1);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerStopSingle) {
  OneShotDelayedBackgroundTimer timer;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());
  ASSERT_FALSE(did_register_tasks());

  timer.Start(FROM_HERE, base::Seconds(30), base::BindOnce(&IncGlobalCounter));

  ASSERT_EQ(pending_task_count(), 1u);
  ASSERT_TRUE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  timer.Stop();
  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  EXPECT_EQ(s_counter, 0);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerStopMultiple) {
  OneShotDelayedBackgroundTimer timer;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());
  ASSERT_FALSE(did_register_tasks());

  timer.Start(FROM_HERE, base::Seconds(30), base::BindOnce(&IncGlobalCounter));

  ASSERT_EQ(pending_task_count(), 1u);
  ASSERT_TRUE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  timer.Stop();
  timer.Stop();

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  EXPECT_EQ(s_counter, 0);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerDestroyed) {
  // Add scope here to destroy timer.
  {
    OneShotDelayedBackgroundTimer timer;

    ASSERT_EQ(pending_task_count(), 0u);
    ASSERT_FALSE(timer.IsRunning());
    ASSERT_FALSE(did_register_tasks());

    timer.Start(FROM_HERE, base::Seconds(30),
                base::BindOnce(&IncGlobalCounter));

    ASSERT_EQ(pending_task_count(), 1u);
    ASSERT_TRUE(timer.IsRunning());
    ASSERT_TRUE(did_register_tasks());
  }

  ASSERT_EQ(pending_task_count(), 0u);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);

  EXPECT_EQ(s_counter, 0);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerStartedWhileRunning) {
  IncGlobalCounter();
  ASSERT_EQ(s_counter, 1);

  OneShotDelayedBackgroundTimer timer;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());
  ASSERT_FALSE(did_register_tasks());

  timer.Start(FROM_HERE, base::Seconds(30), base::BindOnce(&IncGlobalCounter));

  ASSERT_EQ(pending_task_count(), 1u);
  ASSERT_TRUE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  timer.Start(FROM_HERE, base::Seconds(10), base::BindOnce(&DecGlobalCounter));

  // Previous task was cancelled, so s_counter should still be 1.
  ASSERT_EQ(s_counter, 1);
  ASSERT_EQ(pending_task_count(), 1u);
  ASSERT_TRUE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());
  ASSERT_TRUE(did_register_tasks());

  // Expect 0 here because we decremented it. The incrementing task was
  // cancelled when we restarted the experiment.
  EXPECT_EQ(s_counter, 0);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, BoolTaskRunDirectly) {
  std::optional<MemoryReductionTaskContext> called_task_type = std::nullopt;
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindOnce(
          [](std::optional<MemoryReductionTaskContext>& called_task_type,
             MemoryReductionTaskContext task_type) {
            called_task_type = task_type;
          },
          std::ref(called_task_type)),
      base::Seconds(30));

  ASSERT_FALSE(called_task_type.has_value());
  ASSERT_EQ(pending_task_count(), 1u);

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);
  EXPECT_EQ(called_task_type.value(),
            MemoryReductionTaskContext::kDelayExpired);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, BoolTaskRunFromPreFreeze) {
  std::optional<MemoryReductionTaskContext> called_task_type = std::nullopt;
  PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
      SingleThreadTaskRunner::GetCurrentDefault(), FROM_HERE,
      base::BindOnce(
          [](std::optional<MemoryReductionTaskContext>& called_task_type,
             MemoryReductionTaskContext task_type) {
            called_task_type = task_type;
          },
          std::ref(called_task_type)),
      base::Seconds(30));

  ASSERT_FALSE(called_task_type.has_value());
  ASSERT_EQ(pending_task_count(), 1u);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);
  EXPECT_EQ(called_task_type.value(), MemoryReductionTaskContext::kProactive);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerBoolTaskRunDirectly) {
  OneShotDelayedBackgroundTimer timer;
  std::optional<MemoryReductionTaskContext> called_task_type = std::nullopt;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  timer.Start(
      FROM_HERE, base::Seconds(30),
      base::BindOnce(
          [](std::optional<MemoryReductionTaskContext>& called_task_type,
             MemoryReductionTaskContext task_type) {
            called_task_type = task_type;
          },
          std::ref(called_task_type)));

  ASSERT_FALSE(called_task_type.has_value());
  ASSERT_EQ(pending_task_count(), 1u);

  task_environment_.FastForwardBy(base::Seconds(30));

  ASSERT_EQ(pending_task_count(), 0u);
  EXPECT_EQ(called_task_type.value(),
            MemoryReductionTaskContext::kDelayExpired);
}

TEST_F(PreFreezeBackgroundMemoryTrimmerTest, TimerBoolTaskRunFromPreFreeze) {
  OneShotDelayedBackgroundTimer timer;
  std::optional<MemoryReductionTaskContext> called_task_type = std::nullopt;

  ASSERT_EQ(pending_task_count(), 0u);
  ASSERT_FALSE(timer.IsRunning());

  timer.Start(
      FROM_HERE, base::Seconds(30),
      base::BindOnce(
          [](std::optional<MemoryReductionTaskContext>& called_task_type,
             MemoryReductionTaskContext task_type) {
            called_task_type = task_type;
          },
          std::ref(called_task_type)));

  ASSERT_FALSE(called_task_type.has_value());
  ASSERT_EQ(pending_task_count(), 1u);

  PreFreezeBackgroundMemoryTrimmer::OnPreFreezeForTesting();

  ASSERT_EQ(pending_task_count(), 0u);
  EXPECT_EQ(called_task_type.value(), MemoryReductionTaskContext::kProactive);
}

INSTANTIATE_TEST_SUITE_P(PreFreezeSelfCompactionTestWithParam,
                         PreFreezeSelfCompactionTestWithParam,
                         testing::Values(0, 1));

TEST_F(PreFreezeSelfCompactionTest, Simple) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  const size_t kPageSize = base::GetPageSize();
  const size_t kNumPages = 24;
  const size_t size = kNumPages * kPageSize;

  void* addr = nullptr;
  addr = mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANON, -1,
              0);
  ASSERT_NE(addr, MAP_FAILED);

  // we touch the memory here to dirty it, so that it is definitely resident.
  UNSAFE_TODO(memset((void*)addr, 1, size));

  EXPECT_EQ(CountResidentPagesInRange(addr, size), kNumPages);

  const auto region = GetMappedMemoryRegion(addr);
  ASSERT_TRUE(region);

  const auto result = CompactRegion(std::move(*region));
  ASSERT_EQ(result, size);

  EXPECT_EQ(CountResidentPagesInRange(addr, size), 0u);

  munmap(addr, size);
}

TEST_F(PreFreezeSelfCompactionTest, File) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  const size_t kPageSize = base::GetPageSize();
  const size_t kNumPages = 2;
  const size_t size = kNumPages * kPageSize;

  ScopedTempFile file;

  ASSERT_TRUE(file.Create());

  int fd = open(file.path().value().c_str(), O_RDWR);
  ASSERT_NE(fd, -1);

  ASSERT_TRUE(base::WriteFile(file.path(), std::string(size, 1)));

  void* addr = nullptr;
  addr = mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_PRIVATE, fd, 0);
  ASSERT_NE(addr, MAP_FAILED);

  // we touch the memory here to dirty it, so that it is definitely resident.
  UNSAFE_TODO(memset((void*)addr, 2, size));

  EXPECT_EQ(CountResidentPagesInRange(addr, size), kNumPages);

  const auto region = GetMappedMemoryRegion(addr);
  ASSERT_TRUE(region);

  const auto result = CompactRegion(std::move(*region));
  ASSERT_EQ(result, 0);

  EXPECT_EQ(CountResidentPagesInRange(addr, size), kNumPages);

  munmap(addr, size);
}

TEST_F(PreFreezeSelfCompactionTest, Inaccessible) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  const size_t kPageSize = base::GetPageSize();
  const size_t kNumPages = 2;
  const size_t size = kNumPages * kPageSize;

  void* addr = nullptr;
  addr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_NE(addr, MAP_FAILED);

  const auto region = GetMappedMemoryRegion(addr);
  ASSERT_TRUE(region);

  // We expect to not count this region.
  const auto result = CompactRegion(std::move(*region));
  ASSERT_EQ(result, 0);

  munmap(addr, size);
}

TEST_F(PreFreezeSelfCompactionTest, Locked) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  const size_t kPageSize = base::GetPageSize();
  // We use a small number of pages here because Android has a low limit on
  // the max locked size allowed (~64 KiB on many devices).
  const size_t kNumPages = 2;
  const size_t size = kNumPages * kPageSize;

  void* addr = nullptr;
  addr = mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANON, -1,
              0);
  ASSERT_NE(addr, MAP_FAILED);

  ASSERT_EQ(mlock(addr, size), 0);

  // we touch the memory here to dirty it, so that it is definitely resident.
  UNSAFE_TODO(memset((void*)addr, 1, size));

  EXPECT_EQ(CountResidentPagesInRange(addr, size), kNumPages);

  const auto region = GetMappedMemoryRegion(addr);
  ASSERT_TRUE(region);

  const auto result = CompactRegion(std::move(*region));
  ASSERT_EQ(result, 0);

  EXPECT_EQ(CountResidentPagesInRange(addr, size), kNumPages);

  munlock(addr, size);
  munmap(addr, size);
}

TEST_F(PreFreezeSelfCompactionTest, SimpleCancel) {
  auto triggered_at = base::TimeTicks::Now();

  EXPECT_TRUE(ShouldContinueCompaction(triggered_at));

  SelfCompactionManager::MaybeCancelCompaction(
      SelfCompactionManager::CompactCancellationReason::kPageResumed);

  EXPECT_FALSE(ShouldContinueCompaction(triggered_at));
}

TEST_P(PreFreezeSelfCompactionTestWithParam, Cancel) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  ASSERT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);

  std::array<void*, 5> addrs;
  for (size_t i = 1; i < 5; i++) {
    addrs[i] = Map(i * base::GetPageSize());
    ASSERT_NE(addrs[i], MAP_FAILED);
  }

  // We should not record the metric here, because we are not currently
  // running.
  SelfCompactionManager::MaybeCancelCompaction(
      SelfCompactionManager::CompactCancellationReason::kPageResumed);

  // This metric is used for both self compaction and running compaction, with
  // the same prefix for both.
  histograms_.ExpectTotalCount(
      "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason", 0);

  // We want the triggered time to be slightly after the last cancelled time;
  // checks for whether we should cancel depend on this.
  task_environment_.FastForwardBy(base::Seconds(1));

  const auto triggered_at = base::TimeTicks::Now();
  auto state = GetState(triggered_at);
  GetMappedMemoryRegions(&state->regions_);
  ASSERT_EQ(state->regions_.size(), 4u);

  {
    base::AutoLock locker(PreFreezeBackgroundMemoryTrimmer::lock());
    SelfCompactionManager::Instance().compaction_last_triggered_ = triggered_at;
  }
  SelfCompactionManager::Instance().StartCompaction(std::move(state));

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);

  task_environment_.FastForwardBy(
      task_environment_.NextMainThreadPendingTaskDelay());

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);

  SelfCompactionManager::MaybeCancelCompaction(
      SelfCompactionManager::CompactCancellationReason::kPageResumed);

  task_environment_.FastForwardBy(
      task_environment_.NextMainThreadPendingTaskDelay());

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);

  task_environment_.FastForwardBy(base::Seconds(60));

  // We should have exactly one metric recorded, the metric telling us we
  // cancelled self compaction.
  EXPECT_EQ(histograms_.GetTotalCountsForPrefix("Memory.SelfCompact2").size(),
            0);
  EXPECT_EQ(histograms_.GetTotalCountsForPrefix("Memory.RunningCompact").size(),
            0);
  histograms_.ExpectTotalCount(
      "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason", 1);

  // Still only expect it to be recorded once, because we were not running the
  // second time we tried to cancel.
  SelfCompactionManager::MaybeCancelCompaction(
      SelfCompactionManager::CompactCancellationReason::kPageResumed);
  histograms_.ExpectTotalCount(
      "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason", 1);

  for (size_t i = 1; i < 5; i++) {
    Unmap(addrs[i], i * base::GetPageSize());
  }
}

TEST_P(PreFreezeSelfCompactionTestWithParam, TimeoutCancel) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  ASSERT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);

  std::array<void*, 5> addrs;
  for (size_t i = 1; i < 5; i++) {
    addrs[i] = Map(i * base::GetPageSize());
    ASSERT_NE(addrs[i], MAP_FAILED);
  }

  // This metric is used for both self compaction and running compaction, with
  // the same prefix for both.
  histograms_.ExpectTotalCount(
      "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason", 0);

  const auto triggered_at = base::TimeTicks::Now();
  auto state = GetState(triggered_at);
  GetMappedMemoryRegions(&state->regions_);
  ASSERT_EQ(state->regions_.size(), 4u);

  {
    base::AutoLock locker(PreFreezeBackgroundMemoryTrimmer::lock());
    SelfCompactionManager::Instance().compaction_last_triggered_ = triggered_at;
  }
  SelfCompactionManager::Instance().StartCompaction(std::move(state));

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);

  // We should have 4 sections here, based on the sizes mapped above.
  // |StartCompaction| doesn't run right away, but rather schedules a task.
  // Because of the cancellation, we expect only three tasks to run. The first
  // two should compact memory, the last should be cancelled below when we
  // advance the clock.
  for (size_t i = 0; i < 1; i++) {
    EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
    task_environment_.FastForwardBy(
        task_environment_.NextMainThreadPendingTaskDelay());
  }

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);

  // Advance the clock here, to simulate a hang. This will not run any tasks.
  task_environment_.AdvanceClock(base::Seconds(10));

  task_environment_.RunUntilIdle();

  for (size_t i = 1; i < 3; i++) {
    size_t len = i * base::GetPageSize();
    EXPECT_EQ(CountResidentPagesInRange(addrs[i], len), i);
    Unmap(addrs[i], len);
  }

  for (size_t i = 3; i < 5; i++) {
    size_t len = i * base::GetPageSize();
    // Compaction is flakey in tests sometimes, so check LE here.
    EXPECT_LE(CountResidentPagesInRange(addrs[i], len), i);
    Unmap(addrs[i], len);
  }

  histograms_.ExpectTotalCount(
      "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason", 1);

  // Bucket #2 is "Timeout".
  EXPECT_THAT(histograms_.GetAllSamples(
                  "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason"),
              BucketsAre(Bucket(0, 0), Bucket(1, 0), Bucket(2, 1)));
}

TEST_F(PreFreezeSelfCompactionTest, NotCanceled) {
  // MADV_PAGEOUT is only supported starting from Linux 5.4. So, on devices
  // don't support it, we bail out early. This is a known problem on some 32
  // bit devices.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  ASSERT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);

  std::array<void*, 5> addrs;
  for (size_t i = 1; i < 5; i++) {
    size_t len = i * base::GetPageSize();
    addrs[i] = Map(len);
    ASSERT_NE(addrs[i], MAP_FAILED);
  }

  base::HistogramTester histograms;

  const auto triggered_at = base::TimeTicks::Now();
  auto state = SelfCompactionManager::GetSelfCompactionStateForTesting(
      task_environment_.GetMainThreadTaskRunner(), triggered_at);
  GetMappedMemoryRegions(&state->regions_);
  ASSERT_EQ(state->regions_.size(), 4u);

  SelfCompactionManager::Instance().StartCompaction(std::move(state));

  // We should have 4 sections here, based on the sizes mapped above.
  // |StartCompaction| doesn't run right away, but rather schedules a task.
  // So, we expect to have 4 tasks to run here.
  for (size_t i = 0; i < 4; i++) {
    EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
    task_environment_.FastForwardBy(
        task_environment_.NextMainThreadPendingTaskDelay());
  }

  // Fast forward to run the metrics tasks too.
  task_environment_.FastForwardBy(base::Seconds(60));

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);

  // We check here for the names of each metric we expect to be recorded. We
  // can't easily check for the exact values of these metrics unfortunately,
  // since they depend on reading /proc/self/smaps_rollup.
  for (const auto& name : {"Rss", "Pss", "PssAnon", "PssFile", "SwapPss"}) {
    for (const auto& timing :
         {"Before", "After", "After1s", "After10s", "After60s"}) {
      histograms.ExpectTotalCount(StrCat({GetMetricName(name), ".", timing}),
                                  1);
    }
    for (const auto& timing :
         {"BeforeAfter", "After1s", "After10s", "After60s"}) {
      const auto metric = StrCat({GetMetricName(name), ".Diff.", timing});
      base::HistogramTester::CountsMap diff_metrics;
      diff_metrics[StrCat({metric, ".Increase"})] = 1;
      diff_metrics[StrCat({metric, ".Decrease"})] = 1;
      EXPECT_THAT(histograms.GetTotalCountsForPrefix(metric),
                  testing::IsSubsetOf(diff_metrics));
    }
  }

  // We also check that no other histograms (other than the ones expected above)
  // were recorded.
  EXPECT_EQ(histograms.GetTotalCountsForPrefix(GetMetricName("")).size(), 47);

  for (size_t i = 1; i < 5; i++) {
    size_t len = i * base::GetPageSize();
    EXPECT_EQ(CountResidentPagesInRange(addrs[i], len), 0);
    Unmap(addrs[i], len);
  }
}

// Test that we still record metrics even when the feature is disabled.
TEST_P(PreFreezeSelfCompactionTestWithParam, Disabled) {
  // Although we are not actually compacting anything, the self compaction
  // code will exit out before metrics are recorded in the case where compaction
  // is not supported.
  if (!CompactionIsSupported()) {
    GTEST_SKIP() << "No kernel support";
  }

  base::test::ScopedFeatureList feature_list_;
  feature_list_.InitWithFeatures({}, {kShouldFreezeSelf, kUseRunningCompact});

  auto triggered_at = base::TimeTicks::Now();
  auto state = GetState(triggered_at);
  SelfCompactionManager::CompactSelf(std::move(state));

  // Run metrics
  task_environment_.FastForwardBy(base::Seconds(60));

  // We check here for the names of each metric we expect to be recorded. We
  // can't easily check for the exact values of these metrics unfortunately,
  // since they depend on reading /proc/self/smaps_rollup.
  for (const auto& name : {"Rss", "Pss", "PssAnon", "PssFile", "SwapPss"}) {
    for (const auto& timing :
         {"Before", "After", "After1s", "After10s", "After60s"}) {
      histograms_.ExpectTotalCount(StrCat({GetMetricName(name), ".", timing}),
                                   1);
    }
    for (const auto& timing :
         {"BeforeAfter", "After1s", "After10s", "After60s"}) {
      const auto metric = StrCat({GetMetricName(name), ".Diff.", timing});
      base::HistogramTester::CountsMap diff_metrics;
      diff_metrics[StrCat({metric, ".Increase"})] = 1;
      diff_metrics[StrCat({metric, ".Decrease"})] = 1;
      EXPECT_THAT(histograms_.GetTotalCountsForPrefix(metric),
                  testing::IsSubsetOf(diff_metrics));
    }
  }

  // We also check that no other histograms (other than the ones expected above)
  // were recorded.
  EXPECT_EQ(histograms_.GetTotalCountsForPrefix(GetMetricName("")).size(), 48);
}

TEST_F(PreFreezeSelfCompactionTest, OnSelfFreezeCancel) {
  base::test::ScopedFeatureList feature_list_;
  feature_list_.InitAndEnableFeature(kShouldFreezeSelf);

  auto state = SelfCompactionManager::GetSelfCompactionStateForTesting(
      task_environment_.GetMainThreadTaskRunner(), TimeTicks::Now());
  {
    base::AutoLock locker(SelfCompactionManager::lock());
    SelfCompactionManager::Instance().OnTriggerCompact(std::move(state));
  }
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);

  // We advance here because |MaybeCancelCompaction| relies on the current
  // time to determine cancellation, which will not work correctly with mocked
  // time otherwise.
  task_environment_.FastForwardBy(base::Seconds(1));

  SelfCompactionManager::MaybeCancelCompaction(
      SelfCompactionManager::CompactCancellationReason::kPageResumed);
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);

  task_environment_.FastForwardBy(
      task_environment_.NextMainThreadPendingTaskDelay());

  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);
}

}  // namespace base::android
