// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/certificate_manager/enterprise_cert_sources.h"

#include <vector>

#include "base/feature_list.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_view_util.h"
#include "chrome/browser/net/profile_network_context_service.h"
#include "chrome/browser/net/profile_network_context_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/certificate_dialogs.h"
#include "chrome/browser/ui/webui/certificate_manager/certificate_manager_handler.h"
#include "chrome/browser/ui/webui/certificate_manager/certificate_manager_utils.h"
#include "chrome/browser/ui/webui/certificate_viewer/certificate_viewer_webui.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/net/x509_certificate_model.h"
#include "components/server_certificate_database/server_certificate_database.pb.h"
#include "content/public/browser/web_contents.h"
#include "crypto/sha2.h"
#include "net/cert/x509_util.h"

EnterpriseCertSource::EnterpriseCertSource(std::string export_file_name)
    : export_file_name_(std::move(export_file_name)) {}

void EnterpriseCertSource::GetCertificateInfos(
    CertificateManagerPageHandler::GetCertificatesCallback callback) {
  std::vector<certificate_manager::mojom::SummaryCertInfoPtr> cert_infos;
  for (const auto& cert : GetCerts()) {
    x509_certificate_model::X509CertificateModel model(
        net::x509_util::CreateCryptoBuffer(cert));
    cert_infos.push_back(certificate_manager::mojom::SummaryCertInfo::New(
        model.HashCertSHA256(), model.GetTitle(),
        /*is_deletable=*/false));
  }
  std::move(callback).Run(std::move(cert_infos));
}

void EnterpriseCertSource::ViewCertificate(
    const std::string& sha256_hex_hash,
    base::WeakPtr<content::WebContents> web_contents) {
  if (!web_contents) {
    return;
  }

  std::array<uint8_t, crypto::kSHA256Length> hash;
  if (!base::HexStringToSpan(sha256_hex_hash, hash)) {
    return;
  }

  for (const auto& cert : GetCerts()) {
    if (hash == crypto::SHA256Hash(cert)) {
      // Found the cert, open cert viewer dialog if able and then exit
      // function.
      ShowCertificateDialog(std::move(web_contents),
                            net::x509_util::CreateCryptoBuffer(cert));
      return;
    }
  }
}

void EnterpriseCertSource::ExportCertificates(
    base::WeakPtr<content::WebContents> web_contents) {
  if (!web_contents) {
    return;
  }

  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> export_certs;
  for (auto const& cert : GetCerts()) {
    export_certs.push_back(net::x509_util::CreateCryptoBuffer(cert));
  }

  ShowCertExportDialogSaveAll(web_contents.get(),
                              web_contents->GetTopLevelNativeWindow(),
                              std::move(export_certs), export_file_name_);
  return;
}

EnterpriseTrustedCertSource::EnterpriseTrustedCertSource(Profile* profile)
    : EnterpriseCertSource("trusted_certs.pem"), profile_(profile) {}

