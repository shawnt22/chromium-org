// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/browser/remote_suggestions_service.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "components/lens/proto/server/lens_overlay_response.pb.h"
#include "components/omnibox/browser/base_search_provider.h"
#include "components/omnibox/browser/document_suggestions_service.h"
#include "components/omnibox/browser/enterprise_search_aggregator_suggestions_service.h"
#include "components/search/search.h"
#include "components/search_engines/template_url_service.h"
#include "components/variations/net/variations_http_headers.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"

namespace {

std::string RequestTypeToString(RemoteRequestType request_type) {
  switch (request_type) {
    case RemoteRequestType::kSearch:
      return "Search";
    case RemoteRequestType::kSearchWarmup:
      return "SearchWarmup";
    case RemoteRequestType::kImages:
      return "Images";
    case RemoteRequestType::kZeroSuggest:
      return "ZeroSuggest";
    case RemoteRequestType::kZeroSuggestPrefetch:
      return "ZeroSuggestPrefetch";
    case RemoteRequestType::kDocumentSuggest:
      return "DocumentSuggest";
    case RemoteRequestType::kDeletion:
      return "Deletion";
    case RemoteRequestType::kEnterpriseSearchAggregatorSuggest:
      return "EnterpriseSearchAggregatorSuggest";
  }
}

std::string ResponseCodeToSuccessString(int response_code) {
  return response_code == 200 ? "Successful" : "Failed";
}

void LogRequestSent(
    RemoteRequestType request_type,
    metrics::OmniboxEventProto::PageClassification page_classification) {
  base::UmaHistogramEnumeration("Omnibox.SuggestRequestsSent", request_type);

  // Don't slice by page classification for invalid page classifications.
  if (page_classification == metrics::OmniboxEventProto::INVALID_SPEC) {
    return;
  }
  const std::string page_context =
      metrics::OmniboxEventProto::PageClassification_Name(page_classification);
  base::UmaHistogramEnumeration(
      base::StringPrintf("Omnibox.SuggestRequestsSent.%s", page_context),
      request_type);
}

void LogResponseCode(
    RemoteRequestType request_type,
    int response_code,
    metrics::OmniboxEventProto::PageClassification page_classification) {
  base::UmaHistogramSparse("Omnibox.SuggestRequestsSent.HttpResponseCode",
                           response_code);
  base::UmaHistogramSparse(
      base::StringPrintf("Omnibox.SuggestRequestsSent.HttpResponseCode.%s",
                         RequestTypeToString(request_type)),
      response_code);

  // Don't slice by page classification for invalid page classifications.
  if (page_classification == metrics::OmniboxEventProto::INVALID_SPEC) {
    return;
  }
  const std::string page_context =
      metrics::OmniboxEventProto::PageClassification_Name(page_classification);
  base::UmaHistogramSparse(
      base::StringPrintf("Omnibox.SuggestRequestsSent.HttpResponseCode.%s",
                         page_context),
      response_code);
  base::UmaHistogramSparse(
      base::StringPrintf("Omnibox.SuggestRequestsSent.HttpResponseCode.%s.%s",
                         page_context, RequestTypeToString(request_type)),
      response_code);
}

void LogResponseTimeAndCode(
    metrics::OmniboxEventProto::PageClassification page_classification,
    RemoteRequestType request_type,
    base::TimeDelta response_time,
    int response_code) {
  base::UmaHistogramTimes("Omnibox.SuggestRequestsSent.ResponseTime",
                          response_time);
  base::UmaHistogramTimes(
      base::StringPrintf("Omnibox.SuggestRequestsSent.ResponseTime.%s",
                         RequestTypeToString(request_type)),
      response_time);
  base::UmaHistogramTimes(
      base::StringPrintf("Omnibox.SuggestRequestsSent.ResponseTime.%s",
                         ResponseCodeToSuccessString(response_code)),
      response_time);
  base::UmaHistogramTimes(
      base::StringPrintf("Omnibox.SuggestRequestsSent.ResponseTime.%s.%s",
                         RequestTypeToString(request_type),
                         ResponseCodeToSuccessString(response_code)),
      response_time);

  // Don't slice by page classification for invalid page classifications.
  if (page_classification == metrics::OmniboxEventProto::INVALID_SPEC) {
    return;
  }
  const std::string page_context =
      metrics::OmniboxEventProto::PageClassification_Name(page_classification);

  base::UmaHistogramTimes(
      base::StringPrintf("Omnibox.SuggestRequestsSent.ResponseTime.%s",
                         page_context),
      response_time);

  base::UmaHistogramTimes(
      base::StringPrintf("Omnibox.SuggestRequestsSent.ResponseTime.%s.%s.%s",
                         page_context, RequestTypeToString(request_type),
                         ResponseCodeToSuccessString(response_code)),
      response_time);
}

void AddVariationHeaders(network::ResourceRequest* request,
                         bool is_off_the_record) {
  // We only care about the experiment IDs from the variations server which do
  // not require knowing the signed-in state.
  variations::AppendVariationsHeaderUnknownSignedIn(
      request->url,
      is_off_the_record ? variations::InIncognito::kYes
                        : variations::InIncognito::kNo,
      request);
}

// Adds query params to the url from the search terms args
// Lens overlay suggest inputs.
GURL AddLensOverlaySuggestInputsDataToEndpointUrl(
    TemplateURLRef::SearchTermsArgs search_terms_args,
    const GURL& url_to_modify) {
  auto lens_overlay_suggest_inputs =
      search_terms_args.lens_overlay_suggest_inputs;
  if (!lens_overlay_suggest_inputs.has_value()) {
    return url_to_modify;
  }
  GURL modified_url = GURL(url_to_modify);
  bool send_request_and_session_ids = false;
  bool send_vit = false;

  if (search_terms_args.page_classification ==
      metrics::OmniboxEventProto::CONTEXTUAL_SEARCHBOX) {
    send_request_and_session_ids =
        lens_overlay_suggest_inputs
            ->send_gsession_vsrid_for_contextual_suggest();
    send_vit = true;
    modified_url =
        net::AppendOrReplaceQueryParameter(modified_url, "gs_ps", "1");
  } else if (search_terms_args.page_classification ==
             metrics::OmniboxEventProto::LENS_SIDE_PANEL_SEARCHBOX) {
    if (lens_overlay_suggest_inputs
            ->send_gsession_vsrid_vit_for_lens_suggest()) {
      send_request_and_session_ids = true;
      send_vit = true;
    }
    if (lens_overlay_suggest_inputs->has_encoded_image_signals()) {
      modified_url = net::AppendOrReplaceQueryParameter(
          modified_url, "iil",
          lens_overlay_suggest_inputs->encoded_image_signals());
    }
    if (lens_overlay_suggest_inputs->send_vsint_for_lens_suggest() &&
        lens_overlay_suggest_inputs
            ->has_encoded_visual_search_interaction_log_data()) {
      modified_url = net::AppendOrReplaceQueryParameter(
          modified_url, "vsint",
          lens_overlay_suggest_inputs
              ->encoded_visual_search_interaction_log_data());
    }
  }

  if (send_vit &&
      lens_overlay_suggest_inputs->has_contextual_visual_input_type()) {
    modified_url = net::AppendOrReplaceQueryParameter(
        modified_url, "vit",
        lens_overlay_suggest_inputs->contextual_visual_input_type());
  }

  if (send_request_and_session_ids) {
    if (lens_overlay_suggest_inputs->has_encoded_request_id()) {
      modified_url = net::AppendOrReplaceQueryParameter(
          modified_url, "vsrid",
          lens_overlay_suggest_inputs->encoded_request_id());
    }
    if (lens_overlay_suggest_inputs->has_search_session_id()) {
      modified_url = net::AppendOrReplaceQueryParameter(
          modified_url, "gsessionid",
          lens_overlay_suggest_inputs->search_session_id());
    }
  }
  return modified_url;
}

}  // namespace

