// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "components/metrics/metrics_state_manager.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "base/base_switches.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/debug/leak_annotations.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "components/metrics/cloned_install_detector.h"
#include "components/metrics/enabled_state_provider.h"
#include "components/metrics/entropy_state.h"
#include "components/metrics/metrics_log.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/metrics_provider.h"
#include "components/metrics/metrics_switches.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/variations/entropy_provider.h"
#include "components/variations/field_trial_config/field_trial_util.h"
#include "components/variations/pref_names.h"
#include "components/variations/variations_switches.h"
#include "third_party/metrics_proto/chrome_user_metrics_extension.pb.h"
#include "third_party/metrics_proto/system_profile.pb.h"

namespace metrics {

namespace {

int64_t ReadEnabledDate(PrefService* local_state) {
  return local_state->GetInt64(prefs::kMetricsReportingEnabledTimestamp);
}

int64_t ReadInstallDate(PrefService* local_state) {
  return local_state->GetInt64(prefs::kInstallDate);
}

std::string ReadClientId(PrefService* local_state) {
  return local_state->GetString(prefs::kMetricsClientID);
}

// Round a timestamp measured in seconds since epoch to one with a granularity
// of an hour. This can be used before uploaded potentially sensitive
// timestamps.
int64_t RoundSecondsToHour(int64_t time_in_seconds) {
  return 3600 * (time_in_seconds / 3600);
}

// Records the cloned install histogram.
void LogClonedInstall() {
  // Equivalent to UMA_HISTOGRAM_BOOLEAN with the stability flag set.
  UMA_STABILITY_HISTOGRAM_ENUMERATION("UMA.IsClonedInstall", 1, 2);
}

// No-op function used to create a MetricsStateManager.
std::unique_ptr<metrics::ClientInfo> NoOpLoadClientInfoBackup() {
  return nullptr;
}

// Exits the browser with a helpful error message if an invalid,
// field-trial-related command-line flag was specified.
void ExitWithMessage(const std::string& message) {
  puts(message.c_str());
  exit(1);
}

class MetricsStateMetricsProvider : public MetricsProvider {
 public:
  MetricsStateMetricsProvider(
      PrefService* local_state,
      bool metrics_ids_were_reset,
      std::string previous_client_id,
      std::string initial_client_id,
      ClonedInstallDetector const& cloned_install_detector)
      : local_state_(local_state),
        metrics_ids_were_reset_(metrics_ids_were_reset),
        previous_client_id_(std::move(previous_client_id)),
        initial_client_id_(std::move(initial_client_id)),
        cloned_install_detector_(cloned_install_detector) {}

  MetricsStateMetricsProvider(const MetricsStateMetricsProvider&) = delete;
  MetricsStateMetricsProvider& operator=(const MetricsStateMetricsProvider&) =
      delete;

  // MetricsProvider:
  void ProvideSystemProfileMetrics(
      SystemProfileProto* system_profile) override {
    system_profile->set_uma_enabled_date(
        RoundSecondsToHour(ReadEnabledDate(local_state_)));
    system_profile->set_install_date(
        RoundSecondsToHour(ReadInstallDate(local_state_)));

    // Client id in the log shouldn't be different than the |local_state_| one
    // except when the client disabled UMA before we populate this field to the
    // log. If that's the case, the client id in the |local_state_| should be
    // empty and we should set |client_id_was_used_for_trial_assignment| to
    // false.
    std::string client_id = ReadClientId(local_state_);
    system_profile->set_client_id_was_used_for_trial_assignment(
        !client_id.empty() && client_id == initial_client_id_);

    ClonedInstallInfo cloned =
        ClonedInstallDetector::ReadClonedInstallInfo(local_state_);
    if (cloned.reset_count == 0)
      return;
    auto* cloned_install_info = system_profile->mutable_cloned_install_info();
    if (metrics_ids_were_reset_) {
      // Only report the cloned from client_id in the resetting session.
      if (!previous_client_id_.empty()) {
        cloned_install_info->set_cloned_from_client_id(
            MetricsLog::Hash(previous_client_id_));
      }
    }
    cloned_install_info->set_last_timestamp(
        RoundSecondsToHour(cloned.last_reset_timestamp));
    cloned_install_info->set_first_timestamp(
        RoundSecondsToHour(cloned.first_reset_timestamp));
    cloned_install_info->set_count(cloned.reset_count);
  }

