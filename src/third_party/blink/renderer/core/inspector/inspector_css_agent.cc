/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "third_party/blink/renderer/core/inspector/inspector_css_agent.h"

#include <optional>
#include <utility>

#include "third_party/blink/public/common/metrics/document_update_reason.h"
#include "third_party/blink/renderer/core/animation/animation_utils.h"
#include "third_party/blink/renderer/core/animation/css/css_animation.h"
#include "third_party/blink/renderer/core/animation/css/css_animation_data.h"
#include "third_party/blink/renderer/core/animation/css/css_transition.h"
#include "third_party/blink/renderer/core/animation/element_animations.h"
#include "third_party/blink/renderer/core/css/auto_registration.h"
#include "third_party/blink/renderer/core/css/cascade_layer.h"
#include "third_party/blink/renderer/core/css/cascade_layer_map.h"
#include "third_party/blink/renderer/core/css/check_pseudo_has_cache_scope.h"
#include "third_party/blink/renderer/core/css/css_color.h"
#include "third_party/blink/renderer/core/css/css_computed_style_declaration.h"
#include "third_party/blink/renderer/core/css/css_container_rule.h"
#include "third_party/blink/renderer/core/css/css_default_style_sheets.h"
#include "third_party/blink/renderer/core/css/css_font_face.h"
#include "third_party/blink/renderer/core/css/css_font_face_source.h"
#include "third_party/blink/renderer/core/css/css_font_palette_values_rule.h"
#include "third_party/blink/renderer/core/css/css_font_selector.h"
#include "third_party/blink/renderer/core/css/css_function_declarations_rule.h"
#include "third_party/blink/renderer/core/css/css_function_descriptors.h"
#include "third_party/blink/renderer/core/css/css_function_rule.h"
#include "third_party/blink/renderer/core/css/css_gradient_value.h"
#include "third_party/blink/renderer/core/css/css_import_rule.h"
#include "third_party/blink/renderer/core/css/css_keyframe_rule.h"
#include "third_party/blink/renderer/core/css/css_keyframes_rule.h"
#include "third_party/blink/renderer/core/css/css_layer_block_rule.h"
#include "third_party/blink/renderer/core/css/css_layer_statement_rule.h"
#include "third_party/blink/renderer/core/css/css_media_rule.h"
#include "third_party/blink/renderer/core/css/css_pending_substitution_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_property_name.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_property_rule.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_rule.h"
#include "third_party/blink/renderer/core/css/css_rule_list.h"
#include "third_party/blink/renderer/core/css/css_scope_rule.h"
#include "third_party/blink/renderer/core/css/css_starting_style_rule.h"
#include "third_party/blink/renderer/core/css/css_style_rule.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/css_supports_rule.h"
#include "third_party/blink/renderer/core/css/css_unparsed_declaration_value.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/css_variable_data.h"
#include "third_party/blink/renderer/core/css/font_face.h"
#include "third_party/blink/renderer/core/css/font_size_functions.h"
#include "third_party/blink/renderer/core/css/media_list.h"
#include "third_party/blink/renderer/core/css/media_query.h"
#include "third_party/blink/renderer/core/css/media_values.h"
#include "third_party/blink/renderer/core/css/out_of_flow_data.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_local_context.h"
#include "third_party/blink/renderer/core/css/parser/css_variable_parser.h"
#include "third_party/blink/renderer/core/css/properties/computed_style_utils.h"
#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/properties/css_property_ref.h"
#include "third_party/blink/renderer/core/css/properties/longhands/custom_property.h"
#include "third_party/blink/renderer/core/css/properties/shorthand.h"
#include "third_party/blink/renderer/core/css/property_registry.h"
#include "third_party/blink/renderer/core/css/resolver/scoped_style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_rule_usage_tracker.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/style_rule.h"
#include "third_party/blink/renderer/core/css/style_rule_font_palette_values.h"
#include "third_party/blink/renderer/core/css/style_rule_function_declarations.h"
#include "third_party/blink/renderer/core/css/style_sheet.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/css/style_sheet_list.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/dom/dom_node_ids.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/slot_assignment_engine.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/dom/tree_scope.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/html_head_element.h"
#include "third_party/blink/renderer/core/inspector/identifiers_factory.h"
#include "third_party/blink/renderer/core/inspector/inspected_frames.h"
#include "third_party/blink/renderer/core/inspector/inspector_contrast.h"
#include "third_party/blink/renderer/core/inspector/inspector_ghost_rules.h"
#include "third_party/blink/renderer/core/inspector/inspector_history.h"
#include "third_party/blink/renderer/core/inspector/inspector_network_agent.h"
#include "third_party/blink/renderer/core/inspector/inspector_resource_container.h"
#include "third_party/blink/renderer/core/inspector/inspector_resource_content_loader.h"
#include "third_party/blink/renderer/core/inspector/inspector_style_resolver.h"
#include "third_party/blink/renderer/core/inspector/inspector_style_sheet.h"
#include "third_party/blink/renderer/core/inspector/protocol/css.h"
#include "third_party/blink/renderer/core/layout/constraint_space.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_object_inlines.h"
#include "third_party/blink/renderer/core/layout/layout_result.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/core/style/scoped_css_name.h"
#include "third_party/blink/renderer/core/style/style_generated_image.h"
#include "third_party/blink/renderer/core/style/style_image.h"
#include "third_party/blink/renderer/core/style_property_shorthand.h"
#include "third_party/blink/renderer/core/svg/svg_element.h"
#include "third_party/blink/renderer/core/view_transition/view_transition.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_custom_platform_data.h"
#include "third_party/blink/renderer/platform/fonts/shaping/caching_word_shaper.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_view.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"
#include "third_party/blink/renderer/platform/heap/collection_support/clear_collection_scope.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/text/text_run.h"
#include "third_party/blink/renderer/platform/text/writing_direction_mode.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/strcat.h"
#include "third_party/blink/renderer/platform/wtf/text/string_concatenate.h"

namespace blink {

namespace {

int g_frontend_operation_counter = 0;

class FrontendOperationScope {
  STACK_ALLOCATED();

 public:
  FrontendOperationScope() { ++g_frontend_operation_counter; }
  ~FrontendOperationScope() { --g_frontend_operation_counter; }
};

Element* GetPseudoIdAndTag(Element* element,
                           PseudoElement*& pseudo_element,
                           PseudoId& element_pseudo_id,
                           AtomicString& view_transition_name) {
  auto* resolved_element = element;
  auto* try_pseudo = DynamicTo<PseudoElement>(element);
  bool is_transition =
      try_pseudo && IsTransitionPseudoElement(try_pseudo->GetPseudoId());
  // If nested pseudo-element support is turned on, it is better not to do this
  // translation, as it is lossy. We can query styles directly from the pseudo-
  // element node instead of using the originating element and pseudo id.
  // TODO(crbug.com/373478544): Remove this function once this flag is no longer
  // necessary.
  if (RuntimeEnabledFeatures::CSSNestedPseudoElementsEnabled()) {
    // View transition pseudo-elements depend on the old logic; always translate
    // them.
    if (!is_transition) {
      return resolved_element;
    }
  }
  if (try_pseudo) {
    pseudo_element = try_pseudo;
    resolved_element = is_transition
                           ? &pseudo_element->UltimateOriginatingElement()
                           : pseudo_element->ParentOrShadowHostElement();
    // TODO(khushalsagar) : This should never be null.
    if (!resolved_element)
      return nullptr;

    element_pseudo_id = pseudo_element->GetPseudoIdForStyling();
    view_transition_name = pseudo_element->view_transition_name();
  }
  return resolved_element;
}

String CreateShorthandValue(Document& document,
                            const String& shorthand,
                            const String& old_text,
                            const String& longhand,
                            const String& new_value) {
  auto* style_sheet_contents =
      MakeGarbageCollected<StyleSheetContents>(StrictCSSParserContext(
          document.GetExecutionContext()->GetSecureContextMode()));
  String text = StrCat({" div { ", shorthand, ": ", old_text, "; }"});
  CSSParser::ParseSheet(MakeGarbageCollected<CSSParserContext>(document),
                        style_sheet_contents, text);

  auto* style_sheet = MakeGarbageCollected<CSSStyleSheet>(style_sheet_contents);
  auto* rule = To<CSSStyleRule>(style_sheet->ItemInternal(0));
  CSSStyleDeclaration* style = rule->style();
  DummyExceptionStateForTesting exception_state;
  style->setProperty(document.GetExecutionContext(), longhand, new_value,
                     style->getPropertyPriority(longhand), exception_state);
  return style->getPropertyValue(shorthand);
}

HeapVector<Member<CSSStyleRule>> FilterDuplicateRules(
    RuleIndexList* rule_list) {
  HeapVector<Member<CSSStyleRule>> uniq_rules;
  HeapHashSet<Member<CSSRule>> uniq_rules_set;
  for (unsigned i = rule_list ? rule_list->size() : 0; i > 0; --i) {
    CSSRule* rule = rule_list->at(i - 1).rule.Get();
    auto* style_rule = DynamicTo<CSSStyleRule>(rule);
    if (!style_rule || uniq_rules_set.Contains(rule))
      continue;
    uniq_rules_set.insert(rule);
    uniq_rules.push_back(style_rule);
  }
  uniq_rules.Reverse();
  return uniq_rules;
}

void CollectPlatformFontsFromRunFontDataList(
    const HeapVector<ShapeResult::RunFontData>& run_font_data_list,
    HashMap<std::pair<int, String>, std::pair<int, String>>* font_stats) {
  for (const auto& run_font_data : run_font_data_list) {
    const auto* simple_font_data = run_font_data.font_data_.Get();
    String family_name = simple_font_data->PlatformData().FontFamilyName();
    if (family_name.IsNull())
      family_name = "";
    String postscript_name =
        simple_font_data->PlatformData().GetPostScriptName();
    if (postscript_name.IsNull()) {
      postscript_name = "";
    }
    auto font_key = std::make_pair(simple_font_data->IsCustomFont() ? 1 : 0,
                                   postscript_name);
    auto font_stats_it = font_stats->find(font_key);
    if (font_stats_it == font_stats->end()) {
      font_stats->insert(
          font_key, std::make_pair(run_font_data.glyph_count_, family_name));
    } else {
      font_stats_it->value.first += run_font_data.glyph_count_;
    }
  }
}

CSSSyntaxDefinition CreateCombinedSyntax() {
  CSSParserTokenStream stream(
      "<number> | <length> | <percentage> | <angle> | <color> | <time>");
  std::optional<CSSSyntaxDefinition> syntax_definition =
      CSSSyntaxDefinition::Consume(stream);
  DCHECK(syntax_definition.has_value());
  return *syntax_definition;
}

bool IsMarginPaddingProperty(CSSPropertyName property_name) {
  return property_name.Id() == CSSPropertyID::kMarginBottom ||
         property_name.Id() == CSSPropertyID::kMarginLeft ||
         property_name.Id() == CSSPropertyID::kMarginRight ||
         property_name.Id() == CSSPropertyID::kMarginTop ||
         property_name.Id() == CSSPropertyID::kPaddingBottom ||
         property_name.Id() == CSSPropertyID::kPaddingLeft ||
         property_name.Id() == CSSPropertyID::kPaddingRight ||
         property_name.Id() == CSSPropertyID::kPaddingTop;
}

bool IsInsetProperty(CSSPropertyName property_name) {
  return property_name.Id() == CSSPropertyID::kBottom ||
         property_name.Id() == CSSPropertyID::kLeft ||
         property_name.Id() == CSSPropertyID::kRight ||
         property_name.Id() == CSSPropertyID::kTop;
}

bool IsSizingProperty(CSSPropertyName property_name) {
  return property_name.Id() == CSSPropertyID::kHeight ||
         property_name.Id() == CSSPropertyID::kWidth ||
         property_name.Id() == CSSPropertyID::kMinHeight ||
         property_name.Id() == CSSPropertyID::kMinWidth ||
         property_name.Id() == CSSPropertyID::kMaxHeight ||
         property_name.Id() == CSSPropertyID::kMaxWidth;
}

LayoutUnit PickInlineOrBlockSize(LogicalSize value,
                                 CSSPropertyID property_id,
                                 WritingDirectionMode writing_direction_mode) {
  const CSSProperty& property = CSSProperty::Get(property_id);
  CSSPropertyID logical_property_id =
      property.ToLogical(writing_direction_mode).PropertyID();
  if (logical_property_id == CSSPropertyID::kInsetBlockEnd ||
      logical_property_id == CSSPropertyID::kInsetBlockStart ||
      logical_property_id == CSSPropertyID::kBlockSize ||
      logical_property_id == CSSPropertyID::kMaxBlockSize ||
      logical_property_id == CSSPropertyID::kMinBlockSize) {
    return value.block_size;
  }
  return value.inline_size;
}

}  // namespace

typedef blink::protocol::CSS::Backend::EnableCallback EnableCallback;
typedef blink::protocol::CSS::Backend::TakeComputedStyleUpdatesCallback
    TakeComputedStyleUpdatesCallback;

enum ForcePseudoClassFlags {
  kPseudoNone = 0,
  kPseudoHover = 1 << 0,
  kPseudoFocus = 1 << 1,
  kPseudoActive = 1 << 2,
  kPseudoVisited = 1 << 3,
  kPseudoFocusWithin = 1 << 4,
  kPseudoFocusVisible = 1 << 5,
  kPseudoTarget = 1 << 6,
  kPseudoEnabled = 1 << 7,
  kPseudoDisabled = 1 << 8,
  kPseudoValid = 1 << 9,
  kPseudoInvalid = 1 << 10,
  kPseudoUserValid = 1 << 11,
  kPseudoUserInvalid = 1 << 12,
  kPseudoRequired = 1 << 13,
  kPseudoOptional = 1 << 14,
  kPseudoReadOnly = 1 << 15,
  kPseudoReadWrite = 1 << 16,
  kPseudoInRange = 1 << 17,
  kPseudoOutOfRange = 1 << 18,
  kPseudoChecked = 1 << 19,
  kPseudoIndeterminate = 1 << 20,
  kPseudoPlaceholderShown = 1 << 21,
  kPseudoAutofill = 1 << 22,
  kPseudoLink = 1 << 23,
  kPseudoOpen = 1 << 24,
};

static unsigned ComputePseudoClassMask(
    std::unique_ptr<protocol::Array<String>> pseudo_class_array) {
  DEFINE_STATIC_LOCAL(String, active, ("active"));
  DEFINE_STATIC_LOCAL(String, hover, ("hover"));
  DEFINE_STATIC_LOCAL(String, focus, ("focus"));
  DEFINE_STATIC_LOCAL(String, focusVisible, ("focus-visible"));
  DEFINE_STATIC_LOCAL(String, focusWithin, ("focus-within"));
  DEFINE_STATIC_LOCAL(String, target, ("target"));
  // Specific pseudo states
  DEFINE_STATIC_LOCAL(String, enabled, ("enabled"));
  DEFINE_STATIC_LOCAL(String, disabled, ("disabled"));
  DEFINE_STATIC_LOCAL(String, valid, ("valid"));
  DEFINE_STATIC_LOCAL(String, invalid, ("invalid"));
  DEFINE_STATIC_LOCAL(String, userValid, ("user-valid"));
  DEFINE_STATIC_LOCAL(String, userInvalid, ("user-invalid"));
  DEFINE_STATIC_LOCAL(String, required, ("required"));
  DEFINE_STATIC_LOCAL(String, optional, ("optional"));
  DEFINE_STATIC_LOCAL(String, readOnly, ("read-only"));
  DEFINE_STATIC_LOCAL(String, readWrite, ("read-write"));
  DEFINE_STATIC_LOCAL(String, inRange, ("in-range"));
  DEFINE_STATIC_LOCAL(String, outOfRange, ("out-of-range"));
  DEFINE_STATIC_LOCAL(String, visited, ("visited"));
  DEFINE_STATIC_LOCAL(String, checked, ("checked"));
  DEFINE_STATIC_LOCAL(String, indeterminate, ("indeterminate"));
  DEFINE_STATIC_LOCAL(String, placeholderShown, ("placeholder-shown"));
  DEFINE_STATIC_LOCAL(String, autofill, ("autofill"));
  DEFINE_STATIC_LOCAL(String, link, ("link"));
  DEFINE_STATIC_LOCAL(String, open, ("open"));

  if (!pseudo_class_array || pseudo_class_array->empty())
    return kPseudoNone;

  unsigned result = kPseudoNone;
  for (const String& pseudo_class : *pseudo_class_array) {
    if (pseudo_class == active) {
      result |= kPseudoActive;
    } else if (pseudo_class == hover) {
      result |= kPseudoHover;
    } else if (pseudo_class == focus) {
      result |= kPseudoFocus;
    } else if (pseudo_class == focusVisible) {
      result |= kPseudoFocusVisible;
    } else if (pseudo_class == focusWithin) {
      result |= kPseudoFocusWithin;
    } else if (pseudo_class == target) {
      result |= kPseudoTarget;
    } else if (pseudo_class == enabled) {
      result |= kPseudoEnabled;
    } else if (pseudo_class == disabled) {
      result |= kPseudoDisabled;
    } else if (pseudo_class == valid) {
      result |= kPseudoValid;
    } else if (pseudo_class == invalid) {
      result |= kPseudoInvalid;
    } else if (pseudo_class == userValid) {
      result |= kPseudoUserValid;
    } else if (pseudo_class == userInvalid) {
      result |= kPseudoUserInvalid;
    } else if (pseudo_class == required) {
      result |= kPseudoRequired;
    } else if (pseudo_class == optional) {
      result |= kPseudoOptional;
    } else if (pseudo_class == readOnly) {
      result |= kPseudoReadOnly;
    } else if (pseudo_class == readWrite) {
      result |= kPseudoReadWrite;
    } else if (pseudo_class == inRange) {
      result |= kPseudoInRange;
    } else if (pseudo_class == outOfRange) {
      result |= kPseudoOutOfRange;
    } else if (pseudo_class == visited) {
      result |= kPseudoVisited;
    } else if (pseudo_class == checked) {
      result |= kPseudoChecked;
    } else if (pseudo_class == indeterminate) {
      result |= kPseudoIndeterminate;
    } else if (pseudo_class == placeholderShown) {
      result |= kPseudoPlaceholderShown;
    } else if (pseudo_class == autofill) {
      result |= kPseudoAutofill;
    } else if (pseudo_class == link) {
      result |= kPseudoLink;
    } else if (pseudo_class == open) {
      result |= kPseudoOpen;
    }
  }
  return result;
}

class InspectorCSSAgent::StyleSheetAction : public InspectorHistory::Action {
 public:
  StyleSheetAction(const String& name) : InspectorHistory::Action(name) {}
  StyleSheetAction(const StyleSheetAction&) = delete;
  StyleSheetAction& operator=(const StyleSheetAction&) = delete;

