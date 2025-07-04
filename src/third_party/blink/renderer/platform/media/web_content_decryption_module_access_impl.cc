// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/media/web_content_decryption_module_access_impl.h"

#include <memory>
#include <utility>

#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "third_party/blink/public/platform/media/web_encrypted_media_client_impl.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

// The caller owns the created cdm (passed back using |result|).
static void CreateCdm(
    const base::WeakPtr<WebEncryptedMediaClientImpl>& client,
    const WebSecurityOrigin& security_origin,
    const media::CdmConfig& cdm_config,
    std::unique_ptr<WebContentDecryptionModuleResult> result) {
  // If |client| is gone (due to the frame getting destroyed), it is
  // impossible to create the CDM, so fail.
  if (!client) {
    result->CompleteWithError(
        kWebContentDecryptionModuleExceptionInvalidStateError, 0,
        "Failed to create CDM.");
    return;
  }

  client->CreateCdm(security_origin, cdm_config, std::move(result));
}

// static
WebContentDecryptionModuleAccessImpl*
WebContentDecryptionModuleAccessImpl::From(
    WebContentDecryptionModuleAccess* cdm_access) {
  return static_cast<WebContentDecryptionModuleAccessImpl*>(cdm_access);
}

std::unique_ptr<WebContentDecryptionModuleAccessImpl>
WebContentDecryptionModuleAccessImpl::Create(
    const WebSecurityOrigin& security_origin,
    const WebMediaKeySystemConfiguration& configuration,
    const WebString& requested_key_system,
    const media::CdmConfig& cdm_config,
    const base::WeakPtr<WebEncryptedMediaClientImpl>& client) {
  return std::make_unique<WebContentDecryptionModuleAccessImpl>(
      security_origin, configuration, requested_key_system, cdm_config, client);
}

WebContentDecryptionModuleAccessImpl::WebContentDecryptionModuleAccessImpl(
    const WebSecurityOrigin& security_origin,
    const WebMediaKeySystemConfiguration& configuration,
    const WebString& requested_key_system,
    const media::CdmConfig& cdm_config,
    const base::WeakPtr<WebEncryptedMediaClientImpl>& client)
    : security_origin_(security_origin),
      configuration_(configuration),
      requested_key_system_(requested_key_system),
      cdm_config_(cdm_config),
      client_(client) {}

WebContentDecryptionModuleAccessImpl::~WebContentDecryptionModuleAccessImpl() =
    default;

WebString WebContentDecryptionModuleAccessImpl::GetKeySystem() {
  // crbug.com/421223928: Returns the originally requested key system
  return requested_key_system_;
}

WebMediaKeySystemConfiguration
WebContentDecryptionModuleAccessImpl::GetConfiguration() {
  return configuration_;
}

void WebContentDecryptionModuleAccessImpl::CreateContentDecryptionModule(
    WebContentDecryptionModuleResult result,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  // This method needs to run asynchronously, as it may need to load the CDM.
  // As this object's lifetime is controlled by MediaKeySystemAccess on the
  // blink side, copy all values needed by CreateCdm() in case the blink object
  // gets garbage-collected.
  auto result_copy = std::make_unique<WebContentDecryptionModuleResult>(result);
  task_runner->PostTask(FROM_HERE,
                        WTF::BindOnce(&CreateCdm, client_, security_origin_,
                                      cdm_config_, std::move(result_copy)));
}

bool WebContentDecryptionModuleAccessImpl::UseHardwareSecureCodecs() const {
  return cdm_config_.use_hw_secure_codecs;
}

}  // namespace blink
