// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "third_party/blink/renderer/core/paint/timing/image_paint_timing_detector.h"

#include <cstddef>

#include "base/feature_list.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/dom/dom_node_ids.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/layout/layout_image_resource.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_image.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/timing/image_element_timing.h"
#include "third_party/blink/renderer/core/paint/timing/largest_contentful_paint_calculator.h"
#include "third_party/blink/renderer/core/paint/timing/paint_timing.h"
#include "third_party/blink/renderer/core/paint/timing/paint_timing_detector.h"
#include "third_party/blink/renderer/core/style/style_fetched_image.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"
#include "third_party/blink/renderer/core/timing/performance_entry.h"
#include "third_party/blink/renderer/core/timing/soft_navigation_context.h"
#include "third_party/blink/renderer/core/timing/soft_navigation_heuristics.h"
#include "third_party/blink/renderer/platform/heap/thread_state.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/traced_value.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

namespace {

// In order for |rect_size| to align with the importance of the image, we
// use this heuristics to alleviate the effect of scaling. For example,
// an image has intrinsic size being 1x1 and scaled to 100x100, but only 50x100
// is visible in the viewport. In this case, |intrinsic_image_size| is 1x1;
// |displayed_image_size| is 100x100. |intrinsic_image_size| is 50x100.
// As the image do not have a lot of content, we down scale |visual_size| by the
// ratio of |intrinsic_image_size|/|displayed_image_size| = 1/10000.
//
// * |visual_size| refers to the size of the |displayed_image_size| after
// clipping and transforming. The size is in the main-frame's coordinate.
// * |intrinsic_image_size| refers to the the image object's original size
// before scaling. The size is in the image object's coordinate.
// * |displayed_image_size| refers to the paint size in the image object's
// coordinate.
uint64_t DownScaleIfIntrinsicSizeIsSmaller(
    uint64_t visual_size,
    const uint64_t& intrinsic_image_size,
    const uint64_t& displayed_image_size) {
  // This is an optimized equivalence to:
  // |visual_size| * min(|displayed_image_size|, |intrinsic_image_size|) /
  // |displayed_image_size|
  if (intrinsic_image_size < displayed_image_size) {
    DCHECK_GT(displayed_image_size, 0u);
    return static_cast<double>(visual_size) * intrinsic_image_size /
           displayed_image_size;
  }
  return visual_size;
}

}  // namespace

double ImageRecord::EntropyForLCP() const {
  if (recorded_size == 0 || !media_timing)
    return 0.0;
  return media_timing->ContentSizeForEntropy() * 8.0 / recorded_size;
}

std::optional<WebURLRequest::Priority> ImageRecord::RequestPriority() const {
  if (!media_timing)
    return std::nullopt;
  return media_timing->RequestPriority();
}

void ImageRecord::Trace(Visitor* visitor) const {
  visitor->Trace(media_timing);
  visitor->Trace(soft_navigation_context_);
}

ImagePaintTimingDetector::ImagePaintTimingDetector(LocalFrameView* frame_view)
    : uses_page_viewport_(
          base::FeatureList::IsEnabled(features::kUsePageViewportInLCP)),
      records_manager_(frame_view),
      frame_view_(frame_view) {}

ImageRecord* ImageRecordsManager::LargestImage() const {
  if (!largest_painted_image_ ||
      (largest_pending_image_ && (largest_painted_image_->recorded_size <
                                  largest_pending_image_->recorded_size))) {
    return largest_pending_image_.Get();
  }
  return largest_painted_image_.Get();
}

