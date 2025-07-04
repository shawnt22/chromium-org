// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/resource_attribution/cpu_measurement_monitor.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/numerics/clamped_math.h"
#include "base/sequence_checker.h"
#include "base/strings/strcat.h"
#include "base/system/sys_info.h"
#include "base/task/task_traits.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/types/optional_util.h"
#include "base/types/variant_util.h"
#include "components/performance_manager/graph/frame_node_impl.h"
#include "components/performance_manager/graph/page_node_impl.h"
#include "components/performance_manager/graph/process_node_impl.h"
#include "components/performance_manager/graph/worker_node_impl.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/graph/graph_operations.h"
#include "components/performance_manager/public/resource_attribution/attribution_helpers.h"
#include "components/performance_manager/public/resource_attribution/frame_context.h"
#include "components/performance_manager/public/resource_attribution/worker_context.h"
#include "components/performance_manager/resource_attribution/cpu_measurement_data.h"
#include "components/performance_manager/resource_attribution/graph_change.h"
#include "components/performance_manager/resource_attribution/worker_client_pages.h"
#include "content/public/common/process_type.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace resource_attribution {

namespace {

using performance_manager::features::kResourceAttributionIncludeOrigins;

// CHECK's that `result` obeys all constraints: the start and end timestamps
// form a positive interval and `cumulative_cpu` will fit into that interval.
void ValidateCPUTimeResult(const CPUTimeResult& result) {
  // Start and end must form a valid interval.
  CHECK(!result.metadata.measurement_time.is_null());
  CHECK(!result.start_time.is_null());
  const base::TimeDelta interval =
      result.metadata.measurement_time - result.start_time;
  CHECK(interval.is_positive());

  CHECK(!result.cumulative_cpu.is_negative());
}

template <typename FrameOrWorkerNode>
std::optional<OriginInBrowsingInstanceContext>
OriginInBrowsingInstanceContextForNode(
    const FrameOrWorkerNode* node,
    content::BrowsingInstanceId browsing_instance,
    GraphChange graph_change = NoGraphChange{}) {
  if (!base::FeatureList::IsEnabled(kResourceAttributionIncludeOrigins)) {
    return std::nullopt;
  }
  // If this node was just assigned a new origin, assign CPU usage before the
  // change to the previous origin.
  GraphChangeUpdateOrigin* origin_change =
      std::get_if<GraphChangeUpdateOrigin>(&graph_change);
  std::optional<url::Origin> origin;
  if (origin_change && origin_change->node == node) {
    origin = origin_change->previous_origin;
  } else {
    origin = node->GetOrigin();
  }
  if (!origin.has_value()) {
    return std::nullopt;
  }
  return OriginInBrowsingInstanceContext(origin.value(), browsing_instance);
}

void DestroyCPUMeasurementData(const ProcessNode* process_node) {
  auto* node_impl = ProcessNodeImpl::FromNode(process_node);
  if (CPUMeasurementData::Exists(node_impl)) {
    CPUMeasurementData::Destroy(node_impl);
  }
}

template <typename NodeImpl>
scoped_refptr<ScopedCPUTimeResult>& GetNodeImplResultPtr(NodeImpl* node_impl) {
  // Create() is called from On*NodeAdded, but UpdateCPUMeasurements() from the
  // same function can visit nodes whose On*NodeAdded hasn't been called yet. So
  // must ensure that the SharedCPUTimeResultData has been created.
  if (!SharedCPUTimeResultData::Exists(node_impl)) {
    return SharedCPUTimeResultData::Create(node_impl).result_ptr;
  }
  return SharedCPUTimeResultData::Get(node_impl).result_ptr;
}

scoped_refptr<ScopedCPUTimeResult>& GetNodeResultPtr(const FrameNode* node) {
  CHECK(node);
  return GetNodeImplResultPtr(FrameNodeImpl::FromNode(node));
}

scoped_refptr<ScopedCPUTimeResult>& GetNodeResultPtr(const PageNode* node) {
  CHECK(node);
  return GetNodeImplResultPtr(PageNodeImpl::FromNode(node));
}

scoped_refptr<ScopedCPUTimeResult>& GetNodeResultPtr(const ProcessNode* node) {
  CHECK(node);
  return GetNodeImplResultPtr(ProcessNodeImpl::FromNode(node));
}

scoped_refptr<ScopedCPUTimeResult>& GetNodeResultPtr(const WorkerNode* node) {
  CHECK(node);
  return GetNodeImplResultPtr(WorkerNodeImpl::FromNode(node));
}

}  // namespace

CPUMeasurementMonitor::CPUMeasurementMonitor()
    : delegate_factory_(CPUMeasurementDelegate::GetDefaultFactory()) {}

CPUMeasurementMonitor::~CPUMeasurementMonitor() {
  if (graph_) {
    StopMonitoring();
  }
  CHECK(!graph_);
}

void CPUMeasurementMonitor::SetDelegateFactoryForTesting(
    CPUMeasurementDelegate::Factory* factory) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Ensure that this is called before StartMonitoring() so all CPU measurements
  // use the same delegate.
  CHECK(!graph_);
  CHECK(factory);
  delegate_factory_ = factory;
}

void CPUMeasurementMonitor::StartMonitoring(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(!graph_);
  CHECK(origin_results_.empty());
  CHECK(weak_origin_results_.empty());
  CHECK(dead_context_results_.empty());
  graph_ = graph;
  graph_->AddFrameNodeObserver(this);
  graph_->AddPageNodeObserver(this);
  graph_->AddProcessNodeObserver(this);
  graph_->AddWorkerNodeObserver(this);

  // Start monitoring CPU usage for all existing processes. Can't read their CPU
  // usage until they have a pid assigned.
  for (const ProcessNode* process_node : graph_->GetAllProcessNodes()) {
    if (delegate_factory_->ShouldMeasureProcess(process_node)) {
      MonitorCPUUsage(process_node);
    }
  }
}

