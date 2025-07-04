// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/supervised_user/core/browser/supervised_user_service.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "base/version.h"
#include "build/build_config.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/supervised_user/core/browser/kids_chrome_management_url_checker_client.h"
#include "components/supervised_user/core/browser/permission_request_creator_impl.h"
#include "components/supervised_user/core/browser/supervised_user_preferences.h"
#include "components/supervised_user/core/browser/supervised_user_service_observer.h"
#include "components/supervised_user/core/browser/supervised_user_settings_service.h"
#include "components/supervised_user/core/browser/supervised_user_url_filter.h"
#include "components/supervised_user/core/browser/supervised_user_utils.h"
#include "components/supervised_user/core/common/features.h"
#include "components/supervised_user/core/common/pref_names.h"
#include "components/supervised_user/core/common/supervised_user_constants.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/service/sync_user_settings.h"
#include "google_apis/gaia/gaia_id.h"
#include "ui/base/l10n/l10n_util.h"

namespace supervised_user {

namespace {
using base::UserMetricsAction;

// All prefs that configure the url filter.
std::array<const char*, 4> kUrlFilterSettingsPrefs = {
  prefs::kDefaultSupervisedUserFilteringBehavior,
  prefs::kSupervisedUserSafeSites, prefs::kSupervisedUserManualHosts,
  prefs::kSupervisedUserManualURLs};

// Helper that extracts custodian data from given preferences.
std::optional<Custodian> GetCustodianFromPrefs(
    const PrefService& user_prefs,
    std::string_view email_address_pref,
    std::string_view name_pref,
    std::string_view gaia_id_pref,
    std::string_view profile_image_url_pref) {
  std::string email(user_prefs.GetString(email_address_pref));
  std::string name(user_prefs.GetString(name_pref));
  GaiaId gaia_id(user_prefs.GetString(gaia_id_pref));
  std::string profile_image_url(user_prefs.GetString(profile_image_url_pref));

  if (email.empty() && name.empty() && gaia_id.empty() &&
      profile_image_url.empty()) {
    return std::nullopt;
  }
  return Custodian((name.empty() ? email : name), email, gaia_id,
                   profile_image_url);
}

// Sentinel that guards against accidental pref changes.
void PrefChangeNotAllowed(const std::string& pref_name) {
  NOTREACHED() << "Preference change (" << pref_name << ") not allowed.";
}
}  // namespace

Custodian::Custodian(std::string_view name,
                     std::string_view email_address,
                     GaiaId obfuscated_gaia_id,
                     std::string_view profile_image_url)
    : name_(name),
      email_address_(email_address),
      obfuscated_gaia_id_(obfuscated_gaia_id),
      profile_image_url_(profile_image_url) {}
Custodian::Custodian(std::string_view name,
                     std::string_view email_address,
                     std::string_view profile_image_url)
    : Custodian(name, email_address, GaiaId(), profile_image_url) {}

Custodian::Custodian(const Custodian& other) = default;
Custodian::~Custodian() = default;

SupervisedUserService::~SupervisedUserService() {
  DCHECK(did_shutdown_);
}

SupervisedUserURLFilter* SupervisedUserService::GetURLFilter() const {
  return url_filter_.get();
}

bool SupervisedUserService::IsSupervisedLocally() const {
#if BUILDFLAG(IS_ANDROID)
  return IsLocalContentFilteringEnabled() ||
         search_content_filters_observer_->IsEnabled();
#else
  return false;
#endif
}

bool SupervisedUserService::IsLocalContentFilteringEnabled() const {
#if BUILDFLAG(IS_ANDROID)
  return browser_content_filters_observer_->IsEnabled();
#else
  return false;
#endif
}

std::optional<Custodian> SupervisedUserService::GetCustodian() const {
  return GetCustodianFromPrefs(user_prefs_.get(),
                               prefs::kSupervisedUserCustodianEmail,
                               prefs::kSupervisedUserCustodianName,
                               prefs::kSupervisedUserCustodianObfuscatedGaiaId,
                               prefs::kSupervisedUserCustodianProfileImageURL);
}

std::optional<Custodian> SupervisedUserService::GetSecondCustodian() const {
  return GetCustodianFromPrefs(
      user_prefs_.get(), prefs::kSupervisedUserSecondCustodianEmail,
      prefs::kSupervisedUserSecondCustodianName,
      prefs::kSupervisedUserSecondCustodianObfuscatedGaiaId,
      prefs::kSupervisedUserSecondCustodianProfileImageURL);
}

bool SupervisedUserService::IsBlockedURL(const GURL& url) const {
  // TODO(b/359161670): prevent access to URL filtering through lifecycle events
  // rather than individually checking active state.
  if (!IsSubjectToParentalControls(user_prefs_.get())) {
    return false;
  }
  return GetURLFilter()->GetFilteringBehavior(url).IsBlocked();
}

void SupervisedUserService::AddObserver(
    SupervisedUserServiceObserver* observer) {
  observer_list_.AddObserver(observer);
}

void SupervisedUserService::RemoveObserver(
    SupervisedUserServiceObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

// Note: unretained is safe, because the utility that binds callbacks is owned
// by this instance.
SupervisedUserService::SupervisedUserService(
    signin::IdentityManager* identity_manager,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    PrefService& user_prefs,
    SupervisedUserSettingsService& settings_service,
    syncer::SyncService* sync_service,
    std::unique_ptr<SupervisedUserURLFilter> url_filter,
    std::unique_ptr<SupervisedUserService::PlatformDelegate> platform_delegate
#if BUILDFLAG(IS_ANDROID)
    ,
    ContentFiltersObserverBridge::Factory
        content_filters_observer_bridge_factory
#endif
    )
    : user_prefs_(user_prefs),
      settings_service_(settings_service),
      sync_service_(sync_service),
      identity_manager_(identity_manager),
      url_loader_factory_(url_loader_factory),
      url_filter_(std::move(url_filter)),
      // From here, the callbacks and observers can be added.
      controls_state_(
          user_prefs,
          base::BindRepeating(
              &SupervisedUserService::OnFamilyLinkParentalControlsEnabled,
              base::Unretained(this)),
          base::BindRepeating(
              &SupervisedUserService::OnLocalParentalControlsEnabled,
              base::Unretained(this)),
          base::BindRepeating(
              &SupervisedUserService::OnParentalControlsDisabled,
              base::Unretained(this))),
      platform_delegate_(std::move(platform_delegate))
#if BUILDFLAG(IS_ANDROID)
      ,
      browser_content_filters_observer_(
          content_filters_observer_bridge_factory.Run(
              kBrowserContentFiltersSettingName,
              base::BindRepeating(&EnableBrowserContentFilters,
                                  std::ref(user_prefs)),
              base::BindRepeating(&DisableBrowserContentFilters,
                                  std::ref(user_prefs)))),
      search_content_filters_observer_(
          content_filters_observer_bridge_factory.Run(
              kSearchContentFiltersSettingName,
              base::BindRepeating(
                  &SupervisedUserService::EnableSearchContentFilters,
                  base::Unretained(this)),
              base::BindRepeating(&DisableSearchContentFilters,
                                  std::ref(user_prefs))))
#endif  // BUILDFLAG(IS_ANDROID)
{
  CHECK(settings_service_->IsReady())
      << "Settings service is initialized as part of the PrefService, which is "
         "a dependency of this service.";

#if BUILDFLAG(IS_ANDROID)
  browser_content_filters_observer_->Init();
  search_content_filters_observer_->Init();
#endif  // BUILDFLAG(IS_ANDROID)

  main_pref_change_registrar_.Init(&user_prefs_.get());
  main_pref_change_registrar_.Add(
      policy::policy_prefs::kIncognitoModeAvailability,
      base::BindRepeating(
          &SupervisedUserService::OnIncognitoModeAvailabilityChanged,
          base::Unretained(this)));

  // Bumps this instance to read the current state of parental controls.
  controls_state_.Notify();
}

void SupervisedUserService::SetSettingsServiceActive(bool active) {
  settings_service_->SetActive(active);

  // Trigger a sync reconfig to enable/disable the right SU data types.
  // The logic to do this lives in the
  // SupervisedUserSettingsDataTypeController.
  // TODO(crbug.com/40620346): Get rid of this hack and instead call
  // DataTypePreconditionChanged from the controller.
  if (sync_service_ &&
      sync_service_->GetUserSettings()->IsInitialSyncFeatureSetupComplete()) {
    // Trigger a reconfig by grabbing a SyncSetupInProgressHandle and
    // immediately releasing it again (via the temporary unique_ptr going away).
    std::ignore = sync_service_->GetSetupInProgressHandle();
  }
}

void SupervisedUserService::SetUserSettingsActive(bool active) {
  if (active) {
    user_prefs_->SetInteger(
        policy::policy_prefs::kIncognitoModeAvailability,
        static_cast<int>(policy::IncognitoModeAvailability::kDisabled));
    // Sets "Try to block mature sites" on user level.
    user_prefs_->SetBoolean(prefs::kSupervisedUserSafeSites, true);
    user_prefs_->SetInteger(prefs::kDefaultSupervisedUserFilteringBehavior,
                            static_cast<int>(FilteringBehavior::kAllow));
  } else {
    user_prefs_->ClearPref(policy::policy_prefs::kIncognitoModeAvailability);
    user_prefs_->ClearPref(prefs::kSupervisedUserSafeSites);
    user_prefs_->ClearPref(prefs::kDefaultSupervisedUserFilteringBehavior);
  }
}

void SupervisedUserService::OnFamilyLinkParentalControlsEnabled() {
  // Remove the handlers of the disabled parental controls mode.
  RemoveURLFilterPrefChangeHandlers();

  SetSettingsServiceActive(true);
  remote_web_approvals_manager_.AddApprovalRequestCreator(
      std::make_unique<PermissionRequestCreatorImpl>(identity_manager_,
                                                     url_loader_factory_));

  // Add handlers at the end to avoid multiple notifications.
  AddCustodianPrefChangeHandlers();
  AddURLFilterPrefChangeHandlers();

  // Synchronize the filter.
  UpdateURLFilter();
}

void SupervisedUserService::OnLocalParentalControlsEnabled() {
  // Remove the handlers of the disabled parental controls mode. Note that user
  // controls won't listen to any url filter pref changes - these are static for
  // this type of controls.
  RemoveURLFilterPrefChangeHandlers();
  SetUserSettingsActive(true);

  // Add handlers that will prevent unsupported url filter changes.
  AddURLFilterPrefChangeSentinels();

  // Synchronize the filter.
  UpdateURLFilter();
}

void SupervisedUserService::OnParentalControlsDisabled() {
  // Start with removing handlers, to avoid multiple notifications from pref
  // status changes from the settings service.
  RemoveURLFilterPrefChangeHandlers();
  RemoveCustodianPrefChangeHandlers();

  // All disabling operations are idempotent.
  SetSettingsServiceActive(false);
  SetUserSettingsActive(false);
  remote_web_approvals_manager_.ClearApprovalRequestsCreators();

  // Synchronize the filter.
  UpdateURLFilter();
}

void SupervisedUserService::AddURLFilterPrefChangeHandlers() {
  url_filter_pref_change_registrar_.Init(&user_prefs_.get());
  for (const char* const pref : kUrlFilterSettingsPrefs) {
    url_filter_pref_change_registrar_.Add(
        pref, base::BindRepeating(&SupervisedUserService::OnURLFilterChanged,
                                  base::Unretained(this)));
  }
}
void SupervisedUserService::AddURLFilterPrefChangeSentinels() {
  url_filter_pref_change_registrar_.Init(&user_prefs_.get());
  for (const char* const pref : kUrlFilterSettingsPrefs) {
    url_filter_pref_change_registrar_.Add(
        pref, base::BindRepeating(&PrefChangeNotAllowed));
  }
}

void SupervisedUserService::AddCustodianPrefChangeHandlers() {
  custodian_pref_change_registrar_.Init(&user_prefs_.get());
  for (const auto* const pref : kCustodianInfoPrefs) {
    custodian_pref_change_registrar_.Add(
        pref,
        base::BindRepeating(&SupervisedUserService::OnCustodianInfoChanged,
                            base::Unretained(this)));
  }
}

void SupervisedUserService::RemoveURLFilterPrefChangeHandlers() {
  url_filter_pref_change_registrar_.RemoveAll();
}

void SupervisedUserService::RemoveCustodianPrefChangeHandlers() {
  custodian_pref_change_registrar_.RemoveAll();
}

void SupervisedUserService::OnCustodianInfoChanged() {
  observer_list_.Notify(&SupervisedUserServiceObserver::OnCustodianInfoChanged);
}

void SupervisedUserService::OnIncognitoModeAvailabilityChanged() {
  // This is called in the following cases:
  // 1) When kSupervisedUserId changes state and indicates child account, the
  //    `setings_service_`::SetActive(true) call notifies all subscribers that
  //    settings have changed. SupervisedUserPrefStore is one of these
  //    subscribers, and it unconditionally disables the incognito mode.
  // 2) When user supervision is enabled - then this service sets the pref
  //    directly.
  // 3) When incognito mode is explicitly disabled, regardless kSupervisedUserId
  //    status.
  // 4) Backing policy pref is updated independently from supervised user
  //    features. Closing incognito tabs in this situation seems the right thing
  //    to do and closing incognito tabs is idempotent.
  if (platform_delegate_->ShouldCloseIncognitoTabs()) {
    platform_delegate_->CloseIncognitoTabs();
  }
}

void SupervisedUserService::OnURLFilterChanged(const std::string& pref_name) {
  CHECK(IsSubjectToParentalControls(user_prefs_.get()))
      << "Url filter setting `" << pref_name
      << "` can only be dynamically changed by managed user infrastructure.";
  UpdateURLFilter(pref_name);
}

void SupervisedUserService::UpdateURLFilter(
    std::optional<std::string> pref_name) {
  // These prefs hold complex data structures that need to be updated.
  if (pref_name.value_or(prefs::kSupervisedUserManualHosts) ==
      prefs::kSupervisedUserManualHosts) {
    url_filter_->UpdateManualHosts();
  }
  if (pref_name.value_or(prefs::kSupervisedUserManualURLs) ==
      prefs::kSupervisedUserManualURLs) {
    url_filter_->UpdateManualUrls();
  }

  observer_list_.Notify(&SupervisedUserServiceObserver::OnURLFilterChanged);
}

void SupervisedUserService::Shutdown() {
  DCHECK(!did_shutdown_);
  did_shutdown_ = true;

#if BUILDFLAG(IS_ANDROID)
  browser_content_filters_observer_->Shutdown();
  search_content_filters_observer_->Shutdown();
#endif  // BUILDFLAG(IS_ANDROID)

  if (IsSubjectToParentalControls(user_prefs_.get())) {
    base::RecordAction(UserMetricsAction("ManagedUsers_QuitBrowser"));
  }

  CHECK(settings_service_->IsReady())
      << "This service depends on the settings service, which will be shut "
         "down in its own procedure";
  // Note: we can't shut down the settings service here, because it could put
  // the system in incorrect state: supervision is enabled, but artificially
  // deactivated settings service had also reset the filter to defaults (that
  // allow all url classifications). On the other hand, if supervision is
  // disabled, then the settings service is already inactive.
}

#if BUILDFLAG(IS_ANDROID)
void SupervisedUserService::EnableSearchContentFilters() {
  supervised_user::EnableSearchContentFilters(user_prefs_.get());
  observer_list_.Notify(
      &SupervisedUserServiceObserver::OnSearchContentFiltersEnabled);
}
#endif  // BUILDFLAG(IS_ANDROID)

}  // namespace supervised_user
