// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/tls_stream_attempt.h"

#include <memory>
#include <optional>
#include <string_view>

#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "net/base/completion_once_callback.h"
#include "net/base/features.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/tracing.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/tcp_stream_attempt.h"
#include "net/ssl/ssl_cert_request_info.h"

namespace net {

// static
std::string_view TlsStreamAttempt::StateToString(State state) {
  switch (state) {
    case State::kNone:
      return "None";
    case State::kTcpAttempt:
      return "TcpAttempt";
    case State::kTcpAttemptComplete:
      return "TcpAttemptComplete";
    case State::kTlsAttempt:
      return "TlsAttempt";
    case State::kTlsAttemptComplete:
      return "TlsAttemptComplete";
  }
}

TlsStreamAttempt::TlsStreamAttempt(const StreamAttemptParams* params,
                                   IPEndPoint ip_endpoint,
                                   perfetto::Track track,
                                   HostPortPair host_port_pair,
                                   Delegate* delegate)
    : StreamAttempt(params,
                    ip_endpoint,
                    track,
                    NetLogSourceType::TLS_STREAM_ATTEMPT,
                    NetLogEventType::TLS_STREAM_ATTEMPT_ALIVE),
      host_port_pair_(std::move(host_port_pair)),
      delegate_(delegate) {}

TlsStreamAttempt::~TlsStreamAttempt() {
  MaybeRecordTlsHandshakeEnd(ERR_ABORTED);
}

LoadState TlsStreamAttempt::GetLoadState() const {
  switch (next_state_) {
    case State::kNone:
      return LOAD_STATE_IDLE;
    case State::kTcpAttempt:
    case State::kTcpAttemptComplete:
      CHECK(nested_attempt_);
      return nested_attempt_->GetLoadState();
    case State::kTlsAttempt:
    case State::kTlsAttemptComplete:
      return LOAD_STATE_SSL_HANDSHAKE;
  }
}

base::Value::Dict TlsStreamAttempt::GetInfoAsValue() const {
  base::Value::Dict dict;
  dict.Set("next_state", StateToString(next_state_));
  dict.Set("tcp_handshake_completed", tcp_handshake_completed_);
  dict.Set("tls_handshake_started", tls_handshake_started_);
  dict.Set("has_ssl_config", ssl_config_.has_value());
  if (nested_attempt_) {
    dict.Set("nested_attempt", nested_attempt_->GetInfoAsValue());
  }
  return dict;
}

scoped_refptr<SSLCertRequestInfo> TlsStreamAttempt::GetCertRequestInfo() {
  return ssl_cert_request_info_;
}

int TlsStreamAttempt::StartInternal() {
  CHECK_EQ(next_state_, State::kNone);
  next_state_ = State::kTcpAttempt;
  return DoLoop(OK);
}

base::Value::Dict TlsStreamAttempt::GetNetLogStartParams() {
  base::Value::Dict dict;
  dict.Set("host_port", host_port_pair_.ToString());
  return dict;
}

void TlsStreamAttempt::OnIOComplete(int rv) {
  CHECK_NE(rv, ERR_IO_PENDING);
  rv = DoLoop(rv);
  if (rv != ERR_IO_PENDING) {
    NotifyOfCompletion(rv);
  }
}

int TlsStreamAttempt::DoLoop(int rv) {
  CHECK_NE(next_state_, State::kNone);

  do {
    State state = next_state_;
    next_state_ = State::kNone;
    switch (state) {
      case State::kNone:
        NOTREACHED() << "Invalid state";
      case State::kTcpAttempt:
        rv = DoTcpAttempt();
        break;
      case State::kTcpAttemptComplete:
        rv = DoTcpAttemptComplete(rv);
        break;
      case State::kTlsAttempt:
        rv = DoTlsAttempt(rv);
        break;
      case State::kTlsAttemptComplete:
        rv = DoTlsAttemptComplete(rv);
        break;
    }
  } while (next_state_ != State::kNone && rv != ERR_IO_PENDING);

  return rv;
}

int TlsStreamAttempt::DoTcpAttempt() {
  next_state_ = State::kTcpAttemptComplete;
  nested_attempt_ = std::make_unique<TcpStreamAttempt>(&params(), ip_endpoint(),
                                                       track(), &net_log());
  return nested_attempt_->Start(
      base::BindOnce(&TlsStreamAttempt::OnIOComplete, base::Unretained(this)));
}

int TlsStreamAttempt::DoTcpAttemptComplete(int rv) {
  const LoadTimingInfo::ConnectTiming& nested_timing =
      nested_attempt_->connect_timing();
  mutable_connect_timing().connect_start = nested_timing.connect_start;

  tcp_handshake_completed_ = true;
  delegate_->OnTcpHandshakeComplete();

  if (rv != OK) {
    return rv;
  }

  net_log().BeginEvent(NetLogEventType::TLS_STREAM_ATTEMPT_WAIT_FOR_SSL_CONFIG);

  next_state_ = State::kTlsAttempt;

  if (ssl_config_.has_value()) {
    // We restarted for ECH retry and already have a SSLConfig with retry
    // configs.
    return OK;
  }

  int ssl_config_ready_result = delegate_->WaitForSSLConfigReady(base::BindOnce(
      &TlsStreamAttempt::OnIOComplete, weak_ptr_factory_.GetWeakPtr()));
  if (ssl_config_ready_result == ERR_IO_PENDING) {
    TRACE_EVENT_INSTANT("net.stream", "WaitForSSLConfig", track());
  }
  return ssl_config_ready_result;
}

int TlsStreamAttempt::DoTlsAttempt(int rv) {
  CHECK_EQ(rv, OK);

  net_log().EndEvent(NetLogEventType::TLS_STREAM_ATTEMPT_WAIT_FOR_SSL_CONFIG);

  next_state_ = State::kTlsAttemptComplete;

  std::unique_ptr<StreamSocket> nested_socket =
      nested_attempt_->ReleaseStreamSocket();
  if (!ssl_config_) {
    auto get_config_result = delegate_->GetSSLConfig();

    if (get_config_result.has_value()) {
      ssl_config_ = *get_config_result;
      // For metrics, we want to know whether the server advertised Trust Anchor
      // IDs in DNS (i.e., whether the client could use Trust Anchor IDs with
      // this server, regardless of whether the feature was enabled). But we
      // don't want to actually configure the Trust Anchor IDs on the connection
      // if the feature flag isn't enabled.
      trust_anchor_ids_from_dns_ = !ssl_config_->trust_anchor_ids.empty();
      if (!base::FeatureList::IsEnabled(features::kTLSTrustAnchorIDs)) {
        ssl_config_->trust_anchor_ids.clear();
      }
    } else {
      CHECK_EQ(get_config_result.error(), GetSSLConfigError::kAbort);
      return ERR_ABORTED;
    }
  }

  nested_attempt_.reset();

  tls_handshake_started_ = true;
  mutable_connect_timing().ssl_start = base::TimeTicks::Now();
  tls_handshake_timeout_timer_.Start(
      FROM_HERE, kTlsHandshakeTimeout,
      base::BindOnce(&TlsStreamAttempt::OnTlsHandshakeTimeout,
                     base::Unretained(this)));

  ssl_socket_ = params().client_socket_factory->CreateSSLClientSocket(
      params().ssl_client_context, std::move(nested_socket), host_port_pair_,
      *ssl_config_);

  TRACE_EVENT_BEGIN("net.stream", "TlsConnect", track());
  net_log().BeginEvent(NetLogEventType::TLS_STREAM_ATTEMPT_CONNECT);

  return ssl_socket_->Connect(
      base::BindOnce(&TlsStreamAttempt::OnIOComplete, base::Unretained(this)));
}

int TlsStreamAttempt::DoTlsAttemptComplete(int rv) {
  MaybeRecordTlsHandshakeEnd(rv);
  net_log().EndEventWithNetErrorCode(
      NetLogEventType::TLS_STREAM_ATTEMPT_CONNECT, rv);

  mutable_connect_timing().ssl_end = base::TimeTicks::Now();
  tls_handshake_timeout_timer_.Stop();

  const bool ech_enabled = params().ssl_client_context->config().ech_enabled;

  if (!ech_retry_configs_ && rv == ERR_ECH_NOT_NEGOTIATED && ech_enabled) {
    CHECK(ssl_socket_);
    // We used ECH, and the server could not decrypt the ClientHello. However,
    // it was able to handshake with the public name and send authenticated
    // retry configs. If this is not the first time around, retry the connection
    // with the new ECHConfigList, or with ECH disabled (empty retry configs),
    // as directed.
    //
    // See
    // https://www.ietf.org/archive/id/draft-ietf-tls-esni-22.html#section-6.1.6
    ech_retry_configs_ = ssl_socket_->GetECHRetryConfigs();
    ssl_config_->ech_config_list = *ech_retry_configs_;

    // TODO(crbug.com/346835898): Add a NetLog to record ECH retry configs.

    ResetStateForRestart();
    next_state_ = State::kTcpAttempt;
    return OK;
  }

  // If we got a certificate error and the server advertised some Trust Anchor
  // IDs in the handshake that we trust, then retry the connection, using the
  // fresh Trust Anchor IDs from the server. We only want to retry once; if we
  // have we already retried, so we skip all of this and treat the connection
  // error as usual.
  //
  // TODO(https://crbug.com/399937371): clarify and test the interactions of ECH
  // retry and TAI retry.
  if (IsCertificateError(rv) && !retried_for_trust_anchor_ids_ &&
      base::FeatureList::IsEnabled(features::kTLSTrustAnchorIDs)) {
    CHECK(ssl_socket_);

    std::vector<std::vector<uint8_t>> server_trust_anchor_ids =
        ssl_socket_->GetServerTrustAnchorIDsForRetry();
    // https://tlswg.org/tls-trust-anchor-ids/draft-ietf-tls-trust-anchor-ids.html#name-retry-mechanism:
    // If the EncryptedExtensions had no trust_anchor extension, or no match was
    // found, the client returns the error to the application.
    if (!server_trust_anchor_ids.empty()) {
      std::vector<uint8_t> trust_anchor_ids_for_retry =
          SSLConfig::SelectTrustAnchorIDs(
              server_trust_anchor_ids,
              params().ssl_client_context->config().trust_anchor_ids);
      if (!trust_anchor_ids_for_retry.empty()) {
        retried_for_trust_anchor_ids_ = true;
        ssl_config_->trust_anchor_ids = trust_anchor_ids_for_retry;

        ResetStateForRestart();
        next_state_ = State::kTcpAttempt;
        return OK;
      }
    }
  }

  const bool is_ech_capable =
      ssl_config_ && !ssl_config_->ech_config_list.empty();
  SSLClientSocket::RecordSSLConnectResult(
      ssl_socket_.get(), rv, is_ech_capable, ech_enabled, ech_retry_configs_,
      trust_anchor_ids_from_dns_, retried_for_trust_anchor_ids_,
      connect_timing());

  if (rv == OK || IsCertificateError(rv)) {
    CHECK(ssl_socket_);
    SetStreamSocket(std::move(ssl_socket_));
  } else if (rv == ERR_SSL_CLIENT_AUTH_CERT_NEEDED) {
    CHECK(ssl_socket_);
    ssl_cert_request_info_ = base::MakeRefCounted<SSLCertRequestInfo>();
    ssl_socket_->GetSSLCertRequestInfo(ssl_cert_request_info_.get());
  }

  return rv;
}

void TlsStreamAttempt::OnTlsHandshakeTimeout() {
  // TODO(bashi): The error code should be ERR_CONNECTION_TIMED_OUT but use
  // ERR_TIMED_OUT for consistency with ConnectJobs.
  OnIOComplete(ERR_TIMED_OUT);
}

void TlsStreamAttempt::MaybeRecordTlsHandshakeEnd(int rv) {
  if (!tls_handshake_started_ || !tls_handshake_timeout_timer_.IsRunning()) {
    return;
  }
  TRACE_EVENT_END("net.stream", track(), "result", rv);
}

void TlsStreamAttempt::ResetStateForRestart() {
  tcp_handshake_completed_ = false;
  tls_handshake_started_ = false;
  ssl_socket_.reset();
  ssl_cert_request_info_.reset();
}

}  // namespace net
