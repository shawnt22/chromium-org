// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import android.content.SharedPreferences;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.ContextUtils;
import org.chromium.base.ObserverList;
import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.BuildConfig;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;

import java.util.Set;
import java.util.concurrent.TimeUnit;

/** Class to manage reading/writing preferences related to tab declutter. */
@NullMarked
public class TabArchiveSettings {
    public interface Observer {
        // Called when a setting was changed.
        void onSettingChanged();
    }

    // The default time to consider a tab as inactive.
    static final int DEFAULT_ARCHIVE_TIME_HOURS = 21 * 24; // 21 days.
    // The default time interval between same-session declutter passes.
    private static final int DEFAULT_DECLUTTER_INTERVAL_TIME_HOURS = 7 * 24; // 7 days.
    // The default allowable times we can show an IPH.
    private static final int DEFAULT_ALLOWED_IPH_SHOWS = 3;
    // The default max simultaneous archives to allow in a single pass.
    static final int DEFAULT_MAX_SIMULTANEOUS_ARCHIVES = 150;
    private static boolean sIphShownThisSession;

    /** Sets whether the iph was shown this session. */
    public static void setIphShownThisSession(boolean iphShownThisSession) {
        sIphShownThisSession = iphShownThisSession;
    }

    /** Returns whether the iph was shown this session. */
    public static boolean getIphShownThisSession() {
        return sIphShownThisSession;
    }

    @VisibleForTesting static final boolean DIALOG_IPH_DEFAULT = true;
    private static final Set<String> PREF_KEYS_FOR_NOTIFICATIONS =
            Set.of(
                    ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_ENABLED,
                    ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_TIME_DELTA_HOURS,
                    ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_ENABLED,
                    ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_TIME_DELTA_HOURS,
                    ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_DECISION_MADE);

    private final SharedPreferences.OnSharedPreferenceChangeListener mPrefsListener =
            new SharedPreferences.OnSharedPreferenceChangeListener() {
                @Override
                public void onSharedPreferenceChanged(
                        SharedPreferences sharedPrefs, @Nullable String key) {
                    PostTask.postTask(TaskTraits.UI_DEFAULT, () -> maybeNotifyObservers(key));
                }
            };

    private final SharedPreferencesManager mPrefsManager;
    private final ObserverList<Observer> mObservers = new ObserverList<>();

    /**
     * Constructor.
     *
     * @param prefsManager The {@link SharedPreferencesManager} used to read/write settings.
     */
    public TabArchiveSettings(SharedPreferencesManager prefsManager) {
        mPrefsManager = prefsManager;
        ContextUtils.getAppSharedPreferences()
                .registerOnSharedPreferenceChangeListener(mPrefsListener);
    }

    /** Destroys the object, unregistering observers. */
    public void destroy() {
        ContextUtils.getAppSharedPreferences()
                .unregisterOnSharedPreferenceChangeListener(mPrefsListener);
    }

    /** Adds the given observer to the list. */
    public void addObserver(Observer obs) {
        mObservers.addObserver(obs);
    }

    /** Removes the given observer from the list. */
    public void removeObserver(Observer obs) {
        mObservers.removeObserver(obs);
    }

    /** Returns whether archive is enabled in settings. */
    public boolean getArchiveEnabled() {
        // Turn off the archive feature by default for tests since we can't control when tabs
        // are created, and tabs disappearing from tests is very unexpected. For archive tests,
        // this will need to be turned on manually.
        return mPrefsManager.readBoolean(
                ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_ENABLED, !BuildConfig.IS_FOR_TEST);
    }

    /** Sets whether archive is enabled in settings. */
    public void setArchiveEnabled(boolean enabled) {
        mPrefsManager.writeBoolean(ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_ENABLED, enabled);
    }

    /** Returns whether the user has already seen the promo. */
    public boolean getAutoDeleteDecisionMade() {
        return mPrefsManager.readBoolean(
                ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_DECISION_MADE, false);
    }

    /** Sets whether the user has seen the promo. */
    public void setAutoDeleteDecisionMade(boolean enabled) {
        mPrefsManager.writeBoolean(
                ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_DECISION_MADE, enabled);
    }

    /** Returns the time delta used to determine if a tab is eligible for archive. */
    public int getArchiveTimeDeltaHours() {
        return mPrefsManager.readInt(
                ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_TIME_DELTA_HOURS,
                DEFAULT_ARCHIVE_TIME_HOURS);
    }

    /** Similar to above, but the return value is in days. */
    public int getArchiveTimeDeltaDays() {
        return (int) TimeUnit.HOURS.toDays(getArchiveTimeDeltaHours());
    }

    /** Sets the time delta (in hours) used to determine if a tab is eligible for archive. */
    public void setArchiveTimeDeltaHours(int timeDeltaHours) {
        mPrefsManager.writeInt(
                ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_TIME_DELTA_HOURS, timeDeltaHours);
    }

