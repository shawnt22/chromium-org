// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/tracing/common/background_tracing_utils.h"

#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/test/test_proto_loader.h"
#include "base/threading/thread_restrictions.h"
#include "components/prefs/testing_pref_service.h"
#include "components/tracing/common/background_tracing_state_manager.h"
#include "components/tracing/common/pref_names.h"
#include "components/tracing/common/tracing_scenarios_config.h"
#include "components/tracing/common/tracing_switches.h"
#include "content/public/browser/background_tracing_manager.h"
#include "content/public/browser/tracing_delegate.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/snappy/src/snappy.h"

namespace {

class BackgroundTracingUtilsTest : public testing::Test {
  content::BrowserTaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  content::TracingDelegate tracing_delegate_;
  std::unique_ptr<content::BackgroundTracingManager>
      background_tracing_manager =
          content::BackgroundTracingManager::CreateInstance(&tracing_delegate_);
};

const char kInvalidTracingConfig[] = "{][}";

const char kValidProtoTracingConfig[] = R"pb(
  scenarios: {
    scenario_name: "test_scenario"
    start_rules: { name: "start_trigger" manual_trigger_name: "start_trigger" }
    upload_rules: {
      name: "upload_trigger"
      manual_trigger_name: "upload_trigger"
    }
    trace_config: {
      data_sources: { config: { name: "org.chromium.trace_metadata2" } }
    }
  }
)pb";

const char kValidProtoRuleConfig[] = R"pb(
  rules: { name: "trigger1" manual_trigger_name: "trigger1" }
  rules: { name: "trigger2" manual_trigger_name: "trigger2" }
)pb";

std::string GetFieldTracingConfigFromText(const std::string& proto_text) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::TestProtoLoader config_loader(
      base::PathService::CheckedGet(base::DIR_GEN_TEST_DATA_ROOT)
          .Append(
              FILE_PATH_LITERAL("third_party/perfetto/protos/perfetto/"
                                "config/chrome/scenario_config.descriptor")),
      "perfetto.protos.ChromeFieldTracingConfig");
  std::string serialized_message;
  config_loader.ParseFromText(proto_text, serialized_message);
  return serialized_message;
}

std::string GetTracingRulesConfigFromText(const std::string& proto_text) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::TestProtoLoader config_loader(
      base::PathService::CheckedGet(base::DIR_GEN_TEST_DATA_ROOT)
          .Append(
              FILE_PATH_LITERAL("third_party/perfetto/protos/perfetto/"
                                "config/chrome/scenario_config.descriptor")),
      "perfetto.protos.TracingTriggerRulesConfig");
  std::string serialized_message;
  config_loader.ParseFromText(proto_text, serialized_message);
  return serialized_message;
}

TEST_F(BackgroundTracingUtilsTest, SetupFieldTracingFromFieldTrial) {
  std::string serialized_config =
      GetFieldTracingConfigFromText(kValidProtoTracingConfig);
  std::string compressed_config;
  ASSERT_TRUE(snappy::Compress(serialized_config.data(),
                               serialized_config.size(), &compressed_config));
  std::string encoded_config = base::Base64Encode(compressed_config);
  base::test::ScopedFeatureList scoped_list;
  scoped_list.InitAndEnableFeatureWithParameters(tracing::kFieldTracing,
                                                 {{"config", encoded_config}});

  ASSERT_FALSE(tracing::IsBackgroundTracingEnabledFromCommandLine());
  EXPECT_FALSE(tracing::SetupSystemTracingFromFieldTrial());
  EXPECT_TRUE(tracing::SetupFieldTracingFromFieldTrial());
}

TEST_F(BackgroundTracingUtilsTest, SetupSystemTracingFromFieldTrial) {
  std::string serialized_config =
      GetTracingRulesConfigFromText(kValidProtoRuleConfig);
  std::string compressed_config;
  ASSERT_TRUE(snappy::Compress(serialized_config.data(),
                               serialized_config.size(), &compressed_config));
  std::string encoded_config = base::Base64Encode(compressed_config);
  base::test::ScopedFeatureList scoped_list;
  scoped_list.InitAndEnableFeatureWithParameters(tracing::kTracingTriggers,
                                                 {{"config", encoded_config}});

  ASSERT_FALSE(tracing::IsBackgroundTracingEnabledFromCommandLine());
  EXPECT_TRUE(tracing::SetupSystemTracingFromFieldTrial());
}

