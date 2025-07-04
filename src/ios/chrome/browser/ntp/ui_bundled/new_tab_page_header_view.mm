// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_header_view.h"

#import <UIKit/UIKit.h>

#import <algorithm>

#import "base/check.h"
#import "base/feature_list.h"
#import "components/omnibox/common/omnibox_features.h"
#import "components/prefs/pref_service.h"
#import "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/content_suggestions_collection_utils.h"
#import "ios/chrome/browser/content_suggestions/ui_bundled/ntp_home_constant.h"
#import "ios/chrome/browser/lens/ui_bundled/lens_availability.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_constants.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_delegate.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_feature.h"
#import "ios/chrome/browser/ntp/ui_bundled/new_tab_page_header_constants.h"
#import "ios/chrome/browser/omnibox/public/omnibox_constants.h"
#import "ios/chrome/browser/omnibox/public/omnibox_ui_features.h"
#import "ios/chrome/browser/omnibox/ui/omnibox_container_view.h"
#import "ios/chrome/browser/omnibox/ui/omnibox_text_field_ios.h"
#import "ios/chrome/browser/shared/model/profile/features.h"
#import "ios/chrome/browser/shared/ui/elements/extended_touch_target_button.h"
#import "ios/chrome/browser/shared/ui/elements/new_feature_badge_view.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/browser/shared/ui/util/dynamic_type_util.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/browser/start_surface/ui_bundled/start_surface_features.h"
#import "ios/chrome/browser/toolbar/ui_bundled/buttons/toolbar_button_factory.h"
#import "ios/chrome/browser/toolbar/ui_bundled/buttons/toolbar_configuration.h"
#import "ios/chrome/browser/toolbar/ui_bundled/public/toolbar_constants.h"
#import "ios/chrome/browser/toolbar/ui_bundled/public/toolbar_utils.h"
#import "ios/chrome/browser/toolbar/ui_bundled/tab_groups/ui/tab_group_indicator_constants.h"
#import "ios/chrome/browser/toolbar/ui_bundled/tab_groups/ui/tab_group_indicator_view.h"
#import "ios/chrome/common/NSString+Chromium.h"
#import "ios/chrome/common/material_timing.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/common/ui/elements/gradient_view.h"
#import "ios/chrome/common/ui/util/constraints_ui_util.h"
#import "ios/chrome/common/ui/util/ui_util.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/public/provider/chrome/browser/lens/lens_api.h"
#import "ios/public/provider/chrome/browser/lottie/lottie_animation_api.h"
#import "ios/public/provider/chrome/browser/lottie/lottie_animation_configuration.h"
#import "ui/base/l10n/l10n_util.h"
#import "ui/gfx/ios/uikit_util.h"

namespace {

// Fakebox highlight animation duration.
const CGFloat kFakeboxHighlightDuration = 0.4;

// Fakebox highlight background alpha.
const CGFloat kFakeboxHighlightAlpha = 0.06;

// Height margin of the fake location bar.
const CGFloat kFakeLocationBarHeightMargin = 2;

// The constants for the constraints affecting the end button; either Lens or
// Voice Search, depending on if Lens is enabled.
const CGFloat kEndButtonFakeboxTrailingSpace = 13.0;
const CGFloat kEndButtonNormalSizeFakeboxWithBadgeTrailingSpace = 7.0;
const CGFloat kEndButtonMIAEnlargedFakebox = 20.0;
const CGFloat kEndButtonOmniboxTrailingSpace = 7.0;

// The constants for the constraints the leading-edge aligned UI elements.
const CGFloat kHintLabelFakeboxLeadingSpace = 28.0;
const CGFloat kHintLabelFakeboxLeadingSpaceWithIcon = 49.0;

const CGFloat kHintLabelOmniboxLeadingSpace = 20.0;
const CGFloat kHintLabelOmniboxLeadingSpaceWithIcon = 39.0;

const CGFloat kFakeboxImageLeadingSpace = 22.0;
const CGFloat kFakeboxImageSize = 16.0;

// The amount to inset the Fakebox from the rest of the modules on Home, when
// Large Fakebox is enabled.
const CGFloat kLargeFakeboxHorizontalMargin = 8.0;

// The spacing between the items in the button stack.
const CGFloat kButtonSpacing = 9.0;

// The height of the divider between the mic and lens icons.
const CGFloat kIconDividerHeight = 13.0;

// The offset of the account error badge from the ADP center.
const CGFloat kAccountBadgeOffsetFromDiscCenter = 10.0;

// The size of the account error badge that is on top the ADP.
const CGFloat kErrorSymbolPointSize = 16.0;

// The offset from the center of the customization button for where to show the
// new feature badge.
const CGFloat kCustomizationNewBadgeOffset = 14.0;

// The name of the animation for the MIA button.
NSString* const kMIACircleAnimationLightMode = @"mia_circle_animation_no_glow";
NSString* const kMIACircleAnimationDarkMode = @"mia_glowing_circle_animation";

// The value that makes the Lottie animation loop indefinitely.
const CGFloat kLottieInfiniteLoopFlag = -1;

// The value of the sides of the MIA circle animation for the normal size of the
// fakebox.
const CGFloat kMIACircleAnimationSizeNormal = 40.0;

// The value of the sides of the MIA circle animation for the enlarged size of
// the fakebox.
const CGFloat kMIACircleAnimationSizeEnlarged = 48.0;

// The amount to inset the Fakebox from the rest of the modules on Home.
CGFloat FakeboxHorizontalMargin(id<UITraitEnvironment> environment) {
  if (IsSplitToolbarMode(environment) && ShouldEnlargeLogoAndFakebox()) {
    return kLargeFakeboxHorizontalMargin;
  }
  return 0.0;
}

// Returns the top color of the Fakebox's gradient background.
UIColor* FakeboxTopColor() {
  return UIAccessibilityIsReduceTransparencyEnabled()
             ? [UIColor colorNamed:@"fake_omnibox_solid_background_color"]
             : [UIColor colorNamed:@"fake_omnibox_top_gradient_color"];
}

// Returns the bottom color of the Fakebox's gradient background.
UIColor* FakeboxBottomColor() {
  return UIAccessibilityIsReduceTransparencyEnabled()
             ? [UIColor colorNamed:@"fake_omnibox_solid_background_color"]
             : [UIColor colorNamed:@"fake_omnibox_bottom_gradient_color"];
}

// Returns the background color for the NTP Header view. This is the color
// that shows when the fakebox is scrolled up.
UIColor* HeaderBackgroundColor(id<UITraitEnvironment> environment) {
  if (IsSplitToolbarMode(environment)) {
    return [UIColor colorNamed:kBackgroundColor];
  } else {
    return [UIColor colorNamed:@"ntp_background_color"];
  }
}

UIColor* AccountParticleDiscBadgeBackgroundColor(UIUserInterfaceStyle style) {
  if (style == UIUserInterfaceStyleDark) {
    return [UIColor colorNamed:kBackgroundColor];
  } else {
    return [UIColor colorNamed:@"ntp_background_color"];
  }
}

// Returns a color which is a blend of `color_1` and `color_2`, depending on
// the value of `fraction`. `fraction` is a value between 0 and 1. If it is
// closer to 0, the output will be closer to `color_1`, and if it is closer to
// 1 the output will be closer to `color_2`.
UIColor* BlendColors(UIColor* color_1, UIColor* color_2, CGFloat fraction) {
  if (fraction <= 0.0) {
    return color_1;
  } else if (fraction >= 1.0) {
    return color_2;
  } else if ([color_1 isEqual:color_2]) {
    return color_1;
  }

  // Get RGBA components for the two colors, as inputs to the blend.
  CGFloat in_1[4];
  CGFloat in_2[4];
  [color_1 getRed:&in_1[0] green:&in_1[1] blue:&in_1[2] alpha:&in_1[3]];
  [color_2 getRed:&in_2[0] green:&in_2[1] blue:&in_2[2] alpha:&in_2[3]];

  // Blend each RGBA color component, based on the given fraction.
  CGFloat out[4];
  CGFloat inverse = 1.0 - fraction;
  for (int i = 0; i < 4; i++) {
    out[i] = inverse * in_1[i] + fraction * in_2[i];
  }

  return [UIColor colorWithRed:out[0] green:out[1] blue:out[2] alpha:out[3]];
}

// Returns a value in the range of `from` up to `to`, depending on the given
// `percent`.
CGFloat Interpolate(CGFloat from, CGFloat to, CGFloat percent) {
  if (percent <= 0.0) {
    return from;
  } else if (percent >= 1.0) {
    return to;
  }
  return from + (to - from) * percent;
}

// Computes the opacity of the MIA animation given the scroll percent of the
// view.
CGFloat MIAAnimationOpacityForScrollProgress(CGFloat percent) {
  // The progress is inversely proportional with the scroll percentage, meaning
  // that a scroll percent of 0 corresponds to full opacity.
  //
  // To avoid showing a mostly faded animation view for intermediary scrolls,
  // follow a scaled exponential curve that will ease in the animation.
  CGFloat unboundOpacity = 1 - 6 * pow(percent, 4);
  return MIN(MAX(unboundOpacity, 0), 1);
}

}  // namespace

