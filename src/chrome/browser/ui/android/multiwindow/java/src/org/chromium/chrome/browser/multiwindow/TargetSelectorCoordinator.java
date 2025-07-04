// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.multiwindow;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.res.Resources;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ListView;

import androidx.annotation.StringRes;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.util.TimeTextResolver;
import org.chromium.components.favicon.LargeIconBridge;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.MVCListAdapter.ListItem;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.ModelListAdapter;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.SimpleRecyclerViewAdapter;

import java.util.Iterator;
import java.util.List;

/** Coordinator to construct the move target selector dialog. */
@NullMarked
public class TargetSelectorCoordinator {
    private static final int TYPE_ENTRY = 0;

    // Last selector dialog instance. This is used to prevent the user from interacting with
    // multiple instances of selector UI.
    @SuppressLint("StaticFieldLeak")
    static @Nullable TargetSelectorCoordinator sPrevInstance;

    private final Context mContext;
    private final Callback<InstanceInfo> mMoveCallback;

    private final ModelList mModelList = new ModelList();
    private final UiUtils mUiUtils;
    private final View mDialogView;
    private final ModalDialogManager mModalDialogManager;

    private @Nullable PropertyModel mDialog;
    private @Nullable InstanceInfo mSelectedItem;
    private int mCurrentId; // ID for the current instance.

    /**
     * Show 'move window' modal dialog UI.
     *
     * @param context Context to use to build the dialog.
     * @param modalDialogManager {@link ModalDialogManager} object.
     * @param iconBridge An object that fetches favicons from local DB.
     * @param moveCallback Action to take when asked to open a chosen instance.
     * @param instanceInfo List of {@link InstanceInfo} for available Chrome instances.
     */
    public static void showDialog(
            Context context,
            ModalDialogManager modalDialogManager,
            LargeIconBridge iconBridge,
            Callback<InstanceInfo> moveCallback,
            List<InstanceInfo> instanceInfo,
            @StringRes int titleId) {
        new TargetSelectorCoordinator(context, modalDialogManager, iconBridge, moveCallback)
                .showDialog(instanceInfo, titleId);
    }

    private TargetSelectorCoordinator(
            Context context,
            ModalDialogManager modalDialogManager,
            LargeIconBridge iconBridge,
            Callback<InstanceInfo> moveCallback) {
        mContext = context;
        mModalDialogManager = modalDialogManager;
        mMoveCallback = moveCallback;
        mUiUtils = new UiUtils(mContext, iconBridge);

        if (UiUtils.isInstanceSwitcherV2Enabled()) {

            var adapter = new SimpleRecyclerViewAdapter(mModelList);

            adapter.registerType(
                    TYPE_ENTRY,
                    parentView ->
                            LayoutInflater.from(mContext)
                                    .inflate(R.layout.instance_switcher_item_v2, null),
                    TargetSelectorItemViewBinder::bind);

            mDialogView =
                    LayoutInflater.from(context).inflate(R.layout.target_selector_dialog_v2, null);

            int itemVerticalSpacing =
                    mContext.getResources()
                            .getDimensionPixelSize(
                                    R.dimen.instance_switcher_dialog_list_item_padding);
            var itemDecoration = new DialogListItemDecoration(itemVerticalSpacing);

            RecyclerView availableTargetsList = mDialogView.findViewById(R.id.targets_list);
            availableTargetsList.setLayoutManager(
                    new LinearLayoutManager(mContext, LinearLayoutManager.VERTICAL, false));
            availableTargetsList.setAdapter(adapter);
            availableTargetsList.addItemDecoration(itemDecoration);
        } else {
            ModelListAdapter adapter = new ModelListAdapter(mModelList);
            adapter.registerType(
                    TYPE_ENTRY,
                    parentView ->
                            LayoutInflater.from(mContext)
                                    .inflate(R.layout.instance_switcher_item, null),
                    TargetSelectorItemViewBinder::bind);
            mDialogView =
                    LayoutInflater.from(context).inflate(R.layout.target_selector_dialog, null);
            ((ListView) mDialogView.findViewById(R.id.list_view)).setAdapter(adapter);
        }
    }

    private void showDialog(List<InstanceInfo> items, @StringRes int titleId) {
        UiUtils.closeOpenDialogs();
        sPrevInstance = this;
        mDialog = createDialog(items, titleId);
        mModalDialogManager.showDialog(mDialog, ModalDialogType.APP);
    }

