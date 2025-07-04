// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/tab_android.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/token_android.h"
#include "base/check_is_test.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/user_metrics.h"
#include "base/notimplemented.h"
#include "base/trace_event/trace_event.h"
#include "cc/slim/layer.h"
#include "chrome/browser/android/background_tab_manager.h"
#include "chrome/browser/android/compositor/tab_content_manager.h"
#include "chrome/browser/android/tab_features.h"
#include "chrome/browser/android/tab_web_contents_delegate_android.h"
#include "chrome/browser/browser_about_handler.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/notifications/notification_permission_context.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/browser/resource_coordinator/tab_helper.h"
#include "chrome/browser/resource_coordinator/tab_load_tracker.h"
#include "chrome/browser/sync/glue/synced_tab_delegate_android.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/android/context_menu_helper.h"
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"
#include "chrome/browser/ui/autofill/chrome_autofill_client.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/startup/bad_flags_prompt.h"
#include "chrome/browser/ui/tab_helpers.h"
#include "components/android_autofill/browser/android_autofill_client.h"
#include "components/android_autofill/browser/android_autofill_manager.h"
#include "components/android_autofill/browser/android_autofill_provider.h"
#include "components/android_autofill/browser/autofill_provider.h"
#include "components/autofill/content/browser/content_autofill_driver.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_manager.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/supports_handles.h"
#include "components/tabs/public/tab_collection.h"
#include "components/tabs/public/tab_group_tab_collection.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "ui/android/view_android.h"
#include "url/android/gurl_android.h"
#include "url/gurl.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/android/chrome_jni_headers/TabImpl_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::JavaParamRef;
using base::android::JavaRef;
using chrome::android::BackgroundTabManager;
using content::GlobalRequestID;
using content::NavigationController;
using content::WebContents;

namespace {

class TabAndroidHelper : public content::WebContentsUserData<TabAndroidHelper> {
 public:
  static void SetTabForWebContents(WebContents* contents,
                                   TabAndroid* tab_android) {
    content::WebContentsUserData<TabAndroidHelper>::CreateForWebContents(
        contents);
    content::WebContentsUserData<TabAndroidHelper>::FromWebContents(contents)
        ->tab_android_ = tab_android;
  }

  static TabAndroid* FromWebContents(const WebContents* contents) {
    TabAndroidHelper* helper =
        static_cast<TabAndroidHelper*>(contents->GetUserData(UserDataKey()));
    return helper ? helper->tab_android_.get() : nullptr;
  }

  explicit TabAndroidHelper(content::WebContents* web_contents)
      : content::WebContentsUserData<TabAndroidHelper>(*web_contents) {}

 private:
  friend class content::WebContentsUserData<TabAndroidHelper>;

  raw_ptr<TabAndroid> tab_android_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabAndroidHelper);

}  // namespace

namespace tabs {

DEFINE_HANDLE_FACTORY(TabInterface);

// static
TabInterface* TabInterface::GetFromContents(
    content::WebContents* web_contents) {
  auto* tab_android = TabAndroid::FromWebContents(web_contents);
  CHECK(tab_android);
  return tab_android;
}

// static
TabInterface* MaybeGetFromContents(content::WebContents* web_contents) {
  return TabAndroid::FromWebContents(web_contents);
}

}  // namespace tabs

// static
TabAndroid* TabAndroid::FromWebContents(
    const content::WebContents* web_contents) {
  return TabAndroidHelper::FromWebContents(web_contents);
}

// static
TabAndroid* TabAndroid::GetNativeTab(JNIEnv* env, const JavaRef<jobject>& obj) {
  return reinterpret_cast<TabAndroid*>(Java_TabImpl_getNativePtr(env, obj));
}

// static
std::vector<raw_ptr<TabAndroid, VectorExperimental>>
TabAndroid::GetAllNativeTabs(
    JNIEnv* env,
    const ScopedJavaLocalRef<jobjectArray>& obj_array) {
  std::vector<raw_ptr<TabAndroid, VectorExperimental>> tab_native_ptrs;
  ScopedJavaLocalRef<jlongArray> j_tabs_ptr =
      Java_TabImpl_getAllNativePtrs(env, obj_array);
  if (j_tabs_ptr.is_null()) {
    return tab_native_ptrs;
  }

  std::vector<jlong> tab_ptr;
  base::android::JavaLongArrayToLongVector(env, j_tabs_ptr, &tab_ptr);

  for (size_t i = 0; i < tab_ptr.size(); ++i) {
    tab_native_ptrs.push_back(reinterpret_cast<TabAndroid*>(tab_ptr[i]));
  }

  return tab_native_ptrs;
}

