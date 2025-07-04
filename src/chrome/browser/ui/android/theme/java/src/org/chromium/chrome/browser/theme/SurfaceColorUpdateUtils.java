// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.theme;

import android.content.Context;
import android.content.res.ColorStateList;

import androidx.annotation.ColorInt;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.core.content.ContextCompat;

import com.google.android.material.color.MaterialColors;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.components.tab_groups.TabGroupColorId;
import org.chromium.components.tab_groups.TabGroupColorPickerUtils;
import org.chromium.ui.util.ColorUtils;

/** Utility class that provides color values based on feature flags enabled. */
@NullMarked
public class SurfaceColorUpdateUtils {
    private static final String TAG = "SurfaceColorUpdateUtils";

    /** Whether enable the containment on the tab group list pane. */
    public static boolean isTabGroupListContainmentEnabled() {
        return (useNewGtsSurfaceColor()
                        && ChromeFeatureList.sTabGroupListContainment.getValue());
    }

    private static boolean useNewGtsSurfaceColor() {
        return ChromeFeatureList.sGridTabSwitcherSurfaceColorUpdate.isEnabled();
    }

    /** Whether new toolbar and omnibox/location bar surface colors are being used. */
    public static boolean useNewToolbarSurfaceColor() {
        return ChromeFeatureList.sAndroidSurfaceColorUpdate.isEnabled();
    }

    /** Whether new GM3 colors are being used for the tab group colors. */
    public static boolean useNewGm3GtsTabGroupColors() {
        return ChromeFeatureList.sAndroidTabGroupsColorUpdateGm3.isEnabled()
                || ThemeModuleUtils.isForceEnableDependencies();
    }

    /**
     * Returns the background color for the Omnibox based on the enabled flag.
     *
     * @param context {@link Context} used to retrieve colors.
     * @return The background color.
     */
    public static @ColorInt int getOmniboxBackgroundColor(Context context, boolean isIncognito) {
        if (isIncognito) {
            return ContextCompat.getColor(context, R.color.toolbar_text_box_background_incognito);
        }
        return useNewToolbarSurfaceColor()
                ? SemanticColorUtils.getColorSurface(context)
                : ContextCompat.getColor(context, R.color.toolbar_text_box_bg_color);
    }

    /**
     * Returns the background color for the toolbar based on the enabled flag and other parameters.
     *
     * @param context {@link Context} used to retrieve colors.
     * @return The background color.
     */
    public static @ColorInt int getDefaultThemeColor(Context context, boolean isIncognito) {
        if (useNewToolbarSurfaceColor() && !isIncognito) {
            return SemanticColorUtils.getColorSurfaceContainerHigh(context);
        }
        return ChromeColors.getDefaultThemeColor(context, isIncognito);
    }

    /**
     * Determine the background color for tab strip based on surface color update flags.
     *
     * @see TabUiThemeUtil#getTabStripBackgroundColor
     * @param context {@link Context} used to retrieve colors.
     * @return The background color.
     */
    public static @ColorInt int getTabStripBackgroundColorDefault(Context context) {
        if (useNewToolbarSurfaceColor()) {
            return SemanticColorUtils.getColorSurfaceDim(context);
        }
        return SemanticColorUtils.getColorSurfaceContainerHigh(context);
    }

    /**
     * Determine the background color for tab strip when unfocused based on surface color update
     * flags.
     *
     * @see TabUiThemeUtil#getTabStripBackgroundColor
     * @param context {@link Context} used to retrieve colors.
     * @return The background color.
     */
    public static @ColorInt int getTabStripBackgroundColorUnfocused(Context context) {
        if (useNewToolbarSurfaceColor()) {
            @ColorInt int baseColor = SemanticColorUtils.getColorSurfaceDim(context);
            @ColorInt int overlayColor = SemanticColorUtils.getColorOnSurfaceInverse(context);
            float fraction =
                    context.getResources()
                            .getFraction(R.fraction.tab_strip_background_unfocused_fraction, 1, 1);
            return ColorUtils.overlayColor(baseColor, overlayColor, fraction);
        }

        @ColorInt int darkThemeColor = SemanticColorUtils.getColorSurfaceContainerLow(context);
        @ColorInt int lightThemeColor = SemanticColorUtils.getColorSurfaceContainer(context);
        return ColorUtils.inNightMode(context) ? darkThemeColor : lightThemeColor;
    }