RemoteSuggestionsService::Delegate::Delegate() = default;

RemoteSuggestionsService::Delegate::~Delegate() = default;

RemoteSuggestionsService::RemoteSuggestionsService(
    DocumentSuggestionsService* document_suggestions_service,
    EnterpriseSearchAggregatorSuggestionsService*
        enterprise_search_aggregator_suggestions_service,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : document_suggestions_service_(document_suggestions_service),
      enterprise_search_aggregator_suggestions_service_(
          enterprise_search_aggregator_suggestions_service),
      url_loader_factory_(url_loader_factory) {
  DCHECK(url_loader_factory);
}

RemoteSuggestionsService::~RemoteSuggestionsService() = default;

// TODO(crbug.com/404591650): Create a struct to automate the lifecycle of
//   `time_request_sent_`.
void RemoteSuggestionsService::SetTimeRequestSent(
    RemoteRequestType request_type,
    base::TimeTicks time) {
  time_request_sent_[request_type] = time;
}

// static
GURL RemoteSuggestionsService::EndpointUrl(
    const TemplateURL& template_url,
    const TemplateURLRef::SearchTermsArgs& search_terms_args,
    const SearchTermsData& search_terms_data) {
  GURL url = GURL(template_url.suggestions_url_ref().ReplaceSearchTerms(
      search_terms_args, search_terms_data));

  // Return early for non-Google template URLs.
  if (!search::TemplateURLIsGoogle(&template_url, search_terms_data)) {
    return url;
  }

  // Append or replace client= and sclient= based on `page_classification`.
  switch (search_terms_args.page_classification) {
    case metrics::OmniboxEventProto::CHROMEOS_APP_LIST: {
      // Append `sclient=cros-launcher` for CrOS app_list launcher entry point.
      url = net::AppendOrReplaceQueryParameter(url, "sclient", "cros-launcher");
      break;
    }
    case metrics::OmniboxEventProto::CONTEXTUAL_SEARCHBOX:
    case metrics::OmniboxEventProto::SEARCH_SIDE_PANEL_SEARCHBOX:
      // Append `client=chrome-contextual` for non-multimodal and contextual
      // lens searchboxes.
      url = net::AppendOrReplaceQueryParameter(url, "client",
                                               "chrome-contextual");
      break;
    case metrics::OmniboxEventProto::LENS_SIDE_PANEL_SEARCHBOX: {
      // Append `client=chrome-multimodal` for the multimodal lens searchbox.
      url = net::AppendOrReplaceQueryParameter(url, "client",
                                               "chrome-multimodal");
      break;
    }
    default:
      break;
  }
  url = AddLensOverlaySuggestInputsDataToEndpointUrl(search_terms_args, url);

  return url;
}

std::unique_ptr<network::SimpleURLLoader>
RemoteSuggestionsService::StartSuggestionsRequest(
    RemoteRequestType request_type,
    bool is_off_the_record,
    const TemplateURL* template_url,
    TemplateURLRef::SearchTermsArgs search_terms_args,
    const SearchTermsData& search_terms_data,
    CompletionCallback completion_callback) {
  DCHECK(template_url);

  const GURL suggest_url =
      EndpointUrl(*template_url, search_terms_args, search_terms_data);
  if (!suggest_url.is_valid()) {
    return nullptr;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("omnibox_suggest", R"(
        semantics {
          sender: "Omnibox"
          description:
            "Chrome can provide search and navigation suggestions from the "
            "currently-selected search provider in the omnibox dropdown, based "
            "on user input."
          trigger: "User typing in the omnibox."
          data:
            "The text typed into the address bar. Potentially other metadata, "
            "such as the current cursor position or URL of the current page."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "Users can control this feature via the 'Use a prediction service "
            "to help complete searches and URLs typed in the address bar' "
            "setting under 'Privacy'. The feature is enabled by default."
          chrome_policy {
            SearchSuggestEnabled {
                policy_options {mode: MANDATORY}
                SearchSuggestEnabled: false
            }
          }
        })");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = suggest_url;
  request->load_flags = net::LOAD_DO_NOT_SAVE_COOKIES;
  // Set the SiteForCookies to the request URL's site to avoid cookie blocking.
  request->site_for_cookies = net::SiteForCookies::FromUrl(suggest_url);
  // Add Chrome experiment state to the request headers.
  AddVariationHeaders(request.get(), is_off_the_record);

  // Create a unique identifier for the request.
  const base::UnguessableToken request_id = base::UnguessableToken::Create();

  OnRequestCreated(request_id, request.get());

  // Make loader and start download.
  base::ElapsedTimer request_timer;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  loader->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&RemoteSuggestionsService::OnRequestCompleted,
                     weak_ptr_factory_.GetWeakPtr(), request_id, request_type,
                     std::move(request_timer),
                     search_terms_args.page_classification,
                     std::move(completion_callback), loader.get()));

  OnRequestStarted(request_id, request_type,
                   search_terms_args.page_classification, loader.get(),
                   /*request_body*/ "");
  return loader;
}

