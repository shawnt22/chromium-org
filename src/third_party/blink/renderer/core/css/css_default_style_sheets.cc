/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "third_party/blink/renderer/core/css/css_default_style_sheets.h"

#include "third_party/blink/public/resources/grit/blink_resources.h"
#include "third_party/blink/renderer/core/css/media_query_evaluator.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/rule_set.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/html_anchor_element.h"
#include "third_party/blink/renderer/core/html/html_html_element.h"
#include "third_party/blink/renderer/core/html/html_image_element.h"
#include "third_party/blink/renderer/core/html/html_permission_element.h"
#include "third_party/blink/renderer/core/html/media/html_audio_element.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/mathml_names.h"
#include "third_party/blink/renderer/platform/data_resource_helper.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/leak_annotations.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace {
String MaybeRemoveCSSImportant(String string) {
  const StringView kImportantSuffix(" !important");
  return string.EndsWith(kImportantSuffix)
             ? string.Substring(0, string.length() - kImportantSuffix.length())
             : string;
}
}  // namespace

namespace blink {

CSSDefaultStyleSheets& CSSDefaultStyleSheets::Instance() {
  DEFINE_STATIC_LOCAL(Persistent<CSSDefaultStyleSheets>,
                      css_default_style_sheets,
                      (MakeGarbageCollected<CSSDefaultStyleSheets>()));
  return *css_default_style_sheets;
}

static const MediaQueryEvaluator& PrintEval() {
  DEFINE_STATIC_LOCAL(const Persistent<MediaQueryEvaluator>, static_print_eval,
                      (MakeGarbageCollected<MediaQueryEvaluator>("print")));
  return *static_print_eval;
}

static const MediaQueryEvaluator& ForcedColorsEval() {
  // We use "ua-forced-colors" here instead of "forced-colors" to indicate that
  // this is a UA hack for the "forced-colors" media query.
  DEFINE_STATIC_LOCAL(
      Persistent<MediaQueryEvaluator>, forced_colors_eval,
      (MakeGarbageCollected<MediaQueryEvaluator>("ua-forced-colors")));
  return *forced_colors_eval;
}

// static
void CSSDefaultStyleSheets::Init() {
  Instance();
}

// static
StyleSheetContents* CSSDefaultStyleSheets::ParseUASheet(const String& str) {
  // UA stylesheets always parse in the insecure context mode.
  auto* sheet = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(
          kUASheetMode, SecureContextMode::kInsecureContext));
  sheet->ParseString(str);
  // User Agent stylesheets are parsed once for the lifetime of the renderer
  // process and are intentionally leaked.
  LEAK_SANITIZER_IGNORE_OBJECT(sheet);
  return sheet;
}

// static
const MediaQueryEvaluator& CSSDefaultStyleSheets::ScreenEval() {
  DEFINE_STATIC_LOCAL(const Persistent<MediaQueryEvaluator>, static_screen_eval,
                      (MakeGarbageCollected<MediaQueryEvaluator>("screen")));
  return *static_screen_eval;
}

CSSDefaultStyleSheets::CSSDefaultStyleSheets()
    : media_controls_style_sheet_loader_(nullptr) {
  // Strict-mode rules.
  String default_rules =
      StrCat({UncompressResourceAsASCIIString(IDR_UASTYLE_HTML_CSS),
              LayoutTheme::GetTheme().ExtraDefaultStyleSheet()});

  default_style_sheet_ = ParseUASheet(default_rules);

  // Quirks-mode rules.
  String quirks_rules = UncompressResourceAsASCIIString(IDR_UASTYLE_QUIRKS_CSS);
  quirks_style_sheet_ = ParseUASheet(quirks_rules);

  InitializeDefaultStyles();
}

void CSSDefaultStyleSheets::PrepareForLeakDetection() {
  Reset();
}

void CSSDefaultStyleSheets::Reset() {
  // Clear the optional style sheets.
  svg_style_sheet_.Clear();
  mathml_style_sheet_.Clear();
  media_controls_style_sheet_.Clear();
  text_track_style_sheet_.Clear();
  forced_colors_style_sheet_.Clear();
  fullscreen_style_sheet_.Clear();
  marker_style_sheet_.Clear();
  scroll_button_style_sheet_.Clear();
  scroll_marker_style_sheet_.Clear();
  permission_element_style_sheet_.Clear();
  view_source_style_sheet_.Clear();
  json_style_sheet_.Clear();
  // Recreate the default style sheet to clean up possible SVG resources.
  String default_rules =
      StrCat({UncompressResourceAsASCIIString(IDR_UASTYLE_HTML_CSS),
              LayoutTheme::GetTheme().ExtraDefaultStyleSheet()});
  default_style_sheet_ = ParseUASheet(default_rules);

  // Initialize the styles that have the lazily loaded style sheets.
  InitializeDefaultStyles();
  default_view_source_style_.Clear();
  rule_set_group_cache_.clear();
}

