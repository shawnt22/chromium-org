// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.top;

import android.content.Context;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnLongClickListener;
import android.widget.ImageButton;

import androidx.annotation.ColorInt;

import org.chromium.base.Callback;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneShotCallback;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.browser_controls.BrowserControlsVisibilityManager;
import org.chromium.chrome.browser.browser_controls.BrowserStateBrowserControlsVisibilityDelegate;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.layouts.LayoutManager;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.omnibox.LocationBar;
import org.chromium.chrome.browser.omnibox.OmniboxFocusReason;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabObscuringHandler;
import org.chromium.chrome.browser.tabmodel.IncognitoStateProvider;
import org.chromium.chrome.browser.tabmodel.IncognitoStateProvider.IncognitoStateObserver;
import org.chromium.chrome.browser.theme.ThemeColorProvider;
import org.chromium.chrome.browser.theme.TopUiThemeColorProvider;
import org.chromium.chrome.browser.toolbar.R;
import org.chromium.chrome.browser.toolbar.ToolbarDataProvider;
import org.chromium.chrome.browser.toolbar.ToolbarProgressBar;
import org.chromium.chrome.browser.toolbar.ToolbarTabController;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarButtonVariant;
import org.chromium.chrome.browser.toolbar.back_button.BackButtonCoordinator;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButton;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButtonCoordinator;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonDataProvider;
import org.chromium.chrome.browser.toolbar.reload_button.ReloadButtonCoordinator;
import org.chromium.chrome.browser.toolbar.top.NavigationPopup.HistoryDelegate;
import org.chromium.chrome.browser.toolbar.top.tab_strip.TabStripTransitionCoordinator;
import org.chromium.chrome.browser.toolbar.top.tab_strip.TabStripTransitionCoordinator.TabStripHeightObserver;
import org.chromium.chrome.browser.toolbar.top.tab_strip.TabStripTransitionCoordinator.TabStripTransitionDelegate;
import org.chromium.chrome.browser.ui.appmenu.AppMenuButtonHelper;
import org.chromium.chrome.browser.user_education.UserEducationHelper;
import org.chromium.components.browser_ui.desktop_windowing.DesktopWindowStateManager;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.browser_ui.widget.ClipDrawableProgressBar.DrawingInfo;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.ui.resources.ResourceManager;
import org.chromium.ui.util.TokenHolder;

import java.util.List;

/** A coordinator for the top toolbar component. */
@NullMarked
public class TopToolbarCoordinator implements Toolbar {

    /** Observes toolbar color change. */
    public interface ToolbarColorObserver {
        /**
         * @param color The toolbar color.
         */
        void onToolbarColorChanged(@ColorInt int color);
    }

    private final ToolbarLayout mToolbarLayout;
    private final View mLocationBarView;
    private final ObservableSupplierImpl<Tracker> mTrackerSupplier;

    private OptionalBrowsingModeButtonController mOptionalButtonController;

    private final MenuButtonCoordinator mMenuButtonCoordinator;
    private @Nullable ReloadButtonCoordinator mReloadButtonCoordinator;
    private @Nullable final BackButtonCoordinator mBackButtonCoordinator;
    private @Nullable ObservableSupplier<AppMenuButtonHelper> mAppMenuButtonHelperSupplier;

    /** Null until {@link #initializeWithNative} is called. */
    private @Nullable TabStripTransitionCoordinator mTabStripTransitionCoordinator;

    private ToolbarControlContainer mControlContainer;
    private final Supplier<ResourceManager> mResourceManagerSupplier;
    private @Nullable TopToolbarOverlayCoordinator mOverlayCoordinator;

    /**
     * The observer manager will receive all types of toolbar color change updates from toolbar
     * components and send the rendering toolbar color to the ToolbarColorObserver.
     */
    private final ToolbarColorObserverManager mToolbarColorObserverManager;

    private @Nullable IncognitoStateProvider mIncognitoStateProvider;
    private @Nullable IncognitoStateObserver mIncognitoStateObserver;