  void ProvidePreviousSessionData(
      ChromeUserMetricsExtension* uma_proto) override {
    if (metrics_ids_were_reset_) {
      LogClonedInstall();
      if (!previous_client_id_.empty()) {
        // NOTE: If you are adding anything here, consider also changing
        // FileMetricsProvider::ProvideIndependentMetricsOnTaskRunner().

        // If we know the previous client id, overwrite the client id for the
        // previous session log so the log contains the client id at the time
        // of the previous session. This allows better attribution of crashes
        // to earlier behavior. If the previous client id is unknown, leave
        // the current client id.
        uma_proto->set_client_id(MetricsLog::Hash(previous_client_id_));
      }
    }
  }

  void ProvideCurrentSessionData(
      ChromeUserMetricsExtension* uma_proto) override {
    if (cloned_install_detector_->ClonedInstallDetectedInCurrentSession()) {
      LogClonedInstall();
    }
  }

 private:
  const raw_ptr<PrefService> local_state_;
  const bool metrics_ids_were_reset_;
  // |previous_client_id_| is set only (if known) when
  // |metrics_ids_were_reset_|
  const std::string previous_client_id_;
  // The client id that was used to randomize field trials. An empty string if
  // the low entropy source was used to do randomization.
  const std::string initial_client_id_;
  const raw_ref<const ClonedInstallDetector> cloned_install_detector_;
};

bool ShouldEnableBenchmarking(bool force_benchmarking_mode) {
  // TODO(crbug.com/40792683): See whether it's possible to consolidate the
  // switches.
  return force_benchmarking_mode ||
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             variations::switches::kEnableBenchmarking);
}

}  // namespace

// static
bool MetricsStateManager::instance_exists_ = false;

// static
bool MetricsStateManager::enable_provisional_client_id_for_testing_ = false;

MetricsStateManager::MetricsStateManager(
    PrefService* local_state,
    EnabledStateProvider* enabled_state_provider,
    const std::wstring& backup_registry_key,
    const base::FilePath& user_data_dir,
    EntropyParams entropy_params,
    StartupVisibility startup_visibility,
    StoreClientInfoCallback store_client_info,
    LoadClientInfoCallback retrieve_client_info)
    : local_state_(local_state),
      enabled_state_provider_(enabled_state_provider),
      entropy_params_(entropy_params),
      store_client_info_(std::move(store_client_info)),
      load_client_info_(std::move(retrieve_client_info)),
      clean_exit_beacon_(backup_registry_key, user_data_dir, local_state),
      entropy_state_(local_state),
      entropy_source_returned_(ENTROPY_SOURCE_NONE),
      metrics_ids_were_reset_(false),
      startup_visibility_(startup_visibility) {
  DCHECK(!store_client_info_.is_null());
  DCHECK(!load_client_info_.is_null());
  ResetMetricsIDsIfNecessary();

  [[maybe_unused]] bool is_first_run = false;
  int64_t install_date = local_state_->GetInt64(prefs::kInstallDate);

  // Set the install date if this is our first run.
  if (install_date == 0) {
    local_state_->SetInt64(prefs::kInstallDate, base::Time::Now().ToTimeT());
    is_first_run = true;
  }

  if (enabled_state_provider_->IsConsentGiven()) {
    ForceClientIdCreation();
  } else {
#if BUILDFLAG(IS_ANDROID)
    // If on start up we determine that the client has not given their consent
    // to report their metrics, the new sampling trial should be used to
    // determine whether the client is sampled in or out (if the user ever
    // enables metrics reporting). This covers users that are going through
    // the first run, as well as users that have metrics reporting disabled.
    //
    // See crbug/1306481 and the comment above |kUsePostFREFixSamplingTrial| in
    // components/metrics/metrics_pref_names.cc for more details.
    local_state_->SetBoolean(metrics::prefs::kUsePostFREFixSamplingTrial, true);
#endif  // BUILDFLAG(IS_ANDROID)
  }

  // Generate and store a provisional client ID if necessary. This ID will be
  // used for field trial randomization on first run (and possibly in future
  // runs if the user closes Chrome during the FRE) and will be promoted to
  // become the client ID if UMA is enabled during this session, via the logic
  // in ForceClientIdCreation(). If UMA is disabled (refused), we discard it.
  //
  // Note: This means that if a provisional client ID is used for this session,
  // and the user disables (refuses) UMA, then starting from the next run, the
  // field trial randomization (group assignment) will be different.
  if (ShouldGenerateProvisionalClientId(is_first_run)) {
    local_state_->SetString(prefs::kMetricsProvisionalClientID,
                            base::Uuid::GenerateRandomV4().AsLowercaseString());
  }

  // `initial_client_id_` will only be set in the following cases:
  // 1. UMA is enabled
  // 2. there is a provisional client id (due to this being a first run)
  if (!client_id_.empty()) {
    initial_client_id_ = client_id_;
  } else {
    // Note that there is possibly no provisional client ID.
    initial_client_id_ =
        local_state_->GetString(prefs::kMetricsProvisionalClientID);
  }
  CHECK(!instance_exists_);
  instance_exists_ = true;
}