// static
void TabAndroid::AttachTabHelpers(content::WebContents* web_contents) {
  DCHECK(web_contents);
  TabHelpers::AttachTabHelpers(web_contents);
}

TabAndroid::TabAndroid(JNIEnv* env,
                       const JavaRef<jobject>& obj,
                       Profile* profile,
                       int tab_id)
    : weak_java_tab_(env, obj),
      tab_id_(tab_id),
      session_window_id_(SessionID::InvalidValue()),
      content_layer_(cc::slim::Layer::Create()),
      synced_tab_delegate_(new browser_sync::SyncedTabDelegateAndroid(this)),
      profile_(profile->GetWeakPtr()) {
  Java_TabImpl_setNativePtr(env, obj, reinterpret_cast<intptr_t>(this));
}

TabAndroid::~TabAndroid() {
  GetContentLayer()->RemoveAllChildren();
  JNIEnv* env = base::android::AttachCurrentThread();
  const jni_zero::JavaRef<jobject>& obj = weak_java_tab_.get(env);
  if (obj) {
    Java_TabImpl_clearNativePtr(env, obj);
  }
}

// static
std::unique_ptr<TabAndroid> TabAndroid::CreateForTesting(
    Profile* profile,
    int tab_id,
    std::unique_ptr<content::WebContents> web_contents) {
  std::unique_ptr<TabAndroid> tab(new TabAndroid(profile, tab_id));
  tab->web_contents_ = std::move(web_contents);
  return tab;
}

SessionID TabAndroid::GetWindowId() const {
  return session_window_id_;
}

int TabAndroid::GetAndroidId() const {
  return tab_id_;
}

std::unique_ptr<WebContentsStateByteBuffer>
TabAndroid::GetWebContentsByteBuffer() {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> state =
      Java_TabImpl_getWebContentsStateByteBuffer(env, weak_java_tab_.get(env));
  int version = Java_TabImpl_getWebContentsStateSavedStateVersion(
      env, weak_java_tab_.get(env));

  // If the web contents is null (denoted by saved_state_version being -1),
  // return a nullptr.
  if (version == -1) {
    return nullptr;
  }

  return std::make_unique<WebContentsStateByteBuffer>(state, version);
}

base::android::ScopedJavaLocalRef<jobject> TabAndroid::GetJavaObject() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return weak_java_tab_.get(env);
}

scoped_refptr<cc::slim::Layer> TabAndroid::GetContentLayer() const {
  return content_layer_;
}

int TabAndroid::GetLaunchType() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_getLaunchType(env, weak_java_tab_.get(env));
}

int TabAndroid::GetUserAgent() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_getUserAgent(env, weak_java_tab_.get(env));
}

bool TabAndroid::IsNativePage() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_isNativePage(env, weak_java_tab_.get(env));
}

std::u16string TabAndroid::GetTitle() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_getTitle(env, weak_java_tab_.get(env));
}

GURL TabAndroid::GetURL() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return url::GURLAndroid::ToNativeGURL(
      env, Java_TabImpl_getUrl(env, weak_java_tab_.get(env)));
}

bool TabAndroid::IsUserInteractable() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_isUserInteractable(env, weak_java_tab_.get(env));
}

sync_sessions::SyncedTabDelegate* TabAndroid::GetSyncedTabDelegate() const {
  return synced_tab_delegate_.get();
}

bool TabAndroid::IsIncognito() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  const bool is_incognito =
      Java_TabImpl_isIncognito(env, weak_java_tab_.get(env));
  if (Profile* p = profile()) {
    CHECK_EQ(is_incognito, p->IsOffTheRecord());
  }
  return is_incognito;
}

base::Time TabAndroid::GetLastShownTimestamp() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  const int64_t timestamp =
      Java_TabImpl_getLastShownTimestamp(env, weak_java_tab_.get(env));
  return (timestamp == -1)
             ? base::Time()
             : base::Time::FromMillisecondsSinceUnixEpoch(timestamp);
}

int TabAndroid::GetTabLaunchTypeAtCreation() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_getTabLaunchTypeAtCreation(env, weak_java_tab_.get(env));
}

