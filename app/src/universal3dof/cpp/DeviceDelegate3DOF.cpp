/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DeviceDelegate3DOF.h"
#include "DeviceUtils.h"
#include "GestureDelegate.h"
#include "OneEuroFilter.h"

#include "vrb/CameraEye.h"
#include "vrb/Color.h"
#include "vrb/ConcreteClass.h"
#include "vrb/GLError.h"
#include "vrb/Matrix.h"
#include "vrb/Quaternion.h"
#include "vrb/RenderContext.h"
#include "vrb/ShaderUtil.h"
#include "vrb/Vector.h"
#include "vrb/gl.h"
#include "JNIUtil.h"

#include <android/log.h>
#define LOG_TELEMETRY(...) __android_log_print(ANDROID_LOG_INFO, "ApertureTelemetry", __VA_ARGS__)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace {

const char* kSetRenderModeName = "setRenderMode";
const char* kSetRenderModeSignature = "(I)V";
JNIEnv* sEnv = nullptr;
jclass sBrowserClass = nullptr;
jobject sActivity = nullptr;
jmethodID sSetRenderMode = nullptr;

static const char* sDistortionVertexShader = R"SHADER(
attribute vec4 a_position;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = a_position;
}
)SHADER";

static const char* sDistortionFragmentShader = R"SHADER(
precision highp float;

varying vec2 v_uv;

uniform sampler2D u_sceneTexture;
uniform vec2 u_lensCenterLeft;    // Optical center normalized PER EYE [0, 1]
uniform vec2 u_lensCenterRight;   // Optical center normalized PER EYE [0, 1]
uniform vec2 u_eyeTanScale;        // Tan-angle scale factor (W_eye_m / d, H_m / d)
uniform float u_k1;
uniform float u_k2;
uniform int u_distortionMode;     // 0 = OFF (PASSTHROUGH), 1 = NORMAL (INVERSE LOOKUP), 2 = INVERSE
uniform int u_eyeSwapMode;        // 0 = NORMAL, 1 = SWAPPED

// Newton-Raphson solver to find undistorted tan-angle radius ru from distorted tan-angle radius rd
// Solves: ru * (1.0 + k1*ru^2 + k2*ru^4) - rd = 0
float SolveUndistortedRadius(float rd, float k1, float k2) {
    if (rd <= 1e-6 || (k1 == 0.0 && k2 == 0.0)) return rd;
    float ru = rd; // Initial guess
    for (int i = 0; i < 5; ++i) {
        float ru2 = ru * ru;
        float ru4 = ru2 * ru2;
        float f = ru * (1.0 + k1 * ru2 + k2 * ru4) - rd;
        float df = 1.0 + 3.0 * k1 * ru2 + 5.0 * k2 * ru4;
        if (abs(df) < 1e-6) break;
        ru = ru - f / df;
    }
    return max(0.0, ru);
}

void main() {
    bool isRightScreen = (v_uv.x >= 0.5);
    bool isRightEye = isRightScreen;
    if (u_eyeSwapMode == 1) {
        isRightEye = !isRightEye;
    }

    vec2 vpMin = isRightScreen ? vec2(0.5, 0.0) : vec2(0.0, 0.0);
    vec2 vpSize = vec2(0.5, 1.0);
    
    // Normalized eye UV inside single eye viewport [0.0, 1.0] x [0.0, 1.0]
    vec2 eyeUV = (v_uv - vpMin) / vpSize;

    // STAGE 1: PURE PASSTHROUGH (DISTORTION OFF)
    if (u_distortionMode == 0) {
        vec2 sampleScreenUV = (isRightEye ? vec2(0.5, 0.0) : vec2(0.0, 0.0)) + eyeUV * vpSize;
        gl_FragColor = texture2D(u_sceneTexture, sampleScreenUV);
        return;
    }

    // STAGE 2: CARDBOARD INVERSE LENS DISTORTION LOOKUP
    vec2 lensCenter = isRightEye ? u_lensCenterRight : u_lensCenterLeft;
    
    // Screen fragment position relative to optical center in single eye UV space
    vec2 pEye = eyeUV - lensCenter;
    
    // Convert to tan-angle space (physical angle tangents)
    vec2 pTan = pEye * u_eyeTanScale;
    float rd = length(pTan);
    
    vec2 sampleEyeUV;
    if (rd < 1e-6) {
        sampleEyeUV = lensCenter;
    } else {
        vec2 dirTan = pTan / rd;
        float ru = SolveUndistortedRadius(rd, u_k1, u_k2);
        vec2 pTanUndistorted = dirTan * ru;
        vec2 pEyeUndistorted = pTanUndistorted / u_eyeTanScale;
        sampleEyeUV = lensCenter + pEyeUndistorted;
    }
    
    if (sampleEyeUV.x < 0.0 || sampleEyeUV.x > 1.0 ||
        sampleEyeUV.y < 0.0 || sampleEyeUV.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        vec2 sampleScreenUV = (isRightEye ? vec2(0.5, 0.0) : vec2(0.0, 0.0)) + sampleEyeUV * vpSize;
        gl_FragColor = texture2D(u_sceneTexture, sampleScreenUV);
    }
}
)SHADER";