  virtual std::unique_ptr<protocol::CSS::CSSStyle> TakeSerializedStyle(
      Element* element) {
    return nullptr;
  }
};

class InspectorCSSAgent::SetStyleSheetTextAction final
    : public InspectorCSSAgent::StyleSheetAction {
 public:
  SetStyleSheetTextAction(InspectorStyleSheetBase* style_sheet,
                          const String& text)
      : InspectorCSSAgent::StyleSheetAction("SetStyleSheetText"),
        style_sheet_(style_sheet),
        text_(text) {}
  SetStyleSheetTextAction(const SetStyleSheetTextAction&) = delete;
  SetStyleSheetTextAction& operator=(const SetStyleSheetTextAction&) = delete;

  bool Perform(ExceptionState& exception_state) override {
    if (!style_sheet_->GetText(&old_text_))
      return false;
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    return style_sheet_->SetText(old_text_, exception_state);
  }

  bool Redo(ExceptionState& exception_state) override {
    return style_sheet_->SetText(text_, exception_state);
  }

  String MergeId() override {
    return StrCat({"SetStyleSheetText ", style_sheet_->Id()});
  }

  void Merge(Action* action) override {
    DCHECK_EQ(action->MergeId(), MergeId());

    SetStyleSheetTextAction* other =
        static_cast<SetStyleSheetTextAction*>(action);
    text_ = other->text_;
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(style_sheet_);
    InspectorCSSAgent::StyleSheetAction::Trace(visitor);
  }

 private:
  Member<InspectorStyleSheetBase> style_sheet_;
  String text_;
  String old_text_;
};

class InspectorCSSAgent::ModifyRuleAction final
    : public InspectorCSSAgent::StyleSheetAction {
 public:
  enum Type {
    kSetRuleSelector,
    kSetStyleText,
    kSetMediaRuleText,
    kSetContainerRuleText,
    kSetSupportsRuleText,
    kSetKeyframeKey,
    kSetPropertyName,
    kSetScopeRuleText,
  };

  ModifyRuleAction(Type type,
                   InspectorStyleSheet* style_sheet,
                   const SourceRange& range,
                   const String& text)
      : InspectorCSSAgent::StyleSheetAction("ModifyRuleAction"),
        style_sheet_(style_sheet),
        type_(type),
        new_text_(text),
        old_range_(range),
        css_rule_(nullptr) {}
  ModifyRuleAction(const ModifyRuleAction&) = delete;
  ModifyRuleAction& operator=(const ModifyRuleAction&) = delete;

  bool Perform(ExceptionState& exception_state) override {
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    switch (type_) {
      case kSetRuleSelector:
        return style_sheet_->SetRuleSelector(new_range_, old_text_, nullptr,
                                             nullptr, exception_state);
      case kSetStyleText:
        return style_sheet_->SetStyleText(new_range_, old_text_, nullptr,
                                          nullptr, exception_state);
      case kSetMediaRuleText:
        return style_sheet_->SetMediaRuleText(new_range_, old_text_, nullptr,
                                              nullptr, exception_state);
      case kSetContainerRuleText:
        return style_sheet_->SetContainerRuleText(
            new_range_, old_text_, nullptr, nullptr, exception_state);
      case kSetSupportsRuleText:
        return style_sheet_->SetSupportsRuleText(new_range_, old_text_, nullptr,
                                                 nullptr, exception_state);
      case kSetKeyframeKey:
        return style_sheet_->SetKeyframeKey(new_range_, old_text_, nullptr,
                                            nullptr, exception_state);
      case kSetPropertyName:
        return style_sheet_->SetPropertyName(new_range_, old_text_, nullptr,
                                             nullptr, exception_state);
      case kSetScopeRuleText:
        return style_sheet_->SetScopeRuleText(new_range_, old_text_, nullptr,
                                              nullptr, exception_state);
      default:
        NOTREACHED();
    }
  }

  bool Redo(ExceptionState& exception_state) override {
    switch (type_) {
      case kSetRuleSelector:
        css_rule_ = style_sheet_->SetRuleSelector(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetStyleText:
        css_rule_ = style_sheet_->SetStyleText(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetMediaRuleText:
        css_rule_ = style_sheet_->SetMediaRuleText(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetContainerRuleText:
        css_rule_ = style_sheet_->SetContainerRuleText(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetSupportsRuleText:
        css_rule_ = style_sheet_->SetSupportsRuleText(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetKeyframeKey:
        css_rule_ = style_sheet_->SetKeyframeKey(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetPropertyName:
        css_rule_ = style_sheet_->SetPropertyName(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      case kSetScopeRuleText:
        css_rule_ = style_sheet_->SetScopeRuleText(
            old_range_, new_text_, &new_range_, &old_text_, exception_state);
        break;
      default:
        NOTREACHED();
    }
    return css_rule_ != nullptr;
  }

  CSSRule* TakeRule() {
    CSSRule* result = css_rule_;
    css_rule_ = nullptr;
    return result;
  }

  std::unique_ptr<protocol::CSS::CSSStyle> TakeSerializedStyle(
      Element* element) override {
    if (type_ != kSetStyleText)
      return nullptr;
    CSSRule* rule = TakeRule();
    if (auto* style_rule = DynamicTo<CSSStyleRule>(rule))
      return style_sheet_->BuildObjectForStyle(style_rule->style(), element);
    if (auto* keyframe_rule = DynamicTo<CSSKeyframeRule>(rule))
      return style_sheet_->BuildObjectForStyle(keyframe_rule->style(), element);
    if (auto* property_rule = DynamicTo<CSSPropertyRule>(rule)) {
      return style_sheet_->BuildObjectForStyle(property_rule->Style(), nullptr);
    }
    if (auto* font_palette_values_rule =
            DynamicTo<CSSFontPaletteValuesRule>(rule)) {
      return style_sheet_->BuildObjectForStyle(
          font_palette_values_rule->Style(), nullptr);
    }
    if (auto* position_try_rule = DynamicTo<CSSPositionTryRule>(rule)) {
      return style_sheet_->BuildObjectForStyle(position_try_rule->style(),
                                               nullptr);
    }
    return nullptr;
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(style_sheet_);
    visitor->Trace(css_rule_);
    InspectorCSSAgent::StyleSheetAction::Trace(visitor);
  }

  String MergeId() override {
    return String::Format("ModifyRuleAction:%d %s:%d", type_,
                          style_sheet_->Id().Utf8().c_str(), old_range_.start);
  }

  bool IsNoop() override { return old_text_ == new_text_; }

  void Merge(Action* action) override {
    DCHECK_EQ(action->MergeId(), MergeId());

    ModifyRuleAction* other = static_cast<ModifyRuleAction*>(action);
    new_text_ = other->new_text_;
    new_range_ = other->new_range_;
  }

 private:
  Member<InspectorStyleSheet> style_sheet_;
  Type type_;
  String old_text_;
  String new_text_;
  SourceRange old_range_;
  SourceRange new_range_;
  Member<CSSRule> css_rule_;
};

class InspectorCSSAgent::SetElementStyleAction final
    : public InspectorCSSAgent::StyleSheetAction {
 public:
  SetElementStyleAction(InspectorStyleSheetForInlineStyle* style_sheet,
                        const String& text)
      : InspectorCSSAgent::StyleSheetAction("SetElementStyleAction"),
        style_sheet_(style_sheet),
        text_(text) {}
  SetElementStyleAction(const SetElementStyleAction&) = delete;
  SetElementStyleAction& operator=(const SetElementStyleAction&) = delete;

  bool Perform(ExceptionState& exception_state) override {
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    return style_sheet_->SetText(old_text_, exception_state);
  }

  bool Redo(ExceptionState& exception_state) override {
    if (!style_sheet_->GetText(&old_text_))
      return false;
    return style_sheet_->SetText(text_, exception_state);
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(style_sheet_);
    InspectorCSSAgent::StyleSheetAction::Trace(visitor);
  }

  String MergeId() override {
    return StrCat({"SetElementStyleAction:", style_sheet_->Id()});
  }

  std::unique_ptr<protocol::CSS::CSSStyle> TakeSerializedStyle(
      Element* element) override {
    return style_sheet_->BuildObjectForStyle(style_sheet_->InlineStyle(),
                                             element);
  }

  void Merge(Action* action) override {
    DCHECK_EQ(action->MergeId(), MergeId());

    SetElementStyleAction* other = static_cast<SetElementStyleAction*>(action);
    text_ = other->text_;
  }

 private:
  Member<InspectorStyleSheetForInlineStyle> style_sheet_;
  String text_;
  String old_text_;
};

class InspectorCSSAgent::AddRuleAction final
    : public InspectorCSSAgent::StyleSheetAction {
 public:
  AddRuleAction(InspectorStyleSheet* style_sheet,
                const String& rule_text,
                const SourceRange& location)
      : InspectorCSSAgent::StyleSheetAction("AddRule"),
        style_sheet_(style_sheet),
        rule_text_(rule_text),
        location_(location) {}
  AddRuleAction(const AddRuleAction&) = delete;
  AddRuleAction& operator=(const AddRuleAction&) = delete;

  bool Perform(ExceptionState& exception_state) override {
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    CSSStyleSheet::InspectorMutationScope scope(style_sheet_->PageStyleSheet());
    return style_sheet_->DeleteRule(added_range_, exception_state);
  }

  bool Redo(ExceptionState& exception_state) override {
    CSSStyleSheet::InspectorMutationScope scope(style_sheet_->PageStyleSheet());
    css_rule_ = style_sheet_->AddRule(rule_text_, location_, &added_range_,
                                      exception_state);
    if (exception_state.HadException())
      return false;
    return true;
  }

  CSSStyleRule* TakeRule() {
    CSSStyleRule* result = css_rule_;
    css_rule_ = nullptr;
    return result;
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(style_sheet_);
    visitor->Trace(css_rule_);
    InspectorCSSAgent::StyleSheetAction::Trace(visitor);
  }

 private:
  Member<InspectorStyleSheet> style_sheet_;
  Member<CSSStyleRule> css_rule_;
  String rule_text_;
  String old_text_;
  SourceRange location_;
  SourceRange added_range_;
};

// static
CSSStyleRule* InspectorCSSAgent::AsCSSStyleRule(CSSRule* rule) {
  return DynamicTo<CSSStyleRule>(rule);
}

// static
CSSMediaRule* InspectorCSSAgent::AsCSSMediaRule(CSSRule* rule) {
  return DynamicTo<CSSMediaRule>(rule);
}

// static
CSSContainerRule* InspectorCSSAgent::AsCSSContainerRule(CSSRule* rule) {
  return DynamicTo<CSSContainerRule>(rule);
}

// static
CSSSupportsRule* InspectorCSSAgent::AsCSSSupportsRule(CSSRule* rule) {
  return DynamicTo<CSSSupportsRule>(rule);
}

// static
CSSScopeRule* InspectorCSSAgent::AsCSSScopeRule(CSSRule* rule) {
  return DynamicTo<CSSScopeRule>(rule);
}

InspectorCSSAgent::InspectorCSSAgent(
    InspectorDOMAgent* dom_agent,
    InspectedFrames* inspected_frames,
    InspectorNetworkAgent* network_agent,
    InspectorResourceContentLoader* resource_content_loader,
    InspectorResourceContainer* resource_container)
    : dom_agent_(dom_agent),
      inspected_frames_(inspected_frames),
      network_agent_(network_agent),
      resource_content_loader_(resource_content_loader),
      resource_container_(resource_container),
      resource_content_loader_client_id_(
          resource_content_loader->CreateClientId()),
      enable_requested_(&agent_state_, /*default_value=*/false),
      enable_completed_(false),
      coverage_enabled_(&agent_state_, /*default_value=*/false),
      local_fonts_enabled_(&agent_state_, /*default_value=*/true) {
  DCHECK(dom_agent);
  DCHECK(network_agent);
}

InspectorCSSAgent::~InspectorCSSAgent() = default;

void InspectorCSSAgent::Restore() {
  if (enable_requested_.Get())
    CompleteEnabled();
  if (coverage_enabled_.Get())
    SetCoverageEnabled(true);
}

void InspectorCSSAgent::FlushPendingProtocolNotifications() {
  if (!invalidated_documents_.size())
    return;
  HeapHashSet<Member<Document>> invalidated_documents;
  invalidated_documents_.swap(invalidated_documents);
  for (Document* document : invalidated_documents)
    UpdateActiveStyleSheets(document);
}

void InspectorCSSAgent::Reset() {
  id_to_inspector_style_sheet_.clear();
  id_to_inspector_style_sheet_for_inline_style_.clear();
  css_style_sheet_to_inspector_style_sheet_.clear();
  document_to_css_style_sheets_.clear();
  invalidated_documents_.clear();
  node_to_inspector_style_sheet_.clear();
  notify_computed_style_updated_node_ids_.clear();
  ResetNonPersistentData();
}

void InspectorCSSAgent::ResetNonPersistentData() {
  ResetPseudoStates();
  ResetStartingStyles();
}

void InspectorCSSAgent::enable(std::unique_ptr<EnableCallback> prp_callback) {
  if (!dom_agent_->Enabled()) {
    prp_callback->sendFailure(protocol::Response::ServerError(
        "DOM agent needs to be enabled first."));
    return;
  }
  enable_requested_.Set(true);
  resource_content_loader_->EnsureResourcesContentLoaded(
      resource_content_loader_client_id_,
      WTF::BindOnce(&InspectorCSSAgent::ResourceContentLoaded,
                    WrapPersistent(this), std::move(prp_callback)));
}

void InspectorCSSAgent::ResourceContentLoaded(
    std::unique_ptr<EnableCallback> callback) {
  if (enable_requested_.Get())  // Could be disabled while fetching resources.
    CompleteEnabled();
  callback->sendSuccess();
}

void InspectorCSSAgent::CompleteEnabled() {
  instrumenting_agents_->AddInspectorCSSAgent(this);
  dom_agent_->AddDOMListener(this);
  HeapVector<Member<Document>> documents = dom_agent_->Documents();
  for (Document* document : documents) {
    UpdateActiveStyleSheets(document);
    TriggerFontsUpdatedForDocument(document);
  }
  enable_completed_ = true;
}

void InspectorCSSAgent::TriggerFontsUpdatedForDocument(Document* document) {
  const HeapLinkedHashSet<Member<FontFace>>& faces =
      document->GetStyleEngine()
          .GetFontSelector()
          ->GetFontFaceCache()
          ->CssConnectedFontFaces();
  for (FontFace* face : faces) {
    CSSFontFace* css_face = face->CssFontFace();
    if (!css_face)
      continue;
    const CSSFontFaceSource* source = css_face->FrontSource();
    if (!source || !source->IsLoaded())
      continue;
    const FontCustomPlatformData* data = source->GetCustomPlaftormData();
    if (!data)
      continue;
    FontsUpdated(face, source->GetURL(), data);
  }
}

protocol::Response InspectorCSSAgent::disable() {
  Reset();
  dom_agent_->RemoveDOMListener(this);
  instrumenting_agents_->RemoveInspectorCSSAgent(this);
  enable_completed_ = false;
  enable_requested_.Set(false);
  resource_content_loader_->Cancel(resource_content_loader_client_id_);
  coverage_enabled_.Set(false);
  local_fonts_enabled_.Set(true);
  SetCoverageEnabled(false);
  return protocol::Response::Success();
}

void InspectorCSSAgent::DidCommitLoadForLocalFrame(LocalFrame* frame) {
  if (frame == inspected_frames_->Root())
    Reset();
}

void InspectorCSSAgent::MediaQueryResultChanged() {
  FlushPendingProtocolNotifications();
  GetFrontend()->mediaQueryResultChanged();
}

void InspectorCSSAgent::FontsUpdated(
    const FontFace* font,
    const String& src,
    const FontCustomPlatformData* fontCustomPlatformData) {
  FlushPendingProtocolNotifications();

  if (!(font && fontCustomPlatformData)) {
    GetFrontend()->fontsUpdated();
    return;
  }

  Vector<VariationAxis> variation_axis =
      fontCustomPlatformData->GetVariationAxes();

  auto variation_axes =
      std::make_unique<protocol::Array<protocol::CSS::FontVariationAxis>>();
  for (const VariationAxis& axis : variation_axis) {
    variation_axes->push_back(protocol::CSS::FontVariationAxis::create()
                                  .setMinValue(axis.minValue)
                                  .setMaxValue(axis.maxValue)
                                  .setDefaultValue(axis.defaultValue)
                                  .setName(axis.name)
                                  .setTag(axis.tag)
                                  .build());
  }

  // blink::FontFace returns sane property defaults per the web fonts spec,
  // so we don't perform null checks here.
  std::unique_ptr<protocol::CSS::FontFace> font_face =
      protocol::CSS::FontFace::create()
          .setFontFamily(font->family())
          .setFontStyle(font->style())
          .setFontVariant(font->variant())
          .setFontWeight(font->weight())
          .setFontStretch(font->stretch())
          .setFontDisplay(font->display())
          .setUnicodeRange(font->unicodeRange())
          .setSrc(src)
          .setPlatformFontFamily(
              fontCustomPlatformData->FamilyNameForInspector())
          .setFontVariationAxes(
              variation_axes->size() ? std::move(variation_axes) : nullptr)
          .build();
  GetFrontend()->fontsUpdated(std::move(font_face));
}

void InspectorCSSAgent::ActiveStyleSheetsUpdated(Document* document) {
  invalidated_documents_.insert(document);
}

void InspectorCSSAgent::UpdateActiveStyleSheets(Document* document) {
  HeapVector<Member<CSSStyleSheet>> new_sheets_vector;
  InspectorCSSAgent::CollectAllDocumentStyleSheets(document, new_sheets_vector);
  SetActiveStyleSheets(document, new_sheets_vector);
}

void InspectorCSSAgent::SetActiveStyleSheets(
    Document* document,
    const HeapVector<Member<CSSStyleSheet>>& all_sheets_vector) {
  auto it = document_to_css_style_sheets_.find(document);
  GCedHeapHashSet<Member<CSSStyleSheet>>* document_css_style_sheets = nullptr;

  if (it != document_to_css_style_sheets_.end()) {
    document_css_style_sheets = it->value;
  } else {
    document_css_style_sheets =
        MakeGarbageCollected<GCedHeapHashSet<Member<CSSStyleSheet>>>();
    document_to_css_style_sheets_.Set(document, document_css_style_sheets);
  }

  // Style engine sometimes returns the same stylesheet multiple
  // times, probably, because it's used in multiple places.
  // We need to deduplicate because the frontend does not expect
  // duplicate styleSheetAdded events.
  HeapHashSet<Member<CSSStyleSheet>> unique_sheets;
  for (CSSStyleSheet* css_style_sheet : all_sheets_vector) {
    if (!unique_sheets.Contains(css_style_sheet))
      unique_sheets.insert(css_style_sheet);
  }

  HeapHashSet<Member<CSSStyleSheet>> removed_sheets(*document_css_style_sheets);
  HeapVector<Member<CSSStyleSheet>> added_sheets;
  for (CSSStyleSheet* css_style_sheet : unique_sheets) {
    if (removed_sheets.Contains(css_style_sheet)) {
      removed_sheets.erase(css_style_sheet);
    } else {
      added_sheets.push_back(css_style_sheet);
    }
  }

  for (CSSStyleSheet* css_style_sheet : removed_sheets) {
    InspectorStyleSheet* inspector_style_sheet =
        css_style_sheet_to_inspector_style_sheet_.at(css_style_sheet);
    document_css_style_sheets->erase(css_style_sheet);
    if (id_to_inspector_style_sheet_.Contains(inspector_style_sheet->Id())) {
      String id = UnbindStyleSheet(inspector_style_sheet);
      if (GetFrontend())
        GetFrontend()->styleSheetRemoved(id);
    }
  }

  for (CSSStyleSheet* css_style_sheet : added_sheets) {
    InspectorStyleSheet* new_style_sheet = BindStyleSheet(css_style_sheet);
    document_css_style_sheets->insert(css_style_sheet);
    new_style_sheet->SyncTextIfNeeded();
    if (GetFrontend()) {
      GetFrontend()->styleSheetAdded(
          new_style_sheet->BuildObjectForStyleSheetInfo());
    }
  }

  if (document_css_style_sheets->empty())
    document_to_css_style_sheets_.erase(document);
}

void InspectorCSSAgent::DocumentDetached(Document* document) {
  invalidated_documents_.erase(document);
  SetActiveStyleSheets(document, HeapVector<Member<CSSStyleSheet>>());
}

void InspectorCSSAgent::ForcePseudoState(Element* element,
                                         CSSSelector::PseudoType pseudo_type,
                                         bool* result) {
  if (node_id_to_forced_pseudo_state_.empty())
    return;

  int node_id = dom_agent_->BoundNodeId(element);
  if (!node_id)
    return;

  // First check whether focus-within was set because focus or focus-within was
  // forced for a child node.
  NodeIdToNumberFocusedChildren::iterator focused_it =
      node_id_to_number_focused_children_.find(node_id);
  unsigned focused_count =
      focused_it == node_id_to_number_focused_children_.end()
          ? 0
          : focused_it->value;
  if (pseudo_type == CSSSelector::kPseudoFocusWithin && focused_count > 0) {
    *result = true;
    return;
  }

  NodeIdToForcedPseudoState::iterator it =
      node_id_to_forced_pseudo_state_.find(node_id);
  if (it == node_id_to_forced_pseudo_state_.end())
    return;

  bool force = false;
  unsigned forced_pseudo_state = it->value;

  switch (pseudo_type) {
    case CSSSelector::kPseudoActive:
      force = forced_pseudo_state & kPseudoActive;
      break;
    case CSSSelector::kPseudoFocus:
      force = forced_pseudo_state & kPseudoFocus;
      break;
    case CSSSelector::kPseudoFocusWithin:
      force = forced_pseudo_state & kPseudoFocusWithin;
      break;
    case CSSSelector::kPseudoFocusVisible:
      force = forced_pseudo_state & kPseudoFocusVisible;
      break;
    case CSSSelector::kPseudoHover:
      force = forced_pseudo_state & kPseudoHover;
      break;
    case CSSSelector::kPseudoTarget:
      force = forced_pseudo_state & kPseudoTarget;
      break;
    case CSSSelector::kPseudoEnabled:
      force = forced_pseudo_state & kPseudoEnabled;
      break;
    case CSSSelector::kPseudoDisabled:
      force = forced_pseudo_state & kPseudoDisabled;
      break;
    case CSSSelector::kPseudoValid:
      force = forced_pseudo_state & kPseudoValid;
      break;
    case CSSSelector::kPseudoInvalid:
      force = forced_pseudo_state & kPseudoInvalid;
      break;
    case CSSSelector::kPseudoUserValid:
      force = forced_pseudo_state & kPseudoUserValid;
      break;
    case CSSSelector::kPseudoUserInvalid:
      force = forced_pseudo_state & kPseudoUserInvalid;
      break;
    case CSSSelector::kPseudoRequired:
      force = forced_pseudo_state & kPseudoRequired;
      break;
    case CSSSelector::kPseudoOptional:
      force = forced_pseudo_state & kPseudoOptional;
      break;
    case CSSSelector::kPseudoReadOnly:
      force = forced_pseudo_state & kPseudoReadOnly;
      break;
    case CSSSelector::kPseudoReadWrite:
      force = forced_pseudo_state & kPseudoReadWrite;
      break;
    case CSSSelector::kPseudoInRange:
      force = forced_pseudo_state & kPseudoInRange;
      break;
    case CSSSelector::kPseudoOutOfRange:
      force = forced_pseudo_state & kPseudoOutOfRange;
      break;
    case CSSSelector::kPseudoVisited:
      force = forced_pseudo_state & kPseudoVisited;
      break;
    case CSSSelector::kPseudoLink:
      force = forced_pseudo_state & kPseudoLink;
      break;
    case CSSSelector::kPseudoChecked:
      force = forced_pseudo_state & kPseudoChecked;
      break;
    case CSSSelector::kPseudoIndeterminate:
      force = forced_pseudo_state & kPseudoIndeterminate;
      break;
    case CSSSelector::kPseudoPlaceholderShown:
      force = forced_pseudo_state & kPseudoPlaceholderShown;
      break;
    case CSSSelector::kPseudoAutofill:
      force = forced_pseudo_state & kPseudoAutofill;
      break;
    case CSSSelector::kPseudoOpen:
      force = forced_pseudo_state & kPseudoOpen;
      break;
    default:
      break;
  }
  if (force)
    *result = true;
}

void InspectorCSSAgent::ForceStartingStyle(Element* element, bool* result) {
  if (node_id_to_forced_starting_style_.empty()) {
    return;
  }

  int node_id = dom_agent_->BoundNodeId(element);
  if (!node_id) {
    return;
  }

  if (!node_id_to_forced_starting_style_.Contains(node_id)) {
    return;
  }

  *result = true;
}

protocol::Response InspectorCSSAgent::getMediaQueries(
    std::unique_ptr<protocol::Array<protocol::CSS::CSSMedia>>* medias) {
  *medias = std::make_unique<protocol::Array<protocol::CSS::CSSMedia>>();
  for (auto& style : id_to_inspector_style_sheet_) {
    InspectorStyleSheet* style_sheet = style.value;
    CollectMediaQueriesFromStyleSheet(style_sheet->PageStyleSheet(),
                                      medias->get(), nullptr);
    const CSSRuleVector& flat_rules = style_sheet->FlatRules();
    for (unsigned i = 0; i < flat_rules.size(); ++i) {
      CSSRule* rule = flat_rules.at(i).Get();
      if (rule->GetType() == CSSRule::kMediaRule ||
          rule->GetType() == CSSRule::kImportRule)
        CollectMediaQueriesFromRule(rule, medias->get(), nullptr);
    }
  }
  return protocol::Response::Success();
}

std::unique_ptr<protocol::CSS::CSSLayerData>
InspectorCSSAgent::BuildLayerDataObject(const CascadeLayer* layer,
                                        unsigned& max_order) {
  const unsigned order = layer->GetOrder().value_or(0);
  max_order = max(max_order, order);
  std::unique_ptr<protocol::CSS::CSSLayerData> layer_data =
      protocol::CSS::CSSLayerData::create()
          .setName(layer->GetName())
          .setOrder(order)
          .build();
  const auto& sublayers = layer->GetDirectSubLayers();
  if (sublayers.empty())
    return layer_data;

  auto sublayers_data =
      std::make_unique<protocol::Array<protocol::CSS::CSSLayerData>>();
  for (const CascadeLayer* sublayer : sublayers)
    sublayers_data->emplace_back(BuildLayerDataObject(sublayer, max_order));
  layer_data->setSubLayers(std::move(sublayers_data));
  return layer_data;
}

protocol::Response InspectorCSSAgent::getLayersForNode(
    int node_id,
    std::unique_ptr<protocol::CSS::CSSLayerData>* root_layer) {
  Element* element = nullptr;
  const protocol::Response response =
      dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess())
    return response;

  *root_layer = protocol::CSS::CSSLayerData::create()
                    .setName("implicit outer layer")
                    .setOrder(0)
                    .build();

  const auto* scoped_resolver =
      element->GetTreeScope().GetScopedStyleResolver();
  // GetScopedStyleResolver returns a nullptr if the tree scope has no
  // stylesheets.
  if (!scoped_resolver)
    return protocol::Response::Success();

  const CascadeLayerMap* layer_map = scoped_resolver->GetCascadeLayerMap();

  if (!layer_map)
    return protocol::Response::Success();

  const CascadeLayer* root = layer_map->GetRootLayer();
  unsigned max_order = 0;
  auto sublayers_data =
      std::make_unique<protocol::Array<protocol::CSS::CSSLayerData>>();
  for (const auto& sublayer : root->GetDirectSubLayers())
    sublayers_data->emplace_back(BuildLayerDataObject(sublayer, max_order));
  (*root_layer)->setOrder(max_order + 1);
  (*root_layer)->setSubLayers(std::move(sublayers_data));

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::getLocationForSelector(
    const String& style_sheet_id,
    const String& selector_text,
    std::unique_ptr<protocol::Array<protocol::CSS::SourceRange>>* ranges) {
  InspectorStyleSheet* style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, style_sheet);
  if (response.IsError()) {
    return response;
  }

  *ranges = std::make_unique<protocol::Array<protocol::CSS::SourceRange>>();

  const CSSRuleVector& css_rules = style_sheet->FlatRules();
  for (auto css_rule : css_rules) {
    CSSStyleRule* css_style_rule = DynamicTo<CSSStyleRule>(css_rule.Get());
    if (css_style_rule == nullptr) {
      continue;
    }
    CHECK(css_style_rule->GetStyleRule());

    // Iterate over selector list. (eg. `.box, .alert` => ['.box', '.alert'])
    for (const CSSSelector* selector =
             css_style_rule->GetStyleRule()->FirstSelector();
         selector; selector = CSSSelectorList::Next(*selector)) {
      if (selector->SelectorText() == selector_text) {
        const CSSRuleSourceData* source_data =
            style_sheet->SourceDataForRule(css_style_rule);
        if (source_data == nullptr) {
          continue;
        }
        std::unique_ptr<protocol::CSS::SourceRange> range =
            style_sheet->BuildSourceRangeObject(source_data->rule_header_range);

        const CSSStyleSheet* page_style_sheet = style_sheet->PageStyleSheet();
        const TextPosition start_position =
            page_style_sheet->StartPositionInSource();
        if (range->getStartLine() == 0) {
          range->setStartColumn(range->getStartColumn() +
                                start_position.column_.ZeroBasedInt());
        }
        if (range->getEndLine() == 0) {
          range->setEndColumn(range->getEndColumn() +
                              start_position.column_.ZeroBasedInt());
        }
        range->setStartLine(range->getStartLine() +
                            start_position.line_.ZeroBasedInt());
        range->setEndLine(range->getEndLine() +
                          start_position.line_.ZeroBasedInt());
        (*ranges)->emplace_back(std::move(range));
      }
    }
  }

  if ((*ranges)->empty()) {
    String message = StrCat({"Failed to find selector '", selector_text,
                             "' in style sheet ", style_sheet->FinalURL()});
    return protocol::Response::InvalidParams(message.Utf8());
  }

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::getAnimatedStylesForNode(
    int node_id,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSAnimationStyle>>*
        animation_styles,
    std::unique_ptr<protocol::CSS::CSSStyle>* transitions_style,
    std::unique_ptr<
        protocol::Array<protocol::CSS::InheritedAnimatedStyleEntry>>*
        inherited) {
  protocol::Response response = AssertEnabled();
  Element* element = nullptr;
  response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess()) {
    return response;
  }

  auto* owner_document = element->ownerDocument();
  if (!owner_document) {
    return protocol::Response::ServerError(
        "Element does not have an owner document");
  }

  if (!owner_document->GetFrame()) {
    return protocol::Response::ServerError(
        "Owner document does not have a frame attached");
  }

  Element* animating_element = element;
  PseudoId element_pseudo_id = kPseudoIdNone;
  AtomicString view_transition_name = g_null_atom;
  PseudoElement* pseudo_element = nullptr;
  // If the requested element is a view transition pseudo-element, `element`
  // becomes the first non-pseudo parent element or shadow host element after
  // `GetPseudoIdAndTag` call below.
  element = GetPseudoIdAndTag(element, pseudo_element, element_pseudo_id,
                              view_transition_name);
  if (!element) {
    return protocol::Response::ServerError("Pseudo element has no parent");
  }

  // `UpdateViewportSize` makes sure that while getting the values
  // of the interpolated properties, we have the `viewport_size_` defined.
  // Otherwise, when there is a resize is in progress and we try to
  // get the values of the interpolated properties, call to `GetViewportSize()`
  // causes a crash because `viewport_size_` is not defined.
  animating_element->GetDocument().GetStyleEngine().UpdateViewportSize();
  *animation_styles = BuildArrayForCSSAnimationStyleList(animating_element);
  *transitions_style = BuildObjectForTransitionsStyle(animating_element);

  // Inherited styles.
  *inherited = std::make_unique<
      protocol::Array<protocol::CSS::InheritedAnimatedStyleEntry>>();
  Element* parent_element =
      element_pseudo_id ? element : FlatTreeTraversal::ParentElement(*element);
  while (parent_element) {
    std::unique_ptr<protocol::CSS::InheritedAnimatedStyleEntry> entry =
        protocol::CSS::InheritedAnimatedStyleEntry::create()
            .setAnimationStyles(
                BuildArrayForCSSAnimationStyleList(parent_element))
            .setTransitionsStyle(BuildObjectForTransitionsStyle(parent_element))
            .build();
    (*inherited)->emplace_back(std::move(entry));
    parent_element = FlatTreeTraversal::ParentElement(*parent_element);
  }

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::getMatchedStylesForNode(
    int node_id,
    std::unique_ptr<protocol::CSS::CSSStyle>* inline_style,
    std::unique_ptr<protocol::CSS::CSSStyle>* attributes_style,
    std::unique_ptr<protocol::Array<protocol::CSS::RuleMatch>>*
        matched_css_rules,
    std::unique_ptr<protocol::Array<protocol::CSS::PseudoElementMatches>>*
        pseudo_id_matches,
    std::unique_ptr<protocol::Array<protocol::CSS::InheritedStyleEntry>>*
        inherited_entries,
    std::unique_ptr<
        protocol::Array<protocol::CSS::InheritedPseudoElementMatches>>*
        inherited_pseudo_id_matches,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSKeyframesRule>>*
        css_keyframes_rules,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSPositionTryRule>>*
        css_position_try_rules,
    std::optional<int>* active_position_fallback_index,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSPropertyRule>>*
        css_property_rules,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSPropertyRegistration>>*
        css_property_registrations,
    std::unique_ptr<protocol::CSS::CSSFontPaletteValuesRule>*
        css_font_palette_values_rule,
    std::optional<int>* parent_layout_node_id,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSFunctionRule>>*
        css_function_rules) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess())
    return response;

  Element* element = nullptr;
  response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess())
    return response;

  Element* animating_element = element;

  PseudoId element_pseudo_id = kPseudoIdNone;
  AtomicString view_transition_name = g_null_atom;
  // If the requested element is a view transition pseudo-element, `element`
  // becomes the first non-pseudo parent element or shadow host element after
  // `GetPseudoIdAndTag` call below.
  PseudoElement* pseudo_element = nullptr;
  element = GetPseudoIdAndTag(element, pseudo_element, element_pseudo_id,
                              view_transition_name);
  if (!element)
    return protocol::Response::ServerError("Pseudo element has no parent");

  Document& document = element->GetDocument();
  // A non-active document has no styles.
  if (!document.IsActive())
    return protocol::Response::ServerError("Document is not active");

  InspectorGhostRules ghost_rules;
  HeapVector<Member<CSSStyleSheet>> ghost_sheets;

  // The source text of mutable stylesheets needs to be updated
  // to sync the latest changes.
  for (InspectorStyleSheet* stylesheet :
       css_style_sheet_to_inspector_style_sheet_.Values()) {
    stylesheet->SyncTextIfNeeded();
    if (RuntimeEnabledFeatures::InspectorGhostRulesEnabled()) {
      ghost_sheets.push_back(stylesheet->PageStyleSheet());
    }
  }

  if (RuntimeEnabledFeatures::InspectorGhostRulesEnabled()) {
    ghost_rules.PopulateSheetsWithAssertion(std::move(ghost_sheets));
    ghost_rules.Activate(document);
  }

  CheckPseudoHasCacheScope check_pseudo_has_cache_scope(
      &document, /*within_selector_checking=*/false);
  InspectorStyleResolver resolver(element, element_pseudo_id,
                                  view_transition_name);

  // Matched rules.
  *matched_css_rules = BuildArrayForMatchedRuleList(
      resolver.MatchedRules(), element, ghost_rules, element_pseudo_id,
      view_transition_name);

  // Inherited styles.
  *inherited_entries =
      std::make_unique<protocol::Array<protocol::CSS::InheritedStyleEntry>>();
  for (InspectorCSSMatchedRules* match : resolver.ParentRules()) {
    std::unique_ptr<protocol::CSS::InheritedStyleEntry> entry =
        protocol::CSS::InheritedStyleEntry::create()
            .setMatchedCSSRules(BuildArrayForMatchedRuleList(
                match->matched_rules, element, ghost_rules, element_pseudo_id,
                view_transition_name))
            .build();
    if (match->element->style() && match->element->style()->length()) {
      InspectorStyleSheetForInlineStyle* style_sheet =
          AsInspectorStyleSheet(match->element);
      if (style_sheet) {
        entry->setInlineStyle(style_sheet->BuildObjectForStyle(
            style_sheet->InlineStyle(), element, element_pseudo_id,
            view_transition_name));
      }
    }
    (*inherited_entries)->emplace_back(std::move(entry));
  }

  *css_keyframes_rules = AnimationsForNode(element, animating_element);

  std::tie(*css_property_rules, *css_property_registrations) =
      CustomPropertiesForNode(element);

  // Pseudo-elements.
  // Some pseudo-elements can have nested pseudo-elements,
  // and we need to show them in the originating pseudo-element's
  // styles.
  if (pseudo_element) {
    if (!pseudo_element->CanHaveNestedPseudoElement()) {
      return protocol::Response::Success();
    }
    element = pseudo_element;
    resolver =
        InspectorStyleResolver(pseudo_element, kPseudoIdNone, g_null_atom);
  }

  InspectorStyleSheetForInlineStyle* inline_style_sheet =
      AsInspectorStyleSheet(element);
  if (inline_style_sheet) {
    *inline_style =
        inline_style_sheet->BuildObjectForStyle(element->style(), element);
    *attributes_style = BuildObjectForAttributesStyle(element);
  }

  *pseudo_id_matches =
      std::make_unique<protocol::Array<protocol::CSS::PseudoElementMatches>>();

  for (InspectorCSSMatchedRules* match : resolver.PseudoElementRules()) {
    (*pseudo_id_matches)
        ->emplace_back(
            protocol::CSS::PseudoElementMatches::create()
                .setPseudoType(InspectorDOMAgent::ProtocolPseudoElementType(
                    match->pseudo_id))
                .setMatches(BuildArrayForMatchedRuleList(
                    match->matched_rules, element, ghost_rules,
                    match->pseudo_id, match->view_transition_name))
                .build());
    if (match->view_transition_name) {
      (*pseudo_id_matches)
          ->back()
          ->setPseudoIdentifier(match->view_transition_name);
    }
  }

  *inherited_pseudo_id_matches = std::make_unique<
      protocol::Array<protocol::CSS::InheritedPseudoElementMatches>>();

  for (InspectorCSSMatchedPseudoElements* match :
       resolver.ParentPseudoElementRules()) {
    auto parent_pseudo_element_matches = std::make_unique<
        protocol::Array<protocol::CSS::PseudoElementMatches>>();
    for (InspectorCSSMatchedRules* pseudo_match : match->pseudo_element_rules) {
      parent_pseudo_element_matches->emplace_back(
          protocol::CSS::PseudoElementMatches::create()
              .setPseudoType(InspectorDOMAgent::ProtocolPseudoElementType(
                  pseudo_match->pseudo_id))
              .setMatches(BuildArrayForMatchedRuleList(
                  pseudo_match->matched_rules, element, ghost_rules))
              .build());
      if (pseudo_match->view_transition_name) {
        parent_pseudo_element_matches->back()->setPseudoIdentifier(
            pseudo_match->view_transition_name);
      }
    }

    std::unique_ptr<protocol::CSS::InheritedPseudoElementMatches>
        inherited_pseudo_element_matches =
            protocol::CSS::InheritedPseudoElementMatches::create()
                .setPseudoElements(std::move(parent_pseudo_element_matches))
                .build();

    (*inherited_pseudo_id_matches)
        ->emplace_back(std::move(inherited_pseudo_element_matches));
  }

  // Get the index of the active position try fallback index.
  std::optional<size_t> successful_position_fallback_index;
  if (OutOfFlowData* out_of_flow_data = element->GetOutOfFlowData()) {
    successful_position_fallback_index =
        out_of_flow_data->GetNewSuccessfulPositionFallbackIndex();
    if (successful_position_fallback_index.has_value()) {
      *active_position_fallback_index =
          static_cast<int>(successful_position_fallback_index.value());
    }
  }
  *css_position_try_rules =
      PositionTryRulesForElement(element, successful_position_fallback_index);

  if (auto rule = FontPalettesForNode(*element)) {
    *css_font_palette_values_rule = std::move(rule);
  }

  auto* parent_layout_node = LayoutTreeBuilderTraversal::LayoutParent(*element);
  if (parent_layout_node) {
    if (int bound_node_id = dom_agent_->BoundNodeId(parent_layout_node)) {
      *parent_layout_node_id = bound_node_id;
    }
  }

  DocumentStyleSheets::iterator css_style_sheets_for_document_it =
      document_to_css_style_sheets_.find(&document);
  if (css_style_sheets_for_document_it != document_to_css_style_sheets_.end() &&
      resolver.MatchedRules()) {
    HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>
        function_hash_map;
    CollectReferencedFunctionRules(*css_style_sheets_for_document_it->value,
                                   *resolver.MatchedRules(), function_hash_map);
    if (!function_hash_map.empty()) {
      *css_function_rules =
          std::make_unique<protocol::Array<protocol::CSS::CSSFunctionRule>>();
      for (const auto& [scoped_name, rule] : function_hash_map) {
        (*css_function_rules)->emplace_back(BuildObjectForFunctionRule(rule));
      }
    }
  }
  return protocol::Response::Success();
}

template <class CSSRuleCollection>
static CSSKeyframesRule* FindKeyframesRule(CSSRuleCollection* css_rules,
                                           StyleRuleKeyframes* keyframes_rule) {
  if (!css_rules) {
    return nullptr;
  }

  CSSKeyframesRule* result = nullptr;
  for (unsigned j = 0; j < css_rules->length() && !result; ++j) {
    CSSRule* css_rule = css_rules->item(j);
    if (auto* css_style_rule = DynamicTo<CSSKeyframesRule>(css_rule)) {
      if (css_style_rule->Keyframes() == keyframes_rule)
        result = css_style_rule;
    } else if (auto* css_import_rule = DynamicTo<CSSImportRule>(css_rule)) {
      result = FindKeyframesRule(css_import_rule->styleSheet(), keyframes_rule);
    } else {
      result = FindKeyframesRule(css_rule->cssRules(), keyframes_rule);
    }
  }
  return result;
}

template <class CSSRuleCollection>
static CSSPositionTryRule* FindPositionTryRule(
    CSSRuleCollection* css_rules,
    StyleRulePositionTry* position_try_rule) {
  if (!css_rules) {
    return nullptr;
  }

  CSSPositionTryRule* result = nullptr;
  for (unsigned i = 0; i < css_rules->length() && !result; ++i) {
    CSSRule* css_rule = css_rules->item(i);
    if (auto* css_style_rule = DynamicTo<CSSPositionTryRule>(css_rule)) {
      if (css_style_rule->PositionTry() == position_try_rule) {
        result = css_style_rule;
      }
    } else if (auto* css_import_rule = DynamicTo<CSSImportRule>(css_rule)) {
      result =
          FindPositionTryRule(css_import_rule->styleSheet(), position_try_rule);
    } else {
      result = FindPositionTryRule(css_rule->cssRules(), position_try_rule);
    }
  }
  return result;
}

std::unique_ptr<protocol::Array<protocol::CSS::CSSPositionTryRule>>
InspectorCSSAgent::PositionTryRulesForElement(
    Element* element,
    std::optional<size_t> active_position_try_index) {
  Document& document = element->GetDocument();
  CHECK(!document.NeedsLayoutTreeUpdateForNode(*element));

  const ComputedStyle* style = element->EnsureComputedStyle();
  if (!style) {
    return nullptr;
  }

  const PositionTryFallbacks* position_try_fallbacks_ =
      style->GetPositionTryFallbacks();
  if (!position_try_fallbacks_) {
    return nullptr;
  }

  auto css_position_try_rules =
      std::make_unique<protocol::Array<protocol::CSS::CSSPositionTryRule>>();
  StyleResolver& style_resolver = document.GetStyleResolver();
  const HeapVector<PositionTryFallback>& fallbacks =
      position_try_fallbacks_->GetFallbacks();
  for (wtf_size_t i = 0; i < fallbacks.size(); ++i) {
    const PositionTryFallback& fallback = fallbacks[i];
    if (const ScopedCSSName* scoped_name = fallback.GetPositionTryName()) {
      const TreeScope* tree_scope = scoped_name->GetTreeScope();
      if (!tree_scope) {
        tree_scope = &document;
      }
      StyleRulePositionTry* position_try_rule =
          style_resolver.ResolvePositionTryRule(tree_scope,
                                                scoped_name->GetName());
      if (!position_try_rule) {
        continue;
      }
      // Find CSSOM wrapper from internal Style rule.
      DocumentStyleSheets::iterator css_style_sheets_for_document_it =
          document_to_css_style_sheets_.find(&document);
      if (css_style_sheets_for_document_it ==
          document_to_css_style_sheets_.end()) {
        continue;
      }
      bool is_active = active_position_try_index.has_value() &&
                       active_position_try_index.value() == i;
      for (CSSStyleSheet* style_sheet :
           *css_style_sheets_for_document_it->value) {
        if (CSSPositionTryRule* css_position_try_rule =
                FindPositionTryRule(style_sheet, position_try_rule)) {
          InspectorStyleSheet* inspector_style_sheet =
              BindStyleSheet(css_position_try_rule->parentStyleSheet());
          css_position_try_rules->emplace_back(
              inspector_style_sheet->BuildObjectForPositionTryRule(
                  css_position_try_rule, is_active));
          break;
        }
      }
    }
  }
  return css_position_try_rules;
}

// Find CSSRules of type T. The callback may return 'true' to keep searching,
// or 'false' to stop.
template <class T, class CSSRuleCollection, class CallbackFunc>
static void TraverseCSSRules(CSSRuleCollection* css_rules,
                             CallbackFunc callback) {
  if (!css_rules) {
    return;
  }
  for (unsigned j = 0; j < css_rules->length(); ++j) {
    CSSRule* css_rule = css_rules->item(j);
    if (auto* typed_css_rule = DynamicTo<T>(css_rule)) {
      bool keep_searching = callback(*typed_css_rule);
      if (!keep_searching) {
        return;
      }
    } else if (auto* css_import_rule = DynamicTo<CSSImportRule>(css_rule)) {
      TraverseCSSRules<T>(css_import_rule->styleSheet(), callback);
    } else {
      TraverseCSSRules<T>(css_rule->cssRules(), callback);
    }
  }
}

template <class CSSRuleCollection>
static CSSPropertyRule* FindPropertyRule(CSSRuleCollection* css_rules,
                                         StyleRuleProperty* property_rule) {
  CSSPropertyRule* result = nullptr;
  TraverseCSSRules<CSSPropertyRule>(
      css_rules, [&result, property_rule](CSSPropertyRule& css_rule) {
        if (css_rule.Property() == property_rule) {
          result = &css_rule;
          return false;
        }
        return true;  // Keep searching.
      });
  return result;
}

std::unique_ptr<protocol::CSS::CSSPropertyRegistration>
BuildObjectForPropertyRegistration(const AtomicString& name,
                                   const PropertyRegistration& registration) {
  auto css_property_registration =
      protocol::CSS::CSSPropertyRegistration::create()
          .setPropertyName(name)
          .setInherits(registration.Inherits())
          .setSyntax(registration.Syntax().ToString())
          .build();
  if (registration.Initial()) {
    css_property_registration->setInitialValue(
        protocol::CSS::Value::create()
            .setText(registration.Initial()->CssText())
            .build());
  }
  return css_property_registration;
}

std::pair<
    std::unique_ptr<protocol::Array<protocol::CSS::CSSPropertyRule>>,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSPropertyRegistration>>>
InspectorCSSAgent::CustomPropertiesForNode(Element* element) {
  auto result = std::make_pair(
      std::make_unique<protocol::Array<protocol::CSS::CSSPropertyRule>>(),
      std::make_unique<
          protocol::Array<protocol::CSS::CSSPropertyRegistration>>());
  Document& document = element->GetDocument();
  DCHECK(!document.NeedsLayoutTreeUpdateForNode(*element));

  auto style_sheets = document_to_css_style_sheets_.find(&document);
  if (style_sheets == document_to_css_style_sheets_.end()) {
    return result;
  }

  const ComputedStyle* style = element->EnsureComputedStyle();
  if (!style) {
    return result;
  }

  for (const AtomicString& var_name : style->GetVariableNames()) {
    const auto* registration =
        PropertyRegistration::From(document.GetExecutionContext(), var_name);
    if (!registration) {
      continue;
    }

    if (StyleRuleProperty* rule = registration->PropertyRule()) {
      // Find CSSOM wrapper.
      CSSPropertyRule* property_rule = nullptr;
      for (CSSStyleSheet* style_sheet : *style_sheets->value) {
        property_rule = FindPropertyRule(style_sheet, rule);
        if (property_rule)
          break;
      }
      if (property_rule) {
        // @property
        InspectorStyleSheet* inspector_style_sheet =
            BindStyleSheet(property_rule->parentStyleSheet());
        result.first->push_back(
            inspector_style_sheet->BuildObjectForPropertyRule(property_rule));
      }
      // If the property_rule wasn't found, just ignore ignore it.
    } else {
      // CSS.registerProperty
      result.second->push_back(
          BuildObjectForPropertyRegistration(var_name, *registration));
    }
  }

  return result;
}

template <class CSSRuleCollection>
static CSSFontPaletteValuesRule* FindFontPaletteValuesRule(
    CSSRuleCollection* css_rules,
    StyleRuleFontPaletteValues* values_rule) {
  if (!css_rules) {
    return nullptr;
  }

  CSSFontPaletteValuesRule* result = nullptr;
  for (unsigned j = 0; j < css_rules->length() && !result; ++j) {
    CSSRule* css_rule = css_rules->item(j);
    if (auto* css_style_rule = DynamicTo<CSSFontPaletteValuesRule>(css_rule)) {
      if (css_style_rule->FontPaletteValues() == values_rule)
        result = css_style_rule;
    } else if (auto* css_import_rule = DynamicTo<CSSImportRule>(css_rule)) {
      result =
          FindFontPaletteValuesRule(css_import_rule->styleSheet(), values_rule);
    } else {
      result = FindFontPaletteValuesRule(css_rule->cssRules(), values_rule);
    }
  }
  return result;
}

std::unique_ptr<protocol::CSS::CSSFontPaletteValuesRule>
InspectorCSSAgent::FontPalettesForNode(Element& element) {
  const ComputedStyle* style = element.EnsureComputedStyle();
  const FontPalette* palette = style ? style->GetFontPalette() : nullptr;
  if (!palette || !palette->IsCustomPalette()) {
    return {};
  }
  Document& document = element.GetDocument();
  StyleRuleFontPaletteValues* rule =
      document.GetStyleEngine().FontPaletteValuesForNameAndFamily(
          palette->GetPaletteValuesName(),
          style->GetFontDescription().Family().FamilyName());
  if (!rule) {
    return {};
  }

  auto style_sheets = document_to_css_style_sheets_.find(&document);
  if (style_sheets == document_to_css_style_sheets_.end()) {
    return {};
  }

  // Find CSSOM wrapper.
  CSSFontPaletteValuesRule* values_rule = nullptr;
  for (CSSStyleSheet* style_sheet : *style_sheets->value) {
    values_rule = FindFontPaletteValuesRule(style_sheet, rule);
    if (values_rule)
      break;
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(values_rule->parentStyleSheet());
  return inspector_style_sheet->BuildObjectForFontPaletteValuesRule(
      values_rule);
}

CSSKeyframesRule*
InspectorCSSAgent::FindKeyframesRuleFromUAViewTransitionStylesheet(
    Element* element,
    StyleRuleKeyframes* keyframes_style_rule) {
  // This function should only be called for transition pseudo-elements.
  CHECK(IsTransitionPseudoElement(element->GetPseudoId()));
  auto* transition = ViewTransitionUtils::GetTransition(element->GetDocument());

  // There must be a transition and an active UAStyleSheet for the
  // transition when the queried element is a transition pseudo-element.
  CHECK(transition && transition->UAStyleSheet());

  if (!user_agent_view_transition_style_sheet_) {
    // Save the previous view transition style sheet.
    user_agent_view_transition_style_sheet_ = transition->UAStyleSheet();
  } else if (user_agent_view_transition_style_sheet_ !=
             transition->UAStyleSheet()) {
    // If the view transition stylesheet is invalidated
    // unbind the previous inspector stylesheet.
    user_agent_view_transition_style_sheet_ = transition->UAStyleSheet();
    auto previous_css_style_sheet_it =
        css_style_sheet_to_inspector_style_sheet_.find(
            user_agent_view_transition_style_sheet_);
    if (previous_css_style_sheet_it !=
        css_style_sheet_to_inspector_style_sheet_.end()) {
      UnbindStyleSheet(previous_css_style_sheet_it->value);
    }
  }

  for (wtf_size_t i = 0; i < user_agent_view_transition_style_sheet_->length();
       i++) {
    CSSKeyframesRule* css_keyframes_rule_from_stylesheet =
        DynamicTo<CSSKeyframesRule>(
            user_agent_view_transition_style_sheet_->ItemInternal(i));
    if (css_keyframes_rule_from_stylesheet &&
        css_keyframes_rule_from_stylesheet->name() ==
            keyframes_style_rule->GetName()) {
      return css_keyframes_rule_from_stylesheet;
    }
  }

  return nullptr;
}

CSSKeyframesRule* InspectorCSSAgent::FindCSSOMWrapperForKeyframesRule(
    Element* element,
    StyleRuleKeyframes* keyframes_style_rule) {
  Document& document = element->GetDocument();
  // There might be that there aren't any active stylesheets for the document
  // which mean the document_to_css_style_sheets_ map won't contain the
  // entry for the document. So, we first check whether there are registered
  // stylesheets for the document.
  if (document_to_css_style_sheets_.Contains(&document)) {
    for (CSSStyleSheet* style_sheet :
         *document_to_css_style_sheets_.at(&document)) {
      CSSKeyframesRule* css_keyframes_rule =
          FindKeyframesRule(style_sheet, keyframes_style_rule);
      if (css_keyframes_rule) {
        return css_keyframes_rule;
      }
    }
  }

  if (IsTransitionPseudoElement(element->GetPseudoId())) {
    return FindKeyframesRuleFromUAViewTransitionStylesheet(
        element, keyframes_style_rule);
  }

  return nullptr;
}

std::unique_ptr<protocol::Array<protocol::CSS::CSSKeyframesRule>>
InspectorCSSAgent::AnimationsForNode(Element* element,
                                     Element* animating_element) {
  auto css_keyframes_rules =
      std::make_unique<protocol::Array<protocol::CSS::CSSKeyframesRule>>();
  Document& document = element->GetDocument();
  DCHECK(!document.NeedsLayoutTreeUpdateForNode(*element));
  // We want to match the animation name of the animating element not the parent
  // element's animation names for pseudo-elements. When the `element` is a
  // non-pseudo-element then `animating_element` and the `element` are the same.
  const ComputedStyle* style = animating_element->EnsureComputedStyle();
  if (!style)
    return css_keyframes_rules;
  const CSSAnimationData* animation_data = style->Animations();
  StyleResolver& style_resolver = document.GetStyleResolver();
  for (wtf_size_t i = 0;
       animation_data && i < animation_data->NameList().size(); ++i) {
    AtomicString animation_name(animation_data->NameList()[i]);
    if (animation_name == CSSAnimationData::InitialName())
      continue;

    StyleRuleKeyframes* keyframes_rule =
        style_resolver
            .FindKeyframesRule(element, animating_element, animation_name)
            .rule;
    if (!keyframes_rule) {
      continue;
    }

    CSSKeyframesRule* css_keyframes_rule =
        FindCSSOMWrapperForKeyframesRule(animating_element, keyframes_rule);
    if (!css_keyframes_rule) {
      continue;
    }

    auto keyframes =
        std::make_unique<protocol::Array<protocol::CSS::CSSKeyframeRule>>();
    for (unsigned j = 0; j < css_keyframes_rule->length(); ++j) {
      InspectorStyleSheet* inspector_style_sheet =
          BindStyleSheet(css_keyframes_rule->parentStyleSheet());
      keyframes->emplace_back(inspector_style_sheet->BuildObjectForKeyframeRule(
          css_keyframes_rule->Item(j), element));
    }

    InspectorStyleSheet* inspector_style_sheet =
        BindStyleSheet(css_keyframes_rule->parentStyleSheet());
    CSSRuleSourceData* source_data =
        inspector_style_sheet->SourceDataForRule(css_keyframes_rule);
    std::unique_ptr<protocol::CSS::Value> name =
        protocol::CSS::Value::create()
            .setText(css_keyframes_rule->name())
            .build();
    if (source_data) {
      name->setRange(inspector_style_sheet->BuildSourceRangeObject(
          source_data->rule_header_range));
    }
    css_keyframes_rules->emplace_back(protocol::CSS::CSSKeyframesRule::create()
                                          .setAnimationName(std::move(name))
                                          .setKeyframes(std::move(keyframes))
                                          .build());
  }
  return css_keyframes_rules;
}

protocol::Response InspectorCSSAgent::getInlineStylesForNode(
    int node_id,
    std::unique_ptr<protocol::CSS::CSSStyle>* inline_style,
    std::unique_ptr<protocol::CSS::CSSStyle>* attributes_style) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess())
    return response;
  Element* element = nullptr;
  response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess())
    return response;

  InspectorStyleSheetForInlineStyle* style_sheet =
      AsInspectorStyleSheet(element);
  if (!style_sheet)
    return protocol::Response::ServerError("Element is not a style sheet");

  *inline_style = style_sheet->BuildObjectForStyle(element->style(), element);
  *attributes_style = BuildObjectForAttributesStyle(element);
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::getComputedStyleForNode(
    int node_id,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSComputedStyleProperty>>*
        style) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess())
    return response;
  Node* node = nullptr;
  response = dom_agent_->AssertNode(node_id, node);
  if (!response.IsSuccess())
    return response;
  if (!node->ownerDocument()) {
    return protocol::Response::ServerError(
        "Node does not have an owner document");
  }
  Element* element = DynamicTo<Element>(node);
  if (!element && !node->IsDocumentFragment()) {
    element = FlatTreeTraversal::ParentElement(*node);
  }
  if (!element) {
    return protocol::Response::ServerError(
        "Node is not an element and does not have a parent element");
  }

  TRACE_EVENT1("devtools", "InspectorCSSAgent::getComputedStyleForNode", "node",
               element->DebugName());
  auto* computed_style_info =
      MakeGarbageCollected<CSSComputedStyleDeclaration>(element, true);
  CSSComputedStyleDeclaration::ScopedCleanStyleForAllProperties
      clean_style_scope(computed_style_info);
  *style = std::make_unique<
      protocol::Array<protocol::CSS::CSSComputedStyleProperty>>();
  for (CSSPropertyID property_id : CSSPropertyIDList()) {
    const CSSProperty& property_class =
        CSSProperty::Get(ResolveCSSPropertyID(property_id));
    if (!property_class.IsWebExposed(element->GetExecutionContext()) ||
        property_class.IsShorthand() || !property_class.IsProperty()) {
      continue;
    }
    (*style)->emplace_back(
        protocol::CSS::CSSComputedStyleProperty::create()
            .setName(property_class.GetPropertyNameString())
            .setValue(computed_style_info->GetPropertyValue(property_id))
            .build());
  }

  for (const auto& it : computed_style_info->GetVariables()) {
    (*style)->emplace_back(protocol::CSS::CSSComputedStyleProperty::create()
                               .setName(it.key)
                               .setValue(it.value->CssText())
                               .build());
  }
  return protocol::Response::Success();
}

// Resolve percentages in the context of the given element values for margin,
// padding, inset, sizing properties. Return `original_value` string if
// there was an error during percentage resolution. Returns null string if the
// value doesn't have percentages or `property_name` is not margin,
// padding, inset or sizing property.
String InspectorCSSAgent::ResolvePercentagesValues(
    Element* element,
    CSSPropertyName property_name,
    const CSSValue* parsed_value,
    const String& original_value) {
  CHECK(!element->GetDocument().View()->NeedsLayout());
  if (!IsMarginPaddingProperty(property_name) &&
      !IsInsetProperty(property_name) && !IsSizingProperty(property_name)) {
    return g_null_atom;
  }
  if (!parsed_value) {
    return g_null_atom;
  }
  auto* primitive_value = DynamicTo<CSSPrimitiveValue>(parsed_value);
  if (!primitive_value) {
    return g_null_atom;
  }

  ElementResolveContext element_resolve_context(*element);
  CSSToLengthConversionData::Flags ignored_flags = 0;
  CSSToLengthConversionData length_conversion_data(
      element->ComputedStyleRef(), element_resolve_context.ParentStyle(),
      element_resolve_context.RootElementStyle(),
      CSSToLengthConversionData::ViewportSize(),
      CSSToLengthConversionData::ContainerSizes(),
      CSSToLengthConversionData::AnchorData(),
      element->GetComputedStyle()->EffectiveZoom(), ignored_flags, element);
  Length length_value =
      primitive_value->ConvertToLength(length_conversion_data);
  if (!length_value.HasPercent()) {
    return g_null_atom;
  }

  if (auto* box = DynamicTo<LayoutBox>(element->GetLayoutObject())) {
    if (!box->GetLayoutResults().size()) {
      return original_value;
    }
    // Using the first LayoutResult is okay, as all fragments have the same
    // percentage resolution sizes.
    const ConstraintSpace& constraint_space =
        box->GetLayoutResult(0)->GetConstraintSpaceForCaching();
    LogicalSize percentage_resolution_size =
        IsMarginPaddingProperty(property_name)
            ? constraint_space.MarginPaddingPercentageResolutionSize()
            : constraint_space.PercentageResolutionSize();
    LayoutUnit percentage_resolution_value = PickInlineOrBlockSize(
        percentage_resolution_size, property_name.Id(),
        element->GetComputedStyle()->GetWritingDirection());
    if (percentage_resolution_value == kIndefiniteSize) {
      return original_value;
    }
    LayoutUnit resolved_percentage_value =
        MinimumValueForLength(length_value, percentage_resolution_value);

    StringBuilder builder;
    builder.AppendNumber(static_cast<double>(resolved_percentage_value));
    builder.Append("px");
    return builder.ToString();
  }

  return original_value;
}

protocol::Response InspectorCSSAgent::resolveValues(
    std::unique_ptr<protocol::Array<String>> values,
    int node_id,
    std::optional<String> property_name_str,
    std::optional<protocol::DOM::PseudoType> pseudo_type,
    std::optional<String> pseudo_identifier,
    std::unique_ptr<protocol::Array<String>>* results) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess()) {
    return response;
  }

