// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/alert/tab_alert_icon.h"

#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/alert/tab_alert.h"
#include "chrome/common/chrome_features.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/models/image_model.h"

#if BUILDFLAG(ENABLE_GLIC)
#include "chrome/browser/glic/browser_ui/glic_vector_icon_manager.h"
#endif

namespace tabs {

ui::ColorId GetAlertIndicatorColor(TabAlert state,
                                   bool is_tab_active,
                                   bool is_frame_active) {
  int group = 0;
  switch (state) {
    case tabs::TabAlert::MEDIA_RECORDING:
    case tabs::TabAlert::AUDIO_RECORDING:
    case tabs::TabAlert::VIDEO_RECORDING:
    case tabs::TabAlert::DESKTOP_CAPTURING:
      group = 0;
      break;
    case tabs::TabAlert::TAB_CAPTURING:
    case tabs::TabAlert::PIP_PLAYING:
    case tabs::TabAlert::GLIC_ACCESSING:
    case tabs::TabAlert::GLIC_SHARING:
      group = 1;
      break;
    case tabs::TabAlert::AUDIO_PLAYING:
    case tabs::TabAlert::AUDIO_MUTING:
    case tabs::TabAlert::BLUETOOTH_CONNECTED:
    case tabs::TabAlert::BLUETOOTH_SCAN_ACTIVE:
    case tabs::TabAlert::USB_CONNECTED:
    case tabs::TabAlert::HID_CONNECTED:
    case tabs::TabAlert::SERIAL_CONNECTED:
    case tabs::TabAlert::VR_PRESENTING_IN_HEADSET:
      group = 2;
      break;
    default:
      NOTREACHED() << "Unknown tab alert state";
  }

  static constexpr std::array<std::array<std::array<ui::ColorId, 2>, 2>, 3>
      color_ids{{{{{kColorTabAlertMediaRecordingInactiveFrameInactive,
                    kColorTabAlertMediaRecordingInactiveFrameActive},
                   {kColorTabAlertMediaRecordingActiveFrameInactive,
                    kColorTabAlertMediaRecordingActiveFrameActive}}},
                 {{{kColorTabAlertPipPlayingInactiveFrameInactive,
                    kColorTabAlertPipPlayingInactiveFrameActive},
                   {kColorTabAlertPipPlayingActiveFrameInactive,
                    kColorTabAlertPipPlayingActiveFrameActive}}},
                 {{{kColorTabAlertAudioPlayingInactiveFrameInactive,
                    kColorTabAlertAudioPlayingInactiveFrameActive},
                   {kColorTabAlertAudioPlayingActiveFrameInactive,
                    kColorTabAlertAudioPlayingActiveFrameActive}}}}};
  return color_ids[group][is_tab_active][is_frame_active];
}

const gfx::VectorIcon& GetAlertIcon(TabAlert alert_state) {
  switch (alert_state) {
    case TabAlert::AUDIO_PLAYING:
      return vector_icons::kVolumeUpChromeRefreshIcon;
    case TabAlert::AUDIO_MUTING:
      return vector_icons::kVolumeOffChromeRefreshIcon;
    case TabAlert::MEDIA_RECORDING:
    case TabAlert::AUDIO_RECORDING:
    case TabAlert::VIDEO_RECORDING:
    case TabAlert::DESKTOP_CAPTURING:
      return vector_icons::kRadioButtonCheckedIcon;
    case TabAlert::TAB_CAPTURING:
      return vector_icons::kCaptureIcon;
    case TabAlert::BLUETOOTH_CONNECTED:
      return vector_icons::kBluetoothConnectedIcon;
    case TabAlert::BLUETOOTH_SCAN_ACTIVE:
      return vector_icons::kBluetoothScanningChromeRefreshIcon;
    case TabAlert::USB_CONNECTED:
      return vector_icons::kUsbChromeRefreshIcon;
    case TabAlert::HID_CONNECTED:
      return vector_icons::kVideogameAssetChromeRefreshIcon;
    case TabAlert::SERIAL_CONNECTED:
      return vector_icons::kSerialPortChromeRefreshIcon;
    case TabAlert::PIP_PLAYING:
      return vector_icons::kPictureInPictureAltIcon;
    case TabAlert::VR_PRESENTING_IN_HEADSET:
      return vector_icons::kCardboardIcon;
    case TabAlert::GLIC_ACCESSING:
    case TabAlert::GLIC_SHARING:
#if BUILDFLAG(ENABLE_GLIC)
      return glic::GlicVectorIconManager::GetVectorIcon(
          IDR_GLIC_ACCESSING_ICON);
#else
      return kTvIcon;
#endif
  }
}

ui::ImageModel GetAlertImageModel(TabAlert alert_state,
                                  ui::ColorId icon_color) {
  // Tab capturing icon uses a different width compared to
  // the other tab alert indicator icons.
  const int image_width =
      GetLayoutConstant(alert_state == tabs::TabAlert::TAB_CAPTURING
                            ? TAB_ALERT_INDICATOR_CAPTURE_ICON_WIDTH
                            : TAB_ALERT_INDICATOR_ICON_WIDTH);

  return ui::ImageModel::FromVectorIcon(GetAlertIcon(alert_state), icon_color,
                                        image_width);
}

}  // namespace tabs