void CPUMeasurementMonitor::StopMonitoring() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(graph_);
  for (const FrameNode* frame_node : graph_->GetAllFrameNodes()) {
    GetNodeResultPtr(frame_node).reset();
  }
  for (const PageNode* page_node : graph_->GetAllPageNodes()) {
    GetNodeResultPtr(page_node).reset();
  }
  for (const ProcessNode* process_node : graph_->GetAllProcessNodes()) {
    GetNodeResultPtr(process_node).reset();
    DestroyCPUMeasurementData(process_node);
  }
  for (const WorkerNode* worker_node : graph_->GetAllWorkerNodes()) {
    GetNodeResultPtr(worker_node).reset();
  }
  origin_results_.clear();
  dead_context_results_.clear();
  // All ScopedCPUTimeResult objects should be deleted above, removing them all
  // from `weak_origin_results_`.
  CHECK(weak_origin_results_.empty());
  graph_->RemoveFrameNodeObserver(this);
  graph_->RemovePageNodeObserver(this);
  graph_->RemoveProcessNodeObserver(this);
  graph_->RemoveWorkerNodeObserver(this);
  graph_ = nullptr;
}

bool CPUMeasurementMonitor::IsMonitoring() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return graph_;
}

void CPUMeasurementMonitor::RepeatingQueryStarted(internal::QueryId query_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(IsMonitoring());
  // Start with an empty dead measurement list for this query.
  const auto [_, inserted] =
      dead_context_results_.try_emplace(query_id, DeadContextResults{});
  CHECK(inserted);
}

void CPUMeasurementMonitor::RepeatingQueryStopped(internal::QueryId query_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(IsMonitoring());
  size_t erased = dead_context_results_.erase(query_id);
  CHECK_EQ(erased, 1u);
}

bool CPUMeasurementMonitor::IsTrackingQueryForTesting(
    internal::QueryId query_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return base::Contains(dead_context_results_, query_id);
}

size_t CPUMeasurementMonitor::GetDeadContextCountForTesting() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  size_t count = 0;
  for (const auto& [_, results_for_query] : dead_context_results_) {
    count += results_for_query.to_report.size();
    count += results_for_query.kept_alive.size();
  }
  return count;
}

QueryResultMap CPUMeasurementMonitor::UpdateAndGetCPUMeasurements(
    std::optional<internal::QueryId> query_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  UpdateAllCPUMeasurements();

  QueryResultMap results;
  auto save_result = [&results](
                         const ResourceContext& context,
                         const scoped_refptr<ScopedCPUTimeResult>& result_ptr) {
    if (result_ptr) {
      ValidateCPUTimeResult(result_ptr->result());
      // Each context should only be added once.
      const auto [_, inserted] = results.emplace(
          context, QueryResults{.cpu_time_result = result_ptr->result()});
      CHECK(inserted);
    }
  };

  // Populate `results` with CPU results for all live PM node contexts.
  for (const FrameNode* node : graph_->GetAllFrameNodes()) {
    save_result(node->GetResourceContext(), GetNodeResultPtr(node));
  }
  for (const PageNode* node : graph_->GetAllPageNodes()) {
    save_result(node->GetResourceContext(), GetNodeResultPtr(node));
  }
  for (const ProcessNode* node : graph_->GetAllProcessNodes()) {
    save_result(node->GetResourceContext(), GetNodeResultPtr(node));
  }
  for (const WorkerNode* node : graph_->GetAllWorkerNodes()) {
    save_result(node->GetResourceContext(), GetNodeResultPtr(node));
  }

  // Get the set of live `OriginInBrowsingInstanceContext`s.
  //
  // TODO(crbug.com/333248839): Find a way to reduce the number of iterations
  // over resource contexts. UpdateAllCPUMeasurmements() above iterates over
  // contexts that have measurement deltas where as
  // GetLiveOriginInBrowsingInstanceContexts() below iterates over all resource
  // contexts.
  const std::set<OriginInBrowsingInstanceContext> live_origin_contexts =
      GetLiveOriginInBrowsingInstanceContexts();

  // Populate `results` with CPU results for all live
  // `OriginInBrowsingInstanceContext`s, and remove dead ones.
  for (auto it = origin_results_.begin(); it != origin_results_.end();) {
    const OriginInBrowsingInstanceContext& origin_context = it->first;
    scoped_refptr<ScopedCPUTimeResult>& result_ptr = it->second;
    CHECK(result_ptr);
    if (base::Contains(live_origin_contexts, origin_context)) {
      save_result(ResourceContext(origin_context), result_ptr);
      ++it;
    } else {
      SaveFinalMeasurement(std::move(result_ptr));
      it = origin_results_.erase(it);
    }
  }

  // Populate `results` with CPU results for contexts that became dead since the
  // last time this query got an update (note: non-repeating queries don't get
  // results for dead contexts).
  if (query_id.has_value()) {
    auto it = dead_context_results_.find(query_id.value());
    CHECK(it != dead_context_results_.end());

    // Results kept alive in case their dead context was revived by the time of
    // this measurement can be now released.
    auto& dead_context_results_kept_alive = it->second.kept_alive;
    dead_context_results_kept_alive.clear();

    std::set<scoped_refptr<ScopedCPUTimeResult>> dead_context_results_to_report;
    std::swap(it->second.to_report, dead_context_results_to_report);

    for (auto& result : dead_context_results_to_report) {
      ValidateCPUTimeResult(result->result());

      // If the context was revived since being added to
      // `dead_context_results_to_report`, it may already be in `results`, in
      // which case the `emplace()` below no-ops (but the result in `results`
      // and `dead_context_results_to_report` must match, see DCHECK).
      const auto [results_it, inserted] = results.emplace(
          result->context(), QueryResults{.cpu_time_result = result->result()});
      DCHECK(results_it->second.cpu_time_result.value() == result->result());

      if (inserted &&
          ContextIs<OriginInBrowsingInstanceContext>(result->context())) {
        // Keep a reference to the `ScopedCPUTimeResult` of a dead
        // `OriginInBrowsingContext` until the next measurement, so it can be
        // reused if the context is revived.
        dead_context_results_kept_alive.insert(std::move(result));
      }
    }
  }

  return results;
}

