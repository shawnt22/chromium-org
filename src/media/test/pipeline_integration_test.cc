// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "media/base/cdm_callback_promise.h"
#include "media/base/cdm_key_information.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media.h"
#include "media/base/media_switches.h"
#include "media/base/media_tracks.h"
#include "media/base/mock_media_log.h"
#include "media/base/supported_types.h"
#include "media/base/test_data_util.h"
#include "media/base/timestamp_constants.h"
#include "media/cdm/aes_decryptor.h"
#include "media/cdm/json_web_key.h"
#include "media/media_buildflags.h"
#include "media/renderers/renderer_impl.h"
#include "media/test/fake_encrypted_media.h"
#include "media/test/pipeline_integration_test_base.h"
#include "media/test/test_media_source.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/abseil-cpp/absl/strings/ascii.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_ANDROID)
#include "media/filters/android/media_codec_audio_decoder.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "media/filters/mac/audio_toolbox_audio_decoder.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "media/filters/win/media_foundation_audio_decoder.h"
#endif

#define EXPECT_AUDIO_HASH(expected)                        \
  EXPECT_TRUE(GetAudioHash().IsEquivalent(expected, 0.04)) \
      << "Audio hashes differ. Expected: " << expected     \
      << " Actual: " << GetAudioHash().ToString()

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::HasSubstr;
using ::testing::SaveArg;

namespace media {

#if BUILDFLAG(ENABLE_AV1_DECODER)
constexpr int kAV110bitMp4FileDurationMs = 2735;
constexpr int kAV1640WebMFileDurationMs = 2736;
#endif  // BUILDFLAG(ENABLE_AV1_DECODER)

// Constants for the Media Source config change tests.
constexpr int kAppendTimeSec = 1;
constexpr int kAppendTimeMs = kAppendTimeSec * 1000;
constexpr int k320WebMFileDurationMs = 2736;
constexpr int k640WebMFileDurationMs = 2762;
constexpr int kVP9WebMFileDurationMs = 2736;
constexpr int kVP8AWebMFileDurationMs = 2733;

constexpr char kSfxLosslessHash[] = "3.03,2.86,2.99,3.31,3.57,4.06,";

// Hash for a full playthrough of "opus-trimming-test.(webm|ogg)".
constexpr char kOpusEndTrimmingHash_1[] =
    "-4.57,-5.67,-6.52,-6.28,-4.34,-3.58,";

// The above hash, plus an additional playthrough starting from T=1s.
constexpr char kOpusEndTrimmingHash_2[] =
    "-11.91,-11.10,-8.24,-7.08,-7.82,-9.99,";

// The above hash, plus an additional playthrough starting from T=6.36s.
constexpr char kOpusEndTrimmingHash_3[] =
    "-13.31,-14.36,-13.66,-11.65,-10.16,-10.47,";

// Hash for a full playthrough of "bear-opus.webm".
constexpr char kOpusSmallCodecDelayHash_1[] =
    "-0.47,-0.09,1.28,1.07,1.55,-0.22,";

// The above hash, plus an additional playthrough starting from T=1.414s.
constexpr char kOpusSmallCodecDelayHash_2[] = "0.31,0.15,-0.18,0.25,0.70,0.84,";

#if BUILDFLAG(USE_PROPRIETARY_CODECS) && BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
constexpr int k1280IsoFileDurationMs = 2736;

constexpr int k1280IsoAVC3FileDurationMs = 2736;
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)

// Return a timeline offset for bear-320x240-live.webm.
static base::Time kLiveTimelineOffset() {
  // The file contains the following UTC timeline offset:
  // 2012-11-10 12:34:56.789123456
  // Since base::Time only has a resolution of microseconds,
  // construct a base::Time for 2012-11-10 12:34:56.789123.
  static constexpr base::Time::Exploded kExplodedTime = {.year = 2012,
                                                         .month = 11,
                                                         .day_of_week = 6,
                                                         .day_of_month = 10,
                                                         .hour = 12,
                                                         .minute = 34,
                                                         .second = 56,
                                                         .millisecond = 789};
  base::Time timeline_offset;
  EXPECT_TRUE(base::Time::FromUTCExploded(kExplodedTime, &timeline_offset));

  timeline_offset += base::Microseconds(123);

  return timeline_offset;
}

#if BUILDFLAG(IS_MAC)
class ScopedVerboseLogEnabler {
 public:
  ScopedVerboseLogEnabler() : old_level_(logging::GetMinLogLevel()) {
    logging::SetMinLogLevel(-1);
  }

  ScopedVerboseLogEnabler(const ScopedVerboseLogEnabler&) = delete;
  ScopedVerboseLogEnabler& operator=(const ScopedVerboseLogEnabler&) = delete;

  ~ScopedVerboseLogEnabler() { logging::SetMinLogLevel(old_level_); }

 private:
  const int old_level_;
};
#endif

enum PromiseResult { RESOLVED, REJECTED };

// Provides the test key in response to the encrypted event.
class KeyProvidingApp : public FakeEncryptedMedia::AppBase {
 public:
  KeyProvidingApp() = default;

  void OnResolveWithSession(PromiseResult expected,
                            const std::string& session_id) {
    EXPECT_EQ(expected, RESOLVED);
    EXPECT_GT(session_id.length(), 0ul);
    current_session_id_ = session_id;
  }

  void OnResolve(PromiseResult expected) { EXPECT_EQ(expected, RESOLVED); }

  void OnReject(PromiseResult expected,
                media::CdmPromise::Exception exception_code,
                uint32_t system_code,
                const std::string& error_message) {
    EXPECT_EQ(expected, REJECTED) << error_message;
  }

  std::unique_ptr<SimpleCdmPromise> CreatePromise(PromiseResult expected) {
    auto promise = std::make_unique<media::CdmCallbackPromise<>>(
        base::BindOnce(&KeyProvidingApp::OnResolve, base::Unretained(this),
                       expected),
        base::BindOnce(&KeyProvidingApp::OnReject, base::Unretained(this),
                       expected));
    return promise;
  }

  std::unique_ptr<NewSessionCdmPromise> CreateSessionPromise(
      PromiseResult expected) {
    auto promise = std::make_unique<media::CdmCallbackPromise<std::string>>(
        base::BindOnce(&KeyProvidingApp::OnResolveWithSession,
                       base::Unretained(this), expected),
        base::BindOnce(&KeyProvidingApp::OnReject, base::Unretained(this),
                       expected));
    return promise;
  }

  void OnSessionMessage(const std::string& session_id,
                        CdmMessageType message_type,
                        const std::vector<uint8_t>& message,
                        AesDecryptor* decryptor) override {
    EXPECT_FALSE(session_id.empty());
    EXPECT_FALSE(message.empty());
    EXPECT_EQ(current_session_id_, session_id);
    EXPECT_EQ(CdmMessageType::LICENSE_REQUEST, message_type);

    // Extract the key ID from |message|. For Clear Key this is a JSON object
    // containing a set of "kids". There should only be 1 key ID in |message|.
    std::string message_string(message.begin(), message.end());
    KeyIdList key_ids;
    std::string error_message;
    EXPECT_TRUE(ExtractKeyIdsFromKeyIdsInitData(message_string, &key_ids,
                                                &error_message))
        << error_message;
    EXPECT_EQ(1u, key_ids.size());

    // Determine the key that matches the key ID |key_ids[0]|.
    std::vector<uint8_t> key;
    EXPECT_TRUE(LookupKey(key_ids[0], &key));

    // Update the session with the key ID and key.
    std::string jwk = GenerateJWKSet(key, key_ids[0]);
    decryptor->UpdateSession(session_id,
                             std::vector<uint8_t>(jwk.begin(), jwk.end()),
                             CreatePromise(RESOLVED));
  }

  void OnSessionClosed(const std::string& session_id,
                       CdmSessionClosedReason /*reason*/) override {
    EXPECT_EQ(current_session_id_, session_id);
  }

  void OnSessionKeysChange(const std::string& session_id,
                           bool has_additional_usable_key,
                           CdmKeysInfo keys_info) override {
    EXPECT_EQ(current_session_id_, session_id);
    EXPECT_EQ(has_additional_usable_key, true);
  }

  void OnSessionExpirationUpdate(const std::string& session_id,
                                 base::Time new_expiry_time) override {
    EXPECT_EQ(current_session_id_, session_id);
  }

  void OnEncryptedMediaInitData(EmeInitDataType init_data_type,
                                const std::vector<uint8_t>& init_data,
                                AesDecryptor* decryptor) override {
    // Since only 1 session is created, skip the request if the |init_data|
    // has been seen before (no need to add the same key again).
    if (init_data == prev_init_data_)
      return;
    prev_init_data_ = init_data;

    if (current_session_id_.empty()) {
      decryptor->CreateSessionAndGenerateRequest(
          CdmSessionType::kTemporary, init_data_type, init_data,
          CreateSessionPromise(RESOLVED));
      EXPECT_FALSE(current_session_id_.empty());
    }
  }

  virtual bool LookupKey(const std::vector<uint8_t>& key_id,
                         std::vector<uint8_t>* key) {
    // No key rotation.
    return LookupTestKeyVector(key_id, false, key);
  }

  std::string current_session_id_;
  std::vector<uint8_t> prev_init_data_;
};

class RotatingKeyProvidingApp : public KeyProvidingApp {
 public:
  RotatingKeyProvidingApp() : num_distinct_need_key_calls_(0) {}
  ~RotatingKeyProvidingApp() override {
    // Expect that OnEncryptedMediaInitData is fired multiple times with
    // different |init_data|.
    EXPECT_GT(num_distinct_need_key_calls_, 1u);
  }

  void OnEncryptedMediaInitData(EmeInitDataType init_data_type,
                                const std::vector<uint8_t>& init_data,
                                AesDecryptor* decryptor) override {
    // Skip the request if the |init_data| has been seen.
    if (init_data == prev_init_data_)
      return;
    prev_init_data_ = init_data;
    ++num_distinct_need_key_calls_;

    decryptor->CreateSessionAndGenerateRequest(CdmSessionType::kTemporary,
                                               init_data_type, init_data,
                                               CreateSessionPromise(RESOLVED));
  }

  bool LookupKey(const std::vector<uint8_t>& key_id,
                 std::vector<uint8_t>* key) override {
    // With key rotation.
    return LookupTestKeyVector(key_id, true, key);
  }

  uint32_t num_distinct_need_key_calls_;
};

// Ignores the encrypted event and does not perform a license request.
class NoResponseApp : public FakeEncryptedMedia::AppBase {
 public:
  void OnSessionMessage(const std::string& session_id,
                        CdmMessageType message_type,
                        const std::vector<uint8_t>& message,
                        AesDecryptor* decryptor) override {
    EXPECT_FALSE(session_id.empty());
    EXPECT_FALSE(message.empty());
    FAIL() << "Unexpected Message";
  }

  void OnSessionClosed(const std::string& session_id,
                       CdmSessionClosedReason /*reason*/) override {
    EXPECT_FALSE(session_id.empty());
    FAIL() << "Unexpected Closed";
  }

