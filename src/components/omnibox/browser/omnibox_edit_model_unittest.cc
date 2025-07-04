// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/browser/omnibox_edit_model.h"

#include <stddef.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "components/dom_distiller/core/url_constants.h"
#include "components/dom_distiller/core/url_utils.h"
#include "components/omnibox/browser/actions/omnibox_action.h"
#include "components/omnibox/browser/actions/tab_switch_action.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/fake_autocomplete_controller.h"
#include "components/omnibox/browser/omnibox_controller.h"
#include "components/omnibox/browser/omnibox_popup_view.h"
#include "components/omnibox/browser/omnibox_prefs.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "components/omnibox/browser/search_provider.h"
#include "components/omnibox/browser/test_location_bar_model.h"
#include "components/omnibox/browser/test_omnibox_client.h"
#include "components/omnibox/browser/test_omnibox_edit_model.h"
#include "components/omnibox/browser/test_omnibox_popup_view.h"
#include "components/omnibox/browser/test_omnibox_view.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "components/omnibox/browser/unscoped_extension_provider.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/omnibox/common/omnibox_focus_state.h"
#include "components/prefs/testing_pref_service.h"
#include "components/url_formatter/url_fixer.h"
#include "extensions/buildflags/buildflags.h"
#include "omnibox_triggered_feature_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/omnibox_proto/answer_type.pb.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_unittest_util.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/extension_features.h"  // nogncheck
#endif

using metrics::OmniboxEventProto;
using Selection = OmniboxPopupSelection;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SaveArg;

namespace ui {
struct AXNodeData;
}

namespace {

void OpenUrlFromEditBox(OmniboxController* controller,
                        TestOmniboxEditModel* model,
                        const std::u16string url_text,
                        bool is_autocompleted) {
  AutocompleteMatch match(
      controller->autocomplete_controller()->search_provider(), 0, false,
      AutocompleteMatchType::OPEN_TAB);
  match.destination_url = GURL(url_text);
  match.allowed_to_be_default_match = true;
  if (is_autocompleted) {
    match.inline_autocompletion = url_text;
  } else {
    model->SetUserText(url_text);
  }
  model->OnSetFocus(false);
  model->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB, GURL(),
                             std::u16string(), 0);
}

}  // namespace

class OmniboxEditModelTest : public testing::Test {
 public:
  OmniboxEditModelTest() {
    auto omnibox_client = std::make_unique<TestOmniboxClient>();
    omnibox_client_ = omnibox_client.get();

    view_ = std::make_unique<TestOmniboxView>(std::move(omnibox_client));
    view_->controller()->SetEditModelForTesting(
        std::make_unique<TestOmniboxEditModel>(view_->controller(), view_.get(),
                                               /*pref_service=*/nullptr));
  }

  TestOmniboxView* view() { return view_.get(); }
  TestLocationBarModel* location_bar_model() {
    return omnibox_client_->location_bar_model();
  }
  TestOmniboxEditModel* model() {
    return static_cast<TestOmniboxEditModel*>(view_->model());
  }
  OmniboxController* controller() { return view_->controller(); }

 protected:
  base::test::TaskEnvironment task_environment_;
  raw_ptr<TestOmniboxClient, DanglingUntriaged> omnibox_client_;
  std::unique_ptr<TestOmniboxView> view_;
};

TEST_F(OmniboxEditModelTest, DISABLED_InlineAutocompleteText) {
  // Test if the model updates the inline autocomplete text in the view.
  EXPECT_EQ(std::u16string(), view()->inline_autocompletion());
  model()->SetUserText(u"he");
  model()->OnPopupDataChanged(std::u16string(),
                              /*is_temporary_text=*/false, u"llo",
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});
  EXPECT_EQ(u"hello", view()->GetText());
  EXPECT_EQ(u"llo", view()->inline_autocompletion());

  std::u16string text_before = u"he";
  std::u16string text_after = u"hel";
  OmniboxView::StateChanges state_changes{
      &text_before, &text_after, 3, 3, false, true, false, false};
  model()->OnAfterPossibleChange(state_changes, true);
  EXPECT_EQ(std::u16string(), view()->inline_autocompletion());
  model()->OnPopupDataChanged(std::u16string(),
                              /*is_temporary_text=*/false, u"lo",
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});
  EXPECT_EQ(u"hello", view()->GetText());
  EXPECT_EQ(u"lo", view()->inline_autocompletion());

  model()->Revert();
  EXPECT_EQ(std::u16string(), view()->GetText());
  EXPECT_EQ(std::u16string(), view()->inline_autocompletion());

  model()->SetUserText(u"he");
  model()->OnPopupDataChanged(std::u16string(),
                              /*is_temporary_text=*/false, u"llo",
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});
  EXPECT_EQ(u"hello", view()->GetText());
  EXPECT_EQ(u"llo", view()->inline_autocompletion());

  model()->AcceptTemporaryTextAsUserText();
  EXPECT_EQ(u"hello", view()->GetText());
  EXPECT_EQ(std::u16string(), view()->inline_autocompletion());
}

// iOS doesn't use elisions in the Omnibox textfield.
#if !BUILDFLAG(IS_IOS)
TEST_F(OmniboxEditModelTest, RespectUnelisionInZeroSuggest) {
  location_bar_model()->set_url(GURL("https://www.example.com/"));
  location_bar_model()->set_url_for_display(u"example.com");

  EXPECT_TRUE(model()->ResetDisplayTexts());
  model()->Revert();

  // Set up view with unelided text.
  EXPECT_EQ(u"example.com", view()->GetText());
  EXPECT_TRUE(model()->Unelide());
  EXPECT_EQ(u"https://www.example.com/", view()->GetText());
  EXPECT_FALSE(model()->user_input_in_progress());
  EXPECT_TRUE(view()->IsSelectAll());

  // Test that we don't clobber the unelided text with inline autocomplete text.
  EXPECT_EQ(std::u16string(), view()->inline_autocompletion());
  model()->StartZeroSuggestRequest();
  model()->OnPopupDataChanged(std::u16string(), /*is_temporary_text=*/false,
                              std::u16string(), std::u16string(),
                              std::u16string(), false, std::u16string(), {});
  EXPECT_EQ(u"https://www.example.com/", view()->GetText());
  EXPECT_FALSE(model()->user_input_in_progress());
  EXPECT_TRUE(view()->IsSelectAll());
}
#endif  // !BUILDFLAG(IS_IOS)

TEST_F(OmniboxEditModelTest, RevertZeroSuggestTemporaryText) {
  location_bar_model()->set_url(GURL("https://www.example.com/"));
  location_bar_model()->set_url_for_display(u"https://www.example.com/");

  EXPECT_TRUE(model()->ResetDisplayTexts());
  model()->Revert();

  // Simulate getting ZeroSuggestions and arrowing to a different match.
  view()->SelectAll(true);
  model()->StartZeroSuggestRequest();
  model()->OnPopupDataChanged(u"fake_temporary_text",
                              /*is_temporary_text=*/true, std::u16string(),
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});

  // Test that reverting brings back the original input text.
  EXPECT_TRUE(model()->OnEscapeKeyPressed());
  EXPECT_EQ(u"https://www.example.com/", view()->GetText());
  EXPECT_FALSE(model()->user_input_in_progress());
  EXPECT_TRUE(view()->IsSelectAll());
}

