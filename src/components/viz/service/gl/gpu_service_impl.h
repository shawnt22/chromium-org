// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_GL_GPU_SERVICE_IMPL_H_
#define COMPONENTS_VIZ_SERVICE_GL_GPU_SERVICE_IMPL_H_

#include <memory>
#include <string>

#include "base/clang_profiling_buildflags.h"
#include "base/compiler_specific.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/observer_list.h"
#include "base/process/process_handle.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/viz/common/frame_sinks/begin_frame_source.h"
#include "components/viz/service/display_embedder/compositor_gpu_thread.h"
#include "components/viz/service/gl/exit_code.h"
#include "components/viz/service/viz_service_export.h"
#include "gpu/command_buffer/common/shm_count.h"
#include "gpu/command_buffer/service/sequence_id.h"
#include "gpu/config/gpu_info.h"
#include "gpu/config/gpu_preferences.h"
#include "gpu/ipc/common/gpu_disk_cache_type.h"
#include "gpu/ipc/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/common/surface_handle.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"
#include "gpu/ipc/service/gpu_config.h"
#include "gpu/ipc/service/x_util.h"
#include "gpu/vulkan/buildflags.h"
#include "media/media_buildflags.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/shared_remote.h"
#include "services/viz/privileged/mojom/gl/gpu_host.mojom.h"
#include "services/viz/privileged/mojom/gl/gpu_service.mojom.h"
#include "services/viz/privileged/mojom/viz_main.mojom.h"
#include "services/webnn/public/mojom/webnn_context_provider.mojom.h"
#include "skia/buildflags.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "ui/gfx/gpu_extra_info.h"
#include "ui/gfx/native_widget_types.h"

#if BUILDFLAG(IS_WIN)
#include "ui/gl/direct_composition_support.h"
#endif  // BUILDFLAG(IS_WIN)

namespace gpu {
class DawnContextProvider;
class GpuMemoryBufferFactory;
class GpuWatchdogThread;
class ImageDecodeAcceleratorWorker;
class Scheduler;
class SharedContextState;
class SharedImageManager;
class SyncPointManager;
class VulkanImplementation;
}  // namespace gpu

namespace gpu::webgpu {
class DawnCachingInterfaceFactory;
}  // namespace gpu::webgpu

namespace media {
class MediaGpuChannelManager;
}  // namespace media

namespace webnn {
class WebNNContextProviderImpl;
}  // namespace webnn

