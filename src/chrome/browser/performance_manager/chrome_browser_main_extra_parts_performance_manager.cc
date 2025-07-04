// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/public/chrome_browser_main_extra_parts_performance_manager.h"

#include <memory>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/memory/weak_ptr.h"
#include "base/power_monitor/battery_state_sampler.h"
#include "base/power_monitor/power_monitor_buildflags.h"
#include "base/system/sys_info.h"
#include "base/time/default_tick_clock.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/memory/enterprise_memory_limit_pref_observer.h"
#include "chrome/browser/performance_manager/decorators/helpers/page_live_state_decorator_helper.h"
#include "chrome/browser/performance_manager/execution_context_priority/side_panel_loading_voter.h"
#include "chrome/browser/performance_manager/metrics/metrics_provider_desktop.h"
#include "chrome/browser/performance_manager/observers/page_load_metrics_observer.h"
#include "chrome/browser/performance_manager/policies/background_tab_loading_policy.h"
#include "chrome/browser/performance_manager/policies/frame_throttling_policy.h"
#include "chrome/browser/performance_manager/policies/freezing_opt_out_checker.h"
#include "chrome/browser/performance_manager/policies/keep_alive_dse_policy.h"
#include "chrome/browser/performance_manager/policies/policy_features.h"
#include "chrome/browser/performance_manager/policies/termination_target_policy.h"
#include "chrome/browser/performance_manager/policies/working_set_trimmer_policy.h"
#include "chrome/browser/performance_manager/user_tuning/profile_discard_opt_out_list_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "components/performance_manager/decorators/page_aggregator.h"
#include "components/performance_manager/embedder/graph_features.h"
#include "components/performance_manager/embedder/performance_manager_lifetime.h"
#include "components/performance_manager/embedder/performance_manager_registry.h"
#include "components/performance_manager/embedder/scoped_global_scenario_memory.h"
#include "components/performance_manager/graph/policies/bfcache_policy.h"
#include "components/performance_manager/graph/policies/process_priority_policy.h"
#include "components/performance_manager/performance_manager_feature_observer_client.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "components/performance_manager/public/decorators/page_load_tracker_decorator_helper.h"
#include "components/performance_manager/public/decorators/process_metrics_decorator.h"
#include "components/performance_manager/public/execution_context_priority/priority_voting_system.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/metrics/page_resource_monitor.h"
#include "components/performance_manager/public/user_tuning/tab_revisit_tracker.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_features.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "base/allocator/buildflags.h"
#include "chrome/browser/performance_manager/policies/oom_score_policy_chromeos.h"
#include "chrome/browser/performance_manager/policies/report_page_processes_policy.h"
#include "chromeos/dbus/power/power_manager_client.h"
#include "components/performance_manager/power/battery_level_provider_chromeos.h"
#include "components/performance_manager/power/dbus_power_manager_sampling_event_source.h"

#if defined(ARCH_CPU_X86_64)
#include "chrome/browser/performance_manager/policies/userspace_swap_policy_chromeos.h"
#endif  // defined(ARCH_CPU_X86_64)

#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/performance_manager/extension_watcher.h"
#endif

#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/flags/android/chrome_feature_list.h"
#include "chrome/browser/performance_manager/policies/process_rank_policy_android.h"
#else
#include "chrome/browser/performance_manager/policies/memory_saver_mode_policy.h"
#include "chrome/browser/performance_manager/policies/page_discarding_helper.h"
#include "chrome/browser/performance_manager/policies/urgent_page_discarding_policy.h"
#include "chrome/browser/performance_manager/public/user_tuning/battery_saver_mode_manager.h"
#include "chrome/browser/performance_manager/public/user_tuning/performance_detection_manager.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "chrome/browser/performance_manager/user_tuning/user_performance_tuning_notifier.h"
#include "chrome/browser/tab_contents/form_interaction_tab_helper.h"
#include "components/performance_manager/freezing/freezer.h"
#include "components/performance_manager/freezing/freezing_policy.h"
#include "components/performance_manager/public/freezing/freezing.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN)
#include "base/path_service.h"
#endif

