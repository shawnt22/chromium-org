// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/tab_model/tab_model_jni_bridge.h"

#include <stdint.h>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/jni_weak_ref.h"
#include "base/metrics/histogram_macros.h"
#include "base/notimplemented.h"
#include "base/time/time.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"
#include "chrome/browser/ui/android/tab_model/tab_model_observer_jni_bridge.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/resource_request_body_android.h"
#include "ui/base/window_open_disposition.h"
#include "url/android/gurl_android.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/android/chrome_jni_headers/TabModelJniBridge_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::SafeGetArrayLength;
using base::android::ScopedJavaLocalRef;
using chrome::android::ActivityType;
using content::WebContents;

TabModelJniBridge::TabModelJniBridge(JNIEnv* env,
                                     const jni_zero::JavaRef<jobject>& jobj,
                                     Profile* profile,
                                     ActivityType activity_type,
                                     bool is_archived_tab_model)
    : TabModel(profile, activity_type),
      java_object_(env, jobj),
      is_archived_tab_model_(is_archived_tab_model) {
  // The archived tab model isn't tracked in native, except to comply with clear
  // browsing data.
  if (is_archived_tab_model_) {
    TabModelList::SetArchivedTabModel(this);
  } else {
    TabModelList::AddTabModel(this);
  }
}

void TabModelJniBridge::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

void TabModelJniBridge::TabAddedToModel(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj,
                                        const JavaParamRef<jobject>& jtab) {
  // Tab#initialize() should have been called by now otherwise we can't push
  // the window id.
  TabAndroid* tab = TabAndroid::GetNativeTab(env, jtab);
  if (tab) {
    tab->SetWindowSessionID(GetSessionId());
  }

  // Count tabs that are used for incognito mode inside the browser (excluding
  // off-the-record tabs for incognito CCTs, etc.).
  if (GetProfile()->IsIncognitoProfile()) {
    UMA_HISTOGRAM_COUNTS_100("Tab.Count.Incognito", GetTabCount());
  }
}

void TabModelJniBridge::DuplicateTabForTesting(JNIEnv* env,
                                               const JavaParamRef<jobject>& obj,
                                               int index) {
  DuplicateTab(index);
}

int TabModelJniBridge::GetTabCount() const {
  JNIEnv* env = AttachCurrentThread();
  return Java_TabModelJniBridge_getCount(env, java_object_.get(env));
}

int TabModelJniBridge::GetActiveIndex() const {
  JNIEnv* env = AttachCurrentThread();
  return Java_TabModelJniBridge_index(env, java_object_.get(env));
}

void TabModelJniBridge::CreateTab(TabAndroid* parent,
                                  WebContents* web_contents,
                                  bool select) {
  JNIEnv* env = AttachCurrentThread();
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());

  Java_TabModelJniBridge_createTabWithWebContents(
      env, java_object_.get(env), (parent ? parent->GetJavaObject() : nullptr),
      profile->GetJavaObject(), web_contents->GetJavaWebContents(), select);
}

void TabModelJniBridge::HandlePopupNavigation(TabAndroid* parent,
                                              NavigateParams* params) {
  DCHECK_EQ(params->source_contents, parent->web_contents());
  DCHECK(!params->contents_to_insert);
  DCHECK(!params->switch_to_singleton_tab);

  WindowOpenDisposition disposition = params->disposition;
  bool supported = disposition == WindowOpenDisposition::NEW_POPUP ||
                   disposition == WindowOpenDisposition::NEW_FOREGROUND_TAB ||
                   disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB ||
                   disposition == WindowOpenDisposition::NEW_WINDOW ||
                   disposition == WindowOpenDisposition::OFF_THE_RECORD;
  if (!supported) {
    NOTIMPLEMENTED();
    return;
  }

  const GURL& url = params->url;
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> jobj = java_object_.get(env);
  ScopedJavaLocalRef<jobject> jurl = url::GURLAndroid::FromNativeGURL(env, url);
  ScopedJavaLocalRef<jobject> jinitiator_origin =
      params->initiator_origin ? params->initiator_origin->ToJavaObject(env)
                               : nullptr;
  ScopedJavaLocalRef<jobject> jpost_data =
      content::ConvertResourceRequestBodyToJavaObject(env, params->post_data);
  Java_TabModelJniBridge_openNewTab(
      env, jobj, parent->GetJavaObject(), jurl, jinitiator_origin,
      params->extra_headers, jpost_data, static_cast<int>(disposition),
      params->opened_by_another_window, params->is_renderer_initiated);
}