// This verifies the fix for a bug where calling OpenMatch() with a valid
// alternate nav URL would fail a DCHECK if the input began with "http://".
// The failure was due to erroneously trying to strip the scheme from the
// resulting fill_into_edit.  Alternate nav matches are never shown, so there's
// no need to ever try and strip this scheme.
TEST_F(OmniboxEditModelTest, AlternateNavHasHTTP) {
  AutocompleteMatch match(
      controller()->autocomplete_controller()->search_provider(), 0, false,
      AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED);
  // |match.destination_url| has to be set to ensure that OnAutocompleteAccept
  // is called and |alternate_nav_match| is populated.
  match.destination_url = GURL("https://foo/");
  const GURL alternate_nav_url("http://abcd/");

  AutocompleteMatch alternate_nav_match;
  EXPECT_CALL(*omnibox_client_,
              OnAutocompleteAccept(_, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(SaveArg<10>(&alternate_nav_match));

  model()->OnSetFocus(false);  // Avoids DCHECK in OpenMatch().
  model()->SetUserText(u"http://abcd");
  model()->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB,
                               alternate_nav_url, std::u16string(), 0);
  EXPECT_TRUE(
      AutocompleteInput::HasHTTPScheme(alternate_nav_match.fill_into_edit));

  EXPECT_CALL(*omnibox_client_,
              OnAutocompleteAccept(_, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(SaveArg<10>(&alternate_nav_match));

  model()->SetUserText(u"abcd");
  model()->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB,
                               alternate_nav_url, std::u16string(), 0);
  EXPECT_TRUE(
      AutocompleteInput::HasHTTPScheme(alternate_nav_match.fill_into_edit));
}

TEST_F(OmniboxEditModelTest, CurrentMatch) {
  // Test the HTTP case.
  {
    location_bar_model()->set_url(GURL("http://www.example.com/"));
    location_bar_model()->set_url_for_display(u"example.com");
    model()->ResetDisplayTexts();
    model()->Revert();

    // iOS doesn't do elision in the textfield view.
#if BUILDFLAG(IS_IOS)
    EXPECT_EQ(u"http://www.example.com/", view()->GetText());
#else
    EXPECT_EQ(u"example.com", view()->GetText());
#endif

    AutocompleteMatch match = model()->CurrentMatch(nullptr);
    EXPECT_EQ(AutocompleteMatchType::URL_WHAT_YOU_TYPED, match.type);
    EXPECT_TRUE(model()->CurrentTextIsURL());
    EXPECT_EQ("http://www.example.com/", match.destination_url.spec());
  }

  // Test that generating a match from an elided HTTPS URL doesn't drop the
  // secure scheme.
  {
    location_bar_model()->set_url(GURL("https://www.google.com/"));
    location_bar_model()->set_url_for_display(u"google.com");
    model()->ResetDisplayTexts();
    model()->Revert();

    // iOS doesn't do elision in the textfield view.
#if BUILDFLAG(IS_IOS)
    EXPECT_EQ(u"https://www.google.com/", view()->GetText());
#else
    EXPECT_EQ(u"google.com", view()->GetText());
#endif

    AutocompleteMatch match = model()->CurrentMatch(nullptr);
    EXPECT_EQ(AutocompleteMatchType::URL_WHAT_YOU_TYPED, match.type);
    EXPECT_TRUE(model()->CurrentTextIsURL());

    // Additionally verify we aren't accidentally dropping the HTTPS scheme.
    EXPECT_EQ("https://www.google.com/", match.destination_url.spec());
  }
}

TEST_F(OmniboxEditModelTest, DisplayText) {
  location_bar_model()->set_url(GURL("https://www.example.com/"));
  location_bar_model()->set_url_for_display(u"example.com");

  EXPECT_TRUE(model()->ResetDisplayTexts());
  model()->Revert();

  EXPECT_TRUE(model()->CurrentTextIsURL());

#if BUILDFLAG(IS_IOS)
  // iOS OmniboxEditModel always provides the full URL as the OmniboxView
  // permanent display text. Unelision should return false.
  EXPECT_EQ(u"https://www.example.com/", model()->GetPermanentDisplayText());
  EXPECT_EQ(u"https://www.example.com/", view()->GetText());
  EXPECT_FALSE(model()->Unelide());
  EXPECT_FALSE(model()->user_input_in_progress());
  EXPECT_FALSE(view()->IsSelectAll());
#else
  // Verify we can unelide and show the full URL properly.
  EXPECT_EQ(u"example.com", model()->GetPermanentDisplayText());
  EXPECT_EQ(u"example.com", view()->GetText());
  EXPECT_TRUE(model()->Unelide());
  EXPECT_FALSE(model()->user_input_in_progress());
  EXPECT_TRUE(view()->IsSelectAll());
#endif

  EXPECT_EQ(u"https://www.example.com/", view()->GetText());
  EXPECT_TRUE(model()->CurrentTextIsURL());

  // We should still show the current page's icon until the URL is modified.
  EXPECT_TRUE(model()->ShouldShowCurrentPageIcon());
  view()->SetUserText(u"something else");
  EXPECT_FALSE(model()->ShouldShowCurrentPageIcon());
}

TEST_F(OmniboxEditModelTest, UnelideDoesNothingWhenFullURLAlreadyShown) {
  location_bar_model()->set_url(GURL("https://www.example.com/"));
  location_bar_model()->set_url_for_display(u"https://www.example.com/");

  EXPECT_TRUE(model()->ResetDisplayTexts());
  model()->Revert();

  EXPECT_EQ(u"https://www.example.com/", model()->GetPermanentDisplayText());
  EXPECT_TRUE(model()->CurrentTextIsURL());

  // Verify Unelide does nothing.
  EXPECT_FALSE(model()->Unelide());
  EXPECT_EQ(u"https://www.example.com/", view()->GetText());
  EXPECT_FALSE(model()->user_input_in_progress());
  EXPECT_FALSE(view()->IsSelectAll());
  EXPECT_TRUE(model()->CurrentTextIsURL());
  EXPECT_TRUE(model()->ShouldShowCurrentPageIcon());
}

// The tab-switching system sometimes focuses the Omnibox even if it was not
// previously focused. In those cases, ignore the saved focus state.
TEST_F(OmniboxEditModelTest, IgnoreInvalidSavedFocusStates) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures(
      {}, {omnibox::kOmniboxRestoreInvisibleFocusOnly});

  // The Omnibox starts out unfocused. Save that state.
  ASSERT_FALSE(model()->has_focus());
  OmniboxEditModel::State state = model()->GetStateForTabSwitch();
  ASSERT_EQ(OMNIBOX_FOCUS_NONE, state.focus_state);

  // Simulate the tab-switching system focusing the Omnibox.
  model()->OnSetFocus(false);

  // Restoring the old saved state should not clobber the model's focus state.
  model()->RestoreState(&state);
  EXPECT_TRUE(model()->has_focus());
  EXPECT_TRUE(model()->is_caret_visible());
}

TEST_F(OmniboxEditModelTest, RestoreInvisibleFocusOnlyForVisibleState) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures(
      {omnibox::kOmniboxRestoreInvisibleFocusOnly}, {});

  // The Omnibox starts out focused. Save that state.
  model()->OnSetFocus(false);
  ASSERT_TRUE(model()->has_focus());
  OmniboxEditModel::State state = model()->GetStateForTabSwitch();
  ASSERT_EQ(OMNIBOX_FOCUS_VISIBLE, state.focus_state);

  // Remove focus from the Omnibox and confirm it no longer has focus.
  model()->OnKillFocus();
  ASSERT_FALSE(model()->has_focus());

  // Restoring the old saved state should not clobber the model's focus state.
  model()->RestoreState(&state);
  EXPECT_FALSE(model()->has_focus());
}

TEST_F(OmniboxEditModelTest, RestoreInvisibleFocusOnlyForInvisibleState) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures(
      {omnibox::kOmniboxRestoreInvisibleFocusOnly}, {});

  // The Omnibox starts out invisibly focused. Save that state.
  model()->OnSetFocus(false);
  model()->SetCaretVisibility(false);
  ASSERT_TRUE(model()->has_focus());
  OmniboxEditModel::State state = model()->GetStateForTabSwitch();
  ASSERT_EQ(OMNIBOX_FOCUS_INVISIBLE, state.focus_state);

  // Remove focus from the Omnibox and confirm it no longer has focus.
  model()->OnKillFocus();
  ASSERT_FALSE(model()->has_focus());

  // Restoring the old saved state should clobber the model's focus state.
  model()->RestoreState(&state);
  EXPECT_TRUE(model()->has_focus());
}

// Tests ConsumeCtrlKey() consumes ctrl key when down, but does not affect ctrl
// state otherwise.
TEST_F(OmniboxEditModelTest, ConsumeCtrlKey) {
  model()->control_key_state_ = TestOmniboxEditModel::UP;
  model()->ConsumeCtrlKey();
  EXPECT_EQ(model()->control_key_state_, TestOmniboxEditModel::UP);
  model()->control_key_state_ = TestOmniboxEditModel::DOWN;
  model()->ConsumeCtrlKey();
  EXPECT_EQ(model()->control_key_state_,
            TestOmniboxEditModel::DOWN_AND_CONSUMED);
  model()->ConsumeCtrlKey();
  EXPECT_EQ(model()->control_key_state_,
            TestOmniboxEditModel::DOWN_AND_CONSUMED);
}

