// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/app_restore/full_restore_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ash/constants/ash_pref_names.h"
#include "ash/constants/ash_switches.h"
#include "ash/constants/web_app_id_constants.h"
#include "ash/glanceables/post_login_glanceables_metrics_recorder.h"
#include "ash/public/cpp/session/session_controller.h"
#include "ash/shell.h"
#include "ash/wm/desks/templates/saved_desk_controller.h"
#include "ash/wm/window_restore/informed_restore_controller.h"
#include "ash/wm/window_restore/window_restore_metrics.h"
#include "ash/wm/window_restore/window_restore_util.h"
#include "base/barrier_callback.h"
#include "base/check_is_test.h"
#include "base/command_line.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "base/version.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/ash/app_restore/app_restore_arc_task_handler.h"
#include "chrome/browser/ash/app_restore/app_restore_arc_task_handler_factory.h"
#include "chrome/browser/ash/app_restore/full_restore_app_launch_handler.h"
#include "chrome/browser/ash/app_restore/full_restore_data_handler.h"
#include "chrome/browser/ash/app_restore/full_restore_prefs.h"
#include "chrome/browser/ash/app_restore/new_user_restore_pref_handler.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/app_session_service_factory.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/account_id/account_id.h"
#include "components/app_constants/constants.h"
#include "components/app_restore/app_restore_data.h"
#include "components/app_restore/app_restore_info.h"
#include "components/app_restore/app_restore_utils.h"
#include "components/app_restore/features.h"
#include "components/app_restore/full_restore_save_handler.h"
#include "components/app_restore/full_restore_utils.h"
#include "components/app_restore/restore_data.h"
#include "components/app_restore/window_info.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/url_formatter/url_formatter.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "content/public/common/url_constants.h"

// Enable VLOG level 1.
#undef ENABLED_VLOG_LEVEL
#define ENABLED_VLOG_LEVEL 1

namespace ash::full_restore {

namespace {

bool g_restore_for_testing = true;

// If true, do not show any full restore UI.
bool g_last_session_sanitized = false;

// This flag forces full session restore on startup regardless of potential
// non-clean shutdown. It could be used in tests to ignore crashes on shutdown.
constexpr char kForceFullRestoreAndSessionRestoreAfterCrash[] =
    "force-full-restore-and-session-restore-after-crash";

constexpr char kRestoreSettingHistogramName[] = "Apps.RestoreSetting";
constexpr char kRestoreInitSettingHistogramName[] = "Apps.RestoreInitSetting";
constexpr char kFullRestoreWindowCountHistogramName[] =
    "Apps.FullRestoreWindowCount2";

// Returns true if `profile` is the primary user profile.
bool IsPrimaryUser(Profile* profile) {
  return ProfileHelper::Get()->GetUserByProfile(profile) ==
         user_manager::UserManager::Get()->GetPrimaryUser();
}

// Will (maybe) initiate an auto launch of an admin template.
void MaybeInitiateAdminTemplateAutoLaunch() {
  // The controller is available if the admin template feature is enabled.
  if (auto* saved_desk_controller = ash::SavedDeskController::Get()) {
    saved_desk_controller->InitiateAdminTemplateAutoLaunch(base::DoNothing());
  }
}

}  // namespace

class DelegateImpl : public FullRestoreService::Delegate {
 public:
  DelegateImpl() = default;
  DelegateImpl(const DelegateImpl&) = delete;
  DelegateImpl& operator=(const DelegateImpl&) = delete;
  ~DelegateImpl() override = default;

  void MaybeStartInformedRestoreOverviewSession(
      std::unique_ptr<InformedRestoreContentsData> contents_data) override {
    // A unit test that does not override this default delegate may not have ash
    // shell.
    if (Shell::HasInstance()) {
      CHECK(Shell::Get()->informed_restore_controller());
      Shell::Get()
          ->informed_restore_controller()
          ->MaybeStartInformedRestoreSession(std::move(contents_data));
    }
  }

