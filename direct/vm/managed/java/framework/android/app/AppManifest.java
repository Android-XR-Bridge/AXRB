package android.app;

import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.os.Bundle;
import android.os.PatternMatcher;
import android.util.Log;
import android.util.TypedValue;
import java.util.ArrayList;
import java.util.List;
import org.xmlpull.v1.XmlPullParser;

/**
 * The application's manifest as the runtime needs it: application and
 * activity attributes, activity aliases, intent filters and meta-data, read
 * from the APK's binary AndroidManifest.xml through the resource system.
 *
 * Runtime-internal (the package parser's role on Android).
 */
final class AppManifest {
    private static final String TAG = "AppManifest";

    static final class Activity {
        final ActivityInfo info;
        final ArrayList<IntentFilter> filters = new ArrayList<IntentFilter>();

        Activity(ActivityInfo info) {
            this.info = info;
        }
    }

    final String packageName;
    final int versionCode;
    final int versionCodeMajor;
    final String versionName;
    final ApplicationInfo application;
    final ArrayList<Activity> activities = new ArrayList<Activity>();
    final ArrayList<String> requestedPermissions = new ArrayList<String>();
    final ArrayList<String> features = new ArrayList<String>();

    private AppManifest(String packageName, int versionCode, int versionCodeMajor, String versionName,
            ApplicationInfo application) {
        this.packageName = packageName;
        this.versionCode = versionCode;
        this.versionCodeMajor = versionCodeMajor;
        this.versionName = versionName;
        this.application = application;
    }

    // --- queries -------------------------------------------------------------------

    Activity findActivity(String className) {
        for (Activity a : activities) {
            if (a.info.name.equals(className)) return a;
        }
        return null;
    }

    /** The activity an explicit component names, following an alias to its target. */
    ActivityInfo resolveComponent(ComponentName component) {
        if (component == null || !component.getPackageName().equals(packageName)) return null;
        Activity a = findActivity(component.getClassName());
        if (a == null) return null;
        if (a.info.targetActivity != null) {
            Activity target = findActivity(a.info.targetActivity);
            if (target == null) return null;
            ActivityInfo merged = new ActivityInfo(target.info);
            if (a.info.labelRes != 0 || a.info.nonLocalizedLabel != null) {
                merged.labelRes = a.info.labelRes;
                merged.nonLocalizedLabel = a.info.nonLocalizedLabel;
            }
            return merged;
        }
        return a.info;
    }

    /** Activities of this package whose filters match an implicit intent, best match first. */
    List<ResolveInfo> query(Intent intent, int flags) {
        ArrayList<ResolveInfo> out = new ArrayList<ResolveInfo>();
        if (intent.getComponent() != null) {
            ActivityInfo info = resolveComponent(intent.getComponent());
            if (info != null && info.enabled) {
                ResolveInfo ri = new ResolveInfo();
                ri.activityInfo = info;
                out.add(ri);
            }
            return out;
        }
        if (intent.getPackage() != null && !intent.getPackage().equals(packageName)) return out;
        boolean defaultOnly = (flags & PackageManager.MATCH_DEFAULT_ONLY) != 0;
        for (Activity a : activities) {
            if (!a.info.enabled) continue;
            for (IntentFilter f : a.filters) {
                if (defaultOnly && !f.hasCategory(Intent.CATEGORY_DEFAULT)) continue;
                int match = f.match(intent.getAction(), intent.getType(), intent.getScheme(), intent.getData(),
                        intent.getCategories(), TAG);
                if (match < 0) continue;
                ResolveInfo ri = new ResolveInfo();
                ri.activityInfo = a.info.targetActivity != null ? resolveComponent(
                        new ComponentName(packageName, a.info.name)) : a.info;
                if (ri.activityInfo == null) continue;
                ri.filter = f;
                ri.match = match;
                ri.priority = f.getPriority();
                ri.isDefault = f.hasCategory(Intent.CATEGORY_DEFAULT);
                out.add(ri);
                break;
            }
        }
        java.util.Collections.sort(out, new java.util.Comparator<ResolveInfo>() {
            public int compare(ResolveInfo a, ResolveInfo b) {
                if (a.priority != b.priority) return b.priority - a.priority;
                return b.match - a.match;
            }
        });
        return out;
    }

    ActivityInfo launcherActivity() {
        Intent main = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> r = query(main, 0);
        return r.isEmpty() ? null : r.get(0).activityInfo;
    }

