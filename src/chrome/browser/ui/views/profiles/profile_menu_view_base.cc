// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_menu_view_base.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/metrics/histogram_functions.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_ui_util.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/profiles/profile_colors_util.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/chrome_typography.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/supervised_user/core/browser/family_link_user_capabilities.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/themed_vector_icon.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/layout/table_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

#if !BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ui/views/profiles/badged_profile_photo.h"
#endif  // !BUILDFLAG(IS_CHROMEOS)

namespace {

// Helpers --------------------------------------------------------------------

constexpr int kMenuWidth = 328;
constexpr int kMaxImageSize = ProfileMenuViewBase::kIdentityImageSize;
constexpr int kDefaultMargin = 8;
constexpr int kManagementHeaderIconLabelSpacing = 6;

// If the bubble is too large to fit on the screen, it still needs to be at
// least this tall to show one row.
constexpr int kMinimumScrollableContentHeight = 40;

// Spacing between the edge of the user menu and the top/bottom or left/right of
// the menu items.
constexpr int kMenuEdgeMargin = 16;

// Empty space between the rounded rectangle (outside) and menu edge.
constexpr int kIdentityContainerMargin = 12;

// Additional empty space between the menu item (e.g. icon or label) and the
// edge menu margin.
constexpr int kMenuItemLeftInternalPadding = 12;

constexpr char kProfileMenuClickedActionableItemHistogram[] =
    "Profile.Menu.ClickedActionableItem";
constexpr char kProfileMenuClickedActionableItemSupervisedHistogram[] =
    "Profile.Menu.ClickedActionableItem_Supervised";

gfx::ImageSkia SizeImage(const gfx::ImageSkia& image, int size) {
  return gfx::ImageSkiaOperations::CreateResizedImage(
      image, skia::ImageOperations::RESIZE_BEST, gfx::Size(size, size));
}

gfx::ImageSkia ColorImage(const gfx::ImageSkia& image, SkColor color) {
  return gfx::ImageSkiaOperations::CreateColorMask(image, color);
}

std::unique_ptr<views::BoxLayout> CreateBoxLayout(
    views::BoxLayout::Orientation orientation,
    views::BoxLayout::CrossAxisAlignment cross_axis_alignment,
    gfx::Insets insets = gfx::Insets()) {
  auto layout = std::make_unique<views::BoxLayout>(orientation, insets);
  layout->set_cross_axis_alignment(cross_axis_alignment);
  return layout;
}

const gfx::ImageSkia ImageForMenu(const gfx::VectorIcon& icon,
                                  float icon_to_image_ratio,
                                  SkColor color) {
  const int padding =
      static_cast<int>(kMaxImageSize * (1.0f - icon_to_image_ratio) / 2.0f);

  gfx::ImageSkia sized_icon =
      gfx::CreateVectorIcon(icon, kMaxImageSize - 2 * padding, color);
  return gfx::CanvasImageSource::CreatePadded(sized_icon, gfx::Insets(padding));
}

// Resizes and crops `image_model` to a circular shape.
// Note: if the image is backed by a vector icon, it is actually not cropped.
// Cropping it would require theme colors which are not necessarily available,
// and it is best to avoid cropping icons anyway -- icons naturally fitting in
// the circle should be used instead.
ui::ImageModel GetCircularSizedImage(const ui::ImageModel& image_model,
                                     int size) {
  // Resize.
  ui::ImageModel resized =
      profiles::GetSizedAvatarImageModel(image_model, size);
  // It is assumed that vector icons are already fitting in a circle. Only crop
  // images.
  if (!resized.IsImage()) {
    return resized;
  }
  return ui::ImageModel::FromImage(GetSizedAvatarIcon(
      resized.GetImage(), size, size, profiles::AvatarShape::SHAPE_CIRCLE));
}

class FeatureButtonIconView : public views::ImageView {
 public:
  FeatureButtonIconView(const gfx::VectorIcon& icon, float icon_to_image_ratio)
      : icon_(icon), icon_to_image_ratio_(icon_to_image_ratio) {}
  ~FeatureButtonIconView() override = default;

  // views::ImageView:
  void OnThemeChanged() override {
    views::ImageView::OnThemeChanged();
    constexpr int kIconSize = 16;
    const SkColor icon_color = GetColorProvider()->GetColor(ui::kColorIcon);
    gfx::ImageSkia image =
        ImageForMenu(*icon_, icon_to_image_ratio_, icon_color);
    SetImage(ui::ImageModel::FromImageSkia(
        SizeImage(ColorImage(image, icon_color), kIconSize)));
  }

