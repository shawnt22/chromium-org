// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/navigation_throttle.h"

#include <utility>

#include "base/check_deref.h"
#include "content/browser/renderer_host/navigation_request.h"

namespace content {

namespace {

net::Error DefaultNetErrorCode(NavigationThrottle::ThrottleAction action) {
  switch (action) {
    case NavigationThrottle::PROCEED:
    case NavigationThrottle::DEFER:
      return net::OK;
    case NavigationThrottle::CANCEL:
    case NavigationThrottle::CANCEL_AND_IGNORE:
      return net::ERR_ABORTED;
    case NavigationThrottle::BLOCK_REQUEST:
    case NavigationThrottle::BLOCK_REQUEST_AND_COLLAPSE:
      return net::ERR_BLOCKED_BY_CLIENT;
    case NavigationThrottle::BLOCK_RESPONSE:
      return net::ERR_BLOCKED_BY_RESPONSE;
    default:
      NOTREACHED();
  }
}

}  // namespace

NavigationThrottle::ThrottleCheckResult::ThrottleCheckResult(
    NavigationThrottle::ThrottleAction action)
    : NavigationThrottle::ThrottleCheckResult(action,
                                              DefaultNetErrorCode(action),
                                              std::nullopt) {}

NavigationThrottle::ThrottleCheckResult::ThrottleCheckResult(
    NavigationThrottle::ThrottleAction action,
    net::Error net_error_code)
    : NavigationThrottle::ThrottleCheckResult(action,
                                              net_error_code,
                                              std::nullopt) {}

NavigationThrottle::ThrottleCheckResult::ThrottleCheckResult(
    NavigationThrottle::ThrottleAction action,
    net::Error net_error_code,
    std::optional<std::string> error_page_content)
    : action_(action),
      net_error_code_(net_error_code),
      error_page_content_(std::move(error_page_content)) {}

NavigationThrottle::ThrottleCheckResult::ThrottleCheckResult(
    const ThrottleCheckResult& other) = default;

NavigationThrottle::ThrottleCheckResult::~ThrottleCheckResult() {}

NavigationThrottle::NavigationThrottle(NavigationThrottleRegistry& registry)
    : registry_(registry) {}

NavigationThrottle::~NavigationThrottle() {}

NavigationThrottle::ThrottleCheckResult NavigationThrottle::WillStartRequest() {
  return NavigationThrottle::PROCEED;
}

NavigationThrottle::ThrottleCheckResult
NavigationThrottle::WillRedirectRequest() {
  return NavigationThrottle::PROCEED;
}

NavigationThrottle::ThrottleCheckResult NavigationThrottle::WillFailRequest() {
  return NavigationThrottle::PROCEED;
}

NavigationThrottle::ThrottleCheckResult
NavigationThrottle::WillProcessResponse() {
  return NavigationThrottle::PROCEED;
}

NavigationThrottle::ThrottleCheckResult
NavigationThrottle::WillCommitWithoutUrlLoader() {
  return NavigationThrottle::PROCEED;
}

void NavigationThrottle::Resume() {
  if (resume_callback_) {
    resume_callback_.Run();
    return;
  }
  NavigationRequest::From(&registry_->GetNavigationHandle())->Resume(this);
}

void NavigationThrottle::CancelDeferredNavigation(
    NavigationThrottle::ThrottleCheckResult result) {
  if (cancel_deferred_navigation_callback_) {
    cancel_deferred_navigation_callback_.Run(result);
    return;
  }
  NavigationRequest::From(&registry_->GetNavigationHandle())
      ->CancelDeferredNavigation(this, result);
}

}  // namespace content