  Node* node = nullptr;
  response = dom_agent_->AssertNode(node_id, node);
  if (!response.IsSuccess()) {
    return response;
  }

  Element* element = DynamicTo<Element>(node);
  if (!element) {
    return protocol::Response::ServerError("Node is not an element.");
  }

  Document& document = element->GetDocument();
  document.UpdateStyleAndLayoutForNode(element,
                                       DocumentUpdateReason::kInspector);
  CSSParserContext* parser_context =
      MakeGarbageCollected<CSSParserContext>(document);
  ExecutionContext* execution_context = element->GetExecutionContext();

  if (pseudo_type.has_value()) {
    AtomicString view_transition_name = pseudo_identifier.has_value()
                                            ? AtomicString(*pseudo_identifier)
                                            : g_null_atom;
    element = element->GetStyledPseudoElement(
        InspectorDOMAgent::ProtocolPseudoTypeToPseudoId(*pseudo_type),
        view_transition_name);
    if (!element) {
      return protocol::Response::ServerError(
          "Could not retrieve pseudo element.");
    }
  }

  if (!element->GetComputedStyle()) {
    return protocol::Response::ServerError(
        "Computed style of element is null.");
  }

  const AtomicString custom_property_name(
      ("--" + base::UnguessableToken::Create().ToString()).c_str());
  std::optional<AutoRegistration> auto_registration;
  CSSSyntaxDefinition syntax_definition = CreateCombinedSyntax();
  PropertyRegistration* property_registration =
      MakeGarbageCollected<PropertyRegistration>(
          custom_property_name, syntax_definition, /* inherits */ false,
          /* initial */ nullptr);
  // Temporary register property with combined syntax.
  auto_registration.emplace(document, custom_property_name,
                            *property_registration);
  CustomProperty temporary_custom_property(custom_property_name, document);

