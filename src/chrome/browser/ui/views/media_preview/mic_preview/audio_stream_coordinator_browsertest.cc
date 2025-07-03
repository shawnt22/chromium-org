// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/media_preview/mic_preview/audio_stream_coordinator.h"

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/task/current_thread.h"
#include "base/test/bind.h"
#include "base/test/mock_callback.h"
#include "chrome/browser/ui/views/media_preview/media_preview_metrics.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "media/base/audio_glitch_info.h"
#include "media/base/audio_parameters.h"
#include "media/mojo/mojom/audio_data_pipe.mojom.h"
#include "services/audio/public/cpp/fake_stream_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view.h"

class MockStreamFactory : public audio::FakeStreamFactory {
 public:
  void CreateInputStream(
      mojo::PendingReceiver<::media::mojom::AudioInputStream> stream_receiver,
      mojo::PendingRemote<media::mojom::AudioInputStreamClient> client,
      mojo::PendingRemote<::media::mojom::AudioInputStreamObserver> observer,
      mojo::PendingRemote<::media::mojom::AudioLog> log,
      const std::string& device_id,
      const media::AudioParameters& params,
      uint32_t shared_memory_count,
      bool enable_agc,
      media::mojom::AudioProcessingConfigPtr processing_config,
      CreateInputStreamCallback callback) override {
    std::move(callback).Run(media::mojom::ReadWriteAudioDataPipePtr(), false,
                            std::nullopt);
    run_loop.Quit();
  }

  void WaitToCreateInputStream() { run_loop.Run(); }

 private:
  base::RunLoop run_loop;
};

class AudioStreamCoordinatorTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    parent_view_ = std::make_unique<views::View>();
    coordinator_ = std::make_unique<AudioStreamCoordinator>(
        *parent_view_, media_preview_metrics::Context(
                           media_preview_metrics::UiLocation::kPermissionPrompt,
                           media_preview_metrics::PreviewType::kMic,
                           media_preview_metrics::PromptType::kSingle,
                           /*request=*/nullptr));
    fake_stream_factory_ = std::make_unique<MockStreamFactory>();
  }

  void TearDownOnMainThread() override {
    coordinator_.reset();
    parent_view_.reset();
    fake_stream_factory_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  std::unique_ptr<views::View> parent_view_;
  std::unique_ptr<AudioStreamCoordinator> coordinator_;
  std::unique_ptr<MockStreamFactory> fake_stream_factory_;
};

IN_PROC_BROWSER_TEST_F(AudioStreamCoordinatorTest,
                       ConnectToAudioCapturerAndReceiveBuses) {
  constexpr uint32_t kAudioBusesNumber = 9;  // some arbitrary number

  base::MockCallback<base::RepeatingClosure> callback;
  EXPECT_CALL(callback, Run()).Times(kAudioBusesNumber);
  coordinator_->SetAudioBusReceivedCallbackForTest(callback.Get());

  const uint32_t kSampleRate = 33000;
  coordinator_->ConnectToDevice(fake_stream_factory_->MakeRemote(), "device_id",
                                kSampleRate);
  fake_stream_factory_->WaitToCreateInputStream();

  std::unique_ptr<::media::AudioBus> audio_bus = media::AudioBus::Create(
      {media::AudioParameters::AUDIO_PCM_LOW_LATENCY,
       media::ChannelLayoutConfig::Mono(), kSampleRate, kSampleRate / 20});
  audio_bus->Zero();

  for (uint32_t i = 0; i < kAudioBusesNumber; i++) {
    coordinator_->GetAudioCapturerForTest()->Capture(
        audio_bus.get(),
        /*audio_capture_time=*/base::TimeTicks::Now(),
        /*glitch_info=*/{},
        /*volume=*/1.0);
  }

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return testing::Mock::VerifyAndClear(&callback); }));
}
