// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.accessibility;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableBooleanPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableIntPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableObjectPropertyKey;

/** Properties for the page zoom feature. */
@NullMarked
class PageZoomProperties {
    static final WritableObjectPropertyKey<Callback<@Nullable Void>> DECREASE_ZOOM_CALLBACK =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<Callback<@Nullable Void>> INCREASE_ZOOM_CALLBACK =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<Callback<@Nullable Void>> RESET_ZOOM_CALLBACK =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<Callback<Integer>> SEEKBAR_CHANGE_CALLBACK =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<Callback<@Nullable Void>> USER_INTERACTION_CALLBACK =
            new WritableObjectPropertyKey<>();

    static final WritableBooleanPropertyKey DECREASE_ZOOM_ENABLED =
            new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey INCREASE_ZOOM_ENABLED =
            new WritableBooleanPropertyKey();

    static final WritableIntPropertyKey MAXIMUM_SEEK_VALUE = new WritableIntPropertyKey();
    static final WritableIntPropertyKey CURRENT_SEEK_VALUE = new WritableIntPropertyKey();

    static final WritableObjectPropertyKey<Double> DEFAULT_ZOOM_FACTOR =
            new WritableObjectPropertyKey<>();

    static final PropertyKey[] ALL_KEYS = {
        DECREASE_ZOOM_CALLBACK,
        INCREASE_ZOOM_CALLBACK,
        RESET_ZOOM_CALLBACK,
        SEEKBAR_CHANGE_CALLBACK,
        USER_INTERACTION_CALLBACK,
        DECREASE_ZOOM_ENABLED,
        INCREASE_ZOOM_ENABLED,
        MAXIMUM_SEEK_VALUE,
        CURRENT_SEEK_VALUE,
        DEFAULT_ZOOM_FACTOR
    };
}
