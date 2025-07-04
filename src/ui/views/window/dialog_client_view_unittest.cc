// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/window/dialog_client_view.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/gesture_event_details.h"
#include "ui/events/pointer_details.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/metrics.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/test_layout_provider.h"
#include "ui/views/test/test_views.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/views_features.h"
#include "ui/views/widget/unique_widget_ptr.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

using ui::mojom::DialogButton;

namespace views {

class DialogClientViewTest;

class DialogClientViewTestDelegate : public DialogDelegateView {
 public:
  explicit DialogClientViewTestDelegate(DialogClientViewTest* parent);

  // DialogDelegateView:
  gfx::Size CalculatePreferredSize(
      const SizeBounds& available_size) const override;
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;

 private:
  const raw_ptr<DialogClientViewTest> parent_;
};

// Base class for tests. Also acts as the dialog delegate and contents view for
// TestDialogClientView.
class DialogClientViewTest : public test::WidgetTest {
 public:
  DialogClientViewTest()
      : test::WidgetTest(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  DialogClientViewTest(const DialogClientViewTest&) = delete;
  DialogClientViewTest& operator=(const DialogClientViewTest&) = delete;

  // testing::Test:
  void SetUp() override {
    WidgetTest::SetUp();

    delegate_ = new DialogClientViewTestDelegate(this);
    delegate_->set_use_custom_frame(false);
    delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));

    // Note: not using DialogDelegate::CreateDialogWidget(..), since that can
    // alter the frame type according to the platform.
    widget_ = new Widget;
    Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_WINDOW);
    params.delegate = delegate_;
    widget_->Init(std::move(params));
    layout_provider_ = std::make_unique<test::TestLayoutProvider>();
    layout_provider_->SetDistanceMetric(DISTANCE_BUTTON_MAX_LINKABLE_WIDTH,
                                        200);
  }

  void TearDown() override {
    delegate_ = nullptr;
    widget_.ExtractAsDangling()->CloseNow();
    WidgetTest::TearDown();
  }

  gfx::Size preferred_size() const { return preferred_size_; }
  gfx::Size min_size() const { return min_size_; }
  gfx::Size max_size() const { return max_size_; }

 protected:
  gfx::Rect GetUpdatedClientBounds() {
    SizeAndLayoutWidget();
    return client_view()->bounds();
  }

  void SizeAndLayoutWidget() {
    Widget* dialog = widget();
    dialog->SetSize(dialog->GetContentsView()->GetPreferredSize({}));
    views::test::RunScheduledLayout(dialog);
  }

  // Makes sure that the content view is sized correctly. Width must be at least
  // the requested amount, but height should always match exactly.
  void CheckContentsIsSetToPreferredSize() {
    const gfx::Rect client_bounds = GetUpdatedClientBounds();
    const gfx::Size preferred_size = delegate_->GetPreferredSize({});
    EXPECT_EQ(preferred_size.height(), delegate_->bounds().height());
    EXPECT_LE(preferred_size.width(), delegate_->bounds().width());
    EXPECT_EQ(gfx::Point(), delegate_->origin());
    EXPECT_EQ(client_bounds.width(), delegate_->width());
  }

  // Sets the buttons to show in the dialog and refreshes the dialog.
  void SetDialogButtons(int dialog_buttons) {
    delegate_->SetButtons(dialog_buttons);
    delegate_->DialogModelChanged();
  }

  void SetDialogButtonLabel(ui::mojom::DialogButton button,
                            const std::u16string& label) {
    delegate_->SetButtonLabel(button, label);
    delegate_->DialogModelChanged();
  }

  // Sets the view to provide to DisownExtraView() and updates the dialog. This
  // can only be called a single time because DialogClientView caches the result
  // of DisownExtraView() and never calls it again.
  template <typename T>
  T* SetExtraView(std::unique_ptr<T> view) {
    T* passed_view = delegate_->SetExtraView(std::move(view));
    delegate_->DialogModelChanged();
    return passed_view;
  }

  void SetFixedWidth(int width) {
    delegate_->set_fixed_width(width);
    delegate_->DialogModelChanged();
  }

  void SetSizeConstraints(const gfx::Size& min_size,
                          const gfx::Size& preferred_size,
                          const gfx::Size& max_size) {
    min_size_ = min_size;
    preferred_size_ = preferred_size;
    max_size_ = max_size;
  }

  void SetAllowVerticalButtons(bool allow) {
    delegate_->set_allow_vertical_buttons(allow);
    delegate_->DialogModelChanged();
  }

  void SetThreeWideButtonConfiguration() {
    // Ensure the wide button label will be wider than fixed dialog width.
    constexpr int kFixedWidth = 100;
    const std::u16string kLongLabel(kFixedWidth, 'a');

    SetAllowVerticalButtons(true);
    SetFixedWidth(kFixedWidth);
    SetDialogButtons(static_cast<int>(DialogButton::kCancel) |
                     static_cast<int>(DialogButton::kOk));
    SetExtraView(
        std::make_unique<LabelButton>(Button::PressedCallback(), u"extra"));
    SetDialogButtonLabel(ui::mojom::DialogButton::kOk, kLongLabel);
  }

  View* FocusableViewAfter(View* view) {
    const bool dont_loop = false;
    const bool reverse = false;
    return delegate_->GetFocusManager()->GetNextFocusableView(
        view, delegate_->GetWidget(), reverse, dont_loop);
  }

  // Set a longer than normal Cancel label so that the minimum button width is
  // exceeded. The resulting width is around 160 pixels, but depends on system
  // fonts.
  void SetLongCancelLabel() {
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kCancel,
                              u"Cancel Cancel Cancel");
    delegate_->DialogModelChanged();
  }

  DialogClientView* client_view() {
    return static_cast<DialogClientView*>(widget_->client_view());
  }

  DialogDelegateView* delegate() { return delegate_; }

  Widget* widget() { return widget_; }
  test::TestLayoutProvider* layout_provider() { return layout_provider_.get(); }

 private:
  // The dialog Widget.
  std::unique_ptr<test::TestLayoutProvider> layout_provider_;
  raw_ptr<Widget> widget_ = nullptr;
  raw_ptr<DialogDelegateView> delegate_ = nullptr;

  gfx::Size preferred_size_;
  gfx::Size min_size_;
  gfx::Size max_size_;
};

