// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Handles automation from a desktop automation node.
 */
import {AsyncUtil} from '/common/async_util.js';
import {AutomationPredicate} from '/common/automation_predicate.js';
import {AutomationUtil} from '/common/automation_util.js';
import {constants} from '/common/constants.js';
import {WrappingCursor} from '/common/cursors/cursor.js';
import {CursorRange} from '/common/cursors/range.js';
import {LocalStorage} from '/common/local_storage.js';
import {TestImportManager} from '/common/testing/test_import_manager.js';

import {Command} from '../../common/command.js';
import type {ChromeVoxEvent} from '../../common/custom_automation_event.js';
import {CustomAutomationEvent} from '../../common/custom_automation_event.js';
import {EventSourceType} from '../../common/event_source_type.js';
import {Msgs} from '../../common/msgs.js';
import {Personality, QueueMode, TtsCategory} from '../../common/tts_types.js';
import {AutoScrollHandler} from '../auto_scroll_handler.js';
import {AutomationObjectConstructorInstaller} from '../automation_object_constructor_installer.js';
import {CaptionsHandler} from '../captions_handler.js';
import {ChromeVox} from '../chromevox.js';
import {ChromeVoxRange} from '../chromevox_range.js';
import {ChromeVoxState} from '../chromevox_state.js';
import {TextEditHandler} from '../editing/text_edit_handler.js';
import {EventSource} from '../event_source.js';
import {CommandHandlerInterface} from '../input/command_handler_interface.js';
import {LiveRegions} from '../live_regions.js';
import {Output} from '../output/output.js';
import {OutputCustomEvent} from '../output/output_types.js';

import {DesktopAutomationInterface} from './desktop_automation_interface.js';

const ActionType = chrome.automation.ActionType;
type AutomationEvent = chrome.automation.AutomationEvent;
type AutomationNode = chrome.automation.AutomationNode;
const Dir = constants.Dir;
const EventType = chrome.automation.EventType;
const IntentCommandType = chrome.automation.IntentCommandType;
const IntentTextBoundaryType = chrome.automation.IntentTextBoundaryType;
const RoleType = chrome.automation.RoleType;
const StateType = chrome.automation.StateType;

export class DesktopAutomationHandler extends DesktopAutomationInterface {
  /**
   * Time to wait until processing more value changed events.
   */
  static MIN_VALUE_CHANGE_DELAY_MS = 50;

  /**
   * Time to wait until processing more alert events with the same text content.
   */
  static MIN_ALERT_DELAY_MS = 50;

  /**
   * URLs employing NTP (New tap page) searchbox.
   */
  static NTP_SEARCHBOX_URLS = new Set<string>(
      ['chrome://new-tab-page/', 'chrome-untrusted://lens-overlay/']);

  /** The object that speaks changes to an editable text field. */
  private textEditHandler_: TextEditHandler|null = null;

  /** The last time we handled a value changed event. */
  private lastValueChanged_: Date = new Date(0);

  /** The last node that triggered a value changed event. */
  private lastValueTarget_: AutomationNode|null = null;

  /** The last time we handled an alert event. */
  private lastAlertTime_: Date = new Date(0);

  /** The last alert text we processed. */
  private lastAlertText_ = '';

  /** The last root URL encountered. */
  private lastRootUrl_ = '';

  /** Whether a submenu is currently showing. */
  private isSubMenuShowing_ = false;

  /** Whether document selection changes should be ignored. */
  private shouldIgnoreDocumentSelectionFromAction_ = false;

  /** The current page number (for pagination tracking). */
  private currentPage_ = -1;

  /** The total number of pages (for pagination tracking). */
  private totalPages_ = -1;

  private constructor(node: AutomationNode) {
    super(node);
    this.init_(node);
  }

