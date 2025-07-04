// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/preloading/prefetch/prefetch_params.h"

#include <string>

#include "base/command_line.h"
#include "base/metrics/field_trial_params.h"
#include "base/no_destructor.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "content/browser/preloading/prefetch/prefetch_features.h"
#include "content/browser/preloading/preloading_trigger_type_impl.h"
#include "content/browser/preloading/prerender/prerender_features.h"
#include "content/common/features.h"
#include "content/public/browser/prefetch_service_delegate.h"
#include "content/public/common/content_features.h"
#include "third_party/blink/public/mojom/speculation_rules/speculation_rules.mojom.h"

namespace content {

GURL PrefetchProxyHost(const GURL& default_proxy_url) {
  // Command line overrides take priority.
  std::string cmd_line_value =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          "isolated-prerender-tunnel-proxy");
  if (!cmd_line_value.empty()) {
    GURL cmd_line_url(cmd_line_value);
    if (cmd_line_url.is_valid()) {
      return cmd_line_url;
    }
    LOG(ERROR) << "--isolated-prerender-tunnel-proxy value is invalid";
  }

  return default_proxy_url;
}

std::string PrefetchProxyServerExperimentGroup() {
  return base::GetFieldTrialParamValueByFeature(
      features::kPrefetchUseContentRefactor, "server_experiment_group");
}

bool PrefetchAllowAllDomains() {
  return base::GetFieldTrialParamByFeatureAsBool(
             features::kPrefetchUseContentRefactor, "allow_all_domains",
             false) ||
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             "isolated-prerender-allow-all-domains");
}

bool PrefetchAllowAllDomainsForExtendedPreloading() {
  return base::GetFieldTrialParamByFeatureAsBool(
      features::kPrefetchUseContentRefactor,
      "allow_all_domains_for_extended_preloading", true);
}

bool PrefetchServiceSendDecoyRequestForIneligblePrefetch(
    bool disabled_based_on_user_settings) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "prefetch-proxy-never-send-decoy-requests-for-testing")) {
    return false;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "prefetch-proxy-always-send-decoy-requests-for-testing")) {
    return true;
  }

  if (base::GetFieldTrialParamByFeatureAsBool(
          features::kPrefetchUseContentRefactor,
          "disable_decoys_based_on_user_settings", true) &&
      disabled_based_on_user_settings) {
    return false;
  }

  double probability = base::GetFieldTrialParamByFeatureAsDouble(
      features::kPrefetchUseContentRefactor,
      "ineligible_decoy_request_probability", 1.0);

  // Clamp to [0.0, 1.0].
  probability = std::max(0.0, probability);
  probability = std::min(1.0, probability);

  // RandDouble returns [0.0, 1.0) so don't use <= here since that may return
  // true when the probability is supposed to be 0 (i.e.: always false).
  return base::RandDouble() < probability;
}

base::TimeDelta PrefetchTimeoutDuration() {
  return base::Milliseconds(base::GetFieldTrialParamByFeatureAsInt(
      features::kPrefetchUseContentRefactor, "prefetch_timeout_ms",
      10 * 1000 /* 10 seconds */));
}

size_t PrefetchMainframeBodyLengthLimit() {
  return 1024 * base::GetFieldTrialParamByFeatureAsInt(
                    features::kPrefetchUseContentRefactor,
                    "max_mainframe_body_length_kb", 5 * 1024);
}

bool PrefetchCloseIdleSockets() {
  return base::GetFieldTrialParamByFeatureAsBool(
      features::kPrefetchUseContentRefactor, "close_idle_sockets", true);
}

bool PrefetchStartsSpareRenderer() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
             "isolated-prerender-start-spare-renderer") ||
         base::GetFieldTrialParamByFeatureAsBool(
             features::kPrefetchUseContentRefactor, "start_spare_renderer",
             true);
}

base::TimeDelta PrefetchContainerDefaultTtlInPrefetchService() {
  // A value of 0 or less, indicates that |PrefetchService| should keep the
  // prefetch forever.
  return base::Seconds(base::GetFieldTrialParamByFeatureAsInt(
      features::kPrefetchUseContentRefactor, "prefetch_container_lifetime_s",
      10 * 60 /* 10 minutes */));
}

bool PrefetchServiceHTMLOnly() {
  return base::GetFieldTrialParamByFeatureAsBool(
      features::kPrefetchUseContentRefactor, "html_only", false);
}

