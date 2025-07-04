// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/cookie_controls/cookie_controls_content_view.h"

#include "base/strings/string_util.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/controls/rich_controls_container_view.h"
#include "chrome/browser/ui/views/controls/rich_hover_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_settings/browser/ui/cookie_controls_util.h"
#include "components/content_settings/core/common/cookie_controls_enforcement.h"
#include "components/content_settings/core/common/cookie_controls_state.h"
#include "components/content_settings/core/common/features.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/strings/grit/privacy_sandbox_strings.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_features.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/md_text_button_with_spinner.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace {

using Util = ::content_settings::CookieControlsUtil;

constexpr int kMaxBubbleWidth = 1000;

int GetDefaultIconSize() {
  return GetLayoutConstant(PAGE_INFO_ICON_SIZE);
}

std::unique_ptr<views::View> CreateSeparator(bool padded) {
  int vmargin = ChromeLayoutProvider::Get()->GetDistanceMetric(
      DISTANCE_CONTENT_LIST_VERTICAL_MULTI);
  int hmargin = padded
                    ? ChromeLayoutProvider::Get()->GetDistanceMetric(
                          DISTANCE_HORIZONTAL_SEPARATOR_PADDING_PAGE_INFO_VIEW)
                    : 0;

  auto separator = std::make_unique<views::Separator>();
  separator->SetProperty(views::kMarginsKey, gfx::Insets::VH(vmargin, hmargin));
  return separator;
}

std::unique_ptr<views::View> CreateFullWidthSeparator() {
  return CreateSeparator(/*padded=*/false);
}

std::unique_ptr<views::View> CreatePaddedSeparator() {
  return CreateSeparator(/*padded=*/true);
}
}  // namespace

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView, kTitle);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView, kDescription);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView,
                                      kTrackingProtectionsButton);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView, kToggleButton);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView, kToggleLabel);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView,
                                      kThirdPartyCookiesLabel);
DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(CookieControlsContentView,
                                      kFeedbackButton);

CookieControlsContentView::CookieControlsContentView() {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  AddChildView(CreateFullWidthSeparator());
  AddContentLabels();
  AddTrackingProtectionsButton();
  AddToggleRow();
  AddFeedbackSection();
}

void CookieControlsContentView::AddContentLabels() {
  auto* provider = ChromeLayoutProvider::Get();
  const int vertical_margin =
      provider->GetDistanceMetric(DISTANCE_CONTENT_LIST_VERTICAL_MULTI);
  const int side_margin =
      provider->GetInsetsMetric(views::INSETS_DIALOG).left();

  label_wrapper_ = AddChildView(std::make_unique<views::View>());
  label_wrapper_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  label_wrapper_->SetProperty(views::kMarginsKey,
                              gfx::Insets::VH(vertical_margin, side_margin));
  title_ = label_wrapper_->AddChildView(std::make_unique<views::Label>());
  title_->SetTextContext(views::style::CONTEXT_DIALOG_BODY_TEXT);
  title_->SetTextStyle(views::style::STYLE_BODY_3_EMPHASIS);
  title_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  title_->SetProperty(views::kElementIdentifierKey, kTitle);

  description_ = label_wrapper_->AddChildView(std::make_unique<views::Label>());
  description_->SetTextContext(views::style::CONTEXT_LABEL);
  description_->SetTextStyle(views::style::STYLE_BODY_5);
  description_->SetHorizontalAlignment(gfx::HorizontalAlignment::ALIGN_LEFT);
  description_->SetMultiLine(true);
  description_->SetProperty(views::kElementIdentifierKey, kDescription);
}

void CookieControlsContentView::SetToggleIsOn(bool is_on) {
  toggle_button_->SetIsOn(is_on);
}

void CookieControlsContentView::SetToggleIcon(const gfx::VectorIcon& icon) {
  cookies_row_->SetIcon(ui::ImageModel::FromVectorIcon(icon, ui::kColorIcon,
                                                       GetDefaultIconSize()));
}

void CookieControlsContentView::SetToggleVisible(bool visible) {
  toggle_button_->SetVisible(visible);
}

void CookieControlsContentView::SetCookiesLabel(const std::u16string& label) {
  cookies_label_->SetText(label);
  cookies_label_->SetTextStyle(views::style::STYLE_BODY_5);
  cookies_label_->SetProperty(views::kElementIdentifierKey, kToggleLabel);

  // TODO(https://b/344856056): Update this accessibility label for the new UI.
  const std::u16string accessible_name = base::JoinString(
      {
          l10n_util::GetStringUTF16(
              IDS_COOKIE_CONTROLS_BUBBLE_THIRD_PARTY_COOKIES_LABEL),
          label,
      },
      u"\n");
  toggle_button_->GetViewAccessibility().SetName(accessible_name);
}

