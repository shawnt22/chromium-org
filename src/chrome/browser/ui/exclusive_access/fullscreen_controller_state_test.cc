// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/exclusive_access/fullscreen_controller_state_test.h"

#include <iomanip>
#include <iostream>

#include "build/build_config.h"
#include "chrome/browser/fullscreen.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_test.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
using FState = FullscreenControllerStateTest::State;
// Human specified state machine data.
// For each state, for each event, define the resulting state.
constexpr auto kTransitionTableData = std::to_array<
    std::array<FState, FullscreenControllerStateTest::NUM_EVENTS>>({
    {
        // STATE_NORMAL:
        FState::STATE_TO_BROWSER_FULLSCREEN,  // Event TOGGLE_FULLSCREEN
        FState::STATE_TO_TAB_FULLSCREEN,      // Event ENTER_TAB_FULLSCREEN
        FState::STATE_NORMAL,                 // Event EXIT_TAB_FULLSCREEN
        FState::STATE_NORMAL,                 // Event BUBBLE_EXIT_LINK
        FState::STATE_NORMAL,                 // Event WINDOW_CHANGE
    },
    {
        // STATE_BROWSER_FULLSCREEN:
        FState::STATE_TO_NORMAL,               // Event TOGGLE_FULLSCREEN
        FState::STATE_TAB_BROWSER_FULLSCREEN,  // Event ENTER_TAB_FULLSCREEN
        FState::STATE_BROWSER_FULLSCREEN,      // Event EXIT_TAB_FULLSCREEN
        FState::STATE_TO_NORMAL,               // Event BUBBLE_EXIT_LINK
        FState::STATE_BROWSER_FULLSCREEN,      // Event WINDOW_CHANGE
    },
    {
        // STATE_TAB_FULLSCREEN:
        FState::STATE_TO_NORMAL,       // Event TOGGLE_FULLSCREEN
        FState::STATE_TAB_FULLSCREEN,  // Event ENTER_TAB_FULLSCREEN
        FState::STATE_TO_NORMAL,       // Event EXIT_TAB_FULLSCREEN
        FState::STATE_TO_NORMAL,       // Event BUBBLE_EXIT_LINK
        FState::STATE_TAB_FULLSCREEN,  // Event WINDOW_CHANGE
    },
    {
        // STATE_TAB_BROWSER_FULLSCREEN:
        FState::STATE_TO_NORMAL,               // Event TOGGLE_FULLSCREEN
        FState::STATE_TAB_BROWSER_FULLSCREEN,  // Event ENTER_TAB_FULLSCREEN
        FState::STATE_BROWSER_FULLSCREEN,      // Event EXIT_TAB_FULLSCREEN
        FState::STATE_BROWSER_FULLSCREEN,      // Event BUBBLE_EXIT_LINK
        FState::STATE_TAB_BROWSER_FULLSCREEN,  // Event WINDOW_CHANGE
    },
    {
        // STATE_TO_NORMAL:
        FState::STATE_TO_BROWSER_FULLSCREEN,  // Event TOGGLE_FULLSCREEN
                                              // TODO(crbug.com/40951066)
                                              // Should be a route back to TAB
        FState::STATE_TO_NORMAL,              // Event ENTER_TAB_FULLSCREEN
        FState::STATE_TO_NORMAL,              // Event EXIT_TAB_FULLSCREEN
        FState::STATE_TO_NORMAL,              // Event BUBBLE_EXIT_LINK
        FState::STATE_NORMAL,                 // Event WINDOW_CHANGE
    },
    {
        // STATE_TO_BROWSER_FULLSCREEN:
        FState::STATE_TO_NORMAL,  // Event TOGGLE_FULLSCREEN
                                  // TODO(crbug.com/40951066): Should be a
                                  // route to TAB_BROWSER
        FState::STATE_TO_BROWSER_FULLSCREEN,  // Event ENTER_TAB_FULLSCREEN
        FState::STATE_TO_BROWSER_FULLSCREEN,  // Event EXIT_TAB_FULLSCREEN
#if BUILDFLAG(IS_MAC)
                                              // Mac window reports fullscreen
                                              // immediately and an exit
                                              // triggers exit.
        FState::STATE_TO_NORMAL,  // Event BUBBLE_EXIT_LINK
#else
        FState::STATE_TO_BROWSER_FULLSCREEN,  // Event BUBBLE_EXIT_LINK
#endif
        FState::STATE_BROWSER_FULLSCREEN,  // Event WINDOW_CHANGE
    },
    {
        // STATE_TO_TAB_FULLSCREEN:
        // TODO(crbug.com/40951066): Should be a route to TAB_BROWSER
        FState::STATE_TO_TAB_FULLSCREEN,  // Event TOGGLE_FULLSCREEN
        FState::STATE_TO_TAB_FULLSCREEN,  // Event ENTER_TAB_FULLSCREEN
#if BUILDFLAG(IS_MAC)
                                          // Mac runs as expected due to a
                                          // forced
                                          // NotifyTabOfExitIfNecessary();
        FState::STATE_TO_NORMAL,  // Event EXIT_TAB_FULLSCREEN
#else
                                          // TODO(crbug.com/40951066): Should
                                          // be a route back to NORMAL
        FState::STATE_TO_BROWSER_FULLSCREEN,  // Event EXIT_TAB_FULLSCREEN
#endif
#if BUILDFLAG(IS_MAC)
                                  // Mac window reports fullscreen
                                  // immediately and an exit triggers
                                  // exit.
        FState::STATE_TO_NORMAL,  // Event BUBBLE_EXIT_LINK
#else
        FState::STATE_TO_TAB_FULLSCREEN,  // Event BUBBLE_EXIT_LINK
#endif
        FState::STATE_TAB_FULLSCREEN,  // Event WINDOW_CHANGE
    },
});

}  // namespace

