// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/metrics/histogram_shared_memory_config.h"

#include <string_view>

#include "base/metrics/histogram_shared_memory.h"
#include "base/test/scoped_feature_list.h"
#include "content/public/common/process_type.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

using Config = base::HistogramSharedMemory::Config;
using base::HistogramSharedMemory;

struct ProcessTypeToOptionalConfig {
  int process_type;
  std::optional<Config> expected;

  ProcessTypeToOptionalConfig(int type, std::nullopt_t)
      : process_type(type), expected(std::nullopt) {}

  ProcessTypeToOptionalConfig(int type, std::string_view name, size_t size)
      : process_type(type), expected(Config{type, name, size}) {}
};

using HistogramSharedMemoryConfigTest =
    testing::TestWithParam<ProcessTypeToOptionalConfig>;

}  // namespace

TEST(HistogramSharedMemoryConfigTest, PassOnCommandLineIsDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(base::kPassHistogramSharedMemoryOnLaunch);

  EXPECT_FALSE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_RENDERER));
  EXPECT_FALSE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_GPU));
  EXPECT_FALSE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_UTILITY));
}

TEST(HistogramSharedMemoryConfigTest, PassOnCommandLineIsEnabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(base::kPassHistogramSharedMemoryOnLaunch);

  EXPECT_TRUE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_RENDERER));

#if BUILDFLAG(IS_CHROMEOS)
  EXPECT_FALSE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_GPU));
#else   // !BUILDFLAG(IS_CHROMEOS)
  EXPECT_TRUE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_GPU));
#endif  // !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
  EXPECT_FALSE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_UTILITY));
#else   // !BUILDFLAG(IS_ANDROID)
  EXPECT_TRUE(
      HistogramSharedMemory::PassOnCommandLineIsEnabled(PROCESS_TYPE_UTILITY));
#endif  // !BUILDFLAG(IS_ANDROID)
}

TEST_P(HistogramSharedMemoryConfigTest, GetHistogramSharedMemoryConfig) {
  const auto& process_type = GetParam().process_type;
  const auto& expected = GetParam().expected;

  const auto config = GetHistogramSharedMemoryConfig(process_type);
  ASSERT_EQ(config.has_value(), expected.has_value());
  if (config.has_value()) {
    EXPECT_EQ(config->process_type, process_type);
    EXPECT_EQ(config->allocator_name, expected->allocator_name);
    EXPECT_EQ(config->memory_size_bytes, expected->memory_size_bytes);
  }
}

INSTANTIATE_TEST_SUITE_P(
    All,
    HistogramSharedMemoryConfigTest,
    testing::ValuesIn(std::vector<ProcessTypeToOptionalConfig>({
        {PROCESS_TYPE_UNKNOWN, std::nullopt},
        {PROCESS_TYPE_BROWSER, std::nullopt},
        {PROCESS_TYPE_RENDERER, "RendererMetrics", 2 << 20},
        {PROCESS_TYPE_PLUGIN_DEPRECATED, std::nullopt},
        {PROCESS_TYPE_WORKER_DEPRECATED, std::nullopt},
        {PROCESS_TYPE_UTILITY, "UtilityMetrics", 512 << 10},
        {PROCESS_TYPE_ZYGOTE, "ZygoteMetrics", 64 << 10},
        {PROCESS_TYPE_SANDBOX_HELPER, "SandboxHelperMetrics", 64 << 10},
        {PROCESS_TYPE_GPU, "GpuMetrics", 256 << 10},
    })));

}  // namespace content
