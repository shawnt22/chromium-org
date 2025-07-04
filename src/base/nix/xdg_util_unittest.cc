// Copyright 2010 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/nix/xdg_util.h"

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/nix/scoped_xdg_activation_token_injector.h"
#include "base/process/launch.h"
#include "base/strings/cstring_view.h"
#include "base/test/bind.h"
#include "base/test/scoped_path_override.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/cleanup/cleanup.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::StrEq;

namespace base::nix {

namespace {

class MockEnvironment : public Environment {
 public:
  MOCK_METHOD(std::optional<std::string>, GetVar, (cstring_view), (override));
  MOCK_METHOD(bool,
              SetVar,
              (cstring_view, const std::string& new_value),
              (override));
  MOCK_METHOD(bool, UnSetVar, (cstring_view), (override));
};

// Needs to be const char* to make gmock happy.
const char* const kDesktopGnome = "gnome";
const char* const kDesktopGnomeFallback = "gnome-fallback";
const char* const kDesktopMATE = "mate";
const char* const kDesktopKDE4 = "kde4";
const char* const kDesktopKDE = "kde";
const char* const kDesktopXFCE = "xfce";
const char* const kXdgDesktopCinnamon = "X-Cinnamon";
const char* const kXdgDesktopDeepin = "Deepin";
const char* const kXdgDesktopGNOME = "GNOME";
const char* const kXdgDesktopGNOMEClassic = "GNOME:GNOME-Classic";
const char* const kXdgDesktopKDE = "KDE";
const char* const kXdgDesktopPantheon = "Pantheon";
const char* const kXdgDesktopUKUI = "UKUI";
const char* const kXdgDesktopUnity = "Unity";
const char* const kXdgDesktopUnity7 = "Unity:Unity7";
const char* const kXdgDesktopUnity8 = "Unity:Unity8";
const char* const kXdgDesktopCosmic = "COSMIC";
const char* const kKDESessionKDE5 = "5";
const char* const kKDESessionKDE6 = "6";

const char kDesktopSession[] = "DESKTOP_SESSION";
const char kKDESession[] = "KDE_SESSION_VERSION";

const char* const kSessionUnknown = "invalid session";
const char* const kSessionUnspecified = "unspecified";
const char* const kSessionTty = "tty";
const char* const kSessionMir = "mir";
const char* const kSessionX11 = "x11";
const char* const kSessionWayland = "wayland";
const char* const kSessionWaylandCapital = "Wayland";
const char* const kSessionWaylandWhitespace = "wayland ";
const char* const kXdgActivationTokenFromEnv = "test token from env";
const char* const kXdgActivationTokenFromCmdLine = "test token from cmd line";

// This helps EXPECT_THAT(..., ElementsAre(...)) print out more meaningful
// failure messages.
std::vector<std::string> FilePathsToStrings(
    const std::vector<base::FilePath>& paths) {
  std::vector<std::string> values;
  for (const auto& path : paths) {
    values.push_back(path.value());
  }
  return values;
}

}  // namespace

TEST(XDGUtilTest, GetXDGDataWriteLocation) {
  // Test that it returns $XDG_DATA_HOME.
  {
    MockEnvironment getter;
    EXPECT_CALL(getter, GetVar(StrEq("XDG_DATA_HOME")))
        .WillOnce(Return("/user/path"));

    ScopedPathOverride home_override(DIR_HOME, FilePath("/home/user"),
                                     /*is_absolute=*/true, /*create=*/false);
    FilePath path = GetXDGDataWriteLocation(&getter);
    EXPECT_EQ("/user/path", path.value());
  }

  // Test that $XDG_DATA_HOME falls back to $HOME/.local/share.
  {
    MockEnvironment getter;
    EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
    ScopedPathOverride home_override(DIR_HOME, FilePath("/home/user"),
                                     /*is_absolute=*/true, /*create=*/false);
    FilePath path = GetXDGDataWriteLocation(&getter);
    EXPECT_EQ("/home/user/.local/share", path.value());
  }
}

TEST(XDGUtilTest, GetXDGDataSearchLocations) {
  // Test that it returns $XDG_DATA_HOME + $XDG_DATA_DIRS.
  {
    MockEnvironment getter;
    EXPECT_CALL(getter, GetVar(StrEq("XDG_DATA_HOME")))
        .WillOnce(Return("/user/path"));
    EXPECT_CALL(getter, GetVar(StrEq("XDG_DATA_DIRS")))
        .WillOnce(Return("/system/path/1:/system/path/2"));
    ScopedPathOverride home_override(DIR_HOME, FilePath("/home/user"),
                                     /*is_absolute=*/true, /*create=*/false);
    EXPECT_THAT(
        FilePathsToStrings(GetXDGDataSearchLocations(&getter)),
        testing::ElementsAre("/user/path", "/system/path/1", "/system/path/2"));
  }

  // Test that $XDG_DATA_HOME falls back to $HOME/.local/share.
  {
    MockEnvironment getter;
    EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
    EXPECT_CALL(getter, GetVar(StrEq("XDG_DATA_DIRS")))
        .WillOnce(Return("/system/path/1:/system/path/2"));

    ScopedPathOverride home_override(DIR_HOME, FilePath("/home/user"),
                                     /*is_absolute=*/true, /*create=*/false);
    EXPECT_THAT(FilePathsToStrings(GetXDGDataSearchLocations(&getter)),
                testing::ElementsAre("/home/user/.local/share",
                                     "/system/path/1", "/system/path/2"));
  }

  // Test that if neither $XDG_DATA_HOME nor $HOME are specified, it still
  // succeeds.
  {
    MockEnvironment getter;
    EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
    EXPECT_CALL(getter, GetVar(StrEq("XDG_DATA_DIRS")))
        .WillOnce(Return("/system/path/1:/system/path/2"));
    std::vector<std::string> results =
        FilePathsToStrings(GetXDGDataSearchLocations(&getter));
    ASSERT_EQ(3U, results.size());
    EXPECT_FALSE(results[0].empty());
    EXPECT_EQ("/system/path/1", results[1]);
    EXPECT_EQ("/system/path/2", results[2]);
  }

  // Test that $XDG_DATA_DIRS falls back to the two default paths.
  {
    MockEnvironment getter;
    EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
    EXPECT_CALL(getter, GetVar(StrEq("XDG_DATA_HOME")))
        .WillOnce(Return("/user/path"));
    ScopedPathOverride home_override(DIR_HOME, FilePath("/home/user"),
                                     /*is_absolute=*/true, /*create=*/false);
    EXPECT_THAT(
        FilePathsToStrings(GetXDGDataSearchLocations(&getter)),
        testing::ElementsAre("/user/path", "/usr/local/share", "/usr/share"));
  }
}

TEST(XDGUtilTest, GetDesktopEnvironmentGnome) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kDesktopSession)))
      .WillOnce(Return(kDesktopGnome));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_GNOME, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetDesktopEnvironmentMATE) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kDesktopSession)))
      .WillOnce(Return(kDesktopMATE));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_GNOME, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetDesktopEnvironmentKDE4) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kDesktopSession)))
      .WillOnce(Return(kDesktopKDE4));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_KDE4, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetDesktopEnvironmentKDE3) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kDesktopSession)))
      .WillOnce(Return(kDesktopKDE));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_KDE3, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetDesktopEnvironmentXFCE) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kDesktopSession)))
      .WillOnce(Return(kDesktopXFCE));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_XFCE, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopCinnamon) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopCinnamon));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_CINNAMON, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopDeepin) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopDeepin));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_DEEPIN, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopGnome) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopGNOME));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_GNOME, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopGnomeClassic) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopGNOMEClassic));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_GNOME, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopGnomeFallback) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopUnity));
  EXPECT_CALL(getter, GetVar(StrEq(kDesktopSession)))
      .WillOnce(Return(kDesktopGnomeFallback));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_GNOME, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopKDE5) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopKDE));
  EXPECT_CALL(getter, GetVar(StrEq(kKDESession)))
      .WillOnce(Return(kKDESessionKDE5));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_KDE5, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopKDE6) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopKDE));
  EXPECT_CALL(getter, GetVar(StrEq(kKDESession)))
      .WillOnce(Return(kKDESessionKDE6));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_KDE6, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopKDE4) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopKDE));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_KDE4, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopPantheon) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopPantheon));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_PANTHEON, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopUKUI) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopUKUI));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_UKUI, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopUnity) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopUnity));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_UNITY, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopUnity7) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopUnity7));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_UNITY, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopUnity8) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopUnity8));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_UNITY, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgDesktopCosmic) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgCurrentDesktopEnvVar)))
      .WillOnce(Return(kXdgDesktopCosmic));

  EXPECT_EQ(DESKTOP_ENVIRONMENT_COSMIC, GetDesktopEnvironment(&getter));
}