  private async init_(node: AutomationNode): Promise<void> {
    this.addListener_(
        EventType.ALERT, (event: AutomationEvent) => this.onAlert_(event));
    this.addListener_(EventType.BLUR, () => this.onBlur_());
    this.addListener_(
        EventType.DOCUMENT_SELECTION_CHANGED,
        (event: AutomationEvent) => this.onDocumentSelectionChanged_(event));
    this.addListener_(
        EventType.FOCUS, (event: AutomationEvent) => this.onFocus_(event));

    // Note that live region changes from views are really announcement
    // events. Their target nodes contain no live region semantics and have no
    // relation to live regions which are supported in |LiveRegions|.
    this.addListener_(
        EventType.LIVE_REGION_CHANGED,
        (event: AutomationEvent) => this.onLiveRegionChanged_(event));

    this.addListener_(
        EventType.LOAD_COMPLETE,
        (event: AutomationEvent) => this.onLoadComplete_(event));
    this.addListener_(
        EventType.FOCUS_AFTER_MENU_CLOSE,
        (event: AutomationEvent) => this.onMenuEnd_(event));
    this.addListener_(
        EventType.MENU_POPUP_START,
        (event: AutomationEvent) => this.onMenuPopupStart_(event));
    this.addListener_(
        EventType.MENU_START,
        (event: AutomationEvent) => this.onMenuStart_(event));
    this.addListener_(
        EventType.RANGE_VALUE_CHANGED,
        (event: AutomationEvent) => this.onValueChanged_(event));
    this.addListener_(
        EventType.SCROLL_POSITION_CHANGED,
        (event: AutomationEvent) => this.onScrollPositionChanged(event));
    this.addListener_(
        EventType.SCROLL_HORIZONTAL_POSITION_CHANGED,
        (event: AutomationEvent) => this.onScrollPositionChanged(event));
    this.addListener_(
        EventType.SCROLL_VERTICAL_POSITION_CHANGED,
        (event: AutomationEvent) => this.onScrollPositionChanged(event));
    // Called when a same-page link is followed or the url fragment changes.
    this.addListener_(
        EventType.SCROLLED_TO_ANCHOR,
        (event: AutomationEvent) => this.onScrolledToAnchor(event));
    this.addListener_(
        EventType.SELECTION,
        (event: AutomationEvent) => this.onSelection(event));
    this.addListener_(
        EventType.TEXT_SELECTION_CHANGED,
        (event: AutomationEvent) => this.onEditableChanged_(event));
    this.addListener_(
        EventType.VALUE_IN_TEXT_FIELD_CHANGED,
        (event: AutomationEvent) => this.onEditableChanged_(event));
    this.addListener_(
        EventType.VALUE_CHANGED,
        (event: AutomationEvent) => this.onValueChanged_(event));
    this.addListener_(
        EventType.AUTOFILL_AVAILABILITY_CHANGED,
        this.onAutofillAvailabilityChanged);
    this.addListener_(
        EventType.ORIENTATION_CHANGED,
        (event: AutomationEvent) => this.onOrientationChanged(event));
    // Called when a child MenuItem is collapsed.
    this.addListener_(
        EventType.COLLAPSED,
        (event: AutomationEvent) => this.onMenuItemCollapsed(event));

    await AutomationObjectConstructorInstaller.init(node);
    const focus = await AsyncUtil.getFocus();
    if (focus) {
      const event = new CustomAutomationEvent(
          EventType.FOCUS, focus,
          {eventFrom: 'page', eventFromAction: ActionType.FOCUS});
      this.onFocus_(event);
    }
  }

  override get textEditHandler(): TextEditHandler|undefined {
    return this.textEditHandler_ ?? undefined;
  }

  /**
   * Handles the result of a hit test.
   */
  onHitTestResult(node: AutomationNode): void {
    // It's possible the |node| hit has lost focus (via its root).
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    const host = node.root!.parent;
    if (node.parent && host && host.role === RoleType.WEB_VIEW &&
        !host.state![StateType.FOCUSED]) {
      return;
    }

    // It is possible that the user moved since we requested a hit test.  Bail
    // if the current range is valid and on the same page as the hit result
    // (but not the root).
    if (ChromeVoxRange.current && ChromeVoxRange.current.start &&
        ChromeVoxRange.current.start.node &&
        ChromeVoxRange.current.start.node.root) {
      const cur = ChromeVoxRange.current.start.node;
      if (cur.role !== RoleType.ROOT_WEB_AREA &&
          AutomationUtil.getTopLevelRoot(node) ===
              AutomationUtil.getTopLevelRoot(cur)) {
        return;
      }
    }

    chrome.automation.getFocus(function(focus: AutomationNode|undefined) {
      if (!focus && !node) {
        return;
      }
      focus = node || focus;
      const focusedRoot = AutomationUtil.getTopLevelRoot(focus);
      const output = new Output();
      if (focus !== focusedRoot && focusedRoot) {
        output.format('$name', focusedRoot);
      }

      // Even though we usually don't output events from actions, hit test
      // results should generate output.
      const range = CursorRange.fromNode(focus);
      ChromeVoxRange.set(range);
      output
          .withRichSpeechAndBraille(
              range, undefined, OutputCustomEvent.NAVIGATE)
          .go();
    });
  }

