// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/platform/inspect/ax_inspect_utils_mac.h"

#import <Cocoa/Cocoa.h>
#include <CoreGraphics/CoreGraphics.h>

#include <ostream>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/containers/fixed_flat_set.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/scoped_policy.h"
#include "base/strings/pattern.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/accessibility/platform/ax_platform_tree_manager.h"
#include "ui/accessibility/platform/ax_private_attributes_mac.h"
#include "ui/accessibility/platform/inspect/ax_element_wrapper_mac.h"

// TODO(https://crbug.com/406190900): Remove this deprecation pragma.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace ui {

namespace {

const char kChromeTitle[] = "Google Chrome";
const char kChromiumTitle[] = "Chromium";
const char kFirefoxTitle[] = "Firefox";
const char kSafariTitle[] = "Safari";

NSArray* AXChildrenOf(id node) {
  return AXElementWrapper(node).Children();
}

bool HasAXRole(const char* role, const AXUIElementRef node) {
  AXElementWrapper ax_node((__bridge id)node);
  NSString* node_role =
      *ax_node.GetAttributeValue(NSAccessibilityRoleAttribute);
  return base::SysNSStringToUTF8(node_role) == role;
}

bool HasIDOrClass(const std::string& idOrClass, const AXUIElementRef node) {
  AXElementWrapper nsNode((__bridge id)node);
  NSString* nsIDOrClass = base::SysUTF8ToNSString(idOrClass);
  NSString* idValue =
      *nsNode.GetAttributeValue(NSAccessibilityDOMIdentifierAttribute);
  if ([idValue isEqualToString:nsIDOrClass]) {
    return true;
  }

  NSArray* classList = *nsNode.GetAttributeValue(NSAccessibilityDOMClassList);
  return [classList containsObject:nsIDOrClass];
}

}  // namespace

bool IsValidAXAttribute(const std::string& attribute) {
  static NSSet<NSString*>* valid_attributes = [NSSet setWithArray:@[
    NSAccessibilityAccessKeyAttribute,
    NSAccessibilityARIAAtomicAttribute,
    NSAccessibilityARIABusyAttribute,
    NSAccessibilityARIAColumnCountAttribute,
    NSAccessibilityARIAColumnIndexAttribute,
    NSAccessibilityARIACurrentAttribute,
    NSAccessibilityARIALiveAttribute,
    NSAccessibilityARIAPosInSetAttribute,
    NSAccessibilityARIARelevantAttribute,
    NSAccessibilityARIARowCountAttribute,
    NSAccessibilityARIARowIndexAttribute,
    NSAccessibilityARIASetSizeAttribute,
    NSAccessibilityAutocompleteValueAttribute,
    CrNSAccessibilityBlockQuoteLevelAttribute,
    NSAccessibilityBrailleLabelAttribute,
    NSAccessibilityBrailleRoleDescription,
    NSAccessibilityChromeAXNodeIdAttribute,
    NSAccessibilityColumnHeaderUIElementsAttribute,
    NSAccessibilityDescriptionAttribute,
    NSAccessibilityDetailsElementsAttribute,
    NSAccessibilityDOMClassList,
    NSAccessibilityDropEffectsAttribute,
    NSAccessibilityElementBusyAttribute,
    NSAccessibilityFocusableAncestorAttribute,
    NSAccessibilityGrabbedAttribute,
    NSAccessibilityHasPopupAttribute,
    NSAccessibilityInvalidAttribute,
    NSAccessibilityIsMultiSelectable,
    NSAccessibilityKeyShortcutsValueAttribute,
    NSAccessibilityLoadedAttribute,
    NSAccessibilityLoadingProgressAttribute,
    NSAccessibilityMathFractionNumeratorAttribute,
    NSAccessibilityMathFractionDenominatorAttribute,
    NSAccessibilityMathRootRadicandAttribute,
    NSAccessibilityMathRootIndexAttribute,
    NSAccessibilityMathBaseAttribute,
    NSAccessibilityMathSubscriptAttribute,
    NSAccessibilityMathSuperscriptAttribute,
    NSAccessibilityMathUnderAttribute,
    NSAccessibilityMathOverAttribute,
    NSAccessibilityMathPostscriptsAttribute,
    NSAccessibilityMathPrescriptsAttribute,
    NSAccessibilityOwnsAttribute,
    NSAccessibilityPopupValueAttribute,
    NSAccessibilityRequiredAttribute,
    NSAccessibilityRoleDescriptionAttribute,
    NSAccessibilitySelectedAttribute,
    NSAccessibilitySizeAttribute,
    NSAccessibilityTitleAttribute,
    NSAccessibilityTitleUIElementAttribute,
    NSAccessibilityURLAttribute,
    CrNSAccessibilityVisitedAttribute,
  ]];

  return [valid_attributes containsObject:base::SysUTF8ToNSString(attribute)];
}

base::apple::ScopedCFTypeRef<AXUIElementRef> FindAXUIElement(
    const AXUIElementRef node,
    const char* role) {
  return FindAXUIElement(node, base::BindRepeating(&HasAXRole, role));
}