void ImagePaintTimingDetector::PopulateTraceValue(
    TracedValue& value,
    const ImageRecord& first_image_paint) {
  value.SetInteger("DOMNodeId", static_cast<int>(first_image_paint.node_id));
  // The media_timing could have been deleted when this is called.
  value.SetString("imageUrl",
                  first_image_paint.media_timing
                      ? String(first_image_paint.media_timing->Url())
                      : "(deleted)");
  value.SetInteger("size", static_cast<int>(first_image_paint.recorded_size));
  value.SetInteger("candidateIndex", ++count_candidates_);
  value.SetBoolean("isMainFrame", frame_view_->GetFrame().IsMainFrame());
  value.SetBoolean("isOutermostMainFrame",
                   frame_view_->GetFrame().IsOutermostMainFrame());
  value.SetBoolean("isEmbeddedFrame",
                   !frame_view_->GetFrame().LocalFrameRoot().IsMainFrame() ||
                       frame_view_->GetFrame().IsInFencedFrameTree());
  if (first_image_paint.lcp_rect_info_) {
    first_image_paint.lcp_rect_info_->OutputToTraceValue(value);
  }
}

void ImagePaintTimingDetector::ReportCandidateToTrace(
    ImageRecord& largest_image_record,
    base::TimeTicks time) {
  if (!PaintTimingDetector::IsTracing())
    return;
  DCHECK(!time.is_null());
  auto value = std::make_unique<TracedValue>();
  PopulateTraceValue(*value, largest_image_record);
  // TODO(yoav): Report first animated frame times as well.
  TRACE_EVENT_MARK_WITH_TIMESTAMP2(
      "loading", "LargestImagePaint::Candidate", time, "data", std::move(value),
      "frame", GetFrameIdForTracing(&frame_view_->GetFrame()));
}

void ImagePaintTimingDetector::ReportNoCandidateToTrace() {
  if (!PaintTimingDetector::IsTracing())
    return;
  auto value = std::make_unique<TracedValue>();
  value->SetInteger("candidateIndex", ++count_candidates_);
  value->SetBoolean("isMainFrame", frame_view_->GetFrame().IsMainFrame());
  value->SetBoolean("isOutermostMainFrame",
                    frame_view_->GetFrame().IsOutermostMainFrame());
  value->SetBoolean("isEmbeddedFrame",
                    !frame_view_->GetFrame().LocalFrameRoot().IsMainFrame() ||
                        frame_view_->GetFrame().IsInFencedFrameTree());
  TRACE_EVENT2("loading", "LargestImagePaint::NoCandidate", "data",
               std::move(value), "frame",
               GetFrameIdForTracing(&frame_view_->GetFrame()));
}

std::pair<ImageRecord*, bool>
ImagePaintTimingDetector::UpdateMetricsCandidate() {
  ImageRecord* largest_image_record = records_manager_.LargestImage();
  base::TimeTicks time = largest_image_record ? largest_image_record->paint_time
                                              : base::TimeTicks();
  bool animated_first_frame_ready =
      largest_image_record &&
      !largest_image_record->first_animated_frame_time.is_null();
  if (animated_first_frame_ready) {
    time = largest_image_record->first_animated_frame_time;
  }

  const uint64_t size =
      largest_image_record ? largest_image_record->recorded_size : 0;

  double bpp =
      largest_image_record ? largest_image_record->EntropyForLCP() : 0.0;

  std::optional<WebURLRequest::Priority> priority =
      largest_image_record ? largest_image_record->RequestPriority()
                           : std::nullopt;

  PaintTimingDetector& detector = frame_view_->GetPaintTimingDetector();
  // Calling NotifyMetricsIfLargestImagePaintChanged only has an impact on
  // PageLoadMetrics, and not on the web exposed metrics.
  //
  // Two different candidates are rare to have the same time and size.
  // So when they are unchanged, the candidate is considered unchanged.
  bool changed =
      detector.GetLargestContentfulPaintCalculator()
          ->NotifyMetricsIfLargestImagePaintChanged(
              time, size, largest_image_record, bpp, std::move(priority));
  if (changed) {
    if (!time.is_null() && largest_image_record->loaded) {
      ReportCandidateToTrace(*largest_image_record, time);
    } else {
      ReportNoCandidateToTrace();
    }
  }
  return {largest_image_record, changed};
}