static const GLfloat sQuadVertices[] = {
    -1.0f,  1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
     1.0f,  1.0f, 0.0f,
     1.0f, -1.0f, 0.0f
};

static const GLfloat sQuadUVs[] = {
    0.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    1.0f, 0.0f
};

} // namespace

namespace crow {

static const int32_t kControllerIndex = 0;

static const vrb::Vector& GetHomePosition() {
  static vrb::Vector homePosition(0.0f, 1.48f, 1.45f);
  return homePosition;
}

struct DeviceDelegate3DOF::State {
  vrb::RenderContextWeak context;
  device::RenderMode renderMode;
  ImmersiveDisplayPtr display;
  ControllerDelegatePtr controller;
  vrb::CameraEyePtr cameras[2];
  vrb::Color clearColor;
  vrb::Vector position;
  bool clicked;
  GLsizei glWidth, glHeight;
  float near, far;

  // Real Physical Display Metrics measured from Android DisplayMetrics
  float physicalWidthMeters = 0.153838f; // Samsung A52 ~15.38cm
  float physicalHeightMeters = 0.069227f; // Samsung A52 ~6.92cm
  float xdpi = 396.0f;
  float ydpi = 396.0f;

  // Diagnostic Test Modes (Default: DISTORTION OFF for Stage 1 validation)
  int distortionTestMode = 0; // 0 = OFF (PASSTHROUGH), 1 = NORMAL, 2 = INVERSE
  int eyeSwapTestMode = 0;     // 0 = NORMAL, 1 = SWAPPED
  bool calibrationGridMode = false;

  // Offscreen Framebuffer Object (FBO) Engine
  GLuint fbo = 0;
  GLuint fboTexture = 0;
  GLuint fboDepthBuffer = 0;
  int32_t fboWidth = 0;
  int32_t fboHeight = 0;

  GLuint vertexShader = 0;
  GLuint fragmentShader = 0;
  GLuint program = 0;

  GLint aPositionLoc = -1;
  GLint aUVLoc = -1;
  GLint uSceneTextureLoc = -1;
  GLint uLensCenterLeftLoc = -1;
  GLint uLensCenterRightLoc = -1;
  GLint uEyeTanScaleLoc = -1;
  GLint uK1Loc = -1;
  GLint uK2Loc = -1;
  GLint uDistortionModeLoc = -1;
  GLint uEyeSwapModeLoc = -1;

  // Sensor & Orientation State
  std::mutex sensorMutex;
  vrb::Quaternion rawSensorOrientation;
  vrb::Quaternion recenterYawQuat;
  bool recenterRequested;
  std::unique_ptr<OneEuroFilterQuaternion> orientationFilter;
  vrb::Matrix reorientMatrix;

  // ViewerProfile & Stereo Specs
  ViewerProfile profile;

  // Gaze Dwell & Input Router State
  bool dwellEnabled;
  float dwellTimer;
  float dwellDuration;
  bool dwellTriggerActive;
  vrb::Quaternion lastHeadOrientation;
  uint64_t frameCount;

  State()
      : renderMode(device::RenderMode::StandAlone)
      , position(GetHomePosition())
      , clicked(false)
      , glWidth(0)
      , glHeight(0)
      , near(0.1f)
      , far(1000.0f)
      , rawSensorOrientation(0.0f, 0.0f, 0.0f, 1.0f)
      , recenterYawQuat(0.0f, 0.0f, 0.0f, 1.0f)
      , recenterRequested(false)
      , reorientMatrix(vrb::Matrix::Identity())
      , profile(ViewerProfile::CardboardV1())
      , dwellEnabled(false)
      , dwellTimer(0.0f)
      , dwellDuration(1.25f)
      , dwellTriggerActive(false)
      , lastHeadOrientation(0.0f, 0.0f, 0.0f, 1.0f)
      , frameCount(0)
  {
      SetupOrientationFilter();
  }

  void SetupOrientationFilter() {
      orientationFilter = std::make_unique<OneEuroFilterQuaternion>(0.25f, 2.0f, 1.0f);
  }

  void Initialize() {
    vrb::RenderContextPtr render = context.lock();
    if (!render) {
      return;
    }
    vrb::CreationContextPtr create = render->GetRenderThreadCreationContext();
    for (int i = 0; i < 2; ++i) {
        cameras[i] = vrb::CameraEye::Create(create);
    }
  }

  void CleanupDistortionEngine() {
    if (fbo) {
      VRB_GL_CHECK(glDeleteFramebuffers(1, &fbo));
      fbo = 0;
    }
    if (fboTexture) {
      VRB_GL_CHECK(glDeleteTextures(1, &fboTexture));
      fboTexture = 0;
    }
    if (fboDepthBuffer) {
      VRB_GL_CHECK(glDeleteRenderbuffers(1, &fboDepthBuffer));
      fboDepthBuffer = 0;
    }
    fboWidth = 0;
    fboHeight = 0;
  }

