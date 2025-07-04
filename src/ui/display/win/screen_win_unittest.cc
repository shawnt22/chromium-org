// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/351564777): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "ui/display/win/screen_win.h"

#include <windows.h>

#include <inttypes.h>
#include <stddef.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/map_util.h"
#include "base/feature_list.h"
#include "base/features.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/types/optional_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/display.h"
#include "ui/display/display_features.h"
#include "ui/display/display_switches.h"
#include "ui/display/screen.h"
#include "ui/display/test/display_test_util.h"
#include "ui/display/win/display_info.h"
#include "ui/display/win/screen_win_display.h"
#include "ui/display/win/test/screen_util_win.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace display {
namespace win {
namespace {

class TestScreenWin : public ScreenWin {
 public:
  TestScreenWin(const std::vector<internal::DisplayInfo>& display_infos,
                const std::unordered_map<HMONITOR, MONITORINFOEX>& hmonitor_map,
                const std::unordered_map<HWND, gfx::Rect>& hwnd_map)
      : ScreenWin(false), hmonitor_map_(hmonitor_map), hwnd_map_(hwnd_map) {
    UpdateFromDisplayInfos(display_infos);
  }

  TestScreenWin(const TestScreenWin&) = delete;
  TestScreenWin& operator=(const TestScreenWin&) = delete;

  ~TestScreenWin() override { Screen::SetScreenInstance(old_screen_); }

 protected:
  // win::ScreenWin:
  HWND GetHWNDFromNativeWindow(gfx::NativeWindow window) const override {
    // NativeWindow is only used as an identifier in these tests, so interchange
    // a NativeWindow for an HWND for convenience.
    return reinterpret_cast<HWND>(window);
  }

  gfx::NativeWindow GetNativeWindowFromHWND(HWND hwnd) const override {
    // NativeWindow is only used as an identifier in these tests, so interchange
    // an HWND for a NativeWindow for convenience.
    return reinterpret_cast<gfx::NativeWindow>(hwnd);
  }

 private:
  // Finding the corresponding monitor from a point is generally handled by
  // Windows's MonitorFromPoint. This mocked function requires that the provided
  // point is contained entirely in the monitor.
  HMONITOR HMONITORFromScreenPoint(
      const gfx::Point& screen_point) const override {
    for (const auto& [hmonitor, monitor_info] : hmonitor_map_) {
      if (gfx::Rect(monitor_info.rcMonitor).Contains(screen_point))
        return hmonitor;
    }
    NOTREACHED();
  }

  // Finding the corresponding monitor from a rect is generally handled by
  // Windows's MonitorFromRect. This mocked function requires that the provided
  // rectangle overlap at least part of the monitor.
  HMONITOR HMONITORFromScreenRect(const gfx::Rect& screen_rect) const override {
    HMONITOR candidate = hmonitor_map_.begin()->first;
    int largest_area = 0;
    for (const auto& [hmonitor, monitor_info] : hmonitor_map_) {
      gfx::Rect bounds(monitor_info.rcMonitor);
      if (bounds.Intersects(screen_rect)) {
        bounds.Intersect(screen_rect);
        int area = bounds.height() * bounds.width();
        if (largest_area < area) {
          candidate = hmonitor;
          largest_area = area;
        }
      }
    }
    EXPECT_NE(largest_area, 0);
    return candidate;
  }

  // Finding the corresponding monitor from an HWND is generally handled by
  // Windows's MonitorFromWindow. Because we're mocking MonitorFromWindow here,
  // it's important that the HWND fully reside in the bounds of the display,
  // otherwise this could cause MonitorInfoFromScreenRect or
  // MonitorInfoFromScreenPoint to fail to find the monitor based off of a rect
  // or point within the HWND.
  HMONITOR HMONITORFromWindow(HWND hwnd, DWORD default_options) const override {
    auto search = hwnd_map_.find(hwnd);
    if (search != hwnd_map_.end())
      return HMONITORFromScreenRect(search->second);

    EXPECT_EQ(default_options, static_cast<DWORD>(MONITOR_DEFAULTTOPRIMARY));
    for (const auto& [hmonitor, monitor_info] : hmonitor_map_) {
      if (monitor_info.rcMonitor.left == 0 &&
          monitor_info.rcMonitor.top == 0) {
        return hmonitor;
      }
    }
    NOTREACHED();
  }

  std::optional<MONITORINFOEX> MonitorInfoFromHMONITOR(
      HMONITOR monitor) const override {
    return base::OptionalFromPtr(base::FindOrNull(hmonitor_map_, monitor));
  }

  HWND GetRootWindow(HWND hwnd) const override {
    return hwnd;
  }

  int GetSystemMetrics(int metric) const override {
    return metric;
  }

  raw_ptr<Screen> old_screen_ = Screen::SetScreenInstance(this);
  std::unordered_map<HMONITOR, MONITORINFOEX> hmonitor_map_;
  std::unordered_map<HWND, gfx::Rect> hwnd_map_;
};

Screen* GetScreen() {
  return Screen::GetScreen();
}

// Allows tests to specify the screen and associated state.
class TestScreenWinInitializer {
 public:
  virtual void AddMonitor(const gfx::Rect& pixel_bounds,
                          const gfx::Rect& pixel_work,
                          const wchar_t* device_name,
                          float device_scale_factor,
                          DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech =
                              DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER) = 0;

  virtual HWND CreateFakeHwnd(const gfx::Rect& bounds) = 0;

  virtual HMONITOR CreateFakeHMONITOR(const MONITORINFOEX& info) = 0;
};

class TestScreenWinManager final : public TestScreenWinInitializer {
 public:
  TestScreenWinManager() = default;

  TestScreenWinManager(const TestScreenWinManager&) = delete;
  TestScreenWinManager& operator=(const TestScreenWinManager&) = delete;

  ~TestScreenWinManager() = default;

  void AddMonitor(const gfx::Rect& pixel_bounds,
                  const gfx::Rect& pixel_work,
                  const wchar_t* device_name,
                  float device_scale_factor,
                  DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech =
                      DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER) override {
    MONITORINFOEX monitor_info =
        win::test::CreateMonitorInfo(pixel_bounds, pixel_work, device_name);
    HMONITOR monitor = CreateFakeHMONITOR(monitor_info);
    std::optional<HMONITOR> cached_hmonitor;
    if (features::IsScreenWinDisplayLookupByHMONITOREnabled()) {
      cached_hmonitor = monitor;
    }
    display_infos_.push_back(internal::DisplayInfo(
        std::move(cached_hmonitor), monitor_info, device_scale_factor, 1.0f,
        Display::ROTATE_0, 60.0f, gfx::Vector2dF(), tech, std::string()));
  }

  HWND CreateFakeHwnd(const gfx::Rect& bounds) override {
    EXPECT_EQ(screen_win_, nullptr);
    hwnd_map_.emplace(++hwndLast_, bounds);
    return hwndLast_;
  }

  HMONITOR CreateFakeHMONITOR(const MONITORINFOEX& info) override {
    EXPECT_EQ(screen_win_, nullptr);
    hmonitor_map_.emplace(++hmonitorLast_, info);
    return hmonitorLast_;
  }

  void InitializeScreenWin() {
    ASSERT_EQ(screen_win_, nullptr);
    screen_win_ = std::make_unique<TestScreenWin>(display_infos_, hmonitor_map_,
                                                  hwnd_map_);
  }

  ScreenWin* GetScreenWin() {
    return screen_win_.get();
  }

 private:
  HWND hwndLast_ = nullptr;
  HMONITOR hmonitorLast_ = nullptr;
  std::unique_ptr<ScreenWin> screen_win_;
  std::vector<internal::DisplayInfo> display_infos_;
  std::unordered_map<HMONITOR, MONITORINFOEX> hmonitor_map_;
  std::unordered_map<HWND, gfx::Rect> hwnd_map_;
};

class ScreenWinTest : public ::testing::TestWithParam<bool> {
  using Super = ::testing::TestWithParam<bool>;

 public:
  ScreenWinTest(const ScreenWinTest&) = delete;
  ScreenWinTest& operator=(const ScreenWinTest&) = delete;

  static std::string ParamInfoToString(
      ::testing::TestParamInfo<bool> param_info) {
    return param_info.param ? "CachedHmonitor" : "UncachedHmonitor";
  }

 protected:
  ScreenWinTest() {
    // Always enable kReducePPMs. Toggle kScreenWinDisplayLookupByHMONITOR
    // based on the test param, to make sure it can be disabled independently.
    scoped_feature_list_.InitWithFeatureStates({
        {base::features::kReducePPMs, true},
        {display::features::kScreenWinDisplayLookupByHMONITOR,
         use_cached_hmonitor_},
    });
  }

  void SetUp() override {
    Super::SetUp();
    screen_win_initializer_ = std::make_unique<TestScreenWinManager>();
    SetUpScreen(screen_win_initializer_.get());
    screen_win_initializer_->InitializeScreenWin();
  }

  void TearDown() override {
    screen_win_initializer_.reset();
    Super::TearDown();
  }

  virtual void SetUpScreen(TestScreenWinInitializer* initializer) = 0;

  gfx::NativeWindow GetNativeWindowFromHWND(HWND hwnd) const {
    ScreenWin* screen_win = screen_win_initializer_->GetScreenWin();
    return screen_win->GetNativeWindowFromHWND(hwnd);
  }

  ScreenWin* GetScreenWin() { return screen_win_initializer_->GetScreenWin(); }

 protected:
  bool use_cached_hmonitor_ = GetParam();

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  std::unique_ptr<TestScreenWinManager> screen_win_initializer_;
};

// Single Display of 1.0 Device Scale Factor.
class ScreenWinTestSingleDisplay1x : public ScreenWinTest {
 public:
  ScreenWinTestSingleDisplay1x() = default;

  ScreenWinTestSingleDisplay1x(const ScreenWinTestSingleDisplay1x&) = delete;
  ScreenWinTestSingleDisplay1x& operator=(const ScreenWinTestSingleDisplay1x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            1.0);
    fake_hwnd_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
  }

  HWND GetFakeHwnd() {
    return fake_hwnd_;
  }

 private:
  HWND fake_hwnd_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestSingleDisplay1x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

void expect_point_f_eq(gfx::PointF val1, gfx::PointF val2) {
  EXPECT_FLOAT_EQ(val1.x(), val2.x());
  EXPECT_FLOAT_EQ(val1.y(), val2.y());
}

}  // namespace

TEST_P(ScreenWinTestSingleDisplay1x, ScreenToDIPPoints) {
  gfx::PointF origin(0, 0);
  gfx::PointF middle(365, 694);
  gfx::PointF lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->ScreenToDIPPoint(origin));
  EXPECT_EQ(middle, GetScreenWin()->ScreenToDIPPoint(middle));
  EXPECT_EQ(lower_right, GetScreenWin()->ScreenToDIPPoint(lower_right));
}

TEST_P(ScreenWinTestSingleDisplay1x, DIPToScreenPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenPoint(origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenPoint(middle));
  EXPECT_EQ(lower_right, GetScreenWin()->DIPToScreenPoint(lower_right));
}

TEST_P(ScreenWinTestSingleDisplay1x, ClientToDIPPoints) {
  HWND hwnd = GetFakeHwnd();
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPPoint(hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPPoint(hwnd, middle));
  EXPECT_EQ(lower_right, GetScreenWin()->ClientToDIPPoint(hwnd, lower_right));
}

TEST_P(ScreenWinTestSingleDisplay1x, DIPToClientPoints) {
  HWND hwnd = GetFakeHwnd();
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientPoint(hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientPoint(hwnd, middle));
  EXPECT_EQ(lower_right, GetScreenWin()->DIPToClientPoint(hwnd, lower_right));
}

TEST_P(ScreenWinTestSingleDisplay1x, ScreenToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->ScreenToDIPRect(hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ScreenToDIPRect(hwnd, middle));
}

TEST_P(ScreenWinTestSingleDisplay1x, DIPToScreenRects) {
  HWND hwnd = GetFakeHwnd();
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenRect(hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenRect(hwnd, middle));
}

TEST_P(ScreenWinTestSingleDisplay1x, DIPToScreenRectNullHWND) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenRect(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenRect(nullptr, middle));
}

TEST_P(ScreenWinTestSingleDisplay1x, ClientToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPRect(hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPRect(hwnd, middle));
}

TEST_P(ScreenWinTestSingleDisplay1x, DIPToClientRects) {
  HWND hwnd = GetFakeHwnd();
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientRect(hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientRect(hwnd, middle));
}

TEST_P(ScreenWinTestSingleDisplay1x, ScreenToDIPSize) {
  HWND hwnd = GetFakeHwnd();
  gfx::Size size(42, 131);
  EXPECT_EQ(size, GetScreenWin()->ScreenToDIPSize(hwnd, size));
}

TEST_P(ScreenWinTestSingleDisplay1x, DIPToScreenSize) {
  HWND hwnd = GetFakeHwnd();
  gfx::Size size(42, 131);
  EXPECT_EQ(size, GetScreenWin()->DIPToScreenSize(hwnd, size));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetSystemMetricsInDIP) {
  EXPECT_EQ(31, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(42, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetFakeHwnd()));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(1u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1200), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1100), displays[0].work_area());
}

