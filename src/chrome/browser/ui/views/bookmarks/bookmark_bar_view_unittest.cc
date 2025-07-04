// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"

#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/uuid.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service.h"
#include "chrome/browser/bookmarks/bookmark_merged_surface_service_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_test_helpers.h"
#include "chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_test_util.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view_test_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/native_widget_factory.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_client.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

namespace {

class BookmarkBarViewBaseTest : public ChromeViewsTestBase {
 public:
  BookmarkBarViewBaseTest() {
    TestingProfile::Builder profile_builder;
    profile_builder.AddTestingFactory(
        TemplateURLServiceFactory::GetInstance(),
        TemplateURLServiceTestUtil::GetTemplateURLServiceTestingFactory());
    profile_builder.AddTestingFactory(
        BookmarkModelFactory::GetInstance(),
        BookmarkModelFactory::GetDefaultFactory());
    profile_builder.AddTestingFactory(
        ManagedBookmarkServiceFactory::GetInstance(),
        ManagedBookmarkServiceFactory::GetDefaultFactory());
    profile_builder.AddTestingFactory(
        BookmarkMergedSurfaceServiceFactory::GetInstance(),
        BookmarkMergedSurfaceServiceFactory::GetDefaultFactory());
    profile_ = profile_builder.Build();

    BookmarkMergedSurfaceServiceFactory::GetForProfile(profile_.get())
        ->LoadForTesting({});

    Browser::CreateParams params(profile(), true);
    params.window = &browser_window_;
    browser_ = Browser::DeprecatedCreateOwnedForTesting(params);
  }

  virtual BookmarkBarView* bookmark_bar_view() = 0;

  TestingProfile* profile() { return profile_.get(); }
  Browser* browser() { return browser_.get(); }

 protected:
  // Returns a string containing the label of each of the *visible* buttons on
  // the bookmark bar. Each label is separated by a space.
  std::string GetStringForVisibleButtons() {
    std::string result;
    for (size_t i = 0; i < test_helper_->GetBookmarkButtonCount() &&
                       test_helper_->GetBookmarkButton(i)->GetVisible();
         ++i) {
      if (i != 0) {
        result += " ";
      }
      result +=
          base::UTF16ToASCII(test_helper_->GetBookmarkButton(i)->GetText());
    }
    return result;
  }

  // Continues enlarging the bookmark bar until it has at least `count`
  // buttons that are visible.
  // NOTE: if the model has more than |count| buttons this results in
  // |count| + 1 buttons.
  void SizeUntilButtonsVisible(size_t count) {
    const int start_width = bookmark_bar_view()->width();
    const int height = bookmark_bar_view()->GetPreferredSize().height();
    for (size_t i = 0;
         i < 100 && (test_helper_->GetBookmarkButtonCount() < count ||
                     !test_helper_->GetBookmarkButton(count - 1)->GetVisible());
         ++i) {
      bookmark_bar_view()->SetBounds(0, 0, start_width + i * 10, height);
      views::test::RunScheduledLayout(bookmark_bar_view());
    }
  }

  // Continues shrinking the bookmark bar until it has at most `count` buttons
  // that are visible.
  void SizeDownUntilButtonsVisible(size_t count) {
    const int start_width = bookmark_bar_view()->width();
    const int height = bookmark_bar_view()->GetPreferredSize().height();
    // Keep shrinking the bar view's bounds until either:
    // - There are fewer bookmark buttons than `count`.
    // - The button at index `count` is hidden.
    // - Up to a maximum of 100 times.
    for (size_t i = 0;
         i < 100 && (test_helper_->GetBookmarkButtonCount() >= count &&
                     test_helper_->GetBookmarkButton(count)->GetVisible());
         ++i) {
      bookmark_bar_view()->SetBounds(0, 0, start_width - i * 10, height);
      views::test::RunScheduledLayout(bookmark_bar_view());
    }
  }

  BookmarkModel* model() {
    return BookmarkModelFactory::GetForBrowserContext(profile());
  }

  BookmarkMergedSurfaceService* service() {
    return BookmarkMergedSurfaceServiceFactory::GetForProfile(profile());
  }

  void WaitForBookmarkModelToLoad() {
    bookmarks::test::WaitForBookmarkModelToLoad(model());
  }

  // Adds nodes to the bookmark bar node from |string|. See
  // bookmarks::test::AddNodesFromModelString() for details on |string|.
  void AddNodesToBookmarkBarFromModelString(const std::string& string) {
    bookmarks::test::AddNodesFromModelString(
        model(), model()->bookmark_bar_node(), string);
    views::test::RunScheduledLayout(bookmark_bar_view());
  }

