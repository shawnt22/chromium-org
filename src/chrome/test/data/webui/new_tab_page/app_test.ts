// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {ComposeboxPageHandlerRemote} from 'chrome://new-tab-page/composebox.mojom-webui.js';
import type {CustomizeButtonsDocumentRemote} from 'chrome://new-tab-page/customize_buttons.mojom-webui.js';
import {CustomizeButtonsDocumentCallbackRouter, CustomizeButtonsHandlerRemote, CustomizeChromeSection, SidePanelOpenTrigger} from 'chrome://new-tab-page/customize_buttons.mojom-webui.js';
import type {Module} from 'chrome://new-tab-page/lazy_load.js';
import {ComposeboxProxyImpl, counterfactualLoad, ModuleDescriptor, ModuleRegistry} from 'chrome://new-tab-page/lazy_load.js';
import {$$, BackgroundManager, BrowserCommandProxy, CUSTOMIZE_CHROME_BUTTON_ELEMENT_ID, CustomizeButtonsProxy, CustomizeDialogPage, NewTabPageProxy, NtpCustomizeChromeEntryPoint, NtpElement, VoiceAction, WindowProxy} from 'chrome://new-tab-page/new_tab_page.js';
import type {AppElement, CustomizeButtonsElement} from 'chrome://new-tab-page/new_tab_page.js';
import type {PageRemote} from 'chrome://new-tab-page/new_tab_page.mojom-webui.js';
import {NtpBackgroundImageSource, PageCallbackRouter, PageHandlerRemote} from 'chrome://new-tab-page/new_tab_page.mojom-webui.js';
import type {CrButtonElement} from 'chrome://resources/cr_elements/cr_button/cr_button.js';
import type {CrToastElement} from 'chrome://resources/cr_elements/cr_toast/cr_toast.js';
import {Command, CommandHandlerRemote} from 'chrome://resources/js/browser_command.mojom-webui.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {isMac} from 'chrome://resources/js/platform.js';
import {PromiseResolver} from 'chrome://resources/js/promise_resolver.js';
import {assertDeepEquals, assertEquals, assertFalse, assertNotEquals, assertTrue} from 'chrome://webui-test/chai_assert.js';
import type {MetricsTracker} from 'chrome://webui-test/metrics_test_support.js';
import {fakeMetricsPrivate} from 'chrome://webui-test/metrics_test_support.js';
import type {TestMock} from 'chrome://webui-test/test_mock.js';
import {eventToPromise, microtasksFinished} from 'chrome://webui-test/test_util.js';

import {assertNotStyle, assertStyle, createBackgroundImage, createTheme, installMock} from './test_support.js';

