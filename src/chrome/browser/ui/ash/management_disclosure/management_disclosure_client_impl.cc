// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/management_disclosure/management_disclosure_client_impl.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "ash/public/cpp/login_screen.h"
#include "base/check.h"
#include "chrome/browser/ash/policy/core/browser_policy_connector_ash.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/management/management_ui.h"
#include "chrome/browser/ui/webui/management/management_ui_handler_chromeos.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"

namespace {
ManagementDisclosureClientImpl* g_management_disclosure_client_instance =
    nullptr;
}  // namespace

ManagementDisclosureClientImpl::ManagementDisclosureClientImpl(
    policy::BrowserPolicyConnectorAsh* connector,
    Profile* profile)
    : connector_(connector), profile_(profile) {
  // Register this object as the client interface implementation.
  ash::LoginScreen::Get()->SetManagementDisclosureClient(this);

  DCHECK(!g_management_disclosure_client_instance);
  g_management_disclosure_client_instance = this;
}

ManagementDisclosureClientImpl::~ManagementDisclosureClientImpl() {
  ash::LoginScreen::Get()->SetManagementDisclosureClient(nullptr);

  DCHECK_EQ(this, g_management_disclosure_client_instance);
  g_management_disclosure_client_instance = nullptr;
  connector_ = nullptr;
  profile_ = nullptr;
}

std::vector<std::u16string> ManagementDisclosureClientImpl::GetDisclosures() {
  // Fill the map when it is first called.
  if (policy_map_.empty()) {
    std::vector<webui::LocalizedString> localized_strings;
    ManagementUI::GetLocalizedStrings(localized_strings, /*remove_links=*/true);
    for (const auto& [name, id] : localized_strings) {
      policy_map_.emplace(name, id);
    }
  }

  const auto disclosures = ManagementUIHandlerChromeOS::GetDeviceReportingInfo(
      connector_->GetDeviceCloudPolicyManager(), profile_);
  std::vector<std::u16string> disclosure_list;
  // Add all disclosures to list.
  for (const auto& disclosure : disclosures) {
    if (!disclosure.is_dict()) {
      continue;
    }

    const auto* message = disclosure.GetDict().Find("messageId");
    if (!message || !message->is_string()) {
      continue;
    }

    if (!policy_map_.contains(message->GetString())) {
      LOG(WARNING) << "policy disclosure not found in policy_map";
      continue;
    }

    disclosure_list.push_back(
        l10n_util::GetStringUTF16(policy_map_[message->GetString()]));
  }

  return disclosure_list;
}
