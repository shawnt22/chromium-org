// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_BOOKMARKS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_BOOKMARKS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_BUTTON_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/event_utils.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "content/public/browser/page.h"
#include "content/public/browser/page_navigator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/drag_controller.h"

class Browser;

namespace gfx {
class Canvas;
}

namespace views {
class MenuRunner;
}

namespace tab_groups {

class STGTabsMenuModel;

// The visual representation of a SavedTabGroup shown in the bookmarks bar.
class SavedTabGroupButton : public views::MenuButton,
                            public views::DragController,
                            public views::ContextMenuController {
  METADATA_HEADER(SavedTabGroupButton, views::MenuButton)

 public:
  SavedTabGroupButton(const SavedTabGroup& group,
                      PressedCallback callback,
                      Browser* browser,
                      bool animations_enabled = true);

  SavedTabGroupButton(const SavedTabGroupButton&) = delete;
  SavedTabGroupButton& operator=(const SavedTabGroupButton&) = delete;
  ~SavedTabGroupButton() override;

  // views::MenuButton:
  void PaintButtonContents(gfx::Canvas* canvas) override;
  std::unique_ptr<views::LabelButtonBorder> CreateDefaultBorder()
      const override;
  void OnThemeChanged() override;

  void UpdateCachedTooltipText();

  // views::View
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  gfx::Point GetKeyboardContextMenuLocation() override;

  // views::LabelButton
  bool IsTriggerableEvent(const ui::Event& e) override;

  // views::DragController
  void WriteDragDataForView(View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(View* sender, const gfx::Point& p) override;
  bool CanStartDragForView(View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

  // views::ContextMenuController
  void ShowContextMenuForViewImpl(
      View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // Updates the buttons visuals (title and color) alongside its list of tabs
  // displayed in the context menu.
  void UpdateButtonData(const SavedTabGroup& group);

  tab_groups::TabGroupColorId tab_group_color_id() const {
    return tab_group_color_id_;
  }

  const base::Uuid guid() const { return guid_; }

 private:
  FRIEND_TEST_ALL_PREFIXES(SavedTabGroupBarUnitTest, AccessibleName);
  FRIEND_TEST_ALL_PREFIXES(SavedTabGroupBarUnitTest, TooltipText);

  std::u16string GetAccessibleNameForButton() const;
  void SetTextProperties(const SavedTabGroup& group);
  void UpdateButtonLayout();
  void UpdateAccessibleName();
  void SetText(std::u16string_view text) override;
  int GetAndIncrementLatestCommandId();

  raw_ptr<Browser> browser_;

  // The animations for button movement.
  std::unique_ptr<gfx::SlideAnimation> show_animation_;

  // The shared state of the TabGroup.
  bool is_shared_;

  // The color of the TabGroup this button is associated with.
  tab_groups::TabGroupColorId tab_group_color_id_;

  // The guid used to identify the group this button represents.
  base::Uuid guid_;

  // The local guid used to identify the group in the tabstrip if it is open.
  std::optional<tab_groups::TabGroupId> local_group_id_;

  // The command id that gets updated and assigned to the context menu
  // commands.
  int latest_command_id_ = -1;

  // Menu model used by the context menu.
  std::unique_ptr<STGTabsMenuModel> menu_model_;

  // Context menu runner used for this View.
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
};

}  // namespace tab_groups

#endif  // CHROME_BROWSER_UI_VIEWS_BOOKMARKS_SAVED_TAB_GROUPS_SAVED_TAB_GROUP_BUTTON_H_
