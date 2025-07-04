// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_screen.h"

#include <set>
#include <vector>

#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/notimplemented.h"
#include "base/observer_list.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "components/device_event_log/device_event_log.h"
#include "ui/base/linux/linux_desktop.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/base/ui_base_features.h"
#include "ui/display/display.h"
#include "ui/display/display_finder.h"
#include "ui/display/display_list.h"
#include "ui/display/util/display_util.h"
#include "ui/display/util/gpu_info_util.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/display_color_spaces.h"
#include "ui/gfx/font_render_params.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/linux/linux_ui.h"
#include "ui/ozone/platform/wayland/host/dump_util.h"
#include "ui/ozone/platform/wayland/host/org_kde_kwin_idle.h"
#include "ui/ozone/platform/wayland/host/wayland_buffer_manager_host.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor_position.h"
#include "ui/ozone/platform/wayland/host/wayland_output.h"
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/ozone/platform/wayland/host/wayland_zcr_color_management_output.h"
#include "ui/ozone/platform/wayland/host/wayland_zcr_color_manager.h"
#include "ui/ozone/platform/wayland/host/zwp_idle_inhibit_manager.h"

#if BUILDFLAG(USE_DBUS)
#include "ui/ozone/platform/wayland/host/org_gnome_mutter_idle_monitor.h"
#endif

namespace ui {
namespace {

display::Display::Rotation WaylandTransformToRotation(int32_t transform) {
  switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
      return display::Display::ROTATE_0;
    case WL_OUTPUT_TRANSFORM_90:
      return display::Display::ROTATE_270;
    case WL_OUTPUT_TRANSFORM_180:
      return display::Display::ROTATE_180;
    case WL_OUTPUT_TRANSFORM_270:
      return display::Display::ROTATE_90;
    // ui::display::Display does not support flipped rotation.
    // see ui::display::Display::Rotation comment.
    case WL_OUTPUT_TRANSFORM_FLIPPED:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      NOTIMPLEMENTED_LOG_ONCE();
      return display::Display::ROTATE_0;
  }
  NOTREACHED();
}

}  // namespace

WaylandScreen::WaylandScreen(WaylandConnection* connection)
    : connection_(connection), weak_factory_(this) {
  DCHECK(connection_);

  // Chromium specifies either RGBA_8888 or BGRA_8888 as initial image format
  // for alpha case and RGBX_8888 for no alpha case. Figure out
  // which one is supported and use that. If RGBX_8888 is not supported, the
  // format that |have_format_alpha| uses will be used by default (RGBA_8888 or
  // BGRA_8888).
  auto buffer_formats =
      connection_->buffer_manager_host()->GetSupportedBufferFormats();
  for (const auto& buffer_format : buffer_formats) {
    auto format = buffer_format.first;

    // RGBA_8888 is the preferred format.
    if (format == gfx::BufferFormat::RGBA_8888)
      image_format_alpha_ = gfx::BufferFormat::RGBA_8888;

    if (format == gfx::BufferFormat::RGBA_F16)
      image_format_hdr_ = format;

    if (!image_format_hdr_ && format == gfx::BufferFormat::RGBA_1010102)
      image_format_hdr_ = format;

    if (!image_format_alpha_ && format == gfx::BufferFormat::BGRA_8888)
      image_format_alpha_ = gfx::BufferFormat::BGRA_8888;

    if (image_format_alpha_ && image_format_hdr_) {
      break;
    }
  }

  // If no buffer formats are found (neither wl_drm nor zwp_linux_dmabuf are
  // supported or the system has very limited set of supported buffer formats),
  // RGBA_8888 is used by default. On Wayland, that seems to be the most
  // supported.
  if (!image_format_alpha_)
    image_format_alpha_ = gfx::BufferFormat::RGBA_8888;

  // TODO(crbug.com/40719968): |image_format_no_alpha_| should use RGBX_8888
  // when it's available, but for some reason Chromium gets broken when it's
  // used. Though, we can import RGBX_8888 dma buffer to EGLImage
  // successfully. Enable that back when the issue is resolved.
  DCHECK(!image_format_no_alpha_);
  image_format_no_alpha_ = image_format_alpha_;

  if (!image_format_hdr_)
    image_format_hdr_ = image_format_alpha_;

  if (connection_->IsUiScaleEnabled() && LinuxUi::instance()) {
    OnDeviceScaleFactorChanged();
    display_scale_factor_observer_.Observe(LinuxUi::instance());
  }
}

