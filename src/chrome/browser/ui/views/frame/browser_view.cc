// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_view.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/check_deref.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/containers/flat_set.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/notreached.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/download/bubble/download_bubble_prefs.h"
#include "chrome/browser/enterprise/data_protection/data_protection_navigation_observer.h"
#include "chrome/browser/enterprise/watermark/settings.h"
#include "chrome/browser/enterprise/watermark/watermark_view.h"
#include "chrome/browser/extensions/browser_extension_window_controller.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/feature_engagement/tracker_factory.h"
#include "chrome/browser/headless/headless_mode_util.h"
#include "chrome/browser/optimization_guide/optimization_guide_keyed_service.h"
#include "chrome/browser/optimization_guide/optimization_guide_keyed_service_factory.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/promos/promos_utils.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/sharing_hub/sharing_hub_features.h"
#include "chrome/browser/signin/chrome_signin_helper.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/autofill/autofill_bubble_base.h"
#include "chrome/browser/ui/autofill/payments/save_card_ui.h"
#include "chrome/browser/ui/bookmarks/bookmark_stats.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window_state.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/performance_controls/tab_resource_usage_tab_helper.h"
#include "chrome/browser/ui/qrcode_generator/qrcode_generator_bubble_controller.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/sad_tab_helper.h"
#include "chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller.h"
#include "chrome/browser/ui/sharing_hub/sharing_hub_bubble_view.h"
#include "chrome/browser/ui/sync/one_click_signin_links_delegate_impl.h"
#include "chrome/browser/ui/tabs/alert/tab_alert.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/collaboration_messaging_tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_utils.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/toolbar/chrome_labs/chrome_labs_utils.h"
#include "chrome/browser/ui/toolbar/pinned_toolbar/tab_search_toolbar_button_controller.h"
#include "chrome/browser/ui/toolbar/toolbar_pref_names.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/user_education/browser_user_education_interface.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/accelerator_table.h"
#include "chrome/browser/ui/views/accessibility/accessibility_focus_highlight.h"
#include "chrome/browser/ui/views/accessibility/caret_browsing_dialog_delegate.h"
#include "chrome/browser/ui/views/autofill/autofill_bubble_handler_impl.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bubble_view.h"
#include "chrome/browser/ui/views/color_provider_browser_helper.h"
#include "chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.h"
#include "chrome/browser/ui/views/download/download_in_progress_dialog_view.h"
#include "chrome/browser/ui/views/download/download_shelf_view.h"
#include "chrome/browser/ui/views/exclusive_access_bubble_views.h"
#include "chrome/browser/ui/views/extensions/extension_keybinding_registry_views.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_container.h"
#include "chrome/browser/ui/views/eye_dropper/eye_dropper.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/views/frame/app_menu_button.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/browser_view_layout_delegate.h"
#include "chrome/browser/ui/views/frame/contents_layout_manager.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_delegate.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_drop_target_controller.h"
#include "chrome/browser/ui/views/frame/native_browser_frame.h"
#include "chrome/browser/ui/views/frame/scrim_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/top_container_loading_bar.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/top_controls_slide_controller.h"
#include "chrome/browser/ui/views/frame/web_contents_close_handler.h"
#include "chrome/browser/ui/views/fullscreen_control/fullscreen_control_host.h"
#include "chrome/browser/ui/views/global_media_controls/media_toolbar_button_view.h"
#include "chrome/browser/ui/views/hats/hats_next_web_dialog.h"
#include "chrome/browser/ui/views/incognito_clear_browsing_data_dialog_coordinator.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/location_bar/intent_chip_button.h"
#include "chrome/browser/ui/views/location_bar/intent_picker_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/star_view.h"
#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_controller.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/profiles/profile_indicator_icon.h"
#include "chrome/browser/ui/views/profiles/profile_menu_coordinator.h"
#include "chrome/browser/ui/views/qrcode_generator/qrcode_generator_bubble.h"
#include "chrome/browser/ui/views/send_tab_to_self/send_tab_to_self_bubble_view.h"
#include "chrome/browser/ui/views/send_tab_to_self/send_tab_to_self_device_picker_bubble_view.h"
#include "chrome/browser/ui/views/send_tab_to_self/send_tab_to_self_promo_bubble_view.h"
#include "chrome/browser/ui/views/sharing/sharing_dialog_view.h"
#include "chrome/browser/ui/views/sharing_hub/screenshot/screenshot_captured_bubble.h"
#include "chrome/browser/ui/views/sharing_hub/sharing_hub_bubble_view_impl.h"
#include "chrome/browser/ui/views/sharing_hub/sharing_hub_icon_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/side_panel/side_panel_coordinator.h"
#include "chrome/browser/ui/views/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/views/side_panel/side_panel_rounded_corner.h"
#include "chrome/browser/ui/views/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/views/side_panel/side_panel_util.h"
#include "chrome/browser/ui/views/status_bubble_views.h"
#include "chrome/browser/ui/views/sync/one_click_signin_dialog_view.h"
#include "chrome/browser/ui/views/tab_contents/chrome_web_contents_view_focus_helper.h"
#include "chrome/browser/ui/views/tab_search_bubble_host.h"
#include "chrome/browser/ui/views/tabs/browser_tab_strip_controller.h"
#include "chrome/browser/ui/views/tabs/new_tab_button.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_search_button.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/theme_copying_widget.h"
#include "chrome/browser/ui/views/toolbar/browser_app_menu_button.h"
#include "chrome/browser/ui/views/toolbar/chrome_labs/chrome_labs_coordinator.h"
#include "chrome/browser/ui/views/toolbar/pinned_toolbar_actions_container.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/translate/translate_bubble_controller.h"
#include "chrome/browser/ui/views/translate/translate_bubble_view.h"
#include "chrome/browser/ui/views/update_recommended_message_box.h"
#include "chrome/browser/ui/views/upgrade_notification_controller.h"
#include "chrome/browser/ui/views/user_education/browser_user_education_service.h"
#include "chrome/browser/ui/views/web_apps/frame_toolbar/web_app_frame_toolbar_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_preload_manager.h"
#include "chrome/browser/ui/window_sizer/window_sizer.h"
#include "chrome/browser/user_education/user_education_service.h"
#include "chrome/browser/user_education/user_education_service_factory.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/collaboration/public/messaging/message.h"
#include "components/content_settings/core/common/features.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/feature_engagement/public/event_constants.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/tracker.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_queue.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/omnibox/browser/omnibox_popup_view.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "components/performance_manager/public/features.h"
#include "components/permissions/permission_request_manager.h"
#include "components/prefs/pref_service.h"
#include "components/reading_list/core/reading_list_pref_names.h"
#include "components/safe_browsing/core/browser/password_protection/metrics_util.h"
#include "components/search/ntp_features.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/startup_metric_utils/browser/startup_metric_utils.h"
#include "components/sync/service/sync_service.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/user_education/common/feature_promo/feature_promo_controller.h"
#include "components/user_education/common/feature_promo/feature_promo_handle.h"
#include "components/user_education/common/feature_promo/feature_promo_result.h"
#include "components/user_education/common/help_bubble/help_bubble_factory_registry.h"
#include "components/user_education/common/new_badge/new_badge_controller.h"
#include "components/user_education/common/user_education_features.h"
#include "components/user_education/views/help_bubble_view.h"
#include "components/version_info/channel.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/webapps/browser/banners/app_banner_manager.h"
#include "components/webapps/browser/banners/installable_web_app_check_result.h"
#include "components/webapps/browser/banners/web_app_banner_data.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/permission_controller.h"
#include "content/public/browser/permission_descriptor_util.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "extensions/common/command.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/platform/assistive_tech.h"
#include "ui/accessibility/platform/ax_mode_observer.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/base/theme_provider.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/window_open_disposition.h"
#include "ui/base/window_open_disposition_utils.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/display/screen.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/animation/animation_runner.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/scrollbar_size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/accessibility/view_accessibility_utils.h"
#include "ui/views/animation/compositor_animation_runner.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/external_focus_tracker.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/views_features.h"
#include "ui/views/widget/native_widget.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/sublevel_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/window/hit_test_utils.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/accelerators.h"
#include "ash/public/cpp/metrics_util.h"
#include "ash/wm/window_properties.h"
#include "chrome/browser/ui/views/frame/browser_non_client_frame_view_chromeos.h"
#include "chrome/browser/ui/views/frame/top_controls_slide_controller_chromeos.h"
#include "chrome/grit/chrome_unscaled_resources.h"
#include "chromeos/components/mgs/managed_guest_session_utils.h"
#include "chromeos/ui/frame/caption_buttons/frame_caption_button_container_view.h"
#include "chromeos/ui/frame/caption_buttons/frame_size_button.h"
#include "chromeos/ui/wm/desks/desks_helper.h"
#include "ui/compositor/compositor_metrics_tracker.h"
#else
#include "chrome/browser/ui/signin/signin_view_controller.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/fullscreen_util_mac.h"
#include "components/remote_cocoa/app_shim/application_bridge.h"
#include "components/remote_cocoa/browser/application_host.h"
#endif

#if defined(USE_AURA)
#include "chrome/browser/ui/views/theme_profile_key.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/taskbar/taskbar_decorator_win.h"
#include "chrome/browser/win/jumplist.h"
#include "chrome/browser/win/jumplist_factory.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/win/hwnd_util.h"
#include "ui/native_theme/native_theme_win.h"
#include "ui/views/win/scoped_fullscreen_visibility.h"

// To avoid conflicts with the macro from the Windows SDK...
#undef LoadAccelerators
#endif

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
#include "chrome/browser/ui/views/frame/webui_tab_strip_container_view.h"
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)

#if BUILDFLAG(ENABLE_GLIC)
#include "chrome/browser/glic/browser_ui/glic_border_view.h"
#include "chrome/browser/glic/glic_enabling.h"
#include "chrome/browser/glic/glic_keyed_service.h"
#include "chrome/browser/glic/glic_keyed_service_factory.h"
#include "chrome/browser/glic/resources/grit/glic_browser_resources.h"
#include "chrome/browser/glic/widget/glic_widget.h"
#include "chrome/browser/glic/widget/glic_window_controller.h"
#include "ui/views/layout/box_layout_view.h"
#endif

using base::UserMetricsAction;
using content::WebContents;
using input::NativeWebKeyboardEvent;
using web_modal::WebContentsModalDialogHost;

namespace {

// The name of a key to store on the window handle so that other code can
// locate this object using just the handle.
const char* const kBrowserViewKey = "__BROWSER_VIEW__";

// The visible height of the shadow above the tabs. Clicks in this area are
// treated as clicks to the frame, rather than clicks to the tab.
const int kTabShadowSize = 2;

#if BUILDFLAG(IS_CHROMEOS)
// UMA histograms that record animation smoothness for tab loading animation.
constexpr char kTabLoadingSmoothnessHistogramName[] =
    "Chrome.Tabs.AnimationSmoothness.TabLoading";

void RecordTabLoadingSmoothness(int smoothness) {
  UMA_HISTOGRAM_PERCENTAGE(kTabLoadingSmoothnessHistogramName, smoothness);
}
#endif

// See SetDisableRevealerDelayForTesting().
bool g_disable_revealer_delay_for_testing = false;

#if DCHECK_IS_ON()

std::string FocusListToString(views::View* view) {
  std::ostringstream result;
  base::flat_set<views::View*> seen_views;

  while (view) {
    if (base::Contains(seen_views, view)) {
      result << "*CYCLE TO " << view->GetClassName() << "*";
      break;
    }
    seen_views.insert(view);
    result << view->GetClassName() << " ";

    view = view->GetNextFocusableView();
  }

  return result.str();
}

void CheckFocusListForCycles(views::View* const start_view) {
  views::View* view = start_view;

  base::flat_set<views::View*> seen_views;

  while (view) {
    DCHECK(!base::Contains(seen_views, view)) << FocusListToString(start_view);
    seen_views.insert(view);

    views::View* next_view = view->GetNextFocusableView();
    if (next_view) {
      DCHECK_EQ(view, next_view->GetPreviousFocusableView())
          << view->GetClassName();
    }

    view = next_view;
  }
}

#endif  // DCHECK_IS_ON()

bool GetGestureCommand(ui::GestureEvent* event, int* command) {
  DCHECK(command);
  *command = 0;
#if BUILDFLAG(IS_MAC)
  if (event->details().type() == ui::EventType::kGestureSwipe) {
    if (event->details().swipe_left()) {
      *command = IDC_BACK;
      return true;
    } else if (event->details().swipe_right()) {
      *command = IDC_FORWARD;
      return true;
    }
  }
#endif  // BUILDFLAG(IS_MAC)
  return false;
}

bool WidgetHasChildModalDialog(views::Widget* parent_widget) {
  views::Widget::Widgets widgets =
      views::Widget::GetAllChildWidgets(parent_widget->GetNativeView());
  for (views::Widget* widget : widgets) {
    if (widget == parent_widget) {
      continue;
    }
    if (widget->IsModal()) {
      return true;
    }
  }
  return false;
}

#if BUILDFLAG(IS_CHROMEOS)
// Returns whether immmersive fullscreen should replace fullscreen. This
// should only occur for "browser-fullscreen" for tabbed-typed windows (not
// for tab-fullscreen and not for app/popup type windows).
bool ShouldUseImmersiveFullscreenForUrl(ExclusiveAccessBubbleType type) {
  // Kiosk mode needs the whole screen.
  if (IsRunningInAppMode()) {
    return false;
  }
  // An empty URL signifies browser fullscreen. Immersive is used for browser
  // fullscreen only.
  return type ==
         EXCLUSIVE_ACCESS_BUBBLE_TYPE_BROWSER_FULLSCREEN_EXIT_INSTRUCTION;
}
#endif

// Overlay view that owns TopContainerView in some cases (such as during
// immersive fullscreen reveal).
class TopContainerOverlayView : public views::View {
  METADATA_HEADER(TopContainerOverlayView, views::View)

 public:
  explicit TopContainerOverlayView(base::WeakPtr<BrowserView> browser_view)
      : browser_view_(std::move(browser_view)) {}
  ~TopContainerOverlayView() override = default;

  void ChildPreferredSizeChanged(views::View* child) override {
    // When a child of BrowserView changes its preferred size, it
    // invalidates the BrowserView's layout as well. When a child is
    // reparented under this overlay view, this doesn't happen since the
    // overlay view is owned by NonClientView.
    //
    // BrowserView's layout logic still applies in this case. To ensure
    // it is used, we must invalidate BrowserView's layout.
    if (browser_view_) {
      browser_view_->InvalidateLayout();
    }
  }

 private:
  // The BrowserView this overlay is created for. WeakPtr is used since
  // this view is held in a different hierarchy.
  base::WeakPtr<BrowserView> browser_view_;
};

BEGIN_METADATA(TopContainerOverlayView)
END_METADATA

// A view targeter for the overlay view, which makes sure the overlay view
// itself is never a target for events, but its children (i.e. top_container)
// may be.
class OverlayViewTargeterDelegate : public views::ViewTargeterDelegate {
 public:
  OverlayViewTargeterDelegate() = default;
  OverlayViewTargeterDelegate(const OverlayViewTargeterDelegate&) = delete;
  OverlayViewTargeterDelegate& operator=(const OverlayViewTargeterDelegate&) =
      delete;
  ~OverlayViewTargeterDelegate() override = default;

  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    const auto& children = target->children();
    const auto hits_child = [target, rect](const views::View* child) {
      gfx::RectF child_rect(rect);
      views::View::ConvertRectToTarget(target, child, &child_rect);
      return child->HitTestRect(gfx::ToEnclosingRect(child_rect));
    };
    return std::ranges::any_of(children, hits_child);
  }
};

// This class uses a solid background instead of a views::Separator. The latter
// is not guaranteed to fill its bounds and assumes being painted on an opaque
// background (which is why it'd be OK to only partially fill its bounds). This
// needs to fill its bounds to have the entire BrowserView painted.
class ContentsSeparator : public views::View {
  METADATA_HEADER(ContentsSeparator, views::View)

 public:
  ContentsSeparator() {
    SetBackground(
        views::CreateSolidBackground(kColorToolbarContentAreaSeparator));

    // BrowserViewLayout will respect either the height or width of this,
    // depending on orientation, not simultaneously both.
    SetPreferredSize(
        gfx::Size(views::Separator::kThickness, views::Separator::kThickness));
  }
};

BEGIN_METADATA(ContentsSeparator)
END_METADATA

bool ShouldShowWindowIcon(const Browser* browser,
                          bool app_uses_window_controls_overlay,
                          bool app_uses_tabbed) {
#if BUILDFLAG(IS_CHROMEOS)
  // For Chrome OS only, trusted windows (apps and settings) do not show a
  // window icon, crbug.com/119411. Child windows (i.e. popups) do show an icon.
  if (browser->is_trusted_source() || app_uses_window_controls_overlay) {
    return false;
  }
#else
  if (app_uses_tabbed) {
    return false;
  }
#endif
  return browser->SupportsWindowFeature(Browser::FEATURE_TITLEBAR);
}

#if BUILDFLAG(IS_MAC)
void GetAnyTabAudioStates(const Browser* browser,
                          bool* any_tab_playing_audio,
                          bool* any_tab_playing_muted_audio) {
  const TabStripModel* model = browser->tab_strip_model();
  for (int i = 0; i < model->count(); i++) {
    auto* contents = model->GetWebContentsAt(i);
    auto* helper = RecentlyAudibleHelper::FromWebContents(contents);
    if (helper && helper->WasRecentlyAudible()) {
      if (contents->IsAudioMuted()) {
        *any_tab_playing_muted_audio = true;
      } else {
        *any_tab_playing_audio = true;
      }
    }
  }
}
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_MAC)
// OverlayWidget is a child Widget of BrowserFrame used during immersive
// fullscreen on macOS that hosts the top container. Its native Window and View
// interface with macOS fullscreen APIs allowing separation of the top container
// and web contents.
// Currently the only explicit reason for OverlayWidget to be its own subclass
// is to support GetAccelerator() forwarding.
class OverlayWidget : public ThemeCopyingWidget {
 public:
  explicit OverlayWidget(views::Widget* role_model)
      : ThemeCopyingWidget(role_model) {}

  OverlayWidget(const OverlayWidget&) = delete;
  OverlayWidget& operator=(const OverlayWidget&) = delete;

  ~OverlayWidget() override = default;

  // OverlayWidget hosts the top container. Views within the top container look
  // up accelerators by asking their hosting Widget. In non-immersive fullscreen
  // that would be the BrowserFrame. Give top chrome what it expects and forward
  // GetAccelerator() calls to OverlayWidget's parent (BrowserFrame).
  bool GetAccelerator(int cmd_id, ui::Accelerator* accelerator) const override {
    DCHECK(parent());
    return parent()->GetAccelerator(cmd_id, accelerator);
  }

  // Instances of OverlayWidget do not activate directly but their views style
  // should follow the parent (browser frame) activation state. In other words,
  // when the browser frame is not activate the overlay widget views will
  // appear disabled.
  bool ShouldViewsStyleFollowWidgetActivation() const override { return true; }
};

// TabContainerOverlayView is a view that hosts the TabStripRegionView during
// immersive fullscreen. The TopContainerView usually draws the background for
// the tab strip. Since the tab strip has been reparented we need to handle
// drawing the background here.
class TabContainerOverlayView : public views::View {
  METADATA_HEADER(TabContainerOverlayView, views::View)

 public:
  explicit TabContainerOverlayView(base::WeakPtr<BrowserView> browser_view)
      : browser_view_(std::move(browser_view)) {}
  ~TabContainerOverlayView() override = default;

  //
  // views::View overrides
  //

  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor frame_color = browser_view_->frame()->GetFrameView()->GetFrameColor(
        BrowserFrameActiveState::kUseCurrent);
    canvas->DrawColor(frame_color);

    auto* theme_service =
        ThemeServiceFactory::GetForProfile(browser_view_->browser()->profile());
    if (!theme_service->UsingSystemTheme()) {
      auto* non_client_frame_view = browser_view_->frame()->GetFrameView();
      non_client_frame_view->PaintThemedFrame(canvas);
    }
  }

  //
  // `BrowserRootView` handles drag and drop for the tab strip. In immersive
  // fullscreen, the tab strip is hosted in a separate Widget, in a separate
  // view, this view` TabContainerOverlayView`. To support drag and drop for the
  // tab strip in immersive fullscreen, forward all drag and drop requests to
  // the `BrowserRootView`.
  //

  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override {
    return browser_view_->GetWidget()->GetRootView()->GetDropFormats(
        formats, format_types);
  }

  bool AreDropTypesRequired() override {
    return browser_view_->GetWidget()->GetRootView()->AreDropTypesRequired();
  }

  bool CanDrop(const ui::OSExchangeData& data) override {
    return browser_view_->GetWidget()->GetRootView()->CanDrop(data);
  }

  void OnDragEntered(const ui::DropTargetEvent& event) override {
    return browser_view_->GetWidget()->GetRootView()->OnDragEntered(event);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    return browser_view_->GetWidget()->GetRootView()->OnDragUpdated(event);
  }

  void OnDragExited() override {
    return browser_view_->GetWidget()->GetRootView()->OnDragExited();
  }

  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override {
    return browser_view_->GetWidget()->GetRootView()->GetDropCallback(event);
  }

 private:
  // The BrowserView this overlay is created for. WeakPtr is used since
  // this view is held in a different hierarchy.
  base::WeakPtr<BrowserView> browser_view_;
};

BEGIN_METADATA(TabContainerOverlayView)
END_METADATA

#else  // !BUILDFLAG(IS_MAC)

// Calls |method| which is either WebContents::Cut, ::Copy, or ::Paste on
// the given WebContents, returning true if it consumed the event.
bool DoCutCopyPasteForWebContents(content::WebContents* contents,
                                  void (content::WebContents::*method)()) {
  // It's possible for a non-null WebContents to have a null RWHV if it's
  // crashed or otherwise been killed.
  content::RenderWidgetHostView* rwhv = contents->GetRenderWidgetHostView();
  if (!rwhv || !rwhv->HasFocus()) {
    return false;
  }
  // Calling |method| rather than using a fake key event is important since a
  // fake event might be consumed by the web content.
  (contents->*method)();
  return true;
}

#endif  // BUILDFLAG(IS_MAC)

// Combines View::ConvertPointToTarget and View::HitTest for a given |point|.
// Converts |point| from |src| to |dst| and hit tests it against |dst|. The
// converted |point| can then be retrieved and used for additional tests.
bool ConvertedHitTest(views::View* src, views::View* dst, gfx::Point* point) {
  DCHECK(src);
  DCHECK(dst);
  DCHECK(point);
  views::View::ConvertPointToTarget(src, dst, point);
  return dst->HitTestPoint(*point);
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// Delegate implementation for BrowserViewLayout. Usually just forwards calls
// into BrowserView.
class BrowserViewLayoutDelegateImpl : public BrowserViewLayoutDelegate {
 public:
  explicit BrowserViewLayoutDelegateImpl(BrowserView* browser_view)
      : browser_view_(browser_view) {}
  BrowserViewLayoutDelegateImpl(const BrowserViewLayoutDelegateImpl&) = delete;
  BrowserViewLayoutDelegateImpl& operator=(
      const BrowserViewLayoutDelegateImpl&) = delete;
  ~BrowserViewLayoutDelegateImpl() override = default;

  bool ShouldDrawTabStrip() const override {
    return browser_view_->ShouldDrawTabStrip();
  }

  bool GetBorderlessModeEnabled() const override {
    return browser_view_->IsBorderlessModeEnabled();
  }

  gfx::Rect GetBoundsForTabStripRegionInBrowserView() const override {
    const gfx::Size tabstrip_minimum_size =
        browser_view_->tab_strip_region_view()->GetMinimumSize();
    gfx::RectF bounds_f(browser_view_->frame()->GetBoundsForTabStripRegion(
        tabstrip_minimum_size));
    views::View::ConvertRectToTarget(browser_view_->parent(), browser_view_,
                                     &bounds_f);
    return gfx::ToEnclosingRect(bounds_f);
  }

  gfx::Rect GetBoundsForWebAppFrameToolbarInBrowserView() const override {
    const gfx::Size web_app_frame_toolbar_preferred_size =
        browser_view_->web_app_frame_toolbar()->GetPreferredSize();
    gfx::RectF bounds_f(browser_view_->frame()->GetBoundsForWebAppFrameToolbar(
        web_app_frame_toolbar_preferred_size));
    views::View::ConvertRectToTarget(browser_view_->parent(), browser_view_,
                                     &bounds_f);
    return gfx::ToEnclosingRect(bounds_f);
  }

  int GetTopInsetInBrowserView() const override {
    // BrowserView should fill the full window when window controls overlay
    // is enabled or when immersive fullscreen with tabs is enabled.
    if (browser_view_->IsWindowControlsOverlayEnabled() ||
        browser_view_->IsBorderlessModeEnabled()) {
      return 0;
    }
#if BUILDFLAG(IS_MAC)
    if (browser_view_->UsesImmersiveFullscreenTabbedMode() &&
        browser_view_->immersive_mode_controller()->IsEnabled()) {
      return 0;
    }
#endif

    return browser_view_->frame()->GetTopInset() - browser_view_->y();
  }

  bool IsToolbarVisible() const override {
    return browser_view_->IsToolbarVisible();
  }

  bool IsBookmarkBarVisible() const override {
    return browser_view_->IsBookmarkBarVisible();
  }

  bool IsContentsSeparatorEnabled() const override {
    // Web app windows manage their own separator.
    // TODO(crbug.com/40102629): Make PWAs set the visibility of the ToolbarView
    // based on whether it is visible instead of setting the height to 0px. This
    // will enable BrowserViewLayout to hide the contents separator on its own
    // using the same logic used by normal BrowserViews.
    // The separator should not be shown when in split view.
    return !browser_view_->browser()->app_controller() && !IsActiveTabSplit();
  }

  bool IsActiveTabSplit() const override {
    // Use the model state as this can be called during active tab change
    // when the multi contents view hasn't been fully setup and this
    // inconsistency would cause unnecessary re-layout of content view during
    // tab switch.
    const tabs::TabInterface* active_tab =
        browser_view_->browser()->GetActiveTabInterface();
    return active_tab && active_tab->IsSplit();
  }

  ExclusiveAccessBubbleViews* GetExclusiveAccessBubble() const override {
    return browser_view_->exclusive_access_bubble();
  }

  bool IsTopControlsSlideBehaviorEnabled() const override {
    return browser_view_->GetTopControlsSlideBehaviorEnabled();
  }

  float GetTopControlsSlideBehaviorShownRatio() const override {
    return browser_view_->GetTopControlsSlideBehaviorShownRatio();
  }

  bool SupportsWindowFeature(Browser::WindowFeature feature) const override {
    return browser_view_->browser()->SupportsWindowFeature(feature);
  }

  gfx::NativeView GetHostViewForAnchoring() const override {
    return browser_view_->GetWidgetForAnchoring()->GetNativeView();
  }

  bool HasFindBarController() const override {
    return browser_view_->browser()->GetFeatures().HasFindBarController();
  }

  void MoveWindowForFindBarIfNecessary() const override {
    auto* const controller =
        browser_view_->browser()->GetFeatures().GetFindBarController();
    return controller->find_bar()->MoveWindowIfNecessary();
  }

  bool IsWindowControlsOverlayEnabled() const override {
    return browser_view_->IsWindowControlsOverlayEnabled();
  }

  void UpdateWindowControlsOverlay(
      const gfx::Rect& available_titlebar_area) override {
    content::WebContents* web_contents = browser_view_->GetActiveWebContents();
    if (!web_contents) {
      return;
    }

    // The rect passed to WebContents is directly exposed to websites. In case
    // of an empty rectangle, this should be exposed as 0,0 0x0 rather than
    // whatever coordinates might be in rect.
    web_contents->UpdateWindowControlsOverlay(
        available_titlebar_area.IsEmpty()
            ? gfx::Rect()
            : browser_view_->GetMirroredRect(available_titlebar_area));
  }

  bool ShouldLayoutTabStrip() const override {
#if BUILDFLAG(IS_MAC)
    // The tab strip is hosted in a separate widget in immersive fullscreen on
    // macOS.
    if (browser_view_->UsesImmersiveFullscreenTabbedMode() &&
        browser_view_->immersive_mode_controller()->IsEnabled()) {
      return false;
    }
#endif
    return true;
  }

  int GetExtraInfobarOffset() const override {
#if BUILDFLAG(IS_MAC)
    if (browser_view_->UsesImmersiveFullscreenMode() &&
        browser_view_->immersive_mode_controller()->IsEnabled()) {
      return browser_view_->immersive_mode_controller()
          ->GetExtraInfobarOffset();
    }
#endif
    return 0;
  }

 private:
  raw_ptr<BrowserView> browser_view_;
};

class BrowserView::AccessibilityModeObserver : public ui::AXModeObserver {
 public:
  explicit AccessibilityModeObserver(BrowserView* browser_view)
      : browser_view_(browser_view) {
    ax_mode_observation_.Observe(&ui::AXPlatform::GetInstance());
  }

 private:
  // ui::AXModeObserver:
  void OnAssistiveTechChanged(ui::AssistiveTech assistive_tech) override {
    // The WebUI tablet/"touchable" tabstrip is not used when a screen reader is
    // active - see `WebUITabStripContainerView::UseTouchableTabStrip()`.
    // However, updating the assistive tech state in order to read it is slow,
    // so instead of trying to it synchronously at startup, respond to updates
    // here, then pass them to the browser via post so the tabstrip state can
    // be properly updated on a fresh call stack.
    if (ui::IsScreenReader(assistive_tech)) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&BrowserView::MaybeInitializeWebUITabStrip,
                                    browser_view_->GetAsWeakPtr()));
    }
  }

  const raw_ptr<BrowserView> browser_view_;
  base::ScopedObservation<ui::AXPlatform, ui::AXModeObserver>
      ax_mode_observation_{this};
};