void CPUMeasurementMonitor::RecordMemoryMetrics() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!graph_) {
    // Don't record any metrics if not currently monitoring.
    return;
  }

  constexpr size_t kNumContextTypes = std::variant_size<ResourceContext>::value;

  // Estimates for each live ResourceContext type by index into the
  // ResourceContext variant.
  std::set<ScopedCPUTimeResult*> visited_result_ptrs;
  std::vector<base::ClampedNumeric<size_t>> live_context_estimates(
      kNumContextTypes);
  base::ClampedNumeric<size_t> total_live_estimate = 0;
  auto update_live_estimates =
      [&](const ResourceContext& context,
          const scoped_refptr<ScopedCPUTimeResult>& result_ptr,
          size_t overhead) {
        base::ClampedNumeric<size_t> estimate = overhead;
        if (result_ptr) {
          const auto [_, inserted] =
              visited_result_ptrs.insert(result_ptr.get());
          CHECK(inserted);

          // Each result has a single reference.
          estimate += result_ptr->EstimateMemoryUsage();
        }
        live_context_estimates.at(context.index()) += estimate;
        total_live_estimate += estimate;
      };

  // Overhead in NodeInlineData is one pointer per node.
  for (const FrameNode* node : graph_->GetAllFrameNodes()) {
    update_live_estimates(node->GetResourceContext(), GetNodeResultPtr(node),
                          sizeof(scoped_refptr<ScopedCPUTimeResult>));
  }
  for (const PageNode* node : graph_->GetAllPageNodes()) {
    update_live_estimates(node->GetResourceContext(), GetNodeResultPtr(node),
                          sizeof(scoped_refptr<ScopedCPUTimeResult>));
  }
  for (const ProcessNode* node : graph_->GetAllProcessNodes()) {
    update_live_estimates(node->GetResourceContext(), GetNodeResultPtr(node),
                          sizeof(scoped_refptr<ScopedCPUTimeResult>));
  }
  for (const WorkerNode* node : graph_->GetAllWorkerNodes()) {
    update_live_estimates(node->GetResourceContext(), GetNodeResultPtr(node),
                          sizeof(scoped_refptr<ScopedCPUTimeResult>));
  }

  // Overhead in `origin_results_` is one pair (value_type) per map entry.
  for (const auto& [context, result_ptr] : origin_results_) {
    CHECK(result_ptr);
    update_live_estimates(context, result_ptr,
                          sizeof(decltype(origin_results_)::value_type));
  }

  // Estimates for each dead ResourceContext type by index into the
  // ResourceContext variant.
  std::vector<base::ClampedNumeric<size_t>> dead_context_estimates(
      kNumContextTypes);
  base::ClampedNumeric<size_t> total_dead_estimate = 0;
  for (const auto& [_, dead_context_results_for_query] :
       dead_context_results_) {
    for (const auto& dead_context_results_set :
         {dead_context_results_for_query.kept_alive,
          dead_context_results_for_query.to_report}) {
      for (const auto& result : dead_context_results_set) {
        const auto [_, inserted] = visited_result_ptrs.insert(result.get());

        // There can be multiple references to the same `ScopedCPUTimeResult`.
        // Only include the size of the `ScopedCPUTimeResult` object the first
        // time it's seen, but always include the size of the pointer.
        auto estimate = sizeof(scoped_refptr<ScopedCPUTimeResult>);
        if (inserted) {
          estimate += result->EstimateMemoryUsage();
        }

        dead_context_estimates.at(result->context().index()) += estimate;
        total_dead_estimate += estimate;
      }
    }
  }

  for (size_t index = 0; index < kNumContextTypes; ++index) {
    const char* context_name = nullptr;
    switch (index) {
      case base::VariantIndexOfType<ResourceContext, FrameContext>():
        context_name = "FrameContexts";
        break;
      case base::VariantIndexOfType<ResourceContext, PageContext>():
        context_name = "PageContexts";
        break;
      case base::VariantIndexOfType<ResourceContext, ProcessContext>():
        context_name = "ProcessContexts";
        break;
      case base::VariantIndexOfType<ResourceContext, WorkerContext>():
        context_name = "WorkerContexts";
        break;
      case base::VariantIndexOfType<ResourceContext,
                                    OriginInBrowsingInstanceContext>():
        context_name = "OriginInBrowsingInstanceContexts";
        break;
    }
    CHECK(context_name);

    base::UmaHistogramMemoryKB(
        base::StrCat(
            {"PerformanceManager.CPUMonitorMemoryUse.", context_name, ".Live"}),
        live_context_estimates.at(index) / 1024);
    base::UmaHistogramMemoryKB(
        base::StrCat(
            {"PerformanceManager.CPUMonitorMemoryUse.", context_name, ".Dead"}),
        dead_context_estimates.at(index) / 1024);
    base::UmaHistogramMemoryKB(
        base::StrCat({"PerformanceManager.CPUMonitorMemoryUse.", context_name,
                      ".Total"}),
        (live_context_estimates.at(index) + dead_context_estimates.at(index)) /
            1024);
  }
  base::UmaHistogramMemoryKB(
      "PerformanceManager.CPUMonitorMemoryUse.AllContexts.Live",
      total_live_estimate / 1024);
  base::UmaHistogramMemoryKB(
      "PerformanceManager.CPUMonitorMemoryUse.AllContexts.Dead",
      total_dead_estimate / 1024);
  base::UmaHistogramMemoryKB(
      "PerformanceManager.CPUMonitorMemoryUse.AllContexts.Total",
      (total_live_estimate + total_dead_estimate) / 1024);
}

