// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

// Instructions for adding new entries to this file:
// https://chromium.googlesource.com/chromium/src/+/main/docs/how_to_add_your_feature_flag.md#step-2_adding-the-feature-flag-to-the-chrome_flags-ui

#include "chrome/browser/about_flags.h"

#include <array>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/allocator/partition_alloc_features.h"
#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/features.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/base_i18n_switches.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram_functions.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/task_features.h"
#include "base/values.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "cc/base/features.h"
#include "cc/base/switches.h"
#include "chrome/browser/apps/app_discovery_service/app_discovery_service.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/features.h"
#include "chrome/browser/flag_descriptions.h"
#include "chrome/browser/ip_protection/ip_protection_switches.h"
#include "chrome/browser/login_detection/login_detection_util.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_constants.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_feature.h"
#include "chrome/browser/navigation_predictor/navigation_predictor_features.h"
#include "chrome/browser/navigation_predictor/search_engine_preconnector.h"
#include "chrome/browser/net/stub_resolver_config_reader.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/notifications/scheduler/public/features.h"
#include "chrome/browser/page_info/page_info_features.h"
#include "chrome/browser/permissions/notifications_permission_revocation_config.h"
#include "chrome/browser/permissions/quiet_notification_permission_ui_config.h"
#include "chrome/browser/predictors/loading_predictor_config.h"
#include "chrome/browser/preloading/prefetch/search_prefetch/field_trial_settings.h"
#include "chrome/browser/preloading/preloading_features.h"
#include "chrome/browser/preloading/search_preload/search_preload_features.h"
#include "chrome/browser/resource_coordinator/tab_manager_features.h"
#include "chrome/browser/sharing_hub/sharing_hub_features.h"
#include "chrome/browser/site_isolation/about_flags.h"
#include "chrome/browser/task_manager/common/task_manager_features.h"
#include "chrome/browser/tpcd/experiment/tpcd_experiment_features.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_group_home/constants.h"
#include "chrome/browser/ui/toasts/toast_features.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/unexpire_flags.h"
#include "chrome/browser/unexpire_flags_gen.h"
#include "chrome/browser/web_applications/link_capturing_features.h"
#include "chrome/browser/webauthn/webauthn_switches.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/constants/chromeos_features.h"
#include "components/android_autofill/browser/android_autofill_features.h"
#include "components/assist_ranker/predictor_config_definitions.h"
#include "components/autofill/core/browser/studies/autofill_experiments.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/browsing_data/core/features.h"
#include "components/collaboration/public/features.h"
#include "components/commerce/core/commerce_feature_list.h"
#include "components/commerce/core/flag_descriptions.h"
#include "components/component_updater/component_updater_command_line_config_policy.h"
#include "components/component_updater/component_updater_switches.h"
#include "components/compose/buildflags.h"
#include "components/compose/core/browser/compose_features.h"
#include "components/content_settings/core/common/features.h"
#include "components/contextual_search/core/browser/contextual_search_field_trial.h"
#include "components/contextual_search/core/browser/public.h"
#include "components/data_sharing/public/features.h"
#include "components/data_sharing/public/switches.h"
#include "components/dom_distiller/core/dom_distiller_features.h"
#include "components/dom_distiller/core/dom_distiller_switches.h"
#include "components/download/public/common/download_features.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/enterprise/data_controls/core/browser/features.h"
#include "components/enterprise/obfuscation/core/utils.h"
#include "components/error_page/common/error_page_switches.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/feature_list.h"
#include "components/feed/feed_feature_list.h"
#include "components/fingerprinting_protection_filter/common/fingerprinting_protection_filter_features.h"
#include "components/fingerprinting_protection_filter/interventions/common/interventions_features.h"
#include "components/heavy_ad_intervention/heavy_ad_features.h"
#include "components/history/core/browser/features.h"
#include "components/history_clusters/core/config.h"
#include "components/history_clusters/core/features.h"
#include "components/history_clusters/core/on_device_clustering_features.h"
#include "components/history_embeddings/history_embeddings_features.h"
#include "components/input/features.h"
#include "components/language/core/common/language_experiments.h"
#include "components/lens/buildflags.h"
#include "components/lens/lens_features.h"
#include "components/manta/features.h"
#include "components/mirroring/service/mirroring_features.h"
#include "components/network_session_configurator/common/network_features.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_field_trial.h"
#include "components/ntp_tiles/features.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/common/omnibox_feature_configs.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/open_from_clipboard/clipboard_recent_content_features.h"
#include "components/optimization_guide/core/model_execution/model_execution_features.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/optimization_guide_switches.h"
#include "components/page_content_annotations/core/page_content_annotations_features.h"
#include "components/page_content_annotations/core/page_content_annotations_switches.h"
#include "components/page_image_service/features.h"
#include "components/page_info/core/features.h"
#include "components/paint_preview/buildflags/buildflags.h"
#include "components/paint_preview/features/features.h"
#include "components/password_manager/core/browser/features/password_features.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/payments/core/features.h"
#include "components/performance_manager/public/features.h"
#include "components/permissions/features.h"
#include "components/plus_addresses/features.h"
#include "components/power_bookmarks/core/power_bookmark_features.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/remote_cocoa/app_shim/features.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/saved_tab_groups/public/features.h"
#include "components/search/ntp_features.h"
#include "components/search_engines/search_engines_switches.h"
#include "components/security_interstitials/content/stateful_ssl_host_state_delegate.h"
#include "components/security_state/core/security_state.h"
#include "components/segmentation_platform/embedder/home_modules/constants.h"
#include "components/segmentation_platform/public/features.h"
#include "components/send_tab_to_self/features.h"
#include "components/sensitive_content/features.h"
#include "components/services/heap_profiling/public/cpp/switches.h"
#include "components/services/storage/public/cpp/buckets/bucket_info.h"
#include "components/shared_highlighting/core/common/shared_highlighting_features.h"
#include "components/sharing_message/features.h"
#include "components/signin/core/browser/dice_account_reconcilor_delegate.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/site_isolation/features.h"
#include "components/soda/soda_features.h"
#include "components/spellcheck/common/spellcheck_features.h"
#include "components/spellcheck/spellcheck_buildflags.h"
#include "components/supervised_user/core/common/features.h"
#include "components/supervised_user/core/common/supervised_user_constants.h"
#include "components/sync/base/command_line_switches.h"
#include "components/sync/base/features.h"
#include "components/tracing/common/tracing_switches.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "components/translate/core/browser/translate_ranker_impl.h"
#include "components/translate/core/common/translate_util.h"
#include "components/trusted_vault/features.h"
#include "components/ui_devtools/switches.h"
#include "components/variations/variations_switches.h"
#include "components/version_info/channel.h"
#include "components/version_info/version_info.h"
#include "components/visited_url_ranking/public/features.h"
#include "components/viz/common/features.h"
#include "components/viz/common/switches.h"
#include "components/webapps/browser/features.h"
#include "components/webapps/isolated_web_apps/features.h"
#include "components/webui/flags/feature_entry.h"
#include "components/webui/flags/feature_entry_macros.h"
#include "components/webui/flags/flags_state.h"
#include "components/webui/flags/flags_storage.h"
#include "components/webui/flags/flags_ui_metrics.h"
#include "components/webui/flags/flags_ui_switches.h"
#include "components/webui/flags/pref_service_flags_storage.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "device/base/features.h"
#include "device/bluetooth/bluez/bluez_features.h"
#include "device/bluetooth/chromeos_platform_features.h"
#include "device/bluetooth/floss/floss_features.h"
#include "device/fido/features.h"
#include "device/gamepad/public/cpp/gamepad_features.h"
#include "device/vr/buildflags/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "flag_descriptions.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/config/gpu_switches.h"
#include "media/audio/audio_features.h"
#include "media/base/media_switches.h"
#include "media/capture/capture_switches.h"
#include "media/media_buildflags.h"
#include "media/midi/midi_switches.h"
#include "media/webrtc/webrtc_features.h"
#include "mojo/core/embedder/features.h"
#include "net/base/features.h"
#include "net/net_buildflags.h"
#include "net/nqe/effective_connection_type.h"
#include "net/nqe/network_quality_estimator_params.h"
#include "net/websockets/websocket_basic_handshake_stream.h"
#include "partition_alloc/buildflags.h"
#include "pdf/buildflags.h"
#include "ppapi/buildflags/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "sandbox/policy/features.h"
#include "sandbox/policy/switches.h"
#include "services/device/public/cpp/device_features.h"
#include "services/media_session/public/cpp/features.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/tracing/public/cpp/tracing_features.h"
#include "services/webnn/public/mojom/features.mojom-features.h"
#include "skia/buildflags.h"
#include "storage/browser/blob/features.h"
#include "storage/browser/quota/quota_features.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/features_generated.h"
#include "third_party/blink/public/common/forcedark/forcedark_switches.h"
#include "third_party/blink/public/common/switches.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/accessibility_switches.h"
#include "ui/base/ozone_buildflags.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/ui_base_switches.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/display/display_features.h"
#include "ui/display/display_switches.h"
#include "ui/events/blink/blink_features.h"
#include "ui/events/event_switches.h"
#include "ui/gfx/switches.h"
#include "ui/gl/buildflags.h"
#include "ui/gl/gl_features.h"
#include "ui/gl/gl_switches.h"
#include "ui/native_theme/features/native_theme_features.h"
#include "ui/ui_features.h"
#include "url/url_features.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/extension_features.h"
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "base/allocator/buildflags.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "base/process/process.h"
#include "chrome/browser/apps/app_service/chrome_app_deprecation/chrome_app_deprecation.h"
#include "chromeos/constants/chromeos_features.h"
#endif

#if BUILDFLAG(IS_ANDROID)
#include "base/android/pre_freeze_background_memory_trimmer.h"
#include "chrome/browser/contextmenu/context_menu_features.h"
#include "chrome/browser/flags/android/chrome_feature_list.h"
#include "chrome/browser/notifications/chime/android/features.h"
#include "chrome/browser/push_messaging/push_messaging_features.h"
#include "components/browser_ui/photo_picker/android/features.h"
#include "components/credential_management/android/features.h"
#include "components/enterprise/connectors/core/features.h"
#include "components/external_intents/android/external_intents_features.h"
#include "components/facilitated_payments/core/features/features.h"
#include "components/messages/android/messages_feature.h"
#include "components/payments/content/android/payment_feature_map.h"
#include "components/segmentation_platform/public/features.h"
#include "components/translate/content/android/translate_message.h"
#include "ui/android/ui_android_features.h"
#else  // BUILDFLAG(IS_ANDROID)
#include "chrome/browser/component_updater/iwa_key_distribution_component_installer.h"
#include "chrome/browser/media/router/discovery/access_code/access_code_cast_sink_service.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/web_applications/preinstalled_app_install_features.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/app_list/app_list_features.h"
#include "ash/public/cpp/keyboard/keyboard_switches.h"
#include "chrome/browser/ash/app_list/search/search_features.h"
#include "chrome/browser/ash/crostini/crostini_util.h"
#include "chrome/browser/ash/file_suggest/item_suggest_cache.h"
#include "chrome/browser/ash/ownership/owner_settings_service_ash.h"
#include "chrome/browser/ash/ownership/owner_settings_service_ash_factory.h"
#include "chrome/browser/ash/policy/reporting/metrics_reporting/metric_reporting_manager.h"
#include "chrome/browser/ash/settings/about_flags.h"
#include "chrome/browser/component_updater/cros_component_installer_chromeos.h"
#include "chrome/browser/nearby_sharing/common/nearby_share_features.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/webui_url_constants.h"
#include "chromeos/ash/components/assistant/buildflags.h"
#include "chromeos/ash/components/memory/swap_configuration.h"
#include "chromeos/ash/experiences/arc/arc_features.h"
#include "chromeos/ash/experiences/arc/arc_util.h"
#include "chromeos/ash/services/assistant/public/cpp/features.h"
#include "chromeos/constants/chromeos_switches.h"
#include "components/app_restore/features.h"
#include "components/cross_device/nearby/nearby_features.h"
#include "components/metrics/structured/structured_metrics_features.h"  // nogncheck
#include "media/capture/video/chromeos/video_capture_features_chromeos.h"
#include "remoting/host/chromeos/features.h"
#include "third_party/cros_system_api/switches/chrome_switches.h"
#include "ui/events/ozone/features.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/enterprise/platform_auth/platform_auth_features.h"
#include "chrome/browser/ui/browser_dialogs.h"
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/cws_info_service.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/switches.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(ENABLE_PDF)
#include "pdf/pdf_features.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "printing/printing_features.h"
#endif

#if BUILDFLAG(ENABLE_VR)
#include "device/vr/public/cpp/features.h"
#include "device/vr/public/cpp/switches.h"
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "ui/ozone/public/ozone_switches.h"
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/enterprise/platform_auth/platform_auth_features.h"
#include "chrome/browser/tracing/tracing_features.h"
#include "chrome/browser/win/mica_titlebar.h"
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "chrome/browser/contextual_cueing/contextual_cueing_features.h"  // nogncheck
#include "chrome/browser/enterprise/profile_management/profile_management_features.h"
#include "chrome/browser/enterprise/webstore/features.h"
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_ANDROID)
// This causes a gn error on Android builds, because gn does not understand
// buildflags.
#include "components/user_education/common/user_education_features.h"  // nogncheck
#endif

#if BUILDFLAG(IS_LINUX)
#include "chrome/browser/ui/ui_features.h"
#include "ui/base/ui_base_features.h"
#endif

#if defined(TOOLKIT_VIEWS)
#include "ui/views/views_features.h"
#include "ui/views/views_switches.h"
#endif  // defined(TOOLKIT_VIEWS)

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
#include "components/unexportable_keys/features.h"  // nogncheck
#endif

#if BUILDFLAG(SKIA_BUILD_RUST_PNG)
#include "skia/rusty_png_feature.h"
#endif

#if !BUILDFLAG(IS_CHROMEOS)
#include "components/variations/net/variations_command_line.h"
#endif

using flags_ui::FeatureEntry;
using flags_ui::kDeprecated;
using flags_ui::kOsAndroid;
using flags_ui::kOsCrOS;
using flags_ui::kOsCrOSOwnerOnly;
using flags_ui::kOsLinux;
using flags_ui::kOsMac;
using flags_ui::kOsWin;

namespace about_flags {

namespace {

const unsigned kOsAll = kOsMac | kOsWin | kOsLinux | kOsCrOS | kOsAndroid;
const unsigned kOsDesktop = kOsMac | kOsWin | kOsLinux | kOsCrOS;

#if defined(USE_AURA)
const unsigned kOsAura = kOsWin | kOsLinux | kOsCrOS;
#endif  // USE_AURA

#if defined(USE_AURA)
const FeatureEntry::Choice kPullToRefreshChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kPullToRefresh, "0"},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kPullToRefresh, "1"},
    {flag_descriptions::kPullToRefreshEnabledTouchscreen,
     switches::kPullToRefresh, "2"}};
#endif  // USE_AURA

const FeatureEntry::FeatureParam kLocalNetworkAccessChecksBlock[] = {
    {"LocalNetworkAccessChecksWarn", "false"}};
const FeatureEntry::FeatureVariation kLocalNetworkAccessChecksVariations[] = {
    {"(Blocking)", kLocalNetworkAccessChecksBlock,
     std::size(kLocalNetworkAccessChecksBlock), nullptr}};

const FeatureEntry::Choice kEnableBenchmarkingChoices[] = {
    {flag_descriptions::kEnableBenchmarkingChoiceDisabled, "", ""},
    {flag_descriptions::kEnableBenchmarkingChoiceDefaultFeatureStates,
     variations::switches::kEnableBenchmarking, ""},
    {flag_descriptions::kEnableBenchmarkingChoiceMatchFieldTrialTestingConfig,
     variations::switches::kEnableBenchmarking,
     variations::switches::kEnableFieldTrialTestingConfig},
};

const FeatureEntry::Choice kOverlayStrategiesChoices[] = {
    {flag_descriptions::kOverlayStrategiesDefault, "", ""},
    {flag_descriptions::kOverlayStrategiesNone,
     switches::kEnableHardwareOverlays, ""},
    {flag_descriptions::kOverlayStrategiesUnoccludedFullscreen,
     switches::kEnableHardwareOverlays, "single-fullscreen"},
    {flag_descriptions::kOverlayStrategiesUnoccluded,
     switches::kEnableHardwareOverlays, "single-fullscreen,single-on-top"},
    {flag_descriptions::kOverlayStrategiesOccludedAndUnoccluded,
     switches::kEnableHardwareOverlays,
     "single-fullscreen,single-on-top,underlay"},
};

const FeatureEntry::Choice kTouchTextSelectionStrategyChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kTouchSelectionStrategyCharacter,
     blink::switches::kTouchTextSelectionStrategy,
     blink::switches::kTouchTextSelectionStrategy_Character},
    {flag_descriptions::kTouchSelectionStrategyDirection,
     blink::switches::kTouchTextSelectionStrategy,
     blink::switches::kTouchTextSelectionStrategy_Direction}};

#if BUILDFLAG(IS_WIN)
const FeatureEntry::FeatureParam kMediaFoundationClearStrategyUseFrameServer[] =
    {{"strategy", "frame-server"}};

const FeatureEntry::FeatureParam
    kMediaFoundationClearStrategyUseDirectComposition[] = {
        {"strategy", "direct-composition"}};

const FeatureEntry::FeatureParam kMediaFoundationClearStrategyUseDynamic[] = {
    {"strategy", "dynamic"}};

const FeatureEntry::FeatureVariation kMediaFoundationClearStrategyVariations[] =
    {{"Direct Composition", kMediaFoundationClearStrategyUseDirectComposition,
      std::size(kMediaFoundationClearStrategyUseDirectComposition), nullptr},
     {"Frame Server", kMediaFoundationClearStrategyUseFrameServer,
      std::size(kMediaFoundationClearStrategyUseFrameServer), nullptr},
     {"Dynamic", kMediaFoundationClearStrategyUseDynamic,
      std::size(kMediaFoundationClearStrategyUseDynamic), nullptr}};

const FeatureEntry::Choice kUseAngleChoicesWindows[] = {
    {flag_descriptions::kUseAngleDefault, "", ""},
    {flag_descriptions::kUseAngleGL, switches::kUseANGLE,
     gl::kANGLEImplementationOpenGLName},
    {flag_descriptions::kUseAngleD3D11, switches::kUseANGLE,
     gl::kANGLEImplementationD3D11Name},
    {flag_descriptions::kUseAngleD3D9, switches::kUseANGLE,
     gl::kANGLEImplementationD3D9Name},
    {flag_descriptions::kUseAngleD3D11on12, switches::kUseANGLE,
     gl::kANGLEImplementationD3D11on12Name}};
#elif BUILDFLAG(IS_MAC)
const FeatureEntry::Choice kUseAngleChoicesMac[] = {
    {flag_descriptions::kUseAngleDefault, "", ""},
    {flag_descriptions::kUseAngleGL, switches::kUseANGLE,
     gl::kANGLEImplementationOpenGLName},
    {flag_descriptions::kUseAngleMetal, switches::kUseANGLE,
     gl::kANGLEImplementationMetalName}};
#elif BUILDFLAG(IS_ANDROID)
const FeatureEntry::Choice kUseAngleChoicesAndroid[] = {
    {flag_descriptions::kUseAngleDefault, "", ""},
    {flag_descriptions::kUseAngleGLES, switches::kUseANGLE,
     gl::kANGLEImplementationOpenGLESName},
    {flag_descriptions::kUseAngleVulkan, switches::kUseANGLE,
     gl::kANGLEImplementationVulkanName}};
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
const FeatureEntry::Choice kExtensionsToolbarZeroStateChoices[] = {
    {flag_descriptions::kExtensionsToolbarZeroStateChoicesDisabled, "", ""},
    {flag_descriptions::kExtensionsToolbarZeroStateVistWebStore,
     switches::kExtensionsToolbarZeroStateVariation,
     switches::kExtensionsToolbarZeroStateSingleWebStoreLink},
    {flag_descriptions::kExtensionsToolbarZeroStateExploreExtensionsByCategory,
     switches::kExtensionsToolbarZeroStateVariation,
     switches::kExtensionsToolbarZeroStateExploreExtensionsByCategory},
};
#endif  // ENABLE_EXTENSIONS

#if BUILDFLAG(IS_WIN)
const FeatureEntry::FeatureParam kDXGIWaitableSwapChain1Frame = {
    "DXGIWaitableSwapChainMaxQueuedFrames", "1"};

const FeatureEntry::FeatureParam kDXGIWaitableSwapChain2Frames = {
    "DXGIWaitableSwapChainMaxQueuedFrames", "2"};

const FeatureEntry::FeatureParam kDXGIWaitableSwapChain3Frames = {
    "DXGIWaitableSwapChainMaxQueuedFrames", "3"};

const FeatureEntry::FeatureVariation kDXGIWaitableSwapChainVariations[] = {
    {"Max 1 Frame", &kDXGIWaitableSwapChain1Frame, 1, nullptr},
    {"Max 2 Frames", &kDXGIWaitableSwapChain2Frames, 1, nullptr},
    {"Max 3 Frames", &kDXGIWaitableSwapChain3Frames, 1, nullptr}};
#endif

#if BUILDFLAG(IS_LINUX)
const FeatureEntry::Choice kOzonePlatformHintRuntimeChoices[] = {
    {flag_descriptions::kOzonePlatformHintChoiceDefault, "", ""},
    {flag_descriptions::kOzonePlatformHintChoiceAuto,
     switches::kOzonePlatformHint, "auto"},
#if BUILDFLAG(IS_OZONE_X11)
    {flag_descriptions::kOzonePlatformHintChoiceX11,
     switches::kOzonePlatformHint, "x11"},
#endif
#if BUILDFLAG(IS_OZONE_WAYLAND)
    {flag_descriptions::kOzonePlatformHintChoiceWayland,
     switches::kOzonePlatformHint, "wayland"},
#endif
};
#endif

#if BUILDFLAG(ENABLE_VR)
const FeatureEntry::Choice kWebXrForceRuntimeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kWebXrRuntimeChoiceNone, switches::kWebXrForceRuntime,
     switches::kWebXrRuntimeNone},
#if BUILDFLAG(ENABLE_ARCORE)
    {flag_descriptions::kWebXrRuntimeChoiceArCore, switches::kWebXrForceRuntime,
     switches::kWebXrRuntimeArCore},
#endif
#if BUILDFLAG(ENABLE_CARDBOARD)
    {flag_descriptions::kWebXrRuntimeChoiceCardboard,
     switches::kWebXrForceRuntime, switches::kWebXrRuntimeCardboard},
#endif
#if BUILDFLAG(ENABLE_OPENXR)
    {flag_descriptions::kWebXrRuntimeChoiceOpenXR, switches::kWebXrForceRuntime,
     switches::kWebXrRuntimeOpenXr},
#endif  // ENABLE_OPENXR
    {flag_descriptions::kWebXrRuntimeChoiceOrientationSensors,
     switches::kWebXrForceRuntime, switches::kWebXrRuntimeOrientationSensors},
};

const FeatureEntry::Choice KWebXrHandAnonymizationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kWebXrHandAnonymizationChoiceRuntime,
     device::switches::kWebXrHandAnonymizationStrategy,
     device::switches::kWebXrHandAnonymizationStrategyRuntime},
    {flag_descriptions::kWebXrHandAnonymizationChoiceFallback,
     device::switches::kWebXrHandAnonymizationStrategy,
     device::switches::kWebXrHandAnonymizationStrategyFallback},
    {flag_descriptions::kWebXrHandAnonymizationChoiceNone,
     device::switches::kWebXrHandAnonymizationStrategy,
     device::switches::kWebXrHandAnonymizationStrategyNone},
};
#endif  // ENABLE_VR

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kCCTAdaptiveButtonEnable[] = {
    {"open_in_browser", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonCPAOnly[] = {
    {"contextual_only", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonOpenInBrowserCPA[] = {
    {"open_in_browser", "true"},
    {"default_variant", "15"},  // 15 == Open In Browser
    {"contextual_only", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonMenuOpenInBrowserTop[] = {
    {"open_in_browser", "true"},
    {"show_open_in_browser_menu_top", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonMenuRemoval[] = {
    {"open_in_browser", "true"},
    {"remove_find_in_page_menu_item", "true"},
    {"remove_desktop_site_menu_item", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonMenuCombo[] = {
    {"open_in_browser", "true"},
    {"show_open_in_browser_menu_top", "true"},
    {"remove_find_in_page_menu_item", "true"},
    {"remove_desktop_site_menu_item", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonMLTraining[] = {
    {"ml_training", "true"},
    {"open_in_browser", "true"}};
const FeatureEntry::FeatureVariation kCCTAdaptiveButtonVariations[] = {
    {"+OpenInBrowser", kCCTAdaptiveButtonEnable,
     std::size(kCCTAdaptiveButtonEnable), nullptr},
    {"CPA only", kCCTAdaptiveButtonCPAOnly,
     std::size(kCCTAdaptiveButtonCPAOnly), nullptr},
    {"CPA+OpenInBrowser", kCCTAdaptiveButtonOpenInBrowserCPA,
     std::size(kCCTAdaptiveButtonOpenInBrowserCPA), nullptr},
    {"Menu: OpenInBrowser at Top", kCCTAdaptiveButtonMenuOpenInBrowserTop,
     std::size(kCCTAdaptiveButtonMenuOpenInBrowserTop), nullptr},
    {"Menu: Remove FineInPage/DesktopSite", kCCTAdaptiveButtonMenuRemoval,
     std::size(kCCTAdaptiveButtonMenuRemoval), nullptr},
    {"Menu: Combine above 2", kCCTAdaptiveButtonMenuCombo,
     std::size(kCCTAdaptiveButtonMenuCombo), nullptr},
    {"for ML training", kCCTAdaptiveButtonMLTraining,
     std::size(kCCTAdaptiveButtonMLTraining), nullptr},
};

const FeatureEntry::FeatureParam kCCTAdaptiveButtonTestSwitchHide[] = {
    {"hide-button", "true"},
    {"always-animate", "false"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonTestSwitchChip[] = {
    {"hide-button", "false"},
    {"always-animate", "true"}};
const FeatureEntry::FeatureParam kCCTAdaptiveButtonTestSwitchBoth[] = {
    {"hide-button", "true"},
    {"always-animate", "true"}};
const FeatureEntry::FeatureVariation kCCTAdaptiveButtonTestSwitchVariations[] =
    {
        {"+Hide button", kCCTAdaptiveButtonTestSwitchHide,
         std::size(kCCTAdaptiveButtonTestSwitchHide), nullptr},
        {"+Always animate chip", kCCTAdaptiveButtonTestSwitchChip,
         std::size(kCCTAdaptiveButtonTestSwitchChip), nullptr},
        {"+Both", kCCTAdaptiveButtonTestSwitchBoth,
         std::size(kCCTAdaptiveButtonTestSwitchBoth), nullptr},
};
const FeatureEntry::FeatureParam
    kAdaptiveButtonInTopToolbarPageSummaryDisableFallback[] = {
        {"intent_fallback", "false"},
};
const FeatureEntry::FeatureVariation
    kAdaptiveButtonInTopToolbarPageSummaryVariations[] = {
        {"(Disable intent fallback)",
         kAdaptiveButtonInTopToolbarPageSummaryDisableFallback,
         std::size(kAdaptiveButtonInTopToolbarPageSummaryDisableFallback),
         nullptr},
};

const FeatureEntry::FeatureParam kCCTAuthTabHttpsVerificationTimeout10000Ms[] =
    {{"verification_timeout_ms", "10000"}};
const FeatureEntry::FeatureParam kCCTAuthTabHttpsVerificationTimeout1000Ms[] = {
    {"verification_timeout_ms", "1000"}};

const FeatureEntry::FeatureVariation
    kCCTAuthTabEnableHttpsRedirectsVariations[] = {
        {"HTTPS verification timeout 10,000ms",
         kCCTAuthTabHttpsVerificationTimeout10000Ms,
         std::size(kCCTAuthTabHttpsVerificationTimeout10000Ms), nullptr},
        {"HTTPS verification timeout 1000ms",
         kCCTAuthTabHttpsVerificationTimeout1000Ms,
         std::size(kCCTAuthTabHttpsVerificationTimeout1000Ms), nullptr}};

const FeatureEntry::FeatureParam kCCTMinimizedDefaultIcon[] = {
    {"icon_variant", "0"}};
const FeatureEntry::FeatureParam kCCTMinimizedAlternativeIcon[] = {
    {"icon_variant", "1"}};

const FeatureEntry::FeatureVariation kCCTMinimizedIconVariations[] = {
    {"Use default minimize icon", kCCTMinimizedDefaultIcon,
     std::size(kCCTMinimizedDefaultIcon), nullptr},
    {"Use alternative minimize icon", kCCTMinimizedAlternativeIcon,
     std::size(kCCTMinimizedAlternativeIcon), nullptr}};

const FeatureEntry::FeatureParam kCCTResizablePolicyParamUseAllowlist[] = {
    {"default_policy", "use-allowlist"}};
const FeatureEntry::FeatureParam kCCTResizablePolicyParamUseDenylist[] = {
    {"default_policy", "use-denylist"}};

const FeatureEntry::FeatureVariation
    kCCTResizableThirdPartiesDefaultPolicyVariations[] = {
        {"Use Allowlist", kCCTResizablePolicyParamUseAllowlist,
         std::size(kCCTResizablePolicyParamUseAllowlist), nullptr},
        {"Use Denylist", kCCTResizablePolicyParamUseDenylist,
         std::size(kCCTResizablePolicyParamUseDenylist), nullptr}};

const FeatureEntry::FeatureParam kCCTBottomBarButtonBalancedWithHomeParam[] = {
    {"google_bottom_bar_button_list", "0,10,3,2"}};
const FeatureEntry::FeatureParam kCCTBottomBarButtonsBalancedWithCustomParam[] =
    {{"google_bottom_bar_button_list", "0,3,8,2"}};
const FeatureEntry::FeatureParam kCCTBottomBarButtonsBalancedWithSearchParam[] =
    {{"google_bottom_bar_button_list", "0,3,9,2"}};
const FeatureEntry::FeatureParam kCCTBottomBarHomeInSpotlightParam[] = {
    {"google_bottom_bar_button_list", "10,10,3,2"}};
const FeatureEntry::FeatureParam kCCTBottomBarCustomInSpotlightParam[] = {
    {"google_bottom_bar_button_list", "8,8,3,2"}};
const FeatureEntry::FeatureParam kCCTBottomBarSearchInSpotlightParam[] = {
    {"google_bottom_bar_button_list", "9,9,3,2"}};

const FeatureEntry::FeatureVariation kCCTGoogleBottomBarVariations[] = {
    {"Balanced with home button", kCCTBottomBarButtonBalancedWithHomeParam,
     std::size(kCCTBottomBarButtonBalancedWithHomeParam), nullptr},
    {"Balanced with custom button", kCCTBottomBarButtonsBalancedWithCustomParam,
     std::size(kCCTBottomBarButtonsBalancedWithCustomParam), nullptr},
    {"Balanced with search button", kCCTBottomBarButtonsBalancedWithSearchParam,
     std::size(kCCTBottomBarButtonsBalancedWithSearchParam), nullptr},
    {"home button in spotlight", kCCTBottomBarHomeInSpotlightParam,
     std::size(kCCTBottomBarHomeInSpotlightParam), nullptr},
    {"custom button in spotlight", kCCTBottomBarCustomInSpotlightParam,
     std::size(kCCTBottomBarCustomInSpotlightParam), nullptr},
    {"search button in spotlight", kCCTBottomBarSearchInSpotlightParam,
     std::size(kCCTBottomBarSearchInSpotlightParam), nullptr},
};

const FeatureEntry::FeatureParam kCCTDoubleDeckerBottomBarParam[] = {
    {"google_bottom_bar_variant_layout", "1"}};
const FeatureEntry::FeatureParam kCCTSingleDeckerBottomBarParam[] = {
    {"google_bottom_bar_variant_layout", "2"}};
const FeatureEntry::FeatureParam
    kCCTSingleDeckerBottomBarWithButtonsOnRightParam[] = {
        {"google_bottom_bar_variant_layout", "3"}};

const FeatureEntry::FeatureVariation
    kCCTGoogleBottomBarVariantLayoutsVariations[] = {
        {"Double decker", kCCTDoubleDeckerBottomBarParam,
         std::size(kCCTDoubleDeckerBottomBarParam), nullptr},
        {"Single decker", kCCTSingleDeckerBottomBarParam,
         std::size(kCCTSingleDeckerBottomBarParam), nullptr},
        {"Single decker with button(s) on right",
         kCCTSingleDeckerBottomBarWithButtonsOnRightParam,
         std::size(kCCTSingleDeckerBottomBarWithButtonsOnRightParam), nullptr},
};

const FeatureEntry::Choice kReaderModeHeuristicsChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kReaderModeHeuristicsMarkup,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kOGArticle},
    {flag_descriptions::kReaderModeHeuristicsAdaboost,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kAdaBoost},
    {flag_descriptions::kReaderModeHeuristicsAlwaysOn,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kAlwaysTrue},
    {flag_descriptions::kReaderModeHeuristicsAlwaysOff,
     switches::kReaderModeHeuristics, switches::reader_mode_heuristics::kNone},
    {flag_descriptions::kReaderModeHeuristicsAllArticles,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kAllArticles},
};

const FeatureEntry::FeatureParam
    kReaderModeImprovementsTriggerOnMobileFriendly[] = {
        {"trigger_on_mobile_friendly_pages", "true"}};
const FeatureEntry::FeatureParam kReaderModeImprovementsAlwaysOnEntryPoint[] = {
    {"always_on_entry_point", "true"}};
const FeatureEntry::FeatureParam kReaderModeImprovementsCustomCPATimeout[] = {
    {"custom_cpa_timeout_enabled", "true"},
    {"custom_cpa_timeout", "300"}};
const FeatureEntry::FeatureParam
    kReaderModeImprovementsShowReadingModeInRegularTab[] = {
        {"show_in_regular_tab", "true"}};
const FeatureEntry::FeatureParam kReaderModeImprovementsAllOn[] = {
    {"trigger_on_mobile_friendly_pages", "true"},
    {"always_on_entry_point", "true"},
    {"custom_cpa_timeout_enabled", "true"},
    {"custom_cpa_timeout", "300"},
    {"show_in_regular_tab", "true"}};

const FeatureEntry::FeatureVariation kReaderModeImprovementsChoices[] = {
    {"trigger on mobile-friendly pages",
     kReaderModeImprovementsTriggerOnMobileFriendly,
     std::size(kReaderModeImprovementsTriggerOnMobileFriendly), nullptr},
    {"always-on entry point", kReaderModeImprovementsAlwaysOnEntryPoint,
     std::size(kReaderModeImprovementsAlwaysOnEntryPoint), nullptr},
    {"increased cpa timeout", kReaderModeImprovementsCustomCPATimeout,
     std::size(kReaderModeImprovementsCustomCPATimeout), nullptr},
    {"reading mode in regular tab",
     kReaderModeImprovementsShowReadingModeInRegularTab,
     std::size(kReaderModeImprovementsShowReadingModeInRegularTab), nullptr},

    {"all", kReaderModeImprovementsAllOn,
     std::size(kReaderModeImprovementsAllOn), nullptr}};

const FeatureEntry::FeatureParam kReaderModeUseReadabilityDistiller[] = {
    {"use_distiller", "true"}};
const FeatureEntry::FeatureParam kReaderModeUseReadabilityHeuristic[] = {
    {"use_heuristic", "true"}};
const FeatureEntry::FeatureParam kReaderModeUseReadabilityAll[] = {
    {"use_distiller", "true"},
    {"use_heuristic", "true"}};

const FeatureEntry::FeatureVariation kReaderModeUseReadabilityChoices[] = {
    {"distiller only", kReaderModeUseReadabilityDistiller,
     std::size(kReaderModeUseReadabilityDistiller), nullptr},
    {"triggering heuristic only", kReaderModeUseReadabilityHeuristic,
     std::size(kReaderModeUseReadabilityHeuristic), nullptr},
    {"both distiller and triggering heuristic", kReaderModeUseReadabilityAll,
     std::size(kReaderModeUseReadabilityAll), nullptr}};

const FeatureEntry::Choice kForceUpdateMenuTypeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kUpdateMenuTypeNone, switches::kForceUpdateMenuType,
     "none"},
    {flag_descriptions::kUpdateMenuTypeUpdateAvailable,
     switches::kForceUpdateMenuType, "update_available"},
    {flag_descriptions::kUpdateMenuTypeUnsupportedOSVersion,
     switches::kForceUpdateMenuType, "unsupported_os_version"},
};

const FeatureEntry::FeatureParam kOmahaMinSdkVersionAndroidMinSdk1[] = {
    {"min_sdk_version", "1"}};
const FeatureEntry::FeatureParam kOmahaMinSdkVersionAndroidMinSdk1000[] = {
    {"min_sdk_version", "1000"}};
const FeatureEntry::FeatureVariation kOmahaMinSdkVersionAndroidVariations[] = {
    {flag_descriptions::kOmahaMinSdkVersionAndroidMinSdk1Description,
     kOmahaMinSdkVersionAndroidMinSdk1,
     std::size(kOmahaMinSdkVersionAndroidMinSdk1), nullptr},
    {flag_descriptions::kOmahaMinSdkVersionAndroidMinSdk1000Description,
     kOmahaMinSdkVersionAndroidMinSdk1000,
     std::size(kOmahaMinSdkVersionAndroidMinSdk1000), nullptr},
};

const FeatureEntry::FeatureParam
    kOptimizationGuidePersonalizedFetchingAllowPageInsights[] = {
        {"allowed_contexts", "CONTEXT_PAGE_INSIGHTS_HUB"}};
const FeatureEntry::FeatureVariation
    kOptimizationGuidePersonalizedFetchingAllowPageInsightsVariations[] = {
        {"for Page Insights",
         kOptimizationGuidePersonalizedFetchingAllowPageInsights,
         std::size(kOptimizationGuidePersonalizedFetchingAllowPageInsights),
         nullptr}};

const FeatureEntry::FeatureParam kFeedHeaderRemovalParam1 = {
    feed::kFeedHeaderRemovalTreatmentParam,
    feed::kFeedHeaderRemovalTreatmentValue1};
const FeatureEntry::FeatureParam kFeedHeaderRemovalParam2 = {
    feed::kFeedHeaderRemovalTreatmentParam,
    feed::kFeedHeaderRemovalTreatmentValue2};
const FeatureEntry::FeatureVariation kFeedHeaderRemovalVariations[] = {
    {"1", &kFeedHeaderRemovalParam1, 1, nullptr},
    {"2", &kFeedHeaderRemovalParam2, 1, nullptr},
};

const FeatureEntry::Choice kSafetyHubUnifiedPasswordsModuleChoices[] = {
    {"Default", "", ""},
    {"Enabled", switches::kEnableFeatures,
     "SafetyHubLocalPasswordsModule, SafetyHubUnifiedPasswordsModule"},
    {"Disabled", switches::kDisableFeatures,
     "SafetyHubLocalPasswordsModule, SafetyHubUnifiedPasswordsModule"},
};

#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kForceDark_SimpleHsl[] = {
    {"inversion_method", "hsl_based"},
    {"image_behavior", "none"},
    {"foreground_lightness_threshold", "150"},
    {"background_lightness_threshold", "205"}};

const FeatureEntry::FeatureParam kForceDark_SimpleCielab[] = {
    {"inversion_method", "cielab_based"},
    {"image_behavior", "none"},
    {"foreground_lightness_threshold", "150"},
    {"background_lightness_threshold", "205"}};

const FeatureEntry::FeatureParam kForceDark_SimpleRgb[] = {
    {"inversion_method", "rgb_based"},
    {"image_behavior", "none"},
    {"foreground_lightness_threshold", "150"},
    {"background_lightness_threshold", "205"}};

// Keep in sync with the kForceDark_SelectiveImageInversion
// in aw_feature_entries.cc if you tweak these parameters.
const FeatureEntry::FeatureParam kForceDark_SelectiveImageInversion[] = {
    {"inversion_method", "cielab_based"},
    {"image_behavior", "selective"},
    {"foreground_lightness_threshold", "150"},
    {"background_lightness_threshold", "205"}};

const FeatureEntry::FeatureParam kForceDark_SelectiveElementInversion[] = {
    {"inversion_method", "cielab_based"},
    {"image_behavior", "none"},
    {"foreground_lightness_threshold", "150"},
    {"background_lightness_threshold", "205"}};

const FeatureEntry::FeatureParam kForceDark_SelectiveGeneralInversion[] = {
    {"inversion_method", "cielab_based"},
    {"image_behavior", "selective"},
    {"foreground_lightness_threshold", "150"},
    {"background_lightness_threshold", "205"}};

const FeatureEntry::FeatureParam kForceDark_TransparencyAndNumColors[] = {
    {"classifier_policy", "transparency_and_num_colors"}};

const FeatureEntry::FeatureVariation kForceDarkVariations[] = {
    {"with simple HSL-based inversion", kForceDark_SimpleHsl,
     std::size(kForceDark_SimpleHsl), nullptr},
    {"with simple CIELAB-based inversion", kForceDark_SimpleCielab,
     std::size(kForceDark_SimpleCielab), nullptr},
    {"with simple RGB-based inversion", kForceDark_SimpleRgb,
     std::size(kForceDark_SimpleRgb), nullptr},
    {"with selective image inversion", kForceDark_SelectiveImageInversion,
     std::size(kForceDark_SelectiveImageInversion), nullptr},
    {"with selective inversion of non-image elements",
     kForceDark_SelectiveElementInversion,
     std::size(kForceDark_SelectiveElementInversion), nullptr},
    {"with selective inversion of everything",
     kForceDark_SelectiveGeneralInversion,
     std::size(kForceDark_SelectiveGeneralInversion), nullptr},
    {"with selective image inversion based on transparency and number of "
     "colors",
     kForceDark_TransparencyAndNumColors,
     std::size(kForceDark_TransparencyAndNumColors), nullptr}};
#endif  // !BUILDFLAG(IS_CHROMEOS)

const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialNoDialogParam[] = {
        {"dialog", "no_dialog"}};
const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialDefaultParam[] = {
        {"dialog", "default"}};
const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialLowRiskDialogParam[] = {
        {"dialog", "low_risk"}};
const FeatureEntry::FeatureParam
    kWebIdentityDigitalIdentityCredentialHighRiskDialogParam[] = {
        {"dialog", "high_risk"}};
const FeatureEntry::FeatureVariation
    kWebIdentityDigitalIdentityCredentialVariations[] = {
        {"with dialog depending on what credentials are requested",
         kWebIdentityDigitalIdentityCredentialDefaultParam,
         std::size(kWebIdentityDigitalIdentityCredentialDefaultParam), nullptr},
        {"without dialog", kWebIdentityDigitalIdentityCredentialNoDialogParam,
         std::size(kWebIdentityDigitalIdentityCredentialNoDialogParam),
         nullptr},
        {"with confirmation dialog with mild warning before sending identity "
         "request to Android OS",
         kWebIdentityDigitalIdentityCredentialLowRiskDialogParam,
         std::size(kWebIdentityDigitalIdentityCredentialLowRiskDialogParam),
         nullptr},
        {"with confirmation dialog with severe warning before sending "
         "identity request to Android OS",
         kWebIdentityDigitalIdentityCredentialHighRiskDialogParam,
         std::size(kWebIdentityDigitalIdentityCredentialHighRiskDialogParam),
         nullptr}};

const FeatureEntry::FeatureParam kClipboardMaximumAge60Seconds[] = {
    {"UIClipboardMaximumAge", "60"}};
const FeatureEntry::FeatureParam kClipboardMaximumAge90Seconds[] = {
    {"UIClipboardMaximumAge", "90"}};
const FeatureEntry::FeatureParam kClipboardMaximumAge120Seconds[] = {
    {"UIClipboardMaximumAge", "120"}};
const FeatureEntry::FeatureParam kClipboardMaximumAge150Seconds[] = {
    {"UIClipboardMaximumAge", "150"}};
const FeatureEntry::FeatureParam kClipboardMaximumAge180Seconds[] = {
    {"UIClipboardMaximumAge", "180"}};

const FeatureEntry::FeatureVariation kClipboardMaximumAgeVariations[] = {
    {"Enabled 60 seconds", kClipboardMaximumAge60Seconds,
     std::size(kClipboardMaximumAge60Seconds), nullptr},
    {"Enabled 90 seconds", kClipboardMaximumAge90Seconds,
     std::size(kClipboardMaximumAge90Seconds), nullptr},
    {"Enabled 120 seconds", kClipboardMaximumAge120Seconds,
     std::size(kClipboardMaximumAge120Seconds), nullptr},
    {"Enabled 150 seconds", kClipboardMaximumAge150Seconds,
     std::size(kClipboardMaximumAge150Seconds), nullptr},
    {"Enabled 180 seconds", kClipboardMaximumAge180Seconds,
     std::size(kClipboardMaximumAge180Seconds), nullptr},
};

const FeatureEntry::FeatureParam kMBIModeLegacy[] = {{"mode", "legacy"}};
const FeatureEntry::FeatureParam kMBIModeEnabledPerRenderProcessHost[] = {
    {"mode", "per_render_process_host"}};
const FeatureEntry::FeatureParam kMBIModeEnabledPerSiteInstance[] = {
    {"mode", "per_site_instance"}};

const FeatureEntry::FeatureVariation kMBIModeVariations[] = {
    {"legacy mode", kMBIModeLegacy, std::size(kMBIModeLegacy), nullptr},
    {"per render process host", kMBIModeEnabledPerRenderProcessHost,
     std::size(kMBIModeEnabledPerRenderProcessHost), nullptr},
    {"per site instance", kMBIModeEnabledPerSiteInstance,
     std::size(kMBIModeEnabledPerSiteInstance), nullptr}};

const FeatureEntry::FeatureParam kSearchPrefetchWithoutHoldback[] = {
    {"prefetch_holdback", "false"}};
const FeatureEntry::FeatureParam kSearchPrefetchWithHoldback[] = {
    {"prefetch_holdback", "true"}};

const FeatureEntry::FeatureVariation
    kSearchPrefetchServicePrefetchingVariations[] = {
        {"without holdback", kSearchPrefetchWithoutHoldback,
         std::size(kSearchPrefetchWithoutHoldback), nullptr},
        {"with holdback", kSearchPrefetchWithHoldback,
         std::size(kSearchPrefetchWithHoldback), nullptr}};

#if BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kArcVmMemorySizeShift_200[] = {
    {"shift_mib", "-200"}};
const FeatureEntry::FeatureParam kArcVmMemorySizeShift_500[] = {
    {"shift_mib", "-500"}};
const FeatureEntry::FeatureParam kArcVmMemorySizeShift_800[] = {
    {"shift_mib", "-800"}};

const FeatureEntry::FeatureVariation kArcVmMemorySizeVariations[] = {
    {"shift -200MiB", kArcVmMemorySizeShift_200,
     std::size(kArcVmMemorySizeShift_200), nullptr},
    {"shift -500MiB", kArcVmMemorySizeShift_500,
     std::size(kArcVmMemorySizeShift_500), nullptr},
    {"shift -800MiB", kArcVmMemorySizeShift_800,
     std::size(kArcVmMemorySizeShift_800), nullptr},
};
#endif  // BUILDFLAG(IS_CHROMEOS)

const FeatureEntry::Choice kEnableGpuRasterizationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kEnableGpuRasterization, ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kDisableGpuRasterization, ""},
};

const FeatureEntry::FeatureParam kEnableLazyLoadImageForAllInvisiblePage[] = {
    {"enabled_page_type", "all_invisible_page"}};
const FeatureEntry::FeatureParam kEnableLazyLoadImageForPrerenderPage[] = {
    {"enabled_page_type", "prerender_page"}};

const FeatureEntry::FeatureVariation
    kSearchSuggsetionPrerenderTypeVariations[] = {
        {"for all invisible page", kEnableLazyLoadImageForAllInvisiblePage,
         std::size(kEnableLazyLoadImageForAllInvisiblePage), nullptr},
        {"for prerendering page", kEnableLazyLoadImageForPrerenderPage,
         std::size(kEnableLazyLoadImageForPrerenderPage), nullptr}};

const FeatureEntry::FeatureParam kSoftNavigationHeuristicsBasic[] = {
    {"mode", "basic"}};
const FeatureEntry::FeatureParam
    kSoftNavigationHeuristicsAdvancedPaintAttribution[] = {
        {"mode", "advanced_paint_attribution"}};
const FeatureEntry::FeatureParam
    kSoftNavigationHeuristicsPrePaintBasedAttribution[] = {
        {"mode", "pre_paint_based_attriubution"}};

const FeatureEntry::FeatureVariation kSoftNavigationHeuristicsVariations[] = {
    {"Basic (default)", kSoftNavigationHeuristicsBasic,
     std::size(kSoftNavigationHeuristicsBasic), nullptr},
    {"Advanced Paint Attribution (Lazy Uncached Paint Walk)",
     kSoftNavigationHeuristicsAdvancedPaintAttribution,
     std::size(kSoftNavigationHeuristicsAdvancedPaintAttribution), nullptr},
    {"Advanced Paint Attribution (Eager Cached Pre-Paint Walk)",
     kSoftNavigationHeuristicsPrePaintBasedAttribution,
     std::size(kSoftNavigationHeuristicsPrePaintBasedAttribution), nullptr}};

const FeatureEntry::Choice kTopChromeTouchUiChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceAutomatic, switches::kTopChromeTouchUi,
     switches::kTopChromeTouchUiAuto},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kTopChromeTouchUi,
     switches::kTopChromeTouchUiDisabled},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kTopChromeTouchUi,
     switches::kTopChromeTouchUiEnabled}};

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kCctSignInPromptAlways[] = {
    {"cadence_day", "0"},
    {"show_limit", "10000"},
    {"user_act_count", "10000"},
    {"if_allowed_by_embedder", "true"},
    {"if_enabled_by_embedder", "true"}};
const FeatureEntry::FeatureParam kCctSignInTestOnly[] = {
    {"cadence_day", "0"},
    {"show_limit", "4"},
    {"user_act_count", "2"},
    {"if_allowed_by_embedder", "true"},
    {"if_enabled_by_embedder", "true"}};

const FeatureEntry::FeatureVariation kCctSignInPromptVariations[] = {
    {"always show", kCctSignInPromptAlways, std::size(kCctSignInPromptAlways),
     nullptr},
    {"for test", kCctSignInTestOnly, std::size(kCctSignInTestOnly), nullptr}};
#endif

#if BUILDFLAG(IS_CHROMEOS)

const FeatureEntry::FeatureParam kZinkEnableRecommended[] = {
    {"BorealisZinkGlDriverParam", "ZinkEnableRecommended"}};
const FeatureEntry::FeatureParam kZinkEnableAll[] = {
    {"BorealisZinkGlDriverParam", "ZinkEnableAll"}};

const FeatureEntry::FeatureVariation kBorealisZinkGlDriverVariations[] = {
    {"for recommended apps", kZinkEnableRecommended,
     std::size(kZinkEnableRecommended), nullptr},
    {"for all apps", kZinkEnableAll, std::size(kZinkEnableAll), nullptr}};

const char kArcEnableVirtioBlkForDataInternalName[] =
    "arc-enable-virtio-blk-for-data";

const char kProjectorServerSideSpeechRecognition[] =
    "enable-projector-server-side-speech-recognition";

const char kArcEnableAttestationFlag[] = "arc-enable-attestation";

#endif  // BUILDFLAG(IS_CHROMEOS)

const FeatureEntry::Choice kForceUIDirectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceDirectionLtr, switches::kForceUIDirection,
     switches::kForceDirectionLTR},
    {flag_descriptions::kForceDirectionRtl, switches::kForceUIDirection,
     switches::kForceDirectionRTL},
};

const FeatureEntry::Choice kForceTextDirectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceDirectionLtr, switches::kForceTextDirection,
     switches::kForceDirectionLTR},
    {flag_descriptions::kForceDirectionRtl, switches::kForceTextDirection,
     switches::kForceDirectionRTL},
};

const FeatureEntry::Choice kIpProtectionProxyOptOutChoices[] = {
    {flag_descriptions::kIpProtectionProxyOptOutChoiceDefault, "", ""},
    {flag_descriptions::kIpProtectionProxyOptOutChoiceOptOut,
     switches::kDisableIpProtectionProxy, ""},
};

#if BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::Choice kSchedulerConfigurationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kSchedulerConfigurationConservative,
     ash::switches::kSchedulerConfiguration,
     ash::switches::kSchedulerConfigurationConservative},
    {flag_descriptions::kSchedulerConfigurationPerformance,
     ash::switches::kSchedulerConfiguration,
     ash::switches::kSchedulerConfigurationPerformance},
};

const FeatureEntry::FeatureParam kDynamicSearchUpdateAnimationDuration_50[] = {
    {"search_result_translation_duration", "50"}};
const FeatureEntry::FeatureParam kDynamicSearchUpdateAnimationDuration_100[] = {
    {"search_result_translation_duration", "100"}};
const FeatureEntry::FeatureParam kDynamicSearchUpdateAnimationDuration_150[] = {
    {"search_result_translation_duration", "150"}};

const FeatureEntry::FeatureVariation kDynamicSearchUpdateAnimationVariations[] =
    {{"50ms", kDynamicSearchUpdateAnimationDuration_50,
      std::size(kDynamicSearchUpdateAnimationDuration_50), nullptr},
     {"100ms", kDynamicSearchUpdateAnimationDuration_100,
      std::size(kDynamicSearchUpdateAnimationDuration_100), nullptr},
     {"150ms", kDynamicSearchUpdateAnimationDuration_150,
      std::size(kDynamicSearchUpdateAnimationDuration_150), nullptr}};
#endif  // BUILDFLAG(IS_CHROMEOS)

const FeatureEntry::Choice kSiteIsolationOptOutChoices[] = {
    {flag_descriptions::kSiteIsolationOptOutChoiceDefault, "", ""},
    {flag_descriptions::kSiteIsolationOptOutChoiceOptOut,
     switches::kDisableSiteIsolation, ""},
};

const FeatureEntry::Choice kForceColorProfileChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceColorProfileSRGB,
     switches::kForceDisplayColorProfile, "srgb"},
    {flag_descriptions::kForceColorProfileP3,
     switches::kForceDisplayColorProfile, "display-p3-d65"},
    {flag_descriptions::kForceColorProfileRec2020,
     switches::kForceDisplayColorProfile, "rec2020"},
    {flag_descriptions::kForceColorProfileColorSpin,
     switches::kForceDisplayColorProfile, "color-spin-gamma24"},
    {flag_descriptions::kForceColorProfileSCRGBLinear,
     switches::kForceDisplayColorProfile, "scrgb-linear"},
    {flag_descriptions::kForceColorProfileHDR10,
     switches::kForceDisplayColorProfile, "hdr10"},
};

const FeatureEntry::Choice kMemlogModeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDisabled, "", ""},
    {flag_descriptions::kMemlogModeMinimal, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeMinimal},
    {flag_descriptions::kMemlogModeAll, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeAll},
    {flag_descriptions::kMemlogModeBrowser, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeBrowser},
    {flag_descriptions::kMemlogModeGpu, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeGpu},
    {flag_descriptions::kMemlogModeAllRenderers, heap_profiling::kMemlogMode,
     heap_profiling::kMemlogModeAllRenderers},
    {flag_descriptions::kMemlogModeRendererSampling,
     heap_profiling::kMemlogMode, heap_profiling::kMemlogModeRendererSampling},
};

const FeatureEntry::Choice kMemlogStackModeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMemlogStackModeNative,
     heap_profiling::kMemlogStackMode, heap_profiling::kMemlogStackModeNative},
    {flag_descriptions::kMemlogStackModeNativeWithThreadNames,
     heap_profiling::kMemlogStackMode,
     heap_profiling::kMemlogStackModeNativeWithThreadNames},
};

const FeatureEntry::Choice kMemlogSamplingRateChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMemlogSamplingRate10KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate10KB},
    {flag_descriptions::kMemlogSamplingRate50KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate50KB},
    {flag_descriptions::kMemlogSamplingRate100KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate100KB},
    {flag_descriptions::kMemlogSamplingRate500KB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate500KB},
    {flag_descriptions::kMemlogSamplingRate1MB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate1MB},
    {flag_descriptions::kMemlogSamplingRate5MB,
     heap_profiling::kMemlogSamplingRate,
     heap_profiling::kMemlogSamplingRate5MB},
};

const FeatureEntry::FeatureParam
    kOptimizationGuideOnDeviceModelBypassPerfParams[] = {
        {"compatible_on_device_performance_classes", "*"},
};
const FeatureEntry::FeatureVariation
    kOptimizationGuideOnDeviceModelVariations[] = {
        {"BypassPerfRequirement",
         kOptimizationGuideOnDeviceModelBypassPerfParams,
         std::size(kOptimizationGuideOnDeviceModelBypassPerfParams), nullptr},
};

const FeatureEntry::FeatureParam kTextSafetyClassifierNoRetractParams[] = {
    {"on_device_retract_unsafe_content", "false"},
};
const FeatureEntry::FeatureVariation kTextSafetyClassifierVariations[] = {
    {"Executes safety classifier but no retraction of output",
     kTextSafetyClassifierNoRetractParams,
     std::size(kTextSafetyClassifierNoRetractParams), nullptr},
};

const FeatureEntry::FeatureParam kPageActionsMigrationParams[] = {
    {"autofill_address", "true"},
    {"lens_overlay", "true"},
    {"translate", "true"},
    {"memory_saver", "true"},
    {"price_insights", "true"},
    {"offer_notification", "true"},
    {"intent_picker", "true"},
    {"file_system_access", "true"},
    {"zoom", "true"},
    {"pwa_install", "true"},
};
const FeatureEntry::FeatureVariation kPageActionsMigrationVariations[] = {
    {"with all migrated page actions enabled", kPageActionsMigrationParams,
     std::size(kPageActionsMigrationParams), nullptr},
};

const FeatureEntry::FeatureParam kPageContentAnnotationsContentParams[] = {
    {"annotate_title_instead_of_page_content", "false"},
    {"extract_related_searches", "true"},
    {"max_size_for_text_dump_in_bytes", "5120"},
    {"write_to_history_service", "true"},
};
const FeatureEntry::FeatureParam kPageContentAnnotationsTitleParams[] = {
    {"annotate_title_instead_of_page_content", "true"},
    {"extract_related_searches", "true"},
    {"write_to_history_service", "true"},
};
const FeatureEntry::FeatureParam
    kPageContentAnnotationsTimeoutDurationParams[] = {
        {"PageContentAnnotationBatchSizeTimeoutDuration", "0"},
};
const FeatureEntry::FeatureVariation kPageContentAnnotationsVariations[] = {
    {"All Annotations and Persistence on Content",
     kPageContentAnnotationsContentParams,
     std::size(kPageContentAnnotationsContentParams), nullptr},
    {"All Annotations and Persistence on Title",
     kPageContentAnnotationsTitleParams,
     std::size(kPageContentAnnotationsTitleParams), nullptr},
    {"Annotation timeout duration 0 seconds",
     kPageContentAnnotationsTimeoutDurationParams,
     std::size(kPageContentAnnotationsTimeoutDurationParams), nullptr}};

#if !BUILDFLAG(IS_ANDROID)
constexpr FeatureEntry::FeatureParam
    kHappinessTrackingSurveysForDesktopDemoWithoutAutoPrompt[] = {
        {"auto_prompt", "false"}};
constexpr FeatureEntry::FeatureVariation
    kHappinessTrackingSurveysForDesktopDemoVariations[] = {
        {"without Auto Prompt",
         kHappinessTrackingSurveysForDesktopDemoWithoutAutoPrompt,
         std::size(kHappinessTrackingSurveysForDesktopDemoWithoutAutoPrompt),
         nullptr}};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
const FeatureEntry::FeatureParam kHistoryEmbeddingsAtKeywordAcceleration[]{
    {"AtKeywordAcceleration", "true"},
};
const FeatureEntry::FeatureVariation kHistoryEmbeddingsVariations[] = {
    {"with AtKeywordAcceleration", kHistoryEmbeddingsAtKeywordAcceleration,
     std::size(kHistoryEmbeddingsAtKeywordAcceleration), nullptr},
};

#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

const FeatureEntry::FeatureParam kJourneysShowAllVisitsParams[] = {
    {"JourneysLocaleOrLanguageAllowlist", "*"},
    // To show all visits, set the number of visits above the fold to a very
    // high number.
    {"JourneysNumVisitsToAlwaysShowAboveTheFold", "200"},
};
const FeatureEntry::FeatureParam kJourneysAllLocalesParams[] = {
    {"JourneysLocaleOrLanguageAllowlist", "*"},
};
const FeatureEntry::FeatureVariation kJourneysVariations[] = {
    {"No 'Show More' - Show all visits", kJourneysShowAllVisitsParams,
     std::size(kJourneysShowAllVisitsParams), nullptr},
    {"All Supported Locales", kJourneysAllLocalesParams,
     std::size(kJourneysAllLocalesParams), nullptr},
};

const FeatureEntry::FeatureVariation
    kImageServiceOptimizationGuideSalientImagesVariations[] = {
        {"High Performance Canonicalization", nullptr, 0, "3362133"},
};

const FeatureEntry::FeatureVariation kRemotePageMetadataVariations[] = {
    {"High Performance Canonicalization", nullptr, 0, "3362133"},
};

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)

// A limited number of combinations of the rich autocompletion params.
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive1[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "1"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "1"}};
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive2[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "2"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "2"}};
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive3[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "3"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "3"}};
const FeatureEntry::FeatureParam kOmniboxRichAutocompletionAggressive4[] = {
    {"RichAutocompletionAutocompleteTitlesMinChar", "4"},
    {"RichAutocompletionAutocompleteShortcutTextMinChar", "4"}};

const FeatureEntry::FeatureVariation
    kOmniboxRichAutocompletionPromisingVariations[] = {
        {"Min input length 1 characters", kOmniboxRichAutocompletionAggressive1,
         std::size(kOmniboxRichAutocompletionAggressive1), nullptr},
        {"Min input length 2 characters", kOmniboxRichAutocompletionAggressive2,
         std::size(kOmniboxRichAutocompletionAggressive2), nullptr},
        {"Min input length 2 characters", kOmniboxRichAutocompletionAggressive2,
         std::size(kOmniboxRichAutocompletionAggressive2), nullptr},
        {"Min input length 3 characters", kOmniboxRichAutocompletionAggressive3,
         std::size(kOmniboxRichAutocompletionAggressive3), nullptr},
        {"Min input length 4 characters", kOmniboxRichAutocompletionAggressive4,
         std::size(kOmniboxRichAutocompletionAggressive4), nullptr},
};

const FeatureEntry::FeatureParam kOmniboxStarterPackExpansionPreProdUrl[] = {
    {"StarterPackGeminiUrlOverride", "https://gemini.google.com/corp/prompt"}};
const FeatureEntry::FeatureParam kOmniboxStarterPackExpansionStagingUrl[] = {
    {"StarterPackGeminiUrlOverride",
     "https://gemini.google.com/staging/prompt"}};
const FeatureEntry::FeatureVariation kOmniboxStarterPackExpansionVariations[] =
    {{"pre-prod url", kOmniboxStarterPackExpansionPreProdUrl,
      std::size(kOmniboxStarterPackExpansionPreProdUrl), nullptr},
     {"staging url", kOmniboxStarterPackExpansionStagingUrl,
      std::size(kOmniboxStarterPackExpansionStagingUrl), nullptr}};

const FeatureEntry::FeatureParam kOmniboxSearchAggregatorProdParams[] = {
    {"name", "Agentspace"},
    {"shortcut", "agentspace"},
    {"search_url",
     "https://vertexaisearch.cloud.google.com/home/cid/"
     "8884f744-aae1-4fbc-8a64-b8bf7cbf270e?q={searchTerms}"},
    {"suggest_url",
     "https://discoveryengine.googleapis.com/v1alpha/projects/862721868538/"
     "locations/global/collections/default_collection/engines/"
     "teamfood-v11_1720671063545/completionConfig:completeQuery"}};
const FeatureEntry::FeatureParam kOmniboxSearchAggregatorStagingParams[] = {
    {"name", "Agentspace (staging)"},
    {"shortcut", "agentspace"},
    {"icon_url", "https://gstatic.com/vertexaisearch/favicon.png"},
    {"search_url",
     "https://vertexaisearch.cloud.google.com/home/cid/"
     "3abd7045-7845-4f83-b204-e39fcbca3494?q={searchTerms}&mods=widget_staging_"
     "api_mod"},
    {"suggest_url",
     "https://staging-discoveryengine.sandbox.googleapis.com/v1alpha/projects/"
     "862721868538/locations/global/collections/default_collection/engines/"
     "teamfood-v11/completionConfig:completeQuery"}};
const FeatureEntry::FeatureParam kOmniboxSearchAggregatorDemoParams[] = {
    {"name", "Neuravibe"},
    {"shortcut", "neura"},
    {"icon_url", "https://gstatic.com/vertexaisearch/favicon.png"},
    {"search_url",
     "https://vertexaisearch.cloud.google.com/home/cid/"
     "8e21c7cd-cbfe-4162-baf4-3381fc43546e?q={searchTerms}"},
    {"suggest_url",
     "https://discoveryengine.googleapis.com/v1alpha/projects/977834784893/"
     "locations/global/collections/default_collection/engines/"
     "neuravibeenterprisesearch_1732204320742/completionConfig:completeQuery"}};
const FeatureEntry::FeatureVariation kOmniboxSearchAggregatorVariations[] = {
    {"prod", kOmniboxSearchAggregatorProdParams,
     std::size(kOmniboxSearchAggregatorProdParams), nullptr},
    {"staging", kOmniboxSearchAggregatorStagingParams,
     std::size(kOmniboxSearchAggregatorStagingParams), nullptr},
    {"demo", kOmniboxSearchAggregatorDemoParams,
     std::size(kOmniboxSearchAggregatorDemoParams), nullptr}};

const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusTwoDayWindow[] = {
    {"OnFocusMostVisitedRecencyWindow", "1"},
};
const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusThreeDayWindow[] =
    {
        {"OnFocusMostVisitedRecencyWindow", "2"},
};
const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusOneWeekWindow[] =
    {
        {"OnFocusMostVisitedRecencyWindow", "6"},
};
const FeatureEntry::FeatureParam kOmniboxUrlSuggestionsOnFocusTwoWeekWindow[] =
    {
        {"OnFocusMostVisitedRecencyWindow", "13"},
};
const FeatureEntry::FeatureVariation kOmniboxUrlSuggestionsOnFocusVariations[] =
    {
        {"- Two day window", kOmniboxUrlSuggestionsOnFocusTwoDayWindow,
         std::size(kOmniboxUrlSuggestionsOnFocusTwoDayWindow), nullptr},
        {"- Three day window", kOmniboxUrlSuggestionsOnFocusThreeDayWindow,
         std::size(kOmniboxUrlSuggestionsOnFocusThreeDayWindow), nullptr},
        {"- One week window", kOmniboxUrlSuggestionsOnFocusOneWeekWindow,
         std::size(kOmniboxUrlSuggestionsOnFocusOneWeekWindow), nullptr},
        {"- Two week window", kOmniboxUrlSuggestionsOnFocusTwoWeekWindow,
         std::size(kOmniboxUrlSuggestionsOnFocusTwoWeekWindow), nullptr},
};

const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax8[] = {
    {"OmniboxZpsMaxSuggestions", "8"},
    {"OmniboxZpsMaxSearchSuggestions", "4"},
    {"OmniboxZpsMaxUrlSuggestions", "4"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax4[] = {
    {"OmniboxZpsMaxSuggestions", "4"},
    {"OmniboxZpsMaxSearchSuggestions", "2"},
    {"OmniboxZpsMaxUrlSuggestions", "2"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax2TwoZero[] = {
    {"OmniboxZpsMaxSuggestions", "2"},
    {"OmniboxZpsMaxSearchSuggestions", "2"},
    {"OmniboxZpsMaxUrlSuggestions", "0"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax3ThreeZero[] = {
    {"OmniboxZpsMaxSuggestions", "3"},
    {"OmniboxZpsMaxSearchSuggestions", "3"},
    {"OmniboxZpsMaxUrlSuggestions", "0"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax4FourZero[] = {
    {"OmniboxZpsMaxSuggestions", "4"},
    {"OmniboxZpsMaxSearchSuggestions", "4"},
    {"OmniboxZpsMaxUrlSuggestions", "0"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax5FourOne[] = {
    {"OmniboxZpsMaxSuggestions", "5"},
    {"OmniboxZpsMaxSearchSuggestions", "4"},
    {"OmniboxZpsMaxUrlSuggestions", "1"},
};
const FeatureEntry::FeatureParam kOmniboxZpsSuggestionLimitMax5ThreeTwo[] = {
    {"OmniboxZpsMaxSuggestions", "5"},
    {"OmniboxZpsMaxSearchSuggestions", "3"},
    {"OmniboxZpsMaxUrlSuggestions", "2"},
};
const FeatureEntry::FeatureVariation kOmniboxZpsSuggestionLimitVariations[] = {
    {"- Max 8 Suggestions (4 search, 4 url)", kOmniboxZpsSuggestionLimitMax8,
     std::size(kOmniboxZpsSuggestionLimitMax8), nullptr},
    {"- Max 4 Suggestions (2 search, 2 url)", kOmniboxZpsSuggestionLimitMax4,
     std::size(kOmniboxZpsSuggestionLimitMax4), nullptr},
    {"- Max 2 Suggestions (2 search, 0 url)",
     kOmniboxZpsSuggestionLimitMax2TwoZero,
     std::size(kOmniboxZpsSuggestionLimitMax2TwoZero), nullptr},
    {"- Max 3 Suggestions (3 search, 0 url)",
     kOmniboxZpsSuggestionLimitMax3ThreeZero,
     std::size(kOmniboxZpsSuggestionLimitMax3ThreeZero), nullptr},
    {"- Max 4 Suggestions (4 search, 0 url)",
     kOmniboxZpsSuggestionLimitMax4FourZero,
     std::size(kOmniboxZpsSuggestionLimitMax4FourZero), nullptr},
    {"- Max 5 Suggestions (4 search, 1 url)",
     kOmniboxZpsSuggestionLimitMax5FourOne,
     std::size(kOmniboxZpsSuggestionLimitMax5FourOne), nullptr},
    {"- Max 5 Suggestions (3 search, 2 url)",
     kOmniboxZpsSuggestionLimitMax5ThreeTwo,
     std::size(kOmniboxZpsSuggestionLimitMax5ThreeTwo), nullptr},
};

const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit0[] = {
        {"Limit", "0"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit1[] = {
        {"Limit", "1"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit2[] = {
        {"Limit", "2"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit3[] = {
        {"Limit", "3"},
};
const FeatureEntry::FeatureParam
    kOmniboxContextualSearchOnFocusSuggestionsLimit4[] = {
        {"Limit", "4"},
};
const FeatureEntry::FeatureVariation
    kOmniboxContextualSearchOnFocusSuggestionsVariations[] = {
        {"- Limit 0", kOmniboxContextualSearchOnFocusSuggestionsLimit0,
         std::size(kOmniboxContextualSearchOnFocusSuggestionsLimit0), nullptr},
        {"- Limit 1", kOmniboxContextualSearchOnFocusSuggestionsLimit1,
         std::size(kOmniboxContextualSearchOnFocusSuggestionsLimit1), nullptr},
        {"- Limit 2", kOmniboxContextualSearchOnFocusSuggestionsLimit2,
         std::size(kOmniboxContextualSearchOnFocusSuggestionsLimit2), nullptr},
        {"- Limit 3", kOmniboxContextualSearchOnFocusSuggestionsLimit3,
         std::size(kOmniboxContextualSearchOnFocusSuggestionsLimit3), nullptr},
        {"- Limit 4", kOmniboxContextualSearchOnFocusSuggestionsLimit4,
         std::size(kOmniboxContextualSearchOnFocusSuggestionsLimit4), nullptr},
};

const FeatureEntry::FeatureParam
    kContextualSuggestionsAblateOthersWhenPresentAblateAll[] = {
        {"AblateSearchOnly", "false"},
};

const FeatureEntry::FeatureParam
    kContextualSuggestionsAblateOthersWhenPresentAblateSearchOnly[] = {
        {"AblateSearchOnly", "true"},
};

const FeatureEntry::FeatureParam
    kContextualSuggestionsAblateOthersWhenPresentAblateUrlOnly[] = {
        {"AblateUrlOnly", "true"},
};

const FeatureEntry::FeatureVariation
    kContextualSuggestionsAblateOthersWhenPresentVariations[] = {
        {"- Ablate all", kContextualSuggestionsAblateOthersWhenPresentAblateAll,
         std::size(kContextualSuggestionsAblateOthersWhenPresentAblateAll),
         nullptr},
        {"- Ablate search only",
         kContextualSuggestionsAblateOthersWhenPresentAblateSearchOnly,
         std::size(
             kContextualSuggestionsAblateOthersWhenPresentAblateSearchOnly),
         nullptr},
        {"- Ablate URL only",
         kContextualSuggestionsAblateOthersWhenPresentAblateUrlOnly,
         std::size(kContextualSuggestionsAblateOthersWhenPresentAblateUrlOnly),
         nullptr},
};

const FeatureEntry::FeatureParam kOmniboxToolbeltAggressive[] = {
    {"KeepToolbeltAfterInput", "true"},
    {"AlwaysIncludeLensAction", "false"},
    {"ShowLensActionOnNonNtp", "true"},
    {"ShowLensActionOnNtp", "true"},
    {"ShowAiModeActionOnNonNtp", "true"},
    {"ShowAiModeActionOnNtp", "true"},
    {"ShowHistoryActionOnNonNtp", "true"},
    {"ShowHistoryActionOnNtp", "true"},
    {"ShowBookmarksActionOnNonNtp", "true"},
    {"ShowBookmarksActionOnNtp", "true"},
    {"ShowTabsActionOnNonNtp", "true"},
    {"ShowTabsActionOnNtp", "true"},
};
const FeatureEntry::FeatureVariation kOmniboxToolbeltVariations[] = {
    {"Aggressive - zero & typed inputs; all actions.",
     kOmniboxToolbeltAggressive, std::size(kOmniboxToolbeltAggressive),
     nullptr}};

#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

const FeatureEntry::FeatureParam kOmniboxMlUrlScoringEnabledWithFixes[] = {
    {"enable_scoring_signals_annotators_for_ml_scoring", "true"},
    {"MlUrlScoringShortcutDocumentSignals", "true"},
};
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringUnlimitedNumCandidates[] =
    {
        {"MlUrlScoringUnlimitedNumCandidates", "true"},
        {"enable_scoring_signals_annotators_for_ml_scoring", "true"},
        {"MlUrlScoringShortcutDocumentSignals", "true"},
};
// Sets Bookmark(1), History Quick(4), History URL(8), Shortcuts(64),
// Document(512), and History Fuzzy(65536) providers max matches to 10.
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringMaxMatchesByProvider10[] =
    {
        {"MlUrlScoringMaxMatchesByProvider",
         "1:10,4:10,8:10,64:10,512:10,65536:10"},
        {"enable_scoring_signals_annotators_for_ml_scoring", "true"},
        {"MlUrlScoringShortcutDocumentSignals", "true"},
};
// Enables ML scoring for Search suggestions.
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringWithSearches[] = {
    {"MlUrlScoring_EnableMlScoringForSearches", "true"},
};
// Enables ML scoring for verbatim URL suggestions.
const FeatureEntry::FeatureParam kOmniboxMlUrlScoringWithVerbatimURLs[] = {
    {"MlUrlScoring_EnableMlScoringForVerbatimUrls", "true"},
};
// Enables ML scoring for both Search and verbatim URL suggestions.
const FeatureEntry::FeatureParam
    kOmniboxMlUrlScoringWithSearchesAndVerbatimURLs[] = {
        {"MlUrlScoring_EnableMlScoringForSearches", "true"},
        {"MlUrlScoring_EnableMlScoringForVerbatimUrls", "true"},
};

const FeatureEntry::FeatureVariation kOmniboxMlUrlScoringVariations[] = {
    {"Enabled with fixes", kOmniboxMlUrlScoringEnabledWithFixes,
     std::size(kOmniboxMlUrlScoringEnabledWithFixes), nullptr},
    {"unlimited suggestion candidates",
     kOmniboxMlUrlScoringUnlimitedNumCandidates,
     std::size(kOmniboxMlUrlScoringUnlimitedNumCandidates), nullptr},
    {"Increase provider max limit to 10",
     kOmniboxMlUrlScoringMaxMatchesByProvider10,
     std::size(kOmniboxMlUrlScoringMaxMatchesByProvider10), nullptr},
    {"with scoring of Search suggestions", kOmniboxMlUrlScoringWithSearches,
     std::size(kOmniboxMlUrlScoringWithSearches), nullptr},
    {"with scoring of verbatim URL suggestions",
     kOmniboxMlUrlScoringWithVerbatimURLs,
     std::size(kOmniboxMlUrlScoringWithVerbatimURLs), nullptr},
    {"with scoring of Search & verbatim URL suggestions",
     kOmniboxMlUrlScoringWithSearchesAndVerbatimURLs,
     std::size(kOmniboxMlUrlScoringWithSearchesAndVerbatimURLs), nullptr},
};

const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingAdjustedBy0[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1300;0.14,1398;1,1422"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1400"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingDemotedBy50[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1250;0.14,1348;1,1422"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1350"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingPromotedBy50[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1350;0.14,1448;1,1472"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1450"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingPromotedBy100[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,550;0.018,1400;0.14,1498;1,1522"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1500"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};
const FeatureEntry::FeatureParam
    kMlUrlPiecewiseMappedSearchBlendingMobileMapping[] = {
        {"MlUrlPiecewiseMappedSearchBlending", "true"},
        {"MlUrlPiecewiseMappedSearchBlending_BreakPoints",
         "0,590;0.006,790;0.082,1290;0.443,1360;0.464,1400;0.987,1425;1,1530"},
        {"MlUrlPiecewiseMappedSearchBlending_GroupingThreshold", "1340"},
        {"MlUrlPiecewiseMappedSearchBlending_RelevanceBias", "0"}};

const FeatureEntry::FeatureVariation
    kMlUrlPiecewiseMappedSearchBlendingVariations[] = {
        {"adjusted by 0", kMlUrlPiecewiseMappedSearchBlendingAdjustedBy0,
         std::size(kMlUrlPiecewiseMappedSearchBlendingAdjustedBy0), nullptr},
        {"demoted by 50", kMlUrlPiecewiseMappedSearchBlendingDemotedBy50,
         std::size(kMlUrlPiecewiseMappedSearchBlendingDemotedBy50), nullptr},
        {"promoted by 50", kMlUrlPiecewiseMappedSearchBlendingPromotedBy50,
         std::size(kMlUrlPiecewiseMappedSearchBlendingPromotedBy50), nullptr},
        {"promoted by 100", kMlUrlPiecewiseMappedSearchBlendingPromotedBy100,
         std::size(kMlUrlPiecewiseMappedSearchBlendingPromotedBy100), nullptr},
        {"mobile mapping", kMlUrlPiecewiseMappedSearchBlendingMobileMapping,
         std::size(kMlUrlPiecewiseMappedSearchBlendingMobileMapping), nullptr},
};

const FeatureEntry::FeatureParam kMlUrlSearchBlendingStable[] = {
    {"MlUrlSearchBlending_StableSearchBlending", "true"},
    {"MlUrlSearchBlending_MappedSearchBlending", "false"},
};
const FeatureEntry::FeatureParam kMlUrlSearchBlendingMappedConservativeUrls[] =
    {
        {"MlUrlSearchBlending_StableSearchBlending", "false"},
        {"MlUrlSearchBlending_MappedSearchBlending", "true"},
        {"MlUrlSearchBlending_MappedSearchBlendingMin", "0"},
        {"MlUrlSearchBlending_MappedSearchBlendingMax", "2000"},
        {"MlUrlSearchBlending_MappedSearchBlendingGroupingThreshold", "1000"},
};
const FeatureEntry::FeatureParam kMlUrlSearchBlendingMappedModerateUrls[] = {
    {"MlUrlSearchBlending_StableSearchBlending", "false"},
    {"MlUrlSearchBlending_MappedSearchBlending", "true"},
};
const FeatureEntry::FeatureParam kMlUrlSearchBlendingMappedAggressiveUrls[] = {
    {"MlUrlSearchBlending_StableSearchBlending", "false"},
    {"MlUrlSearchBlending_MappedSearchBlending", "true"},
    {"MlUrlSearchBlending_MappedSearchBlendingMin", "1000"},
    {"MlUrlSearchBlending_MappedSearchBlendingMax", "4000"},
    {"MlUrlSearchBlending_MappedSearchBlendingGroupingThreshold", "1500"},
};

const FeatureEntry::FeatureVariation kMlUrlSearchBlendingVariations[] = {
    {"Stable", kMlUrlSearchBlendingStable,
     std::size(kMlUrlSearchBlendingStable), nullptr},
    {"Mapped conservative urls", kMlUrlSearchBlendingMappedConservativeUrls,
     std::size(kMlUrlSearchBlendingMappedConservativeUrls), nullptr},
    {"Mapped moderate urls", kMlUrlSearchBlendingMappedModerateUrls,
     std::size(kMlUrlSearchBlendingMappedModerateUrls), nullptr},
    {"Mapped aggressive urls", kMlUrlSearchBlendingMappedAggressiveUrls,
     std::size(kMlUrlSearchBlendingMappedAggressiveUrls), nullptr},
};

constexpr FeatureEntry::FeatureParam
    kMostVitedTilesNewScoring_DecayStaircaseCap10[] = {
        {history::kMvtScoringParamRecencyFactor.name,
         history::kMvtScoringParamRecencyFactor_DecayStaircase},
        {history::kMvtScoringParamDailyVisitCountCap.name, "10"},
};
constexpr FeatureEntry::FeatureParam kMostVitedTilesNewScoring_DecayCap1[] = {
    {history::kMvtScoringParamRecencyFactor.name,
     history::kMvtScoringParamRecencyFactor_Decay},
    // exp(-1.0 / 11).
    {history::kMvtScoringParamDecayPerDay.name, "0.9131007162822623"},
    {history::kMvtScoringParamDailyVisitCountCap.name, "1"},
};
constexpr FeatureEntry::FeatureVariation
    kMostVisitedTilesNewScoringVariations[] = {
        {"Decay Staircase, Cap 10",
         kMostVitedTilesNewScoring_DecayStaircaseCap10,
         std::size(kMostVitedTilesNewScoring_DecayStaircaseCap10), nullptr},
        {"Decay, Cap 1", kMostVitedTilesNewScoring_DecayCap1,
         std::size(kMostVitedTilesNewScoring_DecayCap1), nullptr},
};

const FeatureEntry::FeatureVariation kUrlScoringModelVariations[] = {
    {"Small model (desktop)", nullptr, 0, nullptr},
    {"Full model (desktop)", nullptr, 0, "3380045"},
    {"Small model (ios)", nullptr, 0, "3379590"},
    {"Full model (ios)", nullptr, 0, "3380197"},
    {"Small model (android)", nullptr, 0, "3381543"},
    {"Full model (android)", nullptr, 0, "3381544"},
};

const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRun[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "300"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "true"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRequest[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "300"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "false"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRun[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "600"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "true"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRequest[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "600"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "false"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRun[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "900"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "true"},
};
const FeatureEntry::FeatureParam
    kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRequest[] = {
        {"ZeroSuggestPrefetchDebounceDelay", "900"},
        {"ZeroSuggestPrefetchDebounceFromLastRun", "false"},
};

const FeatureEntry::FeatureVariation
    kOmniboxZeroSuggestPrefetchDebouncingVariations[] = {
        {"Minimal debouncing relative to last run",
         kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRun,
         std::size(kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRun),
         nullptr},
        {"Minimal debouncing relative to last request",
         kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRequest,
         std::size(kOmniboxZeroSuggestPrefetchDebouncingMinimalFromLastRequest),
         nullptr},
        {"Moderate debouncing relative to last run",
         kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRun,
         std::size(kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRun),
         nullptr},
        {"Moderate debouncing relative to last request",
         kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRequest,
         std::size(
             kOmniboxZeroSuggestPrefetchDebouncingModerateFromLastRequest),
         nullptr},
        {"Aggressive debouncing relative to last run",
         kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRun,
         std::size(kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRun),
         nullptr},
        {"Aggressive debouncing relative to last request",
         kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRequest,
         std::size(
             kOmniboxZeroSuggestPrefetchDebouncingAggressiveFromLastRequest),
         nullptr},
};

#if BUILDFLAG(IS_ANDROID)
constexpr FeatureEntry::FeatureParam kOmniboxAnswerActionsCounterfactual[] = {
    {OmniboxFieldTrial::kAnswerActionsCounterfactual.name, "true"}};
constexpr FeatureEntry::FeatureParam kOmniboxAnswerActionsTreatment1[] = {
    {OmniboxFieldTrial::kAnswerActionsShowAboveKeyboard.name, "true"},
    {OmniboxFieldTrial::kAnswerActionsShowIfUrlsPresent.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowRichCard.name, "false"}};

constexpr FeatureEntry::FeatureParam kOmniboxAnswerActionsTreatment2[] = {
    {OmniboxFieldTrial::kAnswerActionsShowAboveKeyboard.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowIfUrlsPresent.name, "true"},
    {OmniboxFieldTrial::kAnswerActionsShowRichCard.name, "false"}};

constexpr FeatureEntry::FeatureParam kOmniboxAnswerActionsTreatment3[] = {
    {OmniboxFieldTrial::kAnswerActionsShowAboveKeyboard.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowIfUrlsPresent.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowRichCard.name, "false"}};

constexpr FeatureEntry::FeatureParam kOmniboxAnswerActionsTreatment4[] = {
    {OmniboxFieldTrial::kAnswerActionsShowAboveKeyboard.name, "true"},
    {OmniboxFieldTrial::kAnswerActionsShowIfUrlsPresent.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowRichCard.name, "true"}};

constexpr FeatureEntry::FeatureParam kOmniboxAnswerActionsTreatment5[] = {
    {OmniboxFieldTrial::kAnswerActionsShowAboveKeyboard.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowIfUrlsPresent.name, "false"},
    {OmniboxFieldTrial::kAnswerActionsShowRichCard.name, "true"}};

constexpr FeatureEntry::FeatureVariation kOmniboxAnswerActionsVariants[] = {
    {"Counterfactual: fetch without rendering ",
     kOmniboxAnswerActionsCounterfactual,
     std::size(kOmniboxAnswerActionsCounterfactual), "t3379046"},
    {"T1: Show chips above keyboard when there are no url matches",
     kOmniboxAnswerActionsTreatment1,
     std::size(kOmniboxAnswerActionsTreatment1), "t3379047"},
    {"T2: Show chips at position 0", kOmniboxAnswerActionsTreatment2,
     std::size(kOmniboxAnswerActionsTreatment2), "t3379048"},
    {"T3: Show chips at position 0 when there are no url matches",
     kOmniboxAnswerActionsTreatment3,
     std::size(kOmniboxAnswerActionsTreatment3), "t3379049"},
    {"T4: Show rich card above keyboard when there are no url matches",
     kOmniboxAnswerActionsTreatment4,
     std::size(kOmniboxAnswerActionsTreatment4), "t3379050"},
    {"T5: Show rich card at position 0 when there are no url matches",
     kOmniboxAnswerActionsTreatment5,
     std::size(kOmniboxAnswerActionsTreatment5), "t3379051"},
};

constexpr FeatureEntry::FeatureParam kOmniboxDiagInputConnection[]{
    {OmniboxFieldTrial::kAndroidDiagInputConnection.name, "true"}};

constexpr FeatureEntry::FeatureVariation kOmniboxDiagnosticsAndroidVaiants[] = {
    {"- InputConnection", kOmniboxDiagInputConnection,
     std::size(kOmniboxDiagInputConnection), nullptr}};

// Omnibox Mobile Parity Update -->
const FeatureEntry::FeatureParam kOmniboxMobileParityRetrieveTrueFavicon[] = {
    {OmniboxFieldTrial::kMobileParityEnableFeedForGoogleOnly.name, "false"},
    {OmniboxFieldTrial::kMobileParityRetrieveTrueFavicon.name, "true"}};

const FeatureEntry::FeatureParam kOmniboxMobileParityEnableFeedForGoogleOnly[] =
    {{OmniboxFieldTrial::kMobileParityEnableFeedForGoogleOnly.name, "true"},
     {OmniboxFieldTrial::kMobileParityRetrieveTrueFavicon.name, "false"}};

const FeatureEntry::FeatureParam kOmniboxMobileParityEnableEverything[] = {
    {OmniboxFieldTrial::kMobileParityEnableFeedForGoogleOnly.name, "true"},
    {OmniboxFieldTrial::kMobileParityRetrieveTrueFavicon.name, "true"}};

const FeatureEntry::FeatureVariation kOmniboxMobileParityVariants[] = {
    {"with True Favicon", kOmniboxMobileParityRetrieveTrueFavicon,
     std::size(kOmniboxMobileParityRetrieveTrueFavicon)},
    {"with Feed only for Google", kOmniboxMobileParityEnableFeedForGoogleOnly,
     std::size(kOmniboxMobileParityEnableFeedForGoogleOnly)},
    {"everything", kOmniboxMobileParityEnableEverything,
     std::size(kOmniboxMobileParityEnableEverything)},
};
// <-- Omnibox Mobile Parity Update

#endif  // BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kMaxZeroSuggestMatches5[] = {
    {"MaxZeroSuggestMatches", "5"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches6[] = {
    {"MaxZeroSuggestMatches", "6"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches7[] = {
    {"MaxZeroSuggestMatches", "7"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches8[] = {
    {"MaxZeroSuggestMatches", "8"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches9[] = {
    {"MaxZeroSuggestMatches", "9"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches10[] = {
    {"MaxZeroSuggestMatches", "10"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches11[] = {
    {"MaxZeroSuggestMatches", "11"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches12[] = {
    {"MaxZeroSuggestMatches", "12"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches13[] = {
    {"MaxZeroSuggestMatches", "13"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches14[] = {
    {"MaxZeroSuggestMatches", "14"}};
const FeatureEntry::FeatureParam kMaxZeroSuggestMatches15[] = {
    {"MaxZeroSuggestMatches", "15"}};

const FeatureEntry::FeatureVariation kMaxZeroSuggestMatchesVariations[] = {
    {"5", kMaxZeroSuggestMatches5, std::size(kMaxZeroSuggestMatches5), nullptr},
    {"6", kMaxZeroSuggestMatches6, std::size(kMaxZeroSuggestMatches6), nullptr},
    {"7", kMaxZeroSuggestMatches7, std::size(kMaxZeroSuggestMatches7), nullptr},
    {"8", kMaxZeroSuggestMatches8, std::size(kMaxZeroSuggestMatches8), nullptr},
    {"9", kMaxZeroSuggestMatches9, std::size(kMaxZeroSuggestMatches9), nullptr},
    {"10", kMaxZeroSuggestMatches10, std::size(kMaxZeroSuggestMatches10),
     nullptr},
    {"11", kMaxZeroSuggestMatches11, std::size(kMaxZeroSuggestMatches11),
     nullptr},
    {"12", kMaxZeroSuggestMatches12, std::size(kMaxZeroSuggestMatches12),
     nullptr},
    {"13", kMaxZeroSuggestMatches13, std::size(kMaxZeroSuggestMatches13),
     nullptr},
    {"14", kMaxZeroSuggestMatches14, std::size(kMaxZeroSuggestMatches14),
     nullptr},
    {"15", kMaxZeroSuggestMatches15, std::size(kMaxZeroSuggestMatches15),
     nullptr}};

const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches3[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "3"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches4[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "4"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches5[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "5"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches6[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "6"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches7[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "7"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches8[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "8"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches9[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "9"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches10[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "10"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches12[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "12"}};

const FeatureEntry::FeatureVariation
    kOmniboxUIMaxAutocompleteMatchesVariations[] = {
        {"3 matches", kOmniboxUIMaxAutocompleteMatches3,
         std::size(kOmniboxUIMaxAutocompleteMatches3), nullptr},
        {"4 matches", kOmniboxUIMaxAutocompleteMatches4,
         std::size(kOmniboxUIMaxAutocompleteMatches4), nullptr},
        {"5 matches", kOmniboxUIMaxAutocompleteMatches5,
         std::size(kOmniboxUIMaxAutocompleteMatches5), nullptr},
        {"6 matches", kOmniboxUIMaxAutocompleteMatches6,
         std::size(kOmniboxUIMaxAutocompleteMatches6), nullptr},
        {"7 matches", kOmniboxUIMaxAutocompleteMatches7,
         std::size(kOmniboxUIMaxAutocompleteMatches7), nullptr},
        {"8 matches", kOmniboxUIMaxAutocompleteMatches8,
         std::size(kOmniboxUIMaxAutocompleteMatches8), nullptr},
        {"9 matches", kOmniboxUIMaxAutocompleteMatches9,
         std::size(kOmniboxUIMaxAutocompleteMatches9), nullptr},
        {"10 matches", kOmniboxUIMaxAutocompleteMatches10,
         std::size(kOmniboxUIMaxAutocompleteMatches10), nullptr},
        {"12 matches", kOmniboxUIMaxAutocompleteMatches12,
         std::size(kOmniboxUIMaxAutocompleteMatches12), nullptr}};

const FeatureEntry::FeatureParam kOmniboxMaxURLMatches2[] = {
    {OmniboxFieldTrial::kOmniboxMaxURLMatchesParam, "2"}};
const FeatureEntry::FeatureParam kOmniboxMaxURLMatches3[] = {
    {OmniboxFieldTrial::kOmniboxMaxURLMatchesParam, "3"}};
const FeatureEntry::FeatureParam kOmniboxMaxURLMatches4[] = {
    {OmniboxFieldTrial::kOmniboxMaxURLMatchesParam, "4"}};
const FeatureEntry::FeatureParam kOmniboxMaxURLMatches5[] = {
    {OmniboxFieldTrial::kOmniboxMaxURLMatchesParam, "5"}};
const FeatureEntry::FeatureParam kOmniboxMaxURLMatches6[] = {
    {OmniboxFieldTrial::kOmniboxMaxURLMatchesParam, "6"}};

const FeatureEntry::FeatureVariation kOmniboxMaxURLMatchesVariations[] = {
    {"2 matches", kOmniboxMaxURLMatches2, std::size(kOmniboxMaxURLMatches2),
     nullptr},
    {"3 matches", kOmniboxMaxURLMatches3, std::size(kOmniboxMaxURLMatches3),
     nullptr},
    {"4 matches", kOmniboxMaxURLMatches4, std::size(kOmniboxMaxURLMatches4),
     nullptr},
    {"5 matches", kOmniboxMaxURLMatches5, std::size(kOmniboxMaxURLMatches5),
     nullptr},
    {"6 matches", kOmniboxMaxURLMatches6, std::size(kOmniboxMaxURLMatches6),
     nullptr}};

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kOmniboxMiaZpsEnabledWithHistoryAblation[] = {
    {OmniboxFieldTrial::kSuppressPsuggestBackfillWithMIAParam, "true"}};
const FeatureEntry::FeatureVariation kOmniboxMiaZpsVariations[] = {
    {"with History Ablation", kOmniboxMiaZpsEnabledWithHistoryAblation,
     std::size(kOmniboxMiaZpsEnabledWithHistoryAblation), nullptr}};
#endif

const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete90[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "0"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "9"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete91[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "1"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "9"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete92[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "2"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "9"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete100[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "0"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "10"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete101[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "1"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "10"}};
const FeatureEntry::FeatureParam kOmniboxDynamicMaxAutocomplete102[] = {
    {"OmniboxDynamicMaxAutocompleteUrlCutoff", "2"},
    {"OmniboxDynamicMaxAutocompleteIncreasedLimit", "10"}};

const FeatureEntry::FeatureVariation
    kOmniboxDynamicMaxAutocompleteVariations[] = {
        {"9 suggestions if 0 or fewer URLs", kOmniboxDynamicMaxAutocomplete90,
         std::size(kOmniboxDynamicMaxAutocomplete90), nullptr},
        {"9 suggestions if 1 or fewer URLs", kOmniboxDynamicMaxAutocomplete91,
         std::size(kOmniboxDynamicMaxAutocomplete91), nullptr},
        {"9 suggestions if 2 or fewer URLs", kOmniboxDynamicMaxAutocomplete92,
         std::size(kOmniboxDynamicMaxAutocomplete92), nullptr},
        {"10 suggestions if 0 or fewer URLs", kOmniboxDynamicMaxAutocomplete100,
         std::size(kOmniboxDynamicMaxAutocomplete100), nullptr},
        {"10 suggestions if 1 or fewer URLs", kOmniboxDynamicMaxAutocomplete101,
         std::size(kOmniboxDynamicMaxAutocomplete101), nullptr},
        {"10 suggestions if 2 or fewer URLs", kOmniboxDynamicMaxAutocomplete102,
         std::size(kOmniboxDynamicMaxAutocomplete102), nullptr}};

const FeatureEntry::FeatureParam kRepeatableQueries_6Searches_90Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "6"},
};
const FeatureEntry::FeatureParam kRepeatableQueries_12Searches_90Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "12"},
};
const FeatureEntry::FeatureParam kRepeatableQueries_6Searches_7Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "6"},
    {"RepeatableQueriesMaxAgeDays", "7"},
};
const FeatureEntry::FeatureParam kRepeatableQueries_12Searches_7Days[] = {
    {"RepeatableQueriesIgnoreDuplicateVisits", "true"},
    {"RepeatableQueriesMinVisitCount", "12"},
    {"RepeatableQueriesMaxAgeDays", "7"},
};

const FeatureEntry::FeatureVariation kOrganicRepeatableQueriesVariations[] = {
    {"6+ uses, once in last 90d", kRepeatableQueries_6Searches_90Days,
     std::size(kRepeatableQueries_6Searches_90Days), nullptr},
    {"12+ uses, once in last 90d", kRepeatableQueries_12Searches_90Days,
     std::size(kRepeatableQueries_12Searches_90Days), nullptr},
    {"6+ uses, once in last 7d", kRepeatableQueries_6Searches_7Days,
     std::size(kRepeatableQueries_6Searches_7Days), nullptr},
    {"12+ uses, once in last 7d", kRepeatableQueries_12Searches_7Days,
     std::size(kRepeatableQueries_12Searches_7Days), nullptr},
};

const FeatureEntry::FeatureParam kNtpZps0RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "0"}};
const FeatureEntry::FeatureParam kNtpZps5RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "5"}};
const FeatureEntry::FeatureParam kNtpZps10RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "10"}};
const FeatureEntry::FeatureParam kNtpZps15RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsRecentSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumNtpZpsRecentSearches[] = {
    {"No recents", kNtpZps0RecentSearches, std::size(kNtpZps0RecentSearches)},
    {"5 recents", kNtpZps5RecentSearches, std::size(kNtpZps5RecentSearches)},
    {"10 recents", kNtpZps10RecentSearches, std::size(kNtpZps10RecentSearches)},
    {"15 recents", kNtpZps15RecentSearches, std::size(kNtpZps15RecentSearches)},
};
const FeatureEntry::FeatureParam kNtpZps0TrendingSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsTrendingSearches.name, "0"}};
const FeatureEntry::FeatureParam kNtpZps5TrendingSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumNtpZpsTrendingSearches.name, "5"}};
const FeatureEntry::FeatureVariation kNumNtpZpsTrendingSearches[] = {
    {"No trends", kNtpZps0TrendingSearches,
     std::size(kNtpZps0TrendingSearches)},
    {"5 trends", kNtpZps5TrendingSearches, std::size(kNtpZps5TrendingSearches)},
};
const FeatureEntry::FeatureParam kWebZps0RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "0"}};
const FeatureEntry::FeatureParam kWebZps5RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "5"}};
const FeatureEntry::FeatureParam kWebZps10RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "10"}};
const FeatureEntry::FeatureParam kWebZps15RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRecentSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumWebZpsRecentSearches[] = {
    {"No recents", kWebZps0RecentSearches, std::size(kWebZps0RecentSearches)},
    {"5 recents", kWebZps5RecentSearches, std::size(kWebZps5RecentSearches)},
    {"10 recents", kWebZps10RecentSearches, std::size(kWebZps10RecentSearches)},
    {"15 recents", kWebZps15RecentSearches, std::size(kWebZps15RecentSearches)},
};
const FeatureEntry::FeatureParam kWebZps0RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "0"}};
const FeatureEntry::FeatureParam kWebZps5RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "5"}};
const FeatureEntry::FeatureParam kWebZps10RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "10"}};
const FeatureEntry::FeatureParam kWebZps15RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsRelatedSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumWebZpsRelatedSearches[] = {
    {"No related", kWebZps0RelatedSearches, std::size(kWebZps0RelatedSearches)},
    {"5 related", kWebZps5RelatedSearches, std::size(kWebZps5RelatedSearches)},
    {"10 related", kWebZps10RelatedSearches,
     std::size(kWebZps10RelatedSearches)},
    {"15 related", kWebZps15RelatedSearches,
     std::size(kWebZps15RelatedSearches)},
};
const FeatureEntry::FeatureParam kWebZps0MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "0"}};
const FeatureEntry::FeatureParam kWebZps5MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "5"}};
const FeatureEntry::FeatureParam kWebZps10MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "10"}};
const FeatureEntry::FeatureParam kWebZps15MostVisitedUrls[] = {
    {OmniboxFieldTrial::kOmniboxNumWebZpsMostVisitedUrls.name, "15"}};
const FeatureEntry::FeatureVariation kNumWebZpsMostVisitedUrls[] = {
    {"No related", kWebZps0MostVisitedUrls, std::size(kWebZps0MostVisitedUrls)},
    {"5 related", kWebZps5MostVisitedUrls, std::size(kWebZps5MostVisitedUrls)},
    {"10 related", kWebZps10MostVisitedUrls,
     std::size(kWebZps10MostVisitedUrls)},
    {"15 related", kWebZps15MostVisitedUrls,
     std::size(kWebZps15MostVisitedUrls)},
};
const FeatureEntry::FeatureParam kSrpZps0RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "0"}};
const FeatureEntry::FeatureParam kSrpZps5RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "5"}};
const FeatureEntry::FeatureParam kSrpZps10RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "10"}};
const FeatureEntry::FeatureParam kSrpZps15RecentSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRecentSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumSrpZpsRecentSearches[] = {
    {"No recents", kSrpZps0RecentSearches, std::size(kSrpZps0RecentSearches)},
    {"5 recents", kSrpZps5RecentSearches, std::size(kSrpZps5RecentSearches)},
    {"10 recents", kSrpZps10RecentSearches, std::size(kSrpZps10RecentSearches)},
    {"15 recents", kSrpZps15RecentSearches, std::size(kSrpZps15RecentSearches)},
};
const FeatureEntry::FeatureParam kSrpZps0RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "0"}};
const FeatureEntry::FeatureParam kSrpZps5RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "5"}};
const FeatureEntry::FeatureParam kSrpZps10RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "10"}};
const FeatureEntry::FeatureParam kSrpZps15RelatedSearches[] = {
    {OmniboxFieldTrial::kOmniboxNumSrpZpsRelatedSearches.name, "15"}};
const FeatureEntry::FeatureVariation kNumSrpZpsRelatedSearches[] = {
    {"No related", kSrpZps0RelatedSearches, std::size(kSrpZps0RelatedSearches)},
    {"5 related", kSrpZps5RelatedSearches, std::size(kSrpZps5RelatedSearches)},
    {"10 related", kSrpZps10RelatedSearches,
     std::size(kSrpZps10RelatedSearches)},
    {"15 related", kSrpZps15RelatedSearches,
     std::size(kSrpZps15RelatedSearches)},
};

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kMinimumTabWidthSettingPinned[] = {
    {tabs::kMinimumTabWidthFeatureParameterName, "54"}};
const FeatureEntry::FeatureParam kMinimumTabWidthSettingMedium[] = {
    {tabs::kMinimumTabWidthFeatureParameterName, "72"}};
const FeatureEntry::FeatureParam kMinimumTabWidthSettingLarge[] = {
    {tabs::kMinimumTabWidthFeatureParameterName, "140"}};
const FeatureEntry::FeatureParam kMinimumTabWidthSettingFull[] = {
    {tabs::kMinimumTabWidthFeatureParameterName, "256"}};

const FeatureEntry::FeatureVariation kTabScrollingVariations[] = {
    {" - tabs shrink to pinned tab width", kMinimumTabWidthSettingPinned,
     std::size(kMinimumTabWidthSettingPinned), nullptr},
    {" - tabs shrink to a medium width", kMinimumTabWidthSettingMedium,
     std::size(kMinimumTabWidthSettingMedium), nullptr},
    {" - tabs shrink to a large width", kMinimumTabWidthSettingLarge,
     std::size(kMinimumTabWidthSettingLarge), nullptr},
    {" - tabs don't shrink", kMinimumTabWidthSettingFull,
     std::size(kMinimumTabWidthSettingFull), nullptr}};
#endif

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kTabstripComboButtonBackground[] = {
    {"has_background", "true"}};

const FeatureEntry::FeatureParam kTabstripComboButtonReverseButtonOrder[] = {
    {"reverse_button_order", "true"}};

const FeatureEntry::FeatureParam
    kTabstripComboButtonReverseButtonOrderBackground[] = {
        {"has_background", "true"},
        {"reverse_button_order", "true"}};

const FeatureEntry::FeatureParam kTabSearchToolbarButton[] = {
    {"tab_search_toolbar_button", "true"}};

const FeatureEntry::FeatureVariation kTabstripComboButtonVariations[] = {
    {" - with background", kTabstripComboButtonBackground,
     std::size(kTabstripComboButtonBackground)},
    {" - reverse button order", kTabstripComboButtonReverseButtonOrder,
     std::size(kTabstripComboButtonReverseButtonOrder)},
    {" - reverse button order & with background",
     kTabstripComboButtonReverseButtonOrderBackground,
     std::size(kTabstripComboButtonReverseButtonOrderBackground)},
    {" - toolbar button", kTabSearchToolbarButton,
     std::size(kTabSearchToolbarButton)},
};
#endif

const FeatureEntry::FeatureParam kTabScrollingButtonPositionRight[] = {
    {features::kTabScrollingButtonPositionParameterName, "0"}};
const FeatureEntry::FeatureParam kTabScrollingButtonPositionLeft[] = {
    {features::kTabScrollingButtonPositionParameterName, "1"}};
const FeatureEntry::FeatureParam kTabScrollingButtonPositionSplit[] = {
    {features::kTabScrollingButtonPositionParameterName, "2"}};

const FeatureEntry::FeatureVariation kTabScrollingButtonPositionVariations[] = {
    {" - to the right of the tabstrip", kTabScrollingButtonPositionRight,
     std::size(kTabScrollingButtonPositionRight), nullptr},
    {" - to the left of the tabstrip", kTabScrollingButtonPositionLeft,
     std::size(kTabScrollingButtonPositionLeft), nullptr},
    {" - on both sides of the tabstrip", kTabScrollingButtonPositionSplit,
     std::size(kTabScrollingButtonPositionSplit), nullptr}};

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kTabScrollingWithDraggingWithConstantSpeed[] =
    {{tabs::kTabScrollingWithDraggingModeName, "1"}};
const FeatureEntry::FeatureParam kTabScrollingWithDraggingWithVariableSpeed[] =
    {{tabs::kTabScrollingWithDraggingModeName, "2"}};

const FeatureEntry::FeatureVariation kTabScrollingWithDraggingVariations[] = {
    {" - tabs scrolling with constant speed",
     kTabScrollingWithDraggingWithConstantSpeed,
     std::size(kTabScrollingWithDraggingWithConstantSpeed), nullptr},
    {" - tabs scrolling with variable speed region",
     kTabScrollingWithDraggingWithVariableSpeed,
     std::size(kTabScrollingWithDraggingWithVariableSpeed), nullptr}};

const FeatureEntry::FeatureParam kScrollableTabStripOverflowDivider[] = {
    {tabs::kScrollableTabStripOverflowModeName, "1"}};
const FeatureEntry::FeatureParam kScrollableTabStripOverflowFade[] = {
    {tabs::kScrollableTabStripOverflowModeName, "2"}};
const FeatureEntry::FeatureParam kScrollableTabStripOverflowShadow[] = {
    {tabs::kScrollableTabStripOverflowModeName, "3"}};

const FeatureEntry::FeatureVariation kScrollableTabStripOverflowVariations[] = {
    {" - Divider", kScrollableTabStripOverflowDivider,
     std::size(kScrollableTabStripOverflowDivider), nullptr},  // Divider
    {" - Fade", kScrollableTabStripOverflowFade,
     std::size(kScrollableTabStripOverflowFade), nullptr},  // Fade
    {" - Shadow", kScrollableTabStripOverflowShadow,
     std::size(kScrollableTabStripOverflowShadow), nullptr},  // Shadow
};
#endif

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kMiniToolbarOnActiveView[] = {
    {"mini_toolbar_active_config", "showall"}};

const FeatureEntry::FeatureParam kMiniToolbarWithMenuOnlyOnActiveView[] = {
    {"mini_toolbar_active_config", "showmenuonly"}};

const FeatureEntry::FeatureVariation kSideBySideVariations[] = {
    {" - show mini toolbar on active view", kMiniToolbarOnActiveView,
     std::size(kMiniToolbarOnActiveView)},
    {" - show mini toolbar with menu only on active view",
     kMiniToolbarWithMenuOnlyOnActiveView,
     std::size(kMiniToolbarWithMenuOnlyOnActiveView)},
};
#endif

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kNtpCalendarModuleFakeData[] = {
    {ntp_features::kNtpCalendarModuleDataParam, "fake"}};
const FeatureEntry::FeatureVariation kNtpCalendarModuleVariations[] = {
    {"- Fake Data", kNtpCalendarModuleFakeData,
     std::size(kNtpCalendarModuleFakeData), nullptr},
};

const FeatureEntry::FeatureParam kNtpChromeCartModuleFakeData[] = {
    {ntp_features::kNtpChromeCartModuleDataParam, "fake"},
    {ntp_features::kNtpChromeCartModuleAbandonedCartDiscountParam, "true"}};
const FeatureEntry::FeatureParam kNtpChromeCartModuleAbandonedCartDiscount[] = {
    {ntp_features::kNtpChromeCartModuleAbandonedCartDiscountParam, "true"},
    {ntp_features::kNtpChromeCartModuleAbandonedCartDiscountUseUtmParam,
     "true"},
    {"partner-merchant-pattern",
     "(electronicexpress.com|zazzle.com|wish.com|homesquare.com|iherb.com|"
     "zappos.com|otterbox.com)"}};
const FeatureEntry::FeatureParam kNtpChromeCartModuleHeuristicsImprovement[] = {
    {ntp_features::kNtpChromeCartModuleHeuristicsImprovementParam, "true"}};
const FeatureEntry::FeatureParam kNtpChromeCartModuleRBDAndCouponDiscount[] = {
    {ntp_features::kNtpChromeCartModuleHeuristicsImprovementParam, "true"},
    {ntp_features::kNtpChromeCartModuleAbandonedCartDiscountParam, "true"},
    {ntp_features::kNtpChromeCartModuleAbandonedCartDiscountUseUtmParam,
     "true"},
    {"partner-merchant-pattern",
     "(electronicexpress.com|zazzle.com|wish.com|homesquare.com)"},
    {ntp_features::kNtpChromeCartModuleCouponParam, "true"}};
const FeatureEntry::FeatureVariation kNtpChromeCartModuleVariations[] = {
    {"- Fake Data And Discount", kNtpChromeCartModuleFakeData,
     std::size(kNtpChromeCartModuleFakeData), nullptr},
    {"- Abandoned Cart Discount", kNtpChromeCartModuleAbandonedCartDiscount,
     std::size(kNtpChromeCartModuleAbandonedCartDiscount), nullptr},
    {"- Heuristics Improvement", kNtpChromeCartModuleHeuristicsImprovement,
     std::size(kNtpChromeCartModuleHeuristicsImprovement), nullptr},
    {"- RBD and Coupons", kNtpChromeCartModuleRBDAndCouponDiscount,
     std::size(kNtpChromeCartModuleRBDAndCouponDiscount), nullptr},
};

const FeatureEntry::FeatureParam kNtpDriveModuleFakeData[] = {
    {ntp_features::kNtpDriveModuleDataParam, "fake"}};
const FeatureEntry::FeatureParam kNtpDriveModuleManagedUsersOnly[] = {
    {ntp_features::kNtpDriveModuleManagedUsersOnlyParam, "true"}};
const FeatureEntry::FeatureVariation kNtpDriveModuleVariations[] = {
    {"- Fake Data", kNtpDriveModuleFakeData, std::size(kNtpDriveModuleFakeData),
     nullptr},
    {"- Managed Users Only", kNtpDriveModuleManagedUsersOnly,
     std::size(kNtpDriveModuleManagedUsersOnly), nullptr},
};

const FeatureEntry::FeatureParam kNtpOutlookCalendarModuleFakeData[] = {
    {ntp_features::kNtpOutlookCalendarModuleDataParam, "fake"}};
const FeatureEntry::FeatureParam
    kNtpOutlookCalendarModuleFakeAttachmentsData[] = {
        {ntp_features::kNtpOutlookCalendarModuleDataParam, "fake-attachments"}};
const FeatureEntry::FeatureVariation kNtpOutlookCalendarModuleVariations[] = {
    {"- Fake Data", kNtpOutlookCalendarModuleFakeData,
     std::size(kNtpOutlookCalendarModuleFakeData), nullptr},
    {"- Fake Attachments Data", kNtpOutlookCalendarModuleFakeAttachmentsData,
     std::size(kNtpOutlookCalendarModuleFakeAttachmentsData), nullptr},
};

const FeatureEntry::FeatureParam kNtpMiddleSlotPromoDismissalFakeData[] = {
    {ntp_features::kNtpMiddleSlotPromoDismissalParam, "fake"}};
const FeatureEntry::FeatureVariation kNtpMiddleSlotPromoDismissalVariations[] =
    {
        {"- Fake Data", kNtpMiddleSlotPromoDismissalFakeData,
         std::size(kNtpMiddleSlotPromoDismissalFakeData), nullptr},
};

const FeatureEntry::FeatureParam
    kNtpRealboxCr23NoShadowExpandedStateBgMatchesSteadyState[]{
        {"kNtpRealboxCr23ExpandedStateBgMatchesOmnibox", "false"},
        {"kNtpRealboxCr23SteadyStateShadow", "false"}};
const FeatureEntry::FeatureParam
    kNtpRealboxCr23ShadowExpandedStateBgMatchesOmnibox[]{
        {"kNtpRealboxCr23ExpandedStateBgMatchesOmnibox", "true"},
        {"kNtpRealboxCr23SteadyStateShadow", "true"}};
const FeatureEntry::FeatureParam
    kNtpRealboxCr23ShadowExpandedStateBgMatchesSteadyState[]{
        {"kNtpRealboxCr23ExpandedStateBgMatchesOmnibox", "false"},
        {"kNtpRealboxCr23SteadyStateShadow", "true"}};

const FeatureEntry::FeatureVariation kNtpRealboxCr23ThemingVariations[] = {
    {" - Steady state shadow",
     kNtpRealboxCr23ShadowExpandedStateBgMatchesOmnibox,
     std::size(kNtpRealboxCr23ShadowExpandedStateBgMatchesOmnibox), nullptr},
    {" - No steady state shadow + Dark mode background color matches steady"
     "state",
     kNtpRealboxCr23NoShadowExpandedStateBgMatchesSteadyState,
     std::size(kNtpRealboxCr23NoShadowExpandedStateBgMatchesSteadyState),
     nullptr},
    {" -  Steady state shadow + Dark mode background color matches steady "
     "state",
     kNtpRealboxCr23ShadowExpandedStateBgMatchesSteadyState,
     std::size(kNtpRealboxCr23ShadowExpandedStateBgMatchesSteadyState),
     nullptr},
};

const FeatureEntry::FeatureParam kNtpSafeBrowsingModuleFastCooldown[] = {
    {ntp_features::kNtpSafeBrowsingModuleCooldownPeriodDaysParam, "0.001"},
    {ntp_features::kNtpSafeBrowsingModuleCountMaxParam, "1"}};
const FeatureEntry::FeatureVariation kNtpSafeBrowsingModuleVariations[] = {
    {"(Fast Cooldown)", kNtpSafeBrowsingModuleFastCooldown,
     std::size(kNtpSafeBrowsingModuleFastCooldown), nullptr},
};

const FeatureEntry::FeatureParam kNtpSharepointModuleTrendingInsights[] = {
    {"NtpSharepointModuleDataParam", "trending-insights"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleNonInsights[] = {
    {"NtpSharepointModuleDataParam", "non-insights"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleTrendingFakeData[] = {
    {"NtpSharepointModuleDataParam", "fake-trending"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleNonInsightsFakeData[] = {
    {"NtpSharepointModuleDataParam", "fake-non-insights"}};
const FeatureEntry::FeatureParam kNtpSharepointModuleCombinedSuggestions[] = {
    {"NtpSharepointModuleDataParam", "combined"}};

const FeatureEntry::FeatureVariation kNtpSharepointModuleVariations[] = {
    {"- Trending", kNtpSharepointModuleTrendingInsights,
     std::size(kNtpSharepointModuleTrendingInsights), nullptr},
    {"- Recently Used and Shared", kNtpSharepointModuleNonInsights,
     std::size(kNtpSharepointModuleNonInsights), nullptr},
    {"- Fake Trending Data", kNtpSharepointModuleTrendingFakeData,
     std::size(kNtpSharepointModuleTrendingFakeData), nullptr},
    {"- Fake Recently Used and Shared", kNtpSharepointModuleNonInsightsFakeData,
     std::size(kNtpSharepointModuleNonInsightsFakeData), nullptr},
    {"- Combined Suggestions", kNtpSharepointModuleCombinedSuggestions,
     std::size(kNtpSharepointModuleCombinedSuggestions), nullptr}};

const FeatureEntry::FeatureParam kNtpMostRelevantTabResumptionModuleFakeData[] =
    {{ntp_features::kNtpMostRelevantTabResumptionModuleDataParam, "Fake Data"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleFakeDataMostRecent[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam,
         "Fake Data - Most Recent Decorator"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleFakeDataFrequentlyVisitedAtTime[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam,
         "Fake Data - Frequently Visited At Time Decorator"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleFakeDataJustVisited[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam,
         "Fake Data - Just Visited Decorator"}};
const FeatureEntry::FeatureParam kNtpMostRelevantTabResumptionModuleTabData[] =
    {{ntp_features::kNtpMostRelevantTabResumptionModuleDataParam, "1,2"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleRemoteTabData[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam, "2"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleRemoteVisitsData[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam, "2,4"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleAllHistoryRemoteTabData[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam, "2,3,4"}};
const FeatureEntry::FeatureParam
    kNtpMostRelevantTabResumptionModuleVisitData[] = {
        {ntp_features::kNtpMostRelevantTabResumptionModuleDataParam,
         "1,2,3,4"}};
// Most relevant tab resumption module data params may be expressed as a comma
// separated value consisting of the integer representations of the
// `FetchOptions::URLType` enumeration, to specify what URL types should be
// provided as options to the Visited URL Ranking Service's APIs.
const FeatureEntry::FeatureVariation
    kNtpMostRelevantTabResumptionModuleVariations[] = {
        {"- Fake Data", kNtpMostRelevantTabResumptionModuleFakeData,
         std::size(kNtpMostRelevantTabResumptionModuleFakeData), nullptr},
        {"- Fake Data - Most Recent Decorator",
         kNtpMostRelevantTabResumptionModuleFakeDataMostRecent,
         std::size(kNtpMostRelevantTabResumptionModuleFakeData), nullptr},
        {"- Fake Data - Frequently Visited At Time Decorator",
         kNtpMostRelevantTabResumptionModuleFakeDataFrequentlyVisitedAtTime,
         std::size(kNtpMostRelevantTabResumptionModuleFakeData), nullptr},
        {"- Fake Data - Just Visited Decorator",
         kNtpMostRelevantTabResumptionModuleFakeDataJustVisited,
         std::size(kNtpMostRelevantTabResumptionModuleFakeData), nullptr},
        {"- Tabs Only", kNtpMostRelevantTabResumptionModuleTabData,
         std::size(kNtpMostRelevantTabResumptionModuleTabData), nullptr},
        {"- Remote Tabs Only", kNtpMostRelevantTabResumptionModuleRemoteTabData,
         std::size(kNtpMostRelevantTabResumptionModuleRemoteTabData), nullptr},
        {"- Remote Visits", kNtpMostRelevantTabResumptionModuleRemoteVisitsData,
         std::size(kNtpMostRelevantTabResumptionModuleRemoteVisitsData),
         nullptr},
        {"- All History, Remote Tabs",
         kNtpMostRelevantTabResumptionModuleAllHistoryRemoteTabData,
         std::size(kNtpMostRelevantTabResumptionModuleAllHistoryRemoteTabData),
         nullptr},
        {"- All Visits", kNtpMostRelevantTabResumptionModuleVisitData,
         std::size(kNtpMostRelevantTabResumptionModuleVisitData), nullptr},
};
#endif  // !BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kDataSharingShowSendFeedbackDisabled[] = {
    {"show_send_feedback", "false"}};
const FeatureEntry::FeatureParam kDataSharingShowSendFeedbackEnabled[] = {
    {"show_send_feedback", "true"}};
const FeatureEntry::FeatureVariation kDatasharingVariations[] = {
    {"with feedback", kDataSharingShowSendFeedbackEnabled,
     std::size(kDataSharingShowSendFeedbackEnabled)},
    {"without feedback", kDataSharingShowSendFeedbackDisabled,
     std::size(kDataSharingShowSendFeedbackEnabled)}};

#if BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kContextualSearchSuppressShortViewWith300Dp[] =
    {{"contextual_search_minimum_page_height_dp", "300"}};
const FeatureEntry::FeatureParam kContextualSearchSuppressShortViewWith400Dp[] =
    {{"contextual_search_minimum_page_height_dp", "400"}};
const FeatureEntry::FeatureParam kContextualSearchSuppressShortViewWith500Dp[] =
    {{"contextual_search_minimum_page_height_dp", "500"}};
const FeatureEntry::FeatureParam kContextualSearchSuppressShortViewWith600Dp[] =
    {{"contextual_search_minimum_page_height_dp", "600"}};
const FeatureEntry::FeatureVariation
    kContextualSearchSuppressShortViewVariations[] = {
        {"(300 dp)", kContextualSearchSuppressShortViewWith300Dp,
         std::size(kContextualSearchSuppressShortViewWith300Dp), nullptr},
        {"(400 dp)", kContextualSearchSuppressShortViewWith400Dp,
         std::size(kContextualSearchSuppressShortViewWith400Dp), nullptr},
        {"(500 dp)", kContextualSearchSuppressShortViewWith500Dp,
         std::size(kContextualSearchSuppressShortViewWith500Dp), nullptr},
        {"(600 dp)", kContextualSearchSuppressShortViewWith600Dp,
         std::size(kContextualSearchSuppressShortViewWith600Dp), nullptr},
};

const FeatureEntry::FeatureParam kUseRunningCompactDelay_default[] = {
    {"running_compact_delay_after_tasks", "30"}};
const FeatureEntry::FeatureParam kUseRunningCompactDelay_immediate[] = {
    {"running_compact_delay_after_tasks", "2"}};

const FeatureEntry::FeatureVariation kUseRunningCompactDelayOptions[] = {
    {"default", kUseRunningCompactDelay_default,
     std::size(kUseRunningCompactDelay_default), nullptr},
    {"immediate", kUseRunningCompactDelay_immediate,
     std::size(kUseRunningCompactDelay_immediate), nullptr}};

const FeatureEntry::FeatureParam kJumpStartOmnibox1Minute[] = {
    {"jump_start_min_away_time_minutes", "1"},
    {"jump_start_cover_recently_visited_page", "true"}};
const FeatureEntry::FeatureParam kJumpStartOmnibox15Minutes[] = {
    {"jump_start_min_away_time_minutes", "15"},
    {"jump_start_cover_recently_visited_page", "true"}};
const FeatureEntry::FeatureParam kJumpStartOmnibox30Minutes[] = {
    {"jump_start_min_away_time_minutes", "30"},
    {"jump_start_cover_recently_visited_page", "true"}};
const FeatureEntry::FeatureParam kJumpStartOmnibox60Minutes[] = {
    {"jump_start_min_away_time_minutes", "60"},
    {"jump_start_cover_recently_visited_page", "true"}};

const FeatureEntry::FeatureVariation kJumpStartOmniboxVariations[] = {
    {"(after 1min)", kJumpStartOmnibox1Minute,
     std::size(kJumpStartOmnibox1Minute)},
    {"(after 15min)", kJumpStartOmnibox15Minutes,
     std::size(kJumpStartOmnibox15Minutes)},
    {"(after 30min)", kJumpStartOmnibox30Minutes,
     std::size(kJumpStartOmnibox30Minutes)},
    {"(after 60min)", kJumpStartOmnibox60Minutes,
     std::size(kJumpStartOmnibox60Minutes)}};

#endif  // BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam
    kReportNotificationContentDetectionDataRate100[] = {
        {"ReportNotificationContentDetectionDataRate", "100"}};
const FeatureEntry::FeatureVariation
    kReportNotificationContentDetectionDataVariations[] = {
        {"with reporting rate 100",
         kReportNotificationContentDetectionDataRate100,
         std::size(kReportNotificationContentDetectionDataRate100), nullptr},
};

const FeatureEntry::FeatureParam
    kResamplingScrollEventsPredictionFramesBasedEnabledV1[] = {
        {"mode", features::kPredictionTypeFramesBased},
        {"latency", features::kPredictionTypeDefaultFramesVariation1}};
const FeatureEntry::FeatureParam
    kResamplingScrollEventsPredictionFramesBasedEnabledV2[] = {
        {"mode", features::kPredictionTypeFramesBased},
        {"latency", features::kPredictionTypeDefaultFramesVariation2}};
const FeatureEntry::FeatureParam
    kResamplingScrollEventsPredictionFramesBasedEnabledV3[] = {
        {"mode", features::kPredictionTypeFramesBased},
        {"latency", features::kPredictionTypeDefaultFramesVariation3}};
const FeatureEntry::FeatureVariation
    kResamplingScrollEventsExperimentalPredictionVariations[] = {
        {"frames 0.25", kResamplingScrollEventsPredictionFramesBasedEnabledV1,
         std::size(kResamplingScrollEventsPredictionFramesBasedEnabledV1),
         nullptr},
        {"frames 0.375", kResamplingScrollEventsPredictionFramesBasedEnabledV2,
         std::size(kResamplingScrollEventsPredictionFramesBasedEnabledV2),
         nullptr},
        {"frames 0.5", kResamplingScrollEventsPredictionFramesBasedEnabledV3,
         std::size(kResamplingScrollEventsPredictionFramesBasedEnabledV3),
         nullptr},
};

const FeatureEntry::FeatureParam
    kShowWarningsForSuspiciousNotificationsScoreThreshold70[] = {
        {"ShowWarningsForSuspiciousNotificationsScoreThreshold", "70"},
        {"ShowWarningsForSuspiciousNotificationsShouldSwapButtons", "false"}};
const FeatureEntry::FeatureParam
    kShowWarningsForSuspiciousNotificationsScoreThreshold70SwapButtons[] = {
        {"ShowWarningsForSuspiciousNotificationsScoreThreshold", "70"},
        {"ShowWarningsForSuspiciousNotificationsShouldSwapButtons", "true"}};
const FeatureEntry::FeatureVariation
    kShowWarningsForSuspiciousNotificationsVariations[] = {
        {"with suspicious score 70",
         kShowWarningsForSuspiciousNotificationsScoreThreshold70,
         std::size(kShowWarningsForSuspiciousNotificationsScoreThreshold70),
         nullptr},
        {"with suspicious score 70 and swapped buttons",
         kShowWarningsForSuspiciousNotificationsScoreThreshold70SwapButtons,
         std::size(
             kShowWarningsForSuspiciousNotificationsScoreThreshold70SwapButtons),
         nullptr},
};

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kStartSurfaceReturnTime_Immediate[] = {
    {"start_surface_return_time_seconds", "0"},
    {"start_surface_return_time_on_tablet_seconds", "0"}};
const FeatureEntry::FeatureParam kStartSurfaceReturnTime_10Seconds[] = {
    {"start_surface_return_time_seconds", "10"},
    {"start_surface_return_time_on_tablet_seconds", "10"}};
const FeatureEntry::FeatureParam kStartSurfaceReturnTime_1Minute[] = {
    {"start_surface_return_time_seconds", "60"},
    {"start_surface_return_time_on_tablet_seconds", "60"}};
const FeatureEntry::FeatureParam kStartSurfaceReturnTime_5Minute[] = {
    {"start_surface_return_time_seconds", "300"},
    {"start_surface_return_time_on_tablet_seconds", "300"}};
const FeatureEntry::FeatureParam kStartSurfaceReturnTime_60Minute[] = {
    {"start_surface_return_time_seconds", "3600"},
    {"start_surface_return_time_on_tablet_seconds", "3600"}};
const FeatureEntry::FeatureVariation kStartSurfaceReturnTimeVariations[] = {
    {"Immediate", kStartSurfaceReturnTime_Immediate,
     std::size(kStartSurfaceReturnTime_Immediate), nullptr},
    {"10 seconds", kStartSurfaceReturnTime_10Seconds,
     std::size(kStartSurfaceReturnTime_10Seconds), nullptr},
    {"1 minute", kStartSurfaceReturnTime_1Minute,
     std::size(kStartSurfaceReturnTime_1Minute), nullptr},
    {"5 minute", kStartSurfaceReturnTime_5Minute,
     std::size(kStartSurfaceReturnTime_5Minute), nullptr},
    {"60 minute", kStartSurfaceReturnTime_60Minute,
     std::size(kStartSurfaceReturnTime_60Minute), nullptr},
};

const FeatureEntry::FeatureParam kMagicStackAndroid_show_all_modules[] = {
    {"show_all_modules", "true"}};

const FeatureEntry::FeatureVariation kMagicStackAndroidVariations[] = {
    {"Show all modules", kMagicStackAndroid_show_all_modules,
     std::size(kMagicStackAndroid_show_all_modules), nullptr},
};

const FeatureEntry::FeatureParam kDefaultBrowserPromoShowArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceShowCardParam,
     segmentation_platform::kDefaultBrowserPromo},
};
const FeatureEntry::FeatureParam kDefaultBrowserPromoHideArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceHideCardParam,
     segmentation_platform::kDefaultBrowserPromo},
};
const FeatureEntry::FeatureParam kTabGroupPromoShowArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceShowCardParam,
     segmentation_platform::kTabGroupPromo},
};
const FeatureEntry::FeatureParam kTabGroupPromoHideArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceHideCardParam,
     segmentation_platform::kTabGroupPromo},
};
const FeatureEntry::FeatureParam kTabGroupSyncPromoShowArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceShowCardParam,
     segmentation_platform::kTabGroupSyncPromo},
};
const FeatureEntry::FeatureParam kTabGroupSyncPromoHideArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceHideCardParam,
     segmentation_platform::kTabGroupSyncPromo},
};
const FeatureEntry::FeatureParam kQuickDeletePromoShowArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceShowCardParam,
     segmentation_platform::kQuickDeletePromo},
};
const FeatureEntry::FeatureParam kQuickDeletePromoHideArm[] = {
    {segmentation_platform::features::kEphemeralCardRankerForceHideCardParam,
     segmentation_platform::kQuickDeletePromo},
};

const FeatureEntry::FeatureVariation kEphemeralCardRankerCardOverrideOptions[] =
    {
        {"- Force show default browser promo", kDefaultBrowserPromoShowArm,
         std::size(kDefaultBrowserPromoShowArm), nullptr},
        {"- Force hide default browser promo", kDefaultBrowserPromoHideArm,
         std::size(kDefaultBrowserPromoHideArm), nullptr},
        {"- Force show tab group promo", kTabGroupPromoShowArm,
         std::size(kTabGroupPromoShowArm), nullptr},
        {"- Force hide tab group promo", kTabGroupPromoHideArm,
         std::size(kTabGroupPromoHideArm), nullptr},
        {"- Force show tab group sync promo", kTabGroupSyncPromoShowArm,
         std::size(kTabGroupSyncPromoShowArm), nullptr},
        {"- Force hide tab group sync promo", kTabGroupSyncPromoHideArm,
         std::size(kTabGroupSyncPromoHideArm), nullptr},
        {"- Force show quick delete promo", kQuickDeletePromoShowArm,
         std::size(kQuickDeletePromoShowArm), nullptr},
        {"- Force hide quick delete promo", kQuickDeletePromoHideArm,
         std::size(kQuickDeletePromoHideArm), nullptr},
};
#endif  // BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kSearchResumption_use_new_service[] = {
    {"use_new_service", "true"}};
const FeatureEntry::FeatureVariation
    kSearchResumptionModuleAndroidVariations[] = {
        {"Use New Service", kSearchResumption_use_new_service,
         std::size(kSearchResumption_use_new_service), nullptr},
};

const FeatureEntry::FeatureParam
    kMostVitedTilesReselect_enable_partial_match_arm1[] = {
        {"lax_scheme_host", "true"},
        {"lax_ref", "true"},
        {"lax_query", "false"},
        {"lax_path", "false"},
};
const FeatureEntry::FeatureParam
    kMostVitedTilesReselect_enable_partial_match_arm2[] = {
        {"lax_scheme_host", "true"},
        {"lax_ref", "true"},
        {"lax_query", "true"},
        {"lax_path", "false"},
};
const FeatureEntry::FeatureParam
    kMostVitedTilesReselect_enable_partial_match_arm3[] = {
        {"lax_scheme_host", "true"},
        {"lax_ref", "true"},
        {"lax_query", "true"},
        {"lax_path", "true"},
};
const FeatureEntry::FeatureVariation kMostVisitedTilesReselectVariations[] = {
    {"Partial match Arm 1", kMostVitedTilesReselect_enable_partial_match_arm1,
     std::size(kMostVitedTilesReselect_enable_partial_match_arm1), nullptr},
    {"Partial match Arm 2", kMostVitedTilesReselect_enable_partial_match_arm2,
     std::size(kMostVitedTilesReselect_enable_partial_match_arm2), nullptr},
    {"Partial match Arm 3", kMostVitedTilesReselect_enable_partial_match_arm3,
     std::size(kMostVitedTilesReselect_enable_partial_match_arm3), nullptr},
};

const FeatureEntry::FeatureParam
    kNotificationPermissionRationale_show_dialog_next_start[] = {
        {"always_show_rationale_before_requesting_permission", "true"},
        {"permission_request_interval_days", "0"},
};

const FeatureEntry::FeatureVariation
    kNotificationPermissionRationaleVariations[] = {
        {"- Show rationale UI on next startup",
         kNotificationPermissionRationale_show_dialog_next_start,
         std::size(kNotificationPermissionRationale_show_dialog_next_start),
         nullptr},
};

const FeatureEntry::FeatureParam kWebFeedAwareness_new_animation[] = {
    {"awareness_style", "new_animation"}};
const FeatureEntry::FeatureParam kWebFeedAwareness_new_animation_no_limit[] = {
    {"awareness_style", "new_animation_no_limit"}};

const FeatureEntry::FeatureParam kWebFeedAwareness_IPH[] = {
    {"awareness_style", "IPH"}};

const FeatureEntry::FeatureVariation kWebFeedAwarenessVariations[] = {
    {"new animation", kWebFeedAwareness_new_animation,
     std::size(kWebFeedAwareness_new_animation), nullptr},
    {"new animation rate limit off", kWebFeedAwareness_new_animation_no_limit,
     std::size(kWebFeedAwareness_new_animation_no_limit), nullptr},
    {"IPH and dot", kWebFeedAwareness_IPH, std::size(kWebFeedAwareness_IPH),
     nullptr},
};

#endif  // BUILDFLAG(IS_ANDROID)

const FeatureEntry::Choice kNotificationSchedulerChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::
         kNotificationSchedulerImmediateBackgroundTaskDescription,
     notifications::switches::kNotificationSchedulerImmediateBackgroundTask,
     ""},
};

#if BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kPhotoPickerAdoptionStudyActionGetContent[] = {
    {"use_action_get_content", "true"}};
const FeatureEntry::FeatureParam kPhotoPickerAdoptionStudyActionPickImages[] = {
    {"use_action_pick_images", "true"}};
const FeatureEntry::FeatureParam
    kPhotoPickerAdoptionStudyActionPickImagesPlus[] = {
        {"use_action_pick_images_plus", "true"}};
const FeatureEntry::FeatureParam
    kPhotoPickerAdoptionStudyChromePickerWithoutBrowse[] = {
        {"chrome_picker_suppress_browse", "true"}};

const FeatureEntry::FeatureVariation
    kPhotoPickerAdoptionStudyFeatureVariations[] = {
        {"(Android Picker w/ACTION_GET_CONTENT)",
         kPhotoPickerAdoptionStudyActionGetContent,
         std::size(kPhotoPickerAdoptionStudyActionGetContent), nullptr},
        {"(Android Picker w/ACTION_PICK_IMAGES)",
         kPhotoPickerAdoptionStudyActionPickImages,
         std::size(kPhotoPickerAdoptionStudyActionPickImages), nullptr},
        {"(Android Picker w/ACTION_PICK_IMAGES Plus)",
         kPhotoPickerAdoptionStudyActionPickImagesPlus,
         std::size(kPhotoPickerAdoptionStudyActionPickImagesPlus), nullptr},
        {"(Chrome Picker without Browse)",
         kPhotoPickerAdoptionStudyChromePickerWithoutBrowse,
         std::size(kPhotoPickerAdoptionStudyChromePickerWithoutBrowse),
         nullptr}};

const FeatureEntry::FeatureParam
    kAndroidAppIntegrationWithFavicon_UseLargeFavicon[] = {
        {"use_large_favicon", "true"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipDeviceCheck[] = {
        {"skip_device_check", "true"},
        {"use_large_favicon", "true"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipSchemaCheck[] = {
        {"skip_schema_check", "true"},
        {"use_large_favicon", "true"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipDeviceAndSchemaChecks
        [] = {{"skip_device_check", "true"},
              {"skip_schema_check", "true"},
              {"use_large_favicon", "true"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationWithFavicon_DelayTime200Ms[] = {
        {"schedule_delay_time_ms", "200"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationWithFavicon_DelayTime200Ms_UseLargeFavicon[] = {
        {"schedule_delay_time_ms", "200"},
        {"use_large_favicon", "true"}};

const FeatureEntry::FeatureVariation kAndroidAppIntegrationWithFaviconVariations[] =
    {{"Use large favicon (no delay)",
      kAndroidAppIntegrationWithFavicon_UseLargeFavicon,
      std::size(kAndroidAppIntegrationWithFavicon_UseLargeFavicon), nullptr},
     {"Skip device check + use large favicon (no delay)",
      kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipDeviceCheck,
      std::size(
          kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipDeviceCheck),
      nullptr},
     {"Skip schema check + use large favicon (no delay)",
      kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipSchemaCheck,
      std::size(
          kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipSchemaCheck),
      nullptr},
     {"Skip both device and schema checks + use large favicon (no delay)",
      kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipDeviceAndSchemaChecks,
      std::size(
          kAndroidAppIntegrationWithFavicon_UseLargeFavicon_SkipDeviceAndSchemaChecks),
      nullptr},
     {"200ms delay", kAndroidAppIntegrationWithFavicon_DelayTime200Ms,
      std::size(kAndroidAppIntegrationWithFavicon_DelayTime200Ms), nullptr},
     {"200ms delay with large favicon",
      kAndroidAppIntegrationWithFavicon_DelayTime200Ms_UseLargeFavicon,
      std::size(
          kAndroidAppIntegrationWithFavicon_DelayTime200Ms_UseLargeFavicon),
      nullptr}};

const FeatureEntry::FeatureParam
    kAndroidAppIntegrationModule_ForceCardShown_Pixel[] = {
        {"force_card_shown", "true"}};

const FeatureEntry::FeatureParam
    kAndroidAppIntegrationModule_ForceCardShown_NonPixel[] = {
        {"force_card_shown", "true"},
        {"show_third_party_card", "true"}};

const FeatureEntry::FeatureVariation kAndroidAppIntegrationModuleVariations[] =
    {{"Force to show Pixel's notice card",
      kAndroidAppIntegrationModule_ForceCardShown_Pixel,
      std::size(kAndroidAppIntegrationModule_ForceCardShown_Pixel), nullptr},
     {"Force to show opt in card",
      kAndroidAppIntegrationModule_ForceCardShown_NonPixel,
      std::size(kAndroidAppIntegrationModule_ForceCardShown_NonPixel),
      nullptr}};

const FeatureEntry::FeatureParam kAndroidComposeplate_HideIncognitoButton[] = {
    {"hide_incognito_button", "true"}};
const FeatureEntry::FeatureParam kAndroidComposeplate_SkipLocaleCheck[] = {
    {"skip_locale_check", "true"}};

const FeatureEntry::FeatureVariation kAndroidComposeplateVariations[] = {
    {"Hide incognito button", kAndroidComposeplate_HideIncognitoButton,
     std::size(kAndroidComposeplate_HideIncognitoButton), nullptr},
    {"Skip locale check", kAndroidComposeplate_SkipLocaleCheck,
     std::size(kAndroidComposeplate_SkipLocaleCheck), nullptr}};

const FeatureEntry::FeatureParam
    kAndroidAppIntegrationMultiDataSource_SkipDeviceCheck[] = {
        {"multi_data_source_skip_device_check", "true"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationMultiDataSource_SkipSchemaCheck[] = {
        {"multi_data_source_skip_schema_check", "true"}};
const FeatureEntry::FeatureParam
    kAndroidAppIntegrationMultiDataSource_SkipBothDeviceAndSchemaCheck[] = {
        {"multi_data_source_skip_device_check", "true"},
        {"multi_data_source_skip_schema_check", "true"}};

const FeatureEntry::FeatureVariation
    kAndroidAppIntegrationMultiDataSourceVariations[] = {
        {"Skip device check",
         kAndroidAppIntegrationMultiDataSource_SkipDeviceCheck,
         std::size(kAndroidAppIntegrationMultiDataSource_SkipDeviceCheck),
         nullptr},
        {"Skip schema check",
         kAndroidAppIntegrationMultiDataSource_SkipSchemaCheck,
         std::size(kAndroidAppIntegrationMultiDataSource_SkipSchemaCheck),
         nullptr},
        {"Skip both device and schema check",
         kAndroidAppIntegrationMultiDataSource_SkipBothDeviceAndSchemaCheck,
         std::size(
             kAndroidAppIntegrationMultiDataSource_SkipBothDeviceAndSchemaCheck),
         nullptr}};

const FeatureEntry::FeatureParam kAndroidBottomToolbar_DefaultToBottom[] = {
    {"default_to_top", "false"}};

const FeatureEntry::FeatureVariation kAndroidBottomToolbarVariations[] = {
    {"default to bottom", kAndroidBottomToolbar_DefaultToBottom,
     std::size(kAndroidBottomToolbar_DefaultToBottom), nullptr}};

const FeatureEntry::FeatureParam kAuxiliarySearchDonation_MaxDonation_20[] = {
    {chrome::android::kAuxiliarySearchMaxBookmarksCountParam.name, "20"},
    {chrome::android::kAuxiliarySearchMaxTabsCountParam.name, "20"}};
const FeatureEntry::FeatureParam kAuxiliarySearchDonation_MaxDonation_100[] = {
    {chrome::android::kAuxiliarySearchMaxBookmarksCountParam.name, "100"},
    {chrome::android::kAuxiliarySearchMaxTabsCountParam.name, "100"}};
const FeatureEntry::FeatureParam kAuxiliarySearchDonation_MaxDonation_200[] = {
    {chrome::android::kAuxiliarySearchMaxBookmarksCountParam.name, "200"},
    {chrome::android::kAuxiliarySearchMaxTabsCountParam.name, "200"}};
const FeatureEntry::FeatureParam kAuxiliarySearchDonation_MaxDonation_500[] = {
    {chrome::android::kAuxiliarySearchMaxBookmarksCountParam.name, "500"},
    {chrome::android::kAuxiliarySearchMaxTabsCountParam.name, "500"}};
const FeatureEntry::FeatureVariation kAuxiliarySearchDonationVariations[] = {
    {"50 counts", kAuxiliarySearchDonation_MaxDonation_20,
     std::size(kAuxiliarySearchDonation_MaxDonation_20), nullptr},
    {"100 counts", kAuxiliarySearchDonation_MaxDonation_100,
     std::size(kAuxiliarySearchDonation_MaxDonation_100), nullptr},
    {"200 counts", kAuxiliarySearchDonation_MaxDonation_200,
     std::size(kAuxiliarySearchDonation_MaxDonation_200), nullptr},
    {"500 counts", kAuxiliarySearchDonation_MaxDonation_500,
     std::size(kAuxiliarySearchDonation_MaxDonation_500), nullptr},
};

const FeatureEntry::FeatureParam kBoardingPassDetectorUrl_AA[] = {
    {features::kBoardingPassDetectorUrlParamName,
     "https://www.aa.com/checkin/viewMobileBoardingPass"}};
const FeatureEntry::FeatureParam kBoardingPassDetectorUrl_All[] = {
    {features::kBoardingPassDetectorUrlParamName,
     "https://www.aa.com/checkin/viewMobileBoardingPass,https://united.com"}};
const FeatureEntry::FeatureParam kBoardingPassDetectorUrl_Test[] = {
    {features::kBoardingPassDetectorUrlParamName, "http"}};
const FeatureEntry::FeatureVariation kBoardingPassDetectorVariations[] = {
    {"AA", kBoardingPassDetectorUrl_AA, std::size(kBoardingPassDetectorUrl_AA),
     nullptr},
    {"All", kBoardingPassDetectorUrl_All,
     std::size(kBoardingPassDetectorUrl_All), nullptr},
    {"Test", kBoardingPassDetectorUrl_Test,
     std::size(kBoardingPassDetectorUrl_Test), nullptr},
};
#endif  // BUILDFLAG(IS_ANDROID)

// TODO(crbug.com/991082,1015377): Remove after proper support for back/forward
// cache is implemented.
const FeatureEntry::FeatureParam kBackForwardCache_ForceCaching[] = {
    {"TimeToLiveInBackForwardCacheInSeconds", "300"},
    {"should_ignore_blocklists", "true"}};

const FeatureEntry::FeatureVariation kBackForwardCacheVariations[] = {
    {"force caching all pages (experimental)", kBackForwardCache_ForceCaching,
     std::size(kBackForwardCache_ForceCaching), nullptr},
};

const FeatureEntry::FeatureParam kRenderDocument_Subframe[] = {
    {"level", "subframe"}};
const FeatureEntry::FeatureParam kRenderDocument_AllFrames[] = {
    {"level", "all-frames"}};

const FeatureEntry::FeatureVariation kRenderDocumentVariations[] = {
    {"Swap RenderFrameHosts on same-site navigations from subframes and "
     "crashed frames (experimental)",
     kRenderDocument_Subframe, std::size(kRenderDocument_Subframe), nullptr},
    {"Swap RenderFrameHosts on same-site navigations from any frame "
     "(experimental)",
     kRenderDocument_AllFrames, std::size(kRenderDocument_AllFrames), nullptr},
};

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::Choice kWebOtpBackendChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kWebOtpBackendSmsVerification, switches::kWebOtpBackend,
     switches::kWebOtpBackendSmsVerification},
    {flag_descriptions::kWebOtpBackendUserConsent, switches::kWebOtpBackend,
     switches::kWebOtpBackendUserConsent},
    {flag_descriptions::kWebOtpBackendAuto, switches::kWebOtpBackend,
     switches::kWebOtpBackendAuto},
};
#endif  // BUILDFLAG(IS_ANDROID)

// The choices for --enable-experimental-cookie-features. This really should
// just be a SINGLE_VALUE_TYPE, but it is misleading to have the choices be
// labeled "Disabled"/"Enabled". So instead this is made to be a
// MULTI_VALUE_TYPE with choices "Default"/"Enabled".
const FeatureEntry::Choice kEnableExperimentalCookieFeaturesChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kEnableExperimentalCookieFeatures, ""},
};

#if BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::Choice kForceControlFaceAeChoices[] = {
    {"Default", "", ""},
    {"Enable", media::switches::kForceControlFaceAe, "enable"},
    {"Disable", media::switches::kForceControlFaceAe, "disable"}};

const FeatureEntry::Choice kAutoFramingOverrideChoices[] = {
    {"Default", "", ""},
    {"Force enabled", media::switches::kAutoFramingOverride,
     media::switches::kAutoFramingForceEnabled},
    {"Force disabled", media::switches::kAutoFramingOverride,
     media::switches::kAutoFramingForceDisabled}};

const FeatureEntry::Choice kFaceRetouchOverrideChoices[] = {
    {"Default", "", ""},
    {"Enabled with relighting", media::switches::kFaceRetouchOverride,
     media::switches::kFaceRetouchForceEnabledWithRelighting},
    {"Enabled without relighting", media::switches::kFaceRetouchOverride,
     media::switches::kFaceRetouchForceEnabledWithoutRelighting},
    {"Disabled", media::switches::kFaceRetouchOverride,
     media::switches::kFaceRetouchForceDisabled}};

const FeatureEntry::Choice kCrostiniContainerChoices[] = {
    {"Default", "", ""},
    {"Buster", crostini::kCrostiniContainerFlag, "buster"},
    {"Bullseye", crostini::kCrostiniContainerFlag, "bullseye"},
    {"Bookworm", crostini::kCrostiniContainerFlag, "bookworm"},
};
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
// SCT Auditing feature variations.
const FeatureEntry::FeatureParam kSCTAuditingSamplingRateNone[] = {
    {"sampling_rate", "0.0"}};
const FeatureEntry::FeatureParam kSCTAuditingSamplingRateAlternativeOne[] = {
    {"sampling_rate", "0.0001"}};
const FeatureEntry::FeatureParam kSCTAuditingSamplingRateAlternativeTwo[] = {
    {"sampling_rate", "0.001"}};

const FeatureEntry::FeatureVariation kSCTAuditingVariations[] = {
    {"Sampling rate 0%", kSCTAuditingSamplingRateNone,
     std::size(kSCTAuditingSamplingRateNone), nullptr},
    {"Sampling rate 0.01%", kSCTAuditingSamplingRateAlternativeOne,
     std::size(kSCTAuditingSamplingRateAlternativeOne), nullptr},
    {"Sampling rate 0.1%", kSCTAuditingSamplingRateAlternativeTwo,
     std::size(kSCTAuditingSamplingRateAlternativeTwo), nullptr},
};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay10Mins[] = {
    {"long_delay_minutes", "10"}};
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay12Hours[] = {
    {"long_delay_minutes", "720"}};
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay24Hours[] = {
    {"long_delay_minutes", "1440"}};
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay36Hours[] = {
    {"long_delay_minutes", "2160"}};
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay48Hours[] = {
    {"long_delay_minutes", "2880"}};
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay60Hours[] = {
    {"long_delay_minutes", "3600"}};
const FeatureEntry::FeatureParam kLauncherItemSuggest_LongDelay72Hours[] = {
    {"long_delay_minutes", "4320"}};

const FeatureEntry::FeatureVariation kLauncherItemSuggestVariations[] = {
    {"with 10 minute long delay", kLauncherItemSuggest_LongDelay10Mins,
     std::size(kLauncherItemSuggest_LongDelay10Mins), nullptr},
    {"with 12 hour long delay", kLauncherItemSuggest_LongDelay12Hours,
     std::size(kLauncherItemSuggest_LongDelay12Hours), nullptr},
    {"with 24 hour long delay", kLauncherItemSuggest_LongDelay24Hours,
     std::size(kLauncherItemSuggest_LongDelay24Hours), nullptr},
    {"with 36 hour long delay", kLauncherItemSuggest_LongDelay36Hours,
     std::size(kLauncherItemSuggest_LongDelay36Hours), nullptr},
    {"with 48 hour long delay", kLauncherItemSuggest_LongDelay48Hours,
     std::size(kLauncherItemSuggest_LongDelay48Hours), nullptr},
    {"with 60 hour long delay", kLauncherItemSuggest_LongDelay60Hours,
     std::size(kLauncherItemSuggest_LongDelay60Hours), nullptr},
    {"with 72 hour long delay", kLauncherItemSuggest_LongDelay72Hours,
     std::size(kLauncherItemSuggest_LongDelay72Hours), nullptr}};

const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_10[] = {
    {"confidence_threshold", "10"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_20[] = {
    {"confidence_threshold", "20"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_30[] = {
    {"confidence_threshold", "30"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_40[] = {
    {"confidence_threshold", "40"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_50[] = {
    {"confidence_threshold", "50"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_60[] = {
    {"confidence_threshold", "60"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_70[] = {
    {"confidence_threshold", "70"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_80[] = {
    {"confidence_threshold", "80"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_90[] = {
    {"confidence_threshold", "90"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_100[] = {
    {"confidence_threshold", "100"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_110[] = {
    {"confidence_threshold", "110"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchConfidence_120[] = {
    {"confidence_threshold", "120"}};

const FeatureEntry::FeatureVariation
    kLauncherLocalImageSearchConfidenceVariations[] = {
        {"threshold 10", kLauncherLocalImageSearchConfidence_10,
         std::size(kLauncherLocalImageSearchConfidence_10), nullptr},
        {"threshold 20", kLauncherLocalImageSearchConfidence_20,
         std::size(kLauncherLocalImageSearchConfidence_20), nullptr},
        {"threshold 30", kLauncherLocalImageSearchConfidence_30,
         std::size(kLauncherLocalImageSearchConfidence_30), nullptr},
        {"threshold 40", kLauncherLocalImageSearchConfidence_40,
         std::size(kLauncherLocalImageSearchConfidence_40), nullptr},
        {"threshold 50", kLauncherLocalImageSearchConfidence_50,
         std::size(kLauncherLocalImageSearchConfidence_50), nullptr},
        {"threshold 60", kLauncherLocalImageSearchConfidence_60,
         std::size(kLauncherLocalImageSearchConfidence_60), nullptr},
        {"threshold 70", kLauncherLocalImageSearchConfidence_70,
         std::size(kLauncherLocalImageSearchConfidence_70), nullptr},
        {"threshold 80", kLauncherLocalImageSearchConfidence_80,
         std::size(kLauncherLocalImageSearchConfidence_80), nullptr},
        {"threshold 90", kLauncherLocalImageSearchConfidence_90,
         std::size(kLauncherLocalImageSearchConfidence_90), nullptr},
        {"threshold 100", kLauncherLocalImageSearchConfidence_100,
         std::size(kLauncherLocalImageSearchConfidence_100), nullptr},
        {"threshold 110", kLauncherLocalImageSearchConfidence_110,
         std::size(kLauncherLocalImageSearchConfidence_110), nullptr},
        {"threshold 120", kLauncherLocalImageSearchConfidence_120,
         std::size(kLauncherLocalImageSearchConfidence_120), nullptr}};

const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_10[] = {
    {"relevance_threshold", "0.1"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_20[] = {
    {"relevance_threshold", "0.2"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_30[] = {
    {"relevance_threshold", "0.3"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_40[] = {
    {"relevance_threshold", "0.4"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_50[] = {
    {"relevance_threshold", "0.5"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_60[] = {
    {"relevance_threshold", "0.6"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_70[] = {
    {"relevance_threshold", "0.7"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_80[] = {
    {"relevance_threshold", "0.8"}};
const FeatureEntry::FeatureParam kLauncherLocalImageSearchRelevance_90[] = {
    {"relevance_threshold", "0.9"}};

const FeatureEntry::FeatureVariation
    kLauncherLocalImageSearchRelevanceVariations[] = {
        {"threshold 0.1", kLauncherLocalImageSearchRelevance_10,
         std::size(kLauncherLocalImageSearchRelevance_10), nullptr},
        {"threshold 0.2", kLauncherLocalImageSearchRelevance_20,
         std::size(kLauncherLocalImageSearchRelevance_20), nullptr},
        {"threshold 0.3", kLauncherLocalImageSearchRelevance_30,
         std::size(kLauncherLocalImageSearchRelevance_30), nullptr},
        {"threshold 0.4", kLauncherLocalImageSearchRelevance_40,
         std::size(kLauncherLocalImageSearchRelevance_40), nullptr},
        {"threshold 0.5", kLauncherLocalImageSearchRelevance_50,
         std::size(kLauncherLocalImageSearchRelevance_50), nullptr},
        {"threshold 0.6", kLauncherLocalImageSearchRelevance_60,
         std::size(kLauncherLocalImageSearchRelevance_60), nullptr},
        {"threshold 0.7", kLauncherLocalImageSearchRelevance_70,
         std::size(kLauncherLocalImageSearchRelevance_70), nullptr},
        {"threshold 0.8", kLauncherLocalImageSearchRelevance_80,
         std::size(kLauncherLocalImageSearchRelevance_80), nullptr},
        {"threshold 0.9", kLauncherLocalImageSearchRelevance_90,
         std::size(kLauncherLocalImageSearchRelevance_90), nullptr}};

const FeatureEntry::FeatureParam kEolIncentiveOffer[] = {
    {"incentive_type", "offer"}};
const FeatureEntry::FeatureParam kEolIncentiveNoOffer[] = {
    {"incentive_type", "no_offer"}};

const FeatureEntry::FeatureVariation kEolIncentiveVariations[] = {
    {"with offer", kEolIncentiveOffer, std::size(kEolIncentiveOffer), nullptr},
    {"with no offer", kEolIncentiveNoOffer, std::size(kEolIncentiveNoOffer),
     nullptr}};

const FeatureEntry::FeatureParam kCampbell9dot[] = {{"icon", "9dot"}};
const FeatureEntry::FeatureParam kCampbellHero[] = {{"icon", "hero"}};
const FeatureEntry::FeatureParam kCampbellAction[] = {{"icon", "action"}};
const FeatureEntry::FeatureParam kCampbellText[] = {{"icon", "text"}};

const FeatureEntry::FeatureVariation kCampbellGlyphVariations[] = {
    {"9dot", kCampbell9dot, std::size(kCampbell9dot), nullptr},
    {"hero", kCampbellHero, std::size(kCampbellHero), nullptr},
    {"action", kCampbellAction, std::size(kCampbellAction), nullptr},
    {"text", kCampbellText, std::size(kCampbellText), nullptr}};

const FeatureEntry::FeatureParam kCaptureModeEducationShortcutNudge[] = {
    {"CaptureModeEducationParam", "ShortcutNudge"}};
const FeatureEntry::FeatureParam kCaptureModeEducationShortcutTutorial[] = {
    {"CaptureModeEducationParam", "ShortcutTutorial"}};
const FeatureEntry::FeatureParam kCaptureModeEducationQuickSettingsNudge[] = {
    {"CaptureModeEducationParam", "QuickSettingsNudge"}};

const FeatureEntry::FeatureVariation kCaptureModeEducationVariations[] = {
    {"Shortcut Nudge", kCaptureModeEducationShortcutNudge,
     std::size(kCaptureModeEducationShortcutNudge), nullptr},
    {"Shortcut Tutorial", kCaptureModeEducationShortcutTutorial,
     std::size(kCaptureModeEducationShortcutTutorial), nullptr},
    {"Quick Settings Nudge", kCaptureModeEducationQuickSettingsNudge,
     std::size(kCaptureModeEducationQuickSettingsNudge), nullptr}};

const FeatureEntry::FeatureParam
    kSystemShortcutBehaviorIgnoreCommonVdiShortcuts[] = {
        {"behavior_type", "ignore_common_vdi_shortcuts"}};
const FeatureEntry::FeatureParam
    kSystemShortcutBehaviorIgnoreCommonVdiShortcutsFullscreenOnly[] = {
        {"behavior_type", "ignore_common_vdi_shortcut_fullscreen_only"}};
const FeatureEntry::FeatureParam
    kSystemShortcutBehaviorAllowSearchBasedPassthrough[] = {
        {"behavior_type", "allow_search_based_passthrough"}};
const FeatureEntry::FeatureParam
    kSystemShortcutBehaviorAllowSearchBasedPassthroughFullscreenOnly[] = {
        {"behavior_type", "allow_search_based_passthrough_fullscreen_only"}};

const FeatureEntry::FeatureVariation kSystemShortcutBehaviorVariations[] = {
    {"Ignore Common VDI Shortcuts",
     kSystemShortcutBehaviorIgnoreCommonVdiShortcuts,
     std::size(kSystemShortcutBehaviorIgnoreCommonVdiShortcuts), nullptr},
    {"Ignore Common VDI Shortcuts while Fullscreen",
     kSystemShortcutBehaviorIgnoreCommonVdiShortcutsFullscreenOnly,
     std::size(kSystemShortcutBehaviorIgnoreCommonVdiShortcutsFullscreenOnly),
     nullptr},
    {"Allow Search Based Passthrough",
     kSystemShortcutBehaviorAllowSearchBasedPassthrough,
     std::size(kSystemShortcutBehaviorAllowSearchBasedPassthrough), nullptr},
    {"Allow Search Based Passthrough while Fullscreen",
     kSystemShortcutBehaviorAllowSearchBasedPassthroughFullscreenOnly,
     std::size(
         kSystemShortcutBehaviorAllowSearchBasedPassthroughFullscreenOnly),
     nullptr},
};

#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS)
constexpr char kWallpaperFastRefreshInternalName[] = "wallpaper-fast-refresh";
constexpr char kWallpaperGooglePhotosSharedAlbumsInternalName[] =
    "wallpaper-google-photos-shared-albums";
constexpr char kGlanceablesTimeManagementClassroomStudentViewInternalName[] =
    "glanceables-time-management-classroom-student-view";
constexpr char kGlanceablesTimeManagementTasksViewInternalName[] =
    "glanceables-time-management-tasks-view";
constexpr char kBackgroundListeningName[] = "background-listening";
constexpr char kBorealisBigGlInternalName[] = "borealis-big-gl";
constexpr char kBorealisDGPUInternalName[] = "borealis-dgpu";
constexpr char kBorealisEnableUnsupportedHardwareInternalName[] =
    "borealis-enable-unsupported-hardware";
constexpr char kBorealisForceBetaClientInternalName[] =
    "borealis-force-beta-client";
constexpr char kBorealisForceDoubleScaleInternalName[] =
    "borealis-force-double-scale";
constexpr char kBorealisLinuxModeInternalName[] = "borealis-linux-mode";
// This differs slightly from its symbol's name since "enabled" is used
// internally to refer to whether borealis is installed or not.
constexpr char kBorealisPermittedInternalName[] = "borealis-enabled";
constexpr char kBorealisProvisionInternalName[] = "borealis-provision";
constexpr char kBorealisScaleClientByDPIInternalName[] =
    "borealis-scale-client-by-dpi";
constexpr char kBorealisZinkGlDriverInternalName[] = "borealis-zink-gl-driver";
constexpr char kClipboardHistoryLongpressInternalName[] =
    "clipboard-history-longpress";
constexpr char kBluetoothUseFlossInternalName[] = "bluetooth-use-floss";
constexpr char kBluetoothUseLLPrivacyInternalName[] = "bluetooth-use-llprivacy";
constexpr char kAssistantIphInternalName[] = "assistant-iph";
constexpr char kGrowthCampaigns[] = "growth-campaigns";
constexpr char kGrowthCampaignsTestTag[] = "campaigns-test-tag";
constexpr char kVcTrayMicIndicatorInternalName[] = "vc-tray-mic-indicator";
constexpr char kVcTrayTitleHeaderInternalName[] = "vc-tray-title-header";
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kLensOverlayNoOmniboxEntryPoint[] = {
    {"omnibox-entry-point", "false"},
};
const FeatureEntry::FeatureParam kLensOverlayResponsiveOmniboxEntryPoint[] = {
    {"omnibox-entry-point", "true"},
    {"omnibox-entry-point-always-visible", "false"},
};
const FeatureEntry::FeatureParam kLensOverlayPersistentOmniboxEntryPoint[] = {
    {"omnibox-entry-point", "true"},
    {"omnibox-entry-point-always-visible", "true"},
};

const FeatureEntry::FeatureVariation kLensOverlayVariations[] = {
    {"with no omnibox entry point", kLensOverlayNoOmniboxEntryPoint,
     std::size(kLensOverlayNoOmniboxEntryPoint), nullptr},
    {"with responsive chip omnibox entry point",
     kLensOverlayResponsiveOmniboxEntryPoint,
     std::size(kLensOverlayResponsiveOmniboxEntryPoint), nullptr},
    {"with persistent icon omnibox entry point",
     kLensOverlayPersistentOmniboxEntryPoint,
     std::size(kLensOverlayPersistentOmniboxEntryPoint), nullptr},
};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kDeleteLegacyMigratedTabStatesAfterRestore[] =
    {
        {"delete_migrated_files_after_restore", "true"},
};

const FeatureEntry::FeatureVariation kLegacyTabStateDeprecationVariations[] = {
    {"Delete migrated files", kDeleteLegacyMigratedTabStatesAfterRestore,
     std::size(kDeleteLegacyMigratedTabStatesAfterRestore), nullptr},
};
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kLensOverlayImageContextMenuActionsCopy[] = {
    {"enable-copy-as-image", "true"},
    {"enable-save-as-image", "false"},
};

const FeatureEntry::FeatureParam kLensOverlayImageContextMenuActionsSave[] = {
    {"enable-copy-as-image", "false"},
    {"enable-save-as-image", "true"},
};

const FeatureEntry::FeatureParam
    kLensOverlayImageContextMenuActionsCopyAndSave[] = {
        {"enable-copy-as-image", "true"},
        {"enable-save-as-image", "true"},
};

const FeatureEntry::FeatureVariation
    kLensOverlayImageContextMenuActionsVariations[] = {
        {"copy as image", kLensOverlayImageContextMenuActionsCopy,
         std::size(kLensOverlayImageContextMenuActionsCopy), nullptr},
        {"save as image", kLensOverlayImageContextMenuActionsSave,
         std::size(kLensOverlayImageContextMenuActionsSave), nullptr},
        {"copy and save as image",
         kLensOverlayImageContextMenuActionsCopyAndSave,
         std::size(kLensOverlayImageContextMenuActionsCopyAndSave), nullptr},
};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::Choice kAlwaysEnableHdcpChoices[] = {
    {flag_descriptions::kAlwaysEnableHdcpDefault, "", ""},
    {flag_descriptions::kAlwaysEnableHdcpType0,
     ash::switches::kAlwaysEnableHdcp, "type0"},
    {flag_descriptions::kAlwaysEnableHdcpType1,
     ash::switches::kAlwaysEnableHdcp, "type1"},
};

const FeatureEntry::Choice kPrintingPpdChannelChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {ash::switches::kPrintingPpdChannelProduction,
     ash::switches::kPrintingPpdChannel,
     ash::switches::kPrintingPpdChannelProduction},
    {ash::switches::kPrintingPpdChannelStaging,
     ash::switches::kPrintingPpdChannel,
     ash::switches::kPrintingPpdChannelStaging},
    {ash::switches::kPrintingPpdChannelDev, ash::switches::kPrintingPpdChannel,
     ash::switches::kPrintingPpdChannelDev},
    {ash::switches::kPrintingPpdChannelLocalhost,
     ash::switches::kPrintingPpdChannel,
     ash::switches::kPrintingPpdChannelLocalhost}};
#endif  // BUILDFLAG(IS_CHROMEOS)

// Feature variations for kIsolateSandboxedIframes.
const FeatureEntry::FeatureParam kIsolateSandboxedIframesGroupingPerSite{
    "grouping", "per-site"};
const FeatureEntry::FeatureParam kIsolateSandboxedIframesGroupingPerOrigin{
    "grouping", "per-origin"};
const FeatureEntry::FeatureParam kIsolateSandboxedIframesGroupingPerDocument{
    "grouping", "per-document"};
const FeatureEntry::FeatureVariation
    kIsolateSandboxedIframesGroupingVariations[] = {
        {"with grouping by URL's site",
         &kIsolateSandboxedIframesGroupingPerSite, 1, nullptr},
        {"with grouping by URL's origin",
         &kIsolateSandboxedIframesGroupingPerOrigin, 1, nullptr},
        {"with each sandboxed frame document in its own process",
         &kIsolateSandboxedIframesGroupingPerDocument, 1, nullptr},
};

// Feature variation for kPdfInk2.
#if BUILDFLAG(ENABLE_PDF_INK2)
const FeatureEntry::FeatureParam kPdfInk2TextHighlighting[] = {
    {"text-annotations", "false"},
    {"text-highlighting", "true"},
};
const FeatureEntry::FeatureParam kPdfInk2TextAnnotations[] = {
    {"text-annotations", "true"},
    {"text-highlighting", "false"},
};
const FeatureEntry::FeatureParam kPdfInk2TextHighlightingAndAnnotations[] = {
    {"text-annotations", "true"},
    {"text-highlighting", "true"},
};

const FeatureEntry::FeatureVariation kPdfInk2Variations[] = {
    {"with text highlighting", kPdfInk2TextHighlighting,
     std::size(kPdfInk2TextHighlighting), nullptr},
    {"with text annotations", kPdfInk2TextAnnotations,
     std::size(kPdfInk2TextAnnotations), nullptr},
    {"with text highlighting and annotations",
     kPdfInk2TextHighlightingAndAnnotations,
     std::size(kPdfInk2TextHighlightingAndAnnotations), nullptr},
};
#endif  // BUILDFLAG(ENABLE_PDF_INK2)

const FeatureEntry::FeatureParam kWebRtcApmDownmixMethodAverage[] = {
    {"method", "average"}};
const FeatureEntry::FeatureParam kWebRtcApmDownmixMethodFirstChannel[] = {
    {"method", "first"}};
const FeatureEntry::FeatureVariation kWebRtcApmDownmixMethodVariations[] = {
    {"- Average all the input channels", kWebRtcApmDownmixMethodAverage,
     std::size(kWebRtcApmDownmixMethodAverage), nullptr},
    {"- Use first channel", kWebRtcApmDownmixMethodFirstChannel,
     std::size(kWebRtcApmDownmixMethodFirstChannel), nullptr}};

const FeatureEntry::FeatureParam
    kSafetyCheckUnusedSitePermissionsNoDelayParam[] = {
        {"unused-site-permissions-no-delay-for-testing", "true"}};

const FeatureEntry::FeatureParam
    kSafetyCheckUnusedSitePermissionsWithDelayParam[] = {
        {"unused-site-permissions-with-delay-for-testing", "true"}};

const FeatureEntry::FeatureVariation
    kSafetyCheckUnusedSitePermissionsVariations[] = {
        {"for testing no delay", kSafetyCheckUnusedSitePermissionsNoDelayParam,
         std::size(kSafetyCheckUnusedSitePermissionsNoDelayParam), nullptr},
        {"for testing with delay",
         kSafetyCheckUnusedSitePermissionsWithDelayParam,
         std::size(kSafetyCheckUnusedSitePermissionsWithDelayParam), nullptr},
};

const FeatureEntry::FeatureParam kSafetyHub_NoDelay[] = {
    {features::kPasswordCheckNotificationIntervalName, "0d"},
    {features::kRevokedPermissionsNotificationIntervalName, "0d"},
    {features::kNotificationPermissionsNotificationIntervalName, "0d"},
    {features::kSafeBrowsingNotificationIntervalName, "0d"}};
const FeatureEntry::FeatureParam kSafetyHub_WithDelay[] = {
    {features::kPasswordCheckNotificationIntervalName, "0d"},
    {features::kRevokedPermissionsNotificationIntervalName, "5m"},
    {features::kNotificationPermissionsNotificationIntervalName, "5m"},
    {features::kSafeBrowsingNotificationIntervalName, "5m"}};
const FeatureEntry::FeatureVariation kSafetyHubVariations[] = {
    {"for testing no delay", kSafetyHub_NoDelay, std::size(kSafetyHub_NoDelay),
     nullptr},
    {"for testing with delay", kSafetyHub_WithDelay,
     std::size(kSafetyHub_WithDelay), nullptr},
};

const FeatureEntry::FeatureParam kTPCPhaseOutFacilitatedTestingControl1[] = {
    {tpcd::experiment::kForceEligibleForTestingName, "false"},
    {tpcd::experiment::kDisable3PCookiesName, "false"},
    {features::kCookieDeprecationTestingDisableAdsAPIsName, "false"},
    {features::kCookieDeprecationLabelName, "fake_control_1.1"},
    {tpcd::experiment::kVersionName, "9990"}};
const FeatureEntry::FeatureParam kTPCPhaseOutFacilitatedTestingLabelOnly[] = {
    {tpcd::experiment::kForceEligibleForTestingName, "false"},
    {tpcd::experiment::kDisable3PCookiesName, "false"},
    {features::kCookieDeprecationTestingDisableAdsAPIsName, "false"},
    {features::kCookieDeprecationLabelName, "fake_label_only_1.1"},
    {tpcd::experiment::kVersionName, "9991"}};
const FeatureEntry::FeatureParam kTPCPhaseOutFacilitatedTestingTreatment[] = {
    {tpcd::experiment::kForceEligibleForTestingName, "false"},
    {tpcd::experiment::kDisable3PCookiesName, "true"},
    {features::kCookieDeprecationTestingDisableAdsAPIsName, "false"},
    {features::kCookieDeprecationLabelName, "fake_treatment_1.1"},
    {tpcd::experiment::kVersionName, "9992"}};
const FeatureEntry::FeatureParam kTPCPhaseOutFacilitatedTestingControl2[] = {
    {tpcd::experiment::kForceEligibleForTestingName, "false"},
    {tpcd::experiment::kDisable3PCookiesName, "true"},
    {features::kCookieDeprecationTestingDisableAdsAPIsName, "true"},
    {features::kCookieDeprecationLabelName, "fake_control_2"},
    {tpcd::experiment::kVersionName, "9993"}};
const FeatureEntry::FeatureParam kTPCPhaseOutFacilitatedTestingForceControl1[] =
    {{tpcd::experiment::kForceEligibleForTestingName, "true"},
     {tpcd::experiment::kDisable3PCookiesName, "false"},
     {features::kCookieDeprecationTestingDisableAdsAPIsName, "false"},
     {features::kCookieDeprecationLabelName, "fake_control_1.1"},
     {tpcd::experiment::kVersionName, "9994"}};
const FeatureEntry::FeatureParam
    kTPCPhaseOutFacilitatedTestingForceLabelOnly[] = {
        {tpcd::experiment::kForceEligibleForTestingName, "true"},
        {tpcd::experiment::kDisable3PCookiesName, "false"},
        {features::kCookieDeprecationTestingDisableAdsAPIsName, "false"},
        {features::kCookieDeprecationLabelName, "fake_label_only_1.1"},
        {tpcd::experiment::kVersionName, "9995"}};
const FeatureEntry::FeatureParam
    kTPCPhaseOutFacilitatedTestingForceTreatment[] = {
        {tpcd::experiment::kForceEligibleForTestingName, "true"},
        {tpcd::experiment::kDisable3PCookiesName, "true"},
        {features::kCookieDeprecationTestingDisableAdsAPIsName, "false"},
        {features::kCookieDeprecationLabelName, "fake_treatment_1.1"},
        {tpcd::experiment::kVersionName, "9996"}};
const FeatureEntry::FeatureParam kTPCPhaseOutFacilitatedTestingForceControl2[] =
    {{tpcd::experiment::kForceEligibleForTestingName, "true"},
     {tpcd::experiment::kDisable3PCookiesName, "true"},
     {features::kCookieDeprecationTestingDisableAdsAPIsName, "true"},
     {features::kCookieDeprecationLabelName, "fake_control_2"},
     {tpcd::experiment::kVersionName, "9997"}};

const FeatureEntry::FeatureVariation
    kTPCPhaseOutFacilitatedTestingVariations[] = {
        {"Control 1", kTPCPhaseOutFacilitatedTestingControl1,
         std::size(kTPCPhaseOutFacilitatedTestingControl1), nullptr},
        {"LabelOnly", kTPCPhaseOutFacilitatedTestingLabelOnly,
         std::size(kTPCPhaseOutFacilitatedTestingLabelOnly), nullptr},
        {"Treatment", kTPCPhaseOutFacilitatedTestingTreatment,
         std::size(kTPCPhaseOutFacilitatedTestingTreatment), nullptr},
        {"Control 2", kTPCPhaseOutFacilitatedTestingControl2,
         std::size(kTPCPhaseOutFacilitatedTestingControl2), nullptr},
        {"Force Control 1", kTPCPhaseOutFacilitatedTestingForceControl1,
         std::size(kTPCPhaseOutFacilitatedTestingForceControl1), nullptr},
        {"Force LabelOnly", kTPCPhaseOutFacilitatedTestingForceLabelOnly,
         std::size(kTPCPhaseOutFacilitatedTestingForceLabelOnly), nullptr},
        {"Force Treatment", kTPCPhaseOutFacilitatedTestingForceTreatment,
         std::size(kTPCPhaseOutFacilitatedTestingForceTreatment), nullptr},
        {"Force Control 2", kTPCPhaseOutFacilitatedTestingForceControl2,
         std::size(kTPCPhaseOutFacilitatedTestingForceControl2), nullptr},
};

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kTabStateFlatBufferMigrateStaleTabs[] = {
    {"migrate_stale_tabs", "true"}};

const FeatureEntry::FeatureVariation kTabStateFlatBufferVariations[] = {
    {"Migrate Stale Tabs", kTabStateFlatBufferMigrateStaleTabs,
     std::size(kTabStateFlatBufferMigrateStaleTabs), nullptr}};
#endif

const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_MainFrameInitiator
        [] = {
            {content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
            {content_settings::features::
                 kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
             "30d"},
            {content_settings::features::kTpcdBackfillPopupHeuristicsGrantsName,
             "30d"},
            {content_settings::features::
                 kTpcdPopupHeuristicEnableForIframeInitiatorName,
             "none"},
            {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
             "15m"},
            {content_settings::features::
                 kTpcdRedirectHeuristicRequireABAFlowName,
             "true"},
            {content_settings::features::
                 kTpcdRedirectHeuristicRequireCurrentInteractionName,
             "true"}};
const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_MainFrameInitiator[] =
        {{content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
         {content_settings::features::
              kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
          "30d"},
         {content_settings::features::kTpcdBackfillPopupHeuristicsGrantsName,
          "30d"},
         {content_settings::features::
              kTpcdPopupHeuristicEnableForIframeInitiatorName,
          "none"},
         {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
          "30d"},
         {content_settings::features::kTpcdRedirectHeuristicRequireABAFlowName,
          "true"},
         {content_settings::features::
              kTpcdRedirectHeuristicRequireCurrentInteractionName,
          "true"}};
const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_AllFrameInitiator[] =
        {{content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
         {content_settings::features::
              kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
          "30d"},
         {content_settings::features::kTpcdBackfillPopupHeuristicsGrantsName,
          "30d"},
         {content_settings::features::
              kTpcdPopupHeuristicEnableForIframeInitiatorName,
          "all"},
         {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
          "15m"},
         {content_settings::features::kTpcdRedirectHeuristicRequireABAFlowName,
          "true"},
         {content_settings::features::
              kTpcdRedirectHeuristicRequireCurrentInteractionName,
          "true"}};
const FeatureEntry::FeatureParam
    kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_AllFrameInitiator[] =
        {{content_settings::features::kTpcdReadHeuristicsGrantsName, "true"},
         {content_settings::features::
              kTpcdWritePopupCurrentInteractionHeuristicsGrantsName,
          "30d"},
         {content_settings::features::kTpcdBackfillPopupHeuristicsGrantsName,
          "30d"},
         {content_settings::features::
              kTpcdPopupHeuristicEnableForIframeInitiatorName,
          "all"},
         {content_settings::features::kTpcdWriteRedirectHeuristicGrantsName,
          "30d"},
         {content_settings::features::kTpcdRedirectHeuristicRequireABAFlowName,
          "true"},
         {content_settings::features::
              kTpcdRedirectHeuristicRequireCurrentInteractionName,
          "true"}};

const FeatureEntry::FeatureVariation kTpcdHeuristicsGrantsVariations[] = {
    {"CurrentInteraction_ShortRedirect_MainFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_MainFrameInitiator,
     std::size(
         kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_MainFrameInitiator),
     nullptr},
    {"CurrentInteraction_LongRedirect_MainFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_MainFrameInitiator,
     std::size(
         kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_MainFrameInitiator),
     nullptr},
    {"CurrentInteraction_ShortRedirect_AllFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_AllFrameInitiator,
     std::size(
         kTpcdHeuristicsGrants_CurrentInteraction_ShortRedirect_AllFrameInitiator),
     nullptr},
    {"CurrentInteraction_LongRedirect_AllFrameInitiator",
     kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_AllFrameInitiator,
     std::size(
         kTpcdHeuristicsGrants_CurrentInteraction_LongRedirect_AllFrameInitiator),
     nullptr}};

#if BUILDFLAG(IS_CHROMEOS)
const FeatureEntry::FeatureParam kVcInferenceBackendAuto[] = {
    {"inference_backend", "AUTO"},
};

const FeatureEntry::FeatureParam kVcInferenceBackendGpu[] = {
    {"inference_backend", "GPU"},
};

const FeatureEntry::FeatureParam kVcInferenceBackendNpu[] = {
    {"inference_backend", "NPU"},
};

const FeatureEntry::FeatureVariation kVcRelightingInferenceBackendVariations[] =
    {{"AUTO", kVcInferenceBackendAuto, std::size(kVcInferenceBackendAuto),
      nullptr},
     {"GPU", kVcInferenceBackendGpu, std::size(kVcInferenceBackendGpu),
      nullptr},
     {"NPU", kVcInferenceBackendNpu, std::size(kVcInferenceBackendNpu),
      nullptr}};

const FeatureEntry::FeatureVariation kVcRetouchInferenceBackendVariations[] = {
    {"AUTO", kVcInferenceBackendAuto, std::size(kVcInferenceBackendAuto),
     nullptr},
    {"GPU", kVcInferenceBackendGpu, std::size(kVcInferenceBackendGpu), nullptr},
    {"NPU", kVcInferenceBackendNpu, std::size(kVcInferenceBackendNpu),
     nullptr}};

const FeatureEntry::FeatureVariation
    kVcSegmentationInferenceBackendVariations[] = {
        {"AUTO", kVcInferenceBackendAuto, std::size(kVcInferenceBackendAuto),
         nullptr},
        {"GPU", kVcInferenceBackendGpu, std::size(kVcInferenceBackendGpu),
         nullptr},
        {"NPU", kVcInferenceBackendNpu, std::size(kVcInferenceBackendNpu),
         nullptr}};

const FeatureEntry::FeatureParam kVcSegmentationModelHighResolution[] = {
    {"segmentation_model", "high_resolution"},
};

const FeatureEntry::FeatureParam kVcSegmentationModelLowerResolution[] = {
    {"segmentation_model", "lower_resolution"},
};

const FeatureEntry::FeatureVariation kVcSegmentationModelVariations[] = {
    {"High resolution model", kVcSegmentationModelHighResolution,
     std::size(kVcSegmentationModelHighResolution), nullptr},
    {"Lower resolution model", kVcSegmentationModelLowerResolution,
     std::size(kVcSegmentationModelLowerResolution), nullptr},
};

const FeatureEntry::FeatureParam kVcLightIntensity10[] = {
    {"light_intensity", "1.0"},
};

const FeatureEntry::FeatureParam kVcLightIntensity13[] = {
    {"light_intensity", "1.3"},
};

const FeatureEntry::FeatureParam kVcLightIntensity15[] = {
    {"light_intensity", "1.5"},
};

const FeatureEntry::FeatureParam kVcLightIntensity17[] = {
    {"light_intensity", "1.7"},
};

const FeatureEntry::FeatureParam kVcLightIntensity18[] = {
    {"light_intensity", "1.8"},
};

const FeatureEntry::FeatureParam kVcLightIntensity20[] = {
    {"light_intensity", "2.0"},
};

const FeatureEntry::FeatureVariation kVcLightIntensityVariations[] = {
    {"1.0", kVcLightIntensity10, std::size(kVcLightIntensity10), nullptr},
    {"1.3", kVcLightIntensity13, std::size(kVcLightIntensity13), nullptr},
    {"1.5", kVcLightIntensity15, std::size(kVcLightIntensity15), nullptr},
    {"1.7", kVcLightIntensity17, std::size(kVcLightIntensity17), nullptr},
    {"1.8", kVcLightIntensity18, std::size(kVcLightIntensity18), nullptr},
    {"2.0", kVcLightIntensity20, std::size(kVcLightIntensity20), nullptr},
};

const FeatureEntry::FeatureParam
    kCrOSLateBootMissiveDisableStorageDegradation[] = {
        {"controlled_degradation", "false"}};
const FeatureEntry::FeatureParam
    kCrOSLateBootMissiveEnableStorageDegradation[] = {
        {"controlled_degradation", "true"}};
const FeatureEntry::FeatureParam kCrOSLateBootMissiveDisableLegacyStorage[] = {
    {"legacy_storage_enabled",
     "UNDEFINED_PRIORITY"}};  // All others are multi-generation action state.
const FeatureEntry::FeatureParam kCrOSLateBootMissiveEnableLegacyStorage[] = {
    {"legacy_storage_enabled",
     "SECURITY,"
     "IMMEDIATE,"
     "FAST_BATCH,"
     "SLOW_BATCH,"
     "BACKGROUND_BATCH,"
     "MANUAL_BATCH,"
     "MANUAL_BATCH_LACROS,"}};
const FeatureEntry::FeatureParam kCrOSLateBootMissivePartialLegacyStorage[] = {
    {"legacy_storage_enabled",
     "SECURITY,"
     "IMMEDIATE,"}};
const FeatureEntry::FeatureParam kCrOSLateBootMissiveSecurityLegacyStorage[] = {
    {"legacy_storage_enabled", "SECURITY,"}};

const FeatureEntry::FeatureVariation
    kCrOSLateBootMissiveStorageDefaultVariations[] = {
        {"Enable storage degradation",
         kCrOSLateBootMissiveEnableStorageDegradation,
         std::size(kCrOSLateBootMissiveEnableStorageDegradation), nullptr},
        {"Disable storage degradation",
         kCrOSLateBootMissiveDisableStorageDegradation,
         std::size(kCrOSLateBootMissiveDisableStorageDegradation), nullptr},
        {"Enable all queues legacy", kCrOSLateBootMissiveEnableLegacyStorage,
         std::size(kCrOSLateBootMissiveEnableLegacyStorage), nullptr},
        {"Disable all queues legacy", kCrOSLateBootMissiveDisableLegacyStorage,
         std::size(kCrOSLateBootMissiveDisableLegacyStorage), nullptr},
        {"Enable SECURITY and IMMEDIATE queues legacy only",
         kCrOSLateBootMissivePartialLegacyStorage,
         std::size(kCrOSLateBootMissivePartialLegacyStorage), nullptr},
        {"Enable SECURITY queues legacy only",
         kCrOSLateBootMissiveSecurityLegacyStorage,
         std::size(kCrOSLateBootMissiveSecurityLegacyStorage), nullptr},
};
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::Choice kCastMirroringTargetPlayoutDelayChoices[] = {
    {flag_descriptions::kCastMirroringTargetPlayoutDelayDefault, "", ""},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay100ms,
     switches::kCastMirroringTargetPlayoutDelay, "100"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay150ms,
     switches::kCastMirroringTargetPlayoutDelay, "150"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay250ms,
     switches::kCastMirroringTargetPlayoutDelay, "250"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay300ms,
     switches::kCastMirroringTargetPlayoutDelay, "300"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay350ms,
     switches::kCastMirroringTargetPlayoutDelay, "350"},
    {flag_descriptions::kCastMirroringTargetPlayoutDelay400ms,
     switches::kCastMirroringTargetPlayoutDelay, "400"}};

#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
const FeatureEntry::FeatureParam
    kEnableBoundSessionCredentialsWithMultiSessionSupport[] = {
        {"exclusive-registration-path", ""}};

const FeatureEntry::FeatureVariation
    kEnableBoundSessionCredentialsVariations[] = {
        {"with multi-session",
         kEnableBoundSessionCredentialsWithMultiSessionSupport,
         std::size(kEnableBoundSessionCredentialsWithMultiSessionSupport),
         nullptr}};
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kEdgeToEdgeBottomChinDebugFeatureParams[] = {
    {chrome::android::kEdgeToEdgeBottomChinDebugParam.name, "true"}};
const FeatureEntry::FeatureVariation kEdgeToEdgeBottomChinVariations[] = {
    {"debug", kEdgeToEdgeBottomChinDebugFeatureParams,
     std::size(kEdgeToEdgeBottomChinDebugFeatureParams), nullptr},
};

const FeatureEntry::FeatureParam kEdgeToEdgeEverywhereDebugFeatureParams[] = {
    {"e2e_everywhere_debug", "true"}};
const FeatureEntry::FeatureVariation kEdgeToEdgeEverywhereVariations[] = {
    {"debug", kEdgeToEdgeEverywhereDebugFeatureParams,
     std::size(kEdgeToEdgeEverywhereDebugFeatureParams), nullptr},
};

const FeatureEntry::FeatureParam kEdgeToEdgeSafeAreaConstraintFeatureParams[] =
    {{"scrollable_when_stacking", "true"}};
const FeatureEntry::FeatureVariation kEdgeToEdgeSafeAreaConstraintVariations[] =
    {
        {"scrollable variation", kEdgeToEdgeSafeAreaConstraintFeatureParams,
         std::size(kEdgeToEdgeSafeAreaConstraintFeatureParams), nullptr},
};

const FeatureEntry::FeatureParam kBottomBrowserControlsRefactorParams[] = {
    {"disable_bottom_controls_stacker_y_offset", "false"}};
const FeatureEntry::FeatureVariation
    kBottomBrowserControlsRefactorVariations[] = {
        {"Dispatch yOffset", kBottomBrowserControlsRefactorParams,
         std::size(kBottomBrowserControlsRefactorParams), nullptr},
};

const FeatureEntry::FeatureParam sAndroidThemeModuleParams[] = {
    {"force_theme_module_dependencies", "true"}};
const FeatureEntry::FeatureVariation kAndroidThemeModuleVariations[] = {
    {"force dependencies", sAndroidThemeModuleParams,
     std::size(sAndroidThemeModuleParams), nullptr},
};

const FeatureEntry::FeatureParam
    kAuxiliaryNavigationStaysInBrowserOnForDesktopWindowing[] = {
        {"auxiliary_navigation_stays_in_browser", "desktop_wm"}};
const FeatureEntry::FeatureParam kAuxiliaryNavigationStaysInBrowserOn[] = {
    {"auxiliary_navigation_stays_in_browser", "all_wm"}};
const FeatureEntry::FeatureVariation
    kAuxiliaryNavigationStaysInBrowserVariations[] = {
        {" - desktop windowing mode",
         kAuxiliaryNavigationStaysInBrowserOnForDesktopWindowing,
         std::size(kAuxiliaryNavigationStaysInBrowserOnForDesktopWindowing),
         nullptr},
        {" - all windowing modes", kAuxiliaryNavigationStaysInBrowserOn,
         std::size(kAuxiliaryNavigationStaysInBrowserOn), nullptr}};

#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
const flags_ui::FeatureEntry::FeatureParam kPwaNavigationCapturingDefaultOn[] =
    {{"link_capturing_state", "on_by_default"}};
const flags_ui::FeatureEntry::FeatureParam kPwaNavigationCapturingDefaultOff[] =
    {{"link_capturing_state", "off_by_default"}};
const flags_ui::FeatureEntry::FeatureParam
    kPwaNavigationCapturingReimplDefaultOn[] = {
        {"link_capturing_state", "reimpl_default_on"}};
const flags_ui::FeatureEntry::FeatureParam
    kPwaNavigationCapturingReimplDefaultOff[] = {
        {"link_capturing_state", "reimpl_default_off"}};
const flags_ui::FeatureEntry::FeatureParam
    kPwaNavigationCapturingReimplOnViaClientMode[] = {
        {"link_capturing_state", "reimpl_on_via_client_mode"}};
const flags_ui::FeatureEntry::FeatureVariation
    kPwaNavigationCapturingVariations[] = {
        {"V1, On by default", kPwaNavigationCapturingDefaultOn,
         std::size(kPwaNavigationCapturingDefaultOn), nullptr},
        {"V1, Off by default", kPwaNavigationCapturingDefaultOff,
         std::size(kPwaNavigationCapturingDefaultOff), nullptr},
        {"V2, On by default", kPwaNavigationCapturingReimplDefaultOn,
         std::size(kPwaNavigationCapturingReimplDefaultOn), nullptr},
        {"V2, Off by default", kPwaNavigationCapturingReimplDefaultOff,
         std::size(kPwaNavigationCapturingReimplDefaultOff), nullptr},
        {"V2, On by app client_mode",
         kPwaNavigationCapturingReimplOnViaClientMode,
         std::size(kPwaNavigationCapturingReimplOnViaClientMode), nullptr}};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)

// UnoPhase2FollowUp flags.
const char kFastFollowFeatures[] =
    "UnoForAuto,"
    "UnoPhase2FollowUp,"
    "UseHostedDomainForManagementCheckOnSignin";

const FeatureEntry::Choice kReplaceSyncPromosWithSignInPromosChoices[] = {
    {"Default", "", ""},
    {"Follow-ups disabled", "disable-features", kFastFollowFeatures},
    {"Follow-ups enabled", "enable-features", kFastFollowFeatures},
};
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kLinkPreviewTriggerTypeAltClick[] = {
    {"trigger_type", "alt_click"}};
const FeatureEntry::FeatureParam kLinkPreviewTriggerTypeAltHover[] = {
    {"trigger_type", "alt_hover"}};
const FeatureEntry::FeatureParam kLinkPreviewTriggerTypeLongPress[] = {
    {"trigger_type", "long_press"}};

const FeatureEntry::FeatureVariation kLinkPreviewTriggerTypeVariations[] = {
    {"Alt + Click", kLinkPreviewTriggerTypeAltClick,
     std::size(kLinkPreviewTriggerTypeAltClick), nullptr},
    {"Alt + Hover", kLinkPreviewTriggerTypeAltHover,
     std::size(kLinkPreviewTriggerTypeAltHover), nullptr},
    {"Long Press", kLinkPreviewTriggerTypeLongPress,
     std::size(kLinkPreviewTriggerTypeLongPress), nullptr}};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
inline constexpr flags_ui::FeatureEntry::FeatureParam
    kAutofillVirtualViewStructureAndroidSkipCompatibilityCheck = {
        autofill::features::
            kAutofillVirtualViewStructureAndroidSkipsCompatibilityCheck.name,
        "skip_all_checks"};
inline constexpr flags_ui::FeatureEntry::FeatureParam
    kAutofillVirtualViewStructureAndroidOnlySkipAwgCheck = {
        autofill::features::
            kAutofillVirtualViewStructureAndroidSkipsCompatibilityCheck.name,
        "only_skip_awg_check"};

inline constexpr flags_ui::FeatureEntry::FeatureVariation
    kAutofillVirtualViewStructureVariation[] = {
        {" without any compatibility check",
         &kAutofillVirtualViewStructureAndroidSkipCompatibilityCheck, 1,
         nullptr},
        {" without AwG restriction",
         &kAutofillVirtualViewStructureAndroidOnlySkipAwgCheck, 1, nullptr}};

#endif  // BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kGroupSuggestionEnableRecentlyOpenedOnly[] = {
    {"group_suggestion_enable_recently_opened", "true"},
    {"group_suggestion_enable_switch_between", "false"},
    {"group_suggestion_enable_similar_source", "false"},
    {"group_suggestion_enable_same_origin", "false"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableSwitchBetweenOnly[] = {
    {"group_suggestion_enable_recently_opened", "false"},
    {"group_suggestion_enable_switch_between", "true"},
    {"group_suggestion_enable_similar_source", "false"},
    {"group_suggestion_enable_same_origin", "false"},
    {"group_suggestion_trigger_calculation_on_page_load", "false"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableSimilarSourceOnly[] = {
    {"group_suggestion_enable_recently_opened", "false"},
    {"group_suggestion_enable_switch_between", "false"},
    {"group_suggestion_enable_similar_source", "true"},
    {"group_suggestion_enable_same_origin", "false"},
    {"group_suggestion_trigger_calculation_on_page_load", "false"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableSameOriginOnly[] = {
    {"group_suggestion_enable_recently_opened", "false"},
    {"group_suggestion_enable_switch_between", "false"},
    {"group_suggestion_enable_similar_source", "false"},
    {"group_suggestion_enable_same_origin", "true"},
};
const FeatureEntry::FeatureParam kGroupSuggestionEnableTabSwitcherOnly[] = {
    {"group_suggestion_enable_tab_switcher_only", "true"},
};
const FeatureEntry::FeatureVariation kGroupSuggestionVariations[] = {
    {"Recently Opened Only", kGroupSuggestionEnableRecentlyOpenedOnly,
     std::size(kGroupSuggestionEnableRecentlyOpenedOnly), nullptr},
    {"Switch Between Only", kGroupSuggestionEnableSwitchBetweenOnly,
     std::size(kGroupSuggestionEnableSwitchBetweenOnly), nullptr},
    {"Similar Source Only", kGroupSuggestionEnableSimilarSourceOnly,
     std::size(kGroupSuggestionEnableSimilarSourceOnly), nullptr},
    {"Same Origin Only", kGroupSuggestionEnableSameOriginOnly,
     std::size(kGroupSuggestionEnableSameOriginOnly), nullptr},
    {"Tab Switcher Only", kGroupSuggestionEnableTabSwitcherOnly,
     std::size(kGroupSuggestionEnableTabSwitcherOnly), nullptr},
};

#if BUILDFLAG(ENABLE_COMPOSE)
// Vatiations of the Compose proactive nudge.
const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactNoFocusDelay_10[] = {
        {"proactive_nudge_focus_delay_milliseconds", "0"},
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "10"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactNoFocusDelay_10_5[] = {
        {"proactive_nudge_focus_delay_milliseconds", "0"},
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "10"},
        {"nudge_field_change_event_max", "5"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactNoFocusDelay_10_10[] = {
        {"proactive_nudge_focus_delay_milliseconds", "0"},
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "10"},
        {"nudge_field_change_event_max", "10"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactNoFocusDelay_50[] = {
        {"proactive_nudge_focus_delay_milliseconds", "0"},
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "50"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactLongFocusDelay_10[] = {
        {"proactive_nudge_focus_delay_milliseconds", "60000"},  // one minute
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "10"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactLongFocusDelay_50[] = {
        {"proactive_nudge_focus_delay_milliseconds", "60000"},  // one minute
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "50"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactLongTextDelay_10[] = {
        {"proactive_nudge_focus_delay_milliseconds", "0"},
        {"proactive_nudge_text_settled_delay_milliseconds", "10000"},
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "10"}};

const FeatureEntry::FeatureParam
    kComposeProactiveNudge_CompactLongTextDelay_50[] = {
        {"proactive_nudge_focus_delay_milliseconds", "0"},
        {"proactive_nudge_text_settled_delay_milliseconds", "10000"},
        {"proactive_nudge_compact_ui", "true"},
        {"proactive_nudge_text_change_count", "50"}};

const FeatureEntry::FeatureParam kComposeProactiveNudge_LargeNoFocusDelay_10[] =
    {{"proactive_nudge_focus_delay_milliseconds", "0"},
     {"proactive_nudge_compact_ui", "false"},
     {"proactive_nudge_text_change_count", "10"}};

const FeatureEntry::FeatureParam kComposeProactiveNudge_LargeNoFocusDelay_50[] =
    {{"proactive_nudge_focus_delay_milliseconds", "0"},
     {"proactive_nudge_compact_ui", "false"},
     {"proactive_nudge_text_change_count", "50"}};

const FeatureEntry::FeatureVariation kComposeProactiveNudgeVariations[] = {
    {"Compact UI - No focus delay - Show (10 edits)",
     kComposeProactiveNudge_CompactNoFocusDelay_10,
     std::size(kComposeProactiveNudge_CompactNoFocusDelay_10), nullptr},
    {"Compact UI - No focus delay - Show (10 edits) - Dismiss (5 edits)",
     kComposeProactiveNudge_CompactNoFocusDelay_10_5,
     std::size(kComposeProactiveNudge_CompactNoFocusDelay_10_5), nullptr},
    {"Compact UI - No focus delay - Show (10 edits) - Dismiss (10 edits)",
     kComposeProactiveNudge_CompactNoFocusDelay_10_10,
     std::size(kComposeProactiveNudge_CompactNoFocusDelay_10_10), nullptr},
    {"Compact UI - No focus delay - Show (50 edits)",
     kComposeProactiveNudge_CompactNoFocusDelay_50,
     std::size(kComposeProactiveNudge_CompactNoFocusDelay_50), nullptr},
    {"Compact UI - Long focus delay - Show (10 edits)",
     kComposeProactiveNudge_CompactLongFocusDelay_10,
     std::size(kComposeProactiveNudge_CompactLongFocusDelay_10), nullptr},
    {"Compact UI - Long focus delay - Show (50 edits)",
     kComposeProactiveNudge_CompactLongFocusDelay_50,
     std::size(kComposeProactiveNudge_CompactLongFocusDelay_50), nullptr},

    {"Compact UI - No Focus delay - Show (10 edits) - long text delay",
     kComposeProactiveNudge_CompactLongTextDelay_10,
     std::size(kComposeProactiveNudge_CompactLongTextDelay_10), nullptr},
    {"Compact UI - No Focus delay - Show (50 edits) - long text delay",
     kComposeProactiveNudge_CompactLongTextDelay_50,
     std::size(kComposeProactiveNudge_CompactLongTextDelay_50), nullptr},
    {"Large UI - No focus delay - Show (10 edits)",
     kComposeProactiveNudge_LargeNoFocusDelay_10,
     std::size(kComposeProactiveNudge_LargeNoFocusDelay_10), nullptr},
    {"Large UI - Long focus delay - Show (50 edits)",
     kComposeProactiveNudge_LargeNoFocusDelay_50,
     std::size(kComposeProactiveNudge_LargeNoFocusDelay_50), nullptr}};

// Variations of the Compose selection nudge.
const FeatureEntry::FeatureParam kComposeSelectionNudge_1[] = {
    {"selection_nudge_length", "1"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_15[] = {
    {"selection_nudge_length", "15"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_30[] = {
    {"selection_nudge_length", "30"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_30_1s[] = {
    {"selection_nudge_length", "30"},
    {"selection_nudge_delay_milliseconds", "1000"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_30_2s[] = {
    {"selection_nudge_length", "30"},
    {"selection_nudge_delay_milliseconds", "2000"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_50[] = {
    {"selection_nudge_length", "50"}};

const FeatureEntry::FeatureParam kComposeSelectionNudge_100[] = {
    {"selection_nudge_length", "100"}};

const FeatureEntry::FeatureVariation kComposeSelectionNudgeVariations[] = {
    {"1 Char", kComposeSelectionNudge_1, std::size(kComposeSelectionNudge_1),
     nullptr},
    {"15 Char", kComposeSelectionNudge_15, std::size(kComposeSelectionNudge_15),
     nullptr},
    {"30 Char", kComposeSelectionNudge_30, std::size(kComposeSelectionNudge_30),
     nullptr},
    {"50 Char", kComposeSelectionNudge_50, std::size(kComposeSelectionNudge_50),
     nullptr},
    {"100 Char", kComposeSelectionNudge_100,
     std::size(kComposeSelectionNudge_100), nullptr},
    {"30 Char - 1sec", kComposeSelectionNudge_30_1s,
     std::size(kComposeSelectionNudge_30_1s), nullptr},
    {"30 char - 2sec", kComposeSelectionNudge_30_2s,
     std::size(kComposeSelectionNudge_30_2s), nullptr}};
#endif  // ENABLE_COMPOSE

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
const FeatureEntry::FeatureParam kLocationProviderManagerModeNetworkOnly[] = {
    {"LocationProviderManagerMode", "NetworkOnly"}};
const FeatureEntry::FeatureParam kLocationProviderManagerModePlatformOnly[] = {
    {"LocationProviderManagerMode", "PlatformOnly"}};
const FeatureEntry::FeatureParam kLocationProviderManagerModeHybridPlatform[] =
    {{"LocationProviderManagerMode", "HybridPlatform"}};
const FeatureEntry::FeatureParam kLocationProviderManagerModeHybridPlatform2[] =
    {{"LocationProviderManagerMode", "HybridPlatform2"}};

const FeatureEntry::FeatureVariation kLocationProviderManagerVariations[] = {
    {"Network only", kLocationProviderManagerModeNetworkOnly,
     std::size(kLocationProviderManagerModeNetworkOnly), nullptr},
    {"Platform only", kLocationProviderManagerModePlatformOnly,
     std::size(kLocationProviderManagerModePlatformOnly), nullptr},
    {"Wi-Fi fallback", kLocationProviderManagerModeHybridPlatform,
     std::size(kLocationProviderManagerModeHybridPlatform), nullptr},
    {"Fallback on error", kLocationProviderManagerModeHybridPlatform2,
     std::size(kLocationProviderManagerModeHybridPlatform2), nullptr}};
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

const FeatureEntry::FeatureParam kVisitedURLRankingDomainDeduplicationParam[] =
    {{"url_deduplication_include_title", "false"}};

const FeatureEntry::FeatureParam
    kVisitedURLRankingDomainDeduplicationIncludeQueryParam[] = {
        {"url_deduplication_include_title", "false"},
        {"url_deduplication_fallback", "false"}};

const FeatureEntry::FeatureParam
    kVisitedURLRankingDomainDeduplicationIncludePathQueryParam[] = {
        {"url_deduplication_include_title", "false"},
        {"url_deduplication_clear_path", "false"},
        {"url_deduplication_fallback", "false"}};

const FeatureEntry::FeatureVariation
    kVisitedURLRankingDomainDeduplicationVariations[] = {
        {"- Domain Deduplication", kVisitedURLRankingDomainDeduplicationParam,
         std::size(kVisitedURLRankingDomainDeduplicationParam), nullptr},
        {"- Domain Deduplication - Include Query",
         kVisitedURLRankingDomainDeduplicationIncludeQueryParam,
         std::size(kVisitedURLRankingDomainDeduplicationIncludeQueryParam),
         nullptr},
        {"- Domain Deduplication - Include Path and Query",
         kVisitedURLRankingDomainDeduplicationIncludePathQueryParam,
         std::size(kVisitedURLRankingDomainDeduplicationIncludePathQueryParam),
         nullptr}};

// LINT.IfChange(AutofillUploadCardRequestTimeouts)
const FeatureEntry::FeatureParam
    kAutofillUploadCardRequestTimeout_6Point5Seconds[] = {
        {"autofill_upload_card_request_timeout_milliseconds", "6500"}};
const FeatureEntry::FeatureParam kAutofillUploadCardRequestTimeout_7Seconds[] =
    {{"autofill_upload_card_request_timeout_milliseconds", "7000"}};
const FeatureEntry::FeatureParam kAutofillUploadCardRequestTimeout_9Seconds[] =
    {{"autofill_upload_card_request_timeout_milliseconds", "9000"}};
const FeatureEntry::FeatureVariation
    kAutofillUploadCardRequestTimeoutOptions[] = {
        {"6.5 seconds", kAutofillUploadCardRequestTimeout_6Point5Seconds,
         std::size(kAutofillUploadCardRequestTimeout_6Point5Seconds), nullptr},
        {"7 seconds", kAutofillUploadCardRequestTimeout_7Seconds,
         std::size(kAutofillUploadCardRequestTimeout_7Seconds), nullptr},
        {"9 seconds", kAutofillUploadCardRequestTimeout_9Seconds,
         std::size(kAutofillUploadCardRequestTimeout_9Seconds), nullptr}};
// LINT.ThenChange(//ios/chrome/browser/flags/about_flags.mm:AutofillUploadCardRequestTimeouts)

// LINT.IfChange(AutofillVcnEnrollRequestTimeouts)
const FeatureEntry::FeatureParam kAutofillVcnEnrollRequestTimeout_5Seconds[] = {
    {"autofill_vcn_enroll_request_timeout_milliseconds", "5000"}};
const FeatureEntry::FeatureParam
    kAutofillVcnEnrollRequestTimeout_7Point5Seconds[] = {
        {"autofill_vcn_enroll_request_timeout_milliseconds", "7500"}};
const FeatureEntry::FeatureParam kAutofillVcnEnrollRequestTimeout_10Seconds[] =
    {{"autofill_vcn_enroll_request_timeout_milliseconds", "10000"}};
const FeatureEntry::FeatureVariation kAutofillVcnEnrollRequestTimeoutOptions[] =
    {{"5 seconds", kAutofillVcnEnrollRequestTimeout_5Seconds,
      std::size(kAutofillVcnEnrollRequestTimeout_5Seconds), nullptr},
     {"7.5 seconds", kAutofillVcnEnrollRequestTimeout_7Point5Seconds,
      std::size(kAutofillVcnEnrollRequestTimeout_7Point5Seconds), nullptr},
     {"10 seconds", kAutofillVcnEnrollRequestTimeout_10Seconds,
      std::size(kAutofillVcnEnrollRequestTimeout_10Seconds), nullptr}};
// LINT.ThenChange(//ios/chrome/browser/flags/about_flags.mm:AutofillVcnEnrollRequestTimeouts)

const FeatureEntry::FeatureParam
    kAutofillImprovedLabelsWithoutMainTextChanges[] = {
        {"autofill_improved_labels_without_main_text_changes", "true"},
        {"autofill_improved_labels_with_differentiating_labels_in_front",
         "false"}};

const FeatureEntry::FeatureParam
    kAutofillImprovedLabelsWithDifferentiatingLabelsInFront[] = {
        {"autofill_improved_labels_without_main_text_changes", "false"},
        {"autofill_improved_labels_with_differentiating_labels_in_front",
         "true"}};

const FeatureEntry::FeatureVariation kAutofillImprovedLabelsVariations[] = {
    {"without main text changes", kAutofillImprovedLabelsWithoutMainTextChanges,
     std::size(kAutofillImprovedLabelsWithoutMainTextChanges), nullptr},
    {"with differentiating labels in front",
     kAutofillImprovedLabelsWithDifferentiatingLabelsInFront,
     std::size(kAutofillImprovedLabelsWithDifferentiatingLabelsInFront),
     nullptr},
};

#if BUILDFLAG(ENABLE_EXTENSIONS)
const FeatureEntry::FeatureParam
    kExtensionTelemetryEnterpriseReportingIntervalSeconds_20Seconds[] = {
        {"EnterpriseReportingIntervalSeconds", "20"}};
const FeatureEntry::FeatureParam
    kExtensionTelemetryEnterpriseReportingIntervalSeconds_60Seconds[] = {
        {"EnterpriseReportingIntervalSeconds", "60"}};
const FeatureEntry::FeatureParam
    kExtensionTelemetryEnterpriseReportingIntervalSeconds_300Seconds[] = {
        {"EnterpriseReportingIntervalSeconds", "300"}};
const FeatureEntry::FeatureVariation
    kExtensionTelemetryEnterpriseReportingIntervalSecondsVariations[] = {
        {"20 seconds",
         kExtensionTelemetryEnterpriseReportingIntervalSeconds_20Seconds,
         std::size(
             kExtensionTelemetryEnterpriseReportingIntervalSeconds_20Seconds),
         nullptr},
        {"60 seconds",
         kExtensionTelemetryEnterpriseReportingIntervalSeconds_60Seconds,
         std::size(
             kExtensionTelemetryEnterpriseReportingIntervalSeconds_60Seconds),
         nullptr},
        {"300 seconds",
         kExtensionTelemetryEnterpriseReportingIntervalSeconds_300Seconds,
         std::size(
             kExtensionTelemetryEnterpriseReportingIntervalSeconds_300Seconds),
         nullptr}};
constexpr char kExtensionAiDataInternalName[] =
    "enable-extension-ai-data-collection";
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

const FeatureEntry::FeatureParam kDiscountOnShoppyPage[] = {
    {commerce::kDiscountOnShoppyPageParam, "true"}};

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureVariation kDiscountsVariations[] = {
    {"Discount on Shoppy page", kDiscountOnShoppyPage,
     std::size(kDiscountOnShoppyPage), nullptr}};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kDiscountIconOnAndroidUseAlternateColor[] = {
    {commerce::kDiscountOnShoppyPageParam, "true"},
    {"action_chip_with_different_color", "true"}};
const FeatureEntry::FeatureVariation kDiscountsVariationsOnAndroid[] = {
    {"Discount on Shoppy page", kDiscountOnShoppyPage,
     std::size(kDiscountOnShoppyPage), nullptr},
    {"action chip different color", kDiscountIconOnAndroidUseAlternateColor,
     std::size(kDiscountIconOnAndroidUseAlternateColor), nullptr}};
#endif  // BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kSkiaGraphite_ValidationEnabled[] = {
    {"dawn_skip_validation", "false"}};
const FeatureEntry::FeatureParam kSkiaGraphite_ValidationDisabled[] = {
    {"dawn_skip_validation", "true"}};
const FeatureEntry::FeatureParam kSkiaGraphite_DebugLabelsEnabled[] = {
    {"dawn_backend_debug_labels", "true"}};

const FeatureEntry::FeatureVariation kSkiaGraphiteVariations[] = {
    {"dawn frontend validation enabled", kSkiaGraphite_ValidationEnabled,
     std::size(kSkiaGraphite_ValidationEnabled), nullptr},
    {"dawn frontend validation disabled", kSkiaGraphite_ValidationDisabled,
     std::size(kSkiaGraphite_ValidationDisabled), nullptr},
    {"dawn debug labels enabled", kSkiaGraphite_DebugLabelsEnabled,
     std::size(kSkiaGraphite_DebugLabelsEnabled), nullptr},
};

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
const FeatureEntry::FeatureParam kTranslationAPI_SkipLanguagePackLimit[] = {
    {"TranslationAPIAcceptLanguagesCheck", "false"},
    {"TranslationAPILimitLanguagePackCount", "false"}};

const FeatureEntry::FeatureVariation kTranslationAPIVariations[] = {
    {"without language pack limit", kTranslationAPI_SkipLanguagePackLimit,
     std::size(kTranslationAPI_SkipLanguagePackLimit), nullptr}};
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kSensitiveContentUsePwmHeuristics[] = {
    {"sensitive_content_use_pwm_heuristics", "true"}};

const FeatureEntry::FeatureVariation kSensitiveContentVariations[] = {
    {"with password manager heuristics", kSensitiveContentUsePwmHeuristics,
     std::size(kSensitiveContentUsePwmHeuristics), nullptr},
};
#endif  // BUILDFLAG(IS_ANDROID)

// Feature variations for kSubframeProcessReuseThresholds.
const FeatureEntry::FeatureParam kSubframeProcessReuseMemoryThreshold512MB{
    "SubframeProcessReuseMemoryThreshold", "536870912"};
const FeatureEntry::FeatureParam kSubframeProcessReuseMemoryThreshold1GB{
    "SubframeProcessReuseMemoryThreshold", "1073741824"};
const FeatureEntry::FeatureParam kSubframeProcessReuseMemoryThreshold2GB{
    "SubframeProcessReuseMemoryThreshold", "2147483648"};
const FeatureEntry::FeatureParam kSubframeProcessReuseMemoryThreshold4GB{
    "SubframeProcessReuseMemoryThreshold", "4294967296"};
const FeatureEntry::FeatureVariation
    kSubframeProcessReuseThresholdsVariations[] = {
        {"with 512MB memory threshold",
         &kSubframeProcessReuseMemoryThreshold512MB, 1, nullptr},
        {"with 1GB memory threshold", &kSubframeProcessReuseMemoryThreshold1GB,
         1, nullptr},
        {"with 2GB memory threshold", &kSubframeProcessReuseMemoryThreshold2GB,
         1, nullptr},
        {"with 4GB memory threshold", &kSubframeProcessReuseMemoryThreshold4GB,
         1, nullptr},
};

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
const FeatureEntry::FeatureParam kContextualCueingEnabledNoEngagementCap[] = {
    {"BackoffTime", "0h"},
    {"BackoffMultiplierBase", "0.0"},
    {"NudgeCapTime", "0h"},
    {"NudgeCapTimePerDomain", "0h"},
    {"MinPageCountBetweenNudges", "0"},
    {"MinTimeBetweenNudges", "0h"}};
const FeatureEntry::FeatureVariation kContextualCueingEnabledOptions[] = {
    {"no engagement caps", kContextualCueingEnabledNoEngagementCap,
     std::size(kContextualCueingEnabledNoEngagementCap), nullptr},
};
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserOnly[] = {
        {"enabled-processes", "browser-only"}};
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserAndRenderer[] = {
        {"enabled-processes", "browser-and-renderer"}};
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_NonRenderer[] = {
        {"enabled-processes", "non-renderer"}};
const FeatureEntry::FeatureParam
    kPartitionAllocWithAdvancedChecksEnabledProcesses_AllProcesses[] = {
        {"enabled-processes", "all-processes"}};
const FeatureEntry::FeatureVariation
    kPartitionAllocWithAdvancedChecksEnabledProcessesOptions[] = {
        {"on browser process only",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserOnly,
         std::size(
             kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserOnly),
         nullptr},
        {"on browser and renderer processes",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserAndRenderer,
         std::size(
             kPartitionAllocWithAdvancedChecksEnabledProcesses_BrowserAndRenderer),
         nullptr},
        {"on non renderer processes",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_NonRenderer,
         std::size(
             kPartitionAllocWithAdvancedChecksEnabledProcesses_NonRenderer),
         nullptr},
        {"on all processes",
         kPartitionAllocWithAdvancedChecksEnabledProcesses_AllProcesses,
         std::size(
             kPartitionAllocWithAdvancedChecksEnabledProcesses_AllProcesses),
         nullptr}};
#endif  // PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

const FeatureEntry::FeatureParam kSendTabIOSPushNotificationsWithURLImage[] = {
    {send_tab_to_self::kSendTabIOSPushNotificationsURLImageParam, "true"}};
const FeatureEntry::FeatureVariation kSendTabIOSPushNotificationsVariations[] =
    {
        {"With URL Image", kSendTabIOSPushNotificationsWithURLImage,
         std::size(kSendTabIOSPushNotificationsWithURLImage), nullptr},
};

#if BUILDFLAG(IS_ANDROID) && PA_BUILDFLAG(HAS_MEMORY_TAGGING) && \
    PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
// Feature variations for kPartitionAllocMemoryTagging.
const FeatureEntry::FeatureParam
    kPartitionAllocMemoryTaggingParams_AsyncBrowserOnly[] = {
        {"enabled-processes", "browser-only"},
        {"memtag-mode", "async"}};
const FeatureEntry::FeatureParam
    kPartitionAllocMemoryTaggingParams_AsyncNonRenderer[] = {
        {"enabled-processes", "non-renderer"},
        {"memtag-mode", "async"}};
const FeatureEntry::FeatureParam
    kPartitionAllocMemoryTaggingParams_AsyncAllProcesses[] = {
        {"enabled-processes", "all-processes"},
        {"memtag-mode", "async"}};
const FeatureEntry::FeatureParam
    kPartitionAllocMemoryTaggingParams_SyncBrowserOnly[] = {
        {"enabled-processes", "browser-only"},
        {"memtag-mode", "sync"}};
const FeatureEntry::FeatureParam
    kPartitionAllocMemoryTaggingParams_SyncNonRenderer[] = {
        {"enabled-processes", "non-renderer"},
        {"memtag-mode", "sync"}};
const FeatureEntry::FeatureParam
    kPartitionAllocMemoryTaggingParams_SyncAllProcesses[] = {
        {"enabled-processes", "all-processes"},
        {"memtag-mode", "sync"}};
const FeatureEntry::FeatureVariation
    kPartitionAllocMemoryTaggingEnabledProcessesOptions[] = {
        {"ASYNC mode on browser process only",
         kPartitionAllocMemoryTaggingParams_AsyncBrowserOnly,
         std::size(kPartitionAllocMemoryTaggingParams_AsyncBrowserOnly),
         nullptr},
        {"ASYNC mode on non renderer processes",
         kPartitionAllocMemoryTaggingParams_AsyncNonRenderer,
         std::size(kPartitionAllocMemoryTaggingParams_AsyncNonRenderer),
         nullptr},
        {"ASYNC mode on all processes",
         kPartitionAllocMemoryTaggingParams_AsyncAllProcesses,
         std::size(kPartitionAllocMemoryTaggingParams_AsyncAllProcesses),
         nullptr},
        {"SYNC mode on browser process only",
         kPartitionAllocMemoryTaggingParams_SyncBrowserOnly,
         std::size(kPartitionAllocMemoryTaggingParams_SyncBrowserOnly),
         nullptr},
        {"SYNC mode on non renderer processes",
         kPartitionAllocMemoryTaggingParams_SyncNonRenderer,
         std::size(kPartitionAllocMemoryTaggingParams_SyncNonRenderer),
         nullptr},
        {"SYNC mode on all processes",
         kPartitionAllocMemoryTaggingParams_SyncAllProcesses,
         std::size(kPartitionAllocMemoryTaggingParams_SyncAllProcesses),
         nullptr}};
#endif  // BUILDFLAG(IS_ANDROID) && PA_BUILDFLAG(HAS_MEMORY_TAGGING) &&
        // PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

const FeatureEntry::FeatureParam kServiceWorkerAutoPreload_SWNotRunningOnly[] =
    {
        {"enable_only_when_service_worker_not_running", "true"},
};
const FeatureEntry::FeatureVariation kServiceWorkerAutoPreloadVariations[] = {
    {"only when SW is not running", kServiceWorkerAutoPreload_SWNotRunningOnly,
     std::size(kServiceWorkerAutoPreload_SWNotRunningOnly), nullptr},
};

const FeatureEntry::FeatureParam
    kEnableFingerprintingProtectionFilter_WithLogging[] = {
        {"activation_level", "enabled"},
        {"enable_console_logging", "true"}};
const FeatureEntry::FeatureParam
    kEnableFingerprintingProtectionFilter_DryRunWithLogging[] = {
        {"activation_level", "dry_run"},
        {"enable_console_logging", "true"}};
const FeatureEntry::FeatureVariation
    kEnableFingerprintingProtectionFilterVariations[] = {
        {" - with Console Logs",
         kEnableFingerprintingProtectionFilter_WithLogging,
         std::size(kEnableFingerprintingProtectionFilter_WithLogging), nullptr},
        {" - Dry Run with Console Logs",
         kEnableFingerprintingProtectionFilter_DryRunWithLogging,
         std::size(kEnableFingerprintingProtectionFilter_DryRunWithLogging),
         nullptr}};

const FeatureEntry::FeatureParam
    kEnableFingerprintingProtectionFilterInIncognito_WithLogging[] = {
        {"activation_level", "enabled"},
        {"enable_console_logging", "true"}};
const FeatureEntry::FeatureVariation
    kEnableFingerprintingProtectionFilterInIncognitoVariations[] = {
        {" - with Console Logs",
         kEnableFingerprintingProtectionFilterInIncognito_WithLogging,
         std::size(
             kEnableFingerprintingProtectionFilterInIncognito_WithLogging),
         nullptr}};

#if !BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kMerchantTrustEnabledWithSampleData[] = {
    {page_info::kMerchantTrustEnabledWithSampleDataName, "true"}};
const FeatureEntry::FeatureVariation kMerchantTrustVariations[] = {
    {"Enabled with sample data", kMerchantTrustEnabledWithSampleData,
     std::size(kMerchantTrustEnabledWithSampleData), nullptr}};

const FeatureEntry::FeatureParam kAudioDuckingAttenuation_60[] = {
    {"attenuation", "60"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_70[] = {
    {"attenuation", "70"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_80[] = {
    {"attenuation", "80"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_90[] = {
    {"attenuation", "90"}};
const FeatureEntry::FeatureParam kAudioDuckingAttenuation_100[] = {
    {"attenuation", "100"}};

const FeatureEntry::FeatureVariation kAudioDuckingAttenuationVariations[] = {
    {"attenuation 60", kAudioDuckingAttenuation_60,
     std::size(kAudioDuckingAttenuation_60), nullptr},
    {"attenuation 70", kAudioDuckingAttenuation_70,
     std::size(kAudioDuckingAttenuation_70), nullptr},
    {"attenuation 80", kAudioDuckingAttenuation_80,
     std::size(kAudioDuckingAttenuation_80), nullptr},
    {"attenuation 90", kAudioDuckingAttenuation_90,
     std::size(kAudioDuckingAttenuation_90), nullptr},
    {"attenuation 100", kAudioDuckingAttenuation_100,
     std::size(kAudioDuckingAttenuation_100), nullptr}};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam kTabSwitcherColorBlendAnimateVariation1[] = {
    {"animation_duration_ms", "240"},
    {"animation_interpolator", "1"}};
const FeatureEntry::FeatureParam kTabSwitcherColorBlendAnimateVariation2[] = {
    {"animation_duration_ms", "400"},
    {"animation_interpolator", "2"}};
const FeatureEntry::FeatureParam kTabSwitcherColorBlendAnimateVariation3[] = {
    {"animation_duration_ms", "200"},
    {"animation_interpolator", "3"}};
const FeatureEntry::FeatureVariation kTabSwitcherColorBlendAnimateVariations[] =
    {{"Color Blend Animation Variation 1",
      kTabSwitcherColorBlendAnimateVariation1,
      std::size(kTabSwitcherColorBlendAnimateVariation1), nullptr},
     {"Color Blend Animation Variation 2",
      kTabSwitcherColorBlendAnimateVariation2,
      std::size(kTabSwitcherColorBlendAnimateVariation2), nullptr},
     {"Color Blend Animation Variation 3",
      kTabSwitcherColorBlendAnimateVariation3,
      std::size(kTabSwitcherColorBlendAnimateVariation3), nullptr}};
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
const char kAccountStoragePrefsThemesAndSearchEnginesFeatures[] =
    // Flags for account storage of prefs.
    "EnablePreferencesAccountStorage,"
    // Flags for account storage of search engines.
    "DisableSyncAutogeneratedSearchEngines,"
    "SeparateLocalAndAccountSearchEngines,"
    // Flags for account storage of themes.
    "MoveThemePrefsToSpecifics,"
    "SeparateLocalAndAccountThemes,"
    "ThemesBatchUpload";

const FeatureEntry::Choice kAccountStoragePrefsThemesAndSearchEnginesChoices[] =
    {{"Default", "", ""},
     {"Disabled", "disable-features",
      kAccountStoragePrefsThemesAndSearchEnginesFeatures},
     {"Enabled", "enable-features",
      kAccountStoragePrefsThemesAndSearchEnginesFeatures}};
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
const FeatureEntry::FeatureParam
    kMaliciousApkDownloadCheckTelemetryOnlyParams[] = {
        {"telemetry_only", "true"}};
const FeatureEntry::FeatureVariation kMaliciousApkDownloadCheckChoices[] = {
    {"Telemetry only", kMaliciousApkDownloadCheckTelemetryOnlyParams,
     std::size(kMaliciousApkDownloadCheckTelemetryOnlyParams), nullptr}};
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
constexpr char kDisableFacilitatedPaymentsMerchantAllowlistInternalName[] =
    "disable-facilitated-payments-merchant-allowlist";
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)

const char kHistoryPagePromoVariationFeatures[] =
    "HistoryPageHistorySyncPromo,"
    "HistoryPagePromoCtaStringVariation";

const FeatureEntry::Choice kHistoryOptInEntryPointChoices[] = {
    {"Default", "", ""},
    {"Enabled with Turn On CTA", "enable-features",
     "HistoryPageHistorySyncPromo"},
    {"Enabled with Continue CTA", "enable-features",
     kHistoryPagePromoVariationFeatures},
    {"Disabled", "disable-features", kHistoryPagePromoVariationFeatures},
};

const FeatureEntry::FeatureParam kHistoryOptInEducationalTipTurnOn[] = {
    {"history_opt_in_educational_tip_param", "0"}};
const FeatureEntry::FeatureParam kHistoryOptInEducationalTipLetsGo[] = {
    {"history_opt_in_educational_tip_param", "1"}};
const FeatureEntry::FeatureParam kHistoryOptInEducationalTipContinue[] = {
    {"history_opt_in_educational_tip_param", "2"}};

const FeatureEntry::FeatureVariation kHistoryOptInEducationalTipVariations[] = {
    {"Enable with \"Turn on\" string variant",
     kHistoryOptInEducationalTipTurnOn,
     std::size(kHistoryOptInEducationalTipTurnOn), nullptr},
    {"Enable with \"Let's go\" string variant",
     kHistoryOptInEducationalTipLetsGo,
     std::size(kHistoryOptInEducationalTipLetsGo), nullptr},
    {"Enable with \"Continue\" string variant",
     kHistoryOptInEducationalTipContinue,
     std::size(kHistoryOptInEducationalTipContinue), nullptr}};
#endif  // BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam
    kStandardBoundSessionCredentialsEnabledNoOriginTrialToken[] = {
        {"ForceEnableForTesting", "true"}};
const FeatureEntry::FeatureParam
    kStandardBoundSessionCredentialsEnabledOriginTrialToken[] = {
        {"ForceEnableForTesting", "false"}};

const FeatureEntry::FeatureVariation
    kStandardBoundSessionCredentialsVariations[] = {
        {"- Without Origin Trial tokens",
         kStandardBoundSessionCredentialsEnabledNoOriginTrialToken,
         std::size(kStandardBoundSessionCredentialsEnabledNoOriginTrialToken),
         nullptr},
        {"- With Origin Trial tokens",
         kStandardBoundSessionCredentialsEnabledOriginTrialToken,
         std::size(kStandardBoundSessionCredentialsEnabledOriginTrialToken),
         nullptr}};

#if BUILDFLAG(IS_CHROMEOS)
constexpr auto kScannerDisclaimerDebugOverrideChoices =
    std::to_array<FeatureEntry::Choice>({
        {flag_descriptions::kScannerDisclaimerDebugOverrideChoiceDefault, "",
         ""},
        {flag_descriptions::kScannerDisclaimerDebugOverrideChoiceAlwaysReminder,
         ash::switches::kScannerDisclaimerDebugOverride,
         ash::switches::kScannerDisclaimerDebugOverrideReminder},
        {flag_descriptions::kScannerDisclaimerDebugOverrideChoiceAlwaysFull,
         ash::switches::kScannerDisclaimerDebugOverride,
         ash::switches::kScannerDisclaimerDebugOverrideFull},
    });
#endif

const FeatureEntry::FeatureParam kEnableCanvasNoiseInAllModes[] = {
    {"enable_in_regular_mode", "true"}};
const FeatureEntry::FeatureVariation kEnableCanvasNoiseVariations[] = {
    {" - In all browsing modes", kEnableCanvasNoiseInAllModes,
     std::size(kEnableCanvasNoiseInAllModes), nullptr}};

// LINT.IfChange(AutofillVcnEnrollStrikeExpiryTime)
const FeatureEntry::FeatureParam kAutofillVcnEnrollStrikeExpiryTime_120Days[] =
    {{"autofill_vcn_strike_expiry_time_days", "120"}};

const FeatureEntry::FeatureParam kAutofillVcnEnrollStrikeExpiryTime_60Days[] = {
    {"autofill_vcn_strike_expiry_time_days", "60"}};

const FeatureEntry::FeatureParam kAutofillVcnEnrollStrikeExpiryTime_30Days[] = {
    {"autofill_vcn_strike_expiry_time_days", "30"}};

const FeatureEntry::FeatureVariation
    kAutofillVcnEnrollStrikeExpiryTimeOptions[] = {
        {"120 days", kAutofillVcnEnrollStrikeExpiryTime_120Days,
         std::size(kAutofillVcnEnrollStrikeExpiryTime_120Days), nullptr},
        {"60 days", kAutofillVcnEnrollStrikeExpiryTime_60Days,
         std::size(kAutofillVcnEnrollStrikeExpiryTime_60Days), nullptr},
        {"30 days", kAutofillVcnEnrollStrikeExpiryTime_30Days,
         std::size(kAutofillVcnEnrollStrikeExpiryTime_30Days), nullptr}};
// LINT.ThenChange(//ios/chrome/browser/flags/about_flags.mm:AutofillVcnEnrollStrikeExpiryTime)

#if BUILDFLAG(ENABLE_GLIC)
// Variations of the glic panel reset for the top Chrome button.
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_1s[] = {
    {"glic-panel-reset-delay-ms", "1000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_2s[] = {
    {"glic-panel-reset-delay-ms", "2000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_3s[] = {
    {"glic-panel-reset-delay-ms", "3000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_5s[] = {
    {"glic-panel-reset-delay-ms", "3000"}};
const FeatureEntry::FeatureParam kGlicPanelResetTopChromeButtonOnOpen_10s[] = {
    {"glic-panel-reset-delay-ms", "10000"}};

const FeatureEntry::FeatureVariation
    kGlicPanelResetTopChromeButtonVariations[] = {
        {"Reset on open - 1s", kGlicPanelResetTopChromeButtonOnOpen_1s,
         std::size(kGlicPanelResetTopChromeButtonOnOpen_1s), nullptr},
        {"Reset on open - 2s", kGlicPanelResetTopChromeButtonOnOpen_2s,
         std::size(kGlicPanelResetTopChromeButtonOnOpen_2s), nullptr},
        {"Reset on open - 3s", kGlicPanelResetTopChromeButtonOnOpen_3s,
         std::size(kGlicPanelResetTopChromeButtonOnOpen_3s), nullptr},
        {"Reset on open - 5s", kGlicPanelResetTopChromeButtonOnOpen_5s,
         std::size(kGlicPanelResetTopChromeButtonOnOpen_5s), nullptr},
        {"Reset on open - 10s", kGlicPanelResetTopChromeButtonOnOpen_10s,
         std::size(kGlicPanelResetTopChromeButtonOnOpen_10s), nullptr}};

const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_1h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "1"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_2h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "2"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_4h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "4"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_12h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "12"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_24h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "24"},
};
const FeatureEntry::FeatureParam kGlicPanelResetOnSessionTimeout_48h[] = {
    {"glic-panel-reset-session-timeout-delay-h", "48"},
};

const FeatureEntry::FeatureVariation
    kGlicPanelResetOnSessionTimeoutVariations[] = {
        {"Reset after 1h", kGlicPanelResetOnSessionTimeout_1h,
         std::size(kGlicPanelResetOnSessionTimeout_1h), nullptr},
        {"Reset after 2h", kGlicPanelResetOnSessionTimeout_2h,
         std::size(kGlicPanelResetOnSessionTimeout_2h), nullptr},
        {"Reset after 4h", kGlicPanelResetOnSessionTimeout_4h,
         std::size(kGlicPanelResetOnSessionTimeout_4h), nullptr},
        {"Reset after 12h", kGlicPanelResetOnSessionTimeout_12h,
         std::size(kGlicPanelResetOnSessionTimeout_12h), nullptr},
        {"Reset after 24h", kGlicPanelResetOnSessionTimeout_24h,
         std::size(kGlicPanelResetOnSessionTimeout_24h), nullptr},
        {"Reset after 48h", kGlicPanelResetOnSessionTimeout_48h,
         std::size(kGlicPanelResetOnSessionTimeout_48h), nullptr}};
#endif  // BUILDFLAG(ENABLE_GLIC)

const FeatureEntry::FeatureParam kAutofillShowTypePredictionsAsTitle[] = {
    {"as-title", "true"}};
const FeatureEntry::FeatureVariation kAutofillShowTypePredictionsVariations[] =
    {{"- show predictions as title", kAutofillShowTypePredictionsAsTitle,
      std::size(kAutofillShowTypePredictionsAsTitle), nullptr}};

const FeatureEntry::FeatureParam
    kInvalidateChoiceOnRestoreIsRetroactiveOption[] = {
        {"is_retroactive", "true"}};
const FeatureEntry::FeatureVariation
    kInvalidateSearchEngineChoiceOnRestoreVariations[] = {
        {"(retroactive)", kInvalidateChoiceOnRestoreIsRetroactiveOption,
         std::size(kInvalidateChoiceOnRestoreIsRetroactiveOption), nullptr}};

const FeatureEntry::FeatureVariation
    kAISummarizationAPIWithAdaptationVaration[] = {
        {"With Adaptation", nullptr, 0, "3389300"},
        {"With EE Adaptation", nullptr, 0, "3389532"},
};

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
const FeatureEntry::FeatureParam
    kHistorySyncOptinExpansionPillBrowseAcrossDevices[] = {
        {"history-sync-optin-expansion-pill-option", "browse-across-devices"}};
const FeatureEntry::FeatureParam kHistorySyncOptinExpansionPillSyncHistory[] = {
    {"history-sync-optin-expansion-pill-option", "sync-history"}};
const FeatureEntry::FeatureParam
    kHistorySyncOptinExpansionPillSeeTabsFromOtherDevices[] = {
        {"history-sync-optin-expansion-pill-option",
         "see-tabs-from-other-devices"}};
const FeatureEntry::FeatureParam
    kHistorySyncOptinExpansionPillBrowseAcrossDevicesNewProfileMenuPromoVariant
        [] = {{"history-sync-optin-expansion-pill-option",
               "browse-across-devices-new-profile-menu-promo-variant"}};

const FeatureEntry::FeatureVariation kHistorySyncOptinExpansionPillVariations[] = {
    {"- Browse across devices",
     kHistorySyncOptinExpansionPillBrowseAcrossDevices,
     std::size(kHistorySyncOptinExpansionPillBrowseAcrossDevices), nullptr},
    {"- Sync history", kHistorySyncOptinExpansionPillSyncHistory,
     std::size(kHistorySyncOptinExpansionPillSyncHistory), nullptr},
    {"- See tabs from other devices",
     kHistorySyncOptinExpansionPillSeeTabsFromOtherDevices,
     std::size(kHistorySyncOptinExpansionPillSeeTabsFromOtherDevices), nullptr},
    {"- Browse across devices (Profile Menu Variant)",
     kHistorySyncOptinExpansionPillBrowseAcrossDevicesNewProfileMenuPromoVariant,
     std::size(
         kHistorySyncOptinExpansionPillBrowseAcrossDevicesNewProfileMenuPromoVariant),
     nullptr}};
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

#if BUILDFLAG(IS_ANDROID)

const FeatureEntry::FeatureParam kTouchToSearchCalloutTextVariantParams[] = {
    {"text_variant", "true"},
};
const FeatureEntry::FeatureVariation kTouchToSearchCalloutVariations[] = {
    {"Default", nullptr, 0, nullptr},
    {"Text Variant", kTouchToSearchCalloutTextVariantParams,
     std::size(kTouchToSearchCalloutTextVariantParams), nullptr},
};

#endif  // BUILDFLAG(IS_ANDROID)

// RECORDING USER METRICS FOR FLAGS:
// -----------------------------------------------------------------------------
// The first line of the entry is the internal name.
//
// To add a new entry, add to the end of kFeatureEntries. There are two
// distinct types of entries:
// . SINGLE_VALUE: entry is either on or off. Use the SINGLE_VALUE_TYPE
//   macro for this type supplying the command line to the macro.
// . MULTI_VALUE: a list of choices, the first of which should correspond to a
//   deactivated state for this lab (i.e. no command line option). To specify
//   this type of entry use the macro MULTI_VALUE_TYPE supplying it the
//   array of choices.
// See the documentation of FeatureEntry for details on the fields.
//
// Usage of about:flags is logged on startup via the "Launch.FlagsAtStartup"
// UMA histogram. This histogram shows the number of startups with a given flag
// enabled. If you'd like to see user counts instead, make sure to switch to
// "count users" view on the dashboard. When adding new entries, the enum
// "LoginCustomFlags" must be updated in histograms/enums.xml. See note in
// enums.xml and don't forget to run AboutFlagsHistogramTest unit test to
// calculate and verify checksum.
//
// When adding a new choice, add it to the end of the list.
const FeatureEntry kFeatureEntries[] = {
// Include generated flags for flag unexpiry; see //docs/flag_expiry.md and
// //tools/flags/generate_unexpire_flags.py.
#include "build/chromeos_buildflags.h"
#include "chrome/browser/unexpire_flags_gen.inc"
    {variations::switches::kEnableBenchmarking,
     flag_descriptions::kEnableBenchmarkingName,
     flag_descriptions::kEnableBenchmarkingDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableBenchmarkingChoices)},
    {"ignore-gpu-blocklist", flag_descriptions::kIgnoreGpuBlocklistName,
     flag_descriptions::kIgnoreGpuBlocklistDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kIgnoreGpuBlocklist)},
    {"enable-accessibility-on-screen-mode",
     flag_descriptions::kAccessibilityOnScreenModeName,
     flag_descriptions::kAccessibilityOnScreenModeDescription, kOsAll,
     FEATURE_VALUE_TYPE(::features::kAccessibilityOnScreenMode)},
    {"disable-accelerated-2d-canvas",
     flag_descriptions::kAccelerated2dCanvasName,
     flag_descriptions::kAccelerated2dCanvasDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAccelerated2dCanvas)},
    {"overlay-strategies", flag_descriptions::kOverlayStrategiesName,
     flag_descriptions::kOverlayStrategiesDescription, kOsAll,
     MULTI_VALUE_TYPE(kOverlayStrategiesChoices)},
    {"tint-composited-content", flag_descriptions::kTintCompositedContentName,
     flag_descriptions::kTintCompositedContentDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kTintCompositedContent)},
    {"show-overdraw-feedback", flag_descriptions::kShowOverdrawFeedbackName,
     flag_descriptions::kShowOverdrawFeedbackDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kShowOverdrawFeedback)},
#if !BUILDFLAG(IS_CHROMEOS)
    {"feedback-include-variations",
     flag_descriptions::kFeedbackIncludeVariationsName,
     flag_descriptions::kFeedbackIncludeVariationsDescription,
     kOsWin | kOsLinux | kOsMac | kOsAndroid,
     FEATURE_VALUE_TYPE(variations::kFeedbackIncludeVariations)},
#endif
    {"ui-disable-partial-swap", flag_descriptions::kUiPartialSwapName,
     flag_descriptions::kUiPartialSwapDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kUIDisablePartialSwap)},
    {"webrtc-hw-decoding", flag_descriptions::kWebrtcHwDecodingName,
     flag_descriptions::kWebrtcHwDecodingDescription, kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kWebRtcHWDecoding)},
    {"webrtc-hw-encoding", flag_descriptions::kWebrtcHwEncodingName,
     flag_descriptions::kWebrtcHwEncodingDescription, kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kWebRtcHWEncoding)},
    {"webrtc-pqc-for-dtls", flag_descriptions::kWebRtcPqcForDtlsName,
     flag_descriptions::kWebRtcPqcForDtlsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kWebRtcPqcForDtls)},
    {"enable-webrtc-allow-input-volume-adjustment",
     flag_descriptions::kWebRtcAllowInputVolumeAdjustmentName,
     flag_descriptions::kWebRtcAllowInputVolumeAdjustmentDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(features::kWebRtcAllowInputVolumeAdjustment)},
    {"enable-webrtc-apm-downmix-capture-audio-method",
     flag_descriptions::kWebRtcApmDownmixCaptureAudioMethodName,
     flag_descriptions::kWebRtcApmDownmixCaptureAudioMethodDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kWebRtcApmDownmixCaptureAudioMethod,
         kWebRtcApmDownmixMethodVariations,
         "WebRtcApmDownmixCaptureAudioMethod")},
    {"enable-webrtc-hide-local-ips-with-mdns",
     flag_descriptions::kWebrtcHideLocalIpsWithMdnsName,
     flag_descriptions::kWebrtcHideLocalIpsWithMdnsDecription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebRtcHideLocalIpsWithMdns)},
    {"enable-webrtc-use-min-max-vea-dimensions",
     flag_descriptions::kWebrtcUseMinMaxVEADimensionsName,
     flag_descriptions::kWebrtcUseMinMaxVEADimensionsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kWebRtcUseMinMaxVEADimensions)},
#if defined(WEBRTC_USE_PIPEWIRE)
    {"enable-webrtc-pipewire-camera",
     flag_descriptions::kWebrtcPipeWireCameraName,
     flag_descriptions::kWebrtcPipeWireCameraDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWebRtcPipeWireCamera)},
#endif  // defined(WEBRTC_USE_PIPEWIRE)
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"web-hid-in-web-view", flag_descriptions::kEnableWebHidInWebViewName,
     flag_descriptions::kEnableWebHidInWebViewDescription, kOsAll,
     FEATURE_VALUE_TYPE(extensions_features::kEnableWebHidInWebView)},
    {"extensions-on-chrome-urls",
     flag_descriptions::kExtensionsOnChromeUrlsName,
     flag_descriptions::kExtensionsOnChromeUrlsDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kExtensionsOnChromeURLs)},
    {"extensions-on-extension-urls",
     flag_descriptions::kExtensionsOnExtensionUrlsName,
     flag_descriptions::kExtensionsOnExtensionUrlsDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kExtensionsOnExtensionURLs)},
#endif  // ENABLE_EXTENSIONS
#if BUILDFLAG(IS_ANDROID)
    {"contextual-search-suppress-short-view",
     flag_descriptions::kContextualSearchSuppressShortViewName,
     flag_descriptions::kContextualSearchSuppressShortViewDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kContextualSearchSuppressShortView,
         kContextualSearchSuppressShortViewVariations,
         "ContextualSearchSuppressShortView")},
    {"contextual-search-with-credentials-for-debug",
     flag_descriptions::kContextualSearchWithCredentialsForDebugName,
     flag_descriptions::kContextualSearchWithCredentialsForDebugDescription,
     kOsAndroid, FEATURE_VALUE_TYPE(kContextualSearchWithCredentialsForDebug)},
    {"related-searches-all-language",
     flag_descriptions::kRelatedSearchesAllLanguageName,
     flag_descriptions::kRelatedSearchesAllLanguageDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kRelatedSearchesAllLanguage)},
    {"related-searches-switch", flag_descriptions::kRelatedSearchesSwitchName,
     flag_descriptions::kRelatedSearchesSwitchDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kRelatedSearchesSwitch)},
    {"omnibox-shortcuts-android",
     flag_descriptions::kOmniboxShortcutsAndroidName,
     flag_descriptions::kOmniboxShortcutsAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kOmniboxShortcutsAndroid)},
    {"safe-browsing-sync-checker-check-allowlist",
     flag_descriptions::kSafeBrowsingSyncCheckerCheckAllowlistName,
     flag_descriptions::kSafeBrowsingSyncCheckerCheckAllowlistDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(safe_browsing::kSafeBrowsingSyncCheckerCheckAllowlist)},
#endif  // BUILDFLAG(IS_ANDROID)
    {"show-autofill-type-predictions",
     flag_descriptions::kShowAutofillTypePredictionsName,
     flag_descriptions::kShowAutofillTypePredictionsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::test::kAutofillShowTypePredictions,
         kAutofillShowTypePredictionsVariations,
         "AutofillShowTypePredictions")},
    {"autofill-more-prominent-popup",
     flag_descriptions::kAutofillMoreProminentPopupName,
     flag_descriptions::kAutofillMoreProminentPopupDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillMoreProminentPopup)},
    {"autofill-payments-field-swapping",
     flag_descriptions::kAutofillPaymentsFieldSwappingName,
     flag_descriptions::kAutofillPaymentsFieldSwappingDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillPaymentsFieldSwapping)},
    {"backdrop-filter-mirror-edge",
     flag_descriptions::kBackdropFilterMirrorEdgeName,
     flag_descriptions::kBackdropFilterMirrorEdgeDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kBackdropFilterMirrorEdgeMode)},
    {"smooth-scrolling", flag_descriptions::kSmoothScrollingName,
     flag_descriptions::kSmoothScrollingDescription,
     // Mac has a separate implementation with its own setting to disable.
     kOsLinux | kOsCrOS | kOsWin | kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableSmoothScrolling,
                               switches::kDisableSmoothScrolling)},
    {"fractional-scroll-offsets",
     flag_descriptions::kFractionalScrollOffsetsName,
     flag_descriptions::kFractionalScrollOffsetsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFractionalScrollOffsets)},
#if defined(USE_AURA)
    {"overlay-scrollbars", flag_descriptions::kOverlayScrollbarsName,
     flag_descriptions::kOverlayScrollbarsDescription,
     // Uses the system preference on Mac (a different implementation).
     // On Android, this is always enabled.
     kOsAura, FEATURE_VALUE_TYPE(features::kOverlayScrollbar)},
#endif  // USE_AURA
    {"enable-lazy-load-image-for-invisible-pages",
     flag_descriptions::kEnableLazyLoadImageForInvisiblePageName,
     flag_descriptions::kEnableLazyLoadImageForInvisiblePageDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         blink::features::kEnableLazyLoadImageForInvisiblePage,
         kSearchSuggsetionPrerenderTypeVariations,
         "EnableLazyLoadImageForInvisiblePage")},
    {"soft-navigation-heuristics",
     flag_descriptions::kSoftNavigationHeuristicsName,
     flag_descriptions::kSoftNavigationHeuristicsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kSoftNavigationHeuristics,
                                    kSoftNavigationHeuristicsVariations,
                                    "SoftNavigationHeuristics")},
    {"enable-quic", flag_descriptions::kQuicName,
     flag_descriptions::kQuicDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableQuic, switches::kDisableQuic)},
    {"webtransport-developer-mode",
     flag_descriptions::kWebTransportDeveloperModeName,
     flag_descriptions::kWebTransportDeveloperModeDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kWebTransportDeveloperMode)},
    {"disable-javascript-harmony-shipping",
     flag_descriptions::kJavascriptHarmonyShippingName,
     flag_descriptions::kJavascriptHarmonyShippingDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableJavaScriptHarmonyShipping)},
    {"enable-javascript-harmony", flag_descriptions::kJavascriptHarmonyName,
     flag_descriptions::kJavascriptHarmonyDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kJavaScriptHarmony)},
    {"enable-enterprise-profile-badging-for-avatar",
     flag_descriptions::kEnterpriseProfileBadgingForAvatarName,
     flag_descriptions::kEnterpriseProfileBadgingForAvatarDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kEnterpriseProfileBadgingForAvatar)},
    {"enable-enterprise-badging-for-ntp-footer",
     flag_descriptions::kEnterpriseBadgingForNtpFooterName,
     flag_descriptions::kEnterpriseBadgingForNtpFooterDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kEnterpriseBadgingForNtpFooter)},
    {"enable-enterprise-profile-required-interstitial",
     flag_descriptions::kManagedProfileRequiredInterstitialName,
     flag_descriptions::kManagedProfileRequiredInterstitialDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kManagedProfileRequiredInterstitial)},
    {"enable-experimental-webassembly-features",
     flag_descriptions::kExperimentalWebAssemblyFeaturesName,
     flag_descriptions::kExperimentalWebAssemblyFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalWebAssemblyFeatures)},
    {"enable-experimental-webassembly-jspi",
     flag_descriptions::kExperimentalWebAssemblyJSPIName,
     flag_descriptions::kExperimentalWebAssemblyJSPIDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kEnableExperimentalWebAssemblyJSPI)},
    {"enable-webassembly-baseline", flag_descriptions::kEnableWasmBaselineName,
     flag_descriptions::kEnableWasmBaselineDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyBaseline)},
    {"enable-webassembly-lazy-compilation",
     flag_descriptions::kEnableWasmLazyCompilationName,
     flag_descriptions::kEnableWasmLazyCompilationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyLazyCompilation)},
    {"enable-webassembly-tiering", flag_descriptions::kEnableWasmTieringName,
     flag_descriptions::kEnableWasmTieringDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyTiering)},
    {"enable-future-v8-vm-features", flag_descriptions::kV8VmFutureName,
     flag_descriptions::kV8VmFutureDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kV8VmFuture)},
    {"enable-gpu-rasterization", flag_descriptions::kGpuRasterizationName,
     flag_descriptions::kGpuRasterizationDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableGpuRasterizationChoices)},
    {"enable-experimental-web-platform-features",
     flag_descriptions::kExperimentalWebPlatformFeaturesName,
     flag_descriptions::kExperimentalWebPlatformFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalWebPlatformFeatures)},
    {"top-chrome-touch-ui", flag_descriptions::kTopChromeTouchUiName,
     flag_descriptions::kTopChromeTouchUiDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kTopChromeTouchUiChoices)},

#if BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
    {flag_descriptions::kWebUITabStripFlagId,
     flag_descriptions::kWebUITabStripName,
     flag_descriptions::kWebUITabStripDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebUITabStrip)},
    {"webui-tab-strip-context-menu-after-tap",
     flag_descriptions::kWebUITabStripContextMenuAfterTapName,
     flag_descriptions::kWebUITabStripContextMenuAfterTapDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebUITabStripContextMenuAfterTap)},
#endif  // BUILDFLAG(ENABLE_WEBUI_TAB_STRIP)
#if BUILDFLAG(IS_CHROMEOS)
    {"allow-apn-modification-policy",
     flag_descriptions::kAllowApnModificationPolicyName,
     flag_descriptions::kAllowApnModificationPolicyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAllowApnModificationPolicy)},
    {"alt-click-and-six-pack-customization",
     flag_descriptions::kAltClickAndSixPackCustomizationName,
     flag_descriptions::kAltClickAndSixPackCustomizationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAltClickAndSixPackCustomization)},
    {"apn-policies", flag_descriptions::kApnPoliciesName,
     flag_descriptions::kApnPoliciesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kApnPolicies)},
    {"apn-revamp", flag_descriptions::kApnRevampName,
     flag_descriptions::kApnRevampDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kApnRevamp)},
    {"audio-selection-improvement",
     flag_descriptions::kAudioSelectionImprovementName,
     flag_descriptions::kAudioSelectionImprovementDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAudioSelectionImprovement)},
    {"reset-audio-selection-improvement-pref",
     flag_descriptions::kResetAudioSelectionImprovementPrefName,
     flag_descriptions::kResetAudioSelectionImprovementPrefDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kResetAudioSelectionImprovementPref)},
    {"cras-processor-wav-dump", flag_descriptions::kCrasProcessorWavDumpName,
     flag_descriptions::kCrasProcessorWavDumpDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootCrasProcessorWavDump")},
    {"disable-explicit-dma-fences",
     flag_descriptions::kDisableExplicitDmaFencesName,
     flag_descriptions::kDisableExplicitDmaFencesDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kDisableExplicitDmaFences)},
    // TODO(crbug.com/40652358): Remove this flag and provision when HDR is
    // fully
    //  supported on ChromeOS.
    {"use-hdr-transfer-function",
     flag_descriptions::kUseHDRTransferFunctionName,
     flag_descriptions::kUseHDRTransferFunctionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kUseHDRTransferFunction)},
    {"enable-external-display-hdr10",
     flag_descriptions::kEnableExternalDisplayHdr10Name,
     flag_descriptions::kEnableExternalDisplayHdr10Description, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kEnableExternalDisplayHDR10Mode)},
    {"ash-capture-mode-education", flag_descriptions::kCaptureModeEducationName,
     flag_descriptions::kCaptureModeEducationDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kCaptureModeEducation,
                                    kCaptureModeEducationVariations,
                                    "CaptureModeEducation")},
    {"ash-capture-mode-education-bypass-limits",
     flag_descriptions::kCaptureModeEducationBypassLimitsName,
     flag_descriptions::kCaptureModeEducationBypassLimitsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCaptureModeEducationBypassLimits)},
    {"ash-limit-shelf-items-to-active-desk",
     flag_descriptions::kLimitShelfItemsToActiveDeskName,
     flag_descriptions::kLimitShelfItemsToActiveDeskDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPerDeskShelf)},
    {"ash-enable-unified-desktop",
     flag_descriptions::kAshEnableUnifiedDesktopName,
     flag_descriptions::kAshEnableUnifiedDesktopDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kEnableUnifiedDesktop)},
    {"disable-system-blur", flag_descriptions::kDisableSystemBlur,
     flag_descriptions::kDisableSystemBlurDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kDisableSystemBlur)},
    {"bluetooth-audio-le-audio-only",
     flag_descriptions::kBluetoothAudioLEAudioOnlyName,
     flag_descriptions::kBluetoothAudioLEAudioOnlyDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootBluetoothAudioLEAudioOnly")},
    {"bluetooth-btsnoop-internals",
     flag_descriptions::kBluetoothBtsnoopInternalsName,
     flag_descriptions::kBluetoothBtsnoopInternalsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(
         chromeos::bluetooth::features::kBluetoothBtsnoopInternals)},
    {"bluetooth-floss-telephony",
     flag_descriptions::kBluetoothFlossTelephonyName,
     flag_descriptions::kBluetoothFlossTelephonyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(
         chromeos::bluetooth::features::kBluetoothFlossTelephony)},
    {kBluetoothUseFlossInternalName, flag_descriptions::kBluetoothUseFlossName,
     flag_descriptions::kBluetoothUseFlossDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(floss::features::kFlossEnabled)},
    {kBluetoothUseLLPrivacyInternalName,
     flag_descriptions::kBluetoothUseLLPrivacyName,
     flag_descriptions::kBluetoothUseLLPrivacyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(bluez::features::kLinkLayerPrivacy)},
    {"campbell-glyph", flag_descriptions::kCampbellGlyphName,
     flag_descriptions::kCampbellGlyphDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kCampbellGlyph,
                                    kCampbellGlyphVariations,
                                    "GampbellGlyph")},
    {"campbell-key", flag_descriptions::kCampbellKeyName,
     flag_descriptions::kCampbellKeyDescription, kOsCrOS,
     STRING_VALUE_TYPE(ash::switches::kCampbellKey, "")},
    {"cellular-bypass-esim-installation-connectivity-check",
     flag_descriptions::kCellularBypassESimInstallationConnectivityCheckName,
     flag_descriptions::
         kCellularBypassESimInstallationConnectivityCheckDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kCellularBypassESimInstallationConnectivityCheck)},
    {"cellular-use-second-euicc",
     flag_descriptions::kCellularUseSecondEuiccName,
     flag_descriptions::kCellularUseSecondEuiccDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCellularUseSecondEuicc)},
    {"enable-cros-privacy-hub", flag_descriptions::kCrosPrivacyHubName,
     flag_descriptions::kCrosPrivacyHubDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrosPrivacyHub)},
    {"enable-cros-separate-geo-api-key",
     flag_descriptions::kCrosSeparateGeoApiKeyName,
     flag_descriptions::kCrosSeparateGeoApiKeyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrosSeparateGeoApiKey)},
    {"enable-cros-cached-location-provider",
     flag_descriptions::kCrosCachedLocationProviderName,
     flag_descriptions::kCrosCachedLocationProviderDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kCachedLocationProvider)},
    {"cros-components", flag_descriptions::kCrosComponentsName,
     flag_descriptions::kCrosComponentsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kCrosComponents)},
    {"disable-cancel-all-touches",
     flag_descriptions::kDisableCancelAllTouchesName,
     flag_descriptions::kDisableCancelAllTouchesDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kDisableCancelAllTouches)},
    {
        "enable-background-blur",
        flag_descriptions::kEnableBackgroundBlurName,
        flag_descriptions::kEnableBackgroundBlurDescription,
        kOsCrOS,
        FEATURE_VALUE_TYPE(ash::features::kEnableBackgroundBlur),
    },
    {
        "enable-brightness-control-in-settings",
        flag_descriptions::kEnableBrightnessControlInSettingsName,
        flag_descriptions::kEnableBrightnessControlInSettingsDescription,
        kOsCrOS,
        FEATURE_VALUE_TYPE(ash::features::kEnableBrightnessControlInSettings),
    },
    {"list-all-display-modes", flag_descriptions::kListAllDisplayModesName,
     flag_descriptions::kListAllDisplayModesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kListAllDisplayModes)},
    {"enable-edid-based-display-ids",
     flag_descriptions::kEnableEdidBasedDisplayIdsName,
     flag_descriptions::kEnableEdidBasedDisplayIdsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kEnableEdidBasedDisplayIds)},
    {"enable-wifi-qos", flag_descriptions::kEnableWifiQosName,
     flag_descriptions::kEnableWifiQosDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableWifiQos)},
    {"enable-wifi-qos-enterprise",
     flag_descriptions::kEnableWifiQosEnterpriseName,
     flag_descriptions::kEnableWifiQosEnterpriseDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableWifiQosEnterprise)},
    {"esim-empty-activation-code-support",
     flag_descriptions::kESimEmptyActivationCodeSupportedName,
     flag_descriptions::kESimEmptyActivationCodeSupportedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kESimEmptyActivationCodeSupported)},
    {"instant-hotspot-on-nearby",
     flag_descriptions::kInstantHotspotOnNearbyName,
     flag_descriptions::kInstantHotspotOnNearbyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kInstantHotspotOnNearby)},
    {"instant-hotspot-rebrand", flag_descriptions::kInstantHotspotRebrandName,
     flag_descriptions::kInstantHotspotRebrandDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kInstantHotspotRebrand)},
    {"instant-tethering", flag_descriptions::kTetherName,
     flag_descriptions::kTetherDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kInstantTethering)},
    {"deprecate-alt-click", flag_descriptions::kDeprecateAltClickName,
     flag_descriptions::kDeprecateAltClickDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kDeprecateAltClick)},
    {"show-bluetooth-debug-log-toggle",
     flag_descriptions::kShowBluetoothDebugLogToggleName,
     flag_descriptions::kShowBluetoothDebugLogToggleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kShowBluetoothDebugLogToggle)},
    {"show-taps", flag_descriptions::kShowTapsName,
     flag_descriptions::kShowTapsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(ash::switches::kShowTaps)},
    {"show-touch-hud", flag_descriptions::kShowTouchHudName,
     flag_descriptions::kShowTouchHudDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(ash::switches::kAshTouchHud)},
    {"tiled-display-support", flag_descriptions::kTiledDisplaySupportName,
     flag_descriptions::kTiledDisplaySupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kTiledDisplaySupport)},
    {"wake-on-wifi-allowed", flag_descriptions::kWakeOnWifiAllowedName,
     flag_descriptions::kWakeOnWifiAllowedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWakeOnWifiAllowed)},
    {"microphone-mute-switch-device",
     flag_descriptions::kMicrophoneMuteSwitchDeviceName,
     flag_descriptions::kMicrophoneMuteSwitchDeviceDescription, kOsCrOS,
     SINGLE_VALUE_TYPE("enable-microphone-mute-switch-device")},
    {"wifi-connect-mac-address-randomization",
     flag_descriptions::kWifiConnectMacAddressRandomizationName,
     flag_descriptions::kWifiConnectMacAddressRandomizationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWifiConnectMacAddressRandomization)},
    {"wifi-concurrency", flag_descriptions::kWifiConcurrencyName,
     flag_descriptions::kWifiConcurrencyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWifiConcurrency)},
    {"disable-dns-proxy", flag_descriptions::kDisableDnsProxyName,
     flag_descriptions::kDisableDnsProxyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDisableDnsProxy)},
    {"firmware-update-ui-v2", flag_descriptions::kFirmwareUpdateUIV2Name,
     flag_descriptions::kFirmwareUpdateUIV2Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFirmwareUpdateUIV2)},
    {"multi-zone-rgb-keyboard", flag_descriptions::kMultiZoneRgbKeyboardName,
     flag_descriptions::kMultiZoneRgbKeyboardDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kMultiZoneRgbKeyboard)},
    {"enable-rfc-8925", flag_descriptions::kEnableRFC8925Name,
     flag_descriptions::kEnableRFC8925Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableRFC8925)},
    {"enable-root-ns-dns-proxy", flag_descriptions::kEnableRootNsDnsProxyName,
     flag_descriptions::kEnableRootNsDnsProxyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableRootNsDnsProxy)},
    {"support-f11-and-f12-shortcuts",
     flag_descriptions::kSupportF11AndF12ShortcutsName,
     flag_descriptions::kSupportF11AndF12ShortcutsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kSupportF11AndF12KeyShortcuts)},
    {"disconnect-wifi-on-ethernet-connected",
     flag_descriptions::kDisconnectWiFiOnEthernetConnectedName,
     flag_descriptions::kDisconnectWiFiOnEthernetConnectedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDisconnectWiFiOnEthernetConnected)},
    {"cros-apps-background-event-handling",
     flag_descriptions::kCrosAppsBackgroundEventHandlingName,
     flag_descriptions::kCrosAppsBackgroundEventHandlingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kCrosAppsBackgroundEventHandling)},
    {"disable-idle-sockets-close-on-memory-pressure",
     flag_descriptions::kDisableIdleSocketsCloseOnMemoryPressureName,
     flag_descriptions::kDisableIdleSocketsCloseOnMemoryPressureDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         chromeos::features::kDisableIdleSocketsCloseOnMemoryPressure)},
    {"one-group-per-renderer", flag_descriptions::kOneGroupPerRendererName,
     flag_descriptions::kOneGroupPerRendererDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(base::kOneGroupPerRenderer)},
    {"use-dhcpcd10", flag_descriptions::kUseDHCPCD10Name,
     flag_descriptions::kUseDHCPCD10Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kUseDHCPCD10)},
#endif  // BUILDFLAG(IS_CHROMEOS)
    {
        "disable-accelerated-video-decode",
        flag_descriptions::kAcceleratedVideoDecodeName,
        flag_descriptions::kAcceleratedVideoDecodeDescription,
        kOsMac | kOsWin | kOsCrOS | kOsAndroid | kOsLinux,
        SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedVideoDecode),
    },
    {
        "disable-accelerated-video-encode",
        flag_descriptions::kAcceleratedVideoEncodeName,
        flag_descriptions::kAcceleratedVideoEncodeDescription,
        kOsMac | kOsWin | kOsCrOS | kOsAndroid,
        SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedVideoEncode),
    },
#if BUILDFLAG(IS_WIN)
    {
        "enable-hardware-secure-decryption",
        flag_descriptions::kHardwareSecureDecryptionName,
        flag_descriptions::kHardwareSecureDecryptionDescription,
        kOsWin,
        FEATURE_VALUE_TYPE(media::kHardwareSecureDecryption),
    },
    {
        "enable-hardware-secure-decryption-experiment",
        flag_descriptions::kHardwareSecureDecryptionExperimentName,
        flag_descriptions::kHardwareSecureDecryptionExperimentDescription,
        kOsWin,
        FEATURE_VALUE_TYPE(media::kHardwareSecureDecryptionExperiment),
    },
    {
        "enable-hardware-secure-decryption-fallback",
        flag_descriptions::kHardwareSecureDecryptionFallbackName,
        flag_descriptions::kHardwareSecureDecryptionFallbackDescription,
        kOsWin,
        FEATURE_VALUE_TYPE(media::kHardwareSecureDecryptionFallback),
    },
    {
        "enable-media-foundation-clear",
        flag_descriptions::kMediaFoundationClearName,
        flag_descriptions::kMediaFoundationClearDescription,
        kOsWin,
        FEATURE_VALUE_TYPE(media::kMediaFoundationClearPlayback),
    },
    {"enable-media-foundation-clear-rendering-strategy",
     flag_descriptions::kMediaFoundationClearStrategyName,
     flag_descriptions::kMediaFoundationClearStrategyDescription, kOsWin,
     FEATURE_WITH_PARAMS_VALUE_TYPE(media::kMediaFoundationClearRendering,
                                    kMediaFoundationClearStrategyVariations,
                                    "MediaFoundationClearRendering")},
    {
        "enable-media-foundation-camera-usage-monitoring",
        flag_descriptions::kMediaFoundationCameraUsageMonitoringName,
        flag_descriptions::kMediaFoundationCameraUsageMonitoringDescription,
        kOsWin,
        FEATURE_VALUE_TYPE(features::kMediaFoundationCameraUsageMonitoring),
    },
    {
        "enable-waitable-swap-chain",
        flag_descriptions::kUseWaitableSwapChainName,
        flag_descriptions::kUseWaitableSwapChainDescription,
        kOsWin,
        FEATURE_WITH_PARAMS_VALUE_TYPE(features::kDXGIWaitableSwapChain,
                                       kDXGIWaitableSwapChainVariations,
                                       "DXGIWaitableSwapChain"),
    },
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
    {
        "fluent-overlay-scrollbars",
        flag_descriptions::kFluentOverlayScrollbarsName,
        flag_descriptions::kFluentOverlayScrollbarsDescription,
        kOsWin | kOsLinux,
        FEATURE_VALUE_TYPE(features::kFluentOverlayScrollbar),
    },
    {
        "fluent-scrollbars",
        flag_descriptions::kFluentScrollbarsName,
        flag_descriptions::kFluentScrollbarsDescription,
        kOsWin | kOsLinux,
        FEATURE_VALUE_TYPE(features::kFluentScrollbar),
    },
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
#if BUILDFLAG(IS_CHROMEOS)
    {
        "zero-copy-video-capture",
        flag_descriptions::kZeroCopyVideoCaptureName,
        flag_descriptions::kZeroCopyVideoCaptureDescription,
        kOsCrOS,
        ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
            switches::kVideoCaptureUseGpuMemoryBuffer,
            "1",
            switches::kDisableVideoCaptureUseGpuMemoryBuffer,
            "1"),
    },
    {
        "ash-debug-shortcuts",
        flag_descriptions::kDebugShortcutsName,
        flag_descriptions::kDebugShortcutsDescription,
        kOsAll,
        SINGLE_VALUE_TYPE(ash::switches::kAshDebugShortcuts),
    },
    {"ui-slow-animations", flag_descriptions::kUiSlowAnimationsName,
     flag_descriptions::kUiSlowAnimationsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kUISlowAnimations)},
#endif  // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_WIN)
    {
        "zero-copy-video-capture",
        flag_descriptions::kZeroCopyVideoCaptureName,
        flag_descriptions::kZeroCopyVideoCaptureDescription,
        kOsWin,
        FEATURE_VALUE_TYPE(media::kMediaFoundationD3D11VideoCapture),
    },
#endif  // BUILDFLAG(IS_WIN)
    {"enable-show-autofill-signatures",
     flag_descriptions::kShowAutofillSignaturesName,
     flag_descriptions::kShowAutofillSignaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(autofill::switches::kShowAutofillSignatures)},
    {"wallet-service-use-sandbox",
     flag_descriptions::kWalletServiceUseSandboxName,
     flag_descriptions::kWalletServiceUseSandboxDescription,
     kOsAndroid | kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         autofill::switches::kWalletServiceUseSandbox,
         "1",
         autofill::switches::kWalletServiceUseSandbox,
         "0")},
    {"enable-web-bluetooth", flag_descriptions::kWebBluetoothName,
     flag_descriptions::kWebBluetoothDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWebBluetooth)},
    {"enable-web-bluetooth-new-permissions-backend",
     flag_descriptions::kWebBluetoothNewPermissionsBackendName,
     flag_descriptions::kWebBluetoothNewPermissionsBackendDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebBluetoothNewPermissionsBackend)},
    {"enable-webusb-device-detection",
     flag_descriptions::kWebUsbDeviceDetectionName,
     flag_descriptions::kWebUsbDeviceDetectionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebUsbDeviceDetection)},
#if defined(USE_AURA)
    {"overscroll-history-navigation",
     flag_descriptions::kOverscrollHistoryNavigationName,
     flag_descriptions::kOverscrollHistoryNavigationDescription, kOsAura,
     FEATURE_VALUE_TYPE(features::kOverscrollHistoryNavigation)},
    {"pull-to-refresh", flag_descriptions::kPullToRefreshName,
     flag_descriptions::kPullToRefreshDescription, kOsAura,
     MULTI_VALUE_TYPE(kPullToRefreshChoices)},
#endif  // USE_AURA
    {"enable-touch-drag-drop", flag_descriptions::kTouchDragDropName,
     flag_descriptions::kTouchDragDropDescription, kOsWin | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kTouchDragAndDrop)},
    {"touch-selection-strategy", flag_descriptions::kTouchSelectionStrategyName,
     flag_descriptions::kTouchSelectionStrategyDescription,
     kOsAndroid,  // TODO(mfomitchev): Add CrOS/Win/Linux support soon.
     MULTI_VALUE_TYPE(kTouchTextSelectionStrategyChoices)},
#if BUILDFLAG(IS_CHROMEOS)
    {"enable-virtual-keyboard", flag_descriptions::kVirtualKeyboardName,
     flag_descriptions::kVirtualKeyboardDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(keyboard::switches::kEnableVirtualKeyboard)},
    {"disable-virtual-keyboard",
     flag_descriptions::kVirtualKeyboardDisabledName,
     flag_descriptions::kVirtualKeyboardDisabledDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(keyboard::switches::kDisableVirtualKeyboard)},
#endif  // BUILDFLAG(IS_CHROMEOS)
    {"enable-webgl-developer-extensions",
     flag_descriptions::kWebglDeveloperExtensionsName,
     flag_descriptions::kWebglDeveloperExtensionsDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGLDeveloperExtensions)},
    {"enable-webgl-draft-extensions",
     flag_descriptions::kWebglDraftExtensionsName,
     flag_descriptions::kWebglDraftExtensionsDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGLDraftExtensions)},
    {"enable-zero-copy", flag_descriptions::kZeroCopyName,
     flag_descriptions::kZeroCopyDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(blink::switches::kEnableZeroCopy,
                               blink::switches::kDisableZeroCopy)},
    {"enable-vulkan", flag_descriptions::kEnableVulkanName,
     flag_descriptions::kEnableVulkanDescription,
     kOsWin | kOsLinux | kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kVulkan)},
    {"default-angle-vulkan", flag_descriptions::kDefaultAngleVulkanName,
     flag_descriptions::kDefaultAngleVulkanDescription,
     kOsLinux | kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kDefaultANGLEVulkan)},
    {"vulkan-from-angle", flag_descriptions::kVulkanFromAngleName,
     flag_descriptions::kVulkanFromAngleDescription,
     kOsLinux | kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kVulkanFromANGLE)},

#if !BUILDFLAG(IS_CHROMEOS)
    {"enable-system-notifications",
     flag_descriptions::kNotificationsSystemFlagName,
     flag_descriptions::kNotificationsSystemFlagDescription,
     kOsMac | kOsLinux | kOsWin,
     FEATURE_VALUE_TYPE(features::kSystemNotifications)},
#endif  // !BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_CHROMEOS)
    {"enable-ongoing-processes", flag_descriptions::kEnableOngoingProcessesName,
     flag_descriptions::kEnableOngoingProcessesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kOngoingProcesses)},
#endif  // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_ANDROID)
    {"adaptive-button-in-top-toolbar-page-summary",
     flag_descriptions::kAdaptiveButtonInTopToolbarPageSummaryName,
     flag_descriptions::kAdaptiveButtonInTopToolbarPageSummaryDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kAdaptiveButtonInTopToolbarPageSummary,
         kAdaptiveButtonInTopToolbarPageSummaryVariations,
         "AdaptiveButtonInTopToolbarPageSummary")},
    {"contextual-page-actions-share-model",
     flag_descriptions::kContextualPageActionsShareModelName,
     flag_descriptions::kContextualPageActionsShareModelDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         segmentation_platform::features::kContextualPageActionShareModel)},

    {"reader-mode-auto-distill", flag_descriptions::kReaderModeAutoDistillName,
     flag_descriptions::kReaderModeAutoDistillDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(dom_distiller::kReaderModeAutoDistill)},
    {"reader-mode-distill-in-app",
     flag_descriptions::kReaderModeDistillInAppName,
     flag_descriptions::kReaderModeDistillInAppDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(dom_distiller::kReaderModeDistillInApp)},
    {"reader-mode-heuristics", flag_descriptions::kReaderModeHeuristicsName,
     flag_descriptions::kReaderModeHeuristicsDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kReaderModeHeuristicsChoices)},
    {"reader-mode-improvements", flag_descriptions::kReaderModeImprovementsName,
     flag_descriptions::kReaderModeImprovementsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(dom_distiller::kReaderModeImprovements,
                                    kReaderModeImprovementsChoices,
                                    "Reader Mode Improvements")},
    {"reader-mode-use-readability",
     flag_descriptions::kReaderModeUseReadabilityName,
     flag_descriptions::kReaderModeUseReadabilityDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(dom_distiller::kReaderModeUseReadability,
                                    kReaderModeUseReadabilityChoices,
                                    "Reader Mode use readability")},
#endif  // BUILDFLAG(IS_ANDROID)
    {"in-product-help-demo-mode-choice",
     flag_descriptions::kInProductHelpDemoModeChoiceName,
     flag_descriptions::kInProductHelpDemoModeChoiceDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         feature_engagement::kIPHDemoMode,
         feature_engagement::kIPHDemoModeChoiceVariations,
         "IPH_DemoMode")},
#if BUILDFLAG(IS_CHROMEOS)
    {"enable-lock-screen-notification",
     flag_descriptions::kLockScreenNotificationName,
     flag_descriptions::kLockScreenNotificationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kLockScreenNotifications)},
    {"enable-service-workers-for-chrome-untrusted",
     flag_descriptions::kEnableServiceWorkersForChromeUntrustedName,
     flag_descriptions::kEnableServiceWorkersForChromeUntrustedDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableServiceWorkersForChromeUntrusted)},
    {"enterprise-reporting-ui", flag_descriptions::kEnterpriseReportingUIName,
     flag_descriptions::kEnterpriseReportingUIDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnterpriseReportingUI)},
    {"chromebox-usb-passthrough-restrictions",
     flag_descriptions::kChromeboxUsbPassthroughRestrictionsName,
     flag_descriptions::kChromeboxUsbPassthroughRestrictionsDescription,
     kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE(
         "CrOSLateBootChromeboxUsbPassthroughRestrictions")},
    {"disable-bruschetta-install-checks",
     flag_descriptions::kDisableBruschettaInstallChecksName,
     flag_descriptions::kDisableBruschettaInstallChecksDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDisableBruschettaInstallChecks)},
    {"crostini-reset-lxd-db", flag_descriptions::kCrostiniResetLxdDbName,
     flag_descriptions::kCrostiniResetLxdDbDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrostiniResetLxdDb)},
    {"terminal-dev", flag_descriptions::kTerminalDevName,
     flag_descriptions::kTerminalDevDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kTerminalDev)},
    {"permissive-usb-passthrough",
     flag_descriptions::kPermissiveUsbPassthroughName,
     flag_descriptions::kPermissiveUsbPassthroughDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootPermissiveUsbPassthrough")},
    {"camera-angle-backend", flag_descriptions::kCameraAngleBackendName,
     flag_descriptions::kCameraAngleBackendDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootCameraAngleBackend")},
    {"crostini-containerless", flag_descriptions::kCrostiniContainerlessName,
     flag_descriptions::kCrostiniContainerlessDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrostiniContainerless)},
    {"crostini-multi-container", flag_descriptions::kCrostiniMultiContainerName,
     flag_descriptions::kCrostiniMultiContainerDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrostiniMultiContainer)},
    {"crostini-qt-ime-support", flag_descriptions::kCrostiniQtImeSupportName,
     flag_descriptions::kCrostiniQtImeSupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrostiniQtImeSupport)},
    {"crostini-virtual-keyboard-support",
     flag_descriptions::kCrostiniVirtualKeyboardSupportName,
     flag_descriptions::kCrostiniVirtualKeyboardSupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrostiniVirtualKeyboardSupport)},
    {"notifications-ignore-require-interaction",
     flag_descriptions::kNotificationsIgnoreRequireInteractionName,
     flag_descriptions::kNotificationsIgnoreRequireInteractionDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kNotificationsIgnoreRequireInteraction)},
    {"sys-ui-holdback-drive-integration",
     flag_descriptions::kSysUiShouldHoldbackDriveIntegrationName,
     flag_descriptions::kSysUiShouldHoldbackDriveIntegrationDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSysUiShouldHoldbackDriveIntegration)},
    {"sys-ui-holdback-task-management",
     flag_descriptions::kSysUiShouldHoldbackTaskManagementName,
     flag_descriptions::kSysUiShouldHoldbackTaskManagementDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSysUiShouldHoldbackTaskManagement)},
    {"offline-items-in-notifications",
     flag_descriptions::kOfflineItemsInNotificationsName,
     flag_descriptions::kOfflineItemsInNotificationsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kOfflineItemsInNotifications)},

#endif  // BUILDFLAG(IS_CHROMEOS)
#if (BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID))
    {"mojo-linux-sharedmem", flag_descriptions::kMojoLinuxChannelSharedMemName,
     flag_descriptions::kMojoLinuxChannelSharedMemDescription,
     kOsCrOS | kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(mojo::core::kMojoLinuxChannelSharedMem)},
#endif
#if BUILDFLAG(IS_ANDROID)
    {"enable-site-isolation-for-password-sites",
     flag_descriptions::kSiteIsolationForPasswordSitesName,
     flag_descriptions::kSiteIsolationForPasswordSitesDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         site_isolation::features::kSiteIsolationForPasswordSites)},
    {"enable-site-per-process", flag_descriptions::kStrictSiteIsolationName,
     flag_descriptions::kStrictSiteIsolationDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kSitePerProcess)},
#endif

#if !BUILDFLAG(IS_ANDROID)
#if !BUILDFLAG(IS_CHROMEOS)
    {"enable-isolated-web-apps", flag_descriptions::kEnableIsolatedWebAppsName,
     flag_descriptions::kEnableIsolatedWebAppsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kIsolatedWebApps)},
#endif  // !BUILDFLAG(IS_CHROMEOS)
    {"direct-sockets-in-service-workers",
     flag_descriptions::kDirectSocketsInServiceWorkersName,
     flag_descriptions::kDirectSocketsInServiceWorkersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDirectSocketsInServiceWorkers)},
    {"direct-sockets-in-shared-workers",
     flag_descriptions::kDirectSocketsInSharedWorkersName,
     flag_descriptions::kDirectSocketsInSharedWorkersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDirectSocketsInSharedWorkers)},
#if BUILDFLAG(IS_CHROMEOS)
    {"enable-isolated-web-app-managed-guest-session-install",
     flag_descriptions::kEnableIsolatedWebAppManagedGuestSessionInstallName,
     flag_descriptions::
         kEnableIsolatedWebAppManagedGuestSessionInstallDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kIsolatedWebAppManagedGuestSessionInstall)},
    {"enable-isolated-web-app-unmanaged-install",
     flag_descriptions::kEnableIsolatedWebAppUnmanagedInstallName,
     flag_descriptions::kEnableIsolatedWebAppUnmanagedInstallDescription,
     kOsCrOS, FEATURE_VALUE_TYPE(features::kIsolatedWebAppUnmanagedInstall)},
#endif
    {"enable-isolated-web-app-allowlist",
     flag_descriptions::kEnableIsolatedWebAppAllowlistName,
     flag_descriptions::kEnableIsolatedWebAppAllowlistDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kIsolatedWebAppManagedAllowlist)},
    {"enable-isolated-web-app-dev-mode",
     flag_descriptions::kEnableIsolatedWebAppDevModeName,
     flag_descriptions::kEnableIsolatedWebAppDevModeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kIsolatedWebAppDevMode)},
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"enable-iwa-key-distribution-component",
     flag_descriptions::kEnableIwaKeyDistributionComponentName,
     flag_descriptions::kEnableIwaKeyDistributionComponentDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(component_updater::kIwaKeyDistributionComponent)},
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"iwa-key-distribution-component-exp-cohort",
     flag_descriptions::kIwaKeyDistributionComponentExpCohortName,
     flag_descriptions::kIwaKeyDistributionComponentExpCohortDescription,
     kOsDesktop,
     STRING_VALUE_TYPE(component_updater::kIwaKeyDistributionComponentExpCohort,
                       "")},
#if BUILDFLAG(IS_CHROMEOS)
    {"install-isolated-web-app-from-url",
     flag_descriptions::kInstallIsolatedWebAppFromUrl,
     flag_descriptions::kInstallIsolatedWebAppFromUrlDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(switches::kInstallIsolatedWebAppFromUrl, "")},
#endif
#endif

#if !BUILDFLAG(IS_ANDROID)
    {"enable-controlled-frame", flag_descriptions::kEnableControlledFrameName,
     flag_descriptions::kEnableControlledFrameDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kControlledFrame)},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"isolate-origins", flag_descriptions::kIsolateOriginsName,
     flag_descriptions::kIsolateOriginsDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(switches::kIsolateOrigins, "")},
    {about_flags::kSiteIsolationTrialOptOutInternalName,
     flag_descriptions::kSiteIsolationOptOutName,
     flag_descriptions::kSiteIsolationOptOutDescription, kOsAll,
     MULTI_VALUE_TYPE(kSiteIsolationOptOutChoices)},
    {"isolation-by-default", flag_descriptions::kIsolationByDefaultName,
     flag_descriptions::kIsolationByDefaultDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kIsolationByDefault)},
    {"allow-insecure-localhost", flag_descriptions::kAllowInsecureLocalhostName,
     flag_descriptions::kAllowInsecureLocalhostDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kAllowInsecureLocalhost)},
    {"text-based-audio-descriptions",
     flag_descriptions::kTextBasedAudioDescriptionName,
     flag_descriptions::kTextBasedAudioDescriptionDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTextBasedAudioDescription)},
    {"enable-desktop-pwas-app-title",
     flag_descriptions::kDesktopPWAsAppTitleName,
     flag_descriptions::kDesktopPWAsAppTitleDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppEnableAppTitle)},
    {"enable-desktop-pwas-elided-extensions-menu",
     flag_descriptions::kDesktopPWAsElidedExtensionsMenuName,
     flag_descriptions::kDesktopPWAsElidedExtensionsMenuDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDesktopPWAsElidedExtensionsMenu)},
    {"enable-desktop-pwas-tab-strip",
     flag_descriptions::kDesktopPWAsTabStripName,
     flag_descriptions::kDesktopPWAsTabStripDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDesktopPWAsTabStrip)},
    {"enable-desktop-pwas-tab-strip-settings",
     flag_descriptions::kDesktopPWAsTabStripSettingsName,
     flag_descriptions::kDesktopPWAsTabStripSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDesktopPWAsTabStripSettings)},
    {"enable-desktop-pwas-tab-strip-customizations",
     flag_descriptions::kDesktopPWAsTabStripCustomizationsName,
     flag_descriptions::kDesktopPWAsTabStripCustomizationsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDesktopPWAsTabStripCustomizations)},
    {"enable-desktop-pwas-sub-apps", flag_descriptions::kDesktopPWAsSubAppsName,
     flag_descriptions::kDesktopPWAsSubAppsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kDesktopPWAsSubApps)},
    {"enable-desktop-pwas-scope-extensions",
     flag_descriptions::kDesktopPWAsScopeExtensionsName,
     flag_descriptions::kDesktopPWAsScopeExtensionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppEnableScopeExtensions)},
    {"enable-desktop-pwas-borderless",
     flag_descriptions::kDesktopPWAsBorderlessName,
     flag_descriptions::kDesktopPWAsBorderlessDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kWebAppBorderless)},
    {"enable-desktop-pwas-additional-windowing-controls",
     flag_descriptions::kDesktopPWAsAdditionalWindowingControlsName,
     flag_descriptions::kDesktopPWAsAdditionalWindowingControlsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kDesktopPWAsAdditionalWindowingControls)},
    {"record-web-app-debug-info", flag_descriptions::kRecordWebAppDebugInfoName,
     flag_descriptions::kRecordWebAppDebugInfoDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kRecordWebAppDebugInfo)},
    {"use-sync-sandbox", flag_descriptions::kSyncSandboxName,
     flag_descriptions::kSyncSandboxDescription, kOsAll,
     SINGLE_VALUE_TYPE_AND_VALUE(
         syncer::kSyncServiceURL,
         "https://chrome-sync.sandbox.google.com/chrome-sync/alpha")},
#if !BUILDFLAG(IS_ANDROID)
    {"media-router-cast-allow-all-ips",
     flag_descriptions::kMediaRouterCastAllowAllIPsName,
     flag_descriptions::kMediaRouterCastAllowAllIPsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kCastAllowAllIPsFeature)},
    {"media-remoting-without-fullscreen",
     flag_descriptions::kMediaRemotingWithoutFullscreenName,
     flag_descriptions::kMediaRemotingWithoutFullscreenDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kMediaRemotingWithoutFullscreen)},
    {"remote-playback-backend", flag_descriptions::kRemotePlaybackBackendName,
     flag_descriptions::kRemotePlaybackBackendDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kRemotePlaybackBackend)},
    {"allow-all-sites-to-initiate-mirroring",
     flag_descriptions::kAllowAllSitesToInitiateMirroringName,
     flag_descriptions::kAllowAllSitesToInitiateMirroringDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kAllowAllSitesToInitiateMirroring)},
    {"media-route-dial-provider",
     flag_descriptions::kDialMediaRouteProviderName,
     flag_descriptions::kDialMediaRouteProviderDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kDialMediaRouteProvider)},
    {"delay-media-sink-discovery",
     flag_descriptions::kDelayMediaSinkDiscoveryName,
     flag_descriptions::kDelayMediaSinkDiscoveryDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kDelayMediaSinkDiscovery)},
    {"show-cast-permission-rejected-error",
     flag_descriptions::kShowCastPermissionRejectedErrorName,
     flag_descriptions::kShowCastPermissionRejectedErrorDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kShowCastPermissionRejectedError)},
    {"cast-message-logging", flag_descriptions::kCastMessageLoggingName,
     flag_descriptions::kCastMessageLoggingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media_router::kCastMessageLogging)},

    {"cast-streaming-hardware-h264",
     flag_descriptions::kCastStreamingHardwareH264Name,
     flag_descriptions::kCastStreamingHardwareH264Description, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(
         switches::kCastStreamingForceEnableHardwareH264,
         switches::kCastStreamingForceDisableHardwareH264)},

    {"cast-streaming-hardware-hevc",
     flag_descriptions::kCastStreamingHardwareHevcName,
     flag_descriptions::kCastStreamingHardwareHevcDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingHardwareHevc)},

    {"cast-streaming-hardware-vp8",
     flag_descriptions::kCastStreamingHardwareVp8Name,
     flag_descriptions::kCastStreamingHardwareVp8Description, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(
         switches::kCastStreamingForceEnableHardwareVp8,
         switches::kCastStreamingForceDisableHardwareVp8)},

    {"cast-streaming-hardware-vp9",
     flag_descriptions::kCastStreamingHardwareVp9Name,
     flag_descriptions::kCastStreamingHardwareVp9Description, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(
         switches::kCastStreamingForceEnableHardwareVp9,
         switches::kCastStreamingForceDisableHardwareVp9)},

    {"cast-streaming-media-video-encoder",
     flag_descriptions::kCastStreamingMediaVideoEncoderName,
     flag_descriptions::kCastStreamingMediaVideoEncoderDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingMediaVideoEncoder)},

    {"cast-streaming-performance-overlay",
     flag_descriptions::kCastStreamingPerformanceOverlayName,
     flag_descriptions::kCastStreamingPerformanceOverlayDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingPerformanceOverlay)},

    {"enable-cast-streaming-av1", flag_descriptions::kCastStreamingAv1Name,
     flag_descriptions::kCastStreamingAv1Description, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingAv1)},

#if BUILDFLAG(IS_MAC)
    {"enable-cast-streaming-mac-hardware-h264",
     flag_descriptions::kCastStreamingMacHardwareH264Name,
     flag_descriptions::kCastStreamingMacHardwareH264Description, kOsMac,
     FEATURE_VALUE_TYPE(media::kCastStreamingMacHardwareH264)},
    {"use-network-framework-for-local-discovery",
     flag_descriptions::kUseNetworkFrameworkForLocalDiscoveryName,
     flag_descriptions::kUseNetworkFrameworkForLocalDiscoveryDescription,
     kOsMac,
     FEATURE_VALUE_TYPE(media_router::kUseNetworkFrameworkForLocalDiscovery)},
#endif

#if BUILDFLAG(IS_WIN)
    {"enable-cast-streaming-win-hardware-h264",
     flag_descriptions::kCastStreamingWinHardwareH264Name,
     flag_descriptions::kCastStreamingWinHardwareH264Description, kOsWin,
     FEATURE_VALUE_TYPE(media::kCastStreamingWinHardwareH264)},
#endif

    {"enable-cast-streaming-vp8", flag_descriptions::kCastStreamingVp8Name,
     flag_descriptions::kCastStreamingVp8Description, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingVp8)},

    {"enable-cast-streaming-vp9", flag_descriptions::kCastStreamingVp9Name,
     flag_descriptions::kCastStreamingVp9Description, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kCastStreamingVp9)},

    {"enable-cast-streaming-with-hidpi",
     flag_descriptions::kCastEnableStreamingWithHiDPIName,
     flag_descriptions::kCastEnableStreamingWithHiDPIDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(mirroring::features::kCastEnableStreamingWithHiDPI)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"force-off-text-autosizing",
     flag_descriptions::kForceOffTextAutosizingName,
     flag_descriptions::kForceOffTextAutosizingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kForceOffTextAutosizing)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
    {"mac-catap-system-audio-loopback-capture",
     flag_descriptions::kMacCatapSystemAudioLoopbackCaptureName,
     flag_descriptions::kMacCatapSystemAudioLoopbackCaptureDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kMacCatapSystemAudioLoopbackCapture)},

    {"mac-loopback-audio-for-screen-share",
     flag_descriptions::kMacLoopbackAudioForScreenShareName,
     flag_descriptions::kMacLoopbackAudioForScreenShareDescription, kOsMac,
     FEATURE_VALUE_TYPE(media::kMacLoopbackAudioForScreenShare)},

    {"use-sc-content-sharing-picker",
     flag_descriptions::kUseSCContentSharingPickerName,
     flag_descriptions::kUseSCContentSharingPickerDescription, kOsMac,
     FEATURE_VALUE_TYPE(media::kUseSCContentSharingPicker)},
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_LINUX)
    {"pulseaudio-loopback-for-cast",
     flag_descriptions::kPulseaudioLoopbackForCastName,
     flag_descriptions::kPulseaudioLoopbackForCastDescription, kOsLinux,
     FEATURE_VALUE_TYPE(media::kPulseaudioLoopbackForCast)},

    {"pulseaudio-loopback-for-screen-share",
     flag_descriptions::kPulseaudioLoopbackForScreenShareName,
     flag_descriptions::kPulseaudioLoopbackForScreenShareDescription, kOsLinux,
     FEATURE_VALUE_TYPE(media::kPulseaudioLoopbackForScreenShare)},

    {"ozone-platform-hint", flag_descriptions::kOzonePlatformHintName,
     flag_descriptions::kOzonePlatformHintDescription, kOsLinux,
     MULTI_VALUE_TYPE(kOzonePlatformHintRuntimeChoices)},

    {"simplified-tab-drag-ui", flag_descriptions::kSimplifiedTabDragUIName,
     flag_descriptions::kSimplifiedTabDragUIDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kAllowWindowDragUsingSystemDragDrop)},

    {"wayland-per-window-scaling",
     flag_descriptions::kWaylandPerWindowScalingName,
     flag_descriptions::kWaylandPerWindowScalingDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWaylandPerSurfaceScale)},

    {"wayland-text-input-v3", flag_descriptions::kWaylandTextInputV3Name,
     flag_descriptions::kWaylandTextInputV3Description, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWaylandTextInputV3)},

    {"wayland-ui-scaling", flag_descriptions::kWaylandUiScalingName,
     flag_descriptions::kWaylandUiScalingDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWaylandUiScale)},

    {"wayland-linux-drm-syncobj",
     flag_descriptions::kWaylandLinuxDrmSyncobjName,
     flag_descriptions::kWaylandLinuxDrmSyncobjDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWaylandLinuxDrmSyncobj)},

    {"wayland-session-management",
     flag_descriptions::kWaylandSessionManagementName,
     flag_descriptions::kWaylandSessionManagementDescription, kOsLinux,
     FEATURE_VALUE_TYPE(features::kWaylandSessionManagement)},
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(ENABLE_VR)
    {"webxr-projection-layers", flag_descriptions::kWebXrProjectionLayersName,
     flag_descriptions::kWebXrProjectionLayersDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXrLayers)},
    {"webxr-webgpu-binding", flag_descriptions::kWebXrWebGpuBindingName,
     flag_descriptions::kWebXrWebGpuBindingDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXrWebGpuBinding)},
    {"webxr-incubations", flag_descriptions::kWebXrIncubationsName,
     flag_descriptions::kWebXrIncubationsDescription, kOsAll,
     FEATURE_VALUE_TYPE(device::features::kWebXrIncubations)},
    {"webxr-internals", flag_descriptions::kWebXrInternalsName,
     flag_descriptions::kWebXrInternalsDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXrInternals)},
    {"webxr-runtime", flag_descriptions::kWebXrForceRuntimeName,
     flag_descriptions::kWebXrForceRuntimeDescription, kOsDesktop | kOsAndroid,
     MULTI_VALUE_TYPE(kWebXrForceRuntimeChoices)},
    {"webxr-hand-anonymization",
     flag_descriptions::kWebXrHandAnonymizationStrategyName,
     flag_descriptions::kWebXrHandAnonymizationStrategyDescription,
     kOsDesktop | kOsAndroid, MULTI_VALUE_TYPE(KWebXrHandAnonymizationChoices)},
    {"webxr-depth-performance", flag_descriptions::kWebXRDepthPerformanceName,
     flag_descriptions::kWebXRDepthPerformanceDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kWebXRDepthPerformance)},
#if BUILDFLAG(IS_ANDROID)
    {"webxr-shared-buffers", flag_descriptions::kWebXrSharedBuffersName,
     flag_descriptions::kWebXrSharedBuffersDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kWebXrSharedBuffers)},
#if BUILDFLAG(ENABLE_OPENXR)
    {"enable-openxr-android", flag_descriptions::kOpenXRName,
     flag_descriptions::kOpenXRDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kOpenXR)},
    {"enable-openxr-android-smooth-depth",
     flag_descriptions::kOpenXRAndroidSmoothDepthName,
     flag_descriptions::kOpenXRAndroidSmoothDepthDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kOpenXR)},
    {"enable-openxr-extended", flag_descriptions::kOpenXRExtendedFeaturesName,
     flag_descriptions::kOpenXRExtendedFeaturesDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kOpenXrExtendedFeatureSupport)},
#endif
#endif  // BUILDFLAG(IS_ANDROID)
#endif  // ENABLE_VR
#if BUILDFLAG(IS_CHROMEOS)
    {"disable-accelerated-mjpeg-decode",
     flag_descriptions::kAcceleratedMjpegDecodeName,
     flag_descriptions::kAcceleratedMjpegDecodeDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedMjpegDecode)},
#endif  // BUILDFLAG(IS_CHROMEOS)
    {"system-keyboard-lock", flag_descriptions::kSystemKeyboardLockName,
     flag_descriptions::kSystemKeyboardLockDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSystemKeyboardLock)},
#if BUILDFLAG(IS_ANDROID)
    {"notification-permission-rationale-dialog",
     flag_descriptions::kNotificationPermissionRationaleName,
     flag_descriptions::kNotificationPermissionRationaleDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kNotificationPermissionVariant,
         kNotificationPermissionRationaleVariations,
         "NotificationPermissionVariant")},
    {"notification-permission-rationale-bottom-sheet",
     flag_descriptions::kNotificationPermissionRationaleBottomSheetName,
     flag_descriptions::kNotificationPermissionRationaleBottomSheetDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNotificationPermissionBottomSheet)},
    {"reengagement-notification",
     flag_descriptions::kReengagementNotificationName,
     flag_descriptions::kReengagementNotificationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kReengagementNotification)},

    // Enterprise Data Controls
    {"enable-clipboard-data-controls-android",
     flag_descriptions::kEnableClipboardDataControlsAndroidName,
     flag_descriptions::kEnableClipboardDataControlsAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(data_controls::kEnableClipboardDataControlsAndroid)},

    {"right-edge-goes-forward-gesture-nav",
     flag_descriptions::kRightEdgeGoesForwardGestureNavName,
     flag_descriptions::kRightEdgeGoesForwardGestureNavDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kRightEdgeGoesForwardGestureNav)},

    // Android Edge to edge
    {"draw-cutout-edge-to-edge", flag_descriptions::kDrawCutoutEdgeToEdgeName,
     flag_descriptions::kDrawCutoutEdgeToEdgeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kDrawCutoutEdgeToEdge)},
    {"draw-key-native-edge-to-edge",
     flag_descriptions::kDrawKeyNativeEdgeToEdgeName,
     flag_descriptions::kDrawKeyNativeEdgeToEdgeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kDrawKeyNativeEdgeToEdge)},
    {"edge-to-edge-bottom-chin", flag_descriptions::kEdgeToEdgeBottomChinName,
     flag_descriptions::kEdgeToEdgeBottomChinDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kEdgeToEdgeBottomChin,
                                    kEdgeToEdgeBottomChinVariations,
                                    "EdgeToEdgeBottomChin")},
    {"edge-to-edge-everywhere", flag_descriptions::kEdgeToEdgeEverywhereName,
     flag_descriptions::kEdgeToEdgeEverywhereDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kEdgeToEdgeEverywhere,
                                    kEdgeToEdgeEverywhereVariations,
                                    "EdgeToEdgeEverywhere")},
    {"edge-to-edge-safe-area-constraint",
     flag_descriptions::kEdgeToEdgeSafeAreaConstraintName,
     flag_descriptions::kEdgeToEdgeSafeAreaConstraintDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kEdgeToEdgeSafeAreaConstraint,
         kEdgeToEdgeSafeAreaConstraintVariations,
         "EdgeToEdgeSafeAreaConstraint")},
    {"edge-to-edge-tablet", flag_descriptions::kEdgeToEdgeTabletName,
     flag_descriptions::kEdgeToEdgeTabletDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kEdgeToEdgeTablet)},
    {"edge-to-edge-web-opt-in", flag_descriptions::kEdgeToEdgeWebOptInName,
     flag_descriptions::kEdgeToEdgeWebOptInDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kEdgeToEdgeWebOptIn)},
    {"dynamic-safe-area-insets", flag_descriptions::kDynamicSafeAreaInsetsName,
     flag_descriptions::kDynamicSafeAreaInsetsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kDynamicSafeAreaInsets)},
    {"dynamic-safe-area-insets-on-scroll",
     flag_descriptions::kDynamicSafeAreaInsetsOnScrollName,
     flag_descriptions::kDynamicSafeAreaInsetsOnScrollDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kDynamicSafeAreaInsetsOnScroll)},
    {"dynamic-safe-area-insets-supported-by-cc",
     flag_descriptions::kDynamicSafeAreaInsetsSupportedByCCName,
     flag_descriptions::kDynamicSafeAreaInsetsSupportedByCCDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(features::kDynamicSafeAreaInsetsSupportedByCC)},
    {"css-safe-area-max-inset", flag_descriptions::kCSSSafeAreaMaxInsetName,
     flag_descriptions::kCSSSafeAreaMaxInsetDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kCSSSafeAreaMaxInset)},
    {"bottom-browser-controls-refactor",
     flag_descriptions::kBottomBrowserControlsRefactorName,
     flag_descriptions::kBottomBrowserControlsRefactorDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kBottomBrowserControlsRefactor,
         kBottomBrowserControlsRefactorVariations,
         "BottomBrowserControlsRefactor")},

    // Android floating snackbar
    {"floating-snackbar", flag_descriptions::kFloatingSnackbarName,
     flag_descriptions::kFloatingSnackbarDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kFloatingSnackbar)},

    // Android nav bar color animation
    {"nav-bar-color-animation", flag_descriptions::kNavBarColorAnimationName,
     flag_descriptions::kNavBarColorAnimationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNavBarColorAnimation)},

    // Tab closure methods refactor.
    {"tab-closure-method-refactor",
     flag_descriptions::kTabClosureMethodRefactorName,
     flag_descriptions::kTabClosureMethodRefactorDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabClosureMethodRefactor)},

    // Grid tab switcher update.
    {"grid-tab-switcher-update", flag_descriptions::kGridTabSwitcherUpdateName,
     flag_descriptions::kGridTabSwitcherUpdateDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kGridTabSwitcherUpdate)},

    // Predictive back gesture
    {"allow-tab-closing-upon-minimization",
     flag_descriptions::kAllowTabClosingUponMinimizationName,
     flag_descriptions::kAllowTabClosingUponMinimizationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAllowTabClosingUponMinimization)},

    // Pinned tabs.
    {"android-pinned-tabs", flag_descriptions::kAndroidPinnedTabsName,
     flag_descriptions::kAndroidPinnedTabsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidPinnedTabs)},

    {"tab-collection-android", flag_descriptions::kTabCollectionAndroidName,
     flag_descriptions::kTabCollectionAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabCollectionAndroid)},

    {"toolbar-phone-animation-refactor",
     flag_descriptions::kToolbarPhoneAnimationRefactorName,
     flag_descriptions::kToolbarPhoneAnimationRefactorDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kToolbarPhoneAnimationRefactor)},

#endif  // BUILDFLAG(IS_ANDROID)
    {"disallow-doc-written-script-loads",
     flag_descriptions::kDisallowDocWrittenScriptsUiName,
     flag_descriptions::kDisallowDocWrittenScriptsUiDescription, kOsAll,
     // NOTE: if we want to add additional experiment entries for other
     // features controlled by kBlinkSettings, we'll need to add logic to
     // merge the flag values.
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         blink::switches::kBlinkSettings,
         "disallowFetchForDocWrittenScriptsInMainFrame=true",
         blink::switches::kBlinkSettings,
         "disallowFetchForDocWrittenScriptsInMainFrame=false")},
#if BUILDFLAG(IS_WIN)
    {"use-winrt-midi-api", flag_descriptions::kUseWinrtMidiApiName,
     flag_descriptions::kUseWinrtMidiApiDescription, kOsWin,
     FEATURE_VALUE_TYPE(midi::features::kMidiManagerWinrt)},
    {"webrtc-allow-wgc-screen-capturer",
     flag_descriptions::kWebRtcAllowWgcScreenCapturerName,
     flag_descriptions::kWebRtcAllowWgcScreenCapturerDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kWebRtcAllowWgcScreenCapturer)},
    {"webrtc-allow-wgc-window-capturer",
     flag_descriptions::kWebRtcAllowWgcWindowCapturerName,
     flag_descriptions::kWebRtcAllowWgcWindowCapturerDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kWebRtcAllowWgcWindowCapturer)},
    {"webrtc-wgc-require-border",
     flag_descriptions::kWebRtcWgcRequireBorderName,
     flag_descriptions::kWebRtcWgcRequireBorderDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kWebRtcWgcRequireBorder)},
#endif  // BUILDFLAG(IS_WIN)
#if defined(TOOLKIT_VIEWS) || BUILDFLAG(IS_ANDROID)
    {"enable-autofill-credit-card-upload",
     flag_descriptions::kAutofillCreditCardUploadName,
     flag_descriptions::kAutofillCreditCardUploadDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillUpstream)},
#endif  // defined(TOOLKIT_VIEWS) || BUILDFLAG(IS_ANDROID)
    {"force-ui-direction", flag_descriptions::kForceUiDirectionName,
     flag_descriptions::kForceUiDirectionDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceUIDirectionChoices)},
    {"force-text-direction", flag_descriptions::kForceTextDirectionName,
     flag_descriptions::kForceTextDirectionDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceTextDirectionChoices)},
#if BUILDFLAG(IS_ANDROID)
    {"force-update-menu-type", flag_descriptions::kUpdateMenuTypeName,
     flag_descriptions::kUpdateMenuTypeDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kForceUpdateMenuTypeChoices)},
    {"update-menu-item-custom-summary",
     flag_descriptions::kUpdateMenuItemCustomSummaryName,
     flag_descriptions::kUpdateMenuItemCustomSummaryDescription, kOsAndroid,
     SINGLE_VALUE_TYPE_AND_VALUE(
         switches::kForceShowUpdateMenuItemCustomSummary,
         "Custom Summary")},
    {"force-show-update-menu-badge", flag_descriptions::kUpdateMenuBadgeName,
     flag_descriptions::kUpdateMenuBadgeDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kForceShowUpdateMenuBadge)},
    {"set-market-url-for-testing",
     flag_descriptions::kSetMarketUrlForTestingName,
     flag_descriptions::kSetMarketUrlForTestingDescription, kOsAndroid,
     SINGLE_VALUE_TYPE_AND_VALUE(switches::kMarketUrlForTesting,
                                 "https://play.google.com/store/apps/"
                                 "details?id=com.android.chrome")},
    {"omaha-min-sdk-version-android",
     flag_descriptions::kOmahaMinSdkVersionAndroidName,
     flag_descriptions::kOmahaMinSdkVersionAndroidDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kOmahaMinSdkVersionAndroid,
                                    kOmahaMinSdkVersionAndroidVariations,
                                    "OmahaMinSdkVersionAndroidStudy")},
#endif  // BUILDFLAG(IS_ANDROID)
    {"enable-tls13-early-data", flag_descriptions::kEnableTLS13EarlyDataName,
     flag_descriptions::kEnableTLS13EarlyDataDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kEnableTLS13EarlyData)},
#if BUILDFLAG(IS_ANDROID)
    {"feed-loading-placeholder", flag_descriptions::kFeedLoadingPlaceholderName,
     flag_descriptions::kFeedLoadingPlaceholderDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kFeedLoadingPlaceholder)},
    {"feed-signed-out-view-demotion",
     flag_descriptions::kFeedSignedOutViewDemotionName,
     flag_descriptions::kFeedSignedOutViewDemotionDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kFeedSignedOutViewDemotion)},
    {"web-feed-awareness", flag_descriptions::kWebFeedAwarenessName,
     flag_descriptions::kWebFeedAwarenessDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(feed::kWebFeedAwareness,
                                    kWebFeedAwarenessVariations,
                                    "WebFeedAwareness")},
    {"web-feed-onboarding", flag_descriptions::kWebFeedOnboardingName,
     flag_descriptions::kWebFeedOnboardingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kWebFeedOnboarding)},
    {"web-feed-sort", flag_descriptions::kWebFeedSortName,
     flag_descriptions::kWebFeedSortDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kWebFeedSort)},
    {"xsurface-metrics-reporting",
     flag_descriptions::kXsurfaceMetricsReportingName,
     flag_descriptions::kXsurfaceMetricsReportingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kXsurfaceMetricsReporting)},
    {"feed-containment", flag_descriptions::kFeedContainmentName,
     flag_descriptions::kFeedContainmentDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kFeedContainment)},
    {"feed-discofeed-endpoint", flag_descriptions::kFeedDiscoFeedEndpointName,
     flag_descriptions::kFeedDiscoFeedEndpointDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kDiscoFeedEndpoint)},
    {"feed-follow-ui-update", flag_descriptions::kFeedFollowUiUpdateName,
     flag_descriptions::kFeedFollowUiUpdateDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kFeedFollowUiUpdate)},
    {"refresh-feed-on-start", flag_descriptions::kRefreshFeedOnRestartName,
     flag_descriptions::kRefreshFeedOnRestartDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kRefreshFeedOnRestart)},
    {"feed-header-removal", flag_descriptions::kFeedHeaderRemovalName,
     flag_descriptions::kFeedHeaderRemovalDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(feed::kFeedHeaderRemoval,
                                    kFeedHeaderRemovalVariations,
                                    "FeedHeaderRemoval")},
    {"web-feed-deprecation", flag_descriptions::kWebFeedDeprecationName,
     flag_descriptions::kWebFeedDeprecationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feed::kWebFeedKillSwitch)},
#endif  // BUILDFLAG(IS_ANDROID)
    {"enable-force-dark", flag_descriptions::kAutoWebContentsDarkModeName,
     flag_descriptions::kAutoWebContentsDarkModeDescription, kOsAll,
#if BUILDFLAG(IS_CHROMEOS)
     // TODO(crbug.com/40651782): Investigate crash reports and
     // re-enable variations for ChromeOS.
     FEATURE_VALUE_TYPE(blink::features::kForceWebContentsDarkMode)},
#else
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kForceWebContentsDarkMode,
                                    kForceDarkVariations,
                                    "ForceDarkVariations")},
#endif  // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_ANDROID)
    {"enable-accessibility-deprecate-type-announce",
     flag_descriptions::kAccessibilityDeprecateTypeAnnounceName,
     flag_descriptions::kAccessibilityDeprecateTypeAnnounceDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAccessibilityDeprecateTypeAnnounce)},
    {"enable-accessibility-include-long-click-action",
     flag_descriptions::kAccessibilityIncludeLongClickActionName,
     flag_descriptions::kAccessibilityIncludeLongClickActionDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAccessibilityIncludeLongClickAction)},
    {"enable-accessibility-populate-supplemental-description-api",
     flag_descriptions::kAccessibilityPopulateSupplementalDescriptionApiName,
     flag_descriptions::
         kAccessibilityPopulateSupplementalDescriptionApiDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         features::kAccessibilityPopulateSupplementalDescriptionApi)},
    {"enable-accessibility-text-formatting",
     flag_descriptions::kAccessibilityTextFormattingName,
     flag_descriptions::kAccessibilityTextFormattingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAccessibilityTextFormatting)},
    {"enable-accessibility-unified-snapshots",
     flag_descriptions::kAccessibilityUnifiedSnapshotsName,
     flag_descriptions::kAccessibilityUnifiedSnapshotsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAccessibilityUnifiedSnapshots)},
    {"enable-accessibility-manage-broadcast-recevier-on-background",
     flag_descriptions::kAccessibilityManageBroadcastReceiverOnBackgroundName,
     flag_descriptions::
         kAccessibilityManageBroadcastReceiverOnBackgroundDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         features::kAccessibilityManageBroadcastReceiverOnBackground)},
    {"enable-smart-zoom", flag_descriptions::kSmartZoomName,
     flag_descriptions::kSmartZoomDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSmartZoom)},
#endif  // BUILDFLAG(IS_ANDROID)
    {"enable-experimental-accessibility-language-detection",
     flag_descriptions::kExperimentalAccessibilityLanguageDetectionName,
     flag_descriptions::kExperimentalAccessibilityLanguageDetectionDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(
         ::switches::kEnableExperimentalAccessibilityLanguageDetection)},
    {"enable-experimental-accessibility-language-detection-dynamic",
     flag_descriptions::kExperimentalAccessibilityLanguageDetectionDynamicName,
     flag_descriptions::
         kExperimentalAccessibilityLanguageDetectionDynamicDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(
         ::switches::kEnableExperimentalAccessibilityLanguageDetectionDynamic)},
    {"enable-aria-element-reflection",
     flag_descriptions::kAriaElementReflectionName,
     flag_descriptions::kAriaElementReflectionDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kEnableAriaElementReflection)},
#if BUILDFLAG(IS_CHROMEOS)
    {"enable-cros-autocorrect-params-tuning",
     flag_descriptions::kAutocorrectParamsTuningName,
     flag_descriptions::kAutocorrectParamsTuningDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAutocorrectParamsTuning)},
    {"enable-cros-autocorrect-by-default",
     flag_descriptions::kAutocorrectByDefaultName,
     flag_descriptions::kAutocorrectByDefaultDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAutocorrectByDefault)},
    {"enable-cros-first-party-vietnamese-input",
     flag_descriptions::kFirstPartyVietnameseInputName,
     flag_descriptions::kFirstPartyVietnameseInputDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFirstPartyVietnameseInput)},
    {"enable-cros-hindi-inscript-layout",
     flag_descriptions::kHindiInscriptLayoutName,
     flag_descriptions::kHindiInscriptLayoutDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHindiInscriptLayout)},
    {"enable-cros-ime-assist-multi-word",
     flag_descriptions::kImeAssistMultiWordName,
     flag_descriptions::kImeAssistMultiWordDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAssistMultiWord)},
    {"enable-cros-ime-fst-decoder-params-update",
     flag_descriptions::kImeFstDecoderParamsUpdateName,
     flag_descriptions::kImeFstDecoderParamsUpdateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeFstDecoderParamsUpdate)},
    {"enable-cros-ime-manifest-v3", flag_descriptions::kImeManifestV3Name,
     flag_descriptions::kImeManifestV3Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeManifestV3)},
    {"enable-cros-ime-system-emoji-picker-gif-support",
     flag_descriptions::kImeSystemEmojiPickerGIFSupportName,
     flag_descriptions::kImeSystemEmojiPickerGIFSupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeSystemEmojiPickerGIFSupport)},
    {"enable-cros-ime-system-emoji-picker-jelly-support",
     flag_descriptions::kImeSystemEmojiPickerJellySupportName,
     flag_descriptions::kImeSystemEmojiPickerJellySupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeSystemEmojiPickerJellySupport)},
    {"enable-cros-ime-system-emoji-picker-mojo-search",
     flag_descriptions::kImeSystemEmojiPickerMojoSearchName,
     flag_descriptions::kImeSystemEmojiPickerMojoSearchDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeSystemEmojiPickerMojoSearch)},
    {"enable-cros-ime-system-emoji-picker-variant-grouping",
     flag_descriptions::kImeSystemEmojiPickerVariantGroupingName,
     flag_descriptions::kImeSystemEmojiPickerVariantGroupingDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeSystemEmojiPickerVariantGrouping)},
    {"enable-cros-ime-us-english-model-update",
     flag_descriptions::kImeUsEnglishModelUpdateName,
     flag_descriptions::kImeUsEnglishModelUpdateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeUsEnglishModelUpdate)},
    {"enable-cros-ime-korean-only-mode-switch-on-right-alt",
     flag_descriptions::kImeKoreanOnlyModeSwitchOnRightAltName,
     flag_descriptions::kImeKoreanOnlyModeSwitchOnRightAltDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeKoreanOnlyModeSwitchOnRightAlt)},
    {"enable-cros-ime-switch-check-connection-status",
     flag_descriptions::kImeSwitchCheckConnectionStatusName,
     flag_descriptions::kImeSwitchCheckConnectionStatusDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kImeSwitchCheckConnectionStatus)},
    {"enable-cros-japanese-os-settings",
     flag_descriptions::kJapaneseOSSettingsName,
     flag_descriptions::kJapaneseOSSettingsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kJapaneseOSSettings)},
    {"enable-cros-system-japanese-physical-typing",
     flag_descriptions::kSystemJapanesePhysicalTypingName,
     flag_descriptions::kSystemJapanesePhysicalTypingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSystemJapanesePhysicalTyping)},
    {"enable-cros-virtual-keyboard-global-emoji-preferences",
     flag_descriptions::kVirtualKeyboardGlobalEmojiPreferencesName,
     flag_descriptions::kVirtualKeyboardGlobalEmojiPreferencesDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVirtualKeyboardGlobalEmojiPreferences)},
    {"enable-accessibility-bounce-keys",
     flag_descriptions::kAccessibilityBounceKeysName,
     flag_descriptions::kAccessibilityBounceKeysDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityBounceKeys)},
    {"enable-accessibility-slow-keys",
     flag_descriptions::kAccessibilitySlowKeysName,
     flag_descriptions::kAccessibilitySlowKeysDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilitySlowKeys)},
    {"enable-experimental-accessibility-dictation-context-checking",
     flag_descriptions::kExperimentalAccessibilityDictationContextCheckingName,
     flag_descriptions::
         kExperimentalAccessibilityDictationContextCheckingDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         features::kExperimentalAccessibilityDictationContextChecking)},
    {"enable-experimental-accessibility-google-tts-high-quality-voices",
     flag_descriptions::
         kExperimentalAccessibilityGoogleTtsHighQualityVoicesName,
     flag_descriptions::
         kExperimentalAccessibilityGoogleTtsHighQualityVoicesDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         features::kExperimentalAccessibilityGoogleTtsHighQualityVoices)},
    {"enable-experimental-accessibility-manifest-v3",
     flag_descriptions::kExperimentalAccessibilityManifestV3Name,
     flag_descriptions::kExperimentalAccessibilityManifestV3Description,
     kOsCrOS,
     SINGLE_VALUE_TYPE(::switches::kEnableExperimentalAccessibilityManifestV3)},
    {"enable-experimental-accessibility-switch-access-text",
     flag_descriptions::kExperimentalAccessibilitySwitchAccessTextName,
     flag_descriptions::kExperimentalAccessibilitySwitchAccessTextDescription,
     kOsCrOS,
     SINGLE_VALUE_TYPE(
         ::switches::kEnableExperimentalAccessibilitySwitchAccessText)},
    {"enable-system-proxy-for-system-services",
     flag_descriptions::kSystemProxyForSystemServicesName,
     flag_descriptions::kSystemProxyForSystemServicesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSystemProxyForSystemServices)},
    {"system-shortcut-behavior", flag_descriptions::kSystemShortcutBehaviorName,
     flag_descriptions::kSystemShortcutBehaviorDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kSystemShortcutBehavior,
                                    kSystemShortcutBehaviorVariations,
                                    "SystemShortcutBehavior")},
#endif  // BUILDFLAG(IS_CHROMEOS)
    {"enable-cros-touch-text-editing-redesign",
     flag_descriptions::kTouchTextEditingRedesignName,
     flag_descriptions::kTouchTextEditingRedesignDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTouchTextEditingRedesign)},
#if BUILDFLAG(IS_MAC)
    {"enable-retry-capture-device-enumeration-on-crash",
     flag_descriptions::kRetryGetVideoCaptureDeviceInfosName,
     flag_descriptions::kRetryGetVideoCaptureDeviceInfosDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kRetryGetVideoCaptureDeviceInfos)},
    {"enable-immersive-fullscreen-toolbar",
     flag_descriptions::kImmersiveFullscreenName,
     flag_descriptions::kImmersiveFullscreenDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kImmersiveFullscreen)},
#endif  // BUILDFLAG(IS_MAC)
    {"enable-web-payments-experimental-features",
     flag_descriptions::kWebPaymentsExperimentalFeaturesName,
     flag_descriptions::kWebPaymentsExperimentalFeaturesDescription, kOsAll,
     FEATURE_VALUE_TYPE(payments::features::kWebPaymentsExperimentalFeatures)},
    {"enable-debug-for-store-billing",
     flag_descriptions::kAppStoreBillingDebugName,
     flag_descriptions::kAppStoreBillingDebugDescription, kOsAll,
     FEATURE_VALUE_TYPE(payments::features::kAppStoreBillingDebug)},
    {"enable-debug-for-secure-payment-confirmation",
     flag_descriptions::kSecurePaymentConfirmationDebugName,
     flag_descriptions::kSecurePaymentConfirmationDebugDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kSecurePaymentConfirmationDebug)},
    {"enable-network-and-issuer-icons-for-secure-payment-confirmation",
     flag_descriptions::kSecurePaymentConfirmationNetworkAndIssuerIconsName,
     flag_descriptions::
         kSecurePaymentConfirmationNetworkAndIssuerIconsDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kSecurePaymentConfirmationNetworkAndIssuerIcons)},
    {"enable-secure-payment-confirmation-browser-bound-key",
     flag_descriptions::kSecurePaymentConfirmationBrowserBoundKeysName,
     flag_descriptions::kSecurePaymentConfirmationBrowserBoundKeysDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         blink::features::kSecurePaymentConfirmationBrowserBoundKeys)},
#if BUILDFLAG(IS_ANDROID)
    {"show-ready-to-pay-debug-info",
     flag_descriptions::kShowReadyToPayDebugInfoName,
     flag_descriptions::kShowReadyToPayDebugInfoDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(payments::android::kShowReadyToPayDebugInfo)},
#endif  // BUILDFLAG(IS_ANDROID)
    {"keyboard-focusable-scrollers",
     flag_descriptions::kKeyboardFocusableScrollersName,
     flag_descriptions::kKeyboardFocusableScrollersDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kKeyboardFocusableScrollers)},
    {"fill-on-account-select", flag_descriptions::kFillOnAccountSelectName,
     flag_descriptions::kFillOnAccountSelectDescription, kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::kFillOnAccountSelect)},
#if BUILDFLAG(IS_CHROMEOS)
    {"arc-aaudio-mmap-low-latency",
     flag_descriptions::kArcAAudioMMAPLowLatencyName,
     flag_descriptions::kArcAAudioMMAPLowLatencyDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootArcVmAAudioMMAPLowLatency")},
    {"arc-custom-tabs-experiment",
     flag_descriptions::kArcCustomTabsExperimentName,
     flag_descriptions::kArcCustomTabsExperimentDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kCustomTabsExperimentFeature)},
    {kArcEnableAttestationFlag, flag_descriptions::kArcEnableAttestationName,
     flag_descriptions::kArcEnableAttestationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kEnableArcAttestation)},
    {kArcEnableVirtioBlkForDataInternalName,
     flag_descriptions::kArcEnableVirtioBlkForDataName,
     flag_descriptions::kArcEnableVirtioBlkForDataDesc, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kEnableVirtioBlkForData)},
    {"arc-extend-intent-anr-timeout",
     flag_descriptions::kArcExtendIntentAnrTimeoutName,
     flag_descriptions::kArcExtendIntentAnrTimeoutDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kExtendIntentAnrTimeout)},
    {"arc-extend-service-anr-timeout",
     flag_descriptions::kArcExtendServiceAnrTimeoutName,
     flag_descriptions::kArcExtendServiceAnrTimeoutDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kExtendServiceAnrTimeout)},
    {"arc-external-storage-access",
     flag_descriptions::kArcExternalStorageAccessName,
     flag_descriptions::kArcExternalStorageAccessDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kExternalStorageAccess)},
    {"arc-friendlier-error-dialog",
     flag_descriptions::kArcFriendlierErrorDialogName,
     flag_descriptions::kArcFriendlierErrorDialogDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kEnableFriendlierErrorDialog)},
    {"arc-native-bridge-toggle", flag_descriptions::kArcNativeBridgeToggleName,
     flag_descriptions::kArcNativeBridgeToggleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kNativeBridgeToggleFeature)},
    {"arc-per-app-language", flag_descriptions::kArcPerAppLanguageName,
     flag_descriptions::kArcPerAppLanguageDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kPerAppLanguage)},
    {"arc-resize-compat", flag_descriptions::kArcResizeCompatName,
     flag_descriptions::kArcResizeCompatDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kResizeCompat)},
    {"arc-rt-vcpu-dual-core", flag_descriptions::kArcRtVcpuDualCoreName,
     flag_descriptions::kArcRtVcpuDualCoreDesc, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kRtVcpuDualCore)},
    {"arc-rt-vcpu-quad-core", flag_descriptions::kArcRtVcpuQuadCoreName,
     flag_descriptions::kArcRtVcpuQuadCoreDesc, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kRtVcpuQuadCore)},
    {"arc-switch-to-keymint-daemon",
     flag_descriptions::kArcSwitchToKeyMintDaemonName,
     flag_descriptions::kArcSwitchToKeyMintDaemonDesc, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootArcSwitchToKeyMintDaemon")},
    {"arc-switch-to-keymint-on-t-override",
     flag_descriptions::kArcSwitchToKeyMintOnTOverrideName,
     flag_descriptions::kArcSwitchToKeyMintOnTOverrideDesc, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kSwitchToKeyMintOnTOverride)},
    {"arc-sync-install-priority",
     flag_descriptions::kArcSyncInstallPriorityName,
     flag_descriptions::kArcSyncInstallPriorityDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kSyncInstallPriority)},
    {"arc-unthrottle-on-active-audio-v2",
     flag_descriptions::kArcUnthrottleOnActiveAudioV2Name,
     flag_descriptions::kArcUnthrottleOnActiveAudioV2Description, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kUnthrottleOnActiveAudioV2)},
    {"arc-vmm-swap-keyboard-shortcut",
     flag_descriptions::kArcVmmSwapKBShortcutName,
     flag_descriptions::kArcVmmSwapKBShortcutDesc, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kVmmSwapKeyboardShortcut)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS)
    {ui_devtools::switches::kEnableUiDevTools,
     flag_descriptions::kUiDevToolsName,
     flag_descriptions::kUiDevToolsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(ui_devtools::switches::kEnableUiDevTools)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"enable-autofill-virtual-view-structure",
     flag_descriptions::kAutofillVirtualViewStructureAndroidName,
     flag_descriptions::kAutofillVirtualViewStructureAndroidDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::kAutofillVirtualViewStructureAndroid,
         kAutofillVirtualViewStructureVariation,
         "Skip AutofillService Check")},

    {"suppress-autofill-via-accessibility",
     flag_descriptions::kAutofillDeprecateAccessibilityApiName,
     flag_descriptions::kAutofillDeprecateAccessibilityApiDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAndroidAutofillDeprecateAccessibilityApi)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-touchscreen-calibration",
     flag_descriptions::kTouchscreenCalibrationName,
     flag_descriptions::kTouchscreenCalibrationDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(ash::switches::kEnableTouchCalibrationSetting)},
    {"enable-touchscreen-mapping", flag_descriptions::kTouchscreenMappingName,
     flag_descriptions::kTouchscreenMappingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableTouchscreenMappingExperience)},
    {"force-control-face-ae", flag_descriptions::kForceControlFaceAeName,
     flag_descriptions::kForceControlFaceAeDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kForceControlFaceAeChoices)},
    {"auto-framing-override", flag_descriptions::kAutoFramingOverrideName,
     flag_descriptions::kAutoFramingOverrideDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kAutoFramingOverrideChoices)},
    {"face-retouch-override", flag_descriptions::kFaceRetouchOverrideName,
     flag_descriptions::kFaceRetouchOverrideDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kFaceRetouchOverrideChoices)},
    {"crostini-gpu-support", flag_descriptions::kCrostiniGpuSupportName,
     flag_descriptions::kCrostiniGpuSupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrostiniGpuSupport)},
    {"file-transfer-enterprise-connector",
     flag_descriptions::kFileTransferEnterpriseConnectorName,
     flag_descriptions::kFileTransferEnterpriseConnectorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kFileTransferEnterpriseConnector)},
    {"file-transfer-enterprise-connector-ui",
     flag_descriptions::kFileTransferEnterpriseConnectorUIName,
     flag_descriptions::kFileTransferEnterpriseConnectorUIDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kFileTransferEnterpriseConnectorUI)},
    {"files-conflict-dialog", flag_descriptions::kFilesConflictDialogName,
     flag_descriptions::kFilesConflictDialogDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFilesConflictDialog)},
    {"files-local-image-search", flag_descriptions::kFilesLocalImageSearchName,
     flag_descriptions::kFilesLocalImageSearchDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFilesLocalImageSearch)},
    {"files-materialized-views", flag_descriptions::kFilesMaterializedViewsName,
     flag_descriptions::kFilesMaterializedViewsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFilesMaterializedViews)},
    {"files-single-partition-format",
     flag_descriptions::kFilesSinglePartitionFormatName,
     flag_descriptions::kFilesSinglePartitionFormatDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFilesSinglePartitionFormat)},
    {"files-trash-auto-cleanup", flag_descriptions::kFilesTrashAutoCleanupName,
     flag_descriptions::kFilesTrashAutoCleanupDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFilesTrashDrive)},
    {"files-trash-drive", flag_descriptions::kFilesTrashDriveName,
     flag_descriptions::kFilesTrashDriveDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFilesTrashDrive)},
    {"file-system-provider-cloud-file-system",
     flag_descriptions::kFileSystemProviderCloudFileSystemName,
     flag_descriptions::kFileSystemProviderCloudFileSystemDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(
         chromeos::features::kFileSystemProviderCloudFileSystem)},
    {"file-system-provider-content-cache",
     flag_descriptions::kFileSystemProviderContentCacheName,
     flag_descriptions::kFileSystemProviderContentCacheDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kFileSystemProviderContentCache)},
    {"fuse-box-debug", flag_descriptions::kFuseBoxDebugName,
     flag_descriptions::kFuseBoxDebugDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFuseBoxDebug)},
    {"spectre-v2-mitigation", flag_descriptions::kSpectreVariant2MitigationName,
     flag_descriptions::kSpectreVariant2MitigationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(sandbox::policy::features::kSpectreVariant2Mitigation)},
    {"upload-office-to-cloud", flag_descriptions::kUploadOfficeToCloudName,
     flag_descriptions::kUploadOfficeToCloudName, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kUploadOfficeToCloud)},
    {"eap-gtc-wifi-authentication",
     flag_descriptions::kEapGtcWifiAuthenticationName,
     flag_descriptions::kEapGtcWifiAuthenticationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEapGtcWifiAuthentication)},
    {"eche-swa", flag_descriptions::kEcheSWAName,
     flag_descriptions::kEcheSWADescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheSWA)},
    {"eche-launcher", flag_descriptions::kEcheLauncherName,
     flag_descriptions::kEcheLauncherDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheLauncher)},
    {"eche-launcher-app-icon-in-more-apps-button",
     flag_descriptions::kEcheLauncherIconsInMoreAppsButtonName,
     flag_descriptions::kEcheLauncherIconsInMoreAppsButtonDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheLauncherIconsInMoreAppsButton)},
    {"eche-launcher-list-view", flag_descriptions::kEcheLauncherListViewName,
     flag_descriptions::kEcheLauncherListViewDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheLauncherListView)},
    {"eche-swa-check-android-network-info",
     flag_descriptions::kEcheSWACheckAndroidNetworkInfoName,
     flag_descriptions::kEcheSWACheckAndroidNetworkInfoDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheSWACheckAndroidNetworkInfo)},
    {"eche-swa-debug-mode", flag_descriptions::kEcheSWADebugModeName,
     flag_descriptions::kEcheSWADebugModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheSWADebugMode)},
    {"eche-swa-disable-stun-server",
     flag_descriptions::kEcheSWADisableStunServerName,
     flag_descriptions::kEcheSWADisableStunServerDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheSWADisableStunServer)},
    {"eche-swa-measure-latency", flag_descriptions::kEcheSWAMeasureLatencyName,
     flag_descriptions::kEcheSWAMeasureLatencyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheSWAMeasureLatency)},
    {"eche-swa-send-start-signaling",
     flag_descriptions::kEcheSWASendStartSignalingName,
     flag_descriptions::kEcheSWASendStartSignalingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEcheSWASendStartSignaling)},
    {"print-preview-cros-app", flag_descriptions::kPrintPreviewCrosAppName,
     flag_descriptions::kPrintPreviewCrosAppDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPrintPreviewCrosApp)},
    {kGlanceablesTimeManagementClassroomStudentViewInternalName,
     flag_descriptions::kGlanceablesTimeManagementClassroomStudentViewName,
     flag_descriptions::
         kGlanceablesTimeManagementClassroomStudentViewDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kGlanceablesTimeManagementClassroomStudentView)},
    {kGlanceablesTimeManagementTasksViewInternalName,
     flag_descriptions::kGlanceablesTimeManagementTasksViewName,
     flag_descriptions::kGlanceablesTimeManagementTasksViewDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGlanceablesTimeManagementTasksView)},
    {"vc-dlc-ui", flag_descriptions::kVcDlcUiName,
     flag_descriptions::kVcDlcUiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVcDlcUi)},
    {"vc-studio-look", flag_descriptions::kVcStudioLookName,
     flag_descriptions::kVcStudioLookDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVcStudioLook)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"android-app-integration", flag_descriptions::kAndroidAppIntegrationName,
     flag_descriptions::kAndroidAppIntegrationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidAppIntegration)},

    {"android-app-integration-module",
     flag_descriptions::kAndroidAppIntegrationModuleName,
     flag_descriptions::kAndroidAppIntegrationModuleDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         segmentation_platform::features::kAndroidAppIntegrationModule,
         kAndroidAppIntegrationModuleVariations,
         "AndroidAppIntegrationModule")},

    {"android-app-integration-multi-data-source",
     flag_descriptions::kAndroidAppIntegrationMultiDataSourceName,
     flag_descriptions::kAndroidAppIntegrationMultiDataSourceDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kAndroidAppIntegrationMultiDataSource,
         kAndroidAppIntegrationMultiDataSourceVariations,
         "AndroidAppIntegrationMultiDataSource")},

    {"android-app-integration-v2",
     flag_descriptions::kAndroidAppIntegrationV2Name,
     flag_descriptions::kAndroidAppIntegrationV2Description, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidAppIntegrationV2)},

    {"new-tab-page-customization",
     flag_descriptions::kNewTabPageCustomizationName,
     flag_descriptions::kNewTabPageCustomizationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNewTabPageCustomization)},

    {"new-tab-page-customization-v2",
     flag_descriptions::kNewTabPageCustomizationV2Name,
     flag_descriptions::kNewTabPageCustomizationV2Description, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNewTabPageCustomizationV2)},

    {"android-composeplate", flag_descriptions::kAndroidComposeplateName,
     flag_descriptions::kAndroidComposeplateDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kAndroidComposeplate,
                                    kAndroidComposeplateVariations,
                                    "AndroidComposeplate")},

    {"new-tab-page-customization-for-mvt",
     flag_descriptions::kNewTabPageCustomizationForMvtName,
     flag_descriptions::kNewTabPageCustomizationForMvtDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNewTabPageCustomizationForMvt)},

    {"new-tab-page-customization-toolbar-button",
     flag_descriptions::kNewTabPageCustomizationToolbarButtonName,
     flag_descriptions::kNewTabPageCustomizationToolbarButtonDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kNewTabPageCustomizationToolbarButton)},

    {"android-app-integration-with-favicon",
     flag_descriptions::kAndroidAppIntegrationWithFaviconName,
     flag_descriptions::kAndroidAppIntegrationWithFaviconDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kAndroidAppIntegrationWithFavicon,
         kAndroidAppIntegrationWithFaviconVariations,
         "AndroidAppIntegrationWithFavicon")},

    {"android-bottom-toolbar", flag_descriptions::kAndroidBottomToolbarName,
     flag_descriptions::kAndroidBottomToolbarDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kAndroidBottomToolbar,
                                    kAndroidBottomToolbarVariations,
                                    "AndroidBottomToolbar")},

    {"auxiliary-search-donation",
     flag_descriptions::kAuxiliarySearchDonationName,
     flag_descriptions::kAuxiliarySearchDonationDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kAuxiliarySearchDonation,
                                    kAuxiliarySearchDonationVariations,
                                    "AuxiliarySearchDonation")},

    {"disable-instance-limit", flag_descriptions::kDisableInstanceLimitName,
     flag_descriptions::kDisableInstanceLimitDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kDisableInstanceLimit)},

    {"clear-instance-info-when-closed-intentionally",
     flag_descriptions::kClearInstanceInfoWhenClosedIntentionallyName,
     flag_descriptions::kClearInstanceInfoWhenClosedIntentionallyDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kClearInstanceInfoWhenClosedIntentionally)},

    {"change-unfocused-priority",
     flag_descriptions::kChangeUnfocusedPriorityName,
     flag_descriptions::kChangeUnfocusedPriorityDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kChangeUnfocusedPriority)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"most-visited-tiles-new-scoring",
     flag_descriptions::kMostVisitedTilesNewScoringName,
     flag_descriptions::kMostVisitedTilesNewScoringDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history::kMostVisitedTilesNewScoring,
                                    kMostVisitedTilesNewScoringVariations,
                                    "MostVisitedTilesNewScoring")},

    {"most-visited-tiles-visual-deduplication",
     flag_descriptions::kMostVisitedTilesVisualDeduplicationName,
     flag_descriptions::kMostVisitedTilesVisualDeduplicationDescription, kOsAll,
     FEATURE_VALUE_TYPE(history::kMostVisitedTilesVisualDeduplication)},

    {"omnibox-local-history-zero-suggest-beyond-ntp",
     flag_descriptions::kOmniboxLocalHistoryZeroSuggestBeyondNTPName,
     flag_descriptions::kOmniboxLocalHistoryZeroSuggestBeyondNTPDescription,
     kOsAll, FEATURE_VALUE_TYPE(omnibox::kLocalHistoryZeroSuggestBeyondNTP)},

    {"omnibox-suggestion-answer-migration",
     flag_descriptions::kOmniboxSuggestionAnswerMigrationName,
     flag_descriptions::kOmniboxSuggestionAnswerMigrationDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::SuggestionAnswerMigration::
                            kOmniboxSuggestionAnswerMigration)},

    {"omnibox-zero-suggest-prefetch-debouncing",
     flag_descriptions::kOmniboxZeroSuggestPrefetchDebouncingName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchDebouncingDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kZeroSuggestPrefetchDebouncing,
         kOmniboxZeroSuggestPrefetchDebouncingVariations,
         "OmniboxZeroSuggestPrefetchDebouncing")},

    {"omnibox-zero-suggest-prefetching",
     flag_descriptions::kOmniboxZeroSuggestPrefetchingName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestPrefetching)},

    {"omnibox-zero-suggest-prefetching-on-srp",
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnSRPName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnSRPDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestPrefetchingOnSRP)},

    {"omnibox-zero-suggest-prefetching-on-web",
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnWebName,
     flag_descriptions::kOmniboxZeroSuggestPrefetchingOnWebDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestPrefetchingOnWeb)},

    {"omnibox-zero-suggest-in-memory-caching",
     flag_descriptions::kOmniboxZeroSuggestInMemoryCachingName,
     flag_descriptions::kOmniboxZeroSuggestInMemoryCachingDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestInMemoryCaching)},

    {"omnibox-ml-log-url-scoring-signals",
     flag_descriptions::kOmniboxMlLogUrlScoringSignalsName,
     flag_descriptions::kOmniboxMlLogUrlScoringSignalsDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kLogUrlScoringSignals)},
    {"omnibox-ml-url-piecewise-mapped-search-blending",
     flag_descriptions::kOmniboxMlUrlPiecewiseMappedSearchBlendingName,
     flag_descriptions::kOmniboxMlUrlPiecewiseMappedSearchBlendingDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kMlUrlPiecewiseMappedSearchBlending,
         kMlUrlPiecewiseMappedSearchBlendingVariations,
         "MlUrlPiecewiseMappedSearchBlending")},
    {"omnibox-ml-url-score-caching",
     flag_descriptions::kOmniboxMlUrlScoreCachingName,
     flag_descriptions::kOmniboxMlUrlScoreCachingDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kMlUrlScoreCaching)},
    {"omnibox-ml-url-scoring", flag_descriptions::kOmniboxMlUrlScoringName,
     flag_descriptions::kOmniboxMlUrlScoringDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kMlUrlScoring,
                                    kOmniboxMlUrlScoringVariations,
                                    "MlUrlScoring")},
    {"omnibox-ml-url-search-blending",
     flag_descriptions::kOmniboxMlUrlSearchBlendingName,
     flag_descriptions::kOmniboxMlUrlSearchBlendingDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kMlUrlSearchBlending,
                                    kMlUrlSearchBlendingVariations,
                                    "MlUrlScoring")},
    {"omnibox-ml-url-scoring-model",
     flag_descriptions::kOmniboxMlUrlScoringModelName,
     flag_descriptions::kOmniboxMlUrlScoringModelDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kUrlScoringModel,
                                    kUrlScoringModelVariations,
                                    "MlUrlScoring")},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"contextual-search-box-uses-contextual-search-provider",
     flag_descriptions::kContextualSearchBoxUsesContextualSearchProviderName,
     flag_descriptions::
         kContextualSearchBoxUsesContextualSearchProviderDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ContextualSearch::
                            kContextualSearchBoxUsesContextualSearchProvider)},

    {"contextual-search-open-lens-action-uses-thumbnail",
     flag_descriptions::kContextualSearchOpenLensActionUsesThumbnailName,
     flag_descriptions::kContextualSearchOpenLensActionUsesThumbnailDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ContextualSearch::
                            kContextualSearchOpenLensActionUsesThumbnail)},

    {"contextual-suggestions-ablate-others-when-present",
     flag_descriptions::kContextualSuggestionsAblateOthersWhenPresentName,
     flag_descriptions::
         kContextualSuggestionsAblateOthersWhenPresentDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::ContextualSearch::
             kContextualSuggestionsAblateOthersWhenPresent,
         kContextualSuggestionsAblateOthersWhenPresentVariations,
         "ContextualSuggestionsAblateOthersWhenPresent")},

    {"omnibox-contextual-search-on-focus-suggestions",
     flag_descriptions::kOmniboxContextualSearchOnFocusSuggestionsName,
     flag_descriptions::kOmniboxContextualSearchOnFocusSuggestionsDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::ContextualSearch::
             kOmniboxContextualSearchOnFocusSuggestions,
         kOmniboxContextualSearchOnFocusSuggestionsVariations,
         "OmniboxContextualSearchOnFocusSuggestions")},

    {"omnibox-contextual-suggestions",
     flag_descriptions::kOmniboxContextualSuggestionsName,
     flag_descriptions::kOmniboxContextualSuggestionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ContextualSearch::
                            kOmniboxContextualSuggestions)},

    {"lens-overlay-omnibox-entry-point",
     flag_descriptions::kLensOverlayOmniboxEntryPointName,
     flag_descriptions::kLensOverlayOmniboxEntryPointDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayOmniboxEntryPoint)},

    {"omnibox-toolbelt", flag_descriptions::kOmniboxToolbeltName,
     flag_descriptions::kOmniboxToolbeltDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::Toolbelt::kOmniboxToolbelt,
         kOmniboxToolbeltVariations,
         "OmniboxToolbelt")},

    {"omnibox-domain-suggestions",
     flag_descriptions::kOmniboxDomainSuggestionsName,
     flag_descriptions::kOmniboxDomainSuggestionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kDomainSuggestions)},

    {"omnibox-drive-suggestions-no-sync-requirement",
     flag_descriptions::kOmniboxDriveSuggestionsNoSyncRequirementName,
     flag_descriptions::kOmniboxDriveSuggestionsNoSyncRequirementDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kDocumentProviderNoSyncRequirement)},
    {"omnibox-force-allowed-to-be-default",
     flag_descriptions::kOmniboxForceAllowedToBeDefaultName,
     flag_descriptions::kOmniboxForceAllowedToBeDefaultDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::ForceAllowedToBeDefault::
                            kForceAllowedToBeDefault)},
    {"omnibox-rich-autocompletion-promising",
     flag_descriptions::kOmniboxRichAutocompletionPromisingName,
     flag_descriptions::kOmniboxRichAutocompletionPromisingDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kRichAutocompletion,
         kOmniboxRichAutocompletionPromisingVariations,
         "OmniboxBundledExperimentV1")},
    {"omnibox-starter-pack-expansion",
     flag_descriptions::kOmniboxStarterPackExpansionName,
     flag_descriptions::kOmniboxStarterPackExpansionDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kStarterPackExpansion,
                                    kOmniboxStarterPackExpansionVariations,
                                    "StarterPackExpansion")},

    {"omnibox-starter-pack-iph", flag_descriptions::kOmniboxStarterPackIPHName,
     flag_descriptions::kOmniboxStarterPackIPHDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kStarterPackIPH)},

    {"omnibox-focus-triggers-web-and-srp-zero-suggest",
     flag_descriptions::kOmniboxFocusTriggersWebAndSRPZeroSuggestName,
     flag_descriptions::kOmniboxFocusTriggersWebAndSRPZeroSuggestDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kFocusTriggersWebAndSRPZeroSuggest)},

    {"omnibox-show-popup-on-mouse-released",
     flag_descriptions::kOmniboxShowPopupOnMouseReleasedName,
     flag_descriptions::kOmniboxShowPopupOnMouseReleasedDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kShowPopupOnMouseReleased)},

    {"omnibox-hide-suggestion-group-headers",
     flag_descriptions::kOmniboxHideSuggestionGroupHeadersName,
     flag_descriptions::kOmniboxHideSuggestionGroupHeadersDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(omnibox::kHideSuggestionGroupHeaders)},

    {"omnibox-url-suggestions-on-focus",
     flag_descriptions::kOmniboxUrlSuggestionsOnFocus,
     flag_descriptions::kOmniboxUrlSuggestionsOnFocusDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::OmniboxUrlSuggestionsOnFocus::
             kOmniboxUrlSuggestionsOnFocus,
         kOmniboxUrlSuggestionsOnFocusVariations,
         "OmniboxUrlSuggestionsOnFocus")},

    {"omnibox-zps-suggestion-limit",
     flag_descriptions::kOmniboxZpsSuggestionLimit,
     flag_descriptions::kOmniboxZpsSuggestionLimitDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::OmniboxZpsSuggestionLimit::
             kOmniboxZpsSuggestionLimit,
         kOmniboxZpsSuggestionLimitVariations,
         "OmniboxZpsSuggestionLimit")},

    {"omnibox-enterprise-search-aggregator",
     flag_descriptions::kOmniboxSearchAggregatorName,
     flag_descriptions::kOmniboxSearchAggregatorDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::SearchAggregatorProvider::
             kSearchAggregatorProvider,
         kOmniboxSearchAggregatorVariations,
         "SearchAggregatorProvider")},

    {"omnibox-adjust-indentation",
     flag_descriptions::kOmniboxAdjustIndentationName,
     flag_descriptions::kOmniboxAdjustIndentationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         omnibox_feature_configs::AdjustOmniboxIndent::kAdjustOmniboxIndent)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_ANDROID)
    {"animate-suggestions-list-appearance",
     flag_descriptions::kAnimateSuggestionsListAppearanceName,
     flag_descriptions::kAnimateSuggestionsListAppearanceDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kAnimateSuggestionsListAppearance)},

    {"omnibox-answer-actions", flag_descriptions::kOmniboxAnswerActionsName,
     flag_descriptions::kOmniboxAnswerActionsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kOmniboxAnswerActions,
                                    kOmniboxAnswerActionsVariants,
                                    "OmniboxBundledExperimentV1")},

    {"omnibox-asynchronous-view-inflation",
     flag_descriptions::kOmniboxAsyncViewInflationName,
     flag_descriptions::kOmniboxAsyncViewInflationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kOmniboxAsyncViewInflation)},

    {"omnibox-diagnostics", flag_descriptions::kOmniboxDiagnosticsName,
     flag_descriptions::kOmniboxDiagnosticsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kDiagnostics,
                                    kOmniboxDiagnosticsAndroidVaiants,
                                    "Diagnostics")},

    {"omnibox-mobile-parity-update",
     flag_descriptions::kOmniboxMobileParityUpdateName,
     flag_descriptions::kOmniboxMobileParityUpdateDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kOmniboxMobileParityUpdate,
                                    kOmniboxMobileParityVariants,
                                    "OmniboxMobileParityUpdate")},

    {"omnibox-mobile-parity-update-v2",
     flag_descriptions::kOmniboxMobileParityUpdateV2Name,
     flag_descriptions::kOmniboxMobileParityUpdateV2Description, kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kOmniboxMobileParityUpdateV2)},

#endif  // BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_WIN)
    {"omnibox-on-device-head-suggestions",
     flag_descriptions::kOmniboxOnDeviceHeadSuggestionsName,
     flag_descriptions::kOmniboxOnDeviceHeadSuggestionsDescription, kOsWin,
     FEATURE_VALUE_TYPE(omnibox::kOnDeviceHeadProviderNonIncognito)},
    {"omnibox-on-device-head-suggestions-incognito",
     flag_descriptions::kOmniboxOnDeviceHeadSuggestionsIncognitoName,
     flag_descriptions::kOmniboxOnDeviceHeadSuggestionsIncognitoDescription,
     kOsWin, FEATURE_VALUE_TYPE(omnibox::kOnDeviceHeadProviderIncognito)},
#endif  // BUILDFLAG(IS_WIN)

    {"omnibox-on-device-tail-suggestions",
     flag_descriptions::kOmniboxOnDeviceTailSuggestionsName,
     flag_descriptions::kOmniboxOnDeviceTailSuggestionsDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kOnDeviceTailModel)},

    {"omnibox-restore-invisible-focus-only",
     flag_descriptions::kOmniboxRestoreInvisibleFocusOnlyName,
     flag_descriptions::kOmniboxRestoreInvisibleFocusOnlyDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kOmniboxRestoreInvisibleFocusOnly)},

#if BUILDFLAG(IS_CHROMEOS)
    {"scheduler-configuration", flag_descriptions::kSchedulerConfigurationName,
     flag_descriptions::kSchedulerConfigurationDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kSchedulerConfigurationChoices)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"enable-command-line-on-non-rooted-devices",
     flag_descriptions::kEnableCommandLineOnNonRootedName,
     flag_descriptions::kEnableCommandLineOnNoRootedDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCommandLineOnNonRooted)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"force-color-profile", flag_descriptions::kForceColorProfileName,
     flag_descriptions::kForceColorProfileDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceColorProfileChoices)},

    {"forced-colors", flag_descriptions::kForcedColorsName,
     flag_descriptions::kForcedColorsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kForcedColors)},

#if BUILDFLAG(IS_ANDROID)
    {"dynamic-color-gamut", flag_descriptions::kDynamicColorGamutName,
     flag_descriptions::kDynamicColorGamutDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kDynamicColorGamut)},
#endif

    {"hdr-agtm", flag_descriptions::kHdrAgtmName,
     flag_descriptions::kHdrAgtmDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kHdrAgtm)},

    {"memlog", flag_descriptions::kMemlogName,
     flag_descriptions::kMemlogDescription, kOsAll,
     MULTI_VALUE_TYPE(kMemlogModeChoices)},

    {"memlog-sampling-rate", flag_descriptions::kMemlogSamplingRateName,
     flag_descriptions::kMemlogSamplingRateDescription, kOsAll,
     MULTI_VALUE_TYPE(kMemlogSamplingRateChoices)},

    {"memlog-stack-mode", flag_descriptions::kMemlogStackModeName,
     flag_descriptions::kMemlogStackModeDescription, kOsAll,
     MULTI_VALUE_TYPE(kMemlogStackModeChoices)},

    {"omnibox-max-zero-suggest-matches",
     flag_descriptions::kOmniboxMaxZeroSuggestMatchesName,
     flag_descriptions::kOmniboxMaxZeroSuggestMatchesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kMaxZeroSuggestMatches,
                                    kMaxZeroSuggestMatchesVariations,
                                    "OmniboxBundledExperimentV1")},

    {"omnibox-ui-max-autocomplete-matches",
     flag_descriptions::kOmniboxUIMaxAutocompleteMatchesName,
     flag_descriptions::kOmniboxUIMaxAutocompleteMatchesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kUIExperimentMaxAutocompleteMatches,
         kOmniboxUIMaxAutocompleteMatchesVariations,
         "OmniboxBundledExperimentV1")},

    {"omnibox-max-url-matches", flag_descriptions::kOmniboxMaxURLMatchesName,
     flag_descriptions::kOmniboxMaxURLMatchesDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kOmniboxMaxURLMatches,
                                    kOmniboxMaxURLMatchesVariations,
                                    "OmniboxMaxURLMatchesVariations")},

    {"omnibox-mia-zps", flag_descriptions::kOmniboxMiaZps,
     flag_descriptions::kOmniboxMiaZpsDescription, kOsAll,
#if BUILDFLAG(IS_ANDROID)
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox_feature_configs::MiaZPS::kOmniboxMiaZPS,
         kOmniboxMiaZpsVariations,
         "OmniboxMiaZpsVariations")
#else
     FEATURE_VALUE_TYPE(omnibox_feature_configs::MiaZPS::kOmniboxMiaZPS)
#endif
    },

    {"omnibox-dynamic-max-autocomplete",
     flag_descriptions::kOmniboxDynamicMaxAutocompleteName,
     flag_descriptions::kOmniboxDynamicMaxAutocompleteDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kDynamicMaxAutocomplete,
                                    kOmniboxDynamicMaxAutocompleteVariations,
                                    "OmniboxBundledExperimentV1")},

    {"omnibox-grouping-framework-non-zps",
     flag_descriptions::kOmniboxGroupingFrameworkNonZPSName,
     flag_descriptions::kOmniboxGroupingFrameworkDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kGroupingFrameworkForNonZPS)},

    {"omnibox-calc-provider", flag_descriptions::kOmniboxCalcProviderName,
     flag_descriptions::kOmniboxCalcProviderDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox_feature_configs::CalcProvider::kCalcProvider)},

    {"optimization-guide-debug-logs",
     flag_descriptions::kOptimizationGuideDebugLogsName,
     flag_descriptions::kOptimizationGuideDebugLogsDescription, kOsAll,
     SINGLE_VALUE_TYPE(optimization_guide::switches::kDebugLoggingEnabled)},

    {"optimization-guide-model-execution",
     flag_descriptions::kOptimizationGuideModelExecutionName,
     flag_descriptions::kOptimizationGuideModelExecutionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         optimization_guide::features::kOptimizationGuideModelExecution)},

    {"optimization-guide-on-device-model",
     flag_descriptions::kOptimizationGuideOnDeviceModelName,
     flag_descriptions::kOptimizationGuideOnDeviceModelDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         optimization_guide::features::kOnDeviceModelPerformanceParams,
         kOptimizationGuideOnDeviceModelVariations,
         "OptimizationGuideOnDeviceModel")},

    {"text-safety-classifier", flag_descriptions::kTextSafetyClassifierName,
     flag_descriptions::kTextSafetyClassifierDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         optimization_guide::features::kTextSafetyClassifier,
         kTextSafetyClassifierVariations,
         "TextSafetyClassifier")},

    {"organic-repeatable-queries",
     flag_descriptions::kOrganicRepeatableQueriesName,
     flag_descriptions::kOrganicRepeatableQueriesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history::kOrganicRepeatableQueries,
                                    kOrganicRepeatableQueriesVariations,
                                    "OrganicRepeatableQueries")},

    {"omnibox-num-ntp-zps-recent-searches",
     flag_descriptions::kOmniboxNumNtpZpsRecentSearchesName,
     flag_descriptions::kOmniboxNumNtpZpsRecentSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumNtpZpsRecentSearches,
                                    kNumNtpZpsRecentSearches,
                                    "PowerTools")},
    {"omnibox-num-ntp-zps-trending-searches",
     flag_descriptions::kOmniboxNumNtpZpsTrendingSearchesName,
     flag_descriptions::kOmniboxNumNtpZpsTrendingSearchesDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumNtpZpsTrendingSearches,
                                    kNumNtpZpsTrendingSearches,
                                    "PowerTools")},
    {"omnibox-num-web-zps-recent-searches",
     flag_descriptions::kOmniboxNumWebZpsRecentSearchesName,
     flag_descriptions::kOmniboxNumWebZpsRecentSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumWebZpsRecentSearches,
                                    kNumWebZpsRecentSearches,
                                    "PowerTools")},
    {"omnibox-num-web-zps-related-searches",
     flag_descriptions::kOmniboxNumWebZpsRelatedSearchesName,
     flag_descriptions::kOmniboxNumWebZpsRelatedSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumWebZpsRelatedSearches,
                                    kNumWebZpsRelatedSearches,
                                    "PowerTools")},
    {"omnibox-num-web-zps-most-visited-urls",
     flag_descriptions::kOmniboxNumWebZpsMostVisitedUrlsName,
     flag_descriptions::kOmniboxNumWebZpsMostVisitedUrlsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumWebZpsMostVisitedUrls,
                                    kNumWebZpsMostVisitedUrls,
                                    "PowerTools")},
    {"omnibox-num-srp-zps-recent-searches",
     flag_descriptions::kOmniboxNumSrpZpsRecentSearchesName,
     flag_descriptions::kOmniboxNumSrpZpsRecentSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumSrpZpsRecentSearches,
                                    kNumSrpZpsRecentSearches,
                                    "PowerTools")},
    {"omnibox-num-srp-zps-related-searches",
     flag_descriptions::kOmniboxNumSrpZpsRelatedSearchesName,
     flag_descriptions::kOmniboxNumSrpZpsRelatedSearchesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kNumSrpZpsRelatedSearches,
                                    kNumSrpZpsRelatedSearches,
                                    "PowerTools")},
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"history-embeddings", flag_descriptions::kHistoryEmbeddingsName,
     flag_descriptions::kHistoryEmbeddingsDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history_embeddings::kHistoryEmbeddings,
                                    kHistoryEmbeddingsVariations,
                                    "HistoryEmbeddings")},
    {"history-embeddings-answers",
     flag_descriptions::kHistoryEmbeddingsAnswersName,
     flag_descriptions::kHistoryEmbeddingsAnswersDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(history_embeddings::kHistoryEmbeddingsAnswers)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

    {"history-journeys", flag_descriptions::kJourneysName,
     flag_descriptions::kJourneysDescription, kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(history_clusters::internal::kJourneys,
                                    kJourneysVariations,
                                    "HistoryJourneys")},

    {"extract-related-searches-from-prefetched-zps-response",
     flag_descriptions::kExtractRelatedSearchesFromPrefetchedZPSResponseName,
     flag_descriptions::
         kExtractRelatedSearchesFromPrefetchedZPSResponseDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(page_content_annotations::features::
                            kExtractRelatedSearchesFromPrefetchedZPSResponse)},

    {"page-image-service-optimization-guide-salient-images",
     flag_descriptions::kPageImageServiceOptimizationGuideSalientImagesName,
     flag_descriptions::
         kPageImageServiceOptimizationGuideSalientImagesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         page_image_service::kImageServiceOptimizationGuideSalientImages,
         kImageServiceOptimizationGuideSalientImagesVariations,
         "PageImageService")},

    {"page-image-service-suggest-powered-images",
     flag_descriptions::kPageImageServiceSuggestPoweredImagesName,
     flag_descriptions::kPageImageServiceSuggestPoweredImagesDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(page_image_service::kImageServiceSuggestPoweredImages)},

    {"page-content-annotations", flag_descriptions::kPageContentAnnotationsName,
     flag_descriptions::kPageContentAnnotationsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         page_content_annotations::features::kPageContentAnnotations,
         kPageContentAnnotationsVariations,
         "PageContentAnnotations")},

    {"page-content-annotations-persist-salient-image-metadata",
     flag_descriptions::kPageContentAnnotationsPersistSalientImageMetadataName,
     flag_descriptions::
         kPageContentAnnotationsPersistSalientImageMetadataDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         page_content_annotations::features::
             kPageContentAnnotationsPersistSalientImageMetadata)},

    {"page-content-annotations-remote-page-metadata",
     flag_descriptions::kPageContentAnnotationsRemotePageMetadataName,
     flag_descriptions::kPageContentAnnotationsRemotePageMetadataDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         page_content_annotations::features::kRemotePageMetadata,
         kRemotePageMetadataVariations,
         "RemotePageMetadata")},

    {"page-visibility-page-content-annotations",
     flag_descriptions::kPageVisibilityPageContentAnnotationsName,
     flag_descriptions::kPageVisibilityPageContentAnnotationsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(page_content_annotations::features::
                            kPageVisibilityPageContentAnnotations)},

#if BUILDFLAG(IS_CHROMEOS)
    {"language-packs-in-settings",
     flag_descriptions::kLanguagePacksInSettingsName,
     flag_descriptions::kLanguagePacksInSettingsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kLanguagePacksInSettings)},
    {"use-ml-service-for-non-longform-handwriting-on-all-boards",
     flag_descriptions::kUseMlServiceForNonLongformHandwritingOnAllBoardsName,
     flag_descriptions::
         kUseMlServiceForNonLongformHandwritingOnAllBoardsDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kUseMlServiceForNonLongformHandwritingOnAllBoards)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"mbi-mode", flag_descriptions::kMBIModeName,
     flag_descriptions::kMBIModeDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kMBIMode,
                                    kMBIModeVariations,
                                    "MBIMode")},

#if BUILDFLAG(IS_CHROMEOS)
    {"double-tap-to-zoom-in-tablet-mode",
     flag_descriptions::kDoubleTapToZoomInTabletModeName,
     flag_descriptions::kDoubleTapToZoomInTabletModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kDoubleTapToZoomInTabletMode)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {flag_descriptions::kTabGroupSyncServiceDesktopMigrationId,
     flag_descriptions::kTabGroupSyncServiceDesktopMigrationName,
     flag_descriptions::kTabGroupSyncServiceDesktopMigrationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(tab_groups::kTabGroupSyncServiceDesktopMigration)},

#if !BUILDFLAG(IS_ANDROID)
    {flag_descriptions::kScrollableTabStripFlagId,
     flag_descriptions::kScrollableTabStripName,
     flag_descriptions::kScrollableTabStripDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(tabs::kScrollableTabStrip,
                                    kTabScrollingVariations,
                                    "TabScrolling")},
#endif
    {flag_descriptions::kTabScrollingButtonPositionFlagId,
     flag_descriptions::kTabScrollingButtonPositionName,
     flag_descriptions::kTabScrollingButtonPositionDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kTabScrollingButtonPosition,
                                    kTabScrollingButtonPositionVariations,
                                    "TabScrollingButtonPosition")},

#if !BUILDFLAG(IS_ANDROID)
    {flag_descriptions::kScrollableTabStripWithDraggingFlagId,
     flag_descriptions::kScrollableTabStripWithDraggingName,
     flag_descriptions::kScrollableTabStripWithDraggingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(tabs::kScrollableTabStripWithDragging,
                                    kTabScrollingWithDraggingVariations,
                                    "TabScrollingWithDragging")},

    {"tabsearch-toolbar-button",
     flag_descriptions::kLaunchedTabSearchToolbarName,
     flag_descriptions::kLaunchedTabSearchToolbarDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kLaunchedTabSearchToolbarButton)},

    {flag_descriptions::kTabstripComboButtonFlagId,
     flag_descriptions::kTabstripComboButtonName,
     flag_descriptions::kTabstripComboButtonDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kTabstripComboButton,
                                    kTabstripComboButtonVariations,
                                    "TabstripComboButton")},

    {flag_descriptions::kScrollableTabStripOverflowFlagId,
     flag_descriptions::kScrollableTabStripOverflowName,
     flag_descriptions::kScrollableTabStripOverflowDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(tabs::kScrollableTabStripOverflow,
                                    kScrollableTabStripOverflowVariations,
                                    "ScrollableTabStripOverflow")},

    {"split-tabstrip", flag_descriptions::kSplitTabStripName,
     flag_descriptions::kSplitTabStripDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tabs::kSplitTabStrip)},
#endif

#if !BUILDFLAG(IS_ANDROID)
    {flag_descriptions::kSidePanelResizingFlagId,
     flag_descriptions::kSidePanelResizingName,
     flag_descriptions::kSidePanelResizingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSidePanelResizing)},

    {"by-date-history-in-side-panel",
     flag_descriptions::kByDateHistoryInSidePanelName,
     flag_descriptions::kByDateHistoryInSidePanelDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kByDateHistoryInSidePanel)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"enable-share-custom-actions-in-cct",
     flag_descriptions::kShareCustomActionsInCCTName,
     flag_descriptions::kShareCustomActionsInCCTDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kShareCustomActionsInCCT)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"product-specifications",
     commerce::flag_descriptions::kProductSpecificationsName,
     commerce::flag_descriptions::kProductSpecificationsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kProductSpecifications)},

    {"compare-confirmation-toast",
     commerce::flag_descriptions::kCompareConfirmationToastName,
     commerce::flag_descriptions::kCompareConfirmationToastDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(commerce::kCompareConfirmationToast)},

    {"shopping-list", commerce::flag_descriptions::kShoppingListName,
     commerce::flag_descriptions::kShoppingListDescription,
     kOsAndroid | kOsDesktop, FEATURE_VALUE_TYPE(commerce::kShoppingList)},

    {"shopping-alternate-server",
     commerce::flag_descriptions::kShoppingAlternateServerName,
     commerce::flag_descriptions::kShoppingAlternateServerDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kShoppingAlternateServer)},

    {"local-pdp-detection",
     commerce::flag_descriptions::kCommerceLocalPDPDetectionName,
     commerce::flag_descriptions::kCommerceLocalPDPDetectionDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kCommerceLocalPDPDetection)},

    {"price-tracking-subscription-service-locale-key",
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceLocaleKeyName,
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceLocaleKeyDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kPriceTrackingSubscriptionServiceLocaleKey)},

    {"price-tracking-subscription-service-product-version",
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceProductVersionName,
     commerce::flag_descriptions::
         kPriceTrackingSubscriptionServiceProductVersionDescription,
     kOsAndroid | kOsDesktop,
     FEATURE_VALUE_TYPE(
         commerce::kPriceTrackingSubscriptionServiceProductVersion)},

#if BUILDFLAG(IS_ANDROID)
    {"price-change-module", flag_descriptions::kPriceChangeModuleName,
     flag_descriptions::kPriceChangeModuleDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kPriceChangeModule)},

    {"track-by-default-mobile",
     commerce::flag_descriptions::kTrackByDefaultOnMobileName,
     commerce::flag_descriptions::kTrackByDefaultOnMobileDescription,
     kOsAndroid, FEATURE_VALUE_TYPE(commerce::kTrackByDefaultOnMobile)},
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-retail-coupons", flag_descriptions::kRetailCouponsName,
     flag_descriptions::kRetailCouponsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kRetailCoupons)},

    {"ntp-alpha-background-collections",
     flag_descriptions::kNtpAlphaBackgroundCollectionsName,
     flag_descriptions::kNtpAlphaBackgroundCollectionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpAlphaBackgroundCollections)},

    {"ntp-background-image-error-detection",
     flag_descriptions::kNtpBackgroundImageErrorDetectionName,
     flag_descriptions::kNtpBackgroundImageErrorDetectionDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpBackgroundImageErrorDetection)},

    {"ntp-calendar-module", flag_descriptions::kNtpCalendarModuleName,
     flag_descriptions::kNtpCalendarModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpCalendarModule,
                                    kNtpCalendarModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-chrome-cart-module", flag_descriptions::kNtpChromeCartModuleName,
     flag_descriptions::kNtpChromeCartModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpChromeCartModule,
                                    kNtpChromeCartModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-compose-entrypoint",
     flag_descriptions::kNtpSearchboxComposeEntrypointName,
     flag_descriptions::kNtpSearchboxComposeEntrypointDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpSearchboxComposeEntrypoint)},

    {"ntp-composebox", flag_descriptions::kNtpSearchboxComposeboxName,
     flag_descriptions::kNtpSearchboxComposeboxDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpSearchboxComposebox)},

    {"ntp-drive-module", flag_descriptions::kNtpDriveModuleName,
     flag_descriptions::kNtpDriveModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpDriveModule,
                                    kNtpDriveModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-drive-module-no-sync-requirement",
     flag_descriptions::kNtpDriveModuleNoSyncRequirementName,
     flag_descriptions::kNtpDriveModuleNoSyncRequirementDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpDriveModuleNoSyncRequirement)},

    {"ntp-drive-module-segmentation",
     flag_descriptions::kNtpDriveModuleSegmentationName,
     flag_descriptions::kNtpDriveModuleSegmentationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpDriveModuleSegmentation)},

    {"ntp-drive-module-show-six-files",
     flag_descriptions::kNtpDriveModuleShowSixFilesName,
     flag_descriptions::kNtpDriveModuleShowSixFilesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpDriveModuleShowSixFiles)},

#if !defined(OFFICIAL_BUILD)
    {"ntp-dummy-modules", flag_descriptions::kNtpDummyModulesName,
     flag_descriptions::kNtpDummyModulesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpDummyModules)},
#endif

    {"ntp-footer", flag_descriptions::kNtpFooterName,
     flag_descriptions::kNtpFooterDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpFooter)},

    {"ntp-middle-slot-promo-dismissal",
     flag_descriptions::kNtpMiddleSlotPromoDismissalName,
     flag_descriptions::kNtpMiddleSlotPromoDismissalDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpMiddleSlotPromoDismissal,
                                    kNtpMiddleSlotPromoDismissalVariations,
                                    "DesktopNtpModules")},

    {"ntp-mobile-promo", flag_descriptions::kNtpMobilePromoName,
     flag_descriptions::kNtpMobilePromoName, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpMobilePromo)},

    {"force-ntp-mobile-promo", flag_descriptions::kForceNtpMobilePromoName,
     flag_descriptions::kForceNtpMobilePromoName, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kForceNtpMobilePromo)},

    {"ntp-module-sign-in-requirement",
     flag_descriptions::kNtpModuleSignInRequirementName,
     flag_descriptions::kNtpModuleSignInRequirementDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpModuleSignInRequirement)},

    {"ntp-modules-drag-and-drop", flag_descriptions::kNtpModulesDragAndDropName,
     flag_descriptions::kNtpModulesDragAndDropDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpModulesDragAndDrop)},

    {"ntp-most-relevant-tab-resumption-module",
     flag_descriptions::kNtpMostRelevantTabResumptionModuleName,
     flag_descriptions::kNtpMostRelevantTabResumptionModuleDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_features::kNtpMostRelevantTabResumptionModule,
         kNtpMostRelevantTabResumptionModuleVariations,
         "NtpMostRelevantTabResumptionModules")},

    {"ntp-most-relevant-tab-resumption-module-fallback-to-host",
     flag_descriptions::kNtpMostRelevantTabResumptionModuleFallbackToHostName,
     flag_descriptions::
         kNtpMostRelevantTabResumptionModuleFallbackToHostDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         ntp_features::kNtpMostRelevantTabResumptionModuleFallbackToHost)},

    {"ntp-ogb-async-bar-parts",
     flag_descriptions::kNtpOneGoogleBarAsyncBarPartsName,
     flag_descriptions::kNtpOneGoogleBarAsyncBarPartsName, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpOneGoogleBarAsyncBarParts)},

    {"ntp-outlook-calendar-module",
     flag_descriptions::kNtpOutlookCalendarModuleName,
     flag_descriptions::kNtpOutlookCalendarModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpOutlookCalendarModule,
                                    kNtpOutlookCalendarModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-realbox-contextual-and-trending-suggestions",
     flag_descriptions::kNtpRealboxContextualAndTrendingSuggestionsName,
     flag_descriptions::kNtpRealboxContextualAndTrendingSuggestionsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         omnibox_feature_configs::RealboxContextualAndTrendingSuggestions::
             kRealboxContextualAndTrendingSuggestions)},

    {"ntp-realbox-cr23-theming", flag_descriptions::kNtpRealboxCr23ThemingName,
     flag_descriptions::kNtpRealboxCr23ThemingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kRealboxCr23Theming,
                                    kNtpRealboxCr23ThemingVariations,
                                    "NtpRealboxCr23Theming")},

    {"ntp-realbox-match-searchbox-theme",
     flag_descriptions::kNtpRealboxMatchSearchboxThemeName,
     flag_descriptions::kNtpRealboxMatchSearchboxThemeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kRealboxMatchSearchboxTheme)},

    {"ntp-realbox-use-google-g-icon",
     flag_descriptions::kNtpRealboxUseGoogleGIconName,
     flag_descriptions::kNtpRealboxUseGoogleGIconDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kRealboxUseGoogleGIcon)},

    {"ntp-safe-browsing-module", flag_descriptions::kNtpSafeBrowsingModuleName,
     flag_descriptions::kNtpSafeBrowsingModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpSafeBrowsingModule,
                                    kNtpSafeBrowsingModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-sharepoint-module", flag_descriptions::kNtpSharepointModuleName,
     flag_descriptions::kNtpSharepointModuleDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ntp_features::kNtpSharepointModule,
                                    kNtpSharepointModuleVariations,
                                    "DesktopNtpModules")},

    {"ntp-wallpaper-search-button",
     flag_descriptions::kNtpWallpaperSearchButtonName,
     flag_descriptions::kNtpWallpaperSearchButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpWallpaperSearchButton)},

    {"ntp-wallpaper-search-button-animation",
     flag_descriptions::kNtpWallpaperSearchButtonAnimationName,
     flag_descriptions::kNtpWallpaperSearchButtonAnimationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpWallpaperSearchButtonAnimation)},

    {"ntp-microsoft-authentication-module",
     flag_descriptions::kNtpMicrosoftAuthenticationModuleName,
     flag_descriptions::kNtpMicrosoftAuthenticationModuleDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kNtpMicrosoftAuthenticationModule)},

    {"shopping-page-types", commerce::flag_descriptions::kShoppingPageTypesName,
     commerce::flag_descriptions::kShoppingPageTypesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kShoppingPageTypes)},

#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
    {"chrome-wide-echo-cancellation",
     flag_descriptions::kChromeWideEchoCancellationName,
     flag_descriptions::kChromeWideEchoCancellationDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(media::kChromeWideEchoCancellation)},
#endif  // BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)

#if BUILDFLAG(DCHECK_IS_CONFIGURABLE)
    {"dcheck-is-fatal", flag_descriptions::kDcheckIsFatalName,
     flag_descriptions::kDcheckIsFatalDescription, kOsWin,
     FEATURE_VALUE_TYPE(base::kDCheckIsFatalFeature)},
#endif  // BUILDFLAG(DCHECK_IS_CONFIGURABLE)

    {"enable-pixel-canvas-recording",
     flag_descriptions::kEnablePixelCanvasRecordingName,
     flag_descriptions::kEnablePixelCanvasRecordingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kEnablePixelCanvasRecording)},

    {"enable-parallel-downloading", flag_descriptions::kParallelDownloadingName,
     flag_descriptions::kParallelDownloadingDescription, kOsAll,
     FEATURE_VALUE_TYPE(download::features::kParallelDownloading)},

    {"download-notification-service-unified-api",
     flag_descriptions::kDownloadNotificationServiceUnifiedAPIName,
     flag_descriptions::kDownloadNotificationServiceUnifiedAPIDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         download::features::kDownloadNotificationServiceUnifiedAPI)},

    {"tab-hover-card-images", flag_descriptions::kTabHoverCardImagesName,
     flag_descriptions::kTabHoverCardImagesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kTabHoverCardImages)},

#if !BUILDFLAG(IS_ANDROID)
    {flag_descriptions::kTabSearchPositionSettingId,
     flag_descriptions::kTabSearchPositionSettingName,
     flag_descriptions::kTabSearchPositionSettingDescription,
     kOsCrOS | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(tabs::kTabSearchPositionSetting)},
#endif

    {"enable-network-logging-to-file",
     flag_descriptions::kEnableNetworkLoggingToFileName,
     flag_descriptions::kEnableNetworkLoggingToFileDescription, kOsAll,
     SINGLE_VALUE_TYPE(network::switches::kLogNetLog)},

#if !BUILDFLAG(IS_ANDROID)
    {"web-authentication-permit-enterprise-attestation",
     flag_descriptions::kWebAuthenticationPermitEnterpriseAttestationName,
     flag_descriptions::
         kWebAuthenticationPermitEnterpriseAttestationDescription,
     kOsAll,
     ORIGIN_LIST_VALUE_TYPE(
         webauthn::switches::kPermitEnterpriseAttestationOriginList,
         "")},
#endif

    {
        "zero-copy-tab-capture",
        flag_descriptions::kEnableZeroCopyTabCaptureName,
        flag_descriptions::kEnableZeroCopyTabCaptureDescription,
        kOsMac | kOsWin | kOsCrOS,
        FEATURE_VALUE_TYPE(blink::features::kZeroCopyTabCapture),
    },

#if BUILDFLAG(ENABLE_PDF)
    {"accessible-pdf-form", flag_descriptions::kAccessiblePDFFormName,
     flag_descriptions::kAccessiblePDFFormDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kAccessiblePDFForm)},

    {"pdf-oopif", flag_descriptions::kPdfOopifName,
     flag_descriptions::kPdfOopifDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfOopif)},

    {"pdf-portfolio", flag_descriptions::kPdfPortfolioName,
     flag_descriptions::kPdfPortfolioDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfPortfolio)},

    {"pdf-use-skia-renderer", flag_descriptions::kPdfUseSkiaRendererName,
     flag_descriptions::kPdfUseSkiaRendererDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfUseSkiaRenderer)},

#if BUILDFLAG(ENABLE_PDF_INK2)
    {"pdf-ink2", flag_descriptions::kPdfInk2Name,
     flag_descriptions::kPdfInk2Description, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome_pdf::features::kPdfInk2,
                                    kPdfInk2Variations,
                                    "PdfInk2")},
#endif  // BUILDFLAG(ENABLE_PDF_INK2)

#if BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)
    {"pdf-save-to-drive", flag_descriptions::kPdfSaveToDriveName,
     flag_descriptions::kPdfSaveToDriveDescription, kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfSaveToDrive)},
#endif  // BUILDFLAG(ENABLE_PDF_SAVE_TO_DRIVE)

#endif  // BUILDFLAG(ENABLE_PDF)

#if BUILDFLAG(ENABLE_PRINTING)
#if BUILDFLAG(IS_CHROMEOS)
    {"add-printer-via-printscanmgr",
     flag_descriptions::kAddPrinterViaPrintscanmgrName,
     flag_descriptions::kAddPrinterViaPrintscanmgrDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(printing::features::kAddPrinterViaPrintscanmgr)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
    {"cups-ipp-printing-backend",
     flag_descriptions::kCupsIppPrintingBackendName,
     flag_descriptions::kCupsIppPrintingBackendDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(printing::features::kCupsIppPrintingBackend)},
#endif  // BUILDFLAG(IS_LINUX) ||BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN)
    {"fast-enumerate-printers", flag_descriptions::kFastEnumeratePrintersName,
     flag_descriptions::kFastEnumeratePrintersDescription, kOsWin,
     FEATURE_VALUE_TYPE(printing::features::kFastEnumeratePrinters)},

    {"print-with-postscript-type42-fonts",
     flag_descriptions::kPrintWithPostScriptType42FontsName,
     flag_descriptions::kPrintWithPostScriptType42FontsDescription, kOsWin,
     FEATURE_VALUE_TYPE(printing::features::kPrintWithPostScriptType42Fonts)},

    {"print-with-reduced-rasterization",
     flag_descriptions::kPrintWithReducedRasterizationName,
     flag_descriptions::kPrintWithReducedRasterizationDescription, kOsWin,
     FEATURE_VALUE_TYPE(printing::features::kPrintWithReducedRasterization)},

    {"read-printer-capabilities-with-xps",
     flag_descriptions::kReadPrinterCapabilitiesWithXpsName,
     flag_descriptions::kReadPrinterCapabilitiesWithXpsDescription, kOsWin,
     FEATURE_VALUE_TYPE(printing::features::kReadPrinterCapabilitiesWithXps)},

    {"use-xps-for-printing", flag_descriptions::kUseXpsForPrintingName,
     flag_descriptions::kUseXpsForPrintingDescription, kOsWin,
     FEATURE_VALUE_TYPE(printing::features::kUseXpsForPrinting)},

    {"use-xps-for-printing-from-pdf",
     flag_descriptions::kUseXpsForPrintingFromPdfName,
     flag_descriptions::kUseXpsForPrintingFromPdfDescription, kOsWin,
     FEATURE_VALUE_TYPE(printing::features::kUseXpsForPrintingFromPdf)},
#endif  // BUILDFLAG(IS_WIN)
#endif  // BUILDFLAG(ENABLE_PRINTING)

#if BUILDFLAG(IS_WIN)
    {"enable-windows-gaming-input-data-fetcher",
     flag_descriptions::kEnableWindowsGamingInputDataFetcherName,
     flag_descriptions::kEnableWindowsGamingInputDataFetcherDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kEnableWindowsGamingInputDataFetcher)},

    {"windows11-mica-titlebar", flag_descriptions::kWindows11MicaTitlebarName,
     flag_descriptions::kWindows11MicaTitlebarDescription, kOsWin,
     FEATURE_VALUE_TYPE(kWindows11MicaTitlebar)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"enable-nav-bar-matches-tab-android",
     flag_descriptions::kNavBarColorMatchesTabBackgroundName,
     flag_descriptions::kNavBarColorMatchesTabBackgroundDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNavBarColorMatchesTabBackground)},

    {"enable-navigation-capture-refactor-android",
     flag_descriptions::kNavigationCaptureRefactorAndroidName,
     flag_descriptions::kNavigationCaptureRefactorAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(external_intents::kNavigationCaptureRefactorAndroid)},

    {"enable-auxiliary-navigation-stays-in-browser",
     flag_descriptions::kAuxiliaryNavigationStaysInBrowserName,
     flag_descriptions::kAuxiliaryNavigationStaysInBrowserDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         external_intents::kAuxiliaryNavigationStaysInBrowser,
         kAuxiliaryNavigationStaysInBrowserVariations,
         "AuxiliaryNavigationStaysInBrowser")},

    {"enable-magic-stack-android", flag_descriptions::kMagicStackAndroidName,
     flag_descriptions::kMagicStackAndroidDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kMagicStackAndroid,
                                    kMagicStackAndroidVariations,
                                    "MagicStackAndroid")},

    {"enable-educational-tip-module",
     flag_descriptions::kEducationalTipModuleName,
     flag_descriptions::kEducationalTipModuleDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         segmentation_platform::features::kEducationalTipModule)},

    {"enable-educational-tip-default-browser-promo-card",
     flag_descriptions::kEducationalTipDefaultBrowserPromoCardName,
     flag_descriptions::kEducationalTipDefaultBrowserPromoCardDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kEducationalTipDefaultBrowserPromoCard)},

    {"enable-reparent-auxiliary-navigation-from-pwa",
     flag_descriptions::kReparentAuxiliaryNavigationFromPWAName,
     flag_descriptions::kReparentAuxiliaryNavigationFromPWADescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(external_intents::kReparentAuxiliaryNavigationFromPWA)},

    {"enable-reparent-top-level-navigation-from-pwa",
     flag_descriptions::kReparentTopLevelNavigationFromPWAName,
     flag_descriptions::kReparentTopLevelNavigationFromPWADescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(external_intents::kReparentTopLevelNavigationFromPWA)},

    {"enable-segmentation-platform-ephemeral_card_ranker",
     flag_descriptions::kSegmentationPlatformEphemeralCardRankerName,
     flag_descriptions::kSegmentationPlatformEphemeralCardRankerDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         segmentation_platform::features::
             kSegmentationPlatformEphemeralCardRanker,
         kEphemeralCardRankerCardOverrideOptions,
         "EducationalTipModule")},

    {"maylaunchurl-uses-separate-storage-partition",
     flag_descriptions::kMayLaunchUrlUsesSeparateStoragePartitionName,
     flag_descriptions::kMayLaunchUrlUsesSeparateStoragePartitionDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kMayLaunchUrlUsesSeparateStoragePartition)},

    {"mini-origin-bar", flag_descriptions::kMiniOriginBarName,
     flag_descriptions::kMiniOriginBarDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kMiniOriginBar)},

    {"enable-segmentation-platform-android-home-module-ranker-v2",
     flag_descriptions::kSegmentationPlatformAndroidHomeModuleRankerV2Name,
     flag_descriptions::
         kSegmentationPlatformAndroidHomeModuleRankerV2Description,
     kOsAndroid,
     FEATURE_VALUE_TYPE(segmentation_platform::features::
                            kSegmentationPlatformAndroidHomeModuleRankerV2)},

    {"search-in-cct", flag_descriptions::kSearchInCCTName,
     flag_descriptions::kSearchInCCTDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kSearchInCCT)},

    {"search-in-cct-alternate-tap-handling",
     flag_descriptions::kSearchInCCTAlternateTapHandlingName,
     flag_descriptions::kSearchInCCTAlternateTapHandlingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kSearchInCCTAlternateTapHandling)},

    {"settings-single-activity", flag_descriptions::kSettingsSingleActivityName,
     flag_descriptions::kSettingsSingleActivityDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kSettingsSingleActivity)},

    {"enable-search-resumption-module",
     flag_descriptions::kSearchResumptionModuleAndroidName,
     flag_descriptions::kSearchResumptionModuleAndroidDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kSearchResumptionModuleAndroid,
         kSearchResumptionModuleAndroidVariations,
         "kSearchResumptionModuleAndroid")},

    {"enable-tabstate-flatbuffer", flag_descriptions::kTabStateFlatBufferName,
     flag_descriptions::kTabStateFlatBufferDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kTabStateFlatBuffer,
                                    kTabStateFlatBufferVariations,
                                    "TabStateFlatBuffer")},

    {"price-insights", commerce::flag_descriptions::kPriceInsightsName,
     commerce::flag_descriptions::kPriceInsightsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(commerce::kPriceInsights)},

    {"enable-start-surface-return-time",
     flag_descriptions::kStartSurfaceReturnTimeName,
     flag_descriptions::kStartSurfaceReturnTimeDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kStartSurfaceReturnTime,
                                    kStartSurfaceReturnTimeVariations,
                                    "StartSurfaceReturnTime")},

    {"tab-switcher-drag-drop", flag_descriptions::kTabSwitcherDragDropName,
     flag_descriptions::kTabSwitcherDragDropDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabSwitcherDragDropAndroid)},

    {"tab-archival-drag-drop-android",
     flag_descriptions::kTabArchivalDragDropAndroidName,
     flag_descriptions::kTabArchivalDragDropAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabArchivalDragDropAndroid)},

    {"most-visited-tiles-customization",
     flag_descriptions::kMostVisitedTilesCustomizationName,
     flag_descriptions::kMostVisitedTilesCustomizationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kMostVisitedTilesCustomization)},

    {"enable-most-visited-tiles-reselect",
     flag_descriptions::kMostVisitedTilesReselectName,
     flag_descriptions::kMostVisitedTilesReselectDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kMostVisitedTilesReselect,
                                    kMostVisitedTilesReselectVariations,
                                    "kMostVisitedTilesReselect")},

    {"hide-tablet-toolbar-download-button",
     flag_descriptions::kHideTabletToolbarDownloadButtonName,
     flag_descriptions::kHideTabletToolbarDownloadButtonDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kHideTabletToolbarDownloadButton)},

    {"show-new-tab-animations", flag_descriptions::kShowNewTabAnimationsName,
     flag_descriptions::kShowNewTabAnimationsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kShowNewTabAnimations)},

    {"tab-switcher-color-blend-animate",
     flag_descriptions::kTabSwitcherColorBlendAnimateName,
     flag_descriptions::kTabSwitcherColorBlendAnimateDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kTabSwitcherColorBlendAnimate,
         kTabSwitcherColorBlendAnimateVariations,
         "TabSwitcherColorBlendAnimateVariations")},

#endif  // BUILDFLAG(IS_ANDROID)

    {"report-notification-content-detection-data",
     flag_descriptions::kReportNotificationContentDetectionDataName,
     flag_descriptions::kReportNotificationContentDetectionDataDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         safe_browsing::kReportNotificationContentDetectionData,
         kReportNotificationContentDetectionDataVariations,
         "ReportNotificationContentDetectionData")},

    {"show-warnings-for-suspicious-notifications",
     flag_descriptions::kShowWarningsForSuspiciousNotificationsName,
     flag_descriptions::kShowWarningsForSuspiciousNotificationsDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         safe_browsing::kShowWarningsForSuspiciousNotifications,
         kShowWarningsForSuspiciousNotificationsVariations,
         "ShowWarningsForSuspiciousNotifications")},

    {"unsafely-treat-insecure-origin-as-secure",
     flag_descriptions::kTreatInsecureOriginAsSecureName,
     flag_descriptions::kTreatInsecureOriginAsSecureDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(
         network::switches::kUnsafelyTreatInsecureOriginAsSecure,
         "")},

    {"disable-process-reuse", flag_descriptions::kDisableProcessReuse,
     flag_descriptions::kDisableProcessReuseDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDisableProcessReuse)},

    {"subframe-process-reuse-thresholds",
     flag_descriptions::kSubframeProcessReuseThresholds,
     flag_descriptions::kSubframeProcessReuseThresholdsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kSubframeProcessReuseThresholds,
         kSubframeProcessReuseThresholdsVariations,
         "SubframeProcessReuseThresholds" /* trial name */)},

#if !BUILDFLAG(IS_ANDROID)
    {"enable-live-caption-multilang",
     flag_descriptions::kEnableLiveCaptionMultilangName,
     flag_descriptions::kEnableLiveCaptionMultilangDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kLiveCaptionMultiLanguage)},

    {"enable-headless-live-caption",
     flag_descriptions::kEnableHeadlessLiveCaptionName,
     flag_descriptions::kEnableHeadlessLiveCaptionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kHeadlessLiveCaption)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-chromeos-live-translate",
     flag_descriptions::kEnableCrOSLiveTranslateName,
     flag_descriptions::kEnableCrOSLiveTranslateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kLiveTranslate)},
    {"enable-chromeos-soda-languages",
     flag_descriptions::kEnableCrOSSodaLanguagesName,
     flag_descriptions::kEnableCrOSSodaLanguagesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(speech::kCrosExpandSodaLanguages)},
    {"enable-chromeos-soda-conch",
     flag_descriptions::kEnableCrOSSodaConchLanguagesName,
     flag_descriptions::kEnableCrOSSodaLanguagesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(speech::kCrosSodaConchLanguages)},
#endif

    {"read-anything-read-aloud", flag_descriptions::kReadAnythingReadAloudName,
     flag_descriptions::kReadAnythingReadAloudDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingReadAloud)},

    {"read-anything-read-aloud-phrase-highlighting",
     flag_descriptions::kReadAnythingReadAloudPhraseHighlightingName,
     flag_descriptions::kReadAnythingReadAloudPhraseHighlightingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingReadAloudPhraseHighlighting)},

    {"read-anything-images-via-algorithm",
     flag_descriptions::kReadAnythingImagesViaAlgorithmName,
     flag_descriptions::kReadAnythingImagesViaAlgorithmDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingImagesViaAlgorithm)},

    {"read-anything-docs-integration",
     flag_descriptions::kReadAnythingDocsIntegrationName,
     flag_descriptions::kReadAnythingDocsIntegrationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingDocsIntegration)},

    {"read-anything-docs-load-more-button",
     flag_descriptions::kReadAnythingDocsLoadMoreButtonName,
     flag_descriptions::kReadAnythingDocsLoadMoreButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kReadAnythingDocsLoadMoreButton)},

    {"support-tool-screenshot", flag_descriptions::kSupportToolScreenshot,
     flag_descriptions::kSupportToolScreenshotDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSupportToolScreenshot)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
    {"wasm-tts-component-updater-enabled",
     flag_descriptions::kWasmTtsComponentUpdaterEnabledName,
     flag_descriptions::kWasmTtsComponentUpdaterEnabledDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWasmTtsComponentUpdaterEnabled)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
#endif  // !BUILDFLAG(IS_ANDROID)

    {"enable-auto-disable-accessibility",
     flag_descriptions::kEnableAutoDisableAccessibilityName,
     flag_descriptions::kEnableAutoDisableAccessibilityDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kAutoDisableAccessibility)},

    {"image-descriptions-alternative-routing",
     flag_descriptions::kImageDescriptionsAlternateRoutingName,
     flag_descriptions::kImageDescriptionsAlternateRoutingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kImageDescriptionsAlternateRouting)},

#if BUILDFLAG(IS_ANDROID)
    {"app-specific-history", flag_descriptions::kAppSpecificHistoryName,
     flag_descriptions::kAppSpecificHistoryDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAppSpecificHistory)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"cct-adaptive-button", flag_descriptions::kCCTAdaptiveButtonName,
     flag_descriptions::kCCTAdaptiveButtonDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kCCTAdaptiveButton,
                                    kCCTAdaptiveButtonVariations,
                                    "CCTAdaptiveButton")},
    {"cct-adaptive-button-test-switch",
     flag_descriptions::kCCTAdaptiveButtonTestSwitchName,
     flag_descriptions::kCCTAdaptiveButtonTestSwitchDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kCCTAdaptiveButtonTestSwitch,
         kCCTAdaptiveButtonTestSwitchVariations,
         "CCTAdaptiveButtonTestSwitch")},
    {"cct-auth-tab", flag_descriptions::kCCTAuthTabName,
     flag_descriptions::kCCTAuthTabDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTAuthTab)},
    {"cct-auth-tab-disable-all-external-intents",
     flag_descriptions::kCCTAuthTabDisableAllExternalIntentsName,
     flag_descriptions::kCCTAuthTabDisableAllExternalIntentsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTAuthTabDisableAllExternalIntents)},
    {"cct-auth-tab-enable-https-redirects",
     flag_descriptions::kCCTAuthTabEnableHttpsRedirectsName,
     flag_descriptions::kCCTAuthTabEnableHttpsRedirectsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kCCTAuthTabEnableHttpsRedirects,
         kCCTAuthTabEnableHttpsRedirectsVariations,
         "CCTAuthTabEnableHttpsRedirectsVariations")},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"cct-ephemeral-mode", flag_descriptions::kCCTEphemeralModeName,
     flag_descriptions::kCCTEphemeralModeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTEphemeralMode)},
    {"cct-ephemeral-media-viewer-experiment",
     flag_descriptions::kCCTEphemeralMediaViewerExperimentName,
     flag_descriptions::kCCTEphemeralMediaViewerExperimentDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTEphemeralMediaViewerExperiment)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"cct-incognito-available-to-third-party",
     flag_descriptions::kCCTIncognitoAvailableToThirdPartyName,
     flag_descriptions::kCCTIncognitoAvailableToThirdPartyDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTIncognitoAvailableToThirdParty)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"cct-minimized", flag_descriptions::kCCTMinimizedName,
     flag_descriptions::kCCTMinimizedDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kCCTMinimized,
                                    kCCTMinimizedIconVariations,
                                    "CCTMinimizedIconVariations")},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"cct-contextual-menu-items",
     flag_descriptions::kCCTContextualMenuItemsName,
     flag_descriptions::kCCTContextualMenuItemsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTContextualMenuItems)},
    {"cct-resizable-for-third-parties",
     flag_descriptions::kCCTResizableForThirdPartiesName,
     flag_descriptions::kCCTResizableForThirdPartiesDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kCCTResizableForThirdParties,
         kCCTResizableThirdPartiesDefaultPolicyVariations,
         "CCTResizableThirdPartiesDefaultPolicy")},
    {"cct-google-bottom-bar", flag_descriptions::kCCTGoogleBottomBarName,
     flag_descriptions::kCCTGoogleBottomBarDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kCCTGoogleBottomBar,
                                    kCCTGoogleBottomBarVariations,
                                    "CCTGoogleBottomBarVariations")},
    {"cct-google-bottom-bar-variant-layouts",
     flag_descriptions::kCCTGoogleBottomBarVariantLayoutsName,
     flag_descriptions::kCCTGoogleBottomBarVariantLayoutsDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kCCTGoogleBottomBarVariantLayouts,
         kCCTGoogleBottomBarVariantLayoutsVariations,
         "CCTGoogleBottomBarVariantLayoutsVariations")},
    {"cct-open-in-browser-button-if-allowed-by-embedder",
     flag_descriptions::kCCTOpenInBrowserButtonIfAllowedByEmbedderName,
     flag_descriptions::kCCTOpenInBrowserButtonIfAllowedByEmbedderDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kCCTOpenInBrowserButtonIfAllowedByEmbedder)},
    {"cct-open-in-browser-button-if-enabled-by-embedder",
     flag_descriptions::kCCTOpenInBrowserButtonIfEnabledByEmbedderName,
     flag_descriptions::kCCTOpenInBrowserButtonIfEnabledByEmbedderDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kCCTOpenInBrowserButtonIfEnabledByEmbedder)},
    {"cct-predictive-back-gesture",
     flag_descriptions::kCCTPredictiveBackGestureName,
     flag_descriptions::kCCTPredictiveBackGestureDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTPredictiveBackGesture)},
    {"cct-revamped-branding", flag_descriptions::kCCTRevampedBrandingName,
     flag_descriptions::kCCTRevampedBrandingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTRevampedBranding)},
    {"cct-nested-security-icon", flag_descriptions::kCCTNestedSecurityIconName,
     flag_descriptions::kCCTNestedSecurityIconDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTNestedSecurityIcon)},
    {"cct-toolbar-refactor", flag_descriptions::kCCTToolbarRefactorName,
     flag_descriptions::kCCTToolbarRefactorDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCCTToolbarRefactor)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"allow-dsp-based-aec", flag_descriptions::kCrOSDspBasedAecAllowedName,
     flag_descriptions::kCrOSDspBasedAecAllowedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSDspBasedAecAllowed)},
    {"allow-dsp-based-ns", flag_descriptions::kCrOSDspBasedNsAllowedName,
     flag_descriptions::kCrOSDspBasedNsAllowedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSDspBasedNsAllowed)},
    {"allow-dsp-based-agc", flag_descriptions::kCrOSDspBasedAgcAllowedName,
     flag_descriptions::kCrOSDspBasedAgcAllowedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSDspBasedAgcAllowed)},
    {"enforce-mono-audio-capture",
     flag_descriptions::kCrOSEnforceMonoAudioCaptureName,
     flag_descriptions::kCrOSEnforceMonoAudioCaptureDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSEnforceMonoAudioCapture)},
    {"enforce-system-aec", flag_descriptions::kCrOSEnforceSystemAecName,
     flag_descriptions::kCrOSEnforceSystemAecDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSEnforceSystemAec)},
    {"enforce-system-aec-agc", flag_descriptions::kCrOSEnforceSystemAecAgcName,
     flag_descriptions::kCrOSEnforceSystemAecAgcDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSEnforceSystemAecAgc)},
    {"enforce-system-aec-ns-agc",
     flag_descriptions::kCrOSEnforceSystemAecNsAgcName,
     flag_descriptions::kCrOSEnforceSystemAecNsAgcDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSEnforceSystemAecNsAgc)},
    {"enforce-system-aec-ns", flag_descriptions::kCrOSEnforceSystemAecNsName,
     flag_descriptions::kCrOSEnforceSystemAecNsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSEnforceSystemAecNs)},
    {"system-voice-isolation-option",
     flag_descriptions::kCrOSSystemVoiceIsolationOptionName,
     flag_descriptions::kCrOSSystemVoiceIsolationOptionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kCrOSSystemVoiceIsolationOption)},
    {"ignore-ui-gains", flag_descriptions::kIgnoreUiGainsName,
     flag_descriptions::kIgnoreUiGainsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kIgnoreUiGains)},
    {"show-force-respect-ui-gains-toggle",
     flag_descriptions::kShowForceRespectUiGainsToggleName,
     flag_descriptions::kShowForceRespectUiGainsToggleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kShowForceRespectUiGainsToggle)},
    {"show-spatial-audio-toggle",
     flag_descriptions::kShowSpatialAudioToggleName,
     flag_descriptions::kShowSpatialAudioToggleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kShowSpatialAudioToggle)},
    {"single-ca-cert-verification-phase-0",
     flag_descriptions::kSingleCaCertVerificationPhase0Name,
     flag_descriptions::kSingleCaCertVerificationPhase0Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSingleCaCertVerificationPhase0)},
    {"single-ca-cert-verification-phase-1",
     flag_descriptions::kSingleCaCertVerificationPhase1Name,
     flag_descriptions::kSingleCaCertVerificationPhase1Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSingleCaCertVerificationPhase1)},
    {"single-ca-cert-verification-phase-2",
     flag_descriptions::kSingleCaCertVerificationPhase2Name,
     flag_descriptions::kSingleCaCertVerificationPhase2Description, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSingleCaCertVerificationPhase2)},
#endif

    {"boundary-event-dispatch-tracks-node-removal",
     flag_descriptions::kBoundaryEventDispatchTracksNodeRemovalName,
     flag_descriptions::kBoundaryEventDispatchTracksNodeRemovalDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kBoundaryEventDispatchTracksNodeRemoval)},

    // Should only be available if kResamplingScrollEvents is on, and using
    // linear resampling.
    {"enable-resampling-scroll-events-experimental-prediction",
     flag_descriptions::kEnableResamplingScrollEventsExperimentalPredictionName,
     flag_descriptions::
         kEnableResamplingScrollEventsExperimentalPredictionDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ::features::kResamplingScrollEventsExperimentalPrediction,
         kResamplingScrollEventsExperimentalPredictionVariations,
         "ResamplingScrollEventsExperimentalLatency")},

#if BUILDFLAG(IS_WIN)
    {"calculate-native-win-occlusion",
     flag_descriptions::kCalculateNativeWinOcclusionName,
     flag_descriptions::kCalculateNativeWinOcclusionDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kCalculateNativeWinOcclusion)},
#endif  // BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_ANDROID)
    {"happiness-tracking-surveys-for-desktop-demo",
     flag_descriptions::kHappinessTrackingSurveysForDesktopDemoName,
     flag_descriptions::kHappinessTrackingSurveysForDesktopDemoDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kHappinessTrackingSurveysForDesktopDemo,
         kHappinessTrackingSurveysForDesktopDemoVariations,
         "HappinessTrackingSurveysForDesktopDemo")},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"android-elegant-text-height",
     flag_descriptions::kAndroidElegantTextHeightName,
     flag_descriptions::kAndroidElegantTextHeightDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidElegantTextHeight)},

#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN)
    {"use-angle", flag_descriptions::kUseAngleName,
     flag_descriptions::kUseAngleDescriptionWindows, kOsWin,
     MULTI_VALUE_TYPE(kUseAngleChoicesWindows)},
#elif BUILDFLAG(IS_MAC)
    {"use-angle", flag_descriptions::kUseAngleName,
     flag_descriptions::kUseAngleDescriptionMac, kOsMac,
     MULTI_VALUE_TYPE(kUseAngleChoicesMac)},
#elif BUILDFLAG(IS_ANDROID)
        {"use-angle", flag_descriptions::kUseAngleName,
         flag_descriptions::kUseAngleDescriptionAndroid, kOsAndroid,
         MULTI_VALUE_TYPE(kUseAngleChoicesAndroid)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-assistant-dsp", flag_descriptions::kEnableGoogleAssistantDspName,
     flag_descriptions::kEnableGoogleAssistantDspDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::assistant::features::kEnableDspHotword)},
    {"disable-quick-answers-v2-translation",
     flag_descriptions::kDisableQuickAnswersV2TranslationName,
     flag_descriptions::kDisableQuickAnswersV2TranslationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kDisableQuickAnswersV2Translation)},
    {"quick-answers-rich-card", flag_descriptions::kQuickAnswersRichCardName,
     flag_descriptions::kQuickAnswersRichCardDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kQuickAnswersRichCard)},
    {"quick-answers-material-next-ui",
     flag_descriptions::kQuickAnswersMaterialNextUIName,
     flag_descriptions::kQuickAnswersMaterialNextUIDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kQuickAnswersMaterialNextUI)},
    {"magic-boost-revamp-for-quick-answers",
     flag_descriptions::kMagicBoostUpdateForQuickAnswersName,
     flag_descriptions::kMagicBoostUpdateForQuickAnswersDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kMagicBoostRevampForQuickAnswers)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-gamepad-multitouch",
     flag_descriptions::kEnableGamepadMultitouchName,
     flag_descriptions::kEnableGamepadMultitouchDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kEnableGamepadMultitouch)},

#if !BUILDFLAG(IS_ANDROID)
    {"sharing-desktop-screenshots",
     flag_descriptions::kSharingDesktopScreenshotsName,
     flag_descriptions::kSharingDesktopScreenshotsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(sharing_hub::kDesktopScreenshots)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-labs-enable-overview-from-wallpaper",
     flag_descriptions::kEnterOverviewFromWallpaperName,
     flag_descriptions::kEnterOverviewFromWallpaperDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnterOverviewFromWallpaper)},
    {"enable-assistant-stereo-input",
     flag_descriptions::kEnableGoogleAssistantStereoInputName,
     flag_descriptions::kEnableGoogleAssistantStereoInputDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::assistant::features::kEnableStereoAudioInput)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-gpu-service-logging",
     flag_descriptions::kEnableGpuServiceLoggingName,
     flag_descriptions::kEnableGpuServiceLoggingDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableGPUServiceLogging)},

#if !BUILDFLAG(IS_ANDROID)
    {"hardware-media-key-handling",
     flag_descriptions::kHardwareMediaKeyHandling,
     flag_descriptions::kHardwareMediaKeyHandlingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kHardwareMediaKeyHandling)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"arc-window-predictor", flag_descriptions::kArcWindowPredictorName,
     flag_descriptions::kArcWindowPredictorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(full_restore::features::kArcWindowPredictor)},

    {"use-annotated-account-id", flag_descriptions::kUseAnnotatedAccountIdName,
     flag_descriptions::kUseAnnotatedAccountIdDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kUseAnnotatedAccountId)},

    {"use-fake-device-for-media-stream",
     flag_descriptions::kUseFakeDeviceForMediaStreamName,
     flag_descriptions::kUseFakeDeviceForMediaStreamDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kUseFakeDeviceForMediaStream)},

#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
    {"enable-vbr-encode-acceleration",
     flag_descriptions::kChromeOSHWVBREncodingName,
     flag_descriptions::kChromeOSHWVBREncodingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kChromeOSHWVBREncoding)},
#if defined(ARCH_CPU_ARM_FAMILY)
    {"use-gl-scaling", flag_descriptions::kUseGLForScalingName,
     flag_descriptions::kUseGLForScalingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kUseGLForScaling)},
    {"prefer-gl-image-processor",
     flag_descriptions::kPreferGLImageProcessorName,
     flag_descriptions::kPreferGLImageProcessorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kPreferGLImageProcessor)},
    {"prefer-software-mt21", flag_descriptions::kPreferSoftwareMT21Name,
     flag_descriptions::kPreferSoftwareMT21Description, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kPreferSoftwareMT21)},
    {"enable-protected-vulkan-detiling",
     flag_descriptions::kEnableProtectedVulkanDetilingName,
     flag_descriptions::kEnableProtectedVulkanDetilingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kEnableProtectedVulkanDetiling)},
    {"enable-arm-hwdrm-10bit-overlays",
     flag_descriptions::kEnableArmHwdrm10bitOverlaysName,
     flag_descriptions::kEnableArmHwdrm10bitOverlaysDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kEnableArmHwdrm10bitOverlays)},
#if BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)
    {"enable-arm-hwdrm", flag_descriptions::kEnableArmHwdrmName,
     flag_descriptions::kEnableArmHwdrmDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kEnableArmHwdrm)},
#endif  // BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)
#endif  // defined(ARCH_CPU_ARM_FAMILY)
#endif  // BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"force-startup-signin-promo",
     flag_descriptions::kForceStartupSigninPromoName,
     flag_descriptions::kForceStartupSigninPromoDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(switches::kForceStartupSigninPromo)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"file-handling-icons", flag_descriptions::kFileHandlingIconsName,
     flag_descriptions::kFileHandlingIconsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kFileHandlingIcons)},

    {"file-system-observer", flag_descriptions::kFileSystemObserverName,
     flag_descriptions::kFileSystemObserverDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kFileSystemObserver)},

    {"strict-origin-isolation", flag_descriptions::kStrictOriginIsolationName,
     flag_descriptions::kStrictOriginIsolationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kStrictOriginIsolation)},

#if BUILDFLAG(IS_ANDROID)
    {"enable-logging-js-console-messages",
     flag_descriptions::kLogJsConsoleMessagesName,
     flag_descriptions::kLogJsConsoleMessagesDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kLogJsConsoleMessages)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"allow-cross-device-feature-suite",
     flag_descriptions::kAllowCrossDeviceFeatureSuiteName,
     flag_descriptions::kAllowCrossDeviceFeatureSuiteDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAllowCrossDeviceFeatureSuite)},

    {"link-cross-device-internals",
     flag_descriptions::kLinkCrossDeviceInternalsName,
     flag_descriptions::kLinkCrossDeviceInternalsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kLinkCrossDeviceInternals)},

    {"block-telephony-device-phone-mute",
     flag_descriptions::kBlockTelephonyDevicePhoneMuteName,
     flag_descriptions::kBlockTelephonyDevicePhoneMuteDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kBlockTelephonyDevicePhoneMute)},

    {"enable-doze-mode-power-scheduler",
     flag_descriptions::kEnableDozeModePowerSchedulerName,
     flag_descriptions::kEnableDozeModePowerSchedulerDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableDozeModePowerScheduler)},

    {"enable-fast-ink-for-software-cursor",
     flag_descriptions::kEnableFastInkForSoftwareCursorName,
     flag_descriptions::kEnableFastInkForSoftwareCursorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableFastInkForSoftwareCursor)},

    {"enable-heatmap-palm-detection",
     flag_descriptions::kEnableHeatmapPalmDetectionName,
     flag_descriptions::kEnableHeatmapPalmDetectionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableHeatmapPalmDetection)},

    {"enable-neural-stylus-palm-rejection",
     flag_descriptions::kEnableNeuralStylusPalmRejectionName,
     flag_descriptions::kEnableNeuralStylusPalmRejectionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableNeuralPalmDetectionFilter)},

    {"enable-edge-detection", flag_descriptions::kEnableEdgeDetectionName,
     flag_descriptions::kEnableEdgeDetectionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableEdgeDetection)},

    {"enable-fast-touchpad-click",
     flag_descriptions::kEnableFastTouchpadClickName,
     flag_descriptions::kEnableFastTouchpadClickDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableFastTouchpadClick)},

    {"fast-pair-debug-metadata", flag_descriptions::kFastPairDebugMetadataName,
     flag_descriptions::kFastPairDebugMetadataDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFastPairDebugMetadata)},

    {"fast-pair-handshake-long-term-refactor",
     flag_descriptions::kFastPairHandshakeLongTermRefactorName,
     flag_descriptions::kFastPairHandshakeLongTermRefactorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFastPairHandshakeLongTermRefactor)},

    {"fast-pair-keyboards", flag_descriptions::kFastPairKeyboardsName,
     flag_descriptions::kFastPairKeyboardsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFastPairKeyboards)},

    {"fast-pair-pwa-companion", flag_descriptions::kFastPairPwaCompanionName,
     flag_descriptions::kFastPairPwaCompanionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFastPairPwaCompanion)},

    {"nearby-ble-v2", flag_descriptions::kEnableNearbyBleV2Name,
     flag_descriptions::kEnableNearbyBleV2Description, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableNearbyBleV2)},

    {"nearby-ble-v2-extended-adv",
     flag_descriptions::kEnableNearbyBleV2ExtendedAdvertisingName,
     flag_descriptions::kEnableNearbyBleV2ExtendedAdvertisingDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableNearbyBleV2ExtendedAdvertising)},

    {"nearby-ble-v2-gatt-server",
     flag_descriptions::kEnableNearbyBleV2GattServerName,
     flag_descriptions::kEnableNearbyBleV2GattServerDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableNearbyBleV2GattServer)},

    {"nearby-bluetooth-classic-adv",
     flag_descriptions::kEnableNearbyBluetoothClassicAdvertisingName,
     flag_descriptions::kEnableNearbyBluetoothClassicAdvertisingDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableNearbyBluetoothClassicAdvertising)},

    {"nearby-mdns", flag_descriptions::kEnableNearbyMdnsName,
     flag_descriptions::kEnableNearbyMdnsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableNearbyMdns)},

    {"nearby-presence", flag_descriptions::kNearbyPresenceName,
     flag_descriptions::kNearbyPresenceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kNearbyPresence)},

    {"nearby-webrtc", flag_descriptions::kEnableNearbyWebRtcName,
     flag_descriptions::kEnableNearbyWebRtcDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kNearbySharingWebRtc)},

    {"nearby-wifi-direct", flag_descriptions::kEnableNearbyWifiDirectName,
     flag_descriptions::kEnableNearbyWifiDirectDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kNearbySharingWifiDirect)},

    {"nearby-wifi-lan", flag_descriptions::kEnableNearbyWifiLanName,
     flag_descriptions::kEnableNearbyWifiLanDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kNearbySharingWifiLan)},

    {"pcie-billboard-notification",
     flag_descriptions::kPcieBillboardNotificationName,
     flag_descriptions::kPcieBillboardNotificationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPcieBillboardNotification)},

    {"use-search-click-for-right-click",
     flag_descriptions::kUseSearchClickForRightClickName,
     flag_descriptions::kUseSearchClickForRightClickDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kUseSearchClickForRightClick)},

    {"display-alignment-assistance",
     flag_descriptions::kDisplayAlignmentAssistanceName,
     flag_descriptions::kDisplayAlignmentAssistanceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDisplayAlignAssist)},

    {"enable-experimental-rgb-keyboard-patterns",
     flag_descriptions::kExperimentalRgbKeyboardPatternsName,
     flag_descriptions::kExperimentalRgbKeyboardPatternsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kExperimentalRgbKeyboardPatterns)},

    {"enable-hostname-setting", flag_descriptions::kEnableHostnameSettingName,
     flag_descriptions::kEnableHostnameSettingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableHostnameSetting)},

    {"enable-oauth-ipp", flag_descriptions::kEnableOAuthIppName,
     flag_descriptions::kEnableOAuthIppDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableOAuthIpp)},

    {"enable-suspend-state-machine",
     flag_descriptions::kEnableSuspendStateMachineName,
     flag_descriptions::kEnableSuspendStateMachineDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSuspendStateMachine)},

    {"enable-input-device-settings-split",
     flag_descriptions::kEnableInputDeviceSettingsSplitName,
     flag_descriptions::kEnableInputDeviceSettingsSplitDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kInputDeviceSettingsSplit)},

    {"enable-peripheral-customization",
     flag_descriptions::kEnablePeripheralCustomizationName,
     flag_descriptions::kEnablePeripheralCustomizationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPeripheralCustomization)},

    {"enable-peripherals-logging",
     flag_descriptions::kEnablePeripheralsLoggingName,
     flag_descriptions::kEnablePeripheralsLoggingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnablePeripheralsLogging)},

    {"enable-peripheral-notification",
     flag_descriptions::kEnablePeripheralNotificationName,
     flag_descriptions::kEnablePeripheralNotificationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPeripheralNotification)},

    {"enable-accessibility-accelerator",
     flag_descriptions::kAccessibilityAcceleratorName,
     flag_descriptions::kAccessibilityAcceleratorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityAccelerator)},

    {"enable-accessibility-disable-touchpad",
     flag_descriptions::kAccessibilityDisableTouchpadName,
     flag_descriptions::kAccessibilityDisableTouchpadDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityDisableTouchpad)},

    {"enable-accessibility-flash-screen-feature",
     flag_descriptions::kAccessibilityFlashScreenFeatureName,
     flag_descriptions::kAccessibilityFlashScreenFeatureDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityFlashScreenFeature)},

    {"enable-accessibility-shake-to-locate",
     flag_descriptions::kAccessibilityShakeToLocateName,
     flag_descriptions::kAccessibilityShakeToLocateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityShakeToLocate)},

    {"enable-accessibility-service",
     flag_descriptions::kAccessibilityServiceName,
     flag_descriptions::kAccessibilityServiceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityService)},

    {"enable-accessibility-reduced-animations",
     flag_descriptions::kAccessibilityReducedAnimationsName,
     flag_descriptions::kAccessibilityReducedAnimationsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityReducedAnimations)},

    {"enable-accessibility-reduced-animations-in-kiosk",
     flag_descriptions::kAccessibilityReducedAnimationsInKioskName,
     flag_descriptions::kAccessibilityReducedAnimationsInKioskDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityReducedAnimationsInKiosk)},

    {"enable-accessibility-facegaze",
     flag_descriptions::kAccessibilityFaceGazeName,
     flag_descriptions::kAccessibilityFaceGazeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityFaceGaze)},

    {"enable-accessibility-magnifier-follows-chromevox",
     flag_descriptions::kAccessibilityMagnifierFollowsChromeVoxName,
     flag_descriptions::kAccessibilityMagnifierFollowsChromeVoxDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityMagnifierFollowsChromeVox)},

    {"enable-accessibility-manifest-v3-accessibility-common",
     flag_descriptions::kAccessibilityManifestV3AccessibilityCommonName,
     flag_descriptions::kAccessibilityManifestV3AccessibilityCommonDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityManifestV3AccessibilityCommon)},

    {"enable-accessibility-manifest-v3-braille-ime",
     flag_descriptions::kAccessibilityManifestV3BrailleImeName,
     flag_descriptions::kAccessibilityManifestV3BrailleImeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityManifestV3BrailleIme)},

    {"enable-accessibility-manifest-v3-chromevox",
     flag_descriptions::kAccessibilityManifestV3ChromeVoxName,
     flag_descriptions::kAccessibilityManifestV3ChromeVoxDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityManifestV3ChromeVox)},

    {"enable-accessibility-manifest-v3-enhanced-network-tts",
     flag_descriptions::kAccessibilityManifestV3EnhancedNetworkTtsName,
     flag_descriptions::kAccessibilityManifestV3EnhancedNetworkTtsDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityManifestV3EnhancedNetworkTts)},

    {"enable-accessibility-manifest-v3-espeakng",
     flag_descriptions::kAccessibilityManifestV3EspeakNGName,
     flag_descriptions::kAccessibilityManifestV3EspeakNGDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(::features::kAccessibilityManifestV3EspeakNGTts)},

    {"enable-accessibility-manifest-v3-google-tts",
     flag_descriptions::kAccessibilityManifestV3GoogleTtsName,
     flag_descriptions::kAccessibilityManifestV3GoogleTtsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(::features::kAccessibilityManifestV3GoogleTts)},

    {"enable-accessibility-manifest-v3-select-to-speak",
     flag_descriptions::kAccessibilityManifestV3SelectToSpeakName,
     flag_descriptions::kAccessibilityManifestV3SelectToSpeakDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityManifestV3SelectToSpeak)},

    {"enable-accessibility-manifest-v3-switch-access",
     flag_descriptions::kAccessibilityManifestV3SwitchAccessName,
     flag_descriptions::kAccessibilityManifestV3SwitchAccessDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityManifestV3SwitchAccess)},

    {"enable-accessibility-mousekeys",
     flag_descriptions::kAccessibilityMouseKeysName,
     flag_descriptions::kAccessibilityMouseKeysDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityMouseKeys)},

    {"enable-accessibility-captions-on-braille-display",
     flag_descriptions::kAccessibilityCaptionsOnBrailleDisplayName,
     flag_descriptions::kAccessibilityCaptionsOnBrailleDisplayDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kAccessibilityCaptionsOnBrailleDisplay)},

    {"event-based-log-upload", flag_descriptions::kEventBasedLogUpload,
     flag_descriptions::kEventBasedLogUploadDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEventBasedLogUpload)},

#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-fenced-frames-developer-mode",
     flag_descriptions::kEnableFencedFramesDeveloperModeName,
     flag_descriptions::kEnableFencedFramesDeveloperModeDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kFencedFramesDefaultMode)},

    {"enable-unsafe-webgpu", flag_descriptions::kUnsafeWebGPUName,
     flag_descriptions::kUnsafeWebGPUDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableUnsafeWebGPU)},

    {"force-high-performance-gpu",
     flag_descriptions::kForceHighPerformanceGPUName,
     flag_descriptions::kForceHighPerformanceGPUDescription, kOsWin,
     SINGLE_VALUE_TYPE(switches::kForceHighPerformanceGPU)},

    {"enable-webgpu-developer-features",
     flag_descriptions::kWebGpuDeveloperFeaturesName,
     flag_descriptions::kWebGpuDeveloperFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGPUDeveloperFeatures)},

#if BUILDFLAG(IS_CHROMEOS)
    {"game-dashboard-game-pwas", flag_descriptions::kGameDashboardGamePWAs,
     flag_descriptions::kGameDashboardGamePWAsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGameDashboardGamePWAs)},

    {"game-dashboard-gamepad-support",
     flag_descriptions::kGameDashboardGamepadSupport,
     flag_descriptions::kGameDashboardGamepadSupport, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGameDashboardGamepadSupport)},

    {"game-dashboard-games-in-test",
     flag_descriptions::kGameDashboardGamesInTest,
     flag_descriptions::kGameDashboardGamesInTestDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGameDashboardGamesInTest)},

    {"game-dashboard-utilities", flag_descriptions::kGameDashboardUtilities,
     flag_descriptions::kGameDashboardUtilitiesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGameDashboardUtilities)},

    {"gesture-properties-dbus-service",
     flag_descriptions::kEnableGesturePropertiesDBusServiceName,
     flag_descriptions::kEnableGesturePropertiesDBusServiceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGesturePropertiesDBusService)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_CHROMEOS)
    {"global-media-controls-updated-ui",
     flag_descriptions::kGlobalMediaControlsUpdatedUIName,
     flag_descriptions::kGlobalMediaControlsUpdatedUIDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(media::kGlobalMediaControlsUpdatedUI)},
#endif  // !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"enable-network-service-sandbox",
     flag_descriptions::kEnableNetworkServiceSandboxName,
     flag_descriptions::kEnableNetworkServiceSandboxDescription,
     kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(sandbox::policy::features::kNetworkServiceSandbox)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
    {"use-out-of-process-video-decoding",
     flag_descriptions::kUseOutOfProcessVideoDecodingName,
     flag_descriptions::kUseOutOfProcessVideoDecodingDescription,
     kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(media::kUseOutOfProcessVideoDecoding)},
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)

    {"notification-scheduler", flag_descriptions::kNotificationSchedulerName,
     flag_descriptions::kNotificationSchedulerDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(notifications::features::kNotificationScheduleService)},

    {"notification-scheduler-debug-options",
     flag_descriptions::kNotificationSchedulerDebugOptionName,
     flag_descriptions::kNotificationSchedulerDebugOptionDescription,
     kOsAndroid, MULTI_VALUE_TYPE(kNotificationSchedulerChoices)},

#if BUILDFLAG(IS_ANDROID)

    {"debug-chime-notification",
     flag_descriptions::kChimeAlwaysShowNotificationName,
     flag_descriptions::kChimeAlwaysShowNotificationDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(notifications::switches::kDebugChimeNotification)},

    {"use-chime-android-sdk", flag_descriptions::kChimeAndroidSdkName,
     flag_descriptions::kChimeAndroidSdkDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(notifications::features::kUseChimeAndroidSdk)},

#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"scalable-iph-debug", flag_descriptions::kScalableIphDebugName,
     flag_descriptions::kScalableIphDebugDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kScalableIphDebug)},
    {"settings-app-notification-settings",
     flag_descriptions::kSettingsAppNotificationSettingsName,
     flag_descriptions::kSettingsAppNotificationSettingsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSettingsAppNotificationSettings)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"sync-point-graph-validation",
     flag_descriptions::kSyncPointGraphValidationName,
     flag_descriptions::kSyncPointGraphValidationDescription,
     kOsLinux | kOsMac | kOsWin | kOsCrOS | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSyncPointGraphValidation)},

#if BUILDFLAG(IS_ANDROID)
    {"web-otp-backend", flag_descriptions::kWebOtpBackendName,
     flag_descriptions::kWebOtpBackendDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kWebOtpBackendChoices)},

    {"darken-websites-checkbox-in-themes-setting",
     flag_descriptions::kDarkenWebsitesCheckboxInThemesSettingName,
     flag_descriptions::kDarkenWebsitesCheckboxInThemesSettingDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         content_settings::kDarkenWebsitesCheckboxInThemesSetting)},

#endif  // BUILDFLAG(IS_ANDROID)

    {"back-forward-cache", flag_descriptions::kBackForwardCacheName,
     flag_descriptions::kBackForwardCacheDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kBackForwardCache,
                                    kBackForwardCacheVariations,
                                    "BackForwardCache")},
#if BUILDFLAG(IS_ANDROID)
    {"back-forward-transitions", flag_descriptions::kBackForwardTransitionsName,
     flag_descriptions::kBackForwardTransitionsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kBackForwardTransitions)},
    {"mirror-back-forward-gestures-in-rtl",
     flag_descriptions::kMirrorBackForwardGesturesInRTLName,
     flag_descriptions::kMirrorBackForwardGesturesInRTLDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(ui::kMirrorBackForwardGesturesInRTL)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID)
    {"elastic-overscroll", flag_descriptions::kElasticOverscrollName,
     flag_descriptions::kElasticOverscrollDescription, kOsWin | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kElasticOverscroll)},
#endif

#if !BUILDFLAG(IS_ANDROID)
    {"element-capture", flag_descriptions::kElementCaptureName,
     flag_descriptions::kElementCaptureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kElementCapture)},

    {"element-capture-cross-tab",
     flag_descriptions::kCrossTabElementCaptureName,
     flag_descriptions::kCrossTabElementCaptureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kElementCaptureOfOtherTabs)},
#endif

    {"device-posture", flag_descriptions::kDevicePostureName,
     flag_descriptions::kDevicePostureDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kDevicePosture)},

    {"viewport-segments", flag_descriptions::kViewportSegmentsName,
     flag_descriptions::kViewportSegmentsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kViewportSegments)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-assistant-aec", flag_descriptions::kEnableGoogleAssistantAecName,
     flag_descriptions::kEnableGoogleAssistantAecDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::assistant::features::kAssistantAudioEraser)},
#endif

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"enable-location-provider-manager",
     flag_descriptions::kLocationProviderManagerName,
     flag_descriptions::kLocationProviderManagerDescription, kOsMac | kOsWin,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kLocationProviderManager,
                                    kLocationProviderManagerVariations,
                                    "LocationProviderManager")},
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_ANDROID)
    {"mute-notification-snooze-action",
     flag_descriptions::kMuteNotificationSnoozeActionName,
     flag_descriptions::kMuteNotificationSnoozeActionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMuteNotificationSnoozeAction)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"notification-one-tap-unsubscribe",
     flag_descriptions::kNotificationOneTapUnsubscribeName,
     flag_descriptions::kNotificationOneTapUnsubscribeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kNotificationOneTapUnsubscribe)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
    {"enable-new-mac-notification-api",
     flag_descriptions::kNewMacNotificationAPIName,
     flag_descriptions::kNewMacNotificationAPIDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kNewMacNotificationAPI)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"exo-gamepad-vibration", flag_descriptions::kExoGamepadVibrationName,
     flag_descriptions::kExoGamepadVibrationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGamepadVibration)},
    {"exo-ordinal-motion", flag_descriptions::kExoOrdinalMotionName,
     flag_descriptions::kExoOrdinalMotionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kExoOrdinalMotion)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"heavy-ad-privacy-mitigations",
     flag_descriptions::kHeavyAdPrivacyMitigationsName,
     flag_descriptions::kHeavyAdPrivacyMitigationsDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         heavy_ad_intervention::features::kHeavyAdPrivacyMitigations)},

#if BUILDFLAG(IS_CHROMEOS)
    {"crostini-container-install",
     flag_descriptions::kCrostiniContainerInstallName,
     flag_descriptions::kCrostiniContainerInstallDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kCrostiniContainerChoices)},
    {"help-app-app-detail-page", flag_descriptions::kHelpAppAppDetailPageName,
     flag_descriptions::kHelpAppAppDetailPageDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHelpAppAppDetailPage)},
    {"help-app-apps-list", flag_descriptions::kHelpAppAppsListName,
     flag_descriptions::kHelpAppAppsListDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHelpAppAppsList)},
    {"help-app-auto-trigger-install-dialog",
     flag_descriptions::kHelpAppAutoTriggerInstallDialogName,
     flag_descriptions::kHelpAppAutoTriggerInstallDialogDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHelpAppAutoTriggerInstallDialog)},
    {"help-app-home-page-app-articles",
     flag_descriptions::kHelpAppHomePageAppArticlesName,
     flag_descriptions::kHelpAppHomePageAppArticlesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHelpAppHomePageAppArticles)},
    {"help-app-launcher-search", flag_descriptions::kHelpAppLauncherSearchName,
     flag_descriptions::kHelpAppLauncherSearchDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHelpAppLauncherSearch)},
    {"help-app-onboarding-revamp",
     flag_descriptions::kHelpAppOnboardingRevampName,
     flag_descriptions::kHelpAppOnboardingRevampDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHelpAppOnboardingRevamp)},
    {"help-app-opens-instead-of-release-notes-notification",
     flag_descriptions::kHelpAppOpensInsteadOfReleaseNotesNotificationName,
     flag_descriptions::
         kHelpAppOpensInsteadOfReleaseNotesNotificationDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kHelpAppOpensInsteadOfReleaseNotesNotification)},
    {"media-app-pdf-mahi", flag_descriptions::kMediaAppPdfMahiName,
     flag_descriptions::kMediaAppPdfMahiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kMediaAppPdfMahi)},
    {"media-app-image-mantis-reimagine",
     flag_descriptions::kMediaAppImageMantisReimagineName,
     flag_descriptions::kMediaAppImageMantisReimagineDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kMediaAppImageMantisReimagine)},
    {"on-device-app-controls", flag_descriptions::kOnDeviceAppControlsName,
     flag_descriptions::kOnDeviceAppControlsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kForceOnDeviceAppControlsForAllRegions)},
    {"release-notes-notification-all-channels",
     flag_descriptions::kReleaseNotesNotificationAllChannelsName,
     flag_descriptions::kReleaseNotesNotificationAllChannelsDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kReleaseNotesNotificationAllChannels)},
    {"release-notes-notification-always-eligible",
     flag_descriptions::kReleaseNotesNotificationAlwaysEligibleName,
     flag_descriptions::kReleaseNotesNotificationAlwaysEligibleDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kReleaseNotesNotificationAlwaysEligible)},
    {"use-android-staging-smds", flag_descriptions::kUseAndroidStagingSmdsName,
     flag_descriptions::kUseAndroidStagingSmdsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kUseAndroidStagingSmds)},
    {"use-stork-smds-server-address",
     flag_descriptions::kUseStorkSmdsServerAddressName,
     flag_descriptions::kUseStorkSmdsServerAddressDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kUseStorkSmdsServerAddress)},
    {"use-wallpaper-staging-url",
     flag_descriptions::kUseWallpaperStagingUrlName,
     flag_descriptions::kUseWallpaperStagingUrlDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kUseWallpaperStagingUrl)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ENABLE_PAINT_PREVIEW) && BUILDFLAG(IS_ANDROID)
    {"paint-preview-demo", flag_descriptions::kPaintPreviewDemoName,
     flag_descriptions::kPaintPreviewDemoDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(paint_preview::kPaintPreviewDemo)},
#endif  // BUILDFLAG(ENABLE_PAINT_PREVIEW) && BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"panel-self-refresh-2", flag_descriptions::kPanelSelfRefresh2Name,
     flag_descriptions::kPanelSelfRefresh2Description, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kPanelSelfRefresh2)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"automatic-fullscreen-content-setting",
     flag_descriptions::kAutomaticFullscreenContentSettingName,
     flag_descriptions::kAutomaticFullscreenContentSettingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kAutomaticFullscreenContentSetting)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
    {"run-video-capture-service-in-browser",
     flag_descriptions::kRunVideoCaptureServiceInBrowserProcessName,
     flag_descriptions::kRunVideoCaptureServiceInBrowserProcessDescription,
     kOsWin | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kRunVideoCaptureServiceInBrowserProcess)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
    {"double-buffer-compositing",
     flag_descriptions::kDoubleBufferCompositingName,
     flag_descriptions::kDoubleBufferCompositingDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kDoubleBufferCompositing)},

#if !BUILDFLAG(IS_ANDROID)
    {"page-info-hide-site-settings",
     flag_descriptions::kPageInfoHideSiteSettingsName,
     flag_descriptions::kPageInfoHideSiteSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(page_info::kPageInfoHideSiteSettings)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"page-info-history-desktop",
     flag_descriptions::kPageInfoHistoryDesktopName,
     flag_descriptions::kPageInfoHistoryDesktopDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(page_info::kPageInfoHistoryDesktop)},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"block-tpcs-incognito", flag_descriptions::kBlockTpcsIncognitoName,
     flag_descriptions::kBlockTpcsIncognitoDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(privacy_sandbox::kAlwaysBlock3pcsIncognito)},

    {"rws-v2-ui", flag_descriptions::kRwsV2UiName,
     flag_descriptions::kRwsV2UiDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxRelatedWebsiteSetsUi)},

    {"tracking-protection-3pcd", flag_descriptions::kTrackingProtection3pcdName,
     flag_descriptions::kTrackingProtection3pcdDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(content_settings::features::kTrackingProtection3pcd)},

#if BUILDFLAG(IS_CHROMEOS)
    {kClipboardHistoryLongpressInternalName,
     flag_descriptions::kClipboardHistoryLongpressName,
     flag_descriptions::kClipboardHistoryLongpressDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kClipboardHistoryLongpress)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_WIN)
    {"enable-media-foundation-video-capture",
     flag_descriptions::kEnableMediaFoundationVideoCaptureName,
     flag_descriptions::kEnableMediaFoundationVideoCaptureDescription, kOsWin,
     FEATURE_VALUE_TYPE(media::kMediaFoundationVideoCapture)},
#endif  // BUILDFLAG(IS_WIN)
    {"shared-highlighting-manager",
     flag_descriptions::kSharedHighlightingManagerName,
     flag_descriptions::kSharedHighlightingManagerDescription, kOsAll,
     FEATURE_VALUE_TYPE(shared_highlighting::kSharedHighlightingManager)},

#if BUILDFLAG(IS_CHROMEOS)
    {"reset-shortcut-customizations",
     flag_descriptions::kResetShortcutCustomizationsName,
     flag_descriptions::kResetShortcutCustomizationsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kResetShortcutCustomizations)},
    {"shimless-rma-os-update", flag_descriptions::kShimlessRMAOsUpdateName,
     flag_descriptions::kShimlessRMAOsUpdateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kShimlessRMAOsUpdate)},
    {"shimless-rma-hw-validation-skip",
     flag_descriptions::kShimlessRMAHardwareValidationSkipName,
     flag_descriptions::kShimlessRMAHardwareValidationSkipDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kShimlessRMAHardwareValidationSkip)},
    {"shimless-rma-dynamic-device-info-inputs",
     flag_descriptions::kShimlessRMADynamicDeviceInfoInputsName,
     flag_descriptions::kShimlessRMADynamicDeviceInfoInputsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kShimlessRMADynamicDeviceInfoInputs)},
    {"quick-share-v2", flag_descriptions::kQuickShareV2Name,
     flag_descriptions::kQuickShareV2Description, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kQuickShareV2)},
    {"enable-palm-suppression", flag_descriptions::kEnablePalmSuppressionName,
     flag_descriptions::kEnablePalmSuppressionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnablePalmSuppression)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-experimental-cookie-features",
     flag_descriptions::kEnableExperimentalCookieFeaturesName,
     flag_descriptions::kEnableExperimentalCookieFeaturesDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableExperimentalCookieFeaturesChoices)},

    {"canvas-2d-layers", flag_descriptions::kCanvas2DLayersName,
     flag_descriptions::kCanvas2DLayersDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableCanvas2DLayers)},

    {"web-machine-learning-neural-network",
     flag_descriptions::kWebMachineLearningNeuralNetworkName,
     flag_descriptions::kWebMachineLearningNeuralNetworkDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         webnn::mojom::features::kWebMachineLearningNeuralNetwork)},

    {"experimental-web-machine-learning-neural-network",
     flag_descriptions::kExperimentalWebMachineLearningNeuralNetworkName,
     flag_descriptions::kExperimentalWebMachineLearningNeuralNetworkDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         webnn::mojom::features::kExperimentalWebMachineLearningNeuralNetwork)},

#if BUILDFLAG(IS_MAC)
    {"webnn-coreml", flag_descriptions::kWebNNCoreMLName,
     flag_descriptions::kWebNNCoreMLDescription, kOsMac,
     FEATURE_VALUE_TYPE(webnn::mojom::features::kWebNNCoreML)},

    {"webnn-coreml-explicit-gpu-or-npu",
     flag_descriptions::kWebNNCoreMLExplicitGPUOrNPUName,
     flag_descriptions::kWebNNCoreMLExplicitGPUOrNPUDescription, kOsMac,
     FEATURE_VALUE_TYPE(webnn::mojom::features::kWebNNCoreMLExplicitGPUOrNPU)},
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN)
    {"webnn-directml", flag_descriptions::kWebNNDirectMLName,
     flag_descriptions::kWebNNDirectMLDescription, kOsWin,
     FEATURE_VALUE_TYPE(webnn::mojom::features::kWebNNDirectML)},

    {"webnn-onnxruntime", flag_descriptions::kWebNNOnnxRuntimeName,
     flag_descriptions::kWebNNOnnxRuntimeDescription, kOsWin,
     FEATURE_VALUE_TYPE(webnn::mojom::features::kWebNNOnnxRuntime)},
#endif  // BUILDFLAG(IS_WIN)

    {"permission-element",
     flag_descriptions::kPageEmbeddedPermissionControlName,
     flag_descriptions::kPageEmbeddedPermissionControlDescription,
     kOsMac | kOsWin | kOsCrOS | kOsAndroid | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kPermissionElement)},

    {"left-hand-side-activity-indicators",
     flag_descriptions::kLeftHandSideActivityIndicatorsName,
     flag_descriptions::kLeftHandSideActivityIndicatorsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         content_settings::features::kLeftHandSideActivityIndicators)},

#if !BUILDFLAG(IS_ANDROID)
    {"merchant-trust", flag_descriptions::kMerchantTrustName,
     flag_descriptions::kMerchantTrustDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(page_info::kMerchantTrust,
                                    kMerchantTrustVariations,
                                    "MerchantTrust")},
#endif  // !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"privacy-policy-insights", flag_descriptions::kPrivacyPolicyInsightsName,
     flag_descriptions::kPrivacyPolicyInsightsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(page_info::kPrivacyPolicyInsights)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-block-warnings",
     flag_descriptions::kCrosSystemLevelPermissionBlockedWarningsName,
     flag_descriptions::kCrosSystemLevelPermissionBlockedWarningsDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(content_settings::features::
                            kCrosSystemLevelPermissionBlockedWarnings)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"launcher-continue-section-with-recents",
     flag_descriptions::kLauncherContinueSectionWithRecentsName,
     flag_descriptions::kLauncherContinueSectionWithRecentsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kLauncherContinueSectionWithRecents)},
    {"launcher-item-suggest", flag_descriptions::kLauncherItemSuggestName,
     flag_descriptions::kLauncherItemSuggestDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::kLauncherItemSuggest,
                                    kLauncherItemSuggestVariations,
                                    "LauncherItemSuggest")},
    {"eol-incentive", flag_descriptions::kEolIncentiveName,
     flag_descriptions::kEolIncentiveDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kEolIncentive,
                                    kEolIncentiveVariations,
                                    "EolIncentive")},
    {"shelf-auto-hide-separation",
     flag_descriptions::kShelfAutoHideSeparationName,
     flag_descriptions::kShelfAutoHideSeparationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kShelfAutoHideSeparation)},
    {"launcher-keyword-extraction-scoring",
     flag_descriptions::kLauncherKeywordExtractionScoring,
     flag_descriptions::kLauncherKeywordExtractionScoringDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(search_features::kLauncherKeywordExtractionScoring)},
    {"launcher-search-control", flag_descriptions::kLauncherSearchControlName,
     flag_descriptions::kLauncherSearchControlDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kLauncherSearchControl)},
    {"launcher-nudge-session-reset",
     flag_descriptions::kLauncherNudgeSessionResetName,
     flag_descriptions::kLauncherNudgeSessionResetDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kLauncherNudgeSessionReset)},
    {"text-in-shelf", flag_descriptions::kTextInShelfName,
     flag_descriptions::kTextInShelfDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHomeButtonWithText)},
    {"launcher-local-image-search",
     flag_descriptions::kLauncherLocalImageSearchName,
     flag_descriptions::kLauncherLocalImageSearchDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(search_features::kLauncherImageSearch)},
    {"launcher-local-image-search-confidence",
     flag_descriptions::kLauncherLocalImageSearchConfidenceName,
     flag_descriptions::kLauncherLocalImageSearchConfidenceDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         search_features::kLauncherLocalImageSearchConfidence,
         kLauncherLocalImageSearchConfidenceVariations,
         "LauncherLocalImageSearchConfidence")},
    {"launcher-local-image-search-relevance",
     flag_descriptions::kLauncherLocalImageSearchRelevanceName,
     flag_descriptions::kLauncherLocalImageSearchRelevanceDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         search_features::kLauncherLocalImageSearchRelevance,
         kLauncherLocalImageSearchRelevanceVariations,
         "LauncherLocalImageSearchRelevance")},
    {"launcher-local-image-search-ocr",
     flag_descriptions::kLauncherLocalImageSearchOcrName,
     flag_descriptions::kLauncherLocalImageSearchOcrDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(search_features::kLauncherImageSearchOcr)},
    {"launcher-local-image-search-ica",
     flag_descriptions::kLauncherLocalImageSearchIcaName,
     flag_descriptions::kLauncherLocalImageSearchIcaDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(search_features::kLauncherImageSearchIca)},
    {"launcher-key-shortcut-in-best-match",
     flag_descriptions::kLauncherKeyShortcutInBestMatchName,
     flag_descriptions::kLauncherKeyShortcutInBestMatchDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(search_features::kLauncherKeyShortcutInBestMatch)},
    {"quick-app-access-test-ui", flag_descriptions::kQuickAppAccessTestUIName,
     flag_descriptions::kQuickAppAccessTestUIDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kQuickAppAccessTestUI)},

    {"mac-address-randomization",
     flag_descriptions::kMacAddressRandomizationName,
     flag_descriptions::kMacAddressRandomizationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kMacAddressRandomization)},

    {"tethering-experimental-functionality",
     flag_descriptions::kTetheringExperimentalFunctionalityName,
     flag_descriptions::kTetheringExperimentalFunctionalityDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kTetheringExperimentalFunctionality)},

    {"dynamic-search-update-animation",
     flag_descriptions::kDynamicSearchUpdateAnimationName,
     flag_descriptions::kDynamicSearchUpdateAnimationDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         app_list_features::kDynamicSearchUpdateAnimation,
         kDynamicSearchUpdateAnimationVariations,
         "LauncherDynamicAnimations")},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"enable-surface-control", flag_descriptions::kAndroidSurfaceControlName,
     flag_descriptions::kAndroidSurfaceControlDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAndroidSurfaceControl)},
    {"smart-suggestion-for-large-downloads",
     flag_descriptions::kSmartSuggestionForLargeDownloadsName,
     flag_descriptions::kSmartSuggestionForLargeDownloadsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(download::features::kSmartSuggestionForLargeDownloads)},

    {"quick-delete-android-survey",
     flag_descriptions::kQuickDeleteAndroidSurveyName,
     flag_descriptions::kQuickDeleteAndroidSurveyDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kQuickDeleteAndroidSurvey)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"pwa-update-dialog-for-icon",
     flag_descriptions::kPwaUpdateDialogForAppIconName,
     flag_descriptions::kPwaUpdateDialogForAppIconDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPwaUpdateDialogForIcon)},

    {"keyboard-lock-prompt", flag_descriptions::kKeyboardLockPromptName,
     flag_descriptions::kKeyboardLockPromptDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(permissions::features::kKeyboardLockPrompt)},

    {"press-and-hold-esc-to-exit-browser-fullscreen",
     flag_descriptions::kPressAndHoldEscToExitBrowserFullscreenName,
     flag_descriptions::kPressAndHoldEscToExitBrowserFullscreenDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(features::kPressAndHoldEscToExitBrowserFullscreen)},

#if BUILDFLAG(ENABLE_OOP_PRINTING)
    {"enable-oop-print-drivers", flag_descriptions::kEnableOopPrintDriversName,
     flag_descriptions::kEnableOopPrintDriversDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(printing::features::kEnableOopPrintDrivers)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"media-picker-adoption", flag_descriptions::kMediaPickerAdoptionStudyName,
     flag_descriptions::kMediaPickerAdoptionStudyDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         photo_picker::features::kAndroidMediaPickerAdoption,
         kPhotoPickerAdoptionStudyFeatureVariations,
         "MediaPickerAdoption")},
#endif  // BUILDFLAG(IS_ANDROID)

    {"privacy-sandbox-internals",
     flag_descriptions::kPrivacySandboxInternalsName,
     flag_descriptions::kPrivacySandboxInternalsDescription, kOsAll,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxInternalsDevUI)},

    {"align-surface-layer-impl-to-pixel-grid",
     flag_descriptions::kAlignSurfaceLayerImplToPixelGridName,
     flag_descriptions::kAlignSurfaceLayerImplToPixelGridDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kAlignSurfaceLayerImplToPixelGrid)},

#if !BUILDFLAG(IS_ANDROID)
    {"sct-auditing", flag_descriptions::kSCTAuditingName,
     flag_descriptions::kSCTAuditingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kSCTAuditing,
                                    kSCTAuditingVariations,
                                    "SCTAuditingVariations")},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"incognito-screenshot", flag_descriptions::kIncognitoScreenshotName,
     flag_descriptions::kIncognitoScreenshotDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kIncognitoScreenshot)},
#endif

    {"increment-local-surface-id-for-mainframe-same-doc-navigation",
     flag_descriptions::
         kIncrementLocalSurfaceIdForMainframeSameDocNavigationName,
     flag_descriptions::
         kIncrementLocalSurfaceIdForMainframeSameDocNavigationDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         blink::features::
             kIncrementLocalSurfaceIdForMainframeSameDocNavigation)},

    {"enable-speculation-rules-prerendering-target-hint",
     flag_descriptions::kSpeculationRulesPrerenderingTargetHintName,
     flag_descriptions::kSpeculationRulesPrerenderingTargetHintDescription,
     kOsAll, FEATURE_VALUE_TYPE(blink::features::kPrerender2InNewTab)},

    {"prerender-early-document-lifecycle-update",
     flag_descriptions::kPrerender2EarlyDocumentLifecycleUpdateName,
     flag_descriptions::kPrerender2EarlyDocumentLifecycleUpdateDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kPrerender2EarlyDocumentLifecycleUpdate)},

    {"trees-in-viz", flag_descriptions::kTreesInVizName,
     flag_descriptions::kTreesInVizDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTreesInViz)},

#if BUILDFLAG(IS_ANDROID)
    {"prerender2-new-tab-page-android",
     flag_descriptions::kPrerender2ForNewTabPageAndroidName,
     flag_descriptions::kPrerender2ForNewTabPageAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kNewTabPageAndroidTriggerForPrerender2)},
#endif

    {"omnibox-search-prefetch",
     flag_descriptions::kEnableOmniboxSearchPrefetchName,
     flag_descriptions::kEnableOmniboxSearchPrefetchDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(kSearchPrefetchServicePrefetching,
                                    kSearchPrefetchServicePrefetchingVariations,
                                    "SearchSuggestionPrefetch")},
    {"omnibox-search-client-prefetch",
     flag_descriptions::kEnableOmniboxClientSearchPrefetchName,
     flag_descriptions::kEnableOmniboxClientSearchPrefetchDescription, kOsAll,
     FEATURE_VALUE_TYPE(kSearchNavigationPrefetch)},

#if BUILDFLAG(IS_ANDROID)
    {"autofill-enable-offers-in-clank-keyboard-accessory",
     flag_descriptions::kAutofillEnableOffersInClankKeyboardAccessoryName,
     flag_descriptions::
         kAutofillEnableOffersInClankKeyboardAccessoryDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableOffersInClankKeyboardAccessory)},
#endif

#if BUILDFLAG(ENABLE_PDF)
    {"pdf-xfa-forms", flag_descriptions::kPdfXfaFormsName,
     flag_descriptions::kPdfXfaFormsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(chrome_pdf::features::kPdfXfaSupport)},
#endif  // BUILDFLAG(ENABLE_PDF)

    {"enable-managed-configuration-web-api",
     flag_descriptions::kEnableManagedConfigurationWebApiName,
     flag_descriptions::kEnableManagedConfigurationWebApiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(blink::features::kManagedConfiguration)},

    {"clear-cross-site-cross-browsing-context-group-window-name",
     flag_descriptions::kClearCrossSiteCrossBrowsingContextGroupWindowNameName,
     flag_descriptions::
         kClearCrossSiteCrossBrowsingContextGroupWindowNameDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         features::kClearCrossSiteCrossBrowsingContextGroupWindowName)},

#if BUILDFLAG(IS_CHROMEOS)
    {kWallpaperFastRefreshInternalName,
     flag_descriptions::kWallpaperFastRefreshName,
     flag_descriptions::kWallpaperFastRefreshDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWallpaperFastRefresh)},
    {kWallpaperGooglePhotosSharedAlbumsInternalName,
     flag_descriptions::kWallpaperGooglePhotosSharedAlbumsName,
     flag_descriptions::kWallpaperGooglePhotosSharedAlbumsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWallpaperGooglePhotosSharedAlbums)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
    {"enable-get-all-screens-media", flag_descriptions::kGetAllScreensMediaName,
     flag_descriptions::kGetAllScreensMediaDescription, kOsCrOS | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kGetAllScreensMedia)},
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-run-on-os-login", flag_descriptions::kRunOnOsLoginName,
     flag_descriptions::kRunOnOsLoginDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kDesktopPWAsRunOnOsLogin)},
    {"enable-prevent-close", flag_descriptions::kPreventCloseName,
     flag_descriptions::kPreventCloseDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kDesktopPWAsPreventClose)},

    {"enable-cloud-identifiers",
     flag_descriptions::kFileSystemAccessGetCloudIdentifiersName,
     flag_descriptions::kFileSystemAccessGetCloudIdentifiersDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(blink::features::kFileSystemAccessGetCloudIdentifiers)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-global-vaapi-lock", flag_descriptions::kGlobalVaapiLockName,
     flag_descriptions::kGlobalVaapiLockDescription, kOsCrOS | kOsLinux,
     FEATURE_VALUE_TYPE(media::kGlobalVaapiLock)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
    {
        "ui-debug-tools",
        flag_descriptions::kUIDebugToolsName,
        flag_descriptions::kUIDebugToolsDescription,
        kOsWin | kOsLinux | kOsMac,
        FEATURE_VALUE_TYPE(features::kUIDebugTools),
    },

#endif
#if BUILDFLAG(IS_ANDROID)
    {"optimization-guide-personalized-fetching",
     flag_descriptions::kOptimizationGuidePersonalizedFetchingName,
     flag_descriptions::kOptimizationGuidePersonalizedFetchingDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         optimization_guide::features::kOptimizationGuidePersonalizedFetching,
         kOptimizationGuidePersonalizedFetchingAllowPageInsightsVariations,
         "OptimizationGuidePersonalizedFetchingAllowPageInsights")},
    {"optimization-guide-push-notifications",
     flag_descriptions::kOptimizationGuidePushNotificationName,
     flag_descriptions::kOptimizationGuidePushNotificationDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(optimization_guide::features::kPushNotifications)},
#endif

    {"fedcm-alternative-identifiers",
     flag_descriptions::kFedCmAlternativeIdentifiersName,
     flag_descriptions::kFedCmAlternativeIdentifiersDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmAlternativeIdentifiers)},

    {"fedcm-autofill", flag_descriptions::kFedCmAutofillName,
     flag_descriptions::kFedCmAutofillDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmAutofill)},

    {"fedcm-cooldown-on-ignore", flag_descriptions::kFedCmCooldownOnIgnoreName,
     flag_descriptions::kFedCmCooldownOnIgnoreDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmCooldownOnIgnore)},

    {"fedcm-delegation", flag_descriptions::kFedCmDelegationName,
     flag_descriptions::kFedCmDelegationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmDelegation)},

    {"fedcm-idp-registration", flag_descriptions::kFedCmIdPRegistrationName,
     flag_descriptions::kFedCmIdPRegistrationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kFedCmIdPRegistration)},

    {"fedcm-iframe-origin", flag_descriptions::kFedCmIframeOriginName,
     flag_descriptions::kFedCmIframeOriginDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kFedCmIframeOrigin)},

    {"fedcm-lightweight-mode", flag_descriptions::kFedCmLightweightModeName,
     flag_descriptions::kFedCmLightweightModeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kFedCmLightweightMode)},

    {"fedcm-metrics-endpoint", flag_descriptions::kFedCmMetricsEndpointName,
     flag_descriptions::kFedCmMetricsEndpointDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmMetricsEndpoint)},

    {"fedcm-without-well-known-enforcement",
     flag_descriptions::kFedCmWithoutWellKnownEnforcementName,
     flag_descriptions::kFedCmWithoutWellKnownEnforcementDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFedCmWithoutWellKnownEnforcement)},

    {"fedcm-segmentation-platform",
     flag_descriptions::kFedCmSegmentationPlatformName,
     flag_descriptions::kFedCmSegmentationPlatformDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         segmentation_platform::features::kSegmentationPlatformFedCmUser)},

    {"web-identity-digital-credentials",
     flag_descriptions::kWebIdentityDigitalCredentialsName,
     flag_descriptions::kWebIdentityDigitalCredentialsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kWebIdentityDigitalCredentials,
         kWebIdentityDigitalIdentityCredentialVariations,
         "WebIdentityDigitalCredentials")},

    {"web-identity-digital-credentials-creation",
     flag_descriptions::kWebIdentityDigitalCredentialsCreationName,
     flag_descriptions::kWebIdentityDigitalCredentialsCreationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kWebIdentityDigitalCredentialsCreation)},

    {"sanitizer-api", flag_descriptions::kSanitizerApiName,
     flag_descriptions::kSanitizerApiDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kSanitizerAPI)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-input-event-logging",
     flag_descriptions::kEnableInputEventLoggingName,
     flag_descriptions::kEnableInputEventLoggingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableInputEventLogging)},
#endif

    {flag_descriptions::kEnableLensStandaloneFlagId,
     flag_descriptions::kEnableLensStandaloneName,
     flag_descriptions::kEnableLensStandaloneDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensStandalone)},

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay", flag_descriptions::kLensOverlayName,
     flag_descriptions::kLensOverlayDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(lens::features::kLensOverlay,
                                    kLensOverlayVariations,
                                    "LensOverlay")},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"enable-legacy-tabstate-deprecation",
     flag_descriptions::kLegacyTabStateDeprecationName,
     flag_descriptions::kLegacyTabStateDeprecationDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kLegacyTabStateDeprecation,
                                    kLegacyTabStateDeprecationVariations,
                                    "LegacyTabStateDeprecation")},

    {"biometric-reauth-password-filling",
     flag_descriptions::kBiometricReauthForPasswordFillingName,
     flag_descriptions::kBiometricReauthForPasswordFillingDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(password_manager::features::kBiometricTouchToFill)},
#endif

    {"bind-cookies-to-port", flag_descriptions::kBindCookiesToPortName,
     flag_descriptions::kBindCookiesToPortDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kEnablePortBoundCookies)},

    {"bind-cookies-to-scheme", flag_descriptions::kBindCookiesToSchemeName,
     flag_descriptions::kBindCookiesToSchemeDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kEnableSchemeBoundCookies)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-keyboard-backlight-control-in-settings",
     flag_descriptions::kEnableKeyboardBacklightControlInSettingsName,
     flag_descriptions::kEnableKeyboardBacklightControlInSettingsDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kEnableKeyboardBacklightControlInSettings)},
    {"enable-keyboard-rewriter-fix",
     flag_descriptions::kEnableKeyboardRewriterFixName,
     flag_descriptions::kEnableKeyboardRewriterFixDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableKeyboardRewriterFix)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"align-wakeups", flag_descriptions::kAlignWakeUpsName,
     flag_descriptions::kAlignWakeUpsDescription, kOsAll,
     FEATURE_VALUE_TYPE(base::kAlignWakeUps)},

#if BUILDFLAG(ENABLE_VALIDATING_COMMAND_DECODER)
    {"use-passthrough-command-decoder",
     flag_descriptions::kUsePassthroughCommandDecoderName,
     flag_descriptions::kUsePassthroughCommandDecoderDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDefaultPassthroughCommandDecoder)},
#endif  // BUILDFLAG(ENABLE_VALIDATING_COMMAND_DECODER)

#if BUILDFLAG(ENABLE_SWIFTSHADER)
    {"enable-unsafe-swiftshader",
     flag_descriptions::kEnableUnsafeSwiftShaderName,
     flag_descriptions::kEnableUnsafeSwiftShaderDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableUnsafeSwiftShader)},
#endif  // BUILDFLAG(ENABLE_SWIFTSHADER)

#if BUILDFLAG(IS_CHROMEOS)
    {"focus-follows-cursor", flag_descriptions::kFocusFollowsCursorName,
     flag_descriptions::kFocusFollowsCursorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(::features::kFocusFollowsCursor)},
    {"print-preview-cros-primary",
     flag_descriptions::kPrintPreviewCrosPrimaryName,
     flag_descriptions::kPrintPreviewCrosPrimaryDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kPrintPreviewCrosPrimary)},
#endif

    {"prerender2", flag_descriptions::kPrerender2Name,
     flag_descriptions::kPrerender2Description, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kPrerender2)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-phone-hub-call-notification",
     flag_descriptions::kPhoneHubCallNotificationName,
     flag_descriptions::kPhoneHubCallNotificationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPhoneHubCallNotification)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"test-third-party-cookie-phaseout",
     flag_descriptions::kTestThirdPartyCookiePhaseoutName,
     flag_descriptions::kTestThirdPartyCookiePhaseoutDescription, kOsAll,
     SINGLE_VALUE_TYPE(network::switches::kTestThirdPartyCookiePhaseout)},

    {"tpc-phase-out-facilitated-testing",
     flag_descriptions::kTPCPhaseOutFacilitatedTestingName,
     flag_descriptions::kTPCPhaseOutFacilitatedTestingDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         features::kCookieDeprecationFacilitatedTesting,
         kTPCPhaseOutFacilitatedTestingVariations,
         "TPCPhaseOutFacilitatedTesting")},

    {"tpcd-heuristics-grants", flag_descriptions::kTpcdHeuristicsGrantsName,
     flag_descriptions::kTpcdHeuristicsGrantsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         content_settings::features::kTpcdHeuristicsGrants,
         kTpcdHeuristicsGrantsVariations,
         "TpcdHeuristicsGrants")},

    {"tpcd-metadata-grants", flag_descriptions::kTpcdMetadataGrantsName,
     flag_descriptions::kTpcdMetadataGrantsDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kTpcdMetadataGrants)},

#if BUILDFLAG(IS_CHROMEOS)
    {kBackgroundListeningName, flag_descriptions::kBackgroundListeningName,
     flag_descriptions::kBackgroundListeningDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media::kBackgroundListening)},
    {kBorealisBigGlInternalName, flag_descriptions::kBorealisBigGlName,
     flag_descriptions::kBorealisBigGlDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisBigGl)},
    {kBorealisDGPUInternalName, flag_descriptions::kBorealisDGPUName,
     flag_descriptions::kBorealisDGPUDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisDGPU)},
    {kBorealisEnableUnsupportedHardwareInternalName,
     flag_descriptions::kBorealisEnableUnsupportedHardwareName,
     flag_descriptions::kBorealisEnableUnsupportedHardwareDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisEnableUnsupportedHardware)},
    {kBorealisForceBetaClientInternalName,
     flag_descriptions::kBorealisForceBetaClientName,
     flag_descriptions::kBorealisForceBetaClientDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisForceBetaClient)},
    {kBorealisForceDoubleScaleInternalName,
     flag_descriptions::kBorealisForceDoubleScaleName,
     flag_descriptions::kBorealisForceDoubleScaleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisForceDoubleScale)},
    {kBorealisLinuxModeInternalName, flag_descriptions::kBorealisLinuxModeName,
     flag_descriptions::kBorealisLinuxModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisLinuxMode)},
    {kBorealisPermittedInternalName, flag_descriptions::kBorealisPermittedName,
     flag_descriptions::kBorealisPermittedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisPermitted)},
    {kBorealisProvisionInternalName, flag_descriptions::kBorealisProvisionName,
     flag_descriptions::kBorealisProvisionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisProvision)},
    {kBorealisScaleClientByDPIInternalName,
     flag_descriptions::kBorealisScaleClientByDPIName,
     flag_descriptions::kBorealisScaleClientByDPIDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBorealisScaleClientByDPI)},
    {kBorealisZinkGlDriverInternalName,
     flag_descriptions::kBorealisZinkGlDriverName,
     flag_descriptions::kBorealisZinkGlDriverDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kBorealisZinkGlDriver,
                                    kBorealisZinkGlDriverVariations,
                                    "BorealisZinkGlDriver")},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"https-first-balanced-mode",
     flag_descriptions::kHttpsFirstBalancedModeName,
     flag_descriptions::kHttpsFirstBalancedModeDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstBalancedMode)},

    {"https-first-dialog-ui", flag_descriptions::kHttpsFirstDialogUiName,
     flag_descriptions::kHttpsFirstDialogUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kHttpsFirstDialogUi)},

    {"https-first-mode-v2-for-engaged-sites",
     flag_descriptions::kHttpsFirstModeV2ForEngagedSitesName,
     flag_descriptions::kHttpsFirstModeV2ForEngagedSitesDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeV2ForEngagedSites)},

    {"https-upgrades", flag_descriptions::kHttpsUpgradesName,
     flag_descriptions::kHttpsUpgradesDescription, kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsUpgrades)},

    {"https-first-mode-incognito",
     flag_descriptions::kHttpsFirstModeIncognitoName,
     flag_descriptions::kHttpsFirstModeIncognitoDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeIncognito)},

    {"https-first-mode-incognito-new-settings",
     flag_descriptions::kHttpsFirstModeIncognitoNewSettingsName,
     flag_descriptions::kHttpsFirstModeIncognitoNewSettingsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeIncognitoNewSettings)},

    {"https-first-mode-for-typically-secure-users",
     flag_descriptions::kHttpsFirstModeForTypicallySecureUsersName,
     flag_descriptions::kHttpsFirstModeForTypicallySecureUsersDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kHttpsFirstModeV2ForTypicallySecureUsers)},

    {"enable-drdc", flag_descriptions::kEnableDrDcName,
     flag_descriptions::kEnableDrDcDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kEnableDrDc)},

#if BUILDFLAG(IS_CHROMEOS)
    {"traffic-counters", flag_descriptions::kTrafficCountersEnabledName,
     flag_descriptions::kTrafficCountersEnabledDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kTrafficCountersEnabled)},

    {"traffic-counters-for-wifi-testing",
     flag_descriptions::kTrafficCountersForWiFiTestingName,
     flag_descriptions::kTrafficCountersForWiFiTestingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kTrafficCountersForWiFiTesting)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"experimental-omnibox-labs",
     flag_descriptions::kExperimentalOmniboxLabsName,
     flag_descriptions::kExperimentalOmniboxLabsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExperimentalOmniboxLabs)},

    {kExtensionAiDataInternalName,
     flag_descriptions::kExtensionAiDataCollectionName,
     flag_descriptions::kExtensionAiDataCollectionDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kExtensionAiDataCollection)},

    {"extensions-collapse-main-menu",
     flag_descriptions::kExtensionsCollapseMainMenuName,
     flag_descriptions::kExtensionsCollapseMainMenuDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kExtensionsCollapseMainMenu)},

    {"extensions-menu-access-control",
     flag_descriptions::kExtensionsMenuAccessControlName,
     flag_descriptions::kExtensionsMenuAccessControlDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExtensionsMenuAccessControl)},

    {"extensions-toolbar-zero-state-variation",
     flag_descriptions::kExtensionsToolbarZeroStateName,
     flag_descriptions::kExtensionsToolbarZeroStateDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kExtensionsToolbarZeroStateChoices)},

    {"iph-extensions-menu-feature",
     flag_descriptions::kIPHExtensionsMenuFeatureName,
     flag_descriptions::kIPHExtensionsMenuFeatureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(feature_engagement::kIPHExtensionsMenuFeature)},

    {"iph-extensions-request-access-button-feature",
     flag_descriptions::kIPHExtensionsRequestAccessButtonFeatureName,
     flag_descriptions::kIPHExtensionsRequestAccessButtonFeatureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         feature_engagement::kIPHExtensionsRequestAccessButtonFeature)},

    {"extension-manifest-v2-deprecation-warning",
     flag_descriptions::kExtensionManifestV2DeprecationWarningName,
     flag_descriptions::kExtensionManifestV2DeprecationWarningDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         extensions_features::kExtensionManifestV2DeprecationWarning)},

    {"extension-manifest-v2-deprecation-disabled",
     flag_descriptions::kExtensionManifestV2DeprecationDisabledName,
     flag_descriptions::kExtensionManifestV2DeprecationDisabledDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExtensionManifestV2Disabled)},

    {"extension-manifest-v2-deprecation-unsupported",
     flag_descriptions::kExtensionManifestV2DeprecationUnsupportedName,
     flag_descriptions::kExtensionManifestV2DeprecationUnsupportedDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kExtensionManifestV2Unsupported)},

#if BUILDFLAG(IS_WIN)
    {"launch-windows-native-hosts-directly",
     flag_descriptions::kLaunchWindowsNativeHostsDirectlyName,
     flag_descriptions::kLaunchWindowsNativeHostsDirectlyDescription, kOsWin,
     FEATURE_VALUE_TYPE(
         extensions_features::kLaunchWindowsNativeHostsDirectly)},
#endif  // IS_WIN
#endif  // ENABLE_EXTENSIONS

#if !BUILDFLAG(IS_ANDROID)
    {"captured-surface-control", flag_descriptions::kCapturedSurfaceControlName,
     flag_descriptions::kCapturedSurfaceControlDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kCapturedSurfaceControl)},

    {"region-capture-cross-tab", flag_descriptions::kCrossTabRegionCaptureName,
     flag_descriptions::kCrossTabRegionCaptureDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kRegionCaptureOfOtherTabs)},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"skia-graphite", flag_descriptions::kSkiaGraphiteName,
     flag_descriptions::kSkiaGraphiteDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kSkiaGraphite,
                                    kSkiaGraphiteVariations,
                                    "SkiaGraphite")},

    {"skia-graphite-precompilation",
     flag_descriptions::kSkiaGraphitePrecompilationName,
     flag_descriptions::kSkiaGraphitePrecompilationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kSkiaGraphitePrecompilation)},

    {"enable-tab-audio-muting", flag_descriptions::kTabAudioMutingName,
     flag_descriptions::kTabAudioMutingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(media::kEnableTabMuting)},

#if !BUILDFLAG(IS_ANDROID)
    {"customize-chrome-side-panel-extensions-card",
     flag_descriptions::kCustomizeChromeSidePanelExtensionsCardName,
     flag_descriptions::kCustomizeChromeSidePanelExtensionsCardDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kCustomizeChromeSidePanelExtensionsCard)},

    {"customize-chrome-wallpaper-search",
     flag_descriptions::kCustomizeChromeWallpaperSearchName,
     flag_descriptions::kCustomizeChromeWallpaperSearchDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kCustomizeChromeWallpaperSearch)},

    {"customize-chrome-wallpaper-search-button",
     flag_descriptions::kCustomizeChromeWallpaperSearchButtonName,
     flag_descriptions::kCustomizeChromeWallpaperSearchButtonDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(ntp_features::kCustomizeChromeWallpaperSearchButton)},

    {"customize-chrome-wallpaper-search-inspiration-card",
     flag_descriptions::kCustomizeChromeWallpaperSearchInspirationCardName,
     flag_descriptions::
         kCustomizeChromeWallpaperSearchInspirationCardDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         ntp_features::kCustomizeChromeWallpaperSearchInspirationCard)},

    {"wallpaper-search-settings-visibility",
     flag_descriptions::kWallpaperSearchSettingsVisibilityName,
     flag_descriptions::kWallpaperSearchSettingsVisibilityDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(optimization_guide::features::internal::
                            kWallpaperSearchSettingsVisibility)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-component-updater-test-request",
     flag_descriptions::kComponentUpdaterTestRequestName,
     flag_descriptions::kComponentUpdaterTestRequestDescription, kOsCrOS,
     SINGLE_VALUE_TYPE_AND_VALUE(switches::kComponentUpdater,
                                 component_updater::kSwitchTestRequestParam)},

    {kGrowthCampaignsTestTag,
     flag_descriptions::kCampaignsComponentUpdaterTestTagName,
     flag_descriptions::kCampaignsComponentUpdaterTestTagDescription, kOsCrOS,
     STRING_VALUE_TYPE(switches::kCampaignsTestTag, "")},

    {kGrowthCampaigns, flag_descriptions::kCampaignsOverrideName,
     flag_descriptions::kCampaignsOverrideDescription, kOsCrOS,
     STRING_VALUE_TYPE(ash::switches::kGrowthCampaigns, "")},

    {"demo-mode-test-tag",
     flag_descriptions::kDemoModeComponentUpdaterTestTagName,
     flag_descriptions::kDemoModeComponentUpdaterTestTagDescription, kOsCrOS,
     STRING_VALUE_TYPE(switches::kDemoModeTestTag, "")},
#endif

#if BUILDFLAG(IS_WIN)
    {"enable-delegated-compositing",
     flag_descriptions::kEnableDelegatedCompositingName,
     flag_descriptions::kEnableDelegatedCompositingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDelegatedCompositing)},
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_CHROMEOS)
    {"media-session-enter-picture-in-picture",
     flag_descriptions::kMediaSessionEnterPictureInPictureName,
     flag_descriptions::kMediaSessionEnterPictureInPictureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kMediaSessionEnterPictureInPicture)},

    {"auto-picture-in-picture-for-video-playback",
     flag_descriptions::kAutoPictureInPictureForVideoPlaybackName,
     flag_descriptions::kAutoPictureInPictureForVideoPlaybackDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kAutoPictureInPictureForVideoPlayback)},

    {"video-picture-in-picture-controls-update-2024",
     flag_descriptions::kVideoPictureInPictureControlsUpdate2024Name,
     flag_descriptions::kVideoPictureInPictureControlsUpdate2024Description,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kVideoPictureInPictureControlsUpdate2024)},

    {"document-picture-in-picture-animate-resize",
     flag_descriptions::kDocumentPictureInPictureAnimateResizeName,
     flag_descriptions::kDocumentPictureInPictureAnimateResizeDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kDocumentPictureInPictureAnimateResize)},

    {"browser-initiated-automatic-picture-in-picture",
     flag_descriptions::kBrowserInitiatedAutomaticPictureInPictureName,
     flag_descriptions::kBrowserInitiatedAutomaticPictureInPictureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         blink::features::kBrowserInitiatedAutomaticPictureInPicture)},

    {"picture-in-picture-show-window-animation",
     flag_descriptions::kPictureInPictureShowWindowAnimationName,
     flag_descriptions::kPictureInPictureShowWindowAnimationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(media::kPictureInPictureShowWindowAnimation)},

#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_CHROMEOS)

    {"dse-preload2", flag_descriptions::kDsePreload2Name,
     flag_descriptions::kDsePreload2Description, kOsAll,
     FEATURE_VALUE_TYPE(features::kDsePreload2)},
    {"dse-preload2-on-press", flag_descriptions::kDsePreload2OnPressName,
     flag_descriptions::kDsePreload2OnPressDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDsePreload2OnPress)},

    {"http-cache-no-vary-search", flag_descriptions::kHttpCacheNoVarySearchName,
     flag_descriptions::kHttpCacheNoVarySearchDescription, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kHttpCacheNoVarySearch)},

#if !BUILDFLAG(IS_ANDROID)
    {"audio-ducking", flag_descriptions::kAudioDuckingName,
     flag_descriptions::kAudioDuckingDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(media::kAudioDucking,
                                    kAudioDuckingAttenuationVariations,
                                    "AudioDucking")},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"enable-commerce-developer", flag_descriptions::kCommerceDeveloperName,
     flag_descriptions::kCommerceDeveloperDescription, kOsAll,
     FEATURE_VALUE_TYPE(commerce::kCommerceDeveloper)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-libinput-to-handle-touchpad",
     flag_descriptions::kEnableLibinputToHandleTouchpadName,
     flag_descriptions::kEnableLibinputToHandleTouchpadDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kLibinputHandleTouchpad)},

    {"enable-desks-templates", flag_descriptions::kDesksTemplatesName,
     flag_descriptions::kDesksTemplatesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDesksTemplates)},

    {"vc-background-replace", flag_descriptions::kVcBackgroundReplaceName,
     flag_descriptions::kVcBackgroundReplaceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVcBackgroundReplace)},

    {"vc-relighting-inference-backend",
     flag_descriptions::kVcRelightingInferenceBackendName,
     flag_descriptions::kVcRelightingInferenceBackendDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ash::features::kVcRelightingInferenceBackend,
         kVcRelightingInferenceBackendVariations,
         "VcRelightingInferenceBackend")},
    {"vc-retouch-inference-backend",
     flag_descriptions::kVcRetouchInferenceBackendName,
     flag_descriptions::kVcRetouchInferenceBackendDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kVcRetouchInferenceBackend,
                                    kVcRetouchInferenceBackendVariations,
                                    "VcRetouchInferenceBackend")},
    {"vc-segmentation-model", flag_descriptions::kVcSegmentationModelName,
     flag_descriptions::kVcSegmentationModelDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kVcSegmentationModel,
                                    kVcSegmentationModelVariations,
                                    "VCSegmentationModel")},
    {"vc-segmentation-inference-backend",
     flag_descriptions::kVcSegmentationInferenceBackendName,
     flag_descriptions::kVcSegmentationInferenceBackendDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ash::features::kVcSegmentationInferenceBackend,
         kVcSegmentationInferenceBackendVariations,
         "VcSegmentationInferenceBackend")},
    {"vc-light-intensity", flag_descriptions::kVcLightIntensityName,
     flag_descriptions::kVcLightIntensityDescription, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(ash::features::kVcLightIntensity,
                                    kVcLightIntensityVariations,
                                    "VCLightIntensity")},
    {"vc-web-api", flag_descriptions::kVcWebApiName,
     flag_descriptions::kVcWebApiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVcWebApi)},
    {kVcTrayMicIndicatorInternalName,
     flag_descriptions::kVcTrayMicIndicatorName,
     flag_descriptions::kVcTrayMicIndicatorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVcTrayMicIndicator)},
    {kVcTrayTitleHeaderInternalName, flag_descriptions::kVcTrayTitleHeaderName,
     flag_descriptions::kVcTrayTitleHeaderDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kVcTrayTitleHeader)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"animated-image-drag-shadow",
     flag_descriptions::kAnimatedImageDragShadowName,
     flag_descriptions::kAnimatedImageDragShadowDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAnimatedImageDragShadow)},
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"main-node-annotations", flag_descriptions::kMainNodeAnnotationsName,
     flag_descriptions::kMainNodeAnnotationsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMainNodeAnnotations)},
#endif

    {"origin-agent-cluster-default",
     flag_descriptions::kOriginAgentClusterDefaultName,
     flag_descriptions::kOriginAgentClusterDefaultDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kOriginAgentClusterDefaultEnabled)},

    {"origin-keyed-processes-by-default",
     flag_descriptions::kOriginKeyedProcessesByDefaultName,
     flag_descriptions::kOriginKeyedProcessesByDefaultDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kOriginKeyedProcessesByDefault)},

    {"collaboration-messaging", flag_descriptions::kCollaborationMessagingName,
     flag_descriptions::kCollaborationMessagingDescription, kOsAll,
     FEATURE_VALUE_TYPE(collaboration::features::kCollaborationMessaging)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-fake-keyboard-heuristic",
     flag_descriptions::kEnableFakeKeyboardHeuristicName,
     flag_descriptions::kEnableFakeKeyboardHeuristicDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableFakeKeyboardHeuristic)},
    {"enable-fake-mouse-heuristic",
     flag_descriptions::kEnableFakeMouseHeuristicName,
     flag_descriptions::kEnableFakeMouseHeuristicDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableFakeMouseHeuristic)},
#endif  // BUILDFLAG(IS_CHROMEOS)
    {"enable-isolated-sandboxed-iframes",
     flag_descriptions::kIsolatedSandboxedIframesName,
     flag_descriptions::kIsolatedSandboxedIframesDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         blink::features::kIsolateSandboxedIframes,
         kIsolateSandboxedIframesGroupingVariations,
         "IsolateSandboxedIframes" /* trial name */)},

    {"reduce-accept-language", flag_descriptions::kReduceAcceptLanguageName,
     flag_descriptions::kReduceAcceptLanguageDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kReduceAcceptLanguage)},

    {"reduce-accept-language-http",
     flag_descriptions::kReduceAcceptLanguageHTTPName,
     flag_descriptions::kReduceAcceptLanguageHTTPDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kReduceAcceptLanguageHTTP)},

    {"reduce-transfer-size-updated-ipc",
     flag_descriptions::kReduceTransferSizeUpdatedIPCName,
     flag_descriptions::kReduceTransferSizeUpdatedIPCDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kReduceTransferSizeUpdatedIPC)},

#if BUILDFLAG(IS_LINUX)
    {"reduce-user-agent-data-linux-platform-version",
     flag_descriptions::kReduceUserAgentDataLinuxPlatformVersionName,
     flag_descriptions::kReduceUserAgentDataLinuxPlatformVersionDescription,
     kOsLinux,
     FEATURE_VALUE_TYPE(
         blink::features::kReduceUserAgentDataLinuxPlatformVersion)},
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-variable-refresh-rate",
     flag_descriptions::kEnableVariableRefreshRateName,
     flag_descriptions::kEnableVariableRefreshRateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnableVariableRefreshRate)},

    {"enable-projector-app-debug", flag_descriptions::kProjectorAppDebugName,
     flag_descriptions::kProjectorAppDebugDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kProjectorAppDebug)},

    {kProjectorServerSideSpeechRecognition,
     flag_descriptions::kProjectorServerSideSpeechRecognitionName,
     flag_descriptions::kProjectorServerSideSpeechRecognitionDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kInternalServerSideSpeechRecognition)},

    {"enable-projector-server-side-usm",
     flag_descriptions::kProjectorServerSideUsmName,
     flag_descriptions::kProjectorServerSideUsmDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kProjectorUseUSMForS3)},

    {"projector-use-dvs-playback-endpoint",
     flag_descriptions::kProjectorUseDVSPlaybackEndpointName,
     flag_descriptions::kProjectorUseDVSPlaybackEndpointDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kProjectorUseDVSPlaybackEndpoint)},

    {"enable-annotator-mode", flag_descriptions::kAnnotatorModeName,
     flag_descriptions::kAnnotatorModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAnnotatorMode)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"omit-cors-client-cert", flag_descriptions::kOmitCorsClientCertName,
     flag_descriptions::kOmitCorsClientCertDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kOmitCorsClientCert)},

#if BUILDFLAG(IS_CHROMEOS)
    {"always-enable-hdcp", flag_descriptions::kAlwaysEnableHdcpName,
     flag_descriptions::kAlwaysEnableHdcpDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kAlwaysEnableHdcpChoices)},
    {"enable-touchpads-in-diagnostics-app",
     flag_descriptions::kEnableTouchpadsInDiagnosticsAppName,
     flag_descriptions::kEnableTouchpadsInDiagnosticsAppDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableTouchpadsInDiagnosticsApp)},
    {"enable-touchscreens-in-diagnostics-app",
     flag_descriptions::kEnableTouchscreensInDiagnosticsAppName,
     flag_descriptions::kEnableTouchscreensInDiagnosticsAppDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableTouchscreensInDiagnosticsApp)},
    {"enable-external-keyboards-in-diagnostics-app",
     flag_descriptions::kEnableExternalKeyboardsInDiagnosticsAppName,
     flag_descriptions::kEnableExternalKeyboardsInDiagnosticsAppDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableExternalKeyboardsInDiagnostics)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"pwa-restore-backend", flag_descriptions::kPwaRestoreBackendName,
     flag_descriptions::kPwaRestoreBackendDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(syncer::kWebApkBackupAndRestoreBackend)},

    {"pwa-restore-ui", flag_descriptions::kPwaRestoreUiName,
     flag_descriptions::kPwaRestoreUiDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kPwaRestoreUi)},

    {"pwa-restore-ui-at-startup", flag_descriptions::kPwaRestoreUiAtStartupName,
     flag_descriptions::kPwaRestoreUiAtStartupDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kPwaRestoreUiAtStartup)},
#endif  // BUILDFLAG(IS_ANDROID)
    {"autofill-enable-ranking-formula-address-profiles",
     flag_descriptions::kAutofillEnableRankingFormulaAddressProfilesName,
     flag_descriptions::kAutofillEnableRankingFormulaAddressProfilesDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableRankingFormulaAddressProfiles)},

    {"autofill-enable-ranking-formula-credit-cards",
     flag_descriptions::kAutofillEnableRankingFormulaCreditCardsName,
     flag_descriptions::kAutofillEnableRankingFormulaCreditCardsDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableRankingFormulaCreditCards)},

    {"safe-browsing-local-lists-use-sbv5",
     flag_descriptions::kSafeBrowsingLocalListsUseSBv5Name,
     flag_descriptions::kSafeBrowsingLocalListsUseSBv5Description, kOsAll,
     FEATURE_VALUE_TYPE(safe_browsing::kLocalListsUseSBv5)},

    {"safety-check-unused-site-permissions",
     flag_descriptions::kSafetyCheckUnusedSitePermissionsName,
     flag_descriptions::kSafetyCheckUnusedSitePermissionsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         content_settings::features::kSafetyCheckUnusedSitePermissions,
         kSafetyCheckUnusedSitePermissionsVariations,
         "SafetyCheckUnusedSitePermissions")},

    {"safety-hub", flag_descriptions::kSafetyHubName,
     flag_descriptions::kSafetyHubDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kSafetyHub,
                                    kSafetyHubVariations,
                                    "SafetyHub")},

    {"permission-site-settings-radio-button",
     flag_descriptions::kPermissionSiteSettingsRadioButtonName,
     flag_descriptions::kPermissionSiteSettingsRadioButtonDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         permissions::features::kPermissionSiteSettingsRadioButton)},

#if BUILDFLAG(IS_ANDROID)
    {"safety-hub-magic-stack", flag_descriptions::kSafetyHubMagicStackName,
     flag_descriptions::kSafetyHubMagicStackDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSafetyHubMagicStack)},

    {"safety-hub-followup", flag_descriptions::kSafetyHubFollowupName,
     flag_descriptions::kSafetyHubFollowupDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSafetyHubFollowup)},

    {"safety-hub-android-survey",
     flag_descriptions::kSafetyHubAndroidSurveyName,
     flag_descriptions::kSafetyHubAndroidSurveyDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSafetyHubAndroidSurvey)},

    {"safety-hub-android-survey-v2",
     flag_descriptions::kSafetyHubAndroidSurveyV2Name,
     flag_descriptions::kSafetyHubAndroidSurveyV2Description, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSafetyHubAndroidSurveyV2)},

    {"safety-hub-weak-reused-passwords",
     flag_descriptions::kSafetyHubWeakAndReusedPasswordsName,
     flag_descriptions::kSafetyHubWeakAndReusedPasswordsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSafetyHubWeakAndReusedPasswords)},

    {"safety-hub-local-passwords-module",
     flag_descriptions::kSafetyHubLocalPasswordsModuleName,
     flag_descriptions::kSafetyHubLocalPasswordsModuleDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSafetyHubLocalPasswordsModule)},

    {"safety-hub-unified-passwords-module",
     flag_descriptions::kSafetyHubUnifiedPasswordsModuleName,
     flag_descriptions::kSafetyHubUnifiedPasswordsModuleDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kSafetyHubUnifiedPasswordsModuleChoices)},
#else
    {"safety-hub-one-off-survey",
     flag_descriptions::kSafetyHubHaTSOneOffSurveyName,
     flag_descriptions::kSafetyHubHaTSOneOffSurveyDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSafetyHubHaTSOneOffSurvey)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
    {"enable-web-bluetooth-confirm-pairing-support",
     flag_descriptions::kWebBluetoothConfirmPairingSupportName,
     flag_descriptions::kWebBluetoothConfirmPairingSupportDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(device::features::kWebBluetoothConfirmPairingSupport)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

    {"enable-perfetto-system-tracing",
     flag_descriptions::kEnablePerfettoSystemTracingName,
     flag_descriptions::kEnablePerfettoSystemTracingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kEnablePerfettoSystemTracing)},

#if BUILDFLAG(IS_ANDROID)
    {"browsing-data-model-clank", flag_descriptions::kBrowsingDataModelName,
     flag_descriptions::kBrowsingDataModelDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(browsing_data::features::kBrowsingDataModel)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-seamless-refresh-rate-switching",
     flag_descriptions::kEnableSeamlessRefreshRateSwitchingName,
     flag_descriptions::kEnableSeamlessRefreshRateSwitchingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSeamlessRefreshRateSwitching)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"click-to-call", flag_descriptions::kClickToCallName,
     flag_descriptions::kClickToCallDescription, kOsAll,
     FEATURE_VALUE_TYPE(kClickToCall)},

    {"css-gamut-mapping", flag_descriptions::kCssGamutMappingName,
     flag_descriptions::kCssGamutMappingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kBakedGamutMapping)},

    {"clipboard-maximum-age", flag_descriptions::kClipboardMaximumAgeName,
     flag_descriptions::kClipboardMaximumAgeDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(kClipboardMaximumAge,
                                    kClipboardMaximumAgeVariations,
                                    "ClipboardMaximumAge")},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-media-dynamic-cgroup", flag_descriptions::kMediaDynamicCgroupName,
     flag_descriptions::kMediaDynamicCgroupDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootMediaDynamicCgroup")},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"background-resource-fetch",
     flag_descriptions::kBackgroundResourceFetchName,
     flag_descriptions::kBackgroundResourceFetchDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kBackgroundResourceFetch)},

    {"renderer-side-content-decoding",
     flag_descriptions::kRendererSideContentDecodingName,
     flag_descriptions::kRendererSideContentDecodingDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kRendererSideContentDecoding)},

    {"device-bound-session-access-observer-shared-remote",
     flag_descriptions::kDeviceBoundSessionAccessObserverSharedRemoteName,
     flag_descriptions::
         kDeviceBoundSessionAccessObserverSharedRemoteDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         network::features::kDeviceBoundSessionAccessObserverSharedRemote)},

#if BUILDFLAG(IS_ANDROID)
    {"external-navigation-debug-logs",
     flag_descriptions::kExternalNavigationDebugLogsName,
     flag_descriptions::kExternalNavigationDebugLogsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(external_intents::kExternalNavigationDebugLogs)},
#endif

    {"webui-omnibox-popup", flag_descriptions::kWebUIOmniboxPopupName,
     flag_descriptions::kWebUIOmniboxPopupDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kWebUIOmniboxPopup)},

#if BUILDFLAG(IS_CHROMEOS)
    {"arc-vm-memory-size", flag_descriptions::kArcVmMemorySizeName,
     flag_descriptions::kArcVmMemorySizeDesc, kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(arc::kVmMemorySize,
                                    kArcVmMemorySizeVariations,
                                    "VmMemorySize")},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"tab-group-entry-points-android",
     flag_descriptions::kTabGroupEntryPointsAndroidName,
     flag_descriptions::kTabGroupEntryPointsAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabGroupEntryPointsAndroid)},

    {"tab-group-parity-bottom-sheet-android",
     flag_descriptions::kTabGroupParityBottomSheetAndroidName,
     flag_descriptions::kTabGroupParityBottomSheetAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabGroupParityBottomSheetAndroid)},

    {"tab-strip-context-menu-android",
     flag_descriptions::kTabStripContextMenuAndroidName,
     flag_descriptions::kTabStripContextMenuAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabStripContextMenuAndroid)},

    {"tab-strip-density-change-android",
     flag_descriptions::kTabStripDensityChangeAndroidName,
     flag_descriptions::kTabStripDensityChangeAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabStripDensityChangeAndroid)},

    {"tab-strip-group-drag-drop-android",
     flag_descriptions::kTabStripGroupDragDropAndroidName,
     flag_descriptions::kTabStripGroupDragDropAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabStripGroupDragDropAndroid)},

    {"tab-strip-incognito-migration",
     flag_descriptions::kTabStripIncognitoMigrationName,
     flag_descriptions::kTabStripIncognitoMigrationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabStripIncognitoMigration)},

    {"tab-strip-layout-optimization",
     flag_descriptions::kTabStripLayoutOptimizationName,
     flag_descriptions::kTabStripLayoutOptimizationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabStripLayoutOptimization)},

    {"tab-strip-transition-in-desktop-window",
     flag_descriptions::kTabStripTransitionInDesktopWindowName,
     flag_descriptions::kTabStripTransitionInDesktopWindowDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabStripTransitionInDesktopWindow)},

    {"tab-switcher-group-suggestions-android",
     flag_descriptions::kTabSwitcherGroupSuggestionsAndroidName,
     flag_descriptions::kTabSwitcherGroupSuggestionsAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabSwitcherGroupSuggestionsAndroid)},

    {"tab-switcher-group-suggestions-test-mode-android",
     flag_descriptions::kTabSwitcherGroupSuggestionsTestModeAndroidName,
     flag_descriptions::kTabSwitcherGroupSuggestionsTestModeAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kTabSwitcherGroupSuggestionsTestModeAndroid)},
#endif

    {"group-promo-prototype", flag_descriptions::kGroupPromoPrototypeName,
     flag_descriptions::kGroupPromoPrototypeDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         visited_url_ranking::features::kGroupSuggestionService,
         kGroupSuggestionVariations,
         "GroupPromoPrototype")},

    {"use-dmsaa-for-tiles", flag_descriptions::kUseDMSAAForTilesName,
     flag_descriptions::kUseDMSAAForTilesDescription, kOsAll,
     FEATURE_VALUE_TYPE(::features::kUseDMSAAForTiles)},

#if BUILDFLAG(IS_CHROMEOS)
    {"app-launch-shortcut", flag_descriptions::kAppLaunchShortcut,
     flag_descriptions::kAppLaunchShortcutDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kAppLaunchShortcut)},
    {"enable-holding-space-suggestions",
     flag_descriptions::kHoldingSpaceSuggestionsName,
     flag_descriptions::kHoldingSpaceSuggestionsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kHoldingSpaceSuggestions)},
    {"enable-welcome-experience", flag_descriptions::kWelcomeExperienceName,
     flag_descriptions::kWelcomeExperienceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWelcomeExperience)},
    {"enable-welcome-experience-test-unsupported-devices",
     flag_descriptions::kWelcomeExperienceTestUnsupportedDevicesName,
     flag_descriptions::kWelcomeExperienceTestUnsupportedDevicesDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(
         ash::features::kWelcomeExperienceTestUnsupportedDevices)},
    {"enable-welcome-tour", flag_descriptions::kWelcomeTourName,
     flag_descriptions::kWelcomeTourDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWelcomeTour)},
    {"enable-welcome-tour-force-user-eligibility",
     flag_descriptions::kWelcomeTourForceUserEligibilityName,
     flag_descriptions::kWelcomeTourForceUserEligibilityDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWelcomeTourForceUserEligibility)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"google-one-offer-files-banner",
     flag_descriptions::kGoogleOneOfferFilesBannerName,
     flag_descriptions::kGoogleOneOfferFilesBannerDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kGoogleOneOfferFilesBanner)},
#endif

    {"sync-autofill-wallet-credential-data",
     flag_descriptions::kSyncAutofillWalletCredentialDataName,
     flag_descriptions::kSyncAutofillWalletCredentialDataDescription, kOsAll,
     FEATURE_VALUE_TYPE(syncer::kSyncAutofillWalletCredentialData)},

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-labs-window-cycle-shortcut",
     flag_descriptions::kSameAppWindowCycleName,
     flag_descriptions::kSameAppWindowCycleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSameAppWindowCycle)},
    {"promise-icons", flag_descriptions::kPromiseIconsName,
     flag_descriptions::kPromiseIconsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPromiseIcons)},
    {"printing-ppd-channel", flag_descriptions::kPrintingPpdChannelName,
     flag_descriptions::kPrintingPpdChannelDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kPrintingPpdChannelChoices)},
    {"arc-idle-manager", flag_descriptions::kArcIdleManagerName,
     flag_descriptions::kArcIdleManagerDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kEnableArcIdleManager)},
#endif

    {"power-bookmark-backend", flag_descriptions::kPowerBookmarkBackendName,
     flag_descriptions::kPowerBookmarkBackendDescription, kOsAll,
     FEATURE_VALUE_TYPE(power_bookmarks::kPowerBookmarkBackend)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-eol-notification-reset-dismissed-prefs",
     flag_descriptions::kEolResetDismissedPrefsName,
     flag_descriptions::kEolResetDismissedPrefsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(ash::switches::kEolResetDismissedPrefs)},
#endif

#if !BUILDFLAG(IS_ANDROID)
    {"enable-preferences-account-storage",
     flag_descriptions::kEnablePreferencesAccountStorageName,
     flag_descriptions::kEnablePreferencesAccountStorageDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(switches::kEnablePreferencesAccountStorage)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"render-arc-notifications-by-chrome",
     flag_descriptions::kRenderArcNotificationsByChromeName,
     flag_descriptions::kRenderArcNotificationsByChromeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kRenderArcNotificationsByChrome)},
#endif

    {"enable-compression-dictionary-transport",
     flag_descriptions::kCompressionDictionaryTransportName,
     flag_descriptions::kCompressionDictionaryTransportDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kCompressionDictionaryTransport)},

    {"enable-compression-dictionary-transport-backend",
     flag_descriptions::kCompressionDictionaryTransportBackendName,
     flag_descriptions::kCompressionDictionaryTransportBackendDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         network::features::kCompressionDictionaryTransportBackend)},

    {"enable-compression-dictionary-transport-allow-http1",
     flag_descriptions::kCompressionDictionaryTransportOverHttp1Name,
     flag_descriptions::kCompressionDictionaryTransportOverHttp1Description,
     kOsAll,
     FEATURE_VALUE_TYPE(
         net::features::kCompressionDictionaryTransportOverHttp1)},

    {"enable-compression-dictionary-transport-allow-http2",
     flag_descriptions::kCompressionDictionaryTransportOverHttp2Name,
     flag_descriptions::kCompressionDictionaryTransportOverHttp2Description,
     kOsAll,
     FEATURE_VALUE_TYPE(
         net::features::kCompressionDictionaryTransportOverHttp2)},

    {"enable-compression-dictionary-transport-require-known-root-cert",
     flag_descriptions::kCompressionDictionaryTransportRequireKnownRootCertName,
     flag_descriptions::
         kCompressionDictionaryTransportRequireKnownRootCertDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         net::features::kCompressionDictionaryTransportRequireKnownRootCert)},

    {"enable-compute-pressure-rate-obfuscation-mitigation",
     flag_descriptions::kComputePressureRateObfuscationMitigationName,
     flag_descriptions::kComputePressureRateObfuscationMitigationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kComputePressureRateObfuscationMitigation)},

    {"enable-container-type-no-layout-containment",
     flag_descriptions::kContainerTypeNoLayoutContainmentName,
     flag_descriptions::kContainerTypeNoLayoutContainmentDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kContainerTypeNoLayoutContainment)},

    {"enable-compute-pressure-break-calibration-mitigation",
     flag_descriptions::kComputePressureBreakCalibrationMitigationName,
     flag_descriptions::kComputePressureBreakCalibrationMitigationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kComputePressureBreakCalibrationMitigation)},

#if BUILDFLAG(IS_ANDROID)
    {"deprecated-external-picker-function",
     flag_descriptions::kDeprecatedExternalPickerFunctionName,
     flag_descriptions::kDeprecatedExternalPickerFunctionDescription,
     kOsAndroid, FEATURE_VALUE_TYPE(ui::kDeprecatedExternalPickerFunction)},
    {"android-keyboard-a11y", flag_descriptions::kAndroidKeyboardA11yName,
     flag_descriptions::kAndroidKeyboardA11yDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidKeyboardA11y)},
    {"android-meta-click-history-navigation",
     flag_descriptions::kAndroidMetaClickHistoryNavigationName,
     flag_descriptions::kAndroidMetaClickHistoryNavigationDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidMetaClickHistoryNavigation)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"android-native-pages-in-new-tab",
     flag_descriptions::kAndroidNativePagesInNewTabName,
     flag_descriptions::kAndroidNativePagesInNewTabDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidNativePagesInNewTab)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"android-progress-bar-visual-update",
     flag_descriptions::kAndroidProgressBarVisualUpdateName,
     flag_descriptions::kAndroidProgressBarVisualUpdateDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidProgressBarVisualUpdate)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-missive-storage-config", flag_descriptions::kMissiveStorageName,
     flag_descriptions::kMissiveStorageDescription, kOsCrOS,
     PLATFORM_FEATURE_WITH_PARAMS_VALUE_TYPE(
         "CrOSLateBootMissiveStorage",
         kCrOSLateBootMissiveStorageDefaultVariations,
         "CrOSLateBootMissiveStorage")},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"cast-mirroring-target-playout-delay",
     flag_descriptions::kCastMirroringTargetPlayoutDelayName,
     flag_descriptions::kCastMirroringTargetPlayoutDelayDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kCastMirroringTargetPlayoutDelayChoices)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"separate-web-app-shortcut-badge-icon",
     flag_descriptions::kSeparateWebAppShortcutBadgeIconName,
     flag_descriptions::kSeparateWebAppShortcutBadgeIconDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSeparateWebAppShortcutBadgeIcon)},
    {"enable-audio-focus-enforcement",
     flag_descriptions::kEnableAudioFocusEnforcementName,
     flag_descriptions::kEnableAudioFocusEnforcementDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(media_session::features::kAudioFocusEnforcement)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-process-per-site-up-to-main-frame-threshold",
     flag_descriptions::kEnableProcessPerSiteUpToMainFrameThresholdName,
     flag_descriptions::kEnableProcessPerSiteUpToMainFrameThresholdDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kProcessPerSiteUpToMainFrameThreshold)},

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
    {"camera-mic-effects", flag_descriptions::kCameraMicEffectsName,
     flag_descriptions::kCameraMicEffectsDescription,
     static_cast<unsigned short>(kOsMac | kOsWin | kOsLinux),
     FEATURE_VALUE_TYPE(media::kCameraMicEffects)},

    {"camera-mic-preview", flag_descriptions::kCameraMicPreviewName,
     flag_descriptions::kCameraMicPreviewDescription,
     static_cast<unsigned short>(kOsMac | kOsWin | kOsLinux),
     FEATURE_VALUE_TYPE(blink::features::kCameraMicPreview)},

#if !BUILDFLAG(IS_ANDROID)
    {"get-display-media-confers-activation",
     flag_descriptions::kGetDisplayMediaConfersActivationName,
     flag_descriptions::kGetDisplayMediaConfersActivationDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(media::kGetDisplayMediaConfersActivation)},
#endif

    {"get-user-media-deferred-device-settings-selection",
     flag_descriptions::kGetUserMediaDeferredDeviceSettingsSelectionName,
     flag_descriptions::kGetUserMediaDeferredDeviceSettingsSelectionDescription,
     static_cast<unsigned short>(kOsMac | kOsWin | kOsLinux),
     FEATURE_VALUE_TYPE(
         blink::features::kGetUserMediaDeferredDeviceSettingsSelection)},
#endif

    {"render-document", flag_descriptions::kRenderDocumentName,
     flag_descriptions::kRenderDocumentDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kRenderDocument,
                                    kRenderDocumentVariations,
                                    "RenderDocument")},

    {"default-site-instance-groups",
     flag_descriptions::kDefaultSiteInstanceGroupsName,
     flag_descriptions::kDefaultSiteInstanceGroupsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDefaultSiteInstanceGroups)},

#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"cws-info-fast-check", flag_descriptions::kCWSInfoFastCheckName,
     flag_descriptions::kCWSInfoFastCheckDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions::kCWSInfoFastCheck)},

    {"extension-disable-unsupported-developer-mode-extensions",
     flag_descriptions::kExtensionDisableUnsupportedDeveloperName,
     flag_descriptions::kExtensionDisableUnsupportedDeveloperDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         extensions_features::kExtensionDisableUnsupportedDeveloper)},

    {"extension-telemetry-for-enterprise",
     flag_descriptions::kExtensionTelemetryForEnterpriseName,
     flag_descriptions::kExtensionTelemetryForEnterpriseDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         safe_browsing::kExtensionTelemetryForEnterprise,
         kExtensionTelemetryEnterpriseReportingIntervalSecondsVariations,
         "EnterpriseReportingIntervalSeconds")},
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

    {"autofill-enable-cvc-storage-and-filling",
     flag_descriptions::kAutofillEnableCvcStorageAndFillingName,
     flag_descriptions::kAutofillEnableCvcStorageAndFillingDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCvcStorageAndFilling)},

#if BUILDFLAG(IS_CHROMEOS)
    {"drive-fs-show-cse-files", flag_descriptions::kDriveFsShowCSEFilesName,
     flag_descriptions::kDriveFsShowCSEFilesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDriveFsShowCSEFiles)},
    {"drive-fs-mirroring", flag_descriptions::kDriveFsMirroringName,
     flag_descriptions::kDriveFsShowCSEFilesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDriveFsMirroring)},
    {"cros-labs-continuous-overview-animation",
     flag_descriptions::kContinuousOverviewScrollAnimationName,
     flag_descriptions::kContinuousOverviewScrollAnimationDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kContinuousOverviewScrollAnimation)},
    {"cros-labs-window-splitting", flag_descriptions::kWindowSplittingName,
     flag_descriptions::kWindowSplittingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kWindowSplitting)},
    {"cros-labs-tiling-window-resize",
     flag_descriptions::kTilingWindowResizeName,
     flag_descriptions::kTilingWindowResizeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kTilingWindowResize)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"privacy-sandbox-enrollment-overrides",
     flag_descriptions::kPrivacySandboxEnrollmentOverridesName,
     flag_descriptions::kPrivacySandboxEnrollmentOverridesDescription, kOsAll,
     ORIGIN_LIST_VALUE_TYPE(privacy_sandbox::kPrivacySandboxEnrollmentOverrides,
                            "")},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-smart-card-web-api", flag_descriptions::kSmartCardWebApiName,
     flag_descriptions::kSmartCardWebApiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(blink::features::kSmartCard)},
    {"enable-web-printing-api", flag_descriptions::kWebPrintingApiName,
     flag_descriptions::kWebPrintingApiDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(blink::features::kWebPrinting)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"autofill-enable-prefetching-risk-data-for-retrieval",
     flag_descriptions::kAutofillEnablePrefetchingRiskDataForRetrievalName,
     flag_descriptions::
         kAutofillEnablePrefetchingRiskDataForRetrievalDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnablePrefetchingRiskDataForRetrieval)},

#if BUILDFLAG(IS_ANDROID)
    {"read-aloud", flag_descriptions::kReadAloudName,
     flag_descriptions::kReadAloudDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kReadAloud)},

    {"read-aloud-background-playback",
     flag_descriptions::kReadAloudBackgroundPlaybackName,
     flag_descriptions::kReadAloudBackgroundPlaybackDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kReadAloudBackgroundPlayback)},

    {"read-aloud-in-cct", flag_descriptions::kReadAloudInCCTName,
     flag_descriptions::kReadAloudInCCTDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kReadAloudInOverflowMenuInCCT)},

    {"read-aloud-tap-to-seek", flag_descriptions::kReadAloudTapToSeekName,
     flag_descriptions::kReadAloudTapToSeekDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kReadAloudTapToSeek)},
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"third-party-profile-management",
     flag_descriptions::kThirdPartyProfileManagementName,
     flag_descriptions::kThirdPartyProfileManagementDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         profile_management::features::kThirdPartyProfileManagement)},

    {"oidc-auth-profile-management",
     flag_descriptions::kOidcAuthProfileManagementName,
     flag_descriptions::kOidcAuthProfileManagementDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         profile_management::features::kOidcAuthProfileManagement)},

    {"enable-generic-oidc-auth-profile-management",
     flag_descriptions::kEnableGenericOidcAuthProfileManagementName,
     flag_descriptions::kEnableGenericOidcAuthProfileManagementDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(profile_management::features::
                            kEnableGenericOidcAuthProfileManagement)},
    {"enable-user-link-capturing-scope-extensions-pwa",
     flag_descriptions::kDesktopPWAsUserLinkCapturingScopeExtensionsName,
     flag_descriptions::kDesktopPWAsUserLinkCapturingScopeExtensionsDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(features::kPwaNavigationCapturingWithScopeExtensions)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"enable-user-navigation-capturing-pwa",
     flag_descriptions::kPwaNavigationCapturingName,
     flag_descriptions::kPwaNavigationCapturingDescription,
     kOsLinux | kOsMac | kOsWin | kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kPwaNavigationCapturing,
                                    kPwaNavigationCapturingVariations,
                                    "PwaNavigationCapturing")},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)

    {"ip-protection-proxy-opt-out",
     flag_descriptions::kIpProtectionProxyOptOutName,
     flag_descriptions::kIpProtectionProxyOptOutDescription, kOsAll,
     MULTI_VALUE_TYPE(kIpProtectionProxyOptOutChoices)},

    {"protected-audience-debug-token",
     flag_descriptions::kProtectedAudiencesConsentedDebugTokenName,
     flag_descriptions::kProtectedAudiencesConsentedDebugTokenDescription,
     kOsAll,
     STRING_VALUE_TYPE(switches::kProtectedAudiencesConsentedDebugToken, "")},

    {"deprecate-unload", flag_descriptions::kDeprecateUnloadName,
     flag_descriptions::kDeprecateUnloadDescription, kOsAll | kDeprecated,
     FEATURE_VALUE_TYPE(network::features::kDeprecateUnload)},

    {"autofill-enable-fpan-risk-based-authentication",
     flag_descriptions::kAutofillEnableFpanRiskBasedAuthenticationName,
     flag_descriptions::kAutofillEnableFpanRiskBasedAuthenticationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableFpanRiskBasedAuthentication)},

    {"draw-immediately-when-interactive",
     flag_descriptions::kDrawImmediatelyWhenInteractiveName,
     flag_descriptions::kDrawImmediatelyWhenInteractiveDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDrawImmediatelyWhenInteractive)},

    {"ack-on-surface-activation-when-interactive",
     flag_descriptions::kAckOnSurfaceActivationWhenInteractiveName,
     flag_descriptions::kAckOnSurfaceActivationWhenInteractiveDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kAckOnSurfaceActivationWhenInteractive)},

#if BUILDFLAG(IS_MAC)
    {"enable-mac-pwas-notification-attribution",
     flag_descriptions::kMacPWAsNotificationAttributionName,
     flag_descriptions::kMacPWAsNotificationAttributionDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kAppShimNotificationAttribution)},

    {"use-adhoc-signing-for-web-app-shims",
     flag_descriptions::kUseAdHocSigningForWebAppShimsName,
     flag_descriptions::kUseAdHocSigningForWebAppShimsDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kUseAdHocSigningForWebAppShims)},
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_CHROMEOS)
    {"seal-key", flag_descriptions::kSealKeyName,
     flag_descriptions::kSealKeyDescription, kOsCrOS,
     STRING_VALUE_TYPE(ash::switches::kSealKey, "")},
#endif

#if BUILDFLAG(ENABLE_HLS_DEMUXER)
    {"enable-builtin-hls", flag_descriptions::kEnableBuiltinHlsName,
     flag_descriptions::kEnableBuiltinHlsDescription, kOsAll,
     FEATURE_VALUE_TYPE(media::kBuiltInHlsPlayer)},
#endif

#if !BUILDFLAG(IS_CHROMEOS)
    {"profiles-reordering", flag_descriptions::kProfilesReorderingName,
     flag_descriptions::kProfilesReorderingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(kProfilesReordering)},
#endif

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    {"enable-history-sync-optin-expansion-pill",
     flag_descriptions::kEnableHistorySyncOptinExpansionPillName,
     flag_descriptions::kEnableHistorySyncOptinExpansionPillDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         switches::kEnableHistorySyncOptinExpansionPill,
         kHistorySyncOptinExpansionPillVariations,
         "EnableHistorySyncOptinExpansionPill")},
#endif

#if BUILDFLAG(ENABLE_DICE_SUPPORT) && BUILDFLAG(ENABLE_EXTENSIONS)
    {"enable-extensions-explicit-browser-signin",
     flag_descriptions::kEnableExtensionsExplicitBrowserSigninName,
     flag_descriptions::kEnableExtensionsExplicitBrowserSigninDescription,
     kOsMac | kOsWin | kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(switches::kEnableExtensionsExplicitBrowserSignin)},
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT) && BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(IS_CHROMEOS)
    {"flex-firmware-update", flag_descriptions::kFlexFirmwareUpdateName,
     flag_descriptions::kFlexFirmwareUpdateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFlexFirmwareUpdate)},

    {"ignore-device-flex-arc-enabled-policy",
     flag_descriptions::kIgnoreDeviceFlexArcEnabledPolicyName,
     flag_descriptions::kIgnoreDeviceFlexArcEnabledPolicyDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kIgnoreDeviceFlexArcEnabledPolicy)},

    {"ipp-first-setup-for-usb-printers",
     flag_descriptions::kIppFirstSetupForUsbPrintersName,
     flag_descriptions::kIppFirstSetupForUsbPrintersDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kIppFirstSetupForUsbPrinters)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
    {"enable-bound-session-credentials",
     flag_descriptions::kEnableBoundSessionCredentialsName,
     flag_descriptions::kEnableBoundSessionCredentialsDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(switches::kEnableBoundSessionCredentials,
                                    kEnableBoundSessionCredentialsVariations,
                                    "EnableBoundSessionCredentials")},
    {"enable-bound-session-credentials-software-keys-for-manual-testing",
     flag_descriptions::
         kEnableBoundSessionCredentialsSoftwareKeysForManualTestingName,
     flag_descriptions::
         kEnableBoundSessionCredentialsSoftwareKeysForManualTestingDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         unexportable_keys::
             kEnableBoundSessionCredentialsSoftwareKeysForManualTesting)},
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-soul", flag_descriptions::kCrosSoulName,
     flag_descriptions::kCrosSoulDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootCrOSSOUL")},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"observable-api", flag_descriptions::kObservableAPIName,
     flag_descriptions::kObservableAPIDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kObservableAPI)},

    {"menu-elements", flag_descriptions::kMenuElementsName,
     flag_descriptions::kMenuElementsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kMenuElements)},

#if BUILDFLAG(IS_ANDROID)
    {"android-hub-search-tab-groups",
     flag_descriptions::kAndroidHubSearchTabGroupsName,
     flag_descriptions::kAndroidHubSearchTabGroupsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kAndroidHubSearchTabGroups)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_COMPOSE)
    {flag_descriptions::kComposeId, flag_descriptions::kComposeName,
     flag_descriptions::kComposeDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_VALUE_TYPE(compose::features::kEnableCompose)},

    {"compose-proactive-nudge", flag_descriptions::kComposeProactiveNudgeName,
     flag_descriptions::kComposeProactiveNudgeDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         compose::features::kEnableComposeProactiveNudge,
         kComposeProactiveNudgeVariations,
         "ComposeProactiveNudge")},

    {"compose-nudge-display-at-cursor",
     flag_descriptions::kComposeNudgeAtCursorName,
     flag_descriptions::kComposeNudgeAtCursorDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_VALUE_TYPE(compose::features::kEnableComposeNudgeAtCursor)},

    {"compose-segmentation-promotion",
     flag_descriptions::kComposeSegmentationPromotionName,
     flag_descriptions::kComposeSegmentationPromotionDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_VALUE_TYPE(segmentation_platform::features::
                            kSegmentationPlatformComposePromotion)},

    {"compose-selection-nudge", flag_descriptions::kComposeSelectionNudgeName,
     flag_descriptions::kComposeSelectionNudgeDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         compose::features::kEnableComposeSelectionNudge,
         kComposeSelectionNudgeVariations,
         "ComposeSelectionNudge")},

    {"compose-upfront-input-modes",
     flag_descriptions::kComposeUpfrontInputModesName,
     flag_descriptions::kComposeUpfrontInputModesDescription,
     kOsWin | kOsLinux | kOsMac | kOsCrOS,
     FEATURE_VALUE_TYPE(compose::features::kComposeUpfrontInputModes)},
#endif

    {"related-website-sets-permission-grants",
     flag_descriptions::kShowRelatedWebsiteSetsPermissionGrantsName,
     flag_descriptions::kShowRelatedWebsiteSetsPermissionGrantsDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         permissions::features::kShowRelatedWebsiteSetsPermissionGrants)},

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-switcher", flag_descriptions::kCrosSwitcherName,
     flag_descriptions::kCrosSwitcherDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kCrosSwitcher)},
    {"platform-keys-changes-wave-1",
     flag_descriptions::kPlatformKeysChangesWave1Name,
     flag_descriptions::kPlatformKeysChangesWave1Description, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kPlatformKeysChangesWave1)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"autofill-enable-loyalty-cards-filling",
     flag_descriptions::kAutofillEnableLoyaltyCardsFillingName,
     flag_descriptions::kAutofillEnableLoyaltyCardsFillingDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableLoyaltyCardsFilling)},

#if BUILDFLAG(IS_ANDROID)
    {"background-not-perceptible-binding",
     flag_descriptions::kBackgroundNotPerceptibleBindingName,
     flag_descriptions::kBackgroundNotPerceptibleBindingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(base::features::kBackgroundNotPerceptibleBinding)},
    {"boarding-pass-detector", flag_descriptions::kBoardingPassDetectorName,
     flag_descriptions::kBoardingPassDetectorDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kBoardingPassDetector,
                                    kBoardingPassDetectorVariations,
                                    "Allowed Urls")},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"cloud-gaming-device", flag_descriptions::kCloudGamingDeviceName,
     flag_descriptions::kCloudGamingDeviceDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kCloudGamingDevice)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"content-settings-partitioning",
     flag_descriptions::kContentSettingsPartitioningName,
     flag_descriptions::kContentSettingsPartitioningDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         content_settings::features::kContentSettingsPartitioning)},

#if BUILDFLAG(IS_ANDROID)
    {"use-fullscreen-insets-api",
     flag_descriptions::kFullscreenInsetsApiMigrationName,
     flag_descriptions::kFullscreenInsetsApiMigrationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kFullscreenInsetsApiMigration)},

    {"use-fullscreen-insets-api-on-automotive",
     flag_descriptions::kFullscreenInsetsApiMigrationOnAutomotiveName,
     flag_descriptions::kFullscreenInsetsApiMigrationOnAutomotiveDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kFullscreenInsetsApiMigrationOnAutomotive)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
    {"enable-mac-ime-live-conversion-fix",
     flag_descriptions::kMacImeLiveConversionFixName,
     flag_descriptions::kMacImeLiveConversionFixDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kMacImeLiveConversionFix)},
#endif

    {"tear-off-web-app-tab-opens-web-app-window",
     flag_descriptions::kTearOffWebAppAppTabOpensWebAppWindowName,
     flag_descriptions::kTearOffWebAppAppTabOpensWebAppWindowDescription,
     kOsAll, FEATURE_VALUE_TYPE(features::kTearOffWebAppTabOpensWebAppWindow)},

#if BUILDFLAG(IS_ANDROID)
    {"offline-auto-fetch", flag_descriptions::kOfflineAutoFetchName,
     flag_descriptions::kOfflineAutoFetchDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kOfflineAutoFetch)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {kAssistantIphInternalName, flag_descriptions::kAssistantIphName,
     flag_descriptions::kAssistantIphDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(feature_engagement::kIPHLauncherSearchHelpUiFeature)},

    {"battery-badge-icon", flag_descriptions::kBatteryBadgeIconName,
     flag_descriptions::kBatteryBadgeIconDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kBatteryBadgeIcon)},

    {"battery-charge-limit", flag_descriptions::kBatteryChargeLimitName,
     flag_descriptions::kBatteryChargeLimitDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kBatteryChargeLimit)},

    {"bluetooth-wifi-qs-pod-refresh",
     flag_descriptions::kBluetoothWifiQSPodRefreshName,
     flag_descriptions::kBluetoothWifiQSPodRefreshDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kBluetoothWifiQSPodRefresh)},

    {"mahi", flag_descriptions::kMahiName, flag_descriptions::kMahiDescription,
     kOsCrOS, FEATURE_VALUE_TYPE(chromeos::features::kMahi)},

    {"mahi-debugging", flag_descriptions::kMahiDebuggingName,
     flag_descriptions::kMahiDebuggingDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kMahiDebugging)},

    {"mahi-panel-resizable", flag_descriptions::kMahiPanelResizableName,
     flag_descriptions::kMahiPanelResizableDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kMahiPanelResizable)},

    {"notification-width-increase",
     flag_descriptions::kNotificationWidthIncreaseName,
     flag_descriptions::kNotificationWidthIncreaseDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kNotificationWidthIncrease)},

    {"pompano", flag_descriptions::kPompanoName,
     flag_descriptions::kPompanoDescritpion, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kPompano)},

    {"mahi-summarize-selected", flag_descriptions::kMahiSummarizeSelectedName,
     flag_descriptions::kMahiSummarizeSelectedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kMahiSummarizeSelected)},

    {"ash-picker-gifs", flag_descriptions::kAshPickerGifsName,
     flag_descriptions::kAshPickerGifsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kPickerGifs)},

    {"ash-modifier-split", flag_descriptions::kAshModifierSplitName,
     flag_descriptions::kAshModifierSplitDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kModifierSplit)},

    {"ash-split-keyboard-refactor",
     flag_descriptions::kAshSplitKeyboardRefactorName,
     flag_descriptions::kAshSplitKeyboardRefactorDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kSplitKeyboardRefactor)},

    {"enable-toggle-camera-shortcut",
     flag_descriptions::kEnableToggleCameraShortcutName,
     flag_descriptions::kEnableToggleCameraShortcutDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kEnableToggleCameraShortcut)},

    {"ash-null-top-row-fix", flag_descriptions::kAshNullTopRowFixName,
     flag_descriptions::kAshNullTopRowFixDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kNullTopRowFix)},

#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"password-manual-fallback-available",
     flag_descriptions::kPasswordManualFallbackAvailableName,
     flag_descriptions::kPasswordManualFallbackAvailableDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         password_manager::features::kPasswordManualFallbackAvailable)},

    {"save-passwords-contextual-ui",
     flag_descriptions::kSavePasswordsContextualUiName,
     flag_descriptions::kSavePasswordsContextualUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSavePasswordsContextualUi)},

#endif  // !BUILDFLAG(IS_ANDROID)

    {"enable-unrestricted-usb", flag_descriptions::kEnableUnrestrictedUsbName,
     flag_descriptions::kEnableUnrestrictedUsbDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kUnrestrictedUsb)},

    {"autofill-enable-vcn-3ds-authentication",
     flag_descriptions::kAutofillEnableVcn3dsAuthenticationName,
     flag_descriptions::kAutofillEnableVcn3dsAuthenticationDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableVcn3dsAuthentication)},

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-locked-mode", flag_descriptions::kLockedModeName,
     flag_descriptions::kLockedModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(blink::features::kLockedMode)},
#endif  // BUILDFLAG_(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"link-preview", flag_descriptions::kLinkPreviewName,
     flag_descriptions::kLinkPreviewDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kLinkPreview,
                                    kLinkPreviewTriggerTypeVariations,
                                    "LinkPreview")},
#endif  // !BUILDFLAG_(IS_ANDROID)

    {"send-tab-ios-push-notifications",
     flag_descriptions::kSendTabToSelfIOSPushNotificationsName,
     flag_descriptions::kSendTabToSelfIOSPushNotificationsDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         send_tab_to_self::kSendTabToSelfIOSPushNotifications,
         kSendTabIOSPushNotificationsVariations,
         "SendTabToSelfIOSPushNotifications")},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-display-performance-mode",
     flag_descriptions::kEnableDisplayPerformanceModeName,
     flag_descriptions::kEnableDisplayPerformanceModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kDisplayPerformanceMode)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"android-tab-declutter-archive-all-but-active-tab",
     flag_descriptions::kAndroidTabDeclutterArchiveAllButActiveTabName,
     flag_descriptions::kAndroidTabDeclutterArchiveAllButActiveTabDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kAndroidTabDeclutterArchiveAllButActiveTab)},

    {"android-tab-declutter-archive-duplicate-tabs",
     flag_descriptions::kAndroidTabDeclutterArchiveDuplicateTabsName,
     flag_descriptions::kAndroidTabDeclutterArchiveDuplicateTabsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kAndroidTabDeclutterArchiveDuplicateTabs)},

    {"android-tab-declutter-archive-tab-groups",
     flag_descriptions::kAndroidTabDeclutterArchiveTabGroupsName,
     flag_descriptions::kAndroidTabDeclutterArchiveTabGroupsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidTabDeclutterArchiveTabGroups)},

    {"android-tab-declutter-auto-delete",
     flag_descriptions::kAndroidTabDeclutterAutoDeleteName,
     flag_descriptions::kAndroidTabDeclutterAutoDeleteDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidTabDeclutterAutoDelete)},

    {"android-tab-declutter-auto-delete-kill-switch",
     flag_descriptions::kAndroidTabDeclutterAutoDeleteKillSwitchName,
     flag_descriptions::kAndroidTabDeclutterAutoDeleteKillSwitchDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kAndroidTabDeclutterAutoDeleteKillSwitch)},

    {"android-tab-declutter-performance-improvements",
     flag_descriptions::kAndroidTabDeclutterPerformanceImprovementsName,
     flag_descriptions::kAndroidTabDeclutterPerformanceImprovementsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kAndroidTabDeclutterPerformanceImprovements)},

    {"android-tab-groups-color-update-gm3",
     flag_descriptions::kAndroidTabGroupsColorUpdateGM3Name,
     flag_descriptions::kAndroidTabGroupsColorUpdateGM3Description, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidTabGroupsColorUpdateGM3)},

    {"tab-group-sync-disable-network-layer",
     flag_descriptions::kTabGroupSyncDisableNetworkLayerName,
     flag_descriptions::kTabGroupSyncDisableNetworkLayerDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(tab_groups::kTabGroupSyncDisableNetworkLayer)},

    {"swap-new-tab-and-new-tab-in-group-android",
     flag_descriptions::kSwapNewTabAndNewTabInGroupAndroidName,
     flag_descriptions::kSwapNewTabAndNewTabInGroupAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kSwapNewTabAndNewTabInGroupAndroid)},

    {"history-pane-android", flag_descriptions::kHistoryPaneAndroidName,
     flag_descriptions::kHistoryPaneAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kHistoryPaneAndroid)},

    {"bookmark-pane-android", flag_descriptions::kBookmarkPaneAndroidName,
     flag_descriptions::kBookmarkPaneAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kBookmarkPaneAndroid)},

    {"batch-tab-restore", flag_descriptions::kBatchTabRestoreName,
     flag_descriptions::kBatchTabRestoreDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kBatchTabRestore)},

#endif  // BUILDFLAG(IS_ANDROID)

    {"data-sharing-debug-logs", flag_descriptions::kDataSharingDebugLogsName,
     flag_descriptions::kDataSharingDebugLogsDescription, kOsAll,
     SINGLE_VALUE_TYPE(data_sharing::kDataSharingDebugLoggingEnabled)},

    {"autofill-shared-storage-server-card-data",
     flag_descriptions::kAutofillSharedStorageServerCardDataName,
     flag_descriptions::kAutofillSharedStorageServerCardDataDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillSharedStorageServerCardData)},

#if BUILDFLAG(IS_ANDROID)
    {"android-open-pdf-inline", flag_descriptions::kAndroidOpenPdfInlineName,
     flag_descriptions::kAndroidOpenPdfInlineDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAndroidOpenPdfInline)},

    {"android-open-pdf-inline-backport",
     flag_descriptions::kAndroidOpenPdfInlineBackportName,
     flag_descriptions::kAndroidOpenPdfInlineBackportDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidOpenPdfInlineBackport)},

    {"android-pdf-assist-content",
     flag_descriptions::kAndroidPdfAssistContentName,
     flag_descriptions::kAndroidPdfAssistContentDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidPdfAssistContent)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"multi-calendar-in-quick-settings",
     flag_descriptions::kMultiCalendarSupportName,
     flag_descriptions::kMultiCalendarSupportDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kMultiCalendarSupport)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"autofill-enable-syncing-of-pix-bank-accounts",
     flag_descriptions::kAutofillEnableSyncingOfPixBankAccountsName,
     flag_descriptions::kAutofillEnableSyncingOfPixBankAccountsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableSyncingOfPixBankAccounts)},

    {"enable-pix-account-linking",
     flag_descriptions::kEnablePixAccountLinkingName,
     flag_descriptions::kEnablePixAccountLinkingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(payments::facilitated::kEnablePixAccountLinking)},

    {"enable-pix-payments-in-landscape-mode",
     flag_descriptions::kEnablePixPaymentsInLandscapeModeName,
     flag_descriptions::kEnablePixPaymentsInLandscapeModeDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         payments::facilitated::kEnablePixPaymentsInLandscapeMode)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"autofill-enable-card-benefits-for-american-express",
     flag_descriptions::kAutofillEnableCardBenefitsForAmericanExpressName,
     flag_descriptions::
         kAutofillEnableCardBenefitsForAmericanExpressDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCardBenefitsForAmericanExpress)},

    {"autofill-enable-card-benefits-sync",
     flag_descriptions::kAutofillEnableCardBenefitsSyncName,
     flag_descriptions::kAutofillEnableCardBenefitsSyncDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableCardBenefitsSync)},

    {"linked-services-setting", flag_descriptions::kLinkedServicesSettingName,
     flag_descriptions::kLinkedServicesSettingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kLinkedServicesSetting)},

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-mall", flag_descriptions::kCrosMallName,
     flag_descriptions::kCrosMallDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kCrosMall)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC)
    {"reduce-ip-address-change-notification",
     flag_descriptions::kReduceIPAddressChangeNotificationName,
     flag_descriptions::kReduceIPAddressChangeNotificationDescription, kOsMac,
     FEATURE_VALUE_TYPE(net::features::kReduceIPAddressChangeNotification)},
#endif  // BUILDFLAG(IS_MAC)

    {"enable-fingerprinting-protection-blocklist",
     flag_descriptions::kEnableFingerprintingProtectionBlocklistName,
     flag_descriptions::kEnableFingerprintingProtectionBlocklistDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         fingerprinting_protection_filter::features::
             kEnableFingerprintingProtectionFilter,
         kEnableFingerprintingProtectionFilterVariations,
         "EnableFingerprintingProtectionFilter")},

    {"enable-fingerprinting-protection-blocklist-incognito",
     flag_descriptions::kEnableFingerprintingProtectionBlocklistInIncognitoName,
     flag_descriptions::
         kEnableFingerprintingProtectionBlocklistInIncognitoDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         fingerprinting_protection_filter::features::
             kEnableFingerprintingProtectionFilterInIncognito,
         kEnableFingerprintingProtectionFilterInIncognitoVariations,
         "EnableFingerprintingProtectionFilterInIncognito")},

    {"enable-standard-device-bound-session-credentials",
     flag_descriptions::kEnableStandardBoundSessionCredentialsName,
     flag_descriptions::kEnableStandardBoundSessionCredentialsDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(net::features::kDeviceBoundSessions,
                                    kStandardBoundSessionCredentialsVariations,
                                    "standard-device-bound-sessions")},
    {"enable-standard-device-bound-session-persistence",
     flag_descriptions::kEnableStandardBoundSessionPersistenceName,
     flag_descriptions::kEnableStandardBoundSessionPersistenceDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(net::features::kPersistDeviceBoundSessions)},
    {"enable-standard-device-bound-sesssion-refresh-quota",
     flag_descriptions::kEnableStandardBoundSessionRefreshQuotaName,
     flag_descriptions::kEnableStandardBoundSessionRefreshQuotaDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(net::features::kDeviceBoundSessionsRefreshQuota)},

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-soul-gd", flag_descriptions::kCrosSoulGravediggerName,
     flag_descriptions::kCrosSoulGravediggerDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootGravedigger")},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"replace-sync-promos-with-sign-in-promos",
     flag_descriptions::kReplaceSyncPromosWithSignInPromosName,
     flag_descriptions::kReplaceSyncPromosWithSignInPromosDescription,
     kOsAndroid, MULTI_VALUE_TYPE(kReplaceSyncPromosWithSignInPromosChoices)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"pwm-show-suggestions-on-autofocus",
     flag_descriptions::kPasswordManagerShowSuggestionsOnAutofocusName,
     flag_descriptions::kPasswordManagerShowSuggestionsOnAutofocusDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kShowSuggestionsOnAutofocus)},

#if BUILDFLAG(IS_ANDROID)
    {"android-browser-controls-in-viz",
     flag_descriptions::kAndroidBrowserControlsInVizName,
     flag_descriptions::kAndroidBrowserControlsInVizDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAndroidBrowserControlsInViz)},

    {"android-browser-controls-in-viz-bottom-controls",
     flag_descriptions::kAndroidBcivBottomControlsName,
     flag_descriptions::kAndroidBcivBottomControlsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAndroidBcivBottomControls)},
#endif

    {"optimization-guide-enable-dogfood-logging",
     flag_descriptions::kOptimizationGuideEnableDogfoodLoggingName,
     flag_descriptions::kOptimizationGuideEnableDogfoodLoggingDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(
         optimization_guide::switches::kEnableModelQualityDogfoodLogging)},

#if !BUILDFLAG(IS_ANDROID)
    {"hybrid-passkeys-in-context-menu",
     flag_descriptions::kWebAuthnUsePasskeyFromAnotherDeviceInContextMenuName,
     flag_descriptions::
         kWebAuthnUsePasskeyFromAnotherDeviceInContextMenuDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(password_manager::features::
                            kWebAuthnUsePasskeyFromAnotherDeviceInContextMenu)},
    {"web-authentication-passkey-upgrade",
     flag_descriptions::kWebAuthnPasskeyUpgradeName,
     flag_descriptions::kWebAuthnPasskeyUpgradeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(device::kWebAuthnPasskeyUpgrade)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"conch", flag_descriptions::kConchName,
     flag_descriptions::kConchDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kConch)},

    {"conch-system-audio-from-mic",
     flag_descriptions::kConchSystemAudioFromMicName,
     flag_descriptions::kConchSystemAudioFromMicDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kConchSystemAudioFromMic)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"autofill-sync-ewallet-accounts",
     flag_descriptions::kAutofillSyncEwalletAccountsName,
     flag_descriptions::kAutofillSyncEwalletAccountsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillSyncEwalletAccounts)},

    {"ewallet-payments", flag_descriptions::kEwalletPaymentsName,
     flag_descriptions::kEwalletPaymentsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(payments::facilitated::kEwalletPayments)},

    {"payment-link-detection", flag_descriptions::kPaymentLinkDetectionName,
     flag_descriptions::kPaymentLinkDetectionDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kPaymentLinkDetection)},

    {"process-rank-policy-android",
     flag_descriptions::kProcessRankPolicyAndroidName,
     flag_descriptions::kProcessRankPolicyAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kProcessRankPolicyAndroid)},

    {"protected-tabs-android", flag_descriptions::kProtectedTabsAndroidName,
     flag_descriptions::kProtectedTabsAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kProtectedTabsAndroid)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"screenlock-reauth", flag_descriptions::kScreenlockReauthCardName,
     flag_descriptions::kScreenlockReauthCardDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(password_manager::features::kBiometricsAuthForPwdFill)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"ruby-short-heuristics", flag_descriptions::kRubyShortHeuristicsName,
     flag_descriptions::kRubyShortHeuristicsDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kRubyShortHeuristics)},

    {"prompt-api-for-gemini-nano",
     flag_descriptions::kPromptAPIForGeminiNanoName,
     flag_descriptions::kPromptAPIForGeminiNanoDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kAIPromptAPI),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"prompt-api-for-gemini-nano-multimodal-input",
     flag_descriptions::kPromptAPIForGeminiNanoMultimodalInputName,
     flag_descriptions::kPromptAPIForGeminiNanoMultimodalInputDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kAIPromptAPIMultimodalInput),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"summarization-api-for-gemini-nano",
     flag_descriptions::kSummarizationAPIForGeminiNanoName,
     flag_descriptions::kSummarizationAPIForGeminiNanoDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kAISummarizationAPI,
                                    kAISummarizationAPIWithAdaptationVaration,
                                    "AISummarizationAPIWithAdaptation"),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"writer-api-for-gemini-nano",
     flag_descriptions::kWriterAPIForGeminiNanoName,
     flag_descriptions::kWriterAPIForGeminiNanoDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kAIWriterAPI),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"rewriter-api-for-gemini-nano",
     flag_descriptions::kRewriterAPIForGeminiNanoName,
     flag_descriptions::kRewriterAPIForGeminiNanoDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kAIRewriterAPI),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"proofreader-api-for-gemini-nano",
     flag_descriptions::kProofreaderAPIForGeminiNanoName,
     flag_descriptions::kProofreaderAPIForGeminiNanoDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(blink::features::kAIProofreadingAPI),
     flag_descriptions::kAIAPIsForGeminiNanoLinks},

    {"css-masonry-layout", flag_descriptions::kCssMasonryLayoutName,
     flag_descriptions::kCssMasonryLayoutDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kCSSMasonryLayout)},

    {"storage-access-api-follows-same-origin-policy",
     flag_descriptions::kStorageAccessApiFollowsSameOriginPolicyName,
     flag_descriptions::kStorageAccessApiFollowsSameOriginPolicyDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         net::features::kStorageAccessApiFollowsSameOriginPolicy)},

    {"canvas-2d-hibernation", flag_descriptions::kCanvasHibernationName,
     flag_descriptions::kCanvasHibernationDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kCanvas2DHibernation)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"enable-history-sync-optin",
     flag_descriptions::kEnableHistorySyncOptinName,
     flag_descriptions::kEnableHistorySyncOptinDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kEnableHistorySyncOptin)},
    {"sync-enable-bookmarks-in-transport-mode",
     flag_descriptions::kSyncEnableBookmarksInTransportModeName,
     flag_descriptions::kSyncEnableBookmarksInTransportModeDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kSyncEnableBookmarksInTransportMode)},
    {"reading-list-enable-sync-transport-mode-upon-sign-in",
     flag_descriptions::kReadingListEnableSyncTransportModeUponSignInName,
     flag_descriptions::
         kReadingListEnableSyncTransportModeUponSignInDescription,
     kOsWin | kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(syncer::kReadingListEnableSyncTransportModeUponSignIn)},
#endif

    {"visited-url-ranking-service-domain-deduplication",
     flag_descriptions::kVisitedURLRankingServiceDeduplicationName,
     flag_descriptions::kVisitedURLRankingServiceDeduplicationDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         visited_url_ranking::features::kVisitedURLRankingDeduplication,
         kVisitedURLRankingDomainDeduplicationVariations,
         "visited-url-ranking-service-domain-deduplication")},

    {"visited-url-ranking-service-history-visibility-score-filter",
     flag_descriptions::
         kVisitedURLRankingServiceHistoryVisibilityScoreFilterName,
     flag_descriptions::
         kVisitedURLRankingServiceHistoryVisibilityScoreFilterDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(visited_url_ranking::features::
                            kVisitedURLRankingHistoryVisibilityScoreFilter)},

    {"autofill-upload-card-request-timeout",
     flag_descriptions::kAutofillUploadCardRequestTimeoutName,
     flag_descriptions::kAutofillUploadCardRequestTimeoutDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::kAutofillUploadCardRequestTimeout,
         kAutofillUploadCardRequestTimeoutOptions,
         "AutofillUploadCardRequestTimeout")},

    {"autofill-vcn-enroll-request-timeout",
     flag_descriptions::kAutofillVcnEnrollRequestTimeoutName,
     flag_descriptions::kAutofillVcnEnrollRequestTimeoutDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::kAutofillVcnEnrollRequestTimeout,
         kAutofillVcnEnrollRequestTimeoutOptions,
         "AutofillVcnEnrollRequestTimeout")},

    {"autofill-unmask-card-request-timeout",
     flag_descriptions::kAutofillUnmaskCardRequestTimeoutName,
     flag_descriptions::kAutofillUnmaskCardRequestTimeoutDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillUnmaskCardRequestTimeout)},

#if !BUILDFLAG(IS_ANDROID)
    {"freezing-on-energy-saver", flag_descriptions::kFreezingOnEnergySaverName,
     flag_descriptions::kFreezingOnEnergySaverDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         performance_manager::features::kFreezingOnBatterySaver)},

    {"freezing-on-energy-saver-testing",
     flag_descriptions::kFreezingOnEnergySaverTestingName,
     flag_descriptions::kFreezingOnEnergySaverTestingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         performance_manager::features::kFreezingOnBatterySaverForTesting)},

    {"infinite-tabs-freezing", flag_descriptions::kInfiniteTabsFreezingName,
     flag_descriptions::kInfiniteTabsFreezingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(performance_manager::features::kInfiniteTabsFreezing)},

    {"memory-purge-on-freeze-limit",
     flag_descriptions::kMemoryPurgeOnFreezeLimitName,
     flag_descriptions::kMemoryPurgeOnFreezeLimitDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kMemoryPurgeOnFreezeLimit)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN)
    {"hid-get-feature-report-fix",
     flag_descriptions::kHidGetFeatureReportFixName,
     flag_descriptions::kHidGetFeatureReportFixDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kHidGetFeatureReportFix)},
#endif  // BUILDFLAG(IS_WIN)

#if !BUILDFLAG(IS_ANDROID)
    {"translate-open-settings", flag_descriptions::kTranslateOpenSettingsName,
     flag_descriptions::kTranslateOpenSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(language::kTranslateOpenSettings)},
#endif

    {"language-detection-api", flag_descriptions::kLanguageDetectionAPIName,
     flag_descriptions::kLanguageDetectionAPIDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kLanguageDetectionAPI)},

#if BUILDFLAG(IS_ANDROID)
    {"history-page-history-sync-promo",
     flag_descriptions::kHistoryPageHistorySyncPromoName,
     flag_descriptions::kHistoryPageHistorySyncPromoDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kHistoryOptInEntryPointChoices)},

    {"history-opt-in-educational-tip",
     flag_descriptions::kHistoryOptInEducationalTipName,
     flag_descriptions::kHistoryOptInEducationalTipDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(switches::kHistoryOptInEducationalTip,
                                    kHistoryOptInEducationalTipVariations,
                                    "HistoryOptInEducationalTipVariations")},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
    {"supervised-profile-safe-search",
     flag_descriptions::kSupervisedProfileSafeSearchName,
     flag_descriptions::kSupervisedProfileSafeSearchDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         supervised_user::kForceSafeSearchForUnauthenticatedSupervisedUsers)},

    {"supervised-profile-custom-strings",
     flag_descriptions::kSupervisedProfileCustomStringsName,
     flag_descriptions::kSupervisedProfileCustomStringsDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         supervised_user::kCustomProfileStringsForSupervisedUsers)},

    {"supervised-profile-sign-in-iph",
     flag_descriptions::kSupervisedProfileSignInIphName,
     flag_descriptions::kSupervisedProfileSignInIphDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         feature_engagement::kIPHSupervisedUserProfileSigninFeature)},

    {"supervised-profile-kite-badging",
     flag_descriptions::kSupervisedProfileShowKiteBadgeName,
     flag_descriptions::kSupervisedProfileShowKiteBadgeDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(supervised_user::kShowKiteForSupervisedUsers)},

    {"supervised-user-local-web-approvals",
     flag_descriptions::kSupervisedUserLocalWebApprovalsName,
     flag_descriptions::kSupervisedUserLocalWebApprovalsDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(supervised_user::kLocalWebApprovals)},

#endif

#if BUILDFLAG(IS_ANDROID)
    {"sensitive-content", flag_descriptions::kSensitiveContentName,
     flag_descriptions::kSensitiveContentDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         sensitive_content::features::kSensitiveContent,
         kSensitiveContentVariations,
         "SensitiveContent")},

    {"sensitive-content-while-switching-tabs",
     flag_descriptions::kSensitiveContentWhileSwitchingTabsName,
     flag_descriptions::kSensitiveContentWhileSwitchingTabsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         sensitive_content::features::kSensitiveContentWhileSwitchingTabs)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"data-sharing", flag_descriptions::kDataSharingName,
     flag_descriptions::kDataSharingDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(data_sharing::features::kDataSharingFeature,
                                    kDatasharingVariations,
                                    "Enabled")},

    {"collaboration-automotive",
     flag_descriptions::kCollaborationAutomotiveName,
     flag_descriptions::kCollaborationAutomotiveDescription, kOsAll,
     FEATURE_VALUE_TYPE(data_sharing::features::kCollaborationAutomotive)},

    {"collaboration-entreprise-v2",
     flag_descriptions::kCollaborationEntrepriseV2Name,
     flag_descriptions::kCollaborationEntrepriseV2Description, kOsAll,
     FEATURE_VALUE_TYPE(data_sharing::features::kCollaborationEntrepriseV2)},

    {"collaboration-shared-tab-group-account-data",
     flag_descriptions::kCollaborationSharedTabGroupAccountDataName,
     flag_descriptions::kCollaborationSharedTabGroupAccountDataDescription,
     kOsAll, FEATURE_VALUE_TYPE(syncer::kSyncSharedTabGroupAccountData)},

    {"data-sharing-join-only", flag_descriptions::kDataSharingJoinOnlyName,
     flag_descriptions::kDataSharingJoinOnlyDescription, kOsAll,
     FEATURE_VALUE_TYPE(data_sharing::features::kDataSharingJoinOnly)},

    {"data-sharing-non-production-environment",
     flag_descriptions::kDataSharingNonProductionEnvironmentName,
     flag_descriptions::kDataSharingNonProductionEnvironmentDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         data_sharing::features::kDataSharingNonProductionEnvironment)},

    {"history-sync-alternative-illustration",
     flag_descriptions::kHistorySyncAlternativeIllustrationName,
     flag_descriptions::kHistorySyncAlternativeIllustrationDescription, kOsAll,
     FEATURE_VALUE_TYPE(tab_groups::kUseAlternateHistorySyncIllustration)},

    {"left-click-opens-tab-group-bubble",
     flag_descriptions::kLeftClickOpensTabGroupBubbleName,
     flag_descriptions::kLeftClickOpensTabGroupBubbleDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tab_groups::kLeftClickOpensTabGroupBubble)},

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-content-adjusted-refresh-rate",
     flag_descriptions::kCrosContentAdjustedRefreshRateName,
     flag_descriptions::kCrosContentAdjustedRefreshRateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kCrosContentAdjustedRefreshRate)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"autofill-enable-cvc-storage-and-filling-enhancement",
     flag_descriptions::kAutofillEnableCvcStorageAndFillingEnhancementName,
     flag_descriptions::
         kAutofillEnableCvcStorageAndFillingEnhancementDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCvcStorageAndFillingEnhancement)},

#if BUILDFLAG(IS_ANDROID)
    {"discount-on-navigation",
     commerce::flag_descriptions::kDiscountOnNavigationName,
     commerce::flag_descriptions::kDiscountOnNavigationDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(commerce::kEnableDiscountInfoApi,
                                    kDiscountsVariationsOnAndroid,
                                    "DisocuntOnNavigation")},
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"discount-on-navigation",
     commerce::flag_descriptions::kDiscountOnNavigationName,
     commerce::flag_descriptions::kDiscountOnNavigationDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(commerce::kEnableDiscountInfoApi,
                                    kDiscountsVariations,
                                    "DisocuntOnNavigation")},
#endif  //! BUILDFLAG(IS_ANDROID)

    {"devtools-privacy-ui", flag_descriptions::kDevToolsPrivacyUIName,
     flag_descriptions::kDevToolsPrivacyUIDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kDevToolsPrivacyUI)},

    {"permissions-ai-v1", flag_descriptions::kPermissionsAIv1Name,
     flag_descriptions::kPermissionsAIv1Description, kOsDesktop,
     FEATURE_VALUE_TYPE(permissions::features::kPermissionsAIv1)},

    {"permissions-ai-v3", flag_descriptions::kPermissionsAIv3Name,
     flag_descriptions::kPermissionsAIv3Description, kOsDesktop,
     FEATURE_VALUE_TYPE(permissions::features::kPermissionsAIv3)},
#if BUILDFLAG(IS_CHROMEOS)
    {"exclude-display-in-mirror-mode",
     flag_descriptions::kExcludeDisplayInMirrorModeName,
     flag_descriptions::kExcludeDisplayInMirrorModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kExcludeDisplayInMirrorMode)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"enable-task-manager-clank", flag_descriptions::kTaskManagerClankName,
     flag_descriptions::kTaskManagerClankDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kTaskManagerClank)},
#else
    {"enable-task-manager-desktop-refresh",
     flag_descriptions::kTaskManagerDesktopRefreshName,
     flag_descriptions::kTaskManagerDesktopRefreshDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kTaskManagerDesktopRefresh)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"autofill-enable-payment-settings-card-promo-and-scan-card",
     flag_descriptions::kAutofillEnablePaymentSettingsCardPromoAndScanCardName,
     flag_descriptions::
         kAutofillEnablePaymentSettingsCardPromoAndScanCardDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillEnablePaymentSettingsCardPromoAndScanCard)},

    {"autofill-enable-payment-settings-server-card-save",
     flag_descriptions::kAutofillEnablePaymentSettingsServerCardSaveName,
     flag_descriptions::kAutofillEnablePaymentSettingsServerCardSaveDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnablePaymentSettingsServerCardSave)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"cert-verification-network-time",
     flag_descriptions::kCertVerificationNetworkTimeName,
     flag_descriptions::kCertVerificationNetworkTimeDescription,
     kOsMac | kOsWin | kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(features::kCertVerificationNetworkTime)},

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay-simplified-selection",
     flag_descriptions::kLensOverlaySimplifiedSelectionName,
     flag_descriptions::kLensOverlaySimplifiedSelectionDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlaySimplifiedSelection)},

    {"enable-lens-overlay-translate-button",
     flag_descriptions::kLensOverlayTranslateButtonName,
     flag_descriptions::kLensOverlayTranslateButtonDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayTranslateButton)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay-latency-optimizations",
     flag_descriptions::kLensOverlayLatencyOptimizationsName,
     flag_descriptions::kLensOverlayLatencyOptimizationsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayLatencyOptimizations)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay-image-context-menu-actions",
     flag_descriptions::kLensOverlayImageContextMenuActionsName,
     flag_descriptions::kLensOverlayImageContextMenuActionsDescription,
     kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         lens::features::kLensOverlayImageContextMenuActions,
         kLensOverlayImageContextMenuActionsVariations,
         "LensOverlayImageContextMenuActions")},
#endif  // !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay-updated-visuals",
     flag_descriptions::kLensOverlayUpdatedVisualsName,
     flag_descriptions::kLensOverlayUpdatedVisualsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayVisualSelectionUpdates)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"jump-start-omnibox", flag_descriptions::kJumpStartOmniboxName,
     flag_descriptions::kJumpStartOmniboxDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kJumpStartOmnibox,
                                    kJumpStartOmniboxVariations,
                                    "JumpStartOmnibox")},
    {"retain-omnibox-on-focus", flag_descriptions::kRetainOmniboxOnFocusName,
     flag_descriptions::kRetainOmniboxOnFocusDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kRetainOmniboxOnFocus)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"autofill-enable-log-form-events-to-all-parsed-form-types",
     flag_descriptions::kAutofillEnableLogFormEventsToAllParsedFormTypesName,
     flag_descriptions::
         kAutofillEnableLogFormEventsToAllParsedFormTypesDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableLogFormEventsToAllParsedFormTypes)},

    {"enable-segmentation-internals-survey",
     flag_descriptions::kSegmentationSurveyPageName,
     flag_descriptions::kSegmentationSurveyPageDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         segmentation_platform::features::kSegmentationSurveyPage)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-buy-now-pay-later",
     flag_descriptions::kAutofillEnableBuyNowPayLaterName,
     flag_descriptions::kAutofillEnableBuyNowPayLaterDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableBuyNowPayLater)},

    {"autofill-enable-buy-now-pay-later-syncing",
     flag_descriptions::kAutofillEnableBuyNowPayLaterSyncingName,
     flag_descriptions::kAutofillEnableBuyNowPayLaterSyncingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableBuyNowPayLaterSyncing)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"biometric-auth-identity-check",
     flag_descriptions::kBiometricAuthIdentityCheckName,
     flag_descriptions::kBiometricAuthIdentityCheckDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         password_manager::features::kBiometricAuthIdentityCheck)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"autofill-enable-cvc-storage-and-filling-standalone-form-enhancement",
     flag_descriptions::
         kAutofillEnableCvcStorageAndFillingStandaloneFormEnhancementName,
     flag_descriptions::
         kAutofillEnableCvcStorageAndFillingStandaloneFormEnhancementDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillEnableCvcStorageAndFillingStandaloneFormEnhancement)},
#if BUILDFLAG(IS_ANDROID)
    {"cct-sign-in-prompt", flag_descriptions::kCCTSignInPromptName,
     flag_descriptions::kCCTSignInPromptDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(switches::kCctSignInPrompt,
                                    kCctSignInPromptVariations,
                                    "CctSignInPrompt")},
#endif

    {"enable-bookmarks-selected-type-on-signin-for-testing",
     flag_descriptions::kEnableBookmarksSelectedTypeOnSigninForTestingName,
     flag_descriptions::
         kEnableBookmarksSelectedTypeOnSigninForTestingDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         syncer::kEnableBookmarksSelectedTypeOnSigninForTesting)},

#if !BUILDFLAG(IS_ANDROID)
    {"separate-local-and-account-themes",
     flag_descriptions::kSeparateLocalAndAccountThemesName,
     flag_descriptions::kSeparateLocalAndAccountThemesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(syncer::kSeparateLocalAndAccountThemes)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"clank-default-browser-promo-role-manager",
     flag_descriptions::kClankDefaultBrowserPromoRoleManagerName,
     flag_descriptions::kClankDefaultBrowserPromoRoleManagerDescription,
     kOsAndroid,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableDefaultBrowserPromo)},

    {"clank-default-browser-promo2",
     flag_descriptions::kClankDefaultBrowserPromoName,
     flag_descriptions::kClankDefaultBrowserPromoDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kDefaultBrowserPromoAndroid2)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-mall-url", flag_descriptions::kCrosMallUrlName,
     flag_descriptions::kCrosMallUrlDescription, kOsCrOS,
     STRING_VALUE_TYPE(ash::switches::kMallUrl, "")},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"autofill-enable-card-benefits-iph",
     flag_descriptions::kAutofillEnableCardBenefitsIphName,
     flag_descriptions::kAutofillEnableCardBenefitsIphDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableCardBenefitsIph)},

#if BUILDFLAG(IS_ANDROID)
    {"support-multiple-server-requests-for-pix-payments",
     flag_descriptions::kSupportMultipleServerRequestsForPixPaymentsName,
     flag_descriptions::kSupportMultipleServerRequestsForPixPaymentsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         payments::facilitated::kSupportMultipleServerRequestsForPixPayments)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(SKIA_BUILD_RUST_PNG)
    {"rusty-png", flag_descriptions::kRustyPngName,
     flag_descriptions::kRustyPngDescription, kOsAll,
     FEATURE_VALUE_TYPE(skia::kRustyPngFeature)},
#endif

    {"autofill-enable-card-info-runtime-retrieval",
     flag_descriptions::kAutofillEnableCardInfoRuntimeRetrievalName,
     flag_descriptions::kAutofillEnableCardInfoRuntimeRetrievalDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableCardInfoRuntimeRetrieval)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"translation-api", flag_descriptions::kTranslationAPIName,
     flag_descriptions::kTranslationAPIDescription, kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(blink::features::kTranslationAPI,
                                    kTranslationAPIVariations,
                                    "TranslationAPI")},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

    {"password-form-grouped-affiliations",
     flag_descriptions::kPasswordFormGroupedAffiliationsName,
     flag_descriptions::kPasswordFormGroupedAffiliationsDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kPasswordFormGroupedAffiliations)},

#if !BUILDFLAG(IS_ANDROID)
    {"privacy-guide-ai-settings",
     flag_descriptions::kPrivacyGuideAiSettingsName,
     flag_descriptions::kPrivacyGuideAiSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(optimization_guide::features::kPrivacyGuideAiSettings)},
    {"ai-settings-enterprise-disabled-ui",
     flag_descriptions::kAiSettingsPageEnterpriseDisabledName,
     flag_descriptions::kAiSettingsPageEnterpriseDisabledDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         optimization_guide::features::kAiSettingsPageEnterpriseDisabledUi)},

#endif  // !BUILDFLAG(IS_ANDROID)

    {"password-form-clientside-classifier",
     flag_descriptions::kPasswordFormClientsideClassifierName,
     flag_descriptions::kPasswordFormClientsideClassifierDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kPasswordFormClientsideClassifier)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
    {"contextual-cueing", flag_descriptions::kContextualCueingName,
     flag_descriptions::kContextualCueingDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_WITH_PARAMS_VALUE_TYPE(contextual_cueing::kContextualCueing,
                                    kContextualCueingEnabledOptions,
                                    "ContextualCueingEnabledOptions")},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

    {"partitioned-popins", flag_descriptions::kPartitionedPopinsName,
     flag_descriptions::kPartitionedPopinsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(blink::features::kPartitionedPopins)},

#if !BUILDFLAG(IS_ANDROID)
    {"separate-local-and-account-search-engines",
     flag_descriptions::kSeparateLocalAndAccountSearchEnginesName,
     flag_descriptions::kSeparateLocalAndAccountSearchEnginesDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(syncer::kSeparateLocalAndAccountSearchEngines)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
    {"partition-alloc-with-advanced-checks",
     flag_descriptions::kPartitionAllocWithAdvancedChecksName,
     flag_descriptions::kPartitionAllocWithAdvancedChecksDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         base::features::kPartitionAllocWithAdvancedChecks,
         kPartitionAllocWithAdvancedChecksEnabledProcessesOptions,
         "PartitionAllocWithAdvancedChecks")},
#endif  //  PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

    {"partition-visited-link-database-with-self-links",
     flag_descriptions::kPartitionVisitedLinkDatabaseWithSelfLinksName,
     flag_descriptions::kPartitionVisitedLinkDatabaseWithSelfLinksDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kPartitionVisitedLinkDatabaseWithSelfLinks)},

    {"predictable-reported-quota",
     flag_descriptions::kPredictableReportedQuotaName,
     flag_descriptions::kPredictableReportedQuotaDescription, kOsAll,
     FEATURE_VALUE_TYPE(storage::features::kStaticStorageQuota)},

#if BUILDFLAG(IS_ANDROID)
    {"use-ahardwarebuffer-usage-flags-from-vulkan",
     flag_descriptions::kUseHardwareBufferUsageFlagsFromVulkanName,
     flag_descriptions::kUseHardwareBufferUsageFlagsFromVulkanDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(::features::kUseHardwareBufferUsageFlagsFromVulkan)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID) && PA_BUILDFLAG(HAS_MEMORY_TAGGING) && \
    PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
    {"partition-alloc-memory-tagging",
     flag_descriptions::kPartitionAllocMemoryTaggingName,
     flag_descriptions::kPartitionAllocMemoryTaggingDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         base::features::kPartitionAllocMemoryTagging,
         kPartitionAllocMemoryTaggingEnabledProcessesOptions,
         "PartitionAllocMemoryTagging")},
#endif  // BUILDFLAG(IS_ANDROID) && PA_BUILDFLAG(HAS_MEMORY_TAGGING) &&
        // PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

#if BUILDFLAG(IS_MAC)
    {"enable-mac-a11y-api-migration",
     flag_descriptions::kMacAccessibilityAPIMigrationName,
     flag_descriptions::kMacAccessibilityAPIMigrationDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kMacAccessibilityAPIMigration)},
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"allow-legacy-mv2-extensions",
     flag_descriptions::kAllowLegacyMV2ExtensionsName,
     flag_descriptions::kAllowLegacyMV2ExtensionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(extensions_features::kAllowLegacyMV2Extensions)},
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay-translate-languages",
     flag_descriptions::kLensOverlayTranslateLanguagesName,
     flag_descriptions::kLensOverlayTranslateLanguagesDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayTranslateLanguages)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"login-db-deprecation-android",
     flag_descriptions::kLoginDbDeprecationAndroidName,
     flag_descriptions::kLoginDbDeprecationAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(
         password_manager::features::kLoginDbDeprecationAndroid)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_GLIC)
    {"glic", flag_descriptions::kGlicName, flag_descriptions::kGlicDescription,
     kOsMac | kOsWin | kOsLinux, FEATURE_VALUE_TYPE(features::kGlic)},
    {"glic-z-order-changes", flag_descriptions::kGlicZOrderChangesName,
     flag_descriptions::kGlicZOrderChangesDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kGlicZOrderChanges)},
    {"glic-actor", flag_descriptions::kGlicActorName,
     flag_descriptions::kGlicActorDescription, kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kGlicActor)},
    {"glic-panel-reset-top-chrome-button",
     flag_descriptions::kGlicPanelResetTopChromeButtonName,
     flag_descriptions::kGlicPanelResetTopChromeButtonDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicPanelResetTopChromeButton,
                                    kGlicPanelResetTopChromeButtonVariations,
                                    "GlicPanelResetTopChromeButton")},
    {"glic-panel-reset-on-start", flag_descriptions::kGlicPanelResetOnStartName,
     flag_descriptions::kGlicPanelResetOnStartDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kGlicPanelResetOnStart)},
    {"glic-panel-set-position-on-drag",
     flag_descriptions::kGlicPanelSetPositionOnDragName,
     flag_descriptions::kGlicPanelSetPositionOnDragDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(features::kGlicPanelSetPositionOnDrag)},
    {"glic-panel-reset-on-session-timeout",
     flag_descriptions::kGlicPanelResetOnSessionTimeoutName,
     flag_descriptions::kGlicPanelResetOnSessionTimeoutDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kGlicPanelResetOnSessionTimeout,
                                    kGlicPanelResetOnSessionTimeoutVariations,
                                    "GlicPanelResetOnSessionTimeout")},
#endif  // BUILDFLAG(ENABLE_GLIC)

#if BUILDFLAG(IS_ANDROID)
    {"enterprise-url-filtering-event-reporting-on-android",
     flag_descriptions::kEnterpriseUrlFilteringEventReportingOnAndroidName,
     flag_descriptions::
         kEnterpriseUrlFilteringEventReportingOnAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(enterprise_connectors::
                            kEnterpriseUrlFilteringEventReportingOnAndroid)},

    {"enterprise-security-event-reporting-on-android",
     flag_descriptions::kEnterpriseSecurityEventReportingOnAndroidName,
     flag_descriptions::kEnterpriseSecurityEventReportingOnAndroidDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         enterprise_connectors::kEnterpriseSecurityEventReportingOnAndroid)},

#endif  // BUILDFLAG(IS_ANDROID)

    {"service-worker-auto-preload",
     flag_descriptions::kServiceWorkerAutoPreloadName,
     flag_descriptions::kServiceWorkerAutoPreloadDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kServiceWorkerAutoPreload,
                                    kServiceWorkerAutoPreloadVariations,
                                    "ServiceWorkerAutoPreload")},

    {"autofill-enable-save-and-fill",
     flag_descriptions::kAutofillEnableSaveAndFillName,
     flag_descriptions::kAutofillEnableSaveAndFillDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableSaveAndFill)},

    {"autofill-improved-labels", flag_descriptions::kAutofillImprovedLabelsName,
     flag_descriptions::kAutofillImprovedLabelsDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(autofill::features::kAutofillImprovedLabels,
                                    kAutofillImprovedLabelsVariations,
                                    "AutofillImprovedLabels")},

#if BUILDFLAG(IS_ANDROID)
    {"android-appearance-settings",
     flag_descriptions::kAndroidAppearanceSettingsName,
     flag_descriptions::kAndroidAppearanceSettingsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidAppearanceSettings)},

    {"android-bookmark-bar", flag_descriptions::kAndroidBookmarkBarName,
     flag_descriptions::kAndroidBookmarkBarDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidBookmarkBar)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"cros-mall-managed", flag_descriptions::kCrosMallManagedName,
     flag_descriptions::kCrosMallManagedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kCrosMallManaged)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"happy-eyeballs-v3", flag_descriptions::kHappyEyeballsV3Name,
     flag_descriptions::kHappyEyeballsV3Description, kOsAll,
     FEATURE_VALUE_TYPE(net::features::kHappyEyeballsV3)},

#if BUILDFLAG(IS_CHROMEOS)
    {"mantis-feature-key", flag_descriptions::kMantisFeatureKeyName,
     flag_descriptions::kMantisFeatureKeyDescription, kOsCrOS,
     STRING_VALUE_TYPE(ash::switches::kMantisFeatureKey, "")},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"signature-based-sri", flag_descriptions::kSignatureBasedSriName,
     flag_descriptions::kSignatureBasedSriDescription, kOsAll,
     FEATURE_VALUE_TYPE(network::features::kSRIMessageSignatureEnforcement)},

#if !BUILDFLAG(IS_ANDROID)
    {"policy-promotion-banner-flag",
     flag_descriptions::kEnablePolicyPromotionBannerName,
     flag_descriptions::kEnablePolicyPromotionBannerDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kEnablePolicyPromotionBanner)},
#endif

    {"privacy-sandbox-ads-api-ux-enhancements",
     flag_descriptions::kPrivacySandboxAdsApiUxEnhancementsName,
     flag_descriptions::kPrivacySandboxAdsApiUxEnhancementsDescription, kOsAll,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxAdsApiUxEnhancements)},

#if !BUILDFLAG(IS_ANDROID)
    {"safety-hub-services-on-start-up",
     flag_descriptions::kSafetyHubServicesOnStartUpName,
     flag_descriptions::kSafetyHubServicesOnStartUpDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSafetyHubServicesOnStartUp)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
    {"enable-chrome-refresh-token-binding",
     flag_descriptions::kEnableChromeRefreshTokenBindingName,
     flag_descriptions::kEnableChromeRefreshTokenBindingDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(switches::kEnableChromeRefreshTokenBinding)},
#endif  // BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)

#if BUILDFLAG(IS_CHROMEOS)
    {"use-managed-print-job-options-in-print-preview",
     flag_descriptions::kUseManagedPrintJobOptionsInPrintPreviewName,
     flag_descriptions::kUseManagedPrintJobOptionsInPrintPreviewDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kUseManagedPrintJobOptionsInPrintPreview)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"autofill-third-party-mode-content-provider",
     flag_descriptions::kAutofillThirdPartyModeContentProviderName,
     flag_descriptions::kAutofillThirdPartyModeContentProviderDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillThirdPartyModeContentProvider)},
#endif

#if !BUILDFLAG(IS_ANDROID)
    {"three-button-password-save-dialog",
     flag_descriptions::kThreeButtonPasswordSaveDialogName,
     flag_descriptions::kThreeButtonPasswordSaveDialogDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kThreeButtonPasswordSaveDialog)},

#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
    {"block-root-window-accessible-name-change-event",
     flag_descriptions::kBlockRootWindowAccessibleNameChangeEventName,
     flag_descriptions::kBlockRootWindowAccessibleNameChangeEventDescription,
     kOsMac,
     FEATURE_VALUE_TYPE(::features::kBlockRootWindowAccessibleNameChangeEvent)},
#endif  // BUILDFLAG(IS_MAC)

    {"throttle-main-thread-to-60hz", flag_descriptions::kThrottleMainTo60HzName,
     flag_descriptions::kThrottleMainTo60HzDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kThrottleMainFrameTo60Hz)},

    {"client-side-detection-brand-and-page-intent",
     flag_descriptions::kClientSideDetectionBrandAndIntentForScamDetectionName,
     flag_descriptions::
         kClientSideDetectionBrandAndIntentForScamDetectionDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         safe_browsing::kClientSideDetectionBrandAndIntentForScamDetection)},

    {"client-side-detection-show-scam-verdict-warning",
     flag_descriptions::kClientSideDetectionShowScamVerdictWarningName,
     flag_descriptions::kClientSideDetectionShowScamVerdictWarningDescription,
     kOsMac | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(
         safe_browsing::kClientSideDetectionShowScamVerdictWarning)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-keyboard-used-palm-suppression",
     flag_descriptions::kEnableKeyboardUsedPalmSuppressionName,
     flag_descriptions::kEnableKeyboardUsedPalmSuppressionDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ui::kEnableKeyboardUsedPalmSuppression)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"autofill-enable-card-benefits-for-bmo",
     flag_descriptions::kAutofillEnableCardBenefitsForBmoName,
     flag_descriptions::kAutofillEnableCardBenefitsForBmoDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kAutofillEnableCardBenefitsForBmo)},

#if BUILDFLAG(IS_WIN)
    {"windows-system-tracing", flag_descriptions::kWindowsSystemTracingName,
     flag_descriptions::kWindowsSystemTracingDescription, kOsWin,
     FEATURE_VALUE_TYPE(kWindowsSystemTracing)},
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_CHROMEOS)
    {"fast-drm-master-drop", flag_descriptions::kFastDrmMasterDropName,
     flag_descriptions::kFastDrmMasterDropDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(display::features::kFastDrmMasterDrop)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"bookmarks-tree-view", flag_descriptions::kBookmarksTreeViewName,
     flag_descriptions::kBookmarksTreeViewDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kBookmarksTreeView)},
#endif

    {"enable-secure-payment-confirmation-availability-api",
     flag_descriptions::kSecurePaymentConfirmationAvailabilityAPIName,
     flag_descriptions::kSecurePaymentConfirmationAvailabilityAPIDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kSecurePaymentConfirmationAvailabilityAPI)},

    {"copy-image-filename-to-clipboard",
     flag_descriptions::kCopyImageFilenameToClipboardName,
     flag_descriptions::kCopyImageFilenameToClipboardDescription, kOsAll,
     FEATURE_VALUE_TYPE(download::features::kCopyImageFilenameToClipboard)},

    {"autofill-enable-allowlist-for-bmo-card-category-benefits",
     flag_descriptions::kAutofillEnableAllowlistForBmoCardCategoryBenefitsName,
     flag_descriptions::
         kAutofillEnableAllowlistForBmoCardCategoryBenefitsDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillEnableAllowlistForBmoCardCategoryBenefits)},
#if BUILDFLAG(IS_ANDROID)
    {"new-etc1-encoder", flag_descriptions::kNewEtc1EncoderName,
     flag_descriptions::kNewEtc1EncoderDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(ui::kUseNewEtc1Encoder)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX)
    {"automatic-usb-detach", flag_descriptions::kAutomaticUsbDetachName,
     flag_descriptions::kAutomaticUsbDetachDescription, kOsAndroid | kOsLinux,
     FEATURE_VALUE_TYPE(features::kAutomaticUsbDetach)},
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-overlay-side-panel-open-in-new-tab",
     flag_descriptions::kLensOverlaySidePanelOpenInNewTabName,
     flag_descriptions::kLensOverlaySidePanelOpenInNewTabDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlaySidePanelOpenInNewTab)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
    {"enforce-system-echo-cancellation",
     flag_descriptions::kEnforceSystemEchoCancellationName,
     flag_descriptions::kEnforceSystemEchoCancellationDescription,
     kOsWin | kOsMac,
     FEATURE_VALUE_TYPE(media::kEnforceSystemEchoCancellation)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

#if !BUILDFLAG(IS_ANDROID)
    {"improved-password-change-service",
     flag_descriptions::kImprovedPasswordChangeServiceName,
     flag_descriptions::kImprovedPasswordChangeServiceDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         password_manager::features::kImprovedPasswordChangeService)},
    {"mark-all-credentials-as-leaked",
     flag_descriptions::kMarkAllCredentialsAsLeakedName,
     flag_descriptions::kMarkAllCredentialsAsLeakedDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(
         password_manager::features::kMarkAllCredentialsAsLeaked)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"account-storage-prefs-themes-search-engines",
     flag_descriptions::kAccountStoragePrefsThemesAndSearchEnginesName,
     flag_descriptions::kAccountStoragePrefsThemesAndSearchEnginesDescription,
     kOsDesktop,
     MULTI_VALUE_TYPE(kAccountStoragePrefsThemesAndSearchEnginesChoices)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-amount-extraction-desktop",
     flag_descriptions::kAutofillEnableAmountExtractionDesktopName,
     flag_descriptions::kAutofillEnableAmountExtractionDesktopDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableAmountExtractionDesktop)},
#endif

#if !BUILDFLAG(IS_ANDROID)
    {"enable-ax-tree-fixing", flag_descriptions::kAXTreeFixingName,
     flag_descriptions::kAXTreeFixingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kAXTreeFixing)},
#endif  // !BUILDFLAG(IS_ANDROID)
    {"enable-clipboard-contents-id",
     flag_descriptions::kClipboardContentsIdName,
     flag_descriptions::kClipboardContentsIdDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kClipboardContentsId)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-amount-extraction-allowlist-desktop",
     flag_descriptions::kAutofillEnableAmountExtractionAllowlistDesktopName,
     flag_descriptions::
         kAutofillEnableAmountExtractionAllowlistDesktopDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableAmountExtractionAllowlistDesktop)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"devtools-project-settings",
     flag_descriptions::kDevToolsProjectSettingsName,
     flag_descriptions::kDevToolsProjectSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDevToolsWellKnown)},
    {"devtools-css-value-tracing",
     flag_descriptions::kDevToolsCSSValueTracingName,
     flag_descriptions::kDevToolsCSSValueTracingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kDevToolsCssValueTracing)},
    {"devtools-automatic-workspace-folders",
     flag_descriptions::kDevToolsAutomaticWorkspaceFoldersName,
     flag_descriptions::kDevToolsAutomaticWorkspaceFoldersDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(features::kDevToolsAutomaticFileSystems)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"malicious-apk-download-check",
     flag_descriptions::kMaliciousApkDownloadCheckName,
     flag_descriptions::kMaliciousApkDownloadCheckDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(safe_browsing::kMaliciousApkDownloadCheck,
                                    kMaliciousApkDownloadCheckChoices,
                                    "MaliciousApkDownloadCheck")},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {kDisableFacilitatedPaymentsMerchantAllowlistInternalName,
     flag_descriptions::kDisableFacilitatedPaymentsMerchantAllowlistName,
     flag_descriptions::kDisableFacilitatedPaymentsMerchantAllowlistDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         payments::facilitated::kDisableFacilitatedPaymentsMerchantAllowlist)},
#endif  // BUILDFLAF(IS_ANDROID)

    {"drop-input-events-while-paint-holding",
     flag_descriptions::kDropInputEventsWhilePaintHoldingName,
     flag_descriptions::kDropInputEventsWhilePaintHoldingDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kDropInputEventsWhilePaintHolding)},

#if !BUILDFLAG(IS_ANDROID)
    {"dbd-revamp-desktop", flag_descriptions::kDbdRevampDesktopName,
     flag_descriptions::kDbdRevampDesktopDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(browsing_data::features::kDbdRevampDesktop)},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"privacy-sandbox-ad-topics-content-parity",
     flag_descriptions::kPrivacySandboxAdTopicsContentParityName,
     flag_descriptions::kPrivacySandboxAdTopicsContentParityDescription, kOsAll,
     FEATURE_VALUE_TYPE(privacy_sandbox::kPrivacySandboxAdTopicsContentParity)},

#if BUILDFLAG(IS_ANDROID)
    {"enable-android-document-picture-in-picture",
     flag_descriptions::kAndroidDocumentPictureInPictureName,
     flag_descriptions::kAndroidDocumentPictureInPictureDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kDocumentPictureInPictureAPI)},

    {"enable-android-mininal-ui-large-screen",
     flag_descriptions::kAndroidMinimalUiLargeScreenName,
     flag_descriptions::kAndroidMinimalUiLargeScreenDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(webapps::features::kAndroidMinimalUiLargeScreen)},

    {"credential-management-third-party-web-api-request-forwarding",
     flag_descriptions::
         kCredentialManagementThirdPartyWebApiRequestForwardingName,
     flag_descriptions::
         kCredentialManagementThirdPartyWebApiRequestForwardingDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         credential_management::features::
             kCredentialManagementThirdPartyWebApiRequestForwarding)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"enable-android-window-popup-large-screen",
     flag_descriptions::kAndroidWindowPopupLargeScreenName,
     flag_descriptions::kAndroidWindowPopupLargeScreenDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidWindowPopupLargeScreen)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"enable-android-window-occlusion",
     flag_descriptions::kAndroidWindowOcclusionName,
     flag_descriptions::kAndroidWindowOcclusionDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(ui::kAndroidWindowOcclusion)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    {"enterprise-file-obfuscation",
     flag_descriptions::kEnterpriseFileObfuscationName,
     flag_descriptions::kEnterpriseFileObfuscationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(enterprise_obfuscation::kEnterpriseFileObfuscation)},
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)

    {"iph-autofill-credit-card-benefit-feature",
     flag_descriptions::kIPHAutofillCreditCardBenefitFeatureName,
     flag_descriptions::kIPHAutofillCreditCardBenefitFeatureDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         feature_engagement::kIPHAutofillCreditCardBenefitFeature)},

#if BUILDFLAG(IS_CHROMEOS)
    {"allow-user-installed-chrome-apps",
     flag_descriptions::kAllowUserInstalledChromeAppsName,
     flag_descriptions::kAllowUserInstalledChromeAppsDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(
         apps::chrome_app_deprecation::kAllowUserInstalledChromeApps)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {"chrome-web-store-navigation-throttle",
     flag_descriptions::kChromeWebStoreNavigationThrottleName,
     flag_descriptions::kChromeWebStoreNavigationThrottleDescription,
     kOsLinux | kOsMac | kOsWin,
     FEATURE_VALUE_TYPE(
         enterprise::webstore::kChromeWebStoreNavigationThrottle)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

    {"autofill-enable-new-fop-display-desktop",
     flag_descriptions::kAutofillEnableNewFopDisplayDesktopName,
     flag_descriptions::kAutofillEnableNewFopDisplayDesktopDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableNewFopDisplayDesktop)},

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-printing-margins-and-scale",
     flag_descriptions::kEnablePrintingMarginsAndScale,
     flag_descriptions::kEnablePrintingMarginsAndScaleDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(printing::features::kApiPrintingMarginsAndScale)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"supervised-user-block-interstitial-v3",
     flag_descriptions::kSupervisedUserBlockInterstitialV3Name,
     flag_descriptions::kSupervisedUserBlockInterstitialV3Description, kOsAll,
     FEATURE_VALUE_TYPE(supervised_user::kSupervisedUserBlockInterstitialV3)},

#if BUILDFLAG(IS_ANDROID)
    {"web-serial-over-bluetooth",
     flag_descriptions::kWebSerialOverBluetoothName,
     flag_descriptions::kWebSerialOverBluetoothDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(device::features::kBluetoothRfcommAndroid)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
    {"autofill-enable-amount-extraction-testing",
     flag_descriptions::kAutofillEnableAmountExtractionTestingName,
     flag_descriptions::kAutofillEnableAmountExtractionTestingDescription,
     kOsDesktop | kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableAmountExtractionTesting)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)

    {"enable-web-app-update-token-parsing",
     flag_descriptions::kEnableWebAppUpdateTokenParsingName,
     flag_descriptions::kEnableWebAppUpdateTokenParsingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAppEnableUpdateTokenParsing)},

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
    {"root-scrollbar-follows-browser-theme",
     flag_descriptions::kRootScrollbarFollowsTheme,
     flag_descriptions::kRootScrollbarFollowsThemeDescription,
     kOsLinux | kOsWin,
     FEATURE_VALUE_TYPE(blink::features::kRootScrollbarFollowsBrowserTheme)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_CHROMEOS)
    {"scanner-disclaimer-debug-override",
     flag_descriptions::kScannerDisclaimerDebugOverrideName,
     flag_descriptions::kScannerDisclaimerDebugOverrideDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kScannerDisclaimerDebugOverrideChoices)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"android-theme-module", flag_descriptions::kAndroidThemeModuleName,
     flag_descriptions::kAndroidThemeModuleDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kAndroidThemeModule,
                                    kAndroidThemeModuleVariations,
                                    "AndroidThemeModule")},

    {"display-edge-to-edge-fullscreen",
     flag_descriptions::kDisplayEdgeToEdgeFullscreenName,
     flag_descriptions::kDisplayEdgeToEdgeFullscreenDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kDisplayEdgeToEdgeFullscreen)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"local-network-access-check",
     flag_descriptions::kLocalNetworkAccessChecksName,
     flag_descriptions::kLocalNetworkAccessChecksDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         network::features::kLocalNetworkAccessChecks,
         kLocalNetworkAccessChecksVariations,
         "LocalNetworkAccessChecks")},

#if BUILDFLAG(IS_CHROMEOS)
    {"notebook-lm-app-preinstall",
     flag_descriptions::kNotebookLmAppPreinstallName,
     flag_descriptions::kNotebookLmAppPreinstallDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kNotebookLmAppPreinstall)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"enable-canvas-noise", flag_descriptions::kEnableCanvasNoiseName,
     flag_descriptions::kEnableCanvasNoiseDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         fingerprinting_protection_interventions::features::kCanvasNoise,
         kEnableCanvasNoiseVariations,
         "EnableCanvasNoise")},

#if !BUILDFLAG(IS_ANDROID)
    {"tab-capture-infobar-links",
     flag_descriptions::kTabCaptureInfobarLinksName,
     flag_descriptions::kTabCaptureInfobarLinksDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kTabCaptureInfobarLinks)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"enable-empty-space-context-menu-clank",
     flag_descriptions::kContextMenuEmptySpaceName,
     flag_descriptions::kContextMenuEmptySpaceDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kContextMenuEmptySpace)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"android-surface-color-update",
     flag_descriptions::kAndroidSurfaceColorUpdateName,
     flag_descriptions::kAndroidSurfaceColorUpdateDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidSurfaceColorUpdate)},
#endif  // BUILDFLAG(IS_ANDROID)
    {"zero-copy-rbp-partial-raster-with-gpu-compositor",
     flag_descriptions::kZeroCopyRBPPartialRasterWithGpuCompositorName,
     flag_descriptions::kZeroCopyRBPPartialRasterWithGpuCompositorDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kZeroCopyRBPPartialRasterWithGpuCompositor)},

#if BUILDFLAG(IS_ANDROID)
    {"input-on-viz", flag_descriptions::kInputOnVizName,
     flag_descriptions::kInputOnVizDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(input::features::kInputOnViz)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"aaudio-per-stream-device-selection",
     flag_descriptions::kAAudioPerStreamDeviceSelectionName,
     flag_descriptions::kAAudioPerStreamDeviceSelectionDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAAudioPerStreamDeviceSelection)},
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-lens-search-side-panel-new-feedback",
     flag_descriptions::kLensSearchSidePanelNewFeedbackName,
     flag_descriptions::kLensSearchSidePanelNewFeedbackDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensSearchSidePanelNewFeedback)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"headless-tab-model", flag_descriptions::kHeadlessTabModelName,
     flag_descriptions::kHeadlessTabModelDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kHeadlessTabModel)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"autofill-vcn-enroll-strike-expiry-time",
     flag_descriptions::kAutofillVcnEnrollStrikeExpiryTimeName,
     flag_descriptions::kAutofillVcnEnrollStrikeExpiryTimeDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::features::kAutofillVcnEnrollStrikeExpiryTime,
         kAutofillVcnEnrollStrikeExpiryTimeOptions,
         "AutofillVcnEnrollStrikeExpiryTime")},

#if BUILDFLAG(IS_ANDROID)
    {"autofill-enable-show-save-card-securely-message",
     flag_descriptions::kAutofillEnableShowSaveCardSecurelyMessageName,
     flag_descriptions::kAutofillEnableShowSaveCardSecurelyMessageDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableShowSaveCardSecurelyMessage)},

    {"background-compact", flag_descriptions::kBackgroundCompactMessageName,
     flag_descriptions::kBackgroundCompactDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(base::android::kShouldFreezeSelf)},

    {"running-compact", flag_descriptions::kRunningCompactMessageName,
     flag_descriptions::kRunningCompactDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(base::android::kUseRunningCompact,
                                    kUseRunningCompactDelayOptions,
                                    "UseRunningCompactDelay")},

#endif  // BUILDFLAG(IS_ANDROID)

    {"autofill-enable-flat-rate-card-benefits-from-curinos",
     flag_descriptions::kAutofillEnableFlatRateCardBenefitsFromCurinosName,
     flag_descriptions::
         kAutofillEnableFlatRateCardBenefitsFromCurinosDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableFlatRateCardBenefitsFromCurinos)},

#if BUILDFLAG(IS_ANDROID)
    {"grid-tab-switcher-surface-color-update",
     flag_descriptions::kGridTabSwitcherSurfaceColorUpdateName,
     flag_descriptions::kGridTabSwitcherSurfaceColorUpdateDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kGridTabSwitcherSurfaceColorUpdate)},

    // Contextual Page Action button.
    {"cpa-spec-update", flag_descriptions::kCpaSpecUpdateName,
     flag_descriptions::kCpaSpecUpdateDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCpaSpecUpdate)},

    // Android Automotive back button bar streamline.
    {"automotive-back-button-bar-streamline",
     flag_descriptions::kAutomotiveBackButtonBarStreamlineName,
     flag_descriptions::kAutomotiveBackButtonBarStreamlineDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAutomotiveBackButtonBarStreamline)},

#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {flag_descriptions::kTabGroupShorcutsId,
     flag_descriptions::kTabGroupShorcutsName,
     flag_descriptions::kTabGroupShorcutsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tabs::kTabGroupShortcuts)},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"bundled-security-settings",
     flag_descriptions::kBundledSecuritySettingsName,
     flag_descriptions::kBundledSecuritySettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(safe_browsing::kBundledSecuritySettings)},

    {"invalidate-search-engine-choice-on-device-restore-detection",
     flag_descriptions::
         kInvalidateSearchEngineChoiceOnDeviceRestoreDetectionName,
     flag_descriptions::
         kInvalidateSearchEngineChoiceOnDeviceRestoreDetectionDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         switches::kInvalidateSearchEngineChoiceOnDeviceRestoreDetection,
         kInvalidateSearchEngineChoiceOnRestoreVariations,
         "InvalidateSearchEngineChoiceOnDeviceRestoreDetection")},

    {"block-cross-partition-blob-url-fetching",
     flag_descriptions::kBlockCrossPartitionBlobUrlFetchingName,
     flag_descriptions::kBlockCrossPartitionBlobUrlFetchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kBlockCrossPartitionBlobUrlFetching)},

#if BUILDFLAG(IS_ANDROID)
    {"use-android-buffered-input-dispatch",
     flag_descriptions::kUseAndroidBufferedInputDispatchName,
     flag_descriptions::kUseAndroidBufferedInputDispatchDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(input::features::kUseAndroidBufferedInputDispatch)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"web-authentication-immediate-get",
     flag_descriptions::kWebAuthnImmediateGetName,
     flag_descriptions::kWebAuthnImmediateGetDescription, kOsAll,
     FEATURE_VALUE_TYPE(device::kWebAuthnImmediateGet)},

    {"media-playback-while-not-visible-permission-policy",
     flag_descriptions::kMediaPlaybackWhileNotVisiblePermissionPolicyName,
     flag_descriptions::
         kMediaPlaybackWhileNotVisiblePermissionPolicyDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         blink::features::kMediaPlaybackWhileNotVisiblePermissionPolicy)},

#if BUILDFLAG(IS_ANDROID)
    {"android-adaptive-frame-rate",
     flag_descriptions::kAndroidAdaptiveFrameRateName,
     flag_descriptions::kAndroidAdaptiveFrameRateDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kUseFrameIntervalDeciderAdaptiveFrameRate)},

    {"instance-switcher-v2", flag_descriptions::kInstanceSwitcherV2Name,
     flag_descriptions::kInstanceSwitcherV2Description, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kInstanceSwitcherV2)},
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"side-by-side", flag_descriptions::kSideBySideName,
     flag_descriptions::kSideBySideDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kSideBySide,
                                    kSideBySideVariations,
                                    "SideBySide")},
#endif

    {"enable-secure-payment-confirmation-fallback-ux",
     flag_descriptions::kSecurePaymentConfirmationFallbackName,
     flag_descriptions::kSecurePaymentConfirmationFallbackDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         payments::features::kSecurePaymentConfirmationFallback)},

#if BUILDFLAG(IS_ANDROID)
    {"android-window-management-web-api",
     flag_descriptions::kAndroidWindowManagementWebApiName,
     flag_descriptions::kAndroidWindowManagementWebApiDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(ui::kAndroidWindowManagementWebApi)},

    {"browser-controls-debugging",
     flag_descriptions::kBrowserControlsDebuggingName,
     flag_descriptions::kBrowserControlsDebuggingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kBrowserControlsDebugging)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"fwupd-developer-mode", flag_descriptions::kFwupdDeveloperModeName,
     flag_descriptions::kFwupdDeveloperModeDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(ash::features::kFwupdDeveloperMode)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"android-sms-otp-filling", flag_descriptions::kAndroidSmsOtpFillingName,
     flag_descriptions::kAndroidSmsOtpFillingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(password_manager::features::kAndroidSmsOtpFilling)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"tab-group-home", tabs::flag_descriptions::kTabGroupHomeName,
     tabs::flag_descriptions::kTabGroupHomeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(tabs::kTabGroupHome)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"pinned-tab-toast-on-close", flag_descriptions::kPinnedTabToastOnCloseName,
     flag_descriptions::kPinnedTabToastOnCloseDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(toast_features::kPinnedTabToastOnClose)},
#endif  // !BUILDFLAG(IS_ANDROID)

    {"discount-autofill", commerce::flag_descriptions::kDiscountAutofillName,
     commerce::flag_descriptions::kDiscountAutofillDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(commerce::kDiscountAutofill)},

#if BUILDFLAG(IS_ANDROID)
    {"android-web-app-launch-handler",
     flag_descriptions::kAndroidWebAppLaunchHandler,
     flag_descriptions::kAndroidWebAppLaunchHandlerDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAndroidWebAppLaunchHandler)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN)
    {"ui-automation-provider", flag_descriptions::kUiaProviderName,
     flag_descriptions::kUiaProviderDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kUiaProvider)},
#endif

#if BUILDFLAG(IS_ANDROID)
    {"top-controls-refactor", flag_descriptions::kTopControlsRefactorName,
     flag_descriptions::kTopControlsRefactorDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTopControlsRefactor)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"touch-to-search-callout", flag_descriptions::kTouchToSearchCalloutName,
     flag_descriptions::kTouchToSearchCalloutDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kTouchToSearchCallout,
                                    kTouchToSearchCalloutVariations,
                                    "TouchToSearchCallout")},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"notebook-lm-app-shelf-pin", flag_descriptions::kNotebookLmAppShelfPinName,
     flag_descriptions::kNotebookLmAppShelfPinDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kNotebookLmAppShelfPin)},
    {"notebook-lm-app-shelf-pin-reset",
     flag_descriptions::kNotebookLmAppShelfPinResetName,
     flag_descriptions::kNotebookLmAppShelfPinResetDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(chromeos::features::kNotebookLmAppShelfPinReset)},
    {"preinstalled-web-app-always-migrate-calculator",
     flag_descriptions::kPreinstalledWebAppAlwaysMigrateCalculatorName,
     flag_descriptions::kPreinstalledWebAppAlwaysMigrateCalculatorDescription,
     kOsCrOS,
     FEATURE_VALUE_TYPE(features::kPreinstalledWebAppAlwaysMigrateCalculator)},
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
    {"tablet-tab-strip-animation",
     flag_descriptions::kTabletTabStripAnimationName,
     flag_descriptions::kTabletTabStripAnimationDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kTabletTabStripAnimation)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"enable-secure-payment-confirmation-ux-refresh",
     flag_descriptions::kSecurePaymentConfirmationUxRefreshName,
     flag_descriptions::kSecurePaymentConfirmationUxRefreshDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(blink::features::kSecurePaymentConfirmationUxRefresh)},

#if BUILDFLAG(IS_ANDROID)
    {"fill-recovery-password", flag_descriptions::kFillRecoveryPasswordName,
     flag_descriptions::kFillRecoveryPasswordDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(password_manager::features::kFillRecoveryPassword)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"android-use-correct-display-work-area",
     flag_descriptions::kAndroidUseCorrectDisplayWorkAreaName,
     flag_descriptions::kAndroidUseCorrectDisplayWorkAreaDescription,
     kOsAndroid, FEATURE_VALUE_TYPE(ui::kAndroidUseCorrectDisplayWorkArea)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
    {"enable-site-search-allow-user-override-policy",
     flag_descriptions::kEnableSiteSearchAllowUserOverridePolicyName,
     flag_descriptions::kEnableSiteSearchAllowUserOverridePolicyDescription,
     static_cast<unsigned short>(kOsCrOS | kOsLinux | kOsMac | kOsWin),
     FEATURE_VALUE_TYPE(omnibox::kEnableSiteSearchAllowUserOverridePolicy)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) ||
        // BUILDFLAG(IS_CHROMEOS)
    // TODO(crbug.com/40680264): Remove this flag after regression investigation
    // is finished.
    {
        "new-content-for-checkerboarded-scrolls",
        flag_descriptions::kNewContentForCheckerboardedScrollsName,
        flag_descriptions::kNewContentForCheckerboardedScrollsDescription,
        kOsAll,
        FEATURE_VALUE_TYPE(features::kNewContentForCheckerboardedScrolls),
    },

    {"autofill-enable-multiple-request-in-virtual-card-downstream-enrollment",
     flag_descriptions::
         kAutofillEnableMultipleRequestInVirtualCardDownstreamEnrollmentName,
     flag_descriptions::
         kAutofillEnableMultipleRequestInVirtualCardDownstreamEnrollmentDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillEnableMultipleRequestInVirtualCardDownstreamEnrollment)},

#if BUILDFLAG(IS_ANDROID)
    {"mvc-update-view-when-model-changed",
     flag_descriptions::kMvcUpdateViewWhenModelChangedName,
     flag_descriptions::kMvcUpdateViewWhenModelChangedDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kMvcUpdateViewWhenModelChanged)},

    {"reload-tab-ui-resources-if-changed",
     flag_descriptions::kReloadTabUiResourcesIfChangedName,
     flag_descriptions::kReloadTabUiResourcesIfChangedDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kReloadTabUiResourcesIfChanged)},
#endif  // BUILDFLAG(IS_ANDROID)

    {"page-actions-migration", flag_descriptions::kPageActionsMigrationName,
     flag_descriptions::kPageActionsMigrationDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(features::kPageActionsMigration,
                                    kPageActionsMigrationVariations,
                                    "PageActionsMigration")},

    {"field-classification-model-caching",
     flag_descriptions::kFieldClassificationModelCachingName,
     flag_descriptions::kFieldClassificationModelCachingDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kFieldClassificationModelCaching)},

#if BUILDFLAG(IS_ANDROID)
    {"android-restore-tabs-promo-on-fre-bypassed-kill-switch",
     flag_descriptions::kAndroidShowRestoreTabsPromoOnFREBypassedKillSwitchName,
     flag_descriptions::
         kAndroidShowRestoreTabsPromoOnFREBypassedKillSwitchDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         chrome::android::kAndroidShowRestoreTabsPromoOnFREBypassedKillSwitch)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_MAC)
    {"web-authentication-large-blob-for-icloudkeychain",
     flag_descriptions::kWebAuthnLargeBlobForICloudKeychainName,
     flag_descriptions::kWebAuthnLargeBlobForICloudKeychainDescription, kOsMac,
     FEATURE_VALUE_TYPE(device::kWebAuthnLargeBlobForICloudKeychain)},
#endif  // BUILDFLAG(IS_MAC)

    {"autofill-drop-names-with-invalid-characters-for-card-upload",
     flag_descriptions::
         kAutofillDropNamesWithInvalidCharactersForCardUploadName,
     flag_descriptions::
         kAutofillDropNamesWithInvalidCharactersForCardUploadDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::
             kAutofillDropNamesWithInvalidCharactersForCardUpload)},
    {"autofill-require-cvc-for-possible-card-update",
     flag_descriptions::kAutofillRequireCvcForPossibleCardUpdateName,
     flag_descriptions::kAutofillRequireCvcForPossibleCardUpdateDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillRequireCvcForPossibleCardUpdate)},
    {"disable-autofill-strike-system",
     flag_descriptions::kDisableAutofillStrikeSystemName,
     flag_descriptions::kDisableAutofillStrikeSystemDescription, kOsAll,
     FEATURE_VALUE_TYPE(autofill::features::kDisableAutofillStrikeSystem)},

#if BUILDFLAG(IS_ANDROID)
    {"allow-non-family-link-url-filter-mode",
     flag_descriptions::kAllowNonFamilyLinkUrlFilterModeName,
     flag_descriptions::kAllowNonFamilyLinkUrlFilterModeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(supervised_user::kAllowNonFamilyLinkUrlFilterMode)},

    {"propagate-device-content-filters-to-supervised-user",
     flag_descriptions::kPropagateDeviceContentFiltersToSupervisedUserName,
     flag_descriptions::
         kPropagateDeviceContentFiltersToSupervisedUserDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         supervised_user::kPropagateDeviceContentFiltersToSupervisedUser)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"show-tab-list-animations", flag_descriptions::kShowTabListAnimationsName,
     flag_descriptions::kShowTabListAnimationsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kShowTabListAnimations)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
    {"facilitated-payments-enable-a2a-payment",
     flag_descriptions::kFacilitatedPaymentsEnableA2APaymentName,
     flag_descriptions::kFacilitatedPaymentsEnableA2APaymentDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         payments::facilitated::kFacilitatedPaymentsEnableA2APayment)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_CHROMEOS)
    {"cras-output-plugin-processor",
     flag_descriptions::kCrasOutputPluginProcessorName,
     flag_descriptions::kCrasOutputPluginProcessorDescription, kOsCrOS,
     PLATFORM_FEATURE_NAME_TYPE("CrOSLateBootCrasOutputPluginProcessor")},
#endif  // BUILDFLAG(IS_CHROMEOS)

    {"web-app-installation-api", flag_descriptions::kWebAppInstallationApiName,
     flag_descriptions::kWebAppInstallationApiDescription, kOsAll,
     FEATURE_VALUE_TYPE(blink::features::kWebAppInstallation)},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"lens-search-side-panel-default-width-change",
     flag_descriptions::kLensSearchSidePanelDefaultWidthChangeName,
     flag_descriptions::kLensSearchSidePanelDefaultWidthChangeDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         lens::features::kLensSearchSidePanelDefaultWidthChange)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

    {"transferable-resource-pass-alpha-type-directly",
     flag_descriptions::kTransferableResourcePassAlphaTypeDirectlyName,
     flag_descriptions::kTransferableResourcePassAlphaTypeDirectlyDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(features::kTransferableResourcePassAlphaTypeDirectly)},

#if BUILDFLAG(IS_ANDROID)
    {"enable-exclusive-access-manager-on-android",
     flag_descriptions::kEnableExclusiveAccessManagerName,
     flag_descriptions::kEnableExclusiveAccessManagerDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kEnableExclusiveAccessManager)},
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
    {"autofill-enable-buy-now-pay-later-for-klarna",
     flag_descriptions::kAutofillEnableBuyNowPayLaterForKlarnaName,
     flag_descriptions::kAutofillEnableBuyNowPayLaterForKlarnaDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableBuyNowPayLaterForKlarna)},

    {"autofill-enable-buy-now-pay-later-syncing-for-klarna",
     flag_descriptions::kAutofillEnableBuyNowPayLaterSyncingForKlarnaName,
     flag_descriptions::
         kAutofillEnableBuyNowPayLaterSyncingForKlarnaDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableBuyNowPayLaterSyncingForKlarna)},
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

#if !BUILDFLAG(IS_ANDROID)
    {"lens-overlay-permission-bubble-alt",
     flag_descriptions::kLensOverlayPermissionBubbleAltName,
     flag_descriptions::kLensOverlayPermissionBubbleAltDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayPermissionBubbleAlt)},
#endif  // !BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    {"web-signin-leads-to-implicitly-signed-in-state",
     flag_descriptions::kWebSigninLeadsToImplicitlySignedInStateName,
     flag_descriptions::kWebSigninLeadsToImplicitlySignedInStateDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(switches::kWebSigninLeadsToImplicitlySignedInState)},
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

    {"autofill-enable-downstream-card-awareness-iph",
     flag_descriptions::kAutofillEnableDownstreamCardAwarenessIphName,
     flag_descriptions::kAutofillEnableDownstreamCardAwarenessIphDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(
         autofill::features::kAutofillEnableDownstreamCardAwarenessIph)},

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
    {"enable-lens-overlay-back-to-page",
     flag_descriptions::kLensOverlayBackToPageName,
     flag_descriptions::kLensOverlayBackToPageDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(lens::features::kLensOverlayBackToPage)},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_ANDROID)
    {"supervised-user-interstitial-without-approvals",
     flag_descriptions::kSupervisedUserInterstitialWithoutApprovalsName,
     flag_descriptions::kSupervisedUserInterstitialWithoutApprovalsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         supervised_user::kSupervisedUserInterstitialWithoutApprovals)},
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID)
    {"enable-ntp-browser-promos",
     flag_descriptions::kEnableNtpBrowserPromosName,
     flag_descriptions::kEnableNtpBrowserPromosDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(user_education::features::kEnableNtpBrowserPromos)},
#endif

#if BUILDFLAG(IS_CHROMEOS)
    {"enable-multi-capture-notification-update",
     flag_descriptions::kMultiCaptureUsageIndicatorUpdateName,
     flag_descriptions::kMultiCaptureUsageIndicatorUpdateDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(
         chromeos::features::kMultiCaptureReworkedUsageIndicators)},
#endif  // BUILDFLAG(IS_CHROMEOS)

    // Add new entries above this line.

    // NOTE: Adding a new flag requires adding a corresponding entry to enum
    // "LoginCustomFlags" in tools/metrics/histograms/enums.xml. See "Flag
    // Histograms" in tools/metrics/histograms/README.md (run the
    // AboutFlagsHistogramTest unit test to verify this process).
};

class FlagsStateSingleton : public flags_ui::FlagsState::Delegate {
 public:
  FlagsStateSingleton()
      : flags_state_(
            std::make_unique<flags_ui::FlagsState>(kFeatureEntries, this)) {}
  FlagsStateSingleton(const FlagsStateSingleton&) = delete;
  FlagsStateSingleton& operator=(const FlagsStateSingleton&) = delete;
  ~FlagsStateSingleton() override = default;

  static FlagsStateSingleton* GetInstance() {
    return base::Singleton<FlagsStateSingleton>::get();
  }

  static flags_ui::FlagsState* GetFlagsState() {
    return GetInstance()->flags_state_.get();
  }

  void RebuildState(const std::vector<flags_ui::FeatureEntry>& entries) {
    flags_state_ = std::make_unique<flags_ui::FlagsState>(entries, this);
  }

  void RestoreDefaultState() {
    flags_state_ =
        std::make_unique<flags_ui::FlagsState>(kFeatureEntries, this);
  }

 private:
  // flags_ui::FlagsState::Delegate:
  bool ShouldExcludeFlag(const flags_ui::FlagsStorage* storage,
                         const FeatureEntry& entry) override {
    return flags::IsFlagExpired(storage, entry.internal_name);
  }

  std::unique_ptr<flags_ui::FlagsState> flags_state_;
};

bool ShouldSkipNonDeprecatedFeatureEntry(const FeatureEntry& entry) {
  return ~entry.supported_platforms & kDeprecated;
}

}  // namespace

#if BUILDFLAG(IS_CHROMEOS)
// This method may be invoked both synchronously or asynchronously. Based on
// whether the current user is the owner of the device, generates the
// appropriate flag storage.
void GetStorageAsync(Profile* profile,
                     GetStorageCallback callback,
                     bool current_user_is_owner) {
  // On ChromeOS the owner can set system wide flags and other users can only
  // set flags for their own session.
  if (current_user_is_owner) {
    ash::OwnerSettingsServiceAsh* service =
        ash::OwnerSettingsServiceAshFactory::GetForBrowserContext(profile);
    std::move(callback).Run(
        std::make_unique<ash::about_flags::OwnerFlagsStorage>(
            profile->GetPrefs(), service),
        flags_ui::kOwnerAccessToFlags);
  } else {
    std::move(callback).Run(std::make_unique<flags_ui::PrefServiceFlagsStorage>(
                                profile->GetPrefs()),
                            flags_ui::kGeneralAccessFlagsOnly);
  }
}
#endif

// ash-chrome uses different storage flag storage logic from other desktop
// platforms.
void GetStorage(Profile* profile, GetStorageCallback callback) {
#if BUILDFLAG(IS_CHROMEOS)
  // Bypass possible incognito profile.
  // On ChromeOS the owner can set system wide flags and other users can only
  // set flags for their own session.
  Profile* original_profile = profile->GetOriginalProfile();
  if (base::SysInfo::IsRunningOnChromeOS() &&
      ash::OwnerSettingsServiceAshFactory::GetForBrowserContext(
          original_profile)) {
    ash::OwnerSettingsServiceAsh* service =
        ash::OwnerSettingsServiceAshFactory::GetForBrowserContext(
            original_profile);
    service->IsOwnerAsync(base::BindOnce(&GetStorageAsync, original_profile,
                                         std::move(callback)));
  } else {
    GetStorageAsync(original_profile, std::move(callback),
                    /*current_user_is_owner=*/false);
  }
#else
  std::move(callback).Run(std::make_unique<flags_ui::PrefServiceFlagsStorage>(
                              g_browser_process->local_state()),
                          flags_ui::kOwnerAccessToFlags);
#endif
}

bool ShouldSkipConditionalFeatureEntry(const flags_ui::FlagsStorage* storage,
                                       const FeatureEntry& entry) {
#if BUILDFLAG(IS_CHROMEOS)
  version_info::Channel channel = chrome::GetChannel();
  // enable-projector-server-side-speech-recognition is only available if
  // the InternalServerSideSpeechRecognitionControl flag is enabled as well.
  if (!strcmp(kProjectorServerSideSpeechRecognition, entry.internal_name)) {
    return !ash::features::
        IsInternalServerSideSpeechRecognitionControlEnabled();
  }

  // enable-ui-devtools is only available on for non Stable channels.
  if (!strcmp(ui_devtools::switches::kEnableUiDevTools, entry.internal_name) &&
      channel == version_info::Channel::STABLE) {
    return true;
  }

  // Skip arc-enable-attestation if it is enabled by ash switch.
  if (!strcmp(kArcEnableAttestationFlag, entry.internal_name)) {
    return base::CommandLine::ForCurrentProcess()->HasSwitch(
        ash::switches::kArcEnableAttestation);
  }

  if (!strcmp(kArcEnableVirtioBlkForDataInternalName, entry.internal_name)) {
    return !arc::IsArcVmEnabled();
  }

  // Only show the Background Listening flag if channel is one of
  // Beta/Dev/Canary/Unknown (non-stable).
  if (!strcmp(kBackgroundListeningName, entry.internal_name)) {
    return channel != version_info::Channel::BETA &&
           channel != version_info::Channel::DEV &&
           channel != version_info::Channel::CANARY &&
           channel != version_info::Channel::UNKNOWN;
  }

  // Only show Borealis flags on enabled devices.
  if (!strcmp(kBorealisBigGlInternalName, entry.internal_name) ||
      !strcmp(kBorealisDGPUInternalName, entry.internal_name) ||
      !strcmp(kBorealisEnableUnsupportedHardwareInternalName,
              entry.internal_name) ||
      !strcmp(kBorealisForceBetaClientInternalName, entry.internal_name) ||
      !strcmp(kBorealisForceDoubleScaleInternalName, entry.internal_name) ||
      !strcmp(kBorealisLinuxModeInternalName, entry.internal_name) ||
      !strcmp(kBorealisPermittedInternalName, entry.internal_name) ||
      !strcmp(kBorealisProvisionInternalName, entry.internal_name) ||
      !strcmp(kBorealisScaleClientByDPIInternalName, entry.internal_name) ||
      !strcmp(kBorealisZinkGlDriverInternalName, entry.internal_name)) {
    return !base::FeatureList::IsEnabled(features::kBorealis);
  }

  // Only show wallpaper fast refresh flag if channel is one of
  // Dev/Canary/Unknown.
  if (!strcmp(kWallpaperFastRefreshInternalName, entry.internal_name)) {
    return (channel != version_info::Channel::DEV &&
            channel != version_info::Channel::CANARY &&
            channel != version_info::Channel::UNKNOWN);
  }

  // Only show clipboard history longpress flag if channel is one of
  // Beta/Dev/Canary/Unknown.
  if (!strcmp(kClipboardHistoryLongpressInternalName, entry.internal_name)) {
    return channel != version_info::Channel::BETA &&
           channel != version_info::Channel::DEV &&
           channel != version_info::Channel::CANARY &&
           channel != version_info::Channel::UNKNOWN;
  }

  // Disable and prevent users from enabling LL privacy on boards that were
  // explicitly built without floss or hardware does not support LL privacy.
  if (!strcmp(kBluetoothUseLLPrivacyInternalName, entry.internal_name)) {
    return (
        base::FeatureList::GetInstance()->IsFeatureOverriddenFromCommandLine(
            floss::features::kLLPrivacyIsAvailable.name,
            base::FeatureList::OVERRIDE_DISABLE_FEATURE));
  }

  // Only show Assistant Launcher search IPH flag if channel is one of
  // Beta/Dev/Canary/Unknown.
  if (!strcmp(kAssistantIphInternalName, entry.internal_name)) {
    return channel != version_info::Channel::BETA &&
           channel != version_info::Channel::DEV &&
           channel != version_info::Channel::CANARY &&
           channel != version_info::Channel::UNKNOWN;
  }

  // Only show Growth campaigns flag if channel is one of Beta/Dev/Canary/
  // Unknown.
  if (!strcmp(kGrowthCampaigns, entry.internal_name)) {
    return channel != version_info::Channel::BETA &&
           channel != version_info::Channel::DEV &&
           channel != version_info::Channel::CANARY &&
           channel != version_info::Channel::UNKNOWN;
  }

  // Only show Growth campaigns test tag flag if channel is one of
  // Beta/Dev/Canary/ Unknown.
  if (!strcmp(kGrowthCampaignsTestTag, entry.internal_name)) {
    return channel != version_info::Channel::BETA &&
           channel != version_info::Channel::DEV &&
           channel != version_info::Channel::CANARY &&
           channel != version_info::Channel::UNKNOWN;
  }
#endif  // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(ENABLE_EXTENSIONS)
  version_info::Channel chrome_channel = chrome::GetChannel();
  // Only show extension AI data flag in non-stable channels.
  if (!strcmp(kExtensionAiDataInternalName, entry.internal_name)) {
    return chrome_channel != version_info::Channel::BETA &&
           chrome_channel != version_info::Channel::DEV &&
           chrome_channel != version_info::Channel::CANARY &&
           chrome_channel != version_info::Channel::UNKNOWN;
  }
#endif

#if BUILDFLAG(IS_ANDROID)
  // Only show the payments test flag to disable merchant allowlists if channel
  // is one of Beta/Dev/Canary/ Unknown.
  version_info::Channel channel = chrome::GetChannel();
  if (!strcmp(kDisableFacilitatedPaymentsMerchantAllowlistInternalName,
              entry.internal_name)) {
    return channel != version_info::Channel::BETA &&
           channel != version_info::Channel::DEV &&
           channel != version_info::Channel::CANARY &&
           channel != version_info::Channel::UNKNOWN;
  }
#endif  // BUILDFLAG(IS_ANDROID)
  if (flags::IsFlagExpired(storage, entry.internal_name)) {
    return true;
  }

  return false;
}

void ConvertFlagsToSwitches(flags_ui::FlagsStorage* flags_storage,
                            base::CommandLine* command_line,
                            flags_ui::SentinelsMode sentinels) {
  if (command_line->HasSwitch(switches::kNoExperiments)) {
    return;
  }

  FlagsStateSingleton::GetFlagsState()->ConvertFlagsToSwitches(
      flags_storage, command_line, sentinels, switches::kEnableFeatures,
      switches::kDisableFeatures);
}

std::vector<std::string> RegisterAllFeatureVariationParameters(
    flags_ui::FlagsStorage* flags_storage,
    base::FeatureList* feature_list) {
  return FlagsStateSingleton::GetFlagsState()
      ->RegisterAllFeatureVariationParameters(flags_storage, feature_list);
}

void GetFlagFeatureEntries(flags_ui::FlagsStorage* flags_storage,
                           flags_ui::FlagAccess access,
                           base::Value::List& supported_entries,
                           base::Value::List& unsupported_entries) {
  FlagsStateSingleton::GetFlagsState()->GetFlagFeatureEntries(
      flags_storage, access, supported_entries, unsupported_entries,
      base::BindRepeating(&ShouldSkipConditionalFeatureEntry,
                          // Unretained: this callback doesn't outlive this
                          // stack frame.
                          base::Unretained(flags_storage)));
}

void GetFlagFeatureEntriesForDeprecatedPage(
    flags_ui::FlagsStorage* flags_storage,
    flags_ui::FlagAccess access,
    base::Value::List& supported_entries,
    base::Value::List& unsupported_entries) {
  FlagsStateSingleton::GetFlagsState()->GetFlagFeatureEntries(
      flags_storage, access, supported_entries, unsupported_entries,
      base::BindRepeating(&ShouldSkipNonDeprecatedFeatureEntry));
}

flags_ui::FlagsState* GetCurrentFlagsState() {
  return FlagsStateSingleton::GetFlagsState();
}

bool IsRestartNeededToCommitChanges() {
  return FlagsStateSingleton::GetFlagsState()->IsRestartNeededToCommitChanges();
}

void SetFeatureEntryEnabled(flags_ui::FlagsStorage* flags_storage,
                            const std::string& internal_name,
                            bool enable) {
  FlagsStateSingleton::GetFlagsState()->SetFeatureEntryEnabled(
      flags_storage, internal_name, enable);
}

void SetOriginListFlag(const std::string& internal_name,
                       const std::string& value,
                       flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->SetOriginListFlag(internal_name, value,
                                                          flags_storage);
}

void SetStringFlag(const std::string& internal_name,
                   const std::string& value,
                   flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->SetStringFlag(internal_name, value,
                                                      flags_storage);
}

void RemoveFlagsSwitches(base::CommandLine::SwitchMap* switch_list) {
  FlagsStateSingleton::GetFlagsState()->RemoveFlagsSwitches(switch_list);
}

void ResetAllFlags(flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->ResetAllFlags(flags_storage);
}

void RecordUMAStatistics(flags_ui::FlagsStorage* flags_storage,
                         const std::string& histogram_name) {
  std::set<std::string> switches;
  std::set<std::string> features;
  std::set<std::string> variation_ids;
  FlagsStateSingleton::GetFlagsState()->GetSwitchesAndFeaturesFromFlags(
      flags_storage, &switches, &features, &variation_ids);
  // Don't report variation IDs since we don't have an UMA histogram for them.
  flags_ui::ReportAboutFlagsHistogram(histogram_name, switches, features);
}

namespace testing {

std::vector<FeatureEntry>* GetEntriesForTesting() {
  static base::NoDestructor<std::vector<FeatureEntry>> entries;
  return entries.get();
}

void SetFeatureEntries(const std::vector<FeatureEntry>& entries) {
  auto* entries_for_testing = GetEntriesForTesting();  // IN-TEST
  CHECK(entries_for_testing->empty());
  for (const auto& entry : entries) {
    entries_for_testing->push_back(entry);
  }
  FlagsStateSingleton::GetInstance()->RebuildState(*entries_for_testing);
}

ScopedFeatureEntries::ScopedFeatureEntries(
    const std::vector<flags_ui::FeatureEntry>& entries) {
  SetFeatureEntries(entries);
}

ScopedFeatureEntries::~ScopedFeatureEntries() {
  GetEntriesForTesting()->clear();  // IN-TEST
  // Restore the flag state to the production flags.
  FlagsStateSingleton::GetInstance()->RestoreDefaultState();
}

base::span<const FeatureEntry> GetFeatureEntries() {
  if (const auto* entries_for_testing = GetEntriesForTesting();
      !entries_for_testing->empty()) {
    return *entries_for_testing;
  }
  return kFeatureEntries;
}

}  // namespace testing

}  // namespace about_flags