  void AddNodesToAccountBookmarkBarFromModelString(const std::string& string) {
    CHECK(model()->account_bookmark_bar_node());
    bookmarks::test::AddNodesFromModelString(
        model(), model()->account_bookmark_bar_node(), string);
    views::test::RunScheduledLayout(bookmark_bar_view());
  }

  // Creates the model, blocking until it loads, then creates the
  // BookmarkBarView.
  std::unique_ptr<BookmarkBarView> CreateBookmarkModelAndBookmarkBarView() {
    WaitForBookmarkModelToLoad();

    auto bookmark_bar_view =
        std::make_unique<BookmarkBarView>(browser(), nullptr);
    test_helper_ =
        std::make_unique<BookmarkBarViewTestHelper>(bookmark_bar_view.get());
    return bookmark_bar_view;
  }

  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<TestingProfile> profile_;
  TestBrowserWindow browser_window_;
  std::unique_ptr<Browser> browser_;
  std::unique_ptr<BookmarkBarViewTestHelper> test_helper_;
};

class BookmarkBarViewTest : public BookmarkBarViewBaseTest {
 public:
  BookmarkBarViewTest() = default;
  BookmarkBarViewTest(const BookmarkBarViewTest&) = delete;
  BookmarkBarViewTest& operator=(const BookmarkBarViewTest&) = delete;
  ~BookmarkBarViewTest() override = default;

  // BookmarkBarViewBaseTest
  void SetUp() override {
    BookmarkBarViewBaseTest::SetUp();

    bookmark_bar_view_ = CreateBookmarkModelAndBookmarkBarView();
  }

  void TearDown() override {
    BookmarkBarViewBaseTest::TearDown();

    bookmark_bar_view_.reset();
  }

  BookmarkBarView* bookmark_bar_view() override {
    return bookmark_bar_view_.get();
  }

 private:
  base::test::ScopedFeatureList features_{
      switches::kSyncEnableBookmarksInTransportMode};
  std::unique_ptr<BookmarkBarView> bookmark_bar_view_;
};

class BookmarkBarViewInWidgetTest : public BookmarkBarViewBaseTest {
 public:
  BookmarkBarViewInWidgetTest() = default;
  BookmarkBarViewInWidgetTest(const BookmarkBarViewInWidgetTest&) = delete;
  BookmarkBarViewInWidgetTest& operator=(const BookmarkBarViewInWidgetTest&) =
      delete;
  ~BookmarkBarViewInWidgetTest() override = default;

  // BookmarkBarViewBaseTest
  void SetUp() override {
    set_native_widget_type(NativeWidgetType::kDesktop);
    BookmarkBarViewBaseTest::SetUp();

    widget_ =
        CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
    bookmark_bar_view_ =
        widget_->SetContentsView(CreateBookmarkModelAndBookmarkBarView());
  }

  void TearDown() override {
    bookmark_bar_view_ = nullptr;
    widget_.reset();

    BookmarkBarViewBaseTest::TearDown();
  }

  BookmarkBarView* bookmark_bar_view() override { return bookmark_bar_view_; }

  views::Widget* widget() { return widget_.get(); }

 private:
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<BookmarkBarView> bookmark_bar_view_ = nullptr;
};

// Verify that in instant extended mode the visibility of the apps shortcut
// button properly follows the pref value.
TEST_F(BookmarkBarViewTest, AppsShortcutVisibility) {
  browser()->profile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kShowAppsShortcutInBookmarkBar, false);
  EXPECT_FALSE(test_helper_->apps_page_shortcut()->GetVisible());

  // Try to make the Apps shortcut visible. Its visibility depends on whether
  // the Apps shortcut is enabled.
  browser()->profile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kShowAppsShortcutInBookmarkBar, true);
  if (chrome::IsAppsShortcutEnabled(browser()->profile())) {
    EXPECT_TRUE(test_helper_->apps_page_shortcut()->GetVisible());
  } else {
    EXPECT_FALSE(test_helper_->apps_page_shortcut()->GetVisible());
  }

  // Make sure we can also properly transition from true to false.
  browser()->profile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kShowAppsShortcutInBookmarkBar, false);
  EXPECT_FALSE(test_helper_->apps_page_shortcut()->GetVisible());
}

TEST_F(BookmarkBarViewTest, TabGroupsBarVisibility) {
  // Pref to show by default. Tab group bar is visible by default.
  EXPECT_TRUE(test_helper_->saved_tab_group_bar()->GetVisible());

  // Pref not to show hides tab group bar.
  browser()->profile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kShowTabGroupsInBookmarkBar, false);
  EXPECT_FALSE(test_helper_->saved_tab_group_bar()->GetVisible());

  // Pref to show displays tab group bar.
  browser()->profile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kShowTabGroupsInBookmarkBar, true);
  EXPECT_TRUE(test_helper_->saved_tab_group_bar()->GetVisible());
}