void CPUMeasurementMonitor::OnBeforeFrameNodeAdded(
    const FrameNode* frame_node,
    const FrameNode* pending_parent_frame_node,
    const PageNode* pending_page_node,
    const ProcessNode* pending_process_node,
    const FrameNode* pending_parent_or_outer_document_or_embedder) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SharedCPUTimeResultData::Create(FrameNodeImpl::FromNode(frame_node));
  // Take a measurement of the process CPU usage *before* this node was added.
  // This is safe because frames should only be added after their containing
  // process has started.
  UpdateCPUMeasurements(pending_process_node);
}

void CPUMeasurementMonitor::OnBeforeFrameNodeRemoved(
    const FrameNode* frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage, including this frame, so that
  // its final CPU usage is attributed to it before it's removed.
  UpdateCPUMeasurements(frame_node->GetProcessNode());
  SaveFinalMeasurement(std::move(GetNodeResultPtr(frame_node)));
}

void CPUMeasurementMonitor::OnOriginChanged(
    const FrameNode* frame_node,
    const std::optional<url::Origin>& previous_value) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage, but assign this frame's CPU to
  // its previous origin for OriginInBrowsingInstanceContext, so that the CPU
  // usage from before the navigation committed is attributed to the old origin.
  UpdateCPUMeasurements(frame_node->GetProcessNode(),
                        GraphChangeUpdateOrigin(frame_node, previous_value));
}

void CPUMeasurementMonitor::OnPageNodeAdded(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SharedCPUTimeResultData::Create(PageNodeImpl::FromNode(page_node));
}

void CPUMeasurementMonitor::OnBeforePageNodeRemoved(const PageNode* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // No need to call UpdateCPUMeasurements() since a measurement was taken when
  // the last frame was removed from the page.
  SaveFinalMeasurement(std::move(GetNodeResultPtr(page_node)));
}

void CPUMeasurementMonitor::OnProcessNodeAdded(
    const ProcessNode* process_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SharedCPUTimeResultData::Create(ProcessNodeImpl::FromNode(process_node));
}

void CPUMeasurementMonitor::OnProcessLifetimeChange(
    const ProcessNode* process_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!graph_) {
    // Not monitoring CPU usage yet.
    return;
  }
  if (delegate_factory_->ShouldMeasureProcess(process_node)) {
    MonitorCPUUsage(process_node);
  }
}

void CPUMeasurementMonitor::OnBeforeProcessNodeRemoved(
    const ProcessNode* process_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // On most platforms this will get no updates as the OS process is no longer
  // running. Windows and Fuchsia will return final measurements of a process
  // after it exits.
  // TODO(crbug.com/325330345): Capture the full final measurement reported
  // through ChildProcessTerminationInfo::cpu_usage.
  UpdateCPUMeasurements(process_node);
  SaveFinalMeasurement(std::move(GetNodeResultPtr(process_node)));
}

void CPUMeasurementMonitor::OnPriorityChanged(
    const ProcessNode* process_node,
    base::TaskPriority previous_value) {
  UpdateCPUMeasurements(process_node, GraphChangeUpdateProcessPriority(
                                          process_node, previous_value));
}

void CPUMeasurementMonitor::OnBeforeWorkerNodeAdded(
    const WorkerNode* worker_node,
    const ProcessNode* pending_process_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SharedCPUTimeResultData::Create(WorkerNodeImpl::FromNode(worker_node));
  // Take a measurement of the process CPU usage *before* this node was added.
  // This is safe because workers should only be added after their containing
  // process has started.
  UpdateCPUMeasurements(pending_process_node);
}

void CPUMeasurementMonitor::OnBeforeWorkerNodeRemoved(
    const WorkerNode* worker_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage, including this node, so that
  // its final CPU usage is attributed to it before it's removed.
  UpdateCPUMeasurements(worker_node->GetProcessNode());
  SaveFinalMeasurement(std::move(GetNodeResultPtr(worker_node)));
}

void CPUMeasurementMonitor::OnBeforeClientFrameAdded(
    const WorkerNode* worker_node,
    const FrameNode* client_frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage *before* this worker gained a
  // client. The CPU measurement will be distributed to pages that were clients
  // of this worker, not including the new client.
  UpdateCPUMeasurements(worker_node->GetProcessNode());
}

void CPUMeasurementMonitor::OnBeforeClientFrameRemoved(
    const WorkerNode* worker_node,
    const FrameNode* client_frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage *before* this worker lost a
  // client. The CPU measurement will be distributed to pages that were
  // clients of this worker, including the old client.
  UpdateCPUMeasurements(worker_node->GetProcessNode());
}

