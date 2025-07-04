// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/supervised_user/core/browser/supervised_user_interstitial.h"

#include <stddef.h>

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "components/supervised_user/core/browser/supervised_user_error_page.h"
#include "components/supervised_user/core/browser/supervised_user_service.h"
#include "components/supervised_user/core/browser/supervised_user_utils.h"
#include "components/supervised_user/core/browser/web_content_handler.h"
#include "components/supervised_user/core/common/features.h"
#include "components/supervised_user/core/common/pref_names.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace supervised_user {

// static
std::unique_ptr<SupervisedUserInterstitial> SupervisedUserInterstitial::Create(
    std::unique_ptr<WebContentHandler> web_content_handler,
    SupervisedUserService& supervised_user_service,
    const GURL& url,
    const std::u16string& supervised_user_name,
    FilteringBehaviorReason reason) {
  std::unique_ptr<SupervisedUserInterstitial> interstitial =
      base::WrapUnique(new SupervisedUserInterstitial(
          std::move(web_content_handler), supervised_user_service, url,
          supervised_user_name, reason));

  interstitial->web_content_handler()->CleanUpInfoBarOnMainFrame();
  // Caller is responsible for deleting the interstitial.
  return interstitial;
}

SupervisedUserInterstitial::SupervisedUserInterstitial(
    std::unique_ptr<WebContentHandler> web_content_handler,
    SupervisedUserService& supervised_user_service,
    const GURL& url,
    const std::u16string& supervised_user_name,
    FilteringBehaviorReason reason)
    : supervised_user_service_(supervised_user_service),
      web_content_handler_(std::move(web_content_handler)),
      url_(url),
      supervised_user_name_(supervised_user_name),
      filtering_behavior_reason_(reason) {
  CHECK(supervised_user_service.GetURLFilter());
  url_formatter_ = std::make_unique<UrlFormatter>(
      *supervised_user_service.GetURLFilter(), reason);
}

SupervisedUserInterstitial::~SupervisedUserInterstitial() {
  web_content_handler_->MaybeCloseLocalApproval();
}

#if BUILDFLAG(IS_ANDROID)
// static
std::string SupervisedUserInterstitial::GetHTMLContentsWithoutApprovals(
    const GURL& url,
    const std::string& application_locale) {
  return BuildErrorPageHtmlWithoutApprovals(url, application_locale);
}
#endif  // BUILDFLAG(IS_ANDROID)

// static
std::string SupervisedUserInterstitial::GetHTMLContentsWithApprovals(
    SupervisedUserService* supervised_user_service,
    PrefService* pref_service,
    FilteringBehaviorReason reason,
    bool already_sent_request,
    bool is_main_frame,
    const std::string& application_locale,
    std::optional<float> ios_font_size_multiplier) {
  bool allow_access_requests =
      supervised_user_service->remote_web_approvals_manager()
          .AreApprovalRequestsEnabled();
  return BuildErrorPageHtmlWithApprovals(
      allow_access_requests, supervised_user_service->GetCustodian(),
      supervised_user_service->GetSecondCustodian(), reason, application_locale,
      already_sent_request, is_main_frame, ios_font_size_multiplier);
}

void SupervisedUserInterstitial::GoBack() {
  web_content_handler_->GoBack();
  UMA_HISTOGRAM_ENUMERATION(kInterstitialCommandHistogramName, Commands::BACK,
                            Commands::HISTOGRAM_BOUNDING_VALUE);
}

void SupervisedUserInterstitial::RequestUrlAccessRemote(
    base::OnceCallback<void(bool)> callback) {
  UMA_HISTOGRAM_ENUMERATION(kInterstitialCommandHistogramName,
                            Commands::REMOTE_ACCESS_REQUEST,
                            Commands::HISTOGRAM_BOUNDING_VALUE);
  OutputRequestPermissionSourceMetric();

  supervised_user_service_->remote_web_approvals_manager().RequestApproval(
      url_, *url_formatter_.get(), std::move(callback));
}

void SupervisedUserInterstitial::RequestUrlAccessLocal(
    base::OnceCallback<void(bool)> callback) {
  UMA_HISTOGRAM_ENUMERATION(kInterstitialCommandHistogramName,
                            Commands::LOCAL_ACCESS_REQUEST,
                            Commands::HISTOGRAM_BOUNDING_VALUE);
  OutputRequestPermissionSourceMetric();

  DLOG_IF(WARNING, supervised_user_name_.empty())
      << "Supervised user name for local web approval request should not be "
         "empty";
  web_content_handler_->RequestLocalApproval(
      url_, supervised_user_name_, *url_formatter_.get(),
      filtering_behavior_reason_, std::move(callback));
}

#if BUILDFLAG(IS_ANDROID)
void SupervisedUserInterstitial::LearnMore(base::OnceClosure open_help_page) {
  web_content_handler_->LearnMore(std::move(open_help_page));
  UMA_HISTOGRAM_ENUMERATION(kInterstitialCommandHistogramName,
                            Commands::LEARN_MORE,
                            Commands::HISTOGRAM_BOUNDING_VALUE);
}
#endif  // BUILDFLAG(IS_ANDROID)

void SupervisedUserInterstitial::OutputRequestPermissionSourceMetric() {
  RequestPermissionSource source;
  if (web_content_handler_->IsMainFrame()) {
    source = RequestPermissionSource::MAIN_FRAME;
  } else {
    source = RequestPermissionSource::SUB_FRAME;
  }

  UMA_HISTOGRAM_ENUMERATION(kInterstitialPermissionSourceHistogramName, source,
                            RequestPermissionSource::HISTOGRAM_BOUNDING_VALUE);
}
}  // namespace supervised_user
