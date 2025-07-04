// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_non_client_frame_view.h"

#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/themes/custom_theme_supplier.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_types.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/grit/theme_resources.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/theme_provider.h"
#include "ui/color/color_id.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/background.h"
#include "ui/views/window/hit_test_utils.h"

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/taskbar/taskbar_decorator_win.h"
#include "ui/display/win/screen_win.h"
#include "ui/views/win/hwnd_util.h"
#endif

// static
constexpr int BrowserNonClientFrameView::kMinimumDragHeight;

BrowserNonClientFrameView::BrowserNonClientFrameView(BrowserFrame* frame,
                                                     BrowserView* browser_view)
    : frame_(frame), browser_view_(browser_view) {
  DCHECK(frame_);
  DCHECK(browser_view_);
}

BrowserNonClientFrameView::~BrowserNonClientFrameView() = default;

void BrowserNonClientFrameView::OnBrowserViewInitViewsComplete() {
  UpdateMinimumSize();
}

void BrowserNonClientFrameView::OnFullscreenStateChanged() {
  if (frame_->IsFullscreen()) {
    browser_view_->HideDownloadShelf();
  } else {
    browser_view_->UnhideDownloadShelf();
  }
}

bool BrowserNonClientFrameView::CaptionButtonsOnLeadingEdge() const {
  return false;
}

void BrowserNonClientFrameView::UpdateFullscreenTopUI() {}

bool BrowserNonClientFrameView::ShouldHideTopUIForFullscreen() const {
  return frame_->IsFullscreen();
}

bool BrowserNonClientFrameView::CanUserExitFullscreen() const {
  return true;
}

bool BrowserNonClientFrameView::IsFrameCondensed() const {
  return frame_->IsMaximized() || frame_->IsFullscreen();
}

bool BrowserNonClientFrameView::HasVisibleBackgroundTabShapes(
    BrowserFrameActiveState active_state) const {
  DCHECK(browser_view_->GetSupportsTabStrip());

  TabStrip* const tab_strip = browser_view_->tabstrip();

  const bool active = ShouldPaintAsActiveForState(active_state);
  const std::optional<int> bg_id =
      tab_strip->GetCustomBackgroundId(active_state);
  if (bg_id.has_value()) {
    // If the theme has a custom tab background image, assume tab shapes are
    // visible.  This is pessimistic; the theme may use the same image as the
    // frame, just shifted to align, or a solid-color image the same color as
    // the frame; but to detect this we'd need to do some kind of aligned
    // rendering comparison, which seems not worth it.
    const ui::ThemeProvider* tp = GetThemeProvider();
    if (tp->HasCustomImage(bg_id.value())) {
      return true;
    }

    // Inactive tab background images are copied from the active ones, so in the
    // inactive case, check the active image as well.
    if (!active) {
      const int active_id = browser_view_->GetIncognito()
                                ? IDR_THEME_TAB_BACKGROUND_INCOGNITO
                                : IDR_THEME_TAB_BACKGROUND;
      if (tp->HasCustomImage(active_id)) {
        return true;
      }
    }

    // The tab image is a tinted version of the frame image.  Tabs are visible
    // iff the tint has some visible effect.
    return color_utils::IsHSLShiftMeaningful(
        tp->GetTint(ThemeProperties::TINT_BACKGROUND_TAB));
  }

  // Background tab shapes are visible iff the tab color differs from the frame
  // color.
  return TabStyle::Get()->GetTabBackgroundColor(
             TabStyle::TabSelectionState::kInactive,
             /*hovered=*/false, ShouldPaintAsActiveForState(active_state),
             *GetColorProvider()) != GetFrameColor(active_state);
}

bool BrowserNonClientFrameView::EverHasVisibleBackgroundTabShapes() const {
  return HasVisibleBackgroundTabShapes(BrowserFrameActiveState::kActive) ||
         HasVisibleBackgroundTabShapes(BrowserFrameActiveState::kInactive);
}

bool BrowserNonClientFrameView::CanDrawStrokes() const {
  // Web apps should not draw strokes if they don't have a tab strip.
  return !browser_view_->browser()->app_controller() ||
         browser_view_->browser()->app_controller()->has_tab_strip();
}

SkColor BrowserNonClientFrameView::GetCaptionColor(
    BrowserFrameActiveState active_state) const {
  return GetColorProvider()->GetColor(ShouldPaintAsActiveForState(active_state)
                                          ? kColorFrameCaptionActive
                                          : kColorFrameCaptionInactive);
}

SkColor BrowserNonClientFrameView::GetFrameColor(
    BrowserFrameActiveState active_state) const {
  return GetColorProvider()->GetColor(ShouldPaintAsActiveForState(active_state)
                                          ? ui::kColorFrameActive
                                          : ui::kColorFrameInactive);
}

