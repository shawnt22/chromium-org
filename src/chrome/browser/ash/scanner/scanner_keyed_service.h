// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_SCANNER_SCANNER_KEYED_SERVICE_H_
#define CHROME_BROWSER_ASH_SCANNER_SCANNER_KEYED_SERVICE_H_

#include <memory>

#include "ash/public/cpp/scanner/scanner_profile_scoped_delegate.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chromeos/ash/components/specialized_features/feature_access_checker.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/manta/proto/scanner.pb.h"
#include "components/manta/scanner_provider.h"
#include "google_apis/common/request_sender.h"

namespace manta {
class ScannerProvider;
}  // namespace manta

namespace drive {
class DriveAPIService;
}

namespace network {
class SharedURLLoaderFactory;
}

namespace signin {
class IdentityManager;
}

// A ProfileKeyedService for the Scanner feature. This is a top level class
// that is scoped to a particular profile, and provides access to that profile
// instance to all sub classes that require a valid profile instance to
// function.
class ScannerKeyedService : public ash::ScannerProfileScopedDelegate,
                            public KeyedService {
 public:
  ScannerKeyedService(
      PrefService* pref_service,
      signin::IdentityManager* identity_manager,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::unique_ptr<manta::ScannerProvider> scanner_provider,
      specialized_features::FeatureAccessChecker::VariationsServiceCallback
          variations_service_callback);
  ScannerKeyedService(const ScannerKeyedService&) = delete;
  ScannerKeyedService& operator=(const ScannerKeyedService&) = delete;
  ~ScannerKeyedService() override;

  // ash::ScannerProfileScopedDelegate:
  specialized_features::FeatureAccessFailureSet CheckFeatureAccess()
      const override;
  void FetchActionsForImage(
      scoped_refptr<base::RefCountedMemory> jpeg_bytes,
      manta::ScannerProvider::ScannerProtoResponseCallback callback) override;
  void FetchActionDetailsForImage(
      scoped_refptr<base::RefCountedMemory> jpeg_bytes,
      manta::proto::ScannerAction selected_action,
      manta::ScannerProvider::ScannerProtoResponseCallback callback) override;
  drive::DriveServiceInterface* GetDriveService() override;
  google_apis::RequestSender* GetGoogleApisRequestSender() override;

  // KeyedService:
  void Shutdown() override;

 private:
  raw_ptr<signin::IdentityManager> identity_manager_;
  specialized_features::FeatureAccessChecker access_checker_;

  std::unique_ptr<manta::ScannerProvider> scanner_provider_;

  std::unique_ptr<drive::DriveAPIService> drive_service_;
  std::unique_ptr<google_apis::RequestSender> request_sender_;
};

#endif  // CHROME_BROWSER_ASH_SCANNER_SCANNER_KEYED_SERVICE_H_