  void Shutdown() {
    CleanupDistortionEngine();
    if (program) {
      VRB_GL_CHECK(glDeleteProgram(program));
      program = 0;
    }
    if (vertexShader) {
      VRB_GL_CHECK(glDeleteShader(vertexShader));
      vertexShader = 0;
    }
    if (fragmentShader) {
      VRB_GL_CHECK(glDeleteShader(fragmentShader));
      fragmentShader = 0;
    }
  }

  void InitDistortionEngine(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    if (fboWidth == width && fboHeight == height && fbo != 0) return;

    CleanupDistortionEngine();

    fboWidth = width;
    fboHeight = height;

    VRB_GL_CHECK(glGenTextures(1, &fboTexture));
    VRB_GL_CHECK(glBindTexture(GL_TEXTURE_2D, fboTexture));
    VRB_GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fboWidth, fboHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    VRB_GL_CHECK(glGenRenderbuffers(1, &fboDepthBuffer));
    VRB_GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, fboDepthBuffer));
    VRB_GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, fboWidth, fboHeight));

    VRB_GL_CHECK(glGenFramebuffers(1, &fbo));
    VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    VRB_GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture, 0));
    VRB_GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepthBuffer));

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        VRB_ERROR("Universal3DOF: Offscreen FBO incomplete status 0x%x", status);
    }
    VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    if (!program) {
      vertexShader = vrb::LoadShader(GL_VERTEX_SHADER, sDistortionVertexShader);
      fragmentShader = vrb::LoadShader(GL_FRAGMENT_SHADER, sDistortionFragmentShader);
      if (vertexShader && fragmentShader) {
        program = vrb::CreateProgram(vertexShader, fragmentShader);
      }
      if (program) {
        aPositionLoc = vrb::GetAttributeLocation(program, "a_position");
        aUVLoc = vrb::GetAttributeLocation(program, "a_uv");
        uSceneTextureLoc = vrb::GetUniformLocation(program, "u_sceneTexture");
        uLensCenterLeftLoc = vrb::GetUniformLocation(program, "u_lensCenterLeft");
        uLensCenterRightLoc = vrb::GetUniformLocation(program, "u_lensCenterRight");
        uEyeTanScaleLoc = vrb::GetUniformLocation(program, "u_eyeTanScale");
        uK1Loc = vrb::GetUniformLocation(program, "u_k1");
        uK2Loc = vrb::GetUniformLocation(program, "u_k2");
        uDistortionModeLoc = vrb::GetUniformLocation(program, "u_distortionMode");
        uEyeSwapModeLoc = vrb::GetUniformLocation(program, "u_eyeSwapMode");
      }
    }
  }

  void UpdateDisplay() {
    if (!display || glWidth == 0 || glHeight == 0) {
      return;
    }

    int32_t eyeWidth = glWidth / 2;
    int32_t eyeHeight = glHeight;

    float W_m = physicalWidthMeters > 0.020f ? physicalWidthMeters : 0.153838f;
    float H_m = physicalHeightMeters > 0.020f ? physicalHeightMeters : 0.069227f;
    float W_eye_m = W_m / 2.0f;

    float halfIPD = profile.ipd / 2.0f;
    float d = profile.eyeToScreenDistance > 0.005f ? profile.eyeToScreenDistance : 0.039f;
    float y0 = profile.trayToLensCenterDistance > 0.005f ? profile.trayToLensCenterDistance : 0.035f;

    float cx_left_m = W_eye_m - halfIPD;
    float cx_right_m = halfIPD;

    float cy_m = y0;
    if (profile.verticalAlignment == 1) { // CENTER
        cy_m = H_m / 2.0f;
    } else if (profile.verticalAlignment == 2) { // TOP
        cy_m = H_m - y0;
    }

    float left_tanLeft = cx_left_m / d;
    float left_tanRight = (W_eye_m - cx_left_m) / d;
    float left_tanBottom = cy_m / d;
    float left_tanTop = (H_m - cy_m) / d;

    float right_tanLeft = cx_right_m / d;
    float right_tanRight = (W_eye_m - cx_right_m) / d;
    float right_tanBottom = cy_m / d;
    float right_tanTop = (H_m - cy_m) / d;

    float left_L_deg = std::atan(left_tanLeft) * (180.0f / 3.14159265f);
    float left_R_deg = std::atan(left_tanRight) * (180.0f / 3.14159265f);
    float left_T_deg = std::atan(left_tanTop) * (180.0f / 3.14159265f);
    float left_B_deg = std::atan(left_tanBottom) * (180.0f / 3.14159265f);

    float right_L_deg = std::atan(right_tanLeft) * (180.0f / 3.14159265f);
    float right_R_deg = std::atan(right_tanRight) * (180.0f / 3.14159265f);
    float right_T_deg = std::atan(right_tanTop) * (180.0f / 3.14159265f);
    float right_B_deg = std::atan(right_tanBottom) * (180.0f / 3.14159265f);

    vrb::Matrix leftEyeProj = vrb::Matrix::PerspectiveMatrixFromDegrees(left_L_deg, left_R_deg, left_T_deg, left_B_deg, near, far);
    vrb::Matrix rightEyeProj = vrb::Matrix::PerspectiveMatrixFromDegrees(right_L_deg, right_R_deg, right_T_deg, right_B_deg, near, far);

    cameras[0]->SetPerspective(leftEyeProj);
    cameras[1]->SetPerspective(rightEyeProj);

    int leftIdx = (eyeSwapTestMode == 1) ? 1 : 0;
    int rightIdx = (eyeSwapTestMode == 1) ? 0 : 1;

    cameras[leftIdx]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(-halfIPD, 0.0f, 0.0f)));
    cameras[rightIdx]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(halfIPD, 0.0f, 0.0f)));

    display->SetFieldOfView(device::Eye::Left, left_L_deg, left_R_deg, left_T_deg, left_B_deg);
    display->SetFieldOfView(device::Eye::Right, right_L_deg, right_R_deg, right_T_deg, right_B_deg);
    display->SetEyeResolution(eyeWidth, eyeHeight);
    display->SetEyeTransform(device::Eye::Left, cameras[0]->GetEyeTransform());
    display->SetEyeTransform(device::Eye::Right, cameras[1]->GetEyeTransform());
    display->SetCapabilityFlags(device::PositionEmulated | device::Orientation | device::Present |
                               device::InlineSession | device::ImmersiveVRSession);
  }
};

