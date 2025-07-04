// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/css/css_animations.h"

#include "cc/animation/animation.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_animation_trigger_type.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_timeline_range_offset.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_cssnumericvalue_double.h"
#include "third_party/blink/renderer/core/animation/animation.h"
#include "third_party/blink/renderer/core/animation/animation_test_helpers.h"
#include "third_party/blink/renderer/core/animation/css/css_animation.h"
#include "third_party/blink/renderer/core/animation/document_timeline.h"
#include "third_party/blink/renderer/core/animation/element_animations.h"
#include "third_party/blink/renderer/core/css/cssom/css_numeric_value.h"
#include "third_party/blink/renderer/core/dom/dom_token_list.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/animation/compositor_animation.h"
#include "third_party/blink/renderer/platform/animation/compositor_animation_delegate.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/testing/paint_test_configurations.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace {

const double kTolerance = 1e-5;

const double kTimeToleranceMilliseconds = 0.1;
}

namespace blink {

class CSSAnimationsTest : public RenderingTest, public PaintTestConfigurations {
 public:
  CSSAnimationsTest()
      : RenderingTest(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    EnablePlatform();
    platform()->SetThreadedAnimationEnabled(true);
  }

  void SetUp() override {
    EnableCompositing();
    RenderingTest::SetUp();
    SetUpAnimationClockForTesting();
    // Advance timer to document time.
    AdvanceClock(
        base::Seconds(GetDocument().Timeline().ZeroTime().InSecondsF()));
  }

  void TearDown() override {
    platform()->RunUntilIdle();
    RenderingTest::TearDown();
  }

  base::TimeTicks TimelineTime() { return platform()->NowTicks(); }

  void StartAnimationOnCompositor(Animation* animation) {
    static_cast<CompositorAnimationDelegate*>(animation)
        ->NotifyAnimationStarted(TimelineTime().since_origin(),
                                 animation->CompositorGroup());
  }

  void AdvanceClockSeconds(double seconds) {
    PageTestBase::AdvanceClock(base::Seconds(seconds));
    platform()->RunUntilIdle();
    GetPage().Animator().ServiceScriptedAnimations(platform()->NowTicks());
  }

  double GetContrastFilterAmount(Element* element) {
    EXPECT_EQ(1u, element->GetComputedStyle()->Filter().size());
    const FilterOperation* filter =
        element->GetComputedStyle()->Filter().Operations()[0];
    EXPECT_EQ(FilterOperation::OperationType::kContrast, filter->GetType());
    return static_cast<const BasicComponentTransferFilterOperation*>(filter)
        ->Amount();
  }

  double GetSaturateFilterAmount(Element* element) {
    EXPECT_EQ(1u, element->GetComputedStyle()->Filter().size());
    const FilterOperation* filter =
        element->GetComputedStyle()->Filter().Operations()[0];
    EXPECT_EQ(FilterOperation::OperationType::kSaturate, filter->GetType());
    return static_cast<const BasicColorMatrixFilterOperation*>(filter)
        ->Amount();
  }

  void InvalidateCompositorKeyframesSnapshot(Animation* animation) {
    auto* keyframe_effect = DynamicTo<KeyframeEffect>(animation->effect());
    DCHECK(keyframe_effect);
    DCHECK(keyframe_effect->Model());
    keyframe_effect->Model()->InvalidateCompositorKeyframesSnapshot();
  }

  bool IsUseCounted(mojom::WebFeature feature) {
    return GetDocument().IsUseCounted(feature);
  }

  void ClearUseCounter(mojom::WebFeature feature) {
    GetDocument().ClearUseCounterForTesting(feature);
    DCHECK(!IsUseCounted(feature));
  }

  wtf_size_t DeferredTimelinesCount(Element* element) const {
    ElementAnimations* element_animations = element->GetElementAnimations();
    if (!element_animations) {
      return 0;
    }
    CSSAnimations& css_animations = element_animations->CssAnimations();
    return css_animations.timeline_data_.GetDeferredTimelines().size();
  }

 private:
  void SetUpAnimationClockForTesting() {
    GetPage().Animator().Clock().ResetTimeForTesting();
    GetDocument().Timeline().ResetForTesting();
  }
};

INSTANTIATE_PAINT_TEST_SUITE_P(CSSAnimationsTest);

// Verify that a composited animation is retargeted according to its composited
// time.
TEST_P(CSSAnimationsTest, RetargetedTransition) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #test { transition: filter linear 1s; }
      .contrast1 { filter: contrast(50%); }
      .contrast2 { filter: contrast(0%); }
    </style>
    <div id='test'>TEST</div>
  )HTML");
  Element* element = GetDocument().getElementById(AtomicString("test"));
  element->setAttribute(html_names::kClassAttr, AtomicString("contrast1"));
  UpdateAllLifecyclePhasesForTest();
  ElementAnimations* animations = element->GetElementAnimations();
  EXPECT_EQ(1u, animations->Animations().size());
  Animation* animation = (*animations->Animations().begin()).key;
  // Start animation on compositor and advance .8 seconds.
  StartAnimationOnCompositor(animation);
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
  AdvanceClockSeconds(0.8);

  // Starting the second transition should retarget the active transition.
  element->setAttribute(html_names::kClassAttr, AtomicString("contrast2"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_NEAR(0.6, GetContrastFilterAmount(element), kTolerance);

  // As it has been retargeted, advancing halfway should go to 0.3.
  AdvanceClockSeconds(0.5);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_NEAR(0.3, GetContrastFilterAmount(element), kTolerance);
}

// Test that when an incompatible in progress compositor transition
// would be retargeted it does not incorrectly combine with a new
// transition target.
TEST_P(CSSAnimationsTest, IncompatibleRetargetedTransition) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #test { transition: filter 1s linear; }
      .saturate { filter: saturate(20%); }
      .contrast { filter: contrast(20%); }
    </style>
    <div id='test'>TEST</div>
  )HTML");
  Element* element = GetDocument().getElementById(AtomicString("test"));
  element->setAttribute(html_names::kClassAttr, AtomicString("saturate"));
  UpdateAllLifecyclePhasesForTest();
  ElementAnimations* animations = element->GetElementAnimations();
  EXPECT_EQ(1u, animations->Animations().size());
  Animation* animation = (*animations->Animations().begin()).key;

  // Start animation on compositor and advance partially.
  StartAnimationOnCompositor(animation);
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
  AdvanceClockSeconds(0.003);

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FLOAT_EQ(1.0 * (1 - 0.003) + 0.2 * 0.003,
                  GetSaturateFilterAmount(element));

  // Now we start a contrast filter. Since it will try to combine with
  // the in progress saturate filter, and be incompatible, there should
  // be no transition and should immediately apply on the next frame.
  element->setAttribute(html_names::kClassAttr, AtomicString("contrast"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(0.2, GetContrastFilterAmount(element));
}

// Verifies that newly created/cancelled transitions are both taken into
// account when setting the flags. (The filter property is an
// arbitrarily chosen sample).
TEST_P(CSSAnimationsTest, AnimationFlags_Transitions) {
  SetBodyInnerHTML(R"HTML(
    <style>
      #test {
        filter: contrast(20%);
        transition: filter 1s;
      }
      #test.contrast30 { filter: contrast(30%); }
      #test.unrelated { color: green; }
      #test.cancel { transition: none; }
    </style>
    <div id=test></div>
  )HTML");
  Element* element = GetDocument().getElementById(AtomicString("test"));
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentFilterAnimation());

  // Newly created transition:
  element->setAttribute(html_names::kClassAttr, AtomicString("contrast30"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentFilterAnimation());

  // Already running (and unmodified) transition:
  element->setAttribute(html_names::kClassAttr,
                        AtomicString("contrast30 unrelated"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentFilterAnimation());

  // Cancelled transition:
  element->setAttribute(html_names::kClassAttr, AtomicString("cancel"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentFilterAnimation());
}

// Verifies that newly created/updated CSS/JS animations are all taken into
// account when setting the flags. (The filter/opacity/transform properties are
// arbitrarily chosen samples).
TEST_P(CSSAnimationsTest, AnimationFlags_Animations) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes anim {
        from { opacity: 1; }
        to { opacity: 0; }
      }
      #test.animate { animation: anim 1s; }
      #test.newtiming { animation-duration: 2s; }
      #test.unrelated { color: green; }
      #test.cancel { animation: none; }
    </style>
    <div id=test></div>
  )HTML");
  Element* element = GetDocument().getElementById(AtomicString("test"));
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentOpacityAnimation());
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentTransformAnimation());

  // Newly created animation:
  element->setAttribute(html_names::kClassAttr, AtomicString("animate"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentOpacityAnimation());
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentTransformAnimation());

  // Already running (and unmodified) animation:
  element->setAttribute(html_names::kClassAttr,
                        AtomicString("animate unrelated"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentOpacityAnimation());
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentTransformAnimation());

  // Add a JS animation:
  auto* effect = animation_test_helpers::CreateSimpleKeyframeEffectForTest(
      element, CSSPropertyID::kTransform, "scale(1)", "scale(2)");
  GetDocument().Timeline().Play(effect);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentOpacityAnimation());
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentTransformAnimation());

  // Update CSS animation:
  element->setAttribute(html_names::kClassAttr,
                        AtomicString("animate newtiming"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentOpacityAnimation());
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentTransformAnimation());

  // Cancel CSS animation:
  element->setAttribute(html_names::kClassAttr, AtomicString("cancel"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(element->ComputedStyleRef().HasCurrentOpacityAnimation());
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentTransformAnimation());
}