WebContents* TabModelJniBridge::GetWebContentsAt(int index) const {
  TabAndroid* tab = GetTabAt(index);
  return tab == nullptr ? nullptr : tab->web_contents();
}

TabAndroid* TabModelJniBridge::GetTabAt(int index) const {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> jtab =
      Java_TabModelJniBridge_getTabAt(env, java_object_.get(env), index);

  return jtab.is_null() ? nullptr : TabAndroid::GetNativeTab(env, jtab);
}

ScopedJavaLocalRef<jobject> TabModelJniBridge::GetJavaObject() const {
  JNIEnv* env = AttachCurrentThread();
  return java_object_.get(env);
}

void TabModelJniBridge::SetActiveIndex(int index) {
  JNIEnv* env = AttachCurrentThread();
  Java_TabModelJniBridge_setIndex(env, java_object_.get(env), index);
}

void TabModelJniBridge::ForceCloseAllTabs() {
  JNIEnv* env = AttachCurrentThread();
  Java_TabModelJniBridge_forceCloseAllTabs(env, java_object_.get(env));
}

void TabModelJniBridge::CloseTabAt(int index) {
  JNIEnv* env = AttachCurrentThread();
  Java_TabModelJniBridge_closeTabAt(env, java_object_.get(env), index);
}

WebContents* TabModelJniBridge::CreateNewTabForDevTools(const GURL& url,
                                                        bool new_window) {
  // TODO(dfalcantara): Change the Java side so that it creates and returns the
  //                    WebContents, which we can load the URL on and return.
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj =
      Java_TabModelJniBridge_createNewTabForDevTools(
          env, java_object_.get(env),
          url::GURLAndroid::FromNativeGURL(env, url), new_window);
  if (obj.is_null()) {
    VLOG(0) << "Failed to create java tab";
    return nullptr;
  }
  TabAndroid* tab = TabAndroid::GetNativeTab(env, obj);
  if (!tab) {
    VLOG(0) << "Failed to create java tab";
    return nullptr;
  }
  return tab->web_contents();
}

bool TabModelJniBridge::IsSessionRestoreInProgress() const {
  JNIEnv* env = AttachCurrentThread();
  return Java_TabModelJniBridge_isSessionRestoreInProgress(
      env, java_object_.get(env));
}

bool TabModelJniBridge::IsActiveModel() const {
  JNIEnv* env = AttachCurrentThread();
  return Java_TabModelJniBridge_isActiveModel(env, java_object_.get(env));
}

void TabModelJniBridge::AddObserver(TabModelObserver* observer) {
  // If a first observer is being added then instantiate an observer bridge.
  if (!observer_bridge_) {
    JNIEnv* env = AttachCurrentThread();
    observer_bridge_ =
        std::make_unique<TabModelObserverJniBridge>(env, java_object_.get(env));
  }
  observer_bridge_->AddObserver(observer);
}

void TabModelJniBridge::RemoveObserver(TabModelObserver* observer) {
  observer_bridge_->RemoveObserver(observer);

  // Tear down the bridge if there are no observers left.
  if (!observer_bridge_->has_observers()) {
    observer_bridge_.reset();
  }
}

void TabModelJniBridge::BroadcastSessionRestoreComplete(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  if (!is_archived_tab_model_) {
    TabModel::BroadcastSessionRestoreComplete();
  }
}

int TabModelJniBridge::GetTabCountNavigatedInTimeWindow(
    const base::Time& begin_time,
    const base::Time& end_time) const {
  JNIEnv* env = AttachCurrentThread();
  int64_t begin_time_ms = begin_time.InMillisecondsSinceUnixEpoch();
  int64_t end_time_ms = end_time.InMillisecondsSinceUnixEpoch();
  return Java_TabModelJniBridge_getTabCountNavigatedInTimeWindow(
      env, java_object_.get(env), begin_time_ms, end_time_ms);
}