 private:
  const raw_ref<const gfx::VectorIcon> icon_;
  const float icon_to_image_ratio_;
};

// AvatarImageView is used to ensure avatar adornments are kept in sync with
// current theme colors.
class AvatarImageView : public views::ImageView {
  METADATA_HEADER(AvatarImageView, views::ImageView)

 public:
  AvatarImageView(const ui::ImageModel& avatar_image,
                  const ProfileMenuViewBase* root_view,
                  int image_size,
                  int border_size,
                  bool has_dotted_ring)
      : avatar_image_(avatar_image),
        image_size_(image_size),
        border_size_(border_size),
        has_dotted_ring_(has_dotted_ring),
        root_view_(root_view) {
    if (avatar_image_.IsEmpty()) {
      // This can happen if the account image hasn't been fetched yet, if there
      // is no image, or in tests.
      avatar_image_ = ui::ImageModel::FromVectorIcon(
          kUserAccountAvatarIcon, ui::kColorMenuIcon, image_size_);
    }
  }

  // views::ImageView:
  void OnThemeChanged() override {
    ImageView::OnThemeChanged();
    DCHECK(!avatar_image_.IsEmpty());
    ui::ColorProvider* color_provider = GetColorProvider();
    CHECK(color_provider);
    gfx::ImageSkia sized_avatar_image;
    if (has_dotted_ring_) {
      const int size_with_border = image_size_ + 2 * border_size_;
      sized_avatar_image = profiles::GetAvatarWithDottedRing(
          avatar_image_, size_with_border, /*has_padding=*/true,
          /*has_background=*/true, *color_provider);
      // Dotted ring avatar does not support a border, as the border is already
      // included with the dotted ring.
      CHECK_EQ(border_size_, 0);
    } else {
      if (border_size_ > 0) {
        // Total image size is `image_size_ + 2 * border_size_`.
        ui::ImageModel sized_avatar_image_without_border =
            GetCircularSizedImage(avatar_image_, image_size_);
        sized_avatar_image = gfx::CanvasImageSource::CreatePadded(
            sized_avatar_image_without_border.Rasterize(color_provider),
            gfx::Insets(border_size_));
      } else {
        sized_avatar_image =
            profiles::GetSizedAvatarImageModel(avatar_image_, image_size_)
                .Rasterize(color_provider);
      }
      sized_avatar_image = profiles::AddBackgroundToImage(sized_avatar_image,
                                                          GetBackgroundColor());
    }
    gfx::Image circular_sized_avatar_image = profiles::GetSizedAvatarIcon(
        gfx::Image(sized_avatar_image), sized_avatar_image.size().width(),
        sized_avatar_image.size().height(),
        profiles::AvatarShape::SHAPE_CIRCLE);
    SetImage(ui::ImageModel::FromImageSkia(
        *circular_sized_avatar_image.ToImageSkia()));
  }

 private:
  SkColor GetBackgroundColor() const {
    return GetColorProvider()->GetColor(ui::kColorBubbleBackground);
  }

  ui::ImageModel avatar_image_;
  const int image_size_;
  const int border_size_;
  const bool has_dotted_ring_;
  raw_ptr<const ProfileMenuViewBase> root_view_;
};

BEGIN_METADATA(AvatarImageView)
END_METADATA

}  // namespace

ProfileMenuViewBase::IdentitySectionParams::IdentitySectionParams() = default;
ProfileMenuViewBase::IdentitySectionParams::~IdentitySectionParams() = default;
ProfileMenuViewBase::IdentitySectionParams::IdentitySectionParams(
    IdentitySectionParams&&) = default;
ProfileMenuViewBase::IdentitySectionParams&
ProfileMenuViewBase::IdentitySectionParams::operator=(IdentitySectionParams&&) =
    default;

// ProfileMenuViewBase ---------------------------------------------------------

