// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/material_new_tab_page_color_mixer.h"

#include "base/logging.h"
#include "base/metrics/field_trial_params.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/color/chrome_color_provider_utils.h"
#include "components/search/ntp_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"
#include "ui/gfx/color_palette.h"

void AddMaterialNewTabPageColorMixer(ui::ColorProvider* provider,
                                     const ui::ColorProviderKey& key) {
  if (!ShouldApplyChromeMaterialOverrides(key)) {
    return;
  }
  const bool dark_mode =
      key.color_mode == ui::ColorProviderKey::ColorMode::kDark;

  ui::ColorMixer& mixer = provider->AddMixer();
  // When adding a new color ID to this mixer, ensure it is ALSO added
  // to the GM2 color mixer.
  // LINT.IfChange
  mixer[kColorNewTabPageActiveBackground] = {
      ui::kColorSysStateRippleNeutralOnSubtle};
  mixer[kColorNewTabPageAddShortcutBackground] = {ui::kColorSysTonalContainer};
  mixer[kColorNewTabPageAddShortcutForeground] = {
      ui::kColorSysOnTonalContainer};
  mixer[kColorNewTabPageBackground] = {ui::kColorSysBase};
  mixer[kColorNewTabPageBorder] = {ui::kColorSysBaseContainer};
  mixer[kColorNewTabPageButtonBackground] = {ui::kColorSysTonalContainer};
  mixer[kColorNewTabPageButtonBackgroundHovered] = {
      ui::kColorSysStateHoverOnSubtle};
  mixer[kColorNewTabPageButtonForeground] = {ui::kColorSysOnTonalContainer};

  mixer[kColorNewTabPageComposeboxBackground] = {
      dark_mode ? SkColorSetRGB(0x1D, 0x1E, 0x26)
                : SkColorSetRGB(0xF0, 0xF2, 0xF5)};
  mixer[kColorNewTabPageComposeboxFont] = {
      dark_mode ? SkColorSetRGB(0xE6, 0xE8, 0xF0)
                : SkColorSetRGB(0x0A, 0x0A, 0x0A)};
  mixer[kColorNewTabPageComposeboxCancelButton] = {
      dark_mode ? SkColorSetRGB(0xAD, 0xAF, 0xB8)
                : SkColorSetRGB(0x0A, 0x0A, 0x0A)};
  mixer[kColorNewTabPageComposeboxHover] = {
      dark_mode ? SkColorSetRGB(0x25, 0x26, 0x2E)
                : SkColorSetRGB(0xE9, 0xEB, 0xF0)};
  mixer[kColorNewTabPageComposeboxSubmitButton] = {
      SkColorSetRGB(0x0B, 0x50, 0xD0)};

  mixer[kColorNewTabPageControlBackgroundHovered] = {
      ui::kColorSysStateHoverOnSubtle};
  mixer[kColorNewTabPageFocusRing] = {ui::kColorSysStateFocusRing};
  mixer[kColorNewTabPageLink] = {ui::kColorSysPrimary};
  mixer[kColorNewTabPageLogo] = {ui::kColorSysPrimary};

  mixer[kColorNewTabPageMostVisitedTileBackground] = {
      ui::kColorSysSurfaceVariant};
  mixer[kColorNewTabPageMostVisitedForeground] = {ui::kColorSysOnSurfaceSubtle};

  mixer[kColorNewTabPageHistoryClustersModuleItemBackground] = {
      ui::kColorSysBaseContainerElevated};

  mixer[kColorNewTabPageModuleBackground] = {ui::kColorSysBaseContainer};
  mixer[kColorNewTabPageModuleIconBackground] = {ui::kColorSysNeutralContainer};
  // Styling for Doodle Share Button.
  mixer[kColorNewTabPageDoodleShareButtonBackground] = {
      ui::kColorSysNeutralContainer};
  mixer[kColorNewTabPageDoodleShareButtonIcon] = {ui::kColorSysOnSurface};

  mixer[kColorNewTabPageModuleItemBackground] = {
      ui::kColorSysBaseContainerElevated};
  mixer[kColorNewTabPageModuleItemBackgroundHovered] = {
      ui::kColorSysStateHoverBrightBlendProtection};

  mixer[kColorNewTabPageModuleElementDivider] = {ui::kColorSysDivider};
  mixer[kColorNewTabPageModuleContextMenuDivider] = {ui::kColorSysDivider};

  mixer[kColorNewTabPageModuleCalendarEventTimeStatusBackground] = {
      ui::kColorSysNeutralContainer};
  mixer[kColorNewTabPageModuleCalendarAttachmentScrollbarThumb] = {
      ui::kColorSysTonalOutline};
  mixer[kColorNewTabPageModuleCalendarDividerColor] = {ui::kColorSysDivider};

  mixer[kColorNewTabPagePromoBackground] = {ui::kColorSysBase};
  mixer[kColorNewTabPagePrimaryForeground] = {ui::kColorSysOnSurface};
  mixer[kColorNewTabPageSecondaryForeground] = {ui::kColorSysOnSurfaceSubtle};

  mixer[kColorNewTabPageWallpaperSearchButtonBackground] = {
      ui::kColorSysPrimary};
  mixer[kColorNewTabPageWallpaperSearchButtonBackgroundHovered] = {
      kColorNewTabPageButtonBackgroundHovered};
  mixer[kColorNewTabPageWallpaperSearchButtonForeground] = {
      ui::kColorSysOnPrimary};
  if (base::FeatureList::IsEnabled(ntp_features::kRealboxCr23Theming)) {
    // Steady state theme colors.
    mixer[kColorSearchboxBackground] = {kColorToolbarBackgroundSubtleEmphasis};
    mixer[kColorSearchboxBackgroundHovered] = {
        kColorToolbarBackgroundSubtleEmphasisHovered};
    mixer[kColorSearchboxPlaceholder] = {kColorOmniboxTextDimmed};
    mixer[kColorSearchboxSearchIconBackground] = {kColorOmniboxResultsIcon};
    mixer[kColorSearchboxLensVoiceIconBackground] = {ui::kColorSysPrimary};
    mixer[kColorSearchboxSelectionBackground] = {
        kColorOmniboxSelectionBackground};
    mixer[kColorSearchboxSelectionForeground] = {
        kColorOmniboxSelectionForeground};

    // Expanded state theme colors.
    mixer[kColorSearchboxAnswerIconBackground] = {
        kColorOmniboxAnswerIconGM3Background};
    mixer[kColorSearchboxAnswerIconForeground] = {
        kColorOmniboxAnswerIconGM3Foreground};
    mixer[kColorSearchboxForeground] = {kColorOmniboxText};
    mixer[kColorSearchboxResultsActionChip] = {ui::kColorSysTonalOutline};
    mixer[kColorSearchboxResultsActionChipIcon] = {ui::kColorSysPrimary};
    mixer[kColorSearchboxResultsActionChipFocusOutline] = {
        ui::kColorSysStateFocusRing};
    mixer[kColorSearchboxResultsBackgroundHovered] = {
        kColorOmniboxResultsBackgroundHovered};
    mixer[kColorSearchboxResultsButtonHover] = {
        kColorOmniboxResultsButtonInkDropRowHovered};
    mixer[kColorSearchboxResultsDimSelected] = {
        kColorOmniboxResultsTextDimmedSelected};
    mixer[kColorSearchboxResultsFocusIndicator] = {
        kColorOmniboxResultsFocusIndicator};
    mixer[kColorSearchboxResultsForeground] = {kColorOmniboxText};
    mixer[kColorSearchboxResultsForegroundDimmed] = {kColorOmniboxTextDimmed};
    mixer[kColorSearchboxResultsIcon] = {kColorOmniboxResultsIcon};
    mixer[kColorSearchboxResultsIconSelected] = {kColorOmniboxResultsIcon};
    mixer[kColorSearchboxResultsIconFocusedOutline] = {
        kColorOmniboxResultsButtonIconSelected};
    mixer[kColorSearchboxResultsUrl] = {kColorOmniboxResultsUrl};
    mixer[kColorSearchboxResultsUrlSelected] = {
        kColorOmniboxResultsUrlSelected};
    mixer[kColorSearchboxShadow] =
        ui::SetAlpha(gfx::kGoogleGrey900,
                     (dark_mode ? /* % opacity */ 0.32 : 0.1) * SK_AlphaOPAQUE);

    // This determines weather the realbox expanded state background in dark
    // mode will match the omnibox or not.
    if (dark_mode &&
        !ntp_features::kNtpRealboxCr23ExpandedStateBgMatchesOmnibox.Get()) {
      mixer[kColorSearchboxResultsBackground] = {
          kColorToolbarBackgroundSubtleEmphasis};
    } else {
      mixer[kColorSearchboxResultsBackground] = {
          kColorOmniboxResultsBackground};
    }
  }

  /* NewTabFooter */
  mixer[kColorNewTabFooterBackground] = {ui::kColorSysSurface2};
  mixer[kColorNewTabFooterText] = {ui::kColorSysOnSurface};
  mixer[kColorNewTabFooterLogoBackground] = {ui::kColorSysSurface};
  // LINT.ThenChange(//chrome/browser/ui/color/new_tab_page_color_mixer.cc)
}
