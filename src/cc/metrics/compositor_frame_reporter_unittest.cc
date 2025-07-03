// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/metrics/compositor_frame_reporter.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "cc/metrics/compositor_frame_reporting_controller.h"
#include "cc/metrics/event_metrics.h"
#include "components/viz/common/frame_timing_details.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/types/scroll_input_type.h"

namespace cc {
namespace {

using ::testing::Each;
using ::testing::IsEmpty;
using ::testing::NotNull;

class CompositorFrameReporterTest : public testing::Test {
 public:
  CompositorFrameReporterTest() : pipeline_reporter_(CreatePipelineReporter()) {
    AdvanceNowByUs(1);
  }

 protected:
  base::TimeTicks AdvanceNowByUs(int advance_ms) {
    test_tick_clock_.Advance(base::Microseconds(advance_ms));
    return test_tick_clock_.NowTicks();
  }

  base::TimeTicks Now() { return test_tick_clock_.NowTicks(); }

  std::unique_ptr<BeginMainFrameMetrics> BuildBlinkBreakdown() {
    auto breakdown = std::make_unique<BeginMainFrameMetrics>();
    breakdown->handle_input_events = base::Microseconds(10);
    breakdown->animate = base::Microseconds(9);
    breakdown->style_update = base::Microseconds(8);
    breakdown->layout_update = base::Microseconds(7);
    breakdown->compositing_inputs = base::Microseconds(6);
    breakdown->prepaint = base::Microseconds(5);
    breakdown->paint = base::Microseconds(3);
    breakdown->composite_commit = base::Microseconds(2);
    breakdown->update_layers = base::Microseconds(1);

    // Advance now by the sum of the breakdowns.
    AdvanceNowByUs(10 + 9 + 8 + 7 + 6 + 5 + 3 + 2 + 1);

    return breakdown;
  }

  viz::FrameTimingDetails BuildVizBreakdown() {
    viz::FrameTimingDetails viz_breakdown;
    viz_breakdown.received_compositor_frame_timestamp = AdvanceNowByUs(1);
    viz_breakdown.draw_start_timestamp = AdvanceNowByUs(2);
    viz_breakdown.swap_timings.swap_start = AdvanceNowByUs(3);
    viz_breakdown.swap_timings.swap_end = AdvanceNowByUs(4);
    viz_breakdown.presentation_feedback.timestamp = AdvanceNowByUs(5);
    return viz_breakdown;
  }

  std::unique_ptr<EventMetrics> SetupEventMetrics(
      std::unique_ptr<EventMetrics> metrics) {
    if (metrics) {
      AdvanceNowByUs(3);
      metrics->SetDispatchStageTimestamp(
          EventMetrics::DispatchStage::kRendererCompositorStarted);
      AdvanceNowByUs(3);
      metrics->SetDispatchStageTimestamp(
          EventMetrics::DispatchStage::kRendererCompositorFinished);
    }
    return metrics;
  }

  // Sets up the dispatch durations of each EventMetrics according to
  // stage_durations. Stages with a duration of -1 will be skipped.
  std::unique_ptr<EventMetrics> SetupEventMetricsWithDispatchTimes(
      std::unique_ptr<EventMetrics> metrics,
      const std::vector<int>& stage_durations) {
    if (metrics) {
      int num_stages = stage_durations.size();
      int max_num_stages =
          static_cast<int>(EventMetrics::DispatchStage::kMaxValue) + 1;
      CHECK(num_stages <= max_num_stages)
          << num_stages << " > " << max_num_stages;
      // Start indexing from 2 because the 0th index held duration from
      // kGenerated to kArrivedInRendererCompositor, which was already used in
      // when the EventMetrics was created.
      for (int i = 2; i < num_stages; i++) {
        if (stage_durations[i] >= 0) {
          AdvanceNowByUs(stage_durations[i]);
          metrics->SetDispatchStageTimestamp(
              EventMetrics::DispatchStage(i + 2));
        }
      }
    }
    return metrics;
  }

