// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.privacy_sandbox;

import android.content.Context;

import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.site_settings.ChromeSiteSettingsDelegate;
import org.chromium.components.browser_ui.site_settings.SiteSettingsDelegate;
import org.chromium.components.privacy_sandbox.TrackingProtectionDelegate;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.content_public.browser.BrowserContextHandle;

public class ChromeTrackingProtectionDelegate implements TrackingProtectionDelegate {
    private final Profile mProfile;
    private final TrackingProtectionSettingsBridge mTrackingProtectionSettingsBridge;

    public ChromeTrackingProtectionDelegate(Profile profile) {
        mProfile = profile;
        mTrackingProtectionSettingsBridge = new TrackingProtectionSettingsBridge(profile);
    }

    @Override
    public boolean isBlockAll3pcEnabled() {
        return UserPrefs.get(mProfile).getBoolean(Pref.BLOCK_ALL3PC_TOGGLE_ENABLED);
    }

    @Override
    public void setBlockAll3pc(boolean enabled) {
        UserPrefs.get(mProfile).setBoolean(Pref.BLOCK_ALL3PC_TOGGLE_ENABLED, enabled);
    }

    @Override
    public boolean isDoNotTrackEnabled() {
        return UserPrefs.get(mProfile).getBoolean(Pref.ENABLE_DO_NOT_TRACK);
    }

    @Override
    public void setDoNotTrack(boolean enabled) {
        UserPrefs.get(mProfile).setBoolean(Pref.ENABLE_DO_NOT_TRACK, enabled);
    }

    @Override
    public boolean isIpProtectionUxEnabled() {
        return ChromeFeatureList.isEnabled(ChromeFeatureList.IP_PROTECTION_UX);
    }

    @Override
    public boolean isIpProtectionEnabled() {
        return UserPrefs.get(mProfile).getBoolean(Pref.IP_PROTECTION_ENABLED);
    }

    @Override
    public boolean isIpProtectionManaged() {
        return UserPrefs.get(mProfile).isManagedPreference(Pref.IP_PROTECTION_ENABLED);
    }

    @Override
    public boolean isFingerprintingProtectionManaged() {
        return UserPrefs.get(mProfile).isManagedPreference(Pref.FINGERPRINTING_PROTECTION_ENABLED);
    }

    @Override
    public void setIpProtection(boolean enabled) {
        UserPrefs.get(mProfile).setBoolean(Pref.IP_PROTECTION_ENABLED, enabled);
    }

    @Override
    public boolean isIpProtectionDisabledForEnterprise() {
        return mTrackingProtectionSettingsBridge.isIpProtectionDisabledForEnterprise();
    }

    @Override
    public boolean isFingerprintingProtectionUxEnabled() {
        return ChromeFeatureList.isEnabled(ChromeFeatureList.FINGERPRINTING_PROTECTION_UX);
    }

    @Override
    public boolean isFingerprintingProtectionEnabled() {
        return UserPrefs.get(mProfile).getBoolean(Pref.FINGERPRINTING_PROTECTION_ENABLED);
    }

    @Override
    public void setFingerprintingProtection(boolean enabled) {
        UserPrefs.get(mProfile).setBoolean(Pref.FINGERPRINTING_PROTECTION_ENABLED, enabled);
    }

    @Override
    public boolean isDisplayWildcardInContentSettingsEnabled() {
        return ChromeFeatureList.isEnabled(ChromeFeatureList.DISPLAY_WILDCARD_CONTENT_SETTINGS);
    }

    @Override
    public BrowserContextHandle getBrowserContext() {
        return mProfile;
    }

    @Override
    public SiteSettingsDelegate getSiteSettingsDelegate(Context context) {
        return new ChromeSiteSettingsDelegate(context, mProfile);
    }
}
