/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package com.igalia.wolvic;

import androidx.activity.ComponentActivity;
import android.content.Context;
import android.content.Intent;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.TextView;

import com.igalia.wolvic.ui.widgets.WidgetManagerDelegate;
import com.igalia.wolvic.utils.SystemUtils;

import java.util.ArrayList;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import com.google.zxing.integration.android.IntentIntegrator;
import com.google.zxing.integration.android.IntentResult;
import android.Manifest;
import android.content.pm.PackageManager;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import com.igalia.wolvic.utils.CardboardQrDecoder;
import android.widget.Toast;

public class PlatformActivity extends ComponentActivity implements SensorEventListener {
    static String LOGTAG = SystemUtils.createLogtag(PlatformActivity.class);

    @SuppressWarnings("unused")
    public static boolean filterPermission(final String aPermission) {
        return false;
    }

    public static boolean isNotSpecialKey(KeyEvent event) {
        return true;
    }

    public static boolean isPositionTrackingSupported() {
        return false;
    }

    protected Intent getStoreIntent() {
        return null;
    }

    protected String getEyeTrackingPermissionString() { return null; }

    private GLSurfaceView mView;
    private TextView mFrameRate;
    private final ArrayList<Runnable> mPendingEvents = new ArrayList<>();
    private boolean mSurfaceCreated = false;
    private int mFrameCount;
    private long mLastFrameTime = System.currentTimeMillis();

    private SensorManager mSensorManager;
    private Sensor mSelectedSensor;
    private boolean mSensorRegistered = false;
    private com.igalia.wolvic.telemetry.ApertureControlReceiver mApertureReceiver;

    final Object mRenderLock = new Object();

    private final Runnable activityDestroyedRunnable = () -> {
        synchronized (mRenderLock) {
            activityDestroyed();
            mRenderLock.notifyAll();
        }
    };

    private final Runnable activityPausedRunnable = () -> {
        synchronized (mRenderLock) {
            activityPaused();
            mRenderLock.notifyAll();
        }
    };

    private final Runnable activityResumedRunnable = this::activityResumed;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.d(LOGTAG, "Universal3DOF PlatformActivity onCreate");
        super.onCreate(savedInstanceState);
        // Force landscape orientation for 3DOF mobile VR (allows both Landscape Left and Right)
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        setContentView(R.layout.noapi_layout);
        mFrameRate = findViewById(R.id.frame_rate_text);
        if (mFrameRate != null) {
            mFrameRate.setVisibility(View.GONE);
        }
        mView = findViewById(R.id.gl_view);
        mView.setEGLContextClientVersion(3);
        mView.setEGLConfigChooser(8, 8, 8, 0, 16, 0);