FullscreenControllerStateTest::FullscreenControllerStateTest() {
  for (int source = 0; source < NUM_STATES; ++source) {
    for (int event = 0; event < NUM_EVENTS; ++event) {
      if (ShouldSkipStateAndEventPair(static_cast<State>(source),
                                      static_cast<Event>(event))) {
        continue;
      }
      const State destination = kTransitionTableData[source][event];
      state_transitions_[source][destination].event = static_cast<Event>(event);
      state_transitions_[source][destination].state = destination;
      state_transitions_[source][destination].distance = 1;
    }
  }
}

FullscreenControllerStateTest::~FullscreenControllerStateTest() = default;

// static
const char* FullscreenControllerStateTest::GetStateString(State state) {
  switch (state) {
    ENUM_TO_STRING(STATE_NORMAL);
    ENUM_TO_STRING(STATE_BROWSER_FULLSCREEN);
    ENUM_TO_STRING(STATE_TAB_FULLSCREEN);
    ENUM_TO_STRING(STATE_TAB_BROWSER_FULLSCREEN);
    ENUM_TO_STRING(STATE_TO_NORMAL);
    ENUM_TO_STRING(STATE_TO_BROWSER_FULLSCREEN);
    ENUM_TO_STRING(STATE_TO_TAB_FULLSCREEN);
    ENUM_TO_STRING(STATE_INVALID);
    default:
      NOTREACHED() << "No string for state " << state;
  }
}

// static
const char* FullscreenControllerStateTest::GetEventString(Event event) {
  switch (event) {
    ENUM_TO_STRING(TOGGLE_FULLSCREEN);
    ENUM_TO_STRING(ENTER_TAB_FULLSCREEN);
    ENUM_TO_STRING(EXIT_TAB_FULLSCREEN);
    ENUM_TO_STRING(BUBBLE_EXIT_LINK);
    ENUM_TO_STRING(WINDOW_CHANGE);
    ENUM_TO_STRING(EVENT_INVALID);
    default:
      NOTREACHED() << "No string for event " << event;
  }
}