// Tests ctrl_key_state_ is set consumed if the ctrl key is down on focus.
TEST_F(OmniboxEditModelTest, ConsumeCtrlKeyOnRequestFocus) {
  model()->control_key_state_ = TestOmniboxEditModel::DOWN;
  model()->OnSetFocus(false);
  EXPECT_EQ(model()->control_key_state_, TestOmniboxEditModel::UP);
  model()->OnSetFocus(true);
  EXPECT_EQ(model()->control_key_state_,
            TestOmniboxEditModel::DOWN_AND_CONSUMED);
}

// Tests the ctrl key is consumed on a ctrl-action (e.g. ctrl-c to copy)
TEST_F(OmniboxEditModelTest, ConsumeCtrlKeyOnCtrlAction) {
  model()->control_key_state_ = TestOmniboxEditModel::DOWN;
  OmniboxView::StateChanges state_changes{nullptr, nullptr, 0,     0,
                                          false,   false,   false, false};
  model()->OnAfterPossibleChange(state_changes, false);
  EXPECT_EQ(model()->control_key_state_,
            TestOmniboxEditModel::DOWN_AND_CONSUMED);
}

TEST_F(OmniboxEditModelTest, KeywordModePreservesInlineAutocompleteText) {
  // Set the edit model into an inline autocompletion state.
  view()->SetUserText(u"user");
  view()->OnInlineAutocompleteTextMaybeChanged(u"user", u" text");

  // Entering keyword search mode should preserve the full display text as the
  // user text, and select all.
  model()->EnterKeywordModeForDefaultSearchProvider(
      OmniboxEventProto::KEYBOARD_SHORTCUT);
  EXPECT_EQ(u"user text", model()->GetUserTextForTesting());
  EXPECT_EQ(u"user text", view()->GetText());
  EXPECT_TRUE(view()->IsSelectAll());

  // Deleting the user text (exiting keyword) mode should clear everything.
  view()->SetUserText(std::u16string());
  {
    EXPECT_TRUE(view()->GetText().empty());
    EXPECT_TRUE(model()->GetUserTextForTesting().empty());
    size_t start = 0, end = 0;
    view()->GetSelectionBounds(&start, &end);
    EXPECT_EQ(0U, start);
    EXPECT_EQ(0U, end);
  }
}

TEST_F(OmniboxEditModelTest, KeywordModePreservesTemporaryText) {
  // Set the edit model into a temporary text state.
  view()->SetUserText(u"user text");
  GURL destination_url("http://example.com");

  // OnPopupDataChanged() is called when the user focuses a suggestion.
  model()->OnPopupDataChanged(u"match text",
                              /*is_temporary_text=*/true, std::u16string(),
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});

  // Entering keyword search mode should preserve temporary text as the user
  // text, and select all.
  model()->EnterKeywordModeForDefaultSearchProvider(
      OmniboxEventProto::KEYBOARD_SHORTCUT);
  EXPECT_EQ(u"match text", model()->GetUserTextForTesting());
  EXPECT_EQ(u"match text", view()->GetText());
  EXPECT_TRUE(view()->IsSelectAll());
}

TEST_F(OmniboxEditModelTest, CtrlEnterNavigatesToDesiredTLD) {
  // Set the edit model into an inline autocomplete state.
  view()->SetUserText(u"foo");
  model()->StartAutocomplete(false, false);
  view()->OnInlineAutocompleteTextMaybeChanged(u"foo", u"bar");

  model()->OnControlKeyChanged(true);
  model()->OpenSelection();
  OmniboxEditModel::State state = model()->GetStateForTabSwitch();
  EXPECT_EQ(GURL("http://www.foo.com/"),
            state.autocomplete_input.canonicalized_url());
}

TEST_F(OmniboxEditModelTest, CtrlEnterNavigatesToDesiredTLDTemporaryText) {
  // But if it's the temporary text, the View text should be used.
  view()->SetUserText(u"foo");
  model()->StartAutocomplete(false, false);
  model()->OnPopupDataChanged(u"foobar",
                              /*is_temporary_text=*/true, std::u16string(),
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});

  model()->OnControlKeyChanged(true);
  model()->OpenSelection();
  OmniboxEditModel::State state = model()->GetStateForTabSwitch();
  EXPECT_EQ(GURL("http://www.foobar.com/"),
            state.autocomplete_input.canonicalized_url());
}

TEST_F(OmniboxEditModelTest,
       CtrlEnterNavigatesToDesiredTLDSteadyStateElisions) {
  location_bar_model()->set_url(GURL("https://www.example.com/"));
  location_bar_model()->set_url_for_display(u"example.com");

  EXPECT_TRUE(model()->ResetDisplayTexts());
  model()->Revert();

  model()->OnControlKeyChanged(true);
  model()->OpenSelection();
  OmniboxEditModel::State state = model()->GetStateForTabSwitch();
  EXPECT_EQ(GURL("https://www.example.com/"),
            state.autocomplete_input.canonicalized_url());
}

///////////////////////////////////////////////////////////////////////////////
// Popup-related tests

class OmniboxEditModelPopupTest : public ::testing::Test {
 public:
  OmniboxEditModelPopupTest() {
#if BUILDFLAG(ENABLE_EXTENSIONS)
    // `kExperimentalOmniboxLabs` feature flag has to be enabled
    // before the test client initialization for the `UnscopedExtensionProvider`
    // to be initialized. The provider is needed for
    // `GetIconForExtensionWithImageURL` test.
    feature_list_.InitAndEnableFeature(
        extensions_features::kExperimentalOmniboxLabs);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

    auto omnibox_client = std::make_unique<TestOmniboxClient>();
    EXPECT_CALL(*omnibox_client, GetPrefs())
        .WillRepeatedly(Return(pref_service()));

    view_ = std::make_unique<TestOmniboxView>(std::move(omnibox_client));
    view_->controller()->SetEditModelForTesting(
        std::make_unique<TestOmniboxEditModel>(view_->controller(), view_.get(),
                                               pref_service()));

    omnibox::RegisterProfilePrefs(pref_service_.registry());
    model()->set_popup_view(&popup_view_);
    model()->SetPopupIsOpen(true);
  }
  OmniboxEditModelPopupTest(const OmniboxEditModelPopupTest&) = delete;
  OmniboxEditModelPopupTest& operator=(const OmniboxEditModelPopupTest&) =
      delete;

  TestingPrefServiceSimple* pref_service() { return &pref_service_; }
  OmniboxTriggeredFeatureService* triggered_feature_service() {
    return &triggered_feature_service_;
  }
  TestOmniboxEditModel* model() {
    return static_cast<TestOmniboxEditModel*>(view_->model());
  }
  OmniboxController* controller() { return view_->controller(); }
  TestOmniboxClient* client() {
    return static_cast<TestOmniboxClient*>(controller()->client());
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple pref_service_;
  std::unique_ptr<TestOmniboxView> view_;
  TestOmniboxPopupView popup_view_;
  OmniboxTriggeredFeatureService triggered_feature_service_;
};

// This verifies that the new treatment of the user's selected match in
// |SetSelectedLine()| with removed |AutocompleteResult::Selection::empty()|
// is correct in the face of various replacement versions of |empty()|.
TEST_F(OmniboxEditModelPopupTest, SetSelectedLine) {
  ACMatches matches;
  for (size_t i = 0; i < 2; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::URL_WHAT_YOU_TYPED);
    match.keyword = u"match";
    match.allowed_to_be_default_match = true;
    matches.push_back(match);
  }
  auto* result = &controller()->autocomplete_controller()->published_result_;
  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_TRUE(model()->IsPopupSelectionOnInitialLine());
  model()->SetPopupSelection(Selection(0), true, false);
  EXPECT_TRUE(model()->IsPopupSelectionOnInitialLine());
  model()->SetPopupSelection(Selection(0), false, false);
  EXPECT_TRUE(model()->IsPopupSelectionOnInitialLine());
}