void CPUMeasurementMonitor::OnBeforeClientWorkerAdded(
    const WorkerNode* worker_node,
    const WorkerNode* client_worker_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage *before* this worker gained a
  // client. The CPU measurement will be distributed to pages that were clients
  // of this worker, not including the new client.
  UpdateCPUMeasurements(worker_node->GetProcessNode());
}

void CPUMeasurementMonitor::OnBeforeClientWorkerRemoved(
    const WorkerNode* worker_node,
    const WorkerNode* client_worker_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Take a measurement of the process CPU usage *before* this worker lost a
  // client. The CPU measurement will be distributed to pages that were clients
  // of this worker, including the old client.
  UpdateCPUMeasurements(worker_node->GetProcessNode());
}

base::Value::Dict CPUMeasurementMonitor::DescribeFrameNodeData(
    const FrameNode* node) const {
  return SharedCPUTimeResultData::Get(FrameNodeImpl::FromNode(node)).Describe();
}

base::Value::Dict CPUMeasurementMonitor::DescribePageNodeData(
    const PageNode* node) const {
  return SharedCPUTimeResultData::Get(PageNodeImpl::FromNode(node)).Describe();
}

base::Value::Dict CPUMeasurementMonitor::DescribeProcessNodeData(
    const ProcessNode* node) const {
  return SharedCPUTimeResultData::Get(ProcessNodeImpl::FromNode(node))
      .Describe();
}

base::Value::Dict CPUMeasurementMonitor::DescribeWorkerNodeData(
    const WorkerNode* node) const {
  return SharedCPUTimeResultData::Get(WorkerNodeImpl::FromNode(node))
      .Describe();
}

void CPUMeasurementMonitor::MonitorCPUUsage(const ProcessNode* process_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If a process crashes and is restarted, a new process can be assigned to the
  // same ProcessNode (and the same ProcessContext). When that happens
  // OnProcessLifetimeChange will call MonitorCPUUsage again for the same node,
  // creating a new CPUMeasurement that starts measuring the new process from 0.
  // ApplyMeasurementDeltas will add the new measurements and the old
  // measurements in the same ProcessContext.
  DestroyCPUMeasurementData(process_node);
  CPUMeasurementData::Create(
      ProcessNodeImpl::FromNode(process_node),
      delegate_factory_->CreateDelegateForProcess(process_node));
}

void CPUMeasurementMonitor::UpdateAllCPUMeasurements() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Must call StartMonitoring() before getting measurements.
  CHECK(graph_);

  // Update CPU metrics, attributing the cumulative CPU of each process to its
  // frames and workers.
  std::map<ResourceContext, CPUTimeResult> measurement_deltas;
  for (const ProcessNode* process_node : graph_->GetAllProcessNodes()) {
    MeasureAndDistributeCPUUsage(process_node, NoGraphChange(),
                                 measurement_deltas);
  }
  ApplyMeasurementDeltas(measurement_deltas);
}

void CPUMeasurementMonitor::UpdateCPUMeasurements(
    const ProcessNode* process_node,
    GraphChange graph_change) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Must call StartMonitoring() before getting measurements.
  CHECK(graph_);
  CHECK(process_node);

  if (!base::FeatureList::IsEnabled(kResourceAttributionIncludeOrigins) &&
      std::holds_alternative<GraphChangeUpdateOrigin>(graph_change)) {
    // No need to update measurements on origin changes when origins aren't
    // being measured.
    return;
  }

  // Update CPU metrics, attributing the cumulative CPU of the process to its
  // frames and workers.
  std::map<ResourceContext, CPUTimeResult> measurement_deltas;
  MeasureAndDistributeCPUUsage(process_node, graph_change, measurement_deltas);
  ApplyMeasurementDeltas(measurement_deltas, graph_change);
}

scoped_refptr<ScopedCPUTimeResult>& CPUMeasurementMonitor::GetResultPtr(
    const ResourceContext& context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (ContextIs<FrameContext>(context)) {
    return GetNodeResultPtr(AsContext<FrameContext>(context).GetFrameNode());
  }
  if (ContextIs<PageContext>(context)) {
    return GetNodeResultPtr(AsContext<PageContext>(context).GetPageNode());
  }
  if (ContextIs<ProcessContext>(context)) {
    return GetNodeResultPtr(
        AsContext<ProcessContext>(context).GetProcessNode());
  }
  if (ContextIs<WorkerContext>(context)) {
    return GetNodeResultPtr(AsContext<WorkerContext>(context).GetWorkerNode());
  }
  if (ContextIs<OriginInBrowsingInstanceContext>(context)) {
    const auto& origin_context =
        AsContext<OriginInBrowsingInstanceContext>(context);
    auto [it, inserted] = origin_results_.try_emplace(origin_context, nullptr);
    scoped_refptr<ScopedCPUTimeResult>& result_ptr = it->second;
    if (inserted) {
      // Check if there is a result for this `OriginInBrowsingInstanceContext`
      // which is still referenced by `dead_context_results_`.
      auto result_it = weak_origin_results_.find(origin_context);
      if (result_it != weak_origin_results_.end()) {
        result_ptr = result_it->second;
      }
    }
    return result_ptr;
  }
  // That should cover all context types.
  NOTREACHED();
}

