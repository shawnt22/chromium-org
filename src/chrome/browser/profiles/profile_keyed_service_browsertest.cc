// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <sstream>

#include "base/containers/to_vector.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/navigation_predictor/search_engine_preconnector.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/profile_waiter.h"
#include "components/commerce/core/commerce_feature_list.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/core/dependency_graph.h"
#include "components/keyed_service/core/keyed_service_base_factory.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/optimization_guide/machine_learning_tflite_buildflags.h"
#include "components/signin/public/base/signin_switches.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/extension_features.h"
#include "net/base/features.h"
#include "pdf/buildflags.h"
#include "services/network/public/cpp/features.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "third_party/blink/public/common/features.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/base/ui_base_features.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/constants/ash_switches.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "components/user_manager/user_manager.h"
#include "components/user_manager/user_names.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

namespace {
#if !BUILDFLAG(IS_CHROMEOS)
// Creates a Profile and its underlying OTR Profile for testing.
// Waits for all tasks to be done to get as many services created as possible.
// Returns the Original Profile.
Profile* CreateProfileAndWaitForAllTasks(const base::FilePath& profile_path) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ProfileWaiter profile_waiter;
  profile_manager->CreateProfileAsync(profile_path, {});
  Profile* profile = profile_waiter.WaitForProfileAdded();
  // Wait for Profile creation, and potentially other services that will be
  // created after all tasks are done.
  content::RunAllTasksUntilIdle();
  return profile;
}
#endif

// Gets all the KeyedServices from the DependencyGraph.
std::vector<KeyedServiceBaseFactory*> GetKeyedServiceBaseFactories() {
  BrowserContextDependencyManager* dependency_manager =
      BrowserContextDependencyManager::GetInstance();
  DependencyGraph& dependency_graph =
      dependency_manager->GetDependencyGraphForTesting();
  std::vector<raw_ptr<DependencyNode, VectorExperimental>> nodes;
  bool success = dependency_graph.GetConstructionOrder(&nodes);
  DCHECK(success);

  return base::ToVector(nodes, [](DependencyNode* node) {
    return static_cast<KeyedServiceBaseFactory*>(node);
  });
}

// Returns a string representation of the elements of `set1` which are absent
// from `set2`.
std::string GetDifferenceString(const std::set<std::string>& set1,
                                const std::set<std::string>& set2) {
  std::vector<std::string> differences;
  std::ranges::set_difference(set1, set2, std::back_inserter(differences));

  return differences.empty() ? "None" : base::JoinString(differences, ", ");
}

// Helper function to properly display differences between expected and reached
// service names.
std::string DisplaySetDifference(
    const std::set<std::string>& expected_active_services_names,
    const std::set<std::string>& active_services_names) {
  std::stringstream error;
  error << "Differences between expected and reached services:" << std::endl;

  error << "-- Missing Expected Services:" << std::endl;
  error << GetDifferenceString(expected_active_services_names,
                               active_services_names)
        << std::endl;

  error << "-- Added Extra Services:" << std::endl;
  error << GetDifferenceString(active_services_names,
                               expected_active_services_names)
        << std::endl;

  return error.str();
}

// The test comparing expected vs reached keyed services for the given profile.
void TestKeyedProfileServicesActives(
    Profile* profile,
    const std::set<std::string>& expected_active_services_names,
    bool force_create_services = false,
    const base::Location& location = FROM_HERE) {
  const std::vector<KeyedServiceBaseFactory*> keyedServiceFactories =
      GetKeyedServiceBaseFactories();

  if (force_create_services) {
    for (KeyedServiceBaseFactory* factory : keyedServiceFactories) {
      factory->CreateServiceNowForTesting(profile);
    }
  }

  std::set<std::string> active_services_names;
  for (KeyedServiceBaseFactory* factory : keyedServiceFactories) {
    if (factory->IsServiceCreated(profile)) {
      active_services_names.emplace(factory->name());
    }
  }

  EXPECT_EQ(active_services_names, expected_active_services_names)
      << DisplaySetDifference(expected_active_services_names,
                              active_services_names)
      << ", expected at " << location.ToString();
}

}  // namespace

