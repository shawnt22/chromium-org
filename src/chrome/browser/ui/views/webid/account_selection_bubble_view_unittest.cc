// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/webid/account_selection_bubble_view.h"

#include <array>
#include <string>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/browser/ui/views/webid/account_selection_view_base.h"
#include "chrome/browser/ui/views/webid/account_selection_view_test_base.h"
#include "chrome/browser/ui/views/webid/fake_delegate.h"
#include "chrome/browser/ui/views/webid/fedcm_account_selection_view_desktop.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/tabs/public/mock_tab_interface.h"
#include "content/public/browser/identity_request_dialog_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/color_parser.h"
#include "content/public/common/content_features.h"
#include "content/public/test/scoped_accessibility_mode_override.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace webid {

using IdentityProviderDataPtr = scoped_refptr<content::IdentityProviderData>;
using IdentityRequestAccountPtr =
    scoped_refptr<content::IdentityRequestAccount>;
using LoginState = content::IdentityRequestAccount::LoginState;

namespace {

constexpr char kAccountId1[] = "account_id1";
constexpr char kAccountSuffix[] = "suffix";
constexpr char16_t kTopFrameEtldPlusOne[] = u"rp-example.com";

class FakeTabInterface : public tabs::MockTabInterface {
 public:
  ~FakeTabInterface() override = default;
  explicit FakeTabInterface(content::WebContents* contents)
      : contents_(contents) {}
  content::WebContents* GetContents() const override { return contents_; }

 private:
  raw_ptr<content::WebContents> contents_;
};

class FakeFedCmAccountSelectionView : public FedCmAccountSelectionView {
 public:
  FakeFedCmAccountSelectionView(AccountSelectionView::Delegate* delegate,
                                tabs::TabInterface* tab,
                                views::View* anchor_view)
      : FedCmAccountSelectionView(delegate, tab), anchor_view_(anchor_view) {
    url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_);
  }

  void OnAccountSelected(const IdentityRequestAccountPtr& account,
                         const ui::Event& event) {
    // Simulate the account selection by calling ShowSingleAccountConfirmDialog
    // directly
    account_selection_view()->ShowSingleAccountConfirmDialog(
        account, /*show_back_button=*/false);
  }

  void ClickAccountHoverButton(webid::AccountHoverButton* account_hover_button,
                               const IdentityRequestAccountPtr& account) {
    ASSERT_NE(account_hover_button, nullptr);

    // Override the callback bound to FedCmAccountSelectionView class
    // with FakeFedCmAccountSelectionView class to test for AccountHoverButton
    // Lifecycle during account selection.
    account_hover_button->SetCallbackForTesting(
        base::BindRepeating(&FakeFedCmAccountSelectionView::OnAccountSelected,
                            base::Unretained(this), account));

    // Create mouse event
    ui::MouseEvent event(ui::EventType::kMousePressed, gfx::Point(),
                         gfx::Point(), base::TimeTicks(),
                         ui::EF_LEFT_MOUSE_BUTTON, 0);

    account_hover_button->OnPressed(event);
  }

 private:
  scoped_refptr<network::SharedURLLoaderFactory> GetURLLoaderFactory()
      override {
    return url_loader_factory_;
  }

  views::View* GetAnchorView() override { return anchor_view_; }

  network::TestURLLoaderFactory test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  raw_ptr<views::View> anchor_view_;
};

}  // namespace

