// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/resize_shadow_controller.h"

#include <memory>

#include "ash/public/cpp/window_properties.h"
#include "ash/shell.h"
#include "ash/wm/overview/overview_controller.h"
#include "ash/wm/resize_shadow.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state.h"
#include "chromeos/ui/base/window_properties.h"
#include "chromeos/ui/base/window_state_type.h"
#include "chromeos/ui/frame/frame_utils.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace ash {
namespace {

// If window rounded corners are larger than the threshold, use resize shadow
// designed for larger rounded corners.
constexpr int kLargeRoundedCornerThreshold = 2;

}  // namespace

ResizeShadowController::ResizeShadowController() = default;

ResizeShadowController::~ResizeShadowController() {
  RemoveAllShadows();
}

void ResizeShadowController::ShowShadow(aura::Window* window, int hit_test) {
  RecreateShadowIfNeeded(window);
  if (ShouldShowShadowForWindow(window) && window->IsVisible())
    GetShadowForWindow(window)->ShowForHitTest(hit_test);
}

void ResizeShadowController::TryShowAllShadows() {
  for (const auto& shadow : window_shadows_)
    UpdateShadowVisibility(shadow.first, shadow.first->IsVisible());
}

void ResizeShadowController::HideShadow(aura::Window* window) {
  ResizeShadow* shadow = GetShadowForWindow(window);
  if (!shadow)
    return;
  UpdateShadowVisibility(window, false);
}

void ResizeShadowController::HideAllShadows() {
  for (auto& shadow : window_shadows_) {
    if (!shadow.second)
      continue;

    switch (shadow.second->type_) {
      case ResizeShadowType::kLock: {  // Hides lock style of shadow
        UpdateShadowVisibility(shadow.first, false);
        break;
      }
      case ResizeShadowType::kUnlock: {  // Deletes unlock style of shadow
        shadow.second.reset();
        break;
      }
    }
  }
}

void ResizeShadowController::OnCrossFadeAnimationCompleted(
    aura::Window* window) {
  if (auto* shadow = GetShadowForWindow(window)) {
    shadow->ReparentLayer();
  }
}

void ResizeShadowController::RemoveAllShadows() {
  windows_observation_.RemoveAllObservations();
  window_shadows_.clear();
}

void ResizeShadowController::OnWindowHierarchyChanged(
    const aura::WindowObserver::HierarchyChangeParams& params) {
  ResizeShadow* shadow = GetShadowForWindow(params.target);
  if (shadow)
    shadow->ReparentLayer();
}

void ResizeShadowController::OnWindowVisibilityChanging(aura::Window* window,
                                                        bool visible) {
  UpdateShadowVisibility(window, visible);
}

void ResizeShadowController::OnWindowBoundsChanged(
    aura::Window* window,
    const gfx::Rect& old_bounds,
    const gfx::Rect& new_bounds,
    ui::PropertyChangeReason reason) {
  ResizeShadow* shadow = GetShadowForWindow(window);
  if (shadow && window->GetProperty(aura::client::kUseWindowBoundsForShadow))
    shadow->UpdateBoundsAndVisibility();
}

void ResizeShadowController::OnWindowStackingChanged(aura::Window* window) {
  ResizeShadow* shadow = GetShadowForWindow(window);
  if (shadow)
    shadow->ReparentLayer();
}

void ResizeShadowController::OnWindowDestroying(aura::Window* window) {
  windows_observation_.RemoveObservation(window);
  window_shadows_.erase(window);
}

void ResizeShadowController::OnWindowPropertyChanged(aura::Window* window,
                                                     const void* key,
                                                     intptr_t old) {
  if (key == aura::client::kShowStateKey) {
    UpdateShadowVisibility(window, window->IsVisible());
    return;
  }

  // If the resize shadow is being shown, ensure that shadow is configured
  // correctly for either a rounded window or squared window.
  if (ShouldShowShadowForWindow(window) &&
      chromeos::CanPropertyEffectWindowRoundedCorners(key)) {
    RecreateShadowIfNeeded(window);
    UpdateShadowVisibility(window, window->IsVisible());
    return;
  }
}

