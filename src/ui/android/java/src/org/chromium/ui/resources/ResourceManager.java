// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.resources;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.util.SparseArray;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.display.DisplayAndroid;
import org.chromium.ui.resources.ResourceLoader.ResourceLoaderCallback;
import org.chromium.ui.resources.dynamics.BitmapDynamicResource;
import org.chromium.ui.resources.dynamics.DynamicResourceLoader;
import org.chromium.ui.resources.statics.StaticResourceLoader;
import org.chromium.ui.resources.system.SystemResourceLoader;

/**
 * The Java component of a manager for all static resources to be loaded and used by CC layers. This
 * class does not hold any resource state, but passes it directly to native as they are loaded.
 */
@JNINamespace("ui")
@NullMarked
public class ResourceManager implements ResourceLoaderCallback {
    private final SparseArray<ResourceLoader> mResourceLoaders = new SparseArray<>();
    private final SparseArray<SparseArray<LayoutResource>> mLoadedResources =
            new SparseArray<SparseArray<LayoutResource>>();

    private final float mPxToDp;

    private long mNativeResourceManagerPtr;

    private ResourceManager(
            Resources resources, int minScreenSideLength, long staticResourceManagerPtr) {
        mPxToDp = 1.f / resources.getDisplayMetrics().density;

        registerResourceLoader(
                new StaticResourceLoader(AndroidResourceType.STATIC, this, resources));
        registerResourceLoader(new DynamicResourceLoader(AndroidResourceType.DYNAMIC, this));
        registerResourceLoader(new DynamicResourceLoader(AndroidResourceType.DYNAMIC_BITMAP, this));
        registerResourceLoader(
                new SystemResourceLoader(AndroidResourceType.SYSTEM, this, minScreenSideLength));

        mNativeResourceManagerPtr = staticResourceManagerPtr;
    }

    /**
     * Creates an instance of a {@link ResourceManager}.
     * @param windowAndroid            A {@link WindowAndroid} instance to fetch a {@link Context}
     * and thus grab {@link Resources} from.
     * @param staticResourceManagerPtr A pointer to the native component of this class.
     * @return                         A new instance of a {@link ResourceManager}.
     */
    @CalledByNative
    private static ResourceManager create(
            WindowAndroid windowAndroid, long staticResourceManagerPtr) {
        Context context = windowAndroid.getContext().get();
        // This call should happen early enough (i.e. during construction) that this context should
        // not yet have been released.
        if (context == null) {
            throw new IllegalStateException("Context should not be null during initialization.");
        }

        DisplayAndroid displayAndroid = windowAndroid.getDisplay();
        int minScreenSideLength =
                Math.min(displayAndroid.getDisplayWidth(), displayAndroid.getDisplayHeight());

        Resources resources = context.getResources();
        return new ResourceManager(resources, minScreenSideLength, staticResourceManagerPtr);
    }

    /**
     * @return A reference to the {@link DynamicResourceLoader} that provides {@link Resource}
     *     objects to this class.
     */
    public DynamicResourceLoader getDynamicResourceLoader() {
        DynamicResourceLoader ret =
                (DynamicResourceLoader) mResourceLoaders.get(AndroidResourceType.DYNAMIC);
        assert ret != null;
        return ret;
    }

    /**
     * @return A reference to the {@link DynamicResourceLoader} for bitmaps that provides {@link
     *     BitmapDynamicResource} objects to this class.
     */
    public DynamicResourceLoader getBitmapDynamicResourceLoader() {
        DynamicResourceLoader ret =
                (DynamicResourceLoader) mResourceLoaders.get(AndroidResourceType.DYNAMIC_BITMAP);
        assert ret != null;
        return ret;
    }

    /**
     * Automatically loads any synchronous resources specified in |syncIds| and will start
     * asynchronous reads for any asynchronous resources specified in |asyncIds|.
     *
     * @param type AndroidResourceType which will be loaded.
     * @param syncIds Resource ids which will be loaded synchronously.
     * @param asyncIds Resource ids which will be loaded asynchronously.
     */
    public void preloadResources(int type, int[] syncIds, int[] asyncIds) {
        ResourceLoader loader = mResourceLoaders.get(type);
        assumeNonNull(loader);
        if (asyncIds != null) {
            for (Integer resId : asyncIds) {
                loader.preloadResource(resId);
            }
        }

        if (syncIds != null) {
            for (Integer resId : syncIds) {
                loader.loadResource(resId);
            }
        }
    }