namespace viz {

class VulkanContextProvider;
class MetalContextProvider;

// This runs in the GPU process, and communicates with the gpu host (which is
// the window server) over the mojom APIs. This is responsible for setting up
// the connection to clients, allocating/free'ing gpu memory etc.
class VIZ_SERVICE_EXPORT GpuServiceImpl
    : public gpu::GpuChannelManagerDelegate,
#if BUILDFLAG(IS_WIN)
      public gl::DirectCompositionOverlayCapsObserver,
#endif
      public mojom::GpuService,
      public BeginFrameObserverBase {
 public:
  struct VIZ_SERVICE_EXPORT InitParams {
    InitParams();
    InitParams(InitParams&& other);
    InitParams& operator=(InitParams&& other);
    ~InitParams();

    std::unique_ptr<gpu::GpuWatchdogThread> watchdog_thread;
    scoped_refptr<base::SingleThreadTaskRunner> io_runner;
    raw_ptr<gpu::VulkanImplementation> vulkan_implementation = nullptr;
#if BUILDFLAG(SKIA_USE_DAWN)
    std::unique_ptr<gpu::DawnContextProvider> dawn_context_provider;
#endif
  };

  GpuServiceImpl(const gpu::GpuPreferences& gpu_preferences,
                 const gpu::GPUInfo& gpu_info,
                 const gpu::GpuFeatureInfo& gpu_feature_info,
                 const std::optional<gpu::GPUInfo>& gpu_info_for_hardware_gpu,
                 const std::optional<gpu::GpuFeatureInfo>&
                     gpu_feature_info_for_hardware_gpu,
                 const gfx::GpuExtraInfo& gpu_extra_info,
                 InitParams init_params);

  GpuServiceImpl(const GpuServiceImpl&) = delete;
  GpuServiceImpl& operator=(const GpuServiceImpl&) = delete;

  ~GpuServiceImpl() override;

  void UpdateGPUInfo();
  void UpdateGPUInfoGL();

#if BUILDFLAG(IS_ANDROID)
  void InitializeWithHost(
      mojo::PendingRemote<mojom::GpuHost> gpu_host,
      gpu::GpuProcessShmCount use_shader_cache_shm_count,
      scoped_refptr<gl::GLSurface> default_offscreen_surface,
      mojom::GpuServiceCreationParamsPtr creation_params,
      gpu::SyncPointManager* sync_point_manager = nullptr,
      gpu::SharedImageManager* shared_image_manager = nullptr,
      gpu::Scheduler* scheduler = nullptr,
      base::WaitableEvent* shutdown_event = nullptr,
      const gpu::SharedContextState::GrContextOptionsProvider*
          gr_context_options_provider = nullptr);
#else
  void InitializeWithHost(
      mojo::PendingRemote<mojom::GpuHost> gpu_host,
      gpu::GpuProcessShmCount use_shader_cache_shm_count,
      scoped_refptr<gl::GLSurface> default_offscreen_surface,
      mojom::GpuServiceCreationParamsPtr creation_params,
      base::WaitableEvent* shutdown_event = nullptr);
#endif

  void Bind(mojo::PendingReceiver<mojom::GpuService> pending_receiver);

  scoped_refptr<gpu::SharedContextState> GetContextState();

  // Notifies the GpuHost to stop using GPU compositing. This should be called
  // in response to an error in the GPU process that occurred after
  // InitializeWithHost() was called, otherwise GpuFeatureInfo should be set
  // accordingly. This can safely be called from any thread.
  void DisableGpuCompositing();

  // Set a closure to be called on each WakeUpGpu on the IO thread.
  void SetWakeUpGpuClosure(base::RepeatingClosure closure);

  // mojom::GpuService:
  void EstablishGpuChannel(int32_t client_id,
                           uint64_t client_tracing_id,
                           bool is_gpu_host,
                           EstablishGpuChannelCallback callback) override;
  void SetChannelClientPid(int32_t client_id,
                           base::ProcessId client_pid) override;
  void SetChannelDiskCacheHandle(
      int32_t client_id,
      const gpu::GpuDiskCacheHandle& handle) override;
  void OnDiskCacheHandleDestoyed(
      const gpu::GpuDiskCacheHandle& handle) override;
  void CloseChannel(int32_t client_id) override;
#if BUILDFLAG(IS_CHROMEOS)
  void CreateJpegDecodeAccelerator(
      mojo::PendingReceiver<chromeos_camera::mojom::MjpegDecodeAccelerator>
          jda_receiver) override;
  void CreateJpegEncodeAccelerator(
      mojo::PendingReceiver<chromeos_camera::mojom::JpegEncodeAccelerator>
          jea_receiver) override;
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN)
  void RegisterDCOMPSurfaceHandle(
      mojo::PlatformHandle surface_handle,
      RegisterDCOMPSurfaceHandleCallback callback) override;
  void UnregisterDCOMPSurfaceHandle(
      const base::UnguessableToken& token) override;
#endif  // BUILDFLAG(IS_WIN)

  void CreateVideoEncodeAcceleratorProvider(
      mojo::PendingReceiver<media::mojom::VideoEncodeAcceleratorProvider>
          vea_provider_receiver) override;

  void BindWebNNContextProvider(
      mojo::PendingReceiver<webnn::mojom::WebNNContextProvider>
          pending_receiver,
      int client_id) override;

