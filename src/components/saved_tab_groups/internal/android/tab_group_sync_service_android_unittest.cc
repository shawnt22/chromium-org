// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/saved_tab_groups/internal/android/tab_group_sync_service_android.h"

#include <memory>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "components/saved_tab_groups/public/android/tab_group_sync_conversions_bridge.h"
#include "components/saved_tab_groups/public/android/tab_group_sync_conversions_utils.h"
#include "components/saved_tab_groups/public/versioning_message_controller.h"
#include "components/saved_tab_groups/test_support/mock_tab_group_sync_service.h"
#include "components/saved_tab_groups/test_support/saved_tab_group_test_utils.h"
#include "components/sync/test/test_matchers.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "components/saved_tab_groups/internal/native_j_unittests_jni_headers/TabGroupSyncServiceAndroidUnitTest_jni.h"

using base::android::AttachCurrentThread;
using base::android::JavaParamRef;
using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;

namespace tab_groups {
namespace {

const char kTestUuid[] = "abcdefgh";
const char16_t kTestGroupTitle[] = u"Test Group";
const char kTestUrl[] = "https://google.com";
const char16_t kTestTabTitle[] = u"Test Tab";
const int kTabId1 = 2;
const int kTabId2 = 4;
const int kPosition = 3;

MATCHER_P(UuidEq, uuid, "") {
  return arg.saved_guid() == uuid;
}

MATCHER_P3(TabBuilderEq, title, url, position, "") {
  return arg.title() == title && arg.url() == url &&
         static_cast<int>(arg.position()) == position;
}

class MockVersioningMessageController : public VersioningMessageController {
 public:
  MockVersioningMessageController() = default;
  ~MockVersioningMessageController() override = default;

  MOCK_METHOD(bool, IsInitialized, (), (override));
  MOCK_METHOD(bool, ShouldShowMessageUi, (MessageType), (override));
  MOCK_METHOD(void,
              ShouldShowMessageUiAsync,
              (MessageType, base::OnceCallback<void(bool)>),
              (override));
  MOCK_METHOD(void, OnMessageUiShown, (MessageType), (override));
  MOCK_METHOD(void, OnMessageUiDismissed, (MessageType), (override));
};

}  // namespace

class TabGroupSyncServiceAndroidTest : public testing::Test {
 public:
  TabGroupSyncServiceAndroidTest() = default;
  ~TabGroupSyncServiceAndroidTest() override = default;

  void SetUp() override {
    j_test_ = Java_TabGroupSyncServiceAndroidUnitTest_Constructor(
        AttachCurrentThread());
    CreateBridge();
    SetUpJavaTestObserver();
  }

  void CreateBridge() {
    EXPECT_CALL(tab_group_sync_service_, GetVersioningMessageController())
        .WillOnce(Return(&versioning_message_controller_));
    EXPECT_CALL(tab_group_sync_service_, AddObserver(_));
    bridge_ =
        std::make_unique<TabGroupSyncServiceAndroid>(&tab_group_sync_service_);
    j_service_ = bridge_->GetJavaObject();
  }

  void SetUpJavaTestObserver() {
    auto* env = AttachCurrentThread();
    Java_TabGroupSyncServiceAndroidUnitTest_setUpTestObserver(env, j_test_,
                                                              j_service_);
  }

  void TearDown() override {
    EXPECT_CALL(tab_group_sync_service_, RemoveObserver(_));
  }