  std::optional<CSSPropertyName> property_name =
      CSSPropertyName(custom_property_name);
  if (property_name_str.has_value()) {
    property_name =
        CSSPropertyName::From(execution_context, *property_name_str);
    if (!property_name.has_value()) {
      return protocol::Response::ServerError(
          "Could not resolve property name.");
    }
  }

  if (CSSProperty::Get(property_name->Id()).IsShorthand()) {
    return protocol::Response::ServerError(
        "Property name should not be a shorthand.");
  }

  *results = std::make_unique<protocol::Array<String>>();
  for (auto value : *values) {
    const CSSValue* computed_value = nullptr;
    const CSSValue* parsed_value =
        CSSParser::ParseSingleValue(property_name->Id(), value, parser_context);
    if (parsed_value) {
      computed_value =
          StyleResolver::ComputeValue(element, *property_name, *parsed_value);
      if (String resolved_value = ResolvePercentagesValues(
              element, *property_name, computed_value, value)) {
        (*results)->emplace_back(resolved_value);
        continue;
      }
    } else {
      auto local_context = CSSParserLocalContext();
      parsed_value = temporary_custom_property.Parse(value, *parser_context,
                                                     local_context);
      if (!parsed_value) {
        (*results)->emplace_back(value);
        continue;
      }
      computed_value = StyleResolver::ComputeValue(
          element, CSSPropertyName(custom_property_name), *parsed_value);
    }

    if (!computed_value) {
      (*results)->emplace_back(value);
      continue;
    }

    (*results)->emplace_back(computed_value->CssText());
  }

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::getLonghandProperties(
    const String& shorthand_name,
    const String& value,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSProperty>>*
        longhand_properties) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess()) {
    return response;
  }

  CSSPropertyID property_id =
      CssPropertyID(/*execution_context=*/nullptr, shorthand_name);
  if (!IsValidCSSPropertyID(property_id)) {
    return protocol::Response::ServerError("Invalid shorthandName");
  }
  const CSSProperty& property = CSSProperty::Get(property_id);

  CSSParserTokenStream stream(value);

  const CSSParserContext* parser_context =
      MakeGarbageCollected<CSSParserContext>(kHTMLStandardMode,
                                             SecureContextMode::kSecureContext);
  const auto local_context =
      CSSParserLocalContext().WithCurrentShorthand(property.PropertyID());

  HeapVector<CSSPropertyValue, 64> css_longhand_properties;
  const auto* shorthand = DynamicTo<Shorthand>(property);
  if (!shorthand) {
    return protocol::Response::ServerError("Invalid shorthandName");
  }
  bool res = shorthand->ParseShorthand(false, stream, *parser_context,
                                       local_context, css_longhand_properties);
  if (!res) {
    return protocol::Response::ServerError("Error parsing shorthand.");
  }

  *longhand_properties =
      std::make_unique<protocol::Array<protocol::CSS::CSSProperty>>();
  for (auto longhand_property : css_longhand_properties) {
    std::unique_ptr<protocol::CSS::CSSProperty> protocol_longhand =
        protocol::CSS::CSSProperty::create()
            .setName(longhand_property.Name().ToAtomicString())
            .setValue(longhand_property.Value().CssText())
            .build();
    (*longhand_properties)->emplace_back(std::move(protocol_longhand));
  }

  return protocol::Response::Success();
}

void InspectorCSSAgent::CollectPlatformFontsForLayoutObject(
    LayoutObject* layout_object,
    HashMap<std::pair<int, String>, std::pair<int, String>>* font_stats,
    unsigned descendants_depth) {
  if (!layout_object->IsText()) {
    if (!descendants_depth)
      return;

    // Skip recursing inside a display-locked tree.
    if (DisplayLockUtilities::LockedInclusiveAncestorPreventingPaint(
            *layout_object)) {
      return;
    }

    if (!layout_object->IsAnonymous())
      --descendants_depth;
    for (LayoutObject* child = layout_object->SlowFirstChild(); child;
         child = child->NextSibling()) {
      CollectPlatformFontsForLayoutObject(child, font_stats, descendants_depth);
    }
    return;
  }

  // Don't gather text on a display-locked tree.
  if (DisplayLockUtilities::LockedAncestorPreventingPaint(*layout_object))
    return;

  FontCachePurgePreventer preventer;
  DCHECK(layout_object->IsInLayoutNGInlineFormattingContext());
  InlineCursor cursor;
  cursor.MoveTo(*layout_object);
  for (; cursor; cursor.MoveToNextForSameLayoutObject()) {
    const ShapeResultView* shape_result = cursor.Current().TextShapeResult();
    if (!shape_result) {
      continue;
    }
    HeapVector<ShapeResult::RunFontData> run_font_data_list;
    ClearCollectionScope clear_scope(&run_font_data_list);
    shape_result->GetRunFontData(&run_font_data_list);
    CollectPlatformFontsFromRunFontDataList(run_font_data_list, font_stats);
  }
}