    private final TabObscuringHandler mTabObscuringHandler;
    private final @Nullable DesktopWindowStateManager mDesktopWindowStateManager;
    private final OneshotSupplier<TabStripTransitionDelegate> mTabStripTransitionDelegateSupplier;
    private final ObservableSupplierImpl<Boolean> mNtpLoadingSupplier;

    private ObservableSupplier<Integer> mTabCountSupplier;

    /** Token used to block the tab strip transition when find in page toolbar is showing. */
    private int mFindToolbarToken = TokenHolder.INVALID_TOKEN;

    private final int mIndexOfLocationBarInToolbar;

    /**
     * Creates a new {@link TopToolbarCoordinator}.
     *
     * @param reloadButtonCoordinator Component that encapsulates interactions with a reload button.
     *     It only presents on tablet.
     * @param controlContainer The {@link ToolbarControlContainer} for the containing activity.
     * @param toolbarLayout The {@link ToolbarLayout}.
     * @param toolbarDataProvider The provider for toolbar data.
     * @param tabController The controller that handles interactions with the tab.
     * @param userEducationHelper Helper class for showing in-product help text bubbles.
     * @param buttonDataProviders List of classes that wish to display an optional button in the
     *     browsing mode toolbar.
     * @param layoutStateProviderSupplier Supplier of the {@link LayoutStateProvider}.
     * @param normalThemeColorProvider The {@link ThemeColorProvider} for normal mode.
     * @param browsingModeMenuButtonCoordinator Root component for app menu.
     * @param appMenuButtonHelperSupplier For specific handling of the app menu button.
     * @param tabCountSupplier Supplier of {@link
     *     org.chromium.chrome.browser.toolbar.CustomTabCount}.
     * @param homepageEnabledSupplier Supplier of whether Home button is enabled.
     * @param homepageNonNtpSupplier Supplier of whether homepage is set to something other than the
     *     NTP.
     * @param resourceManagerSupplier A supplier of a resource manager for native textures.
     * @param historyDelegate Delegate used to display navigation history.
     * @param initializeWithIncognitoColors Whether the toolbar should be initialized with incognito
     *     colors.
     * @param constraintsSupplier Supplier for browser controls constraints.
     * @param compositorInMotionSupplier Whether there is an ongoing touch or gesture.
     * @param browserStateBrowserControlsVisibilityDelegate Used to keep controls locked when
     *     captures are stale and not able to be taken.
     * @param fullscreenManager Used to check whether in fullscreen.
     * @param tabObscuringHandler Delegate object handling obscuring views.
     * @param desktopWindowStateManager The {@link DesktopWindowStateManager} instance.
     * @param tabStripTransitionDelegateSupplier Supplier for the {@link
     *     TabStripTransitionDelegate}.
     * @param onLongClickListener OnLongClickListener for the toolbar.
     * @param homeButtonDisplay The {@link HomeButtonDisplay} to manage the display and behavior of
     *     home button(s). Should be null on custom tabs.
     */
    public TopToolbarCoordinator(
            ToolbarControlContainer controlContainer,
            ToolbarLayout toolbarLayout,
            ToolbarDataProvider toolbarDataProvider,
            ToolbarTabController tabController,
            UserEducationHelper userEducationHelper,
            List<ButtonDataProvider> buttonDataProviders,
            OneshotSupplier<LayoutStateProvider> layoutStateProviderSupplier,
            ThemeColorProvider normalThemeColorProvider,
            MenuButtonCoordinator browsingModeMenuButtonCoordinator,
            ObservableSupplier<AppMenuButtonHelper> appMenuButtonHelperSupplier,
            ToggleTabStackButtonCoordinator tabSwitcherButtonCoordinator,
            ObservableSupplier<Integer> tabCountSupplier,
            ObservableSupplier<Boolean> homepageEnabledSupplier,
            ObservableSupplier<Boolean> homepageNonNtpSupplier,
            Supplier<ResourceManager> resourceManagerSupplier,
            HistoryDelegate historyDelegate,
            boolean initializeWithIncognitoColors,
            ObservableSupplier<Integer> constraintsSupplier,
            ObservableSupplier<Boolean> compositorInMotionSupplier,
            BrowserStateBrowserControlsVisibilityDelegate
                    browserStateBrowserControlsVisibilityDelegate,
            FullscreenManager fullscreenManager,
            TabObscuringHandler tabObscuringHandler,
            @Nullable DesktopWindowStateManager desktopWindowStateManager,
            OneshotSupplier<TabStripTransitionDelegate> tabStripTransitionDelegateSupplier,
            @Nullable OnLongClickListener onLongClickListener,
            ToolbarProgressBar progressBar,
            ObservableSupplier<@Nullable Tab> tabSupplier,
            ObservableSupplier<Boolean> toolbarNavControlsEnabledSupplier,
            @Nullable BackButtonCoordinator backButtonCoordinator,
            @Nullable HomeButtonDisplay homeButtonDisplay) {
        mToolbarLayout = toolbarLayout;
        mMenuButtonCoordinator = browsingModeMenuButtonCoordinator;
        mControlContainer = controlContainer;
        mBackButtonCoordinator = backButtonCoordinator;
        mOptionalButtonController =
                new OptionalBrowsingModeButtonController(
                        buttonDataProviders,
                        userEducationHelper,
                        mToolbarLayout,
                        () -> toolbarDataProvider.getTab());
        mResourceManagerSupplier = resourceManagerSupplier;
        mTabCountSupplier = tabCountSupplier;
        mToolbarColorObserverManager = new ToolbarColorObserverManager();
        mToolbarLayout.setToolbarColorObserver(mToolbarColorObserverManager);
        mTabObscuringHandler = tabObscuringHandler;
        mDesktopWindowStateManager = desktopWindowStateManager;
        mTrackerSupplier = new ObservableSupplierImpl<>();
        mNtpLoadingSupplier = new ObservableSupplierImpl<>();
        mTabStripTransitionDelegateSupplier = tabStripTransitionDelegateSupplier;
        mToolbarLayout.setOnLongClickListener(onLongClickListener);
        mLocationBarView = mToolbarLayout.findViewById(R.id.location_bar);
        mIndexOfLocationBarInToolbar = mToolbarLayout.indexOfChild(mLocationBarView);

        ImageButton reloadButton = mControlContainer.findViewById(R.id.refresh_button);
        if (reloadButton != null) {
            mReloadButtonCoordinator =
                    new ReloadButtonCoordinator(
                            reloadButton,
                            ignoreCache -> {
                                var omniboxStub = getLocationBar().getOmniboxStub();
                                if (omniboxStub != null) {
                                    omniboxStub.setUrlBarFocus(
                                            false, null, OmniboxFocusReason.UNFOCUS);
                                }
                                tabController.stopOrReloadCurrentTab(ignoreCache);
                            },
                            tabSupplier,
                            mNtpLoadingSupplier,
                            toolbarNavControlsEnabledSupplier,
                            normalThemeColorProvider,
                            /* isWebApp= */ false);
        }

        controlContainer.setPostInitializationDependencies(
                this,
                toolbarLayout,
                initializeWithIncognitoColors,
                constraintsSupplier,
                toolbarDataProvider::getTab,
                compositorInMotionSupplier,
                browserStateBrowserControlsVisibilityDelegate,
                layoutStateProviderSupplier,
                fullscreenManager);
        mToolbarLayout.initialize(
                toolbarDataProvider,
                tabController,
                mMenuButtonCoordinator,
                tabSwitcherButtonCoordinator,
                historyDelegate,
                userEducationHelper,
                mTrackerSupplier,
                progressBar,
                mReloadButtonCoordinator,
                mBackButtonCoordinator,
                homeButtonDisplay);
        mToolbarLayout.setThemeColorProvider(normalThemeColorProvider);
        mAppMenuButtonHelperSupplier = appMenuButtonHelperSupplier;
        new OneShotCallback<>(mAppMenuButtonHelperSupplier, this::setAppMenuButtonHelper);
        homepageEnabledSupplier.addObserver(
                (show) -> mToolbarLayout.onHomeButtonIsEnabledUpdate(show));
        homepageNonNtpSupplier.addObserver(
                (isNonNtp) -> mToolbarLayout.onHomepageIsNonNtpUpdate(isNonNtp));
    }

