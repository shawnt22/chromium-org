// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.readaloud.player.mini;

import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.BUFFERING;
import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.ERROR;
import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.PAUSED;
import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.PLAYING;
import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.PLAYBACK_CREATION;
import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.STOPPED;
import static org.chromium.chrome.modules.readaloud.PlaybackListener.State.UNKNOWN;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ObjectAnimator;
import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.animation.Interpolator;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.ColorInt;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.readaloud.player.Colors;
import org.chromium.chrome.browser.readaloud.player.InteractionHandler;
import org.chromium.chrome.browser.readaloud.player.R;
import org.chromium.chrome.browser.readaloud.player.TouchDelegateUtil;
import org.chromium.chrome.modules.readaloud.PlaybackArgs.PlaybackMode;
import org.chromium.chrome.modules.readaloud.PlaybackListener;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.interpolators.Interpolators;

/** Convenience class for manipulating mini player UI layout. */
@NullMarked
public class MiniPlayerLayout extends LinearLayout {
    private static final long FADE_DURATION_MS = 300L;
    private static final Interpolator FADE_INTERPOLATOR =
            Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR;

    private final Context mContext;

    private TextView mTitle;
    private TextView mSubtitle;
    private ProgressBar mProgressBar;
    private ImageView mPlayPauseView;
    private FrameLayout mBackdrop;
    private View mContents;
    private TextView mLoadingMessage;

    // Layouts related to different playback states.
    private LinearLayout mNormalLayout;
    private LinearLayout mBufferingLayout;
    private LinearLayout mErrorLayout;

    private @PlaybackListener.State int mLastPlaybackState;
    private boolean mEnableAnimations;
    private @Nullable ObjectAnimator mAnimator;
    private MiniPlayerMediator mMediator;
    private float mFinalOpacity;
    private @ColorInt int mBackgroundColorArgb;
    private int mYOffset;
    private PlaybackMode mRequestedPlaybackMode = PlaybackMode.UNSPECIFIED;

    private ProgressBar mSpinner;

