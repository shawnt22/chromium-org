// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SIGNIN_PROFILE_PICKER_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SIGNIN_PROFILE_PICKER_HANDLER_H_

#include <memory>
#include <optional>
#include <unordered_map>

#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/buildflag.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_statistics_common.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui_message_handler.h"

class ScopedProfileKeepAlive;

class ForceSigninUIError;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
//
// LINT.IfChange(ProfilePickerAction)
enum class ProfilePickerAction {
  kLaunchExistingProfile = 0,
  kLaunchExistingProfileCustomizeSettings = 1,
  kLaunchGuestProfile = 2,
  kLaunchNewProfile = 3,
  kDeleteProfile = 4,
  kMaxValue = kDeleteProfile,
};
// LINT.ThenChange(//tools/metrics/histograms/metadata/profile/enums.xml:ProfilePickerAction)

void RecordProfilePickerAction(ProfilePickerAction action);

// The handler for Javascript messages related to the profile picker main view.
class ProfilePickerHandler : public content::WebUIMessageHandler,
                             public content::WebContentsObserver,
                             public ProfileAttributesStorage::Observer {
 public:
  explicit ProfilePickerHandler(bool is_glic_version);

  ProfilePickerHandler(const ProfilePickerHandler&) = delete;
  ProfilePickerHandler& operator=(const ProfilePickerHandler&) = delete;

  ~ProfilePickerHandler() override;

  // Enables the startup performance metrics. Should only be called when the
  // profile picker is shown on startup.
  void EnableStartupMetrics();

  // Displays an error dialog on top of the profile picker based on the error
  // enum.
  // Empty `profile_path` will not show an additional "Sign in" button that
  // allows to reach reauth step.
  void DisplayForceSigninErrorDialog(const base::FilePath& profile_path,
                                     const ForceSigninUIError& error);

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 private:
  friend class ProfilePickerHandlerTest;
  friend class ProfilePickerHandlerInUserProfileTest;
  friend class ProfilePickerCreationFlowBrowserTest;
  friend class ProfilePickerEnterpriseCreationFlowBrowserTest;
  friend class StartupBrowserCreatorPickerInfobarTest;
  friend class SupervisedProfilePickerHideGuestModeTest;
  FRIEND_TEST_ALL_PREFIXES(ProfilePickerHandlerInUserProfileTest,
                           HandleExtendedAccountInformation);
  FRIEND_TEST_ALL_PREFIXES(ProfilePickerCreationFlowBrowserTest,
                           CloseBrowserBeforeCreatingNewProfile);
  FRIEND_TEST_ALL_PREFIXES(ProfilePickerCreationFlowBrowserTest, DeleteProfile);
  FRIEND_TEST_ALL_PREFIXES(ProfilePickerCreationFlowBrowserTest,
                           DeleteProfileFromOwnTab);
  FRIEND_TEST_ALL_PREFIXES(ProfilePickerWithGlicParamBrowserTest,
                           GlicLearnMoreClicked);
  FRIEND_TEST_ALL_PREFIXES(
      ProfilePickerEnterpriseCreationFlowBrowserTest,
      CreateSignedInProfileSigninAlreadyExists_ConfirmSwitch);
  FRIEND_TEST_ALL_PREFIXES(
      ProfilePickerEnterpriseCreationFlowBrowserTest,
      CreateSignedInProfileSigninAlreadyExists_CancelSwitch);

  void HandleMainViewInitialize(const base::Value::List& args);
  void HandleLaunchSelectedProfile(bool open_settings,
                                   const base::Value::List& args);
  void HandleLaunchGuestProfile(const base::Value::List& args);
  void HandleAskOnStartupChanged(const base::Value::List& args);
  void HandleRemoveProfile(const base::Value::List& args);
  void HandleGetProfileStatistics(const base::Value::List& args);
  void HandleCloseProfileStatistics(const base::Value::List& args);
  void HandleSetProfileName(const base::Value::List& args);
  void HandleUpdateProfileOrder(const base::Value::List& args);
  void HandleOnLearnMoreClicked(const base::Value::List& args);

  void HandleSelectNewAccount(const base::Value::List& args);
  void HandleGetNewProfileSuggestedThemeInfo(const base::Value::List& args);
  void HandleGetProfileThemeInfo(const base::Value::List& args);
  void HandleGetAvailableIcons(const base::Value::List& args);
  void HandleContinueWithoutAccount(const base::Value::List& args);

  // Profile switch screen:
  void HandleGetSwitchProfile(const base::Value::List& args);
  void HandleConfirmProfileSwitch(const base::Value::List& args);
  void HandleCancelProfileSwitch(const base::Value::List& args);

  // |args| is unused.
  void HandleRecordSignInPromoImpression(const base::Value::List& args);

  void OnLoadSigninFinished(bool success);
  void GatherProfileStatistics(Profile* profile);
  void OnProfileStatisticsReceived(const base::FilePath& profile_path,
                                   profiles::ProfileCategoryStats result);

  void OnProfileCreationFinished(bool finished_successfully);
  void PushProfilesList();
  base::Value::List GetProfilesList();
  // Adds a profile with `profile_path` to `profiles_order_` and notifies
  // the JS listeners on ui updates.
  void AddProfileToListAndPushUpdates(const base::FilePath& profile_path);
  // Removes a profile with `profile_path` from `profiles_order_` and notifies
  // the JS listeners on ui updates.
  void RemoveProfileFromListAndPushUpdates(const base::FilePath& profile_path);

  // ProfileAttributesStorage::Observer:
  void OnProfileAdded(const base::FilePath& profile_path) override;
  void OnProfileWasRemoved(const base::FilePath& profile_path,
                           const std::u16string& profile_name) override;
  void OnProfileIsOmittedChanged(const base::FilePath& profile_path) override;
  void OnProfileAvatarChanged(const base::FilePath& profile_path) override;
  void OnProfileHighResAvatarLoaded(
      const base::FilePath& profile_path) override;
  void OnProfileNameChanged(const base::FilePath& profile_path,
                            const std::u16string& old_profile_name) override;
  void OnProfileIsManagedChanged(const base::FilePath& profile_path) override;
  void OnProfileSupervisedUserIdChanged(
      const base::FilePath& profile_path) override;

  // content::WebContentsObserver:
  void DidFirstVisuallyNonEmptyPaint() override;
  void OnVisibilityChanged(content::Visibility visibility) override;

  // Sets 'profiles_order_' that is used to freeze the order of the profiles on
  // the picker when it was first shown.
  void SetProfilesOrder(const std::vector<ProfileAttributesEntry*>& entries);

  // Checks the state of `entry` to determine how to handle the locked state.
  // Either shows an error dialog with the appropriate information, or attempts
  // to run the signin/reauth flows.
  void TryLaunchLockedProfile(ProfileAttributesEntry& entry);
  // Callback with the loaded profile to start the reauth flow.
  void OnProfileLoadedForSwitchToReauth(Profile* profile);

  // Updates if guest mode is available following a profile addition, removal,
  // or changed supervision status.
  void MaybeUpdateGuestMode();

  // Returns the list of profiles in the same order as when the picker
  // was first shown.
  // Filters out profiles that are not eligible to be shown: e.g. omitted
  // profiles and glic ineligible profiles if applicable.
  std::vector<ProfileAttributesEntry*> GetProfilesAttributesForDisplay();

  const bool is_glic_version_;

  // Observes changes to profile attributes, and notifies the WebUI.
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorage::Observer>
      profile_attributes_storage_observation_{this};

  // Creation time of the handler, to measure performance on startup. Only set
  // when the picker is shown on startup.
  base::TimeTicks creation_time_on_startup_;

  bool main_view_initialized_ = false;

  // Keep alive used when displaying the profile statistics in the profile
  // deletion dialog. Released when the dialog or the Picker is closed, which
  // will unload the respective profile if this was the only keep alive. Since
  // the dialog and the statistics can be shown only for one single profile at
  // a time, only one ScopedProfileKeepAlive is needed for the Profile Picker.
  std::unique_ptr<ScopedProfileKeepAlive> profile_statistics_keep_alive_;

  // The order of the profiles when the picker was first shown. This is used
  // to freeze the order of profiles on the picker. Newly added profiles, will
  // be added to the end of the list.
  std::unordered_map<base::FilePath, size_t> profiles_order_;
  base::WeakPtrFactory<ProfilePickerHandler> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_SIGNIN_PROFILE_PICKER_HANDLER_H_