    /**
     * @param appMenuButtonHelper The helper for managing menu button interactions.
     */
    public void setAppMenuButtonHelper(AppMenuButtonHelper appMenuButtonHelper) {
        mToolbarLayout.setAppMenuButtonHelper(appMenuButtonHelper);
    }

    /**
     * Initialize the coordinator with the components that have native initialization dependencies.
     *
     * <p>Calling this must occur after the native library have completely loaded.
     *
     * @param profile The primary Profile associated with this Toolbar.
     * @param layoutUpdater A {@link Runnable} used to request layout update upon scene change.
     * @param bookmarkClickHandler The click handler for the bookmarks button.
     * @param customTabsBackClickHandler The click handler for the custom tabs back button.
     * @param layoutManager A {@link LayoutManager} used to watch for scene changes.
     * @param tabSupplier Supplier of the activity tab.
     * @param browserControlsVisibilityManager {@link BrowserControlsVisibilityManager} to access
     *     browser controls offsets and visibility.
     * @param topUiThemeColorProvider {@link ThemeColorProvider} for top UI.
     * @param bottomToolbarControlsOffsetSupplier Supplier of the offset, relative to the bottom of
     *     the viewport, of the bottom-anchored toolbar.
     * @param suppressToolbarSceneLayerSupplier Supplier for whether suppress the update to the
     *     toolbar scene layer.
     * @param progressInfoCallback Callback when progress bar DrawingInfo has an update.
     * @param captureResourceIdSupplier Provides an id for the captured resource shown by the
     *     compositor.
     */
    public void initializeWithNative(
            Profile profile,
            Runnable layoutUpdater,
            OnClickListener bookmarkClickHandler,
            OnClickListener customTabsBackClickHandler,
            LayoutManager layoutManager,
            ObservableSupplier<@Nullable Tab> tabSupplier,
            BrowserControlsVisibilityManager browserControlsVisibilityManager,
            TopUiThemeColorProvider topUiThemeColorProvider,
            ObservableSupplier<Integer> bottomToolbarControlsOffsetSupplier,
            ObservableSupplier<Boolean> suppressToolbarSceneLayerSupplier,
            Callback<DrawingInfo> progressInfoCallback,
            ObservableSupplier<Long> captureResourceIdSupplier) {
        mTrackerSupplier.set(TrackerFactory.getTrackerForProfile(profile));
        mToolbarLayout.setTabCountSupplier(mTabCountSupplier);
        getLocationBar().updateVisualsForState();
        mToolbarLayout.setBookmarkClickHandler(bookmarkClickHandler);
        mToolbarLayout.setCustomTabCloseClickHandler(customTabsBackClickHandler);
        mToolbarLayout.setLayoutUpdater(layoutUpdater);

        mToolbarLayout.onNativeLibraryReady();

        // If fullscreen is disabled, don't bother creating this overlay; only the android view will
        // ever be shown.
        if (DeviceClassManager.enableFullscreen()) {
            mOverlayCoordinator =
                    new TopToolbarOverlayCoordinator(
                            mToolbarLayout.getContext(),
                            layoutManager,
                            progressInfoCallback,
                            tabSupplier,
                            browserControlsVisibilityManager,
                            mResourceManagerSupplier,
                            topUiThemeColorProvider,
                            bottomToolbarControlsOffsetSupplier,
                            suppressToolbarSceneLayerSupplier,
                            LayoutType.BROWSING
                                    | LayoutType.SIMPLE_ANIMATION
                                    | LayoutType.TAB_SWITCHER,
                            /* isVisibilityManuallyControlled= */ false,
                            captureResourceIdSupplier);
            layoutManager.addSceneOverlay(mOverlayCoordinator);
            mToolbarLayout.setOverlayCoordinator(mOverlayCoordinator);
        }

        int tabStripHeightResource = mToolbarLayout.getTabStripHeightFromResource();

        mTabStripTransitionCoordinator =
                new TabStripTransitionCoordinator(
                        browserControlsVisibilityManager,
                        mControlContainer,
                        mToolbarLayout,
                        tabStripHeightResource,
                        mTabObscuringHandler,
                        mDesktopWindowStateManager,
                        mTabStripTransitionDelegateSupplier);
        mToolbarLayout.getContext().registerComponentCallbacks(mTabStripTransitionCoordinator);
        mToolbarLayout.setTabStripTransitionCoordinator(mTabStripTransitionCoordinator);
    }