void CPUMeasurementMonitor::ApplyMeasurementDeltas(
    const std::map<ResourceContext, CPUTimeResult>& measurement_deltas,
    GraphChange graph_change) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& [context, delta] : measurement_deltas) {
    CHECK(!ContextIs<PageContext>(context));
    CHECK(!ContextIs<OriginInBrowsingInstanceContext>(context));

    // Add the new process, frame and worker measurements to the existing
    // measurements, and aggregate new frame and worker measurements to pages.
    if (ContextIs<ProcessContext>(context)) {
      ApplySequentialDelta(context, delta);
    } else if (ContextIs<FrameContext>(context)) {
      ApplySequentialDelta(context, delta);

      const FrameNode* frame_node =
          AsContext<FrameContext>(context).GetFrameNode();
      CHECK(frame_node);
      ApplyOverlappingDelta(frame_node->GetPageNode()->GetResourceContext(),
                            delta);

      std::optional<OriginInBrowsingInstanceContext> origin_context =
          OriginInBrowsingInstanceContextForNode(
              frame_node, frame_node->GetBrowsingInstanceId(), graph_change);
      if (origin_context.has_value()) {
        ApplyOverlappingDelta(origin_context.value(), delta);
      }
    } else if (ContextIs<WorkerContext>(context)) {
      ApplySequentialDelta(context, delta);

      const WorkerNode* worker_node =
          AsContext<WorkerContext>(context).GetWorkerNode();
      CHECK(worker_node);
      auto [client_pages, client_browsing_instances] =
          GetWorkerClientPagesAndBrowsingInstances(worker_node);

      for (const PageNode* page_node : client_pages) {
        ApplyOverlappingDelta(page_node->GetResourceContext(), delta);
      }

      for (content::BrowsingInstanceId browsing_instance :
           client_browsing_instances) {
        std::optional<OriginInBrowsingInstanceContext> origin_context =
            OriginInBrowsingInstanceContextForNode(
                worker_node, browsing_instance, graph_change);
        if (origin_context.has_value()) {
          ApplyOverlappingDelta(origin_context.value(), delta);
        }
      }
    } else {
      // That should cover all context types.
      NOTREACHED();
    }
  }
}

void CPUMeasurementMonitor::ApplySequentialDelta(const ResourceContext& context,
                                                 const CPUTimeResult& delta) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ValidateCPUTimeResult(delta);
  scoped_refptr<ScopedCPUTimeResult>& result_ptr = GetResultPtr(context);
  if (!result_ptr) {
    result_ptr =
        base::MakeRefCounted<ScopedCPUTimeResult>(this, context, delta);
    return;
  }

  CPUTimeResult& result = result_ptr->result();
  ValidateCPUTimeResult(result);
  CHECK_EQ(result.metadata.algorithm, delta.metadata.algorithm);
  CHECK_LE(result.metadata.measurement_time, delta.start_time);
  result.metadata.measurement_time = delta.metadata.measurement_time;
  result.cumulative_cpu += delta.cumulative_cpu;
  result.cumulative_background_cpu += delta.cumulative_background_cpu;

  // Adding a valid delta to a valid result should produce a valid result.
  ValidateCPUTimeResult(result);
}

void CPUMeasurementMonitor::ApplyOverlappingDelta(
    const ResourceContext& context,
    const CPUTimeResult& delta) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ValidateCPUTimeResult(delta);
  scoped_refptr<ScopedCPUTimeResult>& result_ptr = GetResultPtr(context);
  if (!result_ptr) {
    result_ptr =
        base::MakeRefCounted<ScopedCPUTimeResult>(this, context, delta);
    result_ptr->result().metadata.algorithm = MeasurementAlgorithm::kSum;
    return;
  }

  CPUTimeResult& result = result_ptr->result();
  ValidateCPUTimeResult(result);
  CHECK_EQ(result.metadata.algorithm, MeasurementAlgorithm::kSum);
  result.metadata.measurement_time = std::max(result.metadata.measurement_time,
                                              delta.metadata.measurement_time);
  result.start_time = std::min(result.start_time, delta.start_time);
  result.cumulative_cpu += delta.cumulative_cpu;
  result.cumulative_background_cpu += delta.cumulative_background_cpu;

  // Adding a valid delta to a valid result should produce a valid result.
  ValidateCPUTimeResult(result);
}

void CPUMeasurementMonitor::SaveFinalMeasurement(
    scoped_refptr<ScopedCPUTimeResult>&& result_ptr) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (result_ptr) {
    // Copy the scoped_refptr to result list for every existing query_id.
    for (auto& [query_id, dead_context_results_for_query] :
         dead_context_results_) {
      dead_context_results_for_query.to_report.emplace(result_ptr);
    }
    // When `result_ptr` goes out of scope it's dropped from the live
    // measurement results. Now there's one reference for every query, and the
    // CPUTimeResult will be deleted once all queries have gotten the result.
  }
}

std::set<OriginInBrowsingInstanceContext>
CPUMeasurementMonitor::GetLiveOriginInBrowsingInstanceContexts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(graph_);
  std::set<OriginInBrowsingInstanceContext> live_origin_contexts;
  for (const FrameNode* frame_node : graph_->GetAllFrameNodes()) {
    std::optional<OriginInBrowsingInstanceContext> origin_context =
        OriginInBrowsingInstanceContextForNode(
            frame_node, frame_node->GetBrowsingInstanceId());
    if (origin_context.has_value()) {
      live_origin_contexts.insert(origin_context.value());
    }
  }
  for (const WorkerNode* worker_node : graph_->GetAllWorkerNodes()) {
    auto [_, client_browsing_instances] =
        GetWorkerClientPagesAndBrowsingInstances(worker_node);

    for (content::BrowsingInstanceId browsing_instance :
         client_browsing_instances) {
      std::optional<OriginInBrowsingInstanceContext> origin_context =
          OriginInBrowsingInstanceContextForNode(worker_node,
                                                 browsing_instance);
      if (origin_context.has_value()) {
        live_origin_contexts.insert(origin_context.value());
      }
    }
  }
  return live_origin_contexts;
}