// static
bool FullscreenControllerStateTest::IsWindowFullscreenStateChangedReentrant() {
#if BUILDFLAG(IS_MAC)
  return false;
#else
  return true;
#endif
}

void FullscreenControllerStateTest::TransitionToState(State final_state) {
  int max_steps = NUM_STATES;
  while (max_steps-- && TransitionAStepTowardState(final_state)) {
    continue;
  }
  ASSERT_GE(max_steps, 0)
      << "TransitionToState was unable to achieve desired "
      << "target state. TransitionAStepTowardState iterated too many times."
      << GetAndClearDebugLog();
  ASSERT_EQ(final_state, state_)
      << "TransitionToState was unable to achieve "
      << "desired target state. TransitionAStepTowardState returned false."
      << GetAndClearDebugLog();
}

bool FullscreenControllerStateTest::TransitionAStepTowardState(
    State destination_state) {
  const State source_state = state_;
  if (source_state == destination_state) {
    return false;
  }

  const StateTransitionInfo next =
      NextTransitionInShortestPath(source_state, destination_state, NUM_STATES);
  if (next.state == STATE_INVALID) {
    NOTREACHED() << "TransitionAStepTowardState unable to transition. "
                 << "NextTransitionInShortestPath("
                 << GetStateString(source_state) << ", "
                 << GetStateString(destination_state)
                 << ") returned STATE_INVALID." << GetAndClearDebugLog();
  }

  return InvokeEvent(next.event);
}

const char* FullscreenControllerStateTest::GetWindowStateString() {
  return nullptr;
}

bool FullscreenControllerStateTest::InvokeEvent(Event event) {
  const State source_state = state_;
  State next_state = kTransitionTableData[source_state][event];

  EXPECT_FALSE(ShouldSkipStateAndEventPair(source_state, event))
      << GetAndClearDebugLog();

  // When simulating reentrant window change calls, expect the next state
  // automatically.
  if (IsWindowFullscreenStateChangedReentrant()) {
    next_state = kTransitionTableData[next_state][WINDOW_CHANGE];
  }

  // Figure out the fullscreen mode expectation.
  ui_test_utils::FullscreenWaiter::Expectation expectation;
  content::WebContents* const active_tab =
      GetBrowser()->tab_strip_model()->GetActiveWebContents();
  // If event is {ENTER,EXIT}_TAB_FULLSCREEN and `active_tab` is
  // being captured, fullscreen mode won't be updated.
  if ((event != ENTER_TAB_FULLSCREEN && event != EXIT_TAB_FULLSCREEN) ||
      !active_tab->IsBeingVisiblyCaptured()) {
    switch (next_state) {
      case STATE_NORMAL:
        expectation.browser_fullscreen = false;
        expectation.tab_fullscreen = false;
        break;
      case STATE_BROWSER_FULLSCREEN:
        expectation.browser_fullscreen = true;
        expectation.tab_fullscreen = false;
        break;
      case STATE_TAB_FULLSCREEN:
        expectation.browser_fullscreen = false;
        expectation.tab_fullscreen = true;
        break;
      case STATE_TAB_BROWSER_FULLSCREEN:
        expectation.browser_fullscreen = true;
        expectation.tab_fullscreen = true;
        break;
      default:
        // Do nothing.
        break;
    }
  }
  ui_test_utils::FullscreenWaiter waiter(GetBrowser(), expectation);

  debugging_log_ << "  InvokeEvent(" << std::left
                 << std::setw(kMaxStateNameLength) << GetEventString(event)
                 << ") to " << std::setw(kMaxStateNameLength)
                 << GetStateString(next_state);

  state_ = next_state;

  switch (event) {
    case TOGGLE_FULLSCREEN:
      GetFullscreenController()->ToggleBrowserFullscreenMode(
          /*user_initiated=*/false);
      break;
    case ENTER_TAB_FULLSCREEN:
    case EXIT_TAB_FULLSCREEN: {
      if (event == ENTER_TAB_FULLSCREEN) {
        if (GetFullscreenController()->CanEnterFullscreenModeForTab(
                active_tab->GetPrimaryMainFrame())) {
          GetFullscreenController()->EnterFullscreenModeForTab(
              active_tab->GetPrimaryMainFrame());
        }
      } else {
        GetFullscreenController()->ExitFullscreenModeForTab(active_tab);
      }

      // Activating/Deactivating tab fullscreen on a visibly captured tab
      // should not evoke a state change in the browser window.
      if (active_tab->IsBeingVisiblyCaptured()) {
        state_ = source_state;
      }
      break;
    }

    case BUBBLE_EXIT_LINK:
      GetFullscreenController()->ExitExclusiveAccessToPreviousState();
      break;

    case WINDOW_CHANGE:
      ChangeWindowFullscreenState();
      break;

    default:
      NOTREACHED() << "InvokeEvent needs a handler for event "
                   << GetEventString(event) << GetAndClearDebugLog();
  }

  if (GetWindowStateString()) {
    debugging_log_ << " Window state now " << GetWindowStateString() << "\n";
  } else {
    debugging_log_ << "\n";
  }

  waiter.Wait();
  VerifyWindowState();

  return true;
}

