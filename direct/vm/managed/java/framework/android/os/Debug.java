package android.os;

public final class Debug {
    private Debug() {}

    public static class MemoryInfo {
        public int dalvikPss;
        public int dalvikPrivateDirty;
        public int dalvikSharedDirty;
        public int nativePss;
        public int nativePrivateDirty;
        public int nativeSharedDirty;
        public int otherPss;
        public int otherPrivateDirty;
        public int otherSharedDirty;

        public MemoryInfo() {}

        /** The summary keys of dumpsys meminfo, in kilobytes. */
        public java.util.Map<String, String> getMemoryStats() {
            java.util.HashMap<String, String> stats = new java.util.HashMap<String, String>();
            stats.put("summary.java-heap", Integer.toString(dalvikPrivateDirty));
            stats.put("summary.native-heap", Integer.toString(nativePrivateDirty));
            stats.put("summary.code", "0");
            stats.put("summary.stack", "0");
            stats.put("summary.graphics", "0");
            stats.put("summary.private-other", Integer.toString(otherPrivateDirty));
            stats.put("summary.system", "0");
            stats.put("summary.total-pss", Integer.toString(getTotalPss()));
            stats.put("summary.total-swap", "0");
            return stats;
        }

        public int getTotalPss() {
            return dalvikPss + nativePss + otherPss;
        }

        public int getTotalPrivateDirty() {
            return dalvikPrivateDirty + nativePrivateDirty + otherPrivateDirty;
        }

        public int getTotalSharedDirty() {
            return dalvikSharedDirty + nativeSharedDirty + otherSharedDirty;
        }

        public String getMemoryStat(String statName) {
            return null;
        }
    }

    public static void getMemoryInfo(MemoryInfo memoryInfo) {
        memoryInfo.nativePss = (int) (Runtime.getRuntime().totalMemory() / 1024);
    }

    public static long getNativeHeapSize() {
        return Runtime.getRuntime().totalMemory();
    }

    public static long getNativeHeapAllocatedSize() {
        return Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory();
    }

    public static long getNativeHeapFreeSize() {
        return Runtime.getRuntime().freeMemory();
    }

    public static long getPss() {
        return Runtime.getRuntime().totalMemory() / 1024;
    }

    public static boolean isDebuggerConnected() {
        return false;
    }

    public static boolean waitingForDebugger() {
        return false;
    }

    public static void waitForDebugger() {}

    public static long threadCpuTimeNanos() {
        return System.nanoTime();
    }
}