@interface NewTabPageHeaderView ()

// The Lens button. May be null if Lens is not available.
@property(nonatomic, strong, readwrite) ExtendedTouchTargetButton* lensButton;
// The MIA button. May be null if MIA is not available.
@property(nonatomic, strong, readwrite) ExtendedTouchTargetButton* miaButton;
@property(nonatomic, strong) UIView* voiceAndLensDivider;
@property(nonatomic, strong) UIView* miaAndVoiceDivider;

@property(nonatomic, strong, readwrite)
    ExtendedTouchTargetButton* voiceSearchButton;

@property(nonatomic, strong) UIView* separator;

// Layout constraints for fake omnibox background image and blur.
@property(nonatomic, strong) NSLayoutConstraint* fakeLocationBarTopConstraint;
@property(nonatomic, strong)
    NSLayoutConstraint* fakeLocationBarHeightConstraint;
@property(nonatomic, strong) NSLayoutConstraint* hintLabelLeadingConstraint;
@property(nonatomic, strong) NSLayoutConstraint* hintLabelTrailingConstraint;
// View used to add on-touch highlight to the fake omnibox.
@property(nonatomic, strong) UIView* fakeLocationBarHighlightView;
// View used to simulate the top toolbar when the header is stuck to the top of
// the NTP.
@property(nonatomic, strong) UIView* fakeToolbar;

// Whether to use MIA inline with other action buttons.
@property(nonatomic, readonly) BOOL useInlineMIA;
// Whether to only display the MIA button.
@property(nonatomic, readonly) BOOL useSingleButtonMIA;
// Whether the MIA entry point is being shown.
@property(nonatomic, readonly) BOOL shouldShowMIAEntrypoint;
// Whether the fakebox is enlarged due to a MIA entry point variation.
@property(nonatomic, readonly) BOOL useMIAEnlargedFakebox;

@end

@implementation NewTabPageHeaderView {
  CGFloat _lastAnimationPercent;
  BOOL _useNewBadgeForLensButton;
  BOOL _lensButtonWithNewBadgeTapped;
  // The current scale of the transform for the hint label. 1 if not currently
  //  scaled.
  CGFloat _currentHintLabelScale;
  // Stores the small font used for the pinned fakebox.
  UIFont* _hintLabelFontSmall;
  // Stores the big font used for the unpinned fakebox.
  UIFont* _hintLabelFontBig;
  // Image view of the account disc particle badge.
  UIImageView* _accountDiscParticleBadgeImageView;
  // The New Feature badge on the customization menu's entrypoint.
  UIView* _customizationNewFeatureBadge;
  // A view to contain all the buttons on the trailing side of the fakebox.
  UIStackView* _buttonStack;
  // Default search engine logo view.
  UIImageView* _logoView;

  // Constraints to update the `toolbarView`'s postion according to the
  // `tabGroupIndicatorView`'s visibility.
  NSLayoutConstraint* _toolbarNoTabGroupIndicartorConstraint;
  NSLayoutConstraint* _toolbarTabGroupIndicartorConstraint;

  // Maintains the MIA circle animation.
  id<LottieAnimation> _miaAnimation;
  UIView* _miaAnimationView;
}

#pragma mark - Public