    /** Returns the color of the hairline drawn underneath the toolbar. */
    public @ColorInt int getToolbarHairlineColor() {
        return mToolbarLayout.getToolbarHairlineColor();
    }

    /**
     * @param toolbarColorObserver The observer that observes toolbar color change.
     */
    public void setToolbarColorObserver(ToolbarColorObserver toolbarColorObserver) {
        mToolbarColorObserverManager.setToolbarColorObserver(toolbarColorObserver);
    }

    /**
     * @see View#addOnAttachStateChangeListener(View.OnAttachStateChangeListener)
     */
    public void addOnAttachStateChangeListener(View.OnAttachStateChangeListener listener) {
        mToolbarLayout.addOnAttachStateChangeListener(listener);
    }

    /**
     * @see View#removeOnAttachStateChangeListener(View.OnAttachStateChangeListener)
     */
    public void removeOnAttachStateChangeListener(View.OnAttachStateChangeListener listener) {
        mToolbarLayout.removeOnAttachStateChangeListener(listener);
    }

    /** Add an observer that listens to tab strip height update. */
    public void addTabStripHeightObserver(TabStripHeightObserver observer) {
        if (mTabStripTransitionCoordinator == null) return;
        mTabStripTransitionCoordinator.addObserver(observer);
    }

    /** Remove the observer that listens to tab strip height update. */
    public void removeTabStripHeightObserver(TabStripHeightObserver observer) {
        if (mTabStripTransitionCoordinator == null) return;
        mTabStripTransitionCoordinator.removeObserver(observer);
    }

