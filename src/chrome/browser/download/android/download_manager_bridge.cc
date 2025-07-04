// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/android/download_manager_bridge.h"

#include <memory>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "components/download/public/common/download_features.h"
#include "url/android/gurl_android.h"
#include "url/gurl.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/browser/download/android/jni_headers/DownloadManagerBridge_jni.h"

using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using download::DownloadItem;

static void JNI_DownloadManagerBridge_OnAddCompletedDownloadDone(
    JNIEnv* env,
    jlong callback_id,
    jlong download_id) {
  DCHECK(callback_id);

  // Convert java long long int to c++ pointer, take ownership.
  std::unique_ptr<AddCompletedDownloadCallback> cb(
      reinterpret_cast<AddCompletedDownloadCallback*>(callback_id));
  std::move(*cb).Run(download_id);
}

void DownloadManagerBridge::AddCompletedDownload(
    download::DownloadItem* download,
    AddCompletedDownloadCallback callback) {
  JNIEnv* env = base::android::AttachCurrentThread();

  // Make copy on the heap so we can pass the pointer through JNI.
  intptr_t callback_id = reinterpret_cast<intptr_t>(
      new AddCompletedDownloadCallback(std::move(callback)));

  Java_DownloadManagerBridge_addCompletedDownload(env, callback_id);
}

void DownloadManagerBridge::RemoveCompletedDownload(
    download::DownloadItem* download) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_DownloadManagerBridge_removeCompletedDownload(
      env, download->GetGuid(), download->GetFileExternallyRemoved());
}