OptionalPaintTimingCallback
ImagePaintTimingDetector::TakePaintTimingCallback() {
  viewport_size_ = std::nullopt;
  if (!added_entry_in_latest_frame_)
    return std::nullopt;

  added_entry_in_latest_frame_ = false;
  auto callback = WTF::BindOnce(
      [](ImagePaintTimingDetector* self, unsigned int frame_index,
         bool is_recording_lcp, const base::TimeTicks& presentation_timestamp,
         const DOMPaintTimingInfo& paint_timing_info) {
        if (self) {
          self->records_manager_.AssignPaintTimeToRegisteredQueuedRecords(
              presentation_timestamp, paint_timing_info, frame_index,
              is_recording_lcp);
        }
      },
      WrapWeakPersistent(this), frame_index_, IsRecordingLargestImagePaint());
  last_registered_frame_index_ = frame_index_++;

  // This is for unit-testing purposes only. Some of these tests check for UKMs
  // and things that are not covered by WPT.
  // TODO(crbug.com/382396711) convert tests to WPT and remove this.
  if (callback_manager_) {
    callback_manager_->RegisterCallback(std::move(callback));
    return std::nullopt;
  }
  return std::move(callback);
}

void ImagePaintTimingDetector::NotifyImageRemoved(
    const LayoutObject& object,
    const MediaTiming* media_timing) {
  records_manager_.RemoveRecord(
      MediaRecordId::GenerateHash(&object, media_timing));
}

void ImagePaintTimingDetector::StopRecordEntries() {
  // Clear the records queued for presentation callback to ensure no new updates
  // occur.
  records_manager_.ClearImagesQueuedForPaintTime();
  if (frame_view_->GetFrame().IsOutermostMainFrame()) {
    auto* document = frame_view_->GetFrame().GetDocument();
    ukm::builders::Blink_PaintTiming(document->UkmSourceID())
        .SetLCPDebugging_HasViewportImage(contains_full_viewport_image_)
        .Record(document->UkmRecorder());
  }
}

void ImageRecordsManager::AssignPaintTimeToRegisteredQueuedRecords(
    const base::TimeTicks& presentation_timestamp,
    const DOMPaintTimingInfo& paint_timing_info,
    unsigned last_queued_frame_index,
    bool is_recording_lcp) {
  while (!images_queued_for_paint_time_.empty()) {
    ImageRecord* record = images_queued_for_paint_time_.front();
    // Skip any null records at the start of the queue
    if (!record) {
      images_queued_for_paint_time_.pop_front();
      continue;
    }
    // Not ready for this frame yet - we're done with the queue for now.
    if (record->frame_index > last_queued_frame_index) {
      break;
    }

    images_queued_for_paint_time_.pop_front();

    if (record->queue_animated_paint) {
      record->first_animated_frame_time = presentation_timestamp;
      record->queue_animated_paint = false;
    }

    // TODO(crbug.com/364860066): When cleaning up the flag, remove this whole
    // block. This re-enables the old behavior where animated images were not
    // reported until fully loaded.
    if (!record->loaded &&
        !RuntimeEnabledFeatures::ReportFirstFrameTimeAsRenderTimeEnabled()) {
      continue;
    }

    // For non-animated images, if it's not loaded yet (too early) or already
    // painted (too late), move on.
    if ((!record->loaded && record->first_animated_frame_time.is_null()) ||
        !record->paint_time.is_null()) {
      continue;
    }

    // A record may be in |images_queued_for_paint_time_| twice, for instance if
    // is already loaded by the time of its first paint.
    // If it's no longer pending for any other reason, move on.
    auto it = pending_images_.find(record->hash);
    if (it == pending_images_.end()) {
      continue;
    }

    // Set paint time.
    if (record->paint_time.is_null()) {
      record->paint_time = presentation_timestamp;
      record->paint_timing_info = paint_timing_info;
    }
    // Update largest if necessary.
    if (is_recording_lcp &&
        (!largest_painted_image_ ||
         largest_painted_image_->recorded_size < record->recorded_size)) {
      largest_painted_image_ = std::move(it->value);
    }
    // Remove from pending.
    pending_images_.erase(it);
  }
}

