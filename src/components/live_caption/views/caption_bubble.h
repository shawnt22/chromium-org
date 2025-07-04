// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_LIVE_CAPTION_VIEWS_CAPTION_BUBBLE_H_
#define COMPONENTS_LIVE_CAPTION_VIEWS_CAPTION_BUBBLE_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "build/buildflag.h"
#include "components/live_caption/caption_bubble_settings.h"
#include "components/live_caption/views/caption_bubble_model.h"
#include "components/live_caption/views/translation_view_wrapper_base.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/native_theme/caption_style.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/metadata/view_factory.h"

namespace views {
class Checkbox;
class ImageButton;
class ImageView;
class Label;
}  // namespace views

namespace captions {

class CaptionBubbleEventObserver;
class CaptionBubbleFrameView;
class CaptionBubbleLabel;
class CaptionBubbleScrollView;
class ScrollLockButton;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. These should be the same as
// LiveCaptionSessionEvent in enums.xml.
// LINT.IfChange(SessionEvent)
enum class SessionEvent {
  // We began showing captions for an audio stream.
  kStreamStarted = 0,
  // The audio stream ended and the caption bubble closes.
  kStreamEnded = 1,
  // The close button was clicked, so we stopped listening to an audio stream.
  kCloseButtonClicked = 2,
  kMaxValue = kCloseButtonClicked,
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/accessibility/enums.xml:LiveCaptionSessionEvent)

#if BUILDFLAG(IS_CHROMEOS)
// Used by ash window manager to place the caption bubble in the correct
// container.
extern const ui::ClassProperty<bool>* const kIsCaptionBubbleKey;
#endif

///////////////////////////////////////////////////////////////////////////////
// Caption Bubble
//
//  A caption bubble that floats above all other windows and shows
//  automatically- generated text captions for audio and media streams. The
//  captions bubble's widget is a top-level window that has top z order and is
//  visible on all workspaces. It is draggable in and out of the tab.
//
class CaptionBubble : public views::BubbleDialogDelegateView,
                      public gfx::AnimationDelegate,
                      public TranslationViewWrapperBase::Delegate {
  METADATA_HEADER(CaptionBubble, views::BubbleDialogDelegateView)

 public:
  using NewFontListGetter = base::RepeatingCallback<gfx::FontList(
      const std::vector<std::string>& font_names,
      int font_style,
      int font_size,
      gfx::Font::Weight font_weight)>;

  CaptionBubble(
      CaptionBubbleSettings* caption_bubble_settings,
      std::unique_ptr<TranslationViewWrapperBase> translation_view_wrapper,
      const std::string& application_locale,
      base::OnceClosure destroyed_callback);
  CaptionBubble(const CaptionBubble&) = delete;
  CaptionBubble& operator=(const CaptionBubble&) = delete;
  ~CaptionBubble() override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

  // Sets the caption bubble model currently being used for this caption bubble.
  // There exists one CaptionBubble per profile, but one CaptionBubbleModel per
  // media stream. A new CaptionBubbleModel is set when transcriptions from a
  // different media stream are received. A CaptionBubbleModel is owned by the
  // CaptionBubbleControllerViews. It is created when transcriptions from a new
  // media stream are received and exists until the audio stream ends for that
  // stream.
  void SetModel(CaptionBubbleModel* model);

  // Changes the caption style of the caption bubble.
  void UpdateCaptionStyle(std::optional<ui::CaptionStyle> caption_style);

  views::Label* GetLabelForTesting();
  views::ScrollView* GetScrollViewForTesting();
  views::Label* GetDownloadProgressLabelForTesting();
  views::Label* GetScrollLockLabelForTesting();
  bool IsGenericErrorMessageVisibleForTesting() const;
  views::Button* GetCloseButtonForTesting();
  views::Button* GetBackToTabButtonForTesting();
  views::MdTextButton* GetScrollLockButtonForTesting();
  views::View* GetHeaderForTesting();
  TranslationViewWrapperBase* GetTranslationViewWrapperForTesting();
  void SetNewFontListGetterForTesting(NewFontListGetter callback);

  void SetCaptionBubbleStyle();

#if BUILDFLAG(IS_WIN)
  void OnContentSettingsLinkClicked();
#endif

  void UpdateControlsVisibility(bool show_controls);
  void OnMouseEnteredOrExitedWindow(bool entered);

  void SetTitleTextForTesting(const std::u16string title_text) {
    title_->SetText(title_text);
  }

 protected:
  // views::BubbleDialogDelegateView:
  void Init() override;
  void OnBeforeBubbleWidgetInit(views::Widget::InitParams* params,
                                views::Widget* widget) const override;
  bool ShouldShowCloseButton() const override;
  std::unique_ptr<views::NonClientFrameView> CreateNonClientFrameView(
      views::Widget* widget) override;
  gfx::Rect GetBubbleBounds() override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  std::u16string GetAccessibleWindowTitle() const override;
  void OnThemeChanged() override;

 private:
  friend class CaptionBubbleControllerViewsTest;
  friend class CaptionBubbleModel;
  FRIEND_TEST_ALL_PREFIXES(CaptionBubbleControllerViewsTest,
                           AccessibleProperties);

  void BackToTabButtonPressed();
  void CloseButtonPressed();
  void ExpandOrCollapseButtonPressed();
  void SwapButtons(views::Button* first_button,
                   views::Button* second_button,
                   bool show_first_button);
  // TranslationViewWrapperBase::Delegate:
  void CaptionSettingsButtonPressed() override;
  void ScrollLockButtonPressed();

  // Called by CaptionBubbleModel to notify this object that the model's text
  // has changed. Sets the text of the caption bubble to the model's text.
  void OnTextChanged();

  // Called by CaptionBubbleModel to notify this object that the model's
  // download progress text has changed. Sets the text of the caption bubble to
  // the model's download progress text.
  void OnDownloadProgressTextChanged();

  void OnLanguagePackInstalled();

  // Called by CaptionBubbleModel to notify this object that the model's
  // auto-detected language has changed.
  void OnAutoDetectedLanguageChanged();

  // Used to prevent propagating theme changes when no theme colors have
  // changed. Returns whether the caption theme colors have changed since the
  // last time this function was called.
  bool ThemeColorsChanged();

  // Called by CaptionBubbleModel to notify this object that the model's error
  // state has changed. Makes the caption bubble display an error message if
  // the model has an error, otherwise displays the latest text.
  void OnErrorChanged(CaptionBubbleErrorType error_type,
                      OnErrorClickedCallback callback,
                      OnDoNotShowAgainClickedCallback error_silenced_callback);

  // The caption bubble manages its own visibility based on whether there's
  // space for it to be shown, and if it has an error or text to display.
  void UpdateBubbleVisibility();
  void UpdateBubbleAndTitleVisibility();

  // For the provided line index, gets the corresponding rendered line in the
  // label and returns the text position of the first character of that line.
  // Returns the same value regardless of whether the label is visible or not.
  // TODO(crbug.com/40119836): This feature is launching for English first.
  // Make sure this is correct for all languages.
  size_t GetTextIndexOfLineInLabel(size_t line) const;

  // Returns the number of lines in the caption bubble label that are rendered.
  size_t GetNumLinesInLabel() const;
  int GetNumLinesVisible();

  // Internal service methods.
  void MaybeScrollToBottom();
  void UpdateContentSize();
  void Redraw();
  void ShowInactive();
  void Hide();

  // The following methods set the caption bubble style based on the user's
  // preferences, which are stored in `caption_style_`.
  double GetTextScaleFactor();
  const gfx::FontList GetFontList(int font_size);
  void SetTextSizeAndFontFamily();
  void SetTextColor();
  void SetBackgroundColor();

  // TranslationViewWrapperBase::Delegate:
  void OnLanguageChanged(const std::string& display_language) override;
  void UpdateLanguageDirection(const std::string& display_language) override;

  // Places the bubble at the bottom center of the context widget for the active
  // model, ensuring that it's positioned where the user will spot it. If there
  // are multiple browser windows open, and the user plays media on the second
  // window, the caption bubble will show up in the bottom center of the second
  // window, which is where the user is already looking. It also ensures that
  // the caption bubble will appear in the right workspace if a user has Chrome
  // windows open on multiple workspaces. This method has no effect if the
  // active model has changed between when it was posted and executed, which
  // is ensured by passing the active model's id as |model_id|.
  void RepositionInContextRect(CaptionBubbleModel::Id model_id,
                               const gfx::Rect& context_rect);

  void AdjustPosition(CaptionBubbleModel::Id model_id,
                      const gfx::Rect& context_rect);

  void MediaFoundationErrorCheckboxPressed();
  bool HasMediaFoundationError();

  void LogSessionEvent(SessionEvent event);

  std::vector<raw_ptr<views::View, VectorExperimental>> GetButtons();

  void OnTitleTextChanged();

  void UpdateAccessibleName();

  bool IsTranslateHeaderEnabled() const;

  bool IsScrollabilityEnabled() const;
  void ResetScrollIfLocked(gfx::PointF current_offset,
                           views::ScrollView* scrollable);

  // Unowned. Owned by views hierarchy.
  raw_ptr<CaptionBubbleLabel> label_;
  raw_ptr<CaptionBubbleScrollView> scrollable_;
  raw_ptr<views::Label> title_;
  raw_ptr<views::Label> generic_error_text_;
  raw_ptr<views::Label> download_progress_label_;
  raw_ptr<ScrollLockButton> scroll_lock_button_;
  raw_ptr<views::View> header_container_;
  raw_ptr<views::View> left_header_container_;
  raw_ptr<views::View> translate_header_container_;
  raw_ptr<views::ImageView> generic_error_icon_;
  raw_ptr<views::View> generic_error_message_;
  raw_ptr<views::ImageButton> back_to_tab_button_;
  raw_ptr<views::ImageButton> close_button_;
  raw_ptr<views::ImageButton> expand_button_;
  raw_ptr<views::ImageButton> collapse_button_;
  raw_ptr<CaptionBubbleFrameView> frame_;

#if BUILDFLAG(IS_WIN)
  raw_ptr<views::StyledLabel> media_foundation_renderer_error_text_;
  raw_ptr<views::ImageView> media_foundation_renderer_error_icon_;
  raw_ptr<views::View> media_foundation_renderer_error_message_;

  // Checkbox the user can use to indicate whether to silence the error message
  // for the origin.
  raw_ptr<views::Checkbox> media_foundation_renderer_error_checkbox_ = nullptr;
#endif

  std::optional<ui::CaptionStyle> caption_style_;
  raw_ptr<CaptionBubbleModel> model_ = nullptr;
  const raw_ptr<CaptionBubbleSettings> caption_bubble_settings_;
  std::unique_ptr<TranslationViewWrapperBase> translation_view_wrapper_;

  OnErrorClickedCallback error_clicked_callback_;
  OnDoNotShowAgainClickedCallback error_silenced_callback_;
  base::ScopedClosureRunner destroyed_callback_;

  const std::string application_locale_;

  // Whether the caption bubble is expanded to show more lines of text.
  bool is_expanded_;

  bool has_been_shown_ = false;

  // Used to determine whether to propagate theme changes to the widget.
  SkColor text_color_ = gfx::kPlaceholderColor;
  SkColor icon_color_ = gfx::kPlaceholderColor;
  SkColor icon_disabled_color_ = gfx::kPlaceholderColor;
  SkColor link_color_ = gfx::kPlaceholderColor;
  SkColor checkbox_color_ = gfx::kPlaceholderColor;
  SkColor background_color_ = gfx::kPlaceholderColor;

  gfx::SlideAnimation controls_animation_;

  bool render_active_ = false;
  bool mouse_inside_window_ = false;
  std::unique_ptr<CaptionBubbleEventObserver> caption_bubble_event_observer_;

  base::CallbackListSubscription title_text_changed_callback_;

  NewFontListGetter new_font_list_getter_;

  base::WeakPtrFactory<CaptionBubble> weak_ptr_factory_{this};
};

BEGIN_VIEW_BUILDER(/* no export */,
                   CaptionBubble,
                   views::BubbleDialogDelegateView)
END_VIEW_BUILDER

}  // namespace captions

DEFINE_VIEW_BUILDER(/* no export */, captions::CaptionBubble)

#endif  // COMPONENTS_LIVE_CAPTION_VIEWS_CAPTION_BUBBLE_H_