WaylandScreen::~WaylandScreen() = default;

void WaylandScreen::OnOutputAddedOrUpdated(
    const WaylandOutput::Metrics& metrics) {
  WaylandOutput::Metrics copy = metrics;

  if (metrics.display_id == display::kInvalidDisplayId) {
    DCHECK(display_id_map_.contains(metrics.output_id));
    copy.display_id = display_id_map_[metrics.output_id];
  }

  AddOrUpdateDisplay(copy);

  DISPLAY_LOG(EVENT) << "Displays updated, count: "
                     << display_list_.displays().size();
  for (const auto& display : display_list_.displays()) {
    DISPLAY_LOG(EVENT) << display.ToString();
  }
}

void WaylandScreen::OnOutputRemoved(WaylandOutput::Id output_id) {
  DCHECK(display_id_map_.contains(output_id));
  auto iter = display_id_map_.find(output_id);
  if (iter == display_id_map_.end()) {
    return;
  }

  int64_t display_id = iter->second;
  if (display_id == GetPrimaryDisplay().id()) {
    // First, set a new primary display as required by the |display_list_|. It's
    // safe to set any of the displays to be a primary one. Once the output is
    // completely removed, Wayland updates geometry of other displays. And a
    // display, which became the one to be nearest to the origin will become a
    // primary one.
    // TODO(oshima): The server should send this info.
    for (const auto& display : display_list_.displays()) {
      if (display.id() != display_id) {
        display_list_.AddOrUpdateDisplay(display,
                                         display::DisplayList::Type::PRIMARY);
        break;
      }
    }
  }

  // The `display_id_map_` and the `display_list_` must be updated at the same
  // time to ensure internal display state is consistent. Code may otherwise
  // draw different conclusions on the availability of a display depending on
  // which of these structures are queried (see crbug.com/1408304).
  display_id_map_.erase(iter);

  auto it = display_list_.FindDisplayById(display_id);
  if (it != display_list_.displays().end())
    display_list_.RemoveDisplay(display_id);

  DISPLAY_LOG(EVENT) << "Displays updated, count: "
                     << display_list_.displays().size();
  for (const auto& display : display_list_.displays()) {
    DISPLAY_LOG(EVENT) << display.ToString();
  }
}

void WaylandScreen::AddOrUpdateDisplay(const WaylandOutput::Metrics& metrics) {
  DCHECK_NE(metrics.display_id, display::kInvalidDisplayId);
  display::Display changed_display(metrics.display_id);

  DCHECK_GE(metrics.panel_transform, WL_OUTPUT_TRANSFORM_NORMAL);
  DCHECK_LE(metrics.panel_transform, WL_OUTPUT_TRANSFORM_FLIPPED_270);
  display::Display::Rotation panel_rotation =
      WaylandTransformToRotation(metrics.panel_transform);
  changed_display.set_panel_rotation(panel_rotation);

  DCHECK_GE(metrics.logical_transform, WL_OUTPUT_TRANSFORM_NORMAL);
  DCHECK_LE(metrics.logical_transform, WL_OUTPUT_TRANSFORM_FLIPPED_270);
  display::Display::Rotation rotation =
      WaylandTransformToRotation(metrics.logical_transform);
  changed_display.set_rotation(rotation);

  gfx::Size size_in_pixels(metrics.physical_size);
  if (panel_rotation == display::Display::Rotation::ROTATE_90 ||
      panel_rotation == display::Display::Rotation::ROTATE_270) {
    size_in_pixels.Transpose();
  }
  size_in_pixels.Enlarge(-metrics.physical_overscan_insets.width(),
                         -metrics.physical_overscan_insets.height());
  changed_display.set_size_in_pixels(size_in_pixels);

  if (!metrics.logical_size.IsEmpty()) {
    changed_display.set_bounds(gfx::Rect(metrics.origin, metrics.logical_size));
    changed_display.SetScale(metrics.scale_factor);
  } else {
    // Fallback to calculating using physical size.
    // This can happen if xdg_output.logical_size was not sent.
    changed_display.SetScaleAndBounds(metrics.scale_factor,
                                      gfx::Rect(size_in_pixels));
    gfx::Rect new_bounds(changed_display.bounds());
    new_bounds.set_origin(metrics.origin);
    changed_display.set_bounds(new_bounds);
  }
  changed_display.UpdateWorkAreaFromInsets(metrics.insets);

  gfx::DisplayColorSpaces color_spaces;
  color_spaces.SetOutputBufferFormats(image_format_no_alpha_.value(),
                                      image_format_alpha_.value());
  changed_display.SetColorSpaces(color_spaces);

  // There are 2 cases where |changed_display| must be set as primary:
  // 1. When it is the first one being added to the |display_list_|. Or
  // 2. If it is nearest the origin than the previous primary or has the
  // same origin as it. When an user, for example, swaps two side-by-side
  // displays, at some point, as the notification come in, both will have
  // the same origin.
  auto type = display::DisplayList::Type::NOT_PRIMARY;
  if (display_list_.displays().empty()) {
    type = display::DisplayList::Type::PRIMARY;
  } else {
    auto nearest_origin = GetDisplayNearestPoint({0, 0}).bounds().origin();
    auto changed_origin = changed_display.bounds().origin();
    auto nearest_dist = nearest_origin.OffsetFromOrigin().LengthSquared();
    auto changed_dist = changed_origin.OffsetFromOrigin().LengthSquared();
    if (changed_dist < nearest_dist || changed_origin == nearest_origin)
      type = display::DisplayList::Type::PRIMARY;
  }

  changed_display.set_label(metrics.description);
  if (display_id_map_.find(metrics.output_id) == display_id_map_.end()) {
    display_id_map_.emplace(metrics.output_id, metrics.display_id);
  } else {
    // TODO(oshima): Change to DCHECK if stabilized.
    CHECK_EQ(display_id_map_[metrics.output_id], metrics.display_id);
  }
  display_id_map_[metrics.output_id] = metrics.display_id;
  display_list_.AddOrUpdateDisplay(changed_display, type);
}