class AccountSelectionBubbleViewTest : public ChromeViewsTestBase,
                                       public AccountSelectionViewTestBase {
 public:
  AccountSelectionBubbleViewTest() {
    content::IdentityProviderMetadata idp_metadata;
    // Set the brand icon so it is laid out in the tests.
    idp_metadata.brand_icon_url = GURL(kIdpBrandIconUrl);
    idp_metadata.brand_decoded_icon =
        gfx::Image::CreateFrom1xBitmap(gfx::test::CreateBitmap(1));
    idp_data_ = base::MakeRefCounted<content::IdentityProviderData>(
        kIdpForDisplay, idp_metadata, CreateTestClientMetadata(),
        blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
        kDefaultDisclosureFields,
        /*has_login_status_mismatch=*/false);
    accounts_ = {CreateAccount(idp_data_)};
  }

 protected:
  IdentityRequestAccountPtr CreateAccount(
      IdentityProviderDataPtr idp,
      LoginState idp_claimed_login_state = LoginState::kSignUp,
      LoginState browser_trusted_login_state = LoginState::kSignUp,
      std::string account_id = kAccountId1) {
    IdentityRequestAccountPtr account = base::MakeRefCounted<Account>(
        account_id, "", "", "", "", "", GURL(), "", "",
        /*login_hints=*/std::vector<std::string>(),
        /*domain_hints=*/std::vector<std::string>(),
        /*labels=*/std::vector<std::string>(),
        /*login_state=*/idp_claimed_login_state,
        /*browser_trusted_login_state=*/browser_trusted_login_state);
    if (idp_claimed_login_state == LoginState::kSignUp) {
      account->fields = idp->disclosure_fields;
    }
    account->identity_provider = std::move(idp);
    return account;
  }

  void CreateAccountSelectionBubble(
      const std::u16string& iframe_for_display = u"") {
    Reset();
    views::Widget::InitParams params =
        CreateParams(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
                     views::Widget::InitParams::TYPE_WINDOW);

    anchor_widget_ = std::make_unique<views::Widget>();
    anchor_widget_->Init(std::move(params));
    anchor_widget_->Show();

    tab_interface_ =
        std::make_unique<FakeTabInterface>(test_web_contents_.get());
    delegate_ = std::make_unique<FakeDelegate>(test_web_contents_.get());
    account_selection_view_ = std::make_unique<FakeFedCmAccountSelectionView>(
        delegate_.get(), tab_interface_.get(),
        anchor_widget_->GetContentsView());
    std::vector<IdentityRequestAccountPtr> new_accounts;
    std::vector<IdentityProviderDataPtr> idp_list = idp_list_;
    if (idp_list.empty()) {
      idp_list = {idp_data_};
    }
    account_selection_view_->Show(
        content::RelyingPartyData(kTopFrameEtldPlusOne, iframe_for_display),
        idp_list, accounts_, blink::mojom::RpMode::kPassive, new_accounts);
    dialog_ = static_cast<AccountSelectionBubbleView*>(
        account_selection_view_->account_selection_view());
  }

  void CreateAndShowSingleAccountPicker(
      bool has_display_identifier,
      LoginState login_state = LoginState::kSignUp,
      const std::u16string& iframe_for_display = u"") {
    IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
        kAccountSuffix, idp_data_, login_state);
    if (!has_display_identifier) {
      account->display_identifier = "";
    }

    CreateAccountSelectionBubble(iframe_for_display);
    account->identity_provider = idp_data_;
    dialog_->ShowSingleAccountConfirmDialog(account,
                                            /*show_back_button=*/false);
  }

  void CreateAndShowMultiAccountPicker(
      const std::vector<std::string>& account_suffixes,
      bool supports_add_account = false) {
    std::vector<IdentityRequestAccountPtr> account_list =
        CreateTestIdentityRequestAccounts(account_suffixes, idp_data_);

    CreateAccountSelectionBubble();
    dialog_->ShowMultiAccountPicker(account_list, {idp_data_},
                                    /*rp_icon=*/gfx::Image(),
                                    /*show_back_button=*/false);
  }

  void CreateAndShowMultiIdpAccountPicker(
      const std::vector<IdentityRequestAccountPtr>& accounts,
      const std::vector<IdentityProviderDataPtr>& idp_list) {
    accounts_ = accounts;
    idp_list_ = idp_list;

    CreateAccountSelectionBubble();
  }

  void PerformHeaderChecks(views::View* header,
                           const std::u16string& expected_title,
                           bool expected_icon_visibility,
                           const std::u16string& expected_subtitle = u"") {
    // Perform some basic dialog checks.
    EXPECT_FALSE(dialog()->ShouldShowCloseButton());
    EXPECT_FALSE(dialog()->ShouldShowWindowTitle());

    EXPECT_FALSE(dialog()->GetOkButton());
    EXPECT_FALSE(dialog()->GetCancelButton());

    // Order: Potentially hidden IDP brand icon, potentially hidden back button,
    // titles, close button.
    std::vector<std::string> expected_class_names = {"ImageButton", "View",
                                                     "ImageButton"};
    expected_class_names.insert(expected_class_names.begin(),
                                "BrandIconImageView");
    EXPECT_THAT(GetChildClassNames(header),
                testing::ElementsAreArray(expected_class_names));

    views::View* titles_container = GetViewWithClassName(header, "View");
    EXPECT_EQ(
        static_cast<views::BoxLayout*>(titles_container->GetLayoutManager())
            ->main_axis_alignment(),
        views::LayoutAlignment::kCenter);

    // Check title text.
    views::Label* title_view =
        static_cast<views::Label*>(titles_container->children()[0]);
    ASSERT_TRUE(title_view);
    EXPECT_EQ(title_view->GetText(), expected_title);

    views::Label* subtitle_view =
        static_cast<views::Label*>(titles_container->children()[1]);
    ASSERT_TRUE(subtitle_view);
    if (expected_subtitle.empty()) {
      EXPECT_FALSE(subtitle_view->GetVisible());
      EXPECT_EQ(dialog()->GetDialogSubtitle(), std::nullopt);
    } else {
      EXPECT_TRUE(subtitle_view->GetVisible());
      EXPECT_EQ(subtitle_view->GetText(), expected_subtitle);
      EXPECT_EQ(dialog()->GetDialogSubtitle(),
                base::UTF16ToUTF8(expected_subtitle));
    }

    views::ImageView* idp_brand_icon = static_cast<views::ImageView*>(
        GetViewWithClassName(header, "BrandIconImageView"));
    ASSERT_TRUE(idp_brand_icon);
    EXPECT_EQ(idp_brand_icon->GetVisible(), expected_icon_visibility);
  }

  void PerformMultiAccountChecks(views::View* container,
                                 size_t expected_account_rows,
                                 size_t expected_login_rows) {
    views::LayoutManager* layout_manager = container->GetLayoutManager();
    ASSERT_TRUE(layout_manager);
    views::BoxLayout* box_layout_manager =
        static_cast<views::BoxLayout*>(layout_manager);
    ASSERT_TRUE(box_layout_manager);
    EXPECT_EQ(box_layout_manager->GetOrientation(),
              views::BoxLayout::Orientation::kVertical);

    std::vector<raw_ptr<views::View, VectorExperimental>> children =
        container->children();
    ASSERT_EQ(children.size(), 2u);

    EXPECT_TRUE(IsViewClass<views::Separator>(children[0]));

    EXPECT_TRUE(IsViewClass<views::ScrollView>(children[1]));
    views::ScrollView* scroller = static_cast<views::ScrollView*>(children[1]);
    EXPECT_TRUE(scroller->GetDrawOverflowIndicator());
    views::View* contents = scroller->contents();
    bool has_account_mismatch_separator =
        expected_account_rows > 0u && expected_login_rows > 0u;
    size_t expected_children = expected_account_rows;
    if (has_account_mismatch_separator) {
      ++expected_children;
    }
    expected_children += expected_login_rows;
    ASSERT_GT(expected_children, 0u);
    EXPECT_EQ(contents->children().size(), expected_children);
    layout_manager = contents->GetLayoutManager();
    ASSERT_TRUE(layout_manager);
    box_layout_manager = static_cast<views::BoxLayout*>(layout_manager);
    ASSERT_TRUE(box_layout_manager);
    EXPECT_EQ(box_layout_manager->GetOrientation(),
              views::BoxLayout::Orientation::kVertical);
    size_t index = 0;
    for (size_t i = 0; i < expected_account_rows; ++i) {
      EXPECT_TRUE(IsViewClass<HoverButton>(contents->children()[index++]));
    }
    if (has_account_mismatch_separator) {
      EXPECT_TRUE(IsViewClass<views::Separator>(contents->children()[index++]));
    }
    for (size_t i = 0; i < expected_login_rows; ++i) {
      EXPECT_TRUE(IsViewClass<HoverButton>(contents->children()[index++]));
    }
  }

  void PerformSingleAccountConfirmDialogChecks(
      const std::u16string expected_title,
      bool expected_icon_visibility,
      bool has_display_identifier,
      const std::u16string& expected_subtitle = u"") {
    std::vector<raw_ptr<views::View, VectorExperimental>> children =
        dialog()->children();
    ASSERT_EQ(children.size(), 3u);
    PerformHeaderChecks(children[0], expected_title, expected_icon_visibility,
                        expected_subtitle);
    EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

    views::View* single_account_chooser = children[2];
    ASSERT_EQ(single_account_chooser->children().size(), 3u);

    CheckNonHoverableAccountRow(single_account_chooser->children()[0],
                                kAccountSuffix, has_display_identifier);

    // Check the "Continue as" button.
    views::MdTextButton* button = static_cast<views::MdTextButton*>(
        single_account_chooser->children()[1]);
    ASSERT_TRUE(button);
    EXPECT_EQ(button->GetText(),
              base::UTF8ToUTF16("Continue as " + std::string(kGivenNameBase) +
                                kAccountSuffix));

    CheckDisclosureText(single_account_chooser->children()[2],
                        /*expect_terms_of_service=*/true,
                        /*expect_privacy_policy=*/true);
  }

  std::vector<raw_ptr<views::View, VectorExperimental>> GetContents(
      views::View* container) {
    return static_cast<views::ScrollView*>(container->children()[1])
        ->contents()
        ->children();
  }

  void TestSingleAccount(const std::u16string expected_title,
                         bool expected_icon_visibility,
                         bool has_display_identifier,
                         const std::u16string& iframe_for_display = u"",
                         const std::u16string& expected_subtitle = u"") {
    CreateAndShowSingleAccountPicker(has_display_identifier,
                                     LoginState::kSignUp, iframe_for_display);

    PerformSingleAccountConfirmDialogChecks(
        expected_title, expected_icon_visibility, has_display_identifier,
        expected_subtitle);
  }

  void TestMultipleAccounts(const std::u16string& expected_title,
                            bool expected_icon_visibility) {
    const std::vector<std::string> kAccountSuffixes = {"0", "1", "2"};
    CreateAndShowMultiAccountPicker(kAccountSuffixes);

    std::vector<raw_ptr<views::View, VectorExperimental>> children =
        dialog()->children();
    // The separator is in the multiple accounts container.
    ASSERT_EQ(children.size(), 2u);
    PerformHeaderChecks(children[0], expected_title, expected_icon_visibility);

    PerformMultiAccountChecks(children[1], /*expected_account_rows=*/3,
                              /*expected_login_rows=*/0);

    std::vector<raw_ptr<views::View, VectorExperimental>> contents =
        GetContents(children[1]);
    size_t accounts_index = 0;

    // Check the text shown.
    CheckHoverableAccountRows(contents, kAccountSuffixes, accounts_index);
    EXPECT_EQ(accounts_index, contents.size());
  }

  void TestFailureDialog(const std::u16string expected_title,
                         bool expected_icon_visibility) {
    CreateAccountSelectionBubble();
    dialog_->ShowFailureDialog(kIdpETLDPlusOne, idp_data_->idp_metadata);

    const std::vector<raw_ptr<views::View, VectorExperimental>> children =
        dialog()->children();
    ASSERT_EQ(children.size(), 3u);

    PerformHeaderChecks(children[0], expected_title, expected_icon_visibility);
    EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

    const views::View* failure_dialog = children[2];
    const std::vector<raw_ptr<views::View, VectorExperimental>>
        failure_dialog_children = failure_dialog->children();
    ASSERT_EQ(failure_dialog_children.size(), 2u);

    // Check the body shown.
    views::Label* body = static_cast<views::Label*>(failure_dialog_children[0]);
    ASSERT_TRUE(body);
    EXPECT_EQ(body->GetText(),
              u"You can use your idp-example.com account on this site. To "
              u"continue, sign in to idp-example.com.");

    // Check the "Continue" button.
    views::MdTextButton* button =
        static_cast<views::MdTextButton*>(failure_dialog_children[1]);
    ASSERT_TRUE(button);
    EXPECT_EQ(button->GetText(),
              l10n_util::GetStringUTF16(IDS_SIGNIN_CONTINUE));
  }

  void TestErrorDialog(const std::u16string expected_title,
                       const std::u16string expected_summary,
                       const std::u16string expected_description,
                       bool expected_icon_visibility,
                       const std::string& error_code,
                       const GURL& error_url) {
    CreateAccountSelectionBubble();
    dialog_->ShowErrorDialog(
        kIdpETLDPlusOne, idp_data_->idp_metadata,
        content::IdentityCredentialTokenError(error_code, error_url));

    const std::vector<raw_ptr<views::View, VectorExperimental>> children =
        dialog()->children();
    ASSERT_EQ(children.size(), 4u);

    PerformHeaderChecks(children[0], expected_title, expected_icon_visibility);
    EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

    const views::View* error_dialog = children[2];
    const std::vector<raw_ptr<views::View, VectorExperimental>>
        error_dialog_children = error_dialog->children();
    ASSERT_EQ(error_dialog_children.size(), 2u);

    // Check the summary shown.
    views::Label* summary =
        static_cast<views::Label*>(error_dialog_children[0]);
    ASSERT_TRUE(summary);
    EXPECT_EQ(summary->GetText(), expected_summary);

    // Check the description shown.
    views::Label* description =
        static_cast<views::Label*>(error_dialog_children[1]);
    ASSERT_TRUE(description);
    EXPECT_EQ(description->GetText(), expected_description);

    // Check the buttons shown.
    const std::vector<raw_ptr<views::View, VectorExperimental>> button_row =
        children[3]->children();

    if (error_url.is_empty()) {
      ASSERT_EQ(button_row.size(), 1u);

      views::MdTextButton* got_it_button =
          static_cast<views::MdTextButton*>(button_row[0]);
      ASSERT_TRUE(got_it_button);
      EXPECT_EQ(
          got_it_button->GetText(),
          l10n_util::GetStringUTF16(IDS_SIGNIN_ERROR_DIALOG_GOT_IT_BUTTON));
      return;
    }

    ASSERT_EQ(button_row.size(), 2u);
    for (size_t i = 0; i < button_row.size(); ++i) {
      views::MdTextButton* button =
          static_cast<views::MdTextButton*>(button_row[i]);
      ASSERT_TRUE(button);
      EXPECT_EQ(button->GetText(),
                l10n_util::GetStringUTF16(
                    i == 0 ? IDS_SIGNIN_ERROR_DIALOG_MORE_DETAILS_BUTTON
                           : IDS_SIGNIN_ERROR_DIALOG_GOT_IT_BUTTON));
    }
  }

  void CheckMismatchIdp(views::View* idp_row,
                        const std::u16string& expected_title) {
    ASSERT_EQ("HoverButton", idp_row->GetClassName());
    HoverButton* idp_button = static_cast<HoverButton*>(idp_row);
    ASSERT_TRUE(idp_button);
    EXPECT_EQ(GetHoverButtonTitle(idp_button), expected_title);
    EXPECT_EQ(GetHoverButtonSubtitle(idp_button), nullptr);
    ASSERT_TRUE(GetHoverButtonIconView(idp_button));
    // Using GetPreferredSize() since BrandIconImageView uses a fetched image.
    EXPECT_EQ(GetHoverButtonIconView(idp_button)->GetPreferredSize(),
              gfx::Size(kMultiIdpIconSize, kMultiIdpIconSize));
  }

  void CheckUseOtherAccount(
      views::View* button,
      const std::optional<std::u16string>& expected_idp = std::nullopt) {
    EXPECT_TRUE(IsViewClass<HoverButton>(button));
    HoverButton* idp_button = static_cast<HoverButton*>(button);
    ASSERT_TRUE(idp_button);
    if (expected_idp.has_value()) {
      EXPECT_EQ(GetHoverButtonTitle(idp_button),
                u"Use a different " + *expected_idp + u" account");
    } else {
      EXPECT_EQ(GetHoverButtonTitle(idp_button), u"Use a different account");
    }
  }

  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    test_web_contents_ =
        content::WebContentsTester::CreateTestWebContents(&profile_, nullptr);
    // The x, y coordinates shouldn't matter but the width and height are set to
    // an arbitrary number that is large enough to fit the bubble to ensure that
    // the bubble is not hidden because the web contents is too small.
    test_web_contents_->Resize(
        gfx::Rect(/*x=*/0, /*y=*/0, /*width=*/1000, /*height=*/1000));
  }

  void TearDown() override {
    Reset();
    anchor_widget_.reset();
    ChromeViewsTestBase::TearDown();
  }

  void Reset() {
    dialog_ = nullptr;
    account_selection_view_.reset();
  }

  void ResetWebContents() {
    // We should reset FakeDelegate as well since it depends on WebContents.
    // However in the production code the real delegate owns the
    // AccountSelectionView, so that would result in destruction of the
    // AccountSelectionView. In the real code WebContents destruction
    // asynchronously destroys the real delegate, so it is possible to destroy
    // the WebContents while still having the AccountSelectionView alive.
    account_selection_view_->WillDetach(
        tab_interface_.get(), tabs::TabInterface::DetachReason::kDelete);
    tab_interface_.reset();
    test_web_contents_.reset();
  }

  AccountSelectionBubbleView* dialog() { return dialog_; }

  content::WebContents* web_contents() { return test_web_contents_.get(); }

 protected:
  TestingProfile profile_;
  // This enables uses of TestWebContents.
  content::RenderViewHostTestEnabler test_render_host_factories_;
  std::unique_ptr<content::WebContents> test_web_contents_;

  std::unique_ptr<views::Widget> anchor_widget_;

  std::vector<IdentityRequestAccountPtr> accounts_;
  IdentityProviderDataPtr idp_data_;
  // If non-empty used instead of idp_data_.
  std::vector<IdentityProviderDataPtr> idp_list_;
  std::unique_ptr<FakeTabInterface> tab_interface_;
  std::unique_ptr<FakeDelegate> delegate_;
  std::unique_ptr<FakeFedCmAccountSelectionView> account_selection_view_;
  raw_ptr<AccountSelectionBubbleView, DanglingUntriaged> dialog_;
};

