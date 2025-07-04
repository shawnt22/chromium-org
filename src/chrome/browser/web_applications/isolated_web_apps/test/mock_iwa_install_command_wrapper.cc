// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/isolated_web_apps/test/mock_iwa_install_command_wrapper.h"

#include <optional>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/isolated_web_apps/commands/install_isolated_web_app_command.h"
#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_url_info.h"
#include "chrome/browser/web_applications/isolated_web_apps/policy/isolated_web_app_policy_manager.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_contents/web_app_data_retriever.h"
#include "chrome/browser/web_applications/web_contents/web_contents_manager.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace web_app {

namespace {

class MockInstallIsolatedWebApp : public InstallIsolatedWebAppCommand {
 public:
  MockInstallIsolatedWebApp(
      const IsolatedWebAppUrlInfo& url_info,
      const IsolatedWebAppInstallSource& install_source,
      const std::optional<base::Version>& expected_version,
      std::unique_ptr<content::WebContents> web_contents,
      std::unique_ptr<ScopedKeepAlive> optional_keep_alive,
      std::unique_ptr<ScopedProfileKeepAlive> optional_profile_keep_alive,
      base::OnceCallback<
          void(base::expected<InstallIsolatedWebAppCommandSuccess,
                              InstallIsolatedWebAppCommandError>)> callback,
      std::unique_ptr<IsolatedWebAppInstallCommandHelper> command_helper,
      MockIwaInstallCommandWrapper::ExecutionMode execution_mode)
      : InstallIsolatedWebAppCommand(url_info,
                                     install_source,
                                     expected_version,
                                     std::move(web_contents),
                                     std::move(optional_keep_alive),
                                     std::move(optional_profile_keep_alive),
                                     std::move(callback),
                                     std::move(command_helper)),
        url_info_(url_info),
        execution_mode_(execution_mode) {}

 protected:
  // `InstallIsolatedWebAppCommand`:
  void StartWithLock(std::unique_ptr<AppLock> lock) override {
    switch (execution_mode_) {
      case MockIwaInstallCommandWrapper::ExecutionMode::kRunCommand:
        InstallIsolatedWebAppCommand::StartWithLock(std::move(lock));
        break;
      case MockIwaInstallCommandWrapper::ExecutionMode::kSimulateSuccess:
        CompleteAndSelfDestruct(
            CommandResult::kSuccess,
            InstallIsolatedWebAppCommandSuccess(
                url_info_, base::Version(),
                IsolatedWebAppStorageLocation::OwnedBundle(
                    /*dir_name_ascii=*/"some_dir", /*dev_mode=*/false)));
        break;
      case MockIwaInstallCommandWrapper::ExecutionMode::kSimulateFailure:
        CompleteAndSelfDestruct(
            CommandResult::kFailure,
            base::unexpected(InstallIsolatedWebAppCommandError{
                .message = "dummy error message"}));
        break;
    }
  }

 private:
  const IsolatedWebAppUrlInfo url_info_;
  const MockIwaInstallCommandWrapper::ExecutionMode execution_mode_;
};

}  // namespace

MockIwaInstallCommandWrapper::MockIwaInstallCommandWrapper(
    Profile* profile,
    web_app::WebAppProvider* provider,
    ExecutionMode execution_mode,
    bool schedule_command_immediately)
    : provider_(provider),
      profile_(profile),
      execution_mode_(execution_mode),
      schedule_command_immediately_(schedule_command_immediately) {}

MockIwaInstallCommandWrapper::~MockIwaInstallCommandWrapper() = default;

void MockIwaInstallCommandWrapper::Install(
    const IsolatedWebAppInstallSource& install_source,
    const IsolatedWebAppUrlInfo& url_info,
    const base::Version& expected_version,
    WebAppCommandScheduler::InstallIsolatedWebAppCallback callback) {
  install_source_ = install_source;
  url_info_ = url_info;
  expected_version_ = expected_version;
  callback_ = std::move(callback);
  if (schedule_command_immediately_) {
    ScheduleCommand();
  }
}

void MockIwaInstallCommandWrapper::ScheduleCommand() {
  CHECK(!command_was_scheduled_);
  CHECK(install_source_.has_value());
  CHECK(url_info_.has_value());
  CHECK(expected_version_.has_value());
  CHECK(callback_.has_value());
  command_was_scheduled_ = true;
  provider_->command_manager().ScheduleCommand(
      std::make_unique<MockInstallIsolatedWebApp>(
          *url_info_, *install_source_, *expected_version_,
          IsolatedWebAppInstallCommandHelper::CreateIsolatedWebAppWebContents(
              *profile_),
          /*optional_keep_alive=*/nullptr,
          /*optional_profile_keep_alive=*/nullptr, std::move(*callback_),
          std::make_unique<IsolatedWebAppInstallCommandHelper>(
              *url_info_,
              provider_->web_contents_manager().CreateDataRetriever(),
              IsolatedWebAppInstallCommandHelper::
                  CreateDefaultResponseReaderFactory(*profile_)),
          execution_mode_));
}

}  // namespace web_app
