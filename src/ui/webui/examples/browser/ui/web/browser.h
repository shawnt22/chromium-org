// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_WEBUI_EXAMPLES_BROWSER_UI_WEB_BROWSER_H_
#define UI_WEBUI_EXAMPLES_BROWSER_UI_WEB_BROWSER_H_

#include <memory>

#include "components/guest_contents/common/guest_contents.mojom.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_controller.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/examples/browser/ui/web/browser.mojom.h"
#include "ui/webui/mojo_web_ui_controller.h"

namespace webui_examples {

class BrowserPageHandler;

class Browser : public ui::MojoWebUIController,
                public webui_examples::mojom::PageHandlerFactory {
 public:
  static constexpr char kHost[] = "browser";

  explicit Browser(content::WebUI* web_ui);
  Browser(const Browser&) = delete;
  Browser& operator=(const Browser&) = delete;
  ~Browser() override;

  void BindInterface(
      mojo::PendingReceiver<webui_examples::mojom::PageHandlerFactory>
          receiver);
  void BindInterface(
      mojo::PendingReceiver<guest_contents::mojom::GuestContentsHost> receiver);

  content::WebContents* guest_contents() { return guest_contents_.get(); }

 private:
  // webui_examples::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingReceiver<webui_examples::mojom::PageHandler> receiver)
      override;

  mojo::Receiver<webui_examples::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
  raw_ptr<BrowserPageHandler> page_handler_;
  std::unique_ptr<content::WebContents> guest_contents_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace webui_examples

#endif  // UI_WEBUI_EXAMPLES_BROWSER_UI_WEB_BROWSER_H_