// Can't use the parent class's implementation because certs with additional
// constraints need to be handled differently.
void EnterpriseTrustedCertSource::ViewCertificate(
    const std::string& sha256_hex_hash,
    base::WeakPtr<content::WebContents> web_contents) {
  if (!web_contents) {
    return;
  }
  std::array<uint8_t, crypto::kSHA256Length> hash;
  if (!base::HexStringToSpan(sha256_hex_hash, hash)) {
    return;
  }
  ProfileNetworkContextService* service =
      ProfileNetworkContextServiceFactory::GetForContext(profile_);
  ProfileNetworkContextService::CertificatePoliciesForView policies =
      service->GetCertificatePolicyForView();
  std::vector<std::vector<uint8_t>> certs;
  for (const auto& cert : policies.certificate_policies->trust_anchors) {
    certs.push_back(cert);
  }
  for (const auto& cert :
       policies.certificate_policies->trust_anchors_with_enforced_constraints) {
    certs.push_back(cert);
  }

  for (auto const& cert : certs) {
    if (hash == crypto::SHA256Hash(cert)) {
      // Found the cert, open cert viewer dialog if able and then exit
      // function.
      ShowCertificateDialog(std::move(web_contents),
                            net::x509_util::CreateCryptoBuffer(cert));
      return;
    }
  }

  // Certs with additional constraints outside of the cert are handled
  // differently so that the outside constraints can be shown.
  for (const auto& cert_with_constraints :
       policies.certificate_policies
           ->trust_anchors_with_additional_constraints) {
    if (hash == crypto::SHA256Hash(cert_with_constraints->certificate)) {
      // Found the cert, open cert viewer dialog if able and then exit
      // function.
      if (base::FeatureList::IsEnabled(
              ::features::kEnableCertManagementUIV2EditCerts)) {
        chrome_browser_server_certificate_database::CertificateMetadata
            metadata;
        metadata.mutable_constraints()->mutable_dns_names()->Add(
            cert_with_constraints->permitted_dns_names.begin(),
            cert_with_constraints->permitted_dns_names.end());
        for (auto const& cidr : cert_with_constraints->permitted_cidrs) {
          net::IPAddress ip(cidr->ip);
          net::IPAddress mask(cidr->mask);
          if (!ip.IsValid() || !mask.IsValid()) {
            continue;
          }
          chrome_browser_server_certificate_database::CIDR proto_cidr;
          proto_cidr.set_ip(std::string(base::as_string_view(ip.bytes())));
          proto_cidr.set_prefix_length(net::MaskPrefixLength(mask));
          metadata.mutable_constraints()->mutable_cidrs()->Add(
              std::move(proto_cidr));
        }
        metadata.mutable_trust()->set_trust_type(
            chrome_browser_server_certificate_database::CertificateTrust::
                CERTIFICATE_TRUST_TYPE_TRUSTED);
        ShowCertificateDialog(std::move(web_contents),
                              net::x509_util::CreateCryptoBuffer(
                                  cert_with_constraints->certificate),
                              std::move(metadata), base::NullCallback());
      } else {
        ShowCertificateDialog(std::move(web_contents),
                              net::x509_util::CreateCryptoBuffer(
                                  cert_with_constraints->certificate));
      }
      return;
    }
  }
}

std::vector<std::vector<uint8_t>> EnterpriseTrustedCertSource::GetCerts() {
  ProfileNetworkContextService* service =
      ProfileNetworkContextServiceFactory::GetForContext(profile_);
  ProfileNetworkContextService::CertificatePoliciesForView policies =
      service->GetCertificatePolicyForView();
  std::vector<std::vector<uint8_t>> certs;
  for (const auto& cert : policies.certificate_policies->trust_anchors) {
    certs.push_back(cert);
  }
  for (const auto& cert :
       policies.certificate_policies->trust_anchors_with_enforced_constraints) {
    certs.push_back(cert);
  }
  for (const auto& cert_with_constraints :
       policies.certificate_policies
           ->trust_anchors_with_additional_constraints) {
    certs.push_back(cert_with_constraints->certificate);
  }

  return certs;
}

EnterpriseIntermediateCertSource::EnterpriseIntermediateCertSource(
    Profile* profile)
    : EnterpriseCertSource("intermediate_certs.pem"), profile_(profile) {}

std::vector<std::vector<uint8_t>> EnterpriseIntermediateCertSource::GetCerts() {
  ProfileNetworkContextService* service =
      ProfileNetworkContextServiceFactory::GetForContext(profile_);
  ProfileNetworkContextService::CertificatePoliciesForView policies =
      service->GetCertificatePolicyForView();
  std::vector<std::vector<uint8_t>> certs;
  for (const auto& cert : policies.certificate_policies->all_certificates) {
    certs.push_back(cert);
  }

  return certs;
}

EnterpriseDistrustedCertSource::EnterpriseDistrustedCertSource(Profile* profile)
    : EnterpriseCertSource("distrusted_certs.pem"), profile_(profile) {}

std::vector<std::vector<uint8_t>> EnterpriseDistrustedCertSource::GetCerts() {
  ProfileNetworkContextService* service =
      ProfileNetworkContextServiceFactory::GetForContext(profile_);
  ProfileNetworkContextService::CertificatePoliciesForView policies =
      service->GetCertificatePolicyForView();
  std::vector<std::vector<uint8_t>> certs;
  for (const auto& cert : policies.full_distrusted_certs) {
    certs.push_back(cert);
  }

  return certs;
}
