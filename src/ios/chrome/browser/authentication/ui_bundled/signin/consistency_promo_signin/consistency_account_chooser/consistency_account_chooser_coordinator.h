// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_ACCOUNT_CHOOSER_CONSISTENCY_ACCOUNT_CHOOSER_COORDINATOR_H_
#define IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_ACCOUNT_CHOOSER_CONSISTENCY_ACCOUNT_CHOOSER_COORDINATOR_H_

#import "ios/chrome/browser/shared/coordinator/chrome_coordinator/chrome_coordinator.h"

@class ConsistencyAccountChooserCoordinator;
@protocol ConsistencyLayoutDelegate;
@protocol SystemIdentity;

// Delegate for ConsistencyAccountChooserCoordinator.
@protocol ConsistencyAccountChooserCoordinatorDelegate <NSObject>

// Invoked when the user selected an identity.
- (void)consistencyAccountChooserCoordinatorIdentitySelected:
    (ConsistencyAccountChooserCoordinator*)coordinator;

// Invoke add account SigninCoordinator.
- (void)consistencyAccountChooserCoordinatorOpenAddAccount:
    (ConsistencyAccountChooserCoordinator*)coordinator;

// Invoke add account SigninCoordinator.
- (void)consistencyAccountChooserCoordinatorWantsToBeStopped:
    (ConsistencyAccountChooserCoordinator*)coordinator;

@end

// This coordinator presents an entry point to the Chrome sign-in flow with the
// default account available on the device.
@interface ConsistencyAccountChooserCoordinator : ChromeCoordinator

// Identity selected by the user.
@property(nonatomic, strong, readonly) id<SystemIdentity> selectedIdentity;
@property(nonatomic, strong, readonly) UIViewController* viewController;
@property(nonatomic, weak) id<ConsistencyAccountChooserCoordinatorDelegate>
    delegate;
@property(nonatomic, weak) id<ConsistencyLayoutDelegate> layoutDelegate;

- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser
                          selectedIdentity:(id<SystemIdentity>)selectedIdentity
    NS_DESIGNATED_INITIALIZER;

- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser NS_UNAVAILABLE;

@end

#endif  // IOS_CHROME_BROWSER_AUTHENTICATION_UI_BUNDLED_SIGNIN_CONSISTENCY_PROMO_SIGNIN_CONSISTENCY_ACCOUNT_CHOOSER_CONSISTENCY_ACCOUNT_CHOOSER_COORDINATOR_H_
