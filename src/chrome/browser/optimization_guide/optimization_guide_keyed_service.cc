// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/optimization_guide/optimization_guide_keyed_service.h"

#include <memory>
#include <optional>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/system/sys_info.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/component_updater/optimization_guide_on_device_model_installer.h"
#include "chrome/browser/download/background_download_service_factory.h"
#include "chrome/browser/metrics/chrome_metrics_service_accessor.h"
#include "chrome/browser/optimization_guide/chrome_hints_manager.h"
#include "chrome/browser/optimization_guide/chrome_model_quality_logs_uploader_service.h"
#include "chrome/browser/optimization_guide/chrome_prediction_model_store.h"
#include "chrome/browser/optimization_guide/model_execution/chrome_on_device_model_service_controller.h"
#include "chrome/browser/optimization_guide/optimization_guide_keyed_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_key.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_paths.h"
#include "components/component_updater/component_updater_paths.h"
#include "components/component_updater/pref_names.h"
#include "components/leveldb_proto/public/proto_database_provider.h"
#include "components/metrics/metrics_log.h"
#include "components/metrics/version_utils.h"
#include "components/metrics_services_manager/metrics_services_manager.h"
#include "components/optimization_guide/core/delivery/model_util.h"
#include "components/optimization_guide/core/delivery/prediction_manager.h"
#include "components/optimization_guide/core/hints/command_line_top_host_provider.h"
#include "components/optimization_guide/core/hints/hints_processing_util.h"
#include "components/optimization_guide/core/hints/optimization_guide_navigation_data.h"
#include "components/optimization_guide/core/hints/optimization_guide_store.h"
#include "components/optimization_guide/core/hints/tab_url_provider.h"
#include "components/optimization_guide/core/hints/top_host_provider.h"
#include "components/optimization_guide/core/model_execution/feature_keys.h"
#include "components/optimization_guide/core/model_execution/model_broker_client.h"
#include "components/optimization_guide/core/model_execution/model_execution_features_controller.h"
#include "components/optimization_guide/core/model_execution/model_execution_manager.h"
#include "components/optimization_guide/core/model_execution/on_device_asset_manager.h"
#include "components/optimization_guide/core/model_execution/on_device_model_component.h"
#include "components/optimization_guide/core/model_execution/on_device_model_service_controller.h"
#include "components/optimization_guide/core/model_execution/performance_class.h"
#include "components/optimization_guide/core/model_quality/model_quality_log_entry.h"
#include "components/optimization_guide/core/model_quality/model_quality_logs_uploader_service.h"
#include "components/optimization_guide/core/model_quality/model_quality_util.h"
#include "components/optimization_guide/core/optimization_guide_constants.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/optimization_guide_logger.h"
#include "components/optimization_guide/core/optimization_guide_model_executor.h"
#include "components/optimization_guide/core/optimization_guide_prefs.h"
#include "components/optimization_guide/core/optimization_guide_switches.h"
#include "components/optimization_guide/core/optimization_guide_util.h"
#include "components/optimization_guide/proto/hints.pb.h"
#include "components/optimization_guide/proto/models.pb.h"
#include "components/prefs/pref_service.h"
#include "components/services/unzip/content/unzip_service.h"
#include "components/user_prefs/user_prefs.h"
#include "components/variations/service/variations_service.h"
#include "components/variations/synthetic_trials.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/storage_partition.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/commerce/price_tracking/android/price_tracking_notification_bridge.h"
#include "chrome/browser/optimization_guide/android/optimization_guide_bridge.h"
#include "chrome/browser/optimization_guide/android/optimization_guide_tab_url_provider_android.h"
#else
#include "chrome/browser/optimization_guide/optimization_guide_tab_url_provider.h"
#endif

