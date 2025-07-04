// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-menu' shows a menu with a hardcoded set of pages and subpages.
 */
import 'chrome://resources/cr_elements/cr_icons.css.js';
import 'chrome://resources/cr_elements/cr_menu_selector/cr_menu_selector.js';
import 'chrome://resources/cr_elements/cr_hidden_style.css.js';
import 'chrome://resources/cr_elements/cr_nav_menu_item_style.css.js';
import 'chrome://resources/cr_elements/cr_ripple/cr_ripple.js';
import 'chrome://resources/cr_elements/cr_shared_vars.css.js';
import 'chrome://resources/cr_elements/icons.html.js';
import 'chrome://resources/cr_elements/cr_icon/cr_icon.js';
import '../settings_vars.css.js';
import '../icons.html.js';
// <if expr="_google_chrome">
import '../internal/icons.html.js';

// </if>

import type {CrMenuSelector} from 'chrome://resources/cr_elements/cr_menu_selector/cr_menu_selector.js';
import {assert} from 'chrome://resources/js/assert.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {loadTimeData} from '../i18n_setup.js';
import type {MetricsBrowserProxy} from '../metrics_browser_proxy.js';
import {MetricsBrowserProxyImpl} from '../metrics_browser_proxy.js';
import {pageVisibility} from '../page_visibility.js';
import type {PageVisibility} from '../page_visibility.js';
import type {Route} from '../router.js';
import {RouteObserverMixin, Router} from '../router.js';

import {getTemplate} from './settings_menu.html.js';

export interface SettingsMenuElement {
  $: {
    autofill: HTMLLinkElement,
    menu: CrMenuSelector,
    people: HTMLLinkElement,
  };
}

const SettingsMenuElementBase = RouteObserverMixin(PolymerElement);

export class SettingsMenuElement extends SettingsMenuElementBase {
  static get is() {
    return 'settings-menu';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      /**
       * Dictionary defining page visibility.
       */
      pageVisibility_: {
        type: Object,
        value: () => pageVisibility,
      },

      showAiPage_: {
        type: Boolean,
        value: () => loadTimeData.getBoolean('showAiPage'),
      },
    };
  }

  declare private pageVisibility_?: PageVisibility;
  declare private showAiPage_: boolean;
  private metricsBrowserProxy_: MetricsBrowserProxy =
      MetricsBrowserProxyImpl.getInstance();

  private showAiPageMenuItem_(): boolean {
    return this.showAiPage_ &&
        (!this.pageVisibility_ || this.pageVisibility_.ai !== false);
  }

  override currentRouteChanged(newRoute: Route) {
    // Focus the initially selected path.
    const anchors = this.shadowRoot!.querySelectorAll('a');
    for (let i = 0; i < anchors.length; ++i) {
      // Purposefully grabbing the 'href' attribute and not the property.
      const pathname = anchors[i].getAttribute('href')!;
      const anchorRoute = Router.getInstance().getRouteForPath(pathname);
      if (anchorRoute && anchorRoute.contains(newRoute)) {
        this.setSelectedPath_(pathname);
        return;
      }
    }

    this.setSelectedPath_('');  // Nothing is selected.
  }

  focusFirstItem() {
    const firstFocusableItem = this.shadowRoot!.querySelector<HTMLElement>(
        '[role=menuitem]:not([hidden])');
    if (firstFocusableItem) {
      firstFocusableItem.focus();
    }
  }

  /**
   * Prevent clicks on sidebar items from navigating. These are only links for
   * accessibility purposes, taps are handled separately.
   */
  private onLinkClick_(event: Event) {
    if ((event.target as HTMLElement).matches('a:not(#extensionsLink)')) {
      event.preventDefault();
    }
  }

  /**
   * Keeps both menus in sync. `path` needs to come from
   * `element.getAttribute('href')`. Using `element.href` will not work as it
   * would pass the entire URL instead of just the path.
   */
  private setSelectedPath_(path: string) {
    this.$.menu.selected = path;
  }

  private onSelectorActivate_(event: CustomEvent<{selected: string}>) {
    const path = event.detail.selected;
    this.setSelectedPath_(path);

    const route = Router.getInstance().getRouteForPath(path);
    assert(route, 'settings-menu has an entry with an invalid route.');
    Router.getInstance().navigateTo(
        route, /* dynamicParams */ undefined, /* removeSearch */ true);
  }

  private onExtensionsLinkClick_() {
    chrome.metricsPrivate.recordUserAction(
        'SettingsMenu_ExtensionsLinkClicked');
  }

  private onAiPageClick_() {
    this.metricsBrowserProxy_.recordAction(
        'SettingsMenu_AiPageEntryPointClicked');
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-menu': SettingsMenuElement;
  }
}

customElements.define(SettingsMenuElement.is, SettingsMenuElement);
