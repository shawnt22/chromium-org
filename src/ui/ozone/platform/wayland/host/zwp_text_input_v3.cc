// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/zwp_text_input_v3.h"

#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/notimplemented.h"
#include "base/numerics/safe_conversions.h"
#include "ui/base/wayland/wayland_client_input_types.h"
#include "ui/gfx/range/range.h"
#include "ui/ozone/platform/wayland/common/wayland_util.h"
#include "ui/ozone/platform/wayland/host/span_style.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_seat.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"

namespace ui {

namespace {

// Converts Chrome's TextInputType into wayland's content_purpose.
// Some of TextInputType values do not have clearly corresponding wayland value,
// and they fallback to closer type.
uint32_t InputTypeToContentPurpose(TextInputType input_type) {
  switch (input_type) {
    case TEXT_INPUT_TYPE_NONE:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    case TEXT_INPUT_TYPE_TEXT:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    case TEXT_INPUT_TYPE_PASSWORD:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
    case TEXT_INPUT_TYPE_SEARCH:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    case TEXT_INPUT_TYPE_EMAIL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
    case TEXT_INPUT_TYPE_NUMBER:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER;
    case TEXT_INPUT_TYPE_TELEPHONE:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE;
    case TEXT_INPUT_TYPE_URL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL;
    case TEXT_INPUT_TYPE_DATE:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATE;
    case TEXT_INPUT_TYPE_DATE_TIME:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATETIME;
    case TEXT_INPUT_TYPE_DATE_TIME_LOCAL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATETIME;
    case TEXT_INPUT_TYPE_MONTH:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATE;
    case TEXT_INPUT_TYPE_TIME:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TIME;
    case TEXT_INPUT_TYPE_WEEK:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATE;
    case TEXT_INPUT_TYPE_TEXT_AREA:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    case TEXT_INPUT_TYPE_CONTENT_EDITABLE:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    case TEXT_INPUT_TYPE_DATE_TIME_FIELD:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATETIME;
    case TEXT_INPUT_TYPE_NULL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
  }
}

// Converts Chrome's TextInputType into wayland's content_hint.
uint32_t InputFlagsToContentHint(int input_flags) {
  uint32_t hint = 0;
  if (input_flags & TEXT_INPUT_FLAG_AUTOCOMPLETE_ON) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_COMPLETION;
  }
  if (input_flags & TEXT_INPUT_FLAG_SPELLCHECK_ON) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK;
  }
  // No good match. Fallback to SPELLCHECK.
  if (input_flags & TEXT_INPUT_FLAG_AUTOCORRECT_ON) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK;
  }
  if (input_flags & TEXT_INPUT_FLAG_AUTOCAPITALIZE_CHARACTERS) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION;
  }
  if (input_flags & TEXT_INPUT_FLAG_AUTOCAPITALIZE_WORDS) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION;
  }
  if (input_flags & TEXT_INPUT_FLAG_AUTOCAPITALIZE_SENTENCES) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION;
  }
  if (input_flags & TEXT_INPUT_FLAG_HAS_BEEN_PASSWORD) {
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT;
    hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
  }
  return hint;
}

}  // namespace

bool ZwpTextInputV3Impl::SurroundingText::operator==(
    const SurroundingText& other) const {
  // Only compare the data that is sent to IME.
  return text == other.text && cursor == other.cursor && anchor == other.anchor;
}

ZwpTextInputV3Impl::ImeData::ImeData() = default;
ZwpTextInputV3Impl::ImeData::~ImeData() = default;
void ZwpTextInputV3Impl::ImeData::Reset() {
  surrounding_text.reset();
  cursor_rect.reset();
  content_type.reset();
}

ZwpTextInputV3Impl::InputEvents::InputEvents()
    : preedit(std::make_unique<PreeditData>()) {}
ZwpTextInputV3Impl::InputEvents::~InputEvents() = default;
void ZwpTextInputV3Impl::InputEvents::Reset() {
  preedit = std::make_unique<PreeditData>();
  commit = {};
  delete_surrounding_text.reset();
}

ZwpTextInputV3Impl::ZwpTextInputV3Impl(
    WaylandConnection* connection,
    zwp_text_input_manager_v3* text_input_manager)
    : connection_(connection) {
  static constexpr zwp_text_input_v3_listener kTextInputListener = {
      &OnEnter,
      &OnLeave,
      &OnPreeditString,
      &OnCommitString,
      &OnDeleteSurroundingText,
      &OnDone,
  };

  CHECK(text_input_manager);
  auto* text_input = zwp_text_input_manager_v3_get_text_input(
      text_input_manager, connection_->seat()->wl_object());
  obj_ = wl::Object<zwp_text_input_v3>(text_input);

  zwp_text_input_v3_add_listener(text_input, &kTextInputListener, this);
}