namespace {

using ::optimization_guide::ModelExecutionFeaturesController;
using ::optimization_guide::OnDeviceModelComponentStateManager;
using ::optimization_guide::OnDeviceModelPerformanceClass;
using ::optimization_guide::OnDeviceModelServiceController;

// Used to override the value of `version_info::IsOfficialBuild()` for tests.
std::optional<bool> g_is_official_build_for_testing;

// Returns the profile to use for when setting up the keyed service when the
// profile is Off-The-Record. For guest profiles, returns a loaded profile if
// one exists, otherwise just the original profile of the OTR profile. Note:
// guest profiles are off-the-record and "original" profiles.
Profile* GetProfileForOTROptimizationGuide(Profile* profile) {
  DCHECK(profile);
  DCHECK(profile->IsOffTheRecord());

  if (profile->IsGuestSession()) {
    // Guest sessions need to rely on the stores from real profiles
    // as guest profiles cannot fetch or store new models. Note: only
    // loaded profiles should be used as we do not want to force load
    // another profile as that can lead to start up regressions.
    std::vector<Profile*> profiles =
        g_browser_process->profile_manager()->GetLoadedProfiles();
    if (!profiles.empty()) {
      return profiles[0];
    }
  }
  return profile->GetOriginalProfile();
}

scoped_refptr<optimization_guide::OnDeviceModelServiceController>
GetOnDeviceModelServiceController(
    base::WeakPtr<optimization_guide::OnDeviceModelComponentStateManager>
        on_device_component_manager) {
  scoped_refptr<optimization_guide::OnDeviceModelServiceController>
      service_controller = optimization_guide::
          ChromeOnDeviceModelServiceController::GetSingleInstanceMayBeNull();
  if (!service_controller) {
    service_controller = base::MakeRefCounted<
        optimization_guide::ChromeOnDeviceModelServiceController>(
        std::move(on_device_component_manager));
    service_controller->Init();
  }
  return service_controller;
}

class OnDeviceModelComponentStateManagerDelegate
    : public OnDeviceModelComponentStateManager::Delegate {
 public:
  ~OnDeviceModelComponentStateManagerDelegate() override = default;

  base::FilePath GetInstallDirectory() override {
    base::FilePath local_install_path;
    base::PathService::Get(component_updater::DIR_COMPONENT_USER,
                           &local_install_path);
    return local_install_path;
  }

  void GetFreeDiskSpace(const base::FilePath& path,
                        base::OnceCallback<void(int64_t)> callback) override {
    base::TaskTraits traits = {base::MayBlock(),
                               base::TaskPriority::BEST_EFFORT};
    if (optimization_guide::switches::
            ShouldGetFreeDiskSpaceWithUserVisiblePriorityTask()) {
      traits.UpdatePriority(base::TaskPriority::USER_VISIBLE);
    }

    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, traits,
        base::BindOnce(&base::SysInfo::AmountOfFreeDiskSpace, path),
        std::move(callback));
  }

  void RegisterInstaller(
      scoped_refptr<OnDeviceModelComponentStateManager> state_manager,
      bool is_already_installing) override {
    if (!g_browser_process) {
      return;
    }
    component_updater::RegisterOptimizationGuideOnDeviceModelComponent(
        g_browser_process->component_updater(), state_manager->GetWeakPtr(),
        is_already_installing);
  }

  void Uninstall(scoped_refptr<OnDeviceModelComponentStateManager>
                     state_manager) override {
    component_updater::UninstallOptimizationGuideOnDeviceModelComponent(
        state_manager->GetWeakPtr());
  }
};

}  // namespace

// static
std::unique_ptr<optimization_guide::PushNotificationManager>
OptimizationGuideKeyedService::MaybeCreatePushNotificationManager(
    Profile* profile) {
  if (optimization_guide::features::IsPushNotificationsEnabled()) {
    auto push_notification_manager =
        std::make_unique<optimization_guide::PushNotificationManager>();
#if BUILDFLAG(IS_ANDROID)
    push_notification_manager->AddObserver(
        PriceTrackingNotificationBridge::GetForBrowserContext(profile));
#endif
    return push_notification_manager;
  }
  return nullptr;
}

// static
void OptimizationGuideKeyedService::SetIsOfficialBuildForTesting(
    bool is_official_build) {
  g_is_official_build_for_testing = is_official_build;
}

OptimizationGuideKeyedService::OptimizationGuideKeyedService(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (browser_context_) {  // Null in MockOptimizationGuideKeyedService.
    Initialize();
  }
}

OptimizationGuideKeyedService::~OptimizationGuideKeyedService() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void OptimizationGuideKeyedService::BindModelBroker(
    mojo::PendingReceiver<optimization_guide::mojom::ModelBroker> receiver) {
  if (!base::FeatureList::IsEnabled(
          optimization_guide::features::
              kBrokerModelSessionsForUntrustedProcesses)) {
    return;
  }
  if (!base::FeatureList::IsEnabled(
          optimization_guide::features::kOptimizationGuideModelExecution)) {
    return;
  }
  if (!base::FeatureList::IsEnabled(
          optimization_guide::features::kOptimizationGuideOnDeviceModel)) {
    return;
  }
  GetOnDeviceModelServiceController(on_device_component_manager_->GetWeakPtr())
      ->BindBroker(std::move(receiver));
}