  void GetVideoMemoryUsageStats(
      GetVideoMemoryUsageStatsCallback callback) override;
  void AddVideoMemoryUsageStatsOnCompositorGpu(
      GetVideoMemoryUsageStatsCallback callback,
      gpu::VideoMemoryUsageStats video_memory_usage_stats);

  // These methods can be called from the CrBrowserMain thread and the
  // VizCompositorThread (with InputVizard) for PeakGpuMemory tracking.
  void StartPeakMemoryMonitor(uint32_t sequence_num) override;
  void GetPeakMemoryUsage(uint32_t sequence_num,
                          GetPeakMemoryUsageCallback callback) override;
#if BUILDFLAG(IS_WIN)
  void RequestDXGIInfo(RequestDXGIInfoCallback callback) override;
#endif
  void LoadedBlob(const gpu::GpuDiskCacheHandle& handle,
                  const std::string& key,
                  const std::string& data) override;
  void WakeUpGpu() override;
  void GpuSwitched(gl::GpuPreference active_gpu_heuristic) override;
  void DisplayAdded() override;
  void DisplayRemoved() override;
  void DisplayMetricsChanged() override;
  void DestroyAllChannels() override;
  void OnBackgroundCleanup() override;
  void OnBackgrounded() override;
  void OnForegrounded() override;
#if !BUILDFLAG(IS_ANDROID)
  void OnMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel level) override;
#endif
#if BUILDFLAG(IS_APPLE)
  void BeginCATransaction() override;
  void CommitCATransaction(CommitCATransactionCallback callback) override;
#endif
#if BUILDFLAG(CLANG_PROFILING_INSIDE_SANDBOX)
  void WriteClangProfilingProfile(
      WriteClangProfilingProfileCallback callback) override;
#endif
  void Crash() override;
  void Hang() override;
  void ThrowJavaException() override;

  // gpu::GpuChannelManagerDelegate:
  void LoseAllContexts() override;
  void DidCreateContextSuccessfully() override;
  void DidCreateOffscreenContext(const GURL& active_url) override;
  void DidDestroyChannel(int client_id) override;
  void DidDestroyAllChannels() override;
  void DidDestroyOffscreenContext(const GURL& active_url) override;
  void DidLoseContext(gpu::error::ContextLostReason reason,
                      const GURL& active_url) override;
  void GetDawnInfo(bool collect_metrics, GetDawnInfoCallback callback) override;

  void GetIsolationKey(int client_id,
                       const blink::WebGPUExecutionContextToken& token,
                       GetIsolationKeyCallback cb) override;
  void StoreBlobToDisk(const gpu::GpuDiskCacheHandle& handle,
                       const std::string& key,
                       const std::string& shader) override;

  // Attempts to atomically shut down the process. Only does so if not running
  // in host process. An error message will be logged.
  void MaybeExitOnContextLost(
      gpu::error::ContextLostReason context_lost_reason) override;
  bool IsExiting() const override;
  gpu::Scheduler* GetGpuScheduler() override;

  // BeginFrameObserverBase implementation, which called from
  // VizCompositorThread.
  bool OnBeginFrameDerivedImpl(const BeginFrameArgs& args) override;
  void OnBeginFrameSourcePausedChanged(bool paused) override;

  using RequestBeginFrameForGpuServiceCB =
      base::RepeatingCallback<void(bool toggle)>;
  void SetRequestBeginFrameForGpuServiceCB(RequestBeginFrameForGpuServiceCB cb);
  void SetMjpegDecodeAcceleratorBeginFrameCB(
      std::optional<base::RepeatingClosure> cb);

#if BUILDFLAG(IS_WIN)
  // DirectCompositionOverlayCapsObserver implementation.
  // Update overlay info and HDR status on the GPU process and send the updated
  // info back to the browser process if there is a change.
  void OnOverlayCapsChanged() override;
