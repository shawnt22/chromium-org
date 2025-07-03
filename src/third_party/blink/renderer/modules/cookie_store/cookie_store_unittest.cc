// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/cookie_store/cookie_store.h"

#include "base/memory/scoped_refptr.h"
#include "base/task/task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "net/base/isolation_info.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_monster.h"
#include "net/first_party_sets/first_party_set_metadata.h"
#include "services/network/cookie_settings.h"
#include "services/network/restricted_cookie_manager.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/cross_variant_mojo_util.h"
#include "third_party/blink/renderer/bindings/core/v8/idl_types.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_tester.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_testing.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_cookie_init.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_cookie_list_item.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "url/origin.h"

namespace blink {

namespace {

constexpr char kDefaultUrl[] = "https://example.com/";

net::FirstPartySetMetadata ComputeFirstPartySetMetadataSync(
    const url::Origin& origin,
    const net::CookieMonster* cookie_store,
    const net::IsolationInfo& isolation_info) {
  base::test::TestFuture<net::FirstPartySetMetadata> future;
  network::RestrictedCookieManager::ComputeFirstPartySetMetadata(
      origin, cookie_store, isolation_info, future.GetCallback());
  return future.Take();
}

class CookieStoreTest : public testing::Test {
 protected:
  CookieStoreTest()
      : origin_(url::Origin::Create(GURL(kDefaultUrl))),
        isolation_info_(net::IsolationInfo::CreateForInternalRequest(origin_)),
        cookie_monster_(/*store=*/nullptr, /*net_log=*/nullptr),
        backend_(std::make_unique<network::RestrictedCookieManager>(
            network::mojom::RestrictedCookieManagerRole::SCRIPT,
            &cookie_monster_,
            cookie_settings_,
            origin_,
            isolation_info_,
            /*cookies_setting_overrides=*/net::CookieSettingOverrides(),
            /*devtools_cookies_setting_overrides=*/
            net::CookieSettingOverrides(),
            /*cookie_observer=*/mojo::NullRemote(),
            ComputeFirstPartySetMetadataSync(origin_,
                                             &cookie_monster_,
                                             isolation_info_))) {}

  CookieStore* CreateCookieStore(const V8TestingScope& scope) {
    return MakeGarbageCollected<CookieStore>(
        scope.GetExecutionContext(), CreateRemoteAndInstallReceiver(scope));
  }

  void SetCookie(const std::string& cookie_line) {
    GURL url(kDefaultUrl);
    base::test::TestFuture<net::CookieAccessResult> future;
    std::unique_ptr<net::CanonicalCookie> cookie =
        net::CanonicalCookie::CreateForTesting(url, cookie_line,
                                               base::Time::Now());
    cookie_monster_.SetCanonicalCookieAsync(
        std::move(cookie), url, net::CookieOptions::MakeAllInclusive(),
        future.GetCallback());
    net::CookieAccessResult result = future.Take();
    EXPECT_TRUE(result.status.IsInclude());
  }

  net::CookieList GetAllCookies() {
    base::test::TestFuture<const net::CookieList&> future;
    cookie_monster_.GetAllCookiesAsync(future.GetCallback());
    return future.Take();
  }

 private:
  HeapMojoRemote<network::mojom::blink::RestrictedCookieManager>
  CreateRemoteAndInstallReceiver(const V8TestingScope& scope) {
    HeapMojoRemote<network::mojom::blink::RestrictedCookieManager> remote(
        scope.GetExecutionContext());
    scoped_refptr<base::SequencedTaskRunner> task_runner =
        task_environment_.GetMainThreadTaskRunner();
    mojo::PendingReceiver<network::mojom::blink::RestrictedCookieManager>
        receiver = remote.BindNewPipeAndPassReceiver(task_runner);
    backend_->InstallReceiver(
        blink::ToCrossVariantMojoType(std::move(receiver)), base::DoNothing());
    EXPECT_TRUE(base::test::RunUntil([&]() { return remote.is_bound(); }));
    return remote;
  }