std::unique_ptr<optimization_guide::ModelBrokerClient>
OptimizationGuideKeyedService::CreateModelBrokerClient() {
  mojo::PendingRemote<optimization_guide::mojom::ModelBroker> remote;
  GetOnDeviceModelServiceController(on_device_component_manager_->GetWeakPtr())
      ->BindBroker(remote.InitWithNewPipeAndPassReceiver());
  return std::make_unique<optimization_guide::ModelBrokerClient>(
      std::move(remote), optimization_guide::CreateSessionArgs(
                             optimization_guide_logger_->GetWeakPtr(), {}));
}

#if BUILDFLAG(IS_ANDROID)
base::android::ScopedJavaLocalRef<jobject>
OptimizationGuideKeyedService::GetJavaObject() {
  if (!android_bridge_) {
    android_bridge_ =
        std::make_unique<optimization_guide::android::OptimizationGuideBridge>(
            this);
  }
  return android_bridge_->GetJavaObject();
}
#endif

download::BackgroundDownloadService*
OptimizationGuideKeyedService::BackgroundDownloadServiceProvider() {
  Profile* profile = Profile::FromBrowserContext(browser_context_);
  return BackgroundDownloadServiceFactory::GetForKey(profile->GetProfileKey());
}

bool OptimizationGuideKeyedService::ComponentUpdatesEnabledProvider() const {
  return g_browser_process->local_state()->GetBoolean(
      ::prefs::kComponentUpdatesEnabled);
}

void OptimizationGuideKeyedService::Initialize() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  Profile* profile = Profile::FromBrowserContext(browser_context_);

  base::FilePath profile_path = profile->GetOriginalProfile()->GetPath();

  // We have different behavior if |this| is created for an incognito profile.
  // For incognito profiles, we act in "read-only" mode of the original
  // profile's store and do not fetch any new hints or models.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory;
  base::WeakPtr<optimization_guide::OptimizationGuideStore> hint_store;
  if (profile->IsOffTheRecord()) {
    OptimizationGuideKeyedService* original_ogks =
        OptimizationGuideKeyedServiceFactory::GetForProfile(
            GetProfileForOTROptimizationGuide(profile));
    DCHECK(original_ogks);
    hint_store = original_ogks->GetHintsManager()->hint_store();
  } else {
    // Use the database associated with the original profile.
    auto* proto_db_provider = profile->GetOriginalProfile()
                                  ->GetDefaultStoragePartition()
                                  ->GetProtoDatabaseProvider();
    url_loader_factory = profile->GetDefaultStoragePartition()
                             ->GetURLLoaderFactoryForBrowserProcess();

    // Only create a top host provider from the command line if provided.
    top_host_provider_ =
        optimization_guide::CommandLineTopHostProvider::CreateIfEnabled();

    bool optimization_guide_fetching_enabled =
        optimization_guide::IsUserPermittedToFetchFromRemoteOptimizationGuide(
            profile->IsOffTheRecord(), profile->GetPrefs());
    UMA_HISTOGRAM_BOOLEAN("OptimizationGuide.RemoteFetchingEnabled",
                          optimization_guide_fetching_enabled);
    ChromeMetricsServiceAccessor::RegisterSyntheticFieldTrial(
        "SyntheticOptimizationGuideRemoteFetching",
        optimization_guide_fetching_enabled ? "Enabled" : "Disabled",
        variations::SyntheticTrialAnnotationMode::kCurrentLog);

#if BUILDFLAG(IS_ANDROID)
    tab_url_provider_ = std::make_unique<
        optimization_guide::android::OptimizationGuideTabUrlProviderAndroid>(
        profile);
#else
    tab_url_provider_ =
        std::make_unique<OptimizationGuideTabUrlProvider>(profile);
#endif

    hint_store_ =
        optimization_guide::features::ShouldPersistHintsToDisk()
            ? std::make_unique<optimization_guide::OptimizationGuideStore>(
                  proto_db_provider,
                  profile_path.Append(
                      optimization_guide::kOptimizationGuideHintStore),
                  base::ThreadPool::CreateSequencedTaskRunner(
                      {base::MayBlock(), base::TaskPriority::BEST_EFFORT}))
            : nullptr;
    hint_store = hint_store_ ? hint_store_->AsWeakPtr() : nullptr;
  }

  optimization_guide_logger_ = OptimizationGuideLogger::GetInstance();
  DCHECK(optimization_guide_logger_);
  hints_manager_ = std::make_unique<optimization_guide::ChromeHintsManager>(
      profile, profile->GetPrefs(), hint_store, top_host_provider_.get(),
      tab_url_provider_.get(), url_loader_factory,
      MaybeCreatePushNotificationManager(profile),
      IdentityManagerFactory::GetForProfile(profile),
      optimization_guide_logger_.get());

  prediction_manager_ = std::make_unique<optimization_guide::PredictionManager>(
      optimization_guide::ChromePredictionModelStore::GetInstance(),
      url_loader_factory, profile->GetPrefs(), profile->IsOffTheRecord(),
      g_browser_process->GetApplicationLocale(),
      optimization_guide_logger_.get(),
      base::BindRepeating(
          &OptimizationGuideKeyedService::ComponentUpdatesEnabledProvider,
          // It's safe to use |base::Unretained(this)| here because
          // |this| owns |prediction_manager_|.
          base::Unretained(this)),
      base::BindRepeating(&unzip::LaunchUnzipper));

  InitializeModelExecution(profile);

  // Register for profile initialization event to initialize the model
  // downloads.
  profile_observation_.Observe(profile);

  OPTIMIZATION_GUIDE_LOG(
      optimization_guide_common::mojom::LogSource::SERVICE_AND_SETTINGS,
      optimization_guide_logger_,
      "OptimizationGuide: KeyedService is initalized");

  optimization_guide::LogFeatureFlagsInfo(optimization_guide_logger_.get(),
                                          profile->IsOffTheRecord(),
                                          profile->GetPrefs());
}

