// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import android.os.Build;

import androidx.test.InstrumentationRegistry;

import org.junit.runners.model.FrameworkMethod;

import org.chromium.base.Log;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Checks for conditional disables.
 *
 * Currently, this only includes checks against a few {@link android.os.Build} values.
 */
public class DisableIfSkipCheck extends SkipCheck {
    private static final String EXTRA_RUN_DISABLED_TEST =
            "org.chromium.base.test.util.DisableIfSkipCheck.RunDisabledTest";

    private static final String TAG = "base_test";

    @Override
    public boolean shouldSkip(FrameworkMethod method) {
        if (method == null) return true;

        String runDisabledTest =
                InstrumentationRegistry.getArguments().getString(EXTRA_RUN_DISABLED_TEST);
        if ("true".equals(runDisabledTest)) {
            return false;
        }

        List<DisableIf.Build> buildAnnotationList = gatherBuildAnnotations(method);

        for (DisableIf.Build v : buildAnnotationList) {
            if (abi(v) && hardware(v) && product(v) && sdk_range(v) && sdk_exact(v)) {
                if (!v.message().isEmpty()) {
                    Log.i(TAG, "%s is disabled: %s", method.getName(), v.message());
                }
                return true;
            }
        }

        for (DisableIf.Device d :
                AnnotationProcessingUtils.getAnnotations(
                        method.getMethod(), DisableIf.Device.class)) {
            for (String deviceType : d.value()) {
                if (deviceTypeApplies(deviceType)) {
                    Log.i(
                            TAG,
                            "Test "
                                    + method.getDeclaringClass().getName()
                                    + "#"
                                    + method.getName()
                                    + " disabled because of "
                                    + d);
                    return true;
                }
            }
        }

        return false;
    }

    @SuppressWarnings("deprecation")
    private boolean abi(DisableIf.Build v) {
        if (v.supported_abis_includes().isEmpty()) return true;

        return Arrays.asList(Build.SUPPORTED_ABIS).contains(v.supported_abis_includes());
    }

    private boolean hardware(DisableIf.Build v) {
        return v.hardware_is().isEmpty() || Build.HARDWARE.equals(v.hardware_is());
    }

    private boolean product(DisableIf.Build v) {
        return v.product_name_includes().isEmpty()
                || Build.PRODUCT.contains(v.product_name_includes());
    }

    private boolean sdk_range(DisableIf.Build v) {
        return Build.VERSION.SDK_INT > v.sdk_is_greater_than()
                && Build.VERSION.SDK_INT < v.sdk_is_less_than();
    }

    private boolean sdk_exact(DisableIf.Build v) {
        return v.sdk_equals() == 0 || Build.VERSION.SDK_INT == v.sdk_equals();
    }

    protected boolean deviceTypeApplies(String type) {
        return false;
    }

    private List<DisableIf.Build> gatherBuildAnnotations(FrameworkMethod method) {
        List<DisableIf.Build> buildAnnotationList = new ArrayList<>();

        // {@link DisableIf.Build} annotations will be wrapped in a {@link DisableIf.Builds} if
        // there is more than one present on the method.
        for (DisableIf.Builds buildsAnnotation :
                AnnotationProcessingUtils.getAnnotations(
                        method.getMethod(), DisableIf.Builds.class)) {
            buildAnnotationList.addAll(Arrays.asList(buildsAnnotation.value()));
        }

        // This will find the {@link DisableIf.Build} annotation when there's exactly one present on
        // the method.
        buildAnnotationList.addAll(
                AnnotationProcessingUtils.getAnnotations(
                        method.getMethod(), DisableIf.Build.class));

        return buildAnnotationList;
    }
}
