// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {HistoryAppElement, HistoryEntry, HistoryItemElement, HistoryListElement, HistoryToolbarElement} from 'chrome://history/history.js';
import {BrowserServiceImpl, CrRouter, ensureLazyLoaded} from 'chrome://history/history.js';
import {webUIListenerCallback} from 'chrome://resources/js/cr.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {isMac} from 'chrome://resources/js/platform.js';
import {PromiseResolver} from 'chrome://resources/js/promise_resolver.js';
import {assertDeepEquals, assertEquals, assertFalse, assertGT, assertNotEquals, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {pressAndReleaseKeyOn} from 'chrome://webui-test/keyboard_mock_interactions.js';
import {flushTasks} from 'chrome://webui-test/polymer_test_util.js';
import {eventToPromise, microtasksFinished} from 'chrome://webui-test/test_util.js';

import {TestBrowserService} from './test_browser_service.js';
import {createHistoryEntry, createHistoryInfo, shiftClick, waitForEvent} from './test_util.js';

suite('HistoryListTest', function() {
  let app: HistoryAppElement;
  let element: HistoryListElement;
  let toolbar: HistoryToolbarElement;
  let testService: TestBrowserService;

  const TEST_HISTORY_RESULTS = [
    createHistoryEntry('2016-03-15', 'https://www.google.com'),
    createHistoryEntry('2016-03-14 10:00', 'https://www.example.com'),
    createHistoryEntry('2016-03-14 9:00', 'https://www.google.com'),
    createHistoryEntry('2016-03-13', 'https://en.wikipedia.org'),
  ];
  TEST_HISTORY_RESULTS[2]!.starred = true;

  const ADDITIONAL_RESULTS = [
    createHistoryEntry('2016-03-13 10:00', 'https://en.wikipedia.org'),
    createHistoryEntry('2016-03-13 9:50', 'https://www.youtube.com'),
    createHistoryEntry('2016-03-11', 'https://www.google.com'),
    createHistoryEntry('2016-03-10', 'https://www.example.com'),
  ];

  setup(function() {
    window.history.replaceState({}, '', '/');
    document.body.innerHTML = window.trustedTypes!.emptyHTML;
    // Make viewport tall enough to render all items.
    document.body.style.height = '1000px';
    CrRouter.resetForTesting();
    testService = new TestBrowserService();
    BrowserServiceImpl.setInstance(testService);

    app = document.createElement('history-app');
  });

  /**
   * @param queryResults The query results to initialize
   *     the page with.
   * @param query The query to use in the QueryInfo.
   * @return Promise that resolves when initialization is complete
   *     and the lazy loaded module has been loaded.
   */
  function finishSetup(
      queryResults: HistoryEntry[], finished: boolean = true,
      query?: string): Promise<any> {
    testService.handler.setResultFor('queryHistory', Promise.resolve({
      results:
          {info: {finished: finished, term: query || ''}, value: queryResults},
    }));
    document.body.appendChild(app);

    element = app.$.history;
    toolbar = app.$.toolbar;
    const queryManager = app.shadowRoot!.querySelector('history-query-manager');
    assertTrue(!!queryManager);
    queryManager.queryState = {...queryManager.queryState, incremental: true};
    return Promise.all([
      testService.handler.whenCalled('queryHistory'),
      ensureLazyLoaded(),
      microtasksFinished(),
      eventToPromise('viewport-filled', element.$.infiniteList),
    ]);
  }

  function getHistoryData(): HistoryEntry[] {
    return element.$.infiniteList.items as HistoryEntry[];
  }

  test('IsEmpty', async () => {
    await finishSetup([]);
    assertTrue(element.isEmpty);

    // Load some results.
    testService.handler.resetResolver('queryHistory');
    testService.handler.setResultFor(
        'queryHistoryContinuation',
        Promise.resolve(
            {results: {info: createHistoryInfo(), value: ADDITIONAL_RESULTS}}));
    element.dispatchEvent(new CustomEvent(
        'query-history', {detail: true, bubbles: true, composed: true}));
    await testService.handler.whenCalled('queryHistoryContinuation');
    await flushTasks();

    assertFalse(element.isEmpty);
  });

  test('DeletingSingleItem', async function() {
    const visit = createHistoryEntry('2015-01-01', 'http://example.com');
    await finishSetup([visit]);
    assertEquals(getHistoryData().length, 1);
    const items = element.shadowRoot.querySelectorAll('history-item');

    assertEquals(1, items.length);
    items[0]!.$.checkbox.click();
    await microtasksFinished();
    assertDeepEquals([true], getHistoryData().map(i => i.selected));
    toolbar.deleteSelectedItems();
    await flushTasks();
    const dialog = element.$.dialog.get();
    assertTrue(dialog.open);
    testService.handler.resetResolver('queryHistory');
    testService.handler.setResultFor('removeVisits', Promise.resolve([visit]));
    testService.handler.setResultFor('queryHistory', Promise.resolve({}));
    element.shadowRoot.querySelector<HTMLElement>('.action-button')!.click();
    const visits = await testService.handler.whenCalled('removeVisits');
    assertEquals(1, visits.length);
    assertEquals('http://example.com', visits[0].url);
    assertEquals(Date.parse('2015-01-01 UTC'), visits[0].timestamps[0]);

    // The list should fire a query-history event which results in a
    // queryHistory call, since deleting the only item results in an
    // empty history list.
    return testService.handler.whenCalled('queryHistory');
  });

  test('CancellingSelectionOfMultipleItems', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[2]!.$.checkbox.click();
    items[3]!.$.checkbox.click();
    await microtasksFinished();

    // Make sure that the array of data that determines whether or not
    // an item is selected is what we expect after selecting the two
    // items.
    assertDeepEquals(
        [false, false, true, true], getHistoryData().map(i => i.selected));

    toolbar.clearSelectedItems();
    await microtasksFinished();

    // Make sure that clearing the selection updates both the array
    // and the actual history-items affected.
    assertDeepEquals(
        [false, false, false, false], getHistoryData().map(i => i.selected));

    assertFalse(items[2]!.selected);
    assertFalse(items[3]!.selected);
  });

  test('SelectionOfMultipleItemsUsingShiftClick', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[1]!.$.checkbox.click();
    await microtasksFinished();
    assertDeepEquals(
        [false, true, false, false], getHistoryData().map(i => i.selected));
    assertDeepEquals([1], Array.from(element.selectedItems).sort());

    // Shift-select to the last item.
    await shiftClick(items[3]!.$.checkbox);
    assertDeepEquals(
        [false, true, true, true], getHistoryData().map(i => i.selected));
    assertDeepEquals([1, 2, 3], Array.from(element.selectedItems).sort());

    // Shift-select back to the first item.
    await shiftClick(items[0]!.$.checkbox);
    assertDeepEquals(
        [true, true, true, true], getHistoryData().map(i => i.selected));
    assertDeepEquals([0, 1, 2, 3], Array.from(element.selectedItems).sort());

    // Shift-deselect to the third item.
    await shiftClick(items[2]!.$.checkbox);
    assertDeepEquals(
        [false, false, false, true], getHistoryData().map(i => i.selected));
    assertDeepEquals([3], Array.from(element.selectedItems).sort());

    // Select the second item.
    items[1]!.$.checkbox.click();
    await microtasksFinished();
    assertDeepEquals(
        [false, true, false, true], getHistoryData().map(i => i.selected));
    assertDeepEquals([1, 3], Array.from(element.selectedItems).sort());

    // Shift-deselect to the last item.
    await shiftClick(items[3]!.$.checkbox);
    assertDeepEquals(
        [false, false, false, false], getHistoryData().map(i => i.selected));
    assertDeepEquals([], Array.from(element.selectedItems).sort());

    // Shift-select back to the third item.
    await shiftClick(items[2]!.$.checkbox);
    assertDeepEquals(
        [false, false, true, true], getHistoryData().map(i => i.selected));
    assertDeepEquals([2, 3], Array.from(element.selectedItems).sort());

    // Remove selected items.
    element.removeItemsByIndexForTesting(Array.from(element.selectedItems));
    await microtasksFinished();
    assertDeepEquals(
        ['https://www.google.com', 'https://www.example.com'],
        getHistoryData().map(i => i.title));
  });

  // See http://crbug.com/845802.
  test('DisablingCtrlAOnSyncedTabsPage', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    app.shadowRoot!.querySelector('history-router')!.selectedPage =
        'syncedTabs';
    await microtasksFinished();
    const field = toolbar.$.mainToolbar.getSearchField();
    field.blur();
    assertFalse(field.showingSearch);

    const modifier = isMac ? 'meta' : 'ctrl';
    pressAndReleaseKeyOn(document.body, 65, modifier, 'a');

    assertDeepEquals(
        [false, false, false, false], getHistoryData().map(i => i.selected));
  });

  test('SettingFirstAndLastItems', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    const items = element.shadowRoot.querySelectorAll('history-item');
    assertTrue(items[0]!.isCardStart);
    assertTrue(items[0]!.isCardEnd);
    assertFalse(items[1]!.isCardEnd);
    assertFalse(items[2]!.isCardStart);
    assertTrue(items[2]!.isCardEnd);
    assertTrue(items[3]!.isCardStart);
    assertTrue(items[3]!.isCardEnd);
  });

  async function loadWithAdditionalResults() {
    await finishSetup(TEST_HISTORY_RESULTS);
    testService.handler.resetResolver('queryHistory');
    testService.handler.setResultFor(
        'queryHistoryContinuation',
        Promise.resolve(
            {results: {info: createHistoryInfo(), value: ADDITIONAL_RESULTS}}));
    element.dispatchEvent(new CustomEvent(
        'query-history', {detail: true, bubbles: true, composed: true}));
    await testService.handler.whenCalled('queryHistoryContinuation');
    return microtasksFinished();
  }

  test('UpdatingHistoryResults', async function() {
    await loadWithAdditionalResults();
    const items = element.shadowRoot.querySelectorAll('history-item');
    assertTrue(items[3]!.isCardStart);
    assertTrue(items[5]!.isCardEnd);

    assertTrue(items[6]!.isCardStart);
    assertTrue(items[6]!.isCardEnd);

    assertTrue(items[7]!.isCardStart);
    assertTrue(items[7]!.isCardEnd);
  });

  test('DeletingMultipleItemsFromView', async function() {
    await loadWithAdditionalResults();
    element.removeItemsByIndexForTesting([2, 5, 7]);
    await microtasksFinished();
    const items = element.shadowRoot.querySelectorAll('history-item');

    const historyData = getHistoryData();
    assertEquals(historyData.length, 5);
    assertEquals(historyData[0]!.dateRelativeDay, '2016-03-15');
    assertEquals(historyData[2]!.dateRelativeDay, '2016-03-13');
    assertEquals(historyData[4]!.dateRelativeDay, '2016-03-11');

    // Checks that the first and last items have been reset correctly.
    assertTrue(items[2]!.isCardStart);
    assertTrue(items[3]!.isCardEnd);
    assertTrue(items[4]!.isCardStart);
    assertTrue(items[4]!.isCardEnd);
  });

  test('SearchResultsDisplayWithCorrectItemTitle', async function() {
    await finishSetup(
        [createHistoryEntry('2016-03-15', 'https://www.google.com')]);
    element.searchedTerm = 'Google';
    await microtasksFinished();
    const item = element.shadowRoot.querySelector('history-item')!;
    assertTrue(item.isCardStart);
    const heading =
        item.shadowRoot.querySelector<HTMLElement>(
                           '#date-accessed')!.textContent!;
    const title = item.$.link;

    // Check that the card title displays the search term somewhere.
    const index = heading.indexOf('Google');
    assertTrue(index !== -1);

    // Check that the search term is bolded correctly in the
    // history-item.
    assertGT(title.children[1]!.innerHTML.indexOf('<b>google</b>'), -1);
  });

  test('CorrectDisplayMessageWhenNoHistoryAvailable', async function() {
    await finishSetup([]);
    await microtasksFinished();
    assertFalse(element.$.noResults.hidden);
    assertNotEquals('', element.$.noResults.textContent!.trim());
    assertTrue(element.$.infiniteList.hidden);

    testService.handler.setResultFor('queryHistory', Promise.resolve({
      results: {info: createHistoryInfo(), value: TEST_HISTORY_RESULTS},
    }));
    element.dispatchEvent(new CustomEvent(
        'query-history', {bubbles: true, composed: true, detail: false}));
    await testService.handler.whenCalled('queryHistory');
    await flushTasks();
    assertTrue(element.$.noResults.hidden);
    assertFalse(element.$.infiniteList.hidden);
  });

  test('MoreFromThisSiteSendsAndSetsCorrectData', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    testService.handler.resetResolver('queryHistory');
    testService.handler.setResultFor('queryHistory', Promise.resolve({
      results: {
        info: createHistoryInfo('www.google.com'),
        value: TEST_HISTORY_RESULTS,
      },
    }));
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[0]!.$['menu-button'].click();
    await microtasksFinished();
    element.$.sharedMenu.get();
    element.shadowRoot.querySelector<HTMLElement>('#menuMoreButton')!.click();
    await microtasksFinished();
    const query = await testService.handler.whenCalled('queryHistory');
    assertEquals('host:www.google.com', query[0]);
    assertEquals(
        'host:www.google.com',
        toolbar.$.mainToolbar.getSearchField().getValue());

    element.$.sharedMenu.get().close();
    items[0]!.$['menu-button'].click();
    await microtasksFinished();
    assertTrue(element.shadowRoot.querySelector<HTMLElement>(
                                     '#menuMoreButton')!.hidden);

    element.$.sharedMenu.get().close();
    items[1]!.$['menu-button'].click();
    await microtasksFinished();
    assertFalse(
        element.shadowRoot.querySelector<HTMLElement>(
                              '#menuMoreButton')!.hidden);
  });

  test('ChangingSearchDeselectsItems', async function() {
    await finishSetup(
        [createHistoryEntry('2016-06-9', 'https://www.example.com')], true,
        'ex');
    const item = element.shadowRoot.querySelector('history-item')!;
    item.$.checkbox.click();
    await microtasksFinished();

    assertEquals(1, toolbar.count);
    app.shadowRoot!.querySelector(
                       'history-query-manager')!.queryState.incremental = false;

    testService.handler.resetResolver('queryHistory');
    testService.handler.setResultFor('queryHistory', Promise.resolve({
      results: {
        info: createHistoryInfo('ample'),
        value: [createHistoryEntry('2016-06-9', 'https://www.example.com')],
      },
    }));
    element.dispatchEvent(new CustomEvent(
        'query-history', {bubbles: true, composed: true, detail: false}));
    await testService.handler.whenCalled('queryHistory');
    assertEquals(0, toolbar.count);
  });

  test('DeleteItemsEndToEnd', async function() {
    await loadWithAdditionalResults();
    const dialog = element.$.dialog.get();
    let items = element.shadowRoot.querySelectorAll('history-item');

    items[2]!.$.checkbox.click();
    items[5]!.$.checkbox.click();
    items[7]!.$.checkbox.click();
    await microtasksFinished();

    toolbar.deleteSelectedItems();
    await microtasksFinished();
    testService.handler.resetResolver('removeVisits');
    const results = [...TEST_HISTORY_RESULTS, ...ADDITIONAL_RESULTS];
    testService.handler.setResultFor(
        'removeVisits', Promise.resolve([results[2], results[5], results[7]]));
    // Confirmation dialog should appear.
    assertTrue(dialog.open);
    element.shadowRoot.querySelector<HTMLElement>('.action-button')!.click();
    await microtasksFinished();
    const visits = await testService.handler.whenCalled('removeVisits');
    assertEquals(3, visits.length);
    assertEquals(TEST_HISTORY_RESULTS[2]!.url, visits[0]!.url);
    assertEquals(
        TEST_HISTORY_RESULTS[2]!.allTimestamps[0], visits[0]!.timestamps[0]);
    assertEquals(ADDITIONAL_RESULTS[1]!.url, visits[1]!.url);
    assertEquals(
        ADDITIONAL_RESULTS[1]!.allTimestamps[0], visits[1]!.timestamps[0]);
    assertEquals(ADDITIONAL_RESULTS[3]!.url, visits[2]!.url);
    assertEquals(
        ADDITIONAL_RESULTS[3]!.allTimestamps[0], visits[2]!.timestamps[0]);
    const historyData = getHistoryData();
    assertEquals(5, historyData.length);
    assertEquals(historyData[0]!.dateRelativeDay, '2016-03-15');
    assertEquals(historyData[2]!.dateRelativeDay, '2016-03-13');
    assertEquals(historyData[4]!.dateRelativeDay, '2016-03-11');
    assertFalse(dialog.open);

    // Ensure the UI is correctly updated.
    items = element.shadowRoot.querySelectorAll('history-item');

    assertEquals('https://www.google.com', items[0]!.item?.title);
    assertEquals('https://www.example.com', items[1]!.item?.title);
    assertEquals('https://en.wikipedia.org', items[2]!.item?.title);
    assertEquals('https://en.wikipedia.org', items[3]!.item?.title);
    assertEquals('https://www.google.com', items[4]!.item?.title);
  });

  test('DeleteViaMenuButton', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[1]!.$.checkbox.click();
    items[3]!.$.checkbox.click();
    await microtasksFinished();

    items[1]!.$['menu-button'].click();
    await microtasksFinished();

    testService.handler.setResultFor(
        'removeVisits', Promise.resolve([TEST_HISTORY_RESULTS[1]]));

    element.$.sharedMenu.get();
    element.shadowRoot.querySelector<HTMLElement>('#menuRemoveButton')!.click();
    await microtasksFinished();

    const visits = await testService.handler.whenCalled('removeVisits');
    assertEquals(1, visits.length);
    assertEquals(TEST_HISTORY_RESULTS[1]!.url, visits[0]!.url);
    assertEquals(
        TEST_HISTORY_RESULTS[1]!.allTimestamps[0], visits[0]!.timestamps[0]);
    assertDeepEquals(
        [
          'https://www.google.com',
          'https://www.google.com',
          'https://en.wikipedia.org',
        ],
        getHistoryData().map(item => item.title));

    // Deletion should deselect all.
    assertDeepEquals(
        [false, false, false],
        Array.from(items).slice(0, 3).map(i => i.selected));
  });

  test('DeleteDisabledWhilePending', async function() {
    let items: NodeListOf<HistoryItemElement>;
    await finishSetup(TEST_HISTORY_RESULTS);

    const delayedRemove = new PromiseResolver();
    testService.handler.setResultFor('removeVisits', delayedRemove.promise);

    element.$.infiniteList.fillCurrentViewport();
    await microtasksFinished();

    items = element.shadowRoot.querySelectorAll('history-item');
    assertEquals(TEST_HISTORY_RESULTS.length, items.length);

    // Select 2 items.
    items[1]!.$.checkbox.click();
    items[2]!.$.checkbox.click();
    await microtasksFinished();

    // Delete one of the items using its own remove button.
    items[1]!.$['menu-button'].click();
    element.$.sharedMenu.get();
    element.shadowRoot.querySelector<HTMLElement>('#menuRemoveButton')!.click();
    await microtasksFinished();

    const visits = await testService.handler.whenCalled('removeVisits');
    assertEquals(1, visits.length);
    assertEquals(TEST_HISTORY_RESULTS[1]!.url, visits[0]!.url);
    assertEquals(
        TEST_HISTORY_RESULTS[1]!.allTimestamps[0], visits[0]!.timestamps[0]);

    // Deletion is still happening. Verify that menu button and toolbar
    // are disabled.
    assertTrue(
        element.shadowRoot
            .querySelector<HTMLButtonElement>('#menuRemoveButton')!.disabled);
    assertEquals(2, toolbar.count);
    assertTrue(toolbar.shadowRoot.querySelector('cr-toolbar-selection-overlay')!
                   .querySelector('cr-button')!.disabled);

    // Key event should be ignored.
    assertEquals(1, testService.handler.getCallCount('removeVisits'));
    pressAndReleaseKeyOn(document.body, 46, [], 'Delete');
    await microtasksFinished();
    assertEquals(1, testService.handler.getCallCount('removeVisits'));

    delayedRemove.resolve({});
    await microtasksFinished();

    // Reselect some items.
    items = element.shadowRoot.querySelectorAll('history-item');
    assertEquals(TEST_HISTORY_RESULTS.length - 1, items.length);
    items[1]!.$.checkbox.click();
    items[2]!.$.checkbox.click();
    await microtasksFinished();

    // Check that delete option is re-enabled.
    assertEquals(2, toolbar.count);
    assertFalse(
        toolbar.shadowRoot.querySelector('cr-toolbar-selection-overlay')!
            .querySelector('cr-button')!.disabled);

    // Menu button should also be re-enabled.
    items[1]!.$['menu-button'].click();
    element.$.sharedMenu.get();
    assertFalse(
        element.shadowRoot
            .querySelector<HTMLButtonElement>('#menuRemoveButton')!.disabled);
  });

  test('DeletingItemsUsingShortcuts', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    const dialog = element.$.dialog.get();
    const items = element.shadowRoot.querySelectorAll('history-item');

    // Dialog should not appear when there is no item selected.
    pressAndReleaseKeyOn(document.body, 46, [], 'Delete');
    await microtasksFinished();
    assertFalse(dialog.open);

    items[1]!.$.checkbox.click();
    items[2]!.$.checkbox.click();

    await Promise.all([
      items[1]!.$.checkbox.updateComplete,
      items[2]!.$.checkbox.updateComplete,
    ]);

    assertEquals(2, toolbar.count);
    testService.handler.setResultFor(
        'removeVisits',
        Promise.resolve([TEST_HISTORY_RESULTS[1], TEST_HISTORY_RESULTS[2]]));
    pressAndReleaseKeyOn(document.body, 46, [], 'Delete');
    await microtasksFinished();
    assertTrue(dialog.open);
    element.shadowRoot.querySelector<HTMLElement>('.cancel-button')!.click();
    assertFalse(dialog.open);

    pressAndReleaseKeyOn(document.body, 8, [], 'Backspace');
    await microtasksFinished();
    assertTrue(dialog.open);
    element.shadowRoot.querySelector<HTMLElement>('.action-button')!.click();
    const toRemove = await testService.handler.whenCalled('removeVisits');
    assertEquals('https://www.example.com', toRemove[0].url);
    assertEquals('https://www.google.com', toRemove[1].url);
    assertEquals(Date.parse('2016-03-14 10:00 UTC'), toRemove[0].timestamps[0]);
    assertEquals(Date.parse('2016-03-14 9:00 UTC'), toRemove[1].timestamps[0]);
  });

  test('DeleteDialogClosedOnBackNavigation', async function() {
    // Ensure that state changes are always mirrored to the URL.
    await finishSetup([]);
    testService.handler.resetResolver('queryHistory');
    CrRouter.getInstance().setDwellTime(0);

    testService.handler.setResultFor('queryHistory', Promise.resolve({
      results: {
        info: createHistoryInfo('something else'),
        value: TEST_HISTORY_RESULTS,
      },
    }));

    // Navigate from chrome://history/ to
    // chrome://history/?q=something else.
    app.dispatchEvent(new CustomEvent('change-query', {
      bubbles: true,
      composed: true,
      detail: {search: 'something else'},
    }));
    await testService.handler.whenCalled('queryHistory');
    testService.handler.resetResolver('queryHistory');
    testService.handler.setResultFor(
        'queryHistoryContinuation', Promise.resolve({
          results: {
            info: createHistoryInfo('something else'),
            value: ADDITIONAL_RESULTS,
          },
        }));
    element.dispatchEvent(new CustomEvent(
        'query-history', {bubbles: true, composed: true, detail: true}));
    await testService.handler.whenCalled('queryHistoryContinuation');
    await eventToPromise('items-rendered', element.$.infiniteList);
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[2]!.$.checkbox.click();
    await microtasksFinished();
    toolbar.deleteSelectedItems();
    await microtasksFinished();
    // Confirmation dialog should appear.
    assertTrue(element.$.dialog.getIfExists()!.open);
    // Navigate back to chrome://history.
    testService.handler.setResultFor('queryHistory', Promise.resolve({
      results: {
        info: createHistoryInfo('something else'),
        value: TEST_HISTORY_RESULTS,
      },
    }));
    window.history.back();

    await waitForEvent(window, 'popstate');
    await microtasksFinished();
    assertFalse(element.$.dialog.getIfExists()!.open);
  });

  test('ClickingFileUrlSendsMessageToChrome', async function() {
    const fileURL = 'file:///home/myfile';
    await finishSetup([createHistoryEntry('2016-03-15', fileURL)]);
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[0]!.$.link.click();
    const url = await testService.whenCalled('navigateToUrl');
    assertEquals(fileURL, url);
  });

  test('DeleteHistoryResultsInQueryHistoryEvent', async function() {
    await finishSetup(TEST_HISTORY_RESULTS);
    testService.handler.resetResolver('queryHistory');
    webUIListenerCallback('history-deleted');
    const items = element.shadowRoot.querySelectorAll('history-item');
    items[2]!.$.checkbox.click();
    items[3]!.$.checkbox.click();
    await microtasksFinished();

    testService.handler.resetResolver('queryHistory');
    webUIListenerCallback('history-deleted');
    await flushTasks();
    assertEquals(0, testService.handler.getCallCount('queryHistory'));
  });

  test('SetsScrollTarget', async () => {
    await finishSetup(TEST_HISTORY_RESULTS);
    assertEquals(app.scrollTarget, element.$.infiniteList.scrollTarget);
  });

  test('SetsScrollOffset', async () => {
    await finishSetup(TEST_HISTORY_RESULTS);
    element.scrollOffset = 123;
    await microtasksFinished();
    assertEquals(123, element.$.infiniteList.scrollOffset);
  });

  test('AnnouncesExactMatches', async () => {
    await finishSetup([]);
    async function getMessagesForResults(
        term: string, results: HistoryEntry[]) {
      const a11yMessagesEventPromise =
          eventToPromise('cr-a11y-announcer-messages-sent', document.body);
      element.queryState.incremental = false;
      element.historyResult({finished: true, term}, results);
      return (await a11yMessagesEventPromise).detail.messages[0];
    }

    let singleResultMessage =
        await getMessagesForResults('some query', [TEST_HISTORY_RESULTS[0]!]);
    assertEquals(`Found 1 search result for 'some query'`, singleResultMessage);

    let multipleResultsMessage =
        await getMessagesForResults('new query', TEST_HISTORY_RESULTS);
    assertEquals(
        `Found 4 search results for 'new query'`, multipleResultsMessage);

    loadTimeData.overrideValues({enableHistoryEmbeddings: true});
    singleResultMessage =
        await getMessagesForResults('some query', [TEST_HISTORY_RESULTS[0]!]);
    assertEquals(`Found 1 exact match for 'some query'`, singleResultMessage);

    multipleResultsMessage =
        await getMessagesForResults('new query', TEST_HISTORY_RESULTS);
    assertEquals(
        `Found 4 exact matches for 'new query'`, multipleResultsMessage);
  });

  test('ScrollingLoadsMore', async () => {
    // Simulate a shorter window to make this easier.
    document.body.style.maxHeight = '300px';
    document.body.style.height = '300px';
    await finishSetup([], /*finished=*/ false);
    assertTrue(!!app.scrollTarget);

    // Add enough items to allow at least 600px of scrolling under the view.
    const itemSize = 36;
    const heightNeededToScroll = app.scrollTarget.offsetHeight + 600;
    const itemsNeeded = Math.ceil(heightNeededToScroll / itemSize);

    const results = [];
    for (let i = 0; i < itemsNeeded; i++) {
      results.push(createHistoryEntry('2016-03-15', 'https://www.google.com'));
    }
    await finishSetup(results, /*finished=*/ false);
    testService.handler.reset();
    // Make scroll debounce shorter to shorten some wait times below.
    element.setScrollDebounceForTest(1);

    // This check ensures the line below actually scrolls.
    assertGT(
        app.scrollTarget.scrollHeight, app.scrollTarget.offsetHeight + 500);

    // Scroll to just under the threshold to make sure more results don't load.
    app.scrollTarget.scrollTop =
        app.scrollTarget.scrollHeight - app.scrollTarget.offsetHeight - 500;
    // Wait for the scroll observer to trigger.
    await eventToPromise('scroll-timeout-for-test', element);
    assertEquals(
        0, testService.handler.getCallCount('queryHistoryContinuation'));

    // Set up more results.
    testService.handler.setResultFor(
        'queryHistoryContinuation', Promise.resolve({
          results: {
            info: {finished: false, term: ''},
            value: [
              createHistoryEntry(
                  '2013-02-13 10:00', 'https://en.wikipedia.org'),
              createHistoryEntry('2013-02-13 9:50', 'https://www.youtube.com'),
              createHistoryEntry('2013-02-11', 'https://www.google.com'),
              createHistoryEntry('2013-02-10', 'https://www.example.com'),
            ],
          },
        }));

    // Scroll to within 500px of the scroll height. More results should be
    // requested.
    app.scrollTarget.scrollTop =
        app.scrollTarget.scrollHeight - app.scrollTarget.offsetHeight - 400;
    await testService.handler.whenCalled('queryHistoryContinuation');
    await microtasksFinished();
    assertEquals(
        1, testService.handler.getCallCount('queryHistoryContinuation'));
    testService.handler.reset();

    // Should not respond to scroll when inactive.
    element.isActive = false;
    // This check ensures the line below actually scrolls.
    assertGT(
        app.scrollTarget.scrollHeight, app.scrollTarget.offsetHeight + 500);
    app.scrollTarget.scrollTop =
        app.scrollTarget.scrollHeight - app.scrollTarget.offsetHeight - 400;
    // Wait longer than scroll debounce.
    await new Promise(resolve => setTimeout(resolve, 10));
    assertEquals(
        0, testService.handler.getCallCount('queryHistoryContinuation'));
  });

  test('ResizingLoadsMore', async () => {
    // Simulate a shorter window to make this easier.
    document.body.style.maxHeight = '300px';
    document.body.style.height = '300px';
    await finishSetup(TEST_HISTORY_RESULTS, /*finished=*/ false);
    testService.handler.reset();

    // Set up more results.
    testService.handler.setResultFor(
        'queryHistoryContinuation', Promise.resolve({
          results: {
            info: {finished: false, term: ''},
            value: [
              createHistoryEntry(
                  '2013-02-13 10:00', 'https://en.wikipedia.org'),
            ],
          },
        }));


    // Simulate resizing the window. More results should be loaded.
    document.body.style.maxHeight = '800px';
    document.body.style.height = '800px';
    await testService.handler.whenCalled('queryHistoryContinuation');
    assertEquals(
        1, testService.handler.getCallCount('queryHistoryContinuation'));
  });

  teardown(function() {
    app.dispatchEvent(new CustomEvent(
        'change-query', {bubbles: true, composed: true, detail: {search: ''}}));
  });
});