TEST_P(ScreenWinTestSingleDisplay1x, GetNumDisplays) {
  EXPECT_EQ(1, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestSingleDisplay1x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  gfx::NativeWindow native_window = GetNativeWindowFromHWND(GetFakeHwnd());
  EXPECT_EQ(screen->GetAllDisplays()[0],
            screen->GetDisplayNearestWindow(native_window));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(250, 952)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(display,
            screen->GetDisplayMatching(gfx::Rect(1819, 1099, 100, 100)));
}

TEST_P(ScreenWinTestSingleDisplay1x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

TEST_P(ScreenWinTestSingleDisplay1x, DisconnectPrimaryDisplay) {
  auto* screen = GetScreen();
  ASSERT_EQ(1, screen->GetNumDisplays());
  auto primary = screen->GetPrimaryDisplay();
  const int64_t primary_id = primary.id();
  EXPECT_NE(primary_id, display::kInvalidDisplayId);

  if (use_cached_hmonitor_) {
    // Validate that the ScreenWinDisplay starts with a cached HMONITOR.
    const auto screen_win_display =
        GetScreenWin()->GetScreenWinDisplayWithDisplayId(primary_id);
    EXPECT_NE(screen_win_display.hmonitor(), std::nullopt);
    EXPECT_EQ(GetScreenWin()
                  ->GetScreenWinDisplayNearestScreenPoint(gfx::Point(0, 0))
                  .display(),
              screen_win_display.display());
  }

  GetScreenWin()->UpdateFromDisplayInfos({});

  if (base::FeatureList::IsEnabled(features::kSkipEmptyDisplayHotplugEvent)) {
    EXPECT_EQ(1, screen->GetNumDisplays());

    auto new_primary = screen->GetPrimaryDisplay();
    EXPECT_FALSE(new_primary.detected());
    // `GetPrimaryDisplay()` should return the same except for the detected
    // status.
    new_primary.set_detected(true);
    EXPECT_EQ(primary, new_primary);

    if (use_cached_hmonitor_) {
      // The ScreenWinDisplay's cached HMONITOR should be invalidated.
      // GetScreenWinDisplayNearestScreenPoint() should still work without it.
      const auto screen_win_display =
          GetScreenWin()->GetScreenWinDisplayWithDisplayId(primary_id);
      EXPECT_EQ(screen_win_display.hmonitor(), std::nullopt);
      EXPECT_EQ(GetScreenWin()
                    ->GetScreenWinDisplayNearestScreenPoint(gfx::Point(0, 0))
                    .display(),
                screen_win_display.display());
    }
  }
}

namespace {

// Single Display of 1.25 Device Scale Factor.
class ScreenWinTestSingleDisplay1_25x : public ScreenWinTest {
 public:
  ScreenWinTestSingleDisplay1_25x() = default;

  ScreenWinTestSingleDisplay1_25x(const ScreenWinTestSingleDisplay1_25x&) =
      delete;
  ScreenWinTestSingleDisplay1_25x& operator=(
      const ScreenWinTestSingleDisplay1_25x&) = delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            1.25);
    fake_hwnd_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
  }

  HWND GetFakeHwnd() {
    return fake_hwnd_;
  }

 private:
  HWND fake_hwnd_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestSingleDisplay1_25x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestSingleDisplay1_25x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(292, 555.2F),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(1535.2F, 959.2F),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(303, 577),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(243, 462)));
  EXPECT_EQ(gfx::Point(1598, 998),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1279, 799)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, ClientToDIPPoints) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(292, 555),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1535, 959),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, DIPToClientPoints) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(303, 577),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(243, 462)));
  EXPECT_EQ(gfx::Point(1598, 998),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(1279, 799)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, ScreenToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 40, 80),
            GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(202, 396, 34, 43),
            GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, DIPToScreenRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 43, 84),
            GetScreenWin()->DIPToScreenRect(hwnd, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(gfx::Rect(210, 412, 35, 46),
            GetScreenWin()->DIPToScreenRect(hwnd, gfx::Rect(168, 330, 28, 36)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 43, 84),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(
      gfx::Rect(210, 412, 35, 46),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(168, 330, 28, 36)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, ClientToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 40, 80),
            GetScreenWin()->ClientToDIPRect(hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(202, 396, 34, 43),
            GetScreenWin()->ClientToDIPRect(hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, DIPToClientRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 43, 84),
            GetScreenWin()->DIPToClientRect(hwnd, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(gfx::Rect(210, 412, 35, 46),
            GetScreenWin()->DIPToClientRect(hwnd, gfx::Rect(168, 330, 28, 36)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, ScreenToDIPSize) {
  EXPECT_EQ(gfx::Size(34, 105),
            GetScreenWin()->ScreenToDIPSize(GetFakeHwnd(), gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, DIPToScreenSize) {
  EXPECT_EQ(gfx::Size(35, 110),
            GetScreenWin()->DIPToScreenSize(GetFakeHwnd(), gfx::Size(28, 88)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, GetSystemMetricsInDIP) {
  EXPECT_EQ(25, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(34, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.25, GetScreenWin()->GetScaleFactorForHWND(GetFakeHwnd()));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(1u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 1536, 960), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 1536, 880), displays[0].work_area());
}

TEST_P(ScreenWinTestSingleDisplay1_25x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  gfx::NativeWindow native_window = GetNativeWindowFromHWND(GetFakeHwnd());
  EXPECT_EQ(screen->GetAllDisplays()[0],
            screen->GetDisplayNearestWindow(native_window));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(250, 952)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(1535, 959)));
}

TEST_P(ScreenWinTestSingleDisplay1_25x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(display,
            screen->GetDisplayMatching(gfx::Rect(1435, 859, 100, 100)));
}
TEST_P(ScreenWinTestSingleDisplay1_25x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Single Display of 1.5 Device Scale Factor.
class ScreenWinTestSingleDisplay1_5x : public ScreenWinTest {
 public:
  ScreenWinTestSingleDisplay1_5x() = default;

  ScreenWinTestSingleDisplay1_5x(const ScreenWinTestSingleDisplay1_5x&) =
      delete;
  ScreenWinTestSingleDisplay1_5x& operator=(
      const ScreenWinTestSingleDisplay1_5x&) = delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            1.5);
    fake_hwnd_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
  }

  HWND GetFakeHwnd() {
    return fake_hwnd_;
  }

 private:
  HWND fake_hwnd_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestSingleDisplay1_5x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestSingleDisplay1_5x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(243.3333F, 462.6666F),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(1279.3333F, 799.3333F),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 693),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(243, 462)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1279, 799)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, ClientToDIPPoints) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(243, 462),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1279, 799),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, DIPToClientPoints) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 693),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(243, 462)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(1279, 799)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, ScreenToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 34, 67),
            GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(168, 330, 28, 36),
            GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, DIPToScreenRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 51, 101),
            GetScreenWin()->DIPToScreenRect(hwnd, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(gfx::Rect(252, 495, 42, 54),
            GetScreenWin()->DIPToScreenRect(hwnd, gfx::Rect(168, 330, 28, 36)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 51, 101),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(
      gfx::Rect(252, 495, 42, 54),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(168, 330, 28, 36)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, ClientToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 34, 67),
            GetScreenWin()->ClientToDIPRect(hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(168, 330, 28, 36),
            GetScreenWin()->ClientToDIPRect(hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, DIPToClientRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 51, 101),
            GetScreenWin()->DIPToClientRect(hwnd, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(gfx::Rect(252, 495, 42, 54),
            GetScreenWin()->DIPToClientRect(hwnd, gfx::Rect(168, 330, 28, 36)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, ScreenToDIPSize) {
  EXPECT_EQ(gfx::Size(28, 88),
            GetScreenWin()->ScreenToDIPSize(GetFakeHwnd(), gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, DIPToScreenSize) {
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(GetFakeHwnd(), gfx::Size(28, 88)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, GetSystemMetricsInDIP) {
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(28, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.5, GetScreenWin()->GetScaleFactorForHWND(GetFakeHwnd()));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(1u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 1280, 800), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 1280, 734), displays[0].work_area());
}

TEST_P(ScreenWinTestSingleDisplay1_5x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  gfx::NativeWindow native_window = GetNativeWindowFromHWND(GetFakeHwnd());
  EXPECT_EQ(screen->GetAllDisplays()[0],
            screen->GetDisplayNearestWindow(native_window));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(250, 524)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(1279, 733)));
}

TEST_P(ScreenWinTestSingleDisplay1_5x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(display,
            screen->GetDisplayMatching(gfx::Rect(1179, 633, 100, 100)));
}
TEST_P(ScreenWinTestSingleDisplay1_5x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Single Display of 2.0 Device Scale Factor.
class ScreenWinTestSingleDisplay2x : public ScreenWinTest {
 public:
  ScreenWinTestSingleDisplay2x() = default;

  ScreenWinTestSingleDisplay2x(const ScreenWinTestSingleDisplay2x&) = delete;
  ScreenWinTestSingleDisplay2x& operator=(const ScreenWinTestSingleDisplay2x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            2.0);
    fake_hwnd_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
  }

  HWND GetFakeHwnd() {
    return fake_hwnd_;
  }

 private:
  HWND fake_hwnd_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestSingleDisplay2x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestSingleDisplay2x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(182.5, 347),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(959.5, 599.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay2x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(959, 599)));
}

TEST_P(ScreenWinTestSingleDisplay2x, ClientToDIPPoints) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599),
            GetScreenWin()->ClientToDIPPoint(hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestSingleDisplay2x, DIPToClientPoints) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(hwnd, gfx::Point(959, 599)));
}

TEST_P(ScreenWinTestSingleDisplay2x, ScreenToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50),
            GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(126, 248, 21, 26),
            GetScreenWin()->ScreenToDIPRect(hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestSingleDisplay2x, DIPToScreenRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(gfx::Rect(252, 496, 42, 52),
            GetScreenWin()->DIPToScreenRect(hwnd, gfx::Rect(126, 248, 21, 26)));
}

TEST_P(ScreenWinTestSingleDisplay2x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(126, 248, 21, 26)));
}

TEST_P(ScreenWinTestSingleDisplay2x, ClientToDIPRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50),
            GetScreenWin()->ClientToDIPRect(hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(126, 248, 21, 26),
            GetScreenWin()->ClientToDIPRect(hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestSingleDisplay2x, DIPToClientRects) {
  HWND hwnd = GetFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToClientRect(hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(gfx::Rect(252, 496, 42, 52),
            GetScreenWin()->DIPToClientRect(hwnd, gfx::Rect(126, 248, 21, 26)));
}

TEST_P(ScreenWinTestSingleDisplay2x, ScreenToDIPSize) {
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(GetFakeHwnd(), gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestSingleDisplay2x, DIPToScreenSize) {
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(GetFakeHwnd(), gfx::Size(21, 66)));
}

TEST_P(ScreenWinTestSingleDisplay2x, GetSystemMetricsInDIP) {
  EXPECT_EQ(16, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestSingleDisplay2x, GetScaleFactorForHWND) {
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetFakeHwnd()));
}

TEST_P(ScreenWinTestSingleDisplay2x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(1u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 960, 600), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 960, 550), displays[0].work_area());
}

TEST_P(ScreenWinTestSingleDisplay2x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  gfx::NativeWindow native_window = GetNativeWindowFromHWND(GetFakeHwnd());
  EXPECT_EQ(screen->GetAllDisplays()[0],
            screen->GetDisplayNearestWindow(native_window));
}

TEST_P(ScreenWinTestSingleDisplay2x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(125, 476)));
  EXPECT_EQ(display, screen->GetDisplayNearestPoint(gfx::Point(959, 599)));
}

TEST_P(ScreenWinTestSingleDisplay2x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  Display display = screen->GetAllDisplays()[0];
  EXPECT_EQ(display, screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(display, screen->GetDisplayMatching(gfx::Rect(859, 499, 100, 100)));
}

namespace {

// Two Displays of 1.0 Device Scale Factor.
class ScreenWinTestTwoDisplays1x : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplays1x() = default;

  ScreenWinTestTwoDisplays1x(const ScreenWinTestTwoDisplays1x&) = delete;
  ScreenWinTestTwoDisplays1x& operator=(const ScreenWinTestTwoDisplays1x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            1.0);
    initializer->AddMonitor(gfx::Rect(1920, 0, 800, 600),
                            gfx::Rect(1920, 0, 800, 600),
                            L"secondary",
                            1.0);
    fake_hwnd_left_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
    fake_hwnd_right_ =
        initializer->CreateFakeHwnd(gfx::Rect(1920, 0, 800, 600));
  }

  HWND GetLeftFakeHwnd() {
    return fake_hwnd_left_;
  }

  HWND GetRightFakeHwnd() {
    return fake_hwnd_right_;
  }

 private:
  HWND fake_hwnd_left_ = nullptr;
  HWND fake_hwnd_right_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplays1x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplays1x, ScreenToDIPPoints) {
  gfx::PointF left_origin(0, 0);
  gfx::PointF left_middle(365, 694);
  gfx::PointF left_lower_right(1919, 1199);
  EXPECT_EQ(left_origin, GetScreenWin()->ScreenToDIPPoint(left_origin));
  EXPECT_EQ(left_middle, GetScreenWin()->ScreenToDIPPoint(left_middle));
  EXPECT_EQ(left_lower_right,
            GetScreenWin()->ScreenToDIPPoint(left_lower_right));

  gfx::PointF right_origin(1920, 0);
  gfx::PointF right_middle(2384, 351);
  gfx::PointF right_lower_right(2719, 599);
  EXPECT_EQ(right_origin, GetScreenWin()->ScreenToDIPPoint(right_origin));
  EXPECT_EQ(right_middle, GetScreenWin()->ScreenToDIPPoint(right_middle));
  EXPECT_EQ(right_lower_right,
            GetScreenWin()->ScreenToDIPPoint(right_lower_right));
}

