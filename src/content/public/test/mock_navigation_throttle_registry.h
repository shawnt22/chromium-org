// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_MOCK_NAVIGATION_THROTTLE_REGISTRY_H_
#define CONTENT_PUBLIC_TEST_MOCK_NAVIGATION_THROTTLE_REGISTRY_H_

#include <vector>

#include "base/memory/raw_ref.h"
#include "content/public/browser/navigation_throttle_registry.h"

namespace content {

// This class implements NavigationThrottleRegistry functionalities with
// testing features. Tests that needs one of following functions may use
// this class.
// - Register a testing purpose NavigationThrottle after other
//   NavigationThrottles so that the throttle can run after them all.
// - Pass it instead of the real implementation to check if a module
//   under testing registers a target throttle.
//
// WARNING: If you want to register your testing throttle to the real registry,
// or make your throttle work with NavigationSimulator, consider using
// content::TestNavigationThrottleInserter instead.
//
// This class instance needs to outlive navigation throttles that are created
// with the instance as throttles will have a reference to it always.
class MockNavigationThrottleRegistry : public NavigationThrottleRegistry {
 public:
  enum class RegistrationMode {
    // AddThrottle() registers the passed throttle as a testing purpose throttle
    // that runs after other production throttles.
    kAutoRegistrationForTesting,

    // AddThrottle() doesn't register the passed throttle actually, but hold it
    // in the mock. Users can query the hold throttles by
    // throttles(), or call ContainsHeldThrottle() to check if AddThrottle() was
    // called with a specific throttle. The held
    // throttles can be registered manually via RegisterHeldThrottles().
    kHold,
  };
  // `mock_navigation_handle` should outlive this instance.
  explicit MockNavigationThrottleRegistry(
      NavigationHandle* mock_navigation_handle,
      RegistrationMode registration_mode =
          RegistrationMode::kAutoRegistrationForTesting);
  ~MockNavigationThrottleRegistry() override;

  // Implements NavigationThrottleRegistry:
  NavigationHandle& GetNavigationHandle() override;
  void AddThrottle(std::unique_ptr<NavigationThrottle> throttle) override;
  // Following methods are not supported in this mock, and returns false always.
  bool HasThrottle(const std::string& name) override;
  bool EraseThrottleForTesting(const std::string& name) override;

  // Checks if the registry running with `kHold` mode contains a throttle with
  // the given name.
  bool ContainsHeldThrottle(const std::string& name);

  // Registers the held `throttles_` that are added in the registry running with
  /// `kHold` mode. The throttles are removed from `throttles_` and will run for
  // the underlying navigation.
  void RegisterHeldThrottles();

  std::vector<std::unique_ptr<NavigationThrottle>>& throttles() {
    return throttles_;
  }

 private:
  raw_ptr<NavigationHandle> navigation_handle_;
  const RegistrationMode registration_mode_;
  std::vector<std::unique_ptr<NavigationThrottle>> throttles_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_MOCK_NAVIGATION_THROTTLE_REGISTRY_H_
