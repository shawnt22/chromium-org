// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_AUDIO_AUDIO_DEVICE_ID_H_
#define CHROMEOS_ASH_COMPONENTS_AUDIO_AUDIO_DEVICE_ID_H_

#include <optional>

#include "chromeos/ash/components/audio/audio_device.h"

namespace ash {

// Gets the device id string for storing audio preference. The format of
// device string is a string consisting of 3 parts:
// |version of stable device ID| :
// |integer from lower 32 bit of device id| :
// |0(output device) or 1(input device)|
// If an audio device has both integrated input and output devices, the first 2
// parts of the string could be identical, only the last part will differentiate
// them.
// Note that |version of stable device ID| is present only for devices with
// stable device ID version >= 2. For devices with version 1, the device id
// string contains only latter 2 parts - in order to preserve backward
// compatibility with existing ID from before v2 stable device ID was
// introduced.
COMPONENT_EXPORT(CHROMEOS_ASH_COMPONENTS_AUDIO)
std::string GetVersionedDeviceIdString(const AudioDevice& device, int version);

COMPONENT_EXPORT(CHROMEOS_ASH_COMPONENTS_AUDIO)
std::string GetDeviceIdString(const AudioDevice& device);

// Gets uint64_t device stable ID from device id string generated by
// GetDeviceIdString function. Return std::nullopt if input is invalid.
COMPONENT_EXPORT(CHROMEOS_ASH_COMPONENTS_AUDIO)
std::optional<uint64_t> ParseDeviceId(const std::string& id_string);

// Converts AudioDeviceList to string containing comma separated set of
// versioned device IDs.
COMPONENT_EXPORT(CHROMEOS_ASH_COMPONENTS_AUDIO)
const std::string GetDeviceSetIdString(const AudioDeviceList& devices);

}  // namespace ash

#endif  // CHROMEOS_ASH_COMPONENTS_AUDIO_AUDIO_DEVICE_ID_H_