- (instancetype)initWithUseNewBadgeForLensButton:
    (BOOL)useNewBadgeForLensButton {
  self = [super initWithFrame:CGRectZero];
  if (self) {
    self.clipsToBounds = YES;
    _useNewBadgeForLensButton = useNewBadgeForLensButton;
    _lastAnimationPercent = 0;
    _currentHintLabelScale = 1;

    if (@available(iOS 17, *)) {
      NSArray<UITrait>* traits = TraitCollectionSetForTraits(@[
        UITraitPreferredContentSizeCategory.class,
        UITraitUserInterfaceStyle.class
      ]);
      __weak __typeof(self) weakSelf = self;
      UITraitChangeHandler handler = ^(id<UITraitEnvironment> traitEnvironment,
                                       UITraitCollection* previousCollection) {
        [weakSelf updateUIOnTraitChange:previousCollection];
      };
      [self registerForTraitChanges:traits withHandler:handler];
    }
  }
  return self;
}

- (void)addToolbarView:(UIView*)toolbarView {
  _toolBarView = toolbarView;
  [self addSubview:toolbarView];

  _toolbarNoTabGroupIndicartorConstraint =
      [toolbarView.topAnchor constraintEqualToAnchor:self.topAnchor];
  [NSLayoutConstraint activateConstraints:@[
    [toolbarView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [toolbarView.heightAnchor
        constraintEqualToConstant:content_suggestions::FakeToolbarHeight()],
    [toolbarView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
    _toolbarNoTabGroupIndicartorConstraint,
  ]];
}

- (void)setIdentityDiscView:(UIView*)identityDiscView {
  DCHECK(identityDiscView);
  _identityDiscView = identityDiscView;
  [self.toolBarView addSubview:_identityDiscView];

  // Sets the layout constraints for size of Identity Disc and toolbar.
  self.identityDiscView.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [self.identityDiscView.centerYAnchor
        constraintEqualToAnchor:self.toolBarView.centerYAnchor],
  ]];
}

- (void)setPlaceholderText:(NSString*)placeholderText {
  if (_placeholderText == placeholderText) {
    return;
  }
  _placeholderText = placeholderText;
  self.omnibox.textField.placeholder = placeholderText;
  self.searchHintLabel.text = placeholderText;
}

- (void)addViewsToSearchField:(UIView*)searchField {
  // Fake Toolbar.
  self.fakeToolbar = [[UIView alloc] init];
  [searchField insertSubview:self.fakeToolbar atIndex:0];
  self.fakeToolbar.translatesAutoresizingMaskIntoConstraints = NO;

  // Fake location bar.
  [self.fakeToolbar addSubview:self.fakeLocationBar];

  // Omnibox, used for animations.
  // TODO(crbug.com/40615993): See if it is possible to share some
  // initialization code with the real Omnibox.
  UIColor* color = [UIColor colorNamed:kTextfieldPlaceholderColor];
  OmniboxContainerView* omnibox =
      [[OmniboxContainerView alloc] initWithFrame:CGRectZero
                                        textColor:color
                                    textFieldTint:color
                                         iconTint:color
                                    isLensOverlay:NO];
  omnibox.textField.placeholder = self.placeholderText;
  [omnibox.textField setText:@""];
  omnibox.translatesAutoresizingMaskIntoConstraints = NO;
  [searchField addSubview:omnibox];
  AddSameConstraints(omnibox, self.fakeLocationBar);
  omnibox.textField.userInteractionEnabled = NO;
  omnibox.hidden = YES;
  self.omnibox = omnibox;

  // Cancel button, used in animation.
  ToolbarButtonFactory* factory =
      [[ToolbarButtonFactory alloc] initWithStyle:ToolbarStyle::kNormal];
  self.cancelButton = [factory cancelButton];
  [searchField addSubview:self.cancelButton];
  self.cancelButton.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [self.cancelButton.centerYAnchor
        constraintEqualToAnchor:self.fakeLocationBar.centerYAnchor],
    [self.cancelButton.leadingAnchor
        constraintEqualToAnchor:self.fakeLocationBar.trailingAnchor],
  ]];

  // Hint label.
  self.searchHintLabel = [[UILabel alloc] init];
  content_suggestions::ConfigureSearchHintLabel(
      self.searchHintLabel, searchField, self.placeholderText);
  [self updateHintLabelFonts];

  self.hintLabelLeadingConstraint = [self.searchHintLabel.leadingAnchor
      constraintEqualToAnchor:self.fakeLocationBar.leadingAnchor
                     constant:self.hintLabelFakeboxLeadingSpace];
  [NSLayoutConstraint activateConstraints:@[
    self.hintLabelLeadingConstraint,
    [self.searchHintLabel.heightAnchor
        constraintEqualToAnchor:self.fakeLocationBar.heightAnchor
                       constant:-ntp_header::kHintLabelHeightMargin],
    [self.searchHintLabel.centerYAnchor
        constraintEqualToAnchor:self.fakeLocationBar.centerYAnchor
                       constant:-1.0],
  ]];
  // Set a button the same size as the fake omnibox as the accessibility
  // element. If the hint is the only accessible element, when the fake omnibox
  // is taking the full width, there are few points that are not accessible and
  // allow to select the content below it.
  self.searchHintLabel.isAccessibilityElement = NO;
  [self.searchHintLabel
      setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                      forAxis:UILayoutConstraintAxisHorizontal];

  _buttonStack = [[UIStackView alloc] init];
  _buttonStack.translatesAutoresizingMaskIntoConstraints = NO;
  _buttonStack.alignment = UIStackViewAlignmentCenter;
  _buttonStack.spacing = kButtonSpacing;
  _buttonStack.directionalLayoutMargins = NSDirectionalEdgeInsetsMake(
      0, 0, 0, [self endButtonFakeboxTrailingSpace]);
  _buttonStack.layoutMarginsRelativeArrangement = true;
  [searchField addSubview:_buttonStack];
  [NSLayoutConstraint activateConstraints:@[
    [_buttonStack.trailingAnchor
        constraintEqualToAnchor:self.fakeLocationBar.trailingAnchor],
    [_buttonStack.centerYAnchor
        constraintEqualToAnchor:self.fakeLocationBar.centerYAnchor],
  ]];

  if (self.shouldShowMIAEntrypoint) {
    self.miaButton =
        [ExtendedTouchTargetButton buttonWithType:UIButtonTypeSystem];
    [self.miaButton
        setAccessibilityLabel:l10n_util::GetNSString(IDS_IOS_ACCNAME_MIA)];
    [self.miaButton setAccessibilityIdentifier:@"MIA"];

    [_buttonStack addArrangedSubview:self.miaButton];
    if (self.useInlineMIA) {
      [self addMIAAndVoiceDivider];
    } else if (self.useSingleButtonMIA) {
      [self updateAnimationOnMIAButton];
    }
  }

  BOOL displayOtherActions = !self.useSingleButtonMIA;

  if (displayOtherActions) {
    // Voice search.
    self.voiceSearchButton =
        [ExtendedTouchTargetButton buttonWithType:UIButtonTypeSystem];
    [_buttonStack addArrangedSubview:self.voiceSearchButton];
  }

  // Lens.
  const BOOL useLens =
      lens_availability::CheckAndLogAvailabilityForLensEntryPoint(
          LensEntrypoint::NewTabPage, self.isGoogleDefaultSearchEngine);
  if (useLens && displayOtherActions) {
    [self addVoiceAndLensDivider];
    self.lensButton =
        [ExtendedTouchTargetButton buttonWithType:UIButtonTypeSystem];
    [_buttonStack addArrangedSubview:self.lensButton];
    if (_useNewBadgeForLensButton) {
      [self.lensButton addTarget:self
                          action:@selector(lensButtonWithNewBadgeTapped:)
                forControlEvents:UIControlEventTouchUpInside];
    }
  }

  [self updateButtonsForUserInterfaceStyle:self.traitCollection
                                               .userInterfaceStyle];

  // Constraints.
  AddSameConstraints(self.fakeToolbar, searchField);

  self.fakeLocationBarTopConstraint = [self.fakeLocationBar.topAnchor
      constraintEqualToAnchor:searchField.topAnchor];
  self.fakeLocationBarLeadingConstraint = [self.fakeLocationBar.leadingAnchor
      constraintEqualToAnchor:searchField.leadingAnchor
                     constant:FakeboxHorizontalMargin(self)];
  self.fakeLocationBarTrailingConstraint = [self.fakeLocationBar.trailingAnchor
      constraintEqualToAnchor:searchField.trailingAnchor
                     constant:FakeboxHorizontalMargin(self)];
  self.fakeLocationBarHeightConstraint = [self.fakeLocationBar.heightAnchor
      constraintEqualToConstant:content_suggestions::FakeOmniboxHeight()];
  [NSLayoutConstraint activateConstraints:@[
    self.fakeLocationBarTopConstraint,
    self.fakeLocationBarLeadingConstraint,
    self.fakeLocationBarTrailingConstraint,
    self.fakeLocationBarHeightConstraint,
  ]];

  UIButton* referenceButton = self.voiceSearchButton ?: self.miaButton;
  self.hintLabelTrailingConstraint = [self.searchHintLabel.trailingAnchor
      constraintLessThanOrEqualToAnchor:referenceButton.leadingAnchor];
  self.hintLabelTrailingConstraint.priority = UILayoutPriorityDefaultHigh;
  [NSLayoutConstraint activateConstraints:@[
    [referenceButton.centerYAnchor
        constraintEqualToAnchor:self.fakeLocationBar.centerYAnchor],
    self.hintLabelTrailingConstraint,
  ]];

  [self addSearchEngineLogoIfNeededToSearchField:searchField];
}