  void MaybeEndInformedRestoreOverviewSession() override {
    // A unit test that does not override this default delegate may not have ash
    // shell.
    if (Shell::HasInstance()) {
      CHECK(Shell::Get()->informed_restore_controller());
      Shell::Get()
          ->informed_restore_controller()
          ->MaybeEndInformedRestoreSession();
    }
  }

  InformedRestoreContentsData* GetInformedRestoreContentData() override {
    if (Shell::HasInstance()) {
      CHECK(Shell::Get()->informed_restore_controller());
      return Shell::Get()->informed_restore_controller()->contents_data();
    }
    return nullptr;
  }

  void OnInformedRestoreContentsDataUpdated() override {
    if (Shell::HasInstance()) {
      CHECK(Shell::Get()->informed_restore_controller());
      Shell::Get()->informed_restore_controller()->OnContentsDataUpdated();
    }
  }
};

FullRestoreService::FullRestoreService(Profile* profile)
    : profile_(profile),
      app_launch_handler_(std::make_unique<FullRestoreAppLaunchHandler>(
          profile_,
          /*should_init_service=*/true)),
      restore_data_handler_(std::make_unique<FullRestoreDataHandler>(profile_)),
      delegate_(std::make_unique<DelegateImpl>()) {
  on_app_terminating_subscription_ =
      browser_shutdown::AddAppTerminatingCallback(base::BindOnce(
          &FullRestoreService::OnAppTerminating, base::Unretained(this)));

  auto* full_restore_save_handler =
      ::full_restore::FullRestoreSaveHandler::GetInstance();
  full_restore_save_handler->InsertIgnoreApplicationId(ash::kOsFeedbackAppId);

  PrefService* prefs = profile_->GetPrefs();
  DCHECK(prefs);

  pref_change_registrar_.Init(prefs);
  pref_change_registrar_.Add(
      prefs::kRestoreAppsAndPagesPrefName,
      base::BindRepeating(&FullRestoreService::OnPreferenceChanged,
                          weak_ptr_factory_.GetWeakPtr()));

  const user_manager::User* user =
      ProfileHelper::Get()->GetUserByProfile(profile_);
  if (user) {
    ::app_restore::AppRestoreInfo::GetInstance()->SetRestorePref(
        user->GetAccountId(), CanPerformRestore(prefs));
  }

  // Set profile path before init the restore process to create
  // FullRestoreSaveHandler to observe restore windows.
  if (IsPrimaryUser(profile_)) {
    full_restore_save_handler->SetPrimaryProfilePath(profile_->GetPath());

    // In Multi-Profile mode, only set for the primary user. For other users,
    // active profile path is set when switch users.
    ::full_restore::SetActiveProfilePath(profile_->GetPath());

    can_be_inited_ = CanBeInited();
  }

  if (!HasRestorePref(prefs) && HasSessionStartupPref(prefs)) {
    // If there is no full restore pref, but there is a session restore setting,
    // set the first run flag to maintain the previous behavior for the first
    // time running the full restore feature when migrate to the full restore
    // release. Restore browsers and web apps by the browser session restore.
    first_run_full_restore_ = true;
    SetDefaultRestorePrefIfNecessary(prefs);
    full_restore_save_handler->AllowSave();
    VLOG(1) << "No restore pref! First time to run full restore."
            << profile_->GetPath();
  }

  // In some unit tests, there may not be a shell instance and session
  // controller.
  if (auto* session_controller = SessionController::Get()) {
    session_controller->AddObserver(this);
  }
}

FullRestoreService::~FullRestoreService() {
  if (auto* session_controller = SessionController::Get()) {
    session_controller->RemoveObserver(this);
  }
}

// static
void FullRestoreService::SetLastSessionSanitized() {
  g_last_session_sanitized = true;
}

void FullRestoreService::Init(bool& show_notification) {
  // If it is the first time to migrate to the full restore release, we don't
  // have other restore data, so we don't need to consider restoration.
  if (first_run_full_restore_)
    return;

  // If the user of `profile_` is not the primary user, and hasn't been the
  // active user yet, we don't need to consider restoration to prevent the
  // restored windows are written to the active user's profile path.
  if (!can_be_inited_)
    return;

  // If the restore data has not been loaded, wait for it. For test cases,
  // `app_launch_handler_` might be reset as null because test cases might be
  // finished before Init is called, so check `app_launch_handler_` to prevent
  // crash for test cases.
  if (!app_launch_handler_ || !app_launch_handler_->IsRestoreDataLoaded())
    return;

  if (is_shut_down_)
    return;

  PrefService* prefs = profile_->GetPrefs();
  DCHECK(prefs);

  // Determine whether we should show the update string. Crash takes priority
  // over update but we do the computations to store the pref for the next
  // session here first. The pref may not be registered in certain unit tests.
  bool is_update = false;
  if (prefs->HasPrefPath(prefs::kInformedRestoreLastVersion)) {
    const base::Version old_version(
        prefs->GetString(prefs::kInformedRestoreLastVersion));
    const base::Version current_version = version_info::GetVersion();
    prefs->SetString(prefs::kInformedRestoreLastVersion,
                     current_version.GetString());
    is_update = old_version.IsValid() && current_version > old_version;
  }

  if (ExitTypeService::GetLastSessionExitType(profile_) == ExitType::kCrashed) {
    if (!HasRestorePref(prefs))
      SetDefaultRestorePrefIfNecessary(prefs);

    // TODO(crbug.com/388309832): Determine if we should show a notification for
    // crashes if always or never restore setting is set for forest.
    if (!IsAskEveryTime(prefs)) {
      return;
    }

    // If the system crashed before reboot, show the crash notification.
    MaybeShowRestoreDialog(InformedRestoreContentsData::DialogType::kCrash,
                           show_notification);
    return;
  }

  // If either OS pref setting nor Chrome pref setting exist, that means we
  // don't have restore data, so we don't need to consider restoration, and call
  // NewUserRestorePrefHandler to set OS pref setting.
  if (!HasRestorePref(prefs) && !HasSessionStartupPref(prefs)) {
    new_user_pref_handler_ =
        std::make_unique<NewUserRestorePrefHandler>(profile_);
    ::full_restore::FullRestoreSaveHandler::GetInstance()->AllowSave();
    MaybeInitiateAdminTemplateAutoLaunch();
    return;
  }

  const RestoreOption restore_pref = static_cast<RestoreOption>(
      prefs->GetInteger(prefs::kRestoreAppsAndPagesPrefName));
  base::UmaHistogramEnumeration(kRestoreInitSettingHistogramName, restore_pref);

  ::app_restore::RestoreData* restore_data =
      app_launch_handler_->restore_data();

  // Record the window count from the full restore file, unless the option is do
  // not restore.
  if (restore_pref != RestoreOption::kDoNotRestore) {
    if (!restore_data) {
      base::UmaHistogramCounts100(kFullRestoreWindowCountHistogramName, 0);
    } else {
      auto [window_count, tab_count, total_count] =
          ::app_restore::GetWindowAndTabCount(*restore_data);
      base::UmaHistogramCounts100(kFullRestoreWindowCountHistogramName,
                                  window_count);
    }
  }

  switch (restore_pref) {
    case RestoreOption::kAlways: {
      Restore();
      break;
    }
    case RestoreOption::kAskEveryTime: {
      const auto dialog_type =
          is_update ? InformedRestoreContentsData::DialogType::kUpdate
                    : InformedRestoreContentsData::DialogType::kNormal;
      MaybeShowRestoreDialog(dialog_type, show_notification);
      MaybeInitiateAdminTemplateAutoLaunch();
      break;
    }
    case RestoreOption::kDoNotRestore: {
      MaybeShowInformedRestoreOnboarding(/*restore_on=*/false);
      ::full_restore::FullRestoreSaveHandler::GetInstance()->AllowSave();
      MaybeInitiateAdminTemplateAutoLaunch();
      return;
    }
  }
}

void FullRestoreService::OnTransitionedToNewActiveUser(Profile* profile) {
  const bool already_initialized = can_be_inited_;
  if (profile_ != profile || already_initialized)
    return;

  can_be_inited_ = true;
  bool show_notification = false;
  Init(show_notification);
}

void FullRestoreService::LaunchBrowserWhenReady() {
  TRACE_EVENT0("ui", "FullRestoreService::LaunchBrowserWhenReady");
  if (!g_restore_for_testing || !app_launch_handler_)
    return;

  app_launch_handler_->LaunchBrowserWhenReady(first_run_full_restore_);
}

void FullRestoreService::MaybeCloseNotification(bool allow_save) {
  close_notification_ = true;
  VLOG(1) << "The full restore notification is closed for "
          << profile_->GetPath();

  // The crash notification creates a crash lock for the browser session
  // restore. So if the notification has been closed and the system is no longer
  // crash, clear `crashed_lock_`. Otherwise, the crash flag might not be
  // cleared, and the crash notification might be shown again after the normal
  // shutdown process.
  crashed_lock_.reset();

  if (allow_save) {
    // If the user launches an app or clicks the cancel button, start the save
    // timer.
    ::full_restore::FullRestoreSaveHandler::GetInstance()->AllowSave();
  }
}

void FullRestoreService::Restore() {
  if (app_launch_handler_)
    app_launch_handler_->SetShouldRestore();
}

void FullRestoreService::OnSessionStateChanged(
    session_manager::SessionState state) {
  if (!contents_data_) {
    return;
  }

  // Start post-login session right after signing in.
  if (state == session_manager::SessionState::ACTIVE) {
    delegate_->MaybeStartInformedRestoreOverviewSession(
        std::move(contents_data_));
  }
}

void FullRestoreService::SetAppLaunchHandlerForTesting(
    std::unique_ptr<FullRestoreAppLaunchHandler> app_launch_handler) {
  app_launch_handler_ = std::move(app_launch_handler);
}

void FullRestoreService::Shutdown() {
  is_shut_down_ = true;
}

bool FullRestoreService::CanBeInited() const {
  auto* user_manager = user_manager::UserManager::Get();
  DCHECK(user_manager);
  DCHECK(user_manager->GetActiveUser());

  // For non-primary user, wait for `OnTransitionedToNewActiveUser`.
  auto* user = ProfileHelper::Get()->GetUserByProfile(profile_);
  if (user != user_manager->GetPrimaryUser()) {
    VLOG(1) << "Can't init full restore service for non_primary user."
            << profile_->GetPath();
    return false;
  }

  // Check the command line to decide whether this is the restart case.
  // `kLoginManager` means starting Chrome with login/oobe screen, not the
  // restart process. For the restart process, `kLoginUser` should be in the
  // command line.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  DCHECK(command_line);
  if (command_line->HasSwitch(switches::kLoginManager) ||
      !command_line->HasSwitch(switches::kLoginUser)) {
    return true;
  }

  // When the system restarts, and the active user in the previous session is
  // not the primary user, don't init, but wait for the transition to the last
  // active user.
  const auto& last_session_active_account_id =
      user_manager->GetLastSessionActiveAccountId();
  if (last_session_active_account_id.is_valid() &&
      AccountId::FromUserEmail(user->GetAccountId().GetUserEmail()) !=
          last_session_active_account_id) {
    VLOG(1) << "Can't init full restore service for non-active primary user."
            << profile_->GetPath();
    return false;
  }

  return true;
}

void FullRestoreService::InitInformedRestoreContentsData(
    InformedRestoreContentsData::DialogType dialog_type) {
  CHECK(app_launch_handler_->HasRestoreData());

  contents_data_ = std::make_unique<InformedRestoreContentsData>();
  contents_data_->dialog_type = dialog_type;

  contents_data_->restore_callback = base::BindOnce(
      &FullRestoreService::OnDialogRestore, weak_ptr_factory_.GetWeakPtr());
  contents_data_->cancel_callback = base::BindOnce(
      &FullRestoreService::OnDialogCancel, weak_ptr_factory_.GetWeakPtr());

  // Contains per-window app data to be sorted and and added to
  // `contents_data_`.
  struct WindowAppData {
    int window_id;
    std::string app_id;
    raw_ptr<::app_restore::AppRestoreData> app_restore_data;
  };

  // Retrieve app id's from `restore_data`. There can be multiple entries with
  // the same app id, these denote different windows.
  auto* restore_data = app_launch_handler_->restore_data();
  std::vector<WindowAppData> complete_window_list;
  for (const auto& [app_id, launch_list] :
       restore_data->app_id_to_launch_list()) {
    for (const std::pair<const int,
                         std::unique_ptr<::app_restore::AppRestoreData>>&
             id_data_pair : launch_list) {
      complete_window_list.emplace_back(id_data_pair.first, app_id,
                                        id_data_pair.second.get());
    }
  }

  // Sort the windows based on their activation index (more recent windows
  // have a lower index). Windows without an activation index can be placed at
  // the end.
  std::ranges::sort(complete_window_list, [](const WindowAppData& element_a,
                                             const WindowAppData& element_b) {
    return element_a.app_restore_data->window_info.activation_index.value_or(
               INT_MAX) <
           element_b.app_restore_data->window_info.activation_index.value_or(
               INT_MAX);
  });

  for (auto info : complete_window_list) {
    const std::string stored_title =
        base::UTF16ToUTF8(info.app_restore_data->window_info.app_title.value_or(
            std::u16string()));
    contents_data_->apps_infos.emplace_back(info.app_id, stored_title,
                                            info.window_id);
  }
}

void FullRestoreService::MaybeShowRestoreDialog(
    InformedRestoreContentsData::DialogType dialog_type,
    bool& out_show_notification) {
  if (g_last_session_sanitized) {
    return;
  }

  if (!app_launch_handler_) {
    return;
  }

  // Do not show the notification if it is the first run or the notification is
  // being closed.
  if (::first_run::IsChromeFirstRun() || close_notification_) {
    return;
  }

  const bool last_session_crashed =
      dialog_type == InformedRestoreContentsData::DialogType::kCrash;

  if (last_session_crashed &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          kForceFullRestoreAndSessionRestoreAfterCrash)) {
    LOG(WARNING) << "Full session restore was forced by a debug flag.";
    Restore();
    return;
  }