bool ShouldPrefetchBypassProxyForTestHost(std::string_view host) {
  static const base::NoDestructor<std::string> bypass(
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          "bypass-prefetch-proxy-for-host"));
  if (bypass->empty()) {
    return false;
  }
  return host == *bypass;
}

base::TimeDelta PrefetchCacheableDuration() {
  return base::Seconds(base::GetFieldTrialParamByFeatureAsInt(
      features::kPrefetchUseContentRefactor, "cacheable_duration", 300));
}

bool PrefetchProbingEnabled() {
  return base::GetFieldTrialParamByFeatureAsBool(
      features::kPrefetchCanaryCheckerParams, "must_probe_origin", true);
}

bool PrefetchCanaryCheckEnabled() {
  return base::GetFieldTrialParamByFeatureAsBool(
      features::kPrefetchCanaryCheckerParams, "do_canary", true);
}

bool PrefetchTLSCanaryCheckEnabled() {
  return base::GetFieldTrialParamByFeatureAsBool(
      features::kPrefetchCanaryCheckerParams, "do_tls_canary", false);
}

GURL PrefetchTLSCanaryCheckURL(const GURL& default_tls_canary_check_url) {
  GURL url(base::GetFieldTrialParamValueByFeature(
      features::kPrefetchCanaryCheckerParams, "tls_canary_url"));
  if (url.is_valid())
    return url;

  return default_tls_canary_check_url;
}

GURL PrefetchDNSCanaryCheckURL(const GURL& default_dns_canary_check_url) {
  GURL url(base::GetFieldTrialParamValueByFeature(
      features::kPrefetchCanaryCheckerParams, "dns_canary_url"));
  if (url.is_valid())
    return url;

  return default_dns_canary_check_url;
}

base::TimeDelta PrefetchCanaryCheckCacheLifetime() {
  return base::Hours(base::GetFieldTrialParamByFeatureAsInt(
      features::kPrefetchCanaryCheckerParams, "canary_cache_hours", 24));
}

base::TimeDelta PrefetchCanaryCheckTimeout() {
  return base::Milliseconds(base::GetFieldTrialParamByFeatureAsInt(
      features::kPrefetchCanaryCheckerParams, "canary_check_timeout_ms",
      5 * 1000 /* 5 seconds */));
}

int PrefetchCanaryCheckRetries() {
  return base::GetFieldTrialParamByFeatureAsInt(
      features::kPrefetchCanaryCheckerParams, "canary_check_retries", 1);
}

base::TimeDelta PrefetchBlockUntilHeadTimeout(
    const PrefetchType& prefetch_type,
    bool should_disable_block_until_head_timeout,
    bool is_nav_prerender) {
  // If the caller of prefetches requests to disable the timeout, follow that.
  if (should_disable_block_until_head_timeout) {
    return base::Seconds(0);
  }

  // Don't set a timeout for prerender because
  //
  // - The intention of prefetch ahead of prerender is not sending additional
  //   fetch request. The options of the behavior of the timeout case are
  //   1. (Current behavior) Making prerender fail, or 2. Falling back to
  //   network.
  // - 1 reduces the prerender activation rate.
  //
  // For more details, see
  // https://docs.google.com/document/d/1ZP7lYrtqZL9jC2xXieNY_UBMJL1sCrfmzTB8K6v4sD4/edit?resourcekey=0-fkbeQhkT3PhBb9FnnPgnZA&tab=t.wphan8fb23kr
  if (!features::kPrerender2FallbackPrefetchUseBlockUntilHeadTimetout.Get() &&
      is_nav_prerender) {
    return base::Seconds(0);
  }

  int timeout_in_milliseconds = 0;
  if (IsSpeculationRuleType(prefetch_type.trigger_type())) {
    switch (prefetch_type.GetEagerness()) {
      // TODO(crbug.com/40287486): Create a specific param for `kEager`.
      case blink::mojom::SpeculationEagerness::kImmediate:
      case blink::mojom::SpeculationEagerness::kEager:
        timeout_in_milliseconds = base::GetFieldTrialParamByFeatureAsInt(
            features::kPrefetchUseContentRefactor,
            "block_until_head_timeout_immediate_prefetch", 1000);
        break;
      case blink::mojom::SpeculationEagerness::kModerate:
        timeout_in_milliseconds = base::GetFieldTrialParamByFeatureAsInt(
            features::kPrefetchUseContentRefactor,
            "block_until_head_timeout_moderate_prefetch", 0);
        break;
      case blink::mojom::SpeculationEagerness::kConservative:
        timeout_in_milliseconds = base::GetFieldTrialParamByFeatureAsInt(
            features::kPrefetchUseContentRefactor,
            "block_until_head_timeout_conservative_prefetch", 0);
        break;
    }
  } else {
    timeout_in_milliseconds = base::GetFieldTrialParamByFeatureAsInt(
        features::kPrefetchUseContentRefactor,
        "block_until_head_timeout_embedder_prefetch", 1000);
  }
  return base::Milliseconds(timeout_in_milliseconds);
}

