# Adapted from Refract's MIT-licensed P-core selection. Leave homogeneous and
# multi-group hosts to Windows; never override an existing process restriction.
if (-not ('AxrbProcessorTopology' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AxrbProcessorTopology {
    [DllImport("kernel32.dll")] static extern bool GetLogicalProcessorInformationEx(int relation, IntPtr buffer, ref int length);
    public static long PerformanceMask() {
        if (IntPtr.Size != 8) return 0;
        int length = 0;
        GetLogicalProcessorInformationEx(0, IntPtr.Zero, ref length);
        if (length <= 0) return 0;
        IntPtr buffer = Marshal.AllocHGlobal(length);
        try {
            if (!GetLogicalProcessorInformationEx(0, buffer, ref length)) return 0;
            long mask = 0; int best = -1, worst = 256;
            for (int offset = 0; offset < length;) {
                IntPtr entry = IntPtr.Add(buffer, offset);
                int size = Marshal.ReadInt32(entry, 4);
                if (size < 48 || size > length - offset) return 0;
                if (Marshal.ReadInt16(entry, 30) != 1 || Marshal.ReadInt16(entry, 40) != 0) return 0;
                int efficiency = Marshal.ReadByte(entry, 9);
                worst = Math.Min(worst, efficiency);
                if (efficiency > best) { best = efficiency; mask = 0; }
                if (efficiency == best) mask |= Marshal.ReadInt64(entry, 32);
                offset += size;
            }
            return best > worst ? mask : 0;
        } finally { Marshal.FreeHGlobal(buffer); }
    }
}
'@
}
function Get-AxrbPerformanceCoreMask {
    [AxrbProcessorTopology]::PerformanceMask()
}
