package android.content;

import android.content.res.Configuration;

public interface ComponentCallbacks {
    void onConfigurationChanged(Configuration newConfig);

    void onLowMemory();
}