TEST_P(ScreenWinTestTwoDisplays1x, DIPToScreenPoints) {
  gfx::Point left_origin(0, 0);
  gfx::Point left_middle(365, 694);
  gfx::Point left_lower_right(1919, 1199);
  EXPECT_EQ(left_origin, GetScreenWin()->DIPToScreenPoint(left_origin));
  EXPECT_EQ(left_middle, GetScreenWin()->DIPToScreenPoint(left_middle));
  EXPECT_EQ(left_lower_right,
            GetScreenWin()->DIPToScreenPoint(left_lower_right));

  gfx::Point right_origin(1920, 0);
  gfx::Point right_middle(2384, 351);
  gfx::Point right_lower_right(2719, 599);
  EXPECT_EQ(right_origin, GetScreenWin()->DIPToScreenPoint(right_origin));
  EXPECT_EQ(right_middle, GetScreenWin()->DIPToScreenPoint(right_middle));
  EXPECT_EQ(right_lower_right,
            GetScreenWin()->DIPToScreenPoint(right_lower_right));
}

TEST_P(ScreenWinTestTwoDisplays1x, ClientToDIPPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPPoint(left_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPPoint(left_hwnd, middle));
  EXPECT_EQ(lower_right,
            GetScreenWin()->ClientToDIPPoint(left_hwnd, lower_right));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPPoint(right_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPPoint(right_hwnd, middle));
  EXPECT_EQ(lower_right,
            GetScreenWin()->ClientToDIPPoint(right_hwnd, lower_right));
}

TEST_P(ScreenWinTestTwoDisplays1x, DIPToClientPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientPoint(left_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientPoint(left_hwnd, middle));
  EXPECT_EQ(lower_right,
            GetScreenWin()->DIPToClientPoint(left_hwnd, lower_right));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientPoint(right_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientPoint(right_hwnd, middle));
  EXPECT_EQ(lower_right,
            GetScreenWin()->DIPToClientPoint(right_hwnd, lower_right));
}

TEST_P(ScreenWinTestTwoDisplays1x, ScreenToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Rect left_origin(0, 0, 50, 100);
  gfx::Rect left_middle(253, 495, 41, 52);
  EXPECT_EQ(left_origin,
            GetScreenWin()->ScreenToDIPRect(left_hwnd, left_origin));
  EXPECT_EQ(left_middle,
            GetScreenWin()->ScreenToDIPRect(left_hwnd, left_middle));

  HWND right_hwnd = GetRightFakeHwnd();
  gfx::Rect right_origin(1920, 0, 200, 300);
  gfx::Rect right_middle(2000, 496, 100, 200);
  EXPECT_EQ(right_origin,
            GetScreenWin()->ScreenToDIPRect(right_hwnd, right_origin));
  EXPECT_EQ(right_middle,
            GetScreenWin()->ScreenToDIPRect(right_hwnd, right_middle));

  gfx::Rect right_origin_left(1900, 200, 100, 100);
  EXPECT_EQ(right_origin_left,
            GetScreenWin()->ScreenToDIPRect(right_hwnd, right_origin_left));
}

TEST_P(ScreenWinTestTwoDisplays1x, DIPToScreenRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Rect left_origin(0, 0, 50, 100);
  gfx::Rect left_middle(253, 495, 41, 52);
  EXPECT_EQ(left_origin,
            GetScreenWin()->DIPToScreenRect(left_hwnd, left_origin));
  EXPECT_EQ(left_middle,
            GetScreenWin()->DIPToScreenRect(left_hwnd, left_middle));

  HWND right_hwnd = GetRightFakeHwnd();
  gfx::Rect right_origin(1920, 0, 200, 300);
  gfx::Rect right_middle(2000, 496, 100, 200);
  EXPECT_EQ(right_origin,
            GetScreenWin()->DIPToScreenRect(right_hwnd, right_origin));
  EXPECT_EQ(right_middle,
            GetScreenWin()->DIPToScreenRect(right_hwnd, right_middle));

  gfx::Rect right_origin_left(1900, 200, 100, 100);
  EXPECT_EQ(right_origin_left,
            GetScreenWin()->DIPToScreenRect(right_hwnd, right_origin_left));
}

TEST_P(ScreenWinTestTwoDisplays1x, DIPToScreenRectNullHWND) {
  gfx::Rect left_origin(0, 0, 50, 100);
  gfx::Rect left_middle(253, 495, 41, 52);
  EXPECT_EQ(left_origin, GetScreenWin()->DIPToScreenRect(nullptr, left_origin));
  EXPECT_EQ(left_middle, GetScreenWin()->DIPToScreenRect(nullptr, left_middle));

  gfx::Rect right_origin(1920, 0, 200, 300);
  gfx::Rect right_middle(2000, 496, 100, 200);
  EXPECT_EQ(right_origin,
            GetScreenWin()->DIPToScreenRect(nullptr, right_origin));
  EXPECT_EQ(right_middle,
            GetScreenWin()->DIPToScreenRect(nullptr, right_middle));

  gfx::Rect right_origin_left(1900, 200, 100, 100);
  EXPECT_EQ(right_origin_left,
            GetScreenWin()->DIPToScreenRect(nullptr, right_origin_left));
}

TEST_P(ScreenWinTestTwoDisplays1x, ClientToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPRect(left_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPRect(left_hwnd, middle));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPRect(right_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPRect(right_hwnd, middle));
}

TEST_P(ScreenWinTestTwoDisplays1x, DIPToClientRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientRect(left_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientRect(left_hwnd, middle));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientRect(right_hwnd, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientRect(right_hwnd, middle));
}

TEST_P(ScreenWinTestTwoDisplays1x, ScreenToDIPSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Size size(42, 131);
  EXPECT_EQ(size, GetScreenWin()->ScreenToDIPSize(left_hwnd, size));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(size, GetScreenWin()->ScreenToDIPSize(right_hwnd, size));
}

TEST_P(ScreenWinTestTwoDisplays1x, DIPToScreenSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  gfx::Size size(42, 131);
  EXPECT_EQ(size, GetScreenWin()->DIPToScreenSize(left_hwnd, size));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(size, GetScreenWin()->DIPToScreenSize(right_hwnd, size));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetSystemMetricsInDIP) {
  EXPECT_EQ(31, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(42, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetLeftFakeHwnd()));
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetRightFakeHwnd()));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1200), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1100), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(1920, 0, 800, 600), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(1920, 0, 800, 600), displays[1].work_area());
}

TEST_P(ScreenWinTestTwoDisplays1x, GetNumDisplays) {
  EXPECT_EQ(2, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestTwoDisplays1x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  gfx::NativeWindow left_window = GetNativeWindowFromHWND(GetLeftFakeHwnd());
  EXPECT_EQ(left_display, screen->GetDisplayNearestWindow(left_window));

  gfx::NativeWindow right_window = GetNativeWindowFromHWND(GetRightFakeHwnd());
  EXPECT_EQ(right_display, screen->GetDisplayNearestWindow(right_window));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(250, 952)));
  EXPECT_EQ(left_display,
            screen->GetDisplayNearestPoint(gfx::Point(1919, 1199)));

  EXPECT_EQ(right_display, screen->GetDisplayNearestPoint(gfx::Point(1920, 0)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(2000, 400)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(2719, 599)));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(1819, 1099, 100, 100)));

  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(1920, 0, 100, 100)));
  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(2619, 499, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Two Displays of 2.0 Device Scale Factor.
class ScreenWinTestTwoDisplays2x : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplays2x() = default;

  ScreenWinTestTwoDisplays2x(const ScreenWinTestTwoDisplays2x&) = delete;
  ScreenWinTestTwoDisplays2x& operator=(const ScreenWinTestTwoDisplays2x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            2.0);
    initializer->AddMonitor(gfx::Rect(1920, 0, 800, 600),
                            gfx::Rect(1920, 0, 800, 600),
                            L"secondary",
                            2.0);
    fake_hwnd_left_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
    fake_hwnd_right_ =
        initializer->CreateFakeHwnd(gfx::Rect(1920, 0, 800, 600));
  }

  HWND GetLeftFakeHwnd() {
    return fake_hwnd_left_;
  }

  HWND GetRightFakeHwnd() {
    return fake_hwnd_right_;
  }

 private:
  HWND fake_hwnd_left_ = nullptr;
  HWND fake_hwnd_right_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplays2x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplays2x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(182.5, 347),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(959.5, 599.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));

  expect_point_f_eq(gfx::PointF(960, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1920, 0)));
  expect_point_f_eq(gfx::PointF(1192, 175.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2384, 351)));
  expect_point_f_eq(gfx::PointF(1359.5, 299.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2719, 599)));
}

TEST_P(ScreenWinTestTwoDisplays2x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(959, 599)));

  EXPECT_EQ(gfx::Point(1920, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(960, 0)));
  EXPECT_EQ(gfx::Point(2384, 350),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1192, 175)));
  EXPECT_EQ(gfx::Point(2718, 598),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1359, 299)));
}

TEST_P(ScreenWinTestTwoDisplays2x, ClientToDIPPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599), GetScreenWin()->ClientToDIPPoint(
                                      left_hwnd, gfx::Point(1919, 1199)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599), GetScreenWin()->ClientToDIPPoint(
                                      right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays2x, DIPToClientPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(959, 599)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(959, 599)));
}

TEST_P(ScreenWinTestTwoDisplays2x, ScreenToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ScreenToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ScreenToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(960, 0, 100, 150),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1920, 0, 200, 300)));
  EXPECT_EQ(gfx::Rect(1000, 248, 50, 100),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(2000, 496, 100, 200)));

  EXPECT_EQ(gfx::Rect(950, 100, 50, 50),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1900, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x, DIPToScreenRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToScreenRect(
                                          left_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(left_hwnd, gfx::Rect(126, 248, 21, 26)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(
      gfx::Rect(1920, 0, 200, 300),
      GetScreenWin()->DIPToScreenRect(right_hwnd, gfx::Rect(960, 0, 100, 150)));
  EXPECT_EQ(gfx::Rect(2000, 496, 100, 200),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(1000, 248, 50, 100)));

  EXPECT_EQ(
      gfx::Rect(1900, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(right_hwnd, gfx::Rect(950, 100, 50, 50)));
}

TEST_P(ScreenWinTestTwoDisplays2x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(126, 248, 21, 26)));

  EXPECT_EQ(
      gfx::Rect(1920, 0, 200, 300),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(960, 0, 100, 150)));
  EXPECT_EQ(
      gfx::Rect(2000, 496, 100, 200),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(1000, 248, 50, 100)));

  EXPECT_EQ(
      gfx::Rect(1900, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(950, 100, 50, 50)));
}

TEST_P(ScreenWinTestTwoDisplays2x, ClientToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ClientToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ClientToDIPRect(
                                         right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays2x, DIPToClientRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToClientRect(
                                          left_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(left_hwnd, gfx::Rect(126, 248, 21, 26)));
}

TEST_P(ScreenWinTestTwoDisplays2x, ScreenToDIPSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(left_hwnd, gfx::Size(42, 131)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays2x, DIPToScreenSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(left_hwnd, gfx::Size(21, 66)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(right_hwnd, gfx::Size(21, 66)));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetSystemMetricsInDIP) {
  EXPECT_EQ(16, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetScaleFactorForHWND) {
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetLeftFakeHwnd()));
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetRightFakeHwnd()));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 960, 600), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 960, 550), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(960, 0, 400, 300), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(960, 0, 400, 300), displays[1].work_area());
}

