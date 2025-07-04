// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/profile_picker_ui.h"

#include "base/feature_list.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/buildflag.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/browser_signin_policy_handler.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/managed_ui.h"
#include "chrome/browser/ui/profiles/profile_picker.h"
#include "chrome/browser/ui/webui/signin/profile_picker_handler.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/chrome_unscaled_resources.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/profile_picker_resources.h"
#include "chrome/grit/profile_picker_resources_map.h"
#include "chrome/grit/signin_resources.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_service.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/webui/resource_path.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/webui/webui_util.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_GLIC)
#include "chrome/browser/glic/resources/glic_resources.h"
#include "chrome/browser/glic/resources/grit/glic_browser_resources.h"
#endif

namespace {

// Minimum size for the picker UI.
constexpr int kMinimumPickerSizePx = 620;

bool IsBrowserSigninAllowed() {
#if BUILDFLAG(IS_CHROMEOS)
  return true;
#else
  policy::PolicyService* policy_service = g_browser_process->policy_service();
  DCHECK(policy_service);
  const policy::PolicyMap& policies = policy_service->GetPolicies(
      policy::PolicyNamespace(policy::POLICY_DOMAIN_CHROME, std::string()));

  const base::Value* browser_signin_value = policies.GetValue(
      policy::key::kBrowserSignin, base::Value::Type::INTEGER);

  if (!browser_signin_value) {
    return true;
  }

  return static_cast<policy::BrowserSigninMode>(
             browser_signin_value->GetInt()) !=
         policy::BrowserSigninMode::kDisabled;
#endif
}

std::string GetManagedDeviceDisclaimer() {
  std::optional<std::string> manager = GetDeviceManagerIdentity();
  if (!manager) {
    return std::string();
  }

  if (manager->empty()) {
    return l10n_util::GetStringUTF8(
        IDS_PROFILE_PICKER_PROFILE_CREATION_FLOW_DEVICE_MANAGED_DESCRIPTION);
  }

  return l10n_util::GetStringFUTF8(
      IDS_PROFILE_PICKER_PROFILE_CREATION_FLOW_DEVICE_MANAGED_BY_DESCRIPTION,
      base::UTF8ToUTF16(*manager));
}

int GetMainViewTitleId(bool is_glic_version) {
#if BUILDFLAG(ENABLE_GLIC)
  if (is_glic_version) {
    return IDS_PROFILE_PICKER_MAIN_VIEW_TITLE_GLIC;
  }
#endif

  return ProfilePicker::Shown() ? IDS_PROFILE_PICKER_MAIN_VIEW_TITLE_V2
                                : IDS_PROFILE_PICKER_MAIN_VIEW_TITLE;
}

void AddStrings(content::WebUIDataSource* html_source, bool is_glic_version) {
  constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"addSpaceButton", IDS_PROFILE_PICKER_ADD_SPACE_BUTTON},
      {"askOnStartupCheckboxText", IDS_PROFILE_PICKER_ASK_ON_STARTUP},
      {"browseAsGuestButton", IDS_PROFILE_PICKER_BROWSE_AS_GUEST_BUTTON},
      {"controlledSettingPolicy", IDS_MANAGED},
      {"needsSigninPrompt",
       IDS_PROFILE_PICKER_PROFILE_CARD_NEEDS_SIGNIN_PROMPT},
      {"profileCardInputLabel", IDS_PROFILE_PICKER_PROFILE_CARD_INPUT_LABEL},
      {"menu", IDS_MENU},
      {"cancel", IDS_CANCEL},
      {"profileMenuName", IDS_SETTINGS_MORE_ACTIONS},
      {"profileMenuAriaLabel",
       IDS_PROFILE_PICKER_PROFILE_MORE_ACTIONS_ARIA_LABEL},
      {"profileMenuRemoveText", IDS_PROFILE_PICKER_PROFILE_MENU_REMOVE_TEXT},
      {"profileMenuCustomizeText",
       IDS_PROFILE_PICKER_PROFILE_MENU_CUSTOMIZE_TEXT},
      {"removeWarningLocalProfileTitle",
       IDS_PROFILE_PICKER_REMOVE_WARNING_LOCAL_PROFILE_TITLE},
      {"removeWarningSignedInProfileTitle",
       IDS_PROFILE_PICKER_REMOVE_WARNING_SIGNED_IN_PROFILE_TITLE},
      {"removeWarningHistory", IDS_PROFILE_PICKER_REMOVE_WARNING_HISTORY},
      {"removeWarningPasswords", IDS_PROFILE_PICKER_REMOVE_WARNING_PASSWORDS},
      {"removeWarningBookmarks", IDS_PROFILE_PICKER_REMOVE_WARNING_BOOKMARKS},
      {"removeWarningAutofill", IDS_PROFILE_PICKER_REMOVE_WARNING_AUTOFILL},
      {"removeWarningCalculating",
       IDS_PROFILE_PICKER_REMOVE_WARNING_CALCULATING},
      {"backButtonAriaLabel", IDS_PROFILE_PICKER_BACK_BUTTON_ARIA_LABEL},
      {"profileTypeChoiceTitle",
       IDS_PROFILE_PICKER_PROFILE_CREATION_FLOW_PROFILE_TYPE_CHOICE_TITLE},
      {"notNowButtonLabel",
       IDS_PROFILE_PICKER_PROFILE_CREATION_FLOW_NOT_NOW_BUTTON_LABEL},
      {"profileSwitchTitle", IDS_PROFILE_PICKER_PROFILE_SWITCH_TITLE},
      {"profileSwitchSubtitle", IDS_PROFILE_PICKER_PROFILE_SWITCH_SUBTITLE},
      {"switchButtonLabel",
       IDS_PROFILE_PICKER_PROFILE_SWITCH_SWITCH_BUTTON_LABEL},
      {"removeWarningLocalProfile",
       IDS_PROFILE_PICKER_REMOVE_WARNING_LOCAL_PROFILE},
      {"removeWarningSignedInProfile",
       IDS_PROFILE_PICKER_REMOVE_WARNING_SIGNED_IN_PROFILE},
      {"ok", IDS_OK},
      {"signInButtonLabel",
       IDS_PROFILE_PICKER_PROFILE_CREATION_FLOW_SIGNIN_BUTTON_LABEL},
#if BUILDFLAG(ENABLE_GLIC)
      {"glicAddProfileHelper", IDS_PROFILE_PICKER_ADD_PROFILE_HELPER_GLIC},
      {"glicTitleNoProfile",
       IDS_PROFILE_PICKER_MAIN_VIEW_TITLE_GLIC_NO_PROFILE},
      {"mainViewSubtitleGlicNoProfile",
       IDS_PROFILE_PICKER_MAIN_VIEW_SUBTITLE_GLIC_NO_PROFILE},
#endif  // BUILDFLAG(ENABLE_GLIC)
  };
  html_source->AddLocalizedStrings(kLocalizedStrings);

  html_source->AddLocalizedString("mainViewTitle",
                                  GetMainViewTitleId(is_glic_version));