///////////////////////////////////////////////////////////////////////////////
// BrowserView, public:

BrowserView::BrowserView(std::unique_ptr<Browser> browser)
    : views::ClientView(nullptr, nullptr),
      browser_(std::move(browser)),
      accessibility_mode_observer_(
          std::make_unique<AccessibilityModeObserver>(this)) {
  SetShowIcon(::ShouldShowWindowIcon(
      browser_.get(), AppUsesWindowControlsOverlay(), AppUsesTabbed()));

  // In forced app mode, all size controls are always disabled. Otherwise, use
  // `create_params` to enable/disable specific size controls.
  if (IsRunningInForcedAppMode()) {
    SetHasWindowSizeControls(false);
  } else if (GetIsPictureInPictureType()) {
    // Picture in picture windows must always have a title, can never minimize,
    // and can never maximize regardless of what the params say.
    SetShowTitle(true);
    SetCanMinimize(false);
    SetCanMaximize(false);
    SetCanFullscreen(false);
    SetCanResize(true);
  } else {
    SetCanResize(browser_->create_params().can_resize);
    SetCanMaximize(browser_->create_params().can_maximize);
    SetCanFullscreen(browser_->create_params().can_fullscreen);
    SetCanMinimize(true);
  }

  SetProperty(views::kElementIdentifierKey, kBrowserViewElementId);

  // Add any legal notices required for the user to the queue.
  QueueLegalAndPrivacyNotices(browser_->profile());

  // Not all browsers do feature promos. Conditionally create one (or don't) for
  // this browser window.
  feature_promo_controller_ = CreateUserEducationResources(this);

  browser_->tab_strip_model()->AddObserver(this);
  immersive_mode_controller_ = chrome::CreateImmersiveModeController(this);

  // Top container holds tab strip region and toolbar and lives at the front of
  // the view hierarchy.

  std::unique_ptr<TabMenuModelFactory> tab_menu_model_factory;
  if (browser_->app_controller()) {
    tab_menu_model_factory =
        browser_->app_controller()->GetTabMenuModelFactory();

    UpdateWindowControlsOverlayEnabled();
    UpdateBorderlessModeEnabled();
  }

  // TabStrip takes ownership of the controller.
  auto tabstrip_controller = std::make_unique<BrowserTabStripController>(
      browser_->tab_strip_model(), this, std::move(tab_menu_model_factory));
  BrowserTabStripController* tabstrip_controller_ptr =
      tabstrip_controller.get();
  auto tabstrip = std::make_unique<TabStrip>(std::move(tabstrip_controller));
  tabstrip_ = tabstrip.get();
  tabstrip_controller_ptr->InitFromModel(tabstrip_);
  top_container_ = AddChildView(std::make_unique<TopContainerView>(this));

  if (GetIsWebAppType()) {
    web_app_frame_toolbar_ = top_container_->AddChildView(
        std::make_unique<WebAppFrameToolbarView>(this));
    top_container_->set_web_app_frame_toolbar(web_app_frame_toolbar_);
    if (ShouldShowWindowTitle()) {
      web_app_window_title_ = top_container_->AddChildView(
          std::make_unique<views::Label>(GetWindowTitle()));
      web_app_window_title_->SetID(VIEW_ID_WINDOW_TITLE);
    }
  }
  tab_strip_region_view_ = top_container_->AddChildView(
      std::make_unique<TabStripRegionView>(std::move(tabstrip)));

  ColorProviderBrowserHelper::CreateForBrowser(browser_.get());

  // Create WebViews early so |webui_tab_strip_| can observe their size.
  auto devtools_web_view =
      std::make_unique<views::WebView>(browser_->profile());
  devtools_web_view->SetID(VIEW_ID_DEV_TOOLS_DOCKED);
  devtools_web_view->SetVisible(false);

  std::unique_ptr<new_tab_footer::NewTabFooterWebView> new_tab_footer_web_view;
  if (features::IsNtpFooterEnabledWithoutSideBySide()) {
    new_tab_footer_web_view =
        std::make_unique<new_tab_footer::NewTabFooterWebView>(browser_.get());
    new_tab_footer_web_view->SetVisible(false);
  }

  auto contents_container = std::make_unique<views::View>();
  devtools_web_view_ =
      contents_container->AddChildView(std::move(devtools_web_view));

  devtools_scrim_view_ =
      contents_container->AddChildView(std::make_unique<ScrimView>());
  devtools_scrim_view_->layer()->SetName("DevtoolsScrimView");

  views::View* contents_view;
  if (base::FeatureList::IsEnabled(features::kSideBySide)) {
    auto multi_contents_view = std::make_unique<MultiContentsView>(
        this, std::make_unique<MultiContentsViewDelegateImpl>(
                  *browser_->tab_strip_model()));
    multi_contents_view_ =
        contents_container->AddChildView(std::move(multi_contents_view));
    multi_contents_view_->SetID(VIEW_ID_TAB_CONTAINER);
    contents_view = multi_contents_view_;
  } else {
    auto contents_web_view =
        std::make_unique<ContentsWebView>(browser_->profile());
    contents_web_view_ =
        contents_container->AddChildView(std::move(contents_web_view));
    contents_web_view_->SetID(VIEW_ID_TAB_CONTAINER);
    contents_web_view_->set_is_primary_web_contents_for_window(true);
    contents_view = contents_web_view_;
  }

  if (base::FeatureList::IsEnabled(ntp_features::kNtpFooter) &&
      !base::FeatureList::IsEnabled(features::kSideBySide)) {
    new_tab_footer_web_view_separator_ =
        contents_container->AddChildView(std::make_unique<ContentsSeparator>());
    new_tab_footer_web_view_separator_->SetProperty(
        views::kElementIdentifierKey, kFooterWebViewSeparatorElementId);

    new_tab_footer_web_view_ =
        contents_container->AddChildView(std::move(new_tab_footer_web_view));
  }

  // Create the view that will house the Lens overlay. This view is visible but
  // transparent view that is used as a container for the Lens overlay WebView.
  // It must have a higher index than contents_view so that it is drawn on top
  // of it. Uses a fill layout so that the overlay WebView can fill the entire
  // container.
  auto lens_overlay_view = std::make_unique<views::View>();
  lens_overlay_view->SetID(VIEW_ID_LENS_OVERLAY);
  lens_overlay_view->SetVisible(false);
  lens_overlay_view->SetLayoutManager(std::make_unique<views::FillLayout>());
  lens_overlay_view_ =
      contents_container->AddChildView(std::move(lens_overlay_view));

  contents_scrim_view_ =
      contents_container->AddChildView(std::make_unique<ScrimView>());
  contents_scrim_view_->layer()->SetName("ContentsScrimView");

#if BUILDFLAG(ENABLE_GLIC)
  // `IsProfileEligible` returns true if the feature flags are present and the
  // profile can potentially enable the feature. If the feature is disabled the
  // view will exist but never become visible.
  if (glic::GlicEnabling::IsProfileEligible(browser_->profile())) {
    glic_border_ = contents_container->AddChildView(
        views::Builder<glic::GlicBorderView>(
            glic::GlicBorderView::Factory::Create(browser_.get()))
            // https://crbug.com/387458471: By default the border view is
            // visible, meaning it will paint during the initial layout of the
            // browser UI, causing a flash of the border.
            .SetVisible(false)
            // `glic_border_` should never receive input events.
            .SetCanProcessEventsWithinSubtree(false)
            .Build());
  }
#endif
  watermark_view_ = contents_container->AddChildView(
      std::make_unique<enterprise_watermark::WatermarkView>());

#if BUILDFLAG(ENABLE_GLIC)
  contents_container->SetLayoutManager(std::make_unique<ContentsLayoutManager>(
      devtools_web_view_, devtools_scrim_view_, contents_view,
      lens_overlay_view_, contents_scrim_view_, glic_border_, watermark_view_,
      new_tab_footer_web_view_separator_, new_tab_footer_web_view_));
#else
  contents_container->SetLayoutManager(std::make_unique<ContentsLayoutManager>(
      devtools_web_view_, devtools_scrim_view_, contents_view,
      lens_overlay_view_, contents_scrim_view_, nullptr, watermark_view_,
      new_tab_footer_web_view_separator_, new_tab_footer_web_view_));
#endif

  toolbar_ = top_container_->AddChildView(
      std::make_unique<ToolbarView>(browser_.get(), this));

  contents_separator_ =
      top_container_->AddChildView(std::make_unique<ContentsSeparator>());
  contents_separator_->SetProperty(views::kElementIdentifierKey,
                                   kContentsSeparatorViewElementId);

  contents_container_ = AddChildView(std::move(contents_container));
  set_contents_view(contents_container_);

  right_aligned_side_panel_separator_ =
      AddChildView(std::make_unique<ContentsSeparator>());
  right_aligned_side_panel_separator_->SetProperty(
      views::kElementIdentifierKey,
      kRightAlignedSidePanelSeparatorViewElementId);

  const bool is_right_aligned = GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSidePanelHorizontalAlignment);
  unified_side_panel_ = AddChildView(std::make_unique<SidePanel>(
      this, is_right_aligned ? SidePanel::HorizontalAlignment::kRight
                             : SidePanel::HorizontalAlignment::kLeft));
  left_aligned_side_panel_separator_ =
      AddChildView(std::make_unique<ContentsSeparator>());
  left_aligned_side_panel_separator_->SetProperty(
      views::kElementIdentifierKey,
      kLeftAlignedSidePanelSeparatorViewElementId);
  side_panel_rounded_corner_ =
      AddChildView(std::make_unique<SidePanelRoundedCorner>(this));
  side_panel_rounded_corner_->SetProperty(views::kElementIdentifierKey,
                                          kSidePanelRoundedCornerViewElementId);

  // InfoBarContainer needs to be added as a child here for drop-shadow, but
  // needs to come after toolbar in focus order (see EnsureFocusOrder()).
  infobar_container_ =
      AddChildView(std::make_unique<InfoBarContainerView>(this));

  // Create do-nothing view for the sake of controlling the z-order of the find
  // bar widget.
  find_bar_host_view_ = AddChildView(std::make_unique<View>());

  window_scrim_view_ = AddChildView(std::make_unique<ScrimView>());
  window_scrim_view_->layer()->SetName("WindowScrimView");

  UpgradeNotificationController::CreateForBrowser(browser_.get());

#if BUILDFLAG(IS_WIN)
  // Create a custom JumpList and add it to an observer of TabRestoreService
  // so we can update the custom JumpList when a tab is added or removed.
  // JumpList is created asynchronously with a low priority to not delay the
  // startup.
  if (JumpList::Enabled()) {
    content::BrowserThread::PostBestEffortTask(
        FROM_HERE, base::SingleThreadTaskRunner::GetCurrentDefault(),
        base::BindOnce(&BrowserView::CreateJumpList,
                       weak_ptr_factory_.GetWeakPtr()));
  }
#endif

  registrar_.Init(GetProfile()->GetPrefs());
  registrar_.Add(
      prefs::kFullscreenAllowed,
      base::BindRepeating(&BrowserView::UpdateFullscreenAllowedFromPolicy,
                          base::Unretained(this), CanFullscreen()));
  UpdateFullscreenAllowedFromPolicy(CanFullscreen());

  WebUIContentsPreloadManager::GetInstance()->WarmupForBrowser(browser_.get());

  browser_->GetFeatures().InitPostBrowserViewConstruction(this);

  GetViewAccessibility().SetRole(ax::mojom::Role::kClient);

  if (GetFocusManager()) {
    focus_manager_observation_.Observe(GetFocusManager());
  }
}

BrowserView::~BrowserView() {
  browser_->GetFeatures().TearDownPreBrowserWindowDestruction();

  // Remove the layout manager to avoid dangling. This needs to be earlier than
  // other cleanups that destroy views referenced in the layout manager.
  SetLayoutManager(nullptr);

  tab_search_bubble_host_.reset();

  // Destroy the top controls slide controller first as it depends on the
  // tabstrip model and the browser frame.
  top_controls_slide_controller_.reset();

  // All the tabs should have been destroyed already. If we were closed by the
  // OS with some tabs than the NativeBrowserFrame should have destroyed them.
  DCHECK_EQ(0, browser_->tab_strip_model()->count());

  // Stop the animation timer explicitly here to avoid running it in a nested
  // message loop, which may run by Browser destructor.
  loading_animation_timer_.Stop();

  // Immersive mode may need to reparent views before they are removed/deleted.
  immersive_mode_controller_.reset();

  // Reset autofill bubble handler to make sure it does not out-live toolbar,
  // since it is responsible for showing autofill related bubbles from toolbar's
  // child views and it is an observer for avatar toolbar button if any.
  autofill_bubble_handler_.reset();

  auto* global_registry =
      extensions::ExtensionCommandsGlobalRegistry::Get(browser_->profile());
  if (global_registry->registry_for_active_window() ==
      extension_keybinding_registry_.get()) {
    global_registry->set_registry_for_active_window(nullptr);
  }

  // These are raw pointers to child views, so they need to be set to null
  // before `RemoveAllChildViews()` is called to avoid dangling.
  frame_ = nullptr;
  top_container_ = nullptr;
  web_app_frame_toolbar_ = nullptr;
  web_app_window_title_ = nullptr;
  tab_strip_region_view_ = nullptr;
  tabstrip_ = nullptr;
  webui_tab_strip_ = nullptr;
  toolbar_ = nullptr;
  contents_separator_ = nullptr;
  loading_bar_ = nullptr;
  find_bar_host_view_ = nullptr;
  download_shelf_ = nullptr;
  infobar_container_ = nullptr;
  multi_contents_view_ = nullptr;
  contents_web_view_ = nullptr;
  lens_overlay_view_ = nullptr;
  devtools_web_view_ = nullptr;
  devtools_scrim_view_ = nullptr;
  contents_scrim_view_ = nullptr;
  window_scrim_view_ = nullptr;
  watermark_view_ = nullptr;
  glic_border_ = nullptr;
  new_tab_footer_web_view_ = nullptr;
  new_tab_footer_web_view_separator_ = nullptr;
  contents_container_ = nullptr;
  unified_side_panel_ = nullptr;
  right_aligned_side_panel_separator_ = nullptr;
  left_aligned_side_panel_separator_ = nullptr;
  side_panel_rounded_corner_ = nullptr;
  toolbar_button_provider_ = nullptr;

  // Child views maintain PrefMember attributes that point to
  // OffTheRecordProfile's PrefService which gets deleted by ~Browser.
  RemoveAllChildViews();
}

// static
BrowserWindow* BrowserWindow::FindBrowserWindowWithWebContents(
    content::WebContents* web_contents) {
  // Check first to see if the we can find a top level widget for the
  // `web_contents`. This covers the case of searching for the browser window
  // associated with a non-tab contents and the active tab contents. Fall back
  // to searching the tab strip model for a tab contents match. This later
  // search is necessary as a tab contents can be swapped out of the browser
  // window's ContentWebView on a tab switch and may disassociate with its top
  // level NativeView.
  if (const auto* widget = views::Widget::GetTopLevelWidgetForNativeView(
          web_contents->GetNativeView())) {
    return BrowserView::GetBrowserViewForNativeWindow(
        widget->GetNativeWindow());
  }
  const auto* browser = chrome::FindBrowserWithTab(web_contents);
  return browser ? browser->window() : nullptr;
}

// static
BrowserView* BrowserView::GetBrowserViewForNativeWindow(
    gfx::NativeWindow window) {
  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(window);
  return widget ? reinterpret_cast<BrowserView*>(
                      widget->GetNativeWindowProperty(kBrowserViewKey))
                : nullptr;
}

// static
BrowserView* BrowserView::GetBrowserViewForBrowser(const Browser* browser) {
  // It might look like this method should be implemented as:
  //   return static_cast<BrowserView*>(browser->window())
  // but in fact in unit tests browser->window() may not be a BrowserView even
  // in Views Browser builds. Always go through the ForNativeWindow path, which
  // is robust against being given any kind of native window.
  //
  // Also, tests don't always have a non-null NativeWindow backing the
  // BrowserWindow, so be sure to check for that as well.
  //
  // Lastly note that this function can be called during construction of
  // Browser, at which point browser->window() might return nullptr.
  if (!browser->window() || !browser->window()->GetNativeWindow()) {
    return nullptr;
  }
  return GetBrowserViewForNativeWindow(browser->window()->GetNativeWindow());
}

void BrowserView::SetDownloadShelfForTest(DownloadShelf* download_shelf) {
  download_shelf_ = download_shelf;
}

// static
void BrowserView::SetDisableRevealerDelayForTesting(bool disable) {
  g_disable_revealer_delay_for_testing = disable;
}

gfx::Rect BrowserView::GetFindBarBoundingBox() const {
  gfx::Rect contents_bounds = contents_container_->ConvertRectToWidget(
      contents_container_->GetLocalBounds());

  // If the location bar is visible use it to position the bounding box,
  // otherwise use the contents container.
  if (!immersive_mode_controller_->IsEnabled() ||
      immersive_mode_controller_->IsRevealed()) {
    const gfx::Rect bounding_box =
        toolbar_button_provider_->GetFindBarBoundingBox(
            contents_bounds.bottom());
    if (!bounding_box.IsEmpty()) {
      return bounding_box;
    }
  }

  contents_bounds.Inset(gfx::Insets::TLBR(0, 0, 0, gfx::scrollbar_size()));
  return contents_container_->GetMirroredRect(contents_bounds);
}

int BrowserView::GetTabStripHeight() const {
  // We want to return tabstrip_->height(), but we might be called in the midst
  // of layout, when that hasn't yet been updated to reflect the current state.
  // So return what the tabstrip height _ought_ to be right now.
  return ShouldDrawTabStrip() ? tabstrip_->GetPreferredSize().height() : 0;
}

gfx::Size BrowserView::GetWebAppFrameToolbarPreferredSize() const {
  return web_app_frame_toolbar_ ? web_app_frame_toolbar_->GetPreferredSize()
                                : gfx::Size();
}

#if BUILDFLAG(IS_MAC)
bool BrowserView::UsesImmersiveFullscreenMode() const {
  const bool is_pwa =
      base::FeatureList::IsEnabled(features::kImmersiveFullscreenPWAs) &&
      GetIsWebAppType();
  const bool is_tabbed_window = GetSupportsTabStrip();
  return base::FeatureList::IsEnabled(features::kImmersiveFullscreen) &&
         (is_pwa || is_tabbed_window);
}

bool BrowserView::UsesImmersiveFullscreenTabbedMode() const {
  return (GetSupportsTabStrip() &&
          base::FeatureList::IsEnabled(features::kImmersiveFullscreen)) &&
         !GetIsWebAppType();
}
#endif

TabSearchBubbleHost* BrowserView::GetTabSearchBubbleHost() {
  return tab_search_bubble_host_.get();
}

bool BrowserView::GetTabStripVisible() const {
  if (!ShouldDrawTabStrip()) {
    return false;
  }

  // In non-fullscreen the tabstrip should always be visible.
  if (!immersive_mode_controller_->IsEnabled()) {
    return true;
  }

  return immersive_mode_controller_->IsRevealed();
}

bool BrowserView::ShouldDrawTabStrip() const {
  // Return false if this window does not normally display a tabstrip or if the
  // tabstrip is currently hidden, e.g. because we're in fullscreen.
  if (!browser_->SupportsWindowFeature(Browser::FEATURE_TABSTRIP)) {
    return false;
  }

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (WebUITabStripContainerView::UseTouchableTabStrip(browser_.get())) {
    return false;
  }
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)

  // Return false if the tabstrip has not yet been created (by InitViews()),
  // since callers may otherwise try to access it. Note that we can't just check
  // this alone, as the tabstrip is created unconditionally even for windows
  // that won't display it.
  return tabstrip_ != nullptr;
}

bool BrowserView::GetIncognito() const {
  return browser_->profile()->IsIncognitoProfile();
}

bool BrowserView::GetGuestSession() const {
  return browser_->profile()->IsGuestSession();
}

bool BrowserView::GetRegularOrGuestSession() const {
  return profiles::IsRegularOrGuestSession(browser_.get());
}

bool BrowserView::GetAccelerator(int cmd_id,
                                 ui::Accelerator* accelerator) const {
#if BUILDFLAG(IS_MAC)
  // On macOS, most accelerators are defined in MainMenu.xib and are user
  // configurable. Furthermore, their values and enabled state depends on the
  // key window. Views code relies on a static mapping that is not dependent on
  // the key window. Thus, we provide the default Mac accelerator for each
  // CommandId, which is static. This may be inaccurate, but is at least
  // sufficiently well defined for Views to use.
  if (GetDefaultMacAcceleratorForCommandId(cmd_id, accelerator)) {
    return true;
  }
#else
  // We retrieve the accelerator information for standard accelerators
  // for cut, copy and paste.
  if (GetStandardAcceleratorForCommandId(cmd_id, accelerator)) {
    return true;
  }
#endif
  // Else, we retrieve the accelerator information from the accelerator table.
  for (const auto& it : accelerator_table_) {
    if (it.second == cmd_id) {
      *accelerator = it.first;
      return true;
    }
  }
  return false;
}

bool BrowserView::IsAcceleratorRegistered(const ui::Accelerator& accelerator) {
  return accelerator_table_.find(accelerator) != accelerator_table_.end();
}

WebContents* BrowserView::GetActiveWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

bool BrowserView::GetSupportsTabStrip() const {
  return browser_->CanSupportWindowFeature(Browser::FEATURE_TABSTRIP);
}

bool BrowserView::GetIsNormalType() const {
  return browser_->is_type_normal();
}

bool BrowserView::GetIsWebAppType() const {
  return web_app::AppBrowserController::IsWebApp(browser_.get());
}

bool BrowserView::GetIsPictureInPictureType() const {
  return browser_->is_type_picture_in_picture();
}

std::optional<blink::mojom::PictureInPictureWindowOptions>
BrowserView::GetDocumentPictureInPictureOptions() const {
  return browser_->create_params().pip_options;
}

bool BrowserView::GetTopControlsSlideBehaviorEnabled() const {
  return top_controls_slide_controller_ &&
         top_controls_slide_controller_->IsEnabled();
}

float BrowserView::GetTopControlsSlideBehaviorShownRatio() const {
  if (top_controls_slide_controller_) {
    return top_controls_slide_controller_->GetShownRatio();
  }

  return 1.f;
}

views::Widget* BrowserView::GetWidgetForAnchoring() {
#if BUILDFLAG(IS_MAC)
  if (UsesImmersiveFullscreenMode()) {
    return IsFullscreen() ? overlay_widget_.get() : GetWidget();
  }
#endif
  return GetWidget();
}

