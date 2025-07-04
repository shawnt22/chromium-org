// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/enterprise_companion/test/test_utils.h"

#include <optional>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "build/build_config.h"
#include "chrome/enterprise_companion/enterprise_companion_branding.h"
#include "chrome/enterprise_companion/installer_paths.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace enterprise_companion {

namespace {

class TestMethodsMac : public TestMethods {
 public:
  base::FilePath GetTestExePath() override {
    return base::PathService::CheckedGet(base::DIR_EXE)
        .Append("EnterpriseCompanionTestApp")
        .Append(base::StrCat({PRODUCT_FULLNAME_STRING, ".app"}))
        .Append("Contents/MacOS")
        .Append(PRODUCT_FULLNAME_STRING);
  }

  void Clean() override {
    TestMethods::Clean();
    ASSERT_TRUE(base::DeleteFile(GetKSAdminPath()));
  }

  void ExpectInstalled() override {
    TestMethods::ExpectInstalled();
    std::optional<base::FilePath> install_dir = GetInstallDirectory();
    ASSERT_TRUE(install_dir);
    int exe_mode = 0;
    ASSERT_TRUE(base::GetPosixFilePermissions(*install_dir, &exe_mode));
    EXPECT_EQ(exe_mode, base::FILE_PERMISSION_USER_MASK |
                            base::FILE_PERMISSION_READ_BY_GROUP |
                            base::FILE_PERMISSION_EXECUTE_BY_GROUP |
                            base::FILE_PERMISSION_READ_BY_OTHERS |
                            base::FILE_PERMISSION_EXECUTE_BY_OTHERS);
  }

  void Install() override {
    InstallFakeKSAdmin(/*should_succeed=*/true);
    TestMethods::Install();
  }

#if BUILDFLAG(CHROMIUM_BRANDING)
  void InstallOlderVersion() override {
    InstallFakeKSAdmin(/*should_succeed=*/true);
    TestMethods::InstallOlderVersion();
  }
#endif  // #if BUILDFLAG(CHROMIUM_BRANDING)

#if BUILDFLAG(CHROMIUM_BRANDING)
  base::FilePath GetOlderVersionExePath() override {
    return base::PathService::CheckedGet(base::DIR_EXE)
        .Append("old_enterprise_companion")
#if defined(ARCH_CPU_ARM64)
        .Append("chromium_mac_arm64")
#elif defined(ARCH_CPU_X86_64)
        .Append("chromium_mac_amd64")
#else
#error Unsupported architecture
#endif
        .Append("cipd")
        .Append(base::StrCat({PRODUCT_FULLNAME_STRING, ".app"}))
        .Append("Contents/MacOS")
        .Append(PRODUCT_FULLNAME_STRING);
  }
#endif
};

}  // namespace

TestMethods& GetTestMethods() {
  static base::NoDestructor<TestMethodsMac> test_methods;
  return *test_methods;
}

void InstallFakeKSAdmin(bool should_succeed) {
  base::FilePath ksadmin_path = GetKSAdminPath();
  ASSERT_TRUE(base::CreateDirectory(ksadmin_path.DirName()));
  ASSERT_TRUE(base::WriteFile(
      ksadmin_path,
      base::StrCat({"#!/bin/bash\nexit ", should_succeed ? "0" : "1"})));
  ASSERT_TRUE(base::SetPosixFilePermissions(ksadmin_path,
                                            base::FILE_PERMISSION_USER_MASK));
}

}  // namespace enterprise_companion