TEST(ProfileKeyedService_DisplaySetDifferenceTest, UnexpectedActiveService) {
  std::string message =
      DisplaySetDifference(/*expected_active_services_names=*/{},
                           /*active_services_names=*/{"unexpected"});
  EXPECT_THAT(message,
              testing::ContainsRegex("Missing Expected Services:\\s+None"));
  EXPECT_THAT(message,
              testing::ContainsRegex("Added Extra Services:\\s+unexpected"));
}

TEST(ProfileKeyedService_DisplaySetDifferenceTest, MissingExpectedService) {
  std::string message =
      DisplaySetDifference(/*expected_active_services_names=*/{"missing"},
                           /*active_services_names=*/{});
  EXPECT_THAT(message,
              testing::ContainsRegex("Missing Expected Services:\\s+missing"));
  EXPECT_THAT(message, testing::ContainsRegex("Added Extra Services:\\s+None"));
}

// If you are adding a new keyed service and this test fails:
// - determine if your service is intended to be created for the System profile
// - if yes, add it to the list of allowed services
// - if not, update your factory class so that the service is not created for
// the system profile.
//
// Note: if your service should not be used on the system profile, but still has
// to, because other services depend on it, add a comment explaining why.
// Example:
//   // FooService is required because BarService depends on it.
//   // TODO(crbug.com/40781525): Stop creating BarService for the system
//   profile.
class ProfileKeyedServiceBrowserTest : public InProcessBrowserTest {
 public:
  ProfileKeyedServiceBrowserTest() {
    // Force features activation to make sure the test is accurate as possible.
    // Also removes differences between official and non official run of the
    // tests.
    //
    // If a feature is integrated in the fieldtrial_testing_config.json,
    // it might not be considered under an official build. Adding it under the
    // InitWithFeatures below, to activate it, will solve that difference.

    // clang-format off
    feature_list_.InitWithFeatures(
        {
          features::kTrustSafetySentimentSurvey,
#if BUILDFLAG(IS_WIN)
          switches::kEnableBoundSessionCredentials,
#endif  // BUILDFLAG(IS_WIN)
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
          features::kEnableCertManagementUIV2Write,
#endif
          network::features::kBrowsingTopics,
          blink::features::kBuiltInAIAPI,
          extensions_features::kForceWebRequestProxyForTest,
          net::features::kTopLevelTpcdOriginTrial,
          net::features::kTpcdTrialSettings,
          net::features::kTopLevelTpcdTrialSettings,
          network::features::kReduceAcceptLanguage,
          features::kMainNodeAnnotations,
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
          omnibox::kOnDeviceTailModel,
          omnibox::kOnDeviceHeadProviderNonIncognito,
#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)
          switches::kSyncEnableBookmarksInTransportMode,
        },
        {});
    // clang-format on
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

#if !BUILDFLAG(IS_CHROMEOS)
// System Profile does not exist on ChromeOS.
IN_PROC_BROWSER_TEST_F(ProfileKeyedServiceBrowserTest,
                       SystemProfileOTR_NeededServices) {
  Profile* system_profile =
      CreateProfileAndWaitForAllTasks(ProfileManager::GetSystemProfilePath());
  ASSERT_TRUE(system_profile->HasAnyOffTheRecordProfile());
  Profile* system_profile_otr = system_profile->GetPrimaryOTRProfile(false);
  ASSERT_TRUE(system_profile_otr->IsOffTheRecord());
  ASSERT_TRUE(system_profile_otr->IsSystemProfile());
  TestKeyedProfileServicesActives(system_profile_otr,
                                  /*expected_active_services_names=*/{});
}

IN_PROC_BROWSER_TEST_F(ProfileKeyedServiceBrowserTest,
                       SystemProfileParent_NeededServices) {
  Profile* system_profile =
      CreateProfileAndWaitForAllTasks(ProfileManager::GetSystemProfilePath());
  ASSERT_FALSE(system_profile->IsOffTheRecord());
  ASSERT_TRUE(system_profile->IsSystemProfile());
  TestKeyedProfileServicesActives(system_profile,
                                  /*expected_active_services_names=*/{});
}

IN_PROC_BROWSER_TEST_F(ProfileKeyedServiceBrowserTest,
                       SystemProfileParent_ServicesThatCanBeCreated) {
  Profile* system_profile =
      CreateProfileAndWaitForAllTasks(ProfileManager::GetSystemProfilePath());
  ASSERT_FALSE(system_profile->IsOffTheRecord());
  ASSERT_TRUE(system_profile->IsSystemProfile());

  // clang-format off
  std::set<std::string> exepcted_created_services_names = {
    // in components:
    // There is no control over the creation based on the Profile types in
    // components/. These services are not created for the System Profile by
    // default, however their creation is still possible.
    "AutocompleteControllerEmitter",
    "AutofillInternalsService",
    "DataControlsRulesService",
    "HasEnrolledInstrumentQuery",
    "LocalPresentationManager",
    "OmniboxInputWatcher",
    "OmniboxSuggestionsWatcher",
    "PasswordManagerInternalsService",
    "PasswordRequirementsServiceFactory",
    "PolicyBlocklist",
    "PolicyClipboardRestriction",
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ReportingEventRouter",
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "SafeSearch",
    "WebDataService",

    // in chrome: using `BrowserContextKeyedServiceShutdownNotifierFactory`:
    // which does not yet have an implementation using `ProfileSelections`.
    "GalleryWatchManager",
    "MediaFileSystemRegistry",
    "NotificationDisplayService",
    "PermissionsUpdaterShutdownFactory",
    "PluginInfoHostImpl",
    "TurnSyncOnHelperShutdownNotifier",
  };
  // clang-format on

  TestKeyedProfileServicesActives(system_profile,
                                  exepcted_created_services_names,
                                  /*force_create_services=*/true);
}

IN_PROC_BROWSER_TEST_F(ProfileKeyedServiceBrowserTest,
                       SystemProfileOTR_ServicesThatCanBeCreated) {
  Profile* system_profile =
      CreateProfileAndWaitForAllTasks(ProfileManager::GetSystemProfilePath());
  ASSERT_TRUE(system_profile->HasAnyOffTheRecordProfile());
  Profile* system_profile_otr = system_profile->GetPrimaryOTRProfile(false);
  ASSERT_TRUE(system_profile_otr->IsOffTheRecord());
  ASSERT_TRUE(system_profile_otr->IsSystemProfile());

  // clang-format off
  std::set<std::string> exepcted_created_services_names = {
    // in components:
    // There is no control over the creation based on the Profile types in
    // components/. These services are not created for the System Profile by
    // default, however their creation is still possible.
    "AutocompleteControllerEmitter",
    "DataControlsRulesService",
    "HasEnrolledInstrumentQuery",
    "OmniboxInputWatcher",
    "OmniboxSuggestionsWatcher",
    "PolicyBlocklist",
    "PolicyClipboardRestriction",
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ReportingEventRouter",
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "SafeSearch",

    // in chrome: using `BrowserContextKeyedServiceShutdownNotifierFactory`:
    // which does not yet have an implementation using `ProfileSelections`.
    "GalleryWatchManager",
    "MediaFileSystemRegistry",
    "NotificationDisplayService",
    "PermissionsUpdaterShutdownFactory",
    "PluginInfoHostImpl",
    "TurnSyncOnHelperShutdownNotifier",

    // Those services are needed to be able to display IPHs in the Profile
    // Picker.
    "feature_engagement::Tracker",
    "UserEducationService",
  };
  // clang-format on

  TestKeyedProfileServicesActives(system_profile_otr,
                                  exepcted_created_services_names,
                                  /*force_create_services=*/true);
}

#endif  // !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS)
class ProfileKeyedServiceGuestBrowserTest
    : public ProfileKeyedServiceBrowserTest {
 public:
  ProfileKeyedServiceGuestBrowserTest() = default;
  ~ProfileKeyedServiceGuestBrowserTest() override = default;

  // ProfileKeyedServiceBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(ash::switches::kGuestSession);
    command_line->AppendSwitch(::switches::kIncognito);
    command_line->AppendSwitchASCII(ash::switches::kLoginProfile, "user");
    command_line->AppendSwitchASCII(
        ash::switches::kLoginUser,
        user_manager::GuestAccountId().GetUserEmail());
  }
};
#else
using ProfileKeyedServiceGuestBrowserTest = ProfileKeyedServiceBrowserTest;
#endif

IN_PROC_BROWSER_TEST_F(ProfileKeyedServiceGuestBrowserTest,
                       GuestProfileOTR_NeededServices) {
  // clang-format off
  std::set<std::string> guest_otr_active_services {
    "AlarmManager",
    "AXMainNodeAnnotatorController",
    "AutocompleteActionPredictor",
    "AutocompleteClassifier",
    "AutocompleteControllerEmitter",
    "AutocompleteHistoryManager",
    "BackgroundContentsService",
    "BackgroundSyncService",
    "BluetoothApiAdvertisementManager",
    "BluetoothApiSocketManager",
    "BluetoothLowEnergyConnectionManager",
    "BluetoothLowEnergyNotifySessionManager",
    "BluetoothSocketEventDispatcher",
    "BrowsingDataLifetimeManager",
    "BrowsingDataRemover",
    "CookieSettings",
#if BUILDFLAG(IS_WIN)
    "BoundSessionCookieRefreshService",
#endif  // BUILDFLAG(IS_WIN)
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ExtensionInstallEventRouter",
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ChromeEnterpriseRealTimeUrlLookupService",
#if BUILDFLAG(IS_CHROMEOS)
    "ComponentExtensionContentSettingsAllowlist",
#endif
    "EnterpriseReportingPrivateEventRouter",
    "ExtensionNavigationRegistry",
    "ExtensionSystem",
    "ExtensionURLLoaderFactory::BrowserContextShutdownNotifierFactory",
    "FederatedIdentityPermissionContext",
    "FederatedIdentityAutoReauthnPermissionContext",
    "FeedbackPrivateAPI",
    "FileSystemAccessPermissionContext",
    "GeneratedPrefs",
    "HeavyAdService",
#if BUILDFLAG(ENABLE_EXTENSIONS)
    "HidConnectionResourceManager",
#endif
    "HidDeviceManager",
    "HostContentSettingsMap",
    "LiveCaptionController",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "LiveTranslateController",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "MediaRouter",
    "MediaRouterUIService",
    "NotificationDisplayService",
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    "OnDeviceTailModelService",
#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    "OneTimePermissionsTrackerKeyedService",
    "OptimizationGuideKeyedService",
    "PermissionDecisionAutoBlocker",
    "PinnedToolbarActionsModel",
    "PlatformNotificationService",
    "PredictionModelHandlerProvider",
    "PrefWatcher",
    "PrivacySandboxSettings",
    "ProcessManager",
    "ProfileNetworkContextService",
    "RealtimeReportingClient",
    "ReduceAcceptLanguage",
    "RendererUpdater",
    "ResumableTCPServerSocketManager",
    "ResumableTCPSocketManager",
    "ResumableUDPSocketManager",
    "RulesRegistryService",
    "SafeBrowsingPrivateEventRouter",
    "SerialConnectionManager",
    "SerialPortManager",
    "SettingsPrivateEventRouter",
    "SiteDataCacheFacadeFactory",
    "SiteEngagementService",
    "SocketManager",
    "StorageNotificationService",
    "TCPServerSocketEventDispatcher",
    "TCPSocketEventDispatcher",
    "TabGroupsEventRouter",
    "ToolbarActionsModel",
    "TrackingProtectionSettings",
    "UDPSocketEventDispatcher",
    "UkmBackgroundRecorderService",
#if BUILDFLAG(IS_WIN)
    "UnexportableKeyService",
#endif  // BUILDFLAG(IS_WIN)
    "UsbDeviceManager",
    "UsbDeviceResourceManager",
    "sct_reporting::Factory",

    "BtmBrowserSigninDetector",
    "ClientHints",
    "ConnectorsService",
    "DataControlsRulesService",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): This is most likely unnesssary on CrOS because
    // it has its own UI, but factory is created, which should probably be
    // removed.
    "DownloadBubbleUpdateService",
#endif
    "EnterpriseManagementService",
    "FindBarState",
    "HistoryClustersService",
    "IbanManager",
    "InstantService",
    "LanguageDetectionModelService",
    "MediaEngagementServiceFactory",
    "MediaNotificationService",
    "MerchantPromoCodeManager",
    "NoStatePrefetchManager",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "OfflineItemModelManager",
#endif
    "OmniboxInputWatcher",
    "OpenerHeuristicService",
    "PermissionManagerFactory",
    "PrivacySandboxService",
    "SafeBrowsingNavigationObserverManager",
    "StatefulSSLHostStateDelegate",
    "StorageAccessAPIService",
    "SubresourceFilterProfileContext",
    "TpcdTrialService",
    "VerdictCacheManager",
    "WebRequestProxyingURLLoaderFactory",
    "captive_portal::CaptivePortalService",

#if BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946):
    // Verify these are necessary: then reorder or remove.

    "AppListSyncableService",
    "AppServiceProxy",
    "CupsPrintersManagerFactory",
    "DownloadCoreService",
    "EventRouter",
    "FileChangeService",
    "FileSuggestKeyedService",
    "HoldingSpaceService",
    "LogSourceResource",
    "PolicyCertService",
    "PrimaryProfileServices",
    "PrinterEventTracker",
    "SharesheetService",
    "SupervisedUserService",
    "SystemWebAppManager",
    "VirtualKeyboardAPI",
    "VolumeManagerFactory",
    "WebAppProvider",
#endif  // BUILDFLAG(IS_CHROMEOS)
  };
  // clang-format on
  if (SearchEnginePreconnector::ShouldBeEnabledAsKeyedService() &&
      SearchEnginePreconnector::ShouldBeEnabledForOffTheRecord()) {
    guest_otr_active_services.insert("SearchEnginePreconnector");
  }

#if BUILDFLAG(IS_CHROMEOS)
  EXPECT_TRUE(user_manager::UserManager::Get()->IsLoggedInAsGuest());
  // ChromeOS Guest mode starts with the guest otr profile.
  Profile* guest_otr_profile = browser()->profile();
  // Some key services are created asynchronosly. Wait util they're ready.
#else
  Browser* guest_browser = CreateGuestBrowser();
  Profile* guest_otr_profile = guest_browser->profile();
#endif  // BUILDFLAG(IS_CHROMEOS)
  content::RunAllTasksUntilIdle();