bool BrowserView::IsInSplitView() const {
  return multi_contents_view_ && multi_contents_view_->IsInSplitView();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, BrowserWindow implementation:

void BrowserView::Show() {
#if !BUILDFLAG(IS_WIN) && !BUILDFLAG(IS_CHROMEOS)
  // The Browser associated with this browser window must become the active
  // browser at the time |Show()| is called. This is the natural behavior under
  // Windows and Chrome OS, but other platforms will not trigger
  // OnWidgetActivationChanged() until we return to the runloop. Therefore any
  // calls to Browser::GetLastActive() will return the wrong result if we do not
  // explicitly set it here.
  browser()->DidBecomeActive();
#endif

  // If the window is already visible, just activate it.
  if (frame_->IsVisible()) {
    frame_->Activate();
    return;
  }

  // Only set |restore_focus_on_activation_| when it is not set so that restore
  // focus on activation only happen once for the very first Show() call.
  if (!restore_focus_on_activation_.has_value()) {
    restore_focus_on_activation_ = true;
  }

  frame_->Show();

  browser()->OnWindowDidShow();

  // The fullscreen transition clears out focus, but there are some cases (for
  // example, new window in Mac fullscreen with toolbar showing) where we need
  // restore it.
  if (frame_->IsFullscreen() &&
      !frame_->GetFrameView()->ShouldHideTopUIForFullscreen() &&
      GetFocusManager() && !GetFocusManager()->GetFocusedView()) {
    SetFocusToLocationBar(false);
  }

#if !BUILDFLAG(IS_CHROMEOS)
  if (!accessibility_focus_highlight_) {
    accessibility_focus_highlight_ =
        std::make_unique<AccessibilityFocusHighlight>(this);
  }
#endif  // !BUILDFLAG(IS_CHROMEOS)
}

void BrowserView::ShowInactive() {
  if (!frame_->IsVisible()) {
    frame_->ShowInactive();
  }
}

void BrowserView::Hide() {
  // Not implemented.
}

bool BrowserView::IsVisible() const {
  return frame_->IsVisible();
}

void BrowserView::SetBounds(const gfx::Rect& bounds) {
  if (IsForceFullscreen()) {
    return;
  }

  ExitFullscreen();

  // If the BrowserNonClientFrameView has been created, give it a chance to
  // handle the BrowserFrame's bounds change.
  if (frame_->GetFrameView()) {
    frame_->GetFrameView()->SetFrameBounds(bounds);
  } else {
    frame_->SetBounds(bounds);
  }
}

void BrowserView::Close() {
  frame_->Close();
}

void BrowserView::Activate() {
#if !BUILDFLAG(IS_WIN) && !BUILDFLAG(IS_CHROMEOS)
  // Update the list managed by `BrowserList` synchronously the same way
  // `BrowserView::Show()` does.
  browser_->DidBecomeActive();
#endif
  frame_->Activate();
}

void BrowserView::Deactivate() {
  frame_->Deactivate();
}

bool BrowserView::IsActive() const {
  return frame_->IsActive();
}

void BrowserView::FlashFrame(bool flash) {
  frame_->FlashFrame(flash);
}

ui::ZOrderLevel BrowserView::GetZOrderLevel() const {
  return frame_->GetZOrderLevel();
}

void BrowserView::SetZOrderLevel(ui::ZOrderLevel level) {
  frame_->SetZOrderLevel(level);
}

gfx::NativeWindow BrowserView::GetNativeWindow() const {
  // While the browser destruction is going on, the widget can already be gone,
  // but utility functions like FindBrowserWithWindow will still call this.
  return GetWidget() ? GetWidget()->GetNativeWindow() : gfx::NativeWindow();
}

bool BrowserView::IsOnCurrentWorkspace() const {
  // In tests, the native window can be nullptr.
  gfx::NativeWindow native_win = GetNativeWindow();
  if (!native_win) {
    return true;
  }

#if BUILDFLAG(IS_CHROMEOS)
  return chromeos::DesksHelper::Get(native_win)
      ->BelongsToActiveDesk(native_win);
#elif BUILDFLAG(IS_WIN)
  std::optional<bool> on_current_workspace =
      native_win->GetHost()->on_current_workspace();
  if (on_current_workspace.has_value()) {
    return on_current_workspace.value();
  }

  // If the window is not cloaked, it is not on another desktop because
  // windows on another virtual desktop are always cloaked.
  if (!gfx::IsWindowCloaked(native_win->GetHost()->GetAcceleratedWidget())) {
    return true;
  }

  Microsoft::WRL::ComPtr<IVirtualDesktopManager> virtual_desktop_manager;
  if (!SUCCEEDED(::CoCreateInstance(_uuidof(VirtualDesktopManager), nullptr,
                                    CLSCTX_ALL,
                                    IID_PPV_ARGS(&virtual_desktop_manager)))) {
    return true;
  }
  // If a IVirtualDesktopManager method failed, we assume the window is on
  // the current virtual desktop.
  return gfx::IsWindowOnCurrentVirtualDesktop(
             native_win->GetHost()->GetAcceleratedWidget(),
             virtual_desktop_manager) != false;
#else
  return true;
#endif  // BUILDFLAG(IS_CHROMEOS)
}

bool BrowserView::IsVisibleOnScreen() const {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  // TODO(crbug.com/405283740): currently only works for mac and windows. See
  // comments around Widget::IsVisibleOnScreen() for more details. Eventually
  // this should work for all platforms.
  return frame_->IsVisibleOnScreen();
#else
  return IsOnCurrentWorkspace();
#endif
}

void BrowserView::SetTopControlsShownRatio(content::WebContents* web_contents,
                                           float ratio) {
  if (top_controls_slide_controller_) {
    top_controls_slide_controller_->SetShownRatio(web_contents, ratio);
  }
}

bool BrowserView::DoBrowserControlsShrinkRendererSize(
    const content::WebContents* contents) const {
  return top_controls_slide_controller_ &&
         top_controls_slide_controller_->DoBrowserControlsShrinkRendererSize(
             contents);
}

ui::NativeTheme* BrowserView::GetNativeTheme() {
  return views::ClientView::GetNativeTheme();
}

const ui::ThemeProvider* BrowserView::GetThemeProvider() const {
  return views::ClientView::GetThemeProvider();
}

const ui::ColorProvider* BrowserView::GetColorProvider() const {
  return views::ClientView::GetColorProvider();
}

ui::ElementContext BrowserView::GetElementContext() {
  return views::ElementTrackerViews::GetContextForView(this);
}

int BrowserView::GetTopControlsHeight() const {
  if (top_controls_slide_controller_ &&
      top_controls_slide_controller_->IsEnabled()) {
    return top_container_->bounds().height();
  }

  // If the top controls slide feature is disabled, we must give the renderers
  // a value of 0, so as they don't get confused thinking that they need to move
  // the top controls first before the pages start scrolling.
  return 0.f;
}

void BrowserView::SetTopControlsGestureScrollInProgress(bool in_progress) {
  if (top_controls_slide_controller_) {
    top_controls_slide_controller_->SetTopControlsGestureScrollInProgress(
        in_progress);
  }
}

std::vector<StatusBubble*> BrowserView::GetStatusBubbles() {
  std::vector<StatusBubble*> status_bubbles;
  if (multi_contents_view_) {
    if (multi_contents_view_->IsInSplitView()) {
      if (StatusBubble* active_bubble =
              multi_contents_view_->GetActiveContentsView()
                  ->GetStatusBubble()) {
        status_bubbles.push_back(active_bubble);
      }
      if (StatusBubble* inactive_bubble =
              multi_contents_view_->GetInactiveContentsView()
                  ->GetStatusBubble()) {
        status_bubbles.push_back(inactive_bubble);
      }
    } else if (StatusBubble* active_bubble =
                   multi_contents_view_->GetActiveContentsView()
                       ->GetStatusBubble()) {
      status_bubbles.push_back(active_bubble);
    }
  } else if (StatusBubble* bubble = contents_web_view_->GetStatusBubble()) {
    status_bubbles.push_back(bubble);
  }
  return status_bubbles;
}

void BrowserView::UpdateTitleBar() {
  frame_->UpdateWindowTitle();
  if (web_app_window_title_) {
    DCHECK(GetIsWebAppType());
    web_app_window_title_->SetText(GetWindowTitle());
    InvalidateLayout();
  }
  if (!IsLoadingAnimationRunning() && CanChangeWindowIcon()) {
    frame_->UpdateWindowIcon();
  }
}

void BrowserView::BookmarkBarStateChanged(
    BookmarkBar::AnimateChangeType change_type) {
  if (bookmark_bar_view_.get()) {
    BookmarkBar::State new_state = browser_->bookmark_bar_state();
    bookmark_bar_view_->SetBookmarkBarState(new_state, change_type);
  }

  if (MaybeShowBookmarkBar(GetActiveWebContents())) {
    // TODO(crbug.com/326362544): Once BrowserViewLayout extends from
    // LayoutManagerBase we should be able to remove this call as
    // LayoutManagerBase will handle invalidating layout when children are added
    // and removed.
    InvalidateLayout();
  }
}

void BrowserView::TemporarilyShowBookmarkBar(base::TimeDelta duration) {
  browser_->SetForceShowBookmarkBarFlag(
      Browser::ForceShowBookmarkBarFlag::kTabGroupSaved);
  temporary_bookmark_bar_timer_.Start(
      FROM_HERE, duration,
      base::BindOnce(&Browser::ClearForceShowBookmarkBarFlag,
                     browser_->AsWeakPtr(),
                     Browser::ForceShowBookmarkBarFlag::kTabGroupSaved));
}

void BrowserView::UpdateDevTools() {
  UpdateDevToolsForContents(GetActiveWebContents(), true);
  DeprecatedLayoutImmediately();
}

void BrowserView::UpdateLoadingAnimations(bool is_visible) {
  const bool should_animate =
      is_visible && browser_->tab_strip_model()->TabsNeedLoadingUI();

  if (should_animate == IsLoadingAnimationRunning()) {
    // Early return if the loading animation state doesn't change.
    return;
  }

  if (!loading_animation_state_change_closure_.is_null()) {
    std::move(loading_animation_state_change_closure_).Run();
  }

  if (should_animate) {
#if BUILDFLAG(IS_CHROMEOS)
    loading_animation_tracker_.emplace(
        GetWidget()->GetCompositor()->RequestNewCompositorMetricsTracker());
    loading_animation_tracker_->Start(ash::metrics_util::ForSmoothnessV3(
        base::BindRepeating(&RecordTabLoadingSmoothness)));
#endif
    static constexpr base::TimeDelta kAnimationUpdateInterval =
        base::Milliseconds(30);
    // Loads are happening, and the animation isn't running, so start it.
    loading_animation_start_ = base::TimeTicks::Now();
    if (base::FeatureList::IsEnabled(features::kCompositorLoadingAnimations)) {
      loading_animation_ =
          std::make_unique<views::CompositorAnimationRunner>(GetWidget());
      loading_animation_->Start(
          kAnimationUpdateInterval, base::TimeDelta(),
          base::BindRepeating(&BrowserView::LoadingAnimationCallback,
                              base::Unretained(this)));
    } else {
      loading_animation_timer_.Start(
          FROM_HERE, kAnimationUpdateInterval, this,
          &BrowserView::LoadingAnimationTimerCallback);
    }
  } else {
    if (base::FeatureList::IsEnabled(features::kCompositorLoadingAnimations)) {
      loading_animation_->Stop();
      loading_animation_.reset();
    } else {
      loading_animation_timer_.Stop();
    }
#if BUILDFLAG(IS_CHROMEOS)
    loading_animation_tracker_->Stop();
#endif
    // Loads are now complete, update the state if a task was scheduled.
    LoadingAnimationCallback(base::TimeTicks::Now());
  }
}

void BrowserView::SetLoadingAnimationStateChangeClosureForTesting(
    base::OnceClosure closure) {
  loading_animation_state_change_closure_ = std::move(closure);
}

gfx::Point BrowserView::GetThemeOffsetFromBrowserView() const {
  gfx::Point browser_view_origin;
  const views::View* root_view = this;
  while (root_view->parent()) {
    root_view = root_view->parent();
  }
  views::View::ConvertPointToTarget(this, root_view, &browser_view_origin);
  return gfx::Point(
      -browser_view_origin.x(),
      ThemeProperties::kFrameHeightAboveTabs - browser_view_origin.y());
}

// static:
BrowserView::DevToolsDockedPlacement BrowserView::GetDevToolsDockedPlacement(
    const gfx::Rect& contents_webview_bounds,
    const gfx::Rect& local_webview_container_bounds) {
  // If contents_webview has the same bounds as webview_container, it either
  // means that devtools are not open or devtools are open in a separate
  // window (not docked).
  if (contents_webview_bounds == local_webview_container_bounds) {
    return BrowserView::DevToolsDockedPlacement::kNone;
  }

  if (contents_webview_bounds.x() > 0 && contents_webview_bounds.y() == 0 &&
      contents_webview_bounds.x() + contents_webview_bounds.width() ==
          local_webview_container_bounds.width()) {
    return BrowserView::DevToolsDockedPlacement::kLeft;
  } else if (contents_webview_bounds.origin().IsOrigin() &&
             contents_webview_bounds.height() ==
                 local_webview_container_bounds.height()) {
    return BrowserView::DevToolsDockedPlacement::kRight;
  } else if (contents_webview_bounds.width() ==
             local_webview_container_bounds.width()) {
    return BrowserView::DevToolsDockedPlacement::kBottom;
  }

  return BrowserView::DevToolsDockedPlacement::kUnknown;
}

bool BrowserView::IsLoadingAnimationRunning() const {
  if (base::FeatureList::IsEnabled(features::kCompositorLoadingAnimations)) {
    return loading_animation_ != nullptr;
  } else {
    return loading_animation_timer_.IsRunning();
  }
}

void BrowserView::SetStarredState(bool is_starred) {
  PageActionIconView* star_icon =
      toolbar_button_provider_->GetPageActionIconView(
          PageActionIconType::kBookmarkStar);
  if (star_icon) {
    star_icon->SetActive(is_starred);
  }
}

void BrowserView::OnActiveTabChanged(content::WebContents* old_contents,
                                     content::WebContents* new_contents,
                                     int index,
                                     int reason) {
  DCHECK(new_contents);
  TRACE_EVENT0("ui", "BrowserView::OnActiveTabChanged");
  views::WebView* active_contents_view = GetContentsWebView();
  bool tab_change_in_split_view =
      IsTabChangeInSplitView(old_contents, new_contents);

  if (old_contents && !old_contents->IsBeingDestroyed()) {
    // We do not store the focus when closing the tab to work-around bug 4633.
    // Some reports seem to show that the focus manager and/or focused view can
    // be garbage at that point, it is not clear why.
    old_contents->StoreFocus();
  }

  WebContentsObserver::Observe(new_contents);

  // TODO(laurila, crbug.com/1493617): Support multi-tab apps.
  // window.setResizable API should never be called from multi-tab browser.
  CHECK(!GetWebApiWindowResizable());

  // If |contents_container_| already has the correct WebContents, we can save
  // some work.  This also prevents extra events from being reported by the
  // Visibility API under Windows, as ChangeWebContents will briefly hide
  // the WebContents window.
  bool change_tab_contents =
      active_contents_view->web_contents() != new_contents &&
      !tab_change_in_split_view;

#if BUILDFLAG(IS_MAC)
  // Widget::IsActive is inconsistent between Mac and Aura, so don't check for
  // it on Mac. The check is also unnecessary for Mac, since restoring focus
  // won't activate the widget on that platform.
  bool will_restore_focus = !browser_->tab_strip_model()->closing_all() &&
                            GetWidget()->IsVisible() &&
                            !tab_change_in_split_view;
#else
  bool will_restore_focus =
      !browser_->tab_strip_model()->closing_all() && GetWidget()->IsActive() &&
      GetWidget()->IsVisible() && !tab_change_in_split_view;
#endif
  // Update various elements that are interested in knowing the current
  // WebContents.

  // When we toggle the NTP floating bookmarks bar and/or the info bar,
  // we don't want any WebContents to be attached, so that we
  // avoid an unnecessary resize and re-layout of a WebContents.
  if (change_tab_contents) {
    if (will_restore_focus) {
      // Manually clear focus before setting focus behavior so that the focus
      // is not temporarily advanced to an arbitrary place in the UI via
      // SetFocusBehavior(FocusBehavior::NEVER), confusing screen readers.
      // The saved focus for new_contents is restored after it is attached.
      // In addition, this ensures that the next RestoreFocus() will be
      // read out to screen readers, even if focus doesn't actually change.
      GetWidget()->GetFocusManager()->ClearFocus();
    }
    if (loading_bar_) {
      loading_bar_->SetWebContents(nullptr);
    }
    if (multi_contents_view_) {
      multi_contents_view_->GetInactiveContentsView()->SetWebContents(nullptr);
    }
    active_contents_view->SetWebContents(nullptr);
    devtools_web_view_->SetWebContents(nullptr);
  }

  // Do this before updating InfoBarContainer as the InfoBarContainer may
  // callback to us and trigger layout.
  if (bookmark_bar_view_.get()) {
    bookmark_bar_view_->SetBookmarkBarState(
        browser_->bookmark_bar_state(), BookmarkBar::DONT_ANIMATE_STATE_CHANGE);
  }

  infobar_container_->ChangeInfoBarManager(
      infobars::ContentInfoBarManager::FromWebContents(new_contents));

  auto* app_banner_manager =
      webapps::AppBannerManager::FromWebContents(new_contents);
  // May be null in unit tests.
  if (app_banner_manager) {
    ObserveAppBannerManager(app_banner_manager);
  }

  UpdateUIForContents(new_contents);
  RevealTabStripIfNeeded();

  // Layout for DevTools _before_ setting the both main and devtools WebContents
  // to avoid toggling the size of any of them.
  UpdateDevToolsForContents(new_contents, !change_tab_contents);

  if (change_tab_contents) {
    // When the location bar or other UI focus will be restored, first focus the
    // root view so that screen readers announce the current page title. The
    // kFocusContext event will delay the subsequent focus event so that screen
    // readers register them as distinct events.
    if (will_restore_focus) {
      ChromeWebContentsViewFocusHelper* focus_helper =
          ChromeWebContentsViewFocusHelper::FromWebContents(new_contents);
      if (focus_helper &&
          focus_helper->GetStoredFocus() != active_contents_view) {
        GetWidget()->UpdateAccessibleNameForRootView();
        GetWidget()->GetRootView()->NotifyAccessibilityEventDeprecated(
            ax::mojom::Event::kFocusContext, true);
      }
    }

    if (multi_contents_view_) {
      multi_contents_view_->ExecuteOnEachVisibleContentsView(
          base::BindRepeating([](ContentsWebView* contents_view) {
            contents_view->GetWebContentsCloseHandler()->ActiveTabChanged();
          }));
    } else {
      contents_web_view_->GetWebContentsCloseHandler()->ActiveTabChanged();
    }

    if (loading_bar_) {
      loading_bar_->SetWebContents(new_contents);
    }

    if (multi_contents_view_) {
      const tabs::TabInterface* active_tab =
          tabs::TabInterface::GetFromContents(new_contents);
      if (active_tab->IsSplit()) {
        ShowSplitView(/*focus_active_view=*/false);
      } else {
        if (multi_contents_view_->IsInSplitView()) {
          HideSplitView();
        }
        multi_contents_view_->GetActiveContentsView()->SetWebContents(
            new_contents);
      }
    } else {
      active_contents_view->SetWebContents(new_contents);
    }

    SadTabHelper* sad_tab_helper = SadTabHelper::FromWebContents(new_contents);
    if (sad_tab_helper) {
      sad_tab_helper->ReinstallInWebView();
    }

    // The second layout update should be no-op. It will just set the
    // DevTools WebContents.
    UpdateDevToolsForContents(new_contents, true);
  } else if (tab_change_in_split_view) {
    UpdateActiveTabInSplitView();
  }

  MaybeUpdateStoredFocusForWebContents(new_contents);

  if (will_restore_focus) {
    // We only restore focus if our window is visible, to avoid invoking blur
    // handlers when we are eventually shown.
    new_contents->RestoreFocus();
  } else if (!GetWidget()->IsActive()) {
    // When the window is inactive during tab switch, restore focus for the
    // active web content on activation.
    GetFocusManager()->SetStoredFocusView(nullptr);
    restore_focus_on_activation_ = true;
  }

  // Update all the UI bits.
  UpdateTitleBar();

  CHECK_DEREF(browser_->GetFeatures().translate_bubble_controller())
      .CloseBubble();

  // This is only done once when the app is first opened so that there is only
  // one subscriber per web contents.
  if (AppUsesBorderlessMode() && !old_contents) {
    SetWindowManagementPermissionSubscriptionForBorderlessMode(new_contents);
  }
}

void BrowserView::OnTabDetached(content::WebContents* contents,
                                bool was_active) {
  DCHECK(contents);
  if (!was_active) {
    return;
  }

  // This is to unsubscribe the Window Management permission subscriber.
  if (window_management_subscription_id_) {
    contents->GetPrimaryMainFrame()
        ->GetBrowserContext()
        ->GetPermissionController()
        ->UnsubscribeFromPermissionStatusChange(
            window_management_subscription_id_.value());
    window_management_subscription_id_.reset();
  }

  // We need to reset the current tab contents to null before it gets
  // freed. This is because the focus manager performs some operations
  // on the selected WebContents when it is removed.
  if (multi_contents_view_) {
    multi_contents_view_->ExecuteOnEachVisibleContentsView(
        base::BindRepeating([](ContentsWebView* contents_view) {
          contents_view->GetWebContentsCloseHandler()->ActiveTabChanged();
        }));
  } else {
    contents_web_view_->GetWebContentsCloseHandler()->ActiveTabChanged();
  }
  if (loading_bar_) {
    loading_bar_->SetWebContents(nullptr);
  }
  GetContentsWebView()->SetWebContents(nullptr);
  infobar_container_->ChangeInfoBarManager(nullptr);
  app_banner_manager_observation_.Reset();
  UpdateDevToolsForContents(nullptr, true);
}

void BrowserView::ZoomChangedForActiveTab(bool can_show_bubble) {
  const AppMenuButton* app_menu_button =
      toolbar_button_provider()->GetAppMenuButton();
  bool app_menu_showing = app_menu_button && app_menu_button->IsMenuShowing();
  toolbar_button_provider()->ZoomChangedForActiveTab(can_show_bubble &&
                                                     !app_menu_showing);
}

gfx::Rect BrowserView::GetRestoredBounds() const {
  gfx::Rect bounds;
  ui::mojom::WindowShowState state;
  frame_->GetWindowPlacement(&bounds, &state);
  return bounds;
}

ui::mojom::WindowShowState BrowserView::GetRestoredState() const {
  gfx::Rect bounds;
  ui::mojom::WindowShowState state;
  frame_->GetWindowPlacement(&bounds, &state);
  return state;
}

gfx::Rect BrowserView::GetBounds() const {
  return frame_->GetWindowBoundsInScreen();
}

gfx::Size BrowserView::GetContentsSize() const {
  DCHECK(initialized_);
  if (multi_contents_view_) {
    return multi_contents_view_->size();
  } else {
    return contents_web_view_->size();
  }
}

void BrowserView::SetContentsSize(const gfx::Size& size) {
  DCHECK(!GetContentsSize().IsEmpty());

  const int width_diff = size.width() - GetContentsSize().width();
  const int height_diff = size.height() - GetContentsSize().height();

  // Resizing the window may be expensive, so only do it if the size is wrong.
  if (width_diff == 0 && height_diff == 0) {
    return;
  }

  gfx::Rect bounds = GetBounds();
  bounds.set_width(bounds.width() + width_diff);
  bounds.set_height(bounds.height() + height_diff);

  // Constrain the final bounds to the current screen's available area. Bounds
  // enforcement applied earlier does not know the specific frame dimensions.
  // Changes to the window size should not generally trigger screen changes.
  auto display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(GetNativeWindow());
  bounds.AdjustToFit(display.work_area());
  SetBounds(bounds);
}

bool BrowserView::IsMaximized() const {
  return frame_->IsMaximized();
}

bool BrowserView::IsMinimized() const {
  return frame_->IsMinimized();
}

void BrowserView::Maximize() {
  frame_->Maximize();
}

void BrowserView::Minimize() {
  frame_->Minimize();
}

void BrowserView::Restore() {
  frame_->Restore();
}

void BrowserView::EnterFullscreen(const url::Origin& origin,
                                  ExclusiveAccessBubbleType bubble_type,
                                  const int64_t display_id) {
  if (base::FeatureList::IsEnabled(features::kAsyncFullscreenWindowState)) {
    if (IsInSplitView()) {
      multi_contents_view_->CloseSplitView();
    }
    RequestFullscreen(true, display_id);
  } else {
    auto* screen = display::Screen::GetScreen();
    auto display = screen->GetDisplayNearestWindow(GetNativeWindow());
    const bool requesting_another_screen =
        display_id != display.id() && display_id != display::kInvalidDisplayId;
    if (IsFullscreen() && !requesting_another_screen) {
      // Nothing to do.
      return;
    }
    if (IsInSplitView()) {
      multi_contents_view_->CloseSplitView();
    }
    ProcessFullscreen(true, display_id);
  }
}

void BrowserView::ExitFullscreen() {
  if (IsForceFullscreen()) {
    return;
  }

  if (base::FeatureList::IsEnabled(features::kAsyncFullscreenWindowState)) {
    RequestFullscreen(false, display::kInvalidDisplayId);
  } else {
    if (!IsFullscreen()) {
      return;  // Nothing to do.
    }
    ProcessFullscreen(false, display::kInvalidDisplayId);
  }

  const int active_index = browser_->tab_strip_model()->active_index();

  // When the browser is closing when exiting fullscreen mode, the active tab
  // might no longer exist.
  if (browser_->tab_strip_model()->ContainsIndex(active_index)) {
    std::optional<split_tabs::SplitTabId> split_tab_id =
        browser_->tab_strip_model()->GetTabAtIndex(active_index)->GetSplit();
    if (split_tab_id.has_value()) {
      ShowSplitView(GetContentsView()->HasFocus());
    }
  }
}

void BrowserView::UpdateExclusiveAccessBubble(
    const ExclusiveAccessBubbleParams& params,
    ExclusiveAccessBubbleHideCallback first_hide_callback) {
  // Trusted pinned mode does not allow to escape. So do not show the bubble.
  bool is_trusted_pinned =
      platform_util::IsBrowserLockedFullscreen(browser_.get());

  // Whether we should remove the bubble if it exists, or not show the bubble.
  // TODO(jamescook): Figure out what to do with mouse-lock.
  bool should_close_bubble = is_trusted_pinned;
  if (!params.has_download) {
    // ...TYPE_NONE indicates deleting the bubble, except when used with
    // download.
    should_close_bubble |= params.type == EXCLUSIVE_ACCESS_BUBBLE_TYPE_NONE;
#if BUILDFLAG(IS_CHROMEOS)
    // Immersive mode allows the toolbar to be shown, so do not show the bubble.
    // However, do show the bubble in a managed guest session (see
    // crbug.com/741069).
    // Immersive mode logic for downloads is handled by the download controller.
    should_close_bubble |= ShouldUseImmersiveFullscreenForUrl(params.type) &&
                           !chromeos::IsManagedGuestSession();
#endif
  }

  if (should_close_bubble) {
    if (first_hide_callback) {
      std::move(first_hide_callback)
          .Run(ExclusiveAccessBubbleHideReason::kNotShown);
    }

    // If we intend to close the bubble but it has already been deleted no
    // action is needed.
    if (!exclusive_access_bubble_) {
      return;
    }
    // Exit if we've already queued up a task to close the bubble.
    if (exclusive_access_bubble_destruction_task_id_) {
      return;
    }
    // `HideImmediately()` will trigger a callback for the current bubble with
    // `ExclusiveAccessBubbleHideReason::kInterrupted` if available.
    exclusive_access_bubble_->HideImmediately();

    // Perform the destroy async. State updates in the exclusive access bubble
    // view may call back into this method. This otherwise results in premature
    // deletion of the bubble view and UAFs. See crbug.com/1426521.
    exclusive_access_bubble_destruction_task_id_ =
        exclusive_access_bubble_cancelable_task_tracker_.PostTask(
            base::SingleThreadTaskRunner::GetCurrentDefault().get(), FROM_HERE,
            base::BindOnce(&BrowserView::DestroyAnyExclusiveAccessBubble,
                           GetAsWeakPtr()));
    return;
  }

  if (exclusive_access_bubble_) {
    if (exclusive_access_bubble_destruction_task_id_) {
      // We previously posted a destruction task, but now we want to reuse the
      // bubble. Cancel the destruction task.
      exclusive_access_bubble_cancelable_task_tracker_.TryCancel(
          exclusive_access_bubble_destruction_task_id_.value());
      exclusive_access_bubble_destruction_task_id_.reset();
    }
    exclusive_access_bubble_->Update(params, std::move(first_hide_callback));
    return;
  }

  exclusive_access_bubble_ = std::make_unique<ExclusiveAccessBubbleViews>(
      this, params, std::move(first_hide_callback));
}

bool BrowserView::IsExclusiveAccessBubbleDisplayed() const {
  return exclusive_access_bubble_ && (exclusive_access_bubble_->IsShowing() ||
                                      exclusive_access_bubble_->IsVisible());
}

void BrowserView::OnExclusiveAccessUserInput() {
  if (exclusive_access_bubble_.get()) {
    exclusive_access_bubble_->OnUserInput();
  }
}

bool BrowserView::ShouldHideUIForFullscreen() const {
  // Immersive mode needs UI for the slide-down top panel.
  if (immersive_mode_controller_->IsEnabled()) {
    return false;
  }

  return frame_->GetFrameView()->ShouldHideTopUIForFullscreen();
}

bool BrowserView::IsFullscreen() const {
  return frame_->IsFullscreen();
}

bool BrowserView::IsFullscreenBubbleVisible() const {
  return exclusive_access_bubble_ != nullptr;
}

bool BrowserView::IsForceFullscreen() const {
  return force_fullscreen_;
}

void BrowserView::SetForceFullscreen(bool force_fullscreen) {
  force_fullscreen_ = force_fullscreen;
}

void BrowserView::RestoreFocus() {
  WebContents* selected_web_contents = GetActiveWebContents();
  if (selected_web_contents) {
    selected_web_contents->RestoreFocus();
  }
}

void BrowserView::FullscreenStateChanging() {
  // Skip view changes during close, especially to avoid making new OS requests.
  if (frame_->IsClosed()) {
    return;
  }

  if (base::FeatureList::IsEnabled(features::kAsyncFullscreenWindowState)) {
    PrepareFullscreen(IsFullscreen());
  } else {
    ProcessFullscreen(IsFullscreen(), display::kInvalidDisplayId);
  }
}

void BrowserView::FullscreenStateChanged() {
#if BUILDFLAG(IS_CHROMEOS)
  // Avoid using immersive mode in locked fullscreen as it allows the user to
  // exit the locked mode. Keep immersive mode enabled if the webapp is locked
  // for OnTask (only relevant for non-web browser scenarios).
  // TODO(b/365146870): Remove once we consolidate locked fullscreen with
  // OnTask.
  bool avoid_using_immersive_mode =
      platform_util::IsBrowserLockedFullscreen(browser_.get()) &&
      !browser_->IsLockedForOnTask();

  if (avoid_using_immersive_mode) {
    immersive_mode_controller_->SetEnabled(false);
  } else {
    // Enable immersive before the browser refreshes its list of enabled
    // commands.
    // Enable immersive mode when entering browser fullscreen, unless it's in
    // app mode or requested by an extension.
    if (IsFullscreen()) {
      auto* fullscreen_controller =
          GetExclusiveAccessManager()->fullscreen_controller();

      bool enable_immersive =
          !IsRunningInAppMode() &&
          !fullscreen_controller->IsExtensionFullscreenOrPending() &&
          fullscreen_controller->IsFullscreenForBrowser();
      immersive_mode_controller_->SetEnabled(enable_immersive);
    } else if (!immersive_mode_controller_
                    ->ShouldStayImmersiveAfterExitingFullscreen()) {
      // Disable immersive mode if not required to stay immersive after exiting
      // fullscreen.
      immersive_mode_controller_->SetEnabled(false);
    }
  }
#endif

#if BUILDFLAG(IS_MAC)
  if (AppUsesWindowControlsOverlay()) {
    UpdateWindowControlsOverlayEnabled();
  }

  // In mac fullscreen the toolbar view is hosted in the overlay widget that has
  // a higher z-order level. This overlay widget should be used for anchoring
  // secondary UIs, otherwise they will be covered by the toolbar.
  views::Widget* widget_for_anchoring =
      UsesImmersiveFullscreenMode() && IsFullscreen() ? overlay_widget_.get()
                                                      : nullptr;
  contents_container()->SetProperty(views::kWidgetForAnchoringKey,
                                    widget_for_anchoring);
#endif  // BUILDFLAG(IS_MAC)

  browser_->WindowFullscreenStateChanged();

  GetExclusiveAccessManager()
      ->fullscreen_controller()
      ->FullscreenTransitionCompleted();

  if (base::FeatureList::IsEnabled(features::kAsyncFullscreenWindowState)) {
    ToolbarSizeChanged(false);
    frame_->GetFrameView()->OnFullscreenStateChanged();
  }
}

void BrowserView::SetToolbarButtonProvider(ToolbarButtonProvider* provider) {
  toolbar_button_provider_ = provider;
  // Recreate the autofill bubble handler when toolbar button provider changes.
  autofill_bubble_handler_ =
      std::make_unique<autofill::AutofillBubbleHandlerImpl>(
          toolbar_button_provider_);
}

void BrowserView::UpdatePageActionIcon(PageActionIconType type) {
  // When present, the intent chip replaces the intent picker page action icon.
  if (type == PageActionIconType::kIntentPicker &&
      toolbar_button_provider()->GetIntentChipButton()) {
    toolbar_button_provider()->GetIntentChipButton()->Update();
    return;
  }

  PageActionIconView* icon =
      toolbar_button_provider_->GetPageActionIconView(type);
  if (icon) {
    icon->Update();
  }
}

autofill::AutofillBubbleHandler* BrowserView::GetAutofillBubbleHandler() {
  return autofill_bubble_handler_.get();
}

void BrowserView::ExecutePageActionIconForTesting(PageActionIconType type) {
  toolbar_button_provider_->GetPageActionIconView(type)->ExecuteForTesting();
}

LocationBar* BrowserView::GetLocationBar() const {
  return GetLocationBarView();
}

void BrowserView::SetFocusToLocationBar(bool is_user_initiated) {
  // On Windows, changing focus to the location bar causes the browser window to
  // become active. This can steal focus if the user has another window open
  // already. On Chrome OS, changing focus makes a view believe it has a focus
  // even if the widget doens't have a focus. Either cases, we need to ignore
  // this when the browser window isn't active.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  if (!IsActive()) {
    return;
  }
#endif
  if (!IsLocationBarVisible()) {
    return;
  }

  LocationBarView* location_bar = GetLocationBarView();
  location_bar->FocusLocation(is_user_initiated);
  if (!location_bar->omnibox_view()->HasFocus()) {
    // If none of location bar got focus, then clear focus.
    views::FocusManager* focus_manager = GetFocusManager();
    DCHECK(focus_manager);
    focus_manager->ClearFocus();
  }
}

void BrowserView::UpdateReloadStopState(bool is_loading, bool force) {
  if (toolbar_button_provider_->GetReloadButton()) {
    toolbar_button_provider_->GetReloadButton()->ChangeMode(
        is_loading ? ReloadButton::Mode::kStop : ReloadButton::Mode::kReload,
        force);
  }
}

void BrowserView::UpdateToolbar(content::WebContents* contents) {
  // We may end up here during destruction.
  if (toolbar_) {
    toolbar_->Update(contents);
  }
}

bool BrowserView::UpdateToolbarSecurityState() {
  // We may end up here during destruction.
  if (toolbar_) {
    return toolbar_->UpdateSecurityState();
  }

  return false;
}

void BrowserView::UpdateCustomTabBarVisibility(bool visible, bool animate) {
  if (toolbar_) {
    toolbar_->UpdateCustomTabBarVisibility(visible, animate);
  }
}

