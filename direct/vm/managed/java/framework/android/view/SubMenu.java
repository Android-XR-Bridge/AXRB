package android.view;

import android.graphics.drawable.Drawable;

public interface SubMenu extends Menu {
    SubMenu setHeaderTitle(int titleRes);

    SubMenu setHeaderTitle(CharSequence title);

    SubMenu setHeaderIcon(int iconRes);

    SubMenu setHeaderIcon(Drawable icon);

    SubMenu setHeaderView(View view);

    void clearHeader();

    SubMenu setIcon(int iconRes);

    SubMenu setIcon(Drawable icon);

    MenuItem getItem();
}
