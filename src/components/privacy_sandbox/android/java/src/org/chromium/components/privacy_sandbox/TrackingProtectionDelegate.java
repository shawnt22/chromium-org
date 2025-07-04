// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.privacy_sandbox;

import android.content.Context;

import org.chromium.build.annotations.NullMarked;
import org.chromium.components.browser_ui.site_settings.SiteSettingsDelegate;
import org.chromium.content_public.browser.BrowserContextHandle;

/** Interface implemented by the embedder to access embedder-specific logic. */
@NullMarked
public interface TrackingProtectionDelegate {
    /**
     * @return whether block all 3PC pref is enabled.
     */
    boolean isBlockAll3pcEnabled();

    /** Set the value of the block all 3PC pref. */
    void setBlockAll3pc(boolean enabled);

    /**
     * @return whether the Do Not Track pref is enabled.
     */
    boolean isDoNotTrackEnabled();

    /** Set the value of the Do Not Track Pref. */
    void setDoNotTrack(boolean enabled);

    /**
     * @return whether the IP protection UX is enabled.
     */
    boolean isIpProtectionUxEnabled();

    /**
     * @return whether the IP protection is enabled.
     */
    boolean isIpProtectionEnabled();

    /** Set the value of the IP protection state. */
    void setIpProtection(boolean enabled);

    /**
     * @return whether IP protection is disabled for users on enterprise devices.
     */
    boolean isIpProtectionDisabledForEnterprise();

    /**
     * @return whether IP protection is managed.
     */
    boolean isIpProtectionManaged();

    /**
     * @return whether fingerprinting protection is managed.
     */
    boolean isFingerprintingProtectionManaged();

    /**
     * @return whether the fingerprinting protection UX is enabled.
     */
    boolean isFingerprintingProtectionUxEnabled();

    /**
     * @return whether the fingerprinting protection is enabled.
     */
    boolean isFingerprintingProtectionEnabled();

    /**
     * @return true if wildcards should be shown in content settings patterns.
     */
    boolean isDisplayWildcardInContentSettingsEnabled();

    /** Set the value of the fingerprinting protection state. */
    void setFingerprintingProtection(boolean enabled);

    /**
     * @return the browser context associated with the settings page.
     */
    BrowserContextHandle getBrowserContext();

    /** @return the site settings delegate object. */
    SiteSettingsDelegate getSiteSettingsDelegate(Context context);
}