  if (!app_launch_handler_->HasRestoreData()) {
    MaybeShowInformedRestoreOnboarding(/*restore_on=*/true);
    return;
  }
  CHECK(app_launch_handler_->HasRestoreData());

  // If the system is restored from crash, create the crash lock for the browser
  // session restore to help set the browser saving flag.
  ExitTypeService* exit_type_service =
      ExitTypeService::GetInstanceForProfile(profile_);
  if (last_session_crashed && exit_type_service) {
    crashed_lock_ = exit_type_service->CreateCrashedLock();
  }

  if (Shell::HasInstance()) {
    Shell::Get()
        ->post_login_glanceables_metrics_reporter()
        ->RecordPostLoginFullRestoreShown();
  }

  CHECK(delegate_);

  InitInformedRestoreContentsData(dialog_type);

  // Retrieves session service data from browser and app browsers, which
  // will be used to display favicons and tab titles.
  SessionServiceBase* service =
      SessionServiceFactory::GetForProfileForSessionRestore(profile_);
  SessionServiceBase* app_service =
      AppSessionServiceFactory::GetForProfileForSessionRestore(profile_);
  if (service && app_service) {
    auto barrier = base::BarrierCallback<SessionWindows>(
        /*num_callbacks=*/2u, /*done_callback=*/base::BindOnce(
            &FullRestoreService::OnGotAllSessionsAsh,
            weak_ptr_factory_.GetWeakPtr()));

    service->GetLastSession(base::BindOnce(&FullRestoreService::OnGotSessionAsh,
                                           weak_ptr_factory_.GetWeakPtr(),
                                           barrier));
    app_service->GetLastSession(
        base::BindOnce(&FullRestoreService::OnGotSessionAsh,
                       weak_ptr_factory_.GetWeakPtr(), barrier));
  } else {
    OnGotAllSessionsAsh(/*all_session_windows=*/{});
  }

