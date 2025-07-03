// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_EMBEDDER_DEFAULT_MODEL_IOS_DEFAULT_BROWSER_PROMO_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_EMBEDDER_DEFAULT_MODEL_IOS_DEFAULT_BROWSER_PROMO_H_

#include <memory>

#include "base/feature_list.h"
#include "components/segmentation_platform/public/config.h"
#include "components/segmentation_platform/public/model_provider.h"

namespace segmentation_platform {

// Model to predict whether the user belongs to IosDefaultBrowserPromo segment.
// NOTE: This default model is solely used to ensure histograms are collected
// during first run.
class IosDefaultBrowserPromo : public DefaultModelProvider {
 public:
  IosDefaultBrowserPromo();
  ~IosDefaultBrowserPromo() override = default;

  IosDefaultBrowserPromo(const IosDefaultBrowserPromo&) = delete;
  IosDefaultBrowserPromo& operator=(const IosDefaultBrowserPromo&) = delete;

  static std::unique_ptr<Config> GetConfig();

  // ModelProvider implementation.
  std::unique_ptr<ModelConfig> GetModelConfig() override;
  void ExecuteModelWithInput(const ModelProvider::Request& inputs,
                             ExecutionCallback callback) override;
};

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_EMBEDDER_DEFAULT_MODEL_IOS_DEFAULT_BROWSER_PROMO_H_
