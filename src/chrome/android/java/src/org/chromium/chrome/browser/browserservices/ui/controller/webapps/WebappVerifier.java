// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices.ui.controller.webapps;

import org.chromium.base.Promise;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.browserservices.ui.controller.Verifier;

/**
 * Contains common implementation between {@link AddToHomescreenVerifier} and
 * {@link WebApkVerifier}.
 */
@NullMarked
public abstract class WebappVerifier implements Verifier {
    @Override
    public final Promise<Boolean> verify(String url) {
        return Promise.fulfilled(isUrlInScope(url));
    }

    @Override
    public final boolean wasPreviouslyVerified(String url) {
        return isUrlInScope(url);
    }

    @Override
    public final @Nullable String getVerifiedScope(String url) {
        if (isUrlInScope(url)) return getScope();
        return url;
    }

    @Override
    public boolean shouldIgnoreExternalIntentHandlers(String url) {
        return isUrlInScope(url);
    }

    /** Returns the scope that the homscreen shortcut/WebAPK is valid for. */
    protected abstract @Nullable String getScope();

    /**
     * @return {@code true} if given {@code url} is in scope of the webapp.
     */
    protected abstract boolean isUrlInScope(String url);
}
