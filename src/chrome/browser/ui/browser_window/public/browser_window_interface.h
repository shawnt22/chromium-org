// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BROWSER_WINDOW_PUBLIC_BROWSER_WINDOW_INTERFACE_H_
#define CHROME_BROWSER_UI_BROWSER_WINDOW_PUBLIC_BROWSER_WINDOW_INTERFACE_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "content/public/browser/page_navigator.h"

#if !BUILDFLAG(IS_ANDROID)
#include "base/callback_list.h"
#include "ui/base/window_open_disposition.h"
#endif

// This is the public interface for a browser window. Most features in
// //chrome/browser depend on this interface, and thus to prevent circular
// dependencies this interface should not depend on anything else in //chrome.
// Ping erikchen for assistance if this class does not have the functionality
// your feature needs. This comment will be deleted after there are 10+ features
// in BrowserWindowFeatures.
//
// This interface is shared between desktop platforms and the experimental
// desktop android platform. As such, the features exposed directly on this
// class should only be those that apply to all these platforms, and should only
// be features that are core to the concept of a browser window. Classes related
// to specific features should likely instead be stored either as an entry in
// the UnownedUserData (via BrowserWindowInterface::GetUnownedUserDataHost())
// or on DesktopBrowserWindowCapabilities.

#if !BUILDFLAG(IS_ANDROID)
namespace tabs {
class TabInterface;
}  // namespace tabs

namespace views {
class WebView;
class View;
}  // namespace views

namespace web_app {
class AppBrowserController;
}  // namespace web_app

namespace web_modal {
class WebContentsModalDialogHost;
}  // namespace web_modal

class Browser;
class BrowserActions;
class BrowserUserEducationInterface;
class BrowserWindowFeatures;
class DesktopBrowserWindowCapabilities;
class ExclusiveAccessManager;
class GURL;
class ImmersiveModeController;
class Profile;
class SessionID;
class TabStripModel;
#endif  // BUILDFLAG(IS_ANDROID)

namespace ui {
class BaseWindow;
}  // namespace ui

class UnownedUserDataHost;

#if !BUILDFLAG(IS_ANDROID)
// A feature which wants to show window level call to action UI  should call
// BrowserWindowInterface::ShowCallToAction and keep alive the instance of
// ScopedWindowCallToAction for the duration of the window-modal UI.
class ScopedWindowCallToAction {
 public:
  ScopedWindowCallToAction() = default;
  virtual ~ScopedWindowCallToAction() = default;
};
#endif  // !BUILDFLAG(IS_ANDROID)

class BrowserWindowInterface : public content::PageNavigator {
 public:
  // Returns the UnownedUserDataHost associated with this browser window. This
  // is used to retrieve arbitrary features from the browser window without
  // requiring BrowserWindowInterface to have knowledge of them.
  virtual UnownedUserDataHost& GetUnownedUserDataHost() = 0;
  virtual const UnownedUserDataHost& GetUnownedUserDataHost() const = 0;

  // Returns the ui::BaseWindow for this browser window. This allows for
  // generic window actions, such as activation, querying minimize/maximized
  // state, etc.
  virtual ui::BaseWindow* GetWindow() = 0;

  // S T O P
  // Please do not add new features here without consulting desktop leads
  // (erikchen@) and Clank leads (twellington@, dtrainor@). See comment at the
  // top of this file.
  // The following methods will be removed in the future.

#if !BUILDFLAG(IS_ANDROID)
  // Returns nullptr if no browser window with the given session ID exists.
  static BrowserWindowInterface* FromSessionID(const SessionID& session_id);

  // The contents of the active tab is rendered in a views::WebView. When the
  // active tab switches, the contents of the views::WebView is modified, but
  // the instance itself remains the same.
  virtual views::WebView* GetWebView() = 0;

  // Returns the profile that semantically owns this browser window. This value
  // is never null, and never changes for the lifetime of a given browser
  // window. All tabs contained in a browser window have the same
  // profile/BrowserContext as the browser window itself.
  virtual Profile* GetProfile() = 0;

  // Opens a URL, with the given disposition. This is a convenience wrapper
  // around OpenURL from content::PageNavigator.
  virtual void OpenGURL(const GURL& gurl,
                        WindowOpenDisposition disposition) = 0;

  // Returns a session-unique ID.
  virtual const SessionID& GetSessionID() const = 0;

