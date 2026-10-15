package com.axrb.permissionprobe;

import android.app.Activity;
import android.app.Fragment;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.pm.PackageManager;

/** Runtime requests must complete for both Unity's Fragment and Activity APIs. */
public final class PermissionProbe extends Activity {
    private static final String[] PERMISSIONS = {"android.permission.READ_EXTERNAL_STORAGE", "android.permission.CAMERA"};
    private static void verify(int code, int expected, String[] names, int[] values) {
        if (code != expected || !java.util.Arrays.equals(names, PERMISSIONS)
                || !java.util.Arrays.equals(values, new int[]{PackageManager.PERMISSION_GRANTED, PackageManager.PERMISSION_DENIED}))
            throw new AssertionError("Wrong permission callback " + code + ": " + java.util.Arrays.toString(values));
    }
    public static final class RequestFragment extends Fragment {
        @Override public void onCreate(Bundle state) {
            super.onCreate(state);
            requestPermissions(PERMISSIONS, 101);
        }
        @Override public void onRequestPermissionsResult(int code, String[] names, int[] values) {
            verify(code, 101, names, values);
            if (shouldShowRequestPermissionRationale(PERMISSIONS[1]))
                throw new AssertionError("Unsupported camera has no permission dialog");
            System.out.println("PERMISSION_FRAGMENT_PASS");
            getActivity().requestPermissions(PERMISSIONS, 102);
        }
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getFragmentManager().beginTransaction().add(new RequestFragment(), "permissions").commit();
        new Handler(Looper.getMainLooper()).postDelayed(() -> System.exit(2), 10000);
    }
    @Override public void onRequestPermissionsResult(int code, String[] names, int[] values) {
        verify(code, 102, names, values);
        if (checkSelfPermission(PERMISSIONS[1]) != PackageManager.PERMISSION_DENIED)
            throw new AssertionError("Unsupported camera permission was granted");
        System.out.println("PERMISSION_PROBE_PASS");
        System.exit(0);
    }
}
