// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.app.Activity;
import android.content.Context;
import android.os.Handler;
import android.util.Pair;
import android.view.View.OnClickListener;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.chromium.base.ContextUtils;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.bookmarks.TabBookmarker;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.data_sharing.DataSharingTabManager;
import org.chromium.chrome.browser.hub.HubManager;
import org.chromium.chrome.browser.hub.Pane;
import org.chromium.chrome.browser.hub.PaneManager;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthController;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManager;
import org.chromium.chrome.browser.multiwindow.MultiWindowModeStateDispatcher;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.tab_ui.TabContentManager;
import org.chromium.chrome.browser.tab_ui.TabModelDotInfo;
import org.chromium.chrome.browser.tab_ui.TabSwitcher;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabGroupModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tasks.tab_management.archived_tabs_auto_delete_promo.ArchivedTabsAutoDeletePromoManager;
import org.chromium.chrome.browser.theme.ThemeColorProvider;
import org.chromium.chrome.browser.ui.desktop_windowing.AppHeaderUtils;
import org.chromium.chrome.browser.ui.edge_to_edge.EdgeToEdgeController;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.undo_tab_close_snackbar.UndoBarThrottle;
import org.chromium.chrome.browser.user_education.UserEducationHelper;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.desktop_windowing.DesktopWindowStateManager;
import org.chromium.components.browser_ui.widget.scrim.ScrimManager;
import org.chromium.components.tab_group_sync.TabGroupUiActionHandler;
import org.chromium.ui.dragdrop.DragAndDropDelegate;
import org.chromium.ui.modaldialog.ModalDialogManager;

import java.util.function.DoubleConsumer;

/** Impl class that will resolve components for tab management. */
public class TabManagementDelegateImpl implements TabManagementDelegate {
    @Override
    public TabGroupUi createTabGroupUi(
            @NonNull Activity activity,
            @NonNull ViewGroup parentView,
            @NonNull BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull ScrimManager scrimManager,
            @NonNull ObservableSupplier<Boolean> omniboxFocusStateSupplier,
            @NonNull BottomSheetController bottomSheetController,
            @NonNull DataSharingTabManager dataSharingTabManager,
            TabModelSelector tabModelSelector,
            @NonNull TabContentManager tabContentManager,
            @NonNull TabCreatorManager tabCreatorManager,
            @NonNull OneshotSupplier<LayoutStateProvider> layoutStateProviderSupplier,
            @NonNull ModalDialogManager modalDialogManager,
            @NonNull ThemeColorProvider themeColorProvider,
            UndoBarThrottle undoBarThrottle,
            @NonNull ObservableSupplier<TabBookmarker> tabBookmarkerSupplier,
            @NonNull Supplier<ShareDelegate> shareDelegateSupplier) {
        return new TabGroupUiCoordinator(
                activity,
                parentView,
                browserControlsStateProvider,
                scrimManager,
                omniboxFocusStateSupplier,
                bottomSheetController,
                dataSharingTabManager,
                tabModelSelector,
                tabContentManager,
                tabCreatorManager,
                layoutStateProviderSupplier,
                modalDialogManager,
                themeColorProvider,
                undoBarThrottle,
                tabBookmarkerSupplier,
                shareDelegateSupplier);
    }