ProfileMenuViewBase::ProfileMenuViewBase(views::Button* anchor_button,
                                         Browser* browser)
    : BubbleDialogDelegateView(anchor_button, views::BubbleBorder::TOP_RIGHT),
      profile_(raw_ref<Profile>::from_ptr(browser->profile())),
      anchor_button_(anchor_button),
      close_bubble_helper_(this, browser->tab_strip_model()) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_margins(gfx::Insets(0));
  DCHECK(anchor_button);
  views::InkDrop::Get(anchor_button)
      ->AnimateToState(views::InkDropState::ACTIVATED, nullptr);

  SetEnableArrowKeyTraversal(true);

  // TODO(crbug.com/40230528): Using `SetAccessibleWindowRole(kMenu)` here will
  // result in screenreader to announce the menu having only one item. This is
  // probably because this API sets the a11y role for the widget, but not root
  // view in it. This is confusing and prone to misuse. We should unify the two
  // sets of API for BubbleDialogDelegateView.
  GetViewAccessibility().SetRole(ax::mojom::Role::kMenu);

  RegisterWindowClosingCallback(base::BindOnce(
      &ProfileMenuViewBase::OnWindowClosing, base::Unretained(this)));

  SetBackground(views::CreateSolidBackground(kColorProfileMenuBackground));
}

ProfileMenuViewBase::~ProfileMenuViewBase() = default;

void ProfileMenuViewBase::SetProfileIdentityWithCallToAction(
    IdentitySectionParams params) {
  constexpr int kHeaderVerticalSize = 36;
  constexpr int kHeaderImageSize = 16;
  constexpr int kIdentityContainerHorizontalPadding = 24;
  constexpr int kAvatarTopMargin = 24;
  constexpr int kTitleTopMargin = 8;
  constexpr int kBottomMarginWhenNoButton = 24;
  constexpr int kSubtitleBottomMarginWithButton = 12;
  constexpr int kButtonBottomMargin = 28;

  // Vertical view structure when all elements are present. Square brackets []
  // represent empty space:
  //
  // Optional header:
  //     HoverButton: (size: kHeaderVerticalSize)
  //     Horizontal Separator
  // [kAvatarTopMargin]
  // Image: Avatar (size: kIdentityInfoImageSize)
  // [kTitleTopMargin]
  // Label: Title
  // Optional:
  //     Label: Subtitle (optional)
  //     [kSubtitleBottomMarginWithButton] (or [kBottomMarginWhenNoButton])
  // Optional:
  //     Button: maybe with an image inside
  //     [kButtonBottomMargin]
  //
  // Note: If a button is present, a subtitle must also be present. The layout
  // does not support a button without subtitle.

  identity_info_container_->RemoveAllChildViews();
  // Vertical BoxLayout.
  auto box_layout =
      CreateBoxLayout(views::BoxLayout::Orientation::kVertical,
                      views::BoxLayout::CrossAxisAlignment::kCenter);
  box_layout->SetCollapseMarginsSpacing(true);
  identity_info_container_->SetLayoutManager(std::move(box_layout));

  // Paint to a layer with rounded corners. This ensures that no element can
  // draw outside of the rounded corners, even if they use layers. This is
  // needed in particular for the HoverButton highlight.
  identity_info_container_->SetPaintToLayer();
  identity_info_container_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(views::LayoutProvider::Get()->GetCornerRadiusMetric(
          views::Emphasis::kHigh)));

  // No need to set rounded corners on the background, because the container
  // is painted in a layer that has rounded corners already.
  identity_info_container_->SetBackground(
      views::CreateSolidBackground(kColorProfileMenuIdentityInfoBackground));

  // Space around the rectangle, between the rectangle and the menu edge.
  identity_info_container_->SetProperty(views::kMarginsKey,
                                        gfx::Insets(kIdentityContainerMargin));

  if (!params.header_string.empty() && !params.header_image.IsEmpty()) {
    // Header.
    auto hover_button = std::make_unique<HoverButton>(
        std::move(params.header_action),
        std::make_unique<views::ImageView>(
            GetCircularSizedImage(params.header_image, kHeaderImageSize)),
        params.header_string, std::u16string(), nullptr, true, std::u16string(),
        kManagementHeaderIconLabelSpacing);
    hover_button->SetPreferredSize(gfx::Size(
        kMenuWidth - 2 * kIdentityContainerMargin, kHeaderVerticalSize));
    hover_button->SetIconHorizontalMargins(0, 0);
    hover_button->title()->SetTextStyle(views::style::STYLE_BODY_5);

    // Swap the layout manager so that the text is centered.
    auto hover_button_box_layout = std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal);
    hover_button_box_layout->set_main_axis_alignment(
        views::LayoutAlignment::kCenter);
    hover_button->SetLayoutManager(std::move(hover_button_box_layout));
    identity_info_container_->AddChildView(std::move(hover_button));

    // Separator.
    identity_info_container_->AddChildView(
        views::Builder<views::Separator>()
            .SetColorId(kColorProfileMenuBackground)
            .SetPreferredSize(
                gfx::Size(kMenuWidth, views::Separator::kThickness))
            .Build());
  }

  // Avatar.
  identity_info_container_->AddChildView(
      views::Builder<views::View>(
          std::make_unique<AvatarImageView>(
              params.profile_image, this,
              kIdentityInfoImageSize - 2 * params.profile_image_padding,
              params.profile_image_padding, params.has_dotted_ring))
          .SetProperty(views::kMarginsKey,
                       gfx::Insets().set_top(kAvatarTopMargin))
          .Build());

  // Title.
  const bool has_subtitle = !params.subtitle.empty();
  const bool has_button = !params.button_text.empty();
  const int title_bottom_margin = has_subtitle ? 0 : kBottomMarginWhenNoButton;
  identity_info_container_->AddChildView(
      views::Builder<views::Label>()
          .SetText(params.title)
          .SetTextContext(views::style::CONTEXT_LABEL)
          .SetTextStyle(views::style::STYLE_BODY_3_MEDIUM)
          .SetElideBehavior(gfx::ELIDE_TAIL)
          .SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kTitleTopMargin,
                                         kIdentityContainerHorizontalPadding,
                                         title_bottom_margin,
                                         kIdentityContainerHorizontalPadding))
          .SetEnabledColor(kColorProfileMenuIdentityInfoTitle)
          .Build());
  if (!has_subtitle) {
    CHECK(!has_button);
    return;
  }

  // Subtitle.

  // Set the subtitle as the name of the parent container, so accessibility
  // tools can read it together with the button text. The role change is
  // required by Windows ATs.
  identity_info_container_->GetViewAccessibility().SetRole(
      ax::mojom::Role::kGroup);
  identity_info_container_->GetViewAccessibility().SetName(
      params.subtitle, ax::mojom::NameFrom::kAttribute);

  const int subtitle_bottom_margin =
      has_button ? kSubtitleBottomMarginWithButton : kBottomMarginWhenNoButton;
  identity_info_container_->AddChildView(
      views::Builder<views::Label>()
          .SetText(params.subtitle)
          .SetTextContext(views::style::CONTEXT_LABEL)
          .SetTextStyle(views::style::STYLE_BODY_4)
          .SetMultiLine(true)
          .SetHandlesTooltips(false)
          .SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(0, kIdentityContainerHorizontalPadding,
                                         subtitle_bottom_margin,
                                         kIdentityContainerHorizontalPadding))
          .SetEnabledColor(kColorProfileMenuIdentityInfoSubtitle)
          .Build());

  if (!has_button) {
    return;
  }

  // Button.
  identity_info_container_->AddChildView(
      views::Builder<views::MdTextButton>()
          .SetText(params.button_text)
          .SetCallback(base::BindRepeating(&ProfileMenuViewBase::ButtonPressed,
                                           base::Unretained(this),
                                           std::move(params.button_action)))
          .SetStyle(ui::ButtonStyle::kProminent)
          .SetProperty(views::kMarginsKey,
                       gfx::Insets().set_bottom(kButtonBottomMargin))
          .SetImageModel(views::Button::STATE_NORMAL, params.button_image)
          .Build());
}

