// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_report_network_sender.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "base/values.h"
#include "build/buildflag.h"
#include "components/attribution_reporting/suitable_origin.h"
#include "content/browser/attribution_reporting/aggregatable_debug_report.h"
#include "content/browser/attribution_reporting/attribution_debug_report.h"
#include "content/browser/attribution_reporting/attribution_report.h"
#include "content/browser/attribution_reporting/attribution_utils.h"
#include "content/browser/attribution_reporting/send_result.h"
#include "net/base/isolation_info.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "third_party/abseil-cpp/absl/functional/overload.h"
#include "url/gurl.h"
#include "url/origin.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/application_status_listener.h"
#endif

namespace content {

namespace {

template <typename T>
void NetworkHistogram(std::string_view suffix,
                      void (*hist_func)(std::string_view, T value),
                      bool is_debug_report,
                      std::optional<bool> has_trigger_context_id,
                      T value) {
  if (is_debug_report) {
    hist_func(base::StrCat({"Conversions.DebugReport.", suffix}), value);
  } else {
    hist_func(base::StrCat({"Conversions.", suffix}), value);
    if (has_trigger_context_id.has_value()) {
      if (*has_trigger_context_id) {
        hist_func(base::StrCat({"Conversions.ContextID.", suffix}), value);
      } else {
        hist_func(base::StrCat({"Conversions.NoContextID.", suffix}), value);
      }
    }
  }
}

}  // namespace

AttributionReportNetworkSender::AttributionReportNetworkSender(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory))
#if BUILDFLAG(IS_ANDROID)
      ,
      application_status_listener_(
          base::android::ApplicationStatusListener::New(base::BindRepeating(
              &AttributionReportNetworkSender::OnApplicationStateChanged,
              // Listener is destroyed at destructor, and
              // object will be alive for any callback.
              base::Unretained(this)))) {
  CHECK(url_loader_factory_);
  OnApplicationStateChanged(
      base::android::ApplicationStatusListener::GetState());
}

void AttributionReportNetworkSender::OnApplicationStateChanged(
    base::android::ApplicationState state) {
  app_state_ = state;
}
#else
{
  CHECK(url_loader_factory_);
}
#endif

AttributionReportNetworkSender::~AttributionReportNetworkSender() = default;

void AttributionReportNetworkSender::SetInFirstBatch(bool in_first_batch) {
  in_first_batch_ = in_first_batch;
}

void AttributionReportNetworkSender::SendReport(
    AttributionReport report,
    bool is_debug_report,
    ReportSentCallback sent_callback) {
  GURL url = report.ReportURL(is_debug_report);
  std::string body = SerializeAttributionJson(report.ReportBody());

  if (!is_debug_report) {
    switch (report.GetReportType()) {
      case AttributionReport::Type::kEventLevel:
        base::UmaHistogramCounts1000(
            "Conversions.EventLevelReport.ReportBodySize", body.size());
        break;
      case AttributionReport::Type::kAggregatableAttribution:
      case AttributionReport::Type::kNullAggregatable:
        base::UmaHistogramCounts10000(
            "Conversions.AggregatableReport.ReportBodySize", body.size());
        break;
    }
  }

  url::Origin origin(report.reporting_origin());
  SendReport(std::move(url), std::move(origin), std::move(body),
             base::BindOnce(&AttributionReportNetworkSender::OnReportSent,
                            base::Unretained(this), std::move(report),
                            is_debug_report, std::move(sent_callback)));
}

void AttributionReportNetworkSender::SendReport(
    AttributionDebugReport report,
    DebugReportSentCallback callback) {
  GURL url(report.ReportUrl());
  url::Origin origin(report.reporting_origin());
  std::string body = SerializeAttributionJson(report.ReportBody());
  SendReport(
      std::move(url), std::move(origin), std::move(body),
      base::BindOnce(&AttributionReportNetworkSender::OnVerboseDebugReportSent,
                     base::Unretained(this),
                     base::BindOnce(std::move(callback), std::move(report))));
}

void AttributionReportNetworkSender::SendReport(
    AggregatableDebugReport report,
    base::Value::Dict report_body,
    AggregatableDebugReportSentCallback callback) {
  GURL url(report.ReportUrl());
  url::Origin origin(report.reporting_origin());
  std::string body = SerializeAttributionJson(report_body);
  SendReport(std::move(url), std::move(origin), std::move(body),
             base::BindOnce(
                 &AttributionReportNetworkSender::OnAggregatableDebugReportSent,
                 base::Unretained(this),
                 base::BindOnce(std::move(callback), std::move(report),
                                std::move(report_body))));
}

