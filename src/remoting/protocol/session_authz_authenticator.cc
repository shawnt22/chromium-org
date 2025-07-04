// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/session_authz_authenticator.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "remoting/base/http_status.h"
#include "remoting/base/protobuf_http_request_config.h"
#include "remoting/base/session_authz_service_client.h"
#include "remoting/proto/session_authz_service.h"
#include "remoting/protocol/authenticator.h"
#include "remoting/protocol/credentials_type.h"
#include "remoting/protocol/session_authz_reauthorizer.h"

namespace remoting::protocol {

namespace {
Authenticator::RejectionReason ToRejectionReason(
    HttpStatus::Code status_code,
    Authenticator::RejectionReason permission_denied_reason) {
  switch (status_code) {
    case HttpStatus::Code::PERMISSION_DENIED:
      return permission_denied_reason;
    case HttpStatus::Code::UNAUTHENTICATED:
      return Authenticator::RejectionReason::INVALID_CREDENTIALS;
    case HttpStatus::Code::RESOURCE_EXHAUSTED:
      return Authenticator::RejectionReason::TOO_MANY_CONNECTIONS;
    case HttpStatus::Code::NETWORK_ERROR:
      return Authenticator::RejectionReason::NETWORK_FAILURE;
    default:
      return Authenticator::RejectionReason::UNEXPECTED_ERROR;
  }
}
}  // namespace

SessionAuthzAuthenticator::SessionAuthzAuthenticator(
    CredentialsType credentials_type,
    std::unique_ptr<SessionAuthzServiceClient> service_client,
    const CreateBaseAuthenticatorCallback& create_base_authenticator_callback)
    : credentials_type_(credentials_type),
      service_client_(std::move(service_client)),
      create_base_authenticator_callback_(create_base_authenticator_callback) {
  DCHECK(credentials_type == CredentialsType::CLOUD_SESSION_AUTHZ ||
         credentials_type == CredentialsType::CORP_SESSION_AUTHZ);
}

SessionAuthzAuthenticator::~SessionAuthzAuthenticator() = default;

void SessionAuthzAuthenticator::Start(base::OnceClosure resume_callback) {
  GenerateHostToken(std::move(resume_callback));
}

CredentialsType SessionAuthzAuthenticator::credentials_type() const {
  return credentials_type_;
}

const Authenticator& SessionAuthzAuthenticator::implementing_authenticator()
    const {
  return *this;
}

Authenticator::State SessionAuthzAuthenticator::state() const {
  switch (session_authz_state_) {
    case SessionAuthzState::NOT_STARTED:
    case SessionAuthzState::WAITING_FOR_SESSION_TOKEN:
      return WAITING_MESSAGE;
    case SessionAuthzState::GENERATING_HOST_TOKEN:
    case SessionAuthzState::VERIFYING_SESSION_TOKEN:
      return PROCESSING_MESSAGE;
    case SessionAuthzState::READY_TO_SEND_HOST_TOKEN:
      return MESSAGE_READY;
    case SessionAuthzState::SHARED_SECRET_FETCHED:
      return underlying_->state();
    case SessionAuthzState::FAILED:
      return REJECTED;
  }
}

bool SessionAuthzAuthenticator::started() const {
  return session_authz_state_ != SessionAuthzState::NOT_STARTED;
}

Authenticator::RejectionReason SessionAuthzAuthenticator::rejection_reason()
    const {
  DCHECK_EQ(state(), REJECTED);

  if (session_authz_state_ == SessionAuthzState::FAILED) {
    return session_authz_rejection_reason_;
  }
  return underlying_->rejection_reason();
}

Authenticator::RejectionDetails SessionAuthzAuthenticator::rejection_details()
    const {
  DCHECK_EQ(state(), REJECTED);

  if (session_authz_state_ == SessionAuthzState::FAILED) {
    return rejection_details_;
  }
  return underlying_->rejection_details();
}

void SessionAuthzAuthenticator::ProcessMessage(
    const jingle_xmpp::XmlElement* message,
    base::OnceClosure resume_callback) {
  DCHECK_EQ(state(), WAITING_MESSAGE);

  switch (session_authz_state_) {
    case SessionAuthzState::WAITING_FOR_SESSION_TOKEN:
      VerifySessionToken(*message, std::move(resume_callback));
      break;
    case SessionAuthzState::SHARED_SECRET_FETCHED:
      DCHECK_EQ(underlying_->state(), WAITING_MESSAGE);
      underlying_->ProcessMessage(message, std::move(resume_callback));
      StartReauthorizerIfNecessary();
      break;
    default:
      NOTREACHED() << "Unexpected SessionAuthz state: "
                   << static_cast<int>(session_authz_state_);
  }
}

std::unique_ptr<jingle_xmpp::XmlElement>
SessionAuthzAuthenticator::GetNextMessage() {
  DCHECK_EQ(state(), MESSAGE_READY);

  std::unique_ptr<jingle_xmpp::XmlElement> message;
  if (underlying_ && underlying_->state() == MESSAGE_READY) {
    message = underlying_->GetNextMessage();
    StartReauthorizerIfNecessary();
  } else {
    message = CreateEmptyAuthenticatorMessage();
  }

  if (session_authz_state_ == SessionAuthzState::READY_TO_SEND_HOST_TOKEN) {
    AddHostTokenElement(message.get());
  }

  return message;
}

const std::string& SessionAuthzAuthenticator::GetAuthKey() const {
  DCHECK_EQ(state(), ACCEPTED);

  return underlying_->GetAuthKey();
}

const SessionPolicies* SessionAuthzAuthenticator::GetSessionPolicies() const {
  DCHECK_EQ(state(), ACCEPTED);

  return session_policies_.has_value() ? &session_policies_.value() : nullptr;
}

std::unique_ptr<ChannelAuthenticator>
SessionAuthzAuthenticator::CreateChannelAuthenticator() const {
  DCHECK_EQ(state(), ACCEPTED);

  return underlying_->CreateChannelAuthenticator();
}

void SessionAuthzAuthenticator::SetReauthorizerForTesting(
    std::unique_ptr<SessionAuthzReauthorizer> reauthorizer) {
  reauthorizer_ = std::move(reauthorizer);
}

void SessionAuthzAuthenticator::SetSessionIdForTesting(
    std::string_view session_id) {
  session_id_ = session_id;
}

void SessionAuthzAuthenticator::SetHostTokenForTesting(
    std::string_view host_token) {
  host_token_ = host_token;
}

void SessionAuthzAuthenticator::GenerateHostToken(
    base::OnceClosure resume_callback) {
  session_authz_state_ = SessionAuthzState::GENERATING_HOST_TOKEN;
  // Safe to use Unretained() for requests made to |service_client_|, since
  // this class owns |service_client_|, which cancels requests once it gets
  // deleted.
  service_client_->GenerateHostToken(
      base::BindOnce(&SessionAuthzAuthenticator::OnHostTokenGenerated,
                     base::Unretained(this), std::move(resume_callback)));
}

void SessionAuthzAuthenticator::OnHostTokenGenerated(
    base::OnceClosure resume_callback,
    const HttpStatus& status,
    std::unique_ptr<internal::GenerateHostTokenResponseStruct> response) {
  if (!status.ok()) {
    HandleSessionAuthzError("GenerateHostToken", status);
    std::move(resume_callback).Run();
    return;
  }
  session_id_ = response->session_id;
  host_token_ = response->host_token;
  session_authz_state_ = SessionAuthzState::READY_TO_SEND_HOST_TOKEN;
  std::move(resume_callback).Run();
}

void SessionAuthzAuthenticator::AddHostTokenElement(
    jingle_xmpp::XmlElement* message) {
  DCHECK_EQ(session_authz_state_, SessionAuthzState::READY_TO_SEND_HOST_TOKEN);
  DCHECK(!host_token_.empty());

  jingle_xmpp::XmlElement* host_token_element =
      new jingle_xmpp::XmlElement(kHostTokenTag);
  host_token_element->SetBodyText(host_token_);
  message->AddElement(host_token_element);
  session_authz_state_ = SessionAuthzState::WAITING_FOR_SESSION_TOKEN;
}

void SessionAuthzAuthenticator::VerifySessionToken(
    const jingle_xmpp::XmlElement& message,
    base::OnceClosure resume_callback) {
  session_authz_state_ = SessionAuthzState::VERIFYING_SESSION_TOKEN;
  std::string session_token = message.TextNamed(kSessionTokenTag);
  service_client_->VerifySessionToken(
      session_token,
      base::BindOnce(&SessionAuthzAuthenticator::OnVerifiedSessionToken,
                     base::Unretained(this), message,
                     std::move(resume_callback)));
}

void SessionAuthzAuthenticator::OnVerifiedSessionToken(
    const jingle_xmpp::XmlElement& message,
    base::OnceClosure resume_callback,
    const HttpStatus& status,
    std::unique_ptr<internal::VerifySessionTokenResponseStruct> response) {
  if (!status.ok()) {
    HandleSessionAuthzError("VerifySessionToken", status);
    std::move(resume_callback).Run();
    return;
  }
  if (response->session_id != session_id_) {
    session_authz_state_ = SessionAuthzState::FAILED;
    session_authz_rejection_reason_ = RejectionReason::INVALID_ACCOUNT_ID;
    rejection_details_ = RejectionDetails(base::StringPrintf(
        "Session token verification failed. Expected session ID: %s, actual: "
        "%s",
        session_id_, response->session_id));
    std::move(resume_callback).Run();
    return;
  }
  session_authz_state_ = SessionAuthzState::SHARED_SECRET_FETCHED;

  // The other side already started the SPAKE authentication.
  underlying_ = create_base_authenticator_callback_.Run(response->shared_secret,
                                                        WAITING_MESSAGE);
  session_policies_ = std::move(response->session_policies);
  verify_token_response_ = std::move(response);
  underlying_->ProcessMessage(&message, std::move(resume_callback));
  StartReauthorizerIfNecessary();
}

void SessionAuthzAuthenticator::HandleSessionAuthzError(
    const std::string_view& action_name,
    const HttpStatus& status) {
  DCHECK(!status.ok());
  rejection_details_ = RejectionDetails(base::StringPrintf(
      "SessionAuthz %s error, code: %d, message: %s", action_name,
      static_cast<int>(status.error_code()), status.error_message()));
  session_authz_state_ = SessionAuthzState::FAILED;
  session_authz_rejection_reason_ = ToRejectionReason(
      status.error_code(), RejectionReason::AUTHZ_POLICY_CHECK_FAILED);
}

void SessionAuthzAuthenticator::StartReauthorizerIfNecessary() {
  if (reauthorizer_) {
    return;
  }
  if (!underlying_ || underlying_->state() != ACCEPTED) {
    return;
  }
  DCHECK(verify_token_response_);
  reauthorizer_ = std::make_unique<SessionAuthzReauthorizer>(
      service_client_.get(), verify_token_response_->session_id,
      verify_token_response_->session_reauth_token,
      verify_token_response_->session_reauth_token_lifetime,
      base::BindOnce(&SessionAuthzAuthenticator::OnReauthorizationFailed,
                     base::Unretained(this)));
  reauthorizer_->Start();
  verify_token_response_.reset();
}

void SessionAuthzAuthenticator::OnReauthorizationFailed(
    HttpStatus::Code error_code,
    const Authenticator::RejectionDetails& details) {
  session_authz_state_ = SessionAuthzState::FAILED;
  session_authz_rejection_reason_ = ToRejectionReason(
      error_code, RejectionReason::REAUTHZ_POLICY_CHECK_FAILED);
  rejection_details_ = details;

  reauthorizer_.reset();
  NotifyStateChangeAfterAccepted();
}

}  // namespace remoting::protocol