  /**
   * Makes an announcement without changing focus.
   */
  private onAlert_(evt: ChromeVoxEvent): void {
    if (CaptionsHandler.instance.maybeHandleAlert(evt)) {
      return;
    }

    const node = evt.target;
    const range = CursorRange.fromNode(node);
    const output = new Output();
    // Whenever chromevox is running together with dictation, we want to
    // announce the hints provided by the Dictation feature in a different voice
    // to differentiate them from regular UI text.
    if (node.className === 'DictationHintView') {
      output.withInitialSpeechProperties(Personality.DICTATION_HINT);
    }
    output.withSpeechCategory(TtsCategory.LIVE)
        .withSpeechAndBraille(range, undefined, evt.type);

    const alertDelayMet = new Date().getTime() - this.lastAlertTime_.getTime() >
        DesktopAutomationHandler.MIN_ALERT_DELAY_MS;
    if (!alertDelayMet && output.toString() === this.lastAlertText_) {
      return;
    }

    this.lastAlertTime_ = new Date();
    this.lastAlertText_ = output.toString();

    // A workaround for alert nodes that contain no actual content.
    if (output.toString()) {
      output.go();
    }
  }

  private onBlur_(): void {
    // Nullify focus if it no longer exists.
    chrome.automation.getFocus(function(focus: AutomationNode|undefined) {
      if (!focus) {
        ChromeVoxRange.set(null);
      }
    });
  }

  private onDocumentSelectionChanged_(evt: ChromeVoxEvent): void {
    let selectionStart = evt.target.selectionStartObject;

    // No selection.
    if (!selectionStart) {
      return;
    }

    // A caller requested this event be ignored.
    if (this.shouldIgnoreDocumentSelectionFromAction_ &&
        evt.eventFrom === 'action') {
      return;
    }

    // Editable selection.
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (selectionStart.state![StateType.EDITABLE]) {
      selectionStart =
          AutomationUtil.getEditableRoot(selectionStart) ?? selectionStart;
      this.onEditableChanged_(
          new CustomAutomationEvent(evt.type, selectionStart, {
            eventFrom: evt.eventFrom,
            eventFromAction: (evt as CustomAutomationEvent).eventFromAction,
            intents: evt.intents,
          }));
    }

    // Non-editable selections are handled in |Background|.
  }

  /**
   * Provides all feedback once a focus event fires.
   */
  private onFocus_(evt: ChromeVoxEvent): void {
    let node: AutomationNode|null = evt.target;
    const isRootWebArea = node.role === RoleType.ROOT_WEB_AREA;
    const isFrame = isRootWebArea && node.parent && node.parent.root &&
        node.parent.root.role === RoleType.ROOT_WEB_AREA;
    if (isRootWebArea && !isFrame && evt.eventFrom !== 'action') {
      chrome.automation.getFocus(
          (focus: AutomationNode|undefined) =>
              this.maybeRecoverFocusAndOutput_(evt, focus));
      return;
    }

    // Invalidate any previous editable text handler state.
    if (!this.createTextEditHandlerIfNeeded_(node, true)) {
      this.textEditHandler_ = null;
    }

    // Discard focus events on embeddedObject and webView.
    const shouldDiscard = AutomationPredicate.roles(
        [RoleType.EMBEDDED_OBJECT, RoleType.PLUGIN_OBJECT, RoleType.WEB_VIEW]);
    if (shouldDiscard(node)) {
      return;
    }

    if (node.role === RoleType.UNKNOWN) {
      // Ideally, we'd get something more meaningful than focus on an unknown
      // node, but this does sometimes occur. Sync downward to a more reasonable
      // target.
      node = AutomationUtil.findNodePre(
          node, Dir.FORWARD, AutomationPredicate.object);

      if (!node) {
        return;
      }
    }

    if (!node.root) {
      return;
    }

    if (!AutoScrollHandler.instance.onFocusEventNavigation(node)) {
      return;
    }

    // Update the focused root url, which gets used as part of focus recovery.
    this.lastRootUrl_ = node.root.docUrl ?? '';

    // Consider the case when a user presses tab rapidly. The key events may
    // come in far before the accessibility focus events. We therefore must
    // category flush here or the focus events will all queue up.
    Output.forceModeForNextSpeechUtterance(QueueMode.CATEGORY_FLUSH);

    const event = new CustomAutomationEvent(EventType.FOCUS, node, {
      eventFrom: evt.eventFrom,
      eventFromAction: (evt as CustomAutomationEvent).eventFromAction,
      intents: evt.intents,
    });
    this.onEventDefault(event);

    // Refresh the handler, if needed, now that ChromeVox focus is up to date.
    this.createTextEditHandlerIfNeeded_(node);

    // Reset `isSubMenuShowing_` when a focus changes because focus
    // changes should automatically close any menus.
    this.isSubMenuShowing_ = false;
  }

