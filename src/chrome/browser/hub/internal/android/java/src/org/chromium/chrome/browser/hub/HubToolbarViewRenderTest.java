// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.hub;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import static org.chromium.chrome.browser.hub.HubColorMixer.COLOR_MIXER;

import android.app.Activity;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.widget.FrameLayout;

import androidx.annotation.DrawableRes;
import androidx.annotation.Nullable;
import androidx.test.filters.MediumTest;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.base.ThreadUtils;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.test.BaseActivityTestRule;
import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.toolbar.TabSwitcherDrawable;
import org.chromium.chrome.browser.toolbar.TabSwitcherDrawable.TabSwitcherDrawableLocation;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.chrome.test.util.ChromeRenderTestRule;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;
import org.chromium.ui.test.util.BlankUiTestActivity;

import java.util.ArrayList;
import java.util.List;

/** Render tests for {@link HubPaneHostView}. */
@RunWith(BaseJUnit4ClassRunner.class)
@Batch(Batch.PER_CLASS)
public class HubToolbarViewRenderTest {
    @Rule
    public BaseActivityTestRule<BlankUiTestActivity> mActivityTestRule =
            new BaseActivityTestRule<>(BlankUiTestActivity.class);

    @Rule
    public ChromeRenderTestRule mRenderTestRule =
            ChromeRenderTestRule.Builder.withPublicCorpus()
                    .setBugComponent(ChromeRenderTestRule.Component.UI_BROWSER_MOBILE_HUB)
                    .setRevision(13)
                    .build();

    @Mock private TabSwitcherDrawable.Observer mTabSwitcherDrawableObserver;
    @Mock private Pane mPane;

