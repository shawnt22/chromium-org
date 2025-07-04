// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.night_mode;

import static org.chromium.chrome.browser.preferences.ChromePreferenceKeys.UI_THEME_SETTING;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.view.ContextThemeWrapper;

import androidx.annotation.StyleRes;

import org.chromium.base.ResettersForTesting;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;

import java.util.LinkedHashSet;

/** Helper methods for supporting night mode. */
@NullMarked
public class NightModeUtils {
    private static @Nullable Boolean sNightModeSupportedForTest;

    /**
     * @return Whether night mode is supported.
     */
    public static boolean isNightModeSupported() {
        if (sNightModeSupportedForTest != null) return sNightModeSupportedForTest;
        return true;
    }

    /**
     * Updates configuration for night mode to ensure night mode settings are applied properly.
     * Should be called anytime the Activity's configuration changes (e.g. from
     * {@link Activity#onConfigurationChanged(Configuration)}) if uiMode was not overridden on
     * the configuration during activity initialization
     * (see {@link #applyOverridesForNightMode(NightModeStateProvider, Configuration)}).
     * @param activity The {@link Activity} that needs to be updated.
     * @param inNightMode Whether night mode should be set on the activity.
     * @param newConfig The new {@link Configuration} from
     *                  {@link Activity#onConfigurationChanged(Configuration)}.
     * @param themeResIds An ordered set of {@link StyleRes} of the themes applied to the activity.
     */
    public static void updateConfigurationForNightMode(
            Activity activity,
            boolean inNightMode,
            Configuration newConfig,
            LinkedHashSet<Integer> themeResIds) {
        final int uiNightMode =
                inNightMode ? Configuration.UI_MODE_NIGHT_YES : Configuration.UI_MODE_NIGHT_NO;

        if (uiNightMode == (newConfig.uiMode & Configuration.UI_MODE_NIGHT_MASK)) return;

        // Rebase the theme against the new configuration, so the attributes get resolved to the
        // correct colors based on the night mode setting. See https://crbug.com/1280540.
        activity.getTheme().rebase();
    }

    /**
     * @param provider The {@link NightModeStateProvider} that provides the night mode state.
     * @param config The {@link Configuration} on which UI night mode should be overridden if
     *               necessary.
     * @return True if UI night mode is overridden on the provided {@code config}, and false
     *         otherwise.
     */
    public static boolean applyOverridesForNightMode(
            NightModeStateProvider provider, Configuration config) {
        if (!provider.shouldOverrideConfiguration()) return false;

        // Override uiMode so that UIs created by the DecorView (e.g. context menu, floating
        // action bar) get the correct theme. May check if this is needed on newer version
        // of support library. See https://crbug.com/935731.
        final int nightMode =
                provider.isInNightMode()
                        ? Configuration.UI_MODE_NIGHT_YES
                        : Configuration.UI_MODE_NIGHT_NO;
        config.uiMode = nightMode | (config.uiMode & ~Configuration.UI_MODE_NIGHT_MASK);
        return true;
    }

    /**
     * Wraps a {@link Context} into one having a resource configuration with the given night mode
     * setting.
     * @param context {@link Context} to wrap.
     * @param themeResId Theme resource to use with {@link ContextThemeWrapper}.
     * @param nightMode Whether to apply night mode.
     * @return Wrapped {@link Context}.
     */
    public static Context wrapContextWithNightModeConfig(
            Context context, @StyleRes int themeResId, boolean nightMode) {
        ContextThemeWrapper wrapper = new ContextThemeWrapper(context, themeResId);
        Configuration config = new Configuration();
        // Pre-Android O, fontScale gets initialized to 1 in the constructor. Set it to 0 so
        // that applyOverrideConfiguration() does not interpret it as an overridden value.
        config.fontScale = 0;
        int nightModeFlag =
                nightMode ? Configuration.UI_MODE_NIGHT_YES : Configuration.UI_MODE_NIGHT_NO;
        config.uiMode = nightModeFlag | (config.uiMode & ~Configuration.UI_MODE_NIGHT_MASK);
        wrapper.applyOverrideConfiguration(config);
        return wrapper;
    }

    /**
     * The current theme setting, reflecting either the user setting or the default if the user has
     * not explicitly set a preference.
     * @return The current theme setting. See {@link ThemeType}.
     */
    public static @ThemeType int getThemeSetting() {
        int userSetting = ChromeSharedPreferences.getInstance().readInt(UI_THEME_SETTING, -1);
        if (userSetting == -1) {
            return ThemeType.SYSTEM_DEFAULT;
        } else {
            return userSetting;
        }
    }

    /**
     * Returns the title to display for the given theme.
     *
     * @param context The context in which the title will be displayed.
     * @param theme The theme for which to return the title.
     * @return the title to display.
     */
    public static String getThemeSettingTitle(Context context, @ThemeType int theme) {
        switch (theme) {
            case ThemeType.DARK:
                return context.getString(R.string.dark_mode);
            case ThemeType.LIGHT:
                return context.getString(R.string.light_mode);
            case ThemeType.SYSTEM_DEFAULT:
                return context.getString(R.string.themes_system_default_title);
        }
        throw new IllegalArgumentException("Unknown `theme`: " + theme);
    }

    public static void setNightModeSupportedForTesting(@Nullable Boolean nightModeSupported) {
        sNightModeSupportedForTest = nightModeSupported;
        ResettersForTesting.register(() -> sNightModeSupportedForTest = null);
    }
}
