// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.optional_button;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.transition.ChangeBounds;
import android.transition.Fade;
import android.transition.Slide;
import android.transition.Transition;
import android.transition.Transition.TransitionListener;
import android.transition.TransitionManager;
import android.transition.TransitionSet;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver.OnGlobalLayoutListener;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.ImageView.ScaleType;
import android.widget.TextView;

import androidx.annotation.DimenRes;
import androidx.annotation.DrawableRes;
import androidx.annotation.IntDef;
import androidx.annotation.StringRes;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.appcompat.widget.TooltipCompat;
import androidx.core.view.ViewCompat;
import androidx.core.widget.ImageViewCompat;

import com.google.android.material.color.MaterialColors;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.build.annotations.MonotonicNonNull;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.theme.ThemeModuleUtils;
import org.chromium.chrome.browser.toolbar.R;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarButtonVariant;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarFeatures;
import org.chromium.chrome.browser.toolbar.optional_button.ButtonData.ButtonSpec;
import org.chromium.chrome.browser.toolbar.optional_button.OptionalButtonConstants.TransitionType;
import org.chromium.chrome.browser.util.ChromeAccessibilityUtil;
import org.chromium.components.browser_ui.widget.textbubble.TextBubble;
import org.chromium.ui.interpolators.Interpolators;
import org.chromium.ui.listmenu.ListMenuButton;
import org.chromium.ui.widget.ViewRectProvider;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.function.BooleanSupplier;

/** Toolbar button that performs animated transitions between icons. */
@NullMarked
class OptionalButtonView extends FrameLayout implements TransitionListener {
    private static final int SWAP_TRANSITION_DURATION_MS = 300;
    private static final int HIDE_TRANSITION_DURATION_MS = 225;

    // Constants for text bubble displayed instead of chip expansion/collapse
    // depending on the toolbar width. Used for CCT.
    private static final int TEXT_BUBBLE_FOR_ANIMATION_DURATION_MS = 5000;
    private static final int TEXT_BUBBLE_FOR_ANIMATION_START_DELAY_MS = 500;

    private final int mExpandedStatePaddingPx;

    private int mCollapsedStateWidthPx;
    private TextView mActionChipLabel;
    private ImageView mBackground;
    private ListMenuButton mButton;
    private ImageView mAnimationImage;

    private @Nullable Drawable mIconDrawable;

    private @MonotonicNonNull ViewGroup mTransitionRoot;
    private @Nullable String mContentDescription;
    private @Nullable String mActionChipLabelString;
    private boolean mCurrentButtonSupportsTinting;
    private boolean mIsIncognitoBranded;
    private @Nullable ColorStateList mForegroundColorTint;
    private int mBackgroundColorFilter;
    private @Nullable Runnable mOnBeforeHideTransitionCallback;
    private @Nullable Callback<Transition> mFakeBeginTransitionForTesting;
    private @Nullable Handler mHandler;
    private @Nullable Handler mHandlerForTesting;

    private @State int mState;

    private @AdaptiveToolbarButtonVariant int mCurrentButtonVariant =
            AdaptiveToolbarButtonVariant.NONE;
    private boolean mCanCurrentButtonShow;

    // Indicates whether this optional button can change its own the visibility or leave the control
    // to some other entity. {@code true} by default.
    private boolean mCanChangeOwnVisibility = true;
    private @ButtonType int mCurrentButtonType;
    private @ButtonType int mNextButtonType;

    private @Nullable OnClickListener mClickListener;
    private @Nullable OnLongClickListener mLongClickListener;
    private @Nullable Callback<Integer> mTransitionStartedCallback;
    private @Nullable Callback<Integer> mTransitionFinishedCallback;
    private @Nullable BooleanSupplier mIsAnimationAllowedPredicate;
    private final Runnable mCollapseActionChipRunnable =
            new Runnable() {
                @Override
                public void run() {
                    assumeNonNull(mIsAnimationAllowedPredicate);
                    if (mIsAnimationAllowedPredicate.getAsBoolean()) {
                        animateActionChipCollapse();
                    } else {
                        showIcon(false);
                    }
                }
            };