// static
void CPUMeasurementMonitor::MeasureAndDistributeCPUUsage(
    const ProcessNode* process_node,
    GraphChange graph_change,
    std::map<ResourceContext, CPUTimeResult>& measurement_deltas) {
  auto* node_impl = ProcessNodeImpl::FromNode(process_node);
  if (!CPUMeasurementData::Exists(node_impl)) {
    // In tests, FrameNodes can be added to mock processes that don't have a PID
    // so aren't being monitored.
    return;
  }
  auto& data = CPUMeasurementData::Get(node_impl);

  // TODO(crbug.com/325330345): Handle final CPU usage of a process.
  //
  // There isn't a good way to get the process CPU usage after it exits here:
  //
  // 1. Attempts to measure it with GetCumulativeCPUUsage() will fail because
  //    the process info is already reaped.
  // 2. For these cases the ChildProcessTerminationInfo struct contains a final
  //    `cpu_usage` member. This needs to be collected by a
  //    RenderProcessHostObserver (either PM's RenderProcessUserData or a
  //    dedicated observer). But:
  // 3. MeasureAndDistributeCPUUsage() distributes the process measurements
  //    among FrameNodes and by the time the final `cpu_usage` is available, the
  //    FrameNodes for the process are often gone already. The reason is that
  //    FrameNodes are removed on process exit by another
  //    RenderProcessHostObserver, and the observers can fire in any order.
  //
  // For the record, the call stack that removes a FrameNode is:
  //
  // performance_manager::PerformanceManagerImpl::DeleteNode()
  // performance_manager::PerformanceManagerTabHelper::RenderFrameDeleted()
  // content::WebContentsImpl::WebContentsObserverList::NotifyObservers<>()
  // content::WebContentsImpl::RenderFrameDeleted()
  // content::RenderFrameHostImpl::RenderFrameDeleted()
  // content::RenderFrameHostImpl::RenderProcessGone()
  // content::SiteInstanceGroup::RenderProcessExited() <-- observer
  //
  // So it's not possible to attribute the final CPU usage of a process to its
  // frames without a refactor of PerformanceManager to keep the FrameNodes
  // alive slightly longer, or keeping a snapshot of the frame topology using
  // FrameContext until after the ChildProcessTerminationInfo is received, and
  // using that snapshot to distribute the measurements.
  //
  // Assume that the previous measurement was taken at time A
  // (`data.last_measurement_time()`), and the current measurement is being
  // taken at time B (TimeTicks::Now()). Since a measurement is taken in the
  // CPUMeasurementData constructor, there will always be a previous
  // measurement.
  //
  // Let CPU(T) be the cpu measurement at time T.
  //
  // Note that the process is only measured after it's passed to the graph,
  // which is shortly after it's created, so at "process creation time" C,
  // CPU(C) may have a small value instead of 0. On the first call to
  // MeasureAndDistributeCPUUsage(), `data.most_recent_measurement()` will be
  // CPU(C), from the measurement in the constructor.
  //
  // There are 4 cases:
  //
  // 1. The process was created at time A (this is the first measurement.)
  //
  //      A         B
  // |----|---------|
  // | 0% |    X%   |
  //
  //
  // cumulative_cpu += CPU(B) - CPU(A)
  //
  // CPU(B) = GetCumulativeCPUUsage()
  // CPU(A) = `data.most_recent_measurement()` (set in the constructor)
  //
  // 2. The process existed for the entire duration A..B.
  //
  // A              B
  // |--------------|
  // |      X%      |
  //
  // cumulative_cpu += CPU(B) - CPU(A)
  //
  // CPU(B) = GetCumulativeCPUUsage()
  // CPU(A) = `data.most_recent_measurement()`
  //
  // 3. The process existed at time A, but exited at time D, between A and B.
  //
  // A         D    B
  // |---------+----|
  // |    X%   | 0% |
  //
  // cumulative_cpu += CPU(D) - CPU(A)
  //
  // CPU(D) = ChildProcessTerminationInfo::cpu_usage (currently unavailable)
  // CPU(A) = `data.most_recent_measurement()`
  //
  // 4. Process created at time A and exited at time D, between A and B.
  //
  //      A    D    B
  // |----+----+----|
  // | 0% | X% | 0% |
  //
  // cumulative_cpu += CPU(D) - CPU(A)
  //
  // CPU(D) = ChildProcessTerminationInfo::cpu_usage (currently unavailable)
  // CPU(A) = `data.most_recent_measurement()` (set in the constructor)
  //
  // In case 1 and case 2, `cumulative_cpu` increases by
  // `GetCumulativeCPUUsage() - data.most_recent_measurement()`. Case 3 and 4
  // can be ignored because GetCumulativeCPUUsage() will return an error code.
  const base::TimeTicks measurement_interval_start =
      data.last_measurement_time();
  const base::TimeTicks measurement_interval_end = base::TimeTicks::Now();
  CHECK(!measurement_interval_start.is_null());
  CHECK(!measurement_interval_end.is_null());
  if (measurement_interval_start == measurement_interval_end) {
    // No time has passed to measure.
    return;
  }
  // TODO(crbug.com/340226030): Replace with a CHECK.
  if (measurement_interval_start > measurement_interval_end) {
    SCOPED_CRASH_KEY_NUMBER(
        "cpu_measurement", "start",
        measurement_interval_start.since_origin().InMicroseconds());
    SCOPED_CRASH_KEY_NUMBER(
        "cpu_measurement", "end",
        measurement_interval_end.since_origin().InMicroseconds());
    base::debug::DumpWithoutCrashing();
    return;
  }

  std::optional<base::TimeDelta> current_cpu_usage = base::OptionalFromExpected(
      data.measurement_delegate().GetCumulativeCPUUsage());
  if (!current_cpu_usage.has_value()) {
    // GetCumulativeCPUUsage() failed. Don't update the measurement state.
    return;
  }
  if (!data.most_recent_measurement().has_value()) {
    // This is the first successful reading. Just record it.
    data.SetMostRecentMeasurement(current_cpu_usage.value(),
                                  measurement_interval_end);
    return;
  }

  // When measured in quick succession, GetCumulativeCPUUsage() can go
  // backwards.
  if (current_cpu_usage.value() < data.most_recent_measurement().value()) {
    current_cpu_usage = data.most_recent_measurement();
  }

  const base::TimeDelta cumulative_cpu_delta =
      current_cpu_usage.value() - data.most_recent_measurement().value();
  data.SetMostRecentMeasurement(current_cpu_usage.value(),
                                measurement_interval_end);

  // Determine the process priority during the measurement interval. If the
  // process' priority just changed, used the previous priority. Otherwise, use
  // the current priority.
  base::TaskPriority process_priority;
  GraphChangeUpdateProcessPriority* priority_change =
      std::get_if<GraphChangeUpdateProcessPriority>(&graph_change);
  if (priority_change && priority_change->process_node == process_node) {
    process_priority = priority_change->previous_priority;
  } else {
    process_priority = process_node->GetPriority();
  }

  auto record_cpu_deltas = [&measurement_deltas, &measurement_interval_start,
                            &measurement_interval_end,
                            &process_priority](const ResourceContext& context,
                                               base::TimeDelta cpu_delta,
                                               MeasurementAlgorithm algorithm) {
    // Each ProcessNode should be updated by one call to
    // MeasureAndDistributeCPUUsage(), and each FrameNode and WorkerNode is in a
    // single process, so none of these contexts should be in the map yet. Each
    // FrameNode or WorkerNode's containing process is measured when the node is
    // added, so `start_time` will be correctly set to the first time the node
    // is measured.
    CHECK(!cpu_delta.is_negative());
    const auto [_, inserted] = measurement_deltas.emplace(
        context,
        CPUTimeResult{
            .metadata = ResultMetadata(measurement_interval_end, algorithm),
            .start_time = measurement_interval_start,
            .cumulative_cpu = cpu_delta,
            // `cumulative_background_cpu` accumulates CPU consumed while the
            // process' priority is `BEST_EFFORT`.
            .cumulative_background_cpu =
                (process_priority == base::TaskPriority::BEST_EFFORT)
                    ? cpu_delta
                    : base::TimeDelta()});
    CHECK(inserted);
  };

  record_cpu_deltas(process_node->GetResourceContext(), cumulative_cpu_delta,
                    MeasurementAlgorithm::kDirectMeasurement);
  resource_attribution::SplitResourceAmongFramesAndWorkers(
      cumulative_cpu_delta, process_node,
      [&record_cpu_deltas](const FrameNode* f, base::TimeDelta cpu_delta) {
        record_cpu_deltas(f->GetResourceContext(), cpu_delta,
                          MeasurementAlgorithm::kSplit);
      },
      [&record_cpu_deltas](const WorkerNode* w, base::TimeDelta cpu_delta) {
        record_cpu_deltas(w->GetResourceContext(), cpu_delta,
                          MeasurementAlgorithm::kSplit);
      });
}