protocol::Response InspectorCSSAgent::getPlatformFontsForNode(
    int node_id,
    std::unique_ptr<protocol::Array<protocol::CSS::PlatformFontUsage>>*
        platform_fonts) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess())
    return response;
  Node* node = nullptr;
  response = dom_agent_->AssertNode(node_id, node);
  if (!response.IsSuccess())
    return response;

  // Key: {isCustomFont, postscript_name}
  // Value: {glyph_count (which accumulates), family_name}
  HashMap<std::pair<int, String>, std::pair<int, String>> font_stats;
  LayoutObject* root = node->GetLayoutObject();
  if (root) {
    // Iterate upto two layers deep.
    const unsigned descendants_depth = 2;
    CollectPlatformFontsForLayoutObject(root, &font_stats, descendants_depth);
  }
  *platform_fonts =
      std::make_unique<protocol::Array<protocol::CSS::PlatformFontUsage>>();
  for (auto& font : font_stats) {
    std::pair<int, String>& font_description = font.key;
    std::pair<int, String>& font_value = font.value;
    bool is_custom_font = font_description.first == 1;
    (*platform_fonts)
        ->emplace_back(protocol::CSS::PlatformFontUsage::create()
                           .setFamilyName(font_value.second)
                           .setPostScriptName(font_description.second)
                           .setIsCustomFont(is_custom_font)
                           .setGlyphCount(font_value.first)
                           .build());
  }
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::getStyleSheetText(
    const String& style_sheet_id,
    String* result) {
  InspectorStyleSheetBase* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;

  inspector_style_sheet->GetText(result);
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::collectClassNames(
    const String& style_sheet_id,
    std::unique_ptr<protocol::Array<String>>* class_names) {
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  *class_names = inspector_style_sheet->CollectClassNames();
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::setStyleSheetText(
    const String& style_sheet_id,
    const String& text,
    std::optional<String>* source_map_url) {
  FrontendOperationScope scope;
  InspectorStyleSheetBase* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  dom_agent_->History()->Perform(MakeGarbageCollected<SetStyleSheetTextAction>(
                                     inspector_style_sheet, text),
                                 exception_state);
  response = InspectorDOMAgent::ToResponse(exception_state);
  if (!response.IsSuccess())
    return response;
  if (!inspector_style_sheet->SourceMapURL().empty())
    *source_map_url = inspector_style_sheet->SourceMapURL();
  return protocol::Response::Success();
}

static protocol::Response JsonRangeToSourceRange(
    InspectorStyleSheetBase* inspector_style_sheet,
    protocol::CSS::SourceRange* range,
    SourceRange* source_range) {
  if (range->getStartLine() < 0) {
    return protocol::Response::ServerError(
        "range.startLine must be a non-negative integer");
  }
  if (range->getStartColumn() < 0) {
    return protocol::Response::ServerError(
        "range.startColumn must be a non-negative integer");
  }
  if (range->getEndLine() < 0) {
    return protocol::Response::ServerError(
        "range.endLine must be a non-negative integer");
  }
  if (range->getEndColumn() < 0) {
    return protocol::Response::ServerError(
        "range.endColumn must be a non-negative integer");
  }

  unsigned start_offset = 0;
  unsigned end_offset = 0;
  bool success =
      inspector_style_sheet->LineNumberAndColumnToOffset(
          range->getStartLine(), range->getStartColumn(), &start_offset) &&
      inspector_style_sheet->LineNumberAndColumnToOffset(
          range->getEndLine(), range->getEndColumn(), &end_offset);
  if (!success)
    return protocol::Response::ServerError("Specified range is out of bounds");

  if (start_offset > end_offset) {
    return protocol::Response::ServerError(
        "Range start must not succeed its end");
  }
  source_range->start = start_offset;
  source_range->end = end_offset;
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::setRuleSelector(
    const String& style_sheet_id,
    std::unique_ptr<protocol::CSS::SourceRange> range,
    const String& selector,
    std::unique_ptr<protocol::CSS::SelectorList>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange selector_range;
  response = JsonRangeToSourceRange(inspector_style_sheet, range.get(),
                                    &selector_range);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetRuleSelector, inspector_style_sheet, selector_range,
      selector);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    CSSStyleRule* rule = InspectorCSSAgent::AsCSSStyleRule(action->TakeRule());
    inspector_style_sheet = InspectorStyleSheetForRule(rule);
    if (!inspector_style_sheet) {
      return protocol::Response::ServerError(
          "Failed to get inspector style sheet for rule.");
    }
    *result = inspector_style_sheet->BuildObjectForSelectorList(rule);
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::setPropertyRulePropertyName(
    const String& in_styleSheetId,
    std::unique_ptr<protocol::CSS::SourceRange> in_range,
    const String& in_propertyName,
    std::unique_ptr<protocol::CSS::Value>* out_propertyName) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(in_styleSheetId, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange name_range;
  response = JsonRangeToSourceRange(inspector_style_sheet, in_range.get(),
                                    &name_range);
  if (!response.IsSuccess())
    return response;
  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetPropertyName, inspector_style_sheet, name_range,
      in_propertyName);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    auto* rule = To<CSSPropertyRule>(action->TakeRule());
    inspector_style_sheet = BindStyleSheet(rule->parentStyleSheet());
    if (!inspector_style_sheet) {
      return protocol::Response::ServerError(
          "Failed to get inspector style sheet for rule.");
    }
    CSSRuleSourceData* source_data =
        inspector_style_sheet->SourceDataForRule(rule);
    *out_propertyName =
        protocol::CSS::Value::create()
            .setText(rule->name())
            .setRange(inspector_style_sheet->BuildSourceRangeObject(
                source_data->rule_header_range))
            .build();
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::setKeyframeKey(
    const String& style_sheet_id,
    std::unique_ptr<protocol::CSS::SourceRange> range,
    const String& key_text,
    std::unique_ptr<protocol::CSS::Value>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange key_range;
  response =
      JsonRangeToSourceRange(inspector_style_sheet, range.get(), &key_range);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetKeyframeKey, inspector_style_sheet, key_range,
      key_text);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    auto* rule = To<CSSKeyframeRule>(action->TakeRule());
    inspector_style_sheet = BindStyleSheet(rule->parentStyleSheet());
    if (!inspector_style_sheet) {
      return protocol::Response::ServerError(
          "Failed to get inspector style sheet for rule.");
    }
    CSSRuleSourceData* source_data =
        inspector_style_sheet->SourceDataForRule(rule);
    *result = protocol::CSS::Value::create()
                  .setText(rule->keyText())
                  .setRange(inspector_style_sheet->BuildSourceRangeObject(
                      source_data->rule_header_range))
                  .build();
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::MultipleStyleTextsActions(
    std::unique_ptr<protocol::Array<protocol::CSS::StyleDeclarationEdit>> edits,
    HeapVector<Member<StyleSheetAction>>* actions) {
  size_t n = edits->size();
  if (n == 0)
    return protocol::Response::ServerError("Edits should not be empty");

  for (size_t i = 0; i < n; ++i) {
    protocol::CSS::StyleDeclarationEdit* edit = (*edits)[i].get();
    InspectorStyleSheetBase* inspector_style_sheet = nullptr;
    protocol::Response response =
        AssertStyleSheetForId(edit->getStyleSheetId(), inspector_style_sheet);
    if (!response.IsSuccess()) {
      return protocol::Response::ServerError(
          String::Format("StyleSheet not found for edit #%zu of %zu", i + 1, n)
              .Utf8());
    }

    SourceRange range;
    response =
        JsonRangeToSourceRange(inspector_style_sheet, edit->getRange(), &range);
    if (!response.IsSuccess())
      return response;

    if (inspector_style_sheet->IsInlineStyle()) {
      InspectorStyleSheetForInlineStyle* inline_style_sheet =
          static_cast<InspectorStyleSheetForInlineStyle*>(
              inspector_style_sheet);
      SetElementStyleAction* action =
          MakeGarbageCollected<SetElementStyleAction>(inline_style_sheet,
                                                      edit->getText());
      actions->push_back(action);
    } else {
      ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
          ModifyRuleAction::kSetStyleText,
          static_cast<InspectorStyleSheet*>(inspector_style_sheet), range,
          edit->getText());
      actions->push_back(action);
    }
  }
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::setStyleTexts(
    std::unique_ptr<protocol::Array<protocol::CSS::StyleDeclarationEdit>> edits,
    std::optional<int> node_for_property_syntax_validation,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSStyle>>* result) {
  FrontendOperationScope scope;
  HeapVector<Member<StyleSheetAction>> actions;
  protocol::Response response =
      MultipleStyleTextsActions(std::move(edits), &actions);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;

  Element* element = nullptr;
  if (node_for_property_syntax_validation.has_value()) {
    response = dom_agent_->AssertElement(
        node_for_property_syntax_validation.value(), element);
    if (!response.IsSuccess())
      return response;
  }

  int n = actions.size();
  auto serialized_styles =
      std::make_unique<protocol::Array<protocol::CSS::CSSStyle>>();
  for (int i = 0; i < n; ++i) {
    Member<StyleSheetAction> action = actions.at(i);
    bool success = action->Perform(exception_state);
    if (!success) {
      for (int j = i - 1; j >= 0; --j) {
        Member<StyleSheetAction> revert = actions.at(j);
        DummyExceptionStateForTesting undo_exception_state;
        revert->Undo(undo_exception_state);
        DCHECK(!undo_exception_state.HadException());
      }
      return protocol::Response::ServerError(
          String::Format("Failed applying edit #%d: ", i).Utf8() +
          InspectorDOMAgent::ToResponse(exception_state).Message());
    }
  }

  if (element) {
    element->GetDocument().UpdateStyleAndLayoutForNode(
        element, DocumentUpdateReason::kInspector);
  }
  for (int i = 0; i < n; ++i) {
    Member<StyleSheetAction> action = actions.at(i);
    serialized_styles->emplace_back(action->TakeSerializedStyle(element));
  }

  for (int i = 0; i < n; ++i) {
    Member<StyleSheetAction> action = actions.at(i);
    dom_agent_->History()->AppendPerformedAction(action);
  }
  *result = std::move(serialized_styles);
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::SetStyleText(
    InspectorStyleSheetBase* inspector_style_sheet,
    const SourceRange& range,
    const String& text,
    CSSStyleDeclaration*& result) {
  DummyExceptionStateForTesting exception_state;
  if (inspector_style_sheet->IsInlineStyle()) {
    InspectorStyleSheetForInlineStyle* inline_style_sheet =
        static_cast<InspectorStyleSheetForInlineStyle*>(inspector_style_sheet);
    SetElementStyleAction* action =
        MakeGarbageCollected<SetElementStyleAction>(inline_style_sheet, text);
    bool success = dom_agent_->History()->Perform(action, exception_state);
    if (success) {
      result = inline_style_sheet->InlineStyle();
      return protocol::Response::Success();
    }
  } else {
    ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
        ModifyRuleAction::kSetStyleText,
        static_cast<InspectorStyleSheet*>(inspector_style_sheet), range, text);
    bool success = dom_agent_->History()->Perform(action, exception_state);
    if (success) {
      CSSRule* rule = action->TakeRule();
      if (auto* style_rule = DynamicTo<CSSStyleRule>(rule)) {
        result = style_rule->style();
        return protocol::Response::Success();
      }
      if (auto* keyframe_rule = DynamicTo<CSSKeyframeRule>(rule)) {
        result = keyframe_rule->style();
        return protocol::Response::Success();
      }
    }
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::setMediaText(
    const String& style_sheet_id,
    std::unique_ptr<protocol::CSS::SourceRange> range,
    const String& text,
    std::unique_ptr<protocol::CSS::CSSMedia>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange text_range;
  response =
      JsonRangeToSourceRange(inspector_style_sheet, range.get(), &text_range);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetMediaRuleText, inspector_style_sheet, text_range,
      text);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    CSSMediaRule* rule = InspectorCSSAgent::AsCSSMediaRule(action->TakeRule());
    String source_url = rule->parentStyleSheet()->Contents()->BaseURL();
    if (source_url.empty()) {
      source_url = InspectorDOMAgent::DocumentURLString(
          rule->parentStyleSheet()->OwnerDocument());
    }
    *result = BuildMediaObject(rule->media(), kMediaListSourceMediaRule,
                               source_url, rule->parentStyleSheet());
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::setContainerQueryText(
    const String& style_sheet_id,
    std::unique_ptr<protocol::CSS::SourceRange> range,
    const String& text,
    std::unique_ptr<protocol::CSS::CSSContainerQuery>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange text_range;
  response =
      JsonRangeToSourceRange(inspector_style_sheet, range.get(), &text_range);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetContainerRuleText, inspector_style_sheet,
      text_range, text);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    CSSContainerRule* rule =
        InspectorCSSAgent::AsCSSContainerRule(action->TakeRule());
    *result = BuildContainerQueryObject(rule);
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::setScopeText(
    const String& style_sheet_id,
    std::unique_ptr<protocol::CSS::SourceRange> range,
    const String& text,
    std::unique_ptr<protocol::CSS::CSSScope>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange text_range;
  response =
      JsonRangeToSourceRange(inspector_style_sheet, range.get(), &text_range);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetScopeRuleText, inspector_style_sheet, text_range,
      text);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    CSSScopeRule* rule = InspectorCSSAgent::AsCSSScopeRule(action->TakeRule());
    *result = BuildScopeObject(rule);
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::setSupportsText(
    const String& style_sheet_id,
    std::unique_ptr<protocol::CSS::SourceRange> range,
    const String& text,
    std::unique_ptr<protocol::CSS::CSSSupports>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;
  SourceRange text_range;
  response =
      JsonRangeToSourceRange(inspector_style_sheet, range.get(), &text_range);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  ModifyRuleAction* action = MakeGarbageCollected<ModifyRuleAction>(
      ModifyRuleAction::kSetSupportsRuleText, inspector_style_sheet, text_range,
      text);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (success) {
    CSSSupportsRule* rule =
        InspectorCSSAgent::AsCSSSupportsRule(action->TakeRule());
    *result = BuildSupportsObject(rule);
  }
  return InspectorDOMAgent::ToResponse(exception_state);
}

protocol::Response InspectorCSSAgent::createStyleSheet(
    const String& frame_id,
    std::optional<bool> force,
    protocol::CSS::StyleSheetId* out_style_sheet_id) {
  LocalFrame* frame =
      IdentifiersFactory::FrameById(inspected_frames_, frame_id);
  if (!frame)
    return protocol::Response::ServerError("Frame not found");

  Document* document = frame->GetDocument();
  if (!document)
    return protocol::Response::ServerError("Frame does not have a document");

  InspectorStyleSheet* inspector_style_sheet =
      CreateViaInspectorStyleSheet(document, force.value_or(false));
  if (!inspector_style_sheet)
    return protocol::Response::ServerError("No target stylesheet found");

  UpdateActiveStyleSheets(document);

  *out_style_sheet_id = inspector_style_sheet->Id();
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::addRule(
    const String& style_sheet_id,
    const String& rule_text,
    std::unique_ptr<protocol::CSS::SourceRange> location,
    std::optional<int> node_for_property_syntax_validation,
    std::unique_ptr<protocol::CSS::CSSRule>* result) {
  FrontendOperationScope scope;
  InspectorStyleSheet* inspector_style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, inspector_style_sheet);
  if (!response.IsSuccess())
    return response;

  Element* element = nullptr;
  if (node_for_property_syntax_validation.has_value()) {
    response = dom_agent_->AssertElement(
        node_for_property_syntax_validation.value(), element);
    if (!response.IsSuccess())
      return response;
  }

  SourceRange rule_location;
  response = JsonRangeToSourceRange(inspector_style_sheet, location.get(),
                                    &rule_location);
  if (!response.IsSuccess())
    return response;

  DummyExceptionStateForTesting exception_state;
  AddRuleAction* action = MakeGarbageCollected<AddRuleAction>(
      inspector_style_sheet, rule_text, rule_location);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  if (!success)
    return InspectorDOMAgent::ToResponse(exception_state);

  CSSStyleRule* rule = action->TakeRule();
  if (element) {
    element->GetDocument().UpdateStyleAndLayoutForNode(
        element, DocumentUpdateReason::kInspector);
  }
  *result = BuildObjectForRule(rule, element);
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::forcePseudoState(
    int node_id,
    std::unique_ptr<protocol::Array<String>> forced_pseudo_classes) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess())
    return response;
  Element* element = nullptr;
  response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess())
    return response;

  unsigned forced_pseudo_state =
      ComputePseudoClassMask(std::move(forced_pseudo_classes));
  NodeIdToForcedPseudoState::iterator it =
      node_id_to_forced_pseudo_state_.find(node_id);
  unsigned current_forced_pseudo_state =
      it == node_id_to_forced_pseudo_state_.end() ? 0 : it->value;
  bool need_style_recalc = forced_pseudo_state != current_forced_pseudo_state;

  if (!need_style_recalc)
    return protocol::Response::Success();

  if (forced_pseudo_state)
    node_id_to_forced_pseudo_state_.Set(node_id, forced_pseudo_state);
  else
    node_id_to_forced_pseudo_state_.erase(node_id);

  // When adding focus or focus-within, we force focus-within for ancestor
  // nodes to emulate real focus for user convenience.

  // Flips from no forced focus to the forced focus (:focus or :focus-within).
  if (((forced_pseudo_state & kPseudoFocus) == kPseudoFocus &&
       (current_forced_pseudo_state & kPseudoFocus) == 0) ||
      ((forced_pseudo_state & kPseudoFocusWithin) == kPseudoFocusWithin &&
       (current_forced_pseudo_state & kPseudoFocusWithin) == 0)) {
    IncrementFocusedCountForAncestors(element);
  }

  // Flips from the forced focus (:focus or :focus-within) to no focus.
  if (((forced_pseudo_state & kPseudoFocus) == 0 &&
       (current_forced_pseudo_state & kPseudoFocus) == kPseudoFocus) ||
      ((forced_pseudo_state & kPseudoFocusWithin) == 0 &&
       (current_forced_pseudo_state & kPseudoFocusWithin) ==
           kPseudoFocusWithin)) {
    DecrementFocusedCountForAncestors(element);
  }

  element->GetDocument().GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kInspector));
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::forceStartingStyle(int node_id,
                                                         bool forced) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess()) {
    return response;
  }
  Element* element = nullptr;
  response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess()) {
    return response;
  }

  bool current_forced_starting_style =
      node_id_to_forced_starting_style_.Contains(node_id);
  bool need_style_recalc = forced != current_forced_starting_style;

  if (!need_style_recalc) {
    return protocol::Response::Success();
  }

  if (forced) {
    node_id_to_forced_starting_style_.insert(node_id);
  } else {
    node_id_to_forced_starting_style_.erase(node_id);
  }

  element->GetDocument().GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kInspector));
  return protocol::Response::Success();
}

void InspectorCSSAgent::IncrementFocusedCountForAncestors(Element* element) {
  for (Node& ancestor : FlatTreeTraversal::AncestorsOf(*element)) {
    if (!IsA<Element>(ancestor))
      continue;
    int node_id = dom_agent_->BoundNodeId(&ancestor);
    if (!node_id)
      continue;
    NodeIdToNumberFocusedChildren::iterator it =
        node_id_to_number_focused_children_.find(node_id);
    unsigned count =
        it == node_id_to_number_focused_children_.end() ? 0 : it->value;
    node_id_to_number_focused_children_.Set(node_id, count + 1);
  }
}

void InspectorCSSAgent::DecrementFocusedCountForAncestors(Element* element) {
  for (Node& ancestor : FlatTreeTraversal::AncestorsOf(*element)) {
    if (!IsA<Element>(ancestor))
      continue;
    int node_id = dom_agent_->BoundNodeId(&ancestor);
    if (!node_id)
      continue;
    NodeIdToNumberFocusedChildren::iterator it =
        node_id_to_number_focused_children_.find(node_id);
    unsigned count =
        it == node_id_to_number_focused_children_.end() ? 1 : it->value;
    if (count <= 1) {
      // If `count - 1` is zero or overflows, erase the node_id
      // from the map to save memory. If there is zero focused child
      // elements, :focus-within should not be forced.
      node_id_to_number_focused_children_.erase(node_id);
    } else {
      node_id_to_number_focused_children_.Set(node_id, count - 1);
    }
  }
}

std::unique_ptr<protocol::CSS::CSSMedia> InspectorCSSAgent::BuildMediaObject(
    const MediaList* media,
    MediaListSource media_list_source,
    const String& source_url,
    CSSStyleSheet* parent_style_sheet) {
  // Make certain compilers happy by initializing |source| up-front.
  String source = protocol::CSS::CSSMedia::SourceEnum::InlineSheet;
  switch (media_list_source) {
    case kMediaListSourceMediaRule:
      source = protocol::CSS::CSSMedia::SourceEnum::MediaRule;
      break;
    case kMediaListSourceImportRule:
      source = protocol::CSS::CSSMedia::SourceEnum::ImportRule;
      break;
    case kMediaListSourceLinkedSheet:
      source = protocol::CSS::CSSMedia::SourceEnum::LinkedSheet;
      break;
    case kMediaListSourceInlineSheet:
      source = protocol::CSS::CSSMedia::SourceEnum::InlineSheet;
      break;
  }

  const MediaQuerySet* queries = media->Queries();
  const HeapVector<Member<const MediaQuery>>& query_vector =
      queries->QueryVector();
  LocalFrame* frame = nullptr;
  if (parent_style_sheet) {
    if (Document* document = parent_style_sheet->OwnerDocument())
      frame = document->GetFrame();
  }
  MediaQueryEvaluator* media_evaluator =
      MakeGarbageCollected<MediaQueryEvaluator>(frame);

  InspectorStyleSheet* inspector_style_sheet = nullptr;
  if (parent_style_sheet) {
    auto it =
        css_style_sheet_to_inspector_style_sheet_.find(parent_style_sheet);
    if (it != css_style_sheet_to_inspector_style_sheet_.end())
      inspector_style_sheet = it->value;
  }

  auto media_list_array =
      std::make_unique<protocol::Array<protocol::CSS::MediaQuery>>();
  MediaValues* media_values = MediaValues::CreateDynamicIfFrameExists(frame);
  bool has_media_query_items = false;
  for (wtf_size_t i = 0; i < query_vector.size(); ++i) {
    const MediaQuery& query = *query_vector.at(i);
    HeapVector<MediaQueryExp> expressions;
    if (query.ExpNode())
      query.ExpNode()->CollectExpressions(expressions);
    auto expression_array = std::make_unique<
        protocol::Array<protocol::CSS::MediaQueryExpression>>();
    bool has_expression_items = false;
    for (wtf_size_t j = 0; j < expressions.size(); ++j) {
      const MediaQueryExp& media_query_exp = expressions.at(j);
      MediaQueryExpValue exp_value = media_query_exp.Bounds().right.value;
      if (!exp_value.IsNumericLiteralValue()) {
        continue;
      }
      const char* value_name =
          CSSPrimitiveValue::UnitTypeToString(exp_value.GetUnitType());
      std::unique_ptr<protocol::CSS::MediaQueryExpression>
          media_query_expression =
              protocol::CSS::MediaQueryExpression::create()
                  .setValue(exp_value.GetDoubleValue())
                  .setUnit(String(value_name))
                  .setFeature(media_query_exp.MediaFeature())
                  .build();

      if (inspector_style_sheet && media->ParentRule()) {
        media_query_expression->setValueRange(
            inspector_style_sheet->MediaQueryExpValueSourceRange(
                media->ParentRule(), i, j));
      }

      int computed_length;
      if (media_values->ComputeLength(exp_value.GetDoubleValue(),
                                      exp_value.GetUnitType(),
                                      computed_length)) {
        media_query_expression->setComputedLength(computed_length);
      }

      expression_array->emplace_back(std::move(media_query_expression));
      has_expression_items = true;
    }
    if (!has_expression_items)
      continue;
    std::unique_ptr<protocol::CSS::MediaQuery> media_query =
        protocol::CSS::MediaQuery::create()
            .setActive(media_evaluator->Eval(query))
            .setExpressions(std::move(expression_array))
            .build();
    media_list_array->emplace_back(std::move(media_query));
    has_media_query_items = true;
  }

  std::unique_ptr<protocol::CSS::CSSMedia> media_object =
      protocol::CSS::CSSMedia::create()
          .setText(media->MediaTextInternal())
          .setSource(source)
          .build();
  if (has_media_query_items)
    media_object->setMediaList(std::move(media_list_array));

  if (inspector_style_sheet && media_list_source != kMediaListSourceLinkedSheet)
    media_object->setStyleSheetId(inspector_style_sheet->Id());

  if (!source_url.empty()) {
    media_object->setSourceURL(source_url);

    CSSRule* parent_rule = media->ParentRule();
    if (!parent_rule)
      return media_object;
    inspector_style_sheet = BindStyleSheet(parent_rule->parentStyleSheet());
    media_object->setRange(
        inspector_style_sheet->RuleHeaderSourceRange(parent_rule));
  }
  return media_object;
}