  private onLiveRegionChanged_(evt: ChromeVoxEvent): void {
    LiveRegions.announceDesktopLiveRegionChanged(evt.target);
  }

  /**
   * Provides all feedback once a load complete event fires.
   */
  onLoadComplete_(evt: ChromeVoxEvent): void {
    // We are only interested in load completes on valid top level roots.
    const top = AutomationUtil.getTopLevelRoot(evt.target);
    if (!top || top !== evt.target.root || !top.docUrl) {
      return;
    }

    this.lastRootUrl_ = '';
    chrome.automation.getFocus((focus: AutomationNode|undefined) => {
      // In some situations, ancestor windows get focused before a descendant
      // webView/rootWebArea. In particular, a window that gets opened but no
      // inner focus gets set. We catch this generically by re-targetting focus
      // if focus is the ancestor of the load complete target (below).
      if (!focus) {
        return;
      }
      const focusIsAncestor = AutomationUtil.isDescendantOf(evt.target, focus);
      const focusIsDescendant =
          AutomationUtil.isDescendantOf(focus, evt.target);
      if (!focusIsAncestor && !focusIsDescendant) {
        return;
      }

      if (focusIsAncestor) {
        focus = evt.target;
      }

      // Create text edit handler, if needed, now in order not to miss initial
      // value change if text field has already been focused when initializing
      // ChromeVox.
      this.createTextEditHandlerIfNeeded_(focus);

      // If auto read is set, skip focus recovery and start reading from the
      // top.
      if (LocalStorage.get('autoRead') &&
          AutomationUtil.getTopLevelRoot(evt.target) === evt.target) {
        ChromeVoxRange.set(CursorRange.fromNode(evt.target));
        ChromeVox.tts.stop();
        CommandHandlerInterface.instance.onCommand(Command.READ_FROM_HERE);
        return;
      }

      this.maybeRecoverFocusAndOutput_(evt, focus);
    });
  }

  /**
   * Sets whether document selections from actions should be ignored.
   */
  override ignoreDocumentSelectionFromAction(val: boolean): void {
    this.shouldIgnoreDocumentSelectionFromAction_ = val;
  }

  override onNativeNextOrPreviousCharacter(): void {
    if (this.textEditHandler) {
      this.textEditHandler.injectInferredIntents([{
        command: IntentCommandType.MOVE_SELECTION,
        textBoundary: IntentTextBoundaryType.CHARACTER,
      }]);
    }
  }

  override onNativeNextOrPreviousWord(isNext: boolean): void {
    if (this.textEditHandler) {
      this.textEditHandler.injectInferredIntents([{
        command: IntentCommandType.MOVE_SELECTION,
        textBoundary: isNext ? IntentTextBoundaryType.WORD_END :
                               IntentTextBoundaryType.WORD_START,
      }]);
    }
  }

