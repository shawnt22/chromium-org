// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/audio/output_device_mixer_manager.h"

#include "base/check.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/trace_event/trace_event.h"
#include "media/audio/audio_io.h"
#include "media/audio/audio_manager.h"
#include "media/base/audio_latency.h"
#include "services/audio/audio_manager_power_user.h"
#include "services/audio/device_listener_output_stream.h"
#include "services/audio/output_device_mixer.h"

namespace {

const char kNormalizedDefaultDeviceId[] = "";

// Helper function which returns a consistent representation of the default
// device ID.
std::string NormalizeIfDefault(const std::string& device_id) {
  return media::AudioDeviceDescription::IsDefaultDevice(device_id)
             ? kNormalizedDefaultDeviceId
             : device_id;
}

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// Aligned with AudioOutputDeviceMixerManagerStreamCreation enum.
enum class StreamCreation {
  kUnmixable,
  kFallbackToUnmixable,
  kUsingNewMixer,
  kUsingExistingMixer,
  kMaxValue = kUsingExistingMixer,
};

}  // namespace

namespace audio {

class OutputDeviceMixerReferenceProvider : public ReferenceSignalProvider {
 public:
  OutputDeviceMixerReferenceProvider(
      OutputDeviceMixerManager* output_device_mixer_manager)
      : output_device_mixer_manager_(output_device_mixer_manager) {}

  ReferenceOpenOutcome StartListening(ReferenceOutput::Listener* listener,
                                      const std::string& device_id) final {
    DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
    output_device_mixer_manager_->StartListening(listener, device_id);
    return ReferenceOpenOutcome::SUCCESS;
  }

  void StopListening(ReferenceOutput::Listener* listener) final {
    DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
    output_device_mixer_manager_->StopListening(listener);
  }

 private:
  SEQUENCE_CHECKER(owning_sequence_);

  // Ownership of the OutputDeviceMixerReferenceProvider goes:
  // StreamFactory -> InputStream -> InputController -> OutputTapper ->
  // ReferenceSignalProvider
  //
  // Ownership of the OutputDeviceMixerManager goes:
  // StreamFactory -> OutputDeviceMixerManager
  //
  // Since input_streams_ is destroyed before output_device_mixer_manager_ in
  // services/audio/stream_factory.h, this pointer will never be dangling.
  const raw_ptr<OutputDeviceMixerManager> output_device_mixer_manager_
      GUARDED_BY_CONTEXT(owning_sequence_);
};

OutputDeviceMixerManager::OutputDeviceMixerManager(
    media::AudioManager* audio_manager,
    OutputDeviceMixer::CreateCallback create_mixer_callback)
    : audio_manager_(audio_manager),
      current_default_device_id_(
          AudioManagerPowerUser(audio_manager_).GetDefaultOutputDeviceID()),
      current_communication_device_id_(AudioManagerPowerUser(audio_manager_)
                                           .GetCommunicationsOutputDeviceID()),
      create_mixer_callback_(std::move(create_mixer_callback)),
      device_change_weak_ptr_factory_(this) {
  // This should be a static_assert, but there is no compile time way to run
  // AudioDeviceDescription::IsDefaultDevice().
  DCHECK(media::AudioDeviceDescription::IsDefaultDevice(
      kNormalizedDefaultDeviceId));
}

OutputDeviceMixerManager::~OutputDeviceMixerManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
}

media::AudioOutputStream* OutputDeviceMixerManager::MakeOutputStream(
    const std::string& device_id,
    const media::AudioParameters& params,
    base::OnceClosure close_stream_on_device_change) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  OutputDeviceMixer* mixer = nullptr;
  StreamCreation stream_creation = StreamCreation::kUnmixable;

  if (params.format() == media::AudioParameters::AUDIO_PCM_LOW_LATENCY) {
    std::string mixer_device_id = ToMixerDeviceId(device_id);
    mixer = FindMixer(mixer_device_id);
    if (mixer) {
      stream_creation = StreamCreation::kUsingExistingMixer;
    } else {
      mixer = AddMixer(mixer_device_id);
      stream_creation = mixer ? StreamCreation::kUsingNewMixer
                              : StreamCreation::kFallbackToUnmixable;
    }
  }

  base::UmaHistogramEnumeration(
      "Media.Audio.OutputDeviceMixerManager.StreamCreation", stream_creation);

  if (mixer) {
    return mixer->MakeMixableStream(params,
                                    std::move(close_stream_on_device_change));
  }

  DLOG(WARNING) << "Making unmixable output stream";
  return CreateDeviceListenerStream(std::move(close_stream_on_device_change),
                                    device_id, params);
}