DialogClientViewTestDelegate::DialogClientViewTestDelegate(
    DialogClientViewTest* parent)
    : parent_(parent) {}

gfx::Size DialogClientViewTestDelegate::CalculatePreferredSize(
    const SizeBounds& available_size) const {
  return parent_->preferred_size();
}

gfx::Size DialogClientViewTestDelegate::GetMinimumSize() const {
  return parent_->min_size();
}

gfx::Size DialogClientViewTestDelegate::GetMaximumSize() const {
  return parent_->max_size();
}

TEST_F(DialogClientViewTest, UpdateButtons) {
  // Make sure this test runs on all platforms. Mac doesn't allow 0 size
  // windows. Test only makes sure the size changes based on whether the buttons
  // exist or not. The initial size should not matter.
  SetSizeConstraints(gfx::Size(200, 100), gfx::Size(300, 200),
                     gfx::Size(400, 300));
  // This dialog should start with no buttons.
  EXPECT_EQ(delegate()->buttons(),
            static_cast<int>(ui::mojom::DialogButton::kNone));
  EXPECT_EQ(nullptr, client_view()->ok_button());
  EXPECT_EQ(nullptr, client_view()->cancel_button());
  const int height_without_buttons = GetUpdatedClientBounds().height();

  // Update to use both buttons.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  EXPECT_TRUE(client_view()->ok_button()->GetIsDefault());
  EXPECT_FALSE(client_view()->cancel_button()->GetIsDefault());
  const int height_with_buttons = GetUpdatedClientBounds().height();
  EXPECT_GT(height_with_buttons, height_without_buttons);

  // Remove the dialog buttons.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  EXPECT_EQ(nullptr, client_view()->ok_button());
  EXPECT_EQ(nullptr, client_view()->cancel_button());
  EXPECT_EQ(GetUpdatedClientBounds().height(), height_without_buttons);

  // Reset with just an ok button.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  EXPECT_TRUE(client_view()->ok_button()->GetIsDefault());
  EXPECT_EQ(nullptr, client_view()->cancel_button());
  EXPECT_EQ(GetUpdatedClientBounds().height(), height_with_buttons);

  // Reset with just a cancel button.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  EXPECT_EQ(nullptr, client_view()->ok_button());
  EXPECT_EQ(client_view()->cancel_button()->GetIsDefault(),
            PlatformStyle::kDialogDefaultButtonCanBeCancel);
  EXPECT_EQ(GetUpdatedClientBounds().height(), height_with_buttons);
}

TEST_F(DialogClientViewTest, RemoveAndUpdateButtons) {
  // Removing buttons from another context should clear the local pointer.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  delete client_view()->ok_button();
  EXPECT_EQ(nullptr, client_view()->ok_button());
  delete client_view()->cancel_button();
  EXPECT_EQ(nullptr, client_view()->cancel_button());

  // Updating should restore the requested buttons properly.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  EXPECT_TRUE(client_view()->ok_button()->GetIsDefault());
  EXPECT_FALSE(client_view()->cancel_button()->GetIsDefault());
}