    PackageInfo packageInfo(int flags) {
        PackageInfo pi = new PackageInfo();
        pi.packageName = packageName;
        pi.versionCode = versionCode;
        pi.versionCodeMajor = versionCodeMajor;
        pi.versionName = versionName;
        pi.applicationInfo = application;
        pi.firstInstallTime = 0;
        pi.lastUpdateTime = 0;
        if ((flags & PackageManager.GET_ACTIVITIES) != 0) {
            ArrayList<ActivityInfo> list = new ArrayList<ActivityInfo>();
            for (Activity a : activities) list.add(a.info);
            pi.activities = list.toArray(new ActivityInfo[0]);
        }
        if ((flags & PackageManager.GET_PERMISSIONS) != 0 && !requestedPermissions.isEmpty()) {
            pi.requestedPermissions = requestedPermissions.toArray(new String[0]);
            // No runtime permission is ever granted (ContextImpl.checkPermission).
            pi.requestedPermissionsFlags = new int[pi.requestedPermissions.length];
        }
        if ((flags & (PackageManager.GET_SIGNATURES | PackageManager.GET_SIGNING_CERTIFICATES)) != 0) {
            // APK signatures are not verified (spec 13.2): nothing is reported.
            Log.w(TAG, "signing certificates requested; this runtime does not verify or report APK signatures");
        }
        return pi;
    }

    // --- parsing --------------------------------------------------------------------

    static AppManifest parse(Resources res, ApplicationInfo base) throws Exception {
        AssetManager assets = res.getAssets();
        XmlResourceParser p = assets.openXmlResourceParser(AssetManager.COOKIE_APP, "AndroidManifest.xml");
        try {
            return new Reader(res, p, base).read();
        } finally {
            p.close();
        }
    }

    private static final class Reader {
        final Resources res;
        final XmlResourceParser p;
        final ApplicationInfo base;
        String pkg;

        Reader(Resources res, XmlResourceParser p, ApplicationInfo base) {
            this.res = res;
            this.p = p;
            this.base = base;
        }

        int index(int attr) {
            for (int i = 0, n = p.getAttributeCount(); i < n; i++) {
                if (p.getAttributeNameResource(i) == attr) return i;
            }
            return -1;
        }

        String string(int attr) {
            int i = index(attr);
            if (i < 0) return null;
            int ref = p.getAttributeResourceValue(i, 0);
            if (ref != 0) {
                try {
                    return res.getString(ref);
                } catch (Resources.NotFoundException e) {
                    return null;
                }
            }
            return p.getAttributeValue(i);
        }

        int resource(int attr) {
            int i = index(attr);
            return i < 0 ? 0 : p.getAttributeResourceValue(i, 0);
        }

        int integer(int attr, int def) {
            int i = index(attr);
            if (i < 0) return def;
            int ref = p.getAttributeResourceValue(i, 0);
            if (ref != 0) {
                try {
                    return res.getInteger(ref);
                } catch (Resources.NotFoundException e) {
                    return def;
                }
            }
            return p.getAttributeIntValue(i, def);
        }

        boolean bool(int attr, boolean def) {
            int i = index(attr);
            if (i < 0) return def;
            int ref = p.getAttributeResourceValue(i, 0);
            if (ref != 0) {
                try {
                    return res.getBoolean(ref);
                } catch (Resources.NotFoundException e) {
                    return def;
                }
            }
            return p.getAttributeBooleanValue(i, def);
        }

        void label(android.content.pm.PackageItemInfo info) {
            int i = index(android.R.attr.label);
            if (i < 0) return;
            int ref = p.getAttributeResourceValue(i, 0);
            if (ref != 0) {
                info.labelRes = ref;
            } else {
                info.nonLocalizedLabel = p.getAttributeValue(i);
            }
        }

        String className(String name) {
            if (name == null) return null;
            if (name.startsWith(".")) return pkg + name;
            if (name.indexOf('.') < 0) return pkg + "." + name;
            return name;
        }

