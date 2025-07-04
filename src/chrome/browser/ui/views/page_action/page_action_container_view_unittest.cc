// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/page_action/page_action_container_view.h"

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view_params.h"
#include "chrome/browser/ui/views/page_action/test_support/test_page_action_properties_provider.h"
#include "components/vector_icons/vector_icons.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/views/test/views_test_base.h"

namespace page_actions {
namespace {

constexpr int kDefaultBetweenIconSpacing = 8;
constexpr int kDefaultIconSize = 16;

static constexpr actions::ActionId kTestPageActionId = kActionZoomNormal;
static const PageActionPropertiesMap kTestProperties = PageActionPropertiesMap{
    {
        kTestPageActionId,
        PageActionProperties{
            .histogram_name = "TestZoom",
            .type = PageActionIconType::kZoom,
        },
    },
};

class MockIconLabelViewDelegate : public IconLabelBubbleView::Delegate {
 public:
  MOCK_METHOD(SkColor,
              GetIconLabelBubbleSurroundingForegroundColor,
              (),
              (const, override));
  MOCK_METHOD(SkColor,
              GetIconLabelBubbleBackgroundColor,
              (),
              (const, override));
};

class PageActionContainerViewTest : public views::ViewsTestBase {
 public:
  PageActionContainerViewTest() {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        features::kPageActionsMigration,
        {{features::kPageActionsMigrationZoom.name, "true"}});
  }

  ~PageActionContainerViewTest() override = default;

  void TearDown() override {
    views::ViewsTestBase::TearDown();
    actions::ActionManager::Get().ResetActions();
  }

  PageActionViewParams DefaultViewParams() {
    return PageActionViewParams{
        .icon_size = kDefaultIconSize,
        .between_icon_spacing = kDefaultBetweenIconSpacing,
        .icon_label_bubble_delegate = &icon_label_view_delegate_};
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  MockIconLabelViewDelegate icon_label_view_delegate_;
};

TEST_F(PageActionContainerViewTest, GetPageActionView) {
  actions::ActionItem* action_item = actions::ActionManager::Get().AddAction(
      actions::ActionItem::Builder()
          .SetImage(ui::ImageModel::FromVectorIcon(vector_icons::kBackArrowIcon,
                                                   ui::kColorSysPrimary,
                                                   /*icon_size=*/16))
          .SetActionId(kTestPageActionId)
          .Build());

  auto page_action_container = std::make_unique<PageActionContainerView>(
      std::vector<actions::ActionItem*>{action_item},
      TestPageActionPropertiesProvider(kTestProperties), DefaultViewParams());

  PageActionView* page_action_view =
      page_action_container->GetPageActionView(kTestPageActionId);
  ASSERT_TRUE(!!page_action_view);
  EXPECT_EQ(kTestPageActionId, page_action_view->GetActionId());

  // Returns null if the action ID is not found.
  static constexpr actions::ActionId kNonExistantPageActionId = 1;
  EXPECT_EQ(nullptr,
            page_action_container->GetPageActionView(kNonExistantPageActionId));
}

}  // namespace
}  // namespace page_actions