  void OnSessionKeysChange(const std::string& session_id,
                           bool has_additional_usable_key,
                           CdmKeysInfo keys_info) override {
    EXPECT_FALSE(session_id.empty());
    EXPECT_EQ(has_additional_usable_key, true);
  }

  void OnSessionExpirationUpdate(const std::string& session_id,
                                 base::Time new_expiry_time) override {}

  void OnEncryptedMediaInitData(EmeInitDataType init_data_type,
                                const std::vector<uint8_t>& init_data,
                                AesDecryptor* decryptor) override {}
};

// A rough simulation of GpuVideoDecoder that fails every Decode() request. This
// is used to test post-Initialize() fallback paths.
class FailingVideoDecoder : public VideoDecoder {
 public:
  VideoDecoderType GetDecoderType() const override {
    return VideoDecoderType::kTesting;
  }
  void Initialize(const VideoDecoderConfig& config,
                  bool low_delay,
                  CdmContext* cdm_context,
                  InitCB init_cb,
                  const OutputCB& output_cb,
                  const WaitingCB& waiting_cb) override {
    std::move(init_cb).Run(DecoderStatus::Codes::kOk);
  }
  void Decode(scoped_refptr<DecoderBuffer> buffer,
              DecodeCB decode_cb) override {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(decode_cb), DecoderStatus::Codes::kFailed));
  }
  void Reset(base::OnceClosure closure) override { std::move(closure).Run(); }
  bool NeedsBitstreamConversion() const override { return true; }
};

class PipelineIntegrationTest : public testing::Test,
                                public PipelineIntegrationTestBase {
 public:
  // Verifies that seeking works properly for ChunkDemuxer when the
  // seek happens while there is a pending read on the ChunkDemuxer
  // and no data is available.
  bool TestSeekDuringRead(const std::string& filename,
                          int initial_append_size,
                          base::TimeDelta start_seek_time,
                          base::TimeDelta seek_time,
                          int seek_file_position,
                          int seek_append_size) {
    TestMediaSource source(filename, initial_append_size);

    if (StartPipelineWithMediaSource(&source, kNoClockless, nullptr) !=
        PIPELINE_OK) {
      return false;
    }

    Play();
    if (!WaitUntilCurrentTimeIsAfter(start_seek_time))
      return false;

    source.Seek(seek_time, seek_file_position, seek_append_size);
    if (!Seek(seek_time))
      return false;

    source.EndOfStream();

    source.Shutdown();
    Stop();
    return true;
  }

  void OnEnabledAudioTracksChanged(
      std::optional<MediaTrack::Id> enabled_track_id) {
    base::RunLoop run_loop;
    pipeline_->OnEnabledAudioTracksChanged(std::move(enabled_track_id),
                                           run_loop.QuitClosure());
    run_loop.Run();
  }

  void OnSelectedVideoTrackChanged(
      std::optional<MediaTrack::Id> selected_track_id) {
    base::RunLoop run_loop;
    pipeline_->OnSelectedVideoTrackChanged(selected_track_id,
                                           run_loop.QuitClosure());
    run_loop.Run();
  }
};

struct PlaybackTestData {
  const std::string filename;
  const uint32_t start_time_ms;
  const uint32_t duration_ms;
};

struct MSEPlaybackTestData {
  const std::string filename;
  const size_t append_bytes;
  const uint32_t duration_ms;
};

// Tells gtest how to print our PlaybackTestData structure.
std::ostream& operator<<(std::ostream& os, const PlaybackTestData& data) {
  return os << data.filename;
}

std::ostream& operator<<(std::ostream& os, const MSEPlaybackTestData& data) {
  return os << data.filename;
}

class BasicPlaybackTest : public PipelineIntegrationTest,
                          public testing::WithParamInterface<PlaybackTestData> {
};

TEST_P(BasicPlaybackTest, PlayToEnd) {
  PlaybackTestData data = GetParam();

  ASSERT_EQ(PIPELINE_OK, Start(data.filename, kUnreliableDuration));
  EXPECT_EQ(data.start_time_ms, demuxer_->GetStartTime().InMilliseconds());
  EXPECT_EQ(data.duration_ms, pipeline_->GetMediaDuration().InMilliseconds());

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

const PlaybackTestData kOpenCodecsTests[] = {{"bear-vp9-i422.webm", 0, 2736}};

INSTANTIATE_TEST_SUITE_P(OpenCodecs,
                         BasicPlaybackTest,
                         testing::ValuesIn(kOpenCodecsTests));

#if BUILDFLAG(USE_PROPRIETARY_CODECS)

class BasicMSEPlaybackTest
    : public ::testing::WithParamInterface<MSEPlaybackTestData>,
      public PipelineIntegrationTest {
 protected:
  void PlayToEnd() {
    MSEPlaybackTestData data = GetParam();

    TestMediaSource source(data.filename, data.append_bytes);
    ASSERT_EQ(PIPELINE_OK,
              StartPipelineWithMediaSource(&source, kNormal, nullptr));
    source.EndOfStream();

    EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
    EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
    EXPECT_EQ(data.duration_ms,
              pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

    Play();

    ASSERT_TRUE(WaitUntilOnEnded());

    EXPECT_TRUE(demuxer_->GetTimelineOffset().is_null());
    source.Shutdown();
    Stop();
  }
};

TEST_P(BasicMSEPlaybackTest, PlayToEnd) {
  PlayToEnd();
}

const PlaybackTestData kADTSTests[] = {
    {"bear-audio-main-aac.aac", 0, 2708},
    {"bear-audio-lc-aac.aac", 0, 2791},
    {"bear-audio-implicit-he-aac-v1.aac", 0, 2829},
    {"bear-audio-implicit-he-aac-v2.aac", 0, 2900},
};

// TODO(chcunningham): Migrate other basic playback tests to TEST_P.
INSTANTIATE_TEST_SUITE_P(ProprietaryCodecs,
                         BasicPlaybackTest,
                         testing::ValuesIn(kADTSTests));

const MSEPlaybackTestData kMediaSourceADTSTests[] = {
    {"bear-audio-main-aac.aac", kAppendWholeFile, 2773},
    {"bear-audio-lc-aac.aac", kAppendWholeFile, 2794},
    {"bear-audio-implicit-he-aac-v1.aac", kAppendWholeFile, 2858},
    {"bear-audio-implicit-he-aac-v2.aac", kAppendWholeFile, 2901},
};

// TODO(chcunningham): Migrate other basic MSE playback tests to TEST_P.
INSTANTIATE_TEST_SUITE_P(ProprietaryCodecs,
                         BasicMSEPlaybackTest,
                         testing::ValuesIn(kMediaSourceADTSTests));

#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)

struct MSEChangeTypeTestData {
  const MSEPlaybackTestData file_one;
  const MSEPlaybackTestData file_two;
};

class MSEChangeTypeTest
    : public ::testing::WithParamInterface<
          std::tuple<MSEPlaybackTestData, MSEPlaybackTestData>>,
      public PipelineIntegrationTest {
 public:
  // Populate meaningful test suffixes instead of /0, /1, etc.
  struct PrintToStringParamName {
    template <class ParamType>
    std::string operator()(
        const testing::TestParamInfo<ParamType>& info) const {
      std::string s = base::StrCat({std::get<0>(info.param).filename, "_AND_",
                                    std::get<1>(info.param).filename});
      // Strip out invalid param name characters.
      std::erase_if(s, [](char c) {
        return !absl::ascii_isalnum(static_cast<unsigned char>(c)) && c != '_';
      });
      return s;
    }
  };

 protected:
  void PlayBackToBack() {
    // TODO(wolenetz): Consider a modified, composable, hash that lets us
    // combine known hashes for two files to generate an expected hash for when
    // both are played. For now, only the duration (and successful append and
    // play-to-end) are verified.
    MSEPlaybackTestData file_one = std::get<0>(GetParam());
    MSEPlaybackTestData file_two = std::get<1>(GetParam());

    // Start in 'sequence' appendMode, because some test media begin near enough
    // to time 0, resulting in gaps across the changeType boundary in buffered
    // media timeline.
    // TODO(wolenetz): Switch back to 'segments' mode once we have some
    // incubation of a way to flexibly allow playback through unbuffered
    // regions. Known test media requiring sequence mode: MP3-in-MP2T
    TestMediaSource source(file_one.filename, file_one.append_bytes, true);
    ASSERT_EQ(PIPELINE_OK,
              StartPipelineWithMediaSource(&source, kNormal, nullptr));
    source.EndOfStream();

    // Transitions between VP8A and other test media can trigger this again.
    EXPECT_CALL(*this, OnVideoOpacityChange(_)).Times(AnyNumber());

    Ranges<base::TimeDelta> ranges = pipeline_->GetBufferedTimeRanges();
    EXPECT_EQ(1u, ranges.size());
    EXPECT_EQ(0, ranges.start(0).InMilliseconds());
    base::TimeDelta file_one_end_time = ranges.end(0);
    EXPECT_EQ(file_one.duration_ms, file_one_end_time.InMilliseconds());

    // Change type and append |file_two| with start time abutting end of
    // the previous buffered range.
    source.UnmarkEndOfStream();
    source.ChangeType(GetMimeTypeForFile(file_two.filename));
    scoped_refptr<DecoderBuffer> file_two_contents =
        ReadTestDataFile(file_two.filename);
    source.AppendAtTime(
        file_one_end_time,
        file_two.append_bytes == kAppendWholeFile
            ? *file_two_contents
            : (*file_two_contents).first(file_two.append_bytes));
    source.EndOfStream();
    ranges = pipeline_->GetBufferedTimeRanges();
    EXPECT_EQ(1u, ranges.size());
    EXPECT_EQ(0, ranges.start(0).InMilliseconds());

    base::TimeDelta file_two_actual_duration =
        ranges.end(0) - file_one_end_time;
    EXPECT_EQ(file_two_actual_duration.InMilliseconds(), file_two.duration_ms);

    Play();

    ASSERT_TRUE(WaitUntilOnEnded());
    EXPECT_TRUE(demuxer_->GetTimelineOffset().is_null());
    source.Shutdown();
    Stop();
  }
};

TEST_P(MSEChangeTypeTest, PlayBackToBack) {
  PlayBackToBack();
}

const MSEPlaybackTestData kMediaSourceAudioFiles[] = {
    // MP3
    {"sfx.mp3", kAppendWholeFile, 313},

    // Opus in WebM
    {"sfx-opus-441.webm", kAppendWholeFile, 301},

    // Vorbis in WebM
    {"bear-320x240-audio-only.webm", kAppendWholeFile, 2768},

    // FLAC in MP4
    {"sfx-flac_frag.mp4", kAppendWholeFile, 288},

    // Opus in MP4
    {"sfx-opus_frag.mp4", kAppendWholeFile, 301},

#if BUILDFLAG(USE_PROPRIETARY_CODECS)
    // AAC in ADTS
    {"bear-audio-main-aac.aac", kAppendWholeFile, 2773},

    // AAC in MP4
    {"bear-640x360-a_frag.mp4", kAppendWholeFile, 2803},

#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
    // MP3 in MP2T
    {"bear-audio-mp4a.6B.ts", kAppendWholeFile, 1097},
#endif  // BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)
};

const MSEPlaybackTestData kMediaSourceVideoFiles[] = {
    // VP9 in WebM
    {"bear-vp9.webm", kAppendWholeFile, kVP9WebMFileDurationMs},

    // VP9 in MP4
    {"bear-320x240-v_frag-vp9.mp4", kAppendWholeFile, 2736},

    // VP8 in WebM
    {"bear-vp8a.webm", kAppendWholeFile, kVP8AWebMFileDurationMs},

#if BUILDFLAG(ENABLE_AV1_DECODER)
    // AV1 in MP4
    {"bear-av1.mp4", kAppendWholeFile, kVP9WebMFileDurationMs},

    // AV1 in WebM
    {"bear-av1.webm", kAppendWholeFile, kVP9WebMFileDurationMs},
#endif  // BUILDFLAG(ENABLE_AV1_DECODER)

#if BUILDFLAG(USE_PROPRIETARY_CODECS) && BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
    // H264 AVC3 in MP4
    {"bear-1280x720-v_frag-avc3.mp4", kAppendWholeFile,
     k1280IsoAVC3FileDurationMs},
#endif
};

INSTANTIATE_TEST_SUITE_P(
    AudioOnly,
    MSEChangeTypeTest,
    testing::Combine(testing::ValuesIn(kMediaSourceAudioFiles),
                     testing::ValuesIn(kMediaSourceAudioFiles)),
    MSEChangeTypeTest::PrintToStringParamName());

INSTANTIATE_TEST_SUITE_P(
    VideoOnly,
    MSEChangeTypeTest,
    testing::Combine(testing::ValuesIn(kMediaSourceVideoFiles),
                     testing::ValuesIn(kMediaSourceVideoFiles)),
    MSEChangeTypeTest::PrintToStringParamName());

TEST_F(PipelineIntegrationTest, BasicPlayback) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm"));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusOgg) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-opus.ogg"));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusOgg_4ch_ChannelMapping2) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-opus-4ch-channelmapping2.ogg", kWebAudio));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusOgg_11ch_ChannelMapping2) {
  ASSERT_EQ(PIPELINE_OK,
            Start("bear-opus-11ch-channelmapping2.ogg", kWebAudio));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kHashed));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ("a6dbca10f0730373ab948df04b4bc16d7bca6d3a1593dc989b6e376487544bf5",
            GetVideoHash());
  EXPECT_AUDIO_HASH("-3.59,-2.06,-0.43,2.15,0.77,-0.95,");
  EXPECT_TRUE(demuxer_->GetTimelineOffset().is_null());
}