    @Override
    public Pair<TabSwitcher, Pane> createTabSwitcherPane(
            @NonNull Activity activity,
            @NonNull ActivityLifecycleDispatcher lifecycleDispatcher,
            @NonNull OneshotSupplier<ProfileProvider> profileProviderSupplier,
            @NonNull TabModelSelector tabModelSelector,
            @NonNull TabContentManager tabContentManager,
            @NonNull TabCreatorManager tabCreatorManager,
            @NonNull BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull MultiWindowModeStateDispatcher multiWindowModeStateDispatcher,
            @NonNull ScrimManager scrimManager,
            @NonNull SnackbarManager snackbarManager,
            @NonNull ModalDialogManager modalDialogManager,
            @NonNull BottomSheetController bottomSheetController,
            @NonNull DataSharingTabManager dataSharingTabManager,
            @Nullable OneshotSupplier<IncognitoReauthController> incognitoReauthControllerSupplier,
            @NonNull OnClickListener newTabButtonOnClickListener,
            boolean isIncognito,
            @NonNull DoubleConsumer onToolbarAlphaChange,
            @NonNull BackPressManager backPressManager,
            @NonNull ObservableSupplier<EdgeToEdgeController> edgeToEdgeSupplier,
            @Nullable DesktopWindowStateManager desktopWindowStateManager,
            @NonNull ObservableSupplier<TabModelDotInfo> tabModelNotificationDotSupplier,
            @NonNull ObservableSupplier<CompositorViewHolder> compositorViewHolderSupplier,
            @NonNull ObservableSupplier<ShareDelegate> shareDelegateSupplier,
            @NonNull ObservableSupplier<TabBookmarker> tabBookmarkerSupplier,
            @NonNull TabGroupCreationUiDelegate tabGroupCreationUiDelegate,
            UndoBarThrottle undoBarThrottle,
            @NonNull LazyOneshotSupplier<HubManager> hubManagerSupplier,
            @Nullable ArchivedTabsAutoDeletePromoManager archivedTabsAutoDeletePromoManager,
            @NonNull Supplier<TabGroupUiActionHandler> tabGroupUiActionHandlerSupplier,
            @NonNull Supplier<LayoutStateProvider> layoutStateProviderSupplier,
            @Nullable ObservableSupplier<Boolean> xrSpaceModeObservableSupplier,
            @Nullable MultiInstanceManager multiInstanceManager,
            @Nullable DragAndDropDelegate dragDropDelegate) {

        TabSwitcherDragHandler tabSwitcherDragHandler = null;
        if (modalDialogManager != null && dragDropDelegate != null) {
            tabSwitcherDragHandler =
                    new TabSwitcherDragHandler(
                            () -> activity,
                            multiInstanceManager,
                            dragDropDelegate,
                            () -> AppHeaderUtils.isAppInDesktopWindow(desktopWindowStateManager));
            tabSwitcherDragHandler.setTabModelSelector(tabModelSelector);
        }

        // TODO(crbug.com/40946413): Consider making this an activity scoped singleton and possibly
        // hosting it in CTA/HubProvider.
        TabSwitcherPaneCoordinatorFactory factory =
                new TabSwitcherPaneCoordinatorFactory(
                        activity,
                        lifecycleDispatcher,
                        profileProviderSupplier,
                        tabModelSelector,
                        tabContentManager,
                        tabCreatorManager,
                        browserControlsStateProvider,
                        multiWindowModeStateDispatcher,
                        scrimManager,
                        snackbarManager,
                        modalDialogManager,
                        bottomSheetController,
                        dataSharingTabManager,
                        backPressManager,
                        desktopWindowStateManager,
                        edgeToEdgeSupplier,
                        shareDelegateSupplier,
                        tabBookmarkerSupplier,
                        undoBarThrottle,
                        () -> hubManagerSupplier.get().getPaneManager(),
                        tabGroupUiActionHandlerSupplier,
                        layoutStateProviderSupplier,
                        tabSwitcherDragHandler);
        OneshotSupplierImpl<Profile> profileSupplier = new OneshotSupplierImpl<>();
        Handler handler = new Handler();
        profileProviderSupplier.onAvailable(
                (profileProvider) -> profileSupplier.set(profileProvider.getOriginalProfile()));
        UserEducationHelper userEducationHelper =
                new UserEducationHelper(activity, profileSupplier, handler);

        Supplier<TabGroupModelFilter> tabGroupModelFilterSupplier =
                () ->
                        tabModelSelector
                                .getTabGroupModelFilterProvider()
                                .getTabGroupModelFilter(isIncognito);
        TabSwitcherPaneBase pane =
                isIncognito
                        ? new IncognitoTabSwitcherPane(
                                activity,
                                factory,
                                tabGroupModelFilterSupplier,
                                newTabButtonOnClickListener,
                                incognitoReauthControllerSupplier,
                                onToolbarAlphaChange,
                                userEducationHelper,
                                edgeToEdgeSupplier,
                                compositorViewHolderSupplier,
                                tabGroupCreationUiDelegate,
                                xrSpaceModeObservableSupplier)
                        : new TabSwitcherPane(
                                activity,
                                ContextUtils.getAppSharedPreferences(),
                                profileProviderSupplier,
                                factory,
                                tabGroupModelFilterSupplier,
                                newTabButtonOnClickListener,
                                new TabSwitcherPaneDrawableCoordinator(
                                        activity,
                                        tabModelSelector,
                                        tabModelNotificationDotSupplier),
                                onToolbarAlphaChange,
                                userEducationHelper,
                                edgeToEdgeSupplier,
                                compositorViewHolderSupplier,
                                tabGroupCreationUiDelegate,
                                archivedTabsAutoDeletePromoManager,
                                xrSpaceModeObservableSupplier);
        return Pair.create(pane, pane);
    }

    @Override
    public Pane createTabGroupsPane(
            @NonNull Context context,
            @NonNull TabModelSelector tabModelSelector,
            @NonNull DoubleConsumer onToolbarAlphaChange,
            @NonNull OneshotSupplier<ProfileProvider> profileProviderSupplier,
            @NonNull LazyOneshotSupplier<HubManager> hubManagerSupplier,
            @NonNull Supplier<TabGroupUiActionHandler> tabGroupUiActionHandlerSupplier,
            @NonNull Supplier<ModalDialogManager> modalDialogManagerSupplier,
            @NonNull ObservableSupplier<EdgeToEdgeController> edgeToEdgeSupplier,
            @NonNull DataSharingTabManager dataSharingTabManager) {
        LazyOneshotSupplier<TabGroupModelFilter> tabGroupModelFilterSupplier =
                LazyOneshotSupplier.fromSupplier(
                        () ->
                                tabModelSelector
                                        .getTabGroupModelFilterProvider()
                                        .getTabGroupModelFilter(false));
        return new TabGroupsPane(
                context,
                tabGroupModelFilterSupplier,
                onToolbarAlphaChange,
                profileProviderSupplier,
                () -> hubManagerSupplier.get().getPaneManager(),
                tabGroupUiActionHandlerSupplier,
                modalDialogManagerSupplier,
                edgeToEdgeSupplier,
                dataSharingTabManager);
    }

    @Override
    public TabGroupCreationUiDelegate createTabGroupCreationUiFlow(
            @NonNull Context context,
            @NonNull ModalDialogManager modalDialogManager,
            @NonNull OneshotSupplier<HubManager> hubManagerSupplier,
            @NonNull Supplier<TabGroupModelFilter> tabGroupModelFilterSupplier) {
        ObservableSupplierImpl<PaneManager> paneManagerSupplier = new ObservableSupplierImpl<>();
        hubManagerSupplier.onAvailable(
                hubManager -> paneManagerSupplier.set(hubManager.getPaneManager()));
        return new TabGroupCreationUiDelegate(
                context,
                () -> modalDialogManager,
                paneManagerSupplier,
                tabGroupModelFilterSupplier,
                TabGroupCreationDialogManager::new);
    }
}
