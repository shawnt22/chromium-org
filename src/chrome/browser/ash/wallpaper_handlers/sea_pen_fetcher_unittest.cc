// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/wallpaper_handlers/sea_pen_fetcher.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/wallpaper/sea_pen_image.h"
#include "ash/webui/common/mojom/sea_pen.mojom.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_view_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/protobuf_matchers.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/ash/wallpaper_handlers/sea_pen_utils.h"
#include "components/manta/features.h"
#include "components/manta/manta_service_callbacks.h"
#include "components/manta/manta_status.h"
#include "components/manta/proto/manta.pb.h"
#include "components/manta/snapper_provider.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/test/test_screen.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/image/image_unittest_util.h"

namespace ash {}
namespace wallpaper_handlers {

namespace {

constexpr uint32_t kFakeGenerationSeed = 5;

constexpr std::string_view kThumbnailsLatencyMetric =
    "Ash.SeaPen.Api.Thumbnails.Latency";
constexpr std::string_view kThumbnailsStatusCodeMetric =
    "Ash.SeaPen.Api.Thumbnails.MantaStatusCode";
constexpr std::string_view kThumbnailsTimeoutMetric =
    "Ash.SeaPen.Api.Thumbnails.Timeout";
constexpr std::string_view kThumbnailsCountMetric =
    "Ash.SeaPen.Api.Thumbnails.Count2";

constexpr std::string_view kWallpaperLatencyMetric =
    "Ash.SeaPen.Api.Wallpaper.Latency";
constexpr std::string_view kWallpaperStatusCodeMetric =
    "Ash.SeaPen.Api.Wallpaper.MantaStatusCode";
constexpr std::string_view kWallpaperTimeoutMetric =
    "Ash.SeaPen.Api.Wallpaper.Timeout";
constexpr std::string_view kWallpaperHasImageMetric =
    "Ash.SeaPen.Api.Wallpaper.HasImage";

constexpr std::string_view kFreeformThumbnailsLatencyMetric =
    "Ash.SeaPen.Freeform.Api.Thumbnails.Latency";
constexpr std::string_view kFreeformThumbnailsStatusCodeMetric =
    "Ash.SeaPen.Freeform.Api.Thumbnails.MantaStatusCode";
constexpr std::string_view kFreeformThumbnailsTimeoutMetric =
    "Ash.SeaPen.Freeform.Api.Thumbnails.Timeout";
constexpr std::string_view kFreeformThumbnailsCountMetric =
    "Ash.SeaPen.Freeform.Api.Thumbnails.Count";

constexpr std::string_view kFreeformWallpaperLatencyMetric =
    "Ash.SeaPen.Freeform.Api.Wallpaper.Latency";
constexpr std::string_view kFreeformWallpaperStatusCodeMetric =
    "Ash.SeaPen.Freeform.Api.Wallpaper.MantaStatusCode";
constexpr std::string_view kFreeformWallpaperTimeoutMetric =
    "Ash.SeaPen.Freeform.Api.Wallpaper.Timeout";
constexpr std::string_view kFreeformWallpaperHasImageMetric =
    "Ash.SeaPen.Freeform.Api.Wallpaper.HasImage";

const SkBitmap CreateTestBitmap() {
  return gfx::test::CreateBitmap(1, SK_ColorMAGENTA);
}

const std::string_view GetJpgBytes() {
  static const base::NoDestructor<std::string> jpg_bytes([] {
    SkBitmap bitmap = CreateTestBitmap();
    std::optional<std::vector<uint8_t>> data =
        gfx::JPEGCodec::Encode(bitmap, /*quality=*/50);
    return std::string(base::as_string_view(data.value()));
  }());
  return *jpg_bytes;
}

ash::personalization_app::mojom::SeaPenQueryPtr MakeTemplateQuery() {
  return ash::personalization_app::mojom::SeaPenQuery::NewTemplateQuery(
      ash::personalization_app::mojom::SeaPenTemplateQuery::New(
          ash::personalization_app::mojom::SeaPenTemplateId::kFlower,
          ::base::flat_map<
              ash::personalization_app::mojom::SeaPenTemplateChip,
              ash::personalization_app::mojom::SeaPenTemplateOption>(
              {{ash::personalization_app::mojom::SeaPenTemplateChip::
                    kFlowerColor,
                ash::personalization_app::mojom::SeaPenTemplateOption::
                    kFlowerColorBlue},
               {ash::personalization_app::mojom::SeaPenTemplateChip::
                    kFlowerType,
                ash::personalization_app::mojom::SeaPenTemplateOption::
                    kFlowerTypeRose}}),
          ash::personalization_app::mojom::SeaPenUserVisibleQuery::New(
              "test template query", "test template title")));
}

ash::personalization_app::mojom::SeaPenQueryPtr MakeFreeformQuery() {
  return ash::personalization_app::mojom ::SeaPenQuery::NewTextQuery(
      "test query");
}

std::unique_ptr<manta::proto::Response> CreateMantaResponse(
    size_t output_data_length) {
  auto response = std::make_unique<manta::proto::Response>();
  for (size_t i = 0; i < output_data_length; i++) {
    auto* output_data = response->add_output_data();
    output_data->set_generation_seed(kFakeGenerationSeed + i);
    output_data->mutable_image()->set_serialized_bytes(
        std::string(GetJpgBytes()));
  }
  return response;
}

std::unique_ptr<manta::proto::Response> CreateMantaResponseWithGenerativePrompt(
    size_t output_data_length,
    const std::string& generative_prompt) {
  auto response = std::make_unique<manta::proto::Response>();
  for (size_t i = 0; i < output_data_length; i++) {
    auto* output_data = response->add_output_data();
    output_data->set_generation_seed(kFakeGenerationSeed + i);
    output_data->mutable_image()->set_serialized_bytes(
        std::string(GetJpgBytes()));
    output_data->set_generative_prompt(generative_prompt);
  }
  return response;
}

std::unique_ptr<manta::proto::Response> CreateMantaResponseWithPersonError() {
  auto response = CreateMantaResponse(0);
  auto* filtered_data = response->add_filtered_data();
  filtered_data->set_reason(manta::proto::FilteredReason::IMAGE_SAFETY);
  filtered_data->add_additional_reasons(
      manta::proto::FilteredReason::IMAGE_SAFETY_PERSON);
  return response;
}

MATCHER_P(AreJpgBytesClose, expected_bitmap, "") {
  SkBitmap actual_bitmap = gfx::JPEGCodec::Decode(base::as_byte_span(arg));
  return !actual_bitmap.isNull() &&
         gfx::test::AreBitmapsClose(expected_bitmap, actual_bitmap,
                                    /*max_deviation=*/1);
}

testing::Matcher<ash::SeaPenImage> MatchesSeaPenImage(
    const SkBitmap& expected_bitmap,
    const uint32_t expected_id,
    const std::string& expected_prompt = "") {
  return testing::AllOf(
      testing::Field(&ash::SeaPenImage::id, expected_id),
      testing::Field(&ash::SeaPenImage::jpg_bytes,
                     AreJpgBytesClose(expected_bitmap)),
      testing::Field(&ash::SeaPenImage::generative_prompt, expected_prompt));
}

class MockSnapperProvider : virtual public manta::SnapperProvider {
 public:
  MockSnapperProvider() : manta::SnapperProvider(nullptr, nullptr) {}

