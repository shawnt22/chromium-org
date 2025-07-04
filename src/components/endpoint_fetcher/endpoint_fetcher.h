// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ENDPOINT_FETCHER_ENDPOINT_FETCHER_H_
#define COMPONENTS_ENDPOINT_FETCHER_ENDPOINT_FETCHER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/identity_manager/primary_account_access_token_fetcher.h"
#include "components/signin/public/identity_manager/scope_set.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/json_sanitizer.h"

namespace base {
class TimeDelta;
}  // namespace base

namespace network {
class SimpleURLLoader;
class SharedURLLoaderFactory;
}  // namespace network

namespace signin {
struct AccessTokenInfo;
class IdentityManager;
}  // namespace signin

namespace version_info {
enum class Channel;
}

class GoogleServiceAuthError;
class GURL;

namespace endpoint_fetcher {

class EndpointFetcherTest;

enum class CredentialsMode {
  kOmit = 0,
  kInclude = 1,
};

enum class FetchErrorType {
  kAuthError = 0,
  kNetError = 1,
  kResultParseError = 2,
};

enum class HttpMethod {
  kUndefined = -1,
  kGet = 0,
  kPost = 1,
  kDelete = 2,
};

enum AuthType {
  // Unique identifier to access various server-side APIs Chrome uses.
  CHROME_API_KEY,
  // Authorization protocol to access an API based on account permissions.
  OAUTH,
  // No authentication used.
  NO_AUTH
};

struct EndpointResponse {
  std::string response;
  int http_status_code{-1};
  std::optional<FetchErrorType> error_type;
};

using EndpointFetcherCallback =
    base::OnceCallback<void(std::unique_ptr<EndpointResponse>)>;
using UploadProgressCallback =
    base::RepeatingCallback<void(uint64_t position, uint64_t total)>;

// TODO(crbug.com/284531303) EndpointFetcher would benefit from
// re-design/rethinking the APIs.
// EndpointFetcher calls an endpoint and returns
// the response. EndpointFetcher is not thread safe and it is up to the caller
// to wait until the callback function passed to Fetch() completes
// before invoking Fetch() again.
// Destroying an EndpointFetcher will result in the in-flight request being
// cancelled.
// EndpointFetcher performs authentication via the signed in user to
// Chrome.
// If the request times out an empty response will be returned. There will also
// be an error code indicating timeout once more detailed error messaging is
// added TODO(crbug.com/40640190).
class EndpointFetcher {
 public:
  // Parameters the client can configure for the request. This is part of our
  // long term plan to move request parameters (e.g. URL, headers) to one
  // centralized struct as adding additional parameters to the EndpointFetcher
  // constructor does/will not scale. New parameters will be added here and
  // existing parameters will be migrated (crbug.com/357567879).
  class RequestParams {
   public:
    RequestParams(const HttpMethod& method,
                  const net::NetworkTrafficAnnotationTag& annotation_tag);
    RequestParams(const EndpointFetcher::RequestParams& other);
    ~RequestParams();

    struct Header {
      std::string key;
      std::string value;

      Header(const std::string& key, const std::string& value) {
        this->key = key;
        this->value = value;
      }
    };

    const AuthType& auth_type() const { return auth_type_; }

    const GURL& url() const { return url_; }

    const HttpMethod& http_method() const { return http_method_; }

    const base::TimeDelta& timeout() const { return timeout_; }

    const std::optional<std::string>& post_data() const { return post_data_; }

    const std::vector<Header>& headers() const { return headers_; }

    const std::vector<Header>& cors_exempt_headers() const {
      return cors_exempt_headers_;
    }

    const net::NetworkTrafficAnnotationTag annotation_tag() const {
      return annotation_tag_;
    }

    const std::string& content_type() const { return content_type_; }

    std::optional<CredentialsMode> credentials_mode;
    std::optional<int> max_retries;
    std::optional<bool> set_site_for_cookies;
    std::optional<UploadProgressCallback> upload_progress_callback;

    // Authentication-specific parameters
    std::optional<std::string> oauth_consumer_name;
    signin::ScopeSet oauth_scopes;
    std::optional<signin::ConsentLevel> consent_level;
    std::optional<version_info::Channel> channel;

    // Response behavior parameters
    std::optional<bool> sanitize_response{true};

    class Builder final {
     public:
      Builder(const HttpMethod& method,
              const net::NetworkTrafficAnnotationTag& annotation_tag);

      explicit Builder(const EndpointFetcher::RequestParams& other);

      Builder(const Builder&) = delete;
      Builder& operator=(const Builder&) = delete;

      ~Builder();

      // Contains consistency DCHECKs.
      RequestParams Build();

      Builder& SetUrl(const GURL& url) {
        request_params_->url_ = url;
        return *this;
      }

      Builder& SetTimeout(const base::TimeDelta& timeout) {
        request_params_->timeout_ = timeout;
        return *this;
      }

      Builder& SetCredentialsMode(const CredentialsMode& mode) {
        request_params_->credentials_mode = mode;
        return *this;
      }