void CSSDefaultStyleSheets::VerifyUniversalRuleCount() {
#if EXPENSIVE_DCHECKS_ARE_ON()
  // Universal bucket rules need to be checked against every single element,
  // thus we want avoid them in UA stylesheets.
  default_html_style_->CompactRulesIfNeeded();
  DCHECK(default_html_style_->UniversalRules().empty());
  default_html_quirks_style_->CompactRulesIfNeeded();
  DCHECK(default_html_quirks_style_->UniversalRules().empty());

  // The RuleSets below currently contain universal bucket rules.
  // Ideally these should also be empty, DCHECK the current size to only
  // consciously add more universal bucket rules.
  if (mathml_style_sheet_) {
    default_mathml_style_->CompactRulesIfNeeded();
    DCHECK_EQ(default_mathml_style_->UniversalRules().size(), 24u);
  }

  if (svg_style_sheet_) {
    default_svg_style_->CompactRulesIfNeeded();
    DCHECK_EQ(default_svg_style_->UniversalRules().size(), 1u);
  }

  if (media_controls_style_sheet_) {
    default_media_controls_style_->CompactRulesIfNeeded();
    DCHECK_EQ(default_media_controls_style_->UniversalRules().size(), 5u);
  }

  if (fullscreen_style_sheet_) {
    default_fullscreen_style_->CompactRulesIfNeeded();
    // There are 7 rules by default but if the viewport segments MQs are
    // resolved then we have an additional rule.
    DCHECK(default_fullscreen_style_->UniversalRules().size() == 7u ||
           default_fullscreen_style_->UniversalRules().size() == 8u);
  }

  if (marker_style_sheet_ || scroll_button_style_sheet_ ||
      scroll_marker_style_sheet_) {
    default_pseudo_element_style_->CompactRulesIfNeeded();
    size_t expected_rule_count = 0u;
    if (marker_style_sheet_) {
      expected_rule_count += 3u;
    }
    if (scroll_button_style_sheet_) {
      expected_rule_count += 5u;
    }
    if (scroll_marker_style_sheet_) {
      expected_rule_count += 6u;
    }
    DCHECK_EQ(default_pseudo_element_style_->UniversalRules().size(),
              expected_rule_count);
  }

#endif
}

void CSSDefaultStyleSheets::InitializeDefaultStyles() {
  // This must be called only from constructor / PrepareForLeakDetection.
  default_html_style_ = MakeGarbageCollected<RuleSet>();
  default_mathml_style_ = MakeGarbageCollected<RuleSet>();
  default_svg_style_ = MakeGarbageCollected<RuleSet>();
  default_html_quirks_style_ = MakeGarbageCollected<RuleSet>();
  default_print_style_ = MakeGarbageCollected<RuleSet>();
  default_media_controls_style_ = MakeGarbageCollected<RuleSet>();
  default_fullscreen_style_ = MakeGarbageCollected<RuleSet>();
  default_forced_color_style_.Clear();
  default_pseudo_element_style_.Clear();
  default_forced_colors_media_controls_style_.Clear();

  default_html_style_->AddRulesFromSheet(DefaultStyleSheet(), ScreenEval());
  default_html_quirks_style_->AddRulesFromSheet(QuirksStyleSheet(),
                                                ScreenEval());
  default_print_style_->AddRulesFromSheet(DefaultStyleSheet(), PrintEval());

  default_html_style_->CompactRulesIfNeeded();
  default_html_quirks_style_->CompactRulesIfNeeded();
  default_print_style_->CompactRulesIfNeeded();

  CHECK(default_html_style_->ViewTransitionRules().empty())
      << "@view-transition is not implemented for the UA stylesheet.";

  VerifyUniversalRuleCount();
}

