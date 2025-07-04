// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_RENDERERS_PAINT_CANVAS_VIDEO_RENDERER_H_
#define MEDIA_RENDERERS_PAINT_CANVAS_VIDEO_RENDERER_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>

#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/threading/thread_checker.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_image.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "media/base/media_export.h"
#include "media/base/timestamp_constants.h"
#include "media/base/video_frame.h"
#include "media/base/video_transformation.h"
#include "media/renderers/video_frame_shared_image_cache.h"

namespace gfx {
class RectF;
}

namespace gpu {
struct Capabilities;

namespace gles2 {
class GLES2Interface;
}
}  // namespace gpu

namespace viz {
class RasterContextProvider;
}

namespace media {
class VideoTextureBacking;

// Handles rendering of VideoFrames to PaintCanvases.
class MEDIA_EXPORT PaintCanvasVideoRenderer {
 public:
  // Specifies the chroma upsampling filter used for pixel formats with chroma
  // subsampling (YUV 4:2:0 and YUV 4:2:2).
  //
  // NOTE: Keep the numeric values in sync with libyuv::FilterMode.
  enum FilterMode {
    kFilterNone = 0,      // Nearest neighbor.
    kFilterBilinear = 2,  // Bilinear interpolation.
  };

  PaintCanvasVideoRenderer();

  PaintCanvasVideoRenderer(const PaintCanvasVideoRenderer&) = delete;
  PaintCanvasVideoRenderer& operator=(const PaintCanvasVideoRenderer&) = delete;

  ~PaintCanvasVideoRenderer();

  // Paints `video_frame` on `canvas`. The below Paint and Copy functions call
  // into this function.
  //
  // If the format of `video_frame` is PIXEL_FORMAT_NATIVE_TEXTURE, `context_3d`
  // and `context_support` must be provided.
  //
  // If `video_frame` is nullptr or an unsupported format, then paint black.
  struct PaintParams {
    // Translate and scale the video frame to `dest_rect` on the specified
    // canvas. If not specified, then this will be a rectangle at 0,0 with the
    // size of `video_frame->visible_rect().size()`.
    std::optional<gfx::RectF> dest_rect;
    // If true, then reinterpret the video frame as being in sRGB color space
    // (though preserving the original YUV to RGB matrix) when drawing.
    bool reinterpret_as_srgb = false;
    // The transformation to apply to the video before the copy.
    VideoTransformation transformation = media::kNoTransformation;
  };
  void Paint(scoped_refptr<VideoFrame> video_frame,
             cc::PaintCanvas* canvas,
             cc::PaintFlags& flags,
             const PaintParams& params,
             viz::RasterContextProvider* raster_context_provider);

  // Paints |video_frame|, scaled to its |video_frame->visible_rect().size()|
  // on |canvas|. Note that the origin of |video_frame->visible_rect()| is
  // ignored -- the copy is done to the origin of |canvas|.
  //
  // If the format of |video_frame| is PIXEL_FORMAT_NATIVE_TEXTURE, |context_3d|
  // and |context_support| must be provided.
  void Copy(scoped_refptr<VideoFrame> video_frame,
            cc::PaintCanvas* canvas,
            viz::RasterContextProvider* raster_context_provider);

  // Convert the contents of |video_frame| to raw RGB pixels. |rgb_pixels|
  // should point into a buffer large enough to hold as many 32 bit RGBA pixels
  // as are in the visible_rect() area of the frame. |premultiply_alpha|
  // indicates whether the R, G, B samples in |rgb_pixels| should be multiplied
  // by alpha. |filter| specifies the chroma upsampling filter used for pixel
  // formats with chroma subsampling. If chroma planes in the pixel format are
  // not subsampled, |filter| is ignored. |disable_threading| indicates whether
  // this method should convert |video_frame| without posting any tasks to
  // base::ThreadPool, regardless of the frame size. If this method is called
  // from a task running in base::ThreadPool, setting |disable_threading| to
  // true can avoid a potential temporary deadlock of base::ThreadPool. See
  // crbug.com/1402841.
  //
  // NOTE: If |video_frame| doesn't have an alpha plane, all the A samples in
  // |rgb_pixels| will be 255 (equivalent to an alpha of 1.0) and therefore the
  // value of |premultiply_alpha| has no effect on the R, G, B samples in
  // |rgb_pixels|.
  static void ConvertVideoFrameToRGBPixels(const media::VideoFrame* video_frame,
                                           void* rgb_pixels,
                                           size_t row_bytes,
                                           bool premultiply_alpha = true,
                                           FilterMode filter = kFilterNone,
                                           bool disable_threading = false);

  // The output format that ConvertVideoFrameToRGBPixels will write.
  static viz::SharedImageFormat GetRGBPixelsOutputFormat();

  // Copy the contents of |video_frame| to |texture| of |destination_gl|.
  //
  // The format of |video_frame| must be VideoFrame::NATIVE_TEXTURE.
  bool CopyVideoFrameTexturesToGLTexture(
      viz::RasterContextProvider* raster_context_provider,
      gpu::gles2::GLES2Interface* destination_gl,
      scoped_refptr<VideoFrame> video_frame,
      unsigned int target,
      unsigned int texture,
      unsigned int internal_format,
      unsigned int format,
      unsigned int type,
      int level,
      SkAlphaType dst_alpha_type,
      GrSurfaceOrigin dst_origin);

