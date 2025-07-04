// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/link_capturing/web_apps_intent_picker_delegate.h"

#include <map>
#include <string>

#include "base/containers/flat_map.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/task/bind_post_task.h"
#include "chrome/browser/apps/link_capturing/apps_intent_picker_delegate.h"
#include "chrome/browser/apps/link_capturing/enable_link_capturing_infobar_delegate.h"
#include "chrome/browser/apps/link_capturing/intent_picker_info.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/link_capturing_features.h"
#include "chrome/browser/web_applications/web_app_icon_manager.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_ui_manager.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "components/webapps/common/web_app_id.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image.h"

#if BUILDFLAG(IS_MAC)
#include "base/task/thread_pool.h"
#include "chrome/browser/apps/link_capturing/mac_intent_picker_helpers.h"
#endif

namespace apps {

namespace {

void OnAppReparentedRunInNewContents(const std::string& launch_name,
                                     base::OnceClosure callback,
                                     content::WebContents* web_contents) {
  if (!features::ShouldShowLinkCapturingUX()) {
    return;
  }

  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  web_app::WebAppProvider* provider =
      web_app::WebAppProvider::GetForWebApps(profile);
  CHECK(provider);

  provider->ui_manager().MaybeCreateEnableSupportedLinksInfobar(web_contents,
                                                                launch_name);
  provider->ui_manager().MaybeShowIPHPromoForAppsLaunchedViaLinkCapturing(
      /*browser=*/nullptr, profile, launch_name);

  std::move(callback).Run();
}

}  // namespace

WebAppsIntentPickerDelegate::WebAppsIntentPickerDelegate(
    Profile* profile,
    std::vector<int> icon_sizes_in_dep)
    : profile_(*profile),
      provider_(web_app::AreWebAppsUserInstallable(profile)
                    ? web_app::WebAppProvider::GetForWebApps(profile)
                    : nullptr),
      icon_sizes_in_dep_(std::move(icon_sizes_in_dep)) {
  CHECK(!icon_sizes_in_dep_.empty());
}

WebAppsIntentPickerDelegate::~WebAppsIntentPickerDelegate() = default;

bool WebAppsIntentPickerDelegate::ShouldShowIntentPickerWithApps() {
  return web_app::AreWebAppsUserInstallable(&profile_.get());
}

void WebAppsIntentPickerDelegate::FindAllAppsForUrl(
    const GURL& url,
    IntentPickerAppsCallback apps_callback) {
  CHECK(ShouldShowIntentPickerWithApps());
  CHECK(provider_);
  std::vector<apps::IntentPickerAppInfo> apps;
  base::flat_map<webapps::AppId, std::string> all_controlling_apps =
      provider_->registrar_unsafe().GetAllAppsControllingUrl(url);
  for (const auto& [app_id, name] : all_controlling_apps) {
    apps.emplace_back(PickerEntryType::kWeb, ui::ImageModel(), app_id, name);
  }

#if BUILDFLAG(IS_MAC)
  // On the Mac, if there is a Universal Link, jump to a worker thread to do
  // this.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING, base::MayBlock()},
      base::BindOnce(&FindMacAppForUrl, url, base::span(icon_sizes_in_dep_)),
      base::BindOnce(
          &WebAppsIntentPickerDelegate::CacheMacAppInfoAndPostFinalCallback,
          weak_ptr_factory.GetWeakPtr(), std::move(apps_callback),
          std::move(apps)));
#else
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(apps_callback), std::move(apps)));
#endif  // BUILDFLAG(IS_MAC)
}

bool WebAppsIntentPickerDelegate::IsPreferredAppForSupportedLinks(
    const webapps::AppId& app_id) {
  CHECK(ShouldShowIntentPickerWithApps());
  CHECK(provider_);
  return provider_->registrar_unsafe().CapturesLinksInScope(app_id);
}

void WebAppsIntentPickerDelegate::LoadSingleAppIcon(
    PickerEntryType entry_type,
    const std::string& app_id,
    int size_in_dep,
    IconLoadedCallback icon_loaded_callback) {
  CHECK(ShouldShowIntentPickerWithApps());
  CHECK(provider_);
  CHECK(entry_type == PickerEntryType::kWeb ||
        entry_type == PickerEntryType::kMacOs);

  if (entry_type == PickerEntryType::kWeb) {
    web_app::WebAppIconManager& icon_manager = provider_->icon_manager();
    // First, iterate over all icons with the given order of purposes, and
    // verify if there exists an icon that can be loaded. The order of purposes
    // helps ensure we first look for ANY and MASKABLE icons before going for
    // MONOCHROME.
    std::vector<web_app::IconPurpose> ordered_purpose = {
        web_app::IconPurpose::MASKABLE, web_app::IconPurpose::ANY,
        web_app::IconPurpose::MONOCHROME};
    auto size_and_purpose =
        icon_manager.FindIconMatchBigger(app_id, ordered_purpose, size_in_dep);
    if (!size_and_purpose.has_value()) {
      std::move(icon_loaded_callback).Run(ui::ImageModel());
      return;
    }

    web_app::IconPurpose purpose_to_get = size_and_purpose.value().purpose;
    auto transform_bitmaps_to_icon_metadata = base::BindOnce(
        [](std::map<web_app::SquareSizePx, SkBitmap> icons) -> ui::ImageModel {
          bool is_valid_icon = !icons.empty();
          if (!is_valid_icon) {
            return ui::ImageModel();
          }

          CHECK_EQ(icons.size(), 1u);
          return ui::ImageModel::FromImageSkia(
              gfx::ImageSkia::CreateFrom1xBitmap(icons.begin()->second));
        });
    icon_manager.ReadIconAndResize(app_id, purpose_to_get, size_in_dep,
                                   std::move(transform_bitmaps_to_icon_metadata)
                                       .Then(std::move(icon_loaded_callback)));
  } else if (entry_type == apps::PickerEntryType::kMacOs) {
#if BUILDFLAG(IS_MAC)
    // Read from the cached app information if an app with universal links were
    // found.
    ui::ImageModel mac_app_icon;
    if (mac_app_info_.has_value()) {
      CHECK_EQ(mac_app_info_->launch_name, app_id);
      mac_app_icon = ui::ImageModel::FromImage(
          mac_app_info_->icon.CreateExact(gfx::Size(size_in_dep, size_in_dep)));
    }
    std::move(icon_loaded_callback).Run(mac_app_icon);
#else
    NOTREACHED();
#endif  // BUILDFLAG(IS_MAC)
  }
}

