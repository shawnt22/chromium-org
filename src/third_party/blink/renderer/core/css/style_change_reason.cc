// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_change_reason.h"

#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/wtf/static_constructors.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

namespace style_change_reason {
const char kAccessibility[] = "Accessibility";
const char kActiveStylesheetsUpdate[] = "ActiveStylesheetsUpdate";
const char kAffectedByHas[] = "Affected by :has()";
const char kAnimation[] = "Animation";
const char kAttribute[] = "Attribute";
const char kConditionalBackdrop[] = "Conditional ::backdrop";
const char kControl[] = "Control";
const char kControlValue[] = "ControlValue";
const char kDeclarativeContent[] = "Extension declarativeContent.css";
const char kDesignMode[] = "DesignMode";
const char kDialog[] = "Dialog";
const char kDisplayLock[] = "DisplayLock";
const char kEditContext[] = "EditContext";
const char kEnvironmentVariableChanged[] = "EnvironmentVariableChanged";
const char kViewTransition[] = "ViewTransition";
const char kFlatTreeChange[] = "FlatTreeChange";
const char kFonts[] = "Fonts";
const char kFrame[] = "Frame";
const char kFullscreen[] = "Fullscreen";
const char kFunctionRuleChange[] = "@function rule change";
const char kInheritedStyleChangeFromParentFrame[] =
    "InheritedStyleChangeFromParentFrame";
const char kInlineCSSStyleMutated[] =
    "Inline CSS style declaration was mutated";
const char kInspector[] = "Inspector";
const char kKeyframesRuleChange[] = "@keyframes rule change";
const char kLanguage[] = "Language";
const char kLinkColorChange[] = "LinkColorChange";
const char kMediaQuery[] = "Media Query changed";
const char kNodeInserted[] = "Node was inserted into tree";
const char kPictureSourceChanged[] = "PictureSourceChange";
const char kPlatformColorChange[] = "PlatformColorChange";
const char kPlaceElement[] = "placeElement";
const char kPluginChanged[] = "Plugin Changed";
const char kPopoverVisibilityChange[] = "Popover Visibility Change";
const char kPositionTryChange[] = "@position-try change";
const char kPrinting[] = "Printing";
const char kPropertyRegistration[] = "PropertyRegistration";
const char kPseudoClass[] = "PseudoClass";
const char kRelatedStyleRule[] = "Related style rule";
const char kScrollTimeline[] = "ScrollTimeline";
const char kSVGContainerSizeChange[] = "SVGContainerSizeChange";
const char kSettings[] = "Settings";
const char kShadow[] = "Shadow";
const char kStyleAttributeChange[] = "Style attribute change";
const char kStyleRuleChange[] = "Style rule change";
const char kTopLayer[] = "TopLayer";
const char kUseFallback[] = "UseFallback";
const char kViewportDefiningElement[] = "ViewportDefiningElement";
const char kViewportUnits[] = "ViewportUnits";
const char kVisuallyOrdered[] = "VisuallyOrdered";
const char kWritingModeChange[] = "WritingModeChange";
const char kZoom[] = "Zoom";
}  // namespace style_change_reason

namespace style_change_extra_data {
DEFINE_GLOBAL(, AtomicString, g_active);
DEFINE_GLOBAL(, AtomicString, g_active_view_transition);
DEFINE_GLOBAL(, AtomicString, g_active_view_transition_type);
DEFINE_GLOBAL(, AtomicString, g_disabled);
DEFINE_GLOBAL(, AtomicString, g_drag);
DEFINE_GLOBAL(, AtomicString, g_focus);
DEFINE_GLOBAL(, AtomicString, g_focus_visible);
DEFINE_GLOBAL(, AtomicString, g_focus_within);
DEFINE_GLOBAL(, AtomicString, g_hover);
DEFINE_GLOBAL(, AtomicString, g_past);
DEFINE_GLOBAL(, AtomicString, g_unresolved);

void Init() {
  DCHECK(IsMainThread());

  new (base::NotNullTag::kNotNull, (void*)&g_active) AtomicString(":active");
  new (base::NotNullTag::kNotNull, (void*)&g_active_view_transition)
      AtomicString(":active_view_transition");
  new (base::NotNullTag::kNotNull, (void*)&g_active_view_transition_type)
      AtomicString(":active_view_transition_type");
  new (base::NotNullTag::kNotNull, (void*)&g_disabled)
      AtomicString(":disabled");
  new (base::NotNullTag::kNotNull, (void*)&g_drag)
      AtomicString(":-webkit-drag");
  new (base::NotNullTag::kNotNull, (void*)&g_focus) AtomicString(":focus");
  new (base::NotNullTag::kNotNull, (void*)&g_focus_visible)
      AtomicString(":focus-visible");
  new (base::NotNullTag::kNotNull, (void*)&g_focus_within)
      AtomicString(":focus-within");
  new (base::NotNullTag::kNotNull, (void*)&g_hover) AtomicString(":hover");
  new (base::NotNullTag::kNotNull, (void*)&g_past) AtomicString(":past");
  new (base::NotNullTag::kNotNull, (void*)&g_unresolved)
      AtomicString(":unresolved");
}

}  // namespace style_change_extra_data

}  // namespace blink
