// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_SHARING_TEST_SUPPORT_FAKE_DATA_SHARING_SDK_DELEGATE_H_
#define COMPONENTS_DATA_SHARING_TEST_SUPPORT_FAKE_DATA_SHARING_SDK_DELEGATE_H_

#include <map>
#include <string>

#include "components/data_sharing/public/data_sharing_sdk_delegate.h"
#include "components/data_sharing/public/group_data.h"
#include "google_apis/gaia/gaia_id.h"

namespace data_sharing {

// Implementation of DataSharingSDKDelegate for unit tests that holds the group
// info in-memory.
class FakeDataSharingSDKDelegate : public DataSharingSDKDelegate {
 public:
  FakeDataSharingSDKDelegate();
  ~FakeDataSharingSDKDelegate() override;

  // Convenience methods for testing.
  std::optional<data_sharing_pb::GroupData> GetGroup(const GroupId& group_id);

  void RemoveGroup(const GroupId& group_id);

  void UpdateGroup(const GroupId& group_id,
                   const std::string& new_display_name);

  GroupId AddGroupAndReturnId(const std::string& display_name);

  void AddMember(const GroupId& group_id, const GaiaId& member_gaia_id);
  void RemoveMember(const GroupId& group_id, const GaiaId& member_gaia_id);

  void AddAccount(const std::string& email, const GaiaId& gaia_id);

  // DataSharingSDKDelegate impl:
  void Initialize(
      DataSharingNetworkLoader* data_sharing_network_loader) override;
  void CreateGroup(const data_sharing_pb::CreateGroupParams& params,
                   base::OnceCallback<void(
                       const base::expected<data_sharing_pb::CreateGroupResult,
                                            absl::Status>&)> callback) override;
  void ReadGroups(const data_sharing_pb::ReadGroupsParams& params,
                  base::OnceCallback<void(
                      const base::expected<data_sharing_pb::ReadGroupsResult,
                                           absl::Status>&)> callback) override;
  void ReadGroupWithToken(
      const data_sharing_pb::ReadGroupWithTokenParams& params,
      base::OnceCallback<
          void(const base::expected<data_sharing_pb::ReadGroupsResult,
                                    absl::Status>&)> callback) override;

  void AddMember(
      const data_sharing_pb::AddMemberParams& params,
      base::OnceCallback<void(const absl::Status&)> callback) override;
  void RemoveMember(
      const data_sharing_pb::RemoveMemberParams& params,
      base::OnceCallback<void(const absl::Status&)> callback) override;
  void LeaveGroup(
      const data_sharing_pb::LeaveGroupParams& params,
      base::OnceCallback<void(const absl::Status&)> callback) override;
  void DeleteGroup(
      const data_sharing_pb::DeleteGroupParams& params,
      base::OnceCallback<void(const absl::Status&)> callback) override;
  void LookupGaiaIdByEmail(
      const data_sharing_pb::LookupGaiaIdByEmailParams& params,
      base::OnceCallback<
          void(const base::expected<data_sharing_pb::LookupGaiaIdByEmailResult,
                                    absl::Status>&)> callback) override;
  void AddAccessToken(
      const data_sharing_pb::AddAccessTokenParams& params,
      base::OnceCallback<
          void(const base::expected<data_sharing_pb::AddAccessTokenResult,
                                    absl::Status>&)> callback) override;

  void SetUserGaiaId(const GaiaId& gaia_id);

  std::map<GroupId, data_sharing_pb::GroupData>* groups() { return &groups_; }

 private:
  std::map<GroupId, data_sharing_pb::GroupData> groups_;
  std::map<std::string, GaiaId> email_to_gaia_id_;
  int next_group_id_ = 0;
  GaiaId user_gaia_id_;
};

}  // namespace data_sharing

#endif  // COMPONENTS_DATA_SHARING_TEST_SUPPORT_FAKE_DATA_SHARING_SDK_DELEGATE_H_