// Test that views inside the dialog client view have the correct focus order.
TEST_F(DialogClientViewTest, SetupFocusChain) {
  delegate()->GetContentsView()->SetFocusBehavior(View::FocusBehavior::ALWAYS);
  // Initially the dialog client view only contains the content view.
  EXPECT_EQ(delegate()->GetContentsView(),
            FocusableViewAfter(delegate()->GetContentsView()));

  // Add OK and cancel buttons.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));

  if constexpr (PlatformStyle::kIsOkButtonLeading) {
    EXPECT_EQ(client_view()->ok_button(),
              FocusableViewAfter(delegate()->GetContentsView()));
    EXPECT_EQ(client_view()->cancel_button(),
              FocusableViewAfter(client_view()->ok_button()));
    EXPECT_EQ(delegate()->GetContentsView(),
              FocusableViewAfter(client_view()->cancel_button()));
  } else {
    EXPECT_EQ(client_view()->cancel_button(),
              FocusableViewAfter(delegate()->GetContentsView()));
    EXPECT_EQ(client_view()->ok_button(),
              FocusableViewAfter(client_view()->cancel_button()));
    EXPECT_EQ(delegate()->GetContentsView(),
              FocusableViewAfter(client_view()->ok_button()));
  }

  // Add extra view and remove OK button.
  View* extra_view =
      SetExtraView(std::make_unique<StaticSizedView>(gfx::Size(200, 200)));
  extra_view->SetFocusBehavior(View::FocusBehavior::ALWAYS);
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));

  EXPECT_EQ(extra_view, FocusableViewAfter(delegate()->GetContentsView()));
  EXPECT_EQ(client_view()->cancel_button(), FocusableViewAfter(extra_view));
  EXPECT_EQ(delegate()->GetContentsView(), FocusableViewAfter(client_view()));

  // Add a dummy view to the contents view. Consult the FocusManager for the
  // traversal order since it now spans different levels of the view hierarchy.
  View* dummy_view = new StaticSizedView(gfx::Size(200, 200));
  dummy_view->SetFocusBehavior(View::FocusBehavior::ALWAYS);
  delegate()->GetContentsView()->SetFocusBehavior(View::FocusBehavior::NEVER);
  delegate()->GetContentsView()->AddChildViewRaw(dummy_view);
  EXPECT_EQ(dummy_view, FocusableViewAfter(client_view()->cancel_button()));
  EXPECT_EQ(extra_view, FocusableViewAfter(dummy_view));
  EXPECT_EQ(client_view()->cancel_button(), FocusableViewAfter(extra_view));

  // Views are added to the contents view, not the client view, so the focus
  // chain within the client view is not affected.
  // NOTE: The TableLayout requires a view to be in every cell. "Dummy" non-
  // focusable views are inserted to satisfy this requirement.
  EXPECT_TRUE(!client_view()->cancel_button()->GetNextFocusableView() ||
              client_view()
                      ->cancel_button()
                      ->GetNextFocusableView()
                      ->GetFocusBehavior() == View::FocusBehavior::NEVER);
}

// Test that the contents view gets its preferred size in the basic dialog
// configuration.
TEST_F(DialogClientViewTest, ContentsSize) {
  // On Mac the size cannot be 0, so we give it a preferred size.
  SetSizeConstraints(gfx::Size(200, 100), gfx::Size(300, 200),
                     gfx::Size(400, 300));
  CheckContentsIsSetToPreferredSize();
  EXPECT_EQ(delegate()->GetContentsView()->size(), client_view()->size());
  EXPECT_EQ(gfx::Size(300, 200), client_view()->size());
}

// Test the effect of the button strip on layout.
TEST_F(DialogClientViewTest, LayoutWithButtons) {
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  CheckContentsIsSetToPreferredSize();

  EXPECT_LT(delegate()->GetContentsView()->bounds().bottom(),
            client_view()->bounds().bottom());
  const gfx::Size no_extra_view_size = client_view()->bounds().size();

  View* extra_view =
      SetExtraView(std::make_unique<StaticSizedView>(gfx::Size(200, 200)));
  CheckContentsIsSetToPreferredSize();
  EXPECT_GT(client_view()->bounds().height(), no_extra_view_size.height());

  // The dialog is bigger with the extra view than without it.
  const gfx::Size with_extra_view_size = client_view()->size();
  EXPECT_NE(no_extra_view_size, with_extra_view_size);

  // Hiding the extra view removes it.
  extra_view->SetVisible(false);
  CheckContentsIsSetToPreferredSize();
  EXPECT_EQ(no_extra_view_size, client_view()->size());

  // Making it visible again adds it back.
  extra_view->SetVisible(true);
  CheckContentsIsSetToPreferredSize();
  EXPECT_EQ(with_extra_view_size, client_view()->size());

  // Leave |extra_view| hidden. It should still have a parent, to ensure it is
  // owned by a View hierarchy and gets deleted.
  extra_view->SetVisible(false);
  EXPECT_TRUE(extra_view->parent());
}

// Ensure the minimum, maximum and preferred sizes of the contents view are
// respected by the client view, and that the client view includes the button
// row in its minimum and preferred size calculations.
TEST_F(DialogClientViewTest, MinMaxPreferredSize) {
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  const gfx::Size buttons_size = client_view()->GetPreferredSize({});
  EXPECT_FALSE(buttons_size.IsEmpty());

  // When the contents view has no preference, just fit the buttons. The
  // maximum size should be unconstrained in both directions.
  EXPECT_EQ(buttons_size, client_view()->GetMinimumSize());
  EXPECT_EQ(gfx::Size(), client_view()->GetMaximumSize());

  // Ensure buttons are between these widths, for the constants below.
  EXPECT_LT(20, buttons_size.width());
  EXPECT_GT(300, buttons_size.width());

  // With no buttons, client view should match the contents view.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetSizeConstraints(gfx::Size(10, 15), gfx::Size(20, 25), gfx::Size(300, 350));
  EXPECT_EQ(gfx::Size(10, 15), client_view()->GetMinimumSize());
  EXPECT_EQ(gfx::Size(20, 25), client_view()->GetPreferredSize({}));
  EXPECT_EQ(gfx::Size(300, 350), client_view()->GetMaximumSize());

  // With buttons, size should increase vertically only.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  EXPECT_EQ(gfx::Size(buttons_size.width(), 15 + buttons_size.height()),
            client_view()->GetMinimumSize());
  EXPECT_EQ(gfx::Size(buttons_size.width(), 25 + buttons_size.height()),
            client_view()->GetPreferredSize({}));
  EXPECT_EQ(gfx::Size(300, 350 + buttons_size.height()),
            client_view()->GetMaximumSize());

  // If the contents view gets bigger, it should take over the width.
  SetSizeConstraints(gfx::Size(400, 450), gfx::Size(500, 550),
                     gfx::Size(600, 650));
  EXPECT_EQ(gfx::Size(400, 450 + buttons_size.height()),
            client_view()->GetMinimumSize());
  EXPECT_EQ(gfx::Size(500, 550 + buttons_size.height()),
            client_view()->GetPreferredSize({}));
  EXPECT_EQ(gfx::Size(600, 650 + buttons_size.height()),
            client_view()->GetMaximumSize());
}