    @IntDef({
        State.HIDDEN,
        State.SHOWING_ICON,
        State.SHOWING_ACTION_CHIP,
        State.RUNNING_SHOW_TRANSITION,
        State.RUNNING_HIDE_TRANSITION,
        State.RUNNING_ACTION_CHIP_EXPANSION_TRANSITION,
        State.RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION,
        State.RUNNING_SWAP_TRANSITION
    })
    @Retention(RetentionPolicy.SOURCE)
    private @interface State {
        int HIDDEN = 0;
        int SHOWING_ICON = 1;
        int SHOWING_ACTION_CHIP = 2;
        int RUNNING_SHOW_TRANSITION = 3;
        int RUNNING_HIDE_TRANSITION = 4;
        int RUNNING_ACTION_CHIP_EXPANSION_TRANSITION = 5;
        int RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION = 6;
        int RUNNING_SWAP_TRANSITION = 7;
    }

    @IntDef({ButtonType.STATIC, ButtonType.DYNAMIC})
    @Retention(RetentionPolicy.SOURCE)
    private @interface ButtonType {
        int STATIC = 0;
        int DYNAMIC = 1;
    }

    void setTransitionStartedCallback(Callback<Integer> callback) {
        mTransitionStartedCallback = callback;
    }

    void setTransitionFinishedCallback(Callback<Integer> callback) {
        mTransitionFinishedCallback = callback;
    }

    void setIsAnimationAllowedPredicate(BooleanSupplier isAnimationAllowedPredicate) {
        mIsAnimationAllowedPredicate = isAnimationAllowedPredicate;
    }

    void setOnBeforeHideTransitionCallback(Runnable callback) {
        mOnBeforeHideTransitionCallback = callback;
    }

    void setPaddingStart(int paddingStart) {
        setPaddingRelative(paddingStart, getPaddingTop(), getPaddingEnd(), getPaddingBottom());
    }

    public void setIsIncognitoBranded(boolean isIncognitoBranded) {
        mIsIncognitoBranded = isIncognitoBranded;
        if (isCpaSpecUpdateEnabled()) {
            // When isCpaSpecUpdateEnabled, logic for setting the background resource is in
            // #updateButtonWithAnimation.
            return;
        }
        @DrawableRes int backgroundDrawableRes = R.drawable.optional_button_background;
        if (isIncognitoBranded) {
            backgroundDrawableRes = R.drawable.optional_button_background_baseline;
        }
        mButton.setBackgroundResource(backgroundDrawableRes);
    }

    private void setBackgroundResourceHelper(boolean isCpaCheckedState) {
        @DrawableRes
        int backgroundDrawableRes =
                isCpaCheckedState
                        ? R.drawable.optional_button_background_square
                        : R.drawable.optional_button_background;

        // Currently incognito mode doesn't support CPA, so we always set the baseline drawable.
        if (mIsIncognitoBranded) {
            backgroundDrawableRes = R.drawable.optional_button_background_baseline;
        }
        mButton.setBackgroundResource(backgroundDrawableRes);
    }

    void setCollapsedStateWidth(int width) {
        mCollapsedStateWidthPx = width;
    }

    public void cancelTransition() {
        if (isRunningTransition()) {
            TransitionManager.endTransitions(mTransitionRoot);
        }
    }