    private ObservableSupplierImpl<Pane> mFocusedPaneSupplier;
    private Activity mActivity;
    private HubToolbarView mToolbar;
    private PropertyModel mPropertyModel;
    private HubColorMixer mColorMixer;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        mActivityTestRule.launchActivity(null);
        mActivity = mActivityTestRule.getActivity();
        mActivity.setTheme(R.style.Theme_BrowserUI_DayNight);
        if (ChromeFeatureList.sGridTabSwitcherUpdate.isEnabled()) {
            mActivity.getTheme().applyStyle(R.style.HubToolbarActionButtonStyleOverlay_Fill, true);
        } else {
            mActivity
                    .getTheme()
                    .applyStyle(R.style.HubToolbarActionButtonStyleOverlay_Baseline, true);
        }
        ThreadUtils.runOnUiThreadBlocking(this::setUpOnUi);
    }

    private void setUpOnUi() {
        LayoutInflater inflater = LayoutInflater.from(mActivity);
        FrameLayout toolbarContainer =
                (FrameLayout) inflater.inflate(R.layout.hub_toolbar_layout, null, false);
        mToolbar = toolbarContainer.findViewById(R.id.hub_toolbar);
        mActivity.setContentView(toolbarContainer);
        mFocusedPaneSupplier = new ObservableSupplierImpl<>();
        mColorMixer =
                new HubColorMixerImpl(
                        mActivity, new ObservableSupplierImpl<>(true), mFocusedPaneSupplier);
        mPropertyModel =
                new PropertyModel.Builder(HubToolbarProperties.ALL_KEYS)
                        .with(COLOR_MIXER, mColorMixer)
                        .build();
        PropertyModelChangeProcessor.create(mPropertyModel, mToolbar, HubToolbarViewBinder::bind);
        when(mPane.getColorScheme()).thenReturn(HubColorScheme.DEFAULT);
        mFocusedPaneSupplier.set(mPane);
    }

    private FullButtonData enabledButtonData(@DrawableRes int drawableRes) {
        return makeButtonData(drawableRes, () -> {});
    }

    private FullButtonData disabledButtonData(@DrawableRes int drawableRes) {
        return makeButtonData(drawableRes, null);
    }

    private FullButtonData makeButtonData(
            @DrawableRes int drawableRes, @Nullable Runnable onPress) {
        DisplayButtonData displayButtonData =
                new ResourceButtonData(
                        R.string.button_new_tab, R.string.button_new_tab, drawableRes);
        return new DelegateButtonData(displayButtonData, onPress);
    }

    private FullButtonData makeButtonData(Drawable drawable, @Nullable Runnable onPress) {
        DisplayButtonData displayButtonData =
                new DrawableButtonData(R.string.button_new_tab, R.string.button_new_tab, drawable);
        return new DelegateButtonData(displayButtonData, onPress);
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    public void testActionButton() throws Exception {
        FullButtonData enabledButtonData = enabledButtonData(R.drawable.new_tab_icon);
        FullButtonData disabledButtonData = disabledButtonData(R.drawable.new_tab_icon);

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, enabledButtonData);
                });
        mRenderTestRule.render(mToolbar, "actionButtonOnlyImage");

        ThreadUtils.runOnUiThreadBlocking(
                () ->
                        mPropertyModel.set(
                                HubToolbarProperties.ACTION_BUTTON_DATA, disabledButtonData));
        mRenderTestRule.render(mToolbar, "disabledButtonOnlyImage");

        ThreadUtils.runOnUiThreadBlocking(
                () -> mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, null));
        mRenderTestRule.render(mToolbar, "noActionButton");

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, enabledButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPane = mock();
                    when(mPane.getColorScheme()).thenReturn(HubColorScheme.INCOGNITO);
                    mFocusedPaneSupplier.set(mPane);
                });
        mRenderTestRule.render(mToolbar, "actionButtonIncognito");

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, disabledButtonData);
                });
        mRenderTestRule.render(mToolbar, "disabledActionButtonIncognito");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @EnableFeatures({ChromeFeatureList.GRID_TAB_SWITCHER_UPDATE})
    public void testActionButtonWithGTSUpdate() throws Exception {
        FullButtonData enabledButtonData = enabledButtonData(R.drawable.new_tab_icon);
        FullButtonData disabledButtonData = disabledButtonData(R.drawable.new_tab_icon);

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, enabledButtonData);
                });
        mRenderTestRule.render(mToolbar, "actionButtonOnlyImageWithGTSUpdate");

        ThreadUtils.runOnUiThreadBlocking(
                () ->
                        mPropertyModel.set(
                                HubToolbarProperties.ACTION_BUTTON_DATA, disabledButtonData));
        mRenderTestRule.render(mToolbar, "disabledButtonOnlyImageWithGTSUpdate");

        ThreadUtils.runOnUiThreadBlocking(
                () -> mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, null));
        mRenderTestRule.render(mToolbar, "noActionButtonWithGTSUpdate");

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, enabledButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPane = mock();
                    when(mPane.getColorScheme()).thenReturn(HubColorScheme.INCOGNITO);
                    mFocusedPaneSupplier.set(mPane);
                });
        mRenderTestRule.render(mToolbar, "actionButtonIncognitoWithGTSUpdate");

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, disabledButtonData);
                });
        mRenderTestRule.render(mToolbar, "disabledActionButtonIncognitoWithGTSUpdate");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @DisabledTest(message = "https://crbug.com/419357373")
    public void testPaneSwitcher() throws Exception {
        FullButtonData actionButtonData = enabledButtonData(R.drawable.new_tab_icon);
        List<FullButtonData> paneSwitcherButtonData = new ArrayList<>();
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.new_tab_icon));
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.incognito_small));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, actionButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 0);
                    mPropertyModel.set(
                            HubToolbarProperties.PANE_SWITCHER_BUTTON_DATA, paneSwitcherButtonData);
                });
        mRenderTestRule.render(mToolbar, "paneSwitcher");

        ThreadUtils.runOnUiThreadBlocking(
                () -> mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 1));
        mRenderTestRule.render(mToolbar, "paneSwitcherSelectedIndex");

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPane = mock();
                    when(mPane.getColorScheme()).thenReturn(HubColorScheme.INCOGNITO);
                    mFocusedPaneSupplier.set(mPane);
                });
        mRenderTestRule.render(mToolbar, "paneSwitcherIncognito");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @EnableFeatures({ChromeFeatureList.GRID_TAB_SWITCHER_UPDATE})
    @DisabledTest(message = "https://crbug.com/419357373")
    public void testPaneSwitcherWithGtsUpdate() throws Exception {
        FullButtonData actionButtonData = enabledButtonData(R.drawable.new_tab_icon);
        List<FullButtonData> paneSwitcherButtonData = new ArrayList<>();
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.new_tab_icon));
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.incognito_small));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, actionButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 0);
                    mPropertyModel.set(
                            HubToolbarProperties.PANE_SWITCHER_BUTTON_DATA, paneSwitcherButtonData);
                });
        mRenderTestRule.render(mToolbar, "paneSwitcherWithGtsUpdate");

        ThreadUtils.runOnUiThreadBlocking(
                () -> mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 1));
        mRenderTestRule.render(mToolbar, "paneSwitcherSelectedIndexWithGtsUpdate");

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPane = mock();
                    when(mPane.getColorScheme()).thenReturn(HubColorScheme.INCOGNITO);
                    mFocusedPaneSupplier.set(mPane);
                });
        mRenderTestRule.render(mToolbar, "paneSwitcherIncognitoWithGtsUpdate");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @DisabledTest(message = "https://crbug.com/419357373")
    public void testHideMenuButton() throws Exception {
        FullButtonData actionButtonData = enabledButtonData(R.drawable.new_tab_icon);
        List<FullButtonData> paneSwitcherButtonData = new ArrayList<>();
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.new_tab_icon));
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.incognito_small));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, actionButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, false);
                    mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 0);
                    mPropertyModel.set(
                            HubToolbarProperties.PANE_SWITCHER_BUTTON_DATA, paneSwitcherButtonData);
                });
        mRenderTestRule.render(mToolbar, "menuButtonHidden");
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @DisabledTest(message = "https://crbug.com/419357373")
    public void testTabSwitcherDrawable_toggleNotificationStatus() throws Exception {
        TabSwitcherDrawable tabSwitcherDrawable =
                TabSwitcherDrawable.createTabSwitcherDrawable(
                        mActivity,
                        BrandedColorScheme.APP_DEFAULT,
                        TabSwitcherDrawableLocation.HUB_TOOLBAR);
        tabSwitcherDrawable.addTabSwitcherDrawableObserver(mTabSwitcherDrawableObserver);
        tabSwitcherDrawable.updateForTabCount(/* tabCount= */ 1, /* incognito= */ false);
        tabSwitcherDrawable.setNotificationIconStatus(/* shouldShow= */ true);
        verify(mTabSwitcherDrawableObserver, times(2)).onDrawableStateChanged();

        FullButtonData actionButtonData = enabledButtonData(R.drawable.new_tab_icon);
        List<FullButtonData> paneSwitcherButtonData = new ArrayList<>();
        paneSwitcherButtonData.add(makeButtonData(tabSwitcherDrawable, () -> {}));
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.incognito_small));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, actionButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 0);
                    mPropertyModel.set(
                            HubToolbarProperties.PANE_SWITCHER_BUTTON_DATA, paneSwitcherButtonData);
                });
        mRenderTestRule.render(mToolbar, "onGTSTabSwitcherDrawableNotificationOn");

        tabSwitcherDrawable.setNotificationIconStatus(/* shouldShow= */ false);
        verify(mTabSwitcherDrawableObserver, times(3)).onDrawableStateChanged();
        mRenderTestRule.render(mToolbar, "onGTSTabSwitcherDrawableNotificationOff");
        tabSwitcherDrawable.removeTabSwitcherDrawableObserver(mTabSwitcherDrawableObserver);
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @DisabledTest(message = "https://crbug.com/419357373")
    public void testTabSwitcherDrawable_toggleNotificationStatusIncognito() throws Exception {
        TabSwitcherDrawable tabSwitcherDrawable =
                TabSwitcherDrawable.createTabSwitcherDrawable(
                        mActivity,
                        BrandedColorScheme.INCOGNITO,
                        TabSwitcherDrawableLocation.HUB_TOOLBAR);
        tabSwitcherDrawable.addTabSwitcherDrawableObserver(mTabSwitcherDrawableObserver);
        tabSwitcherDrawable.updateForTabCount(/* tabCount= */ 1, /* incognito= */ true);
        tabSwitcherDrawable.setNotificationIconStatus(/* shouldShow= */ true);
        verify(mTabSwitcherDrawableObserver, times(2)).onDrawableStateChanged();

        FullButtonData actionButtonData = enabledButtonData(R.drawable.new_tab_icon);
        List<FullButtonData> paneSwitcherButtonData = new ArrayList<>();
        paneSwitcherButtonData.add(makeButtonData(tabSwitcherDrawable, () -> {}));
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.incognito_small));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, actionButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 1);
                    mPropertyModel.set(
                            HubToolbarProperties.PANE_SWITCHER_BUTTON_DATA, paneSwitcherButtonData);
                    mPane = mock();
                    when(mPane.getColorScheme()).thenReturn(HubColorScheme.INCOGNITO);
                    mFocusedPaneSupplier.set(mPane);
                });
        mRenderTestRule.render(mToolbar, "onIncognitoTabSwitcherDrawableNotificationOn");

        tabSwitcherDrawable.setNotificationIconStatus(/* shouldShow= */ false);
        verify(mTabSwitcherDrawableObserver, times(3)).onDrawableStateChanged();
        mRenderTestRule.render(mToolbar, "onIncognitoTabSwitcherDrawableNotificationOff");
        tabSwitcherDrawable.removeTabSwitcherDrawableObserver(mTabSwitcherDrawableObserver);
    }

    @Test
    @MediumTest
    @Feature({"RenderTest"})
    @DisabledTest(message = "https://crbug.com/419357373")
    public void testSearchBox() throws Exception {
        FullButtonData actionButtonData = enabledButtonData(R.drawable.new_tab_icon);
        List<FullButtonData> paneSwitcherButtonData = new ArrayList<>();
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.new_tab_icon));
        paneSwitcherButtonData.add(enabledButtonData(R.drawable.incognito_small));

        ThreadUtils.runOnUiThreadBlocking(
                () -> {
                    mPropertyModel.set(HubToolbarProperties.ACTION_BUTTON_DATA, actionButtonData);
                    mPropertyModel.set(HubToolbarProperties.MENU_BUTTON_VISIBLE, true);
                    mPropertyModel.set(HubToolbarProperties.PANE_SWITCHER_INDEX, 0);
                    mPropertyModel.set(
                            HubToolbarProperties.PANE_SWITCHER_BUTTON_DATA, paneSwitcherButtonData);
                    mPropertyModel.set(HubToolbarProperties.SEARCH_BOX_VISIBLE, true);
                    mPropertyModel.set(HubToolbarProperties.IS_INCOGNITO, false);
                });
        mRenderTestRule.render(mToolbar, "searchBox");
    }
}
