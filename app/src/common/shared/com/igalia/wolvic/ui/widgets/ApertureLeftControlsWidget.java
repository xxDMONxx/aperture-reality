/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * ApertureReality 3D Left Distance Control Widget for Window Attachment
 */

package com.igalia.wolvic.ui.widgets;

import android.content.Context;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import com.igalia.wolvic.R;
import com.igalia.wolvic.browser.SettingsStore;

public class ApertureLeftControlsWidget extends UIWidget {
    private View mBtnCloser;
    private View mBtnFurther;
    private WindowWidget mAttachedWindow;

    public ApertureLeftControlsWidget(Context aContext) {
        super(aContext);
        initUI();
    }

    public ApertureLeftControlsWidget(Context aContext, AttributeSet aAttrs) {
        super(aContext, aAttrs);
        initUI();
    }

    public ApertureLeftControlsWidget(Context aContext, AttributeSet aAttrs, int aDefStyle) {
        super(aContext, aAttrs, aDefStyle);
        initUI();
    }

    @Override
    protected void initializeWidgetPlacement(WidgetPlacement aPlacement) {
        aPlacement.width = 120;
        aPlacement.height = 140;
        aPlacement.worldWidth = 0.15f;
        aPlacement.visible = true;
        aPlacement.cylinder = true;
        aPlacement.name = "ApertureLeftControls";
    }

    protected void initUI() {
        LayoutInflater inflater = (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        inflater.inflate(R.layout.aperture_left_controls, this, true);

        mBtnCloser = findViewById(R.id.btn_distance_closer);
        mBtnFurther = findViewById(R.id.btn_distance_further);

        if (mBtnCloser != null) {
            mBtnCloser.setOnClickListener(v -> adjustDistance(-0.12f));
        }

        if (mBtnFurther != null) {
            mBtnFurther.setOnClickListener(v -> adjustDistance(0.12f));
        }
    }

    private void adjustDistance(float delta) {
        SettingsStore settings = SettingsStore.getInstance(getContext());
        float currentDist = settings.getWindowDistance();
        float newDist = Math.max(0.0f, Math.min(1.0f, currentDist + delta));
        settings.setWindowDistance(newDist);
    }

    public void attachToWindow(WindowWidget aWindow) {
        if (aWindow == null) return;
        mAttachedWindow = aWindow;
        mWidgetPlacement.parentHandle = aWindow.getHandle();
        mWidgetPlacement.parentAnchorX = 0.0f; // Left edge of window
        mWidgetPlacement.parentAnchorY = 0.5f; // Center height
        mWidgetPlacement.anchorX = 1.0f;       // Anchor right of left control
        mWidgetPlacement.anchorY = 0.5f;
        mWidgetPlacement.translationX = -0.06f; // 6cm offset to left
        mWidgetPlacement.translationY = 0.0f;
        mWidgetPlacement.translationZ = 0.0f;
        mWidgetPlacement.visible = true;
        mWidgetPlacement.cylinder = true;

        if (aWindow.getPlacement() != null) {
            float windowWorldWidth = aWindow.getPlacement().worldWidth;
            int windowWidthPx = aWindow.getPlacement().width;
            int windowHeightPx = aWindow.getPlacement().height;

            mWidgetPlacement.worldWidth = Math.max(0.28f, windowWorldWidth * 0.18f);
            mWidgetPlacement.width = Math.max(220, (int) (windowWidthPx * 0.18f));
            mWidgetPlacement.height = Math.max(260, (int) (windowHeightPx * 0.38f));
        } else {
            mWidgetPlacement.worldWidth = 0.28f;
            mWidgetPlacement.width = 220;
            mWidgetPlacement.height = 260;
        }

        if (mWidgetManager != null) {
            mWidgetManager.addWidget(this);
            mWidgetManager.updateWidget(this);
        }
    }

    @Override
    public void updatePlacementTranslationZ() {
        getPlacement().translationZ = 0.0f;
    }

    @Override
    public void onDismiss() {
    }
}