    /**
     * Updates the button's icon, click handler, description and other attributes with a transition
     * animation. The animation that runs depends on the current state of this view (Whether is
     * hidden or showing another icon) and the attributes of the new icon (Whether it contains an
     * action chip description).
     *
     * @param buttonData object containing the new button's icon, handlers, description and other
     *     attributes. If null then this view starts a hide transition.
     */
    void updateButtonWithAnimation(@Nullable ButtonData buttonData) {
        boolean canShow = buttonData != null && buttonData.canShow();
        // If we receive the same button with the same visibility then there's no need to update.
        if (buttonData != null
                && mCurrentButtonVariant == buttonData.getButtonSpec().getButtonVariant()
                && mCanCurrentButtonShow == canShow
                && mIconDrawable == buttonData.getButtonSpec().getDrawable()) {
            return;
        }

        if (mTransitionRoot == null || mIsAnimationAllowedPredicate == null) {
            throw new IllegalStateException(
                    "Both transitionRoot and animationAllowedPredicate must be set before starting "
                            + "a transition");
        }

        boolean isAnimationAllowedByParent = mIsAnimationAllowedPredicate.getAsBoolean();

        if (isRunningTransition()) {
            // If we are running any transitions then finish them immediately and jump to the next
            // state.
            TransitionManager.endTransitions(mTransitionRoot);
            // TransitionManager.endTransitions calls onTransitionEnd on its own, but it's done
            // asynchronously which causes flaky tests. We call it here to ensure the state updates
            // and callbacks are executed synchronously.
            onTransitionEnd(null);
        }

        if (mState == State.SHOWING_ACTION_CHIP) {
            // If the action chip is expanded then deschedule the collapse task and collapse
            // immediately.
            getHandler().removeCallbacks(mCollapseActionChipRunnable);
            showIcon(false);
            mState = getNextState();
        }

        if (buttonData == null || !canShow) {
            mCurrentButtonVariant = AdaptiveToolbarButtonVariant.NONE;
            mCanCurrentButtonShow = false;
            hide(isAnimationAllowedByParent);
            return;
        }

        ButtonSpec buttonSpec = buttonData.getButtonSpec();
        boolean isButtonVariantChanging = mCurrentButtonVariant != buttonSpec.getButtonVariant();
        // This boolean is final because it's passed to an inner class (OnGlobalLayoutListener).
        final boolean canAnimate = isAnimationAllowedByParent && isButtonVariantChanging;

        mCurrentButtonVariant = buttonSpec.getButtonVariant();
        mCanCurrentButtonShow = canShow;
        mCurrentButtonSupportsTinting = buttonSpec.getSupportsTinting();

        mIconDrawable = buttonSpec.getDrawable();

        boolean isCpaCheckedState = buttonData.getButtonSpec().isChecked();

        if (isCpaSpecUpdateEnabled()) {
            // Change the CPA background to a square if the button data instance is owned by
            // PriceTrackingButtonController and is a "checked" state.
            @DrawableRes
            int resId =
                    isCpaCheckedState
                            ? R.drawable
                                    .modern_toolbar_text_box_background_with_primary_color_square
                            : R.drawable.modern_toolbar_text_box_background;

            mBackground.setImageDrawable(AppCompatResources.getDrawable(getContext(), resId));
            setBackgroundResourceHelper(isCpaCheckedState);
        }

        mNextButtonType = buttonSpec.isDynamicAction() ? ButtonType.DYNAMIC : ButtonType.STATIC;
        @StringRes int chipLabelResId = buttonSpec.getActionChipLabelResId();
        if (buttonSpec.getActionChipLabelResId() == Resources.ID_NULL) {
            mActionChipLabelString = null;
        } else {
            mActionChipLabelString = getContext().getString(chipLabelResId);
        }

        // The button's height precisely matches the avatar and its padding. When an error badge is
        // added in the avatar's bottom-right corner, the avatar height increases. To maintain the
        // original position of the avatar, the button's bottom padding is then reduced.
        int paddingBottom =
                getDimensionPixelSize(
                        buttonSpec.hasErrorBadge()
                                ? R.dimen
                                        .optional_toolbar_phone_button_with_error_badge_padding_bottom
                                : R.dimen
                                        .toolbar_phone_optional_button_foreground_vertical_padding);

        mButton.setPaddingRelative(
                mButton.getPaddingStart(),
                mButton.getPaddingTop(),
                mButton.getPaddingEnd(),
                paddingBottom);

        mClickListener = buttonSpec.getOnClickListener();
        mLongClickListener = buttonSpec.getOnLongClickListener();
        mButton.setEnabled(buttonData.isEnabled());
        mActionChipLabel.setEnabled(buttonData.isEnabled());

        // Set hover state tooltip text for optional toolbar buttons(e.g. share, voice search, new
        // tab and profile).
        if (buttonSpec.getHoverTooltipTextId() != ButtonSpec.INVALID_TOOLTIP_TEXT_ID
                && mButton != null) {
            TooltipCompat.setTooltipText(
                    mButton, getContext().getString(buttonSpec.getHoverTooltipTextId()));
        } else {
            TooltipCompat.setTooltipText(mButton, null);
        }
        mContentDescription = buttonSpec.getContentDescription();
        boolean showTextBubble = buttonData.shouldShowTextBubble();

        // If the transition root hasn't been laid out then try again after the next layout. This
        // may happen if the view gets initialized while the activity is not visible (e.g. when a
        // setting change forces an activity reset).
        if (!ViewCompat.isLaidOut(mTransitionRoot)) {
            getViewTreeObserver()
                    .addOnGlobalLayoutListener(
                            new OnGlobalLayoutListener() {
                                @Override
                                public void onGlobalLayout() {
                                    if (ViewCompat.isLaidOut(mTransitionRoot)) {
                                        startTransitionToNewButton(
                                                canAnimate, showTextBubble, chipLabelResId);
                                        getViewTreeObserver().removeOnGlobalLayoutListener(this);
                                    }
                                }
                            });
        } else {
            startTransitionToNewButton(canAnimate, showTextBubble, chipLabelResId);
        }
    }

