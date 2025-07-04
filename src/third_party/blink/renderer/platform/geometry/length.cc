/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller ( mueller@kde.org )
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2006 Andrew Wellington (proton@wiretapped.net)
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
 *
 */

#include "third_party/blink/renderer/platform/geometry/length.h"

#include <array>

#include "third_party/blink/renderer/platform/geometry/blend.h"
#include "third_party/blink/renderer/platform/geometry/calculation_value.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/size_assertions.h"
#include "third_party/blink/renderer/platform/wtf/static_constructors.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_auto_length);
DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_fill_available_length);
DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_stretch_length);
DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_fit_content_length);
DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_max_content_length);
DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_min_content_length);
DEFINE_GLOBAL(PLATFORM_EXPORT, Length, g_min_intrinsic_length);

// static
void Length::Initialize() {
  new (base::NotNullTag::kNotNull, (void*)&g_auto_length) Length(kAuto);
  new (base::NotNullTag::kNotNull, (void*)&g_fill_available_length)
      Length(kFillAvailable);
  new (base::NotNullTag::kNotNull, (void*)&g_stretch_length) Length(kStretch);
  new (base::NotNullTag::kNotNull, (void*)&g_fit_content_length)
      Length(kFitContent);
  new (base::NotNullTag::kNotNull, (void*)&g_max_content_length)
      Length(kMaxContent);
  new (base::NotNullTag::kNotNull, (void*)&g_min_content_length)
      Length(kMinContent);
  new (base::NotNullTag::kNotNull, (void*)&g_min_intrinsic_length)
      Length(kMinIntrinsic);
}

class CalculationValueHandleMap
    : public GarbageCollected<CalculationValueHandleMap> {
 public:
  CalculationValueHandleMap() = default;
  CalculationValueHandleMap(const CalculationValueHandleMap&) = delete;
  CalculationValueHandleMap& operator=(const CalculationValueHandleMap&) =
      delete;

  struct MemberWithCount {
    DISALLOW_NEW();

   public:
    void Trace(Visitor* visitor) const { visitor->Trace(value); }
    Member<const CalculationValue> value;
    unsigned count = 1u;
  };

  void Trace(Visitor* visitor) const { visitor->Trace(map_); }

  int insert(const CalculationValue* calc_value) {
    DCHECK(index_);
    // FIXME calc(): https://bugs.webkit.org/show_bug.cgi?id=80489
    // This monotonically increasing handle generation scheme is potentially
    // wasteful of the handle space. Consider reusing empty handles.
    while (map_.Contains(index_))
      index_++;

    map_.Set(index_, MemberWithCount(calc_value, 1u));

    return index_;
  }

  const CalculationValue& Get(int index) const {
    DCHECK(map_.Contains(index));
    return *map_.at(index).value;
  }

  unsigned GetCount(int index) const {
    DCHECK(map_.Contains(index));
    return map_.at(index).count;
  }

  wtf_size_t GetMapSize() const { return map_.size(); }

  void DecrementCount(int index) {
    DCHECK(map_.Contains(index));
    auto iter = map_.find(index);
    --iter->value.count;
    if (iter->value.count == 0u) {
      map_.erase(index);
    }
  }

  void IncrementCount(int index) {
    DCHECK(map_.Contains(index));
    auto iter = map_.find(index);
    ++iter->value.count;
  }

 private:
  int index_ = 1;
  HeapHashMap<int, MemberWithCount> map_;
};

static CalculationValueHandleMap& CalcHandles() {
  DCHECK(IsMainThread());
  DEFINE_STATIC_LOCAL(Persistent<CalculationValueHandleMap>, handle_map,
                      (MakeGarbageCollected<CalculationValueHandleMap>()));
  return *handle_map;
}

Length::Length(const CalculationValue* calc)
    : quirk_(false), type_(kCalculated) {
  calculation_handle_ = CalcHandles().insert(calc);
}

Length Length::BlendMixedTypes(const Length& from,
                               double progress,
                               ValueRange range) const {
  DCHECK(from.CanConvertToCalculation());
  DCHECK(CanConvertToCalculation());
  return Length(
      AsCalculationValue()->Blend(*from.AsCalculationValue(), progress, range));
}

Length Length::BlendSameTypes(const Length& from,
                              double progress,
                              ValueRange range) const {
  Length::Type result_type = GetType();
  if (IsZero())
    result_type = from.GetType();

  float blended_value =
      blink::Blend(from.GetFloatValue(), GetFloatValue(), progress);
  if (range == ValueRange::kNonNegative)
    blended_value = ClampTo<float>(blended_value, 0);
  return Length(blended_value, result_type);
}

PixelsAndPercent Length::GetPixelsAndPercent() const {
  switch (GetType()) {
    case kFixed:
      return PixelsAndPercent(Pixels());
    case kPercent:
      return PixelsAndPercent(0.0f, Percent(), /*has_explicit_pixels=*/false,
                              /*has_explicit_percent=*/true);
    case kCalculated:
      return GetCalculationValue().GetPixelsAndPercent();
    default:
      NOTREACHED();
  }
}