WaylandOutput::Id WaylandScreen::GetOutputIdForDisplayId(int64_t display_id) {
  auto iter = std::find_if(
      display_id_map_.begin(), display_id_map_.end(),
      [display_id](auto pair) { return pair.second == display_id; });
  if (iter != display_id_map_.end())
    return iter->first;
  return 0;
}

WaylandOutput* WaylandScreen::GetWaylandOutputForDisplayId(int64_t display_id) {
  if (display_id == display::kInvalidDisplayId) {
    return nullptr;
  }

  auto* output_manager = connection_->wayland_output_manager();
  return output_manager->GetOutput(GetOutputIdForDisplayId(display_id));
}

WaylandOutput::Id WaylandScreen::GetOutputIdMatching(const gfx::Rect& bounds) {
  int64_t display_id = GetDisplayMatching(bounds).id();
  return GetOutputIdForDisplayId(display_id);
}

base::WeakPtr<WaylandScreen> WaylandScreen::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

const std::vector<display::Display>& WaylandScreen::GetAllDisplays() const {
  return display_list_.displays();
}

display::Display WaylandScreen::GetPrimaryDisplay() const {
  DCHECK(display_list_.IsValid());
  return display_list_.displays().empty()
             ? display::Display::GetDefaultDisplay()
             : *display_list_.GetPrimaryDisplayIterator();
}

display::Display WaylandScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  auto* window = connection_->window_manager()->GetWindow(widget);
  // A window might be destroyed by this time on shutting down the browser.
  if (!window)
    return GetPrimaryDisplay();

  const auto entered_output_id = window->GetPreferredEnteredOutputId();
  // Although spec says a surface receives enter/leave surface events on
  // create/move/resize actions, this might be called right after a window is
  // created, but it has not been configured by a Wayland compositor and it
  // has not received enter surface events yet. Another case is when a user
  // switches between displays in a single output mode - Wayland may not send
  // enter events immediately, which can result in empty container of entered
  // ids (check comments in WaylandWindow::OnEnteredOutputIdRemoved). In this
  // case, it's also safe to return the primary display.
  if (!entered_output_id.has_value())
    return GetPrimaryDisplay();

  if (display_id_map_.find(entered_output_id.value()) ==
      display_id_map_.end()) {
    DUMP_WILL_BE_NOTREACHED();
    return GetPrimaryDisplay();
  }

  int64_t display_id = display_id_map_.find(entered_output_id.value())->second;

  DCHECK(!display_list_.displays().empty());
  for (const auto& display : display_list_.displays()) {
    if (display.id() == display_id)
      return display;
  }

  NOTREACHED();
}

