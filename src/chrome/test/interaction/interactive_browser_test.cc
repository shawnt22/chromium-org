// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/interaction/interactive_browser_test.h"

#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/to_string.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/test_switches.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/interaction/interaction_test_util_browser.h"
#include "chrome/test/interaction/interactive_browser_test_internal.h"
#include "chrome/test/interaction/tracked_element_webcontents.h"
#include "chrome/test/interaction/webcontents_interaction_test_util.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/update_user_activation_state_interceptor.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/functional/overload.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom-shared.h"
#include "third_party/blink/public/mojom/frame/user_activation_update_types.mojom-shared.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/interaction/interaction_sequence.h"
#include "ui/base/interaction/interaction_test_util.h"
#include "ui/base/interaction/interactive_test_internal.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/interaction/interaction_test_util_views.h"
#include "ui/views/interaction/interactive_views_test.h"
#include "ui/views/views_delegate.h"

DEFINE_CLASS_CUSTOM_ELEMENT_EVENT_TYPE(InteractiveBrowserTestApi,
                                       kDefaultWaitForJsResultEvent);
DEFINE_CLASS_CUSTOM_ELEMENT_EVENT_TYPE(InteractiveBrowserTestApi,
                                       kDefaultWaitForJsResultAtEvent);

InteractiveBrowserTestApi::InteractiveBrowserTestApi()
    : InteractiveBrowserTestApi(
          std::make_unique<internal::InteractiveBrowserTestPrivate>(
              std::make_unique<InteractionTestUtilBrowser>())) {}

InteractiveBrowserTestApi::InteractiveBrowserTestApi(
    std::unique_ptr<internal::InteractiveBrowserTestPrivate> private_test_impl)
    : InteractiveViewsTestApi(std::move(private_test_impl)) {}

InteractiveBrowserTestApi::~InteractiveBrowserTestApi() = default;

// static
WebContentsInteractionTestUtil*
InteractiveBrowserTestApi::AsInstrumentedWebContents(ui::TrackedElement* el) {
  auto* const web_el = el->AsA<TrackedElementWebContents>();
  CHECK(web_el);
  return web_el->owner();
}

void InteractiveBrowserTestApi::EnableWebUICodeCoverage() {
  test_impl().MaybeStartWebUICodeCoverage();
}

