package android.view;

import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.PorterDuff;
import android.graphics.drawable.Drawable;

public interface MenuItem {
    int SHOW_AS_ACTION_NEVER = 0;
    int SHOW_AS_ACTION_IF_ROOM = 1;
    int SHOW_AS_ACTION_ALWAYS = 2;
    int SHOW_AS_ACTION_WITH_TEXT = 4;
    int SHOW_AS_ACTION_COLLAPSE_ACTION_VIEW = 8;

    public interface OnMenuItemClickListener {
        boolean onMenuItemClick(MenuItem item);
    }

    public interface OnActionExpandListener {
        boolean onMenuItemActionExpand(MenuItem item);

        boolean onMenuItemActionCollapse(MenuItem item);
    }

    int getItemId();

    int getGroupId();

    int getOrder();

    MenuItem setTitle(CharSequence title);

    MenuItem setTitle(int title);

    CharSequence getTitle();

    MenuItem setTitleCondensed(CharSequence title);

    CharSequence getTitleCondensed();

    MenuItem setIcon(Drawable icon);

    MenuItem setIcon(int iconRes);

    Drawable getIcon();

    MenuItem setIconTintList(ColorStateList tint);

    ColorStateList getIconTintList();

    MenuItem setIconTintMode(PorterDuff.Mode tintMode);

    PorterDuff.Mode getIconTintMode();

    MenuItem setIntent(Intent intent);

    Intent getIntent();

    MenuItem setShortcut(char numericChar, char alphaChar);

    MenuItem setNumericShortcut(char numericChar);

    char getNumericShortcut();

    MenuItem setAlphabeticShortcut(char alphaChar);

    char getAlphabeticShortcut();

    MenuItem setCheckable(boolean checkable);

    boolean isCheckable();

    MenuItem setChecked(boolean checked);

    boolean isChecked();

    MenuItem setVisible(boolean visible);

    boolean isVisible();

    MenuItem setEnabled(boolean enabled);

    boolean isEnabled();

    boolean hasSubMenu();

    SubMenu getSubMenu();

    MenuItem setOnMenuItemClickListener(OnMenuItemClickListener menuItemClickListener);

    ContextMenu.ContextMenuInfo getMenuInfo();

    void setShowAsAction(int actionEnum);

    MenuItem setShowAsActionFlags(int actionEnum);

    MenuItem setActionView(View view);

    MenuItem setActionView(int resId);

    View getActionView();

    MenuItem setActionProvider(ActionProvider actionProvider);

    ActionProvider getActionProvider();

    boolean expandActionView();

    boolean collapseActionView();

    boolean isActionViewExpanded();

    MenuItem setOnActionExpandListener(OnActionExpandListener listener);

    MenuItem setContentDescription(CharSequence contentDescription);

    CharSequence getContentDescription();

    MenuItem setTooltipText(CharSequence tooltipText);

    CharSequence getTooltipText();
}
