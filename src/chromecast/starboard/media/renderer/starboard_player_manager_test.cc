// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/starboard/media/renderer/starboard_player_manager.h"

#include <array>
#include <vector>

#include "base/task/sequenced_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chromecast/starboard/media/cdm/starboard_drm_wrapper.h"
#include "chromecast/starboard/media/media/mock_starboard_api_wrapper.h"
#include "chromecast/starboard/media/media/starboard_api_wrapper.h"
#include "chromecast/starboard/media/media/test_matchers.h"
#include "media/base/demuxer_stream.h"
#include "media/base/encryption_scheme.h"
#include "media/base/mock_filters.h"
#include "media/base/video_color_space.h"
#include "media/base/video_transformation.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace chromecast {
namespace media {
namespace {

using ::base::test::RunOnceCallback;
using ::media::DemuxerStream;
using ::media::MockDemuxerStream;
using ::media::MockRendererClient;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::DoubleEq;
using ::testing::InSequence;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::WithArg;

// Returns a valid audio config with values arbitrarily set. The values will
// match the values of GetStarboardAudioConfig.
//
// By default, content is unencrypted. An encryption scheme may be specified.
::media::AudioDecoderConfig GetChromiumAudioConfig(
    ::media::EncryptionScheme encryption_scheme =
        ::media::EncryptionScheme::kUnencrypted) {
  return ::media::AudioDecoderConfig(
      ::media::AudioCodec::kAC3, ::media::SampleFormat::kSampleFormatS32,
      ::media::ChannelLayout::CHANNEL_LAYOUT_5_1, 44100, /*extra_data=*/{},
      encryption_scheme);
}

// Returns a valid video config with values arbitrarily set. The values will
// match the values of GetStarboardVideoConfig.
//
// By default, content is unencrypted. An encryption scheme may be specified.
::media::VideoDecoderConfig GetChromiumVideoConfig(
    ::media::EncryptionScheme encryption_scheme =
        ::media::EncryptionScheme::kUnencrypted) {
  ::media::VideoDecoderConfig video_config(
      ::media::VideoCodec::kHEVC, ::media::VideoCodecProfile::HEVCPROFILE_MAIN,
      ::media::VideoDecoderConfig::AlphaMode::kIsOpaque,
      ::media::VideoColorSpace(1, 1, 1, gfx::ColorSpace::RangeID::LIMITED),
      ::media::VideoTransformation(), gfx::Size(1920, 1080),
      gfx::Rect(0, 0, 1919, 1079), gfx::Size(1280, 720), /*extra_data=*/{},
      encryption_scheme);
  video_config.set_level(5);
  return video_config;
}

// Returns a valid starboard audio config with values arbitrarily set. The
// values will match the values of GetChromiumAudioConfig.
StarboardAudioSampleInfo GetStarboardAudioConfig() {
  return StarboardAudioSampleInfo{
      .codec = kStarboardAudioCodecAc3,
      .mime = R"-(audio/mp4; codecs="ac-3")-",
      .format_tag = 0,
      .number_of_channels = 6,
      .samples_per_second = 44100,
      .average_bytes_per_second = (32 / 8) * 44100 * 6,
      .block_alignment = 4,
      .bits_per_sample = 32,
      .audio_specific_config_size = 0,
      .audio_specific_config = nullptr,
  };
}

// Returns a valid starboard video config with values arbitrarily set. The
// values will match the values of GetChromiumVideoConfig.
StarboardVideoSampleInfo GetStarboardVideoConfig() {
  return StarboardVideoSampleInfo{
      .codec = kStarboardVideoCodecH265,
      .mime = R"-(video/mp4; codecs="hev1.1.6.L5.B0")-",
      .max_video_capabilities = "",
      .is_key_frame = false,
      .frame_width = 1920,
      .frame_height = 1080,
      .color_metadata =
          StarboardColorMetadata{
              // These 0 fields signify "unknown" to starboard.
              .bits_per_channel = 0,
              .chroma_subsampling_horizontal = 0,
              .chroma_subsampling_vertical = 0,
              .cb_subsampling_horizontal = 0,
              .cb_subsampling_vertical = 0,
              .chroma_siting_horizontal = 0,
              .chroma_siting_vertical = 0,
              // No HDR metadata, so everything is 0.
              .mastering_metadata = StarboardMediaMasteringMetadata{},
              .max_cll = 0,
              .max_fall = 0,
              .primaries = 1,  // BT.709
              .transfer = 1,   // BT.709
              .matrix = 1,     // BT.709
              .range = 1,      // broadcast range
              .custom_primary_matrix = {0},
          },
  };
}

// A test fixture is used to avoid boilerplate in each test (managing the
// lifetime of the TaskEnvironment, creating mocks, etc).
class StarboardPlayerManagerTest : public ::testing::Test {
 protected:
  StarboardPlayerManagerTest()
      : audio_stream_(DemuxerStream::Type::AUDIO),
        video_stream_(DemuxerStream::Type::VIDEO) {}