RuleSet* CSSDefaultStyleSheets::DefaultViewSourceStyle() {
  if (!default_view_source_style_) {
    default_view_source_style_ = MakeGarbageCollected<RuleSet>();
    view_source_style_sheet_ = ParseUASheet(
        UncompressResourceAsASCIIString(IDR_UASTYLE_VIEW_SOURCE_CSS));
    default_view_source_style_->AddRulesFromSheet(view_source_style_sheet_,
                                                  ScreenEval());
    default_view_source_style_->CompactRulesIfNeeded();
  }
  return default_view_source_style_.Get();
}

RuleSet* CSSDefaultStyleSheets::DefaultJSONDocumentStyle() {
  if (!default_json_document_style_) {
    json_style_sheet_ = ParseUASheet(
        UncompressResourceAsASCIIString(IDR_UASTYLE_JSON_DOCUMENT_CSS));
    default_json_document_style_ = MakeGarbageCollected<RuleSet>();
    default_json_document_style_->AddRulesFromSheet(json_style_sheet_,
                                                    ScreenEval());
    default_json_document_style_->CompactRulesIfNeeded();
  }
  return default_json_document_style_.Get();
}

static void AddTextTrackCSSProperties(StringBuilder* builder,
                                      CSSPropertyID propertyId,
                                      String value) {
  builder->Append(CSSProperty::Get(propertyId).GetPropertyNameString());
  builder->Append(": ");
  builder->Append(value);
  builder->Append("; ");
}

void CSSDefaultStyleSheets::AddRulesToDefaultStyleSheets(
    StyleSheetContents* rules,
    NamespaceType type) {
  switch (type) {
    case NamespaceType::kHTML:
      default_html_style_->AddRulesFromSheet(rules, ScreenEval());
      default_html_style_->CompactRulesIfNeeded();
      break;
    case NamespaceType::kSVG:
      default_svg_style_->AddRulesFromSheet(rules, ScreenEval());
      default_svg_style_->CompactRulesIfNeeded();
      break;
    case NamespaceType::kMathML:
      default_mathml_style_->AddRulesFromSheet(rules, ScreenEval());
      default_mathml_style_->CompactRulesIfNeeded();
      break;
    case NamespaceType::kMediaControls:
      default_media_controls_style_->AddRulesFromSheet(rules, ScreenEval());
      default_media_controls_style_->CompactRulesIfNeeded();
      break;
  }
  // Add to print and forced color for all namespaces.
  default_print_style_->AddRulesFromSheet(rules, PrintEval());
  default_print_style_->CompactRulesIfNeeded();
  if (default_forced_color_style_) {
    switch (type) {
      case NamespaceType::kMediaControls:
        if (!default_forced_colors_media_controls_style_) {
          default_forced_colors_media_controls_style_ =
              MakeGarbageCollected<RuleSet>();
        }
        default_forced_colors_media_controls_style_->AddRulesFromSheet(
            rules, ForcedColorsEval());
        default_forced_colors_media_controls_style_->CompactRulesIfNeeded();
        break;
      default:
        default_forced_color_style_->AddRulesFromSheet(rules,
                                                       ForcedColorsEval());
        default_forced_color_style_->CompactRulesIfNeeded();
        break;
    }
  }
  VerifyUniversalRuleCount();
}