- (void)addSearchEngineLogoIfNeededToSearchField:(UIView*)searchField {
  if (!base::FeatureList::IsEnabled(omnibox::kOmniboxMobileParityUpdate)) {
    return;
  }

  UIImageView* logoView = [[UIImageView alloc] init];
  logoView.contentMode = UIViewContentModeScaleAspectFit;
  [searchField addSubview:logoView];

  logoView.translatesAutoresizingMaskIntoConstraints = NO;
  AddSquareConstraints(logoView, kFakeboxImageSize);

  [NSLayoutConstraint activateConstraints:@[
    [logoView.leadingAnchor constraintEqualToAnchor:searchField.leadingAnchor
                                           constant:kFakeboxImageLeadingSpace],
    [logoView.centerYAnchor constraintEqualToAnchor:searchField.centerYAnchor
                                           constant:-2.0],

  ]];

  _logoView = logoView;
}

- (void)setDefaultSearchEngineLogo:(UIImage*)logo {
  _logoView.image = logo;
}

- (void)updateButtonsForUserInterfaceStyle:(UIUserInterfaceStyle)style {
  // Variations containing MIA entry point force disable colors in the icons.
  const BOOL forceDisableColors = self.shouldShowMIAEntrypoint;
  const BOOL darkUIStyle = style == UIUserInterfaceStyleDark;
  const BOOL useColorIcon = !darkUIStyle && !forceDisableColors;

  content_suggestions::ConfigureVoiceSearchButton(self.voiceSearchButton,
                                                  useColorIcon);
  if (self.lensButton) {
    content_suggestions::ConfigureLensButtonAppearance(
        self.lensButton, _useNewBadgeForLensButton, useColorIcon);
    if (_useNewBadgeForLensButton) {
      content_suggestions::ConfigureLensButtonWithNewBadgeAlpha(
          self.lensButton, 1 - _lastAnimationPercent);
    }
  }

  if (self.miaButton) {
    content_suggestions::ConfigureInlineMIAButton(self.miaButton, useColorIcon);
  }
}