  std::unique_ptr<EventMetrics> CreateEventMetrics(ui::EventType type) {
    const base::TimeTicks event_time = AdvanceNowByUs(3);
    const base::TimeTicks arrived_in_browser_main_timestamp = AdvanceNowByUs(2);
    AdvanceNowByUs(3);
    return SetupEventMetrics(EventMetrics::CreateForTesting(
        type, event_time, arrived_in_browser_main_timestamp, &test_tick_clock_,
        std::nullopt));
  }

  // Creates EventMetrics with elements in stage_durations representing each
  // dispatch stage's desired duration respectively, with the 0th index
  // representing the duration from kGenerated to kArrivedInRendererCompositor.
  // stage_durations must have at least 1 element for the first required stage
  // Use -1 for stages that want to be skipped.
  std::unique_ptr<EventMetrics> CreateScrollUpdateEventMetricsWithDispatchTimes(
      bool is_inertial,
      ScrollUpdateEventMetrics::ScrollUpdateType scroll_update_type,
      const std::vector<int>& stage_durations) {
    CHECK_GE(stage_durations.size(), 2u);

    const base::TimeTicks event_time = AdvanceNowByUs(3);

    // kGenerated -> kArrivedInBrowserMain
    int begin_rwh_latency_ms = stage_durations[0];
    const base::TimeTicks arrived_in_browser_main_timestamp =
        AdvanceNowByUs(begin_rwh_latency_ms);

    // kArrivedInBrowserMain -> kArrivedInRendererCompositor
    AdvanceNowByUs(stage_durations[1]);

    // Creates a kGestureScrollUpdate event.
    return SetupEventMetricsWithDispatchTimes(
        ScrollUpdateEventMetrics::CreateForTesting(
            ui::EventType::kGestureScrollUpdate, ui::ScrollInputType::kWheel,
            is_inertial, scroll_update_type, /*delta=*/10.0f, event_time,
            arrived_in_browser_main_timestamp, &test_tick_clock_, std::nullopt),
        stage_durations);
  }

  std::unique_ptr<EventMetrics> CreateScrollBeginMetrics(
      ui::ScrollInputType input_type) {
    const base::TimeTicks event_time = AdvanceNowByUs(3);
    const base::TimeTicks arrived_in_browser_main_timestamp = AdvanceNowByUs(2);
    AdvanceNowByUs(3);
    return SetupEventMetrics(ScrollEventMetrics::CreateForTesting(
        ui::EventType::kGestureScrollBegin, input_type,
        /*is_inertial=*/false, event_time, arrived_in_browser_main_timestamp,
        &test_tick_clock_));
  }

  std::unique_ptr<EventMetrics> CreateScrollUpdateEventMetrics(
      ui::ScrollInputType input_type,
      bool is_inertial,
      ScrollUpdateEventMetrics::ScrollUpdateType scroll_update_type) {
    const base::TimeTicks event_time = AdvanceNowByUs(3);
    const base::TimeTicks arrived_in_browser_main_timestamp = AdvanceNowByUs(2);
    AdvanceNowByUs(3);
    return SetupEventMetrics(ScrollUpdateEventMetrics::CreateForTesting(
        ui::EventType::kGestureScrollUpdate, input_type, is_inertial,
        scroll_update_type, /*delta=*/10.0f, event_time,
        arrived_in_browser_main_timestamp, &test_tick_clock_, std::nullopt));
  }

  std::unique_ptr<EventMetrics> CreatePinchEventMetrics(
      ui::EventType type,
      ui::ScrollInputType input_type) {
    const base::TimeTicks event_time = AdvanceNowByUs(3);
    AdvanceNowByUs(3);
    return SetupEventMetrics(PinchEventMetrics::CreateForTesting(
        type, input_type, event_time, &test_tick_clock_));
  }