TEST_F(AccountSelectionBubbleViewTest, SingleAccount) {
  TestSingleAccount(kTitleSignIn,
                    /*expected_icon_visibility=*/true,
                    /*has_display_identifier=*/true);
}

TEST_F(AccountSelectionBubbleViewTest, SingleAccountNoTermsOfService) {
  idp_data_->client_metadata.terms_of_service_url = GURL("");
  CreateAndShowSingleAccountPicker(true);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);
  EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

  views::View* single_account_chooser = children[2];
  ASSERT_EQ(single_account_chooser->children().size(), 3u);

  // Check the "Continue as" button.
  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(single_account_chooser->children()[1]);
  ASSERT_TRUE(button);
  EXPECT_EQ(button->GetText(),
            base::UTF8ToUTF16("Continue as " + std::string(kGivenNameBase) +
                              kAccountSuffix));

  CheckDisclosureText(single_account_chooser->children()[2],
                      /*expect_terms_of_service=*/false,
                      /*expect_privacy_policy=*/true);
}

TEST_F(AccountSelectionBubbleViewTest, SingleAccountOnlyTwoDisclosureFields) {
  idp_data_->disclosure_fields = {
      content::IdentityRequestDialogDisclosureField::kName,
      content::IdentityRequestDialogDisclosureField::kEmail};
  idp_data_->client_metadata.terms_of_service_url = GURL();
  CreateAndShowSingleAccountPicker(true);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);
  EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

  views::View* single_account_chooser = children[2];
  ASSERT_EQ(single_account_chooser->children().size(), 3u);

  // Check the "Continue as" button.
  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(single_account_chooser->children()[1]);
  ASSERT_TRUE(button);
  EXPECT_EQ(button->GetText(),
            base::UTF8ToUTF16("Continue as " + std::string(kGivenNameBase) +
                              kAccountSuffix));

  constexpr char16_t kExpectedText[] =
      u"To continue, idp-example.com will share your name and email address "
      u"with this site. See this site's privacy policy.";

  views::StyledLabel* disclosure_label =
      static_cast<views::StyledLabel*>(single_account_chooser->children()[2]);
  EXPECT_EQ(disclosure_label->GetText(), kExpectedText);
}