int TabAndroid::GetParentId() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_getParentId(env, weak_java_tab_.get(env));
}

void TabAndroid::DeleteFrozenNavigationEntries(
    const WebContentsState::DeletionPredicate& predicate) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_TabImpl_deleteNavigationEntriesFromFrozenState(
      env, weak_java_tab_.get(env), reinterpret_cast<intptr_t>(&predicate));
}

void TabAndroid::SetWindowSessionID(SessionID window_id) {
  session_window_id_ = window_id;

  if (!web_contents()) {
    return;
  }

  sessions::SessionTabHelper* session_tab_helper =
      sessions::SessionTabHelper::FromWebContents(web_contents());
  session_tab_helper->SetWindowID(session_window_id_);
}

std::unique_ptr<content::WebContents> TabAndroid::SwapWebContents(
    std::unique_ptr<content::WebContents> new_contents,
    bool did_start_load,
    bool did_finish_load) {
  content::WebContents* old_contents = web_contents_.get();
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_TabImpl_swapWebContents(env, weak_java_tab_.get(env),
                               new_contents->GetJavaWebContents(),
                               did_start_load, did_finish_load);
  DCHECK_EQ(web_contents_, new_contents);
  new_contents.release();
  return base::WrapUnique(old_contents);
}

bool TabAndroid::IsCustomTab() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_isCustomTab(env, weak_java_tab_.get(env));
}

bool TabAndroid::IsHidden() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_isHidden(env, weak_java_tab_.get(env));
}

void TabAndroid::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void TabAndroid::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void TabAndroid::Destroy(JNIEnv* env) {
  delete this;
}

void TabAndroid::InitWebContents(
    JNIEnv* env,
    jboolean incognito,
    jboolean is_background_tab,
    const JavaParamRef<jobject>& jweb_contents,
    const JavaParamRef<jobject>& jweb_contents_delegate,
    const JavaParamRef<jobject>& jcontext_menu_populator_factory) {
  web_contents_.reset(content::WebContents::FromJavaWebContents(jweb_contents));
  DCHECK(web_contents_.get());

  renderer_preferences_util::UpdateFromSystemSettings(
      web_contents_->GetMutableRendererPrefs(),
      Profile::FromBrowserContext(web_contents_->GetBrowserContext()));
  web_contents_->SetOwnerLocationForDebug(FROM_HERE);

  TabAndroidHelper::SetTabForWebContents(web_contents(), this);
  web_contents_delegate_ =
      std::make_unique<android::TabWebContentsDelegateAndroid>(
          env, jweb_contents_delegate);
  web_contents()->SetDelegate(web_contents_delegate_.get());

  AttachTabHelpers(web_contents_.get());
  tab_features_ =
      std::make_unique<tabs::TabFeatures>(web_contents_.get(), profile());
  // When restoring a frame that was unloaded we re-create the TabAndroid and
  // its host. This triggers visibility changes in both the Browser and
  // Renderer. We need to start tracking the content-to-visible time now. On
  // Android the tab controller does not send a visibility change until later
  // on, at which point it is too late to attempt to track tab changes for
  // unloaded frames.
  web_contents_->SetTabSwitchStartTime(
      base::TimeTicks::Now(),
      resource_coordinator::ResourceCoordinatorTabHelper::IsLoaded(
          web_contents_.get()));

  SetWindowSessionID(session_window_id_);

  ContextMenuHelper::FromWebContents(web_contents())
      ->SetPopulatorFactory(jcontext_menu_populator_factory);

  synced_tab_delegate_->SetWebContents(web_contents());

  // Verify that the WebContents this tab represents matches the expected
  // off the record state.
  CHECK_EQ(profile()->IsOffTheRecord(), incognito);

  if (is_background_tab) {
    BackgroundTabManager::CreateForWebContents(web_contents(), profile());
  }
  content_layer_->InsertChild(web_contents_->GetNativeView()->GetLayer(), 0);

  // Shows a warning notification for dangerous flags in about:flags.
  ShowBadFlagsPrompt(web_contents());

  for (Observer& observer : observers_) {
    observer.OnInitWebContents(this);
  }
}

