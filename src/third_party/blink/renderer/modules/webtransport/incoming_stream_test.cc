// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webtransport/incoming_stream.h"

#include <utility>

#include "base/test/mock_callback.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/bindings/core/v8/iterable.h"
#include "third_party/blink/renderer/bindings/core/v8/native_value_traits_impl.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_tester.h"
#include "third_party/blink/renderer/bindings/core/v8/script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_traits.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_testing.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_dom_exception.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_readable_stream_byob_reader_read_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_readable_stream_read_result.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/streams/readable_stream.h"
#include "third_party/blink/renderer/core/streams/readable_stream_byob_reader.h"
#include "third_party/blink/renderer/core/streams/readable_stream_default_reader.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/modules/webtransport/web_transport_error.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "v8/include/v8.h"

namespace blink {

namespace {

using ::testing::ElementsAre;
using ::testing::StrictMock;

class IncomingStreamTest : public ::testing::Test {
 public:
  // The default value of |capacity| means some sensible value selected by mojo.
  void CreateDataPipe(uint32_t capacity = 0) {
    MojoCreateDataPipeOptions options;
    options.struct_size = sizeof(MojoCreateDataPipeOptions);
    options.flags = MOJO_CREATE_DATA_PIPE_FLAG_NONE;
    options.element_num_bytes = 1;
    options.capacity_num_bytes = capacity;

    MojoResult result = mojo::CreateDataPipe(&options, data_pipe_producer_,
                                             data_pipe_consumer_);
    if (result != MOJO_RESULT_OK) {
      ADD_FAILURE() << "CreateDataPipe() returned " << result;
    }
  }

  IncomingStream* CreateIncomingStream(const V8TestingScope& scope,
                                       uint32_t capacity = 0) {
    CreateDataPipe(capacity);
    auto* script_state = scope.GetScriptState();
    auto* incoming_stream = MakeGarbageCollected<IncomingStream>(
        script_state, mock_on_abort_.Get(), std::move(data_pipe_consumer_));
    incoming_stream->Init(ASSERT_NO_EXCEPTION);
    return incoming_stream;
  }

  void WriteToPipe(Vector<uint8_t> data) {
    EXPECT_EQ(data_pipe_producer_->WriteAllData(data), MOJO_RESULT_OK);
  }

  void ClosePipe() { data_pipe_producer_.reset(); }

  // Copies the contents of a v8::Value containing a Uint8Array to a Vector.
  static Vector<uint8_t> ToVector(V8TestingScope& scope,
                                  v8::Local<v8::Value> v8value) {
    Vector<uint8_t> ret;

    NotShared<DOMUint8Array> value =
        NativeValueTraits<NotShared<DOMUint8Array>>::NativeValue(
            scope.GetIsolate(), v8value, scope.GetExceptionState());
    if (!value) {
      ADD_FAILURE() << "chunk is not an Uint8Array";
      return ret;
    }
    ret.AppendSpan(value->ByteSpan());
    return ret;
  }

  struct Iterator {
    bool done = false;
    Vector<uint8_t> value;
  };

  // Performs a single read from |reader|, converting the output to the
  // Iterator type. Assumes that the readable stream is not errored.
  static Iterator Read(V8TestingScope& scope,
                       ReadableStreamDefaultReader* reader) {
    auto* script_state = scope.GetScriptState();
    auto read_promise = reader->read(script_state, ASSERT_NO_EXCEPTION);
    ScriptPromiseTester tester(script_state, read_promise);
    tester.WaitUntilSettled();
    EXPECT_TRUE(tester.IsFulfilled());
    return IteratorFromReadResult(scope, tester.Value().V8Value());
  }

  static Iterator Read(V8TestingScope& scope,
                       ReadableStreamBYOBReader* reader,
                       NotShared<DOMArrayBufferView> view) {
    auto* script_state = scope.GetScriptState();
    auto* read_options =
        MakeGarbageCollected<ReadableStreamBYOBReaderReadOptions>();
    auto read_promise =
        reader->read(script_state, view, read_options, ASSERT_NO_EXCEPTION);
    ScriptPromiseTester tester(script_state, read_promise);
    tester.WaitUntilSettled();
    EXPECT_TRUE(tester.IsFulfilled());
    return IteratorFromReadResult(scope, tester.Value().V8Value());
  }