      Builder& SetMaxRetries(const int retries) {
        request_params_->max_retries = retries;
        return *this;
      }

      Builder& SetSetSiteForCookies(const bool should_set_site_for_cookies) {
        request_params_->set_site_for_cookies = should_set_site_for_cookies;
        return *this;
      }

      Builder& SetUploadProgressCallback(
          const UploadProgressCallback callback) {
        request_params_->upload_progress_callback = callback;
        return *this;
      }

      Builder& SetPostData(const std::string& post_data) {
        request_params_->post_data_ = post_data;
        return *this;
      }

      Builder& SetHeaders(const std::vector<Header>& headers) {
        request_params_->headers_ = std::move(headers);
        return *this;
      }

      // Only use for legacy setting of Headers. Please use
      // SetCorsExemptHeaders(const std::vector<Header... for any new usage of
      // the EndpointFetcher.
      Builder& SetHeaders(const std::vector<std::string>& headers) {
        // The key and value alternate in this vector, so there is an
        // expectation the vector is of even length.
        DCHECK_EQ(headers.size() % 2, 0UL);
        for (size_t i = 0; i + 1 < headers.size(); i += 2) {
          request_params_->headers_.emplace_back(headers[i], headers[i + 1]);
        }
        return *this;
      }

      Builder& SetCorsExemptHeaders(
          const std::vector<Header>& cors_exempt_headers) {
        request_params_->cors_exempt_headers_ = std::move(cors_exempt_headers);
        return *this;
      }

      // Only use for legacy setting of Cors Exempt Headers. Please use
      // SetCorsExemptHeaders(const std::vector<Header... for any new usage of
      // the EndpointFetcher.
      Builder& SetCorsExemptHeaders(
          const std::vector<std::string>& cors_exempt_headers) {
        // The key and value alternate in this vector, so there is an
        // expectation the vector is of even length.
        DCHECK_EQ(cors_exempt_headers.size() % 2, 0UL);
        for (size_t i = 0; i + 1 < cors_exempt_headers.size(); i += 2) {
          request_params_->headers_.emplace_back(cors_exempt_headers[i],
                                                 cors_exempt_headers[i + 1]);
        }
        return *this;
      }

      Builder& SetAuthType(const AuthType auth_type) {
        request_params_->auth_type_ = auth_type;
        return *this;
      }

      Builder& SetContentType(const std::string& content_type) {
        request_params_->content_type_ = content_type;
        return *this;
      }

      // Authentication-specific builder methods
      Builder& SetOauthConsumerName(const std::string& name) {
        request_params_->oauth_consumer_name = name;
        return *this;
      }

      Builder& SetOauthScopes(const signin::ScopeSet& scopes) {
        request_params_->oauth_scopes = scopes;
        return *this;
      }

      Builder& SetOauthScopes(const std::vector<std::string>& scopes_vector) {
        for (const auto& scope : scopes_vector) {
          request_params_->oauth_scopes.insert(scope);
        }
        return *this;
      }

      Builder& SetConsentLevel(signin::ConsentLevel level) {
        request_params_->consent_level = level;
        return *this;
      }

      Builder& SetChannel(version_info::Channel channel_val) {
        request_params_->channel = channel_val;
        return *this;
      }

      // Response behavior builder methods
      Builder& SetSanitizeResponse(bool sanitize) {
        request_params_->sanitize_response = sanitize;
        return *this;
      }

     private:
      std::unique_ptr<RequestParams> request_params_;
    };

   private:
    friend class EndpointFetcher::RequestParams::Builder;
    GURL url_;
    HttpMethod http_method_{HttpMethod::kUndefined};
    base::TimeDelta timeout_{base::Milliseconds(0)};
    AuthType auth_type_{NO_AUTH};
    std::string content_type_;
    std::optional<std::string> post_data_;
    std::vector<Header> headers_;
    std::vector<Header> cors_exempt_headers_;
    net::NetworkTrafficAnnotationTag annotation_tag_;
  };

  // Preferred constructor - forms identity_manager and url_loader_factory.
  // OAUTH authentication is used for this constructor.
  //
  // Note: When using signin::ConsentLevel::kSignin, please also make sure that
  // your `scopes` are correctly set in AccessTokenRestrictions, otherwise
  // AccessTokenFetcher will assume the `scopes` requires full access and crash
  // if user doesn't have full access (e.g. sign in but not sync).
  // TODO(crbug.com/382343700): Add a DCHECK to enforce this in EndPointFetcher.
  EndpointFetcher(
      const scoped_refptr<network::SharedURLLoaderFactory>& url_loader_factory,
      signin::IdentityManager* identity_manager,
      RequestParams request_params);