  // Set to true as we might want to show the post reboot notification.
  out_show_notification = true;
}

void FullRestoreService::OnPreferenceChanged(const std::string& pref_name) {
  DCHECK_EQ(pref_name, prefs::kRestoreAppsAndPagesPrefName);

  RestoreOption restore_option = static_cast<RestoreOption>(
      profile_->GetPrefs()->GetInteger(prefs::kRestoreAppsAndPagesPrefName));
  base::UmaHistogramEnumeration(kRestoreSettingHistogramName, restore_option);

  const user_manager::User* user =
      ProfileHelper::Get()->GetUserByProfile(profile_);
  if (user) {
    ::app_restore::AppRestoreInfo::GetInstance()->SetRestorePref(
        user->GetAccountId(), CanPerformRestore(profile_->GetPrefs()));
  }
}

void FullRestoreService::OnAppTerminating() {
  if (auto* arc_task_handler =
          app_restore::AppRestoreArcTaskHandlerFactory::GetForProfile(
              profile_)) {
    arc_task_handler->Shutdown();
  }
  app_launch_handler_.reset();
  ::full_restore::FullRestoreSaveHandler::GetInstance()->SetShutDown();
}

void FullRestoreService::OnDialogRestore() {
  VLOG(1) << "The restore button is clicked for " << profile_->GetPath();

  Restore();
  delegate_->MaybeEndInformedRestoreOverviewSession();
}

