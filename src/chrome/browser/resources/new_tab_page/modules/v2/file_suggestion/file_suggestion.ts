// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {File} from '../../../file_suggestion.mojom-webui.js';
import {RecommendationType} from '../../../file_suggestion.mojom-webui.js';
import {recordEnumeration, recordSmallCount} from '../../../metrics_utils.js';

import {getCss} from './file_suggestion.css.js';
import {getHtml} from './file_suggestion.html.js';

export interface FileSuggestionElement {
  $: {
    files: HTMLElement,
  };
}

/**
 * Shared component for file modules, which serve as an inside look to recent
 * activity within a user's Google Drive or Microsoft Sharepoint.
 */
export class FileSuggestionElement extends CrLitElement {
  static get is() {
    return 'ntp-file-suggestion';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      files: {type: Array},
      moduleName: {type: String},
    };
  }

  accessor files: File[] = [];
  accessor moduleName: string = '';

  protected onFileClick_(e: Event) {
    const clickFileEvent = new Event('usage', {composed: true, bubbles: true});
    this.dispatchEvent(clickFileEvent);
    const currentTarget = e.currentTarget as HTMLElement;
    const index = Number(currentTarget.dataset['index']);
    recordSmallCount(
        `NewTabPage.${this.moduleName}.FileClick`, index);
    const recommendationType = this.files[index].recommendationType;
    if (recommendationType != null) {
      recordEnumeration(
          `NewTabPage.${this.moduleName}.RecommendationTypeClick`,
          recommendationType, RecommendationType.MAX_VALUE + 1);
    }
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'ntp-file-suggestion': FileSuggestionElement;
  }
}

customElements.define(FileSuggestionElement.is, FileSuggestionElement);
