// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/lobster/lobster_test_utils.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "base/strings/string_view_util.h"
#include "components/manta/proto/manta.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_unittest_util.h"

namespace {

constexpr int kFakeBaseGenerationSeed = 10;
constexpr char kQueryRewriterTag[] = "use_query_rewrite";
constexpr char kLobsterI18nFlag[] = "use_i18n";

const std::string_view GetTestJpgBytes(const SkBitmap& bitmap) {
  static const base::NoDestructor<std::string> jpg_bytes([&] {
    std::optional<std::vector<uint8_t>> data =
        gfx::JPEGCodec::Encode(bitmap, /*quality=*/50);
    return std::string(base::as_string_view(data.value()));
  }());
  return *jpg_bytes;
}

MATCHER_P(AreJpgBytesClose, expected_bitmap, "") {
  SkBitmap actual_bitmap = gfx::JPEGCodec::Decode(base::as_byte_span(arg));
  // Use `AreBitmapsClose` because JPG encoding/decoding can alter the color
  // slightly.
  return !actual_bitmap.isNull() &&
         gfx::test::AreBitmapsClose(expected_bitmap, actual_bitmap,
                                    /*max_deviation=*/1);
}

}  // namespace

const SkBitmap CreateTestBitmap(int width, int height) {
  return gfx::test::CreateBitmap(width, height, SK_ColorMAGENTA);
}

manta::proto::Request CreateTestMantaRequest(std::string_view query,
                                             std::optional<uint32_t> seed,
                                             const gfx::Size& size,
                                             int num_outputs,
                                             bool use_query_rewriter,
                                             bool use_i18n) {
  manta::proto::Request request;
  manta::proto::RequestConfig& request_config =
      *request.mutable_request_config();
  manta::proto::ImageDimensions& image_dimensions =
      *request_config.mutable_image_dimensions();
  manta::proto::InputData& input_data = *request.add_input_data();

  request.set_feature_name(manta::proto::FeatureName::CHROMEOS_LOBSTER);
  request_config.set_num_outputs(num_outputs);
  input_data.set_text(query.data());
  image_dimensions.set_width(size.width());
  image_dimensions.set_height(size.height());

  if (seed.has_value()) {
    request_config.set_generation_seed(seed.value());
  }

  manta::proto::InputData& query_rewritten_input_data =
      *request.add_input_data();
  query_rewritten_input_data.set_tag(kQueryRewriterTag);
  query_rewritten_input_data.set_text(use_query_rewriter ? "true" : "false");

  manta::proto::InputData& query_i18n_flag_input_data =
      *request.add_input_data();
  query_i18n_flag_input_data.set_tag(kLobsterI18nFlag);
  query_i18n_flag_input_data.set_text(use_i18n ? "true" : "false");

  return request;
}

std::unique_ptr<manta::proto::Response> CreateFakeMantaResponse(
    const std::vector<std::string>& queries_returned_from_server,
    const gfx::Size& image_dimensions) {
  auto response = std::make_unique<manta::proto::Response>();
  for (size_t i = 0; i < queries_returned_from_server.size(); ++i) {
    auto* output_data = response->add_output_data();
    output_data->mutable_image()->set_serialized_bytes(
        std::string(GetTestJpgBytes(CreateTestBitmap(
            image_dimensions.width(), image_dimensions.height()))));
    output_data->set_generation_seed(kFakeBaseGenerationSeed + i);
    output_data->set_generative_prompt(queries_returned_from_server[i]);
  }
  return response;
}

testing::Matcher<ash::LobsterImageCandidate> EqLobsterImageCandidate(
    int expected_id,
    const SkBitmap& expected_bitmap,
    uint32_t expected_generation_seed,
    std::string_view expected_user_query,
    std::string_view expected_rewritten_query) {
  return testing::AllOf(
      testing::Field(&ash::LobsterImageCandidate::id, expected_id),
      testing::Field(&ash::LobsterImageCandidate::image_bytes,
                     AreJpgBytesClose(expected_bitmap)),
      testing::Field(&ash::LobsterImageCandidate::seed,
                     expected_generation_seed),
      testing::Field(&ash::LobsterImageCandidate::user_query,
                     expected_user_query),
      testing::Field(&ash::LobsterImageCandidate::rewritten_query,
                     expected_rewritten_query));
}