TEST_P(ScreenWinTestTwoDisplays2x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  gfx::NativeWindow left_window = GetNativeWindowFromHWND(GetLeftFakeHwnd());
  EXPECT_EQ(left_display, screen->GetDisplayNearestWindow(left_window));

  gfx::NativeWindow right_window = GetNativeWindowFromHWND(GetRightFakeHwnd());
  EXPECT_EQ(right_display, screen->GetDisplayNearestWindow(right_window));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(125, 476)));
  EXPECT_EQ(left_display,
            screen->GetDisplayNearestPoint(gfx::Point(959, 599)));

  EXPECT_EQ(right_display, screen->GetDisplayNearestPoint(gfx::Point(960, 0)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(1000, 200)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(1359, 299)));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(859, 499, 100, 100)));

  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(960, 0, 100, 100)));
  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(1259, 199, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

TEST_P(ScreenWinTestTwoDisplays2x, CheckIdStability) {
  // Callers may use the display ID as a way to persist data like window
  // coordinates across runs. As a result, the IDs must remain stable.
  Screen* screen = GetScreen();
  ASSERT_EQ(2, screen->GetNumDisplays());
  EXPECT_EQ(711638480, screen->GetAllDisplays()[0].id());
  EXPECT_EQ(1158792510, screen->GetAllDisplays()[1].id());
}

namespace {

// Five 1x displays laid out as follows (not to scale):
// +---------+----------------+
// |         |                |
// |    0    |                |
// |         |       1        |
// +---------+                |
// |    2    |                |
// |         +----------------+-----+
// +---------+                |     |
//                            |  3  |
//                            |     |
//                            +--+--+
//                               |4 |
//                               +--+
class ScreenWinTestManyDisplays1x : public ScreenWinTest {
 public:
  ScreenWinTestManyDisplays1x() = default;

  ScreenWinTestManyDisplays1x(const ScreenWinTestManyDisplays1x&) = delete;
  ScreenWinTestManyDisplays1x& operator=(const ScreenWinTestManyDisplays1x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 640, 480),
                            gfx::Rect(0, 0, 640, 380),
                            L"primary0",
                            1.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(0, 0, 640, 380)));
    initializer->AddMonitor(gfx::Rect(640, 0, 1024, 768),
                            gfx::Rect(640, 0, 1024, 768),
                            L"monitor1",
                            1.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(640, 0, 1024, 768)));
    initializer->AddMonitor(gfx::Rect(0, 480, 640, 300),
                            gfx::Rect(0, 480, 640, 300),
                            L"monitor2",
                            1.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(0, 480, 640, 300)));
    initializer->AddMonitor(gfx::Rect(1664, 768, 400, 400),
                            gfx::Rect(1664, 768, 400, 400),
                            L"monitor3",
                            1.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(1664, 768, 400, 400)));
    initializer->AddMonitor(gfx::Rect(1864, 1168, 200, 200),
                            gfx::Rect(1864, 1168, 200, 200),
                            L"monitor4",
                            1.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(1864, 1168, 200, 200)));
  }

  // Returns the hwnd corresponding to the monitor index.
  HWND GetFakeHwnd(size_t monitor_index) {
    return fake_hwnds_[monitor_index];
  }

 private:
  std::vector<HWND> fake_hwnds_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestManyDisplays1x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestManyDisplays1x, ScreenToDIPPoints) {
  gfx::PointF primary_origin(0, 0);
  gfx::PointF primary_middle(250, 252);
  gfx::PointF primary_lower_right(639, 479);
  EXPECT_EQ(primary_origin, GetScreenWin()->ScreenToDIPPoint(primary_origin));
  EXPECT_EQ(primary_middle, GetScreenWin()->ScreenToDIPPoint(primary_middle));
  EXPECT_EQ(primary_lower_right,
            GetScreenWin()->ScreenToDIPPoint(primary_lower_right));

  gfx::PointF monitor1_origin(640, 0);
  gfx::PointF monitor1_middle(852, 357);
  gfx::PointF monitor1_lower_right(1663, 759);
  EXPECT_EQ(monitor1_origin, GetScreenWin()->ScreenToDIPPoint(monitor1_origin));
  EXPECT_EQ(monitor1_middle, GetScreenWin()->ScreenToDIPPoint(monitor1_middle));
  EXPECT_EQ(monitor1_lower_right,
            GetScreenWin()->ScreenToDIPPoint(monitor1_lower_right));

  gfx::PointF monitor2_origin(0, 480);
  gfx::PointF monitor2_middle(321, 700);
  gfx::PointF monitor2_lower_right(639, 779);
  EXPECT_EQ(monitor2_origin, GetScreenWin()->ScreenToDIPPoint(monitor2_origin));
  EXPECT_EQ(monitor2_middle, GetScreenWin()->ScreenToDIPPoint(monitor2_middle));
  EXPECT_EQ(monitor2_lower_right,
            GetScreenWin()->ScreenToDIPPoint(monitor2_lower_right));

  gfx::PointF monitor3_origin(1664, 768);
  gfx::PointF monitor3_middle(1823, 1000);
  gfx::PointF monitor3_lower_right(2063, 1167);
  EXPECT_EQ(monitor3_origin, GetScreenWin()->ScreenToDIPPoint(monitor3_origin));
  EXPECT_EQ(monitor3_middle, GetScreenWin()->ScreenToDIPPoint(monitor3_middle));
  EXPECT_EQ(monitor3_lower_right,
            GetScreenWin()->ScreenToDIPPoint(monitor3_lower_right));

  gfx::PointF monitor4_origin(1864, 1168);
  gfx::PointF monitor4_middle(1955, 1224);
  gfx::PointF monitor4_lower_right(2063, 1367);
  EXPECT_EQ(monitor4_origin, GetScreenWin()->ScreenToDIPPoint(monitor4_origin));
  EXPECT_EQ(monitor4_middle, GetScreenWin()->ScreenToDIPPoint(monitor4_middle));
  EXPECT_EQ(monitor4_lower_right,
            GetScreenWin()->ScreenToDIPPoint(monitor4_lower_right));
}

TEST_P(ScreenWinTestManyDisplays1x, DIPToScreenPoints) {
  gfx::Point primary_origin(0, 0);
  gfx::Point primary_middle(250, 252);
  gfx::Point primary_lower_right(639, 479);
  EXPECT_EQ(primary_origin, GetScreenWin()->DIPToScreenPoint(primary_origin));
  EXPECT_EQ(primary_middle, GetScreenWin()->DIPToScreenPoint(primary_middle));
  EXPECT_EQ(primary_lower_right,
            GetScreenWin()->DIPToScreenPoint(primary_lower_right));

  gfx::Point monitor1_origin(640, 0);
  gfx::Point monitor1_middle(852, 357);
  gfx::Point monitor1_lower_right(1663, 759);
  EXPECT_EQ(monitor1_origin, GetScreenWin()->DIPToScreenPoint(monitor1_origin));
  EXPECT_EQ(monitor1_middle, GetScreenWin()->DIPToScreenPoint(monitor1_middle));
  EXPECT_EQ(monitor1_lower_right,
            GetScreenWin()->DIPToScreenPoint(monitor1_lower_right));

  gfx::Point monitor2_origin(0, 480);
  gfx::Point monitor2_middle(321, 700);
  gfx::Point monitor2_lower_right(639, 779);
  EXPECT_EQ(monitor2_origin, GetScreenWin()->DIPToScreenPoint(monitor2_origin));
  EXPECT_EQ(monitor2_middle, GetScreenWin()->DIPToScreenPoint(monitor2_middle));
  EXPECT_EQ(monitor2_lower_right,
            GetScreenWin()->DIPToScreenPoint(monitor2_lower_right));

  gfx::Point monitor3_origin(1664, 768);
  gfx::Point monitor3_middle(1823, 1000);
  gfx::Point monitor3_lower_right(2063, 1167);
  EXPECT_EQ(monitor3_origin, GetScreenWin()->DIPToScreenPoint(monitor3_origin));
  EXPECT_EQ(monitor3_middle, GetScreenWin()->DIPToScreenPoint(monitor3_middle));
  EXPECT_EQ(monitor3_lower_right,
            GetScreenWin()->DIPToScreenPoint(monitor3_lower_right));

  gfx::Point monitor4_origin(1864, 1168);
  gfx::Point monitor4_middle(1955, 1224);
  gfx::Point monitor4_lower_right(2063, 1367);
  EXPECT_EQ(monitor4_origin, GetScreenWin()->DIPToScreenPoint(monitor4_origin));
  EXPECT_EQ(monitor4_middle, GetScreenWin()->DIPToScreenPoint(monitor4_middle));
  EXPECT_EQ(monitor4_lower_right,
            GetScreenWin()->DIPToScreenPoint(monitor4_lower_right));
}

TEST_P(ScreenWinTestManyDisplays1x, ClientToDIPPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(250, 194);
  gfx::Point lower_right(299, 299);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(origin, GetScreenWin()->ClientToDIPPoint(GetFakeHwnd(i), origin));
    EXPECT_EQ(middle, GetScreenWin()->ClientToDIPPoint(GetFakeHwnd(i), middle));
    EXPECT_EQ(lower_right,
              GetScreenWin()->ClientToDIPPoint(GetFakeHwnd(i), lower_right));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, DIPToClientPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(250, 194);
  gfx::Point lower_right(299, 299);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(origin, GetScreenWin()->DIPToClientPoint(GetFakeHwnd(i), origin));
    EXPECT_EQ(middle, GetScreenWin()->DIPToClientPoint(GetFakeHwnd(i), middle));
    EXPECT_EQ(lower_right,
              GetScreenWin()->DIPToClientPoint(GetFakeHwnd(i), lower_right));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, ScreenToDIPRects) {
  gfx::Rect primary_origin(0, 0, 50, 100);
  gfx::Rect primary_middle(250, 252, 40, 50);
  EXPECT_EQ(primary_origin,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(0), primary_origin));
  EXPECT_EQ(primary_middle,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(0), primary_middle));

  gfx::Rect monitor1_origin(640, 0, 25, 43);
  gfx::Rect monitor1_middle(852, 357, 37, 45);
  EXPECT_EQ(monitor1_origin,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(1), monitor1_origin));
  EXPECT_EQ(monitor1_middle,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(1), monitor1_middle));

  gfx::Rect monitor2_origin(0, 480, 42, 40);
  gfx::Rect monitor2_middle(321, 700, 103, 203);
  EXPECT_EQ(monitor2_origin,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(2), monitor2_origin));
  EXPECT_EQ(monitor2_middle,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(2), monitor2_middle));

  gfx::Rect monitor3_origin(1664, 768, 24, 102);
  gfx::Rect monitor3_middle(1823, 1000, 35, 35);
  EXPECT_EQ(monitor3_origin,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(3), monitor3_origin));
  EXPECT_EQ(monitor3_middle,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(3), monitor3_middle));

  gfx::Rect monitor4_origin(1864, 1168, 15, 20);
  gfx::Rect monitor4_middle(1955, 1224, 25, 30);
  EXPECT_EQ(monitor4_origin,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(4), monitor4_origin));
  EXPECT_EQ(monitor4_middle,
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(4), monitor4_middle));
}

TEST_P(ScreenWinTestManyDisplays1x, DIPToScreenRects) {
  gfx::Rect primary_origin(0, 0, 50, 100);
  gfx::Rect primary_middle(250, 252, 40, 50);
  EXPECT_EQ(primary_origin,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(0), primary_origin));
  EXPECT_EQ(primary_middle,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(0), primary_middle));

  gfx::Rect monitor1_origin(640, 0, 25, 43);
  gfx::Rect monitor1_middle(852, 357, 37, 45);
  EXPECT_EQ(monitor1_origin,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(1), monitor1_origin));
  EXPECT_EQ(monitor1_middle,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(1), monitor1_middle));

  gfx::Rect monitor2_origin(0, 480, 42, 40);
  gfx::Rect monitor2_middle(321, 700, 103, 203);
  EXPECT_EQ(monitor2_origin,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(2), monitor2_origin));
  EXPECT_EQ(monitor2_middle,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(2), monitor2_middle));

  gfx::Rect monitor3_origin(1664, 768, 24, 102);
  gfx::Rect monitor3_middle(1823, 1000, 35, 35);
  EXPECT_EQ(monitor3_origin,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(3), monitor3_origin));
  EXPECT_EQ(monitor3_middle,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(3), monitor3_middle));

  gfx::Rect monitor4_origin(1864, 1168, 15, 20);
  gfx::Rect monitor4_middle(1955, 1224, 25, 30);
  EXPECT_EQ(monitor4_origin,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(4), monitor4_origin));
  EXPECT_EQ(monitor4_middle,
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(4), monitor4_middle));
}

TEST_P(ScreenWinTestManyDisplays1x, DIPToScreenRectNullHWND) {
  gfx::Rect primary_origin(0, 0, 50, 100);
  gfx::Rect primary_middle(250, 252, 40, 50);
  EXPECT_EQ(primary_origin,
            GetScreenWin()->DIPToScreenRect(nullptr, primary_origin));
  EXPECT_EQ(primary_middle,
            GetScreenWin()->DIPToScreenRect(nullptr, primary_middle));

  gfx::Rect monitor1_origin(640, 0, 25, 43);
  gfx::Rect monitor1_middle(852, 357, 37, 45);
  EXPECT_EQ(monitor1_origin,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor1_origin));
  EXPECT_EQ(monitor1_middle,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor1_middle));

  gfx::Rect monitor2_origin(0, 480, 42, 40);
  gfx::Rect monitor2_middle(321, 700, 103, 203);
  EXPECT_EQ(monitor2_origin,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor2_origin));
  EXPECT_EQ(monitor2_middle,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor2_middle));

  gfx::Rect monitor3_origin(1664, 768, 24, 102);
  gfx::Rect monitor3_middle(1823, 1000, 35, 35);
  EXPECT_EQ(monitor3_origin,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor3_origin));
  EXPECT_EQ(monitor3_middle,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor3_middle));

  gfx::Rect monitor4_origin(1864, 1168, 15, 20);
  gfx::Rect monitor4_middle(1955, 1224, 25, 30);
  EXPECT_EQ(monitor4_origin,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor4_origin));
  EXPECT_EQ(monitor4_middle,
            GetScreenWin()->DIPToScreenRect(nullptr, monitor4_middle));
}

TEST_P(ScreenWinTestManyDisplays1x, ClientToDIPRects) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(origin, GetScreenWin()->ClientToDIPRect(GetFakeHwnd(i), origin));
    EXPECT_EQ(middle, GetScreenWin()->ClientToDIPRect(GetFakeHwnd(i), middle));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, DIPToClientRects) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(origin, GetScreenWin()->DIPToClientRect(GetFakeHwnd(i), origin));
    EXPECT_EQ(middle, GetScreenWin()->DIPToClientRect(GetFakeHwnd(i), middle));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, ScreenToDIPSize) {
  gfx::Size size(42, 131);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(size, GetScreenWin()->ScreenToDIPSize(GetFakeHwnd(i), size));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, DIPToScreenSize) {
  gfx::Size size(42, 131);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(size, GetScreenWin()->DIPToScreenSize(GetFakeHwnd(i), size));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, GetSystemMetricsInDIP) {
  EXPECT_EQ(31, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(42, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestManyDisplays1x, GetScaleFactorForHWND) {
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetFakeHwnd(i)));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 640, 480), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 640, 380), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(640, 0, 1024, 768), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(640, 0, 1024, 768), displays[1].work_area());
  EXPECT_EQ(gfx::Rect(0, 480, 640, 300), displays[2].bounds());
  EXPECT_EQ(gfx::Rect(0, 480, 640, 300), displays[2].work_area());
  EXPECT_EQ(gfx::Rect(1664, 768, 400, 400), displays[3].bounds());
  EXPECT_EQ(gfx::Rect(1664, 768, 400, 400), displays[3].work_area());
  EXPECT_EQ(gfx::Rect(1864, 1168, 200, 200), displays[4].bounds());
  EXPECT_EQ(gfx::Rect(1864, 1168, 200, 200), displays[4].work_area());
}

TEST_P(ScreenWinTestManyDisplays1x, GetNumDisplays) {
  EXPECT_EQ(5, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestManyDisplays1x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestManyDisplays1x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  std::vector<Display> displays = screen->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(displays[i],
              screen->GetDisplayNearestWindow(
                  GetNativeWindowFromHWND(GetFakeHwnd(i))));
  }
}

