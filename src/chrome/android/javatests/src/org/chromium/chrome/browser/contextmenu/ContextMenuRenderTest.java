// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextmenu;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.BitmapDrawable;
import android.view.View;
import android.view.ViewStub;

import androidx.test.filters.LargeTest;

import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.ClassRule;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.BaseActivityTestRule;
import org.chromium.base.test.params.ParameterAnnotations;
import org.chromium.base.test.params.ParameterSet;
import org.chromium.base.test.params.ParameterizedRunner;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.test.ChromeJUnit4RunnerDelegate;
import org.chromium.chrome.test.util.ChromeRenderTestRule;
import org.chromium.components.embedder_support.contextmenu.ContextMenuSwitches;
import org.chromium.ui.listmenu.ListItemType;
import org.chromium.ui.listmenu.ListMenuItemProperties;
import org.chromium.ui.listmenu.ListMenuItemViewBinder;
import org.chromium.ui.modelutil.LayoutViewBuilder;
import org.chromium.ui.modelutil.MVCListAdapter.ListItem;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.ModelListAdapter;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.test.util.BlankUiTestActivity;
import org.chromium.ui.test.util.NightModeTestUtils;

import java.io.IOException;
import java.util.List;

/** Render tests for the ContextMenu */
@RunWith(ParameterizedRunner.class)
@ParameterAnnotations.UseRunnerDelegate(ChromeJUnit4RunnerDelegate.class)
@Batch(Batch.PER_CLASS)
public class ContextMenuRenderTest {
    @ParameterAnnotations.ClassParameter
    private static final List<ParameterSet> sClassParams =
            new NightModeTestUtils.NightModeParams().getParameters();

    @ClassRule
    public static BaseActivityTestRule<BlankUiTestActivity> sActivityTestRule =
            new BaseActivityTestRule<>(BlankUiTestActivity.class);

    private static Activity sActivity;

    @Rule
    public ChromeRenderTestRule mRenderTestRule =
            ChromeRenderTestRule.Builder.withPublicCorpus()
                    .setBugComponent(ChromeRenderTestRule.Component.UI_BROWSER_MOBILE_CONTEXT_MENU)
                    .setRevision(1)
                    .build();

    private ModelListAdapter mAdapter;
    private ModelList mListItems;
    private View mView;
    private View mFrame;

    public ContextMenuRenderTest(boolean nightModeEnabled) {
        NightModeTestUtils.setUpNightModeForBlankUiTestActivity(nightModeEnabled);
        mRenderTestRule.setNightModeEnabled(nightModeEnabled);
    }

    @BeforeClass
    public static void setupSuite() {
        sActivity = sActivityTestRule.launchActivity(null);
    }

    @Before
    public void setUp() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mListItems = new ModelList();
                    mAdapter = new ModelListAdapter(mListItems);

                    sActivity.setContentView(R.layout.context_menu_fullscreen_container);
                    mView = sActivity.findViewById(android.R.id.content);
                    ((ViewStub) mView.findViewById(R.id.context_menu_stub)).inflate();
                    mFrame = mView.findViewById(R.id.context_menu_frame);
                    ContextMenuListView listView = mView.findViewById(R.id.context_menu_list_view);
                    listView.setAdapter(mAdapter);

