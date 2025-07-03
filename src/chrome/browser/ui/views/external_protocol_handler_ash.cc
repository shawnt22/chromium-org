// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "ash/constants/ash_features.h"
#include "base/containers/to_vector.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/arc/intent_helper/arc_intent_helper_mojo_ash.h"
#include "chrome/browser/ash/guest_os/guest_os_external_protocol_handler.h"
#include "chrome/browser/chromeos/arc/arc_external_protocol_dialog.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/external_protocol_dialog.h"
#include "chrome/browser/web_applications/app_service/publisher_helper.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/ash/components/browser_context_helper/browser_context_types.h"
#include "chromeos/constants/chromeos_features.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/text_elider.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/controls/message_box_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

using content::WebContents;

// The external protocol dialog for Chrome OS shown when we have a URL with a
// Tel scheme but there are no handlers.
class ExternalProtocolNoHandlersTelSchemeDialog : public views::DialogDelegate {
 public:
  explicit ExternalProtocolNoHandlersTelSchemeDialog(
      aura::Window* parent_window)
      : creation_time_(base::TimeTicks::Now()) {
    DCHECK(parent_window);
    SetOwnedByWidget(OwnedByWidgetPassKey());
    views::DialogDelegate::SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kOk));
    views::DialogDelegate::SetButtonLabel(
        ui::mojom::DialogButton::kOk,
        l10n_util::GetStringUTF16(IDS_EXTERNAL_PROTOCOL_CLOSE_BUTTON_TEXT));

    message_box_view_ = new views::MessageBoxView();
    message_box_view_->SetMessageWidth(400);

    views::DialogDelegate::CreateDialogWidget(this, nullptr, parent_window)
        ->Show();
  }

  ExternalProtocolNoHandlersTelSchemeDialog(
      const ExternalProtocolNoHandlersTelSchemeDialog&) = delete;
  ExternalProtocolNoHandlersTelSchemeDialog& operator=(
      const ExternalProtocolNoHandlersTelSchemeDialog&) = delete;

  ~ExternalProtocolNoHandlersTelSchemeDialog() override = default;

  // views::DialogDelegate:
  std::u16string GetWindowTitle() const override {
    // We display a message to the user on how to use the Click to Call feature.
    return l10n_util::GetStringUTF16(
        IDS_BROWSER_SHARING_CLICK_TO_CALL_DIALOG_HELP_TEXT_NO_DEVICES);
  }
  views::View* GetContentsView() override { return message_box_view_; }
  const views::Widget* GetWidget() const override {
    return message_box_view_->GetWidget();
  }
  views::Widget* GetWidget() override { return message_box_view_->GetWidget(); }

 private:
  // The message box view whose commands we handle.
  raw_ptr<views::MessageBoxView> message_box_view_;

  // The time at which this dialog was created.
  base::TimeTicks creation_time_;
};

namespace {

void OnArcHandled(const GURL& url,
                  const std::optional<url::Origin>& initiating_origin,
                  content::WeakDocumentPtr initiator_document,
                  base::WeakPtr<WebContents> web_contents,
                  bool handled) {
  if (handled) {
    return;
  }

  // If WebContents have been destroyed, do not show any dialog.
  if (!web_contents) {
    return;
  }

  aura::Window* parent_window = web_contents->GetTopLevelNativeWindow();
  // If WebContents has been detached from window tree, do not show any dialog.
  if (!parent_window || !parent_window->GetRootWindow()) {
    return;
  }

  // Display the standard ExternalProtocolDialog if Guest OS has a handler.
  // Otherwise, if there is no handler and the URL is a Tel-link, show the No
  // Handler Tel Scheme dialog
  std::optional<guest_os::GuestOsUrlHandler> registration =
      guest_os::GuestOsUrlHandler::GetForUrl(
          Profile::FromBrowserContext(web_contents->GetBrowserContext()), url);
  if (registration) {
    new ExternalProtocolDialog(web_contents.get(), url,
                               base::UTF8ToUTF16(registration->name()),
                               initiating_origin, initiator_document);
  } else if (url.scheme() == url::kTelScheme) {
    new ExternalProtocolNoHandlersTelSchemeDialog(parent_window);
  }
}

std::string GetAppName(apps::AppServiceProxy* proxy,
                       const std::string& app_id) {
  std::optional<std::string> app_name;
  proxy->AppRegistryCache().ForOneApp(
      app_id,
      [&](const apps::AppUpdate& update) { app_name = update.ShortName(); });
  CHECK(app_name);
  return *app_name;
}

void HandleWebAppManifestProtocolHandler(
    Profile* profile,
    WebContents* web_contents,
    const GURL& url,
    const std::vector<std::string>& app_ids,
    const std::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr initiator_document) {
  CHECK(!app_ids.empty());
  CHECK(apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile));
  auto* proxy = apps::AppServiceProxyFactory::GetForProfile(profile);
  if (app_ids.size() > 1) {
    // TODO(crbug.com/422422887): Figure out how to disambiguate conflicting
    // protocol handlers; for now, pick the first one in the list.
    static constexpr std::string_view kConfictingProtocolHandlersWarning =
        "There's more than one web application handling %s links : [%s]; "
        "ChromeOS currently doesn't support disambiguating multiple handlers.";
    if (content::RenderFrameHost* rfh =
            initiator_document.AsRenderFrameHostIfValid()) {
      std::vector<std::string> app_names = base::ToVector(
          app_ids,
          [&](const auto& app_id) { return GetAppName(proxy, app_id); });
      rfh->AddMessageToConsole(
          blink::mojom::ConsoleMessageLevel::kWarning,
          base::StringPrintf(kConfictingProtocolHandlersWarning, url.scheme(),
                             base::JoinString(app_names, ",")));
    }
  }

  new ExternalProtocolDialog(web_contents, url,
                             base::UTF8ToUTF16(GetAppName(proxy, app_ids[0])),
                             initiating_origin, std::move(initiator_document));
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// ExternalProtocolHandler

// static
void ExternalProtocolHandler::RunExternalProtocolDialog(
    const GURL& url,
    WebContents* web_contents,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr initiator_document,
    const std::u16string& program_name) {
  // Don't launch anything from Shimless RMA app.
  if (ash::features::IsShimlessRMA3pDiagnosticsEnabled() &&
      ash::IsShimlessRmaAppBrowserContext(web_contents->GetBrowserContext())) {
    return;
  }

  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (std::vector<std::string> app_ids =
          web_app::GetWebAppIdsForProtocolUrl(profile, url);
      !app_ids.empty()) {
    HandleWebAppManifestProtocolHandler(profile, web_contents, url, app_ids,
                                        initiating_origin, initiator_document);
    return;
  }

  // Check if ARC version of the dialog is available and run ARC
  // version when possible.
  arc::RunArcExternalProtocolDialog(
      url, initiating_origin, web_contents->GetWeakPtr(), page_transition,
      has_user_gesture, is_in_fenced_frame_tree,
      std::make_unique<arc::ArcIntentHelperMojoAsh>(),
      base::BindOnce(&OnArcHandled, url, initiating_origin,
                     std::move(initiator_document),
                     web_contents->GetWeakPtr()));
}