TEST_P(ScreenWinTestManyDisplays1x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  std::vector<Display> displays = screen->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  EXPECT_EQ(displays[0], screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(displays[0], screen->GetDisplayNearestPoint(gfx::Point(250, 252)));
  EXPECT_EQ(displays[0], screen->GetDisplayNearestPoint(gfx::Point(639, 479)));
  EXPECT_EQ(displays[1], screen->GetDisplayNearestPoint(gfx::Point(640, 0)));
  EXPECT_EQ(displays[1], screen->GetDisplayNearestPoint(gfx::Point(852, 357)));
  EXPECT_EQ(displays[1], screen->GetDisplayNearestPoint(gfx::Point(1663, 759)));
  EXPECT_EQ(displays[2], screen->GetDisplayNearestPoint(gfx::Point(0, 480)));
  EXPECT_EQ(displays[2], screen->GetDisplayNearestPoint(gfx::Point(321, 700)));
  EXPECT_EQ(displays[2], screen->GetDisplayNearestPoint(gfx::Point(639, 779)));
  EXPECT_EQ(displays[3], screen->GetDisplayNearestPoint(gfx::Point(1664, 768)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayNearestPoint(gfx::Point(1823, 1000)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayNearestPoint(gfx::Point(2063, 1167)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayNearestPoint(gfx::Point(1864, 1168)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayNearestPoint(gfx::Point(1955, 1224)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayNearestPoint(gfx::Point(2063, 1367)));
}

TEST_P(ScreenWinTestManyDisplays1x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  std::vector<Display> displays = screen->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  EXPECT_EQ(displays[0], screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(displays[0],
            screen->GetDisplayMatching(gfx::Rect(539, 379, 100, 100)));
  EXPECT_EQ(displays[1],
            screen->GetDisplayMatching(gfx::Rect(640, 0, 100, 100)));
  EXPECT_EQ(displays[1],
            screen->GetDisplayMatching(gfx::Rect(1563, 659, 100, 100)));
  EXPECT_EQ(displays[2],
            screen->GetDisplayMatching(gfx::Rect(0, 480, 100, 100)));
  EXPECT_EQ(displays[2],
            screen->GetDisplayMatching(gfx::Rect(539, 679, 100, 100)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayMatching(gfx::Rect(1664, 768, 100, 100)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayMatching(gfx::Rect(1963, 1067, 100, 100)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayMatching(gfx::Rect(1864, 1168, 100, 100)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayMatching(gfx::Rect(1963, 1267, 100, 100)));
}

TEST_P(ScreenWinTestManyDisplays1x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Five 2x displays laid out as follows (not to scale):
// +---------+----------------+
// |         |                |
// |    0    |                |
// |         |       1        |
// +---------+                |
// |    2    |                |
// |         +----------------+-----+
// +---------+                |     |
//                            |  3  |
//                            |     |
//                            +--+--+
//                               |4 |
//                               +--+
class ScreenWinTestManyDisplays2x : public ScreenWinTest {
 public:
  ScreenWinTestManyDisplays2x() = default;

  ScreenWinTestManyDisplays2x(const ScreenWinTestManyDisplays2x&) = delete;
  ScreenWinTestManyDisplays2x& operator=(const ScreenWinTestManyDisplays2x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 640, 480),
                            gfx::Rect(0, 0, 640, 380),
                            L"primary0",
                            2.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(0, 0, 640, 380)));
    initializer->AddMonitor(gfx::Rect(640, 0, 1024, 768),
                            gfx::Rect(640, 0, 1024, 768),
                            L"monitor1",
                            2.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(640, 0, 1024, 768)));
    initializer->AddMonitor(gfx::Rect(0, 480, 640, 300),
                            gfx::Rect(0, 480, 640, 300),
                            L"monitor2",
                            2.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(0, 480, 640, 300)));
    initializer->AddMonitor(gfx::Rect(1664, 768, 400, 400),
                            gfx::Rect(1664, 768, 400, 400),
                            L"monitor3",
                            2.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(1664, 768, 400, 400)));
    initializer->AddMonitor(gfx::Rect(1864, 1168, 200, 200),
                            gfx::Rect(1864, 1168, 200, 200),
                            L"monitor4",
                            2.0);
    fake_hwnds_.push_back(
        initializer->CreateFakeHwnd(gfx::Rect(1864, 1168, 200, 200)));
  }

  // Returns the hwnd corresponding to the monitor index.
  HWND GetFakeHwnd(size_t monitor_index) {
    return fake_hwnds_[monitor_index];
  }

 private:
  std::vector<HWND> fake_hwnds_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestManyDisplays2x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestManyDisplays2x, ScreenToDIPPoints) {
  // Primary Monitor Points
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(125, 126),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(250, 252)));
  expect_point_f_eq(gfx::PointF(319.5, 239.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(639, 479)));

  // Monitor 1
  expect_point_f_eq(gfx::PointF(320, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(640, 0)));
  expect_point_f_eq(gfx::PointF(426, 178.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(852, 357)));
  expect_point_f_eq(gfx::PointF(831.5, 379.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1663, 759)));

  // Monitor 2
  expect_point_f_eq(gfx::PointF(0, 240),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 480)));
  expect_point_f_eq(gfx::PointF(160.5, 350),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(321, 700)));
  expect_point_f_eq(gfx::PointF(319.5, 389.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(639, 779)));

  // Monitor 3
  expect_point_f_eq(gfx::PointF(832, 384),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1664, 768)));
  expect_point_f_eq(gfx::PointF(911.5, 500),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1823, 1000)));
  expect_point_f_eq(gfx::PointF(1031.5, 583.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2063, 1167)));

  // Monitor 4
  expect_point_f_eq(gfx::PointF(932, 584),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1864, 1168)));
  expect_point_f_eq(gfx::PointF(977.5, 612),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1955, 1224)));
  expect_point_f_eq(gfx::PointF(1031.5, 683.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2063, 1367)));
}

TEST_P(ScreenWinTestManyDisplays2x, DIPToScreenPoints) {
  // Primary Monitor Points
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(250, 252),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(125, 126)));
  EXPECT_EQ(gfx::Point(638, 478),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(319, 239)));

  // Monitor 1
  EXPECT_EQ(gfx::Point(640, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(320, 0)));
  EXPECT_EQ(gfx::Point(852, 356),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(426, 178)));
  EXPECT_EQ(gfx::Point(1662, 758),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(831, 379)));

  // Monitor 2
  EXPECT_EQ(gfx::Point(0, 480),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 240)));
  EXPECT_EQ(gfx::Point(320, 700),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(160, 350)));
  EXPECT_EQ(gfx::Point(638, 778),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(319, 389)));

  // Monitor 3
  EXPECT_EQ(gfx::Point(1664, 768),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(832, 384)));
  EXPECT_EQ(gfx::Point(1822, 1000),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(911, 500)));
  EXPECT_EQ(gfx::Point(2062, 1166),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1031, 583)));

  // Monitor 4
  EXPECT_EQ(gfx::Point(1864, 1168),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(932, 584)));
  EXPECT_EQ(gfx::Point(1954, 1224),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(977, 612)));
  EXPECT_EQ(gfx::Point(2062, 1366),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1031, 683)));
}

TEST_P(ScreenWinTestManyDisplays2x, ClientToDIPPoints) {
  gfx::Point client_origin(0, 0);
  gfx::Point client_middle(250, 194);
  gfx::Point client_lower_right(299, 299);
  gfx::Point dip_origin(0, 0);
  gfx::Point dip_middle(125, 97);
  gfx::Point dip_lower_right(149, 149);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(dip_origin,
              GetScreenWin()->ClientToDIPPoint(GetFakeHwnd(i), client_origin));
    EXPECT_EQ(dip_middle,
              GetScreenWin()->ClientToDIPPoint(GetFakeHwnd(i), client_middle));
    EXPECT_EQ(dip_lower_right, GetScreenWin()->ClientToDIPPoint(
                                   GetFakeHwnd(i), client_lower_right));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, DIPToClientPoints) {
  gfx::Point dip_origin(0, 0);
  gfx::Point dip_middle(125, 97);
  gfx::Point dip_lower_right(149, 149);
  gfx::Point client_origin(0, 0);
  gfx::Point client_middle(250, 194);
  gfx::Point client_lower_right(298, 298);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(client_origin,
              GetScreenWin()->DIPToClientPoint(GetFakeHwnd(i), dip_origin));
    EXPECT_EQ(client_middle,
              GetScreenWin()->DIPToClientPoint(GetFakeHwnd(i), dip_middle));
    EXPECT_EQ(client_lower_right, GetScreenWin()->DIPToClientPoint(
                                      GetFakeHwnd(i), dip_lower_right));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, ScreenToDIPRects) {
  // Primary Monitor
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(0),
                                            gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(gfx::Rect(125, 126, 20, 25),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(0),
                                            gfx::Rect(250, 252, 40, 50)));

  // Monitor 1
  EXPECT_EQ(gfx::Rect(320, 0, 13, 22),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(1),
                                            gfx::Rect(640, 0, 25, 43)));
  EXPECT_EQ(gfx::Rect(426, 178, 19, 23),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(1),
                                            gfx::Rect(852, 357, 37, 45)));

  // Monitor 2
  EXPECT_EQ(gfx::Rect(0, 240, 21, 20),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(2),
                                            gfx::Rect(0, 480, 42, 40)));
  EXPECT_EQ(gfx::Rect(160, 350, 52, 102),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(2),
                                            gfx::Rect(321, 700, 103, 203)));

  // Monitor 3
  EXPECT_EQ(gfx::Rect(832, 384, 12, 51),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(3),
                                            gfx::Rect(1664, 768, 24, 102)));
  EXPECT_EQ(gfx::Rect(911, 500, 18, 18),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(3),
                                            gfx::Rect(1823, 1000, 35, 35)));

  // Monitor 4
  EXPECT_EQ(gfx::Rect(932, 584, 8, 10),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(4),
                                            gfx::Rect(1864, 1168, 15, 20)));
  EXPECT_EQ(gfx::Rect(977, 612, 13, 15),
            GetScreenWin()->ScreenToDIPRect(GetFakeHwnd(4),
                                            gfx::Rect(1955, 1224, 25, 30)));
}

TEST_P(ScreenWinTestManyDisplays2x, DIPToScreenRects) {
  // Primary Monitor
  EXPECT_EQ(
      gfx::Rect(0, 0, 50, 100),
      GetScreenWin()->DIPToScreenRect(GetFakeHwnd(0), gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(gfx::Rect(250, 252, 40, 50),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(0),
                                            gfx::Rect(125, 126, 20, 25)));

  // Monitor 1
  EXPECT_EQ(gfx::Rect(640, 0, 26, 44),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(1),
                                            gfx::Rect(320, 0, 13, 22)));
  EXPECT_EQ(gfx::Rect(852, 356, 38, 46),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(1),
                                            gfx::Rect(426, 178, 19, 23)));

  // Monitor 2
  EXPECT_EQ(gfx::Rect(0, 480, 42, 40),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(2),
                                            gfx::Rect(0, 240, 21, 20)));
  EXPECT_EQ(gfx::Rect(320, 700, 104, 204),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(2),
                                            gfx::Rect(160, 350, 52, 102)));

  // Monitor 3
  EXPECT_EQ(gfx::Rect(1664, 768, 24, 102),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(3),
                                            gfx::Rect(832, 384, 12, 51)));
  EXPECT_EQ(gfx::Rect(1822, 1000, 36, 36),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(3),
                                            gfx::Rect(911, 500, 18, 18)));

  // Monitor 4
  EXPECT_EQ(gfx::Rect(1864, 1168, 16, 20),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(4),
                                            gfx::Rect(932, 584, 8, 10)));
  EXPECT_EQ(gfx::Rect(1954, 1224, 26, 30),
            GetScreenWin()->DIPToScreenRect(GetFakeHwnd(4),
                                            gfx::Rect(977, 612, 13, 15)));
}

TEST_P(ScreenWinTestManyDisplays2x, DIPToScreenRectNullHWND) {
  // Primary Monitor
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(250, 252, 40, 50),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(125, 126, 20, 25)));

  // Monitor 1
  EXPECT_EQ(gfx::Rect(640, 0, 26, 44), GetScreenWin()->DIPToScreenRect(
                                           nullptr, gfx::Rect(320, 0, 13, 22)));
  EXPECT_EQ(
      gfx::Rect(852, 356, 38, 46),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(426, 178, 19, 23)));

  // Monitor 2
  EXPECT_EQ(gfx::Rect(0, 480, 42, 40), GetScreenWin()->DIPToScreenRect(
                                           nullptr, gfx::Rect(0, 240, 21, 20)));
  EXPECT_EQ(
      gfx::Rect(320, 700, 104, 204),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(160, 350, 52, 102)));

  // Monitor 3
  EXPECT_EQ(
      gfx::Rect(1664, 768, 24, 102),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(832, 384, 12, 51)));
  EXPECT_EQ(
      gfx::Rect(1822, 1000, 36, 36),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(911, 500, 18, 18)));

  // Monitor 4
  EXPECT_EQ(
      gfx::Rect(1864, 1168, 16, 20),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(932, 584, 8, 10)));
  EXPECT_EQ(
      gfx::Rect(1954, 1224, 26, 30),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(977, 612, 13, 15)));
}