    /** Cleans up any code as necessary. */
    @SuppressWarnings("NullAway")
    public void destroy() {
        if (mOverlayCoordinator != null) {
            mOverlayCoordinator.destroy();
            mOverlayCoordinator = null;
        }
        mToolbarLayout.destroy();

        if (mOptionalButtonController != null) {
            mOptionalButtonController.destroy();
            mOptionalButtonController = null;
        }

        if (mReloadButtonCoordinator != null) {
            mReloadButtonCoordinator.destroy();
            mReloadButtonCoordinator = null;
        }

        if (mAppMenuButtonHelperSupplier != null) {
            mAppMenuButtonHelperSupplier = null;
        }
        if (mTabCountSupplier != null) {
            mTabCountSupplier = null;
        }
        if (mControlContainer != null) {
            mControlContainer = null;
        }
        if (mTabStripTransitionCoordinator != null) {
            mToolbarLayout
                    .getContext()
                    .unregisterComponentCallbacks(mTabStripTransitionCoordinator);
            mTabStripTransitionCoordinator.destroy();
            mTabStripTransitionCoordinator = null;
        }
        cleanUpIncognitoStateObserver();
    }

    /**
     * Notifies whether the progress bar is being drawn by WebContents for back forward transition
     * UI.
     */
    public void setShowingProgressBarForBackForwardTransition(
            boolean showingProgressBarForBackForwardTransition) {
        mToolbarLayout.setShowingProgressBarForBackForwardTransition(
                showingProgressBarForBackForwardTransition);
    }

    @Override
    public void disableMenuButton() {
        mMenuButtonCoordinator.disableMenuButton();
        mToolbarLayout.onMenuButtonDisabled();
    }

    /**
     * @return The wrapper for the browsing mode toolbar's menu button.
     */
    public @Nullable MenuButton getMenuButtonWrapper() {
        return mMenuButtonCoordinator.getMenuButton();
    }

    @Override
    public ToolbarProgressBar getProgressBar() {
        return mToolbarLayout.getProgressBar();
    }

