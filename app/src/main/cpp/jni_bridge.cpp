// JNI glue between GameActivity/EngineBridge (Kotlin) and mge::Engine.
// This file is the only place engine code meets JNI types (P3 boundary).

#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "mge/core/log.h"
#include "mge/framework/engine.h"

namespace {

mge::Engine gEngine;
ANativeWindow* gWindow = nullptr;

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeInit(JNIEnv*, jobject) {
    mge::EngineConfig config;
    if (!gEngine.init(config)) {
        MGE_LOGE("jni", "engine init failed");
    }
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeShutdown(JNIEnv*, jobject) {
    gEngine.shutdown();
    if (gWindow != nullptr) {
        ANativeWindow_release(gWindow);
        gWindow = nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeSurfaceCreated(
    JNIEnv* env, jobject, jobject surface, jint width, jint height) {
    if (gWindow != nullptr) ANativeWindow_release(gWindow);
    gWindow = ANativeWindow_fromSurface(env, surface);
    gEngine.onSurfaceCreated(width, height);
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeSurfaceChanged(
    JNIEnv*, jobject, jint width, jint height) {
    gEngine.onSurfaceChanged(width, height);
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeSurfaceDestroyed(JNIEnv*, jobject) {
    gEngine.onSurfaceLost();
    if (gWindow != nullptr) {
        ANativeWindow_release(gWindow);
        gWindow = nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativePause(JNIEnv*, jobject) {
    gEngine.onPause();
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeResume(JNIEnv*, jobject) {
    gEngine.onResume();
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeTick(JNIEnv*, jobject, jdouble dtSeconds) {
    gEngine.tick(dtSeconds);
}

JNIEXPORT void JNICALL
Java_com_mobilegamesengine_app_EngineBridge_nativeTouchEvent(
    JNIEnv*, jobject, jint pointerId, jint action, jfloat x, jfloat y, jlong timestampNs) {
    // MotionEvent action constants: 0=DOWN, 1=UP, 2=MOVE, 3=CANCEL,
    // 5=POINTER_DOWN, 6=POINTER_UP.
    mge::TouchAction touchAction;
    switch (action) {
        case 0:
        case 5: touchAction = mge::TouchAction::Down; break;
        case 1:
        case 6: touchAction = mge::TouchAction::Up; break;
        case 2: touchAction = mge::TouchAction::Move; break;
        default: touchAction = mge::TouchAction::Cancel; break;
    }
    mge::TouchEvent event;
    event.pointerId = pointerId;
    event.action = touchAction;
    event.x = x;
    event.y = y;
    event.timestampNs = timestampNs;
    gEngine.pushTouchEvent(event);
}

}  // extern "C"