MetricsStateManager::~MetricsStateManager() {
  CHECK(instance_exists_);
  instance_exists_ = false;
}

std::unique_ptr<MetricsProvider> MetricsStateManager::GetProvider() {
  return std::make_unique<MetricsStateMetricsProvider>(
      local_state_, metrics_ids_were_reset_, previous_client_id_,
      initial_client_id_, cloned_install_detector_);
}

bool MetricsStateManager::IsMetricsReportingEnabled() {
  return enabled_state_provider_->IsReportingEnabled();
}

bool MetricsStateManager::IsExtendedSafeModeSupported() const {
  return clean_exit_beacon_.IsExtendedSafeModeSupported();
}

int MetricsStateManager::GetLowEntropySource() {
  return entropy_state_.GetLowEntropySource();
}

int MetricsStateManager::GetOldLowEntropySource() {
  return entropy_state_.GetOldLowEntropySource();
}

int MetricsStateManager::GetPseudoLowEntropySource() {
  return entropy_state_.GetPseudoLowEntropySource();
}

void MetricsStateManager::InstantiateFieldTrialList() {
  // Instantiate the FieldTrialList to support field trials. If an instance
  // already exists, this is likely a test scenario with a ScopedFeatureList, so
  // use the existing instance so that any overrides are still applied.
  if (!base::FieldTrialList::GetInstance()) {
    // This is intentionally leaked since it needs to live for the duration of
    // the browser process and there's no benefit in cleaning it up at exit.
    base::FieldTrialList* leaked_field_trial_list = new base::FieldTrialList();
    ANNOTATE_LEAKING_OBJECT_PTR(leaked_field_trial_list);
    std::ignore = leaked_field_trial_list;
  }

  // When benchmarking is enabled, field trials' default groups are chosen, so
  // see whether benchmarking needs to be enabled here, before any field trials
  // are created.
  // TODO(crbug.com/40796250): Some FieldTrial-setup-related code is here and
  // some is in VariationsFieldTrialCreator::SetUpFieldTrials(). It's not ideal
  // that it's in two places.
  if (ShouldEnableBenchmarking(entropy_params_.force_benchmarking_mode))
    base::FieldTrial::EnableBenchmarking();

  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(variations::switches::kForceFieldTrialParams)) {
    bool result =
        variations::AssociateParamsFromString(command_line->GetSwitchValueASCII(
            variations::switches::kForceFieldTrialParams));
    if (!result) {
      // Some field trial params implement things like csv or json with a
      // particular param. If some control characters are not %-encoded, it can
      // lead to confusing error messages, so add a hint here.
      ExitWithMessage(base::StringPrintf(
          "Invalid --%s list specified. Make sure you %%-"
          "encode the following characters in param values: %%:/.,",
          variations::switches::kForceFieldTrialParams));
    }
  }

  // Ensure any field trials specified on the command line are initialized.
  if (command_line->HasSwitch(::switches::kForceFieldTrials)) {
    // Create field trials without activating them, so that this behaves in a
    // consistent manner with field trials created from the server.
    bool result = base::FieldTrialList::CreateTrialsFromString(
        command_line->GetSwitchValueASCII(::switches::kForceFieldTrials));
    if (!result) {
      ExitWithMessage(base::StringPrintf("Invalid --%s list specified.",
                                         ::switches::kForceFieldTrials));
    }
  }

  // Initializing the CleanExitBeacon is done after FieldTrialList instantiation
  // to allow experimentation on the CleanExitBeacon.
  clean_exit_beacon_.Initialize();
}

