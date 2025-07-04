// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_menu_coordinator.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_ui_util.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/user_education/browser_user_education_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/profiles/incognito_menu_view.h"
#include "chrome/browser/ui/views/profiles/profile_menu_view_base.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/user_education/common/feature_promo/feature_promo_controller.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

#if !BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ui/views/profiles/profile_menu_view.h"
#endif  // !BUILDFLAG(IS_CHROMEOS)

ProfileMenuCoordinator::~ProfileMenuCoordinator() = default;

void ProfileMenuCoordinator::Show(
    bool is_source_accelerator,
    std::optional<signin_metrics::AccessPoint> explicit_signin_access_point) {
  // TODO(crbug.com/425953501): Update this code.
  Browser* const browser = browser_->GetBrowserForMigrationOnly();
  auto* avatar_toolbar_button = BrowserView::GetBrowserViewForBrowser(browser)
                                    ->toolbar_button_provider()
                                    ->GetAvatarToolbarButton();

  // Do not show avatar bubble if there is no avatar menu button, the button
  // action is disabled or the bubble is already showing.
  if (!avatar_toolbar_button ||
      avatar_toolbar_button->IsButtonActionDisabled() || IsShowing()) {
    return;
  }

  signin_ui_util::RecordProfileMenuViewShown(profile_);
  // Close any existing IPH bubble for the profile menu.
  user_education_->NotifyFeaturePromoFeatureUsed(
      feature_engagement::kIPHProfileSwitchFeature,
      FeaturePromoFeatureUsedAction::kClosePromoIfPresent);
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  user_education_->NotifyFeaturePromoFeatureUsed(
      feature_engagement::kIPHSupervisedUserProfileSigninFeature,
      FeaturePromoFeatureUsedAction::kClosePromoIfPresent);
#endif

  std::unique_ptr<ProfileMenuViewBase> bubble;
  const bool is_incognito = profile_->IsIncognitoProfile();
  if (is_incognito) {
    bubble =
        std::make_unique<IncognitoMenuView>(avatar_toolbar_button, browser);
  } else {
#if BUILDFLAG(IS_CHROMEOS)
    // Note: on Ash, only incognito windows have a profile menu.
    NOTREACHED() << "The profile menu is not implemented on Ash.";
#else
    bubble = std::make_unique<ProfileMenuView>(avatar_toolbar_button, browser,
                                               explicit_signin_access_point);
#endif  // BUILDFLAG(IS_CHROMEOS)
  }
  bubble->SetProperty(views::kElementIdentifierKey,
                      kToolbarAvatarBubbleElementId);

  auto* bubble_ptr = bubble.get();
  DCHECK_EQ(nullptr, bubble_tracker_.view());
  bubble_tracker_.SetView(bubble_ptr);

  views::Widget* widget =
      views::BubbleDialogDelegateView::CreateBubble(std::move(bubble));
  bubble_ptr->CreateAXWidgetObserver(widget);
  widget->Show();
  if (is_source_accelerator) {
    bubble_ptr->FocusFirstProfileButton();
  }
}

bool ProfileMenuCoordinator::IsShowing() const {
  return bubble_tracker_.view() != nullptr;
}

ProfileMenuViewBase*
ProfileMenuCoordinator::GetProfileMenuViewBaseForTesting() {
  return IsShowing()
             ? views::AsViewClass<ProfileMenuViewBase>(bubble_tracker_.view())
             : nullptr;
}

ProfileMenuCoordinator::ProfileMenuCoordinator(BrowserWindowInterface* browser)
    : browser_(browser),
      profile_(browser->GetProfile()),
      user_education_(browser->GetUserEducationInterface()) {}