TEST_P(ScreenWinTestManyDisplays2x, ClientToDIPRects) {
  gfx::Rect client_screen_origin(0, 0, 50, 100);
  gfx::Rect client_dip_origin(0, 0, 25, 50);
  gfx::Rect client_screen_middle(253, 495, 41, 52);
  gfx::Rect client_dip_middle(126, 247, 21, 27);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(client_dip_origin, GetScreenWin()->ClientToDIPRect(
                                     GetFakeHwnd(i), client_screen_origin));
    EXPECT_EQ(client_dip_middle, GetScreenWin()->ClientToDIPRect(
                                     GetFakeHwnd(i), client_screen_middle));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, DIPToClientRects) {
  gfx::Rect client_dip_origin(0, 0, 25, 50);
  gfx::Rect client_screen_origin(0, 0, 50, 100);
  gfx::Rect client_dip_middle(126, 247, 21, 26);
  gfx::Rect client_screen_middle(252, 494, 42, 52);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(client_screen_origin, GetScreenWin()->DIPToClientRect(
                                        GetFakeHwnd(i), client_dip_origin));
    EXPECT_EQ(client_screen_middle, GetScreenWin()->DIPToClientRect(
                                        GetFakeHwnd(i), client_dip_middle));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, ScreenToDIPSize) {
  gfx::Size screen_size(42, 131);
  gfx::Size dip_size(21, 66);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(dip_size,
              GetScreenWin()->ScreenToDIPSize(GetFakeHwnd(i), screen_size));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, DIPToScreenSize) {
  gfx::Size dip_size(21, 66);
  gfx::Size screen_size(42, 132);
  ASSERT_EQ(5, GetScreen()->GetNumDisplays());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(screen_size,
              GetScreenWin()->DIPToScreenSize(GetFakeHwnd(i), dip_size));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, GetSystemMetricsInDIP) {
  EXPECT_EQ(16, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestManyDisplays2x, GetScaleFactorForHWND) {
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetFakeHwnd(i)));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 320, 240), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 320, 190), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(320, 0, 512, 384), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(320, 0, 512, 384), displays[1].work_area());
  EXPECT_EQ(gfx::Rect(0, 240, 320, 150), displays[2].bounds());
  EXPECT_EQ(gfx::Rect(0, 240, 320, 150), displays[2].work_area());
  EXPECT_EQ(gfx::Rect(832, 384, 200, 200), displays[3].bounds());
  EXPECT_EQ(gfx::Rect(832, 384, 200, 200), displays[3].work_area());
  EXPECT_EQ(gfx::Rect(932, 584, 100, 100), displays[4].bounds());
  EXPECT_EQ(gfx::Rect(932, 584, 100, 100), displays[4].work_area());
}

