// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/webui/examples/renderer/render_frame_observer.h"

#include "base/check.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "components/guest_contents/renderer/swap_render_frame.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/v8_value_converter.h"
#include "ipc/ipc_sync_channel.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/web/web_custom_element.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-external.h"
#include "v8/include/v8-function-callback.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-local-handle.h"
#include "v8/include/v8-primitive.h"

namespace webui_examples {

namespace {

class RenderFrameStatus final : public content::RenderFrameObserver {
 public:
  explicit RenderFrameStatus(content::RenderFrame* render_frame)
      : content::RenderFrameObserver(render_frame) {}
  ~RenderFrameStatus() final = default;

  bool IsRenderFrameAvailable() { return render_frame() != nullptr; }

  // RenderFrameObserver implementation.
  void OnDestruct() final {}
};

void AllowCustomElementNameRegistration(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsFunction());
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Function> callback = v8::Local<v8::Function>::Cast(args[0]);
  blink::WebCustomElement::EmbedderNamesAllowedScope embedder_names_scope;
  callback->Call(context, context->Global(), 0, nullptr).ToLocalChecked();
}

content::RenderFrame* GetRenderFrame(v8::Local<v8::Value> value) {
  v8::Local<v8::Context> context;
  if (!v8::Local<v8::Object>::Cast(value)->GetCreationContext().ToLocal(
          &context)) {
    if (context.IsEmpty()) {
      return nullptr;
    }
  }
  blink::WebLocalFrame* frame = blink::WebLocalFrame::FrameForContext(context);
  if (!frame) {
    return nullptr;
  }
  return content::RenderFrame::FromWebFrame(frame);
}

void AttachIframeGuest(const v8::FunctionCallbackInfo<v8::Value>& args) {
  // attachIframeGuest(guestInstanceId, attachParams, contentWindow)
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsInt32());
  CHECK(args[1]->IsObject());
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope handle_scope(isolate);
  int guest_contents_id = args[0].As<v8::Int32>()->Value();
  // Getting the attach params could destroy the frame while it executes JS,
  // so observe the render frame for destruction. We don't expect for this to
  // occur in the Webshell, so we CHECK against it.
  content::RenderFrame* render_frame = GetRenderFrame(args[1]);
  RenderFrameStatus render_frame_status(render_frame);
  CHECK(render_frame_status.IsRenderFrameAvailable());

  blink::WebLocalFrame* frame = render_frame->GetWebFrame();
  // Parent must exist.
  blink::WebFrame* parent_frame = frame->Parent();
  CHECK(parent_frame);
  CHECK(parent_frame->IsWebLocalFrame());

  guest_contents::renderer::SwapRenderFrame(render_frame, guest_contents_id);

  args.GetReturnValue().SetUndefined();
}

// Helper to manage the various V8 required scopes and variables.
class V8BinderContext {
 public:
  using V8Callback =
      base::RepeatingCallback<void(const v8::FunctionCallbackInfo<v8::Value>&)>;

  explicit V8BinderContext(content::RenderFrame* render_frame)
      : isolate_(
            render_frame->GetWebFrame()->GetAgentGroupScheduler()->Isolate()),
        handle_scope_(isolate_),
        context_(render_frame->GetWebFrame()->MainWorldScriptContext()),
        context_scope_(context_) {}
  V8BinderContext(const V8BinderContext&) = delete;
  const V8BinderContext& operator=(const V8BinderContext&) = delete;

  void CreateWebshellObject() {
    object_ = v8::Object::New(isolate_);
    context_->Global()
        ->CreateDataProperty(context_, CreateV8String("webshell"), object_)
        .FromJust();
  }

  void AddCallbackToWebshellObject(const char* name, V8Callback callback) {
    v8::Local<v8::Object> callback_holder = v8::Object::New(isolate_);
    v8::Global<v8::Object> global_callback_holder(isolate_, callback_holder);
    std::unique_ptr<V8Callback> callback_container =
        std::make_unique<V8Callback>(std::move(callback));
    SetPrivateData(callback_holder, kCallback,
                   v8::External::New(isolate_, callback_container.get()));
    global_callback_holder.SetWeak(callback_container.release(),
                                   CleanupV8Callback,
                                   v8::WeakCallbackType::kParameter);
    object_
        ->CreateDataProperty(
            context_, CreateV8String(name),
            v8::Function::New(context_, CallCallback, callback_holder)
                .ToLocalChecked())
        .FromJust();
  }

 private:
  static constexpr char kCallback[] = "callback";

  void SetPrivateData(v8::Local<v8::Object> object,
                      const char* key,
                      v8::Local<v8::Value> value) {
    object->SetPrivate(
        context_, v8::Private::ForApi(isolate_, CreateV8String(key)), value);
  }

  static bool GetPrivateData(v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object,
                             const char* key,
                             v8::Local<v8::Value>* value) {
    v8::Isolate* isolate = context->GetIsolate();
    return object
        ->GetPrivate(context,
                     v8::Private::ForApi(isolate, CreateV8String(isolate, key)))
        .ToLocal(value);
  }

  v8::Local<v8::String> CreateV8String(const char* str) {
    return CreateV8String(isolate_, str);
  }

  // static
  static v8::Local<v8::String> CreateV8String(v8::Isolate* isolate,
                                              const char* str) {
    return v8::String::NewFromUtf8(isolate, str,
                                   v8::NewStringType::kInternalized)
        .ToLocalChecked();
  }

  static void CleanupV8Callback(const v8::WeakCallbackInfo<V8Callback>& data) {
    std::unique_ptr<V8Callback> callback(data.GetParameter());
  }

  static void CallCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Object> callback_holder = args.Data().As<v8::Object>();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    v8::Local<v8::Value> callback_function_value;

    if (isolate->IsExecutionTerminating()) {
      return;
    }

    if (!GetPrivateData(context, callback_holder, kCallback,
                        &callback_function_value)) {
      return;
    }

    CHECK(callback_function_value->IsExternal());
    V8Callback* callback = static_cast<V8Callback*>(
        callback_function_value.As<v8::External>()->Value());
    callback->Run(args);
  }

  const raw_ptr<v8::Isolate> isolate_;
  v8::HandleScope handle_scope_;
  v8::Local<v8::Context> context_;
  v8::Context::Scope context_scope_;
  v8::Local<v8::Object> object_;
};

}  // namespace

RenderFrameObserver::RenderFrameObserver(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

RenderFrameObserver::~RenderFrameObserver() = default;

void RenderFrameObserver::SelfOwn(
    std::unique_ptr<RenderFrameObserver> this_instance) {
  DCHECK_EQ(this, this_instance.get());
  this_instance_ = std::move(this_instance);
}

void RenderFrameObserver::OnDestruct() {
  this_instance_.reset();
}

void RenderFrameObserver::DidStartNavigation(
    const GURL& url,
    std::optional<blink::WebNavigationType> navigation_type) {
  if (!url.SchemeIs("chrome") || url.host() != "browser") {
    this_instance_.reset();
    return;
  }
}

void RenderFrameObserver::ReadyToCommitNavigation(
    blink::WebDocumentLoader* document_loader) {
  V8BinderContext binder_context(render_frame());
  binder_context.CreateWebshellObject();
  binder_context.AddCallbackToWebshellObject(
      "allowWebviewElementRegistration",
      base::BindRepeating(&AllowCustomElementNameRegistration));
  binder_context.AddCallbackToWebshellObject(
      "attachIframeGuest", base::BindRepeating(&AttachIframeGuest));
}

}  // namespace webui_examples
