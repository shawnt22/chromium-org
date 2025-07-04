// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/saved_tab_groups/internal/tab_group_sync_service_test_utils.h"

#include "base/version_info/channel.h"
#include "components/data_sharing/public/features.h"
#include "components/optimization_guide/core/hints/optimization_guide_decider.h"
#include "components/saved_tab_groups/delegate/tab_group_sync_delegate.h"
#include "components/saved_tab_groups/internal/saved_tab_group_model.h"
#include "components/saved_tab_groups/internal/sync_data_type_configuration.h"
#include "components/saved_tab_groups/internal/tab_group_sync_metrics_logger_impl.h"
#include "components/saved_tab_groups/internal/tab_group_sync_service_impl.h"
#include "components/saved_tab_groups/public/collaboration_finder.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/sync/base/collaboration_id.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/features.h"
#include "components/sync/base/report_unrecoverable_error.h"
#include "components/sync/model/client_tag_based_data_type_processor.h"
#include "components/sync/model/data_type_local_change_processor.h"
#include "components/sync/model/data_type_store_service.h"
#include "components/sync_device_info/device_info_tracker.h"

namespace tab_groups::test {
namespace {
std::unique_ptr<SyncDataTypeConfiguration>
CreateSavedTabGroupDataTypeConfiguration(
    version_info::Channel channel,
    syncer::DataTypeStoreService* data_type_store_service) {
  return std::make_unique<SyncDataTypeConfiguration>(
      std::make_unique<syncer::ClientTagBasedDataTypeProcessor>(
          syncer::SAVED_TAB_GROUP,
          base::BindRepeating(&syncer::ReportUnrecoverableError, channel)),
      data_type_store_service->GetStoreFactory());
}

std::unique_ptr<SyncDataTypeConfiguration>
MaybeCreateSharedTabGroupDataTypeConfiguration(
    version_info::Channel channel,
    syncer::DataTypeStoreService* data_type_store_service) {
  if (!data_sharing::features::IsDataSharingFunctionalityEnabled()) {
    return nullptr;
  }

  return std::make_unique<SyncDataTypeConfiguration>(
      std::make_unique<syncer::ClientTagBasedDataTypeProcessor>(
          syncer::SHARED_TAB_GROUP_DATA,
          base::BindRepeating(&syncer::ReportUnrecoverableError, channel)),
      data_type_store_service->GetStoreFactory());
}

std::unique_ptr<SyncDataTypeConfiguration>
MaybeCreateSharedTabGroupAccountDataTypeConfiguration(
    version_info::Channel channel,
    syncer::DataTypeStoreService* data_type_store_service) {
  if (!data_sharing::features::IsDataSharingFunctionalityEnabled() ||
      !base::FeatureList::IsEnabled(syncer::kSyncSharedTabGroupAccountData)) {
    return nullptr;
  }

  return std::make_unique<SyncDataTypeConfiguration>(
      std::make_unique<syncer::ClientTagBasedDataTypeProcessor>(
          syncer::SHARED_TAB_GROUP_ACCOUNT_DATA,
          base::BindRepeating(&syncer::ReportUnrecoverableError, channel)),
      data_type_store_service->GetStoreFactory());
}

class EmptyCollaborationFinder : public CollaborationFinder {
 public:
  EmptyCollaborationFinder() = default;
  ~EmptyCollaborationFinder() override = default;

  // Disallow copy/assign.
  EmptyCollaborationFinder(const EmptyCollaborationFinder&) = delete;
  EmptyCollaborationFinder& operator=(const EmptyCollaborationFinder&) = delete;

  // tab_groups::CollaborationFinder overrides.
  void SetClient(Client* client) override {}
  bool IsCollaborationAvailable(
      const syncer::CollaborationId& collaboration_id) override {
    return collaborations_available_.contains(collaboration_id);
  }
  void SetCollaborationAvailableForTesting(
      const syncer::CollaborationId& collaboration_id) override {
    collaborations_available_.insert(collaboration_id);
  }

 private:
  std::set<syncer::CollaborationId> collaborations_available_;
};

}  // namespace

std::unique_ptr<TabGroupSyncService> CreateTabGroupSyncService(
    std::unique_ptr<SavedTabGroupModel> model,
    syncer::DataTypeStoreService* data_type_store_service,
    PrefService* pref_service,
    syncer::DeviceInfoTracker* device_info_tracker,
    optimization_guide::OptimizationGuideDecider* optimization_guide,
    signin::IdentityManager* identity_manager) {
  auto metrics_logger =
      std::make_unique<TabGroupSyncMetricsLoggerImpl>(device_info_tracker);
  version_info::Channel channel = version_info::Channel::DEFAULT;
  auto saved_config = CreateSavedTabGroupDataTypeConfiguration(
      channel, data_type_store_service);
  auto shared_config = MaybeCreateSharedTabGroupDataTypeConfiguration(
      channel, data_type_store_service);
  auto collaboration_finder = std::make_unique<EmptyCollaborationFinder>();

  return std::make_unique<TabGroupSyncServiceImpl>(
      std::move(model), std::move(saved_config), std::move(shared_config),
      MaybeCreateSharedTabGroupAccountDataTypeConfiguration(
          channel, data_type_store_service),
      pref_service, std::move(metrics_logger), optimization_guide,
      identity_manager, std::move(collaboration_finder),
      /*logger=*/nullptr);
}

}  // namespace tab_groups::test