TEST_P(ScreenWinTestManyDisplays2x, GetNumDisplays) {
  EXPECT_EQ(5, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestManyDisplays2x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestManyDisplays2x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  std::vector<Display> displays = screen->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  for (size_t i = 0; i < 5u; ++i) {
    SCOPED_TRACE(base::StringPrintf("i=%zu", i));
    EXPECT_EQ(displays[i],
              screen->GetDisplayNearestWindow(
                  GetNativeWindowFromHWND(GetFakeHwnd(i))));
  }
}

TEST_P(ScreenWinTestManyDisplays2x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  std::vector<Display> displays = screen->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  EXPECT_EQ(displays[0], screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(displays[0], screen->GetDisplayNearestPoint(gfx::Point(125, 126)));
  EXPECT_EQ(displays[0], screen->GetDisplayNearestPoint(gfx::Point(319, 239)));
  EXPECT_EQ(displays[1], screen->GetDisplayNearestPoint(gfx::Point(320, 0)));
  EXPECT_EQ(displays[1], screen->GetDisplayNearestPoint(gfx::Point(446, 178)));
  EXPECT_EQ(displays[1], screen->GetDisplayNearestPoint(gfx::Point(831, 379)));
  EXPECT_EQ(displays[2], screen->GetDisplayNearestPoint(gfx::Point(0, 240)));
  EXPECT_EQ(displays[2], screen->GetDisplayNearestPoint(gfx::Point(160, 350)));
  EXPECT_EQ(displays[2], screen->GetDisplayNearestPoint(gfx::Point(319, 389)));
  EXPECT_EQ(displays[3], screen->GetDisplayNearestPoint(gfx::Point(832, 384)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayNearestPoint(gfx::Point(911, 500)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayNearestPoint(gfx::Point(1031, 583)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayNearestPoint(gfx::Point(932, 584)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayNearestPoint(gfx::Point(977, 612)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayNearestPoint(gfx::Point(1031, 683)));
}

TEST_P(ScreenWinTestManyDisplays2x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  std::vector<Display> displays = screen->GetAllDisplays();
  ASSERT_EQ(5u, displays.size());
  EXPECT_EQ(displays[0], screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(displays[0],
            screen->GetDisplayMatching(gfx::Rect(219, 139, 100, 100)));
  EXPECT_EQ(displays[1],
            screen->GetDisplayMatching(gfx::Rect(320, 0, 100, 100)));
  EXPECT_EQ(displays[1],
            screen->GetDisplayMatching(gfx::Rect(731, 279, 100, 100)));
  EXPECT_EQ(displays[2],
            screen->GetDisplayMatching(gfx::Rect(0, 240, 100, 100)));
  EXPECT_EQ(displays[2],
            screen->GetDisplayMatching(gfx::Rect(219, 289, 100, 100)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayMatching(gfx::Rect(832, 384, 100, 100)));
  EXPECT_EQ(displays[3],
            screen->GetDisplayMatching(gfx::Rect(931, 483, 100, 100)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayMatching(gfx::Rect(932, 584, 100, 100)));
  EXPECT_EQ(displays[4],
            screen->GetDisplayMatching(gfx::Rect(931, 583, 100, 100)));
}

TEST_P(ScreenWinTestManyDisplays2x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Two Displays of 1.0 (Left) and 2.0 (Right) Device Scale Factor.
class ScreenWinTestTwoDisplays1x2x : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplays1x2x() = default;

  ScreenWinTestTwoDisplays1x2x(const ScreenWinTestTwoDisplays1x2x&) = delete;
  ScreenWinTestTwoDisplays1x2x& operator=(const ScreenWinTestTwoDisplays1x2x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            1.0);
    initializer->AddMonitor(gfx::Rect(1920, 0, 800, 600),
                            gfx::Rect(1920, 0, 800, 600),
                            L"secondary",
                            2.0);
    fake_hwnd_left_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
    fake_hwnd_right_ =
        initializer->CreateFakeHwnd(gfx::Rect(1920, 0, 800, 600));
  }

  HWND GetLeftFakeHwnd() {
    return fake_hwnd_left_;
  }

  HWND GetRightFakeHwnd() {
    return fake_hwnd_right_;
  }

 private:
  HWND fake_hwnd_left_ = nullptr;
  HWND fake_hwnd_right_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplays1x2x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplays1x2x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(365, 694),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(1919, 1199),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));

  expect_point_f_eq(gfx::PointF(1920, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1920, 0)));
  expect_point_f_eq(gfx::PointF(2152, 175.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2384, 351)));
  expect_point_f_eq(gfx::PointF(2319.5, 299.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2719, 599)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1919, 1199)));

  EXPECT_EQ(gfx::Point(1920, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1920, 0)));
  EXPECT_EQ(gfx::Point(2384, 350),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(2152, 175)));
  EXPECT_EQ(gfx::Point(2718, 598),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(2319, 299)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, ClientToDIPPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199), GetScreenWin()->ClientToDIPPoint(
                                        left_hwnd, gfx::Point(1919, 1199)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599), GetScreenWin()->ClientToDIPPoint(
                                      right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, DIPToClientPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199), GetScreenWin()->DIPToClientPoint(
                                        left_hwnd, gfx::Point(1919, 1199)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(959, 599)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, ScreenToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->ScreenToDIPRect(
                                          left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(253, 496, 41, 52),
      GetScreenWin()->ScreenToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(1920, 0, 100, 150),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1920, 0, 200, 300)));
  EXPECT_EQ(gfx::Rect(1960, 248, 50, 100),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(2000, 496, 100, 200)));

  EXPECT_EQ(gfx::Rect(1910, 100, 50, 50),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1900, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, DIPToScreenRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToScreenRect(
                                          left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(left_hwnd, gfx::Rect(252, 496, 42, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(1920, 0, 200, 300),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(1920, 0, 100, 150)));
  EXPECT_EQ(gfx::Rect(2000, 496, 100, 200),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(1960, 248, 50, 100)));

  EXPECT_EQ(gfx::Rect(1900, 200, 100, 100),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(1910, 100, 50, 50)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(252, 496, 42, 52)));

  EXPECT_EQ(
      gfx::Rect(1920, 0, 200, 300),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(1920, 0, 100, 150)));
  EXPECT_EQ(
      gfx::Rect(2000, 496, 100, 200),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(1960, 248, 50, 100)));

  EXPECT_EQ(
      gfx::Rect(1900, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(1910, 100, 50, 50)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, ClientToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->ClientToDIPRect(
                                          left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(253, 496, 41, 52),
      GetScreenWin()->ClientToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ClientToDIPRect(
                                         right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, DIPToClientRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToClientRect(
                                          left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(left_hwnd, gfx::Rect(252, 496, 42, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToClientRect(
                                          right_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(right_hwnd, gfx::Rect(126, 248, 21, 26)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, ScreenToDIPSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 131),
            GetScreenWin()->ScreenToDIPSize(left_hwnd, gfx::Size(42, 131)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, DIPToScreenSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->DIPToScreenSize(left_hwnd, gfx::Size(21, 66)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(right_hwnd, gfx::Size(21, 66)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetSystemMetricsInDIP) {
  EXPECT_EQ(31, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(42, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetLeftFakeHwnd()));
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetRightFakeHwnd()));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1200), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1100), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(1920, 0, 400, 300), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(1920, 0, 400, 300), displays[1].work_area());
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetNumDisplays) {
  EXPECT_EQ(2, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  gfx::NativeWindow left_window = GetNativeWindowFromHWND(GetLeftFakeHwnd());
  EXPECT_EQ(left_display, screen->GetDisplayNearestWindow(left_window));

  gfx::NativeWindow right_window = GetNativeWindowFromHWND(GetRightFakeHwnd());
  EXPECT_EQ(right_display, screen->GetDisplayNearestWindow(right_window));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(250, 952)));
  EXPECT_EQ(left_display,
            screen->GetDisplayNearestPoint(gfx::Point(1919, 1199)));

  EXPECT_EQ(right_display, screen->GetDisplayNearestPoint(gfx::Point(1920, 0)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(2000, 200)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(2319, 299)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(1819, 1099, 100, 100)));

  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(1920, 0, 100, 100)));
  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(2219, 199, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1x2x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Two Displays of 1.5 (Left) and 1.0 (Right) Device Scale Factor.
class ScreenWinTestTwoDisplays1_5x1x : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplays1_5x1x() = default;

  ScreenWinTestTwoDisplays1_5x1x(const ScreenWinTestTwoDisplays1_5x1x&) =
      delete;
  ScreenWinTestTwoDisplays1_5x1x& operator=(
      const ScreenWinTestTwoDisplays1_5x1x&) = delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 800, 600),
                            gfx::Rect(0, 0, 800, 550),
                            L"primary",
                            1.5);
    initializer->AddMonitor(gfx::Rect(800, 120, 640, 480),
                            gfx::Rect(800, 120, 640, 480),
                            L"secondary",
                            1.0);
    fake_hwnd_left_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 800, 550));
    fake_hwnd_right_ =
        initializer->CreateFakeHwnd(gfx::Rect(800, 120, 640, 480));
  }

  HWND GetLeftFakeHwnd() {
    return fake_hwnd_left_;
  }

  HWND GetRightFakeHwnd() {
    return fake_hwnd_right_;
  }

 private:
  HWND fake_hwnd_left_ = nullptr;
  HWND fake_hwnd_right_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplays1_5x1x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplays1_5x1x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(243.3333F, 301.3333F),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 452)));
  expect_point_f_eq(gfx::PointF(532.6666F, 399.3333F),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(799, 599)));

  expect_point_f_eq(gfx::PointF(534, -80),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(800, 120)));
  expect_point_f_eq(gfx::PointF(860, 151),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1126, 351)));
  expect_point_f_eq(gfx::PointF(1173, 399),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1439, 599)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 451),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(243, 301)));
  EXPECT_EQ(gfx::Point(798, 598),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(532, 399)));

  EXPECT_EQ(gfx::Point(800, 120),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(534, -80)));
  EXPECT_EQ(gfx::Point(1126, 351),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(860, 151)));
  EXPECT_EQ(gfx::Point(1439, 599),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1173, 399)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, ClientToDIPPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(243, 462),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1279, 799), GetScreenWin()->ClientToDIPPoint(
                                       left_hwnd, gfx::Point(1919, 1199)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199), GetScreenWin()->ClientToDIPPoint(
                                        right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, DIPToClientPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 693),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(243, 462)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(1279, 799)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199), GetScreenWin()->DIPToClientPoint(
                                        right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, ScreenToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 34, 67), GetScreenWin()->ScreenToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(168, 330, 28, 36),
      GetScreenWin()->ScreenToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(534, -80, 200, 300),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(800, 120, 200, 300)));
  EXPECT_EQ(gfx::Rect(987, 296, 100, 200),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1253, 496, 100, 200)));

  EXPECT_EQ(gfx::Rect(514, 0, 100, 100),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(780, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, DIPToScreenRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 51, 101), GetScreenWin()->DIPToScreenRect(
                                          left_hwnd, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(
      gfx::Rect(252, 495, 42, 54),
      GetScreenWin()->DIPToScreenRect(left_hwnd, gfx::Rect(168, 330, 28, 36)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(800, 120, 200, 300),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(534, -80, 200, 300)));
  EXPECT_EQ(gfx::Rect(1253, 496, 100, 200),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(987, 296, 100, 200)));

  EXPECT_EQ(
      gfx::Rect(780, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(right_hwnd, gfx::Rect(514, 0, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 51, 101),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(
      gfx::Rect(252, 495, 42, 54),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(168, 330, 28, 36)));

  EXPECT_EQ(
      gfx::Rect(800, 120, 200, 300),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(534, -80, 200, 300)));
  EXPECT_EQ(
      gfx::Rect(1253, 496, 100, 200),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(987, 296, 100, 200)));

  EXPECT_EQ(
      gfx::Rect(780, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(514, 0, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, ClientToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 34, 67), GetScreenWin()->ClientToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(168, 330, 28, 36),
      GetScreenWin()->ClientToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(
      gfx::Rect(0, 0, 50, 100),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(253, 496, 41, 52),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, DIPToClientRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 51, 101), GetScreenWin()->DIPToClientRect(
                                          left_hwnd, gfx::Rect(0, 0, 34, 67)));
  EXPECT_EQ(
      gfx::Rect(252, 495, 42, 54),
      GetScreenWin()->DIPToClientRect(left_hwnd, gfx::Rect(168, 330, 28, 36)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(
      gfx::Rect(0, 0, 50, 100),
      GetScreenWin()->DIPToClientRect(right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(253, 496, 41, 52),
      GetScreenWin()->DIPToClientRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, ScreenToDIPSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(28, 88),
            GetScreenWin()->ScreenToDIPSize(left_hwnd, gfx::Size(42, 131)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 131),
            GetScreenWin()->ScreenToDIPSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, DIPToScreenSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 131),
            GetScreenWin()->DIPToScreenSize(left_hwnd, gfx::Size(28, 87)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 131),
            GetScreenWin()->DIPToScreenSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetSystemMetricsInDIP) {
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(28, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.5, GetScreenWin()->GetScaleFactorForHWND(GetLeftFakeHwnd()));
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetRightFakeHwnd()));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 534, 400), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 534, 367), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(534, -80, 640, 480), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(534, -80, 640, 480), displays[1].work_area());
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  gfx::NativeWindow left_window = GetNativeWindowFromHWND(GetLeftFakeHwnd());
  EXPECT_EQ(left_display, screen->GetDisplayNearestWindow(left_window));

  gfx::NativeWindow right_window = GetNativeWindowFromHWND(GetRightFakeHwnd());
  EXPECT_EQ(right_display, screen->GetDisplayNearestWindow(right_window));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(125, 253)));
  EXPECT_EQ(left_display,
            screen->GetDisplayNearestPoint(gfx::Point(533, 399)));

  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(534, -80)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(1000, 200)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(1173, 399)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(433, 299, 100, 100)));

  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(534, -80, 100, 100)));
  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(1073, 299, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays1_5x1x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Two Displays of 2.0 (Left) and 1.0 (Right) Device Scale Factor.
class ScreenWinTestTwoDisplays2x1x : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplays2x1x() = default;

  ScreenWinTestTwoDisplays2x1x(const ScreenWinTestTwoDisplays2x1x&) = delete;
  ScreenWinTestTwoDisplays2x1x& operator=(const ScreenWinTestTwoDisplays2x1x&) =
      delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100),
                            L"primary",
                            2.0);
    initializer->AddMonitor(gfx::Rect(1920, 0, 800, 600),
                            gfx::Rect(1920, 0, 800, 600),
                            L"secondary",
                            1.0);
    fake_hwnd_left_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 1920, 1100));
    fake_hwnd_right_ =
        initializer->CreateFakeHwnd(gfx::Rect(1920, 0, 800, 600));
  }

  HWND GetLeftFakeHwnd() {
    return fake_hwnd_left_;
  }

  HWND GetRightFakeHwnd() {
    return fake_hwnd_right_;
  }

 private:
  HWND fake_hwnd_left_ = nullptr;
  HWND fake_hwnd_right_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplays2x1x,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplays2x1x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(182.5, 347),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(959.5, 599.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));

  expect_point_f_eq(gfx::PointF(960, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1920, 0)));
  expect_point_f_eq(gfx::PointF(1424, 351),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2384, 351)));
  expect_point_f_eq(gfx::PointF(1759, 599),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(2719, 599)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(959, 599)));

  EXPECT_EQ(gfx::Point(1920, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(960, 0)));
  EXPECT_EQ(gfx::Point(2384, 351),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1424, 351)));
  EXPECT_EQ(gfx::Point(2719, 599),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1759, 599)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, ClientToDIPPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599), GetScreenWin()->ClientToDIPPoint(
                                      left_hwnd, gfx::Point(1919, 1199)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199), GetScreenWin()->ClientToDIPPoint(
                                        right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, DIPToClientPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(959, 599)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(365, 694),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(1919, 1199), GetScreenWin()->DIPToClientPoint(
                                        right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, ScreenToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ScreenToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ScreenToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(960, 0, 200, 300),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1920, 0, 200, 300)));
  EXPECT_EQ(gfx::Rect(1040, 496, 100, 200),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(2000, 496, 100, 200)));

  EXPECT_EQ(gfx::Rect(940, 200, 100, 100),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(1900, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, DIPToScreenRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToScreenRect(
                                          left_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(left_hwnd, gfx::Rect(126, 248, 21, 26)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(
      gfx::Rect(1920, 0, 200, 300),
      GetScreenWin()->DIPToScreenRect(right_hwnd, gfx::Rect(960, 0, 200, 300)));
  EXPECT_EQ(gfx::Rect(2000, 496, 100, 200),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(1040, 496, 100, 200)));

  EXPECT_EQ(gfx::Rect(1900, 200, 100, 100),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(940, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(126, 248, 21, 26)));

  EXPECT_EQ(
      gfx::Rect(1920, 0, 200, 300),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(960, 0, 200, 300)));
  EXPECT_EQ(
      gfx::Rect(2000, 496, 100, 200),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(1040, 496, 100, 200)));

  EXPECT_EQ(
      gfx::Rect(1900, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(940, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, ClientToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ClientToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(
      gfx::Rect(0, 0, 50, 100),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(253, 496, 41, 52),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, DIPToClientRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToClientRect(
                                          left_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(left_hwnd, gfx::Rect(126, 248, 21, 26)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(
      gfx::Rect(0, 0, 50, 100),
      GetScreenWin()->DIPToClientRect(right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(253, 496, 41, 52),
      GetScreenWin()->DIPToClientRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, ScreenToDIPSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(left_hwnd, gfx::Size(42, 131)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 131),
            GetScreenWin()->ScreenToDIPSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, DIPToScreenSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(left_hwnd, gfx::Size(21, 66)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 131),
            GetScreenWin()->DIPToScreenSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetSystemMetricsInDIP) {
  EXPECT_EQ(16, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetScaleFactorForHWND) {
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetLeftFakeHwnd()));
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(GetRightFakeHwnd()));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 960, 600), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 960, 550), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(960, 0, 800, 600), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(960, 0, 800, 600), displays[1].work_area());
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetNumDisplays) {
  EXPECT_EQ(2, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  gfx::NativeWindow left_window = GetNativeWindowFromHWND(GetLeftFakeHwnd());
  EXPECT_EQ(left_display, screen->GetDisplayNearestWindow(left_window));

  gfx::NativeWindow right_window = GetNativeWindowFromHWND(GetRightFakeHwnd());
  EXPECT_EQ(right_display, screen->GetDisplayNearestWindow(right_window));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(250, 300)));
  EXPECT_EQ(left_display,
            screen->GetDisplayNearestPoint(gfx::Point(959, 599)));

  EXPECT_EQ(right_display, screen->GetDisplayNearestPoint(gfx::Point(960, 0)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(1500, 400)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(1659, 599)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetDisplayMatching) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(859, 499, 100, 100)));

  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(960, 0, 100, 100)));
  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(1559, 499, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x1x, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Two Displays of 2.0 (Left) and 1.0 (Right) Device Scale Factor under
// Windows DPI Virtualization. Note that the displays do not form a euclidean
// space.
class ScreenWinTestTwoDisplays2x1xVirtualized : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplays2x1xVirtualized() = default;

  ScreenWinTestTwoDisplays2x1xVirtualized(
      const ScreenWinTestTwoDisplays2x1xVirtualized&) = delete;
  ScreenWinTestTwoDisplays2x1xVirtualized& operator=(
      const ScreenWinTestTwoDisplays2x1xVirtualized&) = delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 3200, 1600),
                            gfx::Rect(0, 0, 3200, 1500),
                            L"primary",
                            2.0);
    initializer->AddMonitor(gfx::Rect(6400, 0, 3840, 2400),
                            gfx::Rect(6400, 0, 3840, 2400),
                            L"secondary",
                            2.0);
    fake_hwnd_left_ = initializer->CreateFakeHwnd(gfx::Rect(0, 0, 3200, 1500));
    fake_hwnd_right_ =
        initializer->CreateFakeHwnd(gfx::Rect(6400, 0, 3840, 2400));
  }

  HWND GetLeftFakeHwnd() {
    return fake_hwnd_left_;
  }

  HWND GetRightFakeHwnd() {
    return fake_hwnd_right_;
  }

 private:
  HWND fake_hwnd_left_ = nullptr;
  HWND fake_hwnd_right_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplays2x1xVirtualized,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(182.5, 347),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(1599.5, 799.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(3199, 1599)));

  expect_point_f_eq(gfx::PointF(3200, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(6400, 0)));
  expect_point_f_eq(gfx::PointF(4192, 175.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(8384, 351)));
  expect_point_f_eq(gfx::PointF(5119.5, 1199.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(10239, 2399)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(3198, 1598),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(1599, 799)));

  EXPECT_EQ(gfx::Point(6400, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(3200, 0)));
  EXPECT_EQ(gfx::Point(8384, 350),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(4192, 175)));
  EXPECT_EQ(gfx::Point(10238, 2398),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(5119, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, ClientToDIPPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(left_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599), GetScreenWin()->ClientToDIPPoint(
                                      left_hwnd, gfx::Point(1919, 1199)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(right_hwnd, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599), GetScreenWin()->ClientToDIPPoint(
                                      right_hwnd, gfx::Point(1919, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, DIPToClientPoints) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(left_hwnd, gfx::Point(959, 599)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(right_hwnd, gfx::Point(959, 599)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, ScreenToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ScreenToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ScreenToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(3200, 0, 100, 150),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(6400, 0, 200, 300)));
  EXPECT_EQ(gfx::Rect(3500, 248, 50, 100),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(7000, 496, 100, 200)));

  EXPECT_EQ(gfx::Rect(3190, 100, 50, 50),
            GetScreenWin()->ScreenToDIPRect(right_hwnd,
                                            gfx::Rect(6380, 200, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, DIPToScreenRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToScreenRect(
                                          left_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(left_hwnd, gfx::Rect(126, 248, 21, 26)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(6400, 0, 200, 300),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(3200, 0, 100, 150)));
  EXPECT_EQ(gfx::Rect(7000, 496, 100, 200),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(3500, 248, 50, 100)));

  EXPECT_EQ(gfx::Rect(6380, 200, 100, 100),
            GetScreenWin()->DIPToScreenRect(right_hwnd,
                                            gfx::Rect(3190, 100, 50, 50)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, DIPToScreenRectNullHWND) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(126, 248, 21, 26)));

  EXPECT_EQ(
      gfx::Rect(6400, 0, 200, 300),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(3200, 0, 100, 150)));
  EXPECT_EQ(
      gfx::Rect(7000, 496, 100, 200),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(3500, 248, 50, 100)));

  EXPECT_EQ(
      gfx::Rect(6380, 200, 100, 100),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(3190, 100, 50, 50)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, ClientToDIPRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ClientToDIPRect(
                                         left_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(left_hwnd, gfx::Rect(253, 496, 41, 52)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50), GetScreenWin()->ClientToDIPRect(
                                         right_hwnd, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(right_hwnd, gfx::Rect(253, 496, 41, 52)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, DIPToClientRects) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToClientRect(
                                          left_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(left_hwnd, gfx::Rect(126, 248, 21, 26)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100), GetScreenWin()->DIPToClientRect(
                                          right_hwnd, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(right_hwnd, gfx::Rect(126, 248, 21, 26)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, ScreenToDIPSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(left_hwnd, gfx::Size(42, 131)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(right_hwnd, gfx::Size(42, 131)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, DIPToScreenSize) {
  HWND left_hwnd = GetLeftFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(left_hwnd, gfx::Size(21, 66)));

  HWND right_hwnd = GetRightFakeHwnd();
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(right_hwnd, gfx::Size(21, 66)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetSystemMetricsInDIP) {
  EXPECT_EQ(16, GetScreenWin()->GetSystemMetricsInDIP(31));
  EXPECT_EQ(21, GetScreenWin()->GetSystemMetricsInDIP(42));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetScaleFactorForHWND) {
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetLeftFakeHwnd()));
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(GetRightFakeHwnd()));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(gfx::Rect(0, 0, 1600, 800), displays[0].bounds());
  EXPECT_EQ(gfx::Rect(0, 0, 1600, 750), displays[0].work_area());
  EXPECT_EQ(gfx::Rect(3200, 0, 1920, 1200), displays[1].bounds());
  EXPECT_EQ(gfx::Rect(3200, 0, 1920, 1200), displays[1].work_area());
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetNumDisplays) {
  EXPECT_EQ(2, GetScreen()->GetNumDisplays());
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized,
       GetDisplayNearestWindowPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay(),
            screen->GetDisplayNearestWindow(nullptr));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetDisplayNearestWindow) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  gfx::NativeWindow left_window = GetNativeWindowFromHWND(GetLeftFakeHwnd());
  EXPECT_EQ(left_display, screen->GetDisplayNearestWindow(left_window));

  gfx::NativeWindow right_window = GetNativeWindowFromHWND(GetRightFakeHwnd());
  EXPECT_EQ(right_display, screen->GetDisplayNearestWindow(right_window));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetDisplayNearestPoint) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(0, 0)));
  EXPECT_EQ(left_display, screen->GetDisplayNearestPoint(gfx::Point(125, 476)));
  EXPECT_EQ(left_display,
            screen->GetDisplayNearestPoint(gfx::Point(1599, 799)));

  EXPECT_EQ(right_display, screen->GetDisplayNearestPoint(gfx::Point(3200, 0)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(4000, 400)));
  EXPECT_EQ(right_display,
            screen->GetDisplayNearestPoint(gfx::Point(5119, 1199)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetDisplayMatching) {
  Screen* screen = GetScreen();
  const Display left_display = screen->GetAllDisplays()[0];
  const Display right_display = screen->GetAllDisplays()[1];

  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(0, 0, 100, 100)));
  EXPECT_EQ(left_display,
            screen->GetDisplayMatching(gfx::Rect(1499, 699, 100, 100)));

  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(3200, 0, 100, 100)));
  EXPECT_EQ(right_display,
            screen->GetDisplayMatching(gfx::Rect(5019, 1099, 100, 100)));
}

