// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/functional/callback.h"
#include "chrome/browser/updater/scheduler.h"

namespace updater {

void DoPeriodicTasks(base::OnceClosure callback) {
  std::move(callback).Run();
}

void WakeAllUpdaters(base::OnceClosure callback) {
  std::move(callback).Run();
}

}  // namespace updater