bool ImagePaintTimingDetector::RecordImage(
    const LayoutObject& object,
    const gfx::Size& intrinsic_size,
    const MediaTiming& media_timing,
    const PropertyTreeStateOrAlias& current_paint_chunk_properties,
    const StyleImage* style_image,
    const gfx::Rect& image_border) {
  Node* node = object.GetNode();

  if (!node)
    return false;

  // Before the image resource starts loading, <img> has no size info. We wait
  // until the size is known.
  if (image_border.IsEmpty())
    return false;

  if (media_timing.IsBroken()) {
    return false;
  }

  MediaRecordId record_id(&object, &media_timing);
  MediaRecordIdHash record_id_hash = record_id.GetHash();
  ImageRecord* record = nullptr;

  gfx::RectF mapped_visual_rect =
      frame_view_->GetPaintTimingDetector().CalculateVisualRect(
          image_border, current_paint_chunk_properties);
  uint64_t rect_size = ComputeImageRectSize(
      image_border, mapped_visual_rect, intrinsic_size,
      current_paint_chunk_properties, object, media_timing);

  if (int depth = IgnorePaintTimingScope::IgnoreDepth()) {
    // Record the largest loaded image that is hidden due to documentElement
    // being invisible but by no other reason (i.e. IgnoreDepth() needs to be
    // 1).
    if (depth == 1 && IgnorePaintTimingScope::IsDocumentElementInvisible() &&
        media_timing.IsSufficientContentLoadedForPaint()) {
      records_manager_.MaybeUpdateLargestIgnoredImage(
          record_id, rect_size, image_border, mapped_visual_rect,
          IsRecordingLargestImagePaint());
    }
    return false;
  }

  SoftNavigationContext* context = nullptr;
  if (LocalDOMWindow* window = frame_view_->GetFrame().DomWindow()) {
    if (SoftNavigationHeuristics* heuristics =
            window->GetSoftNavigationHeuristics()) {
      context = heuristics->MaybeGetSoftNavigationContextForTiming(node);
    }
  }

  // RecordImage is called whenever an image is painted, which may happen many
  // times for the same record.  The very first paint for this record, we have
  // to create and initialize things, and all subsequent paints we just do a
  // lookup.
  // Note: Mentions of "Image" should all be "Media" since it can include
  // <video> content.
  if (records_manager_.IsRecordedImage(record_id_hash)) {
    record = records_manager_.GetPendingImage(record_id_hash);
  } else {
    double bpp = (rect_size > 0)
                     ? media_timing.ContentSizeForEntropy() * 8.0 / rect_size
                     : 0.0;
    record = records_manager_.RecordFirstPaintAndMaybeCreateImageRecord(
        IsRecordingLargestImagePaint(), record_id, rect_size, image_border,
        mapped_visual_rect, bpp, context);
  }

  // Note: Even if IsRecordedImage() returns `true`, or if we are calling a new
  // `RecordFirstPaintAndMaybeCreateImageRecord`, we might still not have an
  // `ImageRecord*` for the media.  This is because we "record" all new media on
  // first paint, but we only do Record-keeping for some Nodes (i.e. those which
  // actually need timing for some reason).
  if (!record) {
    return false;
  }

  // Check if context changed from the last time we painted this media.
  if (record->soft_navigation_context_ != context) {
    record->soft_navigation_context_ = context;
    // TODO(crbug.com/424437484): Find a mechanism to re-report this media, if
    // it has already been loaded, because it won't report again otherwise.
    // record->loaded = false;
  }

  // If this frame is the first painted frame for animated content, mark it and
  // call `QueueToMeasurePaintTime` (eventually) to measure it.
  // This mechanism works a bit differently for images and video.
  // The stored value may or may not be exposed as the `renderTime` depending on
  // flags.
  if (media_timing.IsPaintedFirstFrame()) {
    added_entry_in_latest_frame_ |=
        records_manager_.OnFirstAnimatedFramePainted(record_id_hash,
                                                     frame_index_);
  }

  // TODO(crbug.com/372929290): This next check will pass when <video> content
  // has loaded just the first frame of video.  This is likely unexpected, and
  // should likely have been handled in the if block for `IsPaintedFirstFrame`,
  // above.
  if (!record->loaded && media_timing.IsSufficientContentLoadedForPaint()) {
    records_manager_.OnImageLoaded(record_id_hash, frame_index_, style_image);
    added_entry_in_latest_frame_ = true;

    if (std::optional<PaintTimingVisualizer>& visualizer =
            frame_view_->GetPaintTimingDetector().Visualizer()) {
      visualizer->DumpImageDebuggingRect(
          object, mapped_visual_rect,
          media_timing.IsSufficientContentLoadedForPaint(), media_timing.Url());
    }
    CHECK(context == record->soft_navigation_context_);
    if (context) {
      context->AddPaintedArea(record);
    }
    return true;
  }
  return false;
}

