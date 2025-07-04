// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/vr/test/mock_xr_device_hook_base.h"

#include "content/public/test/xr_test_utils.h"
#include "device/vr/public/mojom/isolated_xr_service.mojom.h"
#include "mojo/public/cpp/bindings/sync_call_restrictions.h"

#if BUILDFLAG(IS_ANDROID)
#include "components/webxr/android/openxr_device_provider.h"
#endif

// TODO(crbug.com/41418750): Remove these conversion functions as part of
// the switch to only mojom types.
device_test::mojom::ControllerRole DeviceToMojoControllerRole(
    device::ControllerRole role) {
  switch (role) {
    case device::kControllerRoleInvalid:
      return device_test::mojom::ControllerRole::kControllerRoleInvalid;
    case device::kControllerRoleRight:
      return device_test::mojom::ControllerRole::kControllerRoleRight;
    case device::kControllerRoleLeft:
      return device_test::mojom::ControllerRole::kControllerRoleLeft;
    case device::kControllerRoleVoice:
      return device_test::mojom::ControllerRole::kControllerRoleVoice;
  }
}

device_test::mojom::ControllerFrameDataPtr DeviceToMojoControllerFrameData(
    const device::ControllerFrameData& data) {
  device_test::mojom::ControllerFrameDataPtr ret =
      device_test::mojom::ControllerFrameData::New();
  ret->packet_number = data.packet_number;
  ret->buttons_pressed = data.buttons_pressed;
  ret->buttons_touched = data.buttons_touched;
  ret->supported_buttons = data.supported_buttons;
  for (unsigned int i = 0; i < device::kMaxNumAxes; ++i) {
    ret->axis_data.emplace_back(device_test::mojom::ControllerAxisData::New());
    ret->axis_data[i]->x = data.axis_data[i].x;
    ret->axis_data[i]->y = data.axis_data[i].y;
    ret->axis_data[i]->axis_type = data.axis_data[i].axis_type;
  }
  ret->role = DeviceToMojoControllerRole(data.role);
  ret->is_valid = data.is_valid;
  ret->pose_data = device_test::mojom::PoseFrameData::New();
  ret->pose_data->device_to_origin = gfx::Transform();
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      ret->pose_data->device_to_origin->set_rc(
          row, col, data.pose_data.device_to_origin[row + col * 4]);
    }
  }

  if (data.has_hand_data) {
    device::mojom::XRHandTrackingDataPtr hand_tracking_data =
        device::mojom::XRHandTrackingData::New();
    hand_tracking_data->hand_joint_data =
        std::vector<device::mojom::XRHandJointDataPtr>{};
    auto& joint_data = hand_tracking_data->hand_joint_data;

    // We need to use `resize` here to create default data fields so we can use
    // [] indexing to ensure things are added to the right spot.
    joint_data.resize(std::size(data.hand_data));
    for (const auto& joint_entry : data.hand_data) {
      uint32_t joint_index = static_cast<uint32_t>(joint_entry.joint);

      joint_data[joint_index] = device::mojom::XRHandJointData::New(
          joint_entry.joint, joint_entry.mojo_from_joint, joint_entry.radius);
    }

    ret->hand_data = std::move(hand_tracking_data);
  }

  return ret;
}

MockXRDeviceHookBase::MockXRDeviceHookBase()
    : tracked_classes_{
          device_test::mojom::TrackedDeviceClass::kTrackedDeviceHmd} {
  thread_ = std::make_unique<base::Thread>("MockXRDeviceHookThread");
  thread_->Start();

  // By default, `mock_device_sequence_` is bound to the constructing thread
  // (i.e. the main test thread). We must detach it so it can be bound to
  // our internal `thread_` the first time a checked method is called.
  DETACH_FROM_SEQUENCE(mock_device_sequence_);

  // TODO(https://crbug.com/381913614): Instead of this pattern, consider
  // spinning up/holding onto and setting the test hook on the XrRuntimeManager,
  // which could pass on to providers.
#if BUILDFLAG(IS_WIN)
  content::GetXRDeviceServiceForTesting()->BindTestHook(
      service_test_hook_.BindNewPipeAndPassReceiver());

  mojo::ScopedAllowSyncCallForTesting scoped_allow_sync;
  service_test_hook_->SetTestHook(
      receiver_.BindNewPipeAndPassRemote(thread_->task_runner()));
#elif BUILDFLAG(IS_ANDROID)
  mojo::ScopedAllowSyncCallForTesting scoped_allow_sync;
  webxr::OpenXrDeviceProvider::SetTestHook(
      receiver_.BindNewPipeAndPassRemote(thread_->task_runner()));
#endif
}

