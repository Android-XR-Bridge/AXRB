package android.view;

import android.content.Context;

/** Menus are never shown by the runtime; inflation leaves the menu empty. */
public class MenuInflater {
    public MenuInflater(Context context) {}

    public void inflate(int menuRes, Menu menu) {}
}