gfx::Point WaylandScreen::GetCursorScreenPoint() const {
  // On Wayland, neither surface nor pointer location is provided in global
  // screen coordinates system. Instead, only mouse/touch events location are
  // sent (in local surface coordinates). Given that Chromium assumes that
  // toplevel windows are located at origin when screen coordinates are not
  // available, return the last known cursor position for the currently focused
  // window, if any.
  auto* cursor_position = connection_->wayland_cursor_position();
  auto* focused_window =
      connection_->window_manager()->GetCurrentPointerOrTouchFocusedWindow();
  if (focused_window && cursor_position) {
    return gfx::ScaleToRoundedPoint(
        cursor_position->GetCursorSurfacePoint(),
        1.0f / focused_window->applied_state().ui_scale);
  }

  // Otherwise, make sure the returned point does not overlap any known window.
  auto* window = connection_->window_manager()->GetWindowWithLargestBounds();
  DCHECK(window);
  const gfx::Rect bounds = window->GetBoundsInDIP();
  return gfx::Point(bounds.width() + 10, bounds.height() + 10);
}

gfx::AcceleratedWidget WaylandScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  // It is safe to check only for focused windows and test if they contain the
  // point or not.
  auto* window =
      connection_->window_manager()->GetCurrentPointerOrTouchFocusedWindow();
  if (window && window->GetBoundsInDIP().Contains(point))
    return window->GetWidget();
  return gfx::kNullAcceleratedWidget;
}

gfx::AcceleratedWidget WaylandScreen::GetLocalProcessWidgetAtPoint(
    const gfx::Point& point,
    const std::set<gfx::AcceleratedWidget>& ignore) const {
  auto widget = GetAcceleratedWidgetAtScreenPoint(point);
  return !widget || base::Contains(ignore, widget) ? gfx::kNullAcceleratedWidget
                                                   : widget;
}

display::Display WaylandScreen::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  auto displays = GetAllDisplays();
  if (displays.size() <= 1)
    return GetPrimaryDisplay();
  return *FindDisplayNearestPoint(display_list_.displays(), point);
}

display::Display WaylandScreen::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  if (match_rect.IsEmpty())
    return GetDisplayNearestPoint(match_rect.origin());

  const display::Display* display_matching =
      display::FindDisplayWithBiggestIntersection(display_list_.displays(),
                                                  match_rect);
  return display_matching ? *display_matching : GetPrimaryDisplay();
}

std::unique_ptr<WaylandScreen::WaylandScreenSaverSuspender>
WaylandScreen::WaylandScreenSaverSuspender::Create(WaylandScreen& screen) {
  auto suspender = base::WrapUnique(new WaylandScreenSaverSuspender(screen));
  if (suspender->is_suspending_) {
    screen.screen_saver_suspension_count_++;
    return suspender;
  }

  return nullptr;
}

WaylandScreen::WaylandScreenSaverSuspender::WaylandScreenSaverSuspender(
    WaylandScreen& screen)
    : screen_(screen.GetWeakPtr()) {
  is_suspending_ = screen.SetScreenSaverSuspended(true);
}

WaylandScreen::WaylandScreenSaverSuspender::~WaylandScreenSaverSuspender() {
  if (screen_ && is_suspending_) {
    screen_->screen_saver_suspension_count_--;
    if (screen_->screen_saver_suspension_count_ == 0) {
      screen_->SetScreenSaverSuspended(false);
    }
  }
}

std::unique_ptr<PlatformScreen::PlatformScreenSaverSuspender>
WaylandScreen::SuspendScreenSaver() {
  return WaylandScreenSaverSuspender::Create(*this);
}

bool WaylandScreen::SetScreenSaverSuspended(bool suspend) {
  if (!connection_->zwp_idle_inhibit_manager())
    return false;

  if (suspend) {
    // Wayland inhibits idle behaviour on certain output, and implies that a
    // surface bound to that output should obtain the inhibitor and hold it
    // until it no longer needs to prevent the output to go idle.
    // We assume that the idle lock is initiated by the user, and therefore the
    // surface that we should use is the one owned by the window that is focused
    // currently.
    const auto* window_manager = connection_->window_manager();
    DCHECK(window_manager);
    const auto* current_window = window_manager->GetCurrentFocusedWindow();
    if (!current_window) {
      LOG(WARNING) << "Cannot inhibit going idle when no window is focused";
      return false;
    }
    DCHECK(current_window->root_surface());
    idle_inhibitor_ = connection_->zwp_idle_inhibit_manager()->CreateInhibitor(
        current_window->root_surface()->surface());
  } else {
    idle_inhibitor_.reset();
  }

  return true;
}