DeviceDelegate3DOFPtr
DeviceDelegate3DOF::Create(vrb::RenderContextPtr& aContext) {
  DeviceDelegate3DOFPtr result = std::make_shared<
      vrb::ConcreteClass<DeviceDelegate3DOF, DeviceDelegate3DOF::State>>();
  result->m.context = aContext;
  result->m.Initialize();
  return result;
}

void DeviceDelegate3DOF::SetRenderMode(const device::RenderMode aMode) {
  if (aMode == m.renderMode) {
    return;
  }
  m.renderMode = aMode;
  if (ValidateMethodID(sEnv, sActivity, sSetRenderMode, __FUNCTION__)) {
    sEnv->CallVoidMethod(sActivity, sSetRenderMode,
                         (aMode == device::RenderMode::Immersive ? 1 : 0));
    CheckJNIException(sEnv, __FUNCTION__);
  }
  m.position = GetHomePosition();
}

device::RenderMode DeviceDelegate3DOF::GetRenderMode() {
  return m.renderMode;
}

void DeviceDelegate3DOF::RegisterImmersiveDisplay(ImmersiveDisplayPtr aDisplay) {
  m.display = aDisplay;
  if (m.display) {
    m.display->SetDeviceName("Universal3DOF");
    m.UpdateDisplay();
    m.display->CompleteEnumeration();
  }
}

GestureDelegateConstPtr DeviceDelegate3DOF::GetGestureDelegate() {
  return nullptr;
}

vrb::CameraPtr DeviceDelegate3DOF::GetCamera(const device::Eye aEye) {
  return m.cameras[device::EyeIndex(aEye)];
}

const vrb::Matrix& DeviceDelegate3DOF::GetHeadTransform() const {
  return m.cameras[0]->GetHeadTransform();
}

const vrb::Matrix& DeviceDelegate3DOF::GetReorientTransform() const {
  return m.reorientMatrix;
}

void DeviceDelegate3DOF::SetReorientTransform(const vrb::Matrix& aMatrix) {
  m.reorientMatrix = aMatrix;
}

void DeviceDelegate3DOF::Reorient(const vrb::Matrix& aTransform, ReorientMode aMode) {
  switch (aMode) {
    case ReorientMode::SIX_DOF:
      m.reorientMatrix = DeviceUtils::CalculateReorientationMatrixOnHeadLock(aTransform, GetHeadTransform().GetTranslation());
      break;
    case ReorientMode::NO_ROLL:
      m.reorientMatrix = DeviceUtils::CalculateReorientationMatrixWithoutRoll(aTransform, GetHeadTransform().GetTranslation());
      break;
    default:
      VRB_ERROR("Universal3DOF: Unsupported reorient mode %d", (int)aMode);
  }
}

void DeviceDelegate3DOF::SetClearColor(const vrb::Color& aColor) {
  m.clearColor = aColor;
}

void DeviceDelegate3DOF::SetClipPlanes(const float aNear, const float aFar) {
  m.near = aNear;
  m.far = aFar;
  m.UpdateDisplay();
}

void DeviceDelegate3DOF::SetControllerDelegate(ControllerDelegatePtr& aController) {
  m.controller = aController;
  if (m.controller) {
    m.controller->CreateController(kControllerIndex, -1, "Universal3DOF Gaze Controller");
    m.controller->SetEnabled(kControllerIndex, true);
    m.controller->SetCapabilityFlags(kControllerIndex, device::Orientation | device::PositionEmulated);
    m.controller->SetButtonCount(kControllerIndex, 5);
    m.controller->SetTargetRayMode(kControllerIndex, device::TargetRayMode::Gaze);
    m.controller->SetGazeModeIndex(kControllerIndex);
  }
}