// Ensure button widths are linked under MD.
TEST_F(DialogClientViewTest, LinkedWidthDoesLink) {
  SetLongCancelLabel();

  // Ensure there is no default button since getting a bold font can throw off
  // the cached sizes.
  delegate()->SetDefaultButton(
      static_cast<int>(ui::mojom::DialogButton::kNone));

  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  CheckContentsIsSetToPreferredSize();
  const int ok_button_only_width = client_view()->ok_button()->width();

  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  CheckContentsIsSetToPreferredSize();
  const int cancel_button_width = client_view()->cancel_button()->width();
  EXPECT_LT(cancel_button_width, 200);

  // Ensure the single buttons have different preferred widths when alone, and
  // that the Cancel button is bigger (so that it dominates the size).
  EXPECT_GT(cancel_button_width, ok_button_only_width);

  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  CheckContentsIsSetToPreferredSize();

  // Cancel button shouldn't have changed widths.
  EXPECT_EQ(cancel_button_width, client_view()->cancel_button()->width());

  // OK button should now match the bigger, cancel button.
  EXPECT_EQ(cancel_button_width, client_view()->ok_button()->width());

  // But not when the size of the cancel button exceeds the max linkable width.
  layout_provider()->SetDistanceMetric(DISTANCE_BUTTON_MAX_LINKABLE_WIDTH, 100);
  EXPECT_GT(cancel_button_width, 100);

  delegate()->DialogModelChanged();
  CheckContentsIsSetToPreferredSize();
  EXPECT_EQ(ok_button_only_width, client_view()->ok_button()->width());
  layout_provider()->SetDistanceMetric(DISTANCE_BUTTON_MAX_LINKABLE_WIDTH, 200);

  // The extra view should also match, if it's a matching button type.
  View* extra_button = SetExtraView(std::make_unique<LabelButton>(
      Button::PressedCallback(), std::u16string()));
  CheckContentsIsSetToPreferredSize();
  EXPECT_EQ(cancel_button_width, extra_button->width());
}

TEST_F(DialogClientViewTest, LinkedWidthDoesntLink) {
  SetLongCancelLabel();

  // Ensure there is no default button since getting a bold font can throw off
  // the cached sizes.
  delegate()->SetDefaultButton(
      static_cast<int>(ui::mojom::DialogButton::kNone));

  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  CheckContentsIsSetToPreferredSize();
  const int ok_button_only_width = client_view()->ok_button()->width();

  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  CheckContentsIsSetToPreferredSize();
  const int cancel_button_width = client_view()->cancel_button()->width();
  EXPECT_LT(cancel_button_width, 200);

  // Ensure the single buttons have different preferred widths when alone, and
  // that the Cancel button is bigger (so that it dominates the size).
  EXPECT_GT(cancel_button_width, ok_button_only_width);

  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  CheckContentsIsSetToPreferredSize();

  // Cancel button shouldn't have changed widths.
  EXPECT_EQ(cancel_button_width, client_view()->cancel_button()->width());

  // OK button should now match the bigger, cancel button.
  EXPECT_EQ(cancel_button_width, client_view()->ok_button()->width());

  // But not when the size of the cancel button exceeds the max linkable width.
  layout_provider()->SetDistanceMetric(DISTANCE_BUTTON_MAX_LINKABLE_WIDTH, 100);
  EXPECT_GT(cancel_button_width, 100);

  delegate()->DialogModelChanged();
  CheckContentsIsSetToPreferredSize();
  EXPECT_EQ(ok_button_only_width, client_view()->ok_button()->width());
  layout_provider()->SetDistanceMetric(DISTANCE_BUTTON_MAX_LINKABLE_WIDTH, 200);

  // Checkbox extends LabelButton, but it should not participate in linking.
  View* extra_button =
      SetExtraView(std::make_unique<Checkbox>(std::u16string()));
  CheckContentsIsSetToPreferredSize();
  EXPECT_NE(cancel_button_width, extra_button->width());
}

TEST_F(DialogClientViewTest, ButtonPosition) {
  constexpr int button_row_inset = 13;
  client_view()->SetButtonRowInsets(gfx::Insets(button_row_inset));
  constexpr int contents_height = 37;
  constexpr int contents_width = 222;
  SetSizeConstraints(gfx::Size(), gfx::Size(contents_width, contents_height),
                     gfx::Size(666, 666));
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  SizeAndLayoutWidget();
  EXPECT_EQ(contents_width - button_row_inset,
            client_view()->ok_button()->bounds().right());
  EXPECT_EQ(contents_height + button_row_inset,
            delegate()->height() + client_view()->ok_button()->y());
}

// Ensures that the focus of the button remains after a dialog update.
TEST_F(DialogClientViewTest, FocusUpdate) {
  // Test with just an ok button.
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  EXPECT_FALSE(client_view()->ok_button()->HasFocus());
  client_view()->ok_button()->RequestFocus();  // Set focus.
  EXPECT_TRUE(client_view()->ok_button()->HasFocus());
  delegate()->DialogModelChanged();
  EXPECT_TRUE(client_view()->ok_button()->HasFocus());
}

