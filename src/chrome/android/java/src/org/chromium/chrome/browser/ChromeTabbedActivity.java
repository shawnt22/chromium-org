// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import static org.chromium.chrome.browser.tabwindow.TabWindowManager.INVALID_WINDOW_ID;
import static org.chromium.chrome.browser.ui.IncognitoRestoreAppLaunchDrawBlocker.IS_INCOGNITO_SELECTED;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ShortcutManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.Browser;
import android.text.TextUtils;
import android.text.format.DateUtils;
import android.util.Pair;
import android.view.KeyEvent;
import android.view.KeyboardShortcutGroup;
import android.view.Menu;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.ViewStub;
import android.view.Window;
import android.view.WindowManager;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.lifecycle.Lifecycle;
import androidx.lifecycle.LifecycleObserver;
import androidx.lifecycle.LifecycleRegistry;

import org.chromium.base.ActivityState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.BuildInfo;
import org.chromium.base.Callback;
import org.chromium.base.CallbackController;
import org.chromium.base.CallbackUtils;
import org.chromium.base.CommandLine;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.MemoryPressureListener;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.jank_tracker.JankTracker;
import org.chromium.base.jank_tracker.JankTrackerImpl;
import org.chromium.base.jank_tracker.PlaceholderJankTracker;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneShotCallback;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.supplier.SupplierUtils;
import org.chromium.base.supplier.UnownedUserDataSupplier;
import org.chromium.base.supplier.UnwrapObservableSupplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.Nullable;
import org.chromium.build.annotations.UsedByReflection;
import org.chromium.cc.input.BrowserControlsState;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.IntentHandler.ExternalAppId;
import org.chromium.chrome.browser.IntentHandler.TabOpenType;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.app.metrics.LaunchCauseMetrics;
import org.chromium.chrome.browser.app.metrics.TabbedActivityLaunchCauseMetrics;
import org.chromium.chrome.browser.app.tabmodel.ArchivedTabModelOrchestrator;
import org.chromium.chrome.browser.app.tabmodel.AsyncTabParamsManagerSingleton;
import org.chromium.chrome.browser.app.tabmodel.ChromeNextTabPolicySupplier;
import org.chromium.chrome.browser.app.tabmodel.TabModelOrchestrator;
import org.chromium.chrome.browser.app.tabmodel.TabbedModeTabModelOrchestrator;
import org.chromium.chrome.browser.app.tabwindow.TabWindowManagerSingleton;
import org.chromium.chrome.browser.auxiliary_search.AuxiliarySearchController;
import org.chromium.chrome.browser.auxiliary_search.AuxiliarySearchController.AuxiliarySearchHostType;
import org.chromium.chrome.browser.auxiliary_search.AuxiliarySearchControllerFactory;
import org.chromium.chrome.browser.auxiliary_search.AuxiliarySearchMetrics;
import org.chromium.chrome.browser.auxiliary_search.module.AuxiliarySearchModuleBuilder;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.back_press.MinimizeAppAndCloseTabBackPressHandler;
import org.chromium.chrome.browser.back_press.MinimizeAppAndCloseTabBackPressHandler.MinimizeAppAndCloseTabType;
import org.chromium.chrome.browser.base.ColdStartTracker;
import org.chromium.chrome.browser.bookmarks.BookmarkPane;
import org.chromium.chrome.browser.bookmarks.BookmarkUtils;
import org.chromium.chrome.browser.browserservices.intents.WebappConstants;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.compositor.layouts.Layout;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerChrome;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerChromePhone;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerChromeTablet;
import org.chromium.chrome.browser.compositor.overlays.strip.StripLayoutHelperManager;
import org.chromium.chrome.browser.compositor.overlays.strip.StripLayoutHelperManager.TabModelStartupInfo;
import org.chromium.chrome.browser.cookies.CookiesFetcher;
import org.chromium.chrome.browser.crypto.CipherFactory;
import org.chromium.chrome.browser.data_sharing.DataSharingIntentUtils;
import org.chromium.chrome.browser.data_sharing.DataSharingTabGroupUtils;
import org.chromium.chrome.browser.data_sharing.DataSharingTabManager;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.dom_distiller.ReaderModeManager;
import org.chromium.chrome.browser.download.DownloadNotificationService;
import org.chromium.chrome.browser.download.DownloadOpenSource;
import org.chromium.chrome.browser.download.DownloadUtils;
import org.chromium.chrome.browser.dragdrop.ChromeDragAndDropBrowserDelegate;
import org.chromium.chrome.browser.dragdrop.ChromeDragDropUtils;
import org.chromium.chrome.browser.educational_tip.EducationTipModuleActionDelegate;
import org.chromium.chrome.browser.educational_tip.EducationalTipModuleBuilder;
import org.chromium.chrome.browser.educational_tip.EducationalTipModuleUtils;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.feed.FeedSurfaceTracker;
import org.chromium.chrome.browser.feed.FeedUma;
import org.chromium.chrome.browser.feedback.OmniboxFeedbackSource;
import org.chromium.chrome.browser.firstrun.FirstRunSignInProcessor;
import org.chromium.chrome.browser.flags.ActivityType;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.gesturenav.NavigationSheet;
import org.chromium.chrome.browser.history.HistoryManager;
import org.chromium.chrome.browser.history.HistoryManagerUtils;
import org.chromium.chrome.browser.history.HistoryPane;
import org.chromium.chrome.browser.homepage.HomepageManager;
import org.chromium.chrome.browser.hub.DefaultPaneOrderController;
import org.chromium.chrome.browser.hub.HubColorMixer.OverviewModeAlphaObserver;
import org.chromium.chrome.browser.hub.HubLayoutDependencyHolder;
import org.chromium.chrome.browser.hub.HubManager;
import org.chromium.chrome.browser.hub.HubProvider;
import org.chromium.chrome.browser.hub.HubShowPaneHelper;
import org.chromium.chrome.browser.hub.Pane;
import org.chromium.chrome.browser.hub.PaneId;
import org.chromium.chrome.browser.incognito.IncognitoNotificationManager;
import org.chromium.chrome.browser.incognito.IncognitoNotificationPresenceController;
import org.chromium.chrome.browser.incognito.IncognitoProfileDestroyer;
import org.chromium.chrome.browser.incognito.IncognitoStartup;
import org.chromium.chrome.browser.incognito.IncognitoTabLauncher;
import org.chromium.chrome.browser.incognito.IncognitoTabbedSnapshotController;
import org.chromium.chrome.browser.incognito.IncognitoUtils;
import org.chromium.chrome.browser.init.ActivityProfileProvider;
import org.chromium.chrome.browser.latency_injection.StartupLatencyInjector;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.locale.LocaleManager;
import org.chromium.chrome.browser.magic_stack.HomeModulesConfigManager;
import org.chromium.chrome.browser.magic_stack.HomeModulesMetricsUtils;
import org.chromium.chrome.browser.magic_stack.ModuleDelegate.ModuleType;
import org.chromium.chrome.browser.magic_stack.ModuleRegistry;
import org.chromium.chrome.browser.metrics.AndroidSessionDurationsServiceState;
import org.chromium.chrome.browser.metrics.LaunchMetrics;
import org.chromium.chrome.browser.metrics.MainIntentBehaviorMetrics;
import org.chromium.chrome.browser.metrics.SimpleStartupForegroundSessionDetector;
import org.chromium.chrome.browser.modaldialog.ChromeTabModalPresenter;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManager;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManagerFactory;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils.InstanceAllocationType;
import org.chromium.chrome.browser.native_page.NativePageAssassin;
import org.chromium.chrome.browser.navigation_predictor.NavigationPredictorBridge;
import org.chromium.chrome.browser.new_tab_url.DseNewTabUrlManager;
import org.chromium.chrome.browser.ntp.NewTabPageLaunchOrigin;
import org.chromium.chrome.browser.ntp.NewTabPageUma;
import org.chromium.chrome.browser.ntp.NewTabPageUtils;
import org.chromium.chrome.browser.ntp_customization.NtpCustomizationCoordinator;
import org.chromium.chrome.browser.ntp_customization.NtpCustomizationMetricsUtils;
import org.chromium.chrome.browser.offlinepages.OfflinePageUtils;
import org.chromium.chrome.browser.omnibox.OmniboxFocusReason;
import org.chromium.chrome.browser.paint_preview.StartupPaintPreviewHelper;
import org.chromium.chrome.browser.paint_preview.StartupPaintPreviewHelperSupplier;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.price_change.PriceChangeModuleBuilder;
import org.chromium.chrome.browser.privacy_sandbox.PrivacySandboxSurveyController;
import org.chromium.chrome.browser.profiles.OtrProfileId;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.quick_delete.QuickDeleteController;
import org.chromium.chrome.browser.quick_delete.QuickDeleteDelegateImpl;
import org.chromium.chrome.browser.quick_delete.QuickDeleteMetricsDelegate;
import org.chromium.chrome.browser.read_later.ReadingListBackPressHandler;
import org.chromium.chrome.browser.recent_tabs.CrossDevicePaneFactory;
import org.chromium.chrome.browser.reengagement.ReengagementNotificationController;
import org.chromium.chrome.browser.safety_hub.SafetyHubMagicStackBuilder;
import org.chromium.chrome.browser.search_engines.SearchEngineChoiceNotification;
import org.chromium.chrome.browser.searchwidget.SearchActivityClientImpl;
import org.chromium.chrome.browser.settings.SettingsNavigationFactory;
import org.chromium.chrome.browser.share.ShareHelper;
import org.chromium.chrome.browser.share.send_tab_to_self.SendTabToSelfAndroidBridge;
import org.chromium.chrome.browser.signin.SigninAndHistorySyncActivityLauncherImpl;
import org.chromium.chrome.browser.single_tab.SingleTabModuleBuilder;
import org.chromium.chrome.browser.survey.ChromeSurveyController;
import org.chromium.chrome.browser.sync.ui.SyncErrorMessage;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.RedirectHandlerTabHelper;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabArchiveSettings;
import org.chromium.chrome.browser.tab.TabAssociatedApp;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabDelegateFactory;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabObscuringHandler;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab.tab_restore.HistoricalTabModelObserver;
import org.chromium.chrome.browser.tab_group_suggestion.GroupSuggestionsPromotionCoordinator;
import org.chromium.chrome.browser.tab_group_suggestion.SuggestionEventObserver;
import org.chromium.chrome.browser.tab_group_sync.TabGroupSyncServiceFactory;
import org.chromium.chrome.browser.tab_ui.ActionConfirmationManager;
import org.chromium.chrome.browser.tab_ui.TabGridIphDialogCoordinator;
import org.chromium.chrome.browser.tab_ui.TabSwitcher;
import org.chromium.chrome.browser.tab_ui.TabSwitcherUtils;
import org.chromium.chrome.browser.tabbed_mode.TabbedAppMenuPropertiesDelegate;
import org.chromium.chrome.browser.tabbed_mode.TabbedRootUiCoordinator;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHost;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHostRegistry;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHostUtils;
import org.chromium.chrome.browser.tabmodel.MismatchedIndicesHandler;
import org.chromium.chrome.browser.tabmodel.NextTabPolicy.NextTabPolicySupplier;
import org.chromium.chrome.browser.tabmodel.TabClosingSource;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabClosureParamsUtils;
import org.chromium.chrome.browser.tabmodel.TabGroupColorUtils;
import org.chromium.chrome.browser.tabmodel.TabGroupMetadata;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.browser.tabmodel.TabGroupUtils;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorBase;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tasks.HomeSurfaceTracker;
import org.chromium.chrome.browser.tasks.ReturnToChromeUtil;
import org.chromium.chrome.browser.tasks.tab_management.CloseAllTabsDialog;
import org.chromium.chrome.browser.tasks.tab_management.CloseAllTabsHelper;
import org.chromium.chrome.browser.tasks.tab_management.TabGroupCreationUiDelegate;
import org.chromium.chrome.browser.tasks.tab_management.TabGroupMenuActionHandler;
import org.chromium.chrome.browser.tasks.tab_management.TabGroupUi;
import org.chromium.chrome.browser.tasks.tab_management.TabGroupVisualDataManager;
import org.chromium.chrome.browser.tasks.tab_management.TabManagementDelegate;
import org.chromium.chrome.browser.tasks.tab_management.TabManagementDelegateProvider;
import org.chromium.chrome.browser.tasks.tab_management.TabModelNotificationDotManager;
import org.chromium.chrome.browser.tasks.tab_management.TabUiUtils;
import org.chromium.chrome.browser.tasks.tab_management.TabsSettings;
import org.chromium.chrome.browser.tasks.tab_management.archived_tabs_auto_delete_promo.ArchivedTabsAutoDeletePromoManager;
import org.chromium.chrome.browser.toolbar.ToolbarIntentMetadata;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.chrome.browser.toolbar.top.ToolbarControlContainer;
import org.chromium.chrome.browser.toolbar.top.tab_strip.StripVisibilityState;
import org.chromium.chrome.browser.ui.AppLaunchDrawBlocker;
import org.chromium.chrome.browser.ui.IncognitoRestoreAppLaunchDrawBlockerFactory;
import org.chromium.chrome.browser.ui.RootUiCoordinator;
import org.chromium.chrome.browser.ui.appmenu.AppMenuPropertiesDelegate;
import org.chromium.chrome.browser.ui.desktop_windowing.AppHeaderUtils;
import org.chromium.chrome.browser.ui.edge_to_edge.EdgeToEdgeUtils;
import org.chromium.chrome.browser.ui.extensions.ExtensionService;
import org.chromium.chrome.browser.ui.searchactivityutils.SearchActivityClient;
import org.chromium.chrome.browser.ui.searchactivityutils.SearchActivityExtras.IntentOrigin;
import org.chromium.chrome.browser.ui.signin.BottomSheetSigninAndHistorySyncConfig;
import org.chromium.chrome.browser.ui.signin.account_picker.AccountPickerBottomSheetStrings;
import org.chromium.chrome.browser.ui.signin.history_sync.HistorySyncConfig;
import org.chromium.chrome.browser.undo_tab_close_snackbar.UndoBarController;
import org.chromium.chrome.browser.usage_stats.UsageStatsService;
import org.chromium.chrome.browser.util.ChromeAccessibilityUtil;
import org.chromium.chrome.browser.xr.scenecore.XrSceneCoreSessionManagerImpl;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.edge_to_edge.SystemBarColorHelper;
import org.chromium.components.browser_ui.edge_to_edge.TabbedSystemBarColorHelper;
import org.chromium.components.browser_ui.util.BrowserControlsVisibilityDelegate;
import org.chromium.components.browser_ui.util.ComposedBrowserControlsVisibilityDelegate;
import org.chromium.components.browser_ui.util.FirstDrawDetector;
import org.chromium.components.browser_ui.util.motion.MotionEventInfo;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.embedder_support.util.UrlUtilitiesJni;
import org.chromium.components.external_intents.ExternalNavigationHandler;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.components.omnibox.OmniboxFeatures;
import org.chromium.components.profile_metrics.BrowserProfileType;
import org.chromium.components.signin.metrics.SigninAccessPoint;
import org.chromium.components.supervised_user.SupervisedUserConstants;
import org.chromium.components.tab_group_sync.SavedTabGroup;
import org.chromium.components.tab_group_sync.TabGroupSyncService;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.components.webapps.ShortcutSource;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.Visibility;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.common.ContentSwitches;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.PageTransition;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.dragdrop.DragAndDropDelegate;
import org.chromium.ui.dragdrop.DragAndDropDelegateImpl;
import org.chromium.ui.dragdrop.DragDropMetricUtils;
import org.chromium.ui.dragdrop.DragDropMetricUtils.UrlIntentSource;
import org.chromium.ui.util.XrUtils;
import org.chromium.ui.widget.Toast;
import org.chromium.ui.xr.scenecore.XrSceneCoreSessionManager;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * This is the main activity for ChromeMobile when not running in document mode. All the tabs are
 * accessible via a chrome specific tab switching UI.
 */
public class ChromeTabbedActivity extends ChromeActivity {
    // Name of the ChromeTabbedActivity alias that handles MAIN intents.
    public static final String MAIN_LAUNCHER_ACTIVITY_NAME = "com.google.android.apps.chrome.Main";

    public static final Set<String> TABBED_MODE_COMPONENT_NAMES =
            Set.of(
                    ChromeTabbedActivity.class.getName(),
                    ChromeTabbedActivity2.class.getName(),
                    MAIN_LAUNCHER_ACTIVITY_NAME);

    public static final String HISTOGRAM_DRAGGED_TAB_OPENED_NEW_WINDOW =
            "Android.MultiWindowMode.DraggedTabOpenedNewWindow";

    protected static final String WINDOW_INDEX = "window_index";

    static final String HISTOGRAM_EXPLICIT_VIEW_INTENT_FINISHED_NEW_ACTIVITY =
            "Android.ExplicitViewIntentFinishedNewTabbedActivity";

    private static final String TAG = "ChromeTabbedActivity";

    // How long to delay closing the current tab when our app is minimized.  Have to delay this
    // so that we don't show the contents of the next tab while minimizing.
    private static final long CLOSE_TAB_ON_MINIMIZE_DELAY_MS = 500;

    // Maximum delay for initial tab creation. This is for homepage and NTP, not previous tabs
    // restore. This is needed because we do not know when reading PartnerBrowserCustomizations
    // provider will be finished.
    private static final int INITIAL_TAB_CREATION_TIMEOUT_MS = 500;

    /**
     * Sending an intent with this action to Chrome will cause it to close all tabs (iff the
     * --enable-test-intents command line flag is set). If a URL is supplied in the intent data,
     * this will be loaded and unaffected by the close all action.
     */
    private static final String ACTION_CLOSE_TABS =
            "com.google.android.apps.chrome.ACTION_CLOSE_TABS";

    private static final String TAG_MULTI_INSTANCE = "MultiInstance";

    private static final String HISTOGRAM_MAIN_INTENT_TIME_TO_FIRST_DRAW_WARM_MS =
            "Startup.Android.Warm.MainIntentTimeToFirstDraw";
    private static final String PIXEL_LAUNCHER_NAME = "PixelLauncher";
    private static final String THIRD_PARTY_LAUNCHER_NAME = "ThirdPartyLauncher";

    /**
     * This class is used to warm up the chrome split ClassLoader. See SplitChromeApplication for
     * more info
     */
    @UsedByReflection("SplitChromeApplication.java")
    public static class Preload extends ChromeTabbedActivity {
        private LifecycleRegistry mLifecycleRegistry;

        @UsedByReflection("SplitChromeApplication.java")
        public Preload() {}

        @Override
        public Lifecycle getLifecycle() {
            if (mLifecycleRegistry == null) {
                // LifecycleRegistry normally enforces it is called on the main thread, but this
                // class will be preloaded in a background thread. The only method that gets called
                // in the activity constructor is addObserver(), so just override that.
                mLifecycleRegistry =
                        new LifecycleRegistry(this) {
                            @Override
                            public void addObserver(LifecycleObserver observer) {}
                        };
            }
            return mLifecycleRegistry;
        }
    }

    /**
     * A {@link CipherFactory} instance that is shared among all {@link ChromeTabbedActivity}
     * instances.
     */
    private static class CipherLazyHolder {
        private static final CipherFactory sCipherInstance = new CipherFactory();
    }

    /**
     * Identifies a histogram to use in {@link #maybeDispatchExplicitMainViewIntent(Intent, int)}.
     */
    @IntDef({DispatchedBy.ON_CREATE, DispatchedBy.ON_NEW_INTENT})
    @Retention(RetentionPolicy.SOURCE)
    private @interface DispatchedBy {
        int ON_CREATE = 1;
        int ON_NEW_INTENT = 2;
    }

    private final TabModelNotificationDotManager mTabModelNotificationDotManager =
            new TabModelNotificationDotManager(this);
    // Supplier for a dependency to inform about the type of intent used to launch Chrome.
    private final OneshotSupplierImpl<ToolbarIntentMetadata> mIntentMetadataOneshotSupplier =
            new OneshotSupplierImpl<>();
    private final UnownedUserDataSupplier<StartupPaintPreviewHelper>
            mStartupPaintPreviewHelperSupplier = new StartupPaintPreviewHelperSupplier();
    private final OneshotSupplierImpl<LayoutStateProvider> mLayoutStateProviderSupplier =
            new OneshotSupplierImpl<>();
    private final OneshotSupplierImpl<TabSwitcher> mTabSwitcherSupplier =
            new OneshotSupplierImpl<>();
    private final OneshotSupplierImpl<TabSwitcher> mIncognitoTabSwitcherSupplier =
            new OneshotSupplierImpl<>();
    private final OneshotSupplierImpl<HubManager> mHubManagerSupplier = new OneshotSupplierImpl<>();
    private final OneshotSupplierImpl<ModuleRegistry> mModuleRegistrySupplier =
            new OneshotSupplierImpl<>();
    private final IncognitoTabHost mIncognitoTabHost =
            new IncognitoTabHost() {
                @Override
                public boolean hasIncognitoTabs() {
                    return getTabModelSelector().getModel(true).getCount() > 0;
                }

                @Override
                public void closeAllIncognitoTabs() {
                    if (isActivityFinishingOrDestroyed()) return;

                    // If the tabbed activity has not yet initialized, then finish the activity to
                    // avoid timing issues with clearing the incognito tab state in the
                    // background.
                    if (!areTabModelsInitialized() || !didFinishNativeInitialization()) {
                        finish();
                        return;
                    }

                    terminateIncognitoSession();
                }

                @Override
                public boolean isActiveModel() {
                    return getTabModelSelector().getModel(true).isActiveModel();
                }
            };

    private final OnClickListener mNewTabButtonClickListener = this::onNewTabButtonClick;
    private final MismatchedIndicesHandler mMismatchedIndicesHandler =
            new TabbedMismatchedIndicesHandler(
                    this::getOnCreateTimestampMs, !MultiWindowUtils.isMultiInstanceApi31Enabled());
    private final SearchActivityClient mJumpStartSearchClient =
            new SearchActivityClientImpl(this, IntentOrigin.LAUNCHER);
    private final SearchActivityClient mHubSearchClient =
            new SearchActivityClientImpl(this, IntentOrigin.HUB);
    private final OneshotSupplierImpl<SystemBarColorHelper> mBottomChinSupplier =
            new OneshotSupplierImpl<>();

    private final MainIntentBehaviorMetrics mMainIntentMetrics;
    private final AppLaunchDrawBlocker mAppLaunchDrawBlocker;

