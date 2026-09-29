package com.axrb.dynarmicprobe;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
public class MainActivity extends Activity {
    static { System.loadLibrary("dynarmicprobe"); }
    private static native String run();
    private static native String benchmark();
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i("AXRB.DynarmicProbe", getIntent().getBooleanExtra("benchmark", false) ? benchmark() : run());
        finish();
    }
}