std::unique_ptr<network::SimpleURLLoader>
RemoteSuggestionsService::StartZeroPrefixSuggestionsRequest(
    RemoteRequestType request_type,
    bool is_off_the_record,
    const TemplateURL* template_url,
    TemplateURLRef::SearchTermsArgs search_terms_args,
    const SearchTermsData& search_terms_data,
    CompletionCallback completion_callback) {
  DCHECK(template_url);

  const GURL suggest_url =
      EndpointUrl(*template_url, search_terms_args, search_terms_data);
  if (!suggest_url.is_valid()) {
    return nullptr;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("omnibox_zerosuggest", R"(
        semantics {
          sender: "Omnibox"
          description:
            "When the user focuses the omnibox, Chrome can provide search or "
            "navigation suggestions from the default search provider in the "
            "omnibox dropdown, based on the current page URL.\n"
            "This is limited to users whose default search engine is Google, "
            "as no other search engines currently support this kind of "
            "suggestion."
          trigger: "The omnibox receives focus."
          data: "The URL of the current page."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "Users can control this feature via the 'Use a prediction service "
            "to help complete searches and URLs typed in the address bar' "
            "settings under 'Privacy'. The feature is enabled by default."
          chrome_policy {
            SearchSuggestEnabled {
                policy_options {mode: MANDATORY}
                SearchSuggestEnabled: false
            }
          }
        })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = suggest_url;
  request->load_flags = net::LOAD_DO_NOT_SAVE_COOKIES;
  if (search_terms_args.bypass_cache) {
    request->load_flags |= net::LOAD_BYPASS_CACHE;
  }
  // Set the SiteForCookies to the request URL's site to avoid cookie blocking.
  request->site_for_cookies = net::SiteForCookies::FromUrl(suggest_url);
  // Add Chrome experiment state to the request headers.
  AddVariationHeaders(request.get(), is_off_the_record);

  // Create a unique identifier for the request.
  const base::UnguessableToken request_id = base::UnguessableToken::Create();

  OnRequestCreated(request_id, request.get());

  // Make loader and start download.
  base::ElapsedTimer request_timer;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  loader->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&RemoteSuggestionsService::OnRequestCompleted,
                     weak_ptr_factory_.GetWeakPtr(), request_id, request_type,
                     std::move(request_timer),
                     search_terms_args.page_classification,
                     std::move(completion_callback), loader.get()));

  OnRequestStarted(request_id, request_type,
                   search_terms_args.page_classification, loader.get(),
                   /*request_body*/ "");
  return loader;
}

