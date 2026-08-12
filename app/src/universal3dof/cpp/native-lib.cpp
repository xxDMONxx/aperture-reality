/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <jni.h>
#include <string>

#include "BrowserWorld.h"
#include "DeviceDelegate3DOF.h"
#include "VRBrowser.h"
#include "vrb/GLError.h"
#include "vrb/Logger.h"
#include "JNIUtil.h"

using namespace crow;

#define JNI_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
    Java_com_igalia_wolvic_PlatformActivity_##method_name

namespace {
struct AppContext {
    crow::DeviceDelegate3DOFPtr mDevice;
    JavaContext mJavaContext;
};
typedef std::shared_ptr<AppContext> AppContextPtr;

AppContextPtr sAppContext;
}

extern "C" {

JNI_METHOD(void, activityPaused)
(JNIEnv*, jobject) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->Pause();
  }
  BrowserWorld::Instance().Pause();
  BrowserWorld::Instance().ShutdownGL();
}

JNI_METHOD(void, activityResumed)
(JNIEnv*, jobject) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->Resume();
  }
  BrowserWorld::Instance().InitializeGL();
  BrowserWorld::Instance().Resume();
}

JNI_METHOD(void, activityCreated)
(JNIEnv* aEnv, jobject aActivity, jobject aAssetManager) {
  if (!sAppContext) {
    sAppContext = std::make_shared<AppContext>();
  }
  sAppContext->mJavaContext.activity = aEnv->NewGlobalRef(aActivity);
  sAppContext->mJavaContext.env = aEnv;
  sAppContext->mJavaContext.vm->AttachCurrentThread(&sAppContext->mJavaContext.env, nullptr);

  crow::VRBrowser::InitializeJava(aEnv, aActivity);

  sAppContext->mDevice = crow::DeviceDelegate3DOF::Create(BrowserWorld::Instance().GetRenderContext());
  sAppContext->mDevice->Resume();
  sAppContext->mDevice->InitializeJava(aEnv, aActivity);

  BrowserWorld::Instance().RegisterDeviceDelegate(sAppContext->mDevice);
  BrowserWorld::Instance().InitializeJava(aEnv, aActivity, aAssetManager);
  BrowserWorld::Instance().InitializeGL();
}

JNI_METHOD(void, updateViewport)
(JNIEnv*, jobject, jint aWidth, jint aHeight) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->SetViewport(aWidth, aHeight);
  } else {
    VRB_LOG("Universal3DOF: FAILED TO SET VIEWPORT");
  }
}

JNI_METHOD(void, activityDestroyed)
(JNIEnv*, jobject) {
  BrowserWorld::Instance().ShutdownJava();
  BrowserWorld::Instance().RegisterDeviceDelegate(nullptr);
  BrowserWorld::Destroy();
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->ShutdownJava();
    sAppContext->mDevice = nullptr;
  }
}

JNI_METHOD(void, drawGL)
(JNIEnv*, jobject) {
  BrowserWorld::Instance().Draw();
}

JNI_METHOD(void, touchEvent)
(JNIEnv*, jobject, jboolean aDown, jfloat aX, jfloat aY) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->TouchEvent(aDown, aX, aY);
  }
}

JNI_METHOD(void, keyEvent)
(JNIEnv*, jobject, jint aKeyCode, jboolean aDown) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->KeyEvent(aKeyCode, aDown);
  }
}

JNI_METHOD(void, setHeadOrientation)
(JNIEnv*, jobject, jfloat aX, jfloat aY, jfloat aZ, jfloat aW) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->SetHeadOrientation(aX, aY, aZ, aW);
  }
}

JNI_METHOD(void, recenterYaw)
(JNIEnv*, jobject) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->RecenterYaw();
  }
}

JNI_METHOD(void, setCameraPosOffset)
(JNIEnv*, jobject, jfloat aX, jfloat aY, jfloat aZ) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->SetCameraPosOffset(aX, aY, aZ);
  }
}

JNI_METHOD(void, setScreenFOV)
(JNIEnv*, jobject, jfloat aFOV) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->SetFOV(aFOV);
  }
}

JNI_METHOD(void, setIPD)
(JNIEnv*, jobject, jfloat aIPD) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->SetIPD(aIPD);
  }
}

JNI_METHOD(void, triggerTelemetryLog)
(JNIEnv*, jobject) {
  if (sAppContext && sAppContext->mDevice) {
    sAppContext->mDevice->LogCurrentTelemetry();
  }
}

JNI_METHOD(void, setFullViewerProfile)
(JNIEnv* aEnv, jobject, jstring aName, jfloat aIPD, jfloat aEyeToScreen, jint aVertAlign, jfloat aTrayToLens, jfloat aFOV, jfloat aK1, jfloat aK2, jfloat aFovL, jfloat aFovR, jfloat aFovT, jfloat aFovB) {
  if (sAppContext && sAppContext->mDevice) {
    const char* nameChars = aEnv->GetStringUTFChars(aName, nullptr);
    std::string nameStr = nameChars ? nameChars : "Cardboard VR";
    if (nameChars) aEnv->ReleaseStringUTFChars(aName, nameChars);
    crow::ViewerProfile profile{nameStr, aIPD, aEyeToScreen, aVertAlign, aTrayToLens, aFOV, aK1, aK2, aFovL, aFovR, aFovT, aFovB};
    sAppContext->mDevice->SetViewerProfile(profile);
  }
}

jint JNI_OnLoad(JavaVM* aVm, void*) {
  if (sAppContext) {
    return JNI_VERSION_1_6;
  }
  sAppContext = std::make_shared<AppContext>();
  sAppContext->mJavaContext.vm = aVm;
  return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM*, void*) {
  sAppContext.reset();
}

} // extern "C"