  // Less preferred convenience constructor for OAuth authenticated requests.
  //
  // This constructor internally configures `RequestParams` for OAuth
  // authentication using the provided details.
  //
  // For new code, prefer constructing `RequestParams` directly and using the
  // primary `EndpointFetcher(url_loader_factory, identity_manager,
  // request_params)` constructor.
  EndpointFetcher(
      const scoped_refptr<network::SharedURLLoaderFactory>& url_loader_factory,
      const std::string& oauth_consumer_name,
      const GURL& url,
      const std::string& http_method,
      const std::string& content_type,
      const std::vector<std::string>& scopes,
      const base::TimeDelta& timeout,
      const std::string& post_data,
      const net::NetworkTrafficAnnotationTag& annotation_tag,
      signin::IdentityManager* identity_manager,
      signin::ConsentLevel consent_level);

  // Less preferred convenience constructor for Chrome API Key authenticated
  // requests.
  //
  // This constructor configures `RequestParams` for Chrome API Key
  // authentication using the provided `channel` and other network parameters.
  // It may override some settings from the passed `request_params` argument.
  //
  // For new code, prefer constructing `RequestParams` directly (including
  // setting `AuthType::CHROME_API_KEY` and `channel`) and using the primary
  // `EndpointFetcher(url_loader_factory, identity_manager, request_params)`
  // constructor (with `identity_manager` as nullptr for API key auth).
  EndpointFetcher(
      const scoped_refptr<network::SharedURLLoaderFactory>& url_loader_factory,
      const GURL& url,
      const std::string& content_type,
      const base::TimeDelta& timeout,
      const std::string& post_data,
      const std::vector<std::string>& headers,
      const std::vector<std::string>& cors_exempt_headers,
      version_info::Channel channel,
      const RequestParams request_params);

  // Less preferred convenience constructor for requests requiring no
  // authentication.
  //
  // This constructor configures `RequestParams` for `NO_AUTH`.
  //
  // For new code, prefer constructing `RequestParams` directly (setting
  // `AuthType::NO_AUTH`) and using the primary
  // `EndpointFetcher(url_loader_factory, identity_manager, request_params)`
  // constructor (with `identity_manager` as nullptr).
  EndpointFetcher(
      const scoped_refptr<network::SharedURLLoaderFactory>& url_loader_factory,
      const GURL& url,
      const net::NetworkTrafficAnnotationTag& annotation_tag);

  // Used internally. Can be used if caller constructs their own
  // url_loader_factory and identity_manager.
  EndpointFetcher(
      const std::string& oauth_consumer_name,
      const GURL& url,
      const std::string& http_method,
      const std::string& content_type,
      const std::vector<std::string>& scopes,
      const base::TimeDelta& timeout,
      const std::string& post_data,
      const net::NetworkTrafficAnnotationTag& annotation_tag,
      const scoped_refptr<network::SharedURLLoaderFactory>& url_loader_factory,
      signin::IdentityManager* identity_manager,
      signin::ConsentLevel consent_level);

  EndpointFetcher(const EndpointFetcher& endpoint_fetcher) = delete;
  EndpointFetcher& operator=(const EndpointFetcher& endpoint_fetcher) = delete;

  virtual ~EndpointFetcher();

  // TODO(crbug.com/40642723) enable cancellation support
  virtual void Fetch(EndpointFetcherCallback callback);
  // Deprecated, use Fetch().
  virtual void PerformRequest(EndpointFetcherCallback endpoint_fetcher_callback,
                              const char* key);

  std::string GetUrlForTesting();

 protected:
  // Used for Mock only. see MockEndpointFetcher class.
  explicit EndpointFetcher(
      const net::NetworkTrafficAnnotationTag& annotation_tag);

 private:
  friend class EndpointFetcherTest;

  void OnAuthTokenFetched(EndpointFetcherCallback callback,
                          GoogleServiceAuthError error,
                          signin::AccessTokenInfo access_token_info);

  // Private helper, replaces PerformRequest.
  void PerformHttpRequest(const char* auth_token_key,
                          EndpointFetcherCallback endpoint_fetcher_callback);

  void OnResponseFetched(EndpointFetcherCallback callback,
                         std::unique_ptr<std::string> response_body);
  void OnSanitizationResult(std::unique_ptr<EndpointResponse> response,
                            EndpointFetcherCallback endpoint_fetcher_callback,
                            data_decoder::JsonSanitizer::Result result);

  network::mojom::CredentialsMode GetCredentialsMode() const;
  int GetMaxRetries() const;
  bool GetSetSiteForCookies() const;
  UploadProgressCallback GetUploadProgressCallback() const;

  // Members set in constructor
  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  // `identity_manager_` can be null if it is not needed for authentication (in
  // this case, callers should invoke `PerformRequest` directly).
  const raw_ptr<signin::IdentityManager> identity_manager_;

  // The complete definition of the specific network request to be performed.
  // Contains authentication details and response handling preferences.
  const RequestParams request_params_;

  // Members set in Fetch
  std::unique_ptr<const signin::PrimaryAccountAccessTokenFetcher>
      access_token_fetcher_;
  std::unique_ptr<network::SimpleURLLoader> simple_url_loader_;

  base::WeakPtrFactory<EndpointFetcher> weak_ptr_factory_{this};
};

}  // namespace endpoint_fetcher

#endif  // COMPONENTS_ENDPOINT_FETCHER_ENDPOINT_FETCHER_H_