  MockSnapperProvider(const MockSnapperProvider&) = delete;
  MockSnapperProvider& operator=(const MockSnapperProvider&) = delete;

  ~MockSnapperProvider() override = default;

  MOCK_METHOD(void,
              Call,
              (manta::proto::Request& request,
               net::NetworkTrafficAnnotationTag traffic_annotation,
               manta::MantaProtoResponseCallback done_callback),
              (override));
};

}  // namespace

class SeaPenFetcherTest : public testing::Test {
 public:
  SeaPenFetcherTest() {
    scoped_feature_list_.InitWithFeatures(
        {
            ash::features::kFeatureManagementSeaPen,
            manta::features::kMantaService,
        },
        {});
  }

  SeaPenFetcherTest(const SeaPenFetcherTest&) = delete;
  SeaPenFetcherTest& operator=(const SeaPenFetcherTest&) = delete;

  ~SeaPenFetcherTest() override = default;

  void SetUp() override {
    testing::Test::SetUp();
    display::Screen::SetScreenInstance(&test_screen_);
    auto mock_snapper_provider = std::make_unique<MockSnapperProvider>();
    mock_snapper_provider_ =
        static_cast<testing::StrictMock<MockSnapperProvider>*>(
            mock_snapper_provider.get());
    sea_pen_fetcher_ =
        SeaPenFetcher::MakeSeaPenFetcher(std::move(mock_snapper_provider));
  }