ZwpTextInputV3Impl::~ZwpTextInputV3Impl() = default;

void ZwpTextInputV3Impl::Reset() {
  // Clear last committed values.
  committed_ime_data_.Reset();
  // There is no explicit reset API in v3. See [1].
  // Disable+enable to force a reset has been discussed as a possible solution.
  // But this is not implemented yet in compositors. In fact, it was seen in
  // both mutter and kwin that it can cause the IME to enter a grab state
  // unexpectedly. So at this point, leave it unimplemented.
  //
  // If no reset is implemented at all, it can lead to bad user experience,
  // e.g. preedit being duplicated if composition is aborted on the chromium
  // side by clicking in the input field. So the logic below is still needed
  // until a proper fix is in place.
  //
  // Even though chromium expects only preedit to be reset, the surrounding text
  // in fact could change along with reset being called if composition was
  // canceled internally. So we shouldn't keep old surrounding text anyway. See
  // related crbug.com/353915732 where surrounding text update is not sent after
  // reset when composition is canceled.
  //
  // [1]
  // https://gitlab.freedesktop.org/wayland/wayland-protocols/-/merge_requests/34
  pending_ime_data_.Reset();
  pending_input_events_.Reset();
  applied_input_events_.Reset();
}

void ZwpTextInputV3Impl::SetClient(ZwpTextInputV3Client* context) {
  client_ = context;
}

void ZwpTextInputV3Impl::OnClientDestroyed(ZwpTextInputV3Client* context) {
  if (client_ == context) {
    client_ = nullptr;
    Disable();
  }
}

void ZwpTextInputV3Impl::Enable() {
  // Pending state is reset on enable.
  pending_ime_data_.Reset();
  pending_input_events_.Reset();
  applied_input_events_.Reset();
  zwp_text_input_v3_enable(obj_.get());
  Commit();
}

void ZwpTextInputV3Impl::Disable() {
  // Avoid sending pending requests if done is received after disabling.
  pending_ime_data_.Reset();
  // Do not process pending input events after deactivating.
  pending_input_events_.Reset();
  zwp_text_input_v3_disable(obj_.get());
  Commit();
}

bool ZwpTextInputV3Impl::DoneSerialEqualsCommitCount() {
  return committed_ime_data_.commit_count ==
         applied_input_events_.last_done_serial;
}

void ZwpTextInputV3Impl::SetCursorRect(const gfx::Rect& rect) {
  if (committed_ime_data_.cursor_rect &&
      *committed_ime_data_.cursor_rect == rect) {
    // This is to avoid a loop in sending cursor rect and receiving pre-edit
    // string.
    return;
  }
  pending_ime_data_.cursor_rect = std::make_unique<gfx::Rect>(rect);
  SendPendingImeData();
}

bool ZwpTextInputV3Impl::SendCursorRect() {
  CHECK(DoneSerialEqualsCommitCount());
  if (const auto& rect = pending_ime_data_.cursor_rect; rect) {
    zwp_text_input_v3_set_cursor_rectangle(obj_.get(), rect->x(), rect->y(),
                                           rect->width(), rect->height());
    committed_ime_data_.cursor_rect = std::move(pending_ime_data_.cursor_rect);
    return true;
  }
  return false;
}