    @Override
    public int getPrimaryColor() {
        return mToolbarLayout.getToolbarDataProvider().getPrimaryColor();
    }

    @Override
    public void getPositionRelativeToContainer(View containerView, int[] position) {
        mToolbarLayout.getPositionRelativeToContainer(containerView, position);
    }

    /**
     * Gives inheriting classes the chance to respond to
     * {@link FindToolbar} state changes.
     * @param showing Whether or not the {@code FindToolbar} will be showing.
     */
    public void handleFindLocationBarStateChange(boolean showing) {
        mToolbarLayout.handleFindLocationBarStateChange(showing);
        if (mTabStripTransitionCoordinator != null) {
            if (showing) {
                mFindToolbarToken =
                        mTabStripTransitionCoordinator.requestDeferTabStripTransitionToken();
            } else {
                mTabStripTransitionCoordinator.releaseTabStripToken(mFindToolbarToken);
                mFindToolbarToken = TokenHolder.INVALID_TOKEN;
            }
        }
    }

    /** Sets whether the urlbar should be hidden on first page load. */
    public void setUrlBarHidden(boolean hidden) {
        mToolbarLayout.setUrlBarHidden(hidden);
    }

    /** Tells the Toolbar to update what buttons it is currently displaying. */
    public void updateButtonVisibility() {
        mToolbarLayout.updateButtonVisibility();
        if (mOptionalButtonController != null) {
            mOptionalButtonController.updateButtonVisibility();
        }
    }

    /**
     * Gets the {@link AdaptiveToolbarButtonVariant} of the currently shown optional button. {@code
     * AdaptiveToolbarButtonVariant.NONE} is returned if there's no visible optional button.
     *
     * @return A value from {@link AdaptiveToolbarButtonVariant}.
     */
    public @AdaptiveToolbarButtonVariant int getCurrentOptionalButtonVariant() {
        return mOptionalButtonController.getCurrentButtonVariant();
    }

    /**
     * Sets the delegate for the optional button.
     *
     * @param delegate The {@link OptionalBrowsingModeButtonController.Delegate}.
     */
    public void setOptionalButtonDelegate(OptionalBrowsingModeButtonController.Delegate delegate) {
        mOptionalButtonController.setDelegate(delegate);
    }

    /**
     * Gives inheriting classes the chance to update the visibility of the forward button.
     *
     * @param canGoForward Whether or not the current tab has any history to go forward to.
     */
    public void updateForwardButtonVisibility(boolean canGoForward) {
        mToolbarLayout.updateForwardButtonVisibility(canGoForward);
    }

    @Override
    public void updateReloadButtonVisibility(boolean isReloading) {
        mNtpLoadingSupplier.set(isReloading);
    }

    /**
     * Gives inheriting classes the chance to update the visual status of the
     * bookmark button.
     * @param isBookmarked Whether or not the current tab is already bookmarked.
     * @param editingAllowed Whether or not bookmarks can be modified (added, edited, or removed).
     */
    public void updateBookmarkButton(boolean isBookmarked, boolean editingAllowed) {
        mToolbarLayout.updateBookmarkButton(isBookmarked, editingAllowed);
    }

    /**
     * Triggered when the current tab or model has changed.
     *
     * <p>As there are cases where you can select a model with no tabs (i.e. having incognito tabs
     * but no normal tabs will still allow you to select the normal model), this should not
     * guarantee that the model's current tab is non-null.
     */
    public void onTabOrModelChanged() {
        mToolbarLayout.onTabOrModelChanged();
        mControlContainer.onTabOrModelChanged(mToolbarLayout.isIncognito());
    }

    /**
     * For extending classes to override and carry out the changes related with the primary color
     * for the current tab changing.
     */
    public void onPrimaryColorChanged(boolean shouldAnimate) {
        mToolbarLayout.onPrimaryColorChanged(shouldAnimate);
    }

    /**
     * Sets whether a title should be shown within the Toolbar.
     *
     * @param showTitle Whether a title should be shown.
     */
    public void setShowTitle(boolean showTitle) {
        getLocationBar().setShowTitle(showTitle);
    }