// Ensures that the focus of the button remains after a dialog update that
// contains multiple buttons.
TEST_F(DialogClientViewTest, FocusMultipleButtons) {
  // Test with ok and cancel buttons.
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  EXPECT_FALSE(client_view()->ok_button()->HasFocus());
  EXPECT_FALSE(client_view()->cancel_button()->HasFocus());
  client_view()->cancel_button()->RequestFocus();  // Set focus.
  EXPECT_FALSE(client_view()->ok_button()->HasFocus());
  EXPECT_TRUE(client_view()->cancel_button()->HasFocus());
  delegate()->DialogModelChanged();
  EXPECT_TRUE(client_view()->cancel_button()->HasFocus());
}

// Ensures that the focus persistence works correctly when buttons are removed.
TEST_F(DialogClientViewTest, FocusChangingButtons) {
  // Start with ok and cancel buttons.
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  client_view()->cancel_button()->RequestFocus();  // Set focus.
  FocusManager* focus_manager = delegate()->GetFocusManager();
  EXPECT_EQ(client_view()->cancel_button(), focus_manager->GetFocusedView());

  // Remove buttons.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  EXPECT_EQ(nullptr, focus_manager->GetFocusedView());
}

// Ensures that clicks are ignored for short time after view has been shown.
TEST_F(DialogClientViewTest, IgnorePossiblyUnintendedClicks_ClickAfterShown) {
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));

  // Should ignore clicks right after the dialog is shown.
  ui::MouseEvent mouse_event(ui::EventType::kMousePressed, gfx::PointF(),
                             gfx::PointF(), ui::EventTimeForNow(), ui::EF_NONE,
                             ui::EF_NONE);
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(mouse_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(mouse_event);
  EXPECT_FALSE(widget()->IsClosed());

  cancel_button.NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::PointF(), gfx::PointF(),
      ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()),
      ui::EF_NONE, ui::EF_NONE));
  EXPECT_TRUE(widget()->IsClosed());
}

// Ensures that key events are not ignored for short time, after view has been
// shown.
TEST_F(DialogClientViewTest, DoesNotIgnoreKeyEvents_ReturnKeyAfterShown) {
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));

  // Should not ignore key events right after the dialog is shown.
  ui::KeyEvent press_enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN,
                           ui::EF_NONE, ui::EventTimeForNow());
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(press_enter);
  EXPECT_TRUE(widget()->IsClosed());
}

// Ensures that taps are ignored for a short time after the view has been shown.
TEST_F(DialogClientViewTest, IgnorePossiblyUnintendedClicks_TapAfterShown) {
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));

  // Should ignore taps right after the dialog is shown.
  ui::GestureEvent tap_event(
      0, 0, 0, ui::EventTimeForNow(),
      ui::GestureEventDetails(ui::EventType::kGestureTap));
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(tap_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(tap_event);
  EXPECT_FALSE(widget()->IsClosed());

  ui::GestureEvent tap_event2(
      0, 0, 0,
      ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()),
      ui::GestureEventDetails(ui::EventType::kGestureTap));
  cancel_button.NotifyClick(tap_event2);
  EXPECT_TRUE(widget()->IsClosed());
}

// Ensures that touch events are ignored for a short time after the view has
// been shown.
TEST_F(DialogClientViewTest, IgnorePossiblyUnintendedClicks_TouchAfterShown) {
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));

  // Should ignore touches right after the dialog is shown.
  ui::TouchEvent touch_event(ui::EventType::kTouchPressed, gfx::PointF(),
                             gfx::PointF(), ui::EventTimeForNow(),
                             ui::PointerDetails(ui::EventPointerType::kTouch));
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(touch_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(touch_event);
  EXPECT_FALSE(widget()->IsClosed());

  ui::TouchEvent touch_event2(
      ui::EventType::kTouchPressed, gfx::PointF(), gfx::PointF(),
      ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()),
      ui::PointerDetails(ui::EventPointerType::kTouch));
  cancel_button.NotifyClick(touch_event2);
  EXPECT_TRUE(widget()->IsClosed());
}

// TODO(crbug.com/40269697): investigate the tests on ChromeOS and
// fuchsia
#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_FUCHSIA)
class DesktopDialogClientViewTest : public DialogClientViewTest {
 public:
  void SetUp() override {
    set_native_widget_type(NativeWidgetType::kDesktop);
    DialogClientViewTest::SetUp();
  }
};

// Ensures that unintended clicks are protected properly when a root window's
// bound has been changed.
TEST_F(DesktopDialogClientViewTest,
       IgnorePossiblyUnintendedClicks_TopLevelWindowBoundsChanged) {
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  SizeAndLayoutWidget();
  widget()->Show();
  task_environment()->FastForwardBy(
      base::Milliseconds(GetDoubleClickInterval() * 2));

  // Create another widget on top, change window's bounds, click event to the
  // old widget should be ignored.
  auto* widget1 = CreateTopLevelNativeWidget();
  widget1->SetBounds(gfx::Rect(50, 50, 100, 100));
  ui::MouseEvent mouse_event(ui::EventType::kMousePressed, gfx::Point(),
                             gfx::Point(), ui::EventTimeForNow(), ui::EF_NONE,
                             ui::EF_NONE);
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(mouse_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(mouse_event);
  EXPECT_FALSE(widget()->IsClosed());

  cancel_button.NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()),
      ui::EF_NONE, ui::EF_NONE));
  EXPECT_TRUE(widget()->IsClosed());
  widget1->CloseNow();
}