  ~StarboardPlayerManagerTest() override = default;

  // This should be destructed last.
  base::test::TaskEnvironment task_environment_;
  NiceMock<MockStarboardApiWrapper> starboard_;
  MockDemuxerStream audio_stream_;
  MockDemuxerStream video_stream_;
  MockRendererClient renderer_client_;

  // Since SbPlayer is used as an opaque void* by cast, we can use any type
  // here. All that matters is the address.
  int sb_player_ = 1;
};

TEST_F(StarboardPlayerManagerTest,
       EnablesBitstreamConvertersForDemuxerStreams) {
  // Starboard requires bitstream formats, so it is important that this be
  // configured properly.
  EXPECT_CALL(audio_stream_, EnableBitstreamConverter);
  EXPECT_CALL(video_stream_, EnableBitstreamConverter);
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  EXPECT_THAT(
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true),
      NotNull());
}

TEST_F(StarboardPlayerManagerTest, PlaybackStartCausesSeekInStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);

  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  EXPECT_CALL(starboard_, SeekTo(&sb_player_, kSeekTime.InMicroseconds(), _))
      .Times(1);

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);
}

TEST_F(StarboardPlayerManagerTest, FlushCausesSeekToCurrentTimeInStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr auto kMediaTime = base::Seconds(12);

  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));
  EXPECT_CALL(starboard_, GetPlayerInfo(&sb_player_, NotNull()))
      .WillOnce(WithArg<1>([kMediaTime](StarboardPlayerInfo* player_info) {
        // player_info cannot be null due to the NotNull matcher, so no need to
        // check for null here.
        *player_info = {};
        player_info->current_media_timestamp_micros =
            kMediaTime.InMicroseconds();
      }));

  {
    InSequence s;
    // There should be two seeks: one when we start playback, and one when we
    // flush (set to the current media time).
    EXPECT_CALL(starboard_, SeekTo(&sb_player_, kSeekTime.InMicroseconds(), _))
        .Times(1);
    EXPECT_CALL(starboard_, SeekTo(&sb_player_, kMediaTime.InMicroseconds(), _))
        .Times(1);
  }

  // Additionally, the playback rate should be set to 0 on flush.
  EXPECT_CALL(starboard_, SetPlaybackRate(&sb_player_, DoubleEq(0)))
      .Times(AtLeast(1));

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);
  player_manager->Flush();
}

TEST_F(StarboardPlayerManagerTest, ForwardsPlaybackRateChangesToStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr double kPlaybackRate = 2.0;

  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  EXPECT_CALL(starboard_, SetPlaybackRate(&sb_player_, DoubleEq(0.0)))
      .Times(AnyNumber());
  EXPECT_CALL(starboard_, SetPlaybackRate(&sb_player_, DoubleEq(kPlaybackRate)))
      .Times(1);

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);
  player_manager->SetPlaybackRate(kPlaybackRate);
}

TEST_F(StarboardPlayerManagerTest, ForwardsStreamVolumeChangesToStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr float kVolume = 0.3;

  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  EXPECT_CALL(starboard_, SetVolume(&sb_player_, DoubleEq(kVolume))).Times(1);

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);
  player_manager->SetVolume(kVolume);
}