  MockTabGroupSyncService tab_group_sync_service_;
  std::unique_ptr<TabGroupSyncServiceAndroid> bridge_;
  base::android::ScopedJavaLocalRef<jobject> j_service_;
  base::android::ScopedJavaGlobalRef<jobject> j_test_;
  LocalTabGroupID test_tab_group_id_ = base::Token(4, 5);
  MockVersioningMessageController versioning_message_controller_;
};

TEST_F(TabGroupSyncServiceAndroidTest, OnInitialized) {
  bridge_->OnInitialized();
  Java_TabGroupSyncServiceAndroidUnitTest_testOnInitialized(
      AttachCurrentThread(), j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, UuidConversion) {
  auto* env = AttachCurrentThread();
  base::Uuid uuid = base::Uuid::ParseCaseInsensitive(kTestUuid);
  auto j_uuid = UuidToJavaString(env, uuid);
  auto uuid2 =
      JavaStringToUuid(env, JavaParamRef<jstring>(env, j_uuid.Release()));
  EXPECT_EQ(uuid, uuid2);
}

TEST_F(TabGroupSyncServiceAndroidTest, TabGroupIdConversion) {
  auto* env = AttachCurrentThread();
  LocalTabGroupID tab_group_id = test_tab_group_id_;
  auto j_tab_group_id = TabGroupSyncConversionsBridge::ToJavaTabGroupId(
      env, std::make_optional<LocalTabGroupID>(tab_group_id));
  auto retrieved_tab_group_id =
      TabGroupSyncConversionsBridge::FromJavaTabGroupId(
          env, JavaParamRef<jobject>(env, j_tab_group_id.Release()));
  EXPECT_EQ(retrieved_tab_group_id, tab_group_id);
}

TEST_F(TabGroupSyncServiceAndroidTest, TabIdConversion) {
  LocalTabID tab_id = 5;
  EXPECT_EQ(FromJavaTabId(ToJavaTabId(std::make_optional<LocalTabID>(tab_id))),
            tab_id);
}

TEST_F(TabGroupSyncServiceAndroidTest, SavedTabGroupConversion_NativeToJava) {
  auto* env = AttachCurrentThread();
  SavedTabGroup group = test::CreateTestSavedTabGroup();
  group.SetTitle(kTestGroupTitle);
  group.SetColor(tab_groups::TabGroupColorId::kRed);
  group.SetCreatorCacheGuid("creator_cache_guid");
  group.SetLastUpdaterCacheGuid("last_updater_cache_guid");
  group.SetArchivalTime(base::Time::Now());

  SavedTabGroupTab tab3(GURL(), kTestTabTitle, group.saved_guid(),
                        /*position=*/std::nullopt,
                        /*saved_tab_guid=*/std::nullopt, /*local_tab_id=*/9,
                        "creator_cache_guid", "last_updater_cache_guid");
  group.AddTabLocally(tab3);
  auto j_group = TabGroupSyncConversionsBridge::CreateGroup(env, group);
  Java_TabGroupSyncServiceAndroidUnitTest_testSavedTabGroupConversionNativeToJava(
      env, j_test_, j_group);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnTabGroupAdded) {
  auto* env = AttachCurrentThread();
  SavedTabGroup group = test::CreateTestSavedTabGroup();
  group.SetTitle(kTestGroupTitle);
  group.SetColor(tab_groups::TabGroupColorId::kBlue);
  bridge_->OnTabGroupAdded(group, TriggerSource::REMOTE);
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabGroupAdded(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnTabGroupUpdated) {
  auto* env = AttachCurrentThread();
  SavedTabGroup group = test::CreateTestSavedTabGroup();
  group.SetTitle(kTestGroupTitle);
  group.SetColor(tab_groups::TabGroupColorId::kBlue);
  bridge_->OnTabGroupUpdated(group, TriggerSource::REMOTE);
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabGroupUpdated(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnTabGroupRemoved) {
  auto* env = AttachCurrentThread();
  base::Uuid group_id = base::Uuid::GenerateRandomV4();
  bridge_->OnTabGroupRemoved(test_tab_group_id_, TriggerSource::REMOTE);
  bridge_->OnTabGroupRemoved(group_id, TriggerSource::REMOTE);
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabGroupRemoved(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnTabGroupLocalIdChanged) {
  auto* env = AttachCurrentThread();
  base::Uuid group_id = base::Uuid::GenerateRandomV4();
  bridge_->OnTabGroupLocalIdChanged(group_id, test_tab_group_id_);
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabGroupLocalIdChanged(env,
                                                                       j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, AddGroup) {
  auto* env = AttachCurrentThread();
  SavedTabGroup captured_group = test::CreateTestSavedTabGroup();
  EXPECT_CALL(tab_group_sync_service_, AddGroup(_))
      .WillOnce(SaveArg<0>(&captured_group));

  Java_TabGroupSyncServiceAndroidUnitTest_testAddGroup(env, j_test_);

  EXPECT_EQ(test_tab_group_id_, captured_group.local_group_id());
  EXPECT_EQ(kTestGroupTitle, captured_group.title());
  EXPECT_EQ(TabGroupColorId::kGreen, captured_group.color());

  EXPECT_EQ(1u, captured_group.saved_tabs().size());
  SavedTabGroupTab tab1 = captured_group.saved_tabs()[0];
  EXPECT_EQ(kTabId1, tab1.local_tab_id());
  EXPECT_EQ(GURL(kTestUrl), tab1.url());
  EXPECT_EQ(kTestTabTitle, tab1.title());
}

TEST_F(TabGroupSyncServiceAndroidTest, RemoveGroupByLocalId) {
  auto* env = AttachCurrentThread();

  EXPECT_CALL(tab_group_sync_service_, RemoveGroup(test_tab_group_id_))
      .Times(1);
  Java_TabGroupSyncServiceAndroidUnitTest_testRemoveGroupByLocalId(env,
                                                                   j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, RemoveGroupBySyncId) {
  auto* env = AttachCurrentThread();

  base::Uuid uuid = base::Uuid::ParseCaseInsensitive(kTestUuid);
  auto j_uuid = UuidToJavaString(env, uuid);

  EXPECT_CALL(tab_group_sync_service_, RemoveGroup(uuid)).Times(1);
  Java_TabGroupSyncServiceAndroidUnitTest_testRemoveGroupBySyncId(env, j_test_,
                                                                  j_uuid);
}

TEST_F(TabGroupSyncServiceAndroidTest, UpdateVisualData) {
  auto* env = AttachCurrentThread();

  EXPECT_CALL(tab_group_sync_service_,
              UpdateVisualData(Eq(test_tab_group_id_), _));
  Java_TabGroupSyncServiceAndroidUnitTest_testUpdateVisualData(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, MakeTabGroupShared) {
  JNIEnv* env = AttachCurrentThread();
  const syncer::CollaborationId collaboration_id("collaboration");
  TabGroupSyncService::TabGroupSharingCallback captured_callback;

  EXPECT_CALL(
      tab_group_sync_service_,
      MakeTabGroupShared(
          Eq(test_tab_group_id_), Eq(collaboration_id),
          testing::Truly(
              [](const TabGroupSyncService::TabGroupSharingCallback& cb) {
                return cb.is_null();
              })))
      .WillOnce(testing::WithArgs<2>(
          testing::Invoke([&captured_callback](
                              TabGroupSyncService::TabGroupSharingCallback cb) {
            captured_callback = std::move(cb);
          })));

  ScopedJavaLocalRef<jstring> j_collaboration_id =
      base::android::ConvertUTF8ToJavaString(env, collaboration_id.value());
  Java_TabGroupSyncServiceAndroidUnitTest_testMakeTabGroupShared(
      env, j_test_, j_collaboration_id);
}

TEST_F(TabGroupSyncServiceAndroidTest, AboutToUnShareTabGroup) {
  JNIEnv* env = AttachCurrentThread();

  EXPECT_CALL(tab_group_sync_service_,
              AboutToUnShareTabGroup(Eq(test_tab_group_id_), _));
  Java_TabGroupSyncServiceAndroidUnitTest_testAboutToUnShareTabGroup(env,
                                                                     j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnTabGroupUnShareComplete) {
  JNIEnv* env = AttachCurrentThread();

  EXPECT_CALL(tab_group_sync_service_,
              OnTabGroupUnShareComplete(Eq(test_tab_group_id_), _));
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabGroupUnShareComplete(
      env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, AddTab) {
  auto* env = AttachCurrentThread();

  GURL url(kTestUrl);
  EXPECT_CALL(tab_group_sync_service_,
              AddTab(Eq(test_tab_group_id_), Eq(kTabId1), Eq(kTestTabTitle),
                     Eq(url), Eq(kPosition)));

  EXPECT_CALL(tab_group_sync_service_,
              AddTab(Eq(test_tab_group_id_), Eq(kTabId2), Eq(kTestTabTitle),
                     Eq(url), Eq(std::nullopt)));
  Java_TabGroupSyncServiceAndroidUnitTest_testAddTab(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, NavigateTab) {
  auto* env = AttachCurrentThread();

  GURL url(kTestUrl);
  EXPECT_CALL(tab_group_sync_service_,
              NavigateTab(Eq(test_tab_group_id_), Eq(kTabId1), Eq(url),
                          Eq(kTestTabTitle)));
  EXPECT_CALL(tab_group_sync_service_,
              NavigateTab(Eq(test_tab_group_id_), Eq(kTabId2), Eq(url),
                          Eq(kTestTabTitle)));
  Java_TabGroupSyncServiceAndroidUnitTest_testUpdateTab(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, RemoveTab) {
  auto* env = AttachCurrentThread();

  EXPECT_CALL(tab_group_sync_service_,
              RemoveTab(Eq(test_tab_group_id_), Eq(kTabId1)));
  Java_TabGroupSyncServiceAndroidUnitTest_testRemoveTab(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, MoveTab) {
  auto* env = AttachCurrentThread();

  EXPECT_CALL(tab_group_sync_service_,
              MoveTab(Eq(test_tab_group_id_), Eq(kTabId1), Eq(kPosition)));
  Java_TabGroupSyncServiceAndroidUnitTest_testMoveTab(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, GetAllGroups) {
  auto group = test::CreateTestSavedTabGroup();
  std::vector<SavedTabGroup> expectedGroups = {group};
  EXPECT_CALL(tab_group_sync_service_, GetAllGroups())
      .WillOnce(Return(expectedGroups));
  Java_TabGroupSyncServiceAndroidUnitTest_testGetAllGroups(
      AttachCurrentThread(), j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, GetGroupBySyncId) {
  auto* env = AttachCurrentThread();
  auto group1 = test::CreateTestSavedTabGroup();
  base::Uuid uuid2 = base::Uuid::ParseCaseInsensitive(kTestUuid);

  EXPECT_CALL(tab_group_sync_service_, GetGroup(group1.saved_guid()))
      .WillOnce(Return(std::make_optional<SavedTabGroup>(group1)));
  EXPECT_CALL(tab_group_sync_service_, GetGroup(uuid2))
      .WillOnce(Return(std::nullopt));

  auto j_uuid1 = UuidToJavaString(env, group1.saved_guid());
  auto j_uuid2 = UuidToJavaString(env, uuid2);
  Java_TabGroupSyncServiceAndroidUnitTest_testGetGroupBySyncId(
      env, j_test_, j_uuid1, j_uuid2);
}

TEST_F(TabGroupSyncServiceAndroidTest, GetGroupByLocalId) {
  auto* env = AttachCurrentThread();
  auto group1 = test::CreateTestSavedTabGroup();
  auto local_id_1 = test::GenerateRandomTabGroupID();
  auto local_id_2 = test::GenerateRandomTabGroupID();

  EXPECT_CALL(tab_group_sync_service_, GetGroup(local_id_1))
      .WillOnce(Return(std::make_optional<SavedTabGroup>(group1)));
  EXPECT_CALL(tab_group_sync_service_, GetGroup(local_id_2))
      .WillOnce(Return(std::nullopt));

  auto j_local_id_1 =
      TabGroupSyncConversionsBridge::ToJavaTabGroupId(env, local_id_1);
  auto j_local_id_2 =
      TabGroupSyncConversionsBridge::ToJavaTabGroupId(env, local_id_2);
  Java_TabGroupSyncServiceAndroidUnitTest_testGetGroupByLocalId(
      env, j_test_, j_local_id_1, j_local_id_2);
}

TEST_F(TabGroupSyncServiceAndroidTest, GetDeletedGroupIds) {
  auto local_id_1 = test::GenerateRandomTabGroupID();
  std::vector<LocalTabGroupID> expectedGroupIds = {local_id_1};
  EXPECT_CALL(tab_group_sync_service_, GetDeletedGroupIds())
      .WillOnce(Return(expectedGroupIds));
  Java_TabGroupSyncServiceAndroidUnitTest_testGetDeletedGroupIds(
      AttachCurrentThread(), j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, UpdateLocalTabGroupMapping) {
  auto* env = AttachCurrentThread();
  base::Uuid group_id = base::Uuid::GenerateRandomV4();
  auto j_group_id = UuidToJavaString(env, group_id);

  // Update the mapping.
  EXPECT_CALL(
      tab_group_sync_service_,
      UpdateLocalTabGroupMapping(Eq(group_id), Eq(test_tab_group_id_),
                                 Eq(OpeningSource::kAutoOpenedFromSync)));
  Java_TabGroupSyncServiceAndroidUnitTest_testUpdateLocalTabGroupMapping(
      AttachCurrentThread(), j_test_, j_group_id,
      TabGroupSyncConversionsBridge::ToJavaTabGroupId(env, test_tab_group_id_));

  // Remove the mapping.
  EXPECT_CALL(tab_group_sync_service_,
              RemoveLocalTabGroupMapping(Eq(test_tab_group_id_),
                                         Eq(ClosingSource::kDeletedByUser)));
  Java_TabGroupSyncServiceAndroidUnitTest_testRemoveLocalTabGroupMapping(
      AttachCurrentThread(), j_test_,
      TabGroupSyncConversionsBridge::ToJavaTabGroupId(env, test_tab_group_id_));
}

TEST_F(TabGroupSyncServiceAndroidTest, UpdateLocalTabId) {
  auto* env = AttachCurrentThread();
  base::Uuid tab_id = base::Uuid::GenerateRandomV4();
  auto j_tab_id = UuidToJavaString(env, tab_id);

  EXPECT_CALL(tab_group_sync_service_,
              UpdateLocalTabId(Eq(test_tab_group_id_), Eq(tab_id), Eq(4)));
  Java_TabGroupSyncServiceAndroidUnitTest_testUpdateLocalTabId(
      AttachCurrentThread(), j_test_,
      TabGroupSyncConversionsBridge::ToJavaTabGroupId(env, test_tab_group_id_),
      j_tab_id, 4);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnTabSelected) {
  auto* env = AttachCurrentThread();
  // Select a tab that is part of a group.
  LocalTabID tab_id = 5;
  ScopedJavaLocalRef<jstring> j_tab_title =
      base::android::ConvertUTF16ToJavaString(env, kTestTabTitle);

  EXPECT_CALL(
      tab_group_sync_service_,
      OnTabSelected(Eq(test_tab_group_id_), Eq(tab_id), Eq(kTestTabTitle)));
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabSelected(
      AttachCurrentThread(), j_test_,
      TabGroupSyncConversionsBridge::ToJavaTabGroupId(env, test_tab_group_id_),
      tab_id, j_tab_title);

  // Select a tab that isn't part of a group.
  LocalTabID non_grouped_tab_id = 6;
  EXPECT_CALL(tab_group_sync_service_,
              OnTabSelected(Eq(std::nullopt), Eq(non_grouped_tab_id),
                            Eq(kTestTabTitle)));
  Java_TabGroupSyncServiceAndroidUnitTest_testOnTabSelected(
      AttachCurrentThread(), j_test_, ScopedJavaLocalRef<jobject>(),
      non_grouped_tab_id, j_tab_title);
}

TEST_F(TabGroupSyncServiceAndroidTest, UpdateArchivalStatus) {
  auto* env = AttachCurrentThread();

  base::Uuid uuid = base::Uuid::ParseCaseInsensitive(kTestUuid);
  auto j_uuid = UuidToJavaString(env, uuid);

  EXPECT_CALL(tab_group_sync_service_, UpdateArchivalStatus(uuid, true))
      .Times(1);
  Java_TabGroupSyncServiceAndroidUnitTest_testUpdateArchivalStatus(
      env, j_test_, j_uuid, true);
}

TEST_F(TabGroupSyncServiceAndroidTest, IsInitialized) {
  auto* env = AttachCurrentThread();
  EXPECT_CALL(versioning_message_controller_, IsInitialized)
      .Times(1)
      .WillOnce(Return(true));
  Java_TabGroupSyncServiceAndroidUnitTest_testIsInitialized(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, ShouldShowMessageUi) {
  auto* env = AttachCurrentThread();
  EXPECT_CALL(versioning_message_controller_,
              ShouldShowMessageUi(VersioningMessageController::MessageType::
                                      VERSION_OUT_OF_DATE_INSTANT_MESSAGE))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(versioning_message_controller_,
              ShouldShowMessageUi(VersioningMessageController::MessageType::
                                      VERSION_OUT_OF_DATE_PERSISTENT_MESSAGE))
      .Times(1)
      .WillOnce(Return(false));
  EXPECT_CALL(
      versioning_message_controller_,
      ShouldShowMessageUi(
          VersioningMessageController::MessageType::VERSION_UPDATED_MESSAGE))
      .Times(1)
      .WillOnce(Return(true));
  Java_TabGroupSyncServiceAndroidUnitTest_testShouldShowMessageUi(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, ShouldShowMessageUiAsync) {
  auto* env = AttachCurrentThread();
  EXPECT_CALL(
      versioning_message_controller_,
      ShouldShowMessageUiAsync(VersioningMessageController::MessageType::
                                   VERSION_OUT_OF_DATE_INSTANT_MESSAGE,
                               _))
      .WillOnce(
          testing::WithArg<1>([](base::OnceCallback<void(bool)> callback) {
            std::move(callback).Run(true);
          }));
  Java_TabGroupSyncServiceAndroidUnitTest_testShouldShowMessageUiAsync(env,
                                                                       j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnMessageUiShown) {
  auto* env = AttachCurrentThread();
  EXPECT_CALL(versioning_message_controller_,
              OnMessageUiShown(VersioningMessageController::MessageType::
                                   VERSION_OUT_OF_DATE_INSTANT_MESSAGE))
      .Times(1);
  Java_TabGroupSyncServiceAndroidUnitTest_testOnMessageUiShown(env, j_test_);
}

TEST_F(TabGroupSyncServiceAndroidTest, OnMessageUiDismissed) {
  auto* env = AttachCurrentThread();
  EXPECT_CALL(versioning_message_controller_,
              OnMessageUiDismissed(VersioningMessageController::MessageType::
                                       VERSION_OUT_OF_DATE_PERSISTENT_MESSAGE))
      .Times(1);
  Java_TabGroupSyncServiceAndroidUnitTest_testOnMessageUiDismissed(env,
                                                                   j_test_);
}

}  // namespace tab_groups