// Various assertions around visibility of the overflow_button.
TEST_F(BookmarkBarViewTest, OverflowVisibility) {
  EXPECT_FALSE(test_helper_->overflow_button()->GetVisible());

  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_TRUE(test_helper_->overflow_button()->GetVisible());

  SizeUntilButtonsVisible(1);
  EXPECT_EQ(2u, test_helper_->GetBookmarkButtonCount());
  const int width_for_one = bookmark_bar_view()->bounds().width();
  EXPECT_TRUE(test_helper_->overflow_button()->GetVisible());

  // Go really big, which should force all buttons to be added.
  bookmark_bar_view()->SetBounds(0, 0, 5000,
                                 bookmark_bar_view()->bounds().height());
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ(6u, test_helper_->GetBookmarkButtonCount());
  EXPECT_FALSE(test_helper_->overflow_button()->GetVisible());

  bookmark_bar_view()->SetBounds(0, 0, width_for_one,
                                 bookmark_bar_view()->bounds().height());
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_TRUE(test_helper_->overflow_button()->GetVisible());
}

// Verifies buttons get added correctly when BookmarkBarView is created after
// the model and the model has nodes.
// TODO(crbug.com/375364962): Flaky on Windows.
#if BUILDFLAG(IS_WIN)
#define MAYBE_ButtonsDynamicallyAddedAfterModelHasNodes \
  DISABLED_ButtonsDynamicallyAddedAfterModelHasNodes
#else
#define MAYBE_ButtonsDynamicallyAddedAfterModelHasNodes \
  ButtonsDynamicallyAddedAfterModelHasNodes
#endif
TEST_F(BookmarkBarViewTest, MAYBE_ButtonsDynamicallyAddedAfterModelHasNodes) {
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  SizeUntilButtonsVisible(1);
  EXPECT_EQ(2u, test_helper_->GetBookmarkButtonCount());

  // Go really big, which should force all buttons to be added.
  bookmark_bar_view()->SetBounds(0, 0, 5000,
                                 bookmark_bar_view()->bounds().height());
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ(6u, test_helper_->GetBookmarkButtonCount());

  // Ensure buttons were added in the correct place.
  auto button_iter = bookmark_bar_view()->FindChild(
      test_helper_->saved_tab_groups_separator_view_());
  for (size_t i = 0; i < test_helper_->GetBookmarkButtonCount(); ++i) {
    ++button_iter;
    ASSERT_NE(bookmark_bar_view()->children().cend(), button_iter);
    EXPECT_EQ(test_helper_->GetBookmarkButton(i), *button_iter);
  }
}

// Verifies buttons are added as the model and size change.
TEST_F(BookmarkBarViewTest, ButtonsDynamicallyAdded) {
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());
  SizeUntilButtonsVisible(1);
  EXPECT_EQ(2u, test_helper_->GetBookmarkButtonCount());

  // Go really big, which should force all buttons to be added.
  bookmark_bar_view()->SetBounds(0, 0, 5000,
                                 bookmark_bar_view()->bounds().height());
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ(6u, test_helper_->GetBookmarkButtonCount());
  // Ensure buttons were added in the correct place.
  auto button_iter = bookmark_bar_view()->FindChild(
      test_helper_->saved_tab_groups_separator_view_());
  for (size_t i = 0; i < test_helper_->GetBookmarkButtonCount(); ++i) {
    ++button_iter;
    ASSERT_NE(bookmark_bar_view()->children().cend(), button_iter);
    EXPECT_EQ(test_helper_->GetBookmarkButton(i), *button_iter);
  }
}

TEST_F(BookmarkBarViewTest, AddNodesWhenBarAlreadySized) {
  bookmark_bar_view()->SetBounds(0, 0, 5000,
                                 bookmark_bar_view()->bounds().height());
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ("a b c d e f", GetStringForVisibleButtons());
}