namespace {

bool OpacityFlag(const ComputedStyle& style) {
  return style.HasCurrentOpacityAnimation();
}
bool TransformFlag(const ComputedStyle& style) {
  return style.HasCurrentTransformAnimation();
}
bool ScaleFlag(const ComputedStyle& style) {
  return style.HasCurrentScaleAnimation();
}
bool RotateFlag(const ComputedStyle& style) {
  return style.HasCurrentRotateAnimation();
}
bool TranslateFlag(const ComputedStyle& style) {
  return style.HasCurrentTranslateAnimation();
}
bool FilterFlag(const ComputedStyle& style) {
  return style.HasCurrentFilterAnimation();
}
bool BackdropFilterFlag(const ComputedStyle& style) {
  return style.HasCurrentBackdropFilterAnimation();
}
bool BackgroundColorFlag(const ComputedStyle& style) {
  return style.HasCurrentBackgroundColorAnimation();
}

bool CompositedOpacityFlag(const ComputedStyle& style) {
  return style.IsRunningOpacityAnimationOnCompositor();
}
bool CompositedTransformFlag(const ComputedStyle& style) {
  return style.IsRunningTransformAnimationOnCompositor();
}
bool CompositedScaleFlag(const ComputedStyle& style) {
  return style.IsRunningScaleAnimationOnCompositor();
}
bool CompositedRotateFlag(const ComputedStyle& style) {
  return style.IsRunningRotateAnimationOnCompositor();
}
bool CompositedTranslateFlag(const ComputedStyle& style) {
  return style.IsRunningTranslateAnimationOnCompositor();
}
bool CompositedFilterFlag(const ComputedStyle& style) {
  return style.IsRunningFilterAnimationOnCompositor();
}
bool CompositedBackdropFilterFlag(const ComputedStyle& style) {
  return style.IsRunningBackdropFilterAnimationOnCompositor();
}

using FlagFunction = bool (*)(const ComputedStyle&);

struct FlagData {
  const char* property;
  const char* before;
  const char* after;
  FlagFunction get_flag;
};

FlagData flag_data[] = {
    {"opacity", "0", "1", OpacityFlag},
    {"transform", "scale(1)", "scale(2)", TransformFlag},
    {"rotate", "10deg", "20deg", RotateFlag},
    {"scale", "1", "2", ScaleFlag},
    {"translate", "10px", "20px", TranslateFlag},
    {"filter", "contrast(10%)", "contrast(20%)", FilterFlag},
    {"backdrop-filter", "blur(10px)", "blur(20px)", BackdropFilterFlag},
    {"background-color", "red", "blue", BackgroundColorFlag},
};

FlagData compositor_flag_data[] = {
    {"opacity", "0", "1", CompositedOpacityFlag},
    {"transform", "scale(1)", "scale(2)", CompositedTransformFlag},
    {"scale", "1", "2", CompositedScaleFlag},
    {"rotate", "45deg", "90deg", CompositedRotateFlag},
    {"translate", "10px 0px", "10px 20px", CompositedTranslateFlag},
    {"filter", "contrast(10%)", "contrast(20%)", CompositedFilterFlag},
    {"backdrop-filter", "blur(10px)", "blur(20px)",
     CompositedBackdropFilterFlag},
};

String GenerateTransitionHTMLFrom(const FlagData& data) {
  const char* property = data.property;
  const char* before = data.before;
  const char* after = data.after;

  StringBuilder builder;
  builder.Append("<style>");
  builder.Append(String::Format("#test { transition:%s 1s; }", property));
  builder.Append(String::Format("#test.before { %s:%s; }", property, before));
  builder.Append(String::Format("#test.after { %s:%s; }", property, after));
  builder.Append("</style>");
  builder.Append("<div id=test class=before>Test</div>");
  return builder.ToString();
}

String GenerateCSSAnimationHTMLFrom(const FlagData& data) {
  const char* property = data.property;
  const char* before = data.before;
  const char* after = data.after;

  StringBuilder builder;
  builder.Append("<style>");
  builder.Append("@keyframes anim {");
  builder.Append(String::Format("from { %s:%s; }", property, before));
  builder.Append(String::Format("to { %s:%s; }", property, after));
  builder.Append("}");
  builder.Append("#test.after { animation:anim 1s; }");
  builder.Append("</style>");
  builder.Append("<div id=test>Test</div>");
  return builder.ToString();
}

}  // namespace

// Verify that HasCurrent*Animation flags are set for transitions.
TEST_P(CSSAnimationsTest, AllAnimationFlags_Transitions) {
  for (FlagData data : flag_data) {
    String html = GenerateTransitionHTMLFrom(data);
    SCOPED_TRACE(html);

    SetBodyInnerHTML(html);
    Element* element = GetDocument().getElementById(AtomicString("test"));
    ASSERT_TRUE(element);
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    element->setAttribute(html_names::kClassAttr, AtomicString("after"));
    UpdateAllLifecyclePhasesForTest();
    EXPECT_TRUE(data.get_flag(element->ComputedStyleRef()));
  }
}

// Verify that IsRunning*AnimationOnCompositor flags are set for transitions.
TEST_P(CSSAnimationsTest, AllAnimationFlags_Transitions_Compositor) {
  for (FlagData data : compositor_flag_data) {
    String html = GenerateTransitionHTMLFrom(data);
    SCOPED_TRACE(html);

    SetBodyInnerHTML(html);
    Element* element = GetDocument().getElementById(AtomicString("test"));
    ASSERT_TRUE(element);
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    element->setAttribute(html_names::kClassAttr, AtomicString("after"));
    UpdateAllLifecyclePhasesForTest();
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    ElementAnimations* animations = element->GetElementAnimations();
    ASSERT_EQ(1u, animations->Animations().size());
    Animation* animation = (*animations->Animations().begin()).key;
    StartAnimationOnCompositor(animation);
    AdvanceClockSeconds(0.1);
    UpdateAllLifecyclePhasesForTest();
    EXPECT_TRUE(data.get_flag(element->ComputedStyleRef()));
  }
}

// Verify that HasCurrent*Animation flags are set for CSS animations.
TEST_P(CSSAnimationsTest, AllAnimationFlags_CSSAnimations) {
  for (FlagData data : flag_data) {
    String html = GenerateCSSAnimationHTMLFrom(data);
    SCOPED_TRACE(html);

    SetBodyInnerHTML(html);
    Element* element = GetDocument().getElementById(AtomicString("test"));
    ASSERT_TRUE(element);
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    element->setAttribute(html_names::kClassAttr, AtomicString("after"));
    UpdateAllLifecyclePhasesForTest();
    EXPECT_TRUE(data.get_flag(element->ComputedStyleRef()));
  }
}