void OptimizationGuideKeyedService::InitializeModelExecution(Profile* profile) {
  if (!base::FeatureList::IsEnabled(
          optimization_guide::features::kOptimizationGuideModelExecution)) {
    return;
  }
  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  on_device_component_manager_ =
      optimization_guide::OnDeviceModelComponentStateManager::CreateOrGet(
          g_browser_process->local_state(),
          std::make_unique<OnDeviceModelComponentStateManagerDelegate>());
  on_device_component_manager_->OnStartup();

  if (!profile->IsOffTheRecord() && !profile->IsGuestSession()) {
    // With multiple profiles we only want to fetch the performance class
    // once. This bool helps avoid fetching multiple times.
    static bool performance_class_fetched = false;
    if (!performance_class_fetched &&
        (base::FeatureList::IsEnabled(
             optimization_guide::features::kLogOnDeviceMetricsOnStartup) ||
         optimization_guide::features::IsOnDeviceExecutionEnabled()) &&
        on_device_component_manager_->NeedsPerformanceClassUpdate()) {
      performance_class_fetched = true;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              &OptimizationGuideKeyedService::EnsurePerformanceClassAvailable,
              weak_factory_.GetWeakPtr(), base::DoNothing()),
          optimization_guide::features::GetOnDeviceStartupMetricDelay());
    }
    // If the perf class was previously determined, register that.
    GetOnDeviceModelServiceController(
        on_device_component_manager_->GetWeakPtr())
        ->RegisterPerformanceClassSyntheticTrial(
            optimization_guide::PerformanceClassFromPref(
                *g_browser_process->local_state()));

    auto* variations_service = g_browser_process->variations_service();
    auto dogfood_status =
        variations_service && variations_service->IsLikelyDogfoodClient()
            ? ModelExecutionFeaturesController::DogfoodStatus::DOGFOOD
            : ModelExecutionFeaturesController::DogfoodStatus::NON_DOGFOOD;
    bool is_official_build = g_is_official_build_for_testing.value_or(
        version_info::IsOfficialBuild());
    model_execution_features_controller_ =
        std::make_unique<optimization_guide::ModelExecutionFeaturesController>(
            profile->GetPrefs(), IdentityManagerFactory::GetForProfile(profile),
            g_browser_process->local_state(), dogfood_status,
            is_official_build);

    // Don't create logs uploader service when feature is disabled. All the
    // logs upload get route through this service which exists one per
    // profile.
    if (base::FeatureList::IsEnabled(
            optimization_guide::features::kModelQualityLogging)) {
      model_quality_logs_uploader_service_ = std::make_unique<
          optimization_guide::ChromeModelQualityLogsUploaderService>(
          url_loader_factory, g_browser_process->local_state(),
          model_execution_features_controller_
              ? model_execution_features_controller_->GetWeakPtr()
              : nullptr);
    }
    RecordModelExecutionFeatureSyntheticFieldTrial(
        optimization_guide::UserVisibleFeatureKey::kHistorySearch,
        "HistorySearch");
  }

  scoped_refptr<optimization_guide::OnDeviceModelServiceController>
      service_controller;
  if (base::FeatureList::IsEnabled(
          optimization_guide::features::kOptimizationGuideOnDeviceModel)) {
    service_controller = GetOnDeviceModelServiceController(
        on_device_component_manager_->GetWeakPtr());
    on_device_asset_manager_ =
        std::make_unique<optimization_guide::OnDeviceAssetManager>(
            g_browser_process->local_state(), service_controller->GetWeakPtr(),
            on_device_component_manager_->GetWeakPtr(), this);
  }

  model_execution_manager_ =
      std::make_unique<optimization_guide::ModelExecutionManager>(
          url_loader_factory, IdentityManagerFactory::GetForProfile(profile),
          std::move(service_controller), optimization_guide_logger_.get(),
          model_quality_logs_uploader_service_
              ? model_quality_logs_uploader_service_->GetWeakPtr()
              : nullptr);
}

