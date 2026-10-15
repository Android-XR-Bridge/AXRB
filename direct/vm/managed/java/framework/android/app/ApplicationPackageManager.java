package android.app;

import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.FeatureInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageItemInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.util.Log;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;

/**
 * The package manager of a runner session: the application's own package,
 * described by its manifest. Other packages are not visible.
 */
final class ApplicationPackageManager extends PackageManager {
    private static final String TAG = "ApplicationPackageManager";

    private final AppManifest mManifest;
    private final Resources mResources;
    private final HashMap<ComponentName, Integer> mComponentStates = new HashMap<ComponentName, Integer>();

    ApplicationPackageManager(AppManifest manifest, Resources resources) {
        mManifest = manifest;
        mResources = resources;
    }

    private boolean isSelf(String packageName) {
        return mManifest.packageName.equals(packageName);
    }

    @Override
    public PackageInfo getPackageInfo(String packageName, int flags) throws NameNotFoundException {
        if (!isSelf(packageName)) throw new NameNotFoundException(packageName);
        return mManifest.packageInfo(flags);
    }

    @Override
    public ApplicationInfo getApplicationInfo(String packageName, int flags) throws NameNotFoundException {
        if (!isSelf(packageName)) throw new NameNotFoundException(packageName);
        return mManifest.application;
    }

    @Override
    public ActivityInfo getActivityInfo(ComponentName component, int flags) throws NameNotFoundException {
        ActivityInfo info = mManifest.resolveComponent(component);
        if (info == null) throw new NameNotFoundException(component.toString());
        return info;
    }

    @Override
    public List<PackageInfo> getInstalledPackages(int flags) {
        ArrayList<PackageInfo> l = new ArrayList<PackageInfo>();
        l.add(mManifest.packageInfo(flags));
        return l;
    }

    @Override
    public List<ApplicationInfo> getInstalledApplications(int flags) {
        ArrayList<ApplicationInfo> l = new ArrayList<ApplicationInfo>();
        l.add(mManifest.application);
        return l;
    }

    @Override
    public ResolveInfo resolveActivity(Intent intent, int flags) {
        List<ResolveInfo> r = queryIntentActivities(intent, flags);
        return r.isEmpty() ? null : r.get(0);
    }