suite('NewTabPageAppTest', () => {
  let app: AppElement;
  let customizeButtons: CustomizeButtonsElement;
  let windowProxy: TestMock<WindowProxy>;
  let handler: TestMock<PageHandlerRemote>;
  let callbackRouterRemote: PageRemote;
  let customizeButtonsHandler: TestMock<CustomizeButtonsHandlerRemote>;
  let customizeButtonsCallbackRouterRemote: CustomizeButtonsDocumentRemote;
  let metrics: MetricsTracker;
  let moduleRegistry: TestMock<ModuleRegistry>;
  let backgroundManager: TestMock<BackgroundManager>;
  let moduleResolver: PromiseResolver<Module[]>;

  const url: URL = new URL(location.href);
  const backgroundImageLoadTime: number = 123;

  setup(async () => {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    windowProxy = installMock(WindowProxy);
    handler = installMock(
        PageHandlerRemote,
        mock => NewTabPageProxy.setInstance(mock, new PageCallbackRouter()));
    customizeButtonsHandler = installMock(
        CustomizeButtonsHandlerRemote,
        mock => CustomizeButtonsProxy.setInstance(
            mock, new CustomizeButtonsDocumentCallbackRouter()));
    handler.setResultFor('getMostVisitedSettings', Promise.resolve({
      customLinksEnabled: false,
      shortcutsVisible: false,
    }));
    handler.setResultFor('getBackgroundCollections', Promise.resolve({
      collections: [],
    }));
    handler.setResultFor('getDoodle', Promise.resolve({
      doodle: null,
    }));
    handler.setResultFor('getModulesIdNames', Promise.resolve({data: []}));
    windowProxy.setResultMapperFor('matchMedia', () => ({
                                                   addListener() {},
                                                   addEventListener() {},
                                                   removeListener() {},
                                                   removeEventListener() {},
                                                 }));
    windowProxy.setResultFor('waitForLazyRender', Promise.resolve());
    windowProxy.setResultFor('createIframeSrc', '');
    windowProxy.setResultFor('url', url);
    callbackRouterRemote = NewTabPageProxy.getInstance()
                               .callbackRouter.$.bindNewPipeAndPassRemote();
    customizeButtonsCallbackRouterRemote =
        CustomizeButtonsProxy.getInstance()
            .callbackRouter.$.bindNewPipeAndPassRemote();
    backgroundManager = installMock(BackgroundManager);
    backgroundManager.setResultFor(
        'getBackgroundImageLoadTime', Promise.resolve(backgroundImageLoadTime));
    moduleRegistry = installMock(ModuleRegistry);
    moduleResolver = new PromiseResolver();
    moduleRegistry.setResultFor('initializeModules', moduleResolver.promise);
    metrics = fakeMetricsPrivate();

    app = document.createElement('ntp-app');
    document.body.appendChild(app);
    await microtasksFinished();

    customizeButtons = app.$.customizeButtons;
  });

  function getCustomizeButton(): CrButtonElement {
    return $$(customizeButtons, '#customizeButton')!;
  }

  function getWallpaperSearchButton(): CrButtonElement {
    return $$(customizeButtons, '#wallpaperSearchButton')!;
  }

  function getComposeButton(): HTMLElement|null {
    const searchboxContainer = app.shadowRoot.querySelector('cr-searchbox');
    assertTrue(!!searchboxContainer);
    return searchboxContainer.shadowRoot!.querySelector<HTMLElement>(
        '#composeButton');
  }

  suite('Misc', () => {
    test('logs height', () => {
      // Assert.
      assertEquals(1, metrics.count('NewTabPage.Height'));
      assertEquals(
          1,
          metrics.count('NewTabPage.Height', Math.floor(window.innerHeight)));
    });

    test('logs width', () => {
      // Assert.
      assertEquals(1, metrics.count('NewTabPage.Width'));
      assertEquals(
          1, metrics.count('NewTabPage.Width', Math.floor(window.innerWidth)));
    });

    test('open voice search event opens voice search overlay', async () => {
      // Act.
      $$(app, '#searchbox')!.dispatchEvent(new Event('open-voice-search'));
      await microtasksFinished();

      // Assert.
      assertTrue(!!app.shadowRoot.querySelector('ntp-voice-search-overlay'));
      assertEquals(1, metrics.count('NewTabPage.VoiceActions'));
      assertEquals(
          1,
          metrics.count(
              'NewTabPage.VoiceActions', VoiceAction.ACTIVATE_SEARCH_BOX));
    });

    test('voice search keyboard shortcut', async () => {
      // Test correct shortcut opens voice search.
      // Act.
      window.dispatchEvent(new KeyboardEvent('keydown', {
        ctrlKey: true,
        shiftKey: true,
        code: 'Period',
      }));
      await microtasksFinished();

      // Assert.
      assertTrue(!!app.shadowRoot.querySelector('ntp-voice-search-overlay'));
      assertEquals(1, metrics.count('NewTabPage.VoiceActions'));
      assertEquals(
          1,
          metrics.count(
              'NewTabPage.VoiceActions', VoiceAction.ACTIVATE_KEYBOARD));

      // Test other shortcut doesn't close voice search.
      // Act
      window.dispatchEvent(new KeyboardEvent('keydown', {
        ctrlKey: true,
        shiftKey: true,
        code: 'Enter',
      }));
      await microtasksFinished();

      // Assert.
      assertTrue(!!app.shadowRoot.querySelector('ntp-voice-search-overlay'));
    });

    if (isMac) {
      test('keyboard shortcut opens voice search overlay on mac', async () => {
        // Act.
        window.dispatchEvent(new KeyboardEvent('keydown', {
          metaKey: true,
          shiftKey: true,
          code: 'Period',
        }));
        await microtasksFinished();

        // Assert.
        assertTrue(!!app.shadowRoot.querySelector('ntp-voice-search-overlay'));
      });
    }

    test('help bubble can correctly find anchor elements', () => {
      assertDeepEquals(
          app.getSortedAnchorStatusesForTesting(),
          [
            [CUSTOMIZE_CHROME_BUTTON_ELEMENT_ID, true],
          ],
      );
    });

    test('Webstore toast works correctly', async () => {
      const webstoreToast = $$<CrToastElement>(app, '#webstoreToast')!;
      assertTrue(webstoreToast.hidden);

      // Try to show webstore toast without opening side panel.
      callbackRouterRemote.showWebstoreToast();
      await callbackRouterRemote.$.flushForTesting();

      // The webstore toast should still be hidden.
      assertTrue(webstoreToast.hidden);
      assertFalse(webstoreToast.open);

      // Open the side panel.
      customizeButtonsCallbackRouterRemote
          .setCustomizeChromeSidePanelVisibility(true);
      await customizeButtonsCallbackRouterRemote.$.flushForTesting();

      // Try to show webstore toast again.
      callbackRouterRemote.showWebstoreToast();
      await callbackRouterRemote.$.flushForTesting();

      // The webstore toast should be open.
      assertFalse(webstoreToast.hidden);
      assertTrue(webstoreToast.open);
      assertTrue(!!webstoreToast.firstChild!.textContent);
    });
  });

  suite(`OgbThemingRemoveScrim`, () => {
    test('Ogb updates on ntp load', async () => {
      // Act.

      // Create a dark mode theme with a custom background.
      const theme = createTheme({isDark: true});
      theme.backgroundImage = createBackgroundImage('https://foo.com');
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();

      // Notify the NTP that the ogb has loaded.
      window.dispatchEvent(new MessageEvent('message', {
        data: {
          frameType: 'one-google-bar',
          messageType: 'loaded',
        },
        source: window,
        origin: window.origin,
      }));
      await microtasksFinished();

      // Assert.

      // Dark mode themes with background images and removeScrim set should
      // apply background protection to the ogb.
      assertEquals(1, windowProxy.getCallCount('postMessage'));
      const [_, {type, applyLightTheme}] =
          windowProxy.getArgs('postMessage')[0];
      assertEquals('updateAppearance', type);
      assertEquals(true, applyLightTheme);
      assertNotStyle($$(app, '#oneGoogleBarScrim')!, 'display', 'none');
    });
  });

  suite('OgbScrim', () => {
    test('scroll bounce', async () => {
      // Arrange.

      // Set theme that triggers the scrim.
      const theme = createTheme({isDark: true});
      theme.backgroundImage = createBackgroundImage('https://foo.com');
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();

      // Make sure page is scrollable.
      const spacer = document.createElement('div');
      spacer.style.width = '100%';
      spacer.style.height = '10000px';
      spacer.style.flexShrink = '0';
      $$(app, '#content')!.append(spacer);

      // Simulates a vertical scroll.
      const scrollY = async (y: number) => {
        window.scroll(0, y);
        // `window.scroll` doesn't automatically trigger scroll event.
        window.dispatchEvent(new Event('scroll'));
        // Wait for position update to propagate.
        await new Promise<void>(
            resolve => requestAnimationFrame(() => resolve()));
      };

      // Act (no bounce).
      await scrollY(0);

      // Assert (no bounce).
      assertStyle($$(app, '#oneGoogleBarScrim')!, 'position', 'fixed');

      // Act (scroll).
      await scrollY(10);

      // Assert (scroll).
      assertStyle($$(app, '#oneGoogleBarScrim')!, 'position', 'absolute');
    });
  });

  suite('Theming', () => {
    test('setting theme updates ntp', async () => {
      // Act.
      callbackRouterRemote.setTheme(createTheme());
      await callbackRouterRemote.$.flushForTesting();
      await microtasksFinished();

      // Assert.
      assertEquals(1, backgroundManager.getCallCount('setBackgroundColor'));
      assertEquals(
          0xffff0000 /* red */,
          (await backgroundManager.whenCalled('setBackgroundColor')).value);
      assertStyle(
          $$(app, '#content')!, '--color-new-tab-page-attribution-foreground',
          'rgba(0, 0, 255, 1.00)');
      assertEquals(1, backgroundManager.getCallCount('setShowBackgroundImage'));
      assertFalse(await backgroundManager.whenCalled('setShowBackgroundImage'));
      assertStyle($$(app, '#backgroundImageAttribution')!, 'display', 'none');
      assertStyle($$(app, '#backgroundImageAttribution2')!, 'display', 'none');
      assertFalse(app.$.logo.singleColored);
      assertTrue(!!app.$.logo.theme);
      assertFalse(app.$.logo.theme?.isDark);
      assertEquals(0xffff0000, app.$.logo.theme?.backgroundColor?.value);
    });

    test('setting 3p theme shows attribution', async () => {
      // Arrange.
      const theme = createTheme();
      theme.backgroundImage = createBackgroundImage('https://foo.com');
      theme.backgroundImage.attributionUrl = {url: 'chrome://theme/foo'};

      // Act.
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();
      await microtasksFinished();

      assertNotStyle($$(app, '#themeAttribution')!, 'display', 'none');
      assertEquals(
          'chrome://theme/foo',
          $$<HTMLImageElement>(app, '#themeAttribution img')!.src);
    });

    test('setting background image shows image', async () => {
      // Arrange.
      const theme = createTheme();
      theme.backgroundImage = createBackgroundImage('https://img.png');

      // Act.
      backgroundManager.resetResolver('setShowBackgroundImage');
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();

      // Assert.
      assertEquals(1, backgroundManager.getCallCount('setShowBackgroundImage'));
      assertTrue(await backgroundManager.whenCalled('setShowBackgroundImage'));

      // Scrim removal will remove text shadows as background protection is
      // applied to the background element instead.
      assertNotStyle(
          $$(app, '#backgroundImageAttribution')!, 'background-color',
          'rgba(0, 0, 0, 0)');
      assertStyle(
          $$(app, '#backgroundImageAttribution')!, 'text-shadow', 'none');

      assertEquals(1, backgroundManager.getCallCount('setBackgroundImage'));
      assertEquals(
          'https://img.png',
          (await backgroundManager.whenCalled('setBackgroundImage')).url.url);
      assertTrue(!!app.$.logo.theme?.backgroundColor);
    });

    test('setting attributions shows attributions', async function() {
      // Arrange.
      const theme = createTheme();
      theme.backgroundImageAttribution1 = 'foo';
      theme.backgroundImageAttribution2 = 'bar';
      theme.backgroundImageAttributionUrl = {url: 'https://info.com'};

      // Act.
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();

      // Assert.
      assertNotStyle(
          $$(app, '#backgroundImageAttribution')!, 'display', 'none');
      assertNotStyle(
          $$(app, '#backgroundImageAttribution2')!, 'display', 'none');
      assertEquals(
          'https://info.com',
          $$(app, '#backgroundImageAttribution')!.getAttribute('href'));
      assertEquals(
          'foo', $$(app, '#backgroundImageAttribution1')!.textContent!.trim());
      assertEquals(
          'bar', $$(app, '#backgroundImageAttribution2')!.textContent!.trim());
    });

    test('setting logo color colors logo', async function() {
      // Arrange.
      const theme = createTheme();
      theme.logoColor = {value: 0xffff0000};

      // Act.
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();

      // Assert.
      assertTrue(app.$.logo.singleColored);
      assertStyle(app.$.logo, '--ntp-logo-color', 'rgba(255, 0, 0, 1.00)');
    });

    test('theme updates add shortcut color', async () => {
      const theme = createTheme();
      theme.mostVisited.useWhiteTileIcon = true;
      callbackRouterRemote.setTheme(theme);
      const mostVisited = $$(app, '#mostVisited');
      assertTrue(!!mostVisited);
      assertFalse(mostVisited.hasAttribute('use-white-tile-icon_'));
      await callbackRouterRemote.$.flushForTesting();
      assertTrue(mostVisited.hasAttribute('use-white-tile-icon_'));
    });

    test('theme updates is dark', async () => {
      const theme = createTheme();
      theme.mostVisited.isDark = true;
      callbackRouterRemote.setTheme(theme);
      const mostVisited = $$(app, '#mostVisited');
      assertTrue(!!mostVisited);
      assertFalse(mostVisited.hasAttribute('is-dark_'));
      await callbackRouterRemote.$.flushForTesting();
      assertTrue(mostVisited.hasAttribute('is-dark_'));
    });

    [true, false].forEach((isDark) => {
      test(
          `OGB light mode whenever background image
          (ignoring dark mode) isDark: ${isDark}`,
          async () => {
            // Act.

            // Create a theme with a custom background.
            const theme = createTheme({isDark: isDark});
            theme.backgroundImage = createBackgroundImage('https://foo.com');
            callbackRouterRemote.setTheme(theme);
            await callbackRouterRemote.$.flushForTesting();

            // Notify the NTP that the ogb has loaded.
            window.dispatchEvent(new MessageEvent('message', {
              data: {
                frameType: 'one-google-bar',
                messageType: 'loaded',
              },
              source: window,
              origin: window.origin,
            }));
            await microtasksFinished();

            // Assert.
            assertEquals(1, windowProxy.getCallCount('postMessage'));
            const [_, {type, applyLightTheme}] =
                windowProxy.getArgs('postMessage')[0];
            assertEquals('updateAppearance', type);
            assertEquals(true, applyLightTheme);
          });
    });

    suite('theming metrics', () => {
      test('having no theme produces correct metrics', async () => {
        // Arrange.
        const theme = createTheme();
        theme.isCustomBackground = false;

        // Act.
        callbackRouterRemote.setTheme(theme);
        await callbackRouterRemote.$.flushForTesting();

        // Assert.
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad'));
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad', ''));
        assertEquals(1, metrics.count('NewTabPage.BackgroundImageSource'));
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.BackgroundImageSource',
                NtpBackgroundImageSource.kNoImage));
      });

      test('having first party theme produces correct metrics', async () => {
        // Arrange.
        const theme = createTheme();
        theme.backgroundImage = createBackgroundImage('https://foo.com');
        theme.backgroundImage.imageSource =
            NtpBackgroundImageSource.kFirstPartyThemeWithoutDailyRefresh;
        theme.backgroundImageCollectionId = 'foo_collection';

        // Act.
        callbackRouterRemote.setTheme(theme);
        await callbackRouterRemote.$.flushForTesting();

        // Assert.
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad'));
        assertEquals(
            1,
            metrics.count('NewTabPage.Collections.IdOnLoad', 'foo_collection'));
        assertEquals(1, metrics.count('NewTabPage.BackgroundImageSource'));
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.BackgroundImageSource',
                NtpBackgroundImageSource.kFirstPartyThemeWithoutDailyRefresh));
      });

      test('having third party theme produces correct metrics', async () => {
        // Arrange.
        const theme = createTheme();
        theme.backgroundImage = createBackgroundImage('https://foo.com');
        theme.backgroundImage.imageSource =
            NtpBackgroundImageSource.kThirdPartyTheme;

        // Act.
        callbackRouterRemote.setTheme(theme);
        await callbackRouterRemote.$.flushForTesting();

        // Assert.
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad'));
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad', ''));
        assertEquals(1, metrics.count('NewTabPage.BackgroundImageSource'));
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.BackgroundImageSource',
                NtpBackgroundImageSource.kThirdPartyTheme));
      });

      test(
          'having refresh daily enabled produces correct metrics', async () => {
            // Arrange.
            const theme = createTheme();
            theme.backgroundImage = createBackgroundImage('https://foo.com');
            theme.backgroundImage.imageSource =
                NtpBackgroundImageSource.kFirstPartyThemeWithDailyRefresh;
            theme.backgroundImageCollectionId = 'foo_collection';

            // Act.
            callbackRouterRemote.setTheme(theme);
            await callbackRouterRemote.$.flushForTesting();

            // Assert.
            assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad'));
            assertEquals(
                1,
                metrics.count(
                    'NewTabPage.Collections.IdOnLoad', 'foo_collection'));
            assertEquals(1, metrics.count('NewTabPage.BackgroundImageSource'));
            assertEquals(
                1,
                metrics.count(
                    'NewTabPage.BackgroundImageSource',
                    NtpBackgroundImageSource.kFirstPartyThemeWithDailyRefresh));
          });

      test('setting uploaded background produces correct metrics', async () => {
        // Arrange.
        const theme = createTheme();
        theme.backgroundImage = createBackgroundImage('https://foo.com');
        theme.backgroundImage.imageSource =
            NtpBackgroundImageSource.kUploadedImage;

        // Act.
        callbackRouterRemote.setTheme(theme);
        await callbackRouterRemote.$.flushForTesting();

        // Assert.
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad'));
        assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad', ''));
        assertEquals(1, metrics.count('NewTabPage.BackgroundImageSource'));
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.BackgroundImageSource',
                NtpBackgroundImageSource.kUploadedImage));
      });

      test(
          'setting wallpaper search background produces correct metrics',
          async () => {
            // Arrange.
            const theme = createTheme();
            theme.backgroundImage = createBackgroundImage('https://foo.com');
            theme.backgroundImage.imageSource =
                NtpBackgroundImageSource.kWallpaperSearch;

            // Act.
            callbackRouterRemote.setTheme(theme);
            await callbackRouterRemote.$.flushForTesting();

            // Assert.
            assertEquals(1, metrics.count('NewTabPage.Collections.IdOnLoad'));
            assertEquals(
                1, metrics.count('NewTabPage.Collections.IdOnLoad', ''));
            assertEquals(1, metrics.count('NewTabPage.BackgroundImageSource'));
            assertEquals(
                1,
                metrics.count(
                    'NewTabPage.BackgroundImageSource',
                    NtpBackgroundImageSource.kWallpaperSearch));
          });

      suite('background image load', () => {
        suiteSetup(() => {
          loadTimeData.overrideValues({backgroundImageUrl: 'https://foo.com'});
        });

        test('background image load time is logged', () => {
          // Assert.
          assertEquals(
              1, metrics.count('NewTabPage.Images.ShownTime.BackgroundImage'));
          assertEquals(
              1,
              metrics.count(
                  'NewTabPage.Images.ShownTime.BackgroundImage',
                  Math.floor(
                      backgroundImageLoadTime -
                      window.performance.timeOrigin)));
        });
      });
    });
  });

  suite('Promo', () => {
    test('can show promo with browser command', async () => {
      const promoBrowserCommandHandler = installMock(
          CommandHandlerRemote,
          mock => BrowserCommandProxy.getInstance().handler = mock);
      promoBrowserCommandHandler.setResultFor(
          'canExecuteCommand', Promise.resolve({canExecute: true}));

      const commandId = 123;  // Unsupported command.
      window.dispatchEvent(new MessageEvent('message', {
        data: {
          frameType: 'one-google-bar',
          messageType: 'can-show-promo-with-browser-command',
          commandId,
        },
        source: window,
        origin: window.origin,
      }));

      // Make sure the command is sent to the browser.
      const expectedCommandId =
          await promoBrowserCommandHandler.whenCalled('canExecuteCommand');
      // Unsupported commands get resolved to the default command before being
      // sent to the browser.
      assertEquals(Command.kUnknownCommand, expectedCommandId);

      // Make sure the promo frame gets notified whether the promo can be shown.
      const {data} = await eventToPromise('message', window);
      assertEquals('can-show-promo-with-browser-command', data.messageType);
      assertTrue(data[commandId]);
    });

    test('executes promo browser command', async () => {
      const promoBrowserCommandHandler = installMock(
          CommandHandlerRemote,
          mock => BrowserCommandProxy.getInstance().handler = mock);
      promoBrowserCommandHandler.setResultFor(
          'executeCommand', Promise.resolve({commandExecuted: true}));

      const commandId = 123;  // Unsupported command.
      const clickInfo = {middleButton: true};
      window.dispatchEvent(new MessageEvent('message', {
        data: {
          frameType: 'one-google-bar',
          messageType: 'execute-browser-command',
          data: {
            commandId,
            clickInfo,
          },
        },
        source: window,
        origin: window.origin,
      }));

      // Make sure the command and click information are sent to the browser.
      const [expectedCommandId, expectedClickInfo] =
          await promoBrowserCommandHandler.whenCalled('executeCommand');
      // Unsupported commands get resolved to the default command before being
      // sent to the browser.
      assertEquals(Command.kUnknownCommand, expectedCommandId);
      assertEquals(clickInfo, expectedClickInfo);

      // Make sure the promo frame gets notified whether the command was
      // executed.
      const {data: commandExecuted} = await eventToPromise('message', window);
      assertTrue(commandExecuted);
    });
  });

  suite('Clicks', () => {
    suiteSetup(() => {
      loadTimeData.overrideValues({
        modulesEnabled: true,
      });
    });

    ([
      ['#content', NtpElement.BACKGROUND],
      ['ntp-logo', NtpElement.LOGO],
      ['cr-searchbox', NtpElement.REALBOX],
      ['cr-most-visited', NtpElement.MOST_VISITED],
      ['ntp-middle-slot-promo', NtpElement.MIDDLE_SLOT_PROMO],
      ['#modules', NtpElement.MODULE],
    ] as Array<[string, NtpElement]>)
        .forEach(([selector, element]) => {
          test(`clicking '${selector}' records click`, () => {
            // Act.
            $$<HTMLElement>(app, selector)!.click();

            // Assert.
            assertEquals(1, metrics.count('NewTabPage.Click'));
            assertEquals(1, metrics.count('NewTabPage.Click', element));
          });
        });

    test(`clicking #customizeButton records click`, () => {
      // Act.
      getCustomizeButton().click();

      // Assert.
      assertEquals(1, metrics.count('NewTabPage.Click'));
      assertEquals(
          1, metrics.count('NewTabPage.Click', NtpElement.CUSTOMIZE_BUTTON));
    });

    test('clicking OGB records click', () => {
      // Act.
      window.dispatchEvent(new MessageEvent('message', {
        data: {
          frameType: 'one-google-bar',
          messageType: 'click',
        },
      }));

      // Assert.
      assertEquals(1, metrics.count('NewTabPage.Click'));
      assertEquals(
          1, metrics.count('NewTabPage.Click', NtpElement.ONE_GOOGLE_BAR));
    });
  });

  function modulesCommonTests(modulesElementTag: string) {
    test('promo and modules coordinate', async () => {
      // Arrange.
      loadTimeData.overrideValues({navigationStartTime: 0.0});
      windowProxy.setResultFor('now', 123.0);
      const middleSlotPromo = $$(app, 'ntp-middle-slot-promo');
      assertTrue(!!middleSlotPromo);
      const modules = $$(app, modulesElementTag)!;
      assertTrue(!!modules);

      // Assert.
      assertStyle(middleSlotPromo, 'display', 'none');
      assertStyle(modules, 'display', 'none');

      // Act.
      middleSlotPromo.dispatchEvent(new Event('ntp-middle-slot-promo-loaded'));

      // Assert.
      assertStyle(middleSlotPromo, 'display', 'none');
      assertStyle(modules, 'display', 'none');

      // Act.
      modules.dispatchEvent(new Event('modules-loaded'));
      await microtasksFinished();

      // Assert.
      assertNotStyle(middleSlotPromo, 'display', 'none');
      assertNotStyle(modules, 'display', 'none');
      assertEquals(1, metrics.count('NewTabPage.Modules.ShownTime'));
      assertEquals(1, metrics.count('NewTabPage.Modules.ShownTime', 123));
    });
  }

  suite('V2Modules', () => {
    suiteSetup(() => {
      loadTimeData.overrideValues({
        modulesEnabled: true,
      });
    });

    test('container is hidden', () => {
      const modules = $$(app, 'ntp-modules')!;
      assertTrue(!!modules);
      assertStyle(modules, 'display', 'none');
    });

    test(`clicking records click`, () => {
      // Act.
      $$<HTMLElement>(app, 'ntp-modules')!.click();

      // Assert.
      assertEquals(1, metrics.count('NewTabPage.Click'));
      assertEquals(1, metrics.count('NewTabPage.Click', NtpElement.MODULE));
    });

    modulesCommonTests('ntp-modules');
  });

  suite('CounterfactualModules', () => {
    suiteSetup(() => {
      loadTimeData.overrideValues({
        modulesEnabled: false,
        modulesLoadEnabled: true,
      });
    });

    test('modules loaded but not rendered if counterfactual', async () => {
      // Act.
      const fooElement = document.createElement('div');
      const barElement = document.createElement('div');
      moduleResolver.resolve([
        {
          descriptor:
              new ModuleDescriptor('foo', () => Promise.resolve(fooElement)),
          elements: [fooElement],
        },
        {
          descriptor:
              new ModuleDescriptor('bar', () => Promise.resolve(barElement)),
          elements: [barElement],
        },
      ]);
      await counterfactualLoad();
      await microtasksFinished();

      // Assert.
      assertTrue(moduleRegistry.getCallCount('initializeModules') > 0);
      assertEquals(1, handler.getCallCount('onModulesLoadedWithData'));
      assertEquals(
          0, app.shadowRoot.querySelectorAll('ntp-module-wrapper').length);
    });
  });

  suite('CustomizeChromeSidePanel', () => {
    test('customize chrome in product help might show on startup'), () => {
      assertEquals(1, handler.getCallCount('maybeShowFeaturePromo'));
    };

    test('clicking customize button opens side panel', () => {
      // Act.
      getCustomizeButton().click();

      // Assert.
      assertDeepEquals(
          [
            true,
            CustomizeChromeSection.kUnspecified,
            SidePanelOpenTrigger.kNewTabPage,
          ],
          customizeButtonsHandler.getArgs(
              'setCustomizeChromeSidePanelVisible')[0]);
      assertEquals(
          1,
          metrics.count(
              'NewTabPage.CustomizeChromeOpened',
              NtpCustomizeChromeEntryPoint.CUSTOMIZE_BUTTON));
      assertEquals(
          1,
          customizeButtonsHandler.getCallCount(
              'incrementCustomizeChromeButtonOpenCount'));
    });

    test('clicking customize button hides side panel', async () => {
      // Act.
      customizeButtonsCallbackRouterRemote
          .setCustomizeChromeSidePanelVisibility(true);
      assertEquals(
          0,
          metrics.count(
              'NewTabPage.CustomizeChromeOpened',
              NtpCustomizeChromeEntryPoint.CUSTOMIZE_BUTTON));
      await customizeButtonsCallbackRouterRemote.$.flushForTesting();
      getCustomizeButton().click();

      // Assert.
      assertDeepEquals(
          [
            false,
            CustomizeChromeSection.kUnspecified,
            SidePanelOpenTrigger.kNewTabPage,
          ],
          customizeButtonsHandler.getArgs(
              'setCustomizeChromeSidePanelVisible')[0]);
      assertEquals(
          0,
          metrics.count(
              'NewTabPage.CustomizeChromeOpened',
              NtpCustomizeChromeEntryPoint.CUSTOMIZE_BUTTON));
      assertEquals(
          0,
          customizeButtonsHandler.getCallCount(
              'incrementCustomizeChromeButtonOpenCount'));
    });

    test('clicking customize button is accessible', async () => {
      customizeButtonsCallbackRouterRemote
          .setCustomizeChromeSidePanelVisibility(true);
      await customizeButtonsCallbackRouterRemote.$.flushForTesting();
      assertEquals('true', getCustomizeButton().getAttribute('aria-pressed'));
      customizeButtonsCallbackRouterRemote
          .setCustomizeChromeSidePanelVisibility(false);
      await customizeButtonsCallbackRouterRemote.$.flushForTesting();
      assertEquals('false', getCustomizeButton().getAttribute('aria-pressed'));
    });

    suite('modules', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          modulesEnabled: true,
        });
      });

      test('modules can open side panel', () => {
        // Act.
        $$(app, '#modules')!.dispatchEvent(new Event('customize-module'));

        // Assert.
        assertDeepEquals(
            [
              true,
              CustomizeChromeSection.kModules,
              SidePanelOpenTrigger.kNewTabPage,
            ],
            customizeButtonsHandler.getArgs(
                'setCustomizeChromeSidePanelVisible')[0]);
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.CustomizeChromeOpened',
                NtpCustomizeChromeEntryPoint.MODULE));
      });
    });

    suite('customize URL', () => {
      suiteSetup(() => {
        // We inject the URL param in this suite setup so that the URL is
        // updated before the app element gets created.
        url.searchParams.append('customize', CustomizeDialogPage.THEMES);
      });

      test('URL opens side panel', () => {
        // Assert.
        assertDeepEquals(
            [
              true,
              CustomizeChromeSection.kAppearance,
              SidePanelOpenTrigger.kNewTabPage,
            ],
            customizeButtonsHandler.getArgs(
                'setCustomizeChromeSidePanelVisible')[0]);
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.CustomizeChromeOpened',
                NtpCustomizeChromeEntryPoint.URL));
      });
    });
  });

  suite('LensUploadDialog', () => {
    suiteSetup(() => {
      loadTimeData.overrideValues({
        searchboxLensSearch: true,
      });
    });

    test('lens upload dialog closed on start', () => {
      // Assert.
      assertFalse(!!app.shadowRoot.querySelector('ntp-lens-upload-dialog'));
    });

    test('realbox is not visible when Lens upload dialog is open', async () => {
      // Arrange.
      callbackRouterRemote.setTheme(createTheme());
      await callbackRouterRemote.$.flushForTesting();

      // Act.
      $$(app, '#searchbox')!.dispatchEvent(new Event('open-lens-search'));
      await microtasksFinished();

      // Assert.
      const dialog = app.shadowRoot.querySelector('ntp-lens-upload-dialog');
      assertTrue(!!dialog);
      assertStyle($$(app, '#searchbox')!, 'visibility', 'hidden');

      // Act.
      dialog.closeDialog();
      await microtasksFinished();

      // Assert.
      assertStyle($$(app, '#searchbox')!, 'visibility', 'visible');
    });
  });

  suite('ComposeEntryPoint', () => {
    const DEFAULT_COMPOSE_CLICK_EVENT_OPTIONS = {
      detail: {
        button: 0,
        ctrlKey: false,
        metaKey: false,
        shiftKey: false,
      },
      bubbles: true,
      composed: true,
    };
    suite('compose feature disabled', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          searchboxShowComposeEntrypoint: false,
          searchboxShowComposebox: false,
        });
      });

      test('compose entrypoint not shown', () => {
        // Assert entrypoint is not shown.
        assertFalse(!!getComposeButton());

        // Assert shown histogram not logged.
        assertEquals(0, metrics.count('NewTabPage.ComposeEntrypoint.Shown'));
        // Assert compose button shown count is not incremented.
        assertEquals(
            0, handler.getCallCount('incrementComposeButtonShownCount'));
      });
    });

    suite('compose features enabled', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          searchboxShowComposeEntrypoint: true,
          searchboxShowComposebox: true,
        });
      });

      test('compose entrypoint shows', () => {
        // Assert shown histogram logged.
        assertEquals(1, metrics.count('NewTabPage.ComposeEntrypoint.Shown'));
        // Assert entrypoint is shown.
        assertTrue(!!getComposeButton());
      });
    });

    suite('compose entrypoint enabled - composebox disabled', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          searchboxShowComposeEntrypoint: true,
          searchboxShowComposebox: false,
        });
        // Needed so `.click()` calls don't navigate.
        window.open = () => null;
      });

      test('compose entry point emits histograms when shown', () => {
        // Assert shown histogram logged.
        assertEquals(1, metrics.count('NewTabPage.ComposeEntrypoint.Shown'));

        // Assert button is present.
        assertTrue(!!getComposeButton());

        // Assert increment compose button shown count is called on load.
        assertEquals(
            1, handler.getCallCount('incrementComposeButtonShownCount'));
      });
      test('compose entry point emits histograms when clicked', async () => {
        // Assert compose button is present.
        const composeButton = getComposeButton();
        assertTrue(!!composeButton);

        // Dispatch the 'compose-click' event directly, which cr-searchbox
        // listens for. This simulates the `cr-searchbox-compose-button`
        // child `cr-button` being clicked and its `onClick_` function being
        // called.
        composeButton.dispatchEvent(new CustomEvent(
            'compose-click', DEFAULT_COMPOSE_CLICK_EVENT_OPTIONS));

        await microtasksFinished();

        // Metric should be recorded without user text present.
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.ComposeEntrypoint.Click.UserTextPresent'));
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.ComposeEntrypoint.Click.UserTextPresent', false));
      });
      test(
          'compose entry point emits histograms when clicked with text present',
          () => {
            // Assert compose button is present.
            const searchboxContainer =
                app.shadowRoot.querySelector('cr-searchbox');
            const composeButton = getComposeButton();
            assertTrue(!!composeButton);

            searchboxContainer!.shadowRoot!
                .querySelector<HTMLInputElement>('#input')!.value = 'hello';

            // Dispatch the 'compose-click' event directly, which cr-searchbox
            // listens for. This simulates the `cr-searchbox-compose-button`
            // child `cr-button` being clicked and its `onClick_` function being
            // called.
            composeButton.dispatchEvent(new CustomEvent(
                'compose-click', DEFAULT_COMPOSE_CLICK_EVENT_OPTIONS));

            // Metric should be recorded with user text present.
            assertEquals(
                1,
                metrics.count(
                    'NewTabPage.ComposeEntrypoint.Click.UserTextPresent'));
            assertEquals(
                1,
                metrics.count(
                    'NewTabPage.ComposeEntrypoint.Click.UserTextPresent',
                    true));
          });
    });
  });

  suite('Composebox', () => {
    let composeboxHandler: TestMock<ComposeboxPageHandlerRemote>;
    const DEFAULT_COMPOSE_CLICK_EVENT_OPTIONS = {
      detail: {
        button: 0,
        ctrlKey: false,
        metaKey: false,
        shiftKey: false,
      },
      bubbles: true,
      composed: true,
    };
    suiteSetup(() => {
      loadTimeData.overrideValues({
        searchboxShowComposeEntrypoint: true,
        searchboxShowComposebox: true,
      });
      composeboxHandler = installMock(
          ComposeboxPageHandlerRemote,
          mock =>
              ComposeboxProxyImpl.setInstance(new ComposeboxProxyImpl(mock)));
      // Needed so `.click()` calls don't navigate.
      window.open = () => null;
    });
    test('toggle composebox visibility', async () => {
      // Arrange.
      callbackRouterRemote.setTheme(createTheme());
      await callbackRouterRemote.$.flushForTesting();

      // Act.
      $$(app, '#searchbox')!.dispatchEvent(new Event('open-composebox'));
      await microtasksFinished();

      // Assert.
      const composebox = app.shadowRoot.querySelector('ntp-composebox');
      assertTrue(!!composebox);
      assertStyle($$(app, '#searchbox')!, 'visibility', 'hidden');
    });
    test(
        'Clicking the searchbox composebox button notifies composebox handler',
        async () => {
          composeboxHandler.reset();
          assertEquals(
              composeboxHandler.getCallCount('notifySessionStarted'), 0);
          assertEquals(
              0,
              metrics.count('NewTabPage.Composebox.FromNTPLoadToSessionStart'));


          const composeButton = getComposeButton();
          assertTrue(!!composeButton);

          // Simulate entry point click.
          composeButton.dispatchEvent(new CustomEvent(
              'compose-click', DEFAULT_COMPOSE_CLICK_EVENT_OPTIONS));
          await microtasksFinished();

          // Assert.
          const composebox = app.shadowRoot.querySelector('ntp-composebox');
          assertTrue(!!composebox);
          assertEquals(
              composeboxHandler.getCallCount('notifySessionStarted'), 1);
          assertEquals(
              1,
              metrics.count('NewTabPage.Composebox.FromNTPLoadToSessionStart'));
        });
    test(
        'Clicking the searchbox composebox button with text navigates',
        async () => {
          composeboxHandler.reset();
          assertEquals(
              composeboxHandler.getCallCount('notifySessionStarted'), 0);

          const searchboxContainer =
              app.shadowRoot.querySelector('cr-searchbox');
          const composeButton = getComposeButton();
          assertTrue(!!composeButton);

          searchboxContainer!.shadowRoot!
              .querySelector<HTMLInputElement>('#input')!.value = 'hello';

          // Simulate entry point click with text present.
          composeButton.dispatchEvent(new CustomEvent(
              'compose-click', DEFAULT_COMPOSE_CLICK_EVENT_OPTIONS));

          await microtasksFinished();

          // Assert that this click causes navigation and does not start a
          // composebox session..
          const composebox = app.shadowRoot.querySelector('ntp-composebox');
          assertFalse(!!composebox);
          assertEquals(
              composeboxHandler.getCallCount('notifySessionStarted'), 0);
          assertEquals(
              1,
              metrics.count(
                  'NewTabPage.ComposeEntrypoint.Click.UserTextPresent', true));
        });

    test(
        'Clicking the scrim notifies handler of abandoned session',
        async () => {
          // Arrange.
          composeboxHandler.reset();
          assertEquals(
              composeboxHandler.getCallCount('notifySessionAbandoned'), 0);
          $$(app, '#searchbox')!.dispatchEvent(new Event('open-composebox'));
          await microtasksFinished();
          const composeboxScrim =
              app.shadowRoot.querySelector<HTMLElement>('#composeboxScrim');
          assertTrue(!!composeboxScrim);
          composeboxScrim.click();
          await microtasksFinished();

          // Assert.
          assertEquals(
              composeboxHandler.getCallCount('notifySessionAbandoned'), 1);
        });
  });

  suite('WallpaperSearch', () => {
    setup(async () => {
      // Set a theme with no background image and a baseline color to avoid
      // potential conflicts with the ToT value for
      // `wallpaperSearchHideCondition`.
      callbackRouterRemote.setTheme(createTheme({isBaseline: true}));
      await callbackRouterRemote.$.flushForTesting();
      await microtasksFinished();
    });

    suite('ButtonDisabled', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          wallpaperSearchButtonEnabled: false,
        });
      });

      test('does not increment button shown count on startup'), () => {
        assertEquals(
            0,
            customizeButtonsHandler.getCallCount(
                'incrementWallpaperSearchButtonShownCount'));
      };

      test('wallpaper search button is not shown if it is disabled', () => {
        assertTrue(!!getCustomizeButton());
        assertFalse(!!getWallpaperSearchButton());
      });

      test(
          'setting background image styles customize chrome button',
          async () => {
            // Customize chrome button is expanded and its icon has a
            // non-white color.
            assertNotEquals(32, getCustomizeButton().offsetWidth);
            assertNotStyle(
                getCustomizeButton().querySelector('.customize-text')!,
                'display', 'none');
            assertNotStyle(
                getCustomizeButton().querySelector('.customize-icon')!, 'fill',
                'rgb(255, 255, 255)');

            const theme = createTheme({isDark: true});
            theme.backgroundImage = createBackgroundImage('https://foo.com');
            callbackRouterRemote.setTheme(theme);
            await callbackRouterRemote.$.flushForTesting();

            // Customize chrome button is collapsed and its icon is white.
            assertEquals(32, getCustomizeButton().offsetWidth);
            assertStyle(
                getCustomizeButton().querySelector('.customize-icon')!, 'fill',
                'rgb(255, 255, 255)');
            assertStyle(
                getCustomizeButton().querySelector('.customize-text')!,
                'display', 'none');
          });
    });

    function assertButtonAnimated() {
      assertNotStyle(getWallpaperSearchButton(), 'animation-name', 'none');
      assertNotStyle(
          getWallpaperSearchButton().querySelector('.customize-icon')!,
          'animation-name', 'none');
      assertStyle(
          getWallpaperSearchButton().querySelector('.customize-text')!,
          'animation-name', 'none');
    }

    function assertButtonNotAnimated() {
      assertStyle(getWallpaperSearchButton(), 'animation-name', 'none');
      assertStyle(
          getWallpaperSearchButton().querySelector('.customize-icon')!,
          'animation-name', 'none');
      assertStyle(
          getWallpaperSearchButton().querySelector('.customize-text')!,
          'animation-name', 'none');
    }

    suite('ButtonEnabled', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          wallpaperSearchButtonEnabled: true,
          wallpaperSearchButtonAnimationEnabled: true,
        });
      });

      test('increments button shown count on startup'), () => {
        assertEquals(
            1,
            customizeButtonsHandler.getCallCount(
                'incrementWallpaperSearchButtonShownCount'));
      };

      test('wallpaper search button shows if it is enabled', () => {
        assertTrue(!!getCustomizeButton());
        assertTrue(!!getWallpaperSearchButton());
      });

      test('button has animation', () => {
        assertButtonAnimated();
      });

      test(`clicking #customizeButton records click`, () => {
        getCustomizeButton().click();
        assertEquals(1, metrics.count('NewTabPage.Click'));
        assertEquals(
            1, metrics.count('NewTabPage.Click', NtpElement.CUSTOMIZE_BUTTON));
      });

      test(`clicking #wallpaperSearchButton records click`, () => {
        getWallpaperSearchButton().click();
        assertEquals(1, metrics.count('NewTabPage.Click'));
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.Click', NtpElement.WALLPAPER_SEARCH_BUTTON));
      });

      test('clicking wallpaper search button opens side panel', () => {
        getWallpaperSearchButton().click();
        assertDeepEquals(
            [
              true,
              CustomizeChromeSection.kWallpaperSearch,
              SidePanelOpenTrigger.kNewTabPage,
            ],
            customizeButtonsHandler.getArgs(
                'setCustomizeChromeSidePanelVisible')[0]);
        assertEquals(
            1,
            metrics.count(
                'NewTabPage.CustomizeChromeOpened',
                NtpCustomizeChromeEntryPoint.WALLPAPER_SEARCH_BUTTON));
        assertEquals(
            1,
            customizeButtonsHandler.getCallCount(
                'incrementCustomizeChromeButtonOpenCount'));
      });

      test(
          'wallpaper search button can open wallpaper search ' +
              'and hide side panel',
          async () => {
            // Open side panel to non-wallpaper search page.
            customizeButtonsCallbackRouterRemote
                .setCustomizeChromeSidePanelVisibility(true);
            assertEquals(
                0,
                metrics.count(
                    'NewTabPage.CustomizeChromeOpened',
                    NtpCustomizeChromeEntryPoint.WALLPAPER_SEARCH_BUTTON));
            await customizeButtonsCallbackRouterRemote.$.flushForTesting();

            // Clicking the wallpaper search button should navigate the side
            // panel to the wallpaper search page.
            getWallpaperSearchButton().click();
            assertDeepEquals(
                [
                  true,
                  CustomizeChromeSection.kWallpaperSearch,
                  SidePanelOpenTrigger.kNewTabPage,
                ],
                customizeButtonsHandler.getArgs(
                    'setCustomizeChromeSidePanelVisible')[0]);

            // Clicking the wallpaper search button, when the wallpaper search
            // page is opened, should close the side panel.
            getWallpaperSearchButton().click();
            assertDeepEquals(
                [
                  false,
                  CustomizeChromeSection.kUnspecified,
                  SidePanelOpenTrigger.kNewTabPage,
                ],
                customizeButtonsHandler.getArgs(
                    'setCustomizeChromeSidePanelVisible')[1]);
          });

      test('wallpaper search button is accessible', async () => {
        // Open side panel to non-wallpaper search page.
        customizeButtonsCallbackRouterRemote
            .setCustomizeChromeSidePanelVisibility(true);
        await customizeButtonsCallbackRouterRemote.$.flushForTesting();

        // Only customize chrome button should be labeled as pressed.
        assertEquals(
            'false', getWallpaperSearchButton().getAttribute('aria-pressed'));
        assertEquals('true', getCustomizeButton().getAttribute('aria-pressed'));
        // Open wallpaper search page.
        getWallpaperSearchButton().click();
        await microtasksFinished();

        // Both buttons should be labeled as pressed.
        assertEquals(
            'true', getWallpaperSearchButton().getAttribute('aria-pressed'));
        assertEquals('true', getCustomizeButton().getAttribute('aria-pressed'));
        // Close the side panel.
        customizeButtonsCallbackRouterRemote
            .setCustomizeChromeSidePanelVisibility(false);
        await customizeButtonsCallbackRouterRemote.$.flushForTesting();

        // Both buttons should not be labeled as pressed.
        assertEquals(
            'false', getWallpaperSearchButton().getAttribute('aria-pressed'));
        assertEquals(
            'false', getCustomizeButton().getAttribute('aria-pressed'));
      });

      test(
          'clicking wallpaper search button collapses/expands it', async () => {
            assertNotEquals(32, getWallpaperSearchButton().offsetWidth);
            assertNotStyle(
                getWallpaperSearchButton().querySelector('.customize-text')!,
                'display', 'none');
            getWallpaperSearchButton().click();
            await microtasksFinished();

            assertEquals(32, getWallpaperSearchButton().offsetWidth);
            assertStyle(
                getWallpaperSearchButton().querySelector('.customize-text')!,
                'display', 'none');
          });

      test('button hides in accordance with callback router', async () => {
        // Both buttons shown.
        assertNotStyle(getCustomizeButton(), 'display', 'none');
        assertNotStyle(getWallpaperSearchButton(), 'display', 'none');
        callbackRouterRemote.setWallpaperSearchButtonVisibility(false);
        await callbackRouterRemote.$.flushForTesting();
        await microtasksFinished();

        // Wallpaper search button hides.
        assertNotStyle(getCustomizeButton(), 'display', 'none');
        assertEquals(null, getWallpaperSearchButton());
        callbackRouterRemote.setWallpaperSearchButtonVisibility(true);
        await callbackRouterRemote.$.flushForTesting();
        await microtasksFinished();

        // Wallpaper search button remains hidden.
        assertNotStyle(getCustomizeButton(), 'display', 'none');
        assertEquals(null, getWallpaperSearchButton());
      });
    });

    suite('AnimationDisabled', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          wallpaperSearchButtonEnabled: true,
          wallpaperSearchButtonAnimationEnabled: false,
        });
      });

      test('button has no animation if the flag is disabled', () => {
        assertButtonNotAnimated();
      });
    });

    suite('UnconditionalVisibility', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          wallpaperSearchButtonEnabled: true,
          wallpaperSearchButtonHideCondition: /*NONE*/ 0,
          wallpaperSearchButtonAnimationEnabled: true,
        });
      });

      test('hide condition 0 shows button unconditonally', async () => {
        assertTrue(!!getCustomizeButton());
        assertTrue(!!getWallpaperSearchButton());
        const theme = createTheme({isBaseline: false});
        theme.backgroundImage = createBackgroundImage('https://foo.com');
        await callbackRouterRemote.$.flushForTesting();
        assertTrue(!!getCustomizeButton());
        assertTrue(!!getWallpaperSearchButton());
      });

      test(
          'setting background styles both customize chrome buttons',
          async () => {
            // The fill color of wallpaperSearchButton's icon is explicitly
            // hardcoded to white (#fff) in the SVG <path>, which takes higher
            // percedence over any CSS fill color.
            assertStyle(
                getWallpaperSearchButton()
                    .querySelector(
                        '.customize-icon')!.shadowRoot!.querySelector('path')!,
                'fill', 'rgb(255, 255, 255)');
            // customizeButton's icon should have a non-white color.
            assertNotStyle(
                getCustomizeButton().querySelector('.customize-icon')!, 'fill',
                'rgb(255, 255, 255)');
            // Only customize chrome button should be collapsed.
            assertNotStyle(
                getWallpaperSearchButton().querySelector('.customize-text')!,
                'display', 'none');
            assertStyle(
                getCustomizeButton().querySelector('.customize-text')!,
                'display', 'none');
            assertNotEquals(32, getWallpaperSearchButton().offsetWidth);
            assertEquals(32, getCustomizeButton().offsetWidth);

            // Create and set theme.
            const theme = createTheme({isDark: true});
            theme.backgroundImage = createBackgroundImage('https://foo.com');
            callbackRouterRemote.setTheme(theme);
            await callbackRouterRemote.$.flushForTesting();

            // The fill color of wallpaperSearchButton's icon is explicitly
            // hardcoded to white (#fff) in the SVG <path>, which takes higher
            // percedence over any CSS fill color.
            assertStyle(
                getWallpaperSearchButton()
                    .querySelector(
                        '.customize-icon')!.shadowRoot!.querySelector('path')!,
                'fill', 'rgb(255, 255, 255)');
            // customizeButton's icon should have a non-white color.
            assertStyle(
                getCustomizeButton().querySelector('.customize-icon')!, 'fill',
                'rgb(255, 255, 255)');
            // Only customize chrome button should be collapsed.
            assertNotStyle(
                getWallpaperSearchButton().querySelector('.customize-text')!,
                'display', 'none');
            assertStyle(
                getCustomizeButton().querySelector('.customize-text')!,
                'display', 'none');
            assertNotEquals(32, getWallpaperSearchButton().offsetWidth);
            assertEquals(32, getCustomizeButton().offsetWidth);
          });

      [NtpBackgroundImageSource.kWallpaperSearch,
       NtpBackgroundImageSource.kWallpaperSearchInspiration]
          .forEach((imageSource) => {
            test(
                `having wallpaper search theme ${
                    imageSource} disables animation`,
                async () => {
                  // Arrange.
                  const theme = createTheme();
                  theme.backgroundImage =
                      createBackgroundImage('https://foo.com');
                  theme.backgroundImage.imageSource = imageSource;
                  assertButtonAnimated();

                  // Act.
                  callbackRouterRemote.setTheme(theme);
                  await callbackRouterRemote.$.flushForTesting();

                  // Assert.
                  assertButtonNotAnimated();
                });
          });
    });

    suite('ConditionalVisibility', () => {
      suiteSetup(() => {
        loadTimeData.overrideValues({
          wallpaperSearchButtonEnabled: true,
        });
      });

      test('hideCondition 1 hides button if background is set', async () => {
        loadTimeData.overrideValues({
          wallpaperSearchButtonHideCondition: /*BACKGROUND_IMAGE_SET*/ 1,
        });
        assertTrue(!!getCustomizeButton());
        assertTrue(!!getWallpaperSearchButton());
        // Set theme with a background image and baseline color.
        const theme = createTheme({isBaseline: true});
        theme.backgroundImage = createBackgroundImage('https://img.png');
        callbackRouterRemote.setTheme(theme);
        await backgroundManager.whenCalled('setShowBackgroundImage');
        await microtasksFinished();
        assertTrue(!!getCustomizeButton());
        assertFalse(!!getWallpaperSearchButton());
      });

      test(
          'hide condition 2 hides button if background or' +
              ' non-baseline color is set',
          async () => {
            loadTimeData.overrideValues({
              wallpaperSearchButtonHideCondition: /*THEME_SET*/ 2,
            });
            assertTrue(!!getCustomizeButton());
            assertTrue(!!getWallpaperSearchButton());
            // Set theme with a non-baseline color that has no background image.
            callbackRouterRemote.setTheme(createTheme({isBaseline: false}));
            await callbackRouterRemote.$.flushForTesting();
            await microtasksFinished();
            assertTrue(!!getCustomizeButton());
            assertFalse(!!getWallpaperSearchButton());
            // Resurface button by setting a theme with a baseline color (and no
            // background image).
            callbackRouterRemote.setTheme(createTheme({isBaseline: true}));
            await callbackRouterRemote.$.flushForTesting();
            await microtasksFinished();
            assertTrue(!!getCustomizeButton());
            assertTrue(!!getWallpaperSearchButton());
            // Set theme with a background image and baseline color.
            const theme = createTheme({isBaseline: true});
            theme.backgroundImage = createBackgroundImage('https://img.png');
            callbackRouterRemote.setTheme(theme);
            await backgroundManager.whenCalled('setShowBackgroundImage');
            await microtasksFinished();
            assertTrue(!!getCustomizeButton());
            assertFalse(!!getWallpaperSearchButton());
          });
    });
  });

  suite('MicrosoftAuth', () => {
    [true, false].forEach(
        (microsoftModuleEnabled) =>
            suite(`microsoftModuleEnabled ${microsoftModuleEnabled}`, () => {
              suiteSetup(() => {
                loadTimeData.overrideValues({microsoftModuleEnabled});
              });

              test('Show iframe when appropriate', () => {
                const iframe =
                    $$<HTMLIFrameElement>(app, 'iframe#microsoftAuth');

                if (!microsoftModuleEnabled) {
                  assertFalse(!!iframe);
                  return;
                }

                assertTrue(!!iframe);
                assertEquals(
                    'chrome-untrusted://ntp-microsoft-auth/', iframe.src);
              });
            }));
  });

  suite('NewTabFooter', () => {
    test('hide/show customize chrome and attribution buttons', async () => {
      // Arrange.
      const theme = createTheme();
      theme.backgroundImageAttribution1 = 'foo';
      theme.backgroundImageAttribution2 = 'bar';
      theme.backgroundImageAttributionUrl = {url: 'https://info.com'};
      callbackRouterRemote.setTheme(theme);
      await callbackRouterRemote.$.flushForTesting();

      // Assert default state of the buttons.
      assertTrue(!!$$(app, '#customizeButtons'));
      assertTrue(!!$$(app, '#backgroundImageAttribution'));

      // Act.
      callbackRouterRemote.footerVisibilityUpdated(true);
      await callbackRouterRemote.$.flushForTesting();
      await microtasksFinished();

      // Assert.
      assertFalse(!!$$(app, '#customizeButtons'));
      assertFalse(!!$$(app, '#backgroundImageAttribution'));

      // Act.
      callbackRouterRemote.footerVisibilityUpdated(false);
      await callbackRouterRemote.$.flushForTesting();
      await microtasksFinished();

      // Assert.
      assertTrue(!!$$(app, '#customizeButtons'));
      assertTrue(!!$$(app, '#backgroundImageAttribution'));
    });
  });
});
