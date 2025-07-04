// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
import {webUIListenerCallback} from 'chrome://resources/js/cr.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import type {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {ClearBrowsingDataBrowserProxyImpl, ContentSetting, ContentSettingsTypes, CookieControlsMode, SiteSettingsPrefsBrowserProxyImpl} from 'chrome://settings/lazy_load.js';
import type {CrLinkRowElement, Route, SettingsPrefsElement, SettingsPrivacyPageElement, SyncStatus} from 'chrome://settings/settings.js';
import {CrSettingsPrefs, HatsBrowserProxyImpl, MetricsBrowserProxyImpl, PrivacyGuideInteractions, PrivacyPageBrowserProxyImpl, resetRouterForTesting, Router, routes, StatusAction, TrustSafetyInteraction} from 'chrome://settings/settings.js';
import {assertEquals, assertFalse, assertTrue, assertThrows} from 'chrome://webui-test/chai_assert.js';
import {isChildVisible, isVisible} from 'chrome://webui-test/test_util.js';
import {flushTasks} from 'chrome://webui-test/polymer_test_util.js';

import {TestClearBrowsingDataBrowserProxy} from './test_clear_browsing_data_browser_proxy.js';
import {TestHatsBrowserProxy} from './test_hats_browser_proxy.js';
import {TestMetricsBrowserProxy} from './test_metrics_browser_proxy.js';
import {TestPrivacyPageBrowserProxy} from './test_privacy_page_browser_proxy.js';
import {TestSiteSettingsPrefsBrowserProxy} from './test_site_settings_prefs_browser_proxy.js';

const redesignedPages: Route[] = [
  routes.SITE_SETTINGS_ADS,
  routes.SITE_SETTINGS_AR,
  routes.SITE_SETTINGS_AUTOMATIC_DOWNLOADS,
  routes.SITE_SETTINGS_BACKGROUND_SYNC,
  routes.SITE_SETTINGS_CAMERA,
  routes.SITE_SETTINGS_CLIPBOARD,
  routes.SITE_SETTINGS_FEDERATED_IDENTITY_API,
  routes.SITE_SETTINGS_FILE_SYSTEM_WRITE,
  routes.SITE_SETTINGS_HANDLERS,
  routes.SITE_SETTINGS_HID_DEVICES,
  routes.SITE_SETTINGS_IDLE_DETECTION,
  routes.SITE_SETTINGS_IMAGES,
  routes.SITE_SETTINGS_JAVASCRIPT,
  routes.SITE_SETTINGS_JAVASCRIPT_OPTIMIZER,
  routes.SITE_SETTINGS_LOCAL_FONTS,
  routes.SITE_SETTINGS_MICROPHONE,
  routes.SITE_SETTINGS_MIDI_DEVICES,
  routes.SITE_SETTINGS_NOTIFICATIONS,
  routes.SITE_SETTINGS_PAYMENT_HANDLER,
  routes.SITE_SETTINGS_PDF_DOCUMENTS,
  routes.SITE_SETTINGS_POPUPS,
  routes.SITE_SETTINGS_PROTECTED_CONTENT,
  routes.SITE_SETTINGS_SENSORS,
  routes.SITE_SETTINGS_SERIAL_PORTS,
  routes.SITE_SETTINGS_SOUND,
  routes.SITE_SETTINGS_STORAGE_ACCESS,
  routes.SITE_SETTINGS_USB_DEVICES,
  routes.SITE_SETTINGS_VR,

  // WEB_PRINTING is currently only supported on ChromeOS.
  // <if expr="is_chromeos">
  routes.SITE_SETTINGS_WEB_PRINTING,
  // </if>

  // TODO(crbug.com/40719916) After restructure add coverage for elements on
  // routes which depend on flags being enabled.
  // routes.SITE_SETTINGS_BLUETOOTH_SCANNING,
  // routes.SITE_SETTINGS_BLUETOOTH_DEVICES,
  // routes.SITE_SETTINGS_WINDOW_MANAGEMENT,

  // Doesn't contain toggle or radio buttons
  // routes.SITE_SETTINGS_AUTOMATIC_FULLSCREEN,
  // routes.SITE_SETTINGS_INSECURE_CONTENT,
  // routes.SITE_SETTINGS_ZOOM_LEVELS,
];

// clang-format on