void BrowserView::SetContentScrimVisibility(bool visible) {
  if (base::FeatureList::IsEnabled(features::KScrimForTabModal)) {
    contents_scrim_view()->SetVisible(visible);
  }
}

void BrowserView::SetDevToolsScrimVisibility(bool visible) {
  if (base::FeatureList::IsEnabled(features::KScrimForTabModal)) {
    devtools_scrim_view()->SetVisible(visible);
  }
}

void BrowserView::ResetToolbarTabState(content::WebContents* contents) {
  // We may end up here during destruction.
  if (toolbar_) {
    toolbar_->ResetTabState(contents);
  }
}

void BrowserView::FocusToolbar() {
  // Temporarily reveal the top-of-window views (if not already revealed) so
  // that the toolbar is visible and is considered focusable. If the
  // toolbar gains focus, `immersive_mode_controller_` will keep the
  // top-of-window views revealed.
  std::unique_ptr<ImmersiveRevealedLock> focus_reveal_lock =
      immersive_mode_controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_YES);

  // Start the traversal within the main toolbar. SetPaneFocus stores
  // the current focused view before changing focus.
  toolbar_button_provider_->FocusToolbar();
}

ExtensionsContainer* BrowserView::GetExtensionsContainer() {
  return toolbar_button_provider_->GetExtensionsToolbarContainer();
}

void BrowserView::ToolbarSizeChanged(bool is_animating) {
  // No need to re-layout if the browser is closing. This is unnecessary and
  // dangerous. For tab modal, its modal dialog manager have already gone.
  // Layout will cause CHECK failure due to missing modal dialog manager.
  if (browser()->IsBrowserClosing()) {
    return;
  }

  if (is_animating) {
    GetContentsWebView()->SetFastResize(true);
    if (multi_contents_view_) {
      multi_contents_view_->GetInactiveContentsView()->SetFastResize(true);
    }
  }
  UpdateUIForContents(GetActiveWebContents());

  // Do nothing if we're currently participating in a tab dragging process. The
  // fast resize bit will be reset and the web contents will get re-layed out
  // after the tab dragging ends.
  if (frame()->tab_drag_kind() != TabDragKind::kNone) {
    return;
  }

  if (is_animating) {
    GetContentsWebView()->SetFastResize(false);
    if (multi_contents_view_) {
      multi_contents_view_->GetInactiveContentsView()->SetFastResize(false);
    }
  }

  // When transitioning from animating to not animating we need to make sure the
  // contents_container_ gets layed out. If we don't do this and the bounds
  // haven't changed contents_container_ won't get a Layout and we'll end up
  // with a gray rect because the clip wasn't updated.
  if (!is_animating) {
    if (multi_contents_view_) {
      multi_contents_view_->InvalidateLayout();
    } else {
      contents_web_view_->InvalidateLayout();
    }
    contents_container_->DeprecatedLayoutImmediately();
  }

  // Web apps that use Window Controls Overlay (WCO) revert back to the
  // standalone style title bar when infobars are visible. Update the enabled
  // state of WCO when the size of the toolbar changes since this indicates
  // that the visibility of the infobar may have changed.
  if (AppUsesWindowControlsOverlay()) {
    UpdateWindowControlsOverlayEnabled();
  }

  if (AppUsesBorderlessMode()) {
    UpdateBorderlessModeEnabled();
  }
}

void BrowserView::TabDraggingStatusChanged(bool is_dragging) {
#if !BUILDFLAG(IS_LINUX)
  GetContentsWebView()->SetFastResize(is_dragging);
  if (multi_contents_view_) {
    multi_contents_view_->GetInactiveContentsView()->SetFastResize(is_dragging);
  }
  if (!is_dragging) {
    // When tab dragging is ended, we need to make sure the web contents get
    // re-layed out. Otherwise we may see web contents get clipped to the window
    // size that was used during dragging.
    if (multi_contents_view_) {
      multi_contents_view_->InvalidateLayout();
    } else {
      contents_web_view_->InvalidateLayout();
    }
    contents_container_->DeprecatedLayoutImmediately();
  }
#endif
}

base::CallbackListSubscription BrowserView::AddOnLinkOpeningFromGestureCallback(
    OnLinkOpeningFromGestureCallback callback) {
  return link_opened_from_gesture_callbacks_.Add(callback);
}

void BrowserView::LinkOpeningFromGesture(WindowOpenDisposition disposition) {
  link_opened_from_gesture_callbacks_.Notify(disposition);
}

bool BrowserView::AppUsesWindowControlsOverlay() const {
  return browser()->app_controller() &&
         browser()->app_controller()->AppUsesWindowControlsOverlay();
}

bool BrowserView::AppUsesTabbed() const {
  return browser()->app_controller() &&
         browser()->app_controller()->AppUsesTabbed();
}

bool BrowserView::IsWindowControlsOverlayEnabled() const {
  return window_controls_overlay_enabled_;
}

void BrowserView::UpdateWindowControlsOverlayEnabled() {
  UpdateWindowControlsOverlayToggleVisible();

  // If the toggle is not visible, we can assume that Window Controls Overlay
  // is not enabled.
  bool enabled = should_show_window_controls_overlay_toggle_ &&
                 browser()->app_controller() &&
                 browser()->app_controller()->IsWindowControlsOverlayEnabled();

  if (enabled == window_controls_overlay_enabled_) {
    return;
  }

  window_controls_overlay_enabled_ = enabled;

  // Clear the title-bar-area rect when window controls overlay is disabled.
  if (!window_controls_overlay_enabled_) {
    content::WebContents* web_contents = GetActiveWebContents();
    // `web_contents` can be null while the window is closing, but possibly
    // also at other times. See https://crbug.com/1467247.
    if (web_contents) {
      web_contents->UpdateWindowControlsOverlay(gfx::Rect());
    }
  }

  if (web_app_frame_toolbar()) {
    web_app_frame_toolbar()->OnWindowControlsOverlayEnabledChanged();
  }

  if (frame_ && frame_->GetFrameView()) {
    frame_->GetFrameView()->WindowControlsOverlayEnabledChanged();
  }

  // When Window Controls Overlay is enabled or disabled, the browser window
  // needs to be re-layed out to make sure the title bar and web contents appear
  // in the correct locations.
  InvalidateLayout();

  const std::u16string& state_change_text =
      IsWindowControlsOverlayEnabled()
          ? l10n_util::GetStringUTF16(
                IDS_WEB_APP_WINDOW_CONTROLS_OVERLAY_ENABLED_ALERT)
          : l10n_util::GetStringUTF16(
                IDS_WEB_APP_WINDOW_CONTROLS_OVERLAY_DISABLED_ALERT);
#if BUILDFLAG(IS_MAC)
  if (frame_) {
    frame_->native_browser_frame()->AnnounceTextInInProcessWindow(
        state_change_text);
  }
#else
  GetViewAccessibility().AnnounceText(state_change_text);
#endif
}

void BrowserView::UpdateWindowControlsOverlayToggleVisible() {
  bool should_show = AppUsesWindowControlsOverlay();

  if ((toolbar_ && toolbar_->custom_tab_bar() &&
       toolbar_->custom_tab_bar()->GetVisible()) ||
      (infobar_container_ && infobar_container_->GetVisible())) {
    should_show = false;
  }

  if (IsImmersiveModeEnabled()) {
    should_show = false;
  }

#if BUILDFLAG(IS_MAC)
  // On macOS, when in fullscreen mode, window controls (the menu bar, title
  // bar, and toolbar) are attached to a separate NSView that slides down from
  // the top of the screen, independent of, and overlapping the WebContents.
  // Disable WCO when in fullscreen, because this space is inaccessible to
  // WebContents. https://crbug.com/915110.
  if (frame_ && IsFullscreen()) {
    should_show = false;
  }
#endif

  if (should_show == should_show_window_controls_overlay_toggle_) {
    return;
  }

  DCHECK(AppUsesWindowControlsOverlay());
  should_show_window_controls_overlay_toggle_ = should_show;

  if (web_app_frame_toolbar()) {
    web_app_frame_toolbar()->SetWindowControlsOverlayToggleVisible(should_show);
  }
}

void BrowserView::UpdateBorderlessModeEnabled() {
  bool borderless_mode_enabled = AppUsesBorderlessMode();

  if (toolbar_ && toolbar_->custom_tab_bar() &&
      toolbar_->custom_tab_bar()->GetVisible()) {
    borderless_mode_enabled = false;
  } else if (infobar_container_ && infobar_container_->GetVisible()) {
    borderless_mode_enabled = false;
  } else if (IsImmersiveModeEnabled()) {
    borderless_mode_enabled = false;
  }

  if (auto* web_contents = GetActiveWebContents()) {
    // Last committed URL is null when PWA is opened from chrome://apps.
    url::Origin origin = url::Origin::Create(web_contents->GetVisibleURL());
    if (!origin.opaque()) {
      blink::mojom::PermissionStatus status =
          web_contents->GetPrimaryMainFrame()
              ->GetBrowserContext()
              ->GetPermissionController()
              ->GetPermissionResultForOriginWithoutContext(
                  content::PermissionDescriptorUtil::
                      CreatePermissionDescriptorForPermissionType(
                          blink::PermissionType::WINDOW_MANAGEMENT),
                  origin)
              .status;

      window_management_permission_granted_ =
          status == blink::mojom::PermissionStatus::GRANTED;
    }
  } else {
    // Defaults to the value of borderless_mode_enabled if web contents are
    // null. These get overridden when the app is launched and its web contents
    // are ready.
    window_management_permission_granted_ = borderless_mode_enabled;
  }

  if (borderless_mode_enabled == borderless_mode_enabled_) {
    return;
  }
  borderless_mode_enabled_ = borderless_mode_enabled;

  if (web_app_frame_toolbar()) {
    web_app_frame_toolbar()->UpdateBorderlessModeEnabled();
  }
}

void BrowserView::UpdateWindowManagementPermission(
    blink::mojom::PermissionStatus status) {
  window_management_permission_granted_ =
      status == blink::mojom::PermissionStatus::GRANTED;

  // The layout has to update to reflect the borderless mode view change.
  InvalidateLayout();
}

void BrowserView::SetWindowManagementPermissionSubscriptionForBorderlessMode(
    content::WebContents* web_contents) {
  content::RenderFrameHost* rfh = web_contents->GetPrimaryMainFrame();
  auto* controller = rfh->GetBrowserContext()->GetPermissionController();

  // Last committed URL is null when PWA is opened from chrome://apps.
  url::Origin origin = url::Origin::Create(web_contents->GetVisibleURL());
  if (origin.opaque()) {
    // Permission check should not be tied to an empty origin. This can happen
    // when opening popups from borderless IWAs.
    return;
  }

  UpdateWindowManagementPermission(
      controller
          ->GetPermissionResultForOriginWithoutContext(
              content::PermissionDescriptorUtil::
                  CreatePermissionDescriptorForPermissionType(
                      blink::PermissionType::WINDOW_MANAGEMENT),
              origin)
          .status);

  // It is safe to bind base::Unretained(this) because WebContents is
  // owned by BrowserView.
  window_management_subscription_id_ =
      controller->SubscribeToPermissionStatusChange(
          blink::PermissionType::WINDOW_MANAGEMENT,
          /*render_process_host*/ nullptr, rfh, origin.GetURL(),
          /*should_include_device_status=*/false,
          base::BindRepeating(&BrowserView::UpdateWindowManagementPermission,
                              base::Unretained(this)));
}

void BrowserView::ToggleWindowControlsOverlayEnabled(base::OnceClosure done) {
  browser()->app_controller()->ToggleWindowControlsOverlayEnabled(
      base::BindOnce(&BrowserView::UpdateWindowControlsOverlayEnabled,
                     weak_ptr_factory_.GetWeakPtr())
          .Then(std::move(done)));
}

bool BrowserView::WidgetOwnedByAnchorContainsPoint(
    const gfx::Point& point_in_browser_view_coords) {
  const auto point_in_screen_coords =
      views::View::ConvertPointToScreen(this, point_in_browser_view_coords);

  auto* anchor_widget = GetWidgetForAnchoring();

  views::Widget::Widgets widgets =
      views::Widget::GetAllOwnedWidgets(anchor_widget->GetNativeView());
  return std::ranges::any_of(widgets, [point_in_screen_coords,
                                       anchor_widget](views::Widget* widget) {
    return widget != anchor_widget && widget->IsVisible() &&
           widget->GetWindowBoundsInScreen().Contains(point_in_screen_coords);
  });
}

bool BrowserView::IsBorderlessModeEnabled() const {
  return borderless_mode_enabled_ && window_management_permission_granted_;
}
void BrowserView::ShowChromeLabs() {
  CHECK(IsChromeLabsEnabled());
  browser_->GetFeatures().chrome_labs_coordinator()->ShowOrHide();
}

views::WebView* BrowserView::GetContentsWebView() {
  if (multi_contents_view_) {
    return multi_contents_view_->GetActiveContentsView();
  } else {
    return contents_web_view_;
  }
}

BrowserView* BrowserView::AsBrowserView() {
  return this;
}

bool BrowserView::AppUsesBorderlessMode() const {
  return browser()->app_controller() &&
         browser()->app_controller()->AppUsesBorderlessMode();
}

bool BrowserView::AreDraggableRegionsEnabled() const {
  return IsWindowControlsOverlayEnabled() || IsBorderlessModeEnabled();
}

void BrowserView::UpdateSidePanelHorizontalAlignment() {
  const bool is_right_aligned = GetProfile()->GetPrefs()->GetBoolean(
      prefs::kSidePanelHorizontalAlignment);
  unified_side_panel_->SetHorizontalAlignment(
      is_right_aligned ? SidePanel::HorizontalAlignment::kRight
                       : SidePanel::HorizontalAlignment::kLeft);
  GetBrowserViewLayout()->Layout(this);
  side_panel_rounded_corner_->DeprecatedLayoutImmediately();
  side_panel_rounded_corner_->SchedulePaint();
}

void BrowserView::FocusBookmarksToolbar() {
  DCHECK(!immersive_mode_controller_->IsEnabled());
  if (bookmark_bar_view_ && bookmark_bar_view_->GetVisible() &&
      bookmark_bar_view_->GetPreferredSize().height() != 0) {
    bookmark_bar_view_->SetPaneFocusAndFocusDefault();
  }
}

void BrowserView::FocusInactivePopupForAccessibility() {
#if BUILDFLAG(ENABLE_GLIC)
  if (glic::GlicEnabling::IsEnabledByFlags()) {
    glic::GlicKeyedService* service =
        glic::GlicKeyedServiceFactory::GetGlicKeyedService(GetProfile());
    if (service) {
      glic::GlicWindowController& window_controller =
          service->window_controller();
      if (window_controller.attached_browser() == browser_.get()) {
        window_controller.GetGlicWidget()->Activate();
        return;
      }
    }
  }
#endif  // BUILDFLAG(ENABLE_GLIC)

  if (ActivateFirstInactiveBubbleForAccessibility()) {
    return;
  }

  if (!infobar_container_->children().empty()) {
    infobar_container_->SetPaneFocusAndFocusDefault();
  }
}

void BrowserView::FocusAppMenu() {
  // Chrome doesn't have a traditional menu bar, but it has a menu button in the
  // main toolbar that plays the same role.  If the user presses a key that
  // would typically focus the menu bar, tell the toolbar to focus the menu
  // button.  If the user presses the key again, return focus to the previous
  // location.
  //
  // Not used on the Mac, which has a normal menu bar.
  if (toolbar_->GetAppMenuFocused()) {
    RestoreFocus();
  } else {
    DCHECK(!immersive_mode_controller_->IsEnabled());
    toolbar_->SetPaneFocusAndFocusAppMenu();
  }
}

void BrowserView::RotatePaneFocus(bool forwards) {
  GetFocusManager()->RotatePaneFocus(
      forwards ? views::FocusManager::Direction::kForward
               : views::FocusManager::Direction::kBackward,
      views::FocusManager::FocusCycleWrapping::kEnabled);
}

void BrowserView::FocusWebContentsPane() {
  GetContentsView()->RequestFocus();
}

bool BrowserView::ActivateFirstInactiveBubbleForAccessibility() {
  if (feature_promo_controller_ &&
      feature_promo_controller_->bubble_factory_registry()
          ->ToggleFocusForAccessibility(GetElementContext())) {
    // Record that the user successfully used the accelerator to focus the
    // bubble, reducing the need to describe the accelerator the next time a
    // help bubble is shown.
    feature_engagement::TrackerFactory::GetForBrowserContext(GetProfile())
        ->NotifyEvent(
            feature_engagement::events::kFocusHelpBubbleAcceleratorPressed);
    return true;
  }

  // TODO: this fixes https://crbug.com/40668249 and https://crbug.com/40674460,
  // but a more general solution should be desirable to find any bubbles
  // anchored in the views hierarchy.
  if (toolbar_) {
    views::DialogDelegate* bubble = nullptr;
    for (auto* view : std::initializer_list<views::View*>{
             toolbar_->app_menu_button(), GetLocationBarView(),
             toolbar_button_provider_->GetAvatarToolbarButton(),
             toolbar_button_provider_->GetDownloadButton(), top_container_}) {
      if (view) {
        if (auto* dialog = view->GetProperty(views::kAnchoredDialogKey);
            dialog && !user_education::HelpBubbleView::IsHelpBubble(dialog)) {
          bubble = dialog;
          break;
        }
      }
    }

    if (bubble) {
      CHECK(!user_education::HelpBubbleView::IsHelpBubble(bubble));
      View* focusable = bubble->GetInitiallyFocusedView();

      // A PermissionPromptBubbleView will explicitly return nullptr due to
      // https://crbug.com/40084558. In that case, we explicitly focus the
      // cancel button.
      if (!focusable) {
        focusable = bubble->GetCancelButton();
      }

      if (focusable) {
        focusable->RequestFocus();
#if BUILDFLAG(IS_MAC)
        // TODO(https://crbug.com/40486728): When a view requests focus on other
        // platforms, its widget is activated. When doing so in FocusManager on
        // MacOS a lot of interactive tests fail when the widget is destroyed.
        // Activating the widget here should be safe as this happens only
        // after explicit user action (focusing inactive dialog or rotating
        // panes).
        views::Widget* const widget = bubble->GetWidget();
        if (widget && widget->IsVisible() && !widget->IsActive()) {
          DCHECK(browser_->window()->IsActive());
          widget->Activate();
        }
#endif
        return true;
      }
    }
  }

  return false;
}

void BrowserView::TryNotifyWindowBoundsChanged(const gfx::Rect& widget_bounds) {
  if (interactive_resize_in_progress_ || last_widget_bounds_ == widget_bounds) {
    return;
  }

  last_widget_bounds_ = widget_bounds;

  // `extension_window_controller()` may be null if we are in the process of
  // creating the Browser. In that case, skip the notification.
  if (auto* const controller =
          browser()->GetFeatures().extension_window_controller()) {
    controller->NotifyWindowBoundsChanged();
  }
}

void BrowserView::OnWidgetVisibilityChanged(views::Widget* widget,
                                            bool visible) {
  UpdateLoadingAnimations(visible);
}

std::optional<bool> BrowserView::GetWebApiWindowResizable() const {
  // TODO(laurila, crbug.com/1493617): Support multi-tab apps.
  if (browser()->tab_strip_model()->count() > 1) {
    return std::nullopt;
  }

  // The value can only be set in web apps, where there currently can only be 1
  // WebContents, the return value can be determined only by looking at the
  // value set by the active WebContents' primary page.
  content::WebContents* web_contents =
      const_cast<BrowserView*>(this)->GetActiveWebContents();
  if (!web_contents || !web_contents->GetPrimaryMainFrame()) {
    return std::nullopt;
  }

  return web_contents->GetPrimaryPage().GetResizable();
}

bool BrowserView::GetCanResize() {
  return CanResize();
}

// TODO(laurila, crbug.com/1466855): Map into new `ui::DisplayState` enum
// instead of `ui::mojom::WindowShowState`.
ui::mojom::WindowShowState BrowserView::GetWindowShowState() const {
  if (IsMaximized()) {
    return ui::mojom::WindowShowState::kMaximized;
  } else if (IsMinimized()) {
    return ui::mojom::WindowShowState::kMinimized;
  } else if (IsFullscreen()) {
    return ui::mojom::WindowShowState::kFullscreen;
  } else {
    return ui::mojom::WindowShowState::kDefault;
  }
}

void BrowserView::OnWebApiWindowResizableChanged() {
  // TODO(laurila, crbug.com/1493617): Support multi-tab apps.
  // The value can only be set in web apps, where there currently can only be 1
  // WebContents, the return value can be determined only by looking at the
  // value set by the active WebContents' primary page.
  content::WebContents* web_contents = GetActiveWebContents();
  if (!web_contents || !web_contents->GetPrimaryMainFrame() || !GetWidget()) {
    return;
  }

  auto can_resize = web_contents->GetPrimaryPage().GetResizable();
  if (cached_can_resize_from_web_api_ == can_resize) {
    return;
  }

  // Setting it to std::nullopt should never be blocked.
  if (can_resize.has_value() && browser()->tab_strip_model()->count() > 1) {
    // This adds a warning to the active tab, even when another tab makes the
    // call, which also needs to be fixed as part of the multi-apps support.
    web_contents->GetPrimaryMainFrame()->AddMessageToConsole(
        blink::mojom::ConsoleMessageLevel::kWarning,
        base::StringPrintf("window.setResizable blocked due to being called "
                           "from a multi-tab browser."));
    return;
  }

  cached_can_resize_from_web_api_ = can_resize;
  NotifyWidgetSizeConstraintsChanged();
  InvalidateLayout();  // To show/hide the maximize button.
}

void BrowserView::SynchronizeRenderWidgetHostVisualPropertiesForMainFrame() {
  if (!base::FeatureList::IsEnabled(
          blink::features::kDesktopPWAsAdditionalWindowingControls)) {
    return;
  }
  content::WebContents* web_contents = GetActiveWebContents();
  if (!web_contents || !web_contents->GetPrimaryMainFrame()) {
    return;
  }

  if (content::RenderWidgetHost* render_widget_host =
          web_contents->GetPrimaryMainFrame()->GetRenderWidgetHost()) {
    render_widget_host->SynchronizeVisualProperties();
  }
}

void BrowserView::NotifyWidgetSizeConstraintsChanged() {
  if (!GetWidget()) {
    return;
  }

  // TODO(crbug.com/40943569): Undo changes in this CL and return to use
  // `WidgetObserver::OnWidgetSizeConstraintsChanged` once zoom levels are
  // refactored so that visual properties can be updated during page load.
  GetWidget()->OnSizeConstraintsChanged();

  // `resizable` @media feature value in renderer needs to be updated.
  SynchronizeRenderWidgetHostVisualPropertiesForMainFrame();
}

void BrowserView::OnWidgetShowStateChanged(views::Widget* widget) {
  // `display-state` @media feature value in renderer needs to be updated.
  SynchronizeRenderWidgetHostVisualPropertiesForMainFrame();
}

void BrowserView::OnWidgetWindowModalVisibilityChanged(views::Widget* widget,
                                                       bool visible) {
  if (!base::FeatureList::IsEnabled(features::kScrimForBrowserWindowModal)) {
    return;
  }

#if !BUILDFLAG(IS_MAC)
  // MacOS does not need views window scrim. We use sheets to show window modals
  // (-[NSWindow beginSheet:]), which natively draw a scrim.
  window_scrim_view_->SetVisible(visible);
#endif
}

void BrowserView::DidFirstVisuallyNonEmptyPaint() {
  auto can_resize = GetWebApiWindowResizable();
  if (cached_can_resize_from_web_api_ == can_resize) {
    return;
  }
  cached_can_resize_from_web_api_ = can_resize;

  // Observers must be notified when there's new `Page` with a differing
  // `can_resize` value to make sure that they know that `Widget`'s
  // resizability has changed.
  NotifyWidgetSizeConstraintsChanged();
}

void BrowserView::TitleWasSet(content::NavigationEntry* entry) {
  UpdateAccessibleNameForRootView();
}

void BrowserView::TouchModeChanged() {
  MaybeInitializeWebUITabStrip();
}

void BrowserView::MaybeShowReadingListInSidePanelIPH() {
  // TODO(dfried): This promo is potentially superfluous since the pref is never
  // set; remove.
  const PrefService* const pref_service = browser()->profile()->GetPrefs();
  if (pref_service &&
      pref_service->GetBoolean(
          reading_list::prefs::kReadingListDesktopFirstUseExperienceShown)) {
    MaybeShowFeaturePromo(
        feature_engagement::kIPHReadingListInSidePanelFeature);
  }
}

void BrowserView::MaybeShowTabStripToolbarButtonIPH() {
  if (!browser()->is_type_normal()) {
    return;
  }
  bool should_show =
      features::HasTabSearchToolbarButton() &&
      toolbar_->pinned_toolbar_actions_container()->IsActionPinned(
          kActionTabSearch);
  if (should_show) {
    MaybeShowStartupFeaturePromo(
        feature_engagement::kIPHTabSearchToolbarButtonFeature);
  }
}

void BrowserView::DestroyBrowser() {
  // After this returns other parts of Chrome are going to be shutdown. Close
  // the window now so that we are deleted immediately and aren't left holding
  // references to deleted objects.
  GetWidget()->RemoveObserver(this);
  frame_->CloseNow();
}

bool BrowserView::IsBookmarkBarVisible() const {
  if (!browser_->SupportsWindowFeature(Browser::FEATURE_BOOKMARKBAR)) {
    return false;
  }
  if (!bookmark_bar_view_) {
    return false;
  }
  if (!bookmark_bar_view_->parent()) {
    return false;
  }
  if (bookmark_bar_view_->GetPreferredSize().height() == 0) {
    return false;
  }
  if (immersive_mode_controller_->ShouldHideTopViews()) {
    return false;
  }
  if (immersive_mode_controller_->IsEnabled() &&
      !immersive_mode_controller_->IsRevealed()) {
    return false;
  }
  return true;
}

bool BrowserView::IsBookmarkBarAnimating() const {
  return bookmark_bar_view_.get() &&
         bookmark_bar_view_->size_animation().is_animating();
}

bool BrowserView::IsTabStripEditable() const {
  return tabstrip_->IsTabStripEditable();
}

void BrowserView::SetTabStripNotEditableForTesting() {
  tabstrip_->SetTabStripNotEditableForTesting();
}

bool BrowserView::IsToolbarVisible() const {
#if BUILDFLAG(IS_MAC)
  // Immersive full screen makes it possible to display the toolbar when
  // kShowFullscreenToolbar is not set.
  if (!UsesImmersiveFullscreenMode()) {
    if (IsFullscreen() &&
        !fullscreen_utils::IsAlwaysShowToolbarEnabled(browser())) {
      return false;
    }
  }
#endif
  if (immersive_mode_controller_->ShouldHideTopViews()) {
    return false;
  }
  // It's possible to reach here before we've been notified of being added to a
  // widget, so |toolbar_| is still null.  Return false in this case so callers
  // don't assume they can access the toolbar yet.
  return (browser_->SupportsWindowFeature(Browser::FEATURE_TOOLBAR) ||
          browser_->SupportsWindowFeature(Browser::FEATURE_LOCATIONBAR)) &&
         toolbar_;
}

bool BrowserView::IsToolbarShowing() const {
  return GetTabStripVisible();
}

bool BrowserView::IsLocationBarVisible() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_LOCATIONBAR) &&
         GetLocationBarView()->GetVisible();
}

void BrowserView::ShowUpdateChromeDialog() {
  UpdateRecommendedMessageBox::Show(GetNativeWindow());
}

void BrowserView::ShowIntentPickerBubble(
    std::vector<IntentPickerBubbleView::AppInfo> app_info,
    bool show_stay_in_chrome,
    bool show_remember_selection,
    apps::IntentPickerBubbleType bubble_type,
    const std::optional<url::Origin>& initiating_origin,
    IntentPickerResponse callback) {
  toolbar_->ShowIntentPickerBubble(std::move(app_info), show_stay_in_chrome,
                                   show_remember_selection, bubble_type,
                                   initiating_origin, std::move(callback));
}

void BrowserView::ShowBookmarkBubble(const GURL& url, bool already_bookmarked) {
  toolbar_->ShowBookmarkBubble(url, already_bookmarked);
}

qrcode_generator::QRCodeGeneratorBubbleView*
BrowserView::ShowQRCodeGeneratorBubble(content::WebContents* contents,
                                       const GURL& url,
                                       bool show_back_button) {
  auto* controller =
      qrcode_generator::QRCodeGeneratorBubbleController::Get(contents);
  base::OnceClosure on_closing = controller->GetOnBubbleClosedCallback();
  base::OnceClosure on_back_button_pressed;
  if (show_back_button) {
    on_back_button_pressed = controller->GetOnBackButtonPressedCallback();
  }

  views::View* anchor_view =
      toolbar_button_provider()->GetAnchorView(kActionQrCodeGenerator);

  auto* bubble = new qrcode_generator::QRCodeGeneratorBubble(
      anchor_view, contents->GetWeakPtr(), std::move(on_closing),
      std::move(on_back_button_pressed), url);

  views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble->Show();
  return bubble;
}

sharing_hub::ScreenshotCapturedBubble*
BrowserView::ShowScreenshotCapturedBubble(content::WebContents* contents,
                                          const gfx::Image& image) {
  auto* bubble = new sharing_hub::ScreenshotCapturedBubble(
      toolbar_button_provider()->GetAnchorView(std::nullopt), contents, image,
      browser_->profile());

  views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble->ShowForReason(LocationBarBubbleDelegateView::USER_GESTURE);
  return bubble;
}