  /**
   * Provides all feedback once a change event in a text field fires.
   */
  private onEditableChanged_(evt: ChromeVoxEvent): void {
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (!evt.target.state![StateType.EDITABLE]) {
      return;
    }

    // Skip all unfocused text fields.
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (!evt.target.state![StateType.FOCUSED] &&
        evt.target.state![StateType.EDITABLE]) {
      return;
    }

    const isInput = evt.target.htmlTag === 'input';
    const isTextArea = evt.target.htmlTag === 'textarea';
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    const isContentEditable = evt.target.state![StateType.RICHLY_EDITABLE];

    switch (evt.type) {
      case EventType.DOCUMENT_SELECTION_CHANGED:
        // Event type DOCUMENT_SELECTION_CHANGED is duplicated by
        // TEXT_SELECTION_CHANGED for <input> elements.
        if (isInput) {
          return;
        }
        break;
      case EventType.FOCUS:
        // Allowed regardless of the role.
        break;
      case EventType.TEXT_SELECTION_CHANGED:
        // Event type TEXT_SELECTION_CHANGED is duplicated by
        // DOCUMENT_SELECTION_CHANGED for content editables and text areas.
        // Fall through.
      case EventType.VALUE_IN_TEXT_FIELD_CHANGED:
        // By design, generated only for simple inputs.
        if (isContentEditable || isTextArea) {
          return;
        }
        break;
      case EventType.VALUE_CHANGED:
        // During a transition period, VALUE_CHANGED is duplicated by
        // VALUE_IN_TEXT_FIELD_CHANGED for text field roles.
        //
        // TOTO(NEKTAR): Deprecate and remove VALUE_CHANGED.
        if (isContentEditable || isInput || isTextArea) {
          return;
        }
        break;
      default:
        return;
    }

    if (!this.createTextEditHandlerIfNeeded_(evt.target)) {
      return;
    }

    if (!ChromeVoxRange.current) {
      this.onEventDefault(evt);
      ChromeVoxRange.set(CursorRange.fromNode(evt.target));
    }

    // Sync the ChromeVox range to the editable, if a selection exists.
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    const selectionStartObject = evt.target.root!.selectionStartObject;
    const selectionStartOffset = evt.target.root!.selectionStartOffset || 0;
    const selectionEndObject = evt.target.root!.selectionEndObject;
    const selectionEndOffset = evt.target.root!.selectionEndOffset || 0;
    if (selectionStartObject && selectionEndObject) {
      // Sync to the selection's deep equivalent especially in editables, where
      // selection is often on the root text field with a child offset.
      const selectedRange = new CursorRange(
          new WrappingCursor(selectionStartObject, selectionStartOffset)
              .deepEquivalent,
          new WrappingCursor(selectionEndObject, selectionEndOffset)
              .deepEquivalent);

      // Sync ChromeVox range with selection.
      // TODO(crbug.com/314203187): Not null asserted, check that this is
      // correct.
      if (!ChromeVoxState.instance!.isReadingContinuously) {
        ChromeVoxRange.set(selectedRange, true /* from editing */);
      }
    }
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    this.textEditHandler_!.onEvent(evt);
  }

  /**
   * Provides all feedback once a rangeValueChanged or a valueInTextFieldChanged
   * event fires.
   */
  private onValueChanged_(evt: ChromeVoxEvent): void {
    // Skip root web areas.
    if (evt.target.role === RoleType.ROOT_WEB_AREA) {
      return;
    }

    // Delegate to the edit text handler if this is an editable, with the
    // exception of spin buttons.
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (evt.target.state![StateType.EDITABLE] &&
        evt.target.role !== RoleType.SPIN_BUTTON) {
      // If a value changed event came from NTP Searchbox input, announce the
      // new value. This is a special behavior for NTP Searchbox to announce
      // its suggestions when users navigate them using the up/down arrow key.
      // `evt.intents` is empty when NTP Searchbox gets auto-completed by
      // navigating a list of suggestions; it won't be empty if users type,
      // delete, or paste text in NTP Searchbox.
      // TODO(crbug.com/328824322): Remove the special behavior and implement
      // the active-descendant-based approach in NTP Searchbox when
      // crbug.com/346835896 lands in the stable.
      // TODO(crbug.com/314203187): Not null asserted, check that this is
      // correct.
      const urlString = evt.target.root?.url ?? '';
      if (DesktopAutomationHandler.NTP_SEARCHBOX_URLS.has(urlString) &&
          evt.target.htmlTag === 'input' && !evt.intents?.length) {
        new Output()
            .withString(evt.target.value!)
            .withSpeechCategory(TtsCategory.NAV)
            .withQueueMode(QueueMode.CATEGORY_FLUSH)
            .withoutFocusRing()
            .go();
        return;
      }
      this.onEditableChanged_(evt);
      return;
    }

    const target = evt.target;
    const fromDesktop = target.root!.role === RoleType.DESKTOP;
    const onDesktop =
        ChromeVoxRange.current?.start.node.root!.role === RoleType.DESKTOP;
    const isSlider = target.role === RoleType.SLIDER;

    // TODO(accessibility): get rid of callers who use value changes on list
    // boxes.
    const isListBox = target.role === RoleType.LIST_BOX;
    if (fromDesktop && !onDesktop && !isSlider && !isListBox) {
      // Only respond to value changes from the desktop if it's coming from a
      // slider e.g. the volume slider. Do this to avoid responding to frequent
      // updates from UI e.g. download progress bars.
      return;
    }

    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (!target.state![StateType.FOCUSED] &&
        (!fromDesktop || (!isSlider && !isListBox)) &&
        !AutomationUtil.isDescendantOf(
            ChromeVoxRange.current?.start.node!, target)) {
      return;
    }

    if (new Date().getTime() - this.lastValueChanged_.getTime() <=
        DesktopAutomationHandler.MIN_VALUE_CHANGE_DELAY_MS) {
      return;
    }

    this.lastValueChanged_ = new Date();

    const output = new Output();
    output.withoutFocusRing();

    if (fromDesktop &&
        (!this.lastValueTarget_ || this.lastValueTarget_ !== target)) {
      const range = CursorRange.fromNode(target);
      output.withRichSpeechAndBraille(range, range, OutputCustomEvent.NAVIGATE);
      this.lastValueTarget_ = target;
    } else {
      output.format(
          '$if($value, $value, $if($valueForRange, $valueForRange))', target);
    }

    Output.forceModeForNextSpeechUtterance(QueueMode.INTERJECT);
    output.go();
  }

