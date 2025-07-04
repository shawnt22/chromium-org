// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/android/aaudio_output.h"

#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/numerics/checked_math.h"
#include "base/task/sequenced_task_runner.h"
#include "media/audio/android/audio_device.h"
#include "media/audio/android/audio_manager_android.h"
#include "media/audio/audio_manager.h"
#include "media/base/amplitude_peak_detector.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_sample_types.h"

namespace media {

AAudioOutputStream::AAudioOutputStream(
    AudioManagerAndroid* manager,
    const AudioParameters& params,
    android::AudioDevice device,
    aaudio_usage_t usage,
    AmplitudePeakDetector::PeakDetectedCB peak_detected_cb)
    : audio_manager_(manager),
      params_(params),
      peak_detector_(std::move(peak_detected_cb)),
      stream_wrapper_(this,
                      AAudioStreamWrapper::StreamType::kOutput,
                      params,
                      std::move(device),
                      usage) {
  CHECK(params_.IsValid());
}

AAudioOutputStream::~AAudioOutputStream() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void AAudioOutputStream::Flush() {}

bool AAudioOutputStream::Open() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!stream_wrapper_.Open()) {
    return false;
  }

  CHECK(!audio_bus_);
  audio_bus_ = AudioBus::Create(params_);

  return true;
}

void AAudioOutputStream::Close() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  Stop();
  stream_wrapper_.Close();

  if (audio_manager_) {
    // Note: This must be last, it will delete |this|.
    audio_manager_->ReleaseOutputStream(this);
  }
}

void AAudioOutputStream::Start(AudioSourceCallback* callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  {
    base::AutoLock al(lock_);

    // The device might have been disconnected between Open() and Start().
    if (device_changed_) {
      callback->OnError(AudioSourceCallback::ErrorType::kDeviceChange);
      return;
    }

    CHECK(!callback_);
    callback_ = callback;
  }

  if (stream_wrapper_.Start()) {
    // Successfully started `stream_wrapper_`.
    return;
  }

  {
    base::AutoLock al(lock_);
    callback_->OnError(AudioSourceCallback::ErrorType::kUnknown);
    callback_ = nullptr;
  }
}

void AAudioOutputStream::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AudioSourceCallback* temp_error_callback;
  {
    base::AutoLock al(lock_);
    if (!callback_) {
      return;
    }

    // Save a copy of copy of the callback for error reporting.
    temp_error_callback = callback_;

    // OnAudioDataRequested should no longer provide data from this point on.
    callback_ = nullptr;
  }

  if (!stream_wrapper_.Stop()) {
    temp_error_callback->OnError(AudioSourceCallback::ErrorType::kUnknown);
  }
}

bool AAudioOutputStream::OnAudioDataRequested(void* audio_data,
                                              int32_t num_frames) {
  CHECK_EQ(num_frames, audio_bus_->frames());

  base::AutoLock al(lock_);
  if (!callback_) {
    // Stop() might have already been called, but there can still be pending
    // data callbacks in flight.

    size_t total_size;
    auto total_frames = base::CheckMul(num_frames, audio_bus_->channels());
    if (base::CheckMul(total_frames, sizeof(float))
            .AssignIfValid(&total_size)) {
      // SAFETY: Unfortunately, `audio_data` comes from the OS, and we must use
      // some unsafe buffers. This should be safe because we set the format
      // as AAUDIO_FORMAT_PCM_FLOAT in AAudioStreamWrapper (and CHECK that it is
      // set), so we know this void* is pointing towards floats. We also control
      // the channel count, and the OS gives us `num_frames`.
      UNSAFE_BUFFERS(memset(audio_data, 0, total_size));
    }
    return false;
  }

  const base::TimeTicks delay_timestamp = base::TimeTicks::Now();
  const base::TimeDelta delay = stream_wrapper_.GetOutputDelay(delay_timestamp);

  const int frames_filled =
      callback_->OnMoreData(delay, delay_timestamp, {}, audio_bus_.get());

  peak_detector_.FindPeak(audio_bus_.get());

  audio_bus_->Scale(muted_ ? 0.0 : volume_);
  audio_bus_->ToInterleaved<Float32SampleTypeTraits>(
      frames_filled, reinterpret_cast<float*>(audio_data));

  return true;
}

void AAudioOutputStream::OnDeviceChange() {
  base::AutoLock al(lock_);

  device_changed_ = true;

  if (!callback_) {
    // Report the device change in Start() instead.
    return;
  }

  callback_->OnError(AudioSourceCallback::ErrorType::kDeviceChange);
}

void AAudioOutputStream::OnError() {
  base::AutoLock al(lock_);

  if (!callback_) {
    return;
  }

  if (device_changed_) {
    // We should have already reported a device change error, either in
    // OnDeviceChange() or in Start(). In both cases, `this` should be closed
    // and deleted soon, so silently ignore additional error reporting.
    return;
  }

  callback_->OnError(AudioSourceCallback::ErrorType::kUnknown);
}

void AAudioOutputStream::SetVolume(double volume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  double volume_override = 0;
  if (audio_manager_ &&
      audio_manager_->HasOutputVolumeOverride(&volume_override)) {
    volume = volume_override;
  }

  if (volume < 0.0 || volume > 1.0) {
    return;
  }

  base::AutoLock al(lock_);
  volume_ = volume;
}

void AAudioOutputStream::GetVolume(double* volume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::AutoLock al(lock_);
  *volume = volume_;
}

void AAudioOutputStream::SetMute(bool muted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::AutoLock al(lock_);
  muted_ = muted;
}

}  // namespace media