// Ensures that unintended clicks are protected properly when a root window has
// been closed.
TEST_F(DesktopDialogClientViewTest,
       IgnorePossiblyUnintendedClicks_CloseRootWindow) {
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  SizeAndLayoutWidget();
  widget()->Show();
  task_environment()->FastForwardBy(
      base::Milliseconds(GetDoubleClickInterval() * 2));

  // Create another widget on top, close the top window, click event to the old
  // widget should be ignored.
  auto* widget1 = CreateTopLevelNativeWidget();
  widget1->CloseNow();
  ui::MouseEvent mouse_event(ui::EventType::kMousePressed, gfx::Point(),
                             gfx::Point(), ui::EventTimeForNow(), ui::EF_NONE,
                             ui::EF_NONE);
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(mouse_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(mouse_event);
  EXPECT_FALSE(widget()->IsClosed());

  cancel_button.NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()),
      ui::EF_NONE, ui::EF_NONE));
  EXPECT_TRUE(widget()->IsClosed());
}
#endif  // !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(ENABLE_DESKTOP_AURA)
TEST_F(DialogClientViewTest,
       IgnorePossiblyUnintendedClicks_ClickAfterClosingTooltip) {
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  SizeAndLayoutWidget();
  widget()->Show();
  task_environment()->FastForwardBy(
      base::Milliseconds(GetDoubleClickInterval() * 2));

  UniqueWidgetPtr widget1(std::make_unique<Widget>());
  Widget::InitParams params = CreateParams(Widget::InitParams::TYPE_TOOLTIP);
  widget1->Init(std::move(params));
  widget1->CloseNow();
  ui::MouseEvent mouse_event(ui::EventType::kMousePressed, gfx::Point(),
                             gfx::Point(), ui::EventTimeForNow(), ui::EF_NONE,
                             ui::EF_NONE);
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(mouse_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(mouse_event);
  EXPECT_TRUE(widget()->IsClosed());
}
#endif  // BUILDFLAG(ENABLE_DESKTOP_AURA)

// Ensures that repeated clicks with short intervals after view has been shown
// are also ignored.
TEST_F(DialogClientViewTest, IgnorePossiblyUnintendedClicks_RepeatedClicks) {
  widget()->Show();
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));

  const base::TimeTicks kNow = ui::EventTimeForNow();
  const base::TimeDelta kShortClickInterval =
      base::Milliseconds(GetDoubleClickInterval());

  // Should ignore clicks right after the dialog is shown.
  ui::MouseEvent mouse_event(ui::EventType::kMousePressed, gfx::Point(),
                             gfx::Point(), kNow, ui::EF_NONE, ui::EF_NONE);
  test::ButtonTestApi(client_view()->ok_button()).NotifyClick(mouse_event);
  test::ButtonTestApi cancel_button(client_view()->cancel_button());
  cancel_button.NotifyClick(mouse_event);
  EXPECT_FALSE(widget()->IsClosed());

  // Should ignore repeated clicks with short intervals, even though enough time
  // has passed since the dialog was shown.
  const base::TimeDelta kRepeatedClickInterval = kShortClickInterval / 2;
  const size_t kNumClicks = 4;
  ASSERT_TRUE(kNumClicks * kRepeatedClickInterval > kShortClickInterval);
  base::TimeTicks event_time = kNow;
  for (size_t i = 0; i < kNumClicks; i++) {
    cancel_button.NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       event_time, ui::EF_NONE, ui::EF_NONE));
    EXPECT_FALSE(widget()->IsClosed());
    event_time += kRepeatedClickInterval;
  }

  // Sufficient time passed, events are now allowed.
  event_time += kShortClickInterval;
  cancel_button.NotifyClick(
      ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                     event_time, ui::EF_NONE, ui::EF_NONE));
  EXPECT_TRUE(widget()->IsClosed());
}