    /** Constructor for inflating from XML. */
    public MiniPlayerLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
        this.mContext = context;
    }

    void destroy() {
        destroyAnimator();
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        // Cache important views.
        mTitle = (TextView) findViewById(R.id.title);
        mSubtitle = (TextView) findViewById(R.id.subtitle);
        mProgressBar = (ProgressBar) findViewById(R.id.progress_bar);
        mPlayPauseView = (ImageView) findViewById(R.id.play_button);

        mBackdrop = (FrameLayout) findViewById(R.id.backdrop);
        mContents = findViewById(R.id.mini_player_container);
        mNormalLayout = (LinearLayout) findViewById(R.id.normal_layout);
        mBufferingLayout = (LinearLayout) findViewById(R.id.buffering_layout);
        mErrorLayout = (LinearLayout) findViewById(R.id.error_layout);

        mLoadingMessage = (TextView) findViewById(R.id.loading_message);

        mSpinner = (ProgressBar) findViewById(R.id.readaloud_spinner);

        // Set dynamic colors.
        Context context = getContext();
        mBackgroundColorArgb = Colors.getMiniPlayerBackgroundColor(context);
        Colors.setProgressBarColor(mProgressBar);
        findViewById(R.id.backdrop).setBackgroundColor(mBackgroundColorArgb);
        if (mMediator != null) {
            mMediator.onBackgroundColorUpdated(mBackgroundColorArgb);
        }

        // TODO: Plug in WindowAndroid and use #isWindowOnTablet instead
        if (DeviceFormFactor.isNonMultiDisplayContextOnTablet(context)) {
            int paddingPx =
                    context.getResources()
                            .getDimensionPixelSize(R.dimen.readaloud_mini_player_tablet_padding);
            View container = findViewById(R.id.mini_player_container);
            container.setPadding(paddingPx, 0, paddingPx, 0);
        }
        mLastPlaybackState = PlaybackListener.State.UNKNOWN;
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        super.onLayout(changed, left, top, right, bottom);

        int height = mBackdrop.getHeight();
        if (height == 0) {
            return;
        }

        if (mMediator != null) {
            mMediator.onBackgroundColorUpdated(mBackgroundColorArgb);
            mMediator.onHeightKnown(height);
        }

        // Make the close button touch target bigger.
        TouchDelegateUtil.setBiggerTouchTarget(findViewById(R.id.close_button));
    }

    void changeOpacity(float startValue, float endValue) {
        assert (mMediator != null)
                : "Can't call changeOpacity() before setMediator() which should happen during"
                        + " mediator init.";
        if (endValue == mFinalOpacity) {
            return;
        }
        mFinalOpacity = endValue;
        setAlpha(startValue);

        View nonErrorLayoutContainer = mErrorLayout.getVisibility() == View.GONE ? mContents : null;
        Runnable onFinished =
                endValue == 1f
                        ? () -> mMediator.onFullOpacityReached(nonErrorLayoutContainer)
                        : mMediator::onZeroOpacityReached;

        if (mEnableAnimations) {
            // TODO: handle case where existing animation is incomplete and needs to be reversed
            destroyAnimator();
            mAnimator = ObjectAnimator.ofFloat(this, View.ALPHA, endValue);
            mAnimator.setDuration(FADE_DURATION_MS);
            mAnimator.setInterpolator(FADE_INTERPOLATOR);
            mAnimator.addListener(
                    new AnimatorListenerAdapter() {
                        @Override
                        public void onAnimationEnd(Animator animation) {
                            destroyAnimator();
                            onFinished.run();
                        }
                    });
            mAnimator.start();
        } else {
            setAlpha(endValue);
            onFinished.run();
        }
    }

    void enableAnimations(boolean enable) {
        mEnableAnimations = enable;
    }

    void setTitle(String title) {
        mTitle.setText(title);
    }

    void setRequestedPlaybackMode(PlaybackMode playbackMode) {
        mRequestedPlaybackMode = playbackMode;
        if (mRequestedPlaybackMode == PlaybackMode.OVERVIEW) {
            mLoadingMessage.setText(
                    mContext.getString(R.string.readaloud_mini_player_loading_ai_playback));
        } else {
            mLoadingMessage.setText(mContext.getString(R.string.readaloud_playback_loading));
        }
    }

    void setPlaybackMode(PlaybackMode playbackMode) {
        mSubtitle.setText(
                playbackMode == PlaybackMode.OVERVIEW
                        ? mContext.getString(R.string.readaloud_chrome_now_playing_audio_overview)
                        : mContext.getString(R.string.readaloud_chrome_now_playing));
    }

    /**
     * Set progress bar progress.
     * @param progress Fraction of playback completed in range [0, 1]
     */
    void setProgress(float progress) {
        mProgressBar.setProgress((int) (progress * mProgressBar.getMax()), true);
    }

    /**
     * Set the yOffset of the mini player layout. If yOffset < 0, the view need to shift up from the
     * bottom. It is implemented by applying a bottom margin.
     */
    void setYOffset(int yOffset) {
        if (mYOffset == yOffset) return;

        assert yOffset <= 0;

        mYOffset = -yOffset;
        MarginLayoutParams mlp = (MarginLayoutParams) getLayoutParams();
        mlp.bottomMargin = mYOffset;
        setLayoutParams(mlp);
    }

    void setInteractionHandler(InteractionHandler handler) {
        setOnClickListener(R.id.close_button, handler::onCloseClick);
        setOnClickListener(R.id.mini_player_container, handler::onMiniPlayerExpandClick);
        setOnClickListener(R.id.play_button, handler::onPlayPauseClick);
    }

    void setMediator(MiniPlayerMediator mediator) {
        mMediator = mediator;
    }

    void onPlaybackStateChanged(@PlaybackListener.State int state) {
        switch (state) {
                // UNKNOWN is currently the "reset" state and can be treated same as buffering.
            case PLAYBACK_CREATION:
            case BUFFERING:
            case UNKNOWN:
                showBufferingLayout();
                mProgressBar.setVisibility(View.GONE);
                break;

            case ERROR:
                showErrorLayout();
                mProgressBar.setVisibility(View.GONE);
                break;

            case PLAYING:
                if (mLastPlaybackState != PLAYING && mLastPlaybackState != PAUSED) {
                    showNormalLayout();
                    mProgressBar.setVisibility(View.VISIBLE);
                }

                mPlayPauseView.setImageResource(R.drawable.mini_pause_button);
                mPlayPauseView.setContentDescription(
                        getResources().getString(R.string.readaloud_pause));
                break;

            case STOPPED:
            case PAUSED:
                // Buffering/unknown and error states have their own views, show back the normal
                // layout if needed
                if (mLastPlaybackState != PLAYING
                        && mLastPlaybackState != PAUSED
                        && mLastPlaybackState != ERROR) {
                    showNormalLayout();
                    mProgressBar.setVisibility(View.VISIBLE);
                }
                mPlayPauseView.setImageResource(R.drawable.mini_play_button);
                mPlayPauseView.setContentDescription(
                        getResources().getString(R.string.readaloud_play));
                break;

            default:
                break;
        }
        mLastPlaybackState = state;
    }

    private void showBufferingLayout() {
        mBufferingLayout.setVisibility(View.VISIBLE);
        mNormalLayout.setVisibility(View.GONE);
        mErrorLayout.setVisibility(View.GONE);
        if (mRequestedPlaybackMode == PlaybackMode.OVERVIEW) {
            mLoadingMessage.setText(
                    mContext.getString(R.string.readaloud_mini_player_loading_ai_playback));
            mSpinner.setContentDescription(mContext.getString(R.string.readaloud_mini_player_spinner_overview_content_description));
        } else {
            mLoadingMessage.setText(mContext.getString(R.string.readaloud_playback_loading));
            mSpinner.setContentDescription(mContext.getString(R.string.readaloud_mini_player_spinner_classic_content_description));
        }
    }

    private void showNormalLayout() {
        mNormalLayout.setVisibility(View.VISIBLE);
        mBufferingLayout.setVisibility(View.GONE);
        mErrorLayout.setVisibility(View.GONE);
    }

    private void showErrorLayout() {
        mErrorLayout.setVisibility(View.VISIBLE);
        mNormalLayout.setVisibility(View.GONE);
        mBufferingLayout.setVisibility(View.GONE);
    }

    private void setOnClickListener(int id, Runnable handler) {
        findViewById(id)
                .setOnClickListener(
                        (v) -> {
                            handler.run();
                        });
    }

    private void destroyAnimator() {
        if (mAnimator != null) {
            mAnimator.removeAllListeners();
            mAnimator.cancel();
            mAnimator = null;
        }
    }

    @Nullable ObjectAnimator getAnimatorForTesting() {
        return mAnimator;
    }
}