TEST_F(OmniboxEditModelPopupTest,
       GetPopupAccessibilityLabelForCurrentSelection_KeywordMode) {
  // Populate the TemplateURLService with starter pack entries.
  std::vector<std::unique_ptr<TemplateURLData>> turls =
      template_url_starter_pack_data::GetStarterPackEngines();
  for (auto& starter_turl : turls) {
    controller()->client()->GetTemplateURLService()->Add(
        std::make_unique<TemplateURL>(std::move(*starter_turl)));
  }

  // Populate the TemplateURLService with site search entries.
  TemplateURLData featured_data;
  featured_data.SetShortName(u"SiteSearch");
  featured_data.SetKeyword(u"@sitesearch");
  featured_data.SetURL("https://sitesearch.com");
  TemplateURL* turl = controller()->client()->GetTemplateURLService()->Add(
      std::make_unique<TemplateURL>(featured_data));
  ASSERT_TRUE(turl);

  TemplateURLData nonfeatured_data;
  nonfeatured_data.SetShortName(u"SiteSearch");
  nonfeatured_data.SetKeyword(u"sitesearch");
  nonfeatured_data.SetURL("https://sitesearch.com");
  TemplateURL* nonfeatured_turl =
      controller()->client()->GetTemplateURLService()->Add(
          std::make_unique<TemplateURL>(nonfeatured_data));
  ASSERT_TRUE(nonfeatured_turl);

  // Create matches
  AutocompleteMatch gemini_match(nullptr, 0, false,
                                 AutocompleteMatchType::STARTER_PACK);
  gemini_match.keyword = u"@gemini";
  gemini_match.associated_keyword =
      std::make_unique<AutocompleteMatch>(gemini_match);

  AutocompleteMatch sitesearch_featured_match(
      nullptr, 0, false, AutocompleteMatchType::FEATURED_ENTERPRISE_SEARCH);
  sitesearch_featured_match.keyword = u"@sitesearch";
  sitesearch_featured_match.associated_keyword =
      std::make_unique<AutocompleteMatch>(sitesearch_featured_match);

  AutocompleteMatch sitesearch_other_engine(
      nullptr, 0, false, AutocompleteMatchType::SEARCH_OTHER_ENGINE);
  sitesearch_other_engine.keyword = u"sitesearch";
  AutocompleteMatch sitesearch_nonfeatured_match(
      nullptr, 0, false, AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED);
  sitesearch_nonfeatured_match.keyword = u"google.com";
  sitesearch_nonfeatured_match.associated_keyword =
      std::make_unique<AutocompleteMatch>(sitesearch_other_engine);

  // Create a result with matches.
  ACMatches matches;
  matches.push_back(gemini_match);
  matches.push_back(sitesearch_featured_match);
  matches.push_back(sitesearch_nonfeatured_match);
  AutocompleteResult* result =
      &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  // Test cases.
  struct {
    int line;
    std::u16string input_text;
    std::u16string expected_label;
  } test_cases[] = {
      {0, u"@gemini", u"@gemini, Ask Gemini"},
      {1, u"@sitesearch", u"@sitesearch, Search SiteSearch"},
      {2, u"sitesearch", u"Search SiteSearch"},
  };

  int label_prefix_length = 0;
  for (const auto& test_case : test_cases) {
    model()->SetPopupSelection(OmniboxPopupSelection(
        test_case.line, OmniboxPopupSelection::KEYWORD_MODE));
    std::u16string label =
        model()->GetPopupAccessibilityLabelForCurrentSelection(
            test_case.input_text, true, &label_prefix_length);
    EXPECT_EQ(test_case.expected_label, label);
  }
}

TEST_F(OmniboxEditModelPopupTest, SetSelectedLineWithNoDefaultMatches) {
  // Creates a set of matches with NO matches allowed to be default.
  ACMatches matches;
  for (size_t i = 0; i < 2; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::URL_WHAT_YOU_TYPED);
    match.keyword = u"match";
    matches.push_back(match);
  }
  auto* result = &controller()->autocomplete_controller()->published_result_;
  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);

  model()->OnPopupResultChanged();
  EXPECT_EQ(Selection::kNoMatch, model()->GetPopupSelection().line);
  EXPECT_TRUE(model()->IsPopupSelectionOnInitialLine());

  model()->SetPopupSelection(Selection(0), false, false);
  EXPECT_EQ(0U, model()->GetPopupSelection().line);
  EXPECT_FALSE(model()->IsPopupSelectionOnInitialLine());

  model()->SetPopupSelection(Selection(1), false, false);
  EXPECT_EQ(1U, model()->GetPopupSelection().line);
  EXPECT_FALSE(model()->IsPopupSelectionOnInitialLine());

  model()->ResetPopupToInitialState();
  EXPECT_EQ(Selection::kNoMatch, model()->GetPopupSelection().line);
  EXPECT_TRUE(model()->IsPopupSelectionOnInitialLine());
}

TEST_F(OmniboxEditModelPopupTest, PopupPositionChanging) {
  ACMatches matches;
  for (size_t i = 0; i < 3; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::URL_WHAT_YOU_TYPED);
    match.keyword = u"match";
    match.allowed_to_be_default_match = true;
    matches.push_back(match);
  }
  auto* result = &controller()->autocomplete_controller()->published_result_;
  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_EQ(0u, model()->GetPopupSelection().line);
  // Test moving and wrapping down.
  for (size_t n : {1, 2, 0}) {
    model()->OnUpOrDownPressed(true, false);
    EXPECT_EQ(n, model()->GetPopupSelection().line);
  }
  // And down.
  for (size_t n : {2, 1, 0}) {
    model()->OnUpOrDownPressed(false, false);
    EXPECT_EQ(n, model()->GetPopupSelection().line);
  }
}

#if !(BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID))
TEST_F(OmniboxEditModelPopupTest, PopupStepSelection) {
  ACMatches matches;
  for (size_t i = 0; i < 6; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::URL_WHAT_YOU_TYPED);
    match.keyword = u"match";
    match.allowed_to_be_default_match = true;
    matches.push_back(match);
  }
  // Make the thumbs up/down selection available on match index 1.
  matches[1].type = AutocompleteMatchType::HISTORY_EMBEDDINGS;
  // Make match index 1 deletable to verify we can step to that.
  matches[1].deletable = true;
  // Make match index 2 only have an associated keyword to verify we can step
  // backwards into keyword search mode if keyword search button is enabled.
  matches[2].associated_keyword =
      std::make_unique<AutocompleteMatch>(matches.back());
  // Make match index 3 have an associated keyword, tab match, and deletable to
  // verify keyword mode doesn't override tab match and remove suggestion
  // buttons (as it does with button row disabled)
  matches[3].associated_keyword =
      std::make_unique<AutocompleteMatch>(matches.back());
  matches[3].has_tab_match = true;
  matches[3].deletable = true;
  // Make match index 4 have a suggestion_group_id to test header behavior.
  const auto kNewGroupId = omnibox::GROUP_PREVIOUS_SEARCH_RELATED;
  matches[4].suggestion_group_id = kNewGroupId;
  // Make match index 5 have a suggestion_group_id but no header text.
  matches[5].suggestion_group_id = omnibox::GROUP_HISTORY_CLUSTER;

  auto* result = &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  omnibox::GroupConfigMap suggestion_groups_map;
  suggestion_groups_map[kNewGroupId].set_header_text("header");
  suggestion_groups_map[omnibox::GROUP_HISTORY_CLUSTER].set_header_text("");

  // Do not set the original_group_id on purpose to test that default visibility
  // can be safely queried via OmniboxController::IsSuggestionGroupHidden().
  result->MergeSuggestionGroupsMap(suggestion_groups_map);

  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_EQ(0u, model()->GetPopupSelection().line);

  // Step by lines forward.
  for (size_t n : {1, 2, 3, 4, 5, 0}) {
    model()->OnUpOrDownPressed(true, false);
    EXPECT_EQ(n, model()->GetPopupSelection().line);
  }
  // Step by lines backward.
  for (size_t n : {5, 4, 3, 2, 1, 0}) {
    model()->OnUpOrDownPressed(false, false);
    EXPECT_EQ(n, model()->GetPopupSelection().line);
  }

  // Step by states forward.
  for (auto selection : {
           Selection(1, Selection::NORMAL),
           Selection(1, Selection::FOCUSED_BUTTON_THUMBS_UP),
           Selection(1, Selection::FOCUSED_BUTTON_THUMBS_DOWN),
           Selection(1, Selection::FOCUSED_BUTTON_REMOVE_SUGGESTION),
           Selection(2, Selection::NORMAL),
           Selection(2, Selection::KEYWORD_MODE),
           Selection(3, Selection::NORMAL),
           Selection(3, Selection::KEYWORD_MODE),
           Selection(3, Selection::FOCUSED_BUTTON_REMOVE_SUGGESTION),
           Selection(4, Selection::NORMAL),
           Selection(5, Selection::NORMAL),
           Selection(0, Selection::NORMAL),
       }) {
    model()->OnTabPressed(false);
    EXPECT_EQ(selection, model()->GetPopupSelection());
  }
  // Step by states backward. Unlike prior to suggestion button row, there is
  // no difference in behavior for KEYWORD mode moving forward or backward.
  for (auto selection : {
           Selection(5, Selection::NORMAL),
           Selection(4, Selection::NORMAL),
           Selection(3, Selection::FOCUSED_BUTTON_REMOVE_SUGGESTION),
           Selection(3, Selection::KEYWORD_MODE),
           Selection(3, Selection::NORMAL),
           Selection(2, Selection::KEYWORD_MODE),
           Selection(2, Selection::NORMAL),
           Selection(1, Selection::FOCUSED_BUTTON_REMOVE_SUGGESTION),
           Selection(1, Selection::FOCUSED_BUTTON_THUMBS_DOWN),
           Selection(1, Selection::FOCUSED_BUTTON_THUMBS_UP),
           Selection(1, Selection::NORMAL),
           Selection(0, Selection::NORMAL),
           Selection(5, Selection::NORMAL),
           Selection(4, Selection::NORMAL),
           Selection(3, Selection::FOCUSED_BUTTON_REMOVE_SUGGESTION),
       }) {
    model()->OnTabPressed(true);
    EXPECT_EQ(selection, model()->GetPopupSelection());
  }

  // Try the `kAllLines` step behavior.
  model()->OnUpOrDownPressed(false, true);
  EXPECT_EQ(Selection(0, Selection::NORMAL), model()->GetPopupSelection());
  model()->OnUpOrDownPressed(true, true);
  EXPECT_EQ(Selection(5, Selection::NORMAL), model()->GetPopupSelection());
}
#endif  // !(BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID))

