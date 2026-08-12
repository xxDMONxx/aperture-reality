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
precision mediump float;

varying vec2 v_uv;

uniform sampler2D u_sceneTexture;
uniform vec2 u_lensCenterLeft;
uniform vec2 u_lensCenterRight;
uniform float u_k1;
uniform float u_k2;
uniform float u_eyeAspect;

void main() {
    bool isRight = (v_uv.x >= 0.5);
    vec2 vpMin = isRight ? vec2(0.5, 0.0) : vec2(0.0, 0.0);
    vec2 vpSize = vec2(0.5, 1.0);
    
    // Normalized eye UV in [0.0, 1.0] x [0.0, 1.0]
    vec2 eyeUV = (v_uv - vpMin) / vpSize;
    
    vec2 lensCenter = isRight ? u_lensCenterRight : u_lensCenterLeft;
    
    // Position relative to optical lens center
    vec2 p = eyeUV - lensCenter;
    
    // Correct for aspect ratio to ensure circular radial distortion
    vec2 pAspect = vec2(p.x * u_eyeAspect, p.y);
    
    float r2 = dot(pAspect, pAspect);
    float r4 = r2 * r2;
    
    // Official Google Cardboard Barrel Distortion polynomial formula
    float distortionFactor = 1.0 + u_k1 * r2 + u_k2 * r4;
    
    vec2 distortedEyeUV = lensCenter + p * distortionFactor;
    
    // Vignette / black border outside eye optical field
    if (distortedEyeUV.x < 0.0 || distortedEyeUV.x > 1.0 ||
        distortedEyeUV.y < 0.0 || distortedEyeUV.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        vec2 sampleScreenUV = vpMin + distortedEyeUV * vpSize;
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

// APERTURE REALITY - PERMANENT LOCKED HOME POSITION & WINDOW DISTANCE
// Head Camera Home Position: (0.0f, 1.48f, 1.45f)
// Maintains optimal main browser window & 3D side panel viewing distance across all builds.
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

  // Offscreen Framebuffer Object (FBO) & Real-time Google Cardboard Barrel Distortion Engine
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
  GLint uK1Loc = -1;
  GLint uK2Loc = -1;
  GLint uEyeAspectLoc = -1;

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
      // 1€ filter for orientation quaternions (mincutoff=0.25Hz, beta=2.0, dcutoff=1.0Hz)
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

    // 1. Create Color Texture
    VRB_GL_CHECK(glGenTextures(1, &fboTexture));
    VRB_GL_CHECK(glBindTexture(GL_TEXTURE_2D, fboTexture));
    VRB_GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fboWidth, fboHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    VRB_GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    // 2. Create Depth Renderbuffer
    VRB_GL_CHECK(glGenRenderbuffers(1, &fboDepthBuffer));
    VRB_GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, fboDepthBuffer));
    VRB_GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, fboWidth, fboHeight));

    // 3. Attach to Framebuffer Object
    VRB_GL_CHECK(glGenFramebuffers(1, &fbo));
    VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    VRB_GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture, 0));
    VRB_GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepthBuffer));

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        VRB_ERROR("Universal3DOF: Offscreen FBO incomplete status 0x%x", status);
    }
    VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    // 4. Compile Distortion Shader Program
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
        uK1Loc = vrb::GetUniformLocation(program, "u_k1");
        uK2Loc = vrb::GetUniformLocation(program, "u_k2");
        uEyeAspectLoc = vrb::GetUniformLocation(program, "u_eyeAspect");
      }
    }
  }

  void UpdateDisplay() {
    if (!display || glWidth == 0 || glHeight == 0) {
      return;
    }

    int32_t eyeWidth = glWidth / 2;
    int32_t eyeHeight = glHeight;

    float halfIPD = profile.ipd / 2.0f; // e.g. 0.032m
    float d = profile.eyeToScreenDistance > 0.010f ? profile.eyeToScreenDistance : 0.039f; // lens distance in meters
    float phoneWidthMeters = 0.150f; // Samsung Galaxy A52 screen width ~ 15cm
    float halfScreenWidth = phoneWidthMeters / 2.0f; // 0.075m

    // Compute outer distance (towards screen edge) and inner distance (towards nose divider)
    float xOuter = halfIPD + (halfScreenWidth / 2.0f);
    float xInner = std::max(0.005f, halfScreenWidth - halfIPD);

    // Compute radial distortion scale factor from Cardboard QR profile k1 and k2
    float rNorm = halfIPD / d;
    float rNorm2 = rNorm * rNorm;
    float rNorm4 = rNorm2 * rNorm2;
    float distortionScale = 1.0f + (profile.k1 * rNorm2) + (profile.k2 * rNorm4);
    distortionScale = std::min(1.80f, std::max(0.80f, distortionScale));

    // Convert distances to FOV half-angles in degrees, scaled by Cardboard lens distortion
    float angleOuterDeg = (float)(std::atan((xOuter * distortionScale) / d) * (180.0 / 3.14159265));
    float angleInnerDeg = (float)(std::atan((xInner * distortionScale) / d) * (180.0 / 3.14159265));

    float maxFov = profile.screenFOV > 0.0f ? profile.screenFOV * 0.6f : 55.0f;
    angleOuterDeg = std::min(maxFov, angleOuterDeg);
    angleInnerDeg = std::min(maxFov, angleInnerDeg);

    float topDeg = std::min(maxFov, (float)(std::atan(((halfScreenWidth * 0.5f) * distortionScale) / d) * (180.0 / 3.14159265)));
    float bottomDeg = topDeg;

    // LEFT EYE: outer is LEFT, inner is RIGHT
    vrb::Matrix leftEyeProj = vrb::Matrix::PerspectiveMatrixFromDegrees(
        angleOuterDeg, angleInnerDeg, topDeg, bottomDeg, near, far);

    // RIGHT EYE: inner is LEFT, outer is RIGHT
    vrb::Matrix rightEyeProj = vrb::Matrix::PerspectiveMatrixFromDegrees(
        angleInnerDeg, angleOuterDeg, topDeg, bottomDeg, near, far);

    cameras[0]->SetPerspective(leftEyeProj);
    cameras[1]->SetPerspective(rightEyeProj);

    // Apply eye offset matrices (Left = -halfIPD, Right = +halfIPD)
    cameras[0]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(-halfIPD, 0.0f, 0.0f)));
    cameras[1]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(halfIPD, 0.0f, 0.0f)));

    display->SetFieldOfView(device::Eye::Left, angleOuterDeg, angleInnerDeg, topDeg, bottomDeg);
    display->SetFieldOfView(device::Eye::Right, angleInnerDeg, angleOuterDeg, topDeg, bottomDeg);
    display->SetEyeResolution(eyeWidth, eyeHeight);
    display->SetEyeOffset(device::Eye::Left, -halfIPD, 0.0f, 0.0f);
    display->SetEyeOffset(device::Eye::Right, halfIPD, 0.0f, 0.0f);
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
}

