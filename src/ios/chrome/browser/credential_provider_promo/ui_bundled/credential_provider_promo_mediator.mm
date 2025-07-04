// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/credential_provider_promo/ui_bundled/credential_provider_promo_mediator.h"

#import "base/files/file_path.h"
#import "base/path_service.h"
#import "base/strings/sys_string_conversions.h"
#import "components/feature_engagement/public/event_constants.h"
#import "components/feature_engagement/public/tracker.h"
#import "components/password_manager/core/browser/password_manager_util.h"
#import "components/prefs/pref_service.h"
#import "ios/chrome/browser/credential_provider_promo/ui_bundled/credential_provider_promo_consumer.h"
#import "ios/chrome/browser/promos_manager/model/constants.h"
#import "ios/chrome/browser/promos_manager/model/features.h"
#import "ios/chrome/browser/promos_manager/model/promos_manager.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/public/commands/credential_provider_promo_commands.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/grit/ios_branded_strings.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/public/provider/chrome/browser/branded_images/branded_images_api.h"
#import "ui/base/l10n/l10n_util.h"

namespace {

NSString* const kFirstStepAnimation = @"CPE_promo_animation_edu_autofill";
NSString* const kLearnMoreAnimation = @"CPE_promo_animation_edu_how_to_enable";

// Returns the title string to use when the promo context is `kFirstStep`.
NSString* GetFirstStepTitleString() {
  if (!IOSPasskeysM2Enabled()) {
    return l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_PROMO_INITIAL_TITLE);
  }

  if (@available(iOS 18.0, *)) {
    return l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_PROMO_TITLE_IOS18);
  } else {
    return l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_TITLE);
  }
}

// Returns the subtitle string to use when the promo context is `kFirstStep`.
NSString* GetFirstStepSubtitleString() {
  if (!IOSPasskeysM2Enabled()) {
    return l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_PROMO_INITIAL_SUBTITLE);
  }

  if (@available(iOS 18.0, *)) {
    return nil;
  } else {
    return l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_SUBTITLE);
  }
}

// Returns the primary action string to use when the promo context is
// `kFirstStep`.
NSString* GetFirstStepPrimaryActionString() {
  if (!IOSPasskeysM2Enabled()) {
    return l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_HOW);
  }

  if (@available(iOS 18.0, *)) {
    return l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_SETTINGS_TURN_ON_AUTOFILL);
  } else {
    return l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_HOW);
  }
}

// Returns the subtitle string to use when the promo context is `kLearnMore`.
NSString* GetLearnMoreSubtitleString() {
  NSString* settings_menu_item_string = l10n_util::GetNSString(
      IDS_IOS_CREDENTIAL_PROVIDER_PROMO_OS_PASSWORDS_SETTINGS_TITLE_IOS16);
  CHECK(settings_menu_item_string.length > 0);
  return l10n_util::GetNSStringF(
      IOSPasskeysM2Enabled()
          ? IDS_IOS_CREDENTIAL_PROVIDER_PROMO_INSTRUCTIONS_SUBTITLE
          : IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_MORE_SUBTITLE_WITH_PH,
      base::SysNSStringToUTF16(settings_menu_item_string));
}

}  // namespace

@interface CredentialProviderPromoMediator ()

// Indicates whether the 'first step' or 'learn more' version of the promo is
// being presented.
@property(nonatomic, assign) CredentialProviderPromoContext promoContext;

// The PromosManager is used to register the promo.
@property(nonatomic, assign) PromosManager* promosManager;

@end

@implementation CredentialProviderPromoMediator

- (instancetype)initWithPromosManager:(PromosManager*)promosManager {
  if ((self = [super init])) {
    _promosManager = promosManager;
  }
  return self;
}

- (BOOL)canShowCredentialProviderPromoWithTrigger:
            (CredentialProviderPromoTrigger)trigger
                                        promoSeen:
                                            (BOOL)promoSeenInCurrentSession {
  if (trigger == CredentialProviderPromoTrigger::SetUpList ||
      trigger == CredentialProviderPromoTrigger::TipsNotification) {
    // Always allow showing when triggered by user via the SetUpList or Tips
    // Notification.
    return YES;
  }
  BOOL impressionLimitMet =
      GetApplicationContext()->GetLocalState()->GetBoolean(
          prefs::kIosCredentialProviderPromoStopPromo) ||
      (promoSeenInCurrentSession &&
       trigger != CredentialProviderPromoTrigger::RemindMeLater);
  BOOL policyEnabled = GetApplicationContext()->GetLocalState()->GetBoolean(
      prefs::kIosCredentialProviderPromoPolicyEnabled);
  PrefService* localState = GetApplicationContext()->GetLocalState();
  return !impressionLimitMet && policyEnabled &&
         !password_manager_util::IsCredentialProviderEnabledOnStartup(
             localState);
}