TEST_F(BackgroundTracingUtilsTest, SetupBackgroundTracingFromProtoConfigFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_path = temp_dir.GetPath().AppendASCII("config.pb");
  base::WriteFile(file_path,
                  GetFieldTracingConfigFromText(kValidProtoTracingConfig));

  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->AppendSwitchPath(switches::kBackgroundTracingOutputPath,
                                 temp_dir.GetPath());
  command_line->AppendSwitchPath(switches::kEnableBackgroundTracing, file_path);

  ASSERT_TRUE(tracing::IsBackgroundTracingEnabledFromCommandLine());
  EXPECT_FALSE(tracing::SetupSystemTracingFromFieldTrial());
  EXPECT_FALSE(tracing::SetupFieldTracingFromFieldTrial());
  EXPECT_TRUE(tracing::SetupBackgroundTracingFromCommandLine());
}

TEST_F(BackgroundTracingUtilsTest, SetupFieldTracingFromFieldTrialOutputPath) {
  auto pref_service = std::make_unique<TestingPrefServiceSimple>();
  tracing::RegisterPrefs(pref_service->registry());
  auto state_manager_ = tracing::BackgroundTracingStateManager::CreateInstance(
      pref_service.get());

  std::string serialized_config =
      GetFieldTracingConfigFromText(kValidProtoTracingConfig);
  std::string compressed_config;
  ASSERT_TRUE(snappy::Compress(serialized_config.data(),
                               serialized_config.size(), &compressed_config));
  std::string encoded_config = base::Base64Encode(compressed_config);
  base::test::ScopedFeatureList scoped_list;
  scoped_list.InitAndEnableFeatureWithParameters(tracing::kFieldTracing,
                                                 {{"config", encoded_config}});

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->AppendSwitchPath(switches::kBackgroundTracingOutputPath,
                                 temp_dir.GetPath());

  ASSERT_TRUE(tracing::HasBackgroundTracingOutputPath());
  ASSERT_FALSE(tracing::IsBackgroundTracingEnabledFromCommandLine());
  EXPECT_TRUE(tracing::SetupFieldTracingFromFieldTrial());
}

TEST_F(BackgroundTracingUtilsTest,
       SetupBackgroundTracingFromProtoConfigFileFailed) {
  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->AppendSwitchASCII(switches::kEnableBackgroundTracing, "");

  ASSERT_TRUE(tracing::IsBackgroundTracingEnabledFromCommandLine());
  EXPECT_FALSE(
      tracing::SetupBackgroundTracingFromProtoConfigFile(base::FilePath()));
}

TEST_F(BackgroundTracingUtilsTest, SetupBackgroundTracingWithOutputPathFailed) {
  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->AppendSwitchASCII(switches::kBackgroundTracingOutputPath, "");

  EXPECT_TRUE(tracing::HasBackgroundTracingOutputPath());
  EXPECT_FALSE(tracing::SetBackgroundTracingOutputPath());
}

TEST_F(BackgroundTracingUtilsTest,
       SetupBackgroundTracingFromProtoConfigFileInvalidConfig) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath config_file_path = temp_dir.GetPath().AppendASCII("config.pb");
  base::WriteFile(config_file_path, kInvalidTracingConfig);

  base::test::ScopedCommandLine scoped_command_line;
  base::CommandLine* command_line = scoped_command_line.GetProcessCommandLine();
  command_line->AppendSwitchPath(switches::kEnableBackgroundTracing,
                                 config_file_path);

  ASSERT_TRUE(tracing::IsBackgroundTracingEnabledFromCommandLine());

  EXPECT_FALSE(
      tracing::SetupBackgroundTracingFromProtoConfigFile(config_file_path));
}

TEST_F(BackgroundTracingUtilsTest,
       SetupBackgroundTracingFromCommandLineFieldTrial) {
  ASSERT_FALSE(tracing::IsBackgroundTracingEnabledFromCommandLine());
  EXPECT_FALSE(tracing::SetupBackgroundTracingFromCommandLine());
  EXPECT_FALSE(
      content::BackgroundTracingManager::GetInstance().HasActiveScenario());
}

}  // namespace
