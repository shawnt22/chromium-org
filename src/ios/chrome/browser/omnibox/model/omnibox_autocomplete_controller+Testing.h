// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_OMNIBOX_MODEL_OMNIBOX_AUTOCOMPLETE_CONTROLLER_TESTING_H_
#define IOS_CHROME_BROWSER_OMNIBOX_MODEL_OMNIBOX_AUTOCOMPLETE_CONTROLLER_TESTING_H_

#include "ios/chrome/browser/omnibox/model/omnibox_autocomplete_controller.h"

class AutocompleteController;

/// Testing category exposing private methods for tests.
@interface OmniboxAutocompleteController (Testing)

/// Sets autocomplete controller for testing.
- (void)setAutocompleteController:
    (std::unique_ptr<AutocompleteController>)autocompleteController;

@end

#endif  // IOS_CHROME_BROWSER_OMNIBOX_MODEL_OMNIBOX_AUTOCOMPLETE_CONTROLLER_TESTING_H_