InteractiveBrowserTestApi::MultiStep InteractiveBrowserTestApi::Screenshot(
    ElementSpecifier element,
    const std::string& screenshot_name,
    const std::string& baseline_cl) {
  StepBuilder builder;
  builder.SetDescription("Compare Screenshot");
  ui::test::internal::SpecifyElement(builder, element);
  builder.SetStartCallback(base::BindOnce(
      [](InteractiveBrowserTestApi* test, std::string screenshot_name,
         std::string baseline_cl, ui::InteractionSequence* seq,
         ui::TrackedElement* el) {
        const auto result = InteractionTestUtilBrowser::CompareScreenshot(
            el, screenshot_name, baseline_cl);
        test->test_impl().HandleActionResult(seq, el, "Screenshot", result);
      },
      base::Unretained(this), screenshot_name, baseline_cl));

  auto steps = Steps(MaybeWaitForPaint(element), std::move(builder),
                     MaybeWaitForUserToDismiss(element));
  AddDescriptionPrefix(steps, base::StrCat({"Screenshot( \"", screenshot_name,
                                            "\", \"", baseline_cl, "\" )"}));
  return steps;
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::ScreenshotSurface(
    ElementSpecifier element_in_surface,
    const std::string& screenshot_name,
    const std::string& baseline_cl) {
  StepBuilder builder;
  builder.SetDescription("Compare Surface Screenshot");
  ui::test::internal::SpecifyElement(builder, element_in_surface);
  builder.SetStartCallback(base::BindOnce(
      [](InteractiveBrowserTestApi* test, std::string screenshot_name,
         std::string baseline_cl, ui::InteractionSequence* seq,
         ui::TrackedElement* el) {
        const auto result =
            InteractionTestUtilBrowser::CompareSurfaceScreenshot(
                el, screenshot_name, baseline_cl);
        test->test_impl().HandleActionResult(seq, el, "Screenshot", result);
      },
      base::Unretained(this), screenshot_name, baseline_cl));

  auto steps = Steps(MaybeWaitForPaint(element_in_surface), std::move(builder),
                     MaybeWaitForUserToDismiss(element_in_surface));
  AddDescriptionPrefix(
      steps, base::StrCat({"ScreenshotSurface( \"", screenshot_name, "\", \"",
                           baseline_cl, "\" )"}));
  return steps;
}

InteractiveBrowserTestApi::MultiStep InteractiveBrowserTestApi::InstrumentTab(
    ui::ElementIdentifier id,
    std::optional<int> tab_index,
    BrowserSpecifier in_browser,
    bool wait_for_ready) {
  auto steps = Steps(WithElement(
      ui::test::internal::kInteractiveTestPivotElementId,
      base::BindLambdaForTesting([this, id, tab_index,
                                  in_browser](ui::TrackedElement* el) {
        Browser* const browser = GetBrowserFor(el->context(), in_browser);
        CHECK(browser) << "InstrumentTab(): a specific browser is required.";
        test_impl().AddInstrumentedWebContents(
            WebContentsInteractionTestUtil::ForExistingTabInBrowser(browser, id,
                                                                    tab_index));
      })));
  if (wait_for_ready) {
    steps.push_back(WaitForWebContentsReady(id));
  }
  AddDescriptionPrefix(
      steps,
      base::StringPrintf("InstrumentTab( %s, %d, %d )", id.GetName().c_str(),
                         tab_index.value_or(-1), wait_for_ready));
  return steps;
}

ui::InteractionSequence::StepBuilder
InteractiveBrowserTestApi::InstrumentNextTab(ui::ElementIdentifier id,
                                             BrowserSpecifier in_browser) {
  return WithElement(
             ui::test::internal::kInteractiveTestPivotElementId,
             [this, id, in_browser](ui::TrackedElement* el) {
               Browser* const browser =
                   GetBrowserFor(el->context(), in_browser);
               test_impl().AddInstrumentedWebContents(
                   browser
                       ? WebContentsInteractionTestUtil::ForNextTabInBrowser(
                             browser, id)
                       : WebContentsInteractionTestUtil::ForNextTabInAnyBrowser(
                             id));
             })
      .AddDescriptionPrefix(
          base::StrCat({"InstrumentTab( ", id.GetName(), " )"}));
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::AddInstrumentedTab(ui::ElementIdentifier id,
                                              GURL url,
                                              std::optional<int> at_index,
                                              BrowserSpecifier in_browser) {
  auto steps = Steps(
      InstrumentNextTab(id, in_browser),
      WithElement(
          ui::test::internal::kInteractiveTestPivotElementId,
          base::BindLambdaForTesting([this, url, at_index,
                                      in_browser](ui::TrackedElement* el) {
            Browser* const browser = GetBrowserFor(el->context(), in_browser);
            CHECK(browser) << "AddInstrumentedTab(): a browser is required.";
            NavigateParams navigate_params(
                browser, url, ui::PageTransition::PAGE_TRANSITION_TYPED);
            navigate_params.tabstrip_index = at_index.value_or(-1);
            navigate_params.disposition =
                WindowOpenDisposition::NEW_FOREGROUND_TAB;
            CHECK(Navigate(&navigate_params));
          })),
      WaitForWebContentsReady(id));
  AddDescriptionPrefix(
      steps, base::StringPrintf("AddInstrumentedTab( %s, %s, %d, )",
                                id.GetName().c_str(), url.spec().c_str(),
                                at_index.value_or(-1)));
  return steps;
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::InstrumentNonTabWebView(ui::ElementIdentifier id,
                                                   ElementSpecifier web_view,
                                                   bool wait_for_ready) {
  auto steps = Steps(AfterShow(
      web_view, base::BindLambdaForTesting([this, id](ui::TrackedElement* el) {
        test_impl().AddInstrumentedWebContents(
            WebContentsInteractionTestUtil::ForNonTabWebView(
                AsView<views::WebView>(el), id));
      })));
  if (wait_for_ready) {
    steps.push_back(WaitForWebContentsReady(id));
  }
  AddDescriptionPrefix(
      steps, base::StringPrintf("InstrumentNonTabWebView( %s, %d, )",
                                id.GetName().c_str(), wait_for_ready));
  return steps;
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::InstrumentNonTabWebView(
    ui::ElementIdentifier id,
    AbsoluteViewSpecifier web_view,
    bool wait_for_ready) {
  static constexpr char kTemporaryElementName[] =
      "__InstrumentNonTabWebViewTemporaryElementName__";
  auto steps =
      Steps(NameView(kTemporaryElementName, std::move(web_view)),
            InstrumentNonTabWebView(id, kTemporaryElementName, wait_for_ready));
  AddDescriptionPrefix(steps, "InstrumentNonTabWebView()");
  return steps;
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::InstrumentInnerWebContents(
    ui::ElementIdentifier inner_id,
    ui::ElementIdentifier outer_id,
    size_t inner_contents_index,
    bool wait_for_ready) {
  MultiStep steps;
  steps.emplace_back(Do([this, inner_id, outer_id, inner_contents_index]() {
    test_impl().AddInstrumentedWebContents(
        WebContentsInteractionTestUtil::ForInnerWebContents(
            outer_id, inner_contents_index, inner_id));
  }));
  if (wait_for_ready) {
    steps.push_back(WaitForWebContentsReady(inner_id));
  }
  AddDescriptionPrefix(
      steps, base::StringPrintf("InstrumentInnerWebContents( %s, %s, %u, %d )",
                                inner_id.GetName(), outer_id.GetName(),
                                inner_contents_index, wait_for_ready));
  return steps;
}

InteractiveBrowserTestApi::StepBuilder
InteractiveBrowserTestApi::UninstrumentWebContents(
    ui::ElementIdentifier id,
    bool fail_if_not_instrumented) {
  return std::move(
      (fail_if_not_instrumented
           ? Check([this, id]() {
               return test_impl().UninstrumentWebContents(id);
             })
           : Do([this, id]() { test_impl().UninstrumentWebContents(id); }))
          .SetDescription(
              base::StringPrintf("UninstrumentWebContents(%s)", id.GetName())));
}

// static
ui::InteractionSequence::StepBuilder
InteractiveBrowserTestApi::WaitForWebContentsReady(
    ui::ElementIdentifier webcontents_id,
    std::optional<GURL> expected_url) {
  StepBuilder builder;
  builder.SetDescription(
      base::StringPrintf("WaitForWebContentsReady( %s )",
                         expected_url.value_or(GURL()).spec().c_str()));
  builder.SetElementID(webcontents_id);
  builder.SetContext(kDefaultWebContentsContextMode);
  // Because we're checking the current specific state of the contents, this
  // avoids further navigations breaking the test.
  builder.SetStepStartMode(ui::InteractionSequence::StepStartMode::kImmediate);
  if (expected_url.has_value()) {
    builder.SetStartCallback(base::BindOnce(
        [](GURL expected_url, ui::InteractionSequence* seq,
           ui::TrackedElement* el) {
          auto* const contents =
              el->AsA<TrackedElementWebContents>()->owner()->web_contents();
          if (expected_url != contents->GetURL()) {
            LOG(ERROR) << "Loaded wrong URL; got " << contents->GetURL()
                       << " but expected " << expected_url;
            seq->FailForTesting();
          }
        },
        expected_url.value()));
  }
  return builder;
}

// static
ui::InteractionSequence::StepBuilder
InteractiveBrowserTestApi::WaitForWebContentsNavigation(
    ui::ElementIdentifier webcontents_id,
    std::optional<GURL> expected_url) {
  StepBuilder builder;
  builder.SetDescription(
      base::StringPrintf("WaitForWebContentsNavigation( %s )",
                         expected_url.value_or(GURL()).spec().c_str()));
  builder.SetElementID(webcontents_id);
  builder.SetContext(kDefaultWebContentsContextMode);
  builder.SetTransitionOnlyOnEvent(true);
  if (expected_url.has_value()) {
    builder.SetStartCallback(base::BindOnce(
        [](GURL expected_url, ui::InteractionSequence* seq,
           ui::TrackedElement* el) {
          auto* const contents =
              el->AsA<TrackedElementWebContents>()->owner()->web_contents();
          if (expected_url != contents->GetURL()) {
            LOG(ERROR) << "Loaded wrong URL; got " << contents->GetURL()
                       << " but expected " << expected_url;
            seq->FailForTesting();
          }
        },
        expected_url.value()));
  }
  return builder;
}

// There is a bug that causes WebContents::CompletedFirstVisuallyNonEmptyPaint()
// to occasionally fail to ever become true. This sometimes manifests when
// running tests on Mac builders. In order to prevent tests from hanging when
// trying to ensure a non-empty paint, then, a workaround is required.
//
// See b/332895669 and b/334747109 for more information.

namespace {

// Warning message so people aren't surprised when something else in their test
// flakes after this step due to the bug.
constexpr char kPaintWorkaroundWarning[] =
    "\n\nIMPORTANT NOTE FOR TESTERS AND CHROMIUM GARDENERS:\n\n"
    "There is a known issue (crbug.com/332895669, crbug.com/334747109) on Mac "
    "where sometimes WebContents::CompletedFirstVisuallyNonEmptyPaint() can "
    "return false even for a WebContents that is visible and painted, "
    "especially in secondary UI.\n\n"
    "Unfortunately, this has happened. In order to prevent this test from "
    "timing out, we will be ensuring that the page is visible and renders at "
    "least one frame and then continuing the test.\n\n"
    "In most cases, this will only result in a slight delay. However, in a "
    "handful of cases the test may hang or fail because some other code relies "
    "on the page reporting as painted, which we have no direct control over. "
    "If this happens, you may need to disable the test until the lower-level "
    "bug is fixed.\n";

// CheckJsResult() can handle promises, so queue a promise that only succeeds
// after the contents have been rendered.
constexpr char kPaintWorkaroundFunction[] =
    "() => new Promise(resolve => requestAnimationFrame(() => resolve(true)))";

// Event sent on a delay to bypass the "was this WebContents painted?" check on
// platforms where the check is flaky; see comments above.
DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kPaintWorkaroundEvent);

void MaybePostPaintWorkaroundEvent(ui::TrackedElement* el) {
  // Only secondary web contents are affected.
  if (chrome::FindBrowserWithTab(
          el->AsA<TrackedElementWebContents>()->owner()->web_contents())) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](ui::ElementIdentifier id) {
            if (auto* const el = ui::ElementTracker::GetElementTracker()
                                     ->GetElementInAnyContext(id)) {
              ui::ElementTracker::GetFrameworkDelegate()->NotifyCustomEvent(
                  el, kPaintWorkaroundEvent);
            }
          },
          el->identifier()),
      base::Seconds(1));
}

}  // namespace

// static
ui::InteractionSequence::StepBuilder
InteractiveBrowserTestApi::WaitForWebContentsPainted(
    ui::ElementIdentifier webcontents_id) {
  auto wait_step = WaitForEvent(webcontents_id,
                                TrackedElementWebContents::kFirstNonEmptyPaint);
  wait_step.SetContext(kDefaultWebContentsContextMode);
  wait_step.SetMustBeVisibleAtStart(false);
  wait_step.AddDescriptionPrefix("WaitForWebContentsPainted()");

#if BUILDFLAG(IS_MAC)
  const bool requires_workaround = true;
#else
  const bool requires_workaround = false;
#endif

  if (requires_workaround) {
    // Workaround for b/332895669 and b/334747109:
    //
    // In parallel with waiting for the WebContents to report as painted, post a
    // delayed event, verify the contents are visible, and ensure at least one
    // frame has rendered. This doesn't fix the problem of the WebContents not
    // reporting as painted, but it does prevent tests that want to ensure that
    // the contents *are* painted from hanging.
    wait_step = AnyOf(
        // Ideally this finishes pretty quickly and we can move on.
        RunSubsequence(std::move(wait_step)),
        // Otherwise, create a timeout after the WebContents is shown.
        RunSubsequence(
            // Ensure that the contents are loaded, then wait a short time.
            InAnyContext(
                AfterShow(webcontents_id, &MaybePostPaintWorkaroundEvent)),
            // After the timeout, first post a verbose warning describing the
            // known issue so that test maintainers are not surprised if
            // something later in the test breaks because paint status is still
            // being reported incorrectly.
            InSameContext(
                AfterEvent(webcontents_id, kPaintWorkaroundEvent,
                           []() { LOG(WARNING) << kPaintWorkaroundWarning; }),
                // Ensure that the WebContents actually believes it's visible.
                CheckElement(
                    webcontents_id,
                    [](ui::TrackedElement* el) {
                      return AsInstrumentedWebContents(el)
                          ->web_contents()
                          ->GetVisibility();
                    },
                    content::Visibility::VISIBLE),
                // Force a frame to render before proceeding.
                // After this is done, we at least known that the contents have
                // been painted - even if the WebContents object itself doesn't!
                CheckJsResult(webcontents_id, kPaintWorkaroundFunction))));
  }

  // If the element is already painted, there is no reason to actually wait (and
  // in fact that will cause a timeout). So only execute the wait step if the
  // WebContents is not ready or not painted.
  //
  // Note: this could also be done with a custom `StateObserver` and
  // `WaitForState()` but this approach requires the fewest steps.
  return IfElement(
             webcontents_id,
             [](const ui::TrackedElement* el) {
               // If the page is not ready (i.e. no element) or not
               // painted, execute the wait step; otherwise skip it.
               return !el || !el->AsA<TrackedElementWebContents>()
                                  ->owner()
                                  ->HasPageBeenPainted();
             },
             Then(std::move(wait_step)))
      .SetContext(kDefaultWebContentsContextMode)
      .AddDescriptionPrefix("WaitForWebContentsPainted()");
}

// static
InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::NavigateWebContents(
    ui::ElementIdentifier webcontents_id,
    GURL target_url) {
  auto steps = Steps(
      std::move(StepBuilder()
                    .SetDescription("Navigate")
                    .SetElementID(webcontents_id)
                    .SetContext(kDefaultWebContentsContextMode)
                    .SetStartCallback(base::BindOnce(
                        [](GURL url, ui::InteractionSequence* seq,
                           ui::TrackedElement* el) {
                          auto* const owner =
                              el->AsA<TrackedElementWebContents>()->owner();
                          if (url.EqualsIgnoringRef(
                                  owner->web_contents()->GetURL())) {
                            LOG(ERROR) << "Trying to load URL " << url
                                       << " but WebContents URL is already "
                                       << owner->web_contents()->GetURL();
                            seq->FailForTesting();
                          }
                          owner->LoadPage(url);
                        },
                        target_url))),
      WaitForWebContentsNavigation(webcontents_id, target_url));
  AddDescriptionPrefix(
      steps, base::StrCat({"NavigateWebContents( ", target_url.spec(), " )"}));
  return steps;
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::FocusWebContents(
    ui::ElementIdentifier webcontents_id) {
  auto steps = InAnyContext(WaitForWebContentsPainted(webcontents_id),
                            ActivateSurface(webcontents_id),
                            FocusElement(webcontents_id));
  AddDescriptionPrefix(steps, "FocusWebContents()");
  return steps;
}

// static
InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::WaitForStateChange(
    ui::ElementIdentifier webcontents_id,
    const StateChange& state_change,
    bool expect_timeout) {
  ui::CustomElementEventType event_type =
      expect_timeout ? state_change.timeout_event : state_change.event;
  CHECK(event_type);
  const bool fail_on_close = !state_change.continue_across_navigation;
  StepBuilder step1;
  step1.SetDescription("Queue Event")
      .SetElementID(webcontents_id)
      .SetContext(kDefaultWebContentsContextMode)
      .SetMustRemainVisible(fail_on_close)
      .SetStartCallback(base::BindOnce(
          [](StateChange state_change, ui::TrackedElement* el) {
            el->AsA<TrackedElementWebContents>()
                ->owner()
                ->SendEventOnStateChange(state_change);
          },
          state_change));
  if (state_change.continue_across_navigation) {
    // This is required to prevent failing if the element would otherwise be
    // hidden due to a navigation between trigger and step start.
    step1.SetStepStartMode(ui::InteractionSequence::StepStartMode::kImmediate);
  }

  auto steps = Steps(
      std::move(step1),
      std::move(StepBuilder()
                    .SetDescription("Wait For Event")
                    .SetElementID(webcontents_id)
                    .SetContext(
                        ui::InteractionSequence::ContextMode::kFromPreviousStep)
                    .SetType(ui::InteractionSequence::StepType::kCustomEvent,
                             event_type)
                    .SetMustBeVisibleAtStart(fail_on_close)));
  AddDescriptionPrefix(
      steps, base::StrCat({"WaitForStateChange( ", base::ToString(state_change),
                           ", ", base::ToString(expect_timeout), " )"}));
  return steps;
}

// static
ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::EnsurePresent(
    ui::ElementIdentifier webcontents_id,
    const DeepQuery& where) {
  StepBuilder builder;
  builder.SetDescription(base::StringPrintf(
      "EnsurePresent( %s, %s )", webcontents_id.GetName().c_str(),
      internal::InteractiveBrowserTestPrivate::DeepQueryToString(where)
          .c_str()));
  builder.SetElementID(webcontents_id);
  builder.SetContext(kDefaultWebContentsContextMode);
  builder.SetStartCallback(base::BindOnce(
      [](DeepQuery where, ui::InteractionSequence* seq,
         ui::TrackedElement* el) {
        if (!AsInstrumentedWebContents(el)->Exists(where)) {
          LOG(ERROR) << "Expected DOM element to be present: " << where;
          seq->FailForTesting();
        }
      },
      where));
  return builder;
}

// static
ui::InteractionSequence::StepBuilder
InteractiveBrowserTestApi::EnsureNotPresent(
    ui::ElementIdentifier webcontents_id,
    const DeepQuery& where) {
  StepBuilder builder;
  builder.SetDescription(base::StringPrintf(
      "EnsureNotPresent( %s, %s )", webcontents_id.GetName().c_str(),
      internal::InteractiveBrowserTestPrivate::DeepQueryToString(where)
          .c_str()));
  builder.SetElementID(webcontents_id);
  builder.SetContext(kDefaultWebContentsContextMode);
  builder.SetStartCallback(base::BindOnce(
      [](DeepQuery where, ui::InteractionSequence* seq,
         ui::TrackedElement* el) {
        if (AsInstrumentedWebContents(el)->Exists(where)) {
          LOG(ERROR) << "Expected DOM element not to be present: " << where;
          seq->FailForTesting();
        }
      },
      where));
  return builder;
}

// static
ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::ExecuteJs(
    ui::ElementIdentifier webcontents_id,
    const std::string& function,
    ExecuteJsMode mode) {
  StepBuilder builder;
  builder.SetDescription(
      base::StringPrintf("ExecuteJs(\"\n%s\n\")", function.c_str()));
  builder.SetElementID(webcontents_id);
  builder.SetContext(kDefaultWebContentsContextMode);
  switch (mode) {
    case ExecuteJsMode::kFireAndForget:
      builder.SetMustRemainVisible(false);
      builder.SetStartCallback(base::BindOnce(
          [](std::string function, ui::TrackedElement* el) {
            AsInstrumentedWebContents(el)->Execute(function);
          },
          function));
      break;
    case ExecuteJsMode::kWaitForCompletion:
      builder.SetStartCallback(base::BindOnce(
          [](std::string function, ui::InteractionSequence* seq,
             ui::TrackedElement* el) {
            const auto full_function = base::StringPrintf(
                "() => { (%s)(); return false; }", function.c_str());
            std::string error_msg;
            AsInstrumentedWebContents(el)->Evaluate(full_function, &error_msg);
            if (!error_msg.empty()) {
              LOG(ERROR) << "ExecuteJsAt() failed: " << error_msg;
              seq->FailForTesting();
            }
          },
          function));
      break;
  }
  return builder;
}

// static
ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::ExecuteJsAt(
    ui::ElementIdentifier webcontents_id,
    const DeepQuery& where,
    const std::string& function,
    ExecuteJsMode mode) {
  StepBuilder builder;
  builder.SetDescription(base::StringPrintf(
      "ExecuteJsAt( %s, \"\n%s\n\")",
      internal::InteractiveBrowserTestPrivate::DeepQueryToString(where).c_str(),
      function.c_str()));
  builder.SetElementID(webcontents_id);
  builder.SetContext(kDefaultWebContentsContextMode);
  switch (mode) {
    case ExecuteJsMode::kFireAndForget:
      builder.SetMustRemainVisible(false);
      builder.SetStartCallback(base::BindOnce(
          [](DeepQuery where, std::string function, ui::TrackedElement* el) {
            AsInstrumentedWebContents(el)->ExecuteAt(where, function);
          },
          where, function));
      break;
    case ExecuteJsMode::kWaitForCompletion:
      builder.SetStartCallback(base::BindOnce(
          [](DeepQuery where, std::string function,
             ui::InteractionSequence* seq, ui::TrackedElement* el) {
            const auto full_function = base::StringPrintf(
                R"(
              (el, err) => {
                if (err) {
                  throw err;
                }
                (%s)(el);
                return false;
              }
            )",
                function.c_str());
            std::string error_msg;
            AsInstrumentedWebContents(el)->EvaluateAt(where, full_function,
                                                      &error_msg);
            if (!error_msg.empty()) {
              LOG(ERROR) << "ExecuteJsAt() failed: " << error_msg;
              seq->FailForTesting();
            }
          },
          where, function));
      break;
  }
  return builder;
}

// static
ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::CheckJsResult(
    ui::ElementIdentifier webcontents_id,
    const std::string& function) {
  return CheckJsResult(webcontents_id, function, internal::IsTruthyMatcher());
}

// static
ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::CheckJsResultAt(
    ui::ElementIdentifier webcontents_id,
    const DeepQuery& where,
    const std::string& function) {
  return CheckJsResultAt(webcontents_id, where, function,
                         internal::IsTruthyMatcher());
}

InteractiveBrowserTestApi::MultiStep InteractiveBrowserTestApi::WaitForJsResult(
    ui::ElementIdentifier webcontents_id,
    const std::string& function) {
  return WaitForJsResult(webcontents_id, function, IsTruthy());
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::WaitForJsResultAt(
    ui::ElementIdentifier webcontents_id,
    const DeepQuery& where,
    const std::string& function) {
  return WaitForJsResultAt(webcontents_id, where, function, IsTruthy());
}

InteractiveBrowserTestApi::MultiStep InteractiveBrowserTestApi::MoveMouseTo(
    ui::ElementIdentifier web_contents,
    const DeepQuery& where) {
  auto steps = Steps(WaitForWebContentsPainted(web_contents),
                     InSameContext(MoveMouseTo(
                         web_contents, DeepQueryToRelativePosition(where))));
  AddDescriptionPrefix(steps, "MoveMouseTo()");
  return steps;
}

InteractiveBrowserTestApi::MultiStep InteractiveBrowserTestApi::DragMouseTo(
    ui::ElementIdentifier web_contents,
    const DeepQuery& where,
    bool release) {
  auto steps =
      Steps(WaitForWebContentsPainted(web_contents),
            InSameContext(DragMouseTo(
                web_contents, DeepQueryToRelativePosition(where), release)));
  AddDescriptionPrefix(steps, "DragMouseTo()");
  return steps;
}

ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::ScrollIntoView(
    ui::ElementIdentifier web_contents,
    const DeepQuery& where) {
  return std::move(
      ExecuteJsAt(web_contents, where,
                  "(el) => { el.scrollIntoView({ behavior: 'instant' }); }")
          .SetDescription("ScrollIntoView()"));
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::WaitForElementVisible(
    ui::ElementIdentifier web_contents,
    const DeepQuery& where) {
  DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kWaitforElementVisibleCompleteEvent);
  const std::string function =
      R"(
        function(el) {
          const rect = el.getBoundingClientRect();
          const left = Math.max(0, rect.x);
          const top = Math.max(0, rect.y);
          const right = Math.min(rect.x + rect.width, window.innerWidth);
          const bottom = Math.min(rect.y + rect.height, window.innerHeight);
          return right > left && bottom > top;
        }
      )";

  StateChange change;
  change.event = kWaitforElementVisibleCompleteEvent;
  change.test_function = function;
  change.type = StateChange::Type::kExistsAndConditionTrue;
  change.where = where;

  auto steps = WaitForStateChange(web_contents, change);
  AddDescriptionPrefix(steps, "WaitForElementVisible()");
  return steps;
}

ui::InteractionSequence::StepBuilder InteractiveBrowserTestApi::ClickElement(
    ui::ElementIdentifier web_contents,
    const DeepQuery& where,
    ui_controls::MouseButton button,
    ui_controls::AcceleratorState modifiers,
    ExecuteJsMode execute_mode) {
  int js_button;
  switch (button) {
    case ui_controls::LEFT:
      js_button = 0;
      break;
    case ui_controls::MIDDLE:
      js_button = 1;
      break;
    case ui_controls::RIGHT:
      js_button = 2;
      break;
  }

  const bool shift = modifiers & ui_controls::kShift;
  const bool alt = modifiers & ui_controls::kAlt;
  const bool ctrl = modifiers & ui_controls::kControl;
  const bool meta = modifiers & ui_controls::kCommand;

  auto b2s = [](bool b) { return base::ToString(b); };

  const std::string command = base::StringPrintf(
      R"(
      function(el) {
        const rect = el.getBoundingClientRect();
        const left = Math.max(0, rect.x);
        const top = Math.max(0, rect.y);
        const right = Math.min(rect.x + rect.width, window.innerWidth);
        const bottom = Math.min(rect.y + rect.height, window.innerHeight);
        if (right <= left || bottom <= top) {
          throw new Error(
              'Target element is zero size or ' +
              'has empty intersection with the viewport.');
        }
        const x = (left + right) / 2;
        const y = (top + bottom) / 2;

        const event = new MouseEvent(
            'click',
            {
              bubbles: true,
              cancelable: true,
              clientX: x,
              clientY: y,
              button: %d,
              shiftKey: %s,
              altKey: %s,
              ctrlKey: %s,
              metaKey: %s
            }
        );
        el.dispatchEvent(event);
      }
    )",
      js_button, b2s(shift), b2s(alt), b2s(ctrl), b2s(meta));

  return ExecuteJsAt(web_contents, where, command, execute_mode)
      .SetDescription("ClickElement()");
}

