// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.app.bookmarks;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

import androidx.annotation.VisibleForTesting;
import androidx.appcompat.widget.Toolbar;

import org.chromium.base.Log;
import org.chromium.build.annotations.Initializer;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.SnackbarActivity;
import org.chromium.chrome.browser.bookmarks.BookmarkImageFetcher;
import org.chromium.chrome.browser.bookmarks.BookmarkManagerOpener;
import org.chromium.chrome.browser.bookmarks.BookmarkManagerOpenerImpl;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.bookmarks.BookmarkModelObserver;
import org.chromium.chrome.browser.bookmarks.BookmarkTextInputLayout;
import org.chromium.chrome.browser.bookmarks.BookmarkUiPrefs;
import org.chromium.chrome.browser.bookmarks.BookmarkUiPrefs.BookmarkRowDisplayPref;
import org.chromium.chrome.browser.bookmarks.BookmarkViewUtils;
import org.chromium.chrome.browser.bookmarks.ImprovedBookmarkRow;
import org.chromium.chrome.browser.bookmarks.ImprovedBookmarkRowCoordinator;
import org.chromium.chrome.browser.bookmarks.ImprovedBookmarkRowProperties;
import org.chromium.chrome.browser.bookmarks.ImprovedBookmarkRowProperties.ImageVisibility;
import org.chromium.chrome.browser.bookmarks.ImprovedBookmarkRowViewBinder;
import org.chromium.chrome.browser.commerce.ShoppingServiceFactory;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.bookmarks.BookmarkItem;
import org.chromium.components.browser_ui.widget.TintedDrawable;
import org.chromium.components.image_fetcher.ImageFetcherConfig;
import org.chromium.components.image_fetcher.ImageFetcherFactory;
import org.chromium.components.url_formatter.UrlFormatter;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;
import org.chromium.url.GURL;

/** The activity that enables the user to modify the title, url and parent folder of a bookmark. */
// TODO(crbug.com/40269559): Separate the activity from its view.
// TODO(crbug.com/40269559): Add a coordinator/mediator for business logic.
@NullMarked
public class BookmarkEditActivity extends SnackbarActivity {
    /** The intent extra specifying the ID of the bookmark to be edited. */
    public static final String INTENT_BOOKMARK_ID = "BookmarkEditActivity.BookmarkId";

    private static final String TAG = "BookmarkEdit";

    private BookmarkModel mModel;
    private Profile mProfile;
    private BookmarkUiPrefs mBookmarkUiPrefs;
    private BookmarkManagerOpener mBookmarkManagerOpener;
    private BookmarkId mBookmarkId;
    private PropertyModel mFolderSelectRowModel;

    private ImprovedBookmarkRowCoordinator mFolderSelectRowCoordinator;
    private ImprovedBookmarkRow mFolderSelectRow;
    private BookmarkTextInputLayout mTitleEditText;
    private BookmarkTextInputLayout mUrlEditText;
    private @Nullable MenuItem mDeleteButton;
    private FrameLayout mFolderPickerRowContainer;

    private final BookmarkUiPrefs.Observer mBookmarkUiPrefsObserver =
            new BookmarkUiPrefs.Observer() {
                @Override
                public void onBookmarkRowDisplayPrefChanged(
                        @BookmarkRowDisplayPref int displayPref) {
                    updateFolderPickerRow(displayPref);
                }
            };

    private final BookmarkModelObserver mBookmarkModelObserver =
            new BookmarkModelObserver() {
                @Override
                public void bookmarkModelChanged() {
                    if (mModel.doesBookmarkExist(mBookmarkId)) {
                        updateViewContent(true);
                    }
                }
            };

    @Override
    @Initializer
    protected void onProfileAvailable(Profile profile) {
        super.onProfileAvailable(profile);
        mProfile = profile;
        mModel = BookmarkModel.getForProfile(profile);
        mBookmarkManagerOpener = new BookmarkManagerOpenerImpl();
        mBookmarkId =
                BookmarkId.getBookmarkIdFromString(getIntent().getStringExtra(INTENT_BOOKMARK_ID));
        mModel.addObserver(mBookmarkModelObserver);
        BookmarkItem item = mModel.getBookmarkById(mBookmarkId);
        if (!mModel.doesBookmarkExist(mBookmarkId) || item == null) {
            finish();
            return;
        }

        setContentView(R.layout.bookmark_edit);
        mTitleEditText = findViewById(R.id.title_text);
        mUrlEditText = findViewById(R.id.url_text);

        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);
        assumeNonNull(getSupportActionBar()).setDisplayHomeAsUpEnabled(true);

        View shadow = findViewById(R.id.shadow);
        View scrollView = findViewById(R.id.scroll_view);
        scrollView
                .getViewTreeObserver()
                .addOnScrollChangedListener(
                        () -> {
                            shadow.setVisibility(
                                    scrollView.getScrollY() > 0 ? View.VISIBLE : View.GONE);
                        });

        boolean isFolder = item.isFolder();
        TextView folderTitle = findViewById(R.id.folder_title);
        folderTitle.setText(isFolder ? R.string.bookmark_parent_folder : R.string.bookmark_folder);
        mUrlEditText.setVisibility(isFolder ? View.GONE : View.VISIBLE);
        getSupportActionBar().setTitle(isFolder ? R.string.edit_folder : R.string.edit_bookmark);
        mBookmarkUiPrefs = new BookmarkUiPrefs(ChromeSharedPreferences.getInstance());
        mBookmarkUiPrefs.addObserver(mBookmarkUiPrefsObserver);