// Verify that IsRunning*AnimationOnCompositor flags are set for CSS animations.
TEST_P(CSSAnimationsTest, AllAnimationFlags_CSSAnimations_Compositor) {
  for (FlagData data : compositor_flag_data) {
    String html = GenerateCSSAnimationHTMLFrom(data);
    SCOPED_TRACE(html);

    SetBodyInnerHTML(html);
    Element* element = GetDocument().getElementById(AtomicString("test"));
    ASSERT_TRUE(element);
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    element->setAttribute(html_names::kClassAttr, AtomicString("after"));
    UpdateAllLifecyclePhasesForTest();
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    ElementAnimations* animations = element->GetElementAnimations();
    ASSERT_EQ(1u, animations->Animations().size());
    Animation* animation = (*animations->Animations().begin()).key;
    StartAnimationOnCompositor(animation);
    AdvanceClockSeconds(0.1);
    UpdateAllLifecyclePhasesForTest();
    EXPECT_TRUE(data.get_flag(element->ComputedStyleRef()));
  }
}

// Verify that HasCurrent*Animation flags are set for JS animations.
TEST_P(CSSAnimationsTest, AllAnimationFlags_JSAnimations) {
  for (FlagData data : flag_data) {
    SCOPED_TRACE(data.property);

    SetBodyInnerHTML("<div id=test>Test</div>");
    Element* element = GetDocument().getElementById(AtomicString("test"));
    ASSERT_TRUE(element);
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    CSSPropertyID property_id =
        CssPropertyID(GetDocument().GetExecutionContext(), data.property);
    ASSERT_TRUE(IsValidCSSPropertyID(property_id));
    auto* effect = animation_test_helpers::CreateSimpleKeyframeEffectForTest(
        element, property_id, data.before, data.after);
    GetDocument().Timeline().Play(effect);

    UpdateAllLifecyclePhasesForTest();
    EXPECT_TRUE(data.get_flag(element->ComputedStyleRef()));
  }
}

// Verify that IsRunning*AnimationOnCompositor flags are set for JS animations.
TEST_P(CSSAnimationsTest, AllAnimationFlags_JSAnimations_Compositor) {
  for (FlagData data : compositor_flag_data) {
    SCOPED_TRACE(data.property);

    SetBodyInnerHTML("<div id=test>Test</div>");
    Element* element = GetDocument().getElementById(AtomicString("test"));
    ASSERT_TRUE(element);
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    CSSPropertyID property_id =
        CssPropertyID(GetDocument().GetExecutionContext(), data.property);
    ASSERT_TRUE(IsValidCSSPropertyID(property_id));
    auto* effect = animation_test_helpers::CreateSimpleKeyframeEffectForTest(
        element, property_id, data.before, data.after);
    Animation* animation = GetDocument().Timeline().Play(effect);
    UpdateAllLifecyclePhasesForTest();
    EXPECT_FALSE(data.get_flag(element->ComputedStyleRef()));

    StartAnimationOnCompositor(animation);
    AdvanceClockSeconds(0.1);
    UpdateAllLifecyclePhasesForTest();
    EXPECT_TRUE(data.get_flag(element->ComputedStyleRef()));
  }
}

TEST_P(CSSAnimationsTest, CompositedAnimationUpdateCausesPaintInvalidation) {
  ScopedCompositeBGColorAnimationForTest scoped_feature(true);

  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes anim {
        from { background-color: green; }
        to { background-color: red; }
      }
      #test { background-color: black; }
      #test.animate { animation: anim 1s; }
      #test.newtiming { animation-duration: 2s; }
      #test.unrelated { --unrelated:1; }
    </style>
    <div id=test>Test</div>
  )HTML");

  Element* element = GetDocument().getElementById(AtomicString("test"));
  LayoutObject* lo = element->GetLayoutObject();
  ASSERT_TRUE(element);

  // Not animating yet:
  EXPECT_FALSE(
      element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());

  // Newly created CSS animation:
  element->classList().Add(AtomicString("animate"));
  GetDocument().View()->UpdateLifecycleToCompositingInputsClean(
      DocumentUpdateReason::kTest);
  EXPECT_TRUE(lo->ShouldDoFullPaintInvalidation());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());
  // Do an unrelated change to clear the flag.
  element->classList().toggle(AtomicString("unrelated"), ASSERT_NO_EXCEPTION);
  GetDocument().View()->UpdateLifecycleToCompositingInputsClean(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(lo->ShouldDoFullPaintInvalidation());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());

  // Updated CSS animation:
  element->classList().Add(AtomicString("newtiming"));
  GetDocument().View()->UpdateLifecycleToCompositingInputsClean(
      DocumentUpdateReason::kTest);
  EXPECT_TRUE(lo->ShouldDoFullPaintInvalidation());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());

  // Do an unrelated change to clear the flag.
  element->classList().toggle(AtomicString("unrelated"), ASSERT_NO_EXCEPTION);
  GetDocument().View()->UpdateLifecycleToCompositingInputsClean(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(lo->ShouldDoFullPaintInvalidation());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());

  // Modify the animation outside of a style resolve:
  ElementAnimations* animations = element->GetElementAnimations();
  ASSERT_EQ(1u, animations->Animations().size());
  Animation* animation = (*animations->Animations().begin()).key;
  animation->setStartTime(MakeGarbageCollected<V8CSSNumberish>(0.5),
                          ASSERT_NO_EXCEPTION);
  EXPECT_TRUE(animation->CompositorPending());
  GetDocument().View()->UpdateLifecycleToCompositingInputsClean(
      DocumentUpdateReason::kTest);
  EXPECT_TRUE(lo->ShouldDoFullPaintInvalidation());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());
  EXPECT_FALSE(animation->CompositorPending());

  // Do an unrelated change to clear the flag.
  element->classList().toggle(AtomicString("unrelated"), ASSERT_NO_EXCEPTION);
  GetDocument().View()->UpdateLifecycleToCompositingInputsClean(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(lo->ShouldDoFullPaintInvalidation());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentBackgroundColorAnimation());
}

TEST_P(CSSAnimationsTest, UpdateAnimationFlags_AnimatingElement) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes anim {
        from { transform: scale(1); }
        to { transform: scale(2); }
      }
      #test {
        animation: anim 1s linear;
      }
      #test::before {
        content: "A";
        /* Ensure that we don't early-out in StyleResolver::
           ApplyAnimatedStyle */
        animation: unknown 1s linear;
      }
    </style>
    <div id=test>Test</div>
  )HTML");

  Element* element = GetDocument().getElementById(AtomicString("test"));
  ASSERT_TRUE(element);

  Element* before = element->GetPseudoElement(kPseudoIdBefore);
  ASSERT_TRUE(before);

  // The originating element should be marked having a current transform
  // animation ...
  EXPECT_TRUE(element->ComputedStyleRef().HasCurrentTransformAnimation());

  // ... but the pseudo-element should not.
  EXPECT_FALSE(before->ComputedStyleRef().HasCurrentTransformAnimation());
}