  /**
   * Handle updating the active indicator when the document scrolls.
   */
  onScrollPositionChanged(evt: ChromeVoxEvent): void {
    const currentRange = ChromeVoxRange.current;
    if (currentRange && currentRange.isValid()) {
      new Output().withLocation(currentRange, undefined, evt.type).go();

      if (EventSource.get() !== EventSourceType.TOUCH_GESTURE) {
        return;
      }

      const root = AutomationUtil.getTopLevelRoot(currentRange.start.node);
      if (!root || root.scrollY === undefined) {
        return;
      }

      const currentPage = Math.ceil(root.scrollY / root.location.height) || 1;
      // TODO(crbug.com/314203187): Not null asserted, check that this is
      // correct.
      const totalPages =
          Math.ceil(
              (root.scrollYMax! - root.scrollYMin!) / root.location.height) ||
          1;

      // Ignore announcements if we've already announced something for this page
      // change. Note that this need not care about the root if it changed as
      // well.
      if (this.currentPage_ === currentPage &&
          this.totalPages_ === totalPages) {
        return;
      }
      this.currentPage_ = currentPage;
      this.totalPages_ = totalPages;
      ChromeVox.tts.speak(
          Msgs.getMsg(
              'describe_pos_by_page',
              [String(currentPage), String(totalPages)]),
          QueueMode.QUEUE);
    }
  }

  onSelection(evt: ChromeVoxEvent): void {
    // Invalidate any previous editable text handler state since some nodes,
    // like menuitems, can receive selection while focus remains on an
    // editable leading to braille output routing to the editable.
    this.textEditHandler_ = null;

    chrome.automation.getFocus((focus: AutomationNode|undefined) => {
      const target = evt.target;

      // Desktop tabs get "selection" when there's a focused webview during
      // tab switching.
      // TODO(crbug.com/314203187): Not null asserted, check that this is
      // correct.
      if (target.role === RoleType.TAB &&
          target.root!.role === RoleType.DESKTOP) {
        // Read it only if focus is on the
        // omnibox. We have to resort to this check to get tab switching read
        // out because on switching to a new tab, focus actually remains on the
        // *same* omnibox.
        const currentRange = ChromeVoxRange.current;
        if (currentRange && currentRange.start && currentRange.start.node &&
            currentRange.start.node.className === 'OmniboxViewViews') {
          const range = CursorRange.fromNode(target);
          new Output()
              .withRichSpeechAndBraille(
                  range, range, OutputCustomEvent.NAVIGATE)
              .go();
        }

        // This also suppresses tab selection output when ChromeVox is not on
        // the omnibox.
        return;
      }

      let override = false;
      // TODO(crbug.com/314203187): Not null asserted, check that this is
      // correct.
      const isDesktop =
          (focus && target.root === focus.root &&
           focus.root!.role === RoleType.DESKTOP);

      // TableView fires selection events on rows/cells
      // and we want to ignore those because it also fires focus events.
      const skip = AutomationPredicate.roles(
          [RoleType.CELL, RoleType.GRID_CELL, RoleType.ROW]);
      if (isDesktop && skip(target)) {
        return;
      }

      // IME candidates are announced, independent of focus.
      // This shouldn't move ChromeVoxRange to keep editing work.
      if (target.role === RoleType.IME_CANDIDATE) {
        const range = CursorRange.fromNode(target);
        new Output().withRichSpeech(range, undefined, evt.type).go();
        return;
      }

      // Menu items always announce on selection events, independent of focus.
      if (AutomationPredicate.menuItem(target)) {
        override = true;
      }

      // Overview mode should allow selections.
      if (isDesktop) {
        let walker: AutomationNode|undefined = target;
        while (walker && walker.className !== 'OverviewDeskBarWidget' &&
               walker.className !== 'OverviewModeLabel' &&
               walker.className !== 'Desk_Container_A') {
          walker = walker.parent;
        }

        override = Boolean(walker) || override;
      }

      // Autofill popup menu items are always announced on selection events,
      // independent of focus.
      // The `PopupSeparatorView` is intentionally omitted because it
      // cannot be focused.
      if (target.className === 'PopupSuggestionView' ||
          target.className === 'PopupPasswordSuggestionView' ||
          target.className === 'PopupFooterView' ||
          target.className === 'PopupWarningView' ||
          target.className === 'PopupBaseView' ||
          target.className ===
              'PasswordGenerationPopupViewViews::GeneratedPasswordBox' ||
          target.className === 'PopupRowView' ||
          target.className === 'PopupRowWithButtonView' ||
          target.className === 'PopupRowContentView') {
        override = true;
      }

      // The popup view associated with a datalist element does not descend
      // from the input with which it is associated.
      if (focus?.role === RoleType.TEXT_FIELD_WITH_COMBO_BOX &&
          target.role === RoleType.LIST_BOX_OPTION) {
        override = true;
      }

      if (override || (focus && AutomationUtil.isDescendantOf(target, focus))) {
        this.onEventDefault(evt);
      }
    });
  }

