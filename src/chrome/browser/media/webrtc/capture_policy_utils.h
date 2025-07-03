// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_WEBRTC_CAPTURE_POLICY_UTILS_H_
#define CHROME_BROWSER_MEDIA_WEBRTC_CAPTURE_POLICY_UTILS_H_

#include <vector>

#include "chrome/browser/media/webrtc/desktop_media_list.h"
#include "content/public/common/buildflags.h"

class GURL;
class PrefRegistrySimple;
class PrefService;

namespace content {
class WebContents;
}  // namespace content

// This enum represents the various levels in priority order from most
// restrictive to least restrictive, to which capture may be restricted by
// enterprise policy. It should not be used in Logs, so that it's order may be
// changed as needed.
enum class AllowedScreenCaptureLevel {
  kDisallowed = 0,
  kSameOrigin = 1,
  kTab = 2,
  kWindow = 3,
  kDesktop = 4,
  kUnrestricted = kDesktop,
};

namespace capture_policy {

#if BUILDFLAG(IS_CHROMEOS)
// This pref connects to the MultiScreenCaptureAllowedForUrls policy and will
// replace the deprecated GetDisplayMediaSetSelectAllScreensAllowedForUrls
// policy once the pivot to IWAs is complete.
inline static constexpr char kManagedMultiScreenCaptureAllowedForUrls[] =
    "profile.managed_multi_screen_capture_allowed_for_urls";
#endif

// Gets the highest capture level that the requesting origin is allowed to
// request based on any configured enterprise policies. This is a convenience
// overload which extracts the PrefService from the WebContents.
AllowedScreenCaptureLevel GetAllowedCaptureLevel(
    const GURL& request_origin,
    content::WebContents* capturer_web_contents);

// Gets the highest capture level that the requesting origin is allowed to
// request based on any configured enterprise policies.
AllowedScreenCaptureLevel GetAllowedCaptureLevel(const GURL& request_origin,
                                                 const PrefService& prefs);

// Gets the appropriate DesktopMediaList::WebContentsFilter that should be run
// against every WebContents shown for pickers that include tabs. Functionally
// this returns a no-op unless |capture_level| is kSameOrigin or kDisallowed.
// In the case of the latter, it always returns false, and for the former it
// checks that the WebContents's origin matches |request_origin|.
DesktopMediaList::WebContentsFilter GetIncludableWebContentsFilter(
    const GURL& request_origin,
    AllowedScreenCaptureLevel capture_level);

// Modifies the passed in |media_types| by removing any that are not allowed at
// the specified |capture_level|. Relative Ordering of the remaining items is
// unchanged.
void FilterMediaList(std::vector<DesktopMediaList::Type>& media_types,
                     AllowedScreenCaptureLevel capture_level);

void ShowCaptureTerminatedDialog(content::WebContents* contents);

void RegisterProfilePrefs(PrefRegistrySimple* registry);

// TODO(crbug.com/40230867): Use Origin instead of GURL.
// Passing `std::nullopt` to `url` means "for any origin".
bool IsMultiScreenCaptureAllowed(const std::optional<GURL>& url);

#if BUILDFLAG(ENABLE_SCREEN_CAPTURE)
bool IsTransientActivationRequiredForGetDisplayMedia(
    content::WebContents* contents);
#endif  // BUILDFLAG(ENABLE_SCREEN_CAPTURE)

}  // namespace capture_policy

#endif  // CHROME_BROWSER_MEDIA_WEBRTC_CAPTURE_POLICY_UTILS_H_
