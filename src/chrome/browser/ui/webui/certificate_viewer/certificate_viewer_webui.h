// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CERTIFICATE_VIEWER_CERTIFICATE_VIEWER_WEBUI_H_
#define CHROME_BROWSER_UI_WEBUI_CERTIFICATE_VIEWER_CERTIFICATE_VIEWER_WEBUI_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/common/net/x509_certificate_model.h"
#include "components/server_certificate_database/server_certificate_database.h"
#include "components/server_certificate_database/server_certificate_database.pb.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/web_dialogs/web_dialog_delegate.h"

namespace content {
class WebContents;
}

using chrome_browser_server_certificate_database::CertificateTrust;

typedef base::RepeatingCallback<void(
    net::ServerCertificateDatabase::CertInformation,
    base::OnceCallback<void(bool)>)>
    CertMetadataModificationsCallback;

class ConstrainedWebDialogDelegate;

// Dialog for displaying detailed certificate information. This is used on
// desktop builds to display detailed information in a floating dialog when the
// user clicks on "Certificate Information" from the lock icon of a web site or
// "View" from the Certificate Manager.
class CertificateViewerDialog : public ui::WebDialogDelegate {
 public:
  static CertificateViewerDialog* ShowConstrained(
      std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> certs,
      content::WebContents* web_contents,
      gfx::NativeWindow parent);

  static CertificateViewerDialog* ShowConstrained(
      bssl::UniquePtr<CRYPTO_BUFFER> cert,
      content::WebContents* web_contents,
      gfx::NativeWindow parent);

  static CertificateViewerDialog* ShowConstrainedWithMetadata(
      bssl::UniquePtr<CRYPTO_BUFFER> cert,
      chrome_browser_server_certificate_database::CertificateMetadata
          cert_metadata,
      CertMetadataModificationsCallback modifications_callback,
      content::WebContents* web_contents,
      gfx::NativeWindow parent);

  CertificateViewerDialog(const CertificateViewerDialog&) = delete;
  CertificateViewerDialog& operator=(const CertificateViewerDialog&) = delete;

  ~CertificateViewerDialog() override;

  gfx::NativeWindow GetNativeWebContentsModalDialog();

 private:
  friend class CertificateViewerUITest;

  // If |cert_metadata| is present, exactly one cert should be in |certs|.
  // If |modifications_callback| is not null, |cert_metadata| must be present.
  static CertificateViewerDialog* ShowConstrained(
      std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> certs,
      std::optional<
          chrome_browser_server_certificate_database::CertificateMetadata>
          cert_metadata,
      CertMetadataModificationsCallback modifications_callback,
      content::WebContents* web_contents,
      gfx::NativeWindow parent);

  // Construct a certificate viewer for the passed in certificate. A reference
  // to the certificate pointer is added for the lifetime of the certificate
  // viewer.
  CertificateViewerDialog(
      std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> certs,
      std::optional<
          chrome_browser_server_certificate_database::CertificateMetadata>
          cert_metadata,
      CertMetadataModificationsCallback modifications_callback);

  raw_ptr<ConstrainedWebDialogDelegate, DanglingUntriaged> delegate_ = nullptr;
};

// Dialog handler which handles calls from the JS WebUI code to view certificate
// details and export the certificate.
class CertificateViewerDialogHandler : public content::WebUIMessageHandler {
 public:
  CertificateViewerDialogHandler(
      CertificateViewerDialog* dialog,
      std::vector<x509_certificate_model::X509CertificateModel> certs,
      std::optional<
          chrome_browser_server_certificate_database::CertificateMetadata>
          cert_metadata,
      CertMetadataModificationsCallback modifications_callback);

  CertificateViewerDialogHandler(const CertificateViewerDialogHandler&) =
      delete;
  CertificateViewerDialogHandler& operator=(
      const CertificateViewerDialogHandler&) = delete;

  ~CertificateViewerDialogHandler() override;

  // Overridden from WebUIMessageHandler
  void RegisterMessages() override;

 private:
  // Brings up the export certificate dialog for the chosen certificate in the
  // chain.
  //
  // The input is an integer index to the certificate in the chain to export.
  void HandleExportCertificate(const base::Value::List& args);

  // Gets the details for a specific certificate in the certificate chain.
  // Responds with a tree structure containing the fields and values for certain
  // nodes.
  //
  // The input is an integer index to the certificate in the chain to view.
  void HandleRequestCertificateFields(const base::Value::List& args);

  // Update the trust state of the certificate.
  void HandleUpdateTrustState(const base::Value::List& args);
  void UpdateTrustStateDone(const base::Value& callback_id,
                            CertificateTrust::CertificateTrustType new_trust,
                            bool success);

  void HandleAddConstraint(const base::Value::List& args);
  void HandleDeleteConstraint(const base::Value::List& args);
  void UpdateConstraintsDone(
      const base::Value& callback_id,
      const chrome_browser_server_certificate_database::Constraints
          new_constraints,
      bool success);

  bool CanModifyMetadata() const;

  // Helper function to get the certificate index. Returns -1 if the index is
  // out of range.
  int GetCertificateIndex(int requested_index) const;

  // The dialog.
  raw_ptr<CertificateViewerDialog> dialog_;

  std::vector<x509_certificate_model::X509CertificateModel> certs_;
  std::optional<chrome_browser_server_certificate_database::CertificateMetadata>
      cert_metadata_;
  // Cert Metadata modifications callback. If null, then no modifications are
  // allowed for this certificate.
  CertMetadataModificationsCallback modifications_callback_;
  base::WeakPtrFactory<CertificateViewerDialogHandler> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_CERTIFICATE_VIEWER_CERTIFICATE_VIEWER_WEBUI_H_
