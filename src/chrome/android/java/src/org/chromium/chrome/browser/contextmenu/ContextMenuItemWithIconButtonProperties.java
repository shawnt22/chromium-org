// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextmenu;

import android.graphics.drawable.Drawable;
import android.view.View;

import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.listmenu.ListMenuItemProperties;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableIntPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableObjectPropertyKey;

@NullMarked
class ContextMenuItemWithIconButtonProperties extends ListMenuItemProperties {
    public static final WritableObjectPropertyKey<Drawable> BUTTON_IMAGE =
            new WritableObjectPropertyKey<>();
    public static final WritableObjectPropertyKey<CharSequence> BUTTON_CONTENT_DESC =
            new WritableObjectPropertyKey<>();
    public static final WritableIntPropertyKey BUTTON_MENU_ID = new WritableIntPropertyKey();
    public static final WritableObjectPropertyKey<View.OnClickListener> BUTTON_CLICK_LISTENER =
            new WritableObjectPropertyKey<>();

    public static final PropertyKey[] ALL_KEYS =
            new PropertyKey[] {
                MENU_ITEM_ID,
                TITLE,
                ENABLED,
                BUTTON_IMAGE,
                BUTTON_CONTENT_DESC,
                BUTTON_MENU_ID,
                BUTTON_CLICK_LISTENER,
                HOVER_LISTENER,
            };
}