namespace {

ChromeBrowserMainExtraPartsPerformanceManager* g_instance = nullptr;

#if !BUILDFLAG(IS_ANDROID)
// Glue between the `PageDiscardingHelper` which is in
// //chrome/browser/performance_manager/ and the `FreezingPolicy` which is in
// `//components/performance_manager/`.
//
// TODO(crbug.com/347770670): This can be removed when discarding has fully
// transitioned from a //chrome concept to a //content concept.
class FreezingDiscarder : public performance_manager::freezing::Discarder {
 public:
  FreezingDiscarder() = default;
  ~FreezingDiscarder() override = default;

  // performance_manager::freezing::Discarder:
  void DiscardPages(
      performance_manager::Graph* graph,
      std::vector<const performance_manager::PageNode*> page_nodes) override {
    auto* const helper =
        performance_manager::policies::PageDiscardingHelper::GetFromGraph(
            graph);
    CHECK(helper);
    helper->ImmediatelyDiscardMultiplePages(
        page_nodes,
        ::mojom::LifecycleUnitDiscardReason::FROZEN_WITH_GROWING_MEMORY);
  }
};

#endif  // !BUILDFLAG(IS_ANDROID)

}  // namespace

ChromeBrowserMainExtraPartsPerformanceManager::
    ChromeBrowserMainExtraPartsPerformanceManager()
    : feature_observer_client_(
          std::make_unique<
              performance_manager::PerformanceManagerFeatureObserverClient>()),
      global_performance_scenario_memory_(
          std::make_unique<performance_manager::ScopedGlobalScenarioMemory>()) {
  DCHECK(!g_instance);
  g_instance = this;
}