base::TimeDelta TimestampMs(int milliseconds) {
  return base::Milliseconds(milliseconds);
}

TEST_F(PipelineIntegrationTest, WaveLayoutChange) {
  ASSERT_EQ(PIPELINE_OK, Start("layout_change.wav"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// TODO(crbug.com/40235621): At most one of Playback9Channels48000hz and
// Playback9Channels44100hz will pass, because for 9+ channel files the hardware
// sample rate has to match the file's sample rate. They are both disabled
// because different CI configurations have different hardware sample rates. To
// run the tests, enable them both and expect at most one of them to pass.
TEST_F(PipelineIntegrationTest, DISABLED_Playback9Channels48000hz) {
  EXPECT_EQ(PIPELINE_OK, Start("9ch.wav"));
}

TEST_F(PipelineIntegrationTest, DISABLED_Playback9Channels44100hz) {
  EXPECT_EQ(PIPELINE_OK, Start("9ch_44100.wav"));
}

TEST_F(PipelineIntegrationTest, PlaybackStereo48000hz) {
  EXPECT_EQ(PIPELINE_OK, Start("stereo_48000.wav"));
}

TEST_F(PipelineIntegrationTest, PlaybackWithAudioTrackDisabledThenEnabled) {
#if BUILDFLAG(IS_MAC)
  // Enable scoped logs to help track down hangs.  http://crbug.com/1014646
  ScopedVerboseLogEnabler scoped_log_enabler;
#endif

  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kHashed | kNoClockless));

  // Disable audio.
  OnEnabledAudioTracksChanged(std::nullopt);

  // Seek to flush the pipeline and ensure there's no prerolled audio data.
  ASSERT_TRUE(Seek(base::TimeDelta()));

  Play();
  const base::TimeDelta k500ms = TimestampMs(500);
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(k500ms));
  Pause();

  // Verify that no audio has been played, since we disabled audio tracks.
  EXPECT_AUDIO_HASH(kNullAudioHash);

  // Re-enable audio.
  OnEnabledAudioTracksChanged(MediaTrack::Id("2"));

  // Restart playback from 500ms position.
  ASSERT_TRUE(Seek(k500ms));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  // Verify that audio has been playing after being enabled.
  EXPECT_AUDIO_HASH("-1.53,0.21,1.23,1.56,-0.34,-0.94,");
}

TEST_F(PipelineIntegrationTest, PlaybackWithVideoTrackDisabledThenEnabled) {
#if BUILDFLAG(IS_MAC)
  // Enable scoped logs to help track down hangs.  http://crbug.com/1014646
  ScopedVerboseLogEnabler scoped_log_enabler;
#endif

  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kHashed | kNoClockless));

  // Disable video.
  OnSelectedVideoTrackChanged(std::nullopt);

  // Seek to flush the pipeline and ensure there's no prerolled video data.
  ASSERT_TRUE(Seek(base::TimeDelta()));

  // Reset the video hash in case some of the prerolled video frames have been
  // hashed already.
  ResetVideoHash();

  Play();
  const base::TimeDelta k500ms = TimestampMs(500);
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(k500ms));
  Pause();

  // Verify that no video has been rendered, since we disabled video tracks.
  EXPECT_EQ(kNullVideoHash, GetVideoHash());

  // Reset the hash before enabling the video track and seeking so that those
  // frames are considered as part of the hash.
  ResetVideoHash();

  // Re-enable video.
  OnSelectedVideoTrackChanged(MediaTrack::Id("1"));

  // Seek to flush video pipeline and reset the video hash again to clear state
  // if some prerolled frames got hashed after enabling video.
  ASSERT_TRUE(Seek(base::TimeDelta()));

  // Restart playback from 500ms position.
  ASSERT_TRUE(Seek(k500ms));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  // Verify that video has been rendered after being enabled.
  EXPECT_EQ("38b8b5de49e2ada131674f90e4688dfc9309e6d194267e6f11992d25f11ea472",
            GetVideoHash());
}

TEST_F(PipelineIntegrationTest, TrackStatusChangesBeforePipelineStarted) {
  OnEnabledAudioTracksChanged(std::nullopt);
  OnSelectedVideoTrackChanged(std::nullopt);
}

TEST_F(PipelineIntegrationTest, TrackStatusChangesAfterPipelineEnded) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kHashed));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  // Disable audio track.
  OnEnabledAudioTracksChanged(std::nullopt);
  // Re-enable audio track.
  OnEnabledAudioTracksChanged(MediaTrack::Id("2"));
  // Disable video track.
  OnSelectedVideoTrackChanged(std::nullopt);
  // Re-enable video track.
  OnSelectedVideoTrackChanged(MediaTrack::Id("1"));
}

// TODO(crbug.com/40101269): Enable test when MacOS flake is fixed.
#if BUILDFLAG(IS_MAC)
#define MAYBE_TrackStatusChangesWhileSuspended \
  DISABLED_TrackStatusChangesWhileSuspended
#else
#define MAYBE_TrackStatusChangesWhileSuspended TrackStatusChangesWhileSuspended
#endif

TEST_F(PipelineIntegrationTest, MAYBE_TrackStatusChangesWhileSuspended) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kNoClockless));
  Play();

  ASSERT_TRUE(Suspend());

  // These get triggered every time playback is resumed.
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(320, 240)))
      .Times(AnyNumber());
  EXPECT_CALL(*this, OnVideoOpacityChange(true)).Times(AnyNumber());

  // Disable audio track.
  OnEnabledAudioTracksChanged(std::nullopt);
  ASSERT_TRUE(Resume(TimestampMs(100)));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(200)));
  ASSERT_TRUE(Suspend());

  // Re-enable audio track.
  OnEnabledAudioTracksChanged(MediaTrack::Id("2"));
  ASSERT_TRUE(Resume(TimestampMs(200)));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(300)));
  ASSERT_TRUE(Suspend());

  // Disable video track.
  OnSelectedVideoTrackChanged(std::nullopt);
  ASSERT_TRUE(Resume(TimestampMs(300)));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(400)));
  ASSERT_TRUE(Suspend());

  // Re-enable video track.
  OnSelectedVideoTrackChanged(MediaTrack::Id("1"));
  ASSERT_TRUE(Resume(TimestampMs(400)));
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, ReinitRenderersWhileAudioTrackIsDisabled) {
  // This test is flaky without kNoClockless, see crbug.com/788387.
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kNoClockless));
  Play();

  // These get triggered every time playback is resumed.
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(320, 240)))
      .Times(AnyNumber());
  EXPECT_CALL(*this, OnVideoOpacityChange(true)).Times(AnyNumber());

  // Disable the audio track.
  OnEnabledAudioTracksChanged(std::nullopt);
  // pipeline.Suspend() releases renderers and pipeline.Resume() recreates and
  // reinitializes renderers while the audio track is disabled.
  ASSERT_TRUE(Suspend());
  ASSERT_TRUE(Resume(TimestampMs(100)));
  // Now re-enable the audio track, playback should continue successfully.
  EXPECT_CALL(*this, OnBufferingStateChange(BUFFERING_HAVE_ENOUGH, _)).Times(1);
  OnEnabledAudioTracksChanged(MediaTrack::Id("2"));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(200)));

  Stop();
}

TEST_F(PipelineIntegrationTest, ReinitRenderersWhileVideoTrackIsDisabled) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kNoClockless));
  Play();

  // These get triggered every time playback is resumed.
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(320, 240)))
      .Times(AnyNumber());
  EXPECT_CALL(*this, OnVideoOpacityChange(true)).Times(AnyNumber());

  // Disable the video track.
  OnSelectedVideoTrackChanged(std::nullopt);
  // pipeline.Suspend() releases renderers and pipeline.Resume() recreates and
  // reinitializes renderers while the video track is disabled.
  ASSERT_TRUE(Suspend());
  ASSERT_TRUE(Resume(TimestampMs(100)));
  // Now re-enable the video track, playback should continue successfully.
  OnSelectedVideoTrackChanged(MediaTrack::Id("1"));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(200)));

  Stop();
}

TEST_F(PipelineIntegrationTest, PipelineStoppedWhileAudioRestartPending) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm"));
  Play();

  // Disable audio track first, to re-enable it later and stop the pipeline
  // (which destroys the media renderer) while audio restart is pending.
  OnEnabledAudioTracksChanged(std::nullopt);

  // Playback is paused while all audio tracks are disabled.
  OnEnabledAudioTracksChanged(MediaTrack::Id("2"));
  Stop();
}

