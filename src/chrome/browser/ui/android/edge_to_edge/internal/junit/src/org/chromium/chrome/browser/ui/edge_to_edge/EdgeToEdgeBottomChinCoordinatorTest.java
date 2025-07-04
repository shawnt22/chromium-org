// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package org.chromium.chrome.browser.ui.edge_to_edge;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.verify;

import android.view.View;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.browser_controls.BottomControlsStacker;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.layouts.LayoutManager;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.insets.InsetObserver;

@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class EdgeToEdgeBottomChinCoordinatorTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private View mView;
    @Mock private KeyboardVisibilityDelegate mKeyboardVisibilityDelegate;
    @Mock private InsetObserver mInsetObserver;
    @Mock private LayoutManager mLayoutManager;
    @Mock private EdgeToEdgeController mEdgeToEdgeController;
    @Mock private BottomControlsStacker mBottomControlsStacker;
    @Mock private EdgeToEdgeBottomChinSceneLayer mEdgeToEdgeBottomChinSceneLayer;
    @Mock private FullscreenManager mFullscreenManager;

    @Test
    public void testEdgeToEdgeBottomChinCoordinator() {
        EdgeToEdgeBottomChinCoordinator coordinator =
                new EdgeToEdgeBottomChinCoordinator(
                        mView,
                        mKeyboardVisibilityDelegate,
                        mInsetObserver,
                        mLayoutManager,
                        mEdgeToEdgeController,
                        mBottomControlsStacker,
                        mEdgeToEdgeBottomChinSceneLayer,
                        mFullscreenManager,
                        false);
        verify(mLayoutManager).addSceneOverlay(any());

        coordinator.destroy();
        verify(mEdgeToEdgeBottomChinSceneLayer).destroy();
    }
}
