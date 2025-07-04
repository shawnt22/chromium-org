// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread.h"

#include <errno.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <optional>

#include "base/android/build_info.h"
#include "base/android/jni_android.h"
#include "base/feature_list.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/threading/platform_thread_internal_posix.h"
#include "base/threading/thread_id_name_manager.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/tasks_jni/ThreadUtils_jni.h"

namespace base {

BASE_FEATURE(kIncreaseDisplayCriticalThreadPriority,
             "RaiseDisplayCriticalThreadPriority",
             base::FEATURE_DISABLED_BY_DEFAULT);

namespace internal {

// - kRealtimeAudio corresponds to Android's PRIORITY_AUDIO = -16 value.
// - kDisplay corresponds to Android's PRIORITY_DISPLAY = -4 value.
// - kUtility corresponds to Android's THREAD_PRIORITY_LESS_FAVORABLE = 1 value.
// - kBackground corresponds to Android's PRIORITY_BACKGROUND = 10
//   value. Contrary to the matching Java APi in Android <13, this does not
//   restrict the thread to (subset of) little cores.
const ThreadPriorityToNiceValuePairForTest
    kThreadPriorityToNiceValueMapForTest[7] = {
        {ThreadPriorityForTest::kRealtimeAudio, -16},
        {ThreadPriorityForTest::kDisplay, -4},
        {ThreadPriorityForTest::kNormal, 0},
        {ThreadPriorityForTest::kUtility, 1},
        {ThreadPriorityForTest::kBackground, 10},
};

// - kBackground corresponds to Android's PRIORITY_BACKGROUND = 10 value and can
// result in heavy throttling and force the thread onto a little core on
// big.LITTLE devices.
// - kUtility corresponds to Android's THREAD_PRIORITY_LESS_FAVORABLE = 1 value.
// - kDisplayCritical and kInteractive correspond to Android's PRIORITY_DISPLAY
// = -4 value.
// - kRealtimeAudio corresponds to Android's PRIORITY_AUDIO = -16 value.

int ThreadTypeToNiceValue(const ThreadType thread_type) {
  switch (thread_type) {
    case ThreadType::kBackground:
      return 10;
    case ThreadType::kUtility:
      return 1;
    case ThreadType::kDefault:
      return 0;
    case ThreadType::kDisplayCritical:
    case ThreadType::kInteractive:
      if (base::FeatureList::IsEnabled(
              kIncreaseDisplayCriticalThreadPriority)) {
        return -12;
      }
      return -4;
    case ThreadType::kRealtimeAudio:
      return -16;
  }
}

bool CanSetThreadTypeToRealtimeAudio() {
  return true;
}

bool SetCurrentThreadTypeForPlatform(ThreadType thread_type,
                                     MessagePumpType pump_type_hint) {
  // We set the Audio priority through JNI as the Java setThreadPriority will
  // put it into a preferable cgroup, whereas the "normal" C++ call wouldn't.
  // However, with
  // https://android-review.googlesource.com/c/platform/system/core/+/1975808
  // this becomes obsolete and we can avoid this starting in API level 33.
  if (thread_type == ThreadType::kRealtimeAudio &&
      base::android::BuildInfo::GetInstance()->sdk_int() <
          base::android::SDK_VERSION_T) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_ThreadUtils_setThreadPriorityAudio(env,
                                            PlatformThread::CurrentId().raw());
    return true;
  }
  // Recent versions of Android (O+) up the priority of the UI thread
  // automatically.
  if (thread_type == ThreadType::kDisplayCritical &&
      pump_type_hint == MessagePumpType::UI &&
      GetCurrentThreadNiceValue() <=
          ThreadTypeToNiceValue(ThreadType::kDisplayCritical)) {
    return true;
  }
  return false;
}

std::optional<ThreadPriorityForTest>
GetCurrentThreadPriorityForPlatformForTest() {
  JNIEnv* env = base::android::AttachCurrentThread();
  if (Java_ThreadUtils_isThreadPriorityAudio(
          env, PlatformThread::CurrentId().raw())) {
    return std::make_optional(ThreadPriorityForTest::kRealtimeAudio);
  }
  return std::nullopt;
}

}  // namespace internal

void PlatformThread::SetName(const std::string& name) {
  SetNameCommon(name);

  // Like linux, on android we can get the thread names to show up in the
  // debugger by setting the process name for the LWP.
  // We don't want to do this for the main thread because that would rename
  // the process, causing tools like killall to stop working.
  if (PlatformThread::CurrentId().raw() == getpid()) {
    return;
  }

  // Set the name for the LWP (which gets truncated to 15 characters).
  int err = prctl(PR_SET_NAME, name.c_str());
  if (err < 0 && errno != EPERM) {
    DPLOG(ERROR) << "prctl(PR_SET_NAME)";
  }
}

void InitThreading() {}

void TerminateOnThread() {
  base::android::DetachFromVM();
}

size_t GetDefaultThreadStackSize(const pthread_attr_t& attributes) {
#if !defined(ADDRESS_SANITIZER)
  return 0;
#else
  // AddressSanitizer bloats the stack approximately 2x. Default stack size of
  // 1Mb is not enough for some tests (see http://crbug.com/263749 for example).
  return 2 * (1 << 20);  // 2Mb
#endif
}

}  // namespace base