TEST_F(StarboardPlayerManagerTest, GetsCurrentMediaTimeFromStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr auto kMediaTime = base::Seconds(11);

  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  EXPECT_CALL(starboard_, GetPlayerInfo(&sb_player_, NotNull()))
      .WillOnce(WithArg<1>([kMediaTime](StarboardPlayerInfo* player_info) {
        // player_info cannot be null due to the NotNull matcher, so no need to
        // check for null here.
        *player_info = {};
        player_info->current_media_timestamp_micros =
            kMediaTime.InMicroseconds();
      }));

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);
  EXPECT_EQ(player_manager->GetMediaTime(), kMediaTime);
}

TEST_F(StarboardPlayerManagerTest, GetSbPlayerReturnsTheSbPlayer) {
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());
  EXPECT_EQ(player_manager->GetSbPlayer(), &sb_player_);
}

TEST_F(StarboardPlayerManagerTest,
       BufferingDisabledSetsStreamingInMaxVideoCapabilities) {
  // streaming=1 is not part of an official starboard API, but cast sets this
  // field to signal to partners that their SbPlayer should prioritize
  // minimizing latency (e.g. for when the user is mirroring to the cast
  // device).
  StarboardVideoSampleInfo sb_video_config = GetStarboardVideoConfig();
  sb_video_config.max_video_capabilities = "streaming=1";

  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = sb_video_config,
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  EXPECT_THAT(
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/false),
      NotNull());
}

TEST_F(StarboardPlayerManagerTest,
       ReadsFromDemuxerStreamsAndWritesBuffersToStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr auto kVideoBufferTs = base::Milliseconds(10001);
  constexpr auto kVideoData = std::to_array<uint8_t>({1, 2, 3, 4, 5});
  constexpr auto kAudioBufferTs = base::Milliseconds(10002);
  constexpr auto kAudioData = std::to_array<uint8_t>({9, 8, 7});

  const StarboardAudioSampleInfo sb_audio_config = GetStarboardAudioConfig();
  const StarboardVideoSampleInfo sb_video_config = GetStarboardVideoConfig();

  // This will be updated whenever the player manager seeks in starboard.
  int seek_ticket = -1;
  ON_CALL(starboard_, SeekTo(&sb_player_, _, _))
      .WillByDefault(SaveArg<2>(&seek_ticket));

  // This will be set to the callbacks received by the mock Starboard.
  const StarboardPlayerCallbackHandler* callbacks = nullptr;
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = sb_audio_config,
              .video_sample_info = sb_video_config,
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(DoAll(SaveArg<1>(&callbacks), Return(&sb_player_)));

  EXPECT_CALL(starboard_, SeekTo(&sb_player_, kSeekTime.InMicroseconds(), _))
      .Times(1);

  // Set expectations for the video buffer.
  scoped_refptr<::media::DecoderBuffer> video_buffer =
      ::media::DecoderBuffer::CopyFrom(kVideoData);
  video_buffer->set_timestamp(kVideoBufferTs);
  const StarboardSampleInfo expected_video_info = {
      .type = 1,
      .buffer = video_buffer->data(),
      .buffer_size = static_cast<int>(video_buffer->size()),
      .timestamp = kVideoBufferTs.InMicroseconds(),
      .side_data = base::span<const StarboardSampleSideData>(),
      .video_sample_info = sb_video_config,
      .drm_info = nullptr,
  };
  EXPECT_CALL(video_stream_, OnRead)
      .WillOnce(RunOnceCallback<0>(
          DemuxerStream::Status::kOk,
          std::vector<scoped_refptr<::media::DecoderBuffer>>({video_buffer})));
  EXPECT_CALL(
      starboard_,
      WriteSample(&sb_player_, StarboardMediaType::kStarboardMediaTypeVideo,
                  ElementsAre(MatchesStarboardSampleInfo(expected_video_info))))
      .Times(1);

  // Set expectations for the audio buffer.
  scoped_refptr<::media::DecoderBuffer> audio_buffer =
      ::media::DecoderBuffer::CopyFrom(kAudioData);
  audio_buffer->set_timestamp(kAudioBufferTs);
  const StarboardSampleInfo expected_audio_info = {
      .type = 0,
      .buffer = audio_buffer->data(),
      .buffer_size = static_cast<int>(audio_buffer->size()),
      .timestamp = kAudioBufferTs.InMicroseconds(),
      .side_data = base::span<const StarboardSampleSideData>(),
      .audio_sample_info = sb_audio_config,
      .drm_info = nullptr,
  };
  EXPECT_CALL(audio_stream_, OnRead)
      .WillOnce(RunOnceCallback<0>(
          DemuxerStream::Status::kOk,
          std::vector<scoped_refptr<::media::DecoderBuffer>>({audio_buffer})));
  EXPECT_CALL(
      starboard_,
      WriteSample(&sb_player_, StarboardMediaType::kStarboardMediaTypeAudio,
                  ElementsAre(MatchesStarboardSampleInfo(expected_audio_info))))
      .Times(1);

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());
  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);

  // Simulate Starboard requesting a video buffer, then an audio buffer. The
  // player manager should read from the video stream and provide that buffer to
  // starboard, then read from the audio stream and provide that buffer to
  // starboard.
  ASSERT_THAT(callbacks, NotNull());
  ASSERT_THAT(callbacks->decoder_status_fn, NotNull());
  ASSERT_THAT(callbacks->context, NotNull());
  callbacks->decoder_status_fn(
      &sb_player_, callbacks->context,
      StarboardMediaType::kStarboardMediaTypeVideo,
      StarboardDecoderState::kStarboardDecoderStateNeedsData, seek_ticket);

  callbacks->decoder_status_fn(
      &sb_player_, callbacks->context,
      StarboardMediaType::kStarboardMediaTypeAudio,
      StarboardDecoderState::kStarboardDecoderStateNeedsData, seek_ticket);
}