uint64_t ImagePaintTimingDetector::ComputeImageRectSize(
    const gfx::Rect& image_border,
    const gfx::RectF& mapped_visual_rect,
    const gfx::Size& intrinsic_size,
    const PropertyTreeStateOrAlias& current_paint_chunk_properties,
    const LayoutObject& object,
    const MediaTiming& media_timing) {
  if (std::optional<PaintTimingVisualizer>& visualizer =
          frame_view_->GetPaintTimingDetector().Visualizer()) {
    visualizer->DumpImageDebuggingRect(
        object, mapped_visual_rect,
        media_timing.IsSufficientContentLoadedForPaint(), media_timing.Url());
  }
  uint64_t rect_size = mapped_visual_rect.size().GetArea();
  // Transform visual rect to window before calling downscale.
  gfx::RectF float_visual_rect =
      frame_view_->GetPaintTimingDetector().BlinkSpaceToDIPs(
          gfx::RectF(image_border));
  if (!viewport_size_.has_value()) {
    // If the flag to use page viewport is enabled, we use the page viewport
    // (aka the main frame viewport) for all frames, including iframes. This
    // prevents us from discarding images with size equal to the size of its
    // embedding iframe.
    gfx::Rect viewport_int_rect =
        uses_page_viewport_
            ? frame_view_->GetPage()->GetVisualViewport().VisibleContentRect()
            : frame_view_->GetScrollableArea()->VisibleContentRect();
    gfx::RectF viewport =
        frame_view_->GetPaintTimingDetector().BlinkSpaceToDIPs(
            gfx::RectF(viewport_int_rect));
    viewport_size_ = viewport.size().GetArea();
  }
  // An SVG image size is computed with respect to the virtual viewport of the
  // SVG, so |rect_size| can be larger than |*viewport_size| in edge cases. If
  // the rect occupies the whole viewport, disregard this candidate by saying
  // the size is 0.
  if (rect_size >= *viewport_size_) {
    contains_full_viewport_image_ = true;
    return 0;
  }

  rect_size = DownScaleIfIntrinsicSizeIsSmaller(
      rect_size, intrinsic_size.Area64(), float_visual_rect.size().GetArea());
  return rect_size;
}

void ImagePaintTimingDetector::NotifyImageFinished(
    const LayoutObject& object,
    const MediaTiming* media_timing) {
  records_manager_.NotifyImageFinished(
      MediaRecordId::GenerateHash(&object, media_timing));
}

void ImagePaintTimingDetector::ReportLargestIgnoredImage() {
  added_entry_in_latest_frame_ = true;
  records_manager_.ReportLargestIgnoredImage(frame_index_,
                                             IsRecordingLargestImagePaint());
}