bool CSSDefaultStyleSheets::EnsureDefaultStyleSheetsForElement(
    const Element& element) {
  bool changed_default_style = false;
  // FIXME: We should assert that the sheet only styles SVG elements.
  if (element.IsSVGElement() && !svg_style_sheet_) {
    svg_style_sheet_ =
        ParseUASheet(UncompressResourceAsASCIIString(IDR_UASTYLE_SVG_CSS));
    AddRulesToDefaultStyleSheets(svg_style_sheet_, NamespaceType::kSVG);
    changed_default_style = true;
  }

  // FIXME: We should assert that the sheet only styles MathML elements.
  if (element.IsMathMLElement() && !mathml_style_sheet_) {
    mathml_style_sheet_ =
        ParseUASheet(UncompressResourceAsASCIIString(IDR_UASTYLE_MATHML_CSS));
    AddRulesToDefaultStyleSheets(mathml_style_sheet_, NamespaceType::kMathML);
    changed_default_style = true;
  }

  if (!media_controls_style_sheet_ && HasMediaControlsStyleSheetLoader() &&
      (IsA<HTMLVideoElement>(element) || IsA<HTMLAudioElement>(element))) {
    // FIXME: We should assert that this sheet only contains rules for <video>
    // and <audio>.
    media_controls_style_sheet_ =
        ParseUASheet(media_controls_style_sheet_loader_->GetUAStyleSheet());
    AddRulesToDefaultStyleSheets(media_controls_style_sheet_,
                                 NamespaceType::kMediaControls);
    changed_default_style = true;
  }

  if (!permission_element_style_sheet_ && IsA<HTMLPermissionElement>(element)) {
    CHECK(RuntimeEnabledFeatures::PermissionElementEnabled(
        element.GetExecutionContext()));
    permission_element_style_sheet_ = ParseUASheet(
        UncompressResourceAsASCIIString(IDR_UASTYLE_PERMISSION_ELEMENT_CSS));
    AddRulesToDefaultStyleSheets(permission_element_style_sheet_,
                                 NamespaceType::kHTML);
    changed_default_style = true;
  }

  if (!text_track_style_sheet_ && IsA<HTMLVideoElement>(element)) {
    Settings* settings = element.GetDocument().GetSettings();
    if (settings) {
      // Rules below override rules from html.css and other UA sheets regardless
      // of specificity. See comment in StyleResolver::MatchUARules().
      StringBuilder builder;
      Color color;
      // Use the text track window color if it is set and non-transparent,
      // otherwise use the background color. This is only applicable to caption
      // settings on MacOS, which allows users to specify a window color in
      // addition to a background color. The WebVTT spec does not have a concept
      // of a window background, so this workaround allows the default caption
      // styles on MacOS to render as expected.
      builder.Append("video::cue { ");
      if (CSSParser::ParseColor(
              color,
              MaybeRemoveCSSImportant(settings->GetTextTrackWindowColor())) &&
          color.Alpha() > 0) {
        AddTextTrackCSSProperties(&builder, CSSPropertyID::kBackgroundColor,
                                  settings->GetTextTrackWindowColor());
        AddTextTrackCSSProperties(&builder, CSSPropertyID::kBorderRadius,
                                  settings->GetTextTrackWindowRadius());
      } else {
        AddTextTrackCSSProperties(&builder, CSSPropertyID::kBackgroundColor,
                                  settings->GetTextTrackBackgroundColor());
      }
      AddTextTrackCSSProperties(&builder, CSSPropertyID::kFontFamily,
                                settings->GetTextTrackFontFamily());
      AddTextTrackCSSProperties(&builder, CSSPropertyID::kFontStyle,
                                settings->GetTextTrackFontStyle());
      AddTextTrackCSSProperties(&builder, CSSPropertyID::kFontVariant,
                                settings->GetTextTrackFontVariant());
      AddTextTrackCSSProperties(&builder, CSSPropertyID::kColor,
                                settings->GetTextTrackTextColor());
      AddTextTrackCSSProperties(&builder, CSSPropertyID::kTextShadow,
                                settings->GetTextTrackTextShadow());
      AddTextTrackCSSProperties(&builder, CSSPropertyID::kFontSize,
                                settings->GetTextTrackTextSize());
      builder.Append(" } ");
      text_track_style_sheet_ = ParseUASheet(builder.ReleaseString());
      AddRulesToDefaultStyleSheets(text_track_style_sheet_,
                                   NamespaceType::kMediaControls);
      changed_default_style = true;
    }
  }

  if (changed_default_style) {
    rule_set_group_cache_.clear();
  }

  DCHECK(!default_html_style_->Features()
              .GetRuleInvalidationData()
              .HasIdsInSelectors());
  return changed_default_style;
}