                    mAdapter.registerType(
                            ListItemType.HEADER,
                            new LayoutViewBuilder(R.layout.context_menu_header),
                            ContextMenuHeaderViewBinder::bind);
                    mAdapter.registerType(
                            ListItemType.DIVIDER,
                            new LayoutViewBuilder(R.layout.list_section_divider),
                            (m, v, p) -> {});
                    mAdapter.registerType(
                            ListItemType.CONTEXT_MENU_ITEM,
                            new LayoutViewBuilder(R.layout.context_menu_row),
                            ListMenuItemViewBinder::binder);
                    mAdapter.registerType(
                            ListItemType.CONTEXT_MENU_ITEM_WITH_ICON_BUTTON,
                            new LayoutViewBuilder(R.layout.context_menu_share_row),
                            ContextMenuItemWithIconButtonViewBinder::bind);
                });
    }

    @After
    public void tearDown() throws Exception {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    NightModeTestUtils.tearDownNightModeForBlankUiTestActivity();
                    mListItems.clear();
                });
    }

    @Test
    @LargeTest
    @Feature({"RenderTest"})
    public void testContextMenuViewWithLink() throws IOException {
        doTestContextMenuViewWithLink("context_menu_with_link");
    }

    @Test
    @LargeTest
    @Feature({"RenderTest"})
    @CommandLineFlags.Add(ContextMenuSwitches.FORCE_CONTEXT_MENU_POPUP)
    public void testContextMenuViewWithLink_Popup() throws IOException {
        doTestContextMenuViewWithLink("context_menu_with_link_popup");
    }

    @Test
    @LargeTest
    @Feature({"RenderTest"})
    public void testContextMenuViewWithImageLink() throws IOException {
        doTestContextMenuViewWithImageLink("context_menu_with_image_link");
    }

    @Test
    @LargeTest
    @Feature({"RenderTest"})
    @CommandLineFlags.Add(ContextMenuSwitches.FORCE_CONTEXT_MENU_POPUP)
    public void testContextMenuViewWithImageLink_Popup() throws IOException {
        doTestContextMenuViewWithImageLink("context_menu_with_image_link_popup");
    }

    private void doTestContextMenuViewWithLink(String id) throws IOException {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mListItems.add(
                            new ListItem(
                                    ListItemType.HEADER,
                                    getHeaderModel("", "www.google.com", false)));
                    mListItems.add(new ListItem(ListItemType.DIVIDER, new PropertyModel()));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Open in new tab")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Open in incognito tab")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Copy link address")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM_WITH_ICON_BUTTON,
                                    getShareItemModel("Share link")));
                });
        mRenderTestRule.render(mFrame, id);
    }

    private void doTestContextMenuViewWithImageLink(String id) throws IOException {
        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mListItems.add(
                            new ListItem(
                                    ListItemType.HEADER,
                                    getHeaderModel("Capybara", "www.google.com", true)));
                    mListItems.add(new ListItem(ListItemType.DIVIDER, new PropertyModel()));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Open in new tab")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Open in incognito tab")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Copy link address")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM_WITH_ICON_BUTTON,
                                    getShareItemModel("Share link")));
                    mListItems.add(new ListItem(ListItemType.DIVIDER, new PropertyModel()));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Open image in new tab")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM,
                                    getItemModel("Download image")));
                    mListItems.add(
                            new ListItem(
                                    ListItemType.CONTEXT_MENU_ITEM_WITH_ICON_BUTTON,
                                    getShareItemModel("Share image")));
                });
        mRenderTestRule.render(mFrame, id);
    }

    private PropertyModel getHeaderModel(
            String title, CharSequence url, boolean hasImageThumbnail) {
        PropertyModel model = ContextMenuHeaderCoordinator.buildModel(sActivity, title, url);
        Bitmap image;
        if (hasImageThumbnail) {
            image =
                    BitmapFactory.decodeFile(
                            UrlUtils.getIsolatedTestFilePath(
                                    "chrome/test/data/android/capybara.jpg"));
        } else {
            final int size = model.get(ContextMenuHeaderProperties.MONOGRAM_SIZE_PIXEL);
            image =
                    BitmapFactory.decodeFile(
                            UrlUtils.getIsolatedTestFilePath(
                                    "chrome/test/data/android/UiCapture/cloud.png"));
            image = Bitmap.createScaledBitmap(image, size, size, true);
        }

        model.set(ContextMenuHeaderProperties.IMAGE, image);
        model.set(ContextMenuHeaderProperties.CIRCLE_BG_VISIBLE, !hasImageThumbnail);
        return model;
    }

    private PropertyModel getItemModel(String title) {
        return new PropertyModel.Builder(
                        ListMenuItemProperties.MENU_ITEM_ID,
                        ListMenuItemProperties.TITLE,
                        ListMenuItemProperties.ENABLED)
                .with(ListMenuItemProperties.TITLE, title)
                .build();
    }

    private PropertyModel getShareItemModel(String title) {
        final BitmapDrawable drawable =
                new BitmapDrawable(
                        sActivity.getResources(),
                        BitmapFactory.decodeFile(
                                UrlUtils.getIsolatedTestFilePath(
                                        "chrome/test/data/android/UiCapture/dots.png")));
        return new PropertyModel.Builder(ContextMenuItemWithIconButtonProperties.ALL_KEYS)
                .with(ContextMenuItemWithIconButtonProperties.TITLE, title)
                .with(ContextMenuItemWithIconButtonProperties.ENABLED, true)
                .with(ContextMenuItemWithIconButtonProperties.BUTTON_IMAGE, drawable)
                .build();
    }
}
