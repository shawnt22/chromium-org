// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/storage/durable_storage_permission_context.h"

#include <algorithm>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/engagement/important_sites_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/browser/website_settings_registry.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "components/permissions/permission_decision.h"
#include "components/permissions/permission_request_data.h"
#include "components/permissions/permission_request_id.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/origin_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/cookie_setting_override.h"
#include "net/cookies/site_for_cookies.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom-shared.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/gurl.h"
#include "url/origin.h"

using bookmarks::BookmarkModel;
using PermissionStatus = blink::mojom::PermissionStatus;

DurableStoragePermissionContext::DurableStoragePermissionContext(
    content::BrowserContext* browser_context)
    : permissions::ContentSettingPermissionContextBase(
          browser_context,
          ContentSettingsType::DURABLE_STORAGE,
          network::mojom::PermissionsPolicyFeature::kNotFound) {}

void DurableStoragePermissionContext::DecidePermission(
    std::unique_ptr<permissions::PermissionRequestData> request_data,
    permissions::BrowserPermissionCallback callback) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  DCHECK_NE(PermissionStatus::GRANTED,
            GetPermissionStatus(
                *request_data->resolver, nullptr /* render_frame_host */,
                request_data->requesting_origin, request_data->embedding_origin)
                .status);
  DCHECK_NE(PermissionStatus::DENIED,
            GetPermissionStatus(
                *request_data->resolver, nullptr /* render_frame_host */,
                request_data->requesting_origin, request_data->embedding_origin)
                .status);

  // Durable is only allowed to be granted to the top-level origin. Embedding
  // origin is the last committed navigation origin to the web contents.
  if (request_data->requesting_origin != request_data->embedding_origin) {
    NotifyPermissionSet(*request_data, std::move(callback),
                        /*persist=*/false, PermissionDecision::kNone,
                        /*is_final_decision=*/true);
    return;
  }

  scoped_refptr<content_settings::CookieSettings> cookie_settings =
      CookieSettingsFactory::GetForProfile(
          Profile::FromBrowserContext(browser_context()));

  content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(
      request_data->id.global_render_frame_host_id());
  CHECK(rfh);
  // Don't grant durable for session-only storage, since it won't be persisted
  // anyway. Don't grant durable if we can't write cookies.
  if (cookie_settings->IsCookieSessionOnly(request_data->requesting_origin) ||
      !cookie_settings->IsFullCookieAccessAllowed(
          request_data->requesting_origin,
          net::SiteForCookies::FromUrl(request_data->requesting_origin),
          url::Origin::Create(request_data->requesting_origin),
          net::CookieSettingOverrides(),
          rfh->GetStorageKey().ToCookiePartitionKey())) {
    NotifyPermissionSet(*request_data, std::move(callback),
                        /*persist=*/false, PermissionDecision::kNone,
                        /*is_final_decision=*/true);
    return;
  }

  std::string registerable_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          request_data->requesting_origin,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (registerable_domain.empty() &&
      request_data->requesting_origin.HostIsIPAddress()) {
    registerable_domain = request_data->requesting_origin.host();
  }

  std::set<std::string> installed_registerable_domains =
      site_engagement::ImportantSitesUtil::GetInstalledRegisterableDomains(
          Profile::FromBrowserContext(browser_context()));
  if (base::Contains(installed_registerable_domains, registerable_domain)) {
    NotifyPermissionSet(*request_data, std::move(callback),
                        /*persist=*/true, PermissionDecision::kAllow,
                        /*is_final_decision=*/true);
    return;
  }

  const size_t kMaxImportantResults = 10;
  std::vector<site_engagement::ImportantSitesUtil::ImportantDomainInfo>
      important_sites =
          site_engagement::ImportantSitesUtil::GetImportantRegisterableDomains(
              Profile::FromBrowserContext(browser_context()),
              kMaxImportantResults);

  for (const auto& important_site : important_sites) {
    if (important_site.registerable_domain == registerable_domain) {
      NotifyPermissionSet(*request_data, std::move(callback),
                          /*persist=*/true, PermissionDecision::kAllow,
                          /*is_final_decision=*/true);
      return;
    }
  }

  NotifyPermissionSet(*request_data, std::move(callback),
                      /*persist=*/false, PermissionDecision::kNone,
                      /*is_final_decision=*/true);
}

void DurableStoragePermissionContext::UpdateContentSetting(
    const permissions::PermissionRequestData& request_data,
    ContentSetting content_setting,
    bool is_one_time) {
  DCHECK(!is_one_time);
  DCHECK_EQ(request_data.requesting_origin,
            request_data.requesting_origin.DeprecatedGetOriginAsURL());
  DCHECK_EQ(request_data.embedding_origin,
            request_data.embedding_origin.DeprecatedGetOriginAsURL());
  DCHECK(content_setting == CONTENT_SETTING_ALLOW ||
         content_setting == CONTENT_SETTING_BLOCK);

  HostContentSettingsMapFactory::GetForProfile(browser_context())
      ->SetContentSettingDefaultScope(request_data.requesting_origin, GURL(),
                                      ContentSettingsType::DURABLE_STORAGE,
                                      content_setting);
}