void RemoteSuggestionsService::CreateDocumentSuggestionsRequest(
    const std::u16string& query,
    bool is_off_the_record,
    metrics::OmniboxEventProto::PageClassification page_classification,
    StartCallback start_callback,
    CompletionCallback completion_callback) {
  if (!document_suggestions_service_) {
    return;
  }

  // Create a unique identifier for the request.
  const base::UnguessableToken request_id = base::UnguessableToken::Create();

  base::ElapsedTimer request_timer;
  document_suggestions_service_->CreateDocumentSuggestionsRequest(
      query, is_off_the_record,
      base::BindOnce(&RemoteSuggestionsService::OnRequestCreated,
                     weak_ptr_factory_.GetWeakPtr(), request_id),
      base::BindOnce(&RemoteSuggestionsService::OnRequestStartedAsync,
                     weak_ptr_factory_.GetWeakPtr(), request_id,
                     /*request_type=*/RemoteRequestType::kDocumentSuggest,
                     page_classification, std::move(start_callback)),
      base::BindOnce(&RemoteSuggestionsService::OnRequestCompleted,
                     weak_ptr_factory_.GetWeakPtr(), request_id,
                     /*request_type=*/RemoteRequestType::kDocumentSuggest,
                     std::move(request_timer), page_classification,
                     std::move(completion_callback)));
}

void RemoteSuggestionsService::StopCreatingDocumentSuggestionsRequest() {
  if (document_suggestions_service_) {
    document_suggestions_service_->StopCreatingDocumentSuggestionsRequest();
  }
}

