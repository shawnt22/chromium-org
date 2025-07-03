// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web_transport_throttle_context.h"

#include "base/check_op.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "net/base/features.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

using ThrottleResult = WebTransportThrottleContext::ThrottleResult;
using Tracker = WebTransportThrottleContext::Tracker;

class WebTransportThrottleContextTest : public testing::Test {
 public:
  WebTransportThrottleContextTest() {
    feature_list_.InitWithFeatures(
        /*enabled_features=*/{net::features::
                                  kWebTransportFineGrainedThrottling},
        /*disabled_features=*/{});
  }

  WebTransportThrottleContext& context() { return context_; }

  void FastForwardBy(base::TimeDelta delta) {
    task_environment_.FastForwardBy(delta);
  }

  void CreatePending(int count) {
    for (int i = 0; i < count; ++i) {
      base::RunLoop run_loop;
      auto result = context().PerformThrottle(base::BindLambdaForTesting(
          [&run_loop, this](std::unique_ptr<Tracker> tracker) {
            std::optional<net::IPAddress> address =
                net::IPAddress::FromIPLiteral("192.168.1.18");
            net::IPEndPoint end_point(*address, 80);
            tracker->OnBeforeConnect(end_point);
            trackers_.push(std::move(tracker));
            run_loop.Quit();
          }));
      EXPECT_EQ(ThrottleResult::kOk, result);
      run_loop.Run();
    }
  }

  void CreatePendingWithoutConnect(int count) {
    for (int i = 0; i < count; ++i) {
      base::RunLoop run_loop;
      auto result = context().PerformThrottle(base::BindLambdaForTesting(
          [&run_loop, this](std::unique_ptr<Tracker> tracker) {
            trackers_.push(std::move(tracker));
            run_loop.Quit();
          }));
      EXPECT_EQ(ThrottleResult::kOk, result);
      run_loop.Run();
    }
  }

  void CreatePendingSameSubnet(int count) {
    for (int i = 0; i < count; ++i) {
      base::RunLoop run_loop;
      auto result = context().PerformThrottle(base::BindLambdaForTesting(
          [&run_loop, &i, this](std::unique_ptr<Tracker> tracker) {
            std::optional<net::IPAddress> address =
                net::IPAddress::FromIPLiteral("192.168.1." +
                                              base::NumberToString(i + 1));
            net::IPEndPoint end_point(*address, 80);
            tracker->OnBeforeConnect(end_point);
            trackers_.push(std::move(tracker));
            run_loop.Quit();
          }));
      EXPECT_EQ(ThrottleResult::kOk, result);
      run_loop.Run();
    }
  }

  void CreatePendingInvalidEndPoint(int count) {
    for (int i = 0; i < count; ++i) {
      base::RunLoop run_loop;
      auto result = context().PerformThrottle(base::BindLambdaForTesting(
          [&run_loop, this](std::unique_ptr<Tracker> tracker) {
            net::IPEndPoint end_point;
            tracker->OnBeforeConnect(end_point);
            trackers_.push(std::move(tracker));
            run_loop.Quit();
          }));
      EXPECT_EQ(ThrottleResult::kOk, result);
      run_loop.Run();
    }
  }

  // Causes the first `count` pending handshakes to be signalled established.
  void EstablishPending(int count) {
    DCHECK_LE(count, static_cast<int>(trackers_.size()));
    for (int i = 0; i < count; ++i) {
      trackers_.front()->OnHandshakeEstablished();
      trackers_.pop();
    }
  }

  // Causes the first `count` pending handshakes to be signalled failed.
  void FailPending(int count) {
    DCHECK_LE(count, static_cast<int>(trackers_.size()));
    for (int i = 0; i < count; ++i) {
      trackers_.front()->OnHandshakeFailed();
      trackers_.pop();
    }
  }

  void CloseAbruptly(int count) {
    DCHECK_LE(count, static_cast<int>(trackers_.size()));
    for (int i = 0; i < count; ++i) {
      trackers_.pop();
    }
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  WebTransportThrottleContext context_;

  base::queue<std::unique_ptr<Tracker>> trackers_;
};

class CallTrackingCallback {
 public:
  WebTransportThrottleContext::ThrottleDoneCallback Callback() {
    // This use of base::Unretained is safe because the
    // WebTransportThrottleContext is always destroyed at the end of the test
    // before it gets a change to call any callbacks.
    return base::BindOnce(&CallTrackingCallback::OnCall,
                          base::Unretained(this));
  }

  bool called() const { return called_; }

 private:
  void OnCall(std::unique_ptr<Tracker> tracker) { called_ = true; }

  bool called_ = false;
};

TEST_F(WebTransportThrottleContextTest, PerformThrottleSyncWithNonePending) {
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, PerformThrottleNotSyncWithOnePending) {
  CreatePending(1);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, Max64Connections) {
  CreatePending(64);

  CallTrackingCallback callback;
  EXPECT_EQ(ThrottleResult::kTooManyPendingSessions,
            context().PerformThrottle(callback.Callback()));

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, DelayWithOnePending) {
  CreatePending(1);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);

  // The delay should be at least 5ms.
  FastForwardBy(base::Milliseconds(4));
  EXPECT_FALSE(callback.called());

  // The delay should be less than 16ms.
  FastForwardBy(base::Milliseconds(12));
  EXPECT_TRUE(callback.called());
}

