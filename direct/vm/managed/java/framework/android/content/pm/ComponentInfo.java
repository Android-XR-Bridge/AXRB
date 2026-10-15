package android.content.pm;

public class ComponentInfo extends PackageItemInfo {
    public ApplicationInfo applicationInfo;
    public String processName;
    public String splitName;
    public int descriptionRes;
    public boolean enabled = true;
    public boolean exported = false;
    public boolean directBootAware = false;

    public ComponentInfo() {}

    public ComponentInfo(ComponentInfo orig) {
        super(orig);
        applicationInfo = orig.applicationInfo;
        processName = orig.processName;
        splitName = orig.splitName;
        descriptionRes = orig.descriptionRes;
        enabled = orig.enabled;
        exported = orig.exported;
        directBootAware = orig.directBootAware;
    }

    @Override
    public CharSequence loadLabel(PackageManager pm) {
        if (nonLocalizedLabel != null) return nonLocalizedLabel;
        if (labelRes != 0) {
            CharSequence label = pm.getText(packageName, labelRes, applicationInfo);
            if (label != null) return label;
        }
        if (applicationInfo != null) return applicationInfo.loadLabel(pm);
        return name;
    }

    public boolean isEnabled() {
        return enabled && (applicationInfo == null || applicationInfo.enabled);
    }

    public final int getIconResource() {
        return icon != 0 ? icon : (applicationInfo != null ? applicationInfo.icon : 0);
    }

    public final int getLogoResource() {
        return logo != 0 ? logo : (applicationInfo != null ? applicationInfo.logo : 0);
    }

    public final int getBannerResource() {
        return banner != 0 ? banner : (applicationInfo != null ? applicationInfo.banner : 0);
    }

    @Override
    ApplicationInfo getApplicationInfo() {
        return applicationInfo;
    }
}