void ProfileMenuViewBase::AddPromoButton(const std::u16string& text,
                                         base::RepeatingClosure action,
                                         const gfx::VectorIcon& icon) {
  // Initialize layout if this is the first time a button is added.
  if (!promo_container_->GetLayoutManager()) {
    promo_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
  }

  // Do not allow more than 2 promos to be shown at the same time in the Profile
  // Menu. Currently there only exist two types of promos - if we ever need to
  // support more, a more complex logic to decide which promo to be shown is
  // needed.
  if (promo_container_->children().size() == 2u) {
    return;
  }

  // Only first and last buttons should have the rounded corners.
  bool is_first_button_being_added = promo_container_->children().empty();

  // Reset the last button bottom corners since it will not be the last one
  // anymore.
  if (!is_first_button_being_added) {
    HoverButton* last_child =
        views::AsViewClass<HoverButton>(promo_container_->children().back());
    std::optional<gfx::RoundedCornersF> current_rounded_corners =
        last_child->background()->GetRoundedCornerRadii();
    CHECK(current_rounded_corners.has_value());
    current_rounded_corners->set_lower_left(0.f);
    current_rounded_corners->set_lower_right(0.f);
    // Override the background with the updated corners.
    last_child->SetBackground(views::CreateRoundedRectBackground(
        kColorProfileMenuPromoButtonsBackground,
        current_rounded_corners.value(),
        gfx::Insets::VH(0, kIdentityContainerMargin)));
  }

  // Add background color for promos.
  constexpr int kBackgroundCornerSize = 8;
  constexpr int kButtonBackgroundVerticalSize = 40;
  constexpr int kPromoSeparation = 2;

  std::unique_ptr<HoverButton> button = CreateMenuRowButton(
      std::move(action), std::make_unique<FeatureButtonIconView>(icon, 1.0f),
      text);

  // The current button being added to the end, we can already set the bottom
  // corners.
  gfx::RoundedCornersF rounded_corners(0.f, 0.f, kBackgroundCornerSize,
                                       kBackgroundCornerSize);
  // First element should have upper corners rounded.
  if (is_first_button_being_added) {
    rounded_corners.set_upper_left(kBackgroundCornerSize);
    rounded_corners.set_upper_right(kBackgroundCornerSize);
  }
  button->SetBackground(views::CreateRoundedRectBackground(
      kColorProfileMenuPromoButtonsBackground, rounded_corners,
      gfx::Insets::VH(0, kIdentityContainerMargin)));
  // Button with a background should have a larger size to fit the background.
  button->SetPreferredSize(
      gfx::Size(kMenuWidth, kButtonBackgroundVerticalSize));

  // When adding the first element in the promo container, ensure a separation
  // between the promo container and the next container. Otherwise, add a top
  // margin to the button to add a separatation with the previous promos.
  if (is_first_button_being_added) {
    promo_container_->SetProperty(views::kMarginsKey,
                                  gfx::Insets().set_bottom(kDefaultMargin));
  } else {
    button->SetProperty(views::kMarginsKey,
                        gfx::Insets().set_top(kPromoSeparation));
  }

  promo_container_->AddChildView(std::move(button));
}

