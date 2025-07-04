// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/service_tab_launcher.h"

#include <memory>

#include "base/android/jni_string.h"
#include "base/functional/callback.h"
#include "base/notimplemented.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "url/android/gurl_android.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/android/chrome_jni_headers/ServiceTabLauncher_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

// Called by Java when the WebContents instance for a request Id is available.
void JNI_ServiceTabLauncher_OnWebContentsForRequestAvailable(
    JNIEnv* env,
    jint request_id,
    const JavaParamRef<jobject>& android_web_contents) {
  ServiceTabLauncher::GetInstance()->OnTabLaunched(
      request_id,
      content::WebContents::FromJavaWebContents(android_web_contents));
}

// static
ServiceTabLauncher* ServiceTabLauncher::GetInstance() {
  return base::Singleton<ServiceTabLauncher>::get();
}

ServiceTabLauncher::ServiceTabLauncher() = default;

ServiceTabLauncher::~ServiceTabLauncher() = default;

void ServiceTabLauncher::LaunchTab(content::BrowserContext* browser_context,
                                   const content::OpenURLParams& params,
                                   TabLaunchedCallback callback) {
  WindowOpenDisposition disposition = params.disposition;
  if (disposition != WindowOpenDisposition::NEW_WINDOW &&
      disposition != WindowOpenDisposition::NEW_POPUP &&
      disposition != WindowOpenDisposition::NEW_FOREGROUND_TAB &&
      disposition != WindowOpenDisposition::NEW_BACKGROUND_TAB) {
    // ServiceTabLauncher can currently only launch new tabs.
    NOTIMPLEMENTED();
    return;
  }

  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> post_data;

  // IDMap requires a pointer, so we move |callback| into a heap pointer.
  int request_id = tab_launched_callbacks_.Add(
      std::make_unique<TabLaunchedCallback>(std::move(callback)));
  DCHECK_GE(request_id, 1);

  Java_ServiceTabLauncher_launchTab(
      env, request_id, browser_context->IsOffTheRecord(),
      url::GURLAndroid::FromNativeGURL(env, params.url),
      static_cast<int>(disposition), params.referrer.url.spec(),
      static_cast<int>(params.referrer.policy), params.extra_headers,
      post_data);
}

void ServiceTabLauncher::OnTabLaunched(int request_id,
                                       content::WebContents* web_contents) {
  TabLaunchedCallback* callback = tab_launched_callbacks_.Lookup(request_id);
  // TODO(crbug.com/41458698): The Lookup() can fail though we don't expect that
  // it should be able to. It would be nice if this was a DCHECK() instead.
  if (callback)
    std::move(*callback).Run(web_contents);
  tab_launched_callbacks_.Remove(request_id);
}