        AppManifest read() throws Exception {
            int type;
            while ((type = p.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {}
            if (type != XmlPullParser.START_TAG || !"manifest".equals(p.getName())) {
                throw new IllegalStateException("AndroidManifest.xml has no <manifest> root");
            }
            pkg = p.getAttributeValue(null, "package");
            if (pkg == null) pkg = base.packageName;
            int versionCode = integer(android.R.attr.versionCode, 0);
            int versionCodeMajor = integer(android.R.attr.versionCodeMajor, 0);
            String versionName = string(android.R.attr.versionName);
            ApplicationInfo app = new ApplicationInfo(base);
            app.packageName = pkg;
            AppManifest m = new AppManifest(pkg, versionCode, versionCodeMajor, versionName, app);
            int depth = p.getDepth();
            while ((type = p.next()) != XmlPullParser.END_DOCUMENT
                    && (type != XmlPullParser.END_TAG || p.getDepth() > depth)) {
                if (type != XmlPullParser.START_TAG) continue;
                String tag = p.getName();
                if (tag.equals("application")) {
                    readApplication(m);
                } else if (tag.equals("uses-permission") || tag.equals("uses-permission-sdk-23")) {
                    String name = string(android.R.attr.name);
                    if (name != null) m.requestedPermissions.add(name);
                } else if (tag.equals("uses-feature")) {
                    String name = string(android.R.attr.name);
                    if (name != null) m.features.add(name);
                }
            }
            return m;
        }

        void readApplication(AppManifest m) throws Exception {
            ApplicationInfo app = m.application;
            label(app);
            app.icon = resource(android.R.attr.icon);
            int round = resource(android.R.attr.roundIcon);
            if (app.icon == 0) app.icon = round;
            app.logo = resource(android.R.attr.logo);
            app.theme = resource(android.R.attr.theme);
            app.descriptionRes = resource(android.R.attr.description);
            app.taskAffinity = string(android.R.attr.taskAffinity);
            app.permission = string(android.R.attr.permission);
            app.appComponentFactory = className(string(android.R.attr.appComponentFactory));
            String name = className(string(android.R.attr.name));
            if (name != null) {
                app.className = name;
                app.name = name;
            }
            if (bool(android.R.attr.debuggable, false)) app.flags |= ApplicationInfo.FLAG_DEBUGGABLE;
            if (bool(android.R.attr.allowBackup, true)) app.flags |= ApplicationInfo.FLAG_ALLOW_BACKUP;
            if (bool(android.R.attr.largeHeap, false)) app.flags |= ApplicationInfo.FLAG_LARGE_HEAP;
            if (bool(android.R.attr.supportsRtl, false)) app.flags |= ApplicationInfo.FLAG_SUPPORTS_RTL;
            if (bool(android.R.attr.hardwareAccelerated, app.targetSdkVersion >= 14)) {
                app.flags |= ApplicationInfo.FLAG_HARDWARE_ACCELERATED;
            }
            if (bool(android.R.attr.usesCleartextTraffic, app.targetSdkVersion < 28)) {
                app.flags |= ApplicationInfo.FLAG_USES_CLEARTEXT_TRAFFIC;
            }
            int depth = p.getDepth();
            int type;
            while ((type = p.next()) != XmlPullParser.END_DOCUMENT
                    && (type != XmlPullParser.END_TAG || p.getDepth() > depth)) {
                if (type != XmlPullParser.START_TAG) continue;
                String tag = p.getName();
                if (tag.equals("activity")) {
                    readActivity(m, false);
                } else if (tag.equals("activity-alias")) {
                    readActivity(m, true);
                } else if (tag.equals("meta-data")) {
                    if (app.metaData == null) app.metaData = new Bundle();
                    readMetaData(app.metaData);
                }
            }
        }

        void readActivity(AppManifest m, boolean alias) throws Exception {
            ActivityInfo a = new ActivityInfo();
            a.applicationInfo = m.application;
            a.packageName = pkg;
            a.name = className(string(android.R.attr.name));
            if (a.name == null) throw new IllegalStateException("<activity> without android:name");
            label(a);
            a.icon = resource(android.R.attr.icon);
            a.logo = resource(android.R.attr.logo);
            a.theme = resource(android.R.attr.theme);
            a.launchMode = integer(android.R.attr.launchMode, ActivityInfo.LAUNCH_MULTIPLE);
            a.screenOrientation = integer(android.R.attr.screenOrientation, ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
            a.configChanges = integer(android.R.attr.configChanges, 0);
            a.softInputMode = integer(android.R.attr.windowSoftInputMode, 0);
            a.parentActivityName = className(string(android.R.attr.parentActivityName));
            a.taskAffinity = string(android.R.attr.taskAffinity);
            a.permission = string(android.R.attr.permission);
            a.enabled = bool(android.R.attr.enabled, true);
            if (bool(android.R.attr.noHistory, false)) a.flags |= ActivityInfo.FLAG_NO_HISTORY;
            if (bool(android.R.attr.excludeFromRecents, false)) a.flags |= ActivityInfo.FLAG_EXCLUDE_FROM_RECENTS;
            if (alias) a.targetActivity = className(string(android.R.attr.targetActivity));
            Activity activity = new Activity(a);
            boolean hasFilters = false;
            int depth = p.getDepth();
            int type;
            while ((type = p.next()) != XmlPullParser.END_DOCUMENT
                    && (type != XmlPullParser.END_TAG || p.getDepth() > depth)) {
                if (type != XmlPullParser.START_TAG) continue;
                String tag = p.getName();
                if (tag.equals("intent-filter")) {
                    activity.filters.add(readIntentFilter());
                    hasFilters = true;
                } else if (tag.equals("meta-data")) {
                    if (a.metaData == null) a.metaData = new Bundle();
                    readMetaData(a.metaData);
                }
            }
            // exported defaults to "has intent filters" (targetSdk < 31 rule; S+ requires it explicitly).
            a.exported = bool(android.R.attr.exported, hasFilters);
            m.activities.add(activity);
        }

        IntentFilter readIntentFilter() throws Exception {
            IntentFilter f = new IntentFilter();
            f.setPriority(integer(android.R.attr.priority, 0));
            int depth = p.getDepth();
            int type;
            while ((type = p.next()) != XmlPullParser.END_DOCUMENT
                    && (type != XmlPullParser.END_TAG || p.getDepth() > depth)) {
                if (type != XmlPullParser.START_TAG) continue;
                String tag = p.getName();
                if (tag.equals("action")) {
                    String name = string(android.R.attr.name);
                    if (name != null) f.addAction(name);
                } else if (tag.equals("category")) {
                    String name = string(android.R.attr.name);
                    if (name != null) f.addCategory(name);
                } else if (tag.equals("data")) {
                    String scheme = string(android.R.attr.scheme);
                    if (scheme != null) f.addDataScheme(scheme);
                    String mime = string(android.R.attr.mimeType);
                    if (mime != null) f.addDataType(mime);
                    String host = string(android.R.attr.host);
                    if (host != null) f.addDataAuthority(host, string(android.R.attr.port));
                    String path = string(android.R.attr.path);
                    if (path != null) f.addDataPath(path, PatternMatcher.PATTERN_LITERAL);
                    path = string(android.R.attr.pathPrefix);
                    if (path != null) f.addDataPath(path, PatternMatcher.PATTERN_PREFIX);
                    path = string(android.R.attr.pathPattern);
                    if (path != null) f.addDataPath(path, PatternMatcher.PATTERN_SIMPLE_GLOB);
                }
            }
            return f;
        }

        void readMetaData(Bundle into) {
            String name = string(android.R.attr.name);
            if (name == null) return;
            int ri = index(android.R.attr.resource);
            if (ri >= 0) {
                into.putInt(name, p.getAttributeResourceValue(ri, 0));
                return;
            }
            int vi = index(android.R.attr.value);
            if (vi < 0) return;
            if (!(p instanceof android.content.res.XmlBlock.Parser)) {
                into.putString(name, p.getAttributeValue(vi));
                return;
            }
            android.content.res.XmlBlock.Parser xp = (android.content.res.XmlBlock.Parser) p;
            int t = xp.getAttributeDataType(vi);
            int data = xp.getAttributeData(vi);
            if (t == TypedValue.TYPE_STRING) {
                into.putString(name, p.getAttributeValue(vi));
            } else if (t == TypedValue.TYPE_INT_BOOLEAN) {
                into.putBoolean(name, data != 0);
            } else if (t >= TypedValue.TYPE_FIRST_COLOR_INT && t <= TypedValue.TYPE_LAST_COLOR_INT) {
                into.putInt(name, data);
            } else if (t >= TypedValue.TYPE_FIRST_INT && t <= TypedValue.TYPE_LAST_INT) {
                into.putInt(name, data);
            } else if (t == TypedValue.TYPE_FLOAT) {
                into.putFloat(name, Float.intBitsToFloat(data));
            } else if (t == TypedValue.TYPE_REFERENCE) {
                // android:value="@string/x" puts the referenced value (a string), like PackageParser.
                try {
                    TypedValue v = new TypedValue();
                    res.getValue(data, v, true);
                    if (v.type == TypedValue.TYPE_STRING) {
                        into.putString(name, v.coerceToString().toString());
                    } else {
                        into.putInt(name, v.data);
                    }
                } catch (Resources.NotFoundException e) {
                    into.putInt(name, data);
                }
            } else {
                into.putString(name, p.getAttributeValue(vi));
            }
        }
    }
}