TEST_F(PipelineIntegrationTest, PipelineStoppedWhileVideoRestartPending) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm"));
  Play();

  // Disable video track first, to re-enable it later and stop the pipeline
  // (which destroys the media renderer) while video restart is pending.
  OnSelectedVideoTrackChanged(std::nullopt);
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(200)));

  OnSelectedVideoTrackChanged(MediaTrack::Id("1"));
  Stop();
}

TEST_F(PipelineIntegrationTest, SwitchAudioTrackDuringPlayback) {
  ASSERT_EQ(PIPELINE_OK, Start("multitrack-3video-2audio.webm", kNoClockless));
  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(100)));
  // The first audio track (TrackId=4) is enabled by default. This should
  // disable TrackId=4 and enable TrackId=5.
  OnEnabledAudioTracksChanged(MediaTrack::Id("5"));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(200)));
  Stop();
}

TEST_F(PipelineIntegrationTest, SwitchVideoTrackDuringPlayback) {
  ASSERT_EQ(PIPELINE_OK, Start("multitrack-3video-2audio.webm", kNoClockless));
  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(100)));
  // The first video track (TrackId=1) is enabled by default. This should
  // disable TrackId=1 and enable TrackId=2.
  OnSelectedVideoTrackChanged(MediaTrack::Id("2"));
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(200)));
  Stop();
}

TEST_F(PipelineIntegrationTest, MixSeekAndTrackSwitch) {
  // We need something with multiple tracks.
  ASSERT_EQ(PIPELINE_OK, Start("multitrack-3video-2audio.webm", kNoClockless));
  Pause();
  Seek(base::Seconds(8));
  Play();
  OnSelectedVideoTrackChanged(MediaTrack::Id("3"));
  OnSelectedVideoTrackChanged(MediaTrack::Id("1"));
  OnEnabledAudioTracksChanged({MediaTrack::Id("5")});
  Seek(base::Seconds(0));
  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(TimestampMs(100)));
  Stop();
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusOggTrimmingHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("opus-trimming-test.ogg", kHashed));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_1);

  // Seek within the pre-skip section, this should not cause a beep.
  ASSERT_TRUE(Seek(base::Seconds(1)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_2);

  // Seek somewhere outside of the pre-skip / end-trim section, demuxer should
  // correctly preroll enough to accurately decode this segment.
  ASSERT_TRUE(Seek(base::Milliseconds(6360)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_3);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusWebmTrimmingHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("opus-trimming-test.webm", kHashed));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_1);

  // Seek within the pre-skip section, this should not cause a beep.
  ASSERT_TRUE(Seek(base::Seconds(1)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_2);

  // Seek somewhere outside of the pre-skip / end-trim section, demuxer should
  // correctly preroll enough to accurately decode this segment.
  ASSERT_TRUE(Seek(base::Milliseconds(6360)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_3);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusMp4TrimmingHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("opus-trimming-test.mp4", kHashed));

  Play();

  // TODO(dalecurtis): The test clip currently does not have the edit list
  // entries required to achieve correctness here. Delete this comment and
  // uncomment the EXPECT_AUDIO_HASH lines when https://crbug.com/876544 is
  // fixed.

  ASSERT_TRUE(WaitUntilOnEnded());
  // EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_1);

  // Seek within the pre-skip section, this should not cause a beep.
  ASSERT_TRUE(Seek(base::Seconds(1)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  // EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_2);

  // Seek somewhere outside of the pre-skip / end-trim section, demuxer should
  // correctly preroll enough to accurately decode this segment.
  ASSERT_TRUE(Seek(base::Milliseconds(6360)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  // EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_3);
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlaybackOpusWebmTrimmingHashed) {
  TestMediaSource source("opus-trimming-test.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_1);

  // Seek within the pre-skip section, this should not cause a beep.
  base::TimeDelta seek_time = base::Seconds(1);
  source.Seek(seek_time);
  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_2);

  // Seek somewhere outside of the pre-skip / end-trim section, demuxer should
  // correctly preroll enough to accurately decode this segment.
  seek_time = base::Milliseconds(6360);
  source.Seek(seek_time);
  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_3);
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlaybackOpusMp4TrimmingHashed) {
  TestMediaSource source("opus-trimming-test.mp4", kAppendWholeFile);

  // TODO(dalecurtis): The test clip currently does not have the edit list
  // entries required to achieve correctness here, so we're manually specifying
  // the edits using append window trimming.
  //
  // It's unclear if MSE actually supports edit list features required to
  // achieve correctness either. Delete this comment and remove the manual
  // SetAppendWindow() if/when https://crbug.com/876544 is fixed.
  source.SetAppendWindow(base::TimeDelta(), base::TimeDelta(),
                         base::Microseconds(12720021));

  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_1);

  // Seek within the pre-skip section, this should not cause a beep.
  base::TimeDelta seek_time = base::Seconds(1);
  source.Seek(seek_time);
  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_2);

  // Seek somewhere outside of the pre-skip / end-trim section, demuxer should
  // correctly preroll enough to accurately decode this segment.
  seek_time = base::Milliseconds(6360);
  source.Seek(seek_time);
  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusEndTrimmingHash_3);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusWebmHashed_MonoOutput) {
  ASSERT_EQ(PIPELINE_OK,
            Start("bunny-opus-intensity-stereo.webm", kHashed | kMonoOutput));

  // File should have stereo output, which we know to be encoded using "phase
  // intensity". Downmixing such files to MONO produces artifacts unless the
  // decoder performs the downmix, which disables "phase inversion". See
  // http://crbug.com/806219
  AudioDecoderConfig config =
      demuxer_->GetFirstStream(DemuxerStream::AUDIO)->audio_decoder_config();
  ASSERT_EQ(config.channel_layout(), CHANNEL_LAYOUT_STEREO);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

#if defined(OPUS_FIXED_POINT)
  EXPECT_AUDIO_HASH("-2.41,-1.66,0.79,1.53,1.46,-0.91,");
#else
  EXPECT_AUDIO_HASH("-2.36,-1.64,0.84,1.55,1.51,-0.90,");
#endif
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusPrerollExceedsCodecDelay) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-opus.webm", kHashed));

  AudioDecoderConfig config =
      demuxer_->GetFirstStream(DemuxerStream::AUDIO)->audio_decoder_config();

  // Verify that this file's preroll is not eclipsed by the codec delay so we
  // can detect when preroll is not properly performed.
  base::TimeDelta codec_delay = base::Seconds(
      static_cast<double>(config.codec_delay()) / config.samples_per_second());
  ASSERT_GT(config.seek_preroll(), codec_delay);

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_1);

  // Seek halfway through the file to invoke seek preroll.
  ASSERT_TRUE(Seek(base::Seconds(1.414)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_2);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackOpusMp4PrerollExceedsCodecDelay) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-opus.mp4", kHashed));

  AudioDecoderConfig config =
      demuxer_->GetFirstStream(DemuxerStream::AUDIO)->audio_decoder_config();

  // Verify that this file's preroll is not eclipsed by the codec delay so we
  // can detect when preroll is not properly performed.
  base::TimeDelta codec_delay = base::Seconds(
      static_cast<double>(config.codec_delay()) / config.samples_per_second());
  ASSERT_GT(config.seek_preroll(), codec_delay);

  // TODO(dalecurtis): The test clip currently does not have the edit list
  // entries required to achieve correctness here. Delete this comment and
  // uncomment the EXPECT_AUDIO_HASH lines when https://crbug.com/876544 is
  // fixed.

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  // EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_1);

  // Seek halfway through the file to invoke seek preroll.
  ASSERT_TRUE(Seek(base::Seconds(1.414)));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  // EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_2);
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlaybackOpusPrerollExceedsCodecDelay) {
  TestMediaSource source("bear-opus.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  AudioDecoderConfig config =
      demuxer_->GetFirstStream(DemuxerStream::AUDIO)->audio_decoder_config();

  // Verify that this file's preroll is not eclipsed by the codec delay so we
  // can detect when preroll is not properly performed.
  base::TimeDelta codec_delay = base::Seconds(
      static_cast<double>(config.codec_delay()) / config.samples_per_second());
  ASSERT_GT(config.seek_preroll(), codec_delay);

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_1);

  // Seek halfway through the file to invoke seek preroll.
  base::TimeDelta seek_time = base::Seconds(1.414);
  source.Seek(seek_time);
  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_2);
}

TEST_F(PipelineIntegrationTest,
       MSE_BasicPlaybackOpusMp4PrerollExceedsCodecDelay) {
  TestMediaSource source("bear-opus.mp4", kAppendWholeFile);

  // TODO(dalecurtis): The test clip currently does not have the edit list
  // entries required to achieve correctness here, so we're manually specifying
  // the edits using append window trimming.
  //
  // It's unclear if MSE actually supports edit list features required to
  // achieve correctness either. Delete this comment and remove the manual
  // SetAppendWindow() if/when https://crbug.com/876544 is fixed.
  source.SetAppendWindow(base::TimeDelta(), base::TimeDelta(),
                         base::Microseconds(2740834));

  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  AudioDecoderConfig config =
      demuxer_->GetFirstStream(DemuxerStream::AUDIO)->audio_decoder_config();

  // Verify that this file's preroll is not eclipsed by the codec delay so we
  // can detect when preroll is not properly performed.
  base::TimeDelta codec_delay = base::Seconds(
      static_cast<double>(config.codec_delay()) / config.samples_per_second());
  ASSERT_GT(config.seek_preroll(), codec_delay);

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_1);

  // Seek halfway through the file to invoke seek preroll.
  base::TimeDelta seek_time = base::Seconds(1.414);
  source.Seek(seek_time);
  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_AUDIO_HASH(kOpusSmallCodecDelayHash_2);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackLive) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240-live.webm", kHashed));

  // Live stream does not have duration in the initialization segment.
  // It will be set after the entire file is available.
  EXPECT_CALL(*this, OnDurationChange()).Times(1);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ("a6dbca10f0730373ab948df04b4bc16d7bca6d3a1593dc989b6e376487544bf5",
            GetVideoHash());
  EXPECT_AUDIO_HASH("-3.59,-2.06,-0.43,2.15,0.77,-0.95,");
  EXPECT_EQ(kLiveTimelineOffset(), demuxer_->GetTimelineOffset());
}

TEST_F(PipelineIntegrationTest, S32PlaybackHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx_s32le.wav", kHashed));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(kNullVideoHash, GetVideoHash());
  EXPECT_AUDIO_HASH(kSfxLosslessHash);
}

TEST_F(PipelineIntegrationTest, F32PlaybackHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx_f32le.wav", kHashed));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(kNullVideoHash, GetVideoHash());
  EXPECT_AUDIO_HASH(kSfxLosslessHash);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackEncrypted) {
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  set_encrypted_media_init_data_cb(
      base::BindRepeating(&FakeEncryptedMedia::OnEncryptedMediaInitData,
                          base::Unretained(&encrypted_media)));

  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240-av_enc-av.webm",
                               encrypted_media.GetCdmContext()));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  Stop();
}