std::optional<int> BrowserNonClientFrameView::GetCustomBackgroundId(
    BrowserFrameActiveState active_state) const {
  const ui::ThemeProvider* tp = GetThemeProvider();
  const bool incognito = browser_view_->GetIncognito();
  const bool active = ShouldPaintAsActiveForState(active_state);
  const int active_id =
      incognito ? IDR_THEME_TAB_BACKGROUND_INCOGNITO : IDR_THEME_TAB_BACKGROUND;
  const int inactive_id = incognito
                              ? IDR_THEME_TAB_BACKGROUND_INCOGNITO_INACTIVE
                              : IDR_THEME_TAB_BACKGROUND_INACTIVE;
  const int id = active ? active_id : inactive_id;

  // tp->HasCustomImage() will only return true if the supplied ID has been
  // customized directly.  We also account for the following fallback cases:
  // * The inactive images are copied directly from the active ones if present
  // * Tab backgrounds are generated from frame backgrounds if present, and
  // * The incognito frame image is generated from the normal frame image, so
  //   in incognito mode we look at both.
  const bool has_custom_image =
      tp->HasCustomImage(id) || (!active && tp->HasCustomImage(active_id)) ||
      tp->HasCustomImage(IDR_THEME_FRAME) ||
      (incognito && tp->HasCustomImage(IDR_THEME_FRAME_INCOGNITO));
  return has_custom_image ? std::make_optional(id) : std::nullopt;
}

void BrowserNonClientFrameView::UpdateMinimumSize() {}

gfx::Insets BrowserNonClientFrameView::RestoredMirroredFrameBorderInsets()
    const {
  NOTREACHED();
}

gfx::Insets BrowserNonClientFrameView::GetInputInsets() const {
  NOTREACHED();
}

SkRRect BrowserNonClientFrameView::GetRestoredClipRegion() const {
  NOTREACHED();
}

int BrowserNonClientFrameView::GetTranslucentTopAreaHeight() const {
  return 0;
}

void BrowserNonClientFrameView::SetFrameBounds(const gfx::Rect& bounds) {
  frame_->SetBounds(bounds);
}

void BrowserNonClientFrameView::PaintAsActiveChanged() {
  // Changing the activation state may change the visible frame color.
  SchedulePaint();
}

bool BrowserNonClientFrameView::ShouldPaintAsActiveForState(
    BrowserFrameActiveState active_state) const {
  return (active_state == BrowserFrameActiveState::kUseCurrent)
             ? NonClientFrameView::ShouldPaintAsActive()
             : (active_state == BrowserFrameActiveState::kActive);
}

gfx::ImageSkia BrowserNonClientFrameView::GetFrameImage(
    BrowserFrameActiveState active_state) const {
  const ui::ThemeProvider* tp = GetThemeProvider();
  const int frame_image_id = ShouldPaintAsActiveForState(active_state)
                                 ? IDR_THEME_FRAME
                                 : IDR_THEME_FRAME_INACTIVE;
  return (tp->HasCustomImage(frame_image_id) ||
          tp->HasCustomImage(IDR_THEME_FRAME))
             ? *tp->GetImageSkiaNamed(frame_image_id)
             : gfx::ImageSkia();
}

gfx::ImageSkia BrowserNonClientFrameView::GetFrameOverlayImage(
    BrowserFrameActiveState active_state) const {
  if (browser_view_->GetIncognito() || !browser_view_->GetIsNormalType()) {
    return gfx::ImageSkia();
  }

  const ui::ThemeProvider* tp = GetThemeProvider();
  const int frame_overlay_image_id = ShouldPaintAsActiveForState(active_state)
                                         ? IDR_THEME_FRAME_OVERLAY
                                         : IDR_THEME_FRAME_OVERLAY_INACTIVE;
  return tp->HasCustomImage(frame_overlay_image_id)
             ? *tp->GetImageSkiaNamed(frame_overlay_image_id)
             : gfx::ImageSkia();
}

#if BUILDFLAG(IS_WIN)
// Sending the WM_NCPOINTERDOWN, WM_NCPOINTERUPDATE, and WM_NCPOINTERUP to the
// default window proc does not bring up the system menu on long press, so we
// use the gesture recognizer to turn it into a LONG_TAP gesture and handle it
// here. See https://crbug.com/1327506 for more info.
void BrowserNonClientFrameView::OnGestureEvent(ui::GestureEvent* event) {
  gfx::Point event_loc = event->location();
  // This opens the title bar system context menu on long press in the titlebar.
  // NonClientHitTest returns HTCAPTION if `event_loc` is in the empty space on
  // the titlebar.
  if (event->type() == ui::EventType::kGestureLongTap &&
      NonClientHitTest(event_loc) == HTCAPTION) {
    views::View::ConvertPointToScreen(this, &event_loc);
    event_loc = display::win::GetScreenWin()->DIPToScreenPoint(event_loc);
    views::ShowSystemMenuAtScreenPixelLocation(views::HWNDForView(this),
                                               event_loc);
    event->SetHandled();
  }
}

int BrowserNonClientFrameView::GetSystemMenuY() const {
  if (!browser_view()->GetTabStripVisible()) {
    return GetTopInset(false);
  }
  return GetBoundsForTabStripRegion(
             browser_view()->tab_strip_region_view()->GetMinimumSize())
             .bottom() -
         GetLayoutConstant(TABSTRIP_TOOLBAR_OVERLAP);
}
#endif  // BUILDFLAG(IS_WIN)

BEGIN_METADATA(BrowserNonClientFrameView)
END_METADATA