SharingDialog* BrowserView::ShowSharingDialog(
    content::WebContents* web_contents,
    SharingDialogData data) {
  // TODO(crbug.com/40220302): Remove this altogether. This used to
  // be hardcoded to anchor off the shared clipboard bubble, but that bubble is
  // now gone altogether.
  auto* dialog_view = new SharingDialogView(
      toolbar_button_provider()->GetAnchorView(std::nullopt), web_contents,
      std::move(data));

  views::BubbleDialogDelegateView::CreateBubble(dialog_view)->Show();

  return dialog_view;
}

send_tab_to_self::SendTabToSelfBubbleView*
BrowserView::ShowSendTabToSelfDevicePickerBubble(
    content::WebContents* web_contents) {
  views::View* anchor_view =
      toolbar_button_provider()->GetAnchorView(kActionSendTabToSelf);
  auto* bubble = new send_tab_to_self::SendTabToSelfDevicePickerBubbleView(
      anchor_view, web_contents);

  views::BubbleDialogDelegateView::CreateBubble(bubble);
  // This is always triggered due to a user gesture, c.f. this method's
  // documentation in the interface.
  bubble->ShowForReason(LocationBarBubbleDelegateView::USER_GESTURE);
  return bubble;
}

send_tab_to_self::SendTabToSelfBubbleView*
BrowserView::ShowSendTabToSelfPromoBubble(content::WebContents* web_contents,
                                          bool show_signin_button) {
  views::View* anchor_view =
      toolbar_button_provider()->GetAnchorView(kActionSendTabToSelf);
  auto* bubble = new send_tab_to_self::SendTabToSelfPromoBubbleView(
      anchor_view, web_contents, show_signin_button);

  views::BubbleDialogDelegateView::CreateBubble(bubble);
  // This is always triggered due to a user gesture, c.f. method documentation.
  bubble->ShowForReason(LocationBarBubbleDelegateView::USER_GESTURE);
  return bubble;
}

#if BUILDFLAG(IS_CHROMEOS)
views::Button* BrowserView::GetSharingHubIconButton() {
  return toolbar_button_provider()->GetPageActionIconView(
      PageActionIconType::kSharingHub);
}

void BrowserView::ToggleMultitaskMenu() const {
  auto* frame_view =
      static_cast<BrowserNonClientFrameViewChromeOS*>(frame_->GetFrameView());
  if (!frame_view) {
    return;
  }
  auto* size_button = static_cast<chromeos::FrameSizeButton*>(
      frame_view->caption_button_container()->size_button());
  if (size_button && size_button->GetVisible()) {
    size_button->ToggleMultitaskMenu();
  }
}
#else
sharing_hub::SharingHubBubbleView* BrowserView::ShowSharingHubBubble(
    share::ShareAttempt attempt) {
  auto* bubble = new sharing_hub::SharingHubBubbleViewImpl(
      toolbar_button_provider()->GetAnchorView(std::nullopt), attempt,
      sharing_hub::SharingHubBubbleController::CreateOrGetFromWebContents(
          attempt.web_contents.get()));
  PageActionIconView* icon_view =
      toolbar_button_provider()->GetPageActionIconView(
          PageActionIconType::kSharingHub);
  if (icon_view) {
    bubble->SetHighlightedButton(icon_view);
  }

  views::BubbleDialogDelegateView::CreateBubble(bubble);
  // This is always triggered due to a user gesture, c.f. method documentation.
  bubble->ShowForReason(sharing_hub::SharingHubBubbleViewImpl::USER_GESTURE);

  return bubble;
}
#endif  // BUILDFLAG(IS_CHROMEOS)

ShowTranslateBubbleResult BrowserView::ShowTranslateBubble(
    content::WebContents* web_contents,
    translate::TranslateStep step,
    const std::string& source_language,
    const std::string& target_language,
    translate::TranslateErrors error_type,
    bool is_user_gesture) {
  views::View* contents_view = GetContentsView();

  if (contents_view->HasFocus() && !GetLocationBarView()->IsMouseHovered() &&
      web_contents->IsFocusedElementEditable()) {
    return ShowTranslateBubbleResult::EDITABLE_FIELD_IS_ACTIVE;
  }

  ChromeTranslateClient::FromWebContents(web_contents)
      ->GetTranslateManager()
      ->GetLanguageState()
      ->SetTranslateEnabled(true);

  if (IsMinimized()) {
    return ShowTranslateBubbleResult::BROWSER_WINDOW_MINIMIZED;
  }

  views::Button* translate_icon;
  if (IsPageActionMigrated(PageActionIconType::kTranslate)) {
    translate_icon =
        toolbar_button_provider()->GetPageActionView(kActionShowTranslate);
  } else {
    translate_icon = toolbar_button_provider()->GetPageActionIconView(
        PageActionIconType::kTranslate);
  }

  views::View* anchor_view =
      toolbar_button_provider()->GetAnchorView(kActionShowTranslate);
  if (views::Button::AsButton(anchor_view)) {
    translate_icon = views::Button::AsButton(anchor_view);
  }
  CHECK_DEREF(browser_->GetFeatures().translate_bubble_controller())
      .ShowTranslateBubble(web_contents, anchor_view, translate_icon, step,
                           source_language, target_language, error_type,
                           is_user_gesture ? TranslateBubbleView::USER_GESTURE
                                           : TranslateBubbleView::AUTOMATIC);

  return ShowTranslateBubbleResult::SUCCESS;
}

void BrowserView::StartPartialTranslate(const std::string& source_language,
                                        const std::string& target_language,
                                        const std::u16string& text_selection) {
  // Show the Translate icon and enabled the associated command to show the
  // Translate UI.
  ChromeTranslateClient::FromWebContents(GetActiveWebContents())
      ->GetTranslateManager()
      ->GetLanguageState()
      ->SetTranslateEnabled(true);

  views::Button* translate_icon;
  if (IsPageActionMigrated(PageActionIconType::kTranslate)) {
    translate_icon =
        toolbar_button_provider()->GetPageActionView(kActionShowTranslate);
  } else {
    translate_icon = toolbar_button_provider()->GetPageActionIconView(
        PageActionIconType::kTranslate);
  }

  CHECK_DEREF(browser_->GetFeatures().translate_bubble_controller())
      .StartPartialTranslate(
          GetActiveWebContents(),
          toolbar_button_provider()->GetAnchorView(kActionShowTranslate),
          translate_icon, source_language, target_language, text_selection);
}

void BrowserView::ShowOneClickSigninConfirmation(
    const std::u16string& email,
    base::OnceCallback<void(bool)> confirmed_callback) {
  std::unique_ptr<OneClickSigninLinksDelegate> delegate(
      new OneClickSigninLinksDelegateImpl(browser()));
  OneClickSigninDialogView::ShowDialog(email, std::move(delegate),
                                       GetNativeWindow(),
                                       std::move(confirmed_callback));
}

void BrowserView::SetDownloadShelfVisible(bool visible) {
  DCHECK(download_shelf_);
  browser_->UpdateDownloadShelfVisibility(visible);

  // SetDownloadShelfVisible can force-close the shelf, so make sure we lay out
  // everything correctly, as if the animation had finished. This doesn't
  // matter for showing the shelf, as the show animation will do it.
  ToolbarSizeChanged(false);
}

bool BrowserView::IsDownloadShelfVisible() const {
  return download_shelf_ && download_shelf_->IsShowing();
}

DownloadShelf* BrowserView::GetDownloadShelf() {
  // Don't show download shelf if download bubble is enabled, except that the
  // shelf is already showing (this can happen if prefs were changed at
  // runtime).
  if (download::IsDownloadBubbleEnabled() && !download_shelf_) {
    return nullptr;
  }
  if (!download_shelf_) {
    download_shelf_ =
        AddChildView(std::make_unique<DownloadShelfView>(browser_.get(), this));
    GetBrowserViewLayout()->set_download_shelf(download_shelf_->GetView());
  }
  return download_shelf_;
}

views::View* BrowserView::GetTopContainer() {
  return top_container_;
}

views::View* BrowserView::GetLensOverlayView() {
  return lens_overlay_view_;
}

DownloadBubbleUIController* BrowserView::GetDownloadBubbleUIController() {
  if (auto* download_controller =
          browser_->GetFeatures().download_toolbar_ui_controller()) {
    return download_controller->bubble_controller();
  }
  return nullptr;
}

void BrowserView::ConfirmBrowserCloseWithPendingDownloads(
    int download_count,
    Browser::DownloadCloseType dialog_type,
    base::OnceCallback<void(bool)> callback) {
  // The dialog eats mouse events which results in the close button
  // getting stuck in the hover state. Reset the window controls to
  // prevent this.
  frame()->non_client_view()->ResetWindowControls();
  DownloadInProgressDialogView::Show(GetNativeWindow(), download_count,
                                     dialog_type, std::move(callback));
}

void BrowserView::UserChangedTheme(BrowserThemeChangeType theme_change_type) {
  frame()->UserChangedTheme(theme_change_type);
  // Because the theme change may cause the browser frame to be regenerated,
  // which can mess with tutorials (which expect their bubble anchors to remain
  // visible), this event is sent after the theme change. It can be used to
  // advance a tutorial that expects a theme change.
  if (theme_change_type == BrowserThemeChangeType::kBrowserTheme) {
    views::ElementTrackerViews::GetInstance()->NotifyCustomEvent(
        kBrowserThemeChangedEventId, this);
  }
}

void BrowserView::ShowAppMenu() {
  if (!toolbar_button_provider_->GetAppMenuButton()) {
    return;
  }

  // Keep the top-of-window views revealed as long as the app menu is visible.
  std::unique_ptr<ImmersiveRevealedLock> revealed_lock =
      immersive_mode_controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_NO);

  toolbar_button_provider_->GetAppMenuButton()
      ->menu_button_controller()
      ->Activate(nullptr);
}

bool BrowserView::PreHandleMouseEvent(const blink::WebMouseEvent& event) {
  return false;
}

content::KeyboardEventProcessingResult BrowserView::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  if ((event.GetType() != blink::WebInputEvent::Type::kRawKeyDown) &&
      (event.GetType() != blink::WebInputEvent::Type::kKeyUp)) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  views::FocusManager* focus_manager = GetFocusManager();
  DCHECK(focus_manager);

  if (focus_manager->shortcut_handling_suspended()) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  ui::Accelerator accelerator =
      ui::GetAcceleratorFromNativeWebKeyboardEvent(event);

  // What we have to do here is as follows:
  // - If the |browser_| is for an app, do nothing.
  // - On CrOS if |accelerator| is deprecated, we allow web contents to consume
  //   it if needed.
  // - If the |browser_| is not for an app, and the |accelerator| is not
  //   associated with the browser (e.g. an Ash shortcut), process it.
  // - If the |browser_| is not for an app, and the |accelerator| is associated
  //   with the browser, and it is a reserved one (e.g. Ctrl+w), process it.
  // - If the |browser_| is not for an app, and the |accelerator| is associated
  //   with the browser, and it is not a reserved one, do nothing.

  if (browser_->is_type_app() || browser_->is_type_app_popup()) {
    // Let all keys fall through to a v1 app's web content, even accelerators.
    // We don't use NOT_HANDLED_IS_SHORTCUT here. If we do that, the app
    // might not be able to see a subsequent Char event. See OnHandleInputEvent
    // in content/renderer/render_widget.cc for details.
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

#if BUILDFLAG(IS_CHROMEOS)
  if (ash::AcceleratorController::Get()->IsDeprecated(accelerator)) {
    return (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown)
               ? content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  content::KeyboardEventProcessingResult result =
      frame_->PreHandleKeyboardEvent(event);
  if (result != content::KeyboardEventProcessingResult::NOT_HANDLED) {
    return result;
  }

  int id;
  if (!FindCommandIdForAccelerator(accelerator, &id)) {
    // |accelerator| is not a browser command, it may be handled by ash (e.g.
    // F4-F10). Report if we handled it.
    if (focus_manager->ProcessAccelerator(accelerator)) {
      return content::KeyboardEventProcessingResult::HANDLED;
    }
    // Otherwise, it's not an accelerator.
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  // If it's a known browser command, we decide whether to consume it now, i.e.
  // reserved by browser.
  chrome::BrowserCommandController* controller = browser_->command_controller();
  // Executing the command may cause |this| object to be destroyed.
  if (controller->IsReservedCommandOrKey(id, event)) {
    UpdateAcceleratorMetrics(accelerator, id);
    return focus_manager->ProcessAccelerator(accelerator)
               ? content::KeyboardEventProcessingResult::HANDLED
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  // BrowserView does not register RELEASED accelerators. So if we can find the
  // command id from |accelerator_table_|, it must be a keydown event. This
  // DCHECK ensures we won't accidentally return NOT_HANDLED for a later added
  // RELEASED accelerator in BrowserView.
  DCHECK_EQ(event.GetType(), blink::WebInputEvent::Type::kRawKeyDown);
  // |accelerator| is a non-reserved browser shortcut (e.g. Ctrl+f).
  return content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT;
}

void BrowserView::PreHandleDragUpdate(const content::DropData& drop_data,
                                      const gfx::PointF& point) {
  if (multi_contents_view_) {
    multi_contents_view_->drop_target_controller().OnWebContentsDragUpdate(
        drop_data, point, IsInSplitView());
  }
}

void BrowserView::PreHandleDragExit() {
  if (multi_contents_view_) {
    multi_contents_view_->drop_target_controller().OnWebContentsDragExit();
  }
}

bool BrowserView::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  if (frame_->HandleKeyboardEvent(event)) {
    return true;
  }

  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

#if BUILDFLAG(IS_MAC)
namespace {
remote_cocoa::mojom::CutCopyPasteCommand CommandFromBrowserCommand(
    int command_id) {
  if (command_id == IDC_CUT) {
    return remote_cocoa::mojom::CutCopyPasteCommand::kCut;
  }
  if (command_id == IDC_COPY) {
    return remote_cocoa::mojom::CutCopyPasteCommand::kCopy;
  }
  CHECK_EQ(command_id, IDC_PASTE);
  return remote_cocoa::mojom::CutCopyPasteCommand::kPaste;
}

}  // namespace
#endif

void BrowserView::Cut() {
  base::RecordAction(UserMetricsAction("Cut"));
  CutCopyPaste(IDC_CUT);
}
void BrowserView::Copy() {
  base::RecordAction(UserMetricsAction("Copy"));
  CutCopyPaste(IDC_COPY);
}
void BrowserView::Paste() {
  base::RecordAction(UserMetricsAction("Paste"));
  CutCopyPaste(IDC_PASTE);
}

// TODO(devint): http://b/issue?id=1117225 Cut, Copy, and Paste are always
// enabled in the page menu regardless of whether the command will do
// anything. When someone selects the menu item, we just act as if they hit
// the keyboard shortcut for the command by sending the associated key press
// to windows. The real fix to this bug is to disable the commands when they
// won't do anything. We'll need something like an overall clipboard command
// manager to do that.
void BrowserView::CutCopyPaste(int command_id) {
#if BUILDFLAG(IS_MAC)
  auto command = CommandFromBrowserCommand(command_id);
  auto* application_host =
      GetWidget() ? remote_cocoa::ApplicationHost::GetForNativeView(
                        GetWidget()->GetNativeView())
                  : nullptr;
  if (application_host) {
    application_host->GetApplication()->ForwardCutCopyPaste(command);
  } else {
    remote_cocoa::ApplicationBridge::ForwardCutCopyPasteToNSApp(command);
  }
#else
  // If a WebContents is focused, call its member method.
  //
  // We could make WebContents register accelerators and then just use the
  // plumbing for accelerators below to dispatch these, but it's not clear
  // whether that would still allow keypresses of ctrl-X/C/V to be sent as
  // key events (and not accelerators) to the WebContents so it can give the web
  // page a chance to override them.
  WebContents* contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (contents) {
    void (WebContents::*method)();
    if (command_id == IDC_CUT) {
      method = &content::WebContents::Cut;
    } else if (command_id == IDC_COPY) {
      method = &content::WebContents::Copy;
    } else {
      method = &content::WebContents::Paste;
    }
    if (DoCutCopyPasteForWebContents(contents, method)) {
      return;
    }

    WebContents* devtools =
        DevToolsWindow::GetInTabWebContents(contents, nullptr);
    if (devtools && DoCutCopyPasteForWebContents(devtools, method)) {
      return;
    }
  }

  // Any Views which want to handle the clipboard commands in the Chrome menu
  // should:
  //   (a) Register ctrl-X/C/V as accelerators
  //   (b) Implement CanHandleAccelerators() to not return true unless they're
  //       focused, as the FocusManager will try all registered accelerator
  //       handlers, not just the focused one.
  // Currently, Textfield (which covers the omnibox and find bar, and likely any
  // other native UI in the future that wants to deal with clipboard commands)
  // does the above.
  ui::Accelerator accelerator;
  GetAccelerator(command_id, &accelerator);
  GetFocusManager()->ProcessAccelerator(accelerator);
#endif  // BUILDFLAG(IS_MAC)
}

std::unique_ptr<FindBar> BrowserView::CreateFindBar() {
  return std::make_unique<FindBarHost>(this);
}

WebContentsModalDialogHost* BrowserView::GetWebContentsModalDialogHost() {
  return GetBrowserViewLayout()->GetWebContentsModalDialogHost();
}

BookmarkBarView* BrowserView::GetBookmarkBarView() const {
  return bookmark_bar_view_.get();
}

LocationBarView* BrowserView::GetLocationBarView() const {
  return toolbar_ ? toolbar_->location_bar() : nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, TabStripModelObserver implementation:

void BrowserView::OnSplitTabChanged(const SplitTabChange& change) {
  CHECK(multi_contents_view_);
  switch (change.type) {
    case SplitTabChange::Type::kAdded: {
      const tabs::TabInterface* active_tab =
          browser_->tab_strip_model()->GetActiveTab();
      if (active_tab->IsSplit()) {
        ShowSplitView(GetContentsView()->HasFocus());
      }
      break;
    }

    case SplitTabChange::Type::kVisualsChanged: {
      const tabs::TabInterface* active_tab =
          browser_->tab_strip_model()->GetActiveTab();

      if (active_tab->GetSplit() == change.split_id) {
        if (change.GetVisualsChange()->new_visual_data().split_ratio() !=
            change.GetVisualsChange()->old_visual_data().split_ratio()) {
          multi_contents_view_->UpdateSplitRatio(
              change.GetVisualsChange()->new_visual_data().split_ratio());
        }
      }
      break;
    }

    case SplitTabChange::Type::kContentsChanged: {
      const tabs::TabInterface* active_tab =
          browser_->tab_strip_model()->GetActiveTab();

      if (active_tab->GetSplit() == change.split_id) {
        UpdateContentsInSplitView(change.GetContentsChange()->prev_tabs(),
                                  change.GetContentsChange()->new_tabs());
      }
      break;
    }

    case SplitTabChange::Type::kRemoved: {
      content::WebContents* active_web_contents =
          multi_contents_view_->GetActiveContentsView()->web_contents();

      if (std::any_of(change.GetRemovedChange()->tabs().begin(),
                      change.GetRemovedChange()->tabs().end(),
                      [active_web_contents](
                          const std::pair<tabs::TabInterface*, int>& pair) {
                        return pair.first->GetContents() == active_web_contents;
                      })) {
        HideSplitView();
      }
      break;
    }
  }
}

void BrowserView::TabChangedAt(content::WebContents* contents,
                               int index,
                               TabChangeType change_type) {
  if (change_type != TabChangeType::kLoadingOnly || contents->IsLoading()) {
    return;
  }

  if (contents != GetActiveWebContents()) {
    return;
  }

  UpdateAccessibleURLForRootView(contents->GetURL());
}

void BrowserView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // When the selected tab changes, elements in the omnibox can change, which
  // can change its preferred size. Re-lay-out the toolbar to reflect the
  // possible change.
  if (selection.selection_changed()) {
    toolbar_->InvalidateLayout();
  }

  if (loading_bar_) {
    loading_bar_->SetWebContents(GetActiveWebContents());
  }

  if (change.type() != TabStripModelChange::kInserted) {
    return;
  }

  for ([[maybe_unused]] const auto& contents : change.GetInsert()->contents) {
#if defined(USE_AURA)
    // WebContents inserted in tabs might not have been added to the root
    // window yet. Per http://crbug/342672 add them now since drawing the
    // WebContents requires root window specific data - information about
    // the screen the WebContents is drawn on, for example.
    if (!contents.contents->GetNativeView()->GetRootWindow()) {
      aura::Window* window = contents.contents->GetNativeView();
      aura::Window* root_window = GetNativeWindow()->GetRootWindow();
      aura::client::ParentWindowWithContext(window, root_window,
                                            root_window->GetBoundsInScreen(),
                                            display::kInvalidDisplayId);
      DCHECK(contents.contents->GetNativeView()->GetRootWindow());
    }
#endif
    if (multi_contents_view_) {
      multi_contents_view_->ExecuteOnEachVisibleContentsView(
          base::BindRepeating([](ContentsWebView* contents_view) {
            contents_view->GetWebContentsCloseHandler()->TabInserted();
          }));
    } else {
      contents_web_view_->GetWebContentsCloseHandler()->TabInserted();
    }
  }

  UpdateAccessibleNameForRootView();
}

void BrowserView::TabStripEmpty() {
  // Make sure all optional UI is removed before we are destroyed, otherwise
  // there will be consequences (since our view hierarchy will still have
  // references to freed views).
  UpdateUIForContents(nullptr);
}

void BrowserView::WillCloseAllTabs(TabStripModel* tab_strip_model) {
  if (multi_contents_view_) {
    multi_contents_view_->ExecuteOnEachVisibleContentsView(
        base::BindRepeating([](ContentsWebView* contents_view) {
          contents_view->GetWebContentsCloseHandler()->WillCloseAllTabs();
        }));
  } else {
    contents_web_view_->GetWebContentsCloseHandler()->WillCloseAllTabs();
  }
}

void BrowserView::CloseAllTabsStopped(TabStripModel* tab_strip_model,
                                      CloseAllStoppedReason reason) {
  if (reason != kCloseAllCanceled) {
    return;
  }
  if (multi_contents_view_) {
    multi_contents_view_->ExecuteOnEachVisibleContentsView(
        base::BindRepeating([](ContentsWebView* contents_view) {
          contents_view->GetWebContentsCloseHandler()->CloseAllTabsCanceled();
        }));
  } else {
    contents_web_view_->GetWebContentsCloseHandler()->CloseAllTabsCanceled();
  }
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ui::AcceleratorProvider implementation:

bool BrowserView::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  // Let's let the ToolbarView own the canonical implementation of this method.
  return toolbar_->GetAcceleratorForCommandId(command_id, accelerator);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, views::WidgetDelegate implementation:

bool BrowserView::CanResize() const {
  return WidgetDelegate::CanResize() &&
         GetWebApiWindowResizable().value_or(true);
}

bool BrowserView::CanFullscreen() const {
  return WidgetDelegate::CanFullscreen() &&
         GetWebApiWindowResizable().value_or(true);
}

bool BrowserView::CanMaximize() const {
  return WidgetDelegate::CanMaximize() &&
         GetWebApiWindowResizable().value_or(true);
}

bool BrowserView::CanActivate() const {
  javascript_dialogs::AppModalDialogQueue* queue =
      javascript_dialogs::AppModalDialogQueue::GetInstance();
  if (!queue->active_dialog() || !queue->active_dialog()->view() ||
      !queue->active_dialog()->view()->IsShowing()) {
    return true;
  }

  // If another browser is app modal, flash and activate the modal browser. This
  // has to be done in a post task, otherwise if the user clicked on a window
  // that doesn't have the modal dialog the windows keep trying to get the focus
  // from each other on Windows. http://crbug.com/141650.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserView::ActivateAppModalDialog,
                                weak_ptr_factory_.GetWeakPtr()));
  return false;
}

std::u16string BrowserView::GetWindowTitle() const {
  std::u16string title =
      browser_->GetWindowTitleForCurrentTab(true /* include_app_name */);
#if BUILDFLAG(IS_MAC)
  bool any_tab_playing_audio = false;
  bool any_tab_playing_muted_audio = false;
  GetAnyTabAudioStates(browser_.get(), &any_tab_playing_audio,
                       &any_tab_playing_muted_audio);
  if (any_tab_playing_audio) {
    title = l10n_util::GetStringFUTF16(IDS_WINDOW_AUDIO_PLAYING_MAC, title,
                                       u"\U0001F50A");
  } else if (any_tab_playing_muted_audio) {
    title = l10n_util::GetStringFUTF16(IDS_WINDOW_AUDIO_MUTING_MAC, title,
                                       u"\U0001F507");
  }
#endif
  return title;
}

std::u16string BrowserView::GetAccessibleWindowTitle() const {
  // If there is a focused and visible tab-modal dialog, report the dialog's
  // title instead of the page title.
  views::Widget* tab_modal =
      views::ViewAccessibilityUtils::GetFocusedChildWidgetForAccessibility(
          this);
  if (tab_modal) {
    return tab_modal->widget_delegate()->GetAccessibleWindowTitle();
  }

  return GetAccessibleWindowTitleForChannelAndProfile(chrome::GetChannel(),
                                                      browser_->profile());
}

std::u16string BrowserView::GetAccessibleWindowTitleForChannelAndProfile(
    version_info::Channel channel,
    Profile* profile) const {
  // Start with the tab title, which includes properties of the tab
  // like playing audio or network error.
  int active_index = browser_->tab_strip_model()->active_index();
  std::u16string title;
  if (active_index > -1) {
    title = GetAccessibleTabLabel(active_index, /* include_app_name */ false);
  } else {
    title = browser_->GetWindowTitleForCurrentTab(false /* include_app_name */);
  }

  // Add the name of the browser, unless this is an app window.
  if (browser()->is_type_normal() || browser()->is_type_popup()) {
    int message_id;
    switch (channel) {
      case version_info::Channel::CANARY:
        message_id = IDS_ACCESSIBLE_CANARY_BROWSER_WINDOW_TITLE_FORMAT;
        break;
      case version_info::Channel::DEV:
        message_id = IDS_ACCESSIBLE_DEV_BROWSER_WINDOW_TITLE_FORMAT;
        break;
      case version_info::Channel::BETA:
        message_id = IDS_ACCESSIBLE_BETA_BROWSER_WINDOW_TITLE_FORMAT;
        break;
      default:
        // Stable or unknown.
        message_id = IDS_ACCESSIBLE_BROWSER_WINDOW_TITLE_FORMAT;
        break;
    }
    title = l10n_util::GetStringFUTF16(message_id, title);
  }

  // Finally annotate with the user - add Incognito or guest if it's an
  // incognito or guest window, otherwise use the avatar name.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (profile->IsGuestSession()) {
    title = l10n_util::GetStringFUTF16(IDS_ACCESSIBLE_GUEST_WINDOW_TITLE_FORMAT,
                                       title);
  } else if (profile->IsIncognitoProfile()) {
    title = l10n_util::GetStringFUTF16(
        IDS_ACCESSIBLE_INCOGNITO_WINDOW_TITLE_FORMAT, title);
  } else if (!profile->IsOffTheRecord() &&
             profile_manager->GetNumberOfProfiles() > 1) {
    std::u16string profile_name =
        profiles::GetAvatarNameForProfile(profile->GetPath());
    if (!profile_name.empty()) {
      title = l10n_util::GetStringFUTF16(
          IDS_ACCESSIBLE_WINDOW_TITLE_WITH_PROFILE_FORMAT, title, profile_name);
    }
  }

  return title;
}

void BrowserView::UpdateAccessibleNameForAllTabs() {
  for (int i = 0; i < tabstrip_->GetTabCount(); ++i) {
    tabstrip_->tab_at(i)->UpdateAccessibleName();
  }
}

// This function constructs the accessible label for a tab, which is used by
// assistive technologies to provide meaningful descriptions of the tab's
// content. The label is based on various properties of the tab, such as the
// title, group, alerts and memory usage.
//
// Note: If any new parameters are added or existing ones are removed that
// affect the accessible name, ensure that the corresponding logic in
// Tab::UpdateAccessibleName is updated accordingly to maintain consistency.
std::u16string BrowserView::GetAccessibleTabLabel(int index,
                                                  bool is_for_tab) const {
  std::u16string title = is_for_tab ? browser_->GetTitleForTab(index)
                                    : browser_->GetWindowTitleForTab(index);

  std::optional<tab_groups::TabGroupId> group =
      tabstrip_->tab_at(index)->group();
  if (group.has_value()) {
    std::u16string group_title = tabstrip_->GetGroupTitle(group.value());
    if (group_title.empty()) {
      title = l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_UNNAMED_GROUP_FORMAT,
                                         title);
    } else {
      title = l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_NAMED_GROUP_FORMAT,
                                         title, group_title);
    }
  }

  // Tab is pinned.
  if (tabstrip_->IsTabPinned(tabstrip_->tab_at(index))) {
    title = l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_PINNED_FORMAT, title);
  }

  // Tab has crashed.
  if (tabstrip_->IsTabCrashed(index)) {
    return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_CRASHED_FORMAT, title);
  }

  // Network error interstitial.
  if (tabstrip_->TabHasNetworkError(index)) {
    return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_NETWORK_ERROR_FORMAT,
                                      title);
  }

  // Tab has a pending permission request.
  if (toolbar_ && toolbar_->location_bar() &&
      toolbar_->location_bar()->GetChipController() &&
      toolbar_->location_bar()
          ->GetChipController()
          ->IsPermissionPromptChipVisible()) {
    return l10n_util::GetStringFUTF16(
        IDS_TAB_AX_LABEL_PERMISSION_REQUESTED_FORMAT, title);
  }

  // Alert tab states.
  std::optional<tabs::TabAlert> alert = tabstrip_->GetTabAlertState(index);
  if (alert.has_value()) {
    switch (alert.value()) {
      case tabs::TabAlert::AUDIO_PLAYING:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_AUDIO_PLAYING_FORMAT, title);
        break;
      case tabs::TabAlert::USB_CONNECTED:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_USB_CONNECTED_FORMAT, title);
        break;
      case tabs::TabAlert::BLUETOOTH_CONNECTED:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_BLUETOOTH_CONNECTED_FORMAT, title);
        break;
      case tabs::TabAlert::BLUETOOTH_SCAN_ACTIVE:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_BLUETOOTH_SCAN_ACTIVE_FORMAT, title);
        break;
      case tabs::TabAlert::HID_CONNECTED:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_HID_CONNECTED_FORMAT, title);
        break;
      case tabs::TabAlert::SERIAL_CONNECTED:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_SERIAL_CONNECTED_FORMAT, title);
        break;
      case tabs::TabAlert::MEDIA_RECORDING:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_MEDIA_RECORDING_FORMAT, title);
        break;
      case tabs::TabAlert::AUDIO_RECORDING:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_AUDIO_RECORDING_FORMAT, title);
        break;
      case tabs::TabAlert::VIDEO_RECORDING:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_VIDEO_RECORDING_FORMAT, title);
        break;
      case tabs::TabAlert::AUDIO_MUTING:
        title = l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_AUDIO_MUTING_FORMAT,
                                           title);
        break;
      case tabs::TabAlert::TAB_CAPTURING:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_TAB_CAPTURING_FORMAT, title);
        break;
      case tabs::TabAlert::PIP_PLAYING:
        title = l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_PIP_PLAYING_FORMAT,
                                           title);
        break;
      case tabs::TabAlert::DESKTOP_CAPTURING:
        title = l10n_util::GetStringFUTF16(
            IDS_TAB_AX_LABEL_DESKTOP_CAPTURING_FORMAT, title);
        break;
      case tabs::TabAlert::VR_PRESENTING_IN_HEADSET:
        title =
            l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_VR_PRESENTING, title);
        break;
      case tabs::TabAlert::GLIC_ACCESSING:
#if BUILDFLAG(ENABLE_GLIC)
        title =
            l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_GLIC_ACCESSING, title);
        break;
#else
        NOTREACHED();
#endif
      case tabs::TabAlert::GLIC_SHARING:
#if BUILDFLAG(ENABLE_GLIC)
        title =
            l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_GLIC_SHARING, title);
        break;
#else
        NOTREACHED();
#endif
    }
  }

  const TabRendererData& tab_data = tabstrip_->tab_at(index)->data();
  if (tab_data.should_show_discard_status) {
    title = l10n_util::GetStringFUTF16(IDS_TAB_AX_INACTIVE_TAB, title);
    if (tab_data.discarded_memory_savings_in_bytes > 0) {
      title = l10n_util::GetStringFUTF16(
          IDS_TAB_AX_MEMORY_SAVINGS, title,
          ui::FormatBytes(tab_data.discarded_memory_savings_in_bytes));
    }
  } else if (tab_data.tab_resource_usage &&
             tab_data.tab_resource_usage->memory_usage_in_bytes() > 0) {
    const uint64_t memory_used =
        tab_data.tab_resource_usage->memory_usage_in_bytes();
    const bool is_high_memory_usage =
        tab_data.tab_resource_usage->is_high_memory_usage();
    if (is_high_memory_usage || is_for_tab) {
      const int message_id = is_high_memory_usage ? IDS_TAB_AX_HIGH_MEMORY_USAGE
                                                  : IDS_TAB_AX_MEMORY_USAGE;
      title = l10n_util::GetStringFUTF16(message_id, title,
                                         ui::FormatBytes(memory_used));
    }
  } else if (tab_data.collaboration_messaging &&
             tab_data.collaboration_messaging->HasMessage()) {
    std::u16string given_name = tab_data.collaboration_messaging->given_name();

    switch (tab_data.collaboration_messaging->collaboration_event()) {
      case collaboration::messaging::CollaborationEvent::TAB_ADDED:
        title = l10n_util::GetStringFUTF16(
                    IDS_DATA_SHARING_RECENT_ACTIVITY_MEMBER_ADDED_THIS_TAB,
                    given_name) +
                u", " + title;
        break;
      case collaboration::messaging::CollaborationEvent::TAB_UPDATED:
        title = l10n_util::GetStringFUTF16(
                    IDS_DATA_SHARING_RECENT_ACTIVITY_MEMBER_CHANGED_THIS_TAB,
                    given_name) +
                u", " + title;
        break;
      default:
        NOTREACHED();
    }
  }

  return title;
}

std::vector<views::NativeViewHost*>
BrowserView::GetNativeViewHostsForTopControlsSlide() const {
  std::vector<views::NativeViewHost*> results;
  if (multi_contents_view_) {
    results.push_back(multi_contents_view_->GetActiveContentsView()->holder());
    results.push_back(
        multi_contents_view_->GetInactiveContentsView()->holder());
  } else {
    results.push_back(contents_web_view_->holder());
  }

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (webui_tab_strip_) {
    results.push_back(webui_tab_strip_->GetNativeViewHost());
  }
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)

  return results;
}

void BrowserView::ReparentTopContainerForEndOfImmersive() {
  if (top_container()->parent() == this) {
    return;
  }

  overlay_view_->SetVisible(false);
  top_container()->DestroyLayer();
  AddChildViewAt(top_container(), 0);
  EnsureFocusOrder();
}

void BrowserView::EnsureFocusOrder() {
  // We want the infobar to come before the content pane, but after the bookmark
  // bar (if present) or top container (i.e. toolbar, again if present).
  if (bookmark_bar_view_ && bookmark_bar_view_->parent() == this) {
    infobar_container_->InsertAfterInFocusList(bookmark_bar_view_.get());
  } else if (top_container_->parent() == this) {
    infobar_container_->InsertAfterInFocusList(top_container_);
  }

  // We want the download shelf to come after the contents container (which also
  // contains the debug console, etc.) This prevents it from intruding into the
  // focus order, but makes it easily accessible by using SHIFT-TAB (reverse
  // focus traversal) from the toolbar/omnibox.
  if (download_shelf_ && contents_container_) {
    download_shelf_->GetView()->InsertAfterInFocusList(contents_container_);
  }

#if DCHECK_IS_ON()
  // Make sure we didn't create any cycles in the focus order.
  CheckFocusListForCycles(top_container_);
#endif
}

bool BrowserView::CanChangeWindowIcon() const {
  // The logic of this function needs to be same as GetWindowIcon().
  if (browser_->is_type_devtools()) {
    return false;
  }
  if (browser_->app_controller()) {
    return true;
  }
#if BUILDFLAG(IS_CHROMEOS)
  // On ChromeOS, the tabbed browser always use a static image for the window
  // icon. See GetWindowIcon().
  if (browser_->is_type_normal()) {
    return false;
  }
#endif
  return true;
}

views::View* BrowserView::GetInitiallyFocusedView() {
  return nullptr;
}

#if BUILDFLAG(IS_WIN)
bool BrowserView::GetSupportsTitle() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_TITLEBAR) ||
         WebUITabStripContainerView::SupportsTouchableTabStrip(browser());
}

bool BrowserView::GetSupportsIcon() const {
  return browser_->SupportsWindowFeature(Browser::FEATURE_TITLEBAR);
}
#endif

bool BrowserView::ShouldShowWindowTitle() const {
#if BUILDFLAG(IS_CHROMEOS)
  // For Chrome OS only, trusted windows (apps and settings) do not show a
  // title, crbug.com/119411. Child windows (i.e. popups) do show a title.
  if (browser_->is_trusted_source() || AppUsesWindowControlsOverlay()) {
    return false;
  }
#elif BUILDFLAG(IS_WIN)
  // On Windows in touch mode we display a window title.
  if (WebUITabStripContainerView::UseTouchableTabStrip(browser())) {
    return true;
  }
#endif

  return browser_->SupportsWindowFeature(Browser::FEATURE_TITLEBAR);
}

bool BrowserView::ShouldShowWindowIcon() const {
#if !BUILDFLAG(IS_CHROMEOS)
  if (GetIsWebAppType() && !GetSupportsTabStrip()) {
    return true;
  }
#endif
  return WidgetDelegate::ShouldShowWindowIcon();
}

ui::ImageModel BrowserView::GetWindowAppIcon() {
  web_app::AppBrowserController* app_controller = browser()->app_controller();
  return app_controller ? app_controller->GetWindowAppIcon() : GetWindowIcon();
}

ui::ImageModel BrowserView::GetWindowIcon() {
  // Use the default icon for devtools.
  if (browser_->is_type_devtools()) {
    return ui::ImageModel();
  }

  // Hosted apps always show their app icon.
  web_app::AppBrowserController* app_controller = browser()->app_controller();
  if (app_controller) {
    return app_controller->GetWindowIcon();
  }

#if BUILDFLAG(IS_CHROMEOS)
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  if (browser_->is_type_normal()) {
    return ui::ImageModel::FromImage(rb.GetImageNamed(IDR_CHROME_APP_ICON_192));
  }
  auto* window = GetNativeWindow();
  int override_window_icon_resource_id =
      window ? window->GetProperty(ash::kOverrideWindowIconResourceIdKey) : -1;
  if (override_window_icon_resource_id >= 0) {
    return ui::ImageModel::FromImage(
        rb.GetImageNamed(override_window_icon_resource_id));
  }
#endif

  if (!browser_->is_type_normal()) {
    return ui::ImageModel::FromImage(browser_->GetCurrentPageIcon());
  }

  return ui::ImageModel();
}

bool BrowserView::ExecuteWindowsCommand(int command_id) {
  // Translate WM_APPCOMMAND command ids into a command id that the browser
  // knows how to handle.
  int command_id_from_app_command = GetCommandIDForAppCommandID(command_id);
  if (command_id_from_app_command != -1) {
    command_id = command_id_from_app_command;
  }

  return chrome::ExecuteCommand(browser_.get(), command_id);
}

std::string BrowserView::GetWindowName() const {
  return chrome::GetWindowName(browser_.get());
}

bool BrowserView::ShouldSaveWindowPlacement() const {
  // If IsFullscreen() is true, we've just changed into fullscreen mode, and
  // we're catching the going-into-fullscreen sizing and positioning calls,
  // which we want to ignore.
  return !IsFullscreen() && frame_->ShouldSaveWindowPlacement() &&
         chrome::ShouldSaveWindowPlacement(browser_.get());
}

void BrowserView::SaveWindowPlacement(const gfx::Rect& bounds,
                                      ui::mojom::WindowShowState show_state) {
  DCHECK(ShouldSaveWindowPlacement());

  WidgetDelegate::SaveWindowPlacement(bounds, show_state);
  gfx::Rect saved_bounds = bounds;
  if (chrome::SavedBoundsAreContentBounds(browser_.get())) {
    // Invert the transformation done in GetSavedWindowPlacement().
    gfx::Size client_size =
        frame_->GetFrameView()->GetBoundsForClientView().size();
    if (IsToolbarVisible()) {
      client_size.Enlarge(0, -toolbar_->GetPreferredSize().height());
    }
    saved_bounds.set_size(client_size);
  }
  chrome::SaveWindowPlacement(browser_.get(), saved_bounds, show_state);
}

bool BrowserView::GetSavedWindowPlacement(
    const views::Widget* widget,
    gfx::Rect* bounds,
    ui::mojom::WindowShowState* show_state) const {
  chrome::GetSavedWindowBoundsAndShowState(browser_.get(), bounds, show_state);
  // TODO(crbug.com/40092782): Generalize this code for app and non-app popups?
  if (chrome::SavedBoundsAreContentBounds(browser_.get()) &&
      browser_->is_type_popup()) {
    // This is normal non-app popup window. The value passed in |bounds|
    // represents two pieces of information:
    // - the position of the window, in screen coordinates (outer position).
    // - the size of the content area (inner size).
    // We need to use these values to determine the appropriate size and
    // position of the resulting window.
    if (IsToolbarVisible()) {
      // If we're showing the toolbar, we need to adjust |*bounds| to include
      // its desired height, since the toolbar is considered part of the
      // window's client area as far as GetWindowBoundsForClientBounds is
      // concerned...
      bounds->set_height(bounds->height() +
                         toolbar_->GetPreferredSize().height());
    }

    gfx::Rect rect =
        frame_->non_client_view()->GetWindowBoundsForClientBounds(*bounds);
    rect.set_origin(bounds->origin());

    // Set a default popup origin if the x/y coordinates are 0 and the original
    // values were not known to be explicitly specified via window.open() in JS.
    if (rect.origin().IsOrigin() &&
        browser_->create_params().initial_origin_specified !=
            Browser::ValueSpecified::kSpecified) {
      rect.set_origin(WindowSizer::GetDefaultPopupOrigin(rect.size()));
    }

    // Constrain the final bounds to the target screen's available area. Bounds
    // enforcement applied earlier does not know the specific frame dimensions,
    // but generally yields bounds on the appropriate screen.
    auto display = display::Screen::GetScreen()->GetDisplayMatching(rect);
    rect.AdjustToFit(display.work_area());

    *bounds = rect;
    *show_state = ui::mojom::WindowShowState::kNormal;
  }

  // We return true because we can _always_ locate reasonable bounds using the
  // WindowSizer, and we don't want to trigger the Window's built-in "size to
  // default" handling because the browser window has no default preferred size.
  return true;
}

views::View* BrowserView::GetContentsView() {
  if (multi_contents_view_) {
    return multi_contents_view_->GetActiveContentsView();
  } else {
    return contents_web_view_;
  }
}

views::ClientView* BrowserView::CreateClientView(views::Widget* widget) {
  return this;
}

views::View* BrowserView::CreateOverlayView() {
  overlay_view_ = new TopContainerOverlayView(weak_ptr_factory_.GetWeakPtr());
  overlay_view_->SetVisible(false);
  overlay_view_->SetEventTargeter(std::make_unique<views::ViewTargeter>(
      std::make_unique<OverlayViewTargeterDelegate>()));
  return overlay_view_;
}

#if BUILDFLAG(IS_MAC)
views::View* BrowserView::CreateMacOverlayView() {
  DCHECK(UsesImmersiveFullscreenMode());

  auto create_overlay_widget = [this](views::Widget* parent) -> views::Widget* {
    views::Widget::InitParams params(
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.child = true;
    params.parent = parent->GetNativeView();
    params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.is_overlay = true;
    params.name = "mac-fullscreen-overlay";
    OverlayWidget* overlay_widget = new OverlayWidget(GetWidget());

    // When the overlay is used some Views are moved to the overlay_widget. When
    // this happens we want the fullscreen state of the overlay_widget to match
    // that of BrowserView's Widget. Without this, some views would not think
    // they are in a fullscreen Widget, when we want them to behave as though
    // they are in a fullscreen Widget.
    overlay_widget->SetCheckParentForFullscreen();

    overlay_widget->Init(std::move(params));
    overlay_widget->SetNativeWindowProperty(kBrowserViewKey, this);

    // Disable sublevel widget layering because in fullscreen the NSWindow of
    // `overlay_widget_` is reparented to a AppKit-owned NSWindow that does not
    // have an associated Widget. This will cause issues in sublevel manager
    // which operates at the Widget level.
    if (overlay_widget->GetSublevelManager()) {
      overlay_widget->parent()->GetSublevelManager()->OnWidgetChildRemoved(
          overlay_widget->parent(), overlay_widget);
    }

    return overlay_widget;
  };

  // Create the toolbar overlay widget.
  overlay_widget_ = create_overlay_widget(GetWidget());

  // Create a new TopContainerOverlayView. The tab strip, omnibox, bookmarks
  // etc. will be contained within this view. Right clicking on the blank space
  // that is not taken up by the child views should show the context menu. Set
  // the BrowserFrame as the context menu controller to handle displaying the
  // top container context menu.
  std::unique_ptr<TopContainerOverlayView> overlay_view =
      std::make_unique<TopContainerOverlayView>(weak_ptr_factory_.GetWeakPtr());
  overlay_view->set_context_menu_controller(frame());

  overlay_view->SetEventTargeter(std::make_unique<views::ViewTargeter>(
      std::make_unique<OverlayViewTargeterDelegate>()));
  overlay_view_ = overlay_view.get();
  overlay_widget_->GetRootView()->AddChildView(std::move(overlay_view));

  if (UsesImmersiveFullscreenTabbedMode()) {
    // Create the tab overlay widget as a child of overlay_widget_.
    tab_overlay_widget_ = create_overlay_widget(overlay_widget_);
    std::unique_ptr<TabContainerOverlayView> tab_overlay_view =
        std::make_unique<TabContainerOverlayView>(
            weak_ptr_factory_.GetWeakPtr());
    tab_overlay_view->set_context_menu_controller(frame());
    tab_overlay_view->SetEventTargeter(std::make_unique<views::ViewTargeter>(
        std::make_unique<OverlayViewTargeterDelegate>()));
    tab_overlay_view_ = tab_overlay_view.get();
    tab_overlay_widget_->GetRootView()->AddChildView(
        std::move(tab_overlay_view));
  }

  return overlay_view_;
}
#endif  // IS_MAC

void BrowserView::OnWidgetDestroying(views::Widget* widget) {
  DCHECK(widget_observation_.IsObservingSource(widget));
  widget_observation_.Reset();
  // Destroy any remaining WebContents early on. Doing so may result in
  // calling back to one of the Views/LayoutManagers or supporting classes of
  // BrowserView. By destroying here we ensure all said classes are valid.
  // Note: The BrowserViewTest tests rely on the contents being destroyed in the
  // order that they were present in the tab strip.
  while (browser()->tab_strip_model()->count()) {
    browser()->tab_strip_model()->DetachAndDeleteWebContentsAt(0);
  }
  // Destroy the fullscreen control host, as it observes the native window.
  fullscreen_control_host_.reset();
}

void BrowserView::OnWidgetActivationChanged(views::Widget* widget,
                                            bool active) {
  if (browser_->window()) {
    if (active) {
      if (restore_focus_on_activation_.has_value() &&
          restore_focus_on_activation_.value()) {
        restore_focus_on_activation_ = false;

        // Set initial focus change on the first activation if there is no
        // modal dialog.
        if (!WidgetHasChildModalDialog(GetWidget())) {
          RestoreFocus();
        }
      }
    }
  }

  if (!extension_keybinding_registry_ &&
      GetFocusManager()) {  // focus manager can be null in tests.
    extension_keybinding_registry_ =
        std::make_unique<ExtensionKeybindingRegistryViews>(
            browser_->profile(), GetFocusManager(),
            extensions::ExtensionKeybindingRegistry::ALL_EXTENSIONS, this);
  }

  extensions::ExtensionCommandsGlobalRegistry* registry =
      extensions::ExtensionCommandsGlobalRegistry::Get(browser_->profile());
  if (active) {
    registry->set_registry_for_active_window(
        extension_keybinding_registry_.get());
  } else if (registry->registry_for_active_window() ==
             extension_keybinding_registry_.get()) {
    registry->set_registry_for_active_window(nullptr);
  }

  immersive_mode_controller()->OnWidgetActivationChanged(widget, active);
}

void BrowserView::OnWidgetBoundsChanged(views::Widget* widget,
                                        const gfx::Rect& new_bounds) {
  TryNotifyWindowBoundsChanged(new_bounds);
}

void BrowserView::OnWindowBeginUserBoundsChange() {
  if (interactive_resize_in_progress_) {
    return;
  }
  WebContents* web_contents = GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  interactive_resize_in_progress_ = true;
}

void BrowserView::OnWindowEndUserBoundsChange() {
  interactive_resize_in_progress_ = false;
  TryNotifyWindowBoundsChanged(GetWidget()->GetWindowBoundsInScreen());
}

void BrowserView::OnWidgetMove() {
  if (!initialized_) {
    // Creating the widget can trigger a move. Ignore it until we've initialized
    // things.
    return;
  }

  // Cancel any tabstrip animations, some of them may be invalidated by the
  // window being repositioned.
  // Comment out for one cycle to see if this fixes dist tests.
  // tabstrip_->DestroyDragController();

  // There may be no status bubbles if this is invoked during construction.
  std::vector<StatusBubble*> status_bubbles = GetStatusBubbles();
  for (StatusBubble* status_bubble : status_bubbles) {
    static_cast<StatusBubbleViews*>(status_bubble)->Reposition();
  }

  BookmarkBubbleView::Hide();

  // Close the omnibox popup, if any.
  LocationBarView* location_bar_view = GetLocationBarView();
  if (location_bar_view) {
    location_bar_view->GetOmniboxView()->CloseOmniboxPopup();
  }
}

views::Widget* BrowserView::GetWidget() {
  return View::GetWidget();
}

const views::Widget* BrowserView::GetWidget() const {
  return View::GetWidget();
}

void BrowserView::CreateTabSearchBubble(
    const tab_search::mojom::TabSearchSection section,
    const tab_search::mojom::TabOrganizationFeature organization_feature) {
  // Do not spawn the bubble if using the WebUITabStrip.
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (WebUITabStripContainerView::UseTouchableTabStrip(browser_.get())) {
    return;
  }
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)

  if (auto* tab_search_host = GetTabSearchBubbleHost()) {
    tab_search_host->ShowTabSearchBubble(true, section, organization_feature);
  }
}

void BrowserView::CloseTabSearchBubble() {
  if (auto* tab_search_host = GetTabSearchBubbleHost()) {
    tab_search_host->CloseTabSearchBubble();
  }
}

void BrowserView::ShowSplitView(bool focus_active_view) {
  CHECK(multi_contents_view_);
  const int active_index = browser_->tab_strip_model()->active_index();

  std::optional<split_tabs::SplitTabId> split_tab_id =
      browser_->tab_strip_model()->GetTabAtIndex(active_index)->GetSplit();

  CHECK(split_tab_id.has_value());
  split_tabs::SplitTabData* split_data =
      browser_->tab_strip_model()->GetSplitData(split_tab_id.value());

  std::vector<tabs::TabInterface*> split_tabs = split_data->ListTabs();

  for (size_t i = 0; tabs::TabInterface* tab : split_tabs) {
    multi_contents_view_->SetWebContentsAtIndex(tab->GetContents(), i++);
  }
  const int first_split_tab_index =
      browser_->tab_strip_model()->GetIndexOfTab(split_tabs[0]);
  const int relative_active_position = active_index - first_split_tab_index;
  multi_contents_view_->SetActiveIndex(relative_active_position);

  multi_contents_view_->UpdateSplitRatio(
      split_data->visual_data()->split_ratio());

  if (focus_active_view) {
    multi_contents_view_->GetActiveContentsView()->RequestFocus();
  }
}

void BrowserView::HideSplitView() {
  CHECK(multi_contents_view_);
  multi_contents_view_->CloseSplitView();
}

void BrowserView::UpdateActiveTabInSplitView() {
  CHECK(multi_contents_view_ && multi_contents_view_->IsInSplitView());
  const int active_index = browser_->tab_strip_model()->active_index();

  std::optional<split_tabs::SplitTabId> split_tab_id =
      browser_->tab_strip_model()->GetTabAtIndex(active_index)->GetSplit();

  CHECK(split_tab_id.has_value());

  tabs::TabInterface* first_tab = browser_->tab_strip_model()
                                      ->GetSplitData(split_tab_id.value())
                                      ->ListTabs()[0];
  const int first_split_tab_index =
      browser_->tab_strip_model()->GetIndexOfTab(first_tab);
  const int relative_active_position = active_index - first_split_tab_index;
  multi_contents_view_->SetActiveIndex(relative_active_position);

  // When active tab changes inside a split, it's generally due to focus change.
  // However, there are cases where inactive tab can be activated without a
  // focus change e.g. using tab shortcuts and in these cases update focus.
  if (GetWidget()->IsActive() &&
      multi_contents_view_->GetInactiveContentsView()->HasFocus()) {
    multi_contents_view_->GetActiveContentsView()->RequestFocus();
  }
}

void BrowserView::UpdateContentsInSplitView(
    const std::vector<std::pair<tabs::TabInterface*, int>>& prev_tabs,
    const std::vector<std::pair<tabs::TabInterface*, int>>& new_tabs) {
  CHECK(multi_contents_view_ && multi_contents_view_->IsInSplitView());

  std::optional<split_tabs::SplitTabId> split_id =
      browser_->GetActiveTabInterface()->GetSplit();
  CHECK(split_id.has_value());

  split_tabs::SplitTabData* split_data =
      browser_->tab_strip_model()->GetSplitData(split_id.value());
  const int first_split_tab_index =
      browser_->tab_strip_model()->GetIndexOfTab(split_data->ListTabs()[0]);

  const bool active_view_has_focus =
      multi_contents_view_->GetActiveContentsView()->HasFocus();

  // Clear web contents for prev_tabs in preparation to reset for new_tabs.
  multi_contents_view_->GetInactiveContentsView()->SetWebContents(nullptr);
  multi_contents_view_->GetActiveContentsView()->SetWebContents(nullptr);

  // Set web contents in multi_contents_view_ to match new_tabs and update the
  // active multi_contents_view_ index.
  for (std::pair<tabs::TabInterface*, int> split_tab_with_index : new_tabs) {
    CHECK(split_id == split_tab_with_index.first->GetSplit());
    int relative_index = split_tab_with_index.second - first_split_tab_index;
    multi_contents_view_->SetWebContentsAtIndex(
        split_tab_with_index.first->GetContents(), relative_index);
    if (split_tab_with_index.first->IsActivated()) {
      multi_contents_view_->SetActiveIndex(relative_index);
    }
  }
  // Focus the active contents view if it previously had focus prior to swap.
  if (active_view_has_focus) {
    multi_contents_view_->GetActiveContentsView()->RequestFocus();
  }
}

bool BrowserView::IsTabChangeInSplitView(content::WebContents* old_contents,
                                         content::WebContents* new_contents) {
  return multi_contents_view_ && multi_contents_view_->IsInSplitView() &&
         multi_contents_view_->GetActiveContentsView()->web_contents() ==
             old_contents &&
         multi_contents_view_->GetInactiveContentsView()->web_contents() ==
             new_contents;
}

void BrowserView::MaybeUpdateStoredFocusForWebContents(
    content::WebContents* web_contents) {
  ChromeWebContentsViewFocusHelper* focus_helper =
      ChromeWebContentsViewFocusHelper::FromWebContents(web_contents);
  if (!focus_helper) {
    return;
  }

  // In the case that the last focused view of the WebContents is a
  // ContentsWebView, but not the ContentsWebView hosting the WebContents
  // itself, we must reset the stored focus to prevent incorrect tab
  // activation behavior when the split view is swapped in during a tab switch.
  ContentsWebView* focused_view =
      views::AsViewClass<ContentsWebView>(focus_helper->GetStoredFocus());
  if (focused_view && focused_view->web_contents() != web_contents) {
    focus_helper->SetStoredFocusView(GetContentsView());
  }
}

std::vector<ContentsWebView*> BrowserView::GetAllVisibleContentsWebViews() {
  std::vector<ContentsWebView*> contents_views;
  if (multi_contents_view_) {
    contents_views.push_back(multi_contents_view_->GetActiveContentsView());
    ContentsWebView* inactive_contents_view =
        multi_contents_view_->GetInactiveContentsView();
    if (inactive_contents_view->GetVisible()) {
      contents_views.push_back(inactive_contents_view);
    }
  } else {
    contents_views.push_back(contents_web_view_);
  }
  return contents_views;
}

void BrowserView::RevealTabStripIfNeeded() {
  if (!immersive_mode_controller_->IsEnabled()) {
    return;
  }

  std::unique_ptr<ImmersiveRevealedLock> revealer =
      immersive_mode_controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_YES);
  auto delete_revealer = base::BindOnce(
      [](std::unique_ptr<ImmersiveRevealedLock>) {}, std::move(revealer));
  constexpr auto kDefaultDelay = base::Seconds(1);
  constexpr auto kZeroDelay = base::Seconds(0);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, std::move(delete_revealer),
      g_disable_revealer_delay_for_testing ? kZeroDelay : kDefaultDelay);
}

void BrowserView::GetAccessiblePanes(std::vector<views::View*>* panes) {
  // This should be in the order of pane traversal of the panes using F6
  // (Windows) or Ctrl+Back/Forward (Chrome OS).  If one of these is
  // invisible or has no focusable children, it will be automatically
  // skipped.
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (webui_tab_strip_) {
    panes->push_back(webui_tab_strip_);
  }
#endif
  // If activity indicators or a permission request chip is visible, it must be
  // in the first position in the pane traversal order to be easily accessible
  // for keyboard users.
  if (base::FeatureList::IsEnabled(
          content_settings::features::kLeftHandSideActivityIndicators)) {
    if (toolbar_ && toolbar_->location_bar() &&
        toolbar_->location_bar()
            ->permission_dashboard_controller()
            ->permission_dashboard_view()
            ->GetVisible()) {
      panes->push_back(toolbar_->location_bar()
                           ->permission_dashboard_controller()
                           ->permission_dashboard_view());
    }
  } else if (toolbar_ && toolbar_->location_bar() &&
             toolbar_->location_bar()->GetChipController() &&
             toolbar_->location_bar()
                 ->GetChipController()
                 ->IsPermissionPromptChipVisible()) {
    panes->push_back(toolbar_->location_bar()->GetChipController()->chip());
  }

  panes->push_back(toolbar_button_provider_->GetAsAccessiblePaneView());
  if (tab_strip_region_view_) {
    panes->push_back(tab_strip_region_view_);
  }
  if (toolbar_ && toolbar_->custom_tab_bar()) {
    panes->push_back(toolbar_->custom_tab_bar());
  }
  if (bookmark_bar_view_.get()) {
    panes->push_back(bookmark_bar_view_.get());
  }
  if (infobar_container_) {
    panes->push_back(infobar_container_);
  }
  if (download_shelf_) {
    panes->push_back(download_shelf_->GetView());
  }
  if (unified_side_panel_) {
    panes->push_back(unified_side_panel_);
  }
  // TODO(crbug.com/40119836): Implement for mac.
  if (multi_contents_view_) {
    panes->push_back(multi_contents_view_);
  } else {
    panes->push_back(contents_web_view_);
  }
  if (devtools_web_view_->GetVisible()) {
    panes->push_back(devtools_web_view_);
  }
  if (devtools_scrim_view_->GetVisible()) {
    panes->push_back(devtools_scrim_view_);
  }
}