- (void)configureConsumerWithTrigger:(CredentialProviderPromoTrigger)trigger
                             context:(CredentialProviderPromoContext)context {
  IOSCredentialProviderPromoSource source;
  self.promoContext = context;
  switch (trigger) {
    case CredentialProviderPromoTrigger::SuccessfulLoginUsingExistingPassword:
      source = IOSCredentialProviderPromoSource::kAutofillUsed;
      if (self.promoContext == CredentialProviderPromoContext::kLearnMore) {
        [self setAnimation];
      }
      break;
    case CredentialProviderPromoTrigger::RemindMeLater:
      source = [self promoOriginalSource];

      [self setAnimation];
      break;
    case CredentialProviderPromoTrigger::SetUpList:
      source = IOSCredentialProviderPromoSource::kSetUpList;
      [self setAnimation];
      break;
    case CredentialProviderPromoTrigger::TipsNotification:
      source = IOSCredentialProviderPromoSource::kTipsNotification;
      [self setAnimation];
      break;
  }

  [self setTextAndImageWithSource:source];

  // Set the promo source in the Prefs. Skip for 'RemindMeLater' triggers as
  // source is already present.
  if (trigger != CredentialProviderPromoTrigger::RemindMeLater) {
    GetApplicationContext()->GetLocalState()->SetInteger(
        prefs::kIosCredentialProviderPromoSource, static_cast<int>(source));
  }
}

- (void)registerPromoWithPromosManager {
  if (!self.promosManager) {
    return;
  }
  self.promosManager->RegisterPromoForSingleDisplay(
      promos_manager::Promo::CredentialProviderExtension, base::Hours(24));

  if (self.tracker) {
    self.tracker->NotifyEvent(
        feature_engagement::events::kCredentialProviderExtensionPromoSnoozed);
  }
}

- (IOSCredentialProviderPromoSource)promoOriginalSource {
  int sourceAsInteger = GetApplicationContext()->GetLocalState()->GetInteger(
      prefs::kIosCredentialProviderPromoSource);
  DCHECK(sourceAsInteger <=
         static_cast<int>(IOSCredentialProviderPromoSource::kMaxValue));
  return static_cast<IOSCredentialProviderPromoSource>(sourceAsInteger);
}

#pragma mark - Private

// Sets the animation to the consumer that corresponds to the value of
// promoContext. Depending on the value of promoContext, either the 'first step'
// or 'learn more' animation name is set.
- (void)setAnimation {
  DCHECK(self.consumer);
  NSString* animationName;
  if (self.promoContext == CredentialProviderPromoContext::kFirstStep) {
    animationName = kFirstStepAnimation;
  } else {
    animationName = kLearnMoreAnimation;
  }
  [self.consumer setAnimation:animationName];
}

// Sets the text and image to the consumer. The text set depends on the value of
// promoContext. When `source` is kPasswordCopied, no image is set.
- (void)setTextAndImageWithSource:(IOSCredentialProviderPromoSource)source {
  DCHECK(self.consumer);
  NSString* titleString;
  NSString* subtitleString;
  NSString* primaryActionString;
  UIImage* image;
  NSString* secondaryActionString =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_NO_THANKS);
  NSString* tertiaryActionString =
      l10n_util::GetNSString(IDS_IOS_CREDENTIAL_PROVIDER_PROMO_REMIND_ME_LATER);

  if (self.promoContext == CredentialProviderPromoContext::kFirstStep) {
    titleString = GetFirstStepTitleString();
    subtitleString = GetFirstStepSubtitleString();
    primaryActionString = GetFirstStepPrimaryActionString();
    image = ios::provider::GetBrandedImage(
        ios::provider::BrandedImage::kPasswordSuggestionKey);
  } else {
    titleString = l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_PROMO_LEARN_MORE_TITLE);
    subtitleString = GetLearnMoreSubtitleString();
    primaryActionString = l10n_util::GetNSString(
        IDS_IOS_CREDENTIAL_PROVIDER_PROMO_GO_TO_SETTINGS);
  }

  [self.consumer setTitleString:titleString
                 subtitleString:subtitleString
            primaryActionString:primaryActionString
          secondaryActionString:secondaryActionString
           tertiaryActionString:tertiaryActionString
                          image:image];
}

@end
