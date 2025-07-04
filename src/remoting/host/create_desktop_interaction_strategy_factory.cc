// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/create_desktop_interaction_strategy_factory.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "remoting/host/desktop_interaction_strategy.h"
#include "remoting/host/legacy_interaction_strategy.h"

#if BUILDFLAG(IS_LINUX)
#include "remoting/host/linux/gnome_interaction_strategy.h"
#endif  // BUILDFLAG(IS_LINUX)

namespace remoting {

std::unique_ptr<DesktopInteractionStrategyFactory>
CreateDesktopInteractionStrategyFactory(
    scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> video_capture_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> input_task_runner) {
#if BUILDFLAG(IS_LINUX)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("enable-wayland")) {
    return std::make_unique<GnomeInteractionStrategyFactory>(ui_task_runner);
  }
#endif  // BUILDFLAG(IS_LINUX)

  return std::make_unique<LegacyInteractionStrategyFactory>(
      std::move(caller_task_runner), std::move(ui_task_runner),
      std::move(video_capture_task_runner), std::move(input_task_runner));
}

}  // namespace remoting
