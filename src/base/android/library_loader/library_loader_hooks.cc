// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/library_loader/library_loader_hooks.h"

#include <string>

#include "base/android/jni_string.h"
#include "base/android/library_loader/anchor_functions_buildflags.h"
#include "base/android/library_loader/library_prefetcher.h"
#include "base/android/orderfile/orderfile_buildflags.h"
#include "base/at_exit.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/library_loader_jni/LibraryLoader_jni.h"

#if BUILDFLAG(ORDERFILE_INSTRUMENTATION)
#include "base/android/orderfile/orderfile_instrumentation.h"  // nogncheck
#endif

namespace base {
namespace android {

namespace {

base::AtExitManager* g_at_exit_manager = nullptr;
LibraryLoadedHook* g_registration_callback = nullptr;
NativeInitializationHook* g_native_initialization_hook = nullptr;
LibraryProcessType g_library_process_type = PROCESS_UNINITIALIZED;

}  // namespace

LibraryProcessType GetLibraryProcessType() {
  return g_library_process_type;
}

void SetNativeInitializationHook(
    NativeInitializationHook native_initialization_hook) {
  g_native_initialization_hook = native_initialization_hook;
}

void SetLibraryLoadedHook(LibraryLoadedHook* func) {
  g_registration_callback = func;
}

static jboolean JNI_LibraryLoader_LibraryLoaded(JNIEnv* env,
                                                jint library_process_type) {
  DCHECK_EQ(g_library_process_type, PROCESS_UNINITIALIZED);
  g_library_process_type =
      static_cast<LibraryProcessType>(library_process_type);

#if BUILDFLAG(ORDERFILE_INSTRUMENTATION)
  orderfile::StartDelayedDump();
#endif

  if (g_native_initialization_hook &&
      !g_native_initialization_hook(
          static_cast<LibraryProcessType>(library_process_type))) {
    return false;
  }
  if (g_registration_callback &&
      !g_registration_callback(
          static_cast<LibraryProcessType>(library_process_type))) {
    return false;
  }
  return true;
}

void LibraryLoaderExitHook() {
  if (g_at_exit_manager) {
    delete g_at_exit_manager;
    g_at_exit_manager = nullptr;
  }
}

void InitAtExitManager() {
  g_at_exit_manager = new base::AtExitManager();
}

}  // namespace android
}  // namespace base
