package com.mobilegamesengine.app

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * The engine's Android entry point (tasks 1.1/1.2). Hosts a SurfaceView,
 * translates the Android lifecycle into engine events, and drives one
 * engine tick per display frame via Choreographer.
 */
class GameActivity : Activity(), SurfaceHolder.Callback, Choreographer.FrameCallback {

    private lateinit var surfaceView: SurfaceView
    private var lastFrameNanos = 0L
    private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        EngineBridge.nativeInit()
        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        setContentView(surfaceView)
    }

    override fun onResume() {
        super.onResume()
        EngineBridge.nativeResume()
        running = true
        lastFrameNanos = 0L
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onPause() {
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
        EngineBridge.nativePause()
        super.onPause()
    }

    override fun onDestroy() {
        EngineBridge.nativeShutdown()
        super.onDestroy()
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        val dtSeconds =
            if (lastFrameNanos == 0L) 0.0 else (frameTimeNanos - lastFrameNanos) / 1_000_000_000.0
        lastFrameNanos = frameTimeNanos
        EngineBridge.nativeTick(dtSeconds)
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val frame = holder.surfaceFrame
        EngineBridge.nativeSurfaceCreated(holder.surface, frame.width(), frame.height())
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        EngineBridge.nativeSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        EngineBridge.nativeSurfaceDestroyed()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val actionMasked = event.actionMasked
        val index = event.actionIndex
        EngineBridge.nativeTouchEvent(
            event.getPointerId(index),
            actionMasked,
            event.getX(index),
            event.getY(index),
            event.eventTime * 1_000_000,
        )
        return true
    }
}
