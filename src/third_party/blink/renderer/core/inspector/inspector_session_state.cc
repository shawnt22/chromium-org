// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/inspector/inspector_session_state.h"

#include "third_party/blink/renderer/platform/wtf/text/strcat.h"
#include "third_party/inspector_protocol/crdtp/cbor.h"

namespace blink {
namespace {
using crdtp::span;
using crdtp::SpanFrom;
using crdtp::cbor::CBORTokenizer;
using crdtp::cbor::CBORTokenTag;
using crdtp::cbor::EncodeDouble;
using crdtp::cbor::EncodeFalse;
using crdtp::cbor::EncodeFromLatin1;
using crdtp::cbor::EncodeFromUTF16;
using crdtp::cbor::EncodeInt32;
using crdtp::cbor::EncodeNull;
using crdtp::cbor::EncodeTrue;
}  // namespace

//
// InspectorSessionState
//
InspectorSessionState::InspectorSessionState(
    mojom::blink::DevToolsSessionStatePtr reattach)
    : reattach_state_(std::move(reattach)),
      updates_(mojom::blink::DevToolsSessionState::New()) {}

const mojom::blink::DevToolsSessionState* InspectorSessionState::ReattachState()
    const {
  return reattach_state_.get();
}

void InspectorSessionState::EnqueueUpdate(const WTF::String& key,
                                          const std::vector<uint8_t>* value) {
  std::optional<WTF::Vector<uint8_t>> updated_value;
  if (value) {
    WTF::Vector<uint8_t> payload;
    payload.AppendRange(value->begin(), value->end());
    updated_value = std::move(payload);
  }
  updates_->entries.Set(key, std::move(updated_value));
}

mojom::blink::DevToolsSessionStatePtr InspectorSessionState::TakeUpdates() {
  auto updates = std::move(updates_);
  updates_ = mojom::blink::DevToolsSessionState::New();
  return updates;
}

//
// Encoding / Decoding routines.
//
/*static*/
void InspectorAgentState::Serialize(bool v, std::vector<uint8_t>* out) {
  out->emplace_back(v ? EncodeTrue() : EncodeFalse());
}

/*static*/
bool InspectorAgentState::Deserialize(span<uint8_t> in, bool* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::TRUE_VALUE) {
    *v = true;
    return true;
  }
  if (tokenizer.TokenTag() == CBORTokenTag::FALSE_VALUE) {
    *v = false;
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::Serialize(int32_t v, std::vector<uint8_t>* out) {
  EncodeInt32(v, out);
}

/*static*/
bool InspectorAgentState::Deserialize(span<uint8_t> in, int32_t* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::INT32) {
    *v = tokenizer.GetInt32();
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::Serialize(double v, std::vector<uint8_t>* out) {
  EncodeDouble(v, out);
}

/*static*/
bool InspectorAgentState::Deserialize(span<uint8_t> in, double* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::DOUBLE) {
    *v = tokenizer.GetDouble();
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::Serialize(const WTF::String& v,
                                    std::vector<uint8_t>* out) {
  if (v.Is8Bit()) {
    auto span8 = v.Span8();
    EncodeFromLatin1(span<uint8_t>(span8.data(), span8.size()), out);
  } else {
    auto span16 = v.Span16();
    EncodeFromUTF16(
        span<uint16_t>(reinterpret_cast<const uint16_t*>(span16.data()),
                       span16.size()),
        out);
  }
}

/*static*/
bool InspectorAgentState::Deserialize(span<uint8_t> in, WTF::String* v) {
  CBORTokenizer tokenizer(in);
  if (tokenizer.TokenTag() == CBORTokenTag::STRING8) {
    *v = WTF::String::FromUTF8(tokenizer.GetString8());
    return true;
  }
  if (tokenizer.TokenTag() == CBORTokenTag::STRING16) {
    const crdtp::span<uint8_t> data = tokenizer.GetString16WireRep();
    // SAFETY: GetString16WireRep guarantees `data` is safe.
    *v = WTF::String(UNSAFE_BUFFERS(base::span(
        reinterpret_cast<const UChar*>(data.data()), data.size() / 2)));
    return true;
  }
  return false;
}

/*static*/
void InspectorAgentState::Serialize(const std::vector<uint8_t>& v,
                                    std::vector<uint8_t>* out) {
  // We could CBOR encode this, but since we never look at the contents
  // anyway (except for decoding just below), we just cheat and use the
  // blob directly.
  *out = v;
}

/*static*/
bool InspectorAgentState::Deserialize(span<uint8_t> in,
                                      std::vector<uint8_t>* v) {
  v->insert(v->end(), in.begin(), in.end());
  return true;
}

//
// InspectorAgentState
//
InspectorAgentState::InspectorAgentState(const WTF::String& domain_name)
    : domain_name_(domain_name) {}

WTF::String InspectorAgentState::RegisterField(Field* field) {
  WTF::String prefix_key =
      StrCat({domain_name_, ".", WTF::String::Number(fields_.size()), "/"});
  fields_.push_back(field);
  return prefix_key;
}

void InspectorAgentState::InitFrom(InspectorSessionState* session_state) {
  for (Field* f : fields_)
    f->InitFrom(session_state);
}

void InspectorAgentState::ClearAllFields() {
  for (Field* f : fields_)
    f->Clear();
}

}  // namespace blink