    private @Nullable MultiInstanceManager mMultiInstanceManager;
    private UndoBarController mUndoBarPopupController;
    private LayoutManagerChrome mLayoutManager;
    private ViewGroup mContentContainer;
    private ToolbarControlContainer mControlContainer;
    private TabbedModeTabModelOrchestrator mTabModelOrchestrator;
    private TabModelSelectorBase mTabModelSelector;
    private TabModelSelectorObserver mTabModelSelectorObserver;
    private TabModelSelectorTabObserver mTabModelSelectorTabObserver;
    private TabModelSelectorTabModelObserver mTabModelObserver;
    private HistoricalTabModelObserver mHistoricalTabModelObserver;
    private UndoRefocusHelper mUndoRefocusHelper;
    private BrowserControlsVisibilityDelegate mVrBrowserControlsVisibilityDelegate;
    private boolean mUiWithNativeInitialized;
    private LocaleManager mLocaleManager;
    private Runnable mShowHistoryRunnable;
    private CompositorViewHolder mCompositorViewHolder;

    /** Keeps track of whether or not a specific tab was created based on the startup intent. */
    private boolean mCreatedTabOnStartup;

    // Whether or not the initial tab is being created.
    private boolean mPendingInitialTabCreation;

    /** Keeps track of the pref for the last time since this activity was stopped. */
    private ChromeInactivityTracker mInactivityTracker;

    /** The controller for the auxiliary search. */
    private @Nullable AuxiliarySearchController mAuxiliarySearchController;

    // This is the cached value of IntentHandler#shouldIgnoreIntent and shouldn't be read directly.
    // Use #shouldIgnoreIntent instead.
    private Boolean mShouldIgnoreIntent;

    // Listens to FrameMetrics and records janks.
    private JankTracker mJankTracker;

    // Whether the activity is staring from a resumption. False if the activity is starting from
    // onCreate(), a cold startup.
    private boolean mFromResumption;

    private NextTabPolicySupplier mNextTabPolicySupplier;
    private HubProvider mHubProvider;
    private Runnable mCleanUpHubOverviewColorObserver;
    private ObservableSupplierImpl<TabModelStartupInfo> mTabModelStartupInfoSupplier;
    private CallbackController mCallbackController = new CallbackController();
    private TabbedModeTabDelegateFactory mTabDelegateFactory;
    private ReadingListBackPressHandler mReadingListBackPressHandler;
    private MinimizeAppAndCloseTabBackPressHandler mMinimizeAppAndCloseTabBackPressHandler;
    private HomeSurfaceTracker mHomeSurfaceTracker;

    // ID assigned to each ChromeTabbedActivity instance in Android S+ where multi-instance feature
    // is supported. This can be explicitly set in the incoming Intent or internally assigned.
    private int mWindowId;

    private @InstanceAllocationType int mInstanceAllocationType;

    // The URL of the last active Tab read from the Tab metadata file during cold startup.
    private String mLastActiveTabUrl;

    private DseNewTabUrlManager mDseNewTabUrlManager;

    // Time at which an intent was received and handled.
    private long mIntentHandlingTimeMs;

    // The time it took for the first draw on a warm start.
    private long mTimeToFirstDrawAfterStartMs;

    // Delegate to handle drag and drop features for tablets.
    private DragAndDropDelegate mDragDropDelegate;

    private CookiesFetcher mIncognitoCookiesFetcher;

    // Manager for tab group visual data lifecycle updates.
    private TabGroupVisualDataManager mTabGroupVisualDataManager;

    private SuggestionEventObserver mSuggestionEventObserver;
    private GroupSuggestionsPromotionCoordinator mGroupSuggestionsPromotionCoordinator;
    private @Nullable ArchivedTabsAutoDeletePromoManager mArchivedTabsAutoDeletePromoManager;

    private final LazyOneshotSupplier<@Nullable XrSceneCoreSessionManager>
            mXrSceneCoreSessionManagerSupplier =
                    LazyOneshotSupplier.fromSupplier(this::createXrSceneCoreSessionManager);
    private final Callback<Boolean> mOnXrSpaceModeChanged = this::onXrSpaceModeChanged;

    /** Constructs a ChromeTabbedActivity. */
    public ChromeTabbedActivity() {
        mIntentHandlingTimeMs = SystemClock.uptimeMillis();
        mMainIntentMetrics = new MainIntentBehaviorMetrics();

        // AppLaunchDrawBlocker may block drawing the Activity content until the initial tab is
        // available.
        mAppLaunchDrawBlocker =
                new AppLaunchDrawBlocker(
                        getLifecycleDispatcher(),
                        () -> findViewById(android.R.id.content),
                        this::getIntent,
                        this::shouldIgnoreIntent,
                        this::isTablet,
                        mTabModelProfileSupplier,
                        new IncognitoRestoreAppLaunchDrawBlockerFactory(
                                this::getSavedInstanceState,
                                getTabModelSelectorSupplier(),
                                CipherLazyHolder.sCipherInstance));
    }

    @Override
    protected void onPreCreate() {
        super.onPreCreate();
        mMultiInstanceManager =
                MultiInstanceManagerFactory.create(
                        this,
                        getTabModelOrchestratorSupplier(),
                        getMultiWindowModeStateDispatcher(),
                        getLifecycleDispatcher(),
                        getModalDialogManagerSupplier(),
                        this,
                        () ->
                                mRootUiCoordinator != null
                                        ? mRootUiCoordinator.getDesktopWindowStateManager()
                                        : null);
        mBackPressManager.setFallbackOnBackPressed(
                () -> {
                    minimizeAppAndCloseTabOnBackPress(getActivityTab());
                });
    }

    @Override
    protected @LaunchIntentDispatcher.Action int maybeDispatchLaunchIntent(
            Intent intent, Bundle savedInstanceState) {
        // Detect if incoming intent is a result of Chrome recreating itself. For now, restrict this
        // path to reparenting to ensure the launching logic isn't disrupted.
        // TODO(crbug.com/40681858): Unlock this codepath for all incoming intents once it's
        // confirmed working and stable.
        if (savedInstanceState != null
                && AsyncTabParamsManagerSingleton.getInstance().hasParamsWithTabToReparent()) {
            return LaunchIntentDispatcher.Action.CONTINUE;
        }

        if (getClass().equals(ChromeTabbedActivity.class)
                && Intent.ACTION_MAIN.equals(intent.getAction())) {
            if (OmniboxFeatures.shouldJumpStartOmniboxEngage()) {
                OmniboxFeedbackSource.addFeedbackData(
                        OmniboxFeedbackSource.Psd.JSO_STATE, "engaged");
                return LaunchIntentDispatcher.dispatchToSearchActivity(
                        mJumpStartSearchClient, this, intent);
            } else if (OmniboxFeatures.isJumpStartOmniboxEnabled()) {
                OmniboxFeedbackSource.addFeedbackData(
                        OmniboxFeedbackSource.Psd.JSO_STATE, "active");
            }
            // Call dispatchToTabbedActivity() for MAIN intents to activate proper multi-window
            // TabbedActivity (i.e. if CTA2 is currently running and Chrome is started, CTA2
            // should be brought to front). Don't call dispatchToTabbedActivity() for non-MAIN
            // intents to avoid breaking cases where CTA is started explicitly (e.g. to handle
            // 'Move to other window' command from CTA2).
            return LaunchIntentDispatcher.dispatchToTabbedActivity(this, intent);
        }
        @LaunchIntentDispatcher.Action
        int action = maybeDispatchExplicitMainViewIntent(intent, DispatchedBy.ON_CREATE);
        if (action != LaunchIntentDispatcher.Action.CONTINUE) {
            Log.i(TAG_MULTI_INSTANCE, "Dispatched explicit .Main (CTA) VIEW intent to CCT.");
            return action;
        }
        return super.maybeDispatchLaunchIntent(intent, savedInstanceState);
    }

    // We know of at least one app that explicitly specifies .Main activity in custom tab
    // intents. The app shouldn't be doing that, but until it's updated, we need to support
    // such use case.
    //
    // This method attempts to treat VIEW intents explicitly sent to .Main as custom tab
    // intents, and dispatch them accordingly. If the intent was not dispatched, the method
    // returns Action.CONTINUE.
    //
    // The method also updates the supplied boolean histogram with the dispatching result,
    // but only if the intent is a VIEW intent sent explicitly to .Main activity.
    private @LaunchIntentDispatcher.Action int maybeDispatchExplicitMainViewIntent(
            Intent intent, @DispatchedBy int dispatchedBy) {
        // The first check ensures that this is .Main activity alias (we can't check exactly, but
        // this gets us sufficiently close).
        if (getClass().equals(ChromeTabbedActivity.class)
                && Intent.ACTION_VIEW.equals(intent.getAction())
                && intent.getComponent() != null
                && MAIN_LAUNCHER_ACTIVITY_NAME.equals(intent.getComponent().getClassName())) {
            @LaunchIntentDispatcher.Action
            int action = LaunchIntentDispatcher.dispatchToCustomTabActivity(this, intent);
            switch (dispatchedBy) {
                case DispatchedBy.ON_CREATE:
                case DispatchedBy.ON_NEW_INTENT:
                    break;
                default:
                    assert false : "Unknown dispatchedBy value " + dispatchedBy;
            }
            if (action == LaunchIntentDispatcher.Action.CONTINUE) {
                // Crash if intent came from us, but only in debug builds and only if we weren't
                // explicitly told not to. Hopefully we'll get enough reports to find where
                // these intents come from.
                if (IntentHandler.isExternalIntentSourceChrome(intent)
                        && BuildInfo.isDebugApp()
                        && !CommandLine.getInstance()
                                .hasSwitch(ChromeSwitches.DONT_CRASH_ON_VIEW_MAIN_INTENTS)) {
                    String intentInfo = intent.toString();
                    Bundle extras = intent.getExtras();
                    if (extras != null) {
                        intentInfo +=
                                ", extras.keySet = [" + TextUtils.join(", ", extras.keySet()) + "]";
                    }
                    String message =
                            String.format(
                                    """
                                    VIEW intent sent to .Main activity alias was not dispatched. \
                                    PLEASE report the following info to crbug.com/789732: \
                                    "%s". Use --%s flag to disable this check.""",
                                    intentInfo, ChromeSwitches.DONT_CRASH_ON_VIEW_MAIN_INTENTS);
                    throw new IllegalStateException(message);
                }
            }
            return action;
        }
        return LaunchIntentDispatcher.Action.CONTINUE;
    }

    @Override
    public void initializeCompositor() {
        try {
            TraceEvent.begin("ChromeTabbedActivity.initializeCompositor");
            super.initializeCompositor();

            // LocaleManager can only function after the native library is loaded.
            mLocaleManager = LocaleManager.getInstance();
            mLocaleManager.showSearchEnginePromoIfNeeded(this, null);

            mTabModelOrchestrator.onNativeLibraryReady(getTabContentManager());

            TabModelUtils.runOnTabStateInitialized(
                    mTabModelSelector,
                    mCallbackController.makeCancelable(
                            (tabModelSelector) -> {
                                assert tabModelSelector != null;
                                mTabGroupVisualDataManager =
                                        new TabGroupVisualDataManager(tabModelSelector);
                            }));

            mTabModelNotificationDotManager.initWithNative(mTabModelSelector);

            Profile profile = mTabModelSelector.getCurrentModel().getProfile();
            // For saving non-incognito tab closures for Recent Tabs.
            mHistoricalTabModelObserver =
                    new HistoricalTabModelObserver(
                            mTabModelSelector
                                    .getTabGroupModelFilterProvider()
                                    .getTabGroupModelFilter(false));
            mHistoricalTabModelObserver.addSecondaryTabModelSupplier(
                    ArchivedTabModelOrchestrator.getForProfile(profile)::getTabModel);

            // Defer creation of this helper so it triggers after TabGroupModelFilter observers.
            mUndoRefocusHelper =
                    new UndoRefocusHelper(
                            mTabModelSelector, getLayoutManagerSupplier(), isTablet());

            mTabModelObserver =
                    new TabModelSelectorTabModelObserver(mTabModelSelector) {
                        @Override
                        public void onFinishingTabClosure(
                                Tab tab, @TabClosingSource int closingSource) {
                            closeIfNoTabsAndHomepageEnabled(
                                    false,
                                    /* shouldRemoveWindowWithZeroTabs= */ closingSource
                                            == TabClosingSource.TABLET_TAB_STRIP);
                        }

                        @Override
                        public void tabPendingClosure(
                                Tab tab, @TabClosingSource int closingSource) {
                            closeIfNoTabsAndHomepageEnabled(
                                    true,
                                    /* shouldRemoveWindowWithZeroTabs= */ closingSource
                                            == TabClosingSource.TABLET_TAB_STRIP);
                        }

                        @Override
                        public void multipleTabsPendingClosure(List<Tab> tabs, boolean isAllTabs) {
                            closeIfNoTabsAndHomepageEnabled(
                                    true, /* shouldRemoveWindowWithZeroTabs= */ false);
                        }

                        @Override
                        public void tabRemoved(Tab tab) {
                            closeIfNoTabsAndHomepageEnabled(
                                    false, /* shouldRemoveWindowWithZeroTabs= */ false);
                        }

                        private void closeIfNoTabsAndHomepageEnabled(
                                boolean isPendingClosure, boolean shouldRemoveWindowWithZeroTabs) {
                            if (getTabModelSelector().getTotalTabCount() == 0) {
                                if (shouldRemoveWindowWithZeroTabs) {
                                    finishAndRemoveTask();
                                } else if (HomepageManager.getInstance()
                                        .shouldCloseAppWithZeroTabs()) {
                                    // If the last tab is closed, and homepage is enabled, then exit
                                    // Chrome.
                                    finish();
                                } else if (isPendingClosure) {
                                    NewTabPageUma.recordNtpImpression(
                                            NewTabPageUma.NTP_IMPRESSION_POTENTIAL_NO_TAB);
                                }
                            }
                        }

                        @Override
                        public void didAddTab(
                                Tab tab,
                                @TabLaunchType int type,
                                @TabCreationState int creationState,
                                boolean markedForSelection) {
                            if (type == TabLaunchType.FROM_LONGPRESS_BACKGROUND
                                    || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP
                                    || (type == TabLaunchType.FROM_RECENT_TABS
                                            && !DeviceClassManager.enableAnimations())) {
                                Toast.makeText(
                                                ChromeTabbedActivity.this,
                                                R.string.open_in_new_tab_toast,
                                                Toast.LENGTH_SHORT)
                                        .show();
                            }
                        }
                    };
        } finally {
            TraceEvent.end("ChromeTabbedActivity.initializeCompositor");
        }
    }

    private void onNewTabButtonClick(View view) {
        getTabModelSelector().getModel(false).commitAllTabClosures();
        // This assumes that the keyboard can not be seen at the same time as the
        // new tab button on the toolbar.
        int tabLaunchType =
                (getLayoutManager().getActiveLayoutType() == LayoutType.TAB_SWITCHER)
                        ? TabLaunchType.FROM_TAB_SWITCHER_UI
                        : TabLaunchType.FROM_CHROME_UI;
        getCurrentTabCreator().launchNtp(tabLaunchType);
        mLocaleManager.showSearchEnginePromoIfNeeded(ChromeTabbedActivity.this, null);
        if (getTabModelSelector().isIncognitoSelected()) {
            RecordUserAction.record("MobileToolbarStackViewNewIncognitoTab");
        } else {
            RecordUserAction.record("MobileToolbarStackViewNewTab");
        }
        RecordUserAction.record("MobileTopToolbarNewTabButton");

        RecordUserAction.record("MobileNewTabOpened");
    }