#if BUILDFLAG(ENABLE_GLIC)
  html_source->AddLocalizedString(
      "mainViewSubtitle", is_glic_version
                              ? IDS_PROFILE_PICKER_MAIN_VIEW_SUBTITLE_GLIC
                              : IDS_PROFILE_PICKER_MAIN_VIEW_SUBTITLE);
#else
  html_source->AddLocalizedString("mainViewSubtitle",
                                  IDS_PROFILE_PICKER_MAIN_VIEW_SUBTITLE);
#endif  // BUILDFLAG(ENABLE_GLIC)

  html_source->AddLocalizedString(
      "profileTypeChoiceSubtitle",
      IDS_PROFILE_PICKER_PROFILE_CREATION_FLOW_PROFILE_TYPE_CHOICE_SUBTITLE_UNO);

  html_source->AddString("minimumPickerSize",
                         base::StringPrintf("%ipx", kMinimumPickerSizePx));

  html_source->AddString("managedDeviceDisclaimer",
                         GetManagedDeviceDisclaimer());
}

void AddFlags(content::WebUIDataSource* html_source, bool is_glic_version) {
  html_source->AddBoolean("isGlicVersion", is_glic_version);

  // TODO(crbug.com/385726690): Check if we want to show the locked profiles or
  // not.
  html_source->AddBoolean("isForceSigninEnabled",
                          signin_util::IsForceSigninEnabled());

  // In glic version, disable all other policies:
  // - Profile Creation and signing in are not allowed.
  // - Additional action button should not be shown: Guest and AskOnStartup.
  if (is_glic_version) {
    html_source->AddBoolean("isAskOnStartupAllowed", false);
    html_source->AddBoolean("askOnStartup", false);
    html_source->AddBoolean("profilesReorderingEnabled", false);
    html_source->AddBoolean("signInProfileCreationFlowSupported", false);
    html_source->AddBoolean("isBrowserSigninAllowed", false);
    html_source->AddBoolean("isGuestModeEnabled", false);
    html_source->AddBoolean("isProfileCreationAllowed", false);
    return;
  }

  bool ask_on_startup_allowed =
      static_cast<ProfilePicker::AvailabilityOnStartup>(
          g_browser_process->local_state()->GetInteger(
              prefs::kBrowserProfilePickerAvailabilityOnStartup)) ==
      ProfilePicker::AvailabilityOnStartup::kEnabled;
  html_source->AddBoolean("isAskOnStartupAllowed", ask_on_startup_allowed);
  html_source->AddBoolean("askOnStartup",
                          g_browser_process->local_state()->GetBoolean(
                              prefs::kBrowserShowProfilePickerOnStartup));
  html_source->AddBoolean("profilesReorderingEnabled",
                          base::FeatureList::IsEnabled(kProfilesReordering));
  html_source->AddBoolean("signInProfileCreationFlowSupported",
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
                          AccountConsistencyModeManager::IsDiceSignInAllowed());
#else
                          true);