  static Iterator IteratorFromReadResult(V8TestingScope& scope,
                                         v8::Local<v8::Value> result) {
    CHECK(result->IsObject());
    Iterator ret;
    v8::Local<v8::Value> v8value;
    if (!V8UnpackIterationResult(scope.GetScriptState(),
                                 result.As<v8::Object>(), &v8value,
                                 &ret.done)) {
      ADD_FAILURE() << "Couldn't unpack iterator";
      return {};
    }
    if (ret.done) {
      EXPECT_TRUE(v8value->IsUndefined());
      return ret;
    }

    ret.value = ToVector(scope, v8value);
    return ret;
  }

  static Iterator IteratorFromReadResultAllowingValueOnDone(
      V8TestingScope& scope,
      v8::Local<v8::Value> result) {
    CHECK(result->IsObject());
    Iterator ret;
    v8::Local<v8::Value> v8value;
    if (!V8UnpackIterationResult(scope.GetScriptState(),
                                 result.As<v8::Object>(), &v8value,
                                 &ret.done)) {
      ADD_FAILURE() << "Couldn't unpack iterator";
      return {};
    }

    if (!v8value->IsUndefined()) {
      ret.value = ToVector(scope, v8value);
    }

    return ret;
  }

  base::MockOnceCallback<void(std::optional<uint8_t>)> mock_on_abort_;
  test::TaskEnvironment task_environment_;
  mojo::ScopedDataPipeProducerHandle data_pipe_producer_;
  mojo::ScopedDataPipeConsumerHandle data_pipe_consumer_;
};

TEST_F(IncomingStreamTest, Create) {
  V8TestingScope scope;
  auto* incoming_stream = CreateIncomingStream(scope);
  EXPECT_TRUE(incoming_stream->Readable());
}

TEST_F(IncomingStreamTest, ReadArrayBuffer) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  WriteToPipe({'A'});

  Iterator result = Read(scope, reader);
  EXPECT_FALSE(result.done);
  EXPECT_THAT(result.value, ElementsAre('A'));
}

// Respond BYOB requests created before and after receiving data.
TEST_F(IncomingStreamTest, ReadArrayBufferWithBYOBReader) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetBYOBReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  NotShared<DOMArrayBufferView> view =
      NotShared<DOMUint8Array>(DOMUint8Array::Create(1));
  auto* read_options =
      MakeGarbageCollected<ReadableStreamBYOBReaderReadOptions>();
  auto read_promise =
      reader->read(script_state, view, read_options, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, read_promise);
  EXPECT_FALSE(tester.IsFulfilled());

  WriteToPipe({'A', 'B', 'C'});

  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
  Iterator result = IteratorFromReadResult(scope, tester.Value().V8Value());
  EXPECT_FALSE(result.done);
  EXPECT_THAT(result.value, ElementsAre('A'));

  view = NotShared<DOMUint8Array>(DOMUint8Array::Create(2));
  result = Read(scope, reader, view);
  EXPECT_FALSE(result.done);
  EXPECT_THAT(result.value, ElementsAre('B', 'C'));
}

// Ensure that when `min` is less than buffer size, the BYOB reader
// does not resolve until `min` bytes are available.
TEST_F(IncomingStreamTest,
       ReadArrayBufferWithBYOBReaderAndMinOptionLessThanBufferSize) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetBYOBReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);

  // Create a view of 5 bytes.
  NotShared<DOMArrayBufferView> view =
      NotShared<DOMUint8Array>(DOMUint8Array::Create(5));

  // Set `min` = 3.
  auto* read_options =
      MakeGarbageCollected<ReadableStreamBYOBReaderReadOptions>();
  read_options->setMin(3);

  // Start the read before writing any data.
  auto read_promise =
      reader->read(script_state, view, read_options, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, read_promise);

  // Write only 2 bytes: should not fulfill yet since `min` = 3.
  WriteToPipe({'A', 'B'});
  test::RunPendingTasks();
  EXPECT_FALSE(tester.IsFulfilled());

  // Write one more byte (total now = 3): should fulfill.
  WriteToPipe({'C'});
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  Iterator result = IteratorFromReadResult(scope, tester.Value().V8Value());
  EXPECT_FALSE(result.done);
  EXPECT_EQ(result.value.size(), 3u);
  EXPECT_THAT(result.value, ElementsAre('A', 'B', 'C'));
}