void ProfileMenuViewBase::AddFeatureButton(const std::u16string& text,
                                           base::RepeatingClosure action,
                                           const gfx::VectorIcon& icon,
                                           float icon_to_image_ratio) {
  // Initialize layout if this is the first time a button is added.
  if (!features_container_->GetLayoutManager()) {
    features_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
  }

  features_container_->AddChildView(CreateMenuRowButton(
      std::move(action),
      std::make_unique<FeatureButtonIconView>(icon, icon_to_image_ratio),
      text));
}

void ProfileMenuViewBase::SetProfileManagementHeading(
    const std::u16string& heading) {
  profile_mgmt_heading_ = heading;

  // Add separator before heading.
  profile_mgmt_separator_container_->RemoveAllChildViews();
  profile_mgmt_separator_container_->SetLayoutManager(
      std::make_unique<views::FillLayout>());
  profile_mgmt_separator_container_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(kDefaultMargin, 0)));
  profile_mgmt_separator_container_->AddChildView(
      std::make_unique<views::Separator>());

  // Initialize heading layout.
  profile_mgmt_heading_container_->RemoveAllChildViews();
  profile_mgmt_heading_container_->SetLayoutManager(
      std::make_unique<views::FillLayout>());
  profile_mgmt_heading_container_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(
          kDefaultMargin, kMenuEdgeMargin + kMenuItemLeftInternalPadding,
          kDefaultMargin, kMenuEdgeMargin)));

  // Add heading.
  views::Label* label = profile_mgmt_heading_container_->AddChildView(
      std::make_unique<views::Label>(heading, views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_3_EMPHASIS));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetHandlesTooltips(false);
}

void ProfileMenuViewBase::AddAvailableProfile(const ui::ImageModel& image_model,
                                              const std::u16string& name,
                                              bool is_guest,
                                              base::RepeatingClosure action) {
  // Initialize layout if this is the first time a button is added.
  if (!selectable_profiles_container_->GetLayoutManager()) {
    selectable_profiles_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical));
    // Give the container an accessible name so accessibility tools can provide
    // context for the buttons inside it. The role change is required by Windows
    // ATs.
    selectable_profiles_container_->GetViewAccessibility().SetRole(
        ax::mojom::Role::kGroup);
    selectable_profiles_container_->GetViewAccessibility().SetName(
        profile_mgmt_heading_, ax::mojom::NameFrom::kAttribute);
  }

  DCHECK(!image_model.IsEmpty());
  ui::ImageModel sized_image =
      GetCircularSizedImage(image_model, kOtherProfileImageSize);
  views::Button* button =
      selectable_profiles_container_->AddChildView(CreateMenuRowButton(
          std::move(action), std::make_unique<views::ImageView>(sized_image),
          name));

  if (!is_guest && !first_profile_button_) {
    first_profile_button_ = button;
  }
}