void InspectorCSSAgent::CollectMediaQueriesFromStyleSheet(
    CSSStyleSheet* style_sheet,
    protocol::Array<protocol::CSS::CSSMedia>* media_array,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  MediaList* media_list = style_sheet->media();
  String source_url;
  if (media_list && media_list->length()) {
    Document* doc = style_sheet->OwnerDocument();
    if (doc)
      source_url = doc->Url();
    else if (!style_sheet->Contents()->BaseURL().IsEmpty())
      source_url = style_sheet->Contents()->BaseURL();
    else
      source_url = "";
    media_array->emplace_back(
        BuildMediaObject(media_list,
                         style_sheet->ownerNode() ? kMediaListSourceLinkedSheet
                                                  : kMediaListSourceInlineSheet,
                         source_url, style_sheet));
    if (rule_types) {
      rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::MediaRule);
    }
  }
}

void InspectorCSSAgent::CollectMediaQueriesFromRule(
    CSSRule* rule,
    protocol::Array<protocol::CSS::CSSMedia>* media_array,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  MediaList* media_list;
  String source_url;
  CSSStyleSheet* parent_style_sheet = nullptr;
  bool is_media_rule = true;
  if (auto* media_rule = DynamicTo<CSSMediaRule>(rule)) {
    media_list = media_rule->media();
    parent_style_sheet = media_rule->parentStyleSheet();
  } else if (auto* import_rule = DynamicTo<CSSImportRule>(rule)) {
    media_list = import_rule->media();
    parent_style_sheet = import_rule->parentStyleSheet();
    is_media_rule = false;
  } else {
    media_list = nullptr;
  }

  if (parent_style_sheet) {
    source_url = parent_style_sheet->Contents()->BaseURL();
    if (source_url.empty()) {
      source_url = InspectorDOMAgent::DocumentURLString(
          parent_style_sheet->OwnerDocument());
    }
  } else {
    source_url = "";
  }

  if (media_list && media_list->length()) {
    media_array->emplace_back(BuildMediaObject(
        media_list,
        is_media_rule ? kMediaListSourceMediaRule : kMediaListSourceImportRule,
        source_url, parent_style_sheet));
    if (rule_types) {
      rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::MediaRule);
    }
  }
}

std::unique_ptr<protocol::CSS::CSSContainerQuery>
InspectorCSSAgent::BuildContainerQueryObject(CSSContainerRule* rule) {
  std::unique_ptr<protocol::CSS::CSSContainerQuery> container_query_object =
      protocol::CSS::CSSContainerQuery::create()
          .setText(rule->containerQuery())
          .build();

  auto it =
      css_style_sheet_to_inspector_style_sheet_.find(rule->parentStyleSheet());
  if (it != css_style_sheet_to_inspector_style_sheet_.end()) {
    InspectorStyleSheet* inspector_style_sheet = it->value;
    container_query_object->setStyleSheetId(inspector_style_sheet->Id());
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(rule->parentStyleSheet());
  container_query_object->setRange(
      inspector_style_sheet->RuleHeaderSourceRange(rule));

  if (!rule->Name().empty())
    container_query_object->setName(rule->Name());

  PhysicalAxes physical = rule->Selector().GetPhysicalAxes();
  if (physical != kPhysicalAxesNone) {
    protocol::DOM::PhysicalAxes physical_proto =
        protocol::DOM::PhysicalAxesEnum::Horizontal;
    if (physical == kPhysicalAxesVertical) {
      physical_proto = protocol::DOM::PhysicalAxesEnum::Vertical;
    } else if (physical == kPhysicalAxesBoth) {
      physical_proto = protocol::DOM::PhysicalAxesEnum::Both;
    } else {
      DCHECK(physical == kPhysicalAxesHorizontal);
    }
    container_query_object->setPhysicalAxes(physical_proto);
  }
  LogicalAxes logical = rule->Selector().GetLogicalAxes();
  if (logical != kLogicalAxesNone) {
    protocol::DOM::LogicalAxes logical_proto =
        protocol::DOM::LogicalAxesEnum::Inline;
    if (logical == kLogicalAxesBlock) {
      logical_proto = protocol::DOM::LogicalAxesEnum::Block;
    } else if (logical == kLogicalAxesBoth) {
      logical_proto = protocol::DOM::LogicalAxesEnum::Both;
    } else {
      DCHECK(logical == kLogicalAxesInline);
    }
    container_query_object->setLogicalAxes(logical_proto);
  }
  if (rule->Selector().SelectsScrollStateContainers()) {
    container_query_object->setQueriesScrollState(true);
  }
  return container_query_object;
}

void InspectorCSSAgent::CollectContainerQueriesFromRule(
    CSSRule* rule,
    protocol::Array<protocol::CSS::CSSContainerQuery>* container_queries,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  if (auto* container_rule = DynamicTo<CSSContainerRule>(rule)) {
    container_queries->emplace_back(BuildContainerQueryObject(container_rule));
    rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::ContainerRule);
  }
}

std::unique_ptr<protocol::CSS::CSSSupports>
InspectorCSSAgent::BuildSupportsObject(CSSSupportsRule* rule) {
  std::unique_ptr<protocol::CSS::CSSSupports> supports_object =
      protocol::CSS::CSSSupports::create()
          .setText(rule->ConditionTextInternal())
          .setActive(rule->ConditionIsSupported())
          .build();

  auto it =
      css_style_sheet_to_inspector_style_sheet_.find(rule->parentStyleSheet());
  if (it != css_style_sheet_to_inspector_style_sheet_.end()) {
    InspectorStyleSheet* inspector_style_sheet = it->value;
    supports_object->setStyleSheetId(inspector_style_sheet->Id());
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(rule->parentStyleSheet());
  supports_object->setRange(inspector_style_sheet->RuleHeaderSourceRange(rule));

  return supports_object;
}

void InspectorCSSAgent::CollectSupportsFromRule(
    CSSRule* rule,
    protocol::Array<protocol::CSS::CSSSupports>* supports_list,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  if (auto* supports_rule = DynamicTo<CSSSupportsRule>(rule)) {
    supports_list->emplace_back(BuildSupportsObject(supports_rule));
    rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::SupportsRule);
  }
}

std::unique_ptr<protocol::CSS::CSSLayer> InspectorCSSAgent::BuildLayerObject(
    CSSLayerBlockRule* rule) {
  std::unique_ptr<protocol::CSS::CSSLayer> layer_object =
      protocol::CSS::CSSLayer::create().setText(rule->name()).build();

  auto it =
      css_style_sheet_to_inspector_style_sheet_.find(rule->parentStyleSheet());
  if (it != css_style_sheet_to_inspector_style_sheet_.end()) {
    InspectorStyleSheet* inspector_style_sheet = it->value;
    layer_object->setStyleSheetId(inspector_style_sheet->Id());
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(rule->parentStyleSheet());
  layer_object->setRange(inspector_style_sheet->RuleHeaderSourceRange(rule));

  return layer_object;
}

std::unique_ptr<protocol::CSS::CSSLayer>
InspectorCSSAgent::BuildLayerObjectFromImport(CSSImportRule* rule) {
  std::unique_ptr<protocol::CSS::CSSLayer> layer_object =
      protocol::CSS::CSSLayer::create().setText(rule->layerName()).build();

  auto it =
      css_style_sheet_to_inspector_style_sheet_.find(rule->parentStyleSheet());
  if (it != css_style_sheet_to_inspector_style_sheet_.end()) {
    InspectorStyleSheet* inspector_style_sheet = it->value;
    layer_object->setStyleSheetId(inspector_style_sheet->Id());
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(rule->parentStyleSheet());
  layer_object->setRange(inspector_style_sheet->RuleHeaderSourceRange(rule));

  return layer_object;
}

void InspectorCSSAgent::CollectLayersFromRule(
    CSSRule* rule,
    protocol::Array<protocol::CSS::CSSLayer>* layers_list,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  if (auto* layer_rule = DynamicTo<CSSLayerBlockRule>(rule)) {
    layers_list->emplace_back(BuildLayerObject(layer_rule));
    rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::LayerRule);
  } else if (auto* import_rule = DynamicTo<CSSImportRule>(rule)) {
    if (import_rule->layerName() != g_null_atom) {
      layers_list->emplace_back(BuildLayerObjectFromImport(import_rule));
      rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::LayerRule);
    }
  }
}

std::unique_ptr<protocol::CSS::CSSStartingStyle>
InspectorCSSAgent::BuildStartingStyleObject(CSSStartingStyleRule* rule) {
  std::unique_ptr<protocol::CSS::CSSStartingStyle> starting_style_object =
      protocol::CSS::CSSStartingStyle::create().build();

  auto it =
      css_style_sheet_to_inspector_style_sheet_.find(rule->parentStyleSheet());
  if (it != css_style_sheet_to_inspector_style_sheet_.end()) {
    InspectorStyleSheet* inspector_style_sheet = it->value;
    starting_style_object->setStyleSheetId(inspector_style_sheet->Id());
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(rule->parentStyleSheet());
  starting_style_object->setRange(
      inspector_style_sheet->RuleHeaderSourceRange(rule));

  return starting_style_object;
}

void InspectorCSSAgent::CollectStartingStylesFromRule(
    CSSRule* rule,
    protocol::Array<protocol::CSS::CSSStartingStyle>* starting_style_list,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  if (auto* starting_style_rule = DynamicTo<CSSStartingStyleRule>(rule)) {
    starting_style_list->emplace_back(
        BuildStartingStyleObject(starting_style_rule));
    rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::StartingStyleRule);
  }
}

void InspectorCSSAgent::FillAncestorData(CSSRule* rule,
                                         protocol::CSS::CSSRule* result) {
  auto layers_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSLayer>>();
  auto media_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSMedia>>();
  auto supports_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSSupports>>();
  auto container_queries_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSContainerQuery>>();
  auto scopes_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSScope>>();
  auto rule_types_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSRuleType>>();
  auto starting_style_list =
      std::make_unique<protocol::Array<protocol::CSS::CSSStartingStyle>>();

  CSSRule* parent_rule = rule;
  auto nesting_selectors = std::make_unique<protocol::Array<String>>();
  while (parent_rule) {
    CollectLayersFromRule(parent_rule, layers_list.get(),
                          rule_types_list.get());
    CollectMediaQueriesFromRule(parent_rule, media_list.get(),
                                rule_types_list.get());
    CollectContainerQueriesFromRule(parent_rule, container_queries_list.get(),
                                    rule_types_list.get());
    CollectSupportsFromRule(parent_rule, supports_list.get(),
                            rule_types_list.get());
    CollectScopesFromRule(parent_rule, scopes_list.get(),
                          rule_types_list.get());
    CollectStartingStylesFromRule(parent_rule, starting_style_list.get(),
                                  rule_types_list.get());

    if (parent_rule != rule) {
      if (auto* style_rule = DynamicTo<CSSStyleRule>(parent_rule)) {
        nesting_selectors->emplace_back(style_rule->selectorText());
        rule_types_list->emplace_back(
            protocol::CSS::CSSRuleTypeEnum::StyleRule);
      }
    }

    if (parent_rule->parentRule()) {
      parent_rule = parent_rule->parentRule();
    } else {
      CSSStyleSheet* style_sheet = parent_rule->parentStyleSheet();
      while (style_sheet) {
        CollectMediaQueriesFromStyleSheet(style_sheet, media_list.get(),
                                          rule_types_list.get());
        parent_rule = style_sheet->ownerRule();
        if (parent_rule)
          break;
        style_sheet = style_sheet->parentStyleSheet();
      }
    }
  }
  result->setMedia(std::move(media_list));
  result->setSupports(std::move(supports_list));
  result->setScopes(std::move(scopes_list));
  std::reverse(layers_list.get()->begin(), layers_list.get()->end());
  result->setLayers(std::move(layers_list));
  result->setContainerQueries(std::move(container_queries_list));
  result->setRuleTypes(std::move(rule_types_list));
  result->setStartingStyles(std::move(starting_style_list));
  if (nesting_selectors->size() > 0) {
    result->setNestingSelectors(std::move(nesting_selectors));
  }
}

std::unique_ptr<protocol::CSS::CSSScope> InspectorCSSAgent::BuildScopeObject(
    CSSScopeRule* rule) {
  std::unique_ptr<protocol::CSS::CSSScope> scope_object =
      protocol::CSS::CSSScope::create().setText(rule->PreludeText()).build();

  auto it =
      css_style_sheet_to_inspector_style_sheet_.find(rule->parentStyleSheet());
  if (it != css_style_sheet_to_inspector_style_sheet_.end()) {
    InspectorStyleSheet* inspector_style_sheet = it->value;
    scope_object->setStyleSheetId(inspector_style_sheet->Id());
  }

  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(rule->parentStyleSheet());
  scope_object->setRange(inspector_style_sheet->RuleHeaderSourceRange(rule));

  return scope_object;
}

void InspectorCSSAgent::CollectScopesFromRule(
    CSSRule* rule,
    protocol::Array<protocol::CSS::CSSScope>* scopes_list,
    protocol::Array<protocol::CSS::CSSRuleType>* rule_types) {
  if (auto* scope_rule = DynamicTo<CSSScopeRule>(rule)) {
    scopes_list->emplace_back(BuildScopeObject(scope_rule));
    rule_types->emplace_back(protocol::CSS::CSSRuleTypeEnum::ScopeRule);
  }
}

std::unique_ptr<protocol::CSS::CSSFunctionConditionNode>
InspectorCSSAgent::BuildObjectForFunctionConditionNode(CSSConditionRule* rule) {
  return protocol::CSS::CSSFunctionConditionNode::create()
      .setConditionText(rule->ConditionTextInternal())
      .setChildren(BuildArrayForFunctionNodeChildren(rule->cssRules()))
      .build();
}

std::unique_ptr<protocol::Array<protocol::CSS::CSSFunctionNode>>
InspectorCSSAgent::BuildArrayForFunctionNodeChildren(CSSRuleList* rule_list) {
  std::unique_ptr<protocol::Array<protocol::CSS::CSSFunctionNode>> result =
      std::make_unique<protocol::Array<protocol::CSS::CSSFunctionNode>>();

  const unsigned length = rule_list->length();
  for (unsigned i = 0; i < length; ++i) {
    CSSRule* rule = rule_list->item(i);
    std::unique_ptr<protocol::CSS::CSSFunctionNode> function_node =
        protocol::CSS::CSSFunctionNode::create().build();
    switch (rule->GetType()) {
      case CSSRule::kFunctionDeclarationsRule: {
        CSSFunctionDeclarationsRule* function_rule =
            To<CSSFunctionDeclarationsRule>(rule);
        InspectorStyleSheet* inspector_style_sheet =
            BindStyleSheet(function_rule->parentStyleSheet());
        function_node->setStyle(inspector_style_sheet->BuildObjectForStyle(
            function_rule->style(), nullptr));
        break;
      }
      case CSSRule::kContainerRule: {
        CSSContainerRule* container_rule = To<CSSContainerRule>(rule);
        std::unique_ptr<protocol::CSS::CSSFunctionConditionNode> condition =
            BuildObjectForFunctionConditionNode(container_rule);
        condition->setContainerQueries(
            BuildContainerQueryObject(container_rule));
        function_node->setCondition(std::move(condition));
        break;
      }
      case CSSRule::kMediaRule: {
        CSSMediaRule* media_rule = To<CSSMediaRule>(rule);
        std::unique_ptr<protocol::CSS::CSSFunctionConditionNode> condition =
            BuildObjectForFunctionConditionNode(media_rule);
        String source_url =
            media_rule->parentStyleSheet()->Contents()->BaseURL();
        if (source_url.empty()) {
          source_url = InspectorDOMAgent::DocumentURLString(
              media_rule->parentStyleSheet()->OwnerDocument());
        }
        condition->setMedia(
            BuildMediaObject(media_rule->media(), kMediaListSourceMediaRule,
                             source_url, media_rule->parentStyleSheet()));
        function_node->setCondition(std::move(condition));
        break;
      }
      case CSSRule::kSupportsRule: {
        CSSSupportsRule* supports_rule = To<CSSSupportsRule>(rule);
        std::unique_ptr<protocol::CSS::CSSFunctionConditionNode> condition =
            BuildObjectForFunctionConditionNode(supports_rule);
        condition->setSupports(BuildSupportsObject(supports_rule));
        function_node->setCondition(std::move(condition));
        break;
      }
      default:
        DCHECK(false) << "Unexpected rule type: " << rule->GetType();
        break;
    }
    result->emplace_back(std::move(function_node));
  }
  return result;
}

std::unique_ptr<protocol::CSS::CSSFunctionRule>
InspectorCSSAgent::BuildObjectForFunctionRule(CSSFunctionRule* function_rule) {
  InspectorStyleSheet* inspector_style_sheet =
      BindStyleSheet(function_rule->parentStyleSheet());
  std::unique_ptr<protocol::CSS::Value> name =
      protocol::CSS::Value::create().setText(function_rule->name()).build();

  if (CSSRuleSourceData* source_data =
          inspector_style_sheet->SourceDataForRule(function_rule)) {
    name->setRange(inspector_style_sheet->BuildSourceRangeObject(
        source_data->rule_header_range));
  }

  std::unique_ptr<protocol::Array<protocol::CSS::CSSFunctionParameter>>
      parameters = std::make_unique<
          protocol::Array<protocol::CSS::CSSFunctionParameter>>();
  for (const auto& parameter : function_rule->getParameters()) {
    parameters->emplace_back(protocol::CSS::CSSFunctionParameter::create()
                                 .setName(parameter->name())
                                 .setType(parameter->type())
                                 .build());
  }
  std::unique_ptr<protocol::CSS::CSSFunctionRule> result =
      protocol::CSS::CSSFunctionRule::create()
          .setName(std::move(name))
          .setOrigin(inspector_style_sheet->Origin())
          .setParameters(std::move(parameters))
          .setChildren(
              BuildArrayForFunctionNodeChildren(function_rule->cssRules()))
          .build();
  if (inspector_style_sheet->CanBindOrigin() &&
      !inspector_style_sheet->Id().empty()) {
    result->setStyleSheetId(inspector_style_sheet->Id());
  }
  return result;
}

InspectorStyleSheetForInlineStyle* InspectorCSSAgent::AsInspectorStyleSheet(
    Element* element) {
  NodeToInspectorStyleSheet::iterator it =
      node_to_inspector_style_sheet_.find(element);
  if (it != node_to_inspector_style_sheet_.end())
    return it->value.Get();

  CSSStyleDeclaration* style = element->style();
  if (!style)
    return nullptr;

  InspectorStyleSheetForInlineStyle* inspector_style_sheet =
      MakeGarbageCollected<InspectorStyleSheetForInlineStyle>(element, this);
  id_to_inspector_style_sheet_for_inline_style_.Set(inspector_style_sheet->Id(),
                                                    inspector_style_sheet);
  node_to_inspector_style_sheet_.Set(element, inspector_style_sheet);
  return inspector_style_sheet;
}

// static
void InspectorCSSAgent::CollectAllDocumentStyleSheets(
    Document* document,
    HeapVector<Member<CSSStyleSheet>>& result) {
  for (const auto& style :
       document->GetStyleEngine().ActiveStyleSheetsForInspector())
    InspectorCSSAgent::CollectStyleSheets(style.first, result);
}

// static
void InspectorCSSAgent::CollectStyleSheets(
    CSSStyleSheet* style_sheet,
    HeapVector<Member<CSSStyleSheet>>& result) {
  result.push_back(style_sheet);
  for (unsigned i = 0, size = style_sheet->length(); i < size; ++i) {
    CSSRule* rule = style_sheet->ItemInternal(i);
    if (auto* import_rule = DynamicTo<CSSImportRule>(rule)) {
      CSSStyleSheet* imported_style_sheet = import_rule->styleSheet();
      if (imported_style_sheet)
        InspectorCSSAgent::CollectStyleSheets(imported_style_sheet, result);
    }
  }
}

