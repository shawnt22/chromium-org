// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarks;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.res.Resources;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.DimenRes;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.bookmarks.BookmarkListEntry.SectionHeaderData;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.ui.signin.PersonalizedSigninPromoView;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/** Responsible for binding views to their properties. */
@NullMarked
class BookmarkManagerViewBinder {
    static void bindPersonalizedPromoView(PropertyModel model, View view, PropertyKey key) {
        assert !ChromeFeatureList.isEnabled(ChromeFeatureList.UNO_PHASE_2_FOLLOW_UP);
        if (key == BookmarkManagerProperties.BOOKMARK_PROMO_HEADER) {
            PersonalizedSigninPromoView promoView =
                    view.findViewById(R.id.signin_promo_view_container);
            model.get(BookmarkManagerProperties.BOOKMARK_PROMO_HEADER)
                    .setUpSyncPromoView(promoView);
        }
    }

    static void bindLegacyPromoView(PropertyModel model, View view, PropertyKey key) {}

    static void bindBatchUploadCardView(PropertyModel model, View view, PropertyKey key) {
        if (key == BookmarkManagerProperties.BATCH_UPLOAD_CARD_COORDINATOR) {
            model.get(BookmarkManagerProperties.BATCH_UPLOAD_CARD_COORDINATOR)
                    .setView(view.findViewById(R.id.signin_settings_card));
        }
    }

    static void bindSectionHeaderView(PropertyModel model, View view, PropertyKey key) {
        if (key == BookmarkManagerProperties.BOOKMARK_LIST_ENTRY) {
            Resources resources = view.getResources();
            BookmarkListEntry bookmarkListEntry =
                    model.get(BookmarkManagerProperties.BOOKMARK_LIST_ENTRY);
            TextView title = view.findViewById(R.id.title);
            SectionHeaderData sectionHeaderData = bookmarkListEntry.getSectionHeaderData();
            assumeNonNull(sectionHeaderData);
            title.setText(resources.getText(sectionHeaderData.titleRes));
            final @DimenRes int topPaddingRes = sectionHeaderData.topPaddingRes;
            if (topPaddingRes != Resources.ID_NULL) {
                title.setPaddingRelative(
                        title.getPaddingStart(),
                        resources.getDimensionPixelSize(topPaddingRes),
                        title.getPaddingEnd(),
                        title.getPaddingBottom());
            }
        }
    }

    static void bindDividerView(PropertyModel model, View view, PropertyKey key) {}
}
