// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/extension_resource.h"

#include <stddef.h>

#include <algorithm>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/crx_file/id_util.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/extension_paths.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(IS_WIN)
#include "base/test/file_path_reparse_point_win.h"
#endif

namespace extensions {

TEST(ExtensionResourceTest, CreateEmptyResource) {
  ExtensionResource resource;

  EXPECT_TRUE(resource.extension_root().empty());
  EXPECT_TRUE(resource.relative_path().empty());
  EXPECT_TRUE(resource.GetFilePath().empty());
}

const base::FilePath::StringType ToLower(
    const base::FilePath::StringType& in_str) {
  base::FilePath::StringType str(in_str);
  std::ranges::transform(str, str.begin(), tolower);
  return str;
}

TEST(ExtensionResourceTest, CreateWithMissingResourceOnDisk) {
  base::FilePath root_path;
  ASSERT_TRUE(base::PathService::Get(DIR_TEST_DATA, &root_path));
  base::FilePath relative_path;
  relative_path = relative_path.AppendASCII("cira.js");
  ExtensionId extension_id = crx_file::id_util::GenerateId("test");
  ExtensionResource resource(extension_id, root_path, relative_path);

  // The path doesn't exist on disk, we will be returned an empty path.
  EXPECT_EQ(root_path.value(), resource.extension_root().value());
  EXPECT_EQ(relative_path.value(), resource.relative_path().value());
  EXPECT_TRUE(resource.GetFilePath().empty());
}

TEST(ExtensionResourceTest, ResourcesOutsideOfPath) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath inner_dir = temp.GetPath().AppendASCII("directory");
  ASSERT_TRUE(base::CreateDirectory(inner_dir));
  base::FilePath sub_dir = inner_dir.AppendASCII("subdir");
  ASSERT_TRUE(base::CreateDirectory(sub_dir));
  base::FilePath inner_file = inner_dir.AppendASCII("inner");
  base::FilePath outer_file = temp.GetPath().AppendASCII("outer");
  ASSERT_TRUE(base::WriteFile(outer_file, "X"));
  ASSERT_TRUE(base::WriteFile(inner_file, "X"));
  ExtensionId extension_id = crx_file::id_util::GenerateId("test");

#if BUILDFLAG(IS_POSIX)
  base::FilePath symlink_file = inner_dir.AppendASCII("symlink");
  base::CreateSymbolicLink(
      base::FilePath().AppendASCII("..").AppendASCII("outer"),
      symlink_file);
#endif

#if BUILDFLAG(IS_WIN)
  base::FilePath reparse_dir = inner_dir.AppendASCII("reparse");
  ASSERT_TRUE(base::CreateDirectory(reparse_dir));
  auto reparse_point =
      base::test::FilePathReparsePoint::Create(reparse_dir, temp.GetPath());
  ASSERT_TRUE(reparse_point.has_value());
#endif

  // A non-packing extension should be able to access the file within the
  // directory.
  ExtensionResource r1(extension_id, inner_dir,
                       base::FilePath().AppendASCII("inner"));
  EXPECT_FALSE(r1.GetFilePath().empty());

  // ... but not a relative path that walks out of |inner_dir|.
  ExtensionResource r2(extension_id, inner_dir,
                       base::FilePath().AppendASCII("..").AppendASCII("outer"));
  EXPECT_TRUE(r2.GetFilePath().empty());

  // A packing extension should also be able to access the file within the
  // directory.
  ExtensionResource r3(extension_id, inner_dir,
                       base::FilePath().AppendASCII("inner"));
  r3.set_follow_symlinks_anywhere();
  EXPECT_FALSE(r3.GetFilePath().empty());

  // ... but, again, not a relative path that walks out of |inner_dir|.
  ExtensionResource r4(extension_id, inner_dir,
                       base::FilePath().AppendASCII("..").AppendASCII("outer"));
  r4.set_follow_symlinks_anywhere();
  EXPECT_TRUE(r4.GetFilePath().empty());

  // ... and not even when clever current-directory syntax is present. Note
  // that the path for this test case can't start with the current directory
  // component due to quirks in FilePath::Append(), and the path must exist.
  ExtensionResource r4a(
      extension_id, inner_dir,
      base::FilePath().AppendASCII("subdir").AppendASCII(".").AppendASCII("..").
      AppendASCII("..").AppendASCII("outer"));
  r4a.set_follow_symlinks_anywhere();
  EXPECT_TRUE(r4a.GetFilePath().empty());

#if BUILDFLAG(IS_POSIX)
  // The non-packing extension should also not be able to access a resource that
  // symlinks out of the directory.
  ExtensionResource r5(extension_id, inner_dir,
                       base::FilePath().AppendASCII("symlink"));
  EXPECT_TRUE(r5.GetFilePath().empty());