void ResizeShadowController::OnWindowAddedToRootWindow(aura::Window* window) {
  ResizeShadow* shadow = GetShadowForWindow(window);
  if (shadow) {
    shadow->OnWindowParentToRootWindow();
  }
}

void ResizeShadowController::UpdateResizeShadowBoundsOfWindow(
    aura::Window* window,
    const gfx::Rect& bounds) {
  ResizeShadow* shadow = GetShadowForWindow(window);
  if (shadow)
    shadow->UpdateBounds(bounds);
}

ResizeShadow* ResizeShadowController::GetShadowForWindowForTest(
    aura::Window* window) {
  return GetShadowForWindow(window);
}

void ResizeShadowController::RecreateShadowIfNeeded(aura::Window* window) {
  if (!windows_observation_.IsObservingSource(window))
    windows_observation_.AddObservation(window);
  ResizeShadow* shadow = GetShadowForWindow(window);
  const ash::ResizeShadowType type =
      window->GetProperty(ash::kResizeShadowTypeKey);
  const gfx::RoundedCornersF window_radii =
      ash::WindowState::Get(window)->GetWindowRoundedCorners();
  const int corner_radius = window_radii.upper_left();
  const bool has_large_rounded_corners =
      corner_radius > kLargeRoundedCornerThreshold;

  // If the `window` has a resize shadow with the requested type and the shadow
  // is configured for small/large rounded corners, no need to recreate it.
  if (shadow && shadow->type_ == type &&
      shadow->is_for_large_rounded_corners() == has_large_rounded_corners) {
    return;
  }

  ResizeShadow::InitParams params;
  if (type == ResizeShadowType::kLock) {
    params.thickness = 6;
    params.shadow_corner_radius = 6;
    params.window_corner_radius = 2;
    params.opacity = 0.3f;
    params.color = gfx::kGoogleGrey900;
    params.hit_test_enabled = false;
    params.hide_duration_ms = 0;
    params.is_for_large_rounded_corners = false;
  }

  // Configure window and shadow corner radius when `window` has rounded
  // corners.
  if (has_large_rounded_corners) {
    params.thickness = 6;
    params.window_corner_radius = corner_radius;
    params.shadow_corner_radius = 16;
    params.is_for_large_rounded_corners = true;
  }

  auto new_shadow = std::make_unique<ResizeShadow>(window, params, type);

  auto it = window_shadows_.find(window);
  if (it == window_shadows_.end())
    window_shadows_.insert(std::make_pair(window, std::move(new_shadow)));
  else
    it->second = std::move(new_shadow);
}

ResizeShadow* ResizeShadowController::GetShadowForWindow(
    aura::Window* window) const {
  auto it = window_shadows_.find(window);
  return it != window_shadows_.end() ? it->second.get() : nullptr;
}

void ResizeShadowController::UpdateShadowVisibility(aura::Window* window,
                                                    bool visible) const {
  ResizeShadow* shadow = GetShadowForWindow(window);
  if (!shadow)
    return;

  if (shadow->type_ == ResizeShadowType::kLock) {
    visible &= ShouldShowShadowForWindow(window);
    if (visible)
      shadow->ShowForHitTest();
  }

  if (!visible)
    shadow->Hide();
}

bool ResizeShadowController::ShouldShowShadowForWindow(
    aura::Window* window) const {
  // Hide the shadow if it's a maximized/fullscreen/minimized window or the
  // overview mode is active or if the shadow is disabled.
  if (window->GetProperty(kDisableResizeShadow)) {
    return false;
  }
  ui::mojom::WindowShowState show_state =
      window->GetProperty(aura::client::kShowStateKey);
  return show_state != ui::mojom::WindowShowState::kFullscreen &&
         show_state != ui::mojom::WindowShowState::kMaximized &&
         show_state != ui::mojom::WindowShowState::kMinimized &&
         !Shell::Get()->overview_controller()->InOverviewSession();
}

}  // namespace ash