#endif

  bool is_initialized() const { return !!gpu_host_; }

  media::MediaGpuChannelManager* media_gpu_channel_manager() {
    return media_gpu_channel_manager_.get();
  }

  gpu::GpuChannelManager* gpu_channel_manager() {
    return gpu_channel_manager_.get();
  }

  CompositorGpuThread* compositor_gpu_thread() {
    return compositor_gpu_thread_.get();
  }

  gpu::GpuMemoryBufferFactory* gpu_memory_buffer_factory() {
    return gpu_memory_buffer_factory_.get();
  }

  gpu::SharedImageManager* shared_image_manager() {
    return gpu_channel_manager_->shared_image_manager();
  }

  gl::GLShareGroup* share_group() {
    return gpu_channel_manager_->share_group();
  }

  gpu::raster::GrShaderCache* gr_shader_cache() {
    return gpu_channel_manager_->gr_shader_cache();
  }

  gpu::SyncPointManager* sync_point_manager() {
    return gpu_channel_manager_->sync_point_manager();
  }

  gpu::Scheduler* gpu_scheduler() { return gpu_channel_manager_->scheduler(); }

  scoped_refptr<base::SingleThreadTaskRunner>& main_runner() {
    return main_runner_;
  }

  scoped_refptr<base::SingleThreadTaskRunner> compositor_gpu_task_runner() {
    return compositor_gpu_thread() ? compositor_gpu_thread()->task_runner()
                                   : main_runner_;
  }

  gpu::GpuWatchdogThread* watchdog_thread() { return watchdog_thread_.get(); }

  const gpu::GpuFeatureInfo& gpu_feature_info() const {
    return gpu_feature_info_;
  }

  const gpu::GpuDriverBugWorkarounds& gpu_driver_bug_workarounds() const {
    return gpu_driver_bug_workarounds_;
  }

  bool in_host_process() const { return gpu_info_.in_process_gpu; }

  void set_start_time(base::TimeTicks start_time) { start_time_ = start_time; }

  const gpu::GPUInfo& gpu_info() const { return gpu_info_; }
  const gpu::GpuPreferences& gpu_preferences() const {
    return gpu_preferences_;
  }

#if BUILDFLAG(ENABLE_VULKAN)
  VulkanContextProvider* vulkan_context_provider() const {
    return vulkan_context_provider_.get();
  }
#else
  VulkanContextProvider* vulkan_context_provider() const { return nullptr; }
#endif

#if BUILDFLAG(SKIA_USE_METAL)
  MetalContextProvider* metal_context_provider() const {
    return metal_context_provider_.get();
  }
#else
  MetalContextProvider* metal_context_provider() const { return nullptr; }
#endif

#if BUILDFLAG(SKIA_USE_DAWN)
  gpu::DawnContextProvider* dawn_context_provider() const {
    return dawn_context_provider_.get();
  }
#else
  gpu::DawnContextProvider* dawn_context_provider() const { return nullptr; }
#endif

  base::ProcessId host_process_id() const { return host_process_id_; }

#if BUILDFLAG(IS_ANDROID)
  void SetHostProcessId(base::ProcessId pid);
#endif

  using VisibilityChangedCallback =
      base::RepeatingCallback<void(bool /*visible*/)>;
  void SetVisibilityChangedCallback(VisibilityChangedCallback);

 private:
  void InitializeWithHostInternal(
      mojo::PendingRemote<mojom::GpuHost> gpu_host,
      gpu::GpuProcessShmCount use_shader_cache_shm_count,
      scoped_refptr<gl::GLSurface> default_offscreen_surface,
      mojom::GpuServiceCreationParamsPtr creation_params,
      gpu::SyncPointManager* sync_point_manager,
      gpu::SharedImageManager* shared_image_manager,
      gpu::Scheduler* scheduler,
      base::WaitableEvent* shutdown_event);

  friend class MockGpuServiceImpl;
  GpuServiceImpl();

  // Private helper methods to create objects needed by this class during init.
  gpu::SyncPointManager* CreateSyncPointManager();
  // supports_overlays is only queried when using Ozone.
  // It should not be supplied otherwise.
  gpu::SharedImageManager* CreateSharedImageManager(
      bool supports_overlays = false);
  gpu::Scheduler* CreateScheduler(gpu::SyncPointManager* sync_point_manager);
  base::WaitableEvent* CreateShutdownEvent();

  bool IsNativeBufferSupported(gfx::BufferFormat format,
                               gfx::BufferUsage usage);