bool BrowserView::ShouldDescendIntoChildForEventHandling(
    gfx::NativeView child,
    const gfx::Point& location) {
  // Window for PWAs with window-controls-overlay display override should claim
  // mouse events that fall within the draggable region.
  web_app::AppBrowserController* controller = browser()->app_controller();
  if (AreDraggableRegionsEnabled() && controller &&
      controller->draggable_region().has_value()) {
    // Draggable regions are defined relative to the web contents.
    gfx::Point point_in_contents_web_view_coords(location);
    views::View::ConvertPointToTarget(GetWidget()->GetRootView(),
                                      contents_web_view_,
                                      &point_in_contents_web_view_coords);

    // Draggable regions should be ignored for clicks into any browser view's
    // owned widgets, for example alerts, permission prompts or find bar.
    return !controller->draggable_region()->contains(
               point_in_contents_web_view_coords.x(),
               point_in_contents_web_view_coords.y()) ||
           WidgetOwnedByAnchorContainsPoint(point_in_contents_web_view_coords);
  }

  return true;
}

bool BrowserView::RotatePaneFocusFromView(views::View* focused_view,
                                          bool forward,
                                          bool enable_wrapping) {
  // If an inactive bubble is showing this intentionally focuses that dialog to
  // provide an easy access method to these dialogs without requiring additional
  // keyboard shortcuts or commands. To get back out to pane cycling the dialog
  // needs to be accepted or dismissed.
  if (ActivateFirstInactiveBubbleForAccessibility()) {
    // We only want to signal that we have performed a rotation once for an
    // accessibility bubble. This is important for ChromeOS because the result
    // of this operation is used to determine whether or not we should rotate
    // focus out of the browser.
    // |enable_wrapping| is overloaded with the start of a rotation. Therefore,
    // we can use it to ensure that we only return that we have rotated once to
    // the caller.
    // TODO(crbug.com/40274273): the overloaded |enable_wrapping| is not
    // intuitive and confusing. Refactor this so that start of rotation is more
    // clear and not mangled up with wrapping.
    return enable_wrapping;
  }

  return views::WidgetDelegate::RotatePaneFocusFromView(focused_view, forward,
                                                        enable_wrapping);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, views::ClientView overrides:

views::CloseRequestResult BrowserView::OnWindowCloseRequested() {
  // You cannot close a frame for which there is an active originating drag
  // session.
  if (tabstrip_ && !tabstrip_->IsTabStripCloseable()) {
    return views::CloseRequestResult::kCannotClose;
  }

  // Give beforeunload handlers, the user, or policy the chance to cancel the
  // close before we hide the window below.
  if (const auto closing_status = browser_->HandleBeforeClose();
      closing_status != BrowserClosingStatus::kPermitted) {
    BrowserList::NotifyBrowserCloseCancelled(browser_.get(), closing_status);
    return views::CloseRequestResult::kCannotClose;
  }

  views::CloseRequestResult result = views::CloseRequestResult::kCanClose;
  if (!browser_->tab_strip_model()->empty()) {
    // Tab strip isn't empty.  Hide the frame (so it appears to have closed
    // immediately) and close all the tabs, allowing the renderers to shut
    // down. When the tab strip is empty we'll be called back again.
    frame_->Hide();
    result = views::CloseRequestResult::kCannotClose;
  }

  browser_->OnWindowClosing();
  return result;
}

int BrowserView::NonClientHitTest(const gfx::Point& point) {
#if BUILDFLAG(IS_MAC)
  // The top container while in immersive fullscreen on macOS lives in another
  // Widget (OverlayWidget). This means that BrowserView does not need to
  // consult BrowserViewLayout::NonClientHitTest() to calculate the hit test.
  if (IsImmersiveModeEnabled()) {
    // Handle hits on the overlay widget when it is hovering overtop of the
    // content view.
    gfx::Point screen_point(point);
    View::ConvertPointToScreen(this, &screen_point);
    if (overlay_widget()->GetWindowBoundsInScreen().Contains(screen_point)) {
      return HTNOWHERE;
    }
    return views::ClientView::NonClientHitTest(point);
  }
#endif  // BUILDFLAG(IS_MAC)

  // Since the TabStrip only renders in some parts of the top of the window,
  // the un-obscured area is considered to be part of the non-client caption
  // area of the window. So we need to treat hit-tests in these regions as
  // hit-tests of the titlebar.
  gfx::Point point_in_browser_view_coords(point);
  views::View::ConvertPointToTarget(parent(), this,
                                    &point_in_browser_view_coords);

  // Check if the point is in the web_app_frame_toolbar_. Because this toolbar
  // can entirely be within the window controls overlay area, this check needs
  // to be done before the window controls overlay area check below.
  if (web_app_frame_toolbar_) {
    int web_app_component =
        views::GetHitTestComponent(web_app_frame_toolbar_, point);
    if (web_app_component != HTNOWHERE) {
      return web_app_component;
    }
  }

  // Let the frame handle any events that fall within the bounds of the window
  // controls overlay.
  if (IsWindowControlsOverlayEnabled() && GetActiveWebContents()) {
    // The window controls overlays are to the left and/or right of the
    // |titlebar_area_rect|.
    gfx::Rect titlebar_area_rect =
        GetActiveWebContents()->GetWindowsControlsOverlayRect();

    // The top area rect is the same height as the |titlebar_area_rect| but
    // fills the full width of the browser view.
    gfx::Rect top_area_rect(0, titlebar_area_rect.y(), width(),
                            titlebar_area_rect.height());

    // If the point is within the top_area_rect but not the titlebar_area_rect,
    // then it must be in the window controls overlay.
    if (top_area_rect.Contains(point_in_browser_view_coords) &&
        !titlebar_area_rect.Contains(point_in_browser_view_coords)) {
      return HTNOWHERE;
    }
  }

  // Determine if the TabStrip exists and is capable of being clicked on. We
  // might be a popup window without a TabStrip.
  if (ShouldDrawTabStrip()) {
    // See if the mouse pointer is within the bounds of the TabStripRegionView.
    gfx::Point test_point(point);
    if (ConvertedHitTest(parent(), tab_strip_region_view_, &test_point)) {
      if (tab_strip_region_view_->IsPositionInWindowCaption(test_point)) {
        return HTCAPTION;
      }
      return HTCLIENT;
    }

    // The top few pixels of the TabStrip are a drop-shadow - as we're pretty
    // starved of draggable area, let's give it to window dragging (this also
    // makes sense visually).
    // TODO(tluk): Investigate the impact removing this has on draggable area
    // given the tab strip no longer uses shadows.
    views::Widget* widget = GetWidget();
    if (!(widget->IsMaximized() || widget->IsFullscreen()) &&
        (point_in_browser_view_coords.y() <
         (tab_strip_region_view_->y() + kTabShadowSize))) {
      // We return HTNOWHERE as this is a signal to our containing
      // NonClientView that it should figure out what the correct hit-test
      // code is given the mouse position...
      return HTNOWHERE;
    }
  }

  // For PWAs with window-controls-overlay or borderless display override, see
  // if we're in an app defined draggable region so we can return htcaption.
  web_app::AppBrowserController* controller = browser()->app_controller();

  if (AreDraggableRegionsEnabled() && controller &&
      controller->draggable_region().has_value()) {
    // Draggable regions are defined relative to the web contents.
    gfx::Point point_in_contents_web_view_coords(point_in_browser_view_coords);
    views::View::ConvertPointToTarget(this, contents_web_view(),
                                      &point_in_contents_web_view_coords);

    if (controller->draggable_region()->contains(
            point_in_contents_web_view_coords.x(),
            point_in_contents_web_view_coords.y())) {
      // Draggable regions should be ignored for clicks into any browser view's
      // owned widgets, for example alerts, permission prompts or find bar.
      return WidgetOwnedByAnchorContainsPoint(point_in_browser_view_coords)
                 ? HTCLIENT
                 : HTCAPTION;
    }
  }

  // If the point's y coordinate is below the top of the topmost view and
  // otherwise within the bounds of this view, the point is considered to be
  // within the client area.
  gfx::Rect bounds_from_toolbar_top = bounds();
  bounds_from_toolbar_top.Inset(gfx::Insets::TLBR(GetClientAreaTop(), 0, 0, 0));
  if (bounds_from_toolbar_top.Contains(point)) {
    return HTCLIENT;
  }

  // If the point's y coordinate is above the top of the toolbar, but not
  // over the tabstrip (per previous checking in this function), then we
  // consider it in the window caption (e.g. the area to the right of the
  // tabstrip underneath the window controls). However, note that we DO NOT
  // return HTCAPTION here, because when the window is maximized the window
  // controls will fall into this space (since the BrowserView is sized to
  // entire size of the window at that point), and the HTCAPTION value will
  // cause the window controls not to work. So we return HTNOWHERE so that the
  // caller will hit-test the window controls before finally falling back to
  // HTCAPTION.
  gfx::Rect tabstrip_background_bounds = bounds();
  gfx::Point toolbar_origin = toolbar_->origin();
  views::View::ConvertPointToTarget(top_container_, this, &toolbar_origin);
  tabstrip_background_bounds.set_height(toolbar_origin.y());
  if (tabstrip_background_bounds.Contains(point)) {
    return HTNOWHERE;
  }

  // If the point is somewhere else, delegate to the default implementation.
  return views::ClientView::NonClientHitTest(point);
}

gfx::Size BrowserView::GetMinimumSize() const {
  return GetBrowserViewLayout()->GetMinimumSize(this);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, views::View overrides:

void BrowserView::Layout(PassKey) {
  TRACE_EVENT0("ui", "BrowserView::Layout");
  if (!initialized_ || in_process_fullscreen_) {
    return;
  }

  // Allow only a single layout operation once top controls sliding begins.
  if (top_controls_slide_controller_ &&
      top_controls_slide_controller_->IsEnabled() &&
      top_controls_slide_controller_->IsTopControlsSlidingInProgress()) {
    if (did_first_layout_while_top_controls_are_sliding_) {
      return;
    }
    did_first_layout_while_top_controls_are_sliding_ = true;
  } else {
    did_first_layout_while_top_controls_are_sliding_ = false;
  }

  LayoutSuperclass<views::View>(this);

  // TODO(jamescook): Why was this in the middle of layout code?
  toolbar_->location_bar()->omnibox_view()->SetFocusBehavior(
      IsToolbarVisible() ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
  frame()->GetFrameView()->UpdateMinimumSize();

  // Some of the situations when the BrowserView is laid out are:
  // - Enter/exit immersive fullscreen mode.
  // - Enter/exit tablet mode.
  // - At the beginning/end of the top controls slide behavior in tablet mode.
  // The above may result in a change in the location bar's position, to which a
  // permission bubble may be anchored. For that we must update its anchor
  // position.
  WebContents* contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (contents &&
      permissions::PermissionRequestManager::FromWebContents(contents)) {
    permissions::PermissionRequestManager::FromWebContents(contents)
        ->UpdateAnchor();
  }

  if (feature_promo_controller_) {
    feature_promo_controller_->bubble_factory_registry()
        ->NotifyAnchorBoundsChanged(GetElementContext());
  }
}

void BrowserView::OnGestureEvent(ui::GestureEvent* event) {
  int command;
  if (GetGestureCommand(event, &command) &&
      chrome::IsCommandEnabled(browser(), command)) {
    chrome::ExecuteCommandWithDisposition(
        browser(), command, ui::DispositionFromEventFlags(event->flags()));
    return;
  }

  ClientView::OnGestureEvent(event);
}

void BrowserView::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  // Override here in order to suppress the call to
  // views::ClientView::ViewHierarchyChanged();
}

void BrowserView::AddedToWidget() {
  // BrowserView may be added to a widget more than once if the user changes
  // themes after starting the browser. Do not re-initialize BrowserView in
  // this case.
  if (initialized_) {
    return;
  }

  views::ClientView::AddedToWidget();

  widget_observation_.Observe(GetWidget());

  // Stow a pointer to this object onto the window handle so that we can get at
  // it later when all we have is a native view.
  GetWidget()->SetNativeWindowProperty(kBrowserViewKey, this);

  // Stow a pointer to the browser's profile onto the window handle so that we
  // can get it later when all we have is a native view.
  GetWidget()->SetNativeWindowProperty(Profile::kProfileKey,
                                       browser_->profile());

#if defined(USE_AURA)
  // Stow a pointer to the browser's profile onto the window handle so
  // that windows will be styled with the appropriate NativeTheme.
  SetThemeProfileForWindow(GetNativeWindow(), browser_->profile());
#endif

  toolbar_->Init();

  if (GetIsNormalType()) {
    if (features::HasTabSearchToolbarButton()) {
      tab_search_bubble_host_ = std::make_unique<TabSearchBubbleHost>(
          toolbar_->tab_search_button(), browser_.get(),
          tabstrip_->AsWeakPtr());
    } else {
      tab_search_bubble_host_ = std::make_unique<TabSearchBubbleHost>(
          tab_strip_region_view_->GetTabSearchButton(), browser_.get(),
          tabstrip_->AsWeakPtr());
    }
  }

  // TODO(pbos): Investigate whether the side panels should be creatable when
  // the ToolbarView does not create a button for them. This specifically seems
  // to hit web apps. See https://crbug.com/1267781.
  auto* side_panel_coordinator =
      browser_->GetFeatures().side_panel_coordinator();
  unified_side_panel_->AddObserver(side_panel_coordinator);

#if BUILDFLAG(IS_CHROMEOS)
  // TopControlsSlideController must be initialized here in AddedToWidget()
  // rather than Init() as it depends on the browser frame being ready.
  // It also needs to be after the |toolbar_| had been initialized since it uses
  // the omnibox.
  if (GetIsNormalType()) {
    DCHECK(frame_);
    DCHECK(toolbar_);
    top_controls_slide_controller_ =
        std::make_unique<TopControlsSlideControllerChromeOS>(this);
  }
#endif

  LoadAccelerators();

  // |immersive_mode_controller_| may depend on the presence of a Widget, so it
  // is initialized here.
  immersive_mode_controller_->Init(this);
  immersive_mode_controller_->AddObserver(this);

  // TODO(crbug.com/40664862): Remove BrowserViewLayout dependence on
  // Widget and move to the constructor.
  BrowserViewLayout* browser_view_layout =
      SetLayoutManager(std::make_unique<BrowserViewLayout>(
          std::make_unique<BrowserViewLayoutDelegateImpl>(this), this,
          window_scrim_view_, top_container_, web_app_frame_toolbar_,
          web_app_window_title_, tab_strip_region_view_, tabstrip_, toolbar_,
          infobar_container_, contents_container_, multi_contents_view_,
          left_aligned_side_panel_separator_, unified_side_panel_,
          right_aligned_side_panel_separator_, side_panel_rounded_corner_,
          immersive_mode_controller_.get(), contents_separator_));
  browser_view_layout->SetUseBrowserContentMinimumSize(
      ShouldUseBrowserContentMinimumSize());

  EnsureFocusOrder();

  // This browser view may already have a custom button provider set (e.g the
  // hosted app frame).
  if (!toolbar_button_provider_) {
    SetToolbarButtonProvider(toolbar_);
  }

  if (download::IsDownloadBubbleEnabled()) {
    browser_->GetFeatures().download_toolbar_ui_controller()->Init();
  }

  frame_->OnBrowserViewInitViewsComplete();
  frame_->GetFrameView()->UpdateMinimumSize();
  using_native_frame_ = frame_->ShouldUseNativeFrame();

  MaybeInitializeWebUITabStrip();
  MaybeShowTabStripToolbarButtonIPH();

  // Want to show this promo, but not right at startup.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BrowserView::MaybeShowReadingListInSidePanelIPH,
                     GetAsWeakPtr()),
      base::Minutes(5));

  // Accessible name of the tab is dependent on the visibility state of the chip
  // view, so it needs to be made aware of any changes.
  if (toolbar_ && toolbar_->location_bar() &&
      toolbar_->location_bar()->GetChipController()) {
    chip_visibility_subscription_ =
        toolbar_->location_bar()
            ->GetChipController()
            ->chip()
            ->AddVisibleChangedCallback(base::BindRepeating(
                &BrowserView::UpdateAccessibleNameForAllTabs,
                weak_ptr_factory_.GetWeakPtr()));
  }

  initialized_ = true;
}

void BrowserView::RemovedFromWidget() {
  CHECK(GetFocusManager());
  focus_manager_observation_.Reset();
}

void BrowserView::PaintChildren(const views::PaintInfo& paint_info) {
  views::ClientView::PaintChildren(paint_info);
  static bool did_first_paint = false;
  if (!did_first_paint) {
    did_first_paint = true;
    startup_metric_utils::GetBrowser().RecordBrowserWindowFirstPaint(
        base::TimeTicks::Now());
  }
}

void BrowserView::OnThemeChanged() {
  views::ClientView::OnThemeChanged();
  if (!initialized_) {
    return;
  }

  FrameColorsChanged();
}

bool BrowserView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  const bool parent_result =
      views::ClientView::GetDropFormats(formats, format_types);
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (webui_tab_strip_) {
    WebUITabStripContainerView::GetDropFormatsForView(formats, format_types);
    return true;
  } else {
    return parent_result;
  }
#else
  return parent_result;
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
}

bool BrowserView::AreDropTypesRequired() {
  return true;
}

bool BrowserView::CanDrop(const ui::OSExchangeData& data) {
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (!webui_tab_strip_) {
    return false;
  }
  return WebUITabStripContainerView::IsDraggedTab(data);
#else
  return false;
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
}

void BrowserView::OnDragEntered(const ui::DropTargetEvent& event) {
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  if (!webui_tab_strip_) {
    return;
  }
  if (WebUITabStripContainerView::IsDraggedTab(event.data())) {
    webui_tab_strip_->OpenForTabDrag();
  }
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ui::AcceleratorTarget overrides:

bool BrowserView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  int command_id;
  // Though AcceleratorManager should not send unknown |accelerator| to us, it's
  // still possible the command cannot be executed now.
  if (!FindCommandIdForAccelerator(accelerator, &command_id)) {
    return false;
  }

  UpdateAcceleratorMetrics(accelerator, command_id);
  return chrome::ExecuteCommand(browser_.get(), command_id,
                                accelerator.time_stamp());
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, infobars::InfoBarContainer::Delegate overrides:

void BrowserView::InfoBarContainerStateChanged(bool is_animating) {
  ToolbarSizeChanged(is_animating);
}

void BrowserView::MaybeInitializeWebUITabStrip() {
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  TRACE_EVENT0("ui", "BrowserView::MaybeInitializeWebUITabStrip");
  if (browser_->CanSupportWindowFeature(Browser::FEATURE_TABSTRIP) &&
      WebUITabStripContainerView::UseTouchableTabStrip(browser_.get())) {
    if (!webui_tab_strip_) {
      // We use |contents_container_| here so that enabling or disabling
      // devtools won't affect the tab sizes. We still use only
      // |contents_web_view_| for screenshotting and will adjust the
      // screenshot accordingly. Ideally, the thumbnails should be sized
      // based on a typical tab size, ignoring devtools or e.g. the
      // downloads bar.
      webui_tab_strip_ = top_container_->AddChildView(
          std::make_unique<WebUITabStripContainerView>(
              this, contents_container_, top_container_,
              GetLocationBarView()->omnibox_view()));
      loading_bar_ = top_container_->AddChildView(
          std::make_unique<TopContainerLoadingBar>(browser_.get()));
      loading_bar_->SetWebContents(GetActiveWebContents());
    }
  } else if (webui_tab_strip_) {
    GetBrowserViewLayout()->set_webui_tab_strip(nullptr);
    top_container_->RemoveChildView(webui_tab_strip_);
    webui_tab_strip_.ClearAndDelete();

    GetBrowserViewLayout()->set_loading_bar(nullptr);
    top_container_->RemoveChildView(loading_bar_);
    loading_bar_.ClearAndDelete();
  }
  GetBrowserViewLayout()->set_webui_tab_strip(webui_tab_strip_);
  GetBrowserViewLayout()->set_loading_bar(loading_bar_);
  if (toolbar_) {
    toolbar_->UpdateForWebUITabStrip();
  }
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
}

void BrowserView::LoadingAnimationTimerCallback() {
  LoadingAnimationCallback(base::TimeTicks::Now());
}

void BrowserView::LoadingAnimationCallback(base::TimeTicks timestamp) {
  if (GetSupportsTabStrip()) {
    // Loading animations are shown in the tab for tabbed windows. Update them
    // even if the tabstrip isn't currently visible so they're in the right
    // state when it returns.
    tabstrip_->UpdateLoadingAnimations(timestamp - loading_animation_start_);
  }

  if (ShouldShowWindowIcon()) {
    WebContents* web_contents =
        browser_->tab_strip_model()->GetActiveWebContents();
    // GetActiveWebContents can return null for example under Purify when
    // the animations are running slowly and this function is called on a timer
    // through LoadingAnimationCallback.
    frame_->UpdateThrobber(web_contents && web_contents->IsLoading());
  }
}

#if BUILDFLAG(IS_WIN)
void BrowserView::CreateJumpList() {
  // Ensure that this browser's Profile has a JumpList so that the JumpList is
  // kept up to date.
  JumpListFactory::GetForProfile(browser_->profile());
}
#endif

bool BrowserView::ShouldShowAvatarToolbarIPH() {
  if (GetGuestSession() || GetIncognito()) {
    return false;
  }
  AvatarToolbarButton* avatar_button =
      toolbar_button_provider_
          ? toolbar_button_provider_->GetAvatarToolbarButton()
          : nullptr;
  return avatar_button != nullptr;
}

BrowserViewLayout* BrowserView::GetBrowserViewLayout() const {
  return static_cast<BrowserViewLayout*>(GetLayoutManager());
}

ContentsLayoutManager* BrowserView::GetContentsLayoutManager() const {
  return static_cast<ContentsLayoutManager*>(
      contents_container_->GetLayoutManager());
}

bool BrowserView::MaybeShowBookmarkBar(WebContents* contents) {
  const bool show_bookmark_bar =
      contents && browser_->SupportsWindowFeature(Browser::FEATURE_BOOKMARKBAR);
  if (!show_bookmark_bar && !bookmark_bar_view_.get()) {
    return false;
  }
  if (!bookmark_bar_view_.get()) {
    bookmark_bar_view_ =
        std::make_unique<BookmarkBarView>(browser_.get(), this);
    bookmark_bar_view_->set_owned_by_client(OwnedByClientPassKey());
    bookmark_bar_view_->SetBookmarkBarState(
        browser_->bookmark_bar_state(), BookmarkBar::DONT_ANIMATE_STATE_CHANGE);
    GetBrowserViewLayout()->set_bookmark_bar(bookmark_bar_view_.get());
  }
  // Don't change the visibility of the BookmarkBarView. BrowserViewLayout
  // handles it.
  bookmark_bar_view_->SetPageNavigator(GetActiveWebContents());

  // Update parenting for the bookmark bar. This may detach it from all views.
  bool needs_layout = false;
  views::View* new_parent = nullptr;
  if (show_bookmark_bar) {
    new_parent = top_container_;
  }
  if (new_parent != bookmark_bar_view_->parent()) {
    if (new_parent == top_container_) {
      // BookmarkBarView is attached.
      new_parent->AddChildViewRaw(bookmark_bar_view_.get());
    } else {
      DCHECK(!new_parent);
      // Bookmark bar is being detached from all views because it is hidden.
      bookmark_bar_view_->parent()->RemoveChildView(bookmark_bar_view_.get());
    }
    needs_layout = true;
  }

  // Check for updates to the desired size.
  if (bookmark_bar_view_->GetPreferredSize().height() !=
      bookmark_bar_view_->height()) {
    needs_layout = true;
  }

  return needs_layout;
}

bool BrowserView::MaybeShowInfoBar(WebContents* contents) {
  // TODO(beng): Remove this function once the interface between
  //             InfoBarContainer, DownloadShelfView and WebContents and this
  //             view is sorted out.
  return true;
}

void BrowserView::UpdateDevToolsForContents(WebContents* web_contents,
                                            bool update_devtools_web_contents) {
  DevToolsContentsResizingStrategy strategy;
  WebContents* devtools =
      DevToolsWindow::GetInTabWebContents(web_contents, &strategy);

  if (!devtools_web_view_->web_contents() && devtools &&
      !devtools_focus_tracker_.get()) {
    // Install devtools focus tracker when dev tools window is shown for the
    // first time.
    devtools_focus_tracker_ = std::make_unique<views::ExternalFocusTracker>(
        devtools_web_view_, GetFocusManager());
  }

  // Restore focus to the last focused view when hiding devtools window.
  if (devtools_web_view_->web_contents() && !devtools &&
      devtools_focus_tracker_.get()) {
    devtools_focus_tracker_->FocusLastFocusedExternalView();
    devtools_focus_tracker_.reset();
  }

  // Replace devtools WebContents.
  if (devtools_web_view_->web_contents() != devtools &&
      update_devtools_web_contents) {
    devtools_web_view_->SetWebContents(devtools);
  }

  if (devtools) {
    devtools_web_view_->SetVisible(true);
    GetContentsLayoutManager()->SetContentsResizingStrategy(strategy);
  } else {
    devtools_web_view_->SetVisible(false);
    GetContentsLayoutManager()->SetContentsResizingStrategy(
        DevToolsContentsResizingStrategy());
  }
  contents_container_->DeprecatedLayoutImmediately();

  if (devtools) {
    // When strategy.hide_inspected_contents() returns true, we are hiding the
    // WebContents behind the devtools_web_view_. Otherwise, the WebContents
    // should be right above the devtools_web_view_.
    views::View* contents_view;
    if (multi_contents_view_) {
      contents_view = multi_contents_view_;
    } else {
      contents_view = contents_web_view_;
    }
    size_t devtools_index =
        contents_container_->GetIndexOf(devtools_web_view_).value();
    size_t contents_index =
        contents_container_->GetIndexOf(contents_view).value();
    bool devtools_is_on_top = devtools_index > contents_index;
    if (strategy.hide_inspected_contents() != devtools_is_on_top) {
      contents_container_->ReorderChildView(contents_view, devtools_index);
    }
  }

  DevToolsDockedPlacement new_placement = GetDevToolsDockedPlacement(
      multi_contents_view_ ? multi_contents_view_->bounds()
                           : contents_web_view_->bounds(),
      contents_container_->GetLocalBounds());

  // When browser window is resizing, the contents_container and web_contents
  // bounds can be out of sync, resulting in a state, where it is impossible to
  // infer docked placement based on contents webview bounds. In this case, use
  // the last known docked placement, since resizing a window does not change
  // the devtools dock placement.
  if (new_placement != DevToolsDockedPlacement::kUnknown) {
    current_devtools_docked_placement_ = new_placement;
  }
}

void BrowserView::UpdateUIForContents(WebContents* contents) {
  TRACE_EVENT0("ui", "BrowserView::UpdateUIForContents");
  bool needs_layout = MaybeShowBookmarkBar(contents);

  // TODO(jamescook): This function always returns true. Remove it and figure
  // out when layout is actually required.
  needs_layout |= MaybeShowInfoBar(contents);

  if (multi_contents_view_) {
    bool current_state = multi_contents_view_->IsInSplitView();
    bool updated_state =
        contents && tabs::TabInterface::GetFromContents(contents)->IsSplit();
    needs_layout |= (current_state != updated_state);
  }

  if (needs_layout) {
    DeprecatedLayoutImmediately();
  }
}

int BrowserView::GetClientAreaTop() {
  views::View* top_view = toolbar_;
#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
  // If webui_tab_strip is displayed, the client area starts at its top,
  // otherwise at the top of the toolbar.
  if (webui_tab_strip_ && webui_tab_strip_->GetVisible()) {
    top_view = webui_tab_strip_;
  }
#endif
  return top_view->y();
}

void BrowserView::PrepareFullscreen(bool fullscreen) {
  if (top_controls_slide_controller_) {
    top_controls_slide_controller_->OnBrowserFullscreenStateWillChange(
        fullscreen);
  }

  // Reduce jankiness during the following position changes by:
  //   * Hiding the window until it's in the final position
  //   * Ignoring all intervening layout attempts, which would resize the
  //     webpage and thus are slow and look ugly (enforced via
  //     |in_process_fullscreen_|).
  if (fullscreen) {
    // Move focus out of the location bar if necessary.
    views::FocusManager* focus_manager = GetFocusManager();
    DCHECK(focus_manager);
    // Look for focus in the location bar itself or any child view.
    if (GetLocationBarView()->Contains(focus_manager->GetFocusedView())) {
      focus_manager->ClearFocus();
    }

    fullscreen_control_host_ = std::make_unique<FullscreenControlHost>(this);
  } else {
    // Hide the fullscreen bubble as soon as possible, since the mode toggle can
    // take enough time for the user to notice.
    exclusive_access_bubble_.reset();

    if (fullscreen_control_host_) {
      fullscreen_control_host_->Hide(false);
      fullscreen_control_host_.reset();
    }
  }
}

void BrowserView::ProcessFullscreen(bool fullscreen, const int64_t display_id) {
  CHECK(!base::FeatureList::IsEnabled(features::kAsyncFullscreenWindowState));

  if (in_process_fullscreen_) {
    return;
  }
  in_process_fullscreen_ = true;

  PrepareFullscreen(fullscreen);

  // TODO(b/40276379): Move this out from ProcessFullscreen.
  RequestFullscreen(fullscreen, display_id);

#if !BUILDFLAG(IS_MAC)
  // On Mac platforms, FullscreenStateChanged() is invoked from
  // BrowserFrameMac::OnWindowFullscreenTransitionComplete when the asynchronous
  // fullscreen transition is complete.
  // On other platforms, there is no asynchronous transition so we synchronously
  // invoke the function.
  FullscreenStateChanged();
#endif

  // Undo our anti-jankiness hacks and force a re-layout.
  in_process_fullscreen_ = false;
  ToolbarSizeChanged(false);
  frame_->GetFrameView()->OnFullscreenStateChanged();
}

void BrowserView::RequestFullscreen(bool fullscreen, int64_t display_id) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  // Request target display fullscreen from lower layers on supported platforms.
  frame_->SetFullscreen(fullscreen, display_id);
#else   // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  // TODO(crbug.com/40111909): Reimplement this at lower layers on all
  // platforms.
  if (fullscreen && display_id != display::kInvalidDisplayId) {
    display::Screen* screen = display::Screen::GetScreen();
    display::Display display;
    display::Display current_display =
        screen->GetDisplayNearestWindow(GetNativeWindow());
    if (screen && screen->GetDisplayWithDisplayId(display_id, &display) &&
        current_display.id() != display_id) {
      // Fullscreen windows must exit fullscreen to move to another display.
      if (IsFullscreen()) {
        frame_->SetFullscreen(false);

        // Activate the window to give it input focus and bring it to the front
        // of the z-order. This prevents an inactive fullscreen window from
        // occluding the active window receiving key events on Linux, and also
        // prevents an inactive fullscreen window and its exit bubble from being
        // occluded by the active window on Chrome OS.
        Activate();
      }

      const bool was_maximized = IsMaximized();
      if (restore_pre_fullscreen_bounds_callback_.is_null()) {
        // Use GetBounds(), rather than GetRestoredBounds(), when the window is
        // not maximized, to restore snapped window bounds on fullscreen exit.
        // TODO(crbug.com/40111909): Support lower-layer fullscreen-on-display.
        const gfx::Rect bounds_to_restore =
            was_maximized ? GetRestoredBounds() : GetBounds();
        restore_pre_fullscreen_bounds_callback_ = base::BindOnce(
            [](base::WeakPtr<BrowserView> view, const gfx::Rect& bounds,
               bool maximize) {
              if (view && view->frame()) {
                // Adjust restored bounds to be on-screen, in case the original
                // screen was disconnected or repositioned during fullscreen.
                view->frame()->SetBoundsConstrained(bounds);
                if (maximize) {
                  view->Maximize();
                }
              }
            },
            weak_ptr_factory_.GetWeakPtr(), bounds_to_restore, was_maximized);
      }

      // Restore the window as needed, so it can be moved to the target display.
      // TODO(crbug.com/40111909): Support lower-layer fullscreen-on-display.
      if (was_maximized) {
        Restore();
      }
      SetBounds({display.work_area().origin(),
                 frame_->GetWindowBoundsInScreen().size()});
    }
  }
  frame_->SetFullscreen(fullscreen);
  if (!fullscreen && restore_pre_fullscreen_bounds_callback_) {
    std::move(restore_pre_fullscreen_bounds_callback_).Run();
  }
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
}

