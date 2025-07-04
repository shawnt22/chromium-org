// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/to_string.h"
#include "base/test/bind.h"
#include "base/test/gmock_expected_support.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/web_applications/test/isolated_web_app_test_utils.h"
#include "chrome/browser/web_applications/isolated_web_apps/commands/install_isolated_web_app_command.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_install_source.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_source.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_storage_location.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_trust_checker.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_url_info.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/isolated_web_app_builder.h"
#include "chrome/browser/web_applications/isolated_web_apps/test/test_signed_web_bundle_builder.h"
#include "chrome/browser/web_applications/jobs/uninstall/remove_web_app_job.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "components/web_package/signed_web_bundles/signed_web_bundle_id.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/functional/overload.h"

namespace web_app {
namespace {

class IsolatedWebAppUninstallBrowserTest
    : public IsolatedWebAppBrowserTestHarness,
      public ::testing::WithParamInterface<IwaSourceBundleModeAndFileOp> {
 protected:
  using InstallResult = base::expected<InstallIsolatedWebAppCommandSuccess,
                                       InstallIsolatedWebAppCommandError>;

  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    src_bundle_path_ = scoped_temp_dir_.GetPath().Append(
        base::FilePath::FromASCII("bundle.swbn"));

    IsolatedWebAppBrowserTestHarness::SetUp();
  }

  void CreateAndInstallBundle() {
    std::unique_ptr<BundledIsolatedWebApp> bundle =
        IsolatedWebAppBuilder(
            ManifestBuilder().SetName("app-1.0.0").SetVersion("1.0.0"))
            .BuildBundle(src_bundle_path_, key_pair_);

    switch (mode_and_file_op_) {
      case IwaSourceBundleModeAndFileOp::kDevModeCopy:
        ASSERT_THAT(bundle->InstallWithSource(
                        profile(), &IsolatedWebAppInstallSource::FromDevUi,
                        IwaSourceBundleDevFileOp::kCopy),
                    base::test::HasValue());
        break;
      case IwaSourceBundleModeAndFileOp::kDevModeMove:
        ASSERT_THAT(bundle->InstallWithSource(
                        profile(), &IsolatedWebAppInstallSource::FromDevUi,
                        IwaSourceBundleDevFileOp::kMove),
                    base::test::HasValue());
        break;
      case IwaSourceBundleModeAndFileOp::kProdModeCopy:
      case IwaSourceBundleModeAndFileOp::kProdModeMove:
        ASSERT_THAT(
            bundle->InstallWithSource(
                profile(), &IsolatedWebAppInstallSource::FromGraphicalInstaller,
                mode_and_file_op_),
            base::test::HasValue());
        break;
    }

    const WebApp* web_app =
        provider()->registrar_unsafe().GetAppById(url_info_.app_id());

    ASSERT_TRUE(web_app);
  }

  void Uninstall() {
    base::RunLoop run_loop;
    auto* browsing_data_remover = profile()->GetBrowsingDataRemover();
    browsing_data_remover->SetWouldCompleteCallbackForTesting(
        base::BindLambdaForTesting([&](base::OnceClosure callback) {
          if (browsing_data_remover->GetPendingTaskCountForTesting() == 1) {
            run_loop.Quit();
          }
          std::move(callback).Run();
        }));

    base::test::TestFuture<webapps::UninstallResultCode> future;
    provider()->scheduler().RemoveUserUninstallableManagements(
        url_info_.app_id(), webapps::WebappUninstallSource::kAppsPage,
        future.GetCallback());

    auto code = future.Get();
    ASSERT_EQ(code, webapps::UninstallResultCode::kAppRemoved);
    run_loop.Run();
  }

  WebAppProvider* provider() {
    return WebAppProvider::GetForWebApps(profile());
  }

  IwaSourceBundleModeAndFileOp mode_and_file_op_ = GetParam();

  base::ScopedTempDir scoped_temp_dir_;

  web_package::test::Ed25519KeyPair key_pair_ =
      test::GetDefaultEd25519KeyPair();

  IsolatedWebAppUrlInfo url_info_ =
      IsolatedWebAppUrlInfo::CreateFromSignedWebBundleId(
          test::GetDefaultEd25519WebBundleId());

  base::FilePath src_bundle_path_;
};

IN_PROC_BROWSER_TEST_P(IsolatedWebAppUninstallBrowserTest, Succeeds) {
  // Create buldle and install an IWA and check that it is in the desired
  // stated.
  ASSERT_NO_FATAL_FAILURE(CreateAndInstallBundle());
  std::optional<base::FilePath> path_to_iwa_in_profile;

  const WebApp* web_app_before =
      provider()->registrar_unsafe().GetAppById(url_info_.app_id());
  ASSERT_TRUE(web_app_before);
  ASSERT_TRUE(web_app_before->isolation_data().has_value());

  std::visit(absl::Overload{
                 [&](const IwaStorageOwnedBundle& location) {
                   // Verify that .swbn file was copied to the profile
                   // directory.
                   base::FilePath path = location.GetPath(profile()->GetPath());
                   base::ScopedAllowBlockingForTesting allow_blocking;
                   EXPECT_NE(path, src_bundle_path_);
                   EXPECT_THAT(location, test::OwnedIwaBundleExists(
                                             profile()->GetPath()));
                   path_to_iwa_in_profile = path;
                 },
                 [&](const IwaStorageUnownedBundle& location) {
                   EXPECT_EQ(location.path(), src_bundle_path_);
                 },
                 [&](const IwaStorageProxy& location) { FAIL(); }},
             web_app_before->isolation_data()->location().variant());

  // Uninstall the app and check that the copied to profile directory
  // file has been removed.
  ASSERT_NO_FATAL_FAILURE(Uninstall());
  const WebApp* web_app_after =
      provider()->registrar_unsafe().GetAppById(url_info_.app_id());
  ASSERT_FALSE(web_app_after);

  base::ScopedAllowBlockingForTesting allow_blocking;
  switch (mode_and_file_op_) {
    case IwaSourceBundleModeAndFileOp::kDevModeCopy:
    case IwaSourceBundleModeAndFileOp::kProdModeCopy:
      EXPECT_TRUE(base::PathExists(src_bundle_path_));
      break;
    case IwaSourceBundleModeAndFileOp::kDevModeMove:
    case IwaSourceBundleModeAndFileOp::kProdModeMove:
      EXPECT_FALSE(base::PathExists(src_bundle_path_));
      break;
  }

  switch (mode_and_file_op_) {
    case IwaSourceBundleModeAndFileOp::kDevModeMove:
    case IwaSourceBundleModeAndFileOp::kProdModeMove:
    case IwaSourceBundleModeAndFileOp::kDevModeCopy:
    case IwaSourceBundleModeAndFileOp::kProdModeCopy:
      // Verify that the random directory was removed.
      EXPECT_FALSE(base::PathExists(path_to_iwa_in_profile.value()));
      EXPECT_FALSE(base::PathExists(path_to_iwa_in_profile.value().DirName()));
      break;
  }
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    IsolatedWebAppUninstallBrowserTest,
    ::testing::Values(IwaSourceBundleModeAndFileOp::kDevModeCopy,
                      IwaSourceBundleModeAndFileOp::kDevModeMove,
                      IwaSourceBundleModeAndFileOp::kProdModeCopy,
                      IwaSourceBundleModeAndFileOp::kProdModeMove),
    [](::testing::TestParamInfo<IwaSourceBundleModeAndFileOp> info) {
      return base::ToString(info.param);
    });

}  // namespace
}  // namespace web_app
