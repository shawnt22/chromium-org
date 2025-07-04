// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/link_capturing/navigation_capturing_settings_cros_impl.h"

#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/app_service/launch_utils.h"
#include "chrome/browser/web_applications/chromeos_web_app_experiments.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"

namespace web_app {

std::unique_ptr<NavigationCapturingSettings>
NavigationCapturingSettings::Create(Profile& profile) {
  return std::make_unique<NavigationCapturingSettingsCrosImpl>(profile);
}

NavigationCapturingSettingsCrosImpl::NavigationCapturingSettingsCrosImpl(
    Profile& profile)
    : profile_(profile) {}

NavigationCapturingSettingsCrosImpl::~NavigationCapturingSettingsCrosImpl() =
    default;

std::optional<webapps::AppId>
NavigationCapturingSettingsCrosImpl::GetCapturingWebAppForUrl(const GURL& url) {
  if (std::optional<webapps::AppId> iwa_id =
          WebAppProvider::GetForWebApps(&profile_.get())
              ->registrar_unsafe()
              .FindBestAppWithUrlInScope(url, WebAppFilter::IsIsolatedApp())) {
    // IWA URLs are always captured.
    return *iwa_id;
  }

  if (!apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(
          &profile_.get())) {
    return std::nullopt;
  }

  auto* proxy = apps::AppServiceProxyFactory::GetForProfile(&profile_.get());
  auto app_id = apps::FindAppIdsToLaunchForUrl(proxy, url).preferred;
  if (!app_id.has_value() ||
      proxy->AppRegistryCache().GetAppType(*app_id) != apps::AppType::kWeb) {
    return std::nullopt;
  }
  return app_id;
}

// This is needed on ChromeOS to support the ChromeOsWebAppExperiments
// code, see ChromeOsWebAppExperimentsNavigationBrowserTest for tests with
// this on.
bool NavigationCapturingSettingsCrosImpl::
    ShouldAuxiliaryContextsKeepSameContainer(
        const std::optional<webapps::AppId>& source_browser_app_id,
        const GURL& url) {
  if (source_browser_app_id.has_value() &&
      ChromeOsWebAppExperiments::IsNavigationCapturingReimplEnabledForSourceApp(
          *source_browser_app_id, url)) {
    return true;
  }

  return NavigationCapturingSettings::ShouldAuxiliaryContextsKeepSameContainer(
      source_browser_app_id, url);
}

}  // namespace web_app