    /**
     * Sets the icon drawable that the close button in the toolbar (if any) should show, or hides
     * it if {@code drawable} is {@code null}.
     */
    public void setCloseButtonImageResource(@Nullable Drawable drawable) {
        mToolbarLayout.setCloseButtonImageResource(drawable);
    }

    /**
     * Sets custom actions visibility of the custom tab toolbar.
     *
     * @param isVisible true if should be visible, false if should be hidden.
     */
    public void setCustomActionsVisibility(boolean isVisible) {
        mToolbarLayout.setCustomActionsVisibility(isVisible);
    }

    /**
     * Adds a custom action button to the toolbar layout, if it is supported.
     *
     * @param drawable The icon for the button.
     * @param description The content description for the button.
     * @param listener The {@link View.OnClickListener} to use for clicks to the button.
     * @param {@link ButtonType} of the button.
     */
    public void addCustomActionButton(
            Drawable drawable, String description, View.OnClickListener listener, int type) {
        mToolbarLayout.addCustomActionButton(drawable, description, listener, type);
    }

    /**
     * Updates the visual appearance of a custom action button in the toolbar layout,
     * if it is supported.
     * @param index The index of the button.
     * @param drawable The icon for the button.
     * @param description The content description for the button.
     */
    public void updateCustomActionButton(int index, Drawable drawable, String description) {
        mToolbarLayout.updateCustomActionButton(index, drawable, description);
    }

    @Override
    public int getTabStripHeight() {
        if (mTabStripTransitionCoordinator != null) {
            return mTabStripTransitionCoordinator.getTabStripHeight();
        }
        return mToolbarLayout.getTabStripHeightFromResource();
    }

    /** Triggered when the content view for the specified tab has changed. */
    public void onTabContentViewChanged() {
        mToolbarLayout.onTabContentViewChanged();
    }

    /** Triggered when the page of the specified tab had painted something non-empty. */
    public void onDidFirstVisuallyNonEmptyPaint() {
        mToolbarLayout.onDidFirstVisuallyNonEmptyPaint();
    }

    @Override
    public CaptureReadinessResult isReadyForTextureCapture() {
        return mToolbarLayout.isReadyForTextureCapture();
    }

    /**
     * @param attached Whether or not the web content is attached to the view heirarchy.
     */
    public void setContentAttached(boolean attached) {
        mToolbarLayout.setContentAttached(attached);
    }

    /**
     * Gives inheriting classes the chance to show or hide the TabSwitcher mode of this toolbar.
     *
     * @param inTabSwitcherMode Whether or not TabSwitcher mode should be shown or hidden.
     */
    public void setTabSwitcherMode(boolean inTabSwitcherMode) {
        mToolbarLayout.setTabSwitcherMode(inTabSwitcherMode);
    }

    /**
     * Gives inheriting classes the chance to update their state when the TabSwitcher transition has
     * finished.
     */
    public void onTabSwitcherTransitionFinished() {
        mToolbarLayout.onTabSwitcherTransitionFinished();
    }

    /**
     * @param provider The provider used to determine incognito state.
     * @param overviewColorSupplier Optional override for toolbar color, otherwise it is derived
     *     from incognito state.
     */
    public void setIncognitoStateProvider(
            IncognitoStateProvider provider,
            @Nullable ObservableSupplier<Integer> overviewColorSupplier) {
        if (overviewColorSupplier == null) {
            assert mToolbarLayout != null;
            cleanUpIncognitoStateObserver();
            ObservableSupplierImpl<Integer> supplierImpl = new ObservableSupplierImpl<>();
            Context context = mToolbarLayout.getContext();
            mIncognitoStateObserver =
                    (boolean isIncognito) -> {
                        @ColorInt
                        int color = ChromeColors.getPrimaryBackgroundColor(context, isIncognito);
                        supplierImpl.set(color);
                    };
            mIncognitoStateProvider = provider;
            provider.addIncognitoStateObserverAndTrigger(mIncognitoStateObserver);
        }
    }

    /**
     * Gives inheriting classes the chance to update themselves based on default search engine
     * changes.
     */
    public void onDefaultSearchEngineChanged() {
        mToolbarLayout.onDefaultSearchEngineChanged();
    }