void TabAndroid::InitializeAutofillIfNecessary(JNIEnv* env) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(autofill::AutofillProvider::FromWebContents(web_contents_.get()));
  if (autofill::ContentAutofillClient::FromWebContents(web_contents_.get())) {
    // We need to initialize the keyboard suppressor before creating any
    // AutofillManagers and after the autofill client is available.
    autofill::AutofillProvider::FromWebContents(web_contents_.get())
        ->MaybeInitKeyboardSuppressor();
    return;
  }
  android_autofill::AndroidAutofillClient::CreateForWebContents(
      web_contents_.get());

  // We need to initialize the keyboard suppressor before creating any
  // AutofillManagers and after the autofill client is available.
  autofill::AutofillProvider::FromWebContents(web_contents_.get())
      ->MaybeInitKeyboardSuppressor();

  autofill::ContentAutofillDriver::GetForRenderFrameHost(
      web_contents_->GetPrimaryMainFrame());
}

void TabAndroid::UpdateDelegates(
    JNIEnv* env,
    const JavaParamRef<jobject>& jweb_contents_delegate,
    const JavaParamRef<jobject>& jcontext_menu_populator_factory) {
  ContextMenuHelper::FromWebContents(web_contents())
      ->SetPopulatorFactory(jcontext_menu_populator_factory);
  web_contents_delegate_ =
      std::make_unique<android::TabWebContentsDelegateAndroid>(
          env, jweb_contents_delegate);
  web_contents()->SetDelegate(web_contents_delegate_.get());
}

namespace {
void WillRemoveWebContentsFromTab(content::WebContents* contents,
                                  bool clear_delegate) {
  DCHECK(contents);

  if (contents->GetNativeView()) {
    contents->GetNativeView()->GetLayer()->RemoveFromParent();
  }

  if (clear_delegate) {
    contents->SetDelegate(nullptr);
  }
}
}  // namespace

void TabAndroid::DestroyWebContents(JNIEnv* env) {
  WillRemoveWebContentsFromTab(web_contents(), /*clear_delegate=*/false);

  // Terminate the renderer process if this is the last tab.
  // If there's no unload listener, FastShutdownIfPossible kills the
  // renderer process. Otherwise, we go with the slow path where renderer
  // process shuts down itself when ref count becomes 0.
  // This helps the render process exit quickly which avoids some issues
  // during shutdown. See https://codereview.chromium.org/146693011/
  // and http://crbug.com/338709 for details.
  content::RenderProcessHost* process =
      web_contents()->GetPrimaryMainFrame()->GetProcess();
  if (process) {
    process->FastShutdownIfPossible(1, false);
  }

  tab_features_.reset();
  web_contents_.reset();

  synced_tab_delegate_->ResetWebContents();
}

void TabAndroid::ReleaseWebContents(JNIEnv* env) {
  WillRemoveWebContentsFromTab(web_contents(), /*clear_delegate=*/true);

  // Ownership of |released_contents| is assumed by the code that initiated the
  // release.
  tab_features_.reset();
  content::WebContents* released_contents = web_contents_.release();
  if (released_contents) {
    released_contents->SetOwnerLocationForDebug(std::nullopt);
  }

  // Remove the link from the native WebContents to |this|, since the
  // lifetimes of the two objects are no longer intertwined.
  TabAndroidHelper::SetTabForWebContents(released_contents, nullptr);

  synced_tab_delegate_->ResetWebContents();
}

bool TabAndroid::IsPhysicalBackingSizeEmpty(
    JNIEnv* env,
    const JavaParamRef<jobject>& jweb_contents) {
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  auto size = web_contents->GetNativeView()->GetPhysicalBackingSize();
  return size.IsEmpty();
}

void TabAndroid::OnPhysicalBackingSizeChanged(
    JNIEnv* env,
    const JavaParamRef<jobject>& jweb_contents,
    jint width,
    jint height) {
  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  gfx::Size size(width, height);
  web_contents->GetNativeView()->OnPhysicalBackingSizeChanged(size);
}

void TabAndroid::SetActiveNavigationEntryTitleForUrl(JNIEnv* env,
                                                     std::string& url,
                                                     std::u16string& title) {
  DCHECK(web_contents());

  content::NavigationEntry* entry =
      web_contents()->GetController().GetVisibleEntry();
  if (entry && url == entry->GetVirtualURL().spec()) {
    entry->SetTitle(std::move(title));
  }
}

