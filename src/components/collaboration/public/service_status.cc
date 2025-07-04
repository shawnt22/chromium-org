// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/collaboration/public/service_status.h"

namespace collaboration {

// LINT.IfChange(ServiceStatus)
bool ServiceStatus::IsAllowedToJoin() {
  // Please keep logic consistent with
  // //components/collaboration/public/android/java/src/org/chromium/components/collaboration/ServiceStatus.java.
  switch (collaboration_status) {
    case CollaborationStatus::kDisabled:
    case CollaborationStatus::kDisabledPending:
    case CollaborationStatus::kDisabledForPolicy:
      return false;
    case CollaborationStatus::kAllowedToJoin:
    case CollaborationStatus::kEnabledJoinOnly:
    case CollaborationStatus::kEnabledCreateAndJoin:
    case CollaborationStatus::kVersionOutOfDate:
    case CollaborationStatus::kVersionOutOfDateShowUpdateChromeUi:
      return true;
  }
}

bool ServiceStatus::IsAllowedToCreate() {
  // Please keep logic consistent with
  // //components/collaboration/public/android/java/src/org/chromium/components/collaboration/ServiceStatus.java.

  if (signin_status == SigninStatus::kSigninDisabled) {
    return false;
  }

  switch (collaboration_status) {
    case CollaborationStatus::kDisabled:
    case CollaborationStatus::kDisabledPending:
    case CollaborationStatus::kDisabledForPolicy:
    case CollaborationStatus::kAllowedToJoin:
    case CollaborationStatus::kEnabledJoinOnly:
    case CollaborationStatus::kVersionOutOfDate:
      return false;
    case CollaborationStatus::kEnabledCreateAndJoin:
    case CollaborationStatus::kVersionOutOfDateShowUpdateChromeUi:
      return true;
  }
}
// LINT.ThenChange(//components/collaboration/public/android/java/src/org/chromium/components/collaboration/ServiceStatus.java)

bool ServiceStatus::IsAuthenticationValid() const {
  // This is only used in native code.
  return signin_status == SigninStatus::kSignedIn &&
         sync_status == SyncStatus::kSyncEnabled;
}

bool operator==(const ServiceStatus& lhs, const ServiceStatus& rhs) {
  return lhs.signin_status == rhs.signin_status &&
         lhs.sync_status == rhs.sync_status &&
         lhs.collaboration_status == rhs.collaboration_status;
}

}  // namespace collaboration