suite('PrivacyPage', function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;
  let testClearBrowsingDataBrowserProxy: TestClearBrowsingDataBrowserProxy;
  let metricsBrowserProxy: TestMetricsBrowserProxy;

  suiteSetup(function() {
    loadTimeData.overrideValues({
      isPrivacySandboxRestricted: true,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  function createPage() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);

    return flushTasks();
  }

  setup(function() {
    testClearBrowsingDataBrowserProxy = new TestClearBrowsingDataBrowserProxy();
    ClearBrowsingDataBrowserProxyImpl.setInstance(
        testClearBrowsingDataBrowserProxy);
    const testBrowserProxy = new TestPrivacyPageBrowserProxy();
    PrivacyPageBrowserProxyImpl.setInstance(testBrowserProxy);
    metricsBrowserProxy = new TestMetricsBrowserProxy();
    MetricsBrowserProxyImpl.setInstance(metricsBrowserProxy);

    return createPage();
  });

  teardown(function() {
    page.remove();
    Router.getInstance().navigateTo(routes.BASIC);
    resetRouterForTesting();
  });

  test('showDeleteBrowsingDataDialog', function() {
    assertFalse(!!page.shadowRoot!.querySelector(
        'settings-clear-browsing-data-dialog-v2'));
    page.$.clearBrowsingData.click();
    flush();

    const dialog = page.shadowRoot!.querySelector(
        'settings-clear-browsing-data-dialog-v2');
    assertTrue(!!dialog);
  });

  test('showDeletionConfirmationToast', function() {
    assertFalse(page.$.deleteBrowsingDataToast.open);
    page.$.clearBrowsingData.click();
    flush();

    const dialog = page.shadowRoot!.querySelector(
        'settings-clear-browsing-data-dialog-v2');
    assertTrue(!!dialog);
    dialog.dispatchEvent(new CustomEvent('browsing-data-deleted', {
      bubbles: true,
      composed: true,
      detail: {deletionConfirmationText: 'test'},
    }));
    flush();

    assertTrue(page.$.deleteBrowsingDataToast.open);
    assertEquals('test', page.$.deleteBrowsingDataToast.textContent!.trim());
  });

  // TODO(crbug.com/417690232): Update once its kBundledSecuritySettings is
  // launched.
  test('onSecurityPageClick', function() {
    // Click on the security page row that takes us to
    // chrome://settings/security
    page.$.securityLinkRow.click();
    flush();
    assertEquals(routes.SECURITY, Router.getInstance().getCurrentRoute());

    // Make sure that the new UI is visible and the old UI is not.
    assertTrue(!!page.shadowRoot!.querySelector('settings-security-page-v2'));
    assertFalse(!!page.shadowRoot!.querySelector('settings-security-page'));
  });

  test('NotificationPage', async function() {
    await createPage();

    Router.getInstance().navigateTo(routes.SITE_SETTINGS_NOTIFICATIONS);
    await flushTasks();

    assertTrue(isChildVisible(page, 'settings-notifications-page'));
  });

  test('GeolocationPage', async function() {
    await createPage();

    Router.getInstance().navigateTo(routes.SITE_SETTINGS_LOCATION);
    await flushTasks();

    assertTrue(isChildVisible(page, 'settings-geolocation-page'));
  });

  test('privacySandboxRestricted', function() {
    assertFalse(isChildVisible(page, '#privacySandboxLinkRow'));
  });

  test('LearnMoreHid', async function() {
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_HID_DEVICES);
    await flushTasks();

    const settingsSubpage = page.shadowRoot!.querySelector('settings-subpage');
    assertTrue(!!settingsSubpage);
    assertTrue(isVisible(settingsSubpage));
    assertEquals(
        settingsSubpage.learnMoreUrl,
        'https://support.google.com/chrome?p=webhid&hl=en-US');
  });

  test('LearnMoreSerial', async function() {
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_SERIAL_PORTS);
    await flushTasks();

    const settingsSubpage = page.shadowRoot!.querySelector('settings-subpage');
    assertTrue(!!settingsSubpage);
    assertTrue(isVisible(settingsSubpage));
    assertEquals(
        settingsSubpage.learnMoreUrl,
        'https://support.google.com/chrome?p=webserial&hl=en-US');
  });

  test('LearnMoreUsb', async function() {
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_USB_DEVICES);
    await flushTasks();

    const settingsSubpage = page.shadowRoot!.querySelector('settings-subpage');
    assertTrue(!!settingsSubpage);
    assertTrue(isVisible(settingsSubpage));
    assertEquals(
        settingsSubpage.learnMoreUrl,
        'https://support.google.com/chrome?p=webusb&hl=en-US');
  });

  test('StorageAccessPage', async function() {
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_STORAGE_ACCESS);
    await flushTasks();

    const categorySettingExceptions =
        page.shadowRoot!.querySelectorAll('storage-access-site-list');

    assertEquals(2, categorySettingExceptions.length);
    assertTrue(isVisible(categorySettingExceptions[0]!));
    assertEquals(
        ContentSetting.BLOCK, categorySettingExceptions[0]!.categorySubtype);

    assertTrue(isVisible(categorySettingExceptions[1]!));
    assertEquals(
        ContentSetting.ALLOW, categorySettingExceptions[1]!.categorySubtype);
  });

  test('AutomaticFullscreenPage', async function() {
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_AUTOMATIC_FULLSCREEN);
    await flushTasks();

    const categorySettingExceptions =
        page.shadowRoot!.querySelector('category-setting-exceptions');
    assertTrue(!!categorySettingExceptions);
    assertTrue(isVisible(categorySettingExceptions));
    assertEquals(
        ContentSettingsTypes.AUTOMATIC_FULLSCREEN,
        categorySettingExceptions.category);
  });
});

