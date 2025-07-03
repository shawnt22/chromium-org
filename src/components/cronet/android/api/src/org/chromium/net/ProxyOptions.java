// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import androidx.annotation.NonNull;
import androidx.annotation.RequiresOptIn;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/** Defines a proxy configuration that can be used by Cronet. */
public final class ProxyOptions {

    /**
     * Constructs a proxy configuration out of a list of {@link Proxy}. Proxies in the list will be
     * used in order. Proxy in position n+1 will be used only if we failed to use proxy in position
     * n. A {@code null} list element represents a DIRECT connection, in which case no proxying will
     * take place. This can be used to define fail-open/fail-closed semantics: if the all of the
     * proxies specified in the list happen to be unreachable, adding (or not adding) a {@code null}
     * element at the end of the list will control whether non-proxied connections are allowed.
     *
     * @param proxyList The list of {@link Proxy} that defines this configuration.
     */
    public ProxyOptions(@NonNull List<Proxy> proxyList) {
        if (Objects.requireNonNull(proxyList).isEmpty()) {
            throw new IllegalArgumentException("ProxyList cannot be empty");
        }
        this.mProxyList = new ArrayList<>(proxyList);
    }

    /** Returns the list of proxies that are part of this proxy configuration. */
    public @NonNull List<Proxy> getProxyList() {
        return Collections.unmodifiableList(mProxyList);
    }

    /**
     * An annotation for APIs which are not considered stable yet.
     *
     * <p>Experimental APIs are subject to change, breakage, or removal at any time and may not be
     * production ready.
     *
     * <p>It's highly recommended to reach out to Cronet maintainers (<code>net-dev@chromium.org
     * </code>) before using one of the APIs annotated as experimental outside of debugging and
     * proof-of-concept code.
     *
     * <p>By using an Experimental API, applications acknowledge that they are doing so at their own
     * risk.
     */
    @RequiresOptIn
    public @interface Experimental {}

    private final @NonNull List<Proxy> mProxyList;
}