ImageRecordsManager::ImageRecordsManager(LocalFrameView* frame_view)
    : frame_view_(frame_view) {}

bool ImageRecordsManager::OnFirstAnimatedFramePainted(
    MediaRecordIdHash record_id_hash,
    unsigned current_frame_index) {
  ImageRecord* record = GetPendingImage(record_id_hash);
  DCHECK(record);
  if (record->media_timing &&
      !record->media_timing->GetFirstVideoFrameTime().is_null()) {
    // If this is a video record, then we can get the first frame time from the
    // MediaTiming object, and can use that to set the first frame time in the
    // ImageRecord object.
    record->first_animated_frame_time =
        record->media_timing->GetFirstVideoFrameTime();
    if (RuntimeEnabledFeatures::ReportFirstFrameTimeAsRenderTimeEnabled()) {
      record->paint_time = record->first_animated_frame_time;

      // TODO(crbug.com/383568320): this timestamp it not specified, and it's
      // not clear how it should be coarsened
      DOMHighResTimeStamp dom_timestamp =
          DOMWindowPerformance::performance(
              *frame_view_->GetFrame().GetDocument()->domWindow())
              ->MonotonicTimeToDOMHighResTimeStamp(record->paint_time);
      record->paint_timing_info =
          DOMPaintTimingInfo{dom_timestamp, dom_timestamp};
    }
  } else if (record->first_animated_frame_time.is_null()) {
    // Otherwise, this is an animated image, and so we should wait for the
    // presentation callback to fire to set the first frame presentation time.
    record->queue_animated_paint = true;
    QueueToMeasurePaintTime(record, current_frame_index);
    return true;
  }
  return false;
}

void ImageRecordsManager::OnImageLoaded(MediaRecordIdHash record_id_hash,
                                        unsigned current_frame_index,
                                        const StyleImage* style_image) {
  ImageRecord* record = GetPendingImage(record_id_hash);
  DCHECK(record);
  if (!style_image) {
    auto it = image_finished_times_.find(record_id_hash);
    if (it != image_finished_times_.end()) {
      record->load_time = it->value;
      DCHECK(!record->load_time.is_null());
    }
  } else {
    Document* document = frame_view_->GetFrame().GetDocument();
    if (document && document->domWindow()) {
      record->load_time = ImageElementTiming::From(*document->domWindow())
                              .GetBackgroundImageLoadTime(style_image);
      record->origin_clean = style_image->IsFromOriginCleanStyleSheet();
    }
  }
  OnImageLoadedInternal(record, current_frame_index);
}

void ImageRecordsManager::ReportLargestIgnoredImage(
    unsigned current_frame_index,
    bool is_recording_lcp) {
  if (!largest_ignored_image_)
    return;
  Node* node = DOMNodeIds::NodeForId(largest_ignored_image_->node_id);
  if (!node || !node->GetLayoutObject() ||
      !largest_ignored_image_->media_timing) {
    // The image has been removed, so we have no content to report.
    largest_ignored_image_ = nullptr;
    return;
  }

  // Trigger FCP if it's not already set.
  Document* document = frame_view_->GetFrame().GetDocument();
  DCHECK(document);
  PaintTiming::From(*document).MarkFirstContentfulPaint();

  ImageRecord* record = largest_ignored_image_.Get();
  CHECK(record);
  recorded_images_.insert(record->hash);
  AddPendingImage(record, is_recording_lcp);
  OnImageLoadedInternal(record, current_frame_index);
}

void ImageRecordsManager::OnImageLoadedInternal(ImageRecord* record,
                                                unsigned current_frame_index) {
  SetLoaded(record);
  QueueToMeasurePaintTime(record, current_frame_index);
}