void MetricsStateManager::LogHasSessionShutdownCleanly(
    bool has_session_shutdown_cleanly,
    bool is_extended_safe_mode) {
  clean_exit_beacon_.WriteBeaconValue(has_session_shutdown_cleanly,
                                      is_extended_safe_mode);
}

void MetricsStateManager::ForceClientIdCreation() {
  // TODO(asvitkine): Ideally, all tests would actually set up consent properly,
  // so the command-line checks wouldn't be needed here.
  // Currently, kForceEnableMetricsReporting is used by Java UkmTest and
  // kMetricsRecordingOnly is used by Chromedriver tests.
  DCHECK(enabled_state_provider_->IsConsentGiven() ||
         IsMetricsReportingForceEnabled() || IsMetricsRecordingOnlyEnabled());
#if BUILDFLAG(IS_CHROMEOS)
  std::string previous_client_id = client_id_;
#endif  // BUILDFLAG(IS_CHROMEOS)
  {
    std::string client_id_from_prefs = ReadClientId(local_state_);
    // If client id in prefs matches the cached copy, return early.
    if (!client_id_from_prefs.empty() && client_id_from_prefs == client_id_) {
      base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                    ClientIdSource::kClientIdMatches);
      return;
    }
    client_id_.swap(client_id_from_prefs);
  }

  if (!client_id_.empty()) {
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdFromLocalState);
    return;
  }

  const std::unique_ptr<ClientInfo> client_info_backup = LoadClientInfo();
  if (client_info_backup) {
    client_id_ = client_info_backup->client_id;

    const base::Time now = base::Time::Now();

    // Save the recovered client id and also try to reinstantiate the backup
    // values for the dates corresponding with that client id in order to avoid
    // weird scenarios where we could report an old client id with a recent
    // install date.
    local_state_->SetString(prefs::kMetricsClientID, client_id_);
    local_state_->SetInt64(prefs::kInstallDate,
                           client_info_backup->installation_date != 0
                               ? client_info_backup->installation_date
                               : now.ToTimeT());
    local_state_->SetInt64(prefs::kMetricsReportingEnabledTimestamp,
                           client_info_backup->reporting_enabled_date != 0
                               ? client_info_backup->reporting_enabled_date
                               : now.ToTimeT());

    base::TimeDelta recovered_installation_age;
    if (client_info_backup->installation_date != 0) {
      recovered_installation_age =
          now - base::Time::FromTimeT(client_info_backup->installation_date);
    }
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdBackupRecovered);
    base::UmaHistogramCounts10000("UMA.ClientIdBackupRecoveredWithAge",
                                  recovered_installation_age.InHours());

    // Flush the backup back to persistent storage in case we re-generated
    // missing data above.
    BackUpCurrentClientInfo();
    return;
  }

  // If we're here, there was no client ID yet (either in prefs or backup),
  // so generate a new one. If there's a provisional client id (e.g. UMA
  // was enabled as part of first run), promote that to the client id,
  // otherwise (e.g. UMA enabled in a future session), generate a new one.
  std::string provisional_client_id =
      local_state_->GetString(prefs::kMetricsProvisionalClientID);
  if (provisional_client_id.empty()) {
    client_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdNew);
  } else {
    client_id_ = provisional_client_id;
    local_state_->ClearPref(prefs::kMetricsProvisionalClientID);
    base::UmaHistogramEnumeration("UMA.ClientIdSource",
                                  ClientIdSource::kClientIdFromProvisionalId);
  }
  local_state_->SetString(prefs::kMetricsClientID, client_id_);

  // Record the timestamp of when the user opted in to UMA.
  local_state_->SetInt64(prefs::kMetricsReportingEnabledTimestamp,
                         base::Time::Now().ToTimeT());

  BackUpCurrentClientInfo();
}

const ClonedInstallDetector& MetricsStateManager::GetClonedInstallDetector()
    const {
  return cloned_install_detector_;
}

