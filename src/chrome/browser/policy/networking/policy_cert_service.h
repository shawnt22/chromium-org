// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_NETWORKING_POLICY_CERT_SERVICE_H_
#define CHROME_BROWSER_POLICY_NETWORKING_POLICY_CERT_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "chromeos/ash/components/network/policy_certificate_provider.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace base {
class FilePath;
}

namespace net {
class X509Certificate;
typedef std::vector<scoped_refptr<X509Certificate>> CertificateList;
}  // namespace net

namespace network {
class NSSTempCertsCacheChromeOS;
}

namespace policy {

// This service is responsible for pushing the current list of policy-provided
// certificates to ProfileNetworkContextService.
// This service / its factory keep track of which Profile has used a
// policy-provided trust anchor.
class PolicyCertService : public KeyedService,
                          public ash::PolicyCertificateProvider::Observer {
 public:
  // Constructs a PolicyCertService for |profile| using
  // |policy_certificate_provider| as the source of certificates.
  // If |may_use_profile_wide_trust_anchors| is true, certificates from
  // |policy_certificate_provider| that have requested "Web" trust and have
  // profile-wide scope will be used for |profile|.
  PolicyCertService(Profile* profile,
                    ash::PolicyCertificateProvider* policy_certificate_provider,
                    bool may_use_profile_wide_trust_anchors);

  PolicyCertService(const PolicyCertService&) = delete;
  PolicyCertService& operator=(const PolicyCertService&) = delete;

  ~PolicyCertService() override;

  // Starts observing for changes to the policy-provided certificates and sets
  // a callback to be called when this happens. This should only be called if
  // the network service is enabled.
  void StartObservingCertChanges(base::RepeatingClosure callback);

  // Clears the callback set by `StartObservingCertChanges()` and stops
  // observing for changes to the policy-provided certificates.
  void StopObservingCertChanges();

  // Returns true if the service is currently observing changes to the
  // policy-provided certificates.
  bool IsObservingCertChanges() const {
    return !!on_policy_provided_certs_changed_callback_;
  }

  // Returns true if the profile that owns this service has at least one
  // policy-provided trust anchor configured.
  bool has_policy_certificates() const {
    return !profile_wide_trust_anchors_.empty();
  }

  // PolicyCertificateProvider::Observer:
  void OnPolicyProvidedCertsChanged() override;

  // PolicyCertificateProvider::OnDestroying:
  void OnPolicyCertificateProviderDestroying() override;

  // Fills *|out_all_server_and_authority_certificates| and *|out_trust_anchors|
  // with policy-provided certificates that should be used when verifying a
  // server certificate for Web requests from the StoragePartition identified by
  // |partition_path|.
  void GetPolicyCertificatesForStoragePartition(
      const base::FilePath& partition_path,
      net::CertificateList* out_all_server_and_authority_certificates,
      net::CertificateList* out_trust_anchors) const;

  static std::unique_ptr<PolicyCertService> CreateForTesting(Profile* profile);

  // Sets the profile-wide policy-provided trust anchors reported by this
  // PolicyCertService. This is only callable for instances created through
  // CreateForTesting.
  void SetPolicyTrustAnchorsForTesting(
      const net::CertificateList& trust_anchors);

 private:
  // Constructor used by CreateForTesting.
  explicit PolicyCertService(Profile* profile);

  // Returns all allowed policy-provided certificates that have requested "Web"
  // trust and have profile-wide scope. If |may_use_profile_wide_trust_anchors_|
  // is false, always returns an empty list.
  net::CertificateList GetAllowedProfileWideTrustAnchors();

  const raw_ptr<Profile> profile_;

  // Callback to be called when the policy-provided certificates change. Set via
  // `StartObservingForProfile()`.
  base::RepeatingClosure on_policy_provided_certs_changed_callback_;

  // The source of certificates for this PolicyCertService.
  raw_ptr<ash::PolicyCertificateProvider> policy_certificate_provider_;

  // If true, CA certificates |policy_certificate_provider_| that have requested
  // "Web" trust and have profile-wide scope may be used for |profile_|.
  const bool may_use_profile_wide_trust_anchors_;

  // Caches all server and CA certificates that have profile-wide scope from
  // |policy_certificate_provider_|.
  net::CertificateList profile_wide_all_server_and_authority_certs_;
  // Caches CA certificates that have requested "Web" trust and have
  // profile-wide scope from |policy_certificate_provider_|.
  net::CertificateList profile_wide_trust_anchors_;

  // Holds all policy-provided server and authority certificates and makes them
  // available to NSS as temp certificates. This is needed so they can be used
  // as intermediates when NSS verifies a certificate.
  std::unique_ptr<network::NSSTempCertsCacheChromeOS>
      temp_policy_provided_certs_;
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_NETWORKING_POLICY_CERT_SERVICE_H_