MockXRDeviceHookBase::~MockXRDeviceHookBase() {
  StopHooking();

  if (thread_->IsRunning()) {
    thread_->Stop();
  }
}

void MockXRDeviceHookBase::StopHooking() {
  // Ensure that this is being called from our main thread, and not the mock
  // device thread.
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  // We don't call service_test_hook_->SetTestHook(mojo::NullRemote()), since
  // that will potentially deadlock with reentrant or crossing synchronous mojo
  // calls.
  service_test_hook_.reset();
  // Unretained is safe here because we are going to block until this message
  // has been processed.
  thread_->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&mojo::Receiver<device_test::mojom::XRTestHook>::reset,
                     base::Unretained(&receiver_)));
  // Mojo messages and this destruction task are the only thing that should be
  // posted to the thread. Since we're destroying the mojo pipe, we can safely
  // block here.
  thread_->FlushForTesting();
}

void MockXRDeviceHookBase::WaitNumFrames(uint32_t num_frames) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  WaitForTotalFrameCount(frame_count_ + num_frames);
}

void MockXRDeviceHookBase::WaitForTotalFrameCount(uint32_t total_count) {
  DCHECK(!can_signal_wait_loop_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  target_frame_count_ = total_count;

  // No need to wait if we've already had at least the requested number of
  // frames submitted.
  if (frame_count_ >= target_frame_count_) {
    return;
  }
  wait_loop_ = std::make_unique<base::RunLoop>(
      base::RunLoop::Type::kNestableTasksAllowed);
  can_signal_wait_loop_ = true;

  wait_loop_->Run();

  can_signal_wait_loop_ = false;
  wait_loop_.reset();
}

void MockXRDeviceHookBase::OnFrameSubmitted(
    std::vector<device_test::mojom::ViewDataPtr> views,
    device_test::mojom::XRTestHook::OnFrameSubmittedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  frame_count_++;
  ProcessSubmittedFrameUnlocked(std::move(views));
  if (can_signal_wait_loop_ && frame_count_ >= target_frame_count_) {
    wait_loop_->Quit();
    can_signal_wait_loop_ = false;
  }

  std::move(callback).Run();
}

void MockXRDeviceHookBase::WaitGetDeviceConfig(
    device_test::mojom::XRTestHook::WaitGetDeviceConfigCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  device_test::mojom::DeviceConfigPtr ret =
      device_test::mojom::DeviceConfig::New();
  ret->interpupillary_distance = 0.1f;
  ret->projection_left = device_test::mojom::ProjectionRaw::New(1, 1, 1, 1);
  ret->projection_right = device_test::mojom::ProjectionRaw::New(1, 1, 1, 1);
  std::move(callback).Run(std::move(ret));
}

void MockXRDeviceHookBase::WaitGetPresentingPose(
    device_test::mojom::XRTestHook::WaitGetPresentingPoseCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  auto pose = device_test::mojom::PoseFrameData::New();
  pose->device_to_origin = gfx::Transform();
  std::move(callback).Run(std::move(pose));
}

void MockXRDeviceHookBase::WaitGetMagicWindowPose(
    device_test::mojom::XRTestHook::WaitGetMagicWindowPoseCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  auto pose = device_test::mojom::PoseFrameData::New();
  pose->device_to_origin = gfx::Transform();
  std::move(callback).Run(std::move(pose));
}

void MockXRDeviceHookBase::WaitGetControllerRoleForTrackedDeviceIndex(
    unsigned int index,
    device_test::mojom::XRTestHook::
        WaitGetControllerRoleForTrackedDeviceIndexCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  device_test::mojom::ControllerRole role =
      device_test::mojom::ControllerRole::kControllerRoleInvalid;
  {
    base::AutoLock lock(lock_);
    auto iter = controller_data_map_.find(index);
    if (iter != controller_data_map_.end()) {
      role = DeviceToMojoControllerRole(iter->second.role);
    }
  }

  std::move(callback).Run(role);
}

void MockXRDeviceHookBase::WaitGetTrackedDeviceClass(
    unsigned int index,
    device_test::mojom::XRTestHook::WaitGetTrackedDeviceClassCallback
        callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  DCHECK(index < device::kMaxTrackedDevices);
  device_test::mojom::TrackedDeviceClass tracked_class =
      device_test::mojom::TrackedDeviceClass::kTrackedDeviceInvalid;
  {
    base::AutoLock lock(lock_);
    tracked_class = tracked_classes_[index];
  }
  std::move(callback).Run(tracked_class);
}

void MockXRDeviceHookBase::WaitGetControllerData(
    unsigned int index,
    device_test::mojom::XRTestHook::WaitGetControllerDataCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  device::ControllerFrameData data;
  {
    base::AutoLock lock(lock_);
    if (tracked_classes_[index] ==
        device_test::mojom::TrackedDeviceClass::kTrackedDeviceController) {
      auto iter = controller_data_map_.find(index);
      CHECK(iter != controller_data_map_.end());
      data = iter->second;
    } else {
      // Default to not being valid so that controllers aren't connected unless
      // a test specifically enables it.
      data =
          CreateValidController(device::ControllerRole::kControllerRoleInvalid);
      data.is_valid = false;
    }
  }
  std::move(callback).Run(DeviceToMojoControllerFrameData(data));
}

void MockXRDeviceHookBase::WaitGetEventData(
    device_test::mojom::XRTestHook::WaitGetEventDataCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  device_test::mojom::EventDataPtr ret = device_test::mojom::EventData::New();
  ret->type = device_test::mojom::EventType::kNoEvent;
  {
    base::AutoLock lock(lock_);
    if (!event_data_queue_.empty()) {
      ret = device_test::mojom::EventData::New(event_data_queue_.front());
      event_data_queue_.pop();
    }
  }
  std::move(callback).Run(std::move(ret));
}

unsigned int MockXRDeviceHookBase::ConnectController(
    const device::ControllerFrameData& initial_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  base::AutoLock lock(lock_);
  // Find the first open tracked device slot and fill that.
  for (unsigned int i = 0; i < device::kMaxTrackedDevices; ++i) {
    if (tracked_classes_[i] ==
        device_test::mojom::TrackedDeviceClass::kTrackedDeviceInvalid) {
      tracked_classes_[i] =
          device_test::mojom::TrackedDeviceClass::kTrackedDeviceController;
      controller_data_map_.insert(std::make_pair(i, initial_data));
      return i;
    }
  }
  // We shouldn't be running out of slots during a test.
  NOTREACHED();
}

void MockXRDeviceHookBase::TerminateDeviceServiceProcessForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  mojo::ScopedAllowSyncCallForTesting scoped_allow_sync;
  service_test_hook_->TerminateDeviceServiceProcessForTesting();
}