const CalculationValue* Length::AsCalculationValue() const {
  if (IsCalculated())
    return &GetCalculationValue();
  return MakeGarbageCollected<CalculationValue>(GetPixelsAndPercent(),
                                                ValueRange::kAll);
}

Length Length::SubtractFromOneHundredPercent() const {
  if (IsPercent())
    return Length::Percent(100 - Percent());
  DCHECK(CanConvertToCalculation());
  return Length(AsCalculationValue()->SubtractFromOneHundredPercent());
}

Length Length::Add(const Length& other) const {
  CHECK(CanConvertToCalculation());
  if (IsFixed() && other.IsFixed()) {
    return Length::Fixed(Pixels() + other.Pixels());
  }
  if (IsPercent() && other.IsPercent()) {
    return Length::Percent(Percent() + other.Percent());
  }
  return Length(AsCalculationValue()->Add(*other.AsCalculationValue()));
}

Length Length::Zoom(double factor) const {
  switch (GetType()) {
    case kFixed:
      return Length::Fixed(GetFloatValue() * factor);
    case kCalculated:
      return Length(GetCalculationValue().Zoom(factor));
    default:
      return *this;
  }
}

const CalculationValue& Length::GetCalculationValue() const {
  DCHECK(IsCalculated());
  return CalcHandles().Get(CalculationHandle());
}

void Length::IncrementCalculatedCount() const {
  DCHECK(IsCalculated());
  CalcHandles().IncrementCount(CalculationHandle());
}

void Length::DecrementCalculatedCount() const {
  DCHECK(IsCalculated());
  CalcHandles().DecrementCount(CalculationHandle());
}

unsigned Length::GetCalculatedCountForTest() const {
  DCHECK(IsCalculated());
  return CalcHandles().GetCount(CalculationHandle());
}

wtf_size_t Length::GetCalcHandleMapSizeForTest() {
  return CalcHandles().GetMapSize();
}

float Length::NonNanCalculatedValue(float max_value,
                                    const EvaluationInput& input) const {
  DCHECK(IsCalculated());
  float result = GetCalculationValue().Evaluate(max_value, input);
  if (std::isnan(result))
    return 0;
  return result;
}

bool Length::HasOnlyFixedAndPercent() const {
  if (GetType() == kFixed || GetType() == kPercent) {
    return true;
  }
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasOnlyFixedAndPercent();
  }
  return false;
}

bool Length::HasAuto() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasAuto();
  }
  return GetType() == kAuto;
}

bool Length::HasContentOrIntrinsic() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasContentOrIntrinsicSize();
  }
  return GetType() == kMinContent || GetType() == kMaxContent ||
         GetType() == kFitContent || GetType() == kMinIntrinsic ||
         GetType() == kContent;
}

bool Length::HasAutoOrContentOrIntrinsic() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasAutoOrContentOrIntrinsicSize();
  }
  return GetType() == kAuto || HasContentOrIntrinsic();
}

bool Length::HasPercent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasPercent();
  }
  return GetType() == kPercent;
}

bool Length::HasPercentOrStretch() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasPercentOrStretch();
  }
  return GetType() == kPercent || GetType() == kStretch ||
         GetType() == kFillAvailable;
}

bool Length::HasStretch() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasStretch();
  }
  return GetType() == kStretch || GetType() == kFillAvailable;
}

bool Length::HasMinContent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasMinContent();
  }
  return GetType() == kMinContent;
}

bool Length::HasMaxContent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasMaxContent();
  }
  return GetType() == kMaxContent;
}

bool Length::HasFitContent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasFitContent();
  }
  return GetType() == kFitContent;
}

bool Length::IsCalculatedEqual(const Length& o) const {
  return IsCalculated() &&
         (&GetCalculationValue() == &o.GetCalculationValue() ||
          GetCalculationValue() == o.GetCalculationValue());
}

String Length::ToString() const {
  StringBuilder builder;
  builder.Append("Length(");
  static const auto kTypeNames = std::to_array<const char* const>(
      {"Auto", "Percent", "Fixed", "MinContent", "MaxContent", "MinIntrinsic",
       "FillAvailable", "Stretch", "FitContent", "Calculated", "Flex",
       "ExtendToZoom", "DeviceWidth", "DeviceHeight", "None", "Content"});
  if (type_ < std::size(kTypeNames))
    builder.Append(kTypeNames[type_]);
  else
    builder.Append("?");
  builder.Append(", ");
  if (IsCalculated()) {
    builder.AppendNumber(calculation_handle_);
  } else {
    builder.AppendNumber(value_);
  }
  if (quirk_)
    builder.Append(", Quirk");
  builder.Append(")");
  return builder.ToString();
}

unsigned Length::GetHash() const {
  unsigned hash = 0;
  WTF::AddFloatToHash(hash, value_);
  WTF::AddIntToHash(hash, type_);
  WTF::AddIntToHash(hash, quirk_);
  WTF::AddIntToHash(hash, calculation_handle_);
  return hash;
}

std::ostream& operator<<(std::ostream& ostream, const Length& value) {
  return ostream << value.ToString();
}

struct SameSizeAsLength {
  int32_t value;
  int32_t meta_data;
};
ASSERT_SIZE(Length, SameSizeAsLength);

}  // namespace blink
