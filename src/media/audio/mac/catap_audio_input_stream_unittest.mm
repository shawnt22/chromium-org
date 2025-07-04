// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "media/audio/mac/catap_audio_input_stream.h"

#import <Foundation/Foundation.h>

#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/sys_string_conversions.h"
#include "media/audio/audio_device_description.h"
#include "media/audio/audio_manager.h"
#include "media/audio/mac/audio_loopback_input_mac.h"
#include "media/audio/mac/catap_api.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

namespace {
void LogToStderr(const std::string& message) {
  LOG(ERROR) << message;
}

bool AudioObjectPropertyAddressEq(const AudioObjectPropertyAddress& x,
                                  const AudioObjectPropertyAddress& y) {
  return x.mSelector == y.mSelector && x.mScope == y.mScope &&
         x.mElement == y.mElement;
}

// A function pointer to this function is used as an identifier for the tap IO
// process ID.
OSStatus AudioDeviceIoProcIdFunction(AudioDeviceID,
                                     const AudioTimeStamp*,
                                     const AudioBufferList*,
                                     const AudioTimeStamp*,
                                     AudioBufferList*,
                                     const AudioTimeStamp*,
                                     void*) {
  return noErr;
}
constexpr AudioDeviceIOProcID kTapIoProcId = AudioDeviceIoProcIdFunction;

// Arbitrary numbers that identifies the aggregate device and tap.
constexpr AudioObjectID kAggregateDeviceId = 17;
constexpr AudioObjectID kTap = 23;

// Mock for the AudioInputCallback.
class MockAudioInputCallback : public AudioInputStream::AudioInputCallback {
 public:
  MOCK_METHOD4(OnData,
               void(const AudioBus* bus,
                    base::TimeTicks capture_time,
                    double volume,
                    const AudioGlitchInfo& glitch_info));
  MOCK_METHOD0(OnError, void());
};

// Mock for all CoreAudio API calls.
class API_AVAILABLE(macos(14.2)) MockCatapApi : public CatapApi {
 public:
  MockCatapApi() = default;
  ~MockCatapApi() override = default;

  MOCK_METHOD(OSStatus,
              AudioHardwareCreateAggregateDevice,
              (CFDictionaryRef in_device_properties, AudioDeviceID* out_device),
              (override));
  MOCK_METHOD(OSStatus,
              AudioDeviceCreateIOProcID,
              (AudioDeviceID in_device,
               AudioDeviceIOProc proc,
               void* in_client_data,
               AudioDeviceIOProcID* out_proc_id),
              (override));
  MOCK_METHOD(OSStatus,
              AudioObjectGetPropertyDataSize,
              (AudioObjectID in_object_id,
               const AudioObjectPropertyAddress* in_address,
               UInt32 in_qualifier_data_size,
               const void* in_qualifier_data,
               UInt32* ioDataSize),
              (override));
  MOCK_METHOD(OSStatus,
              AudioObjectGetPropertyData,
              (AudioObjectID in_object_id,
               const AudioObjectPropertyAddress* in_address,
               UInt32 in_qualifier_data_size,
               const void* in_qualifier_data,
               UInt32* ioDataSize,
               void* outData),
              (override));
  MOCK_METHOD(OSStatus,
              AudioObjectSetPropertyData,
              (AudioObjectID in_object_id,
               const AudioObjectPropertyAddress* in_address,
               UInt32 in_qualifier_data_size,
               const void* in_qualifier_data,
               UInt32 in_data_size,
               const void* in_data),
              (override));
  MOCK_METHOD(OSStatus,
              AudioHardwareCreateProcessTap,
              (CATapDescription * in_description, AudioObjectID* out_tap),
              (override));
  MOCK_METHOD(OSStatus,
              AudioDeviceStart,
              (AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id),
              (override));
  MOCK_METHOD(OSStatus,
              AudioDeviceStop,
              (AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id),
              (override));
  MOCK_METHOD(OSStatus,
              AudioDeviceDestroyIOProcID,
              (AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id),
              (override));
  MOCK_METHOD(OSStatus,
              AudioHardwareDestroyAggregateDevice,
              (AudioDeviceID in_device),
              (override));
  MOCK_METHOD(OSStatus,
              AudioHardwareDestroyProcessTap,
              (AudioObjectID in_tap),
              (override));
};

}  // namespace