// Ensure read with `min` equal to buffer size only resolves when the full
// buffer can be filled.
TEST_F(IncomingStreamTest, ReadArrayBufferWithBYOBReaderMinEqualToBufferSize) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetBYOBReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);

  // Create a view of 4 bytes.
  NotShared<DOMArrayBufferView> view =
      NotShared<DOMUint8Array>(DOMUint8Array::Create(4));

  // Set `min` = 4 (equal to view size).
  auto* read_options =
      MakeGarbageCollected<ReadableStreamBYOBReaderReadOptions>();
  read_options->setMin(4);

  // Start the read before writing any data.
  auto read_promise =
      reader->read(script_state, view, read_options, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, read_promise);

  // Write only 3 bytes, which is not enough to fulfill.
  WriteToPipe({'A', 'B', 'C'});
  test::RunPendingTasks();
  EXPECT_FALSE(tester.IsFulfilled());

  // Write 1 more byte (total = 4), now it should fulfill.
  WriteToPipe({'D'});
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  Iterator result = IteratorFromReadResult(scope, tester.Value().V8Value());
  EXPECT_FALSE(result.done);
  EXPECT_EQ(result.value.size(), 4u);
  EXPECT_THAT(result.value, ElementsAre('A', 'B', 'C', 'D'));
}

// This test verifies that a BYOB read with a `min` requirement resolves with
// available data when the stream is closed remotely before `min` bytes are
// received. Even though `min` was not satisfied, the read must resolve with the
// partial data instead of hanging or throwing, as per the spec behavior for
// stream closure.
TEST_F(IncomingStreamTest, ReadWithMinAndStreamClosure) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetBYOBReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);

  NotShared<DOMArrayBufferView> view =
      NotShared<DOMUint8Array>(DOMUint8Array::Create(4));

  auto* read_options =
      MakeGarbageCollected<ReadableStreamBYOBReaderReadOptions>();
  read_options->setMin(4);

  auto read_promise =
      reader->read(script_state, view, read_options, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, read_promise);

  // Write only 3 bytes.
  WriteToPipe({'A', 'B', 'C'});
  test::RunPendingTasks();
  EXPECT_FALSE(tester.IsFulfilled());

  incoming_stream->OnIncomingStreamClosed(true);
  ClosePipe();

  test::RunPendingTasks();
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  // Validate that 3 bytes were returned.
  Iterator result = IteratorFromReadResultAllowingValueOnDone(
      scope, tester.Value().V8Value());
  EXPECT_TRUE(result.done);
  EXPECT_EQ(result.value.size(), 3u);
  EXPECT_THAT(result.value, ElementsAre('A', 'B', 'C'));
}

// Reading data followed by a remote close should not lose data.
TEST_F(IncomingStreamTest, ReadThenClosedWithFin) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::optional<uint8_t>()));

  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  WriteToPipe({'B'});
  incoming_stream->OnIncomingStreamClosed(true);

  Iterator result1 = Read(scope, reader);
  EXPECT_FALSE(result1.done);
  EXPECT_THAT(result1.value, ElementsAre('B'));

  // This write arrives "out of order" due to the data pipe not being
  // synchronised with the mojo interface.
  WriteToPipe({'C'});
  ClosePipe();

  Iterator result2 = Read(scope, reader);
  EXPECT_FALSE(result2.done);
  EXPECT_THAT(result2.value, ElementsAre('C'));

  Iterator result3 = Read(scope, reader);
  EXPECT_TRUE(result3.done);
}

// Reading data followed by a remote abort should not lose data.
TEST_F(IncomingStreamTest, ReadThenClosedWithoutFin) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::optional<uint8_t>()));

  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  WriteToPipe({'B'});
  incoming_stream->OnIncomingStreamClosed(false);

  Iterator result1 = Read(scope, reader);
  EXPECT_FALSE(result1.done);
  EXPECT_THAT(result1.value, ElementsAre('B'));

  // This write arrives "out of order" due to the data pipe not being
  // synchronized with the mojo interface.
  WriteToPipe({'C'});
  ClosePipe();

  Iterator result2 = Read(scope, reader);
  EXPECT_FALSE(result2.done);

  // Even if the stream is not cleanly closed, we still endeavour to deliver all
  // data.
  EXPECT_THAT(result2.value, ElementsAre('C'));

  auto result3 = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester result3_tester(script_state, result3);
  result3_tester.WaitUntilSettled();
  EXPECT_TRUE(result3_tester.IsRejected());
  DOMException* exception = V8DOMException::ToWrappable(
      scope.GetIsolate(), result3_tester.Value().V8Value());
  ASSERT_TRUE(exception);
  EXPECT_EQ(exception->code(),
            static_cast<uint16_t>(DOMExceptionCode::kNetworkError));
  EXPECT_EQ(exception->message(),
            "The stream was aborted by the remote server");
}

