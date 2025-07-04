// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/common/wayland_object.h"

#include <alpha-compositing-unstable-v1-client-protocol.h>
#include <appmenu-client-protocol.h>
#include <chrome-color-management-client-protocol.h>
#include <content-type-v1-client-protocol.h>
#include <cursor-shape-v1-client-protocol.h>
#include <extended-drag-unstable-v1-client-protocol.h>
#include <fractional-scale-v1-client-protocol.h>
#include <gtk-primary-selection-client-protocol.h>
#include <idle-client-protocol.h>
#include <idle-inhibit-unstable-v1-client-protocol.h>
#include <keyboard-extension-unstable-v1-client-protocol.h>
#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
#include <linux-drm-syncobj-v1-client-protocol.h>
#include <overlay-prioritizer-client-protocol.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <pointer-gestures-unstable-v1-client-protocol.h>
#include <presentation-time-client-protocol.h>
#include <primary-selection-unstable-v1-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>
#include <text-input-extension-unstable-v1-client-protocol.h>
#include <text-input-unstable-v1-client-protocol.h>
#include <text-input-unstable-v3-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <wayland-client-core.h>
#include <wayland-cursor.h>
#include <wayland-drm-client-protocol.h>
#include <xdg-activation-v1-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xdg-foreign-unstable-v1-client-protocol.h>
#include <xdg-foreign-unstable-v2-client-protocol.h>
#include <xdg-output-unstable-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-toplevel-drag-v1-client-protocol.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>
#include <xx-session-management-v1-client-protocol.h>

#include "base/logging.h"

namespace wl {
namespace {

void delete_data_device(wl_data_device* data_device) {
  if (wl::get_version_of_object(data_device) >=
      WL_DATA_DEVICE_RELEASE_SINCE_VERSION) {
    wl_data_device_release(data_device);
  } else {
    wl_data_device_destroy(data_device);
  }
}

void delete_output(wl_output* output) {
  if (wl::get_version_of_object(output) >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
    wl_output_release(output);
  } else {
    wl_output_destroy(output);
  }
}

void delete_keyboard(wl_keyboard* keyboard) {
  if (wl::get_version_of_object(keyboard) >=
      WL_KEYBOARD_RELEASE_SINCE_VERSION) {
    wl_keyboard_release(keyboard);
  } else {
    wl_keyboard_destroy(keyboard);
  }
}

void delete_pointer(wl_pointer* pointer) {
  if (wl::get_version_of_object(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
    wl_pointer_release(pointer);
  } else {
    wl_pointer_destroy(pointer);
  }
}

void delete_seat(wl_seat* seat) {
  if (wl::get_version_of_object(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) {
    wl_seat_release(seat);
  } else {
    wl_seat_destroy(seat);
  }
}

void delete_touch(wl_touch* touch) {
  if (wl::get_version_of_object(touch) >= WL_TOUCH_RELEASE_SINCE_VERSION) {
    wl_touch_release(touch);
  } else {
    wl_touch_destroy(touch);
  }
}

void delete_appmenu(org_kde_kwin_appmenu* appmenu) {
  org_kde_kwin_appmenu_release(appmenu);
}

}  // namespace

bool CanBind(const std::string& interface,
             uint32_t available_version,
             uint32_t min_version,
             uint32_t max_version) {
  if (available_version < min_version) {
    LOG(WARNING) << "Unable to bind to " << interface << " version "
                 << available_version << ".  The minimum supported version is "
                 << min_version << ".";
    return false;
  }

  if (available_version > max_version) {
    LOG(WARNING) << "Binding to " << interface << " version " << max_version
                 << " but version " << available_version << " is available.";
  }

  return true;
}

void (*ObjectTraits<wl_cursor_theme>::deleter)(wl_cursor_theme*) =
    &wl_cursor_theme_destroy;

const wl_interface* ObjectTraits<wl_display>::interface = &wl_display_interface;
void (*ObjectTraits<wl_display>::deleter)(wl_display*) = &wl_display_disconnect;

const wl_interface* ObjectTraits<wl_event_queue>::interface = nullptr;
void (*ObjectTraits<wl_event_queue>::deleter)(wl_event_queue*) =
    &wl_event_queue_destroy;

const wl_interface* ObjectTraits<struct wl_proxy>::interface = nullptr;
void (*ObjectTraits<wl_proxy>::deleter)(void*) = &wl_proxy_wrapper_destroy;

// The overwhelming majority of Wayland interfaces follow the fixed pattern for
// naming their interface definition struct and their deleter function, with the
// exception for a few interfaces that use special deleter functions.  This lets
// us generate a lot of boilerplate code by two simple macros defined below.
#define IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(TYPE, DELETER) \
  const wl_interface* ObjectTraits<struct TYPE>::interface =        \
      &TYPE##_interface;                                            \
  void (*ObjectTraits<struct TYPE>::deleter)(struct TYPE*) = &DELETER;

#define IMPLEMENT_WAYLAND_OBJECT_TRAITS(TYPE) \
  IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(TYPE, TYPE##_destroy)

// For convenience, keep aphabetical order in this list.
IMPLEMENT_WAYLAND_OBJECT_TRAITS(gtk_primary_selection_device)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(gtk_primary_selection_device_manager)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(gtk_primary_selection_offer)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(gtk_primary_selection_source)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(org_kde_kwin_appmenu,
                                             delete_appmenu)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(org_kde_kwin_appmenu_manager)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(org_kde_kwin_idle)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(org_kde_kwin_idle_timeout)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(overlay_prioritizer)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(overlay_prioritized_surface)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_buffer)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_callback)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_compositor)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_data_device_manager)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(wl_data_device, delete_data_device)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_data_offer)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_data_source)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_drm)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(wl_keyboard, delete_keyboard)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(wl_output, delete_output)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(wl_pointer, delete_pointer)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_registry)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_region)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(wl_seat, delete_seat)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_shm)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_shm_pool)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_subcompositor)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_subsurface)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wl_surface)