optimization_guide::ChromeHintsManager*
OptimizationGuideKeyedService::GetHintsManager() {
  return hints_manager_.get();
}

void OptimizationGuideKeyedService::OnNavigationStartOrRedirect(
    OptimizationGuideNavigationData* navigation_data) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  base::flat_set<optimization_guide::proto::OptimizationType>
      registered_optimization_types =
          hints_manager_->registered_optimization_types();
  if (!registered_optimization_types.empty()) {
    hints_manager_->OnNavigationStartOrRedirect(navigation_data,
                                                base::DoNothing());
  }

  if (navigation_data) {
    navigation_data->set_registered_optimization_types(
        hints_manager_->registered_optimization_types());
    navigation_data->set_registered_optimization_targets(
        prediction_manager_->GetRegisteredOptimizationTargets());
  }
}

void OptimizationGuideKeyedService::OnNavigationFinish(
    const std::vector<GURL>& navigation_redirect_chain) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  hints_manager_->OnNavigationFinish(navigation_redirect_chain);
}

void OptimizationGuideKeyedService::AddObserverForOptimizationTargetModel(
    optimization_guide::proto::OptimizationTarget optimization_target,
    const std::optional<optimization_guide::proto::Any>& model_metadata,
    optimization_guide::OptimizationTargetModelObserver* observer) {
  prediction_manager_->AddObserverForOptimizationTargetModel(
      optimization_target, model_metadata, observer);
}

void OptimizationGuideKeyedService::RemoveObserverForOptimizationTargetModel(
    optimization_guide::proto::OptimizationTarget optimization_target,
    optimization_guide::OptimizationTargetModelObserver* observer) {
  prediction_manager_->RemoveObserverForOptimizationTargetModel(
      optimization_target, observer);
}

void OptimizationGuideKeyedService::RegisterOptimizationTypes(
    const std::vector<optimization_guide::proto::OptimizationType>&
        optimization_types) {
  hints_manager_->RegisterOptimizationTypes(optimization_types);
}

optimization_guide::OptimizationGuideDecision
OptimizationGuideKeyedService::CanApplyOptimization(
    const GURL& url,
    optimization_guide::proto::OptimizationType optimization_type,
    optimization_guide::OptimizationMetadata* optimization_metadata) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  optimization_guide::OptimizationTypeDecision optimization_type_decision =
      hints_manager_->CanApplyOptimization(url, optimization_type,
                                           optimization_metadata);
  base::UmaHistogramEnumeration(
      "OptimizationGuide.ApplyDecision." +
          optimization_guide::GetStringNameForOptimizationType(
              optimization_type),
      optimization_type_decision);
  return optimization_guide::ChromeHintsManager::
      GetOptimizationGuideDecisionFromOptimizationTypeDecision(
          optimization_type_decision);
}

