// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/linux/video_capture_device_webrtc.h"

#include "base/notimplemented.h"
#include "media/capture/mojom/image_capture_types.h"
#include "media/capture/video/linux/video_capture_device_factory_webrtc.h"
#include "third_party/webrtc/modules/video_capture/video_capture_factory.h"
#include "third_party/webrtc/modules/video_capture/video_capture_impl.h"

using media::mojom::MeteringMode;

namespace media {

// static
VideoCaptureErrorOrDevice VideoCaptureDeviceWebRtc::Create(
    webrtc::VideoCaptureOptions* options,
    const VideoCaptureDeviceDescriptor& device_descriptor) {
  webrtc::scoped_refptr<webrtc::VideoCaptureModule> capture_module =
      webrtc::VideoCaptureFactory::Create(options,
                                          device_descriptor.device_id.c_str());

  if (!capture_module) {
    return VideoCaptureErrorOrDevice(
        VideoCaptureError::kVideoCaptureSystemDeviceIdNotFound);
  }
  return VideoCaptureErrorOrDevice(
      std::make_unique<VideoCaptureDeviceWebRtc>(options, capture_module));
}

VideoCaptureDeviceWebRtc::VideoCaptureDeviceWebRtc(
    webrtc::VideoCaptureOptions* options,
    webrtc::scoped_refptr<webrtc::VideoCaptureModule> capture_module) {
  options_ = options;
  capture_module_ = capture_module;
  capture_module_->RegisterCaptureDataCallback(this);
}

VideoCaptureDeviceWebRtc::~VideoCaptureDeviceWebRtc() {
  capture_module_->DeRegisterCaptureDataCallback();
}

// VideoCaptureDevice implementation.
void VideoCaptureDeviceWebRtc::AllocateAndStart(
    const VideoCaptureParams& params,
    std::unique_ptr<Client> client) {
  client_ = std::move(client);

  webrtc::VideoCaptureCapability requested_capability;
  requested_capability.width = params.requested_format.frame_size.width();
  requested_capability.height = params.requested_format.frame_size.height();
  requested_capability.maxFPS = params.requested_format.frame_rate;
  requested_capability.videoType =
      VideoCaptureDeviceFactoryWebRtc::WebRtcVideoTypeFromChromiumPixelFormat(
          params.requested_format.pixel_format);
  requested_capability.interlaced = false;

  // Get the best matching capability
  int cap_index;
  webrtc::VideoCaptureCapability best_capability;
  std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
      webrtc::VideoCaptureFactory::CreateDeviceInfo(options_.get()));

  if ((cap_index = info->GetBestMatchedCapability(
           capture_module_->CurrentDeviceName(), requested_capability,
           best_capability)) < 0) {
    client_->OnError(VideoCaptureError::kWebRtcStartCaptureFailed, FROM_HERE,
                     "Failed to find best matched capability");
    return;
  }

  int32_t error = capture_module_->StartCapture(best_capability);
  if (error < 0) {
    client_->OnError(VideoCaptureError::kWebRtcStartCaptureFailed, FROM_HERE,
                     "Failed to start Capturing");
    return;
  }

  capture_module_->CaptureSettings(best_capability);
  capture_format_.pixel_format =
      VideoCaptureDeviceFactoryWebRtc::WebRtcVideoTypeToChromiumPixelFormat(
          best_capability.videoType);
  capture_format_.frame_rate = best_capability.maxFPS;
  capture_format_.frame_size.SetSize(best_capability.width,
                                     best_capability.height);
  base_time_.reset();
  client_->OnStarted();
}

void VideoCaptureDeviceWebRtc::StopAndDeAllocate() {
  capture_module_->StopCapture();
}

void VideoCaptureDeviceWebRtc::TakePhoto(TakePhotoCallback callback) {
  NOTIMPLEMENTED();
}

void VideoCaptureDeviceWebRtc::GetPhotoState(GetPhotoStateCallback callback) {
  if (!capture_module_->CaptureStarted()) {
    return;
  }

  mojom::PhotoStatePtr photo_capabilities = mojo::CreateEmptyPhotoState();

  photo_capabilities->current_focus_mode = MeteringMode::NONE;
  photo_capabilities->current_exposure_mode = MeteringMode::NONE;
  photo_capabilities->current_white_balance_mode = MeteringMode::NONE;
  photo_capabilities->height = mojom::Range::New(
      capture_format_.frame_size.height(), capture_format_.frame_size.height(),
      capture_format_.frame_size.height(), 0 /* step */);
  photo_capabilities->width = mojom::Range::New(
      capture_format_.frame_size.width(), capture_format_.frame_size.width(),
      capture_format_.frame_size.width(), 0 /* step */);
  photo_capabilities->red_eye_reduction = mojom::RedEyeReduction::NEVER;
  photo_capabilities->torch = false;

  std::move(callback).Run(std::move(photo_capabilities));
}

void VideoCaptureDeviceWebRtc::SetPhotoOptions(
    mojom::PhotoSettingsPtr settings,
    SetPhotoOptionsCallback callback) {
  NOTIMPLEMENTED();
  std::move(callback).Run(false);
}

int32_t VideoCaptureDeviceWebRtc::OnRawFrame(
    uint8_t* video_frame,
    size_t video_frame_length,
    const webrtc::VideoCaptureCapability& frame_info,
    webrtc::VideoRotation rotation,
    int64_t capture_time_ms) {
  VideoCaptureFormat format;
  int rotation_degree = 0;
  format = capture_format_;
  format.pixel_format =
      VideoCaptureDeviceFactoryWebRtc::WebRtcVideoTypeToChromiumPixelFormat(
          frame_info.videoType);
  format.frame_size.SetSize(frame_info.width, frame_info.height);

  if (!base_time_) {
    base_time_ = base::Milliseconds(capture_time_ms);
  }

  webrtc::videocapturemodule::VideoCaptureImpl::RotationInDegrees(
      rotation, &rotation_degree);

  // Neither PipeWire nor WebRTC currently implement colorspaces, so the actual
  // colorspace is the default colorspace of the used camera but unknown here.
  client_->OnIncomingCapturedData(
      video_frame, video_frame_length, format, gfx::ColorSpace(),
      rotation_degree, false /* flip_y */, base::TimeTicks::Now(),
      base::Milliseconds(capture_time_ms) - *base_time_,
      /*capture_begin_timestamp=*/std::nullopt,
      /*metadata=*/std::nullopt);
  return 0;
}

}  // namespace media
