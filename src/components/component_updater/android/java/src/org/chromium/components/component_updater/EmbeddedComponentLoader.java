// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.component_updater;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.ResultReceiver;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TimeUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;

import java.util.Collection;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * ComponentLoader that is used in embedded WebViews/WebLayers. It implements a ServiceConnection to
 * connect to the provider service to fetch components files.
 */
@NullMarked
public class EmbeddedComponentLoader implements ServiceConnection {
    private static final String TAG = "EmbedComponentLoader";

    /**
     * WebView's ComponentsProviderService name that implements IComponentsProviderService.aidl
     * interface. Use this String in an intent to connect to the service to avoid dependency on the
     * service class itself.
     */
    public static final String AW_COMPONENTS_PROVIDER_SERVICE =
            "org.chromium.android_webview.services.ComponentsProviderService";

    private static final String KEY_RESULT = "RESULT";

    // Maintain a set of ComponentResultReceivers, remove a receiver once it gets a result back.
    // When a connection is established or restablished we request files from the service for the
    // components remaining in the set. Unbind the service when the set is empty, i.e all results
    // are received.
    // Must be only accessed on the UI thread.
    private final Set<ComponentResultReceiver> mComponentsResultReceivers = new HashSet<>();

    // Timer started in connect() to measure component receipt time.
    // Must be only accessed on the UI thread.
    private long mConnectTimeMillisForMetrics;

    public EmbeddedComponentLoader(Collection<ComponentLoaderPolicyBridge> componentLoaderPolicy) {
        ThreadUtils.assertOnUiThread();

        for (ComponentLoaderPolicyBridge policy : componentLoaderPolicy) {
            mComponentsResultReceivers.add(new ComponentResultReceiver(policy));
        }
    }

    private class ComponentResultReceiver extends ResultReceiver {
        private final ComponentLoaderPolicyBridge mComponent;

        public ComponentResultReceiver(ComponentLoaderPolicyBridge component) {
            super(ThreadUtils.getUiThreadHandler());
            mComponent = component;
        }

        @Override
        protected void onReceiveResult(int resultCode, Bundle resultData) {
            ThreadUtils.assertOnUiThread();

            // Already removed, i.e has already received the result.
            if (!mComponentsResultReceivers.remove(this)) {
                return;
            }
            // Only unbind when all results are received because it's a bound service and if we
            // unbind the connection before getting all results back, the service might be
            // killed before sending all results.
            if (mComponentsResultReceivers.isEmpty()) {
                RecordHistogram.recordTimesHistogram(
                        "Android.WebView.ComponentUpdater.ResultsReceived",
                        TimeUtils.elapsedRealtimeMillis() - mConnectTimeMillisForMetrics);
                ContextUtils.getApplicationContext().unbindService(EmbeddedComponentLoader.this);
            }

            if (resultCode != 0) {
                mComponent.componentLoadFailed(
                        ComponentLoadResult.COMPONENTS_PROVIDER_SERVICE_ERROR);
                return;
            }
            Map<String, ParcelFileDescriptor> resultMap =
                    (Map<String, ParcelFileDescriptor>) resultData.getSerializable(KEY_RESULT);
            if (resultMap == null) {
                mComponent.componentLoadFailed(
                        ComponentLoadResult.COMPONENTS_PROVIDER_SERVICE_ERROR);
                return;
            }
            mComponent.componentLoaded(resultMap);
        }

        public ComponentLoaderPolicyBridge getComponentLoaderPolicy() {
            return mComponent;
        }
    }

    @Override
    public void onServiceConnected(ComponentName className, IBinder service) {
        PostTask.runOrPostTask(
                TaskTraits.UI_DEFAULT,
                () -> {
                    try {
                        IComponentsProviderService providerService =
                                IComponentsProviderService.Stub.asInterface(service);
                        for (ComponentResultReceiver receiver : mComponentsResultReceivers) {
                            String componentId =
                                    receiver.getComponentLoaderPolicy().getComponentId();
                            // These binder calls are oneway, so they will not block the UI thread.
                            providerService.getFilesForComponent(componentId, receiver);
                        }
                    } catch (RemoteException e) {
                        Log.d(TAG, "Remote Exception calling ComponentProviderService", e);
                        if (!mComponentsResultReceivers.isEmpty()) {
                            // Clearing up receivers here to avoid unbinding multiple times in the
                            // future.
                            // This means if some receivers get their result after this step, their
                            // results will be ignored.
                            for (ComponentResultReceiver receiver : mComponentsResultReceivers) {
                                receiver.getComponentLoaderPolicy()
                                        .componentLoadFailed(ComponentLoadResult.REMOTE_EXCEPTION);
                            }
                            mComponentsResultReceivers.clear();
                            ContextUtils.getApplicationContext().unbindService(this);
                        }
                    }
                });
    }

    @Override
    public void onServiceDisconnected(ComponentName className) {}

    /**
     * Bind to the provider service with the given {@code intent} and load components.
     *
     * <p>Only connect to the service if there are registered components when the class is created.
     * Must be called once.
     *
     * @param intent Intent to connect to the service.
     * @param background If true, service binding will be offloaded to thread pool to avoid
     *     blocking.
     */
    public void connect(Intent intent, boolean background) {
        ThreadUtils.assertOnUiThread();

        if (mComponentsResultReceivers.isEmpty()) {
            return;
        }

        mConnectTimeMillisForMetrics = TimeUtils.elapsedRealtimeMillis();

        final TimeUtils.ElapsedRealtimeNanosTimer timer = new TimeUtils.ElapsedRealtimeNanosTimer();
        if (background) {
            PostTask.postTask(TaskTraits.BEST_EFFORT_MAY_BLOCK, () -> doConnect(intent));
        } else {
            doConnect(intent);
        }
        RecordHistogram.recordMicroTimesHistogram(
                "Android.WebView.ComponentUpdater.ProviderConnectUiTime", timer.getElapsedMicros());
    }

    // May be called from any thread.
    private void doConnect(Intent intent) {
        final Context appContext = ContextUtils.getApplicationContext();

        final TimeUtils.ElapsedRealtimeNanosTimer timer = new TimeUtils.ElapsedRealtimeNanosTimer();
        boolean success = appContext.bindService(intent, this, Context.BIND_AUTO_CREATE);
        final long micros = timer.getElapsedMicros();

        if (success) {
            RecordHistogram.recordMicroTimesHistogram(
                    "Android.WebView.ComponentUpdater.BindServiceTime.Success", micros);
        } else {
            RecordHistogram.recordMicroTimesHistogram(
                    "Android.WebView.ComponentUpdater.BindServiceTime.Failure", micros);
            Log.d(TAG, "Could not bind to " + intent);
            PostTask.runOrPostTask(
                    TaskTraits.UI_DEFAULT,
                    () -> {
                        for (ComponentResultReceiver receiver : mComponentsResultReceivers) {
                            receiver.getComponentLoaderPolicy()
                                    .componentLoadFailed(
                                            ComponentLoadResult
                                                    .FAILED_TO_CONNECT_TO_COMPONENTS_PROVIDER_SERVICE);
                        }
                        mComponentsResultReceivers.clear();
                    });
        }
    }
}