// Reading after remote close should not lose data.
TEST_F(IncomingStreamTest, ClosedWithFinThenRead) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::optional<uint8_t>()));

  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  WriteToPipe({'B'});
  incoming_stream->OnIncomingStreamClosed(true);
  ClosePipe();

  Iterator result1 = Read(scope, reader);
  EXPECT_FALSE(result1.done);
  EXPECT_THAT(result1.value, ElementsAre('B'));

  Iterator result2 = Read(scope, reader);
  EXPECT_TRUE(result2.done);
}

// reader.closed is fulfilled without any read() call, when the stream is empty.
TEST_F(IncomingStreamTest, ClosedWithFinWithoutRead) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::optional<uint8_t>()));

  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  incoming_stream->OnIncomingStreamClosed(true);
  ClosePipe();

  ScriptPromiseTester tester(script_state, reader->closed(script_state));
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
}

TEST_F(IncomingStreamTest, DataPipeResetBeforeClosedWithFin) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::optional<uint8_t>()));

  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  WriteToPipe({'E'});
  ClosePipe();
  incoming_stream->OnIncomingStreamClosed(true);

  Iterator result1 = Read(scope, reader);
  EXPECT_FALSE(result1.done);
  EXPECT_THAT(result1.value, ElementsAre('E'));

  Iterator result2 = Read(scope, reader);
  EXPECT_TRUE(result2.done);
}

TEST_F(IncomingStreamTest, DataPipeResetBeforeClosedWithoutFin) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::optional<uint8_t>()));

  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  WriteToPipe({'F'});
  ClosePipe();
  incoming_stream->OnIncomingStreamClosed(false);

  Iterator result1 = Read(scope, reader);
  EXPECT_FALSE(result1.done);
  EXPECT_THAT(result1.value, ElementsAre('F'));

  auto result2 = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester result2_tester(script_state, result2);
  result2_tester.WaitUntilSettled();
  EXPECT_TRUE(result2_tester.IsRejected());
  DOMException* exception = V8DOMException::ToWrappable(
      scope.GetIsolate(), result2_tester.Value().V8Value());
  ASSERT_TRUE(exception);
  EXPECT_EQ(exception->code(),
            static_cast<uint16_t>(DOMExceptionCode::kNetworkError));
  EXPECT_EQ(exception->message(),
            "The stream was aborted by the remote server");
}

TEST_F(IncomingStreamTest, WriteToPipeWithPendingRead) {
  V8TestingScope scope;

  auto* incoming_stream = CreateIncomingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  auto read_promise = reader->read(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, read_promise);

  test::RunPendingTasks();

  WriteToPipe({'A'});

  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  Iterator result = IteratorFromReadResult(scope, tester.Value().V8Value());
  EXPECT_FALSE(result.done);
  EXPECT_THAT(result.value, ElementsAre('A'));
}

TEST_F(IncomingStreamTest, Cancel) {
  V8TestingScope scope;
  ScriptState* script_state = scope.GetScriptState();

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::make_optional<uint8_t>(0)));

  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  auto promise = reader->cancel(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, promise);

  test::RunPendingTasks();

  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
}

TEST_F(IncomingStreamTest, CancelWithWebTransportError) {
  V8TestingScope scope;
  ScriptState* script_state = scope.GetScriptState();
  v8::Isolate* isolate = scope.GetIsolate();

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::make_optional<uint8_t>(0)));

  v8::Local<v8::Value> error =
      WebTransportError::Create(isolate,
                                /*stream_error_code=*/std::nullopt, "foobar",
                                V8WebTransportErrorSource::Enum::kStream);
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  auto promise = reader->cancel(script_state, ScriptValue(isolate, error),
                                ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, promise);

  test::RunPendingTasks();

  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
}

TEST_F(IncomingStreamTest, CancelWithWebTransportErrorWithCode) {
  V8TestingScope scope;
  ScriptState* script_state = scope.GetScriptState();
  v8::Isolate* isolate = scope.GetIsolate();

  auto* incoming_stream = CreateIncomingStream(scope);

  EXPECT_CALL(mock_on_abort_, Run(std::make_optional<uint8_t>(19)));

  v8::Local<v8::Value> error =
      WebTransportError::Create(isolate,
                                /*stream_error_code=*/19, "foobar",
                                V8WebTransportErrorSource::Enum::kStream);
  auto* reader = incoming_stream->Readable()->GetDefaultReaderForTesting(
      script_state, ASSERT_NO_EXCEPTION);
  auto promise = reader->cancel(script_state, ScriptValue(isolate, error),
                                ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, promise);

  test::RunPendingTasks();

  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
}

}  // namespace

}  // namespace blink
