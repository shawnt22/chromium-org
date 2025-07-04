// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_memory_buffer_factory_native_pixmap.h"

#include "base/logging.h"
#include "build/build_config.h"
#include "components/viz/common/gpu/vulkan_context_provider.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/buffer_usage_util.h"
#include "ui/gfx/client_native_pixmap.h"
#include "ui/gfx/linux/native_pixmap_dmabuf.h"
#include "ui/gfx/native_pixmap.h"
#include "ui/gl/gl_implementation.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/surface_factory_ozone.h"

#if BUILDFLAG(ENABLE_VULKAN)
#include "gpu/vulkan/vulkan_device_queue.h"
#endif

namespace gpu {

GpuMemoryBufferFactoryNativePixmap::GpuMemoryBufferFactoryNativePixmap()
    : GpuMemoryBufferFactoryNativePixmap(nullptr) {}

GpuMemoryBufferFactoryNativePixmap::GpuMemoryBufferFactoryNativePixmap(
    viz::VulkanContextProvider* vulkan_context_provider)
    : vulkan_context_provider_(vulkan_context_provider) {}

GpuMemoryBufferFactoryNativePixmap::~GpuMemoryBufferFactoryNativePixmap() =
    default;

gfx::GpuMemoryBufferHandle
GpuMemoryBufferFactoryNativePixmap::CreateGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    const gfx::Size& size,
    const gfx::Size& framebuffer_size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    int client_id,
    SurfaceHandle surface_handle) {
  scoped_refptr<gfx::NativePixmap> pixmap =
      ui::OzonePlatform::GetInstance()
          ->GetSurfaceFactoryOzone()
          ->CreateNativePixmap(surface_handle, GetVulkanDeviceQueue(), size,
                               format, usage, framebuffer_size);
  return CreateGpuMemoryBufferFromNativePixmap(id, size, format, usage,
                                               client_id, std::move(pixmap));
}

void GpuMemoryBufferFactoryNativePixmap::DestroyGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    int client_id) {
  base::AutoLock lock(native_pixmaps_lock_);
  NativePixmapMapKey key(id.id, client_id);
  native_pixmaps_.erase(key);
}

bool GpuMemoryBufferFactoryNativePixmap::
    FillSharedMemoryRegionWithBufferContents(
        gfx::GpuMemoryBufferHandle buffer_handle,
        base::UnsafeSharedMemoryRegion shared_memory) {
  return false;
}

VulkanDeviceQueue* GpuMemoryBufferFactoryNativePixmap::GetVulkanDeviceQueue() {
#if BUILDFLAG(ENABLE_VULKAN)
  if (vulkan_context_provider_)
    return vulkan_context_provider_->GetDeviceQueue();
#endif

  return nullptr;
}

gfx::GpuMemoryBufferHandle
GpuMemoryBufferFactoryNativePixmap::CreateGpuMemoryBufferFromNativePixmap(
    gfx::GpuMemoryBufferId id,
    const gfx::Size& size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    int client_id,
    scoped_refptr<gfx::NativePixmap> pixmap) {
  if (!pixmap.get()) {
    DLOG(ERROR) << "Failed to create pixmap " << size.ToString() << ",  "
                << gfx::BufferFormatToString(format) << ", usage "
                << gfx::BufferUsageToString(usage);
    return gfx::GpuMemoryBufferHandle();
  }

  gfx::NativePixmapHandle native_pixmap_handle = pixmap->ExportHandle();
  if (native_pixmap_handle.planes.empty()) {
    return gfx::GpuMemoryBufferHandle();
  }

  gfx::GpuMemoryBufferHandle new_handle(std::move(native_pixmap_handle));
  new_handle.id = id;

  // TODO(reveman): Remove this once crbug.com/628334 has been fixed.
  {
    base::AutoLock lock(native_pixmaps_lock_);
    NativePixmapMapKey key(id.id, client_id);
    DCHECK(native_pixmaps_.find(key) == native_pixmaps_.end());
    native_pixmaps_[key] = pixmap;
  }

  return new_handle;
}

}  // namespace gpu
