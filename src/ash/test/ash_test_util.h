// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_ASH_TEST_UTIL_H_
#define ASH_TEST_ASH_TEST_UTIL_H_

#include <array>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "chromeos/ui/frame/caption_buttons/frame_size_button.h"
#include "chromeos/ui/frame/multitask_menu/multitask_menu_metrics.h"
#include "crypto/hash.h"
#include "services/data_decoder/public/mojom/image_decoder.mojom-shared.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/aura/window.h"
#include "ui/gfx/image/image_skia.h"

namespace base {
class FilePath;
class TimeDelta;
}  // namespace base

namespace chromeos {
class MultitaskMenu;
}  // namespace chromeos

namespace gfx {
class Size;
}  // namespace gfx

namespace ui {
class Layer;
namespace test {
class EventGenerator;
}  // namespace test
}  // namespace ui

namespace views {
class MenuItemView;
class View;
}  // namespace views

namespace ash {

// Takes a screenshot of the primary display and saves the screenshot picture to
// the location specified by `file_path`. Returns true if the screenshot is
// taken and saved successfully. Useful for debugging ash unit tests. When using
// this function on an ash unit test, the test code should be executed with
// --enable-pixel-output-in-tests flag.
// NOTE: `file_path` must end with the extension '.png'. If there is an existing
// file matching `file_path`, the existing file will be overwritten.
bool TakePrimaryDisplayScreenshotAndSave(const base::FilePath& file_path);

// Waits for the specified time duration.
// NOTE: this function should only be used for debugging. It should not be used
// in tests or product code.
void GiveItSomeTimeForDebugging(base::TimeDelta time_duration);

// Returns true if the system tray of the root window specified by
// `root_window_index` is visible.
bool IsSystemTrayForRootWindowVisible(size_t root_window_index);

// Creates a pure color image of the specified size.
gfx::ImageSkia CreateSolidColorTestImage(const gfx::Size& image_size,
                                         SkColor color);

// Creates a solid color image with the given `size` and `color`, and returns
// its encoded representation. `image_out` is filled with the raw decoded image
// if provided.
//
// This function can never fail.
std::string CreateEncodedImageForTesting(
    const gfx::Size& size,
    SkColor color = SK_ColorBLACK,
    data_decoder::mojom::ImageCodec codec =
        data_decoder::mojom::ImageCodec::kDefault,
    gfx::ImageSkia* image_out = nullptr);

// Configures `window` with the specified title and color.
void DecorateWindow(aura::Window* window,
                    const std::u16string& title,
                    SkColor color);

// Waits until there is any visible menu item view with the specified `label`.
// Returns the pointer to the first found target menu item view.
views::MenuItemView* WaitForMenuItemWithLabel(const std::u16string& label);

// Shows and returns the clamshell multitask menu which is anchored to the frame
// size button. Some tests create their own caption button container and
// therefore their own size button. We use that if it is passed, otherwise try
// to fetch the size button from the non client frame view ash.
chromeos::MultitaskMenu* ShowAndWaitMultitaskMenuForWindow(
    std::variant<aura::Window*, chromeos::FrameSizeButton*>
        window_or_size_button,
    chromeos::MultitaskMenuEntryType entry_type =
        chromeos::MultitaskMenuEntryType::kFrameSizeButtonHover);

// Sends a press release key combo `count` times.
void SendKey(ui::KeyboardCode key_code,
             ui::test::EventGenerator* event_generator = nullptr,
             int flags = ui::EF_NONE,
             int count = 1);

// Returns a pointer to the `ui::Layer` in the layer tree associated with the
// specified `layer` which has the specified `name`. In the event that no such
// layer is found, `nullptr` is returned.
ui::Layer* FindLayerWithName(ui::Layer* layer, std::string_view name);

// Returns a pointer to the `ui::Layer` in the layer tree associated with the
// specified `view` which has the specified `name`. In the event that no such
// layer is found, `nullptr` is returned.
ui::Layer* FindLayerWithName(views::View* view, std::string_view name);

// Returns a pointer to the `views::Widget` with the specified `name` found
// across all root windows. In the event that no such widget is found, `nullptr`
// is returned.
views::Widget* FindWidgetWithName(std::string_view name);

// Returns a pointer to the `views::Widget` with the specified `name` found
// across all root windows. If no such widget when this function is called,
// waits until there is one.
// NOTE: This function causes an infinite loop if the target widget never shows.
views::Widget* FindWidgetWithNameAndWaitIfNeeded(const std::string& name);

// Given a range r with elements r0, r1, ..., rn and an optional projection p
// (which defaults to the identity), return a stable hash of the concatenation
// of p(r0), p(r1), ..., p(rn). This is used in some tests to ensure that global
// data tables are not changed without consulting the owners of those tables or
// that other invariants are not violated.
//
// For example, use this like:
//   std::vector<MyType> things;
//   std::string hash = StableHashOfCollection(things, [](const MyType& thing) {
//     return thing.ToString();
//   };
//
// The projection must return something that base::as_byte_span() accepts - a
// string, vector of bytes, or many other range-like types are acceptable.
//
// This hash is guaranteed stable: the same values, supplied in the same order,
// will hash to the same value between test runs.
template <typename Range, typename Proj = std::identity>
  requires std::ranges::range<Range> &&
           std::indirectly_unary_invocable<Proj, std::ranges::iterator_t<Range>>
std::string StableHashOfCollection(const Range& range, Proj proj = {}) {
  crypto::hash::Hasher hasher(crypto::hash::HashKind::kSha256);
  for (const auto& element : range) {
    hasher.Update(base::as_byte_span(proj(element)));
  }
  std::array<uint8_t, crypto::hash::kSha256Size> hash;
  hasher.Finish(hash);
  return base::ToLowerASCII(base::HexEncode(base::as_string_view(hash)));
}

}  // namespace ash

#endif  // ASH_TEST_ASH_TEST_UTIL_H_
