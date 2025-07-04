// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/dump_without_crashing.h"

#include <map>
#include <utility>

#include "base/check.h"
#include "base/debug/crash_logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/trace_event/trace_event.h"
#include "build/buildflag.h"

namespace {

// Pointer to the function that's called by DumpWithoutCrashing* to dump the
// process's memory.
void (*dump_without_crashing_function_)() = nullptr;

template <typename Map, typename Key>
bool ShouldDump(Map& map, Key& key, base::TimeDelta time_between_dumps) {
  static base::NoDestructor<base::Lock> lock;
  base::AutoLock auto_lock(*lock);
  base::TimeTicks now = base::TimeTicks::Now();
  auto [it, inserted] = map.emplace(key, now);
  if (inserted) {
    return true;
  }

  if (now - it->second >= time_between_dumps) {
    it->second = now;
    return true;
  }
  return false;
}

// Map used to store the most recent time a location called
// ShouldDumpWithoutCrashWithLocation.
std::map<base::Location, base::TimeTicks>& LocationToTimestampMap() {
  static base::NoDestructor<std::map<base::Location, base::TimeTicks>>
      location_to_timestamp;
  return *location_to_timestamp;
}

// This function takes `location` and `time_between_dumps` as an input
// and checks if DumpWithoutCrashing() meets the requirements to take the dump
// or not.
bool ShouldDumpWithoutCrashWithLocation(const base::Location& location,
                                        base::TimeDelta time_between_dumps) {
  return ShouldDump(LocationToTimestampMap(), location, time_between_dumps);
}

}  // namespace

namespace base::debug {

bool DumpWithoutCrashing(const base::Location& location,
                         base::TimeDelta time_between_dumps) {
  TRACE_EVENT0("base", "DumpWithoutCrashing");
  if (dump_without_crashing_function_ &&
      ShouldDumpWithoutCrashWithLocation(location, time_between_dumps)) {
#if !BUILDFLAG(IS_NACL)
    // Record the location file and line so that in the case of corrupt stacks
    // we're still getting accurate file/line information. See
    // crbug.com/324771555.
    SCOPED_CRASH_KEY_STRING256("DumpWithoutCrashing", "file",
                               location.file_name());
    SCOPED_CRASH_KEY_NUMBER("DumpWithoutCrashing", "line",
                            location.line_number());
#endif
    (*dump_without_crashing_function_)();
    base::UmaHistogramEnumeration("Stability.DumpWithoutCrashingStatus",
                                  DumpWithoutCrashingStatus::kUploaded);
    return true;
  }
  base::UmaHistogramEnumeration("Stability.DumpWithoutCrashingStatus",
                                DumpWithoutCrashingStatus::kThrottled);
  return false;
}

void SetDumpWithoutCrashingFunction(void (*function)()) {
#if !defined(COMPONENT_BUILD)
  // In component builds, the same base is shared between modules
  // so might be initialized several times. However in non-
  // component builds this should never happen.
  DCHECK(!dump_without_crashing_function_ || !function);
#endif
  dump_without_crashing_function_ = function;
}

void ResetDumpWithoutCrashingThrottlingForTesting() {
  LocationToTimestampMap().clear();
}

}  // namespace base::debug