// static
InteractiveBrowserTestApi::RelativePositionCallback
InteractiveBrowserTestApi::DeepQueryToRelativePosition(const DeepQuery& query) {
  return base::BindOnce(
      [](DeepQuery q, ui::TrackedElement* el) {
        auto* const contents = el->AsA<TrackedElementWebContents>();
        const gfx::Rect container_bounds = contents->GetScreenBounds();
        const gfx::Rect element_bounds =
            contents->owner()->GetElementBoundsInScreen(q);
        CHECK(!element_bounds.IsEmpty())
            << "Cannot target DOM element at " << q << " in "
            << el->identifier() << " because its screen bounds are emtpy.";
        gfx::Rect intersect_bounds = element_bounds;
        intersect_bounds.Intersect(container_bounds);
        CHECK(!intersect_bounds.IsEmpty())
            << "Cannot target DOM element at " << q << " in "
            << el->identifier() << " because its screen bounds "
            << element_bounds.ToString()
            << " are outside the screen bounds of the containing WebView, "
            << container_bounds.ToString()
            << ". Did you forget to scroll the element into view? See "
               "ScrollIntoView().";
        return intersect_bounds.CenterPoint();
      },
      query);
}

InteractiveBrowserTestApi::MultiStep
InteractiveBrowserTestApi::MaybeWaitForPaint(ElementSpecifier element) {
  // Only wait if `element` is actually a `WebContents`.
  //
  // WebContents are typically only referred to via their assigned IDs.
  // TODO(dfried): possibly handle (rare) cases where a name has been assigned.
  if (!std::holds_alternative<ui::ElementIdentifier>(element)) {
    return MultiStep();
  }
  const auto element_id = std::get<ui::ElementIdentifier>(element);

  // Do a `WaitForWebContentsPainted()`, but only if the ID has been assigned to
  // an instrumented `WebContents`.
  //
  // TODO(dfried): Maybe handle `WebView` elements as well.
  return Steps(If(
      [this, element_id]() {
        return test_impl().IsInstrumentedWebContents(element_id);
      },
      Then(WaitForWebContentsPainted(element_id))));
}