InspectorStyleSheet* InspectorCSSAgent::BindStyleSheet(
    CSSStyleSheet* style_sheet) {
  auto it = css_style_sheet_to_inspector_style_sheet_.find(style_sheet);
  if (it != css_style_sheet_to_inspector_style_sheet_.end())
    return it->value;

  Document* document = style_sheet->OwnerDocument();
  InspectorStyleSheet* inspector_style_sheet =
      MakeGarbageCollected<InspectorStyleSheet>(
          network_agent_, style_sheet, DetectOrigin(style_sheet, document),
          InspectorDOMAgent::DocumentURLString(document), this,
          resource_container_);
  id_to_inspector_style_sheet_.Set(inspector_style_sheet->Id(),
                                   inspector_style_sheet);
  css_style_sheet_to_inspector_style_sheet_.Set(style_sheet,
                                                inspector_style_sheet);
  return inspector_style_sheet;
}

String InspectorCSSAgent::StyleSheetId(CSSStyleSheet* style_sheet) {
  return BindStyleSheet(style_sheet)->Id();
}

String InspectorCSSAgent::UnbindStyleSheet(
    InspectorStyleSheet* inspector_style_sheet) {
  String id = inspector_style_sheet->Id();
  id_to_inspector_style_sheet_.erase(id);
  if (inspector_style_sheet->PageStyleSheet()) {
    css_style_sheet_to_inspector_style_sheet_.erase(
        inspector_style_sheet->PageStyleSheet());
  }
  return id;
}

InspectorStyleSheet* InspectorCSSAgent::InspectorStyleSheetForRule(
    CSSStyleRule* rule) {
  if (!rule)
    return nullptr;

  // CSSRules returned by StyleResolver::pseudoCSSRulesForElement lack parent
  // pointers if they are coming from user agent stylesheets. To work around
  // this issue, we use CSSOM wrapper created by inspector.
  if (!rule->parentStyleSheet()) {
    if (!inspector_user_agent_style_sheet_) {
      inspector_user_agent_style_sheet_ = MakeGarbageCollected<CSSStyleSheet>(
          CSSDefaultStyleSheets::Instance().DefaultStyleSheet());
    }
    rule->SetParentStyleSheet(inspector_user_agent_style_sheet_.Get());
  }
  return BindStyleSheet(rule->parentStyleSheet());
}

InspectorStyleSheet* InspectorCSSAgent::CreateViaInspectorStyleSheet(
    Document* document,
    bool force) {
  if (!document)
    return nullptr;

  if (!IsA<HTMLDocument>(document) && !document->IsSVGDocument())
    return nullptr;
  bool has_default_stylesheet =
      default_inspector_stylesheets_.Contains(document);
  CSSStyleSheet& inspector_sheet =
      has_default_stylesheet && !force
          ? *default_inspector_stylesheets_.at(document)
          : document->GetStyleEngine().CreateInspectorStyleSheet();
  if (!force) {
    default_inspector_stylesheets_.Set(document, &inspector_sheet);
  }
  FlushPendingProtocolNotifications();

  auto it = css_style_sheet_to_inspector_style_sheet_.find(&inspector_sheet);
  return it != css_style_sheet_to_inspector_style_sheet_.end() ? it->value
                                                               : nullptr;
}

protocol::Response InspectorCSSAgent::AssertEnabled() {
  return enable_completed_
             ? protocol::Response::Success()
             : protocol::Response::ServerError("CSS agent was not enabled");
}

protocol::Response InspectorCSSAgent::AssertInspectorStyleSheetForId(
    const String& style_sheet_id,
    InspectorStyleSheet*& result) {
  protocol::Response response = AssertEnabled();
  if (!response.IsSuccess())
    return response;
  IdToInspectorStyleSheet::iterator it =
      id_to_inspector_style_sheet_.find(style_sheet_id);
  if (it == id_to_inspector_style_sheet_.end()) {
    return protocol::Response::ServerError(
        "No style sheet with given id found");
  }
  result = it->value.Get();
  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::AssertStyleSheetForId(
    const String& style_sheet_id,
    InspectorStyleSheetBase*& result) {
  InspectorStyleSheet* style_sheet = nullptr;
  protocol::Response response =
      AssertInspectorStyleSheetForId(style_sheet_id, style_sheet);
  if (response.IsSuccess()) {
    result = style_sheet;
    return response;
  }
  IdToInspectorStyleSheetForInlineStyle::iterator it =
      id_to_inspector_style_sheet_for_inline_style_.find(style_sheet_id);
  if (it == id_to_inspector_style_sheet_for_inline_style_.end()) {
    return protocol::Response::ServerError(
        "No style sheet with given id found");
  }
  result = it->value.Get();
  return protocol::Response::Success();
}

protocol::CSS::StyleSheetOrigin InspectorCSSAgent::DetectOrigin(
    CSSStyleSheet* page_style_sheet,
    Document* owner_document) {
  DCHECK(page_style_sheet);

  if (!page_style_sheet->ownerNode() && page_style_sheet->href().empty() &&
      !page_style_sheet->IsConstructed())
    return protocol::CSS::StyleSheetOriginEnum::UserAgent;

  if (page_style_sheet->ownerNode() &&
      page_style_sheet->ownerNode()->IsDocumentNode()) {
    if (owner_document->GetStyleEngine().InspectorStyleSheets().Contains(
            page_style_sheet)) {
      return protocol::CSS::StyleSheetOriginEnum::Inspector;
    }
    return protocol::CSS::StyleSheetOriginEnum::Injected;
  }
  return protocol::CSS::StyleSheetOriginEnum::Regular;
}

std::unique_ptr<protocol::CSS::CSSRule> InspectorCSSAgent::BuildObjectForRule(
    CSSStyleRule* rule,
    Element* element,
    PseudoId pseudo_id,
    const AtomicString& pseudo_argument) {
  InspectorStyleSheet* inspector_style_sheet = InspectorStyleSheetForRule(rule);
  if (!inspector_style_sheet)
    return nullptr;

  std::unique_ptr<protocol::CSS::CSSRule> result =
      inspector_style_sheet->BuildObjectForRuleWithoutAncestorData(
          rule, element, pseudo_id, pseudo_argument);
  FillAncestorData(rule, result.get());
  return result;
}

std::unique_ptr<protocol::Array<protocol::CSS::RuleMatch>>
InspectorCSSAgent::BuildArrayForMatchedRuleList(
    RuleIndexList* rule_list,
    Element* element,
    const InspectorGhostRules& ghost_rules,
    PseudoId pseudo_id,
    const AtomicString& pseudo_argument) {
  auto result = std::make_unique<protocol::Array<protocol::CSS::RuleMatch>>();
  if (!rule_list)
    return result;

  // Dedupe matches coming from the same rule source.
  HeapVector<Member<CSSStyleRule>> uniq_rules;
  HeapHashSet<Member<CSSRule>> uniq_rules_set;
  HeapHashMap<Member<CSSStyleRule>, std::unique_ptr<Vector<unsigned>>>
      rule_indices;
  for (auto it = rule_list->rbegin(); it != rule_list->rend(); ++it) {
    CSSRule* rule = it->rule.Get();
    auto* style_rule = DynamicTo<CSSStyleRule>(rule);
    if (!style_rule)
      continue;
    if (!uniq_rules_set.Contains(rule)) {
      uniq_rules_set.insert(rule);
      uniq_rules.push_back(style_rule);
      rule_indices.Set(style_rule, std::make_unique<Vector<unsigned>>());
    }

    rule_indices.at(style_rule)->push_back(it->index);
  }

  for (auto it = uniq_rules.rbegin(); it != uniq_rules.rend(); ++it) {
    CSSStyleRule* rule = it->Get();
    std::unique_ptr<protocol::CSS::CSSRule> rule_object =
        BuildObjectForRule(rule, element, pseudo_id, pseudo_argument);
    if (!rule_object)
      continue;
    if (ghost_rules.Contains(rule)) {
      protocol::CSS::CSSStyle* style_object = rule_object->getStyle();
      if (!style_object || !style_object->getCssProperties() ||
          style_object->getCssProperties()->size() == 0) {
        // Skip empty ghost rules.
        continue;
      }
    }

    // Transform complex rule_indices into client-friendly, compound-basis for
    // matching_selectors.
    // e.g. ".foo + .bar, h1, body h1" for <h1>
    //  (complex): {.foo: 0, .bar: 1, h1: 2, body: 3, h1: 4}, matches: [2, 4]
    // (compound): {.foo: 0, .bar: 0, h1: 1, body: 2, h1: 2}, matches: [1, 2]
    auto matching_selectors = std::make_unique<protocol::Array<int>>();
    if (rule->GetStyleRule()) {
      // Compound index (0 -> 1 -> 2).
      int compound = 0;
      // Complex index of the next compound (0 -> 2 -> 3 -> kNotFound).
      wtf_size_t next_compound_start =
          rule->GetStyleRule()->IndexOfNextSelectorAfter(0);

      std::sort(rule_indices.at(rule)->begin(), rule_indices.at(rule)->end());
      for (unsigned complex_match : (*rule_indices.at(rule))) {
        while (complex_match >= next_compound_start &&
               next_compound_start != kNotFound) {
          next_compound_start = rule->GetStyleRule()->IndexOfNextSelectorAfter(
              next_compound_start);
          compound++;
        }
        matching_selectors->push_back(compound);
      }
    }

    result->emplace_back(
        protocol::CSS::RuleMatch::create()
            .setRule(std::move(rule_object))
            .setMatchingSelectors(std::move(matching_selectors))
            .build());
  }

  return result;
}

std::unique_ptr<protocol::CSS::CSSStyle>
InspectorCSSAgent::BuildObjectForAttributesStyle(Element* element) {
  if (!element->IsStyledElement())
    return nullptr;

  // FIXME: Ugliness below.
  const CSSPropertyValueSet* attribute_style =
      element->PresentationAttributeStyle();
  if (!attribute_style)
    return nullptr;

  InspectorStyle* inspector_style = MakeGarbageCollected<InspectorStyle>(
      attribute_style->MutableCopy()->EnsureCSSStyleDeclaration(
          element->GetExecutionContext()),
      nullptr, nullptr);
  return inspector_style->BuildObjectForStyle();
}

std::unique_ptr<protocol::Array<protocol::CSS::CSSAnimationStyle>>
InspectorCSSAgent::BuildArrayForCSSAnimationStyleList(Element* element) {
  std::unique_ptr<protocol::Array<protocol::CSS::CSSAnimationStyle>>
      animation_styles =
          std::make_unique<protocol::Array<protocol::CSS::CSSAnimationStyle>>();
  ElementAnimations* element_animations =
      element->GetAnimationTarget()->GetElementAnimations();
  if (!element_animations || element_animations->IsEmpty()) {
    return animation_styles;
  }

  HeapVector<Member<Animation>> animations;
  for (const auto& entry : element_animations->Animations()) {
    Animation& animation = *entry.key;
    AnimationEffect* effect = animation.effect();
    // We only include active CSS animations & WAAPI animations here.
    if (animation.IsCSSTransition() || !effect || !effect->IsInEffect()) {
      continue;
    }

    animations.push_back(animation);
  }

  // Sort animations based on their composite ordering so that
  // the animations are in the order of their application.
  std::sort(animations.begin(), animations.end(),
            [](Animation* a, Animation* b) {
              return Animation::HasLowerCompositeOrdering(
                  b, a, Animation::CompareAnimationsOrdering::kPointerOrder);
            });

  for (auto& animation : animations) {
    KeyframeEffect* effect = DynamicTo<KeyframeEffect>(animation->effect());
    if (!effect) {
      continue;
    }

    String name;
    if (CSSAnimation* css_animation = DynamicTo<CSSAnimation>(*animation)) {
      name = css_animation->animationName();
    }

    EffectStack& effect_stack = element_animations->GetEffectStack();
    HashSet<PropertyHandle> affected_properties =
        effect_stack.AffectedProperties(
            KeyframeEffect::Priority::kDefaultPriority);
    ActiveInterpolationsMap active_interpolations =
        EffectStack::ActiveInterpolations(
            &effect_stack,
            /*new_animations=*/nullptr,
            /*suppressed_animations=*/nullptr,
            KeyframeEffect::Priority::kDefaultPriority, effect);

    PropertyHandleSet animation_properties =
        effect->Model()->Properties().UniqueProperties();
    MutableCSSPropertyValueSet* property_values =
        MakeGarbageCollected<MutableCSSPropertyValueSet>(
            CSSParserMode::kHTMLStandardMode);

    AnimationUtils::ForEachInterpolatedPropertyValue(
        element, animation_properties, active_interpolations,
        [property_values](PropertyHandle property, const CSSValue* value) {
          property_values->SetProperty(property.GetCSSPropertyName(), *value);
        });

    InspectorStyle* inspector_style = MakeGarbageCollected<InspectorStyle>(
        property_values->EnsureCSSStyleDeclaration(
            element->GetExecutionContext()),
        nullptr, nullptr);

    std::unique_ptr<protocol::CSS::CSSAnimationStyle> animation_style =
        protocol::CSS::CSSAnimationStyle::create()
            .setStyle(inspector_style->BuildObjectForStyle())
            .build();

    if (!name.IsNull()) {
      animation_style->setName(name);
    }

    animation_styles->emplace_back(std::move(animation_style));
  }
  return animation_styles;
}

std::unique_ptr<protocol::CSS::CSSStyle>
InspectorCSSAgent::BuildObjectForTransitionsStyle(Element* element) {
  ElementAnimations* element_animations =
      element->GetAnimationTarget()->GetElementAnimations();
  if (!element_animations || element_animations->IsEmpty()) {
    return nullptr;
  }

  EffectStack& effect_stack = element_animations->GetEffectStack();
  HashSet<PropertyHandle> affected_properties = effect_stack.AffectedProperties(
      KeyframeEffect::Priority::kTransitionPriority);
  ActiveInterpolationsMap active_interpolations =
      EffectStack::ActiveInterpolations(
          &effect_stack,
          /*new_animations=*/nullptr,
          /*suppressed_animations=*/nullptr,
          KeyframeEffect::Priority::kTransitionPriority, nullptr);

  MutableCSSPropertyValueSet* property_values =
      MakeGarbageCollected<MutableCSSPropertyValueSet>(
          CSSParserMode::kHTMLStandardMode);
  AnimationUtils::ForEachInterpolatedPropertyValue(
      element, affected_properties, active_interpolations,
      [property_values](PropertyHandle property, const CSSValue* value) {
        property_values->SetProperty(property.GetCSSPropertyName(), *value);
      });
  InspectorStyle* inspector_style = MakeGarbageCollected<InspectorStyle>(
      property_values->EnsureCSSStyleDeclaration(
          element->GetExecutionContext()),
      nullptr, nullptr);
  return inspector_style->BuildObjectForStyle();
}

void InspectorCSSAgent::DidAddDocument(Document* document) {
  if (!tracker_)
    return;

  document->GetStyleEngine().SetRuleUsageTracker(tracker_);
  document->GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kInspector));
}

void InspectorCSSAgent::WillRemoveDOMNode(Node* node) {
  DCHECK(node);

  int node_id = dom_agent_->BoundNodeId(node);
  DCHECK(node_id);
  node_id_to_forced_pseudo_state_.erase(node_id);
  node_id_to_forced_starting_style_.erase(node_id);
  computed_style_updated_node_ids_.erase(node_id);

  NodeToInspectorStyleSheet::iterator it =
      node_to_inspector_style_sheet_.find(node);
  if (it == node_to_inspector_style_sheet_.end())
    return;

  id_to_inspector_style_sheet_for_inline_style_.erase(it->value->Id());
  node_to_inspector_style_sheet_.erase(node);
}

void InspectorCSSAgent::DidModifyDOMAttr(Element* element) {
  if (!element)
    return;

  NodeToInspectorStyleSheet::iterator it =
      node_to_inspector_style_sheet_.find(element);
  if (it == node_to_inspector_style_sheet_.end())
    return;

  it->value->DidModifyElementAttribute();
}

void InspectorCSSAgent::DidMutateStyleSheet(CSSStyleSheet* css_style_sheet) {
  auto it = css_style_sheet_to_inspector_style_sheet_.find(css_style_sheet);
  if (it == css_style_sheet_to_inspector_style_sheet_.end())
    return;
  InspectorStyleSheet* style_sheet = it->value;
  style_sheet->MarkForSync();
  StyleSheetChanged(style_sheet);
}

void InspectorCSSAgent::GetTextPosition(wtf_size_t offset,
                                        const String* text,
                                        TextPosition* result) {
  std::unique_ptr<Vector<wtf_size_t>> line_endings = WTF::GetLineEndings(*text);
  *result = TextPosition::FromOffsetAndLineEndings(offset, *line_endings);
}

void InspectorCSSAgent::DidReplaceStyleSheetText(CSSStyleSheet* css_style_sheet,
                                                 const String& text) {
  BindStyleSheet(css_style_sheet)->CSSOMStyleSheetTextReplaced(text);
}

void InspectorCSSAgent::StyleSheetChanged(
    InspectorStyleSheetBase* style_sheet) {
  if (g_frontend_operation_counter)
    return;
  FlushPendingProtocolNotifications();
  GetFrontend()->styleSheetChanged(style_sheet->Id());
}

void InspectorCSSAgent::ResetPseudoStates() {
  HeapHashSet<Member<Document>> documents_to_change;
  for (auto& state : node_id_to_forced_pseudo_state_) {
    if (auto* element = To<Element>(dom_agent_->NodeForId(state.key)))
      documents_to_change.insert(&element->GetDocument());
  }

  for (auto& count : node_id_to_number_focused_children_) {
    if (auto* element = To<Element>(dom_agent_->NodeForId(count.key)))
      documents_to_change.insert(&element->GetDocument());
  }

  node_id_to_forced_pseudo_state_.clear();
  node_id_to_number_focused_children_.clear();
  for (auto& document : documents_to_change) {
    document->GetStyleEngine().MarkAllElementsForStyleRecalc(
        StyleChangeReasonForTracing::Create(style_change_reason::kInspector));
  }
}

void InspectorCSSAgent::ResetStartingStyles() {
  HeapHashSet<Member<Document>> documents_to_change;
  for (auto& node_id : node_id_to_forced_starting_style_) {
    if (auto* element = To<Element>(dom_agent_->NodeForId(node_id))) {
      documents_to_change.insert(&element->GetDocument());
    }
  }

  node_id_to_forced_starting_style_.clear();
  for (auto& document : documents_to_change) {
    document->GetStyleEngine().MarkAllElementsForStyleRecalc(
        StyleChangeReasonForTracing::Create(style_change_reason::kInspector));
  }
}

HeapVector<Member<CSSStyleDeclaration>> InspectorCSSAgent::MatchingStyles(
    Element* element) {
  PseudoId pseudo_id = kPseudoIdNone;
  AtomicString view_transition_name = g_null_atom;
  PseudoElement* pseudo_element = nullptr;
  element = GetPseudoIdAndTag(element, pseudo_element, pseudo_id,
                              view_transition_name);
  if (!element)
    return {};

  StyleResolver& style_resolver = element->GetDocument().GetStyleResolver();

  // TODO(masonf,futhark): We need to update slot assignments here, so that
  // the flat tree is up to date for the call to PseudoCSSRulesForElement().
  // Eventually, RecalcSlotAssignments() should be called directly in
  // PseudoCSSRulesForElement(), but there are a number of sites within
  // inspector code that traverse the tree and call PseudoCSSRulesForElement()
  // for each element.
  element->GetDocument().GetSlotAssignmentEngine().RecalcSlotAssignments();

  // This ensures that active stylesheets are up-to-date, such that
  // the subsequent collection of matching rules actually match against
  // the correct RuleSets.
  element->GetDocument().GetStyleEngine().UpdateActiveStyle();

  HeapVector<Member<CSSStyleRule>> rules =
      FilterDuplicateRules(style_resolver.PseudoCSSRulesForElement(
          element, pseudo_id, view_transition_name,
          StyleResolver::kAllCSSRules));
  HeapVector<Member<CSSStyleDeclaration>> styles;
  if (!pseudo_id && element->style())
    styles.push_back(element->style());
  for (unsigned i = rules.size(); i > 0; --i) {
    CSSStyleSheet* parent_style_sheet = rules.at(i - 1)->parentStyleSheet();
    if (!parent_style_sheet || !parent_style_sheet->ownerNode())
      continue;  // User agent.
    styles.push_back(rules.at(i - 1)->style());
  }
  return styles;
}

CSSStyleDeclaration* InspectorCSSAgent::FindEffectiveDeclaration(
    const CSSPropertyName& property_name,
    const HeapVector<Member<CSSStyleDeclaration>>& styles) {
  if (!styles.size())
    return nullptr;

  String longhand = property_name.ToAtomicString();
  CSSStyleDeclaration* found_style = nullptr;

  for (unsigned i = 0; i < styles.size(); ++i) {
    CSSStyleDeclaration* style = styles.at(i).Get();
    if (style->getPropertyValue(longhand).empty())
      continue;
    if (style->getPropertyPriority(longhand) == "important")
      return style;
    if (!found_style)
      found_style = style;
  }

  return found_style ? found_style : styles.at(0).Get();
}

