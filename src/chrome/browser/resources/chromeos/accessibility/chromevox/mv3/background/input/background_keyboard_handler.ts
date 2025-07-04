// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview ChromeVox keyboard handler.
 */
import {BridgeHelper} from '/common/bridge_helper.js';
import {KeyCode} from '/common/key_code.js';
import {TestImportManager} from '/common/testing/test_import_manager.js';

import {BridgeConstants} from '../../common/bridge_constants.js';
import {EventSourceType} from '../../common/event_source_type.js';
import type {InternalKeyEvent} from '../../common/internal_key_event.js'
import {ChromeVoxKbHandler} from '../../common/keyboard_handler.js';
import {Msgs} from '../../common/msgs.js';
import {QueueMode} from '../../common/tts_types.js';
import {ChromeVox} from '../chromevox.js';
import {ChromeVoxRange} from '../chromevox_range.js';
import {EventSource} from '../event_source.js';
import {ForcedActionPath} from '../forced_action_path.js';
import {MathHandler} from '../math_handler.js';
import {Output} from '../output/output.js';
import {ChromeVoxPrefs} from '../prefs.js';

const TARGET = BridgeConstants.BackgroundKeyboardHandler.TARGET;
const Action = BridgeConstants.BackgroundKeyboardHandler.Action;

/**
 * Internal pass through mode state (see usage below).
 */
enum KeyboardPassThroughState {
  // No pass through is in progress.
  NO_PASS_THROUGH = 'no_pass_through',

  // The pass through shortcut command has been pressed (keydowns), waiting for
  // user to release (keyups) all the shortcut keys.
  PENDING_PASS_THROUGH_SHORTCUT_KEYUPS = 'pending_pass_through_keyups',

  // The pass through shortcut command has been pressed and released, waiting
  // for the user to press/release a shortcut to be passed through.
  PENDING_SHORTCUT_KEYUPS = 'pending_shortcut_keyups',
}

export class BackgroundKeyboardHandler {
  static instance?: BackgroundKeyboardHandler;
  private static passThroughModeEnabled_: boolean = false;
  private eatenKeyDowns_: Set<number>;
  private passThroughState_: KeyboardPassThroughState;
  private passedThroughKeyDowns_: Set<number>;

  private constructor() {
    this.eatenKeyDowns_ = new Set();
    this.passThroughState_ = KeyboardPassThroughState.NO_PASS_THROUGH;
    this.passedThroughKeyDowns_ = new Set();

    BridgeHelper.registerHandler(
        TARGET, Action.ON_KEY_DOWN,
        (internalEvent: InternalKeyEvent) => this.onKeyDown_(internalEvent));
    BridgeHelper.registerHandler(
        TARGET, Action.ON_KEY_UP,
        (internalEvent: InternalKeyEvent) => this.onKeyUp_(internalEvent));

    chrome.accessibilityPrivate.setKeyboardListener(
        true, ChromeVoxPrefs.isStickyPrefOn);
  }

  static init(): void {
    if (BackgroundKeyboardHandler.instance) {
      throw 'Error: trying to create two instances of singleton BackgroundKeyboardHandler.';
    }
    BackgroundKeyboardHandler.instance = new BackgroundKeyboardHandler();
  }

  static enablePassThroughMode(): void {
    ChromeVox.tts.speak(Msgs.getMsg('pass_through_key'), QueueMode.QUEUE);
    BackgroundKeyboardHandler.passThroughModeEnabled_ = true;
  }