bool CSSDefaultStyleSheets::EnsureDefaultStyleSheetsForPseudoElement(
    PseudoId pseudo_id) {
  switch (pseudo_id) {
    case kPseudoIdScrollButtonBlockStart:
    case kPseudoIdScrollButtonInlineStart:
    case kPseudoIdScrollButtonInlineEnd:
    case kPseudoIdScrollButtonBlockEnd: {
      if (scroll_button_style_sheet_) {
        return false;
      }
      scroll_button_style_sheet_ = ParseUASheet(
          UncompressResourceAsASCIIString(IDR_UASTYLE_SCROLL_BUTTON_CSS));
      if (!default_pseudo_element_style_) {
        default_pseudo_element_style_ = MakeGarbageCollected<RuleSet>();
      }
      default_pseudo_element_style_->AddRulesFromSheet(ScrollButtonStyleSheet(),
                                                       ScreenEval());
      default_pseudo_element_style_->CompactRulesIfNeeded();
      // We just added a new :focus-visible rule to the UA stylesheet, and
      // RuleSetGroup caches whether we have any such rules or not, so we need
      // to clear the cache.
      rule_set_group_cache_.clear();
      return true;
    }
    case kPseudoIdScrollMarker: {
      if (scroll_marker_style_sheet_) {
        return false;
      }
      scroll_marker_style_sheet_ = ParseUASheet(
          UncompressResourceAsASCIIString(IDR_UASTYLE_SCROLL_MARKER_CSS));
      if (!default_pseudo_element_style_) {
        default_pseudo_element_style_ = MakeGarbageCollected<RuleSet>();
      }
      default_pseudo_element_style_->AddRulesFromSheet(ScrollMarkerStyleSheet(),
                                                       ScreenEval());
      default_pseudo_element_style_->CompactRulesIfNeeded();
      // We just added a new :focus-visible rule to the UA stylesheet, and
      // RuleSetGroup caches whether we have any such rules or not, so we need
      // to clear the cache.
      rule_set_group_cache_.clear();
      return true;
    }
    case kPseudoIdMarker: {
      if (marker_style_sheet_) {
        return false;
      }
      marker_style_sheet_ =
          ParseUASheet(UncompressResourceAsASCIIString(IDR_UASTYLE_MARKER_CSS));
      if (!default_pseudo_element_style_) {
        default_pseudo_element_style_ = MakeGarbageCollected<RuleSet>();
      }
      default_pseudo_element_style_->AddRulesFromSheet(MarkerStyleSheet(),
                                                       ScreenEval());
      default_pseudo_element_style_->CompactRulesIfNeeded();
      return true;
    }
    default:
      return false;
  }
}

void CSSDefaultStyleSheets::SetMediaControlsStyleSheetLoader(
    std::unique_ptr<UAStyleSheetLoader> loader) {
  media_controls_style_sheet_loader_.swap(loader);
}

void CSSDefaultStyleSheets::EnsureDefaultStyleSheetForFullscreen(
    const Element& element) {
  if (fullscreen_style_sheet_) {
    DCHECK(!default_fullscreen_style_->DidMediaQueryResultsChange(
        MediaQueryEvaluator(element.GetDocument().GetFrame())));
    return;
  }

  String fullscreen_rules =
      StrCat({UncompressResourceAsASCIIString(IDR_UASTYLE_FULLSCREEN_CSS),
              LayoutTheme::GetTheme().ExtraFullscreenStyleSheet()});
  fullscreen_style_sheet_ = ParseUASheet(fullscreen_rules);

  default_fullscreen_style_->AddRulesFromSheet(
      fullscreen_style_sheet_,
      MediaQueryEvaluator(element.GetDocument().GetFrame()));
  default_fullscreen_style_->CompactRulesIfNeeded();
  VerifyUniversalRuleCount();
}

void CSSDefaultStyleSheets::RebuildFullscreenRuleSetIfMediaQueriesChanged(
    const Element& element) {
  if (!fullscreen_style_sheet_) {
    return;
  }

  if (!default_fullscreen_style_->DidMediaQueryResultsChange(
          MediaQueryEvaluator(element.GetDocument().GetFrame()))) {
    return;
  }

  default_fullscreen_style_ = MakeGarbageCollected<RuleSet>();
  default_fullscreen_style_->AddRulesFromSheet(
      fullscreen_style_sheet_,
      MediaQueryEvaluator(element.GetDocument().GetFrame()));
  default_fullscreen_style_->CompactRulesIfNeeded();
  VerifyUniversalRuleCount();
  rule_set_group_cache_.clear();
}

bool CSSDefaultStyleSheets::EnsureDefaultStyleSheetForForcedColors() {
  if (forced_colors_style_sheet_) {
    return false;
  }

  String forced_colors_rules = String();
  if (RuntimeEnabledFeatures::ForcedColorsEnabled()) {
    forced_colors_rules = StrCat(
        {forced_colors_rules,
         UncompressResourceAsASCIIString(IDR_UASTYLE_THEME_FORCED_COLORS_CSS)});
  }
  forced_colors_style_sheet_ = ParseUASheet(forced_colors_rules);

  if (!default_forced_color_style_) {
    default_forced_color_style_ = MakeGarbageCollected<RuleSet>();
  }
  default_forced_color_style_->AddRulesFromSheet(DefaultStyleSheet(),
                                                 ForcedColorsEval());
  default_forced_color_style_->AddRulesFromSheet(ForcedColorsStyleSheet(),
                                                 ForcedColorsEval());
  if (svg_style_sheet_) {
    default_forced_color_style_->AddRulesFromSheet(SvgStyleSheet(),
                                                   ForcedColorsEval());
  }
  default_forced_color_style_->CompactRulesIfNeeded();

  if (media_controls_style_sheet_) {
    CHECK(!default_forced_colors_media_controls_style_);
    default_forced_colors_media_controls_style_ =
        MakeGarbageCollected<RuleSet>();
    default_forced_colors_media_controls_style_->AddRulesFromSheet(
        MediaControlsStyleSheet(), ForcedColorsEval());
    default_forced_colors_media_controls_style_->CompactRulesIfNeeded();
  }

  return true;
}