ChromeBrowserMainExtraPartsPerformanceManager::
    ~ChromeBrowserMainExtraPartsPerformanceManager() {
  DCHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

// static
ChromeBrowserMainExtraPartsPerformanceManager*
ChromeBrowserMainExtraPartsPerformanceManager::GetInstance() {
  return g_instance;
}

// static
void ChromeBrowserMainExtraPartsPerformanceManager::CreatePoliciesAndDecorators(
    performance_manager::Graph* graph) {
  graph->PassToGraph(
      std::make_unique<performance_manager::ProcessMetricsDecorator>());
  graph->PassToGraph(
      std::make_unique<performance_manager::PageLiveStateDecorator>());
  graph->PassToGraph(
      std::make_unique<performance_manager::TabRevisitTracker>());

  if (performance_manager::policies::WorkingSetTrimmerPolicy::
          PlatformSupportsWorkingSetTrim()) {
    graph->PassToGraph(performance_manager::policies::WorkingSetTrimmerPolicy::
                           CreatePolicyForPlatform());
  }

#if BUILDFLAG(IS_CHROMEOS)
#if defined(ARCH_CPU_X86_64)
  if (performance_manager::policies::UserspaceSwapPolicy::
          UserspaceSwapSupportedAndEnabled()) {
    graph->PassToGraph(
        std::make_unique<performance_manager::policies::UserspaceSwapPolicy>());
  }
#endif  // defined(ARCH_CPU_X86_64)

  graph->PassToGraph(std::make_unique<
                     performance_manager::policies::OomScorePolicyChromeOS>());
  graph->PassToGraph(
      std::make_unique<
          performance_manager::policies::ReportPageProcessesPolicy>());
#endif  // BUILDFLAG(IS_CHROMEOS)

  using performance_manager::policies::DiscardEligibilityPolicy;
  auto discard_eligibility_policy =
      std::make_unique<DiscardEligibilityPolicy>();
  auto weak_discard_eligibility_policy =
      discard_eligibility_policy->GetWeakPtr();
  graph->PassToGraph(std::move(discard_eligibility_policy));

#if BUILDFLAG(IS_WIN)
  if (base::FeatureList::IsEnabled(
          performance_manager::features::kTerminationTargetPolicy)) {
    graph->PassToGraph(
        std::make_unique<performance_manager::TerminationTargetPolicy>());
  }
#endif  // BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_ANDROID)
  using performance_manager::policies::FreezingOptOutChecker;

  graph->PassToGraph(FormInteractionTabHelper::CreateGraphObserver());

  graph->PassToGraph(
      std::make_unique<performance_manager::policies::PageDiscardingHelper>());

#if URGENT_DISCARDING_FROM_PERFORMANCE_MANAGER()
  graph->PassToGraph(
      std::make_unique<
          performance_manager::policies::UrgentPageDiscardingPolicy>());
#endif  // URGENT_DISCARDING_FROM_PERFORMANCE_MANAGER()

  if (base::FeatureList::IsEnabled(
          performance_manager::features::
              kBackgroundTabLoadingFromPerformanceManager)) {
    graph->PassToGraph(
        std::make_unique<
            performance_manager::policies::BackgroundTabLoadingPolicy>(
            base::BindRepeating([]() {
              content::GetUIThreadTaskRunner({})->PostTask(
                  FROM_HERE,
                  base::BindOnce(
                      &SessionRestore::OnTabLoaderFinishedLoadingTabs));
            })));
  }

  // The freezing policy isn't enabled on Android yet as it doesn't play well
  // with the freezing logic already in place in renderers. This logic should be
  // moved to PerformanceManager, this is tracked in https://crbug.com/1156803.
  std::unique_ptr<FreezingOptOutChecker> freezing_opt_out_checker;
  if (base::FeatureList::IsEnabled(
          performance_manager::features::kFreezingFollowsDiscardOptOut)) {
    freezing_opt_out_checker = std::make_unique<FreezingOptOutChecker>(
        weak_discard_eligibility_policy);
  }
  graph->PassToGraph(std::make_unique<performance_manager::FreezingPolicy>(
      std::make_unique<FreezingDiscarder>(),
      std::move(freezing_opt_out_checker)));

  graph->PassToGraph(
      std::make_unique<performance_manager::policies::MemorySaverModePolicy>());
#endif  // !BUILDFLAG(IS_ANDROID)

  graph->PassToGraph(
      std::make_unique<performance_manager::metrics::PageResourceMonitor>());

  if (base::FeatureList::IsEnabled(
          performance_manager::features::kThrottleUnimportantFrameRate)) {
    graph->PassToGraph(std::make_unique<
                       performance_manager::policies::FrameThrottlingPolicy>());
  }

  if (base::FeatureList::IsEnabled(
          performance_manager::features::kBFCachePerformanceManagerPolicy)) {
    graph->PassToGraph(
        std::make_unique<performance_manager::policies::BFCachePolicy>());
  }

#if !BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(
          performance_manager::features::kPMProcessPriorityPolicy)) {
    graph->PassToGraph(std::make_unique<
                       performance_manager::policies::ProcessPriorityPolicy>());
  }
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(
          chrome::android::kProcessRankPolicyAndroid)) {
    graph->PassToGraph(
        std::make_unique<
            performance_manager::policies::ProcessRankPolicyAndroid>());
  }
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
  if (auto* voting_system = graph->GetRegisteredObjectAs<
                            performance_manager::execution_context_priority::
                                PriorityVotingSystem>()) {
    // Ensures the contents of a Side Panel loads at a high priority, even when
    // it is not visible.
    voting_system
        ->AddPriorityVoter<performance_manager::execution_context_priority::
                               SidePanelLoadingVoter>();
  }
#endif  // !BUILDFLAG(IS_ANDROID)

  if (base::FeatureList::IsEnabled(performance_manager::features::
                                       kKeepDefaultSearchEngineRendererAlive)) {
    graph->PassToGraph(
        std::make_unique<performance_manager::policies::KeepAliveDSEPolicy>());
  }
}

content::FeatureObserverClient*
ChromeBrowserMainExtraPartsPerformanceManager::GetFeatureObserverClient() {
  return feature_observer_client_.get();
}

