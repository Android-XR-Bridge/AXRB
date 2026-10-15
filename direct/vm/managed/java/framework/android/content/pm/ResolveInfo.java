package android.content.pm;

import android.content.IntentFilter;
import android.graphics.drawable.Drawable;

public class ResolveInfo {
    public ActivityInfo activityInfo;
    public IntentFilter filter;
    public int priority;
    public int preferredOrder;
    public int match;
    public int specificIndex = -1;
    public boolean isDefault;
    public int labelRes;
    public CharSequence nonLocalizedLabel;
    public int icon;
    public String resolvePackageName;

    public ResolveInfo() {}

    public ResolveInfo(ResolveInfo orig) {
        activityInfo = orig.activityInfo;
        filter = orig.filter;
        priority = orig.priority;
        preferredOrder = orig.preferredOrder;
        match = orig.match;
        specificIndex = orig.specificIndex;
        isDefault = orig.isDefault;
        labelRes = orig.labelRes;
        nonLocalizedLabel = orig.nonLocalizedLabel;
        icon = orig.icon;
        resolvePackageName = orig.resolvePackageName;
    }

    public CharSequence loadLabel(PackageManager pm) {
        if (nonLocalizedLabel != null) return nonLocalizedLabel;
        return activityInfo != null ? activityInfo.loadLabel(pm) : null;
    }

    public Drawable loadIcon(PackageManager pm) {
        return activityInfo != null ? activityInfo.loadIcon(pm) : null;
    }

    public final int getIconResource() {
        if (icon != 0) return icon;
        return activityInfo != null ? activityInfo.getIconResource() : 0;
    }

    @Override
    public String toString() {
        return "ResolveInfo{" + Integer.toHexString(System.identityHashCode(this)) + " "
                + (activityInfo != null ? activityInfo.name : "?") + " m=0x" + Integer.toHexString(match) + "}";
    }
}
