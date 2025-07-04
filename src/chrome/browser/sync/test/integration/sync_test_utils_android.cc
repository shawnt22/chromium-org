// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/test/integration/sync_test_utils_android.h"

#include <memory>
#include <optional>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/token_android.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/browser/ui/android/tab_model/tab_model_jni_bridge.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"
#include "components/saved_tab_groups/public/android/tab_group_sync_conversions_bridge.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/signin/public/identity_manager/account_capabilities_test_mutator.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/signin/public/identity_manager/signin_constants.h"
#include "google_apis/gaia/core_account_id.h"
#include "google_apis/gaia/gaia_id.h"
#include "url/android/gurl_android.h"
#include "url/gurl.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/test/sync_integration_test_support_jni_headers/SyncTestSigninUtils_jni.h"
#include "chrome/test/sync_integration_test_support_jni_headers/SyncTestTabGroupHelpers_jni.h"

namespace sync_test_utils_android {

namespace {

AccountInfo GetFakeAccountInfo(
    const std::string& username,
    const std::optional<std::string>& hosted_domain) {
  AccountInfo account_info;
  account_info.email = username;
  account_info.gaia = signin::GetTestGaiaIdForEmail(username);
  account_info.account_id = CoreAccountId::FromGaiaId(account_info.gaia);
  account_info =
      signin::WithGeneratedUserInfo(account_info, /*given_name=*/"Fake");
  account_info.hosted_domain =
      hosted_domain.value_or(signin::constants::kNoHostedDomainFound);
  bool managed = false;
  if (hosted_domain.has_value() && !hosted_domain.value().empty()) {
    managed = hosted_domain.value() !=
                  signin::constants::kNoHostedDomainFound;
  }
  AccountCapabilitiesTestMutator(&account_info.capabilities)
      .set_is_subject_to_enterprise_policies(managed);
  return account_info;
}

}  // namespace

void SetUpFakeAccountAndSignInForTesting(
    const std::string& username,
    const std::optional<std::string>& hosted_domain,
    signin::ConsentLevel consent_level) {
  base::RunLoop run_loop;
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()}, base::BindLambdaForTesting([&]() {
        Java_SyncTestSigninUtils_setUpAccountAndSignInForTesting(
            base::android::AttachCurrentThread(),
            GetFakeAccountInfo(username, hosted_domain),
            static_cast<int>(consent_level));
        run_loop.Quit();
      }));
  run_loop.Run();
}

void SignOutForTesting() {
  base::RunLoop run_loop;
  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()},
                             base::BindLambdaForTesting([&]() {
                               Java_SyncTestSigninUtils_signOutForTesting(
                                   base::android::AttachCurrentThread());
                               run_loop.Quit();
                             }));
  run_loop.Run();
}

void SetUpFakeAuthForTesting() {
  Java_SyncTestSigninUtils_setUpFakeAuthForTesting(
      base::android::AttachCurrentThread());
}

void TearDownFakeAuthForTesting() {
  base::RunLoop run_loop;
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()}, base::BindLambdaForTesting([&]() {
        Java_SyncTestSigninUtils_tearDownFakeAuthForTesting(
            base::android::AttachCurrentThread());
        run_loop.Quit();
      }));
  run_loop.Run();
}

void SetUpLiveAccountAndSignInForTesting(const std::string& username,
                                         const std::string& password,
                                         signin::ConsentLevel consent_level) {
  base::RunLoop run_loop;
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()}, base::BindLambdaForTesting([&]() {
        JNIEnv* env = base::android::AttachCurrentThread();
        Java_SyncTestSigninUtils_setUpLiveAccountAndSignInForTesting(
            env, username, password, static_cast<int>(consent_level));
        run_loop.Quit();
      }));
  run_loop.Run();
}

void ShutdownLiveAuthForTesting() {
  base::RunLoop run_loop;
  // The heap instance of the callback will be deleted by
  // JNI_SyncTestSigninUtils_OnShutdownComplete when shutdown is completed.
  auto heap_callback =
      std::make_unique<base::OnceClosure>(run_loop.QuitClosure());

  Java_SyncTestSigninUtils_shutdownLiveAuthForTesting(
      base::android::AttachCurrentThread(),
      reinterpret_cast<intptr_t>(heap_callback.release()));

  run_loop.Run();
}

tab_groups::LocalTabGroupID CreateGroupFromTab(TabAndroid* tab) {
  CHECK(tab);
  JNIEnv* env = base::android::AttachCurrentThread();
  auto j_group_id = Java_SyncTestTabGroupHelpers_createGroupFromTab(
      env, tab->GetJavaObject());
  return base::android::TokenAndroid::FromJavaToken(env, j_group_id);
}

std::optional<tab_groups::LocalTabGroupID> GetGroupIdForTab(TabAndroid* tab) {
  CHECK(tab);
  JNIEnv* env = base::android::AttachCurrentThread();
  auto j_group_id =
      Java_SyncTestTabGroupHelpers_getGroupIdForTab(env, tab->GetJavaObject());
  if (j_group_id.is_null()) {
    return std::nullopt;
  }
  return base::android::TokenAndroid::FromJavaToken(env, j_group_id);
}

void UpdateTabGroupVisualData(TabAndroid* tab,
                              const std::string_view& title,
                              tab_groups::TabGroupColorId color) {
  CHECK(tab);
  JNIEnv* env = base::android::AttachCurrentThread();
  auto j_title = base::android::ConvertUTF8ToJavaString(env, title);
  jint j_color = static_cast<jint>(color);
  Java_SyncTestTabGroupHelpers_updateGroupVisualData(env, tab->GetJavaObject(),
                                                     j_title, j_color);
}

void JNI_SyncTestSigninUtils_OnShutdownComplete(JNIEnv* env,
                                                jlong callbackPtr) {
  std::unique_ptr<base::OnceClosure> heap_callback(
      reinterpret_cast<base::OnceClosure*>(callbackPtr));
  std::move(*heap_callback).Run();
}

}  // namespace sync_test_utils_android