#endif

  html_source->AddBoolean("isBrowserSigninAllowed", IsBrowserSigninAllowed());
  html_source->AddBoolean("isForceSigninEnabled",
                          signin_util::IsForceSigninEnabled());
  html_source->AddBoolean("isGuestModeEnabled", profiles::IsGuestModeEnabled());
  html_source->AddBoolean("isProfileCreationAllowed",
                          profiles::IsProfileCreationAllowed());
}

void AddResourcePaths(content::WebUIDataSource* html_source,
                      bool is_glic_version) {
  const webui::ResourcePath kResourcePaths[] = {
      {"left_banner.svg", IDR_SIGNIN_IMAGES_SHARED_LEFT_BANNER_SVG},
      {"left_banner_dark.svg", IDR_SIGNIN_IMAGES_SHARED_LEFT_BANNER_DARK_SVG},
      {"right_banner.svg", IDR_SIGNIN_IMAGES_SHARED_RIGHT_BANNER_SVG},
      {"right_banner_dark.svg", IDR_SIGNIN_IMAGES_SHARED_RIGHT_BANNER_DARK_SVG},
#if BUILDFLAG(ENABLE_GLIC)
      {"glic_banner_top_right.svg",
       glic::GetResourceID(IDR_GLIC_PROFILE_BANNER_TOP_RIGHT)},
      {"glic_banner_bottom_left.svg",
       glic::GetResourceID(IDR_GLIC_PROFILE_BANNER_BOTTOM_LEFT)},
      {"glic_banner_top_right_light.svg",
       glic::GetResourceID(IDR_GLIC_PROFILE_BANNER_TOP_RIGHT_LIGHT)},
      {"glic_banner_bottom_left_light.svg",
       glic::GetResourceID(IDR_GLIC_PROFILE_BANNER_BOTTOM_LEFT_LIGHT)},
      {"glic_profile_branding.css",
       glic::GetResourceID(IDR_GLIC_PROFILE_BRANDING_CSS)},
#endif  // BUILDFLAG(ENABLE_GLIC)
  };
  html_source->AddResourcePaths(kResourcePaths);

  int logo_resource_id;
#if BUILDFLAG(ENABLE_GLIC)
  logo_resource_id = is_glic_version
                         ? glic::GetResourceID(IDR_GLIC_PROFILE_LOGO)
                         : IDR_PRODUCT_LOGO_SVG;
#else
  logo_resource_id = IDR_PRODUCT_LOGO_SVG;
#endif  // BUILDFLAG(ENABLE_GLIC)
  html_source->AddResourcePath("picker_logo.svg", logo_resource_id);
}

}  // namespace

