package android.app;

import android.content.Context;
import android.os.Debug;
import java.util.ArrayList;
import java.util.List;

public class ActivityManager {
    private final Context mContext;

    ActivityManager(Context context) {
        mContext = context;
    }

    public static class MemoryInfo {
        public long availMem;
        public long totalMem;
        public long threshold;
        public boolean lowMemory;

        public MemoryInfo() {}
    }

    public static class RunningAppProcessInfo {
        public static final int IMPORTANCE_FOREGROUND = 100;
        public static final int IMPORTANCE_VISIBLE = 200;
        public static final int IMPORTANCE_SERVICE = 300;
        public static final int IMPORTANCE_CACHED = 400;
        public static final int IMPORTANCE_GONE = 1000;

        public String processName;
        public int pid;
        public int uid;
        public String[] pkgList;
        public int importance = IMPORTANCE_FOREGROUND;
        public int lastTrimLevel;
        public int lru;

        public RunningAppProcessInfo() {}

        public RunningAppProcessInfo(String processName, int pid, String[] pkgList) {
            this.processName = processName;
            this.pid = pid;
            this.pkgList = pkgList;
        }
    }

    public static class RunningTaskInfo {
        public RunningTaskInfo() {}
    }

    public static native long nativeTotalMemory();

    public static native long nativeAvailableMemory();

    public void getMemoryInfo(MemoryInfo outInfo) {
        outInfo.totalMem = nativeTotalMemory();
        outInfo.availMem = nativeAvailableMemory();
        outInfo.threshold = 256L * 1024 * 1024;
        outInfo.lowMemory = outInfo.availMem < outInfo.threshold;
    }

    public static void getMyMemoryState(RunningAppProcessInfo outState) {
        outState.pid = android.os.Process.myPid();
        outState.uid = android.os.Process.myUid();
        outState.importance = RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
    }

    public List<RunningAppProcessInfo> getRunningAppProcesses() {
        ArrayList<RunningAppProcessInfo> list = new ArrayList<RunningAppProcessInfo>();
        String pkg = mContext.getPackageName();
        list.add(new RunningAppProcessInfo(pkg, android.os.Process.myPid(), new String[] {pkg}));
        return list;
    }

    public Debug.MemoryInfo[] getProcessMemoryInfo(int[] pids) {
        Debug.MemoryInfo[] out = new Debug.MemoryInfo[pids.length];
        for (int i = 0; i < pids.length; i++) {
            out[i] = new Debug.MemoryInfo();
            Debug.getMemoryInfo(out[i]);
        }
        return out;
    }

    public int getMemoryClass() {
        return 512;
    }

    public int getLargeMemoryClass() {
        return 1024;
    }

    public boolean isLowRamDevice() {
        return false;
    }

    public static boolean isUserAMonkey() {
        return false;
    }

    public static boolean isRunningInTestHarness() {
        return false;
    }

    public android.content.pm.ConfigurationInfo getDeviceConfigurationInfo() {
        android.content.pm.ConfigurationInfo info = new android.content.pm.ConfigurationInfo();
        info.reqGlEsVersion = 0x00030002;
        return info;
    }

    public List<RunningTaskInfo> getRunningTasks(int maxNum) {
        return new ArrayList<RunningTaskInfo>();
    }
}