void ProfileMenuViewBase::AddProfileManagementFeaturesSeparator() {
  // Add separator before profile management features.
  profile_mgmt_features_separator_container_->RemoveAllChildViews();
  profile_mgmt_features_separator_container_->SetLayoutManager(
      std::make_unique<views::FillLayout>());
  profile_mgmt_features_separator_container_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(kDefaultMargin, 0)));
  profile_mgmt_features_separator_container_->AddChildView(
      std::make_unique<views::Separator>());
}

void ProfileMenuViewBase::AddProfileManagementFeatureButton(
    const gfx::VectorIcon& icon,
    const std::u16string& text,
    base::RepeatingClosure action) {
  // Initialize layout if this is the first time a button is added.
  AddBottomMargin();

  auto icon_view =
      std::make_unique<FeatureButtonIconView>(icon, /*icon_to_image_ratio=*/1);
  profile_mgmt_features_container_->AddChildView(
      CreateMenuRowButton(std::move(action), std::move(icon_view), text));
}

void ProfileMenuViewBase::AddBottomMargin() {
  // Create an empty container with a bottom margin.
  if (!profile_mgmt_features_container_->GetLayoutManager()) {
    profile_mgmt_features_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical));
    profile_mgmt_features_container_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, kDefaultMargin, 0)));
  }
}

void ProfileMenuViewBase::RecordClick(ActionableItem item) {
  // TODO(tangltom): Separate metrics for incognito and guest menu.
  base::UmaHistogramEnumeration(kProfileMenuClickedActionableItemHistogram,
                                item);
  // Additionally output a version of the metric for supervised users, to allow
  // more fine-grained analysis.
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfileIfExists(&profile());
  if (identity_manager &&
      supervised_user::IsPrimaryAccountSubjectToParentalControls(
          identity_manager) == signin::Tribool::kTrue) {
    base::UmaHistogramEnumeration(
        kProfileMenuClickedActionableItemSupervisedHistogram, item);
  }
}

int ProfileMenuViewBase::GetMaxHeight() const {
  gfx::Rect anchor_rect = GetAnchorRect();
  gfx::Rect screen_space =
      display::Screen::GetScreen()
          ->GetDisplayNearestPoint(anchor_rect.CenterPoint())
          .work_area();
  int available_space = screen_space.bottom() - anchor_rect.bottom();
#if BUILDFLAG(IS_WIN)
  // On Windows the bubble can also be show to the top of the anchor.
  available_space =
      std::max(available_space, anchor_rect.y() - screen_space.y());
#endif
  return std::max(kMinimumScrollableContentHeight, available_space);
}

void ProfileMenuViewBase::Reset() {
  RemoveAllChildViews();

  auto components = std::make_unique<views::View>();
  components->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical);

  // Create and add new component containers in the correct order.
  // First, add the parts of the current profile.
  identity_info_container_ =
      components->AddChildView(std::make_unique<views::View>());
  promo_container_ = components->AddChildView(std::make_unique<views::View>());
  features_container_ =
      components->AddChildView(std::make_unique<views::View>());
  profile_mgmt_separator_container_ =
      components->AddChildView(std::make_unique<views::View>());
  // Second, add the profile management header. This includes the heading and
  // the shortcut feature(s) next to it.
  auto profile_mgmt_header = std::make_unique<views::View>();
  views::BoxLayout* profile_mgmt_header_layout =
      profile_mgmt_header->SetLayoutManager(
          CreateBoxLayout(views::BoxLayout::Orientation::kHorizontal,
                          views::BoxLayout::CrossAxisAlignment::kCenter));
  profile_mgmt_heading_container_ =
      profile_mgmt_header->AddChildView(std::make_unique<views::View>());
  profile_mgmt_header_layout->SetFlexForView(profile_mgmt_heading_container_,
                                             1);
  components->AddChildView(std::move(profile_mgmt_header));
  // Third, add the profile management buttons.
  selectable_profiles_container_ =
      components->AddChildView(std::make_unique<views::View>());
  profile_mgmt_features_separator_container_ =
      components->AddChildView(std::make_unique<views::View>());
  profile_mgmt_features_container_ =
      components->AddChildView(std::make_unique<views::View>());
  first_profile_button_ = nullptr;

  // Create a scroll view to hold the components.
  auto scroll_view = std::make_unique<views::ScrollView>();
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // TODO(crbug.com/41406562): it's a workaround for the crash.
  scroll_view->SetDrawOverflowIndicator(false);
  scroll_view->ClipHeightTo(0, GetMaxHeight());
  scroll_view->SetContents(std::move(components));

  // Create a table layout to set the menu width.
  SetLayoutManager(std::make_unique<views::TableLayout>())
      ->AddColumn(
          views::LayoutAlignment::kStretch, views::LayoutAlignment::kStretch,
          views::TableLayout::kFixedSize,
          views::TableLayout::ColumnSize::kFixed, kMenuWidth, kMenuWidth)
      .AddRows(1, 1.0f);
  AddChildView(std::move(scroll_view));
}

