// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.feed.FeedSurfaceScopeDependencyProviderImpl.NetworkResponse;
import org.chromium.chrome.browser.xsurface.feed.ResourceFetcher;
import org.chromium.chrome.browser.xsurface.feed.ResourceFetcher.Header;
import org.chromium.chrome.browser.xsurface.feed.ResourceFetcher.Request;
import org.chromium.chrome.browser.xsurface.feed.ResourceFetcher.Response;
import org.chromium.chrome.browser.xsurface.feed.ResourceFetcher.ResponseCallback;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.List;

/** Implementation of ResourceFetcher methods. */
@NullMarked
public class FeedResourceFetcher implements ResourceFetcher {
    @JNINamespace("feed::android")
    public static class FeedResponse implements Response {
        private final byte @Nullable [] mRawData;
        private final boolean mSuccess;
        private final int mStatusCode;
        private final @Nullable List<Header> mHeaders;

        public FeedResponse(
                boolean success,
                int statusCode,
                @Nullable List<Header> headers,
                byte @Nullable [] rawData) {
            mSuccess = success;
            mStatusCode = statusCode;
            mHeaders = headers;
            mRawData = rawData;
        }

        @Override
        public boolean getSuccess() {
            return mSuccess;
        }

        @Override
        public int getStatusCode() {
            return mStatusCode;
        }

        @Override
        public @Nullable List<Header> getHeaders() {
            return mHeaders;
        }

        @Override
        public byte @Nullable [] getRawData() {
            return mRawData;
        }

        @CalledByNative
        static FeedResponse create(
                boolean success, int statusCode, List<Header> headers, byte @Nullable [] rawData) {
            return new FeedResponse(success, statusCode, headers, rawData);
        }
    }

    public FeedResourceFetcher() {}

    @Override
    public void fetch(Request request, ResponseCallback responseCallback) {
        PostTask.postTask(
                TaskTraits.UI_DEFAULT,
                () -> {
                    List<String> headerNamesAndValues = new ArrayList<>(request.headers.size() * 2);
                    for (Header header : request.headers) {
                        headerNamesAndValues.add(header.name);
                        headerNamesAndValues.add(header.value);
                    }
                    FeedSurfaceScopeDependencyProviderImplJni.get()
                            .fetchResource(
                                    new GURL(request.uri),
                                    request.method,
                                    headerNamesAndValues.toArray(
                                            new String[headerNamesAndValues.size()]),
                                    request.postData,
                                    (NetworkResponse response) -> {
                                        responseCallback.onResponse(
                                                new FeedResponse(
                                                        response.success,
                                                        response.statusCode,
                                                        null,
                                                        response.rawData));
                                    });
                });
    }
}
