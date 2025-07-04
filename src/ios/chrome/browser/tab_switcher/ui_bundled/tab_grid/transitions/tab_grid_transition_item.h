// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_TAB_SWITCHER_UI_BUNDLED_TAB_GRID_TRANSITIONS_TAB_GRID_TRANSITION_ITEM_H_
#define IOS_CHROME_BROWSER_TAB_SWITCHER_UI_BUNDLED_TAB_GRID_TRANSITIONS_TAB_GRID_TRANSITION_ITEM_H_

#import <UIKit/UIKit.h>

// Class defining a transition item in a TabGrid.
@interface TabGridTransitionItem : NSObject

// Transition item's snapshot.
@property(nonatomic, weak, readonly) UIImage* snapshot;

// Transition item's original frame based window coordinates.
@property(nonatomic, assign, readonly) CGRect originalFrame;

// Creates a new TabGridTransitionItem instance with the given `snapshot` and
// `originalFrame`.
+ (instancetype)itemWithSnapshot:(UIImage*)snapshot
                   originalFrame:(CGRect)originalFrame;

@end

#endif  // IOS_CHROME_BROWSER_TAB_SWITCHER_UI_BUNDLED_TAB_GRID_TRANSITIONS_TAB_GRID_TRANSITION_ITEM_H_