  std::vector<base::TimeTicks> GetEventTimestamps(
      const EventMetrics::List& events_metrics) {
    std::vector<base::TimeTicks> event_times;
    event_times.reserve(events_metrics.size());
    std::ranges::transform(events_metrics, std::back_inserter(event_times),
                           [](const auto& event_metrics) {
                             return event_metrics->GetDispatchStageTimestamp(
                                 EventMetrics::DispatchStage::kGenerated);
                           });
    return event_times;
  }

  std::unique_ptr<CompositorFrameReporter> CreatePipelineReporter() {
    GlobalMetricsTrackers trackers{nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   &frame_sorter_};
    auto reporter = std::make_unique<CompositorFrameReporter>(
        ActiveTrackers(), viz::BeginFrameArgs(),
        /*should_report_metrics=*/true,
        CompositorFrameReporter::SmoothThread::kSmoothBoth,
        FrameInfo::SmoothEffectDrivingThread::kUnknown,
        /*layer_tree_host_id=*/1, trackers);
    reporter->set_tick_clock(&test_tick_clock_);
    return reporter;
  }

  void IntToTimeDeltaVector(std::vector<base::TimeDelta>& timedelta_vector,
                            std::vector<int> int_vector) {
    size_t vector_size = int_vector.size();
    for (size_t i = 0; i < vector_size; i++) {
      timedelta_vector[i] = base::Microseconds(int_vector[i]);
    }
  }

  // Disable sub-sampling to deterministically record histograms under test.
  base::MetricsSubSampler::ScopedAlwaysSampleForTesting no_subsampling_;

  // This should be defined before |pipeline_reporter_| so it is created before
  // and destroyed after that.
  base::SimpleTestTickClock test_tick_clock_;

  FrameSorter frame_sorter_;
  std::unique_ptr<CompositorFrameReporter> pipeline_reporter_;

  // Number of breakdown stages of the current PipelineReporter
  const int kNumOfCompositorStages =
      static_cast<int>(CompositorFrameReporter::StageType::kStageTypeCount) - 1;
  const int kNumDispatchStages =
      static_cast<int>(EventMetrics::DispatchStage::kMaxValue);
  const base::TimeDelta kLatencyPredictionDeviationThreshold =
      base::Milliseconds(8.33);
};

TEST_F(CompositorFrameReporterTest, MainFrameAbortedReportingTest) {
  base::HistogramTester histogram_tester;

  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kBeginImplFrameToSendBeginMainFrame,
      Now());
  EXPECT_EQ(0u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kSendBeginMainFrameToCommit, Now());
  EXPECT_EQ(1u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndActivateToSubmitCompositorFrame,
      Now());
  EXPECT_EQ(2u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::
          kSubmitCompositorFrameToPresentationCompositorFrame,
      Now());
  EXPECT_EQ(3u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(3);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kPresentedFrame, Now());
  EXPECT_EQ(4u, pipeline_reporter_->stage_history_size_for_testing());

  pipeline_reporter_ = nullptr;
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.BeginImplFrameToSendBeginMainFrame", 1);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.SendBeginMainFrameToCommit", 1);
  histogram_tester.ExpectTotalCount("CompositorLatency2.Commit", 0);
  histogram_tester.ExpectTotalCount("CompositorLatency2.EndCommitToActivation",
                                    0);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.EndActivateToSubmitCompositorFrame", 1);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.SubmitCompositorFrameToPresentationCompositorFrame",
      1);
}

TEST_F(CompositorFrameReporterTest, ReplacedByNewReporterReportingTest) {
  base::HistogramTester histogram_tester;

  pipeline_reporter_->StartStage(CompositorFrameReporter::StageType::kCommit,
                                 Now());
  EXPECT_EQ(0u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndCommitToActivation, Now());
  EXPECT_EQ(1u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(2);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kReplacedByNewReporter,
      Now());
  EXPECT_EQ(2u, pipeline_reporter_->stage_history_size_for_testing());

  pipeline_reporter_ = nullptr;
  histogram_tester.ExpectTotalCount("CompositorLatency2.Commit", 0);
  histogram_tester.ExpectTotalCount("CompositorLatency2.EndCommitToActivation",
                                    0);
}

