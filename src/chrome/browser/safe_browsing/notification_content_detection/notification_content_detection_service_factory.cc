// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/notification_content_detection/notification_content_detection_service_factory.h"

#include "base/system/sys_info.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/optimization_guide/optimization_guide_keyed_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "components/safe_browsing/content/browser/notification_content_detection/notification_content_detection_service.h"
#include "components/safe_browsing/core/browser/db/database_manager.h"
#include "components/safe_browsing/core/common/features.h"
#include "content/public/browser/browser_context.h"

namespace safe_browsing {

// static
NotificationContentDetectionService*
NotificationContentDetectionServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<NotificationContentDetectionService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /* create= */ true));
}

// static
NotificationContentDetectionServiceFactory*
NotificationContentDetectionServiceFactory::GetInstance() {
  static base::NoDestructor<NotificationContentDetectionServiceFactory>
      instance;
  return instance.get();
}

NotificationContentDetectionServiceFactory::
    NotificationContentDetectionServiceFactory()
    : ProfileKeyedServiceFactory(
          "NotificationContentDetectionService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              // TODO(crbug.com/41488885): Check if this service is needed for
              // Ash Internals.
              .WithAshInternals(ProfileSelection::kOriginalOnly)
              .Build()) {
  DependsOn(OptimizationGuideKeyedServiceFactory::GetInstance());
}

std::unique_ptr<KeyedService> NotificationContentDetectionServiceFactory::
    BuildServiceInstanceForBrowserContext(
        content::BrowserContext* context) const {
  auto* opt_guide = OptimizationGuideKeyedServiceFactory::GetForProfile(
      Profile::FromBrowserContext(context));
  if (!opt_guide) {
    return nullptr;
  }
  if (!g_browser_process || !g_browser_process->safe_browsing_service() ||
      !g_browser_process->safe_browsing_service()->database_manager()) {
    return nullptr;
  }

// The model takes up too much memory to be run on ARM devices.
#if BUILDFLAG(IS_ANDROID) && defined(ARCH_CPU_ARMEL)
  return nullptr;
#elif BUILDFLAG(IS_ANDROID)
  // The model takes up too much memory to be run on low end Android devices.
  if (base::SysInfo::IsLowEndDevice()) {
    return nullptr;
  }
  return CreateNotificationContentDetectionService(opt_guide, context);
#else
  return CreateNotificationContentDetectionService(opt_guide, context);
#endif
}

std::unique_ptr<NotificationContentDetectionService>
NotificationContentDetectionServiceFactory::
    CreateNotificationContentDetectionService(
        OptimizationGuideKeyedService* opt_guide,
        content::BrowserContext* context) const {
  CHECK(opt_guide);
  CHECK(context);
  auto database_manager =
      g_browser_process->safe_browsing_service()->database_manager();
  scoped_refptr<base::SequencedTaskRunner> background_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT});
  return std::make_unique<NotificationContentDetectionService>(
      opt_guide, background_task_runner, database_manager, context);
}

}  // namespace safe_browsing
