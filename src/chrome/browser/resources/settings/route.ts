// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chrome://resources/js/assert.js';

import {loadTimeData} from './i18n_setup.js';
import {pageVisibility} from './page_visibility.js';
import type {SettingsRoutes} from './router.js';
import {Route, Router} from './router.js';

/**
 * Add all of the child routes that originate from the privacy route,
 * regardless of whether the privacy section under basic or advanced.
 */
function addPrivacyChildRoutes(r: Partial<SettingsRoutes>) {
  assert(r.PRIVACY);
  r.CLEAR_BROWSER_DATA = r.PRIVACY.createChild('/clearBrowserData');
  r.CLEAR_BROWSER_DATA.isNavigableDialog = true;

  const visibility = pageVisibility || {};

  if (visibility.safetyHub !== false) {
    r.SAFETY_HUB = r.PRIVACY.createChild('/safetyCheck');
  }

  if (loadTimeData.getBoolean('showPrivacyGuide')) {
    r.PRIVACY_GUIDE = r.PRIVACY.createChild('guide');
  }
  r.SITE_SETTINGS = r.PRIVACY.createChild('/content');
  r.SECURITY = r.PRIVACY.createChild('/security');

  r.COOKIES = r.PRIVACY.createChild('/cookies');
  if (loadTimeData.getBoolean('enableIncognitoTrackingProtections') ) {
    r.INCOGNITO_TRACKING_PROTECTIONS = r.PRIVACY.createChild('/incognito');
  }

  if (!loadTimeData.getBoolean('isPrivacySandboxRestricted')) {
    r.PRIVACY_SANDBOX = r.PRIVACY.createChild('/adPrivacy');
    r.PRIVACY_SANDBOX_TOPICS =
        r.PRIVACY_SANDBOX.createChild('/adPrivacy/interests');
    r.PRIVACY_SANDBOX_MANAGE_TOPICS =
        r.PRIVACY_SANDBOX_TOPICS.createChild('/adPrivacy/interests/manage');
    r.PRIVACY_SANDBOX_FLEDGE =
        r.PRIVACY_SANDBOX.createChild('/adPrivacy/sites');
    r.PRIVACY_SANDBOX_AD_MEASUREMENT =
        r.PRIVACY_SANDBOX.createChild('/adPrivacy/measurement');
  } else if (loadTimeData.getBoolean(
                 'isPrivacySandboxRestrictedNoticeEnabled')) {
    r.PRIVACY_SANDBOX = r.PRIVACY.createChild('/adPrivacy');
    // When the view is restricted, but the notice is configured to show, allow
    // measurement settings only.
    r.PRIVACY_SANDBOX_AD_MEASUREMENT =
        r.PRIVACY_SANDBOX.createChild('/adPrivacy/measurement');
  }

  if (loadTimeData.getBoolean('enableSecurityKeysSubpage')) {
    r.SECURITY_KEYS = r.SECURITY.createChild('/securityKeys');
  }

  r.SITE_SETTINGS_ALL = r.SITE_SETTINGS.createChild('all');
  r.SITE_SETTINGS_SITE_DETAILS =
      r.SITE_SETTINGS_ALL.createChild('/content/siteDetails');

  r.SITE_SETTINGS_HANDLERS = r.SITE_SETTINGS.createChild('/handlers');

  // TODO(tommycli): Find a way to refactor these repetitive category
  // routes.
  r.SITE_SETTINGS_ADS = r.SITE_SETTINGS.createChild('ads');
  r.SITE_SETTINGS_AR = r.SITE_SETTINGS.createChild('ar');
  r.SITE_SETTINGS_AUTOMATIC_DOWNLOADS =
      r.SITE_SETTINGS.createChild('automaticDownloads');
  if (loadTimeData.getBoolean('autoPictureInPictureEnabled')) {
    r.SITE_SETTINGS_AUTO_PICTURE_IN_PICTURE =
        r.SITE_SETTINGS.createChild('autoPictureInPicture');
  }
  if (loadTimeData.getBoolean('capturedSurfaceControlEnabled')) {
    r.SITE_SETTINGS_CAPTURED_SURFACE_CONTROL =
        r.SITE_SETTINGS.createChild('capturedSurfaceControl');
  }
  // <if expr="is_chromeos">
  if (loadTimeData.getBoolean('enableSmartCardReadersContentSetting')) {
    r.SITE_SETTINGS_SMART_CARD_READERS =
        r.SITE_SETTINGS.createChild('smartCardReaders');
  }
  // </if>
  r.SITE_SETTINGS_AUTO_VERIFY = r.SITE_SETTINGS.createChild('autoVerify');
  r.SITE_SETTINGS_BACKGROUND_SYNC =
      r.SITE_SETTINGS.createChild('backgroundSync');
  r.SITE_SETTINGS_CAMERA = r.SITE_SETTINGS.createChild('camera');
  r.SITE_SETTINGS_CLIPBOARD = r.SITE_SETTINGS.createChild('clipboard');
  if (loadTimeData.getBoolean('enableHandTrackingContentSetting')) {
    r.SITE_SETTINGS_HAND_TRACKING = r.SITE_SETTINGS.createChild('handTracking');
  }
  r.SITE_SETTINGS_IDLE_DETECTION = r.SITE_SETTINGS.createChild('idleDetection');
  r.SITE_SETTINGS_IMAGES = r.SITE_SETTINGS.createChild('images');
  r.SITE_SETTINGS_MIXEDSCRIPT = r.SITE_SETTINGS.createChild('insecureContent');
  r.SITE_SETTINGS_JAVASCRIPT = r.SITE_SETTINGS.createChild('javascript');
  r.SITE_SETTINGS_JAVASCRIPT_OPTIMIZER = r.SITE_SETTINGS.createChild('v8');
  if (loadTimeData.getBoolean('enableKeyboardLockPrompt')) {
    r.SITE_SETTINGS_KEYBOARD_LOCK = r.SITE_SETTINGS.createChild('keyboardLock');
  }
  r.SITE_SETTINGS_SOUND = r.SITE_SETTINGS.createChild('sound');
  r.SITE_SETTINGS_SENSORS = r.SITE_SETTINGS.createChild('sensors');
  r.SITE_SETTINGS_LOCATION = r.SITE_SETTINGS.createChild('location');
  r.SITE_SETTINGS_MICROPHONE = r.SITE_SETTINGS.createChild('microphone');
  r.SITE_SETTINGS_NOTIFICATIONS = r.SITE_SETTINGS.createChild('notifications');
  r.SITE_SETTINGS_POPUPS = r.SITE_SETTINGS.createChild('popups');
  r.SITE_SETTINGS_MIDI_DEVICES = r.SITE_SETTINGS.createChild('midiDevices');
  r.SITE_SETTINGS_USB_DEVICES = r.SITE_SETTINGS.createChild('usbDevices');
  r.SITE_SETTINGS_HID_DEVICES = r.SITE_SETTINGS.createChild('hidDevices');
  r.SITE_SETTINGS_SERIAL_PORTS = r.SITE_SETTINGS.createChild('serialPorts');
  if (loadTimeData.getBoolean('enableWebPrintingContentSetting')) {
    r.SITE_SETTINGS_WEB_PRINTING = r.SITE_SETTINGS.createChild('webPrinting');
  }
  if (loadTimeData.getBoolean('enableWebBluetoothNewPermissionsBackend')) {
    r.SITE_SETTINGS_BLUETOOTH_DEVICES =
        r.SITE_SETTINGS.createChild('bluetoothDevices');
  }
  r.SITE_SETTINGS_ZOOM_LEVELS = r.SITE_SETTINGS.createChild('zoomLevels');
  r.SITE_SETTINGS_PDF_DOCUMENTS = r.SITE_SETTINGS.createChild('pdfDocuments');
  r.SITE_SETTINGS_PROTECTED_CONTENT =
      r.SITE_SETTINGS.createChild('protectedContent');
  if (loadTimeData.getBoolean('enablePaymentHandlerContentSetting')) {
    r.SITE_SETTINGS_PAYMENT_HANDLER =
        r.SITE_SETTINGS.createChild('paymentHandler');
  }
  if (loadTimeData.getBoolean('enableFederatedIdentityApiContentSetting')) {
    r.SITE_SETTINGS_FEDERATED_IDENTITY_API =
        r.SITE_SETTINGS.createChild('federatedIdentityApi');
  }
  r.SITE_SETTINGS_SITE_DATA = r.SITE_SETTINGS.createChild('siteData');
  r.SITE_SETTINGS_VR = r.SITE_SETTINGS.createChild('vr');
  if (loadTimeData.getBoolean('enableExperimentalWebPlatformFeatures')) {
    r.SITE_SETTINGS_BLUETOOTH_SCANNING =
        r.SITE_SETTINGS.createChild('bluetoothScanning');
  }

  r.SITE_SETTINGS_WINDOW_MANAGEMENT =
      r.SITE_SETTINGS.createChild('windowManagement');
  r.SITE_SETTINGS_FILE_SYSTEM_WRITE = r.SITE_SETTINGS.createChild('filesystem');
  r.SITE_SETTINGS_FILE_SYSTEM_WRITE_DETAILS =
      r.SITE_SETTINGS_FILE_SYSTEM_WRITE.createChild('siteDetails');
  r.SITE_SETTINGS_LOCAL_FONTS = r.SITE_SETTINGS.createChild('localFonts');
  r.SITE_SETTINGS_STORAGE_ACCESS = r.SITE_SETTINGS.createChild('storageAccess');

  if (loadTimeData.getBoolean('enableAutomaticFullscreenContentSetting')) {
    r.SITE_SETTINGS_AUTOMATIC_FULLSCREEN =
        r.SITE_SETTINGS.createChild('automaticFullScreen');
  }
  if (loadTimeData.getBoolean('enableWebAppInstallation')) {
    r.SITE_SETTINGS_WEB_APP_INSTALLATION =
        r.SITE_SETTINGS.createChild('webApplications');
  }
  if (loadTimeData.getBoolean('enableLocalNetworkAccessSetting')) {
    r.SITE_SETTINGS_LOCAL_NETWORK_ACCESS =
        r.SITE_SETTINGS.createChild('localNetworkAccess');
  }
}

