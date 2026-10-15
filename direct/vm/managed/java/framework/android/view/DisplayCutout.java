package android.view;

import android.graphics.Rect;
import java.util.Collections;
import java.util.List;

/** A display cutout. The runner's display has none. */
public final class DisplayCutout {
    private DisplayCutout() {}

    public int getSafeInsetTop() {
        return 0;
    }

    public int getSafeInsetBottom() {
        return 0;
    }

    public int getSafeInsetLeft() {
        return 0;
    }

    public int getSafeInsetRight() {
        return 0;
    }

    public List<Rect> getBoundingRects() {
        return Collections.emptyList();
    }
}