  /**
   * Provides all feedback once a menu end event fires.
   */
  private onMenuEnd_(_evt: ChromeVoxEvent): void {
    // This is a work around for Chrome context menus not firing a focus event
    // after you close them.
    chrome.automation.getFocus((focus: AutomationNode|undefined) => {
      // Directly output the node here; do not go through |onFocus_| as it
      // contains a lot of logic that can move the selection (if in an
      // editable).
      if (!focus) {
        return;
      }
      const range = CursorRange.fromNode(focus);
      new Output()
          .withRichSpeechAndBraille(
              range, undefined, OutputCustomEvent.NAVIGATE)
          .go();
      ChromeVoxRange.set(range);
    });

    // Reset the state to stop handling a Collapsed event.
    this.isSubMenuShowing_ = false;
  }

  onMenuPopupStart_(event: ChromeVoxEvent): void {
    // Handles a MenuPopupStart event only if it's from a menu node. This event
    // will be fired from a menu node, instead of a menu item node, when its
    // sub-menu gets expanded.
    if (!event.target || !AutomationPredicate.menu(event.target)) {
      return;
    }
    // Set a state to start handling a Collapsed event.
    this.isSubMenuShowing_ = true;
  }

  private onMenuStart_(event: ChromeVoxEvent): void {
    Output.forceModeForNextSpeechUtterance(QueueMode.CATEGORY_FLUSH);
    this.onEventDefault(event);
  }

  /**
   * Provides all feedback once a scrolled to anchor event fires.
   */
  onScrolledToAnchor(evt: ChromeVoxEvent): void {
    if (!evt.target) {
      return;
    }

    if (ChromeVoxRange.current) {
      const target = evt.target;
      const current = ChromeVoxRange.current.start.node;
      if (AutomationUtil.getTopLevelRoot(current) !==
          AutomationUtil.getTopLevelRoot(target)) {
        // Ignore this event if the root of the target differs from that of the
        // current range.
        return;
      }
    }

    this.onEventDefault(evt);
  }

  /**
   * Handles autofill availability changes.
   */
  onAutofillAvailabilityChanged(evt: ChromeVoxEvent): void {
    const node = evt.target;
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    const state = node.state!;
    const currentRange = ChromeVoxRange.current;

    // Notify the user about available autofill options on focused element.
    if (currentRange && currentRange.isValid() && state[StateType.FOCUSED] &&
        state[StateType.AUTOFILL_AVAILABLE]) {
      new Output()
          .withString(Msgs.getMsg('hint_autocomplete_list'))
          .withLocation(currentRange, undefined, evt.type)
          .withQueueMode(QueueMode.QUEUE)
          .go();
    }
  }

  /**
   * Handles orientation changes on the desktop node.
   */
  onOrientationChanged(evt: ChromeVoxEvent): void {
    // Changes on display metrics result in the desktop node's
    // vertical/horizontal states changing.
    if (evt.target.role === RoleType.DESKTOP) {
      // TODO(crbug.com/314203187): Not null asserted, check that this is
      // correct.
      const msg = evt.target.state![StateType.HORIZONTAL] ? 'device_landscape' :
                                                            'device_portrait';
      new Output().format('@' + msg).go();
    }
  }

  /**
   * Handles focus back to a parent MenuItem when its child is collapsed.
   */
  onMenuItemCollapsed(evt: ChromeVoxEvent): void {
    const target = evt.target;
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (!this.isSubMenuShowing_ || !AutomationPredicate.menuItem(target) ||
        !target.state![StateType.COLLAPSED] || !target.selected) {
      return;
    }

    this.onEventDefault(evt);
  }