  void TearDown() override {
    testing::Test::TearDown();
    display::Screen::SetScreenInstance(nullptr);
    mock_snapper_provider_ = nullptr;
  }

  SeaPenFetcher* sea_pen_fetcher() { return sea_pen_fetcher_.get(); }

  base::HistogramTester& histogram_tester() { return histogram_tester_; }

  testing::StrictMock<MockSnapperProvider>& snapper_provider() {
    return *mock_snapper_provider_;
  }

  void FastForwardBy(const base::TimeDelta delta) {
    task_environment_.FastForwardBy(delta);
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
  base::HistogramTester histogram_tester_;
  display::test::TestScreen test_screen_;
  raw_ptr<testing::StrictMock<MockSnapperProvider>> mock_snapper_provider_;
  std::unique_ptr<SeaPenFetcher> sea_pen_fetcher_;
};

TEST_F(SeaPenFetcherTest, TemplateRequestsFourImages) {
  auto query = MakeTemplateQuery();

  EXPECT_CALL(
      snapper_provider(),
      Call(base::test::EqualsProto(CreateMantaRequest(
               query, /*generation_seed=*/std::nullopt,
               /*num_outputs=*/SeaPenFetcher::kNumTextThumbnailsRequested,
               {880, 440}, manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
           testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(
                               SeaPenFetcher::kNumTextThumbnailsRequested),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;

  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, query,
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kOk,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
}

TEST_F(SeaPenFetcherTest, FreeformThumbnailsCallsSnapperProvider) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {
          ash::features::kFeatureManagementSeaPen,
          manta::features::kMantaService,
          ash::features::kSeaPenTextInput,
      },
      {});
  auto query = MakeFreeformQuery();

  EXPECT_CALL(
      snapper_provider(),
      Call(base::test::EqualsProto(CreateMantaRequest(
               query, /*generation_seed=*/std::nullopt,
               /*num_outputs=*/SeaPenFetcher::kNumTextThumbnailsRequested,
               {880, 440}, manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
           testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(
                               SeaPenFetcher::kNumTextThumbnailsRequested),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;

  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, query,
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kOk,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());

  std::vector<testing::Matcher<ash::SeaPenImage>> matchers;
  for (size_t i = 0; i < SeaPenFetcher::kNumTextThumbnailsRequested; i++) {
    matchers.push_back(
        MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed + i));
  }
  EXPECT_THAT(fetch_thumbnails_future
                  .Get<std::optional<std::vector<ash::SeaPenImage>>>()
                  .value(),
              testing::UnorderedElementsAreArray(matchers));

  histogram_tester().ExpectTotalCount(kFreeformThumbnailsLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kFreeformThumbnailsTimeoutMetric, false,
                                        1);
  histogram_tester().ExpectUniqueSample(
      kFreeformThumbnailsCountMetric,
      SeaPenFetcher::kNumTextThumbnailsRequested, 1);
}

TEST_F(SeaPenFetcherTest, ThumbnailsEmptyReturnsError) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(0),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;
  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeTemplateQuery(),
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kGenericError,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
  EXPECT_EQ(std::nullopt,
            fetch_thumbnails_future
                .Get<std::optional<std::vector<ash::SeaPenImage>>>());

  // Recorded an entry in the "0" thumbnail count bucket 1 time.
  histogram_tester().ExpectUniqueSample(kThumbnailsCountMetric, 0, 1);
  histogram_tester().ExpectTotalCount(kThumbnailsLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kThumbnailsTimeoutMetric, false, 1);
}

TEST_F(SeaPenFetcherTest, FreeformThumbnailsEmptyReturnsBlockedError) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {ash::features::kFeatureManagementSeaPen, manta::features::kMantaService,
       ash::features::kSeaPenTextInput},
      {});
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(0),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;
  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeFreeformQuery(),
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kBlockedOutputs,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
  EXPECT_EQ(std::nullopt,
            fetch_thumbnails_future
                .Get<std::optional<std::vector<ash::SeaPenImage>>>());

  // Recorded an entry in the "0" thumbnail count bucket 1 time.
  histogram_tester().ExpectUniqueSample(kFreeformThumbnailsCountMetric, 0, 1);
  histogram_tester().ExpectTotalCount(kFreeformThumbnailsLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kFreeformThumbnailsTimeoutMetric, false,
                                        1);
}

TEST_F(SeaPenFetcherTest, FreeformThumbnailsEmptyReturnsErrorDueToPerson) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {ash::features::kFeatureManagementSeaPen, manta::features::kMantaService,
       ash::features::kSeaPenTextInput},
      {});
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponseWithPersonError(),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;
  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeFreeformQuery(),
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kImageHasPerson,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
  EXPECT_EQ(std::nullopt,
            fetch_thumbnails_future
                .Get<std::optional<std::vector<ash::SeaPenImage>>>());