IMPLEMENT_WAYLAND_OBJECT_TRAITS_WITH_DELETER(wl_touch, delete_touch)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_presentation)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_presentation_feedback)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_single_pixel_buffer_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_viewport)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_viewporter)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_content_type_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_content_type_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_cursor_shape_device_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_cursor_shape_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_fractional_scale_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_fractional_scale_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_linux_drm_syncobj_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_linux_drm_syncobj_surface_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(wp_linux_drm_syncobj_timeline_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_activation_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_activation_token_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_popup)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_positioner)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_surface)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_toplevel)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_toplevel_drag_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_toplevel_drag_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_toplevel_icon_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_toplevel_icon_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xdg_wm_base)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xx_toplevel_session_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xx_session_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(xx_session_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_color_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_color_management_output_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_color_management_surface_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_color_space_creator_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_color_space_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_keyboard_extension_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_extended_keyboard_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_extended_drag_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_extended_drag_source_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_extended_drag_offer_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_extended_text_input_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_blending_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zcr_alpha_compositing_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_idle_inhibit_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_idle_inhibitor_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_keyboard_shortcuts_inhibit_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_keyboard_shortcuts_inhibitor_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_linux_buffer_params_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_linux_dmabuf_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_locked_pointer_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_pointer_constraints_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_pointer_gesture_pinch_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_pointer_gesture_hold_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_pointer_gestures_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_primary_selection_device_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_primary_selection_device_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_primary_selection_offer_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_primary_selection_source_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_relative_pointer_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_relative_pointer_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_text_input_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_text_input_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_text_input_manager_v3)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zwp_text_input_v3)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_decoration_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_exporter_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_exported_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_exporter_v2)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_exported_v2)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_output_manager_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_output_v1)
IMPLEMENT_WAYLAND_OBJECT_TRAITS(zxdg_toplevel_decoration_v1)

}  // namespace wl