TEST_P(CSSAnimationsTest, CSSTransitionBlockedByAnimationUseCounter) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes anim {
        from { z-index: 10; }
        to { z-index: 20; }
      }
      #test {
        z-index: 0;
        transition: z-index 100s steps(2, start);
      }
      #test.animate {
        animation: anim 100s steps(2, start);
      }
      #test.change {
        z-index: 100;
      }
    </style>
    <div id=test class=animate>Test</div>
  )HTML");

  Element* element = GetDocument().getElementById(AtomicString("test"));
  ASSERT_TRUE(element);

  // Verify that we see animation effects.
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(15, element->ComputedStyleRef().ZIndex());
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSTransitionBlockedByAnimation));

  // Attempt to trigger transition. This should not work, because there's a
  // current animation on the same property.
  element->classList().Add(AtomicString("change"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(15, element->ComputedStyleRef().ZIndex());
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSTransitionBlockedByAnimation));

  // Remove animation and attempt to trigger transition at the same time.
  // Transition should still not trigger because of
  // |previous_active_interpolations_for_animations_|.
  ClearUseCounter(WebFeature::kCSSTransitionBlockedByAnimation);
  element->classList().Remove(AtomicString("animate"));
  element->classList().Remove(AtomicString("change"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(0, element->ComputedStyleRef().ZIndex());
  EXPECT_TRUE(IsUseCounted(WebFeature::kCSSTransitionBlockedByAnimation));

  // Finally trigger the transition.
  ClearUseCounter(WebFeature::kCSSTransitionBlockedByAnimation);
  element->classList().Add(AtomicString("change"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(50, element->ComputedStyleRef().ZIndex());
  EXPECT_FALSE(IsUseCounted(WebFeature::kCSSTransitionBlockedByAnimation));
}

// The following group of tests verify that composited CSS animations are
// well behaved when updated via the web-animations API. Verifies that changes
// are synced with the compositor.

class CSSAnimationsCompositorSyncTest : public CSSAnimationsTest {
 public:
  CSSAnimationsCompositorSyncTest() = default;
  explicit CSSAnimationsCompositorSyncTest(bool auto_start)
      : auto_start_(auto_start) {}

  void SetUp() override {
    CSSAnimationsTest::SetUp();
    CreateOpacityAnimation();
  }
  void TearDown() override {
    element_ = nullptr;
    CSSAnimationsTest::TearDown();
  }

  // Creates a composited animation for opacity, and advances to the midpoint
  // of the animation. Verifies that the state of the animation is in sync
  // between the main thread and compositor.
  void CreateOpacityAnimation() {
    SetBodyInnerHTML(R"HTML(
      <style>
        #test { transition: opacity linear 1s; }
        .fade { opacity: 0; }
      </style>
      <div id='test'>TEST</div>
    )HTML");

    element_ = GetDocument().getElementById(AtomicString("test"));
    UpdateAllLifecyclePhasesForTest();
    ElementAnimations* animations = element_->GetElementAnimations();
    EXPECT_FALSE(animations);

    element_->setAttribute(html_names::kClassAttr, AtomicString("fade"));
    UpdateAllLifecyclePhasesForTest();

    if (!auto_start_) {
      return;
    }

    SyncAnimationOnCompositor(/*needs_start_time*/ true);

    Animation* animation = GetAnimation();
    EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
    VerifyCompositorStartTime(TimelineTime().since_origin().InMillisecondsF());
    VerifyCompositorPlaybackRate(1.0);
    VerifyCompositorTimeOffset(0.0);
    VerifyCompositorIterationTime(0);
    int compositor_group = animation->CompositorGroup();

    AdvanceClockSeconds(0.5);
    UpdateAllLifecyclePhasesForTest();
    EXPECT_NEAR(0.5, element_->GetComputedStyle()->Opacity(), kTolerance);
    EXPECT_EQ(compositor_group, animation->CompositorGroup());
    VerifyCompositorStartTime(TimelineTime().since_origin().InMillisecondsF() -
                              500);
    VerifyCompositorPlaybackRate(1.0);
    VerifyCompositorTimeOffset(0.0);
    VerifyCompositorIterationTime(500);
    VerifyCompositorOpacity(0.5);
  }

  Animation* GetAnimation() {
    // Note that the animations are stored as weak references and we cannot
    // persist the reference.
    ElementAnimations* element_animations = element_->GetElementAnimations();
    EXPECT_EQ(1u, element_animations->Animations().size());
    return (*element_animations->Animations().begin()).key.Get();
  }

  void NotifyStartTime() {
    Animation* animation = GetAnimation();
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    base::TimeTicks start_time = keyframe_model->start_time();
    static_cast<CompositorAnimationDelegate*>(animation)
        ->NotifyAnimationStarted(start_time.since_origin(),
                                 animation->CompositorGroup());
  }

  void SyncAnimationOnCompositor(bool needs_start_time) {
    // Verifies that the compositor animation requires a synchronization on the
    // start time.
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    EXPECT_EQ(needs_start_time, !keyframe_model->has_set_start_time());
    EXPECT_TRUE(keyframe_model->needs_synchronized_start_time());

    // Set the opacity keyframe model into a running state and sync with
    // blink::Animation.
    base::TimeTicks timeline_time = TimelineTime();
    keyframe_model->SetRunState(cc::KeyframeModel::RUNNING, TimelineTime());
    if (needs_start_time)
      keyframe_model->set_start_time(timeline_time);
    keyframe_model->set_needs_synchronized_start_time(false);
    NotifyStartTime();
  }

  cc::KeyframeModel* GetCompositorKeyframeForOpacity() {
    cc::Animation* cc_animation =
        GetAnimation()->GetCompositorAnimation()->CcAnimation();
    return cc_animation->GetKeyframeModel(cc::TargetProperty::OPACITY);
  }

  void VerifyCompositorPlaybackRate(double expected_value) {
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    EXPECT_NEAR(expected_value, keyframe_model->playback_rate(), kTolerance);
  }

  void VerifyCompositorTimeOffset(double expected_value) {
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    EXPECT_NEAR(expected_value, keyframe_model->time_offset().InMillisecondsF(),
                kTimeToleranceMilliseconds);
  }

  void VerifyCompositorStartTime(double expected_value) {
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    EXPECT_NEAR(expected_value,
                keyframe_model->start_time().since_origin().InMillisecondsF(),
                kTimeToleranceMilliseconds);
  }

  base::TimeDelta CompositorIterationTime() {
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    return keyframe_model->TrimTimeToCurrentIteration(TimelineTime());
  }

  void VerifyCompositorIterationTime(double expected_value) {
    base::TimeDelta iteration_time = CompositorIterationTime();
    EXPECT_NEAR(expected_value, iteration_time.InMillisecondsF(),
                kTimeToleranceMilliseconds);
  }

  void VerifyCompositorOpacity(double expected_value) {
    cc::KeyframeModel* keyframe_model = GetCompositorKeyframeForOpacity();
    base::TimeDelta iteration_time = CompositorIterationTime();
    const gfx::FloatAnimationCurve* opacity_curve =
        gfx::FloatAnimationCurve::ToFloatAnimationCurve(
            keyframe_model->curve());
    EXPECT_NEAR(expected_value,
                opacity_curve->GetTransformedValue(
                    iteration_time, gfx::TimingFunction::LimitDirection::RIGHT),
                kTolerance);
  }

  Persistent<Element> element_;
  bool auto_start_ = true;
};

class CSSAnimationsCompositorStartTest
    : public CSSAnimationsCompositorSyncTest {
 public:
  CSSAnimationsCompositorStartTest() : CSSAnimationsCompositorSyncTest(false) {}
};

INSTANTIATE_PAINT_TEST_SUITE_P(CSSAnimationsCompositorSyncTest);

// Verifies that cancel is immediately reflected in style update despite being
// deferred on the compositor until PreCommit.
TEST_P(CSSAnimationsCompositorSyncTest, AsyncCancel) {
  Animation* animation = GetAnimation();
  EXPECT_TRUE(
      element_->GetComputedStyle()->IsRunningOpacityAnimationOnCompositor());
  animation->cancel();
  GetDocument().View()->UpdateLifecycleToLayoutClean(
      DocumentUpdateReason::kTest);
  EXPECT_FALSE(animation->HasActiveAnimationsOnCompositor());
  EXPECT_FALSE(
      element_->GetComputedStyle()->IsRunningOpacityAnimationOnCompositor());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(animation->HasActiveAnimationsOnCompositor());
}

// Verifies that changes to the playback rate are synced with the compositor.
TEST_P(CSSAnimationsCompositorSyncTest, UpdatePlaybackRate) {
  Animation* animation = GetAnimation();
  int compositor_group = animation->CompositorGroup();

  animation->updatePlaybackRate(0.5, ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhasesForTest();

  // Compositor animation needs to restart and will have a new compositor group.
  int post_update_compositor_group = animation->CompositorGroup();
  EXPECT_NE(compositor_group, post_update_compositor_group);
  SyncAnimationOnCompositor(/*needs_start_time*/ true);

  // No jump in opacity after changing the playback rate.
  EXPECT_NEAR(0.5, element_->GetComputedStyle()->Opacity(), kTolerance);
  VerifyCompositorPlaybackRate(0.5);
  // The time offset tells the compositor where to seek into the animation, and
  // is calculated as follows:
  // time_offset = current_time / playback_rate = 0.5 / 0.5 = 1.0.
  VerifyCompositorTimeOffset(1000);
  // Start time must have been reset.
  VerifyCompositorStartTime(TimelineTime().since_origin().InMillisecondsF());
  VerifyCompositorIterationTime(500);
  VerifyCompositorOpacity(0.5);

  // Advances the clock, and ensures that the compositor animation is not
  // restarted and that it remains in sync.
  AdvanceClockSeconds(0.5);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_NEAR(0.25, element_->GetComputedStyle()->Opacity(), kTolerance);
  EXPECT_EQ(post_update_compositor_group, animation->CompositorGroup());
  VerifyCompositorTimeOffset(1000);
  VerifyCompositorStartTime(TimelineTime().since_origin().InMillisecondsF() -
                            500);
  VerifyCompositorIterationTime(750);
  VerifyCompositorOpacity(0.25);
}

// Verifies that reversing an animation is synced with the compositor.
TEST_P(CSSAnimationsCompositorSyncTest, Reverse) {
  Animation* animation = GetAnimation();
  int compositor_group = animation->CompositorGroup();

  animation->reverse(ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhasesForTest();

  // Verify update in web-animation API.
  EXPECT_NEAR(-1, animation->playbackRate(), kTolerance);

  // Verify there is no jump in opacity after changing the play direction
  EXPECT_NEAR(0.5, element_->GetComputedStyle()->Opacity(), kTolerance);

  // Compositor animation needs to restart and will have a new compositor group.
  int post_update_compositor_group = animation->CompositorGroup();
  EXPECT_NE(compositor_group, post_update_compositor_group);
  SyncAnimationOnCompositor(/*needs_start_time*/ true);

  // Verify updates to cc Keyframe model.
  // Start time must have been reset.
  VerifyCompositorStartTime(TimelineTime().since_origin().InMillisecondsF());
  VerifyCompositorPlaybackRate(-1.0);
  VerifyCompositorTimeOffset(500);
  VerifyCompositorIterationTime(500);
  VerifyCompositorOpacity(0.5);

  // Advances the clock, and ensures that the compositor animation is not
  // restarted and that it remains in sync.
  AdvanceClockSeconds(0.25);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_NEAR(0.75, element_->GetComputedStyle()->Opacity(), kTolerance);
  EXPECT_EQ(post_update_compositor_group, animation->CompositorGroup());
  VerifyCompositorStartTime(TimelineTime().since_origin().InMillisecondsF() -
                            250);
  VerifyCompositorIterationTime(250);
  VerifyCompositorOpacity(0.75);
}

// Verifies that setting the start time on a running animation restarts the
// compositor animation in sync with blink.
TEST_P(CSSAnimationsCompositorSyncTest, SetStartTime) {
  Animation* animation = GetAnimation();
  int compositor_group = animation->CompositorGroup();

  V8CSSNumberish* start_time = animation->startTime();
  V8CSSNumberish* current_time = animation->currentTime();

  // Partially rewind the animation via setStartTime.
  V8CSSNumberish* new_start_time = MakeGarbageCollected<V8CSSNumberish>(
      start_time->GetAsDouble() + (current_time->GetAsDouble() / 2));

  animation->setStartTime(new_start_time, ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhasesForTest();

  // Verify blink updates.
  current_time = animation->currentTime();
  EXPECT_TRUE(current_time->IsDouble());
  EXPECT_NEAR(250, current_time->GetAsDouble(), kTimeToleranceMilliseconds);
  EXPECT_NEAR(0.75, element_->GetComputedStyle()->Opacity(), kTolerance);

  // Compositor animation needs to restart and will have a new compositor group.
  int post_update_compositor_group = animation->CompositorGroup();
  EXPECT_NE(compositor_group, post_update_compositor_group);
  SyncAnimationOnCompositor(/*needs_start_time*/ false);

  // Verify updates to cc Keyframe model.
  VerifyCompositorStartTime(new_start_time->GetAsDouble());
  VerifyCompositorPlaybackRate(1.0);
  VerifyCompositorTimeOffset(0.0);
  VerifyCompositorIterationTime(250);
  VerifyCompositorOpacity(0.75);

  // Advances the clock, and ensures that the compositor animation is not
  // restarted and that it remains in sync.
  AdvanceClockSeconds(0.25);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_NEAR(0.5, element_->GetComputedStyle()->Opacity(), kTolerance);
  EXPECT_EQ(post_update_compositor_group, animation->CompositorGroup());
  VerifyCompositorStartTime(new_start_time->GetAsDouble());
  VerifyCompositorIterationTime(500);
  VerifyCompositorOpacity(0.5);
}

// Verifies that setting the current time on a running animation restarts the
// compositor animation in sync with blink.
TEST_P(CSSAnimationsCompositorSyncTest, SetCurrentTime) {
  Animation* animation = GetAnimation();
  int compositor_group = animation->CompositorGroup();

  // Advance current time.
  animation->setCurrentTime(MakeGarbageCollected<V8CSSNumberish>(750),
                            ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhasesForTest();

  // Verify blink updates.
  V8CSSNumberish* current_time = animation->currentTime();
  EXPECT_TRUE(current_time->IsDouble());
  EXPECT_NEAR(750, current_time->GetAsDouble(), kTimeToleranceMilliseconds);
  EXPECT_NEAR(0.25, element_->GetComputedStyle()->Opacity(), kTolerance);

  // Compositor animation needs to restart and will have a new compositor group.
  int post_update_compositor_group = animation->CompositorGroup();
  EXPECT_NE(compositor_group, post_update_compositor_group);
  SyncAnimationOnCompositor(/*needs_start_time*/ false);

  // Verify updates to cc Keyframe model.
  // Start time should be set to the recalculated value.
  VerifyCompositorStartTime(animation->startTime()->GetAsDouble());
  VerifyCompositorPlaybackRate(1.0);
  VerifyCompositorTimeOffset(0.0);
  VerifyCompositorIterationTime(750);
  VerifyCompositorOpacity(0.25);

  // Advances the clock, and ensures that the compositor animation is not
  // restarted and that it remains in sync.
  AdvanceClockSeconds(0.2);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_NEAR(0.05, element_->GetComputedStyle()->Opacity(), kTolerance);
  EXPECT_EQ(post_update_compositor_group, animation->CompositorGroup());
  VerifyCompositorIterationTime(950);
  VerifyCompositorOpacity(0.05);
}

TEST_P(CSSAnimationsCompositorSyncTest, PendingCancel) {
  Animation* animation = GetAnimation();
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
  animation->cancel();
  // Cancel is still pending. We avoid stopping on the compositor until commit
  // to prevent blocking on a protected sequence longer than necessary.
  EXPECT_FALSE(animation->HasActiveAnimationsOnCompositor());
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(animation->HasActiveAnimationsOnCompositor());
}

TEST_P(CSSAnimationsCompositorSyncTest, CancelThenPlay) {
  Animation* animation = GetAnimation();
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
  animation->cancel();
  animation->play();
  EXPECT_FALSE(animation->HasActiveAnimationsOnCompositor());
  UpdateAllLifecyclePhasesForTest();
  SyncAnimationOnCompositor(/*needs_start_time*/ true);
  // Animation is rewound to the start.
  VerifyCompositorOpacity(1.0);
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
}

TEST_P(CSSAnimationsCompositorSyncTest, PauseSetCurrentTimePlay) {
  // Opacity changes linearly from 1 to 0 over 1 second. The setup leaves the
  // animation at the midpoint.
  Animation* animation = GetAnimation();

  // Advances the clock, and ensures that the compositor animation is not
  // restarted and that it remains in sync.
  AdvanceClockSeconds(0.2);
  UpdateAllLifecyclePhasesForTest();
  VerifyCompositorOpacity(0.3);

  animation->pause();
  // Advance current time.
  animation->setCurrentTime(MakeGarbageCollected<V8CSSNumberish>(750),
                            ASSERT_NO_EXCEPTION);
  animation->play();
  UpdateAllLifecyclePhasesForTest();
  SyncAnimationOnCompositor(/*needs_start_time*/ true);
  VerifyCompositorOpacity(0.25);
}

INSTANTIATE_PAINT_TEST_SUITE_P(CSSAnimationsCompositorStartTest);

// Simulate slow start of a composited animation (e.g. due to paint holding).
TEST_P(CSSAnimationsCompositorStartTest, DelayedStart) {
  // Opacity changes linearly from 1 to 0 over 1 second.
  // Animation has not been started on the compositor.
  Animation* animation = GetAnimation();

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(animation->StartTimeInternal());
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());

  AdvanceClockSeconds(0.1);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(animation->StartTimeInternal());
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
  AdvanceClockSeconds(0.1);
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(animation->StartTimeInternal());
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());

  SyncAnimationOnCompositor(/*needs_start_time*/ true);
  EXPECT_TRUE(animation->StartTimeInternal());
  EXPECT_TRUE(animation->HasActiveAnimationsOnCompositor());
  VerifyCompositorOpacity(1.0);
}

TEST_P(CSSAnimationsTest, LingeringTimelineAttachments) {
  SetBodyInnerHTML(R"HTML(
    <style>
      .scope {
        timeline-scope: --t1;
      }
      #scroller {
        overflow: auto;
        width: 100px;
        height: 100px;
      }
      #scroller > div {
        width: 50px;
        height: 200px;
      }
      .timeline {
        scroll-timeline: --t1;
      }
    </style>
    <div class=scope>
      <div id=scroller class=timeline>
        <div></div>
      </div>
    </div>
  )HTML");

  Element* scroller = GetDocument().getElementById(AtomicString("scroller"));
  ASSERT_TRUE(scroller);

  ElementAnimations* element_animations = scroller->GetElementAnimations();
  ASSERT_TRUE(element_animations);

  const CSSAnimations& css_animations = element_animations->CssAnimations();
  EXPECT_TRUE(css_animations.HasTimelines());

  scroller->classList().Remove(AtomicString("timeline"));
  UpdateAllLifecyclePhasesForTest();

  // No timeline data should linger on #scroller's CSSAnimations.
  EXPECT_FALSE(css_animations.HasTimelines());
}

TEST_P(CSSAnimationsTest, DeferredTimelineUpdate) {
  SetBodyInnerHTML(R"HTML(
    <div id=target>Target</div>
  )HTML");

  Element* target = GetElementById("target");
  ASSERT_TRUE(target);

  EXPECT_EQ(0u, DeferredTimelinesCount(target));

  target->SetInlineStyleProperty(CSSPropertyID::kTimelineScope, "--t1");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(1u, DeferredTimelinesCount(target));

  target->SetInlineStyleProperty(CSSPropertyID::kTimelineScope, "--t1, --t2");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(2u, DeferredTimelinesCount(target));

  target->SetInlineStyleProperty(CSSPropertyID::kTimelineScope, "none");
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(0u, DeferredTimelinesCount(target));
}

TEST_P(CSSAnimationsTest, OpacityUnchangedWhileDeferred) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes fade {
        to {
          opacity: 0.5;
        }
      }
      #target {
          width: 100px;
          height: 100px;
          background-color: green;
          animation-name: fade;
          animation-duration: 3s;
      }
    </style>
    <div id="target"></div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));

  // The animation must be waiting on a deferred start time.
  ElementAnimations* animations = target->GetElementAnimations();
  ASSERT_EQ(1u, animations->Animations().size());
  Animation* animation = (*animations->Animations().begin()).key;
  ASSERT_TRUE(animation->WaitingOnDeferredStartTime());

  // Ensure the opacity doesn't change, since the animation hasn't started.
  EXPECT_EQ(target->GetComputedStyle()->Opacity(), 1);
}