void MetricsStateManager::CheckForClonedInstall() {
  cloned_install_detector_.CheckForClonedInstall(local_state_);
}

bool MetricsStateManager::ShouldResetClientIdsOnClonedInstall() {
  return cloned_install_detector_.ShouldResetClientIds(local_state_);
}

base::CallbackListSubscription
MetricsStateManager::AddOnClonedInstallDetectedCallback(
    base::OnceClosure callback) {
  return cloned_install_detector_.AddOnClonedInstallDetectedCallback(
      std::move(callback));
}

std::unique_ptr<const variations::EntropyProviders>
MetricsStateManager::CreateEntropyProviders(bool enable_limited_entropy_mode) {
  auto limited_entropy_randomization_source =
      enable_limited_entropy_mode ? GetLimitedEntropyRandomizationSource()
                                  : std::string_view();
  return std::make_unique<variations::EntropyProviders>(
      GetHighEntropySource(),
      variations::ValueInRange{
          .value = base::checked_cast<uint32_t>(GetLowEntropySource()),
          .range = EntropyState::kMaxLowEntropySize},
      limited_entropy_randomization_source,
      ShouldEnableBenchmarking(entropy_params_.force_benchmarking_mode));
}

// static
std::unique_ptr<MetricsStateManager> MetricsStateManager::Create(
    PrefService* local_state,
    EnabledStateProvider* enabled_state_provider,
    const std::wstring& backup_registry_key,
    const base::FilePath& user_data_dir,
    StartupVisibility startup_visibility,
    EntropyParams entropy_params,
    StoreClientInfoCallback store_client_info,
    LoadClientInfoCallback retrieve_client_info) {
  std::unique_ptr<MetricsStateManager> result;
  // Note: |instance_exists_| is updated in the constructor and destructor.
  if (!instance_exists_) {
    result.reset(new MetricsStateManager(
        local_state, enabled_state_provider, backup_registry_key, user_data_dir,
        entropy_params, startup_visibility,
        store_client_info.is_null() ? base::DoNothing()
                                    : std::move(store_client_info),
        retrieve_client_info.is_null()
            ? base::BindRepeating(&NoOpLoadClientInfoBackup)
            : std::move(retrieve_client_info)));
  }
  return result;
}

// static
void MetricsStateManager::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kMetricsProvisionalClientID,
                               std::string());
  registry->RegisterStringPref(prefs::kMetricsClientID, std::string());
  registry->RegisterInt64Pref(prefs::kMetricsReportingEnabledTimestamp, 0);
  registry->RegisterInt64Pref(prefs::kInstallDate, 0);
#if BUILDFLAG(IS_ANDROID)
  registry->RegisterBooleanPref(prefs::kUsePostFREFixSamplingTrial, false);
#endif  // BUILDFLAG(IS_ANDROID)

  EntropyState::RegisterPrefs(registry);
  ClonedInstallDetector::RegisterPrefs(registry);
}

void MetricsStateManager::BackUpCurrentClientInfo() {
  ClientInfo client_info;
  client_info.client_id = client_id_;
  client_info.installation_date = ReadInstallDate(local_state_);
  client_info.reporting_enabled_date = ReadEnabledDate(local_state_);
  store_client_info_.Run(client_info);
}

std::unique_ptr<ClientInfo> MetricsStateManager::LoadClientInfo() {
  // If a cloned install was detected, loading ClientInfo from backup will be
  // a race condition with clearing the backup. Skip all backup reads for this
  // session.
  if (metrics_ids_were_reset_)
    return nullptr;

  std::unique_ptr<ClientInfo> client_info = load_client_info_.Run();

  // The GUID retrieved should be valid unless retrieval failed.
  // If not, return nullptr. This will result in a new GUID being generated by
  // the calling function ForceClientIdCreation().
  if (client_info &&
      !base::Uuid::ParseCaseInsensitive(client_info->client_id).is_valid()) {
    return nullptr;
  }

  return client_info;
}

std::string_view MetricsStateManager::GetLimitedEntropyRandomizationSource() {
  // No limited entropy randomization source will be generated if limited
  // entropy randomization is not supported in this context (e.g. in Android
  // Webview).
  if (entropy_params_.default_entropy_provider_type ==
      EntropyProviderType::kLow) {
    return std::string_view();
  }
  return entropy_state_.GetLimitedEntropyRandomizationSource();
}

