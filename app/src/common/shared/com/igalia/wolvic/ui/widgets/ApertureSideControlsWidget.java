/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * ApertureReality 3D Side Control Panel Widget for Window Attachment
 */

package com.igalia.wolvic.ui.widgets;

import android.content.Context;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.widget.SeekBar;
import android.widget.TextView;

import com.igalia.wolvic.PlatformActivity;
import com.igalia.wolvic.R;

public class ApertureSideControlsWidget extends UIWidget {
    private TextView mBtnRecenter;
    private TextView mTxtIpdValue;
    private TextView mBtnIpdMinus;
    private TextView mBtnIpdPlus;
    private TextView mBtnQrCardboard;
    private SeekBar mSeekBarIpd;

    private float mCurrentIpdMM = 64.0f; // Default 64mm = 0.064m

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
        aPlacement.width = WidgetPlacement.dpDimension(getContext(), R.dimen.side_controls_width);
        aPlacement.height = WidgetPlacement.dpDimension(getContext(), R.dimen.side_controls_height);
        aPlacement.worldWidth = WidgetPlacement.floatDimension(getContext(), R.dimen.side_controls_world_width);
        aPlacement.visible = true;
        aPlacement.cylinder = true;
        aPlacement.name = "ApertureSideControls";
    }

    protected void initUI() {
        LayoutInflater inflater = (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        inflater.inflate(R.layout.aperture_side_controls, this, true);

        mBtnRecenter = findViewById(R.id.btn_recenter);
        mTxtIpdValue = findViewById(R.id.txt_ipd_value);
        mBtnIpdMinus = findViewById(R.id.btn_ipd_minus);
        mBtnIpdPlus = findViewById(R.id.btn_ipd_plus);
        mSeekBarIpd = findViewById(R.id.seekbar_ipd);

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
                updateSeekBarFromValue();
                applyIpd();
            });
        }

        mBtnQrCardboard = findViewById(R.id.btn_qr_cardboard);
        if (mBtnQrCardboard != null) {
            mBtnQrCardboard.setOnClickListener(v -> {
                if (getContext() instanceof PlatformActivity) {
                    ((PlatformActivity) getContext()).promptCardboardQrScanner(newIpdMM -> {
                        mCurrentIpdMM = newIpdMM;
                        updateSeekBarFromValue();
                        applyIpd();
                    });
                }
            });
        }
    }

    private void updateSeekBarFromValue() {
        if (mSeekBarIpd != null) {
            int progress = Math.round((mCurrentIpdMM - 54.0f) * 10.0f);
            mSeekBarIpd.setProgress(progress);
        }
    }

    private void updateIpdUI() {
        if (mTxtIpdValue != null) {
            mTxtIpdValue.setText(String.format("IPD: %.1f mm", mCurrentIpdMM));
        }
        updateSeekBarFromValue();
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

            mWidgetPlacement.worldWidth = Math.max(0.40f, windowWorldWidth * 0.35f);
            mWidgetPlacement.width = Math.max(360, (int) (windowWidthPx * 0.35f));
            mWidgetPlacement.height = Math.max(260, (int) (windowHeightPx * 0.45f));
        }

        if (mWidgetManager != null) {
            mWidgetManager.addWidget(this);
        }
    }

    @Override
    public void onDismiss() {
    }
}
