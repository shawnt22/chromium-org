/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_FORMS_NUMBER_INPUT_TYPE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_FORMS_NUMBER_INPUT_TYPE_H_

#include "third_party/blink/renderer/core/html/forms/text_field_input_type.h"

namespace blink {

class ExceptionState;

class NumberInputType final : public TextFieldInputType {
 public:
  explicit NumberInputType(HTMLInputElement& element)
      : TextFieldInputType(Type::kNumber, element) {}
  bool TypeMismatchFor(const String&) const;
  CORE_EXPORT static String NormalizeFullWidthNumberChars(const String& input);

 private:
  void CountUsage() override;
  void SetValue(const String&,
                bool value_changed,
                TextFieldEventBehavior,
                TextControlSetValueSelection) override;
  double ValueAsDouble() const override;
  void SetValueAsDouble(double,
                        TextFieldEventBehavior,
                        ExceptionState&) const override;
  void SetValueAsDecimal(const Decimal&,
                         TextFieldEventBehavior,
                         ExceptionState&) const override;
  bool TypeMismatch() const override;
  bool GetSizeWithDecoration(int default_size,
                             int& preferred_size) const override;
  StepRange CreateStepRange(AnyStepHandling) const override;
  void HandleKeydownEvent(KeyboardEvent&) override;
  void HandleBeforeTextInsertedEvent(BeforeTextInsertedEvent&) override;
  Decimal ParseToNumber(const String&, const Decimal&) const override;
  String Serialize(const Decimal&) const override;
  String LocalizeValue(const String&) const override;
  String VisibleValue() const override;
  String ConvertFromVisibleValue(const String&) const override;
  String SanitizeValue(const String&) const override;
  void WarnIfValueIsInvalid(const String&) const override;
  bool HasBadInput() const override;
  String BadInputText() const override;
  String ValueNotEqualText(const Decimal& value) const override;
  String RangeOverflowText(const Decimal& maxmum) const override;
  String RangeUnderflowText(const Decimal& minimum) const override;
  String RangeInvalidText(const Decimal& minimum,
                          const Decimal& maximum) const override;
  bool SupportsPlaceholder() const override;
  void MinOrMaxAttributeChanged() override;
  void StepAttributeChanged() override;
  bool SupportsSelectionAPI() const override;
};

template <>
struct DowncastTraits<NumberInputType> {
  static bool AllowFrom(const InputType& type) {
    return type.IsNumberInputType();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_FORMS_NUMBER_INPUT_TYPE_H_