void OptimizationGuideKeyedService::CanApplyOptimization(
    const GURL& url,
    optimization_guide::proto::OptimizationType optimization_type,
    optimization_guide::OptimizationGuideDecisionCallback callback) {
  hints_manager_->CanApplyOptimization(url, optimization_type,
                                       std::move(callback));
}

void OptimizationGuideKeyedService::CanApplyOptimizationOnDemand(
    const std::vector<GURL>& urls,
    const base::flat_set<optimization_guide::proto::OptimizationType>&
        optimization_types,
    optimization_guide::proto::RequestContext request_context,
    optimization_guide::OnDemandOptimizationGuideDecisionRepeatingCallback
        callback,
    std::optional<optimization_guide::proto::RequestContextMetadata>
        request_context_metadata) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(request_context !=
         optimization_guide::proto::RequestContext::CONTEXT_UNSPECIFIED);

  hints_manager_->CanApplyOptimizationOnDemand(urls, optimization_types,
                                               request_context, callback,
                                               request_context_metadata);
}

std::unique_ptr<optimization_guide::OptimizationGuideModelExecutor::Session>
OptimizationGuideKeyedService::StartSession(
    optimization_guide::ModelBasedCapabilityKey feature,
    const std::optional<optimization_guide::SessionConfigParams>&
        config_params) {
  if (!model_execution_manager_) {
    return nullptr;
  }
  return model_execution_manager_->StartSession(feature, config_params);
}

void OptimizationGuideKeyedService::ExecuteModel(
    optimization_guide::ModelBasedCapabilityKey feature,
    const google::protobuf::MessageLite& request_metadata,
    const std::optional<base::TimeDelta>& execution_timeout,
    optimization_guide::OptimizationGuideModelExecutionResultCallback
        callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!model_execution_manager_) {
    std::move(callback).Run(
        optimization_guide::OptimizationGuideModelExecutionResult(
            base::unexpected(
                optimization_guide::OptimizationGuideModelExecutionError::
                    FromModelExecutionError(
                        optimization_guide::
                            OptimizationGuideModelExecutionError::
                                ModelExecutionError::kGenericFailure)),
            nullptr),
        nullptr);
    return;
  }
  model_execution_manager_->ExecuteModel(
      feature, request_metadata, execution_timeout,
      /*log_ai_data_request=*/nullptr, std::move(callback));
}

void OptimizationGuideKeyedService::AddOnDeviceModelAvailabilityChangeObserver(
    optimization_guide::ModelBasedCapabilityKey feature,
    optimization_guide::OnDeviceModelAvailabilityObserver* observer) {
  if (!on_device_component_manager_) {
    return;
  }
  auto service_controller = GetOnDeviceModelServiceController(
      on_device_component_manager_->GetWeakPtr());
  if (service_controller) {
    service_controller->AddOnDeviceModelAvailabilityChangeObserver(feature,
                                                                   observer);
  }
}

void OptimizationGuideKeyedService::
    RemoveOnDeviceModelAvailabilityChangeObserver(
        optimization_guide::ModelBasedCapabilityKey feature,
        optimization_guide::OnDeviceModelAvailabilityObserver* observer) {
  if (!on_device_component_manager_) {
    return;
  }
  auto service_controller = GetOnDeviceModelServiceController(
      on_device_component_manager_->GetWeakPtr());
  if (service_controller) {
    service_controller->RemoveOnDeviceModelAvailabilityChangeObserver(feature,
                                                                      observer);
  }
}

on_device_model::Capabilities
OptimizationGuideKeyedService::GetOnDeviceCapabilities() {
  if (!model_execution_manager_) {
    return {};
  }
  auto capabilities = model_execution_manager_->GetOnDeviceCapabilities();
  capabilities.RetainAll(GetPossibleOnDeviceCapabilities());
  return capabilities;
}

void OptimizationGuideKeyedService::OnProfileInitializationComplete(
    Profile* profile) {
  DCHECK(profile_observation_.IsObservingSource(profile));
  profile_observation_.Reset();

  if (profile->IsOffTheRecord()) {
    return;
  }

  GetPredictionManager()->MaybeInitializeModelDownloads(
      BackgroundDownloadServiceProvider());
}