  // ... but a packing extension can.
  ExtensionResource r6(extension_id, inner_dir,
                       base::FilePath().AppendASCII("symlink"));
  r6.set_follow_symlinks_anywhere();
  EXPECT_FALSE(r6.GetFilePath().empty());
#endif

#if BUILDFLAG(IS_WIN)
  base::FilePath outer_via_reparse =
      base::FilePath().AppendASCII("reparse").AppendASCII("outer");

  // The non-packing extension should also not be able to access a resource that
  // points out of the directory via a reparse point.
  ExtensionResource r7(extension_id, inner_dir, outer_via_reparse);
  EXPECT_TRUE(r7.GetFilePath().empty());

  // ... but a packing extension can.
  ExtensionResource r8(extension_id, inner_dir, outer_via_reparse);
  r8.set_follow_symlinks_anywhere();
  EXPECT_FALSE(r8.GetFilePath().empty());

  // Make sure that a non-normalized extension root path is supported.
  base::FilePath inner_dir_non_normalized =
      temp.GetPath().AppendASCII("dIrEcToRy");
  ExtensionResource r9(extension_id, inner_dir_non_normalized,
                       base::FilePath().AppendASCII("inner"));
  EXPECT_FALSE(r9.GetFilePath().empty());

  // Make sure that a network root path is supported by converting a path such
  // as C:\temp to \\localhost\c$\temp. Regression test for crbug.com/410059474.
  base::FilePath::CharType drive_letter =
      base::ToLowerASCII(inner_dir.value().at(0));
  EXPECT_GE(drive_letter, 'a');
  EXPECT_LE(drive_letter, 'z');
  EXPECT_EQ(inner_dir.value().at(1), ':');
  EXPECT_EQ(inner_dir.value().at(2), '\\');
  base::FilePath inner_dir_network(
      base::FilePath::StringType(FILE_PATH_LITERAL("\\\\localhost\\")) +
      drive_letter + FILE_PATH_LITERAL("$\\") + inner_dir.value().substr(3));
  ExtensionResource r10(extension_id, inner_dir_network,
                        base::FilePath().AppendASCII("inner"));
  EXPECT_FALSE(r10.GetFilePath().empty());
#endif
}

TEST(ExtensionResourceTest, CreateWithAllResourcesOnDisk) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  // Create resource in the extension root.
  const char* filename = "res.ico";
  base::FilePath root_resource = temp.GetPath().AppendASCII(filename);
  std::string data = "some foo";
  ASSERT_TRUE(base::WriteFile(root_resource, data));

  // Create l10n resources (for current locale and its parents).
  base::FilePath l10n_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(l10n_path));

  std::vector<std::string> locales = l10n_util::GetParentLocales(
      l10n_util::GetApplicationLocale(std::string()));
  ASSERT_FALSE(locales.empty());
  for (const std::string& locale : locales) {
    base::FilePath make_path;
    make_path = l10n_path.AppendASCII(locale);
    ASSERT_TRUE(base::CreateDirectory(make_path));
    ASSERT_TRUE(base::WriteFile(make_path.AppendASCII(filename), data));
  }

  base::FilePath path;
  ExtensionId extension_id = crx_file::id_util::GenerateId("test");
  ExtensionResource resource(extension_id, temp.GetPath(),
                             base::FilePath().AppendASCII(filename));
  const base::FilePath& resolved_path = resource.GetFilePath();

  base::FilePath expected_path;
  // Expect default path only, since fallback logic is disabled.
  // See http://crbug.com/27359.
  expected_path = base::MakeAbsoluteFilePath(root_resource);
  ASSERT_FALSE(expected_path.empty());

  EXPECT_EQ(ToLower(expected_path.value()), ToLower(resolved_path.value()));
  EXPECT_EQ(ToLower(temp.GetPath().value()),
            ToLower(resource.extension_root().value()));
  EXPECT_EQ(ToLower(base::FilePath().AppendASCII(filename).value()),
            ToLower(resource.relative_path().value()));
}

}  // namespace extensions