TEST_F(AccountSelectionBubbleViewTest, MultipleAccounts) {
  TestMultipleAccounts(kTitleSignIn,
                       /*expected_icon_visibility=*/true);
}

TEST_F(AccountSelectionBubbleViewTest, UseDifferentAccountNotSupported) {
  idp_data_->idp_metadata.supports_add_account = true;
  const std::vector<std::string> kAccountSuffixes = {"0", "1"};
  CreateAndShowMultiAccountPicker(kAccountSuffixes, true);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/2,
                            /*expected_login_rows=*/0);
}

TEST_F(AccountSelectionBubbleViewTest, ReturningAccount) {
  CreateAndShowSingleAccountPicker(true, LoginState::kSignIn);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);
  EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

  views::View* single_account_chooser = children[2];
  std::vector<raw_ptr<views::View, VectorExperimental>> chooser_children =
      single_account_chooser->children();
  ASSERT_EQ(chooser_children.size(), 2u);
  views::View* single_account_row = chooser_children[0];

  CheckNonHoverableAccountRow(single_account_row, kAccountSuffix,
                              /*has_display_identifier=*/true);

  // Check the "Continue as" button.
  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(chooser_children[1]);
  EXPECT_EQ(button->GetText(),
            base::UTF8ToUTF16("Continue as " + std::string(kGivenNameBase) +
                              kAccountSuffix));
}

TEST_F(AccountSelectionBubbleViewTest, NewAccountWithoutRequestPermission) {
  idp_data_->disclosure_fields = {};
  CreateAndShowSingleAccountPicker(true);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);
  EXPECT_TRUE(IsViewClass<views::Separator>(children[1]));

  views::View* single_account_chooser = children[2];
  std::vector<raw_ptr<views::View, VectorExperimental>> chooser_children =
      single_account_chooser->children();
  ASSERT_EQ(chooser_children.size(), 2u);
  views::View* single_account_row = chooser_children[0];

  CheckNonHoverableAccountRow(single_account_row, kAccountSuffix,
                              /*has_display_identifier=*/true);

  // Check the "Continue as" button.
  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(chooser_children[1]);
  EXPECT_EQ(button->GetText(),
            base::UTF8ToUTF16("Continue as " + std::string(kGivenNameBase) +
                              kAccountSuffix));
}

TEST_F(AccountSelectionBubbleViewTest,
       ContinueButtonWithProperBackgroundColor) {
  CreateAccountSelectionBubble();

  // Set the dialog background color to white.
  dialog()->SetBackgroundColor(SK_ColorWHITE);

  const std::string kDarkBlue = "#1a73e8";
  SkColor bg_color;
  // A blue background sufficiently contracts with the dialog background.
  content::ParseCssColorString(kDarkBlue, &bg_color);
  idp_data_->idp_metadata.brand_background_color = SkColorSetA(bg_color, 0xff);
  IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
      kAccountSuffix, idp_data_, LoginState::kSignIn);

  dialog()->ShowSingleAccountConfirmDialog(account,
                                           /*show_back_button=*/false);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);

  views::View* single_account_chooser = children[2];
  std::vector<raw_ptr<views::View, VectorExperimental>> chooser_children =
      single_account_chooser->children();
  ASSERT_EQ(chooser_children.size(), 2u);

  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(chooser_children[1]);
  ASSERT_TRUE(button);
  EXPECT_EQ(*(button->GetBgColorOverrideDeprecated()), bg_color);
}

TEST_F(AccountSelectionBubbleViewTest,
       ContinueButtonWithImproperBackgroundColor) {
  CreateAccountSelectionBubble();

  // Set the dialog background color to white.
  dialog()->SetBackgroundColor(SK_ColorWHITE);

  const std::string kWhite = "#fff";
  SkColor bg_color;
  // By default a white button does not contrast with the dialog background so
  // the specified color will be ignored.
  content::ParseCssColorString(kWhite, &bg_color);
  content::IdentityProviderMetadata idp_metadata =
      content::IdentityProviderMetadata();
  idp_data_->idp_metadata.brand_background_color = SkColorSetA(bg_color, 0xff);
  IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
      kAccountSuffix, idp_data_, LoginState::kSignIn);

  dialog()->ShowSingleAccountConfirmDialog(account,
                                           /*show_back_button=*/false);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);

  views::View* single_account_chooser = children[2];
  std::vector<raw_ptr<views::View, VectorExperimental>> chooser_children =
      single_account_chooser->children();
  ASSERT_EQ(chooser_children.size(), 2u);

  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(chooser_children[1]);
  ASSERT_TRUE(button);
  // The button color is not customized by the IDP.
  EXPECT_FALSE(button->GetBgColorOverrideDeprecated());
}

