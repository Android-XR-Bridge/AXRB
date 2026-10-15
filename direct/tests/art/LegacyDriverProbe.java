package com.axrb.legacyprobe;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import java.lang.reflect.Method;

/** Loads the runtime the way older Oculus OpenXR loaders do: through com.oculus.systemdriver. */
public final class LegacyDriverProbe extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            Context driver = createPackageContext("com.oculus.systemdriver",
                    Context.CONTEXT_INCLUDE_CODE | Context.CONTEXT_IGNORE_SECURITY);
            Class<?> loader = driver.getClassLoader().loadClass("com.oculus.systemdriver.DriverLoader");
            loader.getMethod("loadXrRuntime").invoke(null);
            Method lookup = loader.getMethod("getProcAddr", String.class);
            System.out.println("LEGACY_DRIVER_PROBE manufacturer=" + Build.MANUFACTURER + " brand=" + Build.BRAND +
                    " model=" + Build.MODEL +
                    " gipa=" + ((Long) lookup.invoke(null, "xrGetInstanceProcAddr") != 0) +
                    " negotiate=" + ((Long) lookup.invoke(null, "xrNegotiateLoaderRuntimeInterface") != 0) +
                    " initialize=" + ((Long) lookup.invoke(null, "xrInitializeLoaderKHR") != 0) +
                    " missing=" + ((Long) lookup.invoke(null, "xrAxrbNoSuchFunction") != 0));
        } catch (Throwable error) {
            System.out.println("LEGACY_DRIVER_PROBE_FAILED " + error);
            error.printStackTrace(System.out);
        }
        System.exit(0);
    }
}