void ImageRecordsManager::MaybeUpdateLargestIgnoredImage(
    const MediaRecordId& record_id,
    const uint64_t& visual_size,
    const gfx::Rect& frame_visual_rect,
    const gfx::RectF& root_visual_rect,
    bool is_recording_lcp) {
  if (visual_size && is_recording_lcp &&
      (!largest_ignored_image_ ||
       visual_size > largest_ignored_image_->recorded_size)) {
    largest_ignored_image_ = CreateImageRecord(
        *record_id.GetLayoutObject(), record_id.GetMediaTiming(), visual_size,
        frame_visual_rect, root_visual_rect, record_id.GetHash(),
        /*soft_navigation_context=*/nullptr);
    largest_ignored_image_->load_time = base::TimeTicks::Now();
  }
}

ImageRecord* ImageRecordsManager::RecordFirstPaintAndMaybeCreateImageRecord(
    bool is_recording_lcp,
    const MediaRecordId& record_id,
    const uint64_t& visual_size,
    const gfx::Rect& frame_visual_rect,
    const gfx::RectF& root_visual_rect,
    double bpp,
    SoftNavigationContext* soft_navigation_context) {
  // Don't process the image yet if it is invisible, as it may later become
  // visible, and potentially eligible to be an LCP candidate.
  if (visual_size == 0u) {
    return nullptr;
  }
  recorded_images_.insert(record_id.GetHash());

  // If we are recording LCP, take the timing unless the correct LCP is already
  // larger.
  bool timing_needed_for_lcp =
      is_recording_lcp &&
      !(largest_painted_image_ &&
        largest_painted_image_->recorded_size > visual_size);
  // If we have a context involved in this node creation, we need to do record
  // keeping.
  // Node: Once the soft nav entry is emitted, we might be able to switch to
  // largest-area-only recording.
  bool timing_needed_for_soft_nav = soft_navigation_context != nullptr;

  if (!timing_needed_for_lcp && !timing_needed_for_soft_nav) {
    return nullptr;
  }

  if (bpp < kMinimumEntropyForLCP) {
    return nullptr;
  }

  ImageRecord* record = CreateImageRecord(
      *record_id.GetLayoutObject(), record_id.GetMediaTiming(), visual_size,
      frame_visual_rect, root_visual_rect, record_id.GetHash(),
      soft_navigation_context);
  AddPendingImage(record, is_recording_lcp);
  return record;
}
void ImageRecordsManager::AddPendingImage(ImageRecord* record,
                                          bool is_recording_lcp) {
  if (is_recording_lcp &&
      (!largest_pending_image_ ||
       (largest_pending_image_->recorded_size < record->recorded_size))) {
    largest_pending_image_ = record;
  }
  pending_images_.insert(record->hash, record);
}

ImageRecord* ImageRecordsManager::CreateImageRecord(
    const LayoutObject& object,
    const MediaTiming* media_timing,
    const uint64_t& visual_size,
    const gfx::Rect& frame_visual_rect,
    const gfx::RectF& root_visual_rect,
    MediaRecordIdHash hash,
    SoftNavigationContext* soft_navigaton_context) {
  DCHECK_GT(visual_size, 0u);
  Node* node = object.GetNode();
  DOMNodeId node_id = node->GetDomNodeId();
  return MakeGarbageCollected<ImageRecord>(node_id, media_timing, visual_size,
                                           frame_visual_rect, root_visual_rect,
                                           hash, soft_navigaton_context);
}

void ImageRecordsManager::ClearImagesQueuedForPaintTime() {
  images_queued_for_paint_time_.clear();
}

void ImageRecordsManager::Trace(Visitor* visitor) const {
  visitor->Trace(frame_view_);
  visitor->Trace(largest_painted_image_);
  visitor->Trace(largest_pending_image_);
  visitor->Trace(pending_images_);
  visitor->Trace(images_queued_for_paint_time_);
  visitor->Trace(largest_ignored_image_);
}

void ImagePaintTimingDetector::Trace(Visitor* visitor) const {
  visitor->Trace(records_manager_);
  visitor->Trace(frame_view_);
  visitor->Trace(callback_manager_);
}
}  // namespace blink
