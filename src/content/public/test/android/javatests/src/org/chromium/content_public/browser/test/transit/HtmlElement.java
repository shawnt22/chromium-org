// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.browser.test.transit;

import android.graphics.Rect;

import org.chromium.base.supplier.Supplier;
import org.chromium.base.test.transit.Condition;
import org.chromium.base.test.transit.ConditionWithResult;
import org.chromium.base.test.transit.Element;
import org.chromium.base.test.transit.Transition;
import org.chromium.base.test.transit.TravelException;
import org.chromium.base.test.transit.TripBuilder;
import org.chromium.build.annotations.Nullable;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.test.transit.HtmlConditions.DisplayedCondition;
import org.chromium.content_public.browser.test.transit.HtmlConditions.NotDisplayedCondition;
import org.chromium.content_public.browser.test.util.DOMUtils;

import java.util.concurrent.TimeoutException;

/**
 * A Public Transit {@link Element} representing an HTML DOM element to be searched for in the given
 * {@link WebContents}.
 */
public class HtmlElement extends Element<Rect> {
    protected final HtmlElementSpec mHtmlElementSpec;
    protected final Supplier<WebContents> mWebContentsSupplier;

    public HtmlElement(HtmlElementSpec htmlElementSpec, Supplier<WebContents> webContentsSupplier) {
        super(htmlElementSpec.getId());
        mHtmlElementSpec = htmlElementSpec;
        mWebContentsSupplier = webContentsSupplier;
    }

    @Override
    public @Nullable ConditionWithResult<Rect> createEnterCondition() {
        return new DisplayedCondition(mWebContentsSupplier, mHtmlElementSpec.getHtmlId());
    }

    @Override
    public Condition createExitCondition() {
        return new NotDisplayedCondition(mWebContentsSupplier, mHtmlElementSpec.getHtmlId());
    }

    /** Returns a trigger to click the HTML element to trigger a Transition. */
    public Transition.Trigger getClickTrigger() {
        return () -> {
            try {
                DOMUtils.clickNode(mWebContentsSupplier.get(), mHtmlElementSpec.getHtmlId());
            } catch (TimeoutException e) {
                throw TravelException.newTravelException(
                        "Timed out trying to click DOM element", e);
            }
        };
    }

    /** Returns a trigger to long press the HTML element to trigger a Transition. */
    public Transition.Trigger getLongPressTrigger() {
        return () -> {
            try {
                DOMUtils.longPressNode(mWebContentsSupplier.get(), mHtmlElementSpec.getHtmlId());
            } catch (TimeoutException e) {
                throw TravelException.newTravelException(
                        "Timed out trying to long press DOM element", e);
            }
        };
    }

    /** Returns a TripBuilder with clicking the HTML element as trigger. */
    public TripBuilder clickTo() {
        return new TripBuilder().withContext(this).withTrigger(getClickTrigger());
    }

    /** Returns a TripBuilder with long pressing the HTML element as trigger. */
    public TripBuilder longPressTo() {
        return new TripBuilder().withContext(this).withTrigger(getLongPressTrigger());
    }
}
