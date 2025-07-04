// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <variant>

#include "base/android/scoped_java_ref.h"
#include "base/android/unguessable_token_android.h"
#include "base/logging.h"
#include "content/common/android/surface_wrapper.h"
#include "content/public/browser/browser_thread.h"
#include "gpu/ipc/common/gpu_surface_tracker.h"
#include "third_party/abseil-cpp/absl/functional/overload.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "content/public/android/content_jni_headers/GpuProcessCallback_jni.h"

namespace content {

base::android::ScopedJavaLocalRef<jobject>
JNI_GpuProcessCallback_GetViewSurface(
    JNIEnv* env,
    jint surface_id) {
  base::android::ScopedJavaLocalRef<jobject> j_surface_wrapper;
  auto surface_record =
      gpu::GpuSurfaceTracker::GetInstance()->AcquireJavaSurface(surface_id);
  std::visit(
      absl::Overload{[&](gl::ScopedJavaSurface&& scoped_java_surface) {
                       if (!scoped_java_surface.IsEmpty()) {
                         if (surface_record.host_input_token) {
                           j_surface_wrapper = JNI_SurfaceWrapper_create(
                               env, scoped_java_surface.j_surface(),
                               surface_record.can_be_used_with_surface_control,
                               surface_record.host_input_token);
                         } else {
                           j_surface_wrapper = JNI_SurfaceWrapper_create(
                               env, scoped_java_surface.j_surface(),
                               surface_record.can_be_used_with_surface_control);
                         }
                       }
                     },
                     [&](gl::ScopedJavaSurfaceControl&& surface_control) {
                       j_surface_wrapper =
                           JNI_SurfaceWrapper_createFromSurfaceControl(
                               env, std::move(surface_control));
                     }},
      std::move(surface_record.surface_variant));
  return j_surface_wrapper;
}

}  // namespace content