void CookieControlsContentView::SetEnforcedIcon(const gfx::VectorIcon& icon,
                                                const std::u16string& tooltip) {
  enforced_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      icon, ui::kColorIcon, GetDefaultIconSize()));
  enforced_icon_->SetTooltipText(tooltip);
}

void CookieControlsContentView::SetEnforcedIconVisible(bool visible) {
  if (enforced_icon_ != nullptr) {
    enforced_icon_->SetVisible(visible);
  }
}

void CookieControlsContentView::SetFeedbackSectionVisibility(bool visible) {
  if (visible && base::FeatureList::IsEnabled(
                     content_settings::features::kUserBypassFeedback)) {
    feedback_section_->SetVisible(true);
    // Ensure that the feedback row is always below ACT feature rows.
    ReorderChildView(feedback_section_, children().size());
  } else {
    feedback_section_->SetVisible(false);
  }
}

void CookieControlsContentView::SetCookiesRowVisible(bool visible) {
  cookies_row_->SetVisible(visible);
}

void CookieControlsContentView::SetTrackingProtectionsButtonVisible(
    bool visible) {
  tracking_protections_button_->SetVisible(visible);
}

void CookieControlsContentView::UpdateFeedbackButtonSubtitle(
    const std::u16string& subtitle) {
  feedback_button_->SetSubtitleText(subtitle);
}

void CookieControlsContentView::SetTrackingProtectionsButtonLabel(
    const std::u16string& label) {
  tracking_protections_button_->SetText(label);
  tracking_protections_button_->GetViewAccessibility().SetName(label);
}

void CookieControlsContentView::AddTrackingProtectionsButton() {
  auto* button_container =
      AddChildView(std::make_unique<views::BoxLayoutView>());
  button_container->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  tracking_protections_button_ = button_container->AddChildView(
      std::make_unique<views::MdTextButtonWithSpinner>(base::BindRepeating(
          &CookieControlsContentView::
              NotifyTrackingProtectionsButtonPressedCallback,
          base::Unretained(this))));

  tracking_protections_button_->SetProperty(views::kCrossAxisAlignmentKey,
                                            views::LayoutAlignment::kStart);
  const int controls_spacing = ChromeLayoutProvider::Get()->GetDistanceMetric(
      views::DISTANCE_RELATED_CONTROL_VERTICAL);
  ChromeLayoutProvider* layout_provider = ChromeLayoutProvider::Get();
  const int side_offset =
      layout_provider
          ->GetInsetsMetric(ChromeInsetsMetric::INSETS_PAGE_INFO_HOVER_BUTTON)
          .left();
  tracking_protections_button_->SetProperty(
      views::kMarginsKey, gfx::Insets::TLBR(controls_spacing, side_offset,
                                            controls_spacing, side_offset));
  tracking_protections_button_->SetProperty(views::kElementIdentifierKey,
                                            kTrackingProtectionsButton);
}

void CookieControlsContentView::AddToggleRow() {
  cookies_row_ = AddChildView(std::make_unique<RichControlsContainerView>());
  cookies_row_->SetTitle(l10n_util::GetStringUTF16(
      IDS_COOKIE_CONTROLS_BUBBLE_THIRD_PARTY_COOKIES_LABEL));

  // The label will be provided via SetCookiesLabel().
  cookies_label_ = cookies_row_->AddSecondaryLabel(u"");
  enforced_icon_ =
      cookies_row_->AddControl(std::make_unique<views::ImageView>());

  toggle_button_ = cookies_row_->AddControl(
      std::make_unique<views::ToggleButton>(base::BindRepeating(
          &CookieControlsContentView::NotifyToggleButtonPressedCallback,
          base::Unretained(this))));
  toggle_button_->SetPreferredSize(
      gfx::Size(toggle_button_->GetPreferredSize().width(),
                cookies_row_->GetFirstLineHeight()));
  toggle_button_->GetViewAccessibility().SetName(l10n_util::GetStringUTF16(
      IDS_COOKIE_CONTROLS_BUBBLE_THIRD_PARTY_COOKIES_LABEL));
  toggle_button_->SetVisible(true);
  toggle_button_->SetProperty(views::kElementIdentifierKey, kToggleButton);
}