    @Override
    public void getLocationBarContentRect(Rect outRect) {
        mToolbarLayout.getLocationBarContentRect(outRect);
    }

    @Override
    public void setTextureCaptureMode(boolean textureMode) {
        mToolbarLayout.setTextureCaptureMode(textureMode);
    }

    @Override
    public boolean shouldIgnoreSwipeGesture() {
        return mToolbarLayout.shouldIgnoreSwipeGesture();
    }

    /**
     * Triggered when the URL input field has gained or lost focus.
     * @param hasFocus Whether the URL field has gained focus.
     */
    public void onUrlFocusChange(boolean hasFocus) {
        mToolbarLayout.onUrlFocusChange(hasFocus);
        if (mTabStripTransitionCoordinator != null) {
            mTabStripTransitionCoordinator.onUrlFocusChange(hasFocus);
        }
    }

    public void onUrlAnimationFinished(boolean hasFocus) {
        if (mTabStripTransitionCoordinator != null) {
            mTabStripTransitionCoordinator.onUrlAnimationFinished(hasFocus);
        }
    }

    /** Notified when a navigation to a different page has occurred. */
    public void onNavigatedToDifferentPage() {
        mToolbarLayout.onNavigatedToDifferentPage();
    }

    public void onPageLoadStopped() {
        mControlContainer.onPageLoadStopped();
    }

    /** Finish any toolbar animations. */
    public void finishAnimations() {
        mToolbarLayout.finishAnimations();
    }

    /**
     * @return {@link LocationBar} object this {@link ToolbarLayout} contains.
     */
    public LocationBar getLocationBar() {
        return mToolbarLayout.getLocationBar();
    }

    private void cleanUpIncognitoStateObserver() {
        if (mIncognitoStateProvider != null && mIncognitoStateObserver != null) {
            mIncognitoStateProvider.removeObserver(mIncognitoStateObserver);
            mIncognitoStateProvider = null;
            mIncognitoStateObserver = null;
        }
    }

    @Override
    public int getHeight() {
        return mToolbarLayout.getHeight();
    }

    /** Returns the {@link OptionalBrowsingModeButtonController}. */
    public @Nullable OptionalBrowsingModeButtonController getOptionalButtonControllerForTesting() {
        return mOptionalButtonController;
    }

    /** Returns the {@link ToolbarLayout} that constitutes the toolbar. */
    public ToolbarLayout getToolbarLayoutForTesting() {
        return mToolbarLayout;
    }

    /** Returns the {@link TabStripTransitionCoordinator}. */
    public @Nullable TabStripTransitionCoordinator getTabStripTransitionCoordinator() {
        return mTabStripTransitionCoordinator;
    }

    @Override
    public void setBrowsingModeHairlineVisibility(boolean isVisible) {
        mToolbarLayout.setHairlineVisibility(isVisible);
    }

    @Override
    public boolean isBrowsingModeToolbarVisible() {
        return mToolbarLayout.getVisibility() == View.VISIBLE;
    }

    @Override
    public View removeLocationBarView() {
        assert mToolbarLayout instanceof ToolbarPhone
                : "Location bar removal logic is only supported on phones";
        mToolbarLayout.removeView(mLocationBarView);
        return mLocationBarView;
    }

    @Override
    public void restoreLocationBarView() {
        assert mToolbarLayout instanceof ToolbarPhone
                : "Location bar restore logic is only supported on phones";
        mToolbarLayout.addView(mLocationBarView, mIndexOfLocationBarInToolbar);
    }

    public void onTransitionStart() {
        mToolbarLayout.onTransitionStart();
    }

    public void onTransitionEnd() {
        mToolbarLayout.onTransitionEnd();
    }

    /** Requests keyboard focus on the toolbar row. */
    public void requestFocus() {
        mToolbarLayout.requestKeyboardFocus();
    }

    /** Returns true if the toolbar contains keyboard focus. */
    public boolean containsKeyboardFocus() {
        return mToolbarLayout.getFocusedChild() != null;
    }
}