void MockXRDeviceHookBase::UpdateController(
    unsigned int index,
    const device::ControllerFrameData& updated_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  base::AutoLock lock(lock_);
  auto iter = controller_data_map_.find(index);
  CHECK(iter != controller_data_map_.end());
  iter->second = updated_data;
}

void MockXRDeviceHookBase::DisconnectController(unsigned int index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  base::AutoLock lock(lock_);
  DCHECK(tracked_classes_[index] ==
         device_test::mojom::TrackedDeviceClass::kTrackedDeviceController);
  auto iter = controller_data_map_.find(index);
  CHECK(iter != controller_data_map_.end());
  controller_data_map_.erase(iter);
  tracked_classes_[index] =
      device_test::mojom::TrackedDeviceClass::kTrackedDeviceInvalid;
}

device::ControllerFrameData MockXRDeviceHookBase::CreateValidController(
    device::ControllerRole role) {
  // Stateless helper may be called on any sequence.
  device::ControllerFrameData ret;
  // Because why shouldn't a 64 button controller exist?
  ret.supported_buttons = UINT64_MAX;
  std::ranges::fill(ret.axis_data, device::ControllerAxisData{});
  ret.role = role;
  ret.is_valid = true;
  // Identity matrix.
  ret.pose_data.device_to_origin[0] = 1;
  ret.pose_data.device_to_origin[5] = 1;
  ret.pose_data.device_to_origin[10] = 1;
  ret.pose_data.device_to_origin[15] = 1;
  return ret;
}

void MockXRDeviceHookBase::PopulateEvent(device_test::mojom::EventData data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  base::AutoLock lock(lock_);
  event_data_queue_.push(data);
}

void MockXRDeviceHookBase::WaitGetCanCreateSession(
    device_test::mojom::XRTestHook::WaitGetCanCreateSessionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(mock_device_sequence_);
  std::move(callback).Run(can_create_session_);
}

void MockXRDeviceHookBase::SetCanCreateSession(bool can_create_session) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(main_sequence_);
  can_create_session_ = can_create_session;
}
