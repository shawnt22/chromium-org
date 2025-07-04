// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_vary_data.h"

#include <array>
#include <string_view>

#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"

namespace net {

crypto::obsolete::Md5 MakeMd5HasherForHttpVaryData() {
  return {};
}

HttpVaryData::HttpVaryData() = default;

bool HttpVaryData::Init(const HttpRequestInfo& request_info,
                        const HttpResponseHeaders& response_headers) {
  auto ctx = MakeMd5HasherForHttpVaryData();

  is_valid_ = false;
  bool processed_header = false;

  // Feed the MD5 context in the order of the Vary header enumeration.  If the
  // Vary header repeats a header name, then that's OK.
  //
  // If the Vary header contains '*' then we can just notice it based on
  // |cached_response_headers| in MatchesRequest(), and don't have to worry
  // about the specific headers.  We still want an HttpVaryData around, to let
  // us handle this case. See section 4.1 of RFC 7234.
  //
  size_t iter = 0;
  constexpr std::string_view name = "vary";
  std::optional<std::string_view> request_header;
  while ((request_header = response_headers.EnumerateHeader(&iter, name))) {
    if (*request_header == "*") {
      // What's in request_digest_ will never be looked at, but make it
      // deterministic so we don't serialize out uninitialized memory content.
      request_digest_.fill(0u);
      return is_valid_ = true;
    }
    AddField(request_info, *request_header, ctx);
    processed_header = true;
  }

  if (!processed_header)
    return false;

  ctx.Finish(request_digest_);
  return is_valid_ = true;
}

bool HttpVaryData::InitFromPickle(base::PickleIterator* iter) {
  is_valid_ = false;
  std::optional<base::span<const uint8_t>> bytes =
      iter->ReadBytes(sizeof(request_digest_));
  if (bytes) {
    base::span(request_digest_).copy_from(*bytes);
    return is_valid_ = true;
  }
  return false;
}

void HttpVaryData::Persist(base::Pickle* pickle) const {
  DCHECK(is_valid());
  pickle->WriteBytes(request_digest_);
}

bool HttpVaryData::MatchesRequest(
    const HttpRequestInfo& request_info,
    const HttpResponseHeaders& cached_response_headers) const {
  // Vary: * never matches.
  if (cached_response_headers.HasHeaderValue("vary", "*"))
    return false;

  HttpVaryData new_vary_data;
  if (!new_vary_data.Init(request_info, cached_response_headers)) {
    // This case can happen if |this| was loaded from a cache that was populated
    // by a build before crbug.com/469675 was fixed.
    return false;
  }
  return new_vary_data.request_digest_ == request_digest_;
}

// static
void HttpVaryData::AddField(const HttpRequestInfo& request_info,
                            std::string_view request_header,
                            crypto::obsolete::Md5& context) {
  std::string request_value =
      request_info.extra_headers.GetHeader(request_header)
          .value_or(std::string());

  // Append a character that cannot appear in the request header line so that we
  // protect against case where the concatenation of two request headers could
  // look the same for a variety of values for the individual request headers.
  // For example, "foo: 12\nbar: 3" looks like "foo: 1\nbar: 23" otherwise.
  request_value.append(1, '\n');

  context.Update(request_value);
}

}  // namespace net