  virtual TabStripModel* GetTabStripModel() = 0;

  // Returns true if the tab strip is currently visible for this browser window.
  // Will return false on browser initialization before the tab strip is
  // initialized.
  virtual bool IsTabStripVisible() = 0;

  // Returns true if the browser controls are hidden due to being in fullscreen.
  virtual bool ShouldHideUIForFullscreen() const = 0;

  // Register callbacks invoked when browser has successfully processed its
  // close request and has been scheduled for deletion.
  using BrowserDidCloseCallback =
      base::RepeatingCallback<void(BrowserWindowInterface*)>;
  virtual base::CallbackListSubscription RegisterBrowserDidClose(
      BrowserDidCloseCallback callback) = 0;

  // Returns the top container view.
  virtual views::View* TopContainer() = 0;

  // WARNING: Many uses of base::WeakPtr are inappropriate and lead to bugs.
  // An appropriate use case is as a variable passed to an asynchronously
  // invoked PostTask.
  // An inappropriate use case is to store as a member of an object that can
  // outlive BrowserWindowInterface. This leads to inconsistent state machines.
  // For example (don't do this):
  // class FooOutlivesBrowser {
  //   base::WeakPtr<BrowserWindowInterface> bwi_;
  //   // Conceptually, this member should only be set if bwi_ is set.
  //   std::optional<SkColor> color_of_browser_;
  // };
  // For example (do this):
  // class FooOutlivesBrowser {
  //   // Use RegisterBrowserDidClose() to clear both bwi_ and
  //   // color_of_browser_ prior to bwi_ destruction.
  //   raw_ptr<BrowserWindowInterface> bwi_;
  //   std::optional<SkColor> color_of_browser_;
  // };
  virtual base::WeakPtr<BrowserWindowInterface> GetWeakPtr() = 0;

  // Returns the view that houses the Lens overlay.
  virtual views::View* LensOverlayView() = 0;

  using ActiveTabChangeCallback =
      base::RepeatingCallback<void(BrowserWindowInterface*)>;
  virtual base::CallbackListSubscription RegisterActiveTabDidChange(
      ActiveTabChangeCallback callback) = 0;

  // Returns the foreground tab. This can be nullptr very early during
  // BrowserWindow initialization, and very late during BrowserWindow teardown.
  virtual tabs::TabInterface* GetActiveTabInterface() = 0;

  // Returns the feature controllers scoped to this browser window.
  // BrowserWindowFeatures that depend on other BrowserWindowFeatures should not
  // use this method. Instead they should use use dependency injection to pass
  // dependencies at construction or initialization. This method exists for
  // three purposes:
  //   (1) TabFeatures often depend on state of BrowserWindowFeatures for the
  //   attached window, which can change. TabFeatures need a way to dynamically
  //   fetch BrowserWindowFeatures.
  //   (2) To expose BrowserWindowFeatures for tests.
  //   (3) It is not possible to perform dependency injection for legacy code
  //   that is conceptually a BrowserWindowFeature and needs access to other
  //   BrowserWindowFeature.
  virtual BrowserWindowFeatures& GetFeatures() = 0;
  virtual const BrowserWindowFeatures& GetFeatures() const = 0;

  // Returns the web contents modal dialog host pertaining to this
  // BrowserWindow.
  virtual web_modal::WebContentsModalDialogHost*
  GetWebContentsModalDialogHostForWindow() = 0;

  // Whether the window is active.
  // The definition of "active" aligns with the window being painted as active
  // instead of the top level widget having focus.
  // Note that this does not work correctly for mac PWA windows, as those are
  // hosted in a separate application with a stub in the browser process.
  virtual bool IsActive() const = 0;

  // Register for these two callbacks to detect changes to IsActive().
  using DidBecomeActiveCallback =
      base::RepeatingCallback<void(BrowserWindowInterface*)>;
  virtual base::CallbackListSubscription RegisterDidBecomeActive(
      DidBecomeActiveCallback callback) = 0;
  using DidBecomeInactiveCallback =
      base::RepeatingCallback<void(BrowserWindowInterface*)>;
  virtual base::CallbackListSubscription RegisterDidBecomeInactive(
      DidBecomeInactiveCallback callback) = 0;

  // This class is responsible for controlling fullscreen and pointer lock.
  virtual ExclusiveAccessManager* GetExclusiveAccessManager() = 0;