const ViewerProfile& DeviceDelegate3DOF::GetViewerProfile() const {
  return m.profile;
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
  vrb::Quaternion qRaw;
  {
    std::lock_guard<std::mutex> lock(m.sensorMutex);
    qRaw = m.rawSensorOrientation;
  }
  vrb::Quaternion qFinal = (m.recenterYawQuat * qRaw).Normalize();
  float sinp = 2.0f * (qFinal.w() * qFinal.x() - qFinal.y() * qFinal.z());
  float pitch = 0.0f;
  if (std::abs(sinp) >= 1.0f) {
    pitch = std::copysign(3.14159265f / 2.0f, sinp) * (180.0f / 3.14159265f);
  } else {
    pitch = std::asin(sinp) * (180.0f / 3.14159265f);
  }
  float yaw = std::atan2(2.0f * (qFinal.w() * qFinal.y() + qFinal.z() * qFinal.x()),
                         1.0f - 2.0f * (qFinal.x() * qFinal.x() + qFinal.y() * qFinal.y())) * (180.0f / 3.14159265f);
  float roll = std::atan2(2.0f * (qFinal.w() * qFinal.z() + qFinal.x() * qFinal.y()),
                          1.0f - 2.0f * (qFinal.y() * qFinal.y() + qFinal.z() * qFinal.z())) * (180.0f / 3.14159265f);

  LOG_TELEMETRY("[TELEMETRY SNAPSHOT] Head: Yaw=%.2f° Pitch=%.2f° Roll=%.2f° | Pos=(%.2f, %.2f, %.2f) | FOV=%.1f° IPD=%.4fm",
                yaw, pitch, roll, m.position.x(), m.position.y(), m.position.z(), m.profile.screenFOV, m.profile.ipd);
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

  // Validate quaternion input to prevent NaN or zero-length issues
  if (std::isnan(qRaw.x()) || std::isnan(qRaw.y()) || std::isnan(qRaw.z()) || std::isnan(qRaw.w()) || qRaw.Length() < 1e-4f) {
    qRaw = vrb::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
  } else {
    qRaw = qRaw.Normalize();
  }

  // Filter raw quaternion using 1€ filter
  int64_t timestamp = 0;
  float deltaTime = 0.016f; // Default ~60fps
  vrb::RenderContextPtr renderCtx = m.context.lock();
  if (renderCtx) {
    double now = renderCtx->GetTimestamp();
    timestamp = (int64_t)(now * 1e9);
  }

  float* filteredData = m.orientationFilter->filter(timestamp, qRaw.Data());
  vrb::Quaternion qFiltered = vrb::Quaternion(filteredData).Normalize();

  // Handle explicit Yaw recentering
  if (m.recenterRequested) {
    float yaw = std::atan2(2.0f * (qFiltered.w() * qFiltered.y() + qFiltered.x() * qFiltered.z()),
                           1.0f - 2.0f * (qFiltered.y() * qFiltered.y() + qFiltered.z() * qFiltered.z()));
    vrb::Quaternion qYawRef(0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f));
    m.recenterYawQuat = qYawRef.Conjugate();
    m.recenterRequested = false;
  }

  // Combine recenter quaternion and filtered orientation
  vrb::Quaternion qFinal = (m.recenterYawQuat * qFiltered).Normalize();

  // Construct dynamic 3DOF head transform: user head at home position, rotated in-place
  vrb::Matrix headRotation = vrb::Matrix::Rotation(qFinal);
  vrb::Matrix headTransform = vrb::Matrix::Translation(m.position).PostMultiply(headRotation);

  // Update left and right eye cameras
  float halfIPD = m.profile.ipd / 2.0f;
  m.cameras[0]->SetHeadTransform(headTransform);
  m.cameras[0]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(-halfIPD, 0.0f, 0.0f)));
  m.cameras[1]->SetHeadTransform(headTransform);
  m.cameras[1]->SetEyeTransform(vrb::Matrix::Translation(vrb::Vector(halfIPD, 0.0f, 0.0f)));

  m.frameCount++;
  if (m.frameCount % 60 == 0) {
    float sinp = 2.0f * (qFinal.w() * qFinal.x() - qFinal.y() * qFinal.z());
    float pitch = (std::abs(sinp) >= 1.0f ? std::copysign(3.14159265f / 2.0f, sinp) : std::asin(sinp)) * (180.0f / 3.14159265f);
    float yaw = std::atan2(2.0f * (qFinal.w() * qFinal.y() + qFinal.z() * qFiltered.x()),
                           1.0f - 2.0f * (qFinal.x() * qFinal.x() + qFinal.y() * qFinal.y())) * (180.0f / 3.14159265f);
    float roll = std::atan2(2.0f * (qFinal.w() * qFinal.z() + qFinal.x() * qFinal.y()),
                            1.0f - 2.0f * (qFinal.y() * qFinal.y() + qFinal.z() * qFinal.z())) * (180.0f / 3.14159265f);

    LOG_TELEMETRY("[3DOF CAMERA STREAM] Yaw=%.2f° Pitch=%.2f° Roll=%.2f° | HeadPos=(%.2f, %.2f, %.2f) | FOV=%.1f° IPD=%.4fm k1=%.3f k2=%.3f",
                  yaw, pitch, roll, m.position.x(), m.position.y(), m.position.z(), m.profile.screenFOV, m.profile.ipd, m.profile.k1, m.profile.k2);
  }

  // Update Gaze pointer transform
  if (m.controller) {
    m.controller->SetTransform(kControllerIndex, headTransform);
  }

  // Gaze Dwell logic: test head orientation stability
  if (m.dwellEnabled && m.controller) {
    // Angular difference between current frame and last frame
    float dot = std::abs(qFinal.x() * m.lastHeadOrientation.x() +
                         qFinal.y() * m.lastHeadOrientation.y() +
                         qFinal.z() * m.lastHeadOrientation.z() +
                         qFinal.w() * m.lastHeadOrientation.w());
    dot = std::min(1.0f, dot);
    float angleDelta = 2.0f * std::acos(dot); // Radians

    // If head movement is small (head is steady / focused on target)
    if (angleDelta < 0.035f) { // ~2 degrees threshold
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
      // Reset dwell timer when moving head significantly
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
  // Always split screen into Left and Right eye viewports (Side-by-Side SBS mode) for 3DOF Cardboard VR viewers
  int32_t eyeWidth = m.glWidth > 0 ? m.glWidth / 2 : 960;
  int32_t eyeHeight = m.glHeight > 0 ? m.glHeight : 1080;

  if (m.glWidth <= 0 || m.glHeight <= 0) {
    int32_t xOffset = (aEye == device::Eye::Left) ? 0 : eyeWidth;
    VRB_GL_CHECK(glViewport(xOffset, 0, eyeWidth, eyeHeight));
    return;
  }

  // Calculate physical screen scaling (assume ~15cm screen width, ~7cm screen height for Samsung Galaxy A52)
  float phoneWidthMeters = 0.150f;
  float pixelsPerMeter = (float)m.glWidth / phoneWidthMeters;
  
  float halfIPD = m.profile.ipd / 2.0f; // e.g. 0.032m for 64mm IPD
  float halfScreenWidth = phoneWidthMeters / 2.0f; // 0.075m
  float halfScreenCenter = halfScreenWidth / 2.0f; // 0.0375m

  // Horizontal shift offset relative to lens optical center
  float shiftMeters = halfIPD - halfScreenCenter; // e.g. 0.032 - 0.0375 = -0.0055m
  int32_t shiftPixels = (int32_t)(shiftMeters * pixelsPerMeter);

  int32_t xOffset = 0;
  if (aEye == device::Eye::Left) {
    xOffset = shiftPixels; // Shifts viewport for left eye
  } else {
    xOffset = eyeWidth - shiftPixels; // Shifts viewport for right eye
  }

  // Vertical alignment offset calculation from trayToLensCenterDistance (Field 6)
  int32_t yOffset = 0;
  if (m.profile.verticalAlignment == 0) { // BOTTOM alignment
    float defaultTrayLensCenter = 0.035f; // 35mm default
    float deltaY = m.profile.trayToLensCenterDistance - defaultTrayLensCenter;
    yOffset = (int32_t)(deltaY * pixelsPerMeter);
    yOffset = std::max(-100, std::min(100, yOffset));
  }

  VRB_GL_CHECK(glViewport(xOffset, yOffset, eyeWidth, eyeHeight));
}

void DeviceDelegate3DOF::EndFrame(const FrameEndMode aMode) {
  if (m.fbo == 0 || !m.program) {
    return;
  }

  // 1. Unbind offscreen FBO -> target physical device screen framebuffer
  VRB_GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));

  // 2. Set full screen viewport
  VRB_GL_CHECK(glViewport(0, 0, m.glWidth, m.glHeight));

  // 3. Disable 3D scene depth/culling state for 2D distortion blit
  VRB_GL_CHECK(glDisable(GL_DEPTH_TEST));
  VRB_GL_CHECK(glDisable(GL_CULL_FACE));
  VRB_GL_CHECK(glDisable(GL_BLEND));
  VRB_GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
  VRB_GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));

  // 4. Activate Distortion Shader
  VRB_GL_CHECK(glUseProgram(m.program));

  // Compute optical lens center coordinates in normalized eye viewport [0, 1]
  float halfIPD = m.profile.ipd / 2.0f;
  float shiftMeters = halfIPD - 0.0375f;
  float shiftNormX = shiftMeters / 0.075f;

  float deltaY = (m.profile.verticalAlignment == 0) ? (m.profile.trayToLensCenterDistance - 0.035f) : 0.0f;
  float shiftNormY = deltaY / 0.070f;

  float lensLeftX = 0.5f + shiftNormX;
  float lensLeftY = 0.5f + shiftNormY;

  float lensRightX = 0.5f - shiftNormX;
  float lensRightY = 0.5f + shiftNormY;

  float eyeAspect = (float)(m.glWidth / 2) / (float)m.glHeight;

  VRB_GL_CHECK(glUniform2f(m.uLensCenterLeftLoc, lensLeftX, lensLeftY));
  VRB_GL_CHECK(glUniform2f(m.uLensCenterRightLoc, lensRightX, lensRightY));
  VRB_GL_CHECK(glUniform1f(m.uK1Loc, m.profile.k1));
  VRB_GL_CHECK(glUniform1f(m.uK2Loc, m.profile.k2));
  VRB_GL_CHECK(glUniform1f(m.uEyeAspectLoc, eyeAspect));

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
  // Screen touch / mouse tap triggers direct select click
  if (m.controller) {
    // Reset dwell timer when manual touch is received
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
  // Gamepad / Keyboard / Headset Button input mapping
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
