// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.dom_distiller;

import static org.chromium.components.embedder_support.util.UrlConstants.CHROME_NATIVE_SCHEME;
import static org.chromium.components.embedder_support.util.UrlConstants.CHROME_SCHEME;

import android.app.Activity;
import android.content.Intent;
import android.content.res.Resources;
import android.net.Uri;
import android.os.SystemClock;
import android.util.Pair;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.browser.customtabs.CustomTabsIntent;

import org.chromium.base.CommandLine;
import org.chromium.base.IntentUtils;
import org.chromium.base.RequiredCallback;
import org.chromium.base.SysUtils;
import org.chromium.base.UserData;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.browser_controls.BrowserControlsVisibilityManager;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider.CustomTabsUiType;
import org.chromium.chrome.browser.customtabs.CustomTabActivity;
import org.chromium.chrome.browser.customtabs.CustomTabIntentDataProvider;
import org.chromium.chrome.browser.customtabs.IncognitoCustomTabIntentDataProvider;
import org.chromium.chrome.browser.document.ChromeLauncherActivity;
import org.chromium.chrome.browser.dom_distiller.TabDistillabilityProvider.DistillabilityObserver;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.fullscreen.BrowserControlsManager;
import org.chromium.chrome.browser.fullscreen.BrowserControlsManagerSupplier;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.Tab.LoadUrlResult;
import org.chromium.chrome.browser.tab.TabHidingType;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab.TabUtils;
import org.chromium.components.dom_distiller.core.DomDistillerFeatures;
import org.chromium.components.dom_distiller.core.DomDistillerUrlUtils;
import org.chromium.components.messages.DismissReason;
import org.chromium.components.messages.MessageBannerProperties;
import org.chromium.components.messages.MessageDispatcher;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.components.messages.MessageIdentifier;
import org.chromium.components.messages.MessageScopeType;
import org.chromium.components.messages.PrimaryActionClickBehavior;
import org.chromium.components.navigation_interception.InterceptNavigationDelegate;
import org.chromium.components.ukm.UkmRecorder;
import org.chromium.content_public.browser.LoadCommittedDetails;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationController;
import org.chromium.content_public.browser.NavigationEntry;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.util.ColorUtils;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.LinkedHashSet;

/**
 * Manages UI effects for reader mode including hiding and showing the reader mode and reader mode
 * preferences toolbar icon and hiding the browser controls when a reader mode page has finished
 * loading.
 */