void FullRestoreService::OnDialogCancel() {
  ::full_restore::FullRestoreSaveHandler::GetInstance()->AllowSave();
  delegate_->MaybeEndInformedRestoreOverviewSession();
}

void FullRestoreService::OnGotSessionAsh(
    base::OnceCallback<void(SessionWindows)> callback,
    SessionWindows session_windows,
    SessionID active_window_id,
    bool read_error) {
  std::move(callback).Run(std::move(session_windows));
}

void FullRestoreService::OnGotAllSessionsAsh(
    const std::vector<SessionWindows>& all_session_windows) {
  // Place all the session windows in map so we don't have to do so many O(n)
  // lookups below.
  SessionWindowsMap session_windows_map;
  for (const SessionWindows& session_windows : all_session_windows) {
    for (const std::unique_ptr<sessions::SessionWindow>& session_window :
         session_windows) {
      session_windows_map.emplace(session_window->window_id.id(),
                                  session_window.get());
    }
  }

  OnSessionInformationReceived(session_windows_map);
}

void FullRestoreService::OnSessionInformationReceived(
    const SessionWindowsMap& session_windows_map) {
  auto* contents_data = contents_data_
                            ? contents_data_.get()
                            : delegate_->GetInformedRestoreContentData();

  // It is possible the user clicks restore or cancel before fetching the
  // session restore data is complete. In this case, there's no need to update
  // anything so we can just bail out here. See http://b/365844258 for more
  // details.
  if (!contents_data) {
    return;
  }

  bool content_updated = false;
  for (auto& info : contents_data->apps_infos) {
    const std::string app_id = info.app_id;
    const int window_id = info.window_id;

    // For non browsers, the app id and title is sufficient for the UI we want
    // to display.
    if (app_id != app_constants::kChromeAppId) {
      continue;
    }

    // Find the `sessions::SessionWindow` associated with `window_id` if it
    // exists.
    auto it = session_windows_map.find(window_id);

    sessions::SessionWindow* session_window =
        it == session_windows_map.end() ? nullptr : it->second;

    // Default to using the app id if we cannot find the associated window for
    // whatever reason.
    if (!session_window) {
      continue;
    }

    content_updated = true;

    // App browsers app ID is the same as regular chrome browsers. To get the
    // correct icon and title from the app service, we need to find the app
    // name and remove the "_crx_", then use that result.
    const std::string app_name = session_window->app_name;
    if (!app_name.empty()) {
      const std::string new_app_id =
          ::app_restore::GetAppIdFromAppName(app_name);
      if (!new_app_id.empty()) {
        info.app_id = new_app_id;
      }
      continue;
    }

    // If there is no selected tab index or it is invalid, we can just pass the
    // URLs as they are. If the selected tab index is one of the first five
    // elements, then we place that URL at the front and place the remaining
    // four URLs afterwards. Otherwise, we put the selected tab index at the
    // front and insert the first four URLs after it.
    std::string active_tab_title;
    const std::vector<std::unique_ptr<sessions::SessionTab>>& tabs =
        session_window->tabs;
    std::vector<InformedRestoreContentsData::TabInfo> tab_infos;
    tab_infos.reserve(tabs.size());

    auto maybe_add_display_tab =
        [&tab_infos, &active_tab_title](sessions::SessionTab* tab) -> void {
      const auto& navigations = tab->navigations;
      const int index = tab->current_navigation_index;

      // `index` can actually be larger than the size of `navigations`. See
      // `sessions::SessionTab::current_navigation_index` for more details.
      if (navigations.size() > static_cast<size_t>(index)) {
        const sessions::SerializedNavigationEntry& entry = navigations[index];

        // Use the tab title if possible. If no tab title is available and it is
        // a chrome WebUI, use the host piece (history, extensions, etc.).
        // Otherwise we will use the formatted url as tab title.
        std::string tab_title = base::UTF16ToUTF8(entry.title());
        const GURL& url = entry.original_request_url();
        const GURL& virtual_url = entry.virtual_url();
        if (tab_title.empty()) {
          if (url.SchemeIs(content::kChromeUIScheme)) {
            tab_title = url.host_piece();
          } else {
            tab_title = base::UTF16ToUTF8(url_formatter::FormatUrl(
                virtual_url.is_empty() ? url : virtual_url,
                url_formatter::kFormatUrlOmitDefaults |
                    url_formatter::kFormatUrlOmitTrivialSubdomains |
                    url_formatter::kFormatUrlOmitHTTPS,
                base::UnescapeRule::SPACES, nullptr, nullptr, nullptr));
          }
        }

        if (active_tab_title.empty()) {
          active_tab_title = tab_title;
        }

        tab_infos.emplace_back(url, virtual_url, tab_title);
      }
    };

    // Add the selected tab first if possible.
    const int selected_tab_index = session_window->selected_tab_index;
    if (selected_tab_index > -1 &&
        selected_tab_index < static_cast<int>(tabs.size())) {
      maybe_add_display_tab(tabs[selected_tab_index].get());
    }

    // Add the other tabs in order until there are no more tabs or we reach the
    // limit.
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
      if (i == selected_tab_index) {
        continue;
      }
      maybe_add_display_tab(tabs[i].get());
    }

    info = InformedRestoreContentsData::AppInfo(
        app_id, active_tab_title, window_id, std::move(tab_infos));
  }

  // Start the post-login session if not yet and pass the contents data to
  // post-login controller.
  if (contents_data_) {
    delegate_->MaybeStartInformedRestoreOverviewSession(
        std::move(contents_data_));
    return;
  }

  // Notify the contents data updated when the data was sent to informed dialog
  // and there are items updated.
  if (!contents_data_ && content_updated) {
    delegate_->OnInformedRestoreContentsDataUpdated();
  }
}

void FullRestoreService::MaybeShowInformedRestoreOnboarding(bool restore_on) {
  if (!Shell::HasInstance()) {
    return;
  }

  if (profile_->IsNewProfile()) {
    return;
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kNoFirstRun)) {
    return;
  }

  auto* informed_restore_controller =
      Shell::Get()->informed_restore_controller();
  CHECK(informed_restore_controller);
  informed_restore_controller->MaybeShowInformedRestoreOnboarding(restore_on);
}

ScopedRestoreForTesting::ScopedRestoreForTesting() {
  g_restore_for_testing = false;
}

ScopedRestoreForTesting::~ScopedRestoreForTesting() {
  g_restore_for_testing = true;
}

}  // namespace ash::full_restore