  // Recorded an entry in the "0" thumbnail count bucket 1 time.
  histogram_tester().ExpectUniqueSample(kFreeformThumbnailsCountMetric, 0, 1);
  histogram_tester().ExpectTotalCount(kFreeformThumbnailsLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kFreeformThumbnailsTimeoutMetric, false,
                                        1);
}

TEST_F(SeaPenFetcherTest, ThumbnailsTimeoutHandled) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        // Run `done_callback` but one second too late.
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(
                               SeaPenFetcher::kNumTemplateThumbnailsRequested),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)),
            SeaPenFetcher::kRequestTimeout + base::Seconds(1));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;
  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeTemplateQuery(),
      fetch_thumbnails_future.GetCallback());

  // Trigger the timeout.
  FastForwardBy(SeaPenFetcher::kRequestTimeout + base::Milliseconds(1));

  EXPECT_EQ(manta::MantaStatusCode::kGenericError,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
  EXPECT_EQ(std::nullopt,
            fetch_thumbnails_future
                .Get<std::optional<std::vector<ash::SeaPenImage>>>());

  // Recorded 1 timeout.
  histogram_tester().ExpectUniqueSample(kThumbnailsTimeoutMetric, true, 1);

  // Does not record following metrics on timeout.
  histogram_tester().ExpectTotalCount(kThumbnailsLatencyMetric, 0);
  histogram_tester().ExpectTotalCount(kThumbnailsStatusCodeMetric, 0);
  histogram_tester().ExpectTotalCount(kThumbnailsCountMetric, 0);
}

TEST_F(SeaPenFetcherTest, FreeformThumbnailsTimeoutHandled) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        // Run `done_callback` but one second too late.
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(
                               SeaPenFetcher::kNumTemplateThumbnailsRequested),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)),
            SeaPenFetcher::kRequestTimeout + base::Seconds(1));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;
  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeFreeformQuery(),
      fetch_thumbnails_future.GetCallback());

  // Trigger the timeout.
  FastForwardBy(SeaPenFetcher::kRequestTimeout + base::Milliseconds(1));

  EXPECT_EQ(manta::MantaStatusCode::kGenericError,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
  EXPECT_EQ(std::nullopt,
            fetch_thumbnails_future
                .Get<std::optional<std::vector<ash::SeaPenImage>>>());

  // Recorded 1 timeout.
  histogram_tester().ExpectUniqueSample(kFreeformThumbnailsTimeoutMetric, true,
                                        1);

  // Does not record following metrics on timeout.
  histogram_tester().ExpectTotalCount(kFreeformThumbnailsLatencyMetric, 0);
  histogram_tester().ExpectTotalCount(kFreeformThumbnailsStatusCodeMetric, 0);
  histogram_tester().ExpectTotalCount(kFreeformThumbnailsCountMetric, 0);
}

