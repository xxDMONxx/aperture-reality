/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * ApertureReality Live USB Telemetry & Remote Control BroadcastReceiver
 */

package com.igalia.wolvic.telemetry;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import com.igalia.wolvic.PlatformActivity;

public class ApertureControlReceiver extends BroadcastReceiver {
    private static final String LOGTAG = "ApertureControlReceiver";

    public static final String ACTION_GET_POSITIONS = "com.aperturereality.GET_POSITIONS";
    public static final String ACTION_SET_CAMERA_POS = "com.aperturereality.SET_CAMERA_POS";
    public static final String ACTION_SET_CONFIG = "com.aperturereality.SET_CONFIG";
    public static final String ACTION_RECENTER = "com.aperturereality.RECENTER";

    private final PlatformActivity mActivity;

    public ApertureControlReceiver(PlatformActivity activity) {
        this.mActivity = activity;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null || mActivity == null) {
            return;
        }

        String action = intent.getAction();
        Log.i(LOGTAG, "Received USB ADB Command: " + action);

        switch (action) {
            case ACTION_GET_POSITIONS:
                mActivity.logCurrentTelemetry();
                break;

            case ACTION_SET_CAMERA_POS:
                float x = intent.getFloatExtra("x", 0.0f);
                float y = intent.getFloatExtra("y", 1.55f);
                float z = intent.getFloatExtra("z", 3.0f);
                Log.i(LOGTAG, "Setting Camera Offset: (" + x + ", " + y + ", " + z + ")");
                mActivity.applyCameraPosOffset(x, y, z);
                break;

            case ACTION_SET_CONFIG:
                if (intent.hasExtra("fov")) {
                    float fov = intent.getFloatExtra("fov", 60.0f);
                    Log.i(LOGTAG, "Setting Screen FOV: " + fov);
                    mActivity.applyScreenFOV(fov);
                }
                if (intent.hasExtra("ipd")) {
                    float ipd = intent.getFloatExtra("ipd", 0.064f);
                    Log.i(LOGTAG, "Setting IPD: " + ipd);
                    mActivity.applyIPD(ipd);
                }
                break;

            case ACTION_RECENTER:
                Log.i(LOGTAG, "Triggering Yaw Recenter");
                mActivity.triggerRecenterYaw();
                break;

            default:
                Log.w(LOGTAG, "Unknown action: " + action);
                break;
        }
    }
}