  // This class is responsible for controlling the top chrome reveal state while
  // in immersive fullscreen.
  virtual ImmersiveModeController* GetImmersiveModeController() = 0;

  // This class manages actions that a user can take that are scoped to a
  // browser window (e.g. most of the 3-dot menu actions).
  virtual BrowserActions* GetActions() = 0;

  // SessionService::WindowType mirrors these values.  If you add to this
  // enum, look at SessionService::WindowType to see if it needs to be
  // updated.
  // TODO(https://crbug.com/331031753): Several of these existing Window Types
  // likely should not have been using Browser as a base to begin with and
  // should be migrated. Please refrain from adding new types.
  enum Type {
    // Normal tabbed non-app browser (previously TYPE_TABBED).
    TYPE_NORMAL,
    // Popup browser.
    TYPE_POPUP,
    // App browser. Specifically, one of these:
    // * Web app; comes in different flavors but is backed by the same code:
    //   - Progressive Web App (PWA)
    //   - Shortcut app (from 3-dot menu > More tools > Create shortcut)
    //   - System web app (Chrome OS only)
    // * Legacy packaged app ("v1 packaged app")
    // * Hosted app (e.g. the Web Store "app" preinstalled on Chromebooks)
    TYPE_APP,
    // Devtools browser.
    TYPE_DEVTOOLS,
    // App popup browser. It behaves like an app browser (e.g. it should have an
    // AppBrowserController) but looks like a popup (e.g. it never has a tab
    // strip).
    TYPE_APP_POPUP,
#if BUILDFLAG(IS_CHROMEOS)
    // Browser for ARC++ Chrome custom tabs.
    // It's an enhanced version of TYPE_POPUP, and is used to show the Chrome
    // Custom Tab toolbar for ARC++ apps. It has UI customizations like using
    // the Android app's theme color, and the three dot menu in
    // CustomTabToolbarview.
    TYPE_CUSTOM_TAB,
#endif
    // Document picture-in-picture browser.  It's mostly the same as a
    // TYPE_POPUP, except that it floats above other windows.  It also has some
    // additional restrictions, like it cannot navigated, to prevent misuse.
    TYPE_PICTURE_IN_PICTURE,
    // If you add a new type, consider updating the test
    // BrowserTest.StartMaximized.
  };
  virtual Type GetType() const = 0;

  // Gets an object that provides common per-browser-window functionality for
  // user education. The remainder of functionality is provided directly by the
  // UserEducationService, which can be retrieved directly from the profile.
  virtual BrowserUserEducationInterface* GetUserEducationInterface() = 0;

  virtual web_app::AppBrowserController* GetAppBrowserController() = 0;

  // This is used by features that need to operate on most or all tabs in the
  // browser window. Do not use this method to find a specific tab.
  virtual std::vector<tabs::TabInterface*> GetAllTabInterfaces() = 0;

  // Downcasts to a Browser*. The only valid use for this method is when
  // migrating a large chunk of code to BrowserWindowInterface, to allow
  // incremental migration.
  virtual Browser* GetBrowserForMigrationOnly() = 0;

  // Checks if the browser popup is tab modal dialog.
  virtual bool IsTabModalPopupDeprecated() const = 0;

  // Features that want to show a window level call to action UI can be mutually
  // exclusive. Before gating on call to action UI first check
  // `CanShowModCanShowCallToActionalUI`. Then call ShowCallToAction() and keep
  // `ScopedWindowCallToAction` alive to prevent other features from showing
  // window level call to action Uis.
  virtual bool CanShowCallToAction() const = 0;
  virtual std::unique_ptr<ScopedWindowCallToAction> ShowCallToAction() = 0;

  virtual DesktopBrowserWindowCapabilities* capabilities() = 0;
  virtual const DesktopBrowserWindowCapabilities* capabilities() const = 0;
#endif  // !BUILDFLAG(IS_ANDROID)

  // S T O P
  // Please do not add new features here without consulting desktop leads
  // (erikchen@) and Clank leads (twellington@, dtrainor@). See comment at the
  // top of this file.
  // The following methods will be removed in the future.
};

#endif  // CHROME_BROWSER_UI_BROWSER_WINDOW_PUBLIC_BROWSER_WINDOW_INTERFACE_H_
