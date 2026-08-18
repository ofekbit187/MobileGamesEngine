package com.mobilegamesengine.app

import android.view.Surface

/**
 * JNI boundary to the native engine (task 1.1). One method per engine
 * lifecycle event — the Kotlin shell owns nothing but forwarding (P3):
 * the engine core never sees Android types beyond the Surface handle.
 */
object EngineBridge {
    init {
        System.loadLibrary("mge_app")
    }

    external fun nativeInit()
    external fun nativeShutdown()

    external fun nativeSurfaceCreated(surface: Surface, width: Int, height: Int)
    external fun nativeSurfaceChanged(width: Int, height: Int)
    external fun nativeSurfaceDestroyed()

    external fun nativePause()
    external fun nativeResume()

    /** One display frame; [dtSeconds] is real elapsed time since the last call. */
    external fun nativeTick(dtSeconds: Double)

    external fun nativeTouchEvent(pointerId: Int, action: Int, x: Float, y: Float, timestampNs: Long)
}