        mView.setRenderer(
                new GLSurfaceView.Renderer() {
                    @Override
                    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                        Log.d(LOGTAG, "Universal3DOF onSurfaceCreated");
                        activityCreated(getAssets());
                        mSurfaceCreated = true;
                        notifyPendingEvents();
                    }

                    @Override
                    public void onSurfaceChanged(GL10 gl, int width, int height) {
                        Log.d(LOGTAG, "Universal3DOF onSurfaceChanged: " + width + "x" + height);
                        updateViewport(width, height);
                    }

                    @Override
                    public void onDrawFrame(GL10 gl) {
                        mFrameCount++;
                        long ctime = System.currentTimeMillis();
                        if ((ctime - mLastFrameTime) >= 1000) {
                            final int value = Math.round(mFrameCount / ((ctime - mLastFrameTime) / 1000.0f));
                            mLastFrameTime = ctime;
                            mFrameCount = 0;
                            runOnUiThread(() -> {
                                if (mFrameRate != null) {
                                    mFrameRate.setText(String.valueOf(value));
                                }
                            });
                        }
                        drawGL();
                    }
                });

        initializeSensors();
        setupUI();
    }

    private void initializeSensors() {
        mSensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (mSensorManager == null) {
            Log.e(LOGTAG, "Universal3DOF: SensorManager unavailable!");
            return;
        }

        mSelectedSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        if (mSelectedSensor != null) {
            Log.i(LOGTAG, "Universal3DOF Sensor Selected: TYPE_ROTATION_VECTOR");
        } else {
            mSelectedSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
            if (mSelectedSensor != null) {
                Log.w(LOGTAG, "Universal3DOF Sensor Selected: TYPE_GAME_ROTATION_VECTOR (fallback, yaw drift possible without magnetometer)");
            } else {
                Log.e(LOGTAG, "Universal3DOF Sensor Error: Neither TYPE_ROTATION_VECTOR nor TYPE_GAME_ROTATION_VECTOR available on device!");
            }
        }
    }

    private boolean mIsFirstSensorEvent = true;

    private float[] fromSensorManagerToWorld(float[] eventValues) {
        float[] R_sensor = new float[9];
        SensorManager.getRotationMatrixFromVector(R_sensor, eventValues);

        int displayRotation = Surface.ROTATION_90;
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                displayRotation = getDisplay().getRotation();
            } else {
                displayRotation = getWindowManager().getDefaultDisplay().getRotation();
            }
        } catch (Exception e) {
            displayRotation = Surface.ROTATION_90;
        }

        // R_sensor rotates vectors from the Phone Portrait Frame to the ENU World Frame.
        // We first create R_cam_to_enu, mapping the Camera Local Frame to the ENU World Frame.
        float[] R_cam_to_enu = new float[9];

        if (displayRotation == Surface.ROTATION_270) {
            // Landscape Right: Top Edge points Right, Right Edge points Down.
            // Camera +X (Right) = Top Edge (+Y portrait)
            // Camera +Y (Up) = Left Edge (-X portrait)
            R_cam_to_enu[0] = R_sensor[1];
            R_cam_to_enu[3] = R_sensor[4];
            R_cam_to_enu[6] = R_sensor[7];

            R_cam_to_enu[1] = -R_sensor[0];
            R_cam_to_enu[4] = -R_sensor[3];
            R_cam_to_enu[7] = -R_sensor[6];
        } else {
            // Landscape Left (Default): Top Edge points Left, Right Edge points Up.
            // Camera +X (Right) = Bottom Edge (-Y portrait)
            // Camera +Y (Up) = Right Edge (+X portrait)
            R_cam_to_enu[0] = -R_sensor[1];
            R_cam_to_enu[3] = -R_sensor[4];
            R_cam_to_enu[6] = -R_sensor[7];

            R_cam_to_enu[1] = R_sensor[0];
            R_cam_to_enu[4] = R_sensor[3];
            R_cam_to_enu[7] = R_sensor[6];
        }
        
        // Camera +Z (Backward) = Screen (+Z portrait)
        R_cam_to_enu[2] = R_sensor[2];
        R_cam_to_enu[5] = R_sensor[5];
        R_cam_to_enu[8] = R_sensor[8];

        // Now transform from ENU World to Wolvic World.
        // Wolvic World: +X=Right, +Y=Up, -Z=Forward.
        // ENU World: +X=East, +Y=North, +Z=Sky.
        // Mapping: Wolvic +X = ENU +X. Wolvic +Y = ENU +Z. Wolvic +Z (Backward) = ENU -Y (South).
        // This is equivalent to multiplying by M_world_to_enu_inv.
        float[] R_cam_to_world = new float[9];
        
        // Row 0 of R_cam_to_world = Row 0 of R_cam_to_enu
        R_cam_to_world[0] = R_cam_to_enu[0];
        R_cam_to_world[1] = R_cam_to_enu[1];
        R_cam_to_world[2] = R_cam_to_enu[2];

        // Row 1 of R_cam_to_world = Row 2 of R_cam_to_enu
        R_cam_to_world[3] = R_cam_to_enu[6];
        R_cam_to_world[4] = R_cam_to_enu[7];
        R_cam_to_world[5] = R_cam_to_enu[8];

        // Row 2 of R_cam_to_world = - Row 1 of R_cam_to_enu
        R_cam_to_world[6] = -R_cam_to_enu[3];
        R_cam_to_world[7] = -R_cam_to_enu[4];
        R_cam_to_world[8] = -R_cam_to_enu[5];

        // Convert the exact final rotation matrix to a quaternion.
        float[] R = R_cam_to_world;
        float tr = R[0] + R[4] + R[8];
        float qw, qx, qy, qz;
        if (tr > 0) {
            float S = (float) Math.sqrt(tr + 1.0) * 2;
            qw = 0.25f * S;
            qx = (R[7] - R[5]) / S;
            qy = (R[2] - R[6]) / S;
            qz = (R[3] - R[1]) / S;
        } else if ((R[0] > R[4]) && (R[0] > R[8])) {
            float S = (float) Math.sqrt(1.0 + R[0] - R[4] - R[8]) * 2;
            qw = (R[7] - R[5]) / S;
            qx = 0.25f * S;
            qy = (R[1] + R[3]) / S;
            qz = (R[2] + R[6]) / S;
        } else if (R[4] > R[8]) {
            float S = (float) Math.sqrt(1.0 + R[4] - R[0] - R[8]) * 2;
            qw = (R[2] - R[6]) / S;
            qx = (R[1] + R[3]) / S;
            qy = 0.25f * S;
            qz = (R[5] + R[7]) / S;
        } else {
            float S = (float) Math.sqrt(1.0 + R[8] - R[0] - R[4]) * 2;
            qw = (R[3] - R[1]) / S;
            qx = (R[2] + R[6]) / S;
            qy = (R[5] + R[7]) / S;
            qz = 0.25f * S;
        }

        return new float[]{qx, qy, qz, qw};
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (mSelectedSensor == null || event.sensor.getType() != mSelectedSensor.getType()) {
            return;
        }

        final float[] q = fromSensorManagerToWorld(event.values);

        if (Float.isNaN(q[0]) || Float.isNaN(q[1]) || Float.isNaN(q[2]) || Float.isNaN(q[3])) {
            return;
        }

        if (mIsFirstSensorEvent) {
            mIsFirstSensorEvent = false;
            queueRunnable(() -> {
                setHeadOrientation(q[0], q[1], q[2], q[3]);
                recenterYaw();
            });
        } else {
            queueRunnable(() -> setHeadOrientation(q[0], q[1], q[2], q[3]));
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // No-op
    }

    @Override
    public boolean onTouchEvent(MotionEvent aEvent) {
        if (aEvent.getActionIndex() != 0) {
            return false;
        }

        int action = aEvent.getAction();
        boolean down = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE);
        if (action != MotionEvent.ACTION_DOWN && action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_MOVE) {
            return false;
        }

        final boolean isDown = down;
        final float xx = aEvent.getX(0);
        final float yy = aEvent.getY(0);
        queueRunnable(() -> touchEvent(isDown, xx, yy));
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        Log.d(LOGTAG, "Universal3DOF onKeyDown: " + keyCode);
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            // Trigger Yaw recentering on Volume Down press
            queueRunnable(this::recenterYaw);
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
            keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
            keyCode == KeyEvent.KEYCODE_SPACE || keyCode == KeyEvent.KEYCODE_HEADSETHOOK) {
            queueRunnable(() -> keyEvent(keyCode, true));
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        Log.d(LOGTAG, "Universal3DOF onKeyUp: " + keyCode);
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_BUTTON_SELECT ||
            keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
            keyCode == KeyEvent.KEYCODE_SPACE || keyCode == KeyEvent.KEYCODE_HEADSETHOOK) {
            queueRunnable(() -> keyEvent(keyCode, false));
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        // Support external Mouse / Touchpad / Gamepad Axis motion
        int action = event.getAction();
        if (action == MotionEvent.ACTION_HOVER_MOVE || action == MotionEvent.ACTION_MOVE) {
            final float x = event.getX();
            final float y = event.getY();
            queueRunnable(() -> touchEvent(false, x, y));
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    public final PlatformActivityPlugin createPlatformPlugin(WidgetManagerDelegate delegate) { return null; }

    @Override
    protected void onPause() {
        Log.d(LOGTAG, "Universal3DOF PlatformActivity onPause");

        if (mApertureReceiver != null) {
            try {
                unregisterReceiver(mApertureReceiver);
            } catch (Exception e) {
                // Ignore
            }
            mApertureReceiver = null;
        }

        if (mSensorManager != null && mSensorRegistered) {
            mSensorManager.unregisterListener(this);
            mSensorRegistered = false;
            Log.d(LOGTAG, "Universal3DOF Sensor unregistered");
        }

        synchronized (mRenderLock) {
            queueRunnable(activityPausedRunnable);
            try {
                mRenderLock.wait();
            } catch(InterruptedException e) {
                Log.e(LOGTAG, "activityPausedRunnable interrupted: " + e.toString());
            }
        }
        mView.onPause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        Log.d(LOGTAG, "Universal3DOF PlatformActivity onResume");
        super.onResume();
        mView.onResume();
        queueRunnable(activityResumedRunnable);
        setImmersiveSticky();

        if (mSensorManager != null && mSelectedSensor != null && !mSensorRegistered) {
            mSensorRegistered = mSensorManager.registerListener(this, mSelectedSensor, SensorManager.SENSOR_DELAY_GAME);
            Log.d(LOGTAG, "Universal3DOF Sensor registered: " + mSensorRegistered);
        }

        if (mApertureReceiver == null) {
            mApertureReceiver = new com.igalia.wolvic.telemetry.ApertureControlReceiver(this);
            android.content.IntentFilter filter = new android.content.IntentFilter();
            filter.addAction(com.igalia.wolvic.telemetry.ApertureControlReceiver.ACTION_GET_POSITIONS);
            filter.addAction(com.igalia.wolvic.telemetry.ApertureControlReceiver.ACTION_SET_CAMERA_POS);
            filter.addAction(com.igalia.wolvic.telemetry.ApertureControlReceiver.ACTION_SET_CONFIG);
            filter.addAction(com.igalia.wolvic.telemetry.ApertureControlReceiver.ACTION_RECENTER);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                registerReceiver(mApertureReceiver, filter, Context.RECEIVER_EXPORTED);
            } else {
                registerReceiver(mApertureReceiver, filter);
            }
            Log.i(LOGTAG, "ApertureControlReceiver registered successfully");
        }
    }

    public void triggerRecenterYaw() {
        queueRunnable(this::recenterYaw);
    }

    public void applyCameraPosOffset(float x, float y, float z) {
        queueRunnable(() -> setCameraPosOffset(x, y, z));
    }

    public void applyScreenFOV(float fov) {
        queueRunnable(() -> setScreenFOV(fov));
    }

    public void applyIPD(float ipd) {
        queueRunnable(() -> setIPD(ipd));
    }

    public void logCurrentTelemetry() {
        queueRunnable(this::triggerTelemetryLog);
    }

    @Override
    protected void onDestroy() {
        Log.d(LOGTAG, "Universal3DOF PlatformActivity onDestroy");

        if (mSensorManager != null && mSensorRegistered) {
            mSensorManager.unregisterListener(this);
            mSensorRegistered = false;
        }

        super.onDestroy();
        synchronized (mRenderLock) {
            queueRunnable(activityDestroyedRunnable);
            try {
                mRenderLock.wait();
            } catch(InterruptedException e) {
                Log.e(LOGTAG, "activityDestroyedRunnable interrupted: " + e.toString());
            }
        }
    }

    void setImmersiveSticky() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow()
                    .getDecorView()
                    .setSystemUiVisibility(
                            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                     | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                     | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                     | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                     | View.SYSTEM_UI_FLAG_FULLSCREEN
                                     | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    private QrCallback mLastQrCallback;
    private static final int CAMERA_PERMISSION_REQUEST = 101;

    public interface QrCallback {
        void onIpdDecoded(float ipdMM);
    }

    public void promptCardboardQrScanner(final QrCallback callback) {
        mLastQrCallback = callback;
        runOnUiThread(() -> {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA}, CAMERA_PERMISSION_REQUEST);
            } else {
                startCameraScanner();
            }
        });
    }

    private void startCameraScanner() {
        IntentIntegrator integrator = new IntentIntegrator(this);
        integrator.setPrompt("Apunta la cámara al código QR impreso en tus gafas Cardboard / VRBox");
        integrator.setCameraId(0);
        integrator.setBeepEnabled(true);
        integrator.setBarcodeImageEnabled(false);
        integrator.setOrientationLocked(false);
        integrator.initiateScan();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == CAMERA_PERMISSION_REQUEST) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                startCameraScanner();
            } else {
                Toast.makeText(this, "Permiso de cámara denegado", Toast.LENGTH_SHORT).show();
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        IntentResult result = IntentIntegrator.parseActivityResult(requestCode, resultCode, data);
        if (result != null) {
            if (result.getContents() != null) {
                String qrUrl = result.getContents();
                CardboardQrDecoder.CardboardProfile profile = CardboardQrDecoder.parseCardboardUrl(qrUrl);
                applyCardboardProfile(profile);
            }
        } else {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    public void applyCardboardProfile(final CardboardQrDecoder.CardboardProfile profile) {
        if (profile == null) return;
        queueRunnable(() -> setFullViewerProfile(
                profile.vendor + " " + profile.model,
                profile.interLensMeters,
                profile.screenToLensMeters,
                profile.verticalAlignment,
                profile.trayToLensCenterMeters,
                (profile.fovLeft + profile.fovRight),
                profile.k1,
                profile.k2,
                profile.fovLeft,
                profile.fovRight,
                profile.fovTop,
                profile.fovBottom
        ));
        runOnUiThread(() -> {
            float ipdMM = profile.interLensMeters * 1000.0f;
            if (mLastQrCallback != null) {
                mLastQrCallback.onIpdDecoded(ipdMM);
            }
            Toast.makeText(this, String.format("Visor [%s %s] configurado: IPD=%.1fmm, Lente=%.1fmm, k1=%.2f",
                    profile.vendor, profile.model, ipdMM, profile.screenToLensMeters * 1000.0f, profile.k1), Toast.LENGTH_LONG).show();
        });
    }

    void queueRunnable(Runnable aRunnable) {
        if (mSurfaceCreated) {
            mView.queueEvent(aRunnable);
        } else {
            synchronized (mPendingEvents) {
                mPendingEvents.add(aRunnable);
            }
            if (mSurfaceCreated) {
                notifyPendingEvents();
            }
        }
    }

    private void notifyPendingEvents() {
        synchronized (mPendingEvents) {
            for (Runnable runnable: mPendingEvents) {
                mView.queueEvent(runnable);
            }
            mPendingEvents.clear();
        }
    }

    private void setupUI() {
        setImmersiveSticky();
    }

    private native void activityCreated(Object aAssetManager);
    private native void updateViewport(int width, int height);
    private native void activityPaused();
    private native void activityResumed();
    private native void activityDestroyed();
    private native void drawGL();
    private native void touchEvent(boolean aDown, float aX, float aY);
    private native void keyEvent(int aKeyCode, boolean aDown);
    private native void setHeadOrientation(float x, float y, float z, float w);
    private native void recenterYaw();
    private native void setCameraPosOffset(float x, float y, float z);
    private native void setScreenFOV(float fov);
    private native void setIPD(float ipd);
    private native void setFullViewerProfile(String name, float ipd, float eyeToScreen, int vertAlign, float trayToLens, float fov, float k1, float k2, float fovL, float fovR, float fovT, float fovB);
    private native void triggerTelemetryLog();
}
