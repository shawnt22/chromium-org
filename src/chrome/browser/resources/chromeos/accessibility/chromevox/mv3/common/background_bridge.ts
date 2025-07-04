// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Provides an interface for non-background contexts (options,
 * panel, etc.) to communicate with the background.
 */

import {BridgeHelper} from '/common/bridge_helper.js';
import type {constants} from '/common/constants.js';

import {BridgeConstants} from './bridge_constants.js';
import type {Command} from './command.js';
import type {EarconId} from './earcon_id.js';
import type {EventSourceType} from './event_source_type.js';
import type {InternalKeyEvent} from './internal_key_event.js';
import type {SerializableLog} from './log_types.js';
import type {QueueMode, TtsSpeechProperties} from './tts_types.js';

type ActionType = chrome.automation.ActionType;
type CustomAction = chrome.automation.CustomAction;
type EventType = chrome.automation.EventType;

interface AutomationActions {
  standardActions: ActionType[];
  customActions: CustomAction[];
}

interface ForcedAction {
  type: string;
  value: (string|Object);
  beforeActionMsg?: string;
  afterActionMsg?: string;
}

export const BackgroundBridge = {
  BackgroundKeyboardHandler: {
    onKeyDown(evt: InternalKeyEvent): Promise<boolean> {
      return BridgeHelper.sendMessage(
          BridgeConstants.BackgroundKeyboardHandler.TARGET,
          BridgeConstants.BackgroundKeyboardHandler.Action.ON_KEY_DOWN, evt);
    },

    onKeyUp(evt: InternalKeyEvent): Promise<boolean> {
      return BridgeHelper.sendMessage(
          BridgeConstants.BackgroundKeyboardHandler.TARGET,
          BridgeConstants.BackgroundKeyboardHandler.Action.ON_KEY_UP, evt);
    },
  },

  Braille: {
    /**
     * Translate braille cells into text.
     * @param cells Cells to be translated.
     */
    backTranslate(cells: ArrayBuffer): Promise<string|null> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Braille.TARGET,
          BridgeConstants.Braille.Action.BACK_TRANSLATE, cells);
    },

    panLeft(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Braille.TARGET,
          BridgeConstants.Braille.Action.PAN_LEFT);
    },

    panRight(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Braille.TARGET,
          BridgeConstants.Braille.Action.PAN_RIGHT);
    },

    /** Enables or disables processing of braille commands. */
    setBypass(bypassed: boolean): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Braille.TARGET,
          BridgeConstants.Braille.Action.SET_BYPASS, bypassed);
    },

    /** @param text The text to write in Braille. */
    write(text: string): Promise<boolean> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Braille.TARGET, BridgeConstants.Braille.Action.WRITE,
          text);
    },
  },

  BrailleBackground: {
    brailleRoute(displayPosition: number): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.BrailleBackground.TARGET,
          BridgeConstants.BrailleBackground.Action.BRAILLE_ROUTE,
          displayPosition);
    },
  },

  ChromeVoxPrefs: {
    /**
     * Get the prefs (not including keys).
     * @return A map of all prefs except the key map from LocalStorage.
     */
    getPrefs(): Promise<{[name: string]: string}> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ChromeVoxPrefs.TARGET,
          BridgeConstants.ChromeVoxPrefs.Action.GET_PREFS);
    },

    getStickyPref(): Promise<boolean> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ChromeVoxPrefs.TARGET,
          BridgeConstants.ChromeVoxPrefs.Action.GET_STICKY_PREF);
    },

    /**
     * Set the value of a pref of logging options.
     * @param key The pref key.
     * @param value The new value of the pref.
     */
    setLoggingPrefs(key: string, value: boolean): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ChromeVoxPrefs.TARGET,
          BridgeConstants.ChromeVoxPrefs.Action.SET_LOGGING_PREFS, key, value);
    },

    /**
     * Set the value of a pref.
     * @param key The pref key.
     * @param value The new value of the pref.
     */
    setPref(key: string, value: Object|string|boolean): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ChromeVoxPrefs.TARGET,
          BridgeConstants.ChromeVoxPrefs.Action.SET_PREF, key, value);
    },
  },

  ChromeVoxRange: {
    clearCurrentRange(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ChromeVoxRange.TARGET,
          BridgeConstants.ChromeVoxRange.Action.CLEAR_CURRENT_RANGE);
    },
  },

  CommandHandler: {
    /**
     * Handles ChromeVox commands.
     * @return True if the command should propagate.
     */
    onCommand(command: Command): Promise<boolean> {
      return BridgeHelper.sendMessage(
          BridgeConstants.CommandHandler.TARGET,
          BridgeConstants.CommandHandler.Action.ON_COMMAND, command);
    },
  },

  Earcons: {
    cancelEarcon(earconId: EarconId): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Earcons.TARGET,
          BridgeConstants.Earcons.Action.CANCEL_EARCON, earconId);
    },

    playEarcon(earconId: EarconId): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.Earcons.TARGET,
          BridgeConstants.Earcons.Action.PLAY_EARCON, earconId);
    },
  },

  EventSource: {
    /** Gets the current event source. */
    get(): Promise<EventSourceType> {
      return BridgeHelper.sendMessage(
          BridgeConstants.EventSource.TARGET,
          BridgeConstants.EventSource.Action.GET);
    },
  },

  GestureCommandHandler: {
    setBypass(bypassed: boolean): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.GestureCommandHandler.TARGET,
          BridgeConstants.GestureCommandHandler.Action.SET_BYPASS, bypassed);
    },
  },

  EventStreamLogger: {
    notifyEventStreamFilterChanged(name: EventType, enabled: boolean):
        Promise<void> {
          return BridgeHelper.sendMessage(
              BridgeConstants.EventStreamLogger.TARGET,
              BridgeConstants.EventStreamLogger.Action
                  .NOTIFY_EVENT_STREAM_FILTER_CHANGED,
              name, enabled);
        },
  },

  ForcedActionPath: {
    /**
     * Creates a new user action monitor.
     * Resolves after all actions in |actions| have been observed.
     */
    listenFor(actions: ForcedAction[]): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ForcedActionPath.TARGET,
          BridgeConstants.ForcedActionPath.Action.LISTEN_FOR, actions);
    },

    /** Destroys the user action monitor. */
    stopListening(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ForcedActionPath.TARGET,
          BridgeConstants.ForcedActionPath.Action.STOP_LISTENING);
    },

    onKeyDown(event: Object): Promise<boolean> {
      return BridgeHelper.sendMessage(
          BridgeConstants.ForcedActionPath.TARGET,
          BridgeConstants.ForcedActionPath.Action.ON_KEY_DOWN, event);
    },
  },

  LibLouis: {
    message(data: string): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.LibLouis.TARGET,
          BridgeConstants.LibLouis.Action.MESSAGE, data);
    },

    error(message: string): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.LibLouis.TARGET,
          BridgeConstants.LibLouis.Action.ERROR, message);
    },
  },

  LocaleOutputHelper: {
    onVoicesChanged(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.LocaleOutputHelper.TARGET,
          BridgeConstants.LocaleOutputHelper.Action.ON_VOICES_CHANGED);
    },
  },

  LogStore: {
    /** Clear the log buffer. */
    clearLog(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.LogStore.TARGET,
          BridgeConstants.LogStore.Action.CLEAR_LOG);
    },

    /**
     * Create logs in order.
     * This function is not currently optimized for speed.
     */
    getLogs(): Promise<SerializableLog[]> {
      return BridgeHelper.sendMessage(
          BridgeConstants.LogStore.TARGET,
          BridgeConstants.LogStore.Action.GET_LOGS);
    },
  },

  PanelBackground: {
    clearSavedNode(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.CLEAR_SAVED_NODE);
    },

    createAllNodeMenuBackgrounds(activatedMenuTitle?: string): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action
              .CREATE_ALL_NODE_MENU_BACKGROUNDS,
          activatedMenuTitle);
    },

    /**
     * Creates a new ISearch object, ready to search starting from the current
     * ChromeVox focus.
     */
    createNewISearch(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.CREATE_NEW_I_SEARCH);
    },

    /** Destroy the ISearch object so it can be garbage collected. */
    destroyISearch(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.DESTROY_I_SEARCH);
    },

    getActionsForCurrentNode(): Promise<AutomationActions> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.GET_ACTIONS_FOR_CURRENT_NODE);
    },

    incrementalSearch(
        searchStr: string, dir: constants.Dir, nextObject?: boolean):
        Promise<void> {
          return BridgeHelper.sendMessage(
              BridgeConstants.PanelBackground.TARGET,
              BridgeConstants.PanelBackground.Action.INCREMENTAL_SEARCH,
              searchStr, dir, nextObject);
        },

    onTutorialReady(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.ON_TUTORIAL_READY);
    },

    performCustomActionOnCurrentNode(actionId: number): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action
              .PERFORM_CUSTOM_ACTION_ON_CURRENT_NODE,
          actionId);
    },

    performStandardActionOnCurrentNode(action: ActionType): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action
              .PERFORM_STANDARD_ACTION_ON_CURRENT_NODE,
          action);
    },

    saveCurrentNode(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.SAVE_CURRENT_NODE);
    },

    /** Adds an event listener to detect panel collapse. */
    async setPanelCollapseWatcher(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.SET_PANEL_COLLAPSE_WATCHER);
    },

    /** Sets the current ChromeVox focus to the current ISearch node. */
    setRangeToISearchNode(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.SET_RANGE_TO_I_SEARCH_NODE);
    },

    /** Wait for the promise to notify panel collapse to resolved. */
    waitForPanelCollapse(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PanelBackground.TARGET,
          BridgeConstants.PanelBackground.Action.WAIT_FOR_PANEL_COLLAPSE);
    },
  },

  PrimaryTts: {
    onVoicesChanged(): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.PrimaryTts.TARGET,
          BridgeConstants.PrimaryTts.Action.ON_VOICES_CHANGED);
    },
  },

  TtsBackground: {
    /** Gets the voice currently used by ChromeVox when calling tts. */
    getCurrentVoice(): Promise<string> {
      return BridgeHelper.sendMessage(
          BridgeConstants.TtsBackground.TARGET,
          BridgeConstants.TtsBackground.Action.GET_CURRENT_VOICE);
    },

    /**
     * @param textString The string of text to be spoken.
     * @param queueMode The queue mode to use for speaking.
     * @param properties Speech properties to use for this utterance.
     */
    speak(
        textString: string, queueMode: QueueMode,
        properties?: TtsSpeechProperties): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.TtsBackground.TARGET,
          BridgeConstants.TtsBackground.Action.SPEAK, textString, queueMode,
          properties);
    },

    /**
     * Method that updates the punctuation echo level, and also persists
     * setting.
     * @param punctuationEcho The index of the desired punctuation echo level in
     *     PunctuationEchoes.
     */
    updatePunctuationEcho(punctuationEcho: number): Promise<void> {
      return BridgeHelper.sendMessage(
          BridgeConstants.TtsBackground.TARGET,
          BridgeConstants.TtsBackground.Action.UPDATE_PUNCTUATION_ECHO,
          punctuationEcho);
    },
  },
};