void OutputDeviceMixerManager::OnDeviceChange() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("audio"),
               "OutputDeviceMixerManager::OnDeviceChange");
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);

  current_default_device_id_ =
      AudioManagerPowerUser(audio_manager_).GetDefaultOutputDeviceID();

  current_communication_device_id_ =
      AudioManagerPowerUser(audio_manager_).GetCommunicationsOutputDeviceID();

  // Invalidate WeakPtrs, cancelling any pending device change callbacks
  // generated by the same device change event.
  device_change_weak_ptr_factory_.InvalidateWeakPtrs();

  OutputDeviceMixers old_mixers;
  output_device_mixers_.swap(old_mixers);

  // Do not call StopListening(), as |old_mixers| are being destroyed anyways.
  for (auto&& mixer : old_mixers)
    mixer->ProcessDeviceChange();
}

void OutputDeviceMixerManager::StartNewListener(
    ReferenceOutput::Listener* listener,
    const std::string& listener_device_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK(IsNormalizedIfDefault(listener_device_id));

  DCHECK(!listener_registration_.contains(listener));
  listener_registration_[listener] = listener_device_id;

  OutputDeviceMixer* mixer = FindMixer(ToMixerDeviceId(listener_device_id));

  if (!mixer)
    return;

  mixer->StartListening(listener);
}

std::unique_ptr<ReferenceSignalProvider>
OutputDeviceMixerManager::GetReferenceSignalProvider() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  return std::make_unique<OutputDeviceMixerReferenceProvider>(this);
}

void OutputDeviceMixerManager::StartListening(
    ReferenceOutput::Listener* listener,
    const std::string& output_device_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);

  std::string listener_device_id = NormalizeIfDefault(output_device_id);

  if (!listener_registration_.contains(listener)) {
    StartNewListener(listener, listener_device_id);
    return;
  }

  std::string registered_listener_device_id =
      listener_registration_.at(listener);

  if (ToMixerDeviceId(registered_listener_device_id) !=
      ToMixerDeviceId(listener_device_id)) {
    // |listener| is listening to a completely different mixer.
    StopListening(listener);
    StartNewListener(listener, listener_device_id);
    return;
  }

  // |listener| is listening to the right mixer, but we might need to update
  // its registration (e.g. when switching between
  // |current_default_device_id_| and kNormalizedDefaultId, or
  // |current_communications_device_id_| and kCommunicationsDeviceId).
  if (registered_listener_device_id != listener_device_id)
    listener_registration_[listener] = listener_device_id;
}

void OutputDeviceMixerManager::StopListening(
    ReferenceOutput::Listener* listener) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);

  const std::string listener_device_id = listener_registration_.at(listener);
  listener_registration_.erase(listener);

  OutputDeviceMixer* mixer = FindMixer(ToMixerDeviceId(listener_device_id));

  // The mixer was never created, because there was no playback to that device
  // (possibly after a device device change). Listening never started, so there
  // is nothing to stop.
  if (!mixer)
    return;

  mixer->StopListening(listener);
}

std::string OutputDeviceMixerManager::ToMixerDeviceId(
    const std::string& device_id) {
  if (media::AudioDeviceDescription::IsDefaultDevice(device_id))
    return kNormalizedDefaultDeviceId;

  DCHECK(!device_id.empty());

  if (device_id == current_default_device_id_)
    return kNormalizedDefaultDeviceId;

  // It's possible for |current_communication_device_id_| and
  // |current_default_device_id_| to match. In that case, replace the
  // communications mixer device ID with the default mixer device ID.
  // Similarly, replace "communications" with kNormalizedDefaultDeviceId when
  // |current_communication_device_id_| is unsupported/unconfigured.
  if (device_id == media::AudioDeviceDescription::kCommunicationsDeviceId &&
      (current_communication_device_id_.empty() ||
       current_communication_device_id_ == current_default_device_id_)) {
    return kNormalizedDefaultDeviceId;
  }

  if (device_id == current_communication_device_id_)
    return media::AudioDeviceDescription::kCommunicationsDeviceId;

  return device_id;
}