TEST_F(StarboardPlayerManagerTest,
       VideoOnlyReadsFromDemuxerStreamAndWritesBufferToStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr auto kVideoBufferTs = base::Milliseconds(10001);
  constexpr auto kVideoData = std::to_array<uint8_t>({1, 2, 3, 4, 5});
  const StarboardVideoSampleInfo sb_video_config = GetStarboardVideoConfig();

  // This will be updated whenever the player manager seeks in starboard.
  int seek_ticket = -1;
  ON_CALL(starboard_, SeekTo(&sb_player_, _, _))
      .WillByDefault(SaveArg<2>(&seek_ticket));

  // This will be set to the callbacks received by the mock Starboard.
  const StarboardPlayerCallbackHandler* callbacks = nullptr;
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = {},
              .video_sample_info = sb_video_config,
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(DoAll(SaveArg<1>(&callbacks), Return(&sb_player_)));

  EXPECT_CALL(starboard_, SeekTo(&sb_player_, kSeekTime.InMicroseconds(), _))
      .Times(1);

  // Set expectations for the video buffer.
  scoped_refptr<::media::DecoderBuffer> video_buffer =
      ::media::DecoderBuffer::CopyFrom(kVideoData);
  video_buffer->set_timestamp(kVideoBufferTs);
  const StarboardSampleInfo expected_video_info = {
      .type = 1,
      .buffer = video_buffer->data(),
      .buffer_size = static_cast<int>(video_buffer->size()),
      .timestamp = kVideoBufferTs.InMicroseconds(),
      .side_data = base::span<const StarboardSampleSideData>(),
      .video_sample_info = sb_video_config,
      .drm_info = nullptr,
  };
  EXPECT_CALL(video_stream_, OnRead)
      .WillOnce(RunOnceCallback<0>(
          DemuxerStream::Status::kOk,
          std::vector<scoped_refptr<::media::DecoderBuffer>>({video_buffer})));
  EXPECT_CALL(
      starboard_,
      WriteSample(&sb_player_, StarboardMediaType::kStarboardMediaTypeVideo,
                  ElementsAre(MatchesStarboardSampleInfo(expected_video_info))))
      .Times(1);

  video_stream_.set_video_decoder_config(GetChromiumVideoConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, /*audio_stream=*/nullptr, &video_stream_,
          &renderer_client_, base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);

  // Simulate Starboard requesting a video buffer. The player manager should
  // read from the video stream and provide that buffer to starboard.
  ASSERT_THAT(callbacks, NotNull());
  ASSERT_THAT(callbacks->decoder_status_fn, NotNull());
  ASSERT_THAT(callbacks->context, NotNull());
  callbacks->decoder_status_fn(
      &sb_player_, callbacks->context,
      StarboardMediaType::kStarboardMediaTypeVideo,
      StarboardDecoderState::kStarboardDecoderStateNeedsData, seek_ticket);
}