    private void startTransitionToNewButton(
            boolean canAnimate, boolean showTextBubble, @StringRes int bubbleTextId) {
        if (mState == State.HIDDEN && mActionChipLabelString == null) {
            showIcon(canAnimate);
        } else if (canAnimate && mActionChipLabelString != null) {
            if (showTextBubble) {
                showIcon(/* animate= */ true);
                getHandler()
                        .postDelayed(
                                () -> showTextBubble(bubbleTextId),
                                TEXT_BUBBLE_FOR_ANIMATION_START_DELAY_MS);
            } else {
                animateActionChipExpansion();
            }
        } else if (canAnimate && mActionChipLabelString == null) {
            animateSwapToNewIcon();
        } else {
            showIcon(false);
        }
    }

    /**
     * Set a view to use as a root for all transition animations. It's used to animate sibling views
     * when this one changes width.
     */
    // TODO(salg): Consider getting rid of this property as it can be awkward to have a view
    // initiating an animation on its siblings.
    void setTransitionRoot(ViewGroup transitionRoot) {
        mTransitionRoot = transitionRoot;
    }

    void setBackgroundColorFilter(int color) {
        mBackgroundColorFilter = color;
        mBackground.setColorFilter(color);
    }

    void setBackgroundAlpha(int alpha) {
        mBackground.setImageAlpha(alpha);
    }

    View getBackgroundView() {
        return mBackground;
    }

    void setColorStateList(ColorStateList colorStateList) {
        mForegroundColorTint = colorStateList;

        if (mCurrentButtonSupportsTinting) {
            ImageViewCompat.setImageTintList(mButton, colorStateList);
        }
        if (colorStateList != null) {
            mActionChipLabel.setTextColor(colorStateList);
        }
    }

    void setHandlerForTesting(Handler handler) {
        mHandlerForTesting = handler;
    }

    View getButtonView() {
        return mButton;
    }

    ImageView getAnimationViewForTesting() {
        return mAnimationImage;
    }