    @Override
    public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) {
        List<ResolveInfo> all = mManifest.query(intent, flags);
        ArrayList<ResolveInfo> out = new ArrayList<ResolveInfo>();
        for (ResolveInfo ri : all) {
            ComponentName cn = new ComponentName(mManifest.packageName, ri.activityInfo.name);
            Integer state = mComponentStates.get(cn);
            if (state != null && (state == COMPONENT_ENABLED_STATE_DISABLED
                    || state == COMPONENT_ENABLED_STATE_DISABLED_USER)) {
                continue;
            }
            out.add(ri);
        }
        return out;
    }

    @Override
    public List<ResolveInfo> queryBroadcastReceivers(Intent intent, int flags) {
        return Collections.emptyList();
    }

    @Override
    public List<ResolveInfo> queryIntentServices(Intent intent, int flags) {
        return Collections.emptyList();
    }

    @Override
    public Intent getLaunchIntentForPackage(String packageName) {
        if (!isSelf(packageName)) return null;
        ActivityInfo launcher = mManifest.launcherActivity();
        if (launcher == null) return null;
        Intent i = new Intent(Intent.ACTION_MAIN);
        i.addCategory(Intent.CATEGORY_LAUNCHER);
        i.setClassName(packageName, launcher.name);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        return i;
    }

    @Override
    public Intent getLeanbackLaunchIntentForPackage(String packageName) {
        return null;
    }

    @Override
    public CharSequence getApplicationLabel(ApplicationInfo info) {
        return info.loadLabel(this);
    }

    @Override
    public Drawable getApplicationIcon(ApplicationInfo info) {
        return info.loadIcon(this);
    }

    @Override
    public Drawable getApplicationIcon(String packageName) throws NameNotFoundException {
        return getApplicationIcon(getApplicationInfo(packageName, 0));
    }

    @Override
    public Drawable getActivityIcon(ComponentName activityName) throws NameNotFoundException {
        return getActivityInfo(activityName, 0).loadIcon(this);
    }

    @Override
    public Drawable getDefaultActivityIcon() {
        return Resources.getSystem().getDrawable(android.R.drawable.sym_def_app_icon, null);
    }

    @Override
    public CharSequence getText(String packageName, int resid, ApplicationInfo appInfo) {
        if (!isSelf(packageName) && (resid >>> 24) != 0x01) return null;
        try {
            return mResources.getText(resid);
        } catch (Resources.NotFoundException e) {
            Log.w(TAG, "text resource 0x" + Integer.toHexString(resid) + " not found");
            return null;
        }
    }

    @Override
    public Drawable getDrawable(String packageName, int resid, ApplicationInfo appInfo) {
        if (resid == 0) return null;
        if (!isSelf(packageName) && (resid >>> 24) != 0x01) return null;
        try {
            return mResources.getDrawable(resid, null);
        } catch (Resources.NotFoundException e) {
            Log.w(TAG, "drawable resource 0x" + Integer.toHexString(resid) + " not found");
            return null;
        }
    }

    @Override
    public Drawable loadItemIcon(PackageItemInfo itemInfo, ApplicationInfo appInfo) {
        int icon = itemInfo.icon;
        if (icon == 0 && appInfo != null) icon = appInfo.icon;
        Drawable d = icon != 0 ? getDrawable(itemInfo.packageName, icon, appInfo) : null;
        return d != null ? d : getDefaultActivityIcon();
    }

    @Override
    public Resources getResourcesForApplication(ApplicationInfo app) throws NameNotFoundException {
        return getResourcesForApplication(app.packageName);
    }

    @Override
    public Resources getResourcesForApplication(String packageName) throws NameNotFoundException {
        if (!isSelf(packageName)) throw new NameNotFoundException(packageName);
        return mResources;
    }

    private static final String[] SYSTEM_FEATURES = {
        FEATURE_TOUCHSCREEN, FEATURE_FAKETOUCH, FEATURE_SCREEN_PORTRAIT, FEATURE_SCREEN_LANDSCAPE,
    };

    @Override
    public FeatureInfo[] getSystemAvailableFeatures() {
        FeatureInfo[] features = new FeatureInfo[SYSTEM_FEATURES.length + 1];
        for (int i = 0; i < SYSTEM_FEATURES.length; i++) {
            features[i] = new FeatureInfo();
            features[i].name = SYSTEM_FEATURES[i];
        }
        // The unnamed entry carries the OpenGL ES version (3.2), as on Android.
        features[SYSTEM_FEATURES.length] = new FeatureInfo();
        features[SYSTEM_FEATURES.length].reqGlEsVersion = 0x00030002;
        return features;
    }

    @Override
    public boolean hasSystemFeature(String featureName) {
        return hasSystemFeature(featureName, 0);
    }

    @Override
    public boolean hasSystemFeature(String featureName, int version) {
        // The desktop host has a pointer (reported as a touchscreen) and a
        // hardware keyboard; no telephony, camera, sensors or radios.
        for (String feature : SYSTEM_FEATURES) {
            if (feature.equals(featureName)) return true;
        }
        return false;
    }

    @Override
    public int checkPermission(String permName, String packageName) {
        return PERMISSION_DENIED;
    }

    @Override
    public int checkSignatures(String packageName1, String packageName2) {
        if (!isSelf(packageName1) || !isSelf(packageName2)) return SIGNATURE_UNKNOWN_PACKAGE;
        // Signatures are not verified (spec 13.2): no claim that the package is signed.
        return SIGNATURE_NEITHER_SIGNED;
    }

    @Override
    public String getInstallerPackageName(String packageName) {
        if (!isSelf(packageName)) throw new IllegalArgumentException("Unknown package: " + packageName);
        return null;
    }

    @Override
    public String[] getPackagesForUid(int uid) {
        return uid == android.os.Process.myUid() ? new String[] {mManifest.packageName} : null;
    }

    @Override
    public String getNameForUid(int uid) {
        return uid == android.os.Process.myUid() ? mManifest.packageName : null;
    }

    @Override
    public void setComponentEnabledSetting(ComponentName componentName, int newState, int flags) {
        if (!isSelf(componentName.getPackageName())) {
            throw new SecurityException("Cannot change the state of another package's component");
        }
        mComponentStates.put(componentName, newState);
    }

    @Override
    public int getComponentEnabledSetting(ComponentName componentName) {
        Integer s = mComponentStates.get(componentName);
        return s != null ? s : COMPONENT_ENABLED_STATE_DEFAULT;
    }

    @Override
    public void setApplicationEnabledSetting(String packageName, int newState, int flags) {
        throw new SecurityException("Cannot change the enabled state of a package");
    }

    @Override
    public int getApplicationEnabledSetting(String packageName) {
        if (!isSelf(packageName)) throw new IllegalArgumentException("Unknown package: " + packageName);
        return COMPONENT_ENABLED_STATE_DEFAULT;
    }

    @Override
    public boolean isSafeMode() {
        return false;
    }

    @Override
    public boolean isInstantApp() {
        return false;
    }

    @Override
    public int getPackageUid(String packageName, int flags) throws NameNotFoundException {
        if (!isSelf(packageName)) throw new NameNotFoundException(packageName);
        return android.os.Process.myUid();
    }

    @Override
    public List<IntentFilter> getIntentFilters(ComponentName component) {
        AppManifest.Activity a = mManifest.findActivity(component.getClassName());
        return a != null ? new ArrayList<IntentFilter>(a.filters) : new ArrayList<IntentFilter>();
    }
}