void OptimizationGuideKeyedService::AddHintForTesting(
    const GURL& url,
    optimization_guide::proto::OptimizationType optimization_type,
    const std::optional<optimization_guide::OptimizationMetadata>& metadata) {
  hints_manager_->AddHintForTesting(url, optimization_type, metadata);
}

void OptimizationGuideKeyedService::AddOnDemandHintForTesting(
    const GURL& url,
    optimization_guide::proto::OptimizationType optimization_type,
    const optimization_guide::OptimizationGuideDecisionWithMetadata& decision) {
  hints_manager_->AddOnDemandHintForTesting(url, optimization_type,  // IN-TEST
                                            decision);
}

void OptimizationGuideKeyedService::AddExecutionResultForTesting(
    optimization_guide::ModelBasedCapabilityKey feature,
    optimization_guide::OptimizationGuideModelExecutionResult result) {
  model_execution_manager_->AddExecutionResultForTesting(feature,  // IN-TEST
                                                         std::move(result));
}

void OptimizationGuideKeyedService::ClearData() {
  hints_manager_->ClearFetchedHints();
}

void OptimizationGuideKeyedService::Shutdown() {
  hints_manager_->Shutdown();
  if (model_execution_manager_) {
    model_execution_manager_->Shutdown();
  }
}

void OptimizationGuideKeyedService::OverrideTargetModelForTesting(
    optimization_guide::proto::OptimizationTarget optimization_target,
    std::unique_ptr<optimization_guide::ModelInfo> model_info) {
  prediction_manager_->OverrideTargetModelForTesting(  // IN-TEST
      optimization_target, std::move(model_info));
}

void OptimizationGuideKeyedService::
    SetModelQualityLogsUploaderServiceForTesting(
        std::unique_ptr<optimization_guide::ModelQualityLogsUploaderService>
            uploader) {
  model_quality_logs_uploader_service_ = std::move(uploader);
}

optimization_guide::ModelExecutionFeaturesController*
OptimizationGuideKeyedService::GetModelExecutionFeaturesController() {
  return model_execution_features_controller_.get();
}

void OptimizationGuideKeyedService::AllowUnsignedUserForTesting(
    optimization_guide::UserVisibleFeatureKey feature) {
  model_execution_features_controller_->AllowUnsignedUserForTesting(
      feature);  // IN-TEST
}

bool OptimizationGuideKeyedService::ShouldFeatureBeCurrentlyEnabledForUser(
    optimization_guide::UserVisibleFeatureKey feature) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!model_execution_features_controller_) {
    return false;
  }
  return model_execution_features_controller_
      ->ShouldFeatureBeCurrentlyEnabledForUser(feature);
}

bool OptimizationGuideKeyedService::
    ShouldFeatureAllowModelExecutionForSignedInUser(
        optimization_guide::UserVisibleFeatureKey feature) const {
  if (!model_execution_features_controller_) {
    return false;
  }
  return model_execution_features_controller_
      ->ShouldFeatureAllowModelExecutionForSignedInUser(feature);
}

bool OptimizationGuideKeyedService::ShouldFeatureBeCurrentlyAllowedForFeedback(
    optimization_guide::proto::LogAiDataRequest::FeatureCase feature) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // If logging is enabled, feedback is always also enabled.
  const optimization_guide::MqlsFeatureMetadata* metadata =
      optimization_guide::MqlsFeatureRegistry::GetInstance().GetFeature(
          feature);
  DCHECK(metadata);
  if (model_execution_features_controller_ &&
      model_execution_features_controller_
          ->ShouldFeatureBeCurrentlyAllowedForLogging(metadata)) {
    return true;
  }

  // Otherwise, feedback is disabled, with one exception: On dogfood clients,
  // feedback is always enabled (as long as the feature is enabled).
  auto* variations_service = g_browser_process->variations_service();
  return !!variations_service && variations_service->IsLikelyDogfoodClient();
}

bool OptimizationGuideKeyedService::ShouldModelExecutionBeAllowedForUser()
    const {
  return model_execution_features_controller_ &&
         model_execution_features_controller_
             ->ShouldModelExecutionBeAllowedForUser();
}

bool OptimizationGuideKeyedService::IsSettingVisible(
    optimization_guide::UserVisibleFeatureKey feature) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!model_execution_features_controller_) {
    return false;
  }

#if !BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(
          optimization_guide::features::kAiSettingsPageForceAvailable)) {
    return true;
  }