void FullscreenControllerStateTest::VerifyWindowState() {
  switch (state_) {
    case STATE_NORMAL:
      VerifyWindowStateExpectations(FULLSCREEN_FOR_BROWSER_FALSE,
                                    FULLSCREEN_FOR_TAB_FALSE);
      break;
    case STATE_BROWSER_FULLSCREEN:
      VerifyWindowStateExpectations(FULLSCREEN_FOR_BROWSER_TRUE,
                                    FULLSCREEN_FOR_TAB_FALSE);
      break;
    case STATE_TAB_FULLSCREEN:
      VerifyWindowStateExpectations(FULLSCREEN_FOR_BROWSER_FALSE,
                                    FULLSCREEN_FOR_TAB_TRUE);
      break;
    case STATE_TAB_BROWSER_FULLSCREEN:
      VerifyWindowStateExpectations(FULLSCREEN_FOR_BROWSER_TRUE,
                                    FULLSCREEN_FOR_TAB_TRUE);
      break;
    case STATE_TO_NORMAL:
      VerifyWindowStateExpectations(FULLSCREEN_FOR_BROWSER_NO_EXPECTATION,
                                    FULLSCREEN_FOR_TAB_NO_EXPECTATION);
      break;

    case STATE_TO_BROWSER_FULLSCREEN:
      VerifyWindowStateExpectations(
#if BUILDFLAG(IS_MAC)
          FULLSCREEN_FOR_BROWSER_TRUE,
#else
          FULLSCREEN_FOR_BROWSER_FALSE,
#endif
          FULLSCREEN_FOR_TAB_NO_EXPECTATION);
      break;
    case STATE_TO_TAB_FULLSCREEN:
      VerifyWindowStateExpectations(FULLSCREEN_FOR_BROWSER_FALSE,
                                    FULLSCREEN_FOR_TAB_TRUE);
      break;

    default:
      NOTREACHED() << GetAndClearDebugLog();
  }
}

void FullscreenControllerStateTest::TestTransitionsForEachState() {
  for (int source_int = 0; source_int < NUM_STATES; ++source_int) {
    for (int event1_int = 0; event1_int < NUM_EVENTS; ++event1_int) {
      const State state = static_cast<State>(source_int);
      const Event event1 = static_cast<Event>(event1_int);

      // Early out if skipping all tests for this state, reduces log noise.
      if (ShouldSkipTest(state, event1)) {
        continue;
      }

      for (int event2_int = 0; event2_int < NUM_EVENTS; ++event2_int) {
        for (int event3_int = 0; event3_int < NUM_EVENTS; ++event3_int) {
          const Event event2 = static_cast<Event>(event2_int);
          const Event event3 = static_cast<Event>(event3_int);

          // Test each state and each event.
          ASSERT_NO_FATAL_FAILURE(TestStateAndEvent(state, event1))
              << GetAndClearDebugLog();

          // Then, add an additional event to the sequence.
          if (ShouldSkipStateAndEventPair(state_, event2)) {
            continue;
          }
          ASSERT_TRUE(InvokeEvent(event2)) << GetAndClearDebugLog();

          // Then, add an additional event to the sequence.
          if (ShouldSkipStateAndEventPair(state_, event3)) {
            continue;
          }
          ASSERT_TRUE(InvokeEvent(event3)) << GetAndClearDebugLog();
        }
      }
    }
  }
}

