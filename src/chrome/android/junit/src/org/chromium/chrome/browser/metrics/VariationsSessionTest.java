// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.metrics;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.Callback;
import org.chromium.base.test.BaseRobolectricTestRunner;

/** Tests for VariationsSession */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class VariationsSessionTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private VariationsSession.Natives mVariationsSessionJniMock;

    private TestVariationsSession mSession;

    private static class TestVariationsSession extends VariationsSession {
        private Callback<String> mCallback;

        @Override
        protected void getRestrictMode(Callback<String> callback) {
            mCallback = callback;
        }

        public void runCallback(String value) {
            mCallback.onResult(value);
        }
    }

    @Before
    public void setUp() {
        VariationsSessionJni.setInstanceForTesting(mVariationsSessionJniMock);
        mSession = new TestVariationsSession();
    }

    @Test
    public void testStart() {
        mSession.start();
        verify(mVariationsSessionJniMock, never())
                .startVariationsSession(eq(mSession), any(String.class));

        String restrictValue = "test";
        mSession.runCallback(restrictValue);
        verify(mVariationsSessionJniMock, times(1)).startVariationsSession(mSession, restrictValue);
    }

    @Test
    public void testGetRestrictModeValue() {
        mSession.getRestrictModeValue(
                new Callback<>() {
                    @Override
                    public void onResult(String restrictMode) {}
                });
        String restrictValue = "test";
        mSession.runCallback(restrictValue);
        verify(mVariationsSessionJniMock, never())
                .startVariationsSession(eq(mSession), any(String.class));

        mSession.start();
        verify(mVariationsSessionJniMock, times(1)).startVariationsSession(mSession, restrictValue);
    }
}