TEST(XDGUtilTest, GetXdgSessiontypeUnset) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));

  EXPECT_EQ(SessionType::kUnset, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeOther) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionUnknown));

  EXPECT_EQ(SessionType::kOther, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeUnspecified) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionUnspecified));

  EXPECT_EQ(SessionType::kUnspecified, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeTty) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionTty));

  EXPECT_EQ(SessionType::kTty, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeMir) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionMir));

  EXPECT_EQ(SessionType::kMir, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeX11) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionX11));

  EXPECT_EQ(SessionType::kX11, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeWayland) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionWayland));

  EXPECT_EQ(SessionType::kWayland, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeWaylandCapital) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionWaylandCapital));

  EXPECT_EQ(SessionType::kWayland, GetSessionType(getter));
}

TEST(XDGUtilTest, GetXdgSessionTypeWaylandWhitespace) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq(kXdgSessionTypeEnvVar)))
      .WillOnce(Return(kSessionWaylandWhitespace));

  EXPECT_EQ(SessionType::kWayland, GetSessionType(getter));
}

TEST(XDGUtilTest, ExtractXdgActivationTokenFromEnvNotSet) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(_)).WillRepeatedly(Return(std::nullopt));
  EXPECT_EQ(std::nullopt, ExtractXdgActivationTokenFromEnv(getter));
  EXPECT_EQ(std::nullopt, TakeXdgActivationToken());
}