// Actions are not part of the selection stepping in Android and iOS at all.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
TEST_F(OmniboxEditModelPopupTest, PopupStepSelectionWithActions) {
  ACMatches matches;
  for (size_t i = 0; i < 4; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::URL_WHAT_YOU_TYPED);
    match.keyword = u"match";
    match.allowed_to_be_default_match = true;
    matches.push_back(match);
  }
  // The second match has a normal action.
  matches[1].actions.push_back(base::MakeRefCounted<OmniboxAction>(
      OmniboxAction::LabelStrings(), GURL()));
  // The fourth match has an action that takes over the match.
  matches[3].takeover_action = base::MakeRefCounted<OmniboxAction>(
      OmniboxAction::LabelStrings(), GURL());

  auto* result = &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_EQ(0u, model()->GetPopupSelection().line);

  // Step by lines forward.
  for (size_t n : {1, 2, 3, 0}) {
    model()->OnUpOrDownPressed(true, false);
    EXPECT_EQ(n, model()->GetPopupSelection().line);
  }
  // Step by lines backward.
  for (size_t n : {3, 2, 1, 0}) {
    model()->OnUpOrDownPressed(false, false);
    EXPECT_EQ(n, model()->GetPopupSelection().line);
  }

  // Step by states forward.
  for (auto selection : {
           Selection(1, Selection::NORMAL),
           Selection(1, Selection::FOCUSED_BUTTON_ACTION),
           Selection(2, Selection::NORMAL),
           Selection(3, Selection::NORMAL),
           Selection(0, Selection::NORMAL),
       }) {
    model()->OnTabPressed(false);
    EXPECT_EQ(selection, model()->GetPopupSelection());
  }
  // Step by states backward.
  for (auto selection : {
           Selection(3, Selection::NORMAL),
           Selection(2, Selection::NORMAL),
           Selection(1, Selection::FOCUSED_BUTTON_ACTION),
           Selection(1, Selection::NORMAL),
           Selection(0, Selection::NORMAL),
       }) {
    model()->OnTabPressed(true);
    EXPECT_EQ(selection, model()->GetPopupSelection());
  }

  // Try the `kAllLines` step behavior.
  model()->OnUpOrDownPressed(false, true);
  EXPECT_EQ(Selection(0, Selection::NORMAL), model()->GetPopupSelection());
  model()->OnUpOrDownPressed(true, true);
  EXPECT_EQ(Selection(3, Selection::NORMAL), model()->GetPopupSelection());
}
#endif

TEST_F(OmniboxEditModelPopupTest, PopupInlineAutocompleteAndTemporaryText) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(omnibox::kGroupingFrameworkForNonZPS);
  // Create a set of three matches "a|1" (inline autocompleted), "a2", "a3".
  // The third match has a suggestion group ID.
  ACMatches matches;
  for (size_t i = 0; i < 3; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::SEARCH_SUGGEST);
    match.allowed_to_be_default_match = true;
    matches.push_back(match);
  }

  matches[0].fill_into_edit = u"a1";
  matches[0].inline_autocompletion = u"1";
  matches[1].fill_into_edit = u"a2";
  matches[2].fill_into_edit = u"a3";
  const auto kNewGroupId = omnibox::GROUP_PREVIOUS_SEARCH_RELATED;
  matches[2].suggestion_group_id = kNewGroupId;

  auto* result = &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  omnibox::GroupConfigMap suggestion_groups_map;
  suggestion_groups_map[kNewGroupId].set_header_text("header");
  // Do not set the original_group_id on purpose to test that default visibility
  // can be safely queried via AutocompleteResult::IsSuggestionGroupHidden().
  result->MergeSuggestionGroupsMap(suggestion_groups_map);

  AutocompleteInput input(u"a", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();

  // Simulate OmniboxController updating the popup, then check initial state.
  model()->OnPopupDataChanged(std::u16string(),
                              /*is_temporary_text=*/false, u"1",
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});
  EXPECT_EQ(Selection(0, Selection::NORMAL), model()->GetPopupSelection());
  EXPECT_EQ(u"1", model()->text());
  EXPECT_FALSE(model()->is_temporary_text());

  // Tab down to second match.
  model()->OnTabPressed(false);
  EXPECT_EQ(Selection(1, Selection::NORMAL), model()->GetPopupSelection());
  EXPECT_EQ(u"a2", model()->text());
  EXPECT_TRUE(model()->is_temporary_text());

  // Now tab down to the third match, and expect that we update the temporary
  // text to the third match.
  model()->OnTabPressed(false);
  EXPECT_EQ(Selection(2, Selection::NORMAL), model()->GetPopupSelection());
  EXPECT_EQ(u"a3", model()->text());
  EXPECT_TRUE(model()->is_temporary_text());

  // Now tab backwards to the second match, expect we update the temporary text
  // to the second match.
  model()->OnTabPressed(true);
  EXPECT_EQ(Selection(1, Selection::NORMAL), model()->GetPopupSelection());
  EXPECT_EQ(u"a2", model()->text());
  EXPECT_TRUE(model()->is_temporary_text());
}