void DeviceDelegate3DOF::ReleaseControllerDelegate() {
  m.controller = nullptr;
}

int32_t DeviceDelegate3DOF::GetControllerModelCount() const {
  return 0;
}

const std::string DeviceDelegate3DOF::GetControllerModelName(const int32_t) const {
  static const std::string name;
  return name;
}

void DeviceDelegate3DOF::ProcessEvents() {}

void DeviceDelegate3DOF::SetHeadOrientation(const float aX, const float aY, const float aZ, const float aW) {
  std::lock_guard<std::mutex> lock(m.sensorMutex);
  m.rawSensorOrientation = vrb::Quaternion(aX, aY, aZ, aW);
}

void DeviceDelegate3DOF::RecenterYaw() {
  m.recenterRequested = true;
}

void DeviceDelegate3DOF::SetViewerProfile(const ViewerProfile& aProfile) {
  m.profile = aProfile;
  m.UpdateDisplay();
  LogCurrentTelemetry();
}

const ViewerProfile& DeviceDelegate3DOF::GetViewerProfile() const {
  return m.profile;
}

void DeviceDelegate3DOF::SetPhysicalScreenDimensions(float widthMeters, float heightMeters, float xdpi, float ydpi) {
  if (widthMeters < heightMeters) {
      std::swap(widthMeters, heightMeters);
  }
  m.physicalWidthMeters = widthMeters;
  m.physicalHeightMeters = heightMeters;
  m.xdpi = xdpi;
  m.ydpi = ydpi;
  m.UpdateDisplay();
  LogCurrentTelemetry();
}

void DeviceDelegate3DOF::SetDistortionTestMode(int mode) {
  m.distortionTestMode = mode;
  LogCurrentTelemetry();
}

void DeviceDelegate3DOF::SetEyeSwapTestMode(int mode) {
  m.eyeSwapTestMode = mode;
  m.UpdateDisplay();
  LogCurrentTelemetry();
}

void DeviceDelegate3DOF::SetCalibrationGridMode(bool enabled) {
  m.calibrationGridMode = enabled;
}

void DeviceDelegate3DOF::SetIPD(const float aIPD) {
  m.profile.ipd = aIPD;
  m.UpdateDisplay();
}

void DeviceDelegate3DOF::SetFOV(const float aFOV) {
  m.profile.screenFOV = aFOV;
  m.UpdateDisplay();
}

void DeviceDelegate3DOF::SetCameraPosOffset(const float aX, const float aY, const float aZ) {
  m.position = vrb::Vector(aX, aY, aZ);
  LOG_TELEMETRY("[CAMERA POS UPDATED] Pos=(%.2f, %.2f, %.2f)", aX, aY, aZ);
}