public class ReaderModeManager extends EmptyTabObserver implements UserData {
    /** Possible states that the distiller can be in on a web page. */
    @IntDef({
        DistillationStatus.POSSIBLE,
        DistillationStatus.NOT_POSSIBLE,
        DistillationStatus.STARTED
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface DistillationStatus {
        /** POSSIBLE means reader mode can be entered. */
        int POSSIBLE = 0;

        /** NOT_POSSIBLE means reader mode cannot be entered. */
        int NOT_POSSIBLE = 1;

        /** STARTED means reader mode is currently in reader mode. */
        int STARTED = 2;
    }

    /** The key to access this object from a {@Tab}. */
    public static final Class<ReaderModeManager> USER_DATA_KEY = ReaderModeManager.class;

    /** The scheme used to access DOM-Distiller. */
    public static final String DOM_DISTILLER_SCHEME = "chrome-distiller";

    /** The intent extra that indicates origin from Reader Mode */
    public static final String EXTRA_READER_MODE_PARENT =
            "org.chromium.chrome.browser.dom_distiller.EXTRA_READER_MODE_PARENT";

    /** Histogram name for the state of the reader mode accessibility setting. */
    public static final String ACCESSIBILITY_SETTING_HISTOGRAM =
            "DomDistiller.Android.OnDistillableResult.AccessibilitySettingEnabled";

    /** Histogram name for the end distillability result. */
    public static final String PAGE_DISTILLATION_RESULT_HISTOGRAM =
            "DomDistiller.Android.OnDistillableResult.PageDistillationResult";

    /** The url of the last page visited if the last page was reader mode page. Otherwise null. */
    private GURL mReaderModePageUrl;

    /** Whether the current web page was distillable or not has been determined. */
    private boolean mIsCurrentPageDistillationStatusDetermined;

    /** The WebContentsObserver responsible for updates to the distillation status of the tab. */
    private WebContentsObserver mWebContentsObserver;

    /** The distillation status of the tab. */
    @DistillationStatus private int mDistillationStatus;

    /** If the prompt was dismissed by the user. */
    private boolean mIsDismissed;

    /**
     * The URL that distiller is using for this tab. This is used to check if a result comes back
     * from distiller and the user has already loaded a new URL.
     */
    private GURL mDistillerUrl;

    /** Used to flag that the prompt was shown and recorded by UMA. */
    private boolean mShowPromptRecorded;

    /** Whether or not the current tab is a Reader Mode page. */
    private boolean mIsViewingReaderModePage;

    /** The time that the user started viewing Reader Mode content. */
    private long mViewStartTimeMs;

    /** The distillability observer attached to the tab. */
    private DistillabilityObserver mDistillabilityObserver;

    /** Whether this manager and tab have been destroyed. */
    private boolean mIsDestroyed;

    /** The tab this manager is attached to. */
    private final Tab mTab;

    /** The supplier of MessageDispatcher to display the message. */
    private final Supplier<MessageDispatcher> mMessageDispatcherSupplier;

    // Hold on to the InterceptNavigationDelegate that the custom tab uses.
    InterceptNavigationDelegate mCustomTabNavigationDelegate;

    /** Whether the messages UI was requested for a navigation. */
    private boolean mMessageRequestedForNavigation;

    // Record the sites which users refuse to view in reader mode.
    // If the size is larger than the capacity, remove the earliest added site first.
    private static final LinkedHashSet<Integer> sMutedSites = new LinkedHashSet<>();
    private static final int MAX_SIZE_OF_DECLINED_SITES = 100;

    /** Whether the message ui is being shown or has already been shown. */
    private boolean mMessageShown;

    /** Property Model of Reader mode message. */
    private PropertyModel mMessageModel;

    /** Whether the reader mode button is currently being shown on the toolbar. */
    private boolean mIsReaderModeButtonShowingOnToolbar;

    // Whether the manager has been notified that a contextual page action has been shown for the
    // current navigation.
    private boolean mHasBeenNotifiedOfCpa;

    ReaderModeManager(Tab tab, Supplier<MessageDispatcher> messageDispatcherSupplier) {
        super();
        mTab = tab;
        mTab.addObserver(this);
        mMessageDispatcherSupplier = messageDispatcherSupplier;
    }

    /**
     * Create an instance of the {@link ReaderModeManager} for the provided tab.
     * @param tab The tab that will have a manager instance attached to it.
     */
    public static void createForTab(Tab tab) {
        tab.getUserDataHost()
                .setUserData(
                        USER_DATA_KEY,
                        new ReaderModeManager(
                                tab, () -> MessageDispatcherProvider.from(tab.getWindowAndroid())));
    }

    /** Clear the status map and references to other objects. */
    @Override
    public void destroy() {
        if (mWebContentsObserver != null) mWebContentsObserver.observe(null);
        mHasBeenNotifiedOfCpa = false;
        mIsReaderModeButtonShowingOnToolbar = false;
        mIsDestroyed = true;
    }

    @Override
    public void onLoadUrl(Tab tab, LoadUrlParams params, LoadUrlResult loadUrlResult) {
        // If a distiller URL was loaded and this is a custom tab, add a navigation
        // handler to bring any navigations back to the main chrome activity.
        Activity activity = TabUtils.getActivity(tab);
        int uiType = CustomTabsUiType.DEFAULT;
        if (activity != null && activity.getIntent().getExtras() != null) {
            uiType =
                    activity.getIntent()
                            .getExtras()
                            .getInt(CustomTabIntentDataProvider.EXTRA_UI_TYPE);
        }
        if (tab == null
                || uiType != CustomTabsUiType.READER_MODE
                || !DomDistillerUrlUtils.isDistilledPage(params.getUrl())) {
            return;
        }

        WebContents webContents = tab.getWebContents();
        if (webContents == null) return;

        mCustomTabNavigationDelegate =
                new InterceptNavigationDelegate() {
                    @Override
                    public void shouldIgnoreNavigation(
                            NavigationHandle navigationHandle,
                            GURL escapedUrl,
                            boolean hiddenCrossFrame,
                            boolean isSandboxedFrame,
                            boolean shouldRunAsync,
                            RequiredCallback<Boolean> resultCallback) {
                        if (DomDistillerUrlUtils.isDistilledPage(navigationHandle.getUrl())
                                || navigationHandle.isExternalProtocol()) {
                            resultCallback.onResult(false);
                            return;
                        }

                        Intent returnIntent =
                                new Intent(Intent.ACTION_VIEW, Uri.parse(escapedUrl.getSpec()));
                        returnIntent.setClassName(activity, ChromeLauncherActivity.class.getName());

                        // Set the parent ID of the tab to be created.
                        returnIntent.putExtra(
                                EXTRA_READER_MODE_PARENT,
                                IntentUtils.safeGetInt(
                                        activity.getIntent().getExtras(),
                                        EXTRA_READER_MODE_PARENT,
                                        Tab.INVALID_TAB_ID));

                        activity.startActivity(returnIntent);
                        activity.finish();
                        resultCallback.onResult(true);
                    }
                };

        DomDistillerTabUtils.setInterceptNavigationDelegate(
                mCustomTabNavigationDelegate, webContents);
    }

    @Override
    public void onPageLoadFinished(Tab tab, GURL url) {
        if (!DomDistillerFeatures.sReaderModeAutoDistill.isEnabled()
                || url.getScheme().equals(DOM_DISTILLER_SCHEME)
                || url.getScheme().equals(CHROME_SCHEME)
                || url.getScheme().equals(CHROME_NATIVE_SCHEME)) {
            return;
        }

        distillInCustomTab();
    }

    @Override
    public void onShown(Tab shownTab, @TabSelectionType int type) {
        // If the reader mode prompt was dismissed, stop here.
        if (mIsDismissed) return;

        mHasBeenNotifiedOfCpa = false;
        mIsReaderModeButtonShowingOnToolbar = false;
        mDistillationStatus = DistillationStatus.NOT_POSSIBLE;
        mDistillerUrl = shownTab.getUrl();

        if (mDistillabilityObserver == null) {
            setDistillabilityObserver(shownTab);
        }

        if (DomDistillerUrlUtils.isDistilledPage(shownTab.getUrl()) && !mIsViewingReaderModePage) {
            onStartedReaderMode();
        }

        // Make sure there is a WebContentsObserver on this tab's WebContents.
        if (mWebContentsObserver == null && mTab.getWebContents() != null) {
            mWebContentsObserver = createWebContentsObserver();
        }
        tryShowingPrompt();
    }

    @Override
    public void onHidden(Tab tab, @TabHidingType int reason) {
        if (mIsViewingReaderModePage) {
            long timeMs = onExitReaderMode();
            recordReaderModeViewDuration(timeMs);
        }
    }

    @Override
    public void onDestroyed(Tab tab) {
        mHasBeenNotifiedOfCpa = false;
        mIsReaderModeButtonShowingOnToolbar = false;
        if (tab == null) return;

        // If the prompt was not shown for the previous navigation, record it now.
        if (!mShowPromptRecorded) {
            recordPromptVisibilityForNavigation(false);
        }
        if (mIsViewingReaderModePage) {
            long timeMs = onExitReaderMode();
            recordReaderModeViewDuration(timeMs);
        }
        TabDistillabilityProvider.get(tab).removeObserver(mDistillabilityObserver);

        removeTabState();
    }

    @Override
    public void onActivityAttachmentChanged(Tab tab, @Nullable WindowAndroid window) {
        // Intentionally do nothing to prevent automatic observer removal on detachment.
    }

    /** Clear the reader mode state for this manager. */
    private void removeTabState() {
        if (mWebContentsObserver != null) mWebContentsObserver.observe(null);
        mDistillationStatus = DistillationStatus.POSSIBLE;
        mIsDismissed = false;
        mMessageRequestedForNavigation = false;
        mDistillerUrl = null;
        mShowPromptRecorded = false;
        mIsViewingReaderModePage = false;
        mHasBeenNotifiedOfCpa = false;
        mIsReaderModeButtonShowingOnToolbar = false;
        mDistillabilityObserver = null;
    }

    @Override
    public void onContentChanged(Tab tab) {
        mHasBeenNotifiedOfCpa = false;
        mIsReaderModeButtonShowingOnToolbar = false;
        // If the content change was because of distiller switching web contents or Reader Mode has
        // already been dismissed for this tab do nothing.
        if (mIsDismissed && !DomDistillerUrlUtils.isDistilledPage(tab.getUrl())) return;

        // If the tab state already existed, only reset the relevant data. Things like view duration
        // need to be preserved.
        mDistillationStatus = DistillationStatus.NOT_POSSIBLE;
        mDistillerUrl = tab.getUrl();

        if (tab.getWebContents() != null) {
            mWebContentsObserver = createWebContentsObserver();
            if (DomDistillerUrlUtils.isDistilledPage(tab.getUrl())) {
                mDistillationStatus = DistillationStatus.STARTED;
                mReaderModePageUrl = tab.getUrl();
            }
        }
    }

    /** A notification that the user started viewing Reader Mode. */
    private void onStartedReaderMode() {
        mIsViewingReaderModePage = true;
        mViewStartTimeMs = SystemClock.elapsedRealtime();

        new UkmRecorder(mTab.getWebContents(), "DomDistiller.Android.ReaderModeShown")
                .addBooleanMetric("Shown")
                .record();
        RecordUserAction.record("DomDistiller.Android.OnStartedReaderMode");
    }

    /**
     * A notification that the user is no longer viewing Reader Mode. This could be because of a
     * navigation away from the page, switching tabs, or closing the browser.
     * @return The amount of time in ms that the user spent viewing Reader Mode.
     */
    private long onExitReaderMode() {
        mIsViewingReaderModePage = false;
        return SystemClock.elapsedRealtime() - mViewStartTimeMs;
    }

    /**
     * Record if the prompt became visible on the current page. This can be overridden for testing.
     * @param visible If the prompt was visible at any time.
     */
    private void recordPromptVisibilityForNavigation(boolean visible) {
        RecordHistogram.recordBooleanHistogram("DomDistiller.ReaderShownForPageLoad", visible);
    }

    /** A notification that the prompt was dismissed without being used. */
    public void onClosed() {
        mIsDismissed = true;
    }

    private WebContentsObserver createWebContentsObserver() {
        return new WebContentsObserver(mTab.getWebContents()) {
            /** Whether or not the previous navigation should be removed. */
            private boolean mShouldRemovePreviousNavigation;

            /** The index of the last committed distiller page in history. */
            private int mLastDistillerPageIndex;

            @Override
            public void didStartNavigationInPrimaryMainFrame(NavigationHandle navigation) {
                if (navigation.isSameDocument()) return;

                // Reader Mode should not pollute the navigation stack. To avoid this, watch for
                // navigations and prepare to remove any that are "chrome-distiller" urls.
                NavigationController controller = getWebContents().getNavigationController();
                int index = controller.getLastCommittedEntryIndex();
                NavigationEntry entry = controller.getEntryAtIndex(index);

                if (entry != null && DomDistillerUrlUtils.isDistilledPage(entry.getUrl())) {
                    mShouldRemovePreviousNavigation = true;
                    mLastDistillerPageIndex = index;
                }

                if (mIsDestroyed) return;

                mDistillerUrl = navigation.getUrl();
                if (DomDistillerUrlUtils.isDistilledPage(navigation.getUrl())) {
                    mDistillationStatus = DistillationStatus.STARTED;
                    mReaderModePageUrl = navigation.getUrl();
                }
            }

            @Override
            public void didFinishNavigationInPrimaryMainFrame(NavigationHandle navigation) {
                // TODO(cjhopman): This should possibly ignore navigations that replace the entry
                // (like those from history.replaceState()).
                if (!navigation.hasCommitted() || navigation.isSameDocument()) {
                    return;
                }

                if (mShouldRemovePreviousNavigation) {
                    mShouldRemovePreviousNavigation = false;
                    NavigationController controller = getWebContents().getNavigationController();
                    if (controller.getEntryAtIndex(mLastDistillerPageIndex) != null) {
                        controller.removeEntryAtIndex(mLastDistillerPageIndex);
                    }
                }

                if (mIsDestroyed) return;

                mDistillationStatus = DistillationStatus.POSSIBLE;
                if (mReaderModePageUrl == null
                        || !navigation
                                .getUrl()
                                .equals(
                                        DomDistillerUrlUtils.getOriginalUrlFromDistillerUrl(
                                                mReaderModePageUrl))) {
                    mDistillationStatus = DistillationStatus.NOT_POSSIBLE;
                    mIsCurrentPageDistillationStatusDetermined = false;
                }
                mReaderModePageUrl = null;

                if (mDistillationStatus == DistillationStatus.POSSIBLE) {
                    mHasBeenNotifiedOfCpa = false;
                    mIsReaderModeButtonShowingOnToolbar = false;
                    tryShowingPrompt();
                }
            }

            @Override
            public void navigationEntryCommitted(LoadCommittedDetails details) {
                if (mIsDestroyed) return;
                // Reset closed state of reader mode in this tab once we know a navigation is
                // happening.
                mIsDismissed = false;
                mHasBeenNotifiedOfCpa = false;
                mIsReaderModeButtonShowingOnToolbar = false;
                mMessageRequestedForNavigation = false;

                // If the prompt was not shown for the previous navigation, record it now.
                if (mTab != null && !mTab.isNativePage() && !mTab.isBeingRestored()) {
                    recordPromptVisibilityForNavigation(false);
                }
                mShowPromptRecorded = false;

                if (mTab != null
                        && !DomDistillerUrlUtils.isDistilledPage(mTab.getUrl())
                        && mIsViewingReaderModePage) {
                    long timeMs = onExitReaderMode();
                    recordReaderModeViewDuration(timeMs);
                }
            }
        };
    }

    /**
     * Record the amount of time the user spent in Reader Mode.
     * @param timeMs The amount of time in ms that the user spent in Reader Mode.
     */
    private void recordReaderModeViewDuration(long timeMs) {
        RecordHistogram.recordLongTimesHistogram("DomDistiller.Time.ViewingReaderModePage", timeMs);
    }

    /** Try showing the reader mode prompt. */
    @VisibleForTesting
    void tryShowingPrompt() {
        if (mTab == null || mTab.getWebContents() == null) return;

        // This prompt should only be shown on incognito or custom tabs, in other cases we'll show a
        // toolbar button (contextual page action) instead.
        if (!shouldUseReaderModeMessages(mTab)) return;

        if (mTab.isCustomTab() && ChromeFeatureList.sCctAdaptiveButton.isEnabled()) {
            // If the manager hasn't been notified of the CPA yet, or the reader mode button is
            // already showing on the toolbar, don't show the prompt.
            if (!mHasBeenNotifiedOfCpa || mIsReaderModeButtonShowingOnToolbar) return;
        }

        // Test if the user is requesting the desktop site. Ignore this if distiller is set to
        // ALWAYS_TRUE.
        boolean usingRequestDesktopSite =
                mTab.getWebContents().getNavigationController().getUseDesktopUserAgent()
                        && !DomDistillerTabUtils.isHeuristicAlwaysTrue();

        if (usingRequestDesktopSite
                || mDistillationStatus != DistillationStatus.POSSIBLE
                || mIsDismissed) {
            return;
        }

        if (sMutedSites.contains(urlToHash(mDistillerUrl))) {
            return;
        }

        MessageDispatcher messageDispatcher = mMessageDispatcherSupplier.get();
        if (messageDispatcher != null) {
            if (!mMessageRequestedForNavigation) {
                // If feature is disabled, reader mode message ui is only shown once per tab.
                if (mMessageShown) {
                    return;
                }
                showReaderModeMessage(messageDispatcher);
                mMessageShown = true;
            }
            mMessageRequestedForNavigation = true;
        }
    }

    private void showReaderModeMessage(MessageDispatcher messageDispatcher) {
        if (mMessageModel != null) {
            // It is safe to dismiss a message which has been dismissed previously.
            messageDispatcher.dismissMessage(mMessageModel, DismissReason.DISMISSED_BY_FEATURE);
        }
        Resources resources = mTab.getContext().getResources();
        // Save url for #onMessageDismissed. mDistillerUrl may have been changed and became
        // different from the url when message is enqueued.
        GURL url = mDistillerUrl;
        mMessageModel =
                new PropertyModel.Builder(MessageBannerProperties.ALL_KEYS)
                        .with(
                                MessageBannerProperties.MESSAGE_IDENTIFIER,
                                MessageIdentifier.READER_MODE)
                        .with(
                                MessageBannerProperties.TITLE,
                                resources.getString(R.string.reader_mode_message_title))
                        .with(
                                MessageBannerProperties.ICON_RESOURCE_ID,
                                R.drawable.ic_mobile_friendly_24dp)
                        .with(
                                MessageBannerProperties.PRIMARY_BUTTON_TEXT,
                                resources.getString(R.string.reader_mode_message_button))
                        .with(
                                MessageBannerProperties.ON_PRIMARY_ACTION,
                                () -> {
                                    activateReaderMode();
                                    return PrimaryActionClickBehavior.DISMISS_IMMEDIATELY;
                                })
                        .with(
                                MessageBannerProperties.ON_DISMISSED,
                                (reason) -> onMessageDismissed(url, reason))
                        .build();
        messageDispatcher.enqueueMessage(
                mMessageModel, mTab.getWebContents(), MessageScopeType.NAVIGATION, false);
    }

    private void onMessageDismissed(GURL url, @DismissReason int dismissReason) {
        mMessageModel = null;
        if (dismissReason == DismissReason.GESTURE) {
            onClosed();
        }

        if (dismissReason != DismissReason.PRIMARY_ACTION) {
            addUrlToMutedSites(url);
        }
    }

    private void addUrlToMutedSites(GURL url) {
        sMutedSites.add(urlToHash(url));
        while (sMutedSites.size() > MAX_SIZE_OF_DECLINED_SITES) {
            int v = sMutedSites.iterator().next();
            sMutedSites.remove(v);
        }
    }

    private void removeUrlFromMutedSites(GURL url) {
        sMutedSites.remove(urlToHash(url));
    }

    public void activateReaderMode() {
        // Contextual page action buttons can't be dismissed, instead we consider a shown but unused
        // button as "dismissed" and mute the site on setReaderModeUiShown(). When the button gets
        // clicked we un-mute the site to prevent the rate limiting logic from showing the CPA
        // button for this site on other tabs.
        removeUrlFromMutedSites(mDistillerUrl);

        if (!SysUtils.isLowEndDevice() && !shouldUseRegularTabsForDistillation()) {
            distillInCustomTab();
        } else {
            navigateToReaderMode();
        }
        RecordUserAction.record("MobileReaderModeActivated");
    }

    private boolean shouldUseRegularTabsForDistillation() {
        return DomDistillerFeatures.sReaderModeDistillInApp.isEnabled();
    }

    /** Navigate the current tab to a Reader Mode URL. */
    private void navigateToReaderMode() {
        WebContents webContents = mTab.getWebContents();
        if (webContents == null) return;

        onStartedReaderMode();

        FullscreenManager fullscreenManager = getFullscreenManager();
        if (fullscreenManager != null) {
            // Make sure to exit fullscreen mode before navigating.
            fullscreenManager.onExitFullscreen(mTab);
        }

        // RenderWidgetHostViewAndroid hides the controls after transitioning to reader mode.
        // See the long history of the issue in https://crbug.com/825765, https://crbug.com/853686,
        // https://crbug.com/861618, https://crbug.com/922388.
        // TODO(pshmakov): find a proper solution instead of this workaround.
        BrowserControlsVisibilityManager browserControlsVisibilityManager =
                getBrowserControlsVisibilityManager();
        if (browserControlsVisibilityManager != null) {
            getBrowserControlsVisibilityManager()
                    .getBrowserVisibilityDelegate()
                    .showControlsTransient();
        }

        DomDistillerTabUtils.distillCurrentPageAndView(webContents);
    }

    private @Nullable BrowserControlsManager getBrowserControlsManager() {
        return BrowserControlsManagerSupplier.getValueOrNullFrom(mTab.getWindowAndroid());
    }

    private @Nullable BrowserControlsVisibilityManager getBrowserControlsVisibilityManager() {
        return getBrowserControlsManager();
    }

    private @Nullable FullscreenManager getFullscreenManager() {
        BrowserControlsManager browserControlsManager = getBrowserControlsManager();
        return browserControlsManager == null
                ? null
                : browserControlsManager.getFullscreenManager();
    }

    private void distillInCustomTab() {
        Activity activity = TabUtils.getActivity(mTab);
        WebContents webContents = mTab.getWebContents();
        if (webContents == null) return;

        GURL url = webContents.getLastCommittedUrl();

        onStartedReaderMode();

        DomDistillerTabUtils.distillCurrentPage(webContents);

        String distillerUrl =
                DomDistillerUrlUtils.getDistillerViewUrlFromUrl(
                        DOM_DISTILLER_SCHEME, url.getSpec(), webContents.getTitle());

        CustomTabsIntent.Builder builder = new CustomTabsIntent.Builder();
        builder.setShowTitle(true);
        builder.setColorScheme(
                ColorUtils.inNightMode(activity)
                        ? CustomTabsIntent.COLOR_SCHEME_DARK
                        : CustomTabsIntent.COLOR_SCHEME_LIGHT);
        CustomTabsIntent customTabsIntent = builder.build();
        customTabsIntent.intent.setClassName(activity, CustomTabActivity.class.getName());

        // Customize items on menu as Reader Mode UI to show 'Find in page' and 'Preference' only.
        CustomTabIntentDataProvider.addReaderModeUiExtras(customTabsIntent.intent);

        // Add the parent ID as an intent extra for back button functionality.
        customTabsIntent.intent.putExtra(EXTRA_READER_MODE_PARENT, mTab.getId());

        // Use Incognito CCT if the source page is in Incognito mode.
        if (mTab.isIncognito()) {
            IncognitoCustomTabIntentDataProvider.addIncognitoExtrasForChromeFeatures(
                    customTabsIntent.intent, IntentHandler.IncognitoCctCallerId.READER_MODE);
        }

        customTabsIntent.launchUrl(activity, Uri.parse(distillerUrl));
    }

    /**
     * Set the observer for updating reader mode status based on whether or not the page should be
     * viewed in reader mode.
     *
     * @param tabToObserve The tab to attach the observer to.
     */
    private void setDistillabilityObserver(final Tab tabToObserve) {
        mDistillabilityObserver =
                (tab, isDistillable, isLast, isMobileOptimized) -> {
                    // Make sure the page didn't navigate while waiting for a response.
                    if (!tab.getUrl().equals(mDistillerUrl)) return;
                    // Make sure the page distillation status hasn't already been determined.
                    if (mIsCurrentPageDistillationStatusDetermined) return;
                    // Make sure that reader mode messages infra should be used.
                    if (!shouldUseReaderModeMessages(tab)) return;

                    Pair<Boolean, Integer> result =
                            ReaderModeManager.computeDistillationStatus(
                                    tab, isDistillable, isMobileOptimized, isLast);
                    mIsCurrentPageDistillationStatusDetermined = result.first;
                    mDistillationStatus = result.second;
                    if (mIsCurrentPageDistillationStatusDetermined) {
                        mHasBeenNotifiedOfCpa = false;
                        mIsReaderModeButtonShowingOnToolbar = false;
                        tryShowingPrompt();
                    }
                };
        TabDistillabilityProvider.get(tabToObserve).addObserver(mDistillabilityObserver);
    }

    /**
     * Returns whether reader mode should trigger through messages. This happens for CCTs and
     * incognito tabs.
     *
     * @param tab The tab where Reader Mode is active.
     * @return Whether reader mode should trigger through messages.
     */
    public static boolean shouldUseReaderModeMessages(Tab tab) {
        return tab != null && (tab.isCustomTab() || tab.isIncognito());
    }

    /**
     * Gets the distillation status for the given arguments, and records metrics if distillability
     * has been fully determined.
     *
     * @param tab The {@link Tab} to determine distillability for.
     * @param isDistillable Whether the tab is considered distillable.
     * @param isMobileOptimized Whether the tab is considered optimized for mobile.
     * @param isLast Whether this is the last signal we'll get for the tab.
     * @returns A pair which contains: pair.first - Whether distillability has been fully
     *     determined. pair.second - The current distillation status.
     */
    public static Pair<Boolean, Integer> computeDistillationStatus(
            Tab tab, boolean isDistillable, boolean isMobileOptimized, boolean isLast) {
        // Compute if mobile friendly pages should be excluded for use in distillation status as
        // well as metrics recording.
        boolean shouldExcludeMobileFriendly = DomDistillerTabUtils.shouldExcludeMobileFriendly(tab);
        boolean excludeCurrentMobilePage = isMobileOptimized && shouldExcludeMobileFriendly;
        // Test if the user is requesting the desktop site. Ignore this if distiller is set to
        // ALWAYS_TRUE.
        // TODO(crbug.com/405186704): Add histogram when RDS results in a RM exclusion.
        boolean excludeRequestDesktopSite =
                tab.getWebContents() != null
                        && tab.getWebContents().getNavigationController().getUseDesktopUserAgent()
                        && !DomDistillerTabUtils.isHeuristicAlwaysTrue();

        // Determine and store distillation status.
        @DistillationStatus int distillationStatus;
        if (isDistillable && !excludeCurrentMobilePage && !excludeRequestDesktopSite) {
            distillationStatus = DistillationStatus.POSSIBLE;
        } else {
            distillationStatus = DistillationStatus.NOT_POSSIBLE;
        }

        // If we get a positive distillation status, or a signal that this is the last distillation
        // signal we'll receive, record metrics and inform the user.
        if (distillationStatus == DistillationStatus.POSSIBLE || isLast) {
            RecordHistogram.recordBooleanHistogram(
                    ACCESSIBILITY_SETTING_HISTOGRAM,
                    DomDistillerTabUtils.isReaderModeAccessibilitySettingEnabled(tab.getProfile()));
            recordDistillationResult(
                    tab,
                    distillationStatus,
                    isDistillable,
                    excludeCurrentMobilePage,
                    excludeRequestDesktopSite);
            return new Pair<>(true, distillationStatus);
        }
        return new Pair<>(false, distillationStatus);
    }

    private int urlToHash(GURL url) {
        return url.getHost().hashCode();
    }

    @VisibleForTesting
    int getDistillationStatus() {
        return mDistillationStatus;
    }

    void muteSiteForTesting(GURL url) {
        sMutedSites.add(urlToHash(url));
    }

    void clearSavedSitesForTesting() {
        sMutedSites.clear();
    }

    /** @return Whether Reader mode and its new UI are enabled. */
    public static boolean isEnabled() {
        boolean enabled =
                CommandLine.getInstance().hasSwitch(ChromeSwitches.ENABLE_DOM_DISTILLER)
                        && !CommandLine.getInstance()
                                .hasSwitch(ChromeSwitches.DISABLE_READER_MODE_BOTTOM_BAR)
                        && DomDistillerTabUtils.isDistillerHeuristicsEnabled();
        return enabled;
    }

    /**
     * Determine if Reader Mode created the intent for a tab being created.
     * @param intent The Intent creating a new tab.
     * @return True whether the intent was created by Reader Mode.
     */
    public static boolean isReaderModeCreatedIntent(@NonNull Intent intent) {
        int readerParentId =
                IntentUtils.safeGetInt(
                        intent.getExtras(),
                        ReaderModeManager.EXTRA_READER_MODE_PARENT,
                        Tab.INVALID_TAB_ID);
        return readerParentId != Tab.INVALID_TAB_ID;
    }

    /**
     * Notify that a contextual page action was shown for the current tab and URL. Used when the
     * contextual page action UI is enabled to update the rate limiting logic and to suppress the
     * message prompt if the current tab is a CCT.
     *
     * @param isReaderMode Whether the reader mode UI is the current CPA being shown.
     */
    public void onContextualPageActionShown(boolean isReaderMode) {
        // If the feature is enabled and the tab is a custom tab, the manager should be aware if the
        // displayed contextual page action is the reader one. Once determined, #tryShowingPrompt
        // can successfully decide between showing a message prompt or suppressing it in favor of
        // the contextual page action's UI.
        if (ChromeFeatureList.sCctAdaptiveButton.isEnabled() && mTab.isCustomTab()) {
            mHasBeenNotifiedOfCpa = true;
            mIsReaderModeButtonShowingOnToolbar = isReaderMode;
            tryShowingPrompt();
        }
        // Contextual page actions can't be dismissed, so we consider an unused button as
        // "dismissed". Interacting with the button will undo this "mute" logic.
        if (isReaderMode) {
            addUrlToMutedSites(mDistillerUrl);
            mMessageShown = true;
        }
    }

    // Describes the end-state of the distillation result, used for metrics reporting. Do not
    // change/reorder existing entries, and keep in sync with accessibility/histograms.xml.
    // LINT.IfChange(DistillationResult)
    @IntDef({
        DistillationResult.NOT_DISTILLABLE,
        DistillationResult.DISTILLABLE,
        DistillationResult.DISTILLABLE_BUT_EXCLUDED_UNKNOWN,
        DistillationResult.DISTILLABLE_BUT_EXCLUDED_MOBILE,
        DistillationResult.DISTILLABLE_BUT_EXCLUDED_RDS,
        DistillationResult.MAX
    })
    @Retention(RetentionPolicy.SOURCE)
    @VisibleForTesting
    @interface DistillationResult {
        // Native signals that the page isn't distillable.
        int NOT_DISTILLABLE = 0;

        // Determined to distillability.
        int DISTILLABLE = 1;

        // Distillable, but excluded for an unknown reason.
        int DISTILLABLE_BUT_EXCLUDED_UNKNOWN = 2;

        // Distillable, but excluded because the web page is mobile friendly.
        int DISTILLABLE_BUT_EXCLUDED_MOBILE = 3;

        // Distillable, but excluded because the user is requesting the desktop version.
        int DISTILLABLE_BUT_EXCLUDED_RDS = 4;

        int MAX = 5;
    }

    // LINT.ThenChange(/tools/metrics/histograms/metadata/accessibility/enums.xml:DistillationResult)

    private static void recordDistillationResult(
            Tab tab,
            @DistillationStatus int status,
            boolean isDistillable,
            boolean excludeMobileFriendly,
            boolean excludeRequestDesktopSite) {
        @DistillationResult int result;
        if (status == DistillationStatus.POSSIBLE) {
            result = DistillationResult.DISTILLABLE;
        } else {
            if (isDistillable) {
                if (excludeMobileFriendly) {
                    result = DistillationResult.DISTILLABLE_BUT_EXCLUDED_MOBILE;
                } else if (excludeRequestDesktopSite) {
                    result = DistillationResult.DISTILLABLE_BUT_EXCLUDED_RDS;
                } else {
                    result = DistillationResult.DISTILLABLE_BUT_EXCLUDED_UNKNOWN;
                }
            } else {
                result = DistillationResult.NOT_DISTILLABLE;
            }
        }
        RecordHistogram.recordEnumeratedHistogram(
                PAGE_DISTILLATION_RESULT_HISTOGRAM, result, DistillationResult.MAX);
        if (tab.getWebContents() != null) {
            new UkmRecorder(tab.getWebContents(), "DomDistiller.Android.DistillabilityResult")
                    .addMetric("Result", result)
                    .record();
        }
    }
}