protocol::Response InspectorCSSAgent::setEffectivePropertyValueForNode(
    int node_id,
    const String& property_name,
    const String& value) {
  Element* element = nullptr;
  protocol::Response response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess())
    return response;
  if (element->GetPseudoId())
    return protocol::Response::ServerError("Elements is pseudo");

  if (!element->GetDocument().IsActive()) {
    return protocol::Response::ServerError(
        "Can't edit a node from a non-active document");
  }

  std::optional<CSSPropertyName> css_property_name =
      CSSPropertyName::From(element->GetExecutionContext(), property_name);
  if (!css_property_name.has_value())
    return protocol::Response::ServerError("Invalid property name");

  CSSStyleDeclaration* style =
      FindEffectiveDeclaration(*css_property_name, MatchingStyles(element));
  if (!style)
    return protocol::Response::ServerError("Can't find a style to edit");

  bool force_important = false;
  InspectorStyleSheetBase* inspector_style_sheet = nullptr;
  CSSRuleSourceData* source_data;
  // An absence of the parent rule means that given style is an inline style.
  if (style->parentRule()) {
    InspectorStyleSheet* style_sheet =
        BindStyleSheet(style->ParentStyleSheet());
    inspector_style_sheet = style_sheet;
    source_data = style_sheet->SourceDataForRule(style->parentRule());
  } else {
    InspectorStyleSheetForInlineStyle* inline_style_sheet =
        AsInspectorStyleSheet(element);
    inspector_style_sheet = inline_style_sheet;
    source_data = inline_style_sheet->RuleSourceData();
  }

  if (!source_data)
    return protocol::Response::ServerError("Can't find a source to edit");

  Vector<StylePropertyShorthand, 4> shorthands;
  getMatchingShorthandsForLonghand(css_property_name->Id(), &shorthands);

  String shorthand =
      shorthands.size() > 0
          ? CSSProperty::Get(shorthands[0].id()).GetPropertyNameString()
          : String();
  String longhand = css_property_name->ToAtomicString();

  int found_index = -1;
  Vector<CSSPropertySourceData>& properties = source_data->property_data;
  for (unsigned i = 0; i < properties.size(); ++i) {
    CSSPropertySourceData property = properties[properties.size() - i - 1];
    String name = property.name;
    if (property.disabled)
      continue;

    if (name != shorthand && name != longhand)
      continue;

    if (property.important || found_index == -1)
      found_index = properties.size() - i - 1;

    if (property.important)
      break;
  }

  SourceRange body_range = source_data->rule_body_range;
  String style_sheet_text;
  inspector_style_sheet->GetText(&style_sheet_text);
  String style_text =
      style_sheet_text.Substring(body_range.start, body_range.length());
  SourceRange change_range;
  if (found_index == -1) {
    String new_property_text =
        StrCat({"\n", longhand, ": ", value,
                (force_important ? " !important" : ""), ";"});
    if (!style_text.empty() && !style_text.StripWhiteSpace().EndsWith(';'))
      new_property_text = StrCat({";", new_property_text});
    style_text = StrCat({style_text, new_property_text});
    change_range.start = body_range.end;
    change_range.end = body_range.end + new_property_text.length();
  } else {
    CSSPropertySourceData declaration = properties[found_index];
    String new_value_text;
    if (declaration.name == shorthand) {
      new_value_text = CreateShorthandValue(element->GetDocument(), shorthand,
                                            declaration.value, longhand, value);
    } else {
      new_value_text = value;
    }

    String new_property_text = StrCat(
        {declaration.name, ": ", new_value_text,
         (declaration.important || force_important ? " !important" : ""), ";"});
    style_text.replace(declaration.range.start - body_range.start,
                       declaration.range.length(), new_property_text);
    change_range.start = declaration.range.start;
    change_range.end = change_range.start + new_property_text.length();
  }
  CSSStyleDeclaration* result_style;
  return SetStyleText(inspector_style_sheet, body_range, style_text,
                      result_style);
}

protocol::Response InspectorCSSAgent::getBackgroundColors(
    int node_id,
    std::unique_ptr<protocol::Array<String>>* background_colors,
    std::optional<String>* computed_font_size,
    std::optional<String>* computed_font_weight) {
  Element* element = nullptr;
  protocol::Response response = dom_agent_->AssertElement(node_id, element);
  if (!response.IsSuccess())
    return response;

  Vector<Color> bgcolors;
  String fs;
  String fw;
  float text_opacity = 1.0f;
  InspectorCSSAgent::GetBackgroundColors(element, &bgcolors, &fs, &fw,
                                         &text_opacity);

  if (bgcolors.size()) {
    *background_colors = std::make_unique<protocol::Array<String>>();
    for (const auto& color : bgcolors) {
      (*background_colors)
          ->emplace_back(
              cssvalue::CSSColor::SerializeAsCSSComponentValue(color));
    }
  }
  if (!fs.empty())
    *computed_font_size = fs;
  if (!fw.empty())
    *computed_font_weight = fw;
  return protocol::Response::Success();
}

// static
void InspectorCSSAgent::GetBackgroundColors(Element* element,
                                            Vector<Color>* colors,
                                            String* computed_font_size,
                                            String* computed_font_weight,
                                            float* text_opacity) {
  InspectorContrast contrast(&element->GetDocument());
  *colors = contrast.GetBackgroundColors(element, text_opacity);
  auto text_info = contrast.GetTextInfo(element);
  *computed_font_size = text_info.font_size;
  *computed_font_weight = text_info.font_weight;
}

namespace {

// Collects instances of <dashed-function>, and the functions referenced by
// them.
//
// The process starts from a CSSPropertyValueSet, where all relevant
// declarations are scanned. Any <dashed-function>s found are
// then "dereferenced" into StyleRuleFunctions, and *their* bodies
// are then scanned for further <dashed-function>s.
class TransitiveFunctionCollector {
  STACK_ALLOCATED();

 public:
  // Collect <dashed-function>s within a CSSPropertyValueSet,
  // typically representing the block of a style rule.
  //
  // The TreeScope must be the originating TreeScope for the rule
  // holding `set`.
  void CollectFromPropertySet(const TreeScope& tree_scope,
                              const CSSPropertyValueSet& set) {
    HashSet<AtomicString> names;
    for (CSSPropertyValue property_value : set.Properties()) {
      if (CSSVariableData* data = GetVariableData(property_value.Value());
          data && data->HasDashedFunctions()) {
        StringView text = data->OriginalText();
        CSSParserTokenStream stream(text);
        CSSVariableParser::CollectDashedFunctions(stream, names);
      }
    }
    for (const AtomicString& name : names) {
      auto* dashed_function =
          MakeGarbageCollected<ScopedCSSName>(name, &tree_scope);
      CollectFromDashedFunction(*dashed_function);
    }
  }

  // Once a <dashed-function> has been found, this finds the corresponding
  // StyleRuleFunction, and adds it to the list. It also continues looking
  // for <dashed-function>s within the StyleRuleFunction.
  void CollectFromDashedFunction(const ScopedCSSName& dashed_function) {
    if (seen_dashed_functions_.Contains(&dashed_function)) {
      // Already processed.
      return;
    }
    auto [function, tree_scope] = LookupFunction(dashed_function);
    // Note: This insertion may insert an explicit nullptr.
    seen_dashed_functions_.insert(&dashed_function, function);
    if (function) {
      CollectFromFunctionBody(*tree_scope, *function);
    }
  }

  // The TreeScope must be the originating TreeScope for
  // the function rule.
  //
  // The `rule` parameter may be the function rule itself (StyleRuleFunction),
  // it may be some conditional rule (e.g. StyleRuleMedia, etc),
  // or it may be StyleRuleFunctionDeclarations, which act as the "leaf nodes"
  // containing local variables / 'result' descriptors.
  void CollectFromFunctionBody(const TreeScope& tree_scope,
                               const StyleRuleBase& rule) {
    if (auto* function_declarations =
            DynamicTo<StyleRuleFunctionDeclarations>(rule)) {
      CollectFromPropertySet(tree_scope, function_declarations->Properties());
    } else if (auto* group_rule = DynamicTo<StyleRuleGroup>(rule)) {
      // Note that StyleRuleFunction, StyleRuleMedia, StyleRuleSupports,
      // and StyleRuleContainer are all subclasses of StyleRuleGroup.
      for (StyleRuleBase* child : group_rule->ChildRules()) {
        CollectFromFunctionBody(tree_scope, *child);
      }
    }
  }

  HeapHashMap<Member<const ScopedCSSName>, Member<StyleRuleFunction>>
  TakeResult() {
    return std::move(seen_dashed_functions_);
  }

 private:
  CSSVariableData* GetVariableData(const CSSValue& value) {
    // Handles custom properties and longhands.
    if (auto* unparsed = DynamicTo<CSSUnparsedDeclarationValue>(value)) {
      return unparsed->VariableDataValue();
    }
    // Handles shorthands.
    if (auto* pending_substitution =
            DynamicTo<cssvalue::CSSPendingSubstitutionValue>(value)) {
      return pending_substitution->ShorthandValue()->VariableDataValue();
    }
    return nullptr;
  }

  // For a given <dashed-function>, returns the corresponding StyleRuleFunction
  // and the TreeScope that StyleRuleFunction was found in. The returned
  // TreeScope may or may not be the same as the TreeScope of `dashed_function`.
  std::pair<StyleRuleFunction*, const TreeScope*> LookupFunction(
      const ScopedCSSName& dashed_function) {
    StyleEngine& style_engine =
        dashed_function.GetTreeScope()->GetDocument().GetStyleEngine();
    return style_engine.FindFunctionAcrossScopes(
        dashed_function.GetName(), dashed_function.GetTreeScope());
  }

  // Note that this implicitly uses HashTraits which compares keys by value
  // (see scoped_css_name.h).
  HeapHashMap<Member<const ScopedCSSName>, Member<StyleRuleFunction>>
      seen_dashed_functions_;
};

}  // namespace

// static
void InspectorCSSAgent::CollectReferencedFunctionRules(
    const HeapHashSet<Member<CSSStyleSheet>>& document_style_sheets,
    const RuleIndexList& rule_list,
    HeapHashMap<Member<const ScopedCSSName>, Member<CSSFunctionRule>>& result) {
  TransitiveFunctionCollector collector;
  for (const IndexedRule& rule : rule_list) {
    if (!rule.tree_scope) {
      // The tree-scope is nullptr for UA rules. At-rules are generally
      // not supported in UA sheets, so there should be no custom function
      // calls there either.
      continue;
    }
    if (const auto* style_rule = DynamicTo<CSSStyleRule>(*rule.rule)) {
      collector.CollectFromPropertySet(
          *rule.tree_scope, style_rule->GetStyleRule()->Properties());
    }
  }

  HeapHashMap<Member<const ScopedCSSName>, Member<StyleRuleFunction>>
      seen_functions = collector.TakeResult();

  if (seen_functions.empty()) {
    return;
  }

  // Build a mapping from StyleRuleFunction to CSSFunctionRule.
  HeapHashMap<Member<StyleRuleFunction>, Member<CSSFunctionRule>> to_css_rule;
  for (CSSStyleSheet* style_sheet : document_style_sheets) {
    TraverseCSSRules<CSSFunctionRule>(
        style_sheet, [&to_css_rule](CSSFunctionRule& rule) {
          to_css_rule.insert(&rule.FunctionRule(), &rule);
          return true;  // Keep traversing.
        });
  }

  // Emit the final results, which map ScopedCSSNames to CSSFunctionRules.
  for (const auto& [scoped_name, style_rule] : seen_functions) {
    if (!style_rule) {
      // An explicit nullptr means the function was referenced,
      // but no corresponding @function rule was found.
      continue;
    }
    result.insert(scoped_name, to_css_rule.at(style_rule));
  }
}

void InspectorCSSAgent::SetCoverageEnabled(bool enabled) {
  if (enabled == !!tracker_)
    return;
  tracker_ = enabled ? MakeGarbageCollected<StyleRuleUsageTracker>() : nullptr;

  for (Document* document : dom_agent_->Documents())
    document->GetStyleEngine().SetRuleUsageTracker(tracker_);
}

void InspectorCSSAgent::WillChangeStyleElement(Element* element) {
  resource_container_->EraseStyleElementContent(element->GetDomNodeId());
}

protocol::Response InspectorCSSAgent::startRuleUsageTracking() {
  coverage_enabled_.Set(true);
  SetCoverageEnabled(true);

  for (Document* document : dom_agent_->Documents()) {
    document->GetStyleEngine().MarkAllElementsForStyleRecalc(
        StyleChangeReasonForTracing::Create(style_change_reason::kInspector));
    document->UpdateStyleAndLayoutTree();
  }

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::trackComputedStyleUpdatesForNode(
    std::optional<int> node_id) {
  if (node_id.has_value()) {
    node_id_for_computed_style_updated_events_ = node_id.value();
  } else {
    node_id_for_computed_style_updated_events_ = std::nullopt;
  }

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::stopRuleUsageTracking(
    std::unique_ptr<protocol::Array<protocol::CSS::RuleUsage>>* result) {
  for (Document* document : dom_agent_->Documents())
    document->UpdateStyleAndLayoutTree();
  double timestamp;
  protocol::Response response = takeCoverageDelta(result, &timestamp);
  SetCoverageEnabled(false);
  return response;
}

void InspectorCSSAgent::BuildRulesMap(
    InspectorStyleSheet* style_sheet,
    HeapHashMap<Member<const StyleRule>, Member<CSSStyleRule>>*
        rule_to_css_rule) {
  const CSSRuleVector& css_rules = style_sheet->FlatRules();
  for (auto css_rule : css_rules) {
    if (css_rule->GetType() == CSSRule::kStyleRule) {
      CSSStyleRule* css_style_rule = DynamicTo<CSSStyleRule>(css_rule.Get());
      rule_to_css_rule->Set(css_style_rule->GetStyleRule(), css_style_rule);
    }
    if (css_rule->GetType() == CSSRule::kImportRule) {
      CSSImportRule* css_import_rule = DynamicTo<CSSImportRule>(css_rule.Get());
      if (!css_import_rule->styleSheet())
        continue;
      auto it = css_style_sheet_to_inspector_style_sheet_.find(
          const_cast<CSSStyleSheet*>(css_import_rule->styleSheet()));
      if (it == css_style_sheet_to_inspector_style_sheet_.end())
        continue;
      InspectorStyleSheet* imported_style_sheet = it->value;
      BuildRulesMap(imported_style_sheet, rule_to_css_rule);
    }
  }
}

protocol::Response InspectorCSSAgent::takeCoverageDelta(
    std::unique_ptr<protocol::Array<protocol::CSS::RuleUsage>>* result,
    double* out_timestamp) {
  if (!tracker_) {
    return protocol::Response::ServerError(
        "CSS rule usage tracking is not enabled");
  }

  StyleRuleUsageTracker::RuleListByStyleSheet coverage_delta =
      tracker_->TakeDelta();

  *out_timestamp = base::TimeTicks::Now().since_origin().InSecondsF();

  *result = std::make_unique<protocol::Array<protocol::CSS::RuleUsage>>();

  for (const auto& entry : coverage_delta) {
    const CSSStyleSheet* css_style_sheet = entry.key.Get();

    auto style_sheet_it = css_style_sheet_to_inspector_style_sheet_.find(
        const_cast<CSSStyleSheet*>(css_style_sheet));
    if (style_sheet_it == css_style_sheet_to_inspector_style_sheet_.end())
      continue;
    InspectorStyleSheet* style_sheet = style_sheet_it->value;

    HeapHashMap<Member<const StyleRule>, Member<CSSStyleRule>> rule_to_css_rule;
    BuildRulesMap(style_sheet, &rule_to_css_rule);

    for (auto used_rule : *entry.value) {
      auto rule_to_css_rule_it = rule_to_css_rule.find(used_rule);
      if (rule_to_css_rule_it == rule_to_css_rule.end())
        continue;
      CSSStyleRule* css_style_rule = rule_to_css_rule_it->value;
      auto it = css_style_sheet_to_inspector_style_sheet_.find(
          const_cast<CSSStyleSheet*>(css_style_rule->parentStyleSheet()));
      if (it == css_style_sheet_to_inspector_style_sheet_.end())
        continue;
      // If the rule comes from an @import'ed file, the `rule_style_sheet` is
      // different from `style_sheet`.
      InspectorStyleSheet* rule_style_sheet = it->value;
      CSSRule* rule = css_style_rule;
      while (rule) {
        if (std::unique_ptr<protocol::CSS::RuleUsage> rule_usage_object =
                rule_style_sheet->BuildObjectForRuleUsage(rule, true)) {
          (*result)->emplace_back(std::move(rule_usage_object));
        }
        rule = rule->parentRule();
      }
    }
  }

  return protocol::Response::Success();
}

protocol::Response InspectorCSSAgent::trackComputedStyleUpdates(
    std::unique_ptr<protocol::Array<protocol::CSS::CSSComputedStyleProperty>>
        properties_to_track) {
  tracked_computed_styles_.clear();
  if (properties_to_track->size() == 0) {
    if (computed_style_updated_callback_) {
      computed_style_updated_callback_->sendSuccess(
          BuildArrayForComputedStyleUpdatedNodes());
      computed_style_updated_callback_ = nullptr;
    }
    computed_style_updated_node_ids_.clear();
    return protocol::Response::Success();
  }

  for (const auto& property : *properties_to_track) {
    String property_name = property->getName();
    HashMap<String, HashSet<String>>::iterator it =
        tracked_computed_styles_.find(property_name);
    if (it != tracked_computed_styles_.end()) {
      it->value.insert(property->getValue());
    } else {
      HashSet<String> tracked_values;
      tracked_values.insert(property->getValue());
      tracked_computed_styles_.Set(property_name, tracked_values);
    }
  }

  return protocol::Response::Success();
}

void InspectorCSSAgent::takeComputedStyleUpdates(
    std::unique_ptr<TakeComputedStyleUpdatesCallback> callback) {
  if (tracked_computed_styles_.empty()) {
    callback->sendFailure(protocol::Response::ServerError(
        "No computed styles are being tracked right now."));
    return;
  }

  if (computed_style_updated_callback_) {
    callback->sendFailure(protocol::Response::ServerError(
        "A previous request has not been resolved yet."));
    return;
  }

  if (computed_style_updated_node_ids_.size()) {
    callback->sendSuccess(BuildArrayForComputedStyleUpdatedNodes());
    computed_style_updated_node_ids_.clear();
    return;
  }

  computed_style_updated_callback_ = std::move(callback);
}

void InspectorCSSAgent::NotifyComputedStyleUpdatedForNode(int node_id) {
  if (!notify_computed_style_updated_node_ids_.Contains(node_id)) {
    return;
  }

  notify_computed_style_updated_node_ids_.erase(node_id);
  if (!node_id_for_computed_style_updated_events_.has_value() ||
      node_id_for_computed_style_updated_events_.value() != node_id) {
    return;
  }

  GetFrontend()->computedStyleUpdated(node_id);
}

void InspectorCSSAgent::DidUpdateComputedStyle(Element* element,
                                               const ComputedStyle* old_style,
                                               const ComputedStyle* new_style) {
  if (tracked_computed_styles_.empty() &&
      !node_id_for_computed_style_updated_events_.has_value()) {
    return;
  }

  int id = dom_agent_->BoundNodeId(element);
  // If node is not mapped yet -> ignore the event.
  if (!id)
    return;

  // If the updated computed styles belong to the tracked node,
  // schedule a task to send `computedStyleUpdated` event.
  if (node_id_for_computed_style_updated_events_.has_value() &&
      node_id_for_computed_style_updated_events_.value() == id &&
      !notify_computed_style_updated_node_ids_.Contains(id)) {
    notify_computed_style_updated_node_ids_.insert(id);
    scoped_refptr<base::SingleThreadTaskRunner> task_runner =
        inspected_frames_->Root()->GetTaskRunner(TaskType::kInternalInspector);
    task_runner->PostDelayedTask(
        FROM_HERE,
        WTF::BindOnce(&InspectorCSSAgent::NotifyComputedStyleUpdatedForNode,
                      WrapPersistent(weak_factory_.GetWeakCell()), id),
        base::Milliseconds(50));
  }

  bool has_both_old_and_new_style = old_style && new_style;
  if (tracked_computed_styles_.empty() || !has_both_old_and_new_style) {
    return;
  }

  if (computed_style_updated_node_ids_.Contains(id))
    return;

  // Compares with the currently tracked styles to see if this node should be
  // included
  for (const auto& tracked_computed_style : tracked_computed_styles_) {
    const HashSet<String>& tracked_values = tracked_computed_style.value;
    CSSPropertyRef ref(tracked_computed_style.key, element->GetDocument());
    if (!ref.IsValid())
      continue;
    const CSSProperty& tracked_property = ref.GetProperty();
    // TODO(crbug/1108356): consider using the Prepared Value once it's ready
    const CSSValue* old_value = old_style
                                    ? ComputedStyleUtils::ComputedPropertyValue(
                                          tracked_property, *old_style)
                                    : nullptr;
    const CSSValue* new_value = new_style
                                    ? ComputedStyleUtils::ComputedPropertyValue(
                                          tracked_property, *new_style)
                                    : nullptr;
    if (old_value == new_value)
      continue;
    String old_value_text = old_value ? old_value->CssText() : "";
    String new_value_text = new_value ? new_value->CssText() : "";
    if (old_value_text == new_value_text)
      continue;
    if (tracked_values.Contains(old_value_text) ||
        tracked_values.Contains(new_value_text)) {
      computed_style_updated_node_ids_.insert(id);
      return;
    }
  }
}

void InspectorCSSAgent::Will(const probe::RecalculateStyle&) {}

void InspectorCSSAgent::Did(const probe::RecalculateStyle&) {
  if (computed_style_updated_callback_ &&
      computed_style_updated_node_ids_.size()) {
    computed_style_updated_callback_->sendSuccess(
        BuildArrayForComputedStyleUpdatedNodes());
    computed_style_updated_node_ids_.clear();
    computed_style_updated_callback_ = nullptr;
  }
}

std::unique_ptr<protocol::Array<int>>
InspectorCSSAgent::BuildArrayForComputedStyleUpdatedNodes() {
  std::unique_ptr<protocol::Array<int>> nodes =
      std::make_unique<protocol::Array<int>>();
  for (int node_id : computed_style_updated_node_ids_) {
    nodes->push_back(node_id);
  }
  return nodes;
}

void InspectorCSSAgent::Trace(Visitor* visitor) const {
  visitor->Trace(dom_agent_);
  visitor->Trace(inspected_frames_);
  visitor->Trace(network_agent_);
  visitor->Trace(resource_content_loader_);
  visitor->Trace(resource_container_);
  visitor->Trace(id_to_inspector_style_sheet_);
  visitor->Trace(id_to_inspector_style_sheet_for_inline_style_);
  visitor->Trace(css_style_sheet_to_inspector_style_sheet_);
  visitor->Trace(document_to_css_style_sheets_);
  visitor->Trace(invalidated_documents_);
  visitor->Trace(node_to_inspector_style_sheet_);
  visitor->Trace(inspector_user_agent_style_sheet_);
  visitor->Trace(user_agent_view_transition_style_sheet_);
  visitor->Trace(tracker_);
  visitor->Trace(weak_factory_);
  visitor->Trace(default_inspector_stylesheets_);
  InspectorBaseAgent::Trace(visitor);
}

void InspectorCSSAgent::LocalFontsEnabled(bool* result) {
  if (!local_fonts_enabled_.Get())
    *result = false;
}

protocol::Response InspectorCSSAgent::setLocalFontsEnabled(bool enabled) {
  local_fonts_enabled_.Set(enabled);
  // TODO(alexrudenko): how to rerender fonts so that
  // local_fonts_enabled_ applies without page reload?
  return protocol::Response::Success();
}

}  // namespace blink