// static
InteractiveBrowserTestApi::StepBuilder
InteractiveBrowserTestApi::MaybeWaitForUserToDismiss(ElementSpecifier element) {
  // In interactive mode (--test-launcher-interactive) the behavior for pixel
  // tests is to wait until the user closes/hides the surface that has the
  // element to screenshot. This may break the rest of the test, but it's fine
  // because the purpose of interactive mode is for a human user to observe
  // what the test sees during the screenshot step.
  return If(
      []() {
        return base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kTestLauncherInteractive);
      },
      Then(Log(
               R"(

------------------

Since --test-launcher-interactive is specified, this test will now wait for you
to dismiss the element that is being screenshot:

)",
               "  ", element,
               R"(

Note that This may cause the remainder of the test to fail or crash, if the test
does not expect the surface to be dismissed.

------------------
)"),
           WaitForHide(element)));
}

Browser* InteractiveBrowserTestApi::GetBrowserFor(
    ui::ElementContext current_context,
    BrowserSpecifier spec) {
  return std::visit(
      absl::Overload{[](AnyBrowser) -> Browser* { return nullptr; },
                     [current_context](CurrentBrowser) {
                       Browser* const browser =
                           InteractionTestUtilBrowser::GetBrowserFromContext(
                               current_context);
                       CHECK(browser) << "Current context is not a browser.";
                       return browser;
                     },
                     [](Browser* browser) {
                       CHECK(browser) << "BrowserSpecifier: Browser* is null.";
                       return browser;
                     },
                     [](std::reference_wrapper<Browser*> browser) {
                       CHECK(browser.get())
                           << "BrowserSpecifier: Browser* is null.";
                       return browser.get();
                     }},
      spec);
}
