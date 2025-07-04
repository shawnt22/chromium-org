// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/audio_decoder.h"

#include <stdint.h>

#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span_writer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/to_string.h"
#include "base/time/time.h"
#include "media/base/audio_bus.h"
#include "media/base/limits.h"
#include "media/filters/audio_file_reader.h"
#include "media/filters/in_memory_url_protocol.h"
#include "media/media_buildflags.h"
#include "third_party/blink/public/platform/web_audio_bus.h"

using media::AudioBus;
using media::AudioFileReader;
using media::InMemoryUrlProtocol;
using std::vector;
using blink::WebAudioBus;

namespace content {

// Decode in-memory audio file data.
bool DecodeAudioFileData(blink::WebAudioBus* destination_bus,
                         base::span<const char> data) {
  DCHECK(destination_bus);
  if (!destination_bus)
    return false;

#if BUILDFLAG(ENABLE_FFMPEG)
  // Uses the FFmpeg library for audio file reading.
  InMemoryUrlProtocol url_protocol(base::as_byte_span(data), false);
  AudioFileReader reader(&url_protocol);

  if (!reader.Open())
    return false;

  size_t number_of_channels = reader.channels();
  double file_sample_rate = reader.sample_rate();

  // Apply sanity checks to make sure crazy values aren't coming out of
  // FFmpeg.
  if (!number_of_channels ||
      number_of_channels > static_cast<size_t>(media::limits::kMaxChannels) ||
      file_sample_rate < media::limits::kMinSampleRate ||
      file_sample_rate > media::limits::kMaxSampleRate)
    return false;

  std::vector<std::unique_ptr<AudioBus>> decoded_audio_packets;
  int number_of_frames = reader.Read(&decoded_audio_packets);

  if (number_of_frames <= 0)
    return false;

  // Allocate and configure the output audio channel data and then
  // copy the decoded data to the destination.
  destination_bus->Initialize(number_of_channels, number_of_frames,
                              file_sample_rate);

  std::vector<base::SpanWriter<float>> dest_channels;
  dest_channels.reserve(number_of_channels);
  for (size_t ch = 0; ch < number_of_channels; ++ch) {
    dest_channels.emplace_back(UNSAFE_TODO(base::span(
        destination_bus->ChannelData(ch), destination_bus->length())));
  }

  // Append all `decoded_audio_packets`, channel per channel.
  for (const auto& packet : decoded_audio_packets) {
    for (size_t ch = 0; ch < number_of_channels; ++ch) {
      dest_channels[ch].Write(packet->channel_span(ch));
    }
  }

  DVLOG(1) << "Decoded file data (unknown duration)-"
           << " data: " << base::ToString(data) << " data size: " << data.size()
           << ", decoded duration: " << (number_of_frames / file_sample_rate)
           << ", number of frames: " << number_of_frames
           << ", estimated frames (if available): "
           << (reader.HasKnownDuration() ? reader.GetNumberOfFrames() : 0)
           << ", sample rate: " << file_sample_rate
           << ", number of channels: " << number_of_channels;

  return number_of_frames > 0;
#else
  return false;
#endif  // BUILDFLAG(ENABLE_FFMPEG)
}

}  // namespace content
