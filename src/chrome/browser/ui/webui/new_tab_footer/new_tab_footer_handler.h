// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NEW_TAB_FOOTER_NEW_TAB_FOOTER_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_NEW_TAB_FOOTER_NEW_TAB_FOOTER_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/search/background/ntp_custom_background_service.h"
#include "chrome/browser/search/background/ntp_custom_background_service_observer.h"
#include "chrome/browser/ui/webui/new_tab_footer/new_tab_footer.mojom.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "components/policy/core/common/management/management_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Profile;

class NewTabFooterHandler : public new_tab_footer::mojom::NewTabFooterHandler,
                            public extensions::ExtensionRegistryObserver,
                            public policy::ManagementService::Observer,
                            public NtpCustomBackgroundServiceObserver {
 public:
  NewTabFooterHandler(
      mojo::PendingReceiver<new_tab_footer::mojom::NewTabFooterHandler>
          pending_handler,
      mojo::PendingRemote<new_tab_footer::mojom::NewTabFooterDocument>
          pending_document,
      base::WeakPtr<TopChromeWebUIController::Embedder> embedder,
      NtpCustomBackgroundService* ntp_custom_background_service,
      content::WebContents* web_contents);

  NewTabFooterHandler(const NewTabFooterHandler&) = delete;
  NewTabFooterHandler& operator=(const NewTabFooterHandler&) = delete;

  ~NewTabFooterHandler() override;

  // new_tab_footer::mojom::NewTabFooterHandler:
  void UpdateNtpExtensionName() override;
  void UpdateManagementNotice() override;
  void UpdateAttachedTabState() override;
  void UpdateBackgroundAttribution() override;
  void OpenExtensionOptionsPageWithFallback() override;
  void OpenManagementPage() override;
  void OpenUrlInCurrentTab(const GURL& url) override;
  void ShowContextMenu(const gfx::Point& point) override;

  // Returns the bitmap representation of the management logo.
  // Exposed for testing only.
  SkBitmap GetManagementNoticeIconBitmap();
  // Passes `AttachedTabStateUpdated` calls to the `document_`.
  void AttachedTabStateUpdated(const GURL& url);

  // ManagementService::Observer
  void OnEnterpriseLogoUpdatedForBrowser() override;

  void SetThemeProviderForTesting(ui::ThemeProvider* theme_provider);

 private:
  std::string GetManagementNoticeText();
  std::string GetManagementNoticeIconDataUrl();

  // extensions::ExtensionRegistryObserver.
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionReady(content::BrowserContext* browser_context,
                        const extensions::Extension* extension) override;

  // NtpCustomBackgroundServiceObserver:
  void OnCustomBackgroundImageUpdated() override;

  GURL curr_background_url_;
  std::string curr_ntp_extension_id_;
  base::WeakPtr<TopChromeWebUIController::Embedder> embedder_;
  const raw_ptr<Profile> profile_;
  const raw_ptr<content::WebContents> web_contents_;
  raw_ptr<NtpCustomBackgroundService> ntp_custom_background_service_;
  raw_ptr<const ui::ThemeProvider> theme_provider_;
  PrefChangeRegistrar profile_pref_change_registrar_;
  PrefChangeRegistrar local_state_pref_change_registrar_;
  GURL last_source_url_;

  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      extension_registry_observation_{this};
  base::ScopedObservation<policy::ManagementService,
                          policy::ManagementService::Observer>
      management_observation_{this};
  base::ScopedObservation<NtpCustomBackgroundService,
                          NtpCustomBackgroundServiceObserver>
      ntp_custom_background_service_observation_{this};

  // These are located at the end of the list of member variables to ensure the
  // WebUI page is disconnected before other members are destroyed.
  mojo::Remote<new_tab_footer::mojom::NewTabFooterDocument> document_;
  mojo::Receiver<new_tab_footer::mojom::NewTabFooterHandler> handler_;
};

#endif  // CHROME_BROWSER_UI_WEBUI_NEW_TAB_FOOTER_NEW_TAB_FOOTER_HANDLER_H_