bool WaylandScreen::IsScreenSaverActive() const {
  return idle_inhibitor_ != nullptr;
}

base::TimeDelta WaylandScreen::CalculateIdleTime() const {
  // Try the org_kde_kwin_idle Wayland protocol extension (KWin).
  if (const auto* kde_idle = connection_->org_kde_kwin_idle()) {
    const auto idle_time = kde_idle->GetIdleTime();
    if (idle_time)
      return *idle_time;
  }

#if BUILDFLAG(USE_DBUS)
  // Try the org.gnome.Mutter.IdleMonitor D-Bus service (Mutter).
  if (!org_gnome_mutter_idle_monitor_)
    org_gnome_mutter_idle_monitor_ =
        std::make_unique<OrgGnomeMutterIdleMonitor>();
  const auto idle_time = org_gnome_mutter_idle_monitor_->GetIdleTime();
  if (idle_time)
    return *idle_time;
#endif  // BUILDFLAG(USE_DBUS)

  NOTIMPLEMENTED_LOG_ONCE();

  // No providers.  Return 0 which means the system never gets idle.
  return base::Seconds(0);
}

void WaylandScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void WaylandScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

base::Value::List WaylandScreen::GetGpuExtraInfo(
    const gfx::GpuExtraInfo& gpu_extra_info) {
  auto values = GetDesktopEnvironmentInfo();
  std::vector<std::string> protocols;
  for (const auto& protocol_and_version : connection_->available_globals()) {
    protocols.push_back(base::StringPrintf("%s:%u",
                                           protocol_and_version.first.c_str(),
                                           protocol_and_version.second));
  }
  values.Append(
      display::BuildGpuInfoEntry("Interfaces exposed by the Wayland compositor",
                                 base::JoinString(protocols, " ")));
  StorePlatformNameIntoListOfValues(values, "wayland");
  return values;
}

std::optional<float> WaylandScreen::GetPreferredScaleFactorForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  if (const auto* window = connection_->window_manager()->GetWindow(widget)) {
    // Returning null while the preferred surface scale has not been received
    // yet could lead to bugs in bounds change handling code, so default to
    // `ui_scale` in that case. Context: client code could produce a wrongly
    // scaled new frame (and commit the corresponding window state), by
    // disregarding ui scale as this is the API responsible for providing the
    // final window scale (ie: ui_scale * window_scale) to upper layers.
    return window->GetPreferredScaleFactor().value_or(1.0f) *
           window->applied_state().ui_scale;
  }
  return std::nullopt;
}

bool WaylandScreen::VerifyOutputStateConsistentForTesting() const {
  // The number of displays tracked by the display_list_ and the display_id_map_
  // should match.
  const auto& displays = display_list_.displays();
  if (display_id_map_.size() != displays.size()) {
    return false;
  }

  // Both the display_list_ and the display_id_map_ should be tracking the same
  // displays.
  for (const auto& pair : display_id_map_) {
    if (std::ranges::find(displays, pair.second, &display::Display::id) ==
        displays.end()) {
      return false;
    }
  }
  return true;
}

void WaylandScreen::OnDeviceScaleFactorChanged() {
  CHECK(connection_->IsUiScaleEnabled());
  CHECK(LinuxUi::instance());
  const auto& linux_ui = *LinuxUi::instance();
  connection_->window_manager()->SetFontScale(
      linux_ui.display_config().font_scale);
}

void WaylandScreen::DumpState(std::ostream& out) const {
  out << "WaylandScreen:" << std::endl;
  for (const auto& display : display_list_.displays()) {
    out << "  display[" << display.id() << "]:" << display.ToString()
        << std::endl;
  }
  out << "  id_map=";
  for (const auto& id_pair : display_id_map_) {
    out << "[" << id_pair.second << ":" << id_pair.first << "] ";
  }
  out << std::endl;
  out << ", screen_saver_suspension_count=" << screen_saver_suspension_count_;
}

}  // namespace ui