TEST_F(CompositorFrameReporterTest, SubmittedFrameReportingTest) {
  base::HistogramTester histogram_tester;

  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kActivation, Now());
  EXPECT_EQ(0u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndActivateToSubmitCompositorFrame,
      Now());
  EXPECT_EQ(1u, pipeline_reporter_->stage_history_size_for_testing());

  AdvanceNowByUs(2);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kPresentedFrame, Now());
  EXPECT_EQ(2u, pipeline_reporter_->stage_history_size_for_testing());

  pipeline_reporter_ = nullptr;
  histogram_tester.ExpectTotalCount("CompositorLatency2.Activation", 1);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.EndActivateToSubmitCompositorFrame", 1);
  histogram_tester.ExpectTotalCount("CompositorLatency2.TotalLatency", 1);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.DroppedFrame.Activation", 0);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.DroppedFrame.EndActivateToSubmitCompositorFrame", 0);
  histogram_tester.ExpectTotalCount(
      "CompositorLatency2.DroppedFrame.TotalLatency", 0);

  histogram_tester.ExpectBucketCount("CompositorLatency2.Activation", 3, 1);
  histogram_tester.ExpectBucketCount(
      "CompositorLatency2.EndActivateToSubmitCompositorFrame", 2, 1);
  histogram_tester.ExpectBucketCount("CompositorLatency2.TotalLatency", 5, 1);
}

