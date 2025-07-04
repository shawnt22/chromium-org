// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_COMMON_APP_GROUP_APP_GROUP_UTILS_H_
#define IOS_CHROME_COMMON_APP_GROUP_APP_GROUP_UTILS_H_

#import <Foundation/Foundation.h>

namespace app_group {

// Synchronously clears the `ApplicationGroup` and the `CommonApplicationGroup`
// app group sandbox (folder and NSUserDefaults).
// The function will be executed on the calling thread.
// Disclaimer: This method may delete data that were not created by Chrome. Its
// only purpose is to reset the application group to it's fresh install state.
// This method may take undetermined time as it will do file access on main
// thread and must only be called for testing purpose.
void ClearAppGroupSandbox();

// Returns a string from the app group user defaults.
// Returns `default_value` if the string is nil.
NSString* UserDefaultsStringForKey(NSString* key, NSString* default_value);

// Returns whether the share extension for multi profile is enabled based on the
// value store in the shared defaults.
BOOL MultiProfileShareExtensionEnabled();

}  // namespace app_group

#endif  // IOS_CHROME_COMMON_APP_GROUP_APP_GROUP_UTILS_H_
