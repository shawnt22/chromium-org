// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {SourcesTestRunner} from 'sources_test_runner';
import {BindingsTestRunner} from 'bindings_test_runner';

import * as SourcesComponents from 'devtools/panels/sources/components/components.js';
import * as Breakpoints from 'devtools/models/breakpoints/breakpoints.js';
import * as RenderCoordinator from 'devtools/ui/components/render_coordinator/render_coordinator.js';

import * as Workspace from 'devtools/models/workspace/workspace.js';

(async function() {
  TestRunner.addResult(`Verify that breakpoints are moved appropriately in case of page reload.\n`);
  await TestRunner.showPanel('sources');
  await TestRunner.evaluateInPagePromise(`
      function addFooJS() {
          var script = document.createElement('script');
          script.src = '${TestRunner.url("./resources/foo.js")}';
          document.body.appendChild(script);
      }
  `);

  var testMapping = BindingsTestRunner.initializeTestMapping();
  var fs = new BindingsTestRunner.TestFileSystem('/var/www');
  BindingsTestRunner.addFooJSFile(fs);

  TestRunner.runTestSuite([
    function addFileSystem(next) {
      fs.reportCreated(next);
    },

    function addNetworkFooJS(next) {
      TestRunner.evaluateInPage('addFooJS()');
      testMapping.addBinding('foo.js');
      BindingsTestRunner.waitForBinding('foo.js').then(next);
    },

    function setBreakpointInFileSystemUISourceCode(next) {
      TestRunner.waitForUISourceCode('foo.js', Workspace.Workspace.projectTypes.FileSystem)
          .then(sourceCode => SourcesTestRunner.showUISourceCodePromise(sourceCode))
          .then(onSourceFrame);

      async function onSourceFrame(sourceFrame) {
        await SourcesTestRunner.setBreakpoint(sourceFrame, 0, '', true);
        // Explicitly request an update to reflect the up-to-date breakpoint list.
        await SourcesComponents.BreakpointsView.BreakpointsSidebarController.instance().update();
        await RenderCoordinator.done();
        dumpBreakpointSidebarPane();
        next();
      }
    },

    async function reloadPageAndDumpBreakpoints(next) {
      const onBreakpointSet = async () => {
        // Explicitly request an update to reflect the up-to-date breakpoint list.
        await SourcesComponents.BreakpointsView.BreakpointsSidebarController.instance().update();
        await RenderCoordinator.done();
        dumpBreakpointSidebarPane();
        next();
      }
      // Wait until the move from network => filesystem happens via
      // `setBreakpoint`, before dumping the breakpoint sidebar pane.
      TestRunner.addSniffer(Breakpoints.BreakpointManager.BreakpointManager.prototype, 'setBreakpoint', onBreakpointSet, true);
      await testMapping.removeBinding('foo.js');
      await TestRunner.reloadPagePromise();
      await TestRunner.waitForUISourceCode('foo.js', Workspace.Workspace.projectTypes.FileSystem);
      testMapping.addBinding('foo.js');
    }
  ]);

  function dumpBreakpointSidebarPane() {
    var pane = SourcesComponents.BreakpointsView.BreakpointsView.instance();
    const location = pane.shadowRoot?.querySelector('.breakpoint-item .location')?.textContent;
    const groupHeader = pane.shadowRoot?.querySelector('.group-header-title');
    TestRunner.addResult(`${groupHeader?.title}:${location}`);
  }
})();