// Tests that when a frame is presented to the user, total event latency metrics
// are reported properly.
TEST_F(CompositorFrameReporterTest,
       EventLatencyTotalForPresentedFrameReported) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<EventMetrics> event_metrics_ptrs[] = {
      CreateEventMetrics(ui::EventType::kTouchPressed),
      CreateEventMetrics(ui::EventType::kTouchMoved),
      CreateEventMetrics(ui::EventType::kTouchMoved),
  };
  EXPECT_THAT(event_metrics_ptrs, Each(NotNull()));
  EventMetrics::List events_metrics(
      std::make_move_iterator(std::begin(event_metrics_ptrs)),
      std::make_move_iterator(std::end(event_metrics_ptrs)));
  std::vector<base::TimeTicks> event_times = GetEventTimestamps(events_metrics);

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kBeginImplFrameToSendBeginMainFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndActivateToSubmitCompositorFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::
          kSubmitCompositorFrameToPresentationCompositorFrame,
      Now());
  pipeline_reporter_->AddEventsMetrics(std::move(events_metrics));

  const base::TimeTicks presentation_time = AdvanceNowByUs(3);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kPresentedFrame,
      presentation_time);

  pipeline_reporter_ = nullptr;

  struct {
    const char* name;
    const base::HistogramBase::Count32 count;
  } expected_counts[] = {
      {"EventLatency.TouchPressed.TotalLatency", 1},
      {"EventLatency.TouchMoved.TotalLatency", 2},
      {"EventLatency.TotalLatency", 3},
  };
  for (const auto& expected_count : expected_counts) {
    histogram_tester.ExpectTotalCount(expected_count.name,
                                      expected_count.count);
  }

  struct {
    const char* name;
    const base::HistogramBase::Sample32 latency_ms;
  } expected_latencies[] = {
      {"EventLatency.TouchPressed.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[0]).InMicroseconds())},
      {"EventLatency.TouchMoved.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[1]).InMicroseconds())},
      {"EventLatency.TouchMoved.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[2]).InMicroseconds())},
      {"EventLatency.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[0]).InMicroseconds())},
      {"EventLatency.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[1]).InMicroseconds())},
      {"EventLatency.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[2]).InMicroseconds())},
  };
  for (const auto& expected_latency : expected_latencies) {
    histogram_tester.ExpectBucketCount(expected_latency.name,
                                       expected_latency.latency_ms, 1);
  }
}

// Tests that when a frame is presented to the user, total scroll event latency
// metrics are reported properly.
TEST_F(CompositorFrameReporterTest,
       EventLatencyScrollTotalForPresentedFrameReported) {
  base::HistogramTester histogram_tester;

  const bool kScrollIsInertial = true;
  const bool kScrollIsNotInertial = false;
  std::unique_ptr<EventMetrics> event_metrics_ptrs[] = {
      CreateScrollBeginMetrics(ui::ScrollInputType::kWheel),
      CreateScrollUpdateEventMetrics(
          ui::ScrollInputType::kWheel, kScrollIsNotInertial,
          ScrollUpdateEventMetrics::ScrollUpdateType::kStarted),
      CreateScrollUpdateEventMetrics(
          ui::ScrollInputType::kWheel, kScrollIsNotInertial,
          ScrollUpdateEventMetrics::ScrollUpdateType::kContinued),
      CreateScrollUpdateEventMetrics(
          ui::ScrollInputType::kWheel, kScrollIsInertial,
          ScrollUpdateEventMetrics::ScrollUpdateType::kContinued),
      CreateScrollBeginMetrics(ui::ScrollInputType::kTouchscreen),
      CreateScrollUpdateEventMetrics(
          ui::ScrollInputType::kTouchscreen, kScrollIsNotInertial,
          ScrollUpdateEventMetrics::ScrollUpdateType::kStarted),
      CreateScrollUpdateEventMetrics(
          ui::ScrollInputType::kTouchscreen, kScrollIsNotInertial,
          ScrollUpdateEventMetrics::ScrollUpdateType::kContinued),
      CreateScrollUpdateEventMetrics(
          ui::ScrollInputType::kTouchscreen, kScrollIsInertial,
          ScrollUpdateEventMetrics::ScrollUpdateType::kContinued),
  };
  EXPECT_THAT(event_metrics_ptrs, Each(NotNull()));
  EventMetrics::List events_metrics(
      std::make_move_iterator(std::begin(event_metrics_ptrs)),
      std::make_move_iterator(std::end(event_metrics_ptrs)));
  std::vector<base::TimeTicks> event_times = GetEventTimestamps(events_metrics);

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kBeginImplFrameToSendBeginMainFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndActivateToSubmitCompositorFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::
          kSubmitCompositorFrameToPresentationCompositorFrame,
      Now());
  pipeline_reporter_->AddEventsMetrics(std::move(events_metrics));

  AdvanceNowByUs(3);
  viz::FrameTimingDetails viz_breakdown = BuildVizBreakdown();
  pipeline_reporter_->SetVizBreakdown(viz_breakdown);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kPresentedFrame,
      viz_breakdown.presentation_feedback.timestamp);

  pipeline_reporter_ = nullptr;

  struct {
    const char* name;
    const base::HistogramBase::Count32 count;
  } expected_counts[] = {
      {"EventLatency.GestureScrollBegin.Wheel.TotalLatency2", 1},
      {"EventLatency.FirstGestureScrollUpdate.Wheel.TotalLatency2", 1},
      {"EventLatency.GestureScrollUpdate.Wheel.TotalLatency2", 1},
      {"EventLatency.InertialGestureScrollUpdate.Wheel.TotalLatency2", 1},
      {"EventLatency.GestureScrollBegin.Touchscreen.TotalLatency2", 1},
      {"EventLatency.FirstGestureScrollUpdate.Touchscreen.TotalLatency2", 1},
      {"EventLatency.GestureScrollUpdate.Touchscreen.TotalLatency", 1},
      {"EventLatency.GestureScrollUpdate.Touchscreen.TotalLatency2", 1},
      {"EventLatency.InertialGestureScrollUpdate.Touchscreen.TotalLatency2", 1},
      {"EventLatency.GestureScrollBegin.TotalLatency2", 2},
      {"EventLatency.GestureScrollBegin.GenerationToBrowserMain", 2},
      {"EventLatency.FirstGestureScrollUpdate.TotalLatency2", 2},
      {"EventLatency.FirstGestureScrollUpdate.GenerationToBrowserMain", 2},
      {"EventLatency.GestureScrollUpdate.TotalLatency2", 2},
      {"EventLatency.GestureScrollUpdate.GenerationToBrowserMain", 2},
      {"EventLatency.InertialGestureScrollUpdate.TotalLatency2", 2},
      {"EventLatency.InertialGestureScrollUpdate.GenerationToBrowserMain", 2},
      {"EventLatency.TotalLatency", 8},
  };
  for (const auto& expected_count : expected_counts) {
    histogram_tester.ExpectTotalCount(expected_count.name,
                                      expected_count.count);
  }

  const base::TimeTicks presentation_time =
      viz_breakdown.presentation_feedback.timestamp;
  struct {
    const char* name;
    const base::HistogramBase::Sample32 latency_ms;
  } expected_latencies[] = {
      {"EventLatency.GestureScrollBegin.Wheel.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[0]).InMicroseconds())},
      {"EventLatency.FirstGestureScrollUpdate.Wheel.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[1]).InMicroseconds())},
      {"EventLatency.GestureScrollUpdate.Wheel.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[2]).InMicroseconds())},
      {"EventLatency.InertialGestureScrollUpdate.Wheel.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[3]).InMicroseconds())},
      {"EventLatency.GestureScrollBegin.Touchscreen.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[4]).InMicroseconds())},
      {"EventLatency.FirstGestureScrollUpdate.Touchscreen.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[5]).InMicroseconds())},
      {"EventLatency.GestureScrollUpdate.Touchscreen.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[6]).InMicroseconds())},
      {"EventLatency.GestureScrollUpdate.Touchscreen.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[6]).InMicroseconds())},
      {"EventLatency.InertialGestureScrollUpdate.Touchscreen.TotalLatency2",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[7]).InMicroseconds())},
  };
  for (const auto& expected_latency : expected_latencies) {
    histogram_tester.ExpectBucketCount(expected_latency.name,
                                       expected_latency.latency_ms, 1);
  }
}

// Tests that when a frame is presented to the user, total pinch event latency
// metrics are reported properly.
TEST_F(CompositorFrameReporterTest,
       EventLatencyPinchTotalForPresentedFrameReported) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<EventMetrics> event_metrics_ptrs[] = {
      CreatePinchEventMetrics(ui::EventType::kGesturePinchBegin,
                              ui::ScrollInputType::kWheel),
      CreatePinchEventMetrics(ui::EventType::kGesturePinchUpdate,
                              ui::ScrollInputType::kWheel),
      CreatePinchEventMetrics(ui::EventType::kGesturePinchBegin,
                              ui::ScrollInputType::kTouchscreen),
      CreatePinchEventMetrics(ui::EventType::kGesturePinchUpdate,
                              ui::ScrollInputType::kTouchscreen),
  };
  EXPECT_THAT(event_metrics_ptrs, Each(NotNull()));
  EventMetrics::List events_metrics(
      std::make_move_iterator(std::begin(event_metrics_ptrs)),
      std::make_move_iterator(std::end(event_metrics_ptrs)));
  std::vector<base::TimeTicks> event_times = GetEventTimestamps(events_metrics);

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kBeginImplFrameToSendBeginMainFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndActivateToSubmitCompositorFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::
          kSubmitCompositorFrameToPresentationCompositorFrame,
      Now());
  pipeline_reporter_->AddEventsMetrics(std::move(events_metrics));

  AdvanceNowByUs(3);
  viz::FrameTimingDetails viz_breakdown = BuildVizBreakdown();
  pipeline_reporter_->SetVizBreakdown(viz_breakdown);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kPresentedFrame,
      viz_breakdown.presentation_feedback.timestamp);

  pipeline_reporter_ = nullptr;

  struct {
    const char* name;
    const base::HistogramBase::Count32 count;
  } expected_counts[] = {
      {"EventLatency.GesturePinchBegin.Touchscreen.TotalLatency", 1},
      {"EventLatency.GesturePinchUpdate.Touchscreen.TotalLatency", 1},
      {"EventLatency.GesturePinchBegin.Touchpad.TotalLatency", 1},
      {"EventLatency.GesturePinchUpdate.Touchpad.TotalLatency", 1},
      {"EventLatency.TotalLatency", 4},
  };
  for (const auto& expected_count : expected_counts) {
    histogram_tester.ExpectTotalCount(expected_count.name,
                                      expected_count.count);
  }

  const base::TimeTicks presentation_time =
      viz_breakdown.presentation_feedback.timestamp;
  struct {
    const char* name;
    const base::HistogramBase::Sample32 latency_ms;
  } expected_latencies[] = {
      {"EventLatency.GesturePinchBegin.Touchpad.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[0]).InMicroseconds())},
      {"EventLatency.GesturePinchUpdate.Touchpad.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[1]).InMicroseconds())},
      {"EventLatency.GesturePinchBegin.Touchscreen.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[2]).InMicroseconds())},
      {"EventLatency.GesturePinchUpdate.Touchscreen.TotalLatency",
       static_cast<base::HistogramBase::Sample32>(
           (presentation_time - event_times[3]).InMicroseconds())},
  };
  for (const auto& expected_latency : expected_latencies) {
    histogram_tester.ExpectBucketCount(expected_latency.name,
                                       expected_latency.latency_ms, 1);
  }
}

// Tests that when the frame is not presented to the user, event latency metrics
// are not reported.
TEST_F(CompositorFrameReporterTest,
       EventLatencyForDidNotPresentFrameNotReported) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<EventMetrics> event_metrics_ptrs[] = {
      CreateEventMetrics(ui::EventType::kTouchPressed),
      CreateEventMetrics(ui::EventType::kTouchMoved),
      CreateEventMetrics(ui::EventType::kTouchMoved),
  };
  EXPECT_THAT(event_metrics_ptrs, Each(NotNull()));
  EventMetrics::List events_metrics(
      std::make_move_iterator(std::begin(event_metrics_ptrs)),
      std::make_move_iterator(std::end(event_metrics_ptrs)));

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kBeginImplFrameToSendBeginMainFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::kEndActivateToSubmitCompositorFrame,
      Now());

  AdvanceNowByUs(3);
  pipeline_reporter_->StartStage(
      CompositorFrameReporter::StageType::
          kSubmitCompositorFrameToPresentationCompositorFrame,
      Now());
  pipeline_reporter_->AddEventsMetrics(std::move(events_metrics));

  AdvanceNowByUs(3);
  pipeline_reporter_->TerminateFrame(
      CompositorFrameReporter::FrameTerminationStatus::kDidNotPresentFrame,
      Now());

  pipeline_reporter_ = nullptr;

  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix("EventLaterncy."),
              IsEmpty());
}

// Verifies that partial update dependent queues are working as expected when
// they reach their maximum capacity.
TEST_F(CompositorFrameReporterTest, PartialUpdateDependentQueues) {
  // This constant should match the constant with the same name in
  // compositor_frame_reporter.cc.
  const size_t kMaxOwnedPartialUpdateDependents = 300u;

  // The first three dependent reporters for the front of the queue.
  auto deps = std::to_array<std::unique_ptr<CompositorFrameReporter>>({
      CreatePipelineReporter(),
      CreatePipelineReporter(),
      CreatePipelineReporter(),
  });

  // Set `deps[0]` as a dependent of the main reporter and adopt it at the same
  // time. This should enqueue it in both non-owned and owned dependents queues.
  deps[0]->SetPartialUpdateDecider(pipeline_reporter_.get());
  pipeline_reporter_->AdoptReporter(std::move(deps[0]));
  DCHECK_EQ(1u,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      1u,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Set `deps[1]` as a dependent of the main reporter, but don't adopt it yet.
  // This should enqueue it in non-owned dependents queue only.
  deps[1]->SetPartialUpdateDecider(pipeline_reporter_.get());
  DCHECK_EQ(2u,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      1u,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Set `deps[2]` as a dependent of the main reporter and adopt it at the same
  // time. This should enqueue it in both non-owned and owned dependents queues.
  deps[2]->SetPartialUpdateDecider(pipeline_reporter_.get());
  pipeline_reporter_->AdoptReporter(std::move(deps[2]));
  DCHECK_EQ(3u,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      2u,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Now adopt `deps[1]` to enqueue it in the owned dependents queue.
  pipeline_reporter_->AdoptReporter(std::move(deps[1]));
  DCHECK_EQ(3u,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      3u,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Fill the queues with more dependent reporters until the capacity is
  // reached. After this, the queues should look like this (assuming n equals
  // `kMaxOwnedPartialUpdateDependents`):
  //   Partial Update Dependents:       [0, 1, 2, 3, 4, ..., n-1]
  //   Owned Partial Update Dependents: [0, 2, 1, 3, 4, ..., n-1]
  while (
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing() <
      kMaxOwnedPartialUpdateDependents) {
    std::unique_ptr<CompositorFrameReporter> dependent =
        CreatePipelineReporter();
    dependent->SetPartialUpdateDecider(pipeline_reporter_.get());
    pipeline_reporter_->AdoptReporter(std::move(dependent));
  }
  DCHECK_EQ(kMaxOwnedPartialUpdateDependents,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      kMaxOwnedPartialUpdateDependents,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Enqueue a new dependent reporter. This should pop `deps[0]` from the front
  // of the owned dependents queue and destroy it. Since the same one is in
  // front of the non-owned dependents queue, it will be popped out of that
  // queue, too. The queues will look like this:
  //   Partial Update Dependents:       [1, 2, 3, 4, ..., n]
  //   Owned Partial Update Dependents: [2, 1, 3, 4, ..., n]
  auto new_dep = CreatePipelineReporter();
  new_dep->SetPartialUpdateDecider(pipeline_reporter_.get());
  pipeline_reporter_->AdoptReporter(std::move(new_dep));
  DCHECK_EQ(kMaxOwnedPartialUpdateDependents,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      kMaxOwnedPartialUpdateDependents,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Enqueue another new dependent reporter. This should pop `deps[2]` from the
  // front of the owned dependents queue and destroy it. It should be removed
  // from the non-owned dependents queue as well.
  //   Partial Update Dependents:       [2, 3, 4, ..., n+1]
  //   Owned Partial Update Dependents: [2, 3, 4, ..., n+1]
  new_dep = CreatePipelineReporter();
  new_dep->SetPartialUpdateDecider(pipeline_reporter_.get());
  pipeline_reporter_->AdoptReporter(std::move(new_dep));
  DCHECK_EQ(kMaxOwnedPartialUpdateDependents,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      kMaxOwnedPartialUpdateDependents,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());

  // Enqueue yet another new dependent reporter. This should pop `deps[1]` from
  // the front of the owned dependents queue and destroy it. Since the same one
  // is in front of the non-owned dependents queue followed by `deps[2]` which
  // was destroyed in the previous step, they will be popped out of that queue,
  // too. The queues will look like this:
  //   Partial Update Dependents:       [3, 4, ..., n+2]
  //   Owned Partial Update Dependents: [3, 4, ..., n+2]
  new_dep = CreatePipelineReporter();
  new_dep->SetPartialUpdateDecider(pipeline_reporter_.get());
  pipeline_reporter_->AdoptReporter(std::move(new_dep));
  DCHECK_EQ(kMaxOwnedPartialUpdateDependents,
            pipeline_reporter_->partial_update_dependents_size_for_testing());
  DCHECK_EQ(
      kMaxOwnedPartialUpdateDependents,
      pipeline_reporter_->owned_partial_update_dependents_size_for_testing());
}

}  // namespace
}  // namespace cc
