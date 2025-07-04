// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/profile_network_context_service_factory.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/first_party_sets/first_party_sets_policy_service_factory.h"
#include "chrome/browser/ip_protection/ip_protection_core_host_factory.h"
#include "chrome/browser/net/profile_network_context_service.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_settings_factory.h"
#include "chrome/browser/privacy_sandbox/tracking_protection_settings_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/sct_reporting_service_factory.h"
#include "chrome/browser/webid/federated_identity_permission_context_factory.h"
#include "chrome/common/buildflags.h"
#include "crypto/crypto_buildflags.h"

#if BUILDFLAG(USE_NSS_CERTS)
#include "chrome/browser/net/nss_service_factory.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/policy/networking/policy_cert_service_factory.h"
#include "chrome/browser/certificate_provider/certificate_provider_service_factory.h"
#endif

#if BUILDFLAG(CHROME_ROOT_STORE_CERT_MANAGEMENT_UI)
#include "chrome/browser/net/server_certificate_database_service_factory.h"  // nogncheck
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "chrome/browser/enterprise/client_certificates/certificate_provisioning_service_factory.h"
#endif

ProfileNetworkContextService*
ProfileNetworkContextServiceFactory::GetForContext(
    content::BrowserContext* browser_context) {
  return static_cast<ProfileNetworkContextService*>(
      GetInstance()->GetServiceForBrowserContext(browser_context, true));
}

ProfileNetworkContextServiceFactory*
ProfileNetworkContextServiceFactory::GetInstance() {
  static base::NoDestructor<ProfileNetworkContextServiceFactory> instance;
  return instance.get();
}

ProfileNetworkContextServiceFactory::ProfileNetworkContextServiceFactory()
    : ProfileKeyedServiceFactory(
          "ProfileNetworkContextService",
          // Create separate service for incognito profiles.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              // TODO(crbug.com/40257657): Check if this service is needed in
              // Guest mode.
              .WithGuest(ProfileSelection::kOwnInstance)
              // TODO(crbug.com/41488885): Check if this service is needed for
              // Ash Internals.
              .WithAshInternals(ProfileSelection::kOwnInstance)
              .Build()) {
#if BUILDFLAG(USE_NSS_CERTS)
  // On platforms that use NSS, NSS should be initialized when a
  // ProfileNetworkContextService is created to ensure that NSS trust anchors
  // are available and NSS can be used to enumerate client certificates if
  // requested.
  DependsOn(NssServiceFactory::GetInstance());
#endif
#if BUILDFLAG(IS_CHROMEOS)
  DependsOn(policy::PolicyCertServiceFactory::GetInstance());
  DependsOn(chromeos::CertificateProviderServiceFactory::GetInstance());
#endif
#if BUILDFLAG(CHROME_ROOT_STORE_CERT_MANAGEMENT_UI)
  DependsOn(net::ServerCertificateDatabaseServiceFactory::GetInstance());
#endif
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  DependsOn(client_certificates::CertificateProvisioningServiceFactory::
                GetInstance());
#endif
  DependsOn(CookieSettingsFactory::GetInstance());
  DependsOn(HostContentSettingsMapFactory::GetInstance());
  DependsOn(TrackingProtectionSettingsFactory::GetInstance());
  DependsOn(PrivacySandboxSettingsFactory::GetInstance());
  DependsOn(FederatedIdentityPermissionContextFactory::GetInstance());
  DependsOn(
      first_party_sets::FirstPartySetsPolicyServiceFactory::GetInstance());
  DependsOn(SCTReportingServiceFactory::GetInstance());
  DependsOn(IpProtectionCoreHostFactory::GetInstance());
}

ProfileNetworkContextServiceFactory::~ProfileNetworkContextServiceFactory() =
    default;

std::unique_ptr<KeyedService>
ProfileNetworkContextServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* profile) const {
  return std::make_unique<ProfileNetworkContextService>(
      Profile::FromBrowserContext(profile));
}

bool ProfileNetworkContextServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}
