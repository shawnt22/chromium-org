// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_MOJOM_NATIVE_HANDLE_TYPES_MOJOM_TRAITS_H_
#define UI_GFX_MOJOM_NATIVE_HANDLE_TYPES_MOJOM_TRAITS_H_

#include <optional>

#include "base/component_export.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/numerics/safe_conversions.h"
#include "build/build_config.h"
#include "mojo/public/cpp/base/unguessable_token_mojom_traits.h"
#include "mojo/public/cpp/bindings/struct_traits.h"
#include "mojo/public/cpp/bindings/union_traits.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "ui/gfx/gpu_memory_buffer_handle.h"
#include "ui/gfx/mojom/native_handle_types.mojom-shared.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
#include "ui/gfx/native_pixmap_handle.h"
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)

#if BUILDFLAG(IS_ANDROID)
#include "base/android/scoped_hardware_buffer_handle.h"
#include "mojo/public/cpp/platform/platform_handle.h"
#include "mojo/public/cpp/system/message_pipe.h"
#endif  // BUILDFLAG(IS_ANDROID)

namespace mojo {

#if BUILDFLAG(IS_ANDROID)
template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    StructTraits<gfx::mojom::AHardwareBufferHandleDataView,
                 ::base::android::ScopedHardwareBufferHandle> {
  static PlatformHandle buffer_handle(
      ::base::android::ScopedHardwareBufferHandle& handle);
  static ScopedMessagePipeHandle tracking_pipe(
      ::base::android::ScopedHardwareBufferHandle& handle);

  static bool Read(gfx::mojom::AHardwareBufferHandleDataView data,
                   ::base::android::ScopedHardwareBufferHandle* handle);
};
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    StructTraits<gfx::mojom::NativePixmapPlaneDataView,
                 gfx::NativePixmapPlane> {
  static uint32_t stride(const gfx::NativePixmapPlane& plane) {
    return plane.stride;
  }
  static int32_t offset(const gfx::NativePixmapPlane& plane) {
    return base::saturated_cast<int32_t>(plane.offset);
  }
  static uint64_t size(const gfx::NativePixmapPlane& plane) {
    return plane.size;
  }
  static mojo::PlatformHandle buffer_handle(gfx::NativePixmapPlane& plane);
  static bool Read(gfx::mojom::NativePixmapPlaneDataView data,
                   gfx::NativePixmapPlane* out);
};

template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    StructTraits<gfx::mojom::NativePixmapHandleDataView,
                 gfx::NativePixmapHandle> {
  static std::vector<gfx::NativePixmapPlane>& planes(
      gfx::NativePixmapHandle& pixmap_handle) {
    return pixmap_handle.planes;
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  static uint64_t modifier(const gfx::NativePixmapHandle& pixmap_handle) {
    return pixmap_handle.modifier;
  }
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  static bool supports_zero_copy_webgpu_import(
      const gfx::NativePixmapHandle& pixmap_handle) {
    return pixmap_handle.supports_zero_copy_webgpu_import;
  }
#endif

#if BUILDFLAG(IS_FUCHSIA)
  static PlatformHandle buffer_collection_handle(
      gfx::NativePixmapHandle& pixmap_handle);

  static uint32_t buffer_index(gfx::NativePixmapHandle& pixmap_handle) {
    return pixmap_handle.buffer_index;
  }

  static bool ram_coherency(gfx::NativePixmapHandle& pixmap_handle) {
    return pixmap_handle.ram_coherency;
  }
#endif  // BUILDFLAG(IS_FUCHSIA)

  static bool Read(gfx::mojom::NativePixmapHandleDataView data,
                   gfx::NativePixmapHandle* out);
};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)

#if BUILDFLAG(IS_WIN)
template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    StructTraits<gfx::mojom::DXGIHandleDataView, gfx::DXGIHandle> {
  static PlatformHandle buffer_handle(gfx::DXGIHandle& handle) {
    return PlatformHandle(handle.TakeBufferHandle());
  }

  static const gfx::DXGIHandleToken& token(const gfx::DXGIHandle& handle) {
    return handle.token();
  }

  static base::UnsafeSharedMemoryRegion& shared_memory_handle(
      gfx::DXGIHandle& handle) {
    return handle.region_;
  }

  static bool Read(gfx::mojom::DXGIHandleDataView data,
                   gfx::DXGIHandle* handle);
};

template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    StructTraits<gfx::mojom::DXGIHandleTokenDataView, gfx::DXGIHandleToken> {
  static const base::UnguessableToken& value(
      const gfx::DXGIHandleToken& input) {
    return input.value();
  }

  static bool Read(gfx::mojom::DXGIHandleTokenDataView& input,
                   gfx::DXGIHandleToken* output);
};
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_APPLE)
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    IOSurfaceHandle {
  IOSurfaceHandle();
  IOSurfaceHandle(IOSurfaceHandle&&);
  IOSurfaceHandle& operator=(IOSurfaceHandle&&);
  ~IOSurfaceHandle();

  base::apple::ScopedMachSendRight mach_send_right;
#if BUILDFLAG(IS_IOS)
  static constexpr size_t kMaxPlanes = 3;
  base::UnsafeSharedMemoryRegion shared_memory_region;
  std::array<uint32_t, kMaxPlanes> plane_strides;
  std::array<uint32_t, kMaxPlanes> plane_offsets;
#endif
};

