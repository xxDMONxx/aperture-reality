/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DEVICE_DELEGATE_3DOF_DOT_H
#define DEVICE_DELEGATE_3DOF_DOT_H

#include "vrb/Forward.h"
#include "vrb/MacroUtils.h"
#include "DeviceDelegate.h"

#include <jni.h>
#include <memory>
#include <string>

namespace crow {

struct ViewerProfile {
  std::string name;
  float ipd = 0.064f;                     // Inter-lens / Interpupillary distance in meters
  float eyeToScreenDistance = 0.039f;    // Screen-to-lens distance in meters
  int verticalAlignment = 0;             // 0 = BOTTOM, 1 = CENTER, 2 = TOP
  float trayToLensCenterDistance = 0.035f;// Tray to lens-center distance in meters
  float screenFOV = 75.0f;               // Field of View in degrees
  float k1 = 0.340f;                      // Radial distortion coefficient 1
  float k2 = 0.550f;                      // Radial distortion coefficient 2
  float fovLeft = 50.0f;                  // Outer FOV half-angle degrees
  float fovRight = 50.0f;                 // Inner FOV half-angle degrees
  float fovTop = 50.0f;                   // Top FOV half-angle degrees
  float fovBottom = 50.0f;                // Bottom FOV half-angle degrees

  static ViewerProfile CardboardV1() {
    return {"Google Cardboard V1", 0.064f, 0.042f, 0, 0.035f, 60.0f, 0.441f, 0.156f, 40.0f, 40.0f, 40.0f, 40.0f};
  }
  static ViewerProfile CardboardV2() {
    return {"Google Cardboard V2", 0.064f, 0.039f, 0, 0.035f, 80.0f, 0.340f, 0.550f, 50.0f, 50.0f, 50.0f, 50.0f};
  }
  static ViewerProfile DaydreamView() {
    return {"Google Daydream View", 0.064f, 0.041f, 1, 0.035f, 90.0f, 0.215f, 0.215f, 55.0f, 55.0f, 55.0f, 55.0f};
  }
};

class DeviceDelegate3DOF;
typedef std::shared_ptr<DeviceDelegate3DOF> DeviceDelegate3DOFPtr;

class DeviceDelegate3DOF : public DeviceDelegate {
public:
  static DeviceDelegate3DOFPtr Create(vrb::RenderContextPtr& aContext);

  // DeviceDelegate interface
  void SetRenderMode(const device::RenderMode aMode) override;
  device::RenderMode GetRenderMode() override;
  void RegisterImmersiveDisplay(ImmersiveDisplayPtr aDisplay) override;
  GestureDelegateConstPtr GetGestureDelegate() override;
  vrb::CameraPtr GetCamera(const device::Eye aEye) override;
  const vrb::Matrix& GetHeadTransform() const override;
  const vrb::Matrix& GetReorientTransform() const override;
  void SetReorientTransform(const vrb::Matrix& aMatrix) override;
  void Reorient(const vrb::Matrix&, ReorientMode) override;
  void SetClearColor(const vrb::Color& aColor) override;
  void SetClipPlanes(const float aNear, const float aFar) override;
  void SetControllerDelegate(ControllerDelegatePtr& aController) override;
  void ReleaseControllerDelegate() override;
  int32_t GetControllerModelCount() const override;
  const std::string GetControllerModelName(const int32_t aModelIndex) const override;
  void ProcessEvents() override;
  void StartFrame(const FramePrediction aPrediction) override;
  void BindEye(const device::Eye aEye) override;
  void EndFrame(const FrameEndMode aMode) override;
  bool IsInGazeMode() const override { return true; }
  int32_t GazeModeIndex() const override { return 0; }

  // DeviceDelegate3DOF specific interface
  void InitializeJava(JNIEnv* aEnv, jobject aActivity);
  void ShutdownJava();
  void SetViewport(const int aWidth, const int aHeight);
  void Pause();
  void Resume();
  void TouchEvent(const bool aDown, const float aX, const float aY);
  void KeyEvent(const int32_t aKeyCode, const bool aDown);

  // Sensor & Orientation tracking interface
  void SetHeadOrientation(const float aX, const float aY, const float aZ, const float aW);
  void RecenterYaw();

  // ViewerProfile & Stereo Camera interface
  void SetViewerProfile(const ViewerProfile& aProfile);
  const ViewerProfile& GetViewerProfile() const;
  void SetPhysicalScreenDimensions(float widthMeters, float heightMeters, float xdpi, float ydpi);
  void SetDistortionTestMode(int mode); // 0 = OFF, 1 = NORMAL, 2 = INVERSE
  void SetEyeSwapTestMode(int mode);     // 0 = NORMAL, 1 = SWAPPED
  void SetCalibrationGridMode(bool enabled);
  void SetIPD(const float aIPD);
  void SetFOV(const float aFOV);
  void SetCameraPosOffset(const float aX, const float aY, const float aZ);
  void LogCurrentTelemetry();

  // Gaze Dwell interface
  void SetDwellEnabled(const bool aEnabled);
  bool GetDwellEnabled() const;
  void SetDwellDuration(const float aDurationSeconds);
  float GetDwellDuration() const;

protected:
  struct State;
  DeviceDelegate3DOF(State& aState);
  virtual ~DeviceDelegate3DOF();

private:
  State& m;
  VRB_NO_DEFAULTS(DeviceDelegate3DOF)
};

} // namespace crow

#endif // DEVICE_DELEGATE_3DOF_DOT_H