#if BUILDFLAG(IS_WIN)
  void RequestDXGIInfoOnMainThread(RequestDXGIInfoCallback callback);
#endif

  void OnBackgroundedOnMainThread();
  void OnForegroundedOnMainThread();

  void OnBackgroundCleanupGpuMainThread();
  void OnBackgroundCleanupCompositorGpuThread();

  // Ensure that all peak memory tracking occurs on the main thread as all
  // MemoryTracker are created on that thread. All requests made before
  // GpuServiceImpl::InitializeWithHost will be enqueued.
  void StartPeakMemoryMonitorOnMainThread(uint32_t sequence_num);
  virtual void GetPeakMemoryUsageOnMainThread(
      uint32_t sequence_num,
      GetPeakMemoryUsageCallback callback);  // virtual for testing.

  void WakeUpGpuOnMainThread();

  // Update overlay info and HDR status on the GPU process and send the updated
  // info back to the browser process if there is a change.
#if BUILDFLAG(IS_WIN)
  void UpdateOverlayAndDXGIInfo();
#endif

  void GetDawnInfoOnMain(bool collect_metrics, GetDawnInfoCallback callback);

  std::string GetShaderPrefixKey();

  gpu::webgpu::DawnCachingInterfaceFactory* dawn_caching_interface_factory() {
#if BUILDFLAG(USE_DAWN) || BUILDFLAG(SKIA_USE_DAWN)
    return dawn_caching_interface_factory_.get();
#else
    return nullptr;
#endif
  }

  void OnBeginFrameOnIO(const BeginFrameArgs& args);

#if BUILDFLAG(IS_LINUX)
  bool IsGMBNV12Supported();
#endif

  scoped_refptr<base::SingleThreadTaskRunner> main_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_runner_;

#if BUILDFLAG(IS_FUCHSIA)
  // TODO(crbug.com/40850116): Fuchsia does not support FIDL communication from
  // ThreadPool's worker threads.
  std::unique_ptr<base::Thread> vea_thread_;
#endif

  // Do not change the class member order here. watchdog_thread_ should be the
  // last one to be destroyed before main_runner_ and io_runner_.
  std::unique_ptr<gpu::GpuWatchdogThread> watchdog_thread_;

  const gpu::GpuPreferences gpu_preferences_;

  // Information about the GPU, such as device and vendor ID.
  gpu::GPUInfo gpu_info_;

  // Information about general chrome feature support for the GPU.
  const gpu::GpuFeatureInfo gpu_feature_info_;

  const gpu::GpuDriverBugWorkarounds gpu_driver_bug_workarounds_;

#if BUILDFLAG(IS_WIN)
  gfx::mojom::DXGIInfoPtr dxgi_info_;
