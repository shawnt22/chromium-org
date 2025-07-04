// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrAutoImgElement} from 'chrome://resources/cr_elements/cr_auto_img/cr_auto_img.js';
import type {CrToastElement} from 'chrome://resources/cr_elements/cr_toast/cr_toast.js';
import {assert} from 'chrome://resources/js/assert.js';
import {Command} from 'chrome://resources/js/browser_command.mojom-webui.js';
import {BrowserCommandProxy} from 'chrome://resources/js/browser_command/browser_command_proxy.js';
import {EventTracker} from 'chrome://resources/js/event_tracker.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from 'chrome://resources/lit/v3_0/lit.rollup.js';
import type {Url} from 'chrome://resources/mojo/url/mojom/url.mojom-webui.js';

import {getCss} from './middle_slot_promo.css.js';
import {getHtml} from './middle_slot_promo.html.js';
import {recordEnumeration} from './metrics_utils.js';
import type {PageHandlerRemote, Promo} from './new_tab_page.mojom-webui.js';
import {NewTabPageProxy} from './new_tab_page_proxy.js';
import {WindowProxy} from './window_proxy.js';

/**
 * List of possible Promo Dismiss actions. This enum must match with the
 * numbering for NtpPromoDismissAction in histogram/enums.xml. These values are
 * persisted to logs. Entries should not be renumbered, removed or reused.
 */
export enum PromoDismissAction {
  DISMISS = 0,
  RESTORE = 1,
}

export function recordPromoDismissAction(action: PromoDismissAction) {
  recordEnumeration(
      'NewTabPage.Promos.DismissAction', action,
      Object.keys(PromoDismissAction).length);
}

/**
 * If a promo exists with content and can be shown, an element containing
 * the rendered promo is returned with an id #promoContainer. Otherwise, null is
 * returned.
 */
export async function renderPromo(promo: Promo):
    Promise<{container: Element, id: string | null}|null> {
  const browserHandler = NewTabPageProxy.getInstance().handler;
  const promoBrowserCommandHandler = BrowserCommandProxy.getInstance().handler;
  if (!promo) {
    return null;
  }

  const commandIds: Command[] = [];

  function createAnchor(target: Url) {
    const commandIdMatch = /^command:(\d+)$/.exec(target.url);
    if (!commandIdMatch && !target.url.startsWith('https://')) {
      return null;
    }
    const el = document.createElement('a');
    let commandId: Command|null = null;
    if (!commandIdMatch) {
      el.href = target.url;
    } else {
      commandId = +commandIdMatch[1];
      // Make sure we don't send unsupported commands to the browser.
      if (!Object.values(Command).includes(commandId)) {
        commandId = Command.kUnknownCommand;
      }
      commandIds.push(commandId);
    }

    function onClick(event: MouseEvent) {
      if (commandId !== null) {
        promoBrowserCommandHandler.executeCommand(commandId, {
          middleButton: event.button === 1,
          altKey: event.altKey,
          ctrlKey: event.ctrlKey,
          metaKey: event.metaKey,
          shiftKey: event.shiftKey,
        });
      }
      browserHandler.onPromoLinkClicked();
    }

    // 'auxclick' handles the middle mouse button which does not trigger a
    // 'click' event.
    el.addEventListener('auxclick', onClick);
    el.addEventListener('click', onClick);
    return el;
  }

  let hasContent = false;
  const promoContainer = document.createElement('div');
  promoContainer.id = 'promoContainer';
  promo.middleSlotParts.forEach(({image, link, text}) => {
    let el;
    if (image) {
      el = new CrAutoImgElement();
      el.autoSrc = image.imageUrl.url;
      el.staticEncode = true;
      if (image.target) {
        const anchor = createAnchor(image.target);
        if (anchor) {
          anchor.appendChild(el);
          el = anchor;
        }
      }
      el.classList.add('image');
    } else if (link) {
      el = createAnchor(link.url);
    } else if (text) {
      el = document.createElement('span');
    }
    const linkOrText = link || text;
    if (el && linkOrText) {
      el.innerText = linkOrText.text;
    }
    if (el) {
      hasContent = true;
      promoContainer.appendChild(el);
    }
  });

  const canShow =
      (await Promise.all(commandIds.map(
           commandId =>
               promoBrowserCommandHandler.canExecuteCommand(commandId))))
          .every(({canExecute}) => canExecute);
  if (hasContent && canShow) {
    browserHandler.onPromoRendered(
        WindowProxy.getInstance().now(), promo.logUrl || null);
    return {container: promoContainer, id: promo.id};
  }
  return null;
}