TEST_F(SeaPenFetcherTest, ThumbnailsHandlesDuplicateRequests) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillRepeatedly([](const manta::proto::Request& request,
                         net::NetworkTrafficAnnotationTag traffic_annotation,
                         manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(
                               SeaPenFetcher::kNumTemplateThumbnailsRequested),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)),
            SeaPenFetcher::kRequestTimeout / 2);
      });

  std::vector<base::test::TestFuture<
      std::optional<std::vector<ash::SeaPenImage>>, manta::MantaStatusCode>>
      fetch_thumbnails_futures(2);

  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeFreeformQuery(),
      fetch_thumbnails_futures.at(0).GetCallback());

  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeFreeformQuery(),
      fetch_thumbnails_futures.at(1).GetCallback());

  // First call has already returned with null images.
  EXPECT_EQ(manta::MantaStatusCode::kOk,
            fetch_thumbnails_futures.at(0).Get<manta::MantaStatusCode>());
  EXPECT_EQ(std::nullopt,
            fetch_thumbnails_futures.at(0)
                .Get<std::optional<std::vector<ash::SeaPenImage>>>());

  EXPECT_FALSE(fetch_thumbnails_futures.at(1).IsReady());

  FastForwardBy(SeaPenFetcher::kRequestTimeout / 2 + base::Milliseconds(1));

  // Second call returns with valid thumbnails.
  EXPECT_TRUE(fetch_thumbnails_futures.at(1).IsReady());
  EXPECT_EQ(manta::MantaStatusCode::kOk,
            fetch_thumbnails_futures.at(1).Get<manta::MantaStatusCode>());
  EXPECT_EQ(SeaPenFetcher::kNumTemplateThumbnailsRequested,
            fetch_thumbnails_futures.at(1)
                .Get<std::optional<std::vector<ash::SeaPenImage>>>()
                ->size());
}

TEST_F(SeaPenFetcherTest, ThumbnailsDropsInvalidJpgBytes) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback inner_callback) {
                  auto response = std::make_unique<manta::proto::Response>();
                  {
                    // Invalid jpg bytes.
                    auto* output_data = response->add_output_data();
                    output_data->set_generation_seed(kFakeGenerationSeed + 1);
                    output_data->mutable_image()->set_serialized_bytes(
                        "not real jpg bytes");
                  }
                  {
                    // Valid jpg bytes.
                    auto* output_data = response->add_output_data();
                    output_data->set_generation_seed(kFakeGenerationSeed);
                    output_data->mutable_image()->set_serialized_bytes(
                        std::string(GetJpgBytes()));
                  }
                  std::move(inner_callback)
                      .Run(std::move(response),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });
  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;

  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, MakeFreeformQuery(),
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kOk,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());
  // Only 1 image made it. The other was dropped due to invalid jpg bytes that
  // failed decoding.
  EXPECT_EQ(1u, fetch_thumbnails_future
                    .Get<std::optional<std::vector<ash::SeaPenImage>>>()
                    ->size());
}

