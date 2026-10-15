package android.view;

import android.graphics.drawable.Drawable;

public interface ContextMenu extends Menu {
    ContextMenu setHeaderTitle(int titleRes);

    ContextMenu setHeaderTitle(CharSequence title);

    ContextMenu setHeaderIcon(int iconRes);

    ContextMenu setHeaderIcon(Drawable icon);

    ContextMenu setHeaderView(View view);

    void clearHeader();

    public interface ContextMenuInfo {}
}