void VerifyTriggerRangeBoundary(
    const AnimationTrigger::RangeBoundary* actual,
    const AnimationTrigger::RangeBoundary* expected) {
  if (expected->IsString()) {
    EXPECT_EQ(actual->GetAsString(), expected->GetAsString());
  } else {
    TimelineRangeOffset* expected_offset = expected->GetAsTimelineRangeOffset();
    TimelineRangeOffset* actual_offset = actual->GetAsTimelineRangeOffset();
    if (expected_offset->hasRangeName()) {
      EXPECT_EQ(expected_offset->rangeName(), actual_offset->rangeName());
    }

    if (expected_offset->hasOffset()) {
      EXPECT_TRUE(expected_offset->offset()->Equals(*actual_offset->offset()));
    }
  }
}

class CSSAnimationsTriggerTest : public CSSAnimationsTest {
 public:
  using Type = AnimationTrigger::Type;

  void TestAnimationTrigger(
      AnimationTrigger* trigger,
      AnimationTrigger::Type expected_type,
      std::optional<bool> expect_view_timeline,
      AnimationTrigger::RangeBoundary* expected_start,
      AnimationTrigger::RangeBoundary* expected_end,
      AnimationTrigger::RangeBoundary* expected_exit_start,
      AnimationTrigger::RangeBoundary* expected_exit_end);