void AttributionReportNetworkSender::SendReport(GURL url,
                                                url::Origin origin,
                                                std::string body,
                                                UrlLoaderCallback callback) {
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = std::move(url);
  resource_request->method = net::HttpRequestHeaders::kPostMethod;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource_request->mode = network::mojom::RequestMode::kSameOrigin;
  resource_request->request_initiator = std::move(origin);
  resource_request->load_flags =
      net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  resource_request->trusted_params = network::ResourceRequest::TrustedParams();
  resource_request->trusted_params->isolation_info =
      net::IsolationInfo::CreateTransient(/*nonce=*/std::nullopt);

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("conversion_measurement_report", R"(
        semantics {
          sender: "Attribution Reporting API"
          description:
            "The Attribution Reporting API supports measurement of clicks and "
            "views with event-level and aggregatable reports without using "
            "cross-site persistent identifiers like third-party cookies."
          trigger:
            "When a triggered attribution has become eligible for reporting "
            "or when an attribution source or trigger registration has failed "
            "and is eligible for error reporting."
          data:
            "Event-level reports include a high-entropy identifier declared "
            "by the site on which the user clicked on or viewed a source and "
            "a noisy low-entropy data value declared on the destination site."
            "Aggregatable reports include encrypted information generated "
            "from both source-side and trigger-side registrations."
            "Debug reports include data related to attribution source or "
            "trigger registration failures."
          destination:OTHER
        }
        policy {
          cookies_allowed: NO
          setting:
            "This feature can be controlled via the 'Ad measurement' setting "
            "in the 'Ad privacy' section of 'Privacy and Security'."
          chrome_policy {
            PrivacySandboxAdMeasurementEnabled {
              PrivacySandboxAdMeasurementEnabled: false
            }
          }
        })");

  auto simple_url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), traffic_annotation);
  network::SimpleURLLoader* simple_url_loader_ptr = simple_url_loader.get();

  auto it = loaders_in_progress_.insert(loaders_in_progress_.begin(),
                                        std::move(simple_url_loader));
  simple_url_loader_ptr->SetTimeoutDuration(base::Seconds(30));

  simple_url_loader_ptr->AttachStringForUpload(std::move(body),
                                               "application/json");

  // Retry once on network change. A network change during DNS resolution
  // results in a DNS error rather than a network change error, so retry in
  // those cases as well.
  int retry_mode = network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE |
                   network::SimpleURLLoader::RETRY_ON_NAME_NOT_RESOLVED;
  simple_url_loader_ptr->SetRetryOptions(/*max_retries=*/1, retry_mode);

  simple_url_loader_ptr->DownloadHeadersOnly(
      url_loader_factory_.get(),
      base::BindOnce(std::move(callback), std::move(it)));
}

