package android.view;

/** Controls the system bars of a window (API 30). */
public interface WindowInsetsController {
    int APPEARANCE_OPAQUE_STATUS_BARS = 1;
    int APPEARANCE_OPAQUE_NAVIGATION_BARS = 1 << 1;
    int APPEARANCE_LOW_PROFILE_BARS = 1 << 2;
    int APPEARANCE_LIGHT_STATUS_BARS = 1 << 3;
    int APPEARANCE_LIGHT_NAVIGATION_BARS = 1 << 4;
    int BEHAVIOR_SHOW_BARS_BY_SWIPE = 0;
    int BEHAVIOR_DEFAULT = 1;
    int BEHAVIOR_SHOW_BARS_BY_TOUCH = 0;
    int BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE = 2;

    interface OnControllableInsetsChangedListener {
        void onControllableInsetsChanged(WindowInsetsController controller, int typeMask);
    }

    void show(int types);

    void hide(int types);

    void setSystemBarsAppearance(int appearance, int mask);

    int getSystemBarsAppearance();

    void setSystemBarsBehavior(int behavior);

    int getSystemBarsBehavior();

    void addOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener);

    void removeOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener);
}
