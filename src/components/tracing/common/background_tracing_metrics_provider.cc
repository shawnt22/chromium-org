// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/tracing/common/background_tracing_metrics_provider.h"

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "components/metrics/content/gpu_metrics_provider.h"
#include "components/metrics/cpu_metrics_provider.h"
#include "components/metrics/metrics_features.h"
#include "components/metrics/metrics_log.h"
#include "content/public/browser/background_tracing_manager.h"
#include "third_party/metrics_proto/chrome_user_metrics_extension.pb.h"
#include "third_party/metrics_proto/trace_log.pb.h"

namespace tracing {
namespace {

base::RepeatingCallback<void(metrics::SystemProfileProto&)>&
GetSystemProfileMetricsRecorder() {
  static base::NoDestructor<
      base::RepeatingCallback<void(metrics::SystemProfileProto&)>>
      recorder;
  return *recorder;
}

}  // namespace

base::RepeatingCallback<void(metrics::SystemProfileProto&)>
BackgroundTracingMetricsProvider::GetSystemProfileMetricsRecorder() {
  return tracing::GetSystemProfileMetricsRecorder();
}

BackgroundTracingMetricsProvider::BackgroundTracingMetricsProvider() {
  system_profile_providers_.emplace_back(
      std::make_unique<metrics::CPUMetricsProvider>());
  system_profile_providers_.emplace_back(
      std::make_unique<metrics::GPUMetricsProvider>());
  tracing::GetSystemProfileMetricsRecorder() = base::BindRepeating(
      [](base::WeakPtr<BackgroundTracingMetricsProvider> self,
         metrics::SystemProfileProto& system_profile_proto) {
        if (self) {
          self->RecordSystemProfileMetrics(system_profile_proto);
        }
      },
      weak_factory_.GetWeakPtr());
}

BackgroundTracingMetricsProvider::~BackgroundTracingMetricsProvider() = default;

void BackgroundTracingMetricsProvider::RecordSystemProfileMetrics(
    metrics::SystemProfileProto& system_profile_proto) {
  RecordCoreSystemProfileMetrics(system_profile_proto);
  // BackgroundTracingManager stores the returned system profile together with
  // the trace in the trace database at trace recording time.
  // ProvideIndependentMetrics() later overrides the system_profile in the log
  // proto with these stored metrics, to ensure that the uploaded system profile
  // matches the system profile at trace recording time.
  for (auto& provider : system_profile_providers_) {
    provider->ProvideSystemProfileMetricsWithLogCreationTime(
        base::TimeTicks::Now(), &system_profile_proto);
  }
}

bool BackgroundTracingMetricsProvider::HasIndependentMetrics() {
  return content::BackgroundTracingManager::GetInstance().HasTraceToUpload();
}

void BackgroundTracingMetricsProvider::ProvideIndependentMetrics(
    base::OnceClosure serialize_log_callback,
    base::OnceCallback<void(bool)> done_callback,
    metrics::ChromeUserMetricsExtension* uma_proto,
    base::HistogramSnapshotManager* snapshot_manager) {
  auto task_runner = base::SequencedTaskRunner::GetCurrentDefault();
  auto provide_embedder_metrics = GetEmbedderMetricsProvider();
  content::BackgroundTracingManager::GetInstance().GetTraceToUpload(
      base::BindOnce(
          [](base::OnceCallback<bool(metrics::ChromeUserMetricsExtension*,
                                     std::string&&)> provide_embedder_metrics,
             base::OnceClosure serialize_log_callback,
             base::OnceCallback<void(bool)> done_callback,
             metrics::ChromeUserMetricsExtension* uma_proto,
             scoped_refptr<base::SequencedTaskRunner> task_runner,
             std::optional<std::string> compressed_trace_content,
             std::optional<std::string> serialized_system_profile,
             base::OnceClosure upload_complete) {
            if (!compressed_trace_content ||
                !std::move(provide_embedder_metrics)
                     .Run(uma_proto, std::move(*compressed_trace_content))) {
              task_runner->PostTask(
                  FROM_HERE, base::BindOnce(std::move(done_callback), false));
              return;
            }
            if (serialized_system_profile) {
              metrics::SystemProfileProto system_profile;
              system_profile.ParsePartialFromString(*serialized_system_profile);
              uma_proto->mutable_system_profile()->MergeFrom(system_profile);
            }
            // Serialize the log on a worker thread instead of on the main
            // thread.
            base::ThreadPool::PostTask(
                FROM_HERE,
                {base::TaskPriority::USER_VISIBLE,
                 base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
                std::move(serialize_log_callback)
                    .Then(base::BindPostTask(
                        task_runner,
                        base::BindOnce(std::move(done_callback)
                                           .Then(std::move(upload_complete)),
                                       true))));
          },
          std::move(provide_embedder_metrics),
          std::move(serialize_log_callback), std::move(done_callback),
          uma_proto, task_runner));
}

base::OnceCallback<bool(metrics::ChromeUserMetricsExtension*, std::string&&)>
BackgroundTracingMetricsProvider::GetEmbedderMetricsProvider() {
  return base::BindOnce([](metrics::ChromeUserMetricsExtension* uma_proto,
                           std::string&& compressed_trace) {
    SetTrace(uma_proto->add_trace_log(), std::move(compressed_trace));
    return true;
  });
}

// static
void BackgroundTracingMetricsProvider::SetTrace(
    metrics::TraceLog* log,
    std::string&& compressed_trace) {
  base::UmaHistogramCounts100000("Tracing.Background.UploadingTraceSizeInKB",
                                 compressed_trace.size() / 1024);

  log->set_raw_data(std::move(compressed_trace));
  log->set_compression_type(metrics::TraceLog::COMPRESSION_TYPE_ZLIB);
}

}  // namespace tracing