// These strings (including `embedder_histogram_suffix`) are persisted to logs.
// `kEager` is treated the same as `kImmediate` here for historical reasons.
// TODO(crbug.com/40287486): Change records for `kEager` or create updated
// metrics that handle `kEager` separately.
// LINT.IfChange
std::string GetMetricsSuffixTriggerTypeAndEagerness(
    const PrefetchType prefetch_type,
    const std::optional<std::string>& embedder_histogram_suffix) {
  switch (prefetch_type.trigger_type()) {
    case PreloadingTriggerType::kSpeculationRule:
      switch (prefetch_type.GetEagerness()) {
        case blink::mojom::SpeculationEagerness::kImmediate:
        case blink::mojom::SpeculationEagerness::kEager:
          return "SpeculationRule_Immediate";
        case blink::mojom::SpeculationEagerness::kModerate:
          return "SpeculationRule_Moderate";
        case blink::mojom::SpeculationEagerness::kConservative:
          return "SpeculationRule_Conservative";
      }
    case PreloadingTriggerType::kSpeculationRuleFromIsolatedWorld:
      switch (prefetch_type.GetEagerness()) {
        case blink::mojom::SpeculationEagerness::kImmediate:
        case blink::mojom::SpeculationEagerness::kEager:
          return "SpeculationRuleFromIsolatedWorld_Immediate";
        case blink::mojom::SpeculationEagerness::kModerate:
          return "SpeculationRuleFromIsolatedWorld_Moderate";
        case blink::mojom::SpeculationEagerness::kConservative:
          return "SpeculationRuleFromIsolatedWorld_Conservative";
      }
    case PreloadingTriggerType::kSpeculationRuleFromAutoSpeculationRules:
      switch (prefetch_type.GetEagerness()) {
        case blink::mojom::SpeculationEagerness::kImmediate:
        case blink::mojom::SpeculationEagerness::kEager:
          return "SpeculationRuleFromAutoSpeculationRules_Immediate";
        case blink::mojom::SpeculationEagerness::kModerate:
          return "SpeculationRuleFromAutoSpeculationRules_Moderate";
        case blink::mojom::SpeculationEagerness::kConservative:
          return "SpeculationRuleFromAutoSpeculationRules_Conservative";
      }
    case PreloadingTriggerType::kEmbedder:
      CHECK(!embedder_histogram_suffix.value().empty());
      return base::StrCat({"Embedder_", embedder_histogram_suffix.value()});
  }
}
// LINT.ThenChange(//tools/metrics/histograms/metadata/prefetch/histograms.xml:TriggerTypeAndEagerness)

bool PrefetchNIKScopeEnabled() {
  return base::FeatureList::IsEnabled(features::kPrefetchNIKScope);
}

bool PrefetchBrowserInitiatedTriggersEnabled() {
  return base::FeatureList::IsEnabled(
      features::kPrefetchBrowserInitiatedTriggers);
}

size_t GetPrefetchDataPipeTeeBodySizeLimit() {
  return std::max(
      static_cast<size_t>(features::kPrefetchReusableBodySizeLimit.Get()),
      features::kPrerender2FallbackBodySizeLimit.Get());
}

bool UsePrefetchScheduler() {
  return base::FeatureList::IsEnabled(features::kPrefetchScheduler) ||
         features::kPrerender2FallbackPrefetchSchedulerPolicy.Get() !=
             features::Prerender2FallbackPrefetchSchedulerPolicy::kNotUse ||
         base::FeatureList::IsEnabled(
             features::kWebViewPrefetchHighestPrefetchPriority) ||
         base::FeatureList::IsEnabled(
             features::kPrefetchMultipleActiveSetSizeLimitForBase);
}

}  // namespace content