// Makes sure focus remains on the tab switch button when nothing changes,
// and leaves when it does. Exercises the ratcheting logic in
// OmniboxEditModel::OnPopupResultChanged().
TEST_F(OmniboxEditModelPopupTest, TestFocusFixing) {
  ACMatches matches;
  AutocompleteMatch match(nullptr, 1000, false,
                          AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  match.contents = u"match1.com";
  match.destination_url = GURL("http://match1.com");
  match.allowed_to_be_default_match = true;
  match.has_tab_match = true;
  matches.push_back(match);

  auto* result = &controller()->autocomplete_controller()->published_result_;
  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  model()->SetPopupSelection(Selection(0), true, false);
  // The default state should be unfocused.
  EXPECT_EQ(Selection::NORMAL, model()->GetPopupSelection().state);

  // Focus the selection.
  model()->SetPopupSelection(Selection(0, Selection::FOCUSED_BUTTON_ACTION));
  EXPECT_EQ(Selection::FOCUSED_BUTTON_ACTION,
            model()->GetPopupSelection().state);

  // Adding a match at end won't change that we selected first suggestion, so
  // shouldn't change focused state.
  matches[0].relevance = 999;
  // Give it a different name so not deduped.
  matches[0].contents = u"match2.com";
  matches[0].destination_url = GURL("http://match2.com");
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_EQ(Selection::FOCUSED_BUTTON_ACTION,
            model()->GetPopupSelection().state);

  // Changing selection should change focused state.
  model()->SetPopupSelection(Selection(1));
  EXPECT_EQ(Selection::NORMAL, model()->GetPopupSelection().state);

  // Adding a match at end will reset selection to first, so should change
  // selected line, and thus focus.
  model()->SetPopupSelection(Selection(model()->GetPopupSelection().line,
                                       Selection::FOCUSED_BUTTON_ACTION));
  matches[0].relevance = 999;
  matches[0].contents = u"match3.com";
  matches[0].destination_url = GURL("http://match3.com");
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_EQ(0U, model()->GetPopupSelection().line);
  EXPECT_EQ(Selection::NORMAL, model()->GetPopupSelection().state);

  // Prepending a match won't change selection, but since URL is different,
  // should clear the focus state.
  model()->SetPopupSelection(Selection(model()->GetPopupSelection().line,
                                       Selection::FOCUSED_BUTTON_ACTION));
  matches[0].relevance = 1100;
  matches[0].contents = u"match4.com";
  matches[0].destination_url = GURL("http://match4.com");
  result->AppendMatches(matches);
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  EXPECT_EQ(0U, model()->GetPopupSelection().line);
  EXPECT_EQ(Selection::NORMAL, model()->GetPopupSelection().state);

  // Selecting |kNoMatch| should clear focus.
  model()->SetPopupSelection(Selection(model()->GetPopupSelection().line,
                                       Selection::FOCUSED_BUTTON_ACTION));
  model()->SetPopupSelection(Selection(Selection::kNoMatch));
  model()->OnPopupResultChanged();
  EXPECT_EQ(Selection::NORMAL, model()->GetPopupSelection().state);
}

// Android and iOS handle actions and metrics differently from other platforms.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
TEST_F(OmniboxEditModelPopupTest, OpenActionSelectionLogsOmniboxEvent) {
  base::HistogramTester histogram_tester;
  ACMatches matches;
  for (size_t i = 0; i < 4; ++i) {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::URL_WHAT_YOU_TYPED);
    match.keyword = u"match";
    match.allowed_to_be_default_match = true;
    matches.push_back(match);
  }
  const GURL url = GURL("http://kong-foo.com");
  matches[1].destination_url = url;
  matches[1].provider =
      controller()->autocomplete_controller()->search_provider();
  matches[1].actions.push_back(base::MakeRefCounted<TabSwitchAction>(url));
  AutocompleteResult* result =
      &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);
  AutocompleteInput input(u"match", metrics::OmniboxEventProto::NTP,
                          TestSchemeClassifier());
  result->SortAndCull(input, /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);
  model()->OnPopupResultChanged();
  model()->OpenSelection(
      OmniboxPopupSelection(1, OmniboxPopupSelection::FOCUSED_BUTTON_ACTION));
  EXPECT_EQ(client()->last_log_disposition(),
            WindowOpenDisposition::SWITCH_TO_TAB);
  histogram_tester.ExpectUniqueSample("Omnibox.EventCount", 1, 1);
}
#endif

TEST_F(OmniboxEditModelPopupTest, OpenThumbsDownSelectionShowsFeedback) {
  // Set the input on the controller.
  controller()->autocomplete_controller()->input_ = AutocompleteInput(
      u"a", metrics::OmniboxEventProto::NTP, TestSchemeClassifier());

  // Set the matches on the controller.
  ACMatches matches;
  {
    AutocompleteMatch match(nullptr, 1000, false,
                            AutocompleteMatchType::SEARCH_SUGGEST);
    match.allowed_to_be_default_match = true;
    match.fill_into_edit = u"a1";
    match.inline_autocompletion = u"1";
    matches.push_back(match);
  }
  {
    AutocompleteMatch match(nullptr, 999, false,
                            AutocompleteMatchType::HISTORY_EMBEDDINGS);
    match.fill_into_edit = u"a2";
    match.destination_url = GURL("https://foo/");
    matches.push_back(match);
  }
  auto* result = &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);
  result->SortAndCull(controller()->autocomplete_controller()->input_,
                      /*template_url_service=*/nullptr,
                      triggered_feature_service(), /*is_lens_active=*/false,
                      /*can_show_contextual_suggestions=*/false,
                      /*mia_enabled*/ false);

  // Inform the model of the controller result set changes.
  model()->OnPopupResultChanged();

  // Simulate OmniboxController updating the popup, then check initial state.
  model()->OnPopupDataChanged(std::u16string(),
                              /*is_temporary_text=*/false, u"a1",
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});
  EXPECT_EQ(Selection(0, Selection::NORMAL), model()->GetPopupSelection());
  EXPECT_EQ(u"a1", model()->text());
  EXPECT_FALSE(model()->is_temporary_text());

  // Tab down to second match.
  model()->OnTabPressed(false);
  EXPECT_EQ(Selection(1, Selection::NORMAL), model()->GetPopupSelection());
  EXPECT_EQ(u"a2", model()->text());
  EXPECT_TRUE(model()->is_temporary_text());

  // Tab to focus the thumbs up button.
  model()->OnTabPressed(false);
  EXPECT_EQ(Selection(1, Selection::FOCUSED_BUTTON_THUMBS_UP),
            model()->GetPopupSelection());
  EXPECT_EQ(u"a2", model()->text());
  EXPECT_TRUE(model()->is_temporary_text());

  EXPECT_EQ(FeedbackType::kNone, result->match_at(1)->feedback_type);

  // Simulate pressing the thumbs up button.
  model()->OpenSelection(OmniboxPopupSelection(
      1, OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_UP));
  EXPECT_EQ(FeedbackType::kThumbsUp, result->match_at(1)->feedback_type);

  // Tab to focus the thumbs down button.
  model()->OnTabPressed(false);
  EXPECT_EQ(Selection(1, Selection::FOCUSED_BUTTON_THUMBS_DOWN),
            model()->GetPopupSelection());
  EXPECT_EQ(u"a2", model()->text());
  EXPECT_TRUE(model()->is_temporary_text());

  // Verify feedback form is requested only once.
  std::u16string input_text;
  GURL destination_url;
  EXPECT_CALL(*client(), ShowFeedbackPage(_, _))
      .Times(1)
      .WillOnce(DoAll(SaveArg<0>(&input_text), SaveArg<1>(&destination_url)));

  // Simulate pressing the thumbs down button.
  model()->OpenSelection(OmniboxPopupSelection(
      1, OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_DOWN));
  EXPECT_EQ(FeedbackType::kThumbsDown, result->match_at(1)->feedback_type);
  EXPECT_EQ(u"a", input_text);
  EXPECT_EQ("https://foo/", destination_url.spec());

  // Simulate pressing the thumbs down button.
  model()->OpenSelection(OmniboxPopupSelection(
      1, OmniboxPopupSelection::FOCUSED_BUTTON_THUMBS_DOWN));
  EXPECT_EQ(FeedbackType::kNone, result->match_at(1)->feedback_type);
}

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
// Tests the `GetMatchIcon()` method, verifying that a page favicon is used for
// `URL_WHAT_YOU_TYPED` matches.
TEST_F(OmniboxEditModelPopupTest,
       GetMatchIconForUrlWhatYouTypedUsesPageFavicon) {
  const GURL kUrl("https://foo.com");

  GURL page_url;
  EXPECT_CALL(*client(), GetFaviconForPageUrl(_, _))
      .WillOnce(DoAll(SaveArg<0>(&page_url), Return(gfx::Image())));
  EXPECT_CALL(*client(), GetFaviconForKeywordSearchProvider(_, _)).Times(0);

  AutocompleteMatch match;
  match.type = AutocompleteMatchType::URL_WHAT_YOU_TYPED;
  match.destination_url = kUrl;

  gfx::Image image = model()->GetMatchIcon(match, 0);
  EXPECT_EQ(page_url, kUrl);
}