void TabAndroid::LoadOriginalImage(JNIEnv* env) {
  content::RenderFrameHost* render_frame_host =
      web_contents()->GetFocusedFrame();
  mojo::AssociatedRemote<chrome::mojom::ChromeRenderFrame> renderer;
  render_frame_host->GetRemoteAssociatedInterfaces()->GetInterface(&renderer);
  renderer->RequestReloadImageForContextNode();
}

void TabAndroid::OnShow(JNIEnv* env) {
  // When changing tabs to one that is unloaded, the tab change notification
  // arrives before the request to InitWebContents. In that case do nothing and
  // allow initialization to record timing.
  //
  // Similarly if we are already visible do not enqueue a timing request.
  if (!web_contents_ ||
      web_contents_->GetVisibility() != content::Visibility::HIDDEN) {
    return;
  }

  // TODO(crbug.com/40868330): When a tab is backgrounded, and then brought
  // again to the foreground it's TabLoadTracker state gets stuck in LOADING.
  // This disagrees with the WebContents internal state. So for now we can only
  // trust UNLOADED. TabLoadTracker::DidStopLoading is not being called
  // correctly except for the initial load in InitWebContents.
  bool loaded =
      resource_coordinator::TabLoadTracker::Get()->GetLoadingState(
          web_contents_.get()) != mojom::LifecycleUnitLoadingState::UNLOADED &&
      !web_contents_->IsLoading();
  web_contents_->SetTabSwitchStartTime(base::TimeTicks::Now(), loaded);
}

scoped_refptr<content::DevToolsAgentHost> TabAndroid::GetDevToolsAgentHost() {
  return devtools_host_;
}

void TabAndroid::SetDevToolsAgentHost(
    scoped_refptr<content::DevToolsAgentHost> host) {
  devtools_host_ = std::move(host);
}

bool TabAndroid::IsTrustedWebActivity() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_isTrustedWebActivity(env, weak_java_tab_.get(env));
}

base::WeakPtr<TabAndroid> TabAndroid::GetTabAndroidWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

base::WeakPtr<tabs::TabInterface> TabAndroid::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

content::WebContents* TabAndroid::GetContents() const {
  return web_contents_.get();
}

void TabAndroid::Close() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_TabImpl_closeTabFromNative(env, weak_java_tab_.get(env));
}

