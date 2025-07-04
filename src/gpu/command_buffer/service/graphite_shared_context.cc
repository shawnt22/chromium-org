// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/graphite_shared_context.h"

#include "base/memory/ptr_util.h"
#include "base/task/single_thread_task_runner.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/gpu/graphite/Context.h"
#include "third_party/skia/include/gpu/graphite/PrecompileContext.h"

namespace gpu {

namespace {
struct RecordingContext {
  skgpu::graphite::GpuFinishedProc old_finished_proc;
  skgpu::graphite::GpuFinishedContext old_context;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner;
};

struct AsyncReadContext {
  GraphiteSharedContext::SkImageReadPixelsCallback old_callback;
  SkImage::ReadPixelsContext old_context;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner;
};

void* CreateAsyncReadContextThreadSafe(
    GraphiteSharedContext::SkImageReadPixelsCallback old_callback,
    SkImage::ReadPixelsContext old_callbackContext,
    bool is_thread_safe) {
  scoped_refptr<base::SingleThreadTaskRunner> task_runner =
      is_thread_safe && base::SingleThreadTaskRunner::HasCurrentDefault()
          ? base::SingleThreadTaskRunner::GetCurrentDefault()
          : nullptr;

  // Wrapped the old callback with a new thread safe callback.
  return new AsyncReadContext(std::move(old_callback), old_callbackContext,
                              std::move(task_runner));
}

static void ReadPixelsCallbackThreadSafe(
    void* ctx,
    std::unique_ptr<const SkSurface::AsyncReadResult> async_result) {
  auto context = base::WrapUnique(static_cast<AsyncReadContext*>(ctx));
  if (!context->old_callback) {
    return;
  }

  // Ensure callbacks are called on the original thread if only one
  // graphite::Context is created and is shared by multiple threads.
  base::SingleThreadTaskRunner* task_runner = context->task_runner.get();
  if (task_runner && !task_runner->BelongsToCurrentThread()) {
    task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(context->old_callback), context->old_context,
                       std::move(async_result)));
    return;
  }

  std::move(context->old_callback)
      .Run(context->old_context, std::move(async_result));
}

}  // namespace

// Helper class used by subclasses to acquire |lock_| if it exists.
// Recursive lock is permitted for locking will be skipped upon reentance.
class SCOPED_LOCKABLE GraphiteSharedContext::AutoLock {
  STACK_ALLOCATED();
 public:
  explicit AutoLock(const GraphiteSharedContext* context)
      EXCLUSIVE_LOCK_FUNCTION(context->lock_);

  AutoLock(AutoLock&) = delete;
  AutoLock& operator=(AutoLock&) = delete;

  ~AutoLock() UNLOCK_FUNCTION();

 private:
  std::optional<base::AutoLockMaybe> auto_lock_;
  const GraphiteSharedContext* context_;
};

// |context->locked_thread_id_| reflects the thread where |lock_| is acquired.
// It should only be changed from Invalid to Current, or Current to Invalid.
// It is accessed with `memory_order_relaxed` as writing to |locked_thread_id_|
// is guarded by |context->lock_|.
//
// The logic of detecting recursive lock with
// "current_thread_id == locked_thread_id_":
// - There is no concurrent write hazard for |locked_thread_id|.
// - If Thread1 holds |lock_| and it tries to re-acquire |lock_| after
//   re-entering GraphiteSharedContext, |locked_thread_id_| can be read back
//   safely now as no one can write to |locked_thread_id_| when |lock_| is held
//   by Thread1. It is determined a recursive lock if the current thread id is
//   |locked_thread_id_|. Locking should be skipped here to avoid a deadlock.
// - If Thread1 has held |lock_| and is writing to |locked_thread_id_| while
//   Thread2 is trying to read it, it means Thread1 changes the id between
//   kInvalidThreadId and Thread1::Id() so neither of them matches the current
//   Thread2 id. Thread2 can proceed to acquire the lock.
//

GraphiteSharedContext::AutoLock::AutoLock(const GraphiteSharedContext* context)
    : context_(context) {
  base::PlatformThreadId current_thread_id = base::PlatformThread::CurrentId();

  if (!context->lock_ || current_thread_id == context->locked_thread_id_.load(
                                                  std::memory_order_relaxed)) {
    // Skip if is_thread_safe is disabled or it's a recursive lock.
  } else {
    auto_lock_.emplace(&context->lock_.value());

    // |locked_thread_id_| must be kInvalid after the lock is acquired.
    CHECK_EQ(context_->locked_thread_id_.load(std::memory_order_relaxed),
             base::kInvalidThreadId);
    context_->locked_thread_id_.store(current_thread_id,
                                      std::memory_order_relaxed);
  }
}

GraphiteSharedContext::AutoLock::~AutoLock() {
  if (auto_lock_.has_value()) {
    CHECK_EQ(context_->locked_thread_id_.load(std::memory_order_relaxed),
             base::PlatformThread::CurrentId());
    context_->locked_thread_id_.store(base::kInvalidThreadId,
                                      std::memory_order_relaxed);
  }
}