class CatapAudioInputStreamTest : public testing::Test {
 public:
  CatapAudioInputStreamTest() = default;
  ~CatapAudioInputStreamTest() override = default;

  AudioInputStream::OpenOutcome CreateAndOpenStream(bool with_permissions) {
    if (@available(macOS 14.2, *)) {
      auto mock_catap_api_object = std::make_unique<MockCatapApi>();
      // Keep a raw pointer to set expectations.
      mock_catap_api_ = mock_catap_api_object.get();

      // Create a default CatapAudioInputStream for testing.
      stream_ = CreateCatapAudioInputStreamForTesting(
          AudioParameters(AudioParameters::AUDIO_PCM_LOW_LATENCY,
                          ChannelLayoutConfig::Stereo(), kLoopbackSampleRate,
                          kCatapLoopbackDefaultFramesPerBuffer),
          media::AudioDeviceDescription::kLoopbackInputDeviceId,
          base::BindRepeating(LogToStderr), base::DoNothing(),
          media::AudioDeviceDescription::kDefaultDeviceId,
          std::move(mock_catap_api_object));
      EXPECT_TRUE(stream_);

      // Set up expectations for a successful open.
      EXPECT_CALL(mock_catap_api(), AudioHardwareCreateProcessTap)
          .WillOnce([](CATapDescription* in_description,
                       AudioObjectID* out_tap) {
            // Default device selected.
            EXPECT_EQ(std::string(
                          base::SysNSStringToUTF8([in_description deviceUID])),
                      media::AudioDeviceDescription::kDefaultDeviceId);
            // Expect the first stream to be selected.
            EXPECT_EQ([[in_description stream] intValue], 0);
            // Not muted during capture.
            EXPECT_EQ([in_description isMuted], CATapUnmuted);
            *out_tap = kTap;
            return noErr;
          });
      EXPECT_CALL(mock_catap_api(), AudioHardwareCreateAggregateDevice)
          .WillOnce([](CFDictionaryRef in_device_properties,
                       AudioDeviceID* out_device) {
            *out_device = kAggregateDeviceId;
            return noErr;
          });
      // Add call expectation for AudioDeviceCreateIOProcID. Store the callback
      // to be able to simulate audio capture callbacks.
      EXPECT_CALL(mock_catap_api(), AudioDeviceCreateIOProcID)
          .WillOnce([this](AudioDeviceID in_device, AudioDeviceIOProc proc,
                           void* in_client_data,
                           AudioDeviceIOProcID* out_proc_id) {
            EXPECT_EQ(in_device, kAggregateDeviceId);
            audio_proc_ = proc;
            EXPECT_EQ(in_client_data, stream_);
            *out_proc_id = kTapIoProcId;
            return noErr;
          });

      // The following two calls are done when probing the tap, which is
      // enabled by default. The set property function is also used to set the
      // sample rate and frames per buffer.
      AudioObjectPropertyAddress kSampleRatePropertyAddress = {
          kAudioDevicePropertyNominalSampleRate,
          kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
      AudioObjectPropertyAddress kFramesPerBufferPropertyAddress = {
          kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
          kAudioObjectPropertyElementMain};
      AudioObjectPropertyAddress kTapDescriptionPropertyAddress = {
          kAudioTapPropertyDescription, kAudioObjectPropertyScopeGlobal,
          kAudioObjectPropertyElementMain};
      int set_sample_rate_count = 0;
      int set_frames_per_buffer_count = 0;
      int set_tap_description_count = 0;

      EXPECT_CALL(mock_catap_api(), AudioObjectGetPropertyData)
          .WillOnce(testing::Return(noErr));
      EXPECT_CALL(mock_catap_api(), AudioObjectSetPropertyData)
          .WillRepeatedly(
              [&](AudioObjectID in_object_id,
                  const AudioObjectPropertyAddress* in_address,
                  UInt32 in_qualifier_data_size, const void* in_qualifier_data,
                  UInt32 in_data_size, const void* in_data) -> OSStatus {
                if (AudioObjectPropertyAddressEq(*in_address,
                                                 kSampleRatePropertyAddress)) {
                  EXPECT_EQ(in_object_id, kAggregateDeviceId);
                  EXPECT_EQ(in_data_size, 8u);
                  EXPECT_FLOAT_EQ(*static_cast<const Float64*>(in_data),
                                  kLoopbackSampleRate);
                  ++set_sample_rate_count;
                  return noErr;
                } else if (AudioObjectPropertyAddressEq(
                               *in_address, kFramesPerBufferPropertyAddress)) {
                  EXPECT_EQ(in_object_id, kAggregateDeviceId);
                  EXPECT_EQ(in_data_size, 4u);
                  EXPECT_FLOAT_EQ(*static_cast<const UInt32*>(in_data),
                                  kCatapLoopbackDefaultFramesPerBuffer);
                  ++set_frames_per_buffer_count;
                  return noErr;
                } else if (AudioObjectPropertyAddressEq(
                               *in_address, kTapDescriptionPropertyAddress)) {
                  // Called from ProbeAudioTapPermissions.
                  EXPECT_EQ(in_object_id, kTap);
                  EXPECT_EQ(in_data_size, sizeof(CATapDescription*));
                  ++set_tap_description_count;
                  return with_permissions ? noErr : -1;
                }
                return -1;
              });

      // Initialize the stream.
      AudioInputStream::OpenOutcome result = stream_->Open();
      EXPECT_EQ(set_sample_rate_count, 1);
      EXPECT_EQ(set_frames_per_buffer_count, 1);
      EXPECT_EQ(set_tap_description_count, 1);
      return result;
    }
    return AudioInputStream::OpenOutcome::kFailed;
  }

  void TearDown() override {
    if (@available(macOS 14.2, *)) {
      if (!stream_) {
        return;
      }
      EXPECT_CALL(mock_catap_api(), AudioDeviceDestroyIOProcID)
          .WillOnce(
              [](AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id) {
                EXPECT_EQ(in_device, kAggregateDeviceId);
                EXPECT_EQ(in_proc_id, kTapIoProcId);
                return noErr;
              });
      EXPECT_CALL(mock_catap_api(), AudioHardwareDestroyAggregateDevice)
          .WillOnce([](AudioDeviceID in_device) {
            EXPECT_EQ(in_device, kAggregateDeviceId);
            return noErr;
          });
      EXPECT_CALL(mock_catap_api(), AudioHardwareDestroyProcessTap)
          .WillOnce([](AudioObjectID in_tap) {
            EXPECT_EQ(in_tap, kTap);
            return noErr;
          });
      stream_->Stop();
      stream_->Close();
      mock_catap_api_ = nullptr;
      stream_.ClearAndDelete();
    }
  }

  API_AVAILABLE(macos(14.2)) MockCatapApi& mock_catap_api() {
    return static_cast<MockCatapApi&>(*mock_catap_api_);
  }

 protected:
  raw_ptr<AudioInputStream> stream_;
  raw_ptr<CatapApi> mock_catap_api_;
  MockAudioInputCallback mock_callback_;
  AudioDeviceIOProc audio_proc_;
};

TEST_F(CatapAudioInputStreamTest, CreateAndInitializeWithPermissions) {
  if (@available(macOS 14.2, *)) {
    EXPECT_EQ(CreateAndOpenStream(/*with_permissions=*/true),
              AudioInputStream::OpenOutcome::kSuccess);
  }
}

TEST_F(CatapAudioInputStreamTest, CreateAndFailToInitializeWithoutPermissions) {
  if (@available(macOS 14.2, *)) {
    EXPECT_EQ(CreateAndOpenStream(/*with_permissions=*/false),
              AudioInputStream::OpenOutcome::kFailedSystemPermissions);
  }
}

TEST_F(CatapAudioInputStreamTest, DoubleOpenResultsInkAlreadyOpen) {
  if (@available(macOS 14.2, *)) {
    EXPECT_EQ(CreateAndOpenStream(/*with_permissions=*/true),
              AudioInputStream::OpenOutcome::kSuccess);
    EXPECT_EQ(stream_->Open(), AudioInputStream::OpenOutcome::kAlreadyOpen);
  }
}

TEST_F(CatapAudioInputStreamTest, CaptureSomeAudioData) {
  if (@available(macOS 14.2, *)) {
    EXPECT_EQ(CreateAndOpenStream(/*with_permissions=*/true),
              AudioInputStream::OpenOutcome::kSuccess);
    EXPECT_CALL(mock_callback_,
                OnData(testing::_, testing::_, testing::_, testing::_))
        .Times(testing::AtLeast(1));
    EXPECT_CALL(mock_catap_api(), AudioDeviceStart)
        .WillOnce([](AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          EXPECT_EQ(in_proc_id, kTapIoProcId);
          return noErr;
        });
    stream_->Start(&mock_callback_);
    ASSERT_NE(audio_proc_, nullptr);
    // Simulate a call to `audio_proc_` with some data.
    const AudioTimeStamp* in_now = nullptr;
    const uint32_t data_byte_size =
        kCatapLoopbackDefaultFramesPerBuffer * sizeof(Float32) * 2;
    std::vector<uint8_t> data_buffer(data_byte_size);

    AudioBufferList input_data;
    input_data.mNumberBuffers = 1;
    AudioBuffer& input_buffer = input_data.mBuffers[0];
    input_buffer.mNumberChannels = 2;
    input_buffer.mDataByteSize = data_byte_size;
    input_buffer.mData = data_buffer.data();

    AudioTimeStamp input_time;
    input_time.mFlags = kAudioTimeStampHostTimeValid;
    input_time.mHostTime = mach_absolute_time();
    AudioBufferList* output_data = nullptr;
    const AudioTimeStamp* output_time = nullptr;

    audio_proc_(0, in_now, &input_data, &input_time, output_data, output_time,
                stream_);
    EXPECT_CALL(mock_catap_api(), AudioDeviceStop)
        .WillOnce([](AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          EXPECT_EQ(in_proc_id, kTapIoProcId);
          return noErr;
        });
    stream_->Stop();
  }
}

TEST_F(CatapAudioInputStreamTest, CaptureSomeAudioDataMissingHostTime) {
  if (@available(macOS 14.2, *)) {
    EXPECT_EQ(CreateAndOpenStream(/*with_permissions=*/true),
              AudioInputStream::OpenOutcome::kSuccess);
    EXPECT_CALL(mock_callback_,
                OnData(testing::_, testing::_, testing::_, testing::_))
        .Times(testing::AtLeast(1));
    EXPECT_CALL(mock_catap_api(), AudioDeviceStart)
        .WillOnce([](AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          EXPECT_EQ(in_proc_id, kTapIoProcId);
          return noErr;
        });
    stream_->Start(&mock_callback_);
    ASSERT_NE(audio_proc_, nullptr);
    // Simulate a call to `audio_proc_` with some data.
    const AudioTimeStamp* in_now = nullptr;
    const uint32_t data_byte_size =
        kCatapLoopbackDefaultFramesPerBuffer * sizeof(Float32) * 2;
    std::vector<uint8_t> data_buffer(data_byte_size);

    AudioBufferList input_data;
    input_data.mNumberBuffers = 1;
    AudioBuffer& input_buffer = input_data.mBuffers[0];
    input_buffer.mNumberChannels = 2;
    input_buffer.mDataByteSize = data_byte_size;
    input_buffer.mData = data_buffer.data();

    AudioTimeStamp input_time;
    input_time.mFlags = kAudioTimeStampHostTimeValid;
    input_time.mHostTime = mach_absolute_time();
    AudioBufferList* output_data = nullptr;
    const AudioTimeStamp* output_time = nullptr;

    audio_proc_(0, in_now, &input_data, &input_time, output_data, output_time,
                stream_);

    // Simulate one more captured frame without a host timestamp. Expect the
    // timestamp of the next OnData() call to be incremented by one buffer
    // duration.
    base::TimeTicks previous_capture_timestamp =
        base::TimeTicks::FromMachAbsoluteTime(input_time.mHostTime);
    input_time.mFlags = 0;
    input_time.mHostTime = 0;

    EXPECT_CALL(mock_callback_, OnData)
        .WillOnce([&previous_capture_timestamp](
                      const AudioBus* source, base::TimeTicks capture_time,
                      double volume, const AudioGlitchInfo& audio_glitch_info) {
          base::TimeDelta kExpectedBufferDuration =
              base::Milliseconds(1000 * kCatapLoopbackDefaultFramesPerBuffer /
                                 kLoopbackSampleRate);
          base::TimeDelta kCaptureTimeTolerance = base::Milliseconds(1);
          EXPECT_LE((capture_time - previous_capture_timestamp -
                     kExpectedBufferDuration)
                        .magnitude(),
                    kCaptureTimeTolerance);
        });
    audio_proc_(0, in_now, &input_data, &input_time, output_data, output_time,
                stream_);

    EXPECT_CALL(mock_catap_api(), AudioDeviceStop)
        .WillOnce([](AudioDeviceID in_device, AudioDeviceIOProcID in_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          EXPECT_EQ(in_proc_id, kTapIoProcId);
          return noErr;
        });
    stream_->Stop();
  }
}

TEST_F(CatapAudioInputStreamTest, LoopbackWithoutChromeId) {
  if (@available(macOS 14.2, *)) {
    auto mock_catap_api_object = std::make_unique<MockCatapApi>();
    // Keep a raw pointer to set expectations.
    mock_catap_api_ = mock_catap_api_object.get();

    // Create a CatapAudioInputStream for testing with kLoopbackWithoutChromeId.
    stream_ = CreateCatapAudioInputStreamForTesting(
        AudioParameters(AudioParameters::AUDIO_PCM_LOW_LATENCY,
                        ChannelLayoutConfig::Stereo(), kLoopbackSampleRate,
                        kCatapLoopbackDefaultFramesPerBuffer),
        media::AudioDeviceDescription::kLoopbackWithoutChromeId,
        base::BindRepeating(LogToStderr), base::DoNothing(),
        media::AudioDeviceDescription::kDefaultDeviceId,
        std::move(mock_catap_api_object));
    EXPECT_TRUE(stream_);

    // Arbitrary number of CoreAudio process audio device IDs to be returned by
    // GetProcessAudioDeviceIds.
    constexpr UInt32 kNumberAudioDeviceIds = 2;
    // Set up expectations for a successful open.
    std::set<AudioObjectID> device_ids_to_exclude;
    EXPECT_CALL(mock_catap_api(), AudioObjectGetPropertyDataSize)
        .WillOnce([](AudioObjectID in_object_id,
                     const AudioObjectPropertyAddress* in_address,
                     UInt32 in_qualifier_data_size,
                     const void* in_qualifier_data, UInt32* ioDataSize) {
          *ioDataSize = kNumberAudioDeviceIds * sizeof(AudioDeviceID);
          return noErr;
        });

    EXPECT_CALL(mock_catap_api(), AudioObjectGetPropertyData)
        .WillOnce([](AudioObjectID in_object_id,
                     const AudioObjectPropertyAddress* in_address,
                     UInt32 in_qualifier_data_size,
                     const void* in_qualifier_data, UInt32* ioDataSize,
                     void* outData) {
          // Return a list of fake device IDs.
          base::span UNSAFE_BUFFERS(device_ids(
              static_cast<AudioDeviceID*>(outData), kNumberAudioDeviceIds));
          for (size_t i = 0; i < kNumberAudioDeviceIds; ++i) {
            device_ids[i] = i + 1;
          }
          return noErr;
        })
        .WillOnce([&device_ids_to_exclude](
                      AudioObjectID in_object_id,
                      const AudioObjectPropertyAddress* in_address,
                      UInt32 in_qualifier_data_size,
                      const void* in_qualifier_data, UInt32* ioDataSize,
                      void* outData) {
          // The second call is to get the process ID of the first device.
          // Set it to the same as the PID of the current process to simulate a
          // process audio device ID that belongs to Chrome.
          int32_t* process_id = static_cast<int32_t*>(outData);
          *process_id = getpid();
          // Store the device IDs that are returned.
          device_ids_to_exclude.insert(in_object_id);
          return noErr;
        })
        .WillOnce([](AudioObjectID in_object_id,
                     const AudioObjectPropertyAddress* in_address,
                     UInt32 in_qualifier_data_size,
                     const void* in_qualifier_data, UInt32* ioDataSize,
                     void* outData) {
          // The third call is to get the process ID of the second device. Set
          // it to something other that the PID of the current process to
          // simulate some other process audio device ID that should not be
          // excluded.
          int32_t* process_id = static_cast<int32_t*>(outData);
          *process_id = getpid() + 1;
          return noErr;
        })
        .WillOnce(testing::Return(noErr));

    EXPECT_CALL(mock_catap_api(), AudioHardwareCreateProcessTap)
        .WillOnce([&device_ids_to_exclude](CATapDescription* in_description,
                                           AudioObjectID* out_tap) {
          // Check that the CoreAudio process device IDs to exclude are properly
          // set in the tap description.
          EXPECT_EQ([in_description processes].count,
                    device_ids_to_exclude.size());
          for (NSNumber* device_id_number in [in_description processes]) {
            EXPECT_TRUE(device_ids_to_exclude.count(
                static_cast<AudioObjectID>([device_id_number intValue])));
          }
          *out_tap = kTap;
          return noErr;
        });

    EXPECT_CALL(mock_catap_api(), AudioHardwareCreateAggregateDevice)
        .WillOnce([](CFDictionaryRef in_device_properties,
                     AudioDeviceID* out_device) {
          *out_device = kAggregateDeviceId;
          return noErr;
        });
    EXPECT_CALL(mock_catap_api(), AudioDeviceCreateIOProcID)
        .WillOnce([this](AudioDeviceID in_device, AudioDeviceIOProc proc,
                         void* in_client_data,
                         AudioDeviceIOProcID* out_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          audio_proc_ = proc;
          EXPECT_EQ(in_client_data, stream_);
          *out_proc_id = kTapIoProcId;
          return noErr;
        });

    // Initialize the stream.
    EXPECT_EQ(stream_->Open(), AudioInputStream::OpenOutcome::kSuccess);
  }
}

TEST_F(CatapAudioInputStreamTest, LoopbackWithMuteDevice) {
  if (@available(macOS 14.2, *)) {
    auto mock_catap_api_object = std::make_unique<MockCatapApi>();
    // Keep a raw pointer to set expectations.
    mock_catap_api_ = mock_catap_api_object.get();

    // Create a CatapAudioInputStream for testing with
    // kLoopbackWithMuteDeviceId.
    stream_ = CreateCatapAudioInputStreamForTesting(
        AudioParameters(AudioParameters::AUDIO_PCM_LOW_LATENCY,
                        ChannelLayoutConfig::Stereo(), kLoopbackSampleRate,
                        kCatapLoopbackDefaultFramesPerBuffer),
        media::AudioDeviceDescription::kLoopbackWithMuteDeviceId,
        base::BindRepeating(LogToStderr), base::DoNothing(),
        media::AudioDeviceDescription::kDefaultDeviceId,
        std::move(mock_catap_api_object));
    EXPECT_TRUE(stream_);

    // Set up expectations for a successful open.
    EXPECT_CALL(mock_catap_api(), AudioHardwareCreateProcessTap)
        .WillOnce([](CATapDescription* in_description, AudioObjectID* out_tap) {
          EXPECT_EQ([in_description isMuted], CATapMuted);
          *out_tap = kTap;
          return noErr;
        });

    EXPECT_CALL(mock_catap_api(), AudioHardwareCreateAggregateDevice)
        .WillOnce([](CFDictionaryRef in_device_properties,
                     AudioDeviceID* out_device) {
          *out_device = kAggregateDeviceId;
          return noErr;
        });
    EXPECT_CALL(mock_catap_api(), AudioDeviceCreateIOProcID)
        .WillOnce([this](AudioDeviceID in_device, AudioDeviceIOProc proc,
                         void* in_client_data,
                         AudioDeviceIOProcID* out_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          audio_proc_ = proc;
          EXPECT_EQ(in_client_data, stream_);
          *out_proc_id = kTapIoProcId;
          return noErr;
        });

    // Initialize the stream.
    EXPECT_EQ(stream_->Open(), AudioInputStream::OpenOutcome::kSuccess);
  }
}

TEST_F(CatapAudioInputStreamTest, LoopbackWithAllDevices) {
  if (@available(macOS 14.2, *)) {
    auto mock_catap_api_object = std::make_unique<MockCatapApi>();
    // Keep a raw pointer to set expectations.
    mock_catap_api_ = mock_catap_api_object.get();

    // Create a CatapAudioInputStream for testing with
    // kLoopbackWithMuteDeviceId.
    stream_ = CreateCatapAudioInputStreamForTesting(
        AudioParameters(AudioParameters::AUDIO_PCM_LOW_LATENCY,
                        ChannelLayoutConfig::Stereo(), kLoopbackSampleRate,
                        kCatapLoopbackDefaultFramesPerBuffer),
        media::AudioDeviceDescription::kLoopbackAllDevicesId,
        base::BindRepeating(LogToStderr), base::DoNothing(),
        media::AudioDeviceDescription::kDefaultDeviceId,
        std::move(mock_catap_api_object));
    EXPECT_TRUE(stream_);

    // Set up expectations for a successful open.
    EXPECT_CALL(mock_catap_api(), AudioHardwareCreateProcessTap)
        .WillOnce([](CATapDescription* in_description, AudioObjectID* out_tap) {
          // Device UID and stream not set indicates that we're capturing all
          // devices.
          EXPECT_EQ([in_description deviceUID], nullptr);
          EXPECT_EQ([in_description stream], nullptr);
          *out_tap = kTap;
          return noErr;
        });

    EXPECT_CALL(mock_catap_api(), AudioHardwareCreateAggregateDevice)
        .WillOnce([](CFDictionaryRef in_device_properties,
                     AudioDeviceID* out_device) {
          *out_device = kAggregateDeviceId;
          return noErr;
        });
    EXPECT_CALL(mock_catap_api(), AudioDeviceCreateIOProcID)
        .WillOnce([this](AudioDeviceID in_device, AudioDeviceIOProc proc,
                         void* in_client_data,
                         AudioDeviceIOProcID* out_proc_id) {
          EXPECT_EQ(in_device, kAggregateDeviceId);
          audio_proc_ = proc;
          EXPECT_EQ(in_client_data, stream_);
          *out_proc_id = kTapIoProcId;
          return noErr;
        });

    // Initialize the stream.
    EXPECT_EQ(stream_->Open(), AudioInputStream::OpenOutcome::kSuccess);
  }
}

}  // namespace media
