// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/webui/ui_bundled/version_handler.h"

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "components/webui/version/version_handler_helper.h"
#include "components/webui/version/version_ui_constants.h"
#include "ios/web/public/webui/web_ui_ios.h"
#include "ui/base/l10n/l10n_util.h"

VersionHandler::VersionHandler() {}

VersionHandler::~VersionHandler() {}

void VersionHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      version_ui::kRequestVariationInfo,
      base::BindRepeating(&VersionHandler::HandleRequestVariationInfo,
                          base::Unretained(this)));
}

void VersionHandler::HandleRequestVariationInfo(const base::Value::List& args) {
  // Respond with the variations info immediately.
  CHECK_EQ(2U, args.size());
  const std::string& callback_id = args[0].GetString();
  const bool return_raw_variations_cmd = args[1].GetBool();

  base::Value::Dict response;
  response.Set(version_ui::kKeyVariationsList, version_ui::GetVariationsList());
  if (return_raw_variations_cmd) {
    response.Set(version_ui::kKeyVariationsCmd,
                 version_ui::GetVariationsCommandLine());
  } else {
    response.Set(version_ui::kKeyVariationsCmd,
                 base::Base64Encode(version_ui::GetVariationsCommandLine()));
  }
  web_ui()->ResolveJavascriptCallback(base::Value(callback_id), response);
}
