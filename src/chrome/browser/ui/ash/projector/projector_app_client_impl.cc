// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/projector/projector_app_client_impl.h"

#include <string>

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/webui/annotator/untrusted_annotator_page_handler_impl.h"
#include "ash/webui/projector_app/public/cpp/projector_app_constants.h"
#include "base/check_deref.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ref.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/feedback/show_feedback_page.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/ash/projector/projector_soda_installation_controller.h"
#include "chromeos/ash/components/account_manager/account_manager_facade_factory.h"
#include "components/account_manager_core/account_manager_facade.h"
#include "components/application_locale_storage/application_locale_storage.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/soda/constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"

namespace {

constexpr char kUsEnglishLocale[] = "en-US";

}  // namespace

// static
void ProjectorAppClientImpl::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      ash::prefs::kProjectorCreationFlowEnabled, /*default_value=*/false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
  registry->RegisterStringPref(
      ash::prefs::kProjectorCreationFlowLanguage,
      /*default_value=*/kUsEnglishLocale,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
  registry->RegisterIntegerPref(
      ash::prefs::kProjectorGalleryOnboardingShowCount, 0,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
  registry->RegisterIntegerPref(
      ash::prefs::kProjectorViewerOnboardingShowCount, 0,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
  registry->RegisterBooleanPref(ash::prefs::kProjectorAllowByPolicy,
                                /*default_value=*/true);
  registry->RegisterBooleanPref(
      ash::prefs::kProjectorDogfoodForFamilyLinkEnabled,
      /*default_value=*/false);
  registry->RegisterBooleanPref(
      ash::prefs::kProjectorExcludeTranscriptDialogShown,
      /*default_value=*/false);
  registry->RegisterBooleanPref(
      ash::prefs::kProjectorSWAUIPrefsMigrated, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_OS_PREF);
}

ProjectorAppClientImpl::ProjectorAppClientImpl(
    PrefService* local_state,
    ApplicationLocaleStorage* application_locale_storage)
    : local_state_(CHECK_DEREF(local_state)),
      application_locale_storage_(CHECK_DEREF(application_locale_storage)),
      pending_screencast_manager_(base::BindRepeating(
          &ProjectorAppClientImpl::NotifyScreencastsPendingStatusChanged,
          base::Unretained(this))) {}

ProjectorAppClientImpl::~ProjectorAppClientImpl() = default;

void ProjectorAppClientImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ProjectorAppClientImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

signin::IdentityManager* ProjectorAppClientImpl::GetIdentityManager() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  return IdentityManagerFactory::GetForProfile(profile);
}

network::mojom::URLLoaderFactory*
ProjectorAppClientImpl::GetUrlLoaderFactory() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  return profile->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess()
      .get();
}

void ProjectorAppClientImpl::OnNewScreencastPreconditionChanged(
    const ash::NewScreencastPrecondition& precondition) {
  for (auto& observer : observers_) {
    observer.OnNewScreencastPreconditionChanged(precondition);
  }
}

const ash::PendingScreencastContainerSet&
ProjectorAppClientImpl::GetPendingScreencasts() const {
  return pending_screencast_manager_.GetPendingScreencasts();
}

void ProjectorAppClientImpl::NotifyScreencastsPendingStatusChanged(
    const ash::PendingScreencastContainerSet& pending_screencast_containers) {
  for (auto& observer : observers_) {
    observer.OnScreencastsPendingStatusChanged(pending_screencast_containers);
  }
}

bool ProjectorAppClientImpl::ShouldDownloadSoda() const {
  return ProjectorSodaInstallationController::ShouldDownloadSoda(
      speech::GetLanguageCode(application_locale_storage_->Get()));
}

void ProjectorAppClientImpl::InstallSoda() {
  return ProjectorSodaInstallationController::InstallSoda(
      *local_state_, application_locale_storage_->Get());
}

void ProjectorAppClientImpl::OnSodaInstallProgress(int combined_progress) {
  for (auto& observer : observers_) {
    observer.OnSodaProgress(combined_progress);
  }
}

void ProjectorAppClientImpl::OnSodaInstallError() {
  for (auto& observer : observers_) {
    observer.OnSodaError();
  }
}

void ProjectorAppClientImpl::OnSodaInstalled() {
  for (auto& observer : observers_) {
    observer.OnSodaInstalled();
  }
}

void ProjectorAppClientImpl::OpenFeedbackDialog() const {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  constexpr char kProjectorAppFeedbackCategoryTag[] = "FromProjectorApp";
  chrome::ShowFeedbackPage(GURL(ash::kChromeUIUntrustedProjectorUrl), profile,
                           feedback::kFeedbackSourceProjectorApp,
                           /*description_template=*/std::string(),
                           /*description_placeholder_text=*/std::string(),
                           kProjectorAppFeedbackCategoryTag,
                           /*extra_diagnostics=*/std::string());
  // TODO(crbug/1048368): Communicate the dialog failing to open by returning an
  // error string. For now, assume that the dialog has opened successfully.
}

void ProjectorAppClientImpl::GetVideo(
    const std::string& video_file_id,
    const std::optional<std::string>& resource_key,
    ash::ProjectorAppClient::OnGetVideoCallback callback) const {
  screencast_manager_.GetVideo(video_file_id, resource_key,
                               std::move(callback));
}

void ProjectorAppClientImpl::NotifyAppUIActive(bool active) {
  pending_screencast_manager_.OnAppActiveStatusChanged(active);
  if (!active) {
    screencast_manager_.ResetScopeSuppressDriveNotifications();
  }
}

void ProjectorAppClientImpl::ToggleFileSyncingNotificationForPaths(
    const std::vector<base::FilePath>& screencast_paths,
    bool suppress) {
  pending_screencast_manager_.ToggleFileSyncingNotificationForPaths(
      screencast_paths, suppress);
}

void ProjectorAppClientImpl::HandleAccountReauth(const std::string& email) {
  ash::GetAccountManagerFacade(
      ProfileManager::GetActiveUserProfile()->GetPath().value())
      ->ShowReauthAccountDialog(
          account_manager::AccountManagerFacade::AccountAdditionSource::
              kChromeOSProjectorAppReauth,
          email, base::DoNothing());
}