TEST_F(SeaPenFetcherTest, WallpaperCallsSnapperProvider) {
  auto query = MakeTemplateQuery();

  EXPECT_CALL(snapper_provider(),
              Call(base::test::EqualsProto(CreateMantaRequest(
                       query, /*generation_seed=*/kFakeGenerationSeed,
                       /*num_outputs=*/1, GetLargestDisplaySizeLandscape(),
                       manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
                   testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed), query,
      fetch_wallpaper_future.GetCallback());

  EXPECT_THAT(fetch_wallpaper_future.Get().value(),
              MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed));

  histogram_tester().ExpectTotalCount(kWallpaperLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kWallpaperStatusCodeMetric,
                                        manta::MantaStatusCode::kOk, 1);
  histogram_tester().ExpectUniqueSample(kWallpaperTimeoutMetric, false, 1);
  histogram_tester().ExpectUniqueSample(kWallpaperHasImageMetric, true, 1);
}

TEST_F(SeaPenFetcherTest, FreeformWallpaperCallsSnapperProvider) {
  auto query = MakeFreeformQuery();

  EXPECT_CALL(snapper_provider(),
              Call(base::test::EqualsProto(CreateMantaRequest(
                       query, /*generation_seed=*/kFakeGenerationSeed,
                       /*num_outputs=*/1, GetLargestDisplaySizeLandscape(),
                       manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
                   testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed), query,
      fetch_wallpaper_future.GetCallback());

  EXPECT_THAT(fetch_wallpaper_future.Get().value(),
              MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed));

  histogram_tester().ExpectTotalCount(kFreeformWallpaperLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperStatusCodeMetric,
                                        manta::MantaStatusCode::kOk, 1);
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperTimeoutMetric, false,
                                        1);
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperHasImageMetric, true,
                                        1);
}

TEST_F(SeaPenFetcherTest, WallpaperHandlesEmptyImage) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(0),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed),
      MakeTemplateQuery(), fetch_wallpaper_future.GetCallback());

  EXPECT_FALSE(fetch_wallpaper_future.Get().has_value());

  histogram_tester().ExpectTotalCount(kWallpaperLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kWallpaperStatusCodeMetric,
                                        manta::MantaStatusCode::kOk, 1);
  histogram_tester().ExpectUniqueSample(kWallpaperTimeoutMetric, false, 1);
  histogram_tester().ExpectUniqueSample(kWallpaperHasImageMetric, false, 1);
}

TEST_F(SeaPenFetcherTest, FreeformWallpaperHandlesEmptyImage) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(0),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed),
      MakeFreeformQuery(), fetch_wallpaper_future.GetCallback());

  EXPECT_FALSE(fetch_wallpaper_future.Get().has_value());

  histogram_tester().ExpectTotalCount(kFreeformWallpaperLatencyMetric, 1);
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperStatusCodeMetric,
                                        manta::MantaStatusCode::kOk, 1);
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperTimeoutMetric, false,
                                        1);
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperHasImageMetric, false,
                                        1);
}

TEST_F(SeaPenFetcherTest, WallpaperHandlesTimeout) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)),
            SeaPenFetcher::kRequestTimeout + base::Seconds(1));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed),
      MakeTemplateQuery(), fetch_wallpaper_future.GetCallback());

  FastForwardBy(SeaPenFetcher::kRequestTimeout + base::Milliseconds(1));

  EXPECT_FALSE(fetch_wallpaper_future.Get().has_value());

  // Timeout metric records true.
  histogram_tester().ExpectUniqueSample(kWallpaperTimeoutMetric, true, 1);

  // No other metrics recorded for timeout.
  histogram_tester().ExpectTotalCount(kWallpaperLatencyMetric, 0);
  histogram_tester().ExpectTotalCount(kWallpaperStatusCodeMetric, 0);
  histogram_tester().ExpectTotalCount(kWallpaperHasImageMetric, 0);
}

TEST_F(SeaPenFetcherTest, FreeformWallpaperHandlesTimeout) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)),
            SeaPenFetcher::kRequestTimeout + base::Seconds(1));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed),
      MakeFreeformQuery(), fetch_wallpaper_future.GetCallback());

  FastForwardBy(SeaPenFetcher::kRequestTimeout + base::Milliseconds(1));

  EXPECT_FALSE(fetch_wallpaper_future.Get().has_value());

  // Timeout metric records true.
  histogram_tester().ExpectUniqueSample(kFreeformWallpaperTimeoutMetric, true,
                                        1);

  // No other metrics recorded for timeout.
  histogram_tester().ExpectTotalCount(kFreeformWallpaperLatencyMetric, 0);
  histogram_tester().ExpectTotalCount(kFreeformWallpaperStatusCodeMetric, 0);
  histogram_tester().ExpectTotalCount(kFreeformWallpaperHasImageMetric, 0);
}