TEST_F(StarboardPlayerManagerTest,
       AudioOnlyReadsFromDemuxerStreamAndWritesBufferToStarboard) {
  constexpr auto kSeekTime = base::Seconds(10);
  constexpr auto kAudioBufferTs = base::Milliseconds(10002);
  constexpr auto kAudioData = std::to_array<uint8_t>({9, 8, 7});
  const StarboardAudioSampleInfo sb_audio_config = GetStarboardAudioConfig();

  // This will be updated whenever the player manager seeks in starboard.
  int seek_ticket = -1;
  ON_CALL(starboard_, SeekTo(&sb_player_, _, _))
      .WillByDefault(SaveArg<2>(&seek_ticket));

  // This will be set to the callbacks received by the mock Starboard.
  const StarboardPlayerCallbackHandler* callbacks = nullptr;
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = nullptr,
              .audio_sample_info = sb_audio_config,
              .video_sample_info = {},
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(DoAll(SaveArg<1>(&callbacks), Return(&sb_player_)));

  EXPECT_CALL(starboard_, SeekTo(&sb_player_, kSeekTime.InMicroseconds(), _))
      .Times(1);

  // Set expectations for the audio buffer.
  scoped_refptr<::media::DecoderBuffer> audio_buffer =
      ::media::DecoderBuffer::CopyFrom(kAudioData);
  audio_buffer->set_timestamp(kAudioBufferTs);
  const StarboardSampleInfo expected_audio_info = {
      .type = 0,
      .buffer = audio_buffer->data(),
      .buffer_size = static_cast<int>(audio_buffer->size()),
      .timestamp = kAudioBufferTs.InMicroseconds(),
      .side_data = base::span<const StarboardSampleSideData>(),
      .audio_sample_info = sb_audio_config,
      .drm_info = nullptr,
  };
  EXPECT_CALL(audio_stream_, OnRead)
      .WillOnce(RunOnceCallback<0>(
          DemuxerStream::Status::kOk,
          std::vector<scoped_refptr<::media::DecoderBuffer>>({audio_buffer})));
  EXPECT_CALL(
      starboard_,
      WriteSample(&sb_player_, StarboardMediaType::kStarboardMediaTypeAudio,
                  ElementsAre(MatchesStarboardSampleInfo(expected_audio_info))))
      .Times(1);

  audio_stream_.set_audio_decoder_config(GetChromiumAudioConfig());

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, /*video_stream=*/nullptr,
          &renderer_client_, base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  ASSERT_THAT(player_manager, NotNull());

  player_manager->StartPlayingFrom(kSeekTime);

  // Simulate Starboard requesting a video buffer, then an audio buffer. The
  // player manager should read from the video stream and provide that buffer to
  // starboard, then read from the audio stream and provide that buffer to
  // starboard.
  ASSERT_THAT(callbacks, NotNull());
  ASSERT_THAT(callbacks->decoder_status_fn, NotNull());
  ASSERT_THAT(callbacks->context, NotNull());

  callbacks->decoder_status_fn(
      &sb_player_, callbacks->context,
      StarboardMediaType::kStarboardMediaTypeAudio,
      StarboardDecoderState::kStarboardDecoderStateNeedsData, seek_ticket);
}

TEST_F(StarboardPlayerManagerTest,
       CreatePlayerReturnsNullIfBothDemuxerStreamsAreNull) {
  EXPECT_THAT(
      StarboardPlayerManager::Create(
          &starboard_, /*audio_stream=*/nullptr, /*video_stream=*/nullptr,
          &renderer_client_, base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true),
      IsNull());
}