  /**
   * Create an editable text handler for the given node if needed.
   * @param node
   * @param opt_onFocus True if called within a focus event
   *     handler. False by default.
   * @return True if the handler exists (created/already present).
   */
  createTextEditHandlerIfNeeded_(node: AutomationNode, opt_onFocus?: boolean):
      boolean {
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (!node.state![StateType.EDITABLE]) {
      return false;
    }

    if (!ChromeVoxRange.current || !ChromeVoxRange.current.start ||
        !ChromeVoxRange.current.start.node) {
      return false;
    }

    const topRoot = AutomationUtil.getTopLevelRoot(node);
    if (!node.state![StateType.FOCUSED] ||
        (topRoot && topRoot.parent &&
         !topRoot.parent.state![StateType.FOCUSED])) {
      return false;
    }

    // Re-target the node to the root of the editable.
    const target: AutomationNode|undefined =
        AutomationUtil.getEditableRoot(node);
    let voxTarget = ChromeVoxRange.current.start.node;
    voxTarget = AutomationUtil.getEditableRoot(voxTarget) || voxTarget;

    // It is possible that ChromeVox has range over some other node when a
    // text field is focused. Only allow this when focus is on a desktop node,
    // ChromeVox is over the keyboard, or during focus events.
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    if (!target || !voxTarget ||
        (!opt_onFocus && target !== voxTarget &&
         target.root!.role !== RoleType.DESKTOP &&
         voxTarget.root!.role !== RoleType.DESKTOP &&
         !AutomationUtil.isDescendantOf(target, voxTarget) &&
         !AutomationUtil.getAncestors(voxTarget.root!)
              .find((n: AutomationNode) => n.role === RoleType.KEYBOARD))) {
      return false;
    }

    if (!this.textEditHandler_ || this.textEditHandler_.node !== target) {
      this.textEditHandler_ = TextEditHandler.createForNode(target);
    }

    return Boolean(this.textEditHandler_);
  }

  private maybeRecoverFocusAndOutput_(
      evt: ChromeVoxEvent, focus: AutomationNode|undefined): void {
    if (!focus) {
      return;
    }
    const focusedRoot = AutomationUtil.getTopLevelRoot(focus);
    if (!focusedRoot) {
      return;
    }

    let curRoot;
    if (ChromeVoxRange.current) {
      curRoot =
          AutomationUtil.getTopLevelRoot(ChromeVoxRange.current.start.node);
    }

    // If initial focus was already placed inside this page (e.g. if a user
    // starts tabbing before load complete), then don't move ChromeVox's
    // position on the page.
    if (curRoot && focusedRoot === curRoot &&
        this.lastRootUrl_ === focusedRoot.docUrl) {
      return;
    }

    this.lastRootUrl_ = focusedRoot.docUrl || '';
    const o = new Output();
    // Restore to previous position.
    // TODO(crbug.com/314203187): Not null asserted, check that this is correct.
    let url = focusedRoot.docUrl!;
    url = url.substring(0, url.indexOf('#')) || url;
    const pos = ChromeVoxState.position[url];

    // Deny recovery for chrome urls.
    if (pos && url.indexOf('chrome://') !== 0) {
      focusedRoot.hitTestWithReply(
          pos.x, pos.y, node => this.onHitTestResult(node));
      return;
    }

    // If range is already on |focus|, exit early to prevent duplicating output.
    const currentRange = ChromeVoxRange.current;
    if (currentRange && currentRange.start && currentRange.start.node &&
        currentRange.start.node === focus) {
      return;
    }

    // This catches initial focus (i.e. on startup).
    if (!curRoot && focus !== focusedRoot) {
      o.format('$name', focusedRoot);
    }

    ChromeVoxRange.set(CursorRange.fromNode(focus));
    if (!ChromeVoxRange.current) {
      return;
    }

    o.withRichSpeechAndBraille(ChromeVoxRange.current, undefined, evt.type)
        .go();

    // Reset `isSubMenuShowing_` when a focus changes because focus
    // changes should automatically close any menus.
    this.isSubMenuShowing_ = false;
  }

  /** Initializes global state for DesktopAutomationHandler. */
  static async init(): Promise<void> {
    if (DesktopAutomationInterface.instance) {
      throw new Error('DesktopAutomationInterface.instance already exists.');
    }

    const desktop = await AsyncUtil.getDesktop();
    DesktopAutomationInterface.instance = new DesktopAutomationHandler(desktop);
  }
}

TestImportManager.exportForTesting(DesktopAutomationHandler);