TEST_F(SeaPenFetcherTest, WallpaperDropsInvalidJpgBytes) {
  EXPECT_CALL(snapper_provider(), Call(testing::_, testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback inner_callback) {
                  auto response = std::make_unique<manta::proto::Response>();
                  {
                    // Invalid jpg bytes.
                    auto* output_data = response->add_output_data();
                    output_data->set_generation_seed(kFakeGenerationSeed + 1);
                    output_data->mutable_image()->set_serialized_bytes(
                        "not real jpg bytes");
                  }
                  std::move(inner_callback)
                      .Run(std::move(response),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });
  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;

  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed + 1),
      MakeFreeformQuery(), fetch_wallpaper_future.GetCallback());

  // The image was dropped due to invalid jpg bytes that
  // failed decoding.
  EXPECT_EQ(std::nullopt,
            fetch_wallpaper_future.Get<std::optional<ash::SeaPenImage>>());
}

TEST_F(SeaPenFetcherTest, FreeformThumbnails_StoresGenerativePrompts) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {
          ash::features::kFeatureManagementSeaPen,
          manta::features::kMantaService,
          ash::features::kSeaPenTextInput,
      },
      {});
  auto query = MakeFreeformQuery();
  static std::string generative_prompt = "prompt used to generate images";

  EXPECT_CALL(
      snapper_provider(),
      Call(base::test::EqualsProto(CreateMantaRequest(
               query, /*generation_seed=*/std::nullopt,
               /*num_outputs=*/SeaPenFetcher::kNumTextThumbnailsRequested,
               {880, 440}, manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
           testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponseWithGenerativePrompt(
                               SeaPenFetcher::kNumTextThumbnailsRequested,
                               generative_prompt),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<std::vector<ash::SeaPenImage>>,
                         manta::MantaStatusCode>
      fetch_thumbnails_future;

  sea_pen_fetcher()->FetchThumbnails(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER, query,
      fetch_thumbnails_future.GetCallback());

  EXPECT_EQ(manta::MantaStatusCode::kOk,
            fetch_thumbnails_future.Get<manta::MantaStatusCode>());

  std::vector<testing::Matcher<ash::SeaPenImage>> matchers;
  for (size_t i = 0; i < SeaPenFetcher::kNumTextThumbnailsRequested; i++) {
    matchers.push_back(MatchesSeaPenImage(
        CreateTestBitmap(), kFakeGenerationSeed + i, generative_prompt));
  }
  EXPECT_THAT(fetch_thumbnails_future
                  .Get<std::optional<std::vector<ash::SeaPenImage>>>()
                  .value(),
              testing::UnorderedElementsAreArray(matchers));
}

TEST_F(SeaPenFetcherTest, FetchFreeformWallpaper_ExperimentOff_UsesUserPrompt) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {
          ash::features::kFeatureManagementSeaPen,
          manta::features::kMantaService,
          ash::features::kSeaPenTextInput,
      },
      {
          ash::features::kSeaPenQueryRewrite,
      });
  auto user_query = MakeFreeformQuery();
  std::string generative_prompt = "prompt used to generate images";
  ash::personalization_app::mojom::SeaPenQueryPtr generative_prompt_query =
      ash::personalization_app::mojom ::SeaPenQuery::NewTextQuery(
          generative_prompt);

  EXPECT_CALL(snapper_provider(),
              Call(base::test::EqualsProto(CreateMantaRequest(
                       user_query, /*generation_seed=*/kFakeGenerationSeed,
                       /*num_outputs=*/1, GetLargestDisplaySizeLandscape(),
                       manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
                   testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed,
                       generative_prompt),
      user_query, fetch_wallpaper_future.GetCallback());

  EXPECT_THAT(fetch_wallpaper_future.Get().value(),
              MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed));
}

TEST_F(SeaPenFetcherTest, FetchFreeformWallpaper_UsesGenerativePrompt) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {
          ash::features::kFeatureManagementSeaPen,
          manta::features::kMantaService,
          ash::features::kSeaPenTextInput,
          ash::features::kSeaPenQueryRewrite,
      },
      {});
  auto user_query = MakeFreeformQuery();
  std::string generative_prompt = "prompt used to generate images";
  ash::personalization_app::mojom::SeaPenQueryPtr generative_prompt_query =
      ash::personalization_app::mojom ::SeaPenQuery::NewTextQuery(
          generative_prompt);

  EXPECT_CALL(
      snapper_provider(),
      Call(base::test::EqualsProto(CreateMantaRequest(
               generative_prompt_query, /*generation_seed=*/kFakeGenerationSeed,
               /*num_outputs=*/1, GetLargestDisplaySizeLandscape(),
               manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
           testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed,
                       generative_prompt),
      user_query, fetch_wallpaper_future.GetCallback());

  EXPECT_THAT(fetch_wallpaper_future.Get().value(),
              MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed));
}

TEST_F(SeaPenFetcherTest,
       FetchFreeformWallpaper_NoGenerativePrompt_UsesUserPrompt) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {
          ash::features::kFeatureManagementSeaPen,
          manta::features::kMantaService,
          ash::features::kSeaPenTextInput,
          ash::features::kSeaPenQueryRewrite,
      },
      {});
  auto user_query = MakeFreeformQuery();

  EXPECT_CALL(snapper_provider(),
              Call(base::test::EqualsProto(CreateMantaRequest(
                       user_query, /*generation_seed=*/kFakeGenerationSeed,
                       /*num_outputs=*/1, GetLargestDisplaySizeLandscape(),
                       manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
                   testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed),
      user_query, fetch_wallpaper_future.GetCallback());

  EXPECT_THAT(fetch_wallpaper_future.Get().value(),
              MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed));
}