FullscreenControllerStateTest::StateTransitionInfo
FullscreenControllerStateTest::NextTransitionInShortestPath(State source,
                                                            State destination,
                                                            int search_limit) {
  if (search_limit <= 0) {
    return StateTransitionInfo();  // Return a default (invalid) state.
  }

  if (state_transitions_[source][destination].state == STATE_INVALID) {
    // Don't know the next state yet, do a depth first search.
    StateTransitionInfo result;

    // Consider all states reachable via each event from the source state.
    for (int event_int = 0; event_int < NUM_EVENTS; ++event_int) {
      const Event event = static_cast<Event>(event_int);
      const State next_state_candidate = kTransitionTableData[source][event];

      if (ShouldSkipStateAndEventPair(source, event)) {
        continue;
      }

      // Recurse.
      StateTransitionInfo candidate = NextTransitionInShortestPath(
          next_state_candidate, destination, search_limit - 1);

      if (candidate.distance + 1 < result.distance) {
        result.event = event;
        result.state = next_state_candidate;
        result.distance = candidate.distance + 1;
      }
    }

    // Cache result so that a search is not required next time.
    state_transitions_[source][destination] = result;
  }

  return state_transitions_[source][destination];
}

std::string FullscreenControllerStateTest::GetAndClearDebugLog() {
  debugging_log_ << "(End of Debugging Log)\n";
  std::string output_log = "\nDebugging Log:\n" + debugging_log_.str();
  debugging_log_.str(std::string());
  return output_log;
}

bool FullscreenControllerStateTest::ShouldSkipStateAndEventPair(State state,
                                                                Event event) {
  // TODO(scheib) Toggling Tab fullscreen while pending Tab or
  // Browser fullscreen is broken currently http://crbug.com/154196
  if ((state == STATE_TO_BROWSER_FULLSCREEN ||
       state == STATE_TO_TAB_FULLSCREEN) &&
      (event == ENTER_TAB_FULLSCREEN || event == EXIT_TAB_FULLSCREEN)) {
    return true;
  }

  if (state == STATE_TO_NORMAL && event == ENTER_TAB_FULLSCREEN) {
    return true;
  }

  return false;
}

bool FullscreenControllerStateTest::ShouldSkipTest(State state, Event event) {
  // When testing reentrancy there are states the fullscreen controller
  // will be unable to remain in, as they will progress due to the
  // reentrant window change call. Skip states that will be instantly
  // exited by the reentrant call.
  if (IsWindowFullscreenStateChangedReentrant() &&
      (kTransitionTableData[state][WINDOW_CHANGE] != state)) {
    debugging_log_ << "\nSkipping reentrant test for transitory source state "
                   << GetStateString(state) << ".\n";
    return true;
  }

  if (ShouldSkipStateAndEventPair(state, event)) {
    debugging_log_ << "\nSkipping test due to ShouldSkipStateAndEventPair("
                   << GetStateString(state) << ", " << GetEventString(event)
                   << ").\n";
    LOG(INFO) << "Skipping test due to ShouldSkipStateAndEventPair("
              << GetStateString(state) << ", " << GetEventString(event) << ").";
    return true;
  }

  return false;
}