TEST_F(DialogClientViewTest, ButtonLayoutWithExtra) {
  // The dialog button row's layout should look like:
  // | <inset> [extra] <flex-margin> [cancel] <margin> [ok] <inset> |
  // Where:
  // 1) The two insets are linkable
  // 2) The ok & cancel buttons have their width linked
  // 3) The extra button has its width linked to the other two
  // 4) The margin should be invariant as the dialog changes width
  // 5) The flex margin should change as the dialog changes width
  //
  // Note that cancel & ok may swap order depending on
  // PlatformStyle::kIsOkButtonLeading; these invariants hold for either order.
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                   static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetDialogButtonLabel(ui::mojom::DialogButton::kOk, u"ok");
  SetDialogButtonLabel(ui::mojom::DialogButton::kCancel, u"cancel");
  SetExtraView(
      std::make_unique<LabelButton>(Button::PressedCallback(), u"extra"));

  widget()->Show();

  View* ok = client_view()->ok_button();
  View* cancel = client_view()->cancel_button();
  View* extra = client_view()->extra_view();

  ASSERT_NE(ok, cancel);
  ASSERT_NE(ok, extra);
  ASSERT_NE(cancel, extra);

  SizeAndLayoutWidget();

  auto bounds_left = [](View* v) { return v->GetBoundsInScreen().x(); };
  auto bounds_right = [](View* v) { return v->GetBoundsInScreen().right(); };

  // (1): left inset == right inset (and they shouldn't be 0):
  int left_inset = bounds_left(extra) - bounds_left(delegate());
  int right_inset = bounds_right(delegate()) -
                    std::max(bounds_right(ok), bounds_right(cancel));
  EXPECT_EQ(left_inset, right_inset);
  EXPECT_GT(left_inset, 0);

  // (2) & (3): All three buttons have their widths linked:
  EXPECT_EQ(ok->width(), cancel->width());
  EXPECT_EQ(ok->width(), extra->width());
  EXPECT_GT(ok->width(), 0);

  // (4): Margin between ok & cancel should be invariant as dialog width
  // changes:
  auto get_margin = [&]() {
    return std::max(bounds_left(ok), bounds_left(cancel)) -
           std::min(bounds_right(ok), bounds_right(cancel));
  };

  // (5): Flex margin between ok/cancel and extra should vary with dialog width
  // (it should absorb 100% of the change in width)
  auto get_flex_margin = [&]() {
    return std::min(bounds_left(ok), bounds_left(cancel)) - bounds_right(extra);
  };

  int old_margin = get_margin();
  int old_flex_margin = get_flex_margin();

  SetSizeConstraints(gfx::Size(), gfx::Size(delegate()->width() + 100, 0),
                     gfx::Size());
  SizeAndLayoutWidget();

  EXPECT_EQ(old_margin, get_margin());
  EXPECT_EQ(old_flex_margin + 100, get_flex_margin());
}

TEST_F(DialogClientViewTest, LayoutWithHiddenExtraView) {
  SetDialogButtons(static_cast<int>(ui::mojom::DialogButton::kCancel) |
                   static_cast<int>(ui::mojom::DialogButton::kOk));
  SetDialogButtonLabel(ui::mojom::DialogButton::kOk, u"ok");
  SetDialogButtonLabel(ui::mojom::DialogButton::kCancel, u"cancel");
  SetExtraView(
      std::make_unique<LabelButton>(Button::PressedCallback(), u"extra"));

  widget()->Show();

  SizeAndLayoutWidget();

  View* ok = client_view()->ok_button();
  View* cancel = client_view()->cancel_button();
  View* extra = client_view()->extra_view();

  int ok_left = ok->bounds().x();
  int cancel_left = cancel->bounds().x();

  extra->SetVisible(false);
  // Re-layout but do not resize the widget. If we resized it without the extra
  // view, it would get narrower and the other buttons would move.
  EXPECT_TRUE(widget()->GetContentsView()->needs_layout());
  views::test::RunScheduledLayout(widget());

  EXPECT_EQ(ok_left, ok->bounds().x());
  EXPECT_EQ(cancel_left, cancel->bounds().x());
}

MATCHER(HasHorizontalButtons, "") {
  const auto ok_bounds = arg->ok_button()->bounds();
  const auto cancel_bounds = arg->cancel_button()->bounds();

  EXPECT_EQ(ok_bounds.CenterPoint().y(), cancel_bounds.CenterPoint().y());

  // Order from the top is always Extra, Cancel, Ok (unlike horizontal
  // platform-specific ordering).
  if (arg->extra_view()) {
    const auto extra_bounds = arg->extra_view()->bounds();
    EXPECT_EQ(ok_bounds.CenterPoint().y(), extra_bounds.CenterPoint().y());
  }

  return true;
}

MATCHER(HasVerticalButtons, "") {
  EXPECT_NE(arg->extra_view(), nullptr);
  if (!arg->extra_view()) {
    return false;
  }

  const auto ok_bounds = arg->ok_button()->bounds();
  const auto cancel_bounds = arg->cancel_button()->bounds();
  const auto extra_bounds = arg->extra_view()->bounds();

  // Buttons should have the same width and be vertically-aligned.
  EXPECT_EQ(ok_bounds.width(), cancel_bounds.width());
  EXPECT_EQ(ok_bounds.width(), extra_bounds.width());
  EXPECT_EQ(ok_bounds.x(), cancel_bounds.x());
  EXPECT_EQ(ok_bounds.x(), extra_bounds.x());

  // Order from the top is always Extra, Cancel, Ok (unlike horizontal
  // platform-specific ordering).
  EXPECT_LT(extra_bounds.y(), cancel_bounds.y());
  EXPECT_LT(cancel_bounds.y(), ok_bounds.y());

  return true;
}

TEST_F(DialogClientViewTest, WideButtonsRenderVertically) {
  SetThreeWideButtonConfiguration();

  widget()->Show();
  SizeAndLayoutWidget();
  EXPECT_THAT(client_view(), HasVerticalButtons());
}

TEST_F(DialogClientViewTest, WideButtonsStayHorizontalIfFeatureDisabled) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      views::features::kDialogVerticalButtonFallback);

  SetThreeWideButtonConfiguration();

  widget()->Show();
  SizeAndLayoutWidget();
  EXPECT_THAT(client_view(), HasHorizontalButtons());
}

TEST_F(DialogClientViewTest, WideButtonsStayHorizontalIfNotFixedWidth) {
  SetThreeWideButtonConfiguration();
  SetFixedWidth(0);

  widget()->Show();
  SizeAndLayoutWidget();
  EXPECT_THAT(client_view(), HasHorizontalButtons());
}