void CSSDefaultStyleSheets::CollectFeaturesTo(const Document& document,
                                              RuleFeatureSet& features) {
  ForEachRuleFeatureSet(document, /*call_for_each_stylesheet=*/false,
                        WTF::BindRepeating(
                            [](RuleFeatureSet& target_features,
                               const RuleFeatureSet& default_style_features,
                               StyleSheetContents* contents) {
                              target_features.Merge(default_style_features);
                            },
                            std::ref(features)));
}

void CSSDefaultStyleSheets::ForEachRuleFeatureSet(
    const Document& document,
    bool call_for_each_stylesheet,
    base::RepeatingCallback<void(const RuleFeatureSet&, StyleSheetContents*)>
        func) {
  if (DefaultHtmlStyle()) {
    const RuleFeatureSet& features = DefaultHtmlStyle()->Features();
    func.Run(features, default_style_sheet_);
    if (call_for_each_stylesheet && permission_element_style_sheet_) {
      func.Run(features, permission_element_style_sheet_);
    }
  }
  if (DefaultMediaControlsStyle()) {
    const RuleFeatureSet& features = DefaultMediaControlsStyle()->Features();
    func.Run(features, media_controls_style_sheet_);
    if (call_for_each_stylesheet && text_track_style_sheet_) {
      func.Run(features, text_track_style_sheet_);
    }
  }
  if (DefaultMathMLStyle()) {
    const RuleFeatureSet& features = DefaultMathMLStyle()->Features();
    func.Run(features, mathml_style_sheet_);
  }
  if (DefaultFullscreenStyle()) {
    const RuleFeatureSet& features = DefaultFullscreenStyle()->Features();
    func.Run(features, fullscreen_style_sheet_);
  }
  if (document.IsViewSource() && DefaultViewSourceStyle()) {
    const RuleFeatureSet& features = DefaultViewSourceStyle()->Features();
    func.Run(features, view_source_style_sheet_);
  }
  if (document.IsJSONDocument() && DefaultJSONDocumentStyle()) {
    const RuleFeatureSet& features = DefaultJSONDocumentStyle()->Features();
    func.Run(features, json_style_sheet_);
  }
}

void CSSDefaultStyleSheets::Trace(Visitor* visitor) const {
  visitor->Trace(default_html_style_);
  visitor->Trace(default_mathml_style_);
  visitor->Trace(default_svg_style_);
  visitor->Trace(default_html_quirks_style_);
  visitor->Trace(default_print_style_);
  visitor->Trace(default_view_source_style_);
  visitor->Trace(default_forced_color_style_);
  visitor->Trace(default_pseudo_element_style_);
  visitor->Trace(default_media_controls_style_);
  visitor->Trace(default_fullscreen_style_);
  visitor->Trace(default_json_document_style_);
  visitor->Trace(default_forced_colors_media_controls_style_);

  visitor->Trace(default_style_sheet_);
  visitor->Trace(quirks_style_sheet_);
  visitor->Trace(svg_style_sheet_);
  visitor->Trace(mathml_style_sheet_);
  visitor->Trace(media_controls_style_sheet_);
  visitor->Trace(permission_element_style_sheet_);
  visitor->Trace(text_track_style_sheet_);
  visitor->Trace(forced_colors_style_sheet_);
  visitor->Trace(fullscreen_style_sheet_);
  visitor->Trace(marker_style_sheet_);
  visitor->Trace(scroll_button_style_sheet_);
  visitor->Trace(scroll_marker_style_sheet_);
  visitor->Trace(view_source_style_sheet_);
  visitor->Trace(json_style_sheet_);

  visitor->Trace(rule_set_group_cache_);
}

CSSDefaultStyleSheets::TestingScope::TestingScope() = default;
CSSDefaultStyleSheets::TestingScope::~TestingScope() {
  Instance().Reset();
}

}  // namespace blink