// Tests the `GetMatchIcon()` method, verifying that a keyword favicon is used
// for `FEATURED_ENTERPRISE_SEARCH` matches with `kSiteSearch` policy origin.
TEST_F(OmniboxEditModelPopupTest,
       GetMatchIconForFeaturedEnterpriseSiteSearchUsesKeywordFavicon) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(SK_ColorRED);
  gfx::Image expected_image =
      gfx::Image(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));

  EXPECT_CALL(*client(), GetFaviconForPageUrl(_, _)).Times(0);
  EXPECT_CALL(*client(), GetFaviconForKeywordSearchProvider(_, _))
      .WillOnce(Return(expected_image));

  TemplateURLData data;
  data.SetKeyword(u"sitesearch");
  data.SetURL("https://sitesearch.com");
  data.featured_by_policy = true;
  data.policy_origin = TemplateURLData::PolicyOrigin::kSiteSearch;
  TemplateURL* turl = controller()->client()->GetTemplateURLService()->Add(
      std::make_unique<TemplateURL>(data));
  ASSERT_TRUE(turl);

  AutocompleteMatch match;
  match.type = AutocompleteMatchType::FEATURED_ENTERPRISE_SEARCH;
  match.destination_url = GURL("https://sitesearch.com");
  match.keyword = u"sitesearch";
  match.associated_keyword = std::make_unique<AutocompleteMatch>(match);

  gfx::Image image = model()->GetMatchIcon(match, 0);
  gfx::test::CheckColors(bitmap.getColor(0, 0),
                         image.ToSkBitmap()->getColor(0, 0));
}

// Tests the `GetMatchIcon()` method, verifying that no favicon is used for
// `FEATURED_ENTERPRISE_SEARCH` matches with `kSearchAggregator` policy origin.
TEST_F(OmniboxEditModelPopupTest,
       GetMatchIconForFeaturedEnterpriseSearchAggregator) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(SK_ColorRED);

  EXPECT_CALL(*client(), GetFaviconForPageUrl(_, _)).Times(0);
  EXPECT_CALL(*client(), GetFaviconForKeywordSearchProvider(_, _)).Times(0);

  TemplateURLData data;
  data.SetKeyword(u"searchaggregator");
  data.SetURL("https://searchaggregator.com");
  data.featured_by_policy = true;
  data.policy_origin = TemplateURLData::PolicyOrigin::kSearchAggregator;
  TemplateURL* turl = controller()->client()->GetTemplateURLService()->Add(
      std::make_unique<TemplateURL>(data));
  ASSERT_TRUE(turl);

  // Creates a set of matches.
  ACMatches matches;
  AutocompleteMatch search_aggregator_match(
      nullptr, 1350, false, AutocompleteMatchType::FEATURED_ENTERPRISE_SEARCH);
  search_aggregator_match.keyword = u"searchaggregator";
  search_aggregator_match.associated_keyword =
      std::make_unique<AutocompleteMatch>(search_aggregator_match);
  search_aggregator_match.icon_url = GURL("https://aggregator.com/icon.png");
  matches.push_back(search_aggregator_match);
  AutocompleteMatch url_match(nullptr, 1000, false,
                              AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  url_match.keyword = u"match";
  matches.push_back(url_match);
  AutocompleteResult* result =
      &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  // Sets the icon bitmap for search aggregator match.
  model()->SetIconBitmap(GURL("https://aggregator.com/icon.png"), bitmap);

  gfx::Image image = model()->GetMatchIcon(search_aggregator_match, 0);
  gfx::test::CheckColors(bitmap.getColor(0, 0),
                         image.ToSkBitmap()->getColor(0, 0));
}

// Tests the `GetMatchIcon()` method, verifying that the icon served by a URL,
// if one is supplied with a content suggestion, is returned.
TEST_F(OmniboxEditModelPopupTest,
       GetMatchIconForFeaturedEnterpriseSearchAggregatorContentSuggestion) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(SK_ColorBLUE);

  // Creates a set of matches.
  ACMatches matches;
  AutocompleteMatch content_match(nullptr, 1000, false,
                                  AutocompleteMatchType::NAVSUGGEST);
  content_match.icon_url = GURL("https://example.com/icon.png");
  matches.push_back(content_match);
  AutocompleteResult* result =
      &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  // Sets the icon bitmap for content match.
  model()->SetIconBitmap(GURL("https://example.com/icon.png"), bitmap);

  gfx::Image image = model()->GetMatchIcon(content_match, 0);
  gfx::test::CheckColors(bitmap.getColor(0, 0),
                         image.ToSkBitmap()->getColor(0, 0));
}
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

#if BUILDFLAG(ENABLE_EXTENSIONS)
// Tests the `GetMatchIcon()` method, verifying that the extension's icon is
// returned when no url is specified for the match.
TEST_F(OmniboxEditModelPopupTest, GetIconForExtensionWithNoImageURL) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(SK_ColorRED);
  gfx::Image expected_image =
      gfx::Image(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));

  TemplateURLData data;
  data.SetShortName(u"extension_name");
  data.SetKeyword(u"api");
  data.SetURL("https://extension.com");
  TemplateURL* turl = controller()->client()->GetTemplateURLService()->Add(
      std::make_unique<TemplateURL>(data, TemplateURL::OMNIBOX_API_EXTENSION,
                                    "extension_id", base::Time::Now(), false));
  ASSERT_TRUE(turl);

  EXPECT_CALL(*client(), GetExtensionIcon(_)).WillOnce(Return(expected_image));

  AutocompleteMatch match(
      controller()->autocomplete_controller()->unscoped_extension_provider(), 0,
      false, AutocompleteMatchType::SEARCH_OTHER_ENGINE);
  match.keyword = u"api";

  gfx::Image image = model()->GetMatchIcon(match, 0);
  gfx::test::CheckColors(bitmap.getColor(0, 0),
                         image.ToSkBitmap()->getColor(0, 0));
}

// Tests the `GetMatchIcon()` method, verifying that the favicon url from the
// extension match is returned. This simulates the case  when the suggestion
// from an extension has a `faviconUrl` set.
TEST_F(OmniboxEditModelPopupTest, GetIconForExtensionWithImageURL) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(SK_ColorRED);
  gfx::Image expected_image =
      gfx::Image(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));

  TemplateURLData data;
  data.SetShortName(u"extension_name");
  data.SetKeyword(u"api");
  data.SetURL("https://extension.com");
  TemplateURL* turl = controller()->client()->GetTemplateURLService()->Add(
      std::make_unique<TemplateURL>(data, TemplateURL::OMNIBOX_API_EXTENSION,
                                    "extension_id", base::Time::Now(), false));
  ASSERT_TRUE(turl);

  EXPECT_CALL(*client(), GetExtensionIcon(_)).Times(0);

  AutocompleteMatch match(
      controller()->autocomplete_controller()->unscoped_extension_provider(), 0,
      false, AutocompleteMatchType::SEARCH_OTHER_ENGINE);
  match.keyword = u"api";
  match.image_url = GURL("https://www.google-icon.com");
  match.provider =
      controller()->autocomplete_controller()->unscoped_extension_provider();

  // Creates a set of matches.
  ACMatches matches;
  matches.push_back(match);
  AutocompleteResult* result =
      &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  // Sets the popup rich suggestion bitmap for the extension match.
  model()->SetPopupRichSuggestionBitmap(0, bitmap);

  gfx::Image image = model()->GetMatchIcon(match, 0);
  gfx::test::CheckColors(bitmap.getColor(0, 0),
                         image.ToSkBitmap()->getColor(0, 0));
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

