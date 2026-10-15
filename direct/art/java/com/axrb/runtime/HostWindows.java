package com.axrb.runtime;

import android.graphics.Point;
import android.graphics.Rect;
import android.os.IBinder;

/** Window/display policy for the Direct host's single application window. */
final class HostWindows {
    static void install(HostServices services) throws Exception {
        IBinder session = services.register("axrb.window_session", "android.view.IWindowSession", (method, args) -> {
            switch (method.getName()) {
                case "addToDisplayAsUser":
                    Object root = ((java.lang.ref.WeakReference<?>) Reflect.field(args[0], "mViewAncestor")).get();
                    logText((android.view.View) Reflect.field(root, "mView"));
                    throw new UnsupportedOperationException(method.getName());
                case "getInTouchMode": return false;
                case "setInTouchMode": case "updateRequestedVisibleTypes": case "setInsets":
                case "reportSystemGestureExclusionChanged": case "reportKeepClearAreasChanged":
                    return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
        services.register("window", "android.view.IWindowManager", (method, args) -> {
            switch (method.getName()) {
                case "hasNavigationBar": case "isKeyguardLocked": case "isKeyguardSecure":
                case "isSafeModeEnabled": case "isWindowTraceEnabled": case "isLayerTracing": return false;
                case "getCurrentAnimatorScale": case "getAnimationScale": return 1f;
                case "getAnimationScales": return new float[]{1f, 1f, 1f};
                case "getInitialDisplaySize": case "getBaseDisplaySize":
                    ((Point) args[1]).set(1920, 1080); return null;
                case "getInitialDisplayDensity": case "getBaseDisplayDensity": return 320;
                case "getDefaultDisplayRotation": return 0;
                case "getWindowingMode": return 1;
                case "openSession": return session.queryLocalInterface("android.view.IWindowSession");
                case "getWindowInsets":
                    for (Object arg : args) if (arg != null && arg.getClass().getName().equals("android.view.InsetsState"))
                        ((Rect) Reflect.call(arg, "getDisplayFrame")).set(0, 0, 1920, 1080);
                    return false;
                case "registerCrossWindowBlurEnabledListener": return false;
                case "registerDisplayWindowListener": return new int[]{0};
                case "registerTaskFpsCallback": case "unregisterTaskFpsCallback": return null;
                default: throw new UnsupportedOperationException(method.getName());
            }
        });
    }

    private static void logText(android.view.View view) {
        if (view instanceof android.widget.TextView)
            System.err.println("AXRB_WINDOW_TEXT " + ((android.widget.TextView) view).getText());
        if (view instanceof android.view.ViewGroup) {
            android.view.ViewGroup group = (android.view.ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) logText(group.getChildAt(i));
        }
    }
}