base::CallbackListSubscription TabAndroid::RegisterWillDiscardContents(
    WillDiscardContentsCallback callback) {
  // Tab discarding is currently an OS level operation and we don't necessarily
  // get signal when this occurs.
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

bool TabAndroid::IsActivated() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_isActivated(env, weak_java_tab_.get(env));
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterDidActivate(
    DidActivateCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterWillDeactivate(
    WillDeactivateCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

bool TabAndroid::IsVisible() const {
  return !IsHidden();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterDidBecomeVisible(
    DidBecomeVisibleCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterWillBecomeHidden(
    WillBecomeHiddenCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterWillDetach(
    WillDetach callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterDidInsert(
    DidInsertCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterPinnedStateChanged(
    PinnedStateChangedCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// TODO(crbug.com/409366905): Finish TabInterface implementation.
base::CallbackListSubscription TabAndroid::RegisterGroupChanged(
    GroupChangedCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

// For now tab scoped modals should continue to be handled by the window-scoped
// ModalDialogManager class in Java.
// TODO(crbug.com/422208977): Investigate adding a capability to trigger tab
// scoped modals directly to tab.

bool TabAndroid::CanShowModalUI() const {
  NOTIMPLEMENTED();
  return false;
}

std::unique_ptr<tabs::ScopedTabModalUI> TabAndroid::ShowModalUI() {
  NOTIMPLEMENTED();
  return nullptr;
}

base::CallbackListSubscription TabAndroid::RegisterModalUIChanged(
    TabInterfaceCallback callback) {
  NOTIMPLEMENTED();
  return base::CallbackListSubscription();
}

bool TabAndroid::IsInNormalWindow() const {
  return true;
}

tabs::TabFeatures* TabAndroid::GetTabFeatures() {
  return tab_features_.get();
}

const tabs::TabFeatures* TabAndroid::GetTabFeatures() const {
  return tab_features_.get();
}

bool TabAndroid::IsPinned() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_TabImpl_getIsPinned(env, weak_java_tab_.get(env));
}

// Split tabs is currently desktop only.
bool TabAndroid::IsSplit() const {
  NOTIMPLEMENTED();
  return false;
}

std::optional<tab_groups::TabGroupId> TabAndroid::GetGroup() const {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_token =
      Java_TabImpl_getTabGroupId(env, weak_java_tab_.get(env));
  if (j_token.is_null()) {
    return std::nullopt;
  }
  return tab_groups::TabGroupId::FromRawToken(
      base::android::TokenAndroid::FromJavaToken(env, j_token));
}

// Split tabs is currently desktop only.
std::optional<split_tabs::SplitTabId> TabAndroid::GetSplit() const {
  NOTIMPLEMENTED();
  return std::nullopt;
}

tabs::TabCollection* TabAndroid::GetParentCollection(
    base::PassKey<tabs::TabCollection>) const {
  return parent_collection_;
}

const tabs::TabCollection* TabAndroid::GetParentCollection() const {
  return parent_collection_;
}

void TabAndroid::OnReparented(tabs::TabCollection* parent,
                              base::PassKey<tabs::TabCollection> pass_key) {
  parent_collection_ = parent;
  OnAncestorChanged(pass_key);
}

void TabAndroid::OnAncestorChanged(base::PassKey<tabs::TabCollection>) {
  // TODO(crbug.com/409366905): Possibly add a detached state.
  if (parent_collection_) {
    UpdateProperties();
  }
}

TabAndroid::TabAndroid(Profile* profile, int tab_id)
    : tab_id_(tab_id),
      session_window_id_(SessionID::InvalidValue()),
      content_layer_(cc::slim::Layer::Create()),
      synced_tab_delegate_(new browser_sync::SyncedTabDelegateAndroid(this)),
      profile_(profile->GetWeakPtr()) {
  CHECK_IS_TEST();
}

void TabAndroid::UpdateProperties() {
  bool pinned = false;
  std::optional<tab_groups::TabGroupId> tab_group_id = std::nullopt;

  tabs::TabCollection* ancestor = parent_collection_;
  while (ancestor) {
    switch (ancestor->type()) {
      case tabs::TabCollection::Type::PINNED:
        pinned = true;
        break;
      case tabs::TabCollection::Type::GROUP:
        tab_group_id = static_cast<tabs::TabGroupTabCollection*>(ancestor)
                           ->GetTabGroupId();
        break;
      case tabs::TabCollection::Type::SPLIT:
        // Intentional fallthrough. Split tabs is currently desktop only.
      case tabs::TabCollection::Type::TABSTRIP:
      case tabs::TabCollection::Type::UNPINNED:
        break;
    }
    ancestor = ancestor->GetParentCollection();
  }

  SetIsPinned(pinned);
  SetTabGroupId(tab_group_id);
}

void TabAndroid::SetIsPinned(bool is_pinned) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_TabImpl_setIsPinned(env, weak_java_tab_.get(env), is_pinned);
}

void TabAndroid::SetTabGroupId(
    std::optional<tab_groups::TabGroupId> tab_group_id) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> java_token;
  if (tab_group_id) {
    java_token =
        base::android::TokenAndroid::Create(env, tab_group_id->token());
  }
  Java_TabImpl_setTabGroupId(env, weak_java_tab_.get(env), java_token);
}

base::android::ScopedJavaLocalRef<jobject> JNI_TabImpl_FromWebContents(
    JNIEnv* env,
    const JavaParamRef<jobject>& jweb_contents) {
  base::android::ScopedJavaLocalRef<jobject> jtab;

  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(jweb_contents);
  TabAndroid* tab =
      web_contents ? TabAndroid::FromWebContents(web_contents) : nullptr;
  if (tab) {
    jtab = tab->GetJavaObject();
  }
  return jtab;
}

static jboolean JNI_TabImpl_HandleNonNavigationAboutURL(
    JNIEnv* env,
    const JavaParamRef<jobject>& jurl) {
  GURL url = url::GURLAndroid::ToNativeGURL(env, jurl);
  // TODO(crbug.com/418187845): Set browser context to support URL block policy.
  return HandleNonNavigationAboutURL(url, /*context=*/nullptr);
}

static void JNI_TabImpl_Init(JNIEnv* env,
                             const JavaParamRef<jobject>& obj,
                             Profile* profile,
                             jint id) {
  TRACE_EVENT0("native", "TabAndroid::Init");
  // This will automatically bind to the Java object and pass ownership there.
  new TabAndroid(env, obj, profile, id);
}
