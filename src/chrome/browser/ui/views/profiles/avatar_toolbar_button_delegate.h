// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_AVATAR_TOOLBAR_BUTTON_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_AVATAR_TOOLBAR_BUTTON_DELEGATE_H_

#include <optional>
#include <string>

#include "base/auto_reset.h"
#include "base/callback_list.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "google_apis/gaia/gaia_id.h"
#include "ui/base/models/image_model.h"

class Browser;
class Profile;
class AvatarToolbarButton;
enum class AvatarDelayType;

namespace ui {
class ColorProvider;
}

// Internal structures.
namespace internal {
class StateManager;
enum class ButtonState;
}  // namespace internal

// Handles the business logic for AvatarToolbarButton.
// Listens to Chrome and Profile changes in order to compute the proper state of
// the button. This state is used to compute the information requested by
// the button to be shown, such as Text and color, Icon, tooltip text etc...
// The different states that can be reached:
// - Regular state: regular browsing session.
// - Private mode: Incognito or Guest browser sessions.
// - Identity name shown: the identity name is shown for a short period of time.
//   This can be triggered by identity changes in Chrome or when an IPH is
//   showing.
// - Explicit modifications override: such as displaying specific text when
//   intercept bubbles are displayed.
// - Sync paused/error state.
//
// TODO(crbug.com/421821832): Consider merging this class with `StateManager` as
// after crrev.com/c/6616340 this class is not doing much anymore.
class AvatarToolbarButtonDelegate : public signin::IdentityManager::Observer {
 public:
  AvatarToolbarButtonDelegate(AvatarToolbarButton* button, Browser* browser);

  AvatarToolbarButtonDelegate(const AvatarToolbarButtonDelegate&) = delete;
  AvatarToolbarButtonDelegate& operator=(const AvatarToolbarButtonDelegate&) =
      delete;

  ~AvatarToolbarButtonDelegate() override;

  // Expected to be called once the avatar button view is properly added to the
  // widget. Expected to be called once to initialize the StateManager. Using
  // `state_manager_` can only be done after calling this method.
  void InitializeStateManager();
  bool IsStateManagerInitialized() const;

  void OnButtonPressed(bool is_source_accelerator);

  bool HasExplicitButtonState() const;

  // These info are based on the `ButtonState`.
  std::pair<std::u16string, std::optional<SkColor>> GetTextAndColor(
      const ui::ColorProvider& color_provider) const;
  std::optional<SkColor> GetHighlightTextColor(
      const ui::ColorProvider& color_provider) const;
  std::optional<std::u16string> GetAccessibilityLabel() const;
  std::u16string GetAvatarTooltipText() const;
  std::pair<ChromeColorIds, ChromeColorIds> GetInkdropColors() const;
  ui::ImageModel GetAvatarIcon(int icon_size,
                               SkColor icon_color,
                               const ui::ColorProvider& color_provider) const;
  bool ShouldPaintBorder() const;
  std::optional<base::RepeatingCallback<void(bool is_source_accelerator)>>
  GetButtonActionOverride();

  [[nodiscard]] base::ScopedClosureRunner SetExplicitButtonState(
      const std::u16string& text,
      std::optional<std::u16string> accessibility_label,
      std::optional<base::RepeatingCallback<void(bool is_source_accelerator)>>
          explicit_action);

  // Called by the AvatarToolbarButton to notify the delegate about events.
  void OnThemeChanged(const ui::ColorProvider* color_provider);

  // Testing functions: check `AvatarToolbarButton` equivalent functions.
  [[nodiscard]] static base::AutoReset<std::optional<base::TimeDelta>>
  CreateScopedInfiniteDelayOverrideForTesting(AvatarDelayType delay_type);
  void TriggerTimeoutForTesting(AvatarDelayType delay_type);
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  [[nodiscard]] static base::AutoReset<std::optional<base::TimeDelta>>
  CreateScopedZeroDelayOverrideSigninPendingTextForTesting();
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

 private:
  // signin::IdentityManager::Observer:
  void OnPrimaryAccountChanged(
      const signin::PrimaryAccountChangeEvent& event_details) override;
  void OnExtendedAccountInfoUpdated(const AccountInfo& info) override;
  void OnErrorStateOfRefreshTokenUpdatedForAccount(
      const CoreAccountInfo& account_info,
      const GoogleServiceAuthError& error,
      signin_metrics::SourceForRefreshTokenOperation token_operation_source)
      override;

  const raw_ptr<AvatarToolbarButton> avatar_toolbar_button_;
  const raw_ptr<Browser> browser_;
  const raw_ptr<Profile> profile_;
  const raw_ptr<signin::IdentityManager> identity_manager_;

  // Gaia Id of the account that was signed in from having it's choice
  // remembered following a web sign-in event but waiting for the available
  // account information to be fetched in order to show the sign in IPH.
  GaiaId gaia_id_for_signin_choice_remembered_;

  // Initialized in `InitializeStates()`.
  std::unique_ptr<internal::StateManager> state_manager_;

  base::ScopedObservation<signin::IdentityManager,
                          signin::IdentityManager::Observer>
      identity_manager_observation_{this};

  base::WeakPtrFactory<AvatarToolbarButtonDelegate> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_AVATAR_TOOLBAR_BUTTON_DELEGATE_H_
