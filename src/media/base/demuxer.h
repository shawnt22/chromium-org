// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_DEMUXER_H_
#define MEDIA_BASE_DEMUXER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <vector>

#include "base/time/time.h"
#include "media/base/container_names.h"
#include "media/base/demuxer_stream.h"
#include "media/base/eme_constants.h"
#include "media/base/media_export.h"
#include "media/base/media_resource.h"
#include "media/base/media_track.h"
#include "media/base/pipeline_status.h"
#include "media/base/ranges.h"

namespace media {

class MediaTracks;

// WARNING: These values are reported to metrics. Entries should not be
// renumbered and numeric values should not be reused. When adding new entries,
// also update media::mojom::RendererType & tools/metrics/histograms/enums.xml.
enum class DemuxerType {
  kUnknownDemuxer = 0,
  kMockDemuxer = 1,
  kFFmpegDemuxer = 2,
  kChunkDemuxer = 3,
  // kMediaUrlDemuxer = 4,     // Deprecated
  kFrameInjectingDemuxer = 5,
  kStreamProviderDemuxer = 6,
  kManifestDemuxer = 7,
};

class MEDIA_EXPORT DemuxerHost {
 public:
  // Notify the host that buffered time ranges have changed. Note that buffered
  // time ranges can grow (when new media data is appended), but they can also
  // shrink (when buffering reaches limit capacity and some buffered data
  // becomes evicted, e.g. due to MSE GC algorithm, or by explicit removal of
  // ranges directed by MSE web app).
  virtual void OnBufferedTimeRangesChanged(
      const Ranges<base::TimeDelta>& ranges) = 0;

  // Sets the duration of the media in microseconds.
  // Duration may be kInfiniteDuration if the duration is not known.
  virtual void SetDuration(base::TimeDelta duration) = 0;

  // Stops execution of the pipeline due to a fatal error. Do not call this
  // method with PIPELINE_OK. Stopping is not immediate so demuxers must be
  // prepared to soft fail on subsequent calls. E.g., if Demuxer::Seek() is
  // called after an unrecoverable error the provided PipelineStatusCallback
  // must be called with an error.
  virtual void OnDemuxerError(PipelineStatus error) = 0;

 protected:
  virtual ~DemuxerHost();
};

class MEDIA_EXPORT Demuxer : public MediaResource {
 public:
  // A new potentially encrypted stream has been parsed.
  // First parameter - The type of initialization data.
  // Second parameter - The initialization data associated with the stream.
  using EncryptedMediaInitDataCB =
      base::RepeatingCallback<void(EmeInitDataType type,
                                   const std::vector<uint8_t>& init_data)>;

  // Notifies demuxer clients that media track configuration has been updated
  // (e.g. the initial stream metadata has been parsed successfully, or a new
  // init segment has been parsed successfully in MSE case).
  using MediaTracksUpdatedCB =
      base::RepeatingCallback<void(std::unique_ptr<MediaTracks>)>;

  // Called once the demuxer has finished enabling or disabling tracks.
  using TrackChangeCB = base::OnceCallback<void(DemuxerStream*)>;

  Demuxer();

  Demuxer(const Demuxer&) = delete;
  Demuxer& operator=(const Demuxer&) = delete;

  ~Demuxer() override;

  // Returns the name of the demuxer for logging purpose.
  virtual std::string GetDisplayName() const = 0;

  // Get the demuxer type for identification purposes.
  virtual DemuxerType GetDemuxerType() const = 0;

  // Completes initialization of the demuxer.
  //
  // The demuxer does not own |host| as it is guaranteed to outlive the
  // lifetime of the demuxer. Don't delete it!  |status_cb| must only be run
  // after this method has returned.
  virtual void Initialize(DemuxerHost* host,
                          PipelineStatusCallback status_cb) = 0;

  // Aborts any pending read operations that the demuxer is involved with; any
  // read aborted will be aborted with a status of kAborted. Future reads will
  // also be aborted until Seek() is called.
  virtual void AbortPendingReads() = 0;

  // Indicates that a new Seek() call is on its way. Implementations may abort
  // pending reads and future Read() calls may return kAborted until Seek() is
  // executed. |seek_time| is the presentation timestamp of the new Seek() call.
  //
  // In actual use, this call occurs on the main thread while Seek() is called
  // on the media thread. StartWaitingForSeek() can be used to synchronize the
  // two.
  //
  // StartWaitingForSeek() MUST be called before Seek().
  virtual void StartWaitingForSeek(base::TimeDelta seek_time) = 0;

  // Indicates that the current Seek() operation is obsoleted by a new one.
  // Implementations can expect that StartWaitingForSeek() will be called
  // when the current seek operation completes.
  //
  // Like StartWaitingForSeek(), CancelPendingSeek() is called on the main
  // thread. Ordering with respect to the to-be-canceled Seek() is not
  // guaranteed. Regardless of ordering, implementations may abort pending reads
  // and may return kAborted from future Read() calls, until after
  // StartWaitingForSeek() and the following Seek() call occurs.
  //
  // |seek_time| should match that passed to the next StartWaitingForSeek(), but
  // may not if the seek target changes again before the current seek operation
  // completes or is aborted.
  virtual void CancelPendingSeek(base::TimeDelta seek_time) = 0;

  // Carry out any actions required to seek to the given time, executing the
  // callback upon completion.
  virtual void Seek(base::TimeDelta time, PipelineStatusCallback status_cb) = 0;

  // Returns whether this demuxer supports seeking and has a timeline. If false,
  // Seek(), CancelPendingSeek(), StartWaitingForSeek(), and GetTimelineOffset()
  // should be noops.
  virtual bool IsSeekable() const = 0;

  // Stops this demuxer.
  //
  // After this call the demuxer may be destroyed. It is illegal to call any
  // method (including Stop()) after a demuxer has stopped.
  virtual void Stop() = 0;

  // Returns the starting time for the media file; it's always positive.
  virtual base::TimeDelta GetStartTime() const = 0;

  // Returns Time represented by presentation timestamp 0.
  // If the timstamps are not associated with a Time, then
  // a null Time is returned.
  virtual base::Time GetTimelineOffset() const = 0;

  // Returns the memory usage in bytes for the demuxer.
  virtual int64_t GetMemoryUsage() const = 0;

  // Returns the container name to use for metrics.
  // Implementations where this is not meaningful will return an empty value.
  // Implementations that do provide values should always provide a value,
  // returning CONTAINER_UNKNOWN in cases where the container is not known.
  virtual std::optional<container_names::MediaContainerName>
  GetContainerForMetrics() const = 0;

  // Ask the demuxer to switch the enabled track for the selected stream type.
  // `change_completed_cb` is fired after the appropriate stream changes are
  // made.
  virtual void OnTracksChanged(DemuxerStream::Type track_type,
                               std::optional<MediaTrack::Id> track_id,
                               base::TimeDelta curr_time,
                               TrackChangeCB change_completed_cb) = 0;

  // Allows a demuxer to change behavior based on the playback rate, including
  // but not limited to changing the amount of buffer space.
  virtual void SetPlaybackRate(double rate) = 0;

  // Allow canChangeType to be disabled.
  virtual void DisableCanChangeType();
};

}  // namespace media

#endif  // MEDIA_BASE_DEMUXER_H_