  ASSERT_FALSE(guest_otr_profile->IsRegularProfile());
  ASSERT_TRUE(guest_otr_profile->IsOffTheRecord());
  ASSERT_TRUE(guest_otr_profile->IsGuestSession());
  TestKeyedProfileServicesActives(guest_otr_profile, guest_otr_active_services);
}

IN_PROC_BROWSER_TEST_F(ProfileKeyedServiceGuestBrowserTest,
                       GuestProfileParent_NeededServices) {
  // clang-format off
  std::set<std::string> guest_active_services {
    "AccountBookmarkSyncServiceFactory",
    "AccountExtensionTracker",
    "ActivityLog",
    "ActivityLogPrivateAPI",
    "AdvancedProtectionStatusManager",
    "AlarmManager",
    "AnnouncementNotificationService",
    "AppLifetimeMonitor",
    "AppLoadService",
    "AppRestoreService",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "AppServiceProxy",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "AppSessionService",
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    "AppShortcutManager",
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)|| BUILDFLAG(IS_WIN)
    "ManualTestHeartbeatEvent",
#endif // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)|| BUILDFLAG(IS_WIN)
    "AppTerminationObserver",
    "AppWindowRegistry",
    "AudioAPI",
    "AutocompleteActionPredictor",
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    "AutocompleteScoringModelService",
#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    "AutofillClientProvider",
    "AutofillImageFetcher",
    "AutofillPrivateEventRouter",
    "AutofillStrikeDatabase",
    "BackgroundContentsService",
    "BackgroundFetchService",
    "BackgroundSyncService",
    "Blocklist",
    "BluetoothAPI",
    "BluetoothApiSocketManager",
    "BluetoothApiAdvertisementManager",
    "BluetoothLowEnergyAPI",
    "BluetoothLowEnergyConnectionManager",
    "BluetoothLowEnergyNotifySessionManager",
    "BluetoothPrivateAPI",
    "BluetoothSocketEventDispatcher",
    "BookmarkManagerPrivateAPI",
#if defined(TOOLKIT_VIEWS)
    "BookmarkExpandedStateTracker",
    "BookmarkMergedSurfaceService",
#endif
    "BookmarkModel",
    "BookmarkUndoService",
    "BookmarksAPI",
    "BrailleDisplayPrivateAPI",
    "BrowsingTopicsService",
    "ChildAccountService",
    "ChromeSigninClient",
    "CommandService",
#if BUILDFLAG(IS_CHROMEOS)
    "ComponentExtensionContentSettingsAllowlist",
#endif
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
    "ComponentLoader",
#endif
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ConnectorsService",
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ContentIndexProvider",
    "ContentSettingsService",
    "CookieSettings",
    "CookiesAPI",
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
    "CorruptedExtensionReinstaller",
#endif
    "CWSInfoService",
    "DataTypeStoreService",
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
    "DelayedInstallManager",
#endif
    "DeveloperPrivateAPI",
    "DeviceInfoSyncService",
    "DownloadCoreService",
    "EventRouter",
    "EnterpriseManagementService",
    "ExtensionActionDispatcher",
    "ExtensionActionManager",
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
    "ExtensionAllowlist",
#endif
    "ExtensionCommandsGlobalRegistry",
    "ExtensionErrorController",
    "ExtensionGCMAppHandler",
    "ExtensionGarbageCollector",
    "ExtensionHostRegistry",
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ExtensionInstallEventRouter",
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    "ExtensionManagement",
    "ExtensionPrefValueMap",
    "ExtensionPrefs",
    "ExtensionRegistrar",
    "ExtensionRegistry",
    "ExtensionSyncService",
    "ExtensionSystem",
    "ExtensionSystemShared",
    "ExtensionUpdater",
    "ExtensionURLLoaderFactory::BrowserContextShutdownNotifierFactory",
    "ExtensionWebUIOverrideRegistrar",
    "ExternalInstallManager",
  #if BUILDFLAG(ENABLE_EXTENSIONS)
    "ExternalProviderManager",
  #endif
    "FaviconService",
    "FederatedIdentityPermissionContext",
    "FederatedIdentityAutoReauthnPermissionContext",
    "FeedbackPrivateAPI",
    "FileSystemAccessPermissionContext",
    "FirstPartySetsPolicyService",
    "FontPrefChangeNotifier",
    "FontSettingsAPI",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "GAIAInfoUpdateService",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "GCMProfileService",
    "GeneratedPrefs",
    "GlobalErrorService",
    "HeavyAdService",
#if BUILDFLAG(ENABLE_EXTENSIONS)
    "HidConnectionResourceManager",
    "ExtensionNavigationRegistry",
#endif
    "HidDeviceManager",
    "HistoryAPI",
    "HistoryService",
    "HostContentSettingsMap",
    "HttpEngagementKeyService",
    "IdentityAPI",
    "IdentityManager",
    "IdleManager",
    "InMemoryURLIndex",
    "InstallStageTracker",
    "InstallTracker",
    "InstallVerifier",
    "InstanceIDProfileService",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "InvalidationService",
#endif
    "LanguageModelManager",
    "LanguageSettingsPrivateDelegate",
    "LazyBackgroundTaskQueue",
    "ListFamilyMembersService",
    "LocalOrSyncableBookmarkSyncServiceFactory",
    "LoginDetectionKeyedService",
    "LoginUIServiceFactory",
    "MDnsAPI",
    "ManagedBookmarkService",
    "ManagedConfigurationAPI",
    "ManagementAPI",
#if BUILDFLAG(ENABLE_EXTENSIONS)
    "ManifestV2ExperimentManager",
#endif
    "MediaGalleriesAPI",
    "MediaRouter",
    "MediaRouterUIService",
    "MenuManager",
    "NavigationPredictorKeyedService",
    "NetworkingPrivateEventRouter",
    "NotificationDisplayService",
    "NtpBackgroundService",
    "NtpCustomBackgroundService",
#if BUILDFLAG(IS_CHROMEOS)
    "NssServiceFactory",
#endif // BUILDFLAG(IS_CHROMEOS)
    "OmniboxAPI",
    "OmniboxSuggestionsWatcher",
#if BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    "OnDeviceTailModelService",
#endif  // BUILDFLAG(BUILD_WITH_TFLITE_LIB)
    "OneTimePermissionsTrackerKeyedService",
    "OperationManager",
    "OptimizationGuideKeyedService",
    "OriginTrialService",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "PageContentAnnotationsService",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "PasswordsPrivateEventRouter",
    "PendingExtensionManager",
    "PermissionDecisionAutoBlocker",
    "PermissionHelper",
#if BUILDFLAG(ENABLE_EXTENSIONS)
    "PermissionsEventRouter",
#endif
    "PermissionsManager",
    "PermissionsUpdaterShutdownFactory",
    "PersonalDataManager",
    "PinnedTabService",
    "PinnedToolbarActionsModel",
    "PlatformNotificationService",
    "PluginManager",
    "PluginPrefs",
    "PowerBookmarkService",
    "PredictionModelHandlerProvider",
    "PredictorDatabase",
    "PrefWatcher",
    "PreferenceAPI",
    "PrinterProviderInternal",
    "PrivacySandboxService",
    "PrivacySandboxSettings",
    "ProcessManager",
    "ProcessMap",
    "ProcessesAPI",
    "ProfileNetworkContextService",
    "ProtocolHandlerRegistry",
    "RealtimeReportingClient",
    "RegionalCapabilitiesService",
    "RendererStartupHelper",
    "RendererUpdater",
    "ResumableTCPServerSocketManager",
    "ResumableTCPSocketManager",
    "ResumableUDPSocketManager",
    "RulesMonitorService",
    "RulesRegistryService",
    "RuntimeAPI",
    "SafeBrowsingMetricsCollector",
    "SafeBrowsingPrivateEventRouter",
    "SafeBrowsingTailoredSecurityService",
    "SearchEngineChoiceServiceFactory",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "SendTabToSelfClientService",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "SendTabToSelfSyncService",
    "SerialConnectionManager",
    "SerialPortManager",
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    "ServerCertificateDatabaseService",
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    "SessionDataService",
    "SessionProtoDBFactory",
    "SessionsAPI",
    "sessions::TabRestoreService",
    "SettingsOverridesAPI",
    "SettingsPrivateEventRouter",
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
    "SharedModuleService",
#endif
    "ShoppingService",
    "SidePanelService",
    "SiteDataCacheFacadeFactory",
    "SiteEngagementService",
    "SocketManager",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Desktop chrome create this via
    // ShoppingService->SyncService->Spellchecker. Investigate if this is
    // expected on desktop chrome.
    "SpellcheckService",
#endif
    "StorageFrontend",
    "StorageNotificationService",
    "SupervisedUserService",
    "SyncInvalidationsService",
    "SystemInfoAPI",
    "TCPServerSocketEventDispatcher",
    "TCPSocketEventDispatcher",
    "TabGroupsEventRouter",
    "TabsWindowsAPI",
    "TemplateURLPrepopulateDataResolver",
    "TemplateURLServiceFactory",
    "ThemeService",
    "ToolbarActionsModel",
    "TopLevelTrialService",
    "TpcdTrialService",
    "TrackingProtectionSettings",
    "TranslateRanker",
    "TriggeredProfileResetter",
    "TtsAPI",
    "UDPSocketEventDispatcher",
    "UkmBackgroundRecorderService",
    "UsbDeviceManager",
    "UsbDeviceResourceManager",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "UserCloudPolicyInvalidator",
    "UserFmRegistrationTokenUploader",
    "UserPolicySigninService",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "UserScriptWorldConfigurationManager",
    "WarningBadgeService",
    "WarningService",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "WebAppProvider",
#endif  // !BUILDFLAG(IS_CHROMEOS)
    "WebAuthenticationProxyAPI",
#if BUILDFLAG(IS_CHROMEOS)
    "WebcamPrivateAPI",
#endif  // BUILDFLAG(IS_CHROMEOS)
    "WebDataService",
    "WebNavigationAPI",
    "WebRequestAPI",
    "WebRequestEventRouter",
    "WebRtcEventLogManagerKeyedService",
    "WebrtcAudioPrivateEventService",
    "WriteQuotaChecker",
    "feature_engagement::Tracker",
    "feedback::FeedbackUploaderChrome",
    "sct_reporting::Factory",
#if !BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946): Investigate if this is necessary on CrOS.
    "ZeroSuggestCacheServiceFactory",
#endif  // !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/374351946):
    // Verify these are necessary: then reorder or remove.

    "AccountManagerPolicyController",
    "ArcAppsPrivateAPI",
    "AuthTokenValidatorFactory",
    "AutotestPrivateAPI",
    "CastMediaNotificationProducerKeyedService",
    "ClientAppMetadataProviderService",
    "ClipboardAPI",
    "CrostiniMetricsService",
    "CupsPrintJobManagerFactory",
    "DebugLogsManagerFactory",
    "DeviceSyncClient",
    "DriveIntegrationService",
    "EasyUnlockService",
    "ExternalLogoutDoneEventHandler",
    "ExternalLogoutRequestEventHandler",
    "InputImeAPI",
    "InputMethodAPI",
    "KcerFactoryAsh",
    "LogSourceResource",
    "LorgnetteScannerManager",
    "MediaNotificationService",
    "MediaPerceptionAPIManager",
    "MediaPlayerAPI",
    "MultiDeviceSetupClient",
    "MultiDeviceSetupService",
    "NearbyConnector",
    "NearbyProcessManager",
    "OAuth2LoginManager",
    "OobeCompletionTrackerFactory",
    "OwnerSettingsService",
    "Pkcs12Migrator",
    "PlatformKeysService",
    "PluginVmEngagementMetricsService",
    "PolicyCertService",
    "PrintJobHistoryService",
    "PrintJobReportingServiceFactory",
    "PrintingManager",
    "Service",
    "SessionStateChangedEventDispatcher",
    "SmbService",
    "SyncedPrintersManager",
    "TerminalPrivateAPI",
    "TtsEngineExtensionObserverChromeOS",
    "UserNetworkConfigurationUpdater",
    "UserPrivateTokenKeyPermissionsManagerService",
    "VirtualKeyboardAPI",
    "VolumeManagerFactory",
    "VpnService",
#endif // BUILDFLAG(IS_CHROMEOS)
  };
  // clang-format on

  if (base::FeatureList::IsEnabled(commerce::kProductSpecifications)) {
    guest_active_services.insert("ProductSpecificationsService");
  }

  if (SearchEnginePreconnector::ShouldBeEnabledAsKeyedService()) {
    guest_active_services.insert("SearchEnginePreconnector");
  }
#if BUILDFLAG(IS_CHROMEOS)
  EXPECT_TRUE(user_manager::UserManager::Get()->IsLoggedInAsGuest());
  // ChromeOS Guest mode starts with the guest otr profile.
  Profile* guest_otr_profile = browser()->profile();
  Profile* guest_parent_profile = guest_otr_profile->GetOriginalProfile();
  // Some key services are created asynchronosly. Wait util they're ready.
#else
  Browser* guest_browser = CreateGuestBrowser();
  Profile* guest_parent_profile =
      guest_browser->profile()->GetOriginalProfile();
#endif  // BUILDFLAG(IS_CHROMEOS)
  content::RunAllTasksUntilIdle();

  ASSERT_FALSE(guest_parent_profile->IsRegularProfile());
  ASSERT_FALSE(guest_parent_profile->IsOffTheRecord());
  ASSERT_TRUE(guest_parent_profile->IsGuestSession());
  TestKeyedProfileServicesActives(guest_parent_profile, guest_active_services);
}