TEST_F(SeaPenFetcherTest, FetchTemplateWallpaper_UsesTemplate) {
  scoped_feature_list_.Reset();
  scoped_feature_list_.InitWithFeatures(
      {
          ash::features::kFeatureManagementSeaPen,
          manta::features::kMantaService,
          ash::features::kSeaPenTextInput,
          ash::features::kSeaPenQueryRewrite,
      },
      {});
  auto template_query = MakeTemplateQuery();
  std::string generative_prompt = "prompt used to generate images";

  EXPECT_CALL(snapper_provider(),
              Call(base::test::EqualsProto(CreateMantaRequest(
                       template_query, /*generation_seed=*/kFakeGenerationSeed,
                       /*num_outputs=*/1, GetLargestDisplaySizeLandscape(),
                       manta::proto::FeatureName::CHROMEOS_WALLPAPER)),
                   testing::_, testing::_))
      .WillOnce([](const manta::proto::Request& request,
                   net::NetworkTrafficAnnotationTag traffic_annotation,
                   manta::MantaProtoResponseCallback done_callback) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](manta::MantaProtoResponseCallback delayed_callback) {
                  std::move(delayed_callback)
                      .Run(CreateMantaResponse(1),
                           {.status_code = manta::MantaStatusCode::kOk,
                            .message = std::string()});
                },
                std::move(done_callback)));
      });

  base::test::TestFuture<std::optional<ash::SeaPenImage>>
      fetch_wallpaper_future;
  sea_pen_fetcher()->FetchWallpaper(
      manta::proto::FeatureName::CHROMEOS_WALLPAPER,
      ash::SeaPenImage(std::string(GetJpgBytes()), kFakeGenerationSeed,
                       generative_prompt),
      template_query, fetch_wallpaper_future.GetCallback());

  EXPECT_THAT(fetch_wallpaper_future.Get().value(),
              MatchesSeaPenImage(CreateTestBitmap(), kFakeGenerationSeed));
}

}  // namespace wallpaper_handlers