GraphiteSharedContext::GraphiteSharedContext(
    std::unique_ptr<skgpu::graphite::Context> graphite_context,
    bool is_thread_safe)
    : graphite_context_(std::move(graphite_context)) {
  DCHECK(graphite_context_);
  if (is_thread_safe) {
    lock_.emplace();
  }
}

GraphiteSharedContext::~GraphiteSharedContext() = default;

skgpu::BackendApi GraphiteSharedContext::backend() const {
  AutoLock auto_lock(this);
  return graphite_context_->backend();
}

std::unique_ptr<skgpu::graphite::Recorder> GraphiteSharedContext::makeRecorder(
    const skgpu::graphite::RecorderOptions& options) {
  AutoLock auto_lock(this);
  return graphite_context_->makeRecorder(options);
}

std::unique_ptr<skgpu::graphite::PrecompileContext>
GraphiteSharedContext::makePrecompileContext() {
  AutoLock auto_lock(this);
  return graphite_context_->makePrecompileContext();
}

bool GraphiteSharedContext::insertRecording(
    const skgpu::graphite::InsertRecordingInfo& info) {
  AutoLock auto_lock(this);
  scoped_refptr<base::SingleThreadTaskRunner> task_runner =
      IsThreadSafe() && base::SingleThreadTaskRunner::HasCurrentDefault()
          ? base::SingleThreadTaskRunner::GetCurrentDefault()
          : nullptr;

  // Ensure fFinishedProc is called on the original thread if there is only one
  // graphite::Context.
  if (info.fFinishedProc && task_runner) {
    skgpu::graphite::InsertRecordingInfo info_copy = info;
    info_copy.fFinishedContext = new RecordingContext{
        info.fFinishedProc, info.fFinishedContext, std::move(task_runner)};

    info_copy.fFinishedProc = [](void* ctx, skgpu::CallbackResult result) {
      auto context = base::WrapUnique(static_cast<RecordingContext*>(ctx));
      DCHECK(context->old_finished_proc);
      base::SingleThreadTaskRunner* task_runner = context->task_runner.get();
      if (task_runner && !task_runner->BelongsToCurrentThread()) {
        task_runner->PostTask(FROM_HERE,
                              base::BindOnce(context->old_finished_proc,
                                             context->old_context, result));
        return;
      }
      context->old_finished_proc(context->old_context, result);
    };

    return graphite_context_->insertRecording(info_copy);
  }

  return graphite_context_->insertRecording(info);
}

bool GraphiteSharedContext::submit(skgpu::graphite::SyncToCpu syncToCpu) {
  AutoLock auto_lock(this);
  return graphite_context_->submit(syncToCpu);
}

bool GraphiteSharedContext::hasUnfinishedGpuWork() const {
  AutoLock auto_lock(this);
  return graphite_context_->hasUnfinishedGpuWork();
}

void GraphiteSharedContext::asyncRescaleAndReadPixels(
    const SkImage* src,
    const SkImageInfo& dstImageInfo,
    const SkIRect& srcRect,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  return graphite_context_->asyncRescaleAndReadPixels(
      src, dstImageInfo, srcRect, rescaleGamma, rescaleMode,
      &ReadPixelsCallbackThreadSafe, new_callbackContext);
}

void GraphiteSharedContext::asyncRescaleAndReadPixels(
    const SkSurface* src,
    const SkImageInfo& dstImageInfo,
    const SkIRect& srcRect,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  return graphite_context_->asyncRescaleAndReadPixels(
      src, dstImageInfo, srcRect, rescaleGamma, rescaleMode,
      &ReadPixelsCallbackThreadSafe, new_callbackContext);
}

bool GraphiteSharedContext::asyncRescaleAndReadPixelsAndSubmit(
    const SkImage* src,
    const SkImageInfo& dstImageInfo,
    const SkIRect& srcRect,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  graphite_context_->asyncRescaleAndReadPixels(
      src, dstImageInfo, srcRect, rescaleGamma, rescaleMode,
      &ReadPixelsCallbackThreadSafe, new_callbackContext);

  return graphite_context_->submit(skgpu::graphite::SyncToCpu::kYes);
}

bool GraphiteSharedContext::asyncRescaleAndReadPixelsAndSubmit(
    const SkSurface* src,
    const SkImageInfo& dstImageInfo,
    const SkIRect& srcRect,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  graphite_context_->asyncRescaleAndReadPixels(
      src, dstImageInfo, srcRect, rescaleGamma, rescaleMode,
      &ReadPixelsCallbackThreadSafe, new_callbackContext);

  return graphite_context_->submit(skgpu::graphite::SyncToCpu::kYes);
}