    /**
     * Returns the background color for the grid tab switcher based on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @return The background color.
     */
    public static @ColorInt int getGridTabSwitcherBackgroundColor(
            Context context, boolean isIncognito) {
        // TODO(crbug.com/414404094): Add semantic color for incognito.
        if (useNewGtsSurfaceColor()) {
            return isIncognito
                    ? ContextCompat.getColor(
                            context, R.color.gm3_baseline_surface_container_high_dark)
                    : SemanticColorUtils.getColorSurfaceContainerHigh(context);
        }
        return isIncognito
                ? ContextCompat.getColor(context, R.color.default_bg_color_dark)
                : SemanticColorUtils.getDefaultBgColor(context);
    }

    /**
     * Returns the background color for the tab grid dialog based on the enabled flag and incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @return The background color.
     */
    public static @ColorInt int getTabGridDialogBackgroundColor(
            Context context, boolean isIncognito) {
        // TODO(crbug.com/414404094): Add semantic color for incognito.
        if (useNewGtsSurfaceColor()) {
            return isIncognito
                    ? ContextCompat.getColor(context, R.color.gm3_baseline_surface_container_dark)
                    : SemanticColorUtils.getColorSurfaceContainer(context);
        }
        @ColorInt
        int defaultBackground =
                ColorUtils.inNightMode(context)
                        ? SemanticColorUtils.getColorSurfaceContainer(context)
                        : SemanticColorUtils.getColorSurface(context);
        return isIncognito
                ? ContextCompat.getColor(context, R.color.gm3_baseline_surface_container_dark)
                : defaultBackground;
    }

    /**
     * Returns the background color for the card view in grid tab switcher on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @param colorId Color chosen by user for the TabGroup, null if not a tab group.
     * @return The background color.
     */
    public static @ColorInt int getCardViewBackgroundColor(
            Context context, boolean isIncognito, @Nullable @TabGroupColorId Integer colorId) {
        if (useNewGm3GtsTabGroupColors() && colorId != null) {
            return TabGroupColorPickerUtils.getTabGroupCardColor(context, colorId, isIncognito);
        }
        if (useNewGtsSurfaceColor()) {
            // TODO(crbug.com/414404094): Add semantic color for incognito tab card view.
            return isIncognito
                    ? ContextCompat.getColor(context, R.color.gm3_baseline_surface_dim_dark)
                    : SemanticColorUtils.getColorSurfaceDim(context);
        }
        return isIncognito
                ? ContextCompat.getColor(
                        context, R.color.gm3_baseline_surface_container_highest_dark)
                : SemanticColorUtils.getColorSurfaceContainerHighest(context);
    }

    /**
     * Returns the text color for the card view in grid tab switcher on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @param colorId Color chosen by user for the TabGroup, null if not a tab group.
     * @return The text appearance for the tab grid card title.
     */
    public static @ColorInt int getCardViewTextColor(
            Context context, boolean isIncognito, @Nullable @TabGroupColorId Integer colorId) {
        if (useNewGm3GtsTabGroupColors() && colorId != null) {
            return TabGroupColorPickerUtils.getTabGroupCardTextColor(context, colorId, isIncognito);
        }
        return isIncognito
                ? context.getColor(R.color.incognito_tab_title_color)
                : SemanticColorUtils.getDefaultTextColor(context);
    }

    /**
     * Returns the placeholder color for the card view in grid tab switcher on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @param colorId Color chosen by user for the TabGroup, null if not a tab group.
     * @return The mini-thumbnail placeholder color.
     */
    public static @ColorInt int getCardViewMiniThumbnailPlaceholderColor(
            Context context, boolean isIncognito, @Nullable @TabGroupColorId Integer colorId) {
        if (useNewGm3GtsTabGroupColors() && colorId != null) {
            return TabGroupColorPickerUtils.getTabGroupCardMiniThumbnailPlaceholderColor(
                    context, colorId, isIncognito);
        }
        if (isIncognito) {
            return context.getColor(R.color.incognito_tab_thumbnail_placeholder_color);
        }
        return SemanticColorUtils.getColorSurfaceContainerLow(context);
    }

    /**
     * Returns the text color used for the card view in grid tab switcher on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @param colorId Color chosen by user for the TabGroup, null if not a tab group.
     * @return The text color for the number used on the tab group cards.
     */
    public static @ColorInt int getCardViewGroupNumberTextColor(
            Context context, boolean isIncognito, @Nullable @TabGroupColorId Integer colorId) {
        if (useNewGm3GtsTabGroupColors() && colorId != null) {
            return TabGroupColorPickerUtils.getTabGroupCardTextColor(context, colorId, isIncognito);
        }
        return isIncognito
                ? context.getColor(R.color.incognito_tab_tile_number_color)
                : SemanticColorUtils.getDefaultTextColor(context);
    }