void DeviceDelegate3DOF::LogCurrentTelemetry() {
  float W_m = m.physicalWidthMeters > 0.020f ? m.physicalWidthMeters : 0.153838f;
  float H_m = m.physicalHeightMeters > 0.020f ? m.physicalHeightMeters : 0.069227f;
  float W_eye_m = W_m / 2.0f;
  float halfIPD = m.profile.ipd / 2.0f;
  float d = m.profile.eyeToScreenDistance > 0.005f ? m.profile.eyeToScreenDistance : 0.039f;
  float y0 = m.profile.trayToLensCenterDistance > 0.005f ? m.profile.trayToLensCenterDistance : 0.035f;

  float cx_left_m = W_eye_m - halfIPD;
  float cx_right_m = halfIPD;
  float cy_m = (m.profile.verticalAlignment == 1) ? (H_m / 2.0f) : ((m.profile.verticalAlignment == 2) ? (H_m - y0) : y0);

  float lensLeft_normEye_X = cx_left_m / W_eye_m;
  float lensRight_normEye_X = cx_right_m / W_eye_m;
  float lens_normEye_Y = cy_m / H_m;

  float lensLeft_normScreen_X = cx_left_m / W_m;
  float lensRight_normScreen_X = (W_eye_m + cx_right_m) / W_m;

  float left_L_deg = std::atan(cx_left_m / d) * (180.0f / 3.14159265f);
  float left_R_deg = std::atan((W_eye_m - cx_left_m) / d) * (180.0f / 3.14159265f);
  float left_T_deg = std::atan((H_m - cy_m) / d) * (180.0f / 3.14159265f);
  float left_B_deg = std::atan(cy_m / d) * (180.0f / 3.14159265f);

  float right_L_deg = std::atan(cx_right_m / d) * (180.0f / 3.14159265f);
  float right_R_deg = std::atan((W_eye_m - cx_right_m) / d) * (180.0f / 3.14159265f);

  LOG_TELEMETRY("\n=== CARDBOARD PROFILE ===\nname = %s\nipd = %.6f m\neyeToScreenDistance = %.6f m\ntrayToLensCenterDistance = %.6f m\nverticalAlignment = %d\nscreenFOV = %.1f°\nk1 = %.6f, k2 = %.6f\n\n=== DISPLAY ===\npixelWidth = %d, pixelHeight = %d\nphysicalWidthMeters = %.6f m, physicalHeightMeters = %.6f m\nxdpi = %.2f, ydpi = %.2f\n\n=== LENS CENTER (3 COORDINATE SYSTEMS) ===\nlensCenter physical meters: LEFT = (%.6fm, %.6fm), RIGHT = (%.6fm, %.6fm)\nlensCenter normalized-per-eye: LEFT = (%.4f, %.4f), RIGHT = (%.4f, %.4f)\nlensCenter normalized-full-screen: LEFT = (%.4f, %.4f), RIGHT = (%.4f, %.4f)\n\n=== LEFT ===\neyeTransform = Translation(%.4f, 0.0, 0.0)\nfieldOfView = (left=%.2f°, right=%.2f°, top=%.2f°, bottom=%.2f°)\nviewport = (x=0, y=0, w=%d, h=%d)\ntextureMapping = LEFT texture -> LEFT display region\n\n=== RIGHT ===\neyeTransform = Translation(%.4f, 0.0, 0.0)\nfieldOfView = (left=%.2f°, right=%.2f°, top=%.2f°, bottom=%.2f°)\nviewport = (x=%d, y=0, w=%d, h=%d)\ntextureMapping = RIGHT texture -> RIGHT display region\n\n=== TEST MODES ===\ndistortionTestMode = %d (0=OFF, 1=NORMAL, 2=INVERSE)\neyeSwapTestMode = %d (0=NORMAL, 1=SWAPPED)",
                m.profile.name.c_str(), m.profile.ipd, m.profile.eyeToScreenDistance, m.profile.trayToLensCenterDistance, m.profile.verticalAlignment, m.profile.screenFOV, m.profile.k1, m.profile.k2,
                m.glWidth, m.glHeight, W_m, H_m, m.xdpi, m.ydpi,
                cx_left_m, cy_m, cx_right_m, cy_m,
                lensLeft_normEye_X, lens_normEye_Y, lensRight_normEye_X, lens_normEye_Y,
                lensLeft_normScreen_X, lens_normEye_Y, lensRight_normScreen_X, lens_normEye_Y,
                -halfIPD, left_L_deg, left_R_deg, left_T_deg, left_B_deg, m.glWidth / 2, m.glHeight,
                +halfIPD, right_L_deg, right_R_deg, left_T_deg, left_B_deg, m.glWidth / 2, m.glWidth / 2, m.glHeight,
                m.distortionTestMode, m.eyeSwapTestMode);
}

void DeviceDelegate3DOF::SetDwellEnabled(const bool aEnabled) {
  m.dwellEnabled = aEnabled;
  if (!aEnabled) {
    m.dwellTimer = 0.0f;
    if (m.controller) {
      m.controller->SetSelectFactor(kControllerIndex, 0.0f);
    }
  }
}

bool DeviceDelegate3DOF::GetDwellEnabled() const {
  return m.dwellEnabled;
}

void DeviceDelegate3DOF::SetDwellDuration(const float aDurationSeconds) {
  m.dwellDuration = aDurationSeconds > 0.1f ? aDurationSeconds : 1.25f;
}

float DeviceDelegate3DOF::GetDwellDuration() const {
  return m.dwellDuration;
}