TEST_F(PipelineIntegrationTest, FlacPlaybackHashed) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx.flac", kHashed));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(kNullVideoHash, GetVideoHash());
  EXPECT_AUDIO_HASH(kSfxLosslessHash);
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback) {
  TestMediaSource source("bear-320x240.webm", 219229);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(k320WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_TRUE(demuxer_->GetTimelineOffset().is_null());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_EosBeforeDemuxerOpened) {
  // After appending only a partial initialization segment, marking end of
  // stream should let the test complete with error indicating failure to open
  // demuxer. Here we append only the first 10 bytes of a test WebM, definitely
  // less than the ~4400 bytes needed to parse its full initialization segment.
  TestMediaSource source("bear-320x240.webm", 10);
  source.set_do_eos_after_next_append(true);
  EXPECT_EQ(
      DEMUXER_ERROR_COULD_NOT_OPEN,
      StartPipelineWithMediaSource(&source, kExpectDemuxerFailure, nullptr));
}

TEST_F(PipelineIntegrationTest, MSE_CorruptedFirstMediaSegment) {
  // After successful initialization segment append completing demuxer opening,
  // immediately append a corrupted media segment to trigger parse error while
  // pipeline is still completing renderer setup.
  TestMediaSource source("bear-320x240_corrupted_after_init_segment.webm",
                         4380);
  source.set_expected_append_result(
      TestMediaSource::ExpectedAppendResult::kFailure);
  EXPECT_EQ(CHUNK_DEMUXER_ERROR_APPEND_FAILED,
            StartPipelineWithMediaSource(&source));
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_Live) {
  TestMediaSource source("bear-320x240-live.webm", 219221);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(k320WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(kLiveTimelineOffset(), demuxer_->GetTimelineOffset());
  source.Shutdown();
  Stop();
}

#if BUILDFLAG(ENABLE_AV1_DECODER)
TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_AV1_WebM) {
  TestMediaSource source("bear-av1.webm", 18898);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP9WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_AV1_10bit_WebM) {
  TestMediaSource source("bear-av1-320x180-10bit.webm", 19076);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP9WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_YUV420P10);
  Stop();
}