void ProfileMenuViewBase::FocusFirstProfileButton() {
  if (first_profile_button_) {
    first_profile_button_->RequestFocus();
  }
}

void ProfileMenuViewBase::Init() {
  Reset();
  BuildMenu();
}

void ProfileMenuViewBase::OnWindowClosing() {
  if (!anchor_button()) {
    return;
  }

  views::InkDrop::Get(anchor_button())
      ->AnimateToState(views::InkDropState::DEACTIVATED, nullptr);
}

bool ProfileMenuViewBase::HandleContextMenu(
    content::RenderFrameHost& render_frame_host,
    const content::ContextMenuParams& params) {
  // Suppresses the context menu because some features, such as inspecting
  // elements, are not appropriate in a bubble.
  return true;
}

void ProfileMenuViewBase::ButtonPressed(base::RepeatingClosure action) {
  DCHECK(action);
  signin_ui_util::RecordProfileMenuClick(profile());
  action.Run();
}

void ProfileMenuViewBase::CreateAXWidgetObserver(views::Widget* widget) {
  ax_widget_observer_ = std::make_unique<AXMenuWidgetObserver>(this, widget);
}

std::unique_ptr<HoverButton> ProfileMenuViewBase::CreateMenuRowButton(
    base::RepeatingClosure action,
    std::unique_ptr<views::View> icon_view,
    const std::u16string& text) {
  CHECK(icon_view);
  auto button = std::make_unique<HoverButton>(
      base::BindRepeating(&ProfileMenuViewBase::ButtonPressed,
                          base::Unretained(this), std::move(action)),
      std::move(icon_view), text, /*subtitle=*/std::u16string(),
      /*secondary_view=*/nullptr, /*add_vertical_label_spacing=*/false);
  button->SetIconHorizontalMargins(kMenuItemLeftInternalPadding, /*right=*/0);
  return button;
}

// Despite ProfileMenuViewBase being a dialog, we are enforcing it to behave
// like a menu from the accessibility POV because it fits better with a menu UX.
// The dialog exposes the kMenuBar role, and the top-level container is kMenu.
// This class is responsible for emitting menu accessible events when the dialog
// is activated or deactivated.
class ProfileMenuViewBase::AXMenuWidgetObserver : public views::WidgetObserver {
 public:
  AXMenuWidgetObserver(ProfileMenuViewBase* owner, views::Widget* widget)
      : owner_(owner) {
    observation_.Observe(widget);
  }
  ~AXMenuWidgetObserver() override = default;

  void OnWidgetActivationChanged(views::Widget* widget, bool active) override {
    if (active) {
      owner_->NotifyAccessibilityEventDeprecated(ax::mojom::Event::kMenuStart,
                                                 true);
      owner_->NotifyAccessibilityEventDeprecated(
          ax::mojom::Event::kMenuPopupStart, true);
    } else {
      owner_->NotifyAccessibilityEventDeprecated(
          ax::mojom::Event::kMenuPopupEnd, true);
      owner_->NotifyAccessibilityEventDeprecated(ax::mojom::Event::kMenuEnd,
                                                 true);
    }
  }

 private:
  raw_ptr<ProfileMenuViewBase> owner_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

BEGIN_METADATA(ProfileMenuViewBase)
END_METADATA
