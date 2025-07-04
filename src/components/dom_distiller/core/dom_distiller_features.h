// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DOM_DISTILLER_CORE_DOM_DISTILLER_FEATURES_H_
#define COMPONENTS_DOM_DISTILLER_CORE_DOM_DISTILLER_FEATURES_H_

#include "base/feature_list.h"

namespace dom_distiller {

// Returns true when flag enable-dom-distiller is set or reader mode is enabled
// from flags or Finch.
bool IsDomDistillerEnabled();

bool ShouldStartDistillabilityService();

BASE_DECLARE_FEATURE(kReaderModeUseReadability);
bool ShouldUseReadabilityDistiller();
bool ShouldUseReadabilityHeuristic();

#if BUILDFLAG(IS_ANDROID)
// Feature declarations below -- alphabetical order.
BASE_DECLARE_FEATURE(kReaderModeAutoDistill);
BASE_DECLARE_FEATURE(kReaderModeDistillInApp);
BASE_DECLARE_FEATURE(kReaderModeImprovements);
#endif

}  // namespace dom_distiller

#endif  // COMPONENTS_DOM_DISTILLER_CORE_DOM_DISTILLER_FEATURES_H_
