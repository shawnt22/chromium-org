// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_PICTURE_IN_PICTURE_BROWSER_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_PICTURE_IN_PICTURE_BROWSER_FRAME_VIEW_H_

#include <optional>

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_widget_fade_animator.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window.h"
#include "chrome/browser/ui/content_settings/content_setting_image_model_states.h"
#include "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_non_client_frame_view.h"
#include "chrome/browser/ui/views/location_bar/content_setting_image_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/overlay/close_image_button.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/multi_animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/widget/widget_observer.h"

class PictureInPictureBoundsChangeAnimation;
class PictureInPictureTucker;

namespace views {
class Label;
class View;
}  // namespace views

namespace {
class WindowEventObserver;
}  // namespace

class PictureInPictureBrowserFrameView
    : public BrowserNonClientFrameView,
      public ChromeLocationBarModelDelegate,
      public LocationIconView::Delegate,
      public IconLabelBubbleView::Delegate,
      public ContentSettingImageView::Delegate,
      public views::WidgetObserver,
      public PictureInPictureWindow,
      public gfx::AnimationDelegate {
  METADATA_HEADER(PictureInPictureBrowserFrameView, BrowserNonClientFrameView)

 public:
  PictureInPictureBrowserFrameView(BrowserFrame* frame,
                                   BrowserView* browser_view);
  PictureInPictureBrowserFrameView(const PictureInPictureBrowserFrameView&) =
      delete;
  PictureInPictureBrowserFrameView& operator=(
      const PictureInPictureBrowserFrameView&) = delete;
  ~PictureInPictureBrowserFrameView() override;

  // BrowserNonClientFrameView:
  gfx::Rect GetBoundsForTabStripRegion(
      const gfx::Size& tabstrip_minimum_size) const override;
  gfx::Rect GetBoundsForWebAppFrameToolbar(
      const gfx::Size& toolbar_preferred_size) const override;
  int GetTopInset(bool restored) const override;
  void OnBrowserViewInitViewsComplete() override;
  void UpdateThrobber(bool running) override {}
  gfx::Rect GetBoundsForClientView() const override;
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override;
  int NonClientHitTest(const gfx::Point& point) override;
  void GetWindowMask(const gfx::Size& size, SkPath* window_mask) override;
  void UpdateWindowIcon() override;
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;
  void OnThemeChanged() override;
  void Layout(PassKey) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void SetFrameBounds(const gfx::Rect& bounds) override;

  // ChromeLocationBarModelDelegate:
  content::WebContents* GetActiveWebContents() const override;
  bool GetURL(GURL* url) const override;
  bool ShouldPreventElision() override;
  bool ShouldTrimDisplayUrlAfterHostName() const override;
  bool ShouldDisplayURL() const override;

  // LocationIconView::Delegate:
  content::WebContents* GetWebContents() override;
  bool IsEditingOrEmpty() const override;
  SkColor GetSecurityChipColor(
      security_state::SecurityLevel security_level) const override;
  bool ShowPageInfoDialog() override;
  LocationBarModel* GetLocationBarModel() const override;
  ui::ImageModel GetLocationIcon(LocationIconView::Delegate::IconFetchedCallback
                                     on_icon_fetched) const override;
  std::optional<ui::ColorId> GetLocationIconBackgroundColorOverride()
      const override;

  // IconLabelBubbleView::Delegate:
  SkColor GetIconLabelBubbleSurroundingForegroundColor() const override;
  SkColor GetIconLabelBubbleBackgroundColor() const override;

  // ContentSettingImageView::Delegate:
  bool ShouldHideContentSettingImage() override;
  content::WebContents* GetContentSettingWebContents() override;
  ContentSettingBubbleModelDelegate* GetContentSettingBubbleModelDelegate()
      override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetVisibilityChanged(views::Widget* eidget, bool visible) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // PictureInPictureWindow:
  void SetForcedTucking(bool tuck) override;

  // gfx::AnimationDelegate:
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationProgressed(const gfx::Animation* animation) override;

  // PictureInPictureBrowserFrameView:
  virtual gfx::Rect GetHitRegion() const;

  // Convert the bounds of a child control view of |top_bar_container_view_| to
  // use the system's coordinate system while we need to know the original
  // container view.
  gfx::Rect ConvertTopBarControlViewBounds(views::View* control_view,
                                           views::View* source_view) const;

  // Gets the bounds of the controls.
  gfx::Rect GetLocationIconViewBounds() const;
  gfx::Rect GetContentSettingViewBounds(size_t index) const;
  gfx::Rect GetBackToTabControlsBounds() const;
  gfx::Rect GetCloseControlsBounds() const;

  LocationIconView* GetLocationIconView();

  // Updates the state of the images showing the content settings status.
  void UpdateContentSettingsIcons();

  // Updates the top bar title and icons according to whether user wants to
  // interact with the window. The top bar should be highlighted in all these
  // cases:
  // - PiP window is hovered with mouse
  // - PiP window is in focus with keyboard navigation
  // - PiP window is in focus with any other format of activation
  // - Dialogs are opened in the PiP window
  void UpdateTopBarView(bool render_active);

  // Returns the height of the top bar area, including the window top border.
  int GetTopAreaHeight() const;

  // Returns the the non-client view area size, i.e. the size of the window
  // elements where web content is not drawn.
  gfx::Size GetNonClientViewAreaSize() const;

  // Called when mouse entered or exited the pip window.
  void OnMouseEnteredOrExitedWindow(bool entered);

  // Returns true if there's an overlay view that's currently shown.
  bool IsOverlayViewVisible() const;

#if BUILDFLAG(IS_WIN)
  gfx::Insets GetClientAreaInsets(HMONITOR monitor) const;
#endif

  // Returns true if `content_setting_views_` has any visible views.
  bool HasAnyVisibleContentSettingViews() const;

  // Helper functions for testing.
  std::vector<gfx::Animation*> GetRenderActiveAnimationsForTesting();
  std::vector<gfx::Animation*> GetRenderInactiveAnimationsForTesting();
  views::View* GetBackToTabButtonForTesting();
  views::View* GetCloseButtonForTesting();
  views::Label* GetWindowTitleForTesting();
  PictureInPictureWidgetFadeAnimator* GetFadeAnimatorForTesting();

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class CloseReason {
    kOther = 0,
    kBackToTabButton = 1,
    kCloseButton = 2,
    kMaxValue = kCloseButton
  };

  void set_close_reason(CloseReason close_reason) {
    close_reason_ = close_reason;
  }

  AutoPipSettingOverlayView* get_auto_pip_setting_overlay_view_for_testing() {
    return auto_pip_setting_overlay_;
  }

  // `BrowserViewLayout` provides a maximum dialog size which is used to scale
  // dialogs to fit their parent widgets.  We don't directly know (or want to
  // know) how it does this.  Instead, assume that it's a constant difference
  // from our widget size.  In other words, our dialog size will be our widget
  // size minus whatever padding this function returns.
  gfx::Size ComputeDialogPadding() const;

  // Force any pending child resize to run, rather than waiting for enough
  // wall-clock time to elapse.
  void RunPendingChildResizeForTesting() {
    child_dialog_observer_helper_->RunPendingChildResizeForTesting();
  }

  bool IsChildResizePendingForTesting() const {
    return child_dialog_observer_helper_->IsChildResizePendingForTesting();
  }

 protected:
  views::View* top_bar_container_view() { return top_bar_container_view_; }

  // Returns the insets of the window frame borders for resizing.
  virtual gfx::Insets ResizeBorderInsets() const;

  // Returns the insets of the window frame borders.
  virtual gfx::Insets FrameBorderInsets() const;

 private:
  // Show `auto_pip_setting_overlay_` if we have it, and have a widget.
  void ShowOverlayIfNeeded();

  void EnforceTucking();

  CloseReason close_reason_ = CloseReason::kOther;

  // Observe child dialogs so that we can resize to ensure that they fit on
  // platforms where child dialogs would otherwise be cut off by our typically
  // small size.
  class ChildDialogObserverHelper : public views::WidgetObserver {
   public:
    explicit ChildDialogObserverHelper(PictureInPictureBrowserFrameView*,
                                       BrowserView*);
    ChildDialogObserverHelper(const ChildDialogObserverHelper&) = delete;
    ChildDialogObserverHelper& operator=(const ChildDialogObserverHelper&) =
        delete;
    ~ChildDialogObserverHelper() override;

    // views::WidgetObserver:
    void OnWidgetBoundsChanged(views::Widget* widget,
                               const gfx::Rect& new_bounds) override;
    void OnWidgetDestroying(views::Widget* widget) override;
    void OnWidgetVisibilityChanged(views::Widget* widget,
                                   bool visible) override;
    void OnWidgetChildAdded(views::Widget* widget,
                            views::Widget* child_dialog) override;
    void OnWidgetChildRemoved(views::Widget* widget,
                              views::Widget* child_dialog) override;

    void RunPendingChildResizeForTesting() {
      if (resize_timer_.IsRunning()) {
        resize_timer_.FireNow();
      }
    }
    bool IsChildResizePendingForTesting() const {
      return resize_timer_.IsRunning();
    }

   private:
    enum class ResizingState {
      // We are not currently resized for a child dialog.  For example, if the
      // user manually resizes the pip window, then this is the right state.
      kNotSizedToChildren,

      // A resize due to a child widget is pending.  These are not run
      // immediately because they can happen too quickly.  So, we batch them.
      // This indicates that there's a delayed task pending.
      kPendingResizeForChild,

      // We are in the process of resizing to match a child widget.  While in
      // this state, we will ignore any child resizes to prevent a loop.
      kResizeForChildInProgress,

      // We have finished transitioning to a new size to fit a child dialog and
      // we have not yet returned to the original size (because the child dialog
      // is still open).
      kSizedToChildren,
    };

    void PostResizeForChild(const gfx::Rect& new_bounds);
    void FinishPendingResizeForChild();

    void MaybeResizeForChildDialog(views::Widget* child_dialog);
    void MaybeRevertSizeAfterChildDialogCloses();

    const raw_ptr<PictureInPictureBrowserFrameView> pip_frame_;
    // TODO: replace this with pip_frame->GetWidget()
    const raw_ptr<views::Widget> pip_widget_;

    ResizingState resizing_state_ = ResizingState::kNotSizedToChildren;

    base::ScopedObservation<views::Widget, views::WidgetObserver>
        pip_widget_observation_{this};

    base::ScopedMultiSourceObservation<views::Widget, views::WidgetObserver>
        child_dialog_observations_{this};

    // Tracks child dialogs that have not yet been shown.
    base::flat_set<raw_ptr<views::Widget, CtnExperimental>>
        invisible_child_dialogs_;

    // The bounds that we forced the window to be in response to a child dialog
    // opening.
    gfx::Rect latest_child_dialog_forced_bounds_;

    // The bounds that the window would ideally be if we did not have to enlarge
    // to fit a child dialog.
    gfx::Rect latest_user_desired_bounds_;

    base::OneShotTimer resize_timer_;
    gfx::Rect pending_bounds_;
  };

  std::unique_ptr<ChildDialogObserverHelper> child_dialog_observer_helper_;

  // A model required to use LocationIconView.
  std::unique_ptr<LocationBarModel> location_bar_model_;

  raw_ptr<views::FlexLayoutView> top_bar_container_view_ = nullptr;

  // An icon to the left of the window title, which reuses the location icon in
  // the location bar of a normal browser. Since the web contents to PiP is
  // guaranteed to be secure, this icon should always be the HTTPS lock.
  raw_ptr<LocationIconView> location_icon_view_ = nullptr;

  raw_ptr<views::Label> window_title_ = nullptr;

  // A container view for the top right buttons.
  raw_ptr<views::FlexLayoutView> button_container_view_ = nullptr;

  // The content setting views for icons and bubbles.
  std::vector<raw_ptr<ContentSettingImageView, VectorExperimental>>
      content_setting_views_;

  raw_ptr<CloseImageButton> close_image_button_ = nullptr;
  raw_ptr<views::View> back_to_tab_button_ = nullptr;

  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};

  // When the window is created and shown for the first time, we show the active
  // window state even if the mouse is not inside it.
  bool render_active_ = true;

  bool mouse_inside_window_ = false;

  // Animations for the top bar title and buttons. When the mouse moves in or
  // out of the window, the title color and camera icon color will highlight
  // or dim, the back to tab button and close button will show or hide, and the
  // camera icon will move left or right if present. We consider animation state
  // 1.0 as the window active state (mouse in) and 0.0 as inactive state (mouse
  // out).
  gfx::SlideAnimation top_bar_color_animation_;
  gfx::SlideAnimation move_camera_button_to_left_animation_;
  gfx::MultiAnimation move_camera_button_to_right_animation_;
  gfx::MultiAnimation show_back_to_tab_button_animation_;
  gfx::MultiAnimation hide_back_to_tab_button_animation_;
  gfx::MultiAnimation show_close_button_animation_;
  gfx::MultiAnimation hide_close_button_animation_;
  gfx::LinearAnimation show_all_buttons_animation_;
  gfx::LinearAnimation hide_all_buttons_animation_;

  // The foreground color given the current state of the
  // `top_bar_color_animation_`.
  std::optional<SkColor> current_foreground_color_;

  // Used to monitor key and mouse events from native window.
  std::unique_ptr<WindowEventObserver> window_event_observer_;

  // If non-null, this displays the allow / block setting overlay for autopip.
  raw_ptr<AutoPipSettingOverlayView> auto_pip_setting_overlay_ = nullptr;

  // Animates programmatic changes to bounds (e.g. via `resizeTo()` or
  // `resizeBy()` calls).
  std::unique_ptr<PictureInPictureBoundsChangeAnimation>
      bounds_change_animation_;

  // Used to animate the Picture-in-Picture window creation.
  std::unique_ptr<PictureInPictureWidgetFadeAnimator> fade_animator_;

  // Used to tuck/untuck this widget into the side of the screen.
  std::unique_ptr<PictureInPictureTucker> tucker_;
  bool is_tucking_forced_ = false;

  base::WeakPtrFactory<PictureInPictureBrowserFrameView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_PICTURE_IN_PICTURE_BROWSER_FRAME_VIEW_H_