TEST_F(AccountSelectionBubbleViewTest, Verifying) {
  IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
      kAccountSuffix, idp_data_, LoginState::kSignIn);

  CreateAccountSelectionBubble();
  dialog_->ShowVerifyingSheet(
      account, l10n_util::GetStringUTF16(IDS_VERIFY_SHEET_TITLE));

  const std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);
  PerformHeaderChecks(children[0], kTitleSigningIn,
                      /*expected_icon_visibility=*/true);
  EXPECT_TRUE(IsViewClass<views::ProgressBar>(children[1]));

  views::View* row_container = dialog()->children()[2];
  ASSERT_EQ(row_container->children().size(), 1u);
  CheckNonHoverableAccountRow(row_container->children()[0], kAccountSuffix,
                              /*has_display_identifier=*/true);
}

TEST_F(AccountSelectionBubbleViewTest, VerifyingForAutoReauthn) {
  IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
      kAccountSuffix, idp_data_, LoginState::kSignIn);
  CreateAccountSelectionBubble();
  const auto title =
      l10n_util::GetStringUTF16(IDS_VERIFY_SHEET_TITLE_AUTO_REAUTHN);
  dialog_->ShowVerifyingSheet(account, title);

  const std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 3u);
  PerformHeaderChecks(children[0], kTitleSigningInWithAutoReauthn,
                      /*expected_icon_visibility=*/true);
  EXPECT_TRUE(IsViewClass<views::ProgressBar>(children[1]));

  views::View* row_container = dialog()->children()[2];
  ASSERT_EQ(row_container->children().size(), 1u);
  CheckNonHoverableAccountRow(row_container->children()[0], kAccountSuffix,
                              /*has_display_identifier=*/true);
}

TEST_F(AccountSelectionBubbleViewTest, Failure) {
  TestFailureDialog(kTitleSignIn, /*expected_icon_visibility=*/true);
}