    /** Sets the time delta (in days) used to determine if a tab is eligible for archive. */
    public void setArchiveTimeDeltaDays(int timeDeltaDays) {
        setArchiveTimeDeltaHours((int) TimeUnit.DAYS.toHours(timeDeltaDays));
    }

    /** Returns whether auto-deletion of archived tabs is enabled. */
    public boolean isAutoDeleteEnabled() {
        return getArchiveEnabled()
                && ChromeFeatureList.sAndroidTabDeclutterAutoDeleteKillSwitch.isEnabled()
                && mPrefsManager.readBoolean(
                        ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_ENABLED, false);
    }

    /** Sets whether auto deletion for archived tabs is enabled in settings. */
    public void setAutoDeleteEnabled(boolean enabled) {
        mPrefsManager.writeBoolean(ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_ENABLED, enabled);
    }

    /** Returns whether archiving of duplicate tabs is enabled. */
    public boolean isArchiveDuplicateTabsEnabled() {
        return getArchiveEnabled()
                && mPrefsManager.readBoolean(
                        ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_DUPLICATE_TABS_ENABLED,
                        ChromeFeatureList.sAndroidTabDeclutterArchiveDuplicateTabs.isEnabled());
    }

    /** Sets whether archiving duplicate tabs is enabled in settings. */
    public void setArchiveDuplicateTabsEnabled(boolean enabled) {
        mPrefsManager.writeBoolean(
                ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_DUPLICATE_TABS_ENABLED, enabled);
    }

    /**
     * Returns the time delta used to determine if an archived tab is eligible for auto deletion.
     */
    public int getAutoDeleteTimeDeltaHours() {
        return mPrefsManager.readInt(
                ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_TIME_DELTA_HOURS,
                ChromeFeatureList.sAndroidTabDeclutterAutoDeleteTimeDeltaHours.getValue());
    }

    /** Similar to above, but the return value is in days. */
    public int getAutoDeleteTimeDeltaDays() {
        return (int) TimeUnit.HOURS.toDays(getAutoDeleteTimeDeltaHours());
    }

    /** Similar to above, but the return value is in months. */
    public int getAutoDeleteTimeDeltaMonths() {
        // Assuming a month to have 30 days on average.
        return getAutoDeleteTimeDeltaDays() / 30;
    }

    /** Sets the time delta used to determine if an archived tab is eligible for auto deletion. */
    public void setAutoDeleteTimeDeltaHours(int timeDeltaHours) {
        mPrefsManager.writeInt(
                ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_TIME_DELTA_HOURS, timeDeltaHours);
    }

    /** Returns the interval to perform declutter in hours. */
    public int getDeclutterIntervalTimeDeltaHours() {
        return DEFAULT_DECLUTTER_INTERVAL_TIME_HOURS;
    }

    /** Returns whether the dialog iph should be shown. */
    public boolean shouldShowDialogIph() {
        return mPrefsManager.readInt(ChromePreferenceKeys.TAB_DECLUTTER_DIALOG_IPH_DISMISS_COUNT, 0)
                < DEFAULT_ALLOWED_IPH_SHOWS;
    }

    /** Sets whether the dialog iph should be shown. */
    public void markDialogIphDismissed() {
        mPrefsManager.writeInt(
                ChromePreferenceKeys.TAB_DECLUTTER_DIALOG_IPH_DISMISS_COUNT,
                mPrefsManager.readInt(ChromePreferenceKeys.TAB_DECLUTTER_DIALOG_IPH_DISMISS_COUNT)
                        + 1);
    }

    public void setShouldShowDialogIphForTesting(boolean shouldShow) {
        mPrefsManager.writeInt(
                ChromePreferenceKeys.TAB_DECLUTTER_DIALOG_IPH_DISMISS_COUNT,
                shouldShow ? 0 : DEFAULT_ALLOWED_IPH_SHOWS);
    }

    public int getMaxSimultaneousArchives() {
        return DEFAULT_MAX_SIMULTANEOUS_ARCHIVES;
    }

    // Private methods.

    private void maybeNotifyObservers(@Nullable String key) {
        if (!PREF_KEYS_FOR_NOTIFICATIONS.contains(key)) return;

        for (Observer obs : mObservers) {
            obs.onSettingChanged();
        }
    }

    // Testing-specific methods.

    public void resetSettingsForTesting() {
        mPrefsManager.removeKey(ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_ENABLED);
        mPrefsManager.removeKey(ChromePreferenceKeys.TAB_DECLUTTER_ARCHIVE_TIME_DELTA_HOURS);
        mPrefsManager.removeKey(ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_ENABLED);
        mPrefsManager.removeKey(ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_TIME_DELTA_HOURS);
        mPrefsManager.removeKey(ChromePreferenceKeys.TAB_DECLUTTER_DIALOG_IPH_DISMISS_COUNT);
        mPrefsManager.removeKey(ChromePreferenceKeys.TAB_DECLUTTER_AUTO_DELETE_DECISION_MADE);
    }
}
