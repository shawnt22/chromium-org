// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.appmenu;

import android.os.Bundle;
import android.view.View;

import androidx.annotation.DrawableRes;
import androidx.annotation.IdRes;
import androidx.annotation.Nullable;
import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.base.ContextUtils;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.chrome.browser.ui.appmenu.AppMenuHandler.AppMenuItemType;
import org.chromium.chrome.browser.ui.appmenu.test.R;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyModel;

class TestAppMenuPropertiesDelegate implements AppMenuPropertiesDelegate {
    public final CallbackHelper menuDismissedCallback = new CallbackHelper();
    public final CallbackHelper footerInflatedCallback = new CallbackHelper();
    public final CallbackHelper headerInflatedCallback = new CallbackHelper();
    public int footerResourceId;
    public int headerResourceId;
    public boolean enableAppIconRow;
    public boolean iconBeforeItem;

    @Override
    public void destroy() {}

    @Override
    public ModelList getMenuItems() {
        ModelList modelList = new ModelList();

        modelList.add(
                new MVCListAdapter.ListItem(
                        AppMenuItemType.STANDARD,
                        buildModelForTextItem(
                                R.id.menu_item_one, "Menu Item One", true, modelList.size())));
        modelList.add(
                new MVCListAdapter.ListItem(
                        AppMenuItemType.STANDARD,
                        buildModelForTextItem(
                                R.id.menu_item_two, "Menu Item Two", false, modelList.size())));
        modelList.add(
                new MVCListAdapter.ListItem(
                        AppMenuItemType.STANDARD,
                        buildModelForTextItem(
                                R.id.menu_item_three, "Menu Item Three", true, modelList.size())));

        if (enableAppIconRow) {
            ModelList icons = new ModelList();
            icons.add(
                    new MVCListAdapter.ListItem(
                            0,
                            buildModelForIcon(
                                    R.id.icon_one,
                                    R.drawable.test_ic_arrow_forward_black_24dp,
                                    "Icon One",
                                    null,
                                    true)));
            icons.add(
                    new MVCListAdapter.ListItem(
                            0,
                            buildModelForIcon(
                                    R.id.icon_two,
                                    R.drawable.test_ic_arrow_forward_black_24dp,
                                    "Icon Two",
                                    "2",
                                    true)));
            icons.add(
                    new MVCListAdapter.ListItem(
                            0,
                            buildModelForIcon(
                                    R.id.icon_three,
                                    R.drawable.test_ic_arrow_forward_black_24dp,
                                    "Icon Three",
                                    null,
                                    false)));
            modelList.add(
                    new MVCListAdapter.ListItem(
                            AppMenuItemType.BUTTON_ROW,
                            new PropertyModel.Builder(AppMenuItemProperties.ALL_KEYS)
                                    .with(AppMenuItemProperties.MENU_ITEM_ID, R.id.icon_row_menu_id)
                                    .with(AppMenuItemProperties.ADDITIONAL_ICONS, icons)
                                    .with(AppMenuItemProperties.SUPPORT_ENTER_ANIMATION, true)
                                    .with(AppMenuItemProperties.POSITION, modelList.size())
                                    .build()));
        }
        return modelList;
    }

    private PropertyModel buildModelForTextItem(
            @IdRes int id, String title, boolean enabled, int position) {
        return new PropertyModel.Builder(AppMenuItemProperties.ALL_KEYS)
                .with(AppMenuItemProperties.MENU_ITEM_ID, id)
                .with(AppMenuItemProperties.ENABLED, enabled)
                .with(AppMenuItemProperties.TITLE, title)
                .with(AppMenuItemProperties.SUPPORT_ENTER_ANIMATION, true)
                .with(AppMenuItemProperties.POSITION, position)
                .build();
    }

    private PropertyModel buildModelForIcon(
            @IdRes int id,
            @DrawableRes int iconId,
            String title,
            @Nullable String titleCondensed,
            boolean enabled) {
        return new PropertyModel.Builder(AppMenuItemProperties.ALL_ICON_KEYS)
                .with(AppMenuItemProperties.MENU_ITEM_ID, id)
                .with(AppMenuItemProperties.ENABLED, enabled)
                .with(
                        AppMenuItemProperties.ICON,
                        AppCompatResources.getDrawable(
                                ContextUtils.getApplicationContext(), iconId))
                .with(AppMenuItemProperties.TITLE, title)
                .with(AppMenuItemProperties.TITLE_CONDENSED, titleCondensed)
                .build();
    }

    @Nullable
    @Override
    public Bundle getBundleForMenuItem(int itemId) {
        return null;
    }

    @Override
    public void loadingStateChanged(boolean isLoading) {}

    @Override
    public void onMenuDismissed() {
        menuDismissedCallback.notifyCalled();
    }

    @Override
    public int getFooterResourceId() {
        return footerResourceId;
    }

    @Override
    public int getHeaderResourceId() {
        return headerResourceId;
    }

    @Override
    public boolean shouldShowFooter(int maxMenuHeight) {
        return footerResourceId != 0;
    }

    @Override
    public boolean shouldShowHeader(int maxMenuHeight) {
        return headerResourceId != 0;
    }

    @Override
    public void onFooterViewInflated(AppMenuHandler appMenuHandler, View view) {
        footerInflatedCallback.notifyCalled();
    }

    @Override
    public void onHeaderViewInflated(AppMenuHandler appMenuHandler, View view) {
        headerInflatedCallback.notifyCalled();
    }

    @Override
    public boolean shouldShowIconBeforeItem() {
        return iconBeforeItem;
    }

    @Override
    public boolean isMenuIconAtStart() {
        return false;
    }

    @Override
    public void onMenuShown() {}
}