void CookieControlsContentView::AddFeedbackSection() {
  feedback_section_ = AddChildView(std::make_unique<views::View>());
  feedback_section_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  const ui::ImageModel feedback_icon = ui::ImageModel::FromVectorIcon(
      kSubmitFeedbackIcon, ui::kColorMenuIcon, GetDefaultIconSize());
  const ui::ImageModel launch_icon = ui::ImageModel::FromVectorIcon(
      vector_icons::kLaunchIcon, ui::kColorMenuIcon, GetDefaultIconSize());

  feedback_section_->AddChildView(CreatePaddedSeparator());

  feedback_button_ =
      feedback_section_->AddChildView(std::make_unique<RichHoverButton>(
          base::BindRepeating(
              &CookieControlsContentView::NotifyFeedbackButtonPressedCallback,
              base::Unretained(this)),
          feedback_icon,
          l10n_util::GetStringUTF16(
              IDS_COOKIE_CONTROLS_BUBBLE_SEND_FEEDBACK_BUTTON_TITLE),
          l10n_util::GetStringUTF16(
              IDS_COOKIE_CONTROLS_BUBBLE_SEND_FEEDBACK_BUTTON_DESCRIPTION),
          launch_icon));

  feedback_button_->SetProperty(views::kElementIdentifierKey, kFeedbackButton);
  feedback_button_->SetTooltipText(l10n_util::GetStringUTF16(
      IDS_COOKIE_CONTROLS_BUBBLE_SEND_FEEDBACK_BUTTON_TITLE));
}

void CookieControlsContentView::SetTrackingProtectionsButtonReloadingState() {
  tracking_protections_button_->SetSpinnerVisible(true);
  tracking_protections_button_->SetText(l10n_util::GetStringUTF16(
      IDS_TRACKING_PROTECTIONS_BUBBLE_RELOADING_SITE_LABEL));
  tracking_protections_button_->SetEnabled(false);
}

void CookieControlsContentView::UpdateContentLabels(
    const std::u16string& title,
    const std::u16string& description) {
  title_->SetText(title);
  description_->SetText(description);
}

void CookieControlsContentView::SetContentLabelsVisible(bool visible) {
  // Set visibility on the wrapper to ensure that margins are correctly updated.
  label_wrapper_->SetVisible(visible);
}

CookieControlsContentView::~CookieControlsContentView() = default;

void CookieControlsContentView::PreferredSizeChanged() {
  views::View::PreferredSizeChanged();
}

gfx::Size CookieControlsContentView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  // Ensure that the width is only increased to support a longer title string,
  // or a longer toggle. Other information can be wrapped or elided to keep
  // the standard size.
  auto size = views::View::CalculatePreferredSize(available_size);

  auto* provider = ChromeLayoutProvider::Get();
  const int margins = provider->GetInsetsMetric(views::INSETS_DIALOG).width();

  int title_width = title_->GetPreferredSize().width() + margins;

  int desired_width = std::clamp(
      std::max(title_width, cookies_row_->GetPreferredSize().width()),
      ChromeLayoutProvider::Get()->GetDistanceMetric(
          views::DistanceMetric::DISTANCE_BUBBLE_PREFERRED_WIDTH),
      kMaxBubbleWidth);

  return gfx::Size(desired_width, size.height());
}

base::CallbackListSubscription
CookieControlsContentView::RegisterToggleButtonPressedCallback(
    base::RepeatingCallback<void(bool)> callback) {
  return toggle_button_callback_list_.Add(std::move(callback));
}

base::CallbackListSubscription
CookieControlsContentView::RegisterFeedbackButtonPressedCallback(
    base::RepeatingClosureList::CallbackType callback) {
  return feedback_button_callback_list_.Add(std::move(callback));
}

base::CallbackListSubscription
CookieControlsContentView::RegisterTrackingProtectionsButtonPressedCallback(
    base::RepeatingCallback<void()> callback) {
  return tracking_protections_button_callback_list_.Add(std::move(callback));
}

void CookieControlsContentView::
    NotifyTrackingProtectionsButtonPressedCallback() {
  tracking_protections_button_callback_list_.Notify();
}

void CookieControlsContentView::NotifyToggleButtonPressedCallback() {
  toggle_button_callback_list_.Notify(toggle_button_->GetIsOn());
}

void CookieControlsContentView::NotifyFeedbackButtonPressedCallback() {
  feedback_button_callback_list_.Notify();
}

BEGIN_METADATA(CookieControlsContentView)
END_METADATA
