package android.hardware.display;

import android.content.Context;
import android.os.Handler;
import android.view.Display;

public final class DisplayManager {
    public static final String DISPLAY_CATEGORY_PRESENTATION = "android.hardware.display.category.PRESENTATION";

    public interface DisplayListener {
        void onDisplayAdded(int displayId);

        void onDisplayRemoved(int displayId);

        void onDisplayChanged(int displayId);
    }

    public DisplayManager(Context context) {}

    public Display getDisplay(int displayId) {
        return displayId == Display.DEFAULT_DISPLAY ? Display.getDefault() : null;
    }

    public Display[] getDisplays() {
        return new Display[] {Display.getDefault()};
    }

    public Display[] getDisplays(String category) {
        return DISPLAY_CATEGORY_PRESENTATION.equals(category) ? new Display[0] : getDisplays();
    }

    public void registerDisplayListener(DisplayListener listener, Handler handler) {}

    public void unregisterDisplayListener(DisplayListener listener) {}
}