void AttributionReportNetworkSender::OnReportSent(
    const AttributionReport& report,
    const bool is_debug_report,
    ReportSentCallback sent_callback,
    UrlLoaderList::iterator it,
    scoped_refptr<net::HttpResponseHeaders> headers) {
  network::SimpleURLLoader* loader = it->get();

  const int net_error = loader->NetError();
  const bool net_ok =
      net_error == net::OK || net_error == net::ERR_HTTP_RESPONSE_CODE_FAILURE;

  // Use the analogous net error if headers are absent; previously this used -1
  // as the value, but that is a legitimate net error value
  // (`net::ERR_IO_PENDING`), which would be misleading/erroneous if ever
  // stringified, either in the internals UI or in metrics.
  const int response_code =
      headers ? headers->response_code() : net::ERR_INVALID_HTTP_RESPONSE;
  const bool http_ok = response_code >= 200 && response_code <= 299;

  const bool net_ok_and_http_ok = net_ok && http_ok;

  // Since net errors are always negative and HTTP errors are always positive,
  // it is fine to combine these in a single histogram.
  const int response_or_net_error = net_ok ? response_code : net_error;

  std::optional<bool> retry_succeed =
      loader->GetNumRetries() > 0 ? std::make_optional<bool>(net_ok_and_http_ok)
                                  : std::nullopt;

  if (in_first_batch_) {
    base::UmaHistogramSparse(
        "Conversions.FirstBatch.HttpResponseOrNetErrorCode",
        response_or_net_error);
  }

#if BUILDFLAG(IS_ANDROID)
  std::string_view suffix;
  switch (app_state_) {
    case base::android::APPLICATION_STATE_HAS_RUNNING_ACTIVITIES:
      suffix = "AppRunning";
      break;
    case base::android::APPLICATION_STATE_HAS_PAUSED_ACTIVITIES:
      suffix = "AppPaused";
      break;
    case base::android::APPLICATION_STATE_HAS_STOPPED_ACTIVITIES:
      suffix = "AppBackgrounded";
      break;
    case base::android::APPLICATION_STATE_HAS_DESTROYED_ACTIVITIES:
      suffix = "AppDestroyed";
      break;
    case base::android::APPLICATION_STATE_UNKNOWN:
      suffix = "AppStateUnknown";
      break;
  }
  base::UmaHistogramSparse(
      base::StrCat({"Conversions.HttpResponseOrNetErrorCode.", suffix}),
      response_or_net_error);
#endif

  std::visit(
      absl::Overload{
          [&](const AttributionReport::EventLevelData&) {
            NetworkHistogram("HttpResponseOrNetErrorCodeEventLevel",
                             &base::UmaHistogramSparse, is_debug_report,
                             /*has_trigger_context_id=*/std::nullopt,
                             response_or_net_error);

            if (retry_succeed.has_value()) {
              NetworkHistogram("ReportRetrySucceedEventLevel",
                               &base::UmaHistogramBoolean, is_debug_report,
                               /*has_trigger_context_id=*/std::nullopt,
                               *retry_succeed);
            }
          },
          [&](const AttributionReport::AggregatableData& data) {
            const bool has_trigger_context_id =
                data.aggregatable_trigger_config()
                    .trigger_context_id()
                    .has_value();

            NetworkHistogram("HttpResponseOrNetErrorCodeAggregatable2",
                             &base::UmaHistogramSparse, is_debug_report,
                             has_trigger_context_id, response_or_net_error);

            if (retry_succeed.has_value()) {
              NetworkHistogram("ReportRetrySucceedAggregatable2",
                               &base::UmaHistogramBoolean, is_debug_report,
                               has_trigger_context_id, *retry_succeed);
            }
          },
      },
      report.data());

  loaders_in_progress_.erase(it);

  if (net_ok_and_http_ok) {
    std::move(sent_callback)
        .Run(report,
             SendResult::Sent(SendResult::Sent::Result::kSent, response_code));
  } else {
    // Retry reports that have not received headers and failed with one of the
    // specified error codes. These codes are chosen from the
    // "Conversions.Report.HttpResponseOrNetErrorCode" histogram. HTTP errors
    // should not be retried to prevent over requesting servers.
    bool should_retry =
        !headers && (net_error == net::ERR_INTERNET_DISCONNECTED ||
                     net_error == net::ERR_NAME_NOT_RESOLVED ||
                     net_error == net::ERR_TIMED_OUT ||
                     net_error == net::ERR_CONNECTION_TIMED_OUT ||
                     net_error == net::ERR_CONNECTION_ABORTED ||
                     net_error == net::ERR_CONNECTION_RESET);
    std::move(sent_callback)
        .Run(report,
             SendResult::Sent(should_retry
                                  ? SendResult::Sent::Result::kTransientFailure
                                  : SendResult::Sent::Result::kFailure,
                              response_or_net_error));
  }
}

void AttributionReportNetworkSender::OnVerboseDebugReportSent(
    base::OnceCallback<void(int status)> callback,
    UrlLoaderList::iterator it,
    scoped_refptr<net::HttpResponseHeaders> headers) {
  // HTTP statuses are positive; network errors are negative.
  int status = headers ? headers->response_code() : (*it)->NetError();

  // Since net errors are always negative and HTTP errors are always positive,
  // it is fine to combine these in a single histogram.
  base::UmaHistogramSparse(
      "Conversions.VerboseDebugReport.HttpResponseOrNetErrorCode", status);

  loaders_in_progress_.erase(it);
  std::move(callback).Run(status);
}

void AttributionReportNetworkSender::OnAggregatableDebugReportSent(
    base::OnceCallback<void(int status)> callback,
    UrlLoaderList::iterator it,
    scoped_refptr<net::HttpResponseHeaders> headers) {
  // HTTP statuses are positive; network errors are negative.
  int status = headers ? headers->response_code() : (*it)->NetError();

  // Since net errors are always negative and HTTP errors are always positive,
  // it is fine to combine these in a single histogram.
  base::UmaHistogramSparse(
      "Conversions.AggregatableDebugReport.HttpResponseOrNetErrorCode", status);

  loaders_in_progress_.erase(it);
  std::move(callback).Run(status);
}

}  // namespace content
