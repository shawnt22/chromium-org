// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;

import androidx.annotation.ColorInt;
import androidx.annotation.StringRes;
import androidx.core.widget.ImageViewCompat;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.components.browser_ui.widget.NumberRollView;
import org.chromium.components.browser_ui.widget.TintedDrawable;
import org.chromium.components.browser_ui.widget.selectable_list.SelectableListToolbar;
import org.chromium.ui.util.KeyboardNavigationListener;
import org.chromium.ui.widget.ChromeImageButton;

import java.util.Collections;
import java.util.List;

/** Handles toolbar functionality for TabListEditor. */
@NullMarked
class TabListEditorToolbar extends SelectableListToolbar<TabListEditorItemSelectionId> {
    private static final List<TabListEditorItemSelectionId> sEmptyIntegerList =
            Collections.emptyList();
    private ChromeImageButton mMenuButton;
    private TabListEditorActionViewLayout mActionViewLayout;
    private @Nullable View mNextFocusableView;
    @ColorInt private int mBackgroundColor;
    @StringRes private int mBackButtonAccessibilityString;
    private @Nullable RelatedTabCountProvider mRelatedTabCountProvider;

    public interface RelatedTabCountProvider {
        /**
         * @param itemIds the selected items.
         * @return the count of tabs including related tabs.
         */
        int getRelatedTabCount(List<TabListEditorItemSelectionId> itemIds);
    }

    public TabListEditorToolbar(Context context, AttributeSet attrs) {
        super(context, attrs);
        mBackButtonAccessibilityString = R.string.accessibility_tab_selection_editor_back_button;
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        showNavigationButton();
        mActionViewLayout = findViewById(R.id.action_view_layout);
        mMenuButton = findViewById(R.id.list_menu_button);

        // Can be overridden by #setToolbarTitle.
        mNumberRollView.setStringForZero(R.string.tab_selection_editor_toolbar_select_tabs);
        mNumberRollView.setString(R.plurals.tab_selection_editor_tabs_count);

        // Move the number roll view into a LinearLayout to manage spacing.
        LinearLayout.LayoutParams params =
                new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        0.0f);
        params.gravity = Gravity.CENTER_VERTICAL;
        ((ViewGroup) mNumberRollView.getParent()).removeView(mNumberRollView);
        mActionViewLayout.addView(mNumberRollView, 0, params);

        int finalChildIdx = mActionViewLayout.getChildCount() - 1;
        mActionViewLayout
                .getChildAt(finalChildIdx)
                .setOnKeyListener(
                        new KeyboardNavigationListener() {
                            @Override
                            public @Nullable View getNextFocusForward() {
                                return mNextFocusableView;
                            }
                        });
    }

    private void showNavigationButton() {
        TintedDrawable navigationIconDrawable =
                TintedDrawable.constructTintedDrawable(
                        getContext(), R.drawable.ic_arrow_back_white_24dp);
        final @ColorInt int lightIconColor =
                SemanticColorUtils.getDefaultIconColorInverse(getContext());
        navigationIconDrawable.setTint(lightIconColor);
        navigationIconDrawable.setAutoMirrored(true);

        setNavigationIcon(navigationIconDrawable);
        setNavigationContentDescription(mBackButtonAccessibilityString);
    }

    @Override
    public void onSelectionStateChange(List<TabListEditorItemSelectionId> selectedItems) {
        super.onSelectionStateChange(selectedItems);

        if (mRelatedTabCountProvider == null) return;

        int selectedCount = mRelatedTabCountProvider.getRelatedTabCount(selectedItems);
        mNumberRollView.setNumber(selectedCount, /* animate= */ true);
    }

    @Override
    protected void setNavigationButton(int navigationButton) {}

    @Override
    protected void showNormalView() {
        // TODO(crbug.com/40632732): This is a temporary way to force the toolbar always in the
        // selection
        // mode until the associated bug is addressed.
        showSelectionView(sEmptyIntegerList, true);
    }

    @Override
    protected void showSelectionView(
            List<TabListEditorItemSelectionId> selectedItems, boolean wasSelectionEnabled) {
        super.showSelectionView(selectedItems, wasSelectionEnabled);
        if (mBackgroundColor != Color.TRANSPARENT) {
            setBackgroundColor(mBackgroundColor);
        }
    }

    /**
     * @return the action view layout.
     */
    public TabListEditorActionViewLayout getActionViewLayout() {
        return mActionViewLayout;
    }

    /** Override the back button content description. */
    public void setBackButtonContentDescription(@StringRes int backButtonContentDescription) {
        mBackButtonAccessibilityString = backButtonContentDescription;
        setNavigationContentDescription(mBackButtonAccessibilityString);
    }

    /**
     * Update the tint for buttons, the navigation button and the action button, in the toolbar.
     *
     * @param tint New {@link ColorStateList} to use.
     */
    public void setButtonTint(ColorStateList tint) {
        TintedDrawable navigation = (TintedDrawable) assumeNonNull(getNavigationIcon());
        navigation.setTint(tint);
        ImageViewCompat.setImageTintList(mMenuButton, tint);
    }

    /**
     * Update the toolbar background color.
     *
     * @param backgroundColor The new color to use.
     */
    public void setToolbarBackgroundColor(@ColorInt int backgroundColor) {
        mBackgroundColor = backgroundColor;
        setBackgroundColor(mBackgroundColor);
    }

    /**
     * Update the {@link ColorStateList} used for text in {@link NumberRollView}.
     * @param colorStateList The new {@link ColorStateList} to use.
     */
    public void setTextColorStateList(ColorStateList colorStateList) {
        mNumberRollView.setTextColorStateList(colorStateList);
    }

    /**
     * Set provider for related tab count.
     * @param relatedTabCountProvider The provider to call to get the related tab count.
     */
    public void setRelatedTabCountProvider(RelatedTabCountProvider relatedTabCountProvider) {
        mRelatedTabCountProvider = relatedTabCountProvider;
    }

    /** Set the title of the toolbar when no tabs are selected. */
    public void setTitle(String title) {
        mNumberRollView.setStringForZero(title);
    }

    /** Set the view to focus to next after the toolbar. */
    public void setNextFocusableView(View nextFocusableView) {
        mNextFocusableView = nextFocusableView;
    }
}
