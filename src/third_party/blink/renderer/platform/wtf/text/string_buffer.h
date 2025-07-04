/*
 * Copyright (C) 2008, 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/351564777): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_WTF_TEXT_STRING_BUFFER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_WTF_TEXT_STRING_BUFFER_H_

#include "base/containers/span.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/assertions.h"
#include "third_party/blink/renderer/platform/wtf/text/string_impl.h"

namespace blink {

// StringBuffer is a thin wrapper of StringImpl::CreateUninitialized().
// It is helpful if the length and Is8Bit flag are known when creating a string.
template <typename CharType>
class StringBuffer {
  DISALLOW_NEW();

 public:
  StringBuffer() = default;

  explicit StringBuffer(unsigned length) {
    base::span<CharType> characters;
    data_ = StringImpl::CreateUninitialized(length, characters);
  }

  StringBuffer(const StringBuffer&) = delete;
  StringBuffer& operator=(const StringBuffer&) = delete;

  ~StringBuffer() = default;

  void Shrink(unsigned new_length);

  // Prefer Span() to length()/Characters().
  base::span<CharType> Span() {
    return base::span<CharType>(Characters(), length());
  }

  unsigned length() const { return data_ ? data_->length() : 0; }
  CharType* Characters() {
    return length() ? const_cast<CharType*>(data_->GetCharacters<CharType>())
                    : nullptr;
  }

  CharType& operator[](unsigned i) {
    SECURITY_DCHECK(i < length());
    return Characters()[i];
  }

  scoped_refptr<StringImpl> Release() { return std::move(data_); }

 private:
  scoped_refptr<StringImpl> data_;
};

template <typename CharType>
void StringBuffer<CharType>::Shrink(unsigned new_length) {
  DCHECK(data_);
  if (data_->length() == new_length)
    return;
  data_ = data_->Substring(0, new_length);
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_WTF_TEXT_STRING_BUFFER_H_