base::apple::ScopedCFTypeRef<AXUIElementRef> FindAXUIElement(
    const AXUIElementRef node,
    const AXFindCriteria& criteria) {
  if (criteria.Run(node)) {
    return base::apple::ScopedCFTypeRef<AXUIElementRef>(
        node, base::scoped_policy::RETAIN);
  }

  NSArray* children = AXChildrenOf((__bridge id)node);
  for (id child in children) {
    base::apple::ScopedCFTypeRef<AXUIElementRef> found =
        FindAXUIElement((__bridge AXUIElementRef)child, criteria);
    if (found) {
      return found;
    }
  }

  return base::apple::ScopedCFTypeRef<AXUIElementRef>();
}

std::pair<base::apple::ScopedCFTypeRef<AXUIElementRef>, int> FindAXUIElement(
    const AXTreeSelector& selector) {
  int pid;
  base::apple::ScopedCFTypeRef<AXUIElementRef> node;
  std::tie(node, pid) = FindAXApplication(selector);

  // ActiveTab selector.
  if (!node) {
    return {node, pid};
  }

  if (selector.types & AXTreeSelector::ActiveTab) {
    // Only active tab in exposed in browsers, thus find first
    // AXWebArea role.
    node = FindAXUIElement(node.get(), "AXWebArea");
  }

  if (selector.types & AXTreeSelector::IDOrClass) {
    node = FindAXUIElement(
        node.get(), base::BindRepeating(&HasIDOrClass, selector.pattern));
  }

  return {node, pid};
}

std::pair<base::apple::ScopedCFTypeRef<AXUIElementRef>, int> FindAXApplication(
    const AXTreeSelector& selector) {
  if (selector.widget) {
    return {base::apple::ScopedCFTypeRef<AXUIElementRef>(
                AXUIElementCreateApplication(selector.widget)),
            selector.widget};
  }

  std::string title;
  if (selector.types & AXTreeSelector::Chrome)
    title = kChromeTitle;
  else if (selector.types & AXTreeSelector::Chromium)
    title = kChromiumTitle;
  else if (selector.types & AXTreeSelector::Firefox)
    title = kFirefoxTitle;
  else if (selector.types & AXTreeSelector::Safari)
    title = kSafariTitle;
  else
    return {base::apple::ScopedCFTypeRef<AXUIElementRef>(), 0};

  NSArray* windows =
      base::apple::CFToNSOwnershipCast(CGWindowListCopyWindowInfo(
          kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
          kCGNullWindowID));

  for (NSDictionary* window_info in windows) {
    int pid = base::apple::ObjCCast<NSNumber>(window_info[@"kCGWindowOwnerPID"])
                  .intValue;
    std::string window_name = base::SysNSStringToUTF8(
        base::apple::ObjCCast<NSString>(window_info[@"kCGWindowOwnerName"]));

    base::apple::ScopedCFTypeRef<AXUIElementRef> node;

    // Application pre-defined selectors match or application title exact match.
    bool app_title_match = window_name == selector.pattern;
    if (window_name == title || app_title_match) {
      node.reset(AXUIElementCreateApplication(pid));
    }

    // Window title match. Application contain an AXWindow accessible object as
    // a first child, which accessible name contain a window title. For example:
    // 'Inbox (2) - asurkov@igalia.com - Gmail'.
    if (!selector.pattern.empty() && !app_title_match) {
      if (!node) {
        node.reset(AXUIElementCreateApplication(pid));
      }

      base::apple::ScopedCFTypeRef<AXUIElementRef> window =
          FindAXWindowChild(node.get(), selector.pattern);
      if (window) {
        node = window;
      }
    }

    // Found a match.
    if (node)
      return {node, pid};
  }
  return {base::apple::ScopedCFTypeRef<AXUIElementRef>(), 0};
}

base::apple::ScopedCFTypeRef<AXUIElementRef> FindAXWindowChild(
    AXUIElementRef parent,
    const std::string& pattern) {
  NSArray* children = AXChildrenOf((__bridge id)parent);
  if (children.count == 0) {
    return base::apple::ScopedCFTypeRef<AXUIElementRef>();
  }

  id window = children.firstObject;

  AXElementWrapper ax_window(window);
  NSString* role = *ax_window.GetAttributeValue(NSAccessibilityRoleAttribute);
  if (base::SysNSStringToUTF8(role) != "AXWindow") {
    return base::apple::ScopedCFTypeRef<AXUIElementRef>();
  }

  NSString* window_title =
      *ax_window.GetAttributeValue(NSAccessibilityTitleAttribute);
  if (base::MatchPattern(base::SysNSStringToUTF8(window_title), pattern)) {
    return base::apple::ScopedCFTypeRef<AXUIElementRef>(
        (__bridge AXUIElementRef)window, base::scoped_policy::RETAIN);
  }

  return base::apple::ScopedCFTypeRef<AXUIElementRef>();
}

bool IsWebContent(AXUIElementRef element,
                  base::WeakPtr<AXPlatformTreeManager> manager) {
  if (!element || !manager) {
    return false;
  }

  AXElementWrapper wrapper((__bridge id)element);
  NSString* chrome_node_id =
      *wrapper.GetAttributeValue(NSAccessibilityChromeAXNodeIdAttribute);
  if (!chrome_node_id) {
    return false;
  }

  AXPlatformNode* ax_platform_node =
      manager->GetPlatformNodeFromTree([chrome_node_id intValue]);
  return ax_platform_node ? ax_platform_node->IsWebContent() : false;
}

}  // namespace ui

#pragma clang diagnostic pop