    private PropertyModel createDialog(List<InstanceInfo> items, @StringRes int titleId) {
        for (InstanceInfo info : items) {
            if (info.type == InstanceInfo.Type.CURRENT) {
                mSelectedItem = info;
                mCurrentId = info.instanceId;
            }
            if ((UiUtils.isInstanceSwitcherV2Enabled() && info.type != InstanceInfo.Type.CURRENT)
                    || !UiUtils.isInstanceSwitcherV2Enabled()) {
                PropertyModel itemModel = generateListItem(info);
                mModelList.add(new ModelListAdapter.ListItem(0, itemModel));
            }
        }
        ModalDialogProperties.Controller controller =
                new ModalDialogProperties.Controller() {
                    @Override
                    public void onDismiss(
                            PropertyModel model, @DialogDismissalCause int dismissalCause) {
                        sPrevInstance = null;
                    }

                    @Override
                    public void onClick(PropertyModel model, int buttonType) {
                        switch (buttonType) {
                            case ModalDialogProperties.ButtonType.POSITIVE:
                                dismissDialog(DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
                                assumeNonNull(mSelectedItem);
                                mMoveCallback.onResult(mSelectedItem);
                                break;
                            case ModalDialogProperties.ButtonType.NEGATIVE:
                                dismissDialog(DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
                                break;
                            default:
                        }
                    }
                };
        Resources resources = mContext.getResources();
        String title = mContext.getString(titleId);
        return new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                .with(ModalDialogProperties.CONTROLLER, controller)
                .with(ModalDialogProperties.CANCEL_ON_TOUCH_OUTSIDE, true)
                .with(ModalDialogProperties.CUSTOM_VIEW, mDialogView)
                .with(ModalDialogProperties.TITLE, title)
                .with(ModalDialogProperties.POSITIVE_BUTTON_DISABLED, true)
                .with(
                        ModalDialogProperties.BUTTON_STYLES,
                        ModalDialogProperties.ButtonStyles.PRIMARY_FILLED_NEGATIVE_OUTLINE)
                .with(
                        ModalDialogProperties.POSITIVE_BUTTON_TEXT,
                        resources,
                        UiUtils.isInstanceSwitcherV2Enabled()
                                ? R.string.move
                                : R.string.target_selector_move)
                .with(ModalDialogProperties.NEGATIVE_BUTTON_TEXT, resources, R.string.cancel)
                .with(
                        ModalDialogProperties.DIALOG_STYLES,
                        ModalDialogProperties.DialogStyles.DIALOG_WHEN_LARGE)
                .build();
    }

    void dismissDialog(@DialogDismissalCause int cause) {
        mModalDialogManager.dismissDialog(mDialog, cause);
    }

    private PropertyModel generateListItem(InstanceInfo item) {
        String title = mUiUtils.getItemTitle(item);
        String desc = mUiUtils.getItemDesc(item);
        PropertyModel.Builder builder =
                new PropertyModel.Builder(TargetSelectorItemProperties.ALL_KEYS)
                        .with(TargetSelectorItemProperties.TITLE, title)
                        .with(TargetSelectorItemProperties.DESC, desc)
                        .with(TargetSelectorItemProperties.INSTANCE_ID, item.instanceId)
                        .with(
                                TargetSelectorItemProperties.CLICK_LISTENER,
                                (view) -> selectInstance(item));

        if (UiUtils.isInstanceSwitcherV2Enabled()) {
            String lastAccessedString =
                    TimeTextResolver.resolveTimeAgoText(
                            mContext.getResources(), item.lastAccessedTime);
            builder.with(TargetSelectorItemProperties.LAST_ACCESSED, lastAccessedString);
            builder.with(TargetSelectorItemProperties.IS_SELECTED, false);
        } else {
            builder.with(
                    TargetSelectorItemProperties.CHECK_TARGET,
                    item.type == InstanceInfo.Type.CURRENT);
        }
        PropertyModel model = builder.build();
        mUiUtils.setFavicon(model, TargetSelectorItemProperties.FAVICON, item);
        return model;
    }

    private void selectInstance(InstanceInfo clickedItem) {
        int instanceId = clickedItem.instanceId;
        assumeNonNull(mSelectedItem);
        if (mSelectedItem.instanceId == instanceId) return;
        // Do not allow the target to be the current one.
        assumeNonNull(mDialog);
        mDialog.set(ModalDialogProperties.POSITIVE_BUTTON_DISABLED, mCurrentId == instanceId);
        Iterator<ListItem> it = mModelList.iterator();
        while (it.hasNext()) {
            ListItem li = it.next();
            int id = li.model.get(TargetSelectorItemProperties.INSTANCE_ID);
            if (UiUtils.isInstanceSwitcherV2Enabled()) {
                li.model.set(TargetSelectorItemProperties.IS_SELECTED, id == instanceId);
                mDialog.set(ModalDialogProperties.POSITIVE_BUTTON_DISABLED, false);
            } else {
                if (id == mSelectedItem.instanceId) {
                    li.model.set(TargetSelectorItemProperties.CHECK_TARGET, false);
                } else if (id == instanceId) {
                    li.model.set(TargetSelectorItemProperties.CHECK_TARGET, true);
                }
            }
        }
        mSelectedItem = clickedItem;
    }
}
