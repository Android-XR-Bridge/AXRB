package com.axrb.runtime;

import android.app.Activity;
import android.graphics.SurfaceTexture;
import android.graphics.PixelFormat;
import android.view.*;
import java.util.*;

/** Adapts standard Android game surfaces to the existing Windows renderer. */
final class HostWindow {
    static { System.load("/system/lib64/libaxrb_art_bridge.so"); }
    private static native long registerSurface(Surface surface, int width, int height);
    private static native void releaseSurface(long window);
    private final List<SurfaceTexture> textures = new ArrayList<>();
    private final List<Long> windows = new ArrayList<>();
    private final Set<View> attached = Collections.newSetFromMap(new IdentityHashMap<>());

    void attach(Activity activity) throws Exception {
        View decor = activity.getWindow().getDecorView();
        decor.measure(View.MeasureSpec.makeMeasureSpec(1920, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY));
        decor.layout(0, 0, 1920, 1080);
        attachSurfaces(decor);
        decor.getViewTreeObserver().dispatchOnGlobalLayout();
        activity.onWindowFocusChanged(true);
        decor.dispatchWindowFocusChanged(true);
        System.out.println("AXRB_AOSP_HOST_SURFACES " + windows.size());
        if (Boolean.getBoolean("axrb.inspectViews")) inspect(decor);
    }

    private static void inspect(View view) throws Exception {
        Class<?> type = view.getClass();
        if (!type.getName().startsWith("android.") && !type.getName().startsWith("com.android.")) {
            for (java.lang.reflect.Field field : type.getDeclaredFields()) {
                field.setAccessible(true);
                Object value = field.get(view);
                System.out.println("AXRB_VIEW " + type.getName() + "." + field.getName() + "=" + value);
                if (value != null && value.getClass().getPackage().equals(type.getPackage()))
                    for (java.lang.reflect.Field inner : value.getClass().getDeclaredFields()) if (inner.getType().isPrimitive()) {
                        inner.setAccessible(true);
                        System.out.println("AXRB_VIEW_STATE " + field.getName() + "." + inner.getName() + "=" + inner.get(value));
                    }
            }
        }
        if (view instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) view).getChildCount(); ++i)
            inspect(((ViewGroup) view).getChildAt(i));
    }

    private void attachSurfaces(View view) throws Exception {
        if (view instanceof SurfaceView && attached.add(view)) {
            SurfaceView surfaceView = (SurfaceView) view;
            SurfaceHolder holder = surfaceView.getHolder();
            SurfaceTexture texture = new SurfaceTexture(false);
            texture.setDefaultBufferSize(1920, 1080);
            textures.add(texture);
            Surface surface = new Surface(texture);
            Reflect.call(holder.getSurface(), "transferFrom", surface);
            holder.getSurfaceFrame().set(0, 0, 1920, 1080);
            long window = registerSurface(holder.getSurface(), 1920, 1080);
            if (window == 0) throw new IllegalStateException("Cannot bridge Android surface");
            windows.add(window);
            @SuppressWarnings("unchecked")
            List<SurfaceHolder.Callback> callbacks = new ArrayList<>((List<SurfaceHolder.Callback>) Reflect.field(surfaceView, "mCallbacks"));
            for (SurfaceHolder.Callback callback : callbacks) {
                callback.surfaceCreated(holder);
                callback.surfaceChanged(holder, PixelFormat.RGBA_8888, 1920, 1080);
            }
            for (SurfaceHolder.Callback callback : callbacks) if (callback instanceof SurfaceHolder.Callback2)
                ((SurfaceHolder.Callback2) callback).surfaceRedrawNeededAsync(holder, () -> {});
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) attachSurfaces(group.getChildAt(i));
        }
    }
}
