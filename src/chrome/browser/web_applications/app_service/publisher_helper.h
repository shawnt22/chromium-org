// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_APP_SERVICE_PUBLISHER_HELPER_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_APP_SERVICE_PUBLISHER_HELPER_H_

#include <string>
#include <vector>

#include "chrome/browser/web_applications/web_app_constants.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "components/webapps/common/web_app_id.h"
#include "url/gurl.h"

class Profile;

namespace apps {
enum class ShortcutSource;
}

namespace web_app {
class WebAppProvider;

// Converts |uninstall_source| to a |WebappUninstallSource|.
webapps::WebappUninstallSource ConvertUninstallSourceToWebAppUninstallSource(
    apps::UninstallSource uninstall_source);

// Whether a web app is considered as an shortcut in App Service sense.
// Returns false if the web app cannot found in the web app registrar.
bool IsAppServiceShortcut(const webapps::AppId& web_app_id,
                          const WebAppProvider& provider);

#if BUILDFLAG(IS_CHROMEOS)
// Consults the app service to figure out which web apps are capable of handling
// `protocol_url`.
std::vector<std::string> GetWebAppIdsForProtocolUrl(Profile* profile,
                                                    const GURL& protocol_url);
#endif

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_APP_SERVICE_PUBLISHER_HELPER_H_