- (void)addSeparatorToSearchField:(UIView*)searchField {
  DCHECK(searchField.superview == self);

  self.separator = [[UIView alloc] init];
  self.separator.backgroundColor = [UIColor colorNamed:kToolbarShadowColor];
  self.separator.alpha = 0;
  self.separator.translatesAutoresizingMaskIntoConstraints = NO;
  [searchField addSubview:self.separator];
  [NSLayoutConstraint activateConstraints:@[
    [self.separator.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [self.separator.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
    [self.separator.topAnchor constraintEqualToAnchor:searchField.bottomAnchor],
    [self.separator.heightAnchor
        constraintEqualToConstant:content_suggestions::HeaderSeparatorHeight()],
  ]];
}

- (CGFloat)searchFieldProgressForOffset:(CGFloat)offset {
  // The scroll offset at which point searchField's frame should stop growing.
  CGFloat maxScaleOffset = [self offsetToBeginFakeOmniboxExpansion];

  // The scroll offset at which point searchField's frame should start
  // growing.
  CGFloat startScaleOffset = maxScaleOffset - ntp_header::kAnimationDistance;
  CGFloat percent = 0;
  if (offset && offset > startScaleOffset) {
    CGFloat animatingOffset = offset - startScaleOffset;
    percent = std::clamp<CGFloat>(
        animatingOffset / ntp_header::kAnimationDistance, 0, 1);
  }
  if (!IsSplitToolbarMode(self)) {
    // For ipad and landscape iphone, this makes the animation start slowly
    // and accelerate especially towards the end.
    percent = percent * percent;
  }
  return percent;
}

- (void)updateSearchFieldWidth:(NSLayoutConstraint*)widthConstraint
                        height:(NSLayoutConstraint*)heightConstraint
                     topMargin:(NSLayoutConstraint*)topMarginConstraint
                     forOffset:(CGFloat)offset
                   screenWidth:(CGFloat)screenWidth
                safeAreaInsets:(UIEdgeInsets)safeAreaInsets {
  CGFloat contentWidth = std::max<CGFloat>(
      0, screenWidth - safeAreaInsets.left - safeAreaInsets.right);
  if (screenWidth == 0 || contentWidth == 0) {
    return;
  }

  CGFloat searchFieldNormalWidth =
      content_suggestions::SearchFieldWidth(contentWidth, self.traitCollection);

  CGFloat percent = [self searchFieldProgressForOffset:offset];

  _miaAnimationView.alpha = MIAAnimationOpacityForScrollProgress(percent);

  [self updateTabGroupIndicatorAvailabilityWithOffset:offset];

  // Update the opacity of the header background color as the user scrolls so
  // that content does not appear beneath it. Since the NTP background might be
  // a gradient, the opacity must be 0 by default.
  self.backgroundColor =
      [HeaderBackgroundColor(self) colorWithAlphaComponent:percent];

  [self setFakeboxBackgroundWithProgress:percent];

  // Offset the hint label constraints with half of the change in width
  // from the original scale, since constraints are calculated before
  // transformations are applied. This prevents the label from overlapping
  // with other UI elements.
  [self scaleHintLabelForPercent:percent];
  CGFloat hintLabelScalingExtraOffset =
      (_currentHintLabelScale - 1) *
      self.searchHintLabel.intrinsicContentSize.width * 0.5;
  self.hintLabelTrailingConstraint.constant = -hintLabelScalingExtraOffset;

  CGFloat fakeOmniboxHeight = content_suggestions::FakeOmniboxHeight();
  CGFloat locationBarHeight = content_suggestions::PinnedFakeOmniboxHeight();

  if (!IsSplitToolbarMode(self)) {
    // When Voiceover is running, if the header's alpha is set to 0, voiceover
    // can't scroll back to it, and it will never come back into view. To
    // prevent that, set the alpha to non-zero when the header is fully
    // offscreen. It will still not be seen, but it will be accessible to
    // Voiceover.
    self.alpha = std::max(1 - percent, 0.01);

    widthConstraint.constant = searchFieldNormalWidth;
    self.fakeLocationBarHeightConstraint.constant =
        fakeOmniboxHeight - kFakeLocationBarHeightMargin;
    self.fakeLocationBar.layer.cornerRadius =
        self.fakeLocationBarHeightConstraint.constant / 2;

    self.fakeLocationBarLeadingConstraint.constant = 0;
    self.fakeLocationBarTrailingConstraint.constant = 0;
    self.fakeLocationBarTopConstraint.constant = 0;

    // Reset the view horizontal constraints.
    self.hintLabelLeadingConstraint.constant =
        self.hintLabelFakeboxLeadingSpace + hintLabelScalingExtraOffset;

    self.separator.alpha = 0;

    _lastAnimationPercent = percent;
    return;
  }

  self.alpha = 1;
  self.separator.alpha = percent;

  // Calculate the amount to grow the width and height of searchField so that
  // its frame covers the entire toolbar area.
  CGFloat maxWidth = self.bounds.size.width;
  widthConstraint.constant =
      Interpolate(searchFieldNormalWidth, maxWidth, percent);
  CGFloat maxTopMarginDiff = fakeOmniboxHeight - locationBarHeight -
                             kAdaptiveLocationBarVerticalMargin;
  topMarginConstraint.constant =
      -content_suggestions::SearchFieldTopMargin() - maxTopMarginDiff * percent;
  heightConstraint.constant =
      ntp_header::kFakeLocationBarTopConstraint -
      content_suggestions::HeaderSeparatorHeight() +
      Interpolate(fakeOmniboxHeight,
                  locationBarHeight + kAdaptiveLocationBarVerticalMargin,
                  percent);

  // Calculate the amount to shrink the width and height of background so that
  // it's where the focused adapative toolbar focuses.
  CGFloat horizontalMargin = FakeboxHorizontalMargin(self);
  self.fakeLocationBarLeadingConstraint.constant = Interpolate(
      horizontalMargin,
      safeAreaInsets.left + kExpandedLocationBarHorizontalMargin, percent);
  self.fakeLocationBarTrailingConstraint.constant = -Interpolate(
      horizontalMargin,
      safeAreaInsets.right + kExpandedLocationBarHorizontalMargin, percent);

  self.fakeLocationBarTopConstraint.constant =
      ntp_header::kFakeLocationBarTopConstraint * percent;
  self.fakeLocationBarHeightConstraint.constant =
      Interpolate(fakeOmniboxHeight, locationBarHeight, percent);
  self.fakeLocationBar.layer.cornerRadius =
      self.fakeLocationBarHeightConstraint.constant / 2;

  // Adjust the position of the search field's subviews.
  CGFloat endButtonInset = Interpolate([self endButtonFakeboxTrailingSpace],
                                       kEndButtonOmniboxTrailingSpace, percent);
  _buttonStack.directionalLayoutMargins =
      NSDirectionalEdgeInsetsMake(0, 0, 0, endButtonInset);
  self.hintLabelLeadingConstraint.constant =
      hintLabelScalingExtraOffset +
      Interpolate(self.hintLabelFakeboxLeadingSpace,
                  self.hintLabelOmniboxLeadingSpace, percent);

  // Fade N badge treatment when scrolled.
  if (_useNewBadgeForLensButton && !_lensButtonWithNewBadgeTapped &&
      self.lensButton) {
    content_suggestions::ConfigureLensButtonWithNewBadgeAlpha(self.lensButton,
                                                              1 - percent);
    // Hide divider when N badge is shown.
    self.voiceAndLensDivider.alpha = percent;
    self.miaAndVoiceDivider.alpha = percent;
  }

  _lastAnimationPercent = percent;
}

- (void)setFakeboxHighlighted:(BOOL)highlighted {
  [UIView animateWithDuration:kFakeboxHighlightDuration
                        delay:0
                      options:UIViewAnimationOptionCurveEaseOut
                   animations:^{
                     CGFloat alpha = highlighted ? kFakeboxHighlightAlpha : 0;
                     self.fakeLocationBarHighlightView.backgroundColor =
                         [UIColor colorWithWhite:0 alpha:alpha];
                   }
                   completion:nil];
}

- (void)setIdentityDiscErrorBadge {
  if (!_identityDiscView) {
    return;
  }
  _accountDiscParticleBadgeImageView = [[UIImageView alloc]
      initWithImage:DefaultSymbolWithPointSize(kErrorCircleFillSymbol,
                                               kErrorSymbolPointSize)];
  _accountDiscParticleBadgeImageView.translatesAutoresizingMaskIntoConstraints =
      NO;
  _accountDiscParticleBadgeImageView.tintColor =
      [UIColor colorNamed:kRed500Color];
  _accountDiscParticleBadgeImageView.backgroundColor =
      AccountParticleDiscBadgeBackgroundColor(
          self.traitCollection.userInterfaceStyle);
  _accountDiscParticleBadgeImageView.layer.cornerRadius =
      _accountDiscParticleBadgeImageView.frame.size.width / 2;
  _accountDiscParticleBadgeImageView.clipsToBounds = YES;
  _accountDiscParticleBadgeImageView.accessibilityIdentifier =
      kNTPFeedHeaderIdentityDiscBadge;

  [_identityDiscView addSubview:_accountDiscParticleBadgeImageView];

  [NSLayoutConstraint activateConstraints:@[
    [_accountDiscParticleBadgeImageView.centerXAnchor
        constraintEqualToAnchor:_identityDiscView.centerXAnchor
                       constant:kAccountBadgeOffsetFromDiscCenter],
    [_accountDiscParticleBadgeImageView.centerYAnchor
        constraintEqualToAnchor:_identityDiscView.centerYAnchor
                       constant:kAccountBadgeOffsetFromDiscCenter],
  ]];
}

- (void)removeIdentityDiscErrorBadge {
  [_accountDiscParticleBadgeImageView removeFromSuperview];
  _accountDiscParticleBadgeImageView = nil;
}

- (void)setCustomizationMenuButton:(UIButton*)customizationMenuButton
                      withNewBadge:(BOOL)hasNewBadge {
  if (_customizationMenuButton) {
    [_customizationMenuButton removeFromSuperview];
  }

  customizationMenuButton.translatesAutoresizingMaskIntoConstraints = NO;
  customizationMenuButton.layer.cornerRadius =
      ntp_home::kCustomizationMenuButtonCornerRadius;
  customizationMenuButton.pointerInteractionEnabled = YES;
  customizationMenuButton.clipsToBounds = YES;

  NewFeatureBadgeView* newBadgeView =
      [[NewFeatureBadgeView alloc] initWithBadgeSize:20 fontSize:10];
  newBadgeView.translatesAutoresizingMaskIntoConstraints = NO;
  newBadgeView.userInteractionEnabled = NO;
  newBadgeView.layer.opacity = hasNewBadge ? 1 : 0;

  [self.toolBarView addSubview:customizationMenuButton];
  [self.toolBarView addSubview:newBadgeView];

  [NSLayoutConstraint activateConstraints:@[
    [customizationMenuButton.centerYAnchor
        constraintEqualToAnchor:self.toolBarView.centerYAnchor],
    [customizationMenuButton.heightAnchor
        constraintEqualToConstant:ntp_home::kCustomizationMenuButtonDimension],
    [customizationMenuButton.widthAnchor
        constraintEqualToAnchor:customizationMenuButton.heightAnchor],
    [customizationMenuButton.leadingAnchor
        constraintEqualToAnchor:self.safeAreaLayoutGuide.leadingAnchor
                       constant:(ntp_home::kIdentityAvatarPadding +
                                 ntp_home::kHeaderIconMargin)],
    [newBadgeView.centerXAnchor
        constraintEqualToAnchor:customizationMenuButton.centerXAnchor
                       constant:kCustomizationNewBadgeOffset],
    [newBadgeView.centerYAnchor
        constraintEqualToAnchor:customizationMenuButton.centerYAnchor
                       constant:-kCustomizationNewBadgeOffset],
  ]];

  _customizationMenuButton = customizationMenuButton;
  _customizationNewFeatureBadge = newBadgeView;
}

- (void)hideBadgeOnCustomizationMenu {
  _customizationNewFeatureBadge.alpha = 0;
}

- (void)updateTabGroupIndicatorAvailabilityWithOffset:(CGFloat)offset {
  BOOL canShowTabStrip = IsRegularXRegularSizeClass(self);
  BOOL isAvailable = !IsCompactHeight(self) && !canShowTabStrip;
  _tabGroupIndicatorView.available = isAvailable;

  // Make the view disappear when the indicator is scrolled out of the screen.
  // The absolute value of the offset is used to make the view disappear when:
  // - Scrolling down to reveal the Discover section.
  // - Scrolling up to reveal the overscroll actions.
  _tabGroupIndicatorView.alpha =
      1 - fmax(0, (abs(offset) / kTabGroupIndicatorHeight));

  _toolbarTabGroupIndicartorConstraint.constant =
      kTabGroupIndicatorNTPToolbarMargin - fmax(offset, 0);
  if (_tabGroupIndicatorView.hidden) {
    _toolbarTabGroupIndicartorConstraint.active = NO;
    _toolbarNoTabGroupIndicartorConstraint.active = YES;
  } else {
    _toolbarNoTabGroupIndicartorConstraint.active = NO;
    _toolbarTabGroupIndicartorConstraint.active = YES;
  }
}

- (UIView*)fakeboxButtonsSnapshot {
  return [_buttonStack snapshotViewAfterScreenUpdates:NO];
}

#pragma mark - UITraitEnvironment

#if !defined(__IPHONE_17_0) || __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_17_0
- (void)traitCollectionDidChange:(UITraitCollection*)previousTraitCollection {
  [super traitCollectionDidChange:previousTraitCollection];
  if (@available(iOS 17, *)) {
    return;
  }

  [self updateUIOnTraitChange:previousTraitCollection];
}

#endif

#pragma mark - Property accessors

- (UIView*)fakeLocationBar {
  if (!_fakeLocationBar) {
    _fakeLocationBar =
        [[GradientView alloc] initWithTopColor:FakeboxTopColor()
                                   bottomColor:FakeboxBottomColor()];
    _fakeLocationBar.userInteractionEnabled = NO;
    _fakeLocationBar.clipsToBounds = YES;
    _fakeLocationBar.translatesAutoresizingMaskIntoConstraints = NO;
    _fakeLocationBarHighlightView = [[UIView alloc] init];
    _fakeLocationBarHighlightView.userInteractionEnabled = NO;
    _fakeLocationBarHighlightView.backgroundColor = UIColor.clearColor;
    _fakeLocationBarHighlightView.translatesAutoresizingMaskIntoConstraints =
        NO;
    [_fakeLocationBar addSubview:_fakeLocationBarHighlightView];
    AddSameConstraints(_fakeLocationBar, _fakeLocationBarHighlightView);
  }
  return _fakeLocationBar;
}

#pragma mark - Setters

// Sets tabgroupIndicatorView.
- (void)setTabGroupIndicatorView:(TabGroupIndicatorView*)view {
  _tabGroupIndicatorView = view;
  _tabGroupIndicatorView.hidden = YES;
  _tabGroupIndicatorView.translatesAutoresizingMaskIntoConstraints = NO;
  _tabGroupIndicatorView.showSeparator = YES;
  [self addSubview:_tabGroupIndicatorView];

  _toolbarTabGroupIndicartorConstraint = [_toolBarView.topAnchor
      constraintEqualToAnchor:_tabGroupIndicatorView.bottomAnchor
                     constant:kTabGroupIndicatorNTPToolbarMargin];
  [NSLayoutConstraint activateConstraints:@[
    [self.tabGroupIndicatorView.leadingAnchor
        constraintEqualToAnchor:self.leadingAnchor],
    [self.tabGroupIndicatorView.trailingAnchor
        constraintEqualToAnchor:self.trailingAnchor],
    [self.tabGroupIndicatorView.topAnchor
        constraintEqualToAnchor:self.topAnchor
                       constant:kTabGroupIndicatorNTPTopMargin],
    [_tabGroupIndicatorView.heightAnchor
        constraintEqualToConstant:kTabGroupIndicatorHeight],
  ]];
  [self updateTabGroupIndicatorAvailabilityWithOffset:0];
}

#pragma mark - Private

// Gets the fonts for the pinned and unpinned fakebox hint label, and sets
// the correct one.
- (void)updateHintLabelFonts {
  _hintLabelFontSmall = LocationBarSteadyViewFont(
      self.traitCollection.preferredContentSizeCategory);
  CGFloat bigFontSize = _hintLabelFontSmall.pointSize /
                        (1.0 - content_suggestions::kHintTextScale);
  _hintLabelFontBig = [_hintLabelFontSmall fontWithSize:bigFontSize];
  self.searchHintLabel.font =
      [self hintLabelFontForPercent:_lastAnimationPercent];
}

// Returns the font for the hint label at the given animation percent.
- (UIFont*)hintLabelFontForPercent:(CGFloat)percent {
  if (percent == 1 && !self.allowFontScaleAnimation) {
    return _hintLabelFontSmall;
  }
  return _hintLabelFontBig;
}

// Scale the the hint label down to at most content_suggestions::kHintTextScale.
- (void)scaleHintLabelForPercent:(CGFloat)percent {
  DCHECK(self.searchHintLabel);
  if (percent == _lastAnimationPercent) {
    return;
  }

  if (percent > 0.90) {
    // When percent is very close to 1, the big font will be scaled down to be
    // almost the same size as the small font. But due to rendering differences
    // the big font scaled down can actually look slightly smaller than the
    // small font. By switching to the small font 10% early, a glitchy jump in
    // size is avoided.
    percent = 1;
  }

  UILabel* searchHintLabel = self.searchHintLabel;
  UIFont* font = [self hintLabelFontForPercent:percent];
  if (searchHintLabel.font != font) {
    searchHintLabel.font = font;
  }

  if (percent == 1 && !self.allowFontScaleAnimation) {
    // When pinned, the small font is used without scaling down.
    _currentHintLabelScale = 1;
    searchHintLabel.transform = CGAffineTransformIdentity;
    return;
  }

  // When unpinned, the bigger font is used and scaling is applied depending on
  // the animation percent.
  _currentHintLabelScale = 1 - (content_suggestions::kHintTextScale * percent);
  searchHintLabel.transform = CGAffineTransformMakeScale(
      _currentHintLabelScale, _currentHintLabelScale);
}

// The positive offset value to begin the fake omnibox expansion animation.
- (CGFloat)offsetToBeginFakeOmniboxExpansion {
  CGFloat offset =
      self.frame.size.height - content_suggestions::FakeToolbarHeight();

  // For non-split toolbar, the fake omnibox goes beneath the toolbar.
  if (!IsSplitToolbarMode(self)) {
    // The animation should start when the primary toolbar is met.
    offset += content_suggestions::FakeOmniboxHeight();

    // iPads pin slightly earlier than landscape iPhones.
    if (IsRegularXRegularSizeClass(self)) {
      offset -= content_suggestions::SearchFieldTopMargin();
    }
  }
  return offset;
}

// Sets the fakebox's background gradient colors, based on the progress towards
// being pinned at the top.
- (void)setFakeboxBackgroundWithProgress:(CGFloat)progress {
  UIColor* pinnedColor = [UIColor colorNamed:kTextfieldBackgroundColor];

  // Use a quadratic curve interpolation.
  progress = progress * progress;
  [_fakeLocationBar
      setStartColor:BlendColors(FakeboxTopColor(), pinnedColor, progress)
           endColor:BlendColors(FakeboxBottomColor(), pinnedColor, progress)];
}

// Creates a thin grey divider that acts as a visual separator.
- (UIView*)createDivider {
  UIView* divider = [[UIView alloc] init];
  divider.backgroundColor = [UIColor colorNamed:kGrey600Color];
  divider.translatesAutoresizingMaskIntoConstraints = NO;
  CGFloat dividerWidth = 1.0 / [[UIScreen mainScreen] scale];

  [NSLayoutConstraint activateConstraints:@[
    [divider.heightAnchor constraintEqualToConstant:kIconDividerHeight],
    [divider.widthAnchor constraintEqualToConstant:dividerWidth],
  ]];

  return divider;
}

// Adds a short vertical line between the mic and lens icons in the fakebox.
- (void)addVoiceAndLensDivider {
  UIView* divider = [self createDivider];
  self.voiceAndLensDivider = divider;
  [_buttonStack addArrangedSubview:divider];
}

// Adds a short vertical line between the MIA and Voice icons in the fakebox.
- (void)addMIAAndVoiceDivider {
  UIView* divider = [self createDivider];
  self.miaAndVoiceDivider = divider;
  [_buttonStack addArrangedSubview:divider];
}

// Handles a lens button with new badge tap. Registers that the tap has occurred
// and animates out the new badge portion of the button.
- (void)lensButtonWithNewBadgeTapped:(id)sender {
  if (!_lensButtonWithNewBadgeTapped) {
    _lensButtonWithNewBadgeTapped = YES;
    [UIView
        animateWithDuration:kMaterialDuration1
                 animations:^{
                   content_suggestions::ConfigureLensButtonWithNewBadgeAlpha(
                       self.lensButton, 0);
                 }];
  }
}

// Returns end button fakebox trailing space depending on fakebox size and
// whether the new badge is displayed.
- (CGFloat)endButtonFakeboxTrailingSpace {
  // If the MIA entry point is shown add a bigger space to the trailing edge to
  // accomodate the animation view.
  if (self.useMIAEnlargedFakebox) {
    return kEndButtonMIAEnlargedFakebox;
  }
  // If normal sized fakebox and new bade is showing, reduce trailing space.
  if (_useNewBadgeForLensButton && !ShouldEnlargeLogoAndFakebox()) {
    return kEndButtonNormalSizeFakeboxWithBadgeTrailingSpace;
  }
  // Common trailing space.
  return kEndButtonFakeboxTrailingSpace;
}

// Updates facets of the UI to reflect the change in the collection of UITraits.
- (void)updateUIOnTraitChange:(UITraitCollection*)previousTraitCollection {
  if (previousTraitCollection.preferredContentSizeCategory !=
      self.traitCollection.preferredContentSizeCategory) {
    [self updateHintLabelFonts];
  }

  if (previousTraitCollection.userInterfaceStyle !=
      self.traitCollection.userInterfaceStyle) {
    // The fakebox background can be a blended color, which will not
    // automatically update when dark/light mode is changed. It needs to be
    // manually updated here.
    [self setFakeboxBackgroundWithProgress:_lastAnimationPercent];

    if (_accountDiscParticleBadgeImageView) {
      _accountDiscParticleBadgeImageView.backgroundColor =
          AccountParticleDiscBadgeBackgroundColor(
              self.traitCollection.userInterfaceStyle);
    }

    if (self.useSingleButtonMIA) {
      [_miaAnimationView removeFromSuperview];
      _miaAnimation = nil;
      [self updateAnimationOnMIAButton];
    }
  }
}

#pragma mark - MIA

- (BOOL)useInlineMIA {
  return self.isGoogleDefaultSearchEngine &&
         GetNTPMIAEntrypointVariation() ==
             NTPMIAEntrypointVariation::kOmniboxContainedInline;
}

- (BOOL)useSingleButtonMIA {
  return self.isGoogleDefaultSearchEngine &&
         ShowOnlyMIAEntrypointInNTPFakebox();
}

- (BOOL)shouldShowMIAEntrypoint {
  return self.useInlineMIA || self.useSingleButtonMIA;
}

// Creates an animation view for the MIA entry point.
- (UIView*)createMIAAnimationView {
  if (!_miaAnimation) {
    _miaAnimation = [self createMIAAnimation];
  }

  UIView* animationView = _miaAnimation.animationView;
  animationView.translatesAutoresizingMaskIntoConstraints = NO;
  animationView.contentMode = UIViewContentModeScaleAspectFit;

  return animationView;
}

// Creates and returns the LottieAnimation for the MIA button.
- (id<LottieAnimation>)createMIAAnimation {
  LottieAnimationConfiguration* config =
      [[LottieAnimationConfiguration alloc] init];
  config.animationName =
      self.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark
          ? kMIACircleAnimationDarkMode
          : kMIACircleAnimationLightMode;
  config.loopAnimationCount = kLottieInfiniteLoopFlag;
  return ios::provider::GenerateLottieAnimation(config);
}

- (void)updateAnimationOnMIAButton {
  if (!self.miaButton) {
    return;
  }
  _miaAnimationView = [self createMIAAnimationView];
  _miaAnimationView.userInteractionEnabled = NO;
  [_miaAnimation play];
  [self.miaButton addSubview:_miaAnimationView];
  AddSameCenterConstraints(_miaAnimationView, self.miaButton);
  AddSizeConstraints(_miaAnimationView, [self miaAnimationSize]);
}

// The size for the animation view dependant on the fakebox size.
- (CGSize)miaAnimationSize {
  if (self.useMIAEnlargedFakebox) {
    return CGSizeMake(kMIACircleAnimationSizeEnlarged,
                      kMIACircleAnimationSizeEnlarged);
  } else {
    return CGSizeMake(kMIACircleAnimationSizeNormal,
                      kMIACircleAnimationSizeNormal);
  }
}

- (BOOL)useMIAEnlargedFakebox {
  return self.isGoogleDefaultSearchEngine && ShouldEnlargeNTPFakeboxForMIA();
}

#pragma mark - helpers

- (CGFloat)hintLabelFakeboxLeadingSpace {
  if (base::FeatureList::IsEnabled(omnibox::kOmniboxMobileParityUpdate)) {
    return kHintLabelFakeboxLeadingSpaceWithIcon;
  } else {
    return kHintLabelFakeboxLeadingSpace;
  }
}

- (CGFloat)hintLabelOmniboxLeadingSpace {
  if (base::FeatureList::IsEnabled(omnibox::kOmniboxMobileParityUpdate)) {
    return kHintLabelOmniboxLeadingSpaceWithIcon;
  } else {
    return kHintLabelOmniboxLeadingSpace;
  }
}

@end