  void TestRangeStartChange(
      Element* target,
      Animation* animation,
      AtomicString new_class,
      bool expect_same,
      const AnimationTrigger::RangeBoundary* expected_bounday);

  AnimationTrigger::RangeBoundary* MakeRangeOffsetBoundary(
      std::optional<V8TimelineRange::Enum> range,
      std::optional<int> pct) {
    TimelineRangeOffset* offset = MakeGarbageCollected<TimelineRangeOffset>();
    if (range) {
      offset->setRangeName(V8TimelineRange(*range));
    }
    if (pct) {
      offset->setOffset(
          CSSNumericValue::FromCSSValue(*CSSNumericLiteralValue::Create(
              *pct, CSSNumericLiteralValue::UnitType::kPercentage)));
    }
    return MakeGarbageCollected<AnimationTrigger::RangeBoundary>(offset);
  }
};

INSTANTIATE_PAINT_TEST_SUITE_P(CSSAnimationsTriggerTest);

void CSSAnimationsTriggerTest::TestAnimationTrigger(
    AnimationTrigger* trigger,
    AnimationTrigger::Type expected_type,
    std::optional<bool> expect_view_timeline,
    AnimationTrigger::RangeBoundary* expected_start,
    AnimationTrigger::RangeBoundary* expected_end,
    AnimationTrigger::RangeBoundary* expected_exit_start,
    AnimationTrigger::RangeBoundary* expected_exit_end) {
  EXPECT_NE(trigger, nullptr);
  EXPECT_EQ(trigger->type(), expected_type);

  AnimationTimeline* timeline = trigger->timeline();
  if (!expect_view_timeline.has_value()) {
    EXPECT_EQ(timeline, &GetDocument().Timeline());
  } else if (expect_view_timeline.value() == false) {
    EXPECT_TRUE(timeline->IsScrollTimeline());
  } else {
    EXPECT_TRUE(timeline->IsViewTimeline());
  }

  const AnimationTrigger::RangeBoundary* range_start =
      trigger->rangeStart(nullptr);
  VerifyTriggerRangeBoundary(range_start, expected_start);

  const AnimationTrigger::RangeBoundary* range_end = trigger->rangeEnd(nullptr);
  VerifyTriggerRangeBoundary(range_end, expected_end);

  const AnimationTrigger::RangeBoundary* exit_range_start =
      trigger->exitRangeStart(nullptr);
  VerifyTriggerRangeBoundary(exit_range_start, expected_exit_start);

  const AnimationTrigger::RangeBoundary* exit_range_end =
      trigger->exitRangeEnd(nullptr);
  VerifyTriggerRangeBoundary(exit_range_end, expected_exit_end);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerOnceOnly) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes myAnim {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
        animation: myAnim linear 0.5s forwards;
        animation-trigger: once;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="scroller" class="scroller">
      <div id="space"></div>
      <div id="target" class="subject"></div>
      <div id="space"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();

  AnimationTrigger::RangeBoundary* normal =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>("normal");
  AnimationTrigger::RangeBoundary* auto_offset =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>("auto");
  TestAnimationTrigger(trigger, V8AnimationTriggerType(Type::Enum::kOnce),
                       /* expect_view_timeline */ std::nullopt, normal, normal,
                       auto_offset, auto_offset);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerViewOnly) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes myAnim {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
        animation: myAnim linear 0.5s forwards;
        animation-trigger: view();
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="scroller" class="scroller">
      <div id="space"></div>
      <div id="target" class="subject"></div>
      <div id="space"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  AnimationTrigger::RangeBoundary* normal =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>("normal");
  AnimationTrigger::RangeBoundary* auto_offset =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>("auto");
  TestAnimationTrigger(trigger, V8AnimationTriggerType(Type::Enum::kOnce),
                       /* expect_view_timeline */ true, normal, normal,
                       auto_offset, auto_offset);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerScrollOnce) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes myAnim {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
        animation: myAnim linear 0.5s forwards;
        animation-trigger: scroll() once 25% 75%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="scroller" class="scroller">
      <div id="space"></div>
      <div id="target" class="subject"></div>
      <div id="space"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();

  AnimationTrigger::RangeBoundary* pct25 =
      MakeRangeOffsetBoundary(std::nullopt, 25);
  AnimationTrigger::RangeBoundary* pct75 =
      MakeRangeOffsetBoundary(std::nullopt, 75);
  AnimationTrigger::RangeBoundary* auto_offset =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>("auto");

  TestAnimationTrigger(trigger, V8AnimationTriggerType(Type::Enum::kOnce),
                       /* expect_view_timeline */ false, pct25, pct75,
                       auto_offset, auto_offset);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerViewAlternate) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes myAnim {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
        animation: myAnim linear 0.5s forwards;
        animation-trigger: view() alternate contain 10% contain 90%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="scroller" class="scroller">
      <div id="space"></div>
      <div id="target" class="subject"></div>
      <div id="space"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));

  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  EXPECT_NE(trigger, nullptr);

  AnimationTrigger::RangeBoundary* contain10 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 10);
  AnimationTrigger::RangeBoundary* contain90 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 90);
  AnimationTrigger::RangeBoundary* auto_offset =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>("auto");

  TestAnimationTrigger(trigger, V8AnimationTriggerType(Type::Enum::kAlternate),
                       /* expect_view_timeline */ true, contain10, contain90,
                       auto_offset, auto_offset);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerViewRepeat) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes myAnim {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
        animation: myAnim linear 0.5s forwards;
        animation-trigger: view() repeat contain 10% contain 90% cover 1% cover 99%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="scroller" class="scroller">
      <div id="space"></div>
      <div id="target" class="subject"></div>
      <div id="space"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));

  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  EXPECT_NE(trigger, nullptr);

  AnimationTrigger::RangeBoundary* contain10 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 10);
  AnimationTrigger::RangeBoundary* contain90 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 90);
  AnimationTrigger::RangeBoundary* cover1 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kCover, 1);
  AnimationTrigger::RangeBoundary* cover99 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kCover, 99);

  TestAnimationTrigger(trigger, V8AnimationTriggerType(Type::Enum::kRepeat),
                       true, contain10, contain90, cover1, cover99);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerNamedTimeline) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes myAnim {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
        view-timeline: --viewtimeline;
      }
      #target {
        animation: myAnim linear 0.5s forwards;
        animation-trigger: --viewtimeline repeat contain 10% contain 90%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
      #wrapper {
        timeline-scope: --viewtimeline;
      }
    </style>
    <div id="wrapper">
      <div id="scroller" class="scroller">
        <div id="space"></div>
        <div class="subject"></div>
        <div id="space"></div>
      </div>
      <div id="target"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));

  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  EXPECT_NE(trigger, nullptr);

  EXPECT_EQ(trigger->type(), V8AnimationTriggerType::Enum::kRepeat);

  EXPECT_FALSE(trigger->GetTimelineInternal()->IsScrollTimeline());
  EXPECT_TRUE(trigger->timeline()->IsViewTimeline());
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerChangeTimeline) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes stretch {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
      }
      #target {
        animation: stretch linear 0.5s forwards;
      }
      .view_trigger {
        animation-trigger: view() repeat contain 10% contain 90%;
      }
      .scroll_trigger {
        animation-trigger: --scrolltimeline repeat contain 10% contain 90%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
        scroll-timeline-name: --scrolltimeline;
      }
      #space {
        width: 50px;
        height: 600px;
      }
      #wrapper {
        timeline-scope: --scrolltimeline;
      }
    </style>
    <div id="wrapper">
      <div id="scroller" class="scroller">
        <div id="space"></div>
        <div class="subject"></div>
        <div id="space"></div>
      </div>
      <div id="target"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  UpdateAllLifecyclePhasesForTest();

  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  EXPECT_NE(trigger, nullptr);

  EXPECT_EQ(trigger->timeline(), &GetDocument().Timeline());

  target->setAttribute(html_names::kClassAttr, AtomicString("view_trigger"));
  UpdateAllLifecyclePhasesForTest();

  AnimationTrigger* view_trigger = animation->GetTrigger();
  EXPECT_NE(trigger, view_trigger);
  EXPECT_NE(view_trigger->timeline(), nullptr);
  EXPECT_TRUE(view_trigger->timeline()->IsViewTimeline());

  target->setAttribute(html_names::kClassAttr, AtomicString("scroll_trigger"));
  UpdateAllLifecyclePhasesForTest();

  AnimationTrigger* scroll_trigger = animation->GetTrigger();
  EXPECT_NE(view_trigger, scroll_trigger);
  EXPECT_NE(scroll_trigger->GetTimelineInternal(), nullptr);
  EXPECT_FALSE(scroll_trigger->GetTimelineInternal()->IsScrollTimeline());
  EXPECT_FALSE(scroll_trigger->timeline()->IsViewTimeline());
  EXPECT_TRUE(scroll_trigger->timeline()->IsScrollTimeline());
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerChangeType) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes stretch {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
      }
      #target {
        animation: stretch linear 0.5s forwards;
      }
      .repeat_trigger {
        animation-trigger: view() repeat contain 10% contain 90%;
      }
      .once_trigger {
        animation-trigger: view() once contain 10% contain 90%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="wrapper">
      <div id="scroller" class="scroller">
        <div id="space"></div>
        <div class="subject"></div>
        <div id="space"></div>
      </div>
      <div id="target"></div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));

  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  EXPECT_NE(trigger, nullptr);

  EXPECT_EQ(trigger->timeline(), &GetDocument().Timeline());

  target->classList().Add(AtomicString("repeat_trigger"));
  UpdateAllLifecyclePhasesForTest();

  AnimationTrigger* repeat_trigger = animation->GetTrigger();
  EXPECT_NE(trigger, repeat_trigger);
  EXPECT_EQ(repeat_trigger->type(), AnimationTrigger::Type::Enum::kRepeat);

  target->classList().Remove(AtomicString("repeat_trigger"));
  target->classList().Add(AtomicString("once_trigger"));
  UpdateAllLifecyclePhasesForTest();

  AnimationTrigger* once_trigger = animation->GetTrigger();
  EXPECT_NE(once_trigger, repeat_trigger);
  EXPECT_EQ(once_trigger->type(), AnimationTrigger::Type::Enum::kOnce);
}