void FullscreenControllerStateTest::TestStateAndEvent(State state,
                                                      Event event) {
  if (ShouldSkipTest(state, event)) {
    return;
  }

  debugging_log_ << "\nTest transition from state " << GetStateString(state)
                 << (IsWindowFullscreenStateChangedReentrant()
                         ? " with reentrant calls.\n"
                         : ".\n");

  // Spaced out text to line up with columns printed in InvokeEvent().
  debugging_log_ << "First,                                               from "
                 << GetStateString(state_) << "\n";
  ASSERT_NO_FATAL_FAILURE(TransitionToState(state)) << GetAndClearDebugLog();

  debugging_log_ << " Then,\n";
  ASSERT_TRUE(InvokeEvent(event)) << GetAndClearDebugLog();
}

void FullscreenControllerStateTest::VerifyWindowStateExpectations(
    FullscreenForBrowserExpectation fullscreen_for_browser,
    FullscreenForTabExpectation fullscreen_for_tab) {
  if (fullscreen_for_browser != FULLSCREEN_FOR_BROWSER_NO_EXPECTATION) {
    EXPECT_EQ(GetFullscreenController()->IsFullscreenForBrowser(),
              !!fullscreen_for_browser)
        << GetAndClearDebugLog();
  }
  if (fullscreen_for_tab != FULLSCREEN_FOR_TAB_NO_EXPECTATION) {
    EXPECT_EQ(GetFullscreenController()->IsWindowFullscreenForTabOrPending(),
              !!fullscreen_for_tab)
        << GetAndClearDebugLog();
    if (auto* tab = GetFullscreenController()->exclusive_access_tab()) {
      const content::FullscreenState state =
          GetFullscreenController()->GetFullscreenState(tab);
      EXPECT_EQ(
          state.target_mode == content::FullscreenMode::kContent ||
              state.target_mode == content::FullscreenMode::kPseudoContent,
          !!fullscreen_for_tab)
          << GetAndClearDebugLog();
    }
  }
}

void FullscreenControllerStateTest::TearDown() {}

FullscreenController* FullscreenControllerStateTest::GetFullscreenController() {
  return GetBrowser()
      ->GetFeatures()
      .exclusive_access_manager()
      ->fullscreen_controller();
}

std::string FullscreenControllerStateTest::GetTransitionTableAsString() const {
  std::ostringstream output;
  output << "kTransitionTableData[NUM_STATES = " << NUM_STATES
         << "][NUM_EVENTS = " << NUM_EVENTS << "] =\n";
  for (int state_int = 0; state_int < NUM_STATES; ++state_int) {
    State state = static_cast<State>(state_int);
    output << "    { // " << GetStateString(state) << ":\n";
    for (int event_int = 0; event_int < NUM_EVENTS; ++event_int) {
      Event event = static_cast<Event>(event_int);
      output << "      " << std::left << std::setw(kMaxStateNameLength + 1)
             << std::string(
                    GetStateString(kTransitionTableData[state][event])) +
                    ","
             << "// Event " << GetEventString(event) << "\n";
    }
    output << "    },\n";
  }
  output << "  };\n";
  return output.str();
}

std::string FullscreenControllerStateTest::GetStateTransitionsAsString() const {
  std::ostringstream output;
  output << "state_transitions_[NUM_STATES = " << NUM_STATES
         << "][NUM_STATES = " << NUM_STATES << "] =\n";
  for (int state1_int = 0; state1_int < NUM_STATES; ++state1_int) {
    State state1 = static_cast<State>(state1_int);
    output << "{ // " << GetStateString(state1) << ":\n";
    for (int state2_int = 0; state2_int < NUM_STATES; ++state2_int) {
      State state2 = static_cast<State>(state2_int);
      const StateTransitionInfo& info = state_transitions_[state1][state2];
      output << "  { " << std::left << std::setw(kMaxStateNameLength + 1)
             << std::string(GetEventString(info.event)) + "," << std::left
             << std::setw(kMaxStateNameLength + 1)
             << std::string(GetStateString(info.state)) + "," << std::right
             << std::setw(2) << info.distance << " }, // "
             << GetStateString(state2) << "\n";
    }
    output << "},\n";
  }
  output << "};";
  return output.str();
}