void BrowserView::LoadAccelerators() {
  views::FocusManager* focus_manager = GetFocusManager();
  DCHECK(focus_manager);

  // Let's fill our own accelerator table.
  const bool is_app_mode = IsRunningInForcedAppMode();
#if BUILDFLAG(IS_CHROMEOS)
  const bool is_captive_portal_signin_window =
      browser_->profile()->IsOffTheRecord() &&
      browser_->profile()->GetOTRProfileID().IsCaptivePortal();
#endif
  const std::vector<AcceleratorMapping> accelerator_list(GetAcceleratorList());
  for (const auto& entry : accelerator_list) {
    // In app mode, only allow accelerators of allowlisted commands to pass
    // through.
    if (is_app_mode && !IsCommandAllowedInAppMode(entry.command_id,
                                                  browser()->is_type_popup())) {
      continue;
    }

#if BUILDFLAG(IS_CHROMEOS)
    if (is_captive_portal_signin_window) {
      int command = entry.command_id;
      // Captive portal signin uses an OTR profile without history.
      if (command == IDC_SHOW_HISTORY) {
        continue;
      }
      // The NewTab command expects navigation to occur in the same browser
      // window. For captive portal signin this is not the case, so hide these
      // to reduce confusion.
      if (command == IDC_NEW_TAB || command == IDC_NEW_TAB_TO_RIGHT ||
          command == IDC_CREATE_NEW_TAB_GROUP) {
        continue;
      }
    }
#endif

    ui::Accelerator accelerator(entry.keycode, entry.modifiers);
    accelerator_table_[accelerator] = entry.command_id;

    // Also register with the focus manager.
    focus_manager->RegisterAccelerator(
        accelerator, ui::AcceleratorManager::kNormalPriority, this);
  }
}

int BrowserView::GetCommandIDForAppCommandID(int app_command_id) const {
#if BUILDFLAG(IS_WIN)
  switch (app_command_id) {
    // NOTE: The order here matches the APPCOMMAND declaration order in the
    // Windows headers.
    case APPCOMMAND_BROWSER_BACKWARD:
      return IDC_BACK;
    case APPCOMMAND_BROWSER_FORWARD:
      return IDC_FORWARD;
    case APPCOMMAND_BROWSER_REFRESH:
      return IDC_RELOAD;
    case APPCOMMAND_BROWSER_HOME:
      return IDC_HOME;
    case APPCOMMAND_BROWSER_STOP:
      return IDC_STOP;
    case APPCOMMAND_BROWSER_SEARCH:
      return IDC_FOCUS_SEARCH;
    case APPCOMMAND_HELP:
      return IDC_HELP_PAGE_VIA_KEYBOARD;
    case APPCOMMAND_NEW:
      return IDC_NEW_TAB;
    case APPCOMMAND_OPEN:
      return IDC_OPEN_FILE;
    case APPCOMMAND_CLOSE:
      return IDC_CLOSE_TAB;
    case APPCOMMAND_SAVE:
      return IDC_SAVE_PAGE;
    case APPCOMMAND_PRINT:
      return IDC_PRINT;
    case APPCOMMAND_COPY:
      return IDC_COPY;
    case APPCOMMAND_CUT:
      return IDC_CUT;
    case APPCOMMAND_PASTE:
      return IDC_PASTE;

      // TODO(pkasting): http://b/1113069 Handle these.
    case APPCOMMAND_UNDO:
    case APPCOMMAND_REDO:
    case APPCOMMAND_SPELL_CHECK:
    default:
      return -1;
  }
#else
  // App commands are Windows-specific so there's nothing to do here.
  return -1;
#endif
}

void BrowserView::UpdateAcceleratorMetrics(const ui::Accelerator& accelerator,
                                           int command_id) {
  const ui::KeyboardCode key_code = accelerator.key_code();
  if (command_id == IDC_HELP_PAGE_VIA_KEYBOARD && key_code == ui::VKEY_F1) {
    base::RecordAction(UserMetricsAction("ShowHelpTabViaF1"));
  }

  if (command_id == IDC_BOOKMARK_THIS_TAB) {
    UMA_HISTOGRAM_ENUMERATION("Bookmarks.EntryPoint",
                              BookmarkEntryPoint::kAccelerator);
  }
  if (command_id == IDC_NEW_TAB &&
      browser_->SupportsWindowFeature(Browser::FEATURE_TABSTRIP)) {
    TabStripModel* const model = browser_->tab_strip_model();
    const auto group_id = model->GetTabGroupForTab(model->active_index());
    if (group_id.has_value()) {
      base::RecordAction(base::UserMetricsAction("Accel_NewTabInGroup"));
    }
  }

  if (command_id == IDC_NEW_INCOGNITO_WINDOW) {
    base::RecordAction(base::UserMetricsAction("Accel_NewIncognitoWindow"));
  }

  if (command_id == IDC_FULLSCREEN) {
    if (browser_->window()->IsFullscreen()) {
      base::RecordAction(base::UserMetricsAction("ExitFullscreen_Accelerator"));
    } else {
      base::RecordAction(
          base::UserMetricsAction("EnterFullscreen_Accelerator"));
    }
  }

#if BUILDFLAG(IS_CHROMEOS)
  // Collect information about the relative popularity of various accelerators
  // on Chrome OS.
  switch (command_id) {
    case IDC_BACK:
      if (key_code == ui::VKEY_BROWSER_BACK) {
        base::RecordAction(UserMetricsAction("Accel_Back_F1"));
      } else if (key_code == ui::VKEY_LEFT) {
        base::RecordAction(UserMetricsAction("Accel_Back_Left"));
      }
      break;
    case IDC_FORWARD:
      if (key_code == ui::VKEY_BROWSER_FORWARD) {
        base::RecordAction(UserMetricsAction("Accel_Forward_F2"));
      } else if (key_code == ui::VKEY_RIGHT) {
        base::RecordAction(UserMetricsAction("Accel_Forward_Right"));
      }
      break;
    case IDC_RELOAD:
    case IDC_RELOAD_BYPASSING_CACHE:
      if (key_code == ui::VKEY_R) {
        base::RecordAction(UserMetricsAction("Accel_Reload_R"));
      } else if (key_code == ui::VKEY_BROWSER_REFRESH) {
        base::RecordAction(UserMetricsAction("Accel_Reload_F3"));
      }
      break;
    case IDC_FOCUS_LOCATION:
      if (key_code == ui::VKEY_D) {
        base::RecordAction(UserMetricsAction("Accel_FocusLocation_D"));
      } else if (key_code == ui::VKEY_L) {
        base::RecordAction(UserMetricsAction("Accel_FocusLocation_L"));
      }
      break;
    case IDC_FOCUS_SEARCH:
      if (key_code == ui::VKEY_E) {
        base::RecordAction(UserMetricsAction("Accel_FocusSearch_E"));
      } else if (key_code == ui::VKEY_K) {
        base::RecordAction(UserMetricsAction("Accel_FocusSearch_K"));
      }
      break;
    default:
      // Do nothing.
      break;
  }
#endif
}

void BrowserView::ShowAvatarBubbleFromAvatarButton(bool is_source_accelerator) {
  // TODO(b/323362927): rename the function and equivalent shortcut ID name to
  // be more precise -- about being the same as button being pressed instead of
  // just showing the avatar bubble since the action can be modified within the
  // button itself, like dismissing some other bubbles.
  if (AvatarToolbarButton* avatar_button =
          toolbar_button_provider_
              ? toolbar_button_provider_->GetAvatarToolbarButton()
              : nullptr) {
    avatar_button->ButtonPressed(is_source_accelerator);
    return;
  }

  // Default behavior -- show the profile menu.
  browser()->GetFeatures().profile_menu_coordinator()->Show(
      is_source_accelerator);
}

void BrowserView::MaybeShowProfileSwitchIPH() {
  if (!ShouldShowAvatarToolbarIPH()) {
    return;
  }
  toolbar_button_provider_->GetAvatarToolbarButton()
      ->MaybeShowProfileSwitchIPH();
}

void BrowserView::MaybeShowSupervisedUserProfileSignInIPH() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  if (!ShouldShowAvatarToolbarIPH()) {
    return;
  }
  toolbar_button_provider_->GetAvatarToolbarButton()
      ->MaybeShowSupervisedUserSignInIPH();
#endif
}

void BrowserView::ShowHatsDialog(
    const std::string& site_id,
    const std::optional<std::string>& hats_histogram_name,
    const std::optional<uint64_t> hats_survey_ukm_id,
    base::OnceClosure success_callback,
    base::OnceClosure failure_callback,
    const SurveyBitsData& product_specific_bits_data,
    const SurveyStringData& product_specific_string_data) {
  // Self deleting on close.
  new HatsNextWebDialog(browser(), site_id, hats_histogram_name,
                        hats_survey_ukm_id, std::move(success_callback),
                        std::move(failure_callback), product_specific_bits_data,
                        product_specific_string_data);
}

void BrowserView::ShowIncognitoClearBrowsingDataDialog() {
  IncognitoClearBrowsingDataDialogCoordinator::GetOrCreateForBrowser(browser())
      ->Show(IncognitoClearBrowsingDataDialogInterface::Type::kDefaultBubble);
}

void BrowserView::ShowIncognitoHistoryDisclaimerDialog() {
  IncognitoClearBrowsingDataDialogCoordinator::GetOrCreateForBrowser(browser())
      ->Show(IncognitoClearBrowsingDataDialogInterface::Type::
                 kHistoryDisclaimerBubble);
}

bool BrowserView::IsTabModalPopupDeprecated() const {
  return browser_->IsTabModalPopupDeprecated();
}

void BrowserView::SetIsTabModalPopupDeprecated(
    bool is_tab_modal_popup_deprecated) {
  browser_->set_is_tab_modal_popup_deprecated(is_tab_modal_popup_deprecated);
}

void BrowserView::UpdateWebAppStatusIconsVisiblity() {
  if (web_app_frame_toolbar()) {
    web_app_frame_toolbar()->UpdateStatusIconsVisibility();
  }
}

ExclusiveAccessContext* BrowserView::GetExclusiveAccessContext() {
  return this;
}

std::string BrowserView::GetWorkspace() const {
  return frame_->GetWorkspace();
}

bool BrowserView::IsVisibleOnAllWorkspaces() const {
  return frame_->IsVisibleOnAllWorkspaces();
}

void BrowserView::ShowEmojiPanel() {
  GetWidget()->ShowEmojiPanel();
}

void BrowserView::ShowCaretBrowsingDialog() {
  CaretBrowsingDialogDelegate::Show(GetNativeWindow(),
                                    GetProfile()->GetPrefs());
}

std::unique_ptr<content::EyeDropper> BrowserView::OpenEyeDropper(
    content::RenderFrameHost* frame,
    content::EyeDropperListener* listener) {
  return ShowEyeDropper(frame, listener);
}

user_education::FeaturePromoControllerCommon*
BrowserView::GetFeaturePromoControllerImpl() {
  return feature_promo_controller_.get();
}

bool BrowserView::IsFeaturePromoQueued(const base::Feature& iph_feature) const {
  return feature_promo_controller_ &&
         feature_promo_controller_->GetPromoStatus(iph_feature) ==
             user_education::FeaturePromoStatus::kQueued;
}

bool BrowserView::IsFeaturePromoActive(const base::Feature& iph_feature) const {
  return feature_promo_controller_ &&
         feature_promo_controller_->IsPromoActive(
             iph_feature, user_education::FeaturePromoStatus::kContinued);
}

user_education::FeaturePromoResult BrowserView::CanShowFeaturePromo(
    const base::Feature& iph_feature) const {
  if (!initialized_) {
    return user_education::FeaturePromoResult::kError;
  }

  if (!feature_promo_controller_) {
    return user_education::FeaturePromoResult::kBlockedByContext;
  }

  return feature_promo_controller_->CanShowPromo(iph_feature);
}

void BrowserView::MaybeShowFeaturePromo(
    user_education::FeaturePromoParams params) {
  // Trying to show a promo before the browser is initialized can result in a
  // failure to retrieve accelerators, which can cause issues for screen reader
  // users.
  if (!initialized_) {
    LOG(ERROR) << "Attempting to show IPH " << params.feature->name
               << " before browser initialization; IPH will not be shown.";
    user_education::FeaturePromoController::PostShowPromoResult(
        std::move(params.show_promo_result_callback),
        user_education::FeaturePromoResult::kError);
    return;
  }

  if (!feature_promo_controller_) {
    user_education::FeaturePromoController::PostShowPromoResult(
        std::move(params.show_promo_result_callback),
        user_education::FeaturePromoResult::kBlockedByContext);
    return;
  }

  feature_promo_controller_->MaybeShowPromo(std::move(params));
}

void BrowserView::MaybeShowStartupFeaturePromo(
    user_education::FeaturePromoParams params) {
  if (feature_promo_controller_) {
    // Preconditions for feature promos may require the browser to be fully
    // constructed before they can be run. Post this task to ensure browser
    // initialization is complete before attempting to show startup promos.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&user_education::FeaturePromoControllerCommon::
                           MaybeShowStartupPromo,
                       feature_promo_controller_->GetAsWeakPtr(),
                       std::move(params)));
  }
}

bool BrowserView::AbortFeaturePromo(const base::Feature& iph_feature) {
  return feature_promo_controller_ &&
         feature_promo_controller_->EndPromo(
             iph_feature, user_education::EndFeaturePromoReason::kAbortPromo);
}

user_education::FeaturePromoHandle BrowserView::CloseFeaturePromoAndContinue(
    const base::Feature& iph_feature) {
  if (!feature_promo_controller_ ||
      feature_promo_controller_->GetPromoStatus(iph_feature) !=
          user_education::FeaturePromoStatus::kBubbleShowing) {
    return user_education::FeaturePromoHandle();
  }
  return feature_promo_controller_->CloseBubbleAndContinuePromo(iph_feature);
}

bool BrowserView::NotifyFeaturePromoFeatureUsed(
    const base::Feature& feature,
    FeaturePromoFeatureUsedAction action) {
  if (feature_promo_controller_) {
    feature_promo_controller_->NotifyFeatureUsedIfValid(feature);
    if (action == FeaturePromoFeatureUsedAction::kClosePromoIfPresent) {
      return feature_promo_controller_->EndPromo(
          feature, user_education::EndFeaturePromoReason::kFeatureEngaged);
    }
  }
  return false;
}

void BrowserView::NotifyAdditionalConditionEvent(const char* event_name) {
  if (!feature_promo_controller_) {
    return;
  }
  if (auto* const tracker =
          feature_engagement::TrackerFactory::GetForBrowserContext(
              GetProfile())) {
    tracker->NotifyEvent(event_name);
  }
}

user_education::DisplayNewBadge BrowserView::MaybeShowNewBadgeFor(
    const base::Feature& feature) {
  auto* const service =
      UserEducationServiceFactory::GetForBrowserContext(GetProfile());
  if (!service || !service->new_badge_controller()) {
    return user_education::DisplayNewBadge();
  }
  return service->new_badge_controller()->MaybeShowNewBadge(feature);
}

void BrowserView::NotifyNewBadgeFeatureUsed(const base::Feature& feature) {
  auto* const service =
      UserEducationServiceFactory::GetForBrowserContext(GetProfile());
  if (service && service->new_badge_registry() &&
      service->new_badge_registry()->IsFeatureRegistered(feature)) {
    service->new_badge_controller()->NotifyFeatureUsedIfValid(feature);
  }
}

void BrowserView::ActivateAppModalDialog() const {
  // If another browser is app modal, flash and activate the modal browser.
  javascript_dialogs::AppModalDialogController* active_dialog =
      javascript_dialogs::AppModalDialogQueue::GetInstance()->active_dialog();
  if (!active_dialog) {
    return;
  }

  Browser* modal_browser =
      chrome::FindBrowserWithTab(active_dialog->web_contents());
  if (modal_browser && (browser_.get() != modal_browser)) {
    modal_browser->window()->FlashFrame(true);
    modal_browser->window()->Activate();
  }

  javascript_dialogs::AppModalDialogQueue::GetInstance()->ActivateModalDialog();
}

bool BrowserView::FindCommandIdForAccelerator(
    const ui::Accelerator& accelerator,
    int* command_id) const {
  auto iter = accelerator_table_.find(accelerator);
  if (iter == accelerator_table_.end()) {
    return false;
  }

  *command_id = iter->second;
  if (accelerator.IsRepeat() && !IsCommandRepeatable(*command_id)) {
    return false;
  }

  return true;
}

void BrowserView::ObserveAppBannerManager(
    webapps::AppBannerManager* new_manager) {
  app_banner_manager_observation_.Reset();
  app_banner_manager_observation_.Observe(new_manager);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ExclusiveAccessContext implementation:
Profile* BrowserView::GetProfile() {
  return browser_->profile();
}

void BrowserView::UpdateUIForTabFullscreen() {
  frame()->GetFrameView()->UpdateFullscreenTopUI();
}

WebContents* BrowserView::GetWebContentsForExclusiveAccess() {
  return GetActiveWebContents();
}

void BrowserView::UnhideDownloadShelf() {
  if (download_shelf_) {
    download_shelf_->Unhide();
  }
}

void BrowserView::HideDownloadShelf() {
  if (download_shelf_) {
    download_shelf_->Hide();
  }

  std::vector<StatusBubble*> status_bubbles = GetStatusBubbles();
  for (StatusBubble* status_bubble : status_bubbles) {
    status_bubble->Hide();
  }
}

bool BrowserView::CanUserEnterFullscreen() const {
  return CanFullscreen();
}

bool BrowserView::CanUserExitFullscreen() const {
  return frame_->GetFrameView()->CanUserExitFullscreen();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ExclusiveAccessBubbleViewsContext implementation:
ExclusiveAccessManager* BrowserView::GetExclusiveAccessManager() {
  return browser_->GetFeatures().exclusive_access_manager();
}

ui::AcceleratorProvider* BrowserView::GetAcceleratorProvider() {
  return this;
}

gfx::NativeView BrowserView::GetBubbleParentView() const {
  return GetWidget()->GetNativeView();
}

gfx::Rect BrowserView::GetClientAreaBoundsInScreen() const {
  return GetWidget()->GetClientAreaBoundsInScreen();
}

bool BrowserView::IsImmersiveModeEnabled() const {
  return immersive_mode_controller()->IsEnabled();
}

gfx::Rect BrowserView::GetTopContainerBoundsInScreen() {
  return top_container_->GetBoundsInScreen();
}

void BrowserView::DestroyAnyExclusiveAccessBubble() {
  exclusive_access_bubble_.reset();
  exclusive_access_bubble_destruction_task_id_.reset();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, extension::ExtensionKeybindingRegistry::Delegate implementation:
content::WebContents* BrowserView::GetWebContentsForExtension() {
  return GetActiveWebContents();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ImmersiveModeController::Observer implementation:
void BrowserView::OnImmersiveRevealStarted() {
  AppMenuButton* app_menu_button =
      toolbar_button_provider()->GetAppMenuButton();
  if (app_menu_button) {
    app_menu_button->CloseMenu();
  }

  top_container()->SetPaintToLayer();
  top_container()->layer()->SetFillsBoundsOpaquely(false);
  overlay_view_->AddChildViewRaw(top_container());
  overlay_view_->SetVisible(true);
  InvalidateLayout();
  GetWidget()->GetRootView()->DeprecatedLayoutImmediately();

#if BUILDFLAG(IS_CHROMEOS)
  top_container()->SetBackground(
      views::CreateSolidBackground(ui::kColorFrameActive));
#endif  // BUILDFLAG(IS_CHROMEOS)
}

void BrowserView::OnImmersiveRevealEnded() {
  ReparentTopContainerForEndOfImmersive();
  InvalidateLayout();
  GetWidget()->GetRootView()->DeprecatedLayoutImmediately();

#if BUILDFLAG(IS_CHROMEOS)
  // Ensure that entering/exiting tablet mode on ChromeOS also updates Window
  // Controls Overlay (WCO). This forces a re-check of the immersive mode flag.
  // Tablet mode implies immersive mode, so if tablet mode is enabled, this will
  // automatically disable WCO, and vice versa.
  if (AppUsesWindowControlsOverlay()) {
    UpdateWindowControlsOverlayEnabled();
  }
  top_container()->SetBackground(nullptr);
#endif  // BUILDFLAG(IS_CHROMEOS)
}

void BrowserView::OnImmersiveFullscreenExited() {
  OnImmersiveRevealEnded();
}

void BrowserView::OnImmersiveModeControllerDestroyed() {
  ReparentTopContainerForEndOfImmersive();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, webapps::AppBannerManager::Observer implementation:
void BrowserView::OnInstallableWebAppStatusUpdated(
    webapps::InstallableWebAppCheckResult result,
    const std::optional<webapps::WebAppBannerData>& data) {
  UpdatePageActionIcon(PageActionIconType::kPwaInstall);
}

void BrowserView::OnWillChangeFocus(View* focused_before, View* focused_now) {
  UpdateAccessibleNameForRootView();
}
void BrowserView::OnDidChangeFocus(View* focused_before, View* focused_now) {
  UpdateAccessibleNameForRootView();
}

WebAppFrameToolbarView* BrowserView::web_app_frame_toolbar() {
  return web_app_frame_toolbar_;
}

const WebAppFrameToolbarView* BrowserView::web_app_frame_toolbar() const {
  return web_app_frame_toolbar_;
}

void BrowserView::PaintAsActiveChanged() {
  const bool is_active = frame_->ShouldPaintAsActive();

  // TODO: Unify semantics of "active" between the BrowserList and
  // BrowserWindowInterface clients. The latter is more accurate definition
  // where the top level window or any of its child widgets can have focus.
  if (is_active) {
    browser_->DidBecomeActive();
  } else {
    browser_->DidBecomeInactive();
  }

  if (web_app_frame_toolbar()) {
    web_app_frame_toolbar()->SetPaintAsActive(is_active);
  }
  FrameColorsChanged();
}

void BrowserView::FrameColorsChanged() {
  if (web_app_window_title_) {
    SkColor frame_color = frame_->GetFrameView()->GetFrameColor(
        BrowserFrameActiveState::kUseCurrent);
    SkColor caption_color = frame_->GetFrameView()->GetCaptionColor(
        BrowserFrameActiveState::kUseCurrent);
    web_app_window_title_->SetBackgroundColor(frame_color);
    web_app_window_title_->SetEnabledColor(caption_color);
  }
}

void BrowserView::UpdateAccessibleNameForRootView() {
  if (GetWidget()) {
    GetWidget()->UpdateAccessibleNameForRootView();
  }
}

void BrowserView::UpdateAccessibleURLForRootView(const GURL& url) {
  if (GetWidget()) {
    GetWidget()->UpdateAccessibleURLForRootView(url);
  }
}

void BrowserView::UpdateFullscreenAllowedFromPolicy(
    bool allowed_without_policy) {
  auto* fullscreen_pref_path = prefs::kFullscreenAllowed;
  if (GetProfile()->GetPrefs()->HasPrefPath(fullscreen_pref_path)) {
    SetCanFullscreen(
        allowed_without_policy &&
        GetProfile()->GetPrefs()->GetBoolean(fullscreen_pref_path));
  }
}

bool BrowserView::ShouldUseBrowserContentMinimumSize() const {
  return browser()->is_type_normal() || IsBrowserAWebApp();
}

bool BrowserView::IsBrowserAWebApp() const {
  bool is_web_app = browser()->is_type_app() && GetIsWebAppType();
#if BUILDFLAG(IS_CHROMEOS)
  // app_controller() is only available if the BrowserView is a WebAppType.
  is_web_app = is_web_app && !browser()->app_controller()->system_app();
#endif
  return is_web_app;
}

void BrowserView::ApplyWatermarkSettings(const std::string& watermark_text) {
  if (watermark_view_) {
    PrefService* prefs = browser_->profile()->GetPrefs();
    watermark_view_->SetString(watermark_text,
                               enterprise_watermark::GetFillColor(prefs),
                               enterprise_watermark::GetOutlineColor(prefs));
  }
}

#if BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)
void BrowserView::ApplyScreenshotSettings(bool allow) {
#if BUILDFLAG(IS_WIN)
  DCHECK_NE(GetWidget()->GetNativeWindow()->GetHost()->GetAcceleratedWidget(),
            gfx::kNullAcceleratedWidget);
#endif  // BUILDFLAG(IS_WIN)
  GetWidget()->SetAllowScreenshots(allow);
}
#endif  // BUILDFLAG(ENTERPRISE_SCREENSHOT_PROTECTION)

BEGIN_METADATA(BrowserView)
ADD_READONLY_PROPERTY_METADATA(gfx::Rect, FindBarBoundingBox)
ADD_READONLY_PROPERTY_METADATA(int, TabStripHeight)
ADD_READONLY_PROPERTY_METADATA(bool, TabStripVisible)
ADD_READONLY_PROPERTY_METADATA(bool, Incognito)
ADD_READONLY_PROPERTY_METADATA(bool, GuestSession)
ADD_READONLY_PROPERTY_METADATA(bool, RegularOrGuestSession)
ADD_READONLY_PROPERTY_METADATA(bool, SupportsTabStrip)
ADD_READONLY_PROPERTY_METADATA(bool, IsNormalType)
ADD_READONLY_PROPERTY_METADATA(bool, IsWebAppType)
ADD_READONLY_PROPERTY_METADATA(bool, TopControlsSlideBehaviorEnabled)
#if BUILDFLAG(IS_WIN)
ADD_READONLY_PROPERTY_METADATA(bool, SupportsTitle)
ADD_READONLY_PROPERTY_METADATA(bool, SupportsIcon)
#endif
ADD_READONLY_PROPERTY_METADATA(float, TopControlsSlideBehaviorShownRatio)
END_METADATA