#endif

  return model_execution_features_controller_->IsSettingVisible(feature);
}

void OptimizationGuideKeyedService::AddModelExecutionSettingsEnabledObserver(
    optimization_guide::SettingsEnabledObserver* observer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!model_execution_features_controller_) {
    return;
  }
  model_execution_features_controller_->AddObserver(observer);
}

void OptimizationGuideKeyedService::RemoveModelExecutionSettingsEnabledObserver(
    optimization_guide::SettingsEnabledObserver* observer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!model_execution_features_controller_) {
    return;
  }
  model_execution_features_controller_->RemoveObserver(observer);
}

void OptimizationGuideKeyedService::
    RecordModelExecutionFeatureSyntheticFieldTrial(
        optimization_guide::UserVisibleFeatureKey feature,
        std::string_view feature_name) {
  ChromeMetricsServiceAccessor::RegisterSyntheticFieldTrial(
      base::StrCat({"SyntheticModelExecutionFeature", feature_name}),
      ShouldFeatureBeCurrentlyEnabledForUser(feature) ? "Enabled" : "Disabled",
      variations::SyntheticTrialAnnotationMode::kCurrentLog);
}

optimization_guide::OnDeviceModelEligibilityReason
OptimizationGuideKeyedService::GetOnDeviceModelEligibility(
    optimization_guide::ModelBasedCapabilityKey feature) {
  if (!model_execution_manager_) {
    return optimization_guide::OnDeviceModelEligibilityReason::
        kFeatureNotEnabled;
  }

  return model_execution_manager_->GetOnDeviceModelEligibility(feature);
}

void OptimizationGuideKeyedService::GetOnDeviceModelEligibilityAsync(
    optimization_guide::ModelBasedCapabilityKey feature,
    const on_device_model::Capabilities& capabilities,
    base::OnceCallback<void(optimization_guide::OnDeviceModelEligibilityReason)>
        callback) {
  EnsurePerformanceClassAvailable(base::BindOnce(
      &OptimizationGuideKeyedService::FinishGetOnDeviceModelEligibility,
      weak_factory_.GetWeakPtr(), feature, capabilities, std::move(callback)));
}

std::optional<optimization_guide::SamplingParamsConfig>
OptimizationGuideKeyedService::GetSamplingParamsConfig(
    optimization_guide::ModelBasedCapabilityKey feature) {
  if (!model_execution_manager_) {
    return std::nullopt;
  }

  return model_execution_manager_->GetSamplingParamsConfig(feature);
}

std::optional<const optimization_guide::proto::Any>
OptimizationGuideKeyedService::GetFeatureMetadata(
    optimization_guide::ModelBasedCapabilityKey feature) {
  if (!model_execution_manager_) {
    return std::nullopt;
  }

  return model_execution_manager_->GetFeatureMetadata(feature);
}

void OptimizationGuideKeyedService::EnsurePerformanceClassAvailable(
    base::OnceClosure complete) {
  GetOnDeviceModelServiceController(on_device_component_manager_->GetWeakPtr())
      ->EnsurePerformanceClassAvailable(std::move(complete));
}

void OptimizationGuideKeyedService::FinishGetOnDeviceModelEligibility(
    optimization_guide::ModelBasedCapabilityKey feature,
    const on_device_model::Capabilities& capabilities,
    base::OnceCallback<void(optimization_guide::OnDeviceModelEligibilityReason)>
        callback) {
  // If this device will never support the requested capabilities, return not
  // available.
  if (!GetPossibleOnDeviceCapabilities().HasAll(capabilities)) {
    std::move(callback).Run(optimization_guide::OnDeviceModelEligibilityReason::
                                kModelAdaptationNotAvailable);
    return;
  }
  std::move(callback).Run(GetOnDeviceModelEligibility(feature));
}

on_device_model::Capabilities
OptimizationGuideKeyedService::GetPossibleOnDeviceCapabilities() const {
  if (!on_device_component_manager_) {
    return {};
  }
  on_device_model::Capabilities capabilities;
  if (on_device_component_manager_->SupportsImageInput()) {
    capabilities.Put(on_device_model::CapabilityFlags::kImageInput);
  }
  if (on_device_component_manager_->SupportsAudioInput()) {
    capabilities.Put(on_device_model::CapabilityFlags::kAudioInput);
  }
  return capabilities;
}