template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    StructTraits<gfx::mojom::IOSurfaceHandleDataView, IOSurfaceHandle> {
  static PlatformHandle mach_send_right(IOSurfaceHandle& handle) {
    return PlatformHandle(std::move(handle.mach_send_right));
  }

#if BUILDFLAG(IS_IOS)
  static base::UnsafeSharedMemoryRegion& shared_memory_handle(
      IOSurfaceHandle& handle) {
    return handle.shared_memory_region;
  }

  static std::array<uint32_t, IOSurfaceHandle::kMaxPlanes>& plane_strides(
      IOSurfaceHandle& handle) {
    return handle.plane_strides;
  }

  static std::array<uint32_t, IOSurfaceHandle::kMaxPlanes>& plane_offsets(
      IOSurfaceHandle& handle) {
    return handle.plane_offsets;
  }
#endif  // BUILDFLAG(IS_IOS)

  static bool Read(gfx::mojom::IOSurfaceHandleDataView data,
                   IOSurfaceHandle* handle);
};
#endif  // BUILDFLAG(IS_APPLE)

template <>
struct COMPONENT_EXPORT(GFX_NATIVE_HANDLE_TYPES_SHARED_MOJOM_TRAITS)
    UnionTraits<gfx::mojom::GpuMemoryBufferPlatformHandleDataView,
                gfx::GpuMemoryBufferHandle> {
  using Tag = gfx::mojom::GpuMemoryBufferPlatformHandleDataView::Tag;

  static Tag GetTag(const gfx::GpuMemoryBufferHandle& handle);

  static bool IsNull(const gfx::GpuMemoryBufferHandle& handle);
  static void SetToNull(gfx::GpuMemoryBufferHandle* handle);

  static base::UnsafeSharedMemoryRegion& shared_memory_handle(
      gfx::GpuMemoryBufferHandle& handle) {
    return handle.region_;
  }

#if BUILDFLAG(IS_APPLE)
  static IOSurfaceHandle io_surface_handle(gfx::GpuMemoryBufferHandle& handle);
#endif  // BUILDFLAG(IS_APPLE)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
  static gfx::NativePixmapHandle& native_pixmap_handle(
      gfx::GpuMemoryBufferHandle& handle) {
    return handle.native_pixmap_handle_;
  }
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)

#if BUILDFLAG(IS_WIN)
  static gfx::DXGIHandle& dxgi_handle(gfx::GpuMemoryBufferHandle& handle) {
    return handle.dxgi_handle_;
  }
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_ANDROID)
  static base::android::ScopedHardwareBufferHandle&
  android_hardware_buffer_handle(gfx::GpuMemoryBufferHandle& handle) {
    return handle.android_hardware_buffer;
  }
#endif  // BUILDFLAG(IS_ANDROID)

  static bool Read(gfx::mojom::GpuMemoryBufferPlatformHandleDataView data,
                   gfx::GpuMemoryBufferHandle* handle);
};

}  // namespace mojo

#endif  // UI_GFX_MOJOM_NATIVE_HANDLE_TYPES_MOJOM_TRAITS_H_