CPUMeasurementMonitor::DeadContextResults::DeadContextResults() = default;
CPUMeasurementMonitor::DeadContextResults::~DeadContextResults() = default;
CPUMeasurementMonitor::DeadContextResults::DeadContextResults(
    DeadContextResults&&) = default;
CPUMeasurementMonitor::DeadContextResults&
CPUMeasurementMonitor::DeadContextResults::operator=(DeadContextResults&&) =
    default;

ScopedCPUTimeResult::ScopedCPUTimeResult(CPUMeasurementMonitor* monitor,
                                         const ResourceContext& context,
                                         const CPUTimeResult& result)
    : monitor_(monitor), context_(context), result_(result) {
  if (ContextIs<OriginInBrowsingInstanceContext>(context_)) {
    auto [_, inserted] = monitor_->weak_origin_results_.emplace(
        AsContext<OriginInBrowsingInstanceContext>(context_), this);
    CHECK(inserted);
  }
}

ScopedCPUTimeResult::~ScopedCPUTimeResult() {
  if (ContextIs<OriginInBrowsingInstanceContext>(context_)) {
    size_t num_erased = monitor_->weak_origin_results_.erase(
        AsContext<OriginInBrowsingInstanceContext>(context_));
    CHECK_EQ(num_erased, 1U);
  }
}

size_t ScopedCPUTimeResult::EstimateMemoryUsage() const {
  size_t size = sizeof(*this);
  if (ContextIs<OriginInBrowsingInstanceContext>(context_)) {
    // OriginInBrowsingInstanceContext includes an url::Origin, which has
    // variable-size data.
    size += AsContext<OriginInBrowsingInstanceContext>(context_)
                .GetOrigin()
                .EstimateMemoryUsage();
  }
  return size;
}

}  // namespace resource_attribution