ProfilePickerUI::ProfilePickerUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui, /*enable_chrome_send=*/true) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* html_source =
      content::WebUIDataSource::CreateAndAdd(
          profile, chrome::kChromeUIProfilePickerHost);

  // `content::WebContents::GetVisibleURL()` is used here because a
  // WebUIController is created before the navigation commits.
  bool is_glic_version = web_ui->GetWebContents()->GetVisibleURL().query() ==
                         chrome::kChromeUIProfilePickerGlicQuery;

  std::unique_ptr<ProfilePickerHandler> handler =
      std::make_unique<ProfilePickerHandler>(is_glic_version);
  profile_picker_handler_ = handler.get();
  web_ui->AddMessageHandler(std::move(handler));

  // Same as above for usage of `content::WebContents::GetVisibleURL()`.
  if (web_ui->GetWebContents()->GetVisibleURL().query() ==
      chrome::kChromeUIProfilePickerStartupQuery) {
    profile_picker_handler_->EnableStartupMetrics();
  }

  // Setting the title here instead of relying on the one provided from the
  // page itself makes it available much earlier, and avoids having to fallback
  // to the one obtained from `NavigationEntry::GetTitleForDisplay()` (which
  // ends up being the URL) when we try to get it on startup for a11y purposes.
  web_ui->OverrideTitle(
      l10n_util::GetStringUTF16(GetMainViewTitleId(is_glic_version)));

  // Add all resources.
  AddStrings(html_source, is_glic_version);
  AddFlags(html_source, is_glic_version);
  AddResourcePaths(html_source, is_glic_version);

  webui::SetupWebUIDataSource(html_source, kProfilePickerResources,
                              IDR_PROFILE_PICKER_PROFILE_PICKER_HTML);
}

ProfilePickerUI::~ProfilePickerUI() = default;

void ProfilePickerUI::CreateHelpBubbleHandler(
    mojo::PendingRemote<help_bubble::mojom::HelpBubbleClient> client,
    mojo::PendingReceiver<help_bubble::mojom::HelpBubbleHandler> handler) {
  help_bubble_handler_ = std::make_unique<user_education::HelpBubbleHandler>(
      std::move(handler), std::move(client), this,
      std::vector<ui::ElementIdentifier>{});
}

void ProfilePickerUI::BindInterface(
    mojo::PendingReceiver<help_bubble::mojom::HelpBubbleHandlerFactory>
        pending_receiver) {
  if (help_bubble_handler_factory_receiver_.is_bound()) {
    help_bubble_handler_factory_receiver_.reset();
  }
  help_bubble_handler_factory_receiver_.Bind(std::move(pending_receiver));
}

// static
gfx::Size ProfilePickerUI::GetMinimumSize() {
  return gfx::Size(kMinimumPickerSizePx, kMinimumPickerSizePx);
}

ProfilePickerHandler* ProfilePickerUI::GetProfilePickerHandlerForTesting() {
  return profile_picker_handler_;
}

void ProfilePickerUI::ShowForceSigninErrorDialog(
    const ForceSigninUIError& error) {
  profile_picker_handler_->DisplayForceSigninErrorDialog(base::FilePath(),
                                                         error);
}

WEB_UI_CONTROLLER_TYPE_IMPL(ProfilePickerUI)