/**
 * Adds Route objects for each path.
 */
function createRoutes(): SettingsRoutes {
  const r: Partial<SettingsRoutes> = {};

  // Root pages.
  r.BASIC = new Route('/');
  r.ABOUT = new Route('/help', loadTimeData.getString('aboutPageTitle'));

  r.SEARCH = r.BASIC.createSection(
      '/search', 'search', loadTimeData.getString('searchPageTitle'));

  if (!loadTimeData.getBoolean('isGuest')) {
    r.PEOPLE = r.BASIC.createSection(
        '/people', 'people', loadTimeData.getString('peoplePageTitle'));
    // <if expr="not chromeos_ash">
    r.SIGN_OUT = r.PEOPLE.createChild('/signOut');
    r.SIGN_OUT.isNavigableDialog = true;
    r.IMPORT_DATA = r.PEOPLE.createChild('/importData');
    r.IMPORT_DATA.isNavigableDialog = true;
    // </if>

    r.SYNC = r.PEOPLE.createChild('/syncSetup');
    r.SYNC_ADVANCED = r.SYNC.createChild('/syncSetup/advanced');
  }

  const visibility = pageVisibility || {};

  if (visibility.ai !== false && loadTimeData.getBoolean('showAiPage')) {
    r.AI = r.BASIC.createSection(
        '/ai', 'ai', loadTimeData.getString('aiInnovationsPageTitle'));
    if (loadTimeData.getBoolean('showTabOrganizationControl')) {
      r.AI_TAB_ORGANIZATION = r.AI.createChild('/ai/tabOrganizer');
    }
    if (loadTimeData.getBoolean('showHistorySearchControl')) {
      r.HISTORY_SEARCH = r.AI.createChild('/ai/historySearch');
    }
    if (loadTimeData.getBoolean('showComposeControl')) {
      r.OFFER_WRITING_HELP = r.AI.createChild('/ai/helpMeWrite');
    }
    if (loadTimeData.getBoolean('showCompareControl')) {
      r.COMPARE = r.AI.createChild('/ai/compareProducts');
    }
    // <if expr="enable_glic">
    if (loadTimeData.getBoolean('showGlicSettings')) {
      r.GLIC_SECTION = r.AI.createSection(
          '/ai/glicSection', 'glicSection',
          loadTimeData.getString('glicPageTitle'));
      r.GEMINI = r.GLIC_SECTION.createChild('/ai/gemini');
    }
    // </if>
  }

  // <if expr="not chromeos_ash">
  if (visibility.people !== false) {
    assert(r.PEOPLE);
    r.MANAGE_PROFILE = r.PEOPLE.createChild('/manageProfile');
  }
  // </if>

  if (visibility.appearance !== false) {
    r.APPEARANCE = r.BASIC.createSection(
        '/appearance', 'appearance',
        loadTimeData.getString('appearancePageTitle'));
    r.FONTS = r.APPEARANCE.createChild('/fonts');
  }

  if (visibility.autofill !== false) {
    r.AUTOFILL = r.BASIC.createSection(
        '/autofill', 'autofill', loadTimeData.getString('autofillPageTitle'));
    r.PAYMENTS = r.AUTOFILL.createChild('/payments');
    r.ADDRESSES = r.AUTOFILL.createChild('/addresses');

    if (loadTimeData.getBoolean('showAutofillAiControl')) {
      r.AUTOFILL_AI = r.AUTOFILL.createChild('/autofillAi');
    }

    // <if expr="is_win or is_macosx">
    r.PASSKEYS = r.AUTOFILL.createChild('/passkeys');
    // </if>
  }

  if (visibility.privacy !== false) {
    r.PRIVACY = r.BASIC.createSection(
        '/privacy', 'privacy', loadTimeData.getString('privacyPageTitle'));
    addPrivacyChildRoutes(r);
  }

  // <if expr="not is_chromeos">
  if (visibility.defaultBrowser !== false) {
    r.DEFAULT_BROWSER = r.BASIC.createSection(
        '/defaultBrowser', 'defaultBrowser',
        loadTimeData.getString('defaultBrowser'));
  }
  // </if>

  r.SEARCH_ENGINES = r.SEARCH.createChild('/searchEngines');

  if (visibility.onStartup !== false) {
    r.ON_STARTUP = r.BASIC.createSection(
        '/onStartup', 'onStartup', loadTimeData.getString('onStartup'));
  }

  // Advanced Routes
  if (visibility.advancedSettings !== false) {
    r.ADVANCED = new Route('/advanced');

    r.LANGUAGES = r.ADVANCED.createSection(
        '/languages', 'languages',
        loadTimeData.getString('languagesPageTitle'));
    r.SPELL_CHECK = r.LANGUAGES.createSection('/spellCheck', 'spellCheck');
    // <if expr="not chromeos_ash and not is_macosx">
    r.EDIT_DICTIONARY = r.SPELL_CHECK.createChild('/editDictionary');
    // </if>

    if (visibility.downloads !== false) {
      r.DOWNLOADS = r.ADVANCED.createSection(
          '/downloads', 'downloads',
          loadTimeData.getString('downloadsPageTitle'));
    }

    r.ACCESSIBILITY = r.ADVANCED.createSection(
        '/accessibility', 'a11y', loadTimeData.getString('a11yPageTitle'));

    // <if expr="is_linux">
    r.CAPTIONS = r.ACCESSIBILITY.createChild('/captions');
    // </if>

    // <if expr="not chromeos_ash">
    r.SYSTEM = r.ADVANCED.createSection(
        '/system', 'system', loadTimeData.getString('systemPageTitle'));
    // </if>

    if (visibility.reset !== false) {
      r.RESET = r.ADVANCED.createSection(
          '/reset', 'reset', loadTimeData.getString('resetPageTitle'));
      r.RESET_DIALOG = r.RESET.createChild('/resetProfileSettings');
      r.RESET_DIALOG.isNavigableDialog = true;
      r.TRIGGERED_RESET_DIALOG =
          r.RESET.createChild('/triggeredResetProfileSettings');
      r.TRIGGERED_RESET_DIALOG.isNavigableDialog = true;
    }

    if (visibility.performance !== false) {
      r.PERFORMANCE = r.BASIC.createSection(
          '/performance', 'performance',
          loadTimeData.getString('performancePageTitle'));
    }
  }
  return r as unknown as SettingsRoutes;
}

/**
 * @return A router with the browser settings routes.
 */
export function buildRouter(): Router {
  return new Router(createRoutes());
}

export function resetRouterForTesting(router: Router = buildRouter()) {
  Router.resetInstanceForTesting(router);

  // Update the exported `routes` variable, otherwise it will be holding stale
  // routes from the previous singleton instance.
  routes = Router.getInstance().getRoutes();
}

Router.setInstance(buildRouter());
window.addEventListener('popstate', function() {
  // On pop state, do not push the state onto the window.history again.
  const routerInstance = Router.getInstance();
  routerInstance.setCurrentRoute(
      routerInstance.getRouteForPath(window.location.pathname) ||
          routerInstance.getRoutes().BASIC,
      new URLSearchParams(window.location.search), true);
});

export let routes: SettingsRoutes = Router.getInstance().getRoutes();
