// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp;

import android.text.TextUtils;

import org.chromium.build.annotations.Contract;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.url.GURL;

/** Provides functions for working with link titles. */
@NullMarked
public final class TitleUtil {
    private TitleUtil() {}

    /**
     * Returns a title suitable for display for a link. If |title| is non-empty, this simply returns
     * it. Otherwise, returns a shortened form of the URL.
     */
    @Contract("!null, _ -> !null")
    public static @Nullable String getTitleForDisplay(@Nullable String title, @Nullable GURL url) {
        if (!TextUtils.isEmpty(title) || url == null || GURL.isEmptyOrInvalid(url)) {
            return title;
        }

        String host = url.getHost();
        if (host == null) host = "";
        String path = url.getPath();
        if (path == null || path.equals("/")) path = "";
        title = host + path;
        return title;
    }
}