class MultipleIdpAccountSelectionBubbleViewTest
    : public AccountSelectionBubbleViewTest {
 public:
  MultipleIdpAccountSelectionBubbleViewTest() = default;

 protected:
  void SetUp() override {
    feature_list_.InitAndEnableFeature(
        features::kFedCmMultipleIdentityProviders);
    AccountSelectionBubbleViewTest::SetUp();
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Tests that the single account case is the same with
// features::kFedCmMultipleIdentityProviders enabled. See
// AccountSelectionBubbleViewTest's SingleAccount test.
TEST_F(MultipleIdpAccountSelectionBubbleViewTest, SingleAccount) {
  TestSingleAccount(kTitleSignIn,
                    /*expected_icon_visibility=*/true,
                    /*has_display_identifier=*/true);
}

// Tests that when there is multiple accounts but only one IDP, the UI is
// exactly the same with features::kFedCmMultipleIdentityProviders enabled (see
// AccountSelectionBubbleViewTest's MultipleAccounts test).
TEST_F(MultipleIdpAccountSelectionBubbleViewTest, MultipleAccountsSingleIdp) {
  TestMultipleAccounts(kTitleSignIn,
                       /*expected_icon_visibility=*/true);
}

// Tests that the logo is visible with features::kFedCmMultipleIdentityProviders
// enabled and multiple IDPs.
TEST_F(MultipleIdpAccountSelectionBubbleViewTest,
       MultipleAccountsMultipleIdps) {
  const std::vector<std::string> kAccountSuffixes1 = {"1", "2"};
  const std::vector<std::string> kAccountSuffixes2 = {"3", "4"};
  std::vector<IdentityProviderDataPtr> idp_list = {
      base::MakeRefCounted<content::IdentityProviderData>(
          kIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata(kTermsOfServiceUrl),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false),
      base::MakeRefCounted<content::IdentityProviderData>(
          kSecondIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata("https://tos-2.com"),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false)};
  std::vector<IdentityRequestAccountPtr> account_list = {
      CreateTestIdentityRequestAccount(kAccountSuffixes1[0], idp_list[0]),
      CreateTestIdentityRequestAccount(kAccountSuffixes1[1], idp_list[0]),
      CreateTestIdentityRequestAccount(kAccountSuffixes2[0], idp_list[1]),
      CreateTestIdentityRequestAccount(kAccountSuffixes2[1], idp_list[1])};
  CreateAndShowMultiIdpAccountPicker(account_list, idp_list);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  views::View* accounts_container = children[1];
  PerformMultiAccountChecks(accounts_container, /*expected_account_rows=*/4,
                            /*expected_login_rows=*/0);

  std::vector<raw_ptr<views::View, VectorExperimental>> accounts =
      GetContents(accounts_container);

  // Check the first IDP.
  size_t accounts_index = 0;
  CheckHoverableAccountRows(accounts, kAccountSuffixes1, accounts_index,
                            /*expect_idp=*/true);

  // Check the second IDP.
  CheckHoverableAccountRows(accounts, kAccountSuffixes2, accounts_index,
                            /*expect_idp=*/true);
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest, OneIdpWithMismatch) {
  const std::vector<std::string> kAccountSuffixes1 = {"1", "2"};
  std::vector<IdentityProviderDataPtr> idp_list = {
      base::MakeRefCounted<content::IdentityProviderData>(
          kIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata(kTermsOfServiceUrl),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false),
      base::MakeRefCounted<content::IdentityProviderData>(
          kSecondIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata("https://tos-2.com"),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/true)};
  std::vector<IdentityRequestAccountPtr> accounts_list =
      CreateTestIdentityRequestAccounts(kAccountSuffixes1, idp_list[0]);
  CreateAndShowMultiIdpAccountPicker(accounts_list, idp_list);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/2,
                            /*expected_login_rows=*/1);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);

  size_t index = 0;
  CheckHoverableAccountRows(contents, kAccountSuffixes1, index,
                            /*expect_idp=*/true);

  // Add one for the separator.
  ++index;
  ASSERT_LT(index, contents.size());
  CheckMismatchIdp(
      contents[index],
      base::StrCat({u"Use your ", kSecondIdpETLDPlusOne, u" account"}));
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest,
       MultiIdpUseOtherAccountNotSupported) {
  const std::vector<std::string> kAccountSuffixes1 = {"1", "2"};
  const std::vector<std::string> kAccountSuffixes2 = {"3"};
  content::IdentityProviderMetadata idp_with_supports_add =
      content::IdentityProviderMetadata();
  idp_with_supports_add.supports_add_account = true;
  std::vector<IdentityProviderDataPtr> idp_list = {
      base::MakeRefCounted<content::IdentityProviderData>(
          kIdpForDisplay, idp_with_supports_add,
          CreateTestClientMetadata(kTermsOfServiceUrl),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false),
      base::MakeRefCounted<content::IdentityProviderData>(
          kSecondIdpForDisplay, idp_with_supports_add,
          CreateTestClientMetadata("https://tos-2.com"),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false)};
  std::vector<IdentityRequestAccountPtr> accounts_list = {
      CreateTestIdentityRequestAccount(kAccountSuffixes1[0], idp_list[0]),
      CreateTestIdentityRequestAccount(kAccountSuffixes1[1], idp_list[0]),
      CreateTestIdentityRequestAccount(kAccountSuffixes2[0], idp_list[1])};
  CreateAndShowMultiIdpAccountPicker(accounts_list, idp_list);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/3,
                            /*expected_login_rows=*/0);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);

  // Check the first IDP.
  size_t index = 0;
  CheckHoverableAccountRows(contents, kAccountSuffixes1, index,
                            /*expect_idp=*/true);

  // Check the second IDP.
  CheckHoverableAccountRows(contents, kAccountSuffixes2, index,
                            /*expect_idp=*/true);
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest, ShowSingleReturningAccount) {
  const std::vector<std::string> kAccountSuffixes1 = {"1", "2"};
  const std::vector<std::string> kAccountSuffixes2 = {"3"};
  idp_list_ = {base::MakeRefCounted<content::IdentityProviderData>(
                   kIdpForDisplay, content::IdentityProviderMetadata(),
                   CreateTestClientMetadata(kTermsOfServiceUrl),
                   blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
                   kDefaultDisclosureFields,
                   /*has_login_status_mismatch=*/false),
               base::MakeRefCounted<content::IdentityProviderData>(
                   kSecondIdpForDisplay, content::IdentityProviderMetadata(),
                   CreateTestClientMetadata("https://tos-2.com"),
                   blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
                   kDefaultDisclosureFields,
                   /*has_login_status_mismatch=*/false),
               base::MakeRefCounted<content::IdentityProviderData>(
                   "idp3.com", content::IdentityProviderMetadata(),
                   CreateTestClientMetadata("https://tos-3.com"),
                   blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
                   kDefaultDisclosureFields,
                   /*has_login_status_mismatch=*/true),
               base::MakeRefCounted<content::IdentityProviderData>(
                   "idp4.com", content::IdentityProviderMetadata(),
                   CreateTestClientMetadata("https://tos-4.com"),
                   blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
                   kDefaultDisclosureFields,
                   /*has_login_status_mismatch=*/true)};
  accounts_ = {
      CreateTestIdentityRequestAccount(kAccountSuffixes2[0], idp_list_[1],
                                       LoginState::kSignIn),
      CreateTestIdentityRequestAccount(kAccountSuffixes1[0], idp_list_[0]),
      CreateTestIdentityRequestAccount(kAccountSuffixes1[1], idp_list_[0])};

  CreateAccountSelectionBubble();

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  views::View* wrapper = children[1];

  views::BoxLayout* layout_manager =
      static_cast<views::BoxLayout*>(wrapper->GetLayoutManager());
  EXPECT_TRUE(layout_manager);
  EXPECT_EQ(layout_manager->GetOrientation(),
            views::BoxLayout::Orientation::kVertical);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);
  ASSERT_EQ(6u, contents.size());

  size_t accounts_index = 0;
  CheckHoverableAccountRows(contents, kAccountSuffixes2, accounts_index,
                            /*expect_idp=*/true);
  CheckHoverableAccountRows(contents, kAccountSuffixes1, accounts_index,
                            /*expect_idp=*/true);
  EXPECT_TRUE(IsViewClass<views::Separator>(contents[accounts_index++]));
  CheckMismatchIdp(contents[accounts_index++], u"Use your idp3.com account");
  CheckMismatchIdp(contents[accounts_index++], u"Use your idp4.com account");
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest, MultiIdpWithAllIdpsMismatch) {
  std::vector<IdentityProviderDataPtr> idp_list = {
      base::MakeRefCounted<content::IdentityProviderData>(
          kIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata(kTermsOfServiceUrl),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/true),
      base::MakeRefCounted<content::IdentityProviderData>(
          kSecondIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata("https://tos-2.com"),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/true)};
  CreateAndShowMultiIdpAccountPicker(std::vector<IdentityRequestAccountPtr>(),
                                     idp_list);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/0,
                            /*expected_login_rows=*/2);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);

  ASSERT_GE(contents.size(), 2u);
  CheckMismatchIdp(contents[0],
                   base::StrCat({u"Use your ", kIdpETLDPlusOne, u" account"}));
  CheckMismatchIdp(
      contents[1],
      base::StrCat({u"Use your ", kSecondIdpETLDPlusOne, u" account"}));
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest, MultipleReturningAccounts) {
  std::vector<IdentityProviderDataPtr> idp_list = {
      base::MakeRefCounted<content::IdentityProviderData>(
          kIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata(kTermsOfServiceUrl),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false),
      base::MakeRefCounted<content::IdentityProviderData>(
          kSecondIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata("https://tos-2.com"),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false)};
  // The UI code receives the accounts sorted in the order in which they should
  // be displayed.
  std::vector<IdentityRequestAccountPtr> accounts_list = {
      CreateTestIdentityRequestAccount("returning1", idp_list[0],
                                       LoginState::kSignIn),
      CreateTestIdentityRequestAccount("returning2", idp_list[1],
                                       LoginState::kSignIn),
      CreateTestIdentityRequestAccount("new1", idp_list[0]),
      CreateTestIdentityRequestAccount("new2", idp_list[1])};

  CreateAndShowMultiIdpAccountPicker(accounts_list, idp_list);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/4,
                            /*expected_login_rows=*/0);

  std::vector<raw_ptr<views::View, VectorExperimental>> accounts =
      GetContents(children[1]);

  // Returning accounts are shown first.
  const std::vector<std::string> expected_account_order = {
      "returning1", "returning2", "new1", "new2"};
  size_t accounts_index = 0;
  CheckHoverableAccountRows(accounts, expected_account_order, accounts_index,
                            /*expect_idp=*/true);
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest,
       MultipleReturningAccountsWithTimestamps) {
  const std::vector<std::string> kAccountSuffixes1 = {"new1", "returning1",
                                                      "returning2"};
  const std::vector<std::string> kAccountSuffixes2 = {"new2", "returning3",
                                                      "returning4"};
  std::vector<IdentityProviderDataPtr> idp_list = {
      base::MakeRefCounted<content::IdentityProviderData>(
          kIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata(kTermsOfServiceUrl),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false),
      base::MakeRefCounted<content::IdentityProviderData>(
          kSecondIdpForDisplay, content::IdentityProviderMetadata(),
          CreateTestClientMetadata("https://tos-2.com"),
          blink::mojom::RpContext::kSignIn, /*format=*/std::nullopt,
          kDefaultDisclosureFields,
          /*has_login_status_mismatch=*/false)};
  // Note that `new2` is last despite having last_used_timestamp because it is
  // not considered a returning account.
  std::vector<IdentityRequestAccountPtr> accounts_list = {
      CreateTestIdentityRequestAccount("returning3", idp_list[1],
                                       LoginState::kSignIn,
                                       base::Time() + base::Microseconds(2)),
      CreateTestIdentityRequestAccount("returning1", idp_list[0],
                                       LoginState::kSignIn,
                                       base::Time() + base::Microseconds(1)),
      CreateTestIdentityRequestAccount("returning2", idp_list[0],
                                       LoginState::kSignIn, base::Time()),
      CreateTestIdentityRequestAccount("returning4", idp_list[1],
                                       LoginState::kSignIn, base::Time()),
      CreateTestIdentityRequestAccount("new1", idp_list[0]),
      CreateTestIdentityRequestAccount("new2", idp_list[1], LoginState::kSignUp,
                                       base::Time() + base::Microseconds(3))};

  CreateAndShowMultiIdpAccountPicker(accounts_list, idp_list);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  ASSERT_EQ(children.size(), 2u);
  // The multiple account chooser container includes the separator.
  PerformHeaderChecks(children[0], kTitleSignInWithoutIdp,
                      /*expected_icon_visibility=*/false);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/6,
                            /*expected_login_rows=*/0);

  std::vector<raw_ptr<views::View, VectorExperimental>> accounts =
      GetContents(children[1]);

  const std::vector<std::string> expected_account_order = {
      "returning3", "returning1", "returning2", "returning4", "new1", "new2"};
  size_t accounts_index = 0;
  CheckHoverableAccountRows(accounts, expected_account_order, accounts_index,
                            /*expect_idp=*/true);
}

