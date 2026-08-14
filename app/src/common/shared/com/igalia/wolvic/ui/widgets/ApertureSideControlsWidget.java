/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * ApertureReality 3D Side Control Panel Widget for Window Attachment
 */

package com.igalia.wolvic.ui.widgets;

import android.content.Context;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.igalia.wolvic.PlatformActivity;
import com.igalia.wolvic.R;

public class ApertureSideControlsWidget extends UIWidget {
    private TextView mBtnTogglePanel;
    private LinearLayout mControlsContainer;
    private TextView mBtnRecenter;
    private TextView mTxtIpdValue;
    private TextView mBtnIpdMinus;
    private TextView mBtnIpdPlus;
    private SeekBar mSeekBarIpd;

    private float mCurrentIpdMM = 60.7f; // Default 60.7mm = 0.0607m
    private boolean mIsExpanded = false;
    private WindowWidget mAttachedWindow;

    public ApertureSideControlsWidget(Context aContext) {
        super(aContext);
        initUI();
    }

    public ApertureSideControlsWidget(Context aContext, AttributeSet aAttrs) {
        super(aContext, aAttrs);
        initUI();
    }

    public ApertureSideControlsWidget(Context aContext, AttributeSet aAttrs, int aDefStyle) {
        super(aContext, aAttrs, aDefStyle);
        initUI();
    }

    @Override
    protected void initializeWidgetPlacement(WidgetPlacement aPlacement) {
        aPlacement.width = 160;
        aPlacement.height = 60;
        aPlacement.worldWidth = 0.18f;
        aPlacement.visible = true;
        aPlacement.cylinder = true;
        aPlacement.name = "ApertureSideControls";
    }

    protected void initUI() {
        LayoutInflater inflater = (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        inflater.inflate(R.layout.aperture_side_controls, this, true);

        mBtnTogglePanel = findViewById(R.id.btn_toggle_panel);
        mControlsContainer = findViewById(R.id.panel_controls_container);
        mBtnRecenter = findViewById(R.id.btn_recenter);
        mTxtIpdValue = findViewById(R.id.txt_ipd_value);
        mBtnIpdMinus = findViewById(R.id.btn_ipd_minus);
        mBtnIpdPlus = findViewById(R.id.btn_ipd_plus);
        mSeekBarIpd = findViewById(R.id.seekbar_ipd);

        if (mBtnTogglePanel != null) {
            mBtnTogglePanel.setOnClickListener(v -> {
                mIsExpanded = !mIsExpanded;
                updateExpandedState();
            });
        }

        if (mBtnRecenter != null) {
            mBtnRecenter.setOnClickListener(v -> {
                if (getContext() instanceof PlatformActivity) {
                    ((PlatformActivity) getContext()).triggerRecenterYaw();
                }
            });
        }

        updateIpdUI();

        if (mSeekBarIpd != null) {
            mSeekBarIpd.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override
                public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                    if (fromUser) {
                        mCurrentIpdMM = 54.0f + (progress / 10.0f);
                        applyIpd();
                    }
                }

                @Override public void onStartTrackingTouch(SeekBar seekBar) {}
                @Override public void onStopTrackingTouch(SeekBar seekBar) {}
            });
        }

        if (mBtnIpdMinus != null) {
            mBtnIpdMinus.setOnClickListener(v -> {
                mCurrentIpdMM = Math.max(54.0f, mCurrentIpdMM - 0.5f);
                applyIpd();
            });
        }

        if (mBtnIpdPlus != null) {
            mBtnIpdPlus.setOnClickListener(v -> {
                mCurrentIpdMM = Math.min(74.0f, mCurrentIpdMM + 0.5f);
                applyIpd();
            });
        }

        updateExpandedState();
    }

    private void updateExpandedState() {
        if (mControlsContainer != null) {
            mControlsContainer.setVisibility(mIsExpanded ? View.VISIBLE : View.GONE);
        }
        if (mBtnTogglePanel != null) {
            mBtnTogglePanel.setText(mIsExpanded ? "⚙️ Ocultar 3D" : "⚙️ 3D / IPD");
        }
        if (mAttachedWindow != null) {
            attachToWindow(mAttachedWindow);
        }
    }

    private void updateIpdUI() {
        if (mTxtIpdValue != null) {
            mTxtIpdValue.setText(String.format("IPD: %.1f mm", mCurrentIpdMM));
        }
        if (mSeekBarIpd != null) {
            int progress = Math.round((mCurrentIpdMM - 54.0f) * 10.0f);
            mSeekBarIpd.setProgress(progress);
        }
    }

    private void applyIpd() {
        updateIpdUI();
        float ipdMeters = mCurrentIpdMM / 1000.0f;
        if (getContext() instanceof PlatformActivity) {
            ((PlatformActivity) getContext()).applyIPD(ipdMeters);
        }
    }

    public void attachToWindow(WindowWidget aWindow) {
        if (aWindow == null) return;
        mAttachedWindow = aWindow;
        mWidgetPlacement.parentHandle = aWindow.getHandle();
        mWidgetPlacement.parentAnchorX = 1.0f; // Right edge of window
        mWidgetPlacement.parentAnchorY = 0.5f; // Center height
        mWidgetPlacement.anchorX = 0.0f;       // Anchor left of side control
        mWidgetPlacement.anchorY = 0.5f;
        mWidgetPlacement.translationX = 0.06f; // 6cm offset to right
        mWidgetPlacement.translationY = 0.0f;
        mWidgetPlacement.translationZ = 0.0f;
        mWidgetPlacement.visible = true;
        mWidgetPlacement.cylinder = true;

        if (aWindow.getPlacement() != null) {
            float windowWorldWidth = aWindow.getPlacement().worldWidth;
            int windowWidthPx = aWindow.getPlacement().width;
            int windowHeightPx = aWindow.getPlacement().height;

            if (mIsExpanded) {
                mWidgetPlacement.worldWidth = Math.max(0.50f, windowWorldWidth * 0.40f);
                mWidgetPlacement.width = Math.max(460, (int) (windowWidthPx * 0.40f));
                mWidgetPlacement.height = Math.max(400, (int) (windowHeightPx * 0.65f));
            } else {
                mWidgetPlacement.worldWidth = Math.max(0.32f, windowWorldWidth * 0.22f);
                mWidgetPlacement.width = Math.max(260, (int) (windowWidthPx * 0.22f));
                mWidgetPlacement.height = Math.max(100, (int) (windowHeightPx * 0.15f));
            }
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