void GraphiteSharedContext::asyncRescaleAndReadPixelsYUV420(
    const SkImage* src,
    SkYUVColorSpace yuvColorSpace,
    sk_sp<SkColorSpace> dstColorSpace,
    const SkIRect& srcRect,
    const SkISize& dstSize,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  return graphite_context_->asyncRescaleAndReadPixelsYUV420(
      src, yuvColorSpace, dstColorSpace, srcRect, dstSize, rescaleGamma,
      rescaleMode, &ReadPixelsCallbackThreadSafe, new_callbackContext);
}

void GraphiteSharedContext::asyncRescaleAndReadPixelsYUV420(
    const SkSurface* src,
    SkYUVColorSpace yuvColorSpace,
    sk_sp<SkColorSpace> dstColorSpace,
    const SkIRect& srcRect,
    const SkISize& dstSize,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  return graphite_context_->asyncRescaleAndReadPixelsYUV420(
      src, yuvColorSpace, dstColorSpace, srcRect, dstSize, rescaleGamma,
      rescaleMode, &ReadPixelsCallbackThreadSafe, new_callbackContext);
}

void GraphiteSharedContext::asyncRescaleAndReadPixelsYUVA420(
    const SkImage* src,
    SkYUVColorSpace yuvColorSpace,
    sk_sp<SkColorSpace> dstColorSpace,
    const SkIRect& srcRect,
    const SkISize& dstSize,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  return graphite_context_->asyncRescaleAndReadPixelsYUVA420(
      src, yuvColorSpace, dstColorSpace, srcRect, dstSize, rescaleGamma,
      rescaleMode, &ReadPixelsCallbackThreadSafe, new_callbackContext);
}

void GraphiteSharedContext::asyncRescaleAndReadPixelsYUVA420(
    const SkSurface* src,
    SkYUVColorSpace yuvColorSpace,
    sk_sp<SkColorSpace> dstColorSpace,
    const SkIRect& srcRect,
    const SkISize& dstSize,
    SkImage::RescaleGamma rescaleGamma,
    SkImage::RescaleMode rescaleMode,
    SkImageReadPixelsCallback callback,
    SkImage::ReadPixelsContext callbackContext) {
  AutoLock auto_lock(this);
  auto* new_callbackContext = CreateAsyncReadContextThreadSafe(
      std::move(callback), callbackContext, IsThreadSafe());

  return graphite_context_->asyncRescaleAndReadPixelsYUVA420(
      src, yuvColorSpace, dstColorSpace, srcRect, dstSize, rescaleGamma,
      rescaleMode, &ReadPixelsCallbackThreadSafe, new_callbackContext);
}

void GraphiteSharedContext::checkAsyncWorkCompletion() {
  AutoLock auto_lock(this);
  return graphite_context_->checkAsyncWorkCompletion();
}

void GraphiteSharedContext::deleteBackendTexture(
    const skgpu::graphite::BackendTexture& texture) {
  AutoLock auto_lock(this);
  return graphite_context_->deleteBackendTexture(texture);
}

void GraphiteSharedContext::freeGpuResources() {
  AutoLock auto_lock(this);
  return graphite_context_->freeGpuResources();
}

void GraphiteSharedContext::performDeferredCleanup(
    std::chrono::milliseconds msNotUsed) {
  AutoLock auto_lock(this);
  return graphite_context_->performDeferredCleanup(msNotUsed);
}

size_t GraphiteSharedContext::currentBudgetedBytes() const {
  AutoLock auto_lock(this);
  return graphite_context_->currentBudgetedBytes();
}

size_t GraphiteSharedContext::currentPurgeableBytes() const {
  AutoLock auto_lock(this);
  return graphite_context_->currentPurgeableBytes();
}

size_t GraphiteSharedContext::maxBudgetedBytes() const {
  AutoLock auto_lock(this);
  return graphite_context_->maxBudgetedBytes();
}

void GraphiteSharedContext::setMaxBudgetedBytes(size_t bytes) {
  AutoLock auto_lock(this);
  return graphite_context_->setMaxBudgetedBytes(bytes);
}

void GraphiteSharedContext::dumpMemoryStatistics(
    SkTraceMemoryDump* traceMemoryDump) const {
  AutoLock auto_lock(this);
  return graphite_context_->dumpMemoryStatistics(traceMemoryDump);
}

bool GraphiteSharedContext::isDeviceLost() const {
  AutoLock auto_lock(this);
  return graphite_context_->isDeviceLost();
}

int GraphiteSharedContext::maxTextureSize() const {
  AutoLock auto_lock(this);
  return graphite_context_->maxTextureSize();
}

bool GraphiteSharedContext::supportsProtectedContent() const {
  AutoLock auto_lock(this);
  return graphite_context_->supportsProtectedContent();
}

skgpu::GpuStatsFlags GraphiteSharedContext::supportedGpuStats() const {
  AutoLock auto_lock(this);
  return graphite_context_->supportedGpuStats();
}

}  // namespace gpu