std::string MetricsStateManager::GetHighEntropySource() {
  // If high entropy randomization is not supported in this context (e.g. in
  // Android Webview), or if UMA is not enabled (so there is no client id), then
  // high entropy randomization is disabled.
  if (entropy_params_.default_entropy_provider_type ==
          EntropyProviderType::kLow ||
      initial_client_id_.empty()) {
    UpdateEntropySourceReturnedValue(ENTROPY_SOURCE_LOW);
    return "";
  }
  UpdateEntropySourceReturnedValue(ENTROPY_SOURCE_HIGH);
  return entropy_state_.GetHighEntropySource(initial_client_id_);
}

void MetricsStateManager::UpdateEntropySourceReturnedValue(
    EntropySourceType type) {
  if (entropy_source_returned_ != ENTROPY_SOURCE_NONE)
    return;

  entropy_source_returned_ = type;
  base::UmaHistogramEnumeration("UMA.EntropySourceType", type,
                                ENTROPY_SOURCE_ENUM_SIZE);
}

void MetricsStateManager::ResetMetricsIDsIfNecessary() {
  if (!ShouldResetClientIdsOnClonedInstall())
    return;
  metrics_ids_were_reset_ = true;
  previous_client_id_ = ReadClientId(local_state_);

  base::UmaHistogramBoolean("UMA.MetricsIDsReset", true);

  DCHECK(client_id_.empty());

  local_state_->ClearPref(prefs::kMetricsClientID);
  local_state_->ClearPref(prefs::kMetricsLogRecordId);
  EntropyState::ClearPrefs(local_state_);

  cloned_install_detector_.RecordClonedInstallInfo(local_state_);

  // Also clear the backed up client info. This is asynchronus; any reads
  // shortly after may retrieve the old ClientInfo from the backup.
  store_client_info_.Run(ClientInfo());
}

bool MetricsStateManager::ShouldGenerateProvisionalClientId(bool is_first_run) {
#if BUILDFLAG(IS_WIN)
  // We do not want to generate a provisional client ID on Windows because
  // there's no UMA checkbox on first run. Instead it comes from the install
  // page. So if UMA is not enabled at this point, it's unlikely it will be
  // enabled in the same session since that requires the user to manually do
  // that via settings page after they unchecked it on the download page.
  //
  // Note: Windows first run is covered by browser tests
  // FirstRunMasterPrefsVariationsSeedTest.PRE_SecondRun and
  // FirstRunMasterPrefsVariationsSeedTest.SecondRun. If the platform ifdef
  // for this logic changes, the tests should be updated as well.
  return false;
#else
  // We should only generate a provisional client ID on the first run. If for
  // some reason there is already a client ID, we do not generate one either.
  // This can happen if metrics reporting is managed by a policy.
  if (!is_first_run || !client_id_.empty())
    return false;

  // Return false if |kMetricsReportingEnabled| is managed by a policy. For
  // example, if metrics reporting is disabled by a policy, then
  // |kMetricsReportingEnabled| will always be set to false, so there is no
  // reason to generate a provisional client ID. If metrics reporting is enabled
  // by a policy, then the default value of |kMetricsReportingEnabled| will be
  // true, and so a client ID will have already been generated (we would have
  // returned false already because of the previous check).
  if (local_state_->IsManagedPreference(prefs::kMetricsReportingEnabled))
    return false;

  // If this is a non-Google-Chrome-branded build, we do not want to generate a
  // provisional client ID because metrics reporting is not enabled on those
  // builds. This would be problematic because we store the provisional client
  // ID in the Local State, and clear it when either 1) we enable UMA (the
  // provisional client ID becomes the client ID), or 2) we disable UMA. Since
  // in non-Google-Chrome-branded builds we never actually go through the code
  // paths to either enable or disable UMA, the pref storing the provisional
  // client ID would never be cleared. However, for test consistency between
  // the different builds, we do not return false here if
  // |enable_provisional_client_id_for_testing_| is set to true.
  if (!BUILDFLAG(GOOGLE_CHROME_BRANDING) &&
      !enable_provisional_client_id_for_testing_) {
    return false;
  }

  return true;
#endif  // BUILDFLAG(IS_WIN)
}

}  // namespace metrics