void DeviceDelegate3DOF::StartFrame(const FramePrediction aPrediction) {
  if (m.glWidth > 0 && m.glHeight > 0) {
    m.InitDistortionEngine(m.glWidth, m.glHeight);
  }

  if (m.fbo != 0) {
    VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m.fbo));
  }

  VRB_GL_CHECK(glClearColor(m.clearColor.Red(), m.clearColor.Green(),
                             m.clearColor.Blue(), m.clearColor.Alpha()));
  VRB_GL_CHECK(glEnable(GL_DEPTH_TEST));
  VRB_GL_CHECK(glEnable(GL_CULL_FACE));
  VRB_GL_CHECK(glEnable(GL_BLEND));
  VRB_GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
  mShouldRender = true;

  vrb::Quaternion qRaw;
  {
    std::lock_guard<std::mutex> lock(m.sensorMutex);
    qRaw = m.rawSensorOrientation;
  }

  if (std::isnan(qRaw.x()) || std::isnan(qRaw.y()) || std::isnan(qRaw.z()) || std::isnan(qRaw.w()) || qRaw.Length() < 1e-4f) {
    qRaw = vrb::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
  } else {
    qRaw = qRaw.Normalize();
  }

  int64_t timestamp = 0;
  float deltaTime = 0.016f;
  vrb::RenderContextPtr renderCtx = m.context.lock();
  if (renderCtx) {
    double now = renderCtx->GetTimestamp();
    timestamp = (int64_t)(now * 1e9);
  }

  float* filteredData = m.orientationFilter->filter(timestamp, qRaw.Data());
  vrb::Quaternion qFiltered = vrb::Quaternion(filteredData).Normalize();

  if (m.recenterRequested) {
    float yaw = std::atan2(2.0f * (qFiltered.w() * qFiltered.y() + qFiltered.x() * qFiltered.z()),
                           1.0f - 2.0f * (qFiltered.y() * qFiltered.y() + qFiltered.z() * qFiltered.z()));
    vrb::Quaternion qYawRef(0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f));
    m.recenterYawQuat = qYawRef.Conjugate();
    m.recenterRequested = false;
  }

  vrb::Quaternion qFinal = (m.recenterYawQuat * qFiltered).Normalize();

  vrb::Matrix headRotation = vrb::Matrix::Rotation(qFinal);
  vrb::Matrix headTransform = vrb::Matrix::Translation(m.position).PostMultiply(headRotation);

  float halfIPD = m.profile.ipd / 2.0f;
  int leftIdx = (m.eyeSwapTestMode == 1) ? 1 : 0;
  int rightIdx = (m.eyeSwapTestMode == 1) ? 0 : 1;

  m.cameras[leftIdx]->SetHeadTransform(headTransform);
  m.cameras[leftIdx]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(-halfIPD, 0.0f, 0.0f)));
  m.cameras[rightIdx]->SetHeadTransform(headTransform);
  m.cameras[rightIdx]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(halfIPD, 0.0f, 0.0f)));

  m.frameCount++;

  if (m.controller) {
    m.controller->SetTransform(kControllerIndex, headTransform);
  }

  if (m.dwellEnabled && m.controller) {
    float dot = std::abs(qFinal.x() * m.lastHeadOrientation.x() +
                         qFinal.y() * m.lastHeadOrientation.y() +
                         qFinal.z() * m.lastHeadOrientation.z() +
                         qFinal.w() * m.lastHeadOrientation.w());
    dot = std::min(1.0f, dot);
    float angleDelta = 2.0f * std::acos(dot);

    if (angleDelta < 0.035f) {
      m.dwellTimer += deltaTime;
      float progress = std::min(1.0f, m.dwellTimer / m.dwellDuration);
      m.controller->SetSelectFactor(kControllerIndex, progress);

      if (m.dwellTimer >= m.dwellDuration && !m.dwellTriggerActive) {
        m.controller->SetButtonState(kControllerIndex, ControllerDelegate::BUTTON_TRIGGER, 0, true, true);
        m.controller->SetSelectActionStart(kControllerIndex);
        m.dwellTriggerActive = true;
      } else if (m.dwellTriggerActive && m.dwellTimer >= (m.dwellDuration + 0.12f)) {
        m.controller->SetSelectActionStop(kControllerIndex);
        m.controller->SetButtonState(kControllerIndex, ControllerDelegate::BUTTON_TRIGGER, 0, false, false);
        m.dwellTriggerActive = false;
        m.dwellTimer = 0.0f;
        m.controller->SetSelectFactor(kControllerIndex, 0.0f);
      }
    } else {
      if (!m.dwellTriggerActive) {
        m.dwellTimer = 0.0f;
        m.controller->SetSelectFactor(kControllerIndex, 0.0f);
      }
    }
    m.lastHeadOrientation = qFinal;
  }

  if (m.display) {
    m.display->SetEyeTransform(device::Eye::Left, m.cameras[0]->GetEyeTransform());
    m.display->SetEyeTransform(device::Eye::Right, m.cameras[1]->GetEyeTransform());
  }
}

void DeviceDelegate3DOF::BindEye(const device::Eye aEye) {
  int32_t eyeWidth = m.glWidth > 0 ? m.glWidth / 2 : 960;
  int32_t eyeHeight = m.glHeight > 0 ? m.glHeight : 1080;

  int32_t xOffset = (aEye == device::Eye::Left) ? 0 : eyeWidth;
  VRB_GL_CHECK(glViewport(xOffset, 0, eyeWidth, eyeHeight));
}