OutputDeviceMixer* OutputDeviceMixerManager::FindMixer(
    const std::string& device_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK_EQ(ToMixerDeviceId(device_id), device_id);

  for (const auto& mixer : output_device_mixers_) {
    if (mixer->device_id() == device_id)
      return mixer.get();
  }

  return nullptr;
}

OutputDeviceMixer* OutputDeviceMixerManager::AddMixer(
    const std::string& device_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  DCHECK_EQ(ToMixerDeviceId(device_id), device_id);

  DCHECK(!FindMixer(device_id));

  media::AudioParameters output_params =
      AudioManagerPowerUser(audio_manager_)
          .GetOutputStreamParameters(device_id);

  if (!output_params.IsValid()) {
    LOG(ERROR) << "Adding OutputDeviceMixer failed: invalid output parameters";
    return nullptr;
  }

  output_params.set_frames_per_buffer(media::AudioLatency::GetRtcBufferSize(
      output_params.sample_rate(), output_params.frames_per_buffer()));

  // TODO(crbug.com/40214421): Temporary work around. Mix all audio as stereo
  // and rely on the system channel mapping.
  if (output_params.channel_layout() == media::CHANNEL_LAYOUT_DISCRETE &&
      output_params.channels() >= 2) {
    output_params.Reset(
        output_params.format(), media::ChannelLayoutConfig::Stereo(),
        output_params.sample_rate(), output_params.frames_per_buffer());
  }

  // base::Unretained(this) is safe here, because |output_device_mixers_|
  // are owned by |this|.
  std::unique_ptr<OutputDeviceMixer> output_device_mixer =
      create_mixer_callback_.Run(
          device_id, output_params,
          base::BindRepeating(&OutputDeviceMixerManager::CreateMixerOwnedStream,
                              base::Unretained(this)),
          audio_manager_->GetTaskRunner());

  // The |device_id| might no longer be valid, e.g. if a device was unplugged.
  if (!output_device_mixer) {
    LOG(ERROR) << "Adding OutputDeviceMixer failed: creation error";
    return nullptr;
  }

  auto* mixer = output_device_mixer.get();
  output_device_mixers_.push_back(std::move(output_device_mixer));

  // Attach any interested listeners.
  for (auto&& listener_device_kvp : listener_registration_) {
    if (ToMixerDeviceId(listener_device_kvp.second) == mixer->device_id())
      mixer->StartListening(listener_device_kvp.first);
  }

  return mixer;
}

base::OnceClosure OutputDeviceMixerManager::GetOnDeviceChangeCallback() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  return base::BindOnce(&OutputDeviceMixerManager::OnDeviceChange,
                        device_change_weak_ptr_factory_.GetWeakPtr());
}

media::AudioOutputStream* OutputDeviceMixerManager::CreateMixerOwnedStream(
    const std::string& device_id,
    const media::AudioParameters& params) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);
  return CreateDeviceListenerStream(GetOnDeviceChangeCallback(), device_id,
                                    params);
}

media::AudioOutputStream* OutputDeviceMixerManager::CreateDeviceListenerStream(
    base::OnceClosure on_device_change_callback,
    const std::string& device_id,
    const media::AudioParameters& params) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(owning_sequence_);

  media::AudioOutputStream* stream =
      audio_manager_->MakeAudioOutputStreamProxy(params, device_id);
  if (!stream) {
    LOG(ERROR) << "Stream proxy limit reached";
    return nullptr;
  }

  // If this stream is created via CreateMixerOwnedStream(),
  // |on_device_change_callback| will call OnDeviceChange(), cancel pending
  // calls to OnDeviceChange(), and release all mixer owned streams.
  //
  // If we are directly creating this stream, |on_device_change_callback| will
  // synchronously close the returned stream.
  return new DeviceListenerOutputStream(audio_manager_, stream,
                                        std::move(on_device_change_callback));
}

bool OutputDeviceMixerManager::IsNormalizedIfDefault(
    const std::string& device_id) {
  return device_id == kNormalizedDefaultDeviceId ||
         !media::AudioDeviceDescription::IsDefaultDevice(device_id);
}

}  // namespace audio