#endif

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VP9_WebM) {
  TestMediaSource source("bear-vp9.webm", 67504);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP9WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VP9_BlockGroup_WebM) {
  TestMediaSource source("bear-vp9-blockgroup.webm", 67871);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP9WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VP8A_WebM) {
  TestMediaSource source("bear-vp8a.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP8AWebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

#if BUILDFLAG(ENABLE_AV1_DECODER)
TEST_F(PipelineIntegrationTest, MSE_ConfigChange_AV1_WebM) {
  TestMediaSource source("bear-av1-480x360.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));

  const gfx::Size kNewSize(640, 480);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-av1-640x480.webm");
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + kAV1640WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}
#endif  // BUILDFLAG(ENABLE_AV1_DECODER)

TEST_F(PipelineIntegrationTest, MSE_ConfigChange_WebM) {
  TestMediaSource source("bear-320x240-16x9-aspect.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));

  const gfx::Size kNewSize(640, 360);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-640x360.webm");
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k640WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_AudioConfigChange_WebM) {
  TestMediaSource source("bear-320x240-audio-only.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));

  const int kNewSampleRate = 48000;
  EXPECT_CALL(*this,
              OnAudioConfigChange(::testing::Property(
                  &AudioDecoderConfig::samples_per_second, kNewSampleRate)))
      .Times(1);

  // A higher sample rate will cause the audio buffer durations to change. This
  // should not manifest as a timestamp gap in AudioTimestampValidator.
  // Timestamp expectations should be reset across config changes.
  EXPECT_MEDIA_LOG(Not(HasSubstr("Large timestamp gap detected")))
      .Times(AnyNumber());

  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-320x240-audio-only-48khz.webm");
  ASSERT_TRUE(source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file));
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(3774, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_RemoveUpdatesBufferedRanges) {
  TestMediaSource source("bear-320x240.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));

  auto buffered_ranges = pipeline_->GetBufferedTimeRanges();
  EXPECT_EQ(1u, buffered_ranges.size());
  EXPECT_EQ(0, buffered_ranges.start(0).InMilliseconds());
  EXPECT_EQ(k320WebMFileDurationMs, buffered_ranges.end(0).InMilliseconds());

  source.RemoveRange(base::Milliseconds(1000),
                     base::Milliseconds(k320WebMFileDurationMs));
  task_environment_.RunUntilIdle();

  buffered_ranges = pipeline_->GetBufferedTimeRanges();
  EXPECT_EQ(1u, buffered_ranges.size());
  EXPECT_EQ(0, buffered_ranges.start(0).InMilliseconds());
  EXPECT_EQ(1001, buffered_ranges.end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

// This test case imitates media playback with advancing media_time and
// continuously adding new data. At some point we should reach the buffering
// limit, after that MediaSource should evict some buffered data and that
// evicted data should be reflected in the change of media::Pipeline buffered
// ranges (returned by GetBufferedTimeRanges). At that point the buffered ranges
// will no longer start at 0.
TEST_F(PipelineIntegrationTest, MSE_FillUpBuffer) {
  const char* input_filename = "bear-320x240.webm";
  TestMediaSource source(input_filename, kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.SetMemoryLimits(1048576);

  scoped_refptr<DecoderBuffer> file = ReadTestDataFile(input_filename);

  auto buffered_ranges = pipeline_->GetBufferedTimeRanges();
  EXPECT_EQ(1u, buffered_ranges.size());
  do {
    // Advance media_time to the end of the currently buffered data
    base::TimeDelta media_time = buffered_ranges.end(0);
    source.Seek(media_time);
    // Ask MediaSource to evict buffered data if buffering limit has been
    // reached (the data will be evicted from the front of the buffered range).
    source.EvictCodedFrames(media_time, file->size());
    source.AppendAtTime(media_time, *file);
    task_environment_.RunUntilIdle();

    buffered_ranges = pipeline_->GetBufferedTimeRanges();
  } while (buffered_ranges.size() == 1 &&
           buffered_ranges.start(0) == base::Seconds(0));

  EXPECT_EQ(1u, buffered_ranges.size());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_GCWithDisabledVideoStream) {
  const char* input_filename = "bear-320x240.webm";
  TestMediaSource source(input_filename, kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  scoped_refptr<DecoderBuffer> file = ReadTestDataFile(input_filename);
  // The input file contains audio + video data. Assuming video data size is
  // larger than audio, so setting memory limits to half of file data_size will
  // ensure that video SourceBuffer is above memory limit and the audio
  // SourceBuffer is below the memory limit.
  source.SetMemoryLimits(file->size() / 2);

  // Disable the video track and start playback. Renderer won't read from the
  // disabled video stream, so the video stream read position should be 0.
  OnSelectedVideoTrackChanged(std::nullopt);
  Play();

  // Wait until audio playback advances past 2 seconds and call MSE GC algorithm
  // to prepare for more data to be appended.
  base::TimeDelta media_time = base::Seconds(2);
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(media_time));
  // At this point the video SourceBuffer is over the memory limit (see the
  // SetMemoryLimits comment above), but MSE GC should be able to remove some
  // of video data and return true indicating success, even though no data has
  // been read from the disabled video stream and its read position is 0.
  ASSERT_TRUE(source.EvictCodedFrames(media_time, 10));

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_ConfigChange_Encrypted_WebM) {
  TestMediaSource source("bear-320x240-16x9-aspect-av_enc-av.webm",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  const gfx::Size kNewSize(640, 360);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-640x360-av_enc-av.webm");

  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k640WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_ConfigChange_ClearThenEncrypted_WebM) {
  TestMediaSource source("bear-320x240-16x9-aspect.webm", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  const gfx::Size kNewSize(640, 360);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-640x360-av_enc-av.webm");

  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k640WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

// Config change from encrypted to clear is allowed by the demuxer, and is
// supported by the Renderer.
TEST_F(PipelineIntegrationTest, MSE_ConfigChange_EncryptedThenClear_WebM) {
  TestMediaSource source("bear-320x240-16x9-aspect-av_enc-av.webm",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  const gfx::Size kNewSize(640, 360);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-640x360.webm");

  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k640WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

#if defined(ARCH_CPU_X86_FAMILY) && !BUILDFLAG(IS_ANDROID)
TEST_F(PipelineIntegrationTest, BasicPlaybackHi10PVP9) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x180-hi10p-vp9.webm"));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHi12PVP9) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x180-hi12p-vp9.webm"));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}
#endif

#if BUILDFLAG(ENABLE_AV1_DECODER)
TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_AV1_MP4) {
  TestMediaSource source("bear-av1.mp4", 24355);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP9WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_AV1_Audio_OPUS_MP4) {
  TestMediaSource source("bear-av1-opus.mp4", 50253);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kVP9WebMFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_AV1_10bit_MP4) {
  TestMediaSource source("bear-av1-320x180-10bit.mp4", 19658);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAV110bitMp4FileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_YUV420P10);
  Stop();
}
#endif

TEST_F(PipelineIntegrationTest, MSE_FlacInMp4_Hashed) {
  TestMediaSource source("sfx-flac_frag.mp4", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(288, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(kNullVideoHash, GetVideoHash());
  EXPECT_AUDIO_HASH(kSfxLosslessHash);
}

TEST_F(PipelineIntegrationTest, MSE_fLaCInMp4_Hashed) {
  TestMediaSource source("sfx-flac_frag.mp4", "audio/mp4; codecs=\"fLaC\"",
                         kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(288, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(kNullVideoHash, GetVideoHash());
  EXPECT_AUDIO_HASH(kSfxLosslessHash);
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHashed_MP3) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx.mp3", kHashed));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

  // Verify codec delay and preroll are stripped.
  EXPECT_AUDIO_HASH("1.30,2.72,4.56,5.08,3.74,2.03,");
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHashed_FlacInMp4) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx-flac.mp4", kHashed));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(kNullVideoHash, GetVideoHash());
  EXPECT_AUDIO_HASH(kSfxLosslessHash);
}

#if BUILDFLAG(ENABLE_AV1_DECODER)
TEST_F(PipelineIntegrationTest, BasicPlayback_VideoOnly_AV1_Mp4) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-av1.mp4"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlayback_VideoOnly_MonoAV1_Mp4) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-mono-av1.mp4"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlayback_Video_AV1_Audio_Opus_Mp4) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-av1-opus.mp4"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}
#endif

class Mp3FastSeekParams {
 public:
  Mp3FastSeekParams(const char* filename, const char* hash)
      : filename(filename), hash(hash) {}
  const char* filename;
  const char* hash;
};

class Mp3FastSeekIntegrationTest
    : public PipelineIntegrationTest,
      public testing::WithParamInterface<Mp3FastSeekParams> {};

TEST_P(Mp3FastSeekIntegrationTest, FastSeekAccuracy_MP3) {
  Mp3FastSeekParams config = GetParam();
  ASSERT_EQ(PIPELINE_OK, Start(config.filename, kHashed));

  // The XING TOC is inaccurate. We don't use it for CBR, we tolerate it for VBR
  // (best option for fast seeking; see Mp3SeekFFmpegDemuxerTest). The chosen
  // seek time exposes inaccuracy in TOC such that the hash will change if seek
  // logic is regressed. See https://crbug.com/545914.
  //
  // Quick TOC design (not pretty!):
  // - All MP3 TOCs are 100 bytes
  // - Each byte is read as a uint8_t; value between 0 - 255.
  // - The index into this array is the numerator in the ratio: index / 100.
  //   This fraction represents a playback time as a percentage of duration.
  // - The value at the given index is the numerator in the ratio: value / 256.
  //   This fraction represents a byte offset as a percentage of the file size.
  //
  // For CBR files, each frame is the same size, so the offset for time of
  // (0.98 * duration) should be around (0.98 * file size). This is 250.88 / 256
  // but the numerator will be truncated in the TOC as 250, losing precision.
  base::TimeDelta seek_time(0.98 * pipeline_->GetMediaDuration());

  ASSERT_TRUE(Seek(seek_time));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_AUDIO_HASH(config.hash);
}

// CBR seeks should always be fast and accurate.
INSTANTIATE_TEST_SUITE_P(
    CBRSeek_HasTOC,
    Mp3FastSeekIntegrationTest,
    ::testing::Values(Mp3FastSeekParams("bear-audio-10s-CBR-has-TOC.mp3",
                                        "-0.58,0.61,3.08,2.55,0.90,-1.20,")));

INSTANTIATE_TEST_SUITE_P(
    CBRSeeks_NoTOC,
    Mp3FastSeekIntegrationTest,
    ::testing::Values(Mp3FastSeekParams("bear-audio-10s-CBR-no-TOC.mp3",
                                        "1.16,0.68,1.25,0.60,1.66,0.93,")));

// VBR seeks can be fast *OR* accurate, but not both. We chose fast.
INSTANTIATE_TEST_SUITE_P(
    VBRSeeks_HasTOC,
    Mp3FastSeekIntegrationTest,
    ::testing::Values(Mp3FastSeekParams("bear-audio-10s-VBR-has-TOC.mp3",
                                        "0.65,0.22,1.01,0.57,0.84,0.83,")));

INSTANTIATE_TEST_SUITE_P(
    VBRSeeks_NoTOC,
    Mp3FastSeekIntegrationTest,
    ::testing::Values(Mp3FastSeekParams("bear-audio-10s-VBR-no-TOC.mp3",
                                        "-0.22,0.80,1.19,0.73,-0.31,-1.12,")));

TEST_F(PipelineIntegrationTest, MSE_MP3) {
  TestMediaSource source("sfx.mp3", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(313, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  EXPECT_TRUE(WaitUntilOnEnded());

  // Verify that codec delay was stripped.
  EXPECT_AUDIO_HASH("1.01,2.71,4.18,4.32,3.04,1.12,");
}

TEST_F(PipelineIntegrationTest, MSE_MP3_TimestampOffset) {
  TestMediaSource source("sfx.mp3", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  EXPECT_EQ(313, source.last_timestamp_offset().InMilliseconds());

  // There are 576 silent frames at the start of this mp3.  The second append
  // should trim them off.
  const base::TimeDelta mp3_preroll_duration = base::Seconds(576.0 / 44100);
  const base::TimeDelta append_time =
      source.last_timestamp_offset() - mp3_preroll_duration;

  scoped_refptr<DecoderBuffer> second_file = ReadTestDataFile("sfx.mp3");
  source.AppendAtTimeWithWindow(append_time, append_time + mp3_preroll_duration,
                                kInfiniteDuration, *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(613, source.last_timestamp_offset().InMilliseconds());
  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(613, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());
}

TEST_F(PipelineIntegrationTest, MSE_MP3_Icecast) {
  TestMediaSource source("icy_sfx.mp3", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  Play();

  EXPECT_TRUE(WaitUntilOnEnded());
}

#if BUILDFLAG(USE_PROPRIETARY_CODECS)

TEST_F(PipelineIntegrationTest, MSE_ADTS) {
  TestMediaSource source("sfx.adts", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(325, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  EXPECT_TRUE(WaitUntilOnEnded());

  // Verify that nothing was stripped.
  EXPECT_AUDIO_HASH("0.46,1.72,4.26,4.57,3.39,1.53,");
}

TEST_F(PipelineIntegrationTest, MSE_ADTS_TimestampOffset) {
  TestMediaSource source("sfx.adts", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithMediaSource(&source, kHashed, nullptr));
  EXPECT_EQ(325, source.last_timestamp_offset().InMilliseconds());

  // Trim off multiple frames off the beginning of the segment which will cause
  // the first decoded frame to be incorrect if preroll isn't implemented.
  const base::TimeDelta adts_preroll_duration =
      base::Seconds(2.5 * 1024 / 44100);
  const base::TimeDelta append_time =
      source.last_timestamp_offset() - adts_preroll_duration;

  scoped_refptr<DecoderBuffer> second_file = ReadTestDataFile("sfx.adts");
  source.AppendAtTimeWithWindow(append_time,
                                append_time + adts_preroll_duration,
                                kInfiniteDuration, *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(592, source.last_timestamp_offset().InMilliseconds());
  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(592, pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  // Verify preroll is stripped.
  EXPECT_AUDIO_HASH("-1.76,-1.35,-0.72,0.70,1.24,0.52,");
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHashed_ADTS) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx.adts", kHashed));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());

  // Verify codec delay and preroll are stripped.
  EXPECT_AUDIO_HASH("1.80,1.66,2.31,3.26,4.46,3.36,");
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHashed_M4A) {
  ASSERT_EQ(PIPELINE_OK,
            Start("440hz-10ms.m4a", kHashed | kUnreliableDuration));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  // Verify preroll is stripped. This file uses a preroll of 2112 frames, which
  // spans all three packets in the file. Postroll is not correctly stripped at
  // present; see the note below.
  EXPECT_AUDIO_HASH("3.84,4.25,4.33,3.58,3.27,3.16,");

  // Note the above hash is incorrect since the <audio> path doesn't properly
  // trim trailing silence at end of stream for AAC decodes. This isn't a huge
  // deal since plain src= tags can't splice streams and MSE requires an
  // explicit append window for correctness.
  //
  // The WebAudio path via AudioFileReader computes this correctly, so the hash
  // below is taken from that test.
  //
  // EXPECT_AUDIO_HASH("3.77,4.53,4.75,3.48,3.67,3.76,");
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_WIN)
std::unique_ptr<AudioDecoder> CreateXheAacDecoder(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    MediaLog& media_log) {
#if BUILDFLAG(IS_MAC)
  return std::make_unique<AudioToolboxAudioDecoder>(media_log.Clone());
#elif BUILDFLAG(IS_ANDROID)
  return std::make_unique<MediaCodecAudioDecoder>(task_runner);
#elif BUILDFLAG(IS_WIN)
  return MediaFoundationAudioDecoder::Create();
#else
#error "xHE-AAC decoding is not supported on this platform.";
#endif
}

TEST_F(PipelineIntegrationTest, BasicPlaybackXHE_AAC) {
  if (!IsDecoderSupportedAudioType(
          {AudioCodec::kAAC, AudioCodecProfile::kXHE_AAC, false})) {
    GTEST_SKIP() << "Unsupported platform.";
  }

  auto prepend_audio_decoders_cb = base::BindLambdaForTesting([this]() {
    std::vector<std::unique_ptr<AudioDecoder>> audio_decoders;
    audio_decoders.push_back(CreateXheAacDecoder(
        task_environment_.GetMainThreadTaskRunner(), media_log_));
    return audio_decoders;
  });

  ASSERT_EQ(PIPELINE_OK,
            Start("noise-xhe-aac.mp4", kNormal, CreateVideoDecodersCB(),
                  prepend_audio_decoders_cb));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  // Note: We don't test hashes for xHE-AAC content since the decoder is
  // provided by the operating system and will apply DRC based on device
  // specific params.

  // TODO(crbug.com/40817722): Seeking doesn't always work properly when using
  // ffmpeg since it doesn't handle non-keyframe xHE-AAC samples properly.
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlaybackXHE_AAC) {
  if (!IsDecoderSupportedAudioType(
          {AudioCodec::kAAC, AudioCodecProfile::kXHE_AAC, false})) {
    GTEST_SKIP() << "Unsupported platform.";
  }

  auto prepend_audio_decoders_cb = base::BindLambdaForTesting([this]() {
    std::vector<std::unique_ptr<AudioDecoder>> audio_decoders;
    audio_decoders.push_back(CreateXheAacDecoder(
        task_environment_.GetMainThreadTaskRunner(), media_log_));
    return audio_decoders;
  });

  TestMediaSource source("noise-xhe-aac.mp4", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(
                             &source, kNormal, prepend_audio_decoders_cb));
  source.EndOfStream();
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  Pause();

  // Note: We don't test hashes for xHE-AAC content since the decoder is
  // provided by the operating system and will apply DRC based on device
  // specific params.

  // Seek to ensure a flushing and playback resumption works properly.
  auto seek_time = pipeline_->GetMediaDuration() / 2;
  ASSERT_TRUE(Seek(seek_time));
  EXPECT_EQ(seek_time, pipeline_->GetMediaTime());

  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_WIN)

std::vector<std::unique_ptr<VideoDecoder>> CreateFailingVideoDecoder() {
  std::vector<std::unique_ptr<VideoDecoder>> failing_video_decoder;
  failing_video_decoder.push_back(std::make_unique<FailingVideoDecoder>());
  return failing_video_decoder;
}

TEST_F(PipelineIntegrationTest, BasicFallback) {
  ASSERT_EQ(PIPELINE_OK,
            Start("bear.webm", kNormal,
                  base::BindRepeating(&CreateFailingVideoDecoder)));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
TEST_F(PipelineIntegrationTest, MSE_ConfigChange_MP4) {
  TestMediaSource source("bear-640x360-av_frag.mp4", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));

  const gfx::Size kNewSize(1280, 720);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-1280x720-av_frag.mp4");
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());

  // TODO(wolenetz): Update to 2769 once MSE endOfStream implementation no
  // longer truncates duration to the highest in intersection ranges, but
  // compliantly to the largest track buffer ranges end time across all tracks
  // and SourceBuffers. See https://crbug.com/639144.
  constexpr int k1280IsoFileDurationMsAV = 2763;

  EXPECT_EQ(kAppendTimeMs + k1280IsoFileDurationMsAV,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_ConfigChange_Encrypted_MP4_CENC_VideoOnly) {
  TestMediaSource source("bear-640x360-v_frag-cenc-mdat.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  const gfx::Size kNewSize(1280, 720);
  EXPECT_CALL(*this, OnVideoConfigChange(::testing::Property(
                         &VideoDecoderConfig::natural_size, kNewSize)))
      .Times(1);
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(kNewSize)).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-1280x720-v_frag-cenc.mp4");
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(33, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k1280IsoFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest,
       MSE_ConfigChange_Encrypted_MP4_CENC_KeyRotation_VideoOnly) {
  TestMediaSource source("bear-640x360-v_frag-cenc-key_rotation.mp4",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new RotatingKeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(1280, 720))).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-1280x720-v_frag-cenc-key_rotation.mp4");
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);
  source.EndOfStream();

  Play();
  EXPECT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(33, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k1280IsoFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_ConfigChange_ClearThenEncrypted_MP4_CENC) {
  TestMediaSource source("bear-640x360-v_frag.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(1280, 720))).Times(1);
  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-1280x720-v_frag-cenc.mp4");
  source.set_expected_append_result(
      TestMediaSource::ExpectedAppendResult::kFailure);
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);

  source.EndOfStream();

  EXPECT_EQ(33, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(kAppendTimeMs + k1280IsoFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

// Config changes from encrypted to clear are not currently supported.
TEST_F(PipelineIntegrationTest, MSE_ConfigChange_EncryptedThenClear_MP4_CENC) {
  TestMediaSource source("bear-640x360-v_frag-cenc-mdat.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  scoped_refptr<DecoderBuffer> second_file =
      ReadTestDataFile("bear-1280x720-av_frag.mp4");

  source.set_expected_append_result(
      TestMediaSource::ExpectedAppendResult::kFailure);
  source.AppendAtTime(base::Seconds(kAppendTimeSec), *second_file);

  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(33, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());

  // The second video was not added, so its time has not been added.
  constexpr int k640IsoCencFileDurationMs = 2769;
  EXPECT_EQ(k640IsoCencFileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  EXPECT_EQ(CHUNK_DEMUXER_ERROR_APPEND_FAILED, WaitUntilEndedOrError());
  source.Shutdown();
}
#endif  // BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)

TEST_F(PipelineIntegrationTest, StereoAACMarkedAsMono) {
  ASSERT_EQ(PIPELINE_OK, Start("mono_cpe.adts"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)

// Verify files which change configuration midstream fail gracefully.
TEST_F(PipelineIntegrationTest, MidStreamConfigChangesFail) {
  ASSERT_EQ(PIPELINE_ERROR_DECODE, Start("midstream_config_change.mp3"));
}

TEST_F(PipelineIntegrationTest, BasicPlayback_16x9AspectRatio) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240-16x9-aspect.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_WebM) {
  TestMediaSource source("bear-320x240-av_enc-av.webm", 219816);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_ClearStart_WebM) {
  TestMediaSource source("bear-320x240-av_enc-av_clear-1s.webm",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_NoEncryptedFrames_WebM) {
  TestMediaSource source("bear-320x240-av_enc-av_clear-all.webm",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new NoResponseApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_MP4_VP9_CENC_VideoOnly) {
  TestMediaSource source("bear-320x240-v_frag-vp9-cenc.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VideoOnly_MP4_VP9) {
  TestMediaSource source("bear-320x240-v_frag-vp9.mp4", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

#if BUILDFLAG(USE_PROPRIETARY_CODECS)
#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)
TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_MP4_CENC_VideoOnly) {
  TestMediaSource source("bear-1280x720-v_frag-cenc.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest,
       MSE_EncryptedPlayback_NoEncryptedFrames_MP4_CENC_VideoOnly) {
  TestMediaSource source("bear-1280x720-v_frag-cenc_clear-all.mp4",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new NoResponseApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_Mp2ts_AAC_HE_SBR_Audio) {
  TestMediaSource source("bear-1280x720-aac_he.ts", kAppendWholeFile);
#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  // Check that SBR is taken into account correctly by mpeg2ts parser. When an
  // SBR stream is parsed as non-SBR stream, then audio frame durations are
  // calculated incorrectly and that leads to gaps in buffered ranges (so this
  // check will fail) and eventually leads to stalled playback.
  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
#else
  EXPECT_EQ(
      DEMUXER_ERROR_COULD_NOT_OPEN,
      StartPipelineWithMediaSource(&source, kExpectDemuxerFailure, nullptr));
#endif
}

#endif  // BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)

TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_MP4_CENC_AudioOnly) {
  TestMediaSource source("bear-1280x720-a_frag-cenc.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_Mpeg2ts_MP3Audio_Mp4a_6B) {
  TestMediaSource source("bear-audio-mp4a.6B.ts",
                         "video/mp2t; codecs=\"mp4a.6B\"", kAppendWholeFile);
#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);
#else
  EXPECT_EQ(
      DEMUXER_ERROR_COULD_NOT_OPEN,
      StartPipelineWithMediaSource(&source, kExpectDemuxerFailure, nullptr));
#endif
}

TEST_F(PipelineIntegrationTest, MSE_Mpeg2ts_MP3Audio_Mp4a_69) {
  TestMediaSource source("bear-audio-mp4a.69.ts",
                         "video/mp2t; codecs=\"mp4a.69\"", kAppendWholeFile);
#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();
  ASSERT_EQ(PIPELINE_OK, pipeline_status_);
#else
  EXPECT_EQ(
      DEMUXER_ERROR_COULD_NOT_OPEN,
      StartPipelineWithMediaSource(&source, kExpectDemuxerFailure, nullptr));
#endif
}

TEST_F(PipelineIntegrationTest,
       MSE_EncryptedPlayback_NoEncryptedFrames_MP4_CENC_AudioOnly) {
  TestMediaSource source("bear-1280x720-a_frag-cenc_clear-all.mp4",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new NoResponseApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)

// Older packagers saved sample encryption auxiliary information in the
// beginning of mdat box.
TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_MP4_CENC_MDAT_Video) {
  TestMediaSource source("bear-640x360-v_frag-cenc-mdat.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_EncryptedPlayback_MP4_CENC_SENC_Video) {
  TestMediaSource source("bear-640x360-v_frag-cenc-senc.mp4", kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

// 'SAIZ' and 'SAIO' boxes contain redundant information which is already
// available in 'SENC' box. Although 'SAIZ' and 'SAIO' boxes are required per
// CENC spec for backward compatibility reasons, but we do not use the two
// boxes if 'SENC' box is present, so the code should work even if the two
// boxes are not present.
TEST_F(PipelineIntegrationTest,
       MSE_EncryptedPlayback_MP4_CENC_SENC_NO_SAIZ_SAIO_Video) {
  TestMediaSource source("bear-640x360-v_frag-cenc-senc-no-saiz-saio.mp4",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new KeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest,
       MSE_EncryptedPlayback_MP4_CENC_KeyRotation_Video) {
  TestMediaSource source("bear-1280x720-v_frag-cenc-key_rotation.mp4",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new RotatingKeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VideoOnly_MP4_AVC3) {
  TestMediaSource source("bear-1280x720-v_frag-avc3.mp4", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();

  EXPECT_EQ(1u, pipeline_->GetBufferedTimeRanges().size());
  EXPECT_EQ(0, pipeline_->GetBufferedTimeRanges().start(0).InMilliseconds());
  EXPECT_EQ(k1280IsoAVC3FileDurationMs,
            pipeline_->GetBufferedTimeRanges().end(0).InMilliseconds());

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}
#endif  // BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)

TEST_F(PipelineIntegrationTest,
       MSE_EncryptedPlayback_MP4_CENC_KeyRotation_Audio) {
  TestMediaSource source("bear-1280x720-a_frag-cenc-key_rotation.mp4",
                         kAppendWholeFile);
  FakeEncryptedMedia encrypted_media(new RotatingKeyProvidingApp());
  EXPECT_EQ(PIPELINE_OK,
            StartPipelineWithEncryptedMedia(&source, &encrypted_media));

  source.EndOfStream();

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
}

TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VideoOnly_MP4_HEVC) {
  // HEVC demuxing might be enabled even on platforms that don't support HEVC
  // decoding. For those cases we'll get DECODER_ERROR_NOT_SUPPORTED, which
  // indicates indicates that we did pass media mime type checks and attempted
  // to actually demux and decode the stream. On platforms that support both
  // demuxing and decoding we'll get PIPELINE_OK.
  const char kMp4HevcVideoOnly[] = "video/mp4; codecs=\"hvc1.1.6.L93.B0\"";
  TestMediaSource source("bear-320x240-v_frag-hevc.mp4", kMp4HevcVideoOnly,
                         kAppendWholeFile);
#if BUILDFLAG(ENABLE_PLATFORM_HEVC)
  PipelineStatus status = StartPipelineWithMediaSource(&source);
  EXPECT_TRUE(status == PIPELINE_OK || status == DECODER_ERROR_NOT_SUPPORTED);
#else
  EXPECT_EQ(
      DEMUXER_ERROR_COULD_NOT_OPEN,
      StartPipelineWithMediaSource(&source, kExpectDemuxerFailure, nullptr));
#endif  // BUILDFLAG(ENABLE_PLATFORM_HEVC)
}

// Same test as above but using a different mime type.
TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_VideoOnly_MP4_HEV1) {
  const char kMp4Hev1VideoOnly[] = "video/mp4; codecs=\"hev1.1.6.L93.B0\"";
  TestMediaSource source("bear-320x240-v_frag-hevc.mp4", kMp4Hev1VideoOnly,
                         kAppendWholeFile);
#if BUILDFLAG(ENABLE_PLATFORM_HEVC)
  PipelineStatus status = StartPipelineWithMediaSource(&source);
  EXPECT_TRUE(status == PIPELINE_OK || status == DECODER_ERROR_NOT_SUPPORTED);
#else
  EXPECT_EQ(
      DEMUXER_ERROR_COULD_NOT_OPEN,
      StartPipelineWithMediaSource(&source, kExpectDemuxerFailure, nullptr));
#endif  // BUILDFLAG(ENABLE_PLATFORM_HEVC)
}

#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)

TEST_F(PipelineIntegrationTest, SeekWhilePaused) {
#if BUILDFLAG(IS_MAC)
  // Enable scoped logs to help track down hangs.  http://crbug.com/1014646
  ScopedVerboseLogEnabler scoped_log_enabler;
#endif

  // This test is flaky without kNoClockless, see crbug.com/796250.
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kNoClockless));

  base::TimeDelta duration(pipeline_->GetMediaDuration());
  base::TimeDelta start_seek_time(duration / 4);
  base::TimeDelta seek_time(duration * 3 / 4);

  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(start_seek_time));
  Pause();
  ASSERT_TRUE(Seek(seek_time));
  EXPECT_EQ(seek_time, pipeline_->GetMediaTime());
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  // Make sure seeking after reaching the end works as expected.
  Pause();
  ASSERT_TRUE(Seek(seek_time));
  EXPECT_EQ(seek_time, pipeline_->GetMediaTime());
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, SeekWhilePlaying) {
#if BUILDFLAG(IS_MAC)
  // Enable scoped logs to help track down hangs.  http://crbug.com/1014646
  ScopedVerboseLogEnabler scoped_log_enabler;
#endif

  // This test is flaky without kNoClockless, see crbug.com/796250.
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm", kNoClockless));

  base::TimeDelta duration(pipeline_->GetMediaDuration());
  base::TimeDelta start_seek_time(duration / 4);
  base::TimeDelta seek_time(duration * 3 / 4);

  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(start_seek_time));
  ASSERT_TRUE(Seek(seek_time));
  EXPECT_GE(pipeline_->GetMediaTime(), seek_time);
  ASSERT_TRUE(WaitUntilOnEnded());

  // Make sure seeking after reaching the end works as expected.
  ASSERT_TRUE(Seek(seek_time));
  EXPECT_GE(pipeline_->GetMediaTime(), seek_time);
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, SuspendWhilePaused) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm"));

  base::TimeDelta duration(pipeline_->GetMediaDuration());
  base::TimeDelta start_seek_time(duration / 4);
  base::TimeDelta seek_time(duration * 3 / 4);

  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(start_seek_time));
  Pause();

  // Suspend while paused.
  ASSERT_TRUE(Suspend());

  // Resuming the pipeline will create a new Renderer,
  // which in turn will trigger video size and opacity notifications.
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(320, 240))).Times(1);
  EXPECT_CALL(*this, OnVideoOpacityChange(true)).Times(1);

  ASSERT_TRUE(Resume(seek_time));
  EXPECT_GE(pipeline_->GetMediaTime(), seek_time);
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, SuspendWhilePlaying) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240.webm"));

  base::TimeDelta duration(pipeline_->GetMediaDuration());
  base::TimeDelta start_seek_time(duration / 4);
  base::TimeDelta seek_time(duration * 3 / 4);

  Play();
  ASSERT_TRUE(WaitUntilCurrentTimeIsAfter(start_seek_time));
  ASSERT_TRUE(Suspend());

  // Resuming the pipeline will create a new Renderer,
  // which in turn will trigger video size and opacity notifications.
  EXPECT_CALL(*this, OnVideoNaturalSizeChange(gfx::Size(320, 240))).Times(1);
  EXPECT_CALL(*this, OnVideoOpacityChange(true)).Times(1);

  ASSERT_TRUE(Resume(seek_time));
  EXPECT_GE(pipeline_->GetMediaTime(), seek_time);
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Verify audio decoder & renderer can handle aborted demuxer reads.
TEST_F(PipelineIntegrationTest, MSE_ChunkDemuxerAbortRead_AudioOnly) {
  ASSERT_TRUE(TestSeekDuringRead("bear-320x240-audio-only.webm", 16384,
                                 base::Milliseconds(464),
                                 base::Milliseconds(617), 0x10CA, 19730));
}

// Verify video decoder & renderer can handle aborted demuxer reads.
TEST_F(PipelineIntegrationTest, MSE_ChunkDemuxerAbortRead_VideoOnly) {
  ASSERT_TRUE(TestSeekDuringRead("bear-320x240-video-only.webm", 32768,
                                 base::Milliseconds(167),
                                 base::Milliseconds(1668), 0x1C896, 65536));
}

TEST_F(PipelineIntegrationTest,
       BasicPlayback_AudioOnly_Opus_4ch_ChannelMapping2_WebM) {
  ASSERT_EQ(
      PIPELINE_OK,
      Start("bear-opus-end-trimming-4ch-channelmapping2.webm", kWebAudio));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest,
       BasicPlayback_AudioOnly_Opus_11ch_ChannelMapping2_WebM) {
  ASSERT_EQ(
      PIPELINE_OK,
      Start("bear-opus-end-trimming-11ch-channelmapping2.webm", kWebAudio));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Verify that VP9 video in WebM containers can be played back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VideoOnly_VP9_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp9.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

#if BUILDFLAG(ENABLE_AV1_DECODER)
TEST_F(PipelineIntegrationTest, BasicPlayback_VideoOnly_AV1_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-av1.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}
#endif

// Verify that VP9 video and Opus audio in the same WebM container can be played
// back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VP9_Opus_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp9-opus.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Verify that VP8 video with alpha channel can be played back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VP8A_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp8a.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_I420A);
}

// Verify that VP8A video with odd width/height can be played back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VP8A_Odd_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp8a-odd-dimensions.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_I420A);
}

// Verify that VP9 video with odd width/height can be played back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VP9_Odd_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp9-odd-dimensions.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Verify that VP9 video with alpha channel can be played back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VP9A_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp9a.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_I420A);
}

// Verify that VP9A video with odd width/height can be played back.
TEST_F(PipelineIntegrationTest, BasicPlayback_VP9A_Odd_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp9a-odd-dimensions.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_I420A);
}

// Verify that VP9 video with 4:4:4 subsampling can be played back.
TEST_F(PipelineIntegrationTest, P444_VP9_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x240-P444.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_I444);
}

// Verify that frames of VP9 video in the BT.709 color space have the YV12HD
// format.
TEST_F(PipelineIntegrationTest, BT709_VP9_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-vp9-bt709.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_format_, PIXEL_FORMAT_I420);
  EXPECT_EQ(last_video_frame_color_space_, gfx::ColorSpace::CreateREC709());
}

TEST_F(PipelineIntegrationTest, HD_VP9_WebM) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-1280x720.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Verify that videos with an odd frame size playback successfully.
TEST_F(PipelineIntegrationTest, BasicPlayback_OddVideoSize) {
  ASSERT_EQ(PIPELINE_OK, Start("butterfly-853x480.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Verify that OPUS audio in a webm which reports a 44.1kHz sample rate plays
// correctly at 48kHz
TEST_F(PipelineIntegrationTest, BasicPlayback_Opus441kHz) {
  ASSERT_EQ(PIPELINE_OK, Start("sfx-opus-441.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());

  EXPECT_EQ(48000, demuxer_->GetFirstStream(DemuxerStream::AUDIO)
                       ->audio_decoder_config()
                       .samples_per_second());
}

// Same as above but using MediaSource.
TEST_F(PipelineIntegrationTest, MSE_BasicPlayback_Opus441kHz) {
  TestMediaSource source("sfx-opus-441.webm", kAppendWholeFile);
  EXPECT_EQ(PIPELINE_OK, StartPipelineWithMediaSource(&source));
  source.EndOfStream();
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  source.Shutdown();
  Stop();
  EXPECT_EQ(48000, demuxer_->GetFirstStream(DemuxerStream::AUDIO)
                       ->audio_decoder_config()
                       .samples_per_second());
}

// Ensures audio-only playback with missing or negative timestamps works.  Tests
// the common live-streaming case for chained ogg.  See http://crbug.com/396864.
TEST_F(PipelineIntegrationTest, BasicPlaybackChainedOgg) {
  ASSERT_EQ(PIPELINE_OK, Start("double-sfx.ogg", kUnreliableDuration));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  ASSERT_EQ(base::TimeDelta(), demuxer_->GetStartTime());
}

TEST_F(PipelineIntegrationTest, TrailingGarbage) {
  ASSERT_EQ(PIPELINE_OK, Start("trailing-garbage.mp3"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackPositiveStartTime) {
  ASSERT_EQ(PIPELINE_OK, Start("nonzero-start-time.webm"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  ASSERT_EQ(base::Microseconds(396000), demuxer_->GetStartTime());
}

#if BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)

// Tests that we signal ended even when audio runs longer than video track.
TEST_F(PipelineIntegrationTest, BasicPlaybackAudioLongerThanVideo) {
  ASSERT_EQ(PIPELINE_OK, Start("bear_audio_longer_than_video_vp8.ogv"));
  // Audio track is 2000ms. Video track is 1001ms. Duration should be higher
  // of the two.
  EXPECT_EQ(2000, pipeline_->GetMediaDuration().InMilliseconds());
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

// Tests that we signal ended even when audio runs shorter than video track.
TEST_F(PipelineIntegrationTest, BasicPlaybackAudioShorterThanVideo) {
  ASSERT_EQ(PIPELINE_OK, Start("bear_audio_shorter_than_video_vp8.ogv"));
  // Audio track is 500ms. Video track is 1001ms. Duration should be higher of
  // the two.
  EXPECT_EQ(1001, pipeline_->GetMediaDuration().InMilliseconds());
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
}

#if BUILDFLAG(USE_PROPRIETARY_CODECS)
TEST_F(PipelineIntegrationTest, NegativeVideoTimestamps) {
  ASSERT_EQ(PIPELINE_OK,
            Start("sync2-trimmed.mp4", kHashed | kUnreliableDuration));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ("dd059004f04a4d7a910123e5b306639a4559b5d7df7c0879d523649ff12a8a43",
            GetVideoHash());
  EXPECT_AUDIO_HASH("89.10,30.04,90.81,29.89,89.55,29.20,");
}

TEST_F(PipelineIntegrationTest, Rotated_Metadata_0) {
  ASSERT_EQ(PIPELINE_OK, Start("bear_rotate_0.mp4"));
  ASSERT_EQ(VIDEO_ROTATION_0,
            metadata_.video_decoder_config.video_transformation().rotation);
}

TEST_F(PipelineIntegrationTest, Rotated_Metadata_90) {
  ASSERT_EQ(PIPELINE_OK, Start("bear_rotate_90.mp4"));
  ASSERT_EQ(VIDEO_ROTATION_90,
            metadata_.video_decoder_config.video_transformation().rotation);
}

TEST_F(PipelineIntegrationTest, Rotated_Metadata_180) {
  ASSERT_EQ(PIPELINE_OK, Start("bear_rotate_180.mp4"));
  ASSERT_EQ(VIDEO_ROTATION_180,
            metadata_.video_decoder_config.video_transformation().rotation);
}

TEST_F(PipelineIntegrationTest, Rotated_Metadata_270) {
  ASSERT_EQ(PIPELINE_OK, Start("bear_rotate_270.mp4"));
  ASSERT_EQ(VIDEO_ROTATION_270,
            metadata_.video_decoder_config.video_transformation().rotation);
}

TEST_F(PipelineIntegrationTest, Spherical) {
  ASSERT_EQ(PIPELINE_OK, Start("spherical.mp4", kHashed));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ("ebb0ad0c2205c9c797f4303b0ed698a07c647c2f37692f227c12875591885877",
            GetVideoHash());
}

TEST_F(PipelineIntegrationTest, BasicPlaybackHi10P) {
  ASSERT_EQ(PIPELINE_OK, Start("bear-320x180-hi10p.mp4"));

  Play();

  ASSERT_TRUE(WaitUntilOnEnded());
}

#if BUILDFLAG(ENABLE_HLS_DEMUXER)
TEST_F(PipelineIntegrationTest, HLSMediaPlaylistTSavc1) {
  base::test::ScopedFeatureList enable_hls{kBuiltInHlsPlayer};
  ASSERT_EQ(PIPELINE_OK, StartPipelineWithHlsManifest("hls/mp_ts_avc1.m3u8"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ("00df0aa6796123f535402c39c20baa3fe1c2ad02fe1f015f84878977b30931d2",
            GetVideoHash());
}
#endif

// Verify that full-range H264 video has the right color space.
TEST_F(PipelineIntegrationTest, Fullrange_H264) {
  ASSERT_EQ(PIPELINE_OK, Start("blackwhite_yuvj420p.mp4"));
  Play();
  ASSERT_TRUE(WaitUntilOnEnded());
  EXPECT_EQ(last_video_frame_color_space_, gfx::ColorSpace::CreateJpeg());
}
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)
#endif  // BUILDFLAG(ENABLE_FFMPEG_VIDEO_DECODERS)

}  // namespace media
