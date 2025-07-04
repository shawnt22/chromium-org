// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_WEBAUDIO_AUDIO_CONTEXT_MANAGER_IMPL_H_
#define CONTENT_BROWSER_MEDIA_WEBAUDIO_AUDIO_CONTEXT_MANAGER_IMPL_H_

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "third_party/blink/public/mojom/webaudio/audio_context_manager.mojom.h"

namespace base {
class TickClock;
}

namespace content {

class RenderFrameHost;

// Implements the mojo interface between WebAudio and the browser so that
// WebAudio can report when audible sounds from an AudioContext starts and
// stops. A manager instance can be associated with multiple AudioContexts.
//
// We do not expect to see more than 3~4 AudioContexts per render frame, so
// handling multiple contexts would not be a significant bottle neck.
class CONTENT_EXPORT AudioContextManagerImpl final
    : public content::DocumentService<blink::mojom::AudioContextManager> {
 public:
  AudioContextManagerImpl(const AudioContextManagerImpl&) = delete;
  AudioContextManagerImpl& operator=(const AudioContextManagerImpl&) = delete;

  static void Create(
      RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<blink::mojom::AudioContextManager> receiver);
  static AudioContextManagerImpl& CreateForTesting(
      RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<blink::mojom::AudioContextManager> receiver);

  // blink::mojom::AudioContextManager implementations.
  // Notify observers that audible audio started/stopped playing from an
  // AudioContext.
  void AudioContextAudiblePlaybackStarted(uint32_t audio_context_id) final;
  void AudioContextAudiblePlaybackStopped(uint32_t audio_context_id) final;

  // Tracks the creation or closure of a AudioContext to maintain the count of
  // concurrent contexts.
  void AudioContextCreated(uint32_t audio_context_id) final;
  void AudioContextClosed(uint32_t audio_context_id) final;

  void set_clock_for_testing(base::TickClock* clock) { clock_ = clock; }

 private:
  explicit AudioContextManagerImpl(
      RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<blink::mojom::AudioContextManager> receiver);
  ~AudioContextManagerImpl() override;

  FRIEND_TEST_ALL_PREFIXES(AudioContextManagerImplTest,
                           TracksMaxConcurrentContexts);
  FRIEND_TEST_ALL_PREFIXES(AudioContextManagerImplTest, NoContextsCreated);

  // Send measured audible duration to UKM database.
  void RecordAudibleTime(base::TimeDelta);

  // To track pending audible time. Stores ID of AudioContext (uint32_t) and
  // the start time of audible period (base::TimeTicks).
  base::flat_map<uint32_t, base::TimeTicks> pending_audible_durations_;

  // Clock used to calculate time between start and stop event. Can be override
  // by tests.
  // It is not owned by the implementation.
  raw_ptr<const base::TickClock> clock_;

  // The set of currently active AudioContexts in the frame. Used to track the
  // maximum number of concurrent contexts.
  base::flat_set<uint32_t> concurrent_audio_context_ids_;
  // The maximum number of concurrent AudioContexts observed during the lifetime
  // of this manager. This value is recorded to a UMA histogram on destruction.
  size_t max_concurrent_audio_contexts_ = 0;
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_WEBAUDIO_AUDIO_CONTEXT_MANAGER_IMPL_H_
