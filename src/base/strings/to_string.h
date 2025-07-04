// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_STRINGS_TO_STRING_H_
#define BASE_STRINGS_TO_STRING_H_

#include <concepts>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "base/base_export.h"
#include "base/containers/span.h"
#include "base/strings/string_view_util.h"
#include "base/types/supports_ostream_operator.h"
#include "base/types/supports_to_string.h"

namespace base {

template <typename T>
std::string ToString(const T& values);

namespace internal {

// Function pointers implicitly convert to `bool`, so use this to avoid printing
// function pointers as "true"/"false".
template <typename T>
concept WillBeIncorrectlyStreamedAsBool =
    std::is_function_v<std::remove_pointer_t<T>>;

// Fallback case when there is no better representation.
template <typename T>
struct ToStringHelper {
  static void Stringify(const T& v, std::ostringstream& ss) {
    // We cast to `void*` to avoid converting a char-like type to char-like*
    // which operator<< treats as a string but does not support for multi-byte
    // char-like types.
    ss << "[" << sizeof(v) << "-byte object at 0x"
       << static_cast<const void*>(std::addressof(v)) << "]";
  }
};

// Boolean values. (Handled explicitly so as to not rely on the behavior of
// std::boolalpha.)
template <>
struct ToStringHelper<bool> {
  static void Stringify(const bool& v, std::ostringstream& ss) {
    ss << (v ? "true" : "false");
  }
};

// Most streamables.
template <typename T>
  requires(SupportsOstreamOperator<const T&> &&
           !WillBeIncorrectlyStreamedAsBool<T>)
struct ToStringHelper<T> {
  static void Stringify(const T& v, std::ostringstream& ss) { ss << v; }
};

// Functions and function pointers.
template <typename T>
  requires(SupportsOstreamOperator<const T&> &&
           WillBeIncorrectlyStreamedAsBool<T>)
struct ToStringHelper<T> {
  static void Stringify(const T& v, std::ostringstream& ss) {
    ToStringHelper<const void*>::Stringify(reinterpret_cast<const void*>(v),
                                           ss);
  }
};

// Integral types that can't stream, like char16_t.
template <typename T>
  requires(!SupportsOstreamOperator<const T&> && std::is_integral_v<T>)
struct ToStringHelper<T> {
  static void Stringify(const T& v, std::ostringstream& ss) {
    if constexpr (std::is_signed_v<T>) {
      static_assert(sizeof(T) <= 8);
      ss << static_cast<int64_t>(v);
    } else {
      static_assert(sizeof(T) <= 8);
      ss << static_cast<uint64_t>(v);
    }
  }
};

// Non-streamables that have a `ToString` member.
template <typename T>
  requires(!SupportsOstreamOperator<const T&> && SupportsToString<const T&>)
struct ToStringHelper<T> {
  static void Stringify(const T& v, std::ostringstream& ss) {
    // .ToString() may not return a std::string, e.g. blink::WTF::String.
    ToStringHelper<decltype(v.ToString())>::Stringify(v.ToString(), ss);
  }
};

// Non-streamable enums (i.e. scoped enums where no `operator<<` overload was
// declared).
template <typename T>
  requires(!SupportsOstreamOperator<const T&> && std::is_enum_v<T>)
struct ToStringHelper<T> {
  static void Stringify(const T& v, std::ostringstream& ss) {
    using UT = typename std::underlying_type_t<T>;
    ToStringHelper<UT>::Stringify(static_cast<UT>(v), ss);
  }
};

// Tuples. Will recursively apply `ToString()` to each value in the tuple.
template <typename... T>
struct ToStringHelper<std::tuple<T...>> {
  template <size_t... I>
  static void StringifyHelper(const std::tuple<T...>& values,
                              std::index_sequence<I...>,
                              std::ostringstream& ss) {
    ss << "<";
    (..., (ss << (I == 0 ? "" : ", "), ss << ToString(std::get<I>(values))));
    ss << ">";
  }

  static void Stringify(const std::tuple<T...>& v, std::ostringstream& ss) {
    StringifyHelper(v, std::make_index_sequence<sizeof...(T)>(), ss);
  }
};

}  // namespace internal

// Converts any type to a string, preferring defined operator<<() or ToString()
// methods if they exist.
template <typename T>
std::string ToString(const T& value) {
  std::ostringstream ss;
  internal::ToStringHelper<std::remove_cvref_t<decltype(value)>>::Stringify(
      value, ss);
  return ss.str();
}

BASE_EXPORT std::string ToString(std::string_view sv);
BASE_EXPORT std::string ToString(std::u16string_view sv);
BASE_EXPORT std::string ToString(std::wstring_view sv);

namespace to_string_internal {

template <typename T>
concept SpanConvertsToStringView = requires {
  { as_string_view(span<T>()) };
};

}  // namespace to_string_internal

// Stringify base::span, hopefully in a way that's useful for tests.
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(to_string_internal::SpanConvertsToStringView<ElementType> ||
           requires(const ElementType& t) {
             { ToString(t) };
           })
constexpr std::string ToString(span<ElementType, Extent, InternalPtrType> r) {
  std::string out = "[";
  if constexpr (to_string_internal::SpanConvertsToStringView<ElementType>) {
    const auto sv = as_string_view(r);
    using T = std::remove_cvref_t<ElementType>;
    if constexpr (std::same_as<wchar_t, T>) {
      out += "L\"";
      out += ToString(sv);
    } else if constexpr (std::same_as<char16_t, T>) {
      out += "u\"";
      out += ToString(sv);
    } else {
      out += "\"";
      out += sv;
    }
    out += '\"';
  } else if constexpr (Extent != 0) {
    // It would be nice to use `JoinString()` here, but making that `constexpr`
    // is more trouble than it's worth.
    if (!r.empty()) {
      out += ToString(r.front());
      for (const ElementType& e : r.template subspan<1>()) {
        out += ", ";
        out += ToString(e);
      }
    }
  }
  out += "]";
  return out;
}

}  // namespace base

#endif  // BASE_STRINGS_TO_STRING_H_