void ChromeBrowserMainExtraPartsPerformanceManager::PostCreateThreads() {
  performance_manager_lifetime_ =
      std::make_unique<performance_manager::PerformanceManagerLifetime>(
          performance_manager::GraphFeatures::WithDefault(),
          base::BindOnce(&ChromeBrowserMainExtraPartsPerformanceManager::
                             CreatePoliciesAndDecorators));

  // There are no existing loaded profiles.
  DCHECK(g_browser_process->profile_manager()->GetLoadedProfiles().empty());

  g_browser_process->profile_manager()->AddObserver(this);

#if BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(chrome::android::kProtectedTabsAndroid)) {
    // performance_manager::policies::DiscardEligibilityPolicy requires
    // performance_manager::user_tuning::ProfileDiscardOptOutListHelper.
    profile_discard_opt_out_list_helper_ = std::make_unique<
        performance_manager::user_tuning::ProfileDiscardOptOutListHelper>();
  }
#else
  profile_discard_opt_out_list_helper_ = std::make_unique<
      performance_manager::user_tuning::ProfileDiscardOptOutListHelper>();
  // Create the UserPerformanceTuningManager and BatterySaverMode here so that
  // early UI code can register observers, but only start them in
  // PreMainMessageLoopRun because they require other systems like the
  // HostFrameSinkManager to exist.
  uint64_t system_memory_kb = base::SysInfo::AmountOfPhysicalMemory() / 1024;
  user_performance_tuning_manager_ = base::WrapUnique(
      new performance_manager::user_tuning::UserPerformanceTuningManager(
          g_browser_process->local_state(),
          std::make_unique<
              performance_manager::user_tuning::UserPerformanceTuningNotifier>(
              base::WrapUnique(new performance_manager::user_tuning::
                                   UserPerformanceTuningManager::
                                       UserPerformanceTuningReceiverImpl),
              /*resident_set_threshold_kb=*/system_memory_kb *
                  performance_manager::user_tuning::
                      UserPerformanceTuningNotifier::
                          kMemoryPercentThresholdForPromo /
                  100,
              /*tab_count_threshold=*/
              performance_manager::user_tuning::UserPerformanceTuningNotifier::
                  kTabCountThresholdForPromo)));
  battery_saver_mode_manager_ = base::WrapUnique(
      new performance_manager::user_tuning::BatterySaverModeManager(
          g_browser_process->local_state()));

  performance_detection_manager_ = base::WrapUnique(
      new performance_manager::user_tuning::PerformanceDetectionManager());
#endif

  page_load_metrics_observer_ =
      std::make_unique<performance_manager::PageLoadMetricsObserver>();
  page_live_state_data_helper_ =
      std::make_unique<performance_manager::PageLiveStateDecoratorHelper>();
  page_load_tracker_decorator_helper_ =
      std::make_unique<performance_manager::PageLoadTrackerDecoratorHelper>();
#if BUILDFLAG(ENABLE_EXTENSIONS)
  extension_watcher_ =
      std::make_unique<performance_manager::ExtensionWatcher>();
#endif

  // Some browser tests need to control how the battery state behaves, so they
  // install a test `BatteryStateSampler` before browser setup.
  if (!base::BatteryStateSampler::HasTestingInstance()) {
    // The ChromeOS `BatteryLevelProvider` and `SamplingEventSource`
    // implementations are in `components` for dependency reasons, so they need
    // to be created here and passed in explicitly to `BatteryStateSampler`.
    // TODO(crbug.com/40871810): All of the battery level machinery should be in
    // the same location, and the ifdefs should be contained to the
    // `BatteryLevelProvider` and SamplingEventSource` instantiation functions.
#if BUILDFLAG(IS_CHROMEOS)
    battery_state_sampler_ = std::make_unique<base::BatteryStateSampler>(
        std::make_unique<
            performance_manager::power::DbusPowerManagerSamplingEventSource>(
            chromeos::PowerManagerClient::Get()),
        std::make_unique<
            performance_manager::power::BatteryLevelProviderChromeOS>(
            chromeos::PowerManagerClient::Get()));
#elif BUILDFLAG(HAS_BATTERY_LEVEL_PROVIDER_IMPL)
    battery_state_sampler_ = std::make_unique<base::BatteryStateSampler>();
#endif
  }
}