void CSSAnimationsTriggerTest::TestRangeStartChange(
    Element* target,
    Animation* animation,
    AtomicString new_class,
    bool expect_same,
    const AnimationTrigger::RangeBoundary* expected_boundary) {
  CSSAnimation* css_animation = DynamicTo<CSSAnimation>(animation);
  AnimationTrigger* old_trigger = css_animation->GetTrigger();
  target->setAttribute(html_names::kClassAttr, new_class);
  UpdateAllLifecyclePhasesForTest();
  AnimationTrigger* new_trigger = css_animation->GetTrigger();
  if (expect_same) {
    EXPECT_EQ(old_trigger, new_trigger);
  } else {
    EXPECT_NE(old_trigger, new_trigger);
  }
  VerifyTriggerRangeBoundary(new_trigger->rangeStart(nullptr),
                             expected_boundary);
}

TEST_P(CSSAnimationsTriggerTest, AnimationTriggerChangeRangeStart) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes stretch {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject {
        height: 50px;
        width: 50px;
      }
      #target {
        animation: stretch linear 0.5s forwards;
      }
      .normal_trigger {
        animation-trigger: view() repeat;
      }
      .normal_trigger2 {
        animation-trigger: view() repeat;
      }
      .contain10_trigger {
        animation-trigger: view() once contain 10%;
      }
      .contain10_trigger2 {
        animation-trigger: view() once contain 10%;
      }
      .contain90_trigger {
        animation-trigger: view() once contain 90%;
      }
      .cover90_trigger {
        animation-trigger: view() once cover 90%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="wrapper">
      <div id="scroller" class="scroller">
        <div id="space"></div>
        <div id="target" class="subject"></div>
        <div id="space"></div>
      </div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  EXPECT_NE(trigger, nullptr);
  EXPECT_EQ(trigger->timeline(), &GetDocument().Timeline());

  const AnimationTrigger::RangeBoundary* normal =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>(String("normal"));
  TestRangeStartChange(target, animation, AtomicString("normal_trigger"),
                       /* expect_same */ false, normal);
  TestRangeStartChange(target, animation, AtomicString("normal_trigger2"),
                       /* expect_same */ true, normal);
  AnimationTrigger::RangeBoundary* contain10 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 10);
  TestRangeStartChange(target, animation, AtomicString("contain10_trigger"),
                       /* expect_same */ false, contain10);
  TestRangeStartChange(target, animation, AtomicString("contain10_trigger2"),
                       /* expect_same */ true, contain10);

  AnimationTrigger::RangeBoundary* contain90 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 90);
  TestRangeStartChange(target, animation, AtomicString("contain90_trigger"),
                       /* expect_same */ false, contain90);

  AnimationTrigger::RangeBoundary* cover90 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kCover, 90);
  TestRangeStartChange(target, animation, AtomicString("cover90_trigger"),
                       /* expect_same */ false, cover90);
}

