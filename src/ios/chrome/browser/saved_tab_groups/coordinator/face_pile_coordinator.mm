// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/saved_tab_groups/coordinator/face_pile_coordinator.h"

#import "ios/chrome/browser/data_sharing/model/data_sharing_service_factory.h"
#import "ios/chrome/browser/saved_tab_groups/coordinator/face_pile_configuration.h"
#import "ios/chrome/browser/saved_tab_groups/coordinator/face_pile_mediator.h"
#import "ios/chrome/browser/saved_tab_groups/ui/face_pile_view.h"
#import "ios/chrome/browser/share_kit/model/share_kit_service_factory.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/signin/model/identity_manager_factory.h"

@implementation FacePileCoordinator {
  // The FacePileView instance managed by this coordinator.
  FacePileView* _facePileView;

  // The FacePileMediator instance.
  FacePileMediator* _mediator;

  // The configuration for the face pile.
  FacePileConfiguration* _configuration;
}

- (instancetype)initWithFacePileConfiguration:
                    (FacePileConfiguration*)configuration
                                      browser:(Browser*)browser {
  self = [super initWithBaseViewController:nil browser:browser];
  if (self) {
    _configuration = configuration;
  }
  return self;
}

#pragma mark - FacePileProviding

- (UIView*)facePileView {
  CHECK(_facePileView)
      << "Call -[FacePileCoordinator start] before requesting `facePileView`.";
  return _facePileView;
}

#pragma mark - ChromeCoordinator

- (void)start {
  _facePileView = [[FacePileView alloc] init];

  ProfileIOS* profile = self.profile;
  _mediator = [[FacePileMediator alloc]
      initWithConfiguration:_configuration
         dataSharingService:data_sharing::DataSharingServiceFactory::
                                GetForProfile(profile)
            shareKitService:ShareKitServiceFactory::GetForProfile(profile)
            identityManager:IdentityManagerFactory::GetForProfile(profile)];

  _mediator.consumer = _facePileView;
}

- (void)stop {
  [_mediator disconnect];
  _mediator = nil;

  [_facePileView removeFromSuperview];
  _facePileView = nil;
}

@end