    private void refreshSignIn() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.refreshSignIn")) {
            FirstRunSignInProcessor.openSyncSettingsIfScheduled(this);
        }
    }

    private HubLayoutDependencyHolder createHubLayoutDependencyHolder() {
        // The tab_switcher_view_holder can be used on both tablet and phone because Hub's
        // animations don't depend on the compositor. This differs from the current tab switcher
        // behavior on phones.
        LazyOneshotSupplier<ViewGroup> rootViewSupplier =
                LazyOneshotSupplier.fromSupplier(
                        () -> {
                            ViewStub stub = findViewById(R.id.tab_switcher_view_holder_stub);
                            return (ViewGroup) stub.inflate();
                        });

        ObservableSupplier<Boolean> incognitoSupplier =
                new UnwrapObservableSupplier<>(
                        mTabModelSelector.getCurrentTabModelSupplier(),
                        (tabModel) -> tabModel == null ? false : tabModel.isIncognito());
        HubLayoutDependencyHolder hubLayoutDependencyHolder =
                new HubLayoutDependencyHolder(
                        mHubProvider.getHubManagerSupplier(),
                        rootViewSupplier,
                        mRootUiCoordinator.getScrimManager(),
                        rootViewSupplier::get,
                        incognitoSupplier,
                        adaptOnOverviewColorAlphaChange(),
                        mXrSceneCoreSessionManagerSupplier.get());

        return hubLayoutDependencyHolder;
    }

    private void setupCompositorContentForPhone() {
        if (isTablet()) return;

        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.setupCompositorContentForPhone")) {
            CompositorViewHolder compositorViewHolder = getCompositorViewHolderSupplier().get();

            mLayoutManager =
                    new LayoutManagerChromePhone(
                            compositorViewHolder,
                            mContentContainer,
                            mTabSwitcherSupplier,
                            getTabModelSelectorSupplier(),
                            getTabContentManagerSupplier(),
                            mRootUiCoordinator::getTopUiThemeColorProvider,
                            createHubLayoutDependencyHolder(),
                            getCompositorViewHolderSupplier(),
                            getContentView(),
                            getToolbarManager(),
                            mRootUiCoordinator.getScrimManager().getScrimVisibilitySupplier());
            mLayoutStateProviderSupplier.set(mLayoutManager);
        }
    }

    private void setupCompositorContentForTablet() {
        if (!isTablet()) return;

        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.setupCompositorContentForTablet")) {
            CompositorViewHolder compositorViewHolder = getCompositorViewHolderSupplier().get();

            ViewStub tabHoverCardViewStub = findViewById(R.id.tab_hover_card_holder_stub);
            View toolbarContainerView = findViewById(R.id.toolbar_container);
            mDragDropDelegate = new DragAndDropDelegateImpl();
            mDragDropDelegate.setDragAndDropBrowserDelegate(
                    new ChromeDragAndDropBrowserDelegate(() -> this));

            assert getToolbarManager() != null;

            ActionConfirmationManager actionConfirmationManager =
                    new ActionConfirmationManager(
                            mTabModelProfileSupplier.get(),
                            this,
                            getModalDialogManagerSupplier().get());

            mLayoutManager =
                    new LayoutManagerChromeTablet(
                            compositorViewHolder,
                            mContentContainer,
                            mTabSwitcherSupplier,
                            getTabModelSelectorSupplier(),
                            getBrowserControlsManager(),
                            getTabContentManagerSupplier(),
                            mRootUiCoordinator::getTopUiThemeColorProvider,
                            mTabModelStartupInfoSupplier,
                            getLifecycleDispatcher(),
                            createHubLayoutDependencyHolder(),
                            mMultiInstanceManager,
                            mDragDropDelegate,
                            toolbarContainerView,
                            tabHoverCardViewStub,
                            getWindowAndroid(),
                            getToolbarManager(),
                            mRootUiCoordinator.getDesktopWindowStateManager(),
                            actionConfirmationManager,
                            mRootUiCoordinator.getDataSharingTabManager(),
                            mRootUiCoordinator.getBottomSheetController(),
                            mRootUiCoordinator.getShareDelegateSupplier(),
                            getXrSpaceModeObservableSupplier());
            mLayoutStateProviderSupplier.set(mLayoutManager);
        }
    }

    /** Returns the supplier for the {@link HubManager} for testing. */
    public OneshotSupplier<HubManager> getHubManagerSupplierForTesting() {
        return mHubManagerSupplier;
    }

    private void initHub() {
        mHubProvider =
                new HubProvider(
                        this,
                        getProfileProviderSupplier(),
                        new DefaultPaneOrderController(),
                        mBackPressManager,
                        getMenuOrKeyboardActionController(),
                        this::getSnackbarManager,
                        getTabModelSelectorSupplier(),
                        () -> getToolbarManager().getOverviewModeMenuButtonCoordinator(),
                        mEdgeToEdgeControllerSupplier,
                        mHubSearchClient,
                        getXrSpaceModeObservableSupplier());
        var builder = mHubProvider.getPaneListBuilder();
        builder.registerPane(
                PaneId.TAB_SWITCHER,
                LazyOneshotSupplier.fromSupplier(() -> createTabSwitcherPane(false)));
        builder.registerPane(
                PaneId.INCOGNITO_TAB_SWITCHER,
                LazyOneshotSupplier.fromSupplier(() -> createTabSwitcherPane(true)));
        builder.registerPane(
                PaneId.TAB_GROUPS, LazyOneshotSupplier.fromSupplier(this::createTabGroupsPane));
        if (ChromeFeatureList.sCrossDeviceTabPaneAndroid.isEnabled()) {
            builder.registerPane(
                    PaneId.CROSS_DEVICE,
                    LazyOneshotSupplier.fromSupplier(this::createCrossDevicePane));
        }
        if (ChromeFeatureList.sHistoryPaneAndroid.isEnabled()) {
            builder.registerPane(
                    PaneId.HISTORY, LazyOneshotSupplier.fromSupplier(this::createHistoryPane));
        }

        if (ChromeFeatureList.sBookmarkPaneAndroid.isEnabled()) {
            builder.registerPane(
                    PaneId.BOOKMARKS, LazyOneshotSupplier.fromSupplier(this::createBookmarkPane));
        }

        mHubProvider
                .getHubManagerSupplier()
                .onAvailable(manager -> mHubManagerSupplier.set(manager));
    }

    private @NonNull ObservableSupplier<Integer> initHubOverviewColorSupplier() {
        ObservableSupplierImpl<Integer> overviewColorSupplier =
                new ObservableSupplierImpl<>(Color.TRANSPARENT);
        mHubManagerSupplier.onAvailable(
                (hubManager) -> {
                    ObservableSupplier<Integer> hubOverviewColorSupplier =
                            hubManager.getHubOverviewColorSupplier();
                    Callback<Integer> hubOverviewColorObserver = overviewColorSupplier::set;
                    hubOverviewColorSupplier.addObserver(hubOverviewColorObserver);

                    mCleanUpHubOverviewColorObserver =
                            () -> hubOverviewColorSupplier.removeObserver(hubOverviewColorObserver);
                });
        return overviewColorSupplier;
    }

    private Pane createTabSwitcherPane(boolean isIncognito) {
        TabManagementDelegate delegate = TabManagementDelegateProvider.getDelegate();
        TabGroupCreationUiDelegate tabGroupCreationUiDelegate =
                delegate.createTabGroupCreationUiFlow(
                        this,
                        getModalDialogManager(),
                        mHubManagerSupplier,
                        mTabModelSelector
                                .getTabGroupModelFilterProvider()
                                .getCurrentTabGroupModelFilterSupplier());
        Pair<TabSwitcher, Pane> result =
                delegate.createTabSwitcherPane(
                        this,
                        getLifecycleDispatcher(),
                        getProfileProviderSupplier(),
                        getTabModelSelector(),
                        getTabContentManager(),
                        getTabCreatorManagerSupplier().get(),
                        getBrowserControlsManager(),
                        getMultiWindowModeStateDispatcher(),
                        mRootUiCoordinator.getScrimManager(),
                        getSnackbarManager(),
                        getModalDialogManager(),
                        mRootUiCoordinator.getBottomSheetController(),
                        mRootUiCoordinator.getDataSharingTabManager(),
                        mRootUiCoordinator.getIncognitoReauthControllerSupplier(),
                        mNewTabButtonClickListener,
                        isIncognito,
                        adaptOnOverviewColorAlphaChange(),
                        mBackPressManager,
                        mEdgeToEdgeControllerSupplier,
                        mRootUiCoordinator.getDesktopWindowStateManager(),
                        mTabModelNotificationDotManager.getNotificationDotObservableSupplier(),
                        getCompositorViewHolderSupplier(),
                        getShareDelegateSupplier(),
                        mTabBookmarkerSupplier,
                        tabGroupCreationUiDelegate,
                        mUndoBarPopupController,
                        mHubProvider.getHubManagerSupplier(),
                        mArchivedTabsAutoDeletePromoManager,
                        () ->
                                ((TabbedRootUiCoordinator) mRootUiCoordinator)
                                        .getTabGroupSyncController(),
                        mLayoutStateProviderSupplier,
                        getXrSpaceModeObservableSupplier(),
                        mMultiInstanceManager,
                        mDragDropDelegate);
        if (didFinishNativeInitialization()) {
            result.first.initWithNative();
        }
        if (isIncognito) {
            mIncognitoTabSwitcherSupplier.set(result.first);
        } else {
            mTabSwitcherSupplier.set(result.first);
        }
        return result.second;
    }

    private Pane createTabGroupsPane() {
        return TabManagementDelegateProvider.getDelegate()
                .createTabGroupsPane(
                        this,
                        getTabModelSelector(),
                        adaptOnOverviewColorAlphaChange(),
                        getProfileProviderSupplier(),
                        mHubProvider.getHubManagerSupplier(),
                        () ->
                                ((TabbedRootUiCoordinator) mRootUiCoordinator)
                                        .getTabGroupSyncController(),
                        getModalDialogManagerSupplier(),
                        mEdgeToEdgeControllerSupplier,
                        mRootUiCoordinator.getDataSharingTabManager());
    }

    private Pane createHistoryPane() {
        return new HistoryPane(
                adaptOnOverviewColorAlphaChange(),
                this,
                getSnackbarManager(),
                getProfileProviderSupplier(),
                mRootUiCoordinator::getBottomSheetController,
                getCurrentTabModel().getCurrentTabSupplier());
    }

    private Pane createBookmarkPane() {
        return new BookmarkPane(
                adaptOnOverviewColorAlphaChange(),
                this,
                getSnackbarManager(),
                getProfileProviderSupplier());
    }

    private Pane createCrossDevicePane() {
        return CrossDevicePaneFactory.create(
                this, adaptOnOverviewColorAlphaChange(), mEdgeToEdgeControllerSupplier);
    }

    private void setupCompositorContent() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.setupCompositorContent")) {
            if (!isLayoutManagerCreated()) {
                if (isTablet()) {
                    setupCompositorContentForTablet();
                } else {
                    setupCompositorContentForPhone();
                }
            }

            mLayoutManager.setEnableAnimations(DeviceClassManager.enableAnimations());
        }
    }

    private void initializeCompositorContent() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.initializeCompositorContent")) {
            // TODO(yusufo): get rid of findViewById(R.id.url_bar).
            initializeCompositorContent(
                    mLayoutManager, findViewById(R.id.url_bar), mControlContainer);
        }
    }

    private boolean isLayoutManagerCreated() {
        return mLayoutManager != null;
    }

    private void onTabSwitcherClicked() {
        var xrSceneCoreSessionManager = mXrSceneCoreSessionManagerSupplier.get();
        if (xrSceneCoreSessionManager != null) {
            // Do nothing if space mode switch is already started.
            xrSceneCoreSessionManager.startSpaceModeChange(
                    true, this::onTabSwitcherClickedInternal);
        } else {
            onTabSwitcherClickedInternal();
        }
    }

    private void onTabSwitcherClickedInternal() {
        Profile profile = mTabModelProfileSupplier.get();
        if (profile != null) {
            TrackerFactory.getTrackerForProfile(profile)
                    .notifyEvent(EventConstants.TAB_SWITCHER_BUTTON_CLICKED);
        }

        if (getFullscreenManager().getPersistentFullscreenMode()) {
            return;
        }

        ReturnToChromeUtil.recordClickTabSwitcher(getTabModelSelector().getCurrentTab());

        showOverview();
    }

    private void initializeToolbarManager() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.initializeToolbarManager")) {
            mUndoBarPopupController.initialize();

            OnClickListener bookmarkClickHandler =
                    v -> mTabBookmarkerSupplier.get().addOrEditBookmark(getActivityTab());

            Profile profile = mTabModelProfileSupplier.get();
            ObservableSupplier<Integer> archivedTabCountSupplier =
                    ArchivedTabModelOrchestrator.getForProfile(profile).getTabCountSupplier();
            getToolbarManager()
                    .initializeWithNative(
                            mLayoutManager,
                            mLayoutManager.getStripLayoutHelperManager(),
                            this::onTabSwitcherClicked,
                            bookmarkClickHandler,
                            /* customTabsBackClickHandler= */ null,
                            archivedTabCountSupplier,
                            mTabModelNotificationDotManager.getNotificationDotObservableSupplier(),
                            mUndoBarPopupController);

            // TODO(crbug.com/40828084): Fix this assert which is tripping on unrelated
            // tests.
            // assert !(mOverviewModeController != null
            //         && mOverviewModeController.overviewVisible());
        }
    }

    private void maybeCreateIncognitoTabSnapshotController() {
        try (TraceEvent e =
                TraceEvent.scoped(
                        "ChromeTabbedActivity.maybeCreateIncognitoTabSnapshotController")) {
            if (!CommandLine.getInstance()
                    .hasSwitch(ChromeSwitches.ENABLE_INCOGNITO_SNAPSHOTS_IN_ANDROID_RECENTS)) {
                IncognitoTabbedSnapshotController.createIncognitoTabSnapshotController(
                        this, mLayoutManager, mTabModelSelector, getLifecycleDispatcher());
            }

            mUiWithNativeInitialized = true;

            // The dataset has already been created, we need to initialize our state.
            mTabModelSelector.notifyChanged();

            // Check for incognito tabs to handle the case where Chrome was swiped away in the
            // background.
            if (!IncognitoTabHostUtils.doIncognitoTabsExist()) {
                IncognitoNotificationManager.dismissIncognitoNotification();
                DownloadNotificationService.getInstance().cancelOffTheRecordDownloads();
            }
        }
    }

    private void maybeGetFeedAppLifecycleAndMaybeCreatePageViewObserver() {
        try (TraceEvent e =
                TraceEvent.scoped(
                        "ChromeTabbedActivity."
                                + "maybeGetFeedAppLifecycleAndMaybeCreatePageViewObserver")) {
            FeedSurfaceTracker.getInstance().startup();

            getProfileProviderSupplier()
                    .runSyncOrOnAvailable(
                            (profileProvider) -> {
                                UsageStatsService.createPageViewObserverIfEnabled(
                                        this,
                                        profileProvider.getOriginalProfile(),
                                        getActivityTabProvider(),
                                        getTabContentManagerSupplier());
                            });
        }
    }

    // Determine if the launch cause was due to the main Chrome launcher icon, or a shortcut being
    // pressed before ChromeActivity#onResumeWithNative has run.
    private boolean isMainIntentLaunchPreOnResume() {
        @ActivityState int state = ApplicationStatus.getStateForActivity(this);
        assert state < ActivityState.RESUMED : "This method should only be used pre onResume";

        Intent launchIntent = getIntent();
        if (launchIntent == null) return false;

        // Ignore if launched from recents.
        if (0 != (launchIntent.getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY)) {
            return false;
        }

        if (IntentUtils.isMainIntentFromLauncher(launchIntent)) {
            return true;
        }

        if (IntentUtils.safeGetBooleanExtra(
                        launchIntent, IntentHandler.EXTRA_INVOKED_FROM_SHORTCUT, false)
                && IntentHandler.wasIntentSenderChrome(launchIntent)) {
            return true;
        }

        return false;
    }

    // Determine if the launch cause was due to the main Chrome launcher icon, or a shortcut being
    // pressed after ChromeActivity#onResumeWithNative has run. Note: A distinction between pre and
    // post onResume is made because LaunchCauseMetrics can only be used after that event has run,
    // and the values returned can differ based on the method used.
    private boolean isMainIntentLaunchPostOnResume() {
        @ActivityState int state = ApplicationStatus.getStateForActivity(this);
        assert state >= ActivityState.RESUMED : "This method can only be used post onResume";

        int launchCause = getLaunchCause();
        assert launchCause != LaunchCauseMetrics.LaunchCause.UNINITIALIZED
                : "Launch Cause has not been computed for this start yet.";
        return (launchCause == LaunchCauseMetrics.LaunchCause.MAIN_LAUNCHER_ICON
                || launchCause == LaunchCauseMetrics.LaunchCause.MAIN_LAUNCHER_ICON_SHORTCUT);
    }

    // Returns whether startup was cold or not.
    private boolean isColdStart() {
        return ColdStartTracker.wasColdOnFirstActivityCreationOrNow()
                && SimpleStartupForegroundSessionDetector.runningCleanForegroundSession();
    }

    @Override
    protected OneshotSupplier<ProfileProvider> createProfileProvider() {
        return new ActivityProfileProvider(getLifecycleDispatcher());
    }

    @Override
    public void startNativeInitialization() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.startNativeInitialization")) {
            startUmaSession();
            // This is on the critical path so don't delay.
            setupCompositorContent();
            if (!DeviceFormFactor.isTablet()) {
                PostTask.postTask(
                        TaskTraits.UI_DEFAULT,
                        mCallbackController.makeCancelable(this::initializeCompositorContent));
            } else {
                // TODO(crbug.com/40853081): Enable split compositor task on tablets.
                initializeCompositorContent();
            }

            // All this initialization can be expensive so it's split into multiple tasks.
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT, mCallbackController.makeCancelable(this::refreshSignIn));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(this::initializeToolbarManager));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(
                            this::maybeCreateIncognitoTabSnapshotController));
            // Always call into this function, even if BackPressManager is disabled to initialize
            // back press managers which reduce code duplication in this class.
            PostTask.postTask(TaskTraits.UI_DEFAULT, this::initializeBackPressHandlers);
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(
                            this::maybeGetFeedAppLifecycleAndMaybeCreatePageViewObserver));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(this::finishNativeInitialization));
        }
    }

    @Override
    public void finishNativeInitialization() {
        try (TraceEvent te = TraceEvent.scoped("ChromeTabbedActivity.finishNativeInitialization")) {
            assert getProfileProviderSupplier().hasValue();
            new NavigationPredictorBridge(
                    getProfileProviderSupplier().get().getOriginalProfile(),
                    getLifecycleDispatcher(),
                    this::isWarmOnResume);

            super.finishNativeInitialization();

            recordFirstAppLaunchTimestampIfNeeded();
            // TODO(jinsukkim): Let these classes handle the registration by themselves.
            mCompositorViewHolder = getCompositorViewHolderSupplier().get();
            getTabObscuringHandler().addObserver(mCompositorViewHolder);

            ChromeAccessibilityUtil.get().addObserver(mLayoutManager);
            if (isTablet()) {
                ChromeAccessibilityUtil.get().addObserver(mCompositorViewHolder);
            }

            TabSwitcher switcher = mTabSwitcherSupplier.get();
            if (switcher != null) {
                switcher.initWithNative();
            }
            TabSwitcher incognitoSwitcher = mIncognitoTabSwitcherSupplier.get();
            if (incognitoSwitcher != null) {
                incognitoSwitcher.initWithNative();
            }

            mInactivityTracker.setLastVisibleTimeMsAndRecord(System.currentTimeMillis());

            getSnackbarManager().setEdgeToEdgeSupplier(getEdgeToEdgeSupplier().get());

            if (ChromeFeatureList.isEnabled(ChromeFeatureList.GROUP_SUGGESTION_SERVICE)) {
                mSuggestionEventObserver =
                        new SuggestionEventObserver(mTabModelSelector, mHubManagerSupplier);
                if (ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                        ChromeFeatureList.GROUP_SUGGESTION_SERVICE,
                        GroupSuggestionsPromotionCoordinator.CREATE_SUGGESTIONS_PROMOTION_UI_PARAM,
                        true)) {
                    mGroupSuggestionsPromotionCoordinator =
                            new GroupSuggestionsPromotionCoordinator(
                                    this,
                                    mRootUiCoordinator.getBottomSheetController(),
                                    mTabModelSelector
                                            .getTabGroupModelFilterProvider()
                                            .getTabGroupModelFilter(false));
                }
            }

            initiateArchivedTabsAutoDeletePromoManager();
        }
    }

    @Override
    public void onResumeWithNative() {
        // While the super#onResumeWithNative call below also invokes #startUmaSession, we call
        // it here (early) as well to setup the UMA activity state for any metrics emitted prior
        // to call to super#onResumeWithNative below. It's safe to call #startUmaSession in both
        // places as a session will only be started if one is not already running.
        startUmaSession();

        // On warm startup, call setInitialOverviewState in onResume() instead of onStart(). This is
        // because onResume() is guaranteed to called after onNewIntent() and thus have the updated
        // Intent which is used by shouldShowOverviewPageOnStart(). See https://crbug.com/1321607.
        if (mFromResumption) {
            setInitialOverviewState();
        } else {
            // Set mFromResumption to be true to skip the call of setInitialOverviewState() in
            // onStart() when the next time onStart() is called, since it is no longer a cold start.
            mFromResumption = true;
        }

        super.onResumeWithNative();

        if (!isColdStart() && isMainIntentLaunchPostOnResume()) {
            StartupLatencyInjector startupLatencyInjector = new StartupLatencyInjector();
            startupLatencyInjector.maybeInjectLatency();
        }

        assert getProfileProviderSupplier().hasValue();
        getProfileProviderSupplier()
                .runSyncOrOnAvailable(
                        (profileProvider) -> {
                            if (mIncognitoCookiesFetcher == null) {
                                mIncognitoCookiesFetcher =
                                        new CookiesFetcher(
                                                profileProvider, CipherLazyHolder.sCipherInstance);
                            }
                            IncognitoStartup.onResumeWithNative(
                                    profileProvider,
                                    mIncognitoCookiesFetcher,
                                    getTabModelSelectorSupplier(),
                                    TABBED_MODE_COMPONENT_NAMES);
                        });

        mLocaleManager.setSnackbarManager(getSnackbarManager());
        mLocaleManager.startObservingPhoneChanges();

        // This call is not guarded by a feature flag.
        SearchEngineChoiceNotification.handleSearchEngineChoice(this, getSnackbarManager());

        if (!isWarmOnResume()) {
            getProfileProviderSupplier()
                    .runSyncOrOnAvailable(
                            (profileProvider) -> {
                                boolean isArticlesListVisible =
                                        UserPrefs.get(profileProvider.getOriginalProfile())
                                                .getBoolean(Pref.ARTICLES_LIST_VISIBLE);
                                FeedUma.recordArticlesListVisible(isArticlesListVisible);
                            });
        } else {
            mInactivityTracker.setLastVisibleTimeMsAndRecord(System.currentTimeMillis());
        }
    }

    @Override
    public void onPauseWithNative() {
        mTabModelSelector.commitAllTabClosures();

        if (mIncognitoCookiesFetcher != null) {
            mIncognitoCookiesFetcher.persistCookies();
        }

        mLocaleManager.setSnackbarManager(null);
        mLocaleManager.stopObservingPhoneChanges();

        // Always track the last backgrounded time in case others are using the pref.
        mInactivityTracker.setLastBackgroundedTimeInPrefs(System.currentTimeMillis());

        MultiWindowUtils.recordTabCountForRelaunchWhenActivityPaused(mTabModelSelector, mWindowId);

        super.onPauseWithNative();
    }

    @Override
    public void onStopWithNative() {
        super.onStopWithNative();

        saveState();
    }

    @Override
    public void onStartWithNative() {
        // While the super#onStartWithNative call below also invokes #startUmaSession, we call
        // it here (early) as well to setup the UMA activity state for any metrics emitted prior
        // to call to super#onStartWithNative below. It's safe to call #startUmaSession in both
        // places as a session will only be started if one is not already running.
        startUmaSession();

        mMainIntentMetrics.logLaunchBehavior();

        super.onStartWithNative();

        FirstDrawDetector.waitForFirstDrawStrict(
                mContentContainer,
                () -> {
                    if (!isColdStart()) {
                        mTimeToFirstDrawAfterStartMs =
                                SystemClock.uptimeMillis() - getOnStartTimestampMs();
                    }
                });

        // Don't call setInitialOverviewState if 1) we're waiting for the tab's creation or we risk
        // showing a glimpse of the tab selector during start up. 2) on warm startup from an
        // resumption. Defer it to onResumeWitheNative() since it needs to check the latest Intent
        // which is only guaranteed to be updated onResume() if onNewIntent() is called.
        if (!mPendingInitialTabCreation && !mFromResumption) {
            setInitialOverviewState();
        }

        Bundle savedInstanceState = getSavedInstanceState();
        if (savedInstanceState != null
                && savedInstanceState.getBoolean(IS_INCOGNITO_SELECTED, false)) {
            // This will be executed only once since SavedInstanceState will be reset a few lines
            // later.
            AndroidSessionDurationsServiceState.restoreNativeFromSerialized(
                    savedInstanceState,
                    getCurrentTabModel()
                            .getProfile()
                            .getPrimaryOtrProfile(/* createIfNeeded= */ true));
        }

        resetSavedInstanceState();
        BookmarkUtils.maybeExpireLastBookmarkLocationForReadLater(
                mInactivityTracker.getTimeSinceLastBackgroundedMs());

        MultiWindowUtils.maybeRecordDesktopWindowCountHistograms(
                mRootUiCoordinator.getDesktopWindowStateManager(),
                mInstanceAllocationType,
                !mFromResumption);
    }

    @Override
    public void onNewIntent(Intent intent) {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onNewIntent")) {
            mIntentHandlingTimeMs = SystemClock.uptimeMillis();

            // Drop the cleaner intent since it's created in order to clear up the OS share sheet.
            if (ShareHelper.isCleanerIntent(intent)) {
                return;
            }

            // The intent to use in maybeDispatchExplicitMainViewIntent(). We're explicitly
            // adding NEW_TASK flag to make sure backing from CCT brings up the caller activity,
            // and not Chrome
            Intent intentForDispatching = new Intent(intent);
            intentForDispatching.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            @LaunchIntentDispatcher.Action
            int action =
                    maybeDispatchExplicitMainViewIntent(
                            intentForDispatching, DispatchedBy.ON_NEW_INTENT);
            if (action != LaunchIntentDispatcher.Action.CONTINUE) {
                // Pressing back button in CCT should bring user to the caller activity.
                moveTaskToBack(true);
                // Intent was dispatched to CustomTabActivity, consume it.
                return;
            }

            super.onNewIntent(intent);

            boolean shouldShowRegularOverviewMode =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_OPEN_REGULAR_OVERVIEW_MODE, false);
            if (shouldShowRegularOverviewMode && IntentHandler.wasIntentSenderChrome(intent)) {
                mTabModelSelector.selectModel(/* incognito= */ false);
                mLayoutManager.showLayout(LayoutType.TAB_SWITCHER, /* animate= */ false);
            }

            boolean shouldHideOverviewMode =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_EXIT_XR_OVERVIEW_MODE, false);
            if (shouldHideOverviewMode
                    && IntentHandler.wasIntentSenderChrome(intent)
                    && isInOverviewMode()) {
                // Request switching to Home Space mode on XR.
                var xrSceneCoreSessionManager = mXrSceneCoreSessionManagerSupplier.get();
                if (xrSceneCoreSessionManager != null) {
                    boolean isFsm =
                            xrSceneCoreSessionManager.getXrSpaceModeObservableSupplier().get();
                    if (isFsm) {
                        xrSceneCoreSessionManager.startSpaceModeChange(
                                /* fsmModeRequested= */ false,
                                () -> xrSceneCoreSessionManager.finishSpaceModeChange());
                    }
                }
                hideOverview(/* animate= */ true);
            }
            // Launch history on an already running instance of Chrome.
            maybeLaunchHistory();
        }
    }

    @Override
    public void onNewIntentWithNative(Intent intent) {
        try {
            TraceEvent.begin("ChromeTabbedActivity.onNewIntentWithNative");

            super.onNewIntentWithNative(intent);
            if (!IntentHandler.shouldIgnoreIntent(intent, this, /* isCustomTab= */ false)) {
                maybeHandleUrlIntent(intent);
            }

            if (IntentUtils.isMainIntentFromLauncher(intent)) {
                logMainIntentBehavior(intent);
            }

            if (CommandLine.getInstance().hasSwitch(ContentSwitches.ENABLE_TEST_INTENTS)) {
                handleDebugIntent(intent);
            }

            // Launch tab switcher if it is a data sharing intent.
            maybeShowTabSwitcherAfterTabModelLoad(intent);
        } finally {
            TraceEvent.end("ChromeTabbedActivity.onNewIntentWithNative");
        }
    }

    /**
     * @param intent The received intent.
     * @return Whether the Intent was successfully handled.
     */
    private boolean maybeHandleUrlIntent(Intent intent) {
        @Nullable TabGroupMetadata tabGroupMetadata = IntentHandler.getTabGroupMetadata(intent);
        return tabGroupMetadata != null
                ? maybeHandleGroupUrlsIntent(intent, tabGroupMetadata)
                : maybeHandleSingleUrlIntent(intent);
    }

    /**
     * @param intent The received intent.
     * @return Whether the Intent was successfully handled.
     */
    private boolean maybeHandleSingleUrlIntent(Intent intent) {
        String url = IntentHandler.getUrlFromIntent(intent);
        @TabOpenType int tabOpenType = IntentHandler.getTabOpenType(intent);
        int tabIdToBringToFront = IntentHandler.getBringTabToFrontId(intent);
        if (url == null && tabIdToBringToFront == Tab.INVALID_TAB_ID) return false;

        LoadUrlParams loadUrlParams =
                IntentHandler.createLoadUrlParamsForIntent(url, intent, mIntentHandlingTimeMs);

        if (IntentHandler.isIntentForMhtmlFileOrContent(intent)
                && tabOpenType == TabOpenType.OPEN_NEW_TAB
                && loadUrlParams.getReferrer() == null
                && loadUrlParams.getVerbatimHeaders() == null) {
            getProfileProviderSupplier()
                    .runSyncOrOnAvailable(
                            (profileProvider) -> {
                                handleMhtmlFileOrContentIntent(
                                        profileProvider.getOriginalProfile(), url, intent);
                            });
            return true;
        }
        processUrlViewIntent(
                loadUrlParams,
                tabOpenType,
                IntentUtils.safeGetStringExtra(intent, Browser.EXTRA_APPLICATION_ID),
                tabIdToBringToFront,
                intent);
        return true;
    }

    /**
     * @param intent The received intent.
     * @param tabGroupMetadata The metadata of the tab group being processed.
     * @return Whether the Intent was successfully handled.
     */
    private boolean maybeHandleGroupUrlsIntent(Intent intent, TabGroupMetadata tabGroupMetadata) {
        @TabOpenType int tabOpenType = IntentHandler.getTabOpenType(intent);

        // Records tab group reparenting group size histogram.
        int tabGroupSizeBeforeReparent = tabGroupMetadata.tabIdsToUrls.size();
        RecordHistogram.recordCount1000Histogram(
                "Android.Reparent.TabGroup.GroupSize", tabGroupSizeBeforeReparent);

        LinkedList<Tab> tabs = new LinkedList<>();
        for (Map.Entry<Integer, String> entry : tabGroupMetadata.tabIdsToUrls) {
            int tabId = entry.getKey();
            String url = entry.getValue();
            assert tabId != Tab.INVALID_TAB_ID : "Invalid tab ID";
            assert url != null : "URL is null";
            assert !url.isEmpty() : "URL is empty";

            // 1. Set intent tabId and url for each iteration.
            IntentHandler.setTabId(intent, tabId);
            intent.setData(Uri.parse(url));
            assert !IntentHandler.isIntentForMhtmlFileOrContent(intent)
                    : "Attempt to drop a tab group with MHTML tab";

            // 2. Create LoadUrlParams for the given URL.
            LoadUrlParams loadUrlParams =
                    IntentHandler.createLoadUrlParamsForIntent(url, intent, mIntentHandlingTimeMs);

            // 3. Process the intent and open a new tab for the URL.
            Tab tab =
                    processUrlViewIntent(
                            loadUrlParams,
                            tabOpenType,
                            IntentUtils.safeGetStringExtra(intent, Browser.EXTRA_APPLICATION_ID),
                            Tab.INVALID_TAB_ID,
                            intent);
            // Restores the correct tab order by adding the tab to the front. `tabIdsToUrls` were
            // stored in reverse to preserve open positions (see {@link
            // TabGroupMetadataExtractor#extractTabGroupMetadata}).
            tabs.add(0, tab);
        }

        // 4. Regroup tabs and restore the original group properties(e.g. color, title, collapsed
        // state).
        TabGroupModelFilter tabGroupModelFilter =
                mTabModelSelector
                        .getTabGroupModelFilterProvider()
                        .getTabGroupModelFilter(tabGroupMetadata.isIncognito);

        boolean isTabStripVisible = true;
        if (mLayoutManager != null) {
            StripLayoutHelperManager layoutHelperManager =
                    mLayoutManager.getStripLayoutHelperManager();
            isTabStripVisible =
                    layoutHelperManager != null
                            && layoutHelperManager.getStripVisibilityState()
                                    == StripVisibilityState.VISIBLE;
        }
        int dropIndex =
                intent.getIntExtra(IntentHandler.EXTRA_TAB_INDEX, TabModel.INVALID_TAB_INDEX);
        TabGroupUtils.regroupTabs(
                tabGroupModelFilter,
                tabs,
                tabGroupMetadata,
                TabGroupUtils.shouldApplyCollapsedState(
                        getCurrentTabModel(),
                        tabGroupMetadata.tabGroupCollapsed,
                        isTabStripVisible,
                        dropIndex));

        // Records tab group reparenting group size diff histogram.
        int tabGroupSizeAfterReparent =
                tabGroupModelFilter.getTabCountForGroup(tabGroupMetadata.tabGroupId);
        RecordHistogram.recordCount1000Histogram(
                "Android.Reparent.TabGroup.GroupSize.Diff",
                tabGroupSizeBeforeReparent - tabGroupSizeAfterReparent);

        // Records tab group reparenting duration histogram.
        long startTime =
                IntentUtils.safeGetLongExtra(intent, IntentHandler.EXTRA_REPARENT_START_TIME, -1);
        if (startTime > 0) {
            long endTime = SystemClock.elapsedRealtime();
            RecordHistogram.recordLongTimesHistogram(
                    "Android.Reparent.TabGroup.Duration", endTime - startTime);
        }
        IntentUtils.safeRemoveExtra(intent, IntentHandler.EXTRA_REPARENT_START_TIME);
        IntentUtils.safeRemoveExtra(intent, IntentHandler.EXTRA_TAB_GROUP_METADATA);
        return true;
    }

    private void handleMhtmlFileOrContentIntent(
            final Profile profile, final String url, final Intent intent) {
        OfflinePageUtils.getLoadUrlParamsForOpeningMhtmlFileOrContent(
                url,
                (loadUrlParams) -> {
                    loadUrlParams.setVerbatimHeaders(
                            IntentHandler.maybeAddAdditionalContentHeaders(
                                    intent, url, loadUrlParams.getVerbatimHeaders()));
                    processUrlViewIntent(
                            loadUrlParams,
                            TabOpenType.OPEN_NEW_TAB,
                            null,
                            Tab.INVALID_TAB_ID,
                            intent);
                },
                profile);
    }

    @Override
    public @ActivityType int getActivityType() {
        return ActivityType.TABBED;
    }

    @Override
    public ChromeTabCreator getTabCreator(boolean incognito) {
        return (ChromeTabCreator) super.getTabCreator(incognito);
    }

    @Override
    public ChromeTabCreator getCurrentTabCreator() {
        return (ChromeTabCreator) super.getCurrentTabCreator();
    }

    private void handleDebugIntent(Intent intent) {
        if (ACTION_CLOSE_TABS.equals(intent.getAction())) {
            CloseAllTabsHelper.closeAllTabsHidingTabGroups(
                    getTabModelSelectorSupplier().get(), /* allowUndo= */ true);
        } else if (MemoryPressureListener.handleDebugIntent(
                ChromeTabbedActivity.this, intent.getAction())) {
            // Handled.
        }
    }

    private void setTrackColdStartupMetrics(boolean shouldTrackColdStartupMetrics) {
        assert getLegacyTabStartupMetricsTracker() != null;
        assert getStartupMetricsTracker() != null;

        if (shouldTrackColdStartupMetrics) {
            getLegacyTabStartupMetricsTracker().setHistogramSuffix(ActivityType.TABBED);
            getStartupMetricsTracker().setHistogramSuffix(ActivityType.TABBED);
        } else {
            getLegacyTabStartupMetricsTracker().cancelTrackingStartupMetrics();
        }
    }

    private void setInitialOverviewState() {
        if (mFromResumption) {
            setInitialOverviewStateWithNtp();
        }
    }

    /**
     * Called on warm startup to show a home surface NTP instead of the last active Tab if the user
     * has left Chrome for a while.
     */
    private void setInitialOverviewStateWithNtp() {
        boolean showedNtp =
                ReturnToChromeUtil.setInitialOverviewStateOnResumeWithNtp(
                        mTabModelSelector.isIncognitoSelected(),
                        shouldShowNtpHomeSurfaceOnStartup(),
                        getCurrentTabModel(),
                        getTabCreator(false),
                        mHomeSurfaceTracker);
        if (showedNtp && mLayoutManager.isLayoutVisible(LayoutType.TAB_SWITCHER)) {
            mLayoutManager.showLayout(LayoutType.BROWSING, /* animate= */ false);
        }
    }

    private void logMainIntentBehavior(Intent intent) {
        assert IntentUtils.isMainIntentFromLauncher(intent);
        // TODO(tedchoc): We should cache the last visible time and reuse it to avoid different
        //                values of this depending on when it is called after the activity was
        //                shown.

        // Temporary safety check to make sure none of this code runs if the feature is
        // disabled.
        if (ReengagementNotificationController.isEnabled()) {
            if (mCallbackController != null) {
                new OneShotCallback<>(
                        mTabModelProfileSupplier,
                        mCallbackController.makeCancelable(
                                profile -> {
                                    assert profile != null
                                            : "Unexpectedly null profile from TabModel.";
                                    if (profile == null) return;

                                    TrackerFactory.getTrackerForProfile(profile)
                                            .notifyEvent(EventConstants.STARTED_FROM_MAIN_INTENT);
                                }));
            }
        }

        mMainIntentMetrics.onMainIntentWithNative(
                mInactivityTracker.getTimeSinceLastBackgroundedMs());
    }

    /** Access the main intent metrics for test validation. */
    public MainIntentBehaviorMetrics getMainIntentBehaviorMetricsForTesting() {
        return mMainIntentMetrics;
    }

    public ChromeInactivityTracker getInactivityTrackerForTesting() {
        return mInactivityTracker;
    }

    @Override
    public void initializeState() {
        // This method goes through 3 steps:
        // 1. Load the saved tab state (but don't start restoring the tabs yet).
        // 2. Process the Intent that this activity received and if that should result in any
        //    new tabs, create them.  This is done after step 1 so that the new tab gets
        //    created after previous tab state was restored.
        // 3. If no tabs were created in any of the above steps, create an NTP, otherwise
        //    start asynchronous tab restore (loading the previously active tab synchronously
        //    if no new tabs created in step 2).

        // Only look at the original intent if this is not a "restoration" and we are allowed to
        // process intents. Any subsequent intents are carried through onNewIntent.
        try {
            TraceEvent.begin("ChromeTabbedActivity.initializeState");

            super.initializeState();
            Log.i(TAG, "#initializeState");
            Intent intent = getIntent();

            boolean hadCipherData =
                    CipherLazyHolder.sCipherInstance.restoreFromBundle(getSavedInstanceState());

            boolean noRestoreState =
                    CommandLine.getInstance().hasSwitch(ChromeSwitches.NO_RESTORE_STATE);
            boolean shouldShowNtpAsHomeSurfaceAtStartup = false;
            final AtomicBoolean isActiveUrlNtp = new AtomicBoolean(false);
            if (noRestoreState) {
                // Clear the state files because they are inconsistent and useless from now on.
                mTabModelOrchestrator.clearState();
            } else {
                // State should be clear when we start first run and hence we do not need to load
                // a previous state. This may change the current Model, watch out for initialization
                // based on the model.
                // Never attempt to restore incognito tabs when this activity was previously swiped
                // away in Recents. http://crbug.com/626629
                boolean ignoreIncognitoFiles = !hadCipherData;

                // If the home surface should be shown on startup, check if the active tab restored
                // from disk is an NTP that can be reused for Start.
                Callback<String> onStandardActiveIndexRead = null;
                shouldShowNtpAsHomeSurfaceAtStartup = shouldShowNtpHomeSurfaceOnStartup();
                mHomeSurfaceTracker = new HomeSurfaceTracker();
                if (shouldShowNtpAsHomeSurfaceAtStartup) {
                    onStandardActiveIndexRead =
                            url -> {
                                mLastActiveTabUrl = url;
                                if (UrlUtilities.isNtpUrl(url)) {
                                    assert !mTabModelSelector.isIncognitoSelected();
                                    isActiveUrlNtp.set(true);
                                }
                            };
                }
                mTabModelOrchestrator.loadState(ignoreIncognitoFiles, onStandardActiveIndexRead);
            }

            getProfileProviderSupplier()
                    .onAvailable(
                            (profileProvider) -> {
                                if (isActivityFinishingOrDestroyed()) return;
                                // The AuxiliarySearchControllerFactory#setIsTablet() must be called
                                // before creating the controller which checks
                                // AuxiliarySearchControllerFactory#isEnabled().
                                AuxiliarySearchControllerFactory.getInstance()
                                        .setIsTablet(
                                                DeviceFormFactor.isWindowOnTablet(
                                                        getWindowAndroid()));
                                mAuxiliarySearchController =
                                        AuxiliarySearchControllerFactory.getInstance()
                                                .createAuxiliarySearchController(
                                                        this,
                                                        profileProvider.getOriginalProfile(),
                                                        mTabModelSelector,
                                                        AuxiliarySearchHostType.CTA);
                                if (mAuxiliarySearchController != null) {
                                    mAuxiliarySearchController.register(
                                            this.getLifecycleDispatcher());
                                }
                            });

            mInactivityTracker.register(this.getLifecycleDispatcher());
            boolean isIntentWithEffect = false;
            boolean isMainIntentFromLauncher = false;
            boolean isLaunchingDraggedTabOrGroup = false;
            if (getSavedInstanceState() == null && intent != null) {
                if (!shouldIgnoreIntent()) {
                    isLaunchingDraggedTabOrGroup = maybeLaunchDraggedTabOrGroupInWindow(intent);
                    // If launching tab or group drag was successful, ignore handling url intent
                    isIntentWithEffect =
                            isLaunchingDraggedTabOrGroup || maybeHandleUrlIntent(intent);
                }

                if (IntentUtils.isMainIntentFromLauncher(intent)) {
                    isMainIntentFromLauncher = true;
                    logMainIntentBehavior(intent);
                }
            }

            mIntentMetadataOneshotSupplier.set(
                    new ToolbarIntentMetadata(isMainIntentFromLauncher, isIntentWithEffect));

            // If we have tabs to reparent and getSavedInstanceState() is non-null, then the tabs
            // are coming from night mode tab reparenting. In this case, reparenting happens
            // synchronously along with tab restoration so there are no tabs waiting for
            // reparenting like there are for other tab reparenting operations.
            // Reparenting is also triggered when the intent launches the current window from a
            // dragged tab.
            boolean hasTabWaitingForReparenting =
                    (AsyncTabParamsManagerSingleton.getInstance().hasParamsWithTabToReparent()
                                    && getSavedInstanceState() == null)
                            || isLaunchingDraggedTabOrGroup;
            mCreatedTabOnStartup =
                    getCurrentTabModel().getCount() > 0
                            || mTabModelOrchestrator.getRestoredTabCount() > 0
                            || isIntentWithEffect
                            || hasTabWaitingForReparenting;

            // We always need to try to restore tabs. The set of tabs might be empty, but at least
            // it will trigger the notification that tab restore is complete which is needed by
            // other parts of Chrome such as sync.
            boolean activeTabBeingRestored = !isIntentWithEffect;

            if (shouldShowNtpAsHomeSurfaceAtStartup
                    && !isIntentWithEffect
                    && !hasTabWaitingForReparenting) {
                // If a home surface should be shown at startup on tablets and the last active Tab
                // is a NTP, we will reuse it to show the home surface UI. Otherwise, we'll create
                // one, and set it as the active Tab. |mLastActiveTabUrl| is null when there isn't
                // any Tab.
                if (!isActiveUrlNtp.get() && mLastActiveTabUrl != null) {
                    ReturnToChromeUtil.createNewTabAndShowHomeSurfaceUi(
                            getTabCreator(false),
                            mHomeSurfaceTracker,
                            mTabModelSelector,
                            mLastActiveTabUrl,
                            null);
                    activeTabBeingRestored = false;
                    mCreatedTabOnStartup = true;
                    mLastActiveTabUrl = null;
                }
                ReturnToChromeUtil.recordHomeSurfaceShownAtStartup();
                ReturnToChromeUtil.recordHomeSurfaceShown();
            }

            mTabModelOrchestrator.restoreTabs(activeTabBeingRestored);

            // Only create an initial tab if no tabs were restored and no intent was handled.
            // Also, check whether the active tab was supposed to be restored and that the total
            // tab count is now non zero.  If this is not the case, tab restore failed and we need
            // to create a new tab as well.
            if (!mCreatedTabOnStartup
                    || (!hasTabWaitingForReparenting
                            && activeTabBeingRestored
                            && getTabModelSelector().getTotalTabCount() == 0)) {
                // If homepage URI is not determined, due to PartnerBrowserCustomizations provider
                // async reading, then create a tab at the async reading finished. If it takes
                // too long, just create NTP.
                mPendingInitialTabCreation = true;
                PartnerBrowserCustomizations.getInstance()
                        .setOnInitializeAsyncFinished(
                                () -> {
                                    if (!isActivityFinishingOrDestroyed()) {
                                        createInitialTab();
                                    }
                                },
                                INITIAL_TAB_CREATION_TIMEOUT_MS);
            }

            // If initial tab creation is pending, this will instead be handled when we create the
            // initial tab in #createInitialTab.
            if (!mPendingInitialTabCreation) {
                Tab currentTab = getActivityTab();
                boolean isTabNtp = isTabRegularNtp(currentTab);
                if (isTabNtp && !currentTab.isNativePage()) {
                    // This will be a NTP, but the native page hasn't been created yet. Need to wait
                    // for this to be created before allowing the toolbar to draw.
                    currentTab.addObserver(
                            new EmptyTabObserver() {
                                @Override
                                public void onContentChanged(Tab tab) {
                                    tab.removeObserver(this);
                                    mAppLaunchDrawBlocker.onActiveTabAvailable();
                                }
                            });
                } else {
                    mAppLaunchDrawBlocker.onActiveTabAvailable();
                }
                // Launch history as a resumption of a previous Chrome journey.
                maybeLaunchHistory();
            }

            if (getSavedInstanceState() != null) {
                long unfoldLatencyBeginTime =
                        getSavedInstanceState()
                                .getLong(ChromeActivity.UNFOLD_LATENCY_BEGIN_TIMESTAMP);
                if (unfoldLatencyBeginTime != 0) {
                    getWindowAndroid().setUnfoldLatencyBeginTime(unfoldLatencyBeginTime);
                }
            }

            maybeShowTabSwitcherAfterTabModelLoad(intent);
        } finally {
            TraceEvent.end("ChromeTabbedActivity.initializeState");
        }
    }

    /**
     * Checks SharedPreferences for the first launch timestamp. If not set, records the current
     * time.
     */
    private void recordFirstAppLaunchTimestampIfNeeded() {
        SharedPreferencesManager prefs = ChromeSharedPreferences.getInstance();
        if (prefs.readLong(ChromePreferenceKeys.FIRST_CTA_START_TIMESTAMP, -1L) == -1L) {
            // Timestamp does not exist
            long currentTimestamp = System.currentTimeMillis();
            prefs.writeLong(ChromePreferenceKeys.FIRST_CTA_START_TIMESTAMP, currentTimestamp);
        }
    }

    private boolean hasStartWithNativeBeenCalled() {
        int activity_state = getLifecycleDispatcher().getCurrentActivityState();
        return activity_state == ActivityLifecycleDispatcher.ActivityState.STARTED_WITH_NATIVE
                || activity_state == ActivityLifecycleDispatcher.ActivityState.RESUMED_WITH_NATIVE;
    }

    /** Create an initial tab for cold start without restored tabs. */
    private void createInitialTab() {
        Log.i(TAG, "#createInitialTab executed.");
        mPendingInitialTabCreation = false;

        String url = null;
        GURL homepageGurl = HomepageManager.getInstance().getHomepageGurl();
        if (homepageGurl.isEmpty()) {
            url = UrlConstants.NTP_URL;
        } else {
            // Migrate legacy NTP URLs (chrome://newtab) to the newer format
            // (chrome-native://newtab)
            if (UrlUtilities.isNtpUrl(homepageGurl)) {
                url = UrlConstants.NTP_URL;
            } else {
                url = homepageGurl.getSpec();
            }
        }
        getTabCreator(false).launchUrl(url, TabLaunchType.FROM_STARTUP);
        PartnerBrowserCustomizations.getInstance()
                .onCreateInitialTab(
                        url,
                        getLifecycleDispatcher(),
                        HomepageManager::getHomepageCharacterizationHelper);

        // If we didn't call setInitialOverviewState() in onStartWithNative() because
        // mPendingInitialTabCreation was true then do so now.
        if (hasStartWithNativeBeenCalled()) {
            setInitialOverviewState();
        }

        mAppLaunchDrawBlocker.onActiveTabAvailable();
        // Launch history as a fresh instance of Chrome.
        maybeLaunchHistory();
    }

    private void recordExternalIntentSourceUMA(Intent intent) {
        @IntentHandler.ExternalAppId
        int externalId = IntentHandler.determineExternalIntentSource(intent, this);

        // Don't record external app page loads for intents we sent.
        if (externalId == IntentHandler.ExternalAppId.CHROME) return;
        RecordHistogram.recordEnumeratedHistogram(
                "MobileIntent.PageLoadDueToExternalApp",
                externalId,
                IntentHandler.ExternalAppId.NUM_ENTRIES);

        maybeRecordExternalAppClickInfo(externalId, intent);
    }

    private void maybeRecordExternalAppClickInfo(
            @IntentHandler.ExternalAppId int externalId, Intent intent) {
        if (externalId != ExternalAppId.PIXEL_LAUNCHER
                || externalId != ExternalAppId.SAMSUNG_LAUNCHER) {
            return;
        }

        AuxiliarySearchMetrics.maybeRecordExternalAppClickInfo(
                externalId == ExternalAppId.PIXEL_LAUNCHER
                        ? PIXEL_LAUNCHER_NAME
                        : THIRD_PARTY_LAUNCHER_NAME,
                intent);
    }

    /**
     * Records an action when a user chose to handle a URL in Chrome that could have been handled
     * by an application installed on the phone. Also records the name of that application.
     * This doesn't include generic URL handlers, such as browsers.
     */
    private static void recordAppHandlersForIntent(Intent intent) {
        List<String> packages =
                IntentUtils.safeGetStringArrayListExtra(
                        intent, ExternalNavigationHandler.EXTRA_EXTERNAL_NAV_PACKAGES);
        if (packages != null && packages.size() > 0) {
            RecordUserAction.record("MobileExternalNavigationReceived");
        }
    }

    /** Processes a url view intent. */
    private @Nullable Tab processUrlViewIntent(
            LoadUrlParams loadUrlParams,
            @TabOpenType int tabOpenType,
            String externalAppId,
            int tabIdToBringToFront,
            Intent intent) {
        if (isActivityFinishingOrDestroyed()) {
            return null;
        }
        if (isProbablyFromChrome(intent, externalAppId)) {
            RecordUserAction.record("MobileTabbedModeViewIntentFromChrome");
        } else {
            RecordUserAction.record("MobileTabbedModeViewIntentFromApp");
        }

        recordExternalIntentSourceUMA(intent);
        recordAppHandlersForIntent(intent);

        final String url = loadUrlParams.getUrl();
        boolean fromLauncherShortcut =
                IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_SHORTCUT, false);
        boolean fromAppWidget =
                IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_APP_WIDGET, false);
        boolean focus = false;

        // TODO(crbug.com/418106849): We should use regular tab model in case the url
        // will open in a regular tab even if the current tab model is incognito.
        TabModel tabModel = getCurrentTabModel();
        final Tab resultTab;
        switch (tabOpenType) {
            case TabOpenType.REUSE_URL_MATCHING_TAB_ELSE_NEW_TAB:
                mTabModelOrchestrator.tryToRestoreTabStateForUrl(url);
                int tabToBeClobberedIndex = TabModelUtils.getTabIndexByUrl(tabModel, url);
                Tab tabToBeClobbered = tabModel.getTabAt(tabToBeClobberedIndex);
                if (tabToBeClobbered != null) {
                    TabModelUtils.setIndex(tabModel, tabToBeClobberedIndex);
                    tabToBeClobbered.reload();
                    resultTab = tabToBeClobbered;
                } else {
                    resultTab = launchIntent(loadUrlParams, externalAppId, true, intent);
                }
                int shortcutSource =
                        intent.getIntExtra(WebappConstants.EXTRA_SOURCE, ShortcutSource.UNKNOWN);
                LaunchMetrics.recordHomeScreenLaunchIntoTab(url, shortcutSource);
                if (fromAppWidget && UrlConstants.CHROME_DINO_URL.equals(url)) {
                    RecordUserAction.record("QuickActionSearchWidget.StartDinoGame");
                }
                break;
            case TabOpenType.BRING_TAB_TO_FRONT:
                // Hub search will allow archived tabs to be passed through here. In this case, CTA
                // should be warm and be in a PAUSED state in the background. This is best-effort
                // and may fail if that's not the case.
                ArchivedTabModelOrchestrator archivedOrchestrator =
                        ArchivedTabModelOrchestrator.getForProfile(mTabModelProfileSupplier.get());
                @Nullable TabModel archivedTabModel = archivedOrchestrator.getTabModel();
                @Nullable
                Tab archivedTab =
                        archivedTabModel == null
                                ? null
                                : archivedOrchestrator
                                        .getTabModel()
                                        .getTabById(tabIdToBringToFront);
                if (archivedTab != null) {
                    archivedOrchestrator
                            .getTabArchiver()
                            .unarchiveAndRestoreTabs(
                                    tabModel.getTabCreator(),
                                    Arrays.asList(archivedTab),
                                    /* updateTimestamp= */ true,
                                    /* areTabsBeingOpened= */ true);
                } else {
                    mTabModelOrchestrator.tryToRestoreTabStateForId(tabIdToBringToFront);
                }

                int tabIndex = TabModelUtils.getTabIndexById(tabModel, tabIdToBringToFront);
                if (tabIndex == TabModel.INVALID_TAB_INDEX) {
                    TabModel otherModel = getTabModelSelector().getModel(!tabModel.isIncognito());
                    tabIndex = TabModelUtils.getTabIndexById(otherModel, tabIdToBringToFront);
                    if (tabIndex != TabModel.INVALID_TAB_INDEX) {
                        getTabModelSelector().selectModel(otherModel.isIncognito());
                        TabModelUtils.setIndex(otherModel, tabIndex);
                        resultTab = otherModel.getTabAt(tabIndex);
                    } else {
                        Log.e(TAG, "Failed to bring tab to front because it doesn't exist.");
                        return null;
                    }
                } else {
                    TabModelUtils.setIndex(tabModel, tabIndex);
                    resultTab = tabModel.getTabAt(tabIndex);
                }

                LayoutManagerChrome layoutManager = getLayoutManager();
                // If the tab-switcher is displayed, hide it to show the tab.
                if (layoutManager != null
                        && layoutManager.isLayoutVisible(LayoutType.TAB_SWITCHER)) {
                    layoutManager.showLayout(LayoutType.BROWSING, /* animate= */ false);
                }

                break;
            case TabOpenType.CLOBBER_CURRENT_TAB:
                // The browser triggered the intent. This happens when clicking links which
                // can be handled by other applications (e.g. www.youtube.com links).
                Tab currentTab = getActivityTab();
                if (currentTab != null) {
                    RedirectHandlerTabHelper.updateIntentInTab(currentTab, intent);
                    currentTab.loadUrl(loadUrlParams);
                    resultTab = currentTab;
                } else {
                    resultTab = launchIntent(loadUrlParams, externalAppId, true, intent);
                }
                break;
            case TabOpenType.REUSE_APP_ID_MATCHING_TAB_ELSE_NEW_TAB:
                resultTab = launchIntent(loadUrlParams, externalAppId, false, intent);
                break;
            case TabOpenType.REUSE_TAB_MATCHING_ID_ELSE_NEW_TAB:
                int tabId =
                        IntentUtils.safeGetIntExtra(
                                intent,
                                TabOpenType.REUSE_TAB_MATCHING_ID_STRING,
                                Tab.INVALID_TAB_ID);
                if (tabId != Tab.INVALID_TAB_ID) {
                    mTabModelOrchestrator.tryToRestoreTabStateForId(tabId);
                    int matchingTabIndex = TabModelUtils.getTabIndexById(tabModel, tabId);
                    if (matchingTabIndex != TabModel.INVALID_TAB_INDEX) {
                        Tab tab = tabModel.getTabAt(matchingTabIndex);
                        String spec = tab.getUrl().getSpec();
                        if (spec.equals(url)
                                || spec.equals(
                                        IntentUtils.safeGetStringExtra(
                                                intent,
                                                TabOpenType.REUSE_TAB_ORIGINAL_URL_STRING))) {
                            tabModel.setIndex(matchingTabIndex, TabSelectionType.FROM_USER);
                            tab.loadUrl(loadUrlParams);
                            resultTab = tab;
                        } else {
                            resultTab = launchIntent(loadUrlParams, externalAppId, false, intent);
                        }
                    } else {
                        resultTab = launchIntent(loadUrlParams, externalAppId, false, intent);
                    }
                } else {
                    resultTab = null;
                }
                break;
            case TabOpenType.OPEN_NEW_TAB:
                if (fromLauncherShortcut) {
                    recordLauncherShortcutAction(false);
                    reportNewTabShortcutUsed(false);
                }

                resultTab = launchIntent(loadUrlParams, externalAppId, true, intent);
                break;
            case TabOpenType.OPEN_NEW_INCOGNITO_TAB:
                if (!TextUtils.equals(externalAppId, getPackageName())) {
                    assert false : "Only Chrome is allowed to open incognito tabs";
                    Log.e(TAG, "Only Chrome is allowed to open incognito tabs");
                    return null;
                }

                if (!IncognitoUtils.isIncognitoModeEnabled(
                        getProfileProviderSupplier().get().getOriginalProfile())) {
                    // The incognito launcher shortcut is manipulated in #onDeferredStartup(),
                    // so it's possible for a user to invoke the shortcut before it's disabled.
                    // Quick actions search widget is installed on the home screen and may
                    // need to be updated before the incognito button is removed.
                    // Opening an incognito tab while incognito mode is disabled from somewhere
                    // besides the launcher shortcut of from quick action search widget is an
                    // error.
                    if (fromAppWidget || fromLauncherShortcut) {
                        // We are using the message introduced for quick action search widget
                        // for both the widget and the launcher shortcut here.
                        Toast.makeText(
                                        ChromeTabbedActivity.this,
                                        R.string.quick_action_search_widget_message_no_incognito,
                                        Toast.LENGTH_LONG)
                                .show();
                    } else {
                        assert false : "Tried to open incognito tab while incognito disabled";
                        Log.e(TAG, "Tried to open incognito tab while incognito disabled");
                    }

                    return null;
                }

                if (url == null || url.equals(UrlConstants.NTP_URL)) {
                    if (fromLauncherShortcut) {
                        resultTab =
                                getTabCreator(true)
                                        .launchUrl(
                                                UrlConstants.NTP_URL,
                                                TabLaunchType.FROM_LAUNCHER_SHORTCUT);
                        recordLauncherShortcutAction(true);
                        reportNewTabShortcutUsed(true);
                    } else if (fromAppWidget) {
                        RecordUserAction.record("QuickActionSearchWidget.StartIncognito");
                        resultTab =
                                getTabCreator(true)
                                        .launchUrl(
                                                UrlConstants.NTP_URL,
                                                TabLaunchType.FROM_APP_WIDGET);
                    } else if (IncognitoTabLauncher.didCreateIntent(intent)) {
                        resultTab =
                                getTabCreator(true)
                                        .launchUrl(
                                                UrlConstants.NTP_URL,
                                                TabLaunchType.FROM_LAUNCH_NEW_INCOGNITO_TAB);
                        if (IncognitoTabLauncher.shouldFocusOmnibox(intent)) {
                            // Since the Tab is created in the foreground, its View will gain
                            // focus, and since the Tab and the URL bar are not yet in the same
                            // View hierarchy, setting the URL bar's focus here won't clear the
                            // Tab's focus. When the Tab is added to the hierarchy, we want the
                            // URL bar to retain focus, so we clear the Tab's focus here.
                            resultTab.getView().clearFocus();
                            focus = true;
                        }

                        IncognitoTabLauncher.recordUse();
                    } else {
                        // Used by the Account management screen to open a new incognito tab.
                        // Account management screen collects its metrics separately.
                        resultTab =
                                getTabCreator(true)
                                        .launchUrl(
                                                UrlConstants.NTP_URL,
                                                TabLaunchType.FROM_CHROME_UI,
                                                intent,
                                                mIntentHandlingTimeMs);
                    }
                } else {
                    resultTab = launchIntent(loadUrlParams, externalAppId, true, intent);
                }
                break;
            default:
                assert false : "Unknown TabOpenType: " + tabOpenType;
                resultTab = null;
                break;
        }

        getToolbarManager()
                .setUrlBarFocusOnceNativeInitialized(
                        focus,
                        focus
                                ? OmniboxFocusReason.LAUNCH_NEW_INCOGNITO_TAB
                                : OmniboxFocusReason.UNFOCUS);

        if (tabModel.getCount() > 0 && isInOverviewMode() && !isTablet()) {
            // Hides the overview page to ensure proper layout change signals are sent.
            hideOverview(/* animate= */ false);
        }
        return resultTab;
    }

    private boolean isProbablyFromChrome(Intent intent, String externalAppId) {
        // To determine if the processed intent is from Chrome, check for any of the following:
        // 1.) The authentication token that will be added to trusted intents.
        // 2.) The app ID matches Chrome.  This value can be spoofed by other applications, but
        //     in cases where we were not able to add the authentication token this is our only
        //     indication the intent was from Chrome.
        return IntentHandler.wasIntentSenderChrome(intent)
                || TextUtils.equals(externalAppId, getPackageName());
    }

    private void maybeLaunchHistory() {
        // Can be launched as (1) a fresh instance of Chrome (2) a new intent on an already running
        // instance of Chrome or (3) a resumption of a previous Chrome journey.
        if (!HistoryManager.isAppSpecificHistoryEnabled()) return;

        boolean shouldLaunchHistory =
                IntentUtils.safeGetBooleanExtra(
                        getIntent(), IntentHandler.EXTRA_OPEN_HISTORY, false);
        if (!shouldLaunchHistory) return;

        getProfileProviderSupplier()
                .runSyncOrOnAvailable(
                        (provider) -> {
                            // History page is always empty if the current tab is incognito. Ensure
                            // the profile flips to the regular one when showing the history page.
                            HistoryManagerUtils.showHistoryManager(
                                    this, getActivityTab(), provider.getOriginalProfile());
                        });
    }

    private boolean maybeLaunchDraggedTabOrGroupInWindow(Intent intent) {
        @Nullable TabGroupMetadata tabGroupMetadata = IntentHandler.getTabGroupMetadata(intent);
        return tabGroupMetadata != null
                ? maybeLaunchDraggedTabGroupInWindow(intent, tabGroupMetadata)
                : maybeLaunchDraggedTabInWindow(intent);
    }

    private boolean maybeLaunchDraggedTabInWindow(Intent intent) {
        int draggedTabId =
                IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_DRAGGED_TAB_ID, Tab.INVALID_TAB_ID);
        if (draggedTabId == Tab.INVALID_TAB_ID) return false;
        if (!IntentHandler.wasIntentSenderChrome(intent)) return false;
        if (mMultiInstanceManager == null) return false;

        // |draggedTabId| is retrieved from the activity the tab is being dragged from.
        int windowId =
                IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_DRAGDROP_TAB_WINDOW_ID, INVALID_WINDOW_ID);
        Tab tab = TabWindowManagerSingleton.getInstance().getTabById(draggedTabId, windowId);
        if (tab == null) {
            RecordHistogram.recordBooleanHistogram(HISTOGRAM_DRAGGED_TAB_OPENED_NEW_WINDOW, false);
            return false;
        }
        boolean isTabInGroup = tab.getTabGroupId() != null;

        mMultiInstanceManager.moveTabToWindow(this, tab, /* atIndex= */ 0);

        if (isTabInGroup) RecordUserAction.record("MobileToolbarReorderTab.TabRemovedFromGroup");
        RecordHistogram.recordBooleanHistogram(HISTOGRAM_DRAGGED_TAB_OPENED_NEW_WINDOW, true);
        DragDropMetricUtils.recordDragDropType(
                ChromeDragDropUtils.getDragDropTypeFromIntent(intent),
                AppHeaderUtils.isAppInDesktopWindow(
                        mRootUiCoordinator.getDesktopWindowStateManager()),
                /* isTabGroup= */ false);
        return true;
    }

    private boolean maybeLaunchDraggedTabGroupInWindow(
            Intent intent, @NonNull TabGroupMetadata tabGroupMetadata) {
        @UrlIntentSource
        int intentSource =
                IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_URL_DRAG_SOURCE, UrlIntentSource.UNKNOWN);
        if (intentSource != UrlIntentSource.TAB_GROUP_IN_STRIP) return false;
        if (mMultiInstanceManager == null) return false;

        // The metadata was attached as part of a drag and drop operation, so detach the target
        // group and reparent them to this instance now.
        mMultiInstanceManager.moveTabGroupToWindow(this, tabGroupMetadata, /* atIndex= */ 0);
        DragDropMetricUtils.recordDragDropType(
                ChromeDragDropUtils.getDragDropTypeFromIntent(intent),
                AppHeaderUtils.isAppInDesktopWindow(
                        mRootUiCoordinator.getDesktopWindowStateManager()),
                /* isTabGroup= */ true);
        return true;
    }

    @Override
    public void performPreInflationStartup() {
        super.performPreInflationStartup();

        if (isMainIntentLaunchPreOnResume()) {
            StartupLatencyInjector startupLatencyInjector = new StartupLatencyInjector();
            startupLatencyInjector.maybeInjectLatency();
        }

        // Android FrameMetrics allow tracking of java views and their deadline misses (frame
        // drops/janks).
        if (ChromeFeatureList.sCollectAndroidFrameTimelineMetrics.isEnabled()) {
            // We delay initialization because we have noticed a impact on started up, but this
            // metric collection isn't critical. Delaying gets us past start up and lets Chrome's
            // scheduler decide its priority.
            mJankTracker =
                    new JankTrackerImpl(
                            this,
                            ChromeFeatureList
                                    .sCollectAndroidFrameTimelineMetricsJankTrackerDelayedStartMs
                                    .getValue());
        } else {
            mJankTracker = new PlaceholderJankTracker();
        }

        // Decide whether to record startup UMA histograms. This is done early in the main
        // Activity.onCreate() to avoid recording navigation delays when they require user input to
        // proceed. Having an uninitialized native library has been taken as a sign of starting
        // Chrome with an immediate navigation without user input.
        // TODO(crbug.com/40926074): Native library initialization was moved to another thread, and
        //  it now proceeds faster, making the metrics think that the start is not cold enough.
        //  To cover more startup cases change the heuristic detecting cold startup that happens
        //  without user interaction.
        if (!LibraryLoader.getInstance().isInitialized()) {
            setTrackColdStartupMetrics(true);
        }

        // Enable Paint Preview only on a cold start. This way the Paint preview is most useful by
        // being much faster than the real load of the page. Also cold start detection excludes user
        // interactions changing the course of restoring the page.
        if (isColdStart()) {
            StartupPaintPreviewHelper.enableShowOnRestore();
        }

        supportRequestWindowFeature(Window.FEATURE_ACTION_MODE_OVERLAY);
        IncognitoTabHostRegistry.getInstance().register(mIncognitoTabHost);
        mStartupPaintPreviewHelperSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        mDseNewTabUrlManager = new DseNewTabUrlManager(mTabModelProfileSupplier);

        initHub();
    }

    @Override
    protected RootUiCoordinator createRootUiCoordinator() {
        return new TabbedRootUiCoordinator(
                this,
                this::onOmniboxFocusChanged,
                getShareDelegateSupplier(),
                getActivityTabProvider(),
                mTabModelProfileSupplier,
                mBookmarkModelSupplier,
                mTabBookmarkerSupplier,
                getTabModelSelectorSupplier(),
                mTabSwitcherSupplier,
                mIncognitoTabSwitcherSupplier,
                mHubManagerSupplier,
                mIntentMetadataOneshotSupplier,
                mLayoutStateProviderSupplier,
                getBrowserControlsManager(),
                getWindowAndroid(),
                getLifecycleDispatcher(),
                getLayoutManagerSupplier(),
                /* menuOrKeyboardActionController= */ this,
                this::getActivityThemeColor,
                getModalDialogManagerSupplier(),
                /* appMenuBlocker= */ this,
                this::supportsAppMenu,
                this::supportsFindInPage,
                getTabCreatorManagerSupplier(),
                getFullscreenManager(),
                getCompositorViewHolderSupplier(),
                getTabContentManagerSupplier(),
                this::getSnackbarManager,
                mEdgeToEdgeControllerSupplier,
                mBottomChinSupplier,
                getActivityType(),
                this::isInOverviewMode,
                /* appMenuDelegate= */ this,
                /* statusBarColorProvider= */ this,
                new OneshotSupplierImpl<>(),
                getIntentRequestTracker(),
                getInsetObserver(),
                this::backShouldCloseTab,
                this::sendToBackground,
                // TODO(sinansahin): This currently only checks for incognito extras in the intent.
                // We should make it more robust by using more signals.
                IntentHandler.hasAnyIncognitoExtra(getIntent().getExtras()),
                mBackPressManager,
                getSavedInstanceState(),
                mMultiInstanceManager,
                initHubOverviewColorSupplier(),
                mManualFillingComponentSupplier,
                getEdgeToEdgeManager(),
                mBookmarkManagerOpenerSupplier,
                getXrSpaceModeObservableSupplier());
    }

    @Override
    protected OneshotSupplier<SystemBarColorHelper> createSystemBarColorHelperSupplier() {
        if (EdgeToEdgeUtils.isEdgeToEdgeEverywhereEnabled()) {
            mSystemBarColorHelperSupplier.set(
                    new TabbedSystemBarColorHelper(
                            ensureEdgeToEdgeLayoutCoordinator(), mBottomChinSupplier));
            return mSystemBarColorHelperSupplier;
        } else if (EdgeToEdgeUtils.isBottomChinFeatureEnabled()) {
            // If isEdgeToEdgeBottomChinEnabled() && !isEdgeToEdgeEverywhereEnabled()
            return mBottomChinSupplier;
        }
        return super.createSystemBarColorHelperSupplier();
    }

    @Override
    protected int getControlContainerLayoutId() {
        return R.layout.control_container;
    }

    @Override
    protected int getToolbarLayoutId() {
        return isTablet() ? R.layout.toolbar_tablet : R.layout.toolbar_phone;
    }

    @Override
    public void performPostInflationStartup() {
        super.performPostInflationStartup();

        if (isFinishing()) return;

        TabModelSelector tabModelSelector = getTabModelSelector();
        IncognitoProfileDestroyer.observeTabModelSelector(tabModelSelector);
        IncognitoNotificationPresenceController.observeTabModelSelector(tabModelSelector);

        // Don't show the keyboard until user clicks in.
        getWindow()
                .setSoftInputMode(
                        WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        mContentContainer = findViewById(android.R.id.content);
        mControlContainer = findViewById(R.id.control_container);
        if (mControlContainer != null) {
            mControlContainer.setXrSpaceModeObservableSupplierMaybe(
                    getXrSpaceModeObservableSupplier());
        }

        Supplier<Boolean> dialogVisibilitySupplier =
                () -> {
                    // Return true if dialog from either tab switcher or tab strip is visible.
                    ToolbarManager toolbarManager = getToolbarManager();
                    TabGroupUi tabGroupUi = toolbarManager.getTabGroupUi();
                    boolean isDialogVisible =
                            tabGroupUi != null && tabGroupUi.isTabGridDialogVisible();

                    if (!mTabSwitcherSupplier.hasValue()) {
                        // The grid tab switcher may be lazily initialized; early out if it isn't
                        // ready.
                        return isDialogVisible;
                    }

                    Supplier<Boolean> tabSwitcherDialogVisibilitySupplier =
                            mTabSwitcherSupplier.get().getTabGridDialogVisibilitySupplier();

                    if (tabSwitcherDialogVisibilitySupplier != null) {
                        isDialogVisible |= tabSwitcherDialogVisibilitySupplier.get();
                    }
                    var incognitoTabSwitcher = mIncognitoTabSwitcherSupplier.get();
                    if (incognitoTabSwitcher != null) {
                        var incognitoDialogVisibilitySupplier =
                                incognitoTabSwitcher.getTabGridDialogVisibilitySupplier();
                        if (incognitoDialogVisibilitySupplier != null) {
                            isDialogVisible |= incognitoDialogVisibilitySupplier.get();
                        }
                    }
                    return isDialogVisible;
                };

        mUndoBarPopupController =
                new UndoBarController(this, mTabModelSelector, this, dialogVisibilitySupplier);

        // TODO(crbug.com/376668040): Remove this once enough time has passed
        // most clients have upgraded to have colors.
        TabModelUtils.runOnTabStateInitialized(
                getTabModelSelectorSupplier().get(),
                mCallbackController.makeCancelable(
                        (tabModelSelectorReturn) -> {
                            TabGroupColorUtils.assignTabGroupColorsIfApplicable(
                                    tabModelSelectorReturn
                                            .getTabGroupModelFilterProvider()
                                            .getCurrentTabGroupModelFilter());
                        }));

        mInactivityTracker =
                new ChromeInactivityTracker(
                        ChromePreferenceKeys.TABBED_ACTIVITY_LAST_BACKGROUNDED_TIME_MS_PREF);
        TabUsageTracker.initialize(this.getLifecycleDispatcher(), tabModelSelector);
        TabGroupUsageTracker.initialize(
                this.getLifecycleDispatcher(), tabModelSelector, this::isWarmOnResume);

        assert getLegacyTabStartupMetricsTracker() != null;
        assert getStartupMetricsTracker() != null;
        StartupPaintPreviewHelper paintPreviewHelper =
                new StartupPaintPreviewHelper(
                        getWindowAndroid(),
                        getOnCreateTimestampMs(),
                        getBrowserControlsManager(),
                        getTabModelSelector(),
                        () -> {
                            return getToolbarManager() == null
                                    ? null
                                    : getToolbarManager().getProgressBarCoordinator();
                        });
        mStartupPaintPreviewHelperSupplier.set(paintPreviewHelper);
        getLegacyTabStartupMetricsTracker().registerPaintPreviewObserver(paintPreviewHelper);
        getStartupMetricsTracker().registerPaintPreviewObserver(paintPreviewHelper);
        maybeRegisterHomeModules();
    }

    private void maybeRegisterHomeModules() {
        if (!HomeModulesMetricsUtils.useMagicStack()) return;

        ModuleRegistry moduleRegistry =
                new ModuleRegistry(
                        HomeModulesConfigManager.getInstance(), getLifecycleDispatcher());
        SingleTabModuleBuilder singleTabModuleBuilder =
                new SingleTabModuleBuilder(
                        this, getTabModelSelectorSupplier(), getTabContentManagerSupplier());
        moduleRegistry.registerModule(ModuleType.SINGLE_TAB, singleTabModuleBuilder);

        if (ChromeFeatureList.sPriceChangeModule.isEnabled()) {
            PriceChangeModuleBuilder priceChangeModuleBuilder =
                    new PriceChangeModuleBuilder(
                            this, getProfileProviderSupplier(), mTabModelSelector);
            moduleRegistry.registerModule(ModuleType.PRICE_CHANGE, priceChangeModuleBuilder);
        }

        if (ChromeFeatureList.sSafetyHubMagicStack.isEnabled()) {
            SafetyHubMagicStackBuilder safetyHubMagicStackBuilder =
                    new SafetyHubMagicStackBuilder(
                            this,
                            mTabModelProfileSupplier,
                            mTabModelSelector,
                            getModalDialogManagerSupplier());
            moduleRegistry.registerModule(ModuleType.SAFETY_HUB, safetyHubMagicStackBuilder);
        }

        if (ChromeFeatureList.sEducationalTipModule.isEnabled()) {
            Set<Integer> tipModuleTypes = EducationalTipModuleUtils.getModuleTypes();
            for (@ModuleType int tipModule : tipModuleTypes) {
                EducationalTipModuleBuilder educationalTipModuleBuilder =
                        new EducationalTipModuleBuilder(
                                tipModule, createEducationTipModuleActionDelegate());
                moduleRegistry.registerModule(tipModule, educationalTipModuleBuilder);
            }
        }

        if (ChromeFeatureList.sAndroidAppIntegrationWithFavicon.isEnabled()) {
            // The AuxiliarySearchControllerFactory#setIsTablet() must be called before using the
            // builder which checks AuxiliarySearchControllerFactory#isEnabled().
            AuxiliarySearchControllerFactory.getInstance()
                    .setIsTablet(DeviceFormFactor.isWindowOnTablet(getWindowAndroid()));
            AuxiliarySearchModuleBuilder auxiliarySearchModuleBuilder =
                    new AuxiliarySearchModuleBuilder(
                            this,
                            () -> {
                                SettingsNavigationFactory.createSettingsNavigation()
                                        .startSettings(this, TabsSettings.class);
                            });
            moduleRegistry.registerModule(
                    ModuleType.AUXILIARY_SEARCH, auxiliarySearchModuleBuilder);
        }

        mModuleRegistrySupplier.set(moduleRegistry);
    }

    private EducationTipModuleActionDelegate createEducationTipModuleActionDelegate() {
        return new EducationTipModuleActionDelegate() {
            @NonNull
            @Override
            public Context getContext() {
                return ChromeTabbedActivity.this;
            }

            @NonNull
            @Override
            public ObservableSupplier<Profile> getProfileSupplier() {
                return mTabModelProfileSupplier;
            }

            @NonNull
            @Override
            public TabModelSelector getTabModelSelector() {
                return mTabModelSelector;
            }

            @NonNull
            @Override
            public BottomSheetController getBottomSheetController() {
                return mRootUiCoordinator.getBottomSheetController();
            }

            @Override
            public void openHubPane(int paneId) {
                if (mLayoutManager == null) return;

                // Opens the tab switcher and displays a specific pane.
                HubShowPaneHelper hubShowPaneHelper = mHubProvider.getHubShowPaneHelper();
                hubShowPaneHelper.setPaneToShow(paneId);
                mLayoutManager.showLayout(LayoutType.TAB_SWITCHER, false);
            }

            @Override
            public void openTabGroupIphDialog() {
                TabGridIphDialogCoordinator tabGridIphDialogCoordinator =
                        new TabGridIphDialogCoordinator(
                                ChromeTabbedActivity.this, getModalDialogManager());
                tabGridIphDialogCoordinator.setParentView(mCompositorViewHolder);

                mLayoutManager.showLayout(LayoutType.TAB_SWITCHER, false);
                tabGridIphDialogCoordinator.showIph();
            }

            @Override
            public void openAndHighlightQuickDeleteMenuItem() {
                // Opens the app menu and highlights the quick delete menu item.
                mRootUiCoordinator.getAppMenuHandler().setMenuHighlight(R.id.quick_delete_menu_id);
                getMenuOrKeyboardActionController()
                        .onMenuOrKeyboardAction(R.id.show_menu, /* fromMenu= */ false);
            }

            @Override
            public void showHistorySyncOptIn(Runnable dismissHistorySyncModuleCallback) {
                BottomSheetSigninAndHistorySyncConfig bottomSheetConfig =
                        new BottomSheetSigninAndHistorySyncConfig.Builder(
                                        new AccountPickerBottomSheetStrings.Builder(
                                                        R.string
                                                                .signin_account_picker_bottom_sheet_title)
                                                .setSubtitleStringId(
                                                        R.string
                                                                .signin_account_picker_bottom_sheet_benefits_subtitle)
                                                .build(),
                                        BottomSheetSigninAndHistorySyncConfig.NoAccountSigninMode
                                                .NO_SIGNIN,
                                        BottomSheetSigninAndHistorySyncConfig.WithAccountSigninMode
                                                .DEFAULT_ACCOUNT_BOTTOM_SHEET,
                                        HistorySyncConfig.OptInMode.REQUIRED)
                                .build();
                @Nullable
                Intent intent =
                        SigninAndHistorySyncActivityLauncherImpl.get()
                                .createBottomSheetSigninIntentOrShowError(
                                        ChromeTabbedActivity.this,
                                        mTabModelSelector.getCurrentModel().getProfile(),
                                        bottomSheetConfig,
                                        SigninAccessPoint.HISTORY_SYNC_EDUCATIONAL_TIP);

                if (intent != null) {
                    WindowAndroid windowAndroid = ChromeTabbedActivity.this.getWindowAndroid();
                    windowAndroid.showIntent(
                            intent,
                            new WindowAndroid.IntentCallback() {
                                @Override
                                public void onIntentCompleted(int resultCode, Intent data) {
                                    if (getContext() != null) {
                                        dismissHistorySyncModuleCallback.run();
                                    }
                                }
                            },
                            null);
                }
            }

            @Override
            public int getTabCountForRelaunchFromSharedPrefs() {
                return MultiWindowUtils.getTabCountForRelaunchFromSharedPrefs(mWindowId);
            }
        };
    }

    private boolean shouldIgnoreIntent() {
        if (mShouldIgnoreIntent == null) {
            // We call this only once to give a consistent view of whether the intent should be
            // ignored during startup as this function depends on transient state like whether the
            // screen is on.
            mShouldIgnoreIntent = IntentHandler.shouldIgnoreIntent(getIntent(), this);
        }
        return mShouldIgnoreIntent;
    }

    @Override
    protected TabModelOrchestrator createTabModelOrchestrator() {
        boolean tabMergingEnabled =
                mMultiInstanceManager != null && mMultiInstanceManager.isTabModelMergingEnabled();
        mTabModelOrchestrator =
                new TabbedModeTabModelOrchestrator(
                        tabMergingEnabled,
                        getLifecycleDispatcher(),
                        CipherLazyHolder.sCipherInstance);
        mTabModelStartupInfoSupplier = new ObservableSupplierImpl<>();
        mTabModelOrchestrator.setStartupInfoObservableSupplier(mTabModelStartupInfoSupplier);
        return mTabModelOrchestrator;
    }

    @Override
    protected void createTabModels() {
        assert mTabModelSelector == null;
        assert mWindowId != INVALID_WINDOW_ID;

        Bundle savedInstanceState = getSavedInstanceState();

        // We determine the model as soon as possible so every systems get initialized coherently.
        boolean startIncognito =
                savedInstanceState != null
                        && savedInstanceState.getBoolean(IS_INCOGNITO_SELECTED, false);

        mNextTabPolicySupplier = new ChromeNextTabPolicySupplier(mLayoutStateProviderSupplier);

        boolean tabModelWasCreated =
                mTabModelOrchestrator.createTabModels(
                        this,
                        getModalDialogManager(),
                        getProfileProviderSupplier(),
                        this,
                        mNextTabPolicySupplier,
                        mMismatchedIndicesHandler,
                        mWindowId);
        if (!tabModelWasCreated) {
            finishAndRemoveTask();
            return;
        }

        if (mMultiInstanceManager != null) {
            int assignedIndex = TabWindowManagerSingleton.getInstance().getIdForWindow(this);
            // The given index and the one computed by TabWindowManager should be one and the same.
            int taskId = ApplicationStatus.getTaskId(this);
            Map<String, Integer> taskMap =
                    ChromeSharedPreferences.getInstance()
                            .readIntsWithPrefix(ChromePreferenceKeys.MULTI_INSTANCE_TASK_MAP);
            String message =
                    String.format(
                            Locale.getDefault(),
                            "Instance mismatch for assignedIndex: %d, mWindowId: %d with taskId:"
                                    + " %s and taskMap: %s",
                            assignedIndex,
                            mWindowId,
                            taskId,
                            taskMap);

            if (MultiWindowUtils.isMultiInstanceApi31Enabled()) {
                boolean indicesMatch = assignedIndex == mWindowId;
                assert indicesMatch : message;
                if (!indicesMatch) {
                    Log.i(TAG_MULTI_INSTANCE, message);
                }
            }

            mMultiInstanceManager.initialize(assignedIndex, taskId);
        }

        mTabModelSelector = mTabModelOrchestrator.getTabModelSelector();
        mTabModelSelectorObserver =
                new TabModelSelectorObserver() {
                    @Override
                    public void onTabStateInitialized() {
                        if (mMultiInstanceManager != null) {
                            mMultiInstanceManager.onTabStateInitialized();
                        }
                    }
                };
        mTabModelSelector.addObserver(mTabModelSelectorObserver);

        mTabModelSelectorTabObserver =
                new TabModelSelectorTabObserver(mTabModelSelector) {
                    @Override
                    public void onDidFinishNavigationInPrimaryMainFrame(
                            Tab tab, NavigationHandle navigation) {
                        if (!navigation.hasCommitted()) return;

                        // Show the sync error message even if the navigation happened on incognito.
                        Profile profile = mTabModelProfileSupplier.get().getOriginalProfile();

                        try (TraceEvent e =
                                TraceEvent.scoped("CheckSyncErrorOnDidFinishNavigation")) {
                            SyncErrorMessage.maybeShowMessageUi(getWindowAndroid(), profile);
                        }
                        try (TraceEvent te = TraceEvent.scoped("updateActiveWebContents")) {
                            SendTabToSelfAndroidBridge.updateActiveWebContents(
                                    tab.getWebContents());
                        }
                    }
                };
        if (startIncognito) mTabModelSelector.selectModel(true);
    }

    TabModelSelectorObserver getTabModelSelectorObserverForTesting() {
        return mTabModelSelectorObserver;
    }

    boolean getCreatedTabOnStartupForTesting() {
        return mCreatedTabOnStartup;
    }

    void setCreatedTabOnStartupForTesting(boolean createdTabOnStartup) {
        mCreatedTabOnStartup = createdTabOnStartup;
    }

    @Override
    protected LaunchCauseMetrics createLaunchCauseMetrics() {
        return new TabbedActivityLaunchCauseMetrics(this);
    }

    @Override
    public AppMenuPropertiesDelegate createAppMenuPropertiesDelegate() {
        return new TabbedAppMenuPropertiesDelegate(
                this,
                getActivityTabProvider(),
                getMultiWindowModeStateDispatcher(),
                getTabModelSelector(),
                getToolbarManager(),
                getWindow().getDecorView(),
                this,
                mLayoutStateProviderSupplier,
                mBookmarkModelSupplier,
                () ->
                        getTabCreator(/* incognito= */ false)
                                .launchUrl(
                                        NewTabPageUtils.encodeNtpUrl(
                                                NewTabPageLaunchOrigin.WEB_FEED),
                                        TabLaunchType.FROM_CHROME_UI),
                getModalDialogManager(),
                getSnackbarManager(),
                mRootUiCoordinator.getExtensionService(),
                mRootUiCoordinator.getIncognitoReauthControllerSupplier(),
                mRootUiCoordinator.getReadAloudControllerSupplier());
    }

    private TabDelegateFactory getTabDelegateFactory() {
        if (mTabDelegateFactory == null) {
            assert getStartupMetricsTracker() != null;
            mTabDelegateFactory =
                    new TabbedModeTabDelegateFactory(
                            this,
                            getAppBrowserControlsVisibilityDelegate(),
                            getShareDelegateSupplier(),
                            mRootUiCoordinator.getEphemeralTabCoordinatorSupplier(),
                            ((TabbedRootUiCoordinator) mRootUiCoordinator)::onContextMenuCopyLink,
                            mRootUiCoordinator.getBottomSheetController(),
                            /* chromeActivityNativeDelegate= */ this,
                            getBrowserControlsManager(),
                            getFullscreenManager(),
                            /* tabCreatorManager= */ this,
                            getTabModelSelectorSupplier(),
                            getCompositorViewHolderSupplier(),
                            getModalDialogManagerSupplier(),
                            this::getSnackbarManager,
                            getBrowserControlsManager(),
                            getActivityTabProvider(),
                            getLifecycleDispatcher(),
                            getWindowAndroid(),
                            mJankTracker,
                            getToolbarManager()::getToolbar,
                            mHomeSurfaceTracker,
                            getTabContentManagerSupplier(),
                            getToolbarManager().getTabStripHeightSupplier(),
                            mModuleRegistrySupplier,
                            mEdgeToEdgeControllerSupplier,
                            getStartupMetricsTracker());
        }
        return mTabDelegateFactory;
    }

    @Override
    protected Pair<ChromeTabCreator, ChromeTabCreator> createTabCreators() {
        return Pair.create(
                new ChromeTabCreator(
                        this,
                        getWindowAndroid(),
                        this::getTabDelegateFactory,
                        getProfileProviderSupplier(),
                        false,
                        AsyncTabParamsManagerSingleton.getInstance(),
                        getTabModelSelectorSupplier(),
                        getCompositorViewHolderSupplier()),
                new ChromeTabCreator(
                        this,
                        getWindowAndroid(),
                        this::getTabDelegateFactory,
                        getProfileProviderSupplier(),
                        true,
                        AsyncTabParamsManagerSingleton.getInstance(),
                        getTabModelSelectorSupplier(),
                        getCompositorViewHolderSupplier()));
    }

    @Override
    protected void initDeferredStartupForActivity() {
        super.initDeferredStartupForActivity();
        DeferredStartupHandler.getInstance().addDeferredTask(this::onDeferredStartup);
    }

    private void onDeferredStartup() {
        if (isActivityFinishingOrDestroyed()) {
            return;
        }

        LauncherShortcutActivity.updateIncognitoShortcut(
                ChromeTabbedActivity.this, mTabModelProfileSupplier.get());

        ChromeSurveyController.initialize(
                mTabModelSelector,
                getLifecycleDispatcher(),
                ChromeTabbedActivity.this,
                MessageDispatcherProvider.from(getWindowAndroid()),
                mTabModelProfileSupplier.get());

        PrivacySandboxSurveyController.initialize(
                mTabModelSelector,
                getLifecycleDispatcher(),
                ChromeTabbedActivity.this,
                MessageDispatcherProvider.from(getWindowAndroid()),
                getActivityTabProvider(),
                mTabModelProfileSupplier.get());

        if (ChromeFeatureList.isEnabled(ChromeFeatureList.HEADLESS_TAB_MODEL)) {
            Profile profile = getProfileProviderSupplier().get().getOriginalProfile();
            TabWindowManagerSingleton.getInstance()
                    .keepAllTabModelsLoaded(mMultiInstanceManager, profile, getTabModelSelector());
        } else {
            mMultiInstanceManager.cleanupSyncedTabGroupsIfOnlyInstance(mTabModelSelector);
        }

        if (mAuxiliarySearchController != null) {
            mAuxiliarySearchController.onDeferredStartup();
        }
    }

    @Override
    protected void recordIntentToCreationTime(long timeMs) {
        super.recordIntentToCreationTime(timeMs);
        RecordHistogram.recordCustomTimesHistogram(
                "MobileStartup.IntentToCreationTime.TabbedMode",
                timeMs,
                1,
                DateUtils.SECOND_IN_MILLIS * 30,
                50);
    }

    @Override
    protected boolean isStartedUpCorrectly(Intent intent) {
        mWindowId = 0;
        mInstanceAllocationType = InstanceAllocationType.DEFAULT;
        Bundle savedInstanceState = getSavedInstanceState();
        int windowId = getExtraWindowIdFromIntent(intent);
        if (savedInstanceState != null && savedInstanceState.containsKey(WINDOW_INDEX)) {
            // Activity is recreated after destruction. |windowId| must not be valid in this case.
            assert windowId == INVALID_WINDOW_ID;
            Log.i(TAG_MULTI_INSTANCE, "Retrieved windowId from saved instance state.");
            mWindowId = savedInstanceState.getInt(WINDOW_INDEX, 0);
        } else if (mMultiInstanceManager != null) {
            // |allocInstanceId| doesn't do any disk I/O that would add a long-running task
            // to pre-inflation startup.
            boolean preferNew = getExtraPreferNewFromIntent(intent);
            Pair<Integer, Integer> instanceIdInfo =
                    mMultiInstanceManager.allocInstanceId(
                            windowId, ApplicationStatus.getTaskId(this), preferNew);
            mWindowId = instanceIdInfo.first;
            mInstanceAllocationType = instanceIdInfo.second;
            logIntentInfo(intent);
            // If a new instance ID was allocated for the newly created activity, potentially
            // dispatch it to an existing activity under special circumstances. See
            // |#maybeDispatchIntentInExistingActivity(Intent)| for details.
            if (instanceIdInfo.second == InstanceAllocationType.NEW_INSTANCE_NEW_TASK
                    && maybeDispatchIntentInExistingActivity(intent)) {
                return false;
            }
        }
        if (mWindowId == INVALID_WINDOW_ID) {
            Log.i(TAG, "Window ID not allocated. Finishing the activity");
            Toast.makeText(this, R.string.max_number_of_windows, Toast.LENGTH_LONG).show();
            recordMaxWindowLimitExceededHistogram(/* limitExceeded= */ true);
            return false;
        } else {
            Map<String, Integer> taskMap =
                    ChromeSharedPreferences.getInstance()
                            .readIntsWithPrefix(ChromePreferenceKeys.MULTI_INSTANCE_TASK_MAP);
            Log.i(
                    TAG_MULTI_INSTANCE,
                    "Window ID allocated: " + mWindowId + ", instance-task map: " + taskMap);
        }

        if (mMultiInstanceManager != null
                && !mMultiInstanceManager.isStartedUpCorrectly(ApplicationStatus.getTaskId(this))) {
            return false;
        }
        recordMaxWindowLimitExceededHistogram(/* limitExceeded= */ false);

        return super.isStartedUpCorrectly(intent);
    }

    private void logIntentInfo(Intent intent) {
        var logMessage =
                "Intent routed via ChromeLauncherActivity: "
                        + IntentUtils.safeGetBooleanExtra(
                                intent,
                                IntentHandler.EXTRA_LAUNCHED_VIA_CHROME_LAUNCHER_ACTIVITY,
                                false)
                        + "\nActivity referrer: "
                        + getReferrer()
                        + "\nIntent referrer extra: "
                        + IntentUtils.safeGetStringExtra(
                                intent, IntentHandler.EXTRA_ACTIVITY_REFERRER)
                        + "\nIntent contains LAUNCHER category: "
                        + intent.hasCategory(Intent.CATEGORY_LAUNCHER)
                        + "\nIntent contains FLAG_ACTIVITY_MULTIPLE_TASK: "
                        + ((intent.getFlags() & Intent.FLAG_ACTIVITY_MULTIPLE_TASK) != 0)
                        + "\nIntent contains FLAG_ACTIVITY_NEW_TASK: "
                        + ((intent.getFlags() & Intent.FLAG_ACTIVITY_NEW_TASK) != 0)
                        + "\nIntent component: "
                        + (intent.getComponent() == null
                                ? "N/A"
                                : intent.getComponent().getClassName())
                        + "\nIntent hash: "
                        + System.identityHashCode(intent);
        Log.i(TAG_MULTI_INSTANCE, logMessage);
    }

    // It is possible that an undesired attempt is made to launch a VIEW intent in a new
    // ChromeTabbedActivity by an external app. When a new activity is created using such an intent,
    // we will try to launch it in an existing ChromeTabbedActivity instead, and finish the newly
    // created ChromeTabbedActivity.
    private boolean maybeDispatchIntentInExistingActivity(Intent intent) {
        if (!MultiWindowUtils.isMultiInstanceApi31Enabled()) {
            return false;
        }

        // Check if the intent creating a new ChromeTabbedActivity was a VIEW intent launched via
        // ChromeLauncherActivity, in which case continue to launch in the current activity.
        boolean isExplicitViewIntent =
                Intent.ACTION_VIEW.equals(intent.getAction())
                        && !IntentUtils.safeGetBooleanExtra(
                                intent,
                                IntentHandler.EXTRA_LAUNCHED_VIA_CHROME_LAUNCHER_ACTIVITY,
                                false);
        if (!isExplicitViewIntent) return false;

        // If the intent sender is Chrome, continue to launch in the current activity.
        if (IntentHandler.wasIntentSenderChrome(intent)) {
            return false;
        }

        // Find an instance to launch the intent in.
        int instanceId = MultiWindowUtils.getInstanceIdForViewIntent(false);

        // If there is no running ChromeTabbedActivity, continue to launch in the current activity.
        if (instanceId == INVALID_WINDOW_ID) {
            return false;
        }

        intent.putExtra(IntentHandler.EXTRA_WINDOW_ID, instanceId);
        MultiWindowUtils.launchIntentInInstance(intent, instanceId);
        return true;
    }

    private void recordMaxWindowLimitExceededHistogram(boolean limitExceeded) {
        RecordHistogram.recordBooleanHistogram(
                "Android.MultiInstance.MaxWindowLimitExceeded", limitExceeded);
    }

    private static int getExtraWindowIdFromIntent(Intent intent) {
        int windowId =
                IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_WINDOW_ID, INVALID_WINDOW_ID);
        return IntentUtils.isTrustedIntentFromSelf(intent) ? windowId : INVALID_WINDOW_ID;
    }

    private static boolean getExtraPreferNewFromIntent(Intent intent) {
        return IntentUtils.safeGetBooleanExtra(intent, IntentHandler.EXTRA_PREFER_NEW, false);
    }

    @Override
    public void terminateIncognitoSession() {
        getTabModelSelector()
                .getModel(true)
                .getTabRemover()
                .closeTabs(TabClosureParams.closeAllTabs().build(), /* allowDialog= */ false);
    }

    @Override
    public boolean onMenuOrKeyboardAction(
            final int id, boolean fromMenu, @Nullable MotionEventInfo triggeringMotion) {
        final Tab currentTab = getActivityTab();
        boolean currentTabIsNtp = isTabNtp(currentTab);
        if (id == R.id.new_tab_menu_id) {
            if (!mTabModelSelector.isTabStateInitialized()) return false;

            getTabModelSelector().getModel(false).commitAllTabClosures();
            RecordUserAction.record("MobileMenuNewTab");
            RecordUserAction.record("MobileNewTabOpened");
            reportNewTabShortcutUsed(false);
            if (fromMenu) RecordUserAction.record("MobileMenuNewTab.AppMenu");

            getTabCreator(false).launchNtp();

            mLocaleManager.showSearchEnginePromoIfNeeded(this, null);
        } else if (id == R.id.new_incognito_tab_menu_id) {
            if (!mTabModelSelector.isTabStateInitialized()) return false;

            Profile profile = mTabModelSelector.getCurrentModel().getProfile();
            if (IncognitoUtils.isIncognitoModeEnabled(profile)) {
                getTabModelSelector().getModel(false).commitAllTabClosures();
                // This action must be recorded before opening the incognito tab since UMA actions
                // are dropped when an incognito tab is open.
                RecordUserAction.record("MobileMenuNewIncognitoTab");
                RecordUserAction.record("MobileNewTabOpened");
                reportNewTabShortcutUsed(true);
                if (fromMenu) RecordUserAction.record("MobileMenuNewIncognitoTab.AppMenu");
                getTabCreator(true).launchNtp();
                Tracker tracker = TrackerFactory.getTrackerForProfile(profile);
                tracker.notifyEvent(EventConstants.APP_MENU_NEW_INCOGNITO_TAB_CLICKED);
            }
        } else if (id == R.id.add_to_group_menu_id
                || id == R.id.add_tab_to_group_menu_id
                || id == R.id.add_tab_to_new_group_menu_id) {
            if (!mTabModelSelector.isTabStateInitialized()) return false;

            Profile profile = mTabModelProfileSupplier.get();
            TabGroupModelFilter filter =
                    mTabModelSelector
                            .getTabGroupModelFilterProvider()
                            .getCurrentTabGroupModelFilter();
            if (id == R.id.add_to_group_menu_id) {
                TrackerFactory.getTrackerForProfile(profile)
                        .notifyEvent("menu_add_to_group_clicked");
                if (filter.getTabGroupCount() == 0) {
                    RecordUserAction.record("MobileMenuAddToNewGroup");
                } else {
                    RecordUserAction.record("MobileMenuAddToGroup");
                }
            }

            new TabGroupMenuActionHandler(
                            this,
                            filter,
                            mRootUiCoordinator.getBottomSheetController(),
                            getModalDialogManager(),
                            profile)
                    .handleAddToGroupAction(currentTab);
        } else if (id == R.id.all_bookmarks_menu_id) {
            getCompositorViewHolderSupplier()
                    .get()
                    .hideKeyboard(
                            () -> {
                                mBookmarkManagerOpenerSupplier
                                        .get()
                                        .showBookmarkManager(
                                                ChromeTabbedActivity.this,
                                                currentTab,
                                                getCurrentTabModel().getProfile());
                            });
            if (currentTabIsNtp) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_BOOKMARKS_MANAGER);
            }

            @BrowserProfileType
            int type =
                    getCurrentTabModel().isIncognito()
                            ? BrowserProfileType.INCOGNITO
                            : BrowserProfileType.REGULAR;
            RecordHistogram.recordEnumeratedHistogram(
                    "Bookmarks.OpenBookmarkManager.PerProfileType",
                    type,
                    BrowserProfileType.MAX_VALUE);

            RecordUserAction.record("MobileMenuAllBookmarks");
        } else if (id == R.id.recent_tabs_menu_id) {
            LoadUrlParams params =
                    new LoadUrlParams(UrlConstants.RECENT_TABS_URL, PageTransition.AUTO_BOOKMARK);
            boolean isInOverviewMode = isInOverviewMode();
            if (currentTab == null || (ChromeFeatureList.sAndroidNativePagesInNewTab.isEnabled()
                    && ChromeFeatureList.sAndroidNativePagesInNewTabRecentTabsEnabled.getValue())) {
                getTabCreator(getCurrentTabModel().isIncognito())
                        .createNewTab(params, TabLaunchType.FROM_CHROME_UI, currentTab);
            } else {
                currentTab.loadUrl(params);
            }
            if (isInOverviewMode) {
                mLayoutManager.showLayout(LayoutType.BROWSING, true);
            }

            if (currentTabIsNtp) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_RECENT_TABS_MANAGER);
            }
            RecordUserAction.record("MobileMenuRecentTabs");
        } else if (id == R.id.extensions_menu_id) {
            LoadUrlParams params =
                    new LoadUrlParams(
                            UrlConstants.CHROME_EXTENSIONS_URL, PageTransition.AUTO_TOPLEVEL);
            if (currentTab == null) {
                getTabCreator(getCurrentTabModel().isIncognito())
                        .createNewTab(params, TabLaunchType.FROM_CHROME_UI, null);
            } else {
                currentTab.loadUrl(params);
            }
            RecordUserAction.record("MobileMenuExtensions");
        } else if (id == R.id.close_tab) {
            // TODO(crbug.com/375468032): use triggeringMotion to decide
            // TabClosureParams.allowUndo.
            getCurrentTabModel()
                    .getTabRemover()
                    .closeTabs(
                            TabClosureParams.closeTab(currentTab).build(), /* allowDialog= */ true);
            RecordUserAction.record("MobileTabClosed");
        } else if (id == R.id.close_all_tabs_menu_id) {
            boolean allowUndo = TabClosureParamsUtils.shouldAllowUndo(triggeringMotion);

            // Close both incognito and normal tabs.
            Runnable closeAllTabsRunnable =
                    CloseAllTabsHelper.buildCloseAllTabsRunnable(
                            getTabModelSelectorSupplier().get(),
                            /* isIncognitoOnly= */ false,
                            allowUndo);
            CloseAllTabsDialog.show(
                    this,
                    getModalDialogManagerSupplier(),
                    getTabModelSelectorSupplier().get(),
                    closeAllTabsRunnable);
            RecordUserAction.record("MobileMenuCloseAllTabs");
        } else if (id == R.id.close_all_incognito_tabs_menu_id) {
            boolean allowUndo = TabClosureParamsUtils.shouldAllowUndo(triggeringMotion);

            // Close only incognito tabs
            Runnable closeAllTabsRunnable =
                    CloseAllTabsHelper.buildCloseAllTabsRunnable(
                            getTabModelSelectorSupplier().get(),
                            /* isIncognitoOnly= */ true,
                            allowUndo);
            CloseAllTabsDialog.show(
                    this,
                    getModalDialogManagerSupplier(),
                    getTabModelSelectorSupplier().get(),
                    closeAllTabsRunnable);
            RecordUserAction.record("MobileMenuCloseAllIncognitoTabs");
        } else if (id == R.id.focus_url_bar) {
            boolean isUrlBarVisible =
                    !isInOverviewMode() && (!isTablet() || getCurrentTabModel().getCount() != 0);
            if (isUrlBarVisible) {
                getToolbarManager()
                        .setUrlBarFocus(true, OmniboxFocusReason.MENU_OR_KEYBOARD_ACTION);
            }
        } else if (id == R.id.focus_and_clear_url_bar) {
            boolean isUrlBarVisible =
                    !isInOverviewMode() && (!isTablet() || getCurrentTabModel().getCount() != 0);
            if (isUrlBarVisible) {
                getToolbarManager()
                        .setUrlBarFocusAndText(
                                true, OmniboxFocusReason.MENU_OR_KEYBOARD_ACTION, "");
            }
        } else if (id == R.id.downloads_menu_id) {
            OtrProfileId otrProfileId = null;
            if (currentTab != null) {
                otrProfileId = currentTab.getProfile().getOtrProfileId();
            }
            DownloadUtils.showDownloadManager(
                    this, currentTab, otrProfileId, DownloadOpenSource.MENU);
            if (currentTabIsNtp) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_DOWNLOADS_MANAGER);
            }
            RecordUserAction.record("MobileMenuDownloadManager");
        } else if (id == R.id.open_recently_closed_tab) {
            TabModel currentModel = mTabModelSelector.getCurrentModel();
            if (!currentModel.isIncognito()) currentModel.openMostRecentlyClosedEntry();
            RecordUserAction.record("MobileTabClosedUndoShortCut");
        } else if (id == R.id.quick_delete_menu_id) {
            assert !mTabModelSelector.getCurrentModel().isIncognito()
                    : "Quick delete is not supported in Incognito.";

            if (getLayoutManager().getActiveLayoutType() == LayoutType.TAB_SWITCHER) {
                QuickDeleteMetricsDelegate.recordHistogram(
                        QuickDeleteMetricsDelegate.QuickDeleteAction
                                .TAB_SWITCHER_MENU_ITEM_CLICKED);
            } else {
                QuickDeleteMetricsDelegate.recordHistogram(
                        QuickDeleteMetricsDelegate.QuickDeleteAction.MENU_ITEM_CLICKED);
            }

            new QuickDeleteController(
                    this,
                    new QuickDeleteDelegateImpl(mTabModelProfileSupplier, mTabSwitcherSupplier),
                    getModalDialogManager(),
                    getSnackbarManager(),
                    getLayoutManager(),
                    mTabModelSelector,
                    ArchivedTabModelOrchestrator.getForProfile(mTabModelProfileSupplier.get())
                            .getTabModelSelector());
        } else if (id == R.id.switch_to_incognito_menu_id) {
            mTabModelSelector.selectModel(true);
            RecordUserAction.record("MobileMenuSwitchToIncognito");
        } else if (id == R.id.switch_out_of_incognito_menu_id) {
            mTabModelSelector.selectModel(false);
            RecordUserAction.record("MobileMenuSwitchOutOfIncognito");
        } else if (id == R.id.ntp_customization_id) {
            Supplier<Profile> profileSupplier =
                    () ->
                            getProfileProviderSupplier().hasValue()
                                    ? getProfileProviderSupplier().get().getOriginalProfile()
                                    : null;
            new NtpCustomizationCoordinator(
                            this,
                            mRootUiCoordinator.getBottomSheetController(),
                            profileSupplier,
                            NtpCustomizationCoordinator.BottomSheetType.MAIN)
                    .showBottomSheet();
            NtpCustomizationMetricsUtils.recordOpenBottomSheetEntry(
                    NtpCustomizationCoordinator.EntryPointType.MAIN_MENU);
            RecordUserAction.record("MobileMenuNtpCustomization");
        } else if (id == R.id.menu_item_content_filter_help_center_id) {
            currentTab.loadUrl(
                    new LoadUrlParams(
                            SupervisedUserConstants.DEVICE_FILTERS_HELP_CENTER_URL,
                            PageTransition.AUTO_TOPLEVEL));
        } else {
            return super.onMenuOrKeyboardAction(id, fromMenu, triggeringMotion);
        }
        return true;
    }

    private boolean isTabNtp(Tab tab) {
        return tab != null && UrlUtilities.isNtpUrl(tab.getUrl());
    }

    private boolean isTabRegularNtp(Tab tab) {
        return isTabNtp(tab) && !tab.isIncognito();
    }

    private void onOmniboxFocusChanged(boolean hasFocus) {
        getTabModalLifetimeHandler().onOmniboxFocusChanged(hasFocus);
    }

    private void recordLauncherShortcutAction(boolean isIncognito) {
        if (isIncognito) {
            RecordUserAction.record("Android.LauncherShortcut.NewIncognitoTab");
        } else {
            RecordUserAction.record("Android.LauncherShortcut.NewTab");
        }
    }

    private boolean minimizeAppAndCloseTabOnBackPress(@Nullable Tab currentTab) {
        if (currentTab == null) {
            BackPressManager.record(BackPressHandler.Type.MINIMIZE_APP_AND_CLOSE_TAB);
            MinimizeAppAndCloseTabBackPressHandler.record(MinimizeAppAndCloseTabType.MINIMIZE_APP);
            moveTaskToBack(true);
            return true;
        }
        // TAB history handler has a higher priority and should navigate page back before
        // minimizing app and closing tab.
        assert !currentTab.canGoBack()
                : "Tab should be navigated back before closing or exiting app";
        final boolean shouldCloseTab = backShouldCloseTab(currentTab);
        final WebContents webContents = currentTab.getWebContents();

        // Minimize the app if either:
        // - we decided not to close the tab
        // - we decided to close the tab, but it was opened by an external app, so we will go
        //   exit Chrome on top of closing the tab
        final boolean minimizeApp =
                !shouldCloseTab || TabAssociatedApp.isOpenedFromExternalApp(currentTab);

        BackPressManager.record(BackPressHandler.Type.MINIMIZE_APP_AND_CLOSE_TAB);

        if (minimizeApp) {
            if (shouldCloseTab) {
                MinimizeAppAndCloseTabBackPressHandler.record(
                        MinimizeAppAndCloseTabType.MINIMIZE_APP_AND_CLOSE_TAB);
                sendToBackground(currentTab);
            } else {
                MinimizeAppAndCloseTabBackPressHandler.record(
                        MinimizeAppAndCloseTabType.MINIMIZE_APP);
                sendToBackground(null);
            }
            return true;
        } else if (shouldCloseTab) {
            MinimizeAppAndCloseTabBackPressHandler.record(MinimizeAppAndCloseTabType.CLOSE_TAB);
            if (webContents != null) webContents.dispatchBeforeUnload(false);
            return true;
        }
        return false;
    }

    private void initializeBackPressHandlers() {
        // Initialize some back press handlers early to reduce code duplication.
        mReadingListBackPressHandler =
                new ReadingListBackPressHandler(
                        /* activity= */ this,
                        getActivityTabProvider(),
                        mBookmarkModelSupplier,
                        mBookmarkManagerOpenerSupplier);

        mBackPressManager.setHasSystemBackArm(true);
        if (!isTablet()) {
            mBackPressManager.addHandler(
                    mReadingListBackPressHandler, BackPressHandler.Type.SHOW_READING_LIST);
        }
        if (mMinimizeAppAndCloseTabBackPressHandler == null) {
            mMinimizeAppAndCloseTabBackPressHandler =
                    new MinimizeAppAndCloseTabBackPressHandler(
                            getActivityTabProvider(),
                            this::backShouldCloseTab,
                            TabAssociatedApp::isOpenedFromExternalApp,
                            this::closeTabUponMinimization,
                            this::sendToBackground);
            mBackPressManager.addHandler(
                    mMinimizeAppAndCloseTabBackPressHandler,
                    BackPressHandler.Type.MINIMIZE_APP_AND_CLOSE_TAB);
            if (MinimizeAppAndCloseTabBackPressHandler.supportCloseTabUponMinimization()) {
                mBackPressManager.addOnSystemNavigationObserver(
                        mMinimizeAppAndCloseTabBackPressHandler);
            }
        }
    }

    /**
     * [true]: Reached the bottom of the back stack on a tab the user did not explicitly create
     * (i.e. it was created by an external app or opening a link in background, etc). [false]:
     * Reached the bottom of the back stack on a tab that the user explicitly created (e.g.
     * selecting "new tab" from menu). Also applies if the close action would result in the
     * destruction of a collaboration.
     *
     * @return Whether pressing the back button on the provided Tab should close the Tab.
     */
    @Override
    public boolean backShouldCloseTab(Tab tab) {
        if (!tab.isInitialized() || tab.isClosing() || tab.isDestroyed()) {
            return false;
        }
        TabModel tabModel = mTabModelSelector.getModel(tab.isIncognitoBranded());
        if (!DataSharingTabGroupUtils.getSyncedGroupsDestroyedByTabRemoval(
                        tabModel, Arrays.asList(tab))
                .collaborationGroupsDestroyed
                .isEmpty()) {
            return false;
        }

        @TabLaunchType int type = tab.getLaunchType();

        return type == TabLaunchType.FROM_LINK
                || type == TabLaunchType.FROM_EXTERNAL_APP
                || type == TabLaunchType.FROM_READING_LIST
                || type == TabLaunchType.FROM_LONGPRESS_FOREGROUND
                || type == TabLaunchType.FROM_LONGPRESS_FOREGROUND_IN_GROUP
                || type == TabLaunchType.FROM_LONGPRESS_INCOGNITO
                || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND
                || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP
                || type == TabLaunchType.FROM_RECENT_TABS
                || type == TabLaunchType.FROM_RECENT_TABS_FOREGROUND
                || (type == TabLaunchType.FROM_RESTORE && tab.getParentId() != Tab.INVALID_TAB_ID);
    }

    /**
     * Sends this Activity to the background.
     *
     * @param tabToClose Tab that will be closed once the app is not visible.
     */
    private void sendToBackground(@Nullable final Tab tabToClose) {
        Log.i(TAG, "sendToBackground(): " + tabToClose);
        moveTaskToBack(true);
        if (tabToClose != null) closeTabUponMinimization(tabToClose);
    }

    private void closeTabUponMinimization(@NonNull Tab tabToClose) {
        // In the case of closing a tab upon minimization, don't allow the close action to
        // happen until after our app is minimized to make sure we don't get a brief glimpse of
        // the newly active tab before we exit Chrome.
        //
        // If the runnable doesn't run before the Activity dies, Chrome won't crash but the tab
        // won't be closed (crbug.com/587565).
        mHandler.postDelayed(
                () -> {
                    if (mTabModelSelector == null
                            || tabToClose.isClosing()
                            || tabToClose.isDestroyed()) {
                        return;
                    }

                    final TabModel currentModel = mTabModelSelector.getCurrentModel();
                    final TabModel tabToCloseModel =
                            mTabModelSelector.getModel(tabToClose.isIncognito());
                    if (currentModel != tabToCloseModel) {
                        // This seems improbable; however, crbug/1463397 suggests otherwise. If
                        // this happens, remain on the current tab and close the tab in the
                        // other model.
                        tabToCloseModel
                                .getTabRemover()
                                .closeTabs(
                                        TabClosureParams.closeTab(tabToClose)
                                                .uponExit(true)
                                                .allowUndo(false)
                                                .build(),
                                        /* allowDialog= */ false);
                        return;
                    }

                    Tab nextTab =
                            currentModel.getNextTabIfClosed(
                                    tabToClose.getId(), /* uponExit= */ true);
                    tabToCloseModel
                            .getTabRemover()
                            .closeTabs(
                                    TabClosureParams.closeTab(tabToClose)
                                            .recommendedNextTab(nextTab)
                                            .uponExit(true)
                                            .allowUndo(false)
                                            .build(),
                                    /* allowDialog= */ false);
                },
                CLOSE_TAB_ON_MINIMIZE_DELAY_MS);
    }

    @Override
    public boolean moveTaskToBack(boolean nonRoot) {
        try {
            return super.moveTaskToBack(nonRoot);
        } catch (NullPointerException e) {
            // Work around framework bug described in https://crbug.com/817567.
            finish();
            return true;
        }
    }

    /**
     * Launch a URL from an intent.
     *
     * @param loadUrlParams Parameters specifying the url to load.
     * @param externalAppId External app id.
     * @param forceNewTab Whether to force the URL to be launched in a new tab or to fall back to
     *     the default behavior for making that determination.
     * @param intent The original intent.
     */
    private Tab launchIntent(
            LoadUrlParams loadUrlParams, String externalAppId, boolean forceNewTab, Intent intent) {
        if (mUiWithNativeInitialized
                && (IntentUtils.safeHasExtra(intent, IntentHandler.EXTRA_URL_DRAG_SOURCE)
                        || !UrlUtilities.isNtpUrl(loadUrlParams.getUrl()))) {
            getLayoutManager().showLayout(LayoutType.BROWSING, false);
            getToolbarManager().finishAnimations();
        }
        if (IntentHandler.wasIntentSenderChrome(intent)) {
            // If the intent was launched by chrome, open the new tab in the appropriate model.
            boolean isIncognito =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_OPEN_NEW_INCOGNITO_TAB, false);
            @TabLaunchType Integer launchType = IntentHandler.getTabLaunchType(intent);
            if (launchType == null) {
                if (IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_SHORTCUT, false)) {
                    launchType = TabLaunchType.FROM_LAUNCHER_SHORTCUT;
                } else if (IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_APP_WIDGET, false)) {
                    launchType = TabLaunchType.FROM_APP_WIDGET;
                } else if (IncognitoTabLauncher.didCreateIntent(intent)) {
                    launchType = TabLaunchType.FROM_LAUNCH_NEW_INCOGNITO_TAB;
                } else {
                    // Using FROM_LINK ensures the tab is parented to the current tab, which allows
                    // the back button to close these tabs and restore selection to the previous
                    // tab.
                    launchType = TabLaunchType.FROM_LINK;
                }
            }

            ChromeTabCreator tabCreator = getTabCreator(isIncognito);
            Tab parentTab = null;
            if (launchType == TabLaunchType.FROM_CHROME_UI) {
                int parentIdFromIntent = IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_PARENT_TAB_ID, Tab.INVALID_TAB_ID);
                if (parentIdFromIntent != Tab.INVALID_TAB_ID && mTabModelSelector != null) {
                    parentTab = mTabModelSelector.getTabById(parentIdFromIntent);
                }
            }

            Tab firstTab = tabCreator.createNewTab(loadUrlParams, launchType, parentTab, intent);

            List<String> additionalUrls =
                    IntentUtils.safeGetSerializableExtra(
                            intent, IntentHandler.EXTRA_ADDITIONAL_URLS);
            boolean openAdditionalUrlsInTabGroup =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_OPEN_ADDITIONAL_URLS_IN_TAB_GROUP, false);
            if (additionalUrls != null) {
                final Tab parent = openAdditionalUrlsInTabGroup ? firstTab : null;
                @TabLaunchType
                int additionalUrlLaunchType =
                        openAdditionalUrlsInTabGroup
                                ? TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP
                                : TabLaunchType.FROM_RESTORE;
                for (int i = 0; i < additionalUrls.size(); i++) {
                    String url = additionalUrls.get(i);
                    LoadUrlParams copy = LoadUrlParams.copy(loadUrlParams);
                    copy.setUrl(url);
                    tabCreator.createNewTab(copy, additionalUrlLaunchType, parent);
                }
            }
            return firstTab;
        }

        // Check if the tab is being created from a Reader Mode navigation.
        if (ReaderModeManager.isEnabled() && ReaderModeManager.isReaderModeCreatedIntent(intent)) {
            Bundle extras = intent.getExtras();
            int readerParentId =
                    IntentUtils.safeGetInt(
                            extras, ReaderModeManager.EXTRA_READER_MODE_PARENT, Tab.INVALID_TAB_ID);
            extras.remove(ReaderModeManager.EXTRA_READER_MODE_PARENT);
            // Set the parent tab to the tab that Reader Mode started from.
            if (readerParentId != Tab.INVALID_TAB_ID && mTabModelSelector != null) {
                return getCurrentTabCreator()
                        .createNewTab(
                                new LoadUrlParams(loadUrlParams.getUrl(), PageTransition.LINK),
                                TabLaunchType.FROM_LINK,
                                mTabModelSelector.getTabById(readerParentId));
            }
        }

        return getTabCreator(false)
                .launchUrlFromExternalApp(loadUrlParams, externalAppId, forceNewTab, intent);
    }

    private void showOverview() {
        if (mLayoutManager == null) return;

        if (isInOverviewMode()) {
            if (didFinishNativeInitialization()) {
                getCompositorViewHolderSupplier().get().hideKeyboard(CallbackUtils.emptyRunnable());
            }
        }

        Tab currentTab = getActivityTab();
        @LayoutType int layoutTypeToShow = LayoutType.TAB_SWITCHER;

        // If we don't have a current tab, show the overview mode.
        if (currentTab == null) {
            mLayoutManager.showLayout(layoutTypeToShow, false);
        } else {
            getCompositorViewHolderSupplier()
                    .get()
                    .hideKeyboard(() -> mLayoutManager.showLayout(layoutTypeToShow, true));
        }
    }

    private void hideOverview(boolean animate) {
        assert isInOverviewMode();
        if (getCurrentTabModel().getCount() != 0) {
            // Don't hide overview if current tab stack is empty()
            mLayoutManager.showLayout(LayoutType.BROWSING, animate);
        }
    }

    @Override
    public void onSaveInstanceState(Bundle outState) {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onSaveInstanceState")) {
            super.onSaveInstanceState(outState);
            CipherLazyHolder.sCipherInstance.saveToBundle(outState);
            outState.putInt(
                    WINDOW_INDEX, TabWindowManagerSingleton.getInstance().getIdForWindow(this));
            Boolean isIncognito = getCurrentTabModel().isIncognito();
            outState.putBoolean(IS_INCOGNITO_SELECTED, isIncognito);
            // If it's Incognito and native is initialized and profile exists, serialize duration
            // service state.
            if (isIncognito && ProfileManager.isInitialized()) {
                AndroidSessionDurationsServiceState.serializeFromNative(
                        outState, getCurrentTabModel().getProfile());
            }
        }
    }

    @Override
    public void onDestroyInternal() {
        if (mReadingListBackPressHandler != null) {
            mReadingListBackPressHandler.destroy();
            mReadingListBackPressHandler = null;
        }
        if (mMinimizeAppAndCloseTabBackPressHandler != null) {
            mMinimizeAppAndCloseTabBackPressHandler.destroy();
            mMinimizeAppAndCloseTabBackPressHandler = null;
        }

        if (mCallbackController != null) {
            mCallbackController.destroy();
            mCallbackController = null;
        }

        if (mJankTracker != null) {
            mJankTracker.destroy();
            mJankTracker = null;
        }

        if (mTabModelSelectorTabObserver != null) {
            mTabModelSelectorTabObserver.destroy();
            mTabModelSelectorTabObserver = null;
        }

        mTabModelNotificationDotManager.destroy();

        if (mHistoricalTabModelObserver != null) mHistoricalTabModelObserver.destroy();

        if (mUndoRefocusHelper != null) mUndoRefocusHelper.destroy();

        if (mTabModelObserver != null) mTabModelObserver.destroy();

        if (mTabModelSelectorObserver != null && mTabModelSelector != null) {
            mTabModelSelector.removeObserver(mTabModelSelectorObserver);
            mTabModelSelectorObserver = null;
        }

        if (mUndoBarPopupController != null) {
            mUndoBarPopupController.destroy();
            mUndoBarPopupController = null;
        }

        StartupPaintPreviewHelper startupPaintPreviewHelper =
                mStartupPaintPreviewHelperSupplier.get();
        if (startupPaintPreviewHelper != null) {
            startupPaintPreviewHelper.destroy();
        }
        mStartupPaintPreviewHelperSupplier.destroy();

        if (mModuleRegistrySupplier.hasValue()) {
            mModuleRegistrySupplier.get().destroy();
        }

        if (mIncognitoCookiesFetcher != null) {
            mIncognitoCookiesFetcher.destroy();
            mIncognitoCookiesFetcher = null;
        }
        IncognitoTabHostRegistry.getInstance().unregister(mIncognitoTabHost);

        TabObscuringHandler tabObscuringHandler = getTabObscuringHandler();
        if (tabObscuringHandler != null) {
            getTabObscuringHandler().removeObserver(mCompositorViewHolder);
        }

        if (isTablet()) ChromeAccessibilityUtil.get().removeObserver(mCompositorViewHolder);
        ChromeAccessibilityUtil.get().removeObserver(mLayoutManager);

        if (mTabDelegateFactory != null) mTabDelegateFactory.destroy();

        mAppLaunchDrawBlocker.destroy();

        if (mAuxiliarySearchController != null) {
            mAuxiliarySearchController.destroy(getLifecycleDispatcher());
        }

        if (mDragDropDelegate != null) {
            mDragDropDelegate.destroy();
        }

        if (mHubProvider != null) mHubProvider.destroy();

        if (mCleanUpHubOverviewColorObserver != null) {
            mCleanUpHubOverviewColorObserver.run();
            mCleanUpHubOverviewColorObserver = null;
        }

        if (mTabGroupVisualDataManager != null) {
            mTabGroupVisualDataManager.destroy();
            mTabGroupVisualDataManager = null;
        }

        if (mDseNewTabUrlManager != null) {
            mDseNewTabUrlManager.destroy();
        }

        if (mSuggestionEventObserver != null) {
            mSuggestionEventObserver.destroy();
            mSuggestionEventObserver = null;
        }

        if (mGroupSuggestionsPromotionCoordinator != null) {
            mGroupSuggestionsPromotionCoordinator.destroy();
            mGroupSuggestionsPromotionCoordinator = null;
        }

        if (mXrSceneCoreSessionManagerSupplier.hasValue()
                && mXrSceneCoreSessionManagerSupplier.get() != null) {
            var xrSceneCoreSessionManager = mXrSceneCoreSessionManagerSupplier.get();
            xrSceneCoreSessionManager
                    .getXrSpaceModeObservableSupplier()
                    .removeObserver(mOnXrSpaceModeChanged);
            xrSceneCoreSessionManager.destroy();
        }

        super.onDestroyInternal();
    }

    @Override
    protected void destroyTabModels() {
        if (mTabModelOrchestrator != null) {
            mTabModelOrchestrator.destroy();
        }
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (ChromeApplicationImpl.isSevereMemorySignal(level)) {
            NativePageAssassin.getInstance().freezeAllHiddenPages();
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        Boolean result =
                KeyboardShortcuts.dispatchKeyEvent(
                        event,
                        mUiWithNativeInitialized,
                        getFullscreenManager(),
                        /* menuOrKeyboardActionController= */ this,
                        this);

        if (Boolean.TRUE.equals(result)) return result;

        @Nullable ExtensionService extensionService = mRootUiCoordinator.getExtensionService();
        if (extensionService != null) {
            // Handle extension shortcuts.
            if (extensionService.dispatchKeyEvent(event)) {
                result = true;
            }
        }

        return result != null ? result : super.dispatchKeyEvent(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (!mUiWithNativeInitialized) {
            return super.onKeyDown(keyCode, event);
        }
        // Detecting a long press of the back button via onLongPress is broken in Android N.
        // To work around this, use a postDelayed, which is supported in all versions.
        if (keyCode == KeyEvent.KEYCODE_BACK
                && !isTablet()
                && !getFullscreenManager().getPersistentFullscreenMode()) {
            if (mShowHistoryRunnable == null) mShowHistoryRunnable = this::showFullHistorySheet;
            mHandler.postDelayed(mShowHistoryRunnable, ViewConfiguration.getLongPressTimeout());
            return super.onKeyDown(keyCode, event);
        }
        boolean isCurrentTabVisible =
                !isInOverviewMode() && (!isTablet() || getCurrentTabModel().getCount() != 0);
        boolean keyboardShortcutHandled =
                KeyboardShortcuts.onKeyDown(
                        event,
                        isCurrentTabVisible,
                        true,
                        getTabModelSelector(),
                        /* menuOrKeyboardActionController= */ this,
                        getToolbarManager());
        if (keyboardShortcutHandled) {
            RecordUserAction.record("MobileKeyboardShortcutUsed");
        }
        return keyboardShortcutHandled || super.onKeyDown(keyCode, event);
    }

    private void showFullHistorySheet() {
        ((TabbedRootUiCoordinator) mRootUiCoordinator).showFullHistorySheet();
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && !isTablet()) {
            mHandler.removeCallbacks(mShowHistoryRunnable);
            mShowHistoryRunnable = null;
            if (event.getEventTime() - event.getDownTime()
                            >= ViewConfiguration.getLongPressTimeout()
                    && NavigationSheet.isInstanceShowing(
                            mRootUiCoordinator.getBottomSheetController())) {
                // If tab history popup is showing, do not process the keyUp event
                // which will dismiss it immediately.
                return true;
            }
        }
        return super.onKeyUp(keyCode, event);
    }

    public boolean hasPendingNavigationRunnableForTesting() {
        ThreadUtils.assertOnUiThread();
        return mShowHistoryRunnable != null;
    }

    @Override
    public void onProvideKeyboardShortcuts(
            List<KeyboardShortcutGroup> data, Menu menu, int deviceId) {
        data.addAll(KeyboardShortcuts.createShortcutGroup(this));
    }

    @VisibleForTesting
    public View getTabsView() {
        return getCompositorViewHolderSupplier().get();
    }

    @VisibleForTesting
    public LayoutManagerChrome getLayoutManager() {
        return (LayoutManagerChrome) getCompositorViewHolderSupplier().get().getLayoutManager();
    }

    @VisibleForTesting
    public OneshotSupplier<LayoutStateProvider> getLayoutStateProviderSupplier() {
        return mLayoutStateProviderSupplier;
    }

    public OneshotSupplier<TabSwitcher> getTabSwitcherSupplierForTesting() {
        return mTabSwitcherSupplier;
    }

    private ComposedBrowserControlsVisibilityDelegate getAppBrowserControlsVisibilityDelegate() {
        return mRootUiCoordinator.getAppBrowserControlsVisibilityDelegate();
    }

    // App Menu related code -----------------------------------------------------------------------

    @Override
    public boolean canShowAppMenu() {
        // The popup menu relies on the model created during the full UI initialization, so do not
        // attempt to show the menu until the UI creation has finished.
        if (!mUiWithNativeInitialized) return false;

        // If the current active tab is showing a tab modal dialog, an app menu shouldn't be shown
        // in any cases, e.g. when a hardware menu button is clicked.
        Tab tab = getActivityTab();
        if (tab != null && ChromeTabModalPresenter.isDialogShowing(tab)) return false;

        return super.canShowAppMenu();
    }

    @Override
    public boolean isInOverviewMode() {
        return mLayoutManager != null && mLayoutManager.isLayoutVisible(LayoutType.TAB_SWITCHER);
    }

    @Override
    public void onStart() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onStart")) {
            super.onStart();
        }
    }

    @Override
    public void onStop() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onStop")) {
            super.onStop();
        }
    }

    @Override
    public void onPause() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onPause")) {

            // isColdStart() relies on {@link SimpleStartupForegroundSessionDetector} and the
            // session is discarded during AsyncInitializationActivity's onPause so the metric has
            // to be recorded here instead of onPauseWithNative().
            if (!isColdStart()
                    && isMainIntentLaunchPostOnResume()
                    && mTimeToFirstDrawAfterStartMs != 0) {
                RecordHistogram.recordTimesHistogram(
                        HISTOGRAM_MAIN_INTENT_TIME_TO_FIRST_DRAW_WARM_MS,
                        mTimeToFirstDrawAfterStartMs);
            }
            super.onPause();
        }
    }

    @Override
    public void onResume() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onResume")) {
            super.onResume();
        }
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onActivityResult")) {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    @Override
    public void performOnConfigurationChanged(Configuration newConfig) {
        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.performOnConfigurationChanged")) {
            super.performOnConfigurationChanged(newConfig);
        }
    }

    @Override
    public void onSceneChange(Layout layout) {
        super.onSceneChange(layout);
        if (!layout.shouldDisplayContentOverlay()) mTabModelSelector.onTabsViewShown();
    }

    /** Writes the tab state to disk. */
    @VisibleForTesting
    public void saveState() {
        mTabModelOrchestrator.saveState();

        // Save whether the current tab is a search result page into preferences.
        Tab currentStandardTab = TabModelUtils.getCurrentTab(mTabModelSelector.getModel(false));
        ChromeSharedPreferences.getInstance()
                .writeBoolean(
                        ChromePreferenceKeys.IS_LAST_VISITED_TAB_SRP,
                        currentStandardTab != null
                                && UrlUtilitiesJni.get()
                                        .isGoogleSearchUrl(currentStandardTab.getUrl().getSpec()));
    }

    @Override
    public void onEnterVr() {
        super.onEnterVr();
        mControlContainer.setVisibility(View.INVISIBLE);
        if (mVrBrowserControlsVisibilityDelegate == null) {
            mVrBrowserControlsVisibilityDelegate =
                    new BrowserControlsVisibilityDelegate(BrowserControlsState.BOTH);
            getAppBrowserControlsVisibilityDelegate()
                    .addDelegate(mVrBrowserControlsVisibilityDelegate);
        }
        mVrBrowserControlsVisibilityDelegate.set(BrowserControlsState.HIDDEN);
    }

    @Override
    public void onExitVr() {
        super.onExitVr();
        mControlContainer.setVisibility(View.VISIBLE);
        if (mVrBrowserControlsVisibilityDelegate != null) {
            mVrBrowserControlsVisibilityDelegate.set(BrowserControlsState.BOTH);
        }
    }

    /**
     * Reports that a new tab launcher shortcut was selected or an action equivalent to a shortcut
     * was performed.
     *
     * @param isIncognito Whether the shortcut or action created a new incognito tab.
     */
    private void reportNewTabShortcutUsed(boolean isIncognito) {
        ShortcutManager shortcutManager = getSystemService(ShortcutManager.class);
        shortcutManager.reportShortcutUsed(
                isIncognito ? "new-incognito-tab-shortcut" : "new-tab-shortcut");
    }

    public MultiInstanceManager getMultiInstanceMangerForTesting() {
        return mMultiInstanceManager;
    }

    public MismatchedIndicesHandler getMismatchedIndicesHandlerForTesting() {
        return mMismatchedIndicesHandler;
    }

    @VisibleForTesting
    public ChromeNextTabPolicySupplier getNextTabPolicySupplier() {
        return (ChromeNextTabPolicySupplier) mNextTabPolicySupplier;
    }

    /** Returns whether to show a NTP as the home surface at startup on tablet in regular mode. */
    private boolean shouldShowNtpHomeSurfaceOnStartup() {
        if (mTabModelSelector.isIncognitoSelected()) return false;

        assert mInactivityTracker != null;
        return ReturnToChromeUtil.shouldShowNtpAsHomeSurfaceAtStartup(
                getIntent(), getSavedInstanceState(), mInactivityTracker);
    }

    /**
     * Creates an adapter between the hub's overview alpha observer that takes a float and the
     * format that the hub expects which is a double.
     */
    private OverviewModeAlphaObserver adaptOnOverviewColorAlphaChange() {
        return alpha -> {
            // If the manager is still null, it doesn't matter whatever is happening. Can safely
            // ignore any signal.
            HubManager hubManager = mHubManagerSupplier.get();
            if (hubManager == null) {
                return;
            }

            OverviewModeAlphaObserver overviewModeAlphaObserver =
                    hubManager.getHubController().getHubColorMixer().getOverviewModeAlphaObserver();
            overviewModeAlphaObserver.accept(alpha);
        };
    }

    private void maybeShowTabSwitcherAfterTabModelLoad(Intent intent) {
        if (!TabUiUtils.isDataSharingFunctionalityEnabled()) return;
        if (!IntentHandler.wasIntentSenderChrome(intent)) return;
        // TODO(crbug.com/369186228): Remove incognito early out.
        if (mTabModelSelector.isIncognitoSelected()) return;

        if (IntentUtils.safeHasExtra(intent, DataSharingIntentUtils.INVITATION_URL_EXTRA)) {
            String urlString =
                    IntentUtils.safeGetStringExtra(
                            intent, DataSharingIntentUtils.INVITATION_URL_EXTRA);
            GURL url = new GURL(urlString);

            DataSharingTabManager dataSharingTabManager =
                    mRootUiCoordinator.getDataSharingTabManager();
            dataSharingTabManager.initiateJoinFlow(
                    url,
                    (onTabSwitcherShownRunnable) ->
                            doRunnableOnTabSwitcher(onTabSwitcherShownRunnable));
        } else if (IntentUtils.safeHasExtra(
                intent, DataSharingIntentUtils.TAB_GROUP_SYNC_ID_EXTRA)) {
            String syncId =
                    IntentUtils.safeGetStringExtra(
                            intent, DataSharingIntentUtils.TAB_GROUP_SYNC_ID_EXTRA);
            Runnable openTabGroupDialogRunnable =
                    () -> {
                        DataSharingTabManager dataSharingTabManager =
                                mRootUiCoordinator.getDataSharingTabManager();
                        TabGroupSyncService tabGroupSyncService =
                                TabGroupSyncServiceFactory.getForProfile(
                                        mTabModelProfileSupplier.get());
                        SavedTabGroup syncGroup = tabGroupSyncService.getGroup(syncId);
                        dataSharingTabManager.displayTabGroupAnywhere(
                                syncGroup.collaborationId, /* isFromInviteFlow= */ false);
                    };
            doRunnableOnTabSwitcher(openTabGroupDialogRunnable);
        }
    }

    private void doRunnableOnTabSwitcher(Runnable runnable) {
        OneshotSupplier<TabModelSelector> wrappedSelector =
                TabModelUtils.onInitializedTabModelSelector(getTabModelSelectorSupplier());
        SupplierUtils.waitForAll(
                () ->
                        TabSwitcherUtils.navigateToTabSwitcher(
                                mLayoutManager, /* animate= */ false, runnable),
                wrappedSelector,
                mLayoutStateProviderSupplier);
    }

    public UndoBarController getUndoBarControllerForTesting() {
        return mUndoBarPopupController;
    }

    private void initiateArchivedTabsAutoDeletePromoManager() {
        if (ChromeFeatureList.sAndroidTabDeclutterAutoDelete.isEnabled()
                && ChromeFeatureList.sAndroidTabDeclutterAutoDeleteKillSwitch.isEnabled()
                && !ChromeSharedPreferences.getInstance()
                        .readBoolean(
                                ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_DECISION_MADE,
                                false)) {
            mArchivedTabsAutoDeletePromoManager =
                    new ArchivedTabsAutoDeletePromoManager(
                            ChromeTabbedActivity.this,
                            mRootUiCoordinator.getBottomSheetController(),
                            new TabArchiveSettings(ChromeSharedPreferences.getInstance()),
                            ArchivedTabModelOrchestrator.getForProfile(
                                            mTabModelSelector.getCurrentModel().getProfile())
                                    .getTabCountSupplier(),
                            mTabModelSelector.getModel(false));
        }
    }

    /**
     * Hide/show web content, make background opaque or transparent when switching to/from XR full
     * space mode.
     *
     * <p>Toolbar updates its visibility in ToolbarManager.onXrSpaceModeChanged.
     *
     * @param fullSpaceMode: true to hide, false to show
     */
    public void onXrSpaceModeChanged(boolean fullSpaceMode) {
        if (mCompositorViewHolder != null) {
            mCompositorViewHolder.getCompositorView().setXrFullSpaceMode(fullSpaceMode);

            // TODO(crbug.com/422140378): The video overlay stays visible on the transparent
            //  background of the HubLayout if webcontent is not hidden.
            Tab tab = mTabModelSelector.getCurrentTab();
            if (tab != null) {
                tab.getWebContents()
                        .updateWebContentsVisibility(
                                fullSpaceMode ? Visibility.HIDDEN : Visibility.VISIBLE);
            }
        }
    }

    private @Nullable XrSceneCoreSessionManager createXrSceneCoreSessionManager() {
        assert !mXrSceneCoreSessionManagerSupplier.hasValue();

        XrSceneCoreSessionManager xrSceneCoreSessionManager = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            // TODO(crbug.com/422134376): To detect "Android XR" query OS instead of device's
            // properties.
            if (XrUtils.isXrDevice()) {
                xrSceneCoreSessionManager = new XrSceneCoreSessionManagerImpl(this);
                xrSceneCoreSessionManager
                        .getXrSpaceModeObservableSupplier()
                        .addSyncObserver(mOnXrSpaceModeChanged);
            }
        }
        return xrSceneCoreSessionManager;
    }

    private @Nullable ObservableSupplier<Boolean> getXrSpaceModeObservableSupplier() {
        var xrSceneCoreSessionManager = mXrSceneCoreSessionManagerSupplier.get();
        return xrSceneCoreSessionManager != null
                ? xrSceneCoreSessionManager.getXrSpaceModeObservableSupplier()
                : null;
    }
}
