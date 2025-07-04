// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_CLIENT_CLIENT_SHARED_IMAGE_INTERFACE_H_
#define GPU_IPC_CLIENT_CLIENT_SHARED_IMAGE_INTERFACE_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/unsafe_shared_memory_pool.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "build/build_config.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/ipc/client/gpu_ipc_client_export.h"
#include "gpu/ipc/common/surface_handle.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_map.h"

namespace gpu {
class SharedImageInterfaceProxy;
class GpuChannelHost;

// Tracks shared images created by a single context and ensures they are deleted
// if the context is lost.
class GPU_IPC_CLIENT_EXPORT ClientSharedImageInterface
    : public SharedImageInterface {
 public:
  ClientSharedImageInterface(SharedImageInterfaceProxy* proxy,
                             scoped_refptr<gpu::GpuChannelHost> channel);

  // SharedImageInterface implementation.
  void UpdateSharedImage(const SyncToken& sync_token,
                         const Mailbox& mailbox) override;
  void UpdateSharedImage(const SyncToken& sync_token,
                         std::unique_ptr<gfx::GpuFence> acquire_fence,
                         const Mailbox& mailbox) override;
  void PresentSwapChain(const SyncToken& sync_token,
                        const Mailbox& mailbox) override;
#if BUILDFLAG(IS_FUCHSIA)
  void RegisterSysmemBufferCollection(zx::eventpair service_handle,
                                      zx::channel sysmem_token,
                                      const viz::SharedImageFormat& format,
                                      gfx::BufferUsage usage,
                                      bool register_with_image_pipe) override;
#endif  // BUILDFLAG(IS_FUCHSIA)
  SyncToken GenUnverifiedSyncToken() override;
  SyncToken GenVerifiedSyncToken() override;
  void VerifySyncToken(SyncToken& sync_token) override;
  void WaitSyncToken(const gpu::SyncToken& sync_token) override;
  scoped_refptr<ClientSharedImage> CreateSharedImage(
      const SharedImageInfo& si_info,
      gpu::SurfaceHandle surface_handle,
      std::optional<SharedImagePoolId> pool_id = std::nullopt) override;
  scoped_refptr<ClientSharedImage> CreateSharedImage(
      const SharedImageInfo& si_info,
      base::span<const uint8_t> pixel_data) override;
  scoped_refptr<ClientSharedImage> CreateSharedImage(
      const SharedImageInfo& si_info,
      gpu::SurfaceHandle surface_handle,
      gfx::BufferUsage buffer_usage,
      std::optional<SharedImagePoolId> pool_id = std::nullopt) override;
  scoped_refptr<ClientSharedImage> CreateSharedImage(
      const SharedImageInfo& si_info,
      gpu::SurfaceHandle surface_handle,
      gfx::BufferUsage buffer_usage,
      gfx::GpuMemoryBufferHandle buffer_handle) override;
  scoped_refptr<ClientSharedImage> CreateSharedImage(
      const SharedImageInfo& si_info,
      gfx::GpuMemoryBufferHandle buffer_handle) override;

  scoped_refptr<ClientSharedImage> CreateSharedImageForMLTensor(
      std::string debug_label,
      viz::SharedImageFormat format,
      const gfx::Size& size,
      gpu::SharedImageUsageSet usage) override;

  // Used by the software compositor only. |usage| must be
  // gpu::SHARED_IMAGE_USAGE_CPU_WRITE_ONLY. Call client_shared_image->Map()
  // later to get the shared memory mapping.
  scoped_refptr<ClientSharedImage> CreateSharedImageForSoftwareCompositor(
      const SharedImageInfo& si_info) override;
  void CopyToGpuMemoryBuffer(const SyncToken& sync_token,
                             const Mailbox& mailbox) override;
#if BUILDFLAG(IS_WIN)
  void CopyToGpuMemoryBufferAsync(
      const SyncToken& sync_token,
      const Mailbox& mailbox,
      base::OnceCallback<void(bool)> callback) override;
  void UpdateSharedImage(const SyncToken& sync_token,
                         scoped_refptr<gfx::D3DSharedFence> d3d_shared_fence,
                         const Mailbox& mailbox) override;
  void CopyNativeGmbToSharedMemoryAsync(
      gfx::GpuMemoryBufferHandle buffer_handle,
      base::UnsafeSharedMemoryRegion memory_region,
      base::OnceCallback<void(bool)> callback) override;
#endif  // BUILDFLAG(IS_WIN)

  SwapChainSharedImages CreateSwapChain(viz::SharedImageFormat format,
                                        const gfx::Size& size,
                                        const gfx::ColorSpace& color_space,
                                        GrSurfaceOrigin surface_origin,
                                        SkAlphaType alpha_type,
                                        SharedImageUsageSet usage,
                                        std::string_view debug_label) override;
  void DestroySharedImage(const SyncToken& sync_token,
                          const Mailbox& mailbox) override;
  void DestroySharedImage(
      const SyncToken& sync_token,
      scoped_refptr<ClientSharedImage> client_shared_image) override;
  scoped_refptr<ClientSharedImage> NotifyMailboxAdded(
      const Mailbox& mailbox,
      viz::SharedImageFormat format,
      const gfx::Size& size,
      const gfx::ColorSpace& color_space,
      GrSurfaceOrigin surface_origin,
      SkAlphaType alpha_type,
      SharedImageUsageSet usage,
      uint32_t texture_target,
      std::string_view debug_label) override;

  scoped_refptr<ClientSharedImage> ImportSharedImage(
      ExportedSharedImage exported_shared_image) override;

  const SharedImageCapabilities& GetCapabilities() override;

  void CreateSharedImagePool(
      const SharedImagePoolId& pool_id,
      mojo::PendingRemote<mojom::SharedImagePoolClientInterface> client_remote)
      override;
  void DestroySharedImagePool(const SharedImagePoolId& pool_id) override;

  gpu::GpuChannelHost* gpu_channel() { return gpu_channel_.get(); }

 protected:
  ~ClientSharedImageInterface() override;

 private:
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_;

  Mailbox AddMailbox(const Mailbox& mailbox);

  const raw_ptr<SharedImageInterfaceProxy> proxy_;

  base::Lock lock_;
  // Map of mailbox to ref counts to match required DestroySharedImage calls.
  absl::flat_hash_map<Mailbox, int> mailboxes_ GUARDED_BY(lock_);

  // Used by ClientSharedImage while creating a GpuMemoryBuffer internally for
  // MappableSI. This pool is used on windows only. It's needed to allocate
  // temporary shared memory to transfer pixels from the gpu process to the
  // renderer, because we can't map DXGI buffers in renderer. This will be null
  // on other platforms.
  const scoped_refptr<base::UnsafeSharedMemoryPool> shared_memory_pool_;
};

}  // namespace gpu

#endif  // GPU_IPC_CLIENT_CLIENT_SHARED_IMAGE_INTERFACE_H_
