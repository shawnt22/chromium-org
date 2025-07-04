// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CONTENT_SETTINGS_ANDROID_COOKIE_CONTROLS_BRIDGE_H_
#define COMPONENTS_CONTENT_SETTINGS_ANDROID_COOKIE_CONTROLS_BRIDGE_H_

#include <optional>

#include "base/android/jni_weak_ref.h"
#include "base/scoped_observation.h"
#include "components/content_settings/browser/ui/cookie_controls_controller.h"
#include "components/content_settings/browser/ui/cookie_controls_view.h"
#include "components/content_settings/core/common/cookie_controls_state.h"

namespace content_settings {

// Communicates between CookieControlsController (C++ backend) and PageInfoView
// (Java UI).
class CookieControlsBridge : public CookieControlsObserver {
 public:
  // Creates a CookeControlsBridge for interaction with a
  // CookieControlsController.
  CookieControlsBridge(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& obj,
      const base::android::JavaParamRef<jobject>& jweb_contents_android,
      const base::android::JavaParamRef<jobject>&
          joriginal_browser_context_handle,
      bool is_incognito_branded);

  CookieControlsBridge(const CookieControlsBridge&) = delete;
  CookieControlsBridge& operator=(const CookieControlsBridge&) = delete;

  ~CookieControlsBridge() override;

  void UpdateWebContents(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jweb_contents_android,
      const base::android::JavaParamRef<jobject>&
          joriginal_browser_context_handle,
      bool is_incognito_branded);

  // Destroys the CookieControlsBridge object. This needs to be called on the
  // java side when the object is not in use anymore.
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);

  void SetThirdPartyCookieBlockingEnabledForSite(JNIEnv* env,
                                                 bool block_cookies);

  void OnTrackingProtectionsChangedForSite(JNIEnv* env);

  void OnUiClosing(JNIEnv* env);

  void OnEntryPointAnimated(JNIEnv* env);

  // CookieControlsObserver:
  void OnStatusChanged(CookieControlsState controls_state,
                       CookieControlsEnforcement enforcement,
                       CookieBlocking3pcdStatus blocking_status,
                       base::Time expiration) override;

  void OnCookieControlsIconStatusChanged(
      bool icon_visible,
      CookieControlsState controls_state,
      CookieBlocking3pcdStatus blocking_status,
      bool should_highlight) override;

  void OnReloadThresholdExceeded() override;

 private:
  base::android::ScopedJavaGlobalRef<jobject> jobject_;
  CookieControlsState controls_state_ = CookieControlsState::kHidden;
  CookieControlsEnforcement enforcement_ =
      CookieControlsEnforcement::kNoEnforcement;
  std::optional<base::Time> expiration_;
  std::unique_ptr<CookieControlsController> controller_;
  base::ScopedObservation<CookieControlsController, CookieControlsObserver>
      observation_{this};
};

}  // namespace content_settings

#endif  // COMPONENTS_CONTENT_SETTINGS_ANDROID_COOKIE_CONTROLS_BRIDGE_H_
