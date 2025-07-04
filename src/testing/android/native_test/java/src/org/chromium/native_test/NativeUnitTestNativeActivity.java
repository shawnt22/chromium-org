// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.native_test;

import android.app.NativeActivity;
import android.os.Bundle;

import org.chromium.build.annotations.Nullable;

/** A {@link android.app.NativeActivity} for running native unit tests. (i.e., not browser tests) */
public class NativeUnitTestNativeActivity extends NativeActivity {
    private final NativeUnitTest mTest = new NativeUnitTest();

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        mTest.preCreate(this);
        super.onCreate(savedInstanceState);
        mTest.postCreate(this);
    }

    @Override
    public void onStart() {
        super.onStart();
        // Force running in sub thread, since NativeActivity processes Looper messages in native
        // code, which makes invoking the test runner Handler problematic.
        mTest.postStart(this, true);
    }
}