  // Copy the CPU-side YUV contents of |video_frame| to texture |texture| in
  // context |destination_gl|.
  // |level|, |internal_format|, |type| specify target texture |texture|.
  // The format of |video_frame| must be mappable.
  // |context_3d| has a GrContext that may be used during the copy.
  // CorrectLastImageDimensions() ensures that the source texture will be
  // cropped to |visible_rect|. Returns true on success.
  bool CopyVideoFrameYUVDataToGLTexture(
      viz::RasterContextProvider* raster_context_provider,
      gpu::gles2::GLES2Interface* destination_gl,
      scoped_refptr<VideoFrame> video_frame,
      unsigned int target,
      unsigned int texture,
      unsigned int internal_format,
      unsigned int format,
      unsigned int type,
      int level,
      SkAlphaType dst_alpha_type,
      GrSurfaceOrigin dst_origin);

  // Calls texImage2D where the texture image data source is the contents of
  // |video_frame|. Texture |texture| needs to be created and bound to |target|
  // before this call and the binding is active upon return.
  // This is an optimization of WebGL |video_frame| TexImage2D implementation
  // for specific combinations of |video_frame| and |texture| formats; e.g. if
  // |frame format| is Y16, optimizes conversion of normalized 16-bit content
  // and calls texImage2D to |texture|. |level|, |internal_format|, |format| and
  // |type| are WebGL texImage2D parameters.
  // Returns false if there is no implementation for given parameters.
  static bool TexImage2D(unsigned target,
                         unsigned texture,
                         gpu::gles2::GLES2Interface* gl,
                         const gpu::Capabilities& gpu_capabilities,
                         VideoFrame* video_frame,
                         int level,
                         int internalformat,
                         unsigned format,
                         unsigned type,
                         GrSurfaceOrigin dst_origin,
                         SkAlphaType dst_alpha_type);

  // Calls texSubImage2D where the texture image data source is the contents of
  // |video_frame|.
  // This is an optimization of WebGL |video_frame| TexSubImage2D implementation
  // for specific combinations of |video_frame| and texture |format| and |type|;
  // e.g. if |frame format| is Y16, converts unsigned 16-bit value to target
  // |format| and calls WebGL texSubImage2D. |level|, |format|, |type|,
  // |xoffset| and |yoffset| are texSubImage2D parameters.
  // Returns false if there is no implementation for given parameters.
  static bool TexSubImage2D(unsigned target,
                            gpu::gles2::GLES2Interface* gl,
                            VideoFrame* video_frame,
                            int level,
                            unsigned format,
                            unsigned type,
                            int xoffset,
                            int yoffset,
                            GrSurfaceOrigin dst_origin,
                            SkAlphaType dst_alpha_type);

  // Copies VideoFrame contents to the `destination` shared image. if
  // `use_visible_rect` is set to true, only `VideoFrame::visible_rect()`
  // portion is copied, otherwise copies all underlying buffer.
  [[nodiscard]] gpu::SyncToken CopyVideoFrameToSharedImage(
      viz::RasterContextProvider* raster_context_provider,
      scoped_refptr<VideoFrame> video_frame,
      const gpu::Mailbox& dest_mailbox,
      const gpu::SyncToken& dest_sync_token,
      bool use_visible_rect);

  // Check whether video frame can be uploaded through
  // CopyVideoFrameToSharedImage(). The limitation comes from
  // VideoFrameYUVConverter.
  bool CanUseCopyVideoFrameToSharedImage(const VideoFrame& video_frame);

  // In general, We hold the most recently painted frame to increase the
  // performance for the case that the same frame needs to be painted
  // repeatedly. Call this function if you are sure the most recent frame will
  // never be painted again, so we can release the resource.
  void ResetCache();

  // Used for unit test.
  gfx::Size LastImageDimensionsForTesting();

 private:
  // This structure wraps information extracted out of a VideoFrame and/or
  // constructed out of it. The various calls in PaintCanvasVideoRenderer must
  // not keep a reference to the VideoFrame so necessary data is extracted out
  // of it.
  struct Cache {
    explicit Cache(VideoFrame::ID frame_id);
    ~Cache();

    // VideoFrame::unique_id() of the videoframe used to generate the cache.
    VideoFrame::ID frame_id;

    // A PaintImage that can be used to draw into a PaintCanvas. This is sized
    // to the visible size of the VideoFrame. Its contents are generated lazily.
    cc::PaintImage paint_image;

    // The backing for the source texture. This is also responsible for managing
    // the lifetime of the texture.
    sk_sp<VideoTextureBacking> texture_backing;

    // The allocated size of VideoFrame texture.
    // This is only set if the VideoFrame was texture-backed.
    gfx::Size coded_size;

    // Used to allow recycling of the previous shared image. This requires that
    // no external users have access to this resource via SkImage. Returns true
    // if the existing resource can be recycled.
    bool Recycle();
  };

  // Update the cache holding the most-recently-painted frame. Returns false
  // if the image couldn't be updated.
  bool UpdateLastImage(scoped_refptr<VideoFrame> video_frame,
                       viz::RasterContextProvider* raster_context_provider);

  std::optional<Cache> cache_;

  // If |cache_| is not used for a while, it's deleted to save memory.
  base::DelayTimer cache_deleting_timer_;
  // Stable paint image id to provide to draw image calls.
  cc::PaintImage::Id renderer_stable_id_;

  // Used for DCHECKs to ensure method calls executed in the correct thread.
  SEQUENCE_CHECKER(sequence_checker_);

  // The RGB shared image cache backing the texture.
  std::unique_ptr<VideoFrameSharedImageCache> rgb_shared_image_cache_;

  // Cache of YUV shared images that are created to upload CPU video frame
  // data to the GPU.
  std::unique_ptr<VideoFrameSharedImageCache> yuv_shared_image_cache_;
};

}  // namespace media

#endif  // MEDIA_RENDERERS_PAINT_CANVAS_VIDEO_RENDERER_H_