void WebAppsIntentPickerDelegate::RecordIntentPickerIconEvent(
    apps::IntentPickerIconEvent event) {
  base::UmaHistogramEnumeration("Webapp.Site.Intents.IntentPickerIconEvent",
                                event);
  if (event == apps::IntentPickerIconEvent::kIconClicked) {
    base::RecordAction(base::UserMetricsAction("IntentPickerIconClicked"));
  }
}

bool WebAppsIntentPickerDelegate::ShouldLaunchAppDirectly(
    const GURL& url,
    const std::string& app_id,
    PickerEntryType entry_type) {
  CHECK(entry_type == PickerEntryType::kWeb ||
        entry_type == PickerEntryType::kMacOs);
  CHECK(ShouldShowIntentPickerWithApps());
  CHECK(provider_);
  if (!features::ShouldShowLinkCapturingUX()) {
    return false;
  }
  if (entry_type == PickerEntryType::kWeb) {
    // Launch app directly only if |url| is in the scope of |app_id|.
    if (base::FeatureList::IsEnabled(
            ::features::kPwaNavigationCapturingWithScopeExtensions)) {
      return provider_->registrar_unsafe().IsUrlInAppExtendedScope(url, app_id);
    } else {
      return provider_->registrar_unsafe().IsUrlInAppScope(url, app_id);
    }
  }

  // This is only reached on MacOS if there is one app available and the picker
  // entry type is kMacOS, so it's fine to launch directly.
  return true;
}

void WebAppsIntentPickerDelegate::RecordOutputMetrics(
    PickerEntryType entry_type,
    IntentPickerCloseReason close_reason,
    bool should_persist,
    bool should_launch_app) {
  // On desktop platforms, the entry type should always be kWeb and
  // should_persist should always be false. This is because the only apps
  // supported by the intent filters are PWAs, and the persistence checkbox does
  // not show up on the intent picker bubble for desktop platforms.
  CHECK_EQ(should_persist, false);
  switch (close_reason) {
    case IntentPickerCloseReason::OPEN_APP:
      base::RecordAction(
          base::UserMetricsAction("IntentPickerViewAcceptLaunchApp"));
      break;
    case apps::IntentPickerCloseReason::DIALOG_DEACTIVATED:
      base::RecordAction(base::UserMetricsAction("IntentPickerViewIgnored"));
      break;
    case apps::IntentPickerCloseReason::STAY_IN_CHROME:
      base::RecordAction(
          base::UserMetricsAction("IntentPickerViewClosedStayInChrome"));
      break;
    case apps::IntentPickerCloseReason::ERROR_BEFORE_PICKER:
    case apps::IntentPickerCloseReason::ERROR_AFTER_PICKER:
    case apps::IntentPickerCloseReason::PREFERRED_APP_FOUND:
      break;
    default:
      NOTREACHED();
  }
}

// Persisting intent preferences for an app is a no-op, since the checkbox in
// the intent picker bubble view does not show up for non-CrOS.
void WebAppsIntentPickerDelegate::PersistIntentPreferencesForApp(
    PickerEntryType entry_type,
    const std::string& app_id) {
  NOTREACHED();
}

void WebAppsIntentPickerDelegate::LaunchApp(content::WebContents* web_contents,
                                            const GURL& url,
                                            const std::string& launch_name,
                                            PickerEntryType entry_type,
                                            base::OnceClosure callback) {
  CHECK(entry_type == apps::PickerEntryType::kWeb ||
        entry_type == apps::PickerEntryType::kMacOs);
  CHECK(ShouldShowIntentPickerWithApps());
  CHECK(provider_);
  if (entry_type == apps::PickerEntryType::kWeb) {
    // Note: This call can destroy the current web contents synchronously,
    // which will destroy this object.
    provider_->ui_manager().ReparentAppTabToWindow(
        web_contents, launch_name,
        base::BindOnce(
            &OnAppReparentedRunInNewContents, launch_name,
            base::BindPostTaskToCurrentDefault(std::move(callback))));
  } else if (entry_type == apps::PickerEntryType::kMacOs) {
#if BUILDFLAG(IS_MAC)
    LaunchMacApp(url, launch_name, std::move(callback));
#else
    NOTREACHED();
#endif  // BUILDFLAG(IS_MAC)
  }
}

#if BUILDFLAG(IS_MAC)
void WebAppsIntentPickerDelegate::CacheMacAppInfoAndPostFinalCallback(
    IntentPickerAppsCallback apps_callback,
    std::vector<IntentPickerAppInfo> apps,
    std::optional<MacAppInfo> mac_app_info) {
  mac_app_info_ = std::move(mac_app_info);
  if (mac_app_info_.has_value()) {
    apps.emplace_back(mac_app_info_.value());
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(apps_callback), std::move(apps)));
}
#endif  // BUILDFLAG(IS_MAC)

}  // namespace apps