// Isolated ContentSettingsVisibility test suite due to significantly higher
// execution time (10-20x factor) of that specific tests compare to other
// sub-tests.
suite('ContentSettingsVisibility', function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('ContentSettingsVisibility', async function() {
    // Ensure pages are visited so that HTML components are stamped.
    redesignedPages.forEach(route => Router.getInstance().navigateTo(route));
    await flushTasks();

    // All redesigned pages will use `settings-category-default-radio-group`,
    // except
    //   1. protocol handlers,
    //   2. pdf documents,
    //   3. protected content (except on chromeos and win),
    //   4. notifications (is in its own element)
    let expectedPagesCount = redesignedPages.length - 4;
    // <if expr="is_chromeos or is_win">
    expectedPagesCount += 1;
    // </if>

    assertEquals(
        page.shadowRoot!
            .querySelectorAll('settings-category-default-radio-group')
            .length,
        expectedPagesCount);
  });
});

suite(`PrivacySandbox`, function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;
  let metricsBrowserProxy: TestMetricsBrowserProxy;

  suiteSetup(function() {
    loadTimeData.overrideValues({
      isPrivacySandboxRestricted: false,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    metricsBrowserProxy = new TestMetricsBrowserProxy();
    MetricsBrowserProxyImpl.setInstance(metricsBrowserProxy);

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('privacySandboxRestricted', function() {
    assertTrue(isChildVisible(page, '#privacySandboxLinkRow'));
  });

  test('privacySandboxRowLabel', function() {
    const privacySandboxLinkRow =
        page.shadowRoot!.querySelector<CrLinkRowElement>(
            '#privacySandboxLinkRow');
    assertTrue(!!privacySandboxLinkRow);
    assertEquals(
        loadTimeData.getString('adPrivacyLinkRowLabel'),
        privacySandboxLinkRow.label);
  });

  test('privacySandboxNotExternalLink', function() {
    const privacySandboxLinkRow =
        page.shadowRoot!.querySelector<CrLinkRowElement>(
            '#privacySandboxLinkRow');
    assertTrue(!!privacySandboxLinkRow);
    assertFalse(privacySandboxLinkRow.external);
  });

  test('clickPrivacySandboxRow', async function() {
    const privacySandboxLinkRow =
        page.shadowRoot!.querySelector<HTMLElement>('#privacySandboxLinkRow');
    assertTrue(!!privacySandboxLinkRow);
    privacySandboxLinkRow.click();
    // Ensure UMA is logged.
    assertEquals(
        'Settings.PrivacySandbox.OpenedFromSettingsParent',
        await metricsBrowserProxy.whenCalled('recordAction'));

    // Ensure the correct route has been navigated to when enabling
    // kPrivacySandboxSettings4.
    await flushTasks();
    assertEquals(
        routes.PRIVACY_SANDBOX, Router.getInstance().getCurrentRoute());
  });
});

suite('WebPrintingNotShown', function () {
  test('navigateToWebPrinting', function () {
    assertThrows(() => Router.getInstance().navigateTo(routes.SITE_SETTINGS_WEB_PRINTING));
  });
});

suite(`CookiesSubpage`, function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    loadTimeData.overrideValues({
      isPrivacySandboxRestricted: false,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('cookiesSubpageAttributes', async function() {
    // The subpage is only in the DOM if the corresponding route is open.
    page.shadowRoot!
        .querySelector<CrLinkRowElement>('#thirdPartyCookiesLinkRow')!.click();
    await flushTasks();

    const cookiesSubpage =
        page.shadowRoot!.querySelector<PolymerElement>('#cookies');
    assertTrue(!!cookiesSubpage);
    assertEquals(
        page.i18n('thirdPartyCookiesPageTitle'),
        cookiesSubpage.getAttribute('page-title'));
    const associatedControl = cookiesSubpage.get('associatedControl');
    assertTrue(!!associatedControl);
    assertEquals('thirdPartyCookiesLinkRow', associatedControl.id);
  });

  test('clickCookiesRow', async function() {
    const thirdPartyCookiesLinkRow =
        page.shadowRoot!.querySelector<HTMLElement>(
            '#thirdPartyCookiesLinkRow');
    assertTrue(!!thirdPartyCookiesLinkRow);
    thirdPartyCookiesLinkRow.click();
    // Check that the correct page was navigated to.
    await flushTasks();
    assertEquals(routes.COOKIES, Router.getInstance().getCurrentRoute());
  });
});

suite('CookiesSubpageRedesignDisabled', function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  function createPage() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);

    return flushTasks();
  }

  test(
      'cookiesLinkRowSublabelAlwaysBlock3pcsIncognitoDisabled',
      async function() {
        loadTimeData.overrideValues({
          is3pcdCookieSettingsRedesignEnabled: false,
          isAlwaysBlock3pcsIncognitoEnabled: false,
        });
        resetRouterForTesting();

        await createPage();

        page.set(
            'prefs.profile.cookie_controls_mode.value', CookieControlsMode.OFF);
        const thirdPartyCookiesLinkRow =
            page.shadowRoot!.querySelector<CrLinkRowElement>(
                '#thirdPartyCookiesLinkRow');
        assertTrue(!!thirdPartyCookiesLinkRow);
        assertEquals(
            page.i18n('thirdPartyCookiesLinkRowSublabelEnabled'),
            thirdPartyCookiesLinkRow.subLabel);

        page.set(
            'prefs.profile.cookie_controls_mode.value',
            CookieControlsMode.INCOGNITO_ONLY);
        assertEquals(
            page.i18n('thirdPartyCookiesLinkRowSublabelDisabledIncognito'),
            thirdPartyCookiesLinkRow.subLabel,
        );

        page.set(
            'prefs.profile.cookie_controls_mode.value',
            CookieControlsMode.BLOCK_THIRD_PARTY);
        assertEquals(
            page.i18n('thirdPartyCookiesLinkRowSublabelDisabled'),
            thirdPartyCookiesLinkRow.subLabel);
      });

  test('cookiesLinkRowSublabel', async function() {
    loadTimeData.overrideValues({
      is3pcdCookieSettingsRedesignEnabled: false,
      isAlwaysBlock3pcsIncognitoEnabled: true,
    });
    resetRouterForTesting();

    await createPage();

    page.set(
        'prefs.profile.cookie_controls_mode.value',
        CookieControlsMode.INCOGNITO_ONLY);
    const thirdPartyCookiesLinkRow =
        page.shadowRoot!.querySelector<CrLinkRowElement>(
            '#thirdPartyCookiesLinkRow');
    assertTrue(!!thirdPartyCookiesLinkRow);
    assertEquals(
        page.i18n('thirdPartyCookiesLinkRowSublabelEnabled'),
        thirdPartyCookiesLinkRow.subLabel);
  });
});

suite(`IncognitoTrackingProtectionsSubpage`, function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;
  let metricsBrowserProxy: TestMetricsBrowserProxy;

  suiteSetup(function() {
    loadTimeData.overrideValues({
      enableIncognitoTrackingProtections: true,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    metricsBrowserProxy = new TestMetricsBrowserProxy();
    MetricsBrowserProxyImpl.setInstance(metricsBrowserProxy);

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  teardown(function() {
    resetRouterForTesting();
  });

  test('clickIncognitoTrackingProtectionsRow', async function() {
    const incognitoTrackingProtectionsLinkRow =
        page.shadowRoot!.querySelector<HTMLElement>(
            '#incognitoTrackingProtectionsLinkRow');
    assertTrue(!!incognitoTrackingProtectionsLinkRow);
    incognitoTrackingProtectionsLinkRow.click();

    assertEquals(
        'Settings.TrackingProtections.OpenedFromPrivacyPage',
        await metricsBrowserProxy.whenCalled('recordAction'));
    // Check that the correct page was navigated to.
    await flushTasks();
    assertEquals(
        routes.INCOGNITO_TRACKING_PROTECTIONS, Router.getInstance().getCurrentRoute());
  });

  // TODO(crbug.com/408036586): Remove once kFingerprintingProtectionUx is launched.
  test('IncognitoTrackingProtectionsRowNotVisible', async function () {
    loadTimeData.overrideValues({
      isFingerprintingProtectionUxEnabled: false,
      isIpProtectionUxEnabled: false,
      enableIncognitoTrackingProtections: false,
    });

    page.remove();
    page = document.createElement('settings-privacy-page');
    document.body.appendChild(page);

    await flushTasks();

    assertFalse(isChildVisible(page, '#incognitoTrackingProtectionsLinkRow'));
  });
});

suite(`AllSitesSubpage`, function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('allSitesViewShowsAllSitesTitle', async function() {
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_ALL);
    await flushTasks();

    const allSitesSubpage =
        page.shadowRoot!.querySelector<PolymerElement>('#allSites');
    assertTrue(!!allSitesSubpage);
    assertEquals(
        page.i18n('siteSettingsAllSites'),
        allSitesSubpage.getAttribute('page-title'));
  });

  test('rwsFilterViewShowsRwsTitle', async function() {
    const searchParams =
        new URLSearchParams('searchSubpage=related:foobar.com');
    Router.getInstance().navigateTo(routes.SITE_SETTINGS_ALL, searchParams);
    await flushTasks();

    const allSitesSubpage =
        page.shadowRoot!.querySelector<PolymerElement>('#allSites');
    assertTrue(!!allSitesSubpage);
    assertEquals(
        loadTimeData.getStringF('allSitesRwsFilterViewTitle', 'foobar.com'),
        allSitesSubpage.getAttribute('page-title'));
  });
});

suite(`PrivacySandbox4EnabledButRestricted`, function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    // Note that the browsertest setup ensures these values are set correctly at
    // startup, such that routes are created (or not). They are included here to
    // make clear the intent of the test.
    loadTimeData.overrideValues({
      isPrivacySandboxRestricted: true,
      isPrivacySandboxRestrictedNoticeEnabled: false,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('noPrivacySandboxRowShown', function() {
    assertFalse(isChildVisible(page, '#privacySandboxLinkRow'));
  });

  test('noRouteForAdPrivacyPaths', function() {
    const adPrivacyPaths = [
      routes.PRIVACY_SANDBOX,
      routes.PRIVACY_SANDBOX_AD_MEASUREMENT,
      routes.PRIVACY_SANDBOX_TOPICS,
      routes.PRIVACY_SANDBOX_FLEDGE,
    ];
    for (const path of adPrivacyPaths) {
      assertThrows(() => Router.getInstance().navigateTo(path));
    }
  });
});

suite(`PrivacySandbox4EnabledButRestrictedWithNotice`, function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    // Note that the browsertest setup ensures these values are set correctly at
    // startup, such that routes are created (or not). They are included here to
    // make clear the intent of the test.
    loadTimeData.overrideValues({
      isPrivacySandboxRestricted: true,
      isPrivacySandboxRestrictedNoticeEnabled: true,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('privacySandboxRowShown', function() {
    assertTrue(isChildVisible(page, '#privacySandboxLinkRow'));
  });

  test('noRouteForDisabledAdPrivacyPaths', function() {
    const removedAdPrivacyPaths = [
      routes.PRIVACY_SANDBOX_TOPICS,
      routes.PRIVACY_SANDBOX_FLEDGE,
    ];
    const presentAdPrivacyPaths = [
      routes.PRIVACY_SANDBOX,
      routes.PRIVACY_SANDBOX_AD_MEASUREMENT,
    ];
    for (const path of removedAdPrivacyPaths) {
      assertThrows(() => Router.getInstance().navigateTo(path));
    }
    for (const path of presentAdPrivacyPaths) {
      Router.getInstance().navigateTo(path);
      assertEquals(path, Router.getInstance().getCurrentRoute());
    }
  });

  test('privacySandboxRowSublabel', function() {
    const privacySandboxLinkRow =
        page.shadowRoot!.querySelector<CrLinkRowElement>(
            '#privacySandboxLinkRow');
    assertTrue(!!privacySandboxLinkRow);
    // Ensure that a measurement-specific message is shown in this
    // configuration. The default is tested in the regular
    // PrivacySandbox4Enabled suite.
    assertEquals(
        loadTimeData.getString('adPrivacyRestrictedLinkRowSubLabel'),
        privacySandboxLinkRow.subLabel);
  });
});

suite('PrivacyGuideRow', function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;
  let metricsBrowserProxy: TestMetricsBrowserProxy;

  suiteSetup(function() {
    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    loadTimeData.overrideValues({showPrivacyGuide: true});
    resetRouterForTesting();

    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    metricsBrowserProxy = new TestMetricsBrowserProxy();
    MetricsBrowserProxyImpl.setInstance(metricsBrowserProxy);

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('rowNotShown', async function() {
    loadTimeData.overrideValues({showPrivacyGuide: false});
    resetRouterForTesting();

    page.remove();
    page = document.createElement('settings-privacy-page');
    document.body.appendChild(page);

    await flushTasks();
    assertFalse(
        loadTimeData.getBoolean('showPrivacyGuide'),
        'showPrivacyGuide was not overwritten');
    assertFalse(
        isChildVisible(page, '#privacyGuideLinkRow'),
        'privacyGuideLinkRow is visible');
  });

  test('privacyGuideRowVisibleSupervisedAccount', function() {
    assertTrue(isChildVisible(page, '#privacyGuideLinkRow'));

    // The user signs in to a supervised user account. This hides the privacy
    // guide entry point.
    const syncStatus: SyncStatus = {
      supervisedUser: true,
      statusAction: StatusAction.NO_ACTION,
    };
    webUIListenerCallback('sync-status-changed', syncStatus);
    flush();
    assertFalse(isChildVisible(page, '#privacyGuideLinkRow'));

    // The user is no longer signed in to a supervised user account. This
    // doesn't show the entry point.
    syncStatus.supervisedUser = false;
    webUIListenerCallback('sync-status-changed', syncStatus);
    flush();
    assertFalse(isChildVisible(page, '#privacyGuideLinkRow'));
  });

  test('privacyGuideRowVisibleManaged', function() {
    assertTrue(isChildVisible(page, '#privacyGuideLinkRow'));

    // The user becomes managed. This hides the privacy guide entry point.
    webUIListenerCallback('is-managed-changed', true);
    flush();
    assertFalse(isChildVisible(page, '#privacyGuideLinkRow'));

    // The user is no longer managed. This doesn't show the entry point.
    webUIListenerCallback('is-managed-changed', false);
    flush();
    assertFalse(isChildVisible(page, '#privacyGuideLinkRow'));
  });

  test('privacyGuideRowClick', async function() {
    const privacyGuideLinkRow =
        page.shadowRoot!.querySelector<HTMLElement>('#privacyGuideLinkRow');
    assertTrue(!!privacyGuideLinkRow);
    privacyGuideLinkRow.click();

    const result = await metricsBrowserProxy.whenCalled(
        'recordPrivacyGuideEntryExitHistogram');
    assertEquals(PrivacyGuideInteractions.SETTINGS_LINK_ROW_ENTRY, result);

    // Ensure the correct route has been navigated to.
    assertEquals(routes.PRIVACY_GUIDE, Router.getInstance().getCurrentRoute());

    // Ensure the privacy guide dialog is shown.
    assertTrue(
        !!page.shadowRoot!.querySelector<HTMLElement>('#privacyGuideDialog'));
  });
});

suite('PrivacyPageSound', function() {
  let testBrowserProxy: TestPrivacyPageBrowserProxy;
  let page: SettingsPrivacyPageElement;

  function getToggleElement() {
    return page.shadowRoot!.querySelector<HTMLElement>(
        '#block-autoplay-setting')!;
  }

  setup(() => {
    loadTimeData.overrideValues({enableBlockAutoplayContentSetting: true});
    resetRouterForTesting();

    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    testBrowserProxy = new TestPrivacyPageBrowserProxy();
    PrivacyPageBrowserProxyImpl.setInstance(testBrowserProxy);

    Router.getInstance().navigateTo(routes.SITE_SETTINGS_SOUND);
    page = document.createElement('settings-privacy-page');
    document.body.appendChild(page);
    return flushTasks();
  });

  teardown(() => {
    page.remove();
  });

  test('UpdateStatus', () => {
    assertTrue(getToggleElement().hasAttribute('disabled'));
    assertFalse(getToggleElement().hasAttribute('checked'));

    webUIListenerCallback(
        'onBlockAutoplayStatusChanged', {pref: {value: true}, enabled: true});

    return flushTasks().then(() => {
      // Check that we are on and enabled.
      assertFalse(getToggleElement().hasAttribute('disabled'));
      assertTrue(getToggleElement().hasAttribute('checked'));

      // Toggle the pref off.
      webUIListenerCallback(
          'onBlockAutoplayStatusChanged',
          {pref: {value: false}, enabled: true});

      return flushTasks().then(() => {
        // Check that we are off and enabled.
        assertFalse(getToggleElement().hasAttribute('disabled'));
        assertFalse(getToggleElement().hasAttribute('checked'));

        // Disable the autoplay status toggle.
        webUIListenerCallback(
            'onBlockAutoplayStatusChanged',
            {pref: {value: false}, enabled: false});

        return flushTasks().then(() => {
          // Check that we are off and disabled.
          assertTrue(getToggleElement().hasAttribute('disabled'));
          assertFalse(getToggleElement().hasAttribute('checked'));
        });
      });
    });
  });

  test('Hidden', () => {
    assertTrue(loadTimeData.getBoolean('enableBlockAutoplayContentSetting'));
    assertFalse(getToggleElement().hidden);

    loadTimeData.overrideValues({enableBlockAutoplayContentSetting: false});
    resetRouterForTesting();

    page.remove();
    page = document.createElement('settings-privacy-page');
    document.body.appendChild(page);

    return flushTasks().then(() => {
      assertFalse(loadTimeData.getBoolean('enableBlockAutoplayContentSetting'));
      assertTrue(getToggleElement().hidden);
    });
  });

  test('Click', async () => {
    assertTrue(getToggleElement().hasAttribute('disabled'));
    assertFalse(getToggleElement().hasAttribute('checked'));

    webUIListenerCallback(
        'onBlockAutoplayStatusChanged', {pref: {value: true}, enabled: true});

    await flushTasks();
    // Check that we are on and enabled.
    assertFalse(getToggleElement().hasAttribute('disabled'));
    assertTrue(getToggleElement().hasAttribute('checked'));

    // Click on the toggle and wait for the proxy to be called.
    getToggleElement().click();
    const enabled =
        await testBrowserProxy.whenCalled('setBlockAutoplayEnabled');
    assertFalse(enabled);
  });
});

suite('HappinessTrackingSurveys', function() {
  let testHatsBrowserProxy: TestHatsBrowserProxy;
  let settingsPrefs: SettingsPrefsElement;
  let page: SettingsPrivacyPageElement;

  suiteSetup(function() {
    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    testHatsBrowserProxy = new TestHatsBrowserProxy();
    HatsBrowserProxyImpl.setInstance(testHatsBrowserProxy);

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  teardown(function() {
    page.remove();
    Router.getInstance().navigateTo(routes.BASIC);
  });

  test('ClearBrowsingDataTrigger', async function() {
    page.$.clearBrowsingData.click();
    const interaction =
        await testHatsBrowserProxy.whenCalled('trustSafetyInteractionOccurred');
    assertEquals(TrustSafetyInteraction.USED_PRIVACY_CARD, interaction);
  });

  test('CookiesTrigger', async function() {
    const thirdPartyCookiesLinkRow =
        page.shadowRoot!.querySelector<HTMLElement>(
            '#thirdPartyCookiesLinkRow');
    assertTrue(!!thirdPartyCookiesLinkRow);
    thirdPartyCookiesLinkRow.click();
    const interaction =
        await testHatsBrowserProxy.whenCalled('trustSafetyInteractionOccurred');
    assertEquals(TrustSafetyInteraction.USED_PRIVACY_CARD, interaction);
  });

  test('SecurityTrigger', async function() {
    page.$.securityLinkRow.click();
    const interaction =
        await testHatsBrowserProxy.whenCalled('trustSafetyInteractionOccurred');
    assertEquals(TrustSafetyInteraction.USED_PRIVACY_CARD, interaction);
  });

  test('PermissionsTrigger', async function() {
    page.$.permissionsLinkRow.click();
    const interaction =
        await testHatsBrowserProxy.whenCalled('trustSafetyInteractionOccurred');
    assertEquals(TrustSafetyInteraction.USED_PRIVACY_CARD, interaction);
  });
});

suite('EnableWebBluetoothNewPermissionsBackend', function() {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;
  let testClearBrowsingDataBrowserProxy: TestClearBrowsingDataBrowserProxy;
  let siteSettingsBrowserProxy: TestSiteSettingsPrefsBrowserProxy;
  let metricsBrowserProxy: TestMetricsBrowserProxy;

  suiteSetup(function() {
    loadTimeData.overrideValues({
      isPrivacySandboxRestricted: true,
      enableWebBluetoothNewPermissionsBackend: true,
    });
    resetRouterForTesting();

    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    testClearBrowsingDataBrowserProxy = new TestClearBrowsingDataBrowserProxy();
    ClearBrowsingDataBrowserProxyImpl.setInstance(
        testClearBrowsingDataBrowserProxy);
    const testBrowserProxy = new TestPrivacyPageBrowserProxy();
    PrivacyPageBrowserProxyImpl.setInstance(testBrowserProxy);
    siteSettingsBrowserProxy = new TestSiteSettingsPrefsBrowserProxy();
    SiteSettingsPrefsBrowserProxyImpl.setInstance(siteSettingsBrowserProxy);
    metricsBrowserProxy = new TestMetricsBrowserProxy();
    MetricsBrowserProxyImpl.setInstance(metricsBrowserProxy);

    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  teardown(function() {
    page.remove();
    Router.getInstance().navigateTo(routes.BASIC);
  });

  test('LearnMoreBluetooth', async function() {
    Router.getInstance().navigateTo(
        routes.SITE_SETTINGS.createChild('bluetoothDevices'));
    await flushTasks();

    const settingsSubpage = page.shadowRoot!.querySelector('settings-subpage');
    assertTrue(!!settingsSubpage);
    assertTrue(isVisible(settingsSubpage));
    assertEquals(
        settingsSubpage.learnMoreUrl,
        'https://support.google.com/chrome?p=bluetooth&hl=en-US');
  });
});

// TODO(crbug.com/397187800): Remove once kDbdRevampDesktop is launched.
suite('DeleteBrowsingDataRevampDisabled', () => {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    loadTimeData.overrideValues({
      enableDeleteBrowsingDataRevamp: false,
    });
    resetRouterForTesting();

    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('showClearBrowsingDataDialog', function() {
    assertFalse(!!page.shadowRoot!.querySelector(
        'settings-clear-browsing-data-dialog'));
    page.$.clearBrowsingData.click();
    flush();

    const dialog =
        page.shadowRoot!.querySelector('settings-clear-browsing-data-dialog');
    assertTrue(!!dialog);
  });
});

// TODO(crbug.com/417690232): Remove once kBundledSecuritySettings is launched.
suite('BundledSecuritySettingsDisabled', () => {
  let page: SettingsPrivacyPageElement;
  let settingsPrefs: SettingsPrefsElement;

  suiteSetup(function() {
    settingsPrefs = document.createElement('settings-prefs');
    return CrSettingsPrefs.initialized;
  });

  setup(function() {
    loadTimeData.overrideValues({
      enableBundledSecuritySettings: false,
    });
    resetRouterForTesting();

    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    page = document.createElement('settings-privacy-page');
    page.prefs = settingsPrefs.prefs!;
    document.body.appendChild(page);
    return flushTasks();
  });

  test('renderOriginalSecurityPage', function() {
    assertFalse(!!page.shadowRoot!.querySelector('settings-security-page'));
    page.$.securityLinkRow.click();
    flush();

    assertTrue(!!page.shadowRoot!.querySelector('settings-security-page'));
  });
});