#endif

  // What we would have gotten if we haven't fallen back to SwiftShader or
  // pure software (in the viz case).
  std::optional<gpu::GPUInfo> gpu_info_for_hardware_gpu_;
  std::optional<gpu::GpuFeatureInfo> gpu_feature_info_for_hardware_gpu_;

  // Information about the GPU process populated on creation.
  gfx::GpuExtraInfo gpu_extra_info_;

  mojo::SharedRemote<mojom::GpuHost> gpu_host_;
  std::unique_ptr<gpu::GpuChannelManager> gpu_channel_manager_;
  std::unique_ptr<media::MediaGpuChannelManager> media_gpu_channel_manager_;

  // Display compositor gpu thread.
  std::unique_ptr<CompositorGpuThread> compositor_gpu_thread_;

  // Toggle gpu service on begin frame source which is used in main thread.
  RequestBeginFrameForGpuServiceCB request_begin_frame_for_gpu_service_cb_;
  // Used in GPU IO thread.
  std::optional<base::RepeatingClosure>
      mjpeg_decode_accelerator_begin_frame_cb_;

  // On some platforms (e.g. android webview), SyncPointManager,
  // SharedImageManager and Scheduler come from external sources.
  std::unique_ptr<gpu::SyncPointManager> owned_sync_point_manager_;

  std::unique_ptr<gpu::SharedImageManager> owned_shared_image_manager_;

  std::unique_ptr<gpu::Scheduler> owned_scheduler_;
  raw_ptr<gpu::Scheduler, DanglingUntriaged> scheduler_;

#if BUILDFLAG(USE_DAWN) || BUILDFLAG(SKIA_USE_DAWN)
  std::unique_ptr<gpu::webgpu::DawnCachingInterfaceFactory>
      dawn_caching_interface_factory_;
#endif

#if BUILDFLAG(ENABLE_VULKAN)
  raw_ptr<gpu::VulkanImplementation> vulkan_implementation_;
  scoped_refptr<VulkanContextProvider> vulkan_context_provider_;
#endif
#if BUILDFLAG(SKIA_USE_METAL)
  std::unique_ptr<MetalContextProvider> metal_context_provider_;
#endif
#if BUILDFLAG(SKIA_USE_DAWN)
  std::unique_ptr<gpu::DawnContextProvider> dawn_context_provider_;
#endif

  std::unique_ptr<webnn::WebNNContextProviderImpl> webnn_context_provider_;

  std::unique_ptr<gpu::GpuMemoryBufferFactory> gpu_memory_buffer_factory_;

  // An event that will be signalled when we shutdown. On some platforms it
  // comes from external sources.
  std::unique_ptr<base::WaitableEvent> owned_shutdown_event_;
  raw_ptr<base::WaitableEvent> shutdown_event_ = nullptr;

  raw_ptr<const gpu::SharedContextState::GrContextOptionsProvider>
      gr_context_options_provider_ = nullptr;

  base::AtomicFlag is_exiting_;

  // Used for performing hardware decode acceleration of images. This is shared
  // by all the GPU channels.
  std::unique_ptr<gpu::ImageDecodeAcceleratorWorker>
      image_decode_accelerator_worker_;

  base::TimeTicks start_time_;

  // Used to track the task to bind |receiver_| on the IO thread.
  base::CancelableTaskTracker bind_task_tracker_;
  // Should only be accessed on the IO thread after creation.
  mojo::Receiver<mojom::GpuService> receiver_{this};

  gpu::GpuMemoryBufferConfigurationSet supported_gmb_configurations_;
  bool supported_gmb_configurations_inited_ = false;

  VisibilityChangedCallback visibility_changed_callback_;

  base::ProcessId host_process_id_ = base::kNullProcessId;

  base::RepeatingClosure wake_up_closure_;

  std::string shader_prefix_key_;

  // This is flag is controlled by the finch experiment
  // ClearGrShaderDiskCacheOnInvalidPrefix. Earlier this flag was assigned in
  // ::LoadedBlob() instead of the constructor which was causing users to fall
  // out of the finch experiment as ::LoadedBlob() is not called in the next
  // browser start after the disk cache is cleared.
  const bool clear_shader_cache_;

  base::WeakPtr<GpuServiceImpl> weak_ptr_;
  base::WeakPtrFactory<GpuServiceImpl> weak_ptr_factory_{this};
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_GL_GPU_SERVICE_IMPL_H_