TEST(XDGUtilTest, ExtractXdgActivationTokenFromEnv) {
  MockEnvironment getter;
  EXPECT_CALL(getter, GetVar(StrEq("XDG_ACTIVATION_TOKEN")))
      .WillOnce(Return(kXdgActivationTokenFromEnv));
  EXPECT_CALL(getter, UnSetVar(StrEq("XDG_ACTIVATION_TOKEN")));
  EXPECT_EQ(kXdgActivationTokenFromEnv,
            ExtractXdgActivationTokenFromEnv(getter));
  EXPECT_EQ(kXdgActivationTokenFromEnv, TakeXdgActivationToken());
  // Should be cleared after the token is taken once.
  EXPECT_EQ(std::nullopt, TakeXdgActivationToken());

  EXPECT_CALL(getter, GetVar(StrEq("XDG_ACTIVATION_TOKEN")))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(getter, GetVar(StrEq("DESKTOP_STARTUP_ID")))
      .WillOnce(Return(kXdgActivationTokenFromEnv));
  EXPECT_CALL(getter, UnSetVar(StrEq("DESKTOP_STARTUP_ID")));
  EXPECT_EQ(kXdgActivationTokenFromEnv,
            ExtractXdgActivationTokenFromEnv(getter));
  EXPECT_EQ(kXdgActivationTokenFromEnv, TakeXdgActivationToken());
  // Should be cleared after the token is taken once.
  EXPECT_EQ(std::nullopt, TakeXdgActivationToken());
}

TEST(XDGUtilTest, ExtractXdgActivationTokenFromCmdLineNotSet) {
  CommandLine command_line(CommandLine::NO_PROGRAM);
  ExtractXdgActivationTokenFromCmdLine(command_line);
  EXPECT_EQ(std::nullopt, TakeXdgActivationToken());
}