        mFolderSelectRowCoordinator =
                new ImprovedBookmarkRowCoordinator(
                        this,
                        new BookmarkImageFetcher(
                                profile,
                                this,
                                mModel,
                                ImageFetcherFactory.createImageFetcher(
                                        ImageFetcherConfig.DISK_CACHE_ONLY,
                                        profile.getProfileKey()),
                                BookmarkViewUtils.getRoundedIconGenerator(
                                        this, BookmarkRowDisplayPref.VISUAL)),
                        mModel,
                        mBookmarkUiPrefs,
                        ShoppingServiceFactory.getForProfile(profile));

        mFolderPickerRowContainer = findViewById(R.id.folder_row_container);

        updateViewContent(false);
    }

    /**
     * @param modelChanged Whether this view update is due to a model change in background.
     */
    @Initializer
    private void updateViewContent(boolean modelChanged) {
        BookmarkItem bookmarkItem = assumeNonNull(mModel.getBookmarkById(mBookmarkId));
        // While the user is editing the bookmark, do not override user's input.
        if (!modelChanged) {
            assumeNonNull(mTitleEditText.getEditText()).setText(bookmarkItem.getTitle());
            assumeNonNull(mUrlEditText.getEditText()).setText(bookmarkItem.getUrl().getSpec());
        }
        mTitleEditText.setEnabled(bookmarkItem.isEditable());
        mUrlEditText.setEnabled(bookmarkItem.isUrlEditable());
        updateFolderPickerRow(mBookmarkUiPrefs.getBookmarkRowDisplayPref());
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        mDeleteButton =
                menu.add(R.string.bookmark_toolbar_delete)
                        .setIcon(
                                TintedDrawable.constructTintedDrawable(
                                        this, R.drawable.ic_delete_white_24dp))
                        .setShowAsActionFlags(MenuItem.SHOW_AS_ACTION_IF_ROOM);

        return super.onCreateOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item == mDeleteButton) {
            // Log added for detecting delete button double clicking.
            Log.i(TAG, "Delete button pressed by user! isFinishing() == " + isFinishing());

            mModel.deleteBookmark(mBookmarkId);
            finish();
            return true;
        } else if (item.getItemId() == android.R.id.home) {
            finish();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    protected void onStop() {
        if (mModel.doesBookmarkExist(mBookmarkId)) {
            BookmarkItem bookmarkItem = assumeNonNull(mModel.getBookmarkById(mBookmarkId));
            final GURL originalUrl = bookmarkItem.getUrl();
            final String title = mTitleEditText.getTrimmedText();
            final String url = mUrlEditText.getTrimmedText();

            if (!mTitleEditText.isEmpty()) {
                mModel.setBookmarkTitle(mBookmarkId, title);
            }

            if (!mUrlEditText.isEmpty() && bookmarkItem.isUrlEditable()) {
                GURL fixedUrl = UrlFormatter.fixupUrl(url);
                if (fixedUrl.isValid() && !fixedUrl.equals(originalUrl)) {
                    mModel.setBookmarkUrl(mBookmarkId, fixedUrl);
                }
            }
        }

        super.onStop();
    }

    @Override
    protected void onDestroy() {
        mModel.removeObserver(mBookmarkModelObserver);
        if (mBookmarkUiPrefs != null) {
            mBookmarkUiPrefs.removeObserver(mBookmarkUiPrefsObserver);
        }
        super.onDestroy();
    }

    @VisibleForTesting
    BookmarkTextInputLayout getTitleEditText() {
        return mTitleEditText;
    }

    @VisibleForTesting
    BookmarkTextInputLayout getUrlEditText() {
        return mUrlEditText;
    }

    @VisibleForTesting
    @Nullable MenuItem getDeleteButton() {
        return mDeleteButton;
    }

    private void updateFolderPickerRow(@BookmarkRowDisplayPref int displayPref) {
        BookmarkItem bookmarkItem = assumeNonNull(mModel.getBookmarkById(mBookmarkId));
        mFolderSelectRowModel =
                mFolderSelectRowCoordinator.createBasePropertyModel(bookmarkItem.getParentId());

        mFolderSelectRowModel.set(
                ImprovedBookmarkRowProperties.END_IMAGE_RES, R.drawable.outline_chevron_right_24dp);
        mFolderSelectRowModel.set(
                ImprovedBookmarkRowProperties.END_IMAGE_VISIBILITY, ImageVisibility.DRAWABLE);
        mFolderSelectRowModel.set(
                ImprovedBookmarkRowProperties.ROW_CLICK_LISTENER,
                () ->
                        mBookmarkManagerOpener.startFolderPickerActivity(
                                /* context= */ this, mProfile, mBookmarkId));

        mFolderSelectRow =
                ImprovedBookmarkRow.buildView(this, displayPref == BookmarkRowDisplayPref.VISUAL);
        PropertyModelChangeProcessor.create(
                mFolderSelectRowModel, mFolderSelectRow, ImprovedBookmarkRowViewBinder::bind);

        mFolderPickerRowContainer.removeAllViews();
        mFolderPickerRowContainer.addView(mFolderSelectRow);
    }

    View getFolderSelectRowForTesting() {
        return mFolderSelectRow;
    }

    PropertyModel getFolderSelectRowPropertyModelForTesting() {
        return mFolderSelectRowModel;
    }
}