void ZwpTextInputV3Impl::SetSurroundingText(
    const std::string& text_with_preedit,
    const gfx::Range& preedit_range,
    const gfx::Range& selection_range) {
  auto text = text_with_preedit;
  int32_t anchor, cursor;
  if (!preedit_range.is_empty()) {
    CHECK(preedit_range.IsBoundedBy({0, text_with_preedit.length()}));
    const size_t preedit_min = preedit_range.GetMin();
    const size_t preedit_max = preedit_range.GetMax();
    // Remove preedit portion from surrounding text
    text.erase(preedit_min, preedit_range.length());
    // Now re-calculate selection range
    if (selection_range.IsValid()) {
      auto selection_start = selection_range.start();
      auto selection_end = selection_range.end();
      anchor = selection_start -
               (selection_start <= preedit_min
                    ? 0
                    : std::min(selection_start, preedit_max) - preedit_min);
      cursor = selection_end -
               (selection_end <= preedit_min
                    ? 0
                    : std::min(selection_end, preedit_max) - preedit_min);

    } else {
      // Invalid selection range. Put cursor at preedit position.
      anchor = preedit_min;
      cursor = preedit_min;
    }
  } else {
    anchor = base::checked_cast<int32_t>(
        selection_range.IsValid() ? selection_range.start() : text.length());
    cursor = base::checked_cast<int32_t>(
        selection_range.IsValid() ? selection_range.end() : text.length());
  }
  auto surrounding_text = std::make_unique<SurroundingText>(
      std::move(text), cursor, anchor, text_with_preedit.length(),
      preedit_range.IsValid() ? preedit_range : selection_range);
  if (committed_ime_data_.surrounding_text &&
      *committed_ime_data_.surrounding_text == *surrounding_text) {
    return;
  }
  pending_ime_data_.surrounding_text = std::move(surrounding_text);
  SendPendingImeData();
}

bool ZwpTextInputV3Impl::SendSurroundingText() {
  CHECK(DoneSerialEqualsCommitCount());
  if (const auto& surrounding = pending_ime_data_.surrounding_text) {
    zwp_text_input_v3_set_surrounding_text(
        obj_.get(), surrounding->text.c_str(), surrounding->cursor,
        surrounding->anchor);
    committed_ime_data_.surrounding_text =
        std::move(pending_ime_data_.surrounding_text);
    return true;
  }
  return false;
}

void ZwpTextInputV3Impl::SetContentType(ui::TextInputType type,
                                        uint32_t flags,
                                        bool should_do_learning) {
  uint32_t content_hint = InputFlagsToContentHint(flags);
  if (!should_do_learning) {
    content_hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
  }
  uint32_t content_purpose = InputTypeToContentPurpose(type);
  if (!should_do_learning) {
    content_hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
  }
  auto content_type =
      std::make_unique<ContentType>(content_hint, content_purpose);
  if (committed_ime_data_.content_type &&
      *committed_ime_data_.content_type == *content_type) {
    return;
  }
  pending_ime_data_.content_type = std::move(content_type);
  SendPendingImeData();
}

bool ZwpTextInputV3Impl::SendContentType() {
  CHECK(DoneSerialEqualsCommitCount());
  if (const auto& content_type = pending_ime_data_.content_type) {
    zwp_text_input_v3_set_content_type(obj_.get(), content_type->content_hint,
                                       content_type->content_purpose);
    committed_ime_data_.content_type =
        std::move(pending_ime_data_.content_type);
    return true;
  }
  return false;
}

void ZwpTextInputV3Impl::SendPendingImeData() {
  if (!DoneSerialEqualsCommitCount()) {
    return;
  }
  bool commit = false;
  if (SendContentType()) {
    commit = true;
  }
  if (SendCursorRect()) {
    commit = true;
  }
  if (SendSurroundingText()) {
    commit = true;
  }
  if (commit) {
    Commit();
  }
}

void ZwpTextInputV3Impl::Commit() {
  zwp_text_input_v3_commit(obj_.get());
  // It will wrap around to 0 once it reaches uint32_t max value. It is
  // expected that this will occur on the compositor side as well.
  ++committed_ime_data_.commit_count;
}

void ZwpTextInputV3Impl::OnEnter(void* data,
                                 struct zwp_text_input_v3* text_input,
                                 struct wl_surface* surface) {
  auto* self = static_cast<ZwpTextInputV3Impl*>(data);
  if (auto* window = wl::RootWindowFromWlSurface(surface)) {
    self->connection_->window_manager()->SetTextInputFocusedWindow(window);
  }
}

void ZwpTextInputV3Impl::OnLeave(void* data,
                                 struct zwp_text_input_v3* text_input,
                                 struct wl_surface* surface) {
  auto* self = static_cast<ZwpTextInputV3Impl*>(data);
  self->connection_->window_manager()->SetTextInputFocusedWindow(nullptr);
}

void ZwpTextInputV3Impl::OnPreeditString(void* data,
                                         struct zwp_text_input_v3* text_input,
                                         const char* text,
                                         int32_t cursor_begin,
                                         int32_t cursor_end) {
  auto* self = static_cast<ZwpTextInputV3Impl*>(data);
  self->pending_input_events_.preedit = std::make_unique<PreeditData>(
      text ? text : std::string(), cursor_begin, cursor_end);
}