  test::TaskEnvironment task_environment_;
  url::Origin origin_;
  net::IsolationInfo isolation_info_;
  net::CookieMonster cookie_monster_;
  network::CookieSettings cookie_settings_;
  std::unique_ptr<network::RestrictedCookieManager> backend_;
};

TEST_F(CookieStoreTest, GetByName) {
  V8TestingScope v8_testing_scope((KURL(kDefaultUrl)));
  CookieStore* cookie_store = CreateCookieStore(v8_testing_scope);

  ScriptState* script_state = v8_testing_scope.GetScriptState();
  ASSERT_TRUE(script_state);
  ExceptionState exception_state(v8_testing_scope.GetIsolate());

  // CookieStore.get should return a Promise that resolves null when there are
  // no cookies.
  ScriptPromise<IDLNullable<CookieListItem>> promise =
      cookie_store->get(script_state, "cookie-name", exception_state);
  ScriptPromiseTester tester_expecting_no_cookies(script_state, promise);
  tester_expecting_no_cookies.WaitUntilSettled();  // Runs a nested event loop.
  EXPECT_FALSE(exception_state.HadException());
  EXPECT_TRUE(tester_expecting_no_cookies.IsFulfilled());
  EXPECT_TRUE(tester_expecting_no_cookies.Value().IsNull());

  // CookieStore.get should return a Promise that resolves this cookie.
  SetCookie("cookie-name=cookie-value");
  promise = cookie_store->get(script_state, "cookie-name", exception_state);
  ScriptPromiseTester tester_expecting_cookie(script_state, promise);
  tester_expecting_cookie.WaitUntilSettled();  // Runs a nested event loop.
  EXPECT_FALSE(exception_state.HadException());
  EXPECT_TRUE(tester_expecting_cookie.IsFulfilled());
  EXPECT_TRUE(tester_expecting_cookie.Value().IsObject());
  CookieListItem* got = CookieListItem::Create(
      v8_testing_scope.GetIsolate(), tester_expecting_cookie.Value().V8Value(),
      exception_state);
  ASSERT_TRUE(got);  // CookieListItem::Create is auto-generated.
  EXPECT_EQ("cookie-name", got->name());
  EXPECT_EQ("cookie-value", got->value());
}

TEST_F(CookieStoreTest, SetByName) {
  V8TestingScope v8_testing_scope((KURL(kDefaultUrl)));
  CookieStore* cookie_store = CreateCookieStore(v8_testing_scope);

  ScriptState* script_state = v8_testing_scope.GetScriptState();
  ASSERT_TRUE(script_state);
  ExceptionState exception_state(v8_testing_scope.GetIsolate());

  std::vector<net::CanonicalCookie> got = GetAllCookies();
  EXPECT_TRUE(got.empty());

  ScriptPromise<IDLUndefined> promise = cookie_store->set(
      script_state, "cookie-name", "cookie-value", exception_state);
  ScriptPromiseTester promise_tester(script_state, promise);
  promise_tester.WaitUntilSettled();
  EXPECT_FALSE(exception_state.HadException());
  EXPECT_TRUE(promise_tester.IsFulfilled());
  got = GetAllCookies();
  EXPECT_EQ(1u, got.size());
  EXPECT_EQ("cookie-name", got[0].Name());
  EXPECT_EQ("cookie-value", got[0].Value());
}

TEST_F(CookieStoreTest, SetWithMixedCaseDomain) {
  V8TestingScope v8_testing_scope((KURL(kDefaultUrl)));
  CookieStore* cookie_store = CreateCookieStore(v8_testing_scope);

  ScriptState* script_state = v8_testing_scope.GetScriptState();
  ASSERT_TRUE(script_state);
  ExceptionState exception_state(v8_testing_scope.GetIsolate());

  std::vector<net::CanonicalCookie> got = GetAllCookies();
  EXPECT_TRUE(got.empty());

  CookieInit* set_options = CookieInit::Create();
  set_options->setName("cookie-name");
  set_options->setValue("cookie-value");
  set_options->setDomain("ExAmPlE.CoM");

  ScriptPromise<IDLUndefined> promise =
      cookie_store->set(script_state, set_options, exception_state);
  ScriptPromiseTester promise_tester(script_state, promise);
  promise_tester.WaitUntilSettled();
  EXPECT_FALSE(exception_state.HadException());
  EXPECT_TRUE(promise_tester.IsFulfilled());
  got = GetAllCookies();
  EXPECT_EQ(1u, got.size());
  EXPECT_EQ("cookie-name", got[0].Name());
  EXPECT_EQ("cookie-value", got[0].Value());
  EXPECT_EQ(".example.com", got[0].Domain());
}

TEST_F(CookieStoreTest, SetWithHttpPrefix) {
  V8TestingScope v8_testing_scope((KURL(kDefaultUrl)));
  CookieStore* cookie_store = CreateCookieStore(v8_testing_scope);

  ScriptState* script_state = v8_testing_scope.GetScriptState();
  ASSERT_TRUE(script_state);
  DummyExceptionStateForTesting exception_state;

  std::vector<net::CanonicalCookie> got = GetAllCookies();
  EXPECT_TRUE(got.empty());

  CookieInit* set_options = CookieInit::Create();
  set_options->setName("__HtTp-name");
  set_options->setValue("cookie-value");
  set_options->setDomain("ExAmPlE.CoM");

  ScriptPromise<IDLUndefined> promise =
      cookie_store->set(script_state, set_options, exception_state);
  ScriptPromiseTester promise_tester(script_state, promise, &exception_state);
  promise_tester.WaitUntilSettled();
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(
      "Cookies with \"__Http-\" prefix cannot be set using the CookieStore "
      "API.",
      exception_state.Message());
  EXPECT_TRUE(promise_tester.IsRejected());
  got = GetAllCookies();
  EXPECT_EQ(0u, got.size());
}

TEST_F(CookieStoreTest, SetWithHostPrefixAndDomain) {
  V8TestingScope v8_testing_scope((KURL(kDefaultUrl)));
  CookieStore* cookie_store = CreateCookieStore(v8_testing_scope);

  ScriptState* script_state = v8_testing_scope.GetScriptState();
  ASSERT_TRUE(script_state);
  ExceptionState exception_state(v8_testing_scope.GetIsolate());

  std::vector<net::CanonicalCookie> got = GetAllCookies();
  EXPECT_TRUE(got.empty());

  CookieInit* set_options = CookieInit::Create();
  set_options->setName("__HosT-name");
  set_options->setValue("cookie-value");
  set_options->setDomain("ExAmPlE.CoM");

  ScriptPromise<IDLUndefined> promise =
      cookie_store->set(script_state, set_options, exception_state);
  ScriptPromiseTester promise_tester(script_state, promise, &exception_state);
  promise_tester.WaitUntilSettled();
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_TRUE(promise_tester.IsRejected());
  got = GetAllCookies();
  EXPECT_EQ(0u, got.size());
}

}  // namespace
}  // namespace blink