void RemoteSuggestionsService::
    CreateEnterpriseSearchAggregatorSuggestionsRequest(
        const std::u16string& query,
        const GURL& suggest_url,
        metrics::OmniboxEventProto::PageClassification page_classification,
        std::vector<int> callback_indexes,
        std::vector<std::vector<int>> suggestion_types,
        IndexedStartCallback start_callback,
        IndexedCompletionCallback completion_callback) {
  if (!enterprise_search_aggregator_suggestions_service_) {
    return;
  }

  // Create a unique identifier for the request.
  const base::UnguessableToken request_id = base::UnguessableToken::Create();

  enterprise_search_aggregator_suggestions_service_
      ->CreateEnterpriseSearchAggregatorSuggestionsRequest(
          query, suggest_url, callback_indexes, suggestion_types,
          base::BindRepeating(&RemoteSuggestionsService::OnRequestCreated,
                              weak_ptr_factory_.GetWeakPtr(), request_id),
          base::BindRepeating(
              &RemoteSuggestionsService::OnIndexedRequestStartedAsync,
              weak_ptr_factory_.GetWeakPtr(), request_id,
              /*request_type=*/
              RemoteRequestType::kEnterpriseSearchAggregatorSuggest,
              page_classification, start_callback),
          base::BindRepeating(
              &RemoteSuggestionsService::OnIndexedRequestCompleted,
              weak_ptr_factory_.GetWeakPtr(), request_id,
              /*request_type=*/
              RemoteRequestType::kEnterpriseSearchAggregatorSuggest,
              metrics::OmniboxEventProto::INVALID_SPEC, base::TimeTicks::Now(),
              std::move(completion_callback)));
}

void RemoteSuggestionsService::
    StopCreatingEnterpriseSearchAggregatorSuggestionsRequest() {
  if (enterprise_search_aggregator_suggestions_service_) {
    enterprise_search_aggregator_suggestions_service_
        ->StopCreatingEnterpriseSearchAggregatorSuggestionsRequest();
  }
}

std::unique_ptr<network::SimpleURLLoader>
RemoteSuggestionsService::StartDeletionRequest(
    const std::string& deletion_url,
    bool is_off_the_record,
    CompletionCallback completion_callback) {
  const GURL url(deletion_url);
  DCHECK(url.is_valid());

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("omnibox_suggest_deletion", R"(
        semantics {
          sender: "Omnibox"
          description:
            "When users attempt to delete server-provided personalized search "
            "or navigation suggestions from the omnibox dropdown, Chrome sends "
            "a message to the server requesting deletion of the suggestion."
          trigger:
            "A user attempt to delete a server-provided omnibox suggestion, "
            "for which the server provided a custom deletion URL."
          data:
            "No user data is explicitly sent with the request, but because the "
            "requested URL is provided by the server for each specific "
            "suggestion, it necessarily uniquely identifies the suggestion the "
            "user is attempting to delete."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "Since this can only be triggered on seeing server-provided "
            "suggestions in the omnibox dropdown, whether it is enabled is the "
            "same as whether those suggestions are enabled.\n"
            "Users can control this feature via the 'Use a prediction service "
            "to help complete searches and URLs typed in the address bar' "
            "setting under 'Privacy'. The feature is enabled by default."
          chrome_policy {
            SearchSuggestEnabled {
                policy_options {mode: MANDATORY}
                SearchSuggestEnabled: false
            }
          }
        })");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  // Set the SiteForCookies to the request URL's site to avoid cookie blocking.
  request->site_for_cookies = net::SiteForCookies::FromUrl(url);
  // Add Chrome experiment state to the request headers.
  AddVariationHeaders(request.get(), is_off_the_record);

  // Create a unique identifier for the request.
  const base::UnguessableToken request_id = base::UnguessableToken::Create();

  OnRequestCreated(request_id, request.get());

  // Make loader and start download.
  base::ElapsedTimer request_timer;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  loader->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&RemoteSuggestionsService::OnRequestCompleted,
                     weak_ptr_factory_.GetWeakPtr(), request_id,
                     /*request_type=*/RemoteRequestType::kDeletion,
                     std::move(request_timer),
                     metrics::OmniboxEventProto::INVALID_SPEC,
                     std::move(completion_callback), loader.get()));

  OnRequestStarted(request_id, /*request_type=*/RemoteRequestType::kDeletion,
                   metrics::OmniboxEventProto::INVALID_SPEC, loader.get(),
                   /*request_body*/ "");
  return loader;
}