TEST_P(ScreenWinTestTwoDisplays2x1xVirtualized, GetPrimaryDisplay) {
  Screen* screen = GetScreen();
  EXPECT_EQ(gfx::Point(0, 0), screen->GetPrimaryDisplay().bounds().origin());
}

namespace {

// Forced 1x DPI for Other Tests without TestScreenWin.
class ScreenWinUninitializedForced1x : public testing::Test {
 public:
  ScreenWinUninitializedForced1x() = default;

  ScreenWinUninitializedForced1x(const ScreenWinUninitializedForced1x&) =
      delete;
  ScreenWinUninitializedForced1x& operator=(
      const ScreenWinUninitializedForced1x&) = delete;

  void SetUp() override {
    testing::Test::SetUp();
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kForceDeviceScaleFactor, "1");
  }

  void TearDown() override {
    ScreenWin::ResetFallbackScreenForTesting();
    Display::ResetForceDeviceScaleFactorForTesting();
    testing::Test::TearDown();
  }
};

}  // namespace

TEST_F(ScreenWinUninitializedForced1x, ScreenToDIPPoints) {
  gfx::PointF origin(0, 0);
  gfx::PointF middle(365, 694);
  gfx::PointF lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->ScreenToDIPPoint(origin));
  EXPECT_EQ(middle, GetScreenWin()->ScreenToDIPPoint(middle));
  EXPECT_EQ(lower_right, GetScreenWin()->ScreenToDIPPoint(lower_right));
}

TEST_F(ScreenWinUninitializedForced1x, DIPToScreenPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenPoint(origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenPoint(middle));
  EXPECT_EQ(lower_right, GetScreenWin()->DIPToScreenPoint(lower_right));
}

TEST_F(ScreenWinUninitializedForced1x, ClientToDIPPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPPoint(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPPoint(nullptr, middle));
  EXPECT_EQ(lower_right,
            GetScreenWin()->ClientToDIPPoint(nullptr, lower_right));
}

TEST_F(ScreenWinUninitializedForced1x, DIPToClientPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientPoint(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientPoint(nullptr, middle));
  EXPECT_EQ(lower_right,
            GetScreenWin()->DIPToClientPoint(nullptr, lower_right));
}

TEST_F(ScreenWinUninitializedForced1x, ScreenToDIPRects) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->ScreenToDIPRect(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->ScreenToDIPRect(nullptr, middle));
}

TEST_F(ScreenWinUninitializedForced1x, DIPToScreenRects) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenRect(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenRect(nullptr, middle));
}

TEST_F(ScreenWinUninitializedForced1x, ClientToDIPRects) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->ClientToDIPRect(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->ClientToDIPRect(nullptr, middle));
}

TEST_F(ScreenWinUninitializedForced1x, DIPToClientRects) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToClientRect(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToClientRect(nullptr, middle));
}

TEST_F(ScreenWinUninitializedForced1x, ScreenToDIPSize) {
  gfx::Size size(42, 131);
  EXPECT_EQ(size, GetScreenWin()->ScreenToDIPSize(nullptr, size));
}

TEST_F(ScreenWinUninitializedForced1x, DIPToScreenSize) {
  gfx::Size size(42, 131);
  EXPECT_EQ(size, GetScreenWin()->DIPToScreenSize(nullptr, size));
}

TEST_F(ScreenWinUninitializedForced1x, GetSystemMetricsInDIP) {
  // GetSystemMetricsInDIP falls back to the system's GetSystemMetrics, so this
  // test is to make sure we don't crash.
  GetScreenWin()->GetSystemMetricsInDIP(SM_CXSIZEFRAME);
}

TEST_F(ScreenWinUninitializedForced1x, GetScaleFactorForHWND) {
  EXPECT_EQ(1.0, GetScreenWin()->GetScaleFactorForHWND(nullptr));
}

namespace {

// Forced 2x DPI for Other Tests without TestScreenWin.
class ScreenWinUninitializedForced2x : public testing::Test {
 public:
  ScreenWinUninitializedForced2x() = default;

  ScreenWinUninitializedForced2x(const ScreenWinUninitializedForced2x&) =
      delete;
  ScreenWinUninitializedForced2x& operator=(
      const ScreenWinUninitializedForced2x&) = delete;

  void SetUp() override {
    testing::Test::SetUp();
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kForceDeviceScaleFactor, "2");
  }

  void TearDown() override {
    ScreenWin::ResetFallbackScreenForTesting();
    Display::ResetForceDeviceScaleFactorForTesting();
    testing::Test::TearDown();
  }
};

}  // namespace

TEST_F(ScreenWinUninitializedForced2x, ScreenToDIPPoints) {
  expect_point_f_eq(gfx::PointF(0, 0),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(0, 0)));
  expect_point_f_eq(gfx::PointF(182.5, 347),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(365, 694)));
  expect_point_f_eq(gfx::PointF(959.5, 599.5),
                    GetScreenWin()->ScreenToDIPPoint(gfx::PointF(1919, 1199)));
}

TEST_F(ScreenWinUninitializedForced2x, DIPToScreenPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToScreenPoint(gfx::Point(959, 599)));
}

TEST_F(ScreenWinUninitializedForced2x, ClientToDIPPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->ClientToDIPPoint(nullptr, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(182, 347),
            GetScreenWin()->ClientToDIPPoint(nullptr, gfx::Point(365, 694)));
  EXPECT_EQ(gfx::Point(959, 599),
            GetScreenWin()->ClientToDIPPoint(nullptr, gfx::Point(1919, 1199)));
}

TEST_F(ScreenWinUninitializedForced2x, DIPToClientPoints) {
  EXPECT_EQ(gfx::Point(0, 0),
            GetScreenWin()->DIPToClientPoint(nullptr, gfx::Point(0, 0)));
  EXPECT_EQ(gfx::Point(364, 694),
            GetScreenWin()->DIPToClientPoint(nullptr, gfx::Point(182, 347)));
  EXPECT_EQ(gfx::Point(1918, 1198),
            GetScreenWin()->DIPToClientPoint(nullptr, gfx::Point(959, 599)));
}

TEST_F(ScreenWinUninitializedForced2x, ScreenToDIPRects) {
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50),
            GetScreenWin()->ScreenToDIPRect(nullptr, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ScreenToDIPRect(nullptr, gfx::Rect(253, 496, 41, 52)));
}

TEST_F(ScreenWinUninitializedForced2x, DIPToScreenRects) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToScreenRect(nullptr, gfx::Rect(126, 248, 21, 26)));
}

TEST_F(ScreenWinUninitializedForced2x, ClientToDIPRects) {
  EXPECT_EQ(gfx::Rect(0, 0, 25, 50),
            GetScreenWin()->ClientToDIPRect(nullptr, gfx::Rect(0, 0, 50, 100)));
  EXPECT_EQ(
      gfx::Rect(126, 248, 21, 26),
      GetScreenWin()->ClientToDIPRect(nullptr, gfx::Rect(253, 496, 41, 52)));
}

TEST_F(ScreenWinUninitializedForced2x, DIPToClientRects) {
  EXPECT_EQ(gfx::Rect(0, 0, 50, 100),
            GetScreenWin()->DIPToClientRect(nullptr, gfx::Rect(0, 0, 25, 50)));
  EXPECT_EQ(
      gfx::Rect(252, 496, 42, 52),
      GetScreenWin()->DIPToClientRect(nullptr, gfx::Rect(126, 248, 21, 26)));
}

TEST_F(ScreenWinUninitializedForced2x, ScreenToDIPSize) {
  EXPECT_EQ(gfx::Size(21, 66),
            GetScreenWin()->ScreenToDIPSize(nullptr, gfx::Size(42, 131)));
}

TEST_F(ScreenWinUninitializedForced2x, DIPToScreenSize) {
  EXPECT_EQ(gfx::Size(42, 132),
            GetScreenWin()->DIPToScreenSize(nullptr, gfx::Size(21, 66)));
}

TEST_F(ScreenWinUninitializedForced2x, GetSystemMetricsInDIP) {
  // This falls back to the system's GetSystemMetrics, so
  // this test is to make sure we don't crash.
  GetScreenWin()->GetSystemMetricsInDIP(SM_CXSIZEFRAME);
}

TEST_F(ScreenWinUninitializedForced2x, GetScaleFactorForHWND) {
  EXPECT_EQ(2.0, GetScreenWin()->GetScaleFactorForHWND(nullptr));
}

namespace {

// Two Displays, one of which is internal (eg. a laptop screen).
class ScreenWinTestTwoDisplaysOneInternal : public ScreenWinTest {
 public:
  ScreenWinTestTwoDisplaysOneInternal() = default;

  ScreenWinTestTwoDisplaysOneInternal(
      const ScreenWinTestTwoDisplaysOneInternal&) = delete;
  ScreenWinTestTwoDisplaysOneInternal& operator=(
      const ScreenWinTestTwoDisplaysOneInternal&) = delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100), L"primary", 1.0,
                            DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL);
    initializer->AddMonitor(gfx::Rect(1920, 0, 800, 600),
                            gfx::Rect(1920, 0, 800, 600), L"secondary", 1.0);
  }
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestTwoDisplaysOneInternal,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestTwoDisplaysOneInternal, InternalDisplayIdSet) {
  EXPECT_NE(Display::InternalDisplayId(), kInvalidDisplayId);
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(2u, displays.size());
  EXPECT_EQ(Display::InternalDisplayId(), displays[0].id());
  EXPECT_NE(Display::InternalDisplayId(), displays[1].id());
}

namespace {

// One display with a max-length |szDevice| value.
class ScreenWinTestOneDisplayLongName : public ScreenWinTest {
 public:
  ScreenWinTestOneDisplayLongName() = default;

  ScreenWinTestOneDisplayLongName(const ScreenWinTestOneDisplayLongName&) =
      delete;
  ScreenWinTestOneDisplayLongName& operator=(
      const ScreenWinTestOneDisplayLongName&) = delete;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {
    const wchar_t* device_name = L"ThisDeviceNameIs32CharactersLong";
    EXPECT_EQ(::wcslen(device_name),
              static_cast<size_t>(ARRAYSIZE(MONITORINFOEX::szDevice)));
    initializer->AddMonitor(gfx::Rect(0, 0, 1920, 1200),
                            gfx::Rect(0, 0, 1920, 1100), device_name, 1.0);
  }
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestOneDisplayLongName,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestOneDisplayLongName, CheckIdStability) {
  // Callers may use the display ID as a way to persist data like window
  // coordinates across runs. As a result, the IDs must remain stable.
  Screen* screen = GetScreen();
  ASSERT_EQ(1, screen->GetNumDisplays());
  EXPECT_EQ(1875308985, screen->GetAllDisplays()[0].id());
}

namespace {

// Zero displays.
class ScreenWinTestNoDisplay : public ScreenWinTest {
 public:
  ScreenWinTestNoDisplay() = default;

  void SetUpScreen(TestScreenWinInitializer* initializer) override {}
};

INSTANTIATE_TEST_SUITE_P(All,
                         ScreenWinTestNoDisplay,
                         ::testing::Bool(),
                         ScreenWinTest::ParamInfoToString);

}  // namespace

TEST_P(ScreenWinTestNoDisplay, DIPToScreenPoints) {
  gfx::Point origin(0, 0);
  gfx::Point middle(365, 694);
  gfx::Point lower_right(1919, 1199);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenPoint(origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenPoint(middle));
  EXPECT_EQ(lower_right, GetScreenWin()->DIPToScreenPoint(lower_right));
}

TEST_P(ScreenWinTestNoDisplay, DIPToScreenRectNullHWND) {
  gfx::Rect origin(0, 0, 50, 100);
  gfx::Rect middle(253, 495, 41, 52);
  EXPECT_EQ(origin, GetScreenWin()->DIPToScreenRect(nullptr, origin));
  EXPECT_EQ(middle, GetScreenWin()->DIPToScreenRect(nullptr, middle));
}

// GetPrimaryDisplay should return a valid display even if there is no display.
TEST_P(ScreenWinTestNoDisplay, GetPrimaryDisplay) {
  auto primary = GetScreen()->GetPrimaryDisplay();
  EXPECT_NE(primary.id(), display::kInvalidDisplayId);
  EXPECT_TRUE(primary.bounds().origin().IsOrigin());
  EXPECT_FALSE(primary.bounds().IsEmpty());
  EXPECT_FALSE(primary.work_area().IsEmpty());
  EXPECT_FALSE(primary.detected());
}

TEST_P(ScreenWinTestNoDisplay, GetDisplays) {
  std::vector<Display> displays = GetScreen()->GetAllDisplays();
  ASSERT_EQ(0u, displays.size());
}

TEST_P(ScreenWinTestNoDisplay, GetNumDisplays) {
  EXPECT_EQ(0, GetScreen()->GetNumDisplays());
}

}  // namespace win
}  // namespace display