TEST_F(AccountSelectionBubbleViewTest, GenericError) {
  TestErrorDialog(kTitleSignIn, u"Can't continue with idp-example.com",
                  u"Something went wrong",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"",
                  /*error_url=*/GURL());
}

TEST_F(AccountSelectionBubbleViewTest, GenericErrorWithErrorUrl) {
  TestErrorDialog(kTitleSignIn, u"Can't continue with idp-example.com",
                  u"Something went wrong",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"",
                  GURL(u"https://idp-example.com/more-details"));
}

TEST_F(AccountSelectionBubbleViewTest, ErrorWithDifferentErrorCodes) {
  // Invalid request without error URL
  TestErrorDialog(kTitleSignIn,
                  u"rp-example.com can't continue using idp-example.com",
                  u"This option is unavailable right now. You can try other "
                  u"ways to continue on rp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"invalid_request",
                  /*error_url=*/GURL());

  // Invalid request with error URL
  TestErrorDialog(
      kTitleSignIn, u"rp-example.com can't continue using idp-example.com",
      u"This option is unavailable right now. Choose \"More "
      u"details\" below to get more information from idp-example.com.",
      /*expected_icon_visibility=*/true,
      /*error_code=*/"invalid_request",
      GURL(u"https://idp-example.com/more-details"));

  // Unauthorized client without error URL
  TestErrorDialog(kTitleSignIn,
                  u"rp-example.com can't continue using idp-example.com",
                  u"This option is unavailable right now. You can try other "
                  u"ways to continue on rp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"unauthorized_client",
                  /*error_url=*/GURL());

  // Unauthorized client with error URL
  TestErrorDialog(
      kTitleSignIn, u"rp-example.com can't continue using idp-example.com",
      u"This option is unavailable right now. Choose \"More "
      u"details\" below to get more information from idp-example.com.",
      /*expected_icon_visibility=*/true,
      /*error_code=*/"unauthorized_client",
      GURL(u"https://idp-example.com/more-details"));

  // Access denied without error URL
  TestErrorDialog(kTitleSignIn, u"Check that you chose the right account",
                  u"Check if the selected account is supported. You can try "
                  u"other ways to continue on rp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"access_denied",
                  /*error_url=*/GURL());

  // Access denied with error URL
  TestErrorDialog(
      kTitleSignIn, u"Check that you chose the right account",
      u"Check if the selected account is supported. Choose \"More "
      u"details\" below to get more information from idp-example.com.",
      /*expected_icon_visibility=*/true,
      /*error_code=*/"access_denied",
      GURL(u"https://idp-example.com/more-details"));

  // Temporarily unavailable without error URL
  TestErrorDialog(kTitleSignIn, u"Try again later",
                  u"idp-example.com isn't available right now. If this issue "
                  u"keeps happening, you can try other ways to continue on "
                  u"rp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"temporarily_unavailable",
                  /*error_url=*/GURL());

  // Temporarily unavailable with error URL
  TestErrorDialog(kTitleSignIn, u"Try again later",
                  u"idp-example.com isn't available right now. If this issue "
                  u"keeps happening, choose \"More details\" below to get more "
                  u"information from idp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"temporarily_unavailable",
                  GURL(u"https://idp-example.com/more-details"));

  // Server error without error URL
  TestErrorDialog(kTitleSignIn, u"Check your internet connection",
                  u"If you're online but this issue keeps happening, you can "
                  u"try other ways to continue on rp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"server_error",
                  /*error_url=*/GURL());

  // Server error with error URL
  TestErrorDialog(kTitleSignIn, u"Check your internet connection",
                  u"If you're online but this issue keeps happening, you can "
                  u"try other ways to continue on rp-example.com.",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"server_error",
                  GURL(u"https://idp-example.com/more-details"));

  // Error not in our predefined list without error URL
  TestErrorDialog(kTitleSignIn, u"Can't continue with idp-example.com",
                  u"Something went wrong",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"error_we_dont_support",
                  /*error_url=*/GURL());

  // Error not in our predefined list with error URL
  TestErrorDialog(kTitleSignIn, u"Can't continue with idp-example.com",
                  u"Something went wrong",
                  /*expected_icon_visibility=*/true,
                  /*error_code=*/"error_we_dont_support",
                  GURL(u"https://idp-example.com/more-details"));
}

// Tests that the brand icon view is hidden if the brand icon is empty.
TEST_F(AccountSelectionBubbleViewTest, EmptyBrandIconHidesImageView) {
  idp_data_->idp_metadata.brand_icon_url = GURL("invalid url");
  idp_data_->idp_metadata.brand_decoded_icon = gfx::Image();
  CreateAndShowSingleAccountPicker(true);

  views::View* brand_icon_image_view = static_cast<views::View*>(
      GetViewWithClassName(dialog()->children()[0], "BrandIconImageView"));
  ASSERT_TRUE(brand_icon_image_view);
  EXPECT_FALSE(brand_icon_image_view->GetVisible());
}

TEST_F(AccountSelectionBubbleViewTest, OneDisabledAccount) {
  IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
      kAccountSuffix, idp_data_, LoginState::kSignUp);
  account->is_filtered_out = true;
  idp_data_->idp_metadata.has_filtered_out_account = true;

  CreateAccountSelectionBubble();
  // The backend will invoke ShowMultiAccountPicker with a single account since
  // there are filtered out accounts.
  dialog_->ShowMultiAccountPicker({account}, {idp_data_},
                                  /*rp_icon=*/gfx::Image(),
                                  /*show_back_button=*/false);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  // The separator is in the multiple accounts container.
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/1,
                            /*expected_login_rows=*/1);

  std::vector<raw_ptr<views::View, VectorExperimental>> accounts =
      GetContents(children[1]);

  // Check the filtered account and use a different account button.
  CheckHoverableAccountRow(accounts[0], kAccountSuffix,
                           /*has_display_identifier=*/true,
                           /*expect_idp=*/false, /*is_modal_dialog=*/false,
                           /*is_disabled=*/true);
  CheckUseOtherAccount(accounts[2]);
}

TEST_F(AccountSelectionBubbleViewTest, MultipleDisabledAccounts) {
  idp_data_->idp_metadata.has_filtered_out_account = true;
  std::vector<IdentityRequestAccountPtr> accounts_list;
  for (size_t i = 0; i < 3; ++i) {
    IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
        kAccountSuffix + base::NumberToString(i), idp_data_,
        LoginState::kSignIn);
    account->is_filtered_out = true;
    accounts_list.push_back(std::move(account));
  }
  CreateAccountSelectionBubble();
  dialog_->ShowMultiAccountPicker(accounts_list, {idp_data_},
                                  /*rp_icon=*/gfx::Image(),
                                  /*show_back_button=*/false);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  // The separator is in the multiple accounts container.
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/3,
                            /*expected_login_rows=*/1);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);

  // Check the text shown.
  for (size_t i = 0; i < 3; ++i) {
    CheckHoverableAccountRow(contents[i],
                             kAccountSuffix + base::NumberToString(i),
                             /*has_display_identifier=*/true,
                             /*expect_idp=*/false, /*is_modal_dialog=*/false,
                             /*is_disabled=*/true);
  }
  CheckUseOtherAccount(contents[4]);
}

