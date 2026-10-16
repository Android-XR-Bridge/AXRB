package com.axrb.companionprobe;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Bundle;
public final class CompanionApkProbe extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            String name = "com.axrb.companionfixture";
            boolean visible = getPackageManager().getPackageInfo(name, 0) != null;
            Context context = createPackageContext(name, Context.CONTEXT_INCLUDE_CODE | Context.CONTEXT_IGNORE_SECURITY);
            Object value = context.getClassLoader().loadClass(name + ".CompanionHelper").getMethod("value").invoke(null);
            boolean main = false;
            for (ResolveInfo info : getPackageManager().queryIntentActivities(new Intent(Intent.ACTION_MAIN), 0))
                if (name.equals(info.activityInfo.packageName)) main = true;
            System.out.println("COMPANION_APK_PROBE visible=" + visible + " context=" + (context != null) +
                    " class=" + "companion-loaded".equals(value) + " main=" + main);
        } catch (Throwable error) {
            System.out.println("COMPANION_APK_PROBE_FAILED " + error);
            error.printStackTrace(System.out);
        }
        System.exit(0);
    }
}
