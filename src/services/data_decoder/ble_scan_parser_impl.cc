// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/data_decoder/ble_scan_parser_impl.h"

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "services/data_decoder/ble_scan_parser/parser.h"

namespace data_decoder {

namespace {

// Definitions of the data type flags:
// https://www.bluetooth.com/specifications/assigned-numbers/generic-access-profile/
constexpr uint8_t kDataTypeFlags = 0x01;
constexpr uint8_t kDataTypeServiceUuids16BitPartial = 0x02;
constexpr uint8_t kDataTypeServiceUuids16BitComplete = 0x03;
constexpr uint8_t kDataTypeServiceUuids32BitPartial = 0x04;
constexpr uint8_t kDataTypeServiceUuids32BitComplete = 0x05;
constexpr uint8_t kDataTypeServiceUuids128BitPartial = 0x06;
constexpr uint8_t kDataTypeServiceUuids128BitComplete = 0x07;
constexpr uint8_t kDataTypeLocalNameShort = 0x08;
constexpr uint8_t kDataTypeLocalNameComplete = 0x09;
constexpr uint8_t kDataTypeTxPowerLevel = 0x0A;
constexpr uint8_t kDataTypeServiceData = 0x16;
constexpr uint8_t kDataTypeManufacturerData = 0xFF;

constexpr char kUuidPrefix[] = "0000";
constexpr char kUuidSuffix[] = "-0000-1000-8000-00805F9B34FB";

BASE_FEATURE(kUseRustBleScanParser,
             "UseRustBleScanParser",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace

BleScanParserImpl::BleScanParserImpl() = default;

BleScanParserImpl::~BleScanParserImpl() = default;

void BleScanParserImpl::Parse(const std::vector<uint8_t>& advertisement_data,
                              ParseCallback callback) {
  mojom::ScanRecordPtr result;
  if (base::FeatureList::IsEnabled(kUseRustBleScanParser)) {
    result = ble_scan_parser::Parse(advertisement_data);
  } else {
    result = ParseBleScan(advertisement_data);
  }
  if (result) {
    base::UmaHistogramBoolean("Bluetooth.LocalNameIsUtf8",
                              base::IsStringUTF8(result->advertisement_name));
  }
  std::move(callback).Run(std::move(result));
}

mojom::ScanRecordPtr BleScanParserImpl::ParseBleScan(
    base::span<const uint8_t> advertisement_data) {
  int8_t tx_power = 0;
  std::string advertisement_name;
  std::vector<device::BluetoothUUID> service_uuids;
  base::flat_map<device::BluetoothUUID, std::vector<uint8_t>> service_data_map;
  base::flat_map<uint16_t, std::vector<uint8_t>> manufacturer_data_map;

  int advertising_flags = -1;

  // A reference for BLE advertising data: https://bit.ly/2DUTnsk
  for (size_t i = 0; i < advertisement_data.size();) {
    size_t length = advertisement_data[i++];
    if (length <= 1 || length > advertisement_data.size() - i) {
      return nullptr;
    }

    // length includes the field_type byte.
    size_t data_length = length - 1;
    uint8_t field_type = advertisement_data[i++];

    switch (field_type) {
      case kDataTypeFlags:
        advertising_flags = advertisement_data[i];
        break;
      case kDataTypeServiceUuids16BitPartial:
      case kDataTypeServiceUuids16BitComplete:
        if (!ParseServiceUuids(advertisement_data.subspan(i, data_length),
                               UuidFormat::kFormat16Bit, &service_uuids)) {
          return nullptr;
        }
        break;
      case kDataTypeServiceUuids32BitPartial:
      case kDataTypeServiceUuids32BitComplete:
        if (!ParseServiceUuids(advertisement_data.subspan(i, data_length),
                               UuidFormat::kFormat32Bit, &service_uuids)) {
          return nullptr;
        }
        break;
      case kDataTypeServiceUuids128BitPartial:
      case kDataTypeServiceUuids128BitComplete:
        if (!ParseServiceUuids(advertisement_data.subspan(i, data_length),
                               UuidFormat::kFormat128Bit, &service_uuids)) {
          return nullptr;
        }
        break;
      case kDataTypeLocalNameShort:
      case kDataTypeLocalNameComplete: {
        base::span<const uint8_t> s =
            advertisement_data.subspan(i, data_length);
        advertisement_name = std::string(s.begin(), s.end());
        break;
      }
      case kDataTypeTxPowerLevel:
        tx_power = advertisement_data[i];
        break;
      case kDataTypeServiceData: {
        if (data_length < 4) {
          return nullptr;
        }

        base::span<const uint8_t> uuid = advertisement_data.subspan(i, 2u);
        base::span<const uint8_t> data =
            advertisement_data.subspan(i + 2, data_length - 2);
        auto parsed_uuid = ParseUuid(uuid, UuidFormat::kFormat16Bit);
        if (!parsed_uuid.IsValid()) {
          return nullptr;
        }
        service_data_map[parsed_uuid] =
            std::vector<uint8_t>(data.begin(), data.end());
        break;
      }
      case kDataTypeManufacturerData: {
        if (data_length < 4) {
          return nullptr;
        }

        uint16_t manufacturer_key = (advertisement_data[i + 1] << 8);
        manufacturer_key += advertisement_data[i];
        base::span<const uint8_t> s =
            advertisement_data.subspan(i + 2, data_length - 2);
        manufacturer_data_map[manufacturer_key] =
            std::vector<uint8_t>(s.begin(), s.end());
        break;
      }
      default:
        // Just ignore. We don't handle other data types.
        break;
    }

    i += data_length;
  }

  return mojom::ScanRecord::New(advertising_flags, tx_power, advertisement_name,
                                service_uuids, service_data_map,
                                manufacturer_data_map);
}

device::BluetoothUUID BleScanParserImpl::ParseUuid(
    base::span<const uint8_t> bytes,
    UuidFormat format) {
  size_t length = bytes.size();
  if (!(format == UuidFormat::kFormat16Bit && length == 2) &&
      !(format == UuidFormat::kFormat32Bit && length == 4) &&
      !(format == UuidFormat::kFormat128Bit && length == 16)) {
    return device::BluetoothUUID();
  }

  std::vector<uint8_t> reversed(bytes.rbegin(), bytes.rend());
  std::string uuid = base::HexEncode(reversed);

  switch (format) {
    case UuidFormat::kFormat16Bit:
      return device::BluetoothUUID(
          base::StrCat({kUuidPrefix, uuid, kUuidSuffix}));
    case UuidFormat::kFormat32Bit:
      return device::BluetoothUUID(base::StrCat({uuid, kUuidSuffix}));
    case UuidFormat::kFormat128Bit:
      uuid.insert(8, 1, '-');
      uuid.insert(13, 1, '-');
      uuid.insert(18, 1, '-');
      uuid.insert(23, 1, '-');
      return device::BluetoothUUID(uuid);
    case UuidFormat::kFormatInvalid:
      NOTREACHED();
  }

  NOTREACHED();
}

bool BleScanParserImpl::ParseServiceUuids(
    base::span<const uint8_t> bytes,
    UuidFormat format,
    std::vector<device::BluetoothUUID>* service_uuids) {
  size_t uuid_length = 0;
  switch (format) {
    case UuidFormat::kFormat16Bit:
      uuid_length = 2;
      break;
    case UuidFormat::kFormat32Bit:
      uuid_length = 4;
      break;
    case UuidFormat::kFormat128Bit:
      uuid_length = 16;
      break;
    case UuidFormat::kFormatInvalid:
      NOTREACHED();
  }

  if (bytes.size() % uuid_length != 0) {
    return false;
  }

  for (size_t start = 0; start < bytes.size(); start += uuid_length) {
    auto uuid = ParseUuid(bytes.subspan(start, uuid_length), format);
    if (!uuid.IsValid()) {
      return false;
    }
    service_uuids->push_back(uuid);
  }

  return true;
}

}  // namespace data_decoder