// The reason for testing with 3 pending connections is that the possible range
// of delays doesn't overlap with 1 pending connection.
TEST_F(WebTransportThrottleContextTest, DelayWithThreePending) {
  CreatePending(3);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);

  // The delay should be at least 20ms.
  FastForwardBy(base::Milliseconds(19));
  EXPECT_FALSE(callback.called());

  // The delay should be less than 61ms.
  FastForwardBy(base::Milliseconds(42));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, DelayIsTruncated) {
  CreatePending(63);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);

  // The delay should be no less than 30s.
  FastForwardBy(base::Seconds(29));
  EXPECT_FALSE(callback.called());

  // The delay should be less than 91s.
  FastForwardBy(base::Seconds(62));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, EstablishedRemainsPendingFor10ms) {
  CreatePending(1);

  EstablishPending(1);

  // The delay should be more than 9ms.
  FastForwardBy(base::Milliseconds(9));

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 11ms.
  FastForwardBy(base::Milliseconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, CancelledOnceRemainsPendingFor50ms) {
  CreatePendingWithoutConnect(1);

  CloseAbruptly(1);

  // The delay should be more than 99ms.
  FastForwardBy(base::Milliseconds(49));
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 51 milliseconds.
  FastForwardBy(base::Milliseconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, CancelledRemainsPendingFor5m) {
  CreatePendingWithoutConnect(2);

  CloseAbruptly(2);

  // The delay should be more than 299 seconds.
  FastForwardBy(base::Seconds(299));
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 301 seconds.
  FastForwardBy(base::Seconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, FailedRemainsPendingFor100ms) {
  CreatePending(1);

  FailPending(1);

  // The delay should be more than 99ms.
  FastForwardBy(base::Milliseconds(99));
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 101 milliseconds.
  FastForwardBy(base::Milliseconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, FailedSameHostRemainsPendingFor5m) {
  CreatePending(2);

  FailPending(2);

  // The delay should be more than 299 seconds.
  FastForwardBy(base::Seconds(299));
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 301 seconds.
  FastForwardBy(base::Seconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, RemovedObsoleteAfter15m) {
  CreatePending(2);

  FailPending(2);

  // Ensure the throttling is done after 300 seconds.
  CallTrackingCallback callback1;
  const auto result1 = context().PerformThrottle(callback1.Callback());
  FastForwardBy(base::Seconds(301));
  EXPECT_EQ(result1, ThrottleResult::kOk);
  EXPECT_TRUE(callback1.called());

  // One new failure before the previous ones become obsolete.
  FastForwardBy(base::Seconds(598));
  CreatePending(1);
  FailPending(1);

  // Previous failures still hold, hence a single new failure requires 300
  // seconds.
  FastForwardBy(base::Seconds(299));
  CallTrackingCallback callback2;
  const auto result2 = context().PerformThrottle(callback2.Callback());
  EXPECT_EQ(result2, ThrottleResult::kOk);
  EXPECT_FALSE(callback2.called());

  // One New failure after the previous ones become obsolete
  FastForwardBy(base::Seconds(601));
  CreatePending(1);
  FailPending(1);

  // The delay should be less than 100ms.
  FastForwardBy(base::Milliseconds(101));
  CallTrackingCallback callback3;
  const auto result3 = context().PerformThrottle(callback3.Callback());
  EXPECT_EQ(result3, ThrottleResult::kOk);
  EXPECT_TRUE(callback3.called());
}

TEST_F(WebTransportThrottleContextTest,
       FailedInvalidEndPointRemainsPendingFor5m) {
  CreatePendingInvalidEndPoint(2);

  FailPending(2);

  // The delay should be more than 299 seconds.
  FastForwardBy(base::Seconds(299));
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 301 seconds.
  FastForwardBy(base::Seconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, FailedSameSubnetRemainsPendingFor2m) {
  CreatePendingSameSubnet(2);

  FailPending(2);

  // The delay should be more than 119 seconds.
  FastForwardBy(base::Seconds(119));
  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);
  EXPECT_FALSE(callback.called());

  // The delay should be less than 121 seconds.
  FastForwardBy(base::Seconds(2));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, HandshakeCompletionTriggersPending) {
  CreatePending(3);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);

  EstablishPending(3);

  // After 10ms the handshakes should no longer be pending and the pending
  // throttle should fire.
  FastForwardBy(base::Milliseconds(10));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest, HandshakeCompletionResetsTimer) {
  CreatePending(5);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);

  EstablishPending(2);

  // After 10ms the handshakes should no longer be pending and the timer for the
  // pending throttle should be reset.
  FastForwardBy(base::Milliseconds(10));

  // The 10ms should be counted towards the throttling time.
  // There should be more than 9ms remaining.
  FastForwardBy(base::Milliseconds(9));
  EXPECT_FALSE(callback.called());

  // There should be less than 51 ms remaining.
  FastForwardBy(base::Milliseconds(42));
  EXPECT_TRUE(callback.called());
}

TEST_F(WebTransportThrottleContextTest,
       ManyHandshakeCompletionsTriggerPending) {
  CreatePending(63);

  CallTrackingCallback callback;
  const auto result = context().PerformThrottle(callback.Callback());
  EXPECT_EQ(result, ThrottleResult::kOk);

  // Move time forward so that the maximum delay for a handshake with one
  // pending has passed.
  FastForwardBy(base::Milliseconds(15));

  // Leave one pending or the pending handshake will be triggered without
  // recalculating the delay.
  EstablishPending(62);

  // After 10ms the handshakes should no longer be pending and the pending
  // connection throttle timer should have fired.
  FastForwardBy(base::Milliseconds(10));
  EXPECT_TRUE(callback.called());
}

}  // namespace

}  // namespace content