TEST_F(AccountSelectionBubbleViewTest, OneDisabledAccountAndOneEnabledAccount) {
  idp_data_->idp_metadata.has_filtered_out_account = true;
  std::vector<IdentityRequestAccountPtr> accounts_list;
  const std::vector<std::string> kAccountSuffixes = {"enabled", "disabled"};
  IdentityRequestAccountPtr account1 = CreateTestIdentityRequestAccount(
      kAccountSuffixes[0], idp_data_, LoginState::kSignIn);
  accounts_list.push_back(std::move(account1));
  IdentityRequestAccountPtr account2 = CreateTestIdentityRequestAccount(
      kAccountSuffixes[1], idp_data_, LoginState::kSignUp);
  account2->is_filtered_out = true;
  accounts_list.push_back(std::move(account2));

  CreateAccountSelectionBubble();
  dialog_->ShowMultiAccountPicker(accounts_list, {idp_data_},
                                  /*rp_icon=*/gfx::Image(),
                                  /*show_back_button=*/false);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  // The separator is in the multiple accounts container.
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/2,
                            /*expected_login_rows=*/1);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);
  CheckHoverableAccountRow(contents[0], kAccountSuffixes[0],
                           /*has_display_identifier=*/true,
                           /*expect_idp=*/false, /*is_modal_dialog=*/false,
                           /*is_disabled=*/false);
  CheckHoverableAccountRow(contents[1], kAccountSuffixes[1],
                           /*has_display_identifier=*/true,
                           /*expect_idp=*/false, /*is_modal_dialog=*/false,
                           /*is_disabled=*/true);
  CheckUseOtherAccount(contents[3]);
}

TEST_F(AccountSelectionBubbleViewTest, SingleIdentifierAccounts) {
  idp_data_->idp_metadata.has_filtered_out_account = true;
  std::vector<IdentityRequestAccountPtr> accounts_list;
  const std::vector<std::string> kAccountSuffixes = {"enabled", "disabled"};
  IdentityRequestAccountPtr account1 = CreateTestIdentityRequestAccount(
      kAccountSuffixes[0], idp_data_, LoginState::kSignIn);
  account1->display_identifier = "";
  accounts_list.push_back(std::move(account1));
  IdentityRequestAccountPtr account2 = CreateTestIdentityRequestAccount(
      kAccountSuffixes[1], idp_data_, LoginState::kSignUp);
  account2->display_identifier = "";
  account2->is_filtered_out = true;
  accounts_list.push_back(std::move(account2));

  CreateAccountSelectionBubble();
  dialog_->ShowMultiAccountPicker(accounts_list, {idp_data_},
                                  /*rp_icon=*/gfx::Image(),
                                  /*show_back_button=*/false);

  std::vector<raw_ptr<views::View, VectorExperimental>> children =
      dialog()->children();
  // The separator is in the multiple accounts container.
  ASSERT_EQ(children.size(), 2u);
  PerformHeaderChecks(children[0], kTitleSignIn,
                      /*expected_icon_visibility=*/true);

  PerformMultiAccountChecks(children[1], /*expected_account_rows=*/2,
                            /*expected_login_rows=*/1);

  std::vector<raw_ptr<views::View, VectorExperimental>> contents =
      GetContents(children[1]);
  CheckHoverableAccountRow(contents[0], kAccountSuffixes[0],
                           /*has_display_identifier=*/false,
                           /*expect_idp=*/false, /*is_modal_dialog=*/false,
                           /*is_disabled=*/false);
  CheckHoverableAccountRow(contents[1], kAccountSuffixes[1],
                           /*has_display_identifier=*/false,
                           /*expect_idp=*/false, /*is_modal_dialog=*/false,
                           /*is_disabled=*/true);
  CheckUseOtherAccount(contents[3]);
}

TEST_F(MultipleIdpAccountSelectionBubbleViewTest,
       SingleAccountSingleIdentifier) {
  TestSingleAccount(kTitleSignIn,
                    /*expected_icon_visibility=*/true,
                    /*has_display_identifier=*/false);
}

TEST_F(AccountSelectionBubbleViewTest, IframeTitle) {
  TestSingleAccount(kTitleIframeSignIn,
                    /*expected_icon_visibility=*/true,
                    /*has_display_identifier=*/false, kIframeETLDPlusOne,
                    kSubtitleIframeSignIn);
}

// TODO(crbug.com/420421406): Re-enable this test on ChromeOS.
#if BUILDFLAG(IS_CHROMEOS)
#define MAYBE_ContinueButtonFocusedWithScreenReader \
  DISABLED_ContinueButtonFocusedWithScreenReader
#else
#define MAYBE_ContinueButtonFocusedWithScreenReader \
  ContinueButtonFocusedWithScreenReader
#endif
TEST_F(AccountSelectionBubbleViewTest,
       MAYBE_ContinueButtonFocusedWithScreenReader) {
  content::ScopedAccessibilityModeOverride screen_reader_mode(
      ui::AXMode::kScreenReader);
  CreateAndShowSingleAccountPicker(/*has_display_identifier=*/true);
  views::View* single_account_chooser = dialog()->children()[2];
  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(single_account_chooser->children()[1]);
  EXPECT_TRUE(button->HasFocus());
}

TEST_F(AccountSelectionBubbleViewTest,
       ContinueButtonNotFocusedWithoutScreenReader) {
  CreateAndShowSingleAccountPicker(/*has_display_identifier=*/true);
  views::View* single_account_chooser = dialog()->children()[2];
  views::MdTextButton* button =
      static_cast<views::MdTextButton*>(single_account_chooser->children()[1]);
  EXPECT_FALSE(button->HasFocus());
}

// Test interaction of AccountHoverButton & FedCmAccountSelectionView via
// FakeFedCmAccountSelectionView when AccountHoverButton OnPressed() is
// called.
class AccountSelectionInteractionTest : public AccountSelectionBubbleViewTest {
 public:
  views::View* GetFirstAccountHoverButton() {
    std::vector<raw_ptr<views::View, VectorExperimental>> children =
        dialog_->children();

    views::View* container = children[1];
    std::vector<raw_ptr<views::View, VectorExperimental>> children2 =
        static_cast<views::ScrollView*>(container->children()[1])
            ->contents()
            ->children();
    // Considering three account details got passed, Account Selection Bubble
    // view should contain three account hover buttons.
    EXPECT_EQ(children2.size(), 3u);
    EXPECT_EQ(children2[0]->GetClassName(), "HoverButton");
    return children2[0];
  }
};

TEST_F(AccountSelectionInteractionTest,
       TestAccountHoverButtonLifecycleDuringAccountSelection) {
  const std::vector<std::string> kAccountSuffixes = {"1", "2", "3"};
  CreateAndShowMultiAccountPicker(kAccountSuffixes);

  AccountHoverButton* account_hover_button =
      static_cast<AccountHoverButton*>(GetFirstAccountHoverButton());
  IdentityRequestAccountPtr account = CreateTestIdentityRequestAccount(
      kAccountSuffix, idp_data_, LoginState::kSignUp);
  // Simulate clicking the account hover button.
  account_selection_view_->ClickAccountHoverButton(account_hover_button,
                                                   account);

  // Now that account selection has been made in OnAccountSelect,
  // perform checks on contents of SingleAccountConfirmDialog
  PerformSingleAccountConfirmDialogChecks(kTitleSignIn,
                                          /*expected_icon_visibility=*/true,
                                          /*has_display_identifier=*/true);
}

}  //  namespace webid
