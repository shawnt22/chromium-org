// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import org.chromium.base.AndroidInfo;
import org.chromium.base.ApkInfo;
import org.chromium.base.ContextUtils;
import org.chromium.base.DeviceInfo;
import org.chromium.base.Log;
import org.chromium.base.TraceEvent;
import org.chromium.base.library_loader.IRelroLibInfo;
import org.chromium.base.version_info.VersionConstants;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.build.annotations.RequiresNonNull;

import java.io.IOException;
import java.util.List;

/** This class is used to start a child process by connecting to a ChildProcessService. */
@NullMarked
public class ChildProcessLauncher {
    private static final String TAG = "ChildProcLauncher";

    /** Delegate that client should use to customize the process launching. */
    public abstract static class Delegate {
        /**
         * Called when the launcher is about to start. Gives the embedder a chance to provide an
         * already bound connection if it has one. (allowing for warm-up connections: connections
         * that are already bound in advance to speed up child process start-up time). Note that
         * onBeforeConnectionAllocated will not be called if this method returns a connection.
         *
         * @param connectionAllocator the allocator the returned connection should have been
         *     allocated of.
         * @param serviceCallback the service callback that the connection should use.
         * @return a bound connection to use to connect to the child process service, or null if a
         *     connection should be allocated and bound by the launcher.
         */
        public @Nullable ChildProcessConnection getBoundConnection(
                ChildConnectionAllocator connectionAllocator,
                ChildProcessConnection.ServiceCallback serviceCallback) {
            return null;
        }

        /**
         * Called before a connection is allocated.
         * Note that this is only called if the ChildProcessLauncher is created with
         * {@link #createWithConnectionAllocator}.
         * @param serviceBundle the bundle passed in the service intent. Clients can add their own
         * extras to the bundle.
         */
        public void onBeforeConnectionAllocated(Bundle serviceBundle) {}

        /**
         * Called before setup is called on the connection.
         *
         * @param childProcessArgs the aidl parcelable passed to the {@link ChildProcessService} in
         *     the setup call.
         */
        public void onBeforeConnectionSetup(IChildProcessArgs childProcessArgs) {}

        /**
         * Called when the connection was successfully established, meaning the setup call on the
         * service was successful.
         *
         * @param connection the connection over which the setup call was made.
         */
        public void onConnectionEstablished(ChildProcessConnection connection) {}

        /**
         * Called as part of establishing the connection. Saves the bundle for transferring to other
         * processes that did not inherit from the App Zygote.
         *
         * @param connection the new connection
         * @param relroInfo the IRelroLibInfo potentially containing useful information for
         *     relocation sharing across processes.
         */
        public void onReceivedZygoteInfo(
                ChildProcessConnection connection, @Nullable IRelroLibInfo relroInfo) {}

        /**
         * Called when a connection has been disconnected. Only invoked if onConnectionEstablished
         * was called, meaning the connection was already established.
         *
         * @param connection the connection that got disconnected.
         */
        public void onConnectionLost(ChildProcessConnection connection) {}
    }

    // Represents an invalid process handle; same as base/process/process.h kNullProcessHandle.
    private static final int NULL_PROCESS_HANDLE = 0;

    // The handle for the thread we were created on and on which all methods should be called.
    private final Handler mLauncherHandler;

    private final Delegate mDelegate;

    private final String[] mCommandLine;
    private final IFileDescriptorInfo[] mFilesToBeMapped;

    // The allocator used to create the connection.
    private final ChildConnectionAllocator mConnectionAllocator;

    // The IBinder interfaces provided to the created service.
    private final @Nullable List<IBinder> mClientInterfaces;

    // A binder box which can be used by the child to unpack additional binders.
    private final @Nullable IBinder mBinderBox;

    // The actual service connection. Set once we have connected to the service. Volatile as it is
    // accessed from threads other than the Launcher thread.
    private volatile @Nullable ChildProcessConnection mConnection;

    /**
     * Constructor.
     *
     * @param launcherHandler the handler for the thread where all operations should happen.
     * @param delegate the delegate that gets notified of the launch progress.
     * @param commandLine the command line that should be passed to the started process.
     * @param filesToBeMapped the files that should be passed to the started process.
     * @param connectionAllocator the allocator used to create connections to the service.
     * @param clientInterfaces the interfaces that should be passed to the started process so it can
     *     communicate with the parent process.
     * @param binderBox an optional binder box the child can use to unpack additional binders
     */
    public ChildProcessLauncher(
            Handler launcherHandler,
            Delegate delegate,
            String[] commandLine,
            IFileDescriptorInfo[] filesToBeMapped,
            ChildConnectionAllocator connectionAllocator,
            @Nullable List<IBinder> clientInterfaces,
            @Nullable IBinder binderBox) {
        assert connectionAllocator != null;
        mLauncherHandler = launcherHandler;
        isRunningOnLauncherThread();
        mCommandLine = commandLine;
        mConnectionAllocator = connectionAllocator;
        mDelegate = delegate;
        mFilesToBeMapped = filesToBeMapped;
        mClientInterfaces = clientInterfaces;
        mBinderBox = binderBox;
    }

