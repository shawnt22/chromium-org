// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.native_test;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;

import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.base.Log;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.build.gtest_apk.NativeTestIntent;
import org.chromium.test.reporter.TestStatusReporter;

import java.io.File;

/** Helper to run tests inside Activity or NativeActivity. */
@JNINamespace("testing::android")
public class NativeTest {
    private static final String TAG = "NativeTest";

    private String mCommandLineFilePath;
    private final StringBuilder mCommandLineFlags = new StringBuilder();
    private TestStatusReporter mReporter;
    private boolean mRunInSubThread;
    private boolean mKeepUserDataDir;
    private String mStdoutFilePath;

    private static class ReportingUncaughtExceptionHandler
            implements Thread.UncaughtExceptionHandler {

        private final TestStatusReporter mReporter;
        private final Thread.UncaughtExceptionHandler mWrappedHandler;

        public ReportingUncaughtExceptionHandler(
                TestStatusReporter reporter, Thread.UncaughtExceptionHandler wrappedHandler) {
            mReporter = reporter;
            mWrappedHandler = wrappedHandler;
        }

        @Override
        public void uncaughtException(Thread thread, Throwable ex) {
            mReporter.uncaughtException(Process.myPid(), ex);
            if (mWrappedHandler != null) mWrappedHandler.uncaughtException(thread, ex);
        }
    }

    public void preCreate(Activity activity) {
        String coverageDeviceFile =
                activity.getIntent().getStringExtra(NativeTestIntent.EXTRA_COVERAGE_DEVICE_FILE);
        if (coverageDeviceFile != null) {
            try {
                Os.setenv("LLVM_PROFILE_FILE", coverageDeviceFile, true);
            } catch (ErrnoException e) {
                Log.w(TAG, "failed to set LLVM_PROFILE_FILE", e);
            }
        }
        // Set TMPDIR to make perfetto_unittests not to use /data/local/tmp as a tmp directory.
        try {
            Os.setenv("TMPDIR", activity.getApplicationContext().getCacheDir().getPath(), false);
        } catch (ErrnoException e) {
            Log.w(TAG, "failed to set TMPDIR", e);
        }
    }

    public void postCreate(Activity activity) {
        parseArgumentsFromIntent(activity, activity.getIntent());
        mReporter = new TestStatusReporter(activity);
        mReporter.testRunStarted(Process.myPid());
        Thread.setDefaultUncaughtExceptionHandler(
                new ReportingUncaughtExceptionHandler(
                        mReporter, Thread.getDefaultUncaughtExceptionHandler()));
    }

    public boolean shouldKeepUserDataDir() {
        return mKeepUserDataDir;
    }

    private void parseArgumentsFromIntent(Activity activity, Intent intent) {
        Log.i(TAG, "Extras:");
        Bundle extras = intent.getExtras();
        if (extras != null) {
            for (String s : extras.keySet()) {
                Log.i(TAG, "  %s", s);
            }
        }

        mCommandLineFilePath = intent.getStringExtra(NativeTestIntent.EXTRA_COMMAND_LINE_FILE);
        if (mCommandLineFilePath == null) {
            mCommandLineFilePath = "";
        } else {
            File commandLineFile = new File(mCommandLineFilePath);
            if (!commandLineFile.isAbsolute()) {
                mCommandLineFilePath =
                        Environment.getExternalStorageDirectory() + "/" + mCommandLineFilePath;
            }
            Log.i(TAG, "command line file path: %s", mCommandLineFilePath);
        }

        String commandLineFlags = intent.getStringExtra(NativeTestIntent.EXTRA_COMMAND_LINE_FLAGS);
        if (commandLineFlags != null) mCommandLineFlags.append(commandLineFlags);

        mRunInSubThread = intent.hasExtra(NativeTestIntent.EXTRA_RUN_IN_SUB_THREAD);

        String gtestFilter = intent.getStringExtra(NativeTestIntent.EXTRA_GTEST_FILTER);
        if (gtestFilter != null) {
            appendCommandLineFlags("--gtest_filter=" + gtestFilter);
            Log.i(TAG, "Tests from gtest_filter: %s", gtestFilter);
        }

        mKeepUserDataDir = intent.getBooleanExtra(NativeTestIntent.EXTRA_KEEP_USER_DATA_DIR, false);
        if (mKeepUserDataDir) {
            Log.i(TAG, "user data dir is kept for the tests.");
        }

        mStdoutFilePath = intent.getStringExtra(NativeTestIntent.EXTRA_STDOUT_FILE);
    }

    public void appendCommandLineFlags(String flags) {
        mCommandLineFlags.append(" ").append(flags);
    }

    public void postStart(final Activity activity, boolean forceRunInSubThread) {
        final Runnable runTestsTask =
                new Runnable() {
                    @Override
                    public void run() {
                        runTests(activity);
                    }
                };

        if (mRunInSubThread || forceRunInSubThread) {
            // Post a task that posts a task that creates a new thread and runs tests on it.

            // On L and M, the system posts a task to the main thread that prints to stdout
            // from android::Layout (https://goo.gl/vZA38p). Chaining the subthread creation
            // through multiple tasks executed on the main thread ensures that this task
            // runs before we start running tests s.t. its output doesn't interfere with
            // the test output. See crbug.com/678146 for additional context.

            final Handler handler = new Handler();
            final Runnable startTestThreadTask =
                    new Runnable() {
                        @Override
                        public void run() {
                            new Thread(runTestsTask).start();
                        }
                    };
            final Runnable postTestStarterTask =
                    new Runnable() {
                        @Override
                        public void run() {
                            handler.post(startTestThreadTask);
                        }
                    };
            handler.post(postTestStarterTask);
        } else {
            // Post a task to run the tests. This allows us to not block
            // onCreate and still run tests on the main thread.
            new Handler().post(runTestsTask);
        }
    }

    private void runTests(Activity activity) {
        NativeTestJni.get()
                .runTests(
                        mCommandLineFlags.toString(),
                        mCommandLineFilePath,
                        mStdoutFilePath,
                        activity.getApplicationContext(),
                        UrlUtils.getIsolatedTestRoot());
        activity.finish();
        mReporter.testRunFinished(Process.myPid());
    }

    @NativeMethods
    interface Natives {
        void runTests(
                String commandLineFlags,
                String commandLineFilePath,
                String stdoutFilePath,
                Context appContext,
                String testDataDir);
    }
}