TEST_P(CSSAnimationsTriggerTest, NonTriggerChange) {
  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes stretch {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .subject50x50 {
        height: 50px;
        width: 50px;
      }
      .subject100x100 {
        height: 100px;
        width: 100px;
      }
      .target {
        height: 10px;
        width: 10px;
        animation: stretch linear 0.5s forwards;
        animation-trigger: view() once contain 10% contain 90%;
      }
      .scroll_tl {
        animation-timeline: scroll();
      }
      .view_tl {
        animation-timeline: view();
      }
      .range_contain {
        animation-range: contain 10% contain 90%;
      }
      .range_cover {
        animation-range: cover 1% cover 99%;
      }
     .scroller {
        overflow-y: scroll;
        height: 500px;
        width: 500px;
        border: solid 1px;
        position: relative;
      }
      #space {
        width: 50px;
        height: 600px;
      }
    </style>
    <div id="wrapper">
      <div id="scroller" class="scroller">
        <div id="space"></div>
        <div id="target" class="target subject50x50"></div>
        <div id="space"></div>
      </div>
    </div>
  )HTML");

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* original_trigger = animation->GetTrigger();
  EXPECT_NE(original_trigger, nullptr);
  EXPECT_TRUE(original_trigger->timeline()->IsViewTimeline());

  target->classList().Add(AtomicString("subject100x100"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(original_trigger, animation->GetTrigger());

  EXPECT_FALSE(animation->timeline()->IsScrollTimeline());
  target->classList().Add(AtomicString("scroll_tl"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(original_trigger, animation->GetTrigger());
  EXPECT_TRUE(animation->timeline()->IsScrollTimeline());

  EXPECT_FALSE(animation->timeline()->IsViewTimeline());
  target->classList().Remove(AtomicString("scroll_tl"));
  target->classList().Add(AtomicString("view_tl"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(original_trigger, animation->GetTrigger());
  EXPECT_TRUE(animation->timeline()->IsViewTimeline());

  const AnimationTrigger::RangeBoundary* normal =
      MakeGarbageCollected<AnimationTrigger::RangeBoundary>(String("normal"));
  VerifyTriggerRangeBoundary(animation->rangeStart(), normal);
  target->classList().Add(AtomicString("range_contain"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(original_trigger, animation->GetTrigger());
  AnimationTrigger::RangeBoundary* contain10 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kContain, 10);
  VerifyTriggerRangeBoundary(animation->rangeStart(), contain10);

  target->classList().Remove(AtomicString("range_contain"));
  target->classList().Add(AtomicString("range_cover"));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_EQ(original_trigger, animation->GetTrigger());
  AnimationTrigger::RangeBoundary* cover1 =
      MakeRangeOffsetBoundary(V8TimelineRange::Enum::kCover, 1);
  VerifyTriggerRangeBoundary(animation->rangeStart(), cover1);
}

TEST_P(CSSAnimationsTriggerTest, DeviceScaleFactor) {
  using RangeBoundary = AnimationTrigger::RangeBoundary;

  GetFrame().SetLayoutZoomFactor(2.0f);

  SetBodyInnerHTML(R"HTML(
    <style>
      @keyframes stretch {
        from { transform: scaleX(1); }
        to { transform: scaleX(5); }
      }
      .target {
        height: 10px;
        width: 10px;
        animation: stretch linear 0.5s forwards;
        animation-trigger: view() once 100px 300px;
      }
    </style>
    <div id="target" class="target"></div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();

  Element* target = GetDocument().getElementById(AtomicString("target"));
  ElementAnimations* animations = target->GetElementAnimations();
  CSSAnimation* animation =
      DynamicTo<CSSAnimation>((*animations->Animations().begin()).key.Get());

  AnimationTrigger* trigger = animation->GetTrigger();
  const RangeBoundary* range_start = trigger->rangeStart(nullptr);
  const RangeBoundary* range_end = trigger->rangeEnd(nullptr);

  EXPECT_TRUE(range_start->IsTimelineRangeOffset());
  EXPECT_TRUE(range_end->IsTimelineRangeOffset());

  TimelineRangeOffset* start_offset = range_start->GetAsTimelineRangeOffset();
  TimelineRangeOffset* end_offset = range_end->GetAsTimelineRangeOffset();

  CSSPrimitiveValue* value_100px =
      CSSNumericLiteralValue::Create(100, CSSPrimitiveValue::UnitType::kPixels);
  CSSNumericValue* offset_100px = CSSNumericValue::FromCSSValue(*value_100px);
  EXPECT_TRUE(start_offset->offset()->Equals(*offset_100px));

  CSSPrimitiveValue* value_300px =
      CSSNumericLiteralValue::Create(300, CSSPrimitiveValue::UnitType::kPixels);
  CSSNumericValue* offset_300px = CSSNumericValue::FromCSSValue(*value_300px);
  EXPECT_TRUE(end_offset->offset()->Equals(*offset_300px));
}

}  // namespace blink
