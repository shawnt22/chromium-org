// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_ACCOUNT_CHOOSER_CONSISTENCY_ACCOUNT_CHOOSER_TABLE_VIEW_CONTROLLER_ACTION_DELEGATE_H_
#define IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_ACCOUNT_CHOOSER_CONSISTENCY_ACCOUNT_CHOOSER_TABLE_VIEW_CONTROLLER_ACTION_DELEGATE_H_

#import <Foundation/Foundation.h>

@class ConsistencyAccountChooserViewController;
@class ConsistencyAccountChooserTableViewController;

// Delegate protocol for ConsistencyAccountChooserTableViewController.
@protocol ConsistencyAccountChooserTableViewControllerActionDelegate <NSObject>

// Invoked when the user selects an identity.
- (void)consistencyAccountChooserTableViewController:
            (ConsistencyAccountChooserTableViewController*)viewController
                         didSelectIdentityWithGaiaID:(NSString*)gaiaID;

// Invoked when the user taps on "Add account".
- (void)consistencyAccountChooserTableViewControllerDidTapOnAddAccount:
    (ConsistencyAccountChooserTableViewController*)viewController;

// Invoked when the user wants to go back to the previous view.
- (void)consistencyAccountChooserTableViewControllerWantsToGoBack:
    (ConsistencyAccountChooserViewController*)viewController;

// Show management help page.
- (void)showManagementHelpPage;

@end

#endif  // IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_ACCOUNT_CHOOSER_CONSISTENCY_ACCOUNT_CHOOSER_TABLE_VIEW_CONTROLLER_ACTION_DELEGATE_H_