  /** Returns true if the key event should stop propagating. */
  private onKeyDown_(evt: InternalKeyEvent): boolean {
    EventSource.set(EventSourceType.STANDARD_KEYBOARD);
    evt.stickyMode = ChromeVoxPrefs.isStickyModeOn();

    // If somehow the user gets into a state where there are dangling key downs
    // don't get a key up, clear the eaten key downs. This is detected by a set
    // list of modifier flags.
    if (!evt.altKey && !evt.ctrlKey && !evt.metaKey && !evt.shiftKey) {
      this.eatenKeyDowns_.clear();
      this.passedThroughKeyDowns_.clear();
    }

    if (BackgroundKeyboardHandler.passThroughModeEnabled_) {
      this.passedThroughKeyDowns_.add(evt.keyCode);
      return false;
    }

    Output.forceModeForNextSpeechUtterance(QueueMode.FLUSH);

    // Try to restore to the last valid range.
    ChromeVoxRange.restoreLastValidRangeIfNeeded();

    let stopPropagation = false;
    if (!this.callOnKeyDownHandlers_(evt) ||
        this.shouldConsumeSearchKey_(evt)) {
      if (BackgroundKeyboardHandler.passThroughModeEnabled_) {
        this.passThroughState_ =
            KeyboardPassThroughState.PENDING_PASS_THROUGH_SHORTCUT_KEYUPS;
      }

      stopPropagation = true;
      this.eatenKeyDowns_.add(evt.keyCode);
    }
    return stopPropagation;
  }

  /** Returns true if the key should continue propagation. */
  private callOnKeyDownHandlers_(evt: InternalKeyEvent): boolean {
    // Defer first to the math handler, if it exists, then ordinary keyboard
    // commands.
    if (!MathHandler.onKeyDown(evt)) {
      return false;
    }

    const forcedActionPath = ForcedActionPath.instance;
    if (forcedActionPath && !forcedActionPath.onKeyDown(evt)) {
      return false;
    }

    return ChromeVoxKbHandler.basicKeyDownActionsListener(evt);
  }

  private shouldConsumeSearchKey_(evt: InternalKeyEvent): boolean {
    // We natively always capture Search, so we have to be very careful to
    // either eat it here or re-inject it; otherwise, some components, like
    // ARC++ with TalkBack never get it. We only want to re-inject when
    // ChromeVox has no range.
    if (!ChromeVoxRange.current) {
      return false;
    }

    // TODO(accessibility): address this awkward indexing once we convert
    // key_code.js to TS.
    return Boolean(evt.metaKey) || evt.keyCode === KeyCode['SEARCH'];
  }

  /** Returns true if the key event should stop propagating. */
  private onKeyUp_(evt: InternalKeyEvent): boolean {
    let stopPropagation = false;
    if (this.eatenKeyDowns_.has(evt.keyCode)) {
      stopPropagation = true;
      this.eatenKeyDowns_.delete(evt.keyCode);
    }

    if (BackgroundKeyboardHandler.passThroughModeEnabled_) {
      this.passedThroughKeyDowns_.delete(evt.keyCode);

      // Assuming we have no keys held (detected by held modifiers + keys we've
      // eaten in key down), we can start pass through for the next keys.
      if (this.passThroughState_ ===
              KeyboardPassThroughState.PENDING_PASS_THROUGH_SHORTCUT_KEYUPS &&
          !evt.altKey && !evt.ctrlKey && !evt.metaKey && !evt.shiftKey &&
          this.eatenKeyDowns_.size === 0) {
        // All keys of the pass through shortcut command have been released.
        // Ready to pass through the next shortcut.
        this.passThroughState_ =
            KeyboardPassThroughState.PENDING_SHORTCUT_KEYUPS;
      } else if (
          this.passThroughState_ ===
              KeyboardPassThroughState.PENDING_SHORTCUT_KEYUPS &&
          this.passedThroughKeyDowns_.size === 0) {
        // All keys of the passed through shortcut have been released. Ready to
        // go back to normal processing (aka no pass through).
        BackgroundKeyboardHandler.passThroughModeEnabled_ = false;
        this.passThroughState_ = KeyboardPassThroughState.NO_PASS_THROUGH;
      }
    }
    return stopPropagation;
  }
}

TestImportManager.exportForTesting(BackgroundKeyboardHandler);
