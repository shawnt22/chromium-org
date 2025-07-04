// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_TRACING_PUBLIC_CPP_PERFETTO_PERFETTO_CONFIG_H_
#define SERVICES_TRACING_PUBLIC_CPP_PERFETTO_PERFETTO_CONFIG_H_

#include <set>
#include <string>

#include "base/component_export.h"
#include "third_party/perfetto/include/perfetto/tracing/core/chrome_config.h"
#include "third_party/perfetto/include/perfetto/tracing/core/trace_config.h"

namespace base {
namespace trace_event {
class TraceConfig;
}  // namespace trace_event
}  // namespace base

namespace tracing {

inline constexpr int kMetadataBufferSizeKb = 256;

size_t COMPONENT_EXPORT(TRACING_CPP) GetDefaultTraceBufferSize();

// Creates a perfetto trace config.
perfetto::TraceConfig COMPONENT_EXPORT(TRACING_CPP) GetDefaultPerfettoConfig(
    const base::trace_event::TraceConfig& chrome_config,
    bool privacy_filtering_enabled = false,
    bool convert_to_legacy_json = false,
    const std::string& json_agent_label_filter = "");

// Modifies |perfetto_config| to make it suitable for tracing in chrome. The
// resulting config is meant to be used for recording from chrome's internal
// tracing service. Returns true on success, or false if |perfetto_config| is
// invalid.
bool COMPONENT_EXPORT(TRACING_CPP) AdaptPerfettoConfigForChrome(
    perfetto::TraceConfig* perfetto_config,
    bool privacy_filtering_enabled = false,
    bool enable_package_name_filter = false,
    bool enable_system_backend = false);

}  // namespace tracing

#endif  // SERVICES_TRACING_PUBLIC_CPP_PERFETTO_PERFETTO_CONFIG_H_