    /**
     * Returns the text color used for the card view in grid tab switcher on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @param colorId Color chosen by user for the TabGroup, null if not a tab group.
     * @return The text color for the number used on the tab group cards.
     */
    public static ColorStateList getCardViewActionButtonColor(
            Context context, boolean isIncognito, @Nullable @TabGroupColorId Integer colorId) {
        if (useNewGm3GtsTabGroupColors() && colorId != null) {
            return ColorStateList.valueOf(
                    TabGroupColorPickerUtils.getTabGroupCardTextColor(
                            context, colorId, isIncognito));
        }
        return isIncognito
                ? AppCompatResources.getColorStateList(
                        context, R.color.incognito_tab_action_button_color)
                : ColorStateList.valueOf(
                        MaterialColors.getColor(context, R.attr.colorOnSurfaceVariant, TAG));
    }

    /**
     * Returns the background color for the grid tab switcher message card based on the enabled flag
     * and incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @return The background color.
     */
    public static @ColorInt int getMessageCardBackgroundColor(Context context) {
        // TODO(crbug.com/414404094): Add semantic color for incognito.
        return useNewGtsSurfaceColor()
                ? SemanticColorUtils.getColorSurfaceContainerLow(context)
                : SemanticColorUtils.getCardBackgroundColor(context);
    }

    /**
     * Returns the background color for the grid tab switcher search box based on the enabled flag
     * and incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @return The background color.
     */
    public static @ColorInt int getGtsSearchBoxBackgroundColor(
            Context context, boolean isIncognito) {
        // TODO(crbug.com/414404094): Add semantic color for incognito.
        if (useNewGtsSurfaceColor()) {
            return isIncognito
                    ? ContextCompat.getColor(context, R.color.gm3_baseline_surface_dark)
                    : SemanticColorUtils.getColorSurface(context);
        }
        @ColorInt
        int defaultBackground =
                ColorUtils.inNightMode(context)
                        ? SemanticColorUtils.getColorSurfaceContainerHighest(context)
                        : SemanticColorUtils.getColorSurfaceContainerHigh(context);
        return isIncognito
                ? ContextCompat.getColor(
                        context, R.color.gm3_baseline_surface_container_highest_dark)
                : defaultBackground;
    }

    /**
     * Returns the background color for the hub pane switcher based on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     */
    public static @ColorInt int getPaneSwitcherBackgroundColor(
            Context context, boolean isIncognito) {
        if (useNewGtsSurfaceColor()) {
            return isIncognito
                    ? ContextCompat.getColor(context, R.color.pane_switcher_background_incognito_v2)
                    : SemanticColorUtils.getColorSurface(context);
        }

        return isIncognito
                ? ContextCompat.getColor(context, R.color.pane_switcher_background_incognito)
                : SemanticColorUtils.getColorSurfaceContainer(context);
    }

    /**
     * Returns the color selected tab item selector should use, based on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     */
    public static @ColorInt int geTabItemSelectorColor(Context context, boolean isIncognito) {
        if (useNewGtsSurfaceColor()) {
            return isIncognito
                    ? ContextCompat.getColor(
                            context, R.color.pane_switcher_selected_tab_incognito_v2)
                    : SemanticColorUtils.getColorSecondaryContainer(context);
        }

        return isIncognito
                ? ContextCompat.getColor(context, R.color.pane_switcher_selected_tab_incognito)
                : SemanticColorUtils.getColorSurfaceBright(context);
    }

    /**
     * Returns the color selected icons in hub pane switcher, based on the enabled flag and
     * incognito.
     *
     * @param context {@link Context} used to retrieve colors.
     * @param isIncognito Whether the color is used for incognito mode.
     * @param isGtsUpdateEnabled Whether GTS display update is enforced or not.
     */
    public static @ColorInt int getHubPaneSwitcherSelectedIconColor(
            Context context, boolean isIncognito, boolean isGtsUpdateEnabled) {
        if (isGtsUpdateEnabled) {
            if (useNewGtsSurfaceColor()) {
                @ColorInt
                int defaultColor =
                        ColorUtils.inNightMode(context)
                                ? SemanticColorUtils.getColorOnSurface(context)
                                : SemanticColorUtils.getColorOnSecondaryContainer(context);

                return isIncognito
                        ? ContextCompat.getColor(
                                context, R.color.pane_switcher_selected_tab_icon_color_incognito)
                        : defaultColor;
            }

            return isIncognito
                    ? ContextCompat.getColor(context, R.color.default_icon_color_light)
                    : SemanticColorUtils.getDefaultIconColor(context);
        }

        return isIncognito
                ? ContextCompat.getColor(context, R.color.default_control_color_active_dark)
                : SemanticColorUtils.getDefaultIconColorAccent1(context);
    }
}