    /** Constructor for inflating from XML. */
    public OptionalButtonView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);

        mState = State.HIDDEN;

        mCollapsedStateWidthPx =
                getDimensionPixelSize(R.dimen.toolbar_phone_optional_button_collapsed_state_width);
        mExpandedStatePaddingPx =
                getDimensionPixelSize(
                        R.dimen.toolbar_phone_optional_button_expanded_state_extra_width);
    }

    @Override
    public void setEnabled(boolean enabled) {
        super.setEnabled(enabled);

        mButton.setEnabled(enabled);
        mActionChipLabel.setEnabled(enabled);
    }

    void setCanChangeVisibility(boolean canChange) {
        mCanChangeOwnVisibility = canChange;
    }

    /**
     * Gets a handler used to schedule the action chip collapse animation after the action chip
     * finishes expanding. Tests can set their own handler with {@code setHandlerForTesting}.
     */
    @Override
    public Handler getHandler() {
        if (mHandlerForTesting != null) {
            return mHandlerForTesting;
        }

        if (mHandler == null) {
            mHandler = new Handler(ThreadUtils.getUiThreadLooper());
        }

        return mHandler;
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        mBackground = findViewById(R.id.swappable_icon_secondary_background);
        mButton = findViewById(R.id.optional_toolbar_button);
        mAnimationImage = findViewById(R.id.swappable_icon_animation_image);
        mActionChipLabel = findViewById(R.id.action_chip_label);

        // If isCpaSpecUpdateEnabled, overriding the background in #updateButtonWithAnimation.
        mBackground.setImageDrawable(
                AppCompatResources.getDrawable(
                        getContext(), R.drawable.modern_toolbar_text_box_background));
    }

    /**
     * Listens to all transition starts. This is called even when animations are disabled.
     * Implementation of {@link TransitionListener}.
     * @param transition Transition that started, not used.
     */
    @Override
    public void onTransitionStart(Transition transition) {
        if (mState != State.RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION) {
            // Disable click listeners during the transitions (except action chip collapse, which
            // goes to the same icon/action).
            mButton.setOnClickListener(null);
            mButton.setOnLongClickListener(null);
            mButton.setContentDescription(null);
        }

        if (mTransitionStartedCallback != null) {
            mTransitionStartedCallback.onResult(getCurrentTransitionType());
        }
    }

    /**
     * Listens to all transition ends. This is called even if the transition is cancelled or if all
     * animations are disabled. Implementation of {@link TransitionListener}.
     *
     * @param transition Transition that ended, not used.
     */
    @Override
    public void onTransitionEnd(@Nullable Transition transition) {
        if (mTransitionFinishedCallback != null
                && getCurrentTransitionType() != TransitionType.NONE) {
            mTransitionFinishedCallback.onResult(getCurrentTransitionType());
        }

        mState = getNextState();
        mCurrentButtonType = mNextButtonType;

        // This image is only used during transitions, it should not be visible afterwards.
        mAnimationImage.setVisibility(GONE);

        if (mState == State.HIDDEN) {
            if (mCanChangeOwnVisibility) this.setVisibility(GONE);
        } else {
            mButton.setVisibility(VISIBLE);
            mButton.setImageDrawable(mIconDrawable);
            ImageViewCompat.setImageTintList(
                    mButton, mCurrentButtonSupportsTinting ? mForegroundColorTint : null);
            mButton.setOnClickListener(mClickListener);
            mButton.setLongClickable(mLongClickListener != null);
            mButton.setOnLongClickListener(mLongClickListener);
            mButton.setContentDescription(mContentDescription);
        }

        // When finished expanding the action chip schedule the collapse transition in 3 seconds.
        if (mState == State.SHOWING_ACTION_CHIP) {
            getHandler()
                    .postDelayed(
                            mCollapseActionChipRunnable,
                            AdaptiveToolbarFeatures.getContextualPageActionDelayMs(
                                    mCurrentButtonVariant));
        }
    }

    /** Implementation of {@link TransitionListener}. Not used. */
    @Override
    public void onTransitionCancel(Transition transition) {}

    /** Implementation of {@link TransitionListener}. Not used. */
    @Override
    public void onTransitionPause(Transition transition) {}

    /** Implementation of {@link TransitionListener}. Not used. */
    @Override
    public void onTransitionResume(Transition transition) {}

    private Transition createSwapIconTransition() {
        TransitionSet transition = new TransitionSet();
        transition.setOrdering(TransitionSet.ORDERING_TOGETHER);

        // All appearing/disappearing views will fade in/out.
        Fade fade = new Fade();

        // When appearing/disappearing mButton will shrink/grow.
        ShrinkTransition shrink = new ShrinkTransition();
        shrink.addTarget(mButton);

        // When appearing/disappearing mAnimationImage will move from/to the top.
        Slide slide = new Slide(Gravity.TOP);
        slide.addTarget(mAnimationImage);

        transition.addTransition(slide).addTransition(shrink).addTransition(fade);
        transition.setDuration(SWAP_TRANSITION_DURATION_MS);
        if (isCpaSpecUpdateEnabled()) {
            transition.setInterpolator(Interpolators.DEFAULT_SPATIAL);
        }
        transition.addListener(this);

        return transition;
    }

    private Transition createShowHideTransition() {
        TransitionSet transition = new TransitionSet();
        transition.setOrdering(TransitionSet.ORDERING_TOGETHER);

        Fade fade = new Fade();
        // When showing/hiding this view we change its width from/to 0dp, this transition animates
        // that width change.
        ChangeBounds changeBounds = new ChangeBounds();

        // When mButton shows/hides we use a grow/shrink animation.
        ShrinkTransition shrink = new ShrinkTransition();

        // When mButton and mBackground show up or hide they slide from/to the end (right in LTR,
        // left in RTL).
        Slide slide = new Slide(Gravity.END);
        slide.addTarget(mButton);
        slide.addTarget(mBackground);

        transition
                .addTransition(slide)
                .addTransition(shrink)
                .addTransition(fade)
                .addTransition(changeBounds);

        transition.setDuration(HIDE_TRANSITION_DURATION_MS);
        if (isCpaSpecUpdateEnabled()) {
            transition.setInterpolator(Interpolators.DEFAULT_SPATIAL);
        }
        transition.addListener(this);

        return transition;
    }

    private Transition createActionChipTransition() {
        TransitionSet transitionSet = new TransitionSet();
        transitionSet.setOrdering(TransitionSet.ORDERING_TOGETHER);

        // During the action chip transition we change this view's width to fit the action chip
        // label, this transition animates that change.
        ChangeBounds changeBounds = new ChangeBounds();

        // The action chip label and the new icon fade in and grow when showing up.
        Fade fade = new Fade();
        ShrinkTransition shrinkTransition = new ShrinkTransition();

        transitionSet
                .addTransition(changeBounds)
                .addTransition(fade)
                .addTransition(shrinkTransition);

        transitionSet.setDuration(SWAP_TRANSITION_DURATION_MS);
        if (isCpaSpecUpdateEnabled()) {
            transitionSet.setInterpolator(Interpolators.DEFAULT_SPATIAL);
        }
        transitionSet.addListener(this);

        return transitionSet;
    }

    private @TransitionType int getCurrentTransitionType() {
        switch (mState) {
            case State.RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION:
                return TransitionType.COLLAPSING_ACTION_CHIP;
            case State.RUNNING_ACTION_CHIP_EXPANSION_TRANSITION:
                return TransitionType.EXPANDING_ACTION_CHIP;
            case State.RUNNING_HIDE_TRANSITION:
                return TransitionType.HIDING;
            case State.RUNNING_SHOW_TRANSITION:
                return TransitionType.SHOWING;
            case State.RUNNING_SWAP_TRANSITION:
                return TransitionType.SWAPPING;
            case State.HIDDEN:
            case State.SHOWING_ACTION_CHIP:
            case State.SHOWING_ICON:
                return TransitionType.NONE;
            default:
                throw new IllegalStateException("Unexpected value: " + mState);
        }
    }

    private void setWidth(int widthPx) {
        ViewGroup.LayoutParams layoutParams = this.getLayoutParams();

        layoutParams.width = widthPx;

        setLayoutParams(layoutParams);
    }

    private boolean isRunningTransition() {
        return mState == State.RUNNING_SHOW_TRANSITION
                || mState == State.RUNNING_HIDE_TRANSITION
                || mState == State.RUNNING_ACTION_CHIP_EXPANSION_TRANSITION
                || mState == State.RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION
                || mState == State.RUNNING_SWAP_TRANSITION;
    }

    private @State int getNextState() {
        switch (mState) {
            case State.RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION:
            case State.RUNNING_SWAP_TRANSITION:
            case State.RUNNING_SHOW_TRANSITION:
                return State.SHOWING_ICON;
            case State.RUNNING_ACTION_CHIP_EXPANSION_TRANSITION:
                return State.SHOWING_ACTION_CHIP;
            case State.RUNNING_HIDE_TRANSITION:
                return State.HIDDEN;
            default:
                return mState;
        }
    }

    private void animateSwapToNewIcon() {
        if (mState != State.SHOWING_ICON) return;

        boolean isRevertingToStatic =
                mCurrentButtonType == ButtonType.DYNAMIC && mNextButtonType == ButtonType.STATIC;

        // Set the background color filter before the transition, these changes are done instantly.
        if (mNextButtonType == ButtonType.DYNAMIC) {
            mBackground.setColorFilter(mBackgroundColorFilter);
        }

        // In mSwapIconTransition mAnimationImage always slides from/to the top, and mButton always
        // grows/shrinks.
        ImageView slidingIcon = mAnimationImage;
        ImageView shrinkingIcon = mButton;

        Drawable newIcon = mIconDrawable;
        Drawable oldIcon = mButton.getDrawable();

        ColorStateList oldIconTint = ImageViewCompat.getImageTintList(mButton);
        ColorStateList newIconTint = mCurrentButtonSupportsTinting ? mForegroundColorTint : null;

        // Prepare icons for the transition, these changes are done instantly.
        if (!isRevertingToStatic) {
            // In the default transition we want the new icon to slide from the top and the old one
            // to shrink.
            slidingIcon.setImageDrawable(newIcon);
            ImageViewCompat.setImageTintList(slidingIcon, newIconTint);
        } else {
            // In the reverse transition we want the new icon to grow and the old icon to slide to
            // the top
            slidingIcon.setImageDrawable(oldIcon);
            ImageViewCompat.setImageTintList(slidingIcon, oldIconTint);
            slidingIcon.setVisibility(VISIBLE);
            shrinkingIcon.setImageDrawable(newIcon);
            ImageViewCompat.setImageTintList(shrinkingIcon, newIconTint);
            shrinkingIcon.setVisibility(GONE);
        }

        // Begin a transition, all layout changes after this call will be animated. The animation
        // starts at the next frame.
        beginDelayedTransition(createSwapIconTransition());

        // Default transition.
        if (!isRevertingToStatic) {
            // New icon slides from the top.
            slidingIcon.setVisibility(VISIBLE);
            // Old icon shrinks.
            shrinkingIcon.setVisibility(GONE);
        }
        // Reverse transition.
        else {
            // Old icon slides to the top.
            slidingIcon.setVisibility(GONE);
            // New icon embiggens.
            shrinkingIcon.setVisibility(VISIBLE);
        }

        // Background shows/hides with a fade animation.
        mBackground.setVisibility(mNextButtonType == ButtonType.DYNAMIC ? VISIBLE : GONE);

        mState = State.RUNNING_SWAP_TRANSITION;
    }

    private void animateActionChipExpansion() {
        if (mState != State.SHOWING_ICON && mState != State.HIDDEN) {
            return;
        }

        if (getVisibility() == GONE) {
            if (mCanChangeOwnVisibility) this.setVisibility(VISIBLE);
            setWidth(0);
        }

        // Prepare views for the transition, these changes aren't animated.

        mActionChipLabel.setVisibility(GONE);
        mActionChipLabel.setText(mActionChipLabelString);

        mAnimationImage.setImageDrawable(mButton.getDrawable());
        // Update mAnimationImage's padding to match mButton's.
        mAnimationImage.setPaddingRelative(
                mButton.getPaddingStart(),
                mButton.getPaddingTop(),
                mButton.getPaddingEnd(),
                mButton.getPaddingBottom());
        ImageViewCompat.setImageTintList(
                mAnimationImage, ImageViewCompat.getImageTintList(mButton));

        mAnimationImage.setVisibility(VISIBLE);

        mButton.setImageDrawable(mIconDrawable);
        ImageViewCompat.setImageTintList(
                mButton, mCurrentButtonSupportsTinting ? mForegroundColorTint : null);
        mButton.setVisibility(GONE);

        if (AdaptiveToolbarFeatures.shouldUseAlternativeActionChipColor(mCurrentButtonVariant)) {
            int highlightColor = MaterialColors.getColor(this, R.attr.colorSecondaryContainer);
            mBackground.setColorFilter(highlightColor);
        } else {
            mBackground.setColorFilter(mBackgroundColorFilter);
        }

        // Begin a transition, all layout changes after this call will be animated. The animation
        // starts at the next frame.
        beginDelayedTransition(createActionChipTransition());

        mButton.setVisibility(VISIBLE);
        mAnimationImage.setVisibility(GONE);
        mActionChipLabel.setVisibility(VISIBLE);
        mBackground.setVisibility(VISIBLE);

        float actionChipLabelTextWidth =
                mActionChipLabel.getPaint().measureText(mActionChipLabelString);

        int maxExpandedStateWidthPx =
                getDimensionPixelSize(R.dimen.toolbar_phone_optional_button_action_chip_max_width);

        int expandedStateWidthPx =
                Math.min(
                        (int)
                                (mCollapsedStateWidthPx
                                        + actionChipLabelTextWidth
                                        + mExpandedStatePaddingPx),
                        maxExpandedStateWidthPx);

        setWidth(expandedStateWidthPx);

        mState = State.RUNNING_ACTION_CHIP_EXPANSION_TRANSITION;
    }

    private void animateActionChipCollapse() {
        // Begin a transition, all layout changes after this call will be animated. The animation
        // starts at the next frame.
        beginDelayedTransition(createActionChipTransition());

        mBackground.setColorFilter(mBackgroundColorFilter);
        mActionChipLabel.setVisibility(GONE);
        setWidth(mCollapsedStateWidthPx);

        mState = State.RUNNING_ACTION_CHIP_COLLAPSE_TRANSITION;
    }

    private void showTextBubble(@StringRes int stringId) {
        // TODO(crbug.com/391931916): Now the bubble shows up when the expansion animation would
        //     have appeared. Consider displaying IPH for setting a different cadence.
        var textBubble =
                new TextBubble(
                        getContext(),
                        this,
                        stringId,
                        stringId,
                        true,
                        new ViewRectProvider(this),
                        ChromeAccessibilityUtil.get().isAccessibilityEnabled());
        textBubble.setAutoDismissTimeout(TEXT_BUBBLE_FOR_ANIMATION_DURATION_MS);
        textBubble.show();
    }

    private void hide(boolean animate) {
        Transition transition = createShowHideTransition();
        if (!animate) {
            transition.setDuration(0);
        }
        // Begin a transition, all layout changes after this call will be animated. The animation
        // starts at the next frame.
        beginDelayedTransition(transition);

        mButton.setVisibility(GONE);
        mBackground.setVisibility(GONE);
        mActionChipLabel.setVisibility(GONE);
        setWidth(0);

        if (mOnBeforeHideTransitionCallback != null) {
            mOnBeforeHideTransitionCallback.run();
        }

        mState = State.RUNNING_HIDE_TRANSITION;
    }

    @Override
    public void onRtlPropertiesChanged(int layoutDirection) {
        if (mButton == null || mAnimationImage == null) return;

        // ImageView's scale type does not take into account the layout's direction, FIT_START
        // always aligns from the left and FIT_END always aligns from the right.
        if (layoutDirection == LAYOUT_DIRECTION_LTR) {
            mButton.setScaleType(ScaleType.FIT_START);
            mAnimationImage.setScaleType(ScaleType.FIT_START);
        } else {
            mButton.setScaleType(ScaleType.FIT_END);
            mAnimationImage.setScaleType(ScaleType.FIT_END);
        }
    }

    private void showIcon(boolean animate) {
        Transition transition = createShowHideTransition();
        if (!animate) {
            transition.setDuration(0);
        }

        // Prepare views for the transition, these changes aren't animated.
        if (mCanChangeOwnVisibility) this.setVisibility(VISIBLE);
        setWidth(0);

        mButton.setVisibility(GONE);
        mBackground.setVisibility(GONE);
        mAnimationImage.setVisibility(GONE);
        mActionChipLabel.setVisibility(GONE);

        mButton.setImageDrawable(mIconDrawable);
        ImageViewCompat.setImageTintList(
                mButton, mCurrentButtonSupportsTinting ? mForegroundColorTint : null);

        // Begin a transition, all layout changes after this call will be animated. The animation
        // starts at the next frame.
        beginDelayedTransition(transition);

        setWidth(mCollapsedStateWidthPx);
        mButton.setVisibility(VISIBLE);

        mBackground.setColorFilter(mBackgroundColorFilter);
        mBackground.setVisibility(mNextButtonType == ButtonType.DYNAMIC ? VISIBLE : GONE);

        mState = State.RUNNING_SHOW_TRANSITION;
    }

    public void setFakeBeginDelayedTransitionForTesting(
            Callback<Transition> fakeBeginDelayedTransition) {
        mFakeBeginTransitionForTesting = fakeBeginDelayedTransition;
    }

    private void beginDelayedTransition(Transition transition) {
        if (mFakeBeginTransitionForTesting != null) {
            mFakeBeginTransitionForTesting.onResult(transition);
            return;
        }

        TransitionManager.beginDelayedTransition(mTransitionRoot, transition);
    }

    private int getDimensionPixelSize(@DimenRes int dimenId) {
        return getResources().getDimensionPixelSize(dimenId);
    }

    // ============================================================================================
    // Flags
    // ============================================================================================
    public static boolean isCpaSpecUpdateEnabled() {
        return ChromeFeatureList.sCpaSpecUpdate.isEnabled()
                || ThemeModuleUtils.isForceEnableDependencies();
    }
}