void ChromeBrowserMainExtraPartsPerformanceManager::PostBrowserStart() {
  // The MemoryPressureMonitor might not be available in some tests.
  if (base::MemoryPressureMonitor::Get()) {
    if (memory::EnterpriseMemoryLimitPrefObserver::PlatformIsSupported()) {
      memory_limit_pref_observer_ =
          std::make_unique<memory::EnterpriseMemoryLimitPrefObserver>(
              g_browser_process->local_state());
    }
  }
}

void ChromeBrowserMainExtraPartsPerformanceManager::PreMainMessageLoopRun() {
#if !BUILDFLAG(IS_ANDROID)
  // This object requires the host frame sink manager to exist, which is
  // created after all the extra parts have run their PostCreateThreads.
  performance_manager::user_tuning::BatterySaverModeManager::GetInstance()
      ->Start();
  performance_manager::user_tuning::UserPerformanceTuningManager::GetInstance()
      ->Start();

  // This object is created by the metrics service before threads, but it
  // needs the UserPerformanceTuningManager to exist. At this point it's
  // instantiated, but still needs to be initialized.
  performance_manager::MetricsProviderDesktop::GetInstance()->Initialize();
#endif
}

void ChromeBrowserMainExtraPartsPerformanceManager::PostMainMessageLoopRun() {
  // |memory_limit_pref_observer_| must be destroyed before its |pref_service_|
  // is destroyed, as the observer's PrefChangeRegistrar's destructor uses the
  // pref_service.
  memory_limit_pref_observer_.reset();

  g_browser_process->profile_manager()->RemoveObserver(this);
  profile_observations_.RemoveAllObservations();

#if BUILDFLAG(ENABLE_EXTENSIONS)
  extension_watcher_.reset();
#endif
  page_load_tracker_decorator_helper_.reset();
  page_live_state_data_helper_.reset();
  page_load_metrics_observer_.reset();

#if BUILDFLAG(IS_ANDROID)
  profile_discard_opt_out_list_helper_.reset();
#else
  battery_saver_mode_manager_.reset();
  user_performance_tuning_manager_.reset();
  performance_detection_manager_.reset();
  profile_discard_opt_out_list_helper_.reset();

  if (battery_state_sampler_)
    battery_state_sampler_->Shutdown();
#endif

  // Releasing `performance_manager_lifetime_` will tear down the registry and
  // graph safely.
  performance_manager_lifetime_.reset();
}

void ChromeBrowserMainExtraPartsPerformanceManager::OnProfileAdded(
    Profile* profile) {
  profile_observations_.AddObservation(profile);
  performance_manager::PerformanceManagerRegistry::GetInstance()
      ->NotifyBrowserContextAdded(profile);

#if BUILDFLAG(IS_ANDROID)
  if (profile_discard_opt_out_list_helper_) {
    profile_discard_opt_out_list_helper_->OnProfileAdded(profile);
  }
#else
  profile_discard_opt_out_list_helper_->OnProfileAdded(profile);
#endif
}

void ChromeBrowserMainExtraPartsPerformanceManager::
    OnOffTheRecordProfileCreated(Profile* off_the_record) {
  OnProfileAdded(off_the_record);
}

void ChromeBrowserMainExtraPartsPerformanceManager::OnProfileWillBeDestroyed(
    Profile* profile) {
  profile_observations_.RemoveObservation(profile);
  performance_manager::PerformanceManagerRegistry::GetInstance()
      ->NotifyBrowserContextRemoved(profile);

#if BUILDFLAG(IS_ANDROID)
  if (profile_discard_opt_out_list_helper_) {
    profile_discard_opt_out_list_helper_->OnProfileWillBeRemoved(profile);
  }
#else
  profile_discard_opt_out_list_helper_->OnProfileWillBeRemoved(profile);
#endif
}
