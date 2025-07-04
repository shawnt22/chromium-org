// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/authentication/ui_bundled/history_sync/history_sync_view_controller.h"

#import "base/metrics/histogram_functions.h"
#import "base/metrics/user_metrics.h"
#import "base/metrics/user_metrics_action.h"
#import "base/notreached.h"
#import "components/signin/public/base/signin_metrics.h"
#import "components/signin/public/identity_manager/tribool.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin/signin_constants.h"
#import "ios/chrome/browser/authentication/ui_bundled/signin/signin_context_style.h"
#import "ios/chrome/browser/keyboard/ui_bundled/UIKeyCommand+Chrome.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/common/ui/util/constraints_ui_util.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util.h"

@implementation HistorySyncViewController {
  // Used to customize content on screen.
  SigninContextStyle _contextStyle;
}

- (instancetype)initWithContextStyle:(SigninContextStyle)contextStyle {
  self = [super init];
  if (self) {
    _contextStyle = contextStyle;
  }
  return self;
}

@dynamic delegate;

- (void)viewDidLoad {
  self.view.accessibilityIdentifier = kHistorySyncViewAccessibilityIdentifier;
  self.shouldHideBanner = YES;
  self.scrollToEndMandatory = YES;
  self.readMoreString =
      l10n_util::GetNSString(IDS_IOS_FIRST_RUN_SCREEN_READ_MORE);
  self.headerImageType = PromoStyleImageType::kAvatar;
  if (IsNewSyncOptInIllustration()) {
    self.headerBackgroundImage = [UIImage imageNamed:@"sync_opt_in_background"];
  } else {
    self.headerBackgroundImage =
        [UIImage imageNamed:@"history_sync_opt_in_background"];
  }

  switch (_contextStyle) {
    case SigninContextStyle::kCollaborationJoinTabGroup:
    case SigninContextStyle::kCollaborationShareTabGroup: {
      self.titleText = l10n_util::GetNSString(
          IDS_IOS_HISTORY_SYNC_GROUP_COLLABORATION_TITLE);
      self.subtitleText = l10n_util::GetNSString(
          IDS_IOS_HISTORY_SYNC_GROUP_COLLABORATION_SUBTITLE);
      break;
    }
    case SigninContextStyle::kDefault: {
      self.titleText = l10n_util::GetNSString(IDS_IOS_HISTORY_SYNC_TITLE);
      self.subtitleText = l10n_util::GetNSString(IDS_IOS_HISTORY_SYNC_SUBTITLE);
      break;
    }
  }

  self.primaryActionString =
      l10n_util::GetNSString(IDS_IOS_HISTORY_SYNC_PRIMARY_ACTION);
  self.secondaryActionString =
      l10n_util::GetNSString(IDS_IOS_HISTORY_SYNC_SECONDARY_ACTION);
  [super viewDidLoad];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  // Hide the buttons, title, and subtitle only if button visibility has not
  // been updated.
  if (self.actionButtonsVisibility == ActionButtonsVisibility::kDefault) {
    self.actionButtonsVisibility = ActionButtonsVisibility::kHidden;
  }
}

#pragma mark - HistorySyncConsumer

- (void)setPrimaryIdentityAvatarImage:(UIImage*)primaryIdentityAvatarImage {
  self.headerImage = primaryIdentityAvatarImage;
}

- (void)setPrimaryIdentityAvatarAccessibilityLabel:
    (NSString*)primaryIdentityAvatarAccessibilityLabel {
  self.headerAccessibilityLabel = primaryIdentityAvatarAccessibilityLabel;
}

- (void)setFooterText:(NSString*)text {
  self.disclaimerText = text;
}

- (void)displayButtonsWithRestrictionCapability:
    (signin::Tribool)canShowUnrestrictedViewCapability {
  // Show action buttons and record button metrics.
  signin_metrics::SyncButtonsType buttonType;
  switch (canShowUnrestrictedViewCapability) {
    case signin::Tribool::kUnknown:
      self.actionButtonsVisibility =
          ActionButtonsVisibility::kEquallyWeightedButtonShown;
      buttonType = signin_metrics::SyncButtonsType::
          kHistorySyncEqualWeightedFromDeadline;
      break;
    case signin::Tribool::kFalse:
      self.actionButtonsVisibility =
          ActionButtonsVisibility::kEquallyWeightedButtonShown;
      buttonType = signin_metrics::SyncButtonsType::
          kHistorySyncEqualWeightedFromCapability;
      break;
    case signin::Tribool::kTrue:
      self.actionButtonsVisibility =
          ActionButtonsVisibility::kRegularButtonsShown;
      buttonType =
          signin_metrics::SyncButtonsType::kHistorySyncNotEqualWeighted;
      break;
    default:
      NOTREACHED();
  }
  base::UmaHistogramEnumeration("Signin.SyncButtons.Shown", buttonType);
}

#pragma mark - UIResponder

// To always be able to register key commands via -keyCommands, the VC must be
// able to become first responder.
- (BOOL)canBecomeFirstResponder {
  return YES;
}

- (NSArray*)keyCommands {
  return @[ UIKeyCommand.cr_close ];
}

- (void)keyCommand_close {
  base::RecordAction(base::UserMetricsAction(kMobileKeyCommandClose));
  // Closing behavior is the same as "No thanks". For consistency with
  // `presentationControllerDidDismiss:` behaviour.
  [self.delegate didTapSecondaryActionButton];
}

@end
