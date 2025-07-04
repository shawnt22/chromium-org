// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_EXTENSION_FEATURES_H_
#define EXTENSIONS_COMMON_EXTENSION_FEATURES_H_

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "extensions/buildflags/buildflags.h"

namespace extensions_features {

///////////////////////////////////////////////////////////////////////////////
// README!
// * Please keep these features alphabetized. One exception: API features go
//   at the top so that they are visibly grouped together.
// * Adding a new feature for an extension API? Great!
//   Please use the naming style `kApi<Namespace><Method>`, e.g.
//   `kApiTabsCreate`.
//   Note that if you are using the features.json files to restrict your
//   API with the feature (which is usually best practice if you are introducing
//   any new features), you will also have to add the feature entry to the list
//   in extensions/common/features/feature_flags.cc so the features system can
//   detect it.
// * Naming Tips: Even though this file is unique to extensions, base::Features
//   have to be globally unique. Thus, it's often best to give features very
//   specific names (often including "Extension", unlike many C++ class names)
//   since namespacing doesn't otherwise exist.
// * Example: --enable-features=Feature1,Feature2. Info: //base/feature_list.h.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// API Features
///////////////////////////////////////////////////////////////////////////////

// NOTE(devlin): If there are consistently enough of these in flux, it might
// make sense to have their own file.

// Controls the availability of action.openPopup().
BASE_DECLARE_FEATURE(kApiActionOpenPopup);

// Controls the availability of contentSettings.clipboard.
BASE_DECLARE_FEATURE(kApiContentSettingsClipboard);

// Controls the availability of the enterprise.kioskInput API.
BASE_DECLARE_FEATURE(kApiEnterpriseKioskInput);

// Controls the availability of the runtime.actionData API.
// TODO(crbug.com/376354347): Remove this when the experiment is finished.
BASE_DECLARE_FEATURE(kApiRuntimeActionData);

// Controls the availability of adding and removing site access requests with
// the permissions API.
BASE_DECLARE_FEATURE(kApiPermissionsHostAccessRequests);

// Controls the availability of executing user scripts programmatically using
// the userScripts API.
BASE_DECLARE_FEATURE(kApiUserScriptsExecute);

// Controls the availability of specifying different world IDs in the
// userScripts API.
BASE_DECLARE_FEATURE(kApiUserScriptsMultipleWorlds);

// Controls the availability of the odfsConfigPrivate API.
BASE_DECLARE_FEATURE(kApiOdfsConfigPrivate);

// Controls the availability of the
// `enterprise.reportingPrivate.onDataMaskingRulesTriggered` API.
BASE_DECLARE_FEATURE(kApiEnterpriseReportingPrivateOnDataMaskingRulesTriggered);

///////////////////////////////////////////////////////////////////////////////
// Other Features
///////////////////////////////////////////////////////////////////////////////

// For historical reasons, this includes some APIs. Please don't add more APIs.

// Enables the UI in the install prompt which lets a user choose to withhold
// requested host permissions by default.
BASE_DECLARE_FEATURE(kAllowWithholdingExtensionPermissionsOnInstall);

#if BUILDFLAG(ENABLE_DESKTOP_ANDROID_EXTENSIONS)
// Blocks installing extensions on Desktop Android (experimental). This feature
// is available only for Desktop Android builds.
// This feature should not be added to fieldtrial_testing_config.json, even
// though it may be enabled via Finch, since that would enable it on ToT for
// bots, and we don't want that.
BASE_DECLARE_FEATURE(kBlockInstallingExtensionsOnDesktopAndroid);
#endif  // BUILDFLAG(ENABLE_DESKTOP_ANDROID_EXTENSIONS)

// When enabled, then bad_message::ReceivedBadMessage will be called when
// browser receives an IPC from a content script and the IPC that unexpectedly
// claims to act on behalf of a given extension id, (i.e. even if the browser
// process things that renderer process never run content scripts from the
// extension).
BASE_DECLARE_FEATURE(kCheckingNoExtensionIdInExtensionIpcs);

// If enabled, `ResetURLLoaderFactories()` will not reset extensions'
// service workers URLLoaderFactories used for fetching scripts and
// sub-resources. This avoids disrupting the worker(s) registration(s)
// when they are in flight.
BASE_DECLARE_FEATURE(kSkipResetServiceWorkerURLLoaderFactories);

// If enabled, <webview>s will be allowed to request permission from an
// embedding Chrome App to request access to Human Interface Devices.
BASE_DECLARE_FEATURE(kEnableWebHidInWebView);

// If enabled, disables unpacked extensions if developer mode is off.
BASE_DECLARE_FEATURE(kExtensionDisableUnsupportedDeveloper);

// A replacement key for declaring icons, in addition to supporting dark mode.
BASE_DECLARE_FEATURE(kExtensionIconVariants);

// Controls displaying a warning that affected MV2 extensions may no longer be
// supported.
BASE_DECLARE_FEATURE(kExtensionManifestV2DeprecationWarning);

// Controls disabling affected MV2 extensions that are no longer supported.
// Users can re-enable these extensions.
BASE_DECLARE_FEATURE(kExtensionManifestV2Disabled);

// Controls fully removing support for user-installed MV2 extensions.
// Users may no longer re-enable these extensions. Enterprises may still
// override this.
BASE_DECLARE_FEATURE(kExtensionManifestV2Unsupported);

// Allows server-side configuration of a temporary exception list.
BASE_DECLARE_FEATURE(kExtensionManifestV2ExceptionList);
extern const base::FeatureParam<std::string>
    kExtensionManifestV2ExceptionListParam;

// A feature to allow legacy MV2 extensions, even if they are not supported by
// the browser or experiment configuration. This is important to allow
// developers of MV2 extensions to continue loading, running, and testing their
// extensions for as long as MV2 is supported in any variant.
// This will be removed once the ExtensionManifestV2Availability enterprise
// policy is no longer supported.
BASE_DECLARE_FEATURE(kAllowLegacyMV2Extensions);

// Controls whether server-side redirects are subject to extensions' web
// accessible resource restrictions.
BASE_DECLARE_FEATURE(kExtensionWARForRedirect);

// If enabled, only manifest v3 extensions is allowed while v2 will be disabled.
// Note that this feature is now only checked by `ExtensionManagement` which
// represents enterprise extension configurations. Flip the feature will block
// mv2 extension by default but the error messages will improperly mention
// enterprise policy.
BASE_DECLARE_FEATURE(kExtensionsManifestV3Only);

// Enables enhanced site control for extensions and allowing the user to control
// site permissions.
BASE_DECLARE_FEATURE(kExtensionsMenuAccessControl);

// If enabled, user permitted sites are granted access. This should only happen
// if kExtensionsMenuAccessControl is enabled, since it's the only entry point
// where user could set permitted sites.
BASE_DECLARE_FEATURE(kExtensionsMenuAccessControlWithPermittedSites);

// If enabled, guide users with zero extensions installed to explore the
// benefits of extensions.
// Displays an IPH anchored to the Extensions Toolbar Button, and replaces the
// extensions submenu with an alternative submenu to recommend extensions.
BASE_DECLARE_FEATURE(kExtensionsToolbarZeroState);

// Forces requests to go through WebRequestProxyingURLLoaderFactory.
BASE_DECLARE_FEATURE(kForceWebRequestProxyForTest);

// Launches Native Host executables directly on Windows rather than using a
// cmd.exe process as a proxy.
BASE_DECLARE_FEATURE(kLaunchWindowsNativeHostsDirectly);

// Controls whether omnibox extensions can use the new capability to intercept
// input without needing keyword mode.
BASE_DECLARE_FEATURE(kExperimentalOmniboxLabs);

// To investigate signal beacon loss in crrev.com/c/2262402.
BASE_DECLARE_FEATURE(kReportKeepaliveUkm);

// Reports Extensions.WebRequest.KeepaliveRequestFinished when enabled.
// Automatically disable extensions not included in the Safe Browsing CRX
// allowlist if the user has turned on Enhanced Safe Browsing (ESB). The
// extensions can be disabled at ESB opt-in time or when an extension is moved
// out of the allowlist.
BASE_DECLARE_FEATURE(kSafeBrowsingCrxAllowlistAutoDisable);

// Controls whether we show an install friction dialog when an Enhanced Safe
// Browsing user tries to install an extension that is not included in the
// Safe Browsing CRX allowlist. This feature also controls if we show a warning
// in 'chrome://extensions' for extensions not included in the allowlist.
BASE_DECLARE_FEATURE(kSafeBrowsingCrxAllowlistShowWarnings);

// When enabled, causes Manifest V3 (and greater) extensions to use structured
// cloning (instead of JSON serialization) for extension messaging, except when
// communicating with native messaging hosts.
BASE_DECLARE_FEATURE(kStructuredCloningForMV3Messaging);

// If enabled, APIs of the Telemetry Extension platform that have pending
// approval will be enabled. Read more about the platform here:
// https://chromium.googlesource.com/chromium/src/+/master/docs/telemetry_extension/README.md.
BASE_DECLARE_FEATURE(kTelemetryExtensionPendingApprovalApi);

// Used to control whether downloads initiated by `WebstoreInstaller` are marked
// as having a corresponding user gesture or not.
BASE_DECLARE_FEATURE(kWebstoreInstallerUserGestureKillSwitch);

///////////////////////////////////////////////////////////////////////////////
// STOP!
// Please don't just add your new feature down here.
// See the guidance at the top of this file.
///////////////////////////////////////////////////////////////////////////////

// Enables declarative net request rules to specify response headers as a
// matching condition.
BASE_DECLARE_FEATURE(kDeclarativeNetRequestResponseHeaderMatching);

// Enables a relaxed rule count for "safe" dynamic or session scoped rules above
// the current limit. If disabled, all dynamic and session scoped rules are
// treated as "safe" but the rule limit's value will be the stricter "unsafe"
// limit.
BASE_DECLARE_FEATURE(kDeclarativeNetRequestSafeRuleLimits);

// If enabled, include JS call stack data in the extension API request
// sent to the browser process. This data is used for telemetry purpose
// only.
BASE_DECLARE_FEATURE(kIncludeJSCallStackInExtensionApiRequest);

// If enabled, use the new CWS itemSnippets API to fetch extension info.
BASE_DECLARE_FEATURE(kUseItemSnippetsAPI);

// If enabled, use the new simpler, more efficient service worker task queue.
BASE_DECLARE_FEATURE(kUseNewServiceWorkerTaskQueue);

// Enables declarative net request rules to specify a header substitution action
// type for modifying headers.
BASE_DECLARE_FEATURE(kDeclarativeNetRequestHeaderSubstitution);

// Show no warning banner when an extension uses CDP's `chrome.debugger`.
BASE_DECLARE_FEATURE(kSilentDebuggerExtensionAPI);

// Controls whether the core SiteInstance in ProcessManager is removed. This
// also requires adjusting when some frames are registered with the
// ProcessManager, since they are no longer created directly with an
// extension's SiteInstance (and instead go through a host swap before commit).
// TODO(https://crbug.com/334991035): Remove this feature after we're confident
// nothing breaks.
BASE_DECLARE_FEATURE(kRemoveCoreSiteInstance);

// Disables loading extensions via the `--disable-extensions-except` command
// line switch.
BASE_DECLARE_FEATURE(kDisableDisableExtensionsExceptCommandLineSwitch);

// Disables loading extensions via the `--load-extension` command line switch.
BASE_DECLARE_FEATURE(kDisableLoadExtensionCommandLineSwitch);

// Disables the `--extensions-on-chrome-urls` flag's functionality on
// `chrome://` URLs. Extension can still run on extension URLs using the new
// flag `--extensions-on-extension-urls` flag.
BASE_DECLARE_FEATURE(kDisableExtensionsOnChromeUrlsSwitch);

// Changes the chrome.userScript API to be enabled by a per-extension toggle
// rather than the developer mode toggle on chrome://extensions.
BASE_DECLARE_FEATURE(kUserScriptUserExtensionToggle);

// Forces the debugger API/feature to always be restricted by developer mode.
// This ensures we're always testing the developer mode API/feature restriction
// capability, even when no other API/feature might be restricted by it.
BASE_DECLARE_FEATURE(kDebuggerAPIRestrictedToDevMode);

// Creates a `browser` object that can be used in place of `chrome` where
// extension APIs are available. It does not include non-extension APIs like
// `loadTimes` , `csi`, etc. or deprecated APIs (e.g. `app`).
BASE_DECLARE_FEATURE(kExtensionBrowserNamespaceAlternative);

// Supports chrome.runtime.onMessage() returning a JS Promise to reply to sender
// response callbacks. Promise resolve or rejection value will be sent to the
// sender response callbacks.
BASE_DECLARE_FEATURE(kRuntimeOnMessagePromiseReturnSupport);

// Optimizes service worker start requests by checking readiness before
// initiating a start.
BASE_DECLARE_FEATURE(kOptimizeServiceWorkerStartRequests);

// When enabled, a call to base::ListValue::Clone is avoided when dispatching an
// extension function. Behind a feature to assess impact
// (go/chrome-performance-work-should-be-finched).
// TODO(crbug.com/424432184): Clean up when experiment is complete.
BASE_DECLARE_FEATURE(kAvoidCloneArgsOnExtensionFunctionDispatch);

}  // namespace extensions_features

#endif  // EXTENSIONS_COMMON_EXTENSION_FEATURES_H_