void ZwpTextInputV3Impl::OnCommitString(void* data,
                                        struct zwp_text_input_v3* text_input,
                                        const char* text) {
  auto* self = static_cast<ZwpTextInputV3Impl*>(data);
  self->pending_input_events_.commit = text ? text : std::string();
}

void ZwpTextInputV3Impl::OnDeleteSurroundingText(
    void* data,
    struct zwp_text_input_v3* text_input,
    uint32_t before_length,
    uint32_t after_length) {
  auto* self = static_cast<ZwpTextInputV3Impl*>(data);
  self->pending_input_events_.delete_surrounding_text =
      std::make_unique<DeleteSurroundingText>(before_length, after_length);
}

void ZwpTextInputV3Impl::OnDone(void* data,
                                struct zwp_text_input_v3* text_input,
                                uint32_t serial) {
  // TODO(crbug.com/40113488) apply delete surrounding
  auto* self = static_cast<ZwpTextInputV3Impl*>(data);
  self->applied_input_events_.last_done_serial = serial;

  if (!self->client_) {
    return;
  }
  auto& surrounding_text = self->committed_ime_data_.surrounding_text;
  const auto& commit_string = self->pending_input_events_.commit;
  const auto& preedit_data = self->pending_input_events_.preedit;
  CHECK(preedit_data);
  const auto& delete_surrounding_text =
      self->pending_input_events_.delete_surrounding_text;
  if (surrounding_text && delete_surrounding_text &&
      surrounding_text->delete_around_range.IsValid()) {
    // Delete surrounding text.
    // If preedit is present, surrounding text will be deleted around the
    // preedit range. So deletion of surrounding text implicitly clears
    // preedit in that case.
    int32_t index = surrounding_text->delete_around_range.start() -
                    delete_surrounding_text->before_length;
    DVLOG_IF(1, index < 0)
        << "got before_length=" << delete_surrounding_text->before_length
        << " which results in negative index for deletion around range="
        << surrounding_text->delete_around_range;
    uint32_t length =
        (index < 0 ? surrounding_text->delete_around_range.end()
                   : delete_surrounding_text->before_length +
                         surrounding_text->delete_around_range.length()) +
        delete_surrounding_text->after_length;
    // Force minimum index of 0.
    index = std::max(0, index);
    if (index + length > surrounding_text->full_length) {
      DVLOG(1) << "got before_length=" << delete_surrounding_text->before_length
               << " after_length=" << delete_surrounding_text->after_length
               << " which makes the deletion around range="
               << surrounding_text->delete_around_range
               << " extend beyond text length="
               << surrounding_text->full_length;
      // Update length to ensure deletion is within text range
      length = surrounding_text->full_length - index;
    }
    self->client_->OnDeleteSurroundingText(index, length);
    self->applied_input_events_.delete_surrounding_text =
        std::move(self->pending_input_events_.delete_surrounding_text);

    // Update the range and surrounding text length to ensure that if another
    // deletion is received before surrounding text is sent again it does not
    // include the deleted text.
    size_t delete_start = base::checked_cast<size_t>(index);
    size_t delete_end = delete_start;
    surrounding_text->full_length -= length;

    // Update full length in case SetSurroundingText is not called before the
    // next delete_surrounding_text + done.
    surrounding_text->full_length += commit_string.length();

    // Add incoming preedit to deletion range and update full length in case
    // SetSurroundingText is not called before the next
    // delete_surrounding_text + done.

    delete_end += preedit_data->text.length();
    surrounding_text->full_length += preedit_data->text.length();
    surrounding_text->delete_around_range =
        gfx::Range(delete_start, delete_end);
  }

  if (!commit_string.empty()) {
    // Replace the existing preedit with the commit string.
    self->client_->OnCommitString(commit_string);
    self->applied_input_events_.commit =
        std::move(self->pending_input_events_.commit);
  }

  // Finally process any new preedit string (only if it changed).
  gfx::Range preedit_cursor =
      (preedit_data->cursor_begin < 0 || preedit_data->cursor_end < 0)
          ? gfx::Range::InvalidRange()
          : gfx::Range(preedit_data->cursor_begin, preedit_data->cursor_end);
  CHECK(self->applied_input_events_.preedit);
  if (*self->applied_input_events_.preedit !=
      *self->pending_input_events_.preedit) {
    self->client_->OnPreeditString(preedit_data->text, {}, preedit_cursor);
    self->applied_input_events_.preedit =
        std::move(self->pending_input_events_.preedit);
  }

  self->pending_input_events_.Reset();
  self->SendPendingImeData();
}

}  // namespace ui