    /**
     * @param resType The type of the Android resource.
     * @param resId   The id of the Android resource.
     * @return The corresponding {@link LayoutResource}.
     */
    public @Nullable LayoutResource getResource(@AndroidResourceType int resType, int resId) {
        SparseArray<LayoutResource> bucket = mLoadedResources.get(resType);
        return bucket != null ? bucket.get(resId) : null;
    }

    @SuppressWarnings("cast")
    @Override
    public void onResourceLoaded(
            @AndroidResourceType int resType, int resId, @Nullable Resource resource) {
        if (resource == null) return;
        Bitmap bitmap = resource.getBitmap();
        saveMetadataForLoadedResource(resType, resId, resource);

        if (mNativeResourceManagerPtr == 0) return;

        ResourceManagerJni.get()
                .onResourceReady(
                        mNativeResourceManagerPtr,
                        ResourceManager.this,
                        resType,
                        resId,
                        bitmap,
                        resource.getBitmapSize().width(),
                        resource.getBitmapSize().height(),
                        resource.createNativeResource());
    }

    @Override
    public void onResourceUnregistered(@AndroidResourceType int resType, int resId) {
        // Only remove dynamic resources that were unregistered.
        if (resType != AndroidResourceType.DYNAMIC_BITMAP
                && resType != AndroidResourceType.DYNAMIC) {
            return;
        }

        if (mNativeResourceManagerPtr == 0) return;

        ResourceManagerJni.get()
                .removeResource(mNativeResourceManagerPtr, ResourceManager.this, resType, resId);
    }

    /** Clear the cache of tinted assets that the native manager holds. */
    public void clearTintedResourceCache() {
        if (mNativeResourceManagerPtr == 0) return;
        ResourceManagerJni.get()
                .clearTintedResourceCache(mNativeResourceManagerPtr, ResourceManager.this);
    }

    private void saveMetadataForLoadedResource(
            @AndroidResourceType int resType, int resId, Resource resource) {
        SparseArray<LayoutResource> bucket = mLoadedResources.get(resType);
        if (bucket == null) {
            bucket = new SparseArray<>();
            mLoadedResources.put(resType, bucket);
        }
        bucket.put(resId, new LayoutResource(mPxToDp, resource));
    }

    @CalledByNative
    private void destroy() {
        assert mNativeResourceManagerPtr != 0;
        mNativeResourceManagerPtr = 0;
    }

    @CalledByNative
    private void resourceRequested(int resType, int resId) {
        ResourceLoader loader = mResourceLoaders.get(resType);
        if (loader != null) loader.loadResource(resId);
    }

    @CalledByNative
    private void preloadResource(int resType, int resId) {
        ResourceLoader loader = mResourceLoaders.get(resType);
        if (loader != null) loader.preloadResource(resId);
    }

    @CalledByNative
    private long getNativePtr() {
        return mNativeResourceManagerPtr;
    }

    private void registerResourceLoader(ResourceLoader loader) {
        mResourceLoaders.put(loader.getResourceType(), loader);
    }

    public void dumpIfNoResource(int resType, int resId) {
        ResourceManagerJni.get()
                .dumpIfNoResource(mNativeResourceManagerPtr, ResourceManager.this, resType, resId);
    }

    @NativeMethods
    interface Natives {
        void onResourceReady(
                long nativeResourceManagerImpl,
                ResourceManager caller,
                int resType,
                int resId,
                Bitmap bitmap,
                int width,
                int height,
                long nativeResource);

        void removeResource(
                long nativeResourceManagerImpl, ResourceManager caller, int resType, int resId);

        void clearTintedResourceCache(long nativeResourceManagerImpl, ResourceManager caller);

        void dumpIfNoResource(
                long nativeResourceManagerImpl, ResourceManager caller, int resType, int resId);
    }
}