export interface MiddleSlotPromoElement {
  $: {
    promoAndDismissContainer: HTMLElement,
    dismissPromoButtonToast: CrToastElement,
    dismissPromoButtonToastMessage: HTMLElement,
    undoDismissPromoButton: HTMLElement,
  };
}

// Element that requests and renders the middle-slot promo. The element is
// hidden until the promo is rendered, If no promo exists or the promo is empty,
// the element remains hidden.
export class MiddleSlotPromoElement extends CrLitElement {
  static get is() {
    return 'ntp-middle-slot-promo';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      shownMiddleSlotPromoId_: {
        type: String,
        reflect: true,
      },

      promo_: {type: Object},
    };
  }

  protected accessor shownMiddleSlotPromoId_: string;
  private accessor promo_: Promo;

  private blocklistedMiddleSlotPromoId_: string;
  private eventTracker_: EventTracker = new EventTracker();
  private pageHandler_: PageHandlerRemote;
  private setPromoListenerId_: number|null = null;

  constructor() {
    super();
    this.pageHandler_ = NewTabPageProxy.getInstance().handler;
  }

  override connectedCallback() {
    super.connectedCallback();
    this.setPromoListenerId_ =
        NewTabPageProxy.getInstance().callbackRouter.setPromo.addListener(
            (promo: Promo) => {
              this.promo_ = promo;
            });
    this.eventTracker_.add(window, 'keydown', this.onWindowKeydown_.bind(this));
    this.pageHandler_.updatePromoData();
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.eventTracker_.removeAll();
    NewTabPageProxy.getInstance().callbackRouter.removeListener(
        this.setPromoListenerId_!);
  }

  override updated(changedProperties: PropertyValues<this>) {
    super.updated(changedProperties);

    const changedPrivateProperties =
        changedProperties as Map<PropertyKey, unknown>;

    if (changedPrivateProperties.has('promo_')) {
      this.onPromoChange_();
    }
  }

  private onPromoChange_() {
    renderPromo(this.promo_).then(promo => {
      if (!promo) {
        this.$.promoAndDismissContainer.hidden = true;
      } else {
        const promoContainer = this.shadowRoot.getElementById('promoContainer');
        if (promoContainer) {
          promoContainer.remove();
        }
        if (loadTimeData.getBoolean('middleSlotPromoDismissalEnabled')) {
          this.shownMiddleSlotPromoId_ = promo.id ?? '';
        }
        const renderedPromoContainer = promo.container;
        assert(renderedPromoContainer);
        this.$.promoAndDismissContainer.prepend(renderedPromoContainer);
        this.$.promoAndDismissContainer.hidden = false;
      }
      this.fire('ntp-middle-slot-promo-loaded');
    });
  }

  // Allow users to undo the dismissal of the default promo using Ctrl+Z (or
  // Cmd+Z on macOS). Mobile promo dismissal is handled by `mobile_promo.ts`.
  private onWindowKeydown_(e: KeyboardEvent) {
    if (!this.blocklistedMiddleSlotPromoId_) {
      return;
    }
    let ctrlKeyPressed = e.ctrlKey;
    // <if expr="is_macosx">
    ctrlKeyPressed = ctrlKeyPressed || e.metaKey;
    // </if>
    if (ctrlKeyPressed && e.key === 'z') {
      this.onUndoDismissPromoButtonClick_();
    }
  }

  protected onDismissPromoButtonClick_() {
    assert(this.$.promoAndDismissContainer);
    this.$.promoAndDismissContainer.hidden = true;
    this.pageHandler_.blocklistPromo(this.shownMiddleSlotPromoId_);
    this.blocklistedMiddleSlotPromoId_ = this.shownMiddleSlotPromoId_;
    this.$.dismissPromoButtonToast.show();
    recordPromoDismissAction(PromoDismissAction.DISMISS);
  }

  protected onUndoDismissPromoButtonClick_() {
    assert(this.$.promoAndDismissContainer);
    this.pageHandler_.undoBlocklistPromo(this.blocklistedMiddleSlotPromoId_);
    this.$.promoAndDismissContainer.hidden = false;
    this.$.dismissPromoButtonToast.hide();
    recordPromoDismissAction(PromoDismissAction.RESTORE);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'ntp-middle-slot-promo': MiddleSlotPromoElement;
  }
}

customElements.define(MiddleSlotPromoElement.is, MiddleSlotPromoElement);