void RemoteSuggestionsService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void RemoteSuggestionsService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void RemoteSuggestionsService::SetDelegate(base::WeakPtr<Delegate> delegate) {
  delegate_ = std::move(delegate);
}

void RemoteSuggestionsService::set_url_loader_factory_for_testing(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  url_loader_factory_ = std::move(url_loader_factory);
}

void RemoteSuggestionsService::OnRequestCreated(
    const base::UnguessableToken& request_id,
    network::ResourceRequest* request) {
  // Notify the observers that request has been created.
  observers_.Notify(&Observer::OnRequestCreated, request_id, request);
}

void RemoteSuggestionsService::OnRequestStarted(
    const base::UnguessableToken& request_id,
    RemoteRequestType request_type,
    metrics::OmniboxEventProto::PageClassification page_classification,
    network::SimpleURLLoader* loader,
    const std::string& request_body) {
  // Notify the observers that the transfer started.
  observers_.Notify(&Observer::OnRequestStarted, request_id, loader,
                    request_body);
  LogRequestSent(request_type, page_classification);
}

void RemoteSuggestionsService::OnRequestStartedAsync(
    const base::UnguessableToken& request_id,
    RemoteRequestType request_type,
    metrics::OmniboxEventProto::PageClassification page_classification,
    StartCallback start_callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    const std::string& request_body) {
  OnRequestStarted(request_id, request_type, page_classification, loader.get(),
                   request_body);
  std::move(start_callback).Run(std::move(loader));
}

void RemoteSuggestionsService::OnIndexedRequestStartedAsync(
    const base::UnguessableToken& request_id,
    RemoteRequestType request_type,
    metrics::OmniboxEventProto::PageClassification page_classification,
    IndexedStartCallback start_callback,
    int request_index,
    std::unique_ptr<network::SimpleURLLoader> loader,
    const std::string& request_body) {
  OnRequestStarted(request_id, request_type, page_classification, loader.get(),
                   request_body);
  std::move(start_callback).Run(request_index, std::move(loader));
}

void RemoteSuggestionsService::OnRequestCompleted(
    const base::UnguessableToken& request_id,
    RemoteRequestType request_type,
    base::ElapsedTimer request_timer,
    metrics::OmniboxEventProto::PageClassification page_classification,
    CompletionCallback completion_callback,
    const network::SimpleURLLoader* source,
    std::unique_ptr<std::string> response_body) {
  const int response_code =
      source->ResponseInfo() && source->ResponseInfo()->headers
          ? source->ResponseInfo()->headers->response_code()
          : 0;

  // Notify the observers that the transfer is done.
  observers_.Notify(&Observer::OnRequestCompleted, request_id, response_code,
                    response_body);
  LogResponseCode(request_type, response_code, page_classification);
  LogResponseTimeAndCode(page_classification, request_type,
                         request_timer.Elapsed(), response_code);

  // Call the completion callback or delegate it.
  if (delegate_) {
    delegate_->OnRequestCompleted(source, response_code,
                                  std::move(response_body),
                                  std::move(completion_callback));
  } else {
    std::move(completion_callback)
        .Run(source, response_code, std::move(response_body));
  }
}

void RemoteSuggestionsService::OnIndexedRequestCompleted(
    const base::UnguessableToken& request_id,
    RemoteRequestType request_type,
    metrics::OmniboxEventProto::PageClassification page_classification,
    base::TimeTicks start_time,
    IndexedCompletionCallback completion_callback,
    const network::SimpleURLLoader* source,
    int request_index,
    std::unique_ptr<std::string> response_body) {
  const int response_code =
      source->ResponseInfo() && source->ResponseInfo()->headers
          ? source->ResponseInfo()->headers->response_code()
          : 0;
  // Notify the observers that the transfer is done.
  observers_.Notify(&Observer::OnRequestCompleted, request_id, response_code,
                    response_body);
  LogResponseCode(request_type, response_code, page_classification);
  LogResponseTimeAndCode(page_classification, request_type,
                         base::TimeTicks::Now() - start_time, response_code);

  // Call the completion callback or delegate it.
  if (delegate_) {
    delegate_->OnIndexedRequestCompleted(request_index, source, response_code,
                                         std::move(response_body),
                                         completion_callback);
  } else {
    std::move(completion_callback)
        .Run(request_index, source, response_code, std::move(response_body));
  }
}