TEST(XDGUtilTest, ExtractXdgActivationTokenFromCmdLine) {
  CommandLine command_line(CommandLine::NO_PROGRAM);
  MockEnvironment getter;
  // Extract activation token initially from env.
  EXPECT_CALL(getter, GetVar(StrEq("XDG_ACTIVATION_TOKEN")))
      .WillOnce(Return(kXdgActivationTokenFromEnv));
  EXPECT_CALL(getter, UnSetVar(StrEq("XDG_ACTIVATION_TOKEN")));
  EXPECT_EQ(kXdgActivationTokenFromEnv,
            ExtractXdgActivationTokenFromEnv(getter));
  // Now extract token from command line.
  command_line.AppendSwitchASCII(kXdgActivationTokenSwitch,
                                 kXdgActivationTokenFromCmdLine);
  ExtractXdgActivationTokenFromCmdLine(command_line);
  // It should match the one from command line, not env.
  EXPECT_EQ(kXdgActivationTokenFromCmdLine, TakeXdgActivationToken());
  // Should be cleared after the token is taken once.
  EXPECT_EQ(std::nullopt, TakeXdgActivationToken());
}

TEST(XDGUtilTest, ScopedXdgActivationTokenInjector) {
  CommandLine cmd_line(CommandLine::NO_PROGRAM);
  MockEnvironment getter;
  cmd_line.AppendSwitch("x");
  cmd_line.AppendSwitch("y");
  cmd_line.AppendSwitch("z");
  CommandLine::SwitchMap initial_switches = cmd_line.GetSwitches();
  // Set token value in env
  EXPECT_CALL(getter, GetVar(StrEq("XDG_ACTIVATION_TOKEN")))
      .WillOnce(Return(kXdgActivationTokenFromEnv));
  EXPECT_CALL(getter, UnSetVar(StrEq("XDG_ACTIVATION_TOKEN")));
  {
    ScopedXdgActivationTokenInjector scoped_injector(cmd_line, getter);
    for (const auto& pair : initial_switches) {
      EXPECT_TRUE(cmd_line.HasSwitch(pair.first));
    }
    EXPECT_TRUE(cmd_line.HasSwitch(kXdgActivationTokenSwitch));
    EXPECT_EQ(kXdgActivationTokenFromEnv,
              cmd_line.GetSwitchValueASCII(kXdgActivationTokenSwitch));
  }
  for (const auto& pair : initial_switches) {
    EXPECT_TRUE(cmd_line.HasSwitch(pair.first));
  }
  EXPECT_FALSE(cmd_line.HasSwitch(nix::kXdgActivationTokenSwitch));
}

TEST(XDGUtilTest, LaunchOptionsWithXdgActivation) {
  bool received_empty_launch_options = false;
  CreateLaunchOptionsWithXdgActivation(base::BindLambdaForTesting(
      [&received_empty_launch_options](LaunchOptions options) {
        EXPECT_TRUE(options.environment.empty());
        received_empty_launch_options = true;
      }));
  EXPECT_TRUE(received_empty_launch_options);

  absl::Cleanup reset_token_creator = [] {
    SetXdgActivationTokenCreator(XdgActivationTokenCreator());
  };
  SetXdgActivationTokenCreator(
      base::BindRepeating([](XdgActivationTokenCallback callback) {
        std::move(callback).Run(kXdgActivationTokenFromEnv);
      }));

  bool received_launch_options_with_test_token = false;
  CreateLaunchOptionsWithXdgActivation(base::BindLambdaForTesting(
      [&received_launch_options_with_test_token](LaunchOptions options) {
        EXPECT_EQ(options.environment["XDG_ACTIVATION_TOKEN"],
                  kXdgActivationTokenFromEnv);
        received_launch_options_with_test_token = true;
      }));
  EXPECT_TRUE(received_launch_options_with_test_token);
}

}  // namespace base::nix
