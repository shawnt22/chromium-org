// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/login_ui_service.h"

#include "base/observer_list.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/signin/signin_view_controller.h"
#include "chrome/common/url_constants.h"

#if !BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ui/profiles/profile_picker.h"
#endif  // !BUILDFLAG(IS_CHROMEOS)

LoginUIService::LoginUIService(Profile* profile)
#if !BUILDFLAG(IS_CHROMEOS)
    : profile_(profile)
#endif
{
}

LoginUIService::~LoginUIService() = default;

void LoginUIService::AddObserver(LoginUIService::Observer* observer) {
  observer_list_.AddObserver(observer);
}

void LoginUIService::RemoveObserver(LoginUIService::Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

LoginUIService::LoginUI* LoginUIService::current_login_ui() const {
  return ui_list_.empty() ? nullptr : ui_list_.front();
}

void LoginUIService::SetLoginUI(LoginUI* ui) {
  ui_list_.remove(ui);
  ui_list_.push_front(ui);
}

void LoginUIService::LoginUIClosed(LoginUI* ui) {
  ui_list_.remove(ui);
}

void LoginUIService::SyncConfirmationUIClosed(
    SyncConfirmationUIClosedResult result) {
  for (Observer& observer : observer_list_) {
    observer.OnSyncConfirmationUIClosed(result);
  }
}

void LoginUIService::DisplayLoginResult(Browser* browser,
                                        const SigninUIError& error,
                                        bool from_profile_picker) {
#if BUILDFLAG(IS_CHROMEOS)
  // ChromeOS doesn't have the avatar bubble so it never calls this function.
  NOTREACHED();
#else
  last_login_error_ = error;
  // TODO(crbug.com/40225985): Check if the condition should be `!error.IsOk()`
  if (!error.message().empty()) {
    if (browser) {
      browser->GetFeatures()
          .signin_view_controller()
          ->ShowModalSigninErrorDialog();
    } else {
      LOG(ERROR) << "Unable to show Login error message: " << error.message();
    }
  }
#endif
}

void LoginUIService::SetProfileBlockingErrorMessage() {
#if BUILDFLAG(IS_CHROMEOS)
  NOTREACHED();
#else
  last_login_error_ = SigninUIError::ProfileIsBlocked();
#endif
}

#if !BUILDFLAG(IS_CHROMEOS)
const SigninUIError& LoginUIService::GetLastLoginError() const {
  return last_login_error_;
}
#endif