TEST_F(StarboardPlayerManagerTest, CreatePlayerReturnsNullIfStarboardIsNull) {
  EXPECT_THAT(
      StarboardPlayerManager::Create(
          /*starboard=*/nullptr, &audio_stream_, &video_stream_,
          &renderer_client_, base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true),
      IsNull());
}

TEST_F(StarboardPlayerManagerTest,
       CreatePlayerReturnsNullIfRendererClientIsNull) {
  EXPECT_THAT(
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_,
          /*client=*/nullptr, base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true),
      IsNull());
}

TEST_F(StarboardPlayerManagerTest, CreatePlayerReturnsNullIfTaskRunnerIsNull) {
  EXPECT_THAT(StarboardPlayerManager::Create(&starboard_, &audio_stream_,
                                             &video_stream_, &renderer_client_,
                                             /*media_task_runner=*/nullptr,
                                             /*enable_buffering=*/true),
              IsNull());
}

TEST_F(StarboardPlayerManagerTest, CreatesDrmSystemForEncryptedAudioAndVideo) {
  // SbDrmSystem is an opaque blob to cast, so its actual value does not matter.
  // All that matters is its address (treated as void*).
  int drm_system = 3;
  EXPECT_CALL(starboard_, CreateDrmSystem).WillOnce(Return(&drm_system));
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = &drm_system,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));
  StarboardDrmWrapper::SetSingletonForTesting(&starboard_);

  // Both audio and video streams are encrypted.
  audio_stream_.set_audio_decoder_config(
      GetChromiumAudioConfig(::media::EncryptionScheme::kCenc));
  video_stream_.set_video_decoder_config(
      GetChromiumVideoConfig(::media::EncryptionScheme::kCenc));

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  EXPECT_THAT(player_manager, NotNull());
}

TEST_F(StarboardPlayerManagerTest, CreatesDrmSystemForEncryptedAudio) {
  // SbDrmSystem is an opaque blob to cast, so its actual value does not matter.
  // All that matters is its address (treated as void*).
  int drm_system = 3;
  EXPECT_CALL(starboard_, CreateDrmSystem).WillOnce(Return(&drm_system));
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = &drm_system,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));
  StarboardDrmWrapper::SetSingletonForTesting(&starboard_);

  // Only the audio stream is encrypted.
  audio_stream_.set_audio_decoder_config(
      GetChromiumAudioConfig(::media::EncryptionScheme::kCenc));
  video_stream_.set_video_decoder_config(
      GetChromiumVideoConfig(::media::EncryptionScheme::kUnencrypted));

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  EXPECT_THAT(player_manager, NotNull());
}

TEST_F(StarboardPlayerManagerTest, CreatesDrmSystemForEncryptedVideo) {
  // SbDrmSystem is an opaque blob to cast, so its actual value does not matter.
  // All that matters is its address (treated as void*).
  int drm_system = 3;
  EXPECT_CALL(starboard_, CreateDrmSystem).WillOnce(Return(&drm_system));
  EXPECT_CALL(
      starboard_,
      CreatePlayer(
          Pointee(MatchesPlayerCreationParam(StarboardPlayerCreationParam{
              .drm_system = &drm_system,
              .audio_sample_info = GetStarboardAudioConfig(),
              .video_sample_info = GetStarboardVideoConfig(),
              .output_mode = StarboardPlayerOutputMode::
                  kStarboardPlayerOutputModePunchOut})),
          _))
      .WillOnce(Return(&sb_player_));
  StarboardDrmWrapper::SetSingletonForTesting(&starboard_);

  // Only the video stream is encrypted.
  audio_stream_.set_audio_decoder_config(
      GetChromiumAudioConfig(::media::EncryptionScheme::kUnencrypted));
  video_stream_.set_video_decoder_config(
      GetChromiumVideoConfig(::media::EncryptionScheme::kCenc));

  std::unique_ptr<StarboardPlayerManager> player_manager =
      StarboardPlayerManager::Create(
          &starboard_, &audio_stream_, &video_stream_, &renderer_client_,
          base::SequencedTaskRunner::GetCurrentDefault(),
          /*enable_buffering=*/true);
  EXPECT_THAT(player_manager, NotNull());
}

}  // namespace
}  // namespace media
}  // namespace chromecast