// Various assertions for removing nodes.
TEST_F(BookmarkBarViewTest, RemoveNode) {
  const BookmarkNode* bookmark_bar_node = model()->bookmark_bar_node();
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());
  SizeUntilButtonsVisible(1);
  EXPECT_EQ(2u, test_helper_->GetBookmarkButtonCount());

  // Remove the 2nd node, should still only have 1 visible.
  model()->Remove(bookmark_bar_node->children()[1].get(),
                  bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("a", GetStringForVisibleButtons());

  // Remove the first node, should force a new button (for the 'c' node).
  model()->Remove(bookmark_bar_node->children()[0].get(),
                  bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  views::test::RunScheduledLayout(bookmark_bar_view());
  ASSERT_EQ("c", GetStringForVisibleButtons());

  model()->CreateAccountPermanentFolders();
  AddNodesToAccountBookmarkBarFromModelString("1 2 3 ");

  model()->Remove(model()->account_bookmark_bar_node()->children()[1].get(),
                  bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("1", GetStringForVisibleButtons());

  // Remove first node, should force a new button (for the '3' node).
  model()->Remove(model()->account_bookmark_bar_node()->children()[0].get(),
                  bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  views::test::RunScheduledLayout(bookmark_bar_view());
  ASSERT_EQ("3", GetStringForVisibleButtons());
}

TEST_F(BookmarkBarViewTest, RemoveAccountNodes) {
  model()->CreateAccountPermanentFolders();
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  AddNodesToAccountBookmarkBarFromModelString("A1 A2 A3 A4 ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());
  SizeUntilButtonsVisible(10);
  EXPECT_EQ(10u, test_helper_->GetBookmarkButtonCount());
  EXPECT_EQ("A1 A2 A3 A4 a b c d e f", GetStringForVisibleButtons());

  // Remove the account nodes, local nodes should still be visible.
  model()->RemoveAccountPermanentFolders();
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("a b c d e f", GetStringForVisibleButtons());
}

TEST_F(BookmarkBarViewTest, RemoveAccountNodesCustomOrder) {
  model()->CreateAccountPermanentFolders();
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  AddNodesToAccountBookmarkBarFromModelString("A1 A2 A3 A4 ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());
  SizeUntilButtonsVisible(10);
  EXPECT_EQ("A1 A2 A3 A4 a b c d e f", GetStringForVisibleButtons());

  service()->Move(model()->account_bookmark_bar_node()->children()[1].get(),
                  BookmarkParentFolder::BookmarkBarFolder(), 10u,
                  /*browser=*/nullptr);
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("A1 A3 A4 a b c d e f A2", GetStringForVisibleButtons());

  // Remove the account nodes, local nodes should still be visible.
  model()->RemoveAccountPermanentFolders();
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("a b c d e f", GetStringForVisibleButtons());
}

TEST_F(BookmarkBarViewTest, RemoveAccountNodesNotAllAccountNodesVisible) {
  model()->CreateAccountPermanentFolders();
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  AddNodesToAccountBookmarkBarFromModelString("1 2 3 4 ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());
  SizeUntilButtonsVisible(2);
  EXPECT_EQ("1 2", GetStringForVisibleButtons());

  // Remove the account nodes, local nodes should still be visible.
  model()->RemoveAccountPermanentFolders();
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("a b", GetStringForVisibleButtons());
}

// Assertions for moving a node on the bookmark bar.
TEST_F(BookmarkBarViewTest, MoveNode) {
  const BookmarkNode* bookmark_bar_node = model()->bookmark_bar_node();
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  // Move 'c' first resulting in 'c a b d e f'.
  model()->Move(bookmark_bar_node->children()[2].get(), bookmark_bar_node, 0);
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  // Make enough room for 1 node.
  SizeUntilButtonsVisible(1);
  EXPECT_EQ("c", GetStringForVisibleButtons());

  // Move 'f' first, resulting in 'f c a b d e'.
  model()->Move(bookmark_bar_node->children()[5].get(), bookmark_bar_node, 0);
  SizeUntilButtonsVisible(2);
  EXPECT_EQ("f c", GetStringForVisibleButtons());

  // Move 'f' to the end, resulting in 'c a b d e f'.
  model()->Move(bookmark_bar_node->children()[0].get(), bookmark_bar_node, 6);
  SizeUntilButtonsVisible(2);
  EXPECT_EQ("c a", GetStringForVisibleButtons());

  // Move 'c' after 'a', resulting in 'a c b d e f'.
  model()->Move(bookmark_bar_node->children()[0].get(), bookmark_bar_node, 2);
  SizeUntilButtonsVisible(2);
  EXPECT_EQ("a c", GetStringForVisibleButtons());
}

// Ensures that the overflow button's menu responds as bookmark button
// visibility changes.
TEST_F(BookmarkBarViewInWidgetTest, ButtonVisiblityUpdatesOverflowMenu) {
  widget()->Show();
  AddNodesToBookmarkBarFromModelString("a b c d ");
  ASSERT_EQ(4u, test_helper_->GetBookmarkButtonCount());
  SizeDownUntilButtonsVisible(1);

  views::MenuButton* overflow_button = bookmark_bar_view()->overflow_button();
  ASSERT_TRUE(overflow_button);
  overflow_button->Activate(nullptr);
  views::MenuItemView* overflow_menu = bookmark_bar_view()->GetMenu();
  ASSERT_TRUE(overflow_menu && overflow_menu->HasSubmenu());
  EXPECT_EQ(3u, overflow_menu->GetSubmenu()->GetMenuItems().size());

  SizeUntilButtonsVisible(2);
  EXPECT_EQ(2u, overflow_menu->GetSubmenu()->GetMenuItems().size());

  SizeUntilButtonsVisible(3);
  EXPECT_EQ(1u, overflow_menu->GetSubmenu()->GetMenuItems().size());

  SizeDownUntilButtonsVisible(1);
  EXPECT_EQ(3u, overflow_menu->GetSubmenu()->GetMenuItems().size());
}

// TODO(crbug.com/375364962): Deflake and re-enable.
// Assertions for changing the title of a node.
TEST_F(BookmarkBarViewTest, DISABLED_ChangeTitle) {
  const BookmarkNode* bookmark_bar_node = model()->bookmark_bar_node();
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  model()->SetTitle(bookmark_bar_node->children()[0].get(), u"a1",
                    bookmarks::metrics::BookmarkEditSource::kUser);
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  // Make enough room for 1 node.
  SizeUntilButtonsVisible(1);
  EXPECT_EQ("a1", GetStringForVisibleButtons());

  model()->SetTitle(bookmark_bar_node->children()[1].get(), u"b1",
                    bookmarks::metrics::BookmarkEditSource::kUser);
  EXPECT_EQ("a1", GetStringForVisibleButtons());

  model()->SetTitle(bookmark_bar_node->children()[5].get(), u"f1",
                    bookmarks::metrics::BookmarkEditSource::kUser);
  EXPECT_EQ("a1", GetStringForVisibleButtons());

  model()->SetTitle(bookmark_bar_node->children()[3].get(), u"d1",
                    bookmarks::metrics::BookmarkEditSource::kUser);

  // Make the second button visible, changes the title of the first to something
  // really long and make sure the second button hides.
  SizeUntilButtonsVisible(2);
  EXPECT_EQ("a1 b1", GetStringForVisibleButtons());
  model()->SetTitle(bookmark_bar_node->children()[0].get(),
                    u"a_really_long_title",
                    bookmarks::metrics::BookmarkEditSource::kUser);
  EXPECT_LE(1u, test_helper_->GetBookmarkButtonCount());

  // Change the title back and make sure the 2nd button is visible again. Don't
  // use GetStringForVisibleButtons() here as more buttons may have been
  // created.
  model()->SetTitle(bookmark_bar_node->children()[0].get(), u"a1",
                    bookmarks::metrics::BookmarkEditSource::kUser);
  ASSERT_LE(2u, test_helper_->GetBookmarkButtonCount());
  EXPECT_TRUE(test_helper_->GetBookmarkButton(0)->GetVisible());
  EXPECT_TRUE(test_helper_->GetBookmarkButton(1)->GetVisible());

  bookmark_bar_view()->SetBounds(0, 0, 5000,
                                 bookmark_bar_view()->bounds().height());
  views::test::RunScheduledLayout(bookmark_bar_view());
  EXPECT_EQ("a1 b1 c d1 e f1", GetStringForVisibleButtons());
}

TEST_F(BookmarkBarViewTest, DropCallbackTest) {
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  SizeUntilButtonsVisible(7);
  EXPECT_EQ(6u, test_helper_->GetBookmarkButtonCount());

  gfx::Point bar_loc;
  views::View::ConvertPointToScreen(bookmark_bar_view(), &bar_loc);
  ui::OSExchangeData drop_data;
  drop_data.SetURL(GURL("http://www.chromium.org/"), std::u16string(u"z"));
  ui::DropTargetEvent target_event(drop_data, gfx::PointF(bar_loc),
                                   gfx::PointF(bar_loc),
                                   ui::DragDropTypes::DRAG_COPY);
  EXPECT_TRUE(bookmark_bar_view()->CanDrop(drop_data));
  bookmark_bar_view()->OnDragUpdated(target_event);
  auto cb = bookmark_bar_view()->GetDropCallback(target_event);
  EXPECT_EQ("a b c d e f", GetStringForVisibleButtons());

  ui::mojom::DragOperation output_drag_op;
  std::move(cb).Run(target_event, output_drag_op,
                    /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ("z a b c d e f", GetStringForVisibleButtons());
  EXPECT_EQ(output_drag_op, ui::mojom::DragOperation::kCopy);
}

TEST_F(BookmarkBarViewTest, MutateModelDuringDrag) {
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  SizeUntilButtonsVisible(7);
  EXPECT_EQ(6u, test_helper_->GetBookmarkButtonCount());

  gfx::Point drop_loc;
  views::View::ConvertPointToScreen(test_helper_->GetBookmarkButton(5),
                                    &drop_loc);
  ui::OSExchangeData drop_data;
  drop_data.SetURL(GURL("http://www.chromium.org/"), std::u16string(u"z"));
  ui::DropTargetEvent target_event(drop_data, gfx::PointF(drop_loc),
                                   gfx::PointF(drop_loc),
                                   ui::DragDropTypes::DRAG_COPY);
  ASSERT_TRUE(bookmark_bar_view()->CanDrop(drop_data));
  bookmark_bar_view()->OnDragUpdated(target_event);
  EXPECT_NE(-1, test_helper_->GetDropLocationModelIndexForTesting());
  model()->Remove(model()->bookmark_bar_node()->children()[4].get(),
                  bookmarks::metrics::BookmarkEditSource::kOther, FROM_HERE);
  EXPECT_EQ(-1, test_helper_->GetDropLocationModelIndexForTesting());
}

TEST_F(BookmarkBarViewTest, DropCallback_InvalidatePtrTest) {
  SizeUntilButtonsVisible(7);
  EXPECT_EQ(0u, test_helper_->GetBookmarkButtonCount());

  gfx::Point bar_loc;
  views::View::ConvertPointToScreen(bookmark_bar_view(), &bar_loc);
  ui::OSExchangeData drop_data;
  drop_data.SetURL(GURL("http://www.chromium.org/"), std::u16string(u"z"));
  ui::DropTargetEvent target_event(drop_data, gfx::PointF(bar_loc),
                                   gfx::PointF(bar_loc),
                                   ui::DragDropTypes::DRAG_COPY);
  EXPECT_TRUE(bookmark_bar_view()->CanDrop(drop_data));
  bookmark_bar_view()->OnDragUpdated(target_event);
  auto cb = bookmark_bar_view()->GetDropCallback(target_event);

  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  EXPECT_EQ(6u, test_helper_->GetBookmarkButtonCount());

  ui::mojom::DragOperation output_drag_op = ui::mojom::DragOperation::kNone;
  std::move(cb).Run(target_event, output_drag_op,
                    /*drag_image_layer_owner=*/nullptr);
  EXPECT_EQ("a b c d e f", GetStringForVisibleButtons());
  EXPECT_EQ(output_drag_op, ui::mojom::DragOperation::kNone);
}

#if !BUILDFLAG(IS_CHROMEOS)
// Verifies that the apps shortcut is shown or hidden following the policy
// value. This policy (and the apps shortcut) isn't present on ChromeOS.
TEST_F(BookmarkBarViewTest, ManagedShowAppsShortcutInBookmarksBar) {
  // By default, the pref is not managed and the apps shortcut is not shown.
  sync_preferences::TestingPrefServiceSyncable* prefs =
      profile()->GetTestingPrefService();
  EXPECT_FALSE(prefs->IsManagedPreference(
      bookmarks::prefs::kShowAppsShortcutInBookmarkBar));
  EXPECT_FALSE(test_helper_->apps_page_shortcut()->GetVisible());

  // Shows the apps shortcut by policy, via the managed pref.
  prefs->SetManagedPref(bookmarks::prefs::kShowAppsShortcutInBookmarkBar,
                        std::make_unique<base::Value>(true));
  EXPECT_TRUE(test_helper_->apps_page_shortcut()->GetVisible());

  // And try hiding it via policy too.
  prefs->SetManagedPref(bookmarks::prefs::kShowAppsShortcutInBookmarkBar,
                        std::make_unique<base::Value>(false));
  EXPECT_FALSE(test_helper_->apps_page_shortcut()->GetVisible());
}
#endif

// Verifies the SavedTabGroupBar's page navigator is set when the
// bookmarkbarview's page navigator is set.
// TODO(crbug.com/375364962): Flaky on Windows & Linux.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
#define MAYBE_PageNavigatorSet DISABLED_PageNavigatorSet
#else
#define MAYBE_PageNavigatorSet PageNavigatorSet
#endif
TEST_F(BookmarkBarViewTest, MAYBE_PageNavigatorSet) {
  // Expect SavedTabGroupBar to have a page navigator when BookmarkBarView
  // does.
  EXPECT_FALSE(test_helper_->saved_tab_group_bar()->page_navigator());
  bookmark_bar_view()->SetPageNavigator(browser());
  EXPECT_TRUE(test_helper_->saved_tab_group_bar()->page_navigator());

  // Reset both page navigators.
  bookmark_bar_view()->SetPageNavigator(nullptr);

  // Expect we can set the SaveTabGroupBar's page navigator without affecting
  // BookmarkBarView.
  test_helper_->saved_tab_group_bar()->SetPageNavigator(browser());
  EXPECT_TRUE(test_helper_->saved_tab_group_bar()->page_navigator());
}

TEST_F(BookmarkBarViewTest, GetAvailableWidthForSavedTabGroupsBar) {
  // Saved tab group bar and bookmark buttons can both fit.
  ASSERT_EQ(
      100, BookmarkBarView::GetAvailableWidthForSavedTabGroupsBar(60, 30, 100));

  // Cases of saved tab group bar and bookmark buttons cannot both fit below.
  // Prioritize fitting saved tab group since it's smaller than half of the
  // available width.
  ASSERT_EQ(
      100, BookmarkBarView::GetAvailableWidthForSavedTabGroupsBar(30, 80, 100));

  // Prioritize fitting bookmark buttons since it's smaller than half of the
  // available width.
  ASSERT_EQ(
      70, BookmarkBarView::GetAvailableWidthForSavedTabGroupsBar(80, 30, 100));

  // Split the space evenly since neither can fit half of the availablel width.
  ASSERT_EQ(
      50, BookmarkBarView::GetAvailableWidthForSavedTabGroupsBar(80, 60, 100));
}

TEST_F(BookmarkBarViewTest, AccessibleProperties) {
  ui::AXNodeData data;

  bookmark_bar_view()->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(data.role, ax::mojom::Role::kToolbar);
  EXPECT_EQ(data.GetStringAttribute(ax::mojom::StringAttribute::kName),
            l10n_util::GetStringUTF8(IDS_ACCNAME_BOOKMARKS));
}

TEST_F(BookmarkBarViewTest, BookmarkFolderButtonAccessibleProperties) {
  auto* folder_button = test_helper_->managed_bookmarks_button();
  ui::AXNodeData data;

  folder_button->GetViewAccessibility().GetAccessibleNodeData(&data);
  // Role in set by menu button controller.
  EXPECT_EQ(data.role, ax::mojom::Role::kPopUpButton);
  EXPECT_EQ(
      data.GetStringAttribute(ax::mojom::StringAttribute::kRoleDescription),
      l10n_util::GetStringUTF8(
          IDS_ACCNAME_BOOKMARK_FOLDER_BUTTON_ROLE_DESCRIPTION));
}

TEST_F(BookmarkBarViewTest, BookmarkFolderButtonTooltipText) {
  auto* folder_button = test_helper_->managed_bookmarks_button();
  folder_button->SetText(u"Managed Bookmarks");

  EXPECT_EQ(u"Managed Bookmarks",
            folder_button->GetRenderedTooltipText(gfx::Point()));

  folder_button->SetText(std::u16string());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_UNNAMED_BOOKMARK_FOLDER),
            folder_button->GetRenderedTooltipText(gfx::Point()));
}

TEST_F(BookmarkBarViewTest, ButtonSeparatorViewAccessibleProperties) {
  auto* seperator_view = test_helper_->saved_tab_groups_separator_view_();
  ui::AXNodeData data;

  seperator_view->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(data.role, ax::mojom::Role::kSplitter);
  EXPECT_EQ(data.GetStringAttribute(ax::mojom::StringAttribute::kName),
            l10n_util::GetStringUTF8(IDS_ACCNAME_SEPARATOR));
}

TEST_F(BookmarkBarViewInWidgetTest, UpdateTooltipText) {
  widget()->Show();

  bookmarks::test::AddNodesFromModelString(model(),
                                           model()->bookmark_bar_node(), "a b");
  SizeUntilButtonsVisible(1);
  ASSERT_EQ(1u, test_helper_->GetBookmarkButtonCount());

  views::LabelButton* button = test_helper_->GetBookmarkButton(0);
  ASSERT_TRUE(button);
  gfx::Point p;
  EXPECT_EQ(u"a\na.com", button->GetRenderedTooltipText(p));
  button->SetText(u"new title");
  EXPECT_EQ(u"new title\na.com", button->GetRenderedTooltipText(p));
}

// Regression test for https://crbug.com/385805737. When BookmarkButton receives
// an AddedToWidget call, it should also call the corresponding superclass
// method (specifically, `LabelButton::AddedToWidget()` must be called).
TEST_F(BookmarkBarViewInWidgetTest,
       BookmarkButtonAddedToWidgetCallsSuperclass) {
  widget()->ShowInactive();
  widget()->Hide();

  bookmarks::test::AddNodesFromModelString(model(),
                                           model()->bookmark_bar_node(), "a b");
  SizeUntilButtonsVisible(1);

  // `BookmarkButton::AddedToWidget()` will have been called, so ensure that
  // `LabelButton::AddedToWidget()` has been called as well.
  ASSERT_EQ(1u, test_helper_->GetBookmarkButtonCount());
  views::LabelButton* button = test_helper_->GetBookmarkButton(0);
  ASSERT_TRUE(button);
  // The `LabelButton::AddedToWidget()` call only has an effect for bookmark
  // buttons on certain platforms, so gate the check.
  if constexpr (views::PlatformStyle::kInactiveWidgetControlsAppearDisabled) {
    EXPECT_TRUE(button->has_paint_as_active_subscription_for_testing());
  }
}

// TODO(crbug.com/375364962): Flaky on Windows & Linux.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
#define MAYBE_AccessibleRoleDescription DISABLED_AccessibleRoleDescription
#else
#define MAYBE_AccessibleRoleDescription AccessibleRoleDescription
#endif
TEST_F(BookmarkBarViewTest, MAYBE_AccessibleRoleDescription) {
  AddNodesToBookmarkBarFromModelString("a b c d e f ");
  SizeUntilButtonsVisible(1);
  views::LabelButton* button = test_helper_->GetBookmarkButton(0);

  ui::AXNodeData data;
  button->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(
      data.GetStringAttribute(ax::mojom::StringAttribute::kRoleDescription),
      l10n_util::GetStringUTF8(IDS_ACCNAME_BOOKMARK_BUTTON_ROLE_DESCRIPTION));
}

// This mock is used for method call counting. It redirects the call to the real
// implementation.
class BookmarkBarViewWithCounter : public BookmarkBarView {
 public:
  explicit BookmarkBarViewWithCounter(Browser* browser)
      : BookmarkBarView(browser, nullptr) {}

  size_t GetSchedulePaintCount() const { return schedule_paint_count_; }

 protected:
  void OnDidSchedulePaint(const gfx::Rect& r) override {
    BookmarkBarView::OnDidSchedulePaint(r);
    ++schedule_paint_count_;
  }

  size_t schedule_paint_count_ = 0;
};

// Test implementation using `BookmarkBarViewWithCounter`.
class BookmarkBarViewWithCounterTest : public BookmarkBarViewBaseTest {
 public:
  // BookmarkBarViewBaseTest
  void SetUp() override {
    BookmarkBarViewBaseTest::SetUp();

    WaitForBookmarkModelToLoad();
    bookmark_bar_view_with_counter_ =
        std::make_unique<BookmarkBarViewWithCounter>(browser());
  }

  BookmarkBarView* bookmark_bar_view() override {
    return bookmark_bar_view_with_counter_.get();
  }
  BookmarkBarViewWithCounter* bookmark_bar_view_with_counter() {
    return static_cast<BookmarkBarViewWithCounter*>(bookmark_bar_view());
  }

 private:
  std::unique_ptr<BookmarkBarViewWithCounter> bookmark_bar_view_with_counter_;
};

TEST_F(BookmarkBarViewWithCounterTest, PaintCountWithIndividualOperations) {
  ASSERT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 0u);

  const bookmarks::BookmarkNode* bookmark_bar = model()->bookmark_bar_node();
  model()->AddFolder(bookmark_bar, 0, u"f1");
  ASSERT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 1u);

  model()->AddFolder(bookmark_bar, 0, u"f2");
  ASSERT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 2u);

  const bookmarks::BookmarkNode* f3 =
      model()->AddFolder(bookmark_bar, 0, u"f3");
  ASSERT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 3u);

  const bookmarks::BookmarkNode* ff3 = model()->AddFolder(f3, 0, u"ff3");
  model()->Move(ff3, bookmark_bar, 0);
  ASSERT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 4u);
}

TEST_F(BookmarkBarViewWithCounterTest,
       PaintCountWithExtensiveChangesOperations) {
  ASSERT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 0u);

  model()->BeginExtensiveChanges();
  const bookmarks::BookmarkNode* bookmark_bar = model()->bookmark_bar_node();
  model()->AddFolder(bookmark_bar, 0, u"f1");
  model()->AddFolder(bookmark_bar, 0, u"f2");
  const bookmarks::BookmarkNode* f3 =
      model()->AddFolder(bookmark_bar, 0, u"f3");
  const bookmarks::BookmarkNode* ff3 = model()->AddFolder(f3, 0, u"ff3");
  model()->Move(ff3, bookmark_bar, 0);
  EXPECT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 0u);

  model()->EndExtensiveChanges();
  EXPECT_EQ(bookmark_bar_view_with_counter()->GetSchedulePaintCount(), 1u);
}

}  // namespace