TEST_F(OmniboxEditModelTest, OmniboxEscapeHistogram) {
  // Escape should incrementally revert temporary text, close the popup, clear
  // input, and blur the omnibox.
  AutocompleteMatch match;
  match.type = AutocompleteMatchType::NAVSUGGEST;
  match.destination_url = GURL("https://google.com");
  model()->SetCurrentMatchForTest(match);

  view()->SetUserText(u"user text");
  model()->OnSetFocus(false);
  model()->SetInputInProgress(true);
  model()->SetPopupIsOpen(true);
  model()->OnPopupDataChanged(/*temporary_text=*/u"fake_temporary_text",
                              /*is_temporary_text=*/true, std::u16string(),
                              std::u16string(), std::u16string(), false,
                              std::u16string(), {});

  EXPECT_TRUE(model()->HasTemporaryText());
  EXPECT_TRUE(model()->PopupIsOpen());
  EXPECT_EQ(view()->GetText(), u"fake_temporary_text");
  EXPECT_TRUE(model()->user_input_in_progress());
  EXPECT_TRUE(model()->has_focus());

  {
    // Revert temporary text.
    base::HistogramTester histogram_tester;
    EXPECT_TRUE(model()->OnEscapeKeyPressed());
    histogram_tester.ExpectUniqueSample("Omnibox.Escape", 1, 1);
    EXPECT_FALSE(model()->HasTemporaryText());
    EXPECT_TRUE(model()->PopupIsOpen());
    EXPECT_EQ(view()->GetText(), u"");
    EXPECT_TRUE(model()->user_input_in_progress());
    EXPECT_TRUE(model()->has_focus());
  }

  {
    // Close the popup.
    base::HistogramTester histogram_tester;
    EXPECT_TRUE(model()->OnEscapeKeyPressed());
    histogram_tester.ExpectUniqueSample("Omnibox.Escape", 2, 1);
    model()->SetPopupIsOpen(false);  // `TestOmniboxEditModel` stubs the popup.
    EXPECT_FALSE(model()->HasTemporaryText());
    EXPECT_FALSE(model()->PopupIsOpen());
    EXPECT_EQ(view()->GetText(), u"");
    EXPECT_TRUE(model()->user_input_in_progress());
    EXPECT_TRUE(model()->has_focus());
  }

  {
    // Clear user input.
    base::HistogramTester histogram_tester;
    EXPECT_TRUE(model()->OnEscapeKeyPressed());
    histogram_tester.ExpectUniqueSample("Omnibox.Escape", 3, 1);
    EXPECT_FALSE(model()->HasTemporaryText());
    EXPECT_FALSE(model()->PopupIsOpen());
    EXPECT_EQ(view()->GetText(), u"");
    EXPECT_FALSE(model()->user_input_in_progress());
    EXPECT_TRUE(model()->has_focus());
  }

  {
    // Blur the omnibox.
    base::HistogramTester histogram_tester;
    EXPECT_TRUE(model()->OnEscapeKeyPressed());
    histogram_tester.ExpectUniqueSample("Omnibox.Escape", 5, 1);
    model()->OnKillFocus();  // `TestOmniboxEditModel` stubs the client which
                             // handles blurring the omnibox.
    EXPECT_FALSE(model()->HasTemporaryText());
    EXPECT_FALSE(model()->PopupIsOpen());
    EXPECT_EQ(view()->GetText(), u"");
    EXPECT_FALSE(model()->user_input_in_progress());
    EXPECT_FALSE(model()->has_focus());
  }
}

TEST_F(OmniboxEditModelTest, IPv4AddressPartsCount) {
  base::HistogramTester histogram_tester;
  constexpr char kIPv4AddressPartsCountHistogramName[] =
      "Omnibox.IPv4AddressPartsCount";
  // Hostnames shall not be recorded.
  OpenUrlFromEditBox(controller(), model(), u"http://example.com", false);
  histogram_tester.ExpectTotalCount(kIPv4AddressPartsCountHistogramName, 0);

  // Autocompleted navigations shall not be recorded.
  OpenUrlFromEditBox(controller(), model(), u"http://127.0.0.1", true);
  histogram_tester.ExpectTotalCount(kIPv4AddressPartsCountHistogramName, 0);

  // Test IPv4 parts are correctly counted.
  OpenUrlFromEditBox(controller(), model(), u"http://127.0.0.1", false);
  OpenUrlFromEditBox(controller(), model(), u"http://127.1/test.html", false);
  OpenUrlFromEditBox(controller(), model(), u"http://127.0.1", false);
  EXPECT_THAT(
      histogram_tester.GetAllSamples(kIPv4AddressPartsCountHistogramName),
      testing::ElementsAre(base::Bucket(2, 1), base::Bucket(3, 1),
                           base::Bucket(4, 1)));
}

#if !(BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID))
// The keyword mode feature is only available on Desktop. Do not test on mobile.
TEST_F(OmniboxEditModelTest, OpenTabMatch) {
  // When the match comes from the Open Tab Provider while in keyword mode,
  // the disposition should be set to SWITCH_TO_TAB.
  AutocompleteMatch match(
      controller()->autocomplete_controller()->open_tab_provider(), 0, false,
      AutocompleteMatchType::OPEN_TAB);
  match.destination_url = GURL("https://foo/");
  match.from_keyword = true;

  WindowOpenDisposition disposition;
  EXPECT_CALL(*omnibox_client_,
              OnAutocompleteAccept(_, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(SaveArg<2>(&disposition));

  model()->OnSetFocus(false);  // Avoids DCHECK in OpenMatch().
  model()->SetUserText(u"http://abcd");
  model()->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB,
                               GURL(), std::u16string(), 0);
  EXPECT_EQ(disposition, WindowOpenDisposition::SWITCH_TO_TAB);

  EXPECT_CALL(*omnibox_client_,
              OnAutocompleteAccept(_, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(SaveArg<2>(&disposition));

  // Suggestions not from the Open Tab Provider or not from keyword mode should
  // not change the disposition.
  match.from_keyword = false;
  model()->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB,
                               GURL(), std::u16string(), 0);
  EXPECT_EQ(disposition, WindowOpenDisposition::CURRENT_TAB);

  EXPECT_CALL(*omnibox_client_,
              OnAutocompleteAccept(_, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(SaveArg<2>(&disposition));

  match.provider = controller()->autocomplete_controller()->search_provider();
  match.from_keyword = true;
  model()->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB,
                               GURL(), std::u16string(), 0);
  EXPECT_EQ(disposition, WindowOpenDisposition::CURRENT_TAB);
}
#endif  // !(BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID))

TEST_F(OmniboxEditModelTest, LogAnswerUsed) {
  base::HistogramTester histogram_tester;
  AutocompleteMatch match(
      controller()->autocomplete_controller()->search_provider(), 0, false,
      AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED);
  match.answer_type = omnibox::ANSWER_TYPE_WEATHER;
  match.destination_url = GURL("https://foo");
  model()->OpenMatchForTesting(match, WindowOpenDisposition::CURRENT_TAB,
                               GURL(), std::u16string(), 0);
  histogram_tester.ExpectUniqueSample("Omnibox.SuggestionUsed.AnswerInSuggest",
                                      8, 1);
}

// Tests `GetPopupRichSuggestionBitmap()` method, verifying that no bitmap is
// fetched when there is no match with an `associated_keyword`.
TEST_F(OmniboxEditModelPopupTest,
       GetPopupRichSuggestionBitmapForMatchWithoutAssociatedKeyword) {
  // Setup match with no bitmap.
  ACMatches matches;
  AutocompleteMatch match_without_associated_keyword(
      nullptr, 1000, false, AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  match_without_associated_keyword.keyword =
      u"match_without_associated_keyword";
  matches.push_back(match_without_associated_keyword);
  auto* result = &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  const SkBitmap* actual_bitmap = model()->GetPopupRichSuggestionBitmap(
      u"match_without_associated_keyword");

  EXPECT_FALSE(actual_bitmap);
}

// Tests `GetPopupRichSuggestionBitmap()` method, verifying that the correct
// bitmap is fetched when there is a match with an `associated_keyword`.
TEST_F(OmniboxEditModelPopupTest,
       GetPopupRichSuggestionBitmapForMatchWithAssociatedKeyword) {
  SkBitmap expected_bitmap;
  expected_bitmap.allocN32Pixels(16, 16);
  expected_bitmap.eraseColor(SK_ColorRED);

  // Setup matches and add to result.
  ACMatches matches;
  AutocompleteMatch match_without_bitmap(
      nullptr, 1000, false, AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  match_without_bitmap.keyword = u"match_without_bitmap";
  match_without_bitmap.associated_keyword =
      std::make_unique<AutocompleteMatch>(match_without_bitmap);
  matches.push_back(match_without_bitmap);
  AutocompleteMatch match_with_bitmap(
      nullptr, 1000, false, AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  match_with_bitmap.keyword = u"match_with_bitmap";
  match_with_bitmap.associated_keyword =
      std::make_unique<AutocompleteMatch>(match_with_bitmap);
  matches.push_back(match_with_bitmap);
  auto* result = &controller()->autocomplete_controller()->published_result_;
  result->AppendMatches(matches);

  // Store bitmap for 'match_with_bitmap' match.
  model()->rich_suggestion_bitmaps_.insert({1, expected_bitmap});

  const SkBitmap* match_without_bitmap_bitmap =
      model()->GetPopupRichSuggestionBitmap(u"match_without_bitmap");
  EXPECT_FALSE(match_without_bitmap_bitmap);

  const SkBitmap* match_with_bitmap_bitmap =
      model()->GetPopupRichSuggestionBitmap(u"match_with_bitmap");
  EXPECT_TRUE(match_with_bitmap_bitmap);
  gfx::test::CheckColors(expected_bitmap.getColor(0, 0),
                         match_with_bitmap_bitmap->getColor(0, 0));
}