    /**
     * Starts the child process and calls setup on it if {@param setupConnection} is true.
     *
     * @param setupConnection whether the setup should be performed on the connection once
     *     established
     * @param queueIfNoFreeConnection whether to queue that request if no service connection is
     *     available. If the launcher was created with a connection provider, this parameter has no
     *     effect.
     * @return true if the connection was started or was queued.
     */
    public boolean start(final boolean setupConnection, final boolean queueIfNoFreeConnection) {
        assert isRunningOnLauncherThread();
        try {
            TraceEvent.begin("ChildProcessLauncher.start");
            ChildProcessConnection.ServiceCallback serviceCallback =
                    new ChildProcessConnection.ServiceCallback() {
                        @Override
                        public void onChildStarted() {}

                        @Override
                        public void onChildStartFailed(ChildProcessConnection connection) {
                            assert isRunningOnLauncherThread();
                            assert mConnection == connection;
                            Log.e(TAG, "ChildProcessConnection.start failed, trying again");
                            mLauncherHandler.post(
                                    new Runnable() {
                                        @Override
                                        public void run() {
                                            // The child process may already be bound to another
                                            // client (this can happen if multi-process WebView is
                                            // used in more than one process), so try starting the
                                            // process again.
                                            // This connection that failed to start has not been
                                            // freed, so a new bound connection will be allocated.
                                            mConnection = null;
                                            start(setupConnection, queueIfNoFreeConnection);
                                        }
                                    });
                        }

                        @Override
                        public void onChildProcessDied(ChildProcessConnection connection) {
                            assert isRunningOnLauncherThread();
                            assert mConnection == connection;
                            ChildProcessLauncher.this.onChildProcessDied();
                        }
                    };
            mConnection = mDelegate.getBoundConnection(mConnectionAllocator, serviceCallback);
            if (mConnection != null) {
                setupConnection();
                return true;
            }
            if (!allocateAndSetupConnection(
                            serviceCallback, setupConnection, queueIfNoFreeConnection)
                    && !queueIfNoFreeConnection) {
                return false;
            }
            return true;
        } finally {
            TraceEvent.end("ChildProcessLauncher.start");
        }
    }

    public @Nullable ChildProcessConnection getConnection() {
        return mConnection;
    }

    public ChildConnectionAllocator getConnectionAllocator() {
        return mConnectionAllocator;
    }

    private boolean allocateAndSetupConnection(
            final ChildProcessConnection.ServiceCallback serviceCallback,
            final boolean setupConnection,
            final boolean queueIfNoFreeConnection) {
        assert mConnection == null;
        Bundle serviceBundle = new Bundle();
        mDelegate.onBeforeConnectionAllocated(serviceBundle);

        mConnection =
                mConnectionAllocator.allocate(
                        ContextUtils.getApplicationContext(), serviceBundle, serviceCallback);
        if (mConnection == null) {
            if (!queueIfNoFreeConnection) {
                Log.d(TAG, "Failed to allocate a child connection (no queuing).");
                return false;
            }
            mConnectionAllocator.queueAllocation(
                    () ->
                            allocateAndSetupConnection(
                                    serviceCallback, setupConnection, queueIfNoFreeConnection));
            return false;
        }

        if (setupConnection) {
            setupConnection();
        }
        return true;
    }

    @RequiresNonNull("mConnection")
    private void setupConnection() {
        ChildProcessConnection.ZygoteInfoCallback zygoteInfoCallback =
                new ChildProcessConnection.ZygoteInfoCallback() {
                    @Override
                    public void onReceivedZygoteInfo(
                            ChildProcessConnection connection, @Nullable IRelroLibInfo relroInfo) {
                        mDelegate.onReceivedZygoteInfo(connection, relroInfo);
                    }
                };
        ChildProcessConnection.ConnectionCallback connectionCallback =
                new ChildProcessConnection.ConnectionCallback() {
                    @Override
                    public void onConnected(@Nullable ChildProcessConnection connection) {
                        onServiceConnected(connection);
                    }
                };
        IChildProcessArgs connectionArgs = createConnectionArgs();
        mDelegate.onBeforeConnectionSetup(connectionArgs);
        mConnection.setupConnection(
                connectionArgs,
                getClientInterfaces(),
                getBinderBox(),
                connectionCallback,
                zygoteInfoCallback);
    }

    private void onServiceConnected(@Nullable ChildProcessConnection connection) {
        ChildProcessConnection curConnection = mConnection;
        assert isRunningOnLauncherThread();
        assert curConnection != null;
        assert curConnection == connection || connection == null;

        Log.d(TAG, "on connect callback, pid=%d", curConnection.getPid());

        mDelegate.onConnectionEstablished(curConnection);

        // Proactively close the FDs rather than waiting for the GC to do it.
        try {
            for (IFileDescriptorInfo fileInfo : mFilesToBeMapped) {
                fileInfo.fd.close();
            }
        } catch (IOException ioe) {
            Log.w(TAG, "Failed to close FD.", ioe);
        }
    }

    public int getPid() {
        assert isRunningOnLauncherThread();
        ChildProcessConnection connection = mConnection;
        return connection == null ? NULL_PROCESS_HANDLE : connection.getPid();
    }

    public @Nullable List<IBinder> getClientInterfaces() {
        return mClientInterfaces;
    }

    public @Nullable IBinder getBinderBox() {
        return mBinderBox;
    }

    private boolean isRunningOnLauncherThread() {
        return mLauncherHandler.getLooper() == Looper.myLooper();
    }

    private IChildProcessArgs createConnectionArgs() {
        IChildProcessArgs args = new IChildProcessArgs();
        args.commandLine = mCommandLine;
        args.fileDescriptorInfos = mFilesToBeMapped;
        args.apkInfo = ApkInfo.getAidlInfo();
        args.androidInfo = AndroidInfo.getAidlInfo();
        args.deviceInfo = DeviceInfo.getAidlInfo();
        args.channel = VersionConstants.CHANNEL;
        return args;
    }

    private void onChildProcessDied() {
        if (getPid() != 0) {
            mDelegate.onConnectionLost(assumeNonNull(mConnection));
        }
    }

    public void stop() {
        assert isRunningOnLauncherThread();
        ChildProcessConnection connection = assumeNonNull(mConnection);
        Log.d(TAG, "stopping child connection: pid=%d", connection.getPid());
        connection.stop();
    }
}