TEST_F(DialogClientViewTest, WideButtonsStayHorizontalIfNoExtraButton) {
  SetThreeWideButtonConfiguration();
  SetExtraView(std::unique_ptr<View>());

  widget()->Show();
  SizeAndLayoutWidget();
  EXPECT_THAT(client_view(), HasHorizontalButtons());
}

TEST_F(DialogClientViewTest, WideButtonsStayHorizontalIfVerticalNotAllowed) {
  SetThreeWideButtonConfiguration();
  SetAllowVerticalButtons(false);

  widget()->Show();
  SizeAndLayoutWidget();
  EXPECT_THAT(client_view(), HasHorizontalButtons());
}

struct IsPossiblyUnintendedInteractionTestCase {
  enum class EventType {
    kKey,
    kMouse,
  };
  std::string test_name;
  EventType event_type;
  bool is_delayed_interaction;
  bool allow_key_events;
  bool is_possibly_unintended_interaction;
};

class InteractionTest : public DialogClientViewTest,
                        public testing::WithParamInterface<
                            IsPossiblyUnintendedInteractionTestCase> {
 public:
  InteractionTest() = default;

  InteractionTest(const InteractionTest&) = delete;
  InteractionTest& operator=(const InteractionTest&) = delete;

  std::unique_ptr<ui::KeyEvent> KeyEventNow() {
    return std::make_unique<ui::KeyEvent>(ui::EventType::kKeyPressed,
                                          ui::VKEY_RETURN, ui::EF_NONE,
                                          ui::EventTimeForNow());
  }

  std::unique_ptr<ui::KeyEvent> KeyEventDelayed() {
    return std::make_unique<ui::KeyEvent>(
        ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE,
        ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()));
  }

  std::unique_ptr<ui::MouseEvent> MouseEventNow() {
    return std::make_unique<ui::MouseEvent>(
        ui::EventType::kMousePressed, gfx::PointF(), gfx::PointF(),
        ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE);
  }

  std::unique_ptr<ui::MouseEvent> MouseEventDelayed() {
    return std::make_unique<ui::MouseEvent>(
        ui::EventType::kMousePressed, gfx::PointF(), gfx::PointF(),
        ui::EventTimeForNow() + base::Milliseconds(GetDoubleClickInterval()),
        ui::EF_NONE, ui::EF_NONE);
  }
};

TEST_P(InteractionTest, IsPossiblyUnintendedInteraction) {
  const IsPossiblyUnintendedInteractionTestCase& test_case = GetParam();

  widget()->Show();

  std::unique_ptr<ui::Event> event;
  switch (test_case.event_type) {
    case IsPossiblyUnintendedInteractionTestCase::EventType::kKey:
      event =
          test_case.is_delayed_interaction ? KeyEventDelayed() : KeyEventNow();
      break;
    case IsPossiblyUnintendedInteractionTestCase::EventType::kMouse:
      event = test_case.is_delayed_interaction ? MouseEventDelayed()
                                               : MouseEventNow();
      break;
  }
  ASSERT_NE(event, nullptr);

  EXPECT_EQ(client_view()->IsPossiblyUnintendedInteraction(
                *event, test_case.allow_key_events),
            test_case.is_possibly_unintended_interaction);
}

INSTANTIATE_TEST_SUITE_P(
    AllInteractions,
    InteractionTest,
    testing::ValuesIn<IsPossiblyUnintendedInteractionTestCase>({
        {
            .test_name = "NotPermissionRelevantKeyEventNow",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kKey,
            .is_delayed_interaction = false,
            .allow_key_events = true,
            .is_possibly_unintended_interaction = false,
        },
        {
            .test_name = "PermissionRelevantKeyEventNow",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kKey,
            .is_delayed_interaction = false,
            .allow_key_events = false,
            .is_possibly_unintended_interaction = true,
        },
        {
            .test_name = "NotPermissionRelevantKeyEventDelayed",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kKey,
            .is_delayed_interaction = true,
            .allow_key_events = true,
            .is_possibly_unintended_interaction = false,
        },
        {
            .test_name = "PermissionRelevantKeyEventDelayed",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kKey,
            .is_delayed_interaction = true,
            .allow_key_events = false,
            .is_possibly_unintended_interaction = false,
        },
        {
            .test_name = "NotPermissionRelevantMouseEventNow",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kMouse,
            .is_delayed_interaction = false,
            .allow_key_events = true,
            .is_possibly_unintended_interaction = true,
        },
        {
            .test_name = "PermissionRelevantMouseEventNow",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kMouse,
            .is_delayed_interaction = false,
            .allow_key_events = false,
            .is_possibly_unintended_interaction = true,
        },
        {
            .test_name = "NotPermissionRelevantMouseEventDelayed",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kMouse,
            .is_delayed_interaction = true,
            .allow_key_events = true,
            .is_possibly_unintended_interaction = false,
        },
        {
            .test_name = "PermissionRelevantMouseEventDelayed",
            .event_type =
                IsPossiblyUnintendedInteractionTestCase::EventType::kMouse,
            .is_delayed_interaction = true,
            .allow_key_events = false,
            .is_possibly_unintended_interaction = false,
        },
    }),
    [](const testing::TestParamInfo<InteractionTest::ParamType>& info) {
      return info.param.test_name;
    });
}  // namespace views