void DeviceDelegate3DOF::EndFrame(const FrameEndMode aMode) {
  if (m.fbo == 0 || !m.program) {
    return;
  }

  VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));

  VRB_GL_CHECK(glViewport(0, 0, m.glWidth, m.glHeight));

  VRB_GL_CHECK(glDisable(GL_DEPTH_TEST));
  VRB_GL_CHECK(glDisable(GL_CULL_FACE));
  VRB_GL_CHECK(glDisable(GL_BLEND));
  VRB_GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
  VRB_GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));

  VRB_GL_CHECK(glUseProgram(m.program));

  float W_m = m.physicalWidthMeters > 0.020f ? m.physicalWidthMeters : 0.153838f;
  float H_m = m.physicalHeightMeters > 0.020f ? m.physicalHeightMeters : 0.069227f;
  float W_eye_m = W_m / 2.0f;

  float halfIPD = m.profile.ipd / 2.0f;
  float d = m.profile.eyeToScreenDistance > 0.005f ? m.profile.eyeToScreenDistance : 0.039f;
  float y0 = m.profile.trayToLensCenterDistance > 0.005f ? m.profile.trayToLensCenterDistance : 0.035f;

  float cx_left_m = W_eye_m - halfIPD;
  float cx_right_m = halfIPD;

  float cy_m = y0;
  if (m.profile.verticalAlignment == 1) {
      cy_m = H_m / 2.0f;
  } else if (m.profile.verticalAlignment == 2) {
      cy_m = H_m - y0;
  }

  float lensLeftX = cx_left_m / W_eye_m;
  float lensLeftY = cy_m / H_m;

  float lensRightX = cx_right_m / W_eye_m;
  float lensRightY = cy_m / H_m;

  float eyeTanScaleX = W_eye_m / d;
  float eyeTanScaleY = H_m / d;

  VRB_GL_CHECK(glUniform2f(m.uLensCenterLeftLoc, lensLeftX, lensLeftY));
  VRB_GL_CHECK(glUniform2f(m.uLensCenterRightLoc, lensRightX, lensRightY));
  VRB_GL_CHECK(glUniform2f(m.uEyeTanScaleLoc, eyeTanScaleX, eyeTanScaleY));
  VRB_GL_CHECK(glUniform1f(m.uK1Loc, m.profile.k1));
  VRB_GL_CHECK(glUniform1f(m.uK2Loc, m.profile.k2));
  VRB_GL_CHECK(glUniform1i(m.uDistortionModeLoc, m.distortionTestMode));
  VRB_GL_CHECK(glUniform1i(m.uEyeSwapModeLoc, m.eyeSwapTestMode));

  VRB_GL_CHECK(glActiveTexture(GL_TEXTURE0));
  VRB_GL_CHECK(glBindTexture(GL_TEXTURE_2D, m.fboTexture));
  VRB_GL_CHECK(glUniform1i(m.uSceneTextureLoc, 0));

  if (m.aPositionLoc >= 0) {
    VRB_GL_CHECK(glEnableVertexAttribArray(m.aPositionLoc));
    VRB_GL_CHECK(glVertexAttribPointer(m.aPositionLoc, 3, GL_FLOAT, GL_FALSE, 0, sQuadVertices));
  }
  if (m.aUVLoc >= 0) {
    VRB_GL_CHECK(glEnableVertexAttribArray(m.aUVLoc));
    VRB_GL_CHECK(glVertexAttribPointer(m.aUVLoc, 2, GL_FLOAT, GL_FALSE, 0, sQuadUVs));
  }

  VRB_GL_CHECK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

  if (m.aPositionLoc >= 0) {
    VRB_GL_CHECK(glDisableVertexAttribArray(m.aPositionLoc));
  }
  if (m.aUVLoc >= 0) {
    VRB_GL_CHECK(glDisableVertexAttribArray(m.aUVLoc));
  }
}

void DeviceDelegate3DOF::InitializeJava(JNIEnv* aEnv, jobject aActivity) {
  if (aEnv == sEnv) {
    return;
  }
  sEnv = aEnv;
  if (!sEnv) {
    return;
  }
  sActivity = sEnv->NewGlobalRef(aActivity);
  sBrowserClass = sEnv->GetObjectClass(sActivity);
  if (!sBrowserClass) {
    return;
  }
  sSetRenderMode = FindJNIMethodID(sEnv, sBrowserClass, kSetRenderModeName, kSetRenderModeSignature);
}

void DeviceDelegate3DOF::ShutdownJava() {
  if (!sEnv) {
    return;
  }
  if (sActivity) {
    sEnv->DeleteGlobalRef(sActivity);
    sActivity = nullptr;
  }
  sBrowserClass = nullptr;
  sSetRenderMode = nullptr;
}

void DeviceDelegate3DOF::SetViewport(const int aWidth, const int aHeight) {
  m.glWidth = aWidth;
  m.glHeight = aHeight;
  BindEye(device::Eye::Left);
  m.UpdateDisplay();
}

void DeviceDelegate3DOF::Pause() {}

void DeviceDelegate3DOF::Resume() {}

void DeviceDelegate3DOF::TouchEvent(const bool aDown, const float aX, const float aY) {
  if (m.controller) {
    m.dwellTimer = 0.0f;
    m.controller->SetSelectFactor(kControllerIndex, 0.0f);
    m.controller->SetButtonState(kControllerIndex, ControllerDelegate::BUTTON_TRIGGER, 0, aDown, aDown);
    if (aDown) {
      m.controller->SetSelectActionStart(kControllerIndex);
    } else {
      m.controller->SetSelectActionStop(kControllerIndex);
    }
  }
}

void DeviceDelegate3DOF::KeyEvent(const int32_t aKeyCode, const bool aDown) {
  if (m.controller) {
    m.dwellTimer = 0.0f;
    m.controller->SetSelectFactor(kControllerIndex, 0.0f);
    m.controller->SetButtonState(kControllerIndex, ControllerDelegate::BUTTON_TRIGGER, 0, aDown, aDown);
    if (aDown) {
      m.controller->SetSelectActionStart(kControllerIndex);
    } else {
      m.controller->SetSelectActionStop(kControllerIndex);
    }
  }
}

DeviceDelegate3DOF::DeviceDelegate3DOF(State& aState) : m(aState) {}

DeviceDelegate3DOF::~DeviceDelegate3DOF() {
  m.Shutdown();
}

} // namespace crow
