// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/toolbar/extension_actions_bridge.h"

#include <memory>

#include "base/android/jni_string.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/android/toolbar/extension_actions_bridge_factory.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "ui/events/android/key_event_android.h"
#include "ui/gfx/android/java_bitmap.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "chrome/browser/ui/android/toolbar/jni_headers/ExtensionAction_jni.h"
#include "chrome/browser/ui/android/toolbar/jni_headers/ExtensionActionsBridge_jni.h"

using base::android::AttachCurrentThread;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using content::WebContents;

namespace extensions {

ExtensionActionsBridge::IconObserver::IconObserver(
    ExtensionActionsBridge* bridge,
    const Extension& extension,
    ExtensionAction& action)
    : bridge_(bridge),
      action_id_(extension.id()),
      icon_factory_(&extension, &action, this) {}

ExtensionActionsBridge::IconObserver::~IconObserver() = default;

gfx::Image ExtensionActionsBridge::IconObserver::GetIcon(int tab_id) {
  return icon_factory_.GetIcon(tab_id);
}

void ExtensionActionsBridge::IconObserver::OnIconUpdated() {
  bridge_->OnToolbarIconUpdated(action_id_);
}

ExtensionActionsBridge::ExtensionActionsBridge(Profile* profile)
    : profile_(profile),
      model_(ToolbarActionsModel::Get(profile)),
      keybinding_registry_(
          std::make_unique<ExtensionKeybindingRegistryAndroid>(profile)) {
  java_object_ = Java_ExtensionActionsBridge_Constructor(
      AttachCurrentThread(), reinterpret_cast<jlong>(this));
  model_observation_.Observe(model_);
}

ExtensionActionsBridge::~ExtensionActionsBridge() {
  Java_ExtensionActionsBridge_destroy(AttachCurrentThread(), java_object_);
}

// static
ExtensionActionsBridge* ExtensionActionsBridge::Get(Profile* profile) {
  return ExtensionActionsBridgeFactory::GetForProfile(profile);
}

ScopedJavaLocalRef<jobject> ExtensionActionsBridge::GetJavaObject() {
  return java_object_.AsLocalRef(AttachCurrentThread());
}

bool ExtensionActionsBridge::AreActionsInitialized(JNIEnv* env) {
  return model_->actions_initialized();
}

std::vector<ToolbarActionsModel::ActionId> ExtensionActionsBridge::GetActionIds(
    JNIEnv* env) {
  const auto& ids = model_->action_ids();
  return std::vector(ids.begin(), ids.end());
}

ScopedJavaLocalRef<jobject> ExtensionActionsBridge::GetAction(
    JNIEnv* env,
    const ToolbarActionsModel::ActionId& action_id,
    int tab_id) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
  DCHECK(registry);
  ExtensionActionManager* manager = ExtensionActionManager::Get(profile_);
  DCHECK(manager);

  const Extension* extension =
      registry->enabled_extensions().GetByID(action_id);
  if (extension == nullptr) {
    return nullptr;
  }

  ExtensionAction* action = manager->GetExtensionAction(*extension);
  if (action == nullptr) {
    return nullptr;
  }

  return Java_ExtensionAction_Constructor(env, action_id,
                                          action->GetTitle(tab_id));
}

ScopedJavaLocalRef<jobject> ExtensionActionsBridge::GetActionIcon(
    JNIEnv* env,
    const ToolbarActionsModel::ActionId& action_id,
    int tab_id) {
  IconObserver* icon_observer = EnsureIconObserver(action_id);
  if (!icon_observer) {
    return nullptr;
  }

  gfx::Image image = icon_observer->GetIcon(tab_id);
  return gfx::ConvertToJavaBitmap(*image.ToSkBitmap());
}

ExtensionAction::ShowAction ExtensionActionsBridge::RunAction(
    JNIEnv* env,
    const ToolbarActionsModel::ActionId& action_id,
    content::WebContents* web_contents) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
  DCHECK(registry);
  ExtensionActionManager* manager = ExtensionActionManager::Get(profile_);
  DCHECK(manager);

  const Extension* extension =
      registry->enabled_extensions().GetByID(action_id);
  if (extension == nullptr) {
    return ExtensionAction::ShowAction::kNone;
  }

  ExtensionActionRunner* runner =
      ExtensionActionRunner::GetForWebContents(web_contents);
  if (runner == nullptr) {
    return ExtensionAction::ShowAction::kNone;
  }

  return runner->RunAction(extension, /*grant_tab_permissions=*/true);
}

bool ExtensionActionsBridge::ExtensionsEnabled(JNIEnv* env) {
  ExtensionManagement* extension_management =
      ExtensionManagementFactory::GetForBrowserContext(profile_);
  return extension_management->ExtensionsEnabledForDesktopAndroid();
}

bool ExtensionActionsBridge::HandleKeyDownEvent(
    JNIEnv* env,
    const ui::KeyEventAndroid& key_event) {
  return keybinding_registry_->HandleKeyDownEvent(key_event);
}

void ExtensionActionsBridge::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  Java_ExtensionActionsBridge_onActionAdded(AttachCurrentThread(), java_object_,
                                            id);
}

void ExtensionActionsBridge::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  RemoveIconObserver(id);
  Java_ExtensionActionsBridge_onActionRemoved(AttachCurrentThread(),
                                              java_object_, id);
}

void ExtensionActionsBridge::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  Java_ExtensionActionsBridge_onActionUpdated(AttachCurrentThread(),
                                              java_object_, id);
}

void ExtensionActionsBridge::OnToolbarModelInitialized() {
  Java_ExtensionActionsBridge_onActionModelInitialized(AttachCurrentThread(),
                                                       java_object_);
}

void ExtensionActionsBridge::OnToolbarPinnedActionsChanged() {
  Java_ExtensionActionsBridge_onPinnedActionsChanged(AttachCurrentThread(),
                                                     java_object_);
}

ExtensionActionsBridge::IconObserver*
ExtensionActionsBridge::EnsureIconObserver(
    const ToolbarActionsModel::ActionId& action_id) {
  if (!icon_observers_.contains(action_id)) {
    ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
    DCHECK(registry);
    ExtensionActionManager* manager = ExtensionActionManager::Get(profile_);
    DCHECK(manager);

    const Extension* extension =
        registry->enabled_extensions().GetByID(action_id);
    if (extension == nullptr) {
      return nullptr;
    }

    ExtensionAction* action = manager->GetExtensionAction(*extension);
    if (action == nullptr) {
      return nullptr;
    }

    icon_observers_.emplace(
        action_id, std::make_unique<IconObserver>(this, *extension, *action));
  }

  auto it = icon_observers_.find(action_id);
  if (it == icon_observers_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void ExtensionActionsBridge::RemoveIconObserver(
    const ToolbarActionsModel::ActionId& action_id) {
  icon_observers_.erase(action_id);
}

void ExtensionActionsBridge::OnToolbarIconUpdated(
    const ToolbarActionsModel::ActionId& action_id) {
  Java_ExtensionActionsBridge_onActionIconUpdated(AttachCurrentThread(),
                                                  java_object_, action_id);
}

static ScopedJavaLocalRef<jobject> JNI_ExtensionActionsBridge_Get(
    JNIEnv* env,
    Profile* profile) {
  ExtensionActionsBridge* bridge = ExtensionActionsBridge::Get(profile);
  DCHECK(bridge);
  return bridge->GetJavaObject();
}

}  // namespace extensions