void TabModelJniBridge::CloseTabsNavigatedInTimeWindow(
    const base::Time& begin_time,
    const base::Time& end_time) {
  JNIEnv* env = AttachCurrentThread();
  int64_t begin_time_ms = begin_time.InMillisecondsSinceUnixEpoch();
  int64_t end_time_ms = end_time.InMillisecondsSinceUnixEpoch();
  return Java_TabModelJniBridge_closeTabsNavigatedInTimeWindow(
      env, java_object_.get(env), begin_time_ms, end_time_ms);
}

void TabModelJniBridge::OpenTab(const GURL& url, int index) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> jobj = java_object_.get(env);
  ScopedJavaLocalRef<jobject> jurl = url::GURLAndroid::FromNativeGURL(env, url);
  Java_TabModelJniBridge_openTabProgrammatically(env, jobj, jurl, index);
}

void TabModelJniBridge::DiscardTab(tabs::TabHandle tab) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
}

void TabModelJniBridge::DuplicateTab(int index) {
  WebContents* web_contents = GetWebContentsAt(index);
  if (web_contents) {
    std::unique_ptr<WebContents> cloned_web_contents = web_contents->Clone();
    ScopedJavaLocalRef<jobject> jweb_contents =
        cloned_web_contents->GetJavaWebContents();
    JNIEnv* env = AttachCurrentThread();
    ScopedJavaLocalRef<jobject> jobj = java_object_.get(env);

    Java_TabModelJniBridge_duplicateTab(env, jobj, index, jweb_contents);
    cloned_web_contents.release();
  }
}

tabs::TabInterface* TabModelJniBridge::GetTab(int index) {
  return GetTabAt(index);
}

void TabModelJniBridge::HighlightTabs(
    const std::set<tabs::TabHandle>& indicies) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
}

void TabModelJniBridge::MoveTab(int from_index, int to_index) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> jobj = java_object_.get(env);
  Java_TabModelJniBridge_moveTabToIndex(env, jobj, from_index, to_index);
}

void TabModelJniBridge::CloseTab(int index) {
  CloseTabAt(index);
}

std::vector<tabs::TabInterface*> TabModelJniBridge::GetAllTabs() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> jobj = java_object_.get(env);
  ScopedJavaLocalRef<jobjectArray> jtabs =
      Java_TabModelJniBridge_getAllTabs(env, jobj);

  std::vector<tabs::TabInterface*> tabs;
  if (jtabs) {
    size_t numTabs = SafeGetArrayLength(env, jtabs);
    tabs.reserve(numTabs);

    jobjectArray jarray = jtabs.obj();
    for (size_t i = 0; i < numTabs; i++) {
      ScopedJavaLocalRef<jobject> jtab(env,
                                       env->GetObjectArrayElement(jarray, i));
      tabs.push_back(TabAndroid::GetNativeTab(env, jtab));
    }
  }
  return tabs;
}

void TabModelJniBridge::PinTab(tabs::TabHandle tab) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
}

void TabModelJniBridge::UnpinTab(tabs::TabHandle tab) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
}

std::optional<tab_groups::TabGroupId> TabModelJniBridge::AddTabsToGroup(
    std::optional<tab_groups::TabGroupId> group_id,
    const std::set<tabs::TabHandle>& tabs) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
  return std::nullopt;
}

void TabModelJniBridge::Ungroup(const std::set<tabs::TabHandle>& tabs) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
}

void TabModelJniBridge::MoveGroupTo(tab_groups::TabGroupId group_id,
                                    int index) {
  // TODO(crbug.com/415351293): Implement.
  NOTIMPLEMENTED();
}

// static
jclass TabModelJniBridge::GetClazz(JNIEnv* env) {
  return org_chromium_chrome_browser_tabmodel_TabModelJniBridge_clazz(env);
}

TabModelJniBridge::~TabModelJniBridge() {
  if (is_archived_tab_model_) {
    TabModelList::SetArchivedTabModel(nullptr);
  } else {
    TabModelList::RemoveTabModel(this);
  }
}

static jlong JNI_TabModelJniBridge_Init(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj,
                                        Profile* profile,
                                        jint j_activity_type,
                                        unsigned char is_archived_tab_model) {
  TabModel* tab_model = new TabModelJniBridge(
      env, obj, profile, static_cast<ActivityType>(j_activity_type),
      is_archived_tab_model);
  return reinterpret_cast<intptr_t>(tab_model);
}
